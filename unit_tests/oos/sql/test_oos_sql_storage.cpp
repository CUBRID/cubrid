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
 * test_oos_sql_storage.cpp - STORAGE PREFER_INLINE column option SQL tests (CBRD-26912)
 *
 * STORAGE PREFER_INLINE lowers a column's OOS demotion priority so the column stays
 * inline when possible. These tests assert the option's *persistence* through the
 * schema, which is where the option must not be silently lost:
 *   - SHOW CREATE TABLE          -> object_printer::describe_attribute emit
 *   - CREATE TABLE ... LIKE      -> classobj_copy_attribute_like flag copy
 *   - ALTER TABLE ... MODIFY     -> build_attr_change_map GAINED/LOST state machine
 *
 * Note: SQL cannot observe *which* column was demoted to OOS (see the placement note in
 * test_oos_sql_boundary.cpp), so demote *ordering* is not asserted here - the demote sort
 * lives in the heap layer. These tests cover the schema-persistence gaps, which ARE
 * observable as DDL text. The unloaddb emit path (a separate utility binary) is not
 * reachable from this in-process harness and is left to a shell-level test.
 */

#include <string>

#include "test_oos_sql_common.hpp"

namespace
{
  // SHOW CREATE TABLE returns one row with column 0 = table name and column 1 = the
  // CREATE TABLE statement. Read both columns directly (mirroring fetch_single_int's
  // fixed-index access) and concatenate the string values; the DDL lives in column 1.
  int
  get_create_table_ddl (const char *table_name, std::string &out_ddl)
  {
    char sql[256];
    snprintf (sql, sizeof (sql), "SHOW CREATE TABLE %s", table_name);

    DB_QUERY_RESULT *result = nullptr;
    int rc = exec_sql_with_result (sql, &result);
    if (rc < 0)
      {
	return rc;
      }
    if (result == nullptr)
      {
	return ER_FAILED;
      }

    rc = db_query_first_tuple (result);
    if (rc != DB_CURSOR_SUCCESS)
      {
	db_query_end (result);
	return ER_FAILED;
      }

    out_ddl.clear ();
    for (int c = 0; c <= 1; ++c)
      {
	DB_VALUE val;
	db_make_null (&val);
	if (db_query_get_tuple_value (result, c, &val) == NO_ERROR
	    && (db_value_type (&val) == DB_TYPE_STRING || db_value_type (&val) == DB_TYPE_CHAR)
	    && db_get_string (&val) != nullptr)
	  {
	    out_ddl += db_get_string (&val);
	    out_ddl += '\n';
	  }
	db_value_clear (&val);
      }

    db_query_end (result);
    return NO_ERROR;
  }

  bool
  ddl_has_prefer_inline (const std::string &ddl)
  {
    return ddl.find ("STORAGE PREFER_INLINE") != std::string::npos;
  }
}

class OosSqlStorage : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_stg_like");
      exec_sql ("DROP TABLE IF EXISTS t_oos_stg");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_stg_like");
      exec_sql ("DROP TABLE IF EXISTS t_oos_stg");
      db_commit_transaction ();
    }
};

// CREATE TABLE with STORAGE PREFER_INLINE must keep the option in SHOW CREATE TABLE.
// Covers object_printer::describe_attribute emit and the parser round-trip.
TEST_F (OosSqlStorage, CreateTablePersistsPreferInline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  id INT PRIMARY KEY,"
		     "  hot VARCHAR(4096) STORAGE PREFER_INLINE,"
		     "  cold VARCHAR(4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_prefer_inline (ddl)) << "DDL was:\n" << ddl;
}

// A column without the option must NOT emit any STORAGE clause (guards against a
// spurious or always-on emit).
TEST_F (OosSqlStorage, DefaultColumnHasNoStorageClause)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (id INT PRIMARY KEY, cold VARCHAR(4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "DDL was:\n" << ddl;
}

// CREATE TABLE ... LIKE must copy the option onto the cloned column. The option can
// only appear in the clone's DDL if classobj_copy_attribute_like copied the flag.
TEST_F (OosSqlStorage, CreateTableLikeCopiesPreferInline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  id INT PRIMARY KEY,"
		     "  hot VARCHAR(4096) STORAGE PREFER_INLINE)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_stg_like LIKE t_oos_stg");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg_like", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_prefer_inline (ddl)) << "cloned DDL was:\n" << ddl;
}

// STORAGE PREFER_INLINE is meaningful only for normal instance attributes; shared
// attributes are not stored in each heap record and must not persist a no-op policy.
TEST_F (OosSqlStorage, SharedAttributeRejectsPreferInline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (s VARCHAR(4096) SHARED 'x' STORAGE PREFER_INLINE)");
  EXPECT_LT (rc, 0);
  db_abort_transaction ();
}

// ALTER TABLE ... MODIFY must add the option, and MODIFY ... STORAGE DEFAULT must drop
// it. Covers the build_attr_change_map GAINED/LOST path plus persistence.
TEST_F (OosSqlStorage, AlterModifyAddsAndDropsPreferInline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (id INT PRIMARY KEY, c VARCHAR(4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_FALSE (ddl_has_prefer_inline (ddl)) << "initial DDL was:\n" << ddl;

  // GAINED: plain column -> STORAGE PREFER_INLINE
  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(4096) STORAGE PREFER_INLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_prefer_inline (ddl)) << "after MODIFY PREFER_INLINE, DDL was:\n" << ddl;

  // LOST: STORAGE PREFER_INLINE -> STORAGE DEFAULT
  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(4096) STORAGE DEFAULT");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "after MODIFY DEFAULT, DDL was:\n" << ddl;
}

// Functional non-regression: a PREFER_INLINE column round-trips a large value through
// the demotion path. The record (two ~3000 B variable columns) exceeds DB_PAGESIZE/4, so
// demotion runs. Placement is not asserted (not observable via SQL); this guards data
// integrity of the new demote sort, not which column was demoted.
TEST_F (OosSqlStorage, PreferInlineColumnRoundTripsLargeValue)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  id INT PRIMARY KEY,"
		     "  hot BIT VARYING STORAGE PREFER_INLINE,"
		     "  cold BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // REPEAT(X'xx', N) yields an N-byte value; LENGTH() reports 2*N (hex-nibble count),
  // matching the convention used in test_oos_sql_boundary.cpp.
  rc = exec_sql ("INSERT INTO t_oos_stg VALUES (1, REPEAT(X'AA', 3000), REPEAT(X'BB', 3000))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(hot) FROM t_oos_stg WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 6000);

  rc = fetch_single_int ("SELECT LENGTH(cold) FROM t_oos_stg WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 6000);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
