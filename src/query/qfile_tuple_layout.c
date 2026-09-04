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
 * qfile_tuple_layout.c - list file tuple slot & accessor API: layout alloc/finalize/debug-check, in-place overwrite,
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
 * qfile_type_list_compute () - pure layout computation: fills col[type_cnt] and the list-level fields from
 *   (domp, type_cnt, hdr_size); idempotent.
 */
static void
qfile_type_list_compute (TP_DOMAIN ** domp, int type_cnt, int hdr_size, QFILE_COL_LAYOUT * col,
			 int *first_non_cached_col, int16_t data_off[2], int16_t * bitmap_size)
{
  int i, off = 0;

  *bitmap_size = (int16_t) ((type_cnt + 7) >> 3);
  data_off[0] = (int16_t) DB_ALIGN (hdr_size, QFILE_TUPLE_ALIGNMENT);
  data_off[1] = (int16_t) DB_ALIGN (hdr_size + *bitmap_size, QFILE_TUPLE_ALIGNMENT);
  *first_non_cached_col = type_cnt;

  for (i = 0; i < type_cnt; i++)
    {
      qfile_col_layout_of_domain (domp[i], &col[i]);

      if (*first_non_cached_col != type_cnt)
	{
	  continue;		/* past the constant prefix: off stays -1 */
	}

      if (col[i].kind == QFILE_COL_VAR)
	{
	  *first_non_cached_col = i;
	  continue;
	}

      off = DB_ALIGN (off, col[i].alignby);
      if (off > INT16_MAX)
	{
	  /* give up the constant-offset cache from here on; correctness is unaffected */
	  *first_non_cached_col = i;
	  continue;
	}
      col[i].off = (int16_t) off;
      off += col[i].size;
    }
}

/*
 * qfile_type_list_alloc () - allocate the [domp[type_cnt] | col[type_cnt]] block of a list's type list.
 *   return: NO_ERROR or ER_FAILED (out of memory)
 */
int
qfile_type_list_alloc (QFILE_TUPLE_VALUE_TYPE_LIST * tl, int type_cnt, int hdr_size)
{
  assert (type_cnt >= 0);
  assert (hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD);

  tl->type_cnt = type_cnt;
  tl->domp = NULL;
  tl->col = NULL;
  tl->first_non_cached_col = 0;
  tl->data_off[0] = 0;
  tl->data_off[1] = 0;
  tl->bitmap_size = 0;
  tl->hdr_size = (uint8_t) hdr_size;
  tl->finalized = false;

  if (type_cnt > 0)
    {
      tl->domp = (TP_DOMAIN **) malloc (type_cnt * (sizeof (TP_DOMAIN *) + sizeof (QFILE_COL_LAYOUT)));
      if (tl->domp == NULL)
	{
	  return ER_FAILED;
	}
      tl->col = (QFILE_COL_LAYOUT *) (tl->domp + type_cnt);
    }

  return NO_ERROR;
}

/*
 * qfile_type_list_copy () - allocate dest and inherit src (block memcpy when src is finalized; a non-finalized src
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
      assert (!src->finalized);
      hdr_size = QFILE_TUPLE_HDR_SIZE_FORWARD;
    }

  if (qfile_type_list_alloc (dest, src->type_cnt, hdr_size) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (src->type_cnt > 0)
    {
      if (src->finalized)
	{
	  memcpy (dest->domp, src->domp, src->type_cnt * (sizeof (TP_DOMAIN *) + sizeof (QFILE_COL_LAYOUT)));
	}
      else
	{
	  memcpy (dest->domp, src->domp, src->type_cnt * sizeof (TP_DOMAIN *));
	}
    }

  dest->first_non_cached_col = src->first_non_cached_col;
  dest->data_off[0] = src->data_off[0];
  dest->data_off[1] = src->data_off[1];
  dest->bitmap_size = src->bitmap_size;
  dest->finalized = src->finalized;

  return NO_ERROR;
}

/*
 * qfile_type_list_finalize () - (re)compute the layout descriptor from domp; must run after the last domp mutation
 *   of a list (qfile_open_list, qfile_modify_type_list, qfile_update_domains_on_type_list, and other domain-fixing
 *   callers).
 */
void
qfile_type_list_finalize (QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  assert (tl->hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || tl->hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD);

  if (tl->type_cnt > 0)
    {
      assert (tl->domp != NULL);
      assert (tl->col == (QFILE_COL_LAYOUT *) (tl->domp + tl->type_cnt));
      qfile_type_list_compute (tl->domp, tl->type_cnt, tl->hdr_size, tl->col, &tl->first_non_cached_col,
			       tl->data_off, &tl->bitmap_size);
    }
  else
    {
      tl->first_non_cached_col = 0;
      tl->bitmap_size = 0;
      tl->data_off[0] = (int16_t) DB_ALIGN (tl->hdr_size, QFILE_TUPLE_ALIGNMENT);
      tl->data_off[1] = tl->data_off[0];
    }

  tl->finalized = true;
}

#if !defined(NDEBUG)
/*
 * qfile_type_list_check () - debug cross-check: stored descriptor == recomputation from domp.
 *   return: true when consistent; false means a domp mutation without qfile_type_list_finalize ()
 */
