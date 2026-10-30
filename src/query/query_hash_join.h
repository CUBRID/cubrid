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
 * Forward Declarations
 */

namespace parallel_query
{
  class worker_manager;
}

struct xasl_node;
struct tp_domain;
struct DOMAIN_COMPARE;

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
  HASHJOIN_STATUS_PARALLEL_PROBE,
  HASHJOIN_STATUS_END,
  HASHJOIN_STATUS_ERROR
} HASHJOIN_STATUS;

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
  /* [key] the converter of this input's value into the key's common domain (coerce_domains), NULL none */
  TP_VALUE_CONVERTER *converters;
  /* [key] the comparison of the two inputs' values once converted: tp_value_compare without coercion, decided */
  DOMAIN_COMPARE *compares;
} HASHJOIN_INPUT_DOMAIN_INFO;

typedef struct hashjoin_domain_info
{
  HASHJOIN_INPUT_DOMAIN_INFO outer;
  HASHJOIN_INPUT_DOMAIN_INFO inner;

  /* Common domains of build and probe inputs. */
  TP_DOMAIN **coerce_domains;

  /* the compiler decided the join from the inputs' column domains (XASL stream) */
  bool decided;
} HASHJOIN_DOMAIN_INFO;

#if defined (SERVER_MODE) || defined (SA_MODE)

typedef struct hashjoin_range_time_stats
{
  TSCTIMEVAL min;
  TSCTIMEVAL max;
} HASHJOIN_RANGE_TIME_STATS;
#define HASHJOIN_RANGE_TIME_STATS_INITIALIZER { { LONG_MAX, 999999 }, { 0, 0 } }

typedef struct hashjoin_range_rows_stats
{
  UINT64 min;
  UINT64 max;
} HASHJOIN_RANGE_ROWS_STATS;
#define HASHJOIN_RANGE_ROWS_STATS_INITIALIZER { LONG_MAX, 0 }

typedef struct hashjoin_input_stats
{
  TSCTIMEVAL elapsed_time;
  UINT64 fetches;
  UINT64 ioreads;
  UINT64 read_rows;
  UINT64 read_keys;
  UINT64 qualified_rows;
} HASHJOIN_INPUT_STATS;

typedef struct hashjoin_range_stats
{
  HASHJOIN_RANGE_TIME_STATS elapsed_time;
  HASHJOIN_RANGE_ROWS_STATS read_rows;
  HASHJOIN_RANGE_ROWS_STATS read_keys;
  HASHJOIN_RANGE_ROWS_STATS qualified_rows;
} HASHJOIN_RANGE_STATS;
#define HASHJOIN_RANGE_STATS_INITIALIZER \
  { HASHJOIN_RANGE_TIME_STATS_INITIALIZER, \
    HASHJOIN_RANGE_ROWS_STATS_INITIALIZER, \
    HASHJOIN_RANGE_ROWS_STATS_INITIALIZER, \
    HASHJOIN_RANGE_ROWS_STATS_INITIALIZER }

// *INDENT-OFF*
typedef struct hashjoin_build_stats : hashjoin_input_stats
{
  HASHJOIN_RANGE_TIME_STATS range_elapsed_time;
} HASHJOIN_BUILD_STATS;
// *INDENT-ON*

// *INDENT-OFF*
typedef struct hashjoin_probe_stats : hashjoin_input_stats
{
  HASHJOIN_RANGE_STATS range;
} HASHJOIN_PROBE_STATS;
// *INDENT-ON*

typedef struct hashjoin_start_stats
{
  TSC_TICKS tick;
  UINT64 fetches;
  UINT64 ioreads;
} HASHJOIN_START_STATS;
#define HASHJOIN_START_STATS_INITIALIZER { { 0 }, 0, 0 }

typedef struct hashjoin_stats
{
  UINT32 num_parallel_threads;

  HASH_METHOD hash_method;
  bool use_hash_memory;
  bool use_hash_hybrid;
  bool use_hash_file;
  bool use_hash_skip;

  bool swap_join_inputs;

  HASHJOIN_INPUT_STATS split;
  HASHJOIN_INPUT_STATS parallel;
  HASHJOIN_BUILD_STATS build;
  HASHJOIN_PROBE_STATS probe;
} HASHJOIN_STATS;

typedef struct hashjoin_stats_group
{
  HASHJOIN_STATUS status;
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

  /* True when the regu_list_pred values are ready in val_descr for the current tuple,
   * so a predicate can reuse them instead of preparing them again.
   * Also true when initialized to NULL for a null-padded build side.
   * Reset per probe row (probe side) and per candidate (build side). */
  bool is_ready;

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
  QFILE_LIST_SECTOR_SCAN_INFO sector_scan;

  std::mutex *part_mutexes;

  hashjoin_shared_split_info ()
    : sector_scan ()
    , part_mutexes (nullptr)
  {
    //
  }
  // *INDENT-ON*
} HASHJOIN_SHARED_SPLIT_INFO;

