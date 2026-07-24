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
#include "connection_defs.h"
#include "heap_oos.hpp"
#include "locator_sr.h"
#include "log_comm.h"
#include "lock_manager.h"
#include "scope_exit.hpp"
#include "server_support.h"
#include "xserver_interface.h"
#include "test_oos_server_common.hpp"

#include <vector>

/* bridge functions defined in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();
void bridge_oos_debug_counters_reset ();
oos_debug_counters bridge_oos_debug_counters_get ();
void bridge_heap_attrinfo_fail_after_oos_publication_reset_once ();
void bridge_heap_attrinfo_disarm_publication_reset_failure ();
SCAN_CODE bridge_heap_attrinfo_insert_to_oos (THREAD_ENTRY *thread_p, const OID *class_oid);
int bridge_locator_fixup_oos_oids_in_recdes (THREAD_ENTRY *thread_p, const OID *class_oid, RECDES *recdes);

static std::string make_filled_payload (int size, char ch);
static void clear_oos_insert_publication_state_for_test ();
static void build_insert_requests (std::vector<std::string> &payloads, std::vector<OID> &oids,
			   std::vector<oos_insert_request> &requests);

static LOG_TDES *
get_current_tdes ()
{
  return LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
}

static OID
make_test_oid (VOLID volid, PAGEID pageid, PGSLOTID slotid)
{
  OID oid = OID_INITIALIZER;
  oid.volid = volid;
  oid.pageid = pageid;
  oid.slotid = slotid;
  return oid;
}

static void
seed_oos_insert_publication_state (const OID &oid, const LOG_LSA &lsa)
{
  LOG_TDES *tdes = get_current_tdes ();
  ASSERT_NE (tdes, nullptr);

  thread_p->oos_oids.clear ();
  tdes->oos_insert_lsa_queue.clear ();
  thread_p->oos_oids.push_back (oid);
  tdes->oos_insert_lsa_queue.push (lsa);
}

static void
assert_oos_insert_publication_state (const OID &oid, const LOG_LSA &lsa)
{
  LOG_TDES *tdes = get_current_tdes ();
  ASSERT_NE (tdes, nullptr);
  ASSERT_EQ (thread_p->oos_oids.size (), 1U);
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[0], &oid));
  ASSERT_EQ (tdes->oos_insert_lsa_queue.size (), 1U);
  EXPECT_TRUE (LSA_EQ (&tdes->oos_insert_lsa_queue.front (), &lsa));
}

static void
assert_oos_insert_publication_state_empty ()
{
  LOG_TDES *tdes = get_current_tdes ();
  ASSERT_NE (tdes, nullptr);
  EXPECT_TRUE (thread_p->oos_oids.empty ());
  EXPECT_TRUE (tdes->oos_insert_lsa_queue.is_empty ());
}

static OID
find_db_user_class_oid ()
{
  OID class_oid = OID_INITIALIZER;
  EXPECT_EQ (xlocator_find_class_oid (thread_p, "_db_user", &class_oid, NULL_LOCK), LC_CLASSNAME_EXIST);
  EXPECT_FALSE (OID_ISNULL (&class_oid));
  return class_oid;
}

static int
build_replicated_heap_recdes (const OID &class_oid, const std::vector<OID> &placeholder_oids, RECDES &recdes)
{
  HEAP_CACHE_ATTRINFO attr_info;
  int error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      return error;
    }

  const int n_variables = attr_info.last_classrepr->n_variable;
  heap_attrinfo_end (thread_p, &attr_info);
  if (n_variables < (int) placeholder_oids.size ())
    {
      return ER_FAILED;
    }

  const int header_size = OR_MVCC_REP_SIZE + OR_CHN_SIZE;
  const int vot_entries = n_variables + 1;
  const int vot_bytes = vot_entries * OR_SHORT_SIZE;
  const int total_size = header_size + vot_bytes + (int) placeholder_oids.size () * OR_OOS_INLINE_SIZE;
  error = recdes_allocate_data_area (&recdes, total_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  recdes.type = REC_HOME;
  recdes.length = total_size;
  std::memset (recdes.data, 0, total_size);
  OR_PUT_INT (recdes.data + OR_REP_OFFSET,
	      (OR_MVCC_FLAG_HAS_OOS << OR_MVCC_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_2BYTE);

  char *vot = recdes.data + header_size;
  for (int i = 0; i <= n_variables; i++)
    {
      const int value_count = i < (int) placeholder_oids.size () ? i : (int) placeholder_oids.size ();
      int offset = vot_bytes + value_count * OR_OOS_INLINE_SIZE;
      if (i < (int) placeholder_oids.size ())
	{
	  offset |= OR_VAR_BIT_OOS;
	}
      if (i == n_variables)
	{
	  offset |= OR_VAR_BIT_LAST_ELEMENT;
	}
      OR_PUT_SHORT (vot + i * OR_SHORT_SIZE, offset);
    }

  char *inline_data = vot + vot_bytes;
  for (std::size_t i = 0; i < placeholder_oids.size (); i++)
    {
      OR_PUT_OID (inline_data + i * OR_OOS_INLINE_SIZE, &placeholder_oids[i]);
      INT64 length = 1000 + (INT64) i;
      OR_PUT_BIGINT (inline_data + i * OR_OOS_INLINE_SIZE + OR_OID_SIZE, &length);
    }
  return NO_ERROR;
}

static int
get_class_variable_count (const OID &class_oid)
{
  HEAP_CACHE_ATTRINFO attr_info;
  int error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      return -1;
    }
  const int n_variables = attr_info.last_classrepr->n_variable;
  heap_attrinfo_end (thread_p, &attr_info);
  return n_variables;
}

static OID
read_replicated_heap_oos_oid (RECDES &recdes, int n_variables, int index)
{
  const int header_size = OR_MVCC_REP_SIZE + OR_CHN_SIZE;
  const int vot_bytes = (n_variables + 1) * OR_SHORT_SIZE;
  OID oid = OID_INITIALIZER;
  OR_GET_OID (recdes.data + header_size + vot_bytes + index * OR_OOS_INLINE_SIZE, &oid);
  return oid;
}

static int
build_oos_replication_recdes (const std::string &payload, RECDES &recdes)
{
  const int total_size = OOS_RECORD_HEADER_SIZE + (int) payload.size ();
  int error = recdes_allocate_data_area (&recdes, total_size);
  if (error != NO_ERROR)
    {
      return error;
    }

  recdes.type = REC_HOME;
  recdes.length = total_size;
  const OOS_RECORD_HEADER header { (int) payload.size (), 0, OID_INITIALIZER };
  std::memcpy (recdes.data, &header, OOS_RECORD_HEADER_SIZE);
  std::memcpy (recdes.data + OOS_RECORD_HEADER_SIZE, payload.data (), payload.size ());
  return NO_ERROR;
}

class replication_tracking_guard
{
  public:
    replication_tracking_guard ()
      : m_old_ha_mode (prm_get_integer_value (PRM_ID_HA_MODE))
      , m_old_ha_state (css_ha_server_state ())
      , m_error (NO_ERROR)
    {
      prm_set_integer_value (PRM_ID_HA_MODE, HA_MODE_FAIL_BACK);
      m_error = css_change_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE, true, 0, true);
      er_clear ();
    }

    ~replication_tracking_guard ()
    {
      (void) css_change_ha_server_state (thread_p, m_old_ha_state, true, 0, true);
      prm_set_integer_value (PRM_ID_HA_SERVER_STATE, m_old_ha_state);
      prm_set_integer_value (PRM_ID_HA_MODE, m_old_ha_mode);
      er_clear ();
    }

    int error () const
    {
      return m_error;
    }

  private:
    int m_old_ha_mode;
    HA_SERVER_STATE m_old_ha_state;
    int m_error;
};

// ============================================================================
// TC: OOS owner descriptor and online file-tracker protection (CBRD-27038)
// ============================================================================
TEST (OosServerTest, OosOwnerDescriptorSupportsDiagnosticsAndProtectedIteration)
{
  const OID class_oid = find_db_user_class_oid ();
  ASSERT_FALSE (OID_ISNULL (&class_oid));

  HFID hfid;
  HFID_SET_NULL (&hfid);
  ASSERT_EQ (xheap_create (thread_p, &hfid, &class_oid, false), NO_ERROR);
  scope_exit destroy_heap ([&] () noexcept
  {
    if (!HFID_IS_NULL (&hfid))
      {
	(void) xheap_destroy (thread_p, &hfid, &class_oid);
	(void) xtran_server_commit (thread_p, false);
      }
  });

  VFID oos_vfid;
  VFID_SET_NULL (&oos_vfid);
  ASSERT_TRUE (heap_oos_find_vfid (thread_p, &hfid, &oos_vfid, true));
  ASSERT_FALSE (VFID_ISNULL (&oos_vfid));

  VFID legacy_oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, legacy_oos_vfid), NO_ERROR);
  scope_exit destroy_legacy_oos ([&] () noexcept
  {
    (void) oos_remove_file (thread_p, legacy_oos_vfid);
  });

  FILE_DESCRIPTORS descriptor;
  ASSERT_EQ (file_descriptor_get (thread_p, &oos_vfid, &descriptor), NO_ERROR);
  EXPECT_TRUE (HFID_EQ (&descriptor.heap_overflow.hfid, &hfid));
  EXPECT_TRUE (OID_EQ (&descriptor.heap_overflow.class_oid, &class_oid));

  FILE *dump_fp = tmpfile ();
  ASSERT_NE (dump_fp, nullptr);
  scope_exit close_dump ([&] () noexcept
  {
    fclose (dump_fp);
  });
  ASSERT_EQ (xfile_tracker_dump_file_list (thread_p, dump_fp, false), NO_ERROR);
  rewind (dump_fp);

  std::string dump_output;
  char line[1024];
  while (fgets (line, sizeof (line), dump_fp) != nullptr)
    {
      dump_output += line;
    }

  char expected_owner[256];
  snprintf (expected_owner, sizeof (expected_owner),
	    "CLASS_OID: %5d|%10d|%5d (_db_user), OOS for HFID: %10d|%5d|%10d", OID_AS_ARGS (&class_oid),
	    HFID_AS_ARGS (&hfid));
  EXPECT_NE (dump_output.find (expected_owner), std::string::npos);
  EXPECT_NE (dump_output.find ("OOS file (owner descriptor unavailable)"), std::string::npos);

  VFID iter_vfid;
  VFID_SET_NULL (&iter_vfid);
  OID locked_class_oid;
  OID_SET_NULL (&locked_class_oid);
  bool found_oos_vfid = false;
  bool schema_modification_was_blocked = false;
  const int checkdb_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  int ddl_tran_index = NULL_TRAN_INDEX;
  scope_exit release_ddl_transaction ([&] () noexcept
  {
    if (ddl_tran_index != NULL_TRAN_INDEX)
      {
	LOG_SET_CURRENT_TRAN_INDEX (thread_p, ddl_tran_index);
	(void) log_abort (thread_p, ddl_tran_index);
	logtb_release_tran_index (thread_p, ddl_tran_index);
	LOG_SET_CURRENT_TRAN_INDEX (thread_p, checkdb_tran_index);
      }
  });

  while (true)
    {
      ASSERT_EQ (file_tracker_interruptable_iterate (thread_p, FILE_OOS, &iter_vfid, &locked_class_oid), NO_ERROR);
      if (VFID_ISNULL (&iter_vfid))
	{
	  break;
	}
      EXPECT_FALSE (OID_ISNULL (&locked_class_oid));
      if (VFID_EQ (&iter_vfid, &oos_vfid))
	{
	  EXPECT_TRUE (OID_EQ (&locked_class_oid, &class_oid));
	  found_oos_vfid = true;

	  TRAN_STATE ddl_tran_state;
	  ddl_tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &ddl_tran_state,
						   TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
	  ASSERT_NE (ddl_tran_index, NULL_TRAN_INDEX);
	  ASSERT_NE (lock_object (thread_p, &class_oid, oid_Root_class_oid, SCH_M_LOCK, LK_COND_LOCK), LK_GRANTED);
	  schema_modification_was_blocked = true;
	  er_clear ();
	  LOG_SET_CURRENT_TRAN_INDEX (thread_p, checkdb_tran_index);
	}
    }
  EXPECT_TRUE (found_oos_vfid);
  EXPECT_TRUE (schema_modification_was_blocked);
  /* The ownerless file models a byte-zeroed legacy descriptor and is never returned without protection. */
  EXPECT_TRUE (VFID_ISNULL (&iter_vfid));
  EXPECT_TRUE (OID_ISNULL (&locked_class_oid));

  ASSERT_NE (ddl_tran_index, NULL_TRAN_INDEX);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, ddl_tran_index);
  ASSERT_EQ (lock_object (thread_p, &class_oid, oid_Root_class_oid, SCH_M_LOCK, LK_COND_LOCK), LK_GRANTED);
  lock_unlock_object (thread_p, &class_oid, oid_Root_class_oid, SCH_M_LOCK, true);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, checkdb_tran_index);

  EXPECT_EQ (file_tracker_check (thread_p), DISK_VALID);
}

