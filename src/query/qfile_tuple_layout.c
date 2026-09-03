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
 * qfile_tuple_layout.c - temporary list file tuple slot & accessor API (CBRD-27365, ADR 0016)
 *
 * Cold paths: layout descriptor allocation / finalize / debug cross-check, in-place overwrite, slot teardown.
 * The hot accessors are inline in qfile_tuple_layout.h.
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

/*
 * qfile_type_list_compute () - pure layout computation (D-181-6 finalize algorithm, spec #180 v1).
 *   Fills col[type_cnt] and the list-level fields from (domp, type_cnt, hdr_size). Idempotent.
 *   An unresolved DB_TYPE_VARIABLE column is laid out as VAR (its tuples so far hold NULL there, #186).
 */
static void
qfile_type_list_compute (TP_DOMAIN ** domp, int type_cnt, int hdr_size, QFILE_COL_LAYOUT * col,
			 int *first_non_cached_col, int16_t data_off[2], int16_t * bitmap_size)
{
  int i, off = 0;

  *bitmap_size = (int16_t) ((type_cnt + 7) >> 3);
  data_off[0] = (int16_t) DB_ALIGN (hdr_size, INT_ALIGNMENT);
  data_off[1] = (int16_t) DB_ALIGN (hdr_size + *bitmap_size, INT_ALIGNMENT);
  *first_non_cached_col = type_cnt;

  for (i = 0; i < type_cnt; i++)
    {
      const TP_DOMAIN *dom = domp[i];
      const PR_TYPE *t;
      int a;

      assert (dom != NULL && dom->type != NULL);
      t = dom->type;

      col[i]._pad = 0;

      if (TP_DOMAIN_TYPE (dom) == DB_TYPE_VARIABLE || t->has_computed_disk_size ())
	{
	  col[i].off = -1;
	  col[i].size = -1;
	  col[i].kind = QFILE_COL_VAR;
	  col[i].var_access = t->has_index_readval () ? QFILE_VAR_DIRECT : QFILE_VAR_SCRATCH;
	  col[i].alignby = 1;
	  if (*first_non_cached_col == type_cnt)
	    {
	      *first_non_cached_col = i;
	    }
	  continue;
	}

      a = MIN (t->alignment, INT_ALIGNMENT);	/* D-180-4: BIGINT/DOUBLE align 4 and are read by memcpy */
      if (a < 1)
	{
	  a = 1;
	}

      col[i].size = (int16_t) t->disksize;
      col[i].kind = QFILE_COL_FIXED;
      col[i].var_access = 0;
      col[i].alignby = (uint8_t) a;

      if (*first_non_cached_col == type_cnt)
	{
	  off = DB_ALIGN (off, a);
	  if (off > INT16_MAX)
	    {
	      /* D-181-4: give up the constant-offset cache from here on; correctness is unaffected */
	      *first_non_cached_col = i;
	      col[i].off = -1;
	    }
	  else
	    {
	      col[i].off = (int16_t) off;
	      off += t->disksize;
	    }
	}
      else
	{
	  col[i].off = -1;
	}
    }
}

/*
 * qfile_type_list_alloc () - allocate the [domp[type_cnt] | col[type_cnt]] block of a list's type list.
 *   return: NO_ERROR or ER_FAILED (out of memory; the caller reports it as before)
 *   The domp entries are left for the caller to fill; finalized is false until qfile_type_list_finalize ().
 *   type_cnt == 0 leaves domp/col NULL like the historical code.
 */
