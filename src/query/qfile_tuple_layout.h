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

/* qfile_tuple_layout.h - list file tuple slot, accessor and assembler API; shared by server, SA build, cursor. */

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

/* Column classification: FIXED has constant disksize; VAR is DIRECT (string/BIT/NUMERIC) or COMPOSITE (else). */
inline void
qfile_col_layout_of_domain (const TP_DOMAIN * dom, QFILE_COL_LAYOUT * column_layout)
{
  const PR_TYPE *t;
  DB_TYPE id;
  int a;

  assert (dom != NULL && dom->type != NULL);
  t = dom->type;
  id = TP_DOMAIN_TYPE (dom);

  assert (id >= 0 && id <= DB_TYPE_LAST && id <= UINT8_MAX);
  column_layout->byte_offset_in_values = -1;
  column_layout->type_id = (uint8_t) id;

  if (id == DB_TYPE_VARIABLE || t->has_computed_disk_size ())
    {
      column_layout->size = -1;
      column_layout->kind = QFILE_COL_VAR;
      column_layout->value_format = (t->has_index_readval () && id != DB_TYPE_OBJECT && id != DB_TYPE_OID
				     && id != DB_TYPE_VARIABLE) ? QFILE_VALUE_DIRECT : QFILE_VALUE_COMPOSITE;
      /* a COMPOSITE column_data is (de)coded in place by data_readval/data_writeval, which assert INT_ALIGNMENT: it sits at a
       * 4-aligned offset behind a 4-byte length header. A DIRECT column_data (index_* encoding) is unaligned. */
      column_layout->alignby = (column_layout->value_format == QFILE_VALUE_COMPOSITE) ? QFILE_TUPLE_ALIGNMENT : 1;
      return;
    }

  a = MIN (t->alignment, QFILE_TUPLE_ALIGNMENT);	/* BIGINT/DOUBLE align 4 and are read by memcpy */
  if (a < 1)
    {
      a = 1;
    }
  column_layout->size = (int16_t) t->disksize;
  column_layout->kind = QFILE_COL_FIXED;
  column_layout->value_format = 0;
  column_layout->alignby = (uint8_t) a;
}

/* variable value length header */
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
qfile_var_hdr_encode (char *p, int column_data_size, int hdr)
{
  unsigned int w;

  assert (column_data_size >= 0 && (hdr == 4 || (hdr == 1 && column_data_size <= QFILE_VAR_HDR_SHORT_MAX)));
  if (hdr == 1)
    {
      *p = (char) column_data_size;
      return;
    }
  w = htonl ((unsigned int) column_data_size | QFILE_TUPLE_LENGTH_HAS_NULL_BIT);
  memcpy (p, &w, 4);
}

/* length header size of a VAR column column_data: a COMPOSITE column always uses the 4-byte form so that its 4-aligned header
 * keeps the column_data 4-aligned; a DIRECT column uses the short form when it fits */
inline int
qfile_col_var_hdr_size (const QFILE_COL_LAYOUT * column_layout, int column_data_size)
{
  return (column_layout->alignby > 1) ? 4 : QFILE_VAR_HDR_SIZE (column_data_size);
}

/* first NULL column of a has-null tuple, capped at type_cnt */
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

/* Layout descriptor lifetime: alloc allocates the block, copy allocates+inherits, set_layout (re)computes from domp. */
extern int qfile_type_list_alloc (QFILE_TUPLE_VALUE_TYPE_LIST * type_list, int type_cnt, int hdr_size);
extern int qfile_type_list_copy (QFILE_TUPLE_VALUE_TYPE_LIST * dest, const QFILE_TUPLE_VALUE_TYPE_LIST * src);
extern void qfile_set_layout (QFILE_TUPLE_VALUE_TYPE_LIST * type_list);
#if !defined(NDEBUG)
extern bool qfile_type_list_check (const QFILE_TUPLE_VALUE_TYPE_LIST * type_list);
#endif

/*
 * qfile_type_list_is_resolved () - false while any column is still DB_TYPE_VARIABLE (layout not settled yet).
 */
inline bool
qfile_type_list_is_resolved (const QFILE_TUPLE_VALUE_TYPE_LIST * type_list)
{
  int i;

  for (i = 0; i < type_list->type_cnt; i++)
    {
      if (TP_DOMAIN_TYPE (type_list->domp[i]) == DB_TYPE_VARIABLE)
	{
	  return false;
	}
    }
  return true;
}

/*
 * qfile_slot_set_layout () - bind the layout descriptor of the list this record will read tuples from.
 */
