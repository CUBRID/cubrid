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
 * test_oos_no_logging.cpp - the identity stamp's logging precondition (CBRD-26950)
 *
 * Distinct identity stamps across reuse of one OOS slot are a logged-operation guarantee. The stamp
 * is the page LSA read before the chunk's own log append, and a skipped append leaves the page LSA
 * where it was, so with logging disabled two occupants of one slot can carry the same stamp. The
 * accepted policy (2026-09-09) keeps documented no-logging bulk loads working and states that
 * precondition rather than rejecting the write, so this test pins down both halves: the same
 * scenario separates the stamps while logging is on and repeats them while it is off, and a
 * no-logging bulk load of large values still stores values that read back byte for byte.
 *
 * Two properties of the runtime shape this binary:
 *
 * - The actual logging state is process-global and one-way: log_set_no_logging() has no inverse. So
 *   this binary holds exactly ONE test, which walks the transition in order. Split into several, the
 *   ones needing logging on could only defend themselves against a shuffled run by skipping, and a
 *   skip is how logged-mode identity coverage gets quietly lost - a shuffled run really does put the
 *   no-logging case first and leave the logged one unrunnable.
 * - No-logging writes are not recoverable, which is exactly why the utility that offers them tells
 *   the operator to back up first. This binary therefore owns its database (oosnologdb) instead of
 *   sharing the unittestdb fixture with the rest of the OOS suite.
 *
 * What a repeated stamp here does NOT show: that ordinary bulk loading produces a stale reclamation
 * request that deletes live data. It is the disclosed limit of the guarantee, not a reproduction of
 * the bug. Note what it also does not show: that the limit is confined to standalone mode. Runtime
 * activation is - log_set_no_logging() fails outside SA_MODE - but the hidden `no_logging` parameter
 * is PRM_FOR_SERVER and log_initialize assigns log_No_logging from it with no mode guard, so a
 * server can boot unlogged, and a server is where vacuum block retries live. No such production
 * path was established by this repair; the accepted policy states the precondition instead, and any
 * future producer of stale references has to preserve it or revisit the design.
 */

#include "gtest/gtest.h"
#include <cstring>
#include <string>
#include <vector>

#include "error_manager.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "oos_file.hpp"
#include "oos_log.hpp"
#include "storage_common.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "test_oos_common.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;

/* bridge to a static function in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

namespace
{
  /* Inserts payload and returns both outputs of oos_insert. Deliberately a copy of the adapter in
   * test_oos_identity_stamp.cpp: sharing it would mean a helper that takes thread_p, and rewriting the
   * 35 call sites there is not worth it for twelve lines. The repair plan files sharing duplicated test
   * adapters as a lower-priority follow-up. */
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
} // namespace

/* One OOS file per test, as in the rest of the suite. */
class OosNoLoggingTest : public ::testing::Test
{
  protected:
    VFID oos_vfid;

    void SetUp () override
    {
      ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);
    }

    /* Removing the file is skipped once logging is off. Destroying a file is a postponed operation,
     * and with logging off log_append_postpone runs the postpone immediately, from outside the system
     * operation that file_rv_destroy asserts it is in. That is how any file destroy behaves with
     * logging off and has nothing to do with the identity stamp, so the no-logging cases leave their
     * OOS file where it is: this binary owns its database and its setup recreates it every run. */
    void TearDown () override
    {
      if (!log_is_no_logging ())
	{
	  ASSERT_EQ (oos_remove_file (thread_p, oos_vfid), NO_ERROR);
	}
      ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
    }

    /* Inserts payload, deletes it, inserts a payload of the same size, and reports the stamp each
     * occupant of the reused slot carried. Fails the calling test unless the slot was really reused,
     * so a repeated or a distinct stamp is always a statement about one physical slot. */
    void stamps_of_two_occupants_of_one_slot (LOG_LSA &first_stamp, LOG_LSA &second_stamp)
    {
      const std::string first_payload = "first slot incarnation";
      const std::string second_payload = "later slot incarnation";
      ASSERT_EQ (first_payload.size (), second_payload.size ()) << "the two records must be the same size";

      OID first_oid = OID_INITIALIZER;
      ASSERT_EQ (insert_with_stamp (oos_vfid, first_payload, first_oid, first_stamp), NO_ERROR);
      ASSERT_EQ (test_oos_utils::oos_delete_with_current_identity_stamp (thread_p, oos_vfid, first_oid), NO_ERROR);

      OID second_oid = OID_INITIALIZER;
      ASSERT_EQ (insert_with_stamp (oos_vfid, second_payload, second_oid, second_stamp), NO_ERROR);
      ASSERT_TRUE (OID_EQ (&first_oid, &second_oid)) << "the scenario requires physical slot reuse";

      LOG_LSA stored = NULL_LSA;
      ASSERT_EQ (oos_get_identity_stamp (thread_p, second_oid, &stored), NO_ERROR);
      ASSERT_TRUE (LSA_EQ (&second_stamp, &stored)) << "the head chunk must carry the stamp insert reported";
    }
};

