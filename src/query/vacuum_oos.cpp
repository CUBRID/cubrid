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
#include "file_manager.h"
#include "heap_file.h"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "oid.h"
#include "oos_file.hpp"
#include "page_buffer.h"
#include "recovery.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "vacuum.h"

#include <algorithm>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Tri-state result of vacuum_oos_vfid_cache_lookup so the caller can distinguish a legitimate
 * "this heap has no OOS file" (NONE) from a transient lookup failure (ERROR). On ERROR the error
 * is left set for the caller to log; the caller er_clear()s and leaves the OOS unreclaimed (a
 * bounded, logged leak) rather than failing the vacuum block. */
typedef enum
{
  VACUUM_OOS_VFID_FOUND,	/* resolved a non-null OOS VFID into out_oos_vfid */
  VACUUM_OOS_VFID_NONE,		/* heap legitimately has no OOS file (negative sentinel cached) */
  VACUUM_OOS_VFID_ERROR		/* transient lookup failure; error left set, not cached */
} VACUUM_OOS_VFID_LOOKUP_RESULT;

static VACUUM_OOS_VFID_LOOKUP_RESULT vacuum_oos_vfid_cache_lookup (THREAD_ENTRY *thread_p,
    VACUUM_OOS_VFID_CACHE *cache, const VFID *heap_vfid, VFID *out_oos_vfid);
static int vacuum_oos_chunk_exists (THREAD_ENTRY *thread_p, const OID &oid, bool *out_exists);
static int vacuum_forward_walk_delete_old_oos (THREAD_ENTRY *thread_p, const VFID *oos_vfid,
    const OID_VECTOR &oos_oids);

/*
 * vacuum_oos_vfid_cache_lookup () - Look up (and populate on miss) the OOS VFID for a given heap VFID.
 *
 * return	      : Tri-state lookup result (see VACUUM_OOS_VFID_LOOKUP_RESULT).
 * thread_p (in)      : Thread entry.
 * cache (in/out)     : Per-block linear-scan cache. Caller owns the lifetime; it is stack-allocated
 *			in vacuum_process_log_block, making it per-block and thread-local.
 * heap_vfid (in)     : Heap file VFID (key).
 * out_oos_vfid (out) : Resolved OOS VFID (may be VFID_NULL sentinel meaning "no OOS file").
 *
 * Caches a VFID_NULL sentinel for heaps with no OOS file, so non-OOS heaps skip file_descriptor_get
 * on repeat lookups. Only the genuine "no OOS file" case (false return, no error set) is cached;
 * transient failures are not — see the in-body comments for why caching one would poison the block.
 *
 * Returns a tri-state: FOUND (out_oos_vfid set to a non-null OOS VFID), NONE (heap legitimately has
 * no OOS file; negative sentinel cached), or ERROR (transient lookup failure; the error is left set
 * for the caller to log and clear — it is intentionally NOT er_clear()ed here, and NOT cached).
 */
