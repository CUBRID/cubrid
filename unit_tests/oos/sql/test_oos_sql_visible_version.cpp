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
 * test_oos_sql_visible_version.cpp - visible-version fetch policy regression tests
 *
 * CBRD-26847
 */

#include <algorithm>
#include <array>
#include <vector>

#include "heap_oos.hpp"
#include "log_impl.h"
#include "object_representation.h"
#include "test_oos_sql_common.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* bridge functions defined in oos_file.cpp (CUBRID_UNIT_TEST_ENABLED builds) */
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();

class OosSqlVisibleVersion : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_visible_version");
      db_commit_transaction ();
    }

    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_visible_version");
      db_commit_transaction ();
    }
};

TEST_F (OosSqlVisibleVersion, ScanrangeNextFirstObjectFetchDoesNotExpandWholeRecord)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_visible_version (id INT PRIMARY KEY, payload BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_visible_version VALUES (1, REPEAT(X'AA', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *class_object = db_find_class ("t_oos_visible_version");
  ASSERT_NE (class_object, nullptr);

  OID class_oid = *reinterpret_cast<OID *> (db_identifier (class_object));
  HFID hfid;
  FILE_TYPE file_type;
  rc = heap_get_class_hfid (thread_p, &class_oid, &hfid, &file_type);
  ASSERT_EQ (rc, NO_ERROR);

  MVCC_SNAPSHOT *snapshot = logtb_get_mvcc_snapshot (thread_p);
  ASSERT_NE (snapshot, nullptr);

  HEAP_SCANRANGE scan_range;
  rc = heap_scanrange_start (thread_p, &scan_range, &hfid, &class_oid, snapshot);
  ASSERT_EQ (rc, NO_ERROR);

  bridge_oos_debug_counters_reset ();
  SCAN_CODE scan = heap_scanrange_to_following (thread_p, &scan_range, nullptr);
  EXPECT_EQ (scan, S_SUCCESS);

  OID next_oid;
  RECDES recdes = RECDES_INITIALIZER;
  OID_SET_NULL (&next_oid);

  bridge_oos_debug_counters_reset ();
  scan = heap_scanrange_next (thread_p, &next_oid, &recdes, &scan_range, PEEK);
  EXPECT_EQ (scan, S_SUCCESS);
  EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, 0ULL);

  heap_scanrange_end (thread_p, &scan_range);
}

/*
 * The old record is much larger than the fixed fetch buffer when logically expanded. Updating only the indexed
 * integer must fetch the stored-form record and Resolve attributes through the attribute layer instead of eagerly
 * Expanding the whole 64KB OOS-backed value.
 */
TEST_F (OosSqlVisibleVersion, LargeOldRecordUpdateDoesNotExpandWholeRecord)
{
  int rc;
  int bit_length = 0;
  int value_matches = 0;

  rc = exec_sql ("CREATE TABLE t_oos_visible_version (id INT PRIMARY KEY, key_col INT, payload BIT VARYING)");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("CREATE INDEX i_oos_visible_version_key ON t_oos_visible_version (key_col)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_visible_version VALUES (1, 10, REPEAT(X'AA', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  bridge_oos_debug_counters_reset ();
  rc = exec_sql ("UPDATE t_oos_visible_version SET key_col = 20 WHERE id = 1");
  ASSERT_GE (rc, 0);
  EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, 0ULL);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(payload) FROM t_oos_visible_version WHERE id = 1", &bit_length);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (bit_length, 131072);

  rc = fetch_single_int ("SELECT payload = CAST(REPEAT(X'AA', 65536) AS BIT VARYING) "
			 "FROM t_oos_visible_version WHERE id = 1", &value_matches);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (value_matches, 1);
}

namespace
{
  struct owned_fetch_scan
  {
    THREAD_ENTRY *thread;
    HEAP_SCANCACHE cache;
    bool active;

    owned_fetch_scan (THREAD_ENTRY *thread_arg, const HFID &hfid, const OID &class_oid)
      : thread (thread_arg), active (heap_scancache_start (thread, &cache, &hfid, &class_oid, true, nullptr) == NO_ERROR)
    {
    }

    void end ()
    {
      if (active)
	{
	  heap_scancache_end (thread, &cache);
	  active = false;
	}
    }

    ~owned_fetch_scan ()
    {
      end ();
    }
  };

  struct observed_stored_row
  {
    OID oid;
    int id;
    int chn;
    bool has_oos;
    oos_chain_ref ref;
    DB_BIGINT full_length;
  };

  void check_materialized_payload (RECDES &record, int id, const std::vector<char> &expected)
  {
    ASSERT_FALSE (OR_RECORD_HAS_OOS (record.data));
    ASSERT_EQ (OR_GET_INT (record.data + OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL
			   (record.data, 1, OR_GET_OFFSET_SIZE (record.data))), id);
    const int offset = OR_VAR_OFFSET (record.data, 0);
    ASSERT_GT (offset, 0);
    ASSERT_LT (offset, record.length);
    OR_BUF buf;
    or_init (&buf, record.data + offset, record.length - offset);
    int error = NO_ERROR;
    ASSERT_EQ (or_get_varbit_length (&buf, &error), static_cast<int> (expected.size () * 8));
    ASSERT_EQ (error, NO_ERROR);
    std::vector<char> actual (expected.size ());
    ASSERT_EQ (or_get_data (&buf, actual.data (), static_cast<int> (actual.size ())), NO_ERROR);
    EXPECT_EQ (actual, expected);
  }
}

// The observation happens at the heap boundary before any attribute Resolve or raw-byte Expand.
// Materialization returns caller-owned COPY bytes; ending the scan cannot invalidate those bytes.
TEST_F (OosSqlVisibleVersion, StoredAndExpandedFetchPreserveMixedRowsAndCallerOwnedBytes)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_visible_version (id INT PRIMARY KEY, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_visible_version VALUES (1, X'19ab23cd45ef')"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_visible_version VALUES "
		       "(2, REPEAT(X'A1', 17001) || REPEAT(X'B2', 17003) || REPEAT(X'C3', 17003))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  THREAD_ENTRY *thread = thread_get_thread_entry_info ();
  DB_OBJECT *table = db_find_class ("t_oos_visible_version");
  ASSERT_NE (table, nullptr);
  OID class_oid = *reinterpret_cast<OID *> (db_identifier (table));
  HFID hfid;
  FILE_TYPE file_type;
  ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, &file_type), NO_ERROR);
  std::vector<observed_stored_row> rows;
  {
    owned_fetch_scan scan (thread, hfid, class_oid);
    ASSERT_TRUE (scan.active);
    OID oid = OID_INITIALIZER;
    std::array<char, 1024> bytes;
    bridge_oos_debug_counters_reset ();
    for (;;)
      {
	RECDES stored = { static_cast<int> (bytes.size ()), 0, REC_HOME, bytes.data () };
	const SCAN_CODE code = heap_next (thread, &hfid, &class_oid, &oid, &stored, &scan.cache, COPY,
					  HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
	if (code == S_END)
	  {
	    break;
	  }
	ASSERT_EQ (code, S_SUCCESS);
	ASSERT_EQ (stored.data, bytes.data ());
	ASSERT_EQ (stored.type, REC_HOME);
	const int id = OR_GET_INT (stored.data + OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL
				   (stored.data, 1, OR_GET_OFFSET_SIZE (stored.data)));
	ASSERT_TRUE (id == 1 || id == 2);
	observed_stored_row row = { oid, id, OR_GET_MVCC_CHN (stored.data), OR_RECORD_HAS_OOS (stored.data), {}, 0 };
	EXPECT_EQ (row.has_oos, id == 2);
	if (row.has_oos)
	  {
	    ASSERT_EQ (heap_oos_parse_inline_ref (&stored, 0, &row.ref, &row.full_length), NO_ERROR);
	    EXPECT_GT (row.full_length, 51007);
	  }
	rows.push_back (row);
      }
    EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, 0ULL);
  }
  ASSERT_EQ (rows.size (), 2U);
  ASSERT_NE (rows[0].id, rows[1].id);
  for (const observed_stored_row &row : rows)
    {
      std::vector<char> expected;
      if (row.id == 1)
	{
	  expected = { '\x19', '\xab', '\x23', '\xcd', '\x45', '\xef' };
	}
      else
	{
	  expected.insert (expected.end (), 17001, '\xa1');
	  expected.insert (expected.end (), 17003, '\xb2');
	  expected.insert (expected.end (), 17003, '\xc3');
	}
      std::vector<char> bytes (128 * 1024, '\x7f');
      RECDES expanded = { static_cast<int> (bytes.size ()), 0, REC_HOME, bytes.data () };
      {
	owned_fetch_scan scan (thread, hfid, class_oid);
	ASSERT_TRUE (scan.active);
	bridge_oos_debug_counters_reset ();
	ASSERT_EQ (heap_get_visible_version (thread, &row.oid, &class_oid, &expanded, &scan.cache, COPY, NULL_CHN,
					     HEAP_RECDES_CONSUME_RAW_BYTES), S_SUCCESS);
	EXPECT_EQ (expanded.data, bytes.data ());
	EXPECT_FALSE (scan.cache.is_recdes_assigned_to_area (expanded));
	EXPECT_EQ (OR_GET_MVCC_CHN (expanded.data), row.chn);
	EXPECT_EQ (bridge_oos_debug_counters_get ().read_many_calls, row.has_oos ? 1ULL : 0ULL);
      }
      check_materialized_payload (expanded, row.id, expected);

      // PEEK expansion belongs to the scan cache and is consumed while that cache is alive.
      {
	owned_fetch_scan scan (thread, hfid, class_oid);
	ASSERT_TRUE (scan.active);
	RECDES peeked = RECDES_INITIALIZER;
	ASSERT_EQ (heap_get_visible_version (thread, &row.oid, &class_oid, &peeked, &scan.cache, PEEK, NULL_CHN,
					     HEAP_RECDES_CONSUME_RAW_BYTES), S_SUCCESS);
	EXPECT_EQ (scan.cache.is_recdes_assigned_to_area (peeked), row.has_oos);
	check_materialized_payload (peeked, row.id, expected);
      }

      // Expanding a COPY must not rewrite the persisted row or change its chain identity.
      std::array<char, 1024> stored_bytes;
      RECDES stored = { static_cast<int> (stored_bytes.size ()), 0, REC_HOME, stored_bytes.data () };
      owned_fetch_scan scan (thread, hfid, class_oid);
      ASSERT_TRUE (scan.active);
      ASSERT_EQ (heap_get_visible_version (thread, &row.oid, &class_oid, &stored, &scan.cache, COPY, NULL_CHN,
					   HEAP_RECDES_DONT_CONSUME_RAW_BYTES), S_SUCCESS);
      EXPECT_EQ (OR_RECORD_HAS_OOS (stored.data), row.has_oos);
      if (row.has_oos)
	{
	  oos_chain_ref after;
	  DB_BIGINT length;
	  ASSERT_EQ (heap_oos_parse_inline_ref (&stored, 0, &after, &length), NO_ERROR);
	  EXPECT_TRUE (OID_EQ (&after.head_oid, &row.ref.head_oid));
	  EXPECT_TRUE (LSA_EQ (&after.identity_stamp, &row.ref.identity_stamp));
	  EXPECT_EQ (length, row.full_length);
	}
    }
}

