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
 * test_oos_sql_crud.cpp - OOS Basic CRUD SQL tests
 *
 * Ported from: cubrid-oos-test/sql/oos_basic_crud.sql
 * CBRD-26352, CBRD-26358, CBRD-26458
 */

#include "test_oos_sql_common.hpp"

#include "log_impl.h"
#include "object_representation.h"
#include "scope_exit.hpp"
#include "work_space.h"

#include <fstream>
#include <string>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* bridge functions defined in oos_file.cpp (CUBRID_UNIT_TEST_ENABLED builds) */
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();

class OosSqlCrud : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_crud");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_crud");
      db_commit_transaction ();
    }
};

// TC-01: Basic INSERT and SELECT of OOS column
TEST_F (OosSqlCrud, BasicInsertSelect)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  small_col VARCHAR(100),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 1024-byte BIT VARYING value
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, 'row1', REPEAT(X'AB', 1024))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// TC-02: Small record should NOT trigger OOS
TEST_F (OosSqlCrud, SmallRecordNoOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  small_data BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 200-byte record: below the four-record target => no OOS
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, REPEAT(X'CD', 200))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(small_data) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 400);
}

// TC-04: Multiple OOS columns in one record
TEST_F (OosSqlCrud, MultipleOosColumns)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  oos_col1 BIT VARYING,"
		 "  oos_col2 BIT VARYING,"
		 "  oos_col3 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_crud VALUES ("
		 "  1,"
		 "  REPEAT(X'AA', 1024),"
		 "  REPEAT(X'BB', 2048),"
		 "  REPEAT(X'CC', 4096)"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len1 = 0, len2 = 0, len3 = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col1) FROM t_oos_crud WHERE id = 1", &len1);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len1, 2048);

  rc = fetch_single_int ("SELECT LENGTH(oos_col2) FROM t_oos_crud WHERE id = 1", &len2);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len2, 4096);

  rc = fetch_single_int ("SELECT LENGTH(oos_col3) FROM t_oos_crud WHERE id = 1", &len3);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len3, 8192);
}

TEST_F (OosSqlCrud, Cbrd27006MultiOosColumnSelectsAndUpdate)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  c1 BIT VARYING,"
		 "  c2 BIT VARYING,"
		 "  c3 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_crud VALUES ("
		 "  1,"
		 "  REPEAT(X'AA', 3500),"
		 "  REPEAT(X'BB', 3500),"
		 "  REPEAT(X'CC', 3500)"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(c1) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 7000);

  rc = fetch_single_int ("SELECT LENGTH(c2) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 7000);

  rc = exec_sql ("UPDATE t_oos_crud SET"
		 "  c1 = REPEAT(X'DD', 3600),"
		 "  c3 = REPEAT(X'EE', 3400)"
		 " WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(c1) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 7200);

  rc = fetch_single_int ("SELECT LENGTH(c2) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 7000);

  rc = fetch_single_int ("SELECT LENGTH(c3) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 6800);
}

TEST_F (OosSqlCrud, Cbrd27006MixedSingleChunkAndMultiChunkRow)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  single1 BIT VARYING,"
		 "  multi BIT VARYING,"
		 "  single2 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_crud VALUES ("
		 "  1,"
		 "  REPEAT(X'11', 3500),"
		 "  REPEAT(X'22', 20000),"
		 "  REPEAT(X'33', 3500)"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(single1) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 7000);

  rc = fetch_single_int ("SELECT LENGTH(multi) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 40000);

  rc = fetch_single_int ("SELECT LENGTH(single2) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 7000);

}

// Lazy OOS Resolve uses oos_read_many() only when at least two requested attributes
// are OOS-backed. Non-OOS and single-OOS projections stay on the scalar path.
TEST_F (OosSqlCrud, Cbrd27006ReadDispatchBatchesOnlyMultiOosProjections)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  small_col VARCHAR(100),"
		 "  c1 BIT VARYING,"
		 "  c2 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 4500B + 4500B: still above the four-record target after the first demotion, so both go OOS
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, 'row1', REPEAT(X'AA', 4500), REPEAT(X'BB', 4500))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;

  // Non-OOS projection on an OOS record: scalar path, no oos_read_many()
  bridge_oos_debug_counters_reset ();
  rc = fetch_single_int ("SELECT LENGTH(small_col) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4);
  EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, 0ULL);

  // Single requested OOS value: scalar path keeps the stack-scratch fast path
  bridge_oos_debug_counters_reset ();
  rc = fetch_single_int ("SELECT LENGTH(c1) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 9000);
  EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, 0ULL);

  // Two requested OOS values: one grouped oos_read_many() over one head page
  bridge_oos_debug_counters_reset ();
  rc = fetch_single_int ("SELECT LENGTH(c1) + LENGTH(c2) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 18000);

  oos_debug_counters counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.read_many_calls, 1ULL);
  EXPECT_EQ (counters.read_many_requests, 2ULL);
  EXPECT_EQ (counters.read_many_grouped_head_pages, 1ULL);

  // The predicate above also requests id. Without it exactly two values are requested, the smallest grouped read.
  bridge_oos_debug_counters_reset ();
  rc = fetch_single_int ("SELECT LENGTH(c1) + LENGTH(c2) FROM t_oos_crud", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 18000);
  counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.read_many_calls, 1ULL);
  EXPECT_EQ (counters.read_many_requests, 2ULL);
}

