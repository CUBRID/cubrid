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

#ifndef _XASL_ITERATION_HPP_
#define _XASL_ITERATION_HPP_

#include "regu_var.hpp"
#include "xasl.h"
#include <functional>

namespace cubxasl
{
  template<typename T>
  using xasl_iteration_function = std::function<T (XASL_NODE *)>;

  template<typename T>
  T iterate_xasl_tree (XASL_NODE *xasl, xasl_iteration_function<T> func, T default_value);
  template<typename T>
  T iterate_regu_var (REGU_VARIABLE *regu, xasl_iteration_function<T> func, T default_value);

  template<typename T>
  T iterate_regu_var (REGU_VARIABLE *regu, xasl_iteration_function<T> func, T default_value)
  {
    T result;
    if (regu == nullptr)
      {
	return default_value;
      }
    if (regu->xasl != nullptr)
      {
	result = iterate_xasl_tree (regu->xasl, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }
    switch (regu->type)
      {
      case TYPE_INARITH:
      case TYPE_OUTARITH:
      {
	ARITH_TYPE *arithptr = regu->value.arithptr;
	result = iterate_regu_var (arithptr->leftptr, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
	result = iterate_regu_var (arithptr->rightptr, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
	result = iterate_regu_var (arithptr->thirdptr, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }
      break;
      case TYPE_FUNC:
      {
	FUNCTION_TYPE *funcp = regu->value.funcp;
	REGU_VARIABLE_LIST regu_var_list = funcp->operand;
	for (; regu_var_list != nullptr; regu_var_list = regu_var_list->next)
	  {
	    result = iterate_regu_var (&regu_var_list->value, func, default_value);
	    if (result != default_value)
	      {
		return result;
	      }
	  }
      }
      break;
      case TYPE_REGUVAL_LIST:
      {
	REGU_VALUE_ITEM *regu_value_item = regu->value.reguval_list->regu_list;
	for (; regu_value_item != nullptr; regu_value_item = regu_value_item->next)
	  {
	    result = iterate_regu_var (regu_value_item->value, func, default_value);
	    if (result != default_value)
	      {
		return result;
	      }
	  }
      }
      break;
      case TYPE_REGU_VAR_LIST:
      {
	REGU_VARIABLE_LIST regu_variable_list = regu->value.regu_var_list;
	for (; regu_variable_list != nullptr; regu_variable_list = regu_variable_list->next)
	  {
	    result = iterate_regu_var (&regu_variable_list->value, func, default_value);
	    if (result != default_value)
	      {
		return result;
	      }
	  }
      }
      break;
      case TYPE_SP:
      {
	SP_TYPE *sp_ptr = regu->value.sp_ptr;
	REGU_VARIABLE_LIST regu_variable_list = sp_ptr->args;
	for (; regu_variable_list != nullptr; regu_variable_list = regu_variable_list->next)
	  {
	    result = iterate_regu_var (&regu_variable_list->value, func, default_value);
	    if (result != default_value)
	      {
		return result;
	      }
	  }
      }
      break;
      case TYPE_CONSTANT:
      case TYPE_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
      case TYPE_SHARED_ATTR_ID:
      case TYPE_POSITION:
      case TYPE_POS_VALUE:
      case TYPE_OID:
      case TYPE_CLASSOID:
      case TYPE_ORDERBY_NUM:
      case TYPE_DBVAL:
      case TYPE_LIST_ID:
      default:
	break;
      }
    return default_value;
  }

  template<typename T>
  T iterate_xasl_tree (XASL_NODE *xasl, xasl_iteration_function<T> func, T default_value)
  {
    XASL_NODE *iterator;
    if (xasl == nullptr)
      {
	return default_value;
      }

    T result = func (xasl);
    if (result != default_value)
      {
	return result;
      }

    for (iterator = xasl->aptr_list; iterator != nullptr; iterator = iterator->next)
      {
	result = iterate_xasl_tree (iterator, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }

    for (iterator = xasl->bptr_list; iterator != nullptr; iterator = iterator->next)
      {
	result = iterate_xasl_tree (iterator, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }


    for (iterator = xasl->dptr_list; iterator != nullptr; iterator = iterator->next)
      {
	result = iterate_xasl_tree (iterator, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }

    for (iterator = xasl->scan_ptr; iterator != nullptr; iterator = iterator->next)
      {
	result = iterate_xasl_tree (iterator, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }

    for (iterator = xasl->connect_by_ptr; iterator != nullptr; iterator = iterator->next)
      {
	result = iterate_xasl_tree (iterator, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }

    for (iterator = xasl->fptr_list; iterator != nullptr; iterator = iterator->next)
      {
	result = iterate_xasl_tree (iterator, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }

    if (xasl->type == BUILDLIST_PROC)
      {
	for (iterator = xasl->proc.buildlist.eptr_list; iterator != nullptr; iterator = iterator->next)
	  {
	    result = iterate_xasl_tree (iterator, func, default_value);
	    if (result != default_value)
	      {
		return result;
	      }
	  }
      }

    if (xasl->type == CTE_PROC)
      {
	result = iterate_xasl_tree (xasl->proc.cte.non_recursive_part, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
	result = iterate_xasl_tree (xasl->proc.cte.recursive_part, func, default_value);
	if (result != default_value)
	  {
	    return result;
	  }
      }

    if (xasl->type == INSERT_PROC)
      {
	REGU_VARIABLE_LIST regu;
	regu = (* (xasl->proc.insert.valptr_lists))->valptrp;
	for (; regu != nullptr; regu = regu->next)
	  {
	    result = iterate_regu_var (&regu->value, func, default_value);
	    if (result != default_value)
	      {
		return result;
	      }
	  }
      }

    return default_value;
  }
}

extern "C" {
  XASL_NODE *xasl_find_by_id (XASL_NODE *xasl, int target_id);
  void xasl_dump_with_id (XASL_NODE *xasl);
}



#endif /* _XASL_ITERATION_HPP_ */
