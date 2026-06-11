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
 * heap_oos.cpp - Heap-level OOS (Out-of-row Overflow Storage) expansion
 *
 *   Replaces inlined OOS OID slots in heap records with the actual variable-attribute bytes,
 *   producing records that look as if OOS had never been used.
 */

#include "heap_oos.hpp"

#include "error_code.h"
#include "error_manager.h"
#include "heap_file.h"
#include "object_representation.h"
#include "oos_file.hpp"
#include "oos_log.hpp"
#include "oos_util.hpp"
#include "storage_common.h"

#include <cassert>
#include <cstring>
#include <new>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * State shared across heap_record_replace_oos_oids() sub-functions.
 */
struct HEAP_OOS_EXPAND_STATE
{
  const char *src;
  int src_length;
  int src_offset_size;
  int src_header_size;
  int n_var;
  int new_length;
  int src_vot_bytes;
  int dst_vot_bytes;
  int fixed_bitmap_bytes;
  std::vector<int> vot_raw;
  std::vector<std::vector<char>> oos_blobs;
};

/*
 * heap_oos_parse_vot () - Walk the source VOT and collect each entry (including flag bits).
 *   return: NO_ERROR or ER_FAILED
 *   state(in/out): fills vot_raw and n_var
 */
static int
heap_oos_parse_vot (HEAP_OOS_EXPAND_STATE *state)
{
  const char *src_vot = (const char *) OR_GET_OBJECT_VAR_TABLE (state->src);
  const int capacity = (state->src_length - state->src_header_size) / state->src_offset_size;

  state->vot_raw.reserve (capacity + 1);
  state->n_var = -1;

  for (int i = 0; i <= capacity; ++i)
    {
      if (i == capacity)
	{
	  assert_release (false && "VOT sentinel (LAST_ELEMENT) not found within record bounds");
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}
      int raw;
      const char *ep = src_vot + i * state->src_offset_size;
      switch (state->src_offset_size)
	{
	case OR_BYTE_SIZE:
	  raw = OR_GET_BYTE (ep);
	  break;
	case OR_SHORT_SIZE:
	  raw = OR_GET_SHORT (ep);
	  break;
	case OR_INT_SIZE:
	  raw = OR_GET_INT (ep);
	  break;
	default:
	  assert_release (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}
      state->vot_raw.push_back (raw);
      if (OR_IS_LAST_ELEMENT (raw))
	{
	  state->n_var = i;
	  break;
	}
    }

  if (state->n_var <= 0)
    {
      /* OR_MVCC_FLAG_HAS_OOS was set but the record has no variable attributes. Corrupt record. */
      assert_release (false && "OOS flag set without variable attributes");
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * heap_oos_read_blobs () - Read the OOS blob for every OOS-tagged variable index.
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   state(in/out): oos_blobs must be resized to n_var (empty vectors)
 */
static int
heap_oos_read_blobs (THREAD_ENTRY *thread_p, HEAP_OOS_EXPAND_STATE *state)
{
  for (int i = 0; i < state->n_var; ++i)
    {
      if (!OR_IS_OOS (state->vot_raw[i]))
	{
	  continue;
	}

      const int value_offset = state->src_header_size + OR_GET_VAR_OFFSET (state->vot_raw[i]);
      if (value_offset + OR_OOS_INLINE_SIZE > state->src_length)
	{
	  assert_release (false && "OOS inline slot extends past record bounds");
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}

      /* Inline OOS slot layout (M2+): [OID (8B) | full_length (8B bigint)]. */
      OID oos_oid = OID_INITIALIZER;
      DB_BIGINT oos_len = 0;
      int rc = NO_ERROR;
      OR_BUF buf;
      or_init (&buf, (char *) state->src + value_offset, OR_OOS_INLINE_SIZE);
      if (or_get_oid (&buf, &oos_oid) != NO_ERROR || OID_ISNULL (&oos_oid))
	{
	  assert_release (false && "failed to read OOS OID from inline slot");
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}
      oos_len = or_get_bigint (&buf, &rc);
      if (rc != NO_ERROR || oos_len <= 0 || oos_len > (DB_BIGINT) DB_MAX_STRING_LENGTH)
	{
	  assert_release (false && "invalid OOS inline length");
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}

      state->oos_blobs[i].resize ((std::size_t) oos_len);
      if (oos_read (thread_p, oos_oid, oos_buffer (state->oos_blobs[i].data (), (std::size_t) oos_len)) != NO_ERROR)
	{
	  oos_error ("oos_read failed for OID %d|%d|%d", OID_AS_ARGS (&oos_oid));
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * heap_oos_compute_layout () - Compute the output record layout and total length.
 *   return: NO_ERROR or ER_FAILED
 *   state(in/out): fills new_length, src_vot_bytes, dst_vot_bytes, fixed_bitmap_bytes
 */
static int
heap_oos_compute_layout (HEAP_OOS_EXPAND_STATE *state)
{
  const int dst_offset_size = BIG_VAR_OFFSET_SIZE;
  state->src_vot_bytes = OR_VAR_TABLE_SIZE_INTERNAL (state->n_var, state->src_offset_size);
  state->dst_vot_bytes = OR_VAR_TABLE_SIZE_INTERNAL (state->n_var, dst_offset_size);

  state->fixed_bitmap_bytes = OR_GET_VAR_OFFSET (state->vot_raw[0]) - state->src_vot_bytes;
  if (state->fixed_bitmap_bytes < 0)
    {
      assert_release (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  int64_t new_values_bytes = 0;
  for (int i = 0; i < state->n_var; ++i)
    {
      const int this_off = OR_GET_VAR_OFFSET (state->vot_raw[i]);
      const int next_off = OR_GET_VAR_OFFSET (state->vot_raw[i + 1]);
      const int src_val_len = next_off - this_off;
      if (src_val_len < 0 || state->src_header_size + next_off > state->src_length)
	{
	  assert_release (false && "VOT offsets out of order or past record");
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;
	}
      if (OR_IS_OOS (state->vot_raw[i]))
	{
	  assert_release (src_val_len == OR_OOS_INLINE_SIZE);
	  new_values_bytes += (int64_t) state->oos_blobs[i].size ();
	}
      else
	{
	  new_values_bytes += src_val_len;
	}
    }

  const int64_t new_length_64 =
	  (int64_t) state->src_header_size + state->dst_vot_bytes + state->fixed_bitmap_bytes + new_values_bytes;
  if (new_length_64 > (int64_t) INT_MAX)
    {
      assert_release (false && "OOS-expanded record size exceeds INT_MAX");
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }
  state->new_length = (int) new_length_64;

  return NO_ERROR;
}

/*
 * heap_oos_build_record () - Assemble the expanded output record from the computed layout.
 *   return: SCAN_CODE
 *   thread_p(in): thread entry
 *   context(in/out): heap get context — rec->data may be reallocated
 *   state(in): OOS expansion state with all phases completed
 */
static SCAN_CODE
heap_oos_build_record (THREAD_ENTRY *thread_p, HEAP_GET_CONTEXT *context, const HEAP_OOS_EXPAND_STATE *state)
{
  RECDES *rec = context->recdes_p;
  const int dst_offset_size = BIG_VAR_OFFSET_SIZE;

  const bool need_realloc = (context->ispeeking == PEEK) || (rec->area_size < state->new_length);
  if (need_realloc)
    {
      if (context->scan_cache == NULL)
	{
	  rec->length = - (state->new_length);
	  return S_DOESNT_FIT;
	}
      context->scan_cache->assign_recdes_to_area (*rec, (size_t) state->new_length);
      if (rec->data == NULL || rec->area_size < state->new_length)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) state->new_length);
	  return S_ERROR;
	}
      context->ispeeking = COPY;
    }

  char *dst = rec->data;

  /* Header: copy verbatim, then clear the OOS flag and reset the offset-size bits. */
  std::memcpy (dst, state->src, state->src_header_size);
  unsigned int repid_bits = (unsigned int) OR_GET_INT (dst + OR_REP_OFFSET);
  repid_bits &= ~ ((unsigned int) OR_MVCC_FLAG_HAS_OOS << OR_MVCC_FLAG_SHIFT_BITS);
  repid_bits &= ~ (unsigned int) OR_OFFSET_SIZE_FLAG;
  repid_bits |= OR_OFFSET_SIZE_4BYTE;
  OR_PUT_INT (dst + OR_REP_OFFSET, (int) repid_bits);

  /* Rewrite the VOT with new offsets. */
  const int dst_first_value_rel = state->dst_vot_bytes + state->fixed_bitmap_bytes;
  char *dst_vot = dst + state->src_header_size;
  int cumulative = 0;
  for (int i = 0; i < state->n_var; ++i)
    {
      OR_PUT_INT (dst_vot + i * dst_offset_size, dst_first_value_rel + cumulative);
      const int val_len = (OR_IS_OOS (state->vot_raw[i])
			   ? (int) state->oos_blobs[i].size ()
			   : (OR_GET_VAR_OFFSET (state->vot_raw[i + 1]) - OR_GET_VAR_OFFSET (state->vot_raw[i])));
      cumulative += val_len;
    }
  OR_PUT_INT (dst_vot + state->n_var * dst_offset_size, OR_SET_VAR_LAST_ELEMENT (dst_first_value_rel + cumulative));

  /* Zero any alignment padding past the last VOT entry. */
  const int vot_entry_bytes = (state->n_var + 1) * dst_offset_size;
  if (state->dst_vot_bytes > vot_entry_bytes)
    {
      std::memset (dst_vot + vot_entry_bytes, 0, state->dst_vot_bytes - vot_entry_bytes);
    }

  /* Fixed attributes + bound-bit bitmap: copy unchanged. */
  if (state->fixed_bitmap_bytes > 0)
    {
      std::memcpy (dst + state->src_header_size + state->dst_vot_bytes,
		   state->src + state->src_header_size + state->src_vot_bytes, state->fixed_bitmap_bytes);
    }

  /* Variable values: inline the OOS blobs in place of their inline slots. */
  int dst_pos = state->src_header_size + state->dst_vot_bytes + state->fixed_bitmap_bytes;
  for (int i = 0; i < state->n_var; ++i)
    {
      if (OR_IS_OOS (state->vot_raw[i]))
	{
	  std::memcpy (dst + dst_pos, state->oos_blobs[i].data (), state->oos_blobs[i].size ());
	  dst_pos += (int) state->oos_blobs[i].size ();
	}
      else
	{
	  const int src_off = state->src_header_size + OR_GET_VAR_OFFSET (state->vot_raw[i]);
	  const int len = OR_GET_VAR_OFFSET (state->vot_raw[i + 1]) - OR_GET_VAR_OFFSET (state->vot_raw[i]);
	  std::memcpy (dst + dst_pos, state->src + src_off, len);
	  dst_pos += len;
	}
    }
  assert (dst_pos == state->new_length);

  rec->length = state->new_length;
  return S_SUCCESS;
}

/*
 * heap_record_replace_oos_oids () - Replace inlined OOS OID slots in a heap record with the actual
 *                                   variable-attribute bytes, producing a record that looks as if
 *                                   OOS had never been used.
 *
 * Reconstruction uses only oos_read() + the on-record variable offset table (VOT). It does NOT
 * consult the class representation, so it is schema-change safe and much cheaper than the previous
 * approach that round-tripped through heap_attrinfo_*.
 *
 * Output VOT is always written with 4-byte offsets (BIG_VAR_OFFSET_SIZE) so that arbitrarily large
 * expansions fit without re-examining the offset-size bits of the original record.
 */
SCAN_CODE
heap_record_replace_oos_oids (THREAD_ENTRY *thread_p, HEAP_GET_CONTEXT *context)
{
  RECDES *rec = context->recdes_p;

  if (!context->expand_oos)
    {
      /* Caller opted out: they handle OOS themselves (e.g. heap_attrinfo_read_dbvalues). */
      return S_SUCCESS;
    }

  assert (rec != NULL && rec->data != NULL && rec->length > 0);

  if (rec == NULL || rec->data == NULL || rec->length <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return S_ERROR;
    }

  if (!heap_recdes_contains_oos (rec))
    {
      return S_SUCCESS;
    }

  try
    {
      /* Snapshot input bytes: rec->data may be invalidated during reallocation below. */
      std::vector<char> src_buf (rec->data, rec->data + rec->length);

      HEAP_OOS_EXPAND_STATE state = { };
      state.src = src_buf.data ();
      state.src_length = (int) src_buf.size ();
      state.src_offset_size = OR_GET_OFFSET_SIZE (state.src);
      state.src_header_size = OR_HEADER_SIZE ((char *) state.src);

      if (heap_oos_parse_vot (&state) != NO_ERROR)
	{
	  return S_ERROR;
	}

      state.oos_blobs.resize (state.n_var);

      if (heap_oos_read_blobs (thread_p, &state) != NO_ERROR)
	{
	  return S_ERROR;
	}

      if (heap_oos_compute_layout (&state) != NO_ERROR)
	{
	  return S_ERROR;
	}

      return heap_oos_build_record (thread_p, context, &state);
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) rec->length);
      return S_ERROR;
    }
}

/*
 * heap_update_home_delete_replaced_oos () - Eagerly delete old OOS records replaced by an UPDATE.
 *
 * Called from heap_update_home on the SA_MODE (non-MVCC) branch after the in-place overwrite has
 * succeeded. MVCC mode keeps the old OOS alive for concurrent readers and lets vacuum reclaim it
 * later; SA_MODE has no readers and no vacuum, so deletion happens here or not at all.
 *
 * Compares the OOS OID lists from the pre-update (home) recdes and the new recdes. OIDs present
 * only in the old list are deleted; OIDs that appear in both (same physical OOS referenced before
 * and after) are preserved. Caller guards on home_recdes.type == REC_HOME and OOS flag set.
 *
 * Despite historical naming ("SA_MODE"), the !is_mvcc_op gate also fires for SERVER_MODE updates
 * to MVCC-disabled classes (catalog tables), so this code can execute server-side too. Caller MUST
 * abort the transaction on error so oos_delete's per-chunk undo records replay any partial deletes
 * during rollback; otherwise the home recdes will reference already-deleted OOS chunks.
 *
 * Strict failure handling: the OOS header flag is set by the record transformer and read via
 * heap_recdes_contains_oos, so a missing OOS file or a failed OID extraction at this point
 * indicates real corruption — log and propagate.
 */
int
heap_update_home_delete_replaced_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  OID_VECTOR old_oos_oids, new_oos_oids;
  VFID oos_vfid;
  int error_code;

  error_code = heap_recdes_get_oos_oids (&context->home_recdes, old_oos_oids);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup: heap_recdes_get_oos_oids(old) failed"
		    " (hfid=%d|%d, oid=%d|%d|%d, rec_len=%d).",
		    VFID_AS_ARGS (&context->hfid.vfid),
		    context->oid.volid, context->oid.pageid, context->oid.slotid, context->home_recdes.length);
      return error_code;
    }
  if (old_oos_oids.empty ())
    {
      return NO_ERROR;
    }

  if (heap_recdes_contains_oos (context->recdes_p))
    {
      error_code = heap_recdes_get_oos_oids (context->recdes_p, new_oos_oids);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  er_log_debug (ARG_FILE_LINE,
			"SA_MODE eager OOS cleanup: heap_recdes_get_oos_oids(new) failed"
			" (hfid=%d|%d, oid=%d|%d|%d, new_rec_len=%d).",
			VFID_AS_ARGS (&context->hfid.vfid),
			context->oid.volid, context->oid.pageid, context->oid.slotid, context->recdes_p->length);
	  return error_code;
	}
    }

  if (!heap_oos_find_vfid (thread_p, &context->hfid, &oos_vfid, false))
    {
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup: OOS flag set but no OOS VFID found for hfid %d|%d"
		    " (oid=%d|%d|%d).",
		    VFID_AS_ARGS (&context->hfid.vfid), context->oid.volid, context->oid.pageid, context->oid.slotid);
      assert_release (false);
      return ER_FAILED;
    }

  for (const OID &old_oid : old_oos_oids)
    {
      if (oos_oid_in_vector (new_oos_oids, &old_oid))
	{
	  /* Same physical OOS referenced by both old and new recdes; keep it. */
	  continue;
	}
      error_code = oos_delete (thread_p, oos_vfid, old_oid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  er_log_debug (ARG_FILE_LINE,
			"SA_MODE eager OOS cleanup: oos_delete(oos_vfid=%d|%d, oid=%d|%d|%d) failed"
			" (hfid=%d|%d, heap_oid=%d|%d|%d).",
			VFID_AS_ARGS (&oos_vfid), old_oid.volid, old_oid.pageid, old_oid.slotid,
			VFID_AS_ARGS (&context->hfid.vfid),
			context->oid.volid, context->oid.pageid, context->oid.slotid);
	  return error_code;
	}
    }

  return NO_ERROR;
}

