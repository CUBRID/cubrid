/*
 *
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
 * test_oos_real_vacuum_server.cpp - SERVER_MODE end-to-end tests for vacuum-based
 * OOS reclaim through the REAL vacuum pipeline.
 *
 * Unlike test_oos_vacuum_server.cpp (which calls the vacuum leaf functions
 * on crafted RECDES), these tests drive the
 * whole chain:
 *
 *   MVCC heap DML (heap_insert_logical / heap_delete_logical / heap_update_logical)
 *     -> MVCC op log records carrying vacuum info
 *     -> log block completion (unittestdb is created with vacuum_log_block_pages=4
 *        to keep the filler small, but the tests loop until the block actually
 *        closes, so they remain correct for any block size)
 *     -> vacuum_wakeup_master_daemon() - unit-test-only helper that wakes the
 *        vacuum master daemon in SERVER_MODE
 *     -> vacuum master -> worker -> vacuum_heap -> vacuum_heap_oos_log_reclaim_intent, and at the sysop
 *        commit the reclaim postpone -> oos_delete (REMOVE); or, for a committed UPDATE,
 *        RVOOS_NOTIFY_VACUUM -> vacuum_oos_reclaim_notified_chains -> oos_delete (ADR-0008)
 *
 * The tests verify the MVCC notion of "stale": vacuum must NOT reclaim an OOS
 * value still visible to a live snapshot, and must reclaim it COMPLETELY
 * (every chunk of every chain) once no live transaction can see it.
 *
 * The test heap is created under the class OID of an existing system class
 * (db_user) instead of a real user class: xheap_create must read the class
 * record for the TDE algorithm, so the OID has to resolve to a real record,
 * but everything past creation is catalog-free — vacuum reads the class OID
 * from the heap page chain, the HFID from the file descriptor
 * (vacuum_heap_get_hfid_and_file_type), and the OOS VFID from the heap header
 * page (heap_oos_find_vfid). db_user is MVCC-enabled (only root, _db_serial,
 * _db_collation and db_ha_apply_info are MVCC-disabled), so the DML produces
 * real vacuum-processable MVCC log records.
 */

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <thread>

#include "fault_injection.h"
#include "mvcc.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "vacuum.h"
#include "xserver_interface.h"

#include "test_oos_server_common.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* bridge functions defined in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

// ============================================================================
// Heap RECDES builders
// ============================================================================
//
// Same binary layout as test_oos_vacuum_server.cpp, with one difference: the
// data area is over-allocated by 2 * OR_MVCCID_SIZE because the records go
// through real heap DML. heap_insert_adjust_recdes_header grows the header
// in-place by OR_MVCCID_SIZE for the insert MVCCID (and asserts the spare
// area exists); the MVCC update path may grow it again for the previous
// version LSA. VOT offsets are relative to the VOT start, so they stay valid
// when the MVCC header grows.
//

static const int HEAP_HDR_SIZE = 8;	/* OR_MVCC_REP_SIZE + OR_CHN_SIZE */
static const int VOT_ENTRY_SZ = 4;	/* OR_INT_SIZE (4-byte offset mode) */
static const int OOS_INLINE_SZ = OR_OOS_INLINE_SIZE;	/* OID + length + identity stamp */
static const int MVCC_HEADER_SPARE = 2 * OR_MVCCID_SIZE;

static int
build_heap_recdes_with_oos (const std::vector<OID> &oos_oids,
			    const std::vector<INT64> &oos_lengths,
			    RECDES &rec_out, int filler_size = 0)
{
  const int n_oos = (int) oos_oids.size ();
  assert (n_oos > 0);
  assert ((int) oos_lengths.size () == n_oos);

  /* Match the borrowed class representation at the logical-write validation boundary. */
  OID borrowed_class = OID_INITIALIZER;
  if (xlocator_find_class_oid (thread_p, "db_user", &borrowed_class, NULL_LOCK) != LC_CLASSNAME_EXIST)
    {
      return ER_FAILED;
    }
  int cache_index = -1;
  OR_CLASSREP *repr = heap_classrepr_get (thread_p, &borrowed_class, nullptr, NULL_REPRID, &cache_index);
  if (repr == nullptr)
    {
      return ER_FAILED;
    }
  const int n_var = repr->n_variable;
  const int repr_id = repr->id;
  heap_classrepr_free_and_init (repr, &cache_index);
  if (n_var < n_oos + (filler_size > 0 ? 1 : 0))
    {
      return ER_FAILED;
    }

  const int vot_bytes = (n_var + 1) * VOT_ENTRY_SZ;	/* one entry per attribute plus the terminator */
  const int data_bytes = n_oos * OOS_INLINE_SZ;
  const int total = HEAP_HDR_SIZE + vot_bytes + data_bytes + filler_size;

  int err = recdes_allocate_data_area (&rec_out, total + MVCC_HEADER_SPARE);
  if (err != NO_ERROR)
    {
      return err;
    }

  rec_out.type = REC_HOME;
  rec_out.length = total;
  std::memset (rec_out.data, 0, total);

  char *base = rec_out.data;

  /* 1. rep_and_flags: OOS flag + 4-byte offset size */
  int rep_and_flags = repr_id | (OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_4BYTE;
  OR_PUT_INT (base + OR_REP_OFFSET, rep_and_flags);

  /* 2. CHN = 0 (already zeroed) */

  /* 3. VOT entries — each stores (offset_from_vot_start | flags); the terminator entry after the last
   * attribute carries OR_VAR_BIT_LAST_ELEMENT and points at the end of the variable area, as the heap
   * writer lays the table out */
  char *vot = base + HEAP_HDR_SIZE;
  for (int i = 0; i < n_oos; i++)
    {
      OR_PUT_INT (vot + i * VOT_ENTRY_SZ, OR_SET_VAR_OOS (vot_bytes + i * OOS_INLINE_SZ));
    }
  for (int i = n_oos; i <= n_var; ++i)
    {
      /* an optional inline attribute of filler_size bytes follows the stubs, to control the record size */
      const int offset = vot_bytes + n_oos * OOS_INLINE_SZ + (i > n_oos ? filler_size : 0);
      OR_PUT_INT (vot + i * VOT_ENTRY_SZ, i == n_var ? OR_SET_VAR_LAST_ELEMENT (offset) : offset);
    }

  /* 4. OOS inline stub: OID (8b) + length (8b) + identity stamp (8b) per column. Every caller in
   * this file embeds OIDs of really inserted chunks, so the true stamp is always readable
   * (CBRD-26950). */
  char *oos_data = vot + vot_bytes;
  for (int i = 0; i < n_oos; i++)
    {
      char *slot = oos_data + i * OOS_INLINE_SZ;
      OR_PUT_OID (slot, &oos_oids[i]);
      INT64 len = oos_lengths[i];
      OR_PUT_BIGINT (slot + OR_OID_SIZE, &len);

      LOG_LSA identity_stamp = NULL_LSA;
      int stamp_err = oos_get_identity_stamp (thread_p, oos_oids[i], &identity_stamp);
      if (stamp_err != NO_ERROR)
	{
	  recdes_free_data_area (&rec_out);
	  return stamp_err;
	}
      INT64 packed_identity_stamp = oos_pack_identity_stamp (identity_stamp);
      OR_PUT_BIGINT (slot + OR_OID_SIZE + OR_BIGINT_SIZE, &packed_identity_stamp);
    }

  return NO_ERROR;
}

/* Plain (no OOS) heap record used as log filler. */
static int
build_plain_heap_recdes (int data_size, RECDES &rec_out)
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

// ============================================================================
// MVCC DML + transaction helpers
// ============================================================================

static void
commit_current_tran ()
{
  TRAN_STATE state = xtran_server_commit (thread_p, false);
  ASSERT_EQ (state, TRAN_UNACTIVE_COMMITTED);
}

static int
heap_insert_mvcc (HFID &hfid, OID &class_oid, HEAP_SCANCACHE &scan_cache, RECDES &recdes, OID &oid_out)
{
  HEAP_OPERATION_CONTEXT ctx;
  heap_create_insert_context (&ctx, &hfid, &class_oid, &recdes, &scan_cache);
  int err = heap_insert_logical (thread_p, &ctx, NULL);
  if (err == NO_ERROR)
    {
      oid_out = ctx.res_oid;
    }
  return err;
}

static int
heap_delete_mvcc (HFID &hfid, OID &class_oid, HEAP_SCANCACHE &scan_cache, OID &oid)
{
  HEAP_OPERATION_CONTEXT ctx;
  heap_create_delete_context (&ctx, &hfid, &oid, &class_oid, &scan_cache);
  return heap_delete_logical (thread_p, &ctx);
}

static int
heap_update_mvcc (HFID &hfid, OID &class_oid, HEAP_SCANCACHE &scan_cache, OID &oid, RECDES &new_recdes)
{
  HEAP_OPERATION_CONTEXT ctx;
  heap_create_update_context (&ctx, &hfid, &oid, &class_oid, &new_recdes, &scan_cache, UPDATE_INPLACE_NONE);
  return heap_update_logical (thread_p, &ctx);
}

// ============================================================================
// Vacuum eligibility + polling helpers
// ============================================================================

static VACUUM_LOG_BLOCKID
current_log_blockid ()
{
  return vacuum_get_log_blockid (log_Gl.prior_info.prior_lsa.load ().pageid);
}

/* Vacuum data entries are produced only when the log crosses a block boundary,
 * so the block containing the interesting MVCC op must be closed before the
 * master daemon can ever see it. On top of that, the master refuses entries
 * whose start_lsa page is within one page of the append head ("too close to
 * end of log", vacuum_master_task::is_cursor_entry_ready_to_vacuum), so push
 * the append head two full blocks past `blockid`, not just one. */
static void
close_log_block_containing (HFID &hfid, OID &class_oid, HEAP_SCANCACHE &scan_cache, VACUUM_LOG_BLOCKID blockid)
{
  const int FILLER_SIZE = 4096;
  int guard = 0;

  while (current_log_blockid () <= blockid + 1)
    {
      ASSERT_LT (guard++, 20000) << "log block did not close; filler ineffective?";

      RECDES filler {};
      ASSERT_EQ (build_plain_heap_recdes (FILLER_SIZE, filler), NO_ERROR);

      OID dummy = OID_INITIALIZER;
      int err = heap_insert_mvcc (hfid, class_oid, scan_cache, filler, dummy);
      recdes_free_data_area (&filler);
      ASSERT_EQ (err, NO_ERROR);
    }
  /* One commit for the whole filler: a commit per row forces a log flush per row, and under machine load those
   * flushes, not vacuum, dominated the case time. */
  commit_current_tran ();
}

/* Poll `pred`, nudging the vacuum master daemon each round (same entry point
 * csql ';vacuum' uses). Returns true as soon as pred holds. */
static bool
wait_for_vacuum (const std::function<bool ()> &pred, int timeout_sec)
{
  const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (timeout_sec);

  while (std::chrono::steady_clock::now () < deadline)
    {
      if (pred ())
	{
	  return true;
	}
      (void) vacuum_wakeup_master_daemon ();
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
  return pred ();
}

/* Pre-shaped scancache so the DML paths never call heap_get_class_info:
 * the borrowed class OID maps to db_user's REAL heap in the hfid cache,
 * not to this test heap, so the class-info path must be avoided. */
static void
start_heap_scan_cache (HFID &hfid, const OID &class_oid, HEAP_SCANCACHE &scan_cache)
{
  heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, &hfid);
  COPY_OID (&scan_cache.node.class_oid, &class_oid);
  scan_cache.file_type = FILE_HEAP;
  scan_cache.mvcc_disabled_class = false;
  scan_cache.page_latch = PGBUF_LATCH_WRITE;
  /* Never keep pages fixed between operations — the vacuum worker needs
   * write latches on these pages while the test polls. */
  scan_cache.cache_last_fix_page = false;
}

// ============================================================================
// Test fixture: real heap + attached OOS file per test
// ============================================================================

class OosRealVacuum : public ::testing::Test
{
  protected:
    HFID hfid;
    VFID oos_vfid;
    OID class_oid;
    HEAP_SCANCACHE scan_cache;

    void SetUp () override
    {
      HFID_SET_NULL (&hfid);
      VFID_SET_NULL (&oos_vfid);

      /* Borrow the OID of a real, MVCC-enabled system class: xheap_create reads
       * the class record (TDE algorithm), so the OID must resolve to a record. */
      OID_SET_NULL (&class_oid);
      ASSERT_EQ (xlocator_find_class_oid (thread_p, "db_user", &class_oid, NULL_LOCK), LC_CLASSNAME_EXIST);
      ASSERT_FALSE (OID_ISNULL (&class_oid));
      ASSERT_FALSE (mvcc_is_mvcc_disabled_class (&class_oid));

      ASSERT_EQ (xheap_create (thread_p, &hfid, &class_oid, false), NO_ERROR);
      ASSERT_TRUE (heap_oos_find_vfid (thread_p, &hfid, &oos_vfid, true));
      ASSERT_FALSE (VFID_ISNULL (&oos_vfid));
      commit_current_tran ();

      start_heap_scan_cache (hfid, class_oid, scan_cache);
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

    /* Live OOS record count (each chunk of a chain is one record). -1 on error. */
    int oos_live_recs ()
    {
      OOS_STATS_INFO info;
      if (oos_get_stats_by_vfid (thread_p, oos_vfid, &info) != NO_ERROR)
	{
	  return -1;
	}
      return info.num_recs;
    }

    /* Insert one OOS payload + one heap row referencing it; commits. */
    void insert_row_with_oos (const std::string &payload, OID &heap_oid_out, OID &oos_oid_out)
    {
      RECDES oos_rec {};
      ASSERT_EQ (test_oos_utils::from_string_into_recdes (payload, oos_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);

      oos_oid_out = OID_INITIALIZER;
      ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid_out), NO_ERROR);

      RECDES heap_rec {};
      ASSERT_EQ (build_heap_recdes_with_oos ({oos_oid_out}, { (INT64) oos_rec.length}, heap_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

      heap_oid_out = OID_INITIALIZER;
      ASSERT_EQ (heap_insert_mvcc (hfid, class_oid, scan_cache, heap_rec, heap_oid_out), NO_ERROR);
      ASSERT_FALSE (OID_ISNULL (&heap_oid_out));

      commit_current_tran ();
    }

    /* MVCC-delete a heap row, commit, and remember the block to close. */
    void delete_row_and_close_block (OID &heap_oid)
    {
      ASSERT_EQ (heap_delete_mvcc (hfid, class_oid, scan_cache, heap_oid), NO_ERROR);
      VACUUM_LOG_BLOCKID delete_block = current_log_blockid ();
      commit_current_tran ();

      close_log_block_containing (hfid, class_oid, scan_cache, delete_block);
    }

    /* MVCC-update heap_oid so it references a freshly inserted OOS payload;
     * commit and close the update's log block. The previous version's OOS
     * becomes update-superseded (reclaimed through the UPDATE notification). Returns
     * the new OOS OID via out param. Mirrors TC-R3's inline update sequence. */
    void update_row_to_new_oos (OID &heap_oid, const std::string &payload, OID &new_oos_oid_out)
    {
      RECDES new_oos_rec {};
      ASSERT_EQ (test_oos_utils::from_string_into_recdes (payload, new_oos_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_oos (&new_oos_rec, recdes_free_data_area);

      new_oos_oid_out = OID_INITIALIZER;
      ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, new_oos_rec, new_oos_oid_out), NO_ERROR);

      RECDES new_heap_rec {};
      ASSERT_EQ (build_heap_recdes_with_oos ({new_oos_oid_out}, { (INT64) new_oos_rec.length}, new_heap_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_heap (&new_heap_rec, recdes_free_data_area);

      ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, new_heap_rec), NO_ERROR);
      VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
      commit_current_tran ();

      close_log_block_containing (hfid, class_oid, scan_cache, update_block);
    }

    void expect_oos_gone (const OID &oos_oid, const char *what)
    {
      RECDES out {};
      int err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, out);
      EXPECT_NE (err, NO_ERROR) << what << " must be unreadable after vacuum";
      if (out.data != nullptr)
	{
	  recdes_free_data_area (&out);
	}
      er_clear ();
    }

    /* Race-free "is this OOS gone?" poll predicate. oos_read/oos_get_length use
     * an UNCONDITIONAL page latch, so a still-present record reads back
     * correctly even while a vacuum worker holds the page — unlike oos_live_recs()
     * (oos_get_stats_by_vfid uses a CONDITIONAL latch and skips busy pages, which
     * can transiently undercount mid-drain and falsely satisfy a non-zero target
     * count). Use this to wait for a specific dead OOS to actually disappear. */
    bool oos_unreadable (const OID &oos_oid)
    {
      RECDES out {};
      int err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, out);
      if (out.data != nullptr)
	{
	  recdes_free_data_area (&out);
	}
      er_clear ();
      return err != NO_ERROR;
    }
};

// ============================================================================
// TC-R1: Single row DELETE — real vacuum drains the OOS completely
// ============================================================================
TEST_F (OosRealVacuum, SingleRowDeleteDrainsCompletely)
{
  OID heap_oid, oos_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, oos_oid);

  ASSERT_GT (oos_live_recs (), 0);

  delete_row_and_close_block (heap_oid);

  ASSERT_TRUE (wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 60))
      << "vacuum did not drain the stale OOS record; live recs = " << oos_live_recs ();

  expect_oos_gone (oos_oid, "single-chunk OOS");
}

