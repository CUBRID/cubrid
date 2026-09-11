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
#include "locator_sr.h"
#include "class_object.h"
#include "locator_cl.h"
#include "xserver_interface.h"
#include "object_representation.h"
#include "object_representation_sr.h"

#include "test_oos_sql_common.hpp"

#if defined(CUBRID_UNIT_TEST_ENABLED)
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();
#endif

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

  struct domain_case
  {
    const char *type;
    const char *value;
    const char *boundary;
  };
  const domain_case partition_domains[] =
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
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_yes");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_no");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_part");
      db_commit_transaction ();
    }

    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_yes");
      exec_sql ("DROP TABLE IF EXISTS t_oos_show_no");
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

TEST_F (OosSqlShow, RawClientInsertOwnsDestinationOos)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, data_col BIT VARYING)"), 0);
  DB_OBJECT *row = db_create (db_find_class ("t_oos_show_yes"));
  ASSERT_NE (row, nullptr);
  DB_VALUE value;
  db_make_int (&value, 1);
  ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
  std::string payload (50000, '\xAB');
  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, payload.data (), payload.size () * 8);
  ASSERT_EQ (db_put (row, "data_col", &value), NO_ERROR);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes "
			       "WHERE id=1 AND data_col=CAST(REPEAT('AB',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  DB_QUERY_RESULT *result = nullptr;
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &result), NO_ERROR);
  int has_oos = -1;
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 1);
  db_query_end (result);
}

TEST_F (OosSqlShow, RedistributionRewritesMultichunkValuesAtDestination)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, data_col BIT VARYING) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES (1, CAST(REPEAT('CD',50000) AS BIT VARYING)), "
		       "(7, CAST(REPEAT('EF',50000) AS BIT VARYING))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_part REORGANIZE PARTITION p0 INTO "
		       "(PARTITION p2 VALUES LESS THAN (5), PARTITION p3 VALUES LESS THAN (10))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE "
			       "(id=1 AND data_col=CAST(REPEAT('CD',50000) AS BIT VARYING)) OR "
			       "(id=7 AND data_col=CAST(REPEAT('EF',50000) AS BIT VARYING))", &matches), NO_ERROR);
  EXPECT_EQ (matches, 2);
  for (const char *query :
       { "SHOW HEAP OOS OF t_oos_show_part__p__p2",
	 "SHOW HEAP OOS OF t_oos_show_part__p__p3"
       })
    {
      DB_QUERY_RESULT *result = nullptr;
      ASSERT_EQ (show_heap_oos_query (query, &result), NO_ERROR);
      int chunks = 0;
      EXPECT_EQ (get_int_column (result, COL_OOS_NUM_RECS, &chunks), NO_ERROR);
      EXPECT_GT (chunks, 1);
      db_query_end (result);
    }
}

