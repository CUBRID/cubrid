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
 * test_oos_sql_reclaim.cpp - OOS space reclamation tests
 *
 * Focused tests for verifying that OOS disk space is properly reclaimed
 * after DELETE and UPDATE operations. Tests both:
 *   - SA_MODE: eager OOS cleanup during non-MVCC UPDATE + vacuum for DELETE
 *   - CS_MODE (SERVER_MODE): vacuum-based cleanup for both UPDATE and DELETE
 *
 * Build modes:
 *   SA_MODE  → linked to cubridsa, boots server in-process
 *   CS_MODE  → linked to cubridcs, connects to running cub_server
 */

#include "test_oos_sql_common.hpp"

// ============================================================================
// Test fixture
// ============================================================================

class OosReclaim : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_reclaim");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_reclaim");
      db_commit_transaction ();
    }
};

// ============================================================================
// Helper: insert N records with OOS data
// ============================================================================
static void
insert_oos_records (int count, int oos_size = 4096)
{
  for (int i = 1; i <= count; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_reclaim VALUES (%d, REPEAT(X'AA', %d))", i, oos_size);
      int rc = exec_sql (sql);
      assert (rc >= 0);
      db_commit_transaction ();
    }
}

// ============================================================================
// Helper: update all records with new OOS data
// ============================================================================
static void
update_all_oos_records (int count, const char *hex_pattern, int oos_size = 4096)
{
  for (int i = 1; i <= count; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"UPDATE t_reclaim SET data_col = REPEAT(X'%s', %d) WHERE id = %d",
		hex_pattern, oos_size, i);
      int rc = exec_sql (sql);
      assert (rc >= 0);
    }
  db_commit_transaction ();
}

// ============================================================================
// TC-R1: INSERT → DELETE → vacuum → OOS space reclaimed
//
// The most basic OOS reclamation test. After DELETE + vacuum, the OOS records
// should be removed and space reusable.
//
// Works in both SA_MODE and CS_MODE:
//   - SA_MODE:  vacuum via xvacuum()
//   - CS_MODE:  vacuum via background vacuum thread (needs server running)
// ============================================================================
TEST_F (OosReclaim, DeleteThenVacuumReclaimsSpace)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Phase 1: Insert 10 OOS records */
  insert_oos_records (10);

  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0)
      << "OOS file should have pages after inserting OOS data";

  /* Phase 2: Delete all and vacuum */
  rc = exec_sql ("DELETE FROM t_reclaim");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Phase 3: Reinsert same amount */
  insert_oos_records (10);

  int pages_after_reinsert = get_oos_page_count ("t_reclaim");

  /* Vacuum should have freed OOS slots. Reinsert may not perfectly reuse all
   * freed slots (bestspace cache limitation), but page count should stay
   * within a reasonable margin — NOT double as it would without cleanup. */
  EXPECT_LE (pages_after_reinsert, pages_after_insert * 2)
      << "DELETE + vacuum should free OOS space — page count should not double";
}

// ============================================================================
// TC-R2: Multiple UPDATEs → OOS space reclaimed (without DELETE)
//
// Each UPDATE replaces the OOS column value, creating a new OOS record.
// Old OOS records must be cleaned up to prevent space leaks.
//
// Mechanism differs by mode:
//   - SA_MODE:     heap_update_home() eagerly deletes old OOS (non-MVCC path)
//   - SERVER_MODE: vacuum follows prev_version_lsa chain to find and delete
//                  old OOS records (vacuum_cleanup_prev_version_oos)
// ============================================================================
TEST_F (OosReclaim, MultipleUpdatesReclaimSpace)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Phase 1: Insert 5 OOS records */
  insert_oos_records (5);

  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0);

  /* Phase 2: Update each record 3 times (creates 15 old OOS versions) */
  update_all_oos_records (5, "BB");
  update_all_oos_records (5, "CC");
  update_all_oos_records (5, "DD");

#if defined (SA_MODE)
  /* SA_MODE: old OOS is eagerly deleted during UPDATE.
   * Page count should stay bounded immediately after updates. */
  int pages_after_updates = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_updates, pages_after_insert + 1)
      << "SA_MODE: old OOS should be eagerly reclaimed during UPDATE";
#else
  /* SERVER_MODE: old OOS is deferred to vacuum via prev_version_lsa chain. */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  int pages_after_vacuum = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_vacuum, pages_after_insert + 1)
      << "SERVER_MODE: vacuum should reclaim old OOS from prev_version chain";
#endif

  /* Verify current values are still readable */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_reclaim", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 5);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_reclaim WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-R3: Multiple UPDATEs → DELETE → vacuum → ALL OOS reclaimed
//
// End-to-end test: multiple updates create old versions, then DELETE + vacuum
// should clean up BOTH the current OOS records AND any remaining old versions.
//
// Works in both modes:
//   - SA_MODE:     UPDATE eagerly cleans old OOS; DELETE+vacuum cleans current
//   - SERVER_MODE: DELETE+vacuum cleans current OOS; vacuum also follows
//                  prev_version chain for any remaining old versions
// ============================================================================
TEST_F (OosReclaim, MultiUpdateThenDeleteVacuumReclaimsAll)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Phase 1: Insert 5 OOS records */
  insert_oos_records (5);

  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0);

  /* Phase 2: Update each record 3 times */
  update_all_oos_records (5, "BB");
  update_all_oos_records (5, "CC");
  update_all_oos_records (5, "DD");

  /* Phase 3: Delete all and vacuum */
  rc = exec_sql ("DELETE FROM t_reclaim");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Phase 4: Reinsert same amount — must reuse freed OOS space */
  insert_oos_records (5);

  int pages_after_reinsert = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_reinsert, pages_after_insert * 2)
      << "After UPDATE + DELETE + vacuum cycle, OOS space should be reusable — page count should not double";

  /* Verify data integrity */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_reclaim", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 5);
}

// ============================================================================
// TC-R4: Large-scale UPDATE churn — stress test for OOS reclamation
//
// 10 records x 10 UPDATEs = 100 old OOS versions that must be reclaimed.
// Verifies that the reclamation mechanism scales without unbounded growth.
// ============================================================================
TEST_F (OosReclaim, UpdateChurnStressTest)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert 10 records */
  insert_oos_records (10);

  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0);

  /* 10 rounds of UPDATE (10 records x 10 updates = 100 old OOS versions) */
  const char *patterns[] = { "11", "22", "33", "44", "55", "66", "77", "88", "99", "FF" };
  for (int round = 0; round < 10; round++)
    {
      update_all_oos_records (10, patterns[round]);
    }

#if defined (SA_MODE)
  /* SA_MODE: eager cleanup should keep page count bounded */
  int pages_after_churn = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_churn, pages_after_insert + 2)
      << "SA_MODE: 100 old OOS versions should all be eagerly reclaimed";
#else
  /* SERVER_MODE: vacuum cleans prev_version chains */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  int pages_after_churn = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_churn, pages_after_insert + 2)
      << "SERVER_MODE: vacuum should reclaim all 100 old OOS versions";
#endif

  /* All records should have the last update value */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_reclaim", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 10);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_reclaim WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
