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
#include "internal_lob_file.hpp"
#include "log_manager.h"
#include "memory_alloc.h"
#include "object_domain.h"
#include "object_representation.h"
#include "oid.h"
#include "oos_file.hpp"
#include "storage_common.h"
#include "vacuum.h"

#include <algorithm>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Result of vacuum_oos_vfid_lookup. Three outcomes, kept separate on purpose:
 *   FOUND : the heap's OOS file was located (returned in out_oos_vfid).
 *   NONE  : the heap has no OOS file at all. This is normal; there is nothing to clean up.
 *   ERROR : the lookup failed for a transient reason (e.g. a page could not be read).
 *
 * Telling NONE and ERROR apart matters. On ERROR we deliberately leave the database error set so
 * the caller can log it, then the caller calls er_clear() and skips this record. The OOS bytes
 * stay on disk for now (a small, logged leak). We never fail the whole vacuum block over it. */
typedef enum
{
  VACUUM_OOS_VFID_FOUND,	/* found a real OOS file; its id is in out_oos_vfid */
  VACUUM_OOS_VFID_NONE,		/* the heap simply has no OOS file (normal) */
  VACUUM_OOS_VFID_ERROR		/* lookup failed; error left set, answer not cached */
} VACUUM_OOS_VFID_LOOKUP_RESULT;

static VACUUM_OOS_VFID_LOOKUP_RESULT vacuum_oos_vfid_lookup (THREAD_ENTRY *thread_p,
    VACUUM_OOS_VFID_MEMO *memo, const VFID *heap_vfid, VFID *out_oos_vfid, VFID *out_internal_lob_vfid);
static int vacuum_forward_walk_oos_delete_atomic (THREAD_ENTRY *thread_p, const VFID *oos_vfid,
    const VFID *internal_lob_vfid, HEAP_OOS_REFERENCE_VECTOR references);

/*
 * vacuum_oos_vfid_lookup () - Find the OOS file that belongs to a given heap file.
 *   A tiny one-entry cache (the "memo") lets us skip the lookup when we ask about the same heap
 *   again right away.
 *
 * return	      : One of FOUND / NONE / ERROR (see VACUUM_OOS_VFID_LOOKUP_RESULT).
 * thread_p (in)      : Thread entry.
 * memo (in/out)      : One-entry cache remembering the last "heap file -> OOS file" answer. The
 *			caller owns it; it lives on the stack in vacuum_process_log_block, so it is
 *			private to one worker thread and one block and needs no locking.
 * heap_vfid (in)     : The heap file we are asking about (the cache key).
 * out_oos_vfid (out) : The heap's OOS file id. May come back VFID_NULL, meaning "no OOS file".
 * out_internal_lob_vfid (out) : The heap's internal LOB file id, same convention. FOUND means at least one
 *			of the two files exists.
 *
 * A heap's OOS file never changes once the heap exists, so the answer is safe to cache. A fresh
 * lookup costs two page reads: file_descriptor_get (to get the HFID), then heap_oos_find_vfid (to
 * read the heap header). A bulk UPDATE produces many records on the same heap in a row, so the cache
 * turns all but the first lookup into a quick id comparison.
 *
 * We only cache a real answer: FOUND, or a genuine "no OOS file" (NONE). A transient ERROR is never
 * cached, so the next record retries from scratch instead of reusing a bad VFID_NULL. On ERROR the
 * database error is left set on purpose (not cleared here) so the caller can log it.
 */
