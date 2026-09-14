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
 * test_oos_sql_show.cpp - SHOW HEAP OOS diagnostic SQL tests (CBRD-26972)
 */

#include <algorithm>
#include <string>

#include "partition_sr.h"
#include "locator_sr.h"
#include "heap_oos.hpp"
#include "log_impl.h"
#include "record_descriptor.hpp"
#include "object_primitive.h"
#include "test_oos_sql_common.hpp"

// Direct server interfaces in SA must use server allocation, just like network_interface_cl.c.
extern unsigned int db_on_server;
void bridge_heap_attrinfo_fail_after_oos_publication_reset_once ();
void bridge_heap_attrinfo_disarm_publication_reset_failure ();

namespace
{
  class scoped_sa_server
  {
    public:
      scoped_sa_server ()
      {
	db_on_server++;
      }
      ~scoped_sa_server ()
      {
	db_on_server--;
      }
  };

  enum show_heap_oos_column
  {
    COL_TABLE_NAME = 0,
    COL_CLASS_OID,
    COL_HEAP_VOLUME_ID,
    COL_HEAP_FILE_ID,
    COL_HEAP_HEADER_PAGE_ID,
    COL_HAS_OOS_FILE,
    COL_OOS_VOLUME_ID,
    COL_OOS_FILE_ID,
    COL_OOS_NUM_USER_PAGES,
    COL_OOS_PAGE_SIZE,
    COL_OOS_NUM_RECS,
    COL_OOS_RECS_SUMLEN,
    COL_OOS_PHYSICAL_BYTES,
    COL_OOS_UNUSED_BYTES
  };

  static int
  show_heap_oos_query (const char *sql, DB_QUERY_RESULT **result)
  {
    int rc = exec_sql_with_result (sql, result);
    if (rc < 0)
      {
	return rc;
      }
    if (*result == nullptr)
      {
	return ER_FAILED;
      }

    rc = db_query_first_tuple (*result);
    if (rc != DB_CURSOR_SUCCESS)
      {
	db_query_end (*result);
	*result = nullptr;
	return ER_FAILED;
      }

    return NO_ERROR;
  }

  static int
  get_int_column (DB_QUERY_RESULT *result, int column, int *out_val)
  {
    DB_VALUE val;
    int rc;

    db_make_null (&val);
    rc = db_query_get_tuple_value (result, column, &val);
    if (rc != NO_ERROR)
      {
	return rc;
      }

    DB_TYPE type = db_value_type (&val);
    if (type == DB_TYPE_INTEGER)
      {
	*out_val = db_get_int (&val);
      }
    else if (type == DB_TYPE_BIGINT)
      {
	*out_val = (int) db_get_bigint (&val);
      }
    else if (type == DB_TYPE_SHORT)
      {
	*out_val = (int) db_get_short (&val);
      }
    else
      {
	rc = ER_FAILED;
      }

    db_value_clear (&val);
    return rc;
  }

  static int
  get_bigint_column (DB_QUERY_RESULT *result, int column, DB_BIGINT *out_val)
  {
    DB_VALUE val;
    int rc;

    db_make_null (&val);
    rc = db_query_get_tuple_value (result, column, &val);
    if (rc != NO_ERROR)
      {
	return rc;
      }

    DB_TYPE type = db_value_type (&val);
    if (type == DB_TYPE_BIGINT)
      {
	*out_val = db_get_bigint (&val);
      }
    else if (type == DB_TYPE_INTEGER)
      {
	*out_val = (DB_BIGINT) db_get_int (&val);
      }
    else if (type == DB_TYPE_SHORT)
      {
	*out_val = (DB_BIGINT) db_get_short (&val);
      }
    else
      {
	rc = ER_FAILED;
      }

    db_value_clear (&val);
    return rc;
  }

  static int
  get_is_null_column (DB_QUERY_RESULT *result, int column, bool *out_is_null)
  {
    DB_VALUE val;
    int rc;

    db_make_null (&val);
    rc = db_query_get_tuple_value (result, column, &val);
    if (rc == NO_ERROR)
      {
	*out_is_null = DB_IS_NULL (&val);
      }

    db_value_clear (&val);
    return rc;
  }

  static int
  get_string_column (DB_QUERY_RESULT *result, int column, std::string *out_val)
  {
    DB_VALUE val;
    int rc;

    db_make_null (&val);
    rc = db_query_get_tuple_value (result, column, &val);
    if (rc == NO_ERROR)
      {
	const char *str = db_get_string (&val);
	if (str == nullptr)
	  {
	    rc = ER_FAILED;
	  }
	else
	  {
	    *out_val = str;
	  }
      }

    db_value_clear (&val);
    return rc;
  }

  static std::string
  unqualified_table_name (const std::string &table_name)
  {
    std::string::size_type separator = table_name.rfind ('.');
    return separator == std::string::npos ? table_name : table_name.substr (separator + 1);
  }

  static void
  expect_sql_count (const char *sql, int expected)
  {
    SCOPED_TRACE (sql);
    int count = -1;
    ASSERT_EQ (fetch_single_int (sql, &count), NO_ERROR);
    EXPECT_EQ (count, expected);
  }

  static void
  expect_oos_records (const char *table, int has_file, int expected_records)
  {
    SCOPED_TRACE (table);
    std::string sql = std::string ("SHOW HEAP OOS OF ") + table;
    DB_QUERY_RESULT *result = nullptr;
    ASSERT_EQ (show_heap_oos_query (sql.c_str (), &result), NO_ERROR);
    int actual_has_file = -1;
    int actual_records = -1;
    EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &actual_has_file), NO_ERROR);
    EXPECT_EQ (get_int_column (result, COL_OOS_NUM_RECS, &actual_records), NO_ERROR);
    EXPECT_EQ (actual_has_file, has_file);
    EXPECT_EQ (actual_records, expected_records);
    EXPECT_EQ (db_query_next_tuple (result), DB_CURSOR_END);
    db_query_end (result);
  }
}

class OosSqlShow : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_no");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_yes");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_part");
      db_commit_transaction ();
    }

    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_no");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_yes");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_part");
      db_commit_transaction ();
    }
};

