/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * test_oos_eager_diagnostics.cpp - eager OOS cleanup of non-MVCC heap DML (CBRD-26950)
 *
 * SA_MODE has no vacuum: heap_delete_logical and heap_update_logical reclaim the OOS value chains of the
 * record they replace synchronously, through heap_oos_delete_unreferenced. Every heap operation is a
 * non-MVCC operation in SA_MODE, so real DML on a real heap reaches the eager entry points inside
 * heap_file.c. These tests observe DML completion, the error stack, the skipped-cleanup diagnostic (its
 * count, the outcome it names and its arrival in the server error log) and the survival of whatever
 * occupies a stale reference's location. Vacuum's own callers stay quiet; see test_oos_vacuum_server.
 * That eager reclamation of an unskipped reference actually frees OOS space is covered at the SQL level
 * by test_oos_sql_eager_cleanup; this binary is about what a SKIPPED reclamation reports.
 */

#include "gtest/gtest.h"
#include <cstring>
#include <string>
#include <vector>

#include "error_manager.h"
#include "file_manager.h"
#include "heap_file.h"
#include "heap_oos.hpp"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "object_representation.h"
#include "oos_file.hpp"
#include "oos_log.hpp"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "xserver_interface.h"
#include "test_oos_common.hpp"
#include "test_oos_error_log.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;
using test_oos_error_log::error_log_contains_since;
using test_oos_error_log::error_log_mentions_since;
using test_oos_error_log::error_log_size;

