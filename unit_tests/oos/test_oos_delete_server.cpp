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
 * test_oos_delete_server.cpp - SERVER_MODE tests for OOS deletion
 *
 * Mirrors the SA_MODE test_oos_delete.cpp tests under full SERVER_MODE
 * infrastructure (MVCC, threading, worker transactions).
 */

#include "test_oos_server_common.hpp"

/* bridge functions defined in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

// ============================================================================
// Helpers
// ============================================================================

static int
get_free_space_of_oid_page (const OID &oid)
{
  VPID vpid = {oid.pageid, oid.volid};
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				 PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      return -1;
    }
  test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };
  return spage_get_free_space (thread_p, page_ptr);
}

static int
peek_oos_header (const OID &oid, OOS_RECORD_HEADER &header_out)
{
  VPID vpid = {oid.pageid, oid.volid};
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				 PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      return ER_FAILED;
    }
  test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };

  RECDES rec;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, oid.slotid, &rec, PEEK);
  if (code != S_SUCCESS)
    {
      return ER_FAILED;
    }

  if (rec.length < (int) sizeof (OOS_RECORD_HEADER))
    {
      return ER_FAILED;
    }
  std::memcpy (&header_out, rec.data, sizeof (OOS_RECORD_HEADER));
  return NO_ERROR;
}

// ============================================================================
// TC: Basic delete
// ============================================================================
TEST (OosDeleteServerTest, OosDeleteBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Hello, OOS delete test!", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free_rec_in (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);

  int free_before = get_free_space_of_oid_page (oid);
  ASSERT_GE (free_before, 0);

  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_after = get_free_space_of_oid_page (oid);
  ASSERT_GE (free_after, 0);

  ASSERT_GT (free_after, free_before);
}

// ============================================================================
// TC: Delete then read fails
// ============================================================================
TEST (OosDeleteServerTest, OosDeleteThenReadFails)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Record to be deleted", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_out {};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
  ASSERT_NE (read_err, NO_ERROR);

  if (rec_out.data != nullptr)
    {
      recdes_free_data_area (&rec_out);
    }
}

// ============================================================================
// TC: Multi-chunk delete
// ============================================================================
TEST (OosDeleteServerTest, OosDeleteMultiChunk)
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

  OID head_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, head_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* peek the header to find the next chunk OID */
  OOS_RECORD_HEADER head_header {};
  err = peek_oos_header (head_oid, head_header);
  ASSERT_EQ (err, NO_ERROR);

  OID next_oid = head_header.next_chunk_oid;
  ASSERT_NE (next_oid.pageid, NULL_PAGEID);

  int head_free_before = get_free_space_of_oid_page (head_oid);
  int next_free_before = get_free_space_of_oid_page (next_oid);
  ASSERT_GE (head_free_before, 0);
  ASSERT_GE (next_free_before, 0);

  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, head_oid);
  ASSERT_EQ (err, NO_ERROR);

  int head_free_after = get_free_space_of_oid_page (head_oid);
  int next_free_after = get_free_space_of_oid_page (next_oid);

  ASSERT_GT (head_free_after, head_free_before);
  ASSERT_GT (next_free_after, next_free_before);
}

// ============================================================================
// TC: UPDATE pattern — insert old, insert new, delete old, verify new
// ============================================================================
TEST (OosDeleteServerTest, OosUpdatePattern)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const std::string old_data = "old value before update";
  const std::string new_data = "new value after update";

  RECDES rec_old {};
  RECDES rec_new {};
  err = test_oos_utils::from_string_into_recdes (old_data, rec_old);
  ASSERT_EQ (err, NO_ERROR);
  err = test_oos_utils::from_string_into_recdes (new_data, rec_new);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_old (&rec_old, recdes_free_data_area);
  test_oos_utils::auto_freed_recdes_ptr defer_new (&rec_new, recdes_free_data_area);

  OID old_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_old, old_oid);
  ASSERT_EQ (err, NO_ERROR);

  OID new_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_new, new_oid);
  ASSERT_EQ (err, NO_ERROR);

  ASSERT_NE (old_oid.slotid, new_oid.slotid);

  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, old_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* new record must still be readable and unchanged */
  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, new_oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (rec_out.length, rec_new.length);
  ASSERT_STREQ (rec_out.data, new_data.c_str ());
  recdes_free_data_area (&rec_out);

  /* old record must be gone */
  RECDES stale_out {};
  int stale_err = test_oos_utils::oos_read_with_alloc (thread_p, old_oid, stale_out);
  ASSERT_NE (stale_err, NO_ERROR);
  if (stale_out.data != nullptr)
    {
      recdes_free_data_area (&stale_out);
    }
}

// ============================================================================
// TC: Delete restores free space
// ============================================================================
TEST (OosDeleteServerTest, OosDeleteRestoresFreeSpace)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const std::string data = "free space restore test data";
  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  /* first insert establishes the page */
  OID first_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, first_oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_after_first_insert = get_free_space_of_oid_page (first_oid);

  /* insert the record we want to delete */
  RECDES rec_target {};
  err = test_oos_utils::from_string_into_recdes (data, rec_target);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_target (&rec_target, recdes_free_data_area);

  OID target_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_target, target_oid);
  ASSERT_EQ (err, NO_ERROR);

  ASSERT_EQ (first_oid.pageid, target_oid.pageid);

  int free_after_second_insert = get_free_space_of_oid_page (target_oid);
  ASSERT_LT (free_after_second_insert, free_after_first_insert);

  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, target_oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_after_delete = get_free_space_of_oid_page (target_oid);

  const int recovered = free_after_delete - free_after_second_insert;
  ASSERT_GE (recovered, rec_target.length);
}