TEST_F (OosSqlShow, RawCopyAreaRoutesInsertAndMovementWithoutChangingPayload)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, data_col BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, data_col BIT VARYING) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_show_part");
  OID root = *db_identifier (cls);
  OID source = *db_identifier (db_find_class ("t_oos_show_yes"));
  HFID root_hfid;
  ASSERT_EQ (heap_get_class_info (thread_p, &root, &root_hfid, nullptr, nullptr), NO_ERROR);
  OID row_oid = OID_INITIALIZER;
  OID last_destination = root;
  HFID last_hfid = root_hfid;
  std::string bytes (50000, '\xDA');
  for (int attempt = 0; attempt < 3; ++attempt)
    {
      HEAP_CACHE_ATTRINFO attrs;
      ASSERT_EQ (heap_attrinfo_start (thread_p, attempt == 2 ? &last_destination : &root, -1, nullptr, &attrs), NO_ERROR);
      DB_VALUE value;
      db_make_int (&value, attempt == 0 ? 1 : 10 + attempt);
      ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "id")), &value, &attrs), NO_ERROR);
      db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
      ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "data_col")), &value, &attrs),
		 NO_ERROR);
      heap_prepared_row supplied;
      ASSERT_EQ (supplied.prepare (thread_p, &attrs), NO_ERROR);
      heap_attrinfo_end (thread_p, &attrs);
      ASSERT_EQ (supplied.finalize (thread_p, &source), NO_ERROR);
      /* A supplied stored image can already contain OOS; it still needs new destination-owned chains. */
      RECDES *record = supplied.record ();
      LC_COPYAREA *area = locator_allocate_copy_area_by_length (record->length + OR_MVCC_MAX_HEADER_SIZE
			  + sizeof (LC_COPYAREA_MANYOBJS));
      ASSERT_NE (area, nullptr);
      memcpy (area->mem, record->data, record->length);
      const std::string original (area->mem, record->length);
      LC_COPYAREA_MANYOBJS *many = LC_MANYOBJS_PTR_IN_COPYAREA (area);
      memset (many, 0, sizeof (*many));
      many->num_objs = 1;
      LC_COPYAREA_ONEOBJ &obj = many->objs;
      obj.operation = attempt == 0 ? LC_FLUSH_INSERT_PRUNE : LC_FLUSH_UPDATE_PRUNE;
      obj.hfid = root_hfid;
      obj.class_oid = root;
      if (attempt == 2)
	{
	  /* Client multi-update batches use the already selected destination class. */
	  many->multi_update_flags = IS_MULTI_UPDATE | START_MULTI_UPDATE | END_MULTI_UPDATE;
	  obj.operation = LC_FLUSH_UPDATE;
	  obj.class_oid = last_destination;
	  obj.hfid = last_hfid;
	}
      obj.oid = row_oid;
      obj.length = record->length;
      obj.offset = 0;
      int error = xlocator_force (thread_p, area, 0, nullptr);
      EXPECT_EQ (std::string (area->mem, original.size ()), original);
      row_oid = obj.oid;
      OID destination = obj.class_oid;
      HFID destination_hfid = obj.hfid;
      last_destination = destination;
      last_hfid = destination_hfid;
      locator_free_copy_area (area);
      ASSERT_EQ (error, NO_ERROR) << db_error_string (1);
      HEAP_SCANCACHE scan;
      ASSERT_EQ (heap_scancache_start (thread_p, &scan, &destination_hfid, &destination, false, nullptr), NO_ERROR);
      RECDES stored = RECDES_INITIALIZER;
      SCAN_CODE status = heap_get_visible_version (thread_p, &row_oid, &destination, &stored, &scan, COPY, NULL_CHN,
			 HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      if (status == S_SUCCESS)
	{
	  EXPECT_EQ (or_rep_id (&stored), heap_get_class_repr_id (thread_p, &destination));
	}
      heap_scancache_end (thread_p, &scan);
      ASSERT_EQ (status, S_SUCCESS);
      int matches = 0;
      const char *query = attempt == 0
			  ? "SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=1 AND "
			  "data_col=CAST(REPEAT('DA',50000) AS BIT VARYING)"
			  : attempt == 1 ? "SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=11 AND "
			  "data_col=CAST(REPEAT('DA',50000) AS BIT VARYING)"
			  : "SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=12 AND "
			  "data_col=CAST(REPEAT('DA',50000) AS BIT VARYING)";
      ASSERT_EQ (fetch_single_int (query, &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      DB_QUERY_RESULT *result = nullptr;
      ASSERT_EQ (show_heap_oos_query (attempt == 0 ? "SHOW HEAP OOS OF t_oos_show_part__p__p0"
				      : "SHOW HEAP OOS OF t_oos_show_part__p__p1", &result), NO_ERROR);
      int chunks = 0;
      EXPECT_EQ (get_int_column (result, COL_OOS_NUM_RECS, &chunks), NO_ERROR);
      EXPECT_GT (chunks, 1);
      db_query_end (result);
    }
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
}

TEST_F (OosSqlShow, RawClientUpdatePreservesUnassignedValuesAndRollsBackFailure)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT UNIQUE, data_col BIT VARYING)"), 0);
  DB_OBJECT *row = db_create (db_find_class ("t_oos_show_yes"));
  ASSERT_NE (row, nullptr);
  DB_VALUE value;
  db_make_int (&value, 1);
  ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
  std::string payload (50000, '\xBC');
  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, payload.data (), payload.size () * 8);
  ASSERT_EQ (db_put (row, "data_col", &value), NO_ERROR);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  db_make_int (&value, 2);
  ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
  ASSERT_EQ (locator_flush_instance (row), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id=2 AND "
			       "data_col=CAST(REPEAT('BC',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id=1 AND "
			       "data_col=CAST(REPEAT('BC',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
#if defined(CUBRID_UNIT_TEST_ENABLED)
  db_make_int (&value, 3);
  heap_prepared_row_test_fail_allocation_once (heap_prepared_row_allocation::record);
  int error = db_put (row, "id", &value);
  if (error == NO_ERROR)
    {
      error = locator_flush_instance (row);
    }
  EXPECT_LT (error, 0);
  EXPECT_TRUE (thread_get_thread_entry_info ()->oos_oids.empty ());
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id=1 AND "
			       "data_col=CAST(REPEAT('BC',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
#endif
}