static VACUUM_OOS_VFID_LOOKUP_RESULT
vacuum_oos_vfid_lookup (THREAD_ENTRY *thread_p, VACUUM_OOS_VFID_MEMO *memo, const VFID *heap_vfid,
			VFID *out_oos_vfid, VFID *out_internal_lob_vfid)
{
  FILE_DESCRIPTORS file_descriptor;
  HFID hfid;

  assert (memo != NULL);

  if (memo->valid && VFID_EQ (&memo->heap_vfid, heap_vfid))
    {
      VFID_COPY (out_oos_vfid, &memo->oos_vfid);
      VFID_COPY (out_internal_lob_vfid, &memo->internal_lob_vfid);
      return (VFID_ISNULL (out_oos_vfid) && VFID_ISNULL (out_internal_lob_vfid))
	     ? VACUUM_OOS_VFID_NONE : VACUUM_OOS_VFID_FOUND;
    }

  VFID_SET_NULL (out_oos_vfid);
  VFID_SET_NULL (out_internal_lob_vfid);

  if (file_descriptor_get (thread_p, heap_vfid, &file_descriptor) != NO_ERROR)
    {
      /* Transient failure. Do not cache it, and do not clear the error here: leave it set so the
       * caller can log it (the caller clears it afterward). */
      return VACUUM_OOS_VFID_ERROR;
    }

  hfid = file_descriptor.heap.hfid;
  if (HFID_IS_NULL (&hfid))
    {
      /* Transient failure. Do not cache it; leave any error set for the caller. */
      return VACUUM_OOS_VFID_ERROR;
    }

  /* Both OOS-layout files of the heap: ordinary OOS attributes and internal LOB chains. A NULL id simply
   * means the heap has no such file yet. A false return is a real failure while reading the heap header
   * (for example pgbuf_fix or spage_get_record failed): do not cache it; leave the error set for the caller. */
  if (!heap_oos_find_vfid_by_type (thread_p, &hfid, FILE_OOS, out_oos_vfid, false, false)
      || !heap_oos_find_vfid_by_type (thread_p, &hfid, FILE_INTERNAL_LOB, out_internal_lob_vfid, false, false))
    {
      VFID_SET_NULL (out_oos_vfid);
      VFID_SET_NULL (out_internal_lob_vfid);
      return VACUUM_OOS_VFID_ERROR;
    }

  memo->valid = true;
  VFID_COPY (&memo->heap_vfid, heap_vfid);
  VFID_COPY (&memo->oos_vfid, out_oos_vfid);
  VFID_COPY (&memo->internal_lob_vfid, out_internal_lob_vfid);

  return (VFID_ISNULL (out_oos_vfid) && VFID_ISNULL (out_internal_lob_vfid))
	 ? VACUUM_OOS_VFID_NONE : VACUUM_OOS_VFID_FOUND;
}

/*
 * vacuum_forward_walk_oos_delete_atomic () - Delete the OOS records that an old row version still
 *   points to. As vacuum walks the undo log, it finds the "pre-image" (how the row looked before an
 *   UPDATE); that old image may still reference OOS records nobody can reach anymore.
 *
 *   The OID list comes in BY VALUE so this helper owns its own copy. That matters: oos_delete can
 *   rotate (swap out) the log page that the caller's original undo_data points into, so we must work
 *   from a copy that does not live in that buffer.
 *
 *   All the deletes run inside one "sysop" (system operation) - the engine's unit of
 *   all-or-nothing work for crash recovery - so the whole multi-chunk delete either fully happens
 *   or fully rolls back. (Contrast vacuum_heap_oos_delete_within_sysop: that one runs inside the caller's
 *   existing sysop and must NOT open its own.)
 *
 *   The caller only calls this for RVHF_UPDATE_NOTIFY_VACUUM records; see commit fc0e35ced for why
 *   other log record types must be excluded.
 */