TEST_F (OosSqlShow, HeapWithoutOosReportsZeroStats)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_no (id INT PRIMARY KEY, data_col INT)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_no VALUES (1, 10)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  DB_QUERY_RESULT *result = nullptr;
  rc = show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_no", &result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (result, nullptr);

  int int_val = -1;
  bool is_null = false;
  DB_BIGINT bigint_val = -1;

  rc = get_int_column (result, COL_HAS_OOS_FILE, &int_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (int_val, 0);

  rc = get_is_null_column (result, COL_OOS_VOLUME_ID, &is_null);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (is_null);

  rc = get_is_null_column (result, COL_OOS_FILE_ID, &is_null);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_TRUE (is_null);

  rc = get_int_column (result, COL_OOS_NUM_USER_PAGES, &int_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (int_val, 0);

  rc = get_int_column (result, COL_OOS_PAGE_SIZE, &int_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (int_val, DB_PAGESIZE);

  rc = get_int_column (result, COL_OOS_NUM_RECS, &int_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (int_val, 0);

  rc = get_bigint_column (result, COL_OOS_PHYSICAL_BYTES, &bigint_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (bigint_val, 0);

  rc = get_bigint_column (result, COL_OOS_UNUSED_BYTES, &bigint_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (bigint_val, 0);

  db_query_end (result);
}

TEST_F (OosSqlShow, HeapWithOosReportsPositiveStats)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_yes VALUES (1, REPEAT(X'AA', 8192))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  DB_QUERY_RESULT *result = nullptr;
  rc = show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (result, nullptr);

  int has_oos = 0;
  int num_pages = 0;
  int page_size = 0;
  int num_recs = 0;
  DB_BIGINT recs_sumlen = 0;
  DB_BIGINT physical_bytes = 0;
  DB_BIGINT unused_bytes = 0;
  bool is_null = true;

  rc = get_int_column (result, COL_HAS_OOS_FILE, &has_oos);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (has_oos, 1);

  rc = get_is_null_column (result, COL_OOS_VOLUME_ID, &is_null);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (is_null);

  rc = get_is_null_column (result, COL_OOS_FILE_ID, &is_null);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_FALSE (is_null);

  rc = get_int_column (result, COL_OOS_NUM_USER_PAGES, &num_pages);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (num_pages, 0);

  rc = get_int_column (result, COL_OOS_PAGE_SIZE, &page_size);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (page_size, 0);

  rc = get_int_column (result, COL_OOS_NUM_RECS, &num_recs);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (num_recs, 0);

  rc = get_bigint_column (result, COL_OOS_RECS_SUMLEN, &recs_sumlen);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (recs_sumlen, 0);

  rc = get_bigint_column (result, COL_OOS_PHYSICAL_BYTES, &physical_bytes);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (physical_bytes, (DB_BIGINT) num_pages * (DB_BIGINT) page_size);

  rc = get_bigint_column (result, COL_OOS_UNUSED_BYTES, &unused_bytes);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (unused_bytes, std::max ((DB_BIGINT) 0, physical_bytes - recs_sumlen));

  db_query_end (result);
}

TEST_F (OosSqlShow, ShowAllHeapOosRunsForNonPartitionedClass)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_yes VALUES (1, REPEAT(X'BB', 8192))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  DB_QUERY_RESULT *result = nullptr;
  rc = show_heap_oos_query ("SHOW ALL HEAP OOS OF t_oos_show_yes", &result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (result, nullptr);

  int has_oos = 0;
  rc = get_int_column (result, COL_HAS_OOS_FILE, &has_oos);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (has_oos, 1);

  db_query_end (result);
}

TEST_F (OosSqlShow, ShowAllHeapOosReportsPartitionRows)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_part (id INT, data_col BIT VARYING) "
		     "PARTITION BY RANGE (id) ("
		     "PARTITION p0 VALUES LESS THAN (10), "
		     "PARTITION p1 VALUES LESS THAN MAXVALUE)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_part VALUES (1, REPEAT(X'CC', 8192))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_part VALUES (11, REPEAT(X'DD', 8192))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  DB_QUERY_RESULT *result = nullptr;
  rc = show_heap_oos_query ("SHOW ALL HEAP OOS OF t_oos_show_part", &result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (result, nullptr);

  int row_count = 0;
  int oos_heap_count = 0;
  do
    {
      int has_oos = 0;
      rc = get_int_column (result, COL_HAS_OOS_FILE, &has_oos);
      ASSERT_EQ (rc, NO_ERROR);

      row_count++;
      if (has_oos == 1)
	{
	  oos_heap_count++;
	}
    }
  while ((rc = db_query_next_tuple (result)) == DB_CURSOR_SUCCESS);

  EXPECT_EQ (rc, DB_CURSOR_END);
  EXPECT_GT (row_count, 1);
  EXPECT_GE (oos_heap_count, 1);

  db_query_end (result);
}

TEST_F (OosSqlShow, PartitionedForceOutlineStoresOosInPrunedHeap)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_part ("
		     "id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		     "PARTITION BY RANGE (id) ("
		     "PARTITION p0 VALUES LESS THAN (10), "
		     "PARTITION p1 VALUES LESS THAN MAXVALUE)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_part VALUES (1, REPEAT(X'EE', 64))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int value_matches = 0;
  rc = fetch_single_int ("SELECT data_col = CAST(REPEAT(X'EE', 64) AS BIT VARYING) "
			 "FROM t_oos_show_part WHERE id = 1", &value_matches);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (value_matches, 1);

  DB_QUERY_RESULT *result = nullptr;
  rc = show_heap_oos_query ("SHOW ALL HEAP OOS OF t_oos_show_part", &result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (result, nullptr);

  bool saw_root = false;
  bool saw_p0 = false;
  bool saw_p1 = false;
  do
    {
      std::string table_name;
      int has_oos = -1;
      int num_recs = -1;

      rc = get_string_column (result, COL_TABLE_NAME, &table_name);
      ASSERT_EQ (rc, NO_ERROR);
      rc = get_int_column (result, COL_HAS_OOS_FILE, &has_oos);
      ASSERT_EQ (rc, NO_ERROR);
      rc = get_int_column (result, COL_OOS_NUM_RECS, &num_recs);
      ASSERT_EQ (rc, NO_ERROR);

      table_name = unqualified_table_name (table_name);
      if (table_name == "t_oos_show_part")
	{
	  saw_root = true;
	  EXPECT_EQ (has_oos, 0);
	  EXPECT_EQ (num_recs, 0);
	}
      else if (table_name == "t_oos_show_part__p__p0")
	{
	  saw_p0 = true;
	  EXPECT_EQ (has_oos, 1);
	  EXPECT_EQ (num_recs, 1);
	}
      else if (table_name == "t_oos_show_part__p__p1")
	{
	  saw_p1 = true;
	  EXPECT_EQ (has_oos, 0);
	  EXPECT_EQ (num_recs, 0);
	}
    }
  while ((rc = db_query_next_tuple (result)) == DB_CURSOR_SUCCESS);

  EXPECT_EQ (rc, DB_CURSOR_END);
  EXPECT_TRUE (saw_root);
  EXPECT_TRUE (saw_p0);
  EXPECT_TRUE (saw_p1);

  db_query_end (result);
}

TEST_F (OosSqlShow, PartitionRangeBoundaryAndNullOwnership)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY RANGE (id) ("
		       "PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  // Alternating destinations and NULL in one statement exercise reusable routing state.
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(10, REPEAT(X'AA', 64)), (9, REPEAT(X'BB', 64)), "
		       "(11, REPEAT(X'CC', 64)), (NULL, REPEAT(X'DD', 64))"), 4);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 4);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
		    "(id = 9 AND data_col = CAST(REPEAT(X'BB', 64) AS BIT VARYING)) OR "
		    "(id IS NULL AND data_col = CAST(REPEAT(X'DD', 64) AS BIT VARYING))", 2);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "(id = 10 AND data_col = CAST(REPEAT(X'AA', 64) AS BIT VARYING)) OR "
		    "(id = 11 AND data_col = CAST(REPEAT(X'CC', 64) AS BIT VARYING))", 2);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
}

TEST_F (OosSqlShow, PartitionListExpressionAndFailedBatchOwnership)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY LIST (ABS(id)) ("
		       "PARTITION p0 VALUES IN (1, 3), "
		       "PARTITION p1 VALUES IN (2, NULL))"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(-1, REPEAT(X'AA', 64)), (-2, REPEAT(X'BB', 64)), "
		       "(-3, REPEAT(X'CC', 64)), (NULL, REPEAT(X'DD', 64))"), 4);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  // A valid first row followed by a missing destination must leave no durable partial write.
  EXPECT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(-1, REPEAT(X'EE', 64)), (4, REPEAT(X'FF', 64))"), ER_PARTITION_NOT_EXIST);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 4);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);

  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES (-2, REPEAT(X'EE', 64))"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 5);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
		    "(id = -1 AND data_col = CAST(REPEAT(X'AA', 64) AS BIT VARYING)) OR "
		    "(id = -3 AND data_col = CAST(REPEAT(X'CC', 64) AS BIT VARYING))", 2);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "(id = -2 AND data_col = CAST(REPEAT(X'BB', 64) AS BIT VARYING)) OR "
		    "(id IS NULL AND data_col = CAST(REPEAT(X'DD', 64) AS BIT VARYING)) OR "
		    "(id = -2 AND data_col = CAST(REPEAT(X'EE', 64) AS BIT VARYING))", 3);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 3);
}

TEST_F (OosSqlShow, PartitionHashNullOwnership)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY HASH (id) PARTITIONS 2"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(1, REPEAT(X'AA', 64)), (0, REPEAT(X'BB', 64)), "
		       "(1, REPEAT(X'CC', 64)), (NULL, REPEAT(X'DD', 64))"), 4);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 4);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
		    "(id = 0 AND data_col = CAST(REPEAT(X'BB', 64) AS BIT VARYING)) OR "
		    "(id IS NULL AND data_col = CAST(REPEAT(X'DD', 64) AS BIT VARYING))", 2);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "(id = 1 AND data_col = CAST(REPEAT(X'AA', 64) AS BIT VARYING)) OR "
		    "(id = 1 AND data_col = CAST(REPEAT(X'CC', 64) AS BIT VARYING))", 2);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
}

