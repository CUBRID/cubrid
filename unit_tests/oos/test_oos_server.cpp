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
 * test_oos_server.cpp - SERVER_MODE tests for core OOS operations
 *
 * Mirrors the SA_MODE test_oos.cpp tests under full SERVER_MODE infrastructure
 * (MVCC, threading, worker transactions).
 */

#include "object_representation.h"
#include "test_oos_server_common.hpp"

#include <vector>

/* bridge functions defined in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();

static std::string
make_filled_payload (int size, char ch)
{
  return std::string ((std::size_t) size, ch);
}

static void
clear_oos_insert_publication_state_for_test ()
{
  thread_p->oos_oids.clear ();

  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      tdes->oos_insert_lsa_queue.clear ();
    }
}

static void
build_insert_requests (std::vector<std::string> &payloads, std::vector<OID> &oids,
		       std::vector<oos_insert_request> &requests)
{
  oids.resize (payloads.size ());
  requests.clear ();
  requests.reserve (payloads.size ());

  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      OID_SET_NULL (&oids[i]);
      oos_insert_request request = { oos_buffer (payloads[i].data (), payloads[i].size ()), &oids[i] };
      requests.push_back (request);
    }
}

static void
assert_read_many_payloads (const std::vector<std::string> &payloads, const std::vector<OID> &oids)
{
  std::vector<std::string> outputs;
  std::vector<oos_read_request> requests;

  outputs.resize (payloads.size ());
  requests.reserve (payloads.size ());
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      outputs[i].resize (payloads[i].size ());
      oos_read_request request = { oids[i], oos_buffer (outputs[i].data (), outputs[i].size ()) };
      requests.push_back (request);
    }

  ASSERT_EQ (oos_read_many (thread_p, cubbase::span<oos_read_request> (requests.data (), requests.size ())),
	     NO_ERROR);
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      ASSERT_EQ (outputs[i], payloads[i]);
    }
}

// ============================================================================
// TC: Create and Destroy
// ============================================================================
TEST (OosServerTest, OosCreateAndDestroy)
{
  int err;

  VFID oos_vfid;
  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  ASSERT_NE (oos_vfid.fileid, NULL_FILEID);
  ASSERT_NE (oos_vfid.volid, NULL_VOLID);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

TEST (OosServerTest, OosCreateAndCreateAgain)
{
  int err;

  VFID oos_vfid;
  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VFID oos_vfid2;
  err = oos_create_file (thread_p, oos_vfid2);
  ASSERT_EQ (err, NO_ERROR);

  /* either volid is different or fileid is different */
  ASSERT_TRUE ((oos_vfid.fileid != oos_vfid2.fileid) || (oos_vfid.volid != oos_vfid2.volid));
}

// ============================================================================
// TC: Insert and Read
// ============================================================================
TEST (OosServerTest, OosInsertAndRead)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec {};
  const std::string random_data = "This is a test OOS data.";
  test_oos_utils::from_string_into_recdes (random_data, rec);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);
  ASSERT_NE (oid.volid, NULL_VOLID);
  ASSERT_NE (oid.slotid, NULL_SLOTID);

  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);

  ASSERT_EQ (rec_out.length, rec.length);
  ASSERT_STREQ (rec_out.data, rec.data);
  ASSERT_STREQ (rec_out.data, random_data.c_str ());

  recdes_free_data_area (&rec);
  recdes_free_data_area (&rec_out);
}

TEST (OosServerTest, OosInsertManyKeepsSinglePageLocalityAndReadManyGroupsHeadPage)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  std::vector<std::string> payloads =
  {
    make_filled_payload (900, 'a'),
    make_filled_payload (1000, 'b'),
    make_filled_payload (1100, 'c')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);

  clear_oos_insert_publication_state_for_test ();
  bridge_oos_debug_counters_reset ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
			      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);

  ASSERT_EQ (oids[0].volid, oids[1].volid);
  ASSERT_EQ (oids[0].pageid, oids[1].pageid);
  ASSERT_EQ (oids[1].volid, oids[2].volid);
  ASSERT_EQ (oids[1].pageid, oids[2].pageid);

  oos_debug_counters counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.insert_many_calls, 1ULL);
  EXPECT_EQ (counters.insert_many_requests, 3ULL);
  EXPECT_EQ (counters.single_page_batch_count, 1ULL);
  EXPECT_EQ (counters.insert_fresh_pages, 1ULL);
  EXPECT_EQ (counters.insert_values_per_fixed_page, 3ULL);

  bridge_oos_debug_counters_reset ();
  assert_read_many_payloads (payloads, oids);

  counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.read_many_calls, 1ULL);
  EXPECT_EQ (counters.read_many_requests, 3ULL);
  EXPECT_EQ (counters.read_many_grouped_head_pages, 1ULL);
  EXPECT_EQ (counters.read_values_per_fixed_page, 3ULL);
}

