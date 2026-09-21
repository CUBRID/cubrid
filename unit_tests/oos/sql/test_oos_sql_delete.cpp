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
 * test_oos_sql_delete.cpp - OOS Delete API SQL tests
 *
 * Ported from: cubrid-oos-test/sql/oos_delete_api.sql
 * CBRD-26609
 */

#include "test_oos_sql_common.hpp"

class OosSqlDelete : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_del");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_del");
      db_commit_transaction ();
    }
};

// TC-01: DELETE a single-chunk OOS record
TEST_F (OosSqlDelete, DeleteSingleChunk)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del ("
		 "  id INT PRIMARY KEY,"
		 "  label VARCHAR(50),"
		 "  data_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, 'single_chunk', REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del WHERE id = 1", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  rc = exec_sql ("DELETE FROM t_oos_del WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del WHERE id = 1", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);
}

// TC-02: DELETE a multi-chunk OOS record (32KB)
TEST_F (OosSqlDelete, DeleteMultiChunk)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del (id INT PRIMARY KEY, huge_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, REPEAT(X'BB', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_del WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del WHERE id = 1", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);
}

// TC-03: DELETE a very large multi-chunk OOS record (160KB)
TEST_F (OosSqlDelete, DeleteLarge160KB)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del (id INT PRIMARY KEY, big_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, REPEAT(X'CC', 163840))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_del WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);
}

// TC-04: DELETE one row among multiple (other rows unaffected)
TEST_F (OosSqlDelete, DeleteSelectiveRowsIntact)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (2, REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (3, REPEAT(X'CC', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_del WHERE id = 2");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_del WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_del WHERE id = 3", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// TC-05: DELETE then reinsert
TEST_F (OosSqlDelete, DeleteAndReinsert)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, REPEAT(X'AA', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (2, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_del");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (10, REPEAT(X'DD', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (20, REPEAT(X'EE', 8192))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);
}

// TC-06: DELETE row with mixed OOS and non-OOS columns
TEST_F (OosSqlDelete, DeleteMixedColumns)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del ("
		 "  id INT PRIMARY KEY,"
		 "  small_col VARCHAR(100),"
		 "  oos_col1 BIT VARYING,"
		 "  oos_col2 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, 'keep_me', REPEAT(X'AA', 1024), REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (2, 'delete_me', REPEAT(X'CC', 4096), REPEAT(X'DD', 8192))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_del WHERE id = 2");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_del", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col1) FROM t_oos_del WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-07: UPDATE OOS column (insert-new + delete-old under the hood)
TEST_F (OosSqlDelete, UpdateOosColumn)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, REPEAT(X'AA', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_del WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);

  rc = exec_sql ("UPDATE t_oos_del SET data_col = REPEAT(X'FF', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_del WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// TC-09: DELETE with multi-chunk then reinsert multi-chunk
TEST_F (OosSqlDelete, DeleteAndReinsertMultiChunk)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_del (id INT PRIMARY KEY, big_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_del VALUES (1, REPEAT(X'AA', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_del WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Reinsert large record (should reuse freed pages)
  rc = exec_sql ("INSERT INTO t_oos_del VALUES (2, REPEAT(X'BB', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(big_col) FROM t_oos_del WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 131072);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