/* bridge to a static function in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

namespace
{
  const int HEAP_HDR_SIZE = OR_MVCC_REP_SIZE + OR_CHN_SIZE;
  const int VOT_ENTRY_SZ = OR_INT_SIZE;	/* 4-byte offset mode */
  /* heap_insert_adjust_recdes_header may grow the header in place for the insert MVCCID, and the update
   * path once more for the previous version LSA; the records go through real DML, so leave room. */
  const int MVCC_HEADER_SPARE = 2 * OR_MVCCID_SIZE;

  /* Builds a heap record without fixed attributes whose variable attributes are the given OOS inline stubs,
   * stamped exactly as the caller says (a stale stamp is as easy to store as a true one), optionally
   * followed by one ordinary inline attribute of filler_size bytes that lets a test control the record's
   * size and so where the heap stores it. */
  int
  build_record_with_stubs (const std::vector<oos_chain_ref> &refs, const std::vector<INT64> &lengths,
			   RECDES &rec_out, int filler_size = 0)
  {
    const int n_oos = (int) refs.size ();
    assert (n_oos > 0 && (int) lengths.size () == n_oos);
    assert (filler_size >= 0 && filler_size % INT_ALIGNMENT == 0);
    const int n_var = n_oos + (filler_size > 0 ? 1 : 0);
    const int vot_bytes = (n_var + 1) * VOT_ENTRY_SZ;
    const int total = HEAP_HDR_SIZE + vot_bytes + n_oos * OR_OOS_INLINE_SIZE + filler_size;

    int err = recdes_allocate_data_area (&rec_out, total + MVCC_HEADER_SPARE);
    if (err != NO_ERROR)
      {
	return err;
      }
    rec_out.type = REC_HOME;
    rec_out.length = total;
    std::memset (rec_out.data, 0, total);
    OR_PUT_INT (rec_out.data + OR_REP_OFFSET,
		(OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_4BYTE);

    char *vot = rec_out.data + HEAP_HDR_SIZE;
    for (int i = 0; i < n_oos; i++)
      {
	OR_PUT_INT (vot + i * VOT_ENTRY_SZ, OR_SET_VAR_OOS (vot_bytes + i * OR_OOS_INLINE_SIZE));
      }
    if (filler_size > 0)
      {
	OR_PUT_INT (vot + n_oos * VOT_ENTRY_SZ, vot_bytes + n_oos * OR_OOS_INLINE_SIZE);
      }
    OR_PUT_INT (vot + n_var * VOT_ENTRY_SZ,
		OR_SET_VAR_LAST_ELEMENT (vot_bytes + n_oos * OR_OOS_INLINE_SIZE + filler_size));

    char *stub = vot + vot_bytes;
    for (int i = 0; i < n_oos; i++, stub += OR_OOS_INLINE_SIZE)
      {
	OR_PUT_OID (stub, &refs[i].head_oid);
	INT64 len = lengths[i];
	OR_PUT_BIGINT (stub + OR_OID_SIZE, &len);
	INT64 packed = oos_pack_identity_stamp (refs[i].identity_stamp);
	OR_PUT_BIGINT (stub + OR_OID_SIZE + OR_BIGINT_SIZE, &packed);
      }
    return NO_ERROR;
  }

  /* A heap record with no OOS at all: the post-image of an UPDATE that drops every OOS-backed attribute. */
  int
  build_plain_record (RECDES &rec_out, int data_size = 16)
  {
    const int total = HEAP_HDR_SIZE + data_size;
    int err = recdes_allocate_data_area (&rec_out, total + MVCC_HEADER_SPARE);
    if (err != NO_ERROR)
      {
	return err;
      }
    rec_out.type = REC_HOME;
    rec_out.length = total;
    std::memset (rec_out.data, 0, total);
    OR_PUT_INT (rec_out.data + OR_REP_OFFSET, OR_OFFSET_SIZE_4BYTE);
    return NO_ERROR;
  }

  LOG_LSA
  stale_stamp (const LOG_LSA &issued)
  {
    return LOG_LSA (issued.pageid + 1, (std::int16_t) issued.offset);
  }

  std::string
  page_filling_payload ()
  {
    return test_oos_utils::make_repeated_pattern_string (bridge_oos_get_max_chunk_size_within_page () - 50);
  }

  /* Byte a page reused as file-table metadata inside the OOS file is filled with. */
  constexpr char METADATA_SENTINEL = '\x5A';

  int
  init_as_metadata_page (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
  {
    (void) args;
    pgbuf_set_page_ptype (thread_p, page, PAGE_FTAB);
    std::memset (page, METADATA_SENTINEL, DB_PAGESIZE);
    pgbuf_log_new_page (thread_p, page, DB_PAGESIZE, PAGE_FTAB);
    return NO_ERROR;
  }
} // namespace

/* A heap of its own, with its own OOS file, per test. The class OID is borrowed from a real system class
 * because xheap_create reads the class record for the TDE algorithm; the scancache is pre-shaped so the DML
 * paths never resolve class information through that borrowed OID (which maps to db_user's real heap). */
class OosEagerDiagnostics : public ::testing::Test
{
  protected:
    HFID hfid;
    VFID oos_vfid;
    OID class_oid;
    HEAP_SCANCACHE scan_cache;
    /* Every test here reads the error log, so notifications are admitted for the whole fixture. */
    test_oos_error_log::notification_log_scope admit_notifications;

    void SetUp () override
    {
      HFID_SET_NULL (&hfid);
      VFID_SET_NULL (&oos_vfid);
      OID_SET_NULL (&class_oid);
      ASSERT_EQ (xlocator_find_class_oid (thread_p, "db_user", &class_oid, NULL_LOCK), LC_CLASSNAME_EXIST);
      ASSERT_EQ (xheap_create (thread_p, &hfid, &class_oid, false), NO_ERROR);
      ASSERT_TRUE (heap_oos_find_vfid (thread_p, &hfid, &oos_vfid, true));
      ASSERT_FALSE (VFID_ISNULL (&oos_vfid));
      commit ();

      heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, &hfid);
      COPY_OID (&scan_cache.node.class_oid, &class_oid);
      scan_cache.file_type = FILE_HEAP;
      /* The class is an ordinary MVCC-enabled one; SA_MODE is what makes every operation non-MVCC and so
       * routes it to the eager cleanup, not this flag. */
      scan_cache.mvcc_disabled_class = false;
      scan_cache.page_latch = PGBUF_LATCH_WRITE;
      scan_cache.cache_last_fix_page = false;

      /* Every test reads the error log, so prove once that it is readable: a helper that cannot find the
       * file returns "nothing was logged", which would make an absence assertion pass for free. */
      ASSERT_GE (test_oos_error_log::error_log_size (), 0) << "the fixture requires a readable server error log";

      heap_oos_test_reset_skipped_cleanup_diagnostics ();
      er_clear ();
    }

    void TearDown () override
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      if (!HFID_IS_NULL (&hfid))
	{
	  (void) xheap_destroy (thread_p, &hfid, &class_oid);
	  (void) xtran_server_commit (thread_p, false);
	}
    }

    void commit ()
    {
      ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
    }

    /* Inserts payload as one OOS value chain and returns the reference the owning record would store. */
    void insert_chain (const std::string &payload, oos_chain_ref &ref_out)
    {
      RECDES rec{};
      ASSERT_EQ (test_oos_utils::from_string_into_recdes (payload, rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer (&rec, recdes_free_data_area);
      ref_out.head_oid = OID_INITIALIZER;
      ref_out.identity_stamp = NULL_LSA;
      ASSERT_EQ (oos_insert (thread_p, oos_vfid, oos_buffer (rec.data, (std::size_t) rec.length), ref_out.head_oid,
			     &ref_out.identity_stamp), NO_ERROR);
    }

    /* Inserts a heap row whose OOS-backed attributes carry the given stubs; commits. */
    void insert_row_with_stubs (const std::vector<oos_chain_ref> &stub_refs, const std::vector<INT64> &lengths,
				OID &row_oid_out)
    {
      RECDES rec{};
      ASSERT_EQ (build_record_with_stubs (stub_refs, lengths, rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer (&rec, recdes_free_data_area);
      HEAP_OPERATION_CONTEXT ctx;
      heap_create_insert_context (&ctx, &hfid, &class_oid, &rec, &scan_cache);
      ASSERT_EQ (heap_insert_logical (thread_p, &ctx, NULL), NO_ERROR);
      row_oid_out = ctx.res_oid;
      commit ();
    }

    /* Inserts a heap row whose only OOS-backed attribute carries the given stub; commits. */
    void insert_row (const oos_chain_ref &stub_ref, INT64 stub_length, OID &row_oid_out, int filler_size = 0)
    {
      RECDES rec{};
      ASSERT_EQ (build_record_with_stubs ({stub_ref}, {stub_length}, rec, filler_size), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer (&rec, recdes_free_data_area);
      HEAP_OPERATION_CONTEXT ctx;
      heap_create_insert_context (&ctx, &hfid, &class_oid, &rec, &scan_cache);
      ASSERT_EQ (heap_insert_logical (thread_p, &ctx, NULL), NO_ERROR);
      row_oid_out = ctx.res_oid;
      commit ();
    }

    /* Inserts an ordinary row with no OOS at all, of the given payload size; commits. */
    void insert_filler_row (int data_size, OID &row_oid_out)
    {
      RECDES rec{};
      ASSERT_EQ (build_plain_record (rec, data_size), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer (&rec, recdes_free_data_area);
      HEAP_OPERATION_CONTEXT ctx;
      heap_create_insert_context (&ctx, &hfid, &class_oid, &rec, &scan_cache);
      ASSERT_EQ (heap_insert_logical (thread_p, &ctx, NULL), NO_ERROR);
      row_oid_out = ctx.res_oid;
      commit ();
    }

    /* Free space left on the heap page that holds row_oid, or -1. */
    int heap_page_free_space (const OID &row_oid)
    {
      VPID vpid = { row_oid.pageid, row_oid.volid };
      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  /* A probe's own failure must not be left on the stack for the next assertion to read as a stray
	   * error from the code under test. */
	  er_clear ();
	  return -1;
	}
      const int free_space = spage_get_free_space (thread_p, page_ptr);
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return free_space;
    }

    /* Slotted-page record type of the row's home slot, or -1 when the slot is gone. */
    int home_record_type (const OID &row_oid)
    {
      VPID vpid = { row_oid.pageid, row_oid.volid };
      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  er_clear ();
	  return -1;
	}
      RECDES rec = RECDES_INITIALIZER;
      const SCAN_CODE code = spage_get_record (thread_p, page_ptr, row_oid.slotid, &rec, PEEK);
      const int type = (code == S_SUCCESS) ? (int) rec.type : -1;
      pgbuf_unfix_and_init (thread_p, page_ptr);
      er_clear ();
      return type;
    }

    /* Grows the row until the heap moves it off its home page, leaving a REC_RELOCATION home slot and a
     * REC_NEWHOME forward record that still carries stub_ref. That forward record is what the relocation
     * call sites of the eager cleanup receive. */
    void relocate_row (OID &row_oid, const oos_chain_ref &stub_ref, INT64 stub_length, int filler_size)
    {
      /* Fill the home page so the grown record cannot stay on it. */
      const int free_space = heap_page_free_space (row_oid);
      ASSERT_GT (free_space, filler_size + 512);
      OID filler_oid = OID_INITIALIZER;
      insert_filler_row (free_space - 256, filler_oid);
      ASSERT_EQ (filler_oid.pageid, row_oid.pageid) << "the scenario requires the filler on the row's home page";
      ASSERT_LT (heap_page_free_space (row_oid), filler_size);

      RECDES grown{};
      ASSERT_EQ (build_record_with_stubs ({stub_ref}, {stub_length}, grown, filler_size), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer (&grown, recdes_free_data_area);
      ASSERT_EQ (update_row (row_oid, grown), NO_ERROR);
      commit ();
      ASSERT_EQ (home_record_type (row_oid), (int) REC_RELOCATION) << "the scenario requires a relocated row";
    }

    int delete_row (OID &row_oid)
    {
      HEAP_OPERATION_CONTEXT ctx;
      heap_create_delete_context (&ctx, &hfid, &row_oid, &class_oid, &scan_cache);
      return heap_delete_logical (thread_p, &ctx);
    }

    int update_row (OID &row_oid, RECDES &new_rec)
    {
      HEAP_OPERATION_CONTEXT ctx;
      heap_create_update_context (&ctx, &hfid, &row_oid, &class_oid, &new_rec, &scan_cache, UPDATE_INPLACE_NONE);
      return heap_update_logical (thread_p, &ctx);
    }

    /* True iff the row's heap slot still holds a record. The slot is probed directly rather than through
     * heap_does_exist, because the class OID is borrowed from db_user and resolving class information
     * through it would return db_user's real heap, not this test heap. */
    bool row_exists (const OID &row_oid)
    {
      VPID vpid = { row_oid.pageid, row_oid.volid };
      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  er_clear ();
	  return false;
	}
      RECDES rec = RECDES_INITIALIZER;
      const bool exists = (spage_get_record (thread_p, page_ptr, row_oid.slotid, &rec, PEEK) == S_SUCCESS);
      pgbuf_unfix_and_init (thread_p, page_ptr);
      er_clear ();
      return exists;
    }

    bool chunk_present (const OID &oid)
    {
      LOG_LSA stamp = NULL_LSA;
      const bool present = (oos_get_identity_stamp (thread_p, oid, &stamp) == NO_ERROR);
      er_clear ();
      return present;
    }

    /* Reads the chain through ref into a buffer of the given length. */
    int read_chain (const oos_chain_ref &ref, std::size_t length, std::string &out)
    {
      out.assign (length, '?');
      return oos_read (thread_p, ref, oos_buffer (out.data (), out.size ()));
    }

    bool page_is_deallocated (const VPID &vpid)
    {
      VPID probe = vpid;
      PAGE_PTR page_ptr = NULL;
      if (pgbuf_fix_if_not_deallocated (thread_p, &probe, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr)
	  != NO_ERROR)
	{
	  /* The probe itself failed; say so rather than reporting "still allocated". */
	  ADD_FAILURE () << "pgbuf_fix_if_not_deallocated failed on " << probe.volid << "|" << probe.pageid;
	  er_clear ();
	  return false;
	}
      if (page_ptr != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	  return false;
	}
      return true;
    }

    /* Reclaims the chain ref names and its page, as vacuum's fast path would have done before a retry. The
     * chain must have had its page to itself. */
    void reclaim_chain_and_its_page (const oos_chain_ref &ref)
    {
      const VPID page = { ref.head_oid.pageid, ref.head_oid.volid };
      std::vector<VPID> emptied;
      ASSERT_EQ (oos_delete (thread_p, oos_vfid, ref, &emptied), NO_ERROR);
      ASSERT_EQ (emptied.size (), 1U) << "the scenario requires the chain to have had its page to itself";
      ASSERT_TRUE (VPID_EQ (&emptied[0], &page));
      commit ();
      ASSERT_EQ (oos_reclaim_empty_pages (thread_p, oos_vfid, emptied), NO_ERROR);
      ASSERT_TRUE (page_is_deallocated (page));
    }

    /* Reallocates the reclaimed page inside the same OOS file as sentinel-filled file-table metadata. */
    void reuse_reclaimed_page_as_metadata (const VPID &expected)
    {
      VPID vpid = VPID_INITIALIZER;
      PAGE_PTR page_ptr = NULL;
      log_sysop_start (thread_p);
      int err = file_alloc (thread_p, &oos_vfid, init_as_metadata_page, NULL, &vpid, &page_ptr);
      if (err != NO_ERROR || page_ptr == NULL)
	{
	  log_sysop_abort (thread_p);
	  FAIL () << "file_alloc failed: " << err;
	}
      pgbuf_unfix_and_init (thread_p, page_ptr);
      log_sysop_commit (thread_p);
      ASSERT_TRUE (VPID_EQ (&vpid, &expected)) << "the scenario requires the reclaimed page to be reused, got "
	  << vpid.volid << "|" << vpid.pageid;
    }

    void release_metadata_page (const VPID &vpid)
    {
      VPID page = vpid;
      log_sysop_start (thread_p);
      int err = file_dealloc (thread_p, &oos_vfid, &page, FILE_OOS);
      if (err != NO_ERROR)
	{
	  log_sysop_abort (thread_p);
	  FAIL () << "file_dealloc failed: " << err;
	}
      log_sysop_commit (thread_p);
      ASSERT_TRUE (page_is_deallocated (page));
    }

    bool metadata_page_is_intact (const VPID &vpid)
    {
      VPID probe = vpid;
      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &probe, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  er_clear ();
	  return false;
	}
      bool intact = pgbuf_get_page_ptype (thread_p, page_ptr) == PAGE_FTAB;
      for (int i = 0; intact && i < DB_PAGESIZE; i++)
	{
	  intact = page_ptr[i] == METADATA_SENTINEL;
	}
      pgbuf_unfix_and_init (thread_p, page_ptr);
      return intact;
    }

    /* The OID of the chunk that follows the head of a multi-chunk chain. The page is released before any
     * assertion, so a failing scenario check cannot leave it fixed for the rest of the binary. */
    void continuation_oid_of (const OID &head_oid, OID &next_out)
    {
      OID_SET_NULL (&next_out);
      VPID vpid = { head_oid.pageid, head_oid.volid };
      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      ASSERT_NE (page_ptr, nullptr);
      RECDES rec = RECDES_INITIALIZER;
      const SCAN_CODE code = spage_get_record (thread_p, page_ptr, head_oid.slotid, &rec, PEEK);
      OOS_RECORD_HEADER header;
      const bool header_read = (code == S_SUCCESS && rec.length >= OOS_RECORD_HEADER_SIZE);
      if (header_read)
	{
	  std::memcpy (&header, rec.data, OOS_RECORD_HEADER_SIZE);
	}
      pgbuf_unfix_and_init (thread_p, page_ptr);
      ASSERT_TRUE (header_read) << "the head chunk must be readable, spage code " << (int) code;
      next_out = header.next_chunk_oid;
      ASSERT_FALSE (OID_ISNULL (&next_out)) << "the scenario requires a multi-chunk chain";
    }
};

// ===========================================================================
// A stale reference: the DML completes, the skip is diagnosed, the occupant survives
// ===========================================================================

TEST_F (OosEagerDiagnostics, DeleteCompletesAndDiagnosesAStampMismatch)
{
  /* The row's stub names a slot whose occupant carries another stamp, as a dead row's reference does once
   * the slot has been handed to a live row. */
  const std::string occupant_payload = "live occupant of the referenced slot";
  oos_chain_ref occupant;
  insert_chain (occupant_payload, occupant);
  oos_chain_ref stale = { occupant.head_oid, stale_stamp (occupant.identity_stamp) };
  OID row = OID_INITIALIZER;
  insert_row (stale, (INT64) occupant_payload.size () + 1, row);

  const long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();

  ASSERT_EQ (delete_row (row), NO_ERROR) << "a skipped cleanup must not fail the DELETE";
  EXPECT_EQ (er_errid (), NO_ERROR) << "successful DML leaves no stray error";
  EXPECT_FALSE (row_exists (row)) << "the DELETE must have completed";

  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1) << "one notification per operation";
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_STAMP_MISMATCH);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_count (), 1) << "the record referenced one chain";
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED))
      << "the skipped cleanup must be visible in the server error log";
  EXPECT_TRUE (error_log_contains_since (log_offset, "delete home"))
      << "the diagnostic must name the call site the record went through";

  EXPECT_TRUE (chunk_present (occupant.head_oid)) << "the current occupant must survive the stale reference";
  std::string out;
  ASSERT_EQ (read_chain (occupant, occupant_payload.size () + 1, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), occupant_payload.c_str ());
  commit ();
}

