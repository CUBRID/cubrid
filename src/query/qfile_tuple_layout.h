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
 * qfile_tuple_layout.h - temporary list file tuple slot, accessor and assembler API (CBRD-27365, ADR 0016)
 *
 * Shared by the server (list_file.c, fetch.c, ...), the SA build and the client cursor (cursor.c). This is the only
 * code that interprets the bytes of a list file tuple (format: query_list.h "Tuple byte format").
 *
 * Contract summary
 *   - A record used as a slot is BOUND to the layout descriptor (type_list) of the list its tuple belongs to.
 *     qfile_retrieve_tuple () binds the record it fills to the scan's type_list on every fill (filler-owns-bind,
 *     D-196-9); records filled outside a scan (raw page tuple, sort output, cursor) are bound explicitly.
 *   - qfile_slot_set_tuple () is the only sanctioned way to point a slot at another tuple; it resets the deform cache
 *     and the scratch bump offset (mutator-owns-reset, D-182-5). memcpy into an owned buffer must be followed by it.
 *   - Accessors take the DECODING domain from the caller (the domain the writer used), while the layout comes
 *     from the bound descriptor (D-196-3). NULL handling stays with the caller: on is_null the DB_VALUE is
 *     untouched.
 *   - qfile_slot_locate () returns the stored body of a column. For a FIXED column that is the aligned data_*
 *     encoding; for a VAR column it is the index_* encoding (DIRECT) or an unaligned data_* encoding (SCRATCH).
 *     Raw consumers may only dereference FIXED bodies (hash keys, counters); everything else decodes through
 *     qfile_slot_read_value () or copies through qfile_slot_locate_aligned ().
 *   - A VAR/SCRATCH body is decoded from a transient aligned copy (stack, heap beyond 256 bytes) with copy == true,
 *     so the DB_VALUE owns its bytes and no slot owns a scratch area (D-199-2). Every reader of such values already
 *     clears its DB_VALUE (fetch clears vfetch_to before each read; set/JSON values are heap objects anyway).
 */

#ifndef _QFILE_TUPLE_LAYOUT_H_
#define _QFILE_TUPLE_LAYOUT_H_

#include <assert.h>
#include <string.h>
#include <stdint.h>

#include "query_list.h"
#include "dbtype.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "string_opfunc.h"	/* db_get_string_length (debug size probe) */

/*
 * Column classification (D-180-4/5, D-196-11, D-199-1).
 *   FIXED: constant disksize, alignby = min (natural alignment, 4).
 *   VAR  : has_computed_disk_size () or an unresolved DB_TYPE_VARIABLE. DIRECT when the type's index_* encoding is
 *          byte-order neutral and alignment free (string family, BIT, NUMERIC: has_index_readval ()); SCRATCH for the
 *          rest (SET/JSON/ELO/VOBJ/OBJECT/...: data_* encoding copied through an aligned scratch). OBJECT has an
 *          index reader but its index encoding is host-order raw bytes and its data encoding may be a VOBJ set on the
 *          client, so it is SCRATCH (D-199-1).
 */
inline void
qfile_col_layout_of_domain (const TP_DOMAIN * dom, QFILE_COL_LAYOUT * c)
{
  const PR_TYPE *t;
  DB_TYPE id;
  int a;

  assert (dom != NULL && dom->type != NULL);
  t = dom->type;
  id = TP_DOMAIN_TYPE (dom);

  c->off = -1;
  c->_pad = 0;

  if (id == DB_TYPE_VARIABLE || t->has_computed_disk_size ())
    {
      c->size = -1;
      c->kind = QFILE_COL_VAR;
      c->var_access = (t->has_index_readval () && id != DB_TYPE_OBJECT && id != DB_TYPE_OID && id != DB_TYPE_VARIABLE)
	? QFILE_VAR_DIRECT : QFILE_VAR_SCRATCH;
      c->alignby = 1;
      return;
    }

  a = MIN (t->alignment, QFILE_TUPLE_ALIGNMENT);	/* D-180-4: BIGINT/DOUBLE align 4 and are read by memcpy */
  if (a < 1)
    {
      a = 1;
    }
  c->size = (int16_t) t->disksize;
  c->kind = QFILE_COL_FIXED;
  c->var_access = 0;
  c->alignby = (uint8_t) a;
}

/* variable value length header (D-180-6) */
inline int
qfile_var_hdr_decode (const char *p, int *hdr)
{
  unsigned char b0 = (unsigned char) *p;
  unsigned int w;

  if ((b0 & QFILE_VAR_HDR_LONG_BIT) == 0)
    {
      *hdr = 1;
      return (int) b0;
    }
  memcpy (&w, p, 4);
  *hdr = 4;
  return (int) (ntohl (w) & QFILE_TUPLE_LENGTH_MASK);
}

