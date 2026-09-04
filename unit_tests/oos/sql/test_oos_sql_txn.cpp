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
 * test_oos_sql_txn.cpp - OOS transaction (ACID) and MVCC SQL tests
 *
 * Ported from: cubrid-oos-test/sql/oos_delete_api_txn.sql,
 *              cubrid-oos-test/sql/oos_transaction_mvcc_txn.sql
 * CBRD-26352, CBRD-26463, CBRD-26609
 *
 * Note: True MVCC isolation tests require two concurrent sessions.
 * These tests verify single-session COMMIT/ROLLBACK atomicity.
 */

#include "test_oos_sql_common.hpp"

class OosSqlTxn : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_txn");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      // Ensure any pending transaction is cleaned up
      db_abort_transaction ();
      exec_sql ("DROP TABLE IF EXISTS t_oos_txn");
      db_commit_transaction ();
    }
};

// TC-01: ROLLBACK cancels OOS INSERT
TEST_F (OosSqlTxn, RollbackInsert)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);

  // Within transaction, row exists
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  db_abort_transaction ();

  // After rollback, row should not exist
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);
}

// TC-02: ROLLBACK cancels OOS UPDATE
TEST_F (OosSqlTxn, RollbackUpdate)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Update in new transaction
  rc = exec_sql ("UPDATE t_oos_txn SET data_col = REPEAT(X'BB', 2048) WHERE id = 1");
  ASSERT_GE (rc, 0);

  // Within transaction, see updated value
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);

  db_abort_transaction ();

  // After rollback, original value restored
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-03: Committed OOS data persists
TEST_F (OosSqlTxn, CommittedDataPersists)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'DD', 2048))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (2, REPEAT(X'EE', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// TC-05: Multi-chunk OOS in transaction with ROLLBACK
TEST_F (OosSqlTxn, RollbackMultiChunk)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, huge_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Insert 32KB multi-chunk record
  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'FF', 32768))");
  ASSERT_GE (rc, 0);

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  db_abort_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);
}

// TC-06: INSERT + UPDATE + DELETE in single transaction then COMMIT
TEST_F (OosSqlTxn, ComboThenCommit)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (2, REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (3, REPEAT(X'CC', 4096))");
  ASSERT_GE (rc, 0);

  // Update row 2
  rc = exec_sql ("UPDATE t_oos_txn SET data_col = REPEAT(X'DD', 1024) WHERE id = 2");
  ASSERT_GE (rc, 0);

  // Delete row 1
  rc = exec_sql ("DELETE FROM t_oos_txn WHERE id = 1");
  ASSERT_GE (rc, 0);

  db_commit_transaction ();

  // row 1 gone, row 2 updated, row 3 unchanged
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 3", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// TC-07: ROLLBACK after DELETE restores visibility
TEST_F (OosSqlTxn, RollbackDelete)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_txn WHERE id = 1");
  ASSERT_GE (rc, 0);

  // Within transaction, row is deleted
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  db_abort_transaction ();

  // After rollback, row visible again
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// From oos_delete_api_txn.sql: DELETE inside committed transaction
TEST_F (OosSqlTxn, DeleteCommit)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_txn WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_txn", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);
}

// From oos_delete_api_txn.sql: DELETE inside rolled-back transaction
TEST_F (OosSqlTxn, DeleteRollback)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_txn (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_txn VALUES (1, REPEAT(X'AA', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_txn WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_abort_transaction ();

  // Row should still exist after rollback
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_txn WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
