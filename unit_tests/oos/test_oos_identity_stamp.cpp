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
 * test_oos_identity_stamp.cpp - the OOS value chain identity stamp (CBRD-26950)
 *
 * Every OOS value chain is created with an identity stamp: the page LSA observed under the write
 * latch immediately before the head chunk's header is built. The stamp travels in the chunk header
 * and in the owning heap record's OOS inline stub, and it is the target identity that oos_delete
 * verifies before reclaiming a chain and that oos_read verifies before returning bytes.
 *
 * These tests drive only the public OOS file API and observe return codes, the error stack, the
 * bytes read back, the emptied-page list and the stamp reported by oos_get_identity_stamp.
 */

#include "gtest/gtest.h"
#include <cstring>
#include <string>
#include <vector>

#include "error_manager.h"
#include "file_manager.h"
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
#include "test_oos_log.hpp"

using namespace test_oos_log;

/* bridge to a static function in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

namespace
{
  /* Inserts payload and returns both outputs of oos_insert. */
  int
  insert_with_stamp (const VFID &oos_vfid, const std::string &payload, OID &oid_out, LOG_LSA &stamp_out)
  {
    RECDES rec{};
    int err = test_oos_utils::from_string_into_recdes (payload, rec);
    if (err != NO_ERROR)
      {
	return err;
      }
    test_oos_utils::auto_freed_recdes_ptr defer_free (&rec, recdes_free_data_area);
    stamp_out = NULL_LSA;
    return oos_insert (thread_p, oos_vfid, oos_buffer (rec.data, static_cast<std::size_t> (rec.length)), oid_out,
		       &stamp_out);
  }

  std::string
  page_filling_payload ()
  {
    return test_oos_utils::make_repeated_pattern_string (bridge_oos_get_max_chunk_size_within_page () - 50);
  }

  /* One variable attribute of a hand-built heap record. */
  struct heap_var_field
  {
    std::string bytes;
    bool is_oos;
  };

  /* The OOS inline stub exactly as the heap writer stores it: [OID (8B) | full length (8B) | packed stamp (8B)]. */
  std::string
  make_inline_stub (const OID &head_oid, DB_BIGINT full_length, const LOG_LSA &identity_stamp)
  {
    alignas (MAX_ALIGNMENT) char stub[OR_OOS_INLINE_SIZE];
    OR_BUF buf;
    or_init (&buf, stub, OR_OOS_INLINE_SIZE);
    or_put_oid (&buf, &head_oid);
    or_put_bigint (&buf, full_length);
    or_put_bigint (&buf, oos_pack_identity_stamp (identity_stamp));
    return std::string (stub, OR_OOS_INLINE_SIZE);
  }

  /* A heap record without fixed attributes and with 4-byte offsets: [rep+flags | CHN | variable offset table |
   * fields]. As the heap writer lays it out, the table has one entry per field plus the terminator that carries
   * OR_VAR_BIT_LAST_ELEMENT, so every field's width is defined by the table and not by the record end. */
  std::vector<char>
  make_heap_record (const std::vector<heap_var_field> &fields)
  {
    const int header_size = OR_MVCC_REP_SIZE + OR_CHN_SIZE;
    const int n_var = (int) fields.size ();
    const int vot_bytes = (n_var + 1) * OR_INT_SIZE;
    std::vector<char> record (header_size + vot_bytes, 0);
    OR_PUT_INT (record.data () + OR_REP_OFFSET,
		(OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_4BYTE);
    int offset = vot_bytes;
    for (int i = 0; i < n_var; i++)
      {
	/* the two flag bits live in the low bits of each entry, so every field must start 4-byte aligned */
	EXPECT_EQ (offset % INT_ALIGNMENT, 0) << "field " << i << " must start 4-byte aligned";
	OR_PUT_INT (record.data () + header_size + i * OR_INT_SIZE, fields[i].is_oos ? OR_SET_VAR_OOS (offset) : offset);
	offset += (int) fields[i].bytes.size ();
      }
    OR_PUT_INT (record.data () + header_size + n_var * OR_INT_SIZE, OR_SET_VAR_LAST_ELEMENT (offset));
    for (const heap_var_field &field : fields)
      {
	record.insert (record.end (), field.bytes.begin (), field.bytes.end ());
      }
    return record;
  }

  RECDES
  recdes_over (std::vector<char> &record)
  {
    RECDES recdes = { (int) record.size (), (int) record.size (), REC_HOME, record.data () };
    return recdes;
  }

  /* Parses the OOS inline stub of variable attribute `location`. */
  int
  parse_inline_ref (RECDES &recdes, int location, oos_chain_ref &ref, DB_BIGINT &length)
  {
    return heap_oos_parse_inline_ref (&recdes, location, &ref, &length);
  }

  /* Byte a page reused as file-table metadata inside the OOS file is filled with. */
  constexpr char METADATA_SENTINEL = '\x5A';

  /* file_alloc page initializer: the page becomes PAGE_FTAB metadata holding only the sentinel. It stands in
   * for a freed page of the same file that file_perm_dealloc reuses for its partial-sector table: the real
   * table page is allocated as a table page and holds extensible-data headers, but what the OOS code sees
   * is the same, a PAGE_FTAB page whose bytes are not a slotted page and must never be read as one. */
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

/* One OOS file per test. TearDown removes it and commits, so a test that has to commit (empty-page
 * reclaim needs committed deletes) never leaves a committed orphan file behind: a later binary's
 * file-tracker dump would try to resolve the synthetic owner class OID against a non-heap page. */
class OosIdentityStampTest : public ::testing::Test
{
  protected:
    VFID oos_vfid;

    void SetUp () override
    {
      ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);
    }

    void TearDown () override
    {
      ASSERT_EQ (oos_remove_file (thread_p, oos_vfid), NO_ERROR);
      ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
    }

    /* Reads the whole chain through ref into a string; returns the oos_read result. */
    int read_through (const oos_chain_ref &ref, std::string &out)
    {
      int len = oos_get_length (thread_p, ref.head_oid);
      if (len < 0)
	{
	  return er_errid ();
	}
      out.assign ((std::size_t) len, '?');
      return oos_read (thread_p, ref, oos_buffer (out.data (), out.size ()));
    }

    /* Scalar read through a retained reference into a buffer of the retained length. Bytes the read did
     * not deliver keep the '?' fill, so a caller can tell that nothing of another occupant arrived. */
    int read_scalar (const oos_chain_ref &ref, std::size_t length, std::string &out)
    {
      out.assign (length, '?');
      return oos_read (thread_p, ref, oos_buffer (out.data (), out.size ()));
    }

    /* The same request through the grouped API. */
    int read_grouped (const oos_chain_ref &ref, std::size_t length, std::string &out)
    {
      out.assign (length, '?');
      oos_read_request request = { ref, oos_buffer (out.data (), out.size ()) };
      return oos_read_many (thread_p, cubbase::span<oos_read_request> (&request, 1));
    }

    bool page_is_deallocated (const VPID &vpid)
    {
      VPID probe = vpid;
      PAGE_PTR page_ptr = NULL;
      if (pgbuf_fix_if_not_deallocated (thread_p, &probe, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr)
	  != NO_ERROR)
	{
	  return false;
	}
      if (page_ptr != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, page_ptr);
	  return false;
	}
      return true;
    }

    /* Deletes the chain through ref, commits and reclaims its page. The chain must have had the page to itself. */
    void reclaim_own_page (const oos_chain_ref &ref)
    {
      const VPID page = { ref.head_oid.pageid, ref.head_oid.volid };
      std::vector<VPID> emptied;
      ASSERT_EQ (oos_delete (thread_p, oos_vfid, ref, &emptied), NO_ERROR);
      ASSERT_EQ (emptied.size (), 1U);
      ASSERT_TRUE (VPID_EQ (&emptied[0], &page));
      /* Reclaim requires committed deletes (the LSA gate defers a live deleter's pages). */
      ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
      ASSERT_EQ (oos_reclaim_empty_pages (thread_p, oos_vfid, emptied), NO_ERROR);
      ASSERT_TRUE (page_is_deallocated (page)) << "the fixture requires the page to be deallocated";
    }

    /* Reallocates the reclaimed page inside the same OOS file as file-table metadata filled with the sentinel.
     * The file manager hands out the first free page of the first partial sector, which is the page just
     * reclaimed, the same choice it makes when it needs a new partial-sector table page. */
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

    /* Gives the metadata page back to the file manager before TearDown destroys the file. file_destroy
     * deallocates real table pages through the file-table chain and skips PAGE_FTAB pages in its sector
     * walk, so a simulated metadata page that stays typed would collide with the next test's page
     * allocations. The postponed deallocation runs at the sysop commit, as in empty-page reclaim. */
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

    /* True iff the page is still PAGE_FTAB metadata and every byte still equals the sentinel. */
    bool metadata_page_is_intact (const VPID &vpid)
    {
      VPID probe = vpid;
      PAGE_PTR page_ptr = pgbuf_fix (thread_p, &probe, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
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

    /* Retires a small chain and lets a two-chunk chain follow it. Chains are written tail first, and the
     * tail is small enough for the page that just freed the small chain's slot, so the follower's
     * continuation chunk begins a new incarnation of that slot while the follower's head gets a page of
     * its own. stale_ref names the retired chain; live_ref and live_payload describe the follower. */
    void reuse_slot_with_continuation_chunk (oos_chain_ref &stale_ref, std::size_t &stale_length,
	oos_chain_ref &live_ref, std::string &live_payload)
    {
      /* Longer than the follower's tail chunk, so a stale read of the reused slot is decided by the head
       * checks and not by the caller-buffer bound. */
      const std::string retired_payload = test_oos_utils::make_repeated_pattern_string (400);
      OID retired_oid = OID_INITIALIZER;
      LOG_LSA retired_stamp = NULL_LSA;
      ASSERT_EQ (insert_with_stamp (oos_vfid, retired_payload, retired_oid, retired_stamp), NO_ERROR);
      stale_ref.head_oid = retired_oid;
      stale_ref.identity_stamp = retired_stamp;
      stale_length = retired_payload.size () + 1;
      ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref), NO_ERROR);

      live_payload = test_oos_utils::make_repeated_pattern_string (bridge_oos_get_max_chunk_size_within_page () + 200);
      OID live_oid = OID_INITIALIZER;
      LOG_LSA live_stamp = NULL_LSA;
      ASSERT_EQ (insert_with_stamp (oos_vfid, live_payload, live_oid, live_stamp), NO_ERROR);
      live_ref.head_oid = live_oid;
      live_ref.identity_stamp = live_stamp;
      ASSERT_FALSE (OID_EQ (&live_oid, &retired_oid)) << "the follower's head must live on its own page";

      LOG_LSA occupant_stamp = NULL_LSA;
      ASSERT_EQ (oos_get_identity_stamp (thread_p, retired_oid, &occupant_stamp), NO_ERROR)
	  << "the scenario requires the follower's continuation chunk to reuse the retired slot";
      ASSERT_FALSE (LSA_EQ (&occupant_stamp, &retired_stamp));
    }
};