TEST_F (OosSqlShow, SerializedPreparationPreservesMvccAndOutlivesSource)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT DEFAULT 7, "
		       "k VARCHAR(80) DEFAULT 'abcdefghijklmnopqrstuvwxyz' STORAGE FORCE_OUTLINE, "
		       "data_col BIT VARYING)"), 0);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  OID class_oid = *db_identifier (cls);
  heap_prepared_row adapted;
  MVCC_REC_HEADER expected = MVCC_REC_HEADER_INITIALIZER;
  {
    HEAP_CACHE_ATTRINFO attrs;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
    std::string payload (50000, '\xED');
    DB_VALUE value;
    db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, payload.data (), payload.size () * 8);
    ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "data_col")), &value, &attrs),
	       NO_ERROR);
    heap_prepared_row source;
    ASSERT_EQ (source.prepare (thread_p, &attrs), NO_ERROR);
    heap_attrinfo_end (thread_p, &attrs);
    ASSERT_EQ (source.finalize (thread_p, &class_oid), NO_ERROR);
    ASSERT_EQ (or_mvcc_get_header (source.record (), &expected), NO_ERROR);
    expected.mvcc_flag |= OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_DELID | OR_MVCC_FLAG_VALID_PREV_VERSION;
    expected.mvcc_ins_id = 101;
    expected.mvcc_del_id = 202;
    expected.prev_version_lsa.pageid = 303;
    expected.prev_version_lsa.offset = 4;
    ASSERT_EQ (or_mvcc_set_header (source.record (), &expected), NO_ERROR);
    const std::string original (source.record ()->data, source.record ()->length);
    ASSERT_EQ (adapted.prepare_serialized (thread_p, &class_oid, source.record ()), NO_ERROR);
    EXPECT_EQ (std::string (source.record ()->data, source.record ()->length), original);
  }
  heap_prepared_row moved (std::move (adapted));
  ASSERT_EQ (moved.finalize (thread_p, &class_oid), NO_ERROR);
  MVCC_REC_HEADER actual;
  ASSERT_EQ (or_mvcc_get_header (moved.record (), &actual), NO_ERROR);
  EXPECT_EQ ((int) actual.mvcc_flag, (int) expected.mvcc_flag);
  EXPECT_EQ (actual.mvcc_ins_id, 101);
  EXPECT_EQ (actual.mvcc_del_id, 202);
  EXPECT_EQ (actual.prev_version_lsa.pageid, 303);
  EXPECT_EQ (actual.prev_version_lsa.offset, 4);
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
  ASSERT_EQ (heap_attrinfo_read_dbvalues (thread_p, &class_oid, moved.record (), &attrs), NO_ERROR);
  DB_VALUE *value = heap_attrinfo_access (db_attribute_id (db_get_attribute (cls, "data_col")), &attrs);
  int bit_length = 0;
  const char *bytes = db_get_bit (value, &bit_length);
  ASSERT_EQ (bit_length, 400000);
  EXPECT_TRUE (std::string (bytes, bit_length / 8) == std::string (50000, '\xED'));
  value = heap_attrinfo_access (db_attribute_id (db_get_attribute (cls, "k")), &attrs);
  EXPECT_STREQ (db_get_string (value), "abcdefghijklmnopqrstuvwxyz");
  heap_attrinfo_end (thread_p, &attrs);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
}

#if defined(CUBRID_UNIT_TEST_ENABLED)
TEST_F (OosSqlShow, RedistributionFailurePreservesSourceAndNextOperation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, a BIT VARYING, b BIT VARYING) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES (1, CAST(REPEAT('CD',50000) AS BIT VARYING), "
		       "CAST(REPEAT('EF',50000) AS BIT VARYING))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  for (int boundary = 0; boundary < 4; ++boundary)
    {
      SCOPED_TRACE (boundary);
      if (boundary < 2)
	{
	  heap_prepared_row_test_fail_allocation_once (boundary == 0 ? heap_prepared_row_allocation::owner
	      : heap_prepared_row_allocation::record);
	}
      else if (boundary == 2)
	{
	  heap_oos_test_fail_before_vfid_lookup_once ();
	}
      else
	{
	  oos_test_fail_insert_many_after_publications (1);
	}
      EXPECT_LT (exec_sql ("ALTER TABLE t_oos_show_part REORGANIZE PARTITION p0 INTO "
			   "(PARTITION p2 VALUES LESS THAN (5), PARTITION p3 VALUES LESS THAN (10))"), 0);
      EXPECT_TRUE (thread_get_thread_entry_info ()->oos_oids.empty ());
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      int matches = 0;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=1 AND "
				   "a=CAST(REPEAT('CD',50000) AS BIT VARYING) AND "
				   "b=CAST(REPEAT('EF',50000) AS BIT VARYING)", &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
    }
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_part REORGANIZE PARTITION p0 INTO "
		       "(PARTITION p2 VALUES LESS THAN (5), PARTITION p3 VALUES LESS THAN (10))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p2 WHERE id=1 AND "
			       "a=CAST(REPEAT('CD',50000) AS BIT VARYING) AND "
			       "b=CAST(REPEAT('EF',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
}

