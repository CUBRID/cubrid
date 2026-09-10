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
 * qfile_tuple_layout.c - list file tuple slot & accessor API: layout allocation/computation/debug-check, in-place overwrite,
 *   slot teardown (hot accessors are inline in the header).
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "qfile_tuple_layout.h"
#include "memory_alloc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "error_manager.h"
#include "dbtype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * qfile_type_list_compute () - pure layout computation: fills column_layout_array[type_cnt] and the list-level fields from
 *   (domp, type_cnt, hdr_size); idempotent.
 */
static void
qfile_type_list_compute (TP_DOMAIN ** domp, int type_cnt, int hdr_size, QFILE_COL_LAYOUT * column_layout_array,
			 int *max_fixed_length_col_cnt, int16_t data_offset[2], int16_t * bitmap_size)
{
  int i, off = 0;

  *bitmap_size = (int16_t) ((type_cnt + 7) >> 3);
  data_offset[0] = (int16_t) DB_ALIGN (hdr_size, QFILE_TUPLE_ALIGNMENT);
  data_offset[1] = (int16_t) DB_ALIGN (hdr_size + *bitmap_size, QFILE_TUPLE_ALIGNMENT);
  *max_fixed_length_col_cnt = type_cnt;

  for (i = 0; i < type_cnt; i++)
    {
      qfile_col_layout_of_domain (domp[i], &column_layout_array[i]);

      if (*max_fixed_length_col_cnt != type_cnt)
	{
	  continue;		/* past the constant prefix: off stays -1 */
	}

      if (column_layout_array[i].kind == QFILE_COL_VAR)
	{
	  *max_fixed_length_col_cnt = i;
	  continue;
	}

      off = DB_ALIGN (off, column_layout_array[i].alignby);
      if (off > INT16_MAX)
	{
	  /* give up the constant-offset cache from here on; correctness is unaffected */
	  *max_fixed_length_col_cnt = i;
	  continue;
	}
      column_layout_array[i].byte_offset_in_values = (int16_t) off;
      off += column_layout_array[i].size;
    }
}

/*
 * qfile_type_list_alloc () - allocate the [domp[type_cnt] | column_layout_array[type_cnt]] block of a list's type list.
 *   return: NO_ERROR or ER_FAILED (out of memory)
 */
int
qfile_type_list_alloc (QFILE_TUPLE_VALUE_TYPE_LIST * type_list, int type_cnt, int hdr_size)
{
  assert (type_cnt >= 0);
  assert (hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD);

  type_list->type_cnt = type_cnt;
  type_list->domp = NULL;
  type_list->column_layout_array = NULL;
  type_list->max_fixed_length_col_cnt = 0;
  type_list->data_offset[0] = 0;
  type_list->data_offset[1] = 0;
  type_list->bitmap_size = 0;
  type_list->hdr_size = (uint8_t) hdr_size;
  type_list->layout_ready = false;

  if (type_cnt > 0)
    {
      type_list->domp = (TP_DOMAIN **) malloc (type_cnt * (sizeof (TP_DOMAIN *) + sizeof (QFILE_COL_LAYOUT)));
      if (type_list->domp == NULL)
	{
	  return ER_FAILED;
	}
      type_list->column_layout_array = (QFILE_COL_LAYOUT *) (type_list->domp + type_cnt);
    }

  return NO_ERROR;
}

/*
 * qfile_type_list_copy () - allocate dest and inherit src (block memcpy when src is layout-ready; a not layout-ready src
 *   only contributes its domains).
 *   return: NO_ERROR or ER_FAILED
 */
