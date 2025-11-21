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
  static constexpr int min_parallel_degree = 2;

  typedef enum parallel_type
  {
    PARALLEL_HEAP_SCAN = 0,
    PARALLEL_HASH_JOIN = 1,
    PARALLEL_SORT = 2,
    PARALLEL_SUBQUERY = 3,
  } PARALLEL_TYPE;

  UINT32 compute_parallel_degree (PARALLEL_TYPE type, UINT64 num_pages, int hint_degree = -1) noexcept;
}				/* namespace parallel_query */