inline void
qfile_slot_set_layout (QFILE_TUPLE_RECORD * tuple_slot, const QFILE_TUPLE_VALUE_TYPE_LIST * type_list)
{
  tuple_slot->type_list = type_list;
  tuple_slot->cached_column_index_in_tuple = -1;
}

/*
 * qfile_slot_set_tuple_ptr () - point the record at another tuple and reset the deform cache.
 */
inline void
qfile_slot_set_tuple_ptr (QFILE_TUPLE_RECORD * tuple_slot, char *tuple_ptr)
{
  tuple_slot->tpl = tuple_ptr;
  tuple_slot->cached_column_index_in_tuple = -1;
}

/*
 * qfile_slot_set_tuple_ptr_and_layout () - bind + set_tuple in one step, used when filling a record from a list.
 */
inline void
qfile_slot_set_tuple_ptr_and_layout (QFILE_TUPLE_RECORD * tuple_slot, char *tuple_ptr,
				     const QFILE_TUPLE_VALUE_TYPE_LIST * type_list)
{
  tuple_slot->type_list = type_list;
  qfile_slot_set_tuple_ptr (tuple_slot, tuple_ptr);
}

extern void qfile_slot_clear (QFILE_TUPLE_RECORD * tuple_slot);

/*
 * qfile_prefix_end () - offset (from data_offset) where prefix columns [0, lim) end; lim <= max_fixed_length_col_cnt.
 */
inline int
qfile_prefix_end (const QFILE_TUPLE_VALUE_TYPE_LIST * type_list, int lim)
{
  assert (lim >= 0 && lim <= type_list->max_fixed_length_col_cnt);
  if (lim == 0)
    {
      return 0;
    }
  return type_list->column_layout_array[lim - 1].byte_offset_in_values + type_list->column_layout_array[lim - 1].size;
}

/*
 * qfile_slot_start () - start the position cache for the current tuple.
 */
inline void
qfile_slot_start (QFILE_TUPLE_RECORD * tuple_slot)
{
  const QFILE_TUPLE_VALUE_TYPE_LIST *type_list = tuple_slot->type_list;
  int lim;

  assert (tuple_slot->tpl != NULL && type_list != NULL && type_list->layout_ready);

  lim = type_list->max_fixed_length_col_cnt;

  tuple_slot->has_null = QFILE_GET_TUPLE_HAS_NULL (tuple_slot->tpl);
  tuple_slot->data_offset = type_list->data_offset[tuple_slot->has_null ? 1 : 0];
  if (tuple_slot->has_null)
    {
      int fn = qfile_first_null_col (QFILE_TUPLE_BITMAP (tuple_slot->tpl, type_list->hdr_size), type_list->type_cnt);
      lim = MIN (lim, fn);
    }
  tuple_slot->fixed_length_col_cnt = (int16_t) MIN (lim, INT16_MAX);
  tuple_slot->cached_column_index_in_tuple = tuple_slot->fixed_length_col_cnt;
  tuple_slot->cached_byte_offset_in_tuple = tuple_slot->data_offset + qfile_prefix_end (type_list, lim);
}

/*
 * qfile_slot_get_column_data () - position accessor: address of the stored column_data of the column at column_index.
 *   return: pointer to the stored column_data (meaningful only when !*is_null; a NULL column has no bytes)
 *   column_data_size(out): stored column_data length (FIXED: disksize; VAR: column_data length L without the length header; 0 for NULL)
 *   is_null(out): column is NULL
 */
extern const char *qfile_slot_get_column_data_walk (QFILE_TUPLE_RECORD * tuple_slot, int column_index,
						    int *column_data_size, bool * is_null);
inline const char *qfile_slot_get_column_data (QFILE_TUPLE_RECORD * tuple_slot, int column_index, int *column_data_size,
					       bool * is_null) __attribute__ ((ALWAYS_INLINE));

inline const char *
qfile_slot_get_column_data (QFILE_TUPLE_RECORD * tuple_slot, int column_index, int *column_data_size, bool * is_null)
{
  assert (tuple_slot->tpl != NULL && tuple_slot->type_list != NULL && tuple_slot->type_list->layout_ready);
  assert (column_index >= 0 && column_index < tuple_slot->type_list->type_cnt);

  if (tuple_slot->cached_column_index_in_tuple >= 0 && column_index < tuple_slot->fixed_length_col_cnt)
    {
      const QFILE_COL_LAYOUT *column_layout = &tuple_slot->type_list->column_layout_array[column_index];

      *column_data_size = column_layout->size;
      *is_null = false;
      return tuple_slot->tpl + tuple_slot->data_offset + column_layout->byte_offset_in_values;
    }
  return qfile_slot_get_column_data_walk (tuple_slot, column_index, column_data_size, is_null);
}