/* HASHJOIN_SHARED_PROBE_INFO */
typedef struct hashjoin_shared_probe_info
{
  // *INDENT-OFF*
  QFILE_LIST_SECTOR_SCAN_INFO sector_scan;

  std::mutex stats_mutex;
  HASHJOIN_RANGE_STATS probe_range;

  hashjoin_shared_probe_info ()
    : sector_scan ()
    , stats_mutex ()
    , probe_range HASHJOIN_RANGE_STATS_INITIALIZER
  {
    //
  }
  // *INDENT-ON*
} HASHJOIN_SHARED_PROBE_INFO;

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
    , build_range_time HASHJOIN_RANGE_TIME_STATS_INITIALIZER
    , probe_range_time HASHJOIN_RANGE_TIME_STATS_INITIALIZER
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

  OUTPTR_LIST *outptr_list;
  PRED_EXPR *during_join_pred;
  PRED_EXPR *after_join_pred;
  VAL_DESCR *val_descr;

  HASHJOIN_STATUS status;

  /* Pointer to a member of HASHJOIN_MANAGER. */
  HASHJOIN_STATS *stats;
} HASHJOIN_CONTEXT;

/* HASHJOIN_MANAGER*/
typedef struct hashjoin_manager
{
  /* Reference to members of HASHJOIN_PROC_NODE. */
  HASHJOIN_INPUT *outer;
  HASHJOIN_INPUT *inner;
  QFILE_LIST_MERGE_INFO *merge_info;
  bool has_ineligible;

  /* Copy of a member of QFILE_LIST_MERGE_INFO. */
  JOIN_TYPE join_type;
  int key_cnt;

  /* Copy of a member of XASL_NODE (parallelism) as the requested degree, then the workers reserved, or 0 when
   * hjoin_try_parallel () or hjoin_try_parallel_probe () decides on a serial join. */
  int num_parallel_threads;

  /* Copy of a member of XASL_STATE. */
  QUERY_ID query_id;

  /* max_hash_list_scan_size, read once per join */
  UINT64 mem_limit;

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
} HASHJOIN_MANAGER;

/*
 * Function Declarations
 */

int qexec_hash_join (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_ID query_id, VAL_DESCR * val_descr);

/* Hash Join Execution */
int hjoin_execute (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
int hjoin_merge_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);

/* Hash Join Shared Split Info */
int hjoin_init_shared_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				  HASHJOIN_SHARED_SPLIT_INFO * shared_info);
void hjoin_clear_shared_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				    HASHJOIN_SHARED_SPLIT_INFO * shared_info);

/* Hash List Scan */
int hjoin_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt, UINT64 mem_limit,
		     QFILE_LIST_ID * list_id);
void hjoin_scan_clear (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan);

/* Hash Join Processing */
TP_VALUE_CONVERTER hjoin_key_converter (int conv_kind, DB_TYPE source, const TP_DOMAIN * common);
int hjoin_fetch_key (THREAD_ENTRY * thread_p, HASHJOIN_FETCH_INFO * fetch_info, QFILE_TUPLE_RECORD * tuple_record,
		     HASH_SCAN_KEY * key, HASH_SCAN_KEY * compare_key, bool * need_skip_next);
void hjoin_update_tuple_hash_key (THREAD_ENTRY * thread_p, QFILE_TUPLE_RECORD * tuple_record, UINT32 hash_key);
int hjoin_probe_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_SCAN_ID * list_scan_id,
		     QFILE_TUPLE_RECORD * tuple_record);
DB_LOGICAL hjoin_eval_pred (THREAD_ENTRY * thread_p, HASHJOIN_FETCH_INFO * probe, HASHJOIN_FETCH_INFO * build,
			    PRED_EXPR * pred, VAL_DESCR * val_descr);

/* Merge QFILE_LIST_ID */
int hjoin_add_tuple_to_list_id (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context, QFILE_LIST_ID * list_id,
				QFILE_TUPLE_RECORD * outer_record, QFILE_TUPLE_RECORD * inner_record,
				QFILE_LIST_MERGE_INFO * merge_info, QFILE_TUPLE_RECORD * overflow_record);

void hjoin_trace_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats);
void hjoin_trace_end (THREAD_ENTRY * thread_p, HASHJOIN_INPUT_STATS * stats, HASHJOIN_START_STATS * start_stats);
void hjoin_trace_merge_stats (HASHJOIN_STATS * stats, HASHJOIN_STATS * context_stats, HASHJOIN_STATUS status);

UINT64 *hjoin_trace_get_worker_stats (HASHJOIN_MANAGER * manager, int index);
void hjoin_trace_drain_worker_stats (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);

#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

#endif /* _QUERY_HASH_JOIN_H_ */