// ============================================================================
// TC: Delete 160KB multi-chunk record
// ============================================================================
TEST (OosDeleteServerTest, OosDeleteLarge160KBMultiChunk)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int large_size = 160 * 1024;
  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  /* verify readable before deletion */
  RECDES rec_check {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_check);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_check.data, rec_in.data);
  recdes_free_data_area (&rec_check);

  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_after {};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_after);
  ASSERT_NE (read_err, NO_ERROR);
  if (rec_after.data != nullptr)
    {
      recdes_free_data_area (&rec_after);
    }
}

// ============================================================================
// TC: Deleted slot becomes REC_UNKNOWN
// ============================================================================
TEST (OosDeleteServerTest, OosDeleteSlotBecomesUnknown)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("slot state verification test", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);

  /* before deletion: slot must hold a valid record type */
  {
    VPID vpid = {oid.pageid, oid.volid};
    PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				   PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    ASSERT_NE (page_ptr, nullptr);
    test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };

    INT16 type_before = spage_get_record_type (page_ptr, oid.slotid);
    ASSERT_NE (type_before, REC_UNKNOWN);
  }

  err = test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  /* after deletion: slot becomes REC_UNKNOWN */
  {
    VPID vpid = {oid.pageid, oid.volid};
    PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				   PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    ASSERT_NE (page_ptr, nullptr);
    test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };

    INT16 type_after = spage_get_record_type (page_ptr, oid.slotid);
    ASSERT_EQ (type_after, REC_UNKNOWN);
  }
}

// ============================================================================
// TC: identity-checked delete under SERVER_MODE (CBRD-26950)
// ============================================================================
TEST (OosDeleteServerTest, OosDeleteStampMismatchAndGoneHeadAreCleanNoOps)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  RECDES rec_in {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes ("Survives a stale delete", rec_in), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  oos_chain_ref live;
  ASSERT_EQ (oos_insert (thread_p, oos_vfid, oos_buffer (rec_in.data, (std::size_t) rec_in.length), oid,
			 &live.identity_stamp), NO_ERROR);
  live.head_oid = oid;

  /* wrong stamp: successful no-op, chunk untouched, error stack clean, no candidate */
  oos_chain_ref stale = live;
  stale.identity_stamp = LOG_LSA (live.identity_stamp.pageid + 1, (std::int16_t) live.identity_stamp.offset);
  std::vector<VPID> emptied;
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_TRUE (emptied.empty ());

  RECDES rec_out {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out), NO_ERROR);
  ASSERT_STREQ (rec_out.data, rec_in.data);
  recdes_free_data_area (&rec_out);

  /* right stamp: reclaimed */
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, live, &emptied), NO_ERROR);
  RECDES rec_gone {};
  ASSERT_NE (test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_gone), NO_ERROR);
  if (rec_gone.data != nullptr)
    {
      recdes_free_data_area (&rec_gone);
    }
  er_clear ();

  /* gone head: a block-retry replay is a successful no-op with a clean error stack */
  const std::size_t candidates = emptied.size ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, live, &emptied), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);
  EXPECT_EQ (emptied.size (), candidates);
}

TEST (OosDeleteServerTest, OosDeleteStaleReferenceAfterSlotReuseKeepsLiveChain)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  RECDES rec_old {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes ("Dead row's value chain", rec_old), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_old (&rec_old, recdes_free_data_area);

  oos_chain_ref stale;
  ASSERT_EQ (oos_insert (thread_p, oos_vfid, oos_buffer (rec_old.data, (std::size_t) rec_old.length), stale.head_oid,
			 &stale.identity_stamp), NO_ERROR);

  /* first reclaim; the slot is free again */
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale), NO_ERROR);

  /* a live row's same-size insert reuses the freed slot */
  RECDES rec_new {};
  ASSERT_EQ (test_oos_utils::from_string_into_recdes ("Live row's value chain", rec_new), NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_new (&rec_new, recdes_free_data_area);

  oos_chain_ref live;
  ASSERT_EQ (oos_insert (thread_p, oos_vfid, oos_buffer (rec_new.data, (std::size_t) rec_new.length), live.head_oid,
			 &live.identity_stamp), NO_ERROR);
  ASSERT_TRUE (OID_EQ (&live.head_oid, &stale.head_oid)) << "the scenario requires physical slot reuse";
  ASSERT_FALSE (LSA_EQ (&live.identity_stamp, &stale.identity_stamp));

  /* a block retry replays the stale reference: no-op, live chain untouched */
  er_clear ();
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, stale), NO_ERROR);
  EXPECT_EQ (er_errid (), NO_ERROR);

  RECDES rec_out {};
  ASSERT_EQ (test_oos_utils::oos_read_with_alloc (thread_p, live.head_oid, rec_out), NO_ERROR);
  ASSERT_STREQ (rec_out.data, rec_new.data);
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