// TC-06: Multi-chunk OOS (value > page size, ~16KB)
TEST_F (OosSqlCrud, MultiChunkOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  huge_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  // 32KB value spans multiple OOS pages
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, REPEAT(X'FF', 32768))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(huge_col) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);
}

// TC-07: Mixed single-chunk and multi-chunk in same table
TEST_F (OosSqlCrud, MixedChunkSizes)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_crud ("
		 "  id INT PRIMARY KEY,"
		 "  data_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (1, REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (2, REPEAT(X'BB', 32768))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_crud VALUES (3, REPEAT(X'CC', 100))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_crud", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 3);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_crud WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_crud WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 65536);
}

TEST_F (OosSqlCrud, FixedValuesAcrossNullsAndRepresentations)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_crud (id INT, s SMALLINT, b BIGINT, f FLOAT, d DOUBLE, "
		       "dt DATE, n NUMERIC(10,2), payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_crud VALUES "
		       "(1, -7, 9000000000, 1.5, -2.25, DATE '2024-02-29', 123.45, X'1234'), "
		       "(2, NULL, NULL, NULL, NULL, NULL, NULL, NULL), "
		       "(3, 12, -9000000000, -1.5, 2.25, DATE '2024-03-01', -123.45, "
		       "CAST(REPEAT('A9', 20000) AS BIT VARYING))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  // Keep rows in the old representation, then read them alongside a new row and its explicit value.
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_crud ADD COLUMN added INT DEFAULT 41"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_crud (id, added) VALUES (4, 99)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  const char *checks[] =
  {
    "SELECT COUNT(*) FROM t_oos_crud WHERE id = 1 AND s = -7 AND b = 9000000000 "
    "AND f = 1.5 AND d = -2.25 AND dt = DATE '2024-02-29' AND n = 123.45 AND added = 41",
    "SELECT COUNT(*) FROM t_oos_crud WHERE id = 2 AND s IS NULL AND b IS NULL "
    "AND f IS NULL AND d IS NULL AND dt IS NULL AND n IS NULL AND added = 41",
    "SELECT COUNT(*) FROM t_oos_crud WHERE id = 3 AND s = 12 AND b = -9000000000 "
    "AND f = -1.5 AND d = 2.25 AND dt = DATE '2024-03-01' AND n = -123.45 "
    "AND added = 41 AND payload = CAST(REPEAT('A9', 20000) AS BIT VARYING)",
    "SELECT COUNT(*) FROM t_oos_crud WHERE id = 4 AND s IS NULL AND added = 99",
    "SELECT COUNT(*) FROM t_oos_crud WHERE id = 1 AND CAST(s AS BIGINT) = -7 "
    "AND CAST(b AS VARCHAR) = '9000000000' AND CAST(dt AS VARCHAR) = '02/29/2024'"
  };
  // Decode non-NULL, NULL, old/default and OOS-backed records in repeated scans. Reuse of one decode slot across rows
  // is pinned by NullFixedValueReplacesBoundValueInReusedSlot.
  for (int pass = 0; pass < 3; ++pass)
    {
      for (const char *sql : checks)
	{
	  int count = 0;
	  ASSERT_EQ (fetch_single_int (sql, &count), NO_ERROR) << sql;
	  EXPECT_EQ (count, 1) << sql;
	}
    }
}