inline void
qfile_var_hdr_encode (char *p, int len)
{
  unsigned int w;

  assert (len >= 0);
  if (len <= QFILE_VAR_HDR_SHORT_MAX)
    {
      *p = (char) len;
      return;
    }
  w = htonl ((unsigned int) len | QFILE_TUPLE_LENGTH_HAS_NULL_BIT);
  memcpy (p, &w, 4);
}

/* first NULL column of a has-null tuple (PG first_null_attr, capped at type_cnt) */
inline int
qfile_first_null_col (const unsigned char *bm, int type_cnt)
{
  int nbytes = (type_cnt + 7) >> 3;
  int i, b;

  for (i = 0; i < nbytes; i++)
    {
      if (bm[i] != 0xFF)
	{
	  unsigned int inv = (unsigned int) (~bm[i] & 0xFF);
	  for (b = 0; (inv & 1) == 0; b++, inv >>= 1)
	    {
	      ;
	    }
	  return MIN ((i << 3) + b, type_cnt);
	}
    }
  return type_cnt;
}

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
 * qfile_type_list_is_resolved () - false while any column is still DB_TYPE_VARIABLE (its layout is not settled yet)
 *   Runtime predicate (the recursive-CTE common-list optimization gate, D-192-2), not debug-only.
 */
inline bool
qfile_type_list_is_resolved (const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  int i;

  for (i = 0; i < tl->type_cnt; i++)
    {
      if (TP_DOMAIN_TYPE (tl->domp[i]) == DB_TYPE_VARIABLE)
	{
	  return false;
	}
    }
  return true;
}

/*
 * qfile_slot_bind () - bind the layout descriptor of the list this record will read tuples from.
 *   The descriptor address stays stable for the life of a scan (late DB_TYPE_VARIABLE domain resolution rewrites
 *   its contents in place).
 */
inline void
qfile_slot_bind (QFILE_TUPLE_RECORD * rec, const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  rec->tl = tl;
  rec->nvalid = -1;
}

/*
 * qfile_slot_set_tuple () - point the record at another tuple and reset the deform cache.
 *   Buffer management (alloc/realloc/free of an owned tpl) still assigns rec->tpl directly; the copy that fills
 *   the buffer must be followed by this call. Ownership (rec->size) is untouched. The position cache is started
 *   lazily by the first accessor call (has-null bit, bitmap scan), so tuples that are never read cost nothing.
 */
inline void
qfile_slot_set_tuple (QFILE_TUPLE_RECORD * rec, char *tpl)
{
  rec->tpl = tpl;
  rec->nvalid = -1;
}

/*
 * qfile_slot_fill () - bind + set_tuple in one step; used by the code that fills a record from a list
 *   (qfile_retrieve_tuple, qfile_get_tuple callers) so every filled record is a usable slot (D-196-9).
 */
inline void
qfile_slot_fill (QFILE_TUPLE_RECORD * rec, char *tpl, const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  rec->tl = tl;
  qfile_slot_set_tuple (rec, tpl);
}

extern void qfile_slot_clear (QFILE_TUPLE_RECORD * rec);

/*
 * qfile_prefix_end () - offset (from data_off) where the bytes of the constant prefix columns [0, lim) end, i.e. where
 *   the walk for column lim starts. This is the UNALIGNED end of column lim-1: the writer aligns a column only when it
 *   writes it, so a NULL column lim contributes no padding and the next bound column starts right here (the aligned
 *   start col[lim].off is only valid when column lim itself is bound). lim <= first_non_cached_col.
 */
inline int
qfile_prefix_end (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, int lim)
{
  assert (lim >= 0 && lim <= tl->first_non_cached_col);
  if (lim == 0)
    {
      return 0;
    }
  return tl->col[lim - 1].off + tl->col[lim - 1].size;
}

/*
 * qfile_slot_start () - start the position cache for the current tuple (D-182-4/5):
 *   fast_limit = min (first non-cached column, first NULL column); nvalid = fast_limit; off = end of the prefix.
 */
inline void
qfile_slot_start (QFILE_TUPLE_RECORD * rec)
{
  const QFILE_TUPLE_VALUE_TYPE_LIST *tl = rec->tl;
  int lim = tl->first_non_cached_col;

  assert (rec->tpl != NULL && tl != NULL && tl->finalized);

  rec->has_null = QFILE_GET_TUPLE_HAS_NULL (rec->tpl);
  rec->data_off = tl->data_off[rec->has_null ? 1 : 0];
  if (rec->has_null)
    {
      int fn = qfile_first_null_col (QFILE_TUPLE_BITMAP (rec->tpl, tl->hdr_size), tl->type_cnt);
      lim = MIN (lim, fn);
    }
  rec->fast_limit = (int16_t) MIN (lim, INT16_MAX);
  rec->nvalid = rec->fast_limit;
  rec->off = rec->data_off + qfile_prefix_end (tl, lim);
}

