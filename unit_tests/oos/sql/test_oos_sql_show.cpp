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

#include "test_oos_sql_common.hpp"

namespace
{
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
    COL_OOS_FREE_BYTES,
    COL_OOS_NUM_PAGES_FREE_0_25,
    COL_OOS_NUM_PAGES_FREE_25_50,
    COL_OOS_NUM_PAGES_FREE_50_75,
    COL_OOS_NUM_PAGES_FREE_75_100,
    COL_OOS_NUM_EMPTY_PAGES,
    COL_OOS_NUM_PAGES_SKIPPED
  };

  enum show_heap_capacity_column
  {
    CAP_COL_TABLE_NAME = 0,
    CAP_COL_CLASS_OID,
    CAP_COL_VOLUME_ID,
    CAP_COL_FILE_ID,
    CAP_COL_HEADER_PAGE_ID,
    CAP_COL_NUM_RECS,
    CAP_COL_NUM_RELOCATED_RECS,
    CAP_COL_NUM_OVERFLOWED_RECS,
    CAP_COL_NUM_PAGES,
    CAP_COL_AVG_REC_LEN,
    CAP_COL_AVG_FREE_SPACE_PER_PAGE,
    CAP_COL_AVG_FREE_SPACE_PER_PAGE_EXCEPT_LAST_PAGE,
    CAP_COL_AVG_OVERHEAD_PER_PAGE,
    CAP_COL_REPR_ID,
    CAP_COL_NUM_TOTAL_ATTRS,
    CAP_COL_NUM_FIXED_WIDTH_ATTRS,
    CAP_COL_NUM_VARIABLE_WIDTH_ATTRS,
    CAP_COL_NUM_SHARED_ATTRS,
    CAP_COL_NUM_CLASS_ATTRS,
    CAP_COL_TOTAL_SIZE_FIXED_WIDTH_ATTRS,
    CAP_COL_HAS_OOS_FILE,
    CAP_COL_OOS_NUM_USER_PAGES,
    CAP_COL_OOS_NUM_RECS,
    CAP_COL_OOS_RECS_SUMLEN,
    CAP_COL_OOS_PHYSICAL_BYTES,
    CAP_COL_OOS_FREE_BYTES
  };

  static int
  show_heap_query (const char *sql, DB_QUERY_RESULT **result)
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
  rc = show_heap_query ("SHOW HEAP OOS OF t_oos_show_no", &result);
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

  rc = get_bigint_column (result, COL_OOS_FREE_BYTES, &bigint_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (bigint_val, 0);

  for (int column = COL_OOS_NUM_PAGES_FREE_0_25; column <= COL_OOS_NUM_PAGES_SKIPPED; column++)
    {
      rc = get_int_column (result, column, &int_val);
      ASSERT_EQ (rc, NO_ERROR);
      EXPECT_EQ (int_val, 0);
    }

  db_query_end (result);
}