// ===========================================================================
// Ticket 02: the stamp is issued at insert and readable through the accessor
// ===========================================================================

TEST_F (OosIdentityStampTest, InsertReportsTheStampTheHeadChunkCarries)
{
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "identity stamp round trip", oid, issued), NO_ERROR);

  LOG_LSA stored = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, oid, &stored), NO_ERROR);
  EXPECT_TRUE (LSA_EQ (&issued, &stored)) << "insert reported " << issued.pageid << "|" << issued.offset
					  << " but the head chunk carries " << stored.pageid << "|" << stored.offset;
}

TEST_F (OosIdentityStampTest, FreshPageIssuesNonNullStamp)
{

  /* A page-filling record needs a page of its own, so this lands on a freshly allocated page whose
   * logged initialization already advanced its page LSA. NULL is an ordinary stamp value and gets no
   * special handling; this only pins down that a normal insert never observes it. */
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, page_filling_payload (), oid, issued), NO_ERROR);
  EXPECT_FALSE (LSA_ISNULL (&issued));
}

TEST_F (OosIdentityStampTest, SuccessiveOccupantsOfOneSlotCarryDifferentStamps)
{

  OID first_oid = OID_INITIALIZER;
  LOG_LSA first_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "first slot incarnation", first_oid, first_stamp), NO_ERROR);

  /* Reclaim the chunk; the slot is free again. */
  ASSERT_EQ (test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, first_oid), NO_ERROR);

  /* A same-size insert reuses the freed slot (ANCHORED slotted page + bestspace), beginning a new
   * slot incarnation under the same OOS OID. */
  OID second_oid = OID_INITIALIZER;
  LOG_LSA second_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "later slot incarnation", second_oid, second_stamp), NO_ERROR);
  ASSERT_TRUE (OID_EQ (&first_oid, &second_oid)) << "the scenario requires physical slot reuse";

  /* The first occupant's own logged insert and delete advanced the page LSA past its stamp, so the
   * second occupant cannot share it. */
  EXPECT_FALSE (LSA_EQ (&first_stamp, &second_stamp));

  LOG_LSA stored = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, second_oid, &stored), NO_ERROR);
  EXPECT_TRUE (LSA_EQ (&second_stamp, &stored));
}