/* Both halves of the contract, in the only order the runtime allows, on one database so that they
 * differ in the logging state and in nothing else. Keeping them in one test is what makes the
 * logged-mode half unskippable: were it a test of its own it could only guard itself against a
 * shuffled run by skipping, and skipping is how logged-mode identity coverage gets quietly lost.
 * (test_oos_identity_stamp.SuccessiveOccupantsOfOneSlotCarryDifferentStamps covers the logged case
 * too, and is order-independent because that binary never turns logging off.)
 *
 * The switch in the middle is also the point about which state is authoritative: SA
 * loaddb --no-logging turns logging off through log_set_no_logging() long after the startup
 * parameter was read, so code that must know whether the stamp guarantee holds has to ask
 * log_is_no_logging(). See the file header for what a repeated stamp does NOT show. */
TEST_F (OosNoLoggingTest, SlotReuseSeparatesStampsWithLoggingRepeatsThemWithoutAndLoadsStillReadBack)
{
  ASSERT_FALSE (log_is_no_logging ()) << "logging is off before this test ran; nothing can turn it back on";
  ASSERT_FALSE (prm_get_bool_value (PRM_ID_LOG_NO_LOGGING)) << "this database is configured to log";

  LOG_LSA logged_first = NULL_LSA;
  LOG_LSA logged_second = NULL_LSA;
  stamps_of_two_occupants_of_one_slot (logged_first, logged_second);
  EXPECT_FALSE (LSA_EQ (&logged_first, &logged_second))
      << "logged inserts and deletes advance the page LSA, so the occupants cannot share a stamp";

  /* Retire the logged half's file here, while its removal can still be logged: TearDown skips the
   * removal once logging is off, for the reason stated there. */
  ASSERT_EQ (oos_remove_file (thread_p, oos_vfid), NO_ERROR);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  ASSERT_EQ (log_set_no_logging (), NO_ERROR);
  ASSERT_TRUE (log_is_no_logging ());
  EXPECT_FALSE (prm_get_bool_value (PRM_ID_LOG_NO_LOGGING))
      << "the parameter still says logging is on, so a parameter check would read the state wrong";

  /* A file of its own, so the slot the unlogged half reuses is not one the logged half touched. */
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  LOG_LSA unlogged_first = NULL_LSA;
  LOG_LSA unlogged_second = NULL_LSA;
  stamps_of_two_occupants_of_one_slot (unlogged_first, unlogged_second);
  EXPECT_TRUE (LSA_EQ (&unlogged_first, &unlogged_second))
      << "with the log appends skipped the page LSA does not move, so both occupants read the same value";

  /* And the other half of the accepted policy: documented no-logging bulk loads stay usable, so the
   * values a load stores are the values it reads back. Large enough to span several chunks each,
   * since a multi-chunk chain is where the stub keeps only the head chunk's stamp. */
  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const int batch_size = 8;
  const int batches = 4;

  std::vector<std::string> payloads;
  std::vector<oos_chain_ref> refs;
  payloads.reserve (batch_size * batches);
  refs.reserve (batch_size * batches);

  for (int batch = 0; batch < batches; batch++)
    {
      const std::size_t first = payloads.size ();
      for (int i = 0; i < batch_size; i++)
	{
	  /* Between two and three chunks each, and every value a different length so a mixed-up
	   * chain cannot read back as the right bytes. */
	  payloads.push_back (test_oos_utils::make_repeated_pattern_string (2 * max_chunk_size + 97 * (int) payloads.size ()
			      + 11));
	}

      std::vector<OID> oids (batch_size, OID_INITIALIZER);
      std::vector<LOG_LSA> stamps (batch_size, NULL_LSA);
      std::vector<oos_insert_request> requests;
      requests.reserve (batch_size);
      for (int i = 0; i < batch_size; i++)
	{
	  const std::string &payload = payloads[first + i];
	  oos_insert_request request = { oos_buffer (const_cast<char *> (payload.data ()), payload.size ()),
					 &oids[i], &stamps[i]
				       };
	  requests.push_back (request);
	}
      ASSERT_EQ (oos_insert_many (thread_p, oos_vfid, cubbase::span<oos_insert_request> (requests.data (),
				  requests.size ())), NO_ERROR) << "batch " << batch;

      for (int i = 0; i < batch_size; i++)
	{
	  oos_chain_ref ref;
	  ref.head_oid = oids[i];
	  ref.identity_stamp = stamps[i];
	  refs.push_back (ref);
	}
      /* Load one batch per transaction, the way a bulk load commits periodically. */
      ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED) << "batch " << batch;
    }

  ASSERT_EQ (refs.size (), payloads.size ());
  for (std::size_t i = 0; i < refs.size (); i++)
    {
      EXPECT_EQ (oos_get_length (thread_p, refs[i].head_oid), (int) payloads[i].size ()) << "value " << i;

      std::string out (payloads[i].size (), '?');
      ASSERT_EQ (oos_read (thread_p, refs[i], oos_buffer (out.data (), out.size ())), NO_ERROR) << "value " << i;
      EXPECT_EQ (out, payloads[i]) << "value " << i << " did not read back byte for byte";
    }
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv ("oosnologdb", "./test_oos_no_logging_log"));
  ::testing::GTEST_FLAG (break_on_failure) = true;

  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS ();
}
