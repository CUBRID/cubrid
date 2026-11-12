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
 * test_oos_sql_serial.cpp - OOS chains of _db_serial rows across NEXT VALUE (CBRD-28073)
 *
 * NEXT VALUE rewrites the _db_serial row outside heap_update. A comment is at most 1024 bytes, so the
 * row reaches OOS only on small pages; this binary owns oosserialdb, created with 4K pages.
 */

#include "test_oos_sql_heap_fixture.hpp"

#include <random>
#include <string>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  const char *SERIAL_DB_NAME = "oosserialdb";

  int
  serial_oos_record_count ()
  {
    DB_QUERY_RESULT *result = nullptr;
    int num_recs = -1;

    if (show_heap_oos_query ("SHOW HEAP OOS OF _db_serial", &result) != NO_ERROR)
      {
	return -1;
      }
    if (get_int_column (result, COL_OOS_NUM_RECS, &num_recs) != NO_ERROR)
      {
	num_recs = -1;
      }
    db_query_end (result);
    return num_recs;
  }

  /* CUBRID compresses long strings, so a repetitive comment would stay inline. */
  std::string
  incompressible_comment (size_t length)
  {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::mt19937 gen (28035);
    std::uniform_int_distribution<size_t> pick (0, sizeof (alphabet) - 2);
    std::string comment;

    for (size_t i = 0; i < length; i++)
      {
	comment.push_back (alphabet[pick (gen)]);
      }
    return comment;
  }

  void
  next_values (int count)
  {
    for (int i = 0; i < count; i++)
      {
	int value = 0;
	ASSERT_EQ (fetch_single_int ("SELECT CAST (s_oos_serial.NEXT_VALUE AS INT)", &value), NO_ERROR);
	db_commit_transaction ();
      }
  }

  void
  expect_serial_row (const std::string &comment, int current_val)
  {
    int value = 0;
    ASSERT_EQ (fetch_single_int ("SELECT CAST (current_val AS INT) FROM _db_serial WHERE name = 's_oos_serial'",
				 &value), NO_ERROR);
    EXPECT_EQ (value, current_val);

    std::string sql = "SELECT IF (comment = '" + comment + "', 1, 0) FROM _db_serial WHERE name = 's_oos_serial'";
    int intact = 0;
    ASSERT_EQ (fetch_single_int (sql.c_str (), &intact), NO_ERROR);
    EXPECT_EQ (intact, 1) << "the comment read back through its OOS chain differs";
  }
}

class OosSqlSerial : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP SERIAL IF EXISTS s_oos_serial");
      db_commit_transaction ();
    }

    void TearDown () override
    {
      exec_sql ("DROP SERIAL IF EXISTS s_oos_serial");
      db_commit_transaction ();
    }
};

TEST_F (OosSqlSerial, NextValueWritesNoCommentChain)
{
  const std::string comment = incompressible_comment (1000);
  std::string create = "CREATE SERIAL s_oos_serial NOCACHE COMMENT '" + comment + "'";
  ASSERT_GE (exec_sql (create.c_str ()), 0);
  db_commit_transaction ();

  const int created = serial_oos_record_count ();
  ASSERT_LE (created, 1);
  ASSERT_NO_FATAL_FAILURE (next_values (1));
  ASSERT_EQ (serial_oos_record_count (), created) << "NEXT VALUE wrote a comment chain";

  ASSERT_NO_FATAL_FAILURE (next_values (49));
  EXPECT_EQ (serial_oos_record_count (), created) << "each NEXT VALUE left the replaced comment chain behind";
  ASSERT_NO_FATAL_FAILURE (expect_serial_row (comment, 50));

  ASSERT_EQ (db_shutdown (), NO_ERROR);
  db_set_client_type (DB_CLIENT_TYPE_MAX);
  ASSERT_EQ (db_login ("dba", NULL), NO_ERROR);
  ASSERT_EQ (db_restart ("unit_test", TRUE, SERIAL_DB_NAME), NO_ERROR);

  EXPECT_EQ (serial_oos_record_count (), created);
  ASSERT_NO_FATAL_FAILURE (expect_serial_row (comment, 50));
  ASSERT_NO_FATAL_FAILURE (next_values (1));
  ASSERT_NO_FATAL_FAILURE (expect_serial_row (comment, 51));
  EXPECT_EQ (serial_oos_record_count (), created);
}

/* NUMERIC is stored with as many bytes as its value needs, so the row changes size as current_val grows. */
TEST_F (OosSqlSerial, NextValueThatWidensCurrentValKeepsTheRow)
{
  const std::string comment = incompressible_comment (1000);
  std::string create = "CREATE SERIAL s_oos_serial START WITH 250 NOCACHE COMMENT '" + comment + "'";
  ASSERT_GE (exec_sql (create.c_str ()), 0);
  db_commit_transaction ();
  const int created = serial_oos_record_count ();

  ASSERT_NO_FATAL_FAILURE (next_values (10));
  ASSERT_NO_FATAL_FAILURE (expect_serial_row (comment, 259));
  EXPECT_EQ (serial_oos_record_count (), created);
}

TEST_F (OosSqlSerial, CachedBlockThatWidensCurrentValKeepsTheRow)
{
  const std::string comment = incompressible_comment (1000);
  std::string create = "CREATE SERIAL s_oos_serial CACHE 300 COMMENT '" + comment + "'";
  ASSERT_GE (exec_sql (create.c_str ()), 0);
  db_commit_transaction ();
  const int created = serial_oos_record_count ();

  ASSERT_NO_FATAL_FAILURE (next_values (1));
  ASSERT_NO_FATAL_FAILURE (expect_serial_row (comment, 300));
  EXPECT_EQ (serial_oos_record_count (), created);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  /* SHOW HEAP OOS OF _db_serial needs DBA */
  db_login ("dba", NULL);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv (SERIAL_DB_NAME));
  return RUN_ALL_TESTS ();
}
