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
 * query_hash_join.hpp
 */

#ifndef _QUERY_HASH_JOIN_HPP_
#define _QUERY_HASH_JOIN_HPP_

#include "query_hash_scan.h"
#include "query_list.h"
#include "thread_entry.hpp"
#include "xasl.h"

#define HASH_JOIN_PROFILE_TIME 1
#define HASH_JOIN_DUMP_HASH_TABLE 1
#define HASH_JOIN_DUMP_PROBE 1
#define HASH_JOIN_DUMP_BUILD 1

struct xasl_node;
typedef struct xasl_node XASL_NODE;

// *INDENT-OFF*
namespace cubxasl
{
  struct pred_expr;
}
using PRED_EXPR = cubxasl::pred_expr;
// *INDENT-ON*

struct hashjoin_input;
typedef struct hashjoin_input HJ_INPUT;

#if 0
struct hashjoin_domain_info;
typedef struct hashjoin_domain_info HJ_DOMAIN_INFO;
#endif

enum hashjoin_status
{
  HASHJOIN_NONE = 0,
  HASHJOIN_FILL_EMPTY,
  HASHJOIN_TRY,
  HASHJOIN_SINGLE,
  HASHJOIN_PARTITION,
  HASHJOIN_END,
  HASHJOIN_ERROR
};
typedef enum hashjoin_status HASHJOIN_STATUS;
typedef enum hashjoin_status HJ_STATUS;

struct hashjoin_input_domain_info
{
  TP_DOMAIN **domains;
  int *value_indexes;
};
typedef struct hashjoin_input_domain_info HASHJOIN_INPUT_DOMAIN_INFO;
typedef struct hashjoin_input_domain_info HJ_INPUT_DOMAIN_INFO;

struct hashjoin_fetch_info
{
  HJ_INPUT_DOMAIN_INFO *input;

  /* The common domains between the domains of values used in the build and probe inputs. */
  TP_DOMAIN **coerce_domains;

  /* Whether there is a need to use the coerce domain. */
  bool need_coerce_domains;
};
typedef struct hashjoin_fetch_info HASHJOIN_FETCH_INFO;
typedef struct hashjoin_fetch_info HJ_FETCH_INFO;

#define HASHJOIN_FETCH_KEY_INFO_INITIALIZER { NULL, NULL, false, false }
#define HJ_FETCH_KEY_INFO_INITIALIZER HASHJOIN_FETCH_KEY_INFO_INITIALIZER

struct hashjoin_partition_info
{
  QFILE_LIST_ID *list_id;
  QFILE_LIST_ID **part_list_id;
  int part_cnt;
};
typedef struct hashjoin_partition_info HASHJOIN_PARTITION_INFO;
typedef struct hashjoin_partition_info HJ_PARTITION_INFO;

#define HASHJOIN_PARTITION_INFO_INITIALIZER { NULL, NULL, 0 }
#define HJ_PARTITION_INFO_INITIALIZER HASHJOIN_PARTITION_INFO_INITIALIZER

struct hashjoin_context
{
  QFILE_LIST_ID *outer_list_id;
  QFILE_LIST_ID *inner_list_id;

  HJ_FETCH_INFO outer_fetch_info;
  HJ_FETCH_INFO inner_fetch_info;
  int key_cnt;

  JOIN_TYPE join_type;
  PRED_EXPR *during_join_pred;

  HASH_LIST_SCAN hash_scan;

  bool is_single_context;
  bool is_build_outer;
};
typedef struct hashjoin_context HASHJOIN_CONTEXT;
typedef struct hashjoin_context HJ_CONTEXT;

struct hashjoin_manager
{
  QFILE_LIST_MERGE_INFO *merge_info;

  QUERY_ID query_id;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;

  HJ_INPUT *outer;
  HJ_INPUT *inner;

  HJ_CONTEXT single_context;
  HJ_CONTEXT *contexts;
  int context_cnt;
};
typedef struct hashjoin_manager HASHJOIN_MANAGER;
typedef struct hashjoin_manager HJ_MANAGER;

struct hashjoin_stats
{
  HASH_METHOD hash_method;
  bool is_build_outer;

  struct
  {
    struct timeval elapsed_time;
    UINT64 fetches;
    UINT64 fetch_time;
    UINT64 ioreads;
    UINT32 part_cnt;
  } part;

  struct
  {
    struct timeval elapsed_time;
    struct timeval build_time;
    UINT64 fetches;
    UINT64 fetch_time;
    UINT64 ioreads;

#if HASH_JOIN_PROFILE_TIME
    struct
    {
      struct timeval fetch;	/* qexec_hash_join_fetch_key */
      struct timeval hash;	/* qdata_hash_scan_key */
      struct timeval insert;	/* qexec_hash_join_build_key */
    } profile;
#endif
  } build;

  struct
  {
    struct timeval elapsed_time;
    struct timeval probe_time;
    UINT64 fetches;
    UINT64 fetch_time;
    UINT64 ioreads;
    UINT64 readkeys;
    UINT64 rows;
    UINT32 max_collisions;

#if HASH_JOIN_PROFILE_TIME
    struct
    {
      struct timeval fetch;	/* qexec_hash_join_fetch_key */
      struct timeval hash;	/* qdata_hash_scan_key */
      struct timeval search;	/* qexec_hash_join_probe_key */
      struct timeval match;	/* qexec_hash_join_fetch_key */
      struct timeval add;	/* qexec_merge_tuple_add_list */
    } profile;
#endif
  } probe;
};
typedef struct hashjoin_stats HASHJOIN_STATS;
typedef struct hashjoin_stats HJ_STATS;

int qexec_hash_join (THREAD_ENTRY *thread_p, XASL_NODE *xasl, UINT64 query_id);

#endif /* _QUERY_HASH_JOIN_HPP_ */