TEST_F (OosSqlShow, PartitionRangeExpressionValidationAndMovement)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY RANGE (id + 1) ("
		       "PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN (20))"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part__p__p0 VALUES (8, REPEAT(X'AA', 64))"), 1);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES (9, REPEAT(X'BB', 64))"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  EXPECT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES (19, REPEAT(X'CC', 64))"), ER_PARTITION_NOT_EXIST);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  EXPECT_EQ (exec_sql ("INSERT INTO t_oos_show_part__p__p0 VALUES (9, REPEAT(X'CC', 64))"),
	     ER_INVALID_DATA_FOR_PARTITION);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  EXPECT_EQ (exec_sql ("UPDATE t_oos_show_part__p__p0 SET id = 9"), ER_INVALID_DATA_FOR_PARTITION);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  EXPECT_EQ (exec_sql ("UPDATE t_oos_show_part SET id = 19 WHERE id = 8"), ER_PARTITION_NOT_EXIST);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);

  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 2);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
		    "id = 8 AND data_col = CAST(REPEAT(X'AA', 64) AS BIT VARYING)", 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "id = 9 AND data_col = CAST(REPEAT(X'BB', 64) AS BIT VARYING)", 1);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 1);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 1);

  // Root-targeted UPDATE may move a row; the sibling row stays in its existing heap.
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET id = id + 1"), 2);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 2);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0", 0);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "(id = 9 AND data_col = CAST(REPEAT(X'AA', 64) AS BIT VARYING)) OR "
		    "(id = 10 AND data_col = CAST(REPEAT(X'BB', 64) AS BIT VARYING))", 2);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
}

TEST_F (OosSqlShow, PartitionListRejectsNullWithoutDestination)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY LIST (id) ("
		       "PARTITION p0 VALUES IN (1), PARTITION p1 VALUES IN (2))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  EXPECT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES (NULL, REPEAT(X'AA', 64))"),
	     ER_PARTITION_NOT_EXIST);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 0);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);

  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES (2, REPEAT(X'BB', 64))"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "id = 2 AND data_col = CAST(REPEAT(X'BB', 64) AS BIT VARYING)", 1);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 1);
}

TEST_F (OosSqlShow, PartitionUpdatePreservesDuplicateProbesAndNonKeyIncrement)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT PRIMARY KEY, counter INT DEFAULT 0, "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY RANGE(id) "
		       "(PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES(11,0,REPEAT(X'AA',64))"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_EQ (exec_sql ("SELECT INCR(counter) FROM t_oos_show_part WHERE id=11"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=11 AND counter=1", 1);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(11,0,REPEAT(X'BB',64)) "
		       "ON DUPLICATE KEY UPDATE id=9,data_col=REPEAT(X'CC',64)"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=9 AND counter=1 "
		    "AND data_col=CAST(REPEAT(X'CC',64) AS BIT VARYING)", 1);
  ASSERT_GE (exec_sql ("REPLACE INTO t_oos_show_part VALUES(9,2,REPEAT(X'DD',64))"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=9 AND counter=2 "
		    "AND data_col=CAST(REPEAT(X'DD',64) AS BIT VARYING)", 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 1);
  expect_oos_records ("t_oos_show_part", 0, 0);
}

TEST_F (OosSqlShow, PartitionUpdateStringDomains)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id VARCHAR(128) COLLATE utf8_en_ci, "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY LIST(id) "
		       "(PARTITION p0 VALUES IN('alpha'), PARTITION p1 VALUES IN('beta'))"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES('ALPHA',REPEAT(X'AA',64))"), 1);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET data_col=REPEAT(X'BB',64)"), 1);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET id='BETA'"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id='beta'", 1);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 1);
  ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_part"), 0);
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id VARCHAR(4000), "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY RANGE(CHAR_LENGTH(id)) "
		       "(PARTITION p0 VALUES LESS THAN(3000), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES(REPEAT('a',2999),REPEAT(X'AA',64))"), 1);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET data_col=REPEAT(X'BB',64)"), 1);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET id=REPEAT('b',3000)"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=REPEAT('b',3000) "
		    "AND data_col=CAST(REPEAT(X'BB',64) AS BIT VARYING)", 1);
  expect_oos_records ("t_oos_show_part", 0, 0);
  // The compressed DEFAULT-policy key remains inline; only the forced VARBIT owns an OOS chunk.
  expect_oos_records ("t_oos_show_part__p__p1", 1, 1);
}

TEST_F (OosSqlShow, PartitionUpdateLegalKeysAndNullMovement)
{
  const char *columns[] =
  {
    "SMALLINT DEFAULT 11", "INTEGER DEFAULT 11", "BIGINT DEFAULT 2147483648",
    "DATE DEFAULT DATE '2024-02-29'", "TIME DEFAULT TIME '12:34:56'",
    "TIMESTAMP DEFAULT TIMESTAMP '2024-02-29 12:34:56'",
    "TIMESTAMPTZ DEFAULT TIMESTAMPTZ '2024-02-29 12:34:56 +09:00'",
    "TIMESTAMPLTZ DEFAULT TIMESTAMPLTZ '2024-02-29 12:34:56 +09:00'",
    "DATETIME DEFAULT DATETIME '2024-02-29 12:34:56.789'",
    "DATETIMETZ DEFAULT DATETIMETZ '2024-02-29 12:34:56.789 +09:00'",
    "DATETIMELTZ DEFAULT DATETIMELTZ '2024-02-29 12:34:56.789 +09:00'",
    "CHAR(8) DEFAULT 'b'", "VARCHAR(128) DEFAULT '한글 partition key'"
  };
  for (const char *column : columns)
    {
      SCOPED_TRACE (column);
      ASSERT_GE (exec_sql ("DROP TABLE IF EXISTS t_oos_show_part"), 0);
      std::string spec = column;
      std::string bound = spec.substr (spec.find (" DEFAULT ") + 9);
      if (spec.find ("CHAR(8)") == 0)
	{
	  bound = "'b       '";
	}
      std::string ddl = std::string ("CREATE TABLE t_oos_show_part (id ") + column
			+ ", data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY LIST(id) "
			+ "(PARTITION p0 VALUES IN (" + bound + "), PARTITION p1 VALUES IN(NULL))";
      ASSERT_GE (exec_sql (ddl.c_str ()), 0);
      ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part(data_col) VALUES(REPEAT(X'AA',64))"), 1);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET data_col=REPEAT(X'BB',64)"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
			"id IS NOT NULL AND data_col=CAST(REPEAT(X'BB',64) AS BIT VARYING)", 1);
      ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET id=NULL"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
			"id IS NULL AND data_col=CAST(REPEAT(X'BB',64) AS BIT VARYING)", 1);
      expect_oos_records ("t_oos_show_part", 0, 0);
      expect_oos_records ("t_oos_show_part__p__p1", 1, 1);
      std::string assignment = std::string ("UPDATE t_oos_show_part SET id=") + bound;
      ASSERT_EQ (exec_sql (assignment.c_str ()), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id IS NOT NULL", 1);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
			"data_col=CAST(REPEAT(X'AA',64) AS BIT VARYING)", 1);
    }
}

TEST_F (OosSqlShow, PartitionUpdateOldOosKeyAndRepresentation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id VARCHAR(128) STORAGE FORCE_OUTLINE, "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE, big_col BIT VARYING) "
		       "PARTITION BY RANGE(LOWER(SUBSTRING(id,1,1))) "
		       "(PARTITION p0 VALUES LESS THAN('b'), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "('A123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-', "
		       "REPEAT(X'AA',64), REPEAT(X'BB',6000))"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_part ADD COLUMN added INT DEFAULT 77"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET data_col=REPEAT(X'CC',64)"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE added=77 "
		    "AND data_col=CAST(REPEAT(X'CC',64) AS BIT VARYING)", 1);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part "
		       "SET id='B123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-'"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE added=77 "
		    "AND big_col=CAST(REPEAT(X'BB',6000) AS BIT VARYING)", 1);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 3);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE added=77 "
		    "AND data_col=CAST(REPEAT(X'AA',64) AS BIT VARYING)", 1);
}

TEST_F (OosSqlShow, PartitionUpdateLobLifecycle)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE, "
		       "text_lob CLOB, binary_lob BLOB STORAGE FORCE_OUTLINE) PARTITION BY RANGE(id) "
		       "(PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(9,REPEAT(X'AA',64),CHAR_TO_CLOB('old text'),BIT_TO_BLOB(X'AABB'))"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET id=id+1"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE CLOB_TO_CHAR(text_lob)='old text' "
		    "AND BLOB_TO_BIT(binary_lob)=X'AABB'", 1);
  expect_oos_records ("t_oos_show_part", 0, 0);
  // One forced VARBIT and one forced BLOB locator; the ordinary CLOB locator stays inline.
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
  ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET text_lob=CHAR_TO_CLOB('new text'), "
		       "binary_lob=BIT_TO_BLOB(X'CCDD')"), 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE CLOB_TO_CHAR(text_lob)='new text' "
		    "AND BLOB_TO_BIT(binary_lob)=X'CCDD'", 1);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE CLOB_TO_CHAR(text_lob)='old text' "
		    "AND BLOB_TO_BIT(binary_lob)=X'AABB'", 1);
}

