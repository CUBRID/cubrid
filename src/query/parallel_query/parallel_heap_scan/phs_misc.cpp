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
 * phs_misc.cpp - miscellaneous functions for parallel heap scan
 */

#if defined (SERVER_MODE)
#include "phs_misc.hpp"
#include "memory_alloc.h"
#include "fetch.h"
#include "query_reevaluation.hpp"
#include "dbtype.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  int regu_var_list_len (struct regu_variable_list_node   *list)
  {
    int len = 0;
    for (struct regu_variable_list_node   *iter = list; iter; iter = iter->next)
      {
	len++;
      }
    return len;
  }

  int regu_var_clear (THREAD_ENTRY *thread_p, REGU_VARIABLE *regu_var)
  {
    int pg_cnt;

    pg_cnt = 0;
    if (!regu_var)
      {
	return pg_cnt;
      }
    regu_var->domain = regu_var->original_domain;

    switch (regu_var->type)
      {
      case TYPE_ATTR_ID:		/* fetch object attribute value */
      case TYPE_SHARED_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
	regu_var->value.attr_descr.cache_dbvalp = NULL;
	break;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
	pg_cnt += arith_list_clear (thread_p, regu_var->value.arithptr);
	break;
      case TYPE_SP:
	delete regu_var->value.sp_ptr->sig;
	regu_var->value.sp_ptr->sig = nullptr;
	break;
      case TYPE_FUNC:
	if (regu_var->value.funcp->tmp_obj != NULL)
	  {
	    switch (regu_var->value.funcp->ftype)
	      {
	      case F_REGEXP_COUNT:
	      case F_REGEXP_INSTR:
	      case F_REGEXP_LIKE:
	      case F_REGEXP_REPLACE:
	      case F_REGEXP_SUBSTR:
	      {
		if (regu_var->value.funcp->tmp_obj->compiled_regex)
		  {
		    delete regu_var->value.funcp->tmp_obj->compiled_regex;
		    regu_var->value.funcp->tmp_obj->compiled_regex = NULL;
		  }
	      }
	      break;
	      default:
		// any member of union func_tmp_obj may have been erased
		assert (false);
		break;
	      }

	    delete regu_var->value.funcp->tmp_obj;
	    regu_var->value.funcp->tmp_obj = NULL;
	  }
	break;
      default:
	break;
      }

    return pg_cnt;
  }

  int pred_clear (THREAD_ENTRY *thread_p, PRED_EXPR *pr)
  {

    int pg_cnt;
    PRED_EXPR *expr;

    pg_cnt = 0;

    if (pr == NULL)
      {
	return pg_cnt;
      }

    switch (pr->type)
      {
      case T_PRED:
	pg_cnt += pred_clear (thread_p, pr->pe.m_pred.lhs);
	for (expr = pr->pe.m_pred.rhs; expr && expr->type == T_PRED; expr = expr->pe.m_pred.rhs)
	  {
	    pg_cnt += pred_clear (thread_p, expr->pe.m_pred.lhs);
	  }
	pg_cnt += pred_clear (thread_p, expr);
	break;
      case T_EVAL_TERM:
	switch (pr->pe.m_eval_term.et_type)
	  {
	  case T_COMP_EVAL_TERM:
	  {
	    COMP_EVAL_TERM *et_comp = &pr->pe.m_eval_term.et.et_comp;

	    pg_cnt += regu_var_clear (thread_p, et_comp->lhs);
	    pg_cnt += regu_var_clear (thread_p, et_comp->rhs);
	  }
	  break;
	  case T_ALSM_EVAL_TERM:
	  {
	    ALSM_EVAL_TERM *et_alsm = &pr->pe.m_eval_term.et.et_alsm;

	    pg_cnt += regu_var_clear (thread_p, et_alsm->elem);
	    pg_cnt += regu_var_clear (thread_p, et_alsm->elemset);
	  }
	  break;
	  case T_LIKE_EVAL_TERM:
	  {
	    LIKE_EVAL_TERM *et_like = &pr->pe.m_eval_term.et.et_like;

	    pg_cnt += regu_var_clear (thread_p, et_like->src);
	    pg_cnt += regu_var_clear (thread_p, et_like->pattern);
	    pg_cnt += regu_var_clear (thread_p, et_like->esc_char);
	  }
	  break;
	  case T_RLIKE_EVAL_TERM:
	  {
	    RLIKE_EVAL_TERM *et_rlike = &pr->pe.m_eval_term.et.et_rlike;

	    pg_cnt += regu_var_clear (thread_p, et_rlike->src);
	    pg_cnt += regu_var_clear (thread_p, et_rlike->pattern);
	    pg_cnt += regu_var_clear (thread_p, et_rlike->case_sensitive);

	    /* free memory of compiled regex */
	    if (et_rlike->compiled_regex)
	      {
		delete et_rlike->compiled_regex;
		et_rlike->compiled_regex = NULL;
	      }
	  }
	  break;
	  }
	break;
      case T_NOT_TERM:
	pg_cnt += pred_clear (thread_p, pr->pe.m_not_term);
	break;
      }

    return pg_cnt;
  }
  int arith_list_clear (THREAD_ENTRY *thread_p, ARITH_TYPE *list)
  {

    int pg_cnt = 0;

    if (list == NULL)
      {
	return NO_ERROR;
      }

    list->domain = list->original_domain;

    if (list->rand_seed != NULL)
      {
	free_and_init (list->rand_seed);
      }

    return pg_cnt;
  }
  typedef enum
  {
    OBJ_GET_WITHOUT_LOCK = 0,
    OBJ_REPEAT_GET_WITH_LOCK = 1,
    OBJ_GET_WITH_LOCK_COMPLETE = 2
  } OBJECT_GET_STATUS;

  SCAN_CODE scan_next_heap_scan_1page_internal (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, VPID *curr_vpid)
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
    struct regu_variable_list_node *p;

    hsidp = &scan_id->s.hsid;
    if (scan_id->mvcc_select_lock_needed)
      {
	COPY_OID (&current_oid, &hsidp->curr_oid);
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
	assert (!scan_id->grouped);

	{
	  recdes.data = NULL;
	  assert (scan_id->direction == S_FORWARD);
	  assert (scan_id->type == S_HEAP_SCAN);
	  {
	    sp_scan =
		    heap_next_1page (thread_p, &hsidp->hfid, curr_vpid, &hsidp->cls_oid, &hsidp->curr_oid, &recdes,
				     &hsidp->scan_cache, is_peeking);
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
	    && PGBUF_IS_PAGE_CHANGED (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa))
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
	  {
	    /* invalid value; the same as QPROC_QUALIFIED */
	    if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	      {
		continue;		/* not qualified, continue to the next tuple */
	      }
	  }

	/* Data filter passed. If object should be locked and is not locked yet, lock it. */
	assert (!scan_id->mvcc_select_lock_needed);

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
			&& PGBUF_IS_PAGE_CHANGED (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa)))
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
	    if (heap_attrinfo_read_dbvalues (thread_p, p_current_oid, &recdes, hsidp->rest_attrs.attr_cache) != NO_ERROR)
	      {
		return S_ERROR;
	      }

	    if (is_peeking == PEEK && hsidp->scan_cache.page_watcher.pgptr != NULL
		&& PGBUF_IS_PAGE_CHANGED (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa))
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
		    && PGBUF_IS_PAGE_CHANGED (hsidp->scan_cache.page_watcher.pgptr, &ref_lsa))
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
}
#endif /* SERVER_MODE */