// ============================================================================
// TC: OOS insert publication-state lifetime (CBRD-27006)
// ============================================================================
TEST (OosServerTest, OosPublicationBeginClearsOidAndLsaTogether)
{
  const OID stale_oid = make_test_oid (1, 987654, 23);
  const LOG_LSA stale_lsa { 876543, 123 };
  seed_oos_insert_publication_state (stale_oid, stale_lsa);

  ASSERT_EQ (heap_oos_begin_insert_publication (thread_p), S_SUCCESS);
  assert_oos_insert_publication_state_empty ();
}

TEST (OosServerTest, OosPublicationBeginMissingTdesLeavesBothSidesUntouched)
{
  const OID stale_oid = make_test_oid (1, 987655, 24);
  const LOG_LSA stale_lsa { 876544, 124 };
  seed_oos_insert_publication_state (stale_oid, stale_lsa);

  const int saved_tran_index = thread_p->tran_index;
  scope_exit restore_tran_index ([&] () noexcept
  {
    thread_p->tran_index = saved_tran_index;
  });

  thread_p->tran_index = NULL_TRAN_INDEX;
  er_clear ();
  const SCAN_CODE result = heap_oos_begin_insert_publication (thread_p);
  const int error = er_errid ();
  thread_p->tran_index = saved_tran_index;
  restore_tran_index.release ();

  EXPECT_EQ (result, S_ERROR);
  EXPECT_EQ (error, ER_LOG_UNKNOWN_TRANINDEX);
  assert_oos_insert_publication_state (stale_oid, stale_lsa);
  er_clear ();
}

