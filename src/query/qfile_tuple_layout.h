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
 * PR-1b/PR-2a scope (format invariant): every reader of a list file tuple goes through the slot accessors below
 * (position / value) or, where no slot can exist, the domain-driven walk; every writer goes through the tuple
 * assembler (qfile_tuple_size / qfile_tuple_fill). The implementation still uses the legacy per-value
 * [flag 4B][len 4B][value] headers and only remembers the deform position (nvalid/off); PR-2b swaps the
 * implementation to the ADR 0016 format without touching the callers.
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
#include "dbtype.h"
#include "error_manager.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "string_opfunc.h"	/* db_get_string_length (debug size probe) */

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
 * Tuple assembler (D-182-11): two passes over QFILE_TUPLE_COL_SRC[] (or a DB_VALUE *[] for the T_NORMAL descriptor
 * path). qfile_tuple_size () returns the exact tuple length (header included) and records each val source's disk
 * size in src[i].len; qfile_tuple_fill () writes exactly that many bytes at out. Every list tuple - page resident,
 * private buffer, sort key body - is produced here, so the byte format lives in this one place.
 *
 * PR-2a implementation = legacy layout: [len 4B][prev_len 4B] then per column [flag 4B][len 4B][body padded to 8B].
 * PR-2b swaps the bodies of these functions for the ADR 0016 format without touching the callers.
 *
 * tl is the layout descriptor of the list the tuple is written into (its domp[] decides the layout in PR-2b); the
 * legacy layout only uses it for the debug type probe below. It may be NULL / unfinalized for the few writers that
 * assemble a tuple before their list exists.
 */

/* legacy encoded size of a bound body of len bytes (header + MAX_ALIGNMENT padding) */
#define QFILE_LEGACY_VALUE_SIZE(len) (QFILE_TUPLE_VALUE_HEADER_SIZE + DB_ALIGN ((len), MAX_ALIGNMENT))

inline void
qfile_col_src_set_value (QFILE_TUPLE_COL_SRC * src, const DB_VALUE * val)
{
  src->val = val;
  src->data = NULL;
  src->len = 0;
  src->is_null = (val == NULL || DB_IS_NULL (val));
}

inline void
qfile_col_src_set_raw (QFILE_TUPLE_COL_SRC * src, const char *data, int len, bool is_null)
{
  src->val = NULL;
  src->data = data;
  src->len = is_null ? 0 : len;
  src->is_null = is_null;
}

/*
 * qfile_value_disk_size () - disk size of a bound value as data_writeval will write it.
 *   return: size, or ER_FAILED (debug builds only: string longer than its precision, see the legacy
 *           qdata_get_tuple_value_size_from_dbval)
 */
inline int
qfile_value_disk_size (const DB_VALUE * value)
{
  DB_VALUE *v = (DB_VALUE *) value;
  int val_size = pr_data_writeval_disk_size (v);

#if !defined(NDEBUG)
  {
    DB_TYPE dbval_type = DB_VALUE_DOMAIN_TYPE (v);
    const PR_TYPE *type_p = pr_type_from_id (dbval_type);

    if (type_p != NULL && type_p->is_size_computed () && pr_is_string_type (dbval_type))
      {
	int precision = DB_VALUE_PRECISION (v);
	int string_length = db_get_string_length (v);

	if (precision == TP_FLOATING_PRECISION_VALUE)
	  {
	    precision = DB_MAX_STRING_LENGTH;
	  }
	assert (string_length <= precision);
	if (val_size < 0)
	  {
	    return ER_FAILED;
	  }
	else if (string_length > precision)
	  {
	    /* abnormal (asserted above); kept from the legacy code for backward compatibility */
	    if (db_string_truncate (v, precision) != NO_ERROR)
	      {
		return ER_FAILED;
	      }
	    er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_DATA_IS_TRUNCATED_TO_PRECISION, 2, precision,
		    string_length);
	    val_size = pr_data_writeval_disk_size (v);
	  }
      }
  }
#endif

  return val_size;
}

#if !defined(NDEBUG)
/*
 * qfile_tuple_check_col_type () - writer-side probe (PR-1b handover 4): a value written into column col must have
 *   the column's type, because the PR-2b layout of the column comes from tl->domp[col]. Compatible pairs that
 *   share a disk representation are accepted; an unresolved DB_TYPE_VARIABLE column accepts anything.
 */
inline void
qfile_tuple_check_col_type (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, int col, const DB_VALUE * val)
{
  DB_TYPE ctype, vtype;

  if (tl == NULL || !tl->finalized || tl->domp == NULL || col >= tl->type_cnt)
    {
      return;
    }
  ctype = TP_DOMAIN_TYPE (tl->domp[col]);
  vtype = DB_VALUE_DOMAIN_TYPE (val);
  assert (ctype == DB_TYPE_VARIABLE || ctype == vtype || (pr_is_string_type (ctype) && pr_is_string_type (vtype))
	  || ((TP_IS_SET_TYPE (ctype) || ctype == DB_TYPE_VOBJ) && (TP_IS_SET_TYPE (vtype) || vtype == DB_TYPE_VOBJ))
	  || ((ctype == DB_TYPE_OBJECT || ctype == DB_TYPE_OID) && (vtype == DB_TYPE_OBJECT || vtype == DB_TYPE_OID)));
}
#endif

/*
 * qfile_tuple_size () - assembler size pass.
 *   return: exact tuple length (header included), or ER_FAILED
 *   src(in/out): a val source gets its disk size stored in len
 *   has_null(out): at least one column is NULL
 */