/*
 * qfile_slot_locate () - position accessor: address of the stored body of column col (D-182-7).
 *   return: pointer to the stored body (meaningful only when !*is_null; a NULL column has no bytes)
 *   body_len(out): stored body length (FIXED: disksize; VAR: body length L without the length header; 0 for NULL)
 *   is_null(out): column is NULL
 *
 *   Columns before fast_limit are O(1) (constant offset). Sequential and repeated accesses to non-decreasing
 *   columns beyond it are O(1) amortized thanks to the (nvalid, off) cache; a smaller column restarts from
 *   fast_limit.
 */
inline const char *
qfile_slot_locate (QFILE_TUPLE_RECORD * rec, int col, int *body_len, bool * is_null)
{
  const QFILE_TUPLE_VALUE_TYPE_LIST *tl = rec->tl;
  const QFILE_COL_LAYOUT *c;
  const unsigned char *bm;
  const char *tpl = rec->tpl;
  int i, off, hdr, len;

  assert (tpl != NULL);
  assert (tl != NULL && tl->finalized);
  assert (col >= 0 && col < tl->type_cnt);

  if (rec->nvalid < 0)
    {
      qfile_slot_start (rec);
    }

  if (col < rec->fast_limit)
    {
      c = &tl->col[col];
      *body_len = c->size;
      *is_null = false;
      return tpl + rec->data_off + c->off;
    }

  if (col >= rec->nvalid)
    {
      i = rec->nvalid;
      off = rec->off;
    }
  else
    {
      i = rec->fast_limit;
      off = rec->data_off + qfile_prefix_end (tl, i);
    }

  bm = rec->has_null ? QFILE_TUPLE_BITMAP (tpl, tl->hdr_size) : NULL;

  for (; i < col; i++)
    {
      if (bm != NULL && !QFILE_BITMAP_IS_BOUND (bm, i))
	{
	  continue;
	}
      c = &tl->col[i];
      if (c->kind == QFILE_COL_FIXED)
	{
	  off = DB_ALIGN (off, c->alignby) + c->size;
	}
      else
	{
	  len = qfile_var_hdr_decode (tpl + off, &hdr);
	  off += hdr + len;
	}
    }

  if (col <= INT16_MAX)
    {
      rec->nvalid = (int16_t) col;
      rec->off = off;
    }

  if (bm != NULL && !QFILE_BITMAP_IS_BOUND (bm, col))
    {
      *body_len = 0;
      *is_null = true;
      return tpl + off;
    }

  c = &tl->col[col];
  if (c->kind == QFILE_COL_FIXED)
    {
      *body_len = c->size;
      *is_null = false;
      return tpl + DB_ALIGN (off, c->alignby);
    }

  len = qfile_var_hdr_decode (tpl + off, &hdr);
  *body_len = len;
  *is_null = false;
  return tpl + off + hdr;
}

/* transient aligned staging for SCRATCH bodies (read and write side): stack for the usual sizes, heap beyond */
#define QFILE_SCRATCH_STACK 256
#define QFILE_SCRATCH_ACQUIRE(stack_buf, len) \
  ((len) <= QFILE_SCRATCH_STACK ? PTR_ALIGN ((stack_buf), MAX_ALIGNMENT) : (char *) db_private_alloc (NULL, (len)))
#define QFILE_SCRATCH_RELEASE(p, stack_buf) \
  do { if ((p) != PTR_ALIGN ((stack_buf), MAX_ALIGNMENT)) { db_private_free (NULL, (p)); } } while (0)

/*
 * qfile_col_read_body () - decode a stored body with the column's layout kind into *value.
 *   return: NO_ERROR or the readval error
 *   c(in): layout of the column the body was stored in
 *   body/len(in): what qfile_slot_locate () / qfile_tuple_walk_next () returned for a bound column
 *   dom(in): decoding domain (D-196-3)
 *   copy(in): readval copy flag; a SCRATCH body is always decoded with copy == true from a transient aligned copy
 *             (D-199-2), so the value never aliases tuple bytes
 */
inline bool
qfile_col_body_needs_copy (const QFILE_COL_LAYOUT * c, const TP_DOMAIN * dom)
{
  return c->kind == QFILE_COL_VAR && (c->var_access == QFILE_VAR_SCRATCH || !dom->type->has_index_readval ());
}