TEST_F (OosIdentityStampTest, MultiPageInsertReportsTheHeadChunkStamp)
{

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const std::string payload = test_oos_utils::make_repeated_pattern_string (2 * max_chunk_size + 100);

  OID head_oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, head_oid, issued), NO_ERROR);

  /* Chains are written tail first, so the head chunk is the last one written and its value is the
   * one the stub must carry. */
  LOG_LSA stored = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, head_oid, &stored), NO_ERROR);
  EXPECT_TRUE (LSA_EQ (&issued, &stored));
}

TEST_F (OosIdentityStampTest, BatchInsertReportsOneStampPerRequest)
{

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  std::vector<std::string> payloads =
  {
    std::string (1000, 'a'),
    std::string (static_cast<std::size_t> (max_chunk_size) + 123, 'b'),	/* multi-page */
    std::string (1200, 'c')
  };
  std::vector<OID> oids (payloads.size (), OID_INITIALIZER);
  std::vector<LOG_LSA> stamps (payloads.size (), NULL_LSA);
  std::vector<oos_insert_request> requests;
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      oos_insert_request request = { oos_buffer (payloads[i].data (), payloads[i].size ()), &oids[i], &stamps[i] };
      requests.push_back (request);
    }
  /* The stamp output is optional: a request without one is still served. */
  std::string extra (800, 'd');
  OID extra_oid = OID_INITIALIZER;
  oos_insert_request extra_request = { oos_buffer (extra.data (), extra.size ()), &extra_oid, NULL };
  requests.push_back (extra_request);

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid, cubbase::span<oos_insert_request> (requests.data (),
			      requests.size ())), NO_ERROR);

  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      LOG_LSA stored = NULL_LSA;
      ASSERT_EQ (oos_get_identity_stamp (thread_p, oids[i], &stored), NO_ERROR) << "request " << i;
      EXPECT_TRUE (LSA_EQ (&stamps[i], &stored)) << "request " << i;
    }
  LOG_LSA extra_stored = NULL_LSA;
  EXPECT_EQ (oos_get_identity_stamp (thread_p, extra_oid, &extra_stored), NO_ERROR);
}

TEST_F (OosIdentityStampTest, AccessorFailsForAbsentChunk)
{

  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "soon to be gone", oid, issued), NO_ERROR);
  ASSERT_EQ (test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, oid), NO_ERROR);

  LOG_LSA stored = NULL_LSA;
  EXPECT_NE (oos_get_identity_stamp (thread_p, oid, &stored), NO_ERROR);
  EXPECT_NE (er_errid (), NO_ERROR);
  er_clear ();

  /* A slot that never existed on a live page is absent too. */
  OID never_used = oid;
  never_used.slotid = 4000;
  EXPECT_NE (oos_get_identity_stamp (thread_p, never_used, &stored), NO_ERROR);
  EXPECT_NE (er_errid (), NO_ERROR);
  er_clear ();
}

// ===========================================================================
// Ticket 03: the stub packs the stamp into one bigint, and reads verify it
// ===========================================================================

TEST (OosIdentityStampPureTest, PackedStubStampRoundTripsEveryValue)
{
  const LOG_LSA samples[] =
  {
    NULL_LSA,
    LOG_LSA (0, 0),
    LOG_LSA (1, 1),
    LOG_LSA (0xC0FFEE, 42),
    LOG_LSA (123456789012345LL, 16383),
    LOG_LSA (MAX_LOG_LSA_PAGEID, MAX_LOG_LSA_OFFSET),
    LOG_LSA (7, -1),
  };
  for (const LOG_LSA &sample : samples)
    {
      const DB_BIGINT packed = oos_pack_identity_stamp (sample);
      const LOG_LSA unpacked = oos_unpack_identity_stamp (packed);
      EXPECT_TRUE (LSA_EQ (&unpacked, &sample)) << (long long) sample.pageid << "|" << (int) sample.offset
	  << " packed to " << (long long) packed;
    }
  /* NULL_LSA (-1, -1) is all ones, so a zero-filled stub never decodes as NULL by accident. */
  EXPECT_EQ (oos_pack_identity_stamp (NULL_LSA), (DB_BIGINT) -1);
  const LOG_LSA zero = oos_unpack_identity_stamp (0);
  EXPECT_FALSE (LSA_ISNULL (&zero));
}

TEST (OosIdentityStampPureTest, ChunkHeaderAndStubAreEachTwentyFourBytes)
{
  /* The chunk header grew by the raw LOG_LSA; every per-page capacity computation follows this constant. */
  EXPECT_EQ (OOS_RECORD_HEADER_SIZE, 24);
  EXPECT_EQ (OOS_RECORD_HEADER_SIZE, (int) (2 * sizeof (int) + sizeof (OID) + sizeof (LOG_LSA)));
  /* The stub packs the stamp into one bigint and stays 8-byte aligned. */
  EXPECT_EQ (OR_OOS_INLINE_SIZE, 24);
  EXPECT_EQ (OR_OOS_INLINE_SIZE, OR_OID_SIZE + OR_BIGINT_SIZE + OR_OOS_IDENTITY_STAMP_SIZE);
}