// ============================================================================
// TC-R2: Multi-chunk chains (2-chunk + 160KB) — every chunk reclaimed
//
// oos_live_recs() counts each chunk as one record, so "== 0" proves the whole
// chain was reclaimed, not just the head.
// ============================================================================
TEST_F (OosRealVacuum, MultiChunkChainsDrainCompletely)
{
  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  OID heap_oid_two_chunks, oos_oid_two_chunks;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (max_chunk + 100),
		       heap_oid_two_chunks, oos_oid_two_chunks);

  OID heap_oid_large, oos_oid_large;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (160 * 1024),
		       heap_oid_large, oos_oid_large);

  int recs_before_delete = oos_live_recs ();
  ASSERT_GE (recs_before_delete, 3) << "expected at least 2 + 2 chunks before delete";

  delete_row_and_close_block (heap_oid_two_chunks);
  delete_row_and_close_block (heap_oid_large);

  ASSERT_TRUE (wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 60))
      << "vacuum left OOS chunks behind; live recs = " << oos_live_recs ();

  expect_oos_gone (oos_oid_two_chunks, "two-chunk OOS chain");
  expect_oos_gone (oos_oid_large, "160KB OOS chain");
}

// ============================================================================
// TC-R3: MVCC UPDATE — old version's OOS drained, new version survives
//
// Exercises the vacuum reclaim of update-superseded OOS through the committed UPDATE's
// RVOOS_NOTIFY_VACUUM (A', ADR-0008)
// (RVHF_UPDATE_NOTIFY_VACUUM path).
// ============================================================================
TEST_F (OosRealVacuum, UpdateStaleVersionDrainsNewSurvives)
{
  OID heap_oid, old_oos_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, old_oos_oid);

  const int recs_one_row = oos_live_recs ();
  ASSERT_GT (recs_one_row, 0);

  /* New OOS value + MVCC update of the heap row to reference it. */
  RECDES new_oos_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096),
	     new_oos_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&new_oos_rec, recdes_free_data_area);

  OID new_oos_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, new_oos_rec, new_oos_oid), NO_ERROR);

  RECDES new_heap_rec {};
  ASSERT_EQ (build_heap_recdes_with_oos ({new_oos_oid}, { (INT64) new_oos_rec.length}, new_heap_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&new_heap_rec, recdes_free_data_area);

  ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, new_heap_rec), NO_ERROR);
  VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();

  close_log_block_containing (hfid, class_oid, scan_cache, update_block);

  /* Old version OOS must drain; new version OOS must survive. */
  ASSERT_TRUE (wait_for_vacuum ([this, recs_one_row] { return oos_live_recs () == recs_one_row; }, 60))
      << "vacuum did not drain exactly the superseded OOS; live recs = " << oos_live_recs ();

  expect_oos_gone (old_oos_oid, "update-superseded OOS");

  RECDES survivor {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, new_oos_oid, survivor), NO_ERROR)
      << "new-version OOS must survive vacuum";
  ASSERT_EQ (survivor.length, new_oos_rec.length);
  recdes_free_data_area (&survivor);
}

// ============================================================================
// TC-R3b: A notification whose MVCCID is older than its block's oldest visible MVCCID
//
// The commit postpone logs RVOOS_NOTIFY_VACUUM after logtb_complete_mvcc, so the global
// oldest visible MVCCID may already have passed the UPDATE's MVCCID when the record lands in a
// block. The notification must not lower that block's oldest visible MVCCID below the one of an
// earlier block (vacuum keeps them non-decreasing), and vacuum must still reclaim the chains.
// ============================================================================
TEST_F (OosRealVacuum, NotifyOlderThanBlockOldestKeepsBlockOrderAndDrains)
{
  /* A committed transaction with MVCCID m0 that leaves a chain no row references. */
  RECDES oos_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096), oos_rec),
	     NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);
  OID oos_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid), NO_ERROR);
  const MVCCID m0 = logtb_get_current_mvccid (thread_p);
  ASSERT_TRUE (MVCCID_IS_VALID (m0));
  commit_current_tran ();

  LOG_LSA stamp = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, oos_oid, &stamp), NO_ERROR);

  /* Let the vacuum master move the global oldest visible MVCCID past m0, then fill whole blocks whose
   * oldest visible MVCCID is above m0. */
  ASSERT_TRUE (wait_for_vacuum ([m0] { return log_Gl.mvcc_table.get_global_oldest_visible () > m0; }, 30));
  close_log_block_containing (hfid, class_oid, scan_cache, current_log_blockid ());

  /* Another transaction opens the next block, then the notification arrives with m0. */
  RECDES filler {};
  ASSERT_EQ (build_plain_heap_recdes (4096, filler), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_filler (&filler, recdes_free_data_area);
  OID filler_oid = OID_INITIALIZER;
  ASSERT_EQ (heap_insert_mvcc (hfid, class_oid, scan_cache, filler, filler_oid), NO_ERROR);
  const MVCCID block_oldest = log_Gl.hdr.oldest_visible_mvccid;
  ASSERT_GT (block_oldest, m0);

  std::vector<char> payload = oos_test_make_reclaim_intent_payload (oos_vfid, { oos_chain_ref { oos_oid, stamp } });
  LOG_DATA_ADDR addr (&hfid.vfid, NULL, -1);
  log_append_mvcc_undo_data_with_mvccid (thread_p, RVOOS_NOTIFY_VACUUM, &addr, (int) payload.size (),
					 payload.data (), m0);
  EXPECT_EQ (log_Gl.hdr.oldest_visible_mvccid, block_oldest)
      << "the notification lowered its block's oldest visible MVCCID";
  VACUUM_LOG_BLOCKID notify_block = current_log_blockid ();
  commit_current_tran ();

  close_log_block_containing (hfid, class_oid, scan_cache, notify_block);

  ASSERT_TRUE (wait_for_vacuum ([this, oos_oid] { return oos_unreadable (oos_oid); }, 60))
      << "vacuum did not reclaim the chain of the notification";
}

