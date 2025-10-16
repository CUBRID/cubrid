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

#include "xasl_iteration.hpp"
#include <iostream>
#include <sstream>
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

extern "C" {
  XASL_NODE *xasl_find_by_id (XASL_NODE *xasl, int target_id)
  {
    if (xasl == nullptr)
      {
	return nullptr;
      }
    std::function<XASL_NODE* (XASL_NODE *)> find_by_id = [target_id] (XASL_NODE* node) -> XASL_NODE*
    {
      if (node->header.id == target_id)
	{
	  return node;
	}
      return nullptr;
    };

    return cubxasl::iterate_xasl_tree<XASL_NODE *> (xasl,find_by_id,nullptr);
  }

  void xasl_dump_with_id (XASL_NODE *xasl)
  {
    const char *xasl_type_string[] =
    {
      "UNION_PROC",
      "DIFFERENCE_PROC",
      "INTERSECTION_PROC",
      "OBJFETCH_PROC",
      "BUILDLIST_PROC",
      "BUILDVALUE_PROC",
      "SCAN_PROC",
      "MERGELIST_PROC",
      "HASHJOIN_PROC",
      "UPDATE_PROC",
      "DELETE_PROC",
      "INSERT_PROC",
      "CONNECTBY_PROC",
      "DO_PROC",
      "MERGE_PROC",
      "BUILD_SCHEMA_PROC",
      "CTE_PROC"
    };

    std::ostringstream oss;
    std::function<bool (XASL_NODE *)> dump_with_id = [xasl_type_string, &oss] (XASL_NODE* node) -> bool
    {
      if (XASL_IS_FLAGED (node, XASL_TOP_MOST_XASL))
	{
	  oss << "QUERY : " << node->query_alias << std::endl;
	}
      oss << "XASL : " << node << " id: " << node->header.id << " type: " << xasl_type_string[node->type] << std::endl;
      return true;
    };
    cubxasl::iterate_xasl_tree<bool> (xasl,dump_with_id,true);

    std::string result = oss.str();
    _er_log_debug (ARG_FILE_LINE, result.c_str());
  }
}