inline int
qfile_col_read_body (const QFILE_COL_LAYOUT * c, const char *body, int len, const TP_DOMAIN * dom, DB_VALUE * value,
		     bool copy)
{
  OR_BUF buf;
  char stack_buf[QFILE_SCRATCH_STACK + MAX_ALIGNMENT];
  char *aligned;
  int rc;

  if (c->kind == QFILE_COL_FIXED)
    {
      or_init (&buf, (char *) body, len);
      return dom->type->data_readval (&buf, value, dom, -1, copy, NULL, 0);
    }
  if (c->var_access == QFILE_VAR_DIRECT && dom->type->has_index_readval ())
    {
      or_init (&buf, (char *) body, len);
      return dom->type->index_readval (&buf, value, dom, len, copy, NULL, 0);
    }

  aligned = QFILE_SCRATCH_ACQUIRE (stack_buf, len);
  if (aligned == NULL)
    {
      return ER_FAILED;
    }
  memcpy (aligned, body, len);
  or_init (&buf, aligned, len);
  rc = dom->type->data_readval (&buf, value, dom, -1, true, NULL, 0);
  QFILE_SCRATCH_RELEASE (aligned, stack_buf);
  return rc;
}

/*
 * qfile_slot_read_value () - value accessor: decode column col into *value with the caller's domain (D-182-7).
 *   return: NO_ERROR or the readval error
 *   dom(in): decoding domain (the one the writer used); the layout comes from the bound descriptor
 *   copy(in): readval copy flag
 *   is_null(out): column is NULL; *value is then left untouched (NULL policy stays with the caller)
 */
inline int
qfile_slot_read_value (QFILE_TUPLE_RECORD * rec, int col, const TP_DOMAIN * dom, DB_VALUE * value, bool copy,
		       bool * is_null)
{
  const QFILE_COL_LAYOUT *c;
  const char *body;
  int len;

  body = qfile_slot_locate (rec, col, &len, is_null);
  if (*is_null)
    {
      return NO_ERROR;
    }

  c = &rec->tl->col[col];
  /* probe: the decoding domain should agree with the stored kind. Exceptions: an unresolved column (values are NULL
   * then), the OBJECT/OID pair (OBJECT columns are VAR/SCRATCH, decoded from an aligned copy with either domain) and a
   * DB_TYPE_NULL decoding domain (a regu whose domain the compiler left unresolved, e.g. the ISS/covering plan probe of
   * CTP _19_apricot/_03_index_skip_scan/_05: mr_data_readval_null leaves the value NULL exactly as the legacy reader did). */
#if !defined(NDEBUG)
  if (TP_DOMAIN_TYPE (rec->tl->domp[col]) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (dom) != DB_TYPE_VARIABLE
      && TP_DOMAIN_TYPE (dom) != DB_TYPE_NULL && TP_DOMAIN_TYPE (dom) != DB_TYPE_OID
      && TP_DOMAIN_TYPE (dom) != DB_TYPE_OBJECT)
    {
      QFILE_COL_LAYOUT dc;

      qfile_col_layout_of_domain (dom, &dc);
      /* the decoding domain must read the bytes the column stored: same kind, same fixed width, same var access */
      assert (dc.kind == c->kind && (c->kind == QFILE_COL_VAR ? dc.var_access == c->var_access : dc.size == c->size));
    }
#endif

  return qfile_col_read_body (c, body, len, dom, value, copy);
}

/*
 * qfile_col_cmpdisk_function () - the disk comparator matching a column's stored encoding (D-180-8).
 *   FIXED -> data_cmpdisk on the aligned body; VAR/DIRECT -> index_cmpdisk on the index body; VAR/SCRATCH ->
 *   data_cmpdisk, which requires the caller to compare aligned copies (qfile_col_body_needs_copy).
 */
inline pr_type::data_cmpdisk_function_type
qfile_col_cmpdisk_function (const QFILE_COL_LAYOUT * c, const TP_DOMAIN * dom)
{
  if (c->kind == QFILE_COL_VAR && c->var_access == QFILE_VAR_DIRECT && dom->type->has_index_readval ())
    {
      return dom->type->get_index_cmpdisk_function ();
    }
  return dom->type->get_data_cmpdisk_function ();
}

/*
 * Tuple assembler (D-182-11): two passes over QFILE_TUPLE_COL_SRC[] (or a DB_VALUE *[] for the T_NORMAL descriptor
 * path). qfile_tuple_size () returns the exact tuple length (header included) and records each val source's body
 * size in src[i].len; qfile_tuple_fill () writes exactly that many bytes at out. Every list tuple - page resident,
 * private buffer, sort key body - is produced here, so the byte format lives in this one place.
 *
 * tl is the FINALIZED layout descriptor of the list the tuple is written into: its col[] decides the layout and its
 * hdr_size the header. A raw source (val == NULL) must come from a column of the same layout kind.
 */

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
 * qfile_value_direct () - a value is written in the index_* encoding when its column is VAR/DIRECT and its own type
 *   has that encoding (a DIRECT column only receives string/BIT/NUMERIC values, D-190-9).
 */
