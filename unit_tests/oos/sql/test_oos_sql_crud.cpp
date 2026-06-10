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
 * test_oos_sql_crud.cpp - OOS Basic CRUD SQL tests
 *
 * Ported from: cubrid-oos-test/sql/oos_basic_crud.sql
 * CBRD-26352, CBRD-26358, CBRD-26458
 */

#include "test_oos_sql_common.hpp"

class OosSqlCrud : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_crud");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_crud");
      db_commit_transaction ();
    }
};

// TC-01: Basic INSERT and SELECT of OOS column
TEST_F (OosSqlCrud, BasicInsertSelect)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  small_col VARCHAR(100),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 1024-byte BIT VARYING value
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, 'row1', REPEAT(X'AB', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-02: Small record should NOT trigger OOS
TEST_F (OosSqlCrud, SmallRecordNoOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  small_data BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 200-byte record: below the DB_PAGESIZE/4 record trigger => no OOS
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, REPEAT(X'CD', 200))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(small_data) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 400);
}

// TC-04: Multiple OOS columns in one record
TEST_F (OosSqlCrud, MultipleOosColumns)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  oos_col1 BIT VARYING,"
		 "  oos_col2 BIT VARYING,"
		 "  oos_col3 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_crud VALUES ("
		 "  1,"
		 "  REPEAT(X'AA', 1024),"
		 "  REPEAT(X'BB', 2048),"
		 "  REPEAT(X'CC', 4096)"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len1 = 0, len2 = 0, len3 = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col1) FROM t_oos_crud WHERE id = 1", &len1);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len1, 2048);

  rc = fetch_single_int ("SELECT LENGTH(oos_col2) FROM t_oos_crud WHERE id = 1", &len2);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len2, 4096);

  rc = fetch_single_int ("SELECT LENGTH(oos_col3) FROM t_oos_crud WHERE id = 1", &len3);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len3, 8192);
}

// TC-06: Multi-chunk OOS (value > page size, ~16KB)
TEST_F (OosSqlCrud, MultiChunkOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  huge_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 32KB value spans multiple OOS pages
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, REPEAT(X'FF', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(huge_col) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);
}

// TC-07: Mixed single-chunk and multi-chunk in same table
TEST_F (OosSqlCrud, MixedChunkSizes)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  data_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (2, REPEAT(X'BB', 32768))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (3, REPEAT(X'CC', 100))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_crud", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 3);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_crud WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