static VACUUM_OOS_VFID_LOOKUP_RESULT
vacuum_oos_vfid_cache_lookup (THREAD_ENTRY *thread_p, VACUUM_OOS_VFID_CACHE *cache, const VFID *heap_vfid,
			      VFID *out_oos_vfid)
{
  FILE_DESCRIPTORS file_descriptor;
  HFID hfid;
  int i;

  assert (cache != NULL);

  for (i = 0; i < cache->size; i++)
    {
      if (VFID_EQ (&cache->entries[i].heap_vfid, heap_vfid))
	{
	  VFID_COPY (out_oos_vfid, &cache->entries[i].oos_vfid);
	  return VFID_ISNULL (out_oos_vfid) ? VACUUM_OOS_VFID_NONE : VACUUM_OOS_VFID_FOUND;
	}
    }

  /* On any failure below — file_descriptor_get, HFID extraction, or transient heap_oos_find_vfid —
   * do NOT cache: a falsely-cached VFID_NULL would skip OOS reclamation for every subsequent record
   * on this heap in the block. Only cache when the heap legitimately has no OOS file (false return
   * with no error set). */
  VFID_SET_NULL (out_oos_vfid);

  if (file_descriptor_get (thread_p, heap_vfid, &file_descriptor) != NO_ERROR)
    {
      /* Transient failure — do NOT cache and do NOT er_clear: leave the error set for the caller
       * to log, then the caller clears it (bounded, logged leak). */
      return VACUUM_OOS_VFID_ERROR;
    }

  hfid = file_descriptor.heap.hfid;
  if (HFID_IS_NULL (&hfid))
    {
      /* Transient failure — do NOT cache; leave the error (if any) set for the caller. */
      return VACUUM_OOS_VFID_ERROR;
    }

  if (!heap_oos_find_vfid (thread_p, &hfid, out_oos_vfid, false))
    {
      VFID_SET_NULL (out_oos_vfid);
      if (er_errid () != NO_ERROR)
	{
	  /* Transient failure (pgbuf_fix returned NULL with ER_PB_BAD_PAGEID, spage_get_record
	   * failure, etc.) — do NOT cache. Leave the error set for the caller to log/clear. */
	  return VACUUM_OOS_VFID_ERROR;
	}
      /* Legitimate "no OOS file": heap_hdr->oos_vfid was VFID_NULL with docreate=false and no
       * error was raised. Fall through to cache the VFID_NULL negative sentinel. */
    }

  /* Round-robin eviction once full (cycle all slots, not just slot 0). `cache->size` stays capped at
   * VACUUM_OOS_VFID_CACHE_SIZE so the lookup loop above stays in-bounds. */
  int slot;
  if (cache->size < VACUUM_OOS_VFID_CACHE_SIZE)
    {
      slot = cache->size++;
    }
  else
    {
      slot = cache->evict_idx++ % VACUUM_OOS_VFID_CACHE_SIZE;
    }
  VFID_COPY (&cache->entries[slot].heap_vfid, heap_vfid);
  VFID_COPY (&cache->entries[slot].oos_vfid, out_oos_vfid);

  return VFID_ISNULL (out_oos_vfid) ? VACUUM_OOS_VFID_NONE : VACUUM_OOS_VFID_FOUND;
}

/*
 * vacuum_oos_chunk_exists () - Idempotency probe for block retry. Sets *out_exists to true iff
 *   the OOS chunk's slot is still present. vacuum_forward_walk_delete_old_oos calls this before
 *   each oos_delete so an OID whose chunk was already removed by a sibling forward-walk's
 *   committed sysop on a prior attempt at this block can be skipped, rather than tripping the
 *   S_DOESNT_EXIST hard error inside oos_delete_chain.
 *
 *   "Already gone" is narrowly defined:
 *     - pgbuf_fix_if_not_deallocated returns NO_ERROR with page_ptr==NULL (page deallocated), OR
 *     - spage_get_record returns S_DOESNT_EXIST (slot removed but page still alive).
 *
 *   Any other failure (real pgbuf_fix error from I/O / interrupt / buffer corruption, or
 *   spage_get_record returning S_ERROR) is propagated as the probe's return value. The caller
 *   must treat that as a forward-walk failure rather than a successful skip — otherwise a
 *   transient access error would be misread as "already deleted" and the OOS chain would
 *   leak with no further reclamation path (UPDATE pre-image OIDs are not reachable from the
 *   live post-image, so heap REMOVE will not retry them).
 */