inline int
qfile_tuple_size (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, QFILE_TUPLE_COL_SRC * src, int n, bool * has_null)
{
  int i, size = QFILE_TUPLE_LENGTH_SIZE;
  bool hn = false;

  assert (tl == NULL || !tl->finalized || tl->type_cnt == n);

  for (i = 0; i < n; i++)
    {
      if (src[i].is_null)
	{
	  hn = true;
	  size += QFILE_TUPLE_VALUE_HEADER_SIZE;
	  continue;
	}
      if (src[i].val != NULL)
	{
	  src[i].len = qfile_value_disk_size (src[i].val);
	  if (src[i].len < 0)
	    {
	      return ER_FAILED;
	    }
	}
      size += QFILE_LEGACY_VALUE_SIZE (src[i].len);
    }

  *has_null = hn;
  return size;
}

/*
 * qfile_tuple_put_value () - emit one legacy [flag][len][body] column at p; return the bytes written.
 */
inline int
qfile_tuple_put_value (char *p, const QFILE_TUPLE_COL_SRC * src)
{
  OR_BUF buf;
  int len, padded;

  if (src->is_null)
    {
      QFILE_PUT_TUPLE_VALUE_FLAG (p, V_UNBOUND);
      QFILE_PUT_TUPLE_VALUE_LENGTH (p, 0);
      return QFILE_TUPLE_VALUE_HEADER_SIZE;
    }

  len = src->len;
  padded = DB_ALIGN (len, MAX_ALIGNMENT);
  QFILE_PUT_TUPLE_VALUE_FLAG (p, V_BOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH (p, padded);
  p += QFILE_TUPLE_VALUE_HEADER_SIZE;

  if (src->val != NULL)
    {
      const PR_TYPE *pr_type = pr_type_from_id (DB_VALUE_DOMAIN_TYPE (src->val));

      if (pr_type == NULL)
	{
	  return ER_FAILED;
	}
      or_init (&buf, p, len);
      if (pr_type->data_writeval (&buf, (DB_VALUE *) src->val) != NO_ERROR || buf.ptr > buf.endptr)
	{
	  assert_release (false);	/* written size must equal the computed size (#183) */
	  return ER_FAILED;
	}
    }
  else if (len > 0)
    {
      memcpy (p, src->data, len);
    }
#if !defined(NDEBUG)
  memset (p + len, 0, padded - len);	/* suppress valgrind UMW */
#endif

  return QFILE_TUPLE_VALUE_HEADER_SIZE + padded;
}

/*
 * qfile_tuple_fill () - assembler fill pass: write the tuple that qfile_tuple_size () measured.
 *   return: NO_ERROR or ER_FAILED
 *   out(in): destination (list page or private buffer) with at least size bytes
 */
inline int
qfile_tuple_fill (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, const QFILE_TUPLE_COL_SRC * src, int n, char *out,
		  int size, bool has_null)
{
  char *p = out + QFILE_TUPLE_LENGTH_SIZE;
  int i, w;

  (void) has_null;		/* legacy layout has no null bitmap */
  QFILE_PUT_TUPLE_LENGTH (out, size);

  for (i = 0; i < n; i++)
    {
#if !defined(NDEBUG)
      if (!src[i].is_null && src[i].val != NULL)
	{
	  qfile_tuple_check_col_type (tl, i, src[i].val);
	}
#else
      (void) tl;
#endif
      w = qfile_tuple_put_value (p, &src[i]);
      if (w < 0)
	{
	  return ER_FAILED;
	}
      p += w;
    }

  assert (CAST_BUFLEN (p - out) == size);
  return NO_ERROR;
}

/*
 * qfile_tuple_size_from_values () / qfile_tuple_fill_from_values () - T_NORMAL overload over the descriptor's
 *   f_valp[] (no source array). The fill pass recomputes each disk size (D-190-3: the string size is cached in the
 *   DB_VALUE after the first computation, so this matches the legacy cost).
 */
inline int
qfile_tuple_size_from_values (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, DB_VALUE ** vals, int n, bool * has_null)
{
  int i, len, size = QFILE_TUPLE_LENGTH_SIZE;
  bool hn = false;

  assert (tl == NULL || !tl->finalized || tl->type_cnt == n);

  for (i = 0; i < n; i++)
    {
      if (DB_IS_NULL (vals[i]))
	{
	  hn = true;
	  size += QFILE_TUPLE_VALUE_HEADER_SIZE;
	  continue;
	}
      len = qfile_value_disk_size (vals[i]);
      if (len < 0)
	{
	  return ER_FAILED;
	}
      size += QFILE_LEGACY_VALUE_SIZE (len);
    }

  *has_null = hn;
  return size;
}

inline int
qfile_tuple_fill_from_values (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, DB_VALUE ** vals, int n, char *out, int size)
{
  QFILE_TUPLE_COL_SRC src;
  char *p = out + QFILE_TUPLE_LENGTH_SIZE;
  int i, w;

  QFILE_PUT_TUPLE_LENGTH (out, size);

  for (i = 0; i < n; i++)
    {
      qfile_col_src_set_value (&src, vals[i]);
      if (!src.is_null)
	{
	  src.len = pr_data_writeval_disk_size (vals[i]);
#if !defined(NDEBUG)
	  qfile_tuple_check_col_type (tl, i, vals[i]);
#endif
	}
      w = qfile_tuple_put_value (p, &src);
      if (w < 0)
	{
	  return ER_FAILED;
	}
      p += w;
    }
#if defined(NDEBUG)
  (void) tl;
#endif

  assert (CAST_BUFLEN (p - out) == size);
  return NO_ERROR;
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