// ============================================================================
// TC-R3c: A log block that holds only notifications
//
// A bulk UPDATE's commit postpone can fill a whole block with notifications whose MVCCID precedes
// the block's oldest visible MVCCID, so the block's newest MVCCID is below its oldest. Vacuum must
// produce that block and reclaim the chains its notifications name.
// ============================================================================
TEST_F (OosRealVacuum, NotificationOnlyBlockIsProducedAndVacuumed)
{
  RECDES oos_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096), oos_rec),
	     NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);
  OID oos_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid), NO_ERROR);
  const MVCCID m0 = logtb_get_current_mvccid (thread_p);
  ASSERT_TRUE (MVCCID_IS_VALID (m0));
  commit_current_tran ();

  LOG_LSA stamp = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, oos_oid, &stamp), NO_ERROR);

  ASSERT_TRUE (wait_for_vacuum ([m0] { return log_Gl.mvcc_table.get_global_oldest_visible () > m0; }, 30));
  close_log_block_containing (hfid, class_oid, scan_cache, current_log_blockid ());

  /* Notifications only, from the middle of one block to the start of the block after next: the block in between
   * holds nothing else, and its first notification names the chain. */
  LOG_DATA_ADDR addr (&hfid.vfid, NULL, -1);
  std::vector<char> empty_payload = oos_test_make_reclaim_intent_payload (oos_vfid, {});
  std::vector<char> chain_payload = oos_test_make_reclaim_intent_payload (oos_vfid, { oos_chain_ref { oos_oid, stamp } });
  auto append_notify = [&] (const std::vector<char> &payload)
  {
    log_append_mvcc_undo_data_with_mvccid (thread_p, RVOOS_NOTIFY_VACUUM, &addr, (int) payload.size (),
					   payload.data (), m0);
  };

  const VACUUM_LOG_BLOCKID first_block = current_log_blockid ();
  int guard = 0;
  while (current_log_blockid () == first_block)
    {
      ASSERT_LT (guard++, 1000000) << "the notifications did not cross a block";
      append_notify (empty_payload);
    }
  const VACUUM_LOG_BLOCKID notify_only_block = current_log_blockid ();
  append_notify (chain_payload);
  while (current_log_blockid () == notify_only_block)
    {
      ASSERT_LT (guard++, 1000000) << "the notifications did not cross a block";
      append_notify (empty_payload);
    }
  VACUUM_LOG_BLOCKID last_block = current_log_blockid ();
  commit_current_tran ();

  close_log_block_containing (hfid, class_oid, scan_cache, last_block);

  ASSERT_TRUE (wait_for_vacuum ([this, oos_oid] { return oos_unreadable (oos_oid); }, 60))
      << "vacuum did not reclaim the chain named in the notification-only block";
}

// ============================================================================
// TC-R4: A live snapshot blocks reclaim; releasing it lets vacuum drain
//
// This is the definition of "stale": vacuum may only reclaim OOS no alive
// transaction can view.
// ============================================================================
TEST_F (OosRealVacuum, SnapshotBlocksReclaimThenDrains)
{
  OID heap_oid, oos_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, oos_oid);

  /* Open a reader transaction with a snapshot that predates the delete. */
  int worker_idx = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  TRAN_STATE reader_state;
  int reader_idx = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &reader_state,
		   TRAN_LOCK_INFINITE_WAIT, TRAN_REPEATABLE_READ);
  ASSERT_NE (reader_idx, NULL_TRAN_INDEX);
  ASSERT_NE (logtb_get_mvcc_snapshot (thread_p), nullptr);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  delete_row_and_close_block (heap_oid);

  /* Grace window: vacuum must hold back while the snapshot is alive. */
  bool drained_early = wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 3);
  EXPECT_FALSE (drained_early) << "vacuum reclaimed an OOS value still visible to a live snapshot";

  RECDES still_there {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, still_there), NO_ERROR)
      << "OOS visible to a live snapshot must remain readable";
  recdes_free_data_area (&still_there);

  /* Release the reader; the value becomes truly stale. */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_idx);
  (void) log_abort (thread_p, reader_idx);
  logtb_release_tran_index (thread_p, reader_idx);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  ASSERT_TRUE (wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 60))
      << "vacuum did not drain after the blocking snapshot was released";

  expect_oos_gone (oos_oid, "post-snapshot stale OOS");
}

// ============================================================================
// TC-R5: Two readers — vacuum respects the OLDEST live snapshot, not "any gone"
//
// TC-R4 opens a single reader, so "reader released" coincides with "horizon
// advanced" and cannot distinguish "oldest snapshot respected" from "any
// snapshot gone". Here two snapshots are taken before the DELETE; releasing the
// NEWER one (B) must NOT unblock reclaim, because the OLDER one (A) still pins
// the global-oldest-visible horizon below the delete MVCCID.
// ============================================================================
TEST_F (OosRealVacuum, TwoReadersOldestSnapshotGatesReclaim)
{
  OID heap_oid, oos_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, oos_oid);

  int worker_idx = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* Reader A — snapshot taken first; predates the delete. */
  TRAN_STATE st_a;
  int reader_a = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &st_a,
					  TRAN_LOCK_INFINITE_WAIT, TRAN_REPEATABLE_READ);
  ASSERT_NE (reader_a, NULL_TRAN_INDEX);
  ASSERT_NE (logtb_get_mvcc_snapshot (thread_p), nullptr);	/* under reader_a */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  /* Reader B — also predates the delete, but its snapshot is newer than A's. */
  TRAN_STATE st_b;
  int reader_b = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &st_b,
					  TRAN_LOCK_INFINITE_WAIT, TRAN_REPEATABLE_READ);
  ASSERT_NE (reader_b, NULL_TRAN_INDEX);
  ASSERT_NE (logtb_get_mvcc_snapshot (thread_p), nullptr);	/* under reader_b */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  delete_row_and_close_block (heap_oid);

  /* Release the NEWER reader (B); the OLDER reader (A) still sees the row, so
   * the global horizon must NOT advance past the delete. */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_b);
  (void) log_abort (thread_p, reader_b);
  logtb_release_tran_index (thread_p, reader_b);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  bool drained_after_b = wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 3);
  EXPECT_FALSE (drained_after_b)
      << "vacuum reclaimed while an older snapshot (reader A) could still see the OOS";

  RECDES still_there {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, still_there), NO_ERROR)
      << "OOS visible to the older snapshot must remain readable";
  recdes_free_data_area (&still_there);

  /* Release the OLDER reader (A) — now no live snapshot can see it. */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_a);
  (void) log_abort (thread_p, reader_a);
  logtb_release_tran_index (thread_p, reader_a);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  ASSERT_TRUE (wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 60))
      << "vacuum did not drain after the oldest snapshot was released; live recs = " << oos_live_recs ();
  expect_oos_gone (oos_oid, "post-two-reader stale OOS");
}

// ============================================================================
// TC-R6: A live snapshot gates the UPDATE notification reclaim too
//
// TC-R4 only gates the DELETE/REMOVE path. The UPDATE path (RVOOS_NOTIFY_VACUUM under the UPDATE's
// MVCCID, exercised by TC-R3) has its own visibility check;
// this pins that a reader whose snapshot predates the UPDATE keeps the
// superseded old-version OOS alive until the reader is released.
// ============================================================================
TEST_F (OosRealVacuum, UpdateSnapshotBlocksOldVersionReclaim)
{
  OID heap_oid, oos1_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, oos1_oid);

  const int recs_one_row = oos_live_recs ();
  ASSERT_GT (recs_one_row, 0);

  /* Open a reader whose snapshot predates the UPDATE — it still sees oos1. */
  int worker_idx = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  TRAN_STATE reader_state;
  int reader_idx = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &reader_state,
		   TRAN_LOCK_INFINITE_WAIT, TRAN_REPEATABLE_READ);
  ASSERT_NE (reader_idx, NULL_TRAN_INDEX);
  ASSERT_NE (logtb_get_mvcc_snapshot (thread_p), nullptr);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  /* UPDATE the row onto a new OOS (oos2); oos1 becomes update-superseded. */
  OID oos2_oid;
  update_row_to_new_oos (heap_oid, test_oos_utils::make_repeated_pattern_string (4096), oos2_oid);

  /* Grace window: while the pre-update reader is alive, oos1 must survive — the
   * total must not fall back to a single row's worth of chunks. */
  bool drained_early = wait_for_vacuum ([this, recs_one_row] { return oos_live_recs () == recs_one_row; }, 3);
  EXPECT_FALSE (drained_early)
      << "vacuum reclaimed the update-superseded OOS still visible to a live snapshot";

  RECDES still_there {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oos1_oid, still_there), NO_ERROR)
      << "old-version OOS visible to a live snapshot must remain readable";
  recdes_free_data_area (&still_there);

  /* Release the reader; the old version becomes truly stale. */
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_idx);
  (void) log_abort (thread_p, reader_idx);
  logtb_release_tran_index (thread_p, reader_idx);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);

  ASSERT_TRUE (wait_for_vacuum ([this, &oos1_oid] { return oos_unreadable (oos1_oid); }, 60))
      << "vacuum did not drain the superseded OOS after the snapshot was released; live recs = "
      << oos_live_recs ();

  expect_oos_gone (oos1_oid, "update-superseded OOS (gated by snapshot)");

  RECDES survivor {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oos2_oid, survivor), NO_ERROR)
      << "new-version OOS must survive vacuum";
  recdes_free_data_area (&survivor);
}

// ============================================================================
// TC-R7: Negative control — a live (never-deleted) row's OOS is never reclaimed
//
// Nothing else proves the drains are CAUSED by vacuum reclaiming dead versions
// rather than some incidental eager path. Closing several blocks and nudging
// the daemon must leave a live row's OOS fully intact.
// ============================================================================
TEST_F (OosRealVacuum, LiveRowNeverReclaimed)
{
  OID heap_oid, oos_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, oos_oid);

  const int recs_one_row = oos_live_recs ();
  ASSERT_GT (recs_one_row, 0);

  /* Close several log blocks so the daemon has plenty of completed work to chew,
   * but never delete the row. */
  for (int i = 0; i < 3; i++)
    {
      close_log_block_containing (hfid, class_oid, scan_cache, current_log_blockid ());
    }

  /* A live row's OOS must NOT vanish no matter how often the daemon runs. */
  bool wrongly_drained = wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 3);
  EXPECT_FALSE (wrongly_drained) << "vacuum reclaimed the OOS of a live (undeleted) row";
  EXPECT_GE (oos_live_recs (), recs_one_row) << "live row's OOS record count shrank";

  RECDES still_there {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, still_there), NO_ERROR)
      << "live row's OOS must remain readable the whole time";
  ASSERT_GT (still_there.length, 0);
  recdes_free_data_area (&still_there);
}