TEST_F (OosEagerDiagnostics, UpdateCompletesAndDiagnosesAStampMismatch)
{
  const std::string occupant_payload = "occupant kept through the update";
  oos_chain_ref occupant;
  insert_chain (occupant_payload, occupant);
  oos_chain_ref stale = { occupant.head_oid, stale_stamp (occupant.identity_stamp) };
  OID row = OID_INITIALIZER;
  insert_row (stale, (INT64) occupant_payload.size () + 1, row);

  /* The post-image references a fresh chain of its own. */
  const std::string fresh_payload = "the new image's own chain";
  oos_chain_ref fresh;
  insert_chain (fresh_payload, fresh);
  RECDES new_rec{};
  ASSERT_EQ (build_record_with_stubs ({fresh}, { (INT64) fresh_payload.size () + 1}, new_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer (&new_rec, recdes_free_data_area);

  const long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();

  ASSERT_EQ (update_row (row, new_rec), NO_ERROR) << "a skipped cleanup must not fail the UPDATE";
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_TRUE (row_exists (row));
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_STAMP_MISMATCH);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_count (), 1);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  EXPECT_TRUE (error_log_contains_since (log_offset, "update home"))
      << "the diagnostic must name the call site the record went through";

  std::string out;
  ASSERT_EQ (read_chain (occupant, occupant_payload.size () + 1, out), NO_ERROR)
      << "the current occupant must survive the stale reference";
  EXPECT_STREQ (out.c_str (), occupant_payload.c_str ());
  ASSERT_EQ (read_chain (fresh, fresh_payload.size () + 1, out), NO_ERROR)
      << "the new image's chain must survive its own UPDATE";
  EXPECT_STREQ (out.c_str (), fresh_payload.c_str ());
  commit ();
}

