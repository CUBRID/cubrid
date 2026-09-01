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

// Growth-gate incremental sweep (CBRD-26786): when the OOS file is about to allocate a new page
// while deletes are pending, it reclaims one empty page instead. Every test first calls
// simulate_hint_loss: with bestspace hints alive an emptied page is reused directly and the
// growth path is never entered.

#include "gtest/gtest.h"
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "file_manager.h"
#include "page_buffer.h"
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

  // num_other_high_best must also be zeroed, or the tier-3 sync scan refills the hints and the
  // insert never reaches the growth path.
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

  // Leave no committed orphan file behind: a later binary's file-tracker dump would try to
  // resolve this file's synthetic owner class OID against a non-heap page and assert.
  void remove_file_and_commit (const VFID &oos_vfid)
  {
    ASSERT_EQ (oos_remove_file (thread_p, oos_vfid), NO_ERROR);
    ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  }

} // namespace

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

TEST (OosGrowthSweepTest, TransientWriteLatchMissKeepsReclaimDebt)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID emptied_oid = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, emptied_oid), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, emptied_oid), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  ASSERT_EQ (count_user_pages (oos_vfid), 2);

  simulate_hint_loss (oos_vfid);
  oos_test_fail_next_reclaim_write_fix ();

  OID first_growth_oid = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, first_growth_oid), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3)
      << "the forced phase-2 latch miss should make this growth allocate";

  simulate_hint_loss (oos_vfid);

  OID retry_oid = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, retry_oid), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3)
      << "the transient phase-2 latch miss lost reclaim debt and stranded the empty page";
  assert_value_intact (first_growth_oid);
  assert_value_intact (retry_oid);

  remove_file_and_commit (oos_vfid);
}

TEST (OosGrowthSweepTest, ConcurrentGrowerWaitsForActiveSweep)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  oos_test_reclaim_force_sweep_in_progress (oos_vfid);
  auto concurrent_sweep = std::async (std::launch::async, [&] ()
  {
    return oos_test_reclaim_sweep_step (thread_p, oos_vfid);
  });

  const auto waiter_deadline = std::chrono::steady_clock::now () + std::chrono::seconds (1);
  while (oos_test_reclaim_waiter_count () == 0
	 && concurrent_sweep.wait_for (std::chrono::milliseconds (0)) == std::future_status::timeout
	 && std::chrono::steady_clock::now () < waiter_deadline)
    {
      std::this_thread::yield ();
    }

  EXPECT_EQ (oos_test_reclaim_waiter_count (), 1);
  EXPECT_EQ (concurrent_sweep.wait_for (std::chrono::milliseconds (0)), std::future_status::timeout)
      << "a concurrent grower bypassed the active reclaim sweep";

  oos_test_reclaim_release_sweep (oos_vfid);
  ASSERT_EQ (concurrent_sweep.get (), NO_ERROR);

  remove_file_and_commit (oos_vfid);
}

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

  // The wasted lap reclaims nothing, so growth proceeds and the count rises by one.
  OID oid2 = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid2), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), 3);
  assert_value_intact (oid1);
  assert_value_intact (oid2);

  remove_file_and_commit (oos_vfid);
}

// An INSERT rollback also empties an allocated page; repeated rollbacks must keep reusing it.
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

  // An eager full sweep would deallocate all three at once and the count would drop to 2.
  for (int i = 0; i < 3; i++)
    {
      OID oid = OID_INITIALIZER;
      ASSERT_EQ (insert_page_filling_record (oos_vfid, oid), NO_ERROR);
      ASSERT_EQ (count_user_pages (oos_vfid), pages_full)
	  << "insert " << i << " changed the page count — sweep was not incremental";
    }

  OID oid_growth = OID_INITIALIZER;
  ASSERT_EQ (insert_page_filling_record (oos_vfid, oid_growth), NO_ERROR);
  ASSERT_EQ (count_user_pages (oos_vfid), pages_full + 1);

  remove_file_and_commit (oos_vfid);
}

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
  PAGE_PTR hdr_check = NULL;
  ASSERT_EQ (pgbuf_fix_if_not_deallocated (thread_p, &hdr_vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
	     &hdr_check), NO_ERROR);
  ASSERT_NE (hdr_check, nullptr);
  pgbuf_unfix (thread_p, hdr_check);
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