TEST (OosServerTest, OosLogicalPreparationFailureSeesCleanPublicationState)
{
  const OID stale_oid = make_test_oid (1, 987656, 25);
  const LOG_LSA stale_lsa { 876545, 125 };
  seed_oos_insert_publication_state (stale_oid, stale_lsa);

  bridge_heap_attrinfo_fail_after_oos_publication_reset_once ();
  scope_exit disarm ([&] () noexcept
  {
    bridge_heap_attrinfo_disarm_publication_reset_failure ();
  });

  OID invalid_class_oid = make_test_oid (1, 999999, 1);
  EXPECT_EQ (bridge_heap_attrinfo_insert_to_oos (thread_p, &invalid_class_oid), S_ERROR);
  assert_oos_insert_publication_state_empty ();
  er_clear ();
}

TEST (OosServerTest, OosClassLookupFailureSeesCleanPublicationState)
{
  const OID stale_oid = make_test_oid (1, 987657, 26);
  const LOG_LSA stale_lsa { 876546, 126 };
  seed_oos_insert_publication_state (stale_oid, stale_lsa);

  OID invalid_class_oid = find_db_user_class_oid ();
  ASSERT_FALSE (OID_ISNULL (&invalid_class_oid));
  invalid_class_oid.slotid = SHRT_MAX;
  EXPECT_EQ (bridge_heap_attrinfo_insert_to_oos (thread_p, &invalid_class_oid), S_ERROR);
  assert_oos_insert_publication_state_empty ();
  er_clear ();
}

