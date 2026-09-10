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
#include <utility>

#include "heap_prepared_row.hpp"
#include "heap_oos.hpp"

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

TEST_F (OosSqlShow, InsertOwnsOosInDestinationHeap)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, data_col BIT VARYING) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES (1, REPEAT(X'CC', 8192))"), 0);

  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part "
			       "WHERE id = 1 AND data_col = CAST(REPEAT(X'CC', 8192) AS BIT VARYING)",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);

  const char *queries[] = { "SHOW HEAP OOS OF t_oos_show_part",
			    "SHOW HEAP OOS OF t_oos_show_part__p__p0",
			    "SHOW HEAP OOS OF t_oos_show_part__p__p1"
			  };
  const int expected[] = { 0, 1, 0 };
  for (int i = 0; i < 3; ++i)
    {
      DB_QUERY_RESULT *result = nullptr;
      ASSERT_EQ (show_heap_oos_query (queries[i], &result), NO_ERROR);
      int has_oos = -1;
      EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
      EXPECT_EQ (has_oos, expected[i]) << queries[i];
      db_query_end (result);
    }
}

TEST_F (OosSqlShow, ForcedOutlineKeyRoutesFromPreparedBytes)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "k VARCHAR(80) DEFAULT 'abcdefghijklmnopqrst' STORAGE FORCE_OUTLINE, "
		       "payload BIT VARYING) PARTITION BY RANGE(k) ("
		       "PARTITION p0 VALUES LESS THAN ('m'), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part(payload) VALUES(REPEAT(X'AB', 40000))"), 0);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE k = 'abcdefghijklmnopqrst' "
			       "AND payload = CAST(REPEAT(X'AB', 40000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  DB_QUERY_RESULT *result = nullptr;
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part", &result), NO_ERROR);
  int has_oos = -1;
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 0);
  db_query_end (result);
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p0", &result), NO_ERROR);
  int chunks = 0;
  EXPECT_EQ (get_int_column (result, COL_OOS_NUM_RECS, &chunks), NO_ERROR);
  EXPECT_GT (chunks, 2);
  db_query_end (result);
}

TEST_F (OosSqlShow, RejectedDestinationCreatesNoOosAndNextInsertSucceeds)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, payload BIT VARYING) "
		       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  EXPECT_LT (exec_sql ("INSERT INTO t_oos_show_part VALUES(20, REPEAT(X'AB', 8192))"), 0);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  DB_QUERY_RESULT *result = nullptr;
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part", &result), NO_ERROR);
  int has_oos = -1;
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 0);
  db_query_end (result);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(NULL, REPEAT(X'CD', 8192))"), 0);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE id IS NULL "
			       "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p0", &result), NO_ERROR);
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 1);
  db_query_end (result);
}

TEST_F (OosSqlShow, LobPreparationPreservesSourceAndDestinationValues)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_no (c CLOB)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_no VALUES(CHAR_TO_CLOB('source clob value'))"), 0);
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, c CLOB STORAGE FORCE_OUTLINE) "
		       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10))"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part SELECT 1, c FROM t_oos_show_no"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_no WHERE CLOB_TO_CHAR(c) = 'source clob value'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE CLOB_TO_CHAR(c) = 'source clob value'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_no"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE CLOB_TO_CHAR(c) = 'source clob value'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  DB_QUERY_RESULT *result = nullptr;
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part", &result), NO_ERROR);
  int has_oos = -1;
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 0);
  db_query_end (result);
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p0", &result), NO_ERROR);
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 1);
  db_query_end (result);
}

