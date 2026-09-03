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
 * qfile_tuple_layout.h - temporary list file tuple slot & accessor API (CBRD-27365, ADR 0016)
 *
 * Shared by the server (list_file.c, fetch.c, ...), the SA build and the client cursor (cursor.c).
 *
 * PR-1b scope (format invariant): every reader of a list file tuple goes through the slot accessors below
 * (position / value) or, where no slot can exist, the domain-driven walk. The implementation still walks the
 * legacy per-value [flag 4B][len 4B][value] headers and only remembers the deform position (nvalid/off);
 * PR-2 swaps the implementation to the new format without touching the callers.
 *
 * Contract summary
 *   - A record used as a slot is BOUND to the layout descriptor (type_list) of the list its tuple belongs to.
 *     qfile_retrieve_tuple () binds the record it fills to the scan's type_list on every fill (filler-owns-bind,
 *     D-196-9); records filled outside a scan (raw page tuple, sort output, cursor) are bound explicitly.
 *   - qfile_slot_set_tuple () is the only sanctioned way to point a slot at another tuple; it resets the cache
 *     (mutator-owns-reset, D-182-5). memcpy into an owned buffer must be followed by it.
 *   - Accessors take the DECODING domain from the caller (the domain the writer used), while the layout comes
 *     from the bound descriptor (D-196-3). NULL handling stays with the caller: on is_null the DB_VALUE is
 *     untouched.
 */

#ifndef _QFILE_TUPLE_LAYOUT_H_
#define _QFILE_TUPLE_LAYOUT_H_

#include <assert.h>
#include <string.h>

#include "query_list.h"
#include "object_primitive.h"
#include "object_representation.h"

/* Header size every list carries in PR-1b (legacy [len][prev_len]). PR-2 derives it from the backward flag
 * of qfile_open_list () (D-181-8). */
#define QFILE_TL_HDR_SIZE_LEGACY QFILE_TUPLE_LENGTH_SIZE

/* Legacy encoding size of a bound value: the value header records the MAX_ALIGNMENT-padded disk size
 * (query_opfunc.c qdata_copy_db_value_to_tuple_value). Used by the in-place contract until PR-2. */
#define QFILE_LEGACY_VALUE_ENCODED_SIZE(disk_size) DB_ALIGN ((disk_size), MAX_ALIGNMENT)

/*
 * Layout descriptor lifetime (D-181-1/2/6).
 *   qfile_type_list_alloc    - allocate the [domp | col] block; finalized = false, domp entries unset.
 *   qfile_type_list_copy     - alloc + inherit (block memcpy when the source is finalized).
 *   qfile_type_list_finalize - (re)compute the descriptor from domp; call right after the last domp mutation.
 *   qfile_type_list_check    - debug: stored descriptor == recomputation from domp (D-181-7).
 */
extern int qfile_type_list_alloc (QFILE_TUPLE_VALUE_TYPE_LIST * tl, int type_cnt, int hdr_size);
extern int qfile_type_list_copy (QFILE_TUPLE_VALUE_TYPE_LIST * dest, const QFILE_TUPLE_VALUE_TYPE_LIST * src);
extern void qfile_type_list_finalize (QFILE_TUPLE_VALUE_TYPE_LIST * tl);
#if !defined(NDEBUG)
extern bool qfile_type_list_check (const QFILE_TUPLE_VALUE_TYPE_LIST * tl);
#endif

/*
 * qfile_slot_bind () - bind the layout descriptor of the list this record will read tuples from.
 *   The descriptor address stays stable for the life of a scan (late DB_TYPE_VARIABLE domain resolution rewrites
 *   its contents in place).
 */
inline void
qfile_slot_bind (QFILE_TUPLE_RECORD * rec, const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  rec->tl = tl;
  rec->nvalid = 0;
  rec->fast_limit = 0;
  rec->off = QFILE_TUPLE_LENGTH_SIZE;
}

/*
 * qfile_slot_set_tuple () - point the record at another tuple and reset the deform cache.
 *   Buffer management (alloc/realloc/free of an owned tpl) still assigns rec->tpl directly; the copy that fills
 *   the buffer must be followed by this call. Ownership (rec->size) is untouched.
 */
inline void
qfile_slot_set_tuple (QFILE_TUPLE_RECORD * rec, char *tpl)
{
  rec->tpl = tpl;
  rec->nvalid = 0;
  rec->off = QFILE_TUPLE_LENGTH_SIZE;
}

/*
 * qfile_slot_fill () - bind + set_tuple in one step; used by the code that fills a record from a list
 *   (qfile_retrieve_tuple, qfile_get_tuple callers) so every filled record is a usable slot (D-196-9).
 */
inline void
qfile_slot_fill (QFILE_TUPLE_RECORD * rec, char *tpl, const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  rec->tl = tl;
  rec->fast_limit = 0;
  qfile_slot_set_tuple (rec, tpl);
}

extern void qfile_slot_clear (QFILE_TUPLE_RECORD * rec);

/*
 * qfile_slot_locate () - position accessor: address of the value body of column col (D-182-7).
 *   return: pointer to the value body (meaningful only when !*is_null; the legacy body of a NULL is 0 bytes)
 *   body_len(out): stored body length (legacy: MAX_ALIGNMENT-padded disk size; 0 for NULL)
 *   is_null(out): column is NULL
 *
 *   Sequential and repeated accesses to non-decreasing columns are O(1) amortized thanks to the (nvalid, off)
 *   cache; a smaller column restarts from the first value.
 */