/*
 * qfile_col_read_body () - decode a stored column_data with the column's layout kind into *value.
 *   return: NO_ERROR or the readval error
 *   column_layout(in): layout of the column the column_data was stored in
 *   column_data/column_data_size(in): what qfile_slot_get_column_data () / qfile_tuple_walk_next () returned for a bound column
 *   dom(in): decoding domain
 *   copy(in): readval copy flag, honoured for every column kind
 *   A COMPOSITE column_data is decoded where it lies: the layout keeps it 4-aligned (qfile_col_layout_of_domain).
 */
inline bool
qfile_col_body_needs_copy (const QFILE_COL_LAYOUT * column_layout, const TP_DOMAIN * dom)
{
  return column_layout->kind == QFILE_COL_VAR && (column_layout->value_format == QFILE_VALUE_COMPOSITE
						  || !dom->type->has_index_readval ());
}

inline int
qfile_col_read_body (const QFILE_COL_LAYOUT * column_layout, const char *column_data, int column_data_size,
		     const TP_DOMAIN * dom, DB_VALUE * value, bool copy)
{
  OR_BUF buf;

  if (column_layout->kind == QFILE_COL_FIXED)
    {
      or_init (&buf, (char *) column_data, column_data_size);
      return dom->type->data_readval (&buf, value, dom, -1, copy, NULL, 0);
    }
  or_init (&buf, (char *) column_data, column_data_size);
  if (column_layout->value_format == QFILE_VALUE_DIRECT)
    {
      /* index_* encoding, unaligned. A decoding domain without index_readval is a regu the compiler left unresolved
       * (DB_TYPE_NULL: reads nothing, yields NULL; DB_TYPE_VARIABLE): its data_readval is applied as with the old
       * format and as develop did. */
      if (dom->type->has_index_readval ())
	{
	  return dom->type->index_readval (&buf, value, dom, column_data_size, copy, NULL, 0);
	}
      assert (TP_DOMAIN_TYPE (dom) == DB_TYPE_NULL || TP_DOMAIN_TYPE (dom) == DB_TYPE_VARIABLE);
      return dom->type->data_readval (&buf, value, dom, -1, copy, NULL, 0);
    }

  /* VAR/COMPOSITE: 4-aligned in the tuple, decoded in place. The caller's copy flag is honoured as with any other
   * column: copy == false lets the value reference the tuple bytes for as long as the tuple is pinned (a SET stays a
   * disk-set reference instead of being materialized element by element). */
  assert (PTR_ALIGN (column_data, QFILE_TUPLE_ALIGNMENT) == column_data);
  return dom->type->data_readval (&buf, value, dom, -1, copy, NULL, 0);
}

/*
 * qfile_slot_read_column_value () - value accessor: decode the column at column_index into *value with the caller's domain.
 *   return: NO_ERROR or the readval error
 *   dom(in): decoding domain (the one the writer used); the layout comes from the bound descriptor
 *   copy(in): readval copy flag
 *   is_null(out): column is NULL; *value is then left untouched (NULL policy stays with the caller)
 */
inline int
qfile_slot_read_column_value (QFILE_TUPLE_RECORD * tuple_slot, int column_index, const TP_DOMAIN * dom,
			      DB_VALUE * value, bool copy, bool * is_null)
{
  const QFILE_COL_LAYOUT *column_layout;
  const char *column_data;
  int column_data_size;

  column_data = qfile_slot_get_column_data (tuple_slot, column_index, &column_data_size, is_null);
  if (*is_null)
    {
      return NO_ERROR;
    }

  column_layout = &tuple_slot->type_list->column_layout_array[column_index];
  /* probe: decoding domain should match the stored kind, except an unresolved column, the OBJECT/OID pair, and a
   * DB_TYPE_NULL decoding domain (a regu whose domain the compiler left unresolved). */
#if !defined(NDEBUG)
  if (TP_DOMAIN_TYPE (tuple_slot->type_list->domp[column_index]) != DB_TYPE_VARIABLE
      && TP_DOMAIN_TYPE (dom) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (dom) != DB_TYPE_NULL
      && TP_DOMAIN_TYPE (dom) != DB_TYPE_OID && TP_DOMAIN_TYPE (dom) != DB_TYPE_OBJECT)
    {
      QFILE_COL_LAYOUT dc;

      qfile_col_layout_of_domain (dom, &dc);
      /* the decoding domain must read the bytes the column stored: same kind, same fixed width, same var access */
      assert (dc.kind == column_layout->kind
	      && (column_layout->kind == QFILE_COL_VAR ? dc.value_format == column_layout->value_format : dc.size ==
		  column_layout->size));
    }
#endif

  return qfile_col_read_body (column_layout, column_data, column_data_size, dom, value, copy);
}