/*
 * heap_update_relocation_delete_replaced_oos () - Eager OOS cleanup for relocation update.
 *
 * Called from heap_update_relocation on the non-MVCC branch (is_mvcc_op == false) after the old
 * forward record (REC_NEWHOME) has been read and before any physical operation. Mirrors
 * heap_update_home_delete_replaced_oos but operates on the forward record because for
 * REC_RELOCATION the actual data (and OOS attributes) live on the forward page; the home slot
 * holds only an 8-byte forwarding OID.
 *
 * Despite historical naming (the sibling is labeled "SA_MODE"), the !is_mvcc_op gate also fires
 * for SERVER_MODE updates to MVCC-disabled classes (catalog tables), so this code can execute
 * server-side too. Caller MUST abort the transaction on error so oos_delete's per-chunk undo
 * records replay any partial deletes during rollback; otherwise the heap recdes will reference
 * already-deleted OOS chunks.
 *
 * Covers all 4 sub-paths of heap_update_relocation (remove_old_forward x 3,
 * update_old_forward x 1). Each sub-path either overwrites or removes the old forward record;
 * any OOS referenced only by the old forward (and not by the new record) becomes unreachable
 * and must be freed here.
 *
 * MVCC mode (concurrent-reader path) keeps the old OOS alive for snapshot reads. Vacuum reclaims
 * the update_old_forward sub-path through RVHF_UPDATE_NOTIFY_VACUUM forward-walk; the
 * remove_old_forward MVCC sub-paths still leak OOS until the forward-walk gate is extended to
 * admit physical-delete log records -- separate follow-up.
 *
 * Strict failure handling: a missing OOS file or a failed OID extraction at this point indicates
 * real corruption -- log and propagate.
 */
