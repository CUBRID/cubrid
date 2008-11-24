/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */

/*
 * scan_manager.c - scan management routines
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "error_manager.h"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "btree.h"
#include "heap_file.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "fetch.h"
#include "list_file.h"
#include "set_scan.h"
#include "system_parameter.h"
#include "btree_load.h"

#include "dbval.h"

#if !defined(SERVER_MODE)
#undef  MUTEX_INIT
#define MUTEX_INIT(a)
#undef  MUTEX_DESTROY
#define MUTEX_DESTROY(a)
#undef  MUTEX_LOCK
#define MUTEX_LOCK(a, b)
#undef  MUTEX_UNLOCK
#define MUTEX_UNLOCK(a)
#endif

/* this macro is used to make sure that heap file identifier is initialized
 * properly that heap file scan routines will work properly.
 */
#define UT_CAST_TO_NULL_HEAP_OID(hfidp,oidp)                                 \
  do { (oidp)->pageid = NULL_PAGEID;                                         \
       (oidp)->volid = (hfidp)->vfid.volid;                                  \
       (oidp)->slotid = NULL_SLOTID;                                         \
  } while (0)

#define GET_NTH_OID(oid_setp, n) ((OID *)((OID *)(oid_setp) + (n)))

 /* Depending on the isolation level, there are times when we may not be
  *  able to fetch a scan item. */
#define QPROC_OK_IF_DELETED(scan, iso)                               \
                ((scan) == S_DOESNT_EXIST &&                \
                 ((iso) == TRAN_REP_CLASS_UNCOMMIT_INSTANCE ||    \
                  (iso) == TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE))

typedef int QPROC_KEY_VAL_FU (KEY_VAL_RANGE * key_vals, int key_cnt);
typedef SCAN_CODE (*QP_SCAN_FUNC) (THREAD_ENTRY * thread_p, SCAN_ID * s_id);

typedef enum
{
  ROP_NA, ROP_EQ,
  ROP_GE, ROP_GT, ROP_GT_INF, ROP_GT_ADJ,
  ROP_LE, ROP_LT, ROP_LT_INF, ROP_LT_ADJ
} ROP_TYPE;

struct rop_range_struct
{
  ROP_TYPE left;
  ROP_TYPE right;
  RANGE range;
} rop_range_table[] =
{
  {
  ROP_NA, ROP_EQ, NA_NA},
  {
  ROP_GE, ROP_LE, GE_LE},
  {
  ROP_GE, ROP_LT, GE_LT},
  {
  ROP_GT, ROP_LE, GT_LE},
  {
  ROP_GT, ROP_LT, GT_LT},
  {
  ROP_GE, ROP_LT_INF, GE_INF},
  {
  ROP_GT, ROP_LT_INF, GT_INF},
  {
  ROP_GT_INF, ROP_LE, INF_LE},
  {
  ROP_GT_INF, ROP_LT, INF_LT},
  {
  ROP_GT_INF, ROP_LT_INF, INF_INF},
  {
  ROP_EQ, ROP_NA, EQ_NA}
};
static const int rop_range_table_size =
  sizeof (rop_range_table) / sizeof (struct rop_range_struct);

#if defined(SERVER_MODE)
static MUTEX_T scan_Iscan_oid_buf_list_mutex = MUTEX_INITIALIZER;
#endif
static OID *scan_Iscan_oid_buf_list = NULL;
static int scan_Iscan_oid_buf_list_count = 0;

static OID *alloc_oid_buf ();
static OID *alloc_iscan_oid_buf_list ();
static void free_iscan_oid_buf_list (OID * oid_buf_p);
static void rop_to_range (RANGE * range, ROP_TYPE left, ROP_TYPE right);
static void range_to_rop (ROP_TYPE * left, ROP_TYPE * rightk, RANGE range);
static ROP_TYPE compare_val_op (DB_VALUE * val1, ROP_TYPE op1,
				DB_VALUE * val2, ROP_TYPE op2);
static int key_val_compare (const void *p1, const void *p2);
static int eliminate_duplicated_keys (KEY_VAL_RANGE * key_vals, int key_cnt);
static int merge_key_ranges (KEY_VAL_RANGE * key_vals, int key_cnt);
static int check_key_vals (KEY_VAL_RANGE * key_vals, int key_cnt,
			   QPROC_KEY_VAL_FU * chk_fn);
static int xd_dbvals_to_midxkey (THREAD_ENTRY * thread_p, BTREE_SCAN * BTS,
				 REGU_VARIABLE *
				 func, DB_VALUE * retval, VAL_DESCR * vd);
static int scan_get_index_oidset (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
static void scan_init_scan_id (SCAN_ID * scan_id,
			       int readonly_scan,
			       int fixed,
			       int grouped,
			       QPROC_SINGLE_FETCH single_fetch,
			       DB_VALUE * join_dbval, VAL_LIST * val_list,
			       VAL_DESCR * vd);
static SCAN_CODE scan_next_scan_local (THREAD_ENTRY * thread_p,
				       SCAN_ID * scan_id);
static SCAN_CODE scan_handle_single_scan (THREAD_ENTRY * thread_p,
					  SCAN_ID * s_id,
					  QP_SCAN_FUNC next_scan);
static SCAN_CODE scan_prev_scan_local (THREAD_ENTRY * thread_p,
				       SCAN_ID * scan_id);

/*
 * alloc_oid_buf () - allocate oid buf
 *   return: pointer to alloced oid buf, NULL for error
 */
static OID *
alloc_oid_buf ()
{
  int oid_buf_size;
  OID *oid_buf_p;

  oid_buf_size = DB_PAGESIZE * PRM_BT_OID_NBUFFERS;
  oid_buf_p = (OID *) malloc (oid_buf_size);
  if (oid_buf_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, oid_buf_size);
    }

  return oid_buf_p;
}

/*
 * alloc_iscan_oid_buf_list () - allocate list of oid buf
 *   return: pointer to alloced oid buf, NULL for error
 */
static OID *
alloc_iscan_oid_buf_list ()
{
  OID *oid_buf_p;
  int rv;

  oid_buf_p = NULL;

  if (scan_Iscan_oid_buf_list != NULL)
    {
      MUTEX_LOCK (rv, scan_Iscan_oid_buf_list_mutex);
      if (scan_Iscan_oid_buf_list != NULL)
	{
	  /* retry with mutex */
	  oid_buf_p = scan_Iscan_oid_buf_list;
	  /* save previous oid buf pointer */
	  scan_Iscan_oid_buf_list = (OID *) (*(intptr_t *) oid_buf_p);
	  scan_Iscan_oid_buf_list_count--;
	}
      MUTEX_UNLOCK (scan_Iscan_oid_buf_list_mutex);
    }

  if (oid_buf_p == NULL)	/* need to alloc */
    {
      oid_buf_p = alloc_oid_buf ();
    }

  return oid_buf_p;
}

/*
 * free_iscan_oid_buf_list () - free the given iscan oid buf
 *   return: NO_ERROR
 */
static void
free_iscan_oid_buf_list (OID * oid_buf_p)
{
  int rv;

  MUTEX_LOCK (rv, scan_Iscan_oid_buf_list_mutex);
  if (scan_Iscan_oid_buf_list_count < PRM_MAX_THREADS)
    {
      /* save previous oid buf pointer */
      *(intptr_t *) oid_buf_p = (intptr_t) scan_Iscan_oid_buf_list;
      scan_Iscan_oid_buf_list = oid_buf_p;
      scan_Iscan_oid_buf_list_count++;
    }
  else
    {
      free_and_init (oid_buf_p);
    }
  MUTEX_UNLOCK (scan_Iscan_oid_buf_list_mutex);
}

/*
 * rop_to_range () - map left/right to range operator
 *   return:
 *   range(out): full-RANGE operator
 *   left(in): left-side range operator
 *   right(in): right-side range operator
 */
static void
rop_to_range (RANGE * range, ROP_TYPE left, ROP_TYPE right)
{
  int i;

  *range = NA_NA;

  for (i = 0; i < rop_range_table_size; i++)
    {
      if (left == rop_range_table[i].left
	  && right == rop_range_table[i].right)
	{
	  /* found match */
	  *range = rop_range_table[i].range;
	  break;
	}
    }
}

/*
 * range_to_rop () - map range to left/right operator
 *   return:
 *   left(out): left-side range operator
 *   right(out): right-side range operator
 *   range(in): full-RANGE operator
 */
static void
range_to_rop (ROP_TYPE * left, ROP_TYPE * right, RANGE range)
{
  int i;

  *left = ROP_NA;
  *right = ROP_NA;

  for (i = 0; i < rop_range_table_size; i++)
    {
      if (range == rop_range_table[i].range)
	{
	  /* found match */
	  *left = rop_range_table[i].left;
	  *right = rop_range_table[i].right;
	  break;
	}
    }
}

/*
 * compare_val_op () - compare two values specified by range operator
 *   return:
 *   val1(in):
 *   op1(in):
 *   val2(in):
 *   op2(in):
 */
static ROP_TYPE
compare_val_op (DB_VALUE * val1, ROP_TYPE op1, DB_VALUE * val2, ROP_TYPE op2)
{
  int rc;

  if (op1 == ROP_GT_INF)	/* val1 is -INF */
    {
      return (op1 == op2) ? ROP_EQ : ROP_LT;
    }
  if (op1 == ROP_LT_INF)	/* val1 is +INF */
    {
      return (op1 == op2) ? ROP_EQ : ROP_GT;
    }
  if (op2 == ROP_GT_INF)	/* val2 is -INF */
    {
      return (op2 == op1) ? ROP_EQ : ROP_GT;
    }
  if (op2 == ROP_LT_INF)	/* val2 is +INF */
    {
      return (op2 == op1) ? ROP_EQ : ROP_LT;
    }

  rc = tp_value_compare (val1, val2, 1, 1);
  if (rc == DB_EQ)
    {
      /* (val1, op1) == (val2, op2) */
      if (op1 == op2)
	{
	  return ROP_EQ;
	}
      if (op1 == ROP_EQ || op1 == ROP_GE || op1 == ROP_LE)
	{
	  if (op2 == ROP_EQ || op2 == ROP_GE || op2 == ROP_LE)
	    {
	      return ROP_EQ;
	    }
	  return (op2 == ROP_GT) ? ROP_LT_ADJ : ROP_GT_ADJ;
	}
      if (op1 == ROP_GT)
	{
	  if (op2 == ROP_EQ || op2 == ROP_GE || op2 == ROP_LE)
	    {
	      return ROP_GT_ADJ;
	    }
	  return (op2 == ROP_LT) ? ROP_GT : ROP_EQ;
	}
      if (op1 == ROP_LT)
	{
	  if (op2 == ROP_EQ || op2 == ROP_GE || op2 == ROP_LE)
	    {
	      return ROP_LT_ADJ;
	    }
	  return (op2 == ROP_GT) ? ROP_LT : ROP_EQ;
	}
    }
  else if (rc == DB_LT)
    {
      /* (val1, op1) < (val2, op2) */
      return ROP_LT;
    }
  else if (rc == DB_GT)
    {
      /* (val1, op1) > (val2, op2) */
      return ROP_GT;
    }

  /* tp_value_compare() returned error? */
  return (rc == DB_EQ) ? ROP_EQ : ROP_NA;
}

/*
 * key_val_compare () - key value sorting function
 *   return:
 *   p1 (in): pointer to key1 range
 *   p2 (in): pointer to key2 range
 */
static int
key_val_compare (const void *p1, const void *p2)
{
  return tp_value_compare (&((KEY_VAL_RANGE *) p1)->key1,
			   &((KEY_VAL_RANGE *) p2)->key1, 1, 1);
}				/* key_value_cmp() */

/*
 * eliminate_duplicated_keys () - elimnate duplicated key values
 *   return: number of keys, -1 for error
 *   key_vals (in): pointer to array of KEY_VAL_RANGE structure
 *   key_cnt (in): number of keys; size of key_vals
 */
static int
eliminate_duplicated_keys (KEY_VAL_RANGE * key_vals, int key_cnt)
{
  int n;
  KEY_VAL_RANGE *curp, *nextp;

  curp = key_vals;
  nextp = key_vals + 1;
  n = 0;
  while (key_cnt > 1 && n < key_cnt - 1)
    {
      if (tp_value_compare (&curp->key1, &nextp->key1, 1, 1) == DB_EQ)
	{
	  pr_clear_value (&nextp->key1);
	  pr_clear_value (&nextp->key2);
	  memmove (nextp, nextp + 1,
		   sizeof (KEY_VAL_RANGE) * (key_cnt - n - 2));
	  key_cnt--;
	}
      else
	{
	  curp++;
	  nextp++;
	  n++;
	}
    }

  return key_cnt;
}

/*
 * merge_key_ranges () - merge search key ranges
 *   return: number of keys, -1 for error
 *   key_vals (in): pointer to array of KEY_VAL_RANGE structure
 *   key_cnt (in): number of keys; size of key_vals
 */