/*
 * qfile_col_cmpdisk_function () - disk comparator for a column's stored encoding: FIXED/COMPOSITE use data_cmpdisk
 *   (aligned copies), VAR/DIRECT uses index_cmpdisk.
 */
extern
  pr_type::data_cmpdisk_function_type
qfile_col_cmpdisk_function (const QFILE_COL_LAYOUT * column_layout, const TP_DOMAIN * dom);

/*
 * Tuple assembler: two passes over QFILE_TUPLE_COL_SRC[] (or DB_VALUE *[] for T_NORMAL); qfile_tuple_size () measures,
 *   qfile_tuple_fill () writes; type_list is the layout-ready descriptor of the destination list.
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
qfile_col_src_set_raw (QFILE_TUPLE_COL_SRC * src, const char *data, int column_data_size, bool is_null)
{
  src->val = NULL;
  src->data = data;
  src->len = is_null ? 0 : column_data_size;
  src->is_null = is_null;
}

/*
 * qfile_value_direct () - true when a value is written in the index_* encoding (VAR/DIRECT column, matching type).
 */
inline bool
qfile_value_direct (const QFILE_COL_LAYOUT * column_layout, const DB_VALUE * value)
{
  return column_layout->kind == QFILE_COL_VAR && column_layout->value_format == QFILE_VALUE_DIRECT
    && pr_type_from_id (DB_VALUE_DOMAIN_TYPE (value))->has_index_readval ();
}

/*
 * qfile_col_stores_null () - a DB_TYPE_NULL column stores NULL whatever the evaluator produced.
 *   The compiler types an expression over NULL literals as NULL (pt_eval_expr_type: disk_size(NULL) is INT 0 on the
 *   server but its column domain is tp_Null); tp_Null has no encoding (FIXED, size 0), and develop's reader decoded
 *   such a column through tp_Null, i.e. yielded NULL no matter what bytes the writer emitted. The writer keeps that
 *   observable result by storing the column as NULL instead of a column_data the layout cannot describe.
 */
inline bool
qfile_col_stores_null (const QFILE_COL_LAYOUT * column_layout, const DB_VALUE * value)
{
  return DB_IS_NULL (value) || column_layout->type_id == DB_TYPE_NULL;
}

/*
 * qfile_value_body_size () - column_data size of a bound value in the encoding its column stores.
 *   return: size, or ER_FAILED (debug builds only: string longer than its precision)
 */
inline int
qfile_value_body_size (const QFILE_COL_LAYOUT * column_layout, const DB_VALUE * value)
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

  if (qfile_value_direct (column_layout, value))
    {
      val_size = pr_index_writeval_disk_size (v);
    }
  else
    {
      val_size = pr_data_writeval_disk_size (v);
      if (column_layout->kind == QFILE_COL_VAR && column_layout->value_format == QFILE_VALUE_DIRECT)
	{
	  /* a DIRECT column stores the index_* encoding; a value without one would be misread by every reader */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_FAILED;
	}
    }
  if (column_layout->kind == QFILE_COL_FIXED && val_size != column_layout->size)
    {
      /* the format is not self-describing: a value wider or narrower than the column's fixed width would overrun
       * or underrun the tuple, so refuse it in release builds too */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }
  return val_size;
}

#if !defined(NDEBUG)
/*
 * qfile_tuple_check_col_type () - writer-side probe: a value written into the column at column_index must have the column's type
 *   (compatible pairs sharing a layout kind are accepted; an unresolved column accepts anything).
 */
inline void
qfile_tuple_check_col_type (const QFILE_TUPLE_VALUE_TYPE_LIST * type_list, int column_index, const DB_VALUE * val)
{
  DB_TYPE ctype, vtype;

  if (type_list == NULL || !type_list->layout_ready || type_list->domp == NULL || column_index >= type_list->type_cnt)
    {
      return;
    }
  ctype = TP_DOMAIN_TYPE (type_list->domp[column_index]);
  vtype = DB_VALUE_DOMAIN_TYPE (val);
  assert (ctype != DB_TYPE_VARIABLE);	/* resolved by the size pass (qfile_tuple_resolve_column) */
  assert (ctype == vtype || (pr_is_string_type (ctype) && pr_is_string_type (vtype))
	  || ((TP_IS_SET_TYPE (ctype) || ctype == DB_TYPE_VOBJ) && (TP_IS_SET_TYPE (vtype) || vtype == DB_TYPE_VOBJ))
	  || ((ctype == DB_TYPE_OBJECT || ctype == DB_TYPE_OID) && (vtype == DB_TYPE_OBJECT || vtype == DB_TYPE_OID)));
}
#endif