TEST_F (OosSqlShow, SupportedPartitionDomainsPreserveValuesAndOwnership)
{
  struct domain_case
  {
    const char *type;
    const char *value;
    const char *boundary;
  };
  const domain_case cases[] =
  {
    { "SMALLINT", "1", "10" }, { "INTEGER", "1", "10" }, { "BIGINT", "1", "10" },
    { "DATE", "'2020-01-01'", "'2021-01-01'" },
    { "TIME", "'01:00:00'", "'02:00:00'" },
    { "TIMESTAMP", "'2020-01-01 01:00:00'", "'2021-01-01 01:00:00'" },
    { "TIMESTAMPTZ", "'2020-01-01 01:00:00 +00:00'", "'2021-01-01 01:00:00 +00:00'" },
    { "TIMESTAMPLTZ", "'2020-01-01 01:00:00 +00:00'", "'2021-01-01 01:00:00 +00:00'" },
    { "DATETIME", "'2020-01-01 01:00:00.123'", "'2021-01-01 01:00:00.123'" },
    { "DATETIMETZ", "'2020-01-01 01:00:00.123 +00:00'", "'2021-01-01 01:00:00.123 +00:00'" },
    { "DATETIMELTZ", "'2020-01-01 01:00:00.123 +00:00'", "'2021-01-01 01:00:00.123 +00:00'" },
    { "CHAR(40)", "'abcdefghijklmnopqrst'", "'m'" },
    { "VARCHAR(80)", "'abcdefghijklmnopqrst'", "'m'" }
  };
  for (const auto &entry : cases)
    {
      SCOPED_TRACE (entry.type);
      char sql[1024];
      snprintf (sql, sizeof (sql), "CREATE TABLE t_oos_show_part (k %s, payload BIT VARYING) "
		"PARTITION BY RANGE(k) (PARTITION p0 VALUES LESS THAN(%s), "
		"PARTITION p1 VALUES LESS THAN MAXVALUE)", entry.type, entry.boundary);
      ASSERT_GE (exec_sql (sql), 0) << db_error_string (1);
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_show_part VALUES(%s, REPEAT(X'AB', 8192))", entry.value);
      ASSERT_GE (exec_sql (sql), 0) << db_error_string (1);
      snprintf (sql, sizeof (sql), "SELECT COUNT(*) FROM t_oos_show_part WHERE k = CAST(%s AS %s) "
		"AND payload = CAST(REPEAT(X'AB', 8192) AS BIT VARYING)", entry.value, entry.type);
      int matches = 0;
      ASSERT_EQ (fetch_single_int (sql, &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      DB_QUERY_RESULT *result = nullptr;
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part", &result), NO_ERROR);
      int has_oos = -1;
      EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
      EXPECT_EQ (has_oos, 0);
      db_query_end (result);
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p0", &result), NO_ERROR);
      EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
      EXPECT_EQ (has_oos, 1);
      db_query_end (result);
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_part"), 0);
    }
}

TEST_F (OosSqlShow, MovedPreparationOutlivesAttributeCache)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT DEFAULT 7, "
		       "a VARCHAR(100) DEFAULT 'abcdefghijklmnopqrstuvwxyz' STORAGE FORCE_OUTLINE, "
		       "b VARCHAR(100) DEFAULT 'zyxwvutsrqponmlkjihgfedcba' STORAGE FORCE_OUTLINE)"), 0);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  ASSERT_NE (cls, nullptr);
  OID class_oid = *db_identifier (cls);
  HFID hfid;
  ASSERT_EQ (heap_get_class_info (thread_p, &class_oid, &hfid, nullptr, nullptr), NO_ERROR);
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
  DB_ATTRIBUTE *id_attr = db_get_attribute (cls, "id");
  ASSERT_NE (id_attr, nullptr);
  for (int i = 0; i < attrs.num_values; ++i)
    {
      if (attrs.values[i].attrid == db_attribute_id (id_attr))
	{
	  attrs.values[i].do_increment = 1;
	}
    }
  heap_prepared_row source;
  int error = source.prepare (thread_p, &attrs);
  heap_attrinfo_end (thread_p, &attrs);
  ASSERT_EQ (error, NO_ERROR);
  heap_prepared_row moved (std::move (source));
  heap_prepared_row destination;
  destination = std::move (moved);
  EXPECT_EQ (source.record (), nullptr);
  EXPECT_EQ (moved.record (), nullptr);
  ASSERT_EQ (destination.finalize (thread_p, &class_oid), NO_ERROR);
  HEAP_OPERATION_CONTEXT context;
  heap_create_insert_context (&context, &hfid, &class_oid, destination.record (), nullptr);
  ASSERT_EQ (heap_insert_logical (thread_p, &context, nullptr), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id = 8 "
			       "AND a = 'abcdefghijklmnopqrstuvwxyz' AND b = 'zyxwvutsrqponmlkjihgfedcba'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  EXPECT_LT (destination.finalize (thread_p, &class_oid), 0);
  er_clear ();
}

#if defined(CUBRID_UNIT_TEST_ENABLED)
TEST_F (OosSqlShow, AllocationAndStorageFailureLeaveNextInsertUsable)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, payload BIT VARYING, payload2 BIT VARYING)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  for (int boundary = 0; boundary < 4; ++boundary)
    {
      if (boundary == 3)
	{
	  oos_test_fail_insert_many_after_publications (1);
	}
      else if (boundary == 2)
	{
	  heap_oos_test_fail_before_vfid_lookup_once ();
	}
      else
	{
	  heap_prepared_row_test_fail_allocation_once (boundary == 0 ? heap_prepared_row_allocation::owner
	      : heap_prepared_row_allocation::record);
	}
      EXPECT_LT (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, REPEAT(X'AB', 8192), REPEAT(X'EF', 8192))"), 0);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      int count = -1;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes", &count), NO_ERROR);
      EXPECT_EQ (count, 0);
      DB_QUERY_RESULT *stats = nullptr;
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &stats), NO_ERROR);
      EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &count), NO_ERROR);
      EXPECT_EQ (count, 0);
      db_query_end (stats);
      ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_yes VALUES(2, REPEAT(X'CD', 8192), REPEAT(X'01', 8192))"), 0);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id = 2 "
				   "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING) "
				   "AND payload2 = CAST(REPEAT(X'01', 8192) AS BIT VARYING)", &count), NO_ERROR);
      EXPECT_EQ (count, 1);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
    }
}
#endif

TEST_F (OosSqlShow, ConstraintFailureAfterOosAllowsNextInsert)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, REPEAT(X'AB', 8192))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  EXPECT_LT (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, REPEAT(X'CD', 8192))"), 0);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_yes VALUES(2, REPEAT(X'EF', 8192))"), 0);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE "
			       "(id = 1 AND payload = CAST(REPEAT(X'AB', 8192) AS BIT VARYING)) OR "
			       "(id = 2 AND payload = CAST(REPEAT(X'EF', 8192) AS BIT VARYING))", &matches), NO_ERROR);
  EXPECT_EQ (matches, 2);
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