TEST (OosIdentityStampPureTest, StubWriteThenParseRoundTripsAtTwentyFourBytes)
{
  /* The OOS inline stub as the heap writer stores it: [OID (8B) | full length (8B) | packed stamp (8B)]. */
  ASSERT_EQ (OR_OOS_INLINE_SIZE, 24);

  OID head_oid;
  head_oid.volid = 3;
  head_oid.pageid = 4242;
  head_oid.slotid = 7;
  const DB_BIGINT full_length = 160 * 1024;
  const LOG_LSA stamps[] = { LOG_LSA (0xC0FFEE, 42), NULL_LSA };

  for (const LOG_LSA &identity_stamp : stamps)
    {
      std::vector<char> record = make_heap_record ({ { make_inline_stub (head_oid, full_length, identity_stamp), true } });
      RECDES recdes = recdes_over (record);
      oos_chain_ref ref;
      DB_BIGINT parsed_length = 0;
      ASSERT_EQ (parse_inline_ref (recdes, 0, ref, parsed_length), NO_ERROR);
      EXPECT_TRUE (OID_EQ (&ref.head_oid, &head_oid));
      EXPECT_EQ (parsed_length, full_length);
      /* A NULL stamp parses like any other value (invariant 3). */
      EXPECT_TRUE (LSA_EQ (&ref.identity_stamp, &identity_stamp));
    }
}

TEST (OosIdentityStampPureTest, ParseReadsEachStubFieldOfARecordWithinItsOwnBounds)
{
  /* Two OOS attributes around an ordinary one: each stub parses from its own field. The ordinary value is
   * 12 bytes, so the second stub sits at a 4-byte-aligned but not 8-byte-aligned offset, where the
   * parser must still read it. */
  OID first_oid;
  first_oid.volid = 1;
  first_oid.pageid = 100;
  first_oid.slotid = 1;
  OID second_oid;
  second_oid.volid = 2;
  second_oid.pageid = 200;
  second_oid.slotid = 2;
  const LOG_LSA first_stamp (11, 1);
  const LOG_LSA second_stamp (22, 2);
  std::vector<char> record = make_heap_record ({ { make_inline_stub (first_oid, 1000, first_stamp), true },
    { std::string ("plain value!"), false },
    { make_inline_stub (second_oid, 2000, second_stamp), true } });
  RECDES recdes = recdes_over (record);

  oos_chain_ref ref;
  DB_BIGINT parsed_length = 0;
  ASSERT_EQ (parse_inline_ref (recdes, 0, ref, parsed_length), NO_ERROR);
  EXPECT_TRUE (OID_EQ (&ref.head_oid, &first_oid));
  EXPECT_EQ (parsed_length, 1000);
  EXPECT_TRUE (LSA_EQ (&ref.identity_stamp, &first_stamp));

  ASSERT_EQ (parse_inline_ref (recdes, 2, ref, parsed_length), NO_ERROR);
  EXPECT_TRUE (OID_EQ (&ref.head_oid, &second_oid));
  EXPECT_EQ (parsed_length, 2000);
  EXPECT_TRUE (LSA_EQ (&ref.identity_stamp, &second_stamp));
}

TEST (OosIdentityStampPureTest, ParseRejectsShortStubFieldBeforeAnotherAttribute)
{
  /* The first field holds only the former 16-byte stub (OID and full length): the identity stamp is missing and
   * the next attribute, an ordinary value, begins where the stamp would be. The record is long enough for a
   * 24-byte read, so only the field boundary can reveal the corruption (CBRD-26950). */
  OID head_oid;
  head_oid.volid = 3;
  head_oid.pageid = 4242;
  head_oid.slotid = 7;
  const std::string full_stub = make_inline_stub (head_oid, 4096, LOG_LSA (0xC0FFEE, 42));
  std::vector<char> record = make_heap_record ({ { full_stub.substr (0, OR_OID_SIZE + OR_BIGINT_SIZE), true },
    { std::string ("SENTINEL-plain value"), false } });
  RECDES recdes = recdes_over (record);
  const std::vector<char> before = record;

  oos_chain_ref ref;
  DB_BIGINT parsed_length = 0;
  er_clear ();
  EXPECT_EQ (parse_inline_ref (recdes, 0, ref, parsed_length), ER_HEAP_OOS_BAD_INLINE_HEADER);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_BAD_INLINE_HEADER);
  /* the rejected reference is well-defined and names nothing */
  EXPECT_TRUE (OID_ISNULL (&ref.head_oid));
  EXPECT_TRUE (LSA_ISNULL (&ref.identity_stamp));
  EXPECT_EQ (parsed_length, 0);
  EXPECT_EQ (record, before);
  er_clear ();
}

TEST (OosIdentityStampPureTest, ParseRejectsStubFieldPastRecordEnd)
{
  OID head_oid;
  head_oid.volid = 3;
  head_oid.pageid = 4242;
  head_oid.slotid = 7;
  std::vector<char> record = make_heap_record ({ { make_inline_stub (head_oid, 4096, LOG_LSA (0xC0FFEE, 42)), true } });
  RECDES recdes = recdes_over (record);
  /* the table still claims a 24-byte field, but the record ends inside it */
  recdes.length -= 1;

  oos_chain_ref ref;
  DB_BIGINT parsed_length = 0;
  er_clear ();
  EXPECT_EQ (parse_inline_ref (recdes, 0, ref, parsed_length), ER_HEAP_OOS_BAD_INLINE_HEADER);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_BAD_INLINE_HEADER);
  EXPECT_TRUE (OID_ISNULL (&ref.head_oid));
  EXPECT_EQ (parsed_length, 0);
  er_clear ();
}

TEST_F (OosIdentityStampTest, ReadWithMatchingReferenceReturnsTheValue)
{

  /* from_string_into_recdes stores the terminator too, so the chain is payload.size () + 1 bytes. */
  const std::string payload = "value behind a verified reference";
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);

  oos_chain_ref ref;
  ref.head_oid = oid;
  ref.identity_stamp = issued;

  std::string out (payload.size () + 1, '?');
  ASSERT_EQ (oos_read (thread_p, ref, oos_buffer (out.data (), out.size ())), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());
}

