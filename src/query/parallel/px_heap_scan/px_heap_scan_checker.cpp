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
#include <set>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  enum class CHECK_RESULT
  {
    NONE,
    PARALLEL_LIST_MERGE,
    PARALLEL_PAGE_BY_PAGE,
    CANNOT_PARALLEL
  };

  CHECK_RESULT merge_check_result (CHECK_RESULT a, CHECK_RESULT b)
  {
    return (a == CHECK_RESULT::CANNOT_PARALLEL || b == CHECK_RESULT::CANNOT_PARALLEL) ? CHECK_RESULT::CANNOT_PARALLEL :
	   (a == CHECK_RESULT::PARALLEL_PAGE_BY_PAGE
	    || b == CHECK_RESULT::PARALLEL_PAGE_BY_PAGE) ? CHECK_RESULT::PARALLEL_PAGE_BY_PAGE :
	   (a == CHECK_RESULT::NONE) ? b :
	   (b == CHECK_RESULT::NONE) ? a :
	   CHECK_RESULT::PARALLEL_LIST_MERGE;
  }
  class check_and_set_map
  {
    public:
      check_and_set_map() = default;
      ~check_and_set_map() = default;
      inline bool is_checked (void *ptr)
      {
	return check_map.find (ptr) != check_map.end();
      }
      inline void set_checked (void *ptr)
      {
	check_map.insert (ptr);
      }
      inline bool is_setted_pbp (void *ptr)
      {
	return set_map_pbp.find (ptr) != set_map_pbp.end();
      }
      inline void set_pbp (void *ptr)
      {
	set_map_pbp.insert (ptr);
      }
      inline bool is_setted_lm (void *ptr)
      {
	return set_map_lm.find (ptr) != set_map_lm.end();
      }
      inline void set_lm (void *ptr)
      {
	set_map_lm.insert (ptr);
      }
      inline bool is_setted_cannot_parallel (void *ptr)
      {
	return set_map_cannot_parallel.find (ptr) != set_map_cannot_parallel.end();
      }
      inline void set_cannot_parallel (void *ptr)
      {
	set_map_cannot_parallel.insert (ptr);
      }
    private:
      std::set<void *> check_map;
      std::set<void *> set_map_pbp;
      std::set<void *> set_map_lm;
      std::set<void *> set_map_cannot_parallel;
  };
  class general_checker
  {
    public:
      general_checker (check_and_set_map *map) : map (map) {}
      CHECK_RESULT check (REGU_VARIABLE *src, bool is_outptr_list = false);
      CHECK_RESULT check (PRED_EXPR *src, bool is_outptr_list = false);
      CHECK_RESULT check (REGU_VARIABLE_LIST src, bool is_outptr_list = false);
      CHECK_RESULT check (ARITH_TYPE *src, bool is_outptr_list = false);
      CHECK_RESULT check (PRED *src, bool is_outptr_list = false);
      CHECK_RESULT check (EVAL_TERM *src, bool is_outptr_list = false);
      CHECK_RESULT check (COMP_EVAL_TERM *src, bool is_outptr_list = false);
      CHECK_RESULT check (ALSM_EVAL_TERM *src, bool is_outptr_list = false);
      CHECK_RESULT check (LIKE_EVAL_TERM *src, bool is_outptr_list = false);
      CHECK_RESULT check (RLIKE_EVAL_TERM *src, bool is_outptr_list = false);
    private:
      check_and_set_map *map;
  };

  class spec_checker
  {
    public:
      spec_checker (check_and_set_map *map) : map (map) {}
      CHECK_RESULT check (ACCESS_SPEC_TYPE *spec);
    private:
      check_and_set_map *map;
  };

  class spec_setter
  {
    public:
      spec_setter (check_and_set_map *map) : map (map) {}
      void set (ACCESS_SPEC_TYPE *spec, CHECK_RESULT result);
    private:
      check_and_set_map *map;
  };
  class xasl_checker
  {
    public:
      xasl_checker (check_and_set_map *map) : map (map) {}
      CHECK_RESULT check (XASL_NODE *xasl);
    private:
      check_and_set_map *map;
  };

  class xasl_setter
  {
    public:
      xasl_setter (check_and_set_map *map) : map (map) {}
      void set (XASL_NODE *xasl, CHECK_RESULT result);
      void set_cannot_parallel_recursive (XASL_NODE *xasl);
    private:
      void set_pbp (XASL_NODE *xasl);
      void set_lm (XASL_NODE *xasl);
      void set_cannot_parallel (XASL_NODE *xasl);
      check_and_set_map *map;
  };

  class checker
  {
    public:
      checker() = default;
      ~checker() = default;
      int check (XASL_NODE *xasl);
    private:
      check_and_set_map check_map;
  };

  CHECK_RESULT general_checker::check (REGU_VARIABLE *src, bool is_outptr_list)
  {
    CHECK_RESULT result = CHECK_RESULT::PARALLEL_LIST_MERGE, temp = CHECK_RESULT::NONE;
    DB_TYPE var_type;
    if (!src)
      {
	return result;
      }
    if (src->xasl)
      {
	if (is_outptr_list)
	  {
	    result = CHECK_RESULT::PARALLEL_PAGE_BY_PAGE;
	    xasl_checker checker (map);
	    temp = checker.check (src->xasl);
	    xasl_setter setter (map);
	    setter.set (src->xasl, temp);
	  }
	else
	  {
	    result = CHECK_RESULT::CANNOT_PARALLEL;
	    xasl_checker checker (map);
	    temp = checker.check (src->xasl);
	    xasl_setter setter (map);
	    setter.set (src->xasl, temp);
	  }
      }

    var_type = TP_DOMAIN_TYPE (src->domain);
    switch (var_type)
      {
      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:
      case DB_TYPE_VOBJ:
	result = merge_check_result (result, CHECK_RESULT::PARALLEL_PAGE_BY_PAGE);
	break;
      default:
	result = merge_check_result (result, CHECK_RESULT::PARALLEL_LIST_MERGE);
	break;
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
      case TYPE_LIST_ID:
	/* can execute with constants */
	break;
      case TYPE_ORDERBY_NUM:
      case TYPE_CLASSOID:
      case TYPE_REGUVAL_LIST:
	/* cannot execute with this regu-variable */
	result = CHECK_RESULT::CANNOT_PARALLEL;
	break;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
	temp = check (src->value.arithptr, is_outptr_list);
	result = merge_check_result (result, temp);
	break;
      case TYPE_SP:
	result = check (src->value.sp_ptr->args, is_outptr_list);
	/* cannot execute sp in child threads */
	if (is_outptr_list)
	  {
	    result = merge_check_result (result, CHECK_RESULT::PARALLEL_PAGE_BY_PAGE);
	  }
	else
	  {
	    result = CHECK_RESULT::CANNOT_PARALLEL;
	  }
	break;
      case TYPE_FUNC:
	temp = check (src->value.funcp->operand, is_outptr_list);
	result = merge_check_result (result, temp);
	break;
      case TYPE_REGU_VAR_LIST:
	temp = check (src->value.regu_var_list, is_outptr_list);
	if (temp == CHECK_RESULT::CANNOT_PARALLEL)
	  {
	    result = CHECK_RESULT::CANNOT_PARALLEL;
	  }
	else
	  {
	    result = merge_check_result (result, CHECK_RESULT::PARALLEL_PAGE_BY_PAGE);
	  }
	break;
      default:
	result = CHECK_RESULT::CANNOT_PARALLEL;
	break;
      }
    return result;
  }

  CHECK_RESULT general_checker::check (PRED_EXPR *src, bool is_outptr_list)
  {
    if (!src)
      {
	return CHECK_RESULT::NONE;
      }

    switch (src->type)
      {
      case T_PRED:
	return check (&src->pe.m_pred, is_outptr_list);
	break;
      case T_EVAL_TERM:
	return check (&src->pe.m_eval_term, is_outptr_list);
	break;
      case T_NOT_TERM:
	return check (src->pe.m_not_term, is_outptr_list);
	break;
      default:
	return CHECK_RESULT::CANNOT_PARALLEL;
	break;
      }
  }
  CHECK_RESULT general_checker::check (REGU_VARIABLE_LIST src, bool is_outptr_list)
  {
    if (!src)
      {
	return CHECK_RESULT::NONE;
      }
    REGU_VARIABLE_LIST curr = src;
    CHECK_RESULT result = CHECK_RESULT::NONE;
    while (curr)
      {
	if (is_outptr_list)
	  {
	    if (REGU_VARIABLE_IS_FLAGED (&curr->value, REGU_VARIABLE_HIDDEN_COLUMN))
	      {
		(void) check (&curr->value, is_outptr_list);
	      }
	    else
	      {
		result = merge_check_result (result, check (&curr->value, is_outptr_list));
	      }
	  }
	else
	  {
	    result = merge_check_result (result, check (&curr->value, is_outptr_list));
	  }
	curr = curr->next;
      }
    return result;
  }

  CHECK_RESULT general_checker::check (ARITH_TYPE *src, bool is_outptr_list)
  {
    CHECK_RESULT result = CHECK_RESULT::NONE, temp = CHECK_RESULT::NONE;
    if (!src)
      {
	return result;
      }
    temp = check (src->leftptr, is_outptr_list);
    result = merge_check_result (result, temp);
    temp = check (src->rightptr, is_outptr_list);
    result = merge_check_result (result, temp);
    temp = check (src->thirdptr, is_outptr_list);
    result = merge_check_result (result, temp);
    temp = check (src->pred, is_outptr_list);
    result = merge_check_result (result, temp);
    if (src->opcode == T_TRACE_STATS)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
      }
    return result;
  }

  CHECK_RESULT general_checker::check (PRED *src, bool is_outptr_list)
  {
    CHECK_RESULT result = CHECK_RESULT::NONE, temp = CHECK_RESULT::NONE;
    if (!src)
      {
	return result;
      }
    temp = check (src->lhs, is_outptr_list);
    result = check (src->rhs, is_outptr_list);
    result = merge_check_result (result, temp);
    return result;
  }

  CHECK_RESULT general_checker::check (EVAL_TERM *src, bool is_outptr_list)
  {
    CHECK_RESULT result = CHECK_RESULT::NONE;
    if (!src)
      {
	return result;
      }
    switch (src->et_type)
      {
      case T_COMP_EVAL_TERM:
	return check (&src->et.et_comp, is_outptr_list);
	break;
      case T_ALSM_EVAL_TERM:
	return check (&src->et.et_alsm, is_outptr_list);
	break;
      case T_LIKE_EVAL_TERM:
	return check (&src->et.et_like, is_outptr_list);
	break;
      case T_RLIKE_EVAL_TERM:
	return check (&src->et.et_rlike, is_outptr_list);
	break;
      default:
	return result;
	break;
      }
  }
  CHECK_RESULT general_checker::check (COMP_EVAL_TERM *src, bool is_outptr_list)
  {
    if (!src)
      {
	return CHECK_RESULT::NONE;
      }
    return merge_check_result (check (src->lhs, is_outptr_list), check (src->rhs, is_outptr_list));
  }
  CHECK_RESULT general_checker::check (ALSM_EVAL_TERM *src, bool is_outptr_list)
  {
    if (!src)
      {
	return CHECK_RESULT::NONE;
      }
    return merge_check_result (check (src->elem, is_outptr_list), check (src->elemset, is_outptr_list));
  }
  CHECK_RESULT general_checker::check (LIKE_EVAL_TERM *src, bool is_outptr_list)
  {
    CHECK_RESULT result = CHECK_RESULT::NONE;
    if (!src)
      {
	return result;
      }
    result = merge_check_result (check (src->src, is_outptr_list), check (src->pattern, is_outptr_list));
    return merge_check_result (result, check (src->esc_char, is_outptr_list));
  }
  CHECK_RESULT general_checker::check (RLIKE_EVAL_TERM *src, bool is_outptr_list)
  {
    CHECK_RESULT result = CHECK_RESULT::NONE;
    if (!src)
      {
	return result;
      }
    result = merge_check_result (check (src->src, is_outptr_list), check (src->pattern, is_outptr_list));
    return merge_check_result (result, check (src->case_sensitive, is_outptr_list));
  }

  CHECK_RESULT spec_checker::check (ACCESS_SPEC_TYPE *spec)
  {
    CHECK_RESULT result = CHECK_RESULT::NONE, temp = CHECK_RESULT::NONE;
    if (!spec)
      {
	return CHECK_RESULT::NONE;
      }
    if (spec->access != ACCESS_METHOD_SEQUENTIAL || spec->type != TARGET_CLASS)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
	return result;
      }
    if (spec->next)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
	return result;
      }
    general_checker general_checker (map);
    result = merge_check_result (result, general_checker.check (spec->s.cls_node.cls_regu_list_pred));
    result = merge_check_result (result, general_checker.check (spec->s.cls_node.cls_regu_list_rest));
    result = merge_check_result (result, general_checker.check (spec->where_pred));
    if (!spec->s.cls_node.cls_regu_list_pred && !spec->s.cls_node.cls_regu_list_rest)
      {
	result = merge_check_result (result, CHECK_RESULT::PARALLEL_PAGE_BY_PAGE);
      }
    return result;
  }

  void spec_setter::set (ACCESS_SPEC_TYPE *spec, CHECK_RESULT result)
  {
    if (result == CHECK_RESULT::NONE)
      {
	return;
      }
    else if (result == CHECK_RESULT::PARALLEL_LIST_MERGE)
      {
	if (map->is_setted_lm ((void *)spec) || map->is_setted_pbp ((void *)spec)
	    || map->is_setted_cannot_parallel ((void *)spec))
	  {
	    return;
	  }
	map->set_lm ((void *)spec);
	ACCESS_SPEC_SET_FLAG (spec, ACCESS_SPEC_FLAG_MERGEABLE_LIST);
      }
    else if (result == CHECK_RESULT::PARALLEL_PAGE_BY_PAGE)
      {
	if (map->is_setted_pbp ((void *)spec) || map->is_setted_cannot_parallel ((void *)spec))
	  {
	    return;
	  }
	map->set_pbp ((void *)spec);
	map->set_lm ((void *)spec);
	ACCESS_SPEC_UNSET_FLAG (spec, ACCESS_SPEC_FLAG_MERGEABLE_LIST);
      }
    else if (result == CHECK_RESULT::CANNOT_PARALLEL)
      {
	if (map->is_setted_cannot_parallel ((void *)spec))
	  {
	    return;
	  }
	map->set_cannot_parallel ((void *)spec);
	map->set_pbp ((void *)spec);
	map->set_lm ((void *)spec);
	ACCESS_SPEC_SET_FLAG (spec, ACCESS_SPEC_FLAG_NO_PARALLEL_HEAP_SCAN);
      }
  }

  CHECK_RESULT xasl_checker::check (XASL_NODE *xasl)
  {
    if (!xasl)
      {
	return CHECK_RESULT::NONE;
      }
    if (map->is_checked ((void *)xasl))
      {
	return CHECK_RESULT::NONE;
      }
    map->set_checked ((void *)xasl);
    CHECK_RESULT result = CHECK_RESULT::NONE;
    CHECK_RESULT subquery_result = CHECK_RESULT::NONE;
    xasl_setter setter (map);
    switch (xasl->type)
      {
      case BUILDLIST_PROC:
	if (xasl->proc.buildlist.g_hash_eligible)
	  {
	    result = CHECK_RESULT::PARALLEL_PAGE_BY_PAGE;
	  }
	if (xasl->proc.buildlist.a_eval_list)
	  {
	    if (XASL_IS_FLAGED (xasl, XASL_ANALYTIC_USES_LIMIT_OPT))
	      {
		result = CHECK_RESULT::PARALLEL_PAGE_BY_PAGE;
	      }
	    else
	      {
		result = CHECK_RESULT::PARALLEL_LIST_MERGE;
	      }
	  }
	break;
      case BUILDVALUE_PROC:
	if (xasl->proc.buildvalue.agg_list)
	  {
	    result = CHECK_RESULT::PARALLEL_PAGE_BY_PAGE;
	  }
	break;
      case CTE_PROC:
	if (xasl->proc.cte.non_recursive_part)
	  {
	    subquery_result = check (xasl->proc.cte.non_recursive_part);
	    setter.set (xasl->proc.cte.non_recursive_part, subquery_result);
	  }
	if (xasl->proc.cte.recursive_part)
	  {
	    setter.set (xasl->proc.cte.recursive_part, CHECK_RESULT::CANNOT_PARALLEL);
	    (void) check (xasl->proc.cte.recursive_part);
	  }
	break;
      case HASHJOIN_PROC:
	if (xasl->proc.hashjoin.outer.xasl)
	  {
	    subquery_result = check (xasl->proc.hashjoin.outer.xasl);
	    setter.set (xasl->proc.hashjoin.outer.xasl, subquery_result);
	  }
	if (xasl->proc.hashjoin.inner.xasl)
	  {
	    subquery_result = check (xasl->proc.hashjoin.inner.xasl);
	    setter.set (xasl->proc.hashjoin.inner.xasl, subquery_result);
	  }
	break;
      case MERGE_PROC:
	if (xasl->proc.merge.insert_xasl)
	  {
	    for (XASL_NODE *xaslp = xasl->proc.merge.insert_xasl; xaslp; xaslp = xaslp->next)
	      {
		setter.set_cannot_parallel_recursive (xaslp);
	      }
	  }
	if (xasl->proc.merge.update_xasl)
	  {
	    for (XASL_NODE *xaslp = xasl->proc.merge.update_xasl; xaslp; xaslp = xaslp->next)
	      {
		setter.set_cannot_parallel_recursive (xaslp);
	      }
	  }
	result = CHECK_RESULT::CANNOT_PARALLEL;
	break;
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
	for (XASL_NODE *xaslp = xasl->aptr_list; xaslp; xaslp = xaslp->next)
	  {
	    setter.set_cannot_parallel_recursive (xaslp);
	  }
	result = CHECK_RESULT::CANNOT_PARALLEL;
	break;
      }
    if (xasl->selected_upd_list || xasl->scan_op_type != S_SELECT || xasl->upd_del_class_cnt > 0
	|| XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
	setter.set_cannot_parallel_recursive (xasl);
      }
    CHECK_RESULT aptr_result = CHECK_RESULT::NONE;
    for (XASL_NODE *xaslp = xasl->aptr_list; xaslp; xaslp = xaslp->next)
      {
	aptr_result = check (xaslp);
	setter.set (xaslp, aptr_result);
      }

    for (XASL_NODE *xaslp = xasl->bptr_list; xaslp; xaslp = xaslp->next)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
	setter.set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->dptr_list; xaslp; xaslp = xaslp->next)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
	setter.set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->fptr_list; xaslp; xaslp = xaslp->next)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
	setter.set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->scan_ptr; xaslp; xaslp = xaslp->next)
      {
	result = CHECK_RESULT::PARALLEL_PAGE_BY_PAGE;
	setter.set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
	setter.set_cannot_parallel_recursive (xaslp);
      }
    if (xasl->if_pred)
      {
	result = CHECK_RESULT::CANNOT_PARALLEL;
      }
    if (xasl->instnum_pred || xasl->instnum_val)
      {
	result = CHECK_RESULT::PARALLEL_PAGE_BY_PAGE;
      }

    spec_checker spec_checker (map);
    spec_setter spec_setter (map);
    CHECK_RESULT spec_result = CHECK_RESULT::NONE;

    general_checker general_checker (map);
    CHECK_RESULT outptr_result = CHECK_RESULT::NONE;
    if (xasl->outptr_list)
      {
	outptr_result = general_checker.check (xasl->outptr_list->valptrp, true);
      }
    for (ACCESS_SPEC_TYPE *specp = xasl->spec_list; specp; specp = specp->next)
      {
	spec_result = spec_checker.check (specp);
	spec_result = merge_check_result (spec_result, outptr_result);
	spec_setter.set (specp, spec_result);
	result = merge_check_result (result, spec_result);
      }
    for (ACCESS_SPEC_TYPE *specp = xasl->merge_spec; specp; specp = specp->next)
      {
	spec_result = spec_checker.check (specp);
	spec_result = merge_check_result (spec_result, outptr_result);
	spec_setter.set (specp, spec_result);
	result = merge_check_result (result, spec_result);
      }

    return result;
  }

  void xasl_setter::set (XASL_NODE *xasl, CHECK_RESULT result)
  {
    if (result == CHECK_RESULT::NONE)
      {
	return;
      }
    else if (result == CHECK_RESULT::PARALLEL_PAGE_BY_PAGE)
      {
	set_pbp (xasl);
      }
    else if (result == CHECK_RESULT::PARALLEL_LIST_MERGE)
      {
	set_lm (xasl);
      }
    else if (result == CHECK_RESULT::CANNOT_PARALLEL)
      {
	set_cannot_parallel (xasl);
      }

    switch (xasl->type)
      {
      case BUILDLIST_PROC:
      case BUILDVALUE_PROC:
	break;
      case CTE_PROC:
	if (xasl->proc.cte.non_recursive_part)
	  {
	    set (xasl->proc.cte.non_recursive_part, result);
	  }
	break;
      case HASHJOIN_PROC:
	if (xasl->proc.hashjoin.outer.xasl)
	  {
	    set (xasl->proc.hashjoin.outer.xasl, result);
	  }
	if (xasl->proc.hashjoin.inner.xasl)
	  {
	    set (xasl->proc.hashjoin.inner.xasl, result);
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
	break;
      }
    spec_setter spec_setter (map);
    for (ACCESS_SPEC_TYPE *specp = xasl->spec_list; specp; specp = specp->next)
      {
	spec_setter.set (specp, result);
      }
    for (ACCESS_SPEC_TYPE *specp = xasl->merge_spec; specp; specp = specp->next)
      {
	spec_setter.set (specp, result);
      }
  }

  void xasl_setter::set_cannot_parallel_recursive (XASL_NODE *xasl)
  {
    set_cannot_parallel (xasl);
    switch (xasl->type)
      {
      case BUILDLIST_PROC:
      case BUILDVALUE_PROC:
	break;
      case CTE_PROC:
	if (xasl->proc.cte.non_recursive_part)
	  {
	    set_cannot_parallel_recursive (xasl->proc.cte.non_recursive_part);
	  }
	break;
      case HASHJOIN_PROC:
	if (xasl->proc.hashjoin.outer.xasl)
	  {
	    set_cannot_parallel_recursive (xasl->proc.hashjoin.outer.xasl);
	  }
	if (xasl->proc.hashjoin.inner.xasl)
	  {
	    set_cannot_parallel_recursive (xasl->proc.hashjoin.inner.xasl);
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
	break;
      }
    for (XASL_NODE *xaslp = xasl->aptr_list; xaslp; xaslp = xaslp->next)
      {
	set_cannot_parallel_recursive (xaslp);
      }

    for (XASL_NODE *xaslp = xasl->bptr_list; xaslp; xaslp = xaslp->next)
      {
	set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->dptr_list; xaslp; xaslp = xaslp->next)
      {
	set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->fptr_list; xaslp; xaslp = xaslp->next)
      {
	set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->scan_ptr; xaslp; xaslp = xaslp->next)
      {
	set_cannot_parallel_recursive (xaslp);
      }
    for (XASL_NODE *xaslp = xasl->connect_by_ptr; xaslp; xaslp = xaslp->next)
      {
	set_cannot_parallel_recursive (xaslp);
      }

    spec_setter spec_setter (map);
    for (ACCESS_SPEC_TYPE *specp = xasl->spec_list; specp; specp = specp->next)
      {
	spec_setter.set (specp, CHECK_RESULT::CANNOT_PARALLEL);
      }
    for (ACCESS_SPEC_TYPE *specp = xasl->merge_spec; specp; specp = specp->next)
      {
	spec_setter.set (specp, CHECK_RESULT::CANNOT_PARALLEL);
      }
  }

  void xasl_setter::set_cannot_parallel (XASL_NODE *xasl)
  {
    if (map->is_setted_cannot_parallel ((void *)xasl))
      {
	return;
      }
    /* pbp, lm -> cannot parallel */
    map->set_cannot_parallel ((void *)xasl);
    map->set_pbp ((void *)xasl);
    map->set_lm ((void *)xasl);
  }

  void xasl_setter::set_pbp (XASL_NODE *xasl)
  {
    if (map->is_setted_pbp ((void *)xasl))
      {
	return;
      }
    if (map->is_setted_cannot_parallel ((void *)xasl))
      {
	return;
      }
    /* list merge -> page by page */
    map->set_pbp ((void *)xasl);
    map->set_lm ((void *)xasl);
  }

  void xasl_setter::set_lm (XASL_NODE *xasl)
  {
    if (map->is_setted_lm ((void *)xasl))
      {
	return;
      }
    if (map->is_setted_cannot_parallel ((void *)xasl) || map->is_setted_pbp ((void *)xasl))
      {
	return;
      }
    map->set_lm ((void *)xasl);
  }

  struct count_distinct_check
  {
    bool operator() (xasl_node *xasl) const
    {
      if (xasl == nullptr)
	{
	  return false;
	}

      if (xasl->type != BUILDVALUE_PROC)
	{
	  return false;
	}

      if (xasl->scan_ptr || xasl->aptr_list || xasl->dptr_list || xasl->fptr_list || xasl->connect_by_ptr || xasl->bptr_list)
	{
	  return false;
	}

      if (!xasl->spec_list)
	{
	  return false;
	}

      for (ACCESS_SPEC_TYPE *specp = xasl->spec_list; specp; specp = specp->next)
	{
	  if (specp->type != TARGET_CLASS)
	    {
	      return false;
	    }

	  if (specp->access != ACCESS_METHOD_SEQUENTIAL)
	    {
	      return false;
	    }
	}

      if (!xasl->proc.buildvalue.agg_list)
	{
	  return false;
	}

      int outptr_cnt = xasl->outptr_list->valptr_cnt;

      REGU_VARIABLE_LIST outptr_it = xasl->outptr_list->valptrp;
      for (; outptr_it != nullptr; outptr_it = outptr_it->next)
	{
	  if (outptr_it->value.type != TYPE_CONSTANT)
	    {
	      return false;
	    }
	}

      check_and_set_map map;
      general_checker general_checker (&map);
      CHECK_RESULT res = CHECK_RESULT::NONE;

      int agg_cnt = 0;

      AGGREGATE_TYPE *agg_it = xasl->proc.buildvalue.agg_list;
      for (; agg_it != nullptr; agg_it = agg_it->next)
	{
	  if (agg_it->function != PT_COUNT_STAR && agg_it->function != PT_COUNT)
	    {
	      return false;
	    }

	  res = general_checker.check (agg_it->operands, false);
	  if (res == CHECK_RESULT::CANNOT_PARALLEL)
	    {
	      return false;
	    }

	  agg_cnt++;
	}

      if (agg_cnt != outptr_cnt)
	{
	  return false;
	}

      for (ACCESS_SPEC_TYPE *specp = xasl->spec_list; specp; specp = specp->next)
	{
	  ACCESS_SPEC_SET_FLAG (specp, ACCESS_SPEC_FLAG_COUNT_DISTINCT);
	}

      return true;
    }
  } const count_distinct_check;

  int checker::check (XASL_NODE *xasl)
  {
    if (!xasl)
      {
	return 0;
      }
    xasl_checker checker (&check_map);
    CHECK_RESULT result = checker.check (xasl);
    xasl_setter setter (&check_map);
    setter.set (xasl, result);
    if (result != CHECK_RESULT::CANNOT_PARALLEL)
      {
	count_distinct_check (xasl);
      }
    return 0;
  }
}

extern int
scan_check_parallel_heap_scan_possible (XASL_NODE *xasl)
{
  parallel_heap_scan::checker checker;
  return checker.check (xasl);
}
