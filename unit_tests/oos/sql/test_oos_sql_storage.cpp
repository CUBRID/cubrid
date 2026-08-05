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
 * test_oos_sql_storage.cpp - STORAGE column option SQL tests (CBRD-26912, CBRD-26067)
 *
 * STORAGE PREFER_INLINE lowers a column's OOS demotion priority so the column stays
 * inline when possible. STORAGE FORCE_OUTLINE sends variable values larger than the
 * 16-byte OOS stub to OOS regardless of record size. STORAGE PREFER_OUTLINE is
 * equivalent to
 * STORAGE DEFAULT in this implementation. These tests assert the options' persistence:
 *   - SHOW CREATE TABLE          -> object_printer::describe_attribute emit
 *   - CREATE TABLE ... LIKE      -> classobj_copy_attribute_like flag copy
 *   - ALTER TABLE ... MODIFY     -> build_attr_change_map GAINED/LOST state machine
 *
 * FORCE_OUTLINE placement is observable through SHOW HEAP OOS because even a small
 * record with a value larger than the OOS stub creates an OOS file and value record.
 * PREFER_INLINE ordering is not observable
 * from SQL (see test_oos_sql_boundary.cpp). The unloaddb emit path (a separate utility
 * binary) is not reachable from this in-process harness and is left to a shell-level test.
 */

#include <string>

#include "test_oos_sql_common.hpp"

namespace
{
  const char *const storage_settings[] = { "PREFER_INLINE", "FORCE_OUTLINE", "PREFER_OUTLINE", "DEFAULT" };

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

  bool
  ddl_has_force_outline (const std::string &ddl)
  {
    return ddl.find ("STORAGE FORCE_OUTLINE") != std::string::npos;
  }

  bool
  ddl_has_prefer_outline (const std::string &ddl)
  {
    return ddl.find ("STORAGE PREFER_OUTLINE") != std::string::npos;
  }

  bool
  ddl_has_storage_default (const std::string &ddl)
  {
    return ddl.find ("STORAGE DEFAULT") != std::string::npos;
  }

  int
  get_oos_stats (const char *table_name, OOS_STATS_INFO *out_stats)
  {
    DB_OBJECT *cls = db_find_class (table_name);
    if (cls == nullptr)
      {
	return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
      }

    OID *class_oid = (OID *) db_identifier (cls);
    if (class_oid == nullptr)
      {
	return ER_FAILED;
      }

    return xoos_get_stats_by_class_oid (thread_get_thread_entry_info (), class_oid, out_stats);
  }