TEST_F (OosEagerDiagnostics, DeleteDiagnosesAHeadSlotAlreadyEmpty)
{
  /* The chain was reclaimed before the row's own DELETE reaches it: the head slot is empty. */
  const std::string payload = "reclaimed ahead of the row";
  oos_chain_ref ref;
  insert_chain (payload, ref);
  OID row = OID_INITIALIZER;
  insert_row (ref, (INT64) payload.size () + 1, row);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, ref), NO_ERROR);
  commit ();

  const long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (delete_row (row), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_FALSE (row_exists (row));
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_SLOT_EMPTY);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  commit ();
}

TEST_F (OosEagerDiagnostics, DeleteDiagnosesADeallocatedAndAReusedHeadPage)
{
  /* Deallocated head page: the chain had its page to itself and empty-page reclaim returned the page. */
  const std::string payload = page_filling_payload ();
  oos_chain_ref gone;
  insert_chain (payload, gone);
  OID row = OID_INITIALIZER;
  insert_row (gone, (INT64) payload.size () + 1, row);
  reclaim_chain_and_its_page (gone);

  long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (delete_row (row), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_FALSE (row_exists (row));
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_PAGE_GONE);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  commit ();

  /* Reused head page: the same file took the reclaimed page back as metadata that is not a slotted page.
   * The OOS code sees the same thing in both halves, a head page that is no longer an OOS page, which is
   * why one outcome covers them; only the debug-level OOS log distinguishes them. */
  oos_chain_ref retyped;
  insert_chain (payload, retyped);
  OID row2 = OID_INITIALIZER;
  insert_row (retyped, (INT64) payload.size () + 1, row2);
  reclaim_chain_and_its_page (retyped);
  const VPID page = { retyped.head_oid.pageid, retyped.head_oid.volid };
  reuse_reclaimed_page_as_metadata (page);
  ASSERT_TRUE (metadata_page_is_intact (page));

  log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (delete_row (row2), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_FALSE (row_exists (row2));
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_PAGE_GONE);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  EXPECT_TRUE (metadata_page_is_intact (page)) << "the skipped cleanup modified the metadata page";
  commit ();
  release_metadata_page (page);
}

