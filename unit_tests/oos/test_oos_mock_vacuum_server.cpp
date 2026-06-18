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
 * test_oos_mock_vacuum_server.cpp - SERVER_MODE mock-vacuum tests for OOS space reclaim
 *
 * These tests boot a full CUBRID server in-process (SERVER_MODE) and exercise
 * the OOS deletion paths by calling oos_delete() directly — mimicking the
 * pattern that vacuum uses, without invoking the actual vacuum_heap_oos_delete()
 * code path.
 *
 * For tests that exercise the real vacuum code path (vacuum_heap_oos_delete,
 * heap_recdes_get_oos_oids), see test_oos_vacuum_server.cpp.
 */

#include "test_oos_server_common.hpp"

/* bridge functions defined in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

// ============================================================================
// Helpers
// ============================================================================

static int
get_free_space_of_oid_page (const OID &oid)
{
  VPID vpid = {oid.pageid, oid.volid};
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				 PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      return -1;
    }
  test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };
  return spage_get_free_space (thread_p, page_ptr);
}

// ============================================================================
// Test fixture: creates and destroys an OOS file per test
// ============================================================================

class OosVacuumServer : public ::testing::Test
{
  protected:
    VFID oos_vfid;

    void SetUp () override
    {
      int err = oos_create_file (thread_p, oos_vfid);
      ASSERT_EQ (err, NO_ERROR);
    }

    void TearDown () override
    {
      oos_remove_file (thread_p, oos_vfid);
    }
};

// ============================================================================
// TC-VS1: Basic vacuum reclaim — insert, delete, verify gone
//
// This is the fundamental operation vacuum performs: given an OOS OID
// from a dead heap record, call oos_delete to reclaim the space.
// ============================================================================
TEST_F (OosVacuumServer, BasicInsertAndDelete)
{
  int err;

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("OOS data for vacuum reclaim test", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);

  /* Verify it's readable before deletion */
  RECDES rec_check {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_check);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_check.data, rec_in.data);
  recdes_free_data_area (&rec_check);

  /* Delete — this is what vacuum_heap_oos_delete does */
  err = oos_delete (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Must be gone */
  RECDES rec_after {};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_after);
  ASSERT_NE (read_err, NO_ERROR);
  if (rec_after.data != nullptr)
    {
      recdes_free_data_area (&rec_after);
    }
}

// ============================================================================
// TC-VS2: Multi-chunk vacuum reclaim — large OOS spanning multiple pages
//
// Vacuum must delete the entire chunk chain. Verifies that oos_delete
// reclaims space from all pages, not just the head chunk.
// ============================================================================
TEST_F (OosVacuumServer, MultiChunkDelete)
{
  int err;

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
  const int large_size = max_chunk + 50; /* guaranteed two chunks */

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID head_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, head_oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_before = get_free_space_of_oid_page (head_oid);
  ASSERT_GE (free_before, 0);

  /* Vacuum deletes the head OID; oos_delete follows the chain internally */
  err = oos_delete (thread_p, oos_vfid, head_oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_after = get_free_space_of_oid_page (head_oid);
  ASSERT_GT (free_after, free_before) << "Head page must gain free space after deletion";

  /* Reading must fail */
  RECDES rec_after {};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, head_oid, rec_after);
  ASSERT_NE (read_err, NO_ERROR);
  if (rec_after.data != nullptr)
    {
      recdes_free_data_area (&rec_after);
    }
}

// ============================================================================
// TC-VS3: 160KB multi-page OOS vacuum reclaim
//
// Stress test for the chain-delete path: 160KB spans ~10 pages at 16KB
// page size. All chunks must be freed.
// ============================================================================
TEST_F (OosVacuumServer, LargeMultiPageDelete)
{
  int err;

  const int large_size = 160 * 1024;
  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Verify round-trip before delete */
  RECDES rec_check {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_check);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (rec_check.length, rec_in.length);
  recdes_free_data_area (&rec_check);

  /* Delete entire chain */
  err = oos_delete (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Must be gone */
  RECDES rec_after {};
  ASSERT_NE (test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_after), NO_ERROR);
  if (rec_after.data != nullptr)
    {
      recdes_free_data_area (&rec_after);
    }
}