TEST_F (OosSqlCrud, GroupedAttributeReadsAcrossOldRepresentation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_crud (id INT, small VARCHAR(100), single_data BIT VARYING, "
		       "multi_data BIT VARYING, empty_data VARCHAR(100), unused INT)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_crud VALUES "
		       "(1, 'first', CAST(REPEAT('AA', 4500) AS BIT VARYING), "
		       "CAST(REPEAT('BB', 20000) AS BIT VARYING), NULL, 8), "
		       "(2, 'second', CAST(REPEAT('CC', 4500) AS BIT VARYING), "
		       "CAST(REPEAT('DD', 20000) AS BIT VARYING), NULL, 9)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_crud ADD COLUMN added INT DEFAULT 41"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  auto *thread = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_crud");
  ASSERT_NE (cls, nullptr);
  OID class_oid = *WS_OID (cls);
  HFID hfid;
  ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, nullptr), NO_ERROR);
  DB_OBJLIST *objects = db_fetch_all_objects (cls, DB_FETCH_READ);
  ASSERT_NE (objects, nullptr);
  scope_exit free_objects ([&] ()
  {
    db_objlist_free (objects);
  });

  const char *names[] = { "multi_data", "added", "small", "single_data", "id", "empty_data" };
  ATTR_ID ids[6];
  for (int i = 0; i < 6; ++i)
    {
      DB_ATTRIBUTE *attribute = db_get_attribute (cls, names[i]);
      ASSERT_NE (attribute, nullptr);
      ids[i] = db_attribute_id (attribute);
    }
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread, &class_oid, 6, ids, &attrs), NO_ERROR);
  scope_exit end_attrs ([&] ()
  {
    heap_attrinfo_end (thread, &attrs);
  });

  int rows = 0;
  for (DB_OBJLIST *item = objects; item != nullptr; item = item->next)
    {
      const OID oid = *WS_OID (item->op);
      HEAP_SCANCACHE scan;
      ASSERT_EQ (heap_scancache_start (thread, &scan, &hfid, &class_oid, true,
				       logtb_get_mvcc_snapshot (thread)), NO_ERROR);
      RECDES record = RECDES_INITIALIZER;
      const SCAN_CODE status = heap_get_visible_version (thread, &oid, &class_oid, &record, &scan, COPY, NULL_CHN,
			       HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      std::vector<char> bytes;
      if (status == S_SUCCESS)
	{
	  bytes.assign (record.data, record.data + record.length);
	}
      EXPECT_EQ (heap_scancache_end (thread, &scan), NO_ERROR);
      ASSERT_EQ (status, S_SUCCESS);
      record.data = bytes.data ();
      record.area_size = static_cast<int> (bytes.size ());
      ASSERT_TRUE (heap_recdes_contains_oos (&record));
      ASSERT_NE (OR_GET_MVCC_REPID (record.data), heap_get_class_repr_id (thread, &class_oid));

      for (int pass = 0; pass < 2; ++pass)
	{
	  bridge_oos_debug_counters_reset ();
	  const int error = pass == 0 ? heap_attrinfo_read_dbvalues (thread, &oid, &record, &attrs)
			    : heap_attrinfo_read_dbvalues_without_oid (thread, &record, &attrs);
	  ASSERT_EQ (error, NO_ERROR);
	  const oos_debug_counters counters = bridge_oos_debug_counters_get ();
	  EXPECT_EQ (counters.read_many_calls, 1ULL);
	  EXPECT_EQ (counters.read_many_requests, 2ULL);

	  const int id = db_get_int (heap_attrinfo_access (ids[4], &attrs));
	  ASSERT_TRUE (id == 1 || id == 2);
	  EXPECT_EQ (db_get_int (heap_attrinfo_access (ids[1], &attrs)), 41);
	  EXPECT_STREQ (db_get_string (heap_attrinfo_access (ids[2], &attrs)), id == 1 ? "first" : "second");
	  EXPECT_TRUE (DB_IS_NULL (heap_attrinfo_access (ids[5], &attrs)));
	  for (int index :
	       {
		       0, 3
	       })
	    {
	      const int length = index == 0 ? 20000 : 4500;
	      const char expected = index == 0 ? (id == 1 ? '\xBB' : '\xDD') : (id == 1 ? '\xAA' : '\xCC');
	      int bits = 0;
	      const char *data = db_get_bit (heap_attrinfo_access (ids[index], &attrs), &bits);
	      ASSERT_EQ (bits, length * 8);
	      ASSERT_NE (data, nullptr);
	      EXPECT_EQ (std::string (data, length), std::string (length, expected));
	    }
	}
      ++rows;
    }
  EXPECT_EQ (rows, 2);
}