TEST_F (OosSqlShow, PartitionUpdateDedicatedIncrementsAndArithmetic)
{
  struct integer_case
  {
    const char *type;
    const char *maximum;
    const char *minimum;
  };
  const integer_case cases[] =
  {
    { "SMALLINT", "32767", "-32768" },
    { "INTEGER", "2147483647", "-2147483648" },
    { "BIGINT", "9223372036854775807", "-9223372036854775808" }
  };
  for (const auto &key : cases)
    {
      SCOPED_TRACE (key.type);
      ASSERT_GE (exec_sql ("DROP TABLE IF EXISTS t_oos_show_part"), 0);
      std::string ddl = std::string ("CREATE TABLE t_oos_show_part (id ") + key.type
			+ ", data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY RANGE(id) "
			+ "(PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)";
      ASSERT_GE (exec_sql (ddl.c_str ()), 0);
      ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES (9,REPEAT(X'AA',64))"), 1);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_EQ (exec_sql ("SELECT INCR(id) FROM t_oos_show_part WHERE id=9"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=10 "
			"AND data_col=CAST(REPEAT(X'AA',64) AS BIT VARYING)", 1);
      expect_oos_records ("t_oos_show_part", 0, 0);
      expect_oos_records ("t_oos_show_part__p__p1", 1, 1);
      ASSERT_EQ (exec_sql ("SELECT DECR(id) FROM t_oos_show_part WHERE id=10"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=9", 1);
      ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET id=id+1"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=10", 1);
      ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET data_col=REPEAT(X'BB',64)"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=10 "
			"AND data_col=CAST(REPEAT(X'BB',64) AS BIT VARYING)", 1);
      std::string set_max = std::string ("UPDATE t_oos_show_part SET id=") + key.maximum;
      ASSERT_EQ (exec_sql (set_max.c_str ()), 1);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_EQ (exec_sql ("SELECT INCR(id) FROM t_oos_show_part"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=0", 1);
      std::string set_min = std::string ("UPDATE t_oos_show_part SET id=") + key.minimum;
      ASSERT_EQ (exec_sql (set_min.c_str ()), 1);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_EQ (exec_sql ("SELECT DECR(id) FROM t_oos_show_part"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=0", 1);
    }
}

TEST_F (OosSqlShow, EffectiveUpdateRouteUsesMissingHistoricalKeyDefault)
{
  // Retain a real serialized representation from before this class acquired its partition key.
  // No heap row is installed: this isolates the supplied-old-record contract from ALTER redistribution.
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (data_col INT DEFAULT 42)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  OID root_oid = *db_identifier (db_find_class ("t_oos_show_part"));
  std::string historical_bytes;
  RECDES historical = RECDES_INITIALIZER;
  REPR_ID historical_repr;
  {
    scoped_sa_server server_scope;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HEAP_CACHE_ATTRINFO old_values;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &old_values), NO_ERROR);
    historical_repr = old_values.last_classrepr->id;
    record_descriptor old_record (cubmem::CSTYLE_BLOCK_ALLOCATOR);
    bool demote = false;
    EXPECT_EQ (heap_attrinfo_transform_to_disk_probe_oos (thread_p, &old_values, NULL, &old_record,
	       LOB_FLAG_INCLUDE_LOB, &demote), S_SUCCESS);
    historical = old_record.get_recdes ();
    historical_bytes.assign (historical.data, historical.length);
    heap_attrinfo_end (thread_p, &old_values);
  }
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_part ADD COLUMN id INT DEFAULT 11"), 0);
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_part PARTITION BY RANGE(id) "
		       "(PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  OID expected = *db_identifier (db_find_class ("t_oos_show_part__p__p1"));
  historical.data = &historical_bytes[0];
  historical.area_size = historical.length;
  {
    scoped_sa_server server_scope;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HEAP_CACHE_ATTRINFO candidate, reference;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &candidate), NO_ERROR);
    ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &reference), NO_ERROR);
    EXPECT_NE (candidate.last_classrepr->id, historical_repr);
    PRUNING_CONTEXT context;
    ASSERT_EQ (partition_load_pruning_context (thread_p, &root_oid, DB_PARTITIONED_CLASS, &context), NO_ERROR);
    OID selected = OID_INITIALIZER, reference_oid = OID_INITIALIZER;
    HFID selected_hfid, reference_hfid;
    int error = partition_prune_update_by_attrinfo (thread_p, &root_oid, &candidate, &historical, &context,
		DB_PARTITIONED_CLASS, &selected, &selected_hfid, NULL);
    EXPECT_EQ (error, NO_ERROR);
    if (error == NO_ERROR)
      {
	EXPECT_TRUE (OID_EQ (&selected, &expected));
	for (int i = 0; i < candidate.num_values; i++)
	  {
	    EXPECT_EQ (candidate.values[i].state, HEAP_UNINIT_ATTRVALUE);
	    EXPECT_TRUE (DB_IS_NULL (&candidate.values[i].dbvalue));
	  }
	record_descriptor record (cubmem::CSTYLE_BLOCK_ALLOCATOR);
	bool demote = false;
	EXPECT_EQ (heap_attrinfo_transform_to_disk_probe_oos (thread_p, &reference, &historical, &record,
		   LOB_FLAG_INCLUDE_LOB, &demote), S_SUCCESS);
	RECDES reference_recdes = record.get_recdes ();
	EXPECT_EQ (partition_prune_update (thread_p, &root_oid, &reference_recdes, &context, DB_PARTITIONED_CLASS,
					   &reference_oid, &reference_hfid, NULL), NO_ERROR);
	EXPECT_TRUE (OID_EQ (&selected, &reference_oid));
	EXPECT_TRUE (HFID_EQ (&selected_hfid, &reference_hfid));
	partition_clear_pruning_context (&context);
      }
    heap_attrinfo_end (thread_p, &reference);
    heap_attrinfo_end (thread_p, &candidate);
  }
}

TEST_F (OosSqlShow, EffectiveUpdateRoutePreservesOldKeyAndPendingIncrement)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT DEFAULT 2, data_col BIT VARYING "
		       "STORAGE FORCE_OUTLINE) PARTITION BY RANGE (id) "
		       "(PARTITION p0 VALUES LESS THAN (10), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  DB_OBJECT *child = db_find_class ("t_oos_show_part__p__p1");
  ASSERT_NE (child, nullptr);
  OID child_oid = *db_identifier (child);
  ATTR_ID id = db_attribute_id (db_get_attribute (child, "id"));
  OID p0_oid = *db_identifier (db_find_class ("t_oos_show_part__p__p0"));
  {
    scoped_sa_server server_scope;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HEAP_CACHE_ATTRINFO old_values;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &child_oid, -1, NULL, &old_values), NO_ERROR);
    DB_VALUE old_key;
    db_make_int (&old_key, 10);
    EXPECT_EQ (heap_attrinfo_set (NULL, id, &old_key, &old_values), NO_ERROR);
    record_descriptor old_record (cubmem::CSTYLE_BLOCK_ALLOCATOR);
    bool demote = false;
    EXPECT_EQ (heap_attrinfo_transform_to_disk_probe_oos (thread_p, &old_values, NULL, &old_record,
	       LOB_FLAG_INCLUDE_LOB, &demote), S_SUCCESS);
    RECDES old_recdes = old_record.get_recdes ();
    // Unchanged, pending INCR, pending DECR, and an already evaluated ordinary assignment.
    for (int mode = 0; mode < 4; mode++)
      {
	SCOPED_TRACE (mode);
	HEAP_CACHE_ATTRINFO candidate;
	EXPECT_EQ (heap_attrinfo_start (thread_p, &child_oid, -1, NULL, &candidate), NO_ERROR);
	HEAP_ATTRVALUE *key = heap_attrvalue_locate (id, &candidate);
	key->do_increment = mode == 1 ? 1 : mode == 2 ? -1 : 0;
	if (mode == 3)
	  {
	    DB_VALUE assigned;
	    db_make_int (&assigned, 9);
	    EXPECT_EQ (heap_attrinfo_set (NULL, id, &assigned, &candidate), NO_ERROR);
	  }
	const auto state = key->state;
	auto *read_repr = candidate.read_classrepr;
	OID selected = OID_INITIALIZER;
	HFID selected_hfid;
	int error = partition_prune_update_by_attrinfo (thread_p, &child_oid, &candidate, &old_recdes, NULL,
		    DB_PARTITIONED_CLASS, &selected, &selected_hfid, NULL);
	EXPECT_EQ (error, NO_ERROR);
	if (error == NO_ERROR)
	  {
	    EXPECT_TRUE (OID_EQ (&selected, mode < 2 ? &child_oid : &p0_oid));
	  }
	EXPECT_EQ (candidate.read_classrepr, read_repr);
	EXPECT_EQ (key->state, state);
	EXPECT_EQ (key->do_increment, mode == 1 ? 1 : mode == 2 ? -1 : 0);
	if (mode == 3)
	  {
	    EXPECT_EQ (db_get_int (&key->dbvalue), 9);
	  }
	else
	  {
	    EXPECT_TRUE (DB_IS_NULL (&key->dbvalue));
	  }
	if (error == NO_ERROR)
	  {
	    // Repeating selection must not consume the pending operation. The first real transform must.
	    EXPECT_EQ (partition_prune_update_by_attrinfo (thread_p, &child_oid, &candidate, &old_recdes, NULL,
		       DB_PARTITIONED_CLASS, &selected, &selected_hfid, NULL), NO_ERROR);
	    record_descriptor actual_record (cubmem::CSTYLE_BLOCK_ALLOCATOR);
	    EXPECT_EQ (heap_attrinfo_transform_to_disk_with_oos_owner (thread_p, &candidate, &old_recdes,
		       &actual_record, LOB_FLAG_INCLUDE_LOB,
		       &selected), S_SUCCESS);
	    EXPECT_EQ (db_get_int (&key->dbvalue), mode == 0 ? 10 : mode == 1 ? 11 : 9);
	    OID final_oid = OID_INITIALIZER;
	    HFID final_hfid;
	    RECDES actual = actual_record.get_recdes ();
	    EXPECT_EQ (partition_prune_update (thread_p, &child_oid, &actual, NULL, DB_PARTITIONED_CLASS,
					       &final_oid, &final_hfid, NULL), NO_ERROR);
	    EXPECT_TRUE (OID_EQ (&selected, &final_oid));
	    EXPECT_TRUE (HFID_EQ (&selected_hfid, &final_hfid));
	  }
	heap_attrinfo_end (thread_p, &candidate);
      }
    heap_attrinfo_end (thread_p, &old_values);
  }
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
}

