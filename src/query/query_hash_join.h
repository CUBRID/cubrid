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
 * query_hash_join.h
 */

#ifndef _QUERY_HASH_JOIN_H_
#define _QUERY_HASH_JOIN_H_

#include "regu_var.hpp"		/* REGU_VARIABLE_LIST */

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager_with_dedicated_pool */
#include "query_hash_scan.h"	/* HASH_METHOD */
#include "system.h"		/* UINT32, UINT64 */
#include "thread_entry.hpp"	/* THREAD_ENTRY */
#include "tsc_timer.h"		/* TSC_TICKS, TSCTIMEVAL, TSC_ADD_TIMEVAL */
#include "xasl_predicate.hpp"	/* PRED_EXPR */
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

/*
 * Debug Macros
 */

#define HASHJOIN_PROFILE_TIME 0
#define HASHJOIN_DUMP_PARTITION 0
#define HASHJOIN_DUMP_HASH_TABLE 0
#define HASHJOIN_DUMP_BUILD 0
#define HASHJOIN_DUMP_PROBE 0

/*
 * Forward Declarations
 */

struct xasl_node;
typedef struct xasl_node XASL_NODE;

struct tp_domain;
typedef struct tp_domain TP_DOMAIN;

/*
 * Enum & Typedef Definitions
 */

typedef enum hashjoin_merge_method
{
  HASHJOIN_MERGE_COMBINE = 0,
  HASHJOIN_MERGE_APPEND,
  HASHJOIN_MERGE_CONNECT
} HASHJOIN_MERGE_METHOD;

/*
 * Struct & Typedef Definitions
 */

typedef struct hashjoin_input
{
  XASL_NODE *xasl;

  /* For evaluating during-join predicates. */
  REGU_VARIABLE_LIST regu_list_pred;
} HASHJOIN_INPUT;

typedef struct hashjoin_input_domain_info
{
  TP_DOMAIN **domains;
  int *value_indexes;
} HASHJOIN_INPUT_DOMAIN_INFO;

typedef struct hashjoin_domain_info
{
  HASHJOIN_INPUT_DOMAIN_INFO outer;
  HASHJOIN_INPUT_DOMAIN_INFO inner;

  /* Common domains of build and probe inputs. */
  TP_DOMAIN **coerce_domains;

  /* Whether to use the coerce domain. */
  bool need_coerce_domains;
} HASHJOIN_DOMAIN_INFO;

#if defined (SERVER_MODE) || defined (SA_MODE)

typedef struct hashjoin_fetch_info
{
  /* Pointers to members of HASHJOIN_DOMAIN_INFO,
   * which is a member of HASHJOIN_PROC_NODE. */
  HASHJOIN_INPUT_DOMAIN_INFO *input;
  TP_DOMAIN **coerce_domains;
  bool need_coerce_domains;

  /* Pointer to a member of HASHJOIN_INPUT. */
  REGU_VARIABLE_LIST regu_list_pred;
} HASHJOIN_FETCH_INFO;
#define HASHJOIN_FETCH_INFO_INITIALIZER { NULL, NULL, false, NULL }

typedef struct hashjoin_input_info
{
  QFILE_LIST_ID *list_id;
  HASHJOIN_FETCH_INFO fetch_info;
} HASHJOIN_INPUT_INFO;
#define HASHJOIN_INPUT_INFO_INITIALIZER { NULL, NULL }

typedef struct hashjoin_input_split_info
{
  HASHJOIN_INPUT_INFO *input;

  QFILE_LIST_ID **part_list_id;
  int part_cnt;
} HASHJOIN_INPUT_SPLIT_INFO;
#define HASHJOIN_INPUT_SPLIT_INFO_INITIALIZER { NULL, NULL, 0 }

typedef struct hashjoin_split_info
{
  HASHJOIN_INPUT_SPLIT_INFO outer;
  HASHJOIN_INPUT_SPLIT_INFO inner;
} HASHJOIN_SPLIT_INFO;
#define HASHJOIN_SPLIT_INFO_INITIALIZER \
  { HASHJOIN_INPUT_SPLIT_INFO_INITIALIZER, HASHJOIN_INPUT_SPLIT_INFO_INITIALIZER }

