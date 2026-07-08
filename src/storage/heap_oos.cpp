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

#include "deduplicate_key.h"
#include "error_code.h"
#include "error_manager.h"
#include "heap_file.h"
#include "log_impl.h"
#include "object_representation.h"
#include "oos_file.hpp"
#include "oos_log.hpp"
#include "oos_util.hpp"
#include "porting.h"
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
 * heap_oos_read_values () - Read the OOS value for every OOS-tagged variable index.
 *   return: NO_ERROR or error code
 *   thread_p(in): thread entry
 *   state(in/out): oos_blobs must be resized to n_var (empty vectors)
 */
static int
heap_oos_read_values (THREAD_ENTRY *thread_p, HEAP_OOS_EXPAND_STATE *state)
{
  std::vector<oos_read_request> requests;

  for (int i = 0; i < state->n_var; ++i)
    {
      if (!OR_IS_OOS (state->vot_raw[i]))
	{
	  continue;
	}

      const int value_offset = state->src_header_size + OR_GET_VAR_OFFSET (state->vot_raw[i]);
      OID oos_oid;
      DB_BIGINT oos_len;

      /* Reuse the single inline-reference parser (same [OID (8B) | full_length (8B)] layout and
       * the same corruption checks the lazy Resolve path uses). It has already er_set on error. */
      RECDES rec = { state->src_length, state->src_length, REC_HOME, (char *) state->src };
      if (heap_oos_parse_inline_ref (&rec, state->src + value_offset, &oos_oid, &oos_len) != NO_ERROR)
	{
	  return ER_HEAP_OOS_BAD_INLINE_HEADER;
	}

      state->oos_blobs[i].resize ((std::size_t) oos_len);
      oos_read_request request = { oos_oid,
				   oos_buffer (state->oos_blobs[i].data (), (std::size_t) oos_len)
				 };
      requests.push_back (request);
    }

  if (!requests.empty ())
    {
      int oos_err = oos_read_many (thread_p, cubbase::span<oos_read_request> (requests.data (), requests.size ()));
      if (oos_err != NO_ERROR)
	{
	  oos_error ("oos_read_many failed while expanding OOS record");
	  return oos_err;
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

  const bool need_copy_realloc = (rec->area_size < state->new_length);
  const bool need_realloc = (context->ispeeking == PEEK) || need_copy_realloc;
  if (need_realloc)
    {
      if (context->scan_cache == NULL)
	{
	  rec->length = - (state->new_length);
	  return S_DOESNT_FIT;
	}
      if (context->ispeeking != PEEK && need_copy_realloc && context->keep_recdes_buffer)
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

      if (heap_oos_read_values (thread_p, &state) != NO_ERROR)
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
 * heap_oos_parse_inline_ref () - Validate and parse the inline OOS reference of an OOS-marked
 *   variable attribute. Inline layout (M2+): [OID (8B) | full_length (8B bigint)].
 *
 *   return: NO_ERROR, or ER_HEAP_OOS_BAD_INLINE_HEADER when the reference is corrupted.
 *   recdes(in): heap record holding the attribute (only data/length are read)
 *   inline_ptr(in): start of the OOS-marked variable region inside recdes
 *   oos_oid(out): forwarder OID of the OOS record
 *   oos_len(out): full byte length of the referenced OOS payload
 */
int
heap_oos_parse_inline_ref (RECDES *recdes, const char *inline_ptr, OID *oos_oid, DB_BIGINT *oos_len)
{
  OR_BUF buf;
  int rc = NO_ERROR;

  /* Keep the OID well-defined before any er_set: Case 1 reports it before it is read. */
  OID_SET_NULL (oos_oid);
  *oos_len = 0;

  buf.ptr = (char *) inline_ptr;
  buf.endptr = recdes->data + recdes->length;

  /* Case 1: the OOS-marked variable region must start with [OID | bigint]. */
  if (buf.endptr - buf.ptr < OR_OID_SIZE + OR_BIGINT_SIZE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_BAD_INLINE_HEADER, 3, OID_AS_ARGS (oos_oid));
      return ER_HEAP_OOS_BAD_INLINE_HEADER;
    }

  or_get_oid (&buf, oos_oid);
  *oos_len = or_get_bigint (&buf, &rc);

  /* Case 2: bigint read failed or the forwarder OID is NULL.
   * Case 3: full length out of the (0, DB_MAX_STRING_LENGTH] range a stored value can hold. */
  if (rc != NO_ERROR || OID_ISNULL (oos_oid) || *oos_len <= 0 || *oos_len > (DB_BIGINT) DB_MAX_STRING_LENGTH)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_OOS_BAD_INLINE_HEADER, 3, OID_AS_ARGS (oos_oid));
      return ER_HEAP_OOS_BAD_INLINE_HEADER;
    }

  return NO_ERROR;
}

/*
 * heap_oos_find_attr_inline_ref () - Find the OOS inline reference stored in a heap record for
 *   a requested variable attribute.
 *
 *   return: pointer to the 16-byte [OOS OID | full length] reference in the variable area, or
 *           NULL when this requested attribute has no OOS reference in this record. NULL also
 *           covers conditions the scalar read path skips or reports itself, including corrupt
 *           offset-size metadata.
 */
static const char *
heap_oos_find_attr_inline_ref (RECDES *recdes, HEAP_ATTRVALUE *value)
{
  OR_ATTRIBUTE *attrepr = value->read_attrepr;
  int vot_entry;

  if (unlikely (IS_DEDUPLICATE_KEY_ATTR_ID (value->attrid)))
    {
      return NULL;
    }

  if (recdes == NULL || recdes->data == NULL || attrepr == NULL || value->attr_type == HEAP_SHARED_ATTR
      || value->attr_type == HEAP_CLASS_ATTR || attrepr->is_fixed != 0
      || OR_VAR_IS_NULL (recdes->data, attrepr->location))
    {
      return NULL;
    }

  if (heap_recdes_get_var_offset_entry (recdes, attrepr->location, &vot_entry) != NO_ERROR
      || !OR_IS_OOS (vot_entry))
    {
      return NULL;
    }

  return recdes->data + OR_VAR_OFFSET (recdes->data, attrepr->location);
}

/*
 * heap_oos_read_grouped_payloads () - Prefetch every requested OOS-marked attribute of one record
 *   through a single grouped oos_read_many() call.
 *
 *   return: NO_ERROR, or an error from inline-reference parsing, buffer allocation, or oos_read_many.
 *   raws(out): resized to attr_info->num_values. raws[i].data != NULL holds the raw disk bytes of an
 *              OOS-resolved attribute (consumed by heap_attrvalue_read's grouped fast path); raws[i].data
 *              == NULL means "not OOS here: read with the scalar reader". Always release with
 *              heap_oos_free_grouped_payloads(), including on error (partial buffers may be attached).
 *   handled(out): true iff the grouped path applies. It only applies to records with at least two
 *              requested OOS values; non-OOS and single-OOS reads stay on the scalar path (raws is
 *              left empty and *handled is false).
 */
int
heap_oos_read_grouped_payloads (THREAD_ENTRY *thread_p, RECDES *recdes, HEAP_CACHE_ATTRINFO *attr_info,
				std::vector<RECDES> &raws, bool *handled)
{
  const RECDES empty_raw = { -1, -1, REC_UNKNOWN, NULL };
  std::vector<oos_read_request> requests;
  int error = NO_ERROR;
  int oos_count = 0;
  int i;

  *handled = false;

  if (recdes == NULL || recdes->data == NULL || !heap_recdes_contains_oos (recdes))
    {
      return NO_ERROR;
    }

  for (i = 0; i < attr_info->num_values && oos_count < 2; i++)
    {
      if (heap_oos_find_attr_inline_ref (recdes, &attr_info->values[i]) != NULL)
	{
	  oos_count++;
	}
    }

  if (oos_count < 2)
    {
      return NO_ERROR;
    }

  *handled = true;

  try
    {
      raws.resize ((std::size_t) attr_info->num_values, empty_raw);
      requests.reserve ((std::size_t) attr_info->num_values);
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) attr_info->num_values * (sizeof (RECDES) + sizeof (oos_read_request)));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = 0; i < attr_info->num_values && error == NO_ERROR; i++)
    {
      const char *inline_ptr = heap_oos_find_attr_inline_ref (recdes, &attr_info->values[i]);
      OID oos_oid;
      DB_BIGINT oos_len;

      if (inline_ptr == NULL)
	{
	  continue;		/* not OOS here: the scalar reader handles it */
	}

      error = heap_oos_parse_inline_ref (recdes, inline_ptr, &oos_oid, &oos_len);
      if (error == NO_ERROR && recdes_allocate_data_area (&raws[i], (int) oos_len) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) oos_len);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	}
      if (error == NO_ERROR)
	{
	  raws[i].length = (int) oos_len;
	  oos_read_request request = { oos_oid, oos_buffer (raws[i].data, (std::size_t) oos_len) };
	  requests.push_back (request);
	}
    }

  if (error == NO_ERROR)
    {
      error = oos_read_many (thread_p, cubbase::span<oos_read_request> (requests.data (), requests.size ()));
    }

  return error;
}