static int
vacuum_forward_walk_oos_delete_atomic (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const VFID *internal_lob_vfid,
				       HEAP_OOS_REFERENCE_VECTOR references)
{
  int error_code = NO_ERROR;
  bool has_oos_file = (oos_vfid != NULL && !VFID_ISNULL (oos_vfid));
  bool has_lob_file = (internal_lob_vfid != NULL && !VFID_ISNULL (internal_lob_vfid));

  /* Sort the references by OID (volid, pageid, slotid). Deleting in this order means back-to-back deletes
   * touch nearby pages, so a page we just loaded stays in the buffer pool (better locality).
   * This matches how the heap itself is scanned. We own this vector (passed by value), so we can
   * sort it in place. */
  std::sort (references.begin (), references.end (),
	     [] (const HEAP_OOS_REFERENCE &a, const HEAP_OOS_REFERENCE &b)
  {
    return oid_compare (&a.oid, &b.oid) < 0;
  });

  /* TODO(perf): oos_delete fixes and unfixes the OOS page on every call. The OIDs above are already
   * sorted into page order, so one day we should group the OIDs that share a page and delete them
   * under a single pgbuf_fix, instead of re-fixing the same page once per OID. */
  VACUUM_OOS_TOUCHED_PAGES touched_pages;

  log_sysop_start (thread_p);
  for (const HEAP_OOS_REFERENCE &reference : references)
    {
      /* This has to be safe to run twice. If the whole block is retried, an earlier forward-walk in
       * this block may have already committed its deletes, so an OID's chunk can already be gone. In
       * that case just skip it instead of failing inside oos_delete. We still report a real failure
       * (I/O error, interrupt, etc.) as an error. */
      bool exists;
      error_code = oos_chunk_exists (thread_p, reference.oid, &exists);
      if (error_code != NO_ERROR)
	{
	  break;
	}
      if (!exists)
	{
	  continue;
	}
      /* Same walk for both kinds; only the file and the delete primitive differ. */
      if (TP_IS_LOB_TYPE (reference.type))
	{
	  INTERNAL_LOB_LOCATOR locator = { reference.oid, 0 };

	  if (!has_lob_file)
	    {
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "internal LOB reference %d|%d|%d without an internal LOB file; "
				   "skipped (bounded leak)", OID_AS_ARGS (&reference.oid));
	      continue;
	    }
	  if (internal_lob_decode_disk_length (locator, reference.disk_length) != NO_ERROR)
	    {
	      error_code = ER_FAILED;
	      break;
	    }
	  /* Walks the chain reading only node headers and deletes each node through oos_delete. LOB file pages
	   * are not tracked for immediate reclaim; the growth-gate sweep reclaims them. */
	  error_code = internal_lob_delete (thread_p, *internal_lob_vfid, locator);
	}
      else
	{
	  if (!has_oos_file)
	    {
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "OOS reference %d|%d|%d without an OOS file; skipped (bounded leak)",
				   OID_AS_ARGS (&reference.oid));
	      continue;
	    }
	  error_code = oos_delete (thread_p, *oos_vfid, reference.oid, &touched_pages);
	}
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }
  if (error_code == NO_ERROR)
    {
      log_sysop_commit (thread_p);

      /* Must run after the commit: an aborted sysop would restore the chunks, and undo cannot
       * re-insert into a deallocated page. Only oos_delete on the ordinary OOS file records touched pages. */
      int reclaim_err = has_oos_file ? vacuum_oos_reclaim_empty_pages (thread_p, oos_vfid, &touched_pages) : NO_ERROR;
      if (reclaim_err != NO_ERROR)
	{
	  assert (reclaim_err == ER_INTERRUPTED);
	  vacuum_er_log_warning (VACUUM_ER_LOG_HEAP,
				 "OOS empty-page reclaim interrupted (file %d|%d); deletes stay committed, "
				 "emptied pages stay allocated until the growth-gate sweep reclaims them.",
				 VFID_AS_ARGS (oos_vfid));
	  error_code = reclaim_err;
	}
    }
  else
    {
      log_sysop_abort (thread_p);
    }
  return error_code;
}

/*
 * vacuum_oos_reclaim_empty_pages () - Reclaim the empty pages of a committed OOS delete batch.
 *   Only ER_INTERRUPTED is propagated; other failures are absorbed (the pages stay allocated,
 *   candidates for a later cycle).
 *
 * return                 : NO_ERROR or ER_INTERRUPTED.
 * thread_p (in)          : Thread entry.
 * oos_vfid (in)          : OOS file the pages belong to.
 * touched_pages (in/out) : Pages the delete batch touched (duplicates allowed); cleared on return.
 */
