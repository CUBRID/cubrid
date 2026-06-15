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
  int rep_and_flags = (OR_MVCC_FLAG_HAS_OOS << OR_MVCC_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_4BYTE;
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
      scan_cache.page_latch = X_LOCK;
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

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