TEST_F (OosSqlShow, InternalAndAddressReservationsBypassPreparation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("CREATE SERIAL t_oos_ticket16_serial START WITH 1"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  OID cls = *db_identifier (db_find_class ("t_oos_show_yes"));
  HFID hfid;
  ASSERT_EQ (heap_get_class_info (thread_p, &cls, &hfid, nullptr, nullptr), NO_ERROR);
  heap_prepared_row_test_fail_allocation_once (heap_prepared_row_allocation::owner);
  OID reserved = OID_INITIALIZER;
  ASSERT_EQ (heap_assign_address (thread_p, &hfid, &cls, &reserved, 100), NO_ERROR);
  EXPECT_FALSE (OID_ISNULL (&reserved));
  int serial = 0;
  ASSERT_EQ (fetch_single_int ("SELECT CAST(t_oos_ticket16_serial.NEXT_VALUE AS INTEGER)", &serial), NO_ERROR);
  EXPECT_EQ (serial, 1);
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_yes ADD COLUMN extra INT"), 0);
  /* The pending failure must survive reservations, serial direct-page persistence and catalog writes. */
  EXPECT_LT (exec_sql ("INSERT INTO t_oos_show_yes(id) VALUES(1)"), 0);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_yes(id, payload) "
		       "VALUES(2, CAST(REPEAT('AA',50000) AS BIT VARYING))"), 0);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id=2 AND "
			       "payload=CAST(REPEAT('AA',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_GE (exec_sql ("DROP SERIAL t_oos_ticket16_serial"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
}
#endif

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
  for (const auto &entry : partition_domains)
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

  /* UPDATE reads unassigned values from the borrowed old record, and applies the fixed decrement once. */
  const std::string old_bytes (destination.record ()->data, destination.record ()->length);
  ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
  attrs.inst_oid = context.res_oid;
  for (int i = 0; i < attrs.num_values; ++i)
    {
      if (attrs.values[i].attrid == db_attribute_id (id_attr))
	{
	  attrs.values[i].do_increment = -1;
	}
    }
  heap_prepared_row update;
  error = update.prepare (thread_p, &attrs, destination.record ());
  heap_attrinfo_end (thread_p, &attrs);
  ASSERT_EQ (error, NO_ERROR);
  EXPECT_EQ (std::string (destination.record ()->data, destination.record ()->length), old_bytes);
  heap_prepared_row moved_update (std::move (update));
  ASSERT_EQ (moved_update.finalize (thread_p, &class_oid), NO_ERROR);
  HEAP_OPERATION_CONTEXT update_context;
  heap_create_update_context (&update_context, &hfid, &context.res_oid, &class_oid, moved_update.record (), nullptr,
			      UPDATE_INPLACE_CURRENT_MVCCID);
  ASSERT_EQ (heap_update_logical (thread_p, &update_context), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id = 7 "
			       "AND a = 'abcdefghijklmnopqrstuvwxyz' AND b = 'zyxwvutsrqponmlkjihgfedcba'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
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

TEST_F (OosSqlShow, DuplicateProbesDoNotPersistCandidateValues)
{
  for (bool replace :
       {
	       true, false
       })
    {
      SCOPED_TRACE (replace ? "REPLACE" : "ON DUPLICATE KEY UPDATE");
      const char *ddl = replace
			? "CREATE TABLE t_oos_show_part (k VARCHAR(80) PRIMARY KEY, payload BIT VARYING) "
			"PARTITION BY RANGE(k) (PARTITION p0 VALUES LESS THAN ('m'), "
			"PARTITION p1 VALUES LESS THAN MAXVALUE)"
			: "CREATE TABLE t_oos_show_part (k VARCHAR(80) STORAGE FORCE_OUTLINE PRIMARY KEY, "
			"payload BIT VARYING) PARTITION BY RANGE(k) (PARTITION p0 VALUES LESS THAN ('m'), "
			"PARTITION p1 VALUES LESS THAN MAXVALUE)";
      ASSERT_GE (exec_sql (ddl), 0);
      const char *sql = replace
			? "REPLACE INTO t_oos_show_part VALUES('abcdefghijklmnopqrst', REPEAT(X'AB', 8192))"
			: "INSERT INTO t_oos_show_part VALUES('abcdefghijklmnopqrst', REPEAT(X'AB', 8192)) "
			"ON DUPLICATE KEY UPDATE payload = REPEAT(X'CD', 8192)";
      for (int attempt = 0; attempt < 2; ++attempt)
	{
#if defined(CUBRID_UNIT_TEST_ENABLED)
	  bridge_oos_debug_counters_reset ();
#endif
	  EXPECT_EQ (exec_sql (sql), attempt == 0 ? 1 : 2);
#if defined(CUBRID_UNIT_TEST_ENABLED)
	  EXPECT_EQ (bridge_oos_debug_counters_get ().insert_many_requests, replace ? 1U : 2U);
#endif
	  DB_QUERY_RESULT *stats = nullptr;
	  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part", &stats), NO_ERROR);
	  int count = -1;
	  EXPECT_EQ (get_int_column (stats, COL_HAS_OOS_FILE, &count), NO_ERROR);
	  EXPECT_EQ (count, 0) << "A duplicate probe must not create a root-owned OOS file";
	  db_query_end (stats);
	  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p0", &stats), NO_ERROR);
	  EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &count), NO_ERROR);
	  EXPECT_EQ (count, replace ? 1 : 2);
	  db_query_end (stats);
	  const char *readback = !replace && attempt == 1
				 ? "SELECT COUNT(*) FROM t_oos_show_part WHERE k = 'abcdefghijklmnopqrst' "
				 "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING)"
				 : "SELECT COUNT(*) FROM t_oos_show_part WHERE k = 'abcdefghijklmnopqrst' "
				 "AND payload = CAST(REPEAT(X'AB', 8192) AS BIT VARYING)";
	  ASSERT_EQ (fetch_single_int (readback, &count), NO_ERROR);
	  EXPECT_EQ (count, 1);
	}
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_part"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlShow, DuplicateProbesReadCompositeKeys)
{
  for (bool replace :
       {
	       true, false
       })
    {
      SCOPED_TRACE (replace ? "REPLACE" : "ON DUPLICATE KEY UPDATE");
      ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (a VARCHAR(100) STORAGE FORCE_OUTLINE, "
			   "b INT, payload BIT VARYING, UNIQUE(a, b))"), 0);
      const char *sql = replace
			? "REPLACE INTO t_oos_show_yes VALUES('abcdefghijklmnopqrst', 1, REPEAT(X'AB', 8192))"
			: "INSERT INTO t_oos_show_yes VALUES('abcdefghijklmnopqrst', 1, REPEAT(X'AB', 8192)) "
			"ON DUPLICATE KEY UPDATE payload = REPEAT(X'CD', 8192)";
      ASSERT_EQ (exec_sql (sql), 1) << db_error_string (1);
      if (!replace)
	{
	  ASSERT_EQ (exec_sql (sql), 2) << db_error_string (1);
	}
      DB_QUERY_RESULT *stats = nullptr;
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &stats), NO_ERROR);
      int count = -1;
      EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &count), NO_ERROR);
      EXPECT_EQ (count, 2);
      db_query_end (stats);
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_yes"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlShow, DuplicateProbesPreserveFunctionIndexesAndCompressedCompositeKeys)
{
  for (bool replace :
       {
	       true, false
       })
    {
      for (int kind = 0; kind < 3; ++kind)
	{
	  SCOPED_TRACE (replace);
	  SCOPED_TRACE (kind);
	  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (a VARCHAR(8000) STORAGE FORCE_OUTLINE, "
			       "b INT DEFAULT 1, payload BIT VARYING)"), 0);
	  ASSERT_GE (exec_sql ("CREATE UNIQUE INDEX probe_idx ON t_oos_show_yes(a, b)"), 0);
	  if (kind < 2)
	    {
	      ASSERT_GE (exec_sql (kind == 0 ? "CREATE INDEX function_idx ON t_oos_show_yes(LOWER(a))"
				   : "CREATE INDEX function_idx ON t_oos_show_yes(LOWER(a), b)"), 0);
	    }
	  const char *sql = replace
			    ? "REPLACE INTO t_oos_show_yes(a, payload) VALUES(REPEAT('Ab', 2000), REPEAT(X'AB', 8192))"
			    : "INSERT INTO t_oos_show_yes(a, payload) VALUES(REPEAT('Ab', 2000), REPEAT(X'AB', 8192)) "
			    "ON DUPLICATE KEY UPDATE payload = REPEAT(X'CD', 8192)";
	  ASSERT_EQ (exec_sql (sql), 1) << db_error_string (1);
	  if (!replace)
	    {
	      ASSERT_EQ (exec_sql (sql), 2) << db_error_string (1);
	    }
	  int count = -1;
	  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE a = REPEAT('Ab', 2000) "
				       "AND b = 1", &count), NO_ERROR);
	  EXPECT_EQ (count, 1);
	  DB_QUERY_RESULT *stats = nullptr;
	  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &stats), NO_ERROR);
	  EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &count), NO_ERROR);
	  EXPECT_EQ (count, 2);
	  db_query_end (stats);
	  ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_yes"), 0);
	  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
	}
    }
}