// ===========================================================================
// A live reference is reclaimed quietly; a chain the new image still references is kept quietly
// ===========================================================================

TEST_F (OosEagerDiagnostics, LiveReferenceIsReclaimedWithoutADiagnostic)
{
  const std::string payload = "the row's own chain";
  oos_chain_ref ref;
  insert_chain (payload, ref);
  OID row = OID_INITIALIZER;
  insert_row (ref, (INT64) payload.size () + 1, row);

  const long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (delete_row (row), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_FALSE (row_exists (row));
  EXPECT_FALSE (chunk_present (ref.head_oid)) << "a live reference must reclaim its chain";
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 0);
  EXPECT_FALSE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  commit ();
}

TEST_F (OosEagerDiagnostics, UpdateKeepsAChainTheNewImageStillReferencesAndDiagnosesOnlyTheDropped)
{
  /* The record holds two OOS-backed attributes: one the new image still references, one it drops and
   * whose target has since been reused. Mixing them in a single UPDATE proves the cleanup loop really
   * ran and made a per-chain decision, which a kept-chain-only test cannot distinguish from a cleanup
   * that never happened. */
  const std::string kept_payload = "referenced before and after";
  const std::string dropped_payload = "referenced only before";
  oos_chain_ref kept;
  oos_chain_ref dropped_occupant;
  insert_chain (kept_payload, kept);
  insert_chain (dropped_payload, dropped_occupant);
  const oos_chain_ref dropped_stale = { dropped_occupant.head_oid, stale_stamp (dropped_occupant.identity_stamp) };

  OID row = OID_INITIALIZER;
  insert_row_with_stubs ({ kept, dropped_stale },
  { (INT64) kept_payload.size () + 1, (INT64) dropped_payload.size () + 1 }, row);

  /* The post-image keeps the first chain and names no second one. */
  RECDES new_rec{};
  ASSERT_EQ (build_record_with_stubs ({kept}, { (INT64) kept_payload.size () + 1}, new_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer (&new_rec, recdes_free_data_area);

  const long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (update_row (row, new_rec), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);

  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1) << "the dropped chain's skip is reported";
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_count (), 1) << "the kept chain is not counted as skipped";
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_STAMP_MISMATCH);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));

  std::string out;
  ASSERT_EQ (read_chain (kept, kept_payload.size () + 1, out), NO_ERROR)
      << "a chain the post-image references is kept";
  EXPECT_STREQ (out.c_str (), kept_payload.c_str ());
  ASSERT_EQ (read_chain (dropped_occupant, dropped_payload.size () + 1, out), NO_ERROR)
      << "the occupant of the dropped reference's slot survives the skip";
  EXPECT_STREQ (out.c_str (), dropped_payload.c_str ());
  commit ();
}

