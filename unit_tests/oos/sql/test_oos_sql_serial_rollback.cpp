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
 * test_oos_sql_serial_rollback.cpp - a rolled-back NEXT VALUE leaves a readable _db_serial row (CBRD-28073)
 *
 * NEXT VALUE rewrites the _db_serial row with a redo-only log record, so a rollback keeps the row. Any OOS chain
 * that write inserted with undo would be deleted by the rollback while the row still names it. The ALTER and
 * CREATE cases guard the same-transaction DDL path. A comment is at most 1024 bytes, so the row reaches OOS only
 * on small pages; this binary shares oosserialdb, created with 4K pages.
 */

#include "test_oos_sql_heap_fixture.hpp"

#include <random>
#include <string>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  const char *SERIAL_DB_NAME = "oosserialdb";

  /* CUBRID compresses long strings, so a repetitive comment would stay inline. */
  std::string
  incompressible_comment (size_t length)
  {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::mt19937 gen (28052);
    std::uniform_int_distribution<size_t> pick (0, sizeof (alphabet) - 2);
    std::string comment;

    for (size_t i = 0; i < length; i++)
      {
	comment.push_back (alphabet[pick (gen)]);
      }
    return comment;
  }

  const char *const SERIAL_NAMES[] = { "s_first", "s_again", "s_alter", "s_create", "s_cached" };

  void
  create_serial (const char *name, const std::string &comment)
  {
    std::string sql = std::string ("CREATE SERIAL ") + name + " NOCACHE COMMENT '" + comment + "'";
    ASSERT_GE (exec_sql (sql.c_str ()), 0);
  }

  void
  next_values (const char *name, int count)
  {
    std::string sql = std::string ("SELECT CAST (") + name + ".NEXT_VALUE AS INT)";
    for (int i = 0; i < count; i++)
      {
	int value = 0;
	ASSERT_EQ (fetch_single_int (sql.c_str (), &value), NO_ERROR);
      }
  }

  void
  expect_serial_row (const char *name, const std::string &comment, int current_val)
  {
    std::string where = std::string (" FROM _db_serial WHERE name = '") + name + "'";
    std::string sql = "SELECT IF (comment = '" + comment + "', 1, 0)" + where;
    int intact = 0;
    ASSERT_EQ (fetch_single_int (sql.c_str (), &intact), NO_ERROR) << "the row names an OOS chain that is gone";
    EXPECT_EQ (intact, 1) << "the comment read back through its OOS chain differs";

    int value = 0;
    sql = "SELECT CAST (current_val AS INT)" + where;
    ASSERT_EQ (fetch_single_int (sql.c_str (), &value), NO_ERROR);
    EXPECT_EQ (value, current_val);
  }

  void
  expect_next_value (const char *name, int expected)
  {
    std::string sql = std::string ("SELECT CAST (") + name + ".NEXT_VALUE AS INT)";
    int value = 0;
    ASSERT_EQ (fetch_single_int (sql.c_str (), &value), NO_ERROR);
    EXPECT_EQ (value, expected) << "a rollback must not hand out the values it issued again";
    db_commit_transaction ();
  }
}

class OosSqlSerialRollback : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      drop_serials ();
    }

    void TearDown () override
    {
      db_abort_transaction ();
      drop_serials ();
    }

  private:
    static void drop_serials ()
    {
      for (const char *name : SERIAL_NAMES)
	{
	  std::string sql = std::string ("DROP SERIAL IF EXISTS ") + name;
	  exec_sql (sql.c_str ());
	}
      db_commit_transaction ();
    }
};

/* CREATE SERIAL stores the comment inline, and the first NEXT VALUE once moved it to a new OOS chain. */
TEST_F (OosSqlSerialRollback, RolledBackFirstNextValueKeepsTheCommentReadable)
{
  const std::string comment = incompressible_comment (1000);
  ASSERT_NO_FATAL_FAILURE (create_serial ("s_first", comment));
  db_commit_transaction ();

  ASSERT_NO_FATAL_FAILURE (next_values ("s_first", 3));
  db_abort_transaction ();

  ASSERT_NO_FATAL_FAILURE (expect_serial_row ("s_first", comment, 3));
  ASSERT_NO_FATAL_FAILURE (expect_next_value ("s_first", 4));
}

TEST_F (OosSqlSerialRollback, RolledBackLaterNextValueKeepsTheCommentReadable)
{
  const std::string comment = incompressible_comment (1000);
  ASSERT_NO_FATAL_FAILURE (create_serial ("s_again", comment));
  db_commit_transaction ();
  ASSERT_NO_FATAL_FAILURE (next_values ("s_again", 1));
  db_commit_transaction ();

  ASSERT_NO_FATAL_FAILURE (next_values ("s_again", 5));
  db_abort_transaction ();

  ASSERT_NO_FATAL_FAILURE (expect_serial_row ("s_again", comment, 6));
  ASSERT_NO_FATAL_FAILURE (expect_next_value ("s_again", 7));
}

TEST_F (OosSqlSerialRollback, RolledBackAlterAndNextValueRestoreTheCommittedRow)
{
  const std::string comment = incompressible_comment (1000);
  ASSERT_NO_FATAL_FAILURE (create_serial ("s_alter", comment));
  db_commit_transaction ();
  ASSERT_NO_FATAL_FAILURE (next_values ("s_alter", 1));
  db_commit_transaction ();

  ASSERT_GE (exec_sql ("ALTER SERIAL s_alter INCREMENT BY 10"), 0);
  ASSERT_NO_FATAL_FAILURE (next_values ("s_alter", 3));
  db_abort_transaction ();

  ASSERT_NO_FATAL_FAILURE (expect_serial_row ("s_alter", comment, 1));
}

TEST_F (OosSqlSerialRollback, RolledBackCreateAndNextValueLeaveNoSerial)
{
  const std::string comment = incompressible_comment (1000);
  ASSERT_NO_FATAL_FAILURE (create_serial ("s_create", comment));
  ASSERT_NO_FATAL_FAILURE (next_values ("s_create", 3));
  db_abort_transaction ();

  int count = -1;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT (*) FROM _db_serial WHERE name = 's_create'", &count), NO_ERROR);
  EXPECT_EQ (count, 0);
}

/* Shutdown writes the cached serial back on the system transaction, which cannot open a topop. */
TEST_F (OosSqlSerialRollback, ShutdownWritesBackACachedSerial)
{
  const std::string comment = incompressible_comment (1000);
  std::string sql = "CREATE SERIAL s_cached CACHE 10 COMMENT '" + comment + "'";
  ASSERT_GE (exec_sql (sql.c_str ()), 0);
  db_commit_transaction ();
  ASSERT_NO_FATAL_FAILURE (next_values ("s_cached", 1));
  db_commit_transaction ();

  ASSERT_EQ (db_shutdown (), NO_ERROR);
  db_set_client_type (DB_CLIENT_TYPE_MAX);
  ASSERT_EQ (db_login ("dba", NULL), NO_ERROR);
  ASSERT_EQ (db_restart ("unit_test", TRUE, SERIAL_DB_NAME), NO_ERROR);

  ASSERT_NO_FATAL_FAILURE (expect_serial_row ("s_cached", comment, 1));
  ASSERT_NO_FATAL_FAILURE (expect_next_value ("s_cached", 2));
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  db_login ("dba", NULL);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv (SERIAL_DB_NAME));
  return RUN_ALL_TESTS ();
}
