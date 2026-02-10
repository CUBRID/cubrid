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
 * px_heap_scan_checker.cpp - module that checks whether parallel heap scan is possible.
 */

#include "px_heap_scan_checker.hpp"

#include "regu_var.hpp"
#include "storage_common.h"
#include "xasl_predicate.hpp"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include <unordered_map>
#include <unordered_set>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  /*
   * Parallel heap scan has two execution modes:
   *   1. list_merge: Fast mode that merges sorted partial results from workers
   *   2. row_by_row: Slower mode that processes rows individually, but covers all cases
   *
   * When list_merge is not possible, row_by_row is always available as a fallback.
   *
   * Count optimization (count_opt) can be applied when:
   *   - The select list contains only count() aggregate functions
   *   - list_merge mode: count_opt is always possible
   *   - row_by_row mode: count_opt may not be possible due to constraints like ROWNUM
   *
   * The COUNT_OPT_POSSIBLE flag specifically checks:
   *   1. Whether the select list consists solely of count() functions
   *   2. Whether row_by_row constraints (e.g., ROWNUM usage) prevent count optimization
   */

  using possible_flags = uint32_t;
  const possible_flags CANNOT_PARALLEL_HEAP_SCAN = 0x1 << 0;
  const possible_flags CANNOT_LIST_MERGE = 0x1 << 1;
  const possible_flags CANNOT_COUNT_OPT = 0x1 << 2;

  // Thread-local map to cache check results for XASL_NODE and prevent infinite recursion
  // This is used to detect circular references in XASL structures and reuse computed results
  thread_local std::unordered_map<XASL_NODE *, possible_flags> xasl_check_cache;

  // Thread-local set to track XASL_NODEs being processed to prevent infinite recursion in process functions
  thread_local std::unordered_set<XASL_NODE *> xasl_processing_set;

  inline void set_flag (possible_flags &flags, possible_flags flag)
  {
    flags |= flag;
  }
  inline void clear_flag (possible_flags &flags, possible_flags flag)
  {
    flags &= ~flag;
  }
  inline bool is_flag_set (possible_flags flags, possible_flags flag)
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

  void process_xasl_node_recursive (XASL_NODE *arg);
  void process_xasl_node_recursive_force_cannot_parallel (XASL_NODE *arg);

  inline bool is_pred_exists (PRED_EXPR *pred_expr)
  {
    if (!pred_expr || pred_expr->type != T_EVAL_TERM || pred_expr->pe.m_eval_term.et_type != T_COMP_EVAL_TERM
	|| pred_expr->pe.m_eval_term.et.et_comp.rel_op != R_EXISTS)
      {
	return false;
      }
    return true;
  }

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
	temp = check<is_outptr_list> (arg->xasl);
	if (is_flag_set (temp, CANNOT_PARALLEL_HEAP_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	  }
      }

    switch (arg->type)
      {
      case TYPE_ATTR_ID:		/* fetch object attribute value */
      case TYPE_SHARED_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
	break;
      case TYPE_CONSTANT:
      case TYPE_OID:
      case TYPE_DBVAL:
      case TYPE_POSITION:
      case TYPE_POS_VALUE:
      case TYPE_LIST_ID:
	/* can execute with constants */
	break;
      case TYPE_ORDERBY_NUM:
      case TYPE_CLASSOID:
      case TYPE_REGUVAL_LIST:
	/* cannot execute with this regu-variable */
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	break;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
	temp = check<is_outptr_list> (arg->value.arithptr);
	result |= temp;
	break;
      case TYPE_SP:
	result |= check<is_outptr_list> (arg->value.sp_ptr->args);
	/* cannot execute sp in child threads */
	if (is_outptr_list)
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	  }
	else
	  {
	    set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	  }
	break;
      case TYPE_FUNC:
	temp = check<is_outptr_list> (arg->value.funcp->operand);
	result |= temp;
	break;
      case TYPE_REGU_VAR_LIST:
	temp = check<is_outptr_list> (arg->value.regu_var_list);
	if (is_flag_set (temp, CANNOT_PARALLEL_HEAP_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	  }
	else
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	  }
	break;
      default:
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
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
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
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
	break;
      case T_ALSM_EVAL_TERM:
	return check<is_outptr_list> (&arg->et.et_alsm);
	break;
      case T_LIKE_EVAL_TERM:
	return check<is_outptr_list> (&arg->et.et_like);
	break;
      case T_RLIKE_EVAL_TERM:
	return check<is_outptr_list> (&arg->et.et_rlike);
	break;
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

  template <>
  possible_flags check<false> (ACCESS_SPEC_TYPE *arg)
  {
    possible_flags result = 0;
    if (!arg)
      {
	return 0;
      }
    if (arg->access != ACCESS_METHOD_SEQUENTIAL || arg->type != TARGET_CLASS)
      {
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	return result;
      }
    if (arg->next)
      {
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	return result;
      }
    result |= check<false> (arg->s.cls_node.cls_regu_list_pred);
    result |= check<false> (arg->s.cls_node.cls_regu_list_rest);
    result |= check<false> (arg->where_pred);
    if (!arg->s.cls_node.cls_regu_list_pred && !arg->s.cls_node.cls_regu_list_rest)
      {
	set_flag (result, CANNOT_LIST_MERGE);
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

    // Check if this XASL_NODE has already been checked
    auto it = xasl_check_cache.find (arg);
    if (it != xasl_check_cache.end ())
      {
	// Return cached result
	return it->second;
      }

    // Mark as being visited (with temporary result 0) to prevent infinite recursion
    xasl_check_cache[arg] = 0;

    possible_flags result = 0, temp = 0;
    bool count_opt = false;
    switch (arg->type)
      {
      case BUILDLIST_PROC:
	if (arg->proc.buildlist.g_hash_eligible)
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	  }
	break;
      case BUILDVALUE_PROC:
	if (arg->proc.buildvalue.agg_list)
	  {
	    set_flag (result, CANNOT_LIST_MERGE);
	    count_opt = true;
	    AGGREGATE_TYPE *agg_it = arg->proc.buildvalue.agg_list;
	    int agg_cnt = 0;
	    temp = 0;
	    for (; agg_it; agg_it = agg_it->next)
	      {
		agg_cnt++;
		if (agg_it->function != PT_COUNT_STAR && agg_it->function != PT_COUNT)
		  {
		    count_opt = false;
		    break;
		  }
		temp |= check<false> (agg_it->operands);
		if (is_flag_set (temp, CANNOT_PARALLEL_HEAP_SCAN))
		  {
		    count_opt = false;
		    break;
		  }
	      }
	    if (agg_cnt != arg->outptr_list->valptr_cnt)
	      {
		count_opt = false;
	      }
	  }
	break;
      case CTE_PROC:
	if (arg->proc.cte.recursive_part)
	  {
	    set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	  }
	break;
      case MERGE_PROC:
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	break;
      case HASHJOIN_PROC:
      case UNION_PROC:
      case DIFFERENCE_PROC:
      case INTERSECTION_PROC:
      case INSERT_PROC:
      case MERGELIST_PROC:
	break;
      case OBJFETCH_PROC:
      case UPDATE_PROC:
      case DELETE_PROC:
      case CONNECTBY_PROC:
      case DO_PROC:
      case BUILD_SCHEMA_PROC:
      case SCAN_PROC:
      default:
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	break;
      }

    if (arg->selected_upd_list || arg->scan_op_type != S_SELECT || arg->upd_del_class_cnt > 0
	|| XASL_IS_FLAGED (arg, XASL_MULTI_UPDATE_AGG))
      {
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
      }
    for (XASL_NODE *xaslp = arg->aptr_list; xaslp; xaslp = xaslp->next)
      {
	result |= check<is_outptr_list> (xaslp);
      }

    if (arg->bptr_list || arg->fptr_list || arg->connect_by_ptr)
      {
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
      }

    for (XASL_NODE *xaslp = arg->dptr_list; xaslp; xaslp = xaslp->next)
      {
	temp = check<false> (xaslp);
	if (is_flag_set (temp, CANNOT_PARALLEL_HEAP_SCAN))
	  {
	    set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	  }
      }

    if (arg->scan_ptr)
      {
	set_flag (result, CANNOT_LIST_MERGE);
	count_opt = false;
      }

    if (arg->connect_by_ptr)
      {
	set_flag (result, CANNOT_LIST_MERGE);
	count_opt = false;
      }

    if (arg->if_pred)
      {
	if (!is_pred_exists (arg->if_pred))
	  {
	    set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	  }
      }

    if (arg->instnum_pred || arg->instnum_val)
      {
	set_flag (result, CANNOT_LIST_MERGE);
	count_opt = false;
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

    if (!count_opt)
      {
	set_flag (result, CANNOT_COUNT_OPT);
      }

    // Update cache with computed result
    xasl_check_cache[arg] = result;

    return result;
  }

  void process_xasl_node_recursive (XASL_NODE *arg)
  {
    if (!arg)
      {
	return;
      }

    // Check if this XASL_NODE is already being processed to prevent infinite recursion
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
	    set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
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
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	break;
      case BUILDLIST_PROC:
      case BUILDVALUE_PROC:
      case HASHJOIN_PROC:
      case UNION_PROC:
      case DIFFERENCE_PROC:
      case INTERSECTION_PROC:
      case INSERT_PROC:
      case MERGELIST_PROC:
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
	set_flag (result, CANNOT_PARALLEL_HEAP_SCAN);
	break;
      }

    for (XASL_NODE *xaslp = arg->aptr_list; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive (xaslp);
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
    for (XASL_NODE *xaslp = arg->scan_ptr; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }

    result |= check<false> (arg);
    if (is_flag_set (result, CANNOT_PARALLEL_HEAP_SCAN))
      {
	for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
	  {
	    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_HEAP_SCAN);
	  }
      }
    else
      {
	/* parallel heap scan is possible */
	if (!is_flag_set (result, CANNOT_COUNT_OPT))
	  {
	    /* count optimization is possible */
	    for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
	      {
		ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_COUNT_DISTINCT);
	      }
	  }
	else
	  {
	    if (!is_flag_set (result, CANNOT_LIST_MERGE))
	      {
		/* list merge is possible */
		for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
		  {
		    ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_MERGEABLE_LIST);
		  }
	      }
	    else
	      {
		/* list merge is not possible, try row by row mode */
		for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
		  {
		    ACCESS_SPEC_UNSET_FLAG (specp, ACCESS_SPEC_FLAG_MERGEABLE_LIST);
		  }
	      }
	  }
      }

  }

  void process_xasl_node_recursive_force_cannot_parallel (XASL_NODE *arg)
  {
    if (!arg)
      {
	return;
      }

    // Check if this XASL_NODE is already being processed to prevent infinite recursion
    if (xasl_processing_set.find (arg) != xasl_processing_set.end ())
      {
	return;
      }
    xasl_processing_set.insert (arg);

    // Mark all access specs as cannot parallel
    for (ACCESS_SPEC_TYPE *specp = arg->spec_list; specp; specp = specp->next)
      {
	ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_NO_PARALLEL_HEAP_SCAN);
      }

    // Recursively process all child nodes
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
    for (XASL_NODE *xaslp = arg->scan_ptr; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }
    for (XASL_NODE *xaslp = arg->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	process_xasl_node_recursive_force_cannot_parallel (xaslp);
      }

    // Process special node types
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
      default:
	break;
      }
  }
}

extern int
scan_check_parallel_heap_scan_possible (XASL_NODE *xasl)
{
  // Clear caches to start fresh for each top-level check
  parallel_heap_scan::xasl_check_cache.clear ();
  parallel_heap_scan::xasl_processing_set.clear ();

  parallel_heap_scan::process_xasl_node_recursive (xasl);

  // Clear caches after processing to free memory
  parallel_heap_scan::xasl_check_cache.clear ();
  parallel_heap_scan::xasl_processing_set.clear ();

  return NO_ERROR;
}