TEST (OosServerTest, OosVfidLookupFailureSeesCleanPublicationState)
{
  const OID stale_oid = make_test_oid (1, 987658, 27);
  const LOG_LSA stale_lsa { 876547, 127 };
  seed_oos_insert_publication_state (stale_oid, stale_lsa);

  const OID class_oid = find_db_user_class_oid ();
  ASSERT_FALSE (OID_ISNULL (&class_oid));
  heap_oos_test_fail_before_vfid_lookup_once ();
  scope_exit disarm ([&] () noexcept
  {
    heap_oos_test_disarm_fail_before_vfid_lookup ();
  });

  EXPECT_EQ (bridge_heap_attrinfo_insert_to_oos (thread_p, &class_oid), S_ERROR);
  EXPECT_EQ (er_errid (), ER_GENERIC_ERROR);
  assert_oos_insert_publication_state_empty ();
  er_clear ();
}

TEST (OosServerTest, OosInsertManyPartialPublicationFailureClearsBothSides)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  LOG_LSA topop_lsa;
  ASSERT_EQ (xtran_server_start_topop (thread_p, &topop_lsa), NO_ERROR);
  scope_exit abort_topop ([&] () noexcept
  {
    (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &topop_lsa);
  });

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  std::vector<std::string> payloads =
  {
    make_filled_payload (1000, 'p'),
    make_filled_payload (max_chunk_size + 100, 'q')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);
  clear_oos_insert_publication_state_for_test ();

  oos_test_fail_insert_many_after_publications (1);
  scope_exit disarm ([&] () noexcept
  {
    oos_test_disarm_insert_publication_failures ();
  });

  EXPECT_EQ (oos_insert_many (thread_p, oos_vfid,
	      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), ER_GENERIC_ERROR);
  assert_oos_insert_publication_state_empty ();
  er_clear ();
}

