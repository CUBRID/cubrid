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
 * Unlike test_oos_vacuum_server.cpp (which calls the vacuum leaf function
 * vacuum_heap_oos_delete_within_sysop on crafted RECDES), these tests drive the
 * whole chain:
 *
 *   MVCC heap DML (heap_insert_logical / heap_delete_logical / heap_update_logical)
 *     -> MVCC op log records carrying vacuum info
 *     -> log block completion (unittestdb is created with vacuum_log_block_pages=4
 *        to keep the filler small, but the tests loop until the block actually
 *        closes, so they remain correct for any block size)
 *     -> vacuum_wakeup_master_daemon() - the same entry csql ';vacuum' and the
 *        SQL VACUUM statement reach via svacuum in SERVER_MODE
 *     -> vacuum master -> worker -> vacuum_heap -> vacuum_heap_oos_delete_within_sysop -> oos_delete
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
#include <functional>
#include <thread>

#include "mvcc.h"
#include "object_representation.h"
#include "vacuum.h"
#include "xserver_interface.h"

#include "test_oos_server_common.hpp"

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
static const int OOS_INLINE_SZ = 16;	/* OR_OID_SIZE + OR_BIGINT_SIZE */
static const int MVCC_HEADER_SPARE = 2 * OR_MVCCID_SIZE;

static int
build_heap_recdes_with_oos (const std::vector<OID> &oos_oids,
			    const std::vector<INT64> &oos_lengths,
			    RECDES &rec_out)
{
  const int n_oos = (int) oos_oids.size ();
  assert (n_oos > 0);
  assert ((int) oos_lengths.size () == n_oos);

  const int vot_bytes = n_oos * VOT_ENTRY_SZ;
  const int data_bytes = n_oos * OOS_INLINE_SZ;
  const int total = HEAP_HDR_SIZE + vot_bytes + data_bytes;

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
  int rep_and_flags = (OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_4BYTE;
  OR_PUT_INT (base + OR_REP_OFFSET, rep_and_flags);

  /* 2. CHN = 0 (already zeroed) */

  /* 3. VOT entries — each stores (offset_from_vot_start | flags) */
  char *vot = base + HEAP_HDR_SIZE;
  for (int i = 0; i < n_oos; i++)
    {
      int offset = vot_bytes + i * OOS_INLINE_SZ;
      int flags = OR_VAR_BIT_OOS;
      if (i == n_oos - 1)
	{
	  flags |= OR_VAR_BIT_LAST_ELEMENT;
	}
      OR_PUT_INT (vot + i * VOT_ENTRY_SZ, offset | flags);
    }

  /* 4. OOS inline data: OID (8b) + length (8b) per column */
  char *oos_data = vot + vot_bytes;
  for (int i = 0; i < n_oos; i++)
    {
      char *slot = oos_data + i * OOS_INLINE_SZ;
      OR_PUT_OID (slot, &oos_oids[i]);
      INT64 len = oos_lengths[i];
      OR_PUT_BIGINT (slot + OR_OID_SIZE, &len);
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
  return vacuum_get_log_blockid (log_Gl.prior_info.prior_lsa.pageid);
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

      commit_current_tran ();
    }
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

      /* Pre-shaped scancache so the DML paths never call heap_get_class_info:
       * the borrowed class OID maps to db_user's REAL heap in the hfid cache,
       * not to this test heap, so the class-info path must be avoided. */
      heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, &hfid);
      COPY_OID (&scan_cache.node.class_oid, &class_oid);
      scan_cache.file_type = FILE_HEAP;
      scan_cache.mvcc_disabled_class = false;
      scan_cache.page_latch = PGBUF_LATCH_WRITE;
      /* Never keep pages fixed between operations — the vacuum worker needs
       * write latches on these pages while the test polls. */
      scan_cache.cache_last_fix_page = false;
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
     * becomes update-superseded (reclaimed via the forward-walk path). Returns
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
// Exercises the vacuum forward-walk reclaim of update-superseded OOS
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
// TC-R6: A live snapshot gates the UPDATE forward-walk reclaim too
//
// TC-R4 only gates the DELETE/REMOVE path. The forward-walk path
// (RVHF_UPDATE_NOTIFY_VACUUM, exercised by TC-R3) has its own visibility check;
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

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