int
vacuum_oos_reclaim_empty_pages (THREAD_ENTRY *thread_p, const VFID *oos_vfid,
				VACUUM_OOS_TOUCHED_PAGES *touched_pages)
{
  assert (oos_vfid != NULL && !VFID_ISNULL (oos_vfid));
  assert (touched_pages != NULL);

  int error_code = oos_reclaim_empty_pages (thread_p, *oos_vfid, *touched_pages);
  touched_pages->clear ();

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
 * vacuum_forward_walk_reclaim_oos () - Free the OOS records that an old row version (the
 *   "pre-image") still points to, found while vacuum walks the undo log forward. Two log record
 *   types use this: RVHF_UPDATE_NOTIFY_VACUUM (update_old_home) and
 *   RVHF_DELETE_NEWHOME_NOTIFY_VACUUM (remove_old_forward).
 *
 * thread_p (in)          : Thread entry.
 * undo_data (in)         : Raw undo bytes from the log: an INT16 record type, then the pre-image
 *                          record body. If it is NULL or too short to hold a pre-image, we ignore it.
 * undo_data_size (in)    : Size of undo_data in bytes.
 * heap_vfid (in)         : The heap file these bytes belong to.
 * oos_vfid_memo (in/out) : The one-entry "heap file -> OOS file" cache for this block.
 *
 * return                 : NO_ERROR or ER_INTERRUPTED.
 *
 * NOTE: ordinary cleanup failures never fail the vacuum block. We log loudly, clear the error, and
 * leave the OOS bytes on disk (a small, logged leak). A shutdown interruption is propagated after
 * closing any sysop so vacuum_process_log_block can stop through its accepted interruption path.
 */
int
vacuum_forward_walk_reclaim_oos (THREAD_ENTRY *thread_p, char *undo_data, int undo_data_size,
				 const VFID *heap_vfid, VACUUM_OOS_VFID_MEMO *oos_vfid_memo)
{
  int error_code = NO_ERROR;

  if (undo_data == NULL || undo_data_size <= (int) sizeof (INT16))
    {
      /* Too small to hold an old row image, so there is nothing to reclaim. */
      return NO_ERROR;
    }

  RECDES undo_recdes;
  undo_recdes.type = * (INT16 *) undo_data;
  undo_recdes.data = undo_data + sizeof (INT16);
  undo_recdes.length = undo_data_size - (int) sizeof (INT16);

  /* Only real row records (REC_HOME / REC_NEWHOME) can carry OOS references. Other undo images are
   * just forwarding pointers - for example heap_update_bigone and update_old_home log an old
   * REC_BIGONE / REC_RELOCATION slot, which is only an 8-byte OID. If we handed one of those to
   * heap_recdes_contains_oos, it would read the OID's pageid as if it were an MVCC header. A pageid
   * that happens to have bit 27 set would look like the "has OOS" flag, and then
   * heap_recdes_get_oos_references would chase a garbage reference list and hit assert_release. So we
   * check the record type first - the same guard the eager-delete paths use (REC_HOME / REC_NEWHOME).
   */
  if (! ((undo_recdes.type == REC_HOME || undo_recdes.type == REC_NEWHOME) && heap_recdes_contains_oos (&undo_recdes)))
    {
      return NO_ERROR;
    }

  /* Copy the undo image into our own buffer BEFORE we fix any page below. That image usually points
   * straight into the worker's current log page. The page reads inside vacuum_oos_vfid_lookup can
   * cause log activity that swaps that page out from under us (the same hazard noted in
   * vacuum_forward_walk_oos_delete_atomic). If that happened, we would be parsing whatever bytes landed
   * there - usually zeros or another page's data - and quietly find nothing. (Seen live: the flags
   * byte at this address changed from 0x69 to 0x00 across the lookup.) The copy also fixes
   * alignment: the image starts at undo_data + sizeof (INT16), and the OR_BUF readers used by
   * heap_recdes_get_oos_references would assert on that unaligned pointer in debug builds. */
  RECDES parse_recdes = undo_recdes;
  char *stable_copy = (char *) db_private_alloc (thread_p, undo_recdes.length);
  if (stable_copy == NULL)
    {
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			   "forward-walk oos cleanup: failed to allocate %d bytes for undo image snapshot; "
			   "leaving OOS unreclaimed (bounded leak) heap_vfid=%d|%d",
			   undo_recdes.length, VFID_AS_ARGS (heap_vfid));
      er_clear ();
      return NO_ERROR;
    }
  memcpy (stable_copy, undo_recdes.data, undo_recdes.length);
  parse_recdes.data = stable_copy;

  VFID oos_vfid;
  VFID internal_lob_vfid;
  VACUUM_OOS_VFID_LOOKUP_RESULT lookup_result =
	  vacuum_oos_vfid_lookup (thread_p, oos_vfid_memo, heap_vfid, &oos_vfid, &internal_lob_vfid);
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
      /* Resolve the heap's owner class: the typed reference walk needs it to tell ordinary OOS attributes
       * (deleted from oos_vfid) apart from internal LOB chains (deleted from internal_lob_vfid). */
      FILE_DESCRIPTORS heap_fdes;
      OID class_oid;
      OID_SET_NULL (&class_oid);
      if (file_descriptor_get (thread_p, heap_vfid, &heap_fdes) == NO_ERROR)
	{
	  COPY_OID (&class_oid, &heap_fdes.heap.class_oid);
	}

      HEAP_OOS_REFERENCE_VECTOR oos_references;
      int oos_err = NO_ERROR;
      if (!VFID_ISNULL (&internal_lob_vfid) && OID_ISNULL (&class_oid))
	{
	  /* Without the owner class the references cannot be typed, and this heap owns internal LOB chains
	   * that must not be deleted from the wrong file. Skip (bounded leak). */
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			       "forward-walk oos cleanup skipped: owner class unknown for heap %d|%d with internal "
			       "LOB chains (bounded leak)", VFID_AS_ARGS (heap_vfid));
	}
      else
	{
	  oos_err = heap_recdes_get_oos_references (thread_p, &class_oid, &parse_recdes, oos_references);
	  if (oos_err == NO_ERROR && !oos_references.empty ())
	    {
	      oos_err = vacuum_forward_walk_oos_delete_atomic (thread_p, &oos_vfid, &internal_lob_vfid,
			std::move (oos_references));
	    }
	}

      if (oos_err != NO_ERROR)
	{
	  if (oos_err == ER_INTERRUPTED)
	    {
	      error_code = oos_err;
	    }
	  else
	    {
	      /* vacuum_forward_walk_oos_delete_atomic already aborted its own sysop, so any partial deletes
	       * were rolled back. What leaks is only the OOS records we never got to delete. */
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
				   "forward-walk oos cleanup failed; leaving OOS unreclaimed "
				   "(bounded leak) heap_vfid=%d|%d oos_vfid=%d|%d err=%d",
				   VFID_AS_ARGS (heap_vfid), VFID_AS_ARGS (&oos_vfid), oos_err);
	      er_clear ();
	      /* DO NOT propagate; the block must complete. */
	    }
	}
    }
  else
    {
      /* VACUUM_OOS_VFID_NONE. The guard above already confirmed this undo image is REC_HOME/REC_NEWHOME
       * and heap_recdes_contains_oos, so the record's OOS flag is set while its heap has no OOS file.
       * That is an invariant violation rather than the benign "nothing to do" case, so assert in debug
       * builds - but never take the server down over it: the block must complete, and the unreclaimed
       * OOS records are a bounded, logged leak. */
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP,
			   "forward-walk oos cleanup: undo image has OOS flag but heap has no OOS file; "
			   "heap_vfid=%d|%d", VFID_AS_ARGS (heap_vfid));
      assert_release (false);
      er_clear ();
    }

  db_private_free_and_init (thread_p, stable_copy);
  return error_code;
}