inline bool
qfile_value_direct (const QFILE_COL_LAYOUT * c, const DB_VALUE * value)
{
  return c->kind == QFILE_COL_VAR && c->var_access == QFILE_VAR_DIRECT
    && pr_type_from_id (DB_VALUE_DOMAIN_TYPE (value))->has_index_readval ();
}

/*
 * qfile_value_body_size () - body size of a bound value in the encoding its column stores (D-180-6.3).
 *   return: size, or ER_FAILED (debug builds only: string longer than its precision, see the legacy
 *           qdata_get_tuple_value_size_from_dbval)
 */
inline int
qfile_value_body_size (const QFILE_COL_LAYOUT * c, const DB_VALUE * value)
{
  DB_VALUE *v = (DB_VALUE *) value;
  int val_size;

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
	if (string_length > precision)
	  {
	    /* abnormal (asserted above); kept from the legacy code for backward compatibility */
	    if (db_string_truncate (v, precision) != NO_ERROR)
	      {
		return ER_FAILED;
	      }
	    er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_DATA_IS_TRUNCATED_TO_PRECISION, 2, precision,
		    string_length);
	  }
      }
  }
#endif

  if (qfile_value_direct (c, value))
    {
      val_size = pr_index_writeval_disk_size (v);
    }
  else
    {
      val_size = pr_data_writeval_disk_size (v);
      if (c->kind == QFILE_COL_VAR && c->var_access == QFILE_VAR_DIRECT)
	{
	  /* a DIRECT column stores the index_* encoding; a value without one would be written in the data_* encoding
	   * and misread by every reader */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_FAILED;
	}
    }
  if (c->kind == QFILE_COL_FIXED && val_size != c->size)
    {
      /* the format is not self-describing (D-182-1): a value wider or narrower than the column's fixed width would
       * overrun or underrun the tuple, so refuse it in release builds too (the legacy [flag][len] format tolerated it) */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }
  return val_size;
}

#if !defined(NDEBUG)
/*
 * qfile_tuple_check_col_type () - writer-side probe (PR-1b handover 4): a value written into column col must have
 *   the column's type, because the layout of the column comes from tl->domp[col]. Compatible pairs that share a
 *   layout kind are accepted; an unresolved DB_TYPE_VARIABLE column accepts anything.
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
  assert (ctype != DB_TYPE_VARIABLE);	/* resolved by the size pass (qfile_tuple_resolve_column) */
  assert (ctype == vtype || (pr_is_string_type (ctype) && pr_is_string_type (vtype))
	  || ((TP_IS_SET_TYPE (ctype) || ctype == DB_TYPE_VOBJ) && (TP_IS_SET_TYPE (vtype) || vtype == DB_TYPE_VOBJ))
	  || ((ctype == DB_TYPE_OBJECT || ctype == DB_TYPE_OID) && (vtype == DB_TYPE_OBJECT || vtype == DB_TYPE_OID)));
}
#endif

/*
 * qfile_tuple_resolve_column () - an unresolved (DB_TYPE_VARIABLE) column receives its first bound value: fix the
 *   column's domain from that value and recompute the layout (mutator-owns-finalize, D-181-6; D-199-13).
 *   return: true when the descriptor changed
 *   The format is not self-describing, so a column's layout must be settled before its first bound value is written;
 *   the executor's regu-driven resolution (qfile_update_domains_on_type_list) can lag behind by several tuples when
 *   the regu domain itself is still DB_TYPE_VARIABLE, and every tuple written meanwhile would be laid out as VAR.
 *   Because the column is resolved at its FIRST bound value, every earlier tuple holds NULL there (0 bytes, no
 *   padding), so re-finalizing does not change how those tuples are read. (#186 had claimed the executor guarantees
 *   this; it does not - D-199-13 - which is why the assembler enforces it here.)
 */
inline bool
qfile_tuple_resolve_column (QFILE_TUPLE_VALUE_TYPE_LIST * tl, int col, const DB_VALUE * val)
{
  TP_DOMAIN *dom;

  if (TP_DOMAIN_TYPE (tl->domp[col]) != DB_TYPE_VARIABLE || val == NULL || DB_IS_NULL (val))
    {
      return false;
    }
  dom = tp_domain_resolve_value (val, NULL);
  if (dom == NULL || TP_DOMAIN_TYPE (dom) == DB_TYPE_VARIABLE)
    {
      return false;
    }
  tl->domp[col] = dom;
  return true;
}