TEST (OosServerTest, OosInsertManyReusesOnlyPageThatFitsWholeBatch)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  std::string seed = make_filled_payload (1000, 's');
  OID seed_oid = OID_INITIALIZER;
  ASSERT_EQ (oos_insert (thread_p, oos_vfid, oos_buffer (seed.data (), seed.size ()), seed_oid), NO_ERROR);

  std::vector<std::string> payloads =
  {
    make_filled_payload (1000, 'x'),
    make_filled_payload (1000, 'y')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);

  clear_oos_insert_publication_state_for_test ();
  bridge_oos_debug_counters_reset ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
			      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);

  EXPECT_EQ (oids[0].volid, seed_oid.volid);
  EXPECT_EQ (oids[0].pageid, seed_oid.pageid);
  EXPECT_EQ (oids[1].volid, seed_oid.volid);
  EXPECT_EQ (oids[1].pageid, seed_oid.pageid);

  oos_debug_counters counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.single_page_batch_count, 1ULL);
  EXPECT_EQ (counters.insert_reused_pages, 1ULL);

  assert_read_many_payloads (payloads, oids);
}

TEST (OosServerTest, OosInsertManyAllocatesFreshPageInsteadOfScatteringBatch)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const int value_size = max_chunk_size / 5;
  const int seed_size = max_chunk_size - value_size - 512;
  ASSERT_GT (seed_size, value_size);

  std::string seed1 = make_filled_payload (seed_size, 'p');
  std::string seed2 = make_filled_payload (seed_size, 'q');
  OID seed_oid1 = OID_INITIALIZER;
  OID seed_oid2 = OID_INITIALIZER;
  ASSERT_EQ (oos_insert (thread_p, oos_vfid, oos_buffer (seed1.data (), seed1.size ()), seed_oid1), NO_ERROR);
  ASSERT_EQ (oos_insert (thread_p, oos_vfid, oos_buffer (seed2.data (), seed2.size ()), seed_oid2), NO_ERROR);
  ASSERT_NE (seed_oid1.pageid, seed_oid2.pageid);

  std::vector<std::string> payloads =
  {
    make_filled_payload (value_size, 'm'),
    make_filled_payload (value_size, 'n')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);

  clear_oos_insert_publication_state_for_test ();
  bridge_oos_debug_counters_reset ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
			      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);

  EXPECT_EQ (oids[0].volid, oids[1].volid);
  EXPECT_EQ (oids[0].pageid, oids[1].pageid);
  EXPECT_NE (oids[0].pageid, seed_oid1.pageid);
  EXPECT_NE (oids[0].pageid, seed_oid2.pageid);

  oos_debug_counters counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.single_page_batch_count, 1ULL);
  EXPECT_EQ (counters.insert_fresh_pages, 1ULL);

  assert_read_many_payloads (payloads, oids);
}

TEST (OosServerTest, OosInsertManySplitsOversizedSingleChunkRun)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const int payload_size = max_chunk_size / 2;
  std::vector<std::string> payloads =
  {
    make_filled_payload (payload_size, 'u'),
    make_filled_payload (payload_size, 'v'),
    make_filled_payload (payload_size, 'w')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);

  clear_oos_insert_publication_state_for_test ();
  bridge_oos_debug_counters_reset ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
			      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);

  EXPECT_NE (oids[0].pageid, oids[1].pageid);
  EXPECT_NE (oids[1].pageid, oids[2].pageid);

  oos_debug_counters counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.insert_many_requests, 3ULL);
  EXPECT_EQ (counters.single_page_batch_count, 3ULL);
  EXPECT_EQ (counters.insert_values_per_fixed_page, 3ULL);

  assert_read_many_payloads (payloads, oids);
}