TEST (OosServerTest, OosInsertManyPublicationAllocationFailureIsConvertedAndCleared)
{
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  LOG_LSA topop_lsa;
  ASSERT_EQ (xtran_server_start_topop (thread_p, &topop_lsa), NO_ERROR);
  scope_exit abort_topop ([&] () noexcept
  {
    (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &topop_lsa);
  });

  std::vector<std::string> payloads = { make_filled_payload (1000, 'a') };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);
  clear_oos_insert_publication_state_for_test ();

  oos_test_throw_bad_alloc_on_next_oid_publication ();
  scope_exit disarm ([&] () noexcept
  {
    oos_test_disarm_insert_publication_failures ();
  });

  EXPECT_EQ (oos_insert_many (thread_p, oos_vfid,
	      cubbase::span<oos_insert_request> (requests.data (), requests.size ())),
	     ER_OUT_OF_VIRTUAL_MEMORY);
  EXPECT_EQ (er_errid (), ER_OUT_OF_VIRTUAL_MEMORY);
  assert_oos_insert_publication_state_empty ();
  er_clear ();
}

TEST (OosServerTest, OosInsertManyValidationFailureDoesNotClaimPublicationStart)
{
  const OID stale_oid = make_test_oid (1, 987659, 28);
  const LOG_LSA stale_lsa { 876548, 128 };
  seed_oos_insert_publication_state (stale_oid, stale_lsa);

  OID output_oid = OID_INITIALIZER;
  VFID invalid_vfid = VFID_INITIALIZER;
  oos_insert_request invalid_request = { oos_buffer (nullptr, 0), &output_oid };
  EXPECT_EQ (oos_insert_many (thread_p, invalid_vfid,
	      cubbase::span<oos_insert_request> (&invalid_request, 1)), ER_GENERIC_ERROR);
  assert_oos_insert_publication_state (stale_oid, stale_lsa);
  er_clear ();
}

TEST (OosServerTest, OosSuccessfulPublicationWithoutReplicationTrackingKeepsOidOnly)
{
  ASSERT_FALSE (log_does_allow_replication ());
  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);

  std::vector<std::string> payloads =
  {
    make_filled_payload (900, 'x'),
    make_filled_payload (1000, 'y')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);
  clear_oos_insert_publication_state_for_test ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
	      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);
  LOG_TDES *tdes = get_current_tdes ();
  ASSERT_NE (tdes, nullptr);
  ASSERT_EQ (thread_p->oos_oids.size (), 2U);
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[0], &oids[0]));
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[1], &oids[1]));
  EXPECT_TRUE (tdes->oos_insert_lsa_queue.is_empty ());
}

TEST (OosServerTest, OosTrackedSingleChunkBatchKeepsPairedPublication)
{
  replication_tracking_guard tracking;
  ASSERT_EQ (tracking.error (), NO_ERROR);
  ASSERT_TRUE (log_does_allow_replication ());

  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);
  std::vector<std::string> payloads =
  {
    make_filled_payload (900, 'i'),
    make_filled_payload (1000, 'j')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);
  clear_oos_insert_publication_state_for_test ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
	      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);
  LOG_TDES *tdes = get_current_tdes ();
  ASSERT_NE (tdes, nullptr);
  ASSERT_EQ (thread_p->oos_oids.size (), 2U);
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[0], &oids[0]));
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[1], &oids[1]));
  EXPECT_EQ (tdes->oos_insert_lsa_queue.size (), 2U);
  EXPECT_FALSE (LSA_ISNULL (&tdes->oos_insert_lsa_queue.front ()));
}