/*
 * qfile_tuple_size () - assembler size pass.
 *   return: exact tuple length (header included, multiple of 4), or ER_FAILED
 *   tl(in/out): layout descriptor of the destination list; an unresolved column is resolved from its first bound value
 *   src(in/out): a val source gets its body size stored in len
 *   has_null(out): at least one column is NULL
 */
inline int
qfile_tuple_size (QFILE_TUPLE_VALUE_TYPE_LIST * tl, QFILE_TUPLE_COL_SRC * src, int n, bool * has_null)
{
  const QFILE_COL_LAYOUT *c;
  int i, size;
  bool hn = false, changed = false;

  assert (tl != NULL && tl->finalized && tl->type_cnt == n);

  for (i = 0; i < n; i++)
    {
      if (src[i].is_null)
	{
	  hn = true;
	}
      else if (src[i].val != NULL)
	{
	  changed |= qfile_tuple_resolve_column (tl, i, src[i].val);
	}
    }
  if (changed)
    {
      qfile_type_list_finalize (tl);
    }
  size = tl->data_off[hn ? 1 : 0];

  for (i = 0; i < n; i++)
    {
      if (src[i].is_null)
	{
	  continue;
	}
      c = &tl->col[i];
      if (src[i].val != NULL)
	{
	  src[i].len = qfile_value_body_size (c, src[i].val);
	  if (src[i].len < 0)
	    {
	      return ER_FAILED;
	    }
	}
      if (c->kind == QFILE_COL_FIXED)
	{
	  assert (src[i].len == c->size);
	  size = DB_ALIGN (size, c->alignby) + c->size;
	}
      else
	{
	  size += QFILE_VAR_HDR_SIZE (src[i].len) + src[i].len;
	}
    }

  *has_null = hn;
  return DB_ALIGN (size, QFILE_TUPLE_ALIGNMENT);
}

/*
 * qfile_tuple_put_value () - emit the body of one bound column at p in the encoding of its column (D-180-5/6.3).
 *   return: bytes written (header included for VAR) or ER_FAILED. "written size == computed size" is asserted.
 */
inline int
qfile_tuple_put_value (char *p, const QFILE_COL_LAYOUT * c, const QFILE_TUPLE_COL_SRC * src)
{
  OR_BUF buf;
  int len = src->len, hdr = 0;
  char *body = p;

  if (c->kind == QFILE_COL_VAR)
    {
      hdr = QFILE_VAR_HDR_SIZE (len);
      qfile_var_hdr_encode (p, len);
      body = p + hdr;
    }

  if (src->val == NULL)
    {
      if (len > 0)
	{
	  memcpy (body, src->data, len);
	}
      return hdr + len;
    }

  if (qfile_value_direct (c, src->val))
    {
      const PR_TYPE *t = pr_type_from_id (DB_VALUE_DOMAIN_TYPE (src->val));

      or_init (&buf, body, len);
      if (t->index_writeval (&buf, src->val) != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != len)
	{
	  assert_release (false);	/* written size must equal the computed size (#183) */
	  return ER_FAILED;
	}
      return hdr + len;
    }

  {
    const PR_TYPE *t = pr_type_from_id (DB_VALUE_DOMAIN_TYPE (src->val));
    char stack_buf[QFILE_SCRATCH_STACK + MAX_ALIGNMENT];
    char *aligned;
    int rc;

    if (t == NULL)
      {
	return ER_FAILED;
      }
    if (c->kind == QFILE_COL_FIXED)
      {
	/* the destination is aligned by construction */
	or_init (&buf, body, len);
	rc = t->data_writeval (&buf, src->val);
	if (rc != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != len)
	  {
	    assert_release (false);
	    return ER_FAILED;
	  }
	return len;
      }

    /* VAR/SCRATCH: data_writeval pads relative to the buffer address, so encode into an aligned area and copy */
    aligned = QFILE_SCRATCH_ACQUIRE (stack_buf, len);
    if (aligned == NULL)
      {
	return ER_FAILED;
      }
    or_init (&buf, aligned, len);
    rc = t->data_writeval (&buf, src->val);
    if (rc == NO_ERROR && CAST_BUFLEN (buf.ptr - buf.buffer) == len)
      {
	memcpy (body, aligned, len);
      }
    else
      {
	assert_release (false);
	rc = ER_FAILED;
      }
    QFILE_SCRATCH_RELEASE (aligned, stack_buf);
    return (rc == NO_ERROR) ? hdr + len : ER_FAILED;
  }
}

/*
 * qfile_tuple_fill () - assembler fill pass: write the tuple that qfile_tuple_size () measured.
 *   return: NO_ERROR or ER_FAILED
 *   out(in): destination (list page or private buffer) with at least size bytes; the prev_len word of a backward
 *            capable list is left for the page writer (qfile_add_tuple_to_list_id)
 */
