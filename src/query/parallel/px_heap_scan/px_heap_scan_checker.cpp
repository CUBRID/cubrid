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
#include "xasl_predicate.hpp"
#include <set>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  class checker
  {
    public:

      int set_impossible (XASL_NODE *xasl);
      int set_impossible_recursively (XASL_NODE *xasl);
      int check (REGU_VARIABLE *src);
      int check (PRED_EXPR *src);
      int check (struct regu_variable_list_node   *src);
      int check (ARITH_TYPE *src);
      int check (PRED *src);
      int check (EVAL_TERM *src);
      int check (COMP_EVAL_TERM *src);
      int check (ALSM_EVAL_TERM *src);
      int check (LIKE_EVAL_TERM *src);
      int check (RLIKE_EVAL_TERM *src);
      int check (ACCESS_SPEC_TYPE *src);
      int check (XASL_NODE *xasl);

    private:
      std::set<void *> visited_ptr;
      std::set<void *> impossible_ptr;
  };

  int checker::set_impossible (XASL_NODE *xasl)
  {
    ACCESS_SPEC_TYPE *specp;
    if (!xasl)
      {
	return 0;
      }

    if (impossible_ptr.find ((void *)xasl) != impossible_ptr.end())
      {
	return 0;
      }
    impossible_ptr.insert ((void *)xasl);

    for (specp = xasl->spec_list; specp; specp = specp->next)
      {
	specp->flags = (ACCESS_SPEC_FLAG) (specp->flags | ACCESS_SPEC_FLAG_NO_PARALLEL_HEAP_SCAN);
      }
    for (specp = xasl->merge_spec; specp; specp = specp->next)
      {
	specp->flags = (ACCESS_SPEC_FLAG) (specp->flags | ACCESS_SPEC_FLAG_NO_PARALLEL_HEAP_SCAN);
      }
    for (XASL_NODE *xaslp = xasl->scan_ptr; xaslp; xaslp = xaslp->next)
      {
	set_impossible_recursively (xaslp);
      }
    if (xasl->type == CTE_PROC)
      {
	set_impossible (xasl->proc.cte.non_recursive_part);
	set_impossible (xasl->proc.cte.recursive_part);
      }
    return 0;
  }

  int checker::set_impossible_recursively (XASL_NODE *xasl)
  {
    int cnt = 0;
    XASL_NODE *xaslp;
    if (!xasl)
      {
	return 0;
      }

    if (impossible_ptr.find ((void *)xasl) != impossible_ptr.end())
      {
	return 0;
      }

    set_impossible (xasl);
    for (xaslp = xasl->aptr_list; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
      }

    for (xaslp = xasl->bptr_list; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
	cnt++;
      }

    for (xaslp = xasl->dptr_list; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
      }

    for (xaslp = xasl->fptr_list; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
	cnt++;
      }

    for (xaslp = xasl->scan_ptr; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
      }

    for (xaslp = xasl->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
	cnt++;
      }

    if (xasl->type == CTE_PROC)
      {
	cnt += set_impossible_recursively (xasl->proc.cte.non_recursive_part);
	cnt += set_impossible_recursively (xasl->proc.cte.recursive_part);
      }

    return cnt;
  }


  int checker::check (XASL_NODE *xasl)
  {
    int cnt = 0;
    XASL_NODE *xaslp;
    ACCESS_SPEC_TYPE *specp;
    if (!xasl)
      {
	return 0;
      }
    auto it = visited_ptr.find ((void *)xasl);
    if (it != visited_ptr.end())
      {
	return 0;
      }
    visited_ptr.insert ((void *)xasl);

    switch (xasl->type)
      {
      case BUILDLIST_PROC:
      case BUILDVALUE_PROC:
	break;
      case CTE_PROC:
	if (xasl->proc.cte.non_recursive_part)
	  {
	    cnt += check (xasl->proc.cte.non_recursive_part);
	  }
	if (xasl->proc.cte.recursive_part)
	  {
	    cnt += set_impossible_recursively (xasl->proc.cte.recursive_part);
	  }
	break;
      case HASHJOIN_PROC:
	if (xasl->proc.hashjoin.outer.xasl)
	  {
	    cnt += check (xasl->proc.hashjoin.outer.xasl);
	  }
	if (xasl->proc.hashjoin.inner.xasl)
	  {
	    cnt += check (xasl->proc.hashjoin.inner.xasl);
	  }
	break;
      case UNION_PROC:
      case DIFFERENCE_PROC:
      case INTERSECTION_PROC:
      case OBJFETCH_PROC:
      case MERGELIST_PROC:
      case UPDATE_PROC:
      case DELETE_PROC:
      case INSERT_PROC:
      case CONNECTBY_PROC:
      case DO_PROC:
      case MERGE_PROC:
      case BUILD_SCHEMA_PROC:
      case SCAN_PROC:
      default:
	set_impossible_recursively (xasl);
	return 0;
	break;
      }
    if (xasl->selected_upd_list)
      {
	set_impossible_recursively (xasl);
      }
    /* lower xasl search */
    /* aptr : can parallel heap scan */
    for (xaslp = xasl->aptr_list; xaslp; xaslp = xaslp->next)
      {
	check (xaslp);
      }
    /* bptr : cannot parallel heap scan */
    for (xaslp = xasl->bptr_list; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
	cnt++;
      }
    /* dptr : cannot parallel heap scan */
    for (xaslp = xasl->dptr_list; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
      }
    /* fptr : cannot parallel heap scan */
    for (xaslp = xasl->fptr_list; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
	cnt++;
      }
    /* scan_ptr : cannot parallel heap scan */
    for (xaslp = xasl->scan_ptr; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
      }
    /* connect_by_ptr : cannot parallel heap scan */
    for (xaslp = xasl->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	cnt += set_impossible_recursively (xaslp);
	cnt++;
      }

    /* this xasl's spec list search */
    for (specp = xasl->spec_list; specp; specp = specp->next)
      {
	cnt += check (specp);
      }
    for (specp = xasl->merge_spec; specp; specp = specp->next)
      {
	cnt += check (specp);
      }
    if (cnt > 0)
      {
	set_impossible (xasl);
      }

    return cnt;
  }

  int checker::check (ACCESS_SPEC_TYPE *src)
  {
    int cnt = 0;

    if (!src)
      {
	return 0;
      }
    if (src->access != ACCESS_METHOD_SEQUENTIAL)
      {
	cnt++;
	return cnt;
      }
    if (src->type != TARGET_CLASS)
      {
	cnt++;
	return cnt;
      }
    cnt += check (src->s.cls_node.cls_regu_list_pred);
    cnt += check (src->s.cls_node.cls_regu_list_rest);
    cnt += check (src->where_pred);
    if (src->next) /* not for 'select c1 from (t1 t2)' */
      {
	cnt++;
      }


    return cnt;
  }

  int checker::check (REGU_VARIABLE *src)
  {
    int cnt = 0;
    if (!src)
      {
	return 0;
      }
    /* cannot execute regu-linked xasl */
    if (src->xasl)
      {
	cnt++;
	set_impossible_recursively (src->xasl);
      }

    switch (src->type)
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
	/* can execute with constants */
	break;
      case TYPE_ORDERBY_NUM:
      case TYPE_LIST_ID:
      case TYPE_CLASSOID:
      case TYPE_REGUVAL_LIST:
	/* cannot execute with this regu-variable */
	cnt++;
	break;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
	cnt += check (src->value.arithptr);
	break;
      case TYPE_SP:
	cnt += check (src->value.sp_ptr->args);
	/* cannot execute sp in child threads */
	cnt++;
	break;
      case TYPE_FUNC:
	cnt += check (src->value.funcp->operand);
	break;
      case TYPE_REGU_VAR_LIST:
	cnt += check (src->value.regu_var_list);
	break;
      default:
	cnt++;
	break;
      }
    return cnt;
  }

  int checker::check (ARITH_TYPE *src)
  {
    if (!src)
      {
	return 0;
      }
    int cnt = 0;
    cnt += check (src->leftptr);
    cnt += check (src->rightptr);
    cnt += check (src->thirdptr);
    return cnt;
  }

  int checker::check (PRED_EXPR *src)
  {
    if (!src)
      {
	return 0;
      }

    switch (src->type)
      {
      case T_PRED:
	return check (&src->pe.m_pred);
	break;
      case T_EVAL_TERM:
	return check (&src->pe.m_eval_term);
	break;
      case T_NOT_TERM:
	return check (src->pe.m_not_term);
	break;
      default:
	return 0;
	break;
      }
  }
  int checker::check (PRED *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->lhs) + check (src->rhs);
  }

  int checker::check (EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }

    switch (src->et_type)
      {
      case T_COMP_EVAL_TERM:
	return check (&src->et.et_comp);
	break;
      case T_ALSM_EVAL_TERM:
	return check (&src->et.et_alsm);
	break;
      case T_LIKE_EVAL_TERM:
	return check (&src->et.et_like);
	break;
      case T_RLIKE_EVAL_TERM:
	return check (&src->et.et_rlike);
	break;
      default:
	return 0;
      }
  }

  int checker::check (COMP_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->lhs) + check (src->rhs);
  }

  int checker::check (ALSM_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->elem) + check (src->elemset);
  }

  int checker::check (LIKE_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->src) + check (src->pattern) + check (src->esc_char);
  }

  int checker::check (RLIKE_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->src) + check (src->pattern) + check (src->case_sensitive);
  }

  int checker::check (struct regu_variable_list_node   *src)
  {
    if (!src)
      {
	return 0;
      }
    int cnt = 0;
    struct regu_variable_list_node   *curr = src;
    while (curr)
      {
	cnt += check (&curr->value);
	curr = curr->next;
      }
    return cnt;
  }

}

extern int
scan_check_parallel_heap_scan_possible (XASL_NODE *xasl)
{
  parallel_heap_scan::checker checker;
  return checker.check (xasl);
}