int
heap_update_relocation_delete_replaced_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
    const RECDES *old_forward_recdes)
{
  OID_VECTOR old_oos_oids, new_oos_oids;
  VFID oos_vfid;
  int error_code;

  error_code = heap_recdes_get_oos_oids (old_forward_recdes, old_oos_oids);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (relocation): heap_recdes_get_oos_oids(old forward) failed"
		    " (hfid=%d|%d, oid=%d|%d|%d, fwd_rec_len=%d).",
		    VFID_AS_ARGS (&context->hfid.vfid),
		    context->oid.volid, context->oid.pageid, context->oid.slotid, old_forward_recdes->length);
      return error_code;
    }
  if (old_oos_oids.empty ())
    {
      return NO_ERROR;
    }

  /* heap_recdes_get_oos_oids internally checks heap_recdes_contains_oos and returns NO_ERROR
   * with an empty vector if the new record has no OOS — no outer guard needed. */
  error_code = heap_recdes_get_oos_oids (context->recdes_p, new_oos_oids);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (relocation): heap_recdes_get_oos_oids(new) failed"
		    " (hfid=%d|%d, oid=%d|%d|%d, new_rec_len=%d).",
		    VFID_AS_ARGS (&context->hfid.vfid),
		    context->oid.volid, context->oid.pageid, context->oid.slotid, context->recdes_p->length);
      return error_code;
    }

  if (!heap_oos_find_vfid (thread_p, &context->hfid, &oos_vfid, false))
    {
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (relocation): OOS flag set but no OOS VFID found for hfid %d|%d"
		    " (oid=%d|%d|%d).",
		    VFID_AS_ARGS (&context->hfid.vfid), context->oid.volid, context->oid.pageid, context->oid.slotid);
      assert_release (false);
      return ER_FAILED;
    }

  for (const OID &old_oid : old_oos_oids)
    {
      if (oos_oid_in_vector (new_oos_oids, &old_oid))
	{
	  /* Same physical OOS referenced by both old and new recdes; keep it. */
	  continue;
	}
      error_code = oos_delete (thread_p, oos_vfid, old_oid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  er_log_debug (ARG_FILE_LINE,
			"SA_MODE eager OOS cleanup (relocation): oos_delete(oos_vfid=%d|%d, oid=%d|%d|%d) failed"
			" (hfid=%d|%d, heap_oid=%d|%d|%d).",
			VFID_AS_ARGS (&oos_vfid), old_oid.volid, old_oid.pageid, old_oid.slotid,
			VFID_AS_ARGS (&context->hfid.vfid),
			context->oid.volid, context->oid.pageid, context->oid.slotid);
	  return error_code;
	}
    }

  return NO_ERROR;
}

