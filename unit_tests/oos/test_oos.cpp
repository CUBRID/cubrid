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

#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "object_representation.h"
#include "oos_file.hpp"
#include "test_oos_common.hpp"
#include "page_buffer_util.hpp"

#include "oos_log.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;

// bridge functions to access static functions in oos_file.cpp
const auto_unfix_page_ptr bridge_oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length,
    VPID &vpid);
int bridge_oos_get_max_chunk_size_within_page ();
int bridge_oos_vpid_init_new (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);
/* bridge_oos_get_recently_inserted_oos_vpid removed — oos_recently_inserted_oos_vpid_map replaced by bestspace */

TEST (OosTest, OosCreateAndDestroy)
{
  int err;

  VFID oos_vfid;
  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  auto [fileid, volid] = oos_vfid;

  test_oos_debug ("oos_vfid: fileid=%d, volid=%d", fileid, volid);
  ASSERT_NE (fileid, NULL_FILEID);
  ASSERT_NE (volid, NULL_VOLID);

  err = oos_remove_file (thread_p, oos_vfid);
  auto [fileid_after_destroy, volid_after_destroy] = oos_vfid;

  ASSERT_EQ (err, NO_ERROR);
  // ASSERT_EQ (fileid_after_destroy, NULL_FILEID);
  // ASSERT_EQ (volid_after_destroy, NULL_VOLID);

}

TEST (OosTest, OosCreateAndCreateAgain)
{
  int err;

  VFID oos_vfid;
  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VFID oos_vfid2;
  err = oos_create_file (thread_p, oos_vfid2);
  ASSERT_EQ (err, NO_ERROR);

  auto [fileid1, volid1] = oos_vfid;
  auto [fileid2, volid2] = oos_vfid2;

  test_oos_debug ("First oos_vfid: fileid=%d, volid=%d", fileid1, volid1);
  test_oos_debug ("Second oos_vfid: fileid=%d, volid=%d", fileid2, volid2);

  // either volid is different or fileid is different
  ASSERT_TRUE ( (fileid1 != fileid2) || (volid1 != volid2) );

}

TEST (OosTest, OosInsertAndRead)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec{};
  const std::string random_data = "This is a test OOS data.";
  test_oos_utils::from_string_into_recdes ("This is a test OOS data.", rec);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);
  ASSERT_NE (oid.volid, NULL_VOLID);
  ASSERT_NE (oid.slotid, NULL_SLOTID);
  test_oos_debug ("OID: volid=%d, pageid=%d, slotid=%d", oid.volid, oid.pageid, oid.slotid);

  RECDES rec_out{};
  err = oos_read (thread_p, oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);

  ASSERT_EQ (rec_out.length, rec.length);
  ASSERT_STREQ (rec_out.data, rec.data);
  ASSERT_STREQ (rec_out.data, random_data.c_str());
}

TEST (OosTest, OosInsertLargerThanPageSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int large_size = DB_PAGESIZE + 5;

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_out{};
  err = oos_read (thread_p, oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, rec_in.data);
  ASSERT_EQ (strlen (rec_out.data),strlen (large_data.c_str()));

  recdes_free_data_area (&rec_in);
  recdes_free_data_area (&rec_out);
  ASSERT_EQ (rec_in.data, nullptr);
  ASSERT_EQ (rec_out.data, nullptr);
}

TEST (OosTest, OosInsertLarge160KBString)
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

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_out{};
  err = oos_read (thread_p, oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, rec_in.data);
  ASSERT_EQ (strlen (rec_out.data),strlen (large_data.c_str()));

  recdes_free_data_area (&rec_in);
  recdes_free_data_area (&rec_out);
  ASSERT_EQ (rec_in.data, nullptr);
  ASSERT_EQ (rec_out.data, nullptr);
}

