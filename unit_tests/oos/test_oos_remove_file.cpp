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
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
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
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
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
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: OosPageDestroyBasic
//
// Create file, insert to allocate a page, deallocate the page via
// oos_remove_page.
// ===========================================================================
TEST (OosFileDestroyTest, OosPageDestroyBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes ("Page destroy test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  // Get the VPID of the page where the record was inserted
  VPID vpid = {oid.pageid, oid.volid};
  test_oos_debug ("Deallocating page vpid={vol=%d,page=%d}", vpid.volid, vpid.pageid);

  err = oos_remove_page (thread_p, oos_vfid, vpid);
  ASSERT_EQ (err, NO_ERROR);
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
  err = oos_insert (thread_p, oos_vfid1, rec1, oid1);
  ASSERT_EQ (err, NO_ERROR);

  OID oid2 = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid2, rec2, oid2);
  ASSERT_EQ (err, NO_ERROR);

  // Destroy file 1
  err = oos_remove_file (thread_p, oos_vfid1);
  ASSERT_EQ (err, NO_ERROR);

  // File 2 should still be readable
  RECDES rec_out{};
  err = oos_read (thread_p, oid2, rec_out);
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
