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
 * test_oos_sql_deferred_write.cpp - Destination-owned OOS writes (CBRD-27089)
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "record_descriptor.hpp"
#include "heap_oos.hpp"
#include "heap_oos_value_ref.hpp"
#include "heap_pending_record.hpp"
#include "packer.hpp"
#include "locator_sr.h"
#include "log_impl.h"
#include "class_object.h"
#include "locator_cl.h"
#include "work_space.h"
#include "xserver_interface.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "transform_cl.h"

#include "test_oos_sql_heap_fixture.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(CUBRID_UNIT_TEST_ENABLED)
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();
#endif

namespace
{
#if defined(CUBRID_UNIT_TEST_ENABLED)
  // Own the received buffer and all state that must be restored before fixture SQL runs.
  struct ownerless_copyarea
  {
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    const int saved_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    LOG_TDES *tdes = LOG_FIND_TDES (saved_tran_index);
    OID class_oid;
    HFID hfid;
    LC_COPYAREA *area = nullptr;
    RECDES record = RECDES_INITIALIZER;
    RECDES original = RECDES_INITIALIZER;
    std::string original_bytes;
    OR_CLASSREP *repr = nullptr;
    int cache_index = -1;
    bool seeded = false;
    const OID stale_oid = { 987655, 24, 1 };
    const LOG_LSA stale_stamp { 876544, 124 };
    const LOG_LSA stale_lsa { 765433, 125 };

    ~ownerless_copyarea ()
    {
      restore_and_clear ();
      if (repr != nullptr)
	{
	  heap_classrepr_free_and_init (repr, &cache_index);
	}
      if (area != nullptr)
	{
	  locator_free_copy_area (area);
	}
    }

    void restore_and_clear ()
    {
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, saved_tran_index);
      if (seeded)
	{
	  thread_p->oos_oids.clear ();
	  tdes->oos_insert_lsa_queue.clear ();
	  seeded = false;
	}
      er_clear ();
    }

    void set_up ()
    {
      ASSERT_NE (tdes, nullptr);
      ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, payload BIT VARYING)"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
      ASSERT_NE (cls, nullptr);
      class_oid = *db_identifier (cls);
      ASSERT_EQ (heap_get_class_hfid (thread_p, &class_oid, &hfid, nullptr), NO_ERROR);
      DB_OBJECT *row = db_create (cls);
      ASSERT_NE (row, nullptr);
      DB_VALUE value;
      db_make_int (&value, 7);
      ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
      const std::string payload (64, '\xAB');
      db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, payload.data (), payload.size () * 8);
      ASSERT_EQ (db_put (row, "payload", &value), NO_ERROR);

      // Use the real workspace sender, including its INSERT header, in a genuine copy area.
      const int source_chn = WS_CHN (row->object);
      MOBJ class_object = static_cast<MOBJ> (cls->object);
      MOBJ row_object = static_cast<MOBJ> (row->object);
      const int capacity = tf_object_size (class_object, row_object) + OR_MVCC_MAX_HEADER_SIZE;
      area = locator_allocate_copy_area_by_length (capacity + sizeof (LC_COPYAREA_MANYOBJS));
      ASSERT_NE (area, nullptr);
      record.data = area->mem;
      record.area_size = capacity;
      record.type = REC_HOME;
      bool has_index = true;
      ASSERT_EQ (tf_mem_to_disk (cls, class_object, row_object, &record, &has_index), TF_SUCCESS);
      ASSERT_FALSE (has_index);
      MVCC_REC_HEADER header = MVCC_REC_HEADER_INITIALIZER;
      ASSERT_EQ (or_mvcc_get_header (&record, &header), NO_ERROR);
      ASSERT_EQ (header.mvcc_flag, OR_MVCC_FLAG_VALID_INSID);
      ASSERT_EQ (OR_HEADER_SIZE (record.data), OR_MVCC_INSERT_HEADER_SIZE);
      ASSERT_EQ (header.mvcc_ins_id, MVCCID_NULL);
      ASSERT_EQ (header.chn, source_chn + 1);
      ::testing::Test::RecordProperty ("received_chn", header.chn);
      ::testing::Test::RecordProperty ("received_length", record.length);
      ::testing::Test::RecordProperty ("received_mvcc_flag", static_cast<int> (header.mvcc_flag));
      ::testing::Test::RecordProperty ("received_header_size", OR_HEADER_SIZE (record.data));
      original = record;
      original_bytes.assign (record.data, record.length);

      // Abandon the unflushed workspace instance before seeding or later SQL can flush it.
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      repr = heap_classrepr_get (thread_p, &class_oid, nullptr, NULL_REPRID, &cache_index);
      ASSERT_NE (repr, nullptr);
      ASSERT_GE (cache_index, 0);
      ASSERT_EQ (repr->id, or_rep_id (&record));
      bool needs_preparation = true;
      ASSERT_EQ (heap_oos_needs_record_preparation (thread_p, &class_oid, &record, &needs_preparation), NO_ERROR);
      ASSERT_FALSE (needs_preparation);
      ASSERT_FALSE (heap_recdes_contains_oos (&record));
    }

    void seed ()
    {
      ASSERT_TRUE (thread_p->oos_oids.empty ());
      ASSERT_TRUE (tdes->oos_insert_lsa_queue.is_empty ());
      seeded = true;
      thread_p->oos_oids.push_back ({ stale_oid, stale_stamp });
      tdes->oos_insert_lsa_queue.push (stale_lsa);
      ASSERT_EQ (thread_p->oos_oids.size (), 1u);
      ASSERT_EQ (tdes->oos_insert_lsa_queue.size (), 1u);
    }

    void expect_received_unchanged ()
    {
      EXPECT_EQ (record.data, original.data);
      EXPECT_EQ (record.length, original.length);
      EXPECT_EQ (record.area_size, original.area_size);
      EXPECT_EQ (record.type, original.type);
      EXPECT_EQ (std::string (record.data, record.length), original_bytes);
    }

    void expect_publications_unchanged ()
    {
      ASSERT_EQ (thread_p->oos_oids.size (), 1u);
      EXPECT_TRUE (OID_EQ (&thread_p->oos_oids.front ().oid, &stale_oid));
      EXPECT_EQ (thread_p->oos_oids.front ().identity_stamp, stale_stamp);
      ASSERT_EQ (tdes->oos_insert_lsa_queue.size (), 1u);
      EXPECT_EQ (tdes->oos_insert_lsa_queue.front (), stale_lsa);
    }
  };

  // Keep the native call visible in each test while this scope owns the invalid transaction index.
  class missing_tdes_call
  {
    public:
      explicit missing_tdes_call (THREAD_ENTRY *thread_p)
	: m_thread_p (thread_p), m_saved_index (LOG_FIND_THREAD_TRAN_INDEX (thread_p))
      {
	LOG_SET_CURRENT_TRAN_INDEX (m_thread_p, NULL_TRAN_INDEX);
	er_clear ();
      }

      ~missing_tdes_call ()
      {
	restore ();
      }

      int capture_error_and_restore ()
      {
	const int error = er_errid ();
	restore ();
	return error;
      }

    private:
      void restore ()
      {
	if (m_active)
	  {
	    LOG_SET_CURRENT_TRAN_INDEX (m_thread_p, m_saved_index);
	    m_active = false;
	  }
      }

      THREAD_ENTRY *m_thread_p;
      const int m_saved_index;
      bool m_active = true;
  };

  enum class write_failure
  {
    preparation, vfid_lookup, partial_batch, heap_insert
  };

  static void
  arm_write_failure (write_failure failure)
  {
    switch (failure)
      {
      case write_failure::preparation:
	heap_oos_test_fail_preparation_once ();
	break;
      case write_failure::vfid_lookup:
	heap_oos_test_fail_before_vfid_lookup_once ();
	break;
      case write_failure::partial_batch:
	oos_test_fail_insert_many_after_publications (1);
	break;
      case write_failure::heap_insert:
	heap_oos_test_fail_heap_insert_once ();
	break;
      }
  }