TEST (OosTest, OosInsertAndRead100LargeStringsAroundMaxOosChunkSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  // max chunk size that can be stored in a single OOS slotted page
  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page();

  for (int large_size = max_chunk_size - 50; large_size <= max_chunk_size + 50; large_size++)
    {
      auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

      RECDES rec_in{};
      err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
      ASSERT_EQ (err, NO_ERROR);

      OID oid;
      err = oos_insert (thread_p, oos_vfid, rec_in, oid);
      ASSERT_EQ (err, NO_ERROR);

      RECDES rec_out{};
      err = oos_read (thread_p, oid, rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ (strlen (rec_out.data),strlen (large_data.c_str()));
      ASSERT_STREQ (rec_out.data, rec_in.data);

      recdes_free_data_area (&rec_in);
      recdes_free_data_area (&rec_out);
      ASSERT_EQ (rec_in.data, nullptr);
      ASSERT_EQ (rec_out.data, nullptr);
    }

}

TEST (OosTest, OosFindBestSpace)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VPID vpid{};
  vpid.volid = NULL_VOLID;
  vpid.pageid = NULL_PAGEID;
  auto random_data_length = 100;
  auto page_ptr = bridge_oos_find_best_page (thread_p, oos_vfid, random_data_length, vpid);
  ASSERT_NE (page_ptr, nullptr);

  test_oos_debug ("Best page found: volid=%d, pageid=%d", vpid.volid, vpid.pageid);
  ASSERT_NE (vpid.volid, NULL_VOLID);
  ASSERT_NE (vpid.pageid, NULL_PAGEID);
}

TEST (OosTest, OosFindBestSpaceReturnsExistingPage)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VPID vpid{};
  vpid.volid = NULL_VOLID;
  vpid.pageid = NULL_PAGEID;
  auto random_data_length = 100;
  auto page_ptr = bridge_oos_find_best_page (thread_p, oos_vfid, random_data_length, vpid);
  ASSERT_NE (page_ptr, nullptr);

  test_oos_debug ("Best page found: volid=%d, pageid=%d", vpid.volid, vpid.pageid);
  ASSERT_NE (vpid.volid, NULL_VOLID);
  ASSERT_NE (vpid.pageid, NULL_PAGEID);

  auto a_string = test_oos_utils::make_repeated_pattern_string (random_data_length);
  RECDES rec{};
  {
    test_oos_utils::from_string_into_recdes (a_string, rec);
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec (&rec, recdes_free_data_area);

  }
  ASSERT_EQ (rec.data, nullptr);
}


TEST (OosTest, OosFixAndUnfixPage)
{
  int err;

  VFID oos_vfid;
  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VPID vpid{NULL_PAGEID, NULL_VOLID};
  const auto random_data_length = 100;

  auto page_ptr = bridge_oos_find_best_page (thread_p, oos_vfid, random_data_length, vpid);
  ASSERT_NE (page_ptr, nullptr);

  test_oos_debug ("Best page found: volid=%d, pageid=%d", vpid.volid, vpid.pageid);
  ASSERT_NE (vpid.volid, NULL_VOLID);
  ASSERT_NE (vpid.pageid, NULL_PAGEID);

}

TEST (OosTest, OosManualSlottedPageInsertAndGet)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const auto data1 = std::string ("this is a random data 1");

  VPID vpid{};
  vpid.volid = NULL_VOLID;
  vpid.pageid = NULL_PAGEID;
  auto random_data_length = 100;
  auto auto_page_ptr = bridge_oos_find_best_page (thread_p, oos_vfid, random_data_length, vpid);
  ASSERT_NE (auto_page_ptr, nullptr);

  test_oos_debug ("Best page found: volid=%d, pageid=%d", vpid.volid, vpid.pageid);
  ASSERT_NE (vpid.volid, NULL_VOLID);
  ASSERT_NE (vpid.pageid, NULL_PAGEID);

  // prepare insert data
  RECDES rec{};
  rec.type = REC_HOME;
  const auto insert_data = std::string ("this is a data to be inserted!");
  err = recdes_allocate_data_area (&rec, insert_data.size() + 1);
  ASSERT_EQ (err, NO_ERROR);

  std::memcpy (rec.data, insert_data.c_str(), insert_data.size() + 1);
  rec.length = static_cast<int> (insert_data.size() + 1);
  ASSERT_EQ (rec.length, insert_data.size() + 1);

  // read
  PGSLOTID slotid_out = NULL_SLOTID;
  PAGE_PTR page_ptr = auto_page_ptr.get();
  auto sp_error = spage_insert (thread_p, page_ptr, &rec, &slotid_out);
  ASSERT_EQ (sp_error, SP_SUCCESS);
  ASSERT_NE (slotid_out, NULL_SLOTID);

  // prepare record to read data
  RECDES rec_out{};
  SCAN_CODE scan_code = spage_get_record (thread_p, page_ptr, slotid_out, &rec_out, PEEK);
  ASSERT_EQ (scan_code, S_SUCCESS);

  // see if rec and rec_out are same
  ASSERT_EQ (rec.length, rec_out.length);
  ASSERT_STREQ (rec.data, rec_out.data);

  recdes_free_data_area (&rec);
  assert (rec.data == nullptr);
  // rec_out data area is PEEKed, so no need to free
  assert (rec_out.data != nullptr);
}