// ============================================================================
// TC-R8: Multi-version forward chain — every stale version drains, newest lives
//
// Generalizes TC-R3's single stale version: INSERT(oos1) -> UPDATE(oos2) ->
// UPDATE(oos3) leaves TWO stale versions. Vacuum must reclaim oos1 AND oos2
// (the whole stale lineage), not just the immediately previous version.
// ============================================================================
TEST_F (OosRealVacuum, MultiVersionForwardChainDrains)
{
  OID heap_oid, oos1_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, oos1_oid);

  const int recs_one_row = oos_live_recs ();
  ASSERT_GT (recs_one_row, 0);

  /* Pre-insert BOTH replacement OOS payloads before either update creates a dead
   * version. With every oos_insert finished before the first reclaim can happen,
   * the live daemon can never recycle a freed slot into a later insert — which
   * would otherwise alias an earlier OID onto a live record and make this test
   * flaky. The updates below only rewrite the heap row; they never insert OOS. */
  RECDES oos2_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096),
	     oos2_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos2 (&oos2_rec, recdes_free_data_area);
  OID oos2_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos2_rec, oos2_oid), NO_ERROR);

  RECDES oos3_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096),
	     oos3_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos3 (&oos3_rec, recdes_free_data_area);
  OID oos3_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos3_rec, oos3_oid), NO_ERROR);

  /* v1 -> v2 (now references oos2): the original version's oos1 becomes stale. */
  RECDES heap_rec_v2 {};
  ASSERT_EQ (build_heap_recdes_with_oos ({oos2_oid}, { (INT64) oos2_rec.length}, heap_rec_v2), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_v2 (&heap_rec_v2, recdes_free_data_area);
  ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, heap_rec_v2), NO_ERROR);
  commit_current_tran ();

  /* v2 -> v3 (now references oos3): oos2 becomes stale too — two stale versions. */
  RECDES heap_rec_v3 {};
  ASSERT_EQ (build_heap_recdes_with_oos ({oos3_oid}, { (INT64) oos3_rec.length}, heap_rec_v3), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_v3 (&heap_rec_v3, recdes_free_data_area);
  ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, heap_rec_v3), NO_ERROR);
  VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();

  close_log_block_containing (hfid, class_oid, scan_cache, update_block);

  /* Both superseded versions (oos1, oos2) drain; only the newest (oos3) remains.
   * Poll on the records actually disappearing rather than the live-rec count: the
   * two concurrent chunk deletions can transiently undercount the stats walk. */
  ASSERT_TRUE (wait_for_vacuum ([this, &oos1_oid, &oos2_oid]
  {
    return oos_unreadable (oos1_oid) && oos_unreadable (oos2_oid);
  }, 60))
      << "vacuum did not drain the full stale lineage; live recs = " << oos_live_recs ();

  expect_oos_gone (oos1_oid, "first stale version OOS");
  expect_oos_gone (oos2_oid, "second stale version OOS");

  RECDES survivor {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oos3_oid, survivor), NO_ERROR)
      << "newest-version OOS must survive vacuum";
  recdes_free_data_area (&survivor);
}

// ============================================================================
// TC-R9: Chunk-boundary arithmetic — exactly one chunk vs. one byte over
//
// oos_insert keeps a single in-page chunk while stored length <= max_chunk and
// spills across pages otherwise. from_string_into_recdes appends a NUL byte, so
// a string of length N stores N+1 bytes: (max_chunk - 1) stores exactly
// max_chunk (1 chunk) and max_chunk stores max_chunk + 1 (spills to 2 chunks).
// oos_live_recs() counts one record per chunk, so this pins the count arithmetic
// at the boundary on both the insert and the reclaim walk.
// ============================================================================
TEST_F (OosRealVacuum, ChunkBoundaryExactSizes)
{
  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
  ASSERT_GT (max_chunk, 1);

  OID heap_exact, oos_exact;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (max_chunk - 1), heap_exact, oos_exact);
  ASSERT_EQ (oos_live_recs (), 1) << "payload at the exact single-chunk boundary must be one chunk";

  OID heap_plus1, oos_plus1;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (max_chunk), heap_plus1, oos_plus1);
  ASSERT_EQ (oos_live_recs (), 3) << "payload one byte over the boundary must spill to two chunks";

  delete_row_and_close_block (heap_exact);
  delete_row_and_close_block (heap_plus1);

  ASSERT_TRUE (wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 60))
      << "boundary-sized OOS chains not fully reclaimed; live recs = " << oos_live_recs ();

  expect_oos_gone (oos_exact, "exact single-chunk-boundary OOS");
  expect_oos_gone (oos_plus1, "one-over-boundary 2-chunk OOS");
}

// ============================================================================
// TC-R10: Re-vacuum after a full drain is idempotent (no-crash double pass)
//
// After a drain, waking the daemon again over the now-empty chains must keep
// the count at 0 and must not crash. Guards the double-pass path (distinct from
// crash recovery, which is out of scope for this file).
// ============================================================================
TEST_F (OosRealVacuum, ReVacuumAfterDrainIsIdempotent)
{
  OID heap_oid, oos_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (4096), heap_oid, oos_oid);
  ASSERT_GT (oos_live_recs (), 0);

  delete_row_and_close_block (heap_oid);
  ASSERT_TRUE (wait_for_vacuum ([this] { return oos_live_recs () == 0; }, 60))
      << "vacuum did not drain; live recs = " << oos_live_recs ();
  expect_oos_gone (oos_oid, "drained OOS");

  /* Wake the daemon repeatedly over the already-empty chains. */
  for (int i = 0; i < 20; i++)
    {
      (void) vacuum_wakeup_master_daemon ();
      std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }

  EXPECT_EQ (oos_live_recs (), 0) << "re-vacuum after drain disturbed the empty OOS file";
  expect_oos_gone (oos_oid, "drained OOS after re-vacuum");
}

static bool
insert_id_vacuumed (const OID &oid)
{
  VPID vpid;
  VPID_GET_FROM_OID (&vpid, &oid);
  PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      er_clear ();
      return false;
    }
  RECDES rec {};
  MVCC_REC_HEADER mvcc_header;
  bool vacuumed = spage_get_record (thread_p, page, oid.slotid, &rec, PEEK) == S_SUCCESS
		  && or_mvcc_get_header (&rec, &mvcc_header) == NO_ERROR
		  && !MVCC_IS_FLAG_SET (&mvcc_header, OR_MVCC_FLAG_VALID_INSID);
  pgbuf_unfix_and_init (thread_p, page);
  return vacuumed;
}

// ============================================================================
// TC-R11: Vacuum waits for the heap header without holding the last heap page
//
// heap_alloc_new_pages holds the heap header and then latches the last heap page. Page latches have
// no deadlock detection, so a vacuum worker that holds the last page while it waits for the header
// stalls both sides until the latch timeout. The test thread plays the allocator: it holds the header
// until the vacuum worker queues on it, then checks that the last page is free and that the real
// allocator finishes.
// ============================================================================
TEST_F (OosRealVacuum, VacuumWaitsForHeapHeaderWithoutHoldingLastPage)
{
  RECDES oos_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096), oos_rec),
	     NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);
  OID oos_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid), NO_ERROR);

  RECDES heap_rec {};
  ASSERT_EQ (build_heap_recdes_with_oos ({oos_oid}, { (INT64) oos_rec.length}, heap_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  VPID last_vpid = VPID_INITIALIZER;
  PGBUF_WATCHER last_watcher;
  ASSERT_EQ (heap_alloc_new_pages (thread_p, &hfid, 1, &last_vpid, &last_watcher), NO_ERROR);
  HEAP_OPERATION_CONTEXT insert_ctx;
  heap_create_insert_context (&insert_ctx, &hfid, &class_oid, &heap_rec, &scan_cache);
  ASSERT_EQ (heap_insert_logical (thread_p, &insert_ctx, &last_watcher), NO_ERROR);
  const OID heap_oid = insert_ctx.res_oid;
  ASSERT_EQ (heap_oid.pageid, last_vpid.pageid);
  ASSERT_EQ (heap_oid.volid, last_vpid.volid);
  VACUUM_LOG_BLOCKID insert_block = current_log_blockid ();
  commit_current_tran ();

  /* Take the header before the insert's log block closes, so vacuum cannot reach the row first. */
  VPID header_vpid;
  header_vpid.volid = hfid.vfid.volid;
  header_vpid.pageid = hfid.hpgid;
  PGBUF_WATCHER header_watcher;
  PGBUF_INIT_WATCHER (&header_watcher, PGBUF_ORDERED_HEAP_HDR, &hfid);
  ASSERT_EQ (pgbuf_ordered_fix (thread_p, &header_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, &header_watcher), NO_ERROR);

  /* Ending a transaction requires that it hold no pages, so close the insert's log block with OOS
   * inserts, which are not MVCC operations, and with the log flush a commit would do. Vacuum skips
   * blocks near the end of the flushed log. */
  bool log_advanced = true;
  for (int guard = 0; log_advanced && current_log_blockid () <= insert_block + 1; guard++)
    {
      OID filler_oid = OID_INITIALIZER;
      log_advanced = guard < 20000
		     && test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, filler_oid) == NO_ERROR;
    }
  if (log_advanced)
    {
      LOG_LSA flush_lsa = log_Gl.prior_info.prior_lsa;
      logpb_flush_pages (thread_p, &flush_lsa);
    }

  const bool vacuum_waits_for_header = log_advanced
				       && wait_for_vacuum ([&header_watcher] { return pgbuf_has_any_waiters (header_watcher.pgptr); }, 60);
  bool last_page_free = false;
  bool allocation_finished = false;
  if (vacuum_waits_for_header)
    {
      PAGE_PTR last_page = pgbuf_fix (thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_CONDITIONAL_LATCH);
      last_page_free = last_page != NULL;
      if (last_page_free)
	{
	  pgbuf_unfix_and_init (thread_p, last_page);

	  /* Only now is the allocator safe to run: with the last page held by vacuum it would block. */
	  VPID next_vpid = VPID_INITIALIZER;
	  PGBUF_WATCHER next_watcher;
	  allocation_finished = heap_alloc_new_pages (thread_p, &hfid, 1, &next_vpid, &next_watcher) == NO_ERROR;
	  if (allocation_finished)
	    {
	      pgbuf_ordered_unfix (thread_p, &next_watcher);
	    }
	}
      er_clear ();
    }
  pgbuf_ordered_unfix (thread_p, &header_watcher);
  commit_current_tran ();

  ASSERT_TRUE (log_advanced) << "OOS filler failed to close the insert's log block";
  ASSERT_TRUE (vacuum_waits_for_header) << "vacuum never queued on the heap header";
  EXPECT_TRUE (last_page_free)
      << "vacuum held the last heap page while it waited for the heap header; an allocator holding the header "
      "would wait for that page until the latch timeout";
  EXPECT_TRUE (!last_page_free || allocation_finished) << "heap_alloc_new_pages failed while vacuum waited";

  EXPECT_TRUE (wait_for_vacuum ([&heap_oid] { return insert_id_vacuumed (heap_oid); }, 60))
      << "vacuum did not finish the row after the header was released";
  EXPECT_FALSE (oos_unreadable (oos_oid)) << "vacuum of a live row lost its OOS value";
}

/* Record type in the row's slot. REC_UNKNOWN means the slot is gone, and -1 means the page cannot be fixed. */
static int
slot_record_type (const OID &oid)
{
  VPID vpid;
  VPID_GET_FROM_OID (&vpid, &oid);
  PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      er_clear ();
      return -1;
    }
  SPAGE_SLOT *slot = spage_get_slot (page, oid.slotid);
  int type = slot != NULL ? (int) slot->record_type : (int) REC_UNKNOWN;
  pgbuf_unfix_and_init (thread_p, page);
  return type;
}

/* Page of the forward record a REC_RELOCATION slot points to, or NULL_PAGEID. */
static PAGEID
forward_page_of (const OID &oid)
{
  VPID vpid;
  VPID_GET_FROM_OID (&vpid, &oid);
  PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      er_clear ();
      return NULL_PAGEID;
    }
  OID forward_oid = OID_INITIALIZER;
  RECDES forward_recdes;
  forward_recdes.data = (char *) &forward_oid;
  forward_recdes.area_size = sizeof (forward_oid);
  const bool copied = spage_get_record (thread_p, page, oid.slotid, &forward_recdes, COPY) == S_SUCCESS;
  pgbuf_unfix_and_init (thread_p, page);
  return copied ? forward_oid.pageid : NULL_PAGEID;
}

static bool
row_removed (int record_type)
{
  return record_type == REC_UNKNOWN || record_type == REC_MARKDELETED || record_type == REC_DELETED_WILL_REUSE;
}

/* HEAP_PAGE_VACUUM_STATUS of the row's page, or -1 if the page cannot be fixed. */
static int
page_vacuum_status (const OID &oid)
{
  VPID vpid;
  VPID_GET_FROM_OID (&vpid, &oid);
  PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      er_clear ();
      return -1;
    }
  int status = (int) heap_page_get_vacuum_status (thread_p, page);
  pgbuf_unfix_and_init (thread_p, page);
  return status;
}

