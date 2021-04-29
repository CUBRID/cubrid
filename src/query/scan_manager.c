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
 * scan_manager.c - scan management routines
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "jansson.h"

#include "error_manager.h"
#include "heap_file.h"
#include "fetch.h"
#include "list_file.h"
#include "set_scan.h"
#include "system_parameter.h"
#include "btree_load.h"
#include "perf_monitor.h"
#include "query_manager.h"
#include "query_evaluator.h"
#include "query_opfunc.h"
#include "query_reevaluation.hpp"
#include "regu_var.hpp"
#include "locator_sr.h"
#include "log_lsa.hpp"
#include "object_primitive.h"
#include "object_representation.h"
#include "dbtype.h"
#include "xasl_predicate.hpp"
#include "xasl.h"

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif

/* this macro is used to make sure that heap file identifier is initialized
 * properly that heap file scan routines will work properly.
 */
#define UT_CAST_TO_NULL_HEAP_OID(hfidp,oidp) \
  do \
    { \
      (oidp)->pageid = NULL_PAGEID; \
      (oidp)->volid = (hfidp)->vfid.volid; \
      (oidp)->slotid = NULL_SLOTID; \
    } \
  while (0)

#define GET_NTH_OID(oid_setp, n) ((OID *)((OID *)(oid_setp) + (n)))

/* ISS_RANGE_DETAILS stores information about the two ranges we use
 * interchangeably in Index Skip Scan mode: along with the real range, we
 * use a "fake" one to obtain the next value for the index's first column.
 *
 * ISS_RANGE_DETAILS tries to completely encapsulate one of these ranges, so
 * that whenever the need arises, we can "swap" them. */
typedef struct iss_range_details ISS_RANGE_DETAILS;
struct iss_range_details
{
  int key_cnt;
  KEY_RANGE *key_ranges;
  SCAN_PRED key_pred;
  RANGE_TYPE range_type;
  int part_key_desc;		/* last partial key domain is descending */
};

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
  ROP_GT_INF, ROP_LT_INF, INF_INF}
};

static const int rop_range_table_size = sizeof (rop_range_table) / sizeof (struct rop_range_struct);

#if defined(SERVER_MODE)
static pthread_mutex_t scan_Iscan_oid_buf_list_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static BTREE_ISCAN_OID_LIST *scan_Iscan_oid_buf_list = NULL;
static int scan_Iscan_oid_buf_list_count = 0;

#define SCAN_ISCAN_OID_BUF_LIST_DEFAULT_SIZE 10

static void scan_init_scan_pred (SCAN_PRED * scan_pred_p, regu_variable_list_node * regu_list, PRED_EXPR * pred_expr,
				 PR_EVAL_FNC pr_eval_fnc);
static void scan_init_scan_attrs (SCAN_ATTRS * scan_attrs_p, int num_attrs, ATTR_ID * attr_ids,
				  HEAP_CACHE_ATTRINFO * attr_cache);
static int scan_init_indx_coverage (THREAD_ENTRY * thread_p, int coverage_enabled, valptr_list_node * output_val_list,
				    regu_variable_list_node * regu_val_list, VAL_DESCR * vd, QUERY_ID query_id,
				    int max_key_len, int func_index_col_id, INDX_COV * indx_cov);
static int scan_alloc_oid_list (BTREE_ISCAN_OID_LIST ** oid_list_p);
static int scan_alloc_iscan_oid_buf_list (BTREE_ISCAN_OID_LIST ** oid_list);
static void scan_free_iscan_oid_buf_list (BTREE_ISCAN_OID_LIST * oid_list);
static void rop_to_range (RANGE * range, ROP_TYPE left, ROP_TYPE right);
static void range_to_rop (ROP_TYPE * left, ROP_TYPE * rightk, RANGE range);
static ROP_TYPE compare_val_op (DB_VALUE * val1, ROP_TYPE op1, DB_VALUE * val2, ROP_TYPE op2, int num_index_term);
static int key_val_compare (const void *p1, const void *p2);
static int eliminate_duplicated_keys (KEY_VAL_RANGE * key_vals, int key_cnt);
static int merge_key_ranges (KEY_VAL_RANGE * key_vals, int key_cnt);
static int reverse_key_list (KEY_VAL_RANGE * key_vals, int key_cnt);
static int check_key_vals (KEY_VAL_RANGE * key_vals, int key_cnt, QPROC_KEY_VAL_FU * chk_fn);
static int scan_dbvals_to_midxkey (THREAD_ENTRY * thread_p, DB_VALUE * retval, bool * indexal,
				   TP_DOMAIN * btree_domainp, int num_term, REGU_VARIABLE * func, VAL_DESCR * vd,
				   int key_minmax, bool is_iss, DB_VALUE * fetched_values);
static int scan_regu_key_to_index_key (THREAD_ENTRY * thread_p, KEY_RANGE * key_ranges, KEY_VAL_RANGE * key_val_range,
				       INDX_SCAN_ID * iscan_id, TP_DOMAIN * btree_domainp, VAL_DESCR * vd);
static int scan_get_index_oidset (THREAD_ENTRY * thread_p, SCAN_ID * s_id, DB_BIGINT * key_limit_upper,
				  DB_BIGINT * key_limit_lower);
static void scan_init_scan_id (SCAN_ID * scan_id, bool force_select_lock, SCAN_OPERATION_TYPE scan_op_type, int fixed,
			       int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
			       val_list_node * val_list, VAL_DESCR * vd);
static int scan_init_index_key_limit (THREAD_ENTRY * thread_p, INDX_SCAN_ID * isidp, KEY_INFO * key_infop,
				      VAL_DESCR * vd);
static SCAN_CODE scan_next_scan_local (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_heap_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_heap_page_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_class_attr_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_index_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_index_key_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_index_node_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_index_lookup_heap (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, INDX_SCAN_ID * isidp,
					      FILTER_INFO * data_filter, TRAN_ISOLATION isolation);
static SCAN_CODE scan_next_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_showstmt_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_set_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_json_table_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_value_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_method_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_handle_single_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id, QP_SCAN_FUNC next_scan);
static SCAN_CODE scan_prev_scan_local (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static void resolve_domains_on_list_scan (LLIST_SCAN_ID * llsidp, val_list_node * ref_val_list);
static void resolve_domain_on_regu_operand (REGU_VARIABLE * regu_var, val_list_node * ref_val_list,
					    QFILE_TUPLE_VALUE_TYPE_LIST * p_type_list);
static int scan_init_multi_range_optimization (THREAD_ENTRY * thread_p, MULTI_RANGE_OPT * multi_range_opt,
					       bool use_range_opt, int max_size);
static int scan_dump_key_into_tuple (THREAD_ENTRY * thread_p, INDX_SCAN_ID * iscan_id, DB_VALUE * key, OID * oid,
				     QFILE_TUPLE_RECORD * tplrec);
static int scan_save_range_details (INDX_SCAN_ID * isidp_src, ISS_RANGE_DETAILS * rdp_dest);
static int scan_restore_range_details (ISS_RANGE_DETAILS * rdp_src, INDX_SCAN_ID * isidp_dest);
static SCAN_CODE scan_get_next_iss_value (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, INDX_SCAN_ID * isidp);
static SCAN_CODE call_get_next_index_oidset (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, INDX_SCAN_ID * isidp,
					     bool should_go_to_next_value);
static int scan_key_compare (DB_VALUE * val1, DB_VALUE * val2, int num_index_term);

/* for hash list scan */
static SCAN_CODE scan_build_hash_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_next_hash_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id);
static SCAN_CODE scan_hash_probe_next (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, QFILE_TUPLE * tuple);
static HASH_METHOD check_hash_list_scan (LLIST_SCAN_ID * llsidp, int *val_cnt, int hash_list_scan_yn);

/*
 * scan_init_iss () - initialize index skip scan structure
 *   return: error code
 *   isidp: pointer to index scan id structure that contains iss structure
 */
int
scan_init_iss (INDX_SCAN_ID * isidp)
{
  DB_VALUE *last_key = NULL;
  INDEX_SKIP_SCAN *iss = NULL;

  if (isidp == NULL)
    {
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return ER_FAILED;
    }

  iss = &isidp->iss;

  /* initialize iss structure */
  iss->current_op = ISS_OP_NONE;
  iss->skipped_range = NULL;

  if (isidp->indx_info == NULL)
    {
      /* no index info specified so no iss needed */
      iss->use = false;
      return NO_ERROR;
    }

  iss->use = isidp->indx_info->use_iss != 0;

  if (!iss->use)
    {
      /* if not using iss, nothing more to do */
      return NO_ERROR;
    }

  /* assign range */
  iss->skipped_range = &isidp->indx_info->iss_range;

  if (iss->skipped_range->key1 == NULL || iss->skipped_range->key1->value.funcp == NULL
      || iss->skipped_range->key1->value.funcp->operand == NULL
      || iss->skipped_range->key1->value.funcp->operand->value.type != TYPE_DBVAL)
    {
      /* this should never happen */
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return ER_FAILED;
    }

  /* reset key value */
  last_key = &iss->skipped_range->key1->value.funcp->operand->value.value.dbval;

  if (!DB_IS_NULL (last_key))
    {
      pr_clear_value (last_key);
      db_make_null (last_key);
    }

  return NO_ERROR;
}

/*
 * scan_init_index_scan () - initialize an index scan structure with the
 *			     specified OID buffer
 * return	     : void
 * isidp (in)	     : index scan
 * oid_list (in)     : OID list.
 * mvcc_snapshot(in) : MVCC snapshot
 */
void
scan_init_index_scan (INDX_SCAN_ID * isidp, struct btree_iscan_oid_list *oid_list, MVCC_SNAPSHOT * mvcc_snapshot)
{
  if (isidp == NULL)
    {
      assert (false);
      return;
    }

  isidp->oid_list = oid_list;
  isidp->copy_buf = NULL;
  isidp->copy_buf_len = 0;
  memset ((void *) (&(isidp->indx_cov)), 0, sizeof (INDX_COV));
  isidp->indx_info = NULL;
  memset ((void *) (&(isidp->multi_range_opt)), 0, sizeof (MULTI_RANGE_OPT));
  scan_init_iss (isidp);
  isidp->scan_cache.mvcc_snapshot = mvcc_snapshot;
  isidp->need_count_only = false;
  isidp->check_not_vacuumed = false;
  isidp->not_vacuumed_res = DISK_VALID;
}

/*
 * scan_save_range_details () - save range details from the index scan id to
 *                              a "backup" iss_range_details structure.
 *    return: error code
 *  rdp_dest: pointer to range details structure to be filled with data from isidp_src
 * isidp_src: pointer to index scan id
 */