TEST (OosTest, ShouldInsertIntoSamePage)
{

  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int large_size = DB_PAGESIZE + 5;

  RECDES rec_in1{};
  RECDES rec_in2{};
  RECDES rec_out1{};
  RECDES rec_out2{};
  {
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_in1 (&rec_in1, recdes_free_data_area);
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_in2 (&rec_in2, recdes_free_data_area);
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_out1 (&rec_out1, recdes_free_data_area);
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_out2 (&rec_out2, recdes_free_data_area);

    err = test_oos_utils::from_string_into_recdes ("first string", rec_in1);
    ASSERT_EQ (err, NO_ERROR);

    err = test_oos_utils::from_string_into_recdes ("second string again", rec_in2);
    ASSERT_EQ (err, NO_ERROR);

    OID oid1;
    err = oos_insert (thread_p, oos_vfid, rec_in1, oid1);
    ASSERT_EQ (err, NO_ERROR);

    OID oid2;
    err = oos_insert (thread_p, oos_vfid, rec_in2, oid2);
    ASSERT_EQ (err, NO_ERROR);

    err = oos_read (thread_p, oid1, rec_out1);
    ASSERT_EQ (err, NO_ERROR);
    ASSERT_STREQ (rec_out1.data, rec_in1.data);
    ASSERT_EQ (strlen (rec_out1.data),strlen ("first string"));

    err = oos_read (thread_p, oid2, rec_out2);
    ASSERT_EQ (err, NO_ERROR);
    ASSERT_STREQ (rec_out2.data, rec_in2.data);
    ASSERT_EQ (strlen (rec_out2.data),strlen ("second string again"));

    // rec_out1 and rec_out2 should be in the same page
    ASSERT_EQ (oid1.pageid, oid2.pageid);
    ASSERT_EQ (oid1.volid, oid2.volid);
  }

  // auto freed recdes
  assert (rec_in1.data == nullptr);
  assert (rec_in2.data == nullptr);
  assert (rec_out1.data == nullptr);
  assert (rec_out2.data == nullptr);

}

TEST (OosTest, OosGetLengthWithinPage)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const std::string data = "Hello, this is test data for oos_get_length!";
  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (data, rec_in);
  ASSERT_EQ (err, NO_ERROR);

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  int length = oos_get_length (thread_p, oid);
  ASSERT_EQ (length, rec_in.length);

  recdes_free_data_area (&rec_in);
}

TEST (OosTest, OosGetLengthAcrossPages)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int large_size = 160 * 1024; // 160 KB — spans multiple pages
  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  /* oos_get_length reads only the first chunk header, which stores the total size */
  int length = oos_get_length (thread_p, oid);
  ASSERT_EQ (length, rec_in.length);

  recdes_free_data_area (&rec_in);
}

TEST (OosTest, OosGetLengthAroundMaxChunkSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();

  /* Test sizes around the boundary between single-page and multi-page storage */
  for (int size = max_chunk_size - 5; size <= max_chunk_size + 5; size++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (size);

      RECDES rec_in{};
      err = test_oos_utils::from_string_into_recdes (data, rec_in);
      ASSERT_EQ (err, NO_ERROR);

      OID oid;
      err = oos_insert (thread_p, oos_vfid, rec_in, oid);
      ASSERT_EQ (err, NO_ERROR);

      int length = oos_get_length (thread_p, oid);
      ASSERT_EQ (length, rec_in.length);

      recdes_free_data_area (&rec_in);
    }
}