/* What vacuum did with a deleted row while the test held the row's heap header past the page latch timeout. */
struct header_hold_observation
{
  VACUUM_LOG_BLOCKID delete_block = VACUUM_NULL_LOG_BLOCKID;
  bool log_advanced = false;
  bool vacuum_queued = false;
  bool header_lookup_failed = false;
  bool vacuum_still_waits = false;
  int type_while_held = -1;
  bool oos_readable_while_held = false;
  int type_after = -1;
  bool oos_reclaimed = false;
  int page_status_while_held = -1;
  int page_status_after = -1;
};

/* A plain row of a second heap, deleted by the same transaction in the log block after the OOS row's delete, or with
 * same_block in the same log block. Vacuum never needs an OOS file for it, so only the order of vacuum work decides
 * when it goes. */
struct unrelated_row
{
  HFID hfid;
  HEAP_SCANCACHE scan_cache;
  OID oid = OID_INITIALIZER;
  bool same_block = false;
  VACUUM_LOG_BLOCKID delete_block = VACUUM_NULL_LOG_BLOCKID;
  long long removed_during_hold_ms = -1;
  bool removed_after_release = false;
};

static int
count_occurrences (const std::string &text, const std::string &needle)
{
  int n = 0;
  for (size_t pos = text.find (needle); pos != std::string::npos; pos = text.find (needle, pos + needle.size ()))
    {
      n++;
    }
  return n;
}

// ============================================================================
// TC-R12: A failed heap header lookup must not leak the OOS value of a deleted row (CBRD-27933)
//
// Runs with a short page latch timeout (CTest test_oos_real_vacuum_latch_timeout). The test thread holds the
// heap header past that timeout, so the vacuum worker's wait for the header fails while it prepares the
// deleted row. Vacuum must not remove the row's heap slot while it keeps the OOS value. Once the header is free
// again, both must be gone. Debug builds stop at a page latch timeout, because pgbuf_timed_sleep asserts when a
// thread with an infinite lock wait times out. There the fault injection FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT
// makes the wait for the held header fail at once instead.
// ============================================================================
class OosRealVacuumHeaderHeld : public OosRealVacuum
{
  protected:
    int latch_timeout_msecs = 0;
    size_t server_log_start = 0;

    void SetUp () override
    {
      OosRealVacuum::SetUp ();
      latch_timeout_msecs = prm_get_integer_value (PRM_ID_PAGE_LATCH_TIMEOUT_IN_MSECS);
      if (latch_timeout_msecs > 10 * 1000)
	{
	  GTEST_SKIP () << "page latch timeout " << latch_timeout_msecs << " ms; set CUBRID_PAGE_LATCH_TIMEOUT_IN_MSECS";
	}
#if !defined (NDEBUG)
      ASSERT_TRUE (FI_INSERTED (FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT))
	  << "a debug build stops at the page latch timeout; set CUBRID_FAULT_INJECTION_IDS="
	  << FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT;
#endif
      server_log_start = server_log ().size ();
    }