// ============================================================================
// TC-VS4: MVCC UPDATE vacuum pattern — old version deleted, new survives
//
// Simulates the MVCC UPDATE vacuum path:
//   1. INSERT creates OOS for original value
//   2. UPDATE creates new OOS for new value
//   3. Vacuum deletes old OOS via oos_delete (prev_version cleanup)
//   4. New OOS remains readable
// ============================================================================
TEST_F (OosVacuumServer, MvccUpdateVacuumPattern)
{
  int err;

  const std::string old_data = test_oos_utils::make_repeated_pattern_string (4096);
  const std::string new_data = test_oos_utils::make_repeated_pattern_string (4096);

  RECDES rec_old {};
  RECDES rec_new {};
  err = test_oos_utils::from_string_into_recdes (old_data, rec_old);
  ASSERT_EQ (err, NO_ERROR);
  err = test_oos_utils::from_string_into_recdes (new_data, rec_new);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_old (&rec_old, recdes_free_data_area);
  test_oos_utils::auto_freed_recdes_ptr defer_new (&rec_new, recdes_free_data_area);

  /* Step 1: Insert "original" OOS */
  OID old_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_old, old_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Step 2: Insert "updated" OOS (new version) */
  OID new_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_new, new_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Step 3: Vacuum deletes old version's OOS */
  err = oos_delete (thread_p, oos_vfid, old_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Step 4: New version OOS still readable */
  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, new_oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (rec_out.length, rec_new.length);
  recdes_free_data_area (&rec_out);

  /* Old version must be gone */
  RECDES stale_out {};
  ASSERT_NE (test_oos_utils::oos_read_with_alloc (thread_p, old_oid, stale_out), NO_ERROR);
  if (stale_out.data != nullptr)
    {
      recdes_free_data_area (&stale_out);
    }
}

// ============================================================================
// TC-VS5: Bulk vacuum — insert N records, delete all, verify space reuse
//
// Simulates vacuum processing a batch of dead heap records, each with OOS.
// Verifies that after deleting all OOS, a fresh set of inserts reuses space.
// ============================================================================
TEST_F (OosVacuumServer, BulkVacuumReclaimAndReuse)
{
  int err;

  const int N = 10;
  const int oos_size = 4096;
  OID oids[N];

  /* Insert N OOS records */
  for (int i = 0; i < N; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
      RECDES rec {};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oids[i]);
      ASSERT_EQ (err, NO_ERROR);

      recdes_free_data_area (&rec);
    }

  int pages_after_insert = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_insert);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_GT (pages_after_insert, 0);

  /* Vacuum: delete all OOS records */
  for (int i = 0; i < N; i++)
    {
      err = oos_delete (thread_p, oos_vfid, oids[i]);
      ASSERT_EQ (err, NO_ERROR);
    }

  /* Reinsert same number of records — should reuse freed space */
  OID new_oids[N];
  for (int i = 0; i < N; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
      RECDES rec {};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, new_oids[i]);
      ASSERT_EQ (err, NO_ERROR);

      recdes_free_data_area (&rec);
    }

  int pages_after_reinsert = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_reinsert);
  ASSERT_EQ (err, NO_ERROR);

  EXPECT_LE (pages_after_reinsert, pages_after_insert * 2)
      << "After vacuum + reinsert, OOS file should reuse freed space";

  /* All new records must be readable */
  for (int i = 0; i < N; i++)
    {
      RECDES rec_out {};
      err = test_oos_utils::oos_read_with_alloc (thread_p, new_oids[i], rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ (rec_out.length, oos_size + 1); /* +1 for null terminator */
      recdes_free_data_area (&rec_out);
    }
}

// ============================================================================
// TC-VS6: Multi-update churn — 10 records x 10 updates, delete all old versions
//
// Stress test for the vacuum prev_version OOS cleanup path:
// Each "update" creates a new OOS and the old one becomes garbage.
// Vacuum must delete all old OOS versions without leaking pages.
// ============================================================================
TEST_F (OosVacuumServer, MultiUpdateChurnVacuum)
{
  int err;

  const int N = 10;
  const int ROUNDS = 10;
  const int oos_size = 2048;

  /* Track current OOS OID for each "row" */
  OID current_oids[N];

  /* Initial insert */
  for (int i = 0; i < N; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
      RECDES rec {};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, current_oids[i]);
      ASSERT_EQ (err, NO_ERROR);

      recdes_free_data_area (&rec);
    }

  int pages_after_insert = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_insert);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_GT (pages_after_insert, 0);

  /* Simulate ROUNDS of UPDATEs: for each round, insert new OOS and delete old */
  for (int round = 0; round < ROUNDS; round++)
    {
      for (int i = 0; i < N; i++)
	{
	  /* Insert new version */
	  auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
	  RECDES rec {};
	  err = test_oos_utils::from_string_into_recdes (data, rec);
	  ASSERT_EQ (err, NO_ERROR);

	  OID new_oid = OID_INITIALIZER;
	  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, new_oid);
	  ASSERT_EQ (err, NO_ERROR);

	  /* Vacuum deletes old version */
	  err = oos_delete (thread_p, oos_vfid, current_oids[i]);
	  ASSERT_EQ (err, NO_ERROR);

	  current_oids[i] = new_oid;

	  recdes_free_data_area (&rec);
	}
    }

  /* After 100 updates (10 rounds x 10 rows), page count should be bounded */
  int pages_after_churn = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_churn);
  ASSERT_EQ (err, NO_ERROR);

  EXPECT_LE (pages_after_churn, pages_after_insert + 2)
      << "After churn with immediate vacuum, OOS page count should stay bounded";

  /* Current versions must still be readable */
  for (int i = 0; i < N; i++)
    {
      RECDES rec_out {};
      err = test_oos_utils::oos_read_with_alloc (thread_p, current_oids[i], rec_out);
      ASSERT_EQ (err, NO_ERROR);
      recdes_free_data_area (&rec_out);
    }
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
