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
#define HASHJOIN_COLLISION_RATE 0
#define HASHJOIN_DUMP_PARTITION 0
#define HASHJOIN_DUMP_HASH_TABLE 0
#define HASHJOIN_DUMP_BUILD 0
#define HASHJOIN_DUMP_PROBE 0

/*
 * Forward Declarations
 */

namespace parallel_query
{
  class worker_manager;
}

struct xasl_node;
struct tp_domain;

typedef struct xasl_node XASL_NODE;
typedef struct tp_domain TP_DOMAIN;

/*
 * Enum & Typedef Definitions
 */

typedef enum hashjoin_status
{
  HASHJOIN_STATUS_NONE = 0,
  HASHJOIN_STATUS_FILL_NULL_VALUES,
  HASHJOIN_STATUS_TRY,
  HASHJOIN_STATUS_SINGLE,
  HASHJOIN_STATUS_PARTITION,
  HASHJOIN_STATUS_PARALLEL,
  HASHJOIN_STATUS_END,
  HASHJOIN_STATUS_ERROR
} HASHJOIN_STATUS;

typedef enum hashjoin_merge_method
{
  HASHJOIN_MERGE_COMBINE = 0,
  HASHJOIN_MERGE_APPEND,
  HASHJOIN_MERGE_CONNECT
} HASHJOIN_MERGE_METHOD;

typedef enum hashjoin_profile_step
{
  HASHJOIN_PROFILE_NONE = 0,
  HASHJOIN_PROFILE_BUILD_FETCH,	/* hjoin_fetch_key */
  HASHJOIN_PROFILE_BUILD_HASH,	/* qdata_hash_scan_key */
  HASHJOIN_PROFILE_BUILD_INSERT,	/* hjoin_build_key */
  HASHJOIN_PROFILE_PROBE_FETCH,	/* hjoin_fetch_key */
  HASHJOIN_PROFILE_PROBE_HASH,	/* qdata_hash_scan_key */
  HASHJOIN_PROFILE_PROBE_SEARCH,	/* hjoin_probe_key */
  HASHJOIN_PROFILE_PROBE_MATCH,	/* hjoin_fetch_key */
  HASHJOIN_PROFILE_PROBE_ADD,	/* hjoin_merge_tuple_to_list_id */
  HASHJOIN_PROFILE_MERGE	/* hjoin_merge_qlist */
} HASHJOIN_PROFILE_STEP;

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

typedef struct hashjoin_range_time_stats
{
  TSCTIMEVAL min;
  TSCTIMEVAL max;
} HASHJOIN_RANGE_TIME_STATS;
#define HASHJOIN_RANGE_TIME_STATS_INITIALIZER { { LONG_MAX, 999999 }, { 0, 0 } }

typedef struct hashjoin_input_stats
{
  TSCTIMEVAL elapsed_time;
  HASHJOIN_RANGE_TIME_STATS range_time;
  UINT64 fetches;
  UINT64 ioreads;
  UINT64 read_rows;
  UINT64 read_keys;
  UINT64 qualified_rows;
} HASHJOIN_INPUT_STATS;

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

  struct
  {
    TSCTIMEVAL elapsed_time;	/* hjoin_fetch_key */
    UINT64 fetches;
    UINT64 ioreads;
    UINT64 qualified_rows;
  } merge;
} HASHJOIN_PROFILE_STATS;
#endif /* HASHJOIN_PROFILE_TIME */

typedef struct hashjoin_start_stats
{
  TSC_TICKS tick;
  UINT64 fetches;
  UINT64 ioreads;
  HASHJOIN_PROFILE_STEP step;
} HASHJOIN_START_STATS;
#define HASHJOIN_START_STATS_INITIALIZER { { 0 }, 0, 0, HASHJOIN_PROFILE_NONE }

typedef struct hashjoin_stats
{
  UINT32 num_parallel_threads;

  HASH_METHOD hash_method;
  bool use_hash_memory;
  bool use_hash_hybrid;
  bool use_hash_file;
  bool use_hash_skip;

  bool swap_join_inputs;

  double collision_rate;

  HASHJOIN_INPUT_STATS split;
  HASHJOIN_INPUT_STATS parallel;
  HASHJOIN_INPUT_STATS build;
  HASHJOIN_INPUT_STATS probe;

#if HASHJOIN_PROFILE_TIME
  HASHJOIN_INPUT_STATS merge;
  HASHJOIN_PROFILE_STATS profile;
#endif				/* HASHJOIN_PROFILE_TIME */
} HASHJOIN_STATS;

