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
 * test_oos_reclaim.cpp - the OOS reclaimer and reclaim intents (ADR-0008, CBRD-27781)
 *
 * A chain is reclaimed only after the unit that decided it is dead commits, with redo-only chunk deletes,
 * tail first and the head last; each link carries the next chunk's identity stamp. These tests check the
 * properties the design rests on, against the storage code:
 *
 *   - a reclaimed chunk is never restored, even when an enclosing unit aborts, so another insert may take
 *     its slot at once (the CBRD-27781 shape: an abort writing a chunk back over a reused slot);
 *   - a reclaim interrupted part way leaves a linked prefix with an intact head, and a rerun finishes it;
 *   - a rerun never follows a link into a slot that a later insert reused;
 *   - an intent of an aborted unit is discarded, and a failed reclaim of a committed intent is not
 *     reported as done (fail-stop).
 *
 * Restart recovery's rerun of a durable intent after a real fail-stop is covered at the SQL level by
 * test_oos_sql_crash_recovery.
 */

#include "gtest/gtest.h"

#include <vector>

#include "log_manager.h"
#include "system_parameter.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "oos_file.hpp"
#include "xserver_interface.h"
#include "test_oos_common.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int bridge_oos_get_max_chunk_size_within_page ();

namespace
{
  bool
  peek_header (const OID &oid, OOS_RECORD_HEADER &header_out)
  {
    VPID vpid = {oid.pageid, oid.volid};
    PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_READ,
				   PGBUF_UNCONDITIONAL_LATCH);
    if (page_ptr == nullptr)
      {
	er_clear ();
	return false;
      }
    RECDES rec;
    const bool found = spage_get_record (thread_p, page_ptr, oid.slotid, &rec, PEEK) == S_SUCCESS
		       && rec.length >= OOS_RECORD_HEADER_SIZE;
    if (found)
      {
	std::memcpy (&header_out, rec.data, OOS_RECORD_HEADER_SIZE);
      }
    pgbuf_unfix_and_init (thread_p, page_ptr);
    er_clear ();
    return found;
  }

  /* The slot holds a chunk carrying exactly this stamp. */
  bool
  chunk_is_present (const oos_chain_ref &chunk)
  {
    OOS_RECORD_HEADER header;
    return peek_header (chunk.head_oid, header) && LSA_EQ (&header.identity_stamp, &chunk.identity_stamp);
  }

  /* Inserts a value of n_chunks chunks and returns every chunk with its stamp, head first. */
  void
  insert_chain (const VFID &oos_vfid, int n_chunks, std::vector<oos_chain_ref> &chunks_out)
  {
    const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
    const std::string value = test_oos_utils::make_repeated_pattern_string (max_chunk * (n_chunks - 1) + 50);
    RECDES rec {};
    ASSERT_EQ (test_oos_utils::from_string_into_recdes (value, rec), NO_ERROR);
    test_oos_utils::auto_freed_recdes_ptr defer (&rec, recdes_free_data_area);

    OID head = OID_INITIALIZER;
    ASSERT_EQ (test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, head), NO_ERROR);

    chunks_out.clear ();
    oos_chain_ref chunk = {head, NULL_LSA};
    ASSERT_EQ (oos_get_identity_stamp (thread_p, head, &chunk.identity_stamp), NO_ERROR);
    while (true)
      {
	OOS_RECORD_HEADER header;
	ASSERT_TRUE (peek_header (chunk.head_oid, header));
	ASSERT_EQ (header.chunk_index, (int) chunks_out.size ());
	chunks_out.push_back (chunk);
	if (OID_ISNULL (&header.next_chunk_oid))
	  {
	    break;
	  }
	chunk = {header.next_chunk_oid, header.next_chunk_stamp};
      }
    ASSERT_EQ ((int) chunks_out.size (), n_chunks);
  }

  int g_fail_stops = 0;
  int g_fail_stop_error = NO_ERROR;

  void
  record_fail_stop (int error)
  {
    g_fail_stops++;
    g_fail_stop_error = error;
  }
}

class OosReclaimTest : public ::testing::Test
{
  protected:
    VFID oos_vfid;

    void SetUp () override
    {
      ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);
      g_fail_stops = 0;
      g_fail_stop_error = NO_ERROR;
    }

    void TearDown () override
    {
      oos_test_fail_reclaim_after_chunk_deletes (-1);
      oos_test_set_reclaim_fail_stop_hook (NULL);
      EXPECT_EQ (oos_remove_file (thread_p, oos_vfid), NO_ERROR);
      EXPECT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
    }
};

/* ADR-0008 decision 4: the chunk header grows to 32B and every link carries the next chunk's stamp. */
TEST_F (OosReclaimTest, EveryLinkCarriesTheStampTheNextChunkWasWrittenWith)
{
  EXPECT_EQ (OOS_RECORD_HEADER_SIZE, 32);

  std::vector<oos_chain_ref> chunks;
  insert_chain (oos_vfid, 4, chunks);
  for (std::size_t i = 0; i < chunks.size (); i++)
    {
      LOG_LSA stored = NULL_LSA;
      ASSERT_EQ (oos_get_identity_stamp (thread_p, chunks[i].head_oid, &stored), NO_ERROR);
      EXPECT_TRUE (LSA_EQ (&stored, &chunks[i].identity_stamp)) << "chunk " << i;
    }
  OOS_RECORD_HEADER tail;
  ASSERT_TRUE (peek_header (chunks.back ().head_oid, tail));
  EXPECT_TRUE (OID_ISNULL (&tail.next_chunk_oid));
  EXPECT_TRUE (LSA_ISNULL (&tail.next_chunk_stamp));
}

