/*
 *
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
 * test_oos_sql_bigone.cpp - OOS + bigone coexistence is rejected (CBRD-26937)
 *
 * When a record still exceeds heap_Maxslotted_reclength after every OOS-eligible
 * variable column has been demoted to out-of-row storage, it would have to be
 * stored as a multipage (REC_BIGONE) overflow record while also carrying OOS OIDs.
 * That combination is unsupported, so heap_attrinfo_transform_to_disk_internal
 * rejects it with ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE before writing any OOS record.
 *
 * To force the residual record over the limit we use a large FIXED BIT(n) column:
 * BIT (unlike BIT VARYING) is not OOS-eligible, so it cannot be demoted and keeps
 * the inline record large regardless of the OOS column beside it.
 */

#include "test_oos_sql_common.hpp"

class OosSqlBigone : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_big");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_big");
      db_commit_transaction ();
    }
};

// Core: a record that still overflows after OOS demotion is rejected.
// BIT(140000) = 17500 B fixed (cannot be demoted); b (> OR_OOS_INLINE_SIZE = 16 B,
// variable) is demoted to OOS, setting has_oos. The residual record (~17.5 KB)
// exceeds heap_Maxslotted_reclength, so the insert is rejected and stores nothing.
TEST_F (OosSqlBigone, OosColumnWithBigoneRejected)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_big (a BIT(140000), b VARCHAR(2000))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_big VALUES (B'1', REPEAT('x', 1000))");
  int errid = er_errid ();
  EXPECT_LT (rc, 0);
  EXPECT_EQ (errid, ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE);
  db_abort_transaction ();

  // the rejected row must not have been stored
  int count = -1;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_big", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);
}

// Guard the has_oos gate: the same oversized fixed column WITHOUT an OOS-eligible
// column is a plain REC_BIGONE overflow record, which is still supported. The
// gate must not fire here.
TEST_F (OosSqlBigone, BigoneWithoutOosColumnSucceeds)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_big (a BIT(140000))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_big VALUES (B'1')");
  EXPECT_GE (rc, 0);
  db_commit_transaction ();

  int count = -1;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_big", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);
}

// Regression guard for the 16 KB threshold choice (not DB_PAGESIZE/4): a record
// that HAS an OOS column and whose inline residual lands between DB_PAGESIZE/4
// (~4 KB) and heap_Maxslotted_reclength (~16 KB) must still insert. BIT(100000) =
// 12500 B fixed + an OOS-demoted varchar leaves ~12.5 KB inline. A naive
// DB_PAGESIZE/4 gate would wrongly reject this row.
TEST_F (OosSqlBigone, OosColumnInlineBetween4kAnd16kSucceeds)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_big (a BIT(100000), b VARCHAR(2000))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_big VALUES (B'1', REPEAT('x', 1000))");
  EXPECT_GE (rc, 0);
  db_commit_transaction ();

  int count = -1;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_big", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);
}

// UPDATE that grows a row into OOS + bigone is rejected too, exercising the same
// gate from the update path. The row starts as a plain REC_BIGONE: b is NULL, so
// it is not OOS-eligible and the large fixed BIT keeps it a (non-OOS) overflow
// record, which inserts fine. Updating b to a > 16 B value demotes it to OOS,
// making the now-has_oos record exceed heap_Maxslotted_reclength.
TEST_F (OosSqlBigone, UpdateIntoOosBigoneRejected)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_big (a BIT(140000), b VARCHAR(2000))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_big VALUES (B'1', NULL)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_big SET b = REPEAT('x', 1000)");
  int errid = er_errid ();
  EXPECT_LT (rc, 0);
  EXPECT_EQ (errid, ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE);
  db_abort_transaction ();

  // the rejected update left the row unchanged (b still NULL)
  int is_null = 0;
  rc = fetch_single_int ("SELECT CASE WHEN b IS NULL THEN 1 ELSE 0 END FROM t_oos_big", &is_null);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (is_null, 1);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