static int
merge_key_ranges (KEY_VAL_RANGE * key_vals, int key_cnt)
{
  int cur_n, next_n;
  KEY_VAL_RANGE *curp, *nextp;
  ROP_TYPE cur_op1, cur_op2, next_op1, next_op2, cmp;

  curp = key_vals;
  cur_n = 0;
  while (key_cnt > 1 && cur_n < key_cnt - 1)
    {

      range_to_rop (&cur_op1, &cur_op2, curp->range);

      nextp = curp + 1;
      next_n = cur_n + 1;
      while (next_n < key_cnt)
	{

	  range_to_rop (&next_op1, &next_op2, nextp->range);

	  /* check if the two key ranges are mergable */
	  if (compare_val_op (&curp->key2, cur_op2,
			      &nextp->key1, next_op1) == ROP_LT ||
	      compare_val_op (&curp->key1, cur_op1,
			      &nextp->key2, next_op2) == ROP_GT)
	    {
	      /* they are disjoint */
	      nextp++;
	      next_n++;
	      continue;
	    }

	  /* determine the lower bound of the merged key range */
	  cmp = compare_val_op (&curp->key1, cur_op1, &nextp->key1, next_op1);
	  if (cmp == ROP_GT_ADJ || cmp == ROP_GT)
	    {
	      pr_clear_value (&curp->key1);
	      curp->key1 = nextp->key1;	/* bitwise copy */
	      DB_MAKE_NULL (&nextp->key1);
	      cur_op1 = next_op1;
	    }
	  else
	    {
	      pr_clear_value (&nextp->key1);
	    }

	  /* determine the upper bound of the merged key range */
	  cmp = compare_val_op (&curp->key2, cur_op2, &nextp->key2, next_op2);
	  if (cmp == ROP_LT || cmp == ROP_LT_ADJ)
	    {
	      pr_clear_value (&curp->key2);
	      curp->key2 = nextp->key2;	/* bitwise copy */
	      DB_MAKE_NULL (&nextp->key2);
	      cur_op2 = next_op2;
	    }
	  else
	    {
	      pr_clear_value (&nextp->key2);
	    }

	  /* determine the new range type */
	  rop_to_range (&curp->range, cur_op1, cur_op2);
	  /* remove merged one(nextp) */
	  memmove (nextp, nextp + 1,
		   sizeof (KEY_VAL_RANGE) * (key_cnt - next_n - 1));
	  key_cnt--;
	  nextp++;
	  next_n++;
	}

      curp++;
      cur_n++;
    }

  return key_cnt;
}

/*
 * check_key_vals () - check key values
 *   return: number of keys, -1 for error
 *   key_vals (in): pointer to array of KEY_VAL_RANGE structure
 *   key_cnt (in): number of keys; size of key_vals
 *   chk_fn (in): check function for key_vals
 */
static int
check_key_vals (KEY_VAL_RANGE * key_vals, int key_cnt,
		QPROC_KEY_VAL_FU * key_val_fn)
{
  if (key_cnt <= 1)
    {
      return key_cnt;
    }

  qsort ((void *) key_vals, key_cnt, sizeof (KEY_VAL_RANGE), key_val_compare);

  return key_val_fn (key_vals, key_cnt);
}

/*
 * xd_dbvals_to_midxkey () -
 *   return: NO_ERROR or ER_code
 */