TEST (OosServerTest, OosInsertManyPreservesMixedSingleAndMultiChunkPublicationOrder)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  std::vector<std::string> payloads =
  {
    make_filled_payload (1000, 'a'),
    make_filled_payload (max_chunk_size + 123, 'b'),
    make_filled_payload (1200, 'c')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);

  clear_oos_insert_publication_state_for_test ();
  bridge_oos_debug_counters_reset ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
			      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);

  ASSERT_FALSE (thread_p->oos_oids.empty ());
  std::size_t pos = 0;
  ASSERT_LT (pos, thread_p->oos_oids.size ());
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[pos], &oids[0]));
  pos++;

  ASSERT_LT (pos, thread_p->oos_oids.size ());
  if (OID_ISNULL (&thread_p->oos_oids[pos]))
    {
      pos++;
      ASSERT_LT (pos, thread_p->oos_oids.size ());
    }
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[pos], &oids[1]));
  pos++;

  ASSERT_LT (pos, thread_p->oos_oids.size ());
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[pos], &oids[2]));
  pos++;
  EXPECT_EQ (pos, thread_p->oos_oids.size ());

  oos_debug_counters counters = bridge_oos_debug_counters_get ();
  EXPECT_EQ (counters.insert_many_requests, 3ULL);
  EXPECT_EQ (counters.single_page_batch_count, 2ULL);

  assert_read_many_payloads (payloads, oids);
}

TEST (OosServerTest, OosInsertLargerThanPageSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int large_size = DB_PAGESIZE + 5;
  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, rec_in.data);
  ASSERT_EQ (strlen (rec_out.data), strlen (large_data.c_str ()));

  recdes_free_data_area (&rec_in);
  recdes_free_data_area (&rec_out);
}

TEST (OosServerTest, OosInsertLarge160KBString)
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

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, rec_in.data);
  ASSERT_EQ (strlen (rec_out.data), strlen (large_data.c_str ()));

  recdes_free_data_area (&rec_in);
  recdes_free_data_area (&rec_out);
}

TEST (OosServerTest, OosInsertAndRead100LargeStringsAroundMaxOosChunkSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();

  for (int large_size = max_chunk_size - 50; large_size <= max_chunk_size + 50; large_size++)
    {
      auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

      RECDES rec_in {};
      err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
      ASSERT_EQ (err, NO_ERROR);

      RECDES rec_out {};
      err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ (strlen (rec_out.data), strlen (large_data.c_str ()));
      ASSERT_STREQ (rec_out.data, rec_in.data);

      recdes_free_data_area (&rec_in);
      recdes_free_data_area (&rec_out);
    }
}

// ============================================================================
// TC: Same-page insertion
// ============================================================================
TEST (OosServerTest, ShouldInsertIntoSamePage)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in1 {};
  RECDES rec_in2 {};
  RECDES rec_out1 {};
  RECDES rec_out2 {};
  {
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_in1 (&rec_in1, recdes_free_data_area);
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_in2 (&rec_in2, recdes_free_data_area);
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_out1 (&rec_out1, recdes_free_data_area);
    test_oos_utils::auto_freed_recdes_ptr defer_free_rec_out2 (&rec_out2, recdes_free_data_area);

    err = test_oos_utils::from_string_into_recdes ("first string", rec_in1);
    ASSERT_EQ (err, NO_ERROR);

    err = test_oos_utils::from_string_into_recdes ("second string again", rec_in2);
    ASSERT_EQ (err, NO_ERROR);

    OID oid1 = OID_INITIALIZER;
    err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in1, oid1);
    ASSERT_EQ (err, NO_ERROR);

    OID oid2 = OID_INITIALIZER;
    err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in2, oid2);
    ASSERT_EQ (err, NO_ERROR);

    err = test_oos_utils::oos_read_with_alloc (thread_p, oid1, rec_out1);
    ASSERT_EQ (err, NO_ERROR);
    ASSERT_STREQ (rec_out1.data, rec_in1.data);

    err = test_oos_utils::oos_read_with_alloc (thread_p, oid2, rec_out2);
    ASSERT_EQ (err, NO_ERROR);
    ASSERT_STREQ (rec_out2.data, rec_in2.data);

    /* small records should land on the same page */
    ASSERT_EQ (oid1.pageid, oid2.pageid);
    ASSERT_EQ (oid1.volid, oid2.volid);
  }
}