inline const char *
qfile_slot_locate (QFILE_TUPLE_RECORD * rec, int col, int *body_len, bool * is_null)
{
  const char *hdr;
  int i;

  assert (rec->tpl != NULL);
  assert (rec->tl != NULL && rec->tl->finalized);
  assert (col >= 0 && col < rec->tl->type_cnt);

  if (col >= rec->nvalid)
    {
      i = rec->nvalid;
      hdr = rec->tpl + rec->off;
    }
  else
    {
      i = 0;
      hdr = rec->tpl + QFILE_TUPLE_LENGTH_SIZE;
    }

  for (; i < col; i++)
    {
      hdr += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (hdr);
    }

  if (col <= INT16_MAX)
    {
      rec->nvalid = (int16_t) col;
      rec->off = (int32_t) (hdr - rec->tpl);
    }

  *body_len = QFILE_GET_TUPLE_VALUE_LENGTH (hdr);
  *is_null = (QFILE_GET_TUPLE_VALUE_FLAG (hdr) == V_UNBOUND);

  return hdr + QFILE_TUPLE_VALUE_HEADER_SIZE;
}

/*
 * qfile_slot_read_value () - value accessor: decode column col into *value with the caller's domain (D-182-7).
 *   return: NO_ERROR or the data_readval error
 *   dom(in): decoding domain (the one the writer used); the layout comes from the bound descriptor
 *   copy(in): data_readval copy flag
 *   is_null(out): column is NULL; *value is then left untouched (NULL policy stays with the caller)
 */
inline int
qfile_slot_read_value (QFILE_TUPLE_RECORD * rec, int col, const TP_DOMAIN * dom, DB_VALUE * value, bool copy,
		       bool * is_null)
{
  OR_BUF buf;
  int len;
  const char *body;

  body = qfile_slot_locate (rec, col, &len, is_null);
  if (*is_null)
    {
      return NO_ERROR;
    }

  or_init (&buf, (char *) body, len);
  return dom->type->data_readval (&buf, value, dom, -1, copy, NULL, 0);
}

/*
 * qfile_legacy_put_value () - PR-1b assembler bridge: emit one legacy [flag 4B][len 4B][body] value.
 *   The copy-style writers (merge, sort key, hash join merge) consume (body, len, is_null) from the position accessor
 *   and re-emit the value with this; PR-2 replaces them with the tuple assembler (D-182-11).
 */
inline void
qfile_legacy_put_value (char *out, const char *body, int len, bool is_null)
{
  QFILE_PUT_TUPLE_VALUE_FLAG (out, is_null ? V_UNBOUND : V_BOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH (out, len);
  if (len > 0)
    {
      memcpy (out + QFILE_TUPLE_VALUE_HEADER_SIZE, body, len);
    }
}

/*
 * qfile_slot_overwrite_value () - in-place rewrite of a bound, same-encoding-size column (D-182-13, #185).
 *   return: NO_ERROR, ER_FAILED when the in-place contract is violated (asserted in debug)
 */
extern int qfile_slot_overwrite_value (QFILE_TUPLE_RECORD * rec, int col, const TP_DOMAIN * dom,
				       const DB_VALUE * value);

/*
 * Domain-driven sequential walk (D-182-16): for readers that cannot bind a descriptor (px XASL_SNAPSHOT reader,
 * thread contract D-181-10) or that own the column domains themselves (aggregate hash entry (de)serialization).
 * The legacy implementation does not need the domain; PR-2 derives kind/size/alignby from it.
 */
typedef struct qfile_tuple_walk QFILE_TUPLE_WALK;
struct qfile_tuple_walk
{
  const char *tpl;		/* tuple start */
  int tpl_len;			/* QFILE_GET_TUPLE_LENGTH (tpl) */
  int off;			/* offset of the next value header */
};

inline void
qfile_tuple_walk_init (QFILE_TUPLE_WALK * walk, const char *tpl)
{
  walk->tpl = tpl;
  walk->tpl_len = QFILE_GET_TUPLE_LENGTH (tpl);
  walk->off = QFILE_TUPLE_LENGTH_SIZE;
}

/*
 * qfile_tuple_walk_next () - advance to the next value.
 *   return: NO_ERROR (bounds are asserted like or_advance does)
 *   body(out): value body; len(out): stored body length; is_null(out): NULL column
 */
inline int
qfile_tuple_walk_next (QFILE_TUPLE_WALK * walk, const TP_DOMAIN * dom, const char **body, int *len, bool * is_null)
{
  const char *hdr = walk->tpl + walk->off;
  int vlen;

  (void) dom;			/* legacy format is self-describing */
  assert (walk->off + QFILE_TUPLE_VALUE_HEADER_SIZE <= walk->tpl_len);

  vlen = QFILE_GET_TUPLE_VALUE_LENGTH (hdr);
  *is_null = (QFILE_GET_TUPLE_VALUE_FLAG (hdr) == V_UNBOUND);
  *len = vlen;
  *body = hdr + QFILE_TUPLE_VALUE_HEADER_SIZE;

  walk->off += QFILE_TUPLE_VALUE_HEADER_SIZE + vlen;
  assert (walk->off <= walk->tpl_len);

  return NO_ERROR;
}

#endif /* _QFILE_TUPLE_LAYOUT_H_ */
