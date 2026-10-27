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

#include <array>
#include <string>

#include "heap_oos.hpp"
#include "object_representation.h"
#include "schema_manager.h"
#include "locator_cl.h"
#include "test_oos_sql_common.hpp"
#include "xserver_interface.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  struct stored_ref
  {
    OID row_oid;
    oos_chain_ref chain;
    DB_BIGINT full_length;
    int chn;
  };

  void fetch_stored_ref (stored_ref &out, int expected_id = 1)
  {
    THREAD_ENTRY *thread = thread_get_thread_entry_info ();
    DB_OBJECT *table = db_find_class ("t_oos_ownership");
    ASSERT_NE (table, nullptr);
    OID class_oid = *reinterpret_cast<OID *> (db_identifier (table));
    HFID hfid;
    FILE_TYPE file_type;
    ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, &file_type), NO_ERROR);
    HEAP_SCANCACHE cache;
    ASSERT_EQ (heap_scancache_start (thread, &cache, &hfid, &class_oid, true, nullptr), NO_ERROR);
    struct scan_end
    {
      THREAD_ENTRY *thread;
      HEAP_SCANCACHE *cache;
      ~scan_end ()
      {
	heap_scancache_end (thread, cache);
      }
    } end {thread, &cache};
    std::array<char, 1024> bytes;
    RECDES record = {static_cast<int> (bytes.size ()), 0, REC_HOME, bytes.data ()};
    OID_SET_NULL (&out.row_oid);
    ASSERT_EQ (heap_next (thread, &hfid, &class_oid, &out.row_oid, &record, &cache, COPY,
			  HEAP_RECDES_DONT_CONSUME_RAW_BYTES), S_SUCCESS);
    ASSERT_EQ (record.type, REC_HOME);
    ASSERT_TRUE (OR_RECORD_HAS_OOS (record.data));
    ASSERT_EQ (OR_GET_INT (record.data + OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL
			   (record.data, 1, OR_GET_OFFSET_SIZE (record.data))), expected_id);
    ASSERT_EQ (heap_oos_parse_inline_ref (&record, 0, &out.chain, &out.full_length), NO_ERROR);
    out.chn = OR_GET_MVCC_CHN (record.data);
    OID next = out.row_oid;
    ASSERT_EQ (heap_next (thread, &hfid, &class_oid, &next, &record, &cache, COPY,
			  HEAP_RECDES_DONT_CONSUME_RAW_BYTES), S_END);
  }

  struct page_algorithms
  {
    TDE_ALGORITHM expected;
    int count = 0;
  };

  int check_page_algorithm (THREAD_ENTRY *thread, PAGE_PTR *page, bool *stop, void *args)
  {
    (void) thread;
    (void) stop;
    page_algorithms *observed = static_cast<page_algorithms *> (args);
    EXPECT_EQ (pgbuf_get_tde_algorithm (*page), observed->expected);
    ++observed->count;
    return NO_ERROR;
  }
}