// ============================================================================
// TC: oos_get_length
// ============================================================================
TEST (OosServerTest, OosGetLengthWithinPage)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const std::string data = "Hello, this is test data for oos_get_length!";
  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (data, rec_in);
  ASSERT_EQ (err, NO_ERROR);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  int length = oos_get_length (thread_p, oid);
  ASSERT_EQ (length, rec_in.length);

  recdes_free_data_area (&rec_in);
}

TEST (OosServerTest, OosGetLengthAcrossPages)
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

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  int length = oos_get_length (thread_p, oid);
  ASSERT_EQ (length, rec_in.length);

  recdes_free_data_area (&rec_in);
}

TEST (OosServerTest, OosGetLengthAroundMaxChunkSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();

  for (int size = max_chunk_size - 5; size <= max_chunk_size + 5; size++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (size);

      RECDES rec_in {};
      err = test_oos_utils::from_string_into_recdes (data, rec_in);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
      ASSERT_EQ (err, NO_ERROR);

      int length = oos_get_length (thread_p, oid);
      ASSERT_EQ (length, rec_in.length);

      recdes_free_data_area (&rec_in);
    }
}

// ============================================================================
// TC: OOS inline format [OID(8B) + length(8B)]
// ============================================================================
TEST (OosServerTest, OosInlineFormatWriteAndReadBack)
{
  ASSERT_EQ (OR_OOS_INLINE_SIZE, OR_OID_SIZE + OR_BIGINT_SIZE);
  ASSERT_EQ (OR_OOS_INLINE_SIZE, 16);

  char buf_data[OR_OOS_INLINE_SIZE];
  OR_BUF write_buf;
  or_init (&write_buf, buf_data, OR_OOS_INLINE_SIZE);

  OID test_oid;
  test_oid.pageid = 42;
  test_oid.slotid = 7;
  test_oid.volid = 3;
  DB_BIGINT test_length = 160 * 1024;

  or_put_oid (&write_buf, &test_oid);
  or_put_bigint (&write_buf, test_length);

  ASSERT_EQ (write_buf.ptr - buf_data, OR_OOS_INLINE_SIZE);

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

TEST (OosServerTest, OosInlineFormatWithRealOosInsert)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int data_size = 2048;
  auto data = test_oos_utils::make_repeated_pattern_string (data_size);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (data, rec_in);
  ASSERT_EQ (err, NO_ERROR);

  OID oos_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oos_oid);
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

  int oos_length = oos_get_length (thread_p, oos_oid);
  ASSERT_EQ (read_length, (DB_BIGINT) oos_length);

  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (read_length, (DB_BIGINT) rec_out.length);

  recdes_free_data_area (&rec_in);
  recdes_free_data_area (&rec_out);
}

TEST (OosServerTest, OosInlineLengthMatchesAcrossPages)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();

  int test_sizes[] = { 512, max_chunk_size - 1, max_chunk_size, max_chunk_size + 1, 160 * 1024 };

  for (int data_size : test_sizes)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (data_size);

      RECDES rec_in {};
      err = test_oos_utils::from_string_into_recdes (data, rec_in);
      ASSERT_EQ (err, NO_ERROR);

      OID oos_oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oos_oid);
      ASSERT_EQ (err, NO_ERROR);

      char inline_buf[OR_OOS_INLINE_SIZE];
      OR_BUF write_buf;
      or_init (&write_buf, inline_buf, OR_OOS_INLINE_SIZE);
      or_put_oid (&write_buf, &oos_oid);
      or_put_bigint (&write_buf, (DB_BIGINT) rec_in.length);

      OR_BUF read_buf;
      or_init (&read_buf, inline_buf, OR_OOS_INLINE_SIZE);
      OID read_oid;
      or_get_oid (&read_buf, &read_oid);

      int rc = NO_ERROR;
      DB_BIGINT inline_length = or_get_bigint (&read_buf, &rc);
      ASSERT_EQ (rc, NO_ERROR);
      ASSERT_EQ (inline_length, (DB_BIGINT) rec_in.length) << "Failed for data_size=" << data_size;

      int io_length = oos_get_length (thread_p, oos_oid);
      ASSERT_EQ (inline_length, (DB_BIGINT) io_length) << "Failed for data_size=" << data_size;

      RECDES rec_out {};
      err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ (inline_length, (DB_BIGINT) rec_out.length) << "Failed for data_size=" << data_size;

      recdes_free_data_area (&rec_in);
      recdes_free_data_area (&rec_out);
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
