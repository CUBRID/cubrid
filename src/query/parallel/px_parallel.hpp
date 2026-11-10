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

/*
 * parallel.h - parallel module
 */

#pragma once

#include "system.h"

namespace parallel_query
{
  enum class parallel_type : int
  {
    SCAN      = 0,	/* heap / list / index scan */
    HASH_JOIN = 1,
    SORT      = 2,
    SUBQUERY  = 3,
    MERGE_JOIN = 4,	/* range-partitioned merge of a sort-merge join's input lists */
  };

  UINT32 compute_parallel_degree (parallel_type type, UINT64 num_pages,
				  int hint_degree = -1 /* auto-compute */ ) noexcept;

  /* true when the current thread's transaction is inside a system operation; no worker sharing the transaction
   * may be started then (CBRD-27492). compute_parallel_degree () already applies it. */
  bool is_under_system_operation () noexcept;
}				/* namespace parallel_query */