inline int
qfile_tuple_fill (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, const QFILE_TUPLE_COL_SRC * src, int n, char *out,
		  int size, bool has_null)
{
  const QFILE_COL_LAYOUT *c;
  unsigned char *bm = NULL;
  int i, w, off;

  assert (tl != NULL && tl->finalized && tl->type_cnt == n);

  QFILE_PUT_TUPLE_LENGTH (out, size, has_null);
  off = tl->data_off[has_null ? 1 : 0];
#if !defined(NDEBUG)
  memset (out + QFILE_TUPLE_LENGTH_OFFSET + 4, 0, off - 4);	/* prev_len slot, bitmap, pad: deterministic for valgrind */
#endif
  if (has_null)
    {
      bm = (unsigned char *) out + tl->hdr_size;
      memset (bm, 0, tl->bitmap_size);
    }

  for (i = 0; i < n; i++)
    {
      if (src[i].is_null)
	{
	  continue;
	}
      if (bm != NULL)
	{
	  QFILE_BITMAP_SET_BOUND (bm, i);
	}
      c = &tl->col[i];
#if !defined(NDEBUG)
      if (src[i].val != NULL)
	{
	  qfile_tuple_check_col_type (tl, i, src[i].val);
	}
#endif
      if (c->kind == QFILE_COL_FIXED)
	{
	  int aligned_off = DB_ALIGN (off, c->alignby);
#if !defined(NDEBUG)
	  memset (out + off, 0, aligned_off - off);
#endif
	  off = aligned_off;
	}
      w = qfile_tuple_put_value (out + off, c, &src[i]);
      if (w < 0)
	{
	  return ER_FAILED;
	}
      off += w;
    }

  assert ((int) DB_ALIGN (off, QFILE_TUPLE_ALIGNMENT) == size);
#if !defined(NDEBUG)
  memset (out + off, 0, size - off);
#endif
  return NO_ERROR;
}

/*
 * qfile_tuple_size_from_values () / qfile_tuple_fill_from_values () - T_NORMAL overload over the descriptor's
 *   f_valp[] (no source array). The fill pass recomputes each body size (D-190-3: the string size is cached in the
 *   DB_VALUE after the first computation, so this matches the legacy cost).
 */
inline int
qfile_tuple_size_from_values (QFILE_TUPLE_VALUE_TYPE_LIST * tl, DB_VALUE ** vals, int n, bool * has_null)
{
  const QFILE_COL_LAYOUT *c;
  int i, len, size;
  bool hn = false, changed = false;

  assert (tl != NULL && tl->finalized && tl->type_cnt == n);

  for (i = 0; i < n; i++)
    {
      if (DB_IS_NULL (vals[i]))
	{
	  hn = true;
	}
      else
	{
	  changed |= qfile_tuple_resolve_column (tl, i, vals[i]);
	}
    }
  if (changed)
    {
      qfile_type_list_finalize (tl);
    }
  size = tl->data_off[hn ? 1 : 0];

  for (i = 0; i < n; i++)
    {
      if (DB_IS_NULL (vals[i]))
	{
	  continue;
	}
      c = &tl->col[i];
      len = qfile_value_body_size (c, vals[i]);
      if (len < 0)
	{
	  return ER_FAILED;
	}
      if (c->kind == QFILE_COL_FIXED)
	{
	  size = DB_ALIGN (size, c->alignby) + c->size;
	}
      else
	{
	  size += QFILE_VAR_HDR_SIZE (len) + len;
	}
    }

  *has_null = hn;
  return DB_ALIGN (size, QFILE_TUPLE_ALIGNMENT);
}

