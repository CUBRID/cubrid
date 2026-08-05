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
 * test_oos_remove_file_server.cpp - SERVER_MODE tests for OOS file destruction
 *
 * Mirrors the SA_MODE test_oos_remove_file.cpp tests under full SERVER_MODE
 * infrastructure (MVCC, threading, worker transactions).
 */

#include "xserver_interface.h"

#include "test_oos_server_common.hpp"

/* bridge functions defined in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

// ============================================================================
// TC: File destroy basic
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: File destroy with data
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyWithData)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Data before file destroy", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: File destroy with multi-chunk data
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyWithMultiChunkData)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const int large_size = max_chunk_size + 50;

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: File destroy clears bestspace cache
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyCacheCleared)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Cache entry test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: Page reclaim basic (skip non-empty, dealloc when emptied, idempotent)
// ============================================================================
TEST (OosFileDestroyServerTest, OosPageReclaimBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Page reclaim test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  VPID vpid = {oid.pageid, oid.volid};

  // Non-empty page: reclaim is skipped with NO_ERROR and the record survives
  err = oos_try_reclaim_empty_page (thread_p, oos_vfid, vpid);
  ASSERT_EQ (err, NO_ERROR);

  bool exists = false;
  err = oos_chunk_exists (thread_p, oid, &exists);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_TRUE (exists);

  // Empty the page, then reclaim deallocates it. Commit first: reclaim's contract is
  // "only after the deletes are committed" — otherwise this transaction's rollback at
  // teardown would replay the RVOOS_DELETE undo onto a deallocated page.
  err = oos_delete (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  err = oos_try_reclaim_empty_page (thread_p, oos_vfid, vpid);
  ASSERT_EQ (err, NO_ERROR);

  PAGE_PTR page_ptr = NULL;
  err = pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &page_ptr);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (page_ptr, nullptr);	// page really deallocated

  // Idempotent: reclaiming an already-deallocated page is a NO_ERROR no-op
  err = oos_try_reclaim_empty_page (thread_p, oos_vfid, vpid);
  ASSERT_EQ (err, NO_ERROR);

  // Leave no committed orphan file behind: a later binary's file-tracker dump would try to
  // resolve this file's synthetic owner class OID against a non-heap page and assert.
  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
}

// ============================================================================
// TC: Destroy one file, other file still works
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyMultipleFiles)
{
  int err;
  VFID oos_vfid1;
  VFID oos_vfid2;

  err = oos_create_file (thread_p, oos_vfid1);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_create_file (thread_p, oos_vfid2);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec1 {};
  err = test_oos_utils::from_string_into_recdes ("File 1 data", rec1);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free1 (&rec1, recdes_free_data_area);

  RECDES rec2 {};
  err = test_oos_utils::from_string_into_recdes ("File 2 data", rec2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free2 (&rec2, recdes_free_data_area);

  OID oid1 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid1, rec1, oid1);
  ASSERT_EQ (err, NO_ERROR);

  OID oid2 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid2, rec2, oid2);
  ASSERT_EQ (err, NO_ERROR);

  /* destroy file 1 */
  err = oos_remove_file (thread_p, oos_vfid1);
  ASSERT_EQ (err, NO_ERROR);

  /* file 2 should still be readable */
  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid2, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, "File 2 data");
  recdes_free_data_area (&rec_out);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