/*
 * qfile_tuple_resolve_column () - resolve an unresolved (DB_TYPE_VARIABLE) column from its first bound value and
 *   recompute the layout.
 *   return: true when the descriptor changed
 */
inline bool
qfile_tuple_resolve_column (QFILE_TUPLE_VALUE_TYPE_LIST * type_list, int column_index, const DB_VALUE * val)
{
  TP_DOMAIN *dom;

  if (TP_DOMAIN_TYPE (type_list->domp[column_index]) != DB_TYPE_VARIABLE || val == NULL || DB_IS_NULL (val))
    {
      return false;
    }
  dom = tp_domain_resolve_value (val, NULL);
  if (dom == NULL || TP_DOMAIN_TYPE (dom) == DB_TYPE_VARIABLE)
    {
      return false;
    }
  type_list->domp[column_index] = dom;
  return true;
}

/*
 * qfile_tuple_size () - assembler size pass.
 *   return: exact tuple length (header included, multiple of 4), or ER_FAILED
 *   type_list(in/out): layout descriptor of the destination list; an unresolved column is resolved from its first bound value
 *   src(in/out): a val source gets its column_data size stored in column_data_size
 *   has_null(out): at least one column is NULL
 */
inline int
qfile_tuple_size (QFILE_TUPLE_VALUE_TYPE_LIST * type_list, QFILE_TUPLE_COL_SRC * src, int n, bool * has_null)
{
  const QFILE_COL_LAYOUT *column_layout;
  int i, size;
  bool hn = false, changed = false;

  assert (type_list != NULL && type_list->layout_ready && type_list->type_cnt == n);

  for (i = 0; i < n; i++)
    {
      if (!src[i].is_null && type_list->column_layout_array[i].type_id == DB_TYPE_NULL)
	{
	  src[i].is_null = true;	/* qfile_col_stores_null (): the fill pass reads is_null too */
	}
      if (src[i].is_null)
	{
	  hn = true;
	}
      else if (src[i].val != NULL)
	{
	  changed |= qfile_tuple_resolve_column (type_list, i, src[i].val);
	}
    }
  if (changed)
    {
      qfile_set_layout (type_list);
    }
  size = type_list->data_offset[hn ? 1 : 0];

  for (i = 0; i < n; i++)
    {
      if (src[i].is_null)
	{
	  continue;
	}
      column_layout = &type_list->column_layout_array[i];
      if (src[i].val != NULL)
	{
	  src[i].len = qfile_value_body_size (column_layout, src[i].val);
	  if (src[i].len < 0)
	    {
	      return ER_FAILED;
	    }
	}
      if (column_layout->kind == QFILE_COL_FIXED)
	{
	  assert (src[i].len == column_layout->size);
	  size = DB_ALIGN (size, column_layout->alignby) + column_layout->size;
	}
      else
	{
	  size =
	    DB_ALIGN (size, column_layout->alignby) + qfile_col_var_hdr_size (column_layout, src[i].len) + src[i].len;
	}
    }

  *has_null = hn;
  return DB_ALIGN (size, QFILE_TUPLE_ALIGNMENT);
}

/*
 * qfile_value_pr_type () - PR_TYPE of a bound value via the static id map (avoids the call/range-test of
 *   pr_type_from_id () and the domain-chain lookup).
 */
inline const PR_TYPE *
qfile_value_pr_type (const DB_VALUE * val)
{
  DB_TYPE vt = DB_VALUE_DOMAIN_TYPE (val);

  assert (vt >= 0 && vt <= DB_TYPE_LAST && vt != DB_TYPE_TABLE);
  return tp_Type_id_map[(int) vt];
}

/*
 * qfile_tuple_put_value () - emit the column_data of one bound column at p in the encoding of its column.
 *   return: bytes written (header included for VAR) or ER_FAILED. "written size == computed size" is asserted.
 *   t(in): PR_TYPE of src->val (qfile_value_pr_type); ignored for a raw source (val == NULL)
 */