inline int
qfile_tuple_fill_from_values (const QFILE_TUPLE_VALUE_TYPE_LIST * tl, DB_VALUE ** vals, int n, char *out, int size)
{
  const QFILE_COL_LAYOUT *c;
  QFILE_TUPLE_COL_SRC src;
  unsigned char *bm = NULL;
  int i, w, off;
  bool has_null = false;

  assert (tl != NULL && tl->finalized && tl->type_cnt == n);

  for (i = 0; i < n; i++)
    {
      if (DB_IS_NULL (vals[i]))
	{
	  has_null = true;
	  break;
	}
    }

  QFILE_PUT_TUPLE_LENGTH (out, size, has_null);
  off = tl->data_off[has_null ? 1 : 0];
#if !defined(NDEBUG)
  memset (out + QFILE_TUPLE_LENGTH_OFFSET + 4, 0, off - 4);
#endif
  if (has_null)
    {
      bm = (unsigned char *) out + tl->hdr_size;
      memset (bm, 0, tl->bitmap_size);
    }

  for (i = 0; i < n; i++)
    {
      qfile_col_src_set_value (&src, vals[i]);
      if (src.is_null)
	{
	  continue;
	}
      if (bm != NULL)
	{
	  QFILE_BITMAP_SET_BOUND (bm, i);
	}
      c = &tl->col[i];
      src.len = qfile_value_direct (c, vals[i]) ? pr_index_writeval_disk_size (vals[i])
	: pr_data_writeval_disk_size (vals[i]);
#if !defined(NDEBUG)
      qfile_tuple_check_col_type (tl, i, vals[i]);
#endif
      if (c->kind == QFILE_COL_FIXED)
	{
	  int aligned_off = DB_ALIGN (off, c->alignby);
#if !defined(NDEBUG)
	  memset (out + off, 0, aligned_off - off);
#endif
	  off = aligned_off;
	}
      w = qfile_tuple_put_value (out + off, c, &src);
      if (w < 0)
	{
	  return ER_FAILED;
	}
      off += w;
    }

  assert ((int) DB_ALIGN (off, QFILE_TUPLE_ALIGNMENT) == size);
#if !defined(NDEBUG)
  memset (out + off, 0, size - off);
#endif
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
 * The caller supplies what the descriptor would: the header size of the list and its column count.
 */
typedef struct qfile_tuple_walk QFILE_TUPLE_WALK;
struct qfile_tuple_walk
{
  const char *tpl;		/* tuple start */
  const unsigned char *bitmap;	/* NULL when the tuple has no NULL column */
  int tpl_len;			/* QFILE_GET_TUPLE_LENGTH (tpl) */
  int off;			/* offset of the next value (unaligned) */
  int col;			/* next column */
  int type_cnt;
};

inline void
qfile_tuple_walk_init (QFILE_TUPLE_WALK * walk, const char *tpl, int hdr_size, int type_cnt)
{
  bool has_null = QFILE_GET_TUPLE_HAS_NULL (tpl);

  assert (hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD);
  walk->tpl = tpl;
  walk->tpl_len = QFILE_GET_TUPLE_LENGTH (tpl);
  walk->bitmap = has_null ? QFILE_TUPLE_BITMAP (tpl, hdr_size) : NULL;
  walk->off = DB_ALIGN (hdr_size + (has_null ? ((type_cnt + 7) >> 3) : 0), QFILE_TUPLE_ALIGNMENT);
  walk->col = 0;
  walk->type_cnt = type_cnt;
}

/*
 * qfile_tuple_walk_next () - advance to the next value, laid out as dom dictates.
 *   return: NO_ERROR (bounds are asserted like or_advance does)
 *   body(out): stored body (see qfile_slot_locate); len(out): stored body length; is_null(out): NULL column
 *   c(out): layout of the column, for qfile_col_read_body (); may be NULL
 */
inline int
qfile_tuple_walk_next (QFILE_TUPLE_WALK * walk, const TP_DOMAIN * dom, const char **body, int *len, bool * is_null,
		       QFILE_COL_LAYOUT * c)
{
  QFILE_COL_LAYOUT lc;
  int hdr;

  assert (walk->col < walk->type_cnt);

  if (walk->bitmap != NULL && !QFILE_BITMAP_IS_BOUND (walk->bitmap, walk->col))
    {
      *is_null = true;
      *len = 0;
      *body = walk->tpl + walk->off;
      walk->col++;
      return NO_ERROR;
    }

  qfile_col_layout_of_domain (dom, &lc);
  if (lc.kind == QFILE_COL_FIXED)
    {
      walk->off = DB_ALIGN (walk->off, lc.alignby);
      *body = walk->tpl + walk->off;
      *len = lc.size;
      walk->off += lc.size;
    }
  else
    {
      *len = qfile_var_hdr_decode (walk->tpl + walk->off, &hdr);
      *body = walk->tpl + walk->off + hdr;
      walk->off += hdr + *len;
    }
  assert (walk->off <= walk->tpl_len);

  *is_null = false;
  walk->col++;
  if (c != NULL)
    {
      *c = lc;
    }
  return NO_ERROR;
}

/*
 * qfile_tuple_walk_read_value () - next value decoded into *value with dom (NULL policy stays with the caller).
 */
inline int
qfile_tuple_walk_read_value (QFILE_TUPLE_WALK * walk, const TP_DOMAIN * dom, DB_VALUE * value, bool copy,
			     bool * is_null)
{
  QFILE_COL_LAYOUT c;
  const char *body;
  int len;

  qfile_tuple_walk_next (walk, dom, &body, &len, is_null, &c);
  if (*is_null)
    {
      return NO_ERROR;
    }
  return qfile_col_read_body (&c, body, len, dom, value, copy);
}

#endif /* _QFILE_TUPLE_LAYOUT_H_ */