    /* The server error log written since this case started. A boot in the same minute appends to the same file. */
    std::string server_log ()
    {
      std::ifstream in (er_get_msglog_filename ());
      std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char> ());
      return text.size () >= server_log_start ? text.substr (server_log_start) : text;
    }

    /* Each failed OOS file lookup of a vacuum worker logs one ER_VACUUM_OOS_LOOKUP_DEFERRED. */
    int deferrals ()
    {
      return count_occurrences (server_log (), "CODE = " + std::to_string (ER_VACUUM_OOS_LOOKUP_DEFERRED) + ",");
    }

    int interrupted_runs (VACUUM_LOG_BLOCKID block)
    {
      return count_occurrences (server_log (), "Processing log block " + std::to_string (block) + " is interrupted");
    }

    /* The server log time of each run's "interrupted" line, in milliseconds since midnight. */
    std::vector<long long> interrupted_times_ms (VACUUM_LOG_BLOCKID block)
    {
      const std::string log = server_log ();
      const std::string line = "Processing log block " + std::to_string (block) + " is interrupted";
      std::vector<long long> times;
      for (size_t pos = log.find (line); pos != std::string::npos; pos = log.find (line, pos + line.size ()))
	{
	  int h, m, s, ms;
	  const size_t stamp = log.rfind ("Time: ", pos);
	  if (stamp != std::string::npos
	      && sscanf (log.c_str () + stamp, "Time: %*d/%*d/%*d %d:%d:%d.%d", &h, &m, &s, &ms) == 4)
	    {
	      times.push_back (((h * 60LL + m) * 60 + s) * 1000 + ms);
	    }
	}
      return times;
    }

    /* Release builds see vacuum queue on the held header. Debug builds never wait there, so they see the first
     * deferral instead. */
    bool vacuum_met_header (PAGE_PTR header_page, int deferrals_before)
    {
#if defined (NDEBUG)
      return pgbuf_has_any_waiters (header_page);
#else
      return deferrals () > deferrals_before;
#endif
    }

    /* OOS inserts are not MVCC operations, so they move the log past a block without giving vacuum work. */
    bool fill_log_past (VACUUM_LOG_BLOCKID block)
    {
      RECDES filler_rec {};
      if (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096), filler_rec)
	  != NO_ERROR)
	{
	  return false;
	}
      test_oos_utils::auto_freed_recdes_ptr defer_filler (&filler_rec, recdes_free_data_area);
      for (int guard = 0; current_log_blockid () <= block; guard++)
	{
	  OID filler_oid = OID_INITIALIZER;
	  if (guard >= 20000
	      || test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, filler_rec, filler_oid) != NO_ERROR)
	    {
	      return false;
	    }
	}
      return true;
    }

    /* Vacuum skips log blocks near the end of the flushed log. */
    static void flush_log ()
    {
      LOG_LSA flush_lsa = log_Gl.prior_info.prior_lsa;
      logpb_flush_pages (thread_p, &flush_lsa);
    }

    bool close_block_with_oos_fillers (VACUUM_LOG_BLOCKID block)
    {
      if (!fill_log_past (block + 1))
	{
	  return false;
	}
      commit_current_tran ();
      flush_log ();
      return true;
    }

    /* Holds the heap header while the log blocks up to last_block close. A transaction must hold no pages when it
     * ends, so this one stays open until release_header. */
    bool hold_header_and_close_blocks (PGBUF_WATCHER &header_watcher, VACUUM_LOG_BLOCKID last_block)
    {
      VPID header_vpid = { hfid.hpgid, hfid.vfid.volid };
      PGBUF_INIT_WATCHER (&header_watcher, PGBUF_ORDERED_HEAP_HDR, &hfid);
      if (pgbuf_ordered_fix (thread_p, &header_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, &header_watcher) != NO_ERROR
	  || !fill_log_past (last_block + 1))
	{
	  return false;
	}
      flush_log ();
      return true;
    }

    void release_header (PGBUF_WATCHER &header_watcher)
    {
      er_clear ();
      if (header_watcher.pgptr != NULL)
	{
	  pgbuf_ordered_unfix (thread_p, &header_watcher);
	}
      commit_current_tran ();
    }

    /* A second heap with one live plain row, plus a row with an OOS value on a new page of this heap. Both inserts
     * are vacuumed. vacuum_heap walks heaps in file id order, and the second heap comes after this one. */
    void prepare_unrelated_row (unrelated_row &other, OID &heap_oid, OID &oos_oid)
    {
      std::vector<HFID> lower;
      for (int attempt = 0; attempt < 20; attempt++)
	{
	  ASSERT_EQ (xheap_create (thread_p, &other.hfid, &class_oid, false), NO_ERROR);
	  if (other.hfid.vfid.fileid > hfid.vfid.fileid)
	    {
	      break;
	    }
	  lower.push_back (other.hfid);
	}
      for (HFID &h : lower)
	{
	  (void) xheap_destroy (thread_p, &h, &class_oid);
	}
      commit_current_tran ();
      ASSERT_GT (other.hfid.vfid.fileid, hfid.vfid.fileid);
      start_heap_scan_cache (other.hfid, class_oid, other.scan_cache);
      RECDES plain_rec {};
      ASSERT_EQ (build_plain_heap_recdes (64, plain_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_plain (&plain_rec, recdes_free_data_area);
      ASSERT_EQ (heap_insert_mvcc (other.hfid, class_oid, other.scan_cache, plain_rec, other.oid), NO_ERROR);
      commit_current_tran ();

      insert_row_on_new_page (false, heap_oid, oos_oid);
      ASSERT_FALSE (HasFatalFailure ());

      /* Vacuum the inserts first, so the delete is the only vacuum the OOS row's page expects. The fillers go to the
       * second heap and leave that page alone. */
      close_log_block_containing (other.hfid, class_oid, other.scan_cache, current_log_blockid ());
      ASSERT_TRUE (wait_for_vacuum ([&heap_oid, &other]
      {
	return insert_id_vacuumed (heap_oid) && insert_id_vacuumed (other.oid);
      }, 60)) << "vacuum did not finish the inserts";
    }

    void drop_unrelated_row (unrelated_row &other)
    {
      (void) heap_scancache_end (thread_p, &other.scan_cache);
      (void) xheap_destroy (thread_p, &other.hfid, &class_oid);
      commit_current_tran ();
    }

    /* Inserts a row with a new OOS value on a new heap page. The header page also stores rows, and holding the
     * header would then block vacuum on the home page itself. A live row without OOS shares the page, so vacuum
     * never empties the page and removes it. With fill_page that row takes all but 256 bytes, so the first row
     * cannot grow in place. */
    void insert_row_on_new_page (bool fill_page, OID &heap_oid_out, OID &oos_oid_out)
    {
      RECDES oos_rec {};
      ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096),
		 oos_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);
      ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid_out), NO_ERROR);

      RECDES heap_rec {};
      ASSERT_EQ (build_heap_recdes_with_oos ({oos_oid_out}, { (INT64) oos_rec.length}, heap_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

      VPID home_vpid = VPID_INITIALIZER;
      PGBUF_WATCHER home_watcher;
      ASSERT_EQ (heap_alloc_new_pages (thread_p, &hfid, 1, &home_vpid, &home_watcher), NO_ERROR);
      HEAP_OPERATION_CONTEXT insert_ctx;
      heap_create_insert_context (&insert_ctx, &hfid, &class_oid, &heap_rec, &scan_cache);
      ASSERT_EQ (heap_insert_logical (thread_p, &insert_ctx, &home_watcher), NO_ERROR);
      heap_oid_out = insert_ctx.res_oid;
      ASSERT_EQ (heap_oid_out.pageid, home_vpid.pageid);
      ASSERT_EQ (heap_oid_out.volid, home_vpid.volid);

      PGBUF_WATCHER anchor_watcher;
      PGBUF_INIT_WATCHER (&anchor_watcher, PGBUF_ORDERED_HEAP_NORMAL, &hfid);
      ASSERT_EQ (pgbuf_ordered_fix (thread_p, &home_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, &anchor_watcher), NO_ERROR);
      const int anchor_size = fill_page ? DB_ALIGN (spage_get_free_space (thread_p, anchor_watcher.pgptr) - 256,
			      INT_ALIGNMENT) : 64;
      RECDES anchor_rec {};
      ASSERT_EQ (build_plain_heap_recdes (anchor_size, anchor_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_anchor (&anchor_rec, recdes_free_data_area);
      HEAP_OPERATION_CONTEXT anchor_ctx;
      heap_create_insert_context (&anchor_ctx, &hfid, &class_oid, &anchor_rec, &scan_cache);
      ASSERT_EQ (heap_insert_logical (thread_p, &anchor_ctx, &anchor_watcher), NO_ERROR);
      ASSERT_EQ (anchor_ctx.res_oid.pageid, home_vpid.pageid);
      commit_current_tran ();
    }

    /* Takes the free space of the header page, which also stores rows, so that a relocated row moves elsewhere. */
    void fill_header_page ()
    {
      VPID header_vpid = { hfid.hpgid, hfid.vfid.volid };
      PGBUF_WATCHER header_watcher;
      PGBUF_INIT_WATCHER (&header_watcher, PGBUF_ORDERED_HEAP_HDR, &hfid);
      ASSERT_EQ (pgbuf_ordered_fix (thread_p, &header_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, &header_watcher), NO_ERROR);
      RECDES filler_rec {};
      ASSERT_EQ (build_plain_heap_recdes (DB_ALIGN (spage_get_free_space (thread_p, header_watcher.pgptr) - 256,
					  INT_ALIGNMENT), filler_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_filler (&filler_rec, recdes_free_data_area);
      HEAP_OPERATION_CONTEXT ctx;
      heap_create_insert_context (&ctx, &hfid, &class_oid, &filler_rec, &scan_cache);
      ASSERT_EQ (heap_insert_logical (thread_p, &ctx, &header_watcher), NO_ERROR);
      ASSERT_EQ (ctx.res_oid.pageid, hfid.hpgid);
      commit_current_tran ();
    }

    header_hold_observation observe_delete_with_header_held (const char *label, OID &heap_oid, const OID &oos_oid,
	bool stop_vacuum = false, unrelated_row *other = nullptr)
    {
      const char *hold_env = std::getenv ("OOS_TEST_HEADER_HOLD_MSECS");
      const int hold_msecs = hold_env != NULL ? std::atoi (hold_env) : other != nullptr ? 60 * 1000 :
			     3 * latch_timeout_msecs;
      header_hold_observation obs;

      /* Read before the delete. With an unrelated row in the next block, vacuum can take the delete's block as soon as
       * it commits, so nothing slow may run between the commit and the header fix. */
      const int deferrals_before = deferrals ();
      /* Two deletes fit in a log block that they start. */
      if (other != nullptr && other->same_block && !fill_log_past (current_log_blockid ()))
	{
	  return obs;
	}
      if (heap_delete_mvcc (hfid, class_oid, scan_cache, heap_oid) != NO_ERROR)
	{
	  return obs;
	}
      const VACUUM_LOG_BLOCKID delete_block = obs.delete_block = current_log_blockid ();
      VACUUM_LOG_BLOCKID last_block = delete_block;
      if (other != nullptr)
	{
	  /* The open transaction keeps vacuum off the first block until the header is held. */
	  if (!other->same_block && !fill_log_past (delete_block))
	    {
	      return obs;
	    }
	  if (heap_delete_mvcc (other->hfid, class_oid, other->scan_cache, other->oid) != NO_ERROR)
	    {
	      return obs;
	    }
	  other->delete_block = last_block = current_log_blockid ();
	}
      commit_current_tran ();

      VPID header_vpid;
      header_vpid.volid = hfid.vfid.volid;
      header_vpid.pageid = hfid.hpgid;
      PGBUF_WATCHER header_watcher;
      PGBUF_INIT_WATCHER (&header_watcher, PGBUF_ORDERED_HEAP_HDR, &hfid);
      if (pgbuf_ordered_fix (thread_p, &header_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, &header_watcher) != NO_ERROR)
	{
	  return obs;
	}

      /* A transaction must hold no pages when it ends, so OOS inserts and a log flush close the block. */
      obs.log_advanced = fill_log_past (last_block + 1);
      if (obs.log_advanced)
	{
	  flush_log ();
	}

      obs.vacuum_queued = obs.log_advanced && wait_for_vacuum ([this, &header_watcher, deferrals_before]
      {
	return vacuum_met_header (header_watcher.pgptr, deferrals_before);
      }, 60);
      obs.page_status_while_held = page_vacuum_status (heap_oid);
      printf ("[CBRD-27933] %s: page latch timeout %d ms; vacuum workers %d; delete log block %lld; "
	      "page vacuum status %d; vacuum queued on heap header %d|%d: %d\n", label, latch_timeout_msecs,
	      prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT), (long long) delete_block, obs.page_status_while_held,
	      header_vpid.volid, header_vpid.pageid, (int) obs.vacuum_queued);
      if (other != nullptr)
	{
	  printf ("[CBRD-27933] %s: unrelated row %d|%d|%d of heap %d|%d|%d deleted in log block %lld\n", label,
		  other->oid.volid, other->oid.pageid, other->oid.slotid, other->hfid.hpgid, other->hfid.vfid.volid,
		  other->hfid.vfid.fileid, (long long) other->delete_block);
	}
      fflush (stdout);
      const auto hold_start = std::chrono::steady_clock::now ();
      auto held_for = [&hold_start]
      {
	return (long long) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now ()
	    - hold_start).count ();
      };
      if (obs.vacuum_queued && stop_vacuum)
	{
	  /* Stopping the workers does not wake a worker that waits for a page latch. The call returns after that wait
	   * ends. */
	  vacuum_stop_workers (thread_p);
	}
      else if (obs.vacuum_queued)
	{
	  while (held_for () < hold_msecs)
	    {
	      if (other != nullptr && other->removed_during_hold_ms < 0 && row_removed (slot_record_type (other->oid)))
		{
		  other->removed_during_hold_ms = held_for ();
		}
	      if (other != nullptr && other->removed_during_hold_ms >= 0 && deferrals () > deferrals_before)
		{
		  break;
		}
	      std::this_thread::sleep_for (std::chrono::milliseconds (std::min (100LL, hold_msecs - held_for ())));
	    }
	}
      const long long held_ms = held_for ();
      obs.header_lookup_failed = deferrals () > deferrals_before;
      obs.vacuum_still_waits = pgbuf_has_any_waiters (header_watcher.pgptr);
      obs.type_while_held = slot_record_type (heap_oid);
      obs.oos_readable_while_held = !oos_unreadable (oos_oid);
      printf ("[CBRD-27933] %s: header held for %lld ms%s: vacuum waits=%d, slot type=%d, oos readable=%d\n", label,
	      held_ms, stop_vacuum ? " until vacuum workers stopped" : "", (int) obs.vacuum_still_waits,
	      obs.type_while_held, (int) obs.oos_readable_while_held);
      if (other != nullptr)
	{
	  printf ("[CBRD-27933] %s: unrelated row removed during hold: %d, after %lld ms of %lld\n", label,
		  (int) (other->removed_during_hold_ms >= 0), other->removed_during_hold_ms, held_ms);
	}
      fflush (stdout);
      er_clear ();
      pgbuf_ordered_unfix (thread_p, &header_watcher);
      commit_current_tran ();

      obs.oos_reclaimed = stop_vacuum ? oos_unreadable (oos_oid)
			  : wait_for_vacuum ([this, &oos_oid] { return oos_unreadable (oos_oid); }, 30);
      obs.type_after = slot_record_type (heap_oid);
      obs.page_status_after = page_vacuum_status (heap_oid);
      printf ("[CBRD-27933] %s: header released: slot type=%d, oos reclaimed=%d, page vacuum status %d\n", label,
	      obs.type_after, (int) obs.oos_reclaimed, obs.page_status_after);
      if (other != nullptr)
	{
	  other->removed_after_release = wait_for_vacuum ([other] { return row_removed (slot_record_type (other->oid)); },
					 30);
	  printf ("[CBRD-27933] %s: unrelated row removed after release: %d\n", label, (int) other->removed_after_release);
	}
      fflush (stdout);
      return obs;
    }

    static void expect_no_leak (const header_hold_observation &obs)
    {
      ASSERT_TRUE (obs.log_advanced) << "OOS filler failed to close the delete's log block";
      ASSERT_TRUE (obs.vacuum_queued) << "vacuum never queued on the heap header";
      EXPECT_FALSE (row_removed (obs.type_while_held) && obs.oos_readable_while_held)
	  << "vacuum removed the deleted row's heap slot while its OOS value stayed stored";
      EXPECT_TRUE (row_removed (obs.type_after)) << "vacuum never removed the deleted row";
      EXPECT_TRUE (obs.oos_reclaimed) << "vacuum never reclaimed the deleted row's OOS value";
    }
};

TEST_F (OosRealVacuumHeaderHeld, HomeRowOosReclaimedAfterHeaderLookupFailure)
{
  OID heap_oid = OID_INITIALIZER, oos_oid = OID_INITIALIZER;
  insert_row_on_new_page (false, heap_oid, oos_oid);
  ASSERT_FALSE (HasFatalFailure ());
  ASSERT_EQ (slot_record_type (heap_oid), (int) REC_HOME);

  expect_no_leak (observe_delete_with_header_held ("REC_HOME", heap_oid, oos_oid));
}

// A header lookup that fails while the server shuts down must leave the deleted row for vacuum after restart.
TEST_F (OosRealVacuumHeaderHeld, ShutdownDuringHeaderLookupFailureLeavesHomeRowToVacuum)
{
  OID heap_oid = OID_INITIALIZER, oos_oid = OID_INITIALIZER;
  insert_row_on_new_page (false, heap_oid, oos_oid);
  ASSERT_FALSE (HasFatalFailure ());

  const header_hold_observation obs = observe_delete_with_header_held ("REC_HOME shutdown", heap_oid, oos_oid, true);
  ASSERT_TRUE (obs.log_advanced) << "OOS filler failed to close the delete's log block";
  ASSERT_TRUE (obs.vacuum_queued) << "vacuum never queued on the heap header";
  EXPECT_FALSE (row_removed (obs.type_after) && !obs.oos_reclaimed)
      << "vacuum stopped with the deleted row's heap slot removed and its OOS value still stored";
}

TEST_F (OosRealVacuumHeaderHeld, RelocatedRowOosReclaimedAfterHeaderLookupFailure)
{
  /* A forward record on the header page would make vacuum wait for the header to latch the forward page,
   * before it reaches the OOS lookup. */
  fill_header_page ();
  ASSERT_FALSE (HasFatalFailure ());
  OID heap_oid = OID_INITIALIZER, old_oos_oid = OID_INITIALIZER;
  insert_row_on_new_page (true, heap_oid, old_oos_oid);
  ASSERT_FALSE (HasFatalFailure ());

  /* The heap relocates a row that grows past the free space of its page. */
  RECDES oos_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096), oos_rec),
	     NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);
  OID oos_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid), NO_ERROR);
  RECDES grown_rec {};
  ASSERT_EQ (build_heap_recdes_with_oos ({oos_oid}, { (INT64) oos_rec.length}, grown_rec, 2048), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_grown (&grown_rec, recdes_free_data_area);
  ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, grown_rec), NO_ERROR);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();
  ASSERT_EQ (slot_record_type (heap_oid), (int) REC_RELOCATION);
  ASSERT_NE (forward_page_of (heap_oid), hfid.hpgid) << "the forward record must not live on the header page";

  /* Let vacuum finish the update first, so the only header lookup left is the one for the delete. */
  close_log_block_containing (hfid, class_oid, scan_cache, update_block);
  ASSERT_TRUE (wait_for_vacuum ([this, &old_oos_oid] { return oos_unreadable (old_oos_oid); }, 60))
      << "vacuum did not reclaim the superseded version's OOS value";

  expect_no_leak (observe_delete_with_header_held ("REC_RELOCATION", heap_oid, oos_oid));
}

// While the OOS lookup of one heap keeps failing, vacuum must still reach a later log block of another heap. With one
// vacuum worker (CUBRID_VACUUM_WORKER_COUNT=1), a worker that retries the failing page in place never gets there.
TEST_F (OosRealVacuumHeaderHeld, UnrelatedHeapVacuumedWhileHeaderLookupKeepsFailing)
{
  unrelated_row other;
  OID heap_oid = OID_INITIALIZER, oos_oid = OID_INITIALIZER;
  prepare_unrelated_row (other, heap_oid, oos_oid);
  ASSERT_FALSE (HasFatalFailure ());

  const header_hold_observation obs = observe_delete_with_header_held ("unrelated heap", heap_oid, oos_oid, false,
				      &other);
  drop_unrelated_row (other);

  expect_no_leak (obs);
  EXPECT_TRUE (obs.header_lookup_failed) << "vacuum did not fail the OOS lookup while the header was held";
  EXPECT_GE (other.removed_during_hold_ms, 0)
      << "vacuum never reached the unrelated heap's later log block while the OOS lookup kept failing";
}