/*
 * vacuum_oos_find_vfid_for_heap_record () - Look up the heap's OOS file the first time it is needed.
 *   Called when the current record has the "has OOS" flag set but the caller has not found the OOS
 *   file yet. At this point a missing OOS file should not happen; it would mean a wrongly-set flag, a
 *   dropped file, or an odd recovery ordering. Either way it must not stop vacuum, so we log it,
 *   clear the error, leave oos_vfid NULL, and skip OOS cleanup for this one record (a small leak).
 *   Debug builds assert first, so if a future bug starts setting the flag wrongly we catch it in
 *   debug runs instead of leaking silently in release.
 *
 * thread_p (in)     : Thread entry.
 * hfid (in)         : Heap file of the record being vacuumed.
 * record (in)       : The record's data.
 * slotid (in)       : The record's slot (for logging only).
 * record_type (in)  : The record's type (for logging only).
 * oos_vfid (in/out) : The caller's OOS file id. Left alone if already found, filled in on success,
 *                     or set to VFID_NULL when the lookup legitimately fails (then skip cleanup).
 */
int
vacuum_oos_find_vfid_for_heap_record (THREAD_ENTRY *thread_p, const HFID *hfid, const RECDES *record,
				      PGSLOTID slotid, INT16 record_type, VFID *oos_vfid, bool already_resolved)
{
  if (!VFID_ISNULL (oos_vfid) || !heap_recdes_contains_oos (record))
    {
      return NO_ERROR;
    }
  if (already_resolved)
    {
      /* The caller looked the OOS file up with an unconditional latch before fixing any heap page, so a
       * NULL oos_vfid here definitively means the heap has no OOS file: the record's "has OOS" flag then
       * refers to internal LOB chains, which live in the separate FILE_INTERNAL_LOB the caller resolved
       * the same way.  Do not look again from here - this runs with the home page held, and fixing the
       * heap header page under that latch inverts the order DML uses and deadlocks. */
      return NO_ERROR;
    }
  if (heap_oos_find_vfid (thread_p, hfid, oos_vfid, false, false))
    {
      return NO_ERROR;
    }
  /* A record that says "I have OOS" but whose heap has no OOS file is a real problem, not a
   * not-yet-created file: the OOS file is always created (docreate=true) when the OOS data is
   * written, which happens before the flag is committed. So this means a wrongly-set flag, a
   * dropped OOS file, or an odd recovery ordering. We still must not fail vacuum here. Returning
   * ER_FAILED would restart a release-build spin in the vacuum_heap_page loop (it clears the error
   * and retries the same page without moving forward). Instead, log it, clear the error, and skip
   * OOS cleanup for this record (a small, logged leak). See commit 1bf7dda05. */
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
  }
  /* Not every failure here is an invariant violation.  A page latch timeout or an interruption is
   * transient: the OOS file is fine, this thread simply could not read the heap header right now.
   * Only assert for the genuine case, so a transient stall never brings the server down. */
  {
    int err = er_errid ();

    if (err != ER_PAGE_LATCH_TIMEDOUT && err != ER_INTERRUPTED)
      {
	/* In debug builds, assert so the bug that set the bad flag is caught the first time vacuum
	 * sees it.  In release builds, assert_release only records a notification error, which the
	 * er_clear () below wipes out before we skip - so vacuum keeps running. */
	assert_release (false);
      }
  }
  er_clear ();
  VFID_SET_NULL (oos_vfid);
  return NO_ERROR;
}

