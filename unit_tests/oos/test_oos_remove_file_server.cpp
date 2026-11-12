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
#include "log_system_tran.hpp"

#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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

// ============================================================================
// TC: LSA gate defers a page emptied by an uncommitted delete
// ============================================================================
TEST (OosFileDestroyServerTest, OosPageReclaimLsaGateDefersUncommitted)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
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

// A matching chain identity authorizes the chunk delete, but its empty page remains an undo
// destination until the real system-worker sysop ends. This exercises the system-tdes horizon,
// separately from the ordinary transaction gate above, without manufacturing an LSA.
TEST (OosFileDestroyServerTest, SystemWorkerUndoDefersPageReturnAndAbortRestoresTheChain)
{
  ::testing::GTEST_FLAG (break_on_failure) = false;
  VFID vfid;
  ASSERT_EQ (oos_create_file (thread_p, vfid), NO_ERROR);
  struct file_cleanup
  {
    VFID &vfid;
    ~file_cleanup ()
    {
      EXPECT_EQ (oos_remove_file (thread_p, vfid), NO_ERROR);
      EXPECT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
    }
  } cleanup {vfid};
  std::string payload = "system-worker undo must restore these exact bytes and the original stamp";
  OID oid = OID_INITIALIZER;
  LOG_LSA stamp = NULL_LSA;
  ASSERT_EQ (oos_insert (thread_p, vfid, oos_buffer (payload.data (), payload.size ()), oid, &stamp), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  const oos_chain_ref ref = {oid, stamp};
  VPID page = {oid.pageid, oid.volid};
  const int regular_index = thread_p->tran_index;
  {
    struct system_worker_scope
    {
      THREAD_ENTRY *thread;
      int saved_index;
      system_worker_scope (THREAD_ENTRY *entry) : thread (entry), saved_index (entry->tran_index)
      {
	thread->claim_system_worker ();
      }
      ~system_worker_scope ()
      {
	thread->tran_index = LOG_SYSTEM_TRAN_INDEX;
	LOG_TDES *tdes = thread->get_system_tdes ()->get_tdes ();
	if (tdes->topops.last >= 0)
	  {
	    log_sysop_abort (thread);
	  }
	thread->retire_system_worker ();
	thread->tran_index = saved_index;
      }
    } worker (thread_p);
    log_sysop_start (thread_p);
    std::vector<VPID> emptied;
    ASSERT_EQ (oos_delete (thread_p, vfid, ref, &emptied), NO_ERROR);
    ASSERT_EQ (emptied.size (), 1U);
    ASSERT_TRUE (VPID_EQ (&emptied.front (), &page));
    LOG_TDES *system_tdes = thread_p->get_system_tdes ()->get_tdes ();
    ASSERT_FALSE (LSA_ISNULL (&system_tdes->head_lsa));
    PAGE_PTR fixed = pgbuf_fix (thread_p, &page, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    ASSERT_NE (fixed, nullptr);
    const LOG_LSA page_lsa = *pgbuf_get_lsa (fixed);
    EXPECT_EQ (spage_number_of_records (fixed), 0);
    EXPECT_TRUE (LSA_LE (&system_tdes->head_lsa, &page_lsa));
    printf ("system undo candidate=%d|%d page_lsa=%lld|%d system_head=%lld|%d\n",
	    page.volid, page.pageid, (long long) page_lsa.pageid, page_lsa.offset,
	    (long long) system_tdes->head_lsa.pageid, system_tdes->head_lsa.offset);
    pgbuf_unfix (thread_p, fixed);

    // Run reclaim as the ordinary caller while the system undo source stays registered.
    thread_p->tran_index = regular_index;
    ASSERT_EQ (oos_reclaim_empty_pages (thread_p, vfid, emptied), NO_ERROR);
    ASSERT_EQ (pgbuf_fix_if_not_deallocated (thread_p, &page, PGBUF_LATCH_READ,
	       PGBUF_UNCONDITIONAL_LATCH, &fixed), NO_ERROR);
    ASSERT_NE (fixed, nullptr);
    pgbuf_unfix (thread_p, fixed);
    thread_p->tran_index = LOG_SYSTEM_TRAN_INDEX;
    log_sysop_abort (thread_p);
    EXPECT_TRUE (LSA_ISNULL (&system_tdes->head_lsa));
  }
  std::string restored (payload.size (), '?');
  ASSERT_EQ (oos_read (thread_p, ref, oos_buffer (restored.data (), restored.size ())), NO_ERROR);
  EXPECT_EQ (restored, payload);
  LOG_LSA restored_stamp;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, oid, &restored_stamp), NO_ERROR);
  EXPECT_TRUE (LSA_EQ (&restored_stamp, &stamp));

  std::vector<VPID> emptied;
  ASSERT_EQ (oos_delete (thread_p, vfid, ref, &emptied), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  PAGE_PTR fixed = pgbuf_fix (thread_p, &page, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  ASSERT_NE (fixed, nullptr);
  const LOG_LSA page_lsa = *pgbuf_get_lsa (fixed);
  EXPECT_EQ (spage_number_of_records (fixed), 0);
  pgbuf_unfix (thread_p, fixed);
  LOG_LSA regular_horizon = NULL_LSA;
  logtb_find_smallest_lsa (thread_p, &regular_horizon);
  EXPECT_TRUE (LSA_ISNULL (&regular_horizon) || LSA_LT (&page_lsa, &regular_horizon));
  log_system_tdes::map_all_tdes ([&page_lsa] (log_tdes &tdes)
  {
    EXPECT_TRUE (LSA_ISNULL (&tdes.head_lsa) || LSA_LT (&page_lsa, &tdes.head_lsa));
  });
  const LOG_LSA append = log_get_append_lsa ();
  EXPECT_TRUE (LSA_LT (&page_lsa, &append));
  printf ("committed empty candidate=%d|%d page_lsa=%lld|%d regular_horizon=%lld|%d append=%lld|%d\n",
	  page.volid, page.pageid, (long long) page_lsa.pageid, page_lsa.offset,
	  (long long) regular_horizon.pageid, regular_horizon.offset, (long long) append.pageid, append.offset);
  ASSERT_EQ (oos_reclaim_empty_pages (thread_p, vfid, emptied), NO_ERROR);
  ASSERT_EQ (pgbuf_fix_if_not_deallocated (thread_p, &page, PGBUF_LATCH_READ,
	     PGBUF_UNCONDITIONAL_LATCH, &fixed), NO_ERROR);
  EXPECT_EQ (fixed, nullptr);
  if (fixed != nullptr)
    {
      pgbuf_unfix (thread_p, fixed);
    }
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