// A failed OOS file lookup leaves the rest of its log block to vacuum. The unrelated heap comes after the failing
// one, so a worker that gave up the block at the failure would not reach its row until the header is free.
TEST_F (OosRealVacuumHeaderHeld, UnrelatedRowInSameBlockVacuumedWhileHeaderLookupKeepsFailing)
{
  unrelated_row other;
  other.same_block = true;
  OID heap_oid = OID_INITIALIZER, oos_oid = OID_INITIALIZER;
  prepare_unrelated_row (other, heap_oid, oos_oid);
  ASSERT_FALSE (HasFatalFailure ());

  const header_hold_observation obs = observe_delete_with_header_held ("unrelated row in the same block", heap_oid,
				      oos_oid, false, &other);
  drop_unrelated_row (other);

  ASSERT_EQ (other.delete_block, obs.delete_block) << "both deletes must share one log block";
  expect_no_leak (obs);
  EXPECT_TRUE (obs.header_lookup_failed) << "vacuum did not fail the OOS lookup while the header was held";
  EXPECT_GE (other.removed_during_hold_ms, 0)
      << "vacuum left the rest of the log block while the OOS lookup of another heap kept failing";
  EXPECT_TRUE (other.removed_after_release);
}

// Every page of a heap repeats the OOS file lookup. While it fails, a run of the log block must look up that heap
// once, not once per page. With a real latch timeout each lookup waits for the header.
TEST_F (OosRealVacuumHeaderHeld, FailingHeapCostsOneLookupPerRun)
{
  OID heap_oids[2], oos_oids[2];
  for (int i = 0; i < 2; i++)
    {
      insert_row_on_new_page (false, heap_oids[i], oos_oids[i]);
      ASSERT_FALSE (HasFatalFailure ());
    }
  ASSERT_NE (heap_oids[0].pageid, heap_oids[1].pageid);
  ASSERT_TRUE (close_block_with_oos_fillers (current_log_blockid ()));
  ASSERT_TRUE (wait_for_vacuum ([&heap_oids]
  {
    return insert_id_vacuumed (heap_oids[0]) && insert_id_vacuumed (heap_oids[1]);
  }, 60)) << "vacuum did not finish the inserts";

  /* Two deletes fit in a log block that they start. */
  ASSERT_TRUE (fill_log_past (current_log_blockid ()));
  const VACUUM_LOG_BLOCKID block = current_log_blockid ();
  for (OID &heap_oid : heap_oids)
    {
      ASSERT_EQ (heap_delete_mvcc (hfid, class_oid, scan_cache, heap_oid), NO_ERROR);
    }
  ASSERT_EQ (current_log_blockid (), block) << "both deletes must share one log block";
  commit_current_tran ();

  PGBUF_WATCHER header_watcher;
  const bool held = hold_header_and_close_blocks (header_watcher, block);
  const bool rerun = held && wait_for_vacuum ([this, block] { return interrupted_runs (block) >= 3; }, 120);
  bool kept = true;
  for (int i = 0; i < 2; i++)
    {
      kept = kept && !row_removed (slot_record_type (heap_oids[i])) && !oos_unreadable (oos_oids[i]);
    }
  release_header (header_watcher);

  ASSERT_TRUE (held) << "OOS filler failed to close the delete's log block";
  ASSERT_TRUE (rerun) << "vacuum did not run the delete's log block three times while the header was held";
  EXPECT_TRUE (kept) << "vacuum removed a deleted row of the failing heap, or its OOS value, while the header was held";
  EXPECT_TRUE (wait_for_vacuum ([this, &oos_oids]
  {
    return oos_unreadable (oos_oids[0]) && oos_unreadable (oos_oids[1]);
  }, 30)) << "vacuum never reclaimed the OOS values after the header was released";
  for (OID &heap_oid : heap_oids)
    {
      EXPECT_TRUE (row_removed (slot_record_type (heap_oid))) << "vacuum never removed a deleted row";
    }
  const int runs = interrupted_runs (block);
  const int lookups = deferrals ();
  printf ("[CBRD-27933] one lookup per run: block %lld interrupted %d times, %d deferred lookups\n", (long long) block,
	  runs, lookups);
  fflush (stdout);
  EXPECT_EQ (lookups, runs) << "a run of the log block looked up the failing heap's OOS file once per page";
}

// A failure that comes back at once must not re-run its log block on every vacuum master iteration.
TEST_F (OosRealVacuumHeaderHeld, ImmediateFailureRerunsAtMostOncePerSecond)
{
#if defined (NDEBUG)
  GTEST_SKIP () << "a real page latch timeout already spaces the runs";
#endif
  OID heap_oid = OID_INITIALIZER, oos_oid = OID_INITIALIZER;
  insert_row_on_new_page (false, heap_oid, oos_oid);
  ASSERT_FALSE (HasFatalFailure ());
  ASSERT_TRUE (close_block_with_oos_fillers (current_log_blockid ()));
  ASSERT_TRUE (wait_for_vacuum ([&heap_oid] { return insert_id_vacuumed (heap_oid); }, 60))
      << "vacuum did not finish the insert";

  ASSERT_EQ (heap_delete_mvcc (hfid, class_oid, scan_cache, heap_oid), NO_ERROR);
  const VACUUM_LOG_BLOCKID block = current_log_blockid ();
  commit_current_tran ();

  PGBUF_WATCHER header_watcher;
  const bool held = hold_header_and_close_blocks (header_watcher, block);
  /* The first failure's call stack dump resolves symbols for seconds, so the window starts after the second run. */
  const bool rerun = held && wait_for_vacuum ([this, block] { return interrupted_runs (block) >= 2; }, 120);
  const int window_s = 10;
  int runs = -1;
  if (rerun)
    {
      const int before = interrupted_runs (block);
      std::this_thread::sleep_for (std::chrono::seconds (window_s));
      runs = interrupted_runs (block) - before;
    }
  /* A run logs its interruption before the master learns of it, so the next run starts more than the interval after
   * that line. The first gap counts too. */
  const long long day_ms = 24 * 60 * 60 * 1000LL;
  const long long retry_interval_ms = 1000;
  const std::vector<long long> times = interrupted_times_ms (block);
  long long min_gap_ms = -1;
  for (size_t i = 1; i < times.size (); i++)
    {
      const long long gap_ms = (times[i] - times[i - 1] + day_ms) % day_ms;
      min_gap_ms = min_gap_ms < 0 ? gap_ms : std::min (min_gap_ms, gap_ms);
    }
  const bool kept = !row_removed (slot_record_type (heap_oid)) && !oos_unreadable (oos_oid);
  release_header (header_watcher);
  printf ("[CBRD-27933] immediate failure: block %lld interrupted %d times in %d s, %zu runs in all with %lld ms "
	  "between the closest two\n", (long long) block, runs, window_s, times.size (), min_gap_ms);
  fflush (stdout);

  ASSERT_TRUE (held) << "OOS filler failed to close the delete's log block";
  ASSERT_TRUE (rerun) << "vacuum did not run the delete's log block twice while the header was held";
  EXPECT_LE (runs, window_s + 1) << "vacuum re-ran an interrupted log block more often than once a second";
  EXPECT_GE (runs, 1) << "vacuum stopped re-running the interrupted log block";
  EXPECT_GE (min_gap_ms, retry_interval_ms)
      << "vacuum re-ran an interrupted log block less than a second after it failed";
  EXPECT_TRUE (kept) << "vacuum removed the deleted row or its OOS value while the header was held";
  EXPECT_TRUE (wait_for_vacuum ([this, &oos_oid] { return oos_unreadable (oos_oid); }, 30))
      << "vacuum never reclaimed the OOS value after the header was released";
  EXPECT_TRUE (row_removed (slot_record_type (heap_oid))) << "vacuum never removed the deleted row";
}

// A committed UPDATE's notification names only the heap; vacuum looks up its OOS file at the heap header. A failed
// lookup leaves the block for a later run instead of stopping the server, and the chain is reclaimed once the
// header is free (ADR-0008 with CBRD-27933).
TEST_F (OosRealVacuumHeaderHeld, UpdateNotificationDeferredWhileHeaderHeldThenReclaimed)
{
  OID heap_oid = OID_INITIALIZER, old_oos_oid = OID_INITIALIZER;
  insert_row_on_new_page (false, heap_oid, old_oos_oid);
  ASSERT_FALSE (HasFatalFailure ());

  RECDES new_oos_rec {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes (test_oos_utils::make_repeated_pattern_string (4096),
	     new_oos_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&new_oos_rec, recdes_free_data_area);
  OID new_oos_oid = OID_INITIALIZER;
  ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, new_oos_rec, new_oos_oid), NO_ERROR);
  RECDES new_heap_rec {};
  ASSERT_EQ (build_heap_recdes_with_oos ({new_oos_oid}, { (INT64) new_oos_rec.length}, new_heap_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&new_heap_rec, recdes_free_data_area);

  const int deferrals_before = deferrals ();
  ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, new_heap_rec), NO_ERROR);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();

  PGBUF_WATCHER header_watcher;
  ASSERT_TRUE (hold_header_and_close_blocks (header_watcher, update_block)) << "the update's log block did not close";
  const bool deferred = wait_for_vacuum ([this, deferrals_before] { return deferrals () > deferrals_before; }, 60);
  const bool old_kept_while_held = !oos_unreadable (old_oos_oid);
  release_header (header_watcher);

  EXPECT_TRUE (deferred) << "vacuum never deferred the block on the held heap header";
  EXPECT_TRUE (old_kept_while_held) << "the dropped chain was reclaimed although the header lookup failed";
  EXPECT_TRUE (wait_for_vacuum ([this, &old_oos_oid] { return oos_unreadable (old_oos_oid); }, 60))
      << "vacuum never reclaimed the UPDATE's dropped chain after the header was released";
  EXPECT_FALSE (oos_unreadable (new_oos_oid)) << "the new version's chain must survive";
}

// A deferred notification must not hold back the rest of its log block. Another heap's deleted row in the same block
// is vacuumed while the header lookup keeps failing; a worker that gave up the block at the notification would leave
// it until the header is free (CBRD-27953, as CBRD-27933 for heap records).
TEST_F (OosRealVacuumHeaderHeld, UpdateNotificationDeferralLeavesRestOfBlockToVacuum)
{
  unrelated_row other;
  OID heap_oid = OID_INITIALIZER, old_oos_oid = OID_INITIALIZER;
  prepare_unrelated_row (other, heap_oid, old_oos_oid);
  ASSERT_FALSE (HasFatalFailure ());

  RECDES plain_rec {};
  ASSERT_EQ (build_plain_heap_recdes (64, plain_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_plain (&plain_rec, recdes_free_data_area);

  /* The update, its notification at commit and the other heap's delete fit in a log block that they start. */
  ASSERT_TRUE (fill_log_past (current_log_blockid ()));
  const VACUUM_LOG_BLOCKID block = current_log_blockid ();
  ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, plain_rec), NO_ERROR);
  ASSERT_EQ (heap_delete_mvcc (other.hfid, class_oid, other.scan_cache, other.oid), NO_ERROR);
  commit_current_tran ();
  ASSERT_EQ (current_log_blockid (), block) << "the update, its notification and the delete must share one log block";

  const int deferrals_before = deferrals ();
  PGBUF_WATCHER header_watcher;
  const bool held = hold_header_and_close_blocks (header_watcher, block);
  const bool deferred = held && wait_for_vacuum ([this, deferrals_before] { return deferrals () > deferrals_before; },
			60);
  const bool other_removed = deferred && wait_for_vacuum ([&other]
  {
    return row_removed (slot_record_type (other.oid));
  }, 20);
  const bool old_kept_while_held = !oos_unreadable (old_oos_oid);
  const int runs = interrupted_runs (block);
  release_header (header_watcher);
  const bool reclaimed = wait_for_vacuum ([this, &old_oos_oid] { return oos_unreadable (old_oos_oid); }, 60);
  printf ("[CBRD-27953] notification and unrelated row: block %lld interrupted %d times, unrelated row removed "
	  "while held %d, dropped chain kept while held %d, reclaimed after release %d\n", (long long) block, runs,
	  (int) other_removed, (int) old_kept_while_held, (int) reclaimed);
  fflush (stdout);
  drop_unrelated_row (other);

  ASSERT_TRUE (held) << "OOS filler failed to close the update's log block";
  EXPECT_TRUE (deferred) << "vacuum never deferred the block on the held heap header";
  EXPECT_TRUE (other_removed) << "vacuum left the rest of the log block while the notification's lookup failed";
  EXPECT_TRUE (old_kept_while_held) << "the dropped chain was reclaimed although the header lookup failed";
  EXPECT_TRUE (reclaimed) << "vacuum never reclaimed the UPDATE's dropped chain after the header was released";
}

