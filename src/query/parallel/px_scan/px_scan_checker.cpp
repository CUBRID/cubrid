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
 * px_scan_checker.cpp - module that checks whether parallel scan is possible.
 */

#include "px_scan_checker.hpp"

#include "dbtype_def.h"
#include "error_manager.h"
#include "regu_var.hpp"
#include "schema_manager.h"
#include "storage_common.h"
#include "work_space.h"
#include "xasl_predicate.hpp"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include <unordered_map>
#include <unordered_set>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  /* modes: list_merge (fast, workers merge partial lists) | row_by_row (fallback) | buildvalue_opt (agg-only selects; blocked by ROWNUM in row_by_row). */

  using possible_flags = uint32_t;
  const possible_flags CANNOT_PARALLEL_SCAN = 0x1 << 0;
  const possible_flags CANNOT_LIST_MERGE = 0x1 << 1;
  const possible_flags CANNOT_BUILDVALUE_OPT = 0x1 << 2;

  static bool
  is_buildvalue_opt_supported_function (FUNC_CODE function)
  {
    switch (function)
      {
      case PT_COUNT_STAR:
      case PT_COUNT:
      case PT_MIN:
      case PT_MAX:
      case PT_SUM:
      case PT_AVG:
      case PT_STDDEV:
      case PT_STDDEV_POP:
      case PT_STDDEV_SAMP:
      case PT_VARIANCE:
      case PT_VAR_POP:
      case PT_VAR_SAMP:
	return true;
      default:
	return false;
      }
  }

  /* thread-local cache: memoizes check results and breaks circular XASL refs. */
  thread_local std::unordered_map<XASL_NODE *, possible_flags> xasl_check_cache;

  /* cycle guard for the process_* recursion. */
  thread_local std::unordered_set<XASL_NODE *> xasl_processing_set;

  static void set_flag (possible_flags &flags, possible_flags flag)
  {
    flags |= flag;
  }
  static bool is_flag_set (possible_flags flags, possible_flags flag)
  {
    return (flags & flag) != 0;
  }

  using rv_list_node = struct regu_variable_list_node;

  /* prototypes */
  template <typename T, bool is_outptr_list>
  possible_flags check (T *arg);

  template <bool is_outptr_list>
  possible_flags check (REGU_VARIABLE *arg);
  template <bool is_outptr_list>
  possible_flags check (PRED_EXPR *arg);
  template <bool is_outptr_list>
  possible_flags check (rv_list_node *arg);
  template <bool is_outptr_list>
  possible_flags check (ARITH_TYPE *arg);
  template <bool is_outptr_list>
  possible_flags check (PRED *arg);
  template <bool is_outptr_list>
  possible_flags check (EVAL_TERM *arg);
  template <bool is_outptr_list>
  possible_flags check (COMP_EVAL_TERM *arg);
  template <bool is_outptr_list>
  possible_flags check (ALSM_EVAL_TERM *arg);
  template <bool is_outptr_list>
  possible_flags check (LIKE_EVAL_TERM *arg);
  template <bool is_outptr_list>
  possible_flags check (RLIKE_EVAL_TERM *arg);
  template <bool is_outptr_list>
  possible_flags check (ACCESS_SPEC_TYPE *arg);
  template <bool is_outptr_list>
  possible_flags check (XASL_NODE *arg);
  template <bool is_outptr_list>
  possible_flags sibling_check (XASL_NODE *sibling);
  template <bool is_outptr_list>
  possible_flags sibling_check (ACCESS_SPEC_TYPE *arg);

  void process_xasl_node_recursive (XASL_NODE *arg);
  void process_xasl_node_recursive_force_cannot_parallel (XASL_NODE *arg);
  void block_parallel_index_and_temp_in_subtree (XASL_NODE *arg);

  template <bool is_outptr_list>
  possible_flags check (REGU_VARIABLE *arg)
  {
    possible_flags result = 0, temp = 0;
    if (!arg)
      {
	return result;
      }

    if (arg->xasl)
      {
	temp = sibling_check<is_outptr_list> (arg->xasl);
	if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
      }

    switch (arg->type)
      {
      case TYPE_ATTR_ID:		/* fetch object attribute value */
      case TYPE_SHARED_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
	if (arg->value.attr_descr.type == DB_TYPE_OBJECT || arg->value.attr_descr.type == DB_TYPE_OID)
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
	break;
      case TYPE_CONSTANT:
      case TYPE_OID:
      case TYPE_DBVAL:
      case TYPE_POSITION:
      case TYPE_POS_VALUE:
      case TYPE_LIST_ID:
	break;
      case TYPE_ORDERBY_NUM:
      case TYPE_CLASSOID:
      case TYPE_REGUVAL_LIST:
	set_flag (result, CANNOT_PARALLEL_SCAN);
	break;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
	temp = check<is_outptr_list> (arg->value.arithptr);
	result |= temp;
	break;
      case TYPE_SP:
	result |= check<is_outptr_list> (arg->value.sp_ptr->args);
	/* SP not executable in child threads. */
	if (is_outptr_list)
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	  }
	else
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
	break;
      case TYPE_FUNC:
	temp = check<is_outptr_list> (arg->value.funcp->operand);
	result |= temp;
	break;
      case TYPE_REGU_VAR_LIST:
	temp = check<is_outptr_list> (arg->value.regu_var_list);
	if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
	else
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	  }
	break;
      default:
	set_flag (result, CANNOT_PARALLEL_SCAN);
	break;
      }

    return result;
  }

  template <bool is_outptr_list>
  possible_flags check (PRED_EXPR *arg)
  {
    if (!arg)
      {
	return 0;
      }
    switch (arg->type)
      {
      case T_PRED:
	return check<is_outptr_list> (&arg->pe.m_pred);
      case T_EVAL_TERM:
	return check<is_outptr_list> (&arg->pe.m_eval_term);
      case T_NOT_TERM:
	return check<is_outptr_list> (arg->pe.m_not_term);
      default:
	return 0;
      }
  }

  template <bool is_outptr_list>
  possible_flags check (rv_list_node *arg)
  {
    if (!arg)
      {
	return 0;
      }
    possible_flags result = 0;
    rv_list_node *curr = arg;
    while (curr)
      {
	result |= check<is_outptr_list> (&curr->value);
	curr = curr->next;
      }
    return result;
  }

  template <bool is_outptr_list>
  possible_flags check (ARITH_TYPE *arg)
  {
    if (!arg)
      {
	return 0;
      }
    possible_flags result = 0;
    result |= check<is_outptr_list> (arg->leftptr);
    result |= check<is_outptr_list> (arg->rightptr);
    result |= check<is_outptr_list> (arg->thirdptr);
    result |= check<is_outptr_list> (arg->pred);
    if (arg->opcode == T_TRACE_STATS || arg->opcode == T_EVALUATE_VARIABLE || arg->opcode == T_DEFINE_VARIABLE
	|| arg->opcode == T_CURRENT_VALUE || arg->opcode == T_NEXT_VALUE)
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
      }
    return result;
  }

  template <bool is_outptr_list>
  possible_flags check (PRED *arg)
  {
    if (!arg)
      {
	return 0;
      }
    possible_flags result = 0;
    result |= check<is_outptr_list> (arg->lhs);
    result |= check<is_outptr_list> (arg->rhs);
    return result;
  }

  template <bool is_outptr_list>
  possible_flags check (EVAL_TERM *arg)
  {
    if (!arg)
      {
	return 0;
      }
    switch (arg->et_type)
      {
      case T_COMP_EVAL_TERM:
	return check<is_outptr_list> (&arg->et.et_comp);
      case T_ALSM_EVAL_TERM:
	return check<is_outptr_list> (&arg->et.et_alsm);
      case T_LIKE_EVAL_TERM:
	return check<is_outptr_list> (&arg->et.et_like);
      case T_RLIKE_EVAL_TERM:
	return check<is_outptr_list> (&arg->et.et_rlike);
      default:
	return 0;
      }
  }

  template <bool is_outptr_list>
  possible_flags check (COMP_EVAL_TERM *arg)
  {
    if (!arg)
      {
	return 0;
      }
    return check<is_outptr_list> (arg->lhs) | check<is_outptr_list> (arg->rhs);
  }

  template <bool is_outptr_list>
  possible_flags check (ALSM_EVAL_TERM *arg)
  {
    if (!arg)
      {
	return 0;
      }
    return check<is_outptr_list> (arg->elem) | check<is_outptr_list> (arg->elemset);
  }

  template <bool is_outptr_list>
  possible_flags check (LIKE_EVAL_TERM *arg)
  {
    if (!arg)
      {
	return 0;
      }
    return check<is_outptr_list> (arg->src) | check<is_outptr_list> (arg->pattern) | check<is_outptr_list> (arg->esc_char);
  }

  template <bool is_outptr_list>
  possible_flags check (RLIKE_EVAL_TERM *arg)
  {
    if (!arg)
      {
	return 0;
      }
    return check<is_outptr_list> (arg->src) | check<is_outptr_list> (arg->pattern) | check<is_outptr_list>
	   (arg->case_sensitive);
  }

  /* filtered index → serial: bug-prone + low usage, excluded as a constraint. function indexes are plain B-tree keys, not blocked. */
  static bool
  is_filtered_index (const INDX_INFO *indexptr)
  {
    if (indexptr == NULL)
      {
	return false;
      }
    if (OID_ISNULL (&indexptr->class_oid))
      {
	return false;
      }
    MOP class_mop = ws_mop (&indexptr->class_oid, NULL);
    if (class_mop == NULL)
      {
	return false;
      }
    SM_CLASS_CONSTRAINT *cons = sm_class_constraints (class_mop);
    for (; cons != NULL; cons = cons->next)
      {
	if (BTID_IS_EQUAL (&cons->index_btid, &indexptr->btid))
	  {
	    if (cons->filter_predicate != NULL)
	      {
		return true;
	      }
	    break;
	  }
      }
    return false;
  }

  template <>
  possible_flags check<false> (ACCESS_SPEC_TYPE *arg)
  {
    possible_flags result = 0;
    if (!arg)
      {
	return 0;
      }
    if (arg->type == TARGET_CLASS)
      {
	if (arg->access == ACCESS_METHOD_SEQUENTIAL)
	  {
	  }
	else if (arg->access == ACCESS_METHOD_INDEX)
	  {
	    if (ACCESS_SPEC_IS_FLAGED (arg, ACCESS_SPEC_FLAG_ONLY_MIN_MAX_SCAN))
	      {
		/* min/max agg scan emits no rows. */
		set_flag (result, CANNOT_PARALLEL_SCAN);
		return result;
	      }
	    if (arg->indexptr != NULL)
	      {
		/* ISS/ILS dynamically rewrites curr_keyno/key-ranges; conflicts with leaf-page cursor. */
		if (arg->indexptr->use_iss || arg->indexptr->ils_prefix_len > 0)
		  {
		    set_flag (result, CANNOT_PARALLEL_SCAN);
		  }

		/* keylimit: global cap incompatible with per-worker page split. */
		if (arg->indexptr->key_info.is_user_given_keylimit)
		  {
		    set_flag (result, CANNOT_PARALLEL_SCAN);
		  }

		/* orderby/groupby skip+desc need globally ordered traversal. */
		if (arg->indexptr->orderby_skip || arg->indexptr->groupby_skip
		    || arg->indexptr->orderby_desc || arg->indexptr->groupby_desc)
		  {
		    set_flag (result, CANNOT_PARALLEL_SCAN);
		  }

		/* filtered index: bug-prone + low usage, excluded. */
		if (is_filtered_index (arg->indexptr))
		  {
		    set_flag (result, CANNOT_PARALLEL_SCAN);
		  }
	      }
	  }
	else
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	    return result;
	  }
      }
    else if (arg->type == TARGET_LIST)
      {
      }
    else
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
	return result;
      }
    if (arg->next)
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
	return result;
      }
    if (arg->type == TARGET_CLASS)
      {
	result |= check<false> (arg->s.cls_node.cls_regu_list_pred);
	result |= check<false> (arg->s.cls_node.cls_regu_list_rest);
	result |= check<false> (arg->where_pred);
	if (arg->access == ACCESS_METHOD_INDEX)
	  {
	    result |= check<false> (arg->s.cls_node.cls_regu_list_key);
	    result |= check<false> (arg->where_key);
	    result |= check<false> (arg->s.cls_node.cls_regu_list_range);
	    result |= check<false> (arg->where_range);
	  }
	if (!arg->s.cls_node.cls_regu_list_pred && !arg->s.cls_node.cls_regu_list_rest)
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	  }
      }
    else if (arg->type == TARGET_LIST)
      {
	result |= check<false> (arg->s.list_node.list_regu_list_pred);
	result |= check<false> (arg->s.list_node.list_regu_list_rest);
	result |= check<false> (arg->where_pred);
	if (!arg->s.list_node.list_regu_list_pred && !arg->s.list_node.list_regu_list_rest)
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	  }
      }
    return result;
  }

  template <bool is_outptr_list>
  possible_flags sibling_check (ACCESS_SPEC_TYPE *arg)
  {
    possible_flags result = 0;
    if (!arg)
      {
	return 0;
      }
    if (arg->next)
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
	return result;
      }
    if (arg->type != TARGET_CLASS && arg->type != TARGET_LIST)
      {
	set_flag (result, CANNOT_LIST_MERGE);
      }
    else
      {
	result |= check<false> (arg->s.cls_node.cls_regu_list_pred);
	result |= check<false> (arg->s.cls_node.cls_regu_list_rest);
	result |= check<false> (arg->where_pred);
      }
    return result;
  }

  template <bool is_outptr_list>
  possible_flags sibling_check (XASL_NODE *sibling)
  {
    if (!sibling)
      {
	return 0;
      }

    possible_flags result = 0, temp = 0;

    if (sibling->selected_upd_list || sibling->scan_op_type != S_SELECT || sibling->upd_del_class_cnt > 0
	|| XASL_IS_FLAGED (sibling, XASL_MULTI_UPDATE_AGG))
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
      }

    for (XASL_NODE *xaslp = sibling->aptr_list; xaslp; xaslp = xaslp->next)
      {
	result |= check<is_outptr_list> (xaslp);
      }

    if (sibling->bptr_list || sibling->fptr_list)
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
      }

    for (XASL_NODE *xaslp = sibling->dptr_list; xaslp; xaslp = xaslp->next)
      {
	temp = sibling_check<false> (xaslp);
	if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
      }

    if (sibling->connect_by_ptr)
      {
	set_flag (result, CANNOT_LIST_MERGE);
      }

    if (sibling->if_pred)
      {
	temp = check<is_outptr_list> (sibling->if_pred);
	if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
      }

    if (sibling->instnum_pred || sibling->instnum_val)
      {
	set_flag (result, CANNOT_LIST_MERGE);
      }

    for (ACCESS_SPEC_TYPE *specp = sibling->spec_list; specp; specp = specp->next)
      {
	result |= sibling_check<is_outptr_list> (specp);
      }

    return result;
  }

  template <bool is_outptr_list>
  possible_flags check (XASL_NODE *arg)
  {
    if (!arg)
      {
	return 0;
      }

    auto it = xasl_check_cache.find (arg);
    if (it != xasl_check_cache.end ())
      {
	return it->second;
      }

    /* mark visited (sentinel 0) before recursing — breaks XASL ref cycles. */
    xasl_check_cache[arg] = 0;

    possible_flags result = 0, temp = 0;
    bool buildvalue_opt = false;
    switch (arg->type)
      {
      case BUILDLIST_PROC:
	break;
      case BUILDVALUE_PROC:
	if (arg->proc.buildvalue.agg_list)
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	    buildvalue_opt = true;
	    AGGREGATE_TYPE *agg_it = arg->proc.buildvalue.agg_list;
	    int agg_cnt = 0;
	    temp = 0;
	    for (; agg_it; agg_it = agg_it->next)
	      {
		agg_cnt++;
		if (!is_buildvalue_opt_supported_function (agg_it->function))
		  {
		    buildvalue_opt = false;
		    break;
		  }
		temp |= check<false> (agg_it->operands);
		if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
		  {
		    buildvalue_opt = false;
		    break;
		  }
	      }
	    if (agg_cnt != arg->outptr_list->valptr_cnt)
	      {
		buildvalue_opt = false;
	      }
	  }
	break;
      case CTE_PROC:
	if (arg->proc.cte.recursive_part)
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
	break;
      case MERGE_PROC:
	set_flag (result, CANNOT_PARALLEL_SCAN);
	break;
      case HASHJOIN_PROC:
      case UNION_PROC:
      case DIFFERENCE_PROC:
      case INTERSECTION_PROC:
      case INSERT_PROC:
	break;
      case MERGELIST_PROC:
	set_flag (result, CANNOT_PARALLEL_SCAN);
	break;
      case OBJFETCH_PROC:
      case UPDATE_PROC:
      case DELETE_PROC:
      case CONNECTBY_PROC:
      case DO_PROC:
      case BUILD_SCHEMA_PROC:
      case SCAN_PROC:
      default:
	set_flag (result, CANNOT_PARALLEL_SCAN);
	break;
      }

    if (arg->selected_upd_list || arg->scan_op_type != S_SELECT || arg->upd_del_class_cnt > 0
	|| XASL_IS_FLAGED (arg, XASL_MULTI_UPDATE_AGG))
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
      }
    for (XASL_NODE *xaslp = arg->aptr_list; xaslp; xaslp = xaslp->next)
      {
	if (XASL_IS_FLAGED (xaslp, XASL_LINK_TO_REGU_VARIABLE))
	  {
	    temp = sibling_check<false> (xaslp);
	    if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
	      {
		set_flag (result, CANNOT_PARALLEL_SCAN);
	      }
	  }
	else
	  {
	    /* this xasl not belong to current arg */
	  }
      }

    if (arg->bptr_list || arg->fptr_list || arg->connect_by_ptr)
      {
	set_flag (result, CANNOT_PARALLEL_SCAN);
      }

    std::unordered_set<XASL_NODE *> dptrs;
    for (XASL_NODE *xaslp1 = arg; xaslp1; xaslp1 = xaslp1->scan_ptr)
      {
	for (XASL_NODE *xaslp2 = xaslp1->dptr_list; xaslp2; xaslp2 = xaslp2->next)
	  {
	    dptrs.insert (xaslp2);
	  }
      }

    for (XASL_NODE *xaslp : dptrs)
      {
	temp = sibling_check<false> (xaslp);
	if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
      }

    if (dptrs.size() > 0)
      {
	std::unordered_set<XASL_NODE *> dptrs2 (dptrs);
	for (XASL_NODE *xaslp : dptrs2)
	  {
	    if (XASL_IS_FLAGED (xaslp, XASL_LINK_TO_REGU_VARIABLE))
	      {
		dptrs.erase (xaslp);
	      }
	  }
	if (dptrs.size() > 0)
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
      }

    if (arg->scan_ptr)
      {
	for (XASL_NODE *xaslp = arg->scan_ptr; xaslp; xaslp = xaslp->scan_ptr)
	  {
	    result |= sibling_check<is_outptr_list> (xaslp);
	  }
      }

    if (arg->connect_by_ptr)
      {
	set_flag (result, CANNOT_LIST_MERGE);
	buildvalue_opt = false;
      }

    if (arg->if_pred)
      {
	temp = check<is_outptr_list> (arg->if_pred);
	if (is_flag_set (temp, CANNOT_PARALLEL_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
      }

    if (arg->instnum_pred || arg->instnum_val)
      {
	set_flag (result, CANNOT_LIST_MERGE);
	buildvalue_opt = false;
      }

    if (arg->outptr_list)
      {
	result |= check<true> (arg->outptr_list->valptrp);
      }

    for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
      {
	result |= check<false> (specp);
      }
    for (ACCESS_SPEC_TYPE *specp = arg->merge_spec; specp; specp = specp->next)
      {
	result |= check<false> (specp);
      }

    if (!buildvalue_opt)
      {
	set_flag (result, CANNOT_BUILDVALUE_OPT);
      }

    /* Update cache with computed result */
    xasl_check_cache[arg] = result;

    return result;
  }

  void process_xasl_node_recursive (XASL_NODE *arg)
  {
    if (!arg)
      {
	return;
      }

    if (xasl_processing_set.find (arg) != xasl_processing_set.end ())
      {
	return;
      }
    xasl_processing_set.insert (arg);

    possible_flags result = 0;
    switch (arg->type)
      {
      case CTE_PROC:
	if (arg->proc.cte.recursive_part)
	  {
	    process_xasl_node_recursive_force_cannot_parallel (arg->proc.cte.recursive_part);
	    set_flag (result, CANNOT_PARALLEL_SCAN);
	  }
	if (arg->proc.cte.non_recursive_part)
	  {
	    process_xasl_node_recursive (arg->proc.cte.non_recursive_part);
	  }
	break;
      case MERGE_PROC:
	if (arg->proc.merge.insert_xasl)
	  {
	    process_xasl_node_recursive_force_cannot_parallel (arg->proc.merge.insert_xasl);
	  }
	if (arg->proc.merge.update_xasl)
	  {
	    process_xasl_node_recursive_force_cannot_parallel (arg->proc.merge.update_xasl);
	  }
	set_flag (result, CANNOT_PARALLEL_SCAN);
	break;
      case MERGELIST_PROC:
	for (ACCESS_SPEC_TYPE *specp = arg->proc.mergelist.outer_spec_list; specp; specp = specp->next)
	  {
	    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
	  }
	for (ACCESS_SPEC_TYPE *specp = arg->proc.mergelist.inner_spec_list; specp; specp = specp->next)
	  {
	    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
	  }
	for (XASL_NODE *xaslp = arg->aptr_list; xaslp; xaslp = xaslp->next)
	  {
	    block_parallel_index_and_temp_in_subtree (xaslp);
	  }
	break;
      case BUILDLIST_PROC:
      case BUILDVALUE_PROC:
      case HASHJOIN_PROC:
      case UNION_PROC:
      case DIFFERENCE_PROC:
      case INTERSECTION_PROC:
      case INSERT_PROC:
	break;
      case OBJFETCH_PROC:
      case UPDATE_PROC:
      case DELETE_PROC:
      case CONNECTBY_PROC:
      case DO_PROC:
      case BUILD_SCHEMA_PROC:
      case SCAN_PROC:
      default:
	process_xasl_node_recursive_force_cannot_parallel (arg);
	set_flag (result, CANNOT_PARALLEL_SCAN);
	break;
      }

    for (XASL_NODE *xaslp = arg->aptr_list; xaslp; xaslp = xaslp->next)
      {
	if (XASL_IS_FLAGED (xaslp, XASL_LINK_TO_REGU_VARIABLE))
	  {
	    process_xasl_node_recursive_force_cannot_parallel (xaslp);
	  }
	else
	  {
	    process_xasl_node_recursive (xaslp);
	  }
      }
    for (XASL_NODE *xaslp = arg->bptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->dptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->fptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->scan_ptr; xaslp; xaslp = xaslp->scan_ptr)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }

    result |= check<false> (arg);

    const bool block_index_spec =
	    (arg->instnum_pred || arg->instnum_val)
	    || XASL_IS_FLAGED (arg, XASL_ANALYTIC_SKIP_SORT)
	    || XASL_IS_FLAGED (arg, XASL_ANALYTIC_USES_LIMIT_OPT);

    const bool block_all_specs = XASL_IS_FLAGED (arg, XASL_SKIP_ORDERBY_LIST);

    if (is_flag_set (result, CANNOT_PARALLEL_SCAN) || block_all_specs)
      {
	for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
	  {
	    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
	  }
      }
    else
      {
	if (block_index_spec)
	  {
	    for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
	      {
		if (specp->type == TARGET_CLASS && specp->access == ACCESS_METHOD_INDEX)
		  {
		    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
		  }
	      }
	  }

	if (!is_flag_set (result, CANNOT_BUILDVALUE_OPT))
	  {
	    for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
	      {
		ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_BUILDVALUE_OPT);
	      }
	  }
	else
	  {
	    if (!is_flag_set (result, CANNOT_LIST_MERGE))
	      {
		for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
		  {
		    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_MERGEABLE_LIST);
		  }
	      }
	    else
	      {
		/* list merge blocked → row-by-row fallback. */
		for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
		  {
		    ACCESS_SPEC_UNSET_FLAG (specp, ACCESS_SPEC_FLAG_MERGEABLE_LIST);
		    if (specp->type == TARGET_LIST
			|| (specp->type == TARGET_CLASS && specp->access == ACCESS_METHOD_INDEX))
		      {
			ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
		      }
		  }
	      }
	  }
      }

  }

  void block_parallel_index_and_temp_in_subtree (XASL_NODE *arg)
  {
    if (!arg)
      {
	return;
      }
    for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
      {
	if (specp->type == TARGET_LIST
	    || (specp->type == TARGET_CLASS && IS_ANY_INDEX_ACCESS (specp->access)))
	  {
	    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
	  }
      }
    for (XASL_NODE *xaslp = arg->aptr_list; xaslp; xaslp = xaslp->next)
      {
	block_parallel_index_and_temp_in_subtree (xaslp);
      }
    for (XASL_NODE *xaslp = arg->bptr_list; xaslp; xaslp = xaslp->next)
      {
	block_parallel_index_and_temp_in_subtree (xaslp);
      }
    for (XASL_NODE *xaslp = arg->dptr_list; xaslp; xaslp = xaslp->next)
      {
	block_parallel_index_and_temp_in_subtree (xaslp);
      }
    for (XASL_NODE *xaslp = arg->fptr_list; xaslp; xaslp = xaslp->next)
      {
	block_parallel_index_and_temp_in_subtree (xaslp);
      }
    for (XASL_NODE *xaslp = arg->scan_ptr; xaslp; xaslp = xaslp->scan_ptr)
      {
	block_parallel_index_and_temp_in_subtree (xaslp);
      }
  }

  void process_xasl_node_recursive_force_cannot_parallel (XASL_NODE *arg)
  {
    if (!arg)
      {
	return;
      }

    if (xasl_processing_set.find (arg) != xasl_processing_set.end ())
      {
	return;
      }
    xasl_processing_set.insert (arg);

    for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
      {
	ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
      }

    for (XASL_NODE *xaslp = arg->aptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->bptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->dptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->fptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->scan_ptr; xaslp; xaslp = xaslp->scan_ptr)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }

    switch (arg->type)
      {
      case CTE_PROC:
	if (arg->proc.cte.recursive_part)
	  {
	    process_xasl_node_recursive_force_cannot_parallel (arg->proc.cte.recursive_part);
	  }
	if (arg->proc.cte.non_recursive_part)
	  {
	    process_xasl_node_recursive_force_cannot_parallel (arg->proc.cte.non_recursive_part);
	  }
	break;
      case MERGE_PROC:
	if (arg->proc.merge.insert_xasl)
	  {
	    process_xasl_node_recursive_force_cannot_parallel (arg->proc.merge.insert_xasl);
	  }
	if (arg->proc.merge.update_xasl)
	  {
	    process_xasl_node_recursive_force_cannot_parallel (arg->proc.merge.update_xasl);
	  }
	break;
      case MERGELIST_PROC:
	/* guard llsid union from pllsid_parallel overwrite via outer/inner spec_list. */
	for (ACCESS_SPEC_TYPE *specp = arg->proc.mergelist.outer_spec_list; specp; specp = specp->next)
	  {
	    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
	  }
	for (ACCESS_SPEC_TYPE *specp = arg->proc.mergelist.inner_spec_list; specp; specp = specp->next)
	  {
	    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_SCAN);
	  }
	break;
      default:
	break;
      }
  }
}

extern int
scan_check_parallel_scan_possible (XASL_NODE *xasl)
{
  parallel_scan::xasl_check_cache.clear ();
  parallel_scan::xasl_processing_set.clear ();

  parallel_scan::process_xasl_node_recursive (xasl);

  parallel_scan::xasl_check_cache.clear ();
  parallel_scan::xasl_processing_set.clear ();

  return NO_ERROR;
}