typedef struct hashjoin_stats_group
{
  HASHJOIN_STATS stats;
  HASHJOIN_STATS *context_stats;
  UINT32 context_cnt;
} HASHJOIN_STATS_GROUP;

/* HASHJOIN_FETCH_INFO */
typedef struct hashjoin_fetch_info
{
  QFILE_LIST_ID *list_id;
  QFILE_LIST_SCAN_ID list_scan_id;
  QFILE_TUPLE_RECORD tuple_record;
  QFILE_TUPLE_RECORD *fill_record;

  /* Pointers to members of HASHJOIN_DOMAIN_INFO,
   * which is a member of HASHJOIN_PROC_NODE. */
  HASHJOIN_INPUT_DOMAIN_INFO *input;
  TP_DOMAIN **coerce_domains;
  bool need_coerce_domains;

  /* Pointer to a member of HASHJOIN_INPUT. */
  REGU_VARIABLE_LIST regu_list_pred;
} HASHJOIN_FETCH_INFO;

/* HASHJOIN_INPUT_SPLIT_INFO */
typedef struct hashjoin_input_split_info
{
  HASHJOIN_FETCH_INFO *fetch_info;
  QFILE_LIST_ID **part_list_id;
} HASHJOIN_INPUT_SPLIT_INFO;

/* HASHJOIN_SPLIT_INFO */
typedef struct hashjoin_split_info
{
  HASHJOIN_INPUT_SPLIT_INFO outer;
  HASHJOIN_INPUT_SPLIT_INFO inner;
} HASHJOIN_SPLIT_INFO;

/* HASHJOIN_SHARED_SPLIT_INFO */
typedef struct hashjoin_shared_split_info
{
  // *INDENT-OFF*
  QFILE_LIST_SECTOR_INFO sector_info;	/* sector-based page distribution (from qfile_collect_list_sector_info) */
  std::atomic<bool> membuf_claimed;	/* atomic flag: one worker claims all membuf pages */
  std::atomic<int> next_sector_index;	/* atomic index for sector distribution */
  std::mutex *part_mutexes;

  hashjoin_shared_split_info ()
    : sector_info (QFILE_LIST_SECTOR_INFO_INITIALIZER)
    , membuf_claimed (false)
    , next_sector_index (0)
    , part_mutexes (nullptr)
  {
    //
  }
  // *INDENT-ON*
} HASHJOIN_SHARED_SPLIT_INFO;

/* HASHJOIN_SHARED_JOIN_INFO */
typedef struct hashjoin_shared_join_info
{
  // *INDENT-OFF*
  std::mutex scan_mutex;
  SCAN_POSITION scan_position;
  UINT32 next_index;

  std::mutex stats_mutex;
  HASHJOIN_RANGE_TIME_STATS build_range_time;
  HASHJOIN_RANGE_TIME_STATS probe_range_time;

  hashjoin_shared_join_info ()
    : scan_mutex ()
    , scan_position (S_BEFORE)
    , next_index (0)
    , stats_mutex ()
    , build_range_time (HASHJOIN_RANGE_TIME_STATS_INITIALIZER)
    , probe_range_time (HASHJOIN_RANGE_TIME_STATS_INITIALIZER)
  {
    //
  }
  // *INDENT-ON*
} HASHJOIN_SHARED_JOIN_INFO;

/* HASHJOIN_CONTEXT*/
typedef struct hashjoin_context
{
  QFILE_LIST_ID *list_id;

  HASHJOIN_FETCH_INFO outer;
  HASHJOIN_FETCH_INFO inner;

  /* Set in hjoin_init_context or hjoin_outer_fill_null_values. */
  HASHJOIN_FETCH_INFO *build;
  HASHJOIN_FETCH_INFO *probe;

  HASH_LIST_SCAN hash_scan;
  PRED_EXPR *during_join_pred;
  VAL_DESCR *val_descr;

  HASHJOIN_STATUS status;

  /* Pointer to a member of HASHJOIN_MANAGER. */
  HASHJOIN_STATS *stats;
} HASHJOIN_CONTEXT;

