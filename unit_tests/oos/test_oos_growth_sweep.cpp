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

// Growth-gate incremental sweep (CBRD-26786): a delete burst must not make the OOS file grow on
// re-insert — when the file is about to allocate a new page while deletes are pending, the sweep
// walks the sector bitmap from its cursor, reclaims the first safely-empty page, and the
// allocation reuses it. These tests observe only external behavior: the file's user page count
// (oos_get_stats_by_vfid) and value integrity (oos_read).
//
// Every scenario first simulates the HINT-LOSS regime (bestspace hash cache and header best[]
// hints cleared): with hints alive, an emptied page is simply reused through bestspace and no
// growth happens at all. The leak this feature closes appears exactly when hints are gone —
// the cache is capped at 1000 entries and best[] at 10, so a large delete burst (the reviewer's
// 14,000-row reproduction) loses track of almost every emptied page.

#include "gtest/gtest.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "file_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "xserver_interface.h"
#include "oos_file.hpp"
#include "test_oos_common.hpp"
#include "oos_log.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;

// bridge functions to access static functions in oos_file.cpp
int bridge_oos_get_max_chunk_size_within_page ();
int bridge_oos_stats_del_bestspace_by_vfid (THREAD_ENTRY *thread_p, const VFID *vfid);
OOS_HDR_STATS *bridge_oos_get_header_stats_ptr (THREAD_ENTRY *thread_p, PAGE_PTR page_header);

namespace
{

  int count_user_pages (const VFID &oos_vfid)
  {
    OOS_STATS_INFO info;
    int err = oos_get_stats_by_vfid (thread_p, oos_vfid, &info);
    EXPECT_EQ (err, NO_ERROR);
    return info.num_user_pages;
  }

  // Simulate the hint-loss regime: evict the file's hash-cache entries and clear every header
  // hint (best[], the second-best ring, and num_other_high_best so the tier-3 sync scan does not
  // fire either). After this, an insert that fits no live hinted page must take the growth
  // fallback — the seam under test.
  void simulate_hint_loss (const VFID &oos_vfid)
  {
    (void) bridge_oos_stats_del_bestspace_by_vfid (thread_p, &oos_vfid);

    VPID hdr_vpid = VPID_INITIALIZER;
    ASSERT_EQ (file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid), NO_ERROR);

    PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
    ASSERT_NE (hdr_page, nullptr);
    test_oos_utils::auto_unfixed_page_ptr hdr_unfixer (hdr_page, test_oos_utils::page_auto_unfix {thread_p});

    OOS_HDR_STATS *oos_hdr = bridge_oos_get_header_stats_ptr (thread_p, hdr_page);
    ASSERT_NE (oos_hdr, nullptr);

    for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
      {
	VPID_SET_NULL (&oos_hdr->estimates.best[i].vpid);
	oos_hdr->estimates.best[i].freespace = 0;
	VPID_SET_NULL (&oos_hdr->estimates.second_best[i]);
      }
    oos_hdr->estimates.num_second_best = 0;
    oos_hdr->estimates.head_second_best = 0;
    oos_hdr->estimates.tail_second_best = 0;
    oos_hdr->estimates.num_other_high_best = 0;
  }

  // A single-chunk record that nearly fills a page, so every insert without a reusable empty
  // page needs a page of its own (same sizing as the bestspace tests).
  std::string page_filling_pattern ()
  {
    const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
    return test_oos_utils::make_repeated_pattern_string (max_chunk - 50);
  }

  int insert_page_filling_record (const VFID &oos_vfid, OID &oid)
  {
    std::string data = page_filling_pattern ();
    RECDES rec{};
    int err = test_oos_utils::from_string_into_recdes (data, rec);
    if (err != NO_ERROR)
      {
	return err;
      }
    test_oos_utils::auto_freed_recdes_ptr defer_free (&rec, recdes_free_data_area);
    return test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
  }

  void assert_value_intact (const OID &oid)
  {
    RECDES rec_out{};
    ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out), NO_ERROR);
    ASSERT_STREQ (rec_out.data, page_filling_pattern ().c_str ());
    recdes_free_data_area (&rec_out);
  }

  void assert_page_allocated (const VPID &vpid, bool expect_allocated)
  {
    PAGE_PTR page_ptr = NULL;
    ASSERT_EQ (pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
	       &page_ptr), NO_ERROR);
    if (expect_allocated)
      {
	ASSERT_NE (page_ptr, nullptr);
	pgbuf_unfix (thread_p, page_ptr);
      }
    else
      {
	ASSERT_EQ (page_ptr, nullptr);
      }
  }

  // Leave no committed orphan file behind: a later binary's file-tracker dump would try to
  // resolve this file's synthetic owner class OID against a non-heap page and assert.
  void remove_file_and_commit (const VFID &oos_vfid)
  {
    ASSERT_EQ (oos_remove_file (thread_p, oos_vfid), NO_ERROR);
    ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  }

} // namespace