/* CBRD-27781: an abort of the enclosing unit must not write a reclaimed chunk back, because another insert
 * may already hold its slot. The reclaimer's deletes are redo-only. */
TEST_F (OosReclaimTest, EnclosingAbortRestoresNoReclaimedChunkEvenAfterSlotReuse)
{
  std::vector<oos_chain_ref> chunks;
  insert_chain (oos_vfid, 1, chunks);

  log_sysop_start (thread_p);
  oos_delete_outcome outcome = OOS_DELETE_OUTCOME_UNKNOWN;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, chunks[0], &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_RECLAIMED);

  /* Another unit takes the freed slot and commits: a nested system operation stands for it here, because
   * its commit makes the insert permanent whatever the enclosing unit does next. */
  std::vector<oos_chain_ref> occupant;
  bool reused = false;
  for (int i = 0; i < 2000 && !reused; i++)
    {
      log_sysop_start (thread_p);
      insert_chain (oos_vfid, 1, occupant);
      log_sysop_commit (thread_p);
      reused = OID_EQ (&occupant[0].head_oid, &chunks[0].head_oid);
    }
  ASSERT_TRUE (reused) << "no insert reused the freed slot";

  /* Before ADR-0008 this abort replayed the delete's undo into the occupied slot. */
  log_sysop_abort (thread_p);

  EXPECT_FALSE (chunk_is_present (chunks[0])) << "an abort restored a reclaimed chunk";
  EXPECT_TRUE (chunk_is_present (occupant[0])) << "the abort damaged the committed occupant of the slot";
}

TEST_F (OosReclaimTest, RerunAfterAReclaimInterruptedPartWayFinishesTheChain)
{
  std::vector<oos_chain_ref> chunks;
  insert_chain (oos_vfid, 4, chunks);

  oos_test_fail_reclaim_after_chunk_deletes (2);
  oos_delete_outcome outcome = OOS_DELETE_OUTCOME_UNKNOWN;
  EXPECT_EQ (oos_delete (thread_p, oos_vfid, chunks[0], &outcome), ER_GENERIC_ERROR);
  er_clear ();
  EXPECT_EQ (outcome, OOS_DELETE_OUTCOME_UNKNOWN);

  /* Tail first: chunks 3 and 2 are gone, 0 and 1 still form a linked prefix from an intact head. */
  EXPECT_TRUE (chunk_is_present (chunks[0]));
  EXPECT_TRUE (chunk_is_present (chunks[1]));
  EXPECT_FALSE (chunk_is_present (chunks[2]));
  EXPECT_FALSE (chunk_is_present (chunks[3]));

  ASSERT_EQ (oos_delete (thread_p, oos_vfid, chunks[0], &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_RECLAIMED);
  for (std::size_t i = 0; i < chunks.size (); i++)
    {
      EXPECT_FALSE (chunk_is_present (chunks[i])) << "chunk " << i;
    }

  /* A third run finds nothing left and skips. */
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, chunks[0], &outcome), NO_ERROR);
  EXPECT_NE (outcome, OOS_DELETE_RECLAIMED);
}

TEST_F (OosReclaimTest, RerunDoesNotFollowALinkIntoASlotALaterInsertReused)
{
  std::vector<oos_chain_ref> chunks;
  insert_chain (oos_vfid, 2, chunks);

  /* The first run deletes the tail and fails before the head. */
  oos_test_fail_reclaim_after_chunk_deletes (1);
  EXPECT_EQ (oos_delete (thread_p, oos_vfid, chunks[0]), ER_GENERIC_ERROR);
  er_clear ();
  ASSERT_TRUE (chunk_is_present (chunks[0]));
  ASSERT_FALSE (chunk_is_present (chunks[1]));

  /* A later insert takes the tail's slot; the head's link still names that slot. */
  std::vector<oos_chain_ref> occupant;
  bool reused = false;
  for (int i = 0; i < 2000 && !reused; i++)
    {
      insert_chain (oos_vfid, 1, occupant);
      reused = OID_EQ (&occupant[0].head_oid, &chunks[1].head_oid);
    }
  ASSERT_TRUE (reused) << "no insert reused the freed tail slot";
  ASSERT_FALSE (LSA_EQ (&occupant[0].identity_stamp, &chunks[1].identity_stamp));

  oos_delete_outcome outcome = OOS_DELETE_OUTCOME_UNKNOWN;
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, chunks[0], &outcome), NO_ERROR);
  EXPECT_EQ (outcome, OOS_DELETE_RECLAIMED);
  EXPECT_FALSE (chunk_is_present (chunks[0]));
  EXPECT_TRUE (chunk_is_present (occupant[0])) << "the rerun followed a stale link and deleted another value";
}

