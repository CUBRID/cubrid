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
 * test_oos_sql_visible_version.cpp - visible-version fetch policy regression tests
 *
 * CBRD-26847
 */

#include "log_impl.h"
#include "test_oos_sql_common.hpp"

/* bridge functions defined in oos_file.cpp (CUBRID_UNIT_TEST_ENABLED builds) */
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();

class OosSqlVisibleVersion : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_visible_version");
      db_commit_transaction ();
    }

    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_visible_version");
      db_commit_transaction ();
    }
};

TEST_F (OosSqlVisibleVersion, ScanrangeNextFirstObjectFetchDoesNotExpandWholeRecord)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_visible_version (id INT PRIMARY KEY, payload BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_visible_version VALUES (1, REPEAT(X'AA', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *class_object = db_find_class ("t_oos_visible_version");
  ASSERT_NE (class_object, nullptr);

  OID class_oid = *reinterpret_cast<OID *> (db_identifier (class_object));
  HFID hfid;
  FILE_TYPE file_type;
  rc = heap_get_class_info (thread_p, &class_oid, &hfid, &file_type, nullptr);
  ASSERT_EQ (rc, NO_ERROR);

  MVCC_SNAPSHOT *snapshot = logtb_get_mvcc_snapshot (thread_p);
  ASSERT_NE (snapshot, nullptr);

  HEAP_SCANRANGE scan_range;
  rc = heap_scanrange_start (thread_p, &scan_range, &hfid, &class_oid, snapshot);
  ASSERT_EQ (rc, NO_ERROR);

  bridge_oos_debug_counters_reset ();
  SCAN_CODE scan = heap_scanrange_to_following (thread_p, &scan_range, nullptr);
  EXPECT_EQ (scan, S_SUCCESS);

  OID next_oid;
  RECDES recdes = RECDES_INITIALIZER;
  OID_SET_NULL (&next_oid);

  bridge_oos_debug_counters_reset ();
  scan = heap_scanrange_next (thread_p, &next_oid, &recdes, &scan_range, PEEK);
  EXPECT_EQ (scan, S_SUCCESS);
  EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, 0ULL);

  heap_scanrange_end (thread_p, &scan_range);
}

/*
 * The old record is much larger than the fixed fetch buffer when logically expanded. Updating only the indexed
 * integer must fetch the stored-form record and Resolve attributes through the attribute layer instead of eagerly
 * Expanding the whole 64KB OOS-backed value.
 */
TEST_F (OosSqlVisibleVersion, LargeOldRecordUpdateDoesNotExpandWholeRecord)
{
  int rc;
  int bit_length = 0;
  int value_matches = 0;

  rc = exec_sql ("CREATE TABLE t_oos_visible_version (id INT PRIMARY KEY, key_col INT, payload BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("CREATE INDEX i_oos_visible_version_key ON t_oos_visible_version (key_col)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_visible_version VALUES (1, 10, REPEAT(X'AA', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  bridge_oos_debug_counters_reset ();
  rc = exec_sql ("UPDATE t_oos_visible_version SET key_col = 20 WHERE id = 1");
  ASSERT_GE (rc, 0);
  EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, 0ULL);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(payload) FROM t_oos_visible_version WHERE id = 1", &bit_length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (bit_length, 131072);

  rc = fetch_single_int ("SELECT payload = CAST(REPEAT(X'AA', 65536) AS BIT VARYING) "
			 "FROM t_oos_visible_version WHERE id = 1", &value_matches);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (value_matches, 1);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