  void
  expect_storage_attribute_error ()
  {
    const char *message = db_error_string (3);
    ASSERT_NE (message, nullptr);
    std::string error (message);
    EXPECT_TRUE (error.find ("STORAGE options can be set only on variable-type normal attributes")
		 != std::string::npos
		 || error.find ("only normal attributes can set storage options") != std::string::npos)
	<< message;
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

TEST_F (OosSqlStorage, CreateTablePersistsForceOutline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  id INT PRIMARY KEY,"
		     "  payload VARCHAR(4096) STORAGE FORCE_OUTLINE)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_force_outline (ddl)) << "DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "DDL was:\n" << ddl;
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

// STORAGE PREFER_OUTLINE and STORAGE DEFAULT are the current default policy, so
// both clauses must parse through the SQL path but neither persists a separate
// schema flag.
TEST_F (OosSqlStorage, ExplicitDefaultStorageClausesActAsDefault)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  id INT PRIMARY KEY,"
		     "  cold VARCHAR(4096) STORAGE PREFER_OUTLINE,"
		     "  warm VARCHAR(4096) STORAGE DEFAULT)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_outline (ddl)) << "DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_storage_default (ddl)) << "DDL was:\n" << ddl;
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

TEST_F (OosSqlStorage, CreateTableLikeCopiesForceOutline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  id INT PRIMARY KEY,"
		     "  payload VARCHAR(4096) STORAGE FORCE_OUTLINE)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_stg_like LIKE t_oos_stg");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg_like", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_force_outline (ddl)) << "cloned DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "cloned DDL was:\n" << ddl;
}

// STORAGE PREFER_INLINE is meaningful only for normal instance attributes; shared
// attributes are not stored in each heap record and must not persist a no-op policy.
TEST_F (OosSqlStorage, SharedAttributeRejectsPreferInline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (s VARCHAR(4096) SHARED 'x' STORAGE PREFER_INLINE)");
  EXPECT_LT (rc, 0);
  db_abort_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_stg (s VARCHAR(4096) SHARED 'x' STORAGE PREFER_OUTLINE)");
  EXPECT_LT (rc, 0);
  db_abort_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_stg (s VARCHAR(4096) SHARED 'x' STORAGE DEFAULT)");
  EXPECT_LT (rc, 0);
  db_abort_transaction ();
}

TEST_F (OosSqlStorage, CreateRejectsAllStorageSettingsForFixedTypes)
{
  for (const char *setting : storage_settings)
    {
      SCOPED_TRACE (setting);
      std::string sql = "CREATE TABLE t_oos_stg (c INT STORAGE ";
      sql += setting;
      sql += ")";

      int rc = exec_sql (sql.c_str ());
      EXPECT_LT (rc, 0);
      expect_storage_attribute_error ();
      db_abort_transaction ();
    }

  int rc = exec_sql ("CREATE TABLE t_oos_stg (c BIT(128) STORAGE PREFER_INLINE)");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
  db_abort_transaction ();
}

TEST_F (OosSqlStorage, CreateAcceptsStorageForPhysicalVariableTypes)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  c CHAR(32) STORAGE PREFER_INLINE,"
		     "  v BIT VARYING STORAGE FORCE_OUTLINE)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_prefer_inline (ddl)) << "DDL was:\n" << ddl;
  EXPECT_TRUE (ddl_has_force_outline (ddl)) << "DDL was:\n" << ddl;
}

TEST_F (OosSqlStorage, VclassRejectsAllStorageSettings)
{
  for (const char *setting : storage_settings)
    {
      SCOPED_TRACE (setting);
      std::string sql = "CREATE VCLASS t_oos_stg_v (c VARCHAR(4096) STORAGE ";
      sql += setting;
      sql += ")";

      int rc = exec_sql (sql.c_str ());
      EXPECT_LT (rc, 0);
      expect_storage_attribute_error ();
      db_abort_transaction ();
    }
}

TEST_F (OosSqlStorage, AlterRejectsAllStorageSettingsForFixedTypes)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (c INT)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  for (const char *setting : storage_settings)
    {
      SCOPED_TRACE (setting);
      std::string sql = "ALTER TABLE t_oos_stg MODIFY c INT STORAGE ";
      sql += setting;

      rc = exec_sql (sql.c_str ());
      EXPECT_LT (rc, 0);
      expect_storage_attribute_error ();
      db_abort_transaction ();
    }

  rc = exec_sql ("ALTER TABLE t_oos_stg CHANGE c c INT STORAGE PREFER_INLINE");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
  db_abort_transaction ();
}