// ===========================================================================
// The relocation call sites see the forward record, and diagnose it the same way
// ===========================================================================

TEST_F (OosEagerDiagnostics, RelocatedUpdateAndDeleteDiagnoseTheForwardRecordsStaleStub)
{
  const std::string first_payload = "occupant named by the relocated row";
  oos_chain_ref first;
  insert_chain (first_payload, first);
  const oos_chain_ref first_stale = { first.head_oid, stale_stamp (first.identity_stamp) };
  OID row = OID_INITIALIZER;
  insert_row (first_stale, (INT64) first_payload.size () + 1, row);

  /* Growing the row moves it off its home page; the forward record keeps the same stub, so the growth
   * itself reclaims nothing and diagnoses nothing. */
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  relocate_row (row, first_stale, (INT64) first_payload.size () + 1, 2048);
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 0) << "a kept chain is not a skipped cleanup";
  EXPECT_TRUE (chunk_present (first.head_oid));

  /* UPDATE of the relocated row: the old forward record's stale stub is unreferenced by the new image. */
  const std::string second_payload = "occupant named by the next image";
  oos_chain_ref second;
  insert_chain (second_payload, second);
  const oos_chain_ref second_stale = { second.head_oid, stale_stamp (second.identity_stamp) };
  RECDES new_rec{};
  ASSERT_EQ (build_record_with_stubs ({second_stale}, { (INT64) second_payload.size () + 1}, new_rec, 2048),
	     NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer (&new_rec, recdes_free_data_area);

  long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (update_row (row, new_rec), NO_ERROR) << "a skipped cleanup must not fail the relocated UPDATE";
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_STAMP_MISMATCH);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  EXPECT_TRUE (error_log_contains_since (log_offset, "update relocation"))
      << "the diagnostic must name the relocation call site the forward record went through";
  std::string out;
  ASSERT_EQ (read_chain (first, first_payload.size () + 1, out), NO_ERROR)
      << "the first occupant must survive the relocated UPDATE";
  EXPECT_STREQ (out.c_str (), first_payload.c_str ());
  commit ();

  /* DELETE of the relocated row: the forward record's stale stub is the one the delete path sees. */
  log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (delete_row (row), NO_ERROR) << "a skipped cleanup must not fail the relocated DELETE";
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_FALSE (row_exists (row));
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1);
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_STAMP_MISMATCH);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  EXPECT_TRUE (error_log_contains_since (log_offset, "delete relocation"))
      << "the diagnostic must name the relocation call site the forward record went through";
  ASSERT_EQ (read_chain (second, second_payload.size () + 1, out), NO_ERROR)
      << "the second occupant must survive the relocated DELETE";
  EXPECT_STREQ (out.c_str (), second_payload.c_str ());
  commit ();
}