// ===========================================================================
// TEST: SweepReclaimsAfterCommittedDeleteBurst (test plan case 1)
//
// Delete burst + commit + hint loss, then re-insert: the growth gate reclaims
// an emptied page and reuses it in place, so the file's page count does not
// move and the new value reads back intact.
// ===========================================================================
TEST (OosGrowthSweepTest, SweepReclaimsAfterCommittedDeleteBurst)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID oid1 = OID_INITIALIZER, oid2 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid2), NO_ERROR);
  const int pages_full = count_user_pages (oos_vfid);
  ASSERT_EQ (pages_full, 3);	// header + one page per record

  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (count_user_pages (oos_vfid), pages_full);	// deletes alone deallocate nothing

  simulate_hint_loss (oos_vfid);

  OID oid3 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid3), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), pages_full)
      << "growth gate did not reuse an emptied page — the file grew";
  assert_value_intact (oid3);

  remove_file_and_commit (oos_vfid);
}

// ===========================================================================
// TEST: SweepLeavesAbortRestoredChunksAlone (test plan case 2)
//
// An aborted delete restores its chunks; the counter's false positive only
// costs one wasted lap: the next growth sweeps, finds the restored page
// non-empty, settles the counter, and grows normally. The restored value
// stays intact.
// ===========================================================================
TEST (OosGrowthSweepTest, SweepLeavesAbortRestoredChunksAlone)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID oid1 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (count_user_pages (oos_vfid), 2);

  // Delete, then abort: undo restores the chunks; the pending-delete count is now stale.
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (xtran_server_abort (thread_p), TRAN_UNACTIVE_ABORTED);

  bool exists = false;
  ASSERT_EQ (oos_chunk_exists (thread_p, oid1, &exists), NO_ERROR);
  ASSERT_TRUE (exists);

  simulate_hint_loss (oos_vfid);

  // The wasted lap finds only the restored (non-empty) page, reclaims nothing, and growth
  // proceeds: page count rises by one and the restored value is untouched.
  OID oid2 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);
  assert_value_intact (oid1);
  assert_value_intact (oid2);

  remove_file_and_commit (oos_vfid);
}

// An INSERT rollback can empty an allocated page. Repeated rollbacks must keep reusing that page
// after bestspace hints are lost instead of growing the file once per transaction.
TEST (OosGrowthSweepTest, InsertRollbackRearmsGrowthSweep)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID live_oid = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, live_oid), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (count_user_pages (oos_vfid), 2);

  OID aborted_oid = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, aborted_oid), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);
  ASSERT_EQ (xtran_server_abort (thread_p), TRAN_UNACTIVE_ABORTED);

  for (int i = 0; i < 3; i++)
    {
      simulate_hint_loss (oos_vfid);

      OID retry_oid = OID_INITIALIZER;
      ASSERT_EQ (insert_page_filling_record (oos_vfid, retry_oid), NO_ERROR);
      ASSERT_EQ (count_user_pages (oos_vfid), 3)
	  << "rollback cycle " << i << " stranded an empty page";
      ASSERT_EQ (xtran_server_abort (thread_p), TRAN_UNACTIVE_ABORTED);
    }

  simulate_hint_loss (oos_vfid);
  OID committed_oid = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, committed_oid), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  assert_value_intact (live_oid);
  assert_value_intact (committed_oid);

  remove_file_and_commit (oos_vfid);
}

// ===========================================================================
// TEST: SweepIsIncrementalWithCursorContinuation (test plan case 4)
//
// Three emptied pages, three growth events: each sweep stops at its FIRST
// reclaimed page (the net page count never moves — a full sweep would drop it
// by two on the first insert) and the next sweep continues from the cursor.
// The fourth growth completes a clean lap and the file finally grows.
// ===========================================================================
TEST (OosGrowthSweepTest, SweepIsIncrementalWithCursorContinuation)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  std::vector<OID> oids (3, OID_INITIALIZER);
  for (OID &oid : oids)
    {
      ASSERT_EQ (insert_page_filling_record (oos_vfid, oid), NO_ERROR);
    }
  const int pages_full = count_user_pages (oos_vfid);
  ASSERT_EQ (pages_full, 4);

  for (const OID &oid : oids)
    {
      ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid), NO_ERROR);
    }
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  simulate_hint_loss (oos_vfid);

  // Each growth event reclaims exactly one page and reuses it: net page count is stable.
  // (An eager full sweep would deallocate all three at once — the count would drop to 2.)
  for (int i = 0; i < 3; i++)
    {
      OID oid = OID_INITIALIZER;
      ASSERT_EQ (insert_page_filling_record (oos_vfid, oid), NO_ERROR);
      ASSERT_EQ (count_user_pages (oos_vfid), pages_full)
	  << "insert " << i << " changed the page count — sweep was not incremental";
    }

  // No empty page is left: the next growth's lap comes up dry and the file grows.
  OID oid_growth = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid_growth), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), pages_full + 1);

  remove_file_and_commit (oos_vfid);
}