inline int
qfile_tuple_put_value (char *p, const QFILE_COL_LAYOUT * column_layout, const QFILE_TUPLE_COL_SRC * src,
		       const PR_TYPE * t)
{
  OR_BUF buf;
  int column_data_size = src->len, hdr = 0;
  char *column_data = p;

  if (column_layout->kind == QFILE_COL_VAR)
    {
      hdr = qfile_col_var_hdr_size (column_layout, column_data_size);
      qfile_var_hdr_encode (p, column_data_size, hdr);
      column_data = p + hdr;
    }

  if (src->val == NULL)
    {
      if (column_data_size > 0)
	{
	  memcpy (column_data, src->data, column_data_size);
	}
      return hdr + column_data_size;
    }

  assert (t != NULL && t == pr_type_from_id (DB_VALUE_DOMAIN_TYPE (src->val)));
  if (t == NULL)
    {
      return ER_FAILED;
    }

  if (column_layout->kind == QFILE_COL_VAR && column_layout->value_format == QFILE_VALUE_DIRECT
      && t->has_index_readval ())
    {
      assert (qfile_value_direct (column_layout, src->val));
      or_init (&buf, column_data, column_data_size);
      if (t->index_writeval (&buf, src->val) != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != column_data_size)
	{
	  assert_release (false);	/* written size must equal the computed size */
	  return ER_FAILED;
	}
      return hdr + column_data_size;
    }

  /* FIXED and VAR/COMPOSITE: the destination is aligned by construction (data_writeval pads relative to the buffer
   * address), so the value is encoded in place */
  assert (PTR_ALIGN (column_data, column_layout->alignby) == column_data);
  or_init (&buf, column_data, column_data_size);
  if (t->data_writeval (&buf, src->val) != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != column_data_size)
    {
      assert_release (false);
      return ER_FAILED;
    }
  return hdr + column_data_size;
}

/*
 * qfile_tuple_fill () - assembler fill pass: write the tuple that qfile_tuple_size () measured.
 *   return: NO_ERROR or ER_FAILED
 *   out(in): destination (list page or private buffer, >= size bytes); prev_len word of a backward list is left for
 *            the page writer
 */