static int
xd_dbvals_to_midxkey (THREAD_ENTRY * thread_p, BTREE_SCAN * BTS,
		      REGU_VARIABLE * func, DB_VALUE * retval, VAL_DESCR * vd)
{
  int ret = NO_ERROR;
  DB_VALUE temp_val, *val;
  DB_TYPE val_type_id;
  bool clear_value;
  unsigned char save_val_is_null;
  unsigned char save_val_type;
  bool save_need_clear;
  DB_MIDXKEY midxkey;

  int n_atts, nwords, i;
  int buf_size, disk_size;
  unsigned int *bits;

  REGU_VARIABLE_LIST operand;

  TP_DOMAIN *dom = NULL;	/* cuurent domain */
  TP_DOMAIN *setdomain = NULL;

  char *nullmap_ptr;		/* ponter to boundbits */
  char *key_ptr;		/* current position in key */

  OR_BUF buf;

  if (func->domain->type->id != DB_TYPE_MIDXKEY)
    {
      return ER_FAILED;
    }

  buf_size = nwords = 0;
  midxkey.buf = NULL;

  /**************************
   calculate midxkey's size
  **************************/

  /* pointer to multi-column index key domain info. structure */
  setdomain = BTS->btid_int.key_type->setdomain;

  val = NULL;			/* init */
  clear_value = false;
  for (operand = func->value.funcp->operand, n_atts = 0, dom = setdomain;
       operand; operand = operand->next, n_atts++, dom = dom->next)
    {
      if (dom->precision < 0)
	{
#if defined(QP_DEBUG)
	  fprintf (stdout, "invalid key_type at %s:%d\n", __FILE__, __LINE__);
#endif
	  goto err_exit;
	}

      if (clear_value)
	{
	  pr_clear_value (val);
	  clear_value = false;
	}

      disk_size = 0;

      if (dom->type->lengthval == NULL)
	{			/* very-simple domain */
	  ret =
	    fetch_peek_dbval (thread_p, &(operand->value), vd, NULL, NULL,
			      NULL, &val);
	  if (ret != NO_ERROR)
	    {
	      goto err_exit;
	    }

	  if (DB_IS_NULL (val))
	    {
	      continue;		/* zero size; go ahead */
	    }

	  disk_size += dom->type->disksize;
	}
      else
	{			/* check for coerce val */
	  if (dom->type->id == DB_TYPE_OBJECT)
	    {
	      disk_size += dom->type->disksize;
	    }
	  else
	    {
	      val = &temp_val;
	      PRIM_SET_NULL (val);
	      clear_value = true;
	      ret =
		fetch_copy_dbval (thread_p, &(operand->value), vd, NULL, NULL,
				  NULL, val);
	      if (ret != NO_ERROR)
		{
		  goto err_exit;
		}

	      if (DB_IS_NULL (val))
		{
		  continue;	/* zero size; go ahead */
		}

	      val_type_id = PRIM_TYPE (val);
	      if ((dom->type->id == DB_TYPE_CHAR
		   || dom->type->id == DB_TYPE_BIT
		   || dom->type->id == DB_TYPE_NCHAR)
		  && QSTR_IS_ANY_CHAR_OR_BIT (val_type_id))
		{
		  val->domain.char_info.length = dom->precision;
		}
	      else if (dom->type->id != val_type_id)
		{
		  save_val_is_null = val->domain.general_info.is_null;
		  save_val_type = val->domain.general_info.type;
		  save_need_clear = val->need_clear;
		  if (tp_value_cast_no_domain_select (val, val, dom, true) !=
		      DOMAIN_COMPATIBLE)
		    {
		      /* restore null casted val's non-null info; prevent server LEAK */
		      if (!save_val_is_null)
			{
			  val->domain.general_info.is_null = save_val_is_null;
			  val->domain.general_info.type = save_val_type;
			  val->need_clear = save_need_clear;
			}
		      goto err_exit;
		    }
		}

	      disk_size += (*(dom->type->lengthval)) (val, 1);
	    }
	}

      DB_ALIGN (disk_size, OR_INT_SIZE);
      buf_size += disk_size;
    }

  nwords = OR_BOUND_BIT_WORDS (n_atts);
  buf_size += (nwords * 4);
  midxkey.buf = (char *) db_private_alloc (thread_p, buf_size);
  if (midxkey.buf == NULL)
    {
      retval->need_clear = false;
      goto err_exit;
    }

  nullmap_ptr = midxkey.buf;
  key_ptr = nullmap_ptr + (nwords * 4);

  OR_BUF_INIT (buf, key_ptr, buf_size - (nwords * 4));

  if (nwords > 0)
    {
      bits = (unsigned int *) nullmap_ptr;
      for (i = 0; i < nwords; i++)
	{
	  bits[i] = 0;
	}
    }

  /****************************************************
   generate multi columns key (values -> midxkey.buf)
  ****************************************************/

  if (clear_value)
    {
      pr_clear_value (val);
      clear_value = false;
    }

  val = &temp_val;
  PRIM_SET_NULL (val);
  for (operand = func->value.funcp->operand, dom = setdomain, i = 0;
       operand && (i < n_atts); operand = operand->next, dom = dom->next, i++)
    {

      if (clear_value)
	{
	  pr_clear_value (val);
	}

      clear_value = true;
      /* must copy, do not peek */
      ret =
	fetch_copy_dbval (thread_p, &(operand->value), vd, NULL, NULL, NULL,
			  val);
      if (ret != NO_ERROR)
	{
	  goto err_exit;
	}

      if (DB_IS_NULL (val))
	{
	  continue;		/* zero size; go ahead */
	}

      if (dom->type->id == DB_TYPE_OBJECT)
	{
	  ;			/* need not to coerce */
	}
      else
	{
	  val_type_id = PRIM_TYPE (val);
	  if ((dom->type->id == DB_TYPE_CHAR
	       || dom->type->id == DB_TYPE_BIT
	       || dom->type->id == DB_TYPE_NCHAR)
	      && QSTR_IS_ANY_CHAR_OR_BIT (PRIM_TYPE (val)))
	    {
	      val->domain.char_info.length = dom->precision;
	    }
	  else if (dom->type->id != val_type_id)
	    {
	      save_val_is_null = val->domain.general_info.is_null;
	      save_val_type = val->domain.general_info.type;
	      save_need_clear = val->need_clear;
	      if (tp_value_cast_no_domain_select (val, val, dom,
						  true) != DOMAIN_COMPATIBLE)
		{
		  /* restore null casted val's non-null info; prevent server LEAK */
		  if (!save_val_is_null)
		    {
		      val->domain.general_info.is_null = save_val_is_null;
		      val->domain.general_info.type = save_val_type;
		      val->need_clear = save_need_clear;
		    }
		  goto err_exit;
		}
	    }
	}

      (*((dom->type)->writeval)) (&buf, val);

      or_get_align32 (&buf);
      OR_ENABLE_BOUND_BIT (nullmap_ptr, i);
    }				/* for (operand = ...) */

  /***********************
   Make midxkey DB_VALUE
  ***********************/
  midxkey.size = buf_size;
  midxkey.ncolumns = n_atts;

  midxkey.domain = BTS->btid_int.key_type;

  ret = db_make_midxkey (retval, &midxkey);
  if (ret != NO_ERROR)
    {
      goto err_exit;
    }

  retval->need_clear = true;

  if (clear_value)
    {
      pr_clear_value (val);
    }

end:

  return ret;

err_exit:

  if (midxkey.buf)
    {
      db_private_free_and_init (thread_p, midxkey.buf);
      midxkey.buf = NULL;
    }

  if (clear_value)
    {
      pr_clear_value (val);
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * scan_get_index_oidset () - Fetch the next group of set of object identifiers
 * from the index associated with the scan identifier.
 *   return: NO_ERROR, or ER_code
 *   s_id(in): Scan identifier
 *
 * Note: If you feel the need
 */
static int
scan_get_index_oidset (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  INDX_SCAN_ID *iscan_id;
  FILTER_INFO key_filter;
  INDX_INFO *indx_infop;
  BTREE_SCAN *BTS;
  int key_cnt, i;
  KEY_VAL_RANGE *key_vals;
  KEY_RANGE *key_ranges;
  RANGE range;
  DB_VALUE *key_val1p, *key_val2p;
  int ret = NO_ERROR;


  /* pointer to INDX_SCAN_ID structure */
  iscan_id = &s_id->s.isid;
  /* pointer to INDX_INFO in INDX_SCAN_ID structure */
  indx_infop = iscan_id->indx_info;
  /* pointer to index scan info. structure */
  BTS = &iscan_id->bt_scan;

  /* number of keys */
  if (iscan_id->curr_keyno == -1)	/* very first time */
    {
      key_cnt = indx_infop->key_info.key_cnt;
    }
  else
    {
      key_cnt = iscan_id->key_cnt;
    }
  /* key values */
  key_vals = iscan_id->key_vals;
  /* key ranges */
  key_ranges = indx_infop->key_info.key_ranges;

  if (key_cnt < 1 || !key_vals || !key_ranges)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }


  /* if it is first time of this scan */
  if (iscan_id->curr_keyno == -1 && indx_infop->key_info.key_cnt == key_cnt)
    {

      /* make DB_VALUE key values from KEY_VALS key ranges */
      for (i = 0; i < key_cnt; i++)
	{
	  /* initialize DB_VALUE first for error case */
	  key_vals[i].range = NA_NA;
	  PRIM_INIT_NULL (&key_vals[i].key1);
	  PRIM_INIT_NULL (&key_vals[i].key2);
	}
      for (i = 0; i < key_cnt; i++)
	{
	  key_vals[i].range = key_ranges[i].range;
	  if (key_ranges[i].key1)
	    {
	      if (key_ranges[i].key1->type == TYPE_FUNC
		  && key_ranges[i].key1->value.funcp->ftype == F_MIDXKEY)
		{
		  ret = xd_dbvals_to_midxkey (thread_p, BTS,
					      key_ranges[i].key1,
					      &key_vals[i].key1, s_id->vd);
		}
	      else
		{
		  ret =
		    fetch_copy_dbval (thread_p, key_ranges[i].key1, s_id->vd,
				      NULL, NULL, NULL, &key_vals[i].key1);
		}
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  if (key_ranges[i].key2)
	    {
	      if (key_ranges[i].key2->type == TYPE_FUNC
		  && key_ranges[i].key2->value.funcp->ftype == F_MIDXKEY)
		{
		  ret = xd_dbvals_to_midxkey (thread_p, BTS,
					      key_ranges[i].key2,
					      &key_vals[i].key2, s_id->vd);
		}
	      else
		{
		  ret =
		    fetch_copy_dbval (thread_p, key_ranges[i].key2, s_id->vd,
				      NULL, NULL, NULL, &key_vals[i].key2);
		}
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      /* clone key1 to key2 in order to safe the key value of upper
	         bound because btree_range_search() might change its value
	         if it is a partial key for multi-column index */
	      if (pr_clone_value (&key_vals[i].key1, &key_vals[i].key2) !=
		  NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  /* check for key range is valid; do partial-key cmp */
	  if (key_ranges[i].key1 && key_ranges[i].key2)
	    {
	      if ((*(BTS->btid_int.key_type->type->cmpval))
		  (&key_vals[i].key1, &key_vals[i].key2,
		   NULL, 0, 0, 1, NULL) > 0)
		{
		  key_vals[i].range = NA_NA;	/* mark as empty range */
		}
	    }

	}

      /* elimnating duplicated keys and merging ranges are required even
         though the query optimizer dose them because the search keys or
         ranges could be unbound values at optimization step such as join
         attribute */
      if (indx_infop->range_type == R_KEYLIST)
	{
	  /* eliminate duplicated keys in the search key list */
	  key_cnt = iscan_id->key_cnt = check_key_vals (key_vals, key_cnt,
							eliminate_duplicated_keys);
	}
      else if (indx_infop->range_type == R_RANGELIST)
	{
	  /* merge serach key ranges */
	  key_cnt = iscan_id->key_cnt = check_key_vals (key_vals, key_cnt,
							merge_key_ranges);
	}

      if (key_cnt < 0)
	{
	  goto exit_on_error;
	}

      iscan_id->curr_keyno = 0;

    }

  /*
   * init vars to execute B+tree key range search
   */

  ret = NO_ERROR;

  /* set key filter information */
  INIT_KEY_FILTER_INFO (key_filter, &iscan_id->key_pred,
			&iscan_id->key_attrs,
			s_id->val_list,
			s_id->vd,
			&iscan_id->cls_oid,
			iscan_id->bt_num_attrs,
			iscan_id->bt_attr_ids,
			&(iscan_id->num_vstr), iscan_id->vstr_ids);
  iscan_id->oid_list.oid_cnt = 0;

  if (iscan_id->curr_keyno < key_cnt)
    {
      /* check for empty range */
      if (key_vals[iscan_id->curr_keyno].range == NA_NA)
	{
	  iscan_id->curr_keyno++;
	  goto end;		/* noting to do */
	}
    }

  /* call 'btree_keyval_search()' or 'btree_range_search()' according to the range type */
  switch (indx_infop->range_type)
    {

    case R_KEY:
      /* key value search */

      /* check prerequisite condition */
      range = key_vals[0].range;
      key_val1p = &key_vals[0].key1;
      key_val2p = &key_vals[0].key2;	/* cloned value */
      if (key_cnt != 1 || range != EQ_NA)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE,
		  0);
	  goto exit_on_error;
	}

      /* When key received as NULL, currently this is assumed an UNBOUND
         value and no object value in the index is equal to NULL value in
         the index scan context. They can be equal to NULL only in the
         "is NULL" context. */
      /* to fix multi-column index NULL problem */
      if (BTREE_START_OF_SCAN (BTS)
	  && (PRIM_IS_NULL (key_val1p)
	      || btree_multicol_key_has_null (key_val1p)))
	{
	  iscan_id->curr_keyno++;
	  break;
	}

      /* calling 'btree_range_search()' */
      iscan_id->oid_list.oid_cnt = btree_range_search (thread_p,
						       &indx_infop->indx_id.i.
						       btid,
						       s_id->readonly_scan,
						       iscan_id->lock_hint,
						       BTS, key_val1p,
						       key_val2p, GE_LE, 1,
						       &iscan_id->cls_oid,
						       iscan_id->oid_list.
						       oidp,
						       (DB_PAGESIZE *
							PRM_BT_OID_NBUFFERS),
						       &key_filter, iscan_id,
						       false,
						       iscan_id->
						       need_count_only);
      if (iscan_id->oid_list.oid_cnt == -1)
	{

	  goto exit_on_error;
	}

      /* We only want to advance the key ptr if we've exhausted the
         current crop of oids on the current key. */
      if (BTREE_END_OF_SCAN (BTS))
	{
	  iscan_id->curr_keyno++;
	}

      break;

    case R_RANGE:
      /* range search */

      /* check prerequisite condition */
      range = key_vals[0].range;
      if (range == EQ_NA)
	{			/* specially, key value search */
	  range = GE_LE;
	}
      key_val1p = &key_vals[0].key1;
      key_val2p = &key_vals[0].key2;
      if (key_cnt != 1 || range < GE_LE || range > INF_LT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE,
		  0);
	  goto exit_on_error;
	}

      /* When key received as NULL, currently this is assumed an UNBOUND
         value and no object value in the index is equal to NULL value in
         the index scan context. They can be equal to NULL only in the
         "is NULL" context. */
      if (range >= GE_LE && range <= GT_LT)
	{
	  /* to fix multi-column index NULL problem */
	  if (BTREE_START_OF_SCAN (BTS)
	      && (PRIM_IS_NULL (key_val1p) || PRIM_IS_NULL (key_val2p)
		  || btree_multicol_key_has_null (key_val1p)
		  || btree_multicol_key_has_null (key_val2p)))
	    {
	      iscan_id->curr_keyno++;
	      break;
	    }
	}
      if (range >= GE_INF && range <= GT_INF)
	{
	  /* to fix multi-column index NULL problem */
	  if (BTREE_START_OF_SCAN (BTS)
	      && (PRIM_IS_NULL (key_val1p)
		  || btree_multicol_key_has_null (key_val1p)))
	    {
	      iscan_id->curr_keyno++;
	      break;
	    }
	  key_val2p = NULL;
	}
      if (range >= INF_LE && range <= INF_LT)
	{
	  /* to fix multi-column index NULL problem */
	  if (BTREE_START_OF_SCAN (BTS)
	      && (PRIM_IS_NULL (key_val2p)
		  || btree_multicol_key_has_null (key_val2p)))
	    {
	      iscan_id->curr_keyno++;
	      break;
	    }
	  key_val1p = NULL;
	}

      /* calling 'btree_range_search()' */
      iscan_id->oid_list.oid_cnt = btree_range_search (thread_p,
						       &indx_infop->indx_id.i.
						       btid,
						       s_id->readonly_scan,
						       iscan_id->lock_hint,
						       BTS, key_val1p,
						       key_val2p, range, 1,
						       &iscan_id->cls_oid,
						       iscan_id->oid_list.
						       oidp,
						       (DB_PAGESIZE *
							PRM_BT_OID_NBUFFERS),
						       &key_filter, iscan_id,
						       false,
						       iscan_id->
						       need_count_only);
      if (iscan_id->oid_list.oid_cnt == -1)
	{
	  goto exit_on_error;
	}

      /* We only want to advance the key ptr if we've exhausted the
         current crop of oids on the current key. */
      if (BTREE_END_OF_SCAN (BTS))
	{
	  iscan_id->curr_keyno++;
	}

      break;

    case R_KEYLIST:
      /* multiple key value search */

      /* for each key value */
      while (iscan_id->curr_keyno < key_cnt)
	{
	  /* check prerequisite condition */
	  range = key_vals[iscan_id->curr_keyno].range;
	  key_val1p = &key_vals[iscan_id->curr_keyno].key1;
	  key_val2p = &key_vals[iscan_id->curr_keyno].key2;	/* cloned */
	  if (range != EQ_NA)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_INVALID_XASLNODE, 0);
	      goto exit_on_error;
	    }

	  /* When key received as NULL, currently this is assumed an UNBOUND
	     value and no object value in the index is equal to NULL value in
	     the index scan context. They can be equal to NULL only in the
	     "is NULL" context. */
	  /* to fix multi-column index NULL problem */
	  if (BTREE_START_OF_SCAN (BTS)
	      && (PRIM_IS_NULL (key_val1p)
		  || btree_multicol_key_has_null (key_val1p)))
	    {
	      /* skip this key value and continue to the next */
	      iscan_id->curr_keyno++;
	      continue;
	    }

	  /* calling 'btree_range_search()' */
	  iscan_id->oid_list.oid_cnt = btree_range_search (thread_p,
							   &indx_infop->
							   indx_id.i.btid,
							   s_id->
							   readonly_scan,
							   iscan_id->
							   lock_hint, BTS,
							   key_val1p,
							   key_val2p, GE_LE,
							   1,
							   &iscan_id->cls_oid,
							   iscan_id->oid_list.
							   oidp,
							   (DB_PAGESIZE *
							    PRM_BT_OID_NBUFFERS),
							   &key_filter,
							   iscan_id, false,
							   iscan_id->
							   need_count_only);
	  if (iscan_id->oid_list.oid_cnt == -1)
	    {
	      goto exit_on_error;
	    }

	  /* We only want to advance the key ptr if we've exhausted the
	     current crop of oids on the current key. */
	  if (BTREE_END_OF_SCAN (BTS))
	    {
	      iscan_id->curr_keyno++;
	    }

	  if (iscan_id->oid_list.oid_cnt > 0)
	    {
	      /* we've got some result */
	      break;
	    }

	}

      break;

    case R_RANGELIST:
      /* multiple range search */

      /* for each key value */
      while (iscan_id->curr_keyno < key_cnt)
	{
	  /* check prerequisite condition */
	  range = key_vals[iscan_id->curr_keyno].range;
	  if (range == EQ_NA)
	    {			/* specially, key value search */
	      range = GE_LE;
	    }
	  key_val1p = &key_vals[iscan_id->curr_keyno].key1;
	  key_val2p = &key_vals[iscan_id->curr_keyno].key2;
	  if (range < GE_LE || range > INF_LT)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_INVALID_XASLNODE, 0);
	      goto exit_on_error;
	    }

	  /* When key received as NULL, currently this is assumed an UNBOUND
	     value and no object value in the index is equal to NULL value in
	     the index scan context. They can be equal to NULL only in the
	     "is NULL" context. */
	  if (range >= GE_LE && range <= GT_LT)
	    {
	      /* to fix multi-column index NULL problem */
	      if (BTREE_START_OF_SCAN (BTS)
		  && (PRIM_IS_NULL (key_val1p) || PRIM_IS_NULL (key_val2p)
		      || btree_multicol_key_has_null (key_val1p)
		      || btree_multicol_key_has_null (key_val2p)))
		{
		  /* skip this range and continue to the next */
		  iscan_id->curr_keyno++;
		  continue;
		}
	    }
	  if (range >= GE_INF && range <= GT_INF)
	    {
	      /* to fix multi-column index NULL problem */
	      if (BTREE_START_OF_SCAN (BTS)
		  && (PRIM_IS_NULL (key_val1p)
		      || btree_multicol_key_has_null (key_val1p)))
		{
		  /* skip this range and continue to the next */
		  iscan_id->curr_keyno++;
		  continue;
		}
	      key_val2p = NULL;
	    }
	  if (range >= INF_LE && range <= INF_LT)
	    {
	      /* to fix multi-column index NULL problem */
	      if (BTREE_START_OF_SCAN (BTS)
		  && (PRIM_IS_NULL (key_val2p)
		      || btree_multicol_key_has_null (key_val2p)))
		{
		  /* skip this range and continue to the next */
		  iscan_id->curr_keyno++;
		  continue;
		}
	      key_val1p = NULL;
	    }

	  /* calling 'btree_range_search()' */
	  iscan_id->oid_list.oid_cnt = btree_range_search (thread_p,
							   &indx_infop->
							   indx_id.i.btid,
							   s_id->
							   readonly_scan,
							   iscan_id->
							   lock_hint, BTS,
							   key_val1p,
							   key_val2p, range,
							   1,
							   &iscan_id->cls_oid,
							   iscan_id->oid_list.
							   oidp,
							   (DB_PAGESIZE *
							    PRM_BT_OID_NBUFFERS),
							   &key_filter,
							   iscan_id, false,
							   iscan_id->
							   need_count_only);
	  if (iscan_id->oid_list.oid_cnt == -1)
	    {
	      goto exit_on_error;
	    }

	  /* We only want to advance the key ptr if we've exhausted the
	     current crop of oids on the current key. */
	  if (BTREE_END_OF_SCAN (BTS))
	    {
	      iscan_id->curr_keyno++;
	    }

	  if (iscan_id->oid_list.oid_cnt > 0)
	    {
	      /* we've got some result */
	      break;
	    }

	}

      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto exit_on_error;

    }

  if (iscan_id->oid_list.oid_cnt > 1)
    {				/* need to sort */
      if (iscan_id->keep_iscan_order == false
	  && iscan_id->need_count_only == false)
	{
	  qsort (iscan_id->oid_list.oidp, iscan_id->oid_list.oid_cnt,
		 sizeof (OID), oid_compare);
	}
    }

end:

  /* if the end of this scan */
  if (iscan_id->curr_keyno == key_cnt)
    {
      for (i = 0; i < key_cnt; i++)
	{
	  pr_clear_value (&key_vals[i].key1);
	  pr_clear_value (&key_vals[i].key2);
	}
      iscan_id->curr_keyno++;	/* to prevent duplicate frees */
    }				/* if (iscan_id->curr_keyno > key_cnt) */


  return ret;

exit_on_error:

  iscan_id->curr_keyno = key_cnt;	/* set as end of this scan */

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 *
 *                    SCAN MANAGEMENT ROUTINES
 *
 */

/*
 * scan_init_scan_id () -
 *   return:
 *   scan_id(out): Scan identifier
 *   readonly_scan(in):
 *   fixed(in):
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *
 * Note: If you feel the need
 */