static int
scan_save_range_details (INDX_SCAN_ID * isidp_src, ISS_RANGE_DETAILS * rdp_dest)
{
  if (rdp_dest == NULL || isidp_src == NULL || isidp_src->indx_info == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  rdp_dest->key_cnt = isidp_src->indx_info->key_info.key_cnt;
  rdp_dest->key_ranges = isidp_src->indx_info->key_info.key_ranges;
  rdp_dest->key_pred = isidp_src->key_pred;
  rdp_dest->range_type = isidp_src->indx_info->range_type;
  rdp_dest->part_key_desc = isidp_src->bt_scan.btid_int.part_key_desc;

  return NO_ERROR;
}

/*
 * scan_restore_range_details () - restore range details from the backup
 *                                 structure into the index scan id.
 *     return: error code
 *    rdp_src: pointer to range details structure to be restored
 * isidp_dest: pointer to index scan id to be filled
 *
 * Note:
 * The index scan is reset so that it is considered a brand new scan (for
 * instance, curr_keyno is set to -1 etc.)
 */
static int
scan_restore_range_details (ISS_RANGE_DETAILS * rdp_src, INDX_SCAN_ID * isidp_dest)
{
  if (isidp_dest == NULL || rdp_src == NULL || isidp_dest->indx_info == NULL)
    {
      return ER_FAILED;
    }

  isidp_dest->curr_keyno = -1;
  isidp_dest->indx_info->key_info.key_cnt = rdp_src->key_cnt;

  isidp_dest->indx_info->key_info.key_ranges = rdp_src->key_ranges;
  isidp_dest->key_pred = rdp_src->key_pred;
  isidp_dest->indx_info->range_type = rdp_src->range_type;
  isidp_dest->bt_scan.btid_int.part_key_desc = rdp_src->part_key_desc;

  return NO_ERROR;
}

/*
 * scan_get_next_iss_value () - retrieve the next value to be used in the
 *                              index skip scan: the next value from the
 *                              first column of the index.
 *   return: S_SUCCESS on successfully retrieving the next value
 *           S_ERROR on encountering an error
 *           S_END when the search does not find any "next value" (reached
 *           end of the btree).
 *   thread_p(in):
 *   scan_id (in): scan id
 *   isidp   (in): index scan id
 *
 * Note:
 * This function is called from call_get_next_index_oidset whenever the
 * current value for the first index column is exhausted and we need to go on
 * to the next possible value of C1. It is ONLY called within an index skip
 * scan optimization context (ONLY when isidp->iss.use is TRUE).
 * We call scan_get_index_oidset(), which in turn calls btree_range_search,
 * but first we set the stage: prepare a "fake" range to obtain the next value
 * for C1 (the first column), extract the value that btree_range_search returns
 * inside isidp->iss.dbval, fill in slot 0 of the real range ([C1=?]) with
 * that new value, and restore the real range as if it were ready to be used
 * for the first time.
 */
static SCAN_CODE
scan_get_next_iss_value (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, INDX_SCAN_ID * isidp)
{
  /* we are being called either before any other btree range search, or after somebody finished a real range search and
   * wants to advance to the next value for the first column of the index. */

  INDEX_SKIP_SCAN *iss = NULL;
  DB_VALUE *last_key = NULL;
  ISS_RANGE_DETAILS scan_range_det, fetch_range_det;
  bool descending_skip_key = false;
  bool descending_scan = false;
  int i;

  if (isidp == NULL)
    {
      /* null pointer was passed to function */
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return S_ERROR;
    }

  if (!isidp->iss.use)
    {
      /* not using iss but function was called; should not be here */
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return S_ERROR;
    }

  if (isidp->iss.skipped_range == NULL || isidp->iss.skipped_range->key1 == NULL
      || isidp->iss.skipped_range->key1->value.funcp == NULL
      || isidp->iss.skipped_range->key1->value.funcp->operand == NULL
      || isidp->iss.skipped_range->key1->value.funcp->operand->value.type != TYPE_DBVAL)
    {
      /* the fetch range is corrupted; should not be here */
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return S_ERROR;
    }

  if (isidp->bt_scan.btid_int.key_type == NULL || isidp->bt_scan.btid_int.key_type->setdomain == NULL
      || TP_DOMAIN_TYPE (isidp->bt_scan.btid_int.key_type) != DB_TYPE_MIDXKEY)
    {
      /* key type is not midxkey so this is not a multi-column index; should not be here */
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return S_ERROR;
    }

  /* populate local variables iss and last_key */
  descending_skip_key = isidp->bt_scan.btid_int.key_type->setdomain->is_desc;
  iss = &isidp->iss;
  last_key = &iss->skipped_range->key1->value.funcp->operand->value.value.dbval;

  if (iss->current_op == ISS_OP_NONE)
    {
      /* first iteration of index skip scan; set up range for fetching first value. key1 might be set but will be
       * ignored for INF_INF range. */
      iss->current_op = ISS_OP_GET_FIRST_KEY;
      iss->skipped_range->range = INF_INF;
    }
  else if (iss->current_op == ISS_OP_DO_RANGE_SEARCH)
    {
      /* find out whether the first column of the index is asc or desc */
      descending_scan = (isidp->bt_scan.use_desc_index ? true : false);
      if (descending_skip_key)
	{
	  descending_scan = !descending_scan;
	}

      if (descending_scan)
	{
	  /* if we're doing a descending scan, we can stop before searching for first_column < NULL since it won't
	   * produce any results */
	  if (DB_IS_NULL (last_key))
	    {
	      pr_clear_value (last_key);
	      db_make_null (last_key);

	      return S_END;
	    }

	  /* set the upper bound to last used key value and lower bound to NULL (i.e. infinity) */
	  iss->skipped_range->key2 = iss->skipped_range->key1;
	  iss->skipped_range->key1 = NULL;
	}

      /* set up ISS state for searching next distinct key */
      iss->current_op = ISS_OP_SEARCH_NEXT_DISTINCT_KEY;
      iss->skipped_range->range = (descending_scan ? INF_LT : GT_INF);
    }
  else
    {
      /* operator is neither ISS_OP_NONE nor ISS_OP_DO_RANGE_SEARCH; we shouldn't be here */
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return S_ERROR;
    }

  /* populate details structure for the fetch range so we can load it */
  fetch_range_det.key_cnt = 1;
  fetch_range_det.key_ranges = iss->skipped_range;
  fetch_range_det.key_pred.pred_expr = NULL;
  fetch_range_det.key_pred.pr_eval_fnc = NULL;
  fetch_range_det.key_pred.regu_list = NULL;
  fetch_range_det.range_type = R_RANGE;
  fetch_range_det.part_key_desc = descending_skip_key;

  /* save current range details */
  if (scan_save_range_details (isidp, &scan_range_det) != NO_ERROR)
    {
      if (descending_scan)
	{
	  /* return range to initial state before exit */
	  iss->skipped_range->key1 = iss->skipped_range->key2;
	  iss->skipped_range->key2 = NULL;
	}
      return S_ERROR;
    }

  /* load the range we set up for fetching the next key of the first column */
  scan_restore_range_details (&fetch_range_det, isidp);

  isidp->curr_keyno = -1;

  /* run a scan to get next key for first index column; value will be stored in the lower (or higher) bound of the
   * fetch range (i.e. in last_key) */
  if (scan_get_index_oidset (thread_p, scan_id, NULL, NULL) != NO_ERROR)
    {
      if (descending_scan)
	{
	  /* return range to initial state before exit */
	  iss->skipped_range->key1 = iss->skipped_range->key2;
	  iss->skipped_range->key2 = NULL;
	}
      scan_restore_range_details (&scan_range_det, isidp);
      return S_ERROR;
    }

  /* undo bounds swapping if scan is descending */
  if (descending_scan)
    {
      iss->skipped_range->key1 = iss->skipped_range->key2;
      iss->skipped_range->key2 = NULL;
    }

  /* as soon as the scan returned, convert the midxkey dbvalue to real db value (if necessary) */
  if (scan_id->s.isid.oids_count == 0)
    {
      /* no other keys exist; restore scan range and exit */
      scan_restore_range_details (&scan_range_det, isidp);
      return S_END;
    }
  else if (DB_VALUE_DOMAIN_TYPE (last_key) == DB_TYPE_MIDXKEY)
    {
      /* scan_get_index_oidset() returned a midxkey */
      DB_VALUE first_midxkey_val;
      DB_VALUE tmp;
      int ret;

      /* initialize temporary variables */
      db_make_null (&first_midxkey_val);
      db_make_null (&tmp);

      /* put pointer to first value of midxkey into first_midxkey_val */
      ret = pr_midxkey_get_element_nocopy (&last_key->data.midxkey, 0, &first_midxkey_val, NULL, NULL);
      if (ret != NO_ERROR)
	{
	  scan_restore_range_details (&scan_range_det, isidp);
	  return S_ERROR;
	}

      /* first_midxkey_val may hold pointer to first value from last_key, which we actually want to place in last_key.
       * steps: 1. clone first_midxkey_val to a temp variable so we have a DB_VALUE independent of last_key 2. clear
       * last_key (no longer holds important data) 3. clone temp variable to last_key (for later use) 4. clear tmp (we
       * no longer need it) */
      pr_clone_value (&first_midxkey_val, &tmp);
      pr_clear_value (last_key);
      pr_clone_value (&tmp, last_key);
      pr_clear_value (&tmp);
      pr_clear_value (&first_midxkey_val);
    }

  /* use last_key in scan_range */
  for (i = 0; i < scan_range_det.key_cnt; i++)
    {
      KEY_RANGE *kr = &(scan_range_det.key_ranges[i]);

      if (kr == NULL)
	{
	  continue;
	}

      if (kr->key1 != NULL)
	{
	  assert_release (kr->key1->type == TYPE_FUNC && TP_DOMAIN_TYPE (kr->key1->domain) == DB_TYPE_MIDXKEY);
	  assert_release (kr->key1->value.funcp->operand);

	  if (kr->key1->value.funcp->operand != NULL)
	    {
	      REGU_VARIABLE *regu = &kr->key1->value.funcp->operand->value;

	      regu->type = TYPE_DBVAL;
	      regu->domain = tp_domain_resolve_default (DB_VALUE_DOMAIN_TYPE (last_key));

	      pr_clear_value (&regu->value.dbval);
	      pr_clone_value (last_key, &regu->value.dbval);
	    }
	}

      if (kr->key2 != NULL)
	{
	  assert_release (kr->key2->type == TYPE_FUNC && TP_DOMAIN_TYPE (kr->key2->domain) == DB_TYPE_MIDXKEY);
	  assert_release (kr->key2->value.funcp->operand);

	  if (kr->key2->value.funcp->operand != NULL)
	    {
	      REGU_VARIABLE *regu = &kr->key2->value.funcp->operand->value;

	      regu->type = TYPE_DBVAL;
	      regu->domain = tp_domain_resolve_default (DB_VALUE_DOMAIN_TYPE (last_key));

	      pr_clear_value (&regu->value.dbval);
	      pr_clone_value (last_key, &regu->value.dbval);
	    }
	}
    }

  /* restore the range used for normal scanning */
  scan_restore_range_details (&scan_range_det, isidp);

  /* prepare for range search */
  isidp->iss.current_op = ISS_OP_DO_RANGE_SEARCH;
  isidp->curr_keyno = -1;

  return S_SUCCESS;
}

/*
 * scan_init_scan_pred () - initialize SCAN_PRED structure
 *   return: none
 */
static void
scan_init_scan_pred (SCAN_PRED * scan_pred_p, regu_variable_list_node * regu_list, PRED_EXPR * pred_expr,
		     PR_EVAL_FNC pr_eval_fnc)
{
  assert (scan_pred_p != NULL);

  scan_pred_p->regu_list = regu_list;
  scan_pred_p->pred_expr = pred_expr;
  scan_pred_p->pr_eval_fnc = pr_eval_fnc;
}

/*
 * scan_init_scan_attrs () - initialize SCAN_ATTRS structure
 *   return: none
 */
static void
scan_init_scan_attrs (SCAN_ATTRS * scan_attrs_p, int num_attrs, ATTR_ID * attr_ids, HEAP_CACHE_ATTRINFO * attr_cache)
{
  assert (scan_attrs_p != NULL);

  scan_attrs_p->num_attrs = num_attrs;
  scan_attrs_p->attr_ids = attr_ids;
  scan_attrs_p->attr_cache = attr_cache;
}

/*
 * scan_init_filter_info () - initialize FILTER_INFO structure as a data/key filter
 *   return: none
 */
void
scan_init_filter_info (FILTER_INFO * filter_info_p, SCAN_PRED * scan_pred, SCAN_ATTRS * scan_attrs,
		       val_list_node * val_list, VAL_DESCR * val_descr, OID * class_oid, int btree_num_attrs,
		       ATTR_ID * btree_attr_ids, int *num_vstr_ptr, ATTR_ID * vstr_ids)
{
  assert (filter_info_p != NULL);

  filter_info_p->scan_pred = scan_pred;
  filter_info_p->scan_attrs = scan_attrs;
  filter_info_p->val_list = val_list;
  filter_info_p->val_descr = val_descr;
  filter_info_p->class_oid = class_oid;
  filter_info_p->btree_num_attrs = btree_num_attrs;
  filter_info_p->btree_attr_ids = btree_attr_ids;
  filter_info_p->num_vstr_ptr = num_vstr_ptr;
  filter_info_p->vstr_ids = vstr_ids;
  filter_info_p->func_idx_col_id = -1;
}

/*
 * scan_init_indx_coverage () - initialize INDX_COV structure
 *   return: error code
 *
 * coverage_enabled(in): true if coverage is enabled
 * output_val_list(in): output val list
 * regu_val_list(in): regu val list
 * vd(in): val descriptor
 * query_id(in): the query id
 * max_key_len(in): the maximum key length
 * indx_cov(in/out): index coverage data
 */
static int
scan_init_indx_coverage (THREAD_ENTRY * thread_p, int coverage_enabled, valptr_list_node * output_val_list,
			 regu_variable_list_node * regu_val_list, VAL_DESCR * vd, QUERY_ID query_id, int max_key_len,
			 int func_index_col_id, INDX_COV * indx_cov)
{
  int err = NO_ERROR;
  int num_membuf_pages = 0;

  if (indx_cov == NULL)
    {
      return ER_FAILED;
    }

  indx_cov->val_descr = vd;
  indx_cov->output_val_list = output_val_list;
  indx_cov->regu_val_list = regu_val_list;
  indx_cov->query_id = query_id;

  if (coverage_enabled == false)
    {
      indx_cov->type_list = NULL;
      indx_cov->list_id = NULL;
      indx_cov->tplrec = NULL;
      indx_cov->lsid = NULL;
      indx_cov->max_tuples = 0;
      indx_cov->func_index_col_id = -1;
      return NO_ERROR;
    }

  indx_cov->func_index_col_id = func_index_col_id;
  indx_cov->type_list =
    (QFILE_TUPLE_VALUE_TYPE_LIST *) db_private_alloc (thread_p, sizeof (QFILE_TUPLE_VALUE_TYPE_LIST));
  if (indx_cov->type_list == NULL)
    {
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }

  if (qdata_get_valptr_type_list (thread_p, output_val_list, indx_cov->type_list) != NO_ERROR)
    {
      err = ER_FAILED;
      goto exit_on_error;
    }

  /*
   * Covering index scan needs large-size memory buffer in order to decrease
   * the number of times doing stop-and-resume during btree_range_search.
   * To do it, QFILE_FLAG_USE_KEY_BUFFER is introduced. If the flag is set,
   * the list file allocates PRM_INDEX_SCAN_KEY_BUFFER_PAGES pages memory
   * for its memory buffer, which is generally larger than prm_get_integer_value (PRM_ID_TEMP_MEM_BUFFER_PAGES).
   */
  indx_cov->list_id = qfile_open_list (thread_p, indx_cov->type_list, NULL, query_id, QFILE_FLAG_USE_KEY_BUFFER);
  if (indx_cov->list_id == NULL)
    {
      err = ER_FAILED;
      goto exit_on_error;
    }

  num_membuf_pages = qmgr_get_temp_file_membuf_pages (indx_cov->list_id->tfile_vfid);
  assert (num_membuf_pages > 0);

  if (max_key_len > 0 && num_membuf_pages > 0)
    {
      indx_cov->max_tuples = num_membuf_pages * IO_PAGESIZE / max_key_len;
      indx_cov->max_tuples = MAX (indx_cov->max_tuples, 1);
    }
  else
    {
      indx_cov->max_tuples = IDX_COV_DEFAULT_TUPLES;
    }

  indx_cov->tplrec = (QFILE_TUPLE_RECORD *) db_private_alloc (thread_p, sizeof (QFILE_TUPLE_RECORD));
  if (indx_cov->tplrec == NULL)
    {
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  indx_cov->tplrec->size = 0;
  indx_cov->tplrec->tpl = NULL;

  indx_cov->lsid = (QFILE_LIST_SCAN_ID *) db_private_alloc (thread_p, sizeof (QFILE_LIST_SCAN_ID));
  if (indx_cov->lsid == NULL)
    {
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }
  indx_cov->lsid->status = S_CLOSED;

  return NO_ERROR;

exit_on_error:

  if (indx_cov->type_list != NULL)
    {
      if (indx_cov->type_list->domp != NULL)
	{
	  db_private_free_and_init (thread_p, indx_cov->type_list->domp);
	}
      db_private_free_and_init (thread_p, indx_cov->type_list);
    }

  if (indx_cov->list_id != NULL)
    {
      QFILE_FREE_AND_INIT_LIST_ID (indx_cov->list_id);
    }

  if (indx_cov->tplrec != NULL)
    {
      if (indx_cov->tplrec->tpl != NULL)
	{
	  db_private_free_and_init (thread_p, indx_cov->tplrec->tpl);
	}
      db_private_free_and_init (thread_p, indx_cov->tplrec);
    }

  if (indx_cov->lsid != NULL)
    {
      db_private_free_and_init (thread_p, indx_cov->lsid);
    }

  return err;
}

/*
 * scan_init_index_key_limit () - initialize/reset index key limits
 *   return: error code
 */
static int
scan_init_index_key_limit (THREAD_ENTRY * thread_p, INDX_SCAN_ID * isidp, KEY_INFO * key_infop, VAL_DESCR * vd)
{
  DB_VALUE *dbvalp;
  TP_DOMAIN *domainp = tp_domain_resolve_default (DB_TYPE_BIGINT);
  bool is_lower_limit_negative = false;
  TP_DOMAIN_STATUS dom_status;

  if (key_infop->key_limit_l != NULL)
    {
      if (fetch_peek_dbval (thread_p, key_infop->key_limit_l, vd, NULL, NULL, NULL, &dbvalp) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      dom_status = tp_value_coerce (dbvalp, dbvalp, domainp);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbvalp, domainp);

	  return ER_FAILED;
	}

      if (DB_VALUE_DOMAIN_TYPE (dbvalp) != DB_TYPE_BIGINT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
      else
	{
	  isidp->key_limit_lower = db_get_bigint (dbvalp);
	}

      if (isidp->key_limit_lower < 0)
	{
	  if (key_infop->is_user_given_keylimit == true)
	    {
	      /* We don't allow users to give us a bad keylimit bound */

	      /* still want to have better error code */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
	      return ER_QPROC_INVALID_PARAMETER;
	    }

	  /* Optimizer adopts keylimit optimization */

	  /* SELECT * from t where ROWNUM = 0 order by a: this would sometimes get optimized using keylimit, if the
	   * circumstances are right. in this case, the lower limit would be "0-1", effectiveley -1. We cannot allow
	   * that to happen, since -1 is a special value meaning "there is no lower limit", and certain critical
	   * decisions (such as resetting the key limit for multiple ranges) depend on knowing whether or not there is
	   * a lower key limit. We set a flag to remember, later on, to "adjust" the key limits such that, if the lower
	   * limit is negative, to return no results.
	   */
	  is_lower_limit_negative = true;
	}
    }
  else
    {
      isidp->key_limit_lower = -1;
    }

  if (key_infop->key_limit_u != NULL)
    {
      if (fetch_peek_dbval (thread_p, key_infop->key_limit_u, vd, NULL, NULL, NULL, &dbvalp) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      dom_status = tp_value_coerce (dbvalp, dbvalp, domainp);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbvalp, domainp);

	  return ER_FAILED;
	}
      if (DB_VALUE_DOMAIN_TYPE (dbvalp) != DB_TYPE_BIGINT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
      else
	{
	  isidp->key_limit_upper = db_get_bigint (dbvalp);
	}

      if (isidp->key_limit_upper < 0)
	{
	  if (key_infop->is_user_given_keylimit == true)
	    {
	      /* We don't allow users to give us a bad keylimit bound */

	      /* still want to have better error code */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
	      return ER_QPROC_INVALID_PARAMETER;
	    }

	  /* Optimizer adopts keylimit optimization */

	  /* Try to sanitize the upper value. It might have been computed from operations on host variables, which are
	   * unpredictable.
	   */
	  isidp->key_limit_upper = 0;
	}
    }
  else
    {
      isidp->key_limit_upper = -1;
    }

  if (is_lower_limit_negative && isidp->key_limit_upper > 0)
    {
      /* decrease the upper limit: key_limit_lower is negative */
      isidp->key_limit_upper += isidp->key_limit_lower;
      if (isidp->key_limit_upper < 0)
	{
	  isidp->key_limit_upper = 0;
	}
      isidp->key_limit_lower = 0;	/* reset it to something usable */
    }

  return NO_ERROR;
}

/*
 * scan_alloc_oid_list () - Allocate an index scan OID list.
 *
 * return		  : Error code.
 * oid_buf_p (out)	  : Allocated buffer.
 * oid_buf_capacity (out) : Allocated buffer capacity.
 */
static int
scan_alloc_oid_list (BTREE_ISCAN_OID_LIST ** oid_list_p)
{
  /* Assert expected arguments. */
  assert (oid_list_p != NULL && *oid_list_p == NULL);

  /* Allocate OID list entry. */
  *oid_list_p = (BTREE_ISCAN_OID_LIST *) malloc (sizeof (BTREE_ISCAN_OID_LIST));
  if (*oid_list_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (BTREE_ISCAN_OID_LIST));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Allocate OID list buffer. */
  (*oid_list_p)->capacity = ISCAN_OID_BUFFER_CAPACITY / OR_OID_SIZE;
  (*oid_list_p)->oidp = (OID *) malloc ((*oid_list_p)->capacity * OR_OID_SIZE);
  if ((*oid_list_p)->oidp == NULL)
    {
      /* Could not allocate memory. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (*oid_list_p)->capacity * OR_OID_SIZE);

      /* Free already allocated. */
      free_and_init (*oid_list_p);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* Allocation successful. */
  /* Initialize other fields. */
  (*oid_list_p)->next_list = NULL;
  (*oid_list_p)->max_oid_cnt = (*oid_list_p)->capacity;
  (*oid_list_p)->oid_cnt = 0;
  return NO_ERROR;
}

/*
 * scan_free_oid_list () - Free an index scan OID list.
 *
 * return		  : void
 */
static void
scan_free_oid_list (BTREE_ISCAN_OID_LIST * oid_list_p)
{
  assert (oid_list_p != NULL);

  if (oid_list_p->oidp != NULL)
    {
      free_and_init (oid_list_p->oidp);
    }
  free_and_init (oid_list_p);
}

/*
 * scan_alloc_iscan_oid_buf_list () - Allocate or use a preallocated buffer for index scan OID list.
 *
 * return	  : Error code.
 * oid_list (out) : Output OID list with allocated buffer.
 */
static int
scan_alloc_iscan_oid_buf_list (BTREE_ISCAN_OID_LIST ** oid_list)
{
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
  int error_code = NO_ERROR;

  /* Assert expected argument. */
  assert (oid_list != NULL && *oid_list == NULL);

  /* Is buffer empty? */
  if (scan_Iscan_oid_buf_list != NULL)
    {
      /* Not empty. */
      /* Protect by mutex and try to get from buffer. */
      rv = pthread_mutex_lock (&scan_Iscan_oid_buf_list_mutex);
      /* Was buffer emptied? */
      if (scan_Iscan_oid_buf_list != NULL)
	{
	  /* Not empty */
	  /* Pop first entry. */
	  *oid_list = scan_Iscan_oid_buf_list;

	  /* Update buffer. */
	  scan_Iscan_oid_buf_list = scan_Iscan_oid_buf_list->next_list;
	  scan_Iscan_oid_buf_list_count--;
	  pthread_mutex_unlock (&scan_Iscan_oid_buf_list_mutex);

	  /* Reset next_list link. */
	  (*oid_list)->next_list = NULL;
	  return NO_ERROR;
	}
      /* Empty. */
      pthread_mutex_unlock (&scan_Iscan_oid_buf_list_mutex);
    }

  /* Allocate a new OID list entry. */
  error_code = scan_alloc_oid_list (oid_list);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  /* Safe guard. */
  assert (*oid_list != NULL);
  assert ((*oid_list)->oidp != NULL);
  assert ((*oid_list)->capacity > 0);

  /* Success. */
  return NO_ERROR;
}

/*
 * scan_free_iscan_oid_buf_list () - Free OID buffer from OID list of index scan.
 *
 * return	 : Void.
 * oid_list (in) : OID list to free.
 */
static void
scan_free_iscan_oid_buf_list (BTREE_ISCAN_OID_LIST * oid_list)
{
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* Free entry. */
  rv = pthread_mutex_lock (&scan_Iscan_oid_buf_list_mutex);
  /* Is buffer at its full capacity? */
  if (scan_Iscan_oid_buf_list_count < MAX_NTRANS)
    {
      /* Add oid_list to scan_Iscan_oid_buf_list */
      oid_list->next_list = scan_Iscan_oid_buf_list;
      scan_Iscan_oid_buf_list = oid_list;
      scan_Iscan_oid_buf_list_count++;
    }
  else
    {
      /* Too many buffers, just free it. */
      scan_free_oid_list (oid_list);
    }
  pthread_mutex_unlock (&scan_Iscan_oid_buf_list_mutex);
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
      if (left == rop_range_table[i].left && right == rop_range_table[i].right)
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
 * scan_key_compare ()
 *   val1(in):
 *   val2(in):
 *   num_index_term(in):
 *   return:
 */
static int
scan_key_compare (DB_VALUE * val1, DB_VALUE * val2, int num_index_term)
{
  int rc = DB_UNK;
  DB_TYPE key_type;
  int dummy_diff_column;
  bool dummy_dom_is_desc, dummy_next_dom_is_desc;
  static bool ignore_trailing_space = prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE);
  static int coerce = (ignore_trailing_space) ? 1 : 3;

  if (val1 == NULL || val2 == NULL)
    {
      assert_release (0);
      return rc;
    }

  if (DB_IS_NULL (val1))
    {
      if (DB_IS_NULL (val2))
	{
	  rc = DB_EQ;
	}
      else
	{
	  rc = DB_LT;
	}
    }
  else if (DB_IS_NULL (val2))
    {
      rc = DB_GT;
    }
  else
    {
      key_type = DB_VALUE_DOMAIN_TYPE (val1);
      if (key_type == DB_TYPE_MIDXKEY)
	{
	  rc =
	    pr_midxkey_compare (db_get_midxkey (val1), db_get_midxkey (val2), 1, 1, num_index_term, NULL, NULL,
				NULL, &dummy_diff_column, &dummy_dom_is_desc, &dummy_next_dom_is_desc);
	}
      else
	{
	  /*
	   * we need to compare without ignoring trailing space
	   * corece = 3 enforce"no-ignore trailing space
	   */
	  rc = tp_value_compare (val1, val2, coerce, 1);
	}
    }

  return rc;
}

/*
 * compare_val_op () - compare two values specified by range operator
 *   return:
 *   val1(in):
 *   op1(in):
 *   val2(in):
 *   op2(in):
 *   num_index_term(in):
 */
static ROP_TYPE
compare_val_op (DB_VALUE * val1, ROP_TYPE op1, DB_VALUE * val2, ROP_TYPE op2, int num_index_term)
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

  rc = scan_key_compare (val1, val2, num_index_term);

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
  int p1_num_index_term, p2_num_index_term;
  DB_VALUE *p1_key, *p2_key;

  p1_num_index_term = ((KEY_VAL_RANGE *) p1)->num_index_term;
  p2_num_index_term = ((KEY_VAL_RANGE *) p2)->num_index_term;
  assert_release (p1_num_index_term == p2_num_index_term);

  p1_key = &((KEY_VAL_RANGE *) p1)->key1;
  p2_key = &((KEY_VAL_RANGE *) p2)->key1;

  return scan_key_compare (p1_key, p2_key, p1_num_index_term);
}

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
  static bool ignore_trailing_space = prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE);
  static int coerce = (ignore_trailing_space) ? 1 : 3;

  curp = key_vals;
  nextp = key_vals + 1;
  n = 0;
  while (key_cnt > 1 && n < key_cnt - 1)
    {
      /*
       * we need to compare without ignoring trailing space
       * corece = 3 enforce"no-ignore trailing space
       */
      if (tp_value_compare (&curp->key1, &nextp->key1, coerce, 1) == DB_EQ)
	{
	  pr_clear_value (&nextp->key1);
	  pr_clear_value (&nextp->key2);
	  memmove (nextp, nextp + 1, sizeof (KEY_VAL_RANGE) * (key_cnt - n - 2));
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
  ROP_TYPE cur_op1, cur_op2, next_op1, next_op2;
  ROP_TYPE cmp_1, cmp_2, cmp_3, cmp_4;
  bool is_mergeable;

  cmp_1 = cmp_2 = cmp_3 = cmp_4 = ROP_NA;

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
	  is_mergeable = true;	/* init */
	  cmp_1 = cmp_2 = cmp_3 = cmp_4 = ROP_NA;

	  if (is_mergeable == true)
	    {
	      cmp_1 = compare_val_op (&curp->key2, cur_op2, &nextp->key1, next_op1, curp->num_index_term);
	      if (cmp_1 == ROP_NA || cmp_1 == ROP_LT)
		{
		  is_mergeable = false;	/* error or disjoint */
		}
	    }

	  if (is_mergeable == true)
	    {
	      cmp_2 = compare_val_op (&curp->key1, cur_op1, &nextp->key2, next_op2, curp->num_index_term);
	      if (cmp_2 == ROP_NA || cmp_2 == ROP_GT)
		{
		  is_mergeable = false;	/* error or disjoint */
		}
	    }

	  if (is_mergeable == true)
	    {
	      /* determine the lower bound of the merged key range */
	      cmp_3 = compare_val_op (&curp->key1, cur_op1, &nextp->key1, next_op1, curp->num_index_term);
	      if (cmp_3 == ROP_NA)
		{
		  is_mergeable = false;
		}
	    }

	  if (is_mergeable == true)
	    {
	      /* determine the upper bound of the merged key range */
	      cmp_4 = compare_val_op (&curp->key2, cur_op2, &nextp->key2, next_op2, curp->num_index_term);
	      if (cmp_4 == ROP_NA)
		{
		  is_mergeable = false;
		}
	    }

	  if (is_mergeable == false)
	    {
	      /* they are disjoint */
	      nextp++;
	      next_n++;
	      continue;		/* skip and go ahead */
	    }

	  /* determine the lower bound of the merged key range */
	  if (cmp_3 == ROP_GT_ADJ || cmp_3 == ROP_GT)
	    {
	      pr_clear_value (&curp->key1);
	      curp->key1 = nextp->key1;	/* bitwise copy */
	      db_make_null (&nextp->key1);
	      cur_op1 = next_op1;
	    }
	  else
	    {
	      pr_clear_value (&nextp->key1);
	    }

	  /* determine the upper bound of the merged key range */
	  if (cmp_4 == ROP_LT || cmp_4 == ROP_LT_ADJ)
	    {
	      pr_clear_value (&curp->key2);
	      curp->key2 = nextp->key2;	/* bitwise copy */
	      db_make_null (&nextp->key2);
	      cur_op2 = next_op2;
	    }
	  else
	    {
	      pr_clear_value (&nextp->key2);
	    }

	  /* determine the new range type */
	  rop_to_range (&curp->range, cur_op1, cur_op2);
	  /* remove merged one(nextp) */
	  memmove (nextp, nextp + 1, sizeof (KEY_VAL_RANGE) * (key_cnt - next_n - 1));
	  key_cnt--;
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
check_key_vals (KEY_VAL_RANGE * key_vals, int key_cnt, QPROC_KEY_VAL_FU * key_val_fn)
{
  if (key_cnt <= 1)
    {
      return key_cnt;
    }

  qsort ((void *) key_vals, key_cnt, sizeof (KEY_VAL_RANGE), key_val_compare);

  return ((*key_val_fn) (key_vals, key_cnt));
}

/*
 * scan_dbvals_to_midxkey () -
 *   return: NO_ERROR or ER_code
 *
 *   retval (out):
 *   indexal (out):
 *   btree_domainp (in):
 *   num_term (in):
 *   func (in):
 *   vd (in):
 *   key_minmax (in):
 *   is_iss (in)
 */
static int
scan_dbvals_to_midxkey (THREAD_ENTRY * thread_p, DB_VALUE * retval, bool * indexable, TP_DOMAIN * btree_domainp,
			int num_term, REGU_VARIABLE * func, VAL_DESCR * vd, int key_minmax, bool is_iss,
			DB_VALUE * fetched_values)
{
  int ret = NO_ERROR;
  DB_VALUE *val = NULL;
  DB_TYPE val_type_id;
  DB_MIDXKEY midxkey;

  int idx_ncols = 0, natts, i, j;
  int buf_size, nullmap_size;
  unsigned char *bits;

  regu_variable_list_node *operand;

  char *nullmap_ptr;		/* ponter to boundbits */
  char *key_ptr;		/* current position in key */

  OR_BUF buf;

  bool need_new_setdomain = false;
  TP_DOMAIN *idx_setdomain = NULL, *vals_setdomain = NULL;
  TP_DOMAIN *idx_dom = NULL, *val_dom = NULL, *dom = NULL, *next = NULL;
  TP_DOMAIN dom_buf;
  DB_VALUE *coerced_values = NULL;
  bool *has_coerced_values = NULL;

  *indexable = false;

  if (TP_DOMAIN_TYPE (func->domain) != DB_TYPE_MIDXKEY)
    {
      assert (false);
      return ER_FAILED;
    }

  idx_ncols = btree_domainp->precision;
  if (idx_ncols <= 0)
    {
      assert (false);
      return ER_FAILED;
    }

  idx_setdomain = btree_domainp->setdomain;

#if !defined(NDEBUG)
  {
    int dom_ncols = 0;

    for (idx_dom = idx_setdomain; idx_dom != NULL; idx_dom = idx_dom->next)
      {
	dom_ncols++;
	if (idx_dom->precision < 0)
	  {
	    assert (false);
	    return ER_FAILED;
	  }
      }

    if (dom_ncols <= 0)
      {
	assert (false);
	return ER_FAILED;
      }

    assert (dom_ncols == idx_ncols);
  }
#endif /* NDEBUG */

  buf_size = 0;
  midxkey.buf = NULL;
  midxkey.min_max_val.position = -1;

  /* bitmap is always fully sized */
  nullmap_size = OR_MULTI_BOUND_BIT_BYTES (idx_ncols);
  buf_size = nullmap_size;

  /* check to need a new setdomain */
  for (operand = func->value.funcp->operand, idx_dom = idx_setdomain, i = 0; operand != NULL && idx_dom != NULL;
       operand = operand->next, idx_dom = idx_dom->next, i++)
    {
      ret = fetch_peek_dbval (thread_p, &(operand->value), vd, NULL, NULL, NULL, &val);
      if (ret != NO_ERROR)
	{
	  goto err_exit;
	}

      pr_clear_value (&fetched_values[i]);
      ret = pr_clone_value (val, &fetched_values[i]);
      if (ret != NO_ERROR)
	{
	  goto err_exit;
	}

      if (DB_IS_NULL (val))
	{
	  if (is_iss && i == 0)
	    {
	      /* If this is INDEX SKIP SCAN we allow the first column to be NULL and we don't need a new domain for it */
	      continue;
	    }
	  else
	    {
	      /* to fix multi-column index NULL problem */
	      goto end;
	    }
	}

      val_type_id = DB_VALUE_DOMAIN_TYPE (val);
      if (TP_IS_STRING_TYPE (val_type_id))
	{
	  /* we need to check for maxes */
	  if (val->data.ch.medium.is_max_string)
	    {
	      /* oops, we found max. */
	      midxkey.min_max_val.position = i;
	      midxkey.min_max_val.type = MAX_COLUMN;

	      /* just stop here */
	      break;
	    }
	}

      if (TP_DOMAIN_TYPE (idx_dom) != val_type_id)
	{
	  /* allocate DB_VALUE array to store coerced values. */
	  if (has_coerced_values == NULL)
	    {
	      assert (has_coerced_values == NULL && coerced_values == NULL);
	      coerced_values = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * idx_ncols);
	      if (coerced_values == NULL)
		{
		  goto err_exit;
		}
	      for (j = 0; j < idx_ncols; j++)
		{
		  db_make_null (&coerced_values[j]);
		}

	      has_coerced_values = (bool *) db_private_alloc (thread_p, sizeof (bool) * idx_ncols);
	      if (has_coerced_values == NULL)
		{
		  goto err_exit;
		}
	      memset (has_coerced_values, 0x0, sizeof (bool) * idx_ncols);
	    }

	  /* Coerce the value to index domain. If there is loss, we should make a new setdomain. */
	  ret = tp_value_coerce_strict (val, &coerced_values[i], idx_dom);
	  if (ret != NO_ERROR)
	    {
	      need_new_setdomain = true;
	    }
	  else
	    {
	      has_coerced_values[i] = true;
	    }
	}
      else if (TP_DOMAIN_TYPE (idx_dom) == DB_TYPE_NUMERIC || TP_DOMAIN_TYPE (idx_dom) == DB_TYPE_CHAR
	       || TP_DOMAIN_TYPE (idx_dom) == DB_TYPE_BIT || TP_DOMAIN_TYPE (idx_dom) == DB_TYPE_NCHAR)
	{
	  /* skip variable string domain : DB_TYPE_VARCHAR, DB_TYPE_VARNCHAR, DB_TYPE_VARBIT */

	  val_dom = tp_domain_resolve_value (val, &dom_buf);
	  if (val_dom == NULL)
	    {
	      goto err_exit;
	    }

	  if (!tp_domain_match_ignore_order (idx_dom, val_dom, TP_EXACT_MATCH))
	    {
	      need_new_setdomain = true;
	    }
	}
    }

  /* calculate midxkey's size & make a new setdomain if need */
  /* NOTICE that this will stop the iteration on MAX_COLUMN value if exists.
   * Remaining key values including MAX_COLUMN position will be filled as NULL
   * by btree_coerce_key at the end of this function.
   */
  for (operand = func->value.funcp->operand, idx_dom = idx_setdomain, natts = 0;
       operand != NULL && idx_dom != NULL
       && (midxkey.min_max_val.position == -1 || natts < midxkey.min_max_val.position);
       operand = operand->next, idx_dom = idx_dom->next, natts++)
    {
      /* If there is coerced value, we will use it regardless of whether a new setdomain is required or not. */
      if (has_coerced_values != NULL && has_coerced_values[natts] == true)
	{
	  assert (coerced_values != NULL);
	  val = &coerced_values[natts];
	}
      else
	{
	  assert (fetched_values != NULL);
	  val = &fetched_values[natts];
	}

      if (need_new_setdomain == true)
	{
	  /* make a value's domain */
	  val_dom = tp_domain_resolve_value (val, &dom_buf);
	  if (val_dom == NULL)
	    {
	      goto err_exit;
	    }

	  val_dom = tp_domain_copy (val_dom, false);
	  if (val_dom == NULL)
	    {
	      goto err_exit;
	    }
	  val_dom->is_desc = idx_dom->is_desc;

	  /* make a new setdomain */
	  if (vals_setdomain == NULL)
	    {
	      assert (dom == NULL);
	      vals_setdomain = val_dom;
	    }
	  else
	    {
	      assert (dom != NULL);
	      dom->next = val_dom;
	    }

	  dom = val_dom;
	}
      else
	{
	  dom = idx_dom;
	}

      if (DB_IS_NULL (val))
	{
	  if (is_iss && natts == 0)
	    {
	      /* We allow the first column to be NULL and we're not writing it in the MIDXKEY buffer */
	      continue;
	    }
	  else
	    {
	      /* impossible case */
	      assert_release (false);
	      goto end;
	    }
	}

      buf_size += dom->type->get_index_size_of_value (val);
    }

  /* add more domain to setdomain for partial key */
  if (need_new_setdomain == true)
    {
      assert (dom != NULL);
      if (idx_dom != NULL)
	{
	  val_dom = tp_domain_copy (idx_dom, false);
	  if (val_dom == NULL)
	    {
	      goto err_exit;
	    }

	  dom->next = val_dom;
	}
    }

  midxkey.buf = (char *) db_private_alloc (thread_p, buf_size);
  if (midxkey.buf == NULL)
    {
      retval->need_clear = false;
      goto err_exit;
    }

  nullmap_ptr = midxkey.buf;
  key_ptr = nullmap_ptr + nullmap_size;

  OR_BUF_INIT (buf, key_ptr, buf_size - nullmap_size);

  if (nullmap_size > 0)
    {
      bits = (unsigned char *) nullmap_ptr;
      for (i = 0; i < nullmap_size; i++)
	{
	  bits[i] = (unsigned char) 0;
	}
    }

  /* generate multi columns key (values -> midxkey.buf) */
  for (operand = func->value.funcp->operand, i = 0, dom = (vals_setdomain != NULL) ? vals_setdomain : idx_setdomain;
       operand != NULL && dom != NULL && (i < natts); operand = operand->next, dom = dom->next, i++)
    {
      if (has_coerced_values != NULL && has_coerced_values[i] == true)
	{
	  assert (coerced_values != NULL);
	  val = &coerced_values[i];
	}
      else
	{
	  assert (fetched_values != NULL);
	  val = &fetched_values[i];
	}

      if (DB_IS_NULL (val))
	{
	  if (is_iss && i == 0)
	    {
	      /* There is nothing to write for NULL. Just make sure the bit is not set */
	      OR_CLEAR_BOUND_BIT (nullmap_ptr, i);
	      continue;
	    }
	  else
	    {
	      /* impossible case */
	      assert_release (false);
	      goto end;
	    }
	}

      dom->type->index_writeval (&buf, val);
      OR_ENABLE_BOUND_BIT (nullmap_ptr, i);
    }

  assert (buf_size == CAST_BUFLEN (buf.ptr - midxkey.buf));

  /* Make midxkey DB_VALUE */
  midxkey.size = buf_size;
  midxkey.ncolumns = natts;

  if (vals_setdomain != NULL)
    {
      midxkey.domain = tp_domain_construct (DB_TYPE_MIDXKEY, NULL, idx_ncols, 0, vals_setdomain);
      if (midxkey.domain == NULL)
	{
	  goto err_exit;
	}

      midxkey.domain = tp_domain_cache (midxkey.domain);
    }
  else
    {
      midxkey.domain = btree_domainp;
    }

  ret = db_make_midxkey (retval, &midxkey);
  if (ret != NO_ERROR)
    {
      goto err_exit;
    }

  retval->need_clear = true;

  *indexable = true;

  ret = btree_coerce_key (retval, num_term, btree_domainp, key_minmax);

  if (has_coerced_values)
    {
      db_private_free_and_init (thread_p, has_coerced_values);
    }

  if (coerced_values)
    {
      db_private_free_and_init (thread_p, coerced_values);
    }

  return ret;

end:
  if (midxkey.buf)
    {
      db_private_free_and_init (thread_p, midxkey.buf);
    }

  if (vals_setdomain != NULL)
    {
      for (dom = vals_setdomain; dom != NULL; dom = next)
	{
	  next = dom->next;
	  tp_domain_free (dom);
	}
    }

  if (has_coerced_values)
    {
      db_private_free_and_init (thread_p, has_coerced_values);
    }

  if (coerced_values)
    {
      db_private_free_and_init (thread_p, coerced_values);
    }

  return ret;

err_exit:

  if (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  goto end;
}

/*
 * scan_regu_key_to_index_key:
 */
static int
scan_regu_key_to_index_key (THREAD_ENTRY * thread_p, KEY_RANGE * key_ranges, KEY_VAL_RANGE * key_val_range,
			    INDX_SCAN_ID * iscan_id, TP_DOMAIN * btree_domainp, VAL_DESCR * vd)
{
  bool indexable = true;
  int key_minmax;
  int curr_key_prefix_length = 0;
  int count;
  int ret = NO_ERROR;
  DB_TYPE db_type;
  int key_len;
  regu_variable_list_node *requ_list;

  assert ((key_ranges->range >= GE_LE && key_ranges->range <= INF_LT) || (key_ranges->range == EQ_NA));
  assert (!(key_ranges->key1 == NULL && key_ranges->key2 == NULL));

  if (iscan_id->bt_attrs_prefix_length && iscan_id->bt_num_attrs == 1)
    {
      curr_key_prefix_length = iscan_id->bt_attrs_prefix_length[0];
    }

  /* TO_DO : fix to move this to XASL generator */
  if (key_ranges->key1)
    {
      if (key_ranges->key1->type == TYPE_FUNC && key_ranges->key1->value.funcp->ftype == F_MIDXKEY)
	{
	  for (requ_list = key_ranges->key1->value.funcp->operand, count = 0; requ_list; requ_list = requ_list->next)
	    {
	      count++;
	    }
	}
      else
	{
	  count = 1;
	}
      key_val_range->num_index_term = count;
    }

  if (key_ranges->key2)
    {
      if (key_ranges->key2->type == TYPE_FUNC && key_ranges->key2->value.funcp->ftype == F_MIDXKEY)
	{
	  for (requ_list = key_ranges->key2->value.funcp->operand, count = 0; requ_list; requ_list = requ_list->next)
	    {
	      count++;
	    }
	}
      else
	{
	  assert_release (key_val_range->num_index_term <= 1);
	  count = 1;
	}
      key_val_range->num_index_term = MAX (key_val_range->num_index_term, count);
    }

  if (key_ranges->key1)
    {
      if (key_ranges->key1->type == TYPE_FUNC && key_ranges->key1->value.funcp->ftype == F_MIDXKEY)
	{
	  if (key_val_range->range == GT_INF || key_val_range->range == GT_LE || key_val_range->range == GT_LT)
	    {
	      key_minmax = BTREE_COERCE_KEY_WITH_MAX_VALUE;
	    }
	  else
	    {
	      key_minmax = BTREE_COERCE_KEY_WITH_MIN_VALUE;
	    }

	  ret =
	    scan_dbvals_to_midxkey (thread_p, &key_val_range->key1, &indexable, btree_domainp,
				    key_val_range->num_index_term, key_ranges->key1, vd, key_minmax, iscan_id->iss.use,
				    iscan_id->fetched_values);
	}
      else
	{
	  ret = fetch_copy_dbval (thread_p, key_ranges->key1, vd, NULL, NULL, NULL, &key_val_range->key1);
	  db_type = DB_VALUE_DOMAIN_TYPE (&key_val_range->key1);

	  if (ret == NO_ERROR && curr_key_prefix_length > 0)
	    {
	      if (TP_IS_CHAR_TYPE (db_type) || TP_IS_BIT_TYPE (db_type))
		{
		  key_len = db_get_string_length (&key_val_range->key1);

		  if (key_len > curr_key_prefix_length)
		    {
		      ret = db_string_truncate (&key_val_range->key1, curr_key_prefix_length);
		      key_val_range->is_truncated = true;
		    }
		}
	    }

	  assert (DB_VALUE_TYPE (&key_val_range->key1) != DB_TYPE_MIDXKEY);
	}

      if (ret != NO_ERROR || indexable == false)
	{
	  key_val_range->range = NA_NA;

	  return ret;
	}
    }

  if (key_ranges->key2)
    {
      if (key_ranges->key2->type == TYPE_FUNC && key_ranges->key2->value.funcp->ftype == F_MIDXKEY)
	{
	  if (key_val_range->range == INF_LT || key_val_range->range == GE_LT || key_val_range->range == GT_LT)
	    {
	      key_minmax = BTREE_COERCE_KEY_WITH_MIN_VALUE;
	    }
	  else
	    {
	      key_minmax = BTREE_COERCE_KEY_WITH_MAX_VALUE;
	    }

	  ret =
	    scan_dbvals_to_midxkey (thread_p, &key_val_range->key2, &indexable, btree_domainp,
				    key_val_range->num_index_term, key_ranges->key2, vd, key_minmax, iscan_id->iss.use,
				    iscan_id->fetched_values);
	}
      else
	{
	  ret = fetch_copy_dbval (thread_p, key_ranges->key2, vd, NULL, NULL, NULL, &key_val_range->key2);

	  db_type = DB_VALUE_DOMAIN_TYPE (&key_val_range->key2);

	  if (ret == NO_ERROR && curr_key_prefix_length > 0)
	    {
	      if (TP_IS_CHAR_TYPE (db_type) || TP_IS_BIT_TYPE (db_type))
		{
		  key_len = db_get_string_length (&key_val_range->key2);

		  if (key_len > curr_key_prefix_length)
		    {
		      ret = db_string_truncate (&key_val_range->key2, curr_key_prefix_length);
		      key_val_range->is_truncated = true;
		    }
		}
	    }

	  assert (DB_VALUE_TYPE (&key_val_range->key2) != DB_TYPE_MIDXKEY);
	}

      if (ret != NO_ERROR || indexable == false)
	{
	  key_val_range->range = NA_NA;

	  return ret;
	}
    }
  else
    {
      if (key_ranges->key1 == NULL)
	{
	  /* impossible case */
	  assert (false);

	  key_val_range->range = NA_NA;

	  return ER_FAILED;
	}

      if ((iscan_id->indx_info->range_type == R_KEY || iscan_id->indx_info->range_type == R_KEYLIST)
	  && key_ranges->key1->type == TYPE_FUNC && key_ranges->key1->value.funcp->ftype == F_MIDXKEY)
	{
	  assert (key_val_range->range == EQ_NA);
	  ret = pr_clone_value (&key_val_range->key1, &key_val_range->key2);
	  if (ret != NO_ERROR)
	    {
	      key_val_range->range = NA_NA;

	      return ret;
	    }

	  /* Set minmax type opposite to key1 */
	  if (key_val_range->key2.data.midxkey.min_max_val.type == MIN_COLUMN)
	    {
	      key_val_range->key2.data.midxkey.min_max_val.type = MAX_COLUMN;
	    }
	  else
	    {
	      key_val_range->key2.data.midxkey.min_max_val.type = MIN_COLUMN;
	    }
	}
      else
	{
	  ret = pr_clone_value (&key_val_range->key1, &key_val_range->key2);
	  if (ret != NO_ERROR)
	    {
	      key_val_range->range = NA_NA;

	      return ret;
	    }
	}
    }

  if (key_val_range->range == EQ_NA)
    {
      key_val_range->range = GE_LE;
    }

  switch (iscan_id->indx_info->range_type)
    {
    case R_KEY:
    case R_KEYLIST:
      /* When key received as NULL, currently this is assumed an UNBOUND value and no object value in the index is
       * equal to NULL value in the index scan context. They can be equal to NULL only in the "is NULL" context. */

      /* to fix multi-column index NULL problem */
      if (DB_IS_NULL (&key_val_range->key1))
	{
	  key_val_range->range = NA_NA;

	  return ret;
	}
      break;

    case R_RANGE:
    case R_RANGELIST:
      /* When key received as NULL, currently this is assumed an UNBOUND value and no object value in the index is
       * equal to NULL value in the index scan context. They can be equal to NULL only in the "is NULL" context. */
      if (key_val_range->range >= GE_LE && key_val_range->range <= GT_LT)
	{
	  /* to fix multi-column index NULL problem */
	  if (DB_IS_NULL (&key_val_range->key1) || DB_IS_NULL (&key_val_range->key2))
	    {
	      key_val_range->range = NA_NA;

	      return ret;
	    }
	  else
	    {
	      int c = DB_UNK;

	      c = scan_key_compare (&key_val_range->key1, &key_val_range->key2, key_val_range->num_index_term);

	      if (c == DB_UNK)
		{
		  /* impossible case */
		  assert_release (false);

		  key_val_range->range = NA_NA;

		  return ER_FAILED;
		}
	      else if (c > 0)
		{
		  key_val_range->range = NA_NA;

		  return ret;
		}
	    }
	}
      else if (key_val_range->range >= GE_INF && key_val_range->range <= GT_INF)
	{
	  /* to fix multi-column index NULL problem */
	  if (DB_IS_NULL (&key_val_range->key1))
	    {
	      key_val_range->range = NA_NA;

	      return ret;
	    }
	}
      else if (key_val_range->range >= INF_LE && key_val_range->range <= INF_LT)
	{
	  /* to fix multi-column index NULL problem */
	  if (DB_IS_NULL (&key_val_range->key2))
	    {
	      key_val_range->range = NA_NA;

	      return ret;
	    }
	}
      break;

    default:
      assert_release (false);
      break;			/* impossible case */
    }

  return ret;
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
scan_get_index_oidset (THREAD_ENTRY * thread_p, SCAN_ID * s_id, DB_BIGINT * key_limit_upper,
		       DB_BIGINT * key_limit_lower)
{
  INDX_SCAN_ID *iscan_id;
  FILTER_INFO key_filter;
  indx_info *indx_infop;
  BTREE_SCAN *bts;
  int key_cnt, i;
  KEY_VAL_RANGE *key_vals;
  KEY_RANGE *key_ranges;
  RANGE range, saved_range;
  int ret = NO_ERROR;
  int curr_key_prefix_length = 0;

  /* pointer to INDX_SCAN_ID structure */
  iscan_id = &s_id->s.isid;

  /* pointer to indx_info in INDX_SCAN_ID structure */
  indx_infop = iscan_id->indx_info;

  /* pointer to index scan info. structure */
  bts = &iscan_id->bt_scan;

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

  if (iscan_id->bt_attrs_prefix_length && iscan_id->bt_num_attrs == 1)
    {
      curr_key_prefix_length = iscan_id->bt_attrs_prefix_length[0];
    }

  /* if it is the first time of this scan */
  if (iscan_id->curr_keyno == -1 && indx_infop->key_info.key_cnt == key_cnt)
    {
      /* make DB_VALUE key values from KEY_VALS key ranges */
      for (i = 0; i < key_cnt; i++)
	{
	  /* initialize DB_VALUE first for error case */
	  key_vals[i].range = NA_NA;
	  db_make_null (&key_vals[i].key1);
	  db_make_null (&key_vals[i].key2);
	  key_vals[i].is_truncated = false;
	  key_vals[i].num_index_term = 0;

	  key_vals[i].range = key_ranges[i].range;
	  if (key_vals[i].range == INF_INF)
	    {
	      continue;
	    }

	  ret =
	    scan_regu_key_to_index_key (thread_p, &key_ranges[i], &key_vals[i], iscan_id, bts->btid_int.key_type,
					s_id->vd);

	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  assert_release (key_vals[i].num_index_term > 0);
	}

      /* eliminating duplicated keys and merging ranges are required even though the query optimizer does them because
       * the search keys or ranges could be unbound values at optimization step such as join attribute */
      if (indx_infop->range_type == R_KEYLIST)
	{
	  /* eliminate duplicated keys in the search key list */
	  key_cnt = iscan_id->key_cnt = check_key_vals (key_vals, key_cnt, eliminate_duplicated_keys);
	}
      else if (indx_infop->range_type == R_RANGELIST)
	{
	  /* merge search key ranges */
	  key_cnt = iscan_id->key_cnt = check_key_vals (key_vals, key_cnt, merge_key_ranges);
	}

      /* if is order by skip and first column is descending, the order will be reversed so reverse the key ranges to be
       * desc. */
      if ((indx_infop->range_type == R_KEYLIST || indx_infop->range_type == R_RANGELIST)
	  && ((indx_infop->orderby_desc && indx_infop->orderby_skip)
	      || (indx_infop->groupby_desc && indx_infop->groupby_skip)))
	{
	  /* in both cases we should reverse the key lists if we have a reverse order by or group by which is skipped */
	  check_key_vals (key_vals, key_cnt, reverse_key_list);
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
  scan_init_filter_info (&key_filter, &iscan_id->key_pred, &iscan_id->key_attrs, s_id->val_list, s_id->vd,
			 &iscan_id->cls_oid, iscan_id->bt_num_attrs, iscan_id->bt_attr_ids, &iscan_id->num_vstr,
			 iscan_id->vstr_ids);
  iscan_id->oids_count = 0;
  key_filter.func_idx_col_id = iscan_id->indx_info->func_idx_col_id;

  if (iscan_id->multi_range_opt.use && iscan_id->multi_range_opt.cnt > 0)
    {
      /* reset any previous results for multiple range optimization */
      int i;

      for (i = 0; i < iscan_id->multi_range_opt.cnt; i++)
	{
	  if (iscan_id->multi_range_opt.top_n_items[i] != NULL)
	    {
	      pr_clear_value (&(iscan_id->multi_range_opt.top_n_items[i]->index_value));
	      db_private_free_and_init (thread_p, iscan_id->multi_range_opt.top_n_items[i]);
	    }
	}

      iscan_id->multi_range_opt.cnt = 0;
    }

  /* if the end of this scan */
  if (iscan_id->curr_keyno > key_cnt)
    {
      return NO_ERROR;
    }
  else
    {
      /* Clear output val list to avoid memory leak. */
      regu_variable_list_node *p;
      for (p = iscan_id->indx_cov.regu_val_list; p; p = p->next)
	{
	  pr_clear_value (p->value.vfetch_to);
	}
    }

  switch (indx_infop->range_type)
    {
    case R_KEY:
      /* key value search */

      /* check prerequisite condition */
      range = key_vals[0].range;

      if (range == NA_NA)
	{
	  /* skip this key value */
	  iscan_id->curr_keyno++;
	  break;
	}

      if (key_cnt != 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	  goto exit_on_error;
	}

      ret =
	btree_prepare_bts (thread_p, bts, &indx_infop->btid, iscan_id, &key_vals[0], &key_filter,
			   &iscan_id->cls_oid, key_limit_upper, key_limit_lower, true, NULL);
      if (ret != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  goto exit_on_error;
	}
      ret = btree_range_scan (thread_p, bts, btree_range_scan_select_visible_oids);
      if (ret != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  goto exit_on_error;
	}
      iscan_id->oids_count = bts->n_oids_read_last_iteration;
      assert (iscan_id->oids_count >= 0);

      /* We only want to advance the key ptr if we've exhausted the current crop of oids on the current key. */
      if (BTREE_END_OF_SCAN (bts))
	{
	  iscan_id->curr_keyno++;
	}

      if (iscan_id->multi_range_opt.use)
	{
	  /* with multiple range optimization, we store the only the top N OIDS or index keys: the only valid exit
	   * condition from 'btree_range_search' is when the index scan has reached the end for this key */
	  assert (BTREE_END_OF_SCAN (bts));
	}

      break;

    case R_RANGE:
      /* range search */

      /* check prerequisite condition */
      saved_range = range = key_vals[0].range;

      if (range == NA_NA)
	{
	  /* skip this key value */
	  iscan_id->curr_keyno++;
	  break;
	}

      if (key_cnt != 1 || range < GE_LE || range > INF_INF)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	  goto exit_on_error;
	}

      if (range >= GE_INF && range <= GT_INF)
	{
	  pr_clear_value (&key_vals[0].key2);
	  PRIM_SET_NULL (&key_vals[0].key2);
	}

      if (range >= INF_LE && range <= INF_LT)
	{
	  pr_clear_value (&key_vals[0].key1);
	  PRIM_SET_NULL (&key_vals[0].key1);
	}

      if (key_vals[0].is_truncated == true)
	{			/* specially, key value search */
	  range = GE_LE;
	}

      if (range == INF_INF)
	{
	  /* if we reached the key count limit, break */
	  if (iscan_id->curr_keyno >= key_cnt)
	    {
	      iscan_id->curr_keyno++;
	      break;
	    }

	  pr_clear_value (&key_vals[0].key1);
	  pr_clear_value (&key_vals[0].key2);
	  PRIM_SET_NULL (&key_vals[0].key1);
	  PRIM_SET_NULL (&key_vals[0].key2);

	  assert_release (key_vals[0].num_index_term == 0);
	}

      key_vals[0].range = range;
      ret =
	btree_prepare_bts (thread_p, bts, &indx_infop->btid, iscan_id, &key_vals[0], &key_filter,
			   &iscan_id->cls_oid, key_limit_upper, key_limit_lower, true, NULL);
      if (ret != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  goto exit_on_error;
	}
      ret = btree_range_scan (thread_p, bts, btree_range_scan_select_visible_oids);
      key_vals[0].range = saved_range;
      if (ret != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  goto exit_on_error;
	}
      iscan_id->oids_count = bts->n_oids_read_last_iteration;
      assert (iscan_id->oids_count >= 0);

      /* We only want to advance the key ptr if we've exhausted the current crop of oids on the current key. */
      if (BTREE_END_OF_SCAN (bts))
	{
	  iscan_id->curr_keyno++;
	}

      if (iscan_id->multi_range_opt.use)
	{
	  /* with multiple range optimization, we store the only the top N OIDS or index keys: the only valid exit
	   * condition from 'btree_range_search' is when the index scan has reached the end for this key */
	  assert (BTREE_END_OF_SCAN (bts));
	}
      break;

    case R_KEYLIST:
      /* multiple key value search */

      /* for each key value */
      while (iscan_id->curr_keyno < key_cnt)
	{
	  /* check prerequisite condition */
	  range = key_vals[iscan_id->curr_keyno].range;

	  if (range == NA_NA)
	    {
	      /* skip this key value and continue to the next */
	      iscan_id->curr_keyno++;
	      if (key_limit_upper && !key_limit_lower && indx_infop->key_info.key_limit_reset)
		{
		  if (scan_init_index_key_limit (thread_p, iscan_id, &indx_infop->key_info, s_id->vd) != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  *key_limit_upper = iscan_id->key_limit_upper;
		}
	      continue;
	    }

	  ret =
	    btree_prepare_bts (thread_p, bts, &indx_infop->btid, iscan_id, &key_vals[iscan_id->curr_keyno],
			       &key_filter, &iscan_id->cls_oid, key_limit_upper, key_limit_lower, true, NULL);
	  if (ret != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      goto exit_on_error;
	    }
	  ret = btree_range_scan (thread_p, bts, btree_range_scan_select_visible_oids);
	  if (ret != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      goto exit_on_error;
	    }
	  iscan_id->oids_count = bts->n_oids_read_last_iteration;
	  assert (iscan_id->oids_count >= 0);

	  /* We only want to advance the key ptr if we've exhausted the current crop of oids on the current key. */
	  if (BTREE_END_OF_SCAN (bts))
	    {
	      iscan_id->curr_keyno++;
	      /* reset upper key limit, if flag is set */
	      if (key_limit_upper && !key_limit_lower && indx_infop->key_info.key_limit_reset)
		{
		  if (scan_init_index_key_limit (thread_p, iscan_id, &indx_infop->key_info, s_id->vd) != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  *key_limit_upper = iscan_id->key_limit_upper;
		}
	    }

	  if (iscan_id->multi_range_opt.use)
	    {
	      /* with multiple range optimization, we store the only the top N OIDS or index keys: the only valid exit
	       * condition from 'btree_range_search' is when the index scan has reached the end for this key */
	      assert (BTREE_END_OF_SCAN (bts));
	      /* continue loop : exhaust all keys in one shot when in multiple range search optimization mode */
	      continue;
	    }
	  if (iscan_id->oids_count > 0)
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
	  saved_range = range = key_vals[iscan_id->curr_keyno].range;

	  if (range == NA_NA)
	    {
	      /* skip this key value and continue to the next */
	      iscan_id->curr_keyno++;
	      if (key_limit_upper && !key_limit_lower && indx_infop->key_info.key_limit_reset)
		{
		  if (scan_init_index_key_limit (thread_p, iscan_id, &indx_infop->key_info, s_id->vd) != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  *key_limit_upper = iscan_id->key_limit_upper;
		}
	      continue;
	    }

	  if (range < GE_LE || range > INF_INF)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	      goto exit_on_error;
	    }

	  if (range >= GE_INF && range <= GT_INF)
	    {
	      pr_clear_value (&key_vals[iscan_id->curr_keyno].key2);
	      PRIM_SET_NULL (&key_vals[iscan_id->curr_keyno].key2);
	    }

	  if (key_vals[iscan_id->curr_keyno].is_truncated == true)
	    {			/* specially, key value search */
	      range = GE_LE;
	    }

	  if (range >= INF_LE && range <= INF_LT)
	    {
	      pr_clear_value (&key_vals[iscan_id->curr_keyno].key1);
	      PRIM_SET_NULL (&key_vals[iscan_id->curr_keyno].key1);
	    }

	  if (range == INF_INF)
	    {
	      if (key_cnt != 1)
		{
		  assert_release (0);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
		  goto exit_on_error;
		}

	      /* if we reached the key count limit, break */
	      if (iscan_id->curr_keyno >= key_cnt)
		{
		  iscan_id->curr_keyno++;
		  break;
		}

	      pr_clear_value (&key_vals[0].key1);
	      pr_clear_value (&key_vals[0].key2);
	      PRIM_SET_NULL (&key_vals[0].key1);
	      PRIM_SET_NULL (&key_vals[0].key2);
	    }

	  key_vals[iscan_id->curr_keyno].range = range;
	  ret =
	    btree_prepare_bts (thread_p, bts, &indx_infop->btid, iscan_id, &key_vals[iscan_id->curr_keyno],
			       &key_filter, &iscan_id->cls_oid, key_limit_upper, key_limit_lower, true, NULL);
	  if (ret != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      goto exit_on_error;
	    }
	  ret = btree_range_scan (thread_p, bts, btree_range_scan_select_visible_oids);
	  key_vals[iscan_id->curr_keyno].range = saved_range;
	  if (ret != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      goto exit_on_error;
	    }
	  iscan_id->oids_count = bts->n_oids_read_last_iteration;
	  assert (iscan_id->oids_count >= 0);

	  /* We only want to advance the key ptr if we've exhausted the current crop of oids on the current key. */
	  if (BTREE_END_OF_SCAN (bts))
	    {
	      iscan_id->curr_keyno++;
	      /* reset upper key limit, if flag is set */
	      if (key_limit_upper && !key_limit_lower && indx_infop->key_info.key_limit_reset)
		{
		  if (scan_init_index_key_limit (thread_p, iscan_id, &indx_infop->key_info, s_id->vd) != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  *key_limit_upper = iscan_id->key_limit_upper;
		}
	    }

	  if (iscan_id->multi_range_opt.use)
	    {
	      /* with multiple range optimization, we store the only the top N OIDS or index keys: the only valid exit
	       * condition from 'btree_range_search' is when the index scan has reached the end for this key */
	      assert (BTREE_END_OF_SCAN (bts));
	      /* continue loop : exhaust all keys in one shot when in multiple range search optimization mode */
	      continue;
	    }
	  if (iscan_id->oids_count > 0)
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

  /* When covering index is used, 'index_scan_in_oid_order' parameter is ignored. */
  if (iscan_id->oid_list != NULL && iscan_id->oid_list->oidp != NULL && iscan_id->oids_count > 1
      && iscan_id->iscan_oid_order == true && iscan_id->need_count_only == false)
    {
      qsort (iscan_id->oid_list->oidp, iscan_id->oids_count, sizeof (OID), oid_compare);
    }

end:

  if (key_limit_upper != NULL && *key_limit_upper == 0)
    {
      /* End scan here! */
      iscan_id->curr_keyno = key_cnt;
    }

  /* if the end of this scan */
  if (iscan_id->curr_keyno == key_cnt)
    {
      for (i = 0; i < key_cnt; i++)
	{
	  pr_clear_value (&key_vals[i].key1);
	  pr_clear_value (&key_vals[i].key2);
	}
      iscan_id->curr_keyno++;	/* to prevent duplicate frees */
    }

  if (thread_is_on_trace (thread_p))
    {
      s_id->scan_stats.read_keys += iscan_id->bt_scan.read_keys;
      iscan_id->bt_scan.read_keys = 0;
      s_id->scan_stats.qualified_keys += iscan_id->bt_scan.qualified_keys;
      iscan_id->bt_scan.qualified_keys = 0;
    }

  return ret;

exit_on_error:
  iscan_id->curr_keyno = key_cnt;	/* set as end of this scan */

  ret = (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
 *   mvcc_select_lock_needed(in):
 *   scan_op_type(in): scan operation type
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
scan_init_scan_id (SCAN_ID * scan_id, bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed,
		   int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
		   VAL_DESCR * vd)
{
  scan_id->status = S_OPENED;
  scan_id->position = S_BEFORE;
  scan_id->direction = S_FORWARD;

  scan_id->mvcc_select_lock_needed = mvcc_select_lock_needed;
  scan_id->scan_op_type = scan_op_type;
  scan_id->fixed = fixed;

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
  scan_id->scan_immediately_stop = false;
}

/*
 * scan_open_heap_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   mvcc_select_lock_needed(in):
 *   scan_op_type(in): scan operation type
 *   fixed(in):
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
 *   cache_recordinfo(in):
 *   regu_list_recordinfo(in):
 *
 * Note: If you feel the need
 */
int
scan_open_heap_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		     /* fields of SCAN_ID */
		     bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed, int grouped,
		     QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list, VAL_DESCR * vd,
		     /* fields of HEAP_SCAN_ID */
		     OID * cls_oid, HFID * hfid, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr,
		     regu_variable_list_node * regu_list_rest, int num_attrs_pred, ATTR_ID * attrids_pred,
		     HEAP_CACHE_ATTRINFO * cache_pred, int num_attrs_rest, ATTR_ID * attrids_rest,
		     HEAP_CACHE_ATTRINFO * cache_rest, SCAN_TYPE scan_type, DB_VALUE ** cache_recordinfo,
		     regu_variable_list_node * regu_list_recordinfo)
{
  HEAP_SCAN_ID *hsidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;

  /* scan type is HEAP SCAN or HEAP SCAN RECORD INFO */
  assert (scan_type == S_HEAP_SCAN || scan_type == S_HEAP_SCAN_RECORD_INFO);
  scan_id->type = scan_type;

  /* initialize SCAN_ID structure */
  scan_init_scan_id (scan_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch, join_dbval, val_list,
		     vd);

  /* initialize HEAP_SCAN_ID structure */
  hsidp = &scan_id->s.hsid;

  /* class object OID */
  COPY_OID (&hsidp->cls_oid, cls_oid);

  /* heap file identifier */
  hsidp->hfid = *hfid;		/* bitwise copy */

  /* OID within the heap */
  UT_CAST_TO_NULL_HEAP_OID (&hsidp->hfid, &hsidp->curr_oid);

  /* scan predicates */
  scan_init_scan_pred (&hsidp->scan_pred, regu_list_pred, pr,
		       ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));
  /* attribute information from predicates */
  scan_init_scan_attrs (&hsidp->pred_attrs, num_attrs_pred, attrids_pred, cache_pred);

  /* regulator variable list for other than predicates */
  hsidp->rest_regu_list = regu_list_rest;

  /* attribute information from other than predicates */
  scan_init_scan_attrs (&hsidp->rest_attrs, num_attrs_rest, attrids_rest, cache_rest);

  /* flags */
  /* do not reset hsidp->caches_inited here */
  hsidp->scancache_inited = false;
  hsidp->scanrange_inited = false;

  hsidp->cache_recordinfo = cache_recordinfo;
  hsidp->recordinfo_regu_list = regu_list_recordinfo;

  return NO_ERROR;
}

/*
 * scan_open_heap_page_scan () - Opens a page by page heap scan.
 *
 * return		    : Error code.
 * thread_p (in)	    :
 * scan_id (in)		    :
 * val_list (in)	    :
 * vd (in)		    :
 * cls_oid (in)		    :
 * hfid (in)		    :
 * pr (in)		    :
 * scan_type (in)	    :
 * cache_page_info (in)	    :
 * regu_list_page_info (in) :
 */
int
scan_open_heap_page_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
			  /* fields of SCAN_ID */
			  val_list_node * val_list, VAL_DESCR * vd,
			  /* fields of HEAP_SCAN_ID */
			  OID * cls_oid, HFID * hfid, PRED_EXPR * pr, SCAN_TYPE scan_type, DB_VALUE ** cache_page_info,
			  regu_variable_list_node * regu_list_page_info)
{
  HEAP_PAGE_SCAN_ID *hpsidp = NULL;
  DB_TYPE single_node_type = DB_TYPE_NULL;

  scan_id->type = scan_type;

  scan_init_scan_id (scan_id, true, S_SELECT, true, false, QPROC_NO_SINGLE_INNER, NULL, val_list, vd);

  hpsidp = &scan_id->s.hpsid;

  COPY_OID (&hpsidp->cls_oid, cls_oid);
  hpsidp->hfid = *hfid;
  hpsidp->cache_page_info = cache_page_info;
  hpsidp->page_info_regu_list = regu_list_page_info;
  scan_init_scan_pred (&hpsidp->scan_pred, NULL, pr, (pr == NULL) ? NULL : eval_fnc (thread_p, pr, &single_node_type));
  VPID_SET_NULL (&hpsidp->curr_vpid);
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
			   int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
			   val_list_node * val_list, VAL_DESCR * vd,
			   /* fields of HEAP_SCAN_ID */
			   OID * cls_oid, HFID * hfid, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr,
			   regu_variable_list_node * regu_list_rest, int num_attrs_pred, ATTR_ID * attrids_pred,
			   HEAP_CACHE_ATTRINFO * cache_pred, int num_attrs_rest, ATTR_ID * attrids_rest,
			   HEAP_CACHE_ATTRINFO * cache_rest)
{
  HEAP_SCAN_ID *hsidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;

  /* scan type is CLASS ATTR SCAN */
  scan_id->type = S_CLASS_ATTR_SCAN;

  /* initialize SCAN_ID structure */
  /* mvcc_select_lock_needed = false, fixed = true */
  scan_init_scan_id (scan_id, false, S_SELECT, true, grouped, single_fetch, join_dbval, val_list, vd);

  /* initialize HEAP_SCAN_ID structure */
  hsidp = &scan_id->s.hsid;

  /* class object OID */
  COPY_OID (&hsidp->cls_oid, cls_oid);
  /* heap file identifier */
  hsidp->hfid = *hfid;		/* bitwise copy */
  /* OID within the heap */
  UT_CAST_TO_NULL_HEAP_OID (&hsidp->hfid, &hsidp->curr_oid);

  /* scan predicates */
  scan_init_scan_pred (&hsidp->scan_pred, regu_list_pred, pr,
		       ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));
  /* attribute information from predicates */
  scan_init_scan_attrs (&hsidp->pred_attrs, num_attrs_pred, attrids_pred, cache_pred);
  /* regulator vairable list for other than predicates */
  hsidp->rest_regu_list = regu_list_rest;
  /* attribute information from other than predicates */
  scan_init_scan_attrs (&hsidp->rest_attrs, num_attrs_rest, attrids_rest, cache_rest);

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
 *   mvcc_select_lock_needed(in):
 *   fixed(in):
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
 *   pr_range(in):
 *   regu_list_range(in):
 *   num_attrs_key(in):
 *   attrids_key(in):
 *   num_attrs_pred(in):
 *   attrids_pred(in):
 *   cache_pred(in):
 *   num_attrs_rest(in):
 *   attrids_rest(in):
 *   cache_rest(in):
 *   num_attrs_range(in):
 *   attrids_range(in):
 *   cache_range(in):
 *   iscan_oid_order(in):
 *   query_id(in):
 *
 * Note: If you feel the need
 */
int
scan_open_index_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		      /* fields of SCAN_ID */
		      bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed, int grouped,
		      QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list, VAL_DESCR * vd,
		      /* fields of INDX_SCAN_ID */
		      indx_info * indx_info, OID * cls_oid, HFID * hfid, regu_variable_list_node * regu_list_key,
		      PRED_EXPR * pr_key, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr,
		      regu_variable_list_node * regu_list_rest, PRED_EXPR * pr_range,
		      regu_variable_list_node * regu_list_range, valptr_list_node * output_val_list,
		      regu_variable_list_node * regu_val_list, int num_attrs_key, ATTR_ID * attrids_key,
		      HEAP_CACHE_ATTRINFO * cache_key, int num_attrs_pred, ATTR_ID * attrids_pred,
		      HEAP_CACHE_ATTRINFO * cache_pred, int num_attrs_rest, ATTR_ID * attrids_rest,
		      HEAP_CACHE_ATTRINFO * cache_rest, int num_attrs_range, ATTR_ID * attrids_range,
		      HEAP_CACHE_ATTRINFO * cache_range, bool iscan_oid_order, QUERY_ID query_id)
{
  int ret = NO_ERROR;
  INDX_SCAN_ID *isidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  BTID *btid;
  VPID Root_vpid;
  PAGE_PTR Root;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTREE_SCAN *BTS;
  int coverage_enabled;
  int func_index_col_id;

  /* scan type is INDEX SCAN */
  scan_id->type = S_INDX_SCAN;

  /* initialize SCAN_ID structure */
  scan_init_scan_id (scan_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch, join_dbval, val_list,
		     vd);

  /* read Root page header info */
  btid = &indx_info->btid;

  Root_vpid.pageid = btid->root_pageid;
  Root_vpid.volid = btid->vfid.volid;

  Root = pgbuf_fix (thread_p, &Root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (Root == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, Root, PAGE_BTREE);

  root_header = btree_get_root_header (thread_p, Root);
  if (root_header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, Root);
      return ER_FAILED;
    }

  /* initialize INDEX_SCAN_ID structure */
  isidp = &scan_id->s.isid;

  /* index information */
  isidp->indx_info = indx_info;

  /* init allocated fields */
  isidp->bt_num_attrs = 0;
  isidp->bt_attr_ids = NULL;
  isidp->vstr_ids = NULL;
  isidp->oid_list = NULL;
  isidp->curr_oidp = NULL;
  isidp->copy_buf = NULL;
  isidp->copy_buf_len = 0;
  isidp->key_vals = NULL;

  isidp->indx_cov.type_list = NULL;
  isidp->indx_cov.list_id = NULL;
  isidp->indx_cov.tplrec = NULL;
  isidp->indx_cov.lsid = NULL;
  isidp->fetched_values = NULL;

  /* index scan info */
  BTS = &isidp->bt_scan;
  BTREE_INIT_SCAN (BTS);

  /* construct BTID_INT structure */
  BTS->btid_int.sys_btid = btid;

  if (btree_glean_root_header_info (thread_p, root_header, &BTS->btid_int) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, Root);
      goto exit_on_error;
    }
  BTS->is_btid_int_valid = true;

  pgbuf_unfix_and_init (thread_p, Root);

  /* initialize key limits */
  if (scan_init_index_key_limit (thread_p, isidp, &indx_info->key_info, vd) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* attribute information of the index key */
  if (heap_get_indexinfo_of_btid (thread_p, cls_oid, &indx_info->btid, &isidp->bt_type, &isidp->bt_num_attrs,
				  &isidp->bt_attr_ids, &isidp->bt_attrs_prefix_length, NULL,
				  &func_index_col_id) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* attribute information of the variable string attrs in index key */
  isidp->num_vstr = 0;
  isidp->vstr_ids = NULL;

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
    {
      isidp->num_vstr = isidp->bt_num_attrs;	/* init to maximum */
      isidp->vstr_ids = (ATTR_ID *) db_private_alloc (thread_p, isidp->num_vstr * sizeof (ATTR_ID));
      if (isidp->vstr_ids == NULL)
	{
	  goto exit_on_error;
	}
    }

  /* indicator whether covering index is used or not */
  coverage_enabled = (indx_info->coverage != 0) && (scan_op_type == S_SELECT) && !mvcc_select_lock_needed;
  scan_id->scan_stats.loose_index_scan = indx_info->ils_prefix_len > 0;

  /* is a single range? */
  isidp->one_range = false;

  /* initial values */
  isidp->curr_keyno = -1;
  isidp->curr_oidno = -1;

  /* OID buffer */
  if (coverage_enabled)
    {
      /* Covering index do not use an oid buffer. */
      scan_id->scan_stats.covered_index = true;
    }
  else
    {
      ret = scan_alloc_iscan_oid_buf_list (&isidp->oid_list);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      /* Safe guard. */
      assert (isidp->oid_list->oidp != NULL);
      assert (isidp->oid_list->capacity > 0);
      assert (isidp->oid_list->next_list == NULL);

      /* Initialize OID list. */
      isidp->oid_list->max_oid_cnt = ISCAN_OID_BUFFER_COUNT;
      isidp->oid_list->oid_cnt = 0;
      /* Initialize current OID pointer to start of buffer. */
      isidp->curr_oidp = isidp->oid_list->oidp;

      /* Safe guard */
      /* OID count limit should not exceed buffer capacity. */
      assert (isidp->oid_list->max_oid_cnt <= isidp->oid_list->capacity);
    }

  /* class object OID */
  COPY_OID (&isidp->cls_oid, cls_oid);

  /* heap file identifier */
  isidp->hfid = *hfid;		/* bitwise copy */

  /* key filter */
  scan_init_scan_pred (&isidp->key_pred, regu_list_key, pr_key,
		       ((pr_key) ? eval_fnc (thread_p, pr_key, &single_node_type) : NULL));

  /* attribute information from key filter */
  scan_init_scan_attrs (&isidp->key_attrs, num_attrs_key, attrids_key, cache_key);

  /* scan predicates */
  scan_init_scan_pred (&isidp->scan_pred, regu_list_pred, pr,
		       ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));

  /* attribute information from predicates */
  scan_init_scan_attrs (&isidp->pred_attrs, num_attrs_pred, attrids_pred, cache_pred);

  /* scan range filter */
  scan_init_scan_pred (&isidp->range_pred, regu_list_range, pr_range,
		       ((pr_range) ? eval_fnc (thread_p, pr_range, &single_node_type) : NULL));

  /* attribute information from range filter */
  scan_init_scan_attrs (&isidp->range_attrs, num_attrs_range, attrids_range, cache_range);

  /* regulator variable list for other than predicates */
  isidp->rest_regu_list = regu_list_rest;

  /* attribute information from other than predicates */
  scan_init_scan_attrs (&isidp->rest_attrs, num_attrs_rest, attrids_rest, cache_rest);

  /* flags */
  /* do not reset hsidp->caches_inited here */
  isidp->scancache_inited = false;

  /* convert key values in the form of REGU_VARIABLE to the form of DB_VALUE */
  isidp->key_cnt = indx_info->key_info.key_cnt;
  if (isidp->key_cnt > 0)
    {
      bool need_copy_buf;

      isidp->key_vals = (KEY_VAL_RANGE *) db_private_alloc (thread_p, isidp->key_cnt * sizeof (KEY_VAL_RANGE));
      if (isidp->key_vals == NULL)
	{
	  goto exit_on_error;
	}

      need_copy_buf = false;	/* init */

      if (BTS->btid_int.key_type == NULL || BTS->btid_int.key_type->type == NULL)
	{
	  goto exit_on_error;
	}

      /* check for the need of index key copy_buf */
      if (TP_DOMAIN_TYPE (BTS->btid_int.key_type) == DB_TYPE_MIDXKEY)
	{
	  /* found multi-column key-val */
	  need_copy_buf = true;

	  /* make fetched values for scan_regu_key_to_index_key(). */
	  isidp->fetched_values = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * isidp->bt_num_attrs);
	  if (isidp->fetched_values == NULL)
	    {
	      goto exit_on_error;
	    }
	  for (int j = 0; j < isidp->bt_num_attrs; j++)
	    {
	      db_make_null (&isidp->fetched_values[j]);
	    }
	}
      else
	{			/* single-column index */
	  if (indx_info->key_info.key_ranges[0].range != EQ_NA)
	    {
	      /* found single-column key-range, not key-val */
	      if (QSTR_IS_ANY_CHAR_OR_BIT (TP_DOMAIN_TYPE (BTS->btid_int.key_type)))
		{
		  /* this type needs index key copy_buf */
		  need_copy_buf = true;
		}
	    }
	}

      if (need_copy_buf)
	{
	  /* alloc index key copy_buf */
	  isidp->copy_buf = (char *) db_private_alloc (thread_p, DBVAL_BUFSIZE);
	  if (isidp->copy_buf == NULL)
	    {
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

  isidp->iscan_oid_order = iscan_oid_order;

  if (scan_init_indx_coverage (thread_p, coverage_enabled, output_val_list, regu_val_list, vd, query_id,
			       root_header->node.max_key_len, func_index_col_id, &(isidp->indx_cov)) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (scan_init_iss (isidp) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* initialize multiple range search optimization structure */
  {
    bool use_multi_range_opt = (isidp->bt_num_attrs > 1 && isidp->indx_info->key_info.key_limit_reset == true
				&& isidp->key_limit_upper > 0 && isidp->key_limit_upper < DB_INT32_MAX
				&& isidp->key_limit_lower == -1) ? true : false;

    if (scan_init_multi_range_optimization (thread_p, &(isidp->multi_range_opt), use_multi_range_opt,
					    (int) isidp->key_limit_upper) != NO_ERROR)
      {
	goto exit_on_error;
      }

    scan_id->scan_stats.multi_range_opt = isidp->multi_range_opt.use;
  }

  return ret;

exit_on_error:

  if (isidp->key_vals)
    {
      db_private_free_and_init (thread_p, isidp->key_vals);
    }
  if (isidp->fetched_values)
    {
      db_private_free_and_init (thread_p, isidp->fetched_values);
    }
  if (isidp->bt_attr_ids)
    {
      db_private_free_and_init (thread_p, isidp->bt_attr_ids);
    }
  if (isidp->vstr_ids)
    {
      db_private_free_and_init (thread_p, isidp->vstr_ids);
    }
  if (isidp->oid_list != NULL)
    {
      scan_free_iscan_oid_buf_list (isidp->oid_list);
      isidp->oid_list = NULL;
    }
  if (isidp->copy_buf)
    {
      db_private_free_and_init (thread_p, isidp->copy_buf);
    }
  if (isidp->indx_cov.type_list != NULL)
    {
      if (isidp->indx_cov.type_list->domp != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->indx_cov.type_list->domp);
	}
      db_private_free_and_init (thread_p, isidp->indx_cov.type_list);
    }
  if (isidp->indx_cov.list_id != NULL)
    {
      QFILE_FREE_AND_INIT_LIST_ID (isidp->indx_cov.list_id);
    }
  if (isidp->indx_cov.tplrec != NULL)
    {
      if (isidp->indx_cov.tplrec->tpl != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->indx_cov.tplrec->tpl);
	}
      db_private_free_and_init (thread_p, isidp->indx_cov.tplrec);
    }
  if (isidp->indx_cov.lsid != NULL)
    {
      db_private_free_and_init (thread_p, isidp->indx_cov.lsid);
    }

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * scan_open_index_key_info_scan () - Opens a scan for index key info
 *
 * return		   : Error code.
 * thread_p (in)	   : Thread entry.
 * scan_id (out)	   : Pointer where scan data is saved.
 * val_list (in)	   : XASL values list.
 * vd (in)		   : XASL values descriptors.
 * indx_info (in)	   : Index info.
 * cls_oid (in)		   : Class object identifier.
 * hfid (in)		   : Heap file identifier.
 * pr (in)		   : Scan predicate.
 * output_val_list (in)	   : Output value pointers list.
 * iscan_oid_order (in)	   : Index scan OID order.
 * query_id (in)	   : Query identifier.
 * key_info_values (in)	   : Array of value pointers to store key info.
 * key_info_regu_list (in) : Regulator variable list for key info.
 */
int
scan_open_index_key_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
			       /* fields of SCAN_ID */
			       val_list_node * val_list, VAL_DESCR * vd,
			       /* fields of INDX_SCAN_ID */
			       indx_info * indx_info, OID * cls_oid, HFID * hfid, PRED_EXPR * pr,
			       valptr_list_node * output_val_list, bool iscan_oid_order, QUERY_ID query_id,
			       DB_VALUE ** key_info_values, regu_variable_list_node * key_info_regu_list)
{
  int ret = NO_ERROR;
  INDX_SCAN_ID *isidp = NULL;
  BTID *btid = NULL;
  VPID root_vpid;
  PAGE_PTR root_page = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTREE_SCAN *bts = NULL;
  int func_index_col_id;
  DB_TYPE single_node_type = DB_TYPE_NULL;

  scan_id->type = S_INDX_KEY_INFO_SCAN;

  /* initialize SCAN_ID structure */
  scan_init_scan_id (scan_id, 1, S_SELECT, false, false, QPROC_NO_SINGLE_INNER, NULL, val_list, vd);

  /* read root_page page header info */
  btid = &indx_info->btid;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (root_page == NULL)
    {
      return ER_FAILED;
    }
  root_header = btree_get_root_header (thread_p, root_page);
  pgbuf_unfix_and_init (thread_p, root_page);

  /* initialize INDEX_SCAN_ID structure */
  isidp = &scan_id->s.isid;

  /* index information */
  isidp->indx_info = indx_info;

  /* init allocated fields */
  isidp->bt_num_attrs = 0;
  isidp->bt_attr_ids = NULL;
  isidp->vstr_ids = NULL;
  isidp->oid_list = NULL;
  isidp->curr_oidp = NULL;
  isidp->copy_buf = NULL;
  isidp->copy_buf_len = 0;
  isidp->key_vals = NULL;

  isidp->indx_cov.type_list = NULL;
  isidp->indx_cov.list_id = NULL;
  isidp->indx_cov.tplrec = NULL;
  isidp->indx_cov.lsid = NULL;

  /* initialize key limits */
  if (scan_init_index_key_limit (thread_p, isidp, &indx_info->key_info, vd) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* index scan info */
  bts = &isidp->bt_scan;
  BTREE_INIT_SCAN (bts);

  /* construct BTID_INT structure */
  bts->btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (thread_p, root_header, &bts->btid_int) != NO_ERROR)
    {
      goto exit_on_error;
    }
  bts->is_btid_int_valid = true;

  /* attribute information of the index key */
  if (heap_get_indexinfo_of_btid (thread_p, cls_oid, &indx_info->btid, &isidp->bt_type, &isidp->bt_num_attrs,
				  NULL, NULL, NULL, &func_index_col_id) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* attribute information of the variable string attrs in index key */
  isidp->num_vstr = 0;
  isidp->vstr_ids = NULL;

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
    {
      isidp->num_vstr = isidp->bt_num_attrs;	/* init to maximum */
      isidp->vstr_ids = (ATTR_ID *) db_private_alloc (thread_p, isidp->num_vstr * sizeof (ATTR_ID));
      if (isidp->vstr_ids == NULL)
	{
	  goto exit_on_error;
	}
    }

  /* is a single range? */
  isidp->one_range = true;

  /* initial values */
  isidp->curr_keyno = -1;
  isidp->curr_oidno = -1;

  /* class object OID */
  COPY_OID (&isidp->cls_oid, cls_oid);

  /* heap file identifier */
  isidp->hfid = *hfid;		/* bitwise copy */

  /* flags */
  /* do not reset hsidp->caches_inited here */
  isidp->scancache_inited = false;

  /* convert key values in the form of REGU_VARIABLE to the form of DB_VALUE */
  isidp->key_cnt = 0;
  isidp->key_vals = NULL;

  /* scan predicate */
  scan_init_scan_pred (&isidp->scan_pred, NULL, pr, ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));

  isidp->iscan_oid_order = iscan_oid_order;

  if (scan_init_indx_coverage (thread_p, false, NULL, NULL, vd, query_id, root_header->node.max_key_len,
			       func_index_col_id, &(isidp->indx_cov)) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (scan_init_iss (isidp) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* initialize multiple range search optimization structure */
  isidp->multi_range_opt.use = false;

  isidp->key_info_values = key_info_values;
  isidp->key_info_regu_list = key_info_regu_list;

  return ret;

exit_on_error:

  if (isidp->key_vals)
    {
      db_private_free_and_init (thread_p, isidp->key_vals);
    }
  if (isidp->bt_attr_ids)
    {
      db_private_free_and_init (thread_p, isidp->bt_attr_ids);
    }
  if (isidp->vstr_ids)
    {
      db_private_free_and_init (thread_p, isidp->vstr_ids);
    }
  assert (isidp->oid_list == NULL);
  if (isidp->copy_buf)
    {
      db_private_free_and_init (thread_p, isidp->copy_buf);
    }
  if (isidp->indx_cov.type_list != NULL)
    {
      if (isidp->indx_cov.type_list->domp != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->indx_cov.type_list->domp);
	}
      db_private_free_and_init (thread_p, isidp->indx_cov.type_list);
    }
  if (isidp->indx_cov.list_id != NULL)
    {
      QFILE_FREE_AND_INIT_LIST_ID (isidp->indx_cov.list_id);
    }
  if (isidp->indx_cov.tplrec != NULL)
    {
      if (isidp->indx_cov.tplrec->tpl != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->indx_cov.tplrec->tpl);
	}
      db_private_free_and_init (thread_p, isidp->indx_cov.tplrec);
    }
  if (isidp->indx_cov.lsid != NULL)
    {
      db_private_free_and_init (thread_p, isidp->indx_cov.lsid);
    }

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * scan_open_index_node_info_scan () - Opens a scan on b-tree nodes.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * scan_id (out)	    : Scan data.
 * val_list (in)	    : XASL value list.
 * vd (in)		    : XASL value descriptors.
 * indx_info (in)	    : Index info.
 * pr (in)		    : Scan predicate.
 * node_info_values (in)    : Array of value pointers to store b-tree node
 *			      information.
 * node_info_regu_list (in) : Regulator variable list.
 */
int
scan_open_index_node_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				/* fields of SCAN_ID */
				val_list_node * val_list, VAL_DESCR * vd,
				/* fields of INDX_SCAN_ID */
				indx_info * indx_info, PRED_EXPR * pr, DB_VALUE ** node_info_values,
				regu_variable_list_node * node_info_regu_list)
{
  INDEX_NODE_SCAN_ID *idx_nsid_p = NULL;
  VPID root_vpid;
  PAGE_PTR root_page = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  BTID *btid = NULL;

  assert (scan_id != NULL);

  scan_id->type = S_INDX_NODE_INFO_SCAN;

  /* initialize SCAN_ID structure */
  scan_init_scan_id (scan_id, 1, S_SELECT, false, false, QPROC_NO_SINGLE_INNER, NULL, val_list, vd);

  idx_nsid_p = &scan_id->s.insid;
  idx_nsid_p->indx_info = indx_info;

  /* scan predicate */
  scan_init_scan_pred (&idx_nsid_p->scan_pred, NULL, pr, ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));

  BTREE_NODE_SCAN_INIT (&idx_nsid_p->btns);

  /* read root_page page header info */
  btid = &indx_info->btid;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (root_page == NULL)
    {
      return ER_FAILED;
    }
  root_header = btree_get_root_header (thread_p, root_page);
  pgbuf_unfix_and_init (thread_p, root_page);

  /* construct BTID_INT structure */
  idx_nsid_p->btns.btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (thread_p, root_header, &idx_nsid_p->btns.btid_int) != NO_ERROR)
    {
      return ER_FAILED;
    }

  idx_nsid_p->node_info_values = node_info_values;
  idx_nsid_p->node_info_regu_list = node_info_regu_list;
  idx_nsid_p->caches_inited = false;

  return NO_ERROR;
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
		     int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
		     VAL_DESCR * vd,
		     /* fields of LLIST_SCAN_ID */
		     QFILE_LIST_ID * list_id, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr,
		     regu_variable_list_node * regu_list_rest, regu_variable_list_node * regu_list_build,
		     regu_variable_list_node * regu_list_probe, int hash_list_scan_yn)
{
  LLIST_SCAN_ID *llsidp;
  int val_cnt;
  DB_TYPE single_node_type = DB_TYPE_NULL;

  /* scan type is LIST SCAN */
  scan_id->type = S_LIST_SCAN;

  /* initialize SCAN_ID structure */
  /* mvcc_select_lock_needed = false, fixed = true */
  scan_init_scan_id (scan_id, false, S_SELECT, true, grouped, single_fetch, join_dbval, val_list, vd);

  /* initialize LLIST_SCAN_ID structure */
  llsidp = &scan_id->s.llsid;

  /* list file ID */
  llsidp->list_id = list_id;	/* points to XASL tree */

  /* scan predicates */
  scan_init_scan_pred (&llsidp->scan_pred, regu_list_pred, pr,
		       ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));

  /* regulator variable list for other than predicates */
  llsidp->rest_regu_list = regu_list_rest;

  /* init for hash list scan */
  /* regulator variable list for build, probe */
  llsidp->hlsid.build_regu_list = regu_list_build;
  llsidp->hlsid.probe_regu_list = regu_list_probe;
  llsidp->hlsid.need_coerce_type = false;

  /* check if hash list scan is possible? */
  llsidp->hlsid.hash_list_scan_yn = check_hash_list_scan (llsidp, &val_cnt, hash_list_scan_yn);
  if (llsidp->hlsid.hash_list_scan_yn != HASH_METH_NOT_USE)
    {
      bool on_trace;
      TSC_TICKS start_tick, end_tick;
      TSCTIMEVAL tv_diff;

      on_trace = thread_is_on_trace (thread_p);
      if (on_trace)
	{
	  tsc_getticks (&start_tick);
	}

      /* create hash table */
      llsidp->hlsid.hash_table =
	mht_create_hls ("Hash List Scan", llsidp->list_id->tuple_cnt, qdata_hash_scan_key, qdata_hscan_key_eq);
      if (llsidp->hlsid.hash_table == NULL)
	{
	  return S_ERROR;
	}

      /* alloc temp key */
      llsidp->hlsid.temp_key = qdata_alloc_hscan_key (thread_p, val_cnt, false);
      llsidp->hlsid.temp_new_key = qdata_alloc_hscan_key (thread_p, val_cnt, true);
      if (scan_start_scan (thread_p, scan_id) != NO_ERROR)
	{
	  return S_ERROR;
	}
      if (scan_build_hash_list_scan (thread_p, scan_id) == S_ERROR)
	{
	  return S_ERROR;
	}
      scan_end_scan (thread_p, scan_id);

      if (on_trace)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (scan_id->scan_stats.elapsed_hash_build, tv_diff);
	}
    }
  else
    {
      llsidp->hlsid.hash_table = NULL;
      llsidp->hlsid.temp_key = NULL;
      llsidp->hlsid.temp_new_key = NULL;
      llsidp->hlsid.curr_hash_entry = NULL;
    }

  return NO_ERROR;
}

/*
 * scan_open_showstmt_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   show_type(in):
 *   arg_list(in):
 */
int
scan_open_showstmt_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
			 /* fields of SCAN_ID */
			 int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
			 VAL_DESCR * vd,
			 /* fields of SHOWSTMT_SCAN_ID */
			 PRED_EXPR * pr, SHOWSTMT_TYPE show_type, regu_variable_list_node * arg_list)
{
  SHOWSTMT_SCAN_ID *stsidp;
  int i, arg_cnt, out_cnt;
  regu_variable_list_node *regu_var_p;
  REGU_VARIABLE *regu;
  QPROC_DB_VALUE_LIST valp;
  DB_VALUE **arg_values = NULL, **out_values = NULL;
  DB_TYPE single_node_type = DB_TYPE_NULL;
  int error;

  /* scan type is S_SHOWSTMT_SCAN */
  scan_id->type = S_SHOWSTMT_SCAN;

  /* initialize SCAN_ID structure */
  /* readonly_scan = true, fixed = true */
  scan_init_scan_id (scan_id, true, S_SELECT, true, grouped, single_fetch, join_dbval, val_list, vd);

  /* initialize SHOWSTMT_SCAN_ID structure */
  stsidp = &scan_id->s.stsid;

  for (regu_var_p = arg_list, i = 0; regu_var_p; regu_var_p = regu_var_p->next)
    {
      i++;
    }

  arg_cnt = i;
  if (arg_cnt > 0)
    {
      arg_values = (DB_VALUE **) db_private_alloc (thread_p, arg_cnt * sizeof (DB_VALUE *));
      if (arg_values == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}

      for (regu_var_p = arg_list, i = 0; regu_var_p; regu_var_p = regu_var_p->next, i++)
	{
	  regu = &regu_var_p->value;
	  assert (regu != NULL && regu->type == TYPE_POS_VALUE);
	  error = fetch_peek_dbval (thread_p, regu, vd, NULL, NULL, NULL, &arg_values[i]);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      assert (i == arg_cnt);
    }

  /* prepare out_values */
  out_cnt = val_list->val_cnt;
  out_values = (DB_VALUE **) db_private_alloc (thread_p, out_cnt * sizeof (DB_VALUE *));
  if (out_values == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }

  for (valp = val_list->valp, i = 0; valp; valp = valp->next, i++)
    {
      out_values[i] = valp->val;
    }
  assert (i == out_cnt);

  stsidp->show_type = show_type;
  stsidp->arg_values = arg_values;
  stsidp->arg_cnt = arg_cnt;
  stsidp->out_values = out_values;
  stsidp->out_cnt = out_cnt;
  stsidp->cursor = 0;
  stsidp->ctx = NULL;

  /* scan predicates */
  scan_init_scan_pred (&stsidp->scan_pred, NULL, pr, ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));

  return NO_ERROR;

exit_on_error:
  if (arg_values != NULL)
    {
      db_private_free_and_init (thread_p, arg_values);
    }

  if (out_values != NULL)
    {
      db_private_free_and_init (thread_p, out_values);
    }
  return error;
}



/*
 * scan_open_values_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 *   valptr_list(in):
 */
int
scan_open_values_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
		       /* fields of SCAN_ID */
		       int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
		       VAL_DESCR * vd,
		       /* fields of REGU_VALUES_SCAN_ID */
		       valptr_list_node * valptr_list)
{
  REGU_VALUES_SCAN_ID *rvsidp;

  assert (valptr_list != NULL);

  scan_id->type = S_VALUES_SCAN;

  /* initialize SCAN_ID structure */
  /* mvcc_select_lock_needed = false, fixed = true */
  scan_init_scan_id (scan_id, false, S_SELECT, true, grouped, single_fetch, join_dbval, val_list, vd);

  rvsidp = &scan_id->s.rvsid;
  rvsidp->regu_list = valptr_list->valptrp;
  rvsidp->value_cnt = valptr_list->valptr_cnt;

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
		    int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
		    VAL_DESCR * vd,
		    /* fields of SET_SCAN_ID */
		    REGU_VARIABLE * set_ptr, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr)
{
  SET_SCAN_ID *ssidp;
  DB_TYPE single_node_type = DB_TYPE_NULL;

  /* scan type is SET SCAN */
  scan_id->type = S_SET_SCAN;

  /* initialize SCAN_ID structure */
  /* mvcc_select_lock_needed = false, fixed = true */
  scan_init_scan_id (scan_id, false, S_SELECT, true, grouped, single_fetch, join_dbval, val_list, vd);

  /* initialize SET_SCAN_ID structure */
  ssidp = &scan_id->s.ssid;

  ssidp->set_ptr = set_ptr;	/* points to XASL tree */

  /* scan predicates */
  scan_init_scan_pred (&ssidp->scan_pred, regu_list_pred, pr,
		       ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));

  return NO_ERROR;
}

/*
 * scan_open_json_table_scan () -
 *   return: NO_ERROR
 *   scan_id(out): Scan identifier
 *   grouped(in):
 *   single_fetch(in):
 *   join_dbval(in):
 *   val_list(in):
 *   vd(in):
 */
int
scan_open_json_table_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, int grouped, QPROC_SINGLE_FETCH single_fetch,
			   DB_VALUE * join_dbval, val_list_node * val_list, VAL_DESCR * vd, PRED_EXPR * pr)
{
  DB_TYPE single_node_type = DB_TYPE_NULL;

  /* scan type is JSON_TABLE SCAN */
  assert (scan_id->type == S_JSON_TABLE_SCAN);

  /* initialize SCAN_ID structure */
  /* mvcc_select_lock_needed = false, fixed = true */
  scan_init_scan_id (scan_id, false, S_SELECT, true, grouped, single_fetch, join_dbval, val_list, vd);

  // scan_init_scan_pred
  scan_init_scan_pred (&scan_id->s.jtid.get_predicate (), NULL, pr,
		       ((pr) ? eval_fnc (thread_p, pr, &single_node_type) : NULL));
  scan_id->s.jtid.set_value_descriptor (vd);

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
		       int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
		       VAL_DESCR * vd,
		       /* */
		       QFILE_LIST_ID * list_id, method_sig_list * meth_sig_list)
{
  /* scan type is METHOD SCAN */
  scan_id->type = S_METHOD_SCAN;

  /* initialize SCAN_ID structure */
  /* mvcc_select_lock_needed = false, fixed = true */
  scan_init_scan_id (scan_id, false, S_SELECT, true, grouped, single_fetch, join_dbval, val_list, vd);

  return method_open_scan (thread_p, &scan_id->s.vaid.scan_buf, list_id, meth_sig_list);
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
  int i;
  HEAP_SCAN_ID *hsidp = NULL;
  INDX_SCAN_ID *isidp = NULL;
  INDEX_NODE_SCAN_ID *insidp = NULL;
  LLIST_SCAN_ID *llsidp = NULL;
  SET_SCAN_ID *ssidp = NULL;
  REGU_VALUES_SCAN_ID *rvsidp = NULL;
  REGU_VALUE_LIST *regu_value_list = NULL;
  regu_variable_list_node *list_node = NULL;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;
  JSON_TABLE_SCAN_ID *jtidp = NULL;

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
    case S_HEAP_SCAN_RECORD_INFO:
      hsidp = &scan_id->s.hsid;
      UT_CAST_TO_NULL_HEAP_OID (&hsidp->hfid, &hsidp->curr_oid);
      if (!OID_IS_ROOTOID (&hsidp->cls_oid))
	{
	  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
	  if (mvcc_snapshot == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      if (scan_id->grouped)
	{
	  ret = heap_scanrange_start (thread_p, &hsidp->scan_range, &hsidp->hfid, &hsidp->cls_oid, mvcc_snapshot);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->scanrange_inited = true;
	}
      else
	{
	  /* A new argument(is_indexscan = false) is appended */
	  ret =
	    heap_scancache_start (thread_p, &hsidp->scan_cache, &hsidp->hfid, &hsidp->cls_oid, scan_id->fixed, false,
				  mvcc_snapshot);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->scancache_inited = true;
	}
      if (hsidp->caches_inited != true)
	{
	  hsidp->pred_attrs.attr_cache->num_values = -1;
	  ret =
	    heap_attrinfo_start (thread_p, &hsidp->cls_oid, hsidp->pred_attrs.num_attrs, hsidp->pred_attrs.attr_ids,
				 hsidp->pred_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->rest_attrs.attr_cache->num_values = -1;
	  ret =
	    heap_attrinfo_start (thread_p, &hsidp->cls_oid, hsidp->rest_attrs.num_attrs, hsidp->rest_attrs.attr_ids,
				 hsidp->rest_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      heap_attrinfo_end (thread_p, hsidp->pred_attrs.attr_cache);
	      goto exit_on_error;
	    }
	  if (hsidp->cache_recordinfo != NULL)
	    {
	      /* initialize cache_recordinfo values */
	      for (i = 0; i < HEAP_RECORD_INFO_COUNT; i++)
		{
		  db_make_null (hsidp->cache_recordinfo[i]);
		}
	    }
	  hsidp->caches_inited = true;
	}
      break;

    case S_HEAP_PAGE_SCAN:
      VPID_SET_NULL (&scan_id->s.hpsid.curr_vpid);
      break;

    case S_CLASS_ATTR_SCAN:
      hsidp = &scan_id->s.hsid;
      hsidp->pred_attrs.attr_cache->num_values = -1;
      if (hsidp->caches_inited != true)
	{
	  ret =
	    heap_attrinfo_start (thread_p, &hsidp->cls_oid, hsidp->pred_attrs.num_attrs, hsidp->pred_attrs.attr_ids,
				 hsidp->pred_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  hsidp->rest_attrs.attr_cache->num_values = -1;
	  ret =
	    heap_attrinfo_start (thread_p, &hsidp->cls_oid, hsidp->rest_attrs.num_attrs, hsidp->rest_attrs.attr_ids,
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
      if (!OID_IS_ROOTOID (&isidp->cls_oid))
	{
	  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
	  if (mvcc_snapshot == NULL)
	    {
	      goto exit_on_error;
	    }
	}

      /* A new argument(is_indexscan = true) is appended */
      ret =
	heap_scancache_start (thread_p, &isidp->scan_cache, &isidp->hfid, &isidp->cls_oid, scan_id->fixed, true,
			      mvcc_snapshot);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      isidp->scancache_inited = true;
      if (isidp->caches_inited != true)
	{
	  if (isidp->range_pred.regu_list != NULL)
	    {
	      isidp->range_attrs.attr_cache->num_values = -1;
	      ret =
		heap_attrinfo_start (thread_p, &isidp->cls_oid, isidp->range_attrs.num_attrs,
				     isidp->range_attrs.attr_ids, isidp->range_attrs.attr_cache);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  if (isidp->key_pred.regu_list != NULL)
	    {
	      isidp->key_attrs.attr_cache->num_values = -1;
	      ret =
		heap_attrinfo_start (thread_p, &isidp->cls_oid, isidp->key_attrs.num_attrs, isidp->key_attrs.attr_ids,
				     isidp->key_attrs.attr_cache);
	      if (ret != NO_ERROR)
		{
		  if (isidp->range_pred.regu_list != NULL)
		    {
		      heap_attrinfo_end (thread_p, isidp->range_attrs.attr_cache);
		    }
		  goto exit_on_error;
		}
	    }
	  isidp->pred_attrs.attr_cache->num_values = -1;
	  ret =
	    heap_attrinfo_start (thread_p, &isidp->cls_oid, isidp->pred_attrs.num_attrs, isidp->pred_attrs.attr_ids,
				 isidp->pred_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      if (isidp->range_pred.regu_list != NULL)
		{
		  heap_attrinfo_end (thread_p, isidp->range_attrs.attr_cache);
		}
	      if (isidp->key_pred.regu_list != NULL)
		{
		  heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
		}
	      goto exit_on_error;
	    }
	  isidp->rest_attrs.attr_cache->num_values = -1;
	  ret =
	    heap_attrinfo_start (thread_p, &isidp->cls_oid, isidp->rest_attrs.num_attrs, isidp->rest_attrs.attr_ids,
				 isidp->rest_attrs.attr_cache);
	  if (ret != NO_ERROR)
	    {
	      if (isidp->range_pred.regu_list != NULL)
		{
		  heap_attrinfo_end (thread_p, isidp->range_attrs.attr_cache);
		}
	      if (isidp->key_pred.regu_list != NULL)
		{
		  heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
		}
	      heap_attrinfo_end (thread_p, isidp->pred_attrs.attr_cache);
	      goto exit_on_error;
	    }
	  isidp->caches_inited = true;
	}
      isidp->oids_count = 0;
      isidp->curr_keyno = -1;
      isidp->curr_oidno = -1;
      isidp->one_range = false;
      break;

    case S_INDX_KEY_INFO_SCAN:
      isidp = &scan_id->s.isid;

      if (!isidp->caches_inited)
	{
	  for (i = 0; i < BTREE_KEY_INFO_COUNT; i++)
	    {
	      db_make_null (isidp->key_info_values[i]);
	    }
	}
      isidp->caches_inited = true;
      isidp->oids_count = 0;
      isidp->curr_keyno = -1;
      isidp->curr_oidno = -1;
      break;

    case S_INDX_NODE_INFO_SCAN:
      insidp = &scan_id->s.insid;

      if (!insidp->caches_inited)
	{
	  for (i = 0; i < BTREE_NODE_INFO_COUNT; i++)
	    {
	      db_make_null (insidp->node_info_values[i]);
	    }
	  insidp->caches_inited = true;
	}
      BTREE_NODE_SCAN_INIT (&insidp->btns);
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

    case S_SHOWSTMT_SCAN:
      if (showstmt_start_scan (thread_p, scan_id) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case S_VALUES_SCAN:
      rvsidp = &scan_id->s.rvsid;
      if (rvsidp->regu_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	  goto exit_on_error;
	}

      for (list_node = rvsidp->regu_list; list_node; list_node = list_node->next)
	{
	  regu_value_list = list_node->value.value.reguval_list;
	  assert (regu_value_list != NULL && regu_value_list->regu_list != NULL);

	  regu_value_list->current_value = regu_value_list->regu_list;
	}
      break;

    case S_SET_SCAN:
      ssidp = &scan_id->s.ssid;
      db_make_null (&ssidp->set);
      break;

    case S_JSON_TABLE_SCAN:
      jtidp = &scan_id->s.jtid;
      // todo: what else to add here?
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

  return ret;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
	  s_id->position = (s_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
	  OID_SET_NULL (&s_id->s.hsid.curr_oid);
	}
      break;

    case S_INDX_SCAN:
      if (s_id->grouped)
	{
	  if (s_id->direction == S_FORWARD && s_id->s.isid.iscan_oid_order == true)
	    {
	      s_id->s.isid.curr_oidno = s_id->s.isid.oids_count;
	      s_id->direction = S_BACKWARD;
	    }
	  else
	    {
	      s_id->s.isid.curr_oidno = -1;
	      s_id->direction = S_FORWARD;
	    }

	  /* reinitialize index skip scan structure */
	  if (scan_init_iss (&s_id->s.isid) != NO_ERROR)
	    {
	      status = S_ERROR;
	      break;
	    }
	}
      else
	{
	  INDX_COV *indx_cov_p;

	  s_id->s.isid.curr_oidno = -1;
	  s_id->s.isid.curr_keyno = -1;
	  s_id->position = S_BEFORE;
	  BTREE_RESET_SCAN (&s_id->s.isid.bt_scan);

	  /* reset key limits */
	  if (s_id->s.isid.indx_info)
	    {
	      if (scan_init_index_key_limit (thread_p, &s_id->s.isid, &s_id->s.isid.indx_info->key_info, s_id->vd) !=
		  NO_ERROR)
		{
		  status = S_ERROR;
		  break;
		}
	    }

	  /* reinitialize index skip scan structure */
	  if (scan_init_iss (&s_id->s.isid) != NO_ERROR)
	    {
	      status = S_ERROR;
	      break;
	    }

	  /* reset index covering */
	  indx_cov_p = &(s_id->s.isid.indx_cov);
	  if (indx_cov_p->lsid != NULL)
	    {
	      qfile_close_scan (thread_p, indx_cov_p->lsid);
	    }

	  if (indx_cov_p->list_id != NULL)
	    {
	      qfile_destroy_list (thread_p, indx_cov_p->list_id);
	      QFILE_FREE_AND_INIT_LIST_ID (indx_cov_p->list_id);

	      indx_cov_p->list_id = qfile_open_list (thread_p, indx_cov_p->type_list, NULL, indx_cov_p->query_id, 0);
	      if (indx_cov_p->list_id == NULL)
		{
		  status = S_ERROR;
		}
	    }
	}
      break;

    case S_LIST_SCAN:
      /* may have scanned some already so clean up */
      qfile_end_scan_fix (thread_p, &s_id->s.llsid.lsid);
      qfile_close_scan (thread_p, &s_id->s.llsid.lsid);

      /* open list file scan for this outer row */
      if (qfile_open_list_scan (s_id->s.llsid.list_id, &s_id->s.llsid.lsid) != NO_ERROR)
	{
	  status = S_ERROR;
	  break;
	}
      qfile_start_scan_fix (thread_p, &s_id->s.llsid.lsid);
      s_id->position = S_BEFORE;
      s_id->s.llsid.lsid.position = S_BEFORE;
      break;

    case S_SHOWSTMT_SCAN:
      s_id->s.stsid.cursor = 0;
      s_id->position = S_BEFORE;
      break;

    case S_CLASS_ATTR_SCAN:
    case S_SET_SCAN:
    case S_JSON_TABLE_SCAN:
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
 * scan_next_scan_block () - Move the scan to the next scan block.
 *                    If there are no more scan blocks left, S_END is returned.
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
    case S_HEAP_SCAN_RECORD_INFO:
    case S_HEAP_PAGE_SCAN:
      if (s_id->grouped)
	{
	  /* grouped, fixed scan */
	  if (s_id->direction == S_FORWARD)
	    {
	      sp_scan = heap_scanrange_to_following (thread_p, &s_id->s.hsid.scan_range, NULL);
	    }
	  else
	    {
	      sp_scan = heap_scanrange_to_prior (thread_p, &s_id->s.hsid.scan_range, NULL);
	    }

	  if (sp_scan == S_SUCCESS || sp_scan == S_END)
	    {
	      return sp_scan;
	    }
	  else
	    {
	      return S_ERROR;
	    }
	}
      else
	{
	  if (s_id->direction == S_FORWARD)
	    {
	      if (s_id->position == S_BEFORE)
		{
		  return S_SUCCESS;
		}
	      else
		{
		  return S_END;
		}
	    }
	  else
	    {
	      if (s_id->position == S_AFTER)
		{
		  return S_SUCCESS;
		}
	      else
		{
		  return S_END;
		}
	    }
	}

    case S_INDX_SCAN:
      if (s_id->grouped)
	{
	  if ((s_id->direction == S_FORWARD && s_id->position == S_BEFORE)
	      || (!BTREE_END_OF_SCAN (&s_id->s.isid.bt_scan) || s_id->s.isid.indx_info->range_type == R_KEYLIST
		  || s_id->s.isid.indx_info->range_type == R_RANGELIST))
	    {
	      if (!(s_id->position == S_BEFORE && s_id->s.isid.one_range == true))
		{
		  /* get the next set of object identifiers specified in the range */
		  if (scan_get_index_oidset (thread_p, s_id, NULL, NULL) != NO_ERROR)
		    {
		      return S_ERROR;
		    }

		  if (s_id->s.isid.oids_count == 0)
		    {		/* range is empty */
		      s_id->position = S_AFTER;
		      return S_END;
		    }

		  if (s_id->position == S_BEFORE && BTREE_END_OF_SCAN (&s_id->s.isid.bt_scan)
		      && s_id->s.isid.indx_info->range_type != R_KEYLIST
		      && s_id->s.isid.indx_info->range_type != R_RANGELIST)
		    {
		      s_id->s.isid.one_range = true;
		    }
		}

	      if (s_id->s.isid.iscan_oid_order == true)
		{
		  s_id->position = S_ON;
		  s_id->direction = S_BACKWARD;
		  s_id->s.isid.curr_oidno = s_id->s.isid.oids_count;
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

    case S_INDX_KEY_INFO_SCAN:
    case S_INDX_NODE_INFO_SCAN:
      if (s_id->grouped)
	{
	  assert (0);
	  return S_ERROR;
	}
      return ((s_id->position == S_BEFORE) ? S_SUCCESS : S_END);

    case S_CLASS_ATTR_SCAN:
    case S_LIST_SCAN:
    case S_SHOWSTMT_SCAN:
    case S_SET_SCAN:
    case S_METHOD_SCAN:
    case S_JSON_TABLE_SCAN:
    case S_VALUES_SCAN:
      return (s_id->position == S_BEFORE) ? S_SUCCESS : S_END;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return S_ERROR;
    }
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
  REGU_VALUES_SCAN_ID *rvsidp;
  SET_SCAN_ID *ssidp;
  KEY_VAL_RANGE *key_vals;
  JSON_TABLE_SCAN_ID *jtidp;
  int i;

  if (scan_id == NULL)
    {
      return;
    }

  if ((scan_id->status == S_ENDED) || (scan_id->status == S_CLOSED))
    {
      return;
    }

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
    case S_HEAP_SCAN_RECORD_INFO:
      hsidp = &scan_id->s.hsid;

      /* do not free attr_cache here. xs_clear_access_spec_list() will free attr_caches. */

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
      if (scan_id->direction == S_FORWARD)
	{
	  scan_id->direction = S_BACKWARD;
	}
      else
	{
	  scan_id->direction = S_FORWARD;
	}
      break;

    case S_CLASS_ATTR_SCAN:
      /* do not free attr_cache here. xs_clear_access_spec_list() will free attr_caches. */
      break;

    case S_INDX_SCAN:
      isidp = &scan_id->s.isid;

      /* do not free attr_cache here. xs_clear_access_spec_list() will free attr_caches. */

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
      /* clear last_key */
      (void) scan_init_iss (isidp);
      break;

    case S_LIST_SCAN:
      llsidp = &scan_id->s.llsid;
      qfile_end_scan_fix (thread_p, &llsidp->lsid);
      qfile_close_scan (thread_p, &llsidp->lsid);
      break;

    case S_SHOWSTMT_SCAN:
      showstmt_end_scan (thread_p, scan_id);
      break;

    case S_VALUES_SCAN:
      rvsidp = &scan_id->s.rvsid;
      break;

    case S_SET_SCAN:
      ssidp = &scan_id->s.ssid;
      pr_clear_value (&ssidp->set);
      break;

    case S_JSON_TABLE_SCAN:
      jtidp = &scan_id->s.jtid;
      jtidp->end (thread_p);
      break;

    case S_METHOD_SCAN:
      break;

    default:
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
  SHOWSTMT_SCAN_ID *stsidp;
  LLIST_SCAN_ID *llsidp;

  if (scan_id == NULL || scan_id->status == S_CLOSED)
    {
      return;
    }

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
    case S_HEAP_SCAN_RECORD_INFO:
    case S_HEAP_PAGE_SCAN:
    case S_CLASS_ATTR_SCAN:
    case S_VALUES_SCAN:
      break;

    case S_INDX_SCAN:
      isidp = &scan_id->s.isid;
      if (isidp->key_vals)
	{
	  db_private_free_and_init (thread_p, isidp->key_vals);
	}
      if (isidp->fetched_values)
	{
	  for (int j = 0; j < isidp->bt_num_attrs; j++)
	    {
	      pr_clear_value (&isidp->fetched_values[j]);
	    }
	  db_private_free_and_init (thread_p, isidp->fetched_values);
	}

      /* free allocated memory for the scan */
      if (isidp->bt_attr_ids)
	{
	  db_private_free_and_init (thread_p, isidp->bt_attr_ids);
	}
      if (isidp->bt_attrs_prefix_length)
	{
	  db_private_free_and_init (thread_p, isidp->bt_attrs_prefix_length);
	}
      if (isidp->vstr_ids)
	{
	  db_private_free_and_init (thread_p, isidp->vstr_ids);
	}
      if (isidp->oid_list != NULL)
	{
	  scan_free_iscan_oid_buf_list (isidp->oid_list);
	  isidp->oid_list = NULL;
	}

      /* free index key copy_buf */
      if (isidp->copy_buf)
	{
	  db_private_free_and_init (thread_p, isidp->copy_buf);
	}

      /* free index covering */
      if (isidp->indx_cov.lsid != NULL)
	{
	  qfile_close_scan (thread_p, isidp->indx_cov.lsid);
	  db_private_free_and_init (thread_p, isidp->indx_cov.lsid);
	}
      if (isidp->indx_cov.list_id != NULL)
	{
	  qfile_close_list (thread_p, isidp->indx_cov.list_id);
	  qfile_destroy_list (thread_p, isidp->indx_cov.list_id);
	  QFILE_FREE_AND_INIT_LIST_ID (isidp->indx_cov.list_id);
	}
      if (isidp->indx_cov.type_list != NULL)
	{
	  if (isidp->indx_cov.type_list->domp != NULL)
	    {
	      db_private_free_and_init (thread_p, isidp->indx_cov.type_list->domp);
	    }
	  db_private_free_and_init (thread_p, isidp->indx_cov.type_list);
	}
      if (isidp->indx_cov.tplrec != NULL)
	{
	  if (isidp->indx_cov.tplrec->tpl != NULL)
	    {
	      db_private_free_and_init (thread_p, isidp->indx_cov.tplrec->tpl);
	    }
	  db_private_free_and_init (thread_p, isidp->indx_cov.tplrec);
	}

      /* free multiple range optimization struct */
      if (isidp->multi_range_opt.top_n_items != NULL)
	{
	  int i;

	  for (i = 0; i < isidp->multi_range_opt.size; i++)
	    {
	      if (isidp->multi_range_opt.top_n_items[i] != NULL)
		{
		  pr_clear_value (&(isidp->multi_range_opt.top_n_items[i]->index_value));
		  db_private_free_and_init (thread_p, isidp->multi_range_opt.top_n_items[i]);
		}
	    }
	  db_private_free_and_init (thread_p, isidp->multi_range_opt.top_n_items);
	  isidp->multi_range_opt.top_n_items = NULL;
	  db_private_free_and_init (thread_p, isidp->multi_range_opt.tplrec.tpl);
	  isidp->multi_range_opt.tplrec.tpl = 0;
	}
      /* free buffer */
      if (isidp->multi_range_opt.buffer != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->multi_range_opt.buffer);
	}
      if (isidp->multi_range_opt.sort_att_idx != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->multi_range_opt.sort_att_idx);
	}
      if (isidp->multi_range_opt.is_desc_order != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->multi_range_opt.is_desc_order);
	}
      if (isidp->multi_range_opt.sort_col_dom != NULL)
	{
	  db_private_free_and_init (thread_p, isidp->multi_range_opt.sort_col_dom);
	}
      memset ((void *) (&(isidp->multi_range_opt)), 0, sizeof (MULTI_RANGE_OPT));
      break;

    case S_LIST_SCAN:
      llsidp = &scan_id->s.llsid;
      /* clear hash list scan table */
      if (llsidp->hlsid.hash_table != NULL)
	{
#if 0
	  (void) mht_dump_hls (thread_p, stdout, llsidp->hlsid.hash_table, 1, qdata_print_hash_scan_entry,
			       (void *) &llsidp->hlsid.hash_list_scan_yn);
	  printf ("temp file : tuple count = %d, file_size = %dK\n", llsidp->list_id->tuple_cnt,
		  llsidp->list_id->page_cnt * 16);
#endif
	  mht_clear_hls (llsidp->hlsid.hash_table, qdata_free_hscan_entry, (void *) thread_p);
	  mht_destroy_hls (llsidp->hlsid.hash_table);
	}
      /* free temp keys and values */
      if (llsidp->hlsid.temp_key != NULL)
	{
	  qdata_free_hscan_key (thread_p, llsidp->hlsid.temp_key, llsidp->hlsid.temp_key->val_count);
	  llsidp->hlsid.temp_key = NULL;
	}
      /* free temp new keys and values */
      if (llsidp->hlsid.temp_new_key != NULL)
	{
	  qdata_free_hscan_key (thread_p, llsidp->hlsid.temp_new_key, llsidp->hlsid.temp_new_key->val_count);
	  llsidp->hlsid.temp_new_key = NULL;
	}
      break;

    case S_SHOWSTMT_SCAN:
      stsidp = &scan_id->s.stsid;
      if (stsidp->arg_values != NULL)
	{
	  db_private_free_and_init (thread_p, stsidp->arg_values);
	}
      if (stsidp->out_values != NULL)
	{
	  db_private_free_and_init (thread_p, stsidp->out_values);
	}
      break;

    case S_SET_SCAN:
      break;

    case S_METHOD_SCAN:
      method_close_scan (thread_p, &scan_id->s.vaid.scan_buf);
      break;

    case S_JSON_TABLE_SCAN:
      break;

    default:
      /* S_VALUES_SCAN */
      break;
    }

  scan_id->status = S_CLOSED;
}

/*
 * call_get_next_index_oidset () - Wrapper for scan_get_next_oidset, accounts
 *                                 for scan variations, such as the "index
 *                                 skip scan" optimization.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   thread_p (in):
 *   scan_id (in/out):
 *   isidp (in/out):
 *   should_go_to_next_value (in): see Notes
 *
 * Note:
 * This function tries to obtain the next set of OIDs for the scan to consume.
 * The real heavy-lifting function is get_next_index_oidset(), which we call,
 * this one is a wrapper.
 * If the "Index Skip Scan" optimization is used, we cycle through successive
 * values of the first index column until we find one that lets
 * get_index_oidset() return with some results.
 * We also handle the case where we are called just because a new crop of OIDs
 * is needed.
 *
 * The boolean should_go_to_next_value controls whether we skip to the next
 * value for the first index column, or we still have data to read for the
 * current value (i.e. because the buffer was full. It is controlled by
 * the caller by evaluating BTREE_END_OF_SCAN. If this is true, there are no
 * more OIDS for the current value of the first index column and we should
 * "skip" to the next one.
 */
static SCAN_CODE
call_get_next_index_oidset (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, INDX_SCAN_ID * isidp,
			    bool should_go_to_next_value)
{
  DB_BIGINT *p_kl_upper = NULL;
  DB_BIGINT *p_kl_lower = NULL;
  int oids_cnt;

/*
 * WHILE (true)
 * {
 *  if (iss && should-skip-to-next-iss-value)
 *    obtain-next-iss-value() or return if it does not find anything;
 *
 *  get-index-oidset();
 *
 *  if (oids count == 0) // did not find anything
 *  {
 *    should-skip-to-next-iss-value = true;
 *    if (iss)
 *      continue; // to allow the while () to fetch the next value for the
 *		// first column in the index
 *  return S_END; // BTRS returned nothing and we are not in ISS mode. Leave.
 *  }
 *
 *  break; //at least one OID found. get out of the loop.
 *}
 */

  while (1)
    {
      if (isidp->iss.use && should_go_to_next_value)
	{
	  SCAN_CODE code = scan_get_next_iss_value (thread_p, scan_id, isidp);
	  if (code != S_SUCCESS)
	    {
	      /* anything wrong? or even end of scan? just leave */
	      return code;
	    }
	}

      p_kl_lower = isidp->key_limit_lower == -1 ? NULL : &isidp->key_limit_lower;
      p_kl_upper = isidp->key_limit_upper == -1 ? NULL : &isidp->key_limit_upper;

      if (scan_get_index_oidset (thread_p, scan_id, p_kl_upper, p_kl_lower) != NO_ERROR)
	{
	  return S_ERROR;
	}

      oids_cnt = isidp->multi_range_opt.use ? isidp->multi_range_opt.cnt : isidp->oids_count;

      if (oids_cnt == 0)
	{
	  if (isidp->iss.use)
	    {
	      should_go_to_next_value = true;
	      continue;
	    }
	  return S_END;		/* no ISS, no oids, this is the end of scan. */
	}

      /* We have at least one OID. Break the loop, allow normal processing. */
      break;
    }

  return S_SUCCESS;
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
  SCAN_CODE status;
  bool on_trace;
  UINT64 old_fetches = 0, old_ioreads = 0;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  on_trace = thread_is_on_trace (thread_p);
  if (on_trace)
    {
      tsc_getticks (&start_tick);

      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
    }

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
    case S_HEAP_SCAN_RECORD_INFO:
      status = scan_next_heap_scan (thread_p, scan_id);
      break;

    case S_HEAP_PAGE_SCAN:
      status = scan_next_heap_page_scan (thread_p, scan_id);
      break;

    case S_CLASS_ATTR_SCAN:
      status = scan_next_class_attr_scan (thread_p, scan_id);
      break;

    case S_INDX_SCAN:
      status = scan_next_index_scan (thread_p, scan_id);
      break;

    case S_INDX_KEY_INFO_SCAN:
      status = scan_next_index_key_info_scan (thread_p, scan_id);
      break;

    case S_INDX_NODE_INFO_SCAN:
      status = scan_next_index_node_info_scan (thread_p, scan_id);
      break;

    case S_LIST_SCAN:
      if (scan_id->s.llsid.hlsid.hash_list_scan_yn != HASH_METH_NOT_USE)
	{
	  status = scan_next_hash_list_scan (thread_p, scan_id);
	}
      else
	{
	  status = scan_next_list_scan (thread_p, scan_id);
	}
      break;

    case S_SHOWSTMT_SCAN:
      status = scan_next_showstmt_scan (thread_p, scan_id);
      break;

    case S_VALUES_SCAN:
      status = scan_next_value_scan (thread_p, scan_id);
      break;

    case S_SET_SCAN:
      status = scan_next_set_scan (thread_p, scan_id);
      break;

    case S_JSON_TABLE_SCAN:
      status = scan_next_json_table_scan (thread_p, scan_id);
      break;

    case S_METHOD_SCAN:
      status = scan_next_method_scan (thread_p, scan_id);
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return S_ERROR;
    }

  if (on_trace)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (scan_id->scan_stats.elapsed_scan, tv_diff);

      scan_id->scan_stats.num_fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      scan_id->scan_stats.num_ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
    }

  return status;
}

typedef enum
{
  OBJ_GET_WITHOUT_LOCK = 0,
  OBJ_REPEAT_GET_WITH_LOCK = 1,
  OBJ_GET_WITH_LOCK_COMPLETE = 2
} OBJECT_GET_STATUS;
/*
 * scan_next_heap_scan () - The scan is moved to the next heap scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_heap_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  HEAP_SCAN_ID *hsidp;
  FILTER_INFO data_filter;
  RECDES recdes = RECDES_INITIALIZER;
  SCAN_CODE sp_scan;
  DB_LOGICAL ev_res;
  OID current_oid, *p_current_oid = NULL;
  MVCC_SCAN_REEV_DATA mvcc_sel_reev_data;
  MVCC_REEV_DATA mvcc_reev_data;
  UPDDEL_MVCC_COND_REEVAL upd_reev;
  OID retry_oid;
  LOG_LSA ref_lsa;
  bool is_peeking;
  OBJECT_GET_STATUS object_get_status;
  regu_variable_list_node *p;

  hsidp = &scan_id->s.hsid;
  if (scan_id->mvcc_select_lock_needed)
    {
      p_current_oid = &current_oid;
    }
  else
    {
      p_current_oid = &hsidp->curr_oid;
    }

  /* set data filter information */
  scan_init_filter_info (&data_filter, &hsidp->scan_pred, &hsidp->pred_attrs, scan_id->val_list, scan_id->vd,
			 &hsidp->cls_oid, 0, NULL, NULL, NULL);

  is_peeking = scan_id->fixed;
  if (scan_id->grouped)
    {
      is_peeking = PEEK;
    }

  if (data_filter.val_list)
    {
      for (p = data_filter.scan_pred->regu_list; p; p = p->next)
	{
	  if (DB_NEED_CLEAR (p->value.vfetch_to))
	    {
	      pr_clear_value (p->value.vfetch_to);
	    }
	}
    }

  while (1)
    {
      COPY_OID (&retry_oid, &hsidp->curr_oid);
      object_get_status = OBJ_GET_WITHOUT_LOCK;

    restart_scan_oid:

      /* get next object */
      if (scan_id->grouped)
	{
	  /* grouped, fixed scan */
	  sp_scan = heap_scanrange_next (thread_p, &hsidp->curr_oid, &recdes, &hsidp->scan_range, is_peeking);
	}
      else
	{
	  recdes.data = NULL;
	  if (scan_id->direction == S_FORWARD)
	    {
	      /* move forward */
	      if (scan_id->type == S_HEAP_SCAN)
		{
		  sp_scan =
		    heap_next (thread_p, &hsidp->hfid, &hsidp->cls_oid, &hsidp->curr_oid, &recdes, &hsidp->scan_cache,
			       is_peeking);
		}
	      else
		{
		  assert (scan_id->type == S_HEAP_SCAN_RECORD_INFO);
		  sp_scan =
		    heap_next_record_info (thread_p, &hsidp->hfid, &hsidp->cls_oid, &hsidp->curr_oid, &recdes,
					   &hsidp->scan_cache, is_peeking, hsidp->cache_recordinfo);
		}
	    }
	  else
	    {
	      /* move backward */
	      if (scan_id->type == S_HEAP_SCAN)
		{
		  sp_scan =
		    heap_prev (thread_p, &hsidp->hfid, &hsidp->cls_oid, &hsidp->curr_oid, &recdes, &hsidp->scan_cache,
			       is_peeking);
		}
	      else
		{
		  assert (scan_id->type == S_HEAP_SCAN_RECORD_INFO);
		  sp_scan =
		    heap_prev_record_info (thread_p, &hsidp->hfid, &hsidp->cls_oid, &hsidp->curr_oid, &recdes,
					   &hsidp->scan_cache, is_peeking, hsidp->cache_recordinfo);
		}
	    }
	}

      if (sp_scan != S_SUCCESS)
	{
	  /* scan error or end of scan */
	  return (sp_scan == S_END) ? S_END : S_ERROR;
	}

      if (hsidp->scan_cache.page_watcher.pgptr != NULL)
	{
	  LSA_COPY (&ref_lsa, pgbuf_get_lsa (hsidp->scan_cache.page_watcher.pgptr));
	}

      /* evaluate the predicates to see if the object qualifies */
      scan_id->scan_stats.read_rows++;

      ev_res = eval_data_filter (thread_p, p_current_oid, &recdes, &hsidp->scan_cache, &data_filter);
      if (ev_res == V_ERROR)
	{
	  return S_ERROR;
	}

      if (is_peeking == PEEK && hsidp->scan_cache.page_watcher.pgptr != NULL
	  && pgbuf_page_has_changed (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa))
	{
	  is_peeking = COPY;
	  COPY_OID (&hsidp->curr_oid, &retry_oid);
	  goto restart_scan_oid;
	}

      if (scan_id->qualification == QPROC_QUALIFIED)
	{
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}
      else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	{
	  if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
	    {
	      continue;		/* qualified, continue to the next tuple */
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
	  else			/* V_UNKNOWN */
	    {
	      /* nop */
	      ;
	    }
	}
      else
	{			/* invalid value; the same as QPROC_QUALIFIED */
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}

      /* Data filter passed. If object should be locked and is not locked yet, lock it. */

      if (scan_id->mvcc_select_lock_needed)
	{
	  /* data filter already initialized, don't have key or range init scan reevaluation structure */
	  upd_reev.init (*scan_id);
	  mvcc_sel_reev_data.set_filters (upd_reev);
	  mvcc_sel_reev_data.qualification = &scan_id->qualification;
	  mvcc_reev_data.set_scan_reevaluation (mvcc_sel_reev_data);
	  COPY_OID (&current_oid, &hsidp->curr_oid);
	  if (scan_id->fixed)
	    {
	      /* Reset recdes.data */
	      recdes.data = NULL;
	    }

	  /* get with lock and reevaluate if the visible version wasn't the latest version */
	  sp_scan =
	    locator_lock_and_get_object_with_evaluation (thread_p, &current_oid, NULL, &recdes, &hsidp->scan_cache,
							 is_peeking, NULL_CHN, &mvcc_reev_data, LOG_WARNING_IF_DELETED);
	  if (sp_scan == S_SUCCESS && mvcc_reev_data.filter_result == V_FALSE)
	    {
	      continue;
	    }
	  else if (er_errid () == ER_HEAP_UNKNOWN_OBJECT || sp_scan == S_DOESNT_EXIST)
	    {
	      er_clear ();
	      continue;
	    }
	  else if (sp_scan != S_SUCCESS)
	    {
	      return S_ERROR;
	    }
	}

      if (mvcc_is_mvcc_disabled_class (&hsidp->cls_oid))
	{
	  LOCK lock = NULL_LOCK;
	  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	  TRAN_ISOLATION tran_isolation = logtb_find_isolation (tran_index);

	  if (scan_id->scan_op_type == S_DELETE || scan_id->scan_op_type == S_UPDATE)
	    {
	      lock = X_LOCK;
	    }
	  else if (oid_is_serial (&hsidp->cls_oid))
	    {
	      /* S_SELECT is currently handled only for serial, but may be extended to the other non-MVCC classes
	       * if needed */
	      lock = S_LOCK;
	    }

	  if (lock != NULL_LOCK && hsidp->scan_cache.page_watcher.pgptr != NULL)
	    {
	      if (tran_isolation == TRAN_READ_COMMITTED && lock == S_LOCK)
		{
		  if (lock_hold_object_instant (thread_p, &hsidp->curr_oid, &hsidp->cls_oid, lock) == LK_GRANTED)
		    {
		      lock = NULL_LOCK;
		      /* object_need_rescan needs to be kept false (page is still fixed, no other transaction could
		       * have change it) */
		    }
		}
	      else
		{
		  if (lock_object (thread_p, &hsidp->curr_oid, &hsidp->cls_oid, lock, LK_COND_LOCK) == LK_GRANTED)
		    {
		      /* successfully locked */
		      lock = NULL_LOCK;
		      /* object_need_rescan needs to be kept false (page is still fixed, no other transaction could
		       * have change it) */
		    }
		}
	    }

	  if (lock != NULL_LOCK)
	    {
	      VPID curr_vpid;

	      VPID_SET_NULL (&curr_vpid);

	      if (hsidp->scan_cache.page_watcher.pgptr != NULL)
		{
		  pgbuf_get_vpid (hsidp->scan_cache.page_watcher.pgptr, &curr_vpid);
		  pgbuf_ordered_unfix (thread_p, &hsidp->scan_cache.page_watcher);
		}
#if defined (SERVER_MODE)
	      else
		{
		  if (object_get_status == OBJ_GET_WITHOUT_LOCK)
		    {
		      /* page not fixed, recdes was read without lock, object may have changed */
		      object_get_status = OBJ_REPEAT_GET_WITH_LOCK;
		    }
		  else if (object_get_status == OBJ_REPEAT_GET_WITH_LOCK)
		    {
		      /* already read with lock, set flag to continue scanning next object */
		      object_get_status = OBJ_GET_WITH_LOCK_COMPLETE;
		    }
		}
#endif

	      if (lock_object (thread_p, &hsidp->curr_oid, &hsidp->cls_oid, lock, LK_UNCOND_LOCK) != LK_GRANTED)
		{
		  return S_ERROR;
		}

	      if (!heap_does_exist (thread_p, NULL, &hsidp->curr_oid))
		{
		  /* not qualified, continue to the next tuple */
		  lock_unlock_object_donot_move_to_non2pl (thread_p, &hsidp->curr_oid, &hsidp->cls_oid, lock);
		  continue;
		}

	      if (tran_isolation == TRAN_READ_COMMITTED && lock == S_LOCK)
		{
		  /* release acquired lock in RC */
		  lock_unlock_object_donot_move_to_non2pl (thread_p, &hsidp->curr_oid, &hsidp->cls_oid, lock);
		}

	      assert (hsidp->scan_cache.page_watcher.pgptr == NULL);

	      if (!VPID_ISNULL (&curr_vpid)
		  && pgbuf_ordered_fix (thread_p, &curr_vpid, OLD_PAGE, PGBUF_LATCH_READ,
					&hsidp->scan_cache.page_watcher) != NO_ERROR)
		{
		  return S_ERROR;
		}

	      if (object_get_status == OBJ_REPEAT_GET_WITH_LOCK
		  || (hsidp->scan_cache.page_watcher.pgptr != NULL
		      && pgbuf_page_has_changed (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa)))
		{
		  is_peeking = COPY;
		  COPY_OID (&hsidp->curr_oid, &retry_oid);
		  goto restart_scan_oid;
		}
	    }
	}


      scan_id->scan_stats.qualified_rows++;

      if (hsidp->rest_regu_list)
	{
	  /* read the rest of the values from the heap into the attribute cache */
	  if (heap_attrinfo_read_dbvalues (thread_p, p_current_oid, &recdes, &hsidp->scan_cache,
					   hsidp->rest_attrs.attr_cache) != NO_ERROR)
	    {
	      return S_ERROR;
	    }

	  if (is_peeking == PEEK && hsidp->scan_cache.page_watcher.pgptr != NULL
	      && pgbuf_page_has_changed (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa))
	    {
	      is_peeking = COPY;
	      COPY_OID (&hsidp->curr_oid, &retry_oid);
	      goto restart_scan_oid;
	    }

	  /* fetch the rest of the values from the object instance */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, hsidp->rest_regu_list, scan_id->vd, &hsidp->cls_oid, p_current_oid, NULL,
				  PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}

	      if (is_peeking != 0 && hsidp->scan_cache.page_watcher.pgptr != NULL
		  && pgbuf_page_has_changed (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa))
		{
		  is_peeking = COPY;
		  COPY_OID (&hsidp->curr_oid, &retry_oid);
		  goto restart_scan_oid;
		}
	    }
	}

      if (hsidp->recordinfo_regu_list != NULL)
	{
	  /* fetch the record info values */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, hsidp->recordinfo_regu_list, scan_id->vd, &hsidp->cls_oid, p_current_oid,
				  NULL, PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}

	      if (is_peeking == PEEK && hsidp->scan_cache.page_watcher.pgptr != NULL
		  && pgbuf_page_has_changed (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa))
		{
		  is_peeking = COPY;
		  COPY_OID (&hsidp->curr_oid, &retry_oid);
		  goto restart_scan_oid;
		}
	    }
	}

      return S_SUCCESS;
    }
}

/*
 * scan_next_heap_page_scan () - The scan is moved to the next page.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * scan_id (in)	 : Scan data.
 */
static SCAN_CODE
scan_next_heap_page_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  HEAP_PAGE_SCAN_ID *hpsidp = NULL;
  FILTER_INFO data_filter;
  SCAN_CODE sp_scan;
  DB_LOGICAL ev_res;

  hpsidp = &scan_id->s.hpsid;

  scan_init_filter_info (&data_filter, &hpsidp->scan_pred, NULL, scan_id->val_list, scan_id->vd, &hpsidp->cls_oid, 0,
			 NULL, NULL, NULL);

  while (true)
    {
      if (scan_id->direction == S_FORWARD)
	{
	  /* move forward */
	  sp_scan = heap_page_next (thread_p, &hpsidp->cls_oid, &hpsidp->hfid, &hpsidp->curr_vpid,
				    hpsidp->cache_page_info);
	}
      else
	{
	  /* move backward */
	  sp_scan = heap_page_prev (thread_p, &hpsidp->cls_oid, &hpsidp->hfid, &hpsidp->curr_vpid,
				    hpsidp->cache_page_info);
	}

      if (sp_scan != S_SUCCESS)
	{
	  return (sp_scan == S_END) ? S_END : S_ERROR;
	}

      /* evaluate filter to see if the page qualifies */
      ev_res = eval_data_filter (thread_p, &hpsidp->cls_oid, NULL, NULL, &data_filter);

      if (ev_res == V_ERROR)
	{
	  return S_ERROR;
	}
      else if (ev_res != V_TRUE)
	{
	  /* V_FALSE || V_UNKNOWN */
	  continue;
	}

      if (hpsidp->page_info_regu_list != NULL)
	{
	  /* fetch the page info values */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, hpsidp->page_info_regu_list, scan_id->vd, &hpsidp->cls_oid, NULL, NULL,
				  PEEK) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }
	}

      return S_SUCCESS;
    }
}

