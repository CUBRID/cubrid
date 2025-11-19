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
 * px_heap_scan_misc.hpp - miscellaneous functions for parallel heap scan
 */

#ifndef _PX_HEAP_SCAN_MISC_HPP_
#define _PX_HEAP_SCAN_MISC_HPP_

#include "regu_var.hpp"
#include "xasl_predicate.hpp"
#include "scan_manager.h"
#include "thread_manager.hpp"



namespace parallel_heap_scan
{
  int regu_var_list_len (struct regu_variable_list_node   *list);
  int regu_var_clear (THREAD_ENTRY *thread_p, REGU_VARIABLE *regu_var);
  int pred_clear (THREAD_ENTRY *thread_p, PRED_EXPR *pred);
  int arith_list_clear (THREAD_ENTRY *thread_p, ARITH_TYPE *list);
  SCAN_CODE scan_next_heap_scan_1page_internal (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, VPID *curr_vpid);
}
#endif /*_PX_HEAP_SCAN_MISC_HPP_ */