static void
scan_init_scan_id (SCAN_ID * scan_id,
		   int readonly_scan,
		   int fixed,
		   int grouped,
		   QPROC_SINGLE_FETCH single_fetch,
		   DB_VALUE * join_dbval, VAL_LIST * val_list, VAL_DESCR * vd)
{
  scan_id->status = S_OPENED;
  scan_id->position = S_BEFORE;
  scan_id->direction = S_FORWARD;

  scan_id->readonly_scan = readonly_scan;
  scan_id->fixed = fixed;

  /* DO NOT EVER, UNDER ANY CIRCUMSTANCES, LET A SCAN BE NON-FIXED.  YOU
     WILL DIE A HIDEOUS DEATH, AND YOUR CHILDREN WILL BE SHUNNED FOR THE
     REST OF THEIR LIVES.
     YOU HAVE BEEN WARNED!!! */

  scan_id->grouped = grouped;	/* is it grouped or single scan? */
  scan_id->qualified_block = false;
  scan_id->single_fetch = single_fetch;
  scan_id->single_fetched = false;
  scan_id->null_fetched = false;
  scan_id->qualification = QPROC_QUALIFIED;

  /* join term */
  scan_id->join_dbval = join_dbval;

  /* value list and descriptor */
  scan_id->val_list = val_list;	/* points to the XASL tree */
  scan_id->vd = vd;		/* set value descriptor pointer */
}

/*
 * scan_open_heap_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   readonly_scan(in):
 *   fixed(in):
 *   lock_hint(in):
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   cls_oid(in):
 *   hfid(in):
 *   regu_list_pred(in):
 *   pr(in):
 *   regu_list_rest(in):
 *   num_attrs_pred(in):
 *   attrids_pred(in):
 *   cache_pred(in):
 *   num_attrs_rest(in):
 *   attrids_rest(in):
 *   cache_rest(in):
 *
 * Note: If you feel the need
 */
int
scan_open_heap_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		     /* fields of SCAN_ID */
		     int readonly_scan,
		     int fixed,
		     int lock_hint,
		     int grouped,
		     QPROC_SINGLE_FETCH single_fetch,
		     DB_VALUE * join_dbval, VAL_LIST * val_list,
		     VAL_DESCR * vd,
		     /* fields of HEAP_SCAN_ID */
		     OID * cls_oid,
		     HFID * hfid,
		     REGU_VARIABLE_LIST regu_list_pred,
		     PRED_EXPR * pr,
		     REGU_VARIABLE_LIST regu_list_rest,
		     int num_attrs_pred,
		     ATTR_ID * attrids_pred,
		     HEAP_CACHE_ATTRINFO * cache_pred,
		     int num_attrs_rest,
		     ATTR_ID * attrids_rest, HEAP_CACHE_ATTRINFO * cache_rest)
{
  HEAP_SCAN_ID *hsidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;


  /* scan type is HEAP SCAN */
  scan_id->type = S_HEAP_SCAN;

  /* initialize SCAN_ID structure */
  scan_init_scan_id (scan_id, readonly_scan, fixed, grouped, single_fetch,
		     join_dbval, val_list, vd);

  /* initialize HEAP_SCAN_ID structure */
  hsidp = &scan_id->s.hsid;

  /* class object OID */
  COPY_OID (&hsidp->cls_oid, cls_oid);
  /* heap file identifier */
  hsidp->hfid = *hfid;		/* bitwise copy */
  /* OID within the heap */
  UT_CAST_TO_NULL_HEAP_OID (&hsidp->hfid, &hsidp->curr_oid);

  /* scan predicates */
  INIT_SCAN_PRED (hsidp->scan_pred, regu_list_pred,
		  pr, (pr) ? eval_fnc (thread_p, pr,
				       &single_node_type) : NULL);
  /* attribute information from predicates */
  INIT_SCAN_ATTRS (hsidp->pred_attrs, num_attrs_pred,
		   attrids_pred, cache_pred);
  /* regulator vairable list for other than predicates */
  hsidp->rest_regu_list = regu_list_rest;
  /* attribute information from other than predicates */
  INIT_SCAN_ATTRS (hsidp->rest_attrs, num_attrs_rest,
		   attrids_rest, cache_rest);

  /* flags */
  /* do not reset hsidp->caches_inited here */
  hsidp->scancache_inited = false;
  hsidp->scanrange_inited = false;

  hsidp->lock_hint = lock_hint;

  return NO_ERROR;
}

/*
 * scan_open_class_attr_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   cls_oid(in):
 *   hfid(in):
 *   regu_list_pred(in):
 *   pr(in):
 *   regu_list_rest(in):
 *   num_attrs_pred(in):
 *   attrids_pred(in):
 *   cache_pred(in):
 *   num_attrs_rest(in):
 *   attrids_rest(in):
 *   cache_rest(in):
 *
 * Note: If you feel the need
 */
int
scan_open_class_attr_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
			   /* fields of SCAN_ID */
			   int grouped,
			   QPROC_SINGLE_FETCH single_fetch,
			   DB_VALUE * join_dbval,
			   VAL_LIST * val_list, VAL_DESCR * vd,
			   /* fields of HEAP_SCAN_ID */
			   OID * cls_oid,
			   HFID * hfid,
			   REGU_VARIABLE_LIST regu_list_pred,
			   PRED_EXPR * pr,
			   REGU_VARIABLE_LIST regu_list_rest,
			   int num_attrs_pred,
			   ATTR_ID * attrids_pred,
			   HEAP_CACHE_ATTRINFO * cache_pred,
			   int num_attrs_rest,
			   ATTR_ID * attrids_rest,
			   HEAP_CACHE_ATTRINFO * cache_rest)
{
  HEAP_SCAN_ID *hsidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;

  /* scan type is CLASS ATTR SCAN */
  scan_id->type = S_CLASS_ATTR_SCAN;

  /* initialize SCAN_ID structure */
  /* readonly_scan = true, fixed = true */
  scan_init_scan_id (scan_id, true, true, grouped, single_fetch,
		     join_dbval, val_list, vd);

  /* initialize HEAP_SCAN_ID structure */
  hsidp = &scan_id->s.hsid;

  /* class object OID */
  COPY_OID (&hsidp->cls_oid, cls_oid);
  /* heap file identifier */
  hsidp->hfid = *hfid;		/* bitwise copy */
  /* OID within the heap */
  UT_CAST_TO_NULL_HEAP_OID (&hsidp->hfid, &hsidp->curr_oid);

  /* scan predicates */
  INIT_SCAN_PRED (hsidp->scan_pred, regu_list_pred,
		  pr, (pr) ? eval_fnc (thread_p, pr,
				       &single_node_type) : NULL);
  /* attribute information from predicates */
  INIT_SCAN_ATTRS (hsidp->pred_attrs, num_attrs_pred,
		   attrids_pred, cache_pred);
  /* regulator vairable list for other than predicates */
  hsidp->rest_regu_list = regu_list_rest;
  /* attribute information from other than predicates */
  INIT_SCAN_ATTRS (hsidp->rest_attrs, num_attrs_rest,
		   attrids_rest, cache_rest);

  /* flags */
  /* do not reset hsidp->caches_inited here */
  hsidp->scancache_inited = false;
  hsidp->scanrange_inited = false;

  return NO_ERROR;
}

/*
 * scan_open_index_scan () -
 *   return: NO_ERROR, or ER_code
 *   scan_id(out): Scan identifier
 *   readonly_scan(in):
 *   fixed(in):
 *   lock_hint(in):
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   indx_info(in):
 *   cls_oid(in):
 *   hfid(in):
 *   regu_list_key(in):
 *   pr_key(in):
 *   regu_list_pred(in):
 *   pr(in):
 *   regu_list_rest(in):
 *   num_attrs_key(in):
 *   attrids_key(in):
 *   num_attrs_pred(in):
 *   attrids_pred(in):
 *   cache_pred(in):
 *   num_attrs_rest(in):
 *   attrids_rest(in):
 *   cache_rest(in):
 *   keep_index_scan_order(in):
 *
 * Note: If you feel the need
 */
int
scan_open_index_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		      /* fields of SCAN_ID */
		      int readonly_scan,
		      int fixed,
		      int lock_hint,
		      int grouped,
		      QPROC_SINGLE_FETCH single_fetch,
		      DB_VALUE * join_dbval,
		      VAL_LIST * val_list, VAL_DESCR * vd,
		      /* fields of INDX_SCAN_ID */
		      INDX_INFO * indx_info,
		      OID * cls_oid,
		      HFID * hfid,
		      REGU_VARIABLE_LIST regu_list_key,
		      PRED_EXPR * pr_key,
		      REGU_VARIABLE_LIST regu_list_pred,
		      PRED_EXPR * pr,
		      REGU_VARIABLE_LIST regu_list_rest,
		      int num_attrs_key,
		      ATTR_ID * attrids_key,
		      HEAP_CACHE_ATTRINFO * cache_key,
		      int num_attrs_pred,
		      ATTR_ID * attrids_pred,
		      HEAP_CACHE_ATTRINFO * cache_pred,
		      int num_attrs_rest,
		      ATTR_ID * attrids_rest,
		      HEAP_CACHE_ATTRINFO * cache_rest, bool keep_iscan_order)
{
  int ret = NO_ERROR;
  INDX_SCAN_ID *isidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  BTID *btid;
  VPID Root_vpid;
  PAGE_PTR Root;
  RECDES Rec;
  BTREE_ROOT_HEADER root_header;
  BTREE_SCAN *BTS;

  /* scan type is INDEX SCAN */
  scan_id->type = S_INDX_SCAN;

  /* initialize SCAN_ID structure */
  scan_init_scan_id (scan_id, readonly_scan, fixed, grouped, single_fetch,
		     join_dbval, val_list, vd);

  /* read Root page heder info */
  btid = &indx_info->indx_id.i.btid;

  Root_vpid.pageid = btid->root_pageid;
  Root_vpid.volid = btid->vfid.volid;

  Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL)
    {
      return ER_FAILED;
    }
  if (spage_get_record (Root, HEADER, &Rec, PEEK) != S_SUCCESS)
    {
      pgbuf_unfix (thread_p, Root);
      return ER_FAILED;
    }
  btree_read_root_header (&Rec, &root_header);
  pgbuf_unfix (thread_p, Root);

  /* initialize INDEX_SCAN_ID structure */
  isidp = &scan_id->s.isid;

  /* index information */
  isidp->indx_info = indx_info;

  /* init alloced fields */
  isidp->bt_num_attrs = 0;
  isidp->bt_attr_ids = NULL;
  isidp->vstr_ids = NULL;
  isidp->oid_list.oidp = NULL;
  isidp->copy_buf = NULL;
  isidp->copy_buf_len = 0;

  isidp->key_vals = NULL;

  BTS = &isidp->bt_scan;

  /* construct BTID_INT structure */
  BTS->btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (&root_header, &BTS->btid_int) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* attribute information of the index key */
  if (heap_get_attrids_of_btid_key
      (thread_p, cls_oid, &indx_info->indx_id.i.btid, &isidp->bt_type,
       &isidp->bt_num_attrs, &isidp->bt_attr_ids) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* attribute information of the variable string attrs in index key */
  isidp->num_vstr = 0;
  isidp->vstr_ids = NULL;

  if (PRM_ORACLE_STYLE_EMPTY_STRING)
    {
      isidp->num_vstr = isidp->bt_num_attrs;	/* init to maximum */
      isidp->vstr_ids = (ATTR_ID *) db_private_alloc (thread_p,
						      isidp->num_vstr *
						      sizeof (ATTR_ID));
      if (isidp->vstr_ids == NULL)
	{
	  goto exit_on_error;
	}
    }

  /* index scan info */
  BTREE_INIT_SCAN (&isidp->bt_scan);
  /* is a single range? */
  isidp->one_range = false;
  /* initial values */
  isidp->curr_keyno = -1;
  isidp->curr_oidno = -1;
  /* OID buffer */
  isidp->oid_list.oid_cnt = 0;
  isidp->oid_list.oidp = alloc_iscan_oid_buf_list ();
  if (isidp->oid_list.oidp == NULL)
    {
      goto exit_on_error;
    }
  isidp->curr_oidp = isidp->oid_list.oidp;
  /* class object OID */
  COPY_OID (&isidp->cls_oid, cls_oid);
  /* heap file identifier */
  isidp->hfid = *hfid;		/* bitwise copy */

  /* key filter */
  INIT_SCAN_PRED (isidp->key_pred, regu_list_key,
		  pr_key,
		  (pr_key) ? eval_fnc (thread_p, pr_key,
				       &single_node_type) : NULL);
  /* attribute information from key filter */
  INIT_SCAN_ATTRS (isidp->key_attrs, num_attrs_key, attrids_key, cache_key);
  /* scan predicates */
  INIT_SCAN_PRED (isidp->scan_pred, regu_list_pred,
		  pr, (pr) ? eval_fnc (thread_p, pr,
				       &single_node_type) : NULL);
  /* attribute information from predicates */
  INIT_SCAN_ATTRS (isidp->pred_attrs, num_attrs_pred,
		   attrids_pred, cache_pred);
  /* regulator vairable list for other than predicates */
  isidp->rest_regu_list = regu_list_rest;
  /* attribute information from other than predicates */
  INIT_SCAN_ATTRS (isidp->rest_attrs, num_attrs_rest,
		   attrids_rest, cache_rest);

  /* flags */
  /* do not reset hsidp->caches_inited here */
  isidp->scancache_inited = false;
  /* convert key values in the form of REGU_VARIABLE to the form of DB_VALUE */
  isidp->key_cnt = indx_info->key_info.key_cnt;
  if (isidp->key_cnt > 0)
    {
      bool need_copy_buf;

      isidp->key_vals =
	(KEY_VAL_RANGE *) db_private_alloc (thread_p,
					    isidp->key_cnt *
					    sizeof (KEY_VAL_RANGE));
      if (isidp->key_vals == NULL)
	{
	  goto exit_on_error;
	}

      need_copy_buf = false;	/* init */

      if (BTS->btid_int.key_type == NULL ||
	  BTS->btid_int.key_type->type == NULL)
	{
	  goto exit_on_error;
	}

      /* check for the need of index key copy_buf */
      if (BTS->btid_int.key_type->type->id == DB_TYPE_MIDXKEY)
	{
	  /* found multi-column key-val */
	  need_copy_buf = true;
	}
      else
	{			/* single-column index */
	  if (indx_info->key_info.key_ranges[0].range != EQ_NA)
	    {
	      /* found single-column key-range, not key-val */
	      if (QSTR_IS_ANY_CHAR_OR_BIT (BTS->btid_int.key_type->type->id))
		{
		  /* this type needs index key copy_buf */
		  need_copy_buf = true;
		}
	    }
	}			/* else */

      if (need_copy_buf)
	{
	  /* alloc index key copy_buf */
	  isidp->copy_buf =
	    (char *) db_private_alloc (thread_p, DBVAL_BUFSIZE);
	  if (isidp->copy_buf == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, DBVAL_BUFSIZE);
	      goto exit_on_error;
	    }
	  isidp->copy_buf_len = DBVAL_BUFSIZE;
	}
    }
  else
    {
      isidp->key_cnt = 0;
      isidp->key_vals = NULL;
    }

  isidp->keep_iscan_order = keep_iscan_order;
  isidp->lock_hint = lock_hint;