// Stored byte image of a visible row, copied out of the scan buffer.
struct row_image
{
  OID oid;
  std::vector<char> bytes;

  RECDES recdes (int length)
  {
    return { length, length, REC_HOME, bytes.data () };
  }

  RECDES recdes ()
  {
    return recdes (static_cast<int> (bytes.size ()));
  }
};

static void
capture_row_images (const char *table, OID &class_oid, std::vector<row_image> &rows)
{
  auto *thread = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class (table);
  ASSERT_NE (cls, nullptr);
  class_oid = *WS_OID (cls);
  HFID hfid;
  ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, nullptr), NO_ERROR);
  DB_OBJLIST *objects = db_fetch_all_objects (cls, DB_FETCH_READ);
  ASSERT_NE (objects, nullptr);
  scope_exit free_objects ([&] ()
  {
    db_objlist_free (objects);
  });
  for (DB_OBJLIST *item = objects; item != nullptr; item = item->next)
    {
      row_image row;
      row.oid = *WS_OID (item->op);
      HEAP_SCANCACHE scan;
      ASSERT_EQ (heap_scancache_start (thread, &scan, &hfid, &class_oid, true, logtb_get_mvcc_snapshot (thread)),
		 NO_ERROR);
      RECDES record = RECDES_INITIALIZER;
      const SCAN_CODE status = heap_get_visible_version (thread, &row.oid, &class_oid, &record, &scan, COPY, NULL_CHN,
			       HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      if (status == S_SUCCESS)
	{
	  row.bytes.assign (record.data, record.data + record.length);
	}
      EXPECT_EQ (heap_scancache_end (thread, &scan), NO_ERROR);
      ASSERT_EQ (status, S_SUCCESS);
      rows.push_back (std::move (row));
    }
}

TEST_F (OosSqlCrud, VarOffsetEntryReaderStopsAtTheEndOfTheOffsetTable)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_crud (a VARCHAR(20), b VARCHAR(20))"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_crud VALUES ('x', 'y')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  OID class_oid;
  std::vector<row_image> rows;
  ASSERT_NO_FATAL_FAILURE (capture_row_images ("t_oos_crud", class_oid, rows));
  ASSERT_EQ (rows.size (), 1u);
  const int header = OR_HEADER_SIZE (rows[0].bytes.data ());
  const int width = OR_GET_OFFSET_SIZE (rows[0].bytes.data ());
  // A record cut after `entries` offset entries has no entry at index `entries`.
  for (int entries = 1; entries <= 3; ++entries)
    {
      SCOPED_TRACE (entries);
      RECDES record = rows[0].recdes (header + entries * width);
      int entry = -1;
      EXPECT_EQ (heap_recdes_get_var_offset_entry (&record, entries - 1, &entry), NO_ERROR);
      entry = -1;
      EXPECT_EQ (heap_recdes_get_var_offset_entry (&record, entries, &entry), ER_FAILED);
      EXPECT_EQ (entry, -1);
    }
}

