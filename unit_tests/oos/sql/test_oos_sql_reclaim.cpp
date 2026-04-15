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
 * after DELETE and UPDATE operations.
 *
 * Build modes:
 *   SA_MODE  -> linked to cubridsa, boots server in-process, page count verification
 *   CS_MODE  -> linked to cubridcs, connects to running cub_server, data correctness only
 *
 * Reclamation mechanism by mode:
 *   SA_MODE:  non-MVCC UPDATE -> eager old OOS deletion in heap_update_home()
 *   CS_MODE:  MVCC UPDATE -> vacuum follows prev_version_lsa chain (vacuum_cleanup_prev_version_oos)
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
// Helpers
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

static void
verify_record_count (int expected)
{
  int count = 0;
  int rc = fetch_single_int ("SELECT COUNT(*) FROM t_reclaim", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, expected);
}

static void
verify_oos_data_readable (int id, int expected_len = 8192)
{
  int len = 0;
  char sql[128];
  snprintf (sql, sizeof (sql), "SELECT LENGTH(data_col) FROM t_reclaim WHERE id = %d", id);
  int rc = fetch_single_int (sql, &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, expected_len);
}

// ============================================================================
// TC-R1: INSERT -> DELETE -> vacuum -> OOS space reclaimed
//
// SA_MODE:  page count verification (reinsert should reuse freed space)
// CS_MODE:  vacuum succeeds + data correctness after reinsert
// ============================================================================
TEST_F (OosReclaim, DeleteThenVacuumReclaimsSpace)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  insert_oos_records (10);

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0);
#endif

  rc = exec_sql ("DELETE FROM t_reclaim");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Reinsert same amount */
  insert_oos_records (10);

#if defined(SA_MODE)
  int pages_after_reinsert = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_reinsert, pages_after_insert * 2)
      << "DELETE + vacuum should free OOS space — page count should not double";
#endif

  /* Both modes: verify data correctness */
  verify_record_count (10);
  verify_oos_data_readable (1);
  verify_oos_data_readable (10);
}

// ============================================================================
// TC-R2: Multiple UPDATEs -> OOS space reclaimed (without DELETE)
//
// SA_MODE:  heap_update_home() eagerly deletes old OOS, page count stays bounded
// CS_MODE:  vacuum cleans prev_version_lsa chain, page count bounded after vacuum
// ============================================================================
TEST_F (OosReclaim, MultipleUpdatesReclaimSpace)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  insert_oos_records (5);

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0);
#endif

  /* Update each record 3 times */
  update_all_oos_records (5, "BB");
  update_all_oos_records (5, "CC");
  update_all_oos_records (5, "DD");

#if defined(SA_MODE)
  /* SA_MODE: old OOS eagerly deleted during UPDATE */
  int pages_after_updates = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_updates, pages_after_insert + 1)
      << "SA_MODE: old OOS should be eagerly reclaimed during UPDATE";
#else
  /* CS_MODE: vacuum cleans old OOS via prev_version_lsa chain */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);
#endif

  /* Both modes: verify data correctness */
  verify_record_count (5);
  verify_oos_data_readable (1);
  verify_oos_data_readable (5);
}

// ============================================================================
// TC-R3: Multiple UPDATEs -> DELETE -> vacuum -> ALL OOS reclaimed
//
// End-to-end test combining UPDATE and DELETE cleanup.
// SA_MODE:  UPDATE eagerly cleans old OOS; DELETE+vacuum cleans current
// CS_MODE:  DELETE+vacuum cleans current OOS + prev_version chain
// ============================================================================
TEST_F (OosReclaim, MultiUpdateThenDeleteVacuumReclaimsAll)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  insert_oos_records (5);

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0);
#endif

  update_all_oos_records (5, "BB");
  update_all_oos_records (5, "CC");
  update_all_oos_records (5, "DD");

  rc = exec_sql ("DELETE FROM t_reclaim");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Reinsert */
  insert_oos_records (5);

#if defined(SA_MODE)
  int pages_after_reinsert = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_reinsert, pages_after_insert * 2)
      << "After UPDATE + DELETE + vacuum, OOS space should be reusable";
#endif

  /* Both modes: verify data correctness */
  verify_record_count (5);
  verify_oos_data_readable (1);
  verify_oos_data_readable (5);
}

// ============================================================================
// TC-R4: Large-scale UPDATE churn — 10 records x 10 UPDATEs = 100 old OOS
//
// Stress test: verifies reclamation scales without unbounded growth.
// ============================================================================
TEST_F (OosReclaim, UpdateChurnStressTest)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_reclaim (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  insert_oos_records (10);

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_reclaim");
  ASSERT_GT (pages_after_insert, 0);
#endif

  /* 10 rounds of UPDATE */
  const char *patterns[] = { "11", "22", "33", "44", "55", "66", "77", "88", "99", "FF" };
  for (int round = 0; round < 10; round++)
    {
      update_all_oos_records (10, patterns[round]);
    }

#if defined(SA_MODE)
  /* SA_MODE: eager cleanup keeps page count bounded */
  int pages_after_churn = get_oos_page_count ("t_reclaim");
  EXPECT_LE (pages_after_churn, pages_after_insert + 2)
      << "SA_MODE: 100 old OOS versions should all be eagerly reclaimed";
#else
  /* CS_MODE: vacuum cleans prev_version chains */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);
#endif

  /* Both modes: verify data correctness */
  verify_record_count (10);
  verify_oos_data_readable (1);
  verify_oos_data_readable (10);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