TEST_F (OosSqlShow, PartitionPreparationFailuresRollBackAndAllowNextWrite)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, "
		       "a BIT VARYING STORAGE FORCE_OUTLINE, b BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(1, REPEAT(X'AA',64), REPEAT(X'BB',64)), "
		       "(11, REPEAT(X'AA',64), REPEAT(X'BB',64))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);

  for (bool update :
       {
	       false, true
       })
    {
      for (int failure = 0; failure < 4; failure++)
	{
	  SCOPED_TRACE (failure);
	  SCOPED_TRACE (update ? "moving UPDATE" : "INSERT");
	  // A multi-chunk second value prevents both values publishing in one small-page batch.
	  if (failure == 0)
	    {
	      oos_test_fail_insert_many_after_publications (1);
	    }
	  else if (failure == 1)
	    {
	      bridge_heap_attrinfo_fail_after_oos_publication_reset_once ();
	    }
	  else if (failure == 2)
	    {
	      heap_oos_test_fail_before_vfid_lookup_once ();
	    }
	  else
	    {
	      oos_test_throw_bad_alloc_on_next_oid_publication ();
	    }
	  int error = exec_sql (update
				? "UPDATE t_oos_show_part SET id=12, a=REPEAT(X'CC',64), "
				"b=REPEAT(X'DD',20000) WHERE id=1"
				: "INSERT INTO t_oos_show_part VALUES (12, REPEAT(X'CC',64), REPEAT(X'DD',20000))");
	  oos_test_disarm_insert_publication_failures ();
	  bridge_heap_attrinfo_disarm_publication_reset_failure ();
	  heap_oos_test_disarm_fail_before_vfid_lookup ();
	  EXPECT_EQ (error, failure == 3 ? ER_OUT_OF_VIRTUAL_MEMORY : ER_GENERIC_ERROR);
	  ASSERT_EQ (db_abort_transaction (), NO_ERROR);

	  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part WHERE id IN (1,11) "
			    "AND a=CAST(REPEAT(X'AA',64) AS BIT VARYING) "
			    "AND b=CAST(REPEAT(X'BB',64) AS BIT VARYING)", 2);
	  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part WHERE id=12", 0);
	  expect_oos_records ("t_oos_show_part", 0, 0);
	  expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
	  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);

	  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES (12, REPEAT(X'CC',64), REPEAT(X'DD',64))"), 0);
	  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=12 "
			    "AND a=CAST(REPEAT(X'CC',64) AS BIT VARYING) "
			    "AND b=CAST(REPEAT(X'DD',64) AS BIT VARYING)", 1);
	  expect_oos_records ("t_oos_show_part__p__p1", 1, 4);
	  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
	  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
	}
    }
}

TEST_F (OosSqlShow, PartitionLobPreparationAndIndexFailuresPreserveCommittedValues)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT PRIMARY KEY, "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE, text_lob CLOB, "
		       "binary_lob BLOB STORAGE FORCE_OUTLINE) PARTITION BY RANGE(id) "
		       "(PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(1,REPEAT(X'AA',64),CHAR_TO_CLOB('old text'),BIT_TO_BLOB(X'AABB')), "
		       "(11,REPEAT(X'AA',64),CHAR_TO_CLOB('old text'),BIT_TO_BLOB(X'AABB'))"), 2);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  for (int failure = 0; failure < 3; failure++)
    {
      SCOPED_TRACE (failure);
      if (failure == 0)
	{
	  // Fail after OOS payload preparation, which copies the written BLOB locator.
	  heap_oos_test_fail_before_vfid_lookup_once ();
	}
      int error = exec_sql (failure == 0
			    ? "UPDATE t_oos_show_part SET id=12, text_lob=CHAR_TO_CLOB('new text'), "
			    "binary_lob=BIT_TO_BLOB(X'CCDD') WHERE id=1"
			    : failure == 1
			    ? "UPDATE t_oos_show_part SET id=11, text_lob=CHAR_TO_CLOB('new text'), "
			    "binary_lob=BIT_TO_BLOB(X'CCDD') WHERE id=1"
			    : "UPDATE t_oos_show_part SET id=11 WHERE id=1");
      heap_oos_test_disarm_fail_before_vfid_lookup ();
      EXPECT_EQ (error, failure == 0 ? ER_GENERIC_ERROR : ER_BTREE_UNIQUE_FAILED);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part WHERE id IN (1,11) "
			"AND CLOB_TO_CHAR(text_lob)='old text' AND BLOB_TO_BIT(binary_lob)=X'AABB'", 2);
      expect_oos_records ("t_oos_show_part", 0, 0);
      expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
      expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
      ASSERT_EQ (exec_sql ("UPDATE t_oos_show_part SET id=12, text_lob=CHAR_TO_CLOB('next text'), "
			   "binary_lob=BIT_TO_BLOB(X'EEFF') WHERE id=1"), 1);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=12 "
			"AND CLOB_TO_CHAR(text_lob)='next text' AND BLOB_TO_BIT(binary_lob)=X'EEFF'", 1);
      expect_oos_records ("t_oos_show_part__p__p1", 1, 4);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlShow, EffectiveKeyCodecFailureClearsOutputAndPreservesAssignment)
{
  // Fault only the codec on a private attribute/domain copy; never mutate cached schema metadata.
  class failing_codec : public PR_TYPE
  {
    public:
      failing_codec (const PR_TYPE &original, bool fail_read) : PR_TYPE (original)
      {
	if (fail_read)
	  {
	    f_data_readval = [] (struct or_buf *, DB_VALUE *out, TP_DOMAIN *, int, bool, char *, int)
	    {
	      DB_VALUE partial;
	      db_make_string (&partial, "partially decoded key");
	      pr_clone_value (&partial, out);
	      return ER_FAILED;
	    };
	  }
	else
	  {
	    f_data_writeval = [] (struct or_buf *, DB_VALUE *)
	    {
	      return ER_FAILED;
	    };
	  }
      }
  };
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id VARCHAR(512), data_col INT) "
		       "PARTITION BY RANGE(LENGTH(id)) (PARTITION p0 VALUES LESS THAN(10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  DB_OBJECT *root = db_find_class ("t_oos_show_part");
  ASSERT_NE (root, nullptr);
  OID root_oid = *db_identifier (root);
  DB_ATTRIBUTE *attribute = db_get_attribute (root, "id");
  ASSERT_NE (attribute, nullptr);
  ATTR_ID key_id = db_attribute_id (attribute);
  std::string text;
  unsigned int random = 12345;
  for (int i = 0; i < 300; i++)
    {
      random = random * 1664525U + 1013904223U;
      text.push_back ('!' + ((random >> 16) % 90));
    }
  {
    scoped_sa_server server_scope;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HEAP_CACHE_ATTRINFO cache;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &cache), NO_ERROR);
    DB_VALUE assigned;
    db_make_string (&assigned, text.c_str ());
    EXPECT_EQ (heap_attrinfo_set (NULL, key_id, &assigned, &cache), NO_ERROR);
    HEAP_ATTRVALUE *slot = heap_attrvalue_locate (key_id, &cache);
    OR_ATTRIBUTE *original = slot->last_attrepr;
    for (bool fail_read :
	 {
		 false, true
	 })
      {
	OR_ATTRIBUTE local_attribute {};
	local_attribute.id = original->id;
	local_attribute.type = original->type;
	local_attribute.is_fixed = original->is_fixed;
	TP_DOMAIN local_domain = *original->domain;
	failing_codec codec (*original->domain->type, fail_read);
	local_domain.type = &codec;
	local_attribute.domain = &local_domain;
	slot->last_attrepr = &local_attribute;
	DB_VALUE key;
	db_make_null (&key);
	int error = heap_attrinfo_get_effective_key (thread_p, &cache, key_id, NULL, &key);
	slot->last_attrepr = original;
	EXPECT_EQ (error, ER_FAILED);
	EXPECT_TRUE (DB_IS_NULL (&key));
	pr_clear_value (&key);
	EXPECT_EQ (slot->state, HEAP_WRITTEN_ATTRVALUE);
	EXPECT_EQ (std::string (db_get_string (&slot->dbvalue), db_get_string_size (&slot->dbvalue)), text);
	db_make_null (&key);
	EXPECT_EQ (heap_attrinfo_get_effective_key (thread_p, &cache, key_id, NULL, &key), NO_ERROR);
	if (!DB_IS_NULL (&key))
	  {
	    EXPECT_EQ (std::string (db_get_string (&key), db_get_string_size (&key)), text);
	  }
	pr_clear_value (&key);
      }
    heap_attrinfo_end (thread_p, &cache);
  }
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
}

