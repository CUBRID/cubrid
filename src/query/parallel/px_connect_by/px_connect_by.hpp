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
 * px_connect_by.hpp - parallel partition build/probe for CONNECT BY hash execution
 */

#pragma once

#if !defined (SERVER_MODE)
#error Belongs to server module
#endif

#include "regu_var.hpp"
#include "query_executor.h"
#include "query_hash_scan.h"
#include "thread_entry.hpp"
#include "xasl.h"

struct mht_hls_table;
typedef struct mht_hls_table MHT_HLS_TABLE;

/* per-worker isolation context for parallel CONNECT BY probe */
typedef struct connectby_px_context
{
  // *INDENT-OFF*
  val_descr vd;

  /* cloned regu lists with vfetch_to/dbvalptr redirected to private vd.dbval_ptr */
  REGU_VARIABLE_LIST prior_regu_list_pred;
  REGU_VARIABLE_LIST prior_regu_list_rest;
  REGU_VARIABLE_LIST hash_probe_regu_list;
  REGU_VARIABLE_LIST hash_build_regu_list;
  REGU_VARIABLE_LIST regu_list_pred;
  REGU_VARIABLE_LIST regu_list_rest;

  /* cloned outptr lists (valptrp chains redirected to private vd.dbval_ptr) */
  valptr_list_node prior_outptr_list;
  valptr_list_node outptr_list;

  /* global result_list from previous levels for cycle detection (read-only) */
  QFILE_LIST_ID *cycle_check_list;
  // *INDENT-ON*
} CONNECTBY_PX_CONTEXT;

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

    int probe_partitions (cubthread::entry &thread_ref,
			  XASL_NODE *xasl, XASL_STATE *xasl_state,
			  QFILE_LIST_ID *global_result_list,
			  QFILE_LIST_ID **frontier_parts,
			  MHT_HLS_TABLE **part_hashes,
			  int partition_count,
			  QFILE_TUPLE_VALUE_TYPE_LIST *type_list,
			  int level_value,
			  int val_cnt,
			  QFILE_LIST_ID *next_frontier,
			  QFILE_LIST_ID *result_list);
  } /* namespace connect_by */
} /* namespace parallel_query */
