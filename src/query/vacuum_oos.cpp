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
 * vacuum_oos.cpp - Vacuum-side OOS (Out-of-row Overflow Storage) reclamation
 */

#include "vacuum_oos.hpp"

#include "error_manager.h"
#include "fault_injection.h"
#include "file_manager.h"
#include "heap_file.h"
#include "log_impl.h"
#include "log_manager.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "oid.h"
#include "oos_file.hpp"
#include "storage_common.h"
#include "vacuum.h"

#include <algorithm>
#include <cstring>
#include <new>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * vacuum_oos_reclaim_empty_pages () - Reclaim the pages a batch of OOS reclaims emptied (fast path).
 *   Only ER_INTERRUPTED is propagated; other failures are absorbed (the pages stay allocated,
 *   candidates for the growth-gate sweep).
 *
 * return                 : NO_ERROR or ER_INTERRUPTED.
 * thread_p (in)          : Thread entry.
 * oos_vfid (in)          : OOS file the pages belong to.
 * emptied_pages (in/out) : Pages the reclaims emptied; cleared on return.
 *
 * Call it after unfixing the heap page: page deallocation takes the OOS file header latch.
 */
int
vacuum_oos_reclaim_empty_pages (THREAD_ENTRY *thread_p, const VFID *oos_vfid,
				VACUUM_OOS_EMPTIED_PAGES *emptied_pages)
{
  assert (oos_vfid != NULL && !VFID_ISNULL (oos_vfid));
  assert (emptied_pages != NULL);

  int error_code = oos_reclaim_empty_pages (thread_p, *oos_vfid, *emptied_pages);
  emptied_pages->clear ();

  if (error_code == ER_INTERRUPTED)
    {
      return error_code;
    }
  if (error_code != NO_ERROR)
    {
      vacuum_er_log_warning (VACUUM_ER_LOG_HEAP,
			     "Could not reclaim empty OOS pages of file %d|%d (error %d); "
			     "leaving them for a later cycle.",
			     VFID_AS_ARGS (oos_vfid), error_code);
      er_clear ();
    }
  return NO_ERROR;
}

/*
 * vacuum_oos_find_vfid_for_heap_record () - Look up the heap's OOS file the first time it is needed, waiting for
 *   the heap header latch.
 *   return            : Error code. A failed lookup (header latch timeout or read error) is returned, never skipped:
 *                       the caller must not vacuum the record without its OOS cleanup.
 *
 * thread_p (in)     : Thread entry.
 * hfid (in)         : Heap file of the record being vacuumed.
 * record (in)       : The record's data.
 * oos_vfid (in/out) : The caller's OOS file id. Left alone if already found, filled in on success (VFID_NULL when the
 *                     heap has no OOS file), set to VFID_NULL on error.
 */
int
vacuum_oos_find_vfid_for_heap_record (THREAD_ENTRY *thread_p, const HFID *hfid, const RECDES *record, VFID *oos_vfid)
{
  if (!VFID_ISNULL (oos_vfid) || !heap_recdes_contains_oos (record))
    {
      return NO_ERROR;
    }

  bool found;
  if (FI_INSERTED (FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT))
    {
      /* A latch timeout stops debug builds in pgbuf_timed_sleep, so a busy header fails here at once instead. */
      found = heap_oos_find_vfid_nowait (thread_p, hfid, oos_vfid);
      if (!found)
	{
	  VPID header_vpid = { hfid->hpgid, hfid->vfid.volid };
	  (void) FI_TEST_ARG (thread_p, FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT, &header_vpid, 0);
	}
    }
  else
    {
      found = heap_oos_find_vfid (thread_p, hfid, oos_vfid, false);
    }
  if (found)
    {
      return NO_ERROR;
    }

  int error_code;
  ASSERT_ERROR_AND_SET (error_code);
  VFID_SET_NULL (oos_vfid);
  return error_code;
}

/*
 * vacuum_oos_report_missing_oos_file () - Report a record with the OOS flag whose heap has no OOS file.
 *   The OOS file is created before an OOS value is written, so this is a wrongly set flag, a dropped OOS file or an
 *   odd recovery order. Debug builds stop here. Release builds log it, and the caller skips the OOS cleanup of this
 *   record (a bounded leak).
 *
 * hfid (in)        : Heap file of the record being vacuumed.
 * record (in)      : The record's data.
 * slotid (in)      : The record's slot (for logging only).
 * record_type (in) : The record's type (for logging only).
 */