TEST_F (OosSqlVisibleVersion, ExpandRetryPreservesCallerBufferAndStoredReference)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_visible_version (id INT PRIMARY KEY, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_visible_version VALUES (1, REPEAT(X'AB', 65536))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  THREAD_ENTRY *thread = thread_get_thread_entry_info ();
  DB_OBJECT *table = db_find_class ("t_oos_visible_version");
  ASSERT_NE (table, nullptr);
  OID class_oid = *reinterpret_cast<OID *> (db_identifier (table));
  HFID hfid;
  FILE_TYPE file_type;
  ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, &file_type), NO_ERROR);
  owned_fetch_scan scan (thread, hfid, class_oid);
  ASSERT_TRUE (scan.active);
  OID oid = OID_INITIALIZER;
  std::array<char, 256> small;
  small.fill ('\x7f');
  RECDES record = { 128, 0, REC_HOME, small.data () };
  ASSERT_EQ (heap_next (thread, &hfid, &class_oid, &oid, &record, &scan.cache, COPY,
			HEAP_RECDES_DONT_CONSUME_RAW_BYTES), S_SUCCESS);
  ASSERT_TRUE (OR_RECORD_HAS_OOS (record.data));
  ASSERT_EQ (record.type, REC_HOME);
  oos_chain_ref before;
  DB_BIGINT stored_length;
  ASSERT_EQ (heap_oos_parse_inline_ref (&record, 0, &before, &stored_length), NO_ERROR);
  ASSERT_EQ (heap_get_visible_version (thread, &oid, &class_oid, &record, &scan.cache, COPY, NULL_CHN,
				       HEAP_RECDES_CONSUME_RAW_BYTES), S_DOESNT_FIT);
  ASSERT_EQ (record.data, small.data ());
  EXPECT_EQ (record.area_size, 128);
  ASSERT_LT (record.length, -65536);
  const int required = -record.length;
  EXPECT_FALSE (scan.cache.is_recdes_assigned_to_area (record));
  EXPECT_TRUE (std::all_of (small.begin () + 128, small.end (), [] (char value)
  {
    return value == '\x7f';
  }));

  // A caller-positioned buffer with no writable bytes must remain caller-owned too.
  RECDES exhausted = { 0, 0, REC_HOME, small.data () };
  ASSERT_EQ (heap_get_visible_version (thread, &oid, &class_oid, &exhausted, &scan.cache, COPY, NULL_CHN,
				       HEAP_RECDES_CONSUME_RAW_BYTES), S_DOESNT_FIT);
  EXPECT_EQ (exhausted.data, small.data ());
  EXPECT_EQ (exhausted.area_size, 0);

  std::vector<char> bytes (required + 16, '\x7f');
  RECDES expanded = { required, 0, REC_HOME, bytes.data () };
  ASSERT_EQ (heap_get_visible_version (thread, &oid, &class_oid, &expanded, &scan.cache, COPY, NULL_CHN,
				       HEAP_RECDES_CONSUME_RAW_BYTES), S_SUCCESS);
  EXPECT_EQ (expanded.data, bytes.data ());
  EXPECT_EQ (expanded.length, required);
  EXPECT_TRUE (std::all_of (bytes.begin () + required, bytes.end (), [] (char value)
  {
    return value == '\x7f';
  }));
  scan.end ();
  check_materialized_payload (expanded, 1, std::vector<char> (65536, '\xab'));

  owned_fetch_scan stored_scan (thread, hfid, class_oid);
  ASSERT_TRUE (stored_scan.active);
  RECDES stored = { static_cast<int> (small.size ()), 0, REC_HOME, small.data () };
  ASSERT_EQ (heap_get_visible_version (thread, &oid, &class_oid, &stored, &stored_scan.cache, COPY, NULL_CHN,
				       HEAP_RECDES_DONT_CONSUME_RAW_BYTES), S_SUCCESS);
  ASSERT_TRUE (OR_RECORD_HAS_OOS (stored.data));
  oos_chain_ref after;
  DB_BIGINT length;
  ASSERT_EQ (heap_oos_parse_inline_ref (&stored, 0, &after, &length), NO_ERROR);
  EXPECT_TRUE (OID_EQ (&after.head_oid, &before.head_oid));
  EXPECT_TRUE (LSA_EQ (&after.identity_stamp, &before.identity_stamp));
  EXPECT_EQ (length, stored_length);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