class OosSqlOwnership : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      ASSERT_GE (exec_sql ("DROP TABLE IF EXISTS t_oos_ownership"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      ASSERT_GE (exec_sql ("CREATE TABLE t_oos_ownership (id INT PRIMARY KEY, payload BIT VARYING)"), 0);
      ASSERT_GE (exec_sql ("INSERT INTO t_oos_ownership VALUES "
			   "(1, REPEAT(X'A1', 17001) || REPEAT(X'B2', 17003) || REPEAT(X'C3', 17003))"), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }

    void TearDown () override
    {
      db_abort_transaction ();
      EXPECT_GE (exec_sql ("DROP TABLE IF EXISTS t_oos_ownership"), 0);
      EXPECT_EQ (db_commit_transaction (), NO_ERROR);
    }

    void expect_model (int id)
    {
      const std::string query = "SELECT COUNT(*) FROM t_oos_ownership WHERE id="
				+ std::to_string (id) + " AND payload = CAST((REPEAT(X'A1', 17001) || REPEAT(X'B2', 17003)"
				" || REPEAT(X'C3', 17003)) AS BIT VARYING)";
      int matches = 0;
      ASSERT_EQ (fetch_single_int (query.c_str (), &matches), NO_ERROR) << db_error_string (1);
      EXPECT_EQ (matches, 1);
    }
};

// The accepted ownership contract reuses the entire unassigned stub. Equal resolved values do
// not prove reuse. SA eager cleanup must keep that same chain, and abort must restore the row.
TEST_F (OosSqlOwnership, UnassignedUpdateRetainsWholeStubAcrossCommitAndAbort)
{
  stored_ref before;
  fetch_stored_ref (before);
  ASSERT_FALSE (HasFatalFailure ());
  ASSERT_GT (before.full_length, 51007);
  expect_model (1);
  ASSERT_GE (exec_sql ("UPDATE t_oos_ownership SET id=2 WHERE id=1"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  stored_ref committed;
  fetch_stored_ref (committed, 2);
  ASSERT_FALSE (HasFatalFailure ());
  EXPECT_TRUE (OID_EQ (&before.chain.head_oid, &committed.chain.head_oid));
  EXPECT_TRUE (LSA_EQ (&before.chain.identity_stamp, &committed.chain.identity_stamp));
  EXPECT_EQ (before.full_length, committed.full_length);
  printf ("unassigned UPDATE before=%d|%d|%d stamp=%lld|%d after=%d|%d|%d stamp=%lld|%d\n",
	  before.chain.head_oid.volid, before.chain.head_oid.pageid, before.chain.head_oid.slotid,
	  (long long) before.chain.identity_stamp.pageid, before.chain.identity_stamp.offset,
	  committed.chain.head_oid.volid, committed.chain.head_oid.pageid, committed.chain.head_oid.slotid,
	  (long long) committed.chain.identity_stamp.pageid, committed.chain.identity_stamp.offset);
  expect_model (2);
  ASSERT_GE (exec_sql ("UPDATE t_oos_ownership SET payload=REPEAT(X'D4', 54007), id=3 WHERE id=2"), 0);
  ASSERT_EQ (db_abort_transaction (), NO_ERROR);
  stored_ref restored;
  fetch_stored_ref (restored, 2);
  ASSERT_FALSE (HasFatalFailure ());
  EXPECT_TRUE (OID_EQ (&committed.chain.head_oid, &restored.chain.head_oid));
  EXPECT_TRUE (LSA_EQ (&committed.chain.identity_stamp, &restored.chain.identity_stamp));
  EXPECT_EQ (committed.full_length, restored.full_length);
  EXPECT_EQ (committed.chn, restored.chn);
  expect_model (2);
}

// Exercise the same existing module boundary used by SQL class creation, after an ordinary
// class already owns a populated NONE OOS file. This is separate from lazy encrypted creation.
TEST_F (OosSqlOwnership, ExistingOosFileReceivesClassAesAndPreservesIdentityAndValues)
{
  THREAD_ENTRY *thread = thread_get_thread_entry_info ();
  VFID before_vfid;
  ASSERT_TRUE (get_oos_vfid_for_table ("t_oos_ownership", &before_vfid));
  ASSERT_FALSE (VFID_ISNULL (&before_vfid));
  TDE_ALGORITHM algorithm;
  ASSERT_EQ (file_get_tde_algorithm (thread, &before_vfid, PGBUF_UNCONDITIONAL_LATCH, &algorithm), NO_ERROR);
  ASSERT_EQ (algorithm, TDE_ALGORITHM_NONE);
  page_algorithms plain {TDE_ALGORITHM_NONE};
  ASSERT_EQ (file_map_pages (thread, &before_vfid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
			     check_page_algorithm, &plain), NO_ERROR);
  ASSERT_GE (plain.count, 4);
  stored_ref before;
  fetch_stored_ref (before);
  ASSERT_FALSE (HasFatalFailure ());
  DB_OBJECT *table = db_find_class ("t_oos_ownership");
  ASSERT_NE (table, nullptr);
  ASSERT_EQ (sm_set_class_tde_algorithm (table, TDE_ALGORITHM_AES), NO_ERROR);
  ASSERT_EQ (locator_flush_class (table), NO_ERROR);
  const OID class_oid = *reinterpret_cast<OID *> (db_identifier (table));
  ASSERT_EQ (xfile_apply_tde_to_class_files (thread, &class_oid), NO_ERROR);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  // Flush and reopen through the real storage path before inspecting persisted file/page flags.
  ASSERT_EQ (db_shutdown (), NO_ERROR);
  ASSERT_EQ (db_restart ("unit_test", TRUE, "unittestdb"), NO_ERROR);
  thread = thread_get_thread_entry_info ();
  VFID after_vfid;
  ASSERT_TRUE (get_oos_vfid_for_table ("t_oos_ownership", &after_vfid));
  EXPECT_TRUE (VFID_EQ (&before_vfid, &after_vfid));
  ASSERT_EQ (file_get_tde_algorithm (thread, &after_vfid, PGBUF_UNCONDITIONAL_LATCH, &algorithm), NO_ERROR);
  EXPECT_EQ (algorithm, TDE_ALGORITHM_AES);
  page_algorithms encrypted {TDE_ALGORITHM_AES};
  ASSERT_EQ (file_map_pages (thread, &after_vfid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
			     check_page_algorithm, &encrypted), NO_ERROR);
  EXPECT_EQ (encrypted.count, plain.count);
  stored_ref after;
  fetch_stored_ref (after);
  ASSERT_FALSE (HasFatalFailure ());
  EXPECT_TRUE (OID_EQ (&before.chain.head_oid, &after.chain.head_oid));
  EXPECT_TRUE (LSA_EQ (&before.chain.identity_stamp, &after.chain.identity_stamp));
  EXPECT_EQ (before.full_length, after.full_length);
  EXPECT_EQ (before.chn, after.chn);
  expect_model (1);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