TEST (OosServerTest, OosTrackedMixedBatchPreservesDummyAndHeadPairing)
{
  replication_tracking_guard tracking;
  ASSERT_EQ (tracking.error (), NO_ERROR);
  ASSERT_TRUE (log_does_allow_replication ());

  VFID oos_vfid;
  ASSERT_EQ (oos_create_file (thread_p, oos_vfid), NO_ERROR);
  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  std::vector<std::string> payloads =
  {
    make_filled_payload (900, 'k'),
    make_filled_payload (max_chunk_size + 100, 'l'),
    make_filled_payload (1000, 'm')
  };
  std::vector<OID> oids;
  std::vector<oos_insert_request> requests;
  build_insert_requests (payloads, oids, requests);
  clear_oos_insert_publication_state_for_test ();

  ASSERT_EQ (oos_insert_many (thread_p, oos_vfid,
	      cubbase::span<oos_insert_request> (requests.data (), requests.size ())), NO_ERROR);
  LOG_TDES *tdes = get_current_tdes ();
  ASSERT_NE (tdes, nullptr);
  ASSERT_EQ (thread_p->oos_oids.size (), 4U);
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[0], &oids[0]));
  EXPECT_TRUE (OID_ISNULL (&thread_p->oos_oids[1]));
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[2], &oids[1]));
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[3], &oids[2]));
  EXPECT_EQ (tdes->oos_insert_lsa_queue.size (), 4U);
  EXPECT_FALSE (LSA_ISNULL (&tdes->oos_insert_lsa_queue.front ()));
}

TEST (OosServerTest, ReplicaOosItemsAccumulateAndFixupConsumesInOrder)
{
  const OID class_oid = find_db_user_class_oid ();
  ASSERT_FALSE (OID_ISNULL (&class_oid));

  LOG_LSA topop_lsa;
  ASSERT_EQ (xtran_server_start_topop (thread_p, &topop_lsa), NO_ERROR);
  scope_exit abort_topop ([&] () noexcept
  {
    (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &topop_lsa);
  });

  RECDES oos_recdes1 = RECDES_INITIALIZER;
  RECDES oos_recdes2 = RECDES_INITIALIZER;
  ASSERT_EQ (build_oos_replication_recdes (make_filled_payload (1000, 'r'), oos_recdes1), NO_ERROR);
  ASSERT_EQ (build_oos_replication_recdes (make_filled_payload (1100, 's'), oos_recdes2), NO_ERROR);
  scope_exit free_oos_recdes ([&] () noexcept
  {
    recdes_free_data_area (&oos_recdes1);
    recdes_free_data_area (&oos_recdes2);
  });

  clear_oos_insert_publication_state_for_test ();
  OID mutable_class_oid = class_oid;
  ASSERT_EQ (locator_oos_insert_force (thread_p, &mutable_class_oid, &oos_recdes1), NO_ERROR);
  ASSERT_EQ (locator_oos_insert_force (thread_p, &mutable_class_oid, &oos_recdes2), NO_ERROR);
  ASSERT_EQ (thread_p->oos_oids.size (), 2U);
  const OID slave_oid1 = thread_p->oos_oids[0];
  const OID slave_oid2 = thread_p->oos_oids[1];

  const OID placeholder1 = make_test_oid (2, 765431, 31);
  const OID placeholder2 = make_test_oid (2, 765432, 32);
  RECDES heap_recdes = RECDES_INITIALIZER;
  ASSERT_EQ (build_replicated_heap_recdes (class_oid, { placeholder1, placeholder2 }, heap_recdes), NO_ERROR);
  scope_exit free_heap_recdes ([&] () noexcept
  {
    recdes_free_data_area (&heap_recdes);
  });

  ASSERT_EQ (bridge_locator_fixup_oos_oids_in_recdes (thread_p, &class_oid, &heap_recdes), NO_ERROR);
  const int n_variables = get_class_variable_count (class_oid);
  ASSERT_GE (n_variables, 2);
  const OID fixed_oid1 = read_replicated_heap_oos_oid (heap_recdes, n_variables, 0);
  const OID fixed_oid2 = read_replicated_heap_oos_oid (heap_recdes, n_variables, 1);
  EXPECT_TRUE (OID_EQ (&fixed_oid1, &slave_oid1));
  EXPECT_TRUE (OID_EQ (&fixed_oid2, &slave_oid2));
}