#if defined(CUBRID_UNIT_TEST_ENABLED)
TEST_F (OosSqlShow, AbandonedDuplicateCandidateDoesNotWriteOos)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, X'AB')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  /* The candidate needs OOS, but the actual UPDATE stays inline. A pending failure at the OOS
   * storage boundary must survive the probe and fire on the next real OOS write. */
  heap_oos_test_fail_before_vfid_lookup_once ();
  EXPECT_EQ (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, REPEAT(X'CD', 40000)) "
		       "ON DUPLICATE KEY UPDATE payload = X'EF'"), 2);
  DB_QUERY_RESULT *stats = nullptr;
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &stats), NO_ERROR);
  int count = -1;
  EXPECT_EQ (get_int_column (stats, COL_HAS_OOS_FILE, &count), NO_ERROR);
  EXPECT_EQ (count, 0);
  db_query_end (stats);
  EXPECT_LT (exec_sql ("INSERT INTO t_oos_show_yes VALUES(2, REPEAT(X'AB', 8192))"), 0);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id = 1 AND payload = X'AB'", &count), NO_ERROR);
  EXPECT_EQ (count, 1);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_yes VALUES(2, REPEAT(X'CD', 40000))"), 1);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id = 2 "
			       "AND payload = CAST(REPEAT(X'CD', 40000) AS BIT VARYING)", &count), NO_ERROR);
  EXPECT_EQ (count, 1);
}

TEST_F (OosSqlShow, DuplicateProbeFailuresLeaveNextWriteUsable)
{
  for (bool replace :
       {
	       true, false
       })
    {
      ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT PRIMARY KEY, a BIT VARYING, b BIT VARYING) "
			   "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
			   "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
      ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(1, X'AB', X'CD')"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      for (int boundary = 0; boundary < 4; ++boundary)
	{
	  SCOPED_TRACE (replace);
	  SCOPED_TRACE (boundary);
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
	  const char *sql = replace
			    ? "REPLACE INTO t_oos_show_part VALUES(1, REPEAT(X'EF', 8192), REPEAT(X'01', 8192))"
			    : "INSERT INTO t_oos_show_part VALUES(1, REPEAT(X'EF', 8192), REPEAT(X'01', 8192)) "
			    "ON DUPLICATE KEY UPDATE a = REPEAT(X'23', 8192), b = REPEAT(X'45', 8192)";
	  EXPECT_LT (exec_sql (sql), 0);
	  auto *thread_p = thread_get_thread_entry_info ();
	  EXPECT_TRUE (thread_p->oos_oids.empty ());
	  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
	  int count = -1;
	  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE id = 1 AND a = X'AB' "
				       "AND b = X'CD'", &count), NO_ERROR);
	  EXPECT_EQ (count, 1);
	  for (const char *query :
	       { "SHOW HEAP OOS OF t_oos_show_part", "SHOW HEAP OOS OF t_oos_show_part__p__p0"
	       })
	    {
	      DB_QUERY_RESULT *stats = nullptr;
	      ASSERT_EQ (show_heap_oos_query (query, &stats), NO_ERROR);
	      EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &count), NO_ERROR);
	      EXPECT_EQ (count, 0);
	      db_query_end (stats);
	    }
	  ASSERT_EQ (exec_sql (sql), 2) << db_error_string (1);
	  const char *readback = replace
				 ? "SELECT COUNT(*) FROM t_oos_show_part WHERE a = CAST(REPEAT(X'EF', 8192) AS BIT VARYING)"
				 : "SELECT COUNT(*) FROM t_oos_show_part WHERE a = CAST(REPEAT(X'23', 8192) AS BIT VARYING)";
	  ASSERT_EQ (fetch_single_int (readback, &count), NO_ERROR);
	  EXPECT_EQ (count, 1);
	  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
	}
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_part"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}
#endif