/*
 * vacuum_heap_oos_delete_within_sysop () - Delete OOS records referenced by a heap record being vacuumed.
 *
 *   PRECONDITION: the caller must already have an open sysop; this function deliberately does NOT
 *   start one. The heap-slot vacuum record and these OOS deletes must commit in the SAME sysop, so a
 *   crash between them cannot leave the heap slot vacuumed while its OOS chunks still look referenced
 *   (or the reverse). The only caller, vacuum_heap_record, opens that sysop and commits/aborts it.
 *
 *   Contrast vacuum_forward_walk_oos_delete_atomic, which runs with no enclosing sysop and therefore
 *   opens and commits its own. Same rule ("OOS deletes happen in exactly one sysop"), different
 *   nesting level - so one must start a sysop and the other must not.
 *
 * return	  : Error code.
 * thread_p (in)  : Thread entry.
 * oos_vfid (in)  : OOS file of the record's heap; NULL / VFID_NULL when the heap has none.
 * internal_lob_vfid (in) : Internal LOB file of the record's heap; NULL / VFID_NULL when the heap has none.
 *		    At least one of the two files must exist.
 * class_oid (in) : Owner class of the record's heap, used to tell ordinary OOS attributes apart from
 *		    internal LOB attributes. NULL / OID_NULL is accepted only when the heap has no internal LOB
 *		    file (every reference is then an ordinary OOS record); otherwise cleanup is skipped.
 * record (in)	  : Heap record whose OOS references are deleted.
 * touched_pages_out (out) : Optional; pages that lost chunks are appended, for reclaim AFTER the
 *			     caller's sysop commits. On error, this call's appends are removed.
 *
 * Ordinary OOS records are deleted from oos_vfid and internal LOB chains from internal_lob_vfid. Both ids
 * were resolved by the caller before any page latch: reading the heap header here, under the home-page
 * latch, would invert the page order DML uses and could deadlock.
 */