// The OOS flag on a NULL (zero-length) variable field must not make the grouped reader ask for a payload there.
TEST_F (OosSqlCrud, OosFlagOnEmptyVariableFieldReadsAsNull)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_crud (id INT, a BIT VARYING, b BIT VARYING, e VARCHAR(100))"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_crud VALUES (1, CAST(REPEAT('AA', 4500) AS BIT VARYING), "
		       "CAST(REPEAT('BB', 20000) AS BIT VARYING), NULL)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  OID class_oid;
  std::vector<row_image> rows;
  ASSERT_NO_FATAL_FAILURE (capture_row_images ("t_oos_crud", class_oid, rows));
  ASSERT_EQ (rows.size (), 1u);
  RECDES record = rows[0].recdes ();
  ASSERT_TRUE (heap_recdes_contains_oos (&record));

  auto *thread = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_crud");
  ASSERT_NE (cls, nullptr);
  const char *names[] = { "id", "a", "b", "e" };
  ATTR_ID ids[4];
  for (int i = 0; i < 4; ++i)
    {
      ids[i] = db_attribute_id (db_get_attribute (cls, names[i]));
    }
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread, &class_oid, 4, ids, &attrs), NO_ERROR);
  scope_exit end_attrs ([&] ()
  {
    heap_attrinfo_end (thread, &attrs);
  });
  const int location = attrs.values[3].last_attrepr->location;

  char *entry_ptr = OR_VAR_ELEMENT_PTR (record.data, location);
  const int width = OR_GET_OFFSET_SIZE (record.data);
  const int entry = OR_GET_OFFSET_INTERNAL (entry_ptr, width);
  ASSERT_FALSE (OR_IS_OOS (entry));
  // Pass 0 reads the stored row. Pass 1 reads it with the OOS flag set on e's empty offset entry.
  for (int pass = 0; pass < 2; ++pass)
    {
      if (pass == 1)
	{
	  OR_PUT_OFFSET_INTERNAL (entry_ptr, OR_SET_VAR_OOS (entry), width);
	}
      bridge_oos_debug_counters_reset ();
      ASSERT_EQ (heap_attrinfo_read_dbvalues_without_oid (thread, &record, &attrs), NO_ERROR) << "pass " << pass;
      const oos_debug_counters counters = bridge_oos_debug_counters_get ();
      EXPECT_EQ (counters.read_many_calls, 1ULL);
      EXPECT_EQ (counters.read_many_requests, 2ULL);
      EXPECT_TRUE (DB_IS_NULL (heap_attrinfo_access (ids[3], &attrs)));
      int bits = 0;
      ASSERT_NE (db_get_bit (heap_attrinfo_access (ids[2], &attrs), &bits), nullptr);
      EXPECT_EQ (bits, 20000 * 8);
    }
}