end:

  return ret;

exit_on_error:

  if (isidp->bt_attr_ids)
    {
      db_private_free (thread_p, isidp->bt_attr_ids);
    }
  if (isidp->vstr_ids)
    {
      db_private_free (thread_p, isidp->vstr_ids);
    }
  if (isidp->oid_list.oidp)
    {
      db_private_free (thread_p, isidp->oid_list.oidp);
    }
  if (isidp->key_vals)
    {
      db_private_free (thread_p, isidp->key_vals);
    }
  /* free index key copy_buf */
  if (isidp->copy_buf)
    {
      db_private_free (thread_p, isidp->copy_buf);
    }

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * scan_open_list_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   list_id(in):
 *   regu_list_pred(in):
 *   pr(in):
 *   regu_list_rest(in):
 */
int
scan_open_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		     /* fields of SCAN_ID */
		     int grouped,
		     QPROC_SINGLE_FETCH single_fetch,
		     DB_VALUE * join_dbval, VAL_LIST * val_list,
		     VAL_DESCR * vd,
		     /* fields of LLIST_SCAN_ID */
		     QFILE_LIST_ID * list_id,
		     REGU_VARIABLE_LIST regu_list_pred,
		     PRED_EXPR * pr, REGU_VARIABLE_LIST regu_list_rest)
{
  LLIST_SCAN_ID *llsidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;


  /* scan type is LIST SCAN */
  scan_id->type = S_LIST_SCAN;

  /* initialize SCAN_ID structure */
  /* readonly_scan = true, fixed = true */
  scan_init_scan_id (scan_id, true, true, grouped, single_fetch,
		     join_dbval, val_list, vd);

  /* initialize LLIST_SCAN_ID structure */
  llsidp = &scan_id->s.llsid;

  /* list file ID */
  llsidp->list_id = list_id;	/* points to XASL tree */

  /* scan predicates */
  INIT_SCAN_PRED (llsidp->scan_pred, regu_list_pred,
		  pr, (pr) ? eval_fnc (thread_p, pr,
				       &single_node_type) : NULL);
  /* regulator vairable list for other than predicates */
  llsidp->rest_regu_list = regu_list_rest;

  return NO_ERROR;
}

/*
 * scan_open_set_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   set_ptr(in):
 *   regu_list_pred(in):
 *   pr(in):
 */
int
scan_open_set_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		    /* fields of SCAN_ID */
		    int grouped,
		    QPROC_SINGLE_FETCH single_fetch,
		    DB_VALUE * join_dbval, VAL_LIST * val_list,
		    VAL_DESCR * vd,
		    /* fields of SET_SCAN_ID */
		    REGU_VARIABLE * set_ptr,
		    REGU_VARIABLE_LIST regu_list_pred, PRED_EXPR * pr)
{
  SET_SCAN_ID *ssidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;


  /* scan type is SET SCAN */
  scan_id->type = S_SET_SCAN;

  /* initialize SCAN_ID structure */
  /* readonly_scan = true, fixed = true */
  scan_init_scan_id (scan_id, true, true, grouped, single_fetch,
		     join_dbval, val_list, vd);

  /* initialize SET_SCAN_ID structure */
  ssidp = &scan_id->s.ssid;

  ssidp->set_ptr = set_ptr;	/* points to XASL tree */

  /* scan predicates */
  INIT_SCAN_PRED (ssidp->scan_pred, regu_list_pred,
		  pr, (pr) ? eval_fnc (thread_p, pr,
				       &single_node_type) : NULL);

  return NO_ERROR;
}

/*
 * scan_open_method_scan () -
 *   return: NO_ERROR, or ER_code
 *   scan_id(out): Scan identifier
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   list_id(in):
 *   meth_sig_list(in):
 *
 * Note: If you feel the need
 */
int
scan_open_method_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		       /* fields of SCAN_ID */
		       int grouped,
		       QPROC_SINGLE_FETCH single_fetch,
		       DB_VALUE * join_dbval,
		       VAL_LIST * val_list, VAL_DESCR * vd,
		       /* */
		       QFILE_LIST_ID * list_id,
		       METHOD_SIG_LIST * meth_sig_list)
{
  /* scan type is METHOD SCAN */
  scan_id->type = S_METHOD_SCAN;

  /* initialize SCAN_ID structure */
  /* readonly_scan = true, fixed = true */
  scan_init_scan_id (scan_id, true, true, grouped, single_fetch,
		     join_dbval, val_list, vd);

  return method_open_scan (thread_p, &scan_id->s.vaid.scan_buf, list_id,
			   meth_sig_list);
}

/*
 * scan_start_scan () - Start the scan process on the given scan identifier.
 *   return: NO_ERROR, or ER_code
 *   scan_id(out): Scan identifier
 *
 * Note: If you feel the need
 */