#endif

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
    { "CHAR(40)", "'abcdefghijklmnopqrstuvwxyz'", "'m'" },
    { "VARCHAR(80)", "'abcdefghijklmnopqrstuvwxyz'", "'m'" }
  };

}

class OosSqlDeferredWrite : public OosSqlHeapFixture
{
};

#if defined(CUBRID_UNIT_TEST_ENABLED)
TEST_F (OosSqlDeferredWrite, OwnerlessCopyAreaBypassClearsBothPublicationsAndKeepsReceivedBytes)
{
  ownerless_copyarea input;
  ASSERT_NO_FATAL_FAILURE (input.set_up ());
  heap_pending_record received;
  RECDES converted = RECDES_INITIALIZER;
  RECDES *active = &input.record;
  ASSERT_NO_FATAL_FAILURE (input.seed ());

  const int error = locator_test_finalize_oos_record (input.thread_p, &input.class_oid, &active, true, nullptr,
		    &received, &converted);

  EXPECT_EQ (error, NO_ERROR);
  EXPECT_TRUE (input.thread_p->oos_oids.empty ());
  EXPECT_TRUE (input.tdes->oos_insert_lsa_queue.is_empty ());
  EXPECT_EQ (active, &input.record);
  input.expect_received_unchanged ();
  EXPECT_EQ (received.get_recdes ().data, nullptr);
  EXPECT_FALSE (received.is_prepared ());
  EXPECT_FALSE (received.is_finalized ());
  EXPECT_EQ (converted.data, nullptr);
}

TEST_F (OosSqlDeferredWrite, OwnerlessCopyAreaBypassMissingTdesPreservesPairAndFails)
{
  ownerless_copyarea input;
  ASSERT_NO_FATAL_FAILURE (input.set_up ());
  heap_pending_record received;
  RECDES converted = RECDES_INITIALIZER;
  RECDES *active = &input.record;
  ASSERT_NO_FATAL_FAILURE (input.seed ());
  // SA reads log_Tran_index. Keep the genuine current class representation fixed during this call.
  missing_tdes_call call (input.thread_p);
  const int error = locator_test_finalize_oos_record (input.thread_p, &input.class_oid, &active, true, nullptr,
		    &received, &converted);
  const int reported_error = call.capture_error_and_restore ();

  EXPECT_EQ (error, ER_LOG_UNKNOWN_TRANINDEX);
  EXPECT_EQ (reported_error, ER_LOG_UNKNOWN_TRANINDEX);
  input.expect_publications_unchanged ();
  EXPECT_EQ (active, &input.record);
  input.expect_received_unchanged ();
  EXPECT_EQ (received.get_recdes ().data, nullptr);
  EXPECT_FALSE (received.is_prepared ());
  EXPECT_FALSE (received.is_finalized ());
  EXPECT_EQ (converted.data, nullptr);
}

TEST_F (OosSqlDeferredWrite, CopyAreaInsertMissingTdesDoesNotEnterSuccessContinuation)
{
  ownerless_copyarea input;
  ASSERT_NO_FATAL_FAILURE (input.set_up ());
  OID inserted_oid = oid_Null_oid;
  int force_count = 37;
  ASSERT_EQ (force_count, 37);
  ASSERT_TRUE (OID_ISNULL (&inserted_oid));
  ASSERT_NO_FATAL_FAILURE (input.seed ());
  missing_tdes_call call (input.thread_p);
  const int error = locator_insert_force (input.thread_p, &input.hfid, &input.class_oid, &inserted_oid, &input.record,
					  false, SINGLE_ROW_INSERT, nullptr, &force_count, DB_NOT_PARTITIONED_CLASS,
					  nullptr, nullptr, UPDATE_INPLACE_NONE, nullptr, false, false, false, true, nullptr);
  const int reported_error = call.capture_error_and_restore ();

  EXPECT_EQ (error, ER_LOG_UNKNOWN_TRANINDEX);
  EXPECT_EQ (reported_error, ER_LOG_UNKNOWN_TRANINDEX);
  // The first success continuation resets this count, before any heap or index insertion.
  EXPECT_EQ (force_count, 37);
  EXPECT_TRUE (OID_ISNULL (&inserted_oid));
  input.expect_publications_unchanged ();
  input.expect_received_unchanged ();
  input.restore_and_clear ();
  int matches = -1;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes", &matches), NO_ERROR);
  EXPECT_EQ (matches, 0);
}

TEST_F (OosSqlDeferredWrite, SmallWorkspaceFlushDoesNotNeedOosPreparation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_no (id INT, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, payload BIT VARYING STORAGE FORCE_OUTLINE)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  DB_OBJECT *row = db_create (db_find_class ("t_oos_show_no"));
  ASSERT_NE (row, nullptr);
  const std::string bytes (64, '\xAB');
  DB_VALUE value;
  db_make_int (&value, 1);
  ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
  ASSERT_EQ (db_put (row, "payload", &value), NO_ERROR);
  // An OOS preparation allocation failure must leave an ordinary small flush unaffected,
  // but must still reject the equally small FORCE_OUTLINE row that actually needs preparation.
  heap_oos_test_fail_preparation_once ();
  EXPECT_EQ (locator_flush_instance (row), NO_ERROR);
  db_make_int (&value, 2);
  EXPECT_EQ (db_put (row, "id", &value), NO_ERROR);
  EXPECT_EQ (locator_flush_instance (row), NO_ERROR);
  // Create the forced row after these flushes: a workspace flush can send other dirty rows too.
  row = db_create (db_find_class ("t_oos_show_yes"));
  ASSERT_NE (row, nullptr);
  db_make_int (&value, 1);
  ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
  ASSERT_EQ (db_put (row, "payload", &value), NO_ERROR);
  EXPECT_LT (locator_flush_instance (row), NO_ERROR);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
}
#endif