int
qfile_type_list_alloc (QFILE_TUPLE_VALUE_TYPE_LIST * tl, int type_cnt, int hdr_size)
{
  assert (type_cnt >= 0);
  assert (hdr_size == 4 || hdr_size == 8);

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
 * qfile_type_list_copy () - allocate dest and inherit src (block memcpy when src is finalized, D-181-6).
 *   return: NO_ERROR or ER_FAILED
 *   A non-finalized src (an INPUT type list) only contributes its domains; the caller finalizes dest.
 */
int
qfile_type_list_copy (QFILE_TUPLE_VALUE_TYPE_LIST * dest, const QFILE_TUPLE_VALUE_TYPE_LIST * src)
{
  int hdr_size = (src->hdr_size == 4 || src->hdr_size == 8) ? src->hdr_size : QFILE_TL_HDR_SIZE_LEGACY;

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
 * qfile_type_list_finalize () - (re)compute the layout descriptor from domp (mutator-owns-finalize, D-181-6).
 *   Must run after the last domp mutation of a list: qfile_open_list, qfile_modify_type_list,
 *   qfile_update_domains_on_type_list, qfile_unify_types, or_unpack_unbound_listid, the px domain resolver and
 *   the executor-side late domain fixes of DISTINCT aggregate/analytic and hash GROUP BY partial lists.
 */
void
qfile_type_list_finalize (QFILE_TUPLE_VALUE_TYPE_LIST * tl)
{
  assert (tl->hdr_size == 4 || tl->hdr_size == 8);

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
      tl->data_off[0] = (int16_t) DB_ALIGN (tl->hdr_size, INT_ALIGNMENT);
      tl->data_off[1] = tl->data_off[0];
    }

  tl->finalized = true;
}

#if !defined(NDEBUG)
/*
 * qfile_type_list_check () - debug cross-check: stored descriptor == recomputation from domp (D-181-7).
 *   return: true when consistent. A false return means a domp mutation without qfile_type_list_finalize ().
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
      /* an empty list (e.g. an XASL list_id that never produced a tuple, or a cleared one) has nothing to lay out and
       * may legitimately never have been finalized; no column can be read from it anyway */
      return tl->domp == NULL && tl->col == NULL;
    }
  if (!tl->finalized || !(tl->hdr_size == 4 || tl->hdr_size == 8))
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

  qfile_type_list_compute (tl->domp, tl->type_cnt, tl->hdr_size, col, &first_non_cached_col, data_off,
			   &bitmap_size);

  ok = (memcmp (col, tl->col, tl->type_cnt * sizeof (QFILE_COL_LAYOUT)) == 0
	&& first_non_cached_col == tl->first_non_cached_col && data_off[0] == tl->data_off[0]
	&& data_off[1] == tl->data_off[1] && bitmap_size == tl->bitmap_size);

  free (col);
  return ok;
}
#endif /* !NDEBUG */

/*
 * qfile_slot_clear () - release the slot-owned scratch area and unbind the descriptor.
 *   Called by the slot owner when the scan/cursor is closed (D-182-10). Does not touch rec->tpl / rec->size:
 *   the owned tuple buffer is still freed by the record owner as before.
 */
void
qfile_slot_clear (QFILE_TUPLE_RECORD * rec)
{
  if (rec->scratch != NULL)
    {
      db_private_free (NULL, rec->scratch);
      rec->scratch = NULL;
    }
  rec->scratch_size = 0;
  rec->tl = NULL;
  rec->nvalid = 0;
  rec->fast_limit = 0;
  rec->off = QFILE_TUPLE_LENGTH_SIZE;
}

/*
 * qfile_slot_overwrite_value () - in-place rewrite of column col with value (D-182-13, #185).
 *   return: NO_ERROR or ER_FAILED
 *   Contract (asserted in debug, ER_FAILED in release): value is not NULL, the stored column is bound, the
 *   value's type is the column's decoding type, and the value's legacy encoded size equals the stored body
 *   length. The five in-place sites (orderby_num, inst_num, CONNECT BY ISLEAF/ISCYCLE/parent_pos) all satisfy it.
 *   Only the body is rewritten; the value header is untouched (it already says bound/len).
 */
int
qfile_slot_overwrite_value (QFILE_TUPLE_RECORD * rec, int col, const TP_DOMAIN * dom, const DB_VALUE * value)
{
  OR_BUF buf;
  const char *body;
  int len;
  bool is_null;

  body = qfile_slot_locate (rec, col, &len, &is_null);

  if (value == NULL || DB_IS_NULL (value) || is_null || dom == NULL || dom->type == NULL
      || TP_DOMAIN_TYPE (dom) != DB_VALUE_DOMAIN_TYPE (value)
      || (int) QFILE_LEGACY_VALUE_ENCODED_SIZE (pr_data_writeval_disk_size ((DB_VALUE *) value)) != len)
    {
      assert (false);
      return ER_FAILED;
    }
  /* the column's own domain must agree with the decoding domain unless it is still unresolved */
  assert (TP_DOMAIN_TYPE (rec->tl->domp[col]) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_TYPE (rec->tl->domp[col]) == TP_DOMAIN_TYPE (dom));

  or_init (&buf, (char *) body, len);
  if (dom->type->data_writeval (&buf, (DB_VALUE *) value) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}