TEST_F (OosSqlShow, HeapCapacityWithoutOosReportsZeroSummary)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_no (id INT PRIMARY KEY, data_col INT)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_no VALUES (1, 10)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  DB_QUERY_RESULT *result = nullptr;
  rc = show_heap_query ("SHOW HEAP CAPACITY OF t_oos_show_no", &result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (result, nullptr);

  int int_val = -1;
  DB_BIGINT bigint_val = -1;

  rc = get_int_column (result, CAP_COL_HAS_OOS_FILE, &int_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (int_val, 0);

  rc = get_int_column (result, CAP_COL_OOS_NUM_USER_PAGES, &int_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (int_val, 0);

  rc = get_int_column (result, CAP_COL_OOS_NUM_RECS, &int_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (int_val, 0);

  rc = get_bigint_column (result, CAP_COL_OOS_RECS_SUMLEN, &bigint_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (bigint_val, 0);

  rc = get_bigint_column (result, CAP_COL_OOS_PHYSICAL_BYTES, &bigint_val);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (bigint_val, 0);

  rc = get_bigint_column (result, CAP_COL_OOS_FREE_BYTES, &bigint_val);
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
  rc = show_heap_query ("SHOW HEAP OOS OF t_oos_show_yes", &result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (result, nullptr);

  int has_oos = 0;
  int num_pages = 0;
  int page_size = 0;
  int num_recs = 0;
  DB_BIGINT recs_sumlen = 0;
  DB_BIGINT physical_bytes = 0;
  DB_BIGINT free_bytes = 0;
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

  rc = get_bigint_column (result, COL_OOS_FREE_BYTES, &free_bytes);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (free_bytes, 0);
  EXPECT_LT (free_bytes, physical_bytes - recs_sumlen);

  int pages_free_0_25 = 0;
  int pages_free_25_50 = 0;
  int pages_free_50_75 = 0;
  int pages_free_75_100 = 0;
  int empty_pages = 0;
  int skipped_pages = 0;

  rc = get_int_column (result, COL_OOS_NUM_PAGES_FREE_0_25, &pages_free_0_25);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_int_column (result, COL_OOS_NUM_PAGES_FREE_25_50, &pages_free_25_50);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_int_column (result, COL_OOS_NUM_PAGES_FREE_50_75, &pages_free_50_75);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_int_column (result, COL_OOS_NUM_PAGES_FREE_75_100, &pages_free_75_100);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_int_column (result, COL_OOS_NUM_EMPTY_PAGES, &empty_pages);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_int_column (result, COL_OOS_NUM_PAGES_SKIPPED, &skipped_pages);
  ASSERT_EQ (rc, NO_ERROR);

  EXPECT_EQ (pages_free_0_25, 0);
  EXPECT_EQ (pages_free_25_50, 1);
  EXPECT_EQ (pages_free_50_75, 0);
  EXPECT_EQ (pages_free_75_100, 0);
  EXPECT_EQ (empty_pages, 0);
  EXPECT_EQ (skipped_pages, 0);
  EXPECT_EQ (pages_free_0_25 + pages_free_25_50 + pages_free_50_75 + pages_free_75_100 + empty_pages,
	     num_pages - 1);

  db_query_end (result);
}

TEST_F (OosSqlShow, HeapCapacityWithOosReportsSummary)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_yes VALUES (1, REPEAT(X'EE', 8192))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  DB_QUERY_RESULT *capacity_result = nullptr;
  rc = show_heap_query ("SHOW HEAP CAPACITY OF t_oos_show_yes", &capacity_result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (capacity_result, nullptr);

  int cap_has_oos = 0;
  int cap_num_pages = 0;
  int cap_num_recs = 0;
  DB_BIGINT cap_recs_sumlen = 0;
  DB_BIGINT cap_physical_bytes = 0;
  DB_BIGINT cap_free_bytes = 0;

  rc = get_int_column (capacity_result, CAP_COL_HAS_OOS_FILE, &cap_has_oos);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (cap_has_oos, 1);

  rc = get_int_column (capacity_result, CAP_COL_OOS_NUM_USER_PAGES, &cap_num_pages);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (cap_num_pages, 0);

  rc = get_int_column (capacity_result, CAP_COL_OOS_NUM_RECS, &cap_num_recs);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (cap_num_recs, 0);

  rc = get_bigint_column (capacity_result, CAP_COL_OOS_RECS_SUMLEN, &cap_recs_sumlen);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (cap_recs_sumlen, 0);

  rc = get_bigint_column (capacity_result, CAP_COL_OOS_PHYSICAL_BYTES, &cap_physical_bytes);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (cap_physical_bytes, (DB_BIGINT) cap_num_pages * (DB_BIGINT) DB_PAGESIZE);

  rc = get_bigint_column (capacity_result, CAP_COL_OOS_FREE_BYTES, &cap_free_bytes);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_GT (cap_free_bytes, 0);

  DB_QUERY_RESULT *oos_result = nullptr;
  rc = show_heap_query ("SHOW HEAP OOS OF t_oos_show_yes", &oos_result);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_NE (oos_result, nullptr);

  int oos_has_oos = 0;
  int oos_num_pages = 0;
  int oos_num_recs = 0;
  DB_BIGINT oos_recs_sumlen = 0;
  DB_BIGINT oos_physical_bytes = 0;
  DB_BIGINT oos_free_bytes = 0;

  rc = get_int_column (oos_result, COL_HAS_OOS_FILE, &oos_has_oos);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_int_column (oos_result, COL_OOS_NUM_USER_PAGES, &oos_num_pages);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_int_column (oos_result, COL_OOS_NUM_RECS, &oos_num_recs);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_bigint_column (oos_result, COL_OOS_RECS_SUMLEN, &oos_recs_sumlen);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_bigint_column (oos_result, COL_OOS_PHYSICAL_BYTES, &oos_physical_bytes);
  ASSERT_EQ (rc, NO_ERROR);
  rc = get_bigint_column (oos_result, COL_OOS_FREE_BYTES, &oos_free_bytes);
  ASSERT_EQ (rc, NO_ERROR);

  EXPECT_EQ (cap_has_oos, oos_has_oos);
  EXPECT_EQ (cap_num_pages, oos_num_pages);
  EXPECT_EQ (cap_num_recs, oos_num_recs);
  EXPECT_EQ (cap_recs_sumlen, oos_recs_sumlen);
  EXPECT_EQ (cap_physical_bytes, oos_physical_bytes);
  EXPECT_EQ (cap_free_bytes, oos_free_bytes);

  db_query_end (oos_result);
  db_query_end (capacity_result);
}

TEST_F (OosSqlShow, ShowAllHeapOosRunsForNonPartitionedClass)
{
  int rc = exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_show_yes VALUES (1, REPEAT(X'BB', 8192))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  DB_QUERY_RESULT *result = nullptr;
  rc = show_heap_query ("SHOW ALL HEAP OOS OF t_oos_show_yes", &result);
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
  rc = show_heap_query ("SHOW ALL HEAP OOS OF t_oos_show_part", &result);
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