int
scan_start_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  int ret = NO_ERROR;
  HEAP_SCAN_ID *hsidp;
  INDX_SCAN_ID *isidp;
  LLIST_SCAN_ID *llsidp;
  SET_SCAN_ID *ssidp;


  switch (scan_id->type)
    {

    case S_HEAP_SCAN:

      hsidp = &scan_id->s.hsid;
      UT_CAST_TO_NULL_HEAP_OID (&hsidp->hfid, &hsidp->curr_oid);
      if (scan_id->grouped)
	{
	  ret = heap_scanrange_start (thread_p, &hsidp->scan_range,
				      &hsidp->hfid,
				      &hsidp->cls_oid, hsidp->lock_hint);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->scanrange_inited = true;
	}
      else
	{
	  /* A new argument(is_indexscan = false) is appended */
	  ret = heap_scancache_start (thread_p, &hsidp->scan_cache,
				      &hsidp->hfid,
				      &hsidp->cls_oid, scan_id->fixed,
				      false, hsidp->lock_hint);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->scancache_inited = true;
	}
      if (hsidp->caches_inited != true)
	{
	  hsidp->pred_attrs.attr_cache->num_values = -1;
	  ret = heap_attrinfo_start (thread_p, &hsidp->cls_oid,
				     hsidp->pred_attrs.num_attrs,
				     hsidp->pred_attrs.attr_ids,
				     hsidp->pred_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->rest_attrs.attr_cache->num_values = -1;
	  ret = heap_attrinfo_start (thread_p, &hsidp->cls_oid,
				     hsidp->rest_attrs.num_attrs,
				     hsidp->rest_attrs.attr_ids,
				     hsidp->rest_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      heap_attrinfo_end (thread_p, hsidp->pred_attrs.attr_cache);
	      goto exit_on_error;
	    }
	  hsidp->caches_inited = true;
	}
      break;

    case S_CLASS_ATTR_SCAN:
      hsidp = &scan_id->s.hsid;
      hsidp->pred_attrs.attr_cache->num_values = -1;
      if (hsidp->caches_inited != true)
	{
	  ret = heap_attrinfo_start (thread_p, &hsidp->cls_oid,
				     hsidp->pred_attrs.num_attrs,
				     hsidp->pred_attrs.attr_ids,
				     hsidp->pred_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->rest_attrs.attr_cache->num_values = -1;
	  ret = heap_attrinfo_start (thread_p, &hsidp->cls_oid,
				     hsidp->rest_attrs.num_attrs,
				     hsidp->rest_attrs.attr_ids,
				     hsidp->rest_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      heap_attrinfo_end (thread_p, hsidp->pred_attrs.attr_cache);
	      goto exit_on_error;
	    }
	  hsidp->caches_inited = true;
	}
      break;

    case S_INDX_SCAN:

      isidp = &scan_id->s.isid;
      /* A new argument(is_indexscan = true) is appended */
      ret = heap_scancache_start (thread_p, &isidp->scan_cache,
				  &isidp->hfid,
				  &isidp->cls_oid, scan_id->fixed,
				  true, isidp->lock_hint);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      isidp->scancache_inited = true;
      if (isidp->caches_inited != true)
	{
	  if (isidp->key_pred.regu_list != NULL)
	    {
	      isidp->key_attrs.attr_cache->num_values = -1;
	      ret = heap_attrinfo_start (thread_p, &isidp->cls_oid,
					 isidp->key_attrs.num_attrs,
					 isidp->key_attrs.attr_ids,
					 isidp->key_attrs.attr_cache);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  isidp->pred_attrs.attr_cache->num_values = -1;
	  ret = heap_attrinfo_start (thread_p, &isidp->cls_oid,
				     isidp->pred_attrs.num_attrs,
				     isidp->pred_attrs.attr_ids,
				     isidp->pred_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      if (isidp->key_pred.regu_list != NULL)
		{
		  heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
		}
	      goto exit_on_error;
	    }
	  isidp->rest_attrs.attr_cache->num_values = -1;
	  ret = heap_attrinfo_start (thread_p, &isidp->cls_oid,
				     isidp->rest_attrs.num_attrs,
				     isidp->rest_attrs.attr_ids,
				     isidp->rest_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      if (isidp->key_pred.regu_list != NULL)
		{
		  heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
		}
	      heap_attrinfo_end (thread_p, isidp->pred_attrs.attr_cache);
	      goto exit_on_error;
	    }
	  isidp->caches_inited = true;
	}
      isidp->oid_list.oid_cnt = 0;
      isidp->curr_keyno = -1;
      isidp->curr_oidno = -1;
      BTREE_INIT_SCAN (&isidp->bt_scan);
      isidp->one_range = false;
      break;

    case S_LIST_SCAN:

      llsidp = &scan_id->s.llsid;
      /* open list file scan */
      if (qfile_open_list_scan (llsidp->list_id, &llsidp->lsid) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      qfile_start_scan_fix (thread_p, &llsidp->lsid);
      break;

    case S_SET_SCAN:
      ssidp = &scan_id->s.ssid;
      DB_MAKE_NULL (&ssidp->set);
      break;

    case S_METHOD_SCAN:
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto exit_on_error;
    }				/* switch (scan_id->type) */

  /* set scan status as started */
  scan_id->position = S_BEFORE;
  scan_id->direction = S_FORWARD;
  scan_id->status = S_STARTED;
  scan_id->qualified_block = false;
  scan_id->single_fetched = false;
  scan_id->null_fetched = false;

end:

  return ret;

exit_on_error:

  if (ret == NO_ERROR)
    {
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  ret = ER_FAILED;
	}
    }
  goto end;
}

/*
 * scan_reset_scan_block () - Move the scan back to the beginning point inside the current scan block.
 *   return: S_SUCCESS, S_END, S_ERROR
 *   s_id(in/out): Scan identifier
 *
 * Note: If you feel the need
 */
SCAN_CODE
scan_reset_scan_block (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SCAN_CODE status = S_SUCCESS;

  s_id->single_fetched = false;
  s_id->null_fetched = false;

  switch (s_id->type)
    {
    case S_HEAP_SCAN:
      if (s_id->grouped)
	{
	  OID_SET_NULL (&s_id->s.hsid.curr_oid);
	}
      else
	{
	  s_id->position =
	    (s_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
	  OID_SET_NULL (&s_id->s.hsid.curr_oid);
	}
      break;

    case S_INDX_SCAN:
      if (s_id->grouped)
	{
	  if (s_id->direction == S_FORWARD
	      && s_id->s.isid.keep_iscan_order == false)
	    {
	      s_id->s.isid.curr_oidno = s_id->s.isid.oid_list.oid_cnt;
	      s_id->direction = S_BACKWARD;
	    }
	  else
	    {
	      s_id->s.isid.curr_oidno = -1;
	      s_id->direction = S_FORWARD;
	    }
	}
      else
	{
	  s_id->s.isid.curr_oidno = -1;
	  s_id->s.isid.curr_keyno = -1;
	  s_id->position = S_BEFORE;
	  BTREE_INIT_SCAN (&s_id->s.isid.bt_scan);
	}
      break;

    case S_LIST_SCAN:
      /* may have scanned some already so clean up */
      qfile_end_scan_fix (thread_p, &s_id->s.llsid.lsid);
      qfile_close_scan (thread_p, &s_id->s.llsid.lsid);

      /* open list file scan for this outer row */
      if (qfile_open_list_scan (s_id->s.llsid.list_id, &s_id->s.llsid.lsid)
	  != NO_ERROR)
	{
	  status = S_ERROR;
	  break;
	}
      qfile_start_scan_fix (thread_p, &s_id->s.llsid.lsid);
      s_id->position = S_BEFORE;
      s_id->s.llsid.lsid.position = S_BEFORE;
      break;

    case S_CLASS_ATTR_SCAN:
    case S_SET_SCAN:
      s_id->position = S_BEFORE;
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      status = S_ERROR;
      break;
    }				/* switch (s_id->type) */

  return status;

}

/*
 * scan_next_scan_block () - Move the scan to the next scan block. If there are no more scan blocks left, S_END is returned.
 *   return: S_SUCCESS, S_END, S_ERROR
 *   s_id(in/out): Scan identifier
 *
 * Note: If you feel the need
 */
SCAN_CODE
scan_next_scan_block (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  SCAN_CODE sp_scan;

  s_id->single_fetched = false;
  s_id->null_fetched = false;
  s_id->qualified_block = false;

  switch (s_id->type)
    {
    case S_HEAP_SCAN:
      if (s_id->grouped)
	{			/* grouped, fixed scan */

	  if (s_id->direction == S_FORWARD)
	    {
	      sp_scan = heap_scanrange_to_following (thread_p,
						     &s_id->s.hsid.scan_range,
						     NULL);
	    }
	  else
	    {
	      sp_scan = heap_scanrange_to_prior (thread_p,
						 &s_id->s.hsid.scan_range,
						 NULL);
	    }

	  return ((sp_scan == S_SUCCESS) ? S_SUCCESS :
		  (sp_scan == S_END) ? S_END : S_ERROR);
	}
      else
	{

	  return ((s_id->direction == S_FORWARD) ?
		  ((s_id->position == S_BEFORE) ? S_SUCCESS : S_END) :
		  ((s_id->position == S_AFTER) ? S_SUCCESS : S_END));
	}

    case S_INDX_SCAN:
      if (s_id->grouped)
	{

	  if ((s_id->direction == S_FORWARD && s_id->position == S_BEFORE)
	      || (!BTREE_END_OF_SCAN (&s_id->s.isid.bt_scan)
		  || s_id->s.isid.indx_info->range_type == R_KEYLIST
		  || s_id->s.isid.indx_info->range_type == R_RANGELIST))
	    {

	      if (!(s_id->position == S_BEFORE
		    && s_id->s.isid.one_range == true))
		{
		  /* get the next set of object identifiers specified in the range */
		  if (scan_get_index_oidset (thread_p, s_id) != NO_ERROR)
		    {
		      return S_ERROR;
		    }
		  if (s_id->s.isid.oid_list.oid_cnt == 0)
		    {		/* range is empty */
		      s_id->position = S_AFTER;
		      return S_END;
		    }

		  if (s_id->position == S_BEFORE
		      && BTREE_END_OF_SCAN (&s_id->s.isid.bt_scan)
		      && s_id->s.isid.indx_info->range_type != R_KEYLIST
		      && s_id->s.isid.indx_info->range_type != R_RANGELIST)
		    {
		      s_id->s.isid.one_range = true;
		    }
		}

	      if (s_id->s.isid.keep_iscan_order == false)
		{
		  s_id->position = S_ON;
		  s_id->direction = S_BACKWARD;
		  s_id->s.isid.curr_oidno = s_id->s.isid.oid_list.oid_cnt;
		}
	      return S_SUCCESS;

	    }
	  else
	    {
	      s_id->position = S_AFTER;
	      return S_END;
	    }

	}
      else
	{

	  return ((s_id->position == S_BEFORE) ? S_SUCCESS : S_END);
	}

    case S_CLASS_ATTR_SCAN:
    case S_LIST_SCAN:
    case S_SET_SCAN:
    case S_METHOD_SCAN:
      return (s_id->position == S_BEFORE) ? S_SUCCESS : S_END;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return S_ERROR;
    }				/* switch (s_id->type) */

}


/*
 * scan_end_scan () - End the scan process on the given scan identifier.
 *   return:
 *   scan_id(in/out): Scan identifier
 *
 * Note: If you feel the need
 */
void
scan_end_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  HEAP_SCAN_ID *hsidp;
  INDX_SCAN_ID *isidp;
  LLIST_SCAN_ID *llsidp;
  SET_SCAN_ID *ssidp;
  KEY_VAL_RANGE *key_vals;
  int i;

  if ((scan_id->status == S_ENDED) || (scan_id->status == S_CLOSED))
    {
      return;
    }

  switch (scan_id->type)
    {

    case S_HEAP_SCAN:
      hsidp = &scan_id->s.hsid;

      /* do not free attr_cache here.
       * xs_clear_access_spec_list() will free attr_caches.
       */

      if (scan_id->grouped)
	{
	  if (hsidp->scanrange_inited)
	    {
	      heap_scanrange_end (thread_p, &hsidp->scan_range);
	    }
	}
      else
	{
	  if (hsidp->scancache_inited)
	    {
	      (void) heap_scancache_end (thread_p, &hsidp->scan_cache);
	    }
	}
      /* switch scan direction for further iterations */
      scan_id->direction = (scan_id->direction == S_FORWARD) ? S_BACKWARD
	: S_FORWARD;
      break;

    case S_CLASS_ATTR_SCAN:
      /* do not free attr_cache here.
       * xs_clear_access_spec_list() will free attr_caches.
       */

      break;

    case S_INDX_SCAN:
      isidp = &scan_id->s.isid;
      /* do not free attr_cache here.
       * xs_clear_access_spec_list() will free attr_caches.
       */

      if (isidp->scancache_inited)
	{
	  (void) heap_scancache_end (thread_p, &isidp->scan_cache);
	}
      if (isidp->curr_keyno >= 0 && isidp->curr_keyno < isidp->key_cnt)
	{
	  key_vals = isidp->key_vals;
	  for (i = 0; i < isidp->key_cnt; i++)
	    {
	      pr_clear_value (&key_vals[i].key1);
	      pr_clear_value (&key_vals[i].key2);
	    }
	}
      /* clear all the used keys */
      btree_scan_clear_key (&(isidp->bt_scan));
      break;

    case S_LIST_SCAN:
      llsidp = &scan_id->s.llsid;
      qfile_end_scan_fix (thread_p, &llsidp->lsid);
      qfile_close_scan (thread_p, &llsidp->lsid);
      break;

    case S_SET_SCAN:
      ssidp = &scan_id->s.ssid;
      pr_clear_value (&ssidp->set);
      break;

    case S_METHOD_SCAN:
      break;
    }

  scan_id->status = S_ENDED;
}


/*
 * scan_close_scan () - The scan identifier is closed and allocated areas and page buffers are freed.
 *   return:
 *   scan_id(in/out): Scan identifier
 *
 * Note: If you feel the need
 */
void
scan_close_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  INDX_SCAN_ID *isidp;


  if (scan_id->status == S_CLOSED)
    {
      return;
    }

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
    case S_CLASS_ATTR_SCAN:
      break;

    case S_INDX_SCAN:
      isidp = &scan_id->s.isid;
      /* free allocated memory for the scan */
      if (isidp->bt_attr_ids)
	{
	  db_private_free (thread_p, isidp->bt_attr_ids);
	}
      if (isidp->vstr_ids)
	{
	  db_private_free (thread_p, isidp->vstr_ids);
	}
      if (isidp->oid_list.oidp)
	{
	  free_iscan_oid_buf_list (isidp->oid_list.oidp);
	}
      if (isidp->key_vals)
	{
	  db_private_free (thread_p, isidp->key_vals);
	}
      /* free index key copy_buf */
      if (isidp->copy_buf)
	{
	  db_private_free (thread_p, isidp->copy_buf);
	}
      break;

    case S_LIST_SCAN:
      break;

    case S_SET_SCAN:
      break;

    case S_METHOD_SCAN:
      method_close_scan (thread_p, &scan_id->s.vaid.scan_buf);
      break;
    }				/* switch (scan_id->type) */

  scan_id->status = S_CLOSED;
}

