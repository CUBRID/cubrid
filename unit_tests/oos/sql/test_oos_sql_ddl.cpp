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

#include <random>
#include <string>

#include "network_interface_cl.h"
#include "server_interface.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// Random alphanumeric text. LZ4 cannot shrink it, so its length on disk stays above the OOS inline target.
static std::string
incompressible_text (size_t length, unsigned int seed)
{
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::mt19937 rng (seed);
  std::uniform_int_distribution<size_t> pick (0, sizeof (alphabet) - 2);
  std::string text (length, ' ');
  for (char &c : text)
    {
      c = alphabet[pick (rng)];
    }
  return text;
}

// Run checkdb (all checks except the previous-link check) on one table: heap, B-tree, and the
// heap-to-B-tree entry check that rebuilds every row's key. Returns NO_ERROR if the table is consistent.
static int
checkdb_table (const char *table_name)
{
  DB_OBJECT *cls = db_find_class (table_name);
  if (cls == NULL)
    {
      return ER_FAILED;
    }
  OID class_oid = *db_identifier (cls);
  return boot_check_db_consistency (CHECKDB_ALL_CHECK_EXCEPT_PREV_LINK, &class_oid, 1, NULL);
}

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

// ============================================================================
// CBRD-28254: multi-column key of a row written before DROP COLUMN
//
// A composite index key is built in a buffer sized from recdes->length plus the length of each OOS
// key value. The OOS lengths must be read through the representation the row was written with. The
// index attributes locate a column in the current representation's variable offset table, and a row
// written before DROP COLUMN can keep that column at another position. Reading the wrong field
// undersized the buffer, and the full OOS value then overflowed the caller's stack buffer (one OOS
// column) or a too-small heap buffer (two OOS columns).
//
// The dropped column must precede the indexed OOS column in the old row's variable offset table.
// With this engine that holds when the dropped column is declared after the indexed one:
// (id, c, v) with v dropped. The order (id, v, c) leaves c at the same position and does not
// reproduce the defect.
// ============================================================================
class OosSqlDdlDropColumnKey : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_ddl_key");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_ddl_key");
      db_commit_transaction ();
    }

    // Create the table and index, insert row id = 1 with the given values (an OOS key column), and
    // drop column v. The row keeps the representation it was written with.
    void make_old_representation_row (const char *create_sql, const char *index_sql, const std::string &values)
    {
      ASSERT_GE (exec_sql (create_sql), 0);
      ASSERT_GE (exec_sql (index_sql), 0);
      db_commit_transaction ();

      std::string insert_sql = "INSERT INTO t_oos_ddl_key VALUES (1, " + values + ", 'small')";
      ASSERT_EQ (exec_sql (insert_sql.c_str ()), 1);
      db_commit_transaction ();
      ASSERT_GT (get_oos_page_count ("t_oos_ddl_key"), 0);

      ASSERT_GE (exec_sql ("ALTER TABLE t_oos_ddl_key DROP COLUMN v"), 0);
      db_commit_transaction ();

      // checkdb rebuilds the old row's key: it reported a false inconsistency (-544) before the fix.
      ASSERT_EQ (checkdb_table ("t_oos_ddl_key"), NO_ERROR);
    }

    void expect_deleted_and_consistent ()
    {
      ASSERT_EQ (exec_sql ("DELETE FROM t_oos_ddl_key WHERE id = 1"), 1);
      db_commit_transaction ();

      int count = -1;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_ddl_key", &count), NO_ERROR);
      EXPECT_EQ (count, 0);
      EXPECT_EQ (checkdb_table ("t_oos_ddl_key"), NO_ERROR);
    }
};

TEST_F (OosSqlDdlDropColumnKey, DeleteCompositeIndexKey)
{
  const std::string c = incompressible_text (5000, 1);
  const char *create_sql = "CREATE TABLE t_oos_ddl_key (id INT, c VARCHAR(8000), v VARCHAR(100))";
  const char *index_sql = "CREATE INDEX i_oos_ddl_key ON t_oos_ddl_key (c, id)";
  ASSERT_NO_FATAL_FAILURE (make_old_representation_row (create_sql, index_sql, "'" + c + "'"));
  ASSERT_NO_FATAL_FAILURE (expect_deleted_and_consistent ());
}

TEST_F (OosSqlDdlDropColumnKey, UpdateCompositeIndexKey)
{
  const std::string c = incompressible_text (5000, 2);
  const char *create_sql = "CREATE TABLE t_oos_ddl_key (id INT, c VARCHAR(8000), v VARCHAR(100))";
  const char *index_sql = "CREATE INDEX i_oos_ddl_key ON t_oos_ddl_key (c, id)";
  ASSERT_NO_FATAL_FAILURE (make_old_representation_row (create_sql, index_sql, "'" + c + "'"));

  // The update removes the old row's key and inserts the new one.
  ASSERT_EQ (exec_sql ("UPDATE t_oos_ddl_key SET id = 2 WHERE id = 1"), 1);
  db_commit_transaction ();

  int len = -1;
  ASSERT_EQ (fetch_single_int ("SELECT LENGTH(c) FROM t_oos_ddl_key WHERE id = 2", &len), NO_ERROR);
  EXPECT_EQ (len, 5000);

  int count = -1;
  std::string by_key = "SELECT COUNT(*) FROM t_oos_ddl_key WHERE c = '" + c + "' AND id = 2";
  ASSERT_EQ (fetch_single_int (by_key.c_str (), &count), NO_ERROR);
  EXPECT_EQ (count, 1);
  EXPECT_EQ (checkdb_table ("t_oos_ddl_key"), NO_ERROR);

  ASSERT_EQ (exec_sql ("DELETE FROM t_oos_ddl_key WHERE id = 2"), 1);
  db_commit_transaction ();
  EXPECT_EQ (checkdb_table ("t_oos_ddl_key"), NO_ERROR);
}

TEST_F (OosSqlDdlDropColumnKey, DeleteUniqueCompositeIndexKey)
{
  const std::string c = incompressible_text (5000, 3);
  const char *create_sql = "CREATE TABLE t_oos_ddl_key (id INT, c VARCHAR(8000), v VARCHAR(100))";
  const char *index_sql = "CREATE UNIQUE INDEX u_oos_ddl_key ON t_oos_ddl_key (c, id)";
  ASSERT_NO_FATAL_FAILURE (make_old_representation_row (create_sql, index_sql, "'" + c + "'"));
  ASSERT_NO_FATAL_FAILURE (expect_deleted_and_consistent ());
}

// Two OOS columns: the estimate read b's stub instead of c's and allocated a heap buffer for 5000
// bytes, then wrote the 15000-byte key into it.
TEST_F (OosSqlDdlDropColumnKey, DeleteCompositeIndexKeyHeapBuffer)
{
  const std::string c = incompressible_text (15000, 4);
  const std::string b = incompressible_text (5000, 5);
  const char *create_sql = "CREATE TABLE t_oos_ddl_key (id INT, c VARCHAR(30000), b VARCHAR(8000), v VARCHAR(100))";
  const char *index_sql = "CREATE INDEX i_oos_ddl_key ON t_oos_ddl_key (c, id)";
  ASSERT_NO_FATAL_FAILURE (make_old_representation_row (create_sql, index_sql, "'" + c + "', '" + b + "'"));
  ASSERT_NO_FATAL_FAILURE (expect_deleted_and_consistent ());
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