TEST_F (OosIdentityStampTest, ReadWithMismatchedStampFailsAsCorruptedRecordAndLeavesChainIntact)
{

  const std::string payload = "never returned through a stale reference";
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);

  oos_chain_ref stale;
  stale.head_oid = oid;
  stale.identity_stamp = LOG_LSA (issued.pageid + 1, (std::int16_t) issued.offset);

  std::string out (payload.size () + 1, '?');
  EXPECT_EQ (oos_read (thread_p, stale, oos_buffer (out.data (), out.size ())), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  /* A NULL stamp is an ordinary value: it mismatches a non-NULL one like any other. */
  stale.identity_stamp = NULL_LSA;
  EXPECT_EQ (oos_read (thread_p, stale, oos_buffer (out.data (), out.size ())), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  /* The chain itself is untouched: the matching reference still reads it. */
  oos_chain_ref live;
  live.head_oid = oid;
  live.identity_stamp = issued;
  ASSERT_EQ (oos_read (thread_p, live, oos_buffer (out.data (), out.size ())), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());
}

TEST_F (OosIdentityStampTest, GroupedReadVerifiesEveryReference)
{

  std::vector<std::string> payloads = { std::string (700, 'p'), std::string (900, 'q') };
  std::vector<OID> oids (payloads.size (), OID_INITIALIZER);
  std::vector<LOG_LSA> stamps (payloads.size (), NULL_LSA);
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      ASSERT_EQ (insert_with_stamp (oos_vfid, payloads[i], oids[i], stamps[i]), NO_ERROR);
    }

  std::vector<std::string> outputs (payloads.size ());
  std::vector<oos_read_request> requests;
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      outputs[i].assign (payloads[i].size () + 1, '?');
      oos_chain_ref ref;
      ref.head_oid = oids[i];
      ref.identity_stamp = stamps[i];
      requests.push_back ({ ref, oos_buffer (outputs[i].data (), outputs[i].size ()) });
    }
  ASSERT_EQ (oos_read_many (thread_p, cubbase::span<oos_read_request> (requests.data (), requests.size ())),
	     NO_ERROR);
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      EXPECT_STREQ (outputs[i].c_str (), payloads[i].c_str ());
    }

  /* One stale reference in the group fails the group with the corrupted-record error. */
  requests[1].ref.identity_stamp = LOG_LSA (stamps[1].pageid + 1, (std::int16_t) stamps[1].offset);
  EXPECT_EQ (oos_read_many (thread_p, cubbase::span<oos_read_request> (requests.data (), requests.size ())),
	     ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
}

// ===========================================================================
// Ticket 04: oos_delete requires target identity on every reclamation path
// ===========================================================================

TEST_F (OosIdentityStampTest, DeleteWithMismatchedStampIsCleanNoOpThatLeavesOccupantIntact)
{
  const std::string payload = "survives a stale delete";
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);

  oos_chain_ref stale;
  stale.head_oid = oid;
  stale.identity_stamp = LOG_LSA (issued.pageid + 1, (std::int16_t) issued.offset);

  std::vector<VPID> emptied;
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR) << "a skipped reclamation must leave no stray error";
  EXPECT_TRUE (emptied.empty ()) << "a no-op reports no reclaim candidate";

  oos_chain_ref live;
  live.head_oid = oid;
  live.identity_stamp = issued;
  std::string out;
  ASSERT_EQ (read_through (live, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());

  /* A NULL stamp is an ordinary value: it mismatches like any other and is never a wildcard. */
  stale.identity_stamp = NULL_LSA;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_TRUE (emptied.empty ());
  ASSERT_EQ (read_through (live, out), NO_ERROR);

  /* The matching reference reclaims the chain. */
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, live, &emptied), NO_ERROR);
  EXPECT_NE (read_through (live, out), NO_ERROR);
  er_clear ();
}

TEST_F (OosIdentityStampTest, DeleteOfGoneHeadIsCleanNoOp)
{
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "reclaimed once, retried twice", oid, issued), NO_ERROR);

  oos_chain_ref ref;
  ref.head_oid = oid;
  ref.identity_stamp = issued;

  std::vector<VPID> emptied;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, ref, &emptied), NO_ERROR);
  const std::size_t candidates_after_real_delete = emptied.size ();

  /* A vacuum block retry replays the same reclamation request after its effects committed: the head
   * slot is gone. That is a success with a clean error stack and no new candidate, not a failure. */
  for (int retry = 0; retry < 2; retry++)
    {
      er_clear ();
      ASSERT_EQ (oos_delete (thread_p, oos_vfid, ref, &emptied), NO_ERROR) << "retry " << retry;
      EXPECT_EQ (er_errid (), NO_ERROR) << "retry " << retry;
      EXPECT_EQ (emptied.size (), candidates_after_real_delete) << "retry " << retry;
    }
}

TEST_F (OosIdentityStampTest, StaleReferenceAfterSlotReuseKeepsTheLiveChain)
{
  /* The CBRD-26950 data loss: reclaim a chain, let a live row's insert reuse the same
   * (volid|pageid|slotid), then replay the dead row's reclamation request. */
  OID old_oid = OID_INITIALIZER;
  LOG_LSA old_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "dead row's value chain", old_oid, old_stamp), NO_ERROR);

  oos_chain_ref stale_ref;
  stale_ref.head_oid = old_oid;
  stale_ref.identity_stamp = old_stamp;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref), NO_ERROR);

  const std::string live_payload = "live row's value chain";
  OID new_oid = OID_INITIALIZER;
  LOG_LSA new_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, live_payload, new_oid, new_stamp), NO_ERROR);
  ASSERT_TRUE (OID_EQ (&new_oid, &old_oid)) << "the scenario requires physical slot reuse";
  ASSERT_FALSE (LSA_EQ (&new_stamp, &old_stamp));

  /* The block retry replays the stale OOS reference: it must no-op and never touch the live chain. */
  std::vector<VPID> emptied;
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_TRUE (emptied.empty ());

  oos_chain_ref live_ref;
  live_ref.head_oid = new_oid;
  live_ref.identity_stamp = new_stamp;
  std::string out;
  ASSERT_EQ (read_through (live_ref, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());
}