TEST_F (OosSqlStorage, ForceOutlineRejectsUnsupportedAttributes)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (c INT STORAGE FORCE_OUTLINE)");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
  db_abort_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_stg (c VARCHAR(4096) SHARED 'x' STORAGE FORCE_OUTLINE)");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
  db_abort_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_stg CLASS ATTRIBUTE (c VARCHAR(4096) STORAGE FORCE_OUTLINE)");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
  db_abort_transaction ();

  rc = exec_sql ("CREATE TABLE t_oos_stg (c INT)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c INT STORAGE FORCE_OUTLINE");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
  db_abort_transaction ();

  rc = exec_sql ("CREATE VCLASS t_oos_stg_v (c VARCHAR(4096) STORAGE FORCE_OUTLINE)");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
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

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(8192)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_prefer_inline (ddl)) << "after omitted STORAGE clause, DDL was:\n" << ddl;

  // LOST: STORAGE PREFER_INLINE -> STORAGE DEFAULT
  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(8192) STORAGE DEFAULT");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "after MODIFY DEFAULT, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_outline (ddl)) << "after MODIFY DEFAULT, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_storage_default (ddl)) << "after MODIFY DEFAULT, DDL was:\n" << ddl;

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(4096) STORAGE PREFER_INLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (ddl_has_prefer_inline (ddl)) << "after second MODIFY PREFER_INLINE, DDL was:\n" << ddl;

  // LOST: STORAGE PREFER_INLINE -> STORAGE PREFER_OUTLINE
  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(4096) STORAGE PREFER_OUTLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "after MODIFY PREFER_OUTLINE, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_outline (ddl)) << "after MODIFY PREFER_OUTLINE, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_storage_default (ddl)) << "after MODIFY PREFER_OUTLINE, DDL was:\n" << ddl;
}

TEST_F (OosSqlStorage, AlterModifyPreservesAndTransitionsForceOutline)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (id INT PRIMARY KEY, c VARCHAR(4096) STORAGE FORCE_OUTLINE)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_TRUE (ddl_has_force_outline (ddl)) << "initial DDL was:\n" << ddl;

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(8192)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_force_outline (ddl)) << "after omitted STORAGE clause, DDL was:\n" << ddl;

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(8192) STORAGE PREFER_INLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_prefer_inline (ddl)) << "after MODIFY PREFER_INLINE, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_force_outline (ddl)) << "after MODIFY PREFER_INLINE, DDL was:\n" << ddl;

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(8192) STORAGE FORCE_OUTLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_force_outline (ddl)) << "after MODIFY FORCE_OUTLINE, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "after MODIFY FORCE_OUTLINE, DDL was:\n" << ddl;

  rc = exec_sql ("ALTER TABLE t_oos_stg CHANGE c c VARCHAR(8192) STORAGE PREFER_OUTLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_force_outline (ddl)) << "after CHANGE PREFER_OUTLINE, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "after CHANGE PREFER_OUTLINE, DDL was:\n" << ddl;

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(8192) STORAGE FORCE_OUTLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c VARCHAR(8192) STORAGE DEFAULT");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_force_outline (ddl)) << "after MODIFY DEFAULT, DDL was:\n" << ddl;
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "after MODIFY DEFAULT, DDL was:\n" << ddl;
}

TEST_F (OosSqlStorage, AlterModifyDropsForceOutlineForFixedType)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (c VARCHAR(4) STORAGE FORCE_OUTLINE)");
  ASSERT_GE (rc, 0);

  rc = exec_sql ("INSERT INTO t_oos_stg VALUES (1234)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c INT");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_force_outline (ddl)) << "after MODIFY to fixed type, DDL was:\n" << ddl;

  int value = 0;
  rc = fetch_single_int ("SELECT c FROM t_oos_stg", &value);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (value, 1234);
}

TEST_F (OosSqlStorage, AlterModifyDropsPreferInlineForFixedType)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (c VARCHAR(4) STORAGE PREFER_INLINE)");
  ASSERT_GE (rc, 0);

  rc = exec_sql ("INSERT INTO t_oos_stg VALUES ('1234')");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c INT");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (ddl_has_prefer_inline (ddl)) << "after MODIFY to fixed type, DDL was:\n" << ddl;

  int value = 0;
  rc = fetch_single_int ("SELECT c FROM t_oos_stg", &value);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (value, 1234);
}