/*
 * heap_delete_home_delete_oos () - Eagerly delete the OOS records of a DELETEd REC_HOME row.
 *
 * Called from heap_delete_home on the non-MVCC branch (is_mvcc_op == false) before the destructive
 * heap_log_delete_physical / heap_delete_physical. MVCC mode keeps the old OOS alive for concurrent
 * readers and lets vacuum reclaim it later; SA_MODE has no readers and no vacuum, so deletion happens
 * here or not at all.
 *
 * Unlike the UPDATE siblings there is no new image on DELETE, and OOS OIDs are freshly allocated per
 * heap record (never shared across rows), so EVERY OOS OID referenced by the home record is deleted
 * unconditionally — no overlap / still-referenced check is needed.
 *
 * Despite the historical "SA_MODE" naming on the siblings, the !is_mvcc_op gate also fires for
 * SERVER_MODE deletes of MVCC-disabled classes (catalog tables), so this code can execute server-side
 * too. Caller MUST abort the transaction on error so oos_delete's per-chunk undo records replay any
 * partial deletes during rollback; otherwise the home recdes will reference already-deleted OOS chunks.
 *
 * Strict failure handling: a missing OOS file or a failed OID extraction at this point indicates real
 * corruption — log and propagate.
 */
int
heap_delete_home_delete_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context)
{
  OID_VECTOR old_oos_oids;
  VFID oos_vfid;
  int error_code;

  error_code = heap_recdes_get_oos_oids (&context->home_recdes, old_oos_oids);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (delete): heap_recdes_get_oos_oids(home) failed"
		    " (hfid=%d|%d, oid=%d|%d|%d, rec_len=%d).",
		    VFID_AS_ARGS (&context->hfid.vfid),
		    context->oid.volid, context->oid.pageid, context->oid.slotid, context->home_recdes.length);
      return error_code;
    }
  if (old_oos_oids.empty ())
    {
      return NO_ERROR;
    }

  if (!heap_oos_find_vfid (thread_p, &context->hfid, &oos_vfid, false))
    {
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (delete): OOS flag set but no OOS VFID found for hfid %d|%d"
		    " (oid=%d|%d|%d).",
		    VFID_AS_ARGS (&context->hfid.vfid), context->oid.volid, context->oid.pageid, context->oid.slotid);
      assert_release (false);
      return ER_FAILED;
    }

  for (const OID &old_oid : old_oos_oids)
    {
      error_code = oos_delete (thread_p, oos_vfid, old_oid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  er_log_debug (ARG_FILE_LINE,
			"SA_MODE eager OOS cleanup (delete): oos_delete(oos_vfid=%d|%d, oid=%d|%d|%d) failed"
			" (hfid=%d|%d, heap_oid=%d|%d|%d).",
			VFID_AS_ARGS (&oos_vfid), old_oid.volid, old_oid.pageid, old_oid.slotid,
			VFID_AS_ARGS (&context->hfid.vfid),
			context->oid.volid, context->oid.pageid, context->oid.slotid);
	  return error_code;
	}
    }

  return NO_ERROR;
}

