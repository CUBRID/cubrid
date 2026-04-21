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
     * probe_init_contexts / probe_clear_contexts
     *
     * Per-worker secondary context helpers used by parallel probe. A secondary context shares
     * the hash_table (already built into the primary's hash_scan) and the outer/inner/build/probe
     * list_id pointers. It owns its own temp_key / temp_new_key / build-side list_scan_id so
     * multiple workers can look up matches concurrently without interfering with each other.
     */

    int probe_init_contexts (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager,
			     HASHJOIN_CONTEXT *primary, HASHJOIN_CONTEXT *secondary);
    void probe_clear_contexts (cubthread::entry &thread_ref, HASHJOIN_CONTEXT *secondary);

    /*
     * probe_prepare / probe_execute
     */

    int probe_prepare (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);
    int probe_execute (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);
  } /* namespace hash_join */
} /* namespace parallel_query */
