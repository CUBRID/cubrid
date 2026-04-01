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
 * test_oos_sql_vacuum.cpp - OOS Vacuum integration SQL tests
 *
 * Verifies that vacuum correctly cleans up OOS records after DELETE/UPDATE.
 * Uses xvacuum() to trigger vacuum in SA_MODE and file_get_num_user_pages()
 * to observe OOS file state before/after vacuum.
 */

#include "test_oos_sql_common.hpp"

#include "vacuum.h"
#include "thread_manager.hpp"

// ============================================================================
// Helper: trigger vacuum in SA_MODE
// ============================================================================

static int
run_vacuum ()
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  return xvacuum (thread_p);
}

// ============================================================================
// Test fixture
// ============================================================================

class OosSqlVacuum : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_vac");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_vac");
      db_commit_transaction ();
    }
};

// ============================================================================
// TC-01: Delete single OOS record, vacuum, verify cleanup
// ============================================================================
TEST_F (OosSqlVacuum, DeleteSingleThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert a record with OOS column large enough to trigger OOS (4KB data) */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Verify data is readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);  /* REPEAT(X'AA', 4096) = 8192 bytes in BIT VARYING */

  /* Delete the record and commit */
  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Run vacuum — should clean up the OOS record */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify table is empty */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  /* Reinsert to verify OOS file is still functional after vacuum */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-02: Delete multiple OOS records, vacuum, verify cleanup
// ============================================================================
TEST_F (OosSqlVacuum, DeleteMultipleThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert multiple records with OOS columns */
  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'BB', %d))", i, 1024 * (i + 1));
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  /* Delete all records */
  rc = exec_sql ("DELETE FROM t_oos_vac");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Run vacuum */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify table is empty and can still be used */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  /* Reinsert to verify OOS file is still functional */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (10, REPEAT(X'CC', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 10", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-03: Update OOS column creates new OOS record; vacuum cleans old one
// ============================================================================
TEST_F (OosSqlVacuum, UpdateThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Update creates a new OOS record; old one remains for MVCC */
  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'FF', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Vacuum should clean up the old OOS record */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* The new record should still be readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);  /* REPEAT(X'FF', 4096) = 8192 bytes in BIT VARYING */
}

// ============================================================================
// TC-04: Delete + vacuum + reinsert — verify OOS space is reusable
// ============================================================================
TEST_F (OosSqlVacuum, DeleteVacuumReinsert)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Reinsert — should reuse vacuumed OOS space */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Verify data integrity */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-05: Multi-chunk OOS record delete + vacuum
// ============================================================================
TEST_F (OosSqlVacuum, DeleteMultiChunkThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, big_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert a large multi-chunk OOS record (64KB) */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'CC', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify empty */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  /* Reinsert multi-chunk to verify OOS still works */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, REPEAT(X'DD', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(big_col) FROM t_oos_vac WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 131072);
}

// ============================================================================
// TC-06: Mixed OOS and non-OOS columns — vacuum only cleans OOS
// ============================================================================
TEST_F (OosSqlVacuum, MixedColumnsVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac ("
		 "  id INT PRIMARY KEY,"
		 "  small_col VARCHAR(50),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, 'keep', REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, 'delete', REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Delete only row 2 */
  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 2");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Row 1 should still be intact */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// ============================================================================
// TC-07: Multiple updates then vacuum — only latest OOS survives
// ============================================================================
TEST_F (OosSqlVacuum, MultipleUpdatesThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Perform multiple updates — each creates a new OOS record */
  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'BB', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'CC', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'DD', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Vacuum should clean up the 3 old OOS versions */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Latest value should still be readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);  /* REPEAT(X'DD', 4096) = 8192 bytes */
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