TEST_F (OosSqlShow, EffectiveRoutingFailurePreservesAssignmentsAndPublication)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN (20))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  DB_OBJECT *root = db_find_class ("t_oos_show_part");
  ASSERT_NE (root, nullptr);
  OID root_oid = *db_identifier (root);
  ATTR_ID key_id = db_attribute_id (db_get_attribute (root, "id"));
  ATTR_ID payload_id = db_attribute_id (db_get_attribute (root, "data_col"));
  OID p0_oid = *db_identifier (db_find_class ("t_oos_show_part__p__p0"));
  OID p1_oid = *db_identifier (db_find_class ("t_oos_show_part__p__p1"));
  {
    scoped_sa_server server_scope;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HEAP_CACHE_ATTRINFO cache;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &cache), NO_ERROR);
    PRUNING_CONTEXT context;
    partition_init_pruning_context (&context);
    LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
    ASSERT_NE (tdes, nullptr);
    const LOG_LSA marker { 876543, 123 };
    thread_p->oos_oids.clear ();
    tdes->oos_insert_lsa_queue.clear ();
    thread_p->oos_oids.push_back (root_oid);
    tdes->oos_insert_lsa_queue.push (marker);
    for (int input :
	 {
		 11, 21, 1, 21, 11
	 })
      {
	DB_VALUE assigned;
	db_make_int (&assigned, input);
	EXPECT_EQ (heap_attrinfo_set (NULL, key_id, &assigned, &cache), NO_ERROR);
	OID destination = OID_INITIALIZER;
	HFID hfid;
	int error = partition_prune_insert_by_attrinfo (thread_p, &root_oid, &cache, &context,
		    DB_PARTITIONED_CLASS, &destination, &hfid, NULL);
	EXPECT_EQ (error, input == 21 ? ER_PARTITION_NOT_EXIST : NO_ERROR);
	if (error == NO_ERROR)
	  {
	    EXPECT_TRUE (OID_EQ (&destination, input < 10 ? &p0_oid : &p1_oid));
	  }
	EXPECT_EQ (db_get_int (&heap_attrvalue_locate (key_id, &cache)->dbvalue), input);
	EXPECT_EQ (heap_attrvalue_locate (key_id, &cache)->state, HEAP_WRITTEN_ATTRVALUE);
	EXPECT_EQ (heap_attrvalue_locate (payload_id, &cache)->state, HEAP_UNINIT_ATTRVALUE);
	EXPECT_EQ (thread_p->oos_oids.size (), 1U);
	if (!thread_p->oos_oids.empty ())
	  {
	    EXPECT_TRUE (OID_EQ (&thread_p->oos_oids.front (), &root_oid));
	  }
	EXPECT_EQ (tdes->oos_insert_lsa_queue.size (), 1U);
	if (!tdes->oos_insert_lsa_queue.is_empty ())
	  {
	    EXPECT_TRUE (LSA_EQ (&tdes->oos_insert_lsa_queue.front (), &marker));
	  }
	er_clear ();
      }
    EXPECT_EQ (heap_oos_begin_insert_publication (thread_p), S_SUCCESS);
    partition_clear_pruning_context (&context);
    heap_attrinfo_end (thread_p, &cache);
  }
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 0);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
}

TEST_F (OosSqlShow, EffectiveInsertRoutePreservesOmittedAssignments)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id INT DEFAULT 11, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  DB_OBJECT *root = db_find_class ("t_oos_show_part");
  ASSERT_NE (root, nullptr);
  OID root_oid = *db_identifier (root);
  DB_OBJECT *child = db_find_class ("t_oos_show_part__p__p1");
  ASSERT_NE (child, nullptr);
  OID expected_oid = *db_identifier (child);
  {
    scoped_sa_server server_scope;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HEAP_CACHE_ATTRINFO candidate;
    HEAP_CACHE_ATTRINFO reference;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &candidate), NO_ERROR);
    int reference_error = heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &reference);
    if (reference_error != NO_ERROR)
      {
	heap_attrinfo_end (thread_p, &candidate);
	FAIL () << reference_error;
      }

    OID selected_oid = OID_INITIALIZER, reference_oid = OID_INITIALIZER;
    HFID selected_hfid, reference_hfid;
    int error = partition_prune_insert_by_attrinfo (thread_p, &root_oid, &candidate, NULL, DB_PARTITIONED_CLASS,
		&selected_oid, &selected_hfid, NULL);
    EXPECT_EQ (error, NO_ERROR);
    if (error == NO_ERROR)
      {
	EXPECT_TRUE (OID_EQ (&selected_oid, &expected_oid));
      }
    for (int i = 0; i < candidate.num_values; i++)
      {
	EXPECT_EQ (candidate.values[i].state, HEAP_UNINIT_ATTRVALUE);
	EXPECT_TRUE (DB_IS_NULL (&candidate.values[i].dbvalue));
      }

    // The reference owns separate values: the inline probe may initialize and mutate them.
    record_descriptor record (cubmem::CSTYLE_BLOCK_ALLOCATOR);
    bool would_demote = false;
    SCAN_CODE scan = heap_attrinfo_transform_to_disk_probe_oos (thread_p, &reference, NULL, &record,
		     LOB_FLAG_INCLUDE_LOB, &would_demote);
    EXPECT_EQ (scan, S_SUCCESS);
    if (scan == S_SUCCESS)
      {
	RECDES recdes = record.get_recdes ();
	EXPECT_EQ (partition_prune_insert (thread_p, &root_oid, &recdes, NULL, NULL, DB_PARTITIONED_CLASS,
					   &reference_oid, &reference_hfid, NULL), NO_ERROR);
	EXPECT_TRUE (OID_EQ (&reference_oid, &expected_oid));
	if (error == NO_ERROR)
	  {
	    EXPECT_TRUE (OID_EQ (&selected_oid, &reference_oid));
	    EXPECT_TRUE (HFID_EQ (&selected_hfid, &reference_hfid));
	  }
      }
    heap_attrinfo_end (thread_p, &reference);
    heap_attrinfo_end (thread_p, &candidate);
  }
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 0);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
}