/* HASHJOIN_MANAGER*/
typedef struct hashjoin_manager
{
  /* Pointer to a member of HASHJOIN_PROC_NODE. */
  HASHJOIN_INPUT *outer;
  HASHJOIN_INPUT *inner;
  QFILE_LIST_MERGE_INFO *merge_info;

  /* Copy of a member of QFILE_LIST_MERGE_INFO. */
  JOIN_TYPE join_type;
  int key_cnt;

  /* Pointer to a member of XASL_NODE. */
  PRED_EXPR *during_join_pred;
  int num_parallel_threads;

  /* Pointer to a member of XASL_STATE. */
  QUERY_ID query_id;
  VAL_DESCR *val_descr;

  HASHJOIN_CONTEXT single_context;
  HASHJOIN_CONTEXT *contexts;
  UINT32 context_cnt;

  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  HASHJOIN_MERGE_METHOD qlist_merge_method;
  int qlist_flag;

  // *INDENT-OFF*
  parallel_query::worker_manager *px_worker_manager;
  // *INDENT-ON*
  UINT64 *px_worker_stats;

  /* From HASHJOIN_PROC_NODE */
  HASHJOIN_STATS_GROUP *stats_group;

#if HASHJOIN_DUMP_HASH_TABLE
  pthread_mutex_t dump_hash_table_mutex;
#endif				/* HASHJOIN_DUMP_HASH_TABLE */
} HASHJOIN_MANAGER;

/*
 * Macro Function Declarations
 */

#if HASHJOIN_PROFILE_TIME
#define HJOIN_PROFILE_START(thread_p, start_stats_p, step) \
   if (thread_is_on_trace ((thread_p))) \
     { \
       hjoin_profile_start ((thread_p), (start_stats_p), (step)); \
     }
#define HJOIN_PROFILE_END(thread_p, stats_p, start_stats_p, step) \
   if (thread_is_on_trace ((thread_p))) \
     { \
       hjoin_profile_end ((thread_p), (stats_p), (start_stats_p), (step)); \
     }
#define HJOIN_PROFILE_MERGE_END(thread_p, stats_p, start_stats_p, step, rows) \
   if (thread_is_on_trace ((thread_p))) \
     { \
       assert ((step) == HASHJOIN_PROFILE_MERGE); \
       hjoin_profile_end ((thread_p), (stats_p), (start_stats_p), (step)); \
       (stats_p)->merge.qualified_rows = (rows); \
     }
#else
#define HJOIN_PROFILE_START(thread_p, start_stats, step) ((void) 0)
#define HJOIN_PROFILE_END(thread_p, stats_p, start_stats_p, step) ((void) 0)
#define HJOIN_PROFILE_MERGE_END(thread_p, stats_p, start_stats_p, step, rows) ((void) 0)
#endif /* HASHJOIN_PROFILE_TIME */

/*
 * Function Declarations
 */

int qexec_hash_join (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_ID query_id, VAL_DESCR * val_descr);

int hjoin_execute (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
int hjoin_merge_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);

int hjoin_init_shared_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				  HASHJOIN_SHARED_SPLIT_INFO * shared_info);
void hjoin_clear_shared_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				    HASHJOIN_SHARED_SPLIT_INFO * shared_info);

int hjoin_fetch_key (THREAD_ENTRY * thread_p, HASHJOIN_FETCH_INFO * fetch_info, QFILE_TUPLE_RECORD * tuple_record,
		     HASH_SCAN_KEY * key, HASH_SCAN_KEY * compare_key, bool * need_skip_next);
void hjoin_update_tuple_hash_key (THREAD_ENTRY * thread_p, QFILE_TUPLE_RECORD * tuple_record, UINT32 hash_key);

void hjoin_trace_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats);
void hjoin_trace_end (THREAD_ENTRY * thread_p, HASHJOIN_INPUT_STATS * stats, HASHJOIN_START_STATS * start_stats);
void hjoin_trace_merge_stats (HASHJOIN_STATS * stats, HASHJOIN_STATS * context_stats);

UINT64 *hjoin_trace_get_worker_stats (HASHJOIN_MANAGER * manager, int index);
void hjoin_trace_drain_worker_stats (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);

#if HASHJOIN_PROFILE_TIME
void hjoin_profile_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats, HASHJOIN_PROFILE_STEP step);
void hjoin_profile_end (THREAD_ENTRY * thread_p, HASHJOIN_PROFILE_STATS * stats, HASHJOIN_START_STATS * start_stats,
			HASHJOIN_PROFILE_STEP step);
#endif /* HASHJOIN_PROFILE_TIME */

#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

#endif /* _QUERY_HASH_JOIN_H_ */