TEST (OosTest, ShouldInsertIntoDifferentPages)
{

  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const int large_size = max_chunk_size + 50;

  RECDES rec_in1{};
  err = test_oos_utils::from_string_into_recdes (std::string (large_size, 'A'), rec_in1);
  ASSERT_EQ (err, NO_ERROR);

  /*
   * rec_in1 is larger than max_chunk_size, so it will be split into
   * two chunks. The tail chunk will occupy some space in the page
   *
   * The first chunk will occupy max_chunk_size bytes (first portion of rec_in1 + OOS_RECORD_HEADER)
   * The second chunk will occupy the remaining bytes of rec_in1 + OOS_RECORD_HEADER,
   * which is (large_size - (max_chunk_size - sizeof (OOS_RECORD_HEADER))) + sizeof (OOS_RECORD_HEADER)
   *
   */

  OID oid1;
  err = oos_insert (thread_p, oos_vfid, rec_in1, oid1);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("Inserted record oid1: volid=%d, pageid=%d, slotid=%d", oid1.volid, oid1.pageid, oid1.slotid);

  // Get the page where the head chunk (oid1) was inserted
  VPID recent_vpid{oid1.pageid, oid1.volid};

  // TODO: when inserting large data, the chunks are inserted in reverse order.
  // Currently, recent_vpid points to the page where the last chunk is inserted, which is the head chunk of the total oos record inserted.

  PAGE_PTR raw_ptr = pgbuf_fix (thread_p, &recent_vpid, OLD_PAGE_IF_IN_BUFFER,
				PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  int free_space = spage_get_free_space (thread_p, raw_ptr);
  assert (raw_ptr != nullptr);
  {
    test_oos_utils::auto_unfixed_page_ptr page_ptr { raw_ptr, test_oos_utils::page_auto_unfix {thread_p} };
    ASSERT_EQ (free_space, 4);
    // TODO: this should be something like (max_chunk_size - (large_size - (max_chunk_size - sizeof (OOS_RECORD_HEADER))) + sizeof (OOS_RECORD_HEADER))
    // ASSERT_EQ (free_space, 8000 something for 8k);
  }

  // TODO
}

TEST (OosTest, OosInlineFormatWriteAndReadBack)
{
  /* Test that OR_OOS_INLINE_SIZE = OR_OID_SIZE + OR_BIGINT_SIZE = 16 bytes */
  ASSERT_EQ (OR_OOS_INLINE_SIZE, OR_OID_SIZE + OR_BIGINT_SIZE);
  ASSERT_EQ (OR_OOS_INLINE_SIZE, 16);

  /* Simulate writing OOS inline data: [OOS OID (8B) + length (8B)] */
  char buf_data[OR_OOS_INLINE_SIZE];
  OR_BUF write_buf;
  or_init (&write_buf, buf_data, OR_OOS_INLINE_SIZE);

  OID test_oid;
  test_oid.pageid = 42;
  test_oid.slotid = 7;
  test_oid.volid = 3;
  DB_BIGINT test_length = 160 * 1024; /* 160 KB */

  or_put_oid (&write_buf, &test_oid);
  or_put_bigint (&write_buf, test_length);

  /* Verify we wrote exactly OR_OOS_INLINE_SIZE bytes */
  ASSERT_EQ (write_buf.ptr - buf_data, OR_OOS_INLINE_SIZE);

  /* Read back: simulate what heap_midxkey_get_oos_extra_size does */
  OR_BUF read_buf;
  or_init (&read_buf, buf_data, OR_OOS_INLINE_SIZE);

  OID read_oid;
  or_get_oid (&read_buf, &read_oid);
  ASSERT_EQ (read_oid.pageid, test_oid.pageid);
  ASSERT_EQ (read_oid.slotid, test_oid.slotid);
  ASSERT_EQ (read_oid.volid, test_oid.volid);

  int rc = NO_ERROR;
  DB_BIGINT read_length = or_get_bigint (&read_buf, &rc);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (read_length, test_length);
}

TEST (OosTest, OosInlineFormatWithRealOosInsert)
{
  /* Insert data into OOS, then verify the inline format [OID + length] round-trips correctly */
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int data_size = 2048;
  auto data = test_oos_utils::make_repeated_pattern_string (data_size);

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (data, rec_in);
  ASSERT_EQ (err, NO_ERROR);

  OID oos_oid;
  err = oos_insert (thread_p, oos_vfid, rec_in, oos_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Build inline OOS data: [OOS OID (8B) + length (8B)] */
  char inline_buf[OR_OOS_INLINE_SIZE];
  OR_BUF write_buf;
  or_init (&write_buf, inline_buf, OR_OOS_INLINE_SIZE);
  or_put_oid (&write_buf, &oos_oid);
  or_put_bigint (&write_buf, (DB_BIGINT) rec_in.length);

  /* Read back OID and length from inline data */
  OR_BUF read_buf;
  or_init (&read_buf, inline_buf, OR_OOS_INLINE_SIZE);

  OID read_oid;
  or_get_oid (&read_buf, &read_oid);
  ASSERT_EQ (read_oid.pageid, oos_oid.pageid);
  ASSERT_EQ (read_oid.slotid, oos_oid.slotid);
  ASSERT_EQ (read_oid.volid, oos_oid.volid);

  int rc = NO_ERROR;
  DB_BIGINT read_length = or_get_bigint (&read_buf, &rc);
  ASSERT_EQ (rc, NO_ERROR);
  ASSERT_EQ (read_length, (DB_BIGINT) rec_in.length);

  /* Verify that the inline length matches what oos_get_length returns (via I/O) */
  int oos_length = oos_get_length (thread_p, oos_oid);
  ASSERT_EQ (read_length, (DB_BIGINT) oos_length);

  /* Verify that oos_read returns a recdes whose length matches the inline length */
  RECDES rec_out{};
  err = oos_read (thread_p, oos_oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (read_length, (DB_BIGINT) rec_out.length);

  recdes_free_data_area (&rec_in);
  recdes_free_data_area (&rec_out);
}

TEST (OosTest, OosInlineLengthMatchesAcrossPages)
{
  /* Test with data spanning multiple OOS pages to ensure inline length is correct */
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();

  /* Test sizes: within page, at boundary, and across pages */
  int test_sizes[] = { 512, max_chunk_size - 1, max_chunk_size, max_chunk_size + 1, 160 * 1024 };

  for (int data_size : test_sizes)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (data_size);

      RECDES rec_in{};
      err = test_oos_utils::from_string_into_recdes (data, rec_in);
      ASSERT_EQ (err, NO_ERROR);

      OID oos_oid;
      err = oos_insert (thread_p, oos_vfid, rec_in, oos_oid);
      ASSERT_EQ (err, NO_ERROR);

      /* Write inline format */
      char inline_buf[OR_OOS_INLINE_SIZE];
      OR_BUF write_buf;
      or_init (&write_buf, inline_buf, OR_OOS_INLINE_SIZE);
      or_put_oid (&write_buf, &oos_oid);
      or_put_bigint (&write_buf, (DB_BIGINT) rec_in.length);

      /* Read back length from inline data */
      OR_BUF read_buf;
      or_init (&read_buf, inline_buf, OR_OOS_INLINE_SIZE);
      OID read_oid;
      or_get_oid (&read_buf, &read_oid);

      int rc = NO_ERROR;
      DB_BIGINT inline_length = or_get_bigint (&read_buf, &rc);
      ASSERT_EQ (rc, NO_ERROR);

      /* Inline length must equal original data length */
      ASSERT_EQ (inline_length, (DB_BIGINT) rec_in.length) << "Failed for data_size=" << data_size;

      /* Inline length must match oos_get_length (I/O-based) */
      int io_length = oos_get_length (thread_p, oos_oid);
      ASSERT_EQ (inline_length, (DB_BIGINT) io_length) << "Failed for data_size=" << data_size;

      /* Inline length must match oos_read recdes length */
      RECDES rec_out{};
      err = oos_read (thread_p, oos_oid, rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ (inline_length, (DB_BIGINT) rec_out.length) << "Failed for data_size=" << data_size;

      recdes_free_data_area (&rec_in);
      recdes_free_data_area (&rec_out);
    }
}

int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  ::testing::GTEST_FLAG (break_on_failure) = true;

  // TIP:
  // While on active development, oos_log level is set to DEBUG.
  // This makes the test output verbose.
  // We need to explicitly set it to INFO or higher level to make test output clean.
  // For debugging test failures, we can set it back to DEBUG or TRACE.
  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS();
}