/*
 * scan_next_class_attr_scan () - The scan is moved to the next class attribute scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_class_attr_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  HEAP_SCAN_ID *hsidp;
  FILTER_INFO data_filter;
  DB_LOGICAL ev_res;

  hsidp = &scan_id->s.hsid;

  /* set data filter information */
  scan_init_filter_info (&data_filter, &hsidp->scan_pred, &hsidp->pred_attrs, scan_id->val_list, scan_id->vd,
			 &hsidp->cls_oid, 0, NULL, NULL, NULL);

  if (scan_id->position == S_BEFORE)
    {
      /* Class attribute scans are always single row scan. */
      scan_id->position = S_AFTER;

      /* evaluate the predicates to see if the object qualifies */
      ev_res = eval_data_filter (thread_p, NULL, NULL, NULL, &data_filter);
      if (ev_res == V_ERROR)
	{
	  return S_ERROR;
	}

      if (scan_id->qualification == QPROC_QUALIFIED)
	{
	  if (ev_res != V_TRUE)
	    {			/* V_FALSE || V_UNKNOWN */
	      return S_END;	/* not qualified */
	    }
	}
      else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	{
	  if (ev_res != V_FALSE)
	    {			/* V_TRUE || V_UNKNOWN */
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
	  else			/* V_UNKNOWN */
	    {
	      /* nop */
	      ;
	    }
	}
      else
	{			/* invalid value; the same as QPROC_QUALIFIED */
	  if (ev_res != V_TRUE)
	    {			/* V_FALSE || V_UNKNOWN */
	      return S_END;	/* not qualified */
	    }
	}

      if (hsidp->rest_regu_list)
	{
	  /* read the rest of the values from the heap into the attribute cache */
	  if (heap_attrinfo_read_dbvalues (thread_p, NULL, NULL, NULL, hsidp->rest_attrs.attr_cache) != NO_ERROR)
	    {
	      return S_ERROR;
	    }

	  /* fetch the rest of the values from the object instance */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, hsidp->rest_regu_list, scan_id->vd, &hsidp->cls_oid, NULL, NULL, PEEK) !=
		  NO_ERROR)
		{
		  return S_ERROR;
		}
	    }
	}

      return S_SUCCESS;
    }
  else
    {
      /* Class attribute scans are always single row scan. */
      return S_END;
    }
}