TEST_F (OosSqlShow, EffectiveInsertRoutePreservesAssignedChar)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id CHAR(8), data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY LIST (id) (PARTITION p0 VALUES IN ('a       '), "
		       "PARTITION p1 VALUES IN ('b       '))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  DB_OBJECT *root = db_find_class ("t_oos_show_part");
  ASSERT_NE (root, nullptr);
  OID root_oid = *db_identifier (root);
  DB_ATTRIBUTE *key_attribute = db_get_attribute (root, "id");
  ASSERT_NE (key_attribute, nullptr);
  ATTR_ID key_id = db_attribute_id (key_attribute);
  DB_OBJECT *child = db_find_class ("t_oos_show_part__p__p1");
  ASSERT_NE (child, nullptr);
  OID expected_oid = *db_identifier (child);
  {
    scoped_sa_server server_scope;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    HEAP_CACHE_ATTRINFO candidate;
    HEAP_CACHE_ATTRINFO reference;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &candidate), NO_ERROR);
    int error = heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &reference);
    if (error != NO_ERROR)
      {
	heap_attrinfo_end (thread_p, &candidate);
	FAIL () << error;
      }
    DB_VALUE assigned;
    db_make_string (&assigned, "b");
    EXPECT_EQ (heap_attrinfo_set (NULL, key_id, &assigned, &candidate), NO_ERROR);
    EXPECT_EQ (heap_attrinfo_set (NULL, key_id, &assigned, &reference), NO_ERROR);
    int key_index = candidate.values[0].attrid == key_id ? 0 : 1;
    const HEAP_ATTRVALUE &source = candidate.values[key_index];
    int source_size = db_get_string_size (&source.dbvalue);
    std::string source_bytes (db_get_string (&source.dbvalue), source_size);
    auto source_state = source.state;

    OID selected_oid = OID_INITIALIZER, reference_oid = OID_INITIALIZER;
    HFID selected_hfid, reference_hfid;
    error = partition_prune_insert_by_attrinfo (thread_p, &root_oid, &candidate, NULL, DB_PARTITIONED_CLASS,
	    &selected_oid, &selected_hfid, NULL);
    EXPECT_EQ (error, NO_ERROR);
    if (error == NO_ERROR)
      {
	EXPECT_TRUE (OID_EQ (&selected_oid, &expected_oid));
      }
    EXPECT_EQ (source.state, source_state);
    EXPECT_EQ (db_get_string_size (&source.dbvalue), source_size);
    EXPECT_EQ (std::string (db_get_string (&source.dbvalue), db_get_string_size (&source.dbvalue)), source_bytes);
    EXPECT_EQ (candidate.values[1 - key_index].state, HEAP_UNINIT_ATTRVALUE);

    record_descriptor record (cubmem::CSTYLE_BLOCK_ALLOCATOR);
    bool would_demote = false;
    SCAN_CODE scan = heap_attrinfo_transform_to_disk_probe_oos (thread_p, &reference, NULL, &record,
		     LOB_FLAG_INCLUDE_LOB, &would_demote);
    EXPECT_EQ (scan, S_SUCCESS);
    if (scan == S_SUCCESS)
      {
	RECDES recdes = record.get_recdes ();
	EXPECT_EQ (partition_prune_insert (thread_p, &root_oid, &recdes, NULL, NULL, DB_PARTITIONED_CLASS,
					   &reference_oid, &reference_hfid, NULL), NO_ERROR);
	EXPECT_TRUE (OID_EQ (&reference_oid, &expected_oid));
	if (error == NO_ERROR)
	  {
	    EXPECT_TRUE (OID_EQ (&selected_oid, &reference_oid));
	    EXPECT_TRUE (HFID_EQ (&selected_hfid, &reference_hfid));
	  }
      }
    heap_attrinfo_end (thread_p, &reference);
    heap_attrinfo_end (thread_p, &candidate);
  }
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
}

TEST_F (OosSqlShow, EffectiveInsertRouteLegalKeyDefaults)
{
  const char *columns[] =
  {
    "SMALLINT DEFAULT 11", "INTEGER DEFAULT 11", "BIGINT DEFAULT 2147483648",
    "DATE DEFAULT DATE '2024-02-29'", "TIME DEFAULT TIME '12:34:56'",
    "TIMESTAMP DEFAULT TIMESTAMP '2024-02-29 12:34:56'",
    "TIMESTAMPTZ DEFAULT TIMESTAMPTZ '2024-02-29 12:34:56 +09:00'",
    "TIMESTAMPLTZ DEFAULT TIMESTAMPLTZ '2024-02-29 12:34:56 +09:00'",
    "DATETIME DEFAULT DATETIME '2024-02-29 12:34:56.789'",
    "DATETIMETZ DEFAULT DATETIMETZ '2024-02-29 12:34:56.789 +09:00'",
    "DATETIMELTZ DEFAULT DATETIMELTZ '2024-02-29 12:34:56.789 +09:00'",
    "CHAR(8) DEFAULT 'b'", "VARCHAR(128) DEFAULT '한글 partition key'"
  };
  for (const char *column : columns)
    {
      SCOPED_TRACE (column);
      ASSERT_GE (exec_sql ("DROP TABLE IF EXISTS t_oos_show_part"), 0);
      std::string column_spec = column;
      std::string bound = column_spec.substr (column_spec.find (" DEFAULT ") + 9);
      if (column_spec.find ("CHAR(8)") == 0)
	{
	  bound = "'b       '";
	}
      std::string ddl = std::string ("CREATE TABLE t_oos_show_part (id ") + column
			+ ", data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY LIST (id) ("
			+ "PARTITION p0 VALUES IN (" + bound + "), PARTITION p1 VALUES IN (NULL))";
      ASSERT_GE (exec_sql (ddl.c_str ()), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      DB_OBJECT *root = db_find_class ("t_oos_show_part");
      ASSERT_NE (root, nullptr);
      OID root_oid = *db_identifier (root);
      {
	scoped_sa_server server_scope;
	THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
	HEAP_CACHE_ATTRINFO candidate;
	HEAP_CACHE_ATTRINFO reference;
	ASSERT_EQ (heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &candidate), NO_ERROR);
	int error = heap_attrinfo_start (thread_p, &root_oid, -1, NULL, &reference);
	if (error != NO_ERROR)
	  {
	    heap_attrinfo_end (thread_p, &candidate);
	    FAIL () << error;
	  }
	OID selected_oid = OID_INITIALIZER, reference_oid = OID_INITIALIZER;
	HFID selected_hfid, reference_hfid;
	error = partition_prune_insert_by_attrinfo (thread_p, &root_oid, &candidate, NULL, DB_PARTITIONED_CLASS,
		&selected_oid, &selected_hfid, NULL);
	EXPECT_EQ (error, NO_ERROR);
	for (int i = 0; i < candidate.num_values; i++)
	  {
	    EXPECT_EQ (candidate.values[i].state, HEAP_UNINIT_ATTRVALUE);
	    EXPECT_TRUE (DB_IS_NULL (&candidate.values[i].dbvalue));
	  }
	record_descriptor record (cubmem::CSTYLE_BLOCK_ALLOCATOR);
	bool would_demote = false;
	SCAN_CODE scan = heap_attrinfo_transform_to_disk_probe_oos (thread_p, &reference, NULL, &record,
			 LOB_FLAG_INCLUDE_LOB, &would_demote);
	EXPECT_EQ (scan, S_SUCCESS);
	if (scan == S_SUCCESS)
	  {
	    RECDES recdes = record.get_recdes ();
	    EXPECT_EQ (partition_prune_insert (thread_p, &root_oid, &recdes, NULL, NULL, DB_PARTITIONED_CLASS,
					       &reference_oid, &reference_hfid, NULL), NO_ERROR);
	    if (error == NO_ERROR)
	      {
		EXPECT_TRUE (OID_EQ (&selected_oid, &reference_oid));
		EXPECT_TRUE (HFID_EQ (&selected_hfid, &reference_hfid));
	      }
	  }
	heap_attrinfo_end (thread_p, &reference);
	heap_attrinfo_end (thread_p, &candidate);
      }
      expect_oos_records ("t_oos_show_part", 0, 0);
      expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
      expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
    }
}

