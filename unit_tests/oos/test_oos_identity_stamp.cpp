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

/*
 * test_oos_identity_stamp.cpp - the OOS value chain identity stamp (CBRD-26950)
 *
 * Every OOS value chain is created with an identity stamp: the page LSA observed under the write
 * latch immediately before the head chunk's header is built. The stamp travels in the chunk header
 * and in the owning heap record's OOS inline stub, and it is the target identity that oos_delete
 * verifies before reclaiming a chain and that oos_read verifies before returning bytes.
 *
 * These tests drive only the public OOS file API and observe return codes, the error stack, the
 * bytes read back, the emptied-page list and the stamp reported by oos_get_identity_stamp.
 */

#include "gtest/gtest.h"
#include <cstring>
#include <string>
#include <vector>

#include "error_manager.h"
#include "heap_oos.hpp"
#include "log_lsa.hpp"
#include "object_representation.h"
#include "oos_file.hpp"
#include "oos_log.hpp"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "test_oos_common.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;

/* bridge to a static function in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

namespace
{
  /* Inserts payload and returns both outputs of oos_insert. */
  int
  insert_with_stamp (const VFID &oos_vfid, const std::string &payload, OID &oid_out, LOG_LSA &stamp_out)
  {
    RECDES rec{};
    int err = test_oos_utils::from_string_into_recdes (payload, rec);
    if (err != NO_ERROR)
      {
	return err;
      }
    test_oos_utils::auto_freed_recdes_ptr defer_free (&rec, recdes_free_data_area);
    stamp_out = NULL_LSA;
    return oos_insert (thread_p, oos_vfid, oos_buffer (rec.data, static_cast<std::size_t> (rec.length)), oid_out,
		       &stamp_out);
  }

  std::string
  page_filling_payload ()
  {
    return test_oos_utils::make_repeated_pattern_string (bridge_oos_get_max_chunk_size_within_page () - 50);
  }
} // namespace

// ===========================================================================
// Ticket 02: the stamp is issued at insert and readable through the accessor
// ===========================================================================

TEST (OosIdentityStampTest, InsertReportsTheStampTheHeadChunkCarries)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "identity stamp round trip", oid, issued), NO_ERROR);

  LOG_LSA stored = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, oid, &stored), NO_ERROR);
  EXPECT_TRUE (LSA_EQ (&issued, &stored)) << "insert reported " << issued.pageid << "|" << issued.offset
					  << " but the head chunk carries " << stored.pageid << "|" << stored.offset;
}

TEST (OosIdentityStampTest, FreshPageIssuesNonNullStamp)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  /* A page-filling record needs a page of its own, so this lands on a freshly allocated page whose
   * logged initialization already advanced its page LSA. NULL is an ordinary stamp value and gets no
   * special handling; this only pins down that a normal insert never observes it. */
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, page_filling_payload (), oid, issued), NO_ERROR);
  EXPECT_FALSE (LSA_ISNULL (&issued));
}

TEST (OosIdentityStampTest, SuccessiveOccupantsOfOneSlotCarryDifferentStamps)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID first_oid = OID_INITIALIZER;
  LOG_LSA first_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "first slot incarnation", first_oid, first_stamp), NO_ERROR);

  /* Reclaim the chunk; the slot is free again. */
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, first_oid), NO_ERROR);

  /* A same-size insert reuses the freed slot (ANCHORED slotted page + bestspace), beginning a new
   * slot incarnation under the same OOS OID. */
  OID second_oid = OID_INITIALIZER;
  LOG_LSA second_stamp = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "later slot incarnation", second_oid, second_stamp), NO_ERROR);
  ASSERT_TRUE (OID_EQ (&first_oid, &second_oid)) << "the scenario requires physical slot reuse";

  /* The first occupant's own logged insert and delete advanced the page LSA past its stamp, so the
   * second occupant cannot share it. */
  EXPECT_FALSE (LSA_EQ (&first_stamp, &second_stamp));

  LOG_LSA stored = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, second_oid, &stored), NO_ERROR);
  EXPECT_TRUE (LSA_EQ (&second_stamp, &stored));
}

TEST (OosIdentityStampTest, MultiPageInsertReportsTheHeadChunkStamp)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const std::string payload = test_oos_utils::make_repeated_pattern_string (2 * max_chunk_size + 100);

  OID head_oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, head_oid, issued), NO_ERROR);

  /* Chains are written tail first, so the head chunk is the last one written and its value is the
   * one the stub must carry. */
  LOG_LSA stored = NULL_LSA;
  ASSERT_EQ (oos_get_identity_stamp (thread_p, head_oid, &stored), NO_ERROR);
  EXPECT_TRUE (LSA_EQ (&issued, &stored));
}