/*
 * scan_next_index_scan () - The scan is moved to the next index scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_index_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  INDX_SCAN_ID *isidp;
  FILTER_INFO data_filter;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  SCAN_CODE lookup_status;
  TRAN_ISOLATION isolation;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  isidp = &scan_id->s.isid;

  assert (!OID_ISNULL (&isidp->cls_oid));

  /* multi range optimization safe guard : fall-back to normal output (OID list or covering index instead of "on the
   * fly" lists), if sorting column is not yet set at this stage; also 'grouped' is not supported */
  if (isidp->multi_range_opt.use && (isidp->multi_range_opt.sort_att_idx == NULL || scan_id->grouped))
    {
      isidp->multi_range_opt.use = false;
      scan_id->scan_stats.multi_range_opt = false;
    }

  /* set data filter information */
  scan_init_filter_info (&data_filter, &isidp->scan_pred, &isidp->pred_attrs, scan_id->val_list, scan_id->vd,
			 &isidp->cls_oid, 0, NULL, NULL, NULL);

  /* Due to the length of time that we hold onto the oid list, it is possible at lower isolation levels (UNCOMMITTED
   * INSTANCES) that the index/heap may have changed since the oid list was read from the btree.  In particular, some
   * of the instances that we are reading may have been deleted by the time we go to fetch them via heap_get_visible_version ().
   * According to the semantics of UNCOMMITTED, it is ok if they are deleted out from under us and we can ignore the
   * SCAN_DOESNT_EXIST error. */

  isolation = logtb_find_current_isolation (thread_p);

  while (1)
    {
      /* get next object from OID list */
      if (scan_id->grouped)
	{
	  assert (isidp->oid_list != NULL);
	  /* grouped scan */
	  if (scan_id->direction == S_FORWARD)
	    {
	      /* move forward (to the next object) */
	      if (isidp->curr_oidno == -1)
		{
		  isidp->curr_oidno = 0;	/* first oid number */
		  isidp->curr_oidp = isidp->oid_list->oidp;
		}
	      else if (isidp->curr_oidno < isidp->oids_count - 1)
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
	    {
	      /* move backward (to the previous object */
	      if (isidp->curr_oidno == isidp->oids_count)
		{
		  isidp->curr_oidno = isidp->oids_count - 1;
		  isidp->curr_oidp = GET_NTH_OID (isidp->oid_list->oidp, isidp->curr_oidno);
		}
	      else if (isidp->curr_oidno > 0)
		{
		  isidp->curr_oidno--;
		  isidp->curr_oidp = GET_NTH_OID (isidp->oid_list->oidp, isidp->curr_oidno);
		}
	      else
		{
		  return S_END;
		}
	    }
	}
      else
	{
	  /* non-grouped, regular index scan */
	  if (scan_id->position == S_BEFORE)
	    {
	      SCAN_CODE ret;

	      /* Either we are not using ISS, or we are using it, and in this case, we are supposed to be here for the
	       * first time */
	      assert_release (!isidp->iss.use || isidp->iss.current_op == ISS_OP_NONE);

	      ret = call_get_next_index_oidset (thread_p, scan_id, isidp, true);
	      if (ret != S_SUCCESS)
		{
		  return ret;
		}

	      if (isidp->need_count_only == true)
		{
		  /* no more scan is needed. just return */
		  return S_SUCCESS;
		}

	      scan_id->position = S_ON;
	      isidp->curr_oidno = 0;	/* first oid number */

	      if (SCAN_IS_INDEX_COVERED (isidp))
		{
		  qfile_close_list (thread_p, isidp->indx_cov.list_id);
		  if (qfile_open_list_scan (isidp->indx_cov.list_id, isidp->indx_cov.lsid) != NO_ERROR)
		    {
		      return S_ERROR;
		    }
		}
	      else
		{
		  if (isidp->multi_range_opt.use)
		    {
		      assert (isidp->curr_oidno < isidp->multi_range_opt.cnt);
		      assert (isidp->multi_range_opt.top_n_items[isidp->curr_oidno] != NULL);

		      isidp->curr_oidp = &(isidp->multi_range_opt.top_n_items[isidp->curr_oidno]->inst_oid);
		    }
		  else
		    {
		      assert (isidp->oid_list != NULL);
		      isidp->curr_oidp = GET_NTH_OID (isidp->oid_list->oidp, isidp->curr_oidno);
		    }
		  assert (HEAP_ISVALID_OID (thread_p, isidp->curr_oidp) != DISK_INVALID);
		}
	    }
	  else if (scan_id->position == S_ON)
	    {
	      int oids_cnt;
	      /* we are in the S_ON case */

	      oids_cnt = isidp->multi_range_opt.use ? isidp->multi_range_opt.cnt : isidp->oids_count;

	      /* if there are OIDs left */
	      if (isidp->curr_oidno < oids_cnt - 1)
		{
		  isidp->curr_oidno++;
		  if (!SCAN_IS_INDEX_COVERED (isidp))
		    {
		      if (isidp->multi_range_opt.use)
			{
			  assert (isidp->curr_oidno < isidp->multi_range_opt.cnt);
			  assert (isidp->multi_range_opt.top_n_items[isidp->curr_oidno] != NULL);

			  isidp->curr_oidp = &(isidp->multi_range_opt.top_n_items[isidp->curr_oidno]->inst_oid);
			}
		      else
			{
			  assert (isidp->oid_list != NULL);
			  isidp->curr_oidp = GET_NTH_OID (isidp->oid_list->oidp, isidp->curr_oidno);
			}
		      assert (HEAP_ISVALID_OID (thread_p, isidp->curr_oidp) != DISK_INVALID);
		    }
		  else
		    {
		      /* TODO: Index covering case keeps OID's in isidp->indx_cov.list_id and not in isidp->oid_list.
		       * Fall through. */
		    }
		}
	      else
		{
		  /* there are no more OIDs left. Decide what to do */

		  /* We can ignore the END OF SCAN signal if we're certain there can be more results, for instance if
		   * we have a multiple range scan, or if we have the "index skip scan" optimization on */
		  if (BTREE_END_OF_SCAN (&isidp->bt_scan) && isidp->indx_info->range_type != R_RANGELIST
		      && isidp->indx_info->range_type != R_KEYLIST && !isidp->iss.use)
		    {
		      return S_END;
		    }
		  else
		    {
		      SCAN_CODE ret;
		      bool go_to_next_iss_value;

		      /* a list in a range is exhausted */
		      if (isidp->multi_range_opt.use)
			{
			  /* for "on the fly" case (multi range opt), all ranges are exhausted from first shoot, force
			   * exit */
			  isidp->oids_count = 0;
			  return S_END;
			}

		      if (SCAN_IS_INDEX_COVERED (isidp))
			{
			  /* close current list and start a new one */
			  qfile_close_scan (thread_p, isidp->indx_cov.lsid);
			  qfile_destroy_list (thread_p, isidp->indx_cov.list_id);
			  QFILE_FREE_AND_INIT_LIST_ID (isidp->indx_cov.list_id);
			  isidp->indx_cov.list_id =
			    qfile_open_list (thread_p, isidp->indx_cov.type_list, NULL, isidp->indx_cov.query_id, 0);
			  if (isidp->indx_cov.list_id == NULL)
			    {
			      return S_ERROR;
			    }
			}
		      /* if this the current scan is not done (i.e. the buffer was full and we need to fetch more rows,
		       * do not go to the next value */
		      go_to_next_iss_value = BTREE_END_OF_SCAN (&isidp->bt_scan)
			&& (isidp->indx_info->range_type == R_KEY || isidp->indx_info->range_type == R_RANGE);
		      ret = call_get_next_index_oidset (thread_p, scan_id, isidp, go_to_next_iss_value);
		      if (ret != S_SUCCESS)
			{
			  return ret;
			}

		      if (isidp->need_count_only == true)
			{
			  /* no more scan is needed. just return */
			  return S_SUCCESS;
			}

		      isidp->curr_oidno = 0;	/* first oid number */
		      if (SCAN_IS_INDEX_COVERED (isidp))
			{
			  qfile_close_list (thread_p, isidp->indx_cov.list_id);
			  if (qfile_open_list_scan (isidp->indx_cov.list_id, isidp->indx_cov.lsid) != NO_ERROR)
			    {
			      return S_ERROR;
			    }
			}
		      else
			{
			  assert (isidp->oid_list != NULL);
			  isidp->curr_oidp = isidp->oid_list->oidp;
			  assert (HEAP_ISVALID_OID (thread_p, isidp->curr_oidp) != DISK_INVALID);
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
	      return S_ERROR;
	    }
	}

      assert (scan_id->position == S_ON);

      scan_id->scan_stats.key_qualified_rows++;

      /* get pages for read */
      if (!SCAN_IS_INDEX_COVERED (isidp))
	{
	  perfmon_inc_stat (thread_p, PSTAT_BT_NUM_NONCOVERED);

	  assert (isidp->curr_oidno >= 0);
	  assert (isidp->curr_oidp != NULL);
	  assert (HEAP_ISVALID_OID (thread_p, isidp->curr_oidp) != DISK_INVALID);

	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&start_tick);
	    }

	  lookup_status = scan_next_index_lookup_heap (thread_p, scan_id, isidp, &data_filter, isolation);

	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (scan_id->scan_stats.elapsed_lookup, tv_diff);
	    }

	  if (lookup_status == S_SUCCESS)
	    {
	      scan_id->scan_stats.data_qualified_rows++;
	    }
	  else if (lookup_status == S_DOESNT_EXIST)
	    {
	      if (scan_id->mvcc_select_lock_needed && isidp->key_limit_upper != -1)
		{
		  isidp->key_limit_upper++;
		}
	      /* not qualified, continue to the next tuple */
	      continue;
	    }
	  else
	    {
	      /* S_ERROR, S_END */
	      return lookup_status;
	    }
	}
      else
	{
	  /* TO DO - in MVCC when mvcc_select_lock_needed is true index coverage must be disabled */
	  if (isidp->multi_range_opt.use)
	    {
	      assert (isidp->curr_oidno < isidp->multi_range_opt.cnt);
	      assert (isidp->multi_range_opt.top_n_items[isidp->curr_oidno] != NULL);

	      if (scan_dump_key_into_tuple (thread_p, isidp,
					    &(isidp->multi_range_opt.top_n_items[isidp->curr_oidno]->index_value),
					    isidp->curr_oidp, &isidp->multi_range_opt.tplrec) != NO_ERROR)
		{
		  return S_ERROR;
		}
	      tplrec.tpl = isidp->multi_range_opt.tplrec.tpl;
	      tplrec.size = isidp->multi_range_opt.tplrec.size;
	    }
	  else
	    {
	      if (qfile_scan_list_next (thread_p, isidp->indx_cov.lsid, &tplrec, PEEK) != S_SUCCESS)
		{
		  return S_ERROR;
		}
	    }

	  perfmon_inc_stat (thread_p, PSTAT_BT_NUM_COVERED);

	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, isidp->indx_cov.regu_val_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) !=
		  NO_ERROR)
		{
		  return S_ERROR;
		}
	    }
	}

      return S_SUCCESS;
    }
}