TEST_F (OosSqlDeferredWrite, WorkspaceFlushNearSizeGateReservesMvccHeadroom)
{
  const int sizes[] = { DB_PAGESIZE / 4 - 128, DB_PAGESIZE / 4 - 40, DB_PAGESIZE / 4 + 16 };
  const int expected_oos[] = { 0, 1, 1 };
  for (int i = 0; i < 3; ++i)
    {
      SCOPED_TRACE (sizes[i]);
      ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, payload BIT VARYING)"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      DB_OBJECT *row = db_create (db_find_class ("t_oos_show_yes"));
      ASSERT_NE (row, nullptr);
      DB_VALUE value;
      db_make_int (&value, 1);
      ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
      const std::string bytes (sizes[i], '\xAB');
      db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
      ASSERT_EQ (db_put (row, "payload", &value), NO_ERROR);
      ASSERT_EQ (locator_flush_instance (row), NO_ERROR);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      char sql[256];
      snprintf (sql, sizeof (sql), "SELECT COUNT(*) FROM t_oos_show_yes WHERE id=1 "
		"AND payload=CAST(REPEAT('AB',%d) AS BIT VARYING)", sizes[i]);
      int matches = 0;
      ASSERT_EQ (fetch_single_int (sql, &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      DB_QUERY_RESULT *result = nullptr;
      ASSERT_EQ (show_heap_oos_query ("SHOW HEAP OOS OF t_oos_show_yes", &result), NO_ERROR);
      int has_oos = -1;
      EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
      EXPECT_EQ (has_oos, expected_oos[i]);
      db_query_end (result);
      db_make_int (&value, 2);
      ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
      ASSERT_EQ (locator_flush_instance (row), NO_ERROR);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      ASSERT_EQ (fetch_single_int (sql, &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_yes"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlDeferredWrite, SmallOldCopyAreaMaterializesCurrentRepresentation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, payload BIT VARYING)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  OID class_oid = *db_identifier (cls);
  HFID hfid;
  ASSERT_EQ (heap_get_class_hfid (thread_p, &class_oid, &hfid, nullptr), NO_ERROR);
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
  DB_VALUE value;
  db_make_int (&value, 1);
  ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "id")), &value, &attrs), NO_ERROR);
  const std::string bytes (64, '\xCD');
  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
  ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "payload")), &value, &attrs), NO_ERROR);
  heap_pending_record supplied;
  ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &supplied), S_SUCCESS);
  heap_attrinfo_end (thread_p, &attrs);
  RECDES record = supplied.get_recdes ();
  ASSERT_FALSE (heap_recdes_contains_oos (&record));
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_yes ADD COLUMN extra INT DEFAULT 7"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  const REPR_ID current_rep = heap_get_class_repr_id (thread_p, &class_oid);
  ASSERT_NE (or_rep_id (&record), current_rep);

  LC_COPYAREA *area = locator_allocate_copy_area_by_length (record.length + OR_MVCC_MAX_HEADER_SIZE
		      + sizeof (LC_COPYAREA_MANYOBJS));
  ASSERT_NE (area, nullptr);
  memcpy (area->mem, record.data, record.length);
  LC_COPYAREA_MANYOBJS *many = LC_MANYOBJS_PTR_IN_COPYAREA (area);
  memset (many, 0, sizeof (*many));
  many->num_objs = 1;
  LC_COPYAREA_ONEOBJ &obj = many->objs;
  obj.operation = LC_FLUSH_INSERT;
  obj.hfid = hfid;
  obj.class_oid = class_oid;
  OID_SET_NULL (&obj.oid);
  obj.length = record.length;
  obj.offset = 0;
  const int error = xlocator_force (thread_p, area, 0, nullptr);
  const OID row_oid = obj.oid;
  locator_free_copy_area (area);
  ASSERT_EQ (error, NO_ERROR);
  HEAP_SCANCACHE scan;
  ASSERT_EQ (heap_scancache_start (thread_p, &scan, &hfid, &class_oid, false, nullptr), NO_ERROR);
  RECDES stored = RECDES_INITIALIZER;
  const SCAN_CODE status = heap_get_visible_version (thread_p, &row_oid, &class_oid, &stored, &scan, COPY, NULL_CHN,
			   HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
  if (status == S_SUCCESS)
    {
      EXPECT_EQ (or_rep_id (&stored), current_rep);
    }
  heap_scancache_end (thread_p, &scan);
  ASSERT_EQ (status, S_SUCCESS);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE id=1 AND extra=7 "
			       "AND payload=CAST(REPEAT('CD',64) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
}

TEST_F (OosSqlDeferredWrite, InsertOwnsOosInDestinationHeap)
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

TEST_F (OosSqlDeferredWrite, RawClientInsertOwnsDestinationOos)
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

TEST_F (OosSqlDeferredWrite, WorkspacePartitionInsertAndMovementOwnDestination)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT, data_col BIT VARYING) "
		       "PARTITION BY RANGE (id) (PARTITION p0 VALUES LESS THAN (10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  // Public db_create/db_put reject partition access; exercise the internal workspace flush path.
  DB_OBJECT *row = db_create_internal (db_find_class ("t_oos_show_part"));
  ASSERT_NE (row, nullptr);
  DB_VALUE value;
  db_make_int (&value, 1);
  ASSERT_EQ (db_put_internal (row, "id", &value), NO_ERROR);
  std::string payload (50000, '\xAB');
  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, payload.data (), payload.size () * 8);
  ASSERT_EQ (db_put_internal (row, "data_col", &value), NO_ERROR);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);

  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE id=1 AND "
			       "data_col=CAST(REPEAT('AB',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  db_make_int (&value, 11);
  ASSERT_EQ (db_put_internal (row, "id", &value), NO_ERROR);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 WHERE id=11 AND "
			       "data_col=CAST(REPEAT('AB',50000) AS BIT VARYING)", &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0", &matches), NO_ERROR);
  EXPECT_EQ (matches, 0);

  const char *queries[] = { "SHOW HEAP OOS OF t_oos_show_part", "SHOW HEAP OOS OF t_oos_show_part__p__p1" };
  for (int i = 0; i < 2; ++i)
    {
      DB_QUERY_RESULT *result = nullptr;
      ASSERT_EQ (show_heap_oos_query (queries[i], &result), NO_ERROR);
      int has_oos = -1;
      EXPECT_EQ (get_int_column (result, COL_HAS_OOS_FILE, &has_oos), NO_ERROR);
      EXPECT_EQ (has_oos, i);
      db_query_end (result);
    }
}

TEST_F (OosSqlDeferredWrite, RedistributionRewritesMultichunkValuesAtDestination)
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

TEST_F (OosSqlDeferredWrite, RawCopyAreaRoutesInsertAndMovementWithoutChangingPayload)
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
  ASSERT_EQ (heap_get_class_hfid (thread_p, &root, &root_hfid, nullptr), NO_ERROR);
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
      heap_pending_record supplied;
      ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &supplied), S_SUCCESS);
      RECDES supplied_record = supplied.get_recdes ();
      heap_attrinfo_end (thread_p, &attrs);
      ASSERT_EQ (heap_oos_finalize_record (thread_p, &source, &supplied_record, &supplied), NO_ERROR);
      /* A supplied stored image can already contain OOS; it still needs new destination-owned chains. */
      RECDES *record = &supplied_record;
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

TEST_F (OosSqlDeferredWrite, RawClientUpdatePreservesUnassignedValuesAndRollsBackFailure)
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
  heap_oos_test_fail_preparation_once ();
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

