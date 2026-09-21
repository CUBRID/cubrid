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

#include "px_query_checker.hpp"
#include "px_sp_eligibility.hpp"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"
#include <set>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  class xasl_checker
  {
    public:
      xasl_checker()=default;
      ~xasl_checker()=default;
      bool is_parallel_executable (XASL_NODE *xasl);
      void mark_sp_blocks (XASL_NODE *xasl);
    private:
      void add_xasl_recursive (XASL_NODE *xasl);
      void check_xasl_recursive (XASL_NODE *xasl);
      bool mark_sp_blocks_recursive (XASL_NODE *xasl, bool is_root);
      void check_regu_var (REGU_VARIABLE *regu_var);
      void check_pred_expr (PRED_EXPR *pred_expr);
      void check_pred (PRED *pred);
      void check_eval_term (EVAL_TERM *eval_term);
      void check_comp_eval_term (COMP_EVAL_TERM *comp_eval_term);
      void check_alsm_eval_term (ALSM_EVAL_TERM *alsm_eval_term);
      void check_like_eval_term (LIKE_EVAL_TERM *like_eval_term);
      void check_rlike_eval_term (RLIKE_EVAL_TERM *rlike_eval_term);
      void check_regu_var_list (REGU_VARIABLE_LIST regu_var_list);
      void check_analytic_eval_list (ANALYTIC_EVAL_TYPE *a_eval_list);
      void check_agg_list (AGGREGATE_TYPE *agg_list);
      void check_arith_list (ARITH_TYPE *arith_list);
      void check_xasl_node (XASL_NODE *xasl, XASL_NODE *owner);
      void check_access_spec_type (ACCESS_SPEC_TYPE *access_spec_type);
      std::set<XASL_NODE *> get_child_xasl_set_recursive (XASL_NODE *xasl);
      std::multimap<XASL_NODE *, XASL_NODE *> m_xasl_map;
      std::multimap<XASL_NODE *, XASL_NODE *> m_list_scan_map;
      std::set<XASL_NODE *> m_aptr_head_set;
      std::set<XASL_NODE *> m_aptr_set;
      /* SP/method occurrences are not a statement-wide disqualifier: they are collected
       * per owning (dispatchable) xasl node and later marked locally with
       * XASL_NO_PARALLEL_SUBQUERY on that node, so sibling blocks keep parallelism. */
      std::set<XASL_NODE *> m_sp_dirty_set;
      XASL_NODE *m_owner=nullptr;
      bool m_is_parallel_executable=true;
  };

  std::set<XASL_NODE *> xasl_checker::get_child_xasl_set_recursive (XASL_NODE *xasl)
  {
    std::set<XASL_NODE *> child_xasl_set;
    auto child_set = m_xasl_map.equal_range (xasl);
    for (auto it = child_set.first; it != child_set.second; it++)
      {
	child_xasl_set.insert (it->second);
	auto child_child_set = get_child_xasl_set_recursive (it->second);
	child_xasl_set.insert (child_child_set.begin(), child_child_set.end());
      }
    return child_xasl_set;
  }

  void xasl_checker::check_xasl_recursive (XASL_NODE *xasl)
  {
    std::size_t i,j;
    for (XASL_NODE *aptr_head_xasl: m_aptr_head_set)
      {
	std::vector<std::set<XASL_NODE *>> aptr_set_vector;
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : aptr head : %p", aptr_head_xasl);
#endif
	for (XASL_NODE *scan_ptr = aptr_head_xasl; scan_ptr != nullptr; scan_ptr= scan_ptr->scan_ptr)
	  {
	    for (XASL_NODE *aptr = scan_ptr->aptr_list; aptr != nullptr; aptr = aptr->next)
	      {
		auto child_set = get_child_xasl_set_recursive (aptr);
		child_set.insert (aptr);
		aptr_set_vector.push_back (child_set);
	      }
	  }
	if (aptr_set_vector.size() > 1)
	  {
	    for (i=0; i<aptr_set_vector.size(); i++)
	      {
		auto src_set = aptr_set_vector[i];
		for (j=0; j<aptr_set_vector.size(); j++)
		  {
		    if (i==j)
		      {
			continue;
		      }
		    else
		      {
			auto dst_set = aptr_set_vector[j];
			for (auto aptr: src_set)
			  {
#if WITH_PARALLEL_DETAIL_INFO
			    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : src_child_set[%d] : %p", i, aptr);
#endif
			    auto list_scan_dst = m_list_scan_map.equal_range (aptr);
			    for (auto it = list_scan_dst.first; it != list_scan_dst.second; it++)
			      {
#if WITH_PARALLEL_DETAIL_INFO
				_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : list_scan : %p -> %p", aptr, it->second);
#endif
				if (dst_set.find (it->second) != dst_set.end())
				  {
#if WITH_PARALLEL_DETAIL_INFO
				    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : non-parallelable ref : %p -> %p", aptr,
						   it->second);
#endif
				    m_is_parallel_executable = false;
				    return;
				  }
			      }
			  }
		      }
		  }
	      }
	  }
      }
  }

  void xasl_checker::check_regu_var (REGU_VARIABLE *regu_var)
  {
    if (!regu_var)
      {
	return;
      }
    switch (regu_var->type)
      {
      case TYPE_DBVAL:
      case TYPE_CONSTANT:
      case TYPE_ORDERBY_NUM:
      case TYPE_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
      case TYPE_SHARED_ATTR_ID:
      case TYPE_POSITION:
      case TYPE_LIST_ID:
      case TYPE_POS_VALUE:
      case TYPE_OID:
      case TYPE_CLASSOID:
      case TYPE_REGUVAL_LIST:
	/* can execute parallel */
	break;
      case TYPE_REGU_VAR_LIST:
	check_regu_var_list (regu_var->value.regu_var_list);
	break;
      case TYPE_FUNC:
	check_regu_var_list (regu_var->value.funcp->operand);
	break;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
	if (regu_var->value.arithptr->opcode == T_EVALUATE_VARIABLE || regu_var->value.arithptr->opcode == T_DEFINE_VARIABLE)
	  {
	    m_is_parallel_executable = false;
	  }
	check_regu_var (regu_var->value.arithptr->leftptr);
	check_regu_var (regu_var->value.arithptr->rightptr);
	check_regu_var (regu_var->value.arithptr->thirdptr);
	break;
      case TYPE_SP:
	if (px_sp_is_parallel_eligible (regu_var->value.sp_ptr->sig))
	  {
	    /* declared PARALLEL_ENABLE: the SP may run inside a px worker, so it does not dirty
	     * its owning block. Its arguments are evaluated in the worker too, so they are
	     * still checked. */
	    check_regu_var_list (regu_var->value.sp_ptr->args);
	  }
	/* ineligible: exclude only the owning block from parallel execution, not the whole
	 * statement (a block mixing eligible and ineligible SPs stays blocked — conservative AND) */
	else if (m_owner)
	  {
	    m_sp_dirty_set.insert (m_owner);
	  }
	else
	  {
	    assert (0);
	    m_is_parallel_executable = false;
	  }
	break;
      default:
	assert (0);
	m_is_parallel_executable = false;
	break;
      }
  }

  void xasl_checker::check_pred_expr (PRED_EXPR *pred_expr)
  {
    if (!pred_expr)
      {
	return;
      }
    switch (pred_expr->type)
      {
      case T_PRED:
	check_pred (&pred_expr->pe.m_pred);
	break;
      case T_EVAL_TERM:
	check_eval_term (&pred_expr->pe.m_eval_term);
	break;
      case T_NOT_TERM:
	check_pred_expr (pred_expr->pe.m_not_term);
	break;
      default:
	assert (0);
	break;
      }
  }

  void xasl_checker::check_pred (PRED *pred)
  {
    if (!pred)
      {
	return;
      }
    check_pred_expr (pred->lhs);
    check_pred_expr (pred->rhs);
  }

  void xasl_checker::check_eval_term (EVAL_TERM *eval_term)
  {
    if (!eval_term)
      {
	return;
      }
    switch (eval_term->et_type)
      {
      case T_COMP_EVAL_TERM:
	check_comp_eval_term (&eval_term->et.et_comp);
	break;
      case T_ALSM_EVAL_TERM:
	check_alsm_eval_term (&eval_term->et.et_alsm);
	break;
      case T_LIKE_EVAL_TERM:
	check_like_eval_term (&eval_term->et.et_like);
	break;
      case T_RLIKE_EVAL_TERM:
	check_rlike_eval_term (&eval_term->et.et_rlike);
	break;
      default:
	assert (0);
	break;
      }
  }

  void xasl_checker::check_comp_eval_term (COMP_EVAL_TERM *comp_eval_term)
  {
    if (!comp_eval_term)
      {
	return;
      }
    check_regu_var (comp_eval_term->lhs);
    check_regu_var (comp_eval_term->rhs);
  }

  void xasl_checker::check_alsm_eval_term (ALSM_EVAL_TERM *alsm_eval_term)
  {
    if (!alsm_eval_term)
      {
	return;
      }
    check_regu_var (alsm_eval_term->elem);
    check_regu_var (alsm_eval_term->elemset);
  }

  void xasl_checker::check_like_eval_term (LIKE_EVAL_TERM *like_eval_term)
  {
    if (!like_eval_term)
      {
	return;
      }
    check_regu_var (like_eval_term->src);
    check_regu_var (like_eval_term->pattern);
    check_regu_var (like_eval_term->esc_char);
  }

  void xasl_checker::check_rlike_eval_term (RLIKE_EVAL_TERM *rlike_eval_term)
  {
    if (!rlike_eval_term)
      {
	return;
      }
    check_regu_var (rlike_eval_term->src);
    check_regu_var (rlike_eval_term->pattern);
    check_regu_var (rlike_eval_term->case_sensitive);
  }

  void xasl_checker::check_regu_var_list (REGU_VARIABLE_LIST regu_var_list)
  {
    REGU_VARIABLE_LIST regu_var_list_p;
    for (regu_var_list_p = regu_var_list; regu_var_list_p != nullptr; regu_var_list_p = regu_var_list_p->next)
      {
	check_regu_var (&regu_var_list_p->value);
      }
  }

  void xasl_checker::check_analytic_eval_list (ANALYTIC_EVAL_TYPE *a_eval_list)
  {
    for (ANALYTIC_EVAL_TYPE *eval = a_eval_list; eval != nullptr; eval = eval->next)
      {
	for (ANALYTIC_TYPE *node = eval->head; node != nullptr; node = node->next)
	  {
	    check_regu_var (&node->operand);
	    if (node->function == PT_PERCENTILE_CONT || node->function == PT_PERCENTILE_DISC)
	      {
		check_regu_var (node->info.percentile.percentile_reguvar);
	      }
	  }
      }
  }

  void xasl_checker::check_agg_list (AGGREGATE_TYPE *agg_list)
  {
    for (AGGREGATE_TYPE *agg = agg_list; agg != nullptr; agg = agg->next)
      {
	check_regu_var_list (agg->operands);
	if (agg->function == PT_PERCENTILE_CONT || agg->function == PT_PERCENTILE_DISC)
	  {
	    check_regu_var (agg->info.percentile.percentile_reguvar);
	  }
      }
  }

  void xasl_checker::check_arith_list (ARITH_TYPE *arith_list)
  {
    /* outarith_list is a single node (see qexec_clear_arith_list) */
    if (arith_list)
      {
	check_regu_var (arith_list->leftptr);
	check_regu_var (arith_list->rightptr);
	check_regu_var (arith_list->thirdptr);
	check_pred_expr (arith_list->pred);
      }
  }

  void xasl_checker::check_xasl_node (XASL_NODE *xasl, XASL_NODE *owner)
  {
    if (!xasl)
      {
	return;
      }
    /* owner: the dispatchable job node the checked content belongs to; SP/method
     * occurrences found below are attributed to it (see m_sp_dirty_set) */
    m_owner = owner;
    ACCESS_SPEC_TYPE *spec_list;
    check_pred_expr (xasl->ordbynum_pred);
    check_regu_var (xasl->orderby_limit);
    if (xasl->outptr_list)
      {
	check_regu_var_list (xasl->outptr_list->valptrp);
      }
    for (spec_list = xasl->spec_list; spec_list != nullptr; spec_list = spec_list->next)
      {
	check_access_spec_type (spec_list);
      }
    for (spec_list = xasl->merge_spec; spec_list != nullptr; spec_list = spec_list->next)
      {
	check_access_spec_type (spec_list);
      }
    check_pred_expr (xasl->during_join_pred);
    check_pred_expr (xasl->after_join_pred);
    check_pred_expr (xasl->if_pred);
    check_pred_expr (xasl->instnum_pred);
    check_regu_var (xasl->limit_offset);
    check_regu_var (xasl->limit_row_count);
    if (xasl->type == BUILDLIST_PROC)
      {
	if (xasl->proc.buildlist.a_outptr_list)
	  {
	    check_regu_var_list (xasl->proc.buildlist.a_outptr_list->valptrp);
	  }
	if (xasl->proc.buildlist.a_outptr_list_ex)
	  {
	    check_regu_var_list (xasl->proc.buildlist.a_outptr_list_ex->valptrp);
	  }
	if (xasl->proc.buildlist.a_outptr_list_interm)
	  {
	    check_regu_var_list (xasl->proc.buildlist.a_outptr_list_interm->valptrp);
	  }
	if (xasl->proc.buildlist.g_outptr_list)
	  {
	    check_regu_var_list (xasl->proc.buildlist.g_outptr_list->valptrp);
	  }
	check_regu_var_list (xasl->proc.buildlist.a_regu_list);
	check_regu_var_list (xasl->proc.buildlist.a_scan_regu_list);
	check_regu_var_list (xasl->proc.buildlist.g_regu_list);
	check_regu_var_list (xasl->proc.buildlist.g_hk_scan_regu_list);
	check_regu_var_list (xasl->proc.buildlist.g_hk_sort_regu_list);
	check_regu_var_list (xasl->proc.buildlist.g_scan_regu_list);
	check_pred_expr (xasl->proc.buildlist.g_having_pred);
	check_pred_expr (xasl->proc.buildlist.g_grbynum_pred);
	check_agg_list (xasl->proc.buildlist.g_agg_list);
	check_analytic_eval_list (xasl->proc.buildlist.a_eval_list);
      }
    else if (xasl->type == BUILDVALUE_PROC)
      {
	/* a buildvalue aggregate evaluates its argument itself (pt_to_aggregate_node stores the
	 * argument regu as the operand, e.g. SUM (sp (c1)) carries a TYPE_SP operand), so an SP
	 * hidden here would otherwise be dispatched to a px worker unseen */
	check_agg_list (xasl->proc.buildvalue.agg_list);
	check_arith_list (xasl->proc.buildvalue.outarith_list);
	check_pred_expr (xasl->proc.buildvalue.having_pred);
      }
  }

  void xasl_checker::check_access_spec_type (ACCESS_SPEC_TYPE *access_spec_type)
  {
    if (!access_spec_type)
      {
	return;
      }
    check_pred_expr (access_spec_type->where_key);
    check_pred_expr (access_spec_type->where_pred);
    check_pred_expr (access_spec_type->where_range);
    switch (access_spec_type->type)
      {
      case TARGET_LIST:
      {
	check_regu_var_list (access_spec_type->s.list_node.list_regu_list_pred);
	check_regu_var_list (access_spec_type->s.list_node.list_regu_list_rest);
	check_regu_var_list (access_spec_type->s.list_node.list_regu_list_build);
	check_regu_var_list (access_spec_type->s.list_node.list_regu_list_probe);
      }
      break;
      case TARGET_CLASS:
      {
	check_regu_var_list (access_spec_type->s.cls_node.cls_regu_list_key);
	check_regu_var_list (access_spec_type->s.cls_node.cls_regu_list_pred);
	check_regu_var_list (access_spec_type->s.cls_node.cls_regu_list_range);
	check_regu_var_list (access_spec_type->s.cls_node.cls_regu_list_rest);
      }
      break;
      case TARGET_METHOD:
	/* exclude only the owning block from parallel execution, not the whole statement */
	if (m_owner)
	  {
	    m_sp_dirty_set.insert (m_owner);
	  }
	else
	  {
	    assert (0);
	    m_is_parallel_executable = false;
	  }
	break;
      case TARGET_CLASS_ATTR:
      case TARGET_DBLINK:
      case TARGET_REGUVAL_LIST:
      case TARGET_SET:
      case TARGET_SHOWSTMT:
      default:
	m_is_parallel_executable = false;
	break;
      }
    switch (access_spec_type->access)
      {
      case ACCESS_METHOD_SEQUENTIAL:
      case ACCESS_METHOD_INDEX:
	break;
      case ACCESS_METHOD_JSON_TABLE:
      case ACCESS_METHOD_SCHEMA:
      case ACCESS_METHOD_SEQUENTIAL_RECORD_INFO:
      case ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN:
      case ACCESS_METHOD_INDEX_KEY_INFO:
      case ACCESS_METHOD_INDEX_NODE_INFO:
      default:
	m_is_parallel_executable = false;
	break;
      }
  }
  void xasl_checker::add_xasl_recursive (XASL_NODE *xasl)
  {
    if (!xasl)
      {
	return;
      }

    if (xasl->scan_op_type != S_SELECT)
      {
	m_is_parallel_executable = false;
	return;
      }

    if (xasl->upd_del_class_cnt > 0)
      {
	m_is_parallel_executable = false;
	return;
      }

    if (xasl->type == HASHJOIN_PROC || xasl->type == MERGELIST_PROC)
      {
	for (XASL_NODE *aptr = xasl->aptr_list; aptr != nullptr; aptr = aptr->next)
	  {
	    XASL_SET_FLAG (aptr, XASL_ZERO_CORR_LEVEL);
	  }
      }
    else
      {
	if (!XASL_IS_FLAGED (xasl, XASL_ZERO_CORR_LEVEL))
	  {
	    m_is_parallel_executable = false;
	    return;
	  }
      }

    for (XASL_NODE *aptr = xasl->aptr_list; aptr != nullptr; aptr = aptr->next)
      {
	add_xasl_recursive (aptr);
	m_xasl_map.insert (std::make_pair (xasl, aptr));
	m_aptr_head_set.insert (xasl);
	m_aptr_set.insert (aptr);
	if (aptr->type == HASHJOIN_PROC || aptr->type == MERGELIST_PROC)
	  {
	    for (XASL_NODE *aptr2 = aptr->aptr_list; aptr2 != nullptr; aptr2 = aptr2->next)
	      {
		XASL_SET_FLAG (aptr2, XASL_ZERO_CORR_LEVEL);
	      }
	  }
	else
	  {
	    if (!XASL_IS_FLAGED (aptr, XASL_ZERO_CORR_LEVEL))
	      {
		m_is_parallel_executable = false;
		return;
	      }
	  }
	check_xasl_node (aptr, aptr);
	if (aptr->spec_list && aptr->spec_list->type == TARGET_LIST)
	  {
	    m_list_scan_map.insert (std::make_pair (aptr, aptr->spec_list->s.list_node.xasl_node));
	  }
	for (XASL_NODE *aptr_scan_ptr = aptr->scan_ptr; aptr_scan_ptr != nullptr; aptr_scan_ptr = aptr_scan_ptr->scan_ptr)
	  {
	    check_xasl_node (aptr_scan_ptr, aptr);
	    if (aptr_scan_ptr->spec_list && aptr_scan_ptr->spec_list->type == TARGET_LIST)
	      {
		m_list_scan_map.insert (std::make_pair (aptr, aptr_scan_ptr->spec_list->s.list_node.xasl_node));
	      }
	  }
      }
    for (XASL_NODE *scan_ptr = xasl->scan_ptr; scan_ptr != nullptr; scan_ptr = scan_ptr->scan_ptr)
      {
	check_xasl_node (scan_ptr, xasl);
	if (scan_ptr ->spec_list && scan_ptr ->spec_list->type == TARGET_LIST)
	  {
	    m_list_scan_map.insert (std::make_pair (xasl, scan_ptr->spec_list->s.list_node.xasl_node));
	  }
	for (XASL_NODE *aptr = scan_ptr->aptr_list; aptr != nullptr; aptr = aptr->next)
	  {
	    m_xasl_map.insert (std::make_pair (xasl, aptr));
	    m_aptr_head_set.insert (xasl);
	    m_aptr_set.insert (aptr);
	    if (aptr->type == HASHJOIN_PROC || aptr->type == MERGELIST_PROC)
	      {
		for (XASL_NODE *aptr2 = aptr->aptr_list; aptr2 != nullptr; aptr2 = aptr2->next)
		  {
		    XASL_SET_FLAG (aptr2, XASL_ZERO_CORR_LEVEL);
		  }
	      }
	    else
	      {
		if (!XASL_IS_FLAGED (aptr, XASL_ZERO_CORR_LEVEL))
		  {
		    m_is_parallel_executable = false;
		    return;
		  }
	      }
	    check_xasl_node (aptr, aptr);
	    if (aptr->spec_list && aptr->spec_list->type == TARGET_LIST)
	      {
		m_list_scan_map.insert (std::make_pair (aptr, aptr->spec_list->s.list_node.xasl_node));
	      }
	    for (XASL_NODE *aptr_scan_ptr = aptr->scan_ptr; aptr_scan_ptr != nullptr; aptr_scan_ptr = aptr_scan_ptr->scan_ptr)
	      {
		check_xasl_node (aptr_scan_ptr, aptr);
		if (aptr_scan_ptr->spec_list && aptr_scan_ptr->spec_list->type == TARGET_LIST)
		  {
		    m_list_scan_map.insert (std::make_pair (aptr, aptr_scan_ptr->spec_list->s.list_node.xasl_node));
		  }
	      }
	  }
      }
    for (XASL_NODE *dptr = xasl->dptr_list; dptr != nullptr; dptr = dptr->next)
      {
	add_xasl_recursive (dptr );
	m_xasl_map.insert (std::make_pair (xasl, dptr));
      }
    for (XASL_NODE *fptr = xasl->fptr_list; fptr != nullptr; fptr = fptr->next)
      {
	add_xasl_recursive (fptr );
	m_xasl_map.insert (std::make_pair (xasl, fptr));
      }

    for (XASL_NODE *connect_by_ptr = xasl->connect_by_ptr; connect_by_ptr != nullptr; connect_by_ptr = connect_by_ptr->next)
      {
	add_xasl_recursive (connect_by_ptr );
	m_xasl_map.insert (std::make_pair (xasl, connect_by_ptr));
      }

    if (xasl->type == CTE_PROC)
      {
	if (xasl->proc.cte.non_recursive_part)
	  {
	    add_xasl_recursive (xasl->proc.cte.non_recursive_part );
	    m_xasl_map.insert (std::make_pair (xasl, xasl->proc.cte.non_recursive_part));
	  }
	if (xasl->proc.cte.recursive_part)
	  {
	    m_is_parallel_executable = false;
	  }
      }
    else if (xasl->type == BUILDLIST_PROC)
      {
	for (XASL_NODE *eptr = xasl->proc.buildlist.eptr_list; eptr != nullptr; eptr = eptr->next)
	  {
	    add_xasl_recursive (eptr );
	    m_xasl_map.insert (std::make_pair (xasl, eptr));
	  }
      }
    check_xasl_node (xasl, xasl);

    if (xasl->spec_list)
      {
	if (xasl->spec_list->type == TARGET_LIST)
	  {
	    m_list_scan_map.insert (std::make_pair (xasl, xasl->spec_list->s.list_node.xasl_node));
	  }
      }
    if (xasl->merge_spec)
      {
	if (xasl->merge_spec->type == TARGET_LIST)
	  {
	    m_list_scan_map.insert (std::make_pair (xasl, xasl->merge_spec->s.list_node.xasl_node));
	  }
      }
  }

  /* propagate SP/method dirt bottom-up: a node whose subtree contains an SP call must
   * not be dispatched to a px worker (the SP would run in worker context), so it gets
   * XASL_NO_PARALLEL_SUBQUERY locally. The root is exempt -- it always executes on the
   * main thread, and flagging it would disable the whole statement (the previous
   * statement-global behavior this change removes). */
  bool xasl_checker::mark_sp_blocks_recursive (XASL_NODE *xasl, bool is_root)
  {
    bool dirty = (m_sp_dirty_set.find (xasl) != m_sp_dirty_set.end ());
    auto child_set = m_xasl_map.equal_range (xasl);
    for (auto it = child_set.first; it != child_set.second; it++)
      {
	dirty |= mark_sp_blocks_recursive (it->second, false);
      }
    if (dirty && !is_root)
      {
	XASL_SET_FLAG (xasl, XASL_NO_PARALLEL_SUBQUERY);
      }
    return dirty;
  }

  void xasl_checker::mark_sp_blocks (XASL_NODE *xasl)
  {
    if (xasl)
      {
	mark_sp_blocks_recursive (xasl, true);
      }
  }

  bool xasl_checker::is_parallel_executable (XASL_NODE *xasl)
  {
    try
      {
	add_xasl_recursive (xasl);
	if (!m_is_parallel_executable)
	  {
	    return false;
	  }
	check_xasl_recursive (xasl);
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE,
		       "parallel_detail_info : is_executable : %p, n_aptr: %zu", xasl, m_aptr_head_set.size());
#endif
	if (m_aptr_set.size() < 2)
	  {
	    return false;
	  }
	return m_is_parallel_executable;
      }
    catch (std::exception &e)
      {
	assert_release (false);
	return false;
      }

  }
}

extern int
check_parallel_subquery_possible (XASL_NODE *xasl)
{
  parallel_query_execute::xasl_checker checker;
  if (xasl)
    {
      if (!checker.is_parallel_executable (xasl))
	{
	  XASL_SET_FLAG (xasl, XASL_NO_PARALLEL_SUBQUERY);
	}
      else
	{
	  /* statement stays parallel-eligible; only SP/method-containing blocks are
	   * excluded locally (query_executor.c runs flagged aptrs inline) */
	  checker.mark_sp_blocks (xasl);
	}
    }
  return NO_ERROR;
}