/*
 * scan_next_index_lookup_heap () - fetch heap record and evaluate data filter
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR, S_DOESNT_EXIST)
 *   scan_id(in/out): Scan identifier
 *   isidp(in/out): Index scan identifier
 *   data_filter(in): data filter information
 *   isolation(in): transaction isolation level
 *
 * Note: If the tuple is not qualified for data filter, S_DOESNT_EXIST is returned.
 */
static SCAN_CODE
scan_next_index_lookup_heap (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, INDX_SCAN_ID * isidp,
			     FILTER_INFO * data_filter, TRAN_ISOLATION isolation)
{
  SCAN_CODE sp_scan;
  DB_LOGICAL ev_res;
  RECDES recdes = RECDES_INITIALIZER;
  indx_info *indx_infop;
  BTID *btid;
  char *indx_name_p;
  char *class_name_p;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };

  assert (scan_id != NULL);
  assert (isidp != NULL);
  assert (isidp->curr_oidp != NULL && !OID_ISNULL (isidp->curr_oidp));

  if (scan_id->fixed == false)
    {
      recdes.data = NULL;
    }

  sp_scan = heap_get_visible_version (thread_p, isidp->curr_oidp, NULL, &recdes, &isidp->scan_cache, scan_id->fixed,
				      NULL_CHN);
  if (sp_scan == S_SNAPSHOT_NOT_SATISFIED)
    {
      if (SCAN_IS_INDEX_COVERED (isidp))
	{
	  /* goto the next tuple */
	  if (!isidp->multi_range_opt.use)
	    {
	      if (qfile_scan_list_next (thread_p, isidp->indx_cov.lsid, &tplrec, PEEK) != S_SUCCESS)
		{
		  return S_ERROR;
		}
	    }
	}

      return S_DOESNT_EXIST;	/* not qualified, continue to the next tuple */
    }
  else if (sp_scan == S_ERROR)
    {
      ASSERT_ERROR ();
      return sp_scan;
    }
  else
    {
      assert (sp_scan == S_SUCCESS || sp_scan == S_SUCCESS_CHN_UPTODATE);
    }

  /* evaluate the predicates to see if the object qualifies */
  ev_res = eval_data_filter (thread_p, isidp->curr_oidp, &recdes, &isidp->scan_cache, data_filter);

  // no key filter evaluation is required here.

  ev_res = update_logical_result (thread_p, ev_res, (int *) &scan_id->qualification);
  if (ev_res == V_ERROR)
    {
      return S_ERROR;
    }
  else if (ev_res != V_TRUE)
    {
      return S_DOESNT_EXIST;
    }

  if (scan_id->mvcc_select_lock_needed)
    {
      UPDDEL_MVCC_COND_REEVAL upd_reev;
      MVCC_SCAN_REEV_DATA mvcc_sel_reev_data;
      MVCC_REEV_DATA mvcc_reev_data;

      upd_reev.init (*scan_id);
      mvcc_sel_reev_data.set_filters (upd_reev);
      mvcc_sel_reev_data.qualification = &scan_id->qualification;
      mvcc_reev_data.set_scan_reevaluation (mvcc_sel_reev_data);

      sp_scan = locator_lock_and_get_object_with_evaluation (thread_p, isidp->curr_oidp, NULL, &recdes,
							     &isidp->scan_cache, scan_id->fixed, NULL_CHN,
							     &mvcc_reev_data, LOG_WARNING_IF_DELETED);
      if (sp_scan == S_SUCCESS)
	{
	  switch (mvcc_reev_data.filter_result)
	    {
	    case V_ERROR:
	      return S_ERROR;
	    case V_FALSE:
	      return S_DOESNT_EXIST;
	    default:
	      break;
	    }
	}
    }

  if (sp_scan == S_DOESNT_EXIST || er_errid () == ER_HEAP_UNKNOWN_OBJECT)
    {
      er_clear ();
      if (SCAN_IS_INDEX_COVERED (isidp))
	{
	  /* goto the next tuple */
	  if (!isidp->multi_range_opt.use)
	    {
	      if (qfile_scan_list_next (thread_p, isidp->indx_cov.lsid, &tplrec, PEEK) != S_SUCCESS)
		{
		  return S_ERROR;
		}
	    }
	}

      return S_DOESNT_EXIST;	/* not qualified, continue to the next tuple */
    }

  if (sp_scan != S_SUCCESS && sp_scan != S_SNAPSHOT_NOT_SATISFIED)
    {
      /* check end of scan */
      if (sp_scan == S_END)
	{
	  assert (false);	/* is impossible case */
	  return S_END;
	}

      indx_infop = isidp->indx_info;
      btid = &indx_infop->btid;
      indx_name_p = NULL;
      class_name_p = NULL;

      /* check scan error */
      if (er_errid () == NO_ERROR)
	{
	  (void) heap_get_indexinfo_of_btid (thread_p, &isidp->cls_oid, btid, NULL, NULL, NULL, NULL, &indx_name_p,
					     NULL);

	  if (heap_get_class_name (thread_p, &isidp->cls_oid, &class_name_p) != NO_ERROR)
	    {
	      /* ignore */
	      er_clear ();
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_INCONSISTENT_BTREE_ENTRY_TYPE2, 11,
		  (indx_name_p) ? indx_name_p : "*UNKNOWN-INDEX*", (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*",
		  isidp->cls_oid.volid, isidp->cls_oid.pageid, isidp->cls_oid.slotid, isidp->curr_oidp->volid,
		  isidp->curr_oidp->pageid, isidp->curr_oidp->slotid, btid->vfid.volid, btid->vfid.fileid,
		  btid->root_pageid);

	  if (class_name_p)
	    {
	      free_and_init (class_name_p);
	    }

	  if (indx_name_p)
	    {
	      free_and_init (indx_name_p);
	    }
	}

      return S_ERROR;
    }

  if (!scan_id->mvcc_select_lock_needed && mvcc_is_mvcc_disabled_class (&isidp->cls_oid))
    {
      /* Data filter passed. If object should be locked and is not locked yet, lock it. */
      LOCK lock = NULL_LOCK;
      int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      TRAN_ISOLATION tran_isolation = logtb_find_isolation (tran_index);

      if (scan_id->scan_op_type == S_DELETE || scan_id->scan_op_type == S_UPDATE)
	{
	  lock = X_LOCK;
	}
      else if (oid_is_serial (&isidp->cls_oid))
	{
	  /* S_SELECT is currently handled only for serial, but may be extended to the other non-MVCC classes if
	   * needed */
	  lock = S_LOCK;
	}

      if (lock != NULL_LOCK)
	{
	  if (tran_isolation == TRAN_READ_COMMITTED && lock == S_LOCK)
	    {
	      if (lock_hold_object_instant (thread_p, isidp->curr_oidp, &isidp->cls_oid, lock) == LK_GRANTED)
		{
		  lock = NULL_LOCK;
		}
	    }
	  else
	    {
	      if (lock_object (thread_p, isidp->curr_oidp, &isidp->cls_oid, lock, LK_COND_LOCK) == LK_GRANTED)
		{
		  /* successfully locked */
		  lock = NULL_LOCK;
		}
	    }
	}

      if (lock != NULL_LOCK)
	{
	  if (isidp->scan_cache.page_watcher.pgptr != NULL)
	    {
	      pgbuf_ordered_unfix (thread_p, &isidp->scan_cache.page_watcher);
	    }

	  if (lock_object (thread_p, isidp->curr_oidp, &isidp->cls_oid, lock, LK_UNCOND_LOCK) != LK_GRANTED)
	    {
	      return S_ERROR;
	    }

	  if (!heap_does_exist (thread_p, NULL, isidp->curr_oidp))
	    {
	      /* not qualified, continue to the next tuple */
	      lock_unlock_object_donot_move_to_non2pl (thread_p, isidp->curr_oidp, &isidp->cls_oid, lock);
	      return S_DOESNT_EXIST;
	    }

	  if (tran_isolation == TRAN_READ_COMMITTED && lock == S_LOCK)
	    {
	      /* release acquired lock in RC */
	      lock_unlock_object_donot_move_to_non2pl (thread_p, isidp->curr_oidp, &isidp->cls_oid, lock);
	    }
	}
    }

  if (isidp->rest_regu_list)
    {
      /* read the rest of the values from the heap into the attribute cache */
      if (heap_attrinfo_read_dbvalues (thread_p, isidp->curr_oidp, &recdes, &isidp->scan_cache,
				       isidp->rest_attrs.attr_cache) != NO_ERROR)
	{
	  return S_ERROR;
	}

      /* fetch the rest of the values from the object instance */
      if (scan_id->val_list)
	{
	  if (fetch_val_list (thread_p, isidp->rest_regu_list, scan_id->vd, &isidp->cls_oid, isidp->curr_oidp, NULL,
			      PEEK) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}
    }

  return S_SUCCESS;
}

/*
 * scan_next_index_key_info_scan () - Scans each key in index and obtains
 *				      information about that key.
 *
 * return	 : Scan code.
 * thread_p (in) : Thread entry.
 * scan_id (in)  : Scan data.
 */
static SCAN_CODE
scan_next_index_key_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  INDX_SCAN_ID *isidp = NULL;
  FILTER_INFO data_filter;
  SCAN_CODE sp_scan;
  DB_LOGICAL ev_res;

  isidp = &scan_id->s.isid;

  scan_init_filter_info (&data_filter, &isidp->scan_pred, NULL, scan_id->val_list, scan_id->vd, &isidp->cls_oid, 0,
			 NULL, NULL, NULL);

  while (true)
    {
      sp_scan =
	btree_get_next_key_info (thread_p, &isidp->indx_info->btid, &isidp->bt_scan, 1, &isidp->cls_oid,
				 isidp, isidp->key_info_values);
      if (sp_scan != S_SUCCESS)
	{
	  return (sp_scan == S_END) ? S_END : S_ERROR;
	}

      ev_res = eval_data_filter (thread_p, NULL, NULL, NULL, &data_filter);
      if (ev_res == V_ERROR)
	{
	  return S_ERROR;
	}
      else if (ev_res != V_TRUE)
	{
	  /* V_FALSE || V_UNKNOWN */
	  continue;
	}

      if (isidp->key_info_regu_list != NULL && scan_id->val_list != NULL)
	{
	  if (fetch_val_list (thread_p, isidp->key_info_regu_list, scan_id->vd, &isidp->cls_oid, NULL, NULL, PEEK) !=
	      NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      return S_SUCCESS;
    }
}

/*
 * scan_next_index_node_info_scan () - Scans for nodes in b-tree and obtains
 *				       information about the nodes.
 *
 * return	 : Scan code.
 * thread_p (in) : Thread entry.
 * scan_id (in)	 : Scan data.
 */
static SCAN_CODE
scan_next_index_node_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  INDEX_NODE_SCAN_ID *insidp = NULL;
  FILTER_INFO data_filter;
  SCAN_CODE sp_scan;
  DB_LOGICAL ev_res;

  insidp = &scan_id->s.insid;

  scan_init_filter_info (&data_filter, &insidp->scan_pred, NULL, scan_id->val_list, scan_id->vd, NULL, 0, NULL, NULL,
			 NULL);

  while (true)
    {
      sp_scan = btree_get_next_node_info (thread_p, &insidp->indx_info->btid, &insidp->btns, insidp->node_info_values);
      if (sp_scan != S_SUCCESS)
	{
	  return (sp_scan == S_END) ? S_END : S_ERROR;
	}

      ev_res = eval_data_filter (thread_p, NULL, NULL, NULL, &data_filter);
      if (ev_res == V_ERROR)
	{
	  return S_ERROR;
	}
      else if (ev_res != V_TRUE)
	{
	  /* V_FALSE || V_UNKNOWN */
	  continue;
	}

      if (insidp->node_info_regu_list != NULL && scan_id->val_list != NULL)
	{
	  if (fetch_val_list (thread_p, insidp->node_info_regu_list, scan_id->vd, NULL, NULL, NULL, PEEK) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      return S_SUCCESS;
    }
}

/*
 * scan_next_list_scan () - The scan is moved to the next list scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  LLIST_SCAN_ID *llsidp;
  SCAN_CODE qp_scan;
  DB_LOGICAL ev_res;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };

  llsidp = &scan_id->s.llsid;

  tplrec.size = 0;
  tplrec.tpl = (QFILE_TUPLE) NULL;

  resolve_domains_on_list_scan (llsidp, scan_id->val_list);

  while ((qp_scan = qfile_scan_list_next (thread_p, &llsidp->lsid, &tplrec, PEEK)) == S_SUCCESS)
    {

      /* fetch the values for the predicate from the tuple */
      if (scan_id->val_list)
	{
	  if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) !=
	      NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      scan_id->scan_stats.read_rows++;

      /* evaluate the predicate to see if the tuple qualifies */
      ev_res = V_TRUE;
      if (llsidp->scan_pred.pr_eval_fnc && llsidp->scan_pred.pred_expr)
	{
	  ev_res = (*llsidp->scan_pred.pr_eval_fnc) (thread_p, llsidp->scan_pred.pred_expr, scan_id->vd, NULL);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      if (scan_id->qualification == QPROC_QUALIFIED)
	{
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}
      else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	{
	  if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
	    {
	      continue;		/* qualified, continue to the next tuple */
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
	  else			/* V_UNKNOWN */
	    {
	      /* nop */
	      ;
	    }
	}
      else
	{			/* invalid value; the same as QPROC_QUALIFIED */
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}

      scan_id->scan_stats.qualified_rows++;

      /* fetch the rest of the values from the tuple */
      if (scan_id->val_list)
	{
	  if (fetch_val_list (thread_p, llsidp->rest_regu_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) != NO_ERROR)
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
}

/*
 * scan_next_value_scan () - The scan is moved to the next value scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_value_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  REGU_VALUES_SCAN_ID *rvsidp;
  regu_variable_list_node *list_node;
  REGU_VALUE_LIST *regu_value_list;
  int i;

  rvsidp = &scan_id->s.rvsid;
  if (scan_id->position == S_BEFORE)
    {
      scan_id->position = S_ON;
    }
  else if (scan_id->position == S_ON)
    {
      for (i = 0, list_node = rvsidp->regu_list; list_node; ++i, list_node = list_node->next)
	{
	  regu_value_list = list_node->value.value.reguval_list;
	  if (regu_value_list == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_CRSPOS, 0);
	      return S_ERROR;
	    }

	  assert (regu_value_list->current_value != NULL);

	  regu_value_list->current_value = regu_value_list->current_value->next;

	  if (regu_value_list->current_value == NULL)
	    {
	      scan_id->position = S_AFTER;

	      if (i == 0)
		{
		  return S_END;
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_CRSPOS, 0);
		  return S_ERROR;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return S_ERROR;
    }

  return S_SUCCESS;
}

/*
 * scan_next_showstmt_scan () - The scan is moved to the next value scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_showstmt_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  SHOWSTMT_SCAN_ID *stsidp;
  SCAN_CODE qp_scan;
  DB_LOGICAL ev_res;

  stsidp = &scan_id->s.stsid;

  if (scan_id->position == S_BEFORE)
    {
      scan_id->position = S_ON;
    }

  if (scan_id->position == S_ON)
    {
      while ((qp_scan = showstmt_next_scan (thread_p, scan_id)) == S_SUCCESS)
	{
	  /* evaluate the predicate to see if the tuple qualifies */
	  ev_res = V_TRUE;
	  if (stsidp->scan_pred.pr_eval_fnc && stsidp->scan_pred.pred_expr)
	    {
	      ev_res = (*stsidp->scan_pred.pr_eval_fnc) (thread_p, stsidp->scan_pred.pred_expr, scan_id->vd, NULL);
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
	}

    }
  else if (scan_id->position == S_AFTER)
    {
      return S_END;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return S_ERROR;
    }

  return qp_scan;
}

/*
 * scan_next_set_scan () - The scan is moved to the next set scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_set_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  SET_SCAN_ID *ssidp;
  SCAN_CODE qp_scan;
  DB_LOGICAL ev_res;
  REGU_VARIABLE *func;
  regu_variable_list_node *ptr;
  int size;

  ssidp = &scan_id->s.ssid;

  /* if we are in the before position, fetch the set */
  if (scan_id->position == S_BEFORE)
    {
      func = ssidp->set_ptr;
      if (func->type == TYPE_FUNC && func->value.funcp->ftype == F_SEQUENCE)
	{
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
	  if (fetch_copy_dbval (thread_p, ssidp->set_ptr, scan_id->vd, NULL, NULL, NULL, &ssidp->set) != NO_ERROR)
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
	  ev_res = (*ssidp->scan_pred.pr_eval_fnc) (thread_p, ssidp->scan_pred.pred_expr, scan_id->vd, NULL);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      if (scan_id->qualification == QPROC_QUALIFIED)
	{
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}
      else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	{
	  if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
	    {
	      continue;		/* qualified, continue to the next tuple */
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
	  else			/* V_UNKNOWN */
	    {
	      /* nop */
	      ;
	    }
	}
      else
	{			/* invalid value; the same as QPROC_QUALIFIED */
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}

      return S_SUCCESS;
    }				/* while ((qp_scan = ) == S_SUCCESS) */

  return qp_scan;
}

/*
 * scan_next_json_table_scan () - The scan is moved to the next json_table scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_json_table_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  int error_code = NO_ERROR;
  SCAN_CODE sc;

  // the status of the scan will be put in scan_id->status
  error_code = scan_id->s.jtid.next_scan (thread_p, *scan_id, sc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return S_ERROR;
    }

  return sc;
}

/*
 * scan_next_method_scan () - The scan is moved to the next method scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_method_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  VA_SCAN_ID *vaidp;
  SCAN_CODE qp_scan;
  val_list_node vl;
  QPROC_DB_VALUE_LIST src_valp;
  QPROC_DB_VALUE_LIST dest_valp;

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
  for (src_valp = vl.valp, dest_valp = scan_id->val_list->valp; src_valp && dest_valp;
       src_valp = src_valp->next, dest_valp = dest_valp->next)
    {
      if (DB_IS_NULL (src_valp->val))
	{
	  pr_clear_value (dest_valp->val);
	}
      else if (DB_VALUE_DOMAIN_TYPE (src_valp->val) != DB_VALUE_DOMAIN_TYPE (dest_valp->val))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
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
scan_handle_single_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id, QP_SCAN_FUNC next_scan)
{
  SCAN_CODE result = S_ERROR;

  if (s_id->scan_immediately_stop == true)
    {
      result = S_END;
      goto end;
    }

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
      /* if scan works in a single_fetch mode and first qualified scan item has already been fetched, return
       * end_of_scan. */
      if (s_id->single_fetched)
	{
	  result = S_END;
	}
      else
	/* if it is known that scan has no qualified items, return the NULL row, without searching. */
      if (s_id->join_dbval && DB_IS_NULL (s_id->join_dbval))
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
      /* if scan works in a single_fetch mode and first qualified scan item has already been fetched, return
       * end_of_scan. */
      if (s_id->single_fetched)
	{
	  result = S_END;
	}
      /* if it is known that scan has no qualified items, return the NULL row, without searching. */
      else if (s_id->join_dbval && DB_IS_NULL (s_id->join_dbval))
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
      /* already returned a NULL row? if scan works in a left outer join mode and a NULL row has already fetched,
       * return end_of_scan. */
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

end:
  /* maintain what is apparently supposed to be an invariant-- S_END implies position is "after" the scan */
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

      while ((qp_scan = qfile_scan_list_prev (thread_p, &llsidp->lsid, &tplrec, PEEK)) == S_SUCCESS)
	{
	  /* fetch the values for the predicate from the tuple */
	  if (scan_id->val_list)
	    {
	      if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) !=
		  NO_ERROR)
		{
		  return S_ERROR;
		}
	    }

	  /* evaluate the predicate to see if the tuple qualifies */
	  ev_res = V_TRUE;
	  if (llsidp->scan_pred.pr_eval_fnc && llsidp->scan_pred.pred_expr)
	    {
	      ev_res = (*llsidp->scan_pred.pr_eval_fnc) (thread_p, llsidp->scan_pred.pred_expr, scan_id->vd, NULL);
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
	      if (fetch_val_list (thread_p, llsidp->rest_regu_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) !=
		  NO_ERROR)
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
  qfile_save_current_scan_tuple_position (&s_id->s.llsid.lsid, &scan_pos->ls_tplpos);
}

/*
 * scan_jump_scan_pos () - Jump to the given scan position and move the scan
 *                         from that point on in the forward direction.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *   scan_pos(in/out): Set to contain current scan position
 *
 * Note: This routine currently assumes only LIST FILE scans.
 */
SCAN_CODE
scan_jump_scan_pos (THREAD_ENTRY * thread_p, SCAN_ID * s_id, SCAN_POS * scan_pos)
{
  LLIST_SCAN_ID *llsidp;
  DB_LOGICAL ev_res;
  QFILE_TUPLE_RECORD tplrec;
  SCAN_CODE qp_scan;

  llsidp = &s_id->s.llsid;

  /* put back saved scan position */
  s_id->status = scan_pos->status;
  s_id->position = scan_pos->position;

  /* jump to the previouslt saved scan position and continue from that point on forward */
  tplrec.size = 0;
  tplrec.tpl = (QFILE_TUPLE) NULL;

  qp_scan = qfile_jump_scan_tuple_position (thread_p, &llsidp->lsid, &scan_pos->ls_tplpos, &tplrec, PEEK);
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
	  if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list, s_id->vd, NULL, NULL, tplrec.tpl, PEEK) !=
	      NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      /* evaluate the predicate to see if the tuple qualifies */
      ev_res = V_TRUE;
      if (llsidp->scan_pred.pr_eval_fnc && llsidp->scan_pred.pred_expr)
	{
	  ev_res = (*llsidp->scan_pred.pr_eval_fnc) (thread_p, llsidp->scan_pred.pred_expr, s_id->vd, NULL);
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
	      if (fetch_val_list (thread_p, llsidp->rest_regu_list, s_id->vd, NULL, NULL, tplrec.tpl, PEEK) != NO_ERROR)
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
  while ((qp_scan = qfile_scan_list_next (thread_p, &llsidp->lsid, &tplrec, PEEK)) == S_SUCCESS);

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
int
scan_initialize (void)
{
  BTREE_ISCAN_OID_LIST *new_oid_list = NULL;
  int error_code = NO_ERROR;
  int i = 0;

  /* Initialize */
  scan_Iscan_oid_buf_list = NULL;
  scan_Iscan_oid_buf_list_count = 0;

  /* Allocate oid buffer list. */
  for (i = 0; i < SCAN_ISCAN_OID_BUF_LIST_DEFAULT_SIZE; i++)
    {
      /* Allocate new entry. */
      new_oid_list = NULL;
      error_code = scan_alloc_oid_list (&new_oid_list);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  /* Free what was already allocated. */
	  scan_finalize ();

	  /* Return error. */
	  return error_code;
	}
      /* Safe guard. */
      assert (new_oid_list->oidp != NULL);
      assert (new_oid_list->capacity > 0);

      /* Save new buffer to buffer list. */
      new_oid_list->next_list = scan_Iscan_oid_buf_list;
      scan_Iscan_oid_buf_list = new_oid_list;
      scan_Iscan_oid_buf_list_count++;
    }
  /* Success. */
  return NO_ERROR;
}

/*
 * scan_finalize () - finalize scan management routine
 *   return:
 */
void
scan_finalize (void)
{
  BTREE_ISCAN_OID_LIST *oid_list_p;

  while (scan_Iscan_oid_buf_list != NULL)
    {
      /* Save current. */
      oid_list_p = scan_Iscan_oid_buf_list;

      /* Advance to next. */
      scan_Iscan_oid_buf_list = oid_list_p->next_list;

      /* Free current. */
      scan_free_oid_list (oid_list_p);
    }

  /* Reset count. */
  scan_Iscan_oid_buf_list_count = 0;
}

/*
 * reverse_key_list () - reverses the key list
 *   return: number of keys, -1 for error
 *   key_vals (in): pointer to array of KEY_VAL_RANGE structure
 *   key_cnt (in): number of keys; size of key_vals
 */
static int
reverse_key_list (KEY_VAL_RANGE * key_vals, int key_cnt)
{
  int i, j;
  KEY_VAL_RANGE temp;

  for (i = 0, j = key_cnt - 1; i < j; i++, j--)
    {
      temp = key_vals[i];
      key_vals[i] = key_vals[j];
      key_vals[j] = temp;
    }

  return key_cnt;
}

/*
 * resolve_domains_on_list_scan () - scans the structures in a list scan id
 *   and resolves the domains in sub-components like regu variables from scan
 *   predicates;
 *
 *   llsidp (in/out): pointer to list scan id structure
 *   ref_val_list (in): list of DB_VALUEs (val_list_node) used as reference
 *
 *  Note : this function is used in context of HV late binding
 */
static void
resolve_domains_on_list_scan (LLIST_SCAN_ID * llsidp, val_list_node * ref_val_list)
{
  regu_variable_list_node *scan_regu = NULL;

  assert (llsidp != NULL);

  if (llsidp->list_id == NULL || ref_val_list == NULL)
    {
      return;
    }

  /* resolve domains on regu_list of scan predicate */
  for (scan_regu = llsidp->scan_pred.regu_list; scan_regu != NULL; scan_regu = scan_regu->next)
    {
      if ((TP_DOMAIN_TYPE (scan_regu->value.domain) == DB_TYPE_VARIABLE
	   || TP_DOMAIN_COLLATION_FLAG (scan_regu->value.domain)) && scan_regu->value.type == TYPE_POSITION)
	{
	  int pos = scan_regu->value.value.pos_descr.pos_no;
	  TP_DOMAIN *new_dom = NULL;

	  assert (pos < llsidp->list_id->type_list.type_cnt);
	  new_dom = llsidp->list_id->type_list.domp[pos];

	  if (TP_DOMAIN_TYPE (new_dom) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_COLLATION_FLAG (new_dom) != TP_DOMAIN_COLL_NORMAL)
	    {
	      continue;
	    }

	  scan_regu->value.value.pos_descr.dom = new_dom;
	  scan_regu->value.domain = new_dom;
	}
    }

  /* resolve domains on rest_regu_list of scan predicate */
  for (scan_regu = llsidp->rest_regu_list; scan_regu != NULL; scan_regu = scan_regu->next)
    {
      if ((TP_DOMAIN_TYPE (scan_regu->value.domain) == DB_TYPE_VARIABLE
	   || TP_DOMAIN_COLLATION_FLAG (scan_regu->value.domain) != TP_DOMAIN_COLL_NORMAL)
	  && scan_regu->value.type == TYPE_POSITION)
	{
	  int pos = scan_regu->value.value.pos_descr.pos_no;
	  TP_DOMAIN *new_dom = NULL;

	  assert (pos < llsidp->list_id->type_list.type_cnt);
	  new_dom = llsidp->list_id->type_list.domp[pos];

	  if (TP_DOMAIN_TYPE (new_dom) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_COLLATION_FLAG (new_dom) != TP_DOMAIN_COLL_NORMAL)
	    {
	      continue;
	    }
	  scan_regu->value.value.pos_descr.dom = new_dom;
	  scan_regu->value.domain = new_dom;
	}
    }

  /* resolve domains on predicate expression of scan predicate */
  if (llsidp->scan_pred.pred_expr == NULL)
    {
      return;
    }

  if (llsidp->scan_pred.pred_expr->type == T_EVAL_TERM)
    {
      EVAL_TERM ev_t = llsidp->scan_pred.pred_expr->pe.m_eval_term;

      if (ev_t.et_type == T_COMP_EVAL_TERM)
	{
	  if (ev_t.et.et_comp.lhs != NULL
	      && (TP_DOMAIN_TYPE (ev_t.et.et_comp.lhs->domain) == DB_TYPE_VARIABLE
		  || TP_DOMAIN_COLLATION_FLAG (ev_t.et.et_comp.lhs->domain) != TP_DOMAIN_COLL_NORMAL))
	    {
	      resolve_domain_on_regu_operand (ev_t.et.et_comp.lhs, ref_val_list, &(llsidp->list_id->type_list));
	    }
	  if (ev_t.et.et_comp.rhs != NULL
	      && (TP_DOMAIN_TYPE (ev_t.et.et_comp.rhs->domain) == DB_TYPE_VARIABLE
		  || TP_DOMAIN_COLLATION_FLAG (ev_t.et.et_comp.rhs->domain) != TP_DOMAIN_COLL_NORMAL))
	    {
	      resolve_domain_on_regu_operand (ev_t.et.et_comp.rhs, ref_val_list, &(llsidp->list_id->type_list));
	    }
	}
    }
}

/*
 * resolve_domain_on_regu_operand () - resolves a domain on a regu variable
 *    from a scan list; helper functions for 'resolve_domains_on_list_scan'
 *
 *   regu_var (in/out): regulator variable with unresolved domain
 *   ref_val_list (in): list of DB_VALUEs (val_list_node) used for cross-checking
 *   p_type_list (in): list of domains used as reference
 *
 *  Note : this function is used in context of HV late binding
 */
static void
resolve_domain_on_regu_operand (REGU_VARIABLE * regu_var, val_list_node * ref_val_list,
				QFILE_TUPLE_VALUE_TYPE_LIST * p_type_list)
{
  assert (regu_var != NULL);
  assert (ref_val_list != NULL);

  if (regu_var->type == TYPE_CONSTANT)
    {
      QPROC_DB_VALUE_LIST value_list;
      int pos = 0;
      bool found = false;

      /* search in ref_val_list for the corresponding DB_VALUE */
      for (value_list = ref_val_list->valp; value_list != NULL; value_list = value_list->next, pos++)
	{
	  if (regu_var->value.dbvalptr == ref_val_list->valp->val)
	    {
	      found = true;
	      break;
	    }
	}

      if (found)
	{
	  assert (pos < p_type_list->type_cnt);
	  regu_var->domain = p_type_list->domp[pos];
	}
    }
}

/*
 * scan_init_multi_range_optimization () - initialize structure for multiple range optimization
 *
 *   return: error code
 *
 * multi_range_opt(in): multiple range optimization structure
 * use_range_opt(in): to use or not optimization
 * max_size(in): size of arrays for the top N values
 */
static int
scan_init_multi_range_optimization (THREAD_ENTRY * thread_p, MULTI_RANGE_OPT * multi_range_opt, bool use_range_opt,
				    int max_size)
{
  int err = NO_ERROR;

  if (multi_range_opt == NULL)
    {
      return ER_FAILED;
    }

  memset ((void *) (multi_range_opt), 0, sizeof (MULTI_RANGE_OPT));
  multi_range_opt->use = use_range_opt;
  multi_range_opt->cnt = 0;

  if (use_range_opt)
    {
      multi_range_opt->size = max_size;
      /* we don't have sort information here, just set an invalid value */
      multi_range_opt->sort_att_idx = NULL;
      multi_range_opt->is_desc_order = NULL;
      multi_range_opt->num_attrs = 0;

      multi_range_opt->top_n_items =
	(RANGE_OPT_ITEM **) db_private_alloc (thread_p, max_size * sizeof (RANGE_OPT_ITEM *));
      if (multi_range_opt->top_n_items == NULL)
	{
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}
      multi_range_opt->buffer = (RANGE_OPT_ITEM **) db_private_alloc (thread_p, max_size * sizeof (RANGE_OPT_ITEM *));
      if (multi_range_opt->buffer == NULL)
	{
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit_on_error;
	}
      memset (multi_range_opt->top_n_items, 0, max_size * sizeof (RANGE_OPT_ITEM *));

      multi_range_opt->tplrec.size = 0;
      multi_range_opt->tplrec.tpl = NULL;

      perfmon_inc_stat (thread_p, PSTAT_BT_NUM_MULTI_RANGE_OPT);
    }

  return err;

exit_on_error:

  if (multi_range_opt->top_n_items != NULL)
    {
      db_private_free_and_init (thread_p, multi_range_opt->top_n_items);
    }
  if (multi_range_opt->buffer != NULL)
    {
      db_private_free_and_init (thread_p, multi_range_opt->buffer);
    }

  return err;
}

/*
 * scan_dump_key_into_tuple () - outputs the value stored in 'key' into the tuple 'tplrec'
 *
 *   return: error code
 *   iscan_id(in):
 *   key(in): MIDXKEY key (as it is retreived from index)
 *   oid(in): oid (required if objects are stored in 'key')
 *   tplrec(out):
 *
 *  Note : this function is used by multiple range search optimization;
 *	   although not required here, the key should be a MIDXKEY value,
 *	   when multiple range search optimization is enabled.
 */
static int
scan_dump_key_into_tuple (THREAD_ENTRY * thread_p, INDX_SCAN_ID * iscan_id, DB_VALUE * key, OID * oid,
			  QFILE_TUPLE_RECORD * tplrec)
{
  int error;
  regu_variable_list_node *p;

  if (iscan_id == NULL || iscan_id->indx_cov.val_descr == NULL || iscan_id->indx_cov.output_val_list == NULL
      || iscan_id->rest_attrs.attr_cache == NULL)
    {
      return ER_FAILED;
    }

  error = btree_attrinfo_read_dbvalues (thread_p, key, iscan_id->bt_attr_ids, iscan_id->bt_num_attrs,
					iscan_id->rest_attrs.attr_cache, -1);
  if (error != NO_ERROR)
    {
      return error;
    }

  for (p = iscan_id->rest_regu_list; p; p = p->next)
    {
      pr_clear_value (p->value.vfetch_to);
    }

  error = fetch_val_list (thread_p, iscan_id->rest_regu_list, iscan_id->indx_cov.val_descr, NULL, oid, NULL, PEEK);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = qdata_copy_valptr_list_to_tuple (thread_p, iscan_id->indx_cov.output_val_list, iscan_id->indx_cov.val_descr,
					   tplrec);
  if (error != NO_ERROR)
    {
      return error;
    }

  return NO_ERROR;
}


#if defined (SERVER_MODE)
/*
 * scan_print_stats_json () -
 * return:
 * scan_id(in):
 */
void
scan_print_stats_json (SCAN_ID * scan_id, json_t * scan_stats)
{
  json_t *scan, *lookup;

  if (scan_id == NULL || scan_stats == NULL)
    {
      return;
    }

  scan = json_pack ("{s:i, s:I, s:I}", "time", TO_MSEC (scan_id->scan_stats.elapsed_scan), "fetch",
		    scan_id->scan_stats.num_fetches, "ioread", scan_id->scan_stats.num_ioreads);

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
    case S_LIST_SCAN:
      json_object_set_new (scan, "readrows", json_integer (scan_id->scan_stats.read_rows));
      json_object_set_new (scan, "rows", json_integer (scan_id->scan_stats.qualified_rows));

      if (scan_id->type == S_HEAP_SCAN)
	{
	  json_object_set_new (scan_stats, "heap", scan);
	}
      else
	{
	  json_object_set_new (scan_stats, "temp", scan);
	}
      break;

    case S_INDX_SCAN:
      json_object_set_new (scan, "readkeys", json_integer (scan_id->scan_stats.read_keys));
      json_object_set_new (scan, "filteredkeys", json_integer (scan_id->scan_stats.qualified_keys));
      json_object_set_new (scan, "rows", json_integer (scan_id->scan_stats.key_qualified_rows));
      json_object_set_new (scan_stats, "btree", scan);

      if (scan_id->scan_stats.covered_index == true)
	{
	  json_object_set_new (scan_stats, "covered", json_true ());
	}
      else
	{
	  lookup = json_pack ("{s:i, s:i}", "time", TO_MSEC (scan_id->scan_stats.elapsed_lookup), "rows",
			      scan_id->scan_stats.data_qualified_rows);

	  json_object_set_new (scan_stats, "lookup", lookup);
	}

      if (scan_id->scan_stats.multi_range_opt == true)
	{
	  json_object_set_new (scan_stats, "mro", json_true ());
	}

      if (scan_id->scan_stats.index_skip_scan == true)
	{
	  json_object_set_new (scan_stats, "iss", json_true ());
	}

      if (scan_id->scan_stats.loose_index_scan == true)
	{
	  json_object_set_new (scan_stats, "loose", json_true ());
	}
      break;

    case S_SHOWSTMT_SCAN:
      json_object_set_new (scan_stats, "show", scan);
      break;

    case S_SET_SCAN:
      json_object_set_new (scan_stats, "set", scan);
      break;

    case S_METHOD_SCAN:
      json_object_set_new (scan_stats, "method", scan);
      break;

    case S_CLASS_ATTR_SCAN:
      json_object_set_new (scan_stats, "class_attr", scan);
      break;

    default:
      json_object_set_new (scan_stats, "noscan", scan);
      break;
    }
}

/*
 * scan_print_stats_text () -
 * return:
 * scan_id(in):
 */
void
scan_print_stats_text (FILE * fp, SCAN_ID * scan_id)
{
  if (scan_id == NULL)
    {
      return;
    }

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
      fprintf (fp, "(heap");
      break;

    case S_INDX_SCAN:
      fprintf (fp, "(btree");
      break;

    case S_LIST_SCAN:
      if (scan_id->s.llsid.hlsid.hash_list_scan_yn == HASH_METH_IN_MEM)
	{
	  fprintf (fp, "(hash temp, build time: %d,", TO_MSEC (scan_id->scan_stats.elapsed_hash_build));
	}
      else if (scan_id->s.llsid.hlsid.hash_list_scan_yn == HASH_METH_HYBRID)
	{
	  fprintf (fp, "(hash temp(h), build time: %d,", TO_MSEC (scan_id->scan_stats.elapsed_hash_build));
	}
      else
	{
	  fprintf (fp, "(temp");
	}
      break;

    case S_SHOWSTMT_SCAN:
      fprintf (fp, "(show");
      break;

    case S_SET_SCAN:
      fprintf (fp, "(set");
      break;

    case S_METHOD_SCAN:
      fprintf (fp, "(method");
      break;

    case S_CLASS_ATTR_SCAN:
      fprintf (fp, "(class_attr");
      break;

    default:
      fprintf (fp, "(noscan");
      break;
    }

  fprintf (fp, " time: %d, fetch: %lld, ioread: %lld", TO_MSEC (scan_id->scan_stats.elapsed_scan),
	   (long long int) scan_id->scan_stats.num_fetches, (long long int) scan_id->scan_stats.num_ioreads);

  switch (scan_id->type)
    {
    case S_HEAP_SCAN:
    case S_LIST_SCAN:
      fprintf (fp, ", readrows: %d, rows: %d)", scan_id->scan_stats.read_rows, scan_id->scan_stats.qualified_rows);
      break;

    case S_INDX_SCAN:
      fprintf (fp, ", readkeys: %d, filteredkeys: %d, rows: %d", scan_id->scan_stats.read_keys,
	       scan_id->scan_stats.qualified_keys, scan_id->scan_stats.key_qualified_rows);

      if (scan_id->scan_stats.covered_index == true)
	{
	  fprintf (fp, ", covered: true");
	}

      if (scan_id->scan_stats.multi_range_opt == true)
	{
	  fprintf (fp, ", mro: true");
	}

      if (scan_id->scan_stats.index_skip_scan == true)
	{
	  fprintf (fp, ", iss: true");
	}

      if (scan_id->scan_stats.loose_index_scan == true)
	{
	  fprintf (fp, ", loose: true");
	}
      fprintf (fp, ")");

      if (scan_id->scan_stats.covered_index == false)
	{
	  fprintf (fp, " (lookup time: %d, rows: %d)", TO_MSEC (scan_id->scan_stats.elapsed_lookup),
		   scan_id->scan_stats.data_qualified_rows);
	}
      break;

    default:
      fprintf (fp, ")");
      break;
    }
}
#endif

/*
 * scan_build_hash_list_scan () - build hash table from list
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_build_hash_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  LLIST_SCAN_ID *llsidp;
  SCAN_CODE qp_scan;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  HASH_SCAN_KEY *key, *new_key;
  HASH_SCAN_VALUE *new_value;

  llsidp = &scan_id->s.llsid;
  key = llsidp->hlsid.temp_key;
  new_key = llsidp->hlsid.temp_new_key;

  tplrec.size = 0;
  tplrec.tpl = (QFILE_TUPLE) NULL;

  resolve_domains_on_list_scan (llsidp, scan_id->val_list);

  while ((qp_scan = qfile_scan_list_next (thread_p, &llsidp->lsid, &tplrec, PEEK)) == S_SUCCESS)
    {
      /* fetch the values for the predicate from the tuple */
      if (scan_id->val_list)
	{
	  if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) !=
	      NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      scan_id->scan_stats.read_rows++;

      /* build key */
      if (qdata_build_hscan_key (thread_p, scan_id->vd, llsidp->hlsid.build_regu_list, key) != NO_ERROR)
	{
	  return S_ERROR;
	}
      /* create new key */
      if (llsidp->hlsid.need_coerce_type)
	{
	  new_key = qdata_copy_hscan_key_without_alloc (thread_p, key, llsidp->hlsid.probe_regu_list, new_key);
	  if (new_key == NULL)
	    {
	      return S_ERROR;
	    }
	}
      else
	{
	  new_key = key;
	}

      /* create new value */
      if (llsidp->hlsid.hash_list_scan_yn == HASH_METH_IN_MEM)
	{
	  new_value = qdata_alloc_hscan_value (thread_p, tplrec.tpl);
	}
      else if (llsidp->hlsid.hash_list_scan_yn == HASH_METH_HYBRID)
	{
	  new_value = qdata_alloc_hscan_value_OID (thread_p, &llsidp->lsid);
	}
      else
	{
	  return S_ERROR;
	}

      if (new_value == NULL)
	{
	  return S_ERROR;
	}
      /* add to hash table */
      if (mht_put_hls (llsidp->hlsid.hash_table, (void *) new_key, (void *) new_value) == NULL)
	{
	  return S_ERROR;
	}
    }

  return qp_scan;
}

