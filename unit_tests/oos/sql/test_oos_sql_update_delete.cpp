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
 * test_oos_sql_update_delete.cpp - OOS UPDATE and DELETE operations SQL tests
 *
 * Ported from: cubrid-oos-test/sql/oos_update_delete.sql
 * CBRD-26352, CBRD-26521
 */

#include "test_oos_sql_common.hpp"

class OosSqlUpdateDelete : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_ud");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_ud");
      db_commit_transaction ();
    }
};

// TC-01: UPDATE OOS column value
TEST_F (OosSqlUpdateDelete, UpdateOosValue)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ud ("
		 "  id INT PRIMARY KEY,"
		 "  label VARCHAR(50),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (1, 'original', REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  rc = exec_sql ("UPDATE t_oos_ud SET oos_col = REPEAT(X'BB', 2048), label = 'updated' WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);
}

// TC-02: UPDATE non-OOS column (OOS column should remain unchanged)
TEST_F (OosSqlUpdateDelete, UpdateNonOosColumn)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ud ("
		 "  id INT PRIMARY KEY,"
		 "  label VARCHAR(50),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (1, 'original', REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_ud SET label = 'label_changed' WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-03: Repeated updates on same OOS column (10 times)
TEST_F (OosSqlUpdateDelete, RepeatedUpdates)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ud (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (1, REPEAT(X'00', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  const char *updates[] =
  {
    "UPDATE t_oos_ud SET data_col = REPEAT(X'01', 1034) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'02', 1044) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'03', 1054) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'04', 1064) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'05', 1074) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'06', 1084) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'07', 1094) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'08', 1104) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'09', 1114) WHERE id = 1",
    "UPDATE t_oos_ud SET data_col = REPEAT(X'0A', 1124) WHERE id = 1",
  };

  for (const auto &sql : updates)
    {
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  // Final value: 0x0A, length 1124 bytes = 2248 bits
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2248);
}

// TC-04: UPDATE single-chunk to multi-chunk and back
TEST_F (OosSqlUpdateDelete, SizeChangeChunkTransition)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ud (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // Single-chunk (1KB)
  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  // -> Multi-chunk (32KB)
  rc = exec_sql ("UPDATE t_oos_ud SET data_col = REPEAT(X'BB', 32768) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);

  // -> Back to single-chunk (1KB)
  rc = exec_sql ("UPDATE t_oos_ud SET data_col = REPEAT(X'CC', 1024) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-05: DELETE rows with OOS columns
TEST_F (OosSqlUpdateDelete, DeleteOosRows)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ud (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (2, REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (3, REPEAT(X'CC', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ud", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 3);

  rc = exec_sql ("DELETE FROM t_oos_ud WHERE id = 2");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ud", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 2);

  // Remaining rows intact
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_ud WHERE id = 3", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// TC-07: TRUNCATE table with OOS data, then reinsert
TEST_F (OosSqlUpdateDelete, TruncateAndReinsert)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ud (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (2, REPEAT(X'BB', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("TRUNCATE TABLE t_oos_ud");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ud", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  // Reinsert
  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (100, REPEAT(X'EE', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_ud", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);
}

// TC-08: UPDATE multi-chunk OOS record (replace entire chain)
TEST_F (OosSqlUpdateDelete, UpdateMultiChunk)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_ud (id INT PRIMARY KEY, huge_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_ud VALUES (1, REPEAT(X'AA', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(huge_col) FROM t_oos_ud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);

  // Update to different 64KB multi-chunk
  rc = exec_sql ("UPDATE t_oos_ud SET huge_col = REPEAT(X'BB', 65536) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(huge_col) FROM t_oos_ud WHERE id = 1", &len);
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