TEST (OosIdentityStampTest, BatchInsertReportsOneStampPerRequest)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  std::vector<std::string> payloads =
  {
    std::string (1000, 'a'),
    std::string (static_cast<std::size_t> (max_chunk_size) + 123, 'b'),	/* multi-page */
    std::string (1200, 'c')
  };
  std::vector<OID> oids (payloads.size (), OID_INITIALIZER);
  std::vector<LOG_LSA> stamps (payloads.size (), NULL_LSA);
  std::vector<oos_insert_request> requests;
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      oos_insert_request request = { oos_buffer (payloads[i].data (), payloads[i].size ()), &oids[i], &stamps[i] };
      requests.push_back (request);
    }
  /* The stamp output is optional: a request without one is still served. */
  std::string extra (800, 'd');
  OID extra_oid = OID_INITIALIZER;
  oos_insert_request extra_request = { oos_buffer (extra.data (), extra.size ()), &extra_oid, NULL };
  requests.push_back (extra_request);

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid, cubbase::span<oos_insert_request> (requests.data (),
			      requests.size ())), NO_ERROR);

  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      LOG_LSA stored = NULL_LSA;
      ASSERT_EQ (oos_get_identity_stamp (thread_p, oids[i], &stored), NO_ERROR) << "request " << i;
      EXPECT_TRUE (LSA_EQ (&stamps[i], &stored)) << "request " << i;
    }
  LOG_LSA extra_stored = NULL_LSA;
  EXPECT_EQ (oos_get_identity_stamp (thread_p, extra_oid, &extra_stored), NO_ERROR);
}

TEST (OosIdentityStampTest, AccessorFailsForAbsentChunk)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, "soon to be gone", oid, issued), NO_ERROR);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, oid), NO_ERROR);

  LOG_LSA stored = NULL_LSA;
  EXPECT_NE (oos_get_identity_stamp (thread_p, oid, &stored), NO_ERROR);
  EXPECT_NE (er_errid (), NO_ERROR);
  er_clear ();

  /* A slot that never existed on a live page is absent too. */
  OID never_used = oid;
  never_used.slotid = 4000;
  EXPECT_NE (oos_get_identity_stamp (thread_p, never_used, &stored), NO_ERROR);
  EXPECT_NE (er_errid (), NO_ERROR);
  er_clear ();
}

// ===========================================================================
// Ticket 03: the stub packs the stamp into one bigint, and reads verify it
// ===========================================================================

TEST (OosIdentityStampTest, PackedStubStampRoundTripsEveryValue)
{
  const LOG_LSA samples[] =
  {
    NULL_LSA,
    LOG_LSA (0, 0),
    LOG_LSA (1, 1),
    LOG_LSA (0xC0FFEE, 42),
    LOG_LSA (123456789012345LL, 16383),
    LOG_LSA (MAX_LOG_LSA_PAGEID, MAX_LOG_LSA_OFFSET),
    LOG_LSA (7, -1),
  };
  for (const LOG_LSA &sample : samples)
    {
      const DB_BIGINT packed = oos_pack_identity_stamp (sample);
      const LOG_LSA unpacked = oos_unpack_identity_stamp (packed);
      EXPECT_TRUE (LSA_EQ (&unpacked, &sample)) << (long long) sample.pageid << "|" << (int) sample.offset
	  << " packed to " << (long long) packed;
    }
  /* NULL_LSA (-1, -1) is all ones, so a zero-filled stub never decodes as NULL by accident. */
  EXPECT_EQ (oos_pack_identity_stamp (NULL_LSA), (DB_BIGINT) -1);
  const LOG_LSA zero = oos_unpack_identity_stamp (0);
  EXPECT_FALSE (LSA_ISNULL (&zero));
}