static int
vacuum_oos_chunk_exists (THREAD_ENTRY *thread_p, const OID &oid, bool *out_exists)
{
  *out_exists = false;

  VPID vpid;
  vpid.volid = oid.volid;
  vpid.pageid = oid.pageid;

  PAGE_PTR page_ptr = NULL;
  int error_code = pgbuf_fix_if_not_deallocated (thread_p, &vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH,
		   &page_ptr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (page_ptr == NULL)
    {
      /* Page legitimately deallocated; chunk is gone. */
      return NO_ERROR;
    }

  RECDES probe = RECDES_INITIALIZER;
  SCAN_CODE code = spage_get_record (thread_p, page_ptr, oid.slotid, &probe, PEEK);
  pgbuf_unfix_and_init (thread_p, page_ptr);

  if (code == S_SUCCESS)
    {
      *out_exists = true;
      return NO_ERROR;
    }
  if (code == S_DOESNT_EXIST)
    {
      /* Slot already removed by a prior committed sysop; chunk is gone. */
      return NO_ERROR;
    }
  ASSERT_ERROR ();
  int errid = er_errid ();
  return errid != NO_ERROR ? errid : ER_FAILED;
}

/*
 * vacuum_forward_walk_delete_old_oos () - Delete OOS records referenced by an UPDATE pre-image
 *   discovered during forward-walk log replay. The caller must extract the OIDs into a self-owned
 *   vector before invoking this helper, since oos_delete may rotate the log page that the original
 *   undo_data points into. The sysop opened here makes the multi-chunk oos_delete sequence atomic
 *   under recovery. Caller restricts invocation to RVHF_UPDATE_NOTIFY_VACUUM (see commit
 *   fc0e35ced for why other rcvindexes must be excluded).
 */
static int
vacuum_forward_walk_delete_old_oos (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const OID_VECTOR &oos_oids)
{
  int error_code = NO_ERROR;

  /* Sort OIDs by (volid, pageid, slotid) so successive oos_delete calls hit the OOS file's
   * pages in page-locality order; mirrors the heap's VFID+OID-sorted access pattern that
   * keeps the buffer pool warm. Caller passes a const reference, so copy locally. */
  OID_VECTOR sorted_oos_oids (oos_oids);
  std::sort (sorted_oos_oids.begin (), sorted_oos_oids.end (),
	     [] (const OID &a, const OID &b)
  {
    return oid_compare (&a, &b) < 0;
  });

  log_sysop_start (thread_p);
  for (const OID &oid : sorted_oos_oids)
    {
      /* Idempotency for block retry: a sibling forward-walk earlier in this block may have
       * committed its sysop before a later one failed. On retry, the earlier OID's chunk is
       * already physically gone — skip rather than fail at oos_delete_chain's S_DOESNT_EXIST.
       * Real probe failures (I/O, interrupt, etc.) propagate as forward-walk errors. */
      bool exists;
      error_code = vacuum_oos_chunk_exists (thread_p, oid, &exists);
      if (error_code != NO_ERROR)
	{
	  break;
	}
      if (!exists)
	{
	  continue;
	}
      error_code = oos_delete (thread_p, *oos_vfid, oid);
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }
  if (error_code == NO_ERROR)
    {
      log_sysop_commit (thread_p);
    }
  else
    {
      log_sysop_abort (thread_p);
    }
  return error_code;
}

/*
 * vacuum_forward_walk_reclaim_oos () - Reclaim the OOS records referenced by an OOS-bearing heap
 *   pre-image found in a vacuum forward-walk undo record. Shared by the RVHF_UPDATE_NOTIFY_VACUUM
 *   (update_old_home) and RVHF_DELETE_NEWHOME_NOTIFY_VACUUM (remove_old_forward) paths.
 *
 * thread_p (in)           : Thread entry.
 * undo_recdes (in)        : Undo (pre-image) heap recdes reconstructed from the log undo data.
 * heap_vfid (in)          : VFID of the heap file the record lives in (the log_vacuum vfid).
 * oos_vfid_cache (in/out) : Per-block heap-VFID -> OOS-VFID cache.
 *
 * NOTE: Any OOS reclaim failure degrades to a bounded, logged leak (log loudly, er_clear, return).
 * It never propagates an error or fails the block — that would trip the shutdown-only assert in
 * vacuum_finished_block_vacuum and risk wedging vacuum.
 */
void
vacuum_forward_walk_reclaim_oos (THREAD_ENTRY *thread_p, const RECDES *undo_recdes, const VFID *heap_vfid,
				 VACUUM_OOS_VFID_CACHE *oos_vfid_cache)
{
  /* Only object-instance records (REC_HOME / REC_NEWHOME) carry an OOS-bearing VOT. Other undo
   * images are forwarding pointers: heap_update_bigone and heap_update_relocation's update_old_home
   * log the old REC_BIGONE / REC_RELOCATION home slot, which is just an 8-byte OID. Feeding such a
   * record to heap_recdes_contains_oos reinterprets the OID's pageid as an MVCC header — a pageid
   * with bit 27 set spuriously trips OR_MVCC_FLAG_HAS_OOS, after which heap_recdes_get_oos_oids walks
   * a bogus VOT and hits assert_release. Mirror the record-type guard the eager-delete paths apply
   * (forward_recdes.type == REC_NEWHOME / home_recdes.type == REC_HOME). */
  if (! ((undo_recdes->type == REC_HOME || undo_recdes->type == REC_NEWHOME) && heap_recdes_contains_oos (undo_recdes)))
    {
      return;
    }

  /* Snapshot the undo image into a private buffer BEFORE any page fix below. The image usually
   * points straight into the worker's current log page buffer, and the page fixes inside
   * vacuum_oos_vfid_cache_lookup can trigger log activity that rotates that buffer — the same
   * hazard vacuum_forward_walk_delete_old_oos documents for oos_delete. Parsing the rotated
   * buffer reads zeroed/foreign bytes and silently extracts nothing (verified live: the flags
   * word at the same address flipped from 0x69 to 0x00 across the lookup). The copy also fixes
   * alignment: the image starts at undo_data + sizeof (INT16), so the OR_BUF readers in
   * heap_recdes_get_oos_oids (or_get_oid) would assert on the raw pointer in debug builds. */
  RECDES parse_recdes = *undo_recdes;
  char *stable_copy = (char *) db_private_alloc (thread_p, undo_recdes->length);
  if (stable_copy == NULL)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			   "forward-walk oos cleanup: failed to allocate %d bytes for undo image snapshot; "
			   "leaving OOS unreclaimed (bounded leak) heap_vfid=%d|%d",
			   undo_recdes->length, VFID_AS_ARGS (heap_vfid));
      er_clear ();
      return;
    }
  memcpy (stable_copy, undo_recdes->data, undo_recdes->length);
  parse_recdes.data = stable_copy;

  VFID oos_vfid;
  VACUUM_OOS_VFID_LOOKUP_RESULT lookup_result =
	  vacuum_oos_vfid_cache_lookup (thread_p, oos_vfid_cache, heap_vfid, &oos_vfid);
  if (lookup_result == VACUUM_OOS_VFID_ERROR)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			   "transient OOS vfid lookup failure; leaving OOS unreclaimed "
			   "(bounded leak) heap_vfid=%d|%d", VFID_AS_ARGS (heap_vfid));
      er_clear ();
      /* DO NOT propagate; the block must complete. */
    }
  else if (lookup_result == VACUUM_OOS_VFID_FOUND)
    {
      OID_VECTOR oos_oids;
      int oos_err = heap_recdes_get_oos_oids (&parse_recdes, oos_oids);

      if (oos_err == NO_ERROR)
	{
	  oos_err = vacuum_forward_walk_delete_old_oos (thread_p, &oos_vfid, oos_oids);
	}

      if (oos_err != NO_ERROR)
	{
	  /* vacuum_forward_walk_delete_old_oos already aborted its own sysop, so partial deletes rolled
	   * back; the leak is just the un-deleted OOS records. */
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			       "forward-walk oos cleanup failed; leaving OOS unreclaimed "
			       "(bounded leak) heap_vfid=%d|%d oos_vfid=%d|%d err=%d",
			       VFID_AS_ARGS (heap_vfid), VFID_AS_ARGS (&oos_vfid), oos_err);
	  er_clear ();
	  /* DO NOT propagate; the block must complete. */
	}
    }
  /* VACUUM_OOS_VFID_NONE: heap legitimately has no OOS file — nothing to do. */

  db_private_free_and_init (thread_p, stable_copy);
}