TEST_F (OosIdentityStampTest, StaleReferenceToDeallocatedAndReallocatedPageIsCleanNoOp)
{
  /* CBRD-26786 made page reallocation real: a stale OOS reference may point into a different page
   * incarnation. Reuse that machinery: a page-filling chain gets a page of its own, its delete
   * empties the page, and the committed empty page is deallocated by the reclaim batch. */
  OID old_oid = OID_INITIALIZER;
  LOG_LSA old_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, page_filling_payload (), old_oid, old_stamp), NO_ERROR);
  oos_chain_ref stale_ref;
  stale_ref.head_oid = old_oid;
  stale_ref.identity_stamp = old_stamp;

  std::vector<VPID> emptied;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref, &emptied), NO_ERROR);
  const VPID page = {old_oid.pageid, old_oid.volid};
  ASSERT_EQ (emptied.size (), 1U);
  ASSERT_TRUE (VPID_EQ (&emptied[0], &page));

  /* Reclaim requires committed deletes (the LSA gate defers a live deleter's pages). */
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (oos_reclaim_empty_pages (thread_p, oos_vfid, emptied), NO_ERROR);
  {
    VPID probe = page;
    PAGE_PTR page_ptr = NULL;
    ASSERT_EQ (pgbuf_fix_if_not_deallocated (thread_p, &probe, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr),
	       NO_ERROR);
    ASSERT_EQ (page_ptr, nullptr) << "the fixture requires the page to be deallocated";
  }

  /* Deallocated page: a clean no-op with no candidate. */
  emptied.clear ();
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_TRUE (emptied.empty ());

  /* Reallocated page: the next growth reuses the reclaimed page, so the live row's chain begins a
   * new page incarnation at the very same head OOS OID. The stale reference must still no-op and
   * the new occupant must survive. */
  const std::string live_payload = page_filling_payload ();
  OID new_oid = OID_INITIALIZER;
  LOG_LSA new_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, live_payload, new_oid, new_stamp), NO_ERROR);
  ASSERT_TRUE (OID_EQ (&new_oid, &old_oid)) << "the scenario requires the reclaimed page and slot to be reused";
  ASSERT_FALSE (LSA_EQ (&new_stamp, &old_stamp));

  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_TRUE (emptied.empty ());

  oos_chain_ref live_ref;
  live_ref.head_oid = new_oid;
  live_ref.identity_stamp = new_stamp;
  std::string out;
  ASSERT_EQ (read_through (live_ref, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());
}

TEST_F (OosIdentityStampTest, OccupancyProbeCannotTellOccupantsApart)
{
  /* Documents why the probe must never gate a delete: after slot reuse it still answers "occupied",
   * while the identity check inside oos_delete recognises the stale reference. */
  OID old_oid = OID_INITIALIZER;
  LOG_LSA old_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "first occupant", old_oid, old_stamp), NO_ERROR);
  oos_chain_ref stale_ref;
  stale_ref.head_oid = old_oid;
  stale_ref.identity_stamp = old_stamp;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref), NO_ERROR);

  OID new_oid = OID_INITIALIZER;
  LOG_LSA new_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "later occupant", new_oid, new_stamp), NO_ERROR);
  ASSERT_TRUE (OID_EQ (&new_oid, &old_oid));

  bool exists = false;
  ASSERT_EQ (oos_chunk_exists (thread_p, old_oid, &exists), NO_ERROR);
  EXPECT_TRUE (exists) << "the probe sees an occupied slot and cannot say whose";

  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale_ref), NO_ERROR);
  ASSERT_EQ (oos_chunk_exists (thread_p, new_oid, &exists), NO_ERROR);
  EXPECT_TRUE (exists) << "the identity-checked delete left the later occupant alone";
}

// ===========================================================================
// Review repair 02: stale and malformed OOS references
//
// Each test retains the original chain reference and the original payload length before the target
// changes, then drives oos_read, oos_read_many and oos_delete with that retained reference. Helpers that
// fetch the current occupant's length or stamp would fail before the API under test runs.
// ===========================================================================