TEST_F (OosReclaimTest, IntentOfAnAbortedUnitIsDiscardedAndOfACommittedUnitReclaims)
{
  std::vector<oos_chain_ref> chunks;
  insert_chain (oos_vfid, 3, chunks);
  const OID owner = OID_INITIALIZER;

  log_sysop_start (thread_p);
  ASSERT_EQ (oos_log_reclaim_intent (thread_p, oos_vfid, owner, 0, "unit test", {chunks[0]}), NO_ERROR);
  EXPECT_TRUE (chunk_is_present (chunks[0])) << "an intent must not reclaim before its unit commits";
  log_sysop_abort (thread_p);
  for (std::size_t i = 0; i < chunks.size (); i++)
    {
      EXPECT_TRUE (chunk_is_present (chunks[i])) << "an aborted unit reclaimed chunk " << i;
    }

  const std::size_t emptied_before = oos_test_reclaim_emptied_count (oos_vfid);
  log_sysop_start (thread_p);
  ASSERT_EQ (oos_log_reclaim_intent (thread_p, oos_vfid, owner, 0, "unit test", {chunks[0]}), NO_ERROR);
  log_sysop_commit (thread_p);
  for (std::size_t i = 0; i < chunks.size (); i++)
    {
      EXPECT_FALSE (chunk_is_present (chunks[i])) << "a committed intent left chunk " << i;
    }
  /* Each chunk of a multi-page value fills its own page, so the reclaim emptied pages the next growth reuses. */
  EXPECT_GT (oos_test_reclaim_emptied_count (oos_vfid), emptied_before);
}

TEST_F (OosReclaimTest, FailedReclaimOfACommittedIntentFailStopsInsteadOfReportingDone)
{
  std::vector<oos_chain_ref> chunks;
  insert_chain (oos_vfid, 3, chunks);
  const OID owner = OID_INITIALIZER;

  oos_test_set_reclaim_fail_stop_hook (record_fail_stop);
  oos_test_fail_reclaim_after_chunk_deletes (1);

  log_sysop_start (thread_p);
  ASSERT_EQ (oos_log_reclaim_intent (thread_p, oos_vfid, owner, 0, "unit test", {chunks[0]}), NO_ERROR);
  log_sysop_commit (thread_p);

  EXPECT_EQ (g_fail_stops, 1) << "a failed reclaim of a committed intent must stop the server";
  EXPECT_EQ (g_fail_stop_error, ER_GENERIC_ERROR);
  er_clear ();

  /* What the stopped server leaves for restart recovery: a linked prefix from an intact head. */
  EXPECT_TRUE (chunk_is_present (chunks[0]));
  EXPECT_TRUE (chunk_is_present (chunks[1]));
  EXPECT_FALSE (chunk_is_present (chunks[2]));

  /* The rerun recovery performs. */
  oos_test_set_reclaim_fail_stop_hook (NULL);
  ASSERT_EQ (oos_delete (thread_p, oos_vfid, chunks[0]), NO_ERROR);
  for (std::size_t i = 0; i < chunks.size (); i++)
    {
      EXPECT_FALSE (chunk_is_present (chunks[i])) << "chunk " << i;
    }
}

/* log_do_postpone, the path a transaction takes when its postpones overflow the postpone cache (and the one
 * restart recovery takes), skipped a postpone whose LSA is the last parent of a system operation that started
 * right after it. An UPDATE that drops a chain followed by an OOS page allocation, as in a bulk UPDATE, lost its
 * reclaim this way and leaked the chain. */
TEST_F (OosReclaimTest, PostponeRightBeforeASystemOperationRunsWhenTheCacheOverflows)
{
  std::vector<oos_chain_ref> chunks;
  insert_chain (oos_vfid, 1, chunks);
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  const OID owner = OID_INITIALIZER;

  ASSERT_EQ (oos_log_reclaim_intent (thread_p, oos_vfid, owner, 0, "unit test", {chunks[0]}), NO_ERROR);

  /* A committed system operation whose last parent is the postpone above. */
  std::vector<oos_chain_ref> other;
  log_sysop_start (thread_p);
  insert_chain (oos_vfid, 1, other);
  log_sysop_commit (thread_p);

  /* Overflow the postpone cache so the commit falls back to log_do_postpone. The references are stale: their
   * reclaim skips. */
  oos_chain_ref stale = other[0];
  stale.identity_stamp.offset++;
  const int overflow = prm_get_integer_value (PRM_ID_LOG_POSTPONE_CACHE_SIZE) + 8;
  for (int i = 0; i < overflow; i++)
    {
      ASSERT_EQ (oos_log_reclaim_intent (thread_p, oos_vfid, owner, 0, "unit test", {stale}), NO_ERROR);
    }
  ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);

  EXPECT_FALSE (chunk_is_present (chunks[0])) << "the postpone right before the system operation did not run";
  EXPECT_TRUE (chunk_is_present (other[0])) << "a stale reference reclaimed a live chain";
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv ());
  return RUN_ALL_TESTS ();
}
