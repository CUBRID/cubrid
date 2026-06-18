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
 * px_hash_join.hpp
 */

#pragma once

#include "query_hash_join.h"

#include "thread_entry.hpp"		/* cubthread::entry */

namespace parallel_query
{
  namespace hash_join
  {
    /*
     * build_partitions
     */

    int build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info);

    /*
     * execute_partitions
     */

    int execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);

    /*
     * parallel_probe
     */

    int init_context (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context);
    void clear_context (cubthread::entry &thread_ref, HASHJOIN_CONTEXT *context);

    int probe_prepare (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);
    int probe_execute (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);
  } /* namespace hash_join */
} /* namespace parallel_query */