typedef struct hashjoin_input_stats
{
  TSCTIMEVAL elapsed_time;
  UINT64 fetches;
  UINT64 fetch_time;
  UINT64 ioreads;
  UINT64 read_rows;
  UINT64 read_keys;
  UINT64 qualified_rows;
} HASHJOIN_INPUT_STATS;
#define HASHJOIN_INPUT_STATS_INITIALIZER { { 0 }, 0, 0, 0, 0, 0, 0 }
#define HASHJOIN_INPUT_STATS_MAX_INITIALIZER { { LONG_MAX, 999999 }, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX }

#if HASHJOIN_PROFILE_TIME
typedef struct hashjoin_profile_stats
{
  struct
  {
    TSCTIMEVAL fetch;		/* hjoin_fetch_key */
    TSCTIMEVAL hash;		/* qdata_hash_scan_key */
    TSCTIMEVAL insert;		/* hjoin_build_key */
  } build;

  struct
  {
    TSCTIMEVAL fetch;		/* hjoin_fetch_key */
    TSCTIMEVAL hash;		/* qdata_hash_scan_key */
    TSCTIMEVAL search;		/* hjoin_probe_key */
    TSCTIMEVAL match;		/* hjoin_fetch_key */
    TSCTIMEVAL add;		/* hjoin_merge_tuple_to_list_id */
  } probe;
} HASHJOIN_PROFILE_STATS;
#define HASHJOIN_PROFILE_STATS_INITIALIZER { { 0 }, { 0 } }
#endif /* HASHJOIN_PROFILE_TIME */

typedef struct hashjoin_stats
{
  HASH_METHOD hash_method;
  bool is_build_outer;

  HASHJOIN_INPUT_STATS split;
  HASHJOIN_INPUT_STATS build;
  HASHJOIN_INPUT_STATS probe;

#if HASHJOIN_PROFILE_TIME
  HASHJOIN_PROFILE_STATS profile;
#endif				/* HASHJOIN_PROFILE_TIME */
} HASHJOIN_STATS;

typedef struct hashjoin_stats_group
{
  HASHJOIN_STATS stats;
  HASHJOIN_STATS *context_stats;
  int context_cnt;
} HASHJOIN_STATS_GROUP;

typedef struct hashjoin_context
{
  QFILE_LIST_ID *list_id;

  HASHJOIN_INPUT_INFO outer;
  HASHJOIN_INPUT_INFO inner;

  PRED_EXPR *during_join_pred;

  HASH_LIST_SCAN hash_scan;
  bool is_build_outer;

  HASHJOIN_STATS *stats;
} HASHJOIN_CONTEXT;

typedef struct hashjoin_manager
{
  /* Pointers to members of HASHJOIN_PROC_NODE. */
  HASHJOIN_INPUT *outer;
  HASHJOIN_INPUT *inner;
  QFILE_LIST_MERGE_INFO *merge_info;

  HASHJOIN_CONTEXT single_context;
  HASHJOIN_CONTEXT *contexts;
  int context_cnt;

  QUERY_ID query_id;
  VAL_DESCR *vd;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;

  HASHJOIN_MERGE_METHOD merge_method;
  int qlist_flag;

#if defined (SERVER_MODE)
  /* *INDENT-OFF* */
  parallel_query::worker_manager_with_dedicated_pool * px_workpool;
  /* *INDENT-ON* */
#endif				/* defined (SERVER_MODE) */

  /* Pointer to a member of HASHJOIN_PROC_NODE. */
  HASHJOIN_STATS_GROUP *stats_group;
} HASHJOIN_MANAGER;

/*
 * Function Declarations
 */

int qexec_hash_join (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_ID query_id, VAL_DESCR * vd);

int hjoin_execute (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
int hjoin_split_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_INPUT_SPLIT_INFO * part_info,
		       HASH_SCAN_KEY * key);

#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

#endif /* _QUERY_HASH_JOIN_H_ */
