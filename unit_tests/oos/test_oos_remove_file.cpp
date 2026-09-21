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

#include "gtest/gtest.h"
#include <cstdio>
#include <cstring>
#include <vector>

#include "file_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "xserver_interface.h"
#include "oos_file.hpp"
#include "test_oos_common.hpp"
#include "oos_log.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;

// bridge functions to access static functions in oos_file.cpp
int bridge_oos_get_max_chunk_size_within_page ();


// ===========================================================================
// TEST: OosFileDestroyBasic
//
// Create an OOS file and destroy it. Verify NO_ERROR return.
// ===========================================================================
TEST (OosFileDestroyTest, OosFileDestroyBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: OosFileDestroyWithData
//
// Create file, insert records, then destroy file. Verify NO_ERROR.
// ===========================================================================
TEST (OosFileDestroyTest, OosFileDestroyWithData)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("Data before file destroy", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);
  test_oos_debug ("Inserted oid={vol=%d,page=%d,slot=%d}", oid.volid, oid.pageid, oid.slotid);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: OosFileDestroyWithMultiChunkData
//
// Create file, insert a large multi-chunk record, then destroy file.
// ===========================================================================
TEST (OosFileDestroyTest, OosFileDestroyWithMultiChunkData)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const int large_size = max_chunk_size + 50;

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("Inserted multi-chunk oid={vol=%d,page=%d,slot=%d}", oid.volid, oid.pageid, oid.slotid);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: OosFileDestroyCacheCleared
//
// Create file, insert (so bestspace cache has entry), destroy, verify
// that the file can be destroyed without error (cache entries cleaned).
// ===========================================================================
TEST (OosFileDestroyTest, OosFileDestroyCacheCleared)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("Cache entry test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: OosPageReclaimBasic
//
// Create file, insert to allocate a page, then exercise oos_reclaim_empty_pages.
// ===========================================================================
TEST (OosFileDestroyTest, OosPageReclaimBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("Page reclaim test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  // Get the VPID of the page where the record was inserted
  VPID vpid = {oid.pageid, oid.volid};

  // Non-empty page: the candidate is skipped with NO_ERROR and the record survives
  std::vector<VPID> candidates {vpid};
  err = oos_reclaim_empty_pages (thread_p, oos_vfid, candidates);
  ASSERT_EQ (err, NO_ERROR);

  bool exists = false;
  err = oos_chunk_exists (thread_p, oid, &exists);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_TRUE (exists);

  // Commit before reclaiming: reclaim requires committed deletes, or this transaction's teardown
  // rollback replays the RVOOS_DELETE undo onto a deallocated page. {vpid, vpid} exercises dedupe.
  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  test_oos_debug ("Reclaiming empty page vpid={vol=%d,page=%d}", vpid.volid, vpid.pageid);
  candidates = {vpid, vpid};
  err = oos_reclaim_empty_pages (thread_p, oos_vfid, candidates);
  ASSERT_EQ (err, NO_ERROR);

  PAGE_PTR page_ptr = NULL;
  err = pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (page_ptr, nullptr);	// page really deallocated

  // Idempotent: reclaiming an already-deallocated page is a NO_ERROR no-op
  candidates = {vpid};
  err = oos_reclaim_empty_pages (thread_p, oos_vfid, candidates);
  ASSERT_EQ (err, NO_ERROR);

  // Leave no committed orphan file behind: a later binary's file-tracker dump would try to
  // resolve this file's synthetic owner class OID against a non-heap page and assert.
  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
}

// ===========================================================================
// TEST: OosPageReclaimLsaGateDefersUncommitted
//
// A page emptied by a not-yet-committed delete must not be deallocated: the deleter's abort
// would restore the chunks into it. After the commit, the next reclaim call deallocates it.
// ===========================================================================
TEST (OosFileDestroyTest, OosPageReclaimLsaGateDefersUncommitted)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("LSA gate test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  VPID vpid = {oid.pageid, oid.volid};

  // Deliberately uncommitted: this transaction is still a live undo source.
  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  std::vector<VPID> candidates {vpid};
  err = oos_reclaim_empty_pages (thread_p, oos_vfid, candidates);
  ASSERT_EQ (err, NO_ERROR);

  PAGE_PTR page_ptr = NULL;
  err = pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (page_ptr, nullptr);	// still allocated — deferred, not reclaimed
  pgbuf_unfix (thread_p, page_ptr);

  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  candidates = {vpid};
  err = oos_reclaim_empty_pages (thread_p, oos_vfid, candidates);
  ASSERT_EQ (err, NO_ERROR);

  err = pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (page_ptr, nullptr);	// now really deallocated

  // Leave no committed orphan file behind (see OosPageReclaimBasic).
  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
}

// ===========================================================================
// TEST: OosPageReclaimStickyFirstPage
//
// The sticky first page (OOS_HDR_STATS) must never be deallocated, even if forced.
// ===========================================================================
TEST (OosFileDestroyTest, OosPageReclaimStickyFirstPage)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VPID hdr_vpid = VPID_INITIALIZER;
  err = file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_FALSE (VPID_ISNULL (&hdr_vpid));

  std::vector<VPID> candidates {hdr_vpid};
  err = oos_reclaim_empty_pages (thread_p, oos_vfid, candidates);
  ASSERT_EQ (err, NO_ERROR);

  PAGE_PTR page_ptr = NULL;
  err = pgbuf_fix_if_not_deallocated (thread_p, &hdr_vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (page_ptr, nullptr);	// header page still alive
  pgbuf_unfix (thread_p, page_ptr);
}


// ===========================================================================
// TEST: OosFileDestroyMultipleFiles
//
// Create two OOS files, destroy one, verify the other still works
// (can insert and read).
// ===========================================================================
TEST (OosFileDestroyTest, OosFileDestroyMultipleFiles)
{
  int err;
  VFID oos_vfid1;
  VFID oos_vfid2;

  err = oos_create_file (thread_p, oos_vfid1);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_create_file (thread_p, oos_vfid2);
  ASSERT_EQ (err, NO_ERROR);

  // Insert into both files
  RECDES rec1{};
  err = test_oos_utils::from_string_into_recdes ("File 1 data", rec1);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free1 (&rec1, recdes_free_data_area);

  RECDES rec2{};
  err = test_oos_utils::from_string_into_recdes ("File 2 data", rec2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free2 (&rec2, recdes_free_data_area);

  OID oid1 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid1, rec1, oid1);
  ASSERT_EQ (err, NO_ERROR);

  OID oid2 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid2, rec2, oid2);
  ASSERT_EQ (err, NO_ERROR);

  // Destroy file 1
  err = oos_remove_file (thread_p, oos_vfid1);
  ASSERT_EQ (err, NO_ERROR);

  // File 2 should still be readable
  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid2, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, "File 2 data");
  recdes_free_data_area (&rec_out);
}


int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  ::testing::GTEST_FLAG (break_on_failure) = true;

  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS();
}