/*
 * scan_next_hash_list_scan () - The scan is moved to the next hash list scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_next_hash_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id)
{
  LLIST_SCAN_ID *llsidp;
  SCAN_CODE qp_scan;
  DB_LOGICAL ev_res;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };

  tplrec.size = 0;
  tplrec.tpl = (QFILE_TUPLE) NULL;

  llsidp = &scan_id->s.llsid;

  while ((qp_scan = scan_hash_probe_next (thread_p, scan_id, &tplrec.tpl)) == S_SUCCESS)
    {

      /* fetch the values for the predicate from the tuple */
      if (scan_id->val_list)
	{
	  if (fetch_val_list (thread_p, llsidp->scan_pred.regu_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) !=
	      NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      scan_id->scan_stats.read_rows++;

      /* evaluate the predicate to see if the tuple qualifies */
      ev_res = V_TRUE;
      if (llsidp->scan_pred.pr_eval_fnc && llsidp->scan_pred.pred_expr)
	{
	  ev_res = (*llsidp->scan_pred.pr_eval_fnc) (thread_p, llsidp->scan_pred.pred_expr, scan_id->vd, NULL);
	  if (ev_res == V_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      if (scan_id->qualification == QPROC_QUALIFIED)
	{
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}
      else if (scan_id->qualification == QPROC_NOT_QUALIFIED)
	{
	  if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
	    {
	      continue;		/* qualified, continue to the next tuple */
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
	  else			/* V_UNKNOWN */
	    {
	      /* nop */
	      ;
	    }
	}
      else
	{			/* invalid value; the same as QPROC_QUALIFIED */
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      continue;		/* not qualified, continue to the next tuple */
	    }
	}

      scan_id->scan_stats.qualified_rows++;

      /* fetch the rest of the values from the tuple */
      if (scan_id->val_list)
	{
	  if (fetch_val_list (thread_p, llsidp->rest_regu_list, scan_id->vd, NULL, NULL, tplrec.tpl, PEEK) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}
      return S_SUCCESS;
    }

  return qp_scan;
}

/*
 * scan_next_hash_list_scan () - The scan is moved to the next hash list scan item.
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   scan_id(in/out): Scan identifier
 *
 * Note: If there are no more scan items, S_END is returned. If an error occurs, S_ERROR is returned.
 */
static SCAN_CODE
scan_hash_probe_next (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, QFILE_TUPLE * tuple)
{
  LLIST_SCAN_ID *llsidp;
  SCAN_CODE qp_scan;
  HASH_SCAN_KEY *key;
  HASH_SCAN_VALUE *hvalue;
  QFILE_LIST_SCAN_ID *scan_id_p;
  QFILE_TUPLE_POSITION tuple_pos;
  QFILE_TUPLE_SIMPLE_POS *simple_pos;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };

  llsidp = &scan_id->s.llsid;
  key = llsidp->hlsid.temp_key;
  scan_id_p = &llsidp->lsid;

  if (scan_id_p->position == S_BEFORE)
    {
      if (llsidp->hlsid.hash_table->nentries > 0)
	{
	  /* init curr_hash_entry */
	  llsidp->hlsid.curr_hash_entry = NULL;
	  /* build key */
	  if (qdata_build_hscan_key (thread_p, scan_id->vd, llsidp->hlsid.probe_regu_list, key) != NO_ERROR)
	    {
	      return S_ERROR;
	    }

	  /* get value from hash table */
	  hvalue =
	    (HASH_SCAN_VALUE *) mht_get_hls (llsidp->hlsid.hash_table, key, (void **) &llsidp->hlsid.curr_hash_entry);
	  if (hvalue == NULL)
	    {
	      return S_END;
	    }
	  if (llsidp->hlsid.hash_list_scan_yn == HASH_METH_IN_MEM)
	    {
	      *tuple = hvalue->tuple;
	    }
	  else if (llsidp->hlsid.hash_list_scan_yn == HASH_METH_HYBRID)
	    {
	      MAKE_TUPLE_POSTION (tuple_pos, hvalue->pos, scan_id_p);
	      if (qfile_jump_scan_tuple_position (thread_p, scan_id_p, &tuple_pos, &tplrec, PEEK) != S_SUCCESS)
		{
		  return S_ERROR;
		}
	      *tuple = tplrec.tpl;
	    }
	  else
	    {
	      return S_ERROR;
	    }
	  scan_id_p->position = S_ON;
	  return S_SUCCESS;
	}
      else
	{
	  return S_END;
	}
    }
  else if (scan_id_p->position == S_ON)
    {
      if (llsidp->hlsid.curr_hash_entry->next)
	{
	  llsidp->hlsid.curr_hash_entry = llsidp->hlsid.curr_hash_entry->next;
	  if (llsidp->hlsid.hash_list_scan_yn == HASH_METH_IN_MEM)
	    {
	      *tuple = ((HASH_SCAN_VALUE *) llsidp->hlsid.curr_hash_entry->data)->tuple;
	    }
	  else if (llsidp->hlsid.hash_list_scan_yn == HASH_METH_HYBRID)
	    {
	      simple_pos = ((HASH_SCAN_VALUE *) llsidp->hlsid.curr_hash_entry->data)->pos;
	      MAKE_TUPLE_POSTION (tuple_pos, simple_pos, scan_id_p);

	      if (qfile_jump_scan_tuple_position (thread_p, scan_id_p, &tuple_pos, &tplrec, PEEK) != S_SUCCESS)
		{
		  return S_ERROR;
		}
	      *tuple = tplrec.tpl;
	    }
	  else
	    {
	      return S_ERROR;
	    }
	  return S_SUCCESS;
	}
      else
	{
	  if (llsidp->hlsid.hash_list_scan_yn == HASH_METH_HYBRID)
	    {
	      qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
	    }
	  scan_id_p->position = S_AFTER;
	  return S_END;
	}
    }
  else if (scan_id_p->position == S_AFTER)
    {
      return S_END;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return S_ERROR;
    }

  return qp_scan;
}

/*
 * check_hash_list_scan () - Check if hash list scan is possible
 *   return: int  1: in-memory 2: hybrid in-memory
 *   llsidp (in): list scan id pointer
 *   node :
 *      1. count of tuple of list file > 0
 *      2. list file size check
 *      3. regu_list_build, regu_list_probe is not null
 *      4. The number of probe regu_var and build regu match
 *      5. type of regu var is not oid && vobj
 *      6. list file from dptr is not allowed
*/
static HASH_METHOD
check_hash_list_scan (LLIST_SCAN_ID * llsidp, int *val_cnt, int hash_list_scan_yn)
{
  int build_cnt;
  regu_variable_list_node *build, *probe;
  DB_TYPE vtype1, vtype2;
  UINT64 mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);

  /* no_hash_list_scan sql hint check */
  if (hash_list_scan_yn == 0)
    {
      return HASH_METH_NOT_USE;
    }

  /* count of tuple of list file > 0 */
  if (llsidp->list_id->tuple_cnt <= 0)
    {
      return HASH_METH_NOT_USE;
    }
  /* regu_list_build, regu_list_probe is not null */
  if (llsidp->hlsid.build_regu_list == NULL || llsidp->hlsid.probe_regu_list == NULL)
    {
      return HASH_METH_NOT_USE;
    }

  build = llsidp->hlsid.build_regu_list;
  probe = llsidp->hlsid.probe_regu_list;

  for (build_cnt = 0; build && probe; build_cnt++)
    {
      /* type of regu var is not oid && vobj */
      /* This is the case when type coercion is impossible. so use list scan */
      /* In the list scan, Vobj is converted to oid for comparison at tp_value_compare_with_error(). */
      vtype1 = REGU_VARIABLE_GET_TYPE (&probe->value);
      vtype2 = REGU_VARIABLE_GET_TYPE (&build->value);

      if (((vtype1 == DB_TYPE_OBJECT || vtype1 == DB_TYPE_VOBJ) && vtype2 == DB_TYPE_OID) ||
	  ((vtype2 == DB_TYPE_OBJECT || vtype2 == DB_TYPE_VOBJ) && vtype1 == DB_TYPE_OID))
	{
	  return HASH_METH_NOT_USE;
	}
      if (vtype1 != vtype2)
	{
	  llsidp->hlsid.need_coerce_type = true;
	}
      build = build->next;
      probe = probe->next;
    }
  /* The number of probe regu_var and build regu match */
  if (build != NULL || probe != NULL)
    {
      return HASH_METH_NOT_USE;
    }
  *val_cnt = build_cnt;

  /* 6. list file from dptr is not allowed */
  /* Since dptr is searched after scan_open_scan, it is checked when llsidp->list_id->tuple_cnt <= 0 */

  /* list file size check */
  if ((UINT64) llsidp->list_id->page_cnt * DB_PAGESIZE <= mem_limit)
    {
      return HASH_METH_IN_MEM;
    }
  else if ((UINT64) llsidp->list_id->tuple_cnt * (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)) <= mem_limit)
    {
      /* bytes of 1 row = sizeof(HENTRY_HLS) + sizeof(QFILE_TUPLE_SIMPLE_POS) = 36 bytes (64bit) */
      /* HENTRY_HLS = pointer(8bytes) * 3 = 24 bytes */
      /* SIMPLE_POS = pageid(4bytes) + voldid(2bytes) + padding(2bytes) + offset(4bytes) = 12 bytes */
      return HASH_METH_HYBRID;
    }
  else
    {
      return HASH_METH_NOT_USE;
    }

  return HASH_METH_NOT_USE;
}