int
vacuum_heap_oos_delete_within_sysop (THREAD_ENTRY *thread_p, const VFID *oos_vfid, const VFID *internal_lob_vfid,
				     const OID *class_oid, const RECDES *record,
				     VACUUM_OOS_TOUCHED_PAGES *touched_pages_out)
{
  bool has_oos_file = (oos_vfid != NULL && !VFID_ISNULL (oos_vfid));
  bool has_lob_file = (internal_lob_vfid != NULL && !VFID_ISNULL (internal_lob_vfid));
  size_t n_touched_on_entry = touched_pages_out != NULL ? touched_pages_out->size () : 0;

  assert (has_oos_file || has_lob_file);

  if (has_lob_file && (class_oid == NULL || OID_ISNULL (class_oid)))
    {
      /* The heap owns internal LOB chains but the owner class is unknown, so the references cannot be typed
       * and deleting them from the wrong file would corrupt it. Skip this record (bounded leak). */
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "%s",
			   "OOS cleanup skipped: owner class unknown for a heap with internal LOB chains (bounded leak).");
      return NO_ERROR;
    }

  HEAP_OOS_REFERENCE_VECTOR oos_references;
  int error_code = heap_recdes_get_oos_references (thread_p, class_oid, record, oos_references);
  if (error_code != NO_ERROR)
    {
      if (error_code == ER_INTERRUPTED)
	{
	  return error_code;
	}
      /* A cleanup-side failure (e.g. the class representation could not be read) must not fail the
       * heap-slot vacuum. Log, clear, and skip OOS cleanup for this record (bounded leak). */
      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "%s",
			   "OOS cleanup skipped: could not read OOS references (bounded leak).");
      er_clear ();
      return NO_ERROR;
    }

  /* TODO(perf): oos_delete fixes and unfixes the OOS page on every call. When a record references
   * several OOS values on the same page, one day we should sort/group them by page and delete all of
   * a page's values under a single pgbuf_fix, instead of re-fixing the same page once per OID. */
  for (const HEAP_OOS_REFERENCE &reference : oos_references)
    {
      /* Same walk for both kinds; only the file and the delete primitive differ. */
      if (TP_IS_LOB_TYPE (reference.type))
	{
	  INTERNAL_LOB_LOCATOR locator = { reference.oid, 0 };

	  if (!has_lob_file)
	    {
	      vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "internal LOB reference %d|%d|%d without an internal LOB file; "
				   "skipped (bounded leak)", OID_AS_ARGS (&reference.oid));
	      continue;
	    }
	  if (internal_lob_decode_disk_length (locator, reference.disk_length) != NO_ERROR)
	    {
	      error_code = ER_FAILED;
	    }
	  else
	    {
	      /* Walks the chain reading only node headers and deletes each node through oos_delete. LOB file
	       * pages are not tracked for immediate reclaim; the growth-gate sweep reclaims them. */
	      error_code = internal_lob_delete (thread_p, *internal_lob_vfid, locator);
	    }
	}
      else if (!has_oos_file)
	{
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "OOS reference %d|%d|%d without an OOS file; skipped (bounded leak)",
			       OID_AS_ARGS (&reference.oid));
	  continue;
	}
      else
	{
	  error_code = oos_delete (thread_p, *oos_vfid, reference.oid, touched_pages_out);
	}
      if (error_code != NO_ERROR)
	{
	  vacuum_er_log_error (VACUUM_ER_LOG_HEAP, "Failed to delete OOS record %d|%d|%d.",
			       reference.oid.volid, reference.oid.pageid, reference.oid.slotid);
	  /* The caller's abort restores these deletes — keep its batch list committed-only. (An
	   * OOM inside oos_delete_chain may already have cleared the list; never grow it.) */
	  if (touched_pages_out != NULL && touched_pages_out->size () > n_touched_on_entry)
	    {
	      touched_pages_out->resize (n_touched_on_entry);
	    }
	  return error_code;
	}
    }

  return NO_ERROR;
}