void
vacuum_oos_report_missing_oos_file (const HFID *hfid, const RECDES *record, PGSLOTID slotid, INT16 record_type)
{
  int repid_and_flags = OR_GET_INT (record->data + OR_REP_OFFSET);
  int record_flags = OR_GET_RECORD_FLAGS (record->data);
  int offset_size = OR_GET_OFFSET_SIZE (record->data);
  vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
		       "OOS flag set but no OOS VFID for hfid %d|%d slotid=%d rectype=%d rec_len=%d "
		       "repid_and_flags=0x%08x record_flags=0x%02x offset_size=%d - skipping OOS cleanup "
		       "(bounded leak)",
		       VFID_AS_ARGS (&hfid->vfid), (int) slotid, (int) record_type,
		       record->length, repid_and_flags, record_flags, offset_size);
  assert (false);
}

/*
 * vacuum_heap_oos_log_reclaim_intent () - Record the OOS chains of a heap record being vacuumed (REMOVE) as a
 *   reclaim intent of the caller's sysop.
 *
 *   PRECONDITION: the caller has an open sysop that also removes the heap record. Its commit runs the
 *   reclaim; its abort discards the intent along with the heap change, so no chunk delete is ever undone
 *   (ADR-0008, CBRD-27781). The only caller, vacuum_heap_record, commits that sysop through
 *   vacuum_oos_sysop_commit_collecting_emptied_pages.
 *
 * return	  : Error code.
 * thread_p (in)  : Thread entry.
 * oos_vfid (in)  : OOS file of the record's heap (must be valid).
 * record (in)	  : Heap record whose OOS references are reclaimed.
 * owner_oid (in) : The record's OID, for diagnostics.
 */
int
vacuum_heap_oos_log_reclaim_intent (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const RECDES *record,
				    const OID *owner_oid)
{
  assert (!VFID_ISNULL (oos_vfid));

  OOS_REF_VECTOR oos_refs;
  int error_code = heap_recdes_get_oos_refs (record, oos_refs);
  if (error_code != NO_ERROR)
    {
      if (error_code != ER_OUT_OF_VIRTUAL_MEMORY)
	{
	  assert_release (false);
	}
      return error_code;
    }
  assert (LOG_FIND_CURRENT_TDES (thread_p)->is_under_sysop ());

  /* Sort by head OID so the reclaim visits nearby pages back to back. */
  std::sort (oos_refs.begin (), oos_refs.end (), [] (const oos_chain_ref &a, const oos_chain_ref &b)
  {
    return oid_compare (&a.head_oid, &b.head_oid) < 0;
  });

  error_code = oos_log_reclaim_intent (thread_p, *oos_vfid, *owner_oid, 0, "vacuum remove", oos_refs);
  if (error_code != NO_ERROR)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to record the OOS reclaim of record %d|%d|%d.",
			   OID_AS_ARGS (owner_oid));
    }
  return error_code;
}

/*
 * vacuum_oos_sysop_commit_collecting_emptied_pages () - Commit vacuum's sysop and collect the OOS pages its
 *   reclaim postpones emptied, for the fast path after the heap page is unfixed.
 *
 * thread_p (in)	  : Thread entry.
 * emptied_pages (in/out) : The pages are appended, each once.
 */
void
vacuum_oos_sysop_commit_collecting_emptied_pages (THREAD_ENTRY *thread_p, VACUUM_OOS_EMPTIED_PAGES *emptied_pages)
{
  oos_emptied_page_sink sink (*emptied_pages);
  log_sysop_commit (thread_p);
}

/*
 * vacuum_oos_reclaim_notified_chains () - Reclaim the chains one RVOOS_NOTIFY_VACUUM record names, then
 *   return the pages it emptied to the file manager.
 *
 * return	       : NO_ERROR, or ER_INTERRUPTED on shutdown (the block is processed again after restart).
 *			 Any other reclaim failure stops the server (oos_reclaim_notified_chains).
 * thread_p (in)       : Thread entry.
 * undo_data (in)      : Undo data of the record.
 * undo_data_size (in) : Its size.
 *
 * Vacuum reaches the record only once the UPDATE's MVCCID precedes every active snapshot, so no reader can
 * still reach the chains through the old version.
 */
int
vacuum_oos_reclaim_notified_chains (THREAD_ENTRY *thread_p, const char *undo_data, int undo_data_size)
{
  /* The undo data may point into the worker's current log page, which the page fixes below can rotate out.
   * Work on a private copy. */
  std::vector<char> notify_copy;
  try
    {
      notify_copy.assign (undo_data, undo_data + undo_data_size);
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) undo_data_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  VFID oos_vfid = VFID_INITIALIZER;
  VACUUM_OOS_EMPTIED_PAGES emptied_pages;
  int error_code = oos_reclaim_notified_chains (thread_p, notify_copy.data (), (int) notify_copy.size (), &oos_vfid,
		   &emptied_pages);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  if (emptied_pages.empty ())
    {
      return NO_ERROR;
    }
  return vacuum_oos_reclaim_empty_pages (thread_p, &oos_vfid, &emptied_pages);
}