/*
 * heap_oos_free_grouped_payloads () - Release the raw OOS buffers attached by
 *   heap_oos_read_grouped_payloads(). Safe on an empty vector (grouped path not taken).
 */
void
heap_oos_free_grouped_payloads (std::vector<RECDES> &raws)
{
  for (RECDES &raw : raws)
    {
      if (raw.data != NULL)
	{
	  recdes_free_data_area (&raw);
	}
    }
  raws.clear ();
}

/*
 * heap_oos_insert_serialized_values () - Insert already-serialized attribute payloads into
 *   the class OOS file.
 *
 * heap_file.c keeps DB_VALUE-to-RECDES serialization, including the BLOB/CLOB ELO-locator copy
 * step. This helper owns the OOS side of the write: class OOS file lookup, insert-publication state
 * reset, and the batched OOS API call.
 */
SCAN_CODE
heap_oos_insert_serialized_values (THREAD_ENTRY *thread_p, const OID *class_oid,
				   cubbase::span<oos_insert_request> requests)
{
  HFID oos_hfid;
  VFID oos_vfid;
  LOG_TDES *tdes;
  int tran_index;

  if (heap_get_class_info (thread_p, class_oid, &oos_hfid, NULL, NULL) != NO_ERROR)
    {
      return S_ERROR;
    }
  if (!heap_oos_find_vfid (thread_p, &oos_hfid, &oos_vfid, true))
    {
      return S_ERROR;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return S_ERROR;
    }

  tdes->oos_insert_lsa_queue.clear ();
  thread_p->oos_oids.clear ();

  if (!requests.empty () && oos_insert_many (thread_p, oos_vfid, requests) != NO_ERROR)
    {
      return S_ERROR;
    }

  return S_SUCCESS;
}