TEST_F (OosSqlShow, DuplicateProbesPreserveLobValuesAndRollback)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_no (c CLOB)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_no VALUES(CHAR_TO_CLOB('source'))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  for (bool replace :
       {
	       true, false
       })
    {
      ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, c CLOB STORAGE FORCE_OUTLINE, "
			   "payload BIT VARYING)"), 0);
      ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, CHAR_TO_CLOB('original'), X'AB')"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      const char *sql = replace
			? "REPLACE INTO t_oos_show_yes SELECT 1, c, REPEAT(X'CD', 40000) FROM t_oos_show_no"
			: "INSERT INTO t_oos_show_yes SELECT 1, c, REPEAT(X'CD', 40000) FROM t_oos_show_no "
			"ON DUPLICATE KEY UPDATE c = CHAR_TO_CLOB('updated'), payload = REPEAT(X'EF', 40000)";
      ASSERT_EQ (exec_sql (sql), 2) << db_error_string (1);
      int count = -1;
      ASSERT_EQ (fetch_single_int (replace
				   ? "SELECT COUNT(*) FROM t_oos_show_yes WHERE CLOB_TO_CHAR(c) = 'source'"
				   : "SELECT COUNT(*) FROM t_oos_show_yes WHERE CLOB_TO_CHAR(c) = 'updated'", &count), NO_ERROR);
      EXPECT_EQ (count, 1);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE CLOB_TO_CHAR(c) = 'original'", &count),
		 NO_ERROR);
      EXPECT_EQ (count, 1);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_no WHERE CLOB_TO_CHAR(c) = 'source'", &count), NO_ERROR);
      EXPECT_EQ (count, 1);
      ASSERT_EQ (exec_sql (sql), 2) << db_error_string (1);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_yes"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlShow, ReplaceProbeReadsOutlinedCandidateAgainstInlineExistingKey)
{
  /* Keep the old key inline: standalone DELETE's baseline eager cleanup precedes index-key
   * reading for already-outlined old keys. Only the new candidate uses FORCE_OUTLINE here. */
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (k VARCHAR(80), b INT, payload BIT VARYING, UNIQUE(k, b))"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_yes VALUES('abcdefghijklmnopqrst', 1, X'AB')"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_yes MODIFY k VARCHAR(80) STORAGE FORCE_OUTLINE"), 0);
#if defined(CUBRID_UNIT_TEST_ENABLED)
  bridge_oos_debug_counters_reset ();
#endif
  ASSERT_EQ (exec_sql ("REPLACE INTO t_oos_show_yes VALUES('abcdefghijklmnopqrst', 1, REPEAT(X'CD', 8192))"), 2)
      << db_error_string (1);
#if defined(CUBRID_UNIT_TEST_ENABLED)
  EXPECT_EQ (bridge_oos_debug_counters_get ().insert_many_requests, 2U);
#endif
  int count = -1;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE k = 'abcdefghijklmnopqrst' AND b = 1 "
			       "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING)", &count), NO_ERROR);
  EXPECT_EQ (count, 1);
}

TEST_F (OosSqlShow, DuplicateProbesPreserveMultipleUniqueConstraintsAndForeignKeys)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_no (id INT PRIMARY KEY)"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_no VALUES(1)"), 1);
  for (bool replace :
       {
	       true, false
       })
    {
      SCOPED_TRACE (replace);
      ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, k VARCHAR(80) UNIQUE, "
			   "ref_id INT REFERENCES t_oos_show_no(id), payload BIT VARYING)"), 0);
      ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, 'a', 1, X'AB'), (2, 'b', 1, X'CD')"), 2);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      const char *invalid = replace
			    ? "REPLACE INTO t_oos_show_yes VALUES(1, 'b', 9, REPEAT(X'EF', 8192))"
			    : "INSERT INTO t_oos_show_yes VALUES(1, 'a', 1, REPEAT(X'EF', 8192)) "
			    "ON DUPLICATE KEY UPDATE ref_id = 9, payload = REPEAT(X'23', 8192)";
      EXPECT_LT (exec_sql (invalid), 0);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      int count = -1;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE "
				   "(id = 1 AND k = 'a' AND payload = X'AB') OR "
				   "(id = 2 AND k = 'b' AND payload = X'CD')", &count), NO_ERROR);
      EXPECT_EQ (count, 2);
      DB_QUERY_RESULT *stats = nullptr;
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &stats), NO_ERROR);
      EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &count), NO_ERROR);
      EXPECT_EQ (count, 0);
      db_query_end (stats);
      const char *valid = replace
			  ? "REPLACE INTO t_oos_show_yes VALUES(1, 'b', 1, REPEAT(X'EF', 8192))"
			  : "INSERT INTO t_oos_show_yes VALUES(1, 'a', 1, REPEAT(X'EF', 8192)) "
			  "ON DUPLICATE KEY UPDATE payload = REPEAT(X'23', 8192)";
      ASSERT_EQ (exec_sql (valid), replace ? 3 : 2) << db_error_string (1);
      ASSERT_EQ (fetch_single_int (replace
				   ? "SELECT COUNT(*) FROM t_oos_show_yes WHERE id = 1 AND k = 'b' "
				   "AND payload = CAST(REPEAT(X'EF', 8192) AS BIT VARYING)"
				   : "SELECT COUNT(*) FROM t_oos_show_yes WHERE id = 1 AND k = 'a' "
				   "AND payload = CAST(REPEAT(X'23', 8192) AS BIT VARYING)", &count), NO_ERROR);
      EXPECT_EQ (count, 1);
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_yes"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlShow, UpdateMovementAllocatesOnlyAtDestination)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, payload BIT VARYING) "
		       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(1, X'AB')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET id = 11, payload = REPEAT(X'CD', 8192) WHERE id = 1"), 0);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id = 11 "
			       "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  DB_QUERY_RESULT *result = nullptr;
  int has_oos = -1;
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p0", &result), NO_ERROR);
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 0);
  db_query_end (result);
  ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p1", &result), NO_ERROR);
  EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
  EXPECT_EQ (has_oos, 1);
  db_query_end (result);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id = 1 AND payload = X'AB'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
}