TEST_F (OosIdentityStampTest, StaleReadOfMissingSlotIsAControlledFailure)
{
  const std::string payload = "value whose slot is vacated";
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);
  const std::string neighbour_payload = "neighbour that stays on the page";
  OID neighbour_oid = OID_INITIALIZER;
  LOG_LSA neighbour_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, neighbour_payload, neighbour_oid, neighbour_stamp), NO_ERROR);
  ASSERT_EQ (oid.pageid, neighbour_oid.pageid) << "the scenario keeps both chains on one page";

  oos_chain_ref stale;
  stale.head_oid = oid;
  stale.identity_stamp = issued;
  const std::size_t stale_length = payload.size () + 1;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale), NO_ERROR);

  std::string out;
  er_clear ();
  EXPECT_EQ (read_scalar (stale, stale_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  /* The grouped read fails the group at the stale request even when a live request shares the page. */
  std::string neighbour_out (neighbour_payload.size () + 1, '?');
  out.assign (stale_length, '?');
  oos_chain_ref neighbour;
  neighbour.head_oid = neighbour_oid;
  neighbour.identity_stamp = neighbour_stamp;
  oos_read_request requests[] =
  {
    { neighbour, oos_buffer (neighbour_out.data (), neighbour_out.size ()) },
    { stale, oos_buffer (out.data (), out.size ()) },
  };
  EXPECT_EQ (oos_read_many (thread_p, cubbase::span<oos_read_request> (requests, 2)), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  ASSERT_EQ (read_scalar (neighbour, neighbour_payload.size () + 1, neighbour_out), NO_ERROR);
  EXPECT_STREQ (neighbour_out.c_str (), neighbour_payload.c_str ());
}

TEST_F (OosIdentityStampTest, StaleReadOfDeallocatedPageIsAControlledFailure)
{
  const std::string payload = page_filling_payload ();
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);
  oos_chain_ref stale;
  stale.head_oid = oid;
  stale.identity_stamp = issued;
  const std::size_t stale_length = payload.size () + 1;
  reclaim_own_page (stale);

  /* A deallocated head page must be reported, not asserted on inside the page fix. */
  std::string out;
  er_clear ();
  EXPECT_EQ (read_scalar (stale, stale_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
  EXPECT_EQ (read_grouped (stale, stale_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
}

TEST_F (OosIdentityStampTest, StaleReadOfReusedHeadSlotDeliversNoBytesOfTheNewOccupant)
{
  /* Equal lengths: only the identity stamp tells the two occupants apart. */
  const std::string retired_payload (64, 'A');
  const std::string live_payload (64, 'B');
  OID retired_oid = OID_INITIALIZER;
  LOG_LSA retired_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, retired_payload, retired_oid, retired_stamp), NO_ERROR);
  oos_chain_ref stale;
  stale.head_oid = retired_oid;
  stale.identity_stamp = retired_stamp;
  const std::size_t length = retired_payload.size () + 1;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale), NO_ERROR);

  OID live_oid = OID_INITIALIZER;
  LOG_LSA live_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, live_payload, live_oid, live_stamp), NO_ERROR);
  ASSERT_TRUE (OID_EQ (&live_oid, &retired_oid)) << "the scenario requires physical slot reuse";

  /* The identity check happens before any payload byte is copied: the caller's buffer stays untouched. */
  const std::string untouched (length, '?');
  std::string out;
  er_clear ();
  EXPECT_EQ (read_scalar (stale, length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (out, untouched) << "bytes of the new occupant reached the caller's buffer";
  er_clear ();
  EXPECT_EQ (read_grouped (stale, length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (out, untouched) << "bytes of the new occupant reached the caller's buffer";
  er_clear ();

  oos_chain_ref live;
  live.head_oid = live_oid;
  live.identity_stamp = live_stamp;
  ASSERT_EQ (read_scalar (live, length, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());
}

TEST_F (OosIdentityStampTest, StaleReferenceToSlotReusedByContinuationChunkFailsReadsAndSkipsDelete)
{
  oos_chain_ref stale, live;
  std::size_t stale_length = 0;
  std::string live_payload;
  reuse_slot_with_continuation_chunk (stale, stale_length, live, live_payload);

  /* The occupant is a continuation chunk of another chain. Identity is compared first, so this is a stale
   * reference, not a head-index assertion. */
  std::string out;
  er_clear ();
  EXPECT_EQ (read_scalar (stale, stale_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
  EXPECT_EQ (read_grouped (stale, stale_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  /* A retried delete through the same stale reference is a clean skip that leaves the continuation chunk,
   * and with it the whole live chain, in place. */
  std::vector<VPID> emptied;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR) << "a skipped reclamation must leave no stray error";
  EXPECT_TRUE (emptied.empty ()) << "a no-op reports no reclaim candidate";

  const std::size_t live_length = live_payload.size () + 1;
  ASSERT_EQ (read_scalar (live, live_length, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());
  ASSERT_EQ (read_grouped (live, live_length, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());
}

TEST_F (OosIdentityStampTest, MatchingNonHeadReferenceIsRejectedBeforeAnyDelete)
{
  oos_chain_ref stale, live;
  std::size_t stale_length = 0;
  std::string live_payload;
  reuse_slot_with_continuation_chunk (stale, stale_length, live, live_payload);

  /* A reference that names the continuation chunk with the stamp it actually carries is malformed: identity
   * matches, but the target is not a chain head. */
  oos_chain_ref non_head;
  non_head.head_oid = stale.head_oid;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, non_head.head_oid, &non_head.identity_stamp), NO_ERROR);

  std::vector<VPID> emptied;
  er_clear ();
  EXPECT_EQ (oos_delete (thread_p, oos_vfid, non_head, &emptied), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD) << "a malformed head reference is an error, not a skip";
  EXPECT_TRUE (emptied.empty ());
  er_clear ();

  const std::size_t live_length = live_payload.size () + 1;
  std::string out;
  EXPECT_EQ (read_scalar (non_head, live_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
  EXPECT_EQ (read_grouped (non_head, live_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  /* Every original chunk and byte of the live chain survives, through its own reference. */
  ASSERT_EQ (read_scalar (live, live_length, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());
  ASSERT_EQ (read_grouped (live, live_length, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());
}

TEST_F (OosIdentityStampTest, StaleReferenceToPageReusedAsFileMetadataSkipsDeleteAndFailsReads)
{
  const std::string payload = page_filling_payload ();
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);
  oos_chain_ref stale;
  stale.head_oid = oid;
  stale.identity_stamp = issued;
  const std::size_t stale_length = payload.size () + 1;
  reclaim_own_page (stale);

  /* The same file reuses the page for its own metadata: the page fixes successfully but is not a slotted
   * page. Its type must be checked under the latch before any slotted-page interpretation. */
  const VPID page = { oid.pageid, oid.volid };
  reuse_reclaimed_page_as_metadata (page);
  ASSERT_TRUE (metadata_page_is_intact (page));

  std::vector<VPID> emptied;
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR) << "a skipped reclamation must leave no stray error";
  EXPECT_TRUE (emptied.empty ()) << "a no-op reports no reclaim candidate";
  EXPECT_TRUE (metadata_page_is_intact (page)) << "the stale delete modified the metadata page";

  std::string out;
  EXPECT_EQ (read_scalar (stale, stale_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
  EXPECT_EQ (read_grouped (stale, stale_length, out), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
  EXPECT_TRUE (metadata_page_is_intact (page));

  /* The diagnostics agree that no chunk is there. */
  bool exists = true;
  ASSERT_EQ (oos_chunk_exists (thread_p, oid, &exists), NO_ERROR);
  EXPECT_FALSE (exists);
  LOG_LSA stored = NULL_LSA;
  EXPECT_NE (oos_get_identity_stamp (thread_p, oid, &stored), NO_ERROR);
  er_clear ();
  EXPECT_TRUE (metadata_page_is_intact (page));

  release_metadata_page (page);
}

// ===========================================================================
// Repair ticket 03: the delete reports whether it reclaimed the chain or skipped a stale target
// ===========================================================================

TEST_F (OosIdentityStampTest, DeleteOutcomeNamesEachSkipReason)
{
  /* A reclaimed chain, then a retry of the same request: the retry finds the head slot empty. */
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "reclaimed, then retried", oid, issued), NO_ERROR);
  oos_chain_ref ref = { oid, issued };
  std::vector<VPID> emptied;
  oos_delete_outcome outcome = OOS_DELETE_SKIPPED_PAGE_GONE;
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, ref, &emptied, &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_RECLAIMED);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, ref, &emptied, &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_SKIPPED_SLOT_EMPTY);
  EXPECT_EQ (er_errid (), NO_ERROR) << "a reported skip is still a clean success";
  emptied.clear ();

  /* A deallocated head page, then the same page reused as file metadata: both are "page gone". The file
   * holds no other live chunk here, so the page-filling chain has its page to itself. */
  OID paged_oid = OID_INITIALIZER;
  LOG_LSA paged_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, page_filling_payload (), paged_oid, paged_stamp), NO_ERROR);
  oos_chain_ref gone = { paged_oid, paged_stamp };
  reclaim_own_page (gone);
  outcome = OOS_DELETE_RECLAIMED;
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, gone, &emptied, &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_SKIPPED_PAGE_GONE);
  const VPID page = { paged_oid.pageid, paged_oid.volid };
  reuse_reclaimed_page_as_metadata (page);
  outcome = OOS_DELETE_RECLAIMED;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, gone, &emptied, &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_SKIPPED_PAGE_GONE);
  EXPECT_TRUE (metadata_page_is_intact (page));
  EXPECT_TRUE (emptied.empty ());
  EXPECT_EQ (er_errid (), NO_ERROR);
  release_metadata_page (page);

  /* A stale stamp against a live occupant of the slot. */
  const std::string live_payload = "live occupant";
  OID live_oid = OID_INITIALIZER;
  LOG_LSA live_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, live_payload, live_oid, live_stamp), NO_ERROR);
  oos_chain_ref stale = { live_oid, LOG_LSA (live_stamp.pageid + 1, (std::int16_t) live_stamp.offset) };
  outcome = OOS_DELETE_RECLAIMED;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale, &emptied, &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_SKIPPED_STAMP_MISMATCH);
  EXPECT_TRUE (emptied.empty ());
  EXPECT_EQ (er_errid (), NO_ERROR);
  std::string out;
  oos_chain_ref live = { live_oid, live_stamp };
  ASSERT_EQ (read_through (live, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), live_payload.c_str ());

  /* A failed call reports no outcome at all, so a caller reading the outcome before the return code is
   * never told a chain was reclaimed. A reference with no head OOS OID is such a failure: the stub parser
   * rejects one, so it can only come from a caller passing an invalid argument. */
  oos_chain_ref headless = { OID_INITIALIZER, live_stamp };
  outcome = OOS_DELETE_RECLAIMED;
  er_clear ();
  EXPECT_EQ (oos_delete (thread_p, oos_vfid, headless, &emptied, &outcome), ER_HEAP_OOS_INVALID_ARGUMENT);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_INVALID_ARGUMENT);
  EXPECT_EQ (outcome, OOS_DELETE_OUTCOME_UNKNOWN);
  EXPECT_TRUE (emptied.empty ());
  er_clear ();
  ASSERT_EQ (read_through (live, out), NO_ERROR) << "a rejected delete touches nothing";

  /* The outcome is optional and does not change the result a caller that does not ask for it sees. */
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale, &emptied), NO_ERROR);
  ASSERT_EQ (read_through (live, out), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, live, &emptied, &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_RECLAIMED);

  /* Every outcome has its own name for diagnostics. */
  const oos_delete_outcome all[] = { OOS_DELETE_OUTCOME_UNKNOWN, OOS_DELETE_RECLAIMED, OOS_DELETE_SKIPPED_PAGE_GONE,
				     OOS_DELETE_SKIPPED_SLOT_EMPTY, OOS_DELETE_SKIPPED_STAMP_MISMATCH
				   };
  for (oos_delete_outcome a : all)
    {
      ASSERT_NE (oos_delete_outcome_string (a), nullptr);
      EXPECT_GT (std::strlen (oos_delete_outcome_string (a)), 0U);
      for (oos_delete_outcome b : all)
	{
	  if (a != b)
	    {
	      EXPECT_STRNE (oos_delete_outcome_string (a), oos_delete_outcome_string (b));
	    }
	}
    }
}