// ===========================================================================
// One notification per operation, however many of the record's chains were skipped
// ===========================================================================

TEST_F (OosEagerDiagnostics, ThreeStaleChainsInOneRecordAreOneNotification)
{
  /* The eager cleanup runs with a write latch held on the heap page and each notification is a flushed
   * write to the error log, so a record with several OOS-backed attributes must report once, not once
   * per chain. */
  const std::string payloads[] = { "first chain", "second chain", "third chain" };
  std::vector<oos_chain_ref> stale_refs;
  std::vector<oos_chain_ref> live_refs;
  std::vector<INT64> lengths;
  for (const std::string &payload : payloads)
    {
      oos_chain_ref live;
      insert_chain (payload, live);
      live_refs.push_back (live);
      stale_refs.push_back ({ live.head_oid, stale_stamp (live.identity_stamp) });
      lengths.push_back ((INT64) payload.size () + 1);
    }
  OID row = OID_INITIALIZER;
  insert_row_with_stubs (stale_refs, lengths, row);

  const long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  ASSERT_EQ (delete_row (row), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_FALSE (row_exists (row));

  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 1) << "three skipped chains, one notification";
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_count (), 3) << "the notification must carry the count";
  EXPECT_EQ (heap_oos_test_last_skipped_cleanup_outcome (), (int) OOS_DELETE_SKIPPED_STAMP_MISMATCH);
  EXPECT_TRUE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  EXPECT_TRUE (error_log_contains_since (log_offset, "delete home"));

  /* Every occupant is still readable through its own reference. */
  for (std::size_t i = 0; i < live_refs.size (); i++)
    {
      std::string out;
      ASSERT_EQ (read_chain (live_refs[i], payloads[i].size () + 1, out), NO_ERROR) << "chain " << i;
      EXPECT_STREQ (out.c_str (), payloads[i].c_str ());
    }
  commit ();
}