/*
 * heap_oos_delete_unreferenced () - Eagerly delete the OOS records referenced by old_recdes and
 *   not referenced by new_recdes. new_recdes == NULL means none are referenced anymore (DELETE):
 *   every OOS OID of old_recdes is deleted unconditionally, which is safe because OOS OIDs are
 *   freshly allocated per heap record and never shared across rows. With a non-NULL new_recdes
 *   (UPDATE), OIDs present in both images (same physical OOS referenced before and after) are
 *   preserved.
 *
 * Called from the non-MVCC (!is_mvcc_op) branches of heap delete/update. For REC_RELOCATION rows
 * the caller passes the forward (REC_NEWHOME) record as old_recdes, because the actual data (and
 * OOS attributes) live on the forward page; the home slot holds only an 8-byte forwarding OID.
 *
 * MVCC mode keeps the old OOS alive for concurrent readers and lets vacuum reclaim it later;
 * SA_MODE has no readers and no vacuum, so deletion happens here or not at all. Despite the
 * historical "SA_MODE" tag in the diagnostics, the !is_mvcc_op gate also fires for SERVER_MODE
 * operations on MVCC-disabled classes (catalog tables), so this code can execute server-side too.
 * Caller MUST abort the transaction on error so oos_delete's per-chunk undo records replay any
 * partial deletes during rollback; otherwise the surviving recdes would reference already-deleted
 * OOS chunks.
 *
 * Strict failure handling: the OOS header flag is set by the record transformer and read via
 * heap_recdes_contains_oos, so a missing OOS file or a failed OID extraction at this point
 * indicates real corruption — log and propagate.
 *
 * op_ctx (in): short operation tag for diagnostics, e.g. "update home", "delete relocation".
 */
int
heap_oos_delete_unreferenced (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
			      const RECDES *old_recdes, const RECDES *new_recdes, const char *op_ctx)
{
  std::vector<OID> old_oos_oids;
  std::vector<OID> new_oos_oids;
  VFID oos_vfid;
  int error_code;

  error_code = heap_recdes_get_oos_oids (old_recdes, old_oos_oids);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (%s): heap_recdes_get_oos_oids(old) failed"
		    " (hfid=%d|%d, oid=%d|%d|%d, old_rec_len=%d).",
		    op_ctx, VFID_AS_ARGS (&context->hfid.vfid),
		    context->oid.volid, context->oid.pageid, context->oid.slotid, old_recdes->length);
      return error_code;
    }
  if (old_oos_oids.empty ())
    {
      return NO_ERROR;
    }

  if (new_recdes != NULL)
    {
      /* heap_recdes_get_oos_oids returns NO_ERROR with an empty vector when the new record has no
       * OOS — no heap_recdes_contains_oos guard needed. */
      error_code = heap_recdes_get_oos_oids (new_recdes, new_oos_oids);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  er_log_debug (ARG_FILE_LINE,
			"SA_MODE eager OOS cleanup (%s): heap_recdes_get_oos_oids(new) failed"
			" (hfid=%d|%d, oid=%d|%d|%d, new_rec_len=%d).",
			op_ctx, VFID_AS_ARGS (&context->hfid.vfid),
			context->oid.volid, context->oid.pageid, context->oid.slotid, new_recdes->length);
	  return error_code;
	}
    }

  if (!heap_oos_find_vfid (thread_p, &context->hfid, &oos_vfid, false))
    {
      er_log_debug (ARG_FILE_LINE,
		    "SA_MODE eager OOS cleanup (%s): OOS flag set but no OOS VFID found for hfid %d|%d"
		    " (oid=%d|%d|%d).",
		    op_ctx, VFID_AS_ARGS (&context->hfid.vfid),
		    context->oid.volid, context->oid.pageid, context->oid.slotid);
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
			"SA_MODE eager OOS cleanup (%s): oos_delete(oos_vfid=%d|%d, oid=%d|%d|%d) failed"
			" (hfid=%d|%d, heap_oid=%d|%d|%d).",
			op_ctx, VFID_AS_ARGS (&oos_vfid), old_oid.volid, old_oid.pageid, old_oid.slotid,
			VFID_AS_ARGS (&context->hfid.vfid),
			context->oid.volid, context->oid.pageid, context->oid.slotid);
	  return error_code;
	}
    }

  return NO_ERROR;
}