TEST_F (OosSqlDeferredWrite, PrefetchSkipsUnexpandedOosNeighbors)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, data_col BIT VARYING STORAGE FORCE_OUTLINE)"), 0);
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  OID rows[3];
  const std::string bytes (50000, '\xAC');
  for (int i = 0; i < 3; ++i)
    {
      DB_OBJECT *row = db_create (cls);
      ASSERT_NE (row, nullptr);
      DB_VALUE value;
      db_make_int (&value, i);
      ASSERT_EQ (db_put (row, "id", &value), NO_ERROR);
      if (i == 1)
	{
	  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
	  ASSERT_EQ (db_put (row, "data_col", &value), NO_ERROR);
	}
      ASSERT_EQ (locator_flush_instance (row), NO_ERROR);
      OID *row_oid = ws_identifier (row);
      ASSERT_NE (row_oid, nullptr);
      rows[i] = *row_oid;
    }
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_EQ (rows[0].pageid, rows[1].pageid);
  ASSERT_EQ (rows[1].pageid, rows[2].pageid);
  OID class_oid = *db_identifier (cls);
  for (int primary :
       {
	       0, 2
       })
    {
      LC_COPYAREA *area = locator_allocate_copy_area_by_length (2 * DB_PAGESIZE);
      ASSERT_NE (area, nullptr);
      LC_COPYAREA_MANYOBJS *many = LC_MANYOBJS_PTR_IN_COPYAREA (area);
      many->num_objs = 0;
      LC_COPYAREA_ONEOBJ *obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (many);
      int offset = 0;
      RECDES recdes = RECDES_INITIALIZER;
      recdes.data = area->mem;
      recdes.area_size = area->length - sizeof (*many);
      LC_COPYAREA_DESC prefetch = { many, &obj, &offset, &recdes };
      ASSERT_EQ (heap_prefetch (thread_get_thread_entry_info (), &class_oid, &rows[primary], &prefetch), NO_ERROR);
      obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (many);
      bool inline_neighbor = false;
      for (int i = 0; i < many->num_objs; ++i, obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj))
	{
	  if (obj->operation != LC_FETCH || !OID_EQ (&obj->class_oid, &class_oid))
	    {
	      continue;
	    }
	  RECDES view = RECDES_INITIALIZER;
	  view.data = area->mem + obj->offset;
	  view.length = obj->length;
	  EXPECT_TRUE (heap_recdes_has_valid_header (&view));
	  EXPECT_FALSE (heap_recdes_contains_oos (&view));
	  EXPECT_FALSE (OID_EQ (&obj->oid, &rows[1]));
	  inline_neighbor |= OID_EQ (&obj->oid, &rows[2 - primary]);
	}
      EXPECT_TRUE (inline_neighbor);
      locator_free_copy_area (area);
    }
}

