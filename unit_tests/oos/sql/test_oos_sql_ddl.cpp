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
 * test_oos_sql_ddl.cpp - OOS DDL operations SQL tests
 *
 * Ported from: cubrid-oos-test/sql/oos_ddl_operations.sql
 */

#include "test_oos_sql_common.hpp"

class OosSqlDdl : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_ddl");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_ddl");
      db_commit_transaction ();
    }
};

// TC-01: ALTER TABLE ADD COLUMN to table with existing OOS data
TEST_F (OosSqlDdl, AlterAddColumn)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ddl (id INT PRIMARY KEY, oos_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (2, REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Add new column
  rc = exec_sql ("ALTER TABLE t_oos_ddl ADD COLUMN new_col VARCHAR(100)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Verify existing OOS data intact
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_ddl WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_ddl WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);

  // Insert with new column
  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (3, REPEAT(X'CC', 4096), 'new_value')");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 3);
}

// TC-03: ALTER TABLE DROP COLUMN that contains OOS data
TEST_F (OosSqlDdl, AlterDropOosColumn)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ddl ("
		 "  id INT PRIMARY KEY,"
		 "  keep_col VARCHAR(100),"
		 "  drop_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, 'keep_me', REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (2, 'keep_me_too', REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Drop the OOS column
  rc = exec_sql ("ALTER TABLE t_oos_ddl DROP COLUMN drop_col");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Remaining data should be intact
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);
}

// TC-05: CREATE TABLE AS SELECT (CTAS) with OOS data
TEST_F (OosSqlDdl, CreateTableAsSelect)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ddl (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (2, REPEAT(X'BB', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // CTAS
  exec_sql ("DROP TABLE IF EXISTS t_oos_ddl_dst");
  db_commit_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_ddl_dst AS SELECT * FROM t_oos_ddl");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl_dst", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ddl_dst WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);

  // Drop source; dst should still work
  exec_sql ("DROP TABLE t_oos_ddl");
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl_dst", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  exec_sql ("DROP TABLE t_oos_ddl_dst");
  db_commit_transaction ();
}

// TC-06: DROP TABLE then CREATE TABLE with same name
TEST_F (OosSqlDdl, DropAndRecreateSameName)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ddl (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, REPEAT(X'AA', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (2, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Drop
  rc = exec_sql ("DROP TABLE t_oos_ddl");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Recreate with different schema
  rc = exec_sql ("CREATE TABLE t_oos_ddl ("
		 "  id INT PRIMARY KEY,"
		 "  label VARCHAR(50),"
		 "  data_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, 'new_table', REPEAT(X'CC', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ddl WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-07: Multiple sequential DROP/CREATE cycles
TEST_F (OosSqlDdl, DropCreateCycles)
{
  int rc;
  int count = 0;

  // Cycle 1
  rc = exec_sql ("CREATE TABLE t_oos_ddl (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  rc = exec_sql ("DROP TABLE t_oos_ddl");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Cycle 2
  rc = exec_sql ("CREATE TABLE t_oos_ddl (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (2, REPEAT(X'CC', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  rc = exec_sql ("DROP TABLE t_oos_ddl");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Cycle 3
  rc = exec_sql ("CREATE TABLE t_oos_ddl (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ddl VALUES (1, REPEAT(X'DD', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