TEST_F (OosIdentityStampTest, InterruptedDeleteAndReadReportTheInterruptNotASkip)
{
  const std::string payload = "operational failures stay errors";
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);
  oos_chain_ref live;
  live.head_oid = oid;
  live.identity_stamp = issued;
  const std::size_t length = payload.size () + 1;

  /* An interrupted transaction fails its next page fix with ER_INTERRUPTED; the fix consumes the flag. */
  const int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  std::vector<VPID> emptied;
  (void) logtb_set_tran_index_interrupt (thread_p, tran_index, true);
  er_clear ();
  EXPECT_EQ (oos_delete (thread_p, oos_vfid, live, &emptied), ER_INTERRUPTED);
  EXPECT_EQ (er_errid (), ER_INTERRUPTED) << "an operational failure must not be converted to a clean skip";
  EXPECT_TRUE (emptied.empty ());
  (void) logtb_set_tran_index_interrupt (thread_p, tran_index, false);
  er_clear ();

  std::string out;
  (void) logtb_set_tran_index_interrupt (thread_p, tran_index, true);
  EXPECT_EQ (read_scalar (live, length, out), ER_INTERRUPTED);
  EXPECT_EQ (er_errid (), ER_INTERRUPTED) << "an operational failure must not be reported as a stale reference";
  (void) logtb_set_tran_index_interrupt (thread_p, tran_index, false);
  er_clear ();

  /* Nothing was deleted or skipped: the chain reads back whole. */
  ASSERT_EQ (read_scalar (live, length, out), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());
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