int
qfile_type_list_copy (QFILE_TUPLE_VALUE_TYPE_LIST * dest, const QFILE_TUPLE_VALUE_TYPE_LIST * src)
{
  int hdr_size = src->hdr_size;

  if (hdr_size != QFILE_TUPLE_HDR_SIZE_FORWARD && hdr_size != QFILE_TUPLE_HDR_SIZE_BACKWARD)
    {
      /* a list id never opened (hdr_size 0) or an INPUT type list holds no tuples, so the header is immaterial;
       * qfile_open_list () decides it once the list actually exists */
      assert (!src->layout_ready);
      hdr_size = QFILE_TUPLE_HDR_SIZE_FORWARD;
    }

  if (qfile_type_list_alloc (dest, src->type_cnt, hdr_size) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (src->type_cnt > 0)
    {
      if (src->layout_ready)
	{
	  memcpy (dest->domp, src->domp, src->type_cnt * (sizeof (TP_DOMAIN *) + sizeof (QFILE_COL_LAYOUT)));
	}
      else
	{
	  memcpy (dest->domp, src->domp, src->type_cnt * sizeof (TP_DOMAIN *));
	}
    }

  dest->max_fixed_length_col_cnt = src->max_fixed_length_col_cnt;
  dest->data_offset[0] = src->data_offset[0];
  dest->data_offset[1] = src->data_offset[1];
  dest->bitmap_size = src->bitmap_size;
  dest->layout_ready = src->layout_ready;

  return NO_ERROR;
}

/*
 * qfile_set_layout () - (re)compute the layout descriptor from domp; must run after the last domp mutation
 *   of a list (qfile_open_list, qfile_modify_type_list, qfile_update_domains_on_type_list, and other domain-fixing
 *   callers).
 */
void
qfile_set_layout (QFILE_TUPLE_VALUE_TYPE_LIST * type_list)
{
  assert (type_list->hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || type_list->hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD);

  if (type_list->type_cnt > 0)
    {
      assert (type_list->domp != NULL);
      assert (type_list->column_layout_array == (QFILE_COL_LAYOUT *) (type_list->domp + type_list->type_cnt));
      qfile_type_list_compute (type_list->domp, type_list->type_cnt, type_list->hdr_size,
			       type_list->column_layout_array, &type_list->max_fixed_length_col_cnt,
			       type_list->data_offset, &type_list->bitmap_size);
    }
  else
    {
      type_list->max_fixed_length_col_cnt = 0;
      type_list->bitmap_size = 0;
      type_list->data_offset[0] = (int16_t) DB_ALIGN (type_list->hdr_size, QFILE_TUPLE_ALIGNMENT);
      type_list->data_offset[1] = type_list->data_offset[0];
    }

  type_list->layout_ready = true;
}

#if !defined(NDEBUG)
/*
 * qfile_type_list_check () - debug cross-check: stored descriptor == recomputation from domp.
 *   return: true when consistent; false means a domp mutation without qfile_set_layout ()
 */
bool
qfile_type_list_check (const QFILE_TUPLE_VALUE_TYPE_LIST * type_list)
{
  QFILE_COL_LAYOUT *column_layout_array;
  int max_fixed_length_col_cnt;
  int16_t data_offset[2], bitmap_size;
  bool ok;

  if (type_list->type_cnt <= 0)
    {
      /* an empty list has nothing to lay out and may never have had its layout set; no column can be read from it anyway */
      return type_list->domp == NULL && type_list->column_layout_array == NULL;
    }
  if (!type_list->layout_ready
      || !(type_list->hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || type_list->hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD))
    {
      return false;
    }
  if (type_list->column_layout_array != (QFILE_COL_LAYOUT *) (type_list->domp + type_list->type_cnt))
    {
      return false;
    }

  column_layout_array = (QFILE_COL_LAYOUT *) malloc (type_list->type_cnt * sizeof (QFILE_COL_LAYOUT));
  if (column_layout_array == NULL)
    {
      return true;		/* cannot check; do not fail the caller for that */
    }

  qfile_type_list_compute (type_list->domp, type_list->type_cnt, type_list->hdr_size, column_layout_array,
			   &max_fixed_length_col_cnt, data_offset, &bitmap_size);

  ok =
    (memcmp (column_layout_array, type_list->column_layout_array, type_list->type_cnt * sizeof (QFILE_COL_LAYOUT)) == 0
     && max_fixed_length_col_cnt == type_list->max_fixed_length_col_cnt && data_offset[0] == type_list->data_offset[0]
     && data_offset[1] == type_list->data_offset[1] && bitmap_size == type_list->bitmap_size);

  free (column_layout_array);
  return ok;
}
#endif /* !NDEBUG */

/*
 * qfile_slot_get_column_data_walk () - out-of-line half of qfile_slot_get_column_data (): starts the cache if needed, then walks from
 *   the cached position to the column at column_index.
 */
const char *
qfile_slot_get_column_data_walk (QFILE_TUPLE_RECORD * tuple_slot, int column_index, int *column_data_size,
				 bool * is_null)
{
  const QFILE_TUPLE_VALUE_TYPE_LIST *type_list = tuple_slot->type_list;
  const QFILE_COL_LAYOUT *column_layout;
  const unsigned char *bm;
  const char *tuple_ptr = tuple_slot->tpl;
  int i, off, hdr, var_data_size;

  if (tuple_slot->cached_column_index_in_tuple < 0)
    {
      qfile_slot_start (tuple_slot);
    }

  if (column_index < tuple_slot->fixed_length_col_cnt)
    {
      column_layout = &type_list->column_layout_array[column_index];
      *column_data_size = column_layout->size;
      *is_null = false;
      return tuple_ptr + tuple_slot->data_offset + column_layout->byte_offset_in_values;
    }

  if (column_index >= tuple_slot->cached_column_index_in_tuple)
    {
      i = tuple_slot->cached_column_index_in_tuple;
      off = tuple_slot->cached_byte_offset_in_tuple;
    }
  else
    {
      i = tuple_slot->fixed_length_col_cnt;
      off = tuple_slot->data_offset + qfile_prefix_end (type_list, i);
    }

  bm = tuple_slot->has_null ? QFILE_TUPLE_BITMAP (tuple_ptr, type_list->hdr_size) : NULL;

  for (; i < column_index; i++)
    {
      if (bm != NULL && !QFILE_BITMAP_IS_BOUND (bm, i))
	{
	  continue;
	}
      column_layout = &type_list->column_layout_array[i];
      off = DB_ALIGN (off, column_layout->alignby);
      if (column_layout->kind == QFILE_COL_FIXED)
	{
	  off += column_layout->size;
	}
      else
	{
	  /* the format is not self-describing: a length header read out of the tuple is the only thing that keeps the
	   * walk inside it, so check the invariant here as qfile_tuple_walk_next () does (debug only, SER-02) */
	  assert (off < QFILE_GET_TUPLE_LENGTH (tuple_ptr));
	  var_data_size = qfile_var_hdr_decode (tuple_ptr + off, &hdr);
	  off += hdr + var_data_size;
	}
      assert (off <= QFILE_GET_TUPLE_LENGTH (tuple_ptr));
    }

  if (column_index <= INT16_MAX)
    {
      tuple_slot->cached_column_index_in_tuple = (int16_t) column_index;
      tuple_slot->cached_byte_offset_in_tuple = off;
    }

  if (bm != NULL && !QFILE_BITMAP_IS_BOUND (bm, column_index))
    {
      *column_data_size = 0;
      *is_null = true;
      return tuple_ptr + off;
    }

  column_layout = &type_list->column_layout_array[column_index];
  off = DB_ALIGN (off, column_layout->alignby);
  if (column_layout->kind == QFILE_COL_FIXED)
    {
      assert (off + column_layout->size <= QFILE_GET_TUPLE_LENGTH (tuple_ptr));
      *column_data_size = column_layout->size;
      *is_null = false;
      return tuple_ptr + off;
    }

  assert (off < QFILE_GET_TUPLE_LENGTH (tuple_ptr));
  var_data_size = qfile_var_hdr_decode (tuple_ptr + off, &hdr);
  assert (off + hdr + var_data_size <= QFILE_GET_TUPLE_LENGTH (tuple_ptr));
  *column_data_size = var_data_size;
  *is_null = false;
  return tuple_ptr + off + hdr;
}

/*
 * qfile_slot_clear () - unbind the descriptor when the scan/cursor closes; does not touch tuple_slot->tpl / tuple_slot->size
 *   (still freed by the record owner).
 */
void
qfile_slot_clear (QFILE_TUPLE_RECORD * tuple_slot)
{
  tuple_slot->type_list = NULL;
  tuple_slot->cached_column_index_in_tuple = -1;
}

/*
 * qfile_slot_overwrite_value () - in-place rewrite of the column at column_index with value.
 *   return: NO_ERROR or ER_FAILED
 */
int
qfile_slot_overwrite_value (QFILE_TUPLE_RECORD * tuple_slot, int column_index, const TP_DOMAIN * dom,
			    const DB_VALUE * value)
{
  OR_BUF buf;
  const QFILE_COL_LAYOUT *column_layout;
  const char *column_data;
  const PR_TYPE *t;
  int column_data_size, new_len;
  bool is_null;

  column_data = qfile_slot_get_column_data (tuple_slot, column_index, &column_data_size, &is_null);

  if (value == NULL || DB_IS_NULL (value) || is_null || dom == NULL || dom->type == NULL
      || TP_DOMAIN_TYPE (dom) != DB_VALUE_DOMAIN_TYPE (value))
    {
      assert (false);
      return ER_FAILED;
    }
  column_layout = &tuple_slot->type_list->column_layout_array[column_index];
  t = pr_type_from_id (DB_VALUE_DOMAIN_TYPE (value));
  new_len = qfile_value_direct (column_layout, value) ? pr_index_writeval_disk_size ((DB_VALUE *) value)
    : pr_data_writeval_disk_size ((DB_VALUE *) value);
  if (new_len != column_data_size)
    {
      assert (false);
      return ER_FAILED;
    }
  /* the column's own domain must agree with the decoding domain unless it is still unresolved */
  assert (TP_DOMAIN_TYPE (tuple_slot->type_list->domp[column_index]) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_TYPE (tuple_slot->type_list->domp[column_index]) == TP_DOMAIN_TYPE (dom));

  if (column_layout->kind == QFILE_COL_FIXED)
    {
      or_init (&buf, (char *) column_data, column_data_size);
      return (t->data_writeval (&buf, value) == NO_ERROR) ? NO_ERROR : ER_FAILED;
    }
  if (qfile_value_direct (column_layout, value))
    {
      or_init (&buf, (char *) column_data, column_data_size);
      if (t->index_writeval (&buf, value) != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != column_data_size)
	{
	  assert (false);
	  return ER_FAILED;
	}
      return NO_ERROR;
    }

  /* VAR/COMPOSITE: the column_data is 4-aligned in the tuple, overwrite it in place */
  assert (PTR_ALIGN (column_data, QFILE_TUPLE_ALIGNMENT) == column_data);
  or_init (&buf, (char *) column_data, column_data_size);
  if (t->data_writeval (&buf, value) != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != column_data_size)
    {
      assert (false);
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * qfile_col_cmpdisk_function () - disk comparator matching a column's stored encoding
 */
pr_type::data_cmpdisk_function_type
qfile_col_cmpdisk_function (const QFILE_COL_LAYOUT * column_layout, const TP_DOMAIN * dom)
{
  if (column_layout->kind == QFILE_COL_VAR && column_layout->value_format == QFILE_VALUE_DIRECT
      && dom->type->has_index_readval ())
    {
      return dom->type->get_index_cmpdisk_function ();
    }
  return dom->type->get_data_cmpdisk_function ();
}
