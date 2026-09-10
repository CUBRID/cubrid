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
 * precondition rather than rejecting the write, so these tests pin down both halves: the same
 * scenario separates the stamps while logging is on and repeats them while it is off, and a
 * no-logging bulk load of large values still stores values that read back byte for byte.
 *
 * Two properties of the runtime shape this binary:
 *
 * - The actual logging state is process-global and one-way. log_set_no_logging() has no inverse, so
 *   the logged-mode case has to run before the switch. Every case states its own precondition
 *   instead of trusting the order.
 * - No-logging writes are not recoverable, which is exactly why the utility that offers them tells
 *   the operator to back up first. This binary therefore owns its database (oosnologdb) instead of
 *   sharing the unittestdb fixture with the rest of the OOS suite.
 *
 * What a repeated stamp here does NOT show: that ordinary bulk loading produces a stale reclamation
 * request that deletes live data. No-logging is standalone-only, where there is no vacuum block
 * retry to replay a stale reference, and the loss scenario CBRD-26950 fixes needs such a replay. The
 * case below is the disclosed limit of the guarantee, not a reproduction of the bug.
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

    /* Turns the actual logging state off if it is still on. */
    static void ensure_no_logging ()
    {
      if (!log_is_no_logging ())
	{
	  ASSERT_EQ (log_set_no_logging (), NO_ERROR);
	}
      ASSERT_TRUE (log_is_no_logging ());
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

/* The logged-mode half of the contract, on this binary's own database so that the no-logging case
 * below differs from it in the logging state and in nothing else. Skips rather than fails if the
 * switch has already happened, because it is one-way and this case needs it off. */
TEST_F (OosNoLoggingTest, LoggedModeSeparatesTheStampsOfTwoOccupantsOfOneSlot)
{
  if (log_is_no_logging ())
    {
      GTEST_SKIP () << "logging is already disabled in this process and cannot be turned back on";
    }

  LOG_LSA first_stamp = NULL_LSA;
  LOG_LSA second_stamp = NULL_LSA;
  stamps_of_two_occupants_of_one_slot (first_stamp, second_stamp);

  EXPECT_FALSE (LSA_EQ (&first_stamp, &second_stamp))
      << "logged inserts and deletes advance the page LSA, so the occupants cannot share a stamp";
}

/* The startup parameter is not the logging state: loaddb --no-logging turns logging off through
 * log_set_no_logging() long after the parameter was read, so code that must know whether the stamp
 * guarantee holds has to ask log_is_no_logging(). */
TEST_F (OosNoLoggingTest, RuntimeActivationChangesTheStateWithoutChangingTheStartupParameter)
{
  if (log_is_no_logging ())
    {
      GTEST_SKIP () << "logging is already disabled in this process and cannot be turned back on";
    }
  ASSERT_FALSE (prm_get_bool_value (PRM_ID_LOG_NO_LOGGING)) << "this database is configured to log";

  ASSERT_EQ (log_set_no_logging (), NO_ERROR);

  EXPECT_TRUE (log_is_no_logging ());
  EXPECT_FALSE (prm_get_bool_value (PRM_ID_LOG_NO_LOGGING))
      << "the parameter still says logging is on, so a parameter check would read the state wrong";
}

/* The disclosed limit of the guarantee. See the file header for what this does not show. */
TEST_F (OosNoLoggingTest, SameSlotReuseWithoutLoggingCanRepeatTheStamp)
{
  ensure_no_logging ();

  LOG_LSA first_stamp = NULL_LSA;
  LOG_LSA second_stamp = NULL_LSA;
  stamps_of_two_occupants_of_one_slot (first_stamp, second_stamp);

  EXPECT_TRUE (LSA_EQ (&first_stamp, &second_stamp))
      << "with the log appends skipped the page LSA does not move, so both occupants read the same value";
}

/* Documented no-logging bulk loads stay usable: the values a load stores are the values it reads
 * back. Large enough to span several chunks each, since a multi-chunk chain is where the stub keeps
 * only the head chunk's stamp. */
TEST_F (OosNoLoggingTest, BulkLoadWithoutLoggingStoresLargeValuesThatReadBack)
{
  ensure_no_logging ();

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
