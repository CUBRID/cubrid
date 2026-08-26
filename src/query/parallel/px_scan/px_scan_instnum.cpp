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
 * px_scan_instnum.cpp - ROWNUM (inst_num) under parallel scan.
 *
 * Two halves, split by build mode rather than by file: eligibility runs while the XASL is built
 * (client), numbering runs while the scan executes (server). The server half reaches into the
 * query engine (qfile_*, fetch_peek_dbval), which the client library does not link.
 */

#include "px_scan_instnum.hpp"

#include "regu_var.hpp"
#include "xasl.h"

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "dbtype.h"
#include "error_manager.h"
#include "fetch.h"
#include "list_file.h"
#include "object_domain.h"
#include "object_primitive.h"
#endif /* SERVER_MODE || SA_MODE */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
/* wf119: unguarded — client half now compiled into server */
  /* returns true if any TYPE_CONSTANT regu var in this subtree points at iv (the inst_num() value). */
  static bool regu_subtree_refs_instnum (REGU_VARIABLE *r, DB_VALUE *iv);
  static bool pred_refs_instnum (PRED_EXPR *p, DB_VALUE *iv);

  static bool
  regu_var_list_refs_instnum (struct regu_variable_list_node *list, DB_VALUE *iv)
  {
    for (struct regu_variable_list_node *n = list; n != nullptr; n = n->next)
      {
	if (regu_subtree_refs_instnum (&n->value, iv))
	  {
	    return true;
	  }
      }
    return false;
  }

  static bool
  regu_subtree_refs_instnum (REGU_VARIABLE *r, DB_VALUE *iv)
  {
    if (r == nullptr)
      {
	return false;
      }
    switch (r->type)
      {
      case TYPE_CONSTANT:
	return r->value.dbvalptr == iv;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
      {
	ARITH_TYPE *a = r->value.arithptr;
	if (a == nullptr)
	  {
	    return false;
	  }
	/* T_CASE/T_DECODE/T_IF/T_PREDICATE keep their condition in a->pred, not in the operands. */
	return regu_subtree_refs_instnum (a->leftptr, iv) || regu_subtree_refs_instnum (a->rightptr, iv)
	       || regu_subtree_refs_instnum (a->thirdptr, iv) || pred_refs_instnum (a->pred, iv);
      }
      case TYPE_FUNC:
	return regu_var_list_refs_instnum (r->value.funcp->operand, iv);
      case TYPE_SP:
	return regu_var_list_refs_instnum (r->value.sp_ptr->args, iv);
      case TYPE_REGU_VAR_LIST:
	return regu_var_list_refs_instnum (r->value.regu_var_list, iv);
      case TYPE_ATTR_ID:
      case TYPE_SHARED_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
      case TYPE_OID:
      case TYPE_DBVAL:
      case TYPE_POSITION:
      case TYPE_POS_VALUE:
      case TYPE_LIST_ID:
      case TYPE_ORDERBY_NUM:
      case TYPE_CLASSOID:
      case TYPE_REGUVAL_LIST:
	return false;
      default:
	/* unknown container: conservatively assume it may reference instnum -> block. */
	return true;
      }
  }

  static bool
  pred_refs_instnum (PRED_EXPR *p, DB_VALUE *iv)
  {
    if (p == nullptr)
      {
	return false;
      }
    switch (p->type)
      {
      case T_PRED:
	return pred_refs_instnum (p->pe.m_pred.lhs, iv) || pred_refs_instnum (p->pe.m_pred.rhs, iv);
      case T_NOT_TERM:
	return pred_refs_instnum (p->pe.m_not_term, iv);
      case T_EVAL_TERM:
	switch (p->pe.m_eval_term.et_type)
	  {
	  case T_COMP_EVAL_TERM:
	    return regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_comp.lhs, iv)
		   || regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_comp.rhs, iv);
	  case T_ALSM_EVAL_TERM:
	    return regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_alsm.elem, iv)
		   || regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_alsm.elemset, iv);
	  case T_LIKE_EVAL_TERM:
	    return regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_like.src, iv)
		   || regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_like.pattern, iv)
		   || regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_like.esc_char, iv);
	  case T_RLIKE_EVAL_TERM:
	    return regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_rlike.src, iv)
		   || regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_rlike.pattern, iv)
		   || regu_subtree_refs_instnum (p->pe.m_eval_term.et.et_rlike.case_sensitive, iv);
	  default:
	    return true;	/* unknown term: conservatively block, like the regu default. */
	  }
      default:
	return true;
      }
  }

  /* shared precondition for both instnum fast-path modes: instnum_val appears in the output only as a
   * top-level pass-through column. Sets *passthrough_cnt_out (may be 0). */
  static bool
  is_plain_instnum_buildlist (XASL_NODE *x, int *passthrough_cnt_out)
  {
    if (x == nullptr || x->instnum_val == nullptr || x->save_instnum_val != nullptr)
      {
	return false;
      }
    if (x->ordbynum_pred != nullptr || x->ordbynum_val != nullptr)
      {
	return false;		/* ORDER BY + LIMIT/topn: parallel topn destroys scan-order ROWNUM. */
      }
    if (x->type != BUILDLIST_PROC)
      {
	return false;
      }
    if (x->proc.buildlist.groupby_list != nullptr || x->proc.buildlist.g_agg_list != nullptr
	|| x->proc.buildlist.a_eval_list != nullptr)
      {
	return false;		/* GROUP BY / aggregate / analytic: out of scope. */
      }
    if (x->outptr_list == nullptr)
      {
	return false;
      }
    int passthrough = 0;
    for (struct regu_variable_list_node *v = x->outptr_list->valptrp; v != nullptr; v = v->next)
      {
	REGU_VARIABLE *r = &v->value;
	if (r->type == TYPE_CONSTANT && r->value.dbvalptr == x->instnum_val)
	  {
	    passthrough++;
	    continue;
	  }
	if (regu_subtree_refs_instnum (r, x->instnum_val))
	  {
	    return false;	/* nested use, e.g. ROWNUM + 1. */
	  }
      }
    if (passthrough_cnt_out != nullptr)
      {
	*passthrough_cnt_out = passthrough;
      }
    return true;
  }

  /* inst_num() is a plain pass-through output column that main can renumber at merge. */
  bool
  is_renumberable_instnum (XASL_NODE *x)
  {
    int passthrough = 0;
    if (x == nullptr || x->instnum_pred != nullptr)
      {
	return false;
      }
    return is_plain_instnum_buildlist (x, &passthrough) && passthrough >= 1;
  }

  /* single-term "inst_num() <= ?": workers can draw numbers from a shared counter and stop early. */
  bool
  is_atomic_instnum_eligible (XASL_NODE *x)
  {
    if (get_instnum_upper_limit_rhs (x, nullptr) == nullptr)
      {
	return false;
      }
    return is_plain_instnum_buildlist (x, nullptr);
  }