// ===========================================================================
// TEST: SweepCounterResetsOnCleanLapAndRearms (test plan case 5)
//
// A lap that reclaims nothing resets the counter: later growth proceeds
// without touching any page. New deletes re-arm the gate — the reset must not
// wedge the mechanism.
// ===========================================================================
TEST (OosGrowthSweepTest, SweepCounterResetsOnCleanLapAndRearms)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID oid1 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  simulate_hint_loss (oos_vfid);

  // Growth 1 reclaims the emptied page (count stays 2), leaving the stale counter to be
  // settled by the clean lap of growth 2, which then grows the file (count 3).
  OID oid2 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 2);

  OID oid3 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid3), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);

  // Re-arm: a fresh committed delete makes the next growth reclaim again instead of growing.
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  simulate_hint_loss (oos_vfid);

  OID oid4 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid4), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3)
      << "counter did not re-arm after the clean-lap reset";
  assert_value_intact (oid3);
  assert_value_intact (oid4);

  remove_file_and_commit (oos_vfid);
}

// ===========================================================================
// TEST: SweepDefersUncommittedDeletesButAllowsGrowth (test plan case 6)
//
// Pages emptied by a still-uncommitted delete are LSA-gated: the sweep defers
// them and the file GROWS (an insert never waits for the writer to finish).
// After the commit, the next growth reclaims a deferred page.
// ===========================================================================
TEST (OosGrowthSweepTest, SweepDefersUncommittedDeletesButAllowsGrowth)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID oid1 = OID_INITIALIZER, oid2 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);

  // Empty both pages but stay uncommitted: this transaction is a live undo source.
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid2), NO_ERROR);
  simulate_hint_loss (oos_vfid);

  // Deferred-only lap: nothing is reclaimed, growth is allowed — the count rises.
  OID oid3 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid3), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 4)
      << "an uncommitted deleter's page was reclaimed, or growth was blocked";

  // The commit ends the undo source; the kept (deferred) count arms the next growth to reclaim.
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  simulate_hint_loss (oos_vfid);

  OID oid4 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid4), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 4)
      << "deferred page was not reclaimed after its deleter committed";
  assert_value_intact (oid3);
  assert_value_intact (oid4);

  remove_file_and_commit (oos_vfid);
}

// ===========================================================================
// TEST: BootRuleSweepsUnconditionallyAfterRestart (test plan case 7)
//
// A restart loses the counter and cursor. The boot rule makes the first
// growth of an unknown file sweep regardless, and keeps sweeping across
// growths until a lap completes — so every empty page of the previous boot is
// recovered, not just the first.
// ===========================================================================
TEST (OosGrowthSweepTest, BootRuleSweepsUnconditionallyAfterRestart)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  std::vector<OID> oids (2, OID_INITIALIZER);
  for (OID &oid : oids)
    {
      ASSERT_EQ (insert_page_filling_record (oos_vfid, oid), NO_ERROR);
    }
  for (const OID &oid : oids)
    {
      ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid), NO_ERROR);
    }
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);

  // Simulate a restart: all in-memory reclaim bookkeeping is gone; the truth stays on disk.
  oos_test_reclaim_reset_side_map ();
  simulate_hint_loss (oos_vfid);

  // Both previous-boot pages are recovered across the next growths, then the file grows.
  OID oid_a = OID_INITIALIZER, oid_b = OID_INITIALIZER, oid_c = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid_a), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3)
      << "boot rule did not sweep on the first growth after a (simulated) restart";
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid_b), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3)
      << "the second previous-boot empty page was not recovered";
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid_c), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 4);

  remove_file_and_commit (oos_vfid);
}

// ===========================================================================
// TEST: SweepSkipsRefilledPageAndNeverReclaimsHeader (test plan case 8)
//
// A page emptied and then refilled (through live bestspace hints) is skipped
// by the sweep with its record intact; and with every data page empty, the
// sticky first page (OOS_HDR_STATS) is never a reclaim candidate.
// ===========================================================================
TEST (OosGrowthSweepTest, SweepSkipsRefilledPageAndNeverReclaimsHeader)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  VPID hdr_vpid = VPID_INITIALIZER;
  ASSERT_EQ (file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid), NO_ERROR);

  OID oid1 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid1), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  // Hints are still alive: this insert refills the emptied page through bestspace, leaving the
  // pending-delete count stale.
  OID oid2 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (count_user_pages (oos_vfid), 2);

  // The stale count makes this growth sweep; the refilled page must be skipped, not reclaimed.
  simulate_hint_loss (oos_vfid);
  OID oid3 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid3), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);
  assert_value_intact (oid2);

  // Empty EVERY data page, then grow: the sweep may reclaim only data pages — the sticky first
  // page survives even though the lap visits it.
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid3), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  simulate_hint_loss (oos_vfid);

  OID oid4 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid4), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);
  assert_page_allocated (hdr_vpid, true);
  assert_value_intact (oid4);

  remove_file_and_commit (oos_vfid);
}

int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  ::testing::GTEST_FLAG (break_on_failure) = true;

  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS();
}
