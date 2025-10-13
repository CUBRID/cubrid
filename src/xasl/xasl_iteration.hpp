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

#include "xasl.h"
#include <functional>

namespace cubxasl
{
  template<typename T>
  using xasl_iteration_function = std::function<T (XASL_NODE *)>;

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

    return default_value;
  }
}

extern "C" {
  XASL_NODE *xasl_find_by_id (XASL_NODE *xasl, int target_id);
  void xasl_dump_with_id (XASL_NODE *xasl);
}



#endif /* _XASL_ITERATION_HPP_ */