/* wf119: end of former !SERVER_MODE region */

#if defined (SERVER_MODE) || defined (SA_MODE)
  instnum_mode
  detect_instnum_mode (XASL_NODE *x, std::vector<int> &rownum_col_indices, atomic_instnum &draw)
  {
    instnum_mode mode = instnum_mode::NONE;

    /* Pass-through ROWNUM output columns; index == position in the outptr valptr list. Both modes
     * need them: RENUMBER never numbers them during the scan, and ATOMIC_DRAW numbers them with
     * whatever each worker drew, which is not the position the row lands in after the merge. */
    if (x->instnum_val != nullptr && x->save_instnum_val == nullptr && x->outptr_list != nullptr)
      {
	int idx = 0;
	for (regu_variable_list_node *v = x->outptr_list->valptrp; v != nullptr; v = v->next, idx++)
	  {
	    if (v->value.type == TYPE_CONSTANT && v->value.value.dbvalptr == x->instnum_val)
	      {
		rownum_col_indices.push_back (idx);
	      }
	  }
      }

    /* the limit itself is resolved later, once a VAL_DESCR is available. */
    draw.limit_rhs = get_instnum_upper_limit_rhs (x, &draw.is_less_than);
    if (draw.limit_rhs != nullptr)
      {
	mode = instnum_mode::ATOMIC_DRAW;
	draw.seed (x->list_id != nullptr ? (INT64) x->list_id->tuple_cnt : 0);
      }
    else if (x->instnum_pred == nullptr && !rownum_col_indices.empty ())
      {
	mode = instnum_mode::RENUMBER;
      }
    return mode;
  }

  int
  resolve_instnum_limit (THREAD_ENTRY *thread_p, atomic_instnum &draw, VAL_DESCR *vd)
  {
    DB_VALUE *limit_val = nullptr;
    INT64 raw_limit = 0;

    if (fetch_peek_dbval (thread_p, draw.limit_rhs, vd, NULL, NULL, NULL, &limit_val) != NO_ERROR)
      {
	return ER_FAILED;
      }
    if (limit_val != nullptr && !DB_IS_NULL (limit_val))
      {
	DB_VALUE coerced;
	TP_DOMAIN_STATUS dom_status;

	db_make_null (&coerced);
	dom_status = tp_value_coerce (limit_val, &coerced, &tp_Bigint_domain);
	if (dom_status == DOMAIN_COMPATIBLE)
	  {
	    raw_limit = db_get_bigint (&coerced);
	    if (raw_limit > 0)
	      {
		/* The coercion rounds, so ask the comparison itself - the question serial asks every
		 * row - whether the rounded candidate qualifies, and step down once if it does not.
		 * Rounding lands within 1, so that candidate and its predecessor are the only two. */
		DB_VALUE_COMPARE_RESULT cmp = tp_value_compare (&coerced, limit_val, 1, 0);
		const bool qualifies = draw.is_less_than ? (cmp == DB_LT) : (cmp != DB_GT);
		if (!qualifies)
		  {
		    raw_limit--;
		  }
	      }
	  }
	else if (dom_status == DOMAIN_OVERFLOW && DB_VALUE_DOMAIN_TYPE (limit_val) == DB_TYPE_NUMERIC)
	  {
	    /* serial widens inst_num() rather than coercing the bound, so out of BIGINT range a
	     * positive bound admits every row and a negative one none (cf. key-limit handling). */
	    raw_limit = DB_VALUE_NUMERIC_IS_VALUE_NEGATIVE (limit_val) ? 0 : DB_BIGINT_MAX;
	    er_clear ();
	  }
	else
	  {
	    pr_clear_value (&coerced);
	    (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, limit_val, &tp_Bigint_domain);
	    return ER_FAILED;
	  }
	pr_clear_value (&coerced);
      }
    /* NULL rhs keeps the limit 0: unknown for every row -> no rows, like serial. */
    draw.resolve_limit (raw_limit);
    return NO_ERROR;
  }

  int
  renumber_instnum_lists (THREAD_ENTRY *thread_p, std::vector<QFILE_LIST_ID *> &lists,
			  const std::vector<int> &col_indices, INT64 start_at)
  {
    DB_VALUE rownum_val;
    INT64 counter = start_at;
    for (QFILE_LIST_ID *list_id : lists)
      {
	if (list_id == nullptr || list_id->tuple_cnt <= 0)
	  {
	    continue;
	  }
	QFILE_LIST_SCAN_ID s_id;
	if (qfile_open_list_scan (list_id, &s_id) != NO_ERROR)
	  {
	    return ER_FAILED;
	  }
	QFILE_TUPLE_RECORD tuple_rec = { NULL, 0 };
	SCAN_CODE sc;
	while ((sc = qfile_scan_list_next (thread_p, &s_id, &tuple_rec, PEEK)) == S_SUCCESS)
	  {
	    db_make_bigint (&rownum_val, ++counter);
	    for (int col : col_indices)
	      {
		if (qfile_set_tuple_column_value (thread_p, list_id, s_id.curr_pgptr, &s_id.curr_vpid,
						  tuple_rec.tpl, col, &rownum_val, &tp_Bigint_domain) != NO_ERROR)
		  {
		    qfile_close_scan (thread_p, &s_id);
		    return ER_FAILED;
		  }
	      }
	  }
	qfile_close_scan (thread_p, &s_id);
	if (sc == S_ERROR)
	  {
	    return ER_FAILED;
	  }
      }
    return NO_ERROR;
  }
#endif /* SERVER_MODE || SA_MODE */
}