TEST_F (OosSqlDeferredWrite, PendingReferencesResolveAndFinalizeInPlace)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, "
		       "a BIT VARYING STORAGE FORCE_OUTLINE, b BIT VARYING STORAGE FORCE_OUTLINE)"), 0);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  OID class_oid = *db_identifier (cls);
  heap_pending_record storage;
  int locations[2];
  {
    HEAP_CACHE_ATTRINFO attrs;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
    DB_VALUE value;
    db_make_int (&value, 7);
    ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "id")), &value, &attrs), NO_ERROR);
    const char *names[] = { "a", "b" };
    for (int i = 0; i < 2; ++i)
      {
	std::string bytes (50000, i == 0 ? '\xAB' : '\xCD');
	ATTR_ID id = db_attribute_id (db_get_attribute (cls, names[i]));
	db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
	ASSERT_EQ (heap_attrinfo_set (nullptr, id, &value, &attrs), NO_ERROR);
	for (int j = 0; j < attrs.num_values; ++j)
	  {
	    if (attrs.values[j].attrid == id)
	      {
		locations[i] = attrs.values[j].last_attrepr->location;
	      }
	  }
      }
    ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &storage), S_SUCCESS);
    heap_attrinfo_end (thread_p, &attrs);
  }
  EXPECT_GE (storage.retained_bytes (), 100000u);
  // Actual fetch publication rejects the row and copied byte images before
  // publishing a descriptor or increasing the copy-area count.
  LC_COPYAREA_MANYOBJS exported = {};
  LC_COPYAREA_ONEOBJ descriptor;
  std::memset (&descriptor, 0x5A, sizeof (descriptor));
  const std::string untouched ((char *) &descriptor, sizeof (descriptor));
  record_descriptor temporary_copy (storage.get_recdes ());
  for (const RECDES *candidate :
       {
	       &storage.get_recdes (), &temporary_copy.get_recdes ()
       })
    {
      EXPECT_EQ (locator_copyarea_add_fetch (&class_oid, &oid_Null_oid, candidate, 0, &exported, &descriptor),
		 ER_HEAP_OOS_BAD_INLINE_HEADER);
      EXPECT_EQ (exported.num_objs, 0);
      EXPECT_EQ (std::string ((char *) &descriptor, sizeof (descriptor)), untouched);
      er_clear ();
    }
  RECDES unowned = temporary_copy.get_recdes ();
  EXPECT_EQ (unowned.type, REC_HOME);
  EXPECT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &unowned), ER_HEAP_OOS_BAD_INLINE_HEADER);
  er_clear ();
  char short_bytes[OR_MVCC_MIN_HEADER_SIZE] = {};
  for (int length = 0; length < OR_MVCC_MIN_HEADER_SIZE; ++length)
    {
      RECDES short_row = { sizeof (short_bytes), length, REC_HOME, short_bytes };
      EXPECT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &short_row), ER_HEAP_OOS_BAD_INLINE_HEADER);
      er_clear ();
      EXPECT_EQ (locator_copyarea_add_fetch (&class_oid, &oid_Null_oid, &short_row, 0, &exported, &descriptor),
		 ER_HEAP_OOS_BAD_INLINE_HEADER);
      EXPECT_EQ (exported.num_objs, 0);
      EXPECT_EQ (std::string ((char *) &descriptor, sizeof (descriptor)), untouched);
      er_clear ();
    }
  HFID hfid;
  ASSERT_EQ (heap_get_class_hfid (thread_p, &class_oid, &hfid, nullptr), NO_ERROR);
  HEAP_OPERATION_CONTEXT insertion;
  heap_create_insert_context (&insertion, &hfid, &class_oid, &unowned, nullptr);
  EXPECT_EQ (heap_insert_logical (thread_p, &insertion, nullptr), ER_HEAP_OOS_BAD_INLINE_HEADER);
  er_clear ();
  RECDES record = storage.get_recdes ();
  MVCC_REC_HEADER enlarged;
  ASSERT_EQ (or_mvcc_get_header (&record, &enlarged), NO_ERROR);
  enlarged.mvcc_flag |= OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_DELID | OR_MVCC_FLAG_VALID_PREV_VERSION;
  enlarged.mvcc_ins_id = 101;
  enlarged.mvcc_del_id = 202;
  enlarged.prev_version_lsa.pageid = 303;
  enlarged.prev_version_lsa.offset = 4;
  ASSERT_EQ (or_mvcc_set_header (&record, &enlarged), NO_ERROR);
  ASSERT_GT (record.length, storage.get_recdes ().length);
  char *const original_buffer = record.data;
  const int original_length = record.length;
  std::vector<char> original (record.data, record.data + record.length);
  std::vector<char> payloads[2];
  for (int i = 0; i < 2; ++i)
    {
      heap_oos_value_ref ref;
      // Prepared bytes alone cannot authorize access to retained memory.
      EXPECT_EQ (heap_oos_value_ref::decode (record, locations[i], ref), ER_HEAP_OOS_BAD_INLINE_HEADER);
      er_clear ();
      heap_pending_record wrong_owner;
      EXPECT_EQ (heap_oos_value_ref::decode (record, locations[i], ref, &wrong_owner), ER_HEAP_OOS_BAD_INLINE_HEADER);
      er_clear ();
      record_descriptor copied (record);
      EXPECT_EQ (heap_oos_value_ref::decode (copied.get_recdes (), locations[i], ref, &storage),
		 ER_HEAP_OOS_BAD_INLINE_HEADER);
      er_clear ();
      ASSERT_EQ (heap_oos_value_ref::decode (record, locations[i], ref, &storage), NO_ERROR);
      payloads[i].resize (ref.length ());
      ASSERT_EQ (ref.read_into (thread_p, { payloads[i].data (), payloads[i].size () }), NO_ERROR);
      // The same bytes received from a client or fetched from disk cannot authorize memory access.
      RECDES untrusted = record;
      untrusted.type = REC_HOME;
      EXPECT_EQ (heap_oos_value_ref::decode (untrusted, locations[i], ref), ER_HEAP_OOS_BAD_INLINE_HEADER);
      er_clear ();
      char *stub = nullptr;
      ASSERT_EQ (heap_recdes_get_oos_inline_stub (&record, locations[i], &stub), NO_ERROR);
      char saved[OR_OOS_INLINE_SIZE];
      std::memcpy (saved, stub, sizeof (saved));
      OR_BUF buf;
      or_init (&buf, stub + OR_OID_SIZE + OR_BIGINT_SIZE, OR_BIGINT_SIZE);
      or_put_bigint (&buf, -1);
      EXPECT_EQ (heap_oos_value_ref::decode (record, locations[i], ref, &storage), ER_HEAP_OOS_BAD_INLINE_HEADER);
      er_clear ();
      std::memcpy (stub, saved, sizeof (saved));
      or_init (&buf, stub + OR_OID_SIZE, OR_BIGINT_SIZE);
      or_put_bigint (&buf, (DB_BIGINT) payloads[i].size () + 1);
      EXPECT_EQ (heap_oos_value_ref::decode (record, locations[i], ref, &storage), ER_HEAP_OOS_BAD_INLINE_HEADER);
      er_clear ();
      std::memcpy (stub, saved, sizeof (saved));
    }
  // Both requested attributes take grouped Resolve and survive source DB_VALUE cleanup.
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
  ASSERT_EQ (heap_attrinfo_read_dbvalues (thread_p, &oid_Null_oid, &record, &attrs, &storage), NO_ERROR);
  EXPECT_EQ (db_get_int (heap_attrinfo_access (db_attribute_id (db_get_attribute (cls, "id")), &attrs)), 7);
  heap_attrinfo_end (thread_p, &attrs);
  EXPECT_TRUE (thread_p->oos_oids.empty ());

  ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &record, &storage), NO_ERROR);
  EXPECT_EQ (record.data, original_buffer);
  EXPECT_EQ (record.length, original_length);
  EXPECT_EQ (record.type, REC_HOME);
  EXPECT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &record), NO_ERROR);
  // Storage validation supports legacy last offsets without a modern sentinel.
  const int last_location = std::max (locations[0], locations[1]) + 1;
  const int offset_size = OR_GET_OFFSET_SIZE (record.data);
  int last_entry;
  ASSERT_EQ (heap_recdes_get_var_offset_entry (&record, last_location, &last_entry), NO_ERROR);
  OR_BUF last_buf;
  char *last_ptr = OR_VAR_ELEMENT_PTR (record.data, last_location);
  or_init (&last_buf, last_ptr, offset_size);
  or_put_offset_internal (&last_buf, OR_GET_VAR_OFFSET (last_entry), offset_size);
  EXPECT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &record), NO_ERROR);
  or_init (&last_buf, last_ptr, offset_size);
  or_put_offset_internal (&last_buf, last_entry, offset_size);
  EXPECT_EQ (locator_copyarea_add_fetch (&class_oid, &oid_Null_oid, &record, 0, &exported, &descriptor),
	     ER_HEAP_OOS_BAD_INLINE_HEADER);
  EXPECT_EQ (exported.num_objs, 0);
  er_clear ();
  // Root metadata has a separate format and bypasses the heap-row check.
  EXPECT_EQ (locator_copyarea_add_fetch (oid_Root_class_oid, &class_oid, &record, 0, &exported, &descriptor), NO_ERROR);
  EXPECT_EQ (exported.num_objs, 1);
  // Keep the finalized bytes while destroying the row and all retained memory values.
  record_descriptor persisted (record);
  {
    heap_pending_record retired (std::move (storage));
  }
  record = persisted.get_recdes ();
  for (int i = 0; i < 2; ++i)
    {
      heap_oos_value_ref ref;
      ASSERT_EQ (heap_oos_value_ref::decode (record, locations[i], ref), NO_ERROR);
      std::vector<char> bytes (ref.length ());
      ASSERT_EQ (ref.read_into (thread_p, { bytes.data (), bytes.size () }), NO_ERROR);
      EXPECT_EQ (bytes, payloads[i]);
      char *stub = nullptr;
      ASSERT_EQ (heap_recdes_get_oos_inline_stub (&record, locations[i], &stub), NO_ERROR);
      // Mask only the overwritten stubs: every other record byte must be identical.
      std::memcpy (original.data () + (stub - record.data), stub, OR_OOS_INLINE_SIZE);
    }
  EXPECT_EQ (std::string (record.data, record.length), std::string (original.data (), original.size ()));
  heap_create_insert_context (&insertion, &hfid, &class_oid, &record, nullptr);
  ASSERT_EQ (heap_insert_logical (thread_p, &insertion, nullptr), NO_ERROR);
  HEAP_OPERATION_CONTEXT update;
  heap_create_update_context (&update, &hfid, &insertion.res_oid, &class_oid, &unowned, nullptr, UPDATE_INPLACE_NONE);
  EXPECT_EQ (heap_update_logical (thread_p, &update), ER_HEAP_OOS_BAD_INLINE_HEADER);
  er_clear ();
  RECDES expanded = RECDES_INITIALIZER;
  ASSERT_EQ (recdes_allocate_data_area (&expanded, IO_MAX_PAGE_SIZE), NO_ERROR);
  HEAP_SCANCACHE scan;
  ASSERT_EQ (heap_scancache_start (thread_p, &scan, &hfid, &class_oid, false, nullptr), NO_ERROR);
  SCAN_CODE fetched = heap_get_visible_version (thread_p, &insertion.res_oid, &class_oid, &expanded, &scan, COPY,
		      NULL_CHN, HEAP_RECDES_CONSUME_RAW_BYTES);
  if (fetched == S_DOESNT_FIT)
    {
      const int required = -expanded.length;
      recdes_free_data_area (&expanded);
      ASSERT_EQ (recdes_allocate_data_area (&expanded, required), NO_ERROR);
      fetched = heap_get_visible_version (thread_p, &insertion.res_oid, &class_oid, &expanded, &scan, COPY,
					  NULL_CHN, HEAP_RECDES_CONSUME_RAW_BYTES);
    }
  ASSERT_EQ (fetched, S_SUCCESS);
  EXPECT_EQ (locator_copyarea_add_fetch (&class_oid, &insertion.res_oid, &expanded, 0, &exported, &descriptor), NO_ERROR);
  EXPECT_EQ (exported.num_objs, 2);
  recdes_free_data_area (&expanded);
  ASSERT_EQ (heap_scancache_end (thread_p, &scan), NO_ERROR);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
}