TEST (OosServerTest, ReplicaFixupRejectsInsufficientOids)
{
  const OID class_oid = find_db_user_class_oid ();
  ASSERT_FALSE (OID_ISNULL (&class_oid));
  const OID accumulated_oid = make_test_oid (1, 765433, 33);
  thread_p->oos_oids = { accumulated_oid };

  const OID placeholder1 = make_test_oid (2, 765434, 34);
  const OID placeholder2 = make_test_oid (2, 765435, 35);
  RECDES recdes = RECDES_INITIALIZER;
  ASSERT_EQ (build_replicated_heap_recdes (class_oid, { placeholder1, placeholder2 }, recdes), NO_ERROR);
  scope_exit free_recdes ([&] () noexcept
  {
    recdes_free_data_area (&recdes);
  });

  EXPECT_EQ (bridge_locator_fixup_oos_oids_in_recdes (thread_p, &class_oid, &recdes), ER_HA_GENERIC_ERROR);
  EXPECT_EQ (er_errid (), ER_HA_GENERIC_ERROR);
  er_clear ();
}

TEST (OosServerTest, ReplicaFixupRejectsExtraOids)
{
  const OID class_oid = find_db_user_class_oid ();
  ASSERT_FALSE (OID_ISNULL (&class_oid));
  const OID accumulated_oid1 = make_test_oid (1, 765436, 36);
  const OID accumulated_oid2 = make_test_oid (1, 765437, 37);
  thread_p->oos_oids = { accumulated_oid1, accumulated_oid2 };

  const OID placeholder = make_test_oid (2, 765438, 38);
  RECDES recdes = RECDES_INITIALIZER;
  ASSERT_EQ (build_replicated_heap_recdes (class_oid, { placeholder }, recdes), NO_ERROR);
  scope_exit free_recdes ([&] () noexcept
  {
    recdes_free_data_area (&recdes);
  });

  EXPECT_EQ (bridge_locator_fixup_oos_oids_in_recdes (thread_p, &class_oid, &recdes), ER_HA_GENERIC_ERROR);
  EXPECT_EQ (er_errid (), ER_HA_GENERIC_ERROR);
  er_clear ();
}

TEST (OosServerTest, ReplicaScalarPublicationAllocationFailureInvalidatesAccumulator)
{
  const OID class_oid = find_db_user_class_oid ();
  ASSERT_FALSE (OID_ISNULL (&class_oid));

  LOG_LSA topop_lsa;
  ASSERT_EQ (xtran_server_start_topop (thread_p, &topop_lsa), NO_ERROR);
  scope_exit abort_topop ([&] () noexcept
  {
    (void) xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &topop_lsa);
  });

  RECDES recdes = RECDES_INITIALIZER;
  ASSERT_EQ (build_oos_replication_recdes (make_filled_payload (1000, 't'), recdes), NO_ERROR);
  scope_exit free_recdes ([&] () noexcept
  {
    recdes_free_data_area (&recdes);
  });

  thread_p->oos_oids = { make_test_oid (1, 765439, 39) };
  LOG_TDES *tdes = get_current_tdes ();
  ASSERT_NE (tdes, nullptr);
  tdes->oos_insert_lsa_queue.clear ();
  oos_test_throw_bad_alloc_on_next_oid_publication ();
  scope_exit disarm ([&] () noexcept
  {
    oos_test_disarm_insert_publication_failures ();
  });

  OID mutable_class_oid = class_oid;
  EXPECT_EQ (locator_oos_insert_force (thread_p, &mutable_class_oid, &recdes), ER_OUT_OF_VIRTUAL_MEMORY);
  EXPECT_EQ (er_errid (), ER_OUT_OF_VIRTUAL_MEMORY);
  assert_oos_insert_publication_state_empty ();
  er_clear ();
}

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