// Several notifications of one heap in a log block fail their lookup together: one lookup per run, not one per
// notification. With a real latch timeout each lookup waits for the header.
TEST_F (OosRealVacuumHeaderHeld, FailingHeapNotificationsCostOneLookupPerRun)
{
  OID heap_oids[2], oos_oids[2];
  for (int i = 0; i < 2; i++)
    {
      insert_row_on_new_page (false, heap_oids[i], oos_oids[i]);
      ASSERT_FALSE (HasFatalFailure ());
    }
  ASSERT_TRUE (close_block_with_oos_fillers (current_log_blockid ()));
  ASSERT_TRUE (wait_for_vacuum ([&heap_oids]
  {
    return insert_id_vacuumed (heap_oids[0]) && insert_id_vacuumed (heap_oids[1]);
  }, 60)) << "vacuum did not finish the inserts";

  RECDES plain_rec {};
  ASSERT_EQ (build_plain_heap_recdes (64, plain_rec), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_plain (&plain_rec, recdes_free_data_area);

  /* Two updates and their notifications fit in a log block that they start. The new versions have no OOS value, so
   * vacuum_heap never looks up the heap's OOS file and every lookup in the block is a notification's. */
  ASSERT_TRUE (fill_log_past (current_log_blockid ()));
  const VACUUM_LOG_BLOCKID block = current_log_blockid ();
  for (OID &heap_oid : heap_oids)
    {
      ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, plain_rec), NO_ERROR);
    }
  commit_current_tran ();
  ASSERT_EQ (current_log_blockid (), block) << "both updates and their notifications must share one log block";

  const int deferrals_before = deferrals ();
  PGBUF_WATCHER header_watcher;
  const bool held = hold_header_and_close_blocks (header_watcher, block);
  const bool rerun = held && wait_for_vacuum ([this, block] { return interrupted_runs (block) >= 2; }, 120);
  const bool kept = !oos_unreadable (oos_oids[0]) && !oos_unreadable (oos_oids[1]);
  /* One read of the log for both counts. A run logs its deferral before its interruption, so a run in progress
   * counts one lookup more than the finished runs. */
  const std::string held_log = server_log ();
  const int runs = count_occurrences (held_log, "Processing log block " + std::to_string (block) + " is interrupted");
  const int lookups = count_occurrences (held_log, "CODE = " + std::to_string (ER_VACUUM_OOS_LOOKUP_DEFERRED) + ",")
		      - deferrals_before;
  release_header (header_watcher);
  const bool reclaimed = wait_for_vacuum ([this, &oos_oids]
  {
    return oos_unreadable (oos_oids[0]) && oos_unreadable (oos_oids[1]);
  }, 60);
  printf ("[CBRD-27953] two notifications: block %lld interrupted %d times, %d deferred lookups, dropped chains "
	  "kept while held %d, reclaimed after release %d\n", (long long) block, runs, lookups, (int) kept,
	  (int) reclaimed);
  fflush (stdout);

  ASSERT_TRUE (held) << "OOS filler failed to close the updates' log block";
  ASSERT_TRUE (rerun) << "vacuum did not leave the updates' log block for a later run";
  EXPECT_TRUE (kept) << "a dropped chain was reclaimed although the header lookup failed";
  EXPECT_GE (lookups, runs) << "a run of the log block left the block without a failed lookup";
  EXPECT_LE (lookups, runs + 1) << "a run of the log block looked up the failing heap once per notification";
  EXPECT_TRUE (reclaimed) << "vacuum never reclaimed the dropped chains after the header was released";
}

// ============================================================================
// TC-R15..R17: A' UPDATE notification (ADR-0008, CBRD-27237)
//
// An MVCC UPDATE records the chains it drops as a postpone. Only the committed UPDATE hands them to
// vacuum (RVOOS_NOTIFY_VACUUM under the UPDATE's MVCCID); a transaction or savepoint rollback discards
// the postpone, and the restored version keeps its chain. The undo-image forward-walk that deleted the
// live chain of a rolled-back UPDATE is gone.
//
// A rollback test cannot observe "vacuum never deletes" directly. A witness row is deleted, by a committed
// transaction, in the same log block as the rolled-back UPDATE; vacuum processes a block as one job, so
// once the witness's OOS drained, the job that saw the UPDATE's log records has finished.
// ============================================================================
class OosRealVacuumUpdateNotify : public OosRealVacuum
{
  protected:
    /* Inserts a new OOS payload and updates heap_oid to reference it, without committing. */
    void update_row_uncommitted (OID &heap_oid, const std::string &payload, OID &new_oos_oid_out)
    {
      RECDES new_oos_rec {};
      ASSERT_EQ (test_oos_utils::from_string_into_recdes (payload, new_oos_rec), NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_oos (&new_oos_rec, recdes_free_data_area);

      new_oos_oid_out = OID_INITIALIZER;
      ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, new_oos_rec, new_oos_oid_out), NO_ERROR);

      RECDES new_heap_rec {};
      ASSERT_EQ (build_heap_recdes_with_oos ({new_oos_oid_out}, { (INT64) new_oos_rec.length}, new_heap_rec),
		 NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_heap (&new_heap_rec, recdes_free_data_area);

      ASSERT_EQ (heap_update_mvcc (hfid, class_oid, scan_cache, heap_oid, new_heap_rec), NO_ERROR);
    }

    /* Deletes the witness row in the current log block, commits, closes the block, and waits until vacuum
     * reclaimed the witness's OOS. Returns the block. */
    VACUUM_LOG_BLOCKID delete_witness_and_wait (OID &witness_heap_oid, const OID &witness_oos_oid,
	VACUUM_LOG_BLOCKID expected_block)
    {
      EXPECT_EQ (heap_delete_mvcc (hfid, class_oid, scan_cache, witness_heap_oid), NO_ERROR);
      VACUUM_LOG_BLOCKID block = current_log_blockid ();
      EXPECT_EQ (block, expected_block) << "the witness must share the rolled-back UPDATE's log block";
      commit_current_tran ();
      close_log_block_containing (hfid, class_oid, scan_cache, block);
      EXPECT_TRUE (wait_for_vacuum ([this, &witness_oos_oid] { return oos_unreadable (witness_oos_oid); }, 60))
	  << "vacuum did not process the witness's block";
      return block;
    }

    /* length counts the terminating NUL that from_string_into_recdes stores with the payload. */
    void expect_value (const OID &oos_oid, int length, const char *what)
    {
      RECDES out {};
      ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, out), NO_ERROR) << what;
      EXPECT_EQ (out.length, length) << what;
      recdes_free_data_area (&out);
    }
};

TEST_F (OosRealVacuumUpdateNotify, RolledBackUpdateKeepsTheRestoredVersionsChainThroughVacuum)
{
  const std::string value = test_oos_utils::make_repeated_pattern_string (4096);
  OID heap_oid, old_oos_oid, witness_heap_oid, witness_oos_oid;
  insert_row_with_oos (value, heap_oid, old_oos_oid);
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (3000), witness_heap_oid, witness_oos_oid);

  OID new_oos_oid;
  update_row_uncommitted (heap_oid, test_oos_utils::make_repeated_pattern_string (5000), new_oos_oid);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  ASSERT_EQ (xtran_server_abort (thread_p), TRAN_UNACTIVE_ABORTED);

  delete_witness_and_wait (witness_heap_oid, witness_oos_oid, update_block);

  expect_value (old_oos_oid, (int) value.size () + 1, "the rolled-back UPDATE's restored version must keep its chain");
  expect_oos_gone (new_oos_oid, "the rolled-back UPDATE's own new chain (undone by the rollback)");
}

TEST_F (OosRealVacuumUpdateNotify, SavepointRolledBackUpdateKeepsItsChainAndLaterCommitDoesNotReclaimIt)
{
  const std::string value = test_oos_utils::make_repeated_pattern_string (4096);
  OID heap_oid, old_oos_oid, witness_heap_oid, witness_oos_oid;
  insert_row_with_oos (value, heap_oid, old_oos_oid);
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (3000), witness_heap_oid, witness_oos_oid);

  LOG_LSA savept_lsa;
  ASSERT_EQ (xtran_server_savepoint (thread_p, "oos_sp", &savept_lsa), NO_ERROR);
  OID new_oos_oid;
  update_row_uncommitted (heap_oid, test_oos_utils::make_repeated_pattern_string (5000), new_oos_oid);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  ASSERT_EQ (xtran_server_partial_abort (thread_p, "oos_sp", &savept_lsa), TRAN_UNACTIVE_ABORTED);

  /* The same transaction commits with the witness delete: the discarded postpone must not run. */
  delete_witness_and_wait (witness_heap_oid, witness_oos_oid, update_block);

  expect_value (old_oos_oid, (int) value.size () + 1, "a savepoint rollback must discard the UPDATE's drop postpone");
  expect_oos_gone (new_oos_oid, "the rolled-back UPDATE's own new chain (undone by the rollback)");
}

/* The pages the notification consumer empties go to the fast path (Work818 F3): a chain that filled a page
 * by itself returns that page without waiting for the file to grow. */
TEST_F (OosRealVacuumUpdateNotify, CommittedUpdateReturnsThePageItsDroppedChainEmptied)
{
  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
  OID heap_oid, old_oos_oid;
  insert_row_with_oos (test_oos_utils::make_repeated_pattern_string (max_chunk), heap_oid, old_oos_oid);

  OID new_oos_oid;
  update_row_uncommitted (heap_oid, test_oos_utils::make_repeated_pattern_string (max_chunk), new_oos_oid);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();

  OOS_STATS_INFO before;
  ASSERT_EQ (oos_get_stats_by_vfid (thread_p, oos_vfid, &before), NO_ERROR);

  close_log_block_containing (hfid, class_oid, scan_cache, update_block);
  ASSERT_TRUE (wait_for_vacuum ([this, &old_oos_oid] { return oos_unreadable (old_oos_oid); }, 60))
      << "vacuum did not reclaim the committed UPDATE's dropped chain";

  OOS_STATS_INFO after;
  ASSERT_TRUE (wait_for_vacuum ([this, &after, &before]
  {
    return oos_get_stats_by_vfid (thread_p, oos_vfid, &after) == NO_ERROR && after.num_user_pages < before.num_user_pages;
  }, 10)) << "the emptied page was not returned by the fast path: pages before " << before.num_user_pages
	  << ", after " << after.num_user_pages;
  expect_value (new_oos_oid, max_chunk + 1, "the new version's chain must survive");
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