TEST (OosSqlPacking, GenericDescriptorsPreserveArbitraryBytes)
{
  const char raw[] = { '\xFF', '\x00', '\xAB', '\xCD', '\xEF' };
  record_descriptor source (raw, sizeof (raw));
  char packed[128];
  cubpacking::packer packer (packed, sizeof (packed));
  source.pack (packer);
  cubpacking::unpacker unpacker (packed, packer.get_current_size ());
  record_descriptor restored;
  restored.unpack (unpacker);
  EXPECT_EQ (restored.get_recdes ().type, source.get_recdes ().type);
  EXPECT_EQ (restored.get_recdes ().length, sizeof (raw));
  EXPECT_EQ (std::string (restored.get_recdes ().data, restored.get_recdes ().length), std::string (raw, sizeof (raw)));
}

TEST_F (OosSqlDeferredWrite, FinalizationResetsPublicationOnceAndFailureCannotRetry)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (a BIT VARYING STORAGE FORCE_OUTLINE)"), 0);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  ASSERT_NE (tdes, nullptr);
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  OID class_oid = *db_identifier (cls);
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
  heap_pending_record inline_row;
  ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &inline_row), S_SUCCESS);
  RECDES inline_record = inline_row.get_recdes ();
  std::string bytes (50000, '\xAB');
  DB_VALUE value;
  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), bytes.size () * 8);
  ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "a")), &value, &attrs), NO_ERROR);
  heap_pending_record first;
  ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &first), S_SUCCESS);
  RECDES first_record = first.get_recdes ();
  ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &first_record, &first), NO_ERROR);
  ASSERT_EQ (thread_p->oos_oids.size (), 1u);
  // Standalone does not emit replication LSAs; seed the existing publication
  // queue to verify that the paired reset also clears it.
  if (tdes->oos_insert_lsa_queue.is_empty ())
    {
      tdes->oos_insert_lsa_queue.push (thread_p->oos_oids.front ().identity_stamp);
    }
  ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &inline_record, &inline_row), NO_ERROR);
  EXPECT_TRUE (thread_p->oos_oids.empty ());
  EXPECT_TRUE (tdes->oos_insert_lsa_queue.is_empty ());

  heap_pending_record next;
  ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &next), S_SUCCESS);
  RECDES next_record = next.get_recdes ();
  ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &next_record, &next), NO_ERROR);
  ASSERT_EQ (thread_p->oos_oids.size (), 1u);
  const OID published = thread_p->oos_oids.front ().oid;
  if (tdes->oos_insert_lsa_queue.is_empty ())
    {
      tdes->oos_insert_lsa_queue.push (thread_p->oos_oids.front ().identity_stamp);
    }
  const std::size_t published_lsas = tdes->oos_insert_lsa_queue.size ();
  ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &inline_record, &inline_row), NO_ERROR);
  ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &first_record, &first), NO_ERROR);
  ASSERT_EQ (thread_p->oos_oids.size (), 1u);
  EXPECT_TRUE (OID_EQ (&published, &thread_p->oos_oids.front ().oid));
  EXPECT_EQ (tdes->oos_insert_lsa_queue.size (), published_lsas);

#if defined(CUBRID_UNIT_TEST_ENABLED)
  heap_pending_record failed;
  ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &failed), S_SUCCESS);
  RECDES failed_record = failed.get_recdes ();
  heap_oos_test_fail_before_vfid_lookup_once ();
  ASSERT_NE (heap_oos_finalize_record (thread_p, &class_oid, &failed_record, &failed), NO_ERROR);
  er_clear ();
  EXPECT_NE (heap_oos_finalize_record (thread_p, &class_oid, &failed_record, &failed), NO_ERROR);
  EXPECT_TRUE (thread_p->oos_oids.empty ());
  EXPECT_TRUE (tdes->oos_insert_lsa_queue.is_empty ());
  er_clear ();
#endif
  heap_attrinfo_end (thread_p, &attrs);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
}

TEST_F (OosSqlDeferredWrite, SerializedPreparationPreservesMvccAndOutlivesSource)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT DEFAULT 7, "
		       "k VARCHAR(80) DEFAULT 'abcdefghijklmnopqrstuvwxyz' STORAGE FORCE_OUTLINE, "
		       "data_col BIT VARYING)"), 0);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  OID class_oid = *db_identifier (cls);
  heap_pending_record adapted;
  MVCC_REC_HEADER expected = MVCC_REC_HEADER_INITIALIZER;
  {
    HEAP_CACHE_ATTRINFO attrs;
    ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
    std::string payload (50000, '\xED');
    DB_VALUE value;
    db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, payload.data (), payload.size () * 8);
    ASSERT_EQ (heap_attrinfo_set (nullptr, db_attribute_id (db_get_attribute (cls, "data_col")), &value, &attrs),
	       NO_ERROR);
    heap_pending_record source;
    ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &source), S_SUCCESS);
    RECDES source_record = source.get_recdes ();
    heap_attrinfo_end (thread_p, &attrs);
    ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &source_record, &source), NO_ERROR);
    ASSERT_EQ (or_mvcc_get_header (&source_record, &expected), NO_ERROR);
    expected.mvcc_flag |= OR_MVCC_FLAG_VALID_INSID | OR_MVCC_FLAG_VALID_DELID | OR_MVCC_FLAG_VALID_PREV_VERSION;
    expected.mvcc_ins_id = 101;
    expected.mvcc_del_id = 202;
    expected.prev_version_lsa.pageid = 303;
    expected.prev_version_lsa.offset = 4;
    ASSERT_EQ (or_mvcc_set_header (&source_record, &expected), NO_ERROR);
    const std::string original (source_record.data, source_record.length);
    ASSERT_EQ (heap_prepare_oos_record (thread_p, &class_oid, &source_record, &adapted), NO_ERROR);
    EXPECT_EQ (std::string (source_record.data, source_record.length), original);
  }
  heap_pending_record moved (std::move (adapted));
  RECDES moved_record = moved.get_recdes ();
  ASSERT_EQ (heap_oos_finalize_record (thread_p, &class_oid, &moved_record, &moved), NO_ERROR);
  MVCC_REC_HEADER actual;
  ASSERT_EQ (or_mvcc_get_header (&moved_record, &actual), NO_ERROR);
  EXPECT_EQ ((int) actual.mvcc_flag, (int) expected.mvcc_flag);
  EXPECT_EQ (actual.mvcc_ins_id, 101);
  EXPECT_EQ (actual.mvcc_del_id, 202);
  EXPECT_EQ (actual.prev_version_lsa.pageid, 303);
  EXPECT_EQ (actual.prev_version_lsa.offset, 4);
  HEAP_CACHE_ATTRINFO attrs;
  ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
  ASSERT_EQ (heap_attrinfo_read_dbvalues (thread_p, &class_oid, &moved_record, &attrs), NO_ERROR);
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
TEST_F (OosSqlDeferredWrite, RedistributionFailurePreservesSourceAndNextOperation)
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
	  heap_oos_test_fail_preparation_once ();
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