TEST_F (OosSqlShow, PartitionInsertLegalKeySqlMatrix)
{
  struct key_case
  {
    const char *type;
    const char *value;
  };
  const key_case cases[] =
  {
    { "SMALLINT", "11" }, { "INTEGER", "11" }, { "BIGINT", "2147483648" },
    { "DATE", "DATE '2024-02-29'" }, { "TIME", "TIME '12:34:56'" },
    { "TIMESTAMP", "TIMESTAMP '2024-02-29 12:34:56'" },
    { "TIMESTAMPTZ", "TIMESTAMPTZ '2024-02-29 12:34:56 +09:00'" },
    { "TIMESTAMPLTZ", "TIMESTAMPLTZ '2024-02-29 12:34:56 +09:00'" },
    { "DATETIME", "DATETIME '2024-02-29 12:34:56.789'" },
    { "DATETIMETZ", "DATETIMETZ '2024-02-29 12:34:56.789 +09:00'" },
    { "DATETIMELTZ", "DATETIMELTZ '2024-02-29 12:34:56.789 +09:00'" },
    { "CHAR(8)", "'b'" }, { "VARCHAR(128)", "'한글 partition key'" }
  };
  for (const key_case &key : cases)
    {
      SCOPED_TRACE (key.type);
      ASSERT_GE (exec_sql ("DROP TABLE IF EXISTS t_oos_show_part"), 0);
      std::string ddl = std::string ("CREATE TABLE t_oos_show_part (id ") + key.type + " DEFAULT " + key.value
			+ ", data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY HASH (id) PARTITIONS 2";
      ASSERT_GE (exec_sql (ddl.c_str ()), 0);
      std::string insert = std::string ("INSERT INTO t_oos_show_part VALUES (") + key.value
			   + ", REPEAT(X'AA', 64)), (NULL, REPEAT(X'BB', 64)), (" + key.value
			   + ", REPEAT(X'CC', 64))";
      ASSERT_EQ (exec_sql (insert.c_str ()), 3);
      ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part (data_col) VALUES (REPEAT(X'DD', 64))"), 1);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      std::string matching = std::string ("SELECT COUNT(*) FROM t_oos_show_part WHERE id = CAST(") + key.value
			     + " AS " + key.type + ") AND data_col IN (CAST(REPEAT(X'AA', 64) AS BIT VARYING), "
			     + "CAST(REPEAT(X'CC', 64) AS BIT VARYING), CAST(REPEAT(X'DD', 64) AS BIT VARYING))";
      expect_sql_count (matching.c_str (), 3);
      expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id IS NULL "
			"AND data_col = CAST(REPEAT(X'BB', 64) AS BIT VARYING)", 1);
      int p0_count = -1, p1_count = -1;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0", &p0_count), NO_ERROR);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1", &p1_count), NO_ERROR);
      EXPECT_EQ (p0_count + p1_count, 4);
      // Retain literal partition observations for the independent pinned-library run.
      printf ("LEGAL_KEY %s p0=%d p1=%d\n", key.type, p0_count, p1_count);
      expect_oos_records ("t_oos_show_part", 0, 0);
      expect_oos_records ("t_oos_show_part__p__p0", p0_count > 0 ? 1 : 0, p0_count);
      expect_oos_records ("t_oos_show_part__p__p1", p1_count > 0 ? 1 : 0, p1_count);
    }
}

TEST_F (OosSqlShow, PartitionInsertOwnsExternalKeyAndMultiplePayloads)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "id VARCHAR(128) STORAGE FORCE_OUTLINE, "
		       "small_col BIT VARYING STORAGE FORCE_OUTLINE, large_col BIT VARYING) "
		       "PARTITION BY RANGE (LOWER(SUBSTRING(id, 1, 1))) ("
		       "PARTITION p0 VALUES LESS THAN ('b'), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "('A123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-', "
		       "REPEAT(X'AA',64), REPEAT(X'BB',6000)), "
		       "('B123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-', "
		       "REPEAT(X'CC',64), REPEAT(X'DD',6000))"), 2);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
		    "id = 'A123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-' AND "
		    "small_col = CAST(REPEAT(X'AA',64) AS BIT VARYING) AND "
		    "large_col = CAST(REPEAT(X'BB',6000) AS BIT VARYING)", 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "id = 'B123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-' AND "
		    "small_col = CAST(REPEAT(X'CC',64) AS BIT VARYING) AND "
		    "large_col = CAST(REPEAT(X'DD',6000) AS BIT VARYING)", 1);
  // The key and both payloads each fit one OOS chunk; the ordinary VARBIT cannot be compressed.
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 3);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 3);
}

TEST_F (OosSqlShow, PartitionInsertRetainsBigoneRejectionBeforeOos)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, fixed_col BIT(140000), "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY RANGE(id) ("
		       "PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  EXPECT_LT (exec_sql ("INSERT INTO t_oos_show_part VALUES(11,B'1',REPEAT(X'AA',64))"), 0);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_OVERPASS_MAXOBJ_SIZE);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part", 0);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
  // Without an OOS value the existing whole-record overflow path remains supported.
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES(11,B'1',NULL)"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=11 AND data_col IS NULL", 1);
  expect_oos_records ("t_oos_show_part__p__p1", 0, 0);
}

TEST_F (OosSqlShow, PartitionInsertGeneratedKeysAndDomainConversion)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT AUTO_INCREMENT(9,1), "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY RANGE(id) ("
		       "PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part(data_col) VALUES(REPEAT(X'AA',64)),(REPEAT(X'BB',64))"), 2);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES('11',REPEAT(X'CC',64))"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE "
		    "id=9 AND data_col=CAST(REPEAT(X'AA',64) AS BIT VARYING)", 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE "
		    "(id=10 AND data_col=CAST(REPEAT(X'BB',64) AS BIT VARYING)) OR "
		    "(id=11 AND data_col=CAST(REPEAT(X'CC',64) AS BIT VARYING))", 2);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 1);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
}

TEST_F (OosSqlShow, PartitionInsertDynamicDefault)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id DATE DEFAULT CURRENT_DATE, "
		       "expected_date DATE, data_col BIT VARYING STORAGE FORCE_OUTLINE) "
		       "PARTITION BY HASH(id) PARTITIONS 2"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part(expected_date,data_col) VALUES"
		       "(CURRENT_DATE,REPEAT(X'AA',64)),(CURRENT_DATE,REPEAT(X'BB',64))"), 2);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part WHERE id=expected_date AND "
		    "data_col IN (CAST(REPEAT(X'AA',64) AS BIT VARYING),CAST(REPEAT(X'BB',64) AS BIT VARYING))", 2);
  int p0_count = -1, p1_count = -1;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0", &p0_count), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1", &p1_count), NO_ERROR);
  EXPECT_EQ (p0_count + p1_count, 2);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", p0_count > 0 ? 1 : 0, p0_count);
  expect_oos_records ("t_oos_show_part__p__p1", p1_count > 0 ? 1 : 0, p1_count);
}

TEST_F (OosSqlShow, PartitionInsertUsesColumnCollation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id VARCHAR(20) COLLATE utf8_en_ci, "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY LIST(id) ("
		       "PARTITION p0 VALUES IN('alpha'), PARTITION p1 VALUES IN('beta'))"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES('BETA',REPEAT(X'AA',64)),"
		       "('AlPhA',REPEAT(X'BB',64)),('beta',REPEAT(X'CC',64))"), 3);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id='alpha' AND "
		    "data_col=CAST(REPEAT(X'BB',64) AS BIT VARYING)", 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id='beta' AND "
		    "data_col IN (CAST(REPEAT(X'AA',64) AS BIT VARYING),CAST(REPEAT(X'CC',64) AS BIT VARYING))", 2);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 1);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
}

TEST_F (OosSqlShow, PartitionInsertCompressedExpressionKey)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id VARCHAR(4096) STORAGE FORCE_OUTLINE, "
		       "data_col BIT VARYING STORAGE FORCE_OUTLINE) PARTITION BY RANGE(CHAR_LENGTH(id)) ("
		       "PARTITION p0 VALUES LESS THAN(3000), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_part VALUES(REPEAT('b',3000),REPEAT(X'AA',64)),"
		       "(REPEAT('a',2999),REPEAT(X'BB',64))"), 2);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=REPEAT('a',2999) "
		    "AND data_col=CAST(REPEAT(X'BB',64) AS BIT VARYING)", 1);
  expect_sql_count ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=REPEAT('b',3000) "
		    "AND data_col=CAST(REPEAT(X'AA',64) AS BIT VARYING)", 1);
  expect_oos_records ("t_oos_show_part", 0, 0);
  expect_oos_records ("t_oos_show_part__p__p0", 1, 2);
  expect_oos_records ("t_oos_show_part__p__p1", 1, 2);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  if (db_login ("DBA", NULL) != NO_ERROR)
    {
      fprintf (stderr, "db_login failed\n");
      return EXIT_FAILURE;
    }
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