inline int
qfile_tuple_fill (const QFILE_TUPLE_VALUE_TYPE_LIST * type_list, const QFILE_TUPLE_COL_SRC * src, int n, char *out,
		  int size, bool has_null)
{
  const QFILE_COL_LAYOUT *column_layout;
  unsigned char *bm = NULL;
  int i, w, off;

  assert (type_list != NULL && type_list->layout_ready && type_list->type_cnt == n);

  QFILE_PUT_TUPLE_LENGTH (out, size, has_null);
  off = type_list->data_offset[has_null ? 1 : 0];
#if !defined(NDEBUG)
  memset (out + QFILE_TUPLE_LENGTH_OFFSET + 4, 0, off - 4);	/* prev_len slot, bitmap, pad: deterministic for valgrind */
#endif
  if (has_null)
    {
      bm = (unsigned char *) out + type_list->hdr_size;
      memset (bm, 0, type_list->bitmap_size);
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
      column_layout = &type_list->column_layout_array[i];
#if !defined(NDEBUG)
      if (src[i].val != NULL)
	{
	  qfile_tuple_check_col_type (type_list, i, src[i].val);
	}
#endif
      {
	int aligned_off = DB_ALIGN (off, column_layout->alignby);	/* FIXED / COMPOSITE bodies are aligned; DIRECT (alignby 1) is not */
#if !defined(NDEBUG)
	memset (out + off, 0, aligned_off - off);
#endif
	off = aligned_off;
      }
      w =
	qfile_tuple_put_value (out + off, column_layout, &src[i],
			       (src[i].val != NULL) ? qfile_value_pr_type (src[i].val) : NULL);
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
 * qfile_tuple_size_from_values () / qfile_tuple_fill_from_values () - T_NORMAL overload over f_valp[]; the size pass
 *   records each column_data length in lens[] so the fill pass need not recompute it; a value matching its column's type is
 *   measured directly, others go through qfile_value_body_size ().
 */
inline int
qfile_tuple_size_from_values (QFILE_TUPLE_VALUE_TYPE_LIST * type_list, DB_VALUE ** vals, int *lens, int n,
			      bool * has_null)
{
  const QFILE_COL_LAYOUT *column_layout;
  const DB_VALUE *v;
  DB_TYPE vt;
  int i, column_data_size, values_size;
  bool hn;

  assert (type_list != NULL && type_list->layout_ready && type_list->type_cnt == n);
  assert (lens != NULL || n == 0);	/* a zero-column list never allocates f_valp/f_len (INSERT ... SELECT inner block) */
  assert (type_list->data_offset[0] % QFILE_TUPLE_ALIGNMENT == 0
	  && type_list->data_offset[1] % QFILE_TUPLE_ALIGNMENT == 0);

restart:
  /* one pass: values_size summed from 0, data_offset added at the end (has-null verdict is only known after the pass) */
  values_size = 0;
  hn = false;
  for (i = 0; i < n; i++)
    {
      v = vals[i];
      column_layout = &type_list->column_layout_array[i];
      if (qfile_col_stores_null (column_layout, v))
	{
	  hn = true;
	  lens[i] = 0;
	  continue;
	}
      vt = DB_VALUE_DOMAIN_TYPE (v);
      if (vt == (DB_TYPE) column_layout->type_id)
	{
	  /* the value has the column's type: the column entry is the encoding (kind/value_format were derived from it) */
	  if (column_layout->kind == QFILE_COL_FIXED)
	    {
	      column_data_size = column_layout->size;
	    }
	  else if (column_layout->value_format == QFILE_VALUE_DIRECT)
	    {
	      column_data_size = qfile_value_pr_type (v)->get_index_size_of_value (v);
	    }
	  else
	    {
	      column_data_size = qfile_value_pr_type (v)->get_disk_size_of_value (v);
	    }
	  assert (column_data_size == qfile_value_body_size (column_layout, v));
	}
      else
	{
	  if (column_layout->type_id == DB_TYPE_VARIABLE && qfile_tuple_resolve_column (type_list, i, v))
	    {
	      /* late resolution: the layout changed under us; measure again with the settled descriptor */
	      qfile_set_layout (type_list);
	      goto restart;
	    }
	  column_data_size = qfile_value_body_size (column_layout, v);
	  if (column_data_size < 0)
	    {
	      return ER_FAILED;
	    }
	}
      lens[i] = column_data_size;
      if (column_layout->kind == QFILE_COL_FIXED)
	{
	  values_size = DB_ALIGN (values_size, column_layout->alignby) + column_layout->size;
	}
      else
	{
	  values_size =
	    DB_ALIGN (values_size, column_layout->alignby) + qfile_col_var_hdr_size (column_layout,
										     column_data_size) +
	    column_data_size;
	}
    }

  *has_null = hn;
  return DB_ALIGN (type_list->data_offset[hn ? 1 : 0] + values_size, QFILE_TUPLE_ALIGNMENT);
}

inline int
qfile_tuple_fill_from_values (const QFILE_TUPLE_VALUE_TYPE_LIST * type_list, DB_VALUE ** vals, const int *lens, int n,
			      char *out, int size, bool has_null)
{
  const QFILE_COL_LAYOUT *column_layout;
  QFILE_TUPLE_COL_SRC src;
  unsigned char *bm = NULL;
  int i, w, off;

  assert (type_list != NULL && type_list->layout_ready && type_list->type_cnt == n);
  assert (lens != NULL || n == 0);

  QFILE_PUT_TUPLE_LENGTH (out, size, has_null);
  off = type_list->data_offset[has_null ? 1 : 0];
#if !defined(NDEBUG)
  memset (out + QFILE_TUPLE_LENGTH_OFFSET + 4, 0, off - 4);
#endif
  if (has_null)
    {
      bm = (unsigned char *) out + type_list->hdr_size;
      memset (bm, 0, type_list->bitmap_size);
    }

  for (i = 0; i < n; i++)
    {
      column_layout = &type_list->column_layout_array[i];
      if (qfile_col_stores_null (column_layout, vals[i]))
	{
	  assert (has_null);
	  continue;
	}
      if (bm != NULL)
	{
	  QFILE_BITMAP_SET_BOUND (bm, i);
	}
      src.val = vals[i];
      src.data = NULL;
      src.len = lens[i];
      src.is_null = false;
#if !defined(NDEBUG)
      qfile_tuple_check_col_type (type_list, i, vals[i]);
      assert (src.len == (qfile_value_direct (column_layout, vals[i]) ? pr_index_writeval_disk_size (vals[i])
			  : pr_data_writeval_disk_size (vals[i])));
#endif
      {
	int aligned_off = DB_ALIGN (off, column_layout->alignby);
#if !defined(NDEBUG)
	memset (out + off, 0, aligned_off - off);
#endif
	off = aligned_off;
      }
      w = qfile_tuple_put_value (out + off, column_layout, &src, qfile_value_pr_type (vals[i]));
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
 * qfile_slot_overwrite_value () - in-place rewrite of a bound, same-encoding-size column.
 *   return: NO_ERROR, ER_FAILED when the in-place contract is violated (asserted in debug)
 */
extern int qfile_slot_overwrite_value (QFILE_TUPLE_RECORD * tuple_slot, int column_index, const TP_DOMAIN * dom,
				       const DB_VALUE * value);

/*
 * Domain-driven sequential walk: for readers that cannot bind a descriptor (px XASL_SNAPSHOT reader) or that own
 *   the column domains themselves (aggregate hash entry (de)serialization); the caller supplies header size and
 *   column count instead.
 */
typedef struct qfile_tuple_walk QFILE_TUPLE_WALK;
struct qfile_tuple_walk
{
  const char *tuple_ptr;	/* tuple start */
  const unsigned char *bitmap;	/* NULL when the tuple has no NULL column */
  int tpl_len;			/* QFILE_GET_TUPLE_LENGTH (tuple_ptr) */
  int next_byte_offset_in_tuple;	/* next value offset from tuple start, before alignment */
  int next_column_index_in_tuple;	/* next column */
  int type_cnt;
};

inline void
qfile_tuple_walk_init (QFILE_TUPLE_WALK * walk, const char *tuple_ptr, int hdr_size, int type_cnt)
{
  bool has_null = QFILE_GET_TUPLE_HAS_NULL (tuple_ptr);

  assert (hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD);
  walk->tuple_ptr = tuple_ptr;
  walk->tpl_len = QFILE_GET_TUPLE_LENGTH (tuple_ptr);
  walk->bitmap = has_null ? QFILE_TUPLE_BITMAP (tuple_ptr, hdr_size) : NULL;
  walk->next_byte_offset_in_tuple = DB_ALIGN (hdr_size + (has_null ? ((type_cnt + 7) >> 3) : 0), QFILE_TUPLE_ALIGNMENT);
  walk->next_column_index_in_tuple = 0;
  walk->type_cnt = type_cnt;
}

/*
 * qfile_tuple_walk_next () - advance to the next value, laid out as dom dictates.
 *   return: NO_ERROR (bounds are asserted like or_advance does)
 *   column_data(out): stored column_data (see qfile_slot_get_column_data); column_data_size(out): stored column_data length; is_null(out): NULL column
 *   column_layout(out): layout of the column, for qfile_col_read_body (); may be NULL
 */
inline int
qfile_tuple_walk_next (QFILE_TUPLE_WALK * walk, const TP_DOMAIN * dom, const char **column_data, int *column_data_size,
		       bool * is_null, QFILE_COL_LAYOUT * column_layout)
{
  QFILE_COL_LAYOUT lc;
  int hdr;

  assert (walk->next_column_index_in_tuple < walk->type_cnt);

  if (walk->bitmap != NULL && !QFILE_BITMAP_IS_BOUND (walk->bitmap, walk->next_column_index_in_tuple))
    {
      *is_null = true;
      *column_data_size = 0;
      *column_data = walk->tuple_ptr + walk->next_byte_offset_in_tuple;
      walk->next_column_index_in_tuple++;
      return NO_ERROR;
    }

  qfile_col_layout_of_domain (dom, &lc);
  walk->next_byte_offset_in_tuple = DB_ALIGN (walk->next_byte_offset_in_tuple, lc.alignby);
  if (lc.kind == QFILE_COL_FIXED)
    {
      *column_data = walk->tuple_ptr + walk->next_byte_offset_in_tuple;
      *column_data_size = lc.size;
      walk->next_byte_offset_in_tuple += lc.size;
    }
  else
    {
      *column_data_size = qfile_var_hdr_decode (walk->tuple_ptr + walk->next_byte_offset_in_tuple, &hdr);
      *column_data = walk->tuple_ptr + walk->next_byte_offset_in_tuple + hdr;
      walk->next_byte_offset_in_tuple += hdr + *column_data_size;
    }
  assert (walk->next_byte_offset_in_tuple <= walk->tpl_len);

  *is_null = false;
  walk->next_column_index_in_tuple++;
  if (column_layout != NULL)
    {
      *column_layout = lc;
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
  QFILE_COL_LAYOUT column_layout;
  const char *column_data;
  int column_data_size;

  qfile_tuple_walk_next (walk, dom, &column_data, &column_data_size, is_null, &column_layout);
  if (*is_null)
    {
      return NO_ERROR;
    }
  return qfile_col_read_body (&column_layout, column_data, column_data_size, dom, value, copy);
}

#endif /* _QFILE_TUPLE_LAYOUT_H_ */