/*
 * scan_next_scan_local () - The scan is moved to the next scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_scan_local (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  HEAP_SCAN_ID *hsidp;
  INDX_SCAN_ID *isidp;
  LLIST_SCAN_ID *llsidp;
  SET_SCAN_ID *ssidp;
  VA_SCAN_ID *vaidp;
  FILTER_INFO data_filter;
  SCAN_CODE sp_scan;
  SCAN_CODE qp_scan;
  DB_LOGICAL ev_res;
  OID class_oid;
  RECDES recdes;
  QFILE_TUPLE_RECORD tplrec;
  VAL_LIST vl;
  QPROC_DB_VALUE_LIST src_valp;
  QPROC_DB_VALUE_LIST dest_valp;
  TRAN_ISOLATION isolation;


  switch (scan_id->type)
    {

    case S_HEAP_SCAN:
      hsidp = &scan_id->s.hsid;
      /* set data filter information */
      INIT_DATA_FILTER_INFO (data_filter, &hsidp->scan_pred,
			     &hsidp->pred_attrs,
			     scan_id->val_list, scan_id->vd, &hsidp->cls_oid);

      while (1)
	{

	  /* get next object */
	  if (scan_id->grouped)
	    {
	      /* grouped, fixed scan */
	      sp_scan =
		heap_scanrange_next (thread_p, &hsidp->curr_oid, &recdes,
				     &hsidp->scan_range, PEEK);
	    }
	  else
	    {
	      /* regular, fixed scan */
	      if (scan_id->fixed == false)
		{
		  recdes.data = NULL;
		}
	      if (scan_id->direction == S_FORWARD)
		{
		  /* move forward */
		  sp_scan =
		    heap_next (thread_p, &hsidp->hfid, &hsidp->cls_oid,
			       &hsidp->curr_oid, &recdes, &hsidp->scan_cache,
			       scan_id->fixed);
		}
	      else
		{
		  /* move backward */
		  sp_scan =
		    heap_prev (thread_p, &hsidp->hfid, &hsidp->cls_oid,
			       &hsidp->curr_oid, &recdes, &hsidp->scan_cache,
			       scan_id->fixed);
		}
	    }
	  if (sp_scan != S_SUCCESS)
	    {
	      /* scan error or end of scan */
	      return (sp_scan == S_END) ? S_END : S_ERROR;
	    }

	  /* evaluate the predicates to see if the object qualifies */
	  ev_res =
	    eval_data_filter (thread_p, &hsidp->curr_oid, &recdes,
			      &data_filter);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }

	  if (scan_id->qualification == QPROC_QUALIFIED)
	    {
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	    {
	      if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
		{
		  continue;	/* qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_QUALIFIED_OR_NOT)
	    {
	      if (ev_res == V_TRUE)
		{
		  scan_id->qualification = QPROC_QUALIFIED;
		}
	      else if (ev_res == V_FALSE)
		{
		  scan_id->qualification = QPROC_NOT_QUALIFIED;
		}
	      else		/* V_UNKNOWN */
		{
		  /* nop */
		  ;
		}
	    }
	  else
	    {			/* invalid value; the same as QPROC_QUALIFIED */
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }

	  if (hsidp->rest_regu_list)
	    {
	      /* read the rest of the values from the heap into the attribute
	         cache */
	      if (heap_attrinfo_read_dbvalues
		  (thread_p, &hsidp->curr_oid, &recdes,
		   hsidp->rest_attrs.attr_cache) != NO_ERROR)
		{
		  return S_ERROR;
		}
	      /* fetch the rest of the values from the object instance */
	      if (scan_id->val_list)
		{
		  if (fetch_val_list
		      (thread_p, hsidp->rest_regu_list, scan_id->vd,
		       &hsidp->cls_oid, &hsidp->curr_oid, NULL,
		       PEEK) != NO_ERROR)
		    {
		      return S_ERROR;
		    }
		}
	    }			/* if (hsidp->rest_regu_list) */

	  return S_SUCCESS;

	}			/* while (1) */

      break;			/* cannot reach to this line */

    case S_CLASS_ATTR_SCAN:

      hsidp = &scan_id->s.hsid;
      /* set data filter information */
      INIT_DATA_FILTER_INFO (data_filter, &hsidp->scan_pred,
			     &hsidp->pred_attrs,
			     scan_id->val_list, scan_id->vd, &hsidp->cls_oid);

      if (scan_id->position == S_BEFORE)
	{
	  /* Class attribute scans are always single row scan. */
	  scan_id->position = S_AFTER;

	  /* evaluate the predicates to see if the object qualifies */
	  ev_res = eval_data_filter (thread_p, NULL, NULL, &data_filter);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }

	  if (scan_id->qualification == QPROC_QUALIFIED)
	    {
	      if (ev_res != V_TRUE)
		{		/* V_FALSE || V_UNKNOWN */
		  return S_END;	/* not qualified */
		}
	    }
	  else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	    {
	      if (ev_res != V_FALSE)
		{		/* V_TRUE || V_UNKNOWN */
		  return S_END;	/* qualified */
		}
	    }
	  else if (scan_id->qualification == QPROC_QUALIFIED_OR_NOT)
	    {
	      if (ev_res == V_TRUE)
		{
		  scan_id->qualification = QPROC_QUALIFIED;
		}
	      else if (ev_res == V_FALSE)
		{
		  scan_id->qualification = QPROC_NOT_QUALIFIED;
		}
	      else		/* V_UNKNOWN */
		{
		  /* nop */
		  ;
		}
	    }
	  else
	    {			/* invalid value; the same as QPROC_QUALIFIED */
	      if (ev_res != V_TRUE)
		{		/* V_FALSE || V_UNKNOWN */
		  return S_END;	/* not qualified */
		}
	    }

	  if (hsidp->rest_regu_list)
	    {
	      /* read the rest of the values from the heap into the attribute
	         cache */
	      if (heap_attrinfo_read_dbvalues (thread_p, NULL, NULL,
					       hsidp->rest_attrs.attr_cache)
		  != NO_ERROR)
		{
		  return S_ERROR;
		}

	      /* fetch the rest of the values from the object instance */
	      if (scan_id->val_list)
		{
		  if (fetch_val_list
		      (thread_p, hsidp->rest_regu_list, scan_id->vd,
		       &hsidp->cls_oid, NULL, NULL, PEEK) != NO_ERROR)
		    {
		      return S_ERROR;
		    }
		}
	    }			/* if (hsidp->rest_regu_list) */

	  return S_SUCCESS;

	}
      else
	{			/* if (scan_id->position == S_BEFORE) */

	  /* Class attribute scans are always single row scan. */
	  return S_END;
	}			/* if (scan_id->position == S_BEFORE) */

      break;			/* cannot reach to this line */

    case S_INDX_SCAN:

      isidp = &scan_id->s.isid;
      /* set data filter information */
      INIT_DATA_FILTER_INFO (data_filter, &isidp->scan_pred,
			     &isidp->pred_attrs,
			     scan_id->val_list, scan_id->vd, &isidp->cls_oid);

      /* Due to the length of time that we hold onto the oid list, it is
         possible at lower isolation levels (UNCOMMITTED INSTANCES) that
         the index/heap may have changed since the oid list was read from
         the btree.  In particular, some of the instances that we are
         reading may have been deleted by the time we go to fetch them via
         heap_get_with_class_oid().  According to the semantics of
         UNCOMMITTED, it is okay if they are deleted out from under us and
         we can ignore the SCAN_DOESNT_EXIST error. */
      isolation = logtb_find_current_isolation (thread_p);

      while (1)
	{

	  /* get next object from OID list */
	  if (scan_id->grouped)
	    {
	      /* grouped scan */
	      if (scan_id->direction == S_FORWARD)
		{
		  /* move forward (to the next object) */
		  if (isidp->curr_oidno == -1)
		    {
		      isidp->curr_oidno = 0;	/* first oid number */
		      isidp->curr_oidp = isidp->oid_list.oidp;
		    }
		  else if (isidp->curr_oidno < isidp->oid_list.oid_cnt - 1)
		    {
		      isidp->curr_oidno++;
		      isidp->curr_oidp++;
		    }
		  else
		    {
		      return S_END;
		    }
		}
	      else
		{		/* if (scan_id->direction == S_FORWARD) */
		  /* move backward (to the previous object */
		  if (isidp->curr_oidno == isidp->oid_list.oid_cnt)
		    {
		      isidp->curr_oidno = isidp->oid_list.oid_cnt - 1;
		      isidp->curr_oidp = GET_NTH_OID (isidp->oid_list.oidp,
						      isidp->curr_oidno);
		    }
		  else if (isidp->curr_oidno > 0)
		    {
		      isidp->curr_oidno--;
		      isidp->curr_oidp = GET_NTH_OID (isidp->oid_list.oidp,
						      isidp->curr_oidno);
		    }
		  else
		    {
		      return S_END;
		    }
		}		/* if (scan_id->direction == S_FORWARD) */
	    }
	  else
	    {			/* if (scan_id->grouped) */
	      /* non-grouped, regular index scan */
	      if (scan_id->position == S_BEFORE || scan_id->position == S_ON)
		{
		  if (scan_id->position == S_BEFORE)
		    {
		      /* get the set of object identifiers specified in the
		         range */
		      if (scan_get_index_oidset (thread_p, scan_id) !=
			  NO_ERROR)
			{
			  return S_ERROR;
			}
		      if (isidp->oid_list.oid_cnt == 0)
			{
			  /* range is empty */
			  return S_END;
			}

		      if (isidp->need_count_only == true)
			{
			  /* no more scan is needed. just return */
			  return S_SUCCESS;
			}

		      scan_id->position = S_ON;
		      isidp->curr_oidno = 0;	/* first oid number */
		      isidp->curr_oidp = GET_NTH_OID (isidp->oid_list.oidp,
						      isidp->curr_oidno);
		    }
		  else
		    {
		      if (isidp->curr_oidno < isidp->oid_list.oid_cnt - 1)
			{
			  isidp->curr_oidno++;
			  isidp->curr_oidp =
			    GET_NTH_OID (isidp->oid_list.oidp,
					 isidp->curr_oidno);

			}
		      else
			{
			  if (BTREE_END_OF_SCAN (&isidp->bt_scan)
			      && isidp->indx_info->range_type != R_RANGELIST
			      && isidp->indx_info->range_type != R_KEYLIST)
			    {
			      return S_END;
			    }
			  else
			    {
			      if (scan_get_index_oidset (thread_p, scan_id) !=
				  NO_ERROR)
				{
				  return S_ERROR;
				}
			      if (isidp->oid_list.oid_cnt == 0)
				{
				  /* range is empty */
				  return S_END;
				}

			      if (isidp->need_count_only == true)
				{
				  /* no more scan is needed. just return */
				  return S_SUCCESS;
				}

			      isidp->curr_oidno = 0;	/* first oid number */
			      isidp->curr_oidp = isidp->oid_list.oidp;
			    }
			}

		    }
		}
	      else if (scan_id->position == S_AFTER)
		{
		  return S_END;
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_QPROC_UNKNOWN_CRSPOS, 0);
		  return S_ERROR;
		}
	    }

	  if (scan_id->fixed == false)
	    {
	      recdes.data = NULL;
	    }
	  sp_scan =
	    heap_get_with_class_oid (thread_p, isidp->curr_oidp, &recdes,
				     &isidp->scan_cache, &class_oid,
				     scan_id->fixed);
	  if (sp_scan != S_SUCCESS
	      && !QPROC_OK_IF_DELETED (sp_scan, isolation))
	    {
	      /* scan error or end of scan */
	      return (sp_scan == S_END) ? S_END : S_ERROR;
	    }

	  /* We need to check if the instnace is from the class that we
	     are interested in. Index scans that use B-tree for unique
	     attributes can return any class in the inheritence hierarchy
	     since uniques span hierarchies. */
	  if (sp_scan == S_DOESNT_EXIST
	      || !OID_EQ (&class_oid, &isidp->cls_oid))
	    {
	      continue;		/* continue to the next object */
	    }

	  /* evaluate the predicates to see if the object qualifise */
	  ev_res =
	    eval_data_filter (thread_p, isidp->curr_oidp, &recdes,
			      &data_filter);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }

	  if (scan_id->qualification == QPROC_QUALIFIED)
	    {
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	    {
	      if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
		{
		  continue;	/* qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_QUALIFIED_OR_NOT)
	    {
	      if (ev_res == V_TRUE)
		{
		  scan_id->qualification = QPROC_QUALIFIED;
		}
	      else if (ev_res == V_FALSE)
		{
		  scan_id->qualification = QPROC_NOT_QUALIFIED;
		}
	      else		/* V_UNKNOWN */
		{
		  /* nop */
		  ;
		}
	    }
	  else
	    {			/* invalid value; the same as QPROC_QUALIFIED */
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }

	  if (PRM_ORACLE_STYLE_EMPTY_STRING)
	    {
	      if (isidp->num_vstr)
		{
		  int i;
		  REGU_VARIABLE_LIST regup;
		  DB_VALUE *dbvalp;

		  /* read the key range the values from the heap into
		   * the attribute cache */
		  if (heap_attrinfo_read_dbvalues (thread_p, isidp->curr_oidp,
						   &recdes,
						   isidp->key_attrs.
						   attr_cache) != NO_ERROR)
		    {
		      return S_ERROR;
		    }

		  /* for all attributes specified in the key range,
		   * apply special data filter; 'key range attr IS NOT NULL'
		   */
		  regup = isidp->key_pred.regu_list;
		  for (i = 0; i < isidp->num_vstr && regup; i++)
		    {
		      if (isidp->vstr_ids[i] == -1)
			{
			  continue;	/* skip and go ahead */
			}

		      if (fetch_peek_dbval (thread_p, &regup->value,
					    scan_id->vd, NULL, NULL,
					    NULL, &dbvalp) != NO_ERROR)
			{
			  ev_res = V_ERROR;	/* error */
			  break;
			}
		      else if (DB_IS_NULL (dbvalp))
			{
			  ev_res = V_FALSE;	/* found Empty-string */
			  break;
			}

		      regup = regup->next;
		    }

		  if (ev_res == V_TRUE && i < isidp->num_vstr)
		    {
		      /* must be impossible. unknown error */
		      ev_res = V_ERROR;
		    }
		}
	    }

	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }
	  else
	    {
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }

	  if (isidp->rest_regu_list)
	    {
	      /* read the rest of the values from the heap into the attribute
	         cache */
	      if (heap_attrinfo_read_dbvalues (thread_p, isidp->curr_oidp,
					       &recdes,
					       isidp->rest_attrs.
					       attr_cache) != NO_ERROR)
		{
		  return S_ERROR;
		}

	      /* fetch the rest of the values from the object instance */
	      if (scan_id->val_list)
		{
		  if (fetch_val_list (thread_p, isidp->rest_regu_list,
				      scan_id->vd, &isidp->cls_oid,
				      isidp->curr_oidp, NULL,
				      PEEK) != NO_ERROR)
		    {
		      return S_ERROR;
		    }
		}
	    }

	  return S_SUCCESS;

	}

      break;			/* cannot reach to this line */

    case S_LIST_SCAN:

      llsidp = &scan_id->s.llsid;

      tplrec.size = 0;
      tplrec.tpl = (QFILE_TUPLE) NULL;

      while ((qp_scan = qfile_scan_list_next (thread_p, &llsidp->lsid,
					      &tplrec, PEEK)) == S_SUCCESS)
	{

	  /* fetch the values for the predicate from the tuple */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list,
				  scan_id->vd, NULL,
				  NULL, tplrec.tpl, PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  /* evaluate the predicate to see if the tuple qualifies */
	  ev_res = V_TRUE;
	  if (llsidp->scan_pred.pr_eval_fnc && llsidp->scan_pred.pred_expr)
	    {
	      ev_res = (*llsidp->scan_pred.pr_eval_fnc) (thread_p,
							 llsidp->scan_pred.
							 pred_expr,
							 scan_id->vd, NULL);
	      if (ev_res == V_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  if (scan_id->qualification == QPROC_QUALIFIED)
	    {
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	    {
	      if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
		{
		  continue;	/* qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_QUALIFIED_OR_NOT)
	    {
	      if (ev_res == V_TRUE)
		{
		  scan_id->qualification = QPROC_QUALIFIED;
		}
	      else if (ev_res == V_FALSE)
		{
		  scan_id->qualification = QPROC_NOT_QUALIFIED;
		}
	      else		/* V_UNKNOWN */
		{
		  /* nop */
		  ;
		}
	    }
	  else
	    {			/* invalid value; the same as QPROC_QUALIFIED */
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }

	  /* fetch the rest of the values from the tuple */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, llsidp->rest_regu_list,
				  scan_id->vd, NULL, NULL,
				  tplrec.tpl, PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  if (llsidp->tplrecp)
	    {
	      llsidp->tplrecp->size = tplrec.size;
	      llsidp->tplrecp->tpl = tplrec.tpl;
	    }
	  return S_SUCCESS;

	}

      return qp_scan;

    case S_SET_SCAN:

      ssidp = &scan_id->s.ssid;

      /* if we are in the before postion, fetch the set */
      if (scan_id->position == S_BEFORE)
	{
	  REGU_VARIABLE *func;

	  func = ssidp->set_ptr;
	  if (func->type == TYPE_FUNC
	      && func->value.funcp->ftype == F_SEQUENCE)
	    {
	      REGU_VARIABLE_LIST ptr;
	      int size;

	      size = 0;
	      for (ptr = func->value.funcp->operand; ptr; ptr = ptr->next)
		{
		  size++;
		}
	      ssidp->operand = func->value.funcp->operand;
	      ssidp->set_card = size;
	    }
	  else
	    {
	      pr_clear_value (&ssidp->set);
	      if (fetch_copy_dbval (thread_p, ssidp->set_ptr, scan_id->vd,
				    NULL, NULL, NULL,
				    &ssidp->set) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }
	}

      /* evaluate set expression and put resultant set in DB_VALUE */
      while ((qp_scan = qproc_next_set_scan (thread_p, scan_id)) == S_SUCCESS)
	{

	  assert (scan_id->val_list != NULL);
	  assert (scan_id->val_list->val_cnt == 1);

	  ev_res = V_TRUE;
	  if (ssidp->scan_pred.pr_eval_fnc && ssidp->scan_pred.pred_expr)
	    {
	      ev_res = (*ssidp->scan_pred.pr_eval_fnc) (thread_p,
							ssidp->scan_pred.
							pred_expr,
							scan_id->vd, NULL);
	      if (ev_res == V_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  if (scan_id->qualification == QPROC_QUALIFIED)
	    {
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	    {
	      if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
		{
		  continue;	/* qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_QUALIFIED_OR_NOT)
	    {
	      if (ev_res == V_TRUE)
		{
		  scan_id->qualification = QPROC_QUALIFIED;
		}
	      else if (ev_res == V_FALSE)
		{
		  scan_id->qualification = QPROC_NOT_QUALIFIED;
		}
	      else		/* V_UNKNOWN */
		{
		  /* nop */
		  ;
		}
	    }
	  else
	    {			/* invalid value; the same as QPROC_QUALIFIED */
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }

	  return S_SUCCESS;

	}			/* while ((qp_scan = ) == S_SUCCESS) */

      return qp_scan;

    case S_METHOD_SCAN:

      vaidp = &scan_id->s.vaid;

      /* execute method scan */
      qp_scan = method_scan_next (thread_p, &vaidp->scan_buf, &vl);
      if (qp_scan != S_SUCCESS)
	{
	  /* scan error or end of scan */
	  if (qp_scan == S_END)
	    {
	      scan_id->position = S_AFTER;
	      return S_END;
	    }
	  else
	    {
	      return S_ERROR;
	    }
	}

      /* copy the result into the value list of the scan ID */
      for (src_valp = vl.valp, dest_valp = scan_id->val_list->valp;
	   src_valp && dest_valp;
	   src_valp = src_valp->next, dest_valp = dest_valp->next)
	{

	  if (PRIM_IS_NULL (src_valp->val))
	    {
	      pr_clear_value (dest_valp->val);
	    }
	  else if (PRIM_TYPE (src_valp->val) != PRIM_TYPE (dest_valp->val))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_INVALID_DATATYPE, 0);
	      pr_clear_value (src_valp->val);
	      free_and_init (src_valp->val);
	      return S_ERROR;
	    }
	  else if (!qdata_copy_db_value (dest_valp->val, src_valp->val))
	    {
	      return S_ERROR;
	    }
	  pr_clear_value (src_valp->val);
	  free_and_init (src_valp->val);

	}

      return S_SUCCESS;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return S_ERROR;
    }
}

/*
 * scan_handle_single_scan () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: This second order function applies the given next-scan function,
 * then enforces the single_fetch , null_fetch semantics.
 * Note that when "single_fetch", "null_fetch" is asserted, at least one
 * qualified scan item, the NULL row, is returned.
 */
static SCAN_CODE
scan_handle_single_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id,
			 QP_SCAN_FUNC next_scan)
{
  SCAN_CODE result = S_ERROR;

  switch (s_id->single_fetch)
    {
    case QPROC_NO_SINGLE_INNER:
      result = (*next_scan) (thread_p, s_id);

      if (result == S_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case QPROC_SINGLE_OUTER:
      /* already returned a row? */
      /* if scan works in a single_fetch mode and first qualified scan
       * item has already been fetched, return end_of_scan.
       */
      if (s_id->single_fetched)
	{
	  result = S_END;
	}
      else
	/* if it is known that scan has no qualified items, return
	 * the NULL row, without searching.
	 */
      if (s_id->join_dbval && PRIM_IS_NULL (s_id->join_dbval))
	{
	  qdata_set_value_list_to_null (s_id->val_list);
	  s_id->single_fetched = true;
	  result = S_SUCCESS;
	}
      else
	{
	  result = (*next_scan) (thread_p, s_id);

	  if (result == S_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (result == S_END)
	    {
	      qdata_set_value_list_to_null (s_id->val_list);
	      result = S_SUCCESS;
	    }

	  s_id->single_fetched = true;
	}
      break;

    case QPROC_SINGLE_INNER:	/* currently, not used */
      /* already returned a row? */
      /* if scan works in a single_fetch mode and first qualified scan
       * item has already been fetched, return end_of_scan.
       */
      if (s_id->single_fetched)
	{
	  result = S_END;
	}
      /* if it is known that scan has no qualified items, return
       * the NULL row, without searching.
       */
      else if (s_id->join_dbval && PRIM_IS_NULL (s_id->join_dbval))
	{
	  result = S_END;
	}
      else
	{
	  result = (*next_scan) (thread_p, s_id);

	  if (result == S_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (result == S_SUCCESS)
	    {
	      s_id->single_fetched = true;
	    }
	}
      break;

    case QPROC_NO_SINGLE_OUTER:
      /* already returned a NULL row?
         if scan works in a left outer join mode and a NULL row has
         already fetched, return end_of_scan. */
      if (s_id->null_fetched)
	{
	  result = S_END;
	}
      else
	{
	  result = (*next_scan) (thread_p, s_id);

	  if (result == S_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (result == S_END)
	    {
	      if (!s_id->single_fetched)
		{
		  /* no qualified items, return a NULL row */
		  qdata_set_value_list_to_null (s_id->val_list);
		  s_id->null_fetched = true;
		  result = S_SUCCESS;
		}
	    }

	  if (result == S_SUCCESS)
	    {
	      s_id->single_fetched = true;
	    }
	}
      break;
    }

  /* maintain what is apparently suposed to be an invariant--
   * S_END implies position is "after" the scan
   */
  if (result == S_END)
    {
      if (s_id->direction != S_BACKWARD)
	{
	  s_id->position = S_AFTER;
	}
      else
	{
	  s_id->position = S_BEFORE;
	}
    }

  return result;

exit_on_error:

  return S_ERROR;
}

/*
 * scan_next_scan () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 */
SCAN_CODE
scan_next_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  return scan_handle_single_scan (thread_p, s_id, scan_next_scan_local);
}

/*
 * scan_prev_scan_local () - The scan is moved to the previous scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned.
 * If an error occurs, S_ERROR is returned. This routine currently supports only LIST FILE scans.
 */
static SCAN_CODE
scan_prev_scan_local (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  LLIST_SCAN_ID *llsidp;
  SCAN_CODE qp_scan;
  DB_LOGICAL ev_res;
  QFILE_TUPLE_RECORD tplrec;


  switch (scan_id->type)
    {
    case S_LIST_SCAN:
      llsidp = &scan_id->s.llsid;

      tplrec.size = 0;
      tplrec.tpl = (QFILE_TUPLE) NULL;

      while ((qp_scan = qfile_scan_list_prev (thread_p, &llsidp->lsid,
					      &tplrec, PEEK)) == S_SUCCESS)
	{

	  /* fetch the values for the predicate from the tuple */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list,
				  scan_id->vd, NULL,
				  NULL, tplrec.tpl, PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  /* evaluate the predicate to see if the tuple qualifies */
	  ev_res = V_TRUE;
	  if (llsidp->scan_pred.pr_eval_fnc && llsidp->scan_pred.pred_expr)
	    {
	      ev_res = (*llsidp->scan_pred.pr_eval_fnc) (thread_p,
							 llsidp->scan_pred.
							 pred_expr,
							 scan_id->vd, NULL);
	      if (ev_res == V_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  if (scan_id->qualification == QPROC_QUALIFIED)
	    {
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	    {
	      if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
		{
		  continue;	/* qualified, continue to the next tuple */
		}
	    }
	  else if (scan_id->qualification == QPROC_QUALIFIED_OR_NOT)
	    {
	      if (ev_res == V_TRUE)
		{
		  scan_id->qualification = QPROC_QUALIFIED;
		}
	      else if (ev_res == V_FALSE)
		{
		  scan_id->qualification = QPROC_NOT_QUALIFIED;
		}
	      else		/* V_UNKNOWN */
		{
		  /* nop */
		  ;
		}
	    }
	  else
	    {			/* invalid value; the same as QPROC_QUALIFIED */
	      if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
		{
		  continue;	/* not qualified, continue to the next tuple */
		}
	    }

	  /* fetch the rest of the values from the tuple */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list
		  (thread_p, llsidp->rest_regu_list, scan_id->vd, NULL, NULL,
		   tplrec.tpl, PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  if (llsidp->tplrecp)
	    {
	      llsidp->tplrecp->size = tplrec.size;
	      llsidp->tplrecp->tpl = tplrec.tpl;
	    }
	  return S_SUCCESS;

	}			/* while ((qp_scan = ...) == S_SUCCESS) */
      if (qp_scan == S_END)
	{
	  scan_id->position = S_BEFORE;
	}

      return qp_scan;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return S_ERROR;
    }				/* switch (scan_id->type) */

}

/*
 * scan_prev_scan () - The scan is moved to the previous scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned.
 * If an error occurs, S_ERROR is returned. This routine currently supports only LIST FILE scans.
 */
SCAN_CODE
scan_prev_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id)
{
  return scan_handle_single_scan (thread_p, s_id, scan_prev_scan_local);
}

/*
 * scan_save_scan_pos () - Save current scan position information.
 *   return:
 *   scan_id(in/out): Scan identifier
 *   scan_pos(in/out): Set to contain current scan position
 *
 * Note: This routine currently assumes only LIST FILE scans.
 */
void
scan_save_scan_pos (SCAN_ID * s_id, SCAN_POS * scan_pos)
{
  scan_pos->status = s_id->status;
  scan_pos->position = s_id->position;
  qfile_save_current_scan_tuple_position (&s_id->s.llsid.lsid,
					  &scan_pos->ls_tplpos);
}


/*
 * scan_jump_scan_pos () - Jump to the given scan position and move the scan from that point on in the forward direction.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *   scan_pos(in/out): Set to contain current scan position
 *
 * Note: This routine currently assumes only LIST FILE scans.
 */
SCAN_CODE
scan_jump_scan_pos (THREAD_ENTRY * thread_p, SCAN_ID * s_id,
		    SCAN_POS * scan_pos)
{
  LLIST_SCAN_ID *llsidp;
  DB_LOGICAL ev_res;
  QFILE_TUPLE_RECORD tplrec;
  SCAN_CODE qp_scan;

  llsidp = &s_id->s.llsid;

  /* put back saved scan position */
  s_id->status = scan_pos->status;
  s_id->position = scan_pos->position;

  /* jump to the previouslt saved scan position and continue from that point
     on forward */
  tplrec.size = 0;
  tplrec.tpl = (QFILE_TUPLE) NULL;

  qp_scan = qfile_jump_scan_tuple_position (thread_p, &llsidp->lsid,
					    &scan_pos->ls_tplpos, &tplrec,
					    PEEK);
  if (qp_scan != S_SUCCESS)
    {
      if (qp_scan == S_END)
	{
	  s_id->position = S_AFTER;
	}
      return qp_scan;
    }

  do
    {
      /* fetch the value for the predicate from the tuple */
      if (s_id->val_list)
	{
	  if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list, s_id->vd,
			      NULL, NULL, tplrec.tpl, PEEK) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      /* evaluate the predicate to see if the tuple qualifies */
      ev_res = V_TRUE;
      if (llsidp->scan_pred.pr_eval_fnc && llsidp->scan_pred.pred_expr)
	{
	  ev_res = (*llsidp->scan_pred.pr_eval_fnc) (thread_p,
						     llsidp->scan_pred.
						     pred_expr, s_id->vd,
						     NULL);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      if (s_id->qualification == QPROC_QUALIFIED)
	{
	  if (ev_res == V_TRUE)
	    {
	      /* nop */ ;
	    }
	  /* qualified, return it */
	}
      else if (s_id->qualification == QPROC_NOT_QUALIFIED)
	{
	  if (ev_res == V_FALSE)
	    {
	      ev_res = V_TRUE;	/* not qualified, return it */
	    }
	}
      else if (s_id->qualification == QPROC_QUALIFIED_OR_NOT)
	{
	  if (ev_res == V_TRUE)
	    {
	      s_id->qualification = QPROC_QUALIFIED;
	    }
	  else if (ev_res == V_FALSE)
	    {
	      s_id->qualification = QPROC_NOT_QUALIFIED;
	    }
	  else			/* V_UNKNOWN */
	    {
	      /* nop */
	      ;
	    }
	  ev_res = V_TRUE;	/* return it */
	}
      else
	{			/* invalid value; the same as QPROC_QUALIFIED */
	  if (ev_res == V_TRUE)
	    {
	      /* nop */ ;
	    }
	  /* qualified, return it */
	}

      if (ev_res == V_TRUE)
	{
	  /* fetch the rest of the values from the tuple */
	  if (s_id->val_list)
	    {
	      if (fetch_val_list (thread_p, llsidp->rest_regu_list, s_id->vd,
				  NULL, NULL, tplrec.tpl, PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  if (llsidp->tplrecp)
	    {
	      llsidp->tplrecp->size = tplrec.size;
	      llsidp->tplrecp->tpl = tplrec.tpl;
	    }
	  return S_SUCCESS;
	}

    }
  while ((qp_scan = qfile_scan_list_next (thread_p, &llsidp->lsid, &tplrec,
					  PEEK)) == S_SUCCESS);

  if (qp_scan == S_END)
    {
      s_id->position = S_AFTER;
    }
  return qp_scan;
}

/*
 * scan_initialize () - initialize scan management routine
 *   return: NO_ERROR if all OK, ER status otherwise
 */
void
scan_initialize (void)
{
  int i;
  OID *oid_buf_p;

  scan_Iscan_oid_buf_list = NULL;
  scan_Iscan_oid_buf_list_count = 0;

  /* pre-allocate oid buf list */
  for (i = 0; i < 10; i++)
    {
      oid_buf_p = alloc_oid_buf ();
      if (oid_buf_p == NULL)
	{
	  break;
	}
      /* save privious oid buf pointer */
      *(intptr_t *) oid_buf_p = (intptr_t) scan_Iscan_oid_buf_list;
      scan_Iscan_oid_buf_list = oid_buf_p;
      scan_Iscan_oid_buf_list_count++;
    }
}

/*
 * scan_finalize () - finalize scan management routine
 *   return:
 */
void
scan_finalize (void)
{
  OID *oid_buf_p;

  while (scan_Iscan_oid_buf_list != NULL)
    {
      oid_buf_p = scan_Iscan_oid_buf_list;
      /* save previous oid buf pointer */
      scan_Iscan_oid_buf_list = (OID *) (*(intptr_t *) oid_buf_p);
      free_and_init (oid_buf_p);
    }
  scan_Iscan_oid_buf_list_count = 0;
}