/*
 * vacuum_oos_find_vfid_for_heap_record () - Lazy lookup of the heap's OOS VFID when the current
 *   record carries the OOS flag but the caller has not cached oos_vfid yet. A missing OOS file
 *   at this point is unexpected (false-positive flag / dropped file / recovery-ordering edge);
 *   it must not abort vacuum, so log it, clear the error, leave oos_vfid NULL, and skip OOS
 *   cleanup for this record (bounded leak). Debug builds assert instead, so a
 *   future flag-planting bug is caught in debug runs rather than leaking silently.
 *
 * thread_p (in)     : Thread entry.
 * hfid (in)         : Heap file identifier of the record being vacuumed.
 * record (in)       : Current record data.
 * slotid (in)       : Slot of the record (diagnostics only).
 * record_type (in)  : Record type (diagnostics only).
 * oos_vfid (in/out) : Caller's cached OOS VFID; left untouched if already resolved, populated on
 *                     success, VFID_NULL when the lookup legitimately fails (skip OOS cleanup).
 */
int
vacuum_oos_find_vfid_for_heap_record (THREAD_ENTRY *thread_p, const HFID *hfid, const RECDES *record,
				      PGSLOTID slotid, INT16 record_type, VFID *oos_vfid)
{
  if (!heap_recdes_contains_oos (record) || !VFID_ISNULL (oos_vfid))
    {
      return NO_ERROR;
    }
  if (heap_oos_find_vfid (thread_p, hfid, oos_vfid, false))
    {
      return NO_ERROR;
    }
  /* A record carrying OR_MVCC_FLAG_HAS_OOS whose heap has no resolvable OOS file is NOT a
   * lazy-creation artifact (the file is created with docreate=true when the OOS chunk is
   * written, before the flag is committed). It indicates a false-positive HAS_OOS flag, a
   * dropped OOS file, or a recovery-ordering edge. None of these may fail/abort vacuum:
   * returning ER_FAILED here re-arms the release-only spin in the vacuum_heap_page loop
   * (it er_clear()+continues without advancing page_ptr). Log, clear, and skip OOS cleanup
   * for this record - bounded, logged leak. See commit 1bf7dda05. */
  {
    int repid_and_flags = OR_GET_INT (record->data + OR_REP_OFFSET);
    int mvcc_flags = OR_GET_MVCC_FLAG (record->data);
    int offset_size = OR_GET_OFFSET_SIZE (record->data);
    vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			 "OOS flag set but no OOS VFID for hfid %d|%d slotid=%d rectype=%d rec_len=%d "
			 "repid_and_flags=0x%08x mvcc_flags=0x%02x offset_size=%d - skipping OOS cleanup "
			 "(bounded leak)",
			 VFID_AS_ARGS (&hfid->vfid), (int) slotid, (int) record_type,
			 record->length, repid_and_flags, mvcc_flags, offset_size);
  }
  /* Debug: abort so the flag-planting bug is caught at its first vacuum sighting.
   * Release: assert_release only er_set()s a notification, which the er_clear() below
   * wipes before the skip — vacuum keeps going. */
  assert_release (false);
  er_clear ();
  VFID_SET_NULL (oos_vfid);
  return NO_ERROR;
}