TEST_F (OosSqlDeferredWrite, InternalAndAddressReservationsBypassPreparation)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("CREATE SERIAL t_oos_ticket16_serial START WITH 1"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  OID cls = *db_identifier (db_find_class ("t_oos_show_yes"));
  HFID hfid;
  ASSERT_EQ (heap_get_class_hfid (thread_p, &cls, &hfid, nullptr), NO_ERROR);
  heap_oos_test_fail_preparation_once ();
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

TEST_F (OosSqlDeferredWrite, ForcedOutlineKeyRoutesFromPreparedBytes)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part ("
		       "k VARCHAR(80) DEFAULT 'abcdefghijklmnopqrstuvwxyz' STORAGE FORCE_OUTLINE, "
		       "payload BIT VARYING) PARTITION BY RANGE(k) ("
		       "PARTITION p0 VALUES LESS THAN ('m'), PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part(payload) VALUES(REPEAT(X'AB', 40000))"), 0);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE k = 'abcdefghijklmnopqrstuvwxyz' "
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

TEST_F (OosSqlDeferredWrite, RejectedDestinationCreatesNoOosAndNextInsertSucceeds)
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

TEST_F (OosSqlDeferredWrite, LobPreparationPreservesSourceAndDestinationValues)
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

TEST_F (OosSqlDeferredWrite, SupportedPartitionDomainsPreserveValuesAndOwnership)
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

TEST_F (OosSqlDeferredWrite, LoaderQueueRetainsClearedInputsAndRollsBackBulkFailure)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT, payload BIT VARYING)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("t_oos_show_yes");
  OID class_oid = *db_identifier (cls);
  HFID hfid;
  ASSERT_EQ (heap_get_class_hfid (thread_p, &class_oid, &hfid, nullptr), NO_ERROR);
  ATTR_ID id_attr = db_attribute_id (db_get_attribute (cls, "id"));
  ATTR_ID payload_attr = db_attribute_id (db_get_attribute (cls, "payload"));
  const std::string payload (50000, static_cast<char> (0xab));
  for (int attempt = 0; attempt < 2; ++attempt)
    {
      std::vector<heap_pending_record> rows;
      HEAP_CACHE_ATTRINFO attrs;
      ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, nullptr, &attrs), NO_ERROR);
      for (int id = 0; id < 64; ++id)
	{
	  DB_VALUE value;
	  db_make_int (&value, id);
	  ASSERT_EQ (heap_attrinfo_set (nullptr, id_attr, &value, &attrs), NO_ERROR);
	  db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, payload.data (), payload.size () * 8);
	  ASSERT_EQ (heap_attrinfo_set (nullptr, payload_attr, &value, &attrs), NO_ERROR);

	  heap_pending_record row;
	  ASSERT_EQ (heap_attrinfo_prepare_record (thread_p, &attrs, nullptr, &row, false), S_SUCCESS);
	  EXPECT_GE (row.retained_bytes (), payload.size ());
	  EXPECT_LT (row.get_recdes ().length, 1000);
	  rows.push_back (std::move (row));
	  db_value_clear (&value);
	  heap_attrinfo_clear_dbvalues (&attrs);
	}
      heap_attrinfo_end (thread_p, &attrs);
      HEAP_SCANCACHE cache;
      ASSERT_EQ (heap_scancache_start_modify (thread_p, &cache, &hfid, &class_oid, MULTI_ROW_INSERT, nullptr), NO_ERROR);
      int force_count = 0;
#if defined(CUBRID_UNIT_TEST_ENABLED)
      if (attempt == 0)
	{
	  heap_oos_test_fail_before_vfid_lookup_once ();
	}
#endif
      int error = locator_multi_insert_force (thread_p, &hfid, &class_oid, rows, false, MULTI_ROW_INSERT,
					      &cache, &force_count, DB_NOT_PARTITIONED_CLASS, nullptr, nullptr,
					      UPDATE_INPLACE_NONE, true);
      heap_scancache_end_modify (thread_p, &cache);
#if defined(CUBRID_UNIT_TEST_ENABLED)
      if (attempt == 0)
	{
	  EXPECT_NE (error, NO_ERROR);
	  EXPECT_TRUE (thread_p->oos_oids.empty ());
	  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
	  int count = -1;
	  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes", &count), NO_ERROR);
	  EXPECT_EQ (count, 0);
	  continue;
	}
#endif
      ASSERT_EQ (error, NO_ERROR);
      int matches = 0;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE "
				   "payload=CAST(REPEAT('AB',50000) AS BIT VARYING)", &matches), NO_ERROR);
      EXPECT_EQ (matches, 64);
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
    }
}