/*
 * heap_delete_relocation_delete_oos () - Eager OOS cleanup for a DELETEd relocated (REC_RELOCATION) row.
 *
 * Called from heap_delete_relocation on the non-MVCC branch (is_mvcc_op == false) before the
 * destructive forward heap_log_delete_physical / heap_delete_physical. Mirrors
 * heap_delete_home_delete_oos but extracts the OOS OIDs from the forward REC_NEWHOME record, because
 * for REC_RELOCATION the actual data (and OOS attributes) live on the forward page; the home slot
 * holds only an 8-byte forwarding OID.
 *
 * As with the REC_HOME case there is no new image on DELETE and OOS OIDs are never shared across rows,
 * so EVERY OOS OID referenced by the forward record is deleted unconditionally.
 *
 * Despite the historical "SA_MODE" naming, the !is_mvcc_op gate also fires for SERVER_MODE deletes of
 * MVCC-disabled classes (catalog tables), so this code can execute server-side too. Caller MUST abort
 * the transaction on error so oos_delete's per-chunk undo records replay any partial deletes during
 * rollback; otherwise the forward recdes will reference already-deleted OOS chunks.
 *
 * Strict failure handling: a missing OOS file or a failed OID extraction at this point indicates real
 * corruption — log and propagate.
 */
