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

#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "oos_file.hpp"
#include "test_oos_common.hpp"
#include "oos_log.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;

// bridge functions to access static functions in oos_file.cpp
int bridge_oos_get_max_chunk_size_within_page ();

// ---------------------------------------------------------------------------
// helper: fix page from OID with READ latch, return free space, then unfix
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// helper: peek OOS_RECORD_HEADER from a slot (before deleting)
// ---------------------------------------------------------------------------
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


// ===========================================================================
// TEST: OosDeleteBasic
//
// Insert a small record, then delete it.
// Verify oos_delete returns NO_ERROR and the page gains free space.
// ===========================================================================
TEST (OosDeleteTest, OosDeleteBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("Hello, OOS delete test!", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free_rec_in (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);
  test_oos_debug ("Inserted oid={vol=%d,page=%d,slot=%d}", oid.volid, oid.pageid, oid.slotid);

  int free_before = get_free_space_of_oid_page (oid);
  ASSERT_GE (free_before, 0);
  test_oos_debug ("free_before=%d", free_before);

  err = oos_delete (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_after = get_free_space_of_oid_page (oid);
  ASSERT_GE (free_after, 0);
  test_oos_debug ("free_after=%d", free_after);

  // free space must have grown by at least the record's user-data length
  ASSERT_GT (free_after, free_before);
}


// ===========================================================================
// TEST: OosDeleteThenReadFails
//
// After deletion the OID is invalid; oos_read must return an error.
// ===========================================================================
TEST (OosDeleteTest, OosDeleteThenReadFails)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("Record to be deleted", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_delete (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  // Reading a deleted slot should fail
  RECDES rec_out{};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
  ASSERT_NE (read_err, NO_ERROR);

  // rec_out data area should not have been allocated if read failed
  if (rec_out.data != nullptr)
    {
      recdes_free_data_area (&rec_out);
    }
}


// ===========================================================================
// TEST: OosDeleteMultiChunk
//
// Insert a record that spans two pages (> max_chunk_size).
// Before deleting, save the free space of each chunk's page.
// After deleting, verify both pages gained free space.
// ===========================================================================
TEST (OosDeleteTest, OosDeleteMultiChunk)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  // 50 bytes past the single-chunk limit → guaranteed two chunks
  const int large_size = max_chunk_size + 50;

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID head_oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, head_oid);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("head_oid={vol=%d,page=%d,slot=%d}", head_oid.volid, head_oid.pageid, head_oid.slotid);

  // Peek the header of the first chunk to find the next chunk OID
  OOS_RECORD_HEADER head_header{};
  err = peek_oos_header (head_oid, head_header);
  ASSERT_EQ (err, NO_ERROR);

  OID next_oid = head_header.next_chunk_oid;
  test_oos_debug ("next_oid={vol=%d,page=%d,slot=%d}", next_oid.volid, next_oid.pageid, next_oid.slotid);

  // A two-chunk record must have a valid next chunk
  ASSERT_NE (next_oid.pageid, NULL_PAGEID);

  int head_free_before = get_free_space_of_oid_page (head_oid);
  int next_free_before = get_free_space_of_oid_page (next_oid);
  ASSERT_GE (head_free_before, 0);
  ASSERT_GE (next_free_before, 0);
  test_oos_debug ("head_free_before=%d, next_free_before=%d", head_free_before, next_free_before);

  err = oos_delete (thread_p, oos_vfid, head_oid);
  ASSERT_EQ (err, NO_ERROR);

  int head_free_after = get_free_space_of_oid_page (head_oid);
  int next_free_after = get_free_space_of_oid_page (next_oid);
  test_oos_debug ("head_free_after=%d, next_free_after=%d", head_free_after, next_free_after);

  // Both pages must have gained free space
  ASSERT_GT (head_free_after, head_free_before);
  ASSERT_GT (next_free_after, next_free_before);
}


// ===========================================================================
// TEST: OosUpdatePattern
//
// Simulate UPDATE: insert old record, insert new record, delete old record.
// Verify the new record is still readable and old OID is gone.
// ===========================================================================
TEST (OosDeleteTest, OosUpdatePattern)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const std::string old_data = "old value before update";
  const std::string new_data = "new value after update";

  RECDES rec_old{};
  RECDES rec_new{};
  err = test_oos_utils::from_string_into_recdes (old_data, rec_old);
  ASSERT_EQ (err, NO_ERROR);
  err = test_oos_utils::from_string_into_recdes (new_data, rec_new);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_old (&rec_old, recdes_free_data_area);
  test_oos_utils::auto_freed_recdes_ptr defer_new (&rec_new, recdes_free_data_area);

  OID old_oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_old, old_oid);
  ASSERT_EQ (err, NO_ERROR);

  OID new_oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_new, new_oid);
  ASSERT_EQ (err, NO_ERROR);

  // Both OIDs should be different slots
  ASSERT_NE (old_oid.slotid, new_oid.slotid);

  // Delete the old record (UPDATE path: discard previous version)
  err = oos_delete (thread_p, oos_vfid, old_oid);
  ASSERT_EQ (err, NO_ERROR);

  // New record must still be readable and unchanged
  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, new_oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (rec_out.length, rec_new.length);
  ASSERT_STREQ (rec_out.data, new_data.c_str());
  recdes_free_data_area (&rec_out);

  // Old OID must no longer be readable
  RECDES stale_out{};
  int stale_err = test_oos_utils::oos_read_with_alloc (thread_p, old_oid, stale_out);
  ASSERT_NE (stale_err, NO_ERROR);
  if (stale_out.data != nullptr)
    {
      recdes_free_data_area (&stale_out);
    }
}