#if defined(CUBRID_UNIT_TEST_ENABLED)
TEST_F (OosSqlDeferredWrite, AllocationAndStorageFailureLeaveNextInsertUsable)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (id INT PRIMARY KEY, payload BIT VARYING, payload2 BIT VARYING)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  for (auto boundary :
       {
	       write_failure::preparation, write_failure::vfid_lookup,
	       write_failure::partial_batch, write_failure::heap_insert
       })
    {
      arm_write_failure (boundary);
      EXPECT_LT (exec_sql ("INSERT INTO t_oos_show_yes VALUES(1, REPEAT(X'AB', 8192), REPEAT(X'EF', 8192))"), 0);
      EXPECT_TRUE (thread_get_thread_entry_info ()->oos_oids.empty ());
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

TEST_F (OosSqlDeferredWrite, ConstraintFailureAfterOosAllowsNextInsert)
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

TEST_F (OosSqlDeferredWrite, DuplicateProbesDoNotPersistCandidateValues)
{
  /* FORCE_OUTLINE keys must remain larger than the 24-byte OOS inline stub. */
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
			? "REPLACE INTO t_oos_show_part VALUES('abcdefghijklmnopqrstuvwxyz', REPEAT(X'AB', 8192))"
			: "INSERT INTO t_oos_show_part VALUES('abcdefghijklmnopqrstuvwxyz', REPEAT(X'AB', 8192)) "
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
				 ? "SELECT COUNT(*) FROM t_oos_show_part WHERE k = 'abcdefghijklmnopqrstuvwxyz' "
				 "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING)"
				 : "SELECT COUNT(*) FROM t_oos_show_part WHERE k = 'abcdefghijklmnopqrstuvwxyz' "
				 "AND payload = CAST(REPEAT(X'AB', 8192) AS BIT VARYING)";
	  ASSERT_EQ (fetch_single_int (readback, &count), NO_ERROR);
	  EXPECT_EQ (count, 1);
	}
      ASSERT_GE (exec_sql ("DROP TABLE t_oos_show_part"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

TEST_F (OosSqlDeferredWrite, DuplicateProbesReadCompositeKeys)
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
			? "REPLACE INTO t_oos_show_yes VALUES('abcdefghijklmnopqrstuvwxyz', 1, REPEAT(X'AB', 8192))"
			: "INSERT INTO t_oos_show_yes VALUES('abcdefghijklmnopqrstuvwxyz', 1, REPEAT(X'AB', 8192)) "
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

TEST_F (OosSqlDeferredWrite, DuplicateProbesPreserveFunctionIndexesAndCompressedCompositeKeys)
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
TEST_F (OosSqlDeferredWrite, AbandonedDuplicateCandidateDoesNotWriteOos)
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

TEST_F (OosSqlDeferredWrite, DuplicateProbeFailuresLeaveNextWriteUsable)
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
      for (auto boundary :
	   {
		   write_failure::preparation, write_failure::vfid_lookup,
		   write_failure::partial_batch
	   })
	{
	  SCOPED_TRACE (replace);
	  SCOPED_TRACE (static_cast<int> (boundary));
	  arm_write_failure (boundary);
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

TEST_F (OosSqlDeferredWrite, DuplicateProbesPreserveLobValuesAndRollback)
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

TEST_F (OosSqlDeferredWrite, ReplaceProbeReadsOutlinedCandidateAgainstInlineExistingKey)
{
  /* Keep the old key inline: standalone DELETE's baseline eager cleanup precedes index-key
   * reading for already-outlined old keys. Only the new candidate uses FORCE_OUTLINE here. */
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_yes (k VARCHAR(80), b INT, payload BIT VARYING, UNIQUE(k, b))"), 0);
  ASSERT_EQ (exec_sql ("INSERT INTO t_oos_show_yes VALUES('abcdefghijklmnopqrstuvwxyz', 1, X'AB')"), 1);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  ASSERT_GE (exec_sql ("ALTER TABLE t_oos_show_yes MODIFY k VARCHAR(80) STORAGE FORCE_OUTLINE"), 0);
#if defined(CUBRID_UNIT_TEST_ENABLED)
  bridge_oos_debug_counters_reset ();
#endif
  ASSERT_EQ (exec_sql ("REPLACE INTO t_oos_show_yes VALUES('abcdefghijklmnopqrstuvwxyz', 1, REPEAT(X'CD', 8192))"), 2)
      << db_error_string (1);
#if defined(CUBRID_UNIT_TEST_ENABLED)
  EXPECT_EQ (bridge_oos_debug_counters_get ().insert_many_requests, 2U);
#endif
  int count = -1;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_yes WHERE k = 'abcdefghijklmnopqrstuvwxyz' AND b = 1 "
			       "AND payload = CAST(REPEAT(X'CD', 8192) AS BIT VARYING)", &count), NO_ERROR);
  EXPECT_EQ (count, 1);
}

TEST_F (OosSqlDeferredWrite, DuplicateProbesPreserveMultipleUniqueConstraintsAndForeignKeys)
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

TEST_F (OosSqlDeferredWrite, UpdateMovementAllocatesOnlyAtDestination)
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

TEST_F (OosSqlDeferredWrite, UpdateDomainsPreserveUnassignedValuesThroughMovementAndRollback)
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

TEST_F (OosSqlDeferredWrite, UpdateForcedKeyUsesCanonicalValueAndRejectsWrongPartition)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (k VARCHAR(80) DEFAULT 'abcdefghijklmnopqrstuvwxyz' "
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
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p0 WHERE k = 'abcdefghijklmnopqrstuvwxyz'",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
}

TEST_F (OosSqlDeferredWrite, UpdateLayoutGrowthAndLobOverwritePreserveValues)
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
TEST_F (OosSqlDeferredWrite, UpdateFailureClearsPublicationAndRollsBackBothDestinations)
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
      for (auto boundary :
	   {
		   write_failure::preparation, write_failure::vfid_lookup,
		   write_failure::partial_batch
	   })
	{
	  SCOPED_TRACE (target);
	  SCOPED_TRACE (static_cast<int> (boundary));
	  arm_write_failure (boundary);
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

TEST_F (OosSqlDeferredWrite, UpdateIndexFailureRollsBackMovementAndNonmovement)
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

TEST_F (OosSqlDeferredWrite, NonpartitionedUpdateChecksForeignKeysAfterFinalization)
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

TEST_F (OosSqlDeferredWrite, RollbackPreservesMultiChunkValuesAndLiveOwnership)
{
  ASSERT_GE (exec_sql ("CREATE TABLE t_oos_show_part (id INT PRIMARY KEY, a BIT VARYING, b BIT VARYING) "
		       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
		       "PARTITION p1 VALUES LESS THAN MAXVALUE)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES "
		       "(1, CAST(REPEAT('AB',50000) AS BIT VARYING), CAST(REPEAT('CD',6000) AS BIT VARYING)), "
		       "(2, CAST(REPEAT('EF',50000) AS BIT VARYING), CAST(REPEAT('01',6000) AS BIT VARYING))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  const char *queries[] = { "SHOW HEAP OOS OF t_oos_show_part", "SHOW HEAP OOS OF t_oos_show_part__p__p0",
			    "SHOW HEAP OOS OF t_oos_show_part__p__p1"
			  };
  int original_chunks[3];
  for (int i = 0; i < 3; ++i)
    {
      DB_QUERY_RESULT *stats = nullptr;
      ASSERT_EQ (show_heap_oos_query (queries[i], &stats), NO_ERROR);
      ASSERT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &original_chunks[i]), NO_ERROR);
      db_query_end (stats);
    }
  ASSERT_EQ (original_chunks[0], 0);
  ASSERT_GT (original_chunks[1], 4);
  ASSERT_EQ (original_chunks[2], 0);
  for (bool duplicate :
       {
	       false, true
       })
    {
      SCOPED_TRACE (duplicate);
      if (duplicate)
	{
	  EXPECT_LT (exec_sql ("UPDATE t_oos_show_part SET id=2, a=CAST(REPEAT('23',50000) AS BIT VARYING) "
			       "WHERE id=1"), 0);
	}
      else
	{
	  ASSERT_GE (exec_sql ("UPDATE t_oos_show_part SET id=11, a=CAST(REPEAT('23',50000) AS BIT VARYING) "
			       "WHERE id=1"), 0);
	  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(12, CAST(REPEAT('45',50000) AS BIT VARYING), NULL)"), 0);
	}
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
      int matches = 0;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part WHERE "
				   "(id=1 AND a=CAST(REPEAT('AB',50000) AS BIT VARYING) "
				   "AND b=CAST(REPEAT('CD',6000) AS BIT VARYING)) OR "
				   "(id=2 AND a=CAST(REPEAT('EF',50000) AS BIT VARYING) "
				   "AND b=CAST(REPEAT('01',6000) AS BIT VARYING))", &matches), NO_ERROR);
      EXPECT_EQ (matches, 2);
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part", &matches), NO_ERROR);
      EXPECT_EQ (matches, 2);
      for (int i = 0; i < 3; ++i)
	{
	  DB_QUERY_RESULT *stats = nullptr;
	  ASSERT_EQ (show_heap_oos_query (queries[i], &stats), NO_ERROR);
	  ASSERT_EQ (get_int_column (stats, COL_OOS_NUM_RECS, &matches), NO_ERROR);
	  EXPECT_EQ (matches, original_chunks[i]);
	  db_query_end (stats);
	}
      /* The next ordinary operation must reset the previous successful write's publication. */
      ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(3, X'AB', NULL)"), 0);
      EXPECT_TRUE (thread_get_thread_entry_info ()->oos_oids.empty ());
      ASSERT_EQ (db_abort_transaction (), NO_ERROR);
    }
  ASSERT_GE (exec_sql ("INSERT INTO t_oos_show_part VALUES(12, CAST(REPEAT('67',50000) AS BIT VARYING), NULL)"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int matches = 0;
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM t_oos_show_part__p__p1 "
			       "WHERE id=12 AND a=CAST(REPEAT('67',50000) AS BIT VARYING) AND b IS NULL",
			       &matches), NO_ERROR);
  EXPECT_EQ (matches, 1);
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