// oos_read_many () keeps its per-request bookkeeping on the stack for up to 8 requests and in a vector above that.
TEST_F (OosSqlCrud, GroupedReadOfManyOosColumnsKeepsEveryValue)
{
  for (int n :
       {
	       2, 8, 9
       })
    {
      SCOPED_TRACE (n);
      std::string create = "CREATE TABLE t_oos_crud (id INT";
      std::string values = "1";
      std::string predicate;
      std::string lengths;
      for (int i = 0; i < n; ++i)
	{
	  const std::string column = "c" + std::to_string (i);
	  const std::string literal = "'" + std::string (80, static_cast<char> ('a' + i)) + "'";
	  create += ", " + column + " VARCHAR(80) STORAGE FORCE_OUTLINE";
	  values += ", " + literal;
	  predicate += (i ? " AND " : "") + column + " = " + literal;
	  lengths += (i ? " + LENGTH(" : "LENGTH(") + column + ")";
	}
      ASSERT_GE (exec_sql ((create + ")").c_str ()), 0);
      ASSERT_GE (exec_sql (("INSERT INTO t_oos_crud VALUES (" + values + ")").c_str ()), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);

      int count = 0;
      ASSERT_EQ (fetch_single_int (("SELECT COUNT(*) FROM t_oos_crud WHERE " + predicate).c_str (), &count), NO_ERROR);
      EXPECT_EQ (count, 1);

      bridge_oos_debug_counters_reset ();
      int total_length = 0;
      ASSERT_EQ (fetch_single_int (("SELECT " + lengths + " FROM t_oos_crud").c_str (), &total_length), NO_ERROR);
      EXPECT_EQ (total_length, 80 * n);
      const oos_debug_counters counters = bridge_oos_debug_counters_get ();
      EXPECT_EQ (counters.read_many_calls, 1ULL);
      EXPECT_EQ (counters.read_many_requests, static_cast<unsigned long long> (n));

      ASSERT_GE (exec_sql ("DROP TABLE t_oos_crud"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

// Peak resident set size of this process in KiB since the last reset, or -1 when it cannot be read.
static long
peak_rss_kb ()
{
  std::ifstream status ("/proc/self/status");
  for (std::string line; std::getline (status, line);)
    {
      if (line.rfind ("VmHWM:", 0) == 0)
	{
	  return std::stol (line.substr (6));
	}
    }
  return -1;
}

static bool
reset_peak_rss ()
{
  std::ofstream clear_refs ("/proc/self/clear_refs");
  clear_refs << "5" << std::flush;
  return clear_refs.good ();
}

// Every row's grouped payload buffers must be released once its values are decoded. A standalone server call frees
// its private heap when it ends, so the retained buffers show only as the peak resident size while one scan runs.
TEST_F (OosSqlCrud, GroupedReadReleasesItsPayloadBuffers)
{
  if (!reset_peak_rss () || peak_rss_kb () < 0)
    {
      GTEST_SKIP () << "needs /proc/self/clear_refs and VmHWM";
    }
  const int rows = 500;
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_crud (id INT, a BIT VARYING, b BIT VARYING)"), 0);
  for (int i = 0; i < rows; ++i)
    {
      const std::string sql = "INSERT INTO t_oos_crud VALUES (" + std::to_string (i)
			      + ", REPEAT(X'AA', 4500), REPEAT(X'BB', 20000))";
      ASSERT_GE (exec_sql (sql.c_str ()), 0);
      if (i % 100 == 99)
	{
	  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
	}
    }
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  int total = 0;
  const char *scan = "SELECT SUM(LENGTH(a) + LENGTH(b)) FROM t_oos_crud";
  // The first scan brings in the pages and caches that later scans reuse.
  ASSERT_EQ (fetch_single_int (scan, &total), NO_ERROR);
  ASSERT_TRUE (reset_peak_rss ());
  const long baseline_kb = peak_rss_kb ();
  bridge_oos_debug_counters_reset ();
  ASSERT_EQ (fetch_single_int (scan, &total), NO_ERROR);
  EXPECT_EQ (total, rows * (9000 + 40000));
  EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, static_cast<unsigned long long> (rows));
  const long growth_kb = peak_rss_kb () - baseline_kb;
  // Keeping every row's two payloads would add about 12 MiB.
  EXPECT_LT (growth_kb, 4 * 1024) << "peak RSS grew by " << growth_kb << " KiB over one " << rows << "-row scan";
}

// Reading a NULL fixed value into a slot that held a bound value must give NULL, not the previous value.
TEST_F (OosSqlCrud, NullFixedValueReplacesBoundValueInReusedSlot)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_crud (id INT, s SMALLINT, f FLOAT)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_crud VALUES (1, -7, 1.5), (2, NULL, NULL)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  OID class_oid;
  std::vector<row_image> rows;
  ASSERT_NO_FATAL_FAILURE (capture_row_images ("t_oos_crud", class_oid, rows));
  ASSERT_EQ (rows.size (), 2u);

  auto *thread = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_crud");
  ASSERT_NE (cls, nullptr);
  const char *names[] = { "id", "s", "f" };
  ATTR_ID ids[3];
  for (int i = 0; i < 3; ++i)
    {
      ids[i] = db_attribute_id (db_get_attribute (cls, names[i]));
    }
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread, &class_oid, 3, ids, &attrs), NO_ERROR);
  scope_exit end_attrs ([&] ()
  {
    heap_attrinfo_end (thread, &attrs);
  });
  // Both rows are read twice through the same attrs, so one of the NULL reads follows the bound row.
  for (int pass = 0; pass < 2; ++pass)
    {
      for (row_image &image : rows)
	{
	  RECDES record = image.recdes ();
	  ASSERT_EQ (heap_attrinfo_read_dbvalues_without_oid (thread, &record, &attrs), NO_ERROR);
	  const int id = db_get_int (heap_attrinfo_access (ids[0], &attrs));
	  SCOPED_TRACE (id);
	  if (id == 1)
	    {
	      EXPECT_EQ (db_get_short (heap_attrinfo_access (ids[1], &attrs)), -7);
	      EXPECT_FALSE (DB_IS_NULL (heap_attrinfo_access (ids[2], &attrs)));
	    }
	  else
	    {
	      EXPECT_TRUE (DB_IS_NULL (heap_attrinfo_access (ids[1], &attrs)));
	      EXPECT_TRUE (DB_IS_NULL (heap_attrinfo_access (ids[2], &attrs)));
	    }
	}
    }
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