TEST_F (OosSqlShow, UpdateDomainsPreserveUnassignedValuesThroughMovementAndRollback)
{
  for (const auto &entry : partition_domains)
    {
      SCOPED_TRACE (entry.type);
      char sql[1536];
      snprintf (sql, sizeof (sql), "CREATE TABLE t_oos_show_part (k %s, payload BIT VARYING, n INT DEFAULT 7) "
		"PARTITION BY RANGE(k) (PARTITION p0 VALUES LESS THAN(%s), "
		"PARTITION p1 VALUES LESS THAN MAXVALUE)", entry.type, entry.boundary);
      ASSERT_GE (exec_sql (sql), 0) << db_error_string (1);
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_show_part(k, payload) VALUES(%s, REPEAT(X'AB', 40000))",
		entry.value);
      ASSERT_GE (exec_sql (sql), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET n = n + 1"), 0);
      int matches = 0;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE n = 8 "
				   "AND payload = CAST(REPEAT(X'AB', 40000) AS BIT VARYING)", &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      snprintf (sql, sizeof (sql), "UPDATE t_oos_show_part SET k = %s, n = n - 1", entry.boundary);
      ASSERT_GE (exec_sql (sql), 0) << db_error_string (1);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE n = 6 "
				   "AND payload = CAST(REPEAT(X'AB', 40000) AS BIT VARYING)", &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      DB_QUERY_RESULT *stats = nullptr;
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part__p__p1", &stats), NO_ERROR);
      int chunks = 0;
      EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &chunks), NO_ERROR);
      EXPECT_GT (chunks, 2);
      db_query_end (stats);
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_part", &stats), NO_ERROR);
      EXPECT_EQ (get_int_column (stats, COL_HAS_OOS_FILE, &chunks), NO_ERROR);
      EXPECT_EQ (chunks, 0);
      db_query_end (stats);
      ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET k = NULL"), 0);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE k IS NULL AND n = 6",
				   &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE n = 6", &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET payload = REPEAT(X'CD', 8192)"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE n = 6 "
				   "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING)", &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_part"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlShow, UpdateForcedKeyUsesCanonicalValueAndRejectsWrongPartition)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (k VARCHAR(80) DEFAULT 'abcdefghijklmnopqrst' "
		       "STORAGE FORCE_OUTLINE, payload BIT VARYING) PARTITION BY RANGE(k) "
		       "(PARTITION p0 VALUES LESS THAN('m'), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part(payload) VALUES(REPEAT(X'AB', 8192))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  EXPECT_LT (exec_sql ("UPDATE t_oos_show_part__p__p0 SET k = 'zyxwvutsrqponmlkjihg'"), 0);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET k = 'zyxwvutsrqponmlkjihg'"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE k = 'zyxwvutsrqponmlkjihg' "
			       "AND payload = CAST(REPEAT(X'AB', 8192) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET k = DEFAULT"), 0);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE k = 'abcdefghijklmnopqrst'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
}