TEST_F (OosSqlStorage, FailedExplicitFixedTypeAlterPreservesSchemaAndData)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (c VARCHAR(4) STORAGE PREFER_INLINE)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_stg VALUES ('1234')");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY c INT STORAGE DEFAULT");
  EXPECT_LT (rc, 0);
  expect_storage_attribute_error ();
  db_abort_transaction ();

  std::string ddl;
  rc = get_create_table_ddl ("t_oos_stg", ddl);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (ddl_has_prefer_inline (ddl)) << "after failed ALTER, DDL was:\n" << ddl;

  int value = 0;
  rc = fetch_single_int ("SELECT CAST(c AS INT) FROM t_oos_stg", &value);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (value, 1234);
}

TEST_F (OosSqlStorage, ForceOutlineBypassesRecordGateOnlyAboveInlineStubSize)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg ("
		     "  id INT PRIMARY KEY,"
		     "  payload VARCHAR(4096) STORAGE FORCE_OUTLINE)");
  ASSERT_GE (rc, 0);

  /* Packed VARCHAR includes its length prefix, terminator, and alignment: 14 characters occupy 16 bytes on disk,
   * while 15 characters occupy 20 bytes. */
  rc = exec_sql ("INSERT INTO t_oos_stg VALUES "
		 "(1, REPEAT('x', 3000)), (2, 'y'), (3, REPEAT('z', 14)), (4, REPEAT('w', 15)), (5, NULL)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  OOS_STATS_INFO stats;
  rc = get_oos_stats ("t_oos_stg", &stats);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (stats.has_oos_file, 1);
  EXPECT_EQ (stats.num_recs, 2);

  int length = 0;
  rc = fetch_single_int ("SELECT LENGTH(payload) FROM t_oos_stg WHERE id = 1", &length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (length, 3000);

  rc = fetch_single_int ("SELECT LENGTH(payload) FROM t_oos_stg WHERE id = 2", &length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (length, 1);

  rc = fetch_single_int ("SELECT LENGTH(payload) FROM t_oos_stg WHERE id = 3", &length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (length, 14);

  rc = fetch_single_int ("SELECT DISK_SIZE(payload) FROM t_oos_stg WHERE id = 3", &length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (length, 16);

  rc = fetch_single_int ("SELECT LENGTH(payload) FROM t_oos_stg WHERE id = 4", &length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (length, 15);

  rc = fetch_single_int ("SELECT DISK_SIZE(payload) FROM t_oos_stg WHERE id = 4", &length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (length, 16);

  int null_count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_stg WHERE id = 5 AND payload IS NULL", &null_count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (null_count, 1);
}

TEST_F (OosSqlStorage, AlterForceOutlineAppliesOnlyAfterRewrite)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (id INT PRIMARY KEY, payload VARCHAR(4096))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_stg VALUES (1, REPEAT('a', 3000))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  OOS_STATS_INFO stats;
  rc = get_oos_stats ("t_oos_stg", &stats);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (stats.has_oos_file, 0);

  rc = exec_sql ("ALTER TABLE t_oos_stg MODIFY payload VARCHAR(4096) STORAGE FORCE_OUTLINE");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_oos_stats ("t_oos_stg", &stats);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (stats.has_oos_file, 0);

  rc = exec_sql ("UPDATE t_oos_stg SET payload = CONCAT(payload, 'b') WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = get_oos_stats ("t_oos_stg", &stats);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (stats.has_oos_file, 1);
  EXPECT_EQ (stats.num_recs, 1);

  int length = 0;
  rc = fetch_single_int ("SELECT LENGTH(payload) FROM t_oos_stg WHERE id = 1", &length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (length, 3001);
}

TEST_F (OosSqlStorage, ForceOutlineRemainsNonReservedIdentifier)
{
  int rc = exec_sql ("CREATE TABLE t_oos_stg (force_outline INT)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_stg (force_outline) VALUES (7)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int value = 0;
  rc = fetch_single_int ("SELECT force_outline FROM t_oos_stg", &value);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (value, 7);
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