bool
qfile_type_list_check (const QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  QFILE_COL_LAYOUT *col;
  int first_non_cached_col;
  int16_t data_off[2], bitmap_size;
  bool ok;

  if (tl->type_cnt <= 0)
    {
      /* an empty list has nothing to lay out and may never have been finalized; no column can be read from it anyway */
      return tl->domp == NULL && tl->col == NULL;
    }
  if (!tl->finalized
      || !(tl->hdr_size == QFILE_TUPLE_HDR_SIZE_FORWARD || tl->hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD))
    {
      return false;
    }
  if (tl->col != (QFILE_COL_LAYOUT *) (tl->domp + tl->type_cnt))
    {
      return false;
    }

  col = (QFILE_COL_LAYOUT *) malloc (tl->type_cnt * sizeof (QFILE_COL_LAYOUT));
  if (col == NULL)
    {
      return true;		/* cannot check; do not fail the caller for that */
    }

  qfile_type_list_compute (tl->domp, tl->type_cnt, tl->hdr_size, col, &first_non_cached_col, data_off, &bitmap_size);

  ok = (memcmp (col, tl->col, tl->type_cnt * sizeof (QFILE_COL_LAYOUT)) == 0
	&& first_non_cached_col == tl->first_non_cached_col && data_off[0] == tl->data_off[0]
	&& data_off[1] == tl->data_off[1] && bitmap_size == tl->bitmap_size);

  free (col);
  return ok;
}
#endif /* !NDEBUG */

/*
 * qfile_slot_locate_walk () - out-of-line half of qfile_slot_locate (): starts the cache if needed, then walks from
 *   the cached position to column col.
 */
const char *
qfile_slot_locate_walk (QFILE_TUPLE_RECORD * rec, int col, int *body_len, bool * is_null)
{
  const QFILE_TUPLE_VALUE_TYPE_LIST *tl = rec->tl;
  const QFILE_COL_LAYOUT *c;
  const unsigned char *bm;
  const char *tpl = rec->tpl;
  int i, off, hdr, len;

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
      off = DB_ALIGN (off, c->alignby);
      if (c->kind == QFILE_COL_FIXED)
	{
	  off += c->size;
	}
      else
	{
	  /* the format is not self-describing: a length header read out of the tuple is the only thing that keeps the
	   * walk inside it, so check the invariant here as qfile_tuple_walk_next () does (debug only, SER-02) */
	  assert (off < QFILE_GET_TUPLE_LENGTH (tpl));
	  len = qfile_var_hdr_decode (tpl + off, &hdr);
	  off += hdr + len;
	}
      assert (off <= QFILE_GET_TUPLE_LENGTH (tpl));
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
  off = DB_ALIGN (off, c->alignby);
  if (c->kind == QFILE_COL_FIXED)
    {
      assert (off + c->size <= QFILE_GET_TUPLE_LENGTH (tpl));
      *body_len = c->size;
      *is_null = false;
      return tpl + off;
    }

  assert (off < QFILE_GET_TUPLE_LENGTH (tpl));
  len = qfile_var_hdr_decode (tpl + off, &hdr);
  assert (off + hdr + len <= QFILE_GET_TUPLE_LENGTH (tpl));
  *body_len = len;
  *is_null = false;
  return tpl + off + hdr;
}

/*
 * qfile_slot_clear () - unbind the descriptor when the scan/cursor closes; does not touch rec->tpl / rec->size
 *   (still freed by the record owner).
 */
void
qfile_slot_clear (QFILE_TUPLE_RECORD * rec)
{
  rec->tl = NULL;
  rec->nvalid = -1;
}

/*
 * qfile_slot_overwrite_value () - in-place rewrite of column col with value.
 *   return: NO_ERROR or ER_FAILED
 */
int
qfile_slot_overwrite_value (QFILE_TUPLE_RECORD * rec, int col, const TP_DOMAIN * dom, const DB_VALUE * value)
{
  OR_BUF buf;
  const QFILE_COL_LAYOUT *c;
  const char *body;
  const PR_TYPE *t;
  int len, new_len;
  bool is_null;

  body = qfile_slot_locate (rec, col, &len, &is_null);

  if (value == NULL || DB_IS_NULL (value) || is_null || dom == NULL || dom->type == NULL
      || TP_DOMAIN_TYPE (dom) != DB_VALUE_DOMAIN_TYPE (value))
    {
      assert (false);
      return ER_FAILED;
    }
  c = &rec->tl->col[col];
  t = pr_type_from_id (DB_VALUE_DOMAIN_TYPE (value));
  new_len = qfile_value_direct (c, value) ? pr_index_writeval_disk_size ((DB_VALUE *) value)
    : pr_data_writeval_disk_size ((DB_VALUE *) value);
  if (new_len != len)
    {
      assert (false);
      return ER_FAILED;
    }
  /* the column's own domain must agree with the decoding domain unless it is still unresolved */
  assert (TP_DOMAIN_TYPE (rec->tl->domp[col]) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_TYPE (rec->tl->domp[col]) == TP_DOMAIN_TYPE (dom));

  if (c->kind == QFILE_COL_FIXED)
    {
      or_init (&buf, (char *) body, len);
      return (t->data_writeval (&buf, value) == NO_ERROR) ? NO_ERROR : ER_FAILED;
    }
  if (qfile_value_direct (c, value))
    {
      or_init (&buf, (char *) body, len);
      if (t->index_writeval (&buf, value) != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != len)
	{
	  assert (false);
	  return ER_FAILED;
	}
      return NO_ERROR;
    }

  /* VAR/SCRATCH: the body is 4-aligned in the tuple, overwrite it in place */
  assert (PTR_ALIGN (body, QFILE_TUPLE_ALIGNMENT) == body);
  or_init (&buf, (char *) body, len);
  if (t->data_writeval (&buf, value) != NO_ERROR || CAST_BUFLEN (buf.ptr - buf.buffer) != len)
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
qfile_col_cmpdisk_function (const QFILE_COL_LAYOUT * c, const TP_DOMAIN * dom)
{
  if (c->kind == QFILE_COL_VAR && c->var_access == QFILE_VAR_DIRECT && dom->type->has_index_readval ())
    {
      return dom->type->get_index_cmpdisk_function ();
    }
  return dom->type->get_data_cmpdisk_function ();
}