TEST_F (OosSqlShow, UpdateLayoutGrowthAndLobOverwritePreserveValues)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, c CLOB STORAGE FORCE_OUTLINE, "
		       "payload BIT VARYING STORAGE PREFER_INLINE, other_payload BIT VARYING) "
		       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(1, CHAR_TO_CLOB('original'), X'', X'')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  const int sizes[] = { 120, 248, 256, 2048, 8000 };
  for (int size : sizes)
    {
      SCOPED_TRACE (size);
      char sql[1024];
      snprintf (sql, sizeof (sql), "UPDATE t_oos_show_part SET payload = REPEAT(X'AB', %d), "
		"other_payload = REPEAT(X'CD', 40000), c = CHAR_TO_CLOB('replacement')", size);
      ASSERT_GE (exec_sql (sql), 0) << db_error_string (1);
      snprintf (sql, sizeof (sql), "SELECT COUNT(*) FROM t_oos_show_part WHERE "
		"payload = CAST(REPEAT(X'AB', %d) AS BIT VARYING) AND "
		"other_payload = CAST(REPEAT(X'CD', 40000) AS BIT VARYING) AND CLOB_TO_CHAR(c) = 'replacement'", size);
      int matches = 0;
      ASSERT_EQ (fetch_single_int (sql, &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE CLOB_TO_CHAR(c) = 'original' "
				   "AND payload = X'' AND other_payload = X''", &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
    }
  ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET id = 11, c = CHAR_TO_CLOB('moved')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET id = 1"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE CLOB_TO_CHAR(c) = 'moved'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
}

#if defined(CUBRID_UNIT_TEST_ENABLED)
TEST_F (OosSqlShow, UpdateFailureClearsPublicationAndRollsBackBothDestinations)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, a BIT VARYING, b BIT VARYING) "
		       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(1, X'AB', X'CD')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  for (int target :
       {
	       1, 11
       })
    {
      for (int boundary = 0; boundary < 4; ++boundary)
	{
	  SCOPED_TRACE (target);
	  SCOPED_TRACE (boundary);
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
	  char sql[512];
	  snprintf (sql, sizeof (sql), "UPDATE t_oos_show_part SET id = %d, "
		    "a = REPEAT(X'EF', 8192), b = REPEAT(X'01', 8192)", target);
	  EXPECT_LT (exec_sql (sql), 0);
	  EXPECT_TRUE (thread_get_thread_entry_info ()->oos_oids.empty ());
	  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
	  int matches = 0;
	  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 "
				       "WHERE id = 1 AND a = X'AB' AND b = X'CD'", &matches), NO_ERROR);
	  EXPECT_EQ (matches, 1);
	  for (const char *stats_sql :
	       { "SHOW HEAP OOS OF t_oos_show_part__p__p0",
		 "SHOW HEAP OOS OF t_oos_show_part__p__p1"
	       })
	    {
	      DB_QUERY_RESULT *stats = nullptr;
	      ASSERT_EQ (show_heap_oos_query (stats_sql, &stats), NO_ERROR);
	      EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &matches), NO_ERROR);
	      EXPECT_EQ (matches, 0);
	      db_query_end (stats);
	    }
	  ASSERT_GE (exec_sql (sql), 0);
	  snprintf (sql, sizeof (sql), "SELECT COUNT(*) FROM t_oos_show_part WHERE id = %d AND "
		    "a = CAST(REPEAT(X'EF', 8192) AS BIT VARYING) AND b = CAST(REPEAT(X'01', 8192) AS BIT VARYING)",
		    target);
	  ASSERT_EQ (fetch_single_int (sql, &matches), NO_ERROR);
	  EXPECT_EQ (matches, 1);
	  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
	}
    }
}
#endif

TEST_F (OosSqlShow, UpdateIndexFailureRollsBackMovementAndNonmovement)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT PRIMARY KEY, payload BIT VARYING) "
		       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(1, X'AB'), (2, X'CD'), (11, X'EF')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  for (int duplicate :
       {
	       2, 11
       })
    {
      char sql[512];
      snprintf (sql, sizeof (sql), "UPDATE t_oos_show_part SET id = %d, payload = REPEAT(X'01', 8192) WHERE id = 1",
		duplicate);
      EXPECT_LT (exec_sql (sql), 0);
      EXPECT_TRUE (thread_get_thread_entry_info ()->oos_oids.empty ());
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      int matches = 0;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE "
				   "(id = 1 AND payload = X'AB') OR (id = 2 AND payload = X'CD') OR "
				   "(id = 11 AND payload = X'EF')", &matches), NO_ERROR);
      EXPECT_EQ (matches, 3);
      for (const char *stats_sql :
	   { "SHOW HEAP OOS OF t_oos_show_part__p__p0",
	     "SHOW HEAP OOS OF t_oos_show_part__p__p1"
	   })
	{
	  DB_QUERY_RESULT *stats = nullptr;
	  ASSERT_EQ (show_heap_oos_query (stats_sql, &stats), NO_ERROR);
	  EXPECT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &matches), NO_ERROR);
	  EXPECT_EQ (matches, 0);
	  db_query_end (stats);
	}
      snprintf (sql, sizeof (sql), "UPDATE t_oos_show_part SET id = %d, payload = REPEAT(X'01', 8192) WHERE id = 1",
		duplicate + 1);
      ASSERT_GE (exec_sql (sql), 0);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlShow, NonpartitionedUpdateChecksForeignKeysAfterFinalization)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_no (id INT PRIMARY KEY)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_no VALUES(1), (2)"), 0);
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, ref_id INT REFERENCES t_oos_show_no(id), "
		       "payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, 1, X'AB')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  EXPECT_LT (exec_sql ("UPDATE t_oos_show_yes SET ref_id = 3, payload = REPEAT(X'CD', 8192)"), 0);
  EXPECT_TRUE (thread_get_thread_entry_info ()->oos_oids.empty ());
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE ref_id = 1 AND payload = X'AB'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_GE (exec_sql ("UPDATE t_oos_show_yes SET ref_id = 2, payload = REPEAT(X'EF', 8192)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE ref_id = 2 "
			       "AND payload = CAST(REPEAT(X'EF', 8192) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_yes"), 0);
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