/*
 * vacuum_heap_oos_delete () - Delete OOS records referenced by a heap record being vacuumed.
 *
 * return	  : Error code.
 * thread_p (in)  : Thread entry.
 * oos_vfid (in)  : OOS file of the record's heap (must be valid).
 * record (in)	  : Heap record whose OOS references are deleted.
 */
int
vacuum_heap_oos_delete (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const RECDES *record)
{
  assert (!VFID_ISNULL (oos_vfid));
  OID_VECTOR oos_oids;
  int error_code = heap_recdes_get_oos_oids (record, oos_oids);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      return error_code;
    }

  for (const OID &oos_oid : oos_oids)
    {
      error_code = oos_delete (thread_p, *oos_vfid, oos_oid);
      if (error_code != NO_ERROR)
	{
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			       "Failed to delete OOS record %d|%d|%d.", oos_oid.volid, oos_oid.pageid, oos_oid.slotid);
	  return error_code;
	}
    }

  return NO_ERROR;
}

/*
 * bridge_log_append_undo_for_prev_version_test () - Test helper: append an undo log record carrying
 *   a heap recdes and return its LSA (usable as a prev_version_lsa to simulate an old OOS version).
 *
 * Uses RVES_NOTIFY_VACUUM: it is an MVCC op (so the record is LOG_MVCC_UNDO_DATA), its undo handler
 * vacuum_rv_es_nop is a no-op (so test rollback won't reverse-apply the hand-crafted recdes), and it
 * accepts a NULL vfid at append time.
 *
 * thread_p(in)   : Thread entry.
 * old_recdes(in) : Old heap recdes to log (treated as undo data).
 * out_lsa(out)   : LSA of the appended record.
 */
void
bridge_log_append_undo_for_prev_version_test (THREAD_ENTRY *thread_p, const VFID *, const RECDES *old_recdes,
    LOG_LSA *out_lsa)
{
  LOG_DATA_ADDR addr;
  LOG_TDES *tdes;

  addr.vfid = NULL;
  addr.pgptr = NULL;
  addr.offset = -1;

  log_append_undo_recdes (thread_p, RVES_NOTIFY_VACUUM, &addr, old_recdes);

  /* Flush the prior list so the just-appended record becomes fetchable by log_get_undo_record
   * (which asserts process_lsa < append_lsa). */
  LOG_CS_ENTER (thread_p);
  logpb_prior_lsa_append_all_list (thread_p);
  LOG_CS_EXIT (thread_p);

  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  assert (tdes != NULL);
  LSA_COPY (out_lsa, &tdes->tail_lsa);
}