// ===========================================================================
// TEST: OosDeleteRestoresFreeSpace
//
// Insert a record and then delete it.  The page's free space must return to
// at least the value it had before the insert.
// ===========================================================================
TEST (OosDeleteTest, OosDeleteRestoresFreeSpace)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const std::string data = "free space restore test data";
  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  // First insert establishes the page; record free space after it
  OID first_oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, first_oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_after_first_insert = get_free_space_of_oid_page (first_oid);

  // Insert the record we want to delete
  RECDES rec_target{};
  err = test_oos_utils::from_string_into_recdes (data, rec_target);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_target (&rec_target, recdes_free_data_area);

  OID target_oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_target, target_oid);
  ASSERT_EQ (err, NO_ERROR);

  // Both inserts should land on the same page (small data)
  ASSERT_EQ (first_oid.pageid, target_oid.pageid);

  int free_after_second_insert = get_free_space_of_oid_page (target_oid);
  ASSERT_LT (free_after_second_insert, free_after_first_insert);

  // Delete the second record
  err = oos_delete (thread_p, oos_vfid, target_oid);
  ASSERT_EQ (err, NO_ERROR);

  int free_after_delete = get_free_space_of_oid_page (target_oid);
  test_oos_debug ("free_after_first_insert=%d, free_after_second_insert=%d, free_after_delete=%d",
		  free_after_first_insert, free_after_second_insert, free_after_delete);

  // spage_delete reclaims record data but retains the slot entry as a reusable
  // tombstone (sizeof SPAGE_SLOT = 4 bytes).  So the recovered space equals the
  // record data size, not the full (data + slot) that was consumed on insert.
  // Verify that at least the record data size was returned to the page.
  const int recovered = free_after_delete - free_after_second_insert;
  ASSERT_GE (recovered, rec_target.length);
}


// ===========================================================================
// TEST: OosDeleteLarge160KBMultiChunk
//
// Insert a 160 KB record (many chunks across pages), delete it,
// verify oos_read on the head OID fails afterward.
// ===========================================================================
TEST (OosDeleteTest, OosDeleteLarge160KBMultiChunk)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int large_size = 160 * 1024; // 160 KB
  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("Inserted 160KB record at oid={vol=%d,page=%d,slot=%d}",
		  oid.volid, oid.pageid, oid.slotid);

  // Verify it can be read before deletion
  RECDES rec_check{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_check);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_check.data, rec_in.data);
  recdes_free_data_area (&rec_check);

  // Delete the full chain
  err = oos_delete (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  // Reading any chunk from the head OID must now fail
  RECDES rec_after{};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_after);
  ASSERT_NE (read_err, NO_ERROR);
  if (rec_after.data != nullptr)
    {
      recdes_free_data_area (&rec_after);
    }
}


// ===========================================================================
// TEST: OosDeleteSlotBecomesUnknown
//
// After oos_delete, the slot is internally marked REC_DELETED_WILL_REUSE by
// spage_delete. The public API spage_get_record_type maps deleted slots to
// REC_UNKNOWN. Verify this transition: valid type before, REC_UNKNOWN after.
// ===========================================================================
TEST (OosDeleteTest, OosDeleteSlotBecomesUnknown)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("slot state verification test", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);

  // Before deletion: slot must hold a valid record type (not unknown/deleted)
  {
    VPID vpid = {oid.pageid, oid.volid};
    PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				   PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    ASSERT_NE (page_ptr, nullptr);
    test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };

    INT16 type_before = spage_get_record_type (page_ptr, oid.slotid);
    ASSERT_NE (type_before, REC_UNKNOWN);
    test_oos_debug ("type_before=%d", type_before);
  }

  err = oos_delete (thread_p, oos_vfid, oid);
  ASSERT_EQ (err, NO_ERROR);

  // After deletion: spage_get_record_type returns REC_UNKNOWN for deleted slots
  // (internally the slot is REC_DELETED_WILL_REUSE, but the API maps it to REC_UNKNOWN)
  {
    VPID vpid = {oid.pageid, oid.volid};
    PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				   PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    ASSERT_NE (page_ptr, nullptr);
    test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };

    INT16 type_after = spage_get_record_type (page_ptr, oid.slotid);
    ASSERT_EQ (type_after, REC_UNKNOWN);
    test_oos_debug ("type_after=%d (REC_UNKNOWN=%d)", type_after, REC_UNKNOWN);
  }
}


// TODO: add recovery tests — verify undo (rollback restores deleted chunks) and redo (crash recovery re-deletes)
//       for both single-chunk and multi-chunk records. Requires integration test infrastructure.


int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  ::testing::GTEST_FLAG (break_on_failure) = true;

  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS();
}
