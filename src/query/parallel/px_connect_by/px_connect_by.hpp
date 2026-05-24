/*
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
 * px_connect_by.hpp - parallel partition build for CONNECT BY hash execution
 */

#pragma once

#if !defined (SERVER_MODE)
#error Belongs to server module
#endif

#include "regu_var.hpp"
#include "query_executor.h"
#include "query_hash_scan.h"
#include "thread_entry.hpp"

struct mht_hls_table;
typedef struct mht_hls_table MHT_HLS_TABLE;

namespace parallel_query
{
  namespace connect_by
  {
    int build_partition_hashes (cubthread::entry &thread_ref,
				QFILE_LIST_ID **partition_lists,
				int partition_count,
				REGU_VARIABLE_LIST regu_list_pred,
				REGU_VARIABLE_LIST regu_list_rest,
				REGU_VARIABLE_LIST hash_build_regu_list,
				VAL_DESCR *vd,
				int val_cnt,
				MHT_HLS_TABLE **out_hashes);
  } /* namespace connect_by */
} /* namespace parallel_query */