int
heap_delete_relocation_delete_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
				   const RECDES *forward_recdes)
{
  OID_VECTOR old_oos_oids;
  VFID oos_vfid;
  int error_code;

  error_code = heap_recdes_get_oos_oids (forward_recdes, old_oos_oids);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (delete relocation): heap_recdes_get_oos_oids(forward) failed"
		    " (hfid=%d|%d, oid=%d|%d|%d, fwd_rec_len=%d).",
		    VFID_AS_ARGS (&context->hfid.vfid),
		    context->oid.volid, context->oid.pageid, context->oid.slotid, forward_recdes->length);
      return error_code;
    }
  if (old_oos_oids.empty ())
    {
      return NO_ERROR;
    }

  if (!heap_oos_find_vfid (thread_p, &context->hfid, &oos_vfid, false))
    {
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (delete relocation): OOS flag set but no OOS VFID found for hfid %d|%d"
		    " (oid=%d|%d|%d).",
		    VFID_AS_ARGS (&context->hfid.vfid), context->oid.volid, context->oid.pageid, context->oid.slotid);
      assert_release (false);
      return ER_FAILED;
    }

  for (const OID &old_oid : old_oos_oids)
    {
      error_code = oos_delete (thread_p, oos_vfid, old_oid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  er_log_debug (ARG_FILE_LINE,
			"SA_MODE eager OOS cleanup (delete relocation): oos_delete(oos_vfid=%d|%d, oid=%d|%d|%d) failed"
			" (hfid=%d|%d, heap_oid=%d|%d|%d).",
			VFID_AS_ARGS (&oos_vfid), old_oid.volid, old_oid.pageid, old_oid.slotid,
			VFID_AS_ARGS (&context->hfid.vfid),
			context->oid.volid, context->oid.pageid, context->oid.slotid);
	  return error_code;
	}
    }

  return NO_ERROR;
}