// ===========================================================================
// Malformed heads and operational failures stay errors
// ===========================================================================

TEST_F (OosEagerDiagnostics, MatchingNonHeadReferenceFailsTheDelete)
{
  /* The row's stub names a continuation chunk with the stamp that chunk actually carries: identity matches,
   * but the target is not a chain head. This is a malformed reference, not a stale one. */
  const std::string payload = test_oos_utils::make_repeated_pattern_string (bridge_oos_get_max_chunk_size_within_page ()
			      + 200);
  oos_chain_ref head;
  insert_chain (payload, head);
  oos_chain_ref non_head;
  continuation_oid_of (head.head_oid, non_head.head_oid);
  ASSERT_EQ (oos_get_identity_stamp (thread_p, non_head.head_oid, &non_head.identity_stamp), NO_ERROR);
  OID row = OID_INITIALIZER;
  insert_row (non_head, (INT64) payload.size () + 1, row);

  const long log_offset = error_log_size ();
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  er_clear ();
  EXPECT_EQ (delete_row (row), ER_HEAP_OOS_CORRUPTED_RECORD) << "a malformed reference is an error, not a skip";
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 0);
  EXPECT_FALSE (error_log_mentions_since (log_offset, ER_HEAP_OOS_EAGER_CLEANUP_SKIPPED));
  ASSERT_EQ (xtran_server_abort (thread_p), TRAN_UNACTIVE_ABORTED);
  er_clear ();

  /* Nothing of the live chain was touched. */
  EXPECT_TRUE (chunk_present (head.head_oid));
  EXPECT_TRUE (chunk_present (non_head.head_oid));
  std::string out;
  ASSERT_EQ (read_chain (head, payload.size () + 1, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());
}

TEST_F (OosEagerDiagnostics, InterruptedCleanupIsAnErrorNotASkip)
{
  const std::string payload = "operational failures stay errors";
  oos_chain_ref ref;
  insert_chain (payload, ref);
  RECDES rec{};
  ASSERT_EQ (build_record_with_stubs ({ref}, { (INT64) payload.size () + 1}, rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer (&rec, recdes_free_data_area);

  /* Through the eager entry point directly: an interrupted transaction fails its next page fix with
   * ER_INTERRUPTED, which the fix consumes; a DELETE would have spent it on the heap page instead. */
  HEAP_OPERATION_CONTEXT context;
  std::memset (&context, 0, sizeof (context));
  context.hfid = hfid;
  context.oid.volid = hfid.vfid.volid;
  context.oid.pageid = hfid.hpgid;
  context.oid.slotid = 1;

  const int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  heap_oos_test_reset_skipped_cleanup_diagnostics ();
  (void) logtb_set_tran_index_interrupt (thread_p, tran_index, true);
  er_clear ();
  EXPECT_EQ (heap_oos_delete_unreferenced (thread_p, &context, &rec, NULL, "unit test interrupt"), ER_INTERRUPTED);
  EXPECT_EQ (er_errid (), ER_INTERRUPTED) << "an operational failure must not be converted to a clean skip";
  (void) logtb_set_tran_index_interrupt (thread_p, tran_index, false);
  er_clear ();
  EXPECT_EQ (heap_oos_test_skipped_cleanup_notifications (), 0);

  EXPECT_TRUE (chunk_present (ref.head_oid));
  std::string out;
  ASSERT_EQ (read_chain (ref, payload.size () + 1, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());
  commit ();
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;

  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS ();
}
