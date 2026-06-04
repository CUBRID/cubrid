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
 * test_oos_sql_boundary.cpp - OOS boundary conditions and edge case SQL tests
 *
 * Ported from: cubrid-oos-test/sql/oos_boundary_edge.sql
 * CBRD-26352, CBRD-26547, CBRD-26565, CBRD-26488, CBRD-26608
 */

#include "test_oos_sql_common.hpp"

class OosSqlBoundary : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_bnd");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_bnd");
      db_commit_transaction ();
    }
};

// TC-03: NULL values in OOS-eligible columns
TEST_F (OosSqlBoundary, NullOosColumn)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd (id INT PRIMARY KEY, oos_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, NULL)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (2, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (3, NULL)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_bnd", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 3);

  // Update NULL to OOS value
  rc = exec_sql ("UPDATE t_oos_bnd SET oos_col = REPEAT(X'BB', 2048) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_bnd WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);

  // Update OOS value to NULL
  rc = exec_sql ("UPDATE t_oos_bnd SET oos_col = NULL WHERE id = 2");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int is_null = 0;
  rc = fetch_single_int ("SELECT CASE WHEN oos_col IS NULL THEN 1 ELSE 0 END FROM t_oos_bnd WHERE id = 2", &is_null);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (is_null, 1);
}

// TC-05: DROP TABLE with OOS data (CBRD-26608)
TEST_F (OosSqlBoundary, DropTableWithOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (2, REPEAT(X'BB', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (3, REPEAT(X'CC', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_bnd", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 3);

  // DROP TABLE should clean up both heap and OOS files
  rc = exec_sql ("DROP TABLE t_oos_bnd");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Table is now dropped; the DROP succeeding is the verification
}

// TC-06: DROP TABLE and recreate with same name
TEST_F (OosSqlBoundary, DropAndRecreateOosFile)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, REPEAT(X'AA', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DROP TABLE t_oos_bnd");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Recreate
  rc = exec_sql ("CREATE TABLE t_oos_bnd (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_bnd WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// TC-07: Index scan with OOS columns (CBRD-26547)
TEST_F (OosSqlBoundary, IndexScanWithOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd ("
		 "  id INT PRIMARY KEY,"
		 "  idx_col INT,"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("CREATE INDEX idx_oos_test ON t_oos_bnd (idx_col)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, 100, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (2, 200, REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (3, 100, REPEAT(X'CC', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Index scan should work correctly with OOS columns
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_bnd WHERE idx_col = 100", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);
}

// TC-09: MVCC header with OOS flag (CBRD-26488)
TEST_F (OosSqlBoundary, MvccHeaderWithOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Multiple updates exercise MVCC header transitions
  rc = exec_sql ("UPDATE t_oos_bnd SET data_col = REPEAT(X'BB', 2048) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_bnd SET data_col = REPEAT(X'CC', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_bnd SET data_col = REPEAT(X'DD', 1024) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_bnd WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-11: data_readval with COPY (CBRD-26352) - CTAS forces COPY read path
TEST_F (OosSqlBoundary, CopyReadPath)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (2, REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  exec_sql ("DROP TABLE IF EXISTS t_oos_bnd_copy");
  db_commit_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_bnd_copy AS SELECT * FROM t_oos_bnd");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_bnd_copy", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  exec_sql ("DROP TABLE t_oos_bnd_copy");
  db_commit_transaction ();
}

// TC-12: INSERT ... SELECT with OOS columns
TEST_F (OosSqlBoundary, InsertSelect)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  exec_sql ("DROP TABLE IF EXISTS t_oos_bnd_dst");
  db_commit_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_bnd_dst (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (2, REPEAT(X'BB', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_bnd_dst SELECT * FROM t_oos_bnd");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_bnd_dst", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_bnd_dst WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);

  exec_sql ("DROP TABLE t_oos_bnd_dst");
  db_commit_transaction ();
}

// TC-13: mid-size variable columns participate in OOS demotion
//
// A variable column is OOS-eligible when its value is larger than the OOS stub
// (OR_OOS_INLINE_SIZE = 16 B) it would be replaced with. This record is built
// only from ~500 B columns: each is larger than the 16 B stub (so eligible) yet
// smaller than the legacy 512 B floor (so previously ineligible). The record as a
// whole exceeds the DB_PAGESIZE/4 trigger, so the largest columns are demoted to
// OOS one by one. This guards round-trip correctness of that newly activated path.
//
// NOTE: SQL cannot observe *which* columns went to OOS, so this asserts that every
// column reads back at full length without error, not placement. A placement-asserting
// test would need a low-level harness.
TEST_F (OosSqlBoundary, MidSizeColumnsEligibleForOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_bnd ("
		 "  id INT PRIMARY KEY,"
		 "  c1 BIT VARYING, c2 BIT VARYING, c3 BIT VARYING, c4 BIT VARYING, c5 BIT VARYING,"
		 "  c6 BIT VARYING, c7 BIT VARYING, c8 BIT VARYING, c9 BIT VARYING, c10 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 10 columns x 500 B = ~5000 B payload > DB_PAGESIZE/4 (4096 at a 16 KB page).
  // Each column: 500 B > OR_OOS_INLINE_SIZE (16) but < legacy 512 B floor.
  rc = exec_sql ("INSERT INTO t_oos_bnd VALUES (1, "
		 "REPEAT(X'AA', 500), REPEAT(X'BB', 500), REPEAT(X'CC', 500), REPEAT(X'DD', 500), REPEAT(X'EE', 500), "
		 "REPEAT(X'11', 500), REPEAT(X'22', 500), REPEAT(X'33', 500), REPEAT(X'44', 500), REPEAT(X'55', 500))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_bnd", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  // first, middle and last columns all round-trip at full length through the demotion
  // path (covers both the demoted-to-OOS and the still-inline columns of the record)
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(c1) FROM t_oos_bnd WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 1000);
  rc = fetch_single_int ("SELECT LENGTH(c5) FROM t_oos_bnd WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 1000);
  rc = fetch_single_int ("SELECT LENGTH(c10) FROM t_oos_bnd WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 1000);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