TEST (OosIdentityStampTest, StubWriteThenParseRoundTripsAtTwentyFourBytes)
{
  /* The OOS inline stub as the heap writer stores it: [OID (8B) | full length (8B) | packed stamp (8B)]. */
  ASSERT_EQ (OR_OOS_INLINE_SIZE, 24);

  OID head_oid;
  head_oid.volid = 3;
  head_oid.pageid = 4242;
  head_oid.slotid = 7;
  const DB_BIGINT full_length = 160 * 1024;
  const LOG_LSA stamps[] = { LOG_LSA (0xC0FFEE, 42), NULL_LSA };

  for (const LOG_LSA &identity_stamp : stamps)
    {
      alignas (MAX_ALIGNMENT) char stub[OR_OOS_INLINE_SIZE];
      OR_BUF write_buf;
      or_init (&write_buf, stub, OR_OOS_INLINE_SIZE);
      or_put_oid (&write_buf, &head_oid);
      or_put_bigint (&write_buf, full_length);
      or_put_bigint (&write_buf, oos_pack_identity_stamp (identity_stamp));
      ASSERT_EQ (write_buf.ptr - stub, OR_OOS_INLINE_SIZE);

      RECDES recdes = { OR_OOS_INLINE_SIZE, OR_OOS_INLINE_SIZE, REC_HOME, stub };
      oos_chain_ref ref;
      DB_BIGINT parsed_length = 0;
      ASSERT_EQ (heap_oos_parse_inline_ref (&recdes, stub, &ref, &parsed_length), NO_ERROR);
      EXPECT_TRUE (OID_EQ (&ref.head_oid, &head_oid));
      EXPECT_EQ (parsed_length, full_length);
      /* A NULL stamp parses like any other value (invariant 3). */
      EXPECT_TRUE (LSA_EQ (&ref.identity_stamp, &identity_stamp));
    }
}

TEST (OosIdentityStampTest, ReadWithMatchingReferenceReturnsTheValue)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  /* from_string_into_recdes stores the terminator too, so the chain is payload.size () + 1 bytes. */
  const std::string payload = "value behind a verified reference";
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);

  oos_chain_ref ref;
  ref.head_oid = oid;
  ref.identity_stamp = issued;

  std::string out (payload.size () + 1, '?');
  ASSERT_EQ (oos_read (thread_p, ref, oos_buffer (out.data (), out.size ())), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());
}

TEST (OosIdentityStampTest, ReadWithMismatchedStampFailsAsCorruptedRecordAndLeavesChainIntact)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  const std::string payload = "never returned through a stale reference";
  OID oid = OID_INITIALIZER;
  LOG_LSA issued = NULL_LSA;
  ASSERT_EQ (insert_with_stamp (oos_vfid, payload, oid, issued), NO_ERROR);

  oos_chain_ref stale;
  stale.head_oid = oid;
  stale.identity_stamp = LOG_LSA (issued.pageid + 1, (std::int16_t) issued.offset);

  std::string out (payload.size () + 1, '?');
  EXPECT_EQ (oos_read (thread_p, stale, oos_buffer (out.data (), out.size ())), ER_HEAP_OOS_CORRUPTED_RECORD);
  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  /* A NULL stamp is an ordinary value: it mismatches a non-NULL one like any other. */
  stale.identity_stamp = NULL_LSA;
  EXPECT_EQ (oos_read (thread_p, stale, oos_buffer (out.data (), out.size ())), ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();

  /* The chain itself is untouched: the matching reference still reads it. */
  oos_chain_ref live;
  live.head_oid = oid;
  live.identity_stamp = issued;
  ASSERT_EQ (oos_read (thread_p, live, oos_buffer (out.data (), out.size ())), NO_ERROR);
  EXPECT_STREQ (out.c_str (), payload.c_str ());
}

TEST (OosIdentityStampTest, GroupedReadVerifiesEveryReference)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  std::vector<std::string> payloads = { std::string (700, 'p'), std::string (900, 'q') };
  std::vector<OID> oids (payloads.size (), OID_INITIALIZER);
  std::vector<LOG_LSA> stamps (payloads.size (), NULL_LSA);
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      ASSERT_EQ (insert_with_stamp (oos_vfid, payloads[i], oids[i], stamps[i]), NO_ERROR);
    }

  std::vector<std::string> outputs (payloads.size ());
  std::vector<oos_read_request> requests;
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      outputs[i].assign (payloads[i].size () + 1, '?');
      oos_chain_ref ref;
      ref.head_oid = oids[i];
      ref.identity_stamp = stamps[i];
      requests.push_back ({ ref, oos_buffer (outputs[i].data (), outputs[i].size ()) });
    }
  ASSERT_EQ (oos_read_many (thread_p, cubbase::span<oos_read_request> (requests.data (), requests.size ())),
	     NO_ERROR);
  for (std::size_t i = 0; i < payloads.size (); i++)
    {
      EXPECT_STREQ (outputs[i].c_str (), payloads[i].c_str ());
    }

  /* One stale reference in the group fails the group with the corrupted-record error. */
  requests[1].ref.identity_stamp = LOG_LSA (stamps[1].pageid + 1, (std::int16_t) stamps[1].offset);
  EXPECT_EQ (oos_read_many (thread_p, cubbase::span<oos_read_request> (requests.data (), requests.size ())),
	     ER_HEAP_OOS_CORRUPTED_RECORD);
  er_clear ();
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;

  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS ();
}
