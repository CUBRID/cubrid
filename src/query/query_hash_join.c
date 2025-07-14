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
 * query_hash_join.c
 */

#include "query_hash_join.h"

#include "dbtype.h"		/* db_make_null */
#include "fetch.h"		/* fetch_val_list */
#include "list_file.h"		/* qfile_open_list, qfile_close_list */
#include "memory_alloc.h"	/* CEIL_PTVDIV */
#include "object_representation.h"	/* TP_DOMAIN */
#include "perf_monitor.h"	/* perfmon_get_from_statistic, PSTAT_... */
#include "query_list.h"		/* JOIN_TYPE */
#include "system_parameter.h"	/* prm_get_bigint_value, PRM_ID_... */
#include "tsc_timer.h"		/* TSC_TICKS, TSCTIMEVAL, TSC_ADD_TIMEVAL */
#include "xasl.h"		/* XASL_NODE, HASHJOIN_PROC_NODE */
#include "xasl_predicate.hpp"	/* PRED_EXPR */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * Debug Macros
 */

#define PARTITION_FILL_FACTOR 0.8

#define DUMP_HASH_TABLE_LIMIT 100
#define DUMP_PROBE_LIMIT 20

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
  HASHJOIN_STATUS_END,
  HASHJOIN_STATUS_ERROR
} HASHJOIN_STATUS;

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
  HASHJOIN_PROFILE_PROBE_ADD	/* hjoin_merge_tuple_to_list_id */
} HASHJOIN_PROFILE_STEP;

typedef enum hashjoin_print_step
{
  HASHJOIN_PRINT_NONE = 0,
  HASHJOIN_PRINT_READ_KEY,
  HASHJOIN_PRINT_NOT_MATCHED_KEY,
  HASHJOIN_PRINT_NOT_QUALIFIED_KEY,
  HASHJOIN_PRINT_QUALIFIED_KEY,
  HASHJOIN_PRINT_FILL_EMPTY_KEY
} HASHJOIN_PRINT_STEP;

typedef struct hashjoin_start_stats
{
  TSC_TICKS tick;
  UINT64 fetches;
  UINT64 fetch_time;
  UINT64 ioreads;
  HASHJOIN_PROFILE_STEP step;
} HASHJOIN_START_STATS;
#define HASHJOIN_START_STATS_INITIALIZER { { 0 }, 0, 0, 0 }

/*
 * Struct & Typedef Definitions
 */

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

typedef struct hashjoin_partition_info
{
  QFILE_LIST_ID *list_id;
  QFILE_LIST_ID **part_list_id;
  int part_cnt;
  HASHJOIN_INPUT_STATS *stats;
} HASHJOIN_PARTITION_INFO;
#define HASHJOIN_PARTITION_INFO_INITIALIZER { NULL, NULL, 0 , NULL}

typedef struct hashjoin_context
{
  QFILE_LIST_ID *outer_list_id;
  QFILE_LIST_ID *inner_list_id;

  HASHJOIN_FETCH_INFO outer_fetch_info;
  HASHJOIN_FETCH_INFO inner_fetch_info;
  int key_cnt;

  JOIN_TYPE join_type;
  PRED_EXPR *during_join_pred;

  HASH_LIST_SCAN hash_scan;
  bool is_build_outer;
  bool is_last_context;

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

  /* Pointer to a member of HASHJOIN_PROC_NODE. */
  HASHJOIN_STATS_GROUP *stats_group;
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
#else
#define HJOIN_PROFILE_START(thread_p, start_stats, step) ((void) 0)
#define HJOIN_PROFILE_END(thread_p, stats_p, start_stats_p, step) ((void) 0)
#endif /* HASHJOIN_PROFILE_TIME */

#if HASHJOIN_DUMP_HASH_TABLE
#define HJOIN_DUMP_HASH_TABLE(thread_p, hash_scan_p, list_id_p) \
  hjoin_dump_hash_table ((thread_p), (hash_scan_p), (list_id_p))
#else
#define HJOIN_DUMP_HASH_TABLE(thread_p, hash_scan_p, list_id_p) ((void) 0)
#endif /* HASHJOIN_DUMP_HASH_TABLE */

#if !defined(NDEBUG) && HASHJOIN_DUMP_PROBE
#define HJOIN_PRINT_TUPLE(list_scan_id, tuple, step) \
  hjoin_print_tuple ((list_scan_id), (tuple), (step))
#else
#define HJOIN_PRINT_TUPLE(list_scan_id, tuple, step) ((void) 0)
#endif /* !NDEBUG && HASHJOIN_DUMP_PROBE */

/*
 * Function Declarations
 */

/* Hash Join Execution */
static QFILE_LIST_ID *hjoin_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);
static QFILE_LIST_ID *hjoin_with_context (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
					  HASHJOIN_CONTEXT * context);
static QFILE_LIST_ID *hjoin_outer_fill_null_values (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
						    HASHJOIN_CONTEXT * context);
static QFILE_LIST_ID *hjoin_internal (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);

/* Hash Join Manager */
static int hjoin_init_manager (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, XASL_NODE * xasl,
			       QUERY_ID query_id, VAL_DESCR * vd);
static void hjoin_clear_manager (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);

/* Hash Join Domain Info */
static int hjoin_init_domain_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				   HASHJOIN_DOMAIN_INFO * domain_info);

/* Hash Join Partitioning */
static HASHJOIN_STATUS hjoin_make_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);
static int hjoin_make_partition_input (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				       HASHJOIN_PARTITION_INFO * part_info, HASHJOIN_FETCH_INFO * fetch_info,
				       bool is_null_allowed, HASH_SCAN_KEY * key);

/* Hash Join Context */
static int hjoin_init_context (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
static void hjoin_clear_contexts (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context);

/* Hash List Scan */
static int hjoin_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt, QFILE_LIST_ID * list_id);
static void hjoin_scan_clear (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan);

/* Hash Join Processing */
static HASHJOIN_STATUS hjoin_check_empty_inputs (HASHJOIN_CONTEXT * context);
static int hjoin_fetch_key (THREAD_ENTRY * thread_p, HASHJOIN_FETCH_INFO * fetch_info,
			    QFILE_TUPLE_RECORD * tuple_record, HASH_SCAN_KEY * key,
			    HASH_SCAN_KEY * compare_key, bool * need_skip_next);

/* Build Phase */
static int hjoin_build (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			QFILE_LIST_SCAN_ID * list_scan_id);
static int hjoin_build_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan,
			    QFILE_LIST_SCAN_ID * list_scan_id, QFILE_TUPLE_RECORD * tuple_record);

/* Probe Phase */
static int hjoin_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			QFILE_LIST_SCAN_ID * build_scan_id, QFILE_LIST_SCAN_ID * probe_scan_id,
			QFILE_LIST_ID * list_id);
static int hjoin_outer_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			      QFILE_LIST_SCAN_ID * build_scan_id, QFILE_LIST_SCAN_ID * probe_scan_id,
			      QFILE_LIST_ID * list_id);
static int hjoin_probe_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan,
			    QFILE_LIST_SCAN_ID * list_scan_id, QFILE_TUPLE_RECORD * tuple_record);

/* Merge QFILE_LIST_ID */
static int hjoin_merge_tuple_to_list_id (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
					 QFILE_TUPLE_RECORD * outer_record, QFILE_TUPLE_RECORD * inner_record,
					 QFILE_LIST_MERGE_INFO * merge_info, QFILE_TUPLE_RECORD * overflow_record);
static int hjoin_merge_tuple (THREAD_ENTRY * thread_p, QFILE_TUPLE_RECORD * outer_record,
			      QFILE_TUPLE_RECORD * inner_record, QFILE_LIST_MERGE_INFO * merge_info,
			      QFILE_TUPLE_RECORD * overflow_record);

/* Trace */
static void hjoin_trace_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats);
static void hjoin_trace_end (THREAD_ENTRY * thread_p, HASHJOIN_INPUT_STATS * stats, HASHJOIN_START_STATS * start_stats);
static void hjoin_trace_skew (QFILE_LIST_ID * list_id, QFILE_LIST_ID ** part_list_id, unsigned int part_cnt,
			      HASHJOIN_INPUT_STATS * stats);

#if HASHJOIN_PROFILE_TIME
static void hjoin_profile_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats,
				 HASHJOIN_PROFILE_STEP step);
static void hjoin_profile_end (THREAD_ENTRY * thread_p, HASHJOIN_PROFILE_STATS * stats,
			       HASHJOIN_START_STATS * start_stats, HASHJOIN_PROFILE_STEP step);
#endif /* HASHJOIN_PROFILE_TIME */

static void hjoin_trace_merge_stats (HASHJOIN_STATS * stats, HASHJOIN_STATS * context_stats);

/* Sanity Check */
static void hjoin_check_valid_part_info (HASHJOIN_PARTITION_INFO * info);

/* Dump */
#if HASHJOIN_DUMP_HASH_TABLE
static void hjoin_dump_hash_table (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_ID * list_id);
#endif /* HASHJOIN_DUMP_HASH_TABLE */

#if !defined(NDEBUG) && HASHJOIN_DUMP_PROBE
static void hjoin_print_tuple (QFILE_TUPLE_VALUE_TYPE_LIST * type_list_p, QFILE_TUPLE tuple, HASHJOIN_PRINT_STEP step);
#endif /* !NDEBUG && HASHJOIN_DUMP_PROBE */

/*
 * Function Definitions
 */

/*
 * qexec_hash_join() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   xasl(in): XASL node for hash join execution.
 *   query_id(in): Query identifier.
 *   vd(in): Value descriptor for positional values.
 */
int
qexec_hash_join (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_ID query_id, VAL_DESCR * vd)
{
  QFILE_LIST_ID *list_id = NULL;

  HASHJOIN_MANAGER manager;
  HASHJOIN_CONTEXT *single_context;
  HASHJOIN_STATUS status, part_status;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (xasl != NULL);
  assert (query_id != NULL_QUERY_ID);

  error = hjoin_init_manager (thread_p, &manager, xasl, query_id, vd);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  single_context = &manager.single_context;

  status = hjoin_check_empty_inputs (single_context);
  switch (status)
    {
    case HASHJOIN_STATUS_FILL_NULL_VALUES:
      list_id = hjoin_outer_fill_null_values (thread_p, &manager, single_context);
      break;

    case HASHJOIN_STATUS_TRY:
      part_status = hjoin_make_partition (thread_p, &manager);
      switch (part_status)
	{
	case HASHJOIN_STATUS_SINGLE:
	  list_id = hjoin_with_context (thread_p, &manager, single_context);
	  break;

	case HASHJOIN_STATUS_PARTITION:
	  list_id = hjoin_partition (thread_p, &manager);
	  break;

	case HASHJOIN_STATUS_END:
	  /* impossible case */
	  /* hjoin_check_empty_inputs guarantees STATUS_END cannot occur here */
	  assert_release (false);
	  goto error_exit;

	case HASHJOIN_STATUS_ERROR:
	  /* hjoin_make_partition always retries as HASHJOIN_STATUS_SINGLE;
	   * except for ER_INTERRUPTED, never returns HASHJOIN_STATUS_ERROR */
	  error = er_errid ();
	  assert_release (error == ER_INTERRUPTED);
	  goto error_exit;

	default:
	  /* impossible case */
	  assert_release (false);
	  goto error_exit;
	}
      break;

    case HASHJOIN_STATUS_END:
      /* Nothing to do */
      assert (list_id == NULL);
      break;

    case HASHJOIN_STATUS_ERROR:
      [[fallthrough]];
    default:
      /* impossible case */
      assert_release (false);
      goto error_exit;
    }

  if (list_id != NULL)
    {
      assert (list_id->last_pgptr == NULL);

      qfile_destroy_list (thread_p, xasl->list_id);	/* may be unnecessary */
      qfile_copy_list_id (xasl->list_id, list_id, false);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }
  else if (status == HASHJOIN_STATUS_END)
    {
      ASSERT_NO_ERROR ();
    }
  else
    {
      /* list_id can be NULL when join result is empty */
      error = er_errid ();
    }

cleanup:
  hjoin_clear_manager (thread_p, &manager);

  return error;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  goto cleanup;
}

/*
 * hjoin_partition() -
 *   return: List identifier containing the join result; NULL if no result or on error.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 */
static QFILE_LIST_ID *
hjoin_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  QFILE_LIST_ID *list_id = NULL, *t_list_id = NULL;

  HASHJOIN_CONTEXT *current_context;
  QFILE_LIST_ID *context_list_id = NULL;
  int context_cnt, context_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  HASHJOIN_STATS *total_stats = &manager->stats_group->stats;

  context_cnt = manager->context_cnt;

  for (context_index = 0; context_index < context_cnt; context_index++)
    {
      current_context = &manager->contexts[context_index];

      context_list_id = hjoin_with_context (thread_p, manager, current_context);

      if (thread_is_on_trace (thread_p))
	{
	  hjoin_trace_merge_stats (total_stats, current_context->stats);
	}

      if (context_list_id == NULL)
	{
	  error = er_errid ();
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  else
	    {
	      /* list_id can be NULL when join result is empty */
	      continue;
	    }
	}

      if (list_id != NULL)
	{
	  t_list_id = qfile_combine_two_list (thread_p, list_id, context_list_id, QFILE_FLAG_ALL | QFILE_FLAG_UNION);
	  if (t_list_id == NULL)
	    {
	      goto error_exit;
	    }

	  qfile_destroy_list (thread_p, list_id);
	  QFILE_FREE_AND_INIT_LIST_ID (list_id);

	  qfile_destroy_list (thread_p, context_list_id);
	  QFILE_FREE_AND_INIT_LIST_ID (context_list_id);

	  list_id = t_list_id;
	  t_list_id = NULL;
	}
      else
	{
	  list_id = context_list_id;
	}
    }

  ASSERT_NO_ERROR ();

  assert (list_id->last_pgptr == NULL);

  return list_id;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
    }

  if (context_list_id != NULL)
    {
      qfile_close_list (thread_p, context_list_id);
      qfile_destroy_list (thread_p, context_list_id);
      QFILE_FREE_AND_INIT_LIST_ID (context_list_id);
    }

  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  return NULL;
}

/*
 * hjoin_with_context() -
 *   return: List identifier containing the join result; NULL if no result or on error.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
static QFILE_LIST_ID *
hjoin_with_context (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *list_id = NULL;
  HASHJOIN_STATUS status;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  status = hjoin_check_empty_inputs (context);

  /* In outer joins, tuples with NULL in any join column are placed in the last partition.
   * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition. */
  if (context->is_last_context && IS_OUTER_JOIN_TYPE (context->join_type))
    {
      status = (status == HASHJOIN_STATUS_TRY) ? HASHJOIN_STATUS_FILL_NULL_VALUES : status;
    }

  switch (status)
    {
    case HASHJOIN_STATUS_FILL_NULL_VALUES:
      assert (context != &manager->single_context);
      list_id = hjoin_outer_fill_null_values (thread_p, manager, context);
      break;

    case HASHJOIN_STATUS_TRY:
      list_id = hjoin_internal (thread_p, manager, context);
      break;

    case HASHJOIN_STATUS_END:
      /* Nothing to do */
      assert (list_id == NULL);
      break;

    case HASHJOIN_STATUS_ERROR:
    default:
      if (er_errid () == NO_ERROR)
	{
	  assert_release (false);
	}
      return NULL;
    }

  assert (list_id->last_pgptr == NULL);

  return list_id;
}

/*
 * hjoin_outer_fill_null_values() -
 *   return: List identifier containing the join result; NULL if no result or on error.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
static QFILE_LIST_ID *
hjoin_outer_fill_null_values (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *list_id = NULL;
  QFILE_LIST_ID *outer_list_id = NULL, *inner_list_id = NULL;
  QFILE_LIST_SCAN_ID outer_scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  QFILE_TUPLE_RECORD *left_record;
  QFILE_TUPLE_RECORD *right_record;
  SCAN_CODE scan_code;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  /* Prevent faults when qfile_close_scan is called */
  outer_scan_id.status = S_CLOSED;

  if (context->join_type == JOIN_LEFT)
    {
      outer_list_id = context->outer_list_id;
      inner_list_id = context->inner_list_id;
      left_record = &tuple_record;
      right_record = NULL;
    }
  else if (context->join_type == JOIN_RIGHT)
    {
      outer_list_id = context->inner_list_id;
      inner_list_id = context->outer_list_id;
      left_record = NULL;
      right_record = &tuple_record;
    }
  else
    {
      /* impossible case */
      assert_release (false);
      goto error_exit;
    }

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, QFILE_FLAG_ALL, NULL);
  if (list_id == NULL)
    {
      goto error_exit;
    }

  error = qfile_open_list_scan (outer_list_id, &outer_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
      stats->build.part_rows = inner_list_id->tuple_cnt;
      stats->probe.part_rows = outer_list_id->tuple_cnt;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &outer_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error = hjoin_merge_tuple_to_list_id (thread_p, list_id, left_record, right_record, manager->merge_info,
					    &overflow_record);
      if (error != NO_ERROR)
	{
	  break;
	}

      if (thread_is_on_trace (thread_p))
	{
	  stats->probe.rows++;
	}
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->probe, &start_stats);
      stats->build.rows = inner_list_id->tuple_cnt;
      assert (stats->probe.readkeys == 0);
    }

  /* After qfile_open_list_scan, if an error occurs,
   * ensure qfile_close_scan runs here
   * before jumping to error_exit. */
  qfile_close_scan (thread_p, &outer_scan_id);

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto error_exit;
    }

  qfile_close_list (thread_p, list_id);

  ASSERT_NO_ERROR ();

cleanup:
  if (overflow_record.tpl != NULL)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  qfile_close_list (thread_p, outer_list_id);
  qfile_destroy_list (thread_p, outer_list_id);

  assert (list_id->last_pgptr == NULL);

  return list_id;

error_exit:
  if (er_errid () == NO_ERROR)
    {
      assert_release (false);
    }

  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  goto cleanup;
}

/*
 * hjoin_internal() -
 *   return: List identifier containing the join result; NULL if no result or on error.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
static QFILE_LIST_ID *
hjoin_internal (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *list_id = NULL;
  QFILE_LIST_ID *build_list_id = NULL, *probe_list_id = NULL;
  QFILE_LIST_SCAN_ID build_list_scan_id, probe_list_scan_id;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  /* Prevent faults when qfile_close_scan is called */
  build_list_scan_id.status = S_CLOSED;
  probe_list_scan_id.status = S_CLOSED;

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, QFILE_FLAG_ALL, NULL);
  if (list_id == NULL)
    {
      goto error_exit;
    }

  error = hjoin_init_context (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (context->is_build_outer)
    {
      build_list_id = context->outer_list_id;
      probe_list_id = context->inner_list_id;
    }
  else
    {
      build_list_id = context->inner_list_id;
      probe_list_id = context->outer_list_id;
    }

  error = qfile_open_list_scan (build_list_id, &build_list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = hjoin_build (thread_p, manager, context, &build_list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = qfile_open_list_scan (probe_list_id, &probe_list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (IS_OUTER_JOIN_TYPE (context->join_type))
    {
      error = hjoin_outer_probe (thread_p, manager, context, &build_list_scan_id, &probe_list_scan_id, list_id);
    }
  else
    {
      error = hjoin_probe (thread_p, manager, context, &build_list_scan_id, &probe_list_scan_id, list_id);
    }
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  qfile_close_list (thread_p, list_id);

  ASSERT_NO_ERROR ();

cleanup:
  qfile_close_scan (thread_p, &build_list_scan_id);
  qfile_close_scan (thread_p, &probe_list_scan_id);

  qfile_close_list (thread_p, build_list_id);
  qfile_destroy_list (thread_p, build_list_id);

  qfile_close_list (thread_p, probe_list_id);
  qfile_destroy_list (thread_p, probe_list_id);

  hjoin_scan_clear (thread_p, &context->hash_scan);

  assert (list_id->last_pgptr == NULL);

  return list_id;

error_exit:
  if (er_errid () == NO_ERROR)
    {
      assert_release (false);
    }

  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  goto cleanup;
}

/*
 * hjoin_init_manager() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in/out): Hash join manager to initialize.
 *   xasl(in): XASL node for hash join execution.
 *   query_id(in): Query identifier.
 *   vd(in): Value descriptor for positional values.
 */
static int
hjoin_init_manager (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, XASL_NODE * xasl, QUERY_ID query_id,
		    VAL_DESCR * vd)
{
  HASHJOIN_PROC_NODE *proc;
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  HASHJOIN_DOMAIN_INFO *domain_info;
  HASHJOIN_CONTEXT *context;

  QFILE_TUPLE_VALUE_TYPE_LIST *type_list = NULL;
  int type_cnt, type_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (xasl != NULL);

  memset (manager, 0, sizeof (HASHJOIN_MANAGER));

  proc = &xasl->proc.hashjoin;

  merge_info = &proc->merge_info;
  assert (merge_info->ls_pos_cnt > 0);
  assert (merge_info->ls_pos_list != NULL);
  manager->merge_info = merge_info;

  manager->outer = &proc->outer;
  manager->inner = &proc->inner;
  assert (manager->outer->xasl != NULL);
  assert (manager->inner->xasl != NULL);

  outer_list_id = manager->outer->xasl->list_id;
  inner_list_id = manager->inner->xasl->list_id;
  assert (outer_list_id != NULL);
  assert (inner_list_id != NULL);

  /* When aptr_list is executed in qexec_execute_mainblock_internal,
   * it checks the results from outer_xasl and inner_xasl in merge_info.
   * If either has no result, the other is skipped,
   * and the skipped node can have a type count of 0 in list_id.type_list. */
  if (outer_list_id->type_list.type_cnt == 0 || inner_list_id->type_list.type_cnt == 0)
    {
      return NO_ERROR;
    }

  assert (outer_list_id->type_list.domp != NULL);
  assert (inner_list_id->type_list.domp != NULL);

  domain_info = &proc->domain_info;
  error = hjoin_init_domain_info (thread_p, manager, domain_info);
  if (error != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  assert_release (false);
	  return er_errid ();
	}
      return error;
    }

  /* single_context */
  context = &manager->single_context;

  context->outer_list_id = outer_list_id;
  context->inner_list_id = inner_list_id;

  context->outer_fetch_info.input = &domain_info->outer;
  context->outer_fetch_info.coerce_domains = domain_info->coerce_domains;
  context->outer_fetch_info.need_coerce_domains = domain_info->need_coerce_domains;
  context->outer_fetch_info.regu_list_pred = proc->outer.regu_list_pred;

  context->inner_fetch_info.input = &domain_info->inner;
  context->inner_fetch_info.coerce_domains = domain_info->coerce_domains;
  context->inner_fetch_info.need_coerce_domains = domain_info->need_coerce_domains;
  context->inner_fetch_info.regu_list_pred = proc->inner.regu_list_pred;

  context->key_cnt = merge_info->ls_column_cnt;

  context->join_type = merge_info->join_type;
  context->during_join_pred = xasl->during_join_pred;

  assert (context->hash_scan.hash_list_scan_type == HASH_METH_NOT_USE);
  assert (context->is_build_outer == false);
  assert (context->is_last_context == false);

  /* contexts */
  assert (manager->contexts == NULL);
  assert (manager->context_cnt == 0);

  /* query_id, vd */
  manager->query_id = query_id;
  manager->vd = vd;

  /* type_list */
  type_list = &manager->type_list;
  assert (type_list->domp == NULL);
  assert (type_list->type_cnt == 0);

  type_cnt = merge_info->ls_pos_cnt;

  type_list->domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *) * type_cnt);
  if (type_list->domp == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (TP_DOMAIN *) * type_cnt);
      return error;
    }

  type_list->type_cnt = type_cnt;

  for (type_index = 0; type_index < type_cnt; type_index++)
    {
      if (merge_info->ls_outer_inner_list[type_index] == QFILE_OUTER_LIST)
	{
	  type_list->domp[type_index] = outer_list_id->type_list.domp[merge_info->ls_pos_list[type_index]];
	}
      else
	{
	  type_list->domp[type_index] = inner_list_id->type_list.domp[merge_info->ls_pos_list[type_index]];
	}
    }

  /* stats_group */
  if (thread_is_on_trace (thread_p))
    {
      manager->stats_group = &proc->stats_group;
      memset (manager->stats_group, 0, sizeof (HASHJOIN_STATS_GROUP));

      context->stats = &manager->stats_group->stats;
    }
  else
    {
      assert (manager->stats_group == NULL);
      assert (context->stats == NULL);
    }

  ASSERT_NO_ERROR ();
  return NO_ERROR;
}

/*
 * hjoin_clear_manager() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager to clear.
 */
static void
hjoin_clear_manager (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  HASHJOIN_CONTEXT *contexts = NULL;
  int context_cnt, context_index;

  assert (thread_p != NULL);
  assert (manager != NULL);

  if (manager->type_list.domp != NULL)
    {
      db_private_free_and_init (thread_p, manager->type_list.domp);
    }

  if (manager->contexts != NULL)
    {
      contexts = manager->contexts;
      context_cnt = manager->context_cnt;

      for (context_index = 0; context_index < context_cnt; context_index++)
	{
	  hjoin_clear_contexts (thread_p, &contexts[context_index]);
	}

      db_private_free_and_init (thread_p, contexts);

      manager->contexts = NULL;
      manager->context_cnt = 0;
    }
  else
    {
      assert (manager->context_cnt == 0);
    }
}

/*
 * hjoin_clear_manager() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   domain_info(in/out): Domain information for join columns.
 */
static int
hjoin_init_domain_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_DOMAIN_INFO * domain_info)
{
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;

  TP_DOMAIN **outer_domains, **inner_domains, **coerce_domains;
  int *outer_value_indexes, *inner_value_indexes;
  int outer_value_index, inner_value_index;
  int domain_cnt, domain_index;
  int skip_index;
  bool need_coerce_domains;

  DB_TYPE outer_type, inner_type, common_type;
  int outer_precision, inner_precision;
  int outer_scale, inner_scale;
  int outer_integral, inner_integral;
  int common_precision, common_scale;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (domain_info != NULL);

  /* Do not perform NULL checks; 
   * validation is expected to be handled by the caller */
  merge_info = manager->merge_info;
  outer_list_id = manager->outer->xasl->list_id;
  inner_list_id = manager->inner->xasl->list_id;

  /* domain_info */
  domain_cnt = merge_info->ls_column_cnt;

  outer_domains = domain_info->outer.domains;
  outer_value_indexes = domain_info->outer.value_indexes;
  assert (outer_domains != NULL);
  assert (outer_value_indexes != NULL);

  inner_domains = domain_info->inner.domains;
  inner_value_indexes = domain_info->inner.value_indexes;
  assert (inner_domains != NULL);
  assert (inner_value_indexes != NULL);

  coerce_domains = domain_info->coerce_domains;
  need_coerce_domains = domain_info->need_coerce_domains = false;

  memset (coerce_domains, 0, sizeof (TP_DOMAIN *) * domain_cnt);

  /* This code references tp_infer_common_domain but reduces unnecessary calls to tp_domain_new. */
  for (domain_index = 0; domain_index < domain_cnt; domain_index++)
    {
      outer_value_index = outer_value_indexes[domain_index];
      inner_value_index = inner_value_indexes[domain_index];

      outer_domains[domain_index] = outer_list_id->type_list.domp[outer_value_index];
      inner_domains[domain_index] = inner_list_id->type_list.domp[inner_value_index];
      assert (outer_domains[domain_index] != NULL);
      assert (inner_domains[domain_index] != NULL);

      outer_type = TP_DOMAIN_TYPE (outer_domains[domain_index]);
      inner_type = TP_DOMAIN_TYPE (inner_domains[domain_index]);

      /* common_type */
      if (outer_type == inner_type)
	{
	  common_type = outer_type;
	}
      else
	{
	  need_coerce_domains = true;

	  if (outer_type == DB_TYPE_NULL)
	    {
	      assert (false);
	      coerce_domains[domain_index] = inner_domains[domain_index];
	      continue;
	    }
	  else if (inner_type == DB_TYPE_NULL)
	    {
	      assert (false);
	      coerce_domains[domain_index] = outer_domains[domain_index];
	      continue;
	    }
	  else
	    {
	      common_type = (tp_more_general_type (outer_type, inner_type) > 0) ? outer_type : inner_type;
	    }
	}

      /* outer_precision, outer_scale */
      outer_precision = outer_domains[domain_index]->precision;
      outer_scale = outer_domains[domain_index]->scale;

      inner_precision = inner_domains[domain_index]->precision;
      inner_scale = inner_domains[domain_index]->scale;

      if (outer_precision == inner_precision && outer_scale == inner_scale)
	{
	  common_precision = inner_precision;
	  common_scale = inner_scale;
	}
      else
	{
	  need_coerce_domains = true;

	  if (outer_precision == TP_FLOATING_PRECISION_VALUE || inner_precision == TP_FLOATING_PRECISION_VALUE)
	    {
	      common_precision = TP_FLOATING_PRECISION_VALUE;
	      common_scale = 0;
	    }
	  else if (common_type == DB_TYPE_NUMERIC)
	    {
	      common_scale = MAX (outer_scale, inner_scale);

	      outer_integral = outer_precision - outer_scale;
	      inner_integral = inner_precision - inner_scale;

	      common_precision = MAX (outer_integral, inner_integral) + common_scale;
	      common_precision = MIN (common_precision, DB_MAX_NUMERIC_PRECISION);
	    }
	  else
	    {
	      common_precision = MAX (outer_precision, inner_precision);
	      common_scale = 0;
	    }
	}

      /* need_coerce_domains, coerce_domains */
      if (need_coerce_domains)
	{
	  if (common_type == outer_type && common_precision == outer_precision && common_scale == outer_scale)
	    {
	      coerce_domains[domain_index] = outer_domains[domain_index];
	    }
	  else if (common_type == inner_type && common_precision == inner_precision && common_scale == inner_scale)
	    {
	      coerce_domains[domain_index] = inner_domains[domain_index];
	    }
	  else
	    {
	      coerce_domains[domain_index] =
		tp_domain_copy ((common_type == outer_type) ? outer_domains[domain_index] : inner_domains[domain_index],
				false);
	      if (coerce_domains[domain_index] == NULL)
		{
		  if (er_errid () == NO_ERROR)
		    {
		      assert_release (false);
		    }
		  return er_errid ();
		}

	      coerce_domains[domain_index]->precision = common_precision;
	      coerce_domains[domain_index]->scale = common_scale;

	      coerce_domains[domain_index] = tp_domain_cache (coerce_domains[domain_index]);
	    }
	}
    }				/* for (domain_index < domain_cnt) */

#if !defined (NDEBUG)
  if (!need_coerce_domains)
    {
      for (domain_index = 0; domain_index < domain_cnt; domain_index++)
	{
	  assert (coerce_domains[domain_index] == NULL);
	}
    }
#endif /* !NDEBUG */

  /* If join predicates compare different types, need_coerce_domains is set to true;
   * otherwise, it is false.
   * 
   * When need_coerce_domains is true, coerce_domains is set to inner_domains,
   * outer_domains, or a common domain for comparison.
   *
   * If either inner_domains or outer_domains matches coerce_domains, 
   * no coercion is needed. Otherwise, values are coerced to the common domain. */
  domain_info->need_coerce_domains = need_coerce_domains;

  ASSERT_NO_ERROR ();
  return NO_ERROR;
}

/*
 * hjoin_make_partition() -
 *   return: One of the following HASHJOIN_STATUS values:
 *           - HASHJOIN_STATUS_PARTITION: Partitioning is applied.
 *           - HASHJOIN_STATUS_SINGLE: Partitioning is not needed or falls back on error.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 */
static HASHJOIN_STATUS
hjoin_make_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  QFILE_LIST_ID **outer_part_list_id = NULL, **inner_part_list_id = NULL;

  UINT64 mem_limit;
  INT64 min_tuple_cnt;
  int part_cnt, part_index;

  HASHJOIN_CONTEXT *single_context, *contexts = NULL;
  HASHJOIN_STATUS status;

  HASHJOIN_PARTITION_INFO part_info = HASHJOIN_PARTITION_INFO_INITIALIZER;
  HASH_SCAN_KEY *part_key = NULL;
  bool is_outer_join;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
  assert (mem_limit > 0);

  HASHJOIN_STATS *stats = &manager->stats_group->stats;
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  /* Do not perform NULL checks; 
   * validation is expected to be handled by the caller */
  merge_info = manager->merge_info;
  single_context = &manager->single_context;
  outer_list_id = single_context->outer_list_id;
  inner_list_id = single_context->inner_list_id;
  is_outer_join = IS_OUTER_JOIN_TYPE (single_context->join_type);

  min_tuple_cnt = (outer_list_id->tuple_cnt < inner_list_id->tuple_cnt) ? outer_list_id->tuple_cnt :
    inner_list_id->tuple_cnt;

  part_cnt = CEIL_PTVDIV (min_tuple_cnt * (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)),
			  mem_limit * PARTITION_FILL_FACTOR);
  if (part_cnt <= 1)
    {
      /* give up */
      assert (manager->context_cnt == 0);
      status = HASHJOIN_STATUS_SINGLE;
      goto cleanup;
    }

  if (is_outer_join)
    {
      /* In outer joins, tuples with NULL in any join column are placed in the last partition.
       * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in this partition. */
      part_cnt += 1;
    }

  manager->context_cnt = part_cnt;

  contexts = (HASHJOIN_CONTEXT *) db_private_alloc (thread_p, sizeof (HASHJOIN_CONTEXT) * part_cnt);
  if (contexts == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (HASHJOIN_CONTEXT) * part_cnt);
      goto error_exit;
    }
  memset (contexts, 0, sizeof (HASHJOIN_CONTEXT) * part_cnt);

  outer_part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, sizeof (QFILE_LIST_ID *) * part_cnt);
  if (outer_part_list_id == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (QFILE_LIST_ID *) * part_cnt);
      goto error_exit;
    }
  memset (outer_part_list_id, 0, sizeof (QFILE_LIST_ID *) * part_cnt);

  inner_part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, sizeof (QFILE_LIST_ID *) * part_cnt);
  if (inner_part_list_id == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (QFILE_LIST_ID *) * part_cnt);
      goto error_exit;
    }
  memset (inner_part_list_id, 0, sizeof (QFILE_LIST_ID *) * part_cnt);

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      memcpy (&contexts[part_index], single_context, sizeof (HASHJOIN_CONTEXT));

      outer_part_list_id[part_index] =
	qfile_open_list (thread_p, &outer_list_id->type_list, NULL, outer_list_id->query_id, QFILE_FLAG_ALL, NULL);
      if (outer_part_list_id[part_index] == NULL)
	{
	  goto error_exit;
	}
      contexts[part_index].outer_list_id = outer_part_list_id[part_index];

      inner_part_list_id[part_index] =
	qfile_open_list (thread_p, &inner_list_id->type_list, NULL, inner_list_id->query_id, QFILE_FLAG_ALL, NULL);
      if (inner_part_list_id[part_index] == NULL)
	{
	  goto error_exit;
	}
      contexts[part_index].inner_list_id = inner_part_list_id[part_index];

      if (part_index == part_cnt - 1)
	{
	  contexts[part_index].is_last_context = true;
	}
    }

  manager->contexts = contexts;

  if (thread_is_on_trace (thread_p))
    {
      manager->stats_group->context_stats = (HASHJOIN_STATS *) malloc (sizeof (HASHJOIN_STATS) * part_cnt);
      if (manager->stats_group->context_stats == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (HASHJOIN_STATS) * part_cnt);
	  goto error_exit;
	}
      memset (manager->stats_group->context_stats, 0, sizeof (HASHJOIN_STATS) * part_cnt);

      manager->stats_group->context_cnt = part_cnt;

      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  contexts[part_index].stats = &manager->stats_group->context_stats[part_index];
	}
    }

  part_key = qdata_alloc_hscan_key (thread_p, merge_info->ls_column_cnt, true);
  if (part_key == NULL)
    {
      goto error_exit;
    }

  /* common */
  part_info.part_cnt = part_cnt;

  /* outer */
  part_info.list_id = outer_list_id;
  part_info.part_list_id = outer_part_list_id;
  part_info.stats = &stats->outer;

  error = hjoin_make_partition_input (thread_p, manager, &part_info, &single_context->outer_fetch_info,
				      is_outer_join, part_key);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_skew (outer_list_id, outer_part_list_id, part_cnt, &stats->outer);
    }

  /* inner */
  part_info.list_id = inner_list_id;
  part_info.part_list_id = inner_part_list_id;
  part_info.stats = &stats->inner;

  error = hjoin_make_partition_input (thread_p, manager, &part_info, &single_context->inner_fetch_info,
				      is_outer_join, part_key);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_skew (inner_list_id, inner_part_list_id, part_cnt, &stats->inner);
    }

  qfile_close_list (thread_p, outer_list_id);
  qfile_destroy_list (thread_p, outer_list_id);

  qfile_close_list (thread_p, inner_list_id);
  qfile_destroy_list (thread_p, inner_list_id);

  status = HASHJOIN_STATUS_PARTITION;

  ASSERT_NO_ERROR ();

cleanup:
  if (part_key != NULL)
    {
      qdata_free_hscan_key (thread_p, part_key, merge_info->ls_column_cnt);
    }

  if (outer_part_list_id != NULL)
    {
      db_private_free_and_init (thread_p, outer_part_list_id);
    }

  if (inner_part_list_id != NULL)
    {
      db_private_free_and_init (thread_p, inner_part_list_id);
    }

  return status;

error_exit:
  if (contexts != NULL)
    {
      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  hjoin_clear_contexts (thread_p, &contexts[part_index]);
	}

      db_private_free_and_init (thread_p, contexts);

      manager->contexts = NULL;
      manager->context_cnt = 0;
    }

  if (thread_is_on_trace (thread_p))
    {
      if (manager->stats_group->context_stats != NULL)
	{
	  db_private_free_and_init (thread_p, manager->stats_group->context_stats);
	}

      manager->stats_group->context_cnt = 0;
    }

  if (error == ER_INTERRUPTED || er_errid () == ER_INTERRUPTED)
    {
      status = HASHJOIN_STATUS_ERROR;
    }
  else
    {
      /* retry */
      er_clear ();
      status = HASHJOIN_STATUS_SINGLE;
    }

  goto cleanup;
}

/*
 * hjoin_make_partition_input() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   part_info(in): Partitioning information.
 *   fetch_info(in): Information for reading join column values.
 *   is_null_allowed(in): Whether to include tuples with NULL in any join column in partitioning.
 *   key(in/out): Space for reading join column values.
 */
static int
hjoin_make_partition_input (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
			    HASHJOIN_PARTITION_INFO * part_info, HASHJOIN_FETCH_INFO * fetch_info,
			    bool is_null_allowed, HASH_SCAN_KEY * key)
{
  QFILE_LIST_SCAN_ID list_scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool need_skip_next = false;

  unsigned int hash_key, part_id;
  int part_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (part_info != NULL);
  assert (fetch_info != NULL);
  assert (key != NULL);

  hjoin_check_valid_part_info (part_info);

  HASHJOIN_INPUT_STATS *stats = part_info->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  /* Prevent faults when qfile_close_scan is called */
  list_scan_id.status = S_CLOSED;

  error = qfile_open_list_scan (part_info->list_id, &list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error = hjoin_fetch_key (thread_p, fetch_info, &tuple_record, key, NULL /* compare_key */ , &need_skip_next);
      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
      else if (need_skip_next)
	{
	  if (is_null_allowed)
	    {
	      /* In outer joins, tuples with NULL in any join column are placed in the last partition.
	       * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition. */
	      error =
		qfile_add_tuple_to_list (thread_p, part_info->part_list_id[part_info->part_cnt - 1], tuple_record.tpl);
	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}
	    }

	  need_skip_next = false;	/* init */
	  continue;
	}			/* else if (need_skip_next) */
      else
	{
	  /* fall through */
	}

      /* part_id */
      hash_key = qdata_hash_scan_key (key, UINT_MAX, HASH_METH_IN_MEM);
      part_id = (is_null_allowed) ? hash_key % (part_info->part_cnt - 1) : hash_key % (part_info->part_cnt);

      error = qfile_add_tuple_to_list (thread_p, part_info->part_list_id[part_id], tuple_record.tpl);
      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
    }				/* while (qfile_scan_list_next (list_scan_id)) */

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, stats, &start_stats);
    }

  /* After qfile_open_list_scan, if an error occurs,
   * ensure qfile_close_scan runs here
   * before jumping to error_exit. */
  qfile_close_scan (thread_p, &list_scan_id);

  for (part_index = 0; part_index < part_info->part_cnt; part_index++)
    {
      qfile_close_list (thread_p, part_info->part_list_id[part_index]);
    }

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
      goto error_exit;
    }

  ASSERT_NO_ERROR ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_init_context() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in/out): Hash join context to initialize.
 */
static int
hjoin_init_context (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  QFILE_LIST_ID *build_list_id, *probe_list_id;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->stats != NULL || !thread_is_on_trace (thread_p));

  /* Do not perform NULL checks; 
   * validation is expected to be handled by the caller */
  outer_list_id = context->outer_list_id;
  inner_list_id = context->inner_list_id;
  assert (outer_list_id != NULL && outer_list_id->tuple_cnt > 0);
  assert (inner_list_id != NULL && inner_list_id->tuple_cnt > 0);

  if (context->join_type == JOIN_INNER)
    {
      if (outer_list_id->tuple_cnt < inner_list_id->tuple_cnt)
	{
	  context->is_build_outer = true;
	}
      else if (outer_list_id->tuple_cnt == inner_list_id->tuple_cnt
	       && outer_list_id->page_cnt < inner_list_id->page_cnt)
	{
	  context->is_build_outer = true;
	}
      else
	{
	  context->is_build_outer = false;
	}
    }
  else if (context->join_type == JOIN_LEFT)
    {
      context->is_build_outer = false;
    }
  else if (context->join_type == JOIN_RIGHT)
    {
      context->is_build_outer = true;
    }
  else
    {
      /* impossible case */
      assert_release (false);
      goto error_exit;
    }

  if (context->is_build_outer)
    {
      build_list_id = outer_list_id;
      probe_list_id = inner_list_id;
    }
  else
    {
      build_list_id = inner_list_id;
      probe_list_id = outer_list_id;
    }

  error = hjoin_scan_init (thread_p, &context->hash_scan, context->key_cnt, build_list_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      context->stats->hash_method = context->hash_scan.hash_list_scan_type;
      context->stats->is_build_outer = context->is_build_outer;
      context->stats->build.part_rows = build_list_id->tuple_cnt;
      context->stats->probe.part_rows = probe_list_id->tuple_cnt;
    }

  ASSERT_NO_ERROR ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  hjoin_scan_clear (thread_p, &context->hash_scan);

  return error;
}

/*
 * hjoin_clear_contexts() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   context(in): Hash join context to clear.
 */
static void
hjoin_clear_contexts (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context)
{
  assert (thread_p != NULL);
  assert (context != NULL);

  if (context->outer_list_id != NULL)
    {
      qfile_close_list (thread_p, context->outer_list_id);
      qfile_destroy_list (thread_p, context->outer_list_id);
      QFILE_FREE_AND_INIT_LIST_ID (context->outer_list_id);
    }

  if (context->inner_list_id != NULL)
    {
      qfile_close_list (thread_p, context->inner_list_id);
      qfile_destroy_list (thread_p, context->inner_list_id);
      QFILE_FREE_AND_INIT_LIST_ID (context->inner_list_id);
    }

  hjoin_scan_clear (thread_p, &context->hash_scan);
}

/*
 * hjoin_scan_init() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   hash_scan(in/out): Hash scan structure to initialize.
 *   key_cnt(in): Number of join columns.
 *   list_id(in): List identifier to be used as build input.
 */
static int
hjoin_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt, QFILE_LIST_ID * list_id)
{
  UINT64 mem_limit;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_id != NULL && list_id->tuple_cnt > 0);
  assert (key_cnt > 0);

  mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
  assert (mem_limit > 0);

  assert (hash_scan->build_regu_list == NULL);	/* Unused */
  assert (hash_scan->probe_regu_list == NULL);	/* Unused */

  hash_scan->temp_key = qdata_alloc_hscan_key (thread_p, key_cnt, true);
  if (hash_scan->temp_key == NULL)
    {
      goto error_exit;
    }

  hash_scan->temp_new_key = qdata_alloc_hscan_key (thread_p, key_cnt, true);
  if (hash_scan->temp_new_key == NULL)
    {
      goto error_exit;
    }

  if ((UINT64) list_id->page_cnt * DB_PAGESIZE <= mem_limit)
    {
#if HASHJOIN_DUMP_BUILD
      fprintf (stdout, "\nHash Join Method: In Memory\n");
      fprintf (stdout, "  - Page Count: %d <= %lu\n", list_id->page_cnt, mem_limit / 16344);
#endif /* HASHJOIN_DUMP_BUILD */

      hash_scan->hash_list_scan_type = HASH_METH_IN_MEM;

      hash_scan->memory.hash_table = mht_create_hls ("Hash Join", list_id->tuple_cnt, NULL, NULL);
      if (hash_scan->memory.hash_table == NULL)
	{
	  goto error_exit;
	}

      hash_scan->memory.curr_hash_entry = NULL;
    }
  else if ((UINT64) list_id->tuple_cnt * (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)) <= mem_limit)
    {
#if HASHJOIN_DUMP_BUILD
      fprintf (stdout, "\nHash Join Method: Hybrid\n");
      fprintf (stdout, "  - Page Count: %d > %lu\n", list_id->page_cnt, mem_limit / 16344);
      fprintf (stdout, "  - Tuple Count: %ld <= %lu\n", list_id->tuple_cnt,
	       mem_limit / (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)));
#endif /* HASHJOIN_DUMP_BUILD */

      hash_scan->hash_list_scan_type = HASH_METH_HYBRID;

      hash_scan->memory.hash_table = mht_create_hls ("Hash Join", list_id->tuple_cnt, NULL, NULL);
      if (hash_scan->memory.hash_table == NULL)
	{
	  goto error_exit;
	}

      hash_scan->memory.curr_hash_entry = NULL;
    }
  else
    {
#if HASHJOIN_DUMP_BUILD
      fprintf (stdout, "\nHash Join Method: File\n");
      fprintf (stdout, "  - Page Count: %d > %lu\n", list_id->page_cnt, mem_limit / 16344);
      fprintf (stdout, "  - Tuple Count: %ld > %lu\n", list_id->tuple_cnt,
	       mem_limit / (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)));
#endif /* HASHJOIN_DUMP_BUILD */

      hash_scan->hash_list_scan_type = HASH_METH_HASH_FILE;

      hash_scan->file.hash_table = (FHSID *) db_private_alloc (thread_p, sizeof (FHSID));
      if (hash_scan->file.hash_table == NULL)
	{
	  goto error_exit;
	}

      if (fhs_create (thread_p, hash_scan->file.hash_table, list_id->tuple_cnt) == NULL)
	{
	  goto error_exit;
	}

      hash_scan->file.curr_oid = OID_INITIALIZER;
      hash_scan->file.is_dk_bucket = false;
    }

  hash_scan->curr_hash_key = 0;
  hash_scan->need_coerce_type = false;

  ASSERT_NO_ERROR ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  hjoin_scan_clear (thread_p, hash_scan);

  return error;
}

/*
 * hjoin_scan_clear() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   hash_scan(in): Hash scan structure to clear.
 */
static void
hjoin_scan_clear (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan)
{
  assert (thread_p != NULL);
  assert (hash_scan != NULL);

  if (hash_scan->temp_key != NULL)
    {
      qdata_free_hscan_key (thread_p, hash_scan->temp_key, hash_scan->temp_key->val_count);
      hash_scan->temp_key = NULL;
    }

  if (hash_scan->temp_new_key != NULL)
    {
      qdata_free_hscan_key (thread_p, hash_scan->temp_new_key, hash_scan->temp_new_key->val_count);
      hash_scan->temp_new_key = NULL;
    }

  switch (hash_scan->hash_list_scan_type)
    {
    case HASH_METH_IN_MEM:
    case HASH_METH_HYBRID:
      if (hash_scan->memory.hash_table != NULL)
	{
	  mht_clear_hls (hash_scan->memory.hash_table, qdata_free_hscan_entry, (void *) thread_p);
	  mht_destroy_hls (hash_scan->memory.hash_table);
	  hash_scan->memory.hash_table = NULL;
	}
      break;

    case HASH_METH_HASH_FILE:
      if (hash_scan->file.hash_table != NULL)
	{
	  fhs_destroy (thread_p, hash_scan->file.hash_table);
	  db_private_free_and_init (thread_p, hash_scan->file.hash_table);
	}
      break;

    case HASH_METH_NOT_USE:
    default:
      /* Nothing to do */
      break;
    }

  hash_scan->hash_list_scan_type == HASH_METH_NOT_USE;
}

/*
 * hjoin_check_empty_inputs() -
 *   return: One of the following HASHJOIN_STATUS values:
 *           - HASHJOIN_STATUS_END: Inner join with one empty input, or outer join with empty preserved side.
 *           - HASHJOIN_STATUS_FILL_NULL_VALUES: Outer join with empty null-supplying side.
 *           - HASHJOIN_STATUS_TRY: Both inputs are non-empty; proceed with the join.
 *   context(in): Hash join context containing per-partition state.
 */
static HASHJOIN_STATUS
hjoin_check_empty_inputs (HASHJOIN_CONTEXT * context)
{
  INT64 outer_tuple_cnt, inner_tuple_cnt;
  HASHJOIN_STATUS status;

  assert (context != NULL);

  /* When aptr_list is executed in qexec_execute_mainblock_internal,
   * it checks the results from outer_xasl and inner_xasl in merge_info.
   * If either has no result, the other is skipped,
   * and the skipped node can have a type count of 0 in list_id.type_list. */
  if (context->outer_list_id == NULL || context->inner_list_id == NULL)
    {
      return HASHJOIN_STATUS_END;
    }

  outer_tuple_cnt = context->outer_list_id->tuple_cnt;
  inner_tuple_cnt = context->inner_list_id->tuple_cnt;

  /* HASHJOIN_STATUS_END must be checked first. */
  if (context->join_type == JOIN_INNER)
    {
      status = (outer_tuple_cnt == 0 || inner_tuple_cnt == 0) ? HASHJOIN_STATUS_END : HASHJOIN_STATUS_TRY;
    }
  else if (context->join_type == JOIN_LEFT)
    {
      status =
	(outer_tuple_cnt == 0) ? HASHJOIN_STATUS_END : (inner_tuple_cnt ==
							0) ? HASHJOIN_STATUS_FILL_NULL_VALUES : HASHJOIN_STATUS_TRY;
    }
  else if (context->join_type == JOIN_RIGHT)
    {
      status =
	(inner_tuple_cnt == 0) ? HASHJOIN_STATUS_END : (outer_tuple_cnt ==
							0) ? HASHJOIN_STATUS_FILL_NULL_VALUES : HASHJOIN_STATUS_TRY;
    }
  else
    {
      /* impossible case */
      assert_release (false);
      status = HASHJOIN_STATUS_ERROR;
    }

  return status;
}

/*
 * hjoin_fetch_key() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   fetch_info(in): Information for reading join column values. 
 *   tuple_record(in): Tuple to read values from.
 *   key(in/out): Space for reading join column values.
 *   compare_key(in): Key for comparison with the read key. (can be NULL).
 *   need_skip_next(in/out): Set to true if the current tuple should be skipped.
 */
static int
hjoin_fetch_key (THREAD_ENTRY * thread_p, HASHJOIN_FETCH_INFO * fetch_info, QFILE_TUPLE_RECORD * tuple_record,
		 HASH_SCAN_KEY * key, HASH_SCAN_KEY * compare_key, bool * need_skip_next)
{
  TP_DOMAIN **domains, **coerce_domains;
  int *value_indexes;
  bool need_coerce_domains;

  QFILE_TUPLE tuple_record_end;
  QFILE_TUPLE tuple_value;
  OR_BUF buf;
  int value_size, value_index, key_index;

  TP_DOMAIN_STATUS domain_status = DOMAIN_COMPATIBLE;
  DB_VALUE pre_coerce_value;

  DB_VALUE_COMPARE_RESULT compare_result = DB_EQ;
  int compare_size;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (fetch_info != NULL && fetch_info->input != NULL);
  assert (tuple_record != NULL && tuple_record->tpl != NULL);
  assert (key != NULL);
  assert (need_skip_next != NULL && *need_skip_next == false);

  /* Do not perform NULL checks; 
   * validation is expected to be handled by the caller */
  domains = fetch_info->input->domains;
  value_indexes = fetch_info->input->value_indexes;
  coerce_domains = fetch_info->coerce_domains;
  need_coerce_domains = fetch_info->need_coerce_domains;

  db_make_null (&pre_coerce_value);

  tuple_record_end = tuple_record->tpl + QFILE_GET_TUPLE_LENGTH (tuple_record->tpl);

  /* Skip the tuple header */
  tuple_value = tuple_record->tpl + QFILE_TUPLE_LENGTH_SIZE;

  for (value_index = 0; tuple_value < tuple_record_end; value_index++)
    {
      for (key_index = 0; key_index < key->val_count; key_index++)
	{
	  /*
	   * The same tuple value can be referenced by multiple keys.
	   *
	   * e.g. value_indexes[0] = 0
	   *      value_indexes[1] = 1
	   *      value_indexes[2] = 1
	   *      value_indexes[3] = 3
	   */
	  if (value_indexes[key_index] != value_index)
	    {
	      continue;
	    }

	  /* Skip the tuple if any value is NULL */
	  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_value) == V_UNBOUND)
	    {
	      goto skip_next;
	    }

	  value_size = QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value);
	  assert (value_size > 0);

	  /* Skip the tuple value header */
	  or_init (&buf, tuple_value + QFILE_TUPLE_VALUE_HEADER_SIZE, value_size);

	  pr_clear_value (key->values[key_index]);

	  if (need_coerce_domains && coerce_domains[key_index] != NULL
	      && coerce_domains[key_index] != domains[key_index])
	    {
	      error =
		domains[key_index]->type->data_readval (&buf, &pre_coerce_value, domains[key_index], -1, false, NULL,
							0);
	      if (error != NO_ERROR)
		{
		  goto error_exit;
		}

	      domain_status = tp_value_coerce (&pre_coerce_value, key->values[key_index], coerce_domains[key_index]);
	      if (domain_status != DOMAIN_COMPATIBLE)
		{
		  tp_domain_status_er_set (domain_status, ARG_FILE_LINE, &pre_coerce_value, coerce_domains[key_index]);
		  error = er_errid ();
		  pr_clear_value (&pre_coerce_value);
		  goto error_exit;
		}

	      pr_clear_value (&pre_coerce_value);
	    }
	  else
	    {
	      error =
		domains[key_index]->type->data_readval (&buf, key->values[key_index], domains[key_index], -1, false,
							NULL, 0);
	      if (error != NO_ERROR)
		{
		  goto error_exit;
		}
	    }

	  if (compare_key != NULL)
	    {
	      /* Skip the tuple if any value does not match */
	      compare_result = tp_value_compare (key->values[key_index], compare_key->values[key_index], 0, 0);
	      if (compare_result != DB_EQ)
		{
		  goto skip_next;
		}
	    }
	}

      /* Skip the current tuple value */
      tuple_value += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value);
    }

  ASSERT_NO_ERROR ();
  return NO_ERROR;

skip_next:
  *need_skip_next = true;

  ASSERT_NO_ERROR ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_check_valid_part_info() -
 *   return: None.
 *   info(in): Partition information to be validated.
 */
static void
hjoin_check_valid_part_info (HASHJOIN_PARTITION_INFO * info)
{
  assert (info->list_id != NULL);
  assert (info->part_cnt > 1 && info->part_list_id != NULL);
#if !defined (NDEBUG)
  for (int part_index = 0; part_index < info->part_cnt; part_index++)
    {
      assert (info->part_list_id[part_index] != NULL);
    }
#endif /* !NDEBUG */
}

/*
 * hjoin_build() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 *   list_scan_id(in): Scan identifier for the build input.
 */
static int
hjoin_build (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
	     QFILE_LIST_SCAN_ID * list_scan_id)
{
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool need_skip_next = false;

  HASHJOIN_FETCH_INFO *fetch_info;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (list_scan_id != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  hash_scan = &context->hash_scan;

  hash_method = hash_scan->hash_list_scan_type;
  assert (hash_method != HASH_METH_NOT_USE);

  key = hash_scan->temp_key;
  assert (key != NULL);

  if (context->is_build_outer)
    {
      fetch_info = &context->outer_fetch_info;
    }
  else
    {
      fetch_info = &context->inner_fetch_info;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_BUILD_FETCH);
      error = hjoin_fetch_key (thread_p, fetch_info, &tuple_record, key, NULL /* compare_key */ , &need_skip_next);
      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_BUILD_FETCH);

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
      else if (need_skip_next)
	{
	  need_skip_next = false;	/* init */
	  continue;
	}
      else
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_BUILD_HASH);
	  hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_BUILD_HASH);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_BUILD_INSERT);
	  error = hjoin_build_key (thread_p, hash_scan, list_scan_id, &tuple_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_BUILD_INSERT);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      stats->build.rows++;
	    }
	}
    }				/* while (qfile_scan_list_next (list_scan_id)) */

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->build, &start_stats);
    }

  /* qfile_close_scan is called by the caller. */

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
      goto error_exit;
    }

  HJOIN_DUMP_HASH_TABLE (thread_p, hash_scan, &list_scan_id->list_id);

  ASSERT_NO_ERROR ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_build_key() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   hash_scan(in): Hash scan structure used for hash table operations.
 *   list_scan_id(in): Scan identifier for the build input.
 *   tuple_record(in): Tuple to be inserted into the hash table
 */
static int
hjoin_build_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_SCAN_ID * list_scan_id,
		 QFILE_TUPLE_RECORD * tuple_record)
{
  HASH_SCAN_VALUE *hash_value = NULL;
  TFTID tftid;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_scan_id != NULL);
  assert (tuple_record != NULL && tuple_record->tpl != NULL);

  switch (hash_scan->hash_list_scan_type)
    {
    case HASH_METH_IN_MEM:
      assert (hash_scan->memory.hash_table != NULL);

      hash_value = qdata_alloc_hscan_value (thread_p, tuple_record->tpl);
      if (hash_value == NULL)
	{
	  if (er_errid () == NO_ERROR)
	    {
	      assert_release (false);
	    }
	  return er_errid ();
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, (void *) hash_value) == NULL)
	{
	  if (er_errid () == NO_ERROR)
	    {
	      assert_release (false);
	    }
	  qdata_free_hscan_value (thread_p, hash_value);
	  return er_errid ();
	}
      break;

    case HASH_METH_HYBRID:
      assert (hash_scan->memory.hash_table != NULL);

      hash_value = qdata_alloc_hscan_value_OID (thread_p, list_scan_id);
      if (hash_value == NULL)
	{
	  if (er_errid () == NO_ERROR)
	    {
	      assert_release (false);
	    }
	  return er_errid ();
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, (void *) hash_value) == NULL)
	{
	  if (er_errid () == NO_ERROR)
	    {
	      assert_release (false);
	    }
	  qdata_free_hscan_value (thread_p, hash_value);
	  return er_errid ();
	}
      break;

    case HASH_METH_HASH_FILE:
      assert (hash_scan->file.hash_table != NULL);

      SET_TFTID (tftid, list_scan_id->curr_vpid.volid, list_scan_id->curr_vpid.pageid, list_scan_id->curr_offset);
      if (fhs_insert (thread_p, hash_scan->file.hash_table, (void *) &hash_scan->curr_hash_key, &tftid) == NULL)
	{
	  if (er_errid () == NO_ERROR)
	    {
	      assert_release (false);
	    }
	  return er_errid ();
	}
      break;

    case HASH_METH_NOT_USE:
      [[fallthrough]];
    default:
      /* impossible case */
      assert_release (false);
      return er_errid ();
    }

  ASSERT_NO_ERROR ();
  return NO_ERROR;
}

/*
 * hjoin_probe() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 *   build_scan_id(in): Scan identifier for the build input.
 *   probe_scan_id(in): Scan identifier for the probe input.
 *   list_id(in/out): List identifier containing the join result.
 */
static int
hjoin_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
	     QFILE_LIST_SCAN_ID * build_scan_id, QFILE_LIST_SCAN_ID * probe_scan_id, QFILE_LIST_ID * list_id)
{
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_TUPLE_RECORD found_record = { NULL, 0 };
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  QFILE_TUPLE_RECORD *left_record;
  QFILE_TUPLE_RECORD *right_record;
  SCAN_CODE scan_code;

  HASHJOIN_FETCH_INFO *build_fetch_info, *probe_fetch_info;
  bool need_skip_next = false;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key, *found_key;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (build_scan_id != NULL);
  assert (probe_scan_id != NULL);
  assert (list_id != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
  UINT64 max_collisions = 0;
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  hash_scan = &context->hash_scan;

  hash_method = hash_scan->hash_list_scan_type;
  assert (hash_method != HASH_METH_NOT_USE);

  key = hash_scan->temp_key;
  found_key = hash_scan->temp_new_key;
  assert (key != NULL);
  assert (found_key != NULL);

  if (context->is_build_outer)
    {
      build_fetch_info = &context->outer_fetch_info;
      probe_fetch_info = &context->inner_fetch_info;

      left_record = &found_record;
      right_record = &tuple_record;
    }
  else
    {
      build_fetch_info = &context->inner_fetch_info;
      probe_fetch_info = &context->outer_fetch_info;

      left_record = &tuple_record;
      right_record = &found_record;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, probe_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      HJOIN_PRINT_TUPLE (probe_scan_id, tuple_record.tpl, HASHJOIN_PRINT_READ_KEY);

      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
      error = hjoin_fetch_key (thread_p, probe_fetch_info, &tuple_record, key, NULL /* compare_key */ ,
			       &need_skip_next);
      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
      else if (need_skip_next)
	{
	  need_skip_next = false;	/* init */
	  continue;
	}
      else
	{
	  /* fall through */
	}

      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);

      if (thread_is_on_trace (thread_p))
	{
	  max_collisions = 0;
	}

      do
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);
	  error = hjoin_probe_key (thread_p, hash_scan, build_scan_id, &found_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (found_record.tpl == NULL)
	    {
	      break;		/* not found */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      max_collisions++;	/* found */
	    }

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	  error = hjoin_fetch_key (thread_p, build_fetch_info, &found_record, found_key, key /* compare_key */ ,
				   &need_skip_next);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	  else if (need_skip_next)
	    {
	      HJOIN_PRINT_TUPLE (build_scan_id, found_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

	      need_skip_next = false;	/* init */
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

	  HJOIN_PRINT_TUPLE (build_scan_id, found_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, left_record, right_record, manager->merge_info,
					  &overflow_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      stats->probe.rows++;
	    }
	}
      while (true);

      if (thread_is_on_trace (thread_p))
	{
	  stats->probe.readkeys += max_collisions;
	  stats->probe.max_collisions = MAX (stats->probe.max_collisions, max_collisions);
	}

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
    }				/* while (qfile_scan_list_next (list_scan_id)) */

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->probe, &start_stats);
    }

  /* qfile_close_scan is called by the caller. */

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
      goto error_exit;
    }

  ASSERT_NO_ERROR ();

cleanup:
  if (overflow_record.tpl != NULL)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  return error;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  goto cleanup;
}

/*
 * hjoin_outer_probe() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 *   build_scan_id(in): Scan identifier for the build input.
 *   probe_scan_id(in): Scan identifier for the probe input.
 *   list_id(in/out): List identifier containing the join result.
 */
static int
hjoin_outer_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
		   QFILE_LIST_SCAN_ID * build_scan_id, QFILE_LIST_SCAN_ID * probe_scan_id, QFILE_LIST_ID * list_id)
{
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_TUPLE_RECORD found_record = { NULL, 0 };
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  QFILE_TUPLE_RECORD *left_record;
  QFILE_TUPLE_RECORD *right_record;
  SCAN_CODE scan_code;

  HASHJOIN_FETCH_INFO *build_fetch_info, *probe_fetch_info;
  bool need_skip_next = false;
  bool any_record_added;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key, *found_key;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (build_scan_id != NULL);
  assert (probe_scan_id != NULL);
  assert (list_id != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
  UINT64 max_collisions = 0;
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  hash_scan = &context->hash_scan;

  hash_method = hash_scan->hash_list_scan_type;
  assert (hash_method != HASH_METH_NOT_USE);

  key = hash_scan->temp_key;
  found_key = hash_scan->temp_new_key;
  assert (key != NULL);
  assert (found_key != NULL);

  if (context->is_build_outer)
    {
      build_fetch_info = &context->outer_fetch_info;
      probe_fetch_info = &context->inner_fetch_info;

      left_record = &found_record;
      right_record = &tuple_record;
    }
  else
    {
      build_fetch_info = &context->inner_fetch_info;
      probe_fetch_info = &context->outer_fetch_info;

      left_record = &tuple_record;
      right_record = &found_record;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, probe_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      HJOIN_PRINT_TUPLE (probe_scan_id, tuple_record.tpl, HASHJOIN_PRINT_READ_KEY);

      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
      error = hjoin_fetch_key (thread_p, probe_fetch_info, &tuple_record, key, NULL /* compare_key */ ,
			       &need_skip_next);
      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
      else if (need_skip_next)
	{
	  HJOIN_PRINT_TUPLE (probe_scan_id, tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

	  if (context->join_type == JOIN_LEFT)
	    {
	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	      error =
		hjoin_merge_tuple_to_list_id (thread_p, list_id, &tuple_record, NULL, manager->merge_info,
					      &overflow_record);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	    }
	  else if (context->join_type == JOIN_RIGHT)
	    {
	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	      error =
		hjoin_merge_tuple_to_list_id (thread_p, list_id, NULL, &tuple_record, manager->merge_info,
					      &overflow_record);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	    }
	  else
	    {
	      assert_release (false);
	      error = er_errid ();
	      break;		/* error_exit */
	    }

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      stats->probe.rows++;
	    }

	  need_skip_next = false;	/* init */
	  continue;
	}			/* else if (need_skip_next) */
      else
	{
	  /* fall through */
	}

      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);

      if (thread_is_on_trace (thread_p))
	{
	  max_collisions = 0;
	}

      any_record_added = false;

      do
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);
	  error = hjoin_probe_key (thread_p, hash_scan, build_scan_id, &found_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (found_record.tpl == NULL)
	    {
	      break;		/* not found */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      max_collisions++;	/* found */
	    }

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	  error = hjoin_fetch_key (thread_p, build_fetch_info, &found_record, found_key, key /* compare_key */ ,
				   &need_skip_next);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	  else if (need_skip_next)
	    {
	      HJOIN_PRINT_TUPLE (build_scan_id, found_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

	      need_skip_next = false;	/* init */
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

	  if (context->during_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	      do
		{
		  error =
		    fetch_val_list (thread_p, probe_fetch_info->regu_list_pred, manager->vd, NULL, NULL,
				    tuple_record.tpl, PEEK);
		  if (error != NO_ERROR)
		    {
		      break;	/* error_exit */
		    }

		  error =
		    fetch_val_list (thread_p, build_fetch_info->regu_list_pred, manager->vd, NULL, NULL,
				    found_record.tpl, PEEK);
		  if (error != NO_ERROR)
		    {
		      break;	/* error_exit */
		    }

		  ev_res = eval_pred (thread_p, context->during_join_pred, manager->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      error = ER_FAILED;
		      break;	/* error_exit */
		    }
		}
	      while (false);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}

	      /* Search the next hash entry if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  HJOIN_PRINT_TUPLE (build_scan_id, found_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
		  assert (need_skip_next == false);
		  continue;
		}
	    }			/* if (context->during_join_pred != NULL) */

	  HJOIN_PRINT_TUPLE (build_scan_id, found_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, left_record, right_record, manager->merge_info,
					  &overflow_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      stats->probe.rows++;
	    }

	  any_record_added = true;
	}
      while (true);

      if (thread_is_on_trace (thread_p))
	{
	  stats->probe.readkeys += max_collisions;
	  stats->probe.max_collisions = MAX (stats->probe.max_collisions, max_collisions);
	}

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}

      if (!any_record_added)
	{
	  HJOIN_PRINT_TUPLE (probe_scan_id, tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

	  if (context->join_type == JOIN_LEFT)
	    {
	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	      error =
		hjoin_merge_tuple_to_list_id (thread_p, list_id, &tuple_record, NULL, manager->merge_info,
					      &overflow_record);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	    }
	  else if (context->join_type == JOIN_RIGHT)
	    {
	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	      error =
		hjoin_merge_tuple_to_list_id (thread_p, list_id, NULL, &tuple_record, manager->merge_info,
					      &overflow_record);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	    }
	  else
	    {
	      /* impossible case */
	      assert_release (false);
	      error = er_errid ();
	      break;		/* error_exit */
	    }

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      stats->probe.rows++;
	    }
	}			/* if (!any_record_added) */
    }				/* while (qfile_scan_list_next (probe_scan_id)) */

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->probe, &start_stats);
    }

  /* qfile_close_scan is called by the caller. */

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
      goto error_exit;
    }

  ASSERT_NO_ERROR ();

cleanup:
  if (overflow_record.tpl != NULL)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  return error;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  goto cleanup;
}

/*
 * hjoin_probe_key() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   hash_scan(in): Hash scan structure used for hash table operations.
 *   list_scan_id(in): Scan identifier for the probe input.
 *   tuple_record(in/out): Tuple found in the hash table.
 */
static int
hjoin_probe_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_SCAN_ID * list_scan_id,
		 QFILE_TUPLE_RECORD * tuple_record)
{
  HASH_SCAN_VALUE *hash_value = NULL;
  TFTID tftid;
  EH_SEARCH eh_search;
  QFILE_TUPLE_POSITION tuple_position;
  SCAN_CODE scan_code;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_scan_id != NULL);
  assert (tuple_record != NULL);

  switch (hash_scan->hash_list_scan_type)
    {
    case HASH_METH_IN_MEM:
      assert (hash_scan->memory.hash_table != NULL);

      if (tuple_record->tpl == NULL)
	{
	  hash_value =
	    (HASH_SCAN_VALUE *) mht_get_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
					     (void **) &hash_scan->memory.curr_hash_entry);
	}
      else
	{
	  hash_value =
	    (HASH_SCAN_VALUE *) mht_get_next_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
						  (void **) &hash_scan->memory.curr_hash_entry);
	}

      if (hash_value != NULL)
	{
	  tuple_record->tpl = ((HASH_SCAN_VALUE *) hash_scan->memory.curr_hash_entry->data)->tuple;
	  tuple_record->size = QFILE_GET_TUPLE_VALUE_LENGTH (tuple_record->tpl);
	}
      else
	{
	  /* not found */
	  tuple_record->tpl = NULL;
	  tuple_record->size = 0;
	}
      break;			/* HASH_METH_IN_MEM */

    case HASH_METH_HYBRID:
      assert (hash_scan->memory.hash_table != NULL);

      if (tuple_record->tpl == NULL)
	{
	  hash_value =
	    (HASH_SCAN_VALUE *) mht_get_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
					     (void **) &hash_scan->memory.curr_hash_entry);
	}
      else
	{
	  hash_value =
	    (HASH_SCAN_VALUE *) mht_get_next_hls (hash_scan->memory.hash_table,
						  (void *) &hash_scan->curr_hash_key,
						  (void **) &hash_scan->memory.curr_hash_entry);
	}

      if (hash_value != NULL)
	{
	  MAKE_TUPLE_POSTION (tuple_position, hash_value->pos, list_scan_id);
	  scan_code = qfile_jump_scan_tuple_position (thread_p, list_scan_id, &tuple_position, tuple_record, PEEK);
	  if (scan_code != S_SUCCESS)
	    {
	      if (er_errid () == NO_ERROR)
		{
		  assert_release (false);
		}
	      return er_errid ();
	    }
	}
      else
	{
	  /* not found */
	  tuple_record->tpl = NULL;
	  tuple_record->size = 0;
	}
      break;			/* HASH_METH_HYBRID */

    case HASH_METH_HASH_FILE:
      assert (hash_scan->file.hash_table != NULL);

      if (tuple_record->tpl == NULL)
	{
	  eh_search = fhs_search (thread_p, hash_scan, &tftid);
	}
      else
	{
	  eh_search = fhs_search_next (thread_p, hash_scan, &tftid);
	}

      if (eh_search == EH_KEY_FOUND)
	{
	  MAKE_TFTID_TO_TUPLE_POSTION (tuple_position, tftid, list_scan_id);
	  scan_code = qfile_jump_scan_tuple_position (thread_p, list_scan_id, &tuple_position, tuple_record, PEEK);
	  if (scan_code != S_SUCCESS)
	    {
	      if (er_errid () == NO_ERROR)
		{
		  assert_release (false);
		}
	      return er_errid ();
	    }
	}
      else if (eh_search == EH_KEY_NOTFOUND)
	{
	  /* not found */
	  tuple_record->tpl = NULL;
	  tuple_record->size = 0;
	}
      else
	{
	  if (er_errid () == NO_ERROR)
	    {
	      assert_release (false);
	    }
	  return er_errid ();
	}
      break;			/* HASH_METH_HASH_FILE */

    case HASH_METH_NOT_USE:
      [[fallthrough]];
    default:
      /* impossible case */
      assert_release (false);
      return er_errid ();
    }

  ASSERT_NO_ERROR ();
  return NO_ERROR;
}

/*
 * hjoin_merge_tuple_to_list_id() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   list_id(in/out): List identifier to be merged.
 *   outer_record(in): Outer tuple to merge. (can be NULL).
 *   inner_record(in): Inner tuple to merge. (can be NULL).
 *   merge_info(in): Information used to merge the joined result.
 *   overflow_record(in/out): Space used for merging tuples too large to fit on a single page.
 */
static int
hjoin_merge_tuple_to_list_id (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
			      QFILE_TUPLE_RECORD * outer_record,
			      QFILE_TUPLE_RECORD * inner_record, QFILE_LIST_MERGE_INFO * merge_info,
			      QFILE_TUPLE_RECORD * overflow_record)
{
  QFILE_TUPLE_DESCRIPTOR *tuple_descriptor;
  int max_record_size, max_unbound_size;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (list_id != NULL);
  assert (outer_record != NULL || inner_record != NULL);
  assert (merge_info != NULL);
  assert (overflow_record != NULL);

  max_unbound_size = QFILE_TUPLE_VALUE_HEADER_SIZE * (merge_info->ls_pos_cnt);

  max_record_size = (outer_record != NULL) ? QFILE_GET_TUPLE_LENGTH (outer_record->tpl) : max_unbound_size;
  max_record_size += (inner_record != NULL) ? QFILE_GET_TUPLE_LENGTH (inner_record->tpl) : max_unbound_size;
  max_record_size = DB_ALIGN (max_record_size, MAX_ALIGNMENT);

  if (max_record_size < QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      tuple_descriptor = &list_id->tpl_descr;
      tuple_descriptor->tpl_size = max_record_size;
      tuple_descriptor->tplrec1 = outer_record;
      tuple_descriptor->tplrec2 = inner_record;
      tuple_descriptor->merge_info = merge_info;

      error = qfile_generate_tuple_into_list (thread_p, list_id, T_MERGE);
    }
  else
    {
      error = hjoin_merge_tuple (thread_p, outer_record, inner_record, merge_info, overflow_record);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      error = qfile_add_tuple_to_list (thread_p, list_id, overflow_record->tpl);
    }

  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  ASSERT_NO_ERROR ();
  return NO_ERROR;

error_exit:
  if (er_errid () == NO_ERROR)
    {
      assert_release (false);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_merge_tuple() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   outer_record(in): Outer tuple to merge. (can be NULL).
 *   inner_record(in): Inner tuple to merge. (can be NULL).
 *   merge_info(in): Information used to merge the joined result.
 *   overflow_record(in/out): Space used for merging tuples too large to fit on a single page.
 */
static int
hjoin_merge_tuple (THREAD_ENTRY * thread_p, QFILE_TUPLE_RECORD * outer_record,
		   QFILE_TUPLE_RECORD * inner_record, QFILE_LIST_MERGE_INFO * merge_info,
		   QFILE_TUPLE_RECORD * overflow_record)
{
  QFILE_TUPLE_RECORD *tuple_record;
  QFILE_TUPLE outer_record_end, inner_record_end, tuple_record_end;
  QFILE_TUPLE tuple_value;
  INT32 unbound_value[2] = { 0, 0 };	/* QFILE_TUPLE_VALUE_HEADER */
  int realloc_size, offset, value_size;
  int pos_index, value_index, skip_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (outer_record != NULL || inner_record != NULL);
  assert (merge_info != NULL);
  assert (overflow_record != NULL);

  QFILE_PUT_TUPLE_VALUE_FLAG ((char *) unbound_value, V_UNBOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH ((char *) unbound_value, 0);

  outer_record_end = outer_record->tpl + QFILE_GET_TUPLE_LENGTH (outer_record->tpl);
  inner_record_end = inner_record->tpl + QFILE_GET_TUPLE_LENGTH (inner_record->tpl);

  offset = QFILE_TUPLE_LENGTH_SIZE;

  for (pos_index = 0; pos_index < merge_info->ls_pos_cnt; pos_index++)
    {
      if (merge_info->ls_outer_inner_list[pos_index] == QFILE_OUTER_LIST)
	{
	  tuple_record = outer_record;
	  tuple_record_end = outer_record_end;
	}
      else if (merge_info->ls_outer_inner_list[pos_index] == QFILE_INNER_LIST)
	{
	  tuple_record = inner_record;
	  tuple_record_end = inner_record_end;
	}
      else
	{
	  /* impossible case */
	  assert_release (false);
	  return er_errid ();
	}

      if (tuple_record != NULL)
	{
	  value_index = merge_info->ls_outer_inner_list[pos_index];

	  tuple_value = tuple_record->tpl + QFILE_TUPLE_LENGTH_SIZE;
	  for (skip_index = 0; skip_index < value_index; skip_index++)
	    {
	      tuple_value += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value);
	    }

	  if (tuple_value >= tuple_record_end)
	    {
	      /* impossible case */
	      assert (false);
	      error = ER_TF_BUFFER_OVERFLOW;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      return error;
	    }
	}
      else
	{
	  tuple_value = (char *) unbound_value;
	}

      value_size = QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value);

      if ((overflow_record->size - offset) < value_size)
	{
	  realloc_size = overflow_record->size + CEIL_PTVDIV (value_size, DB_PAGESIZE);

	  /* overflow_record is managed and cleaned up by the caller. */
	  error = qfile_reallocate_tuple (overflow_record, realloc_size);
	  if (error != NO_ERROR)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, DB_PAGESIZE);
	      return error;
	    }
	}

      memcpy (overflow_record->tpl + offset, tuple_value, value_size);
      offset += value_size;
    }				/* for (pos_index < merge_info->ls_pos_cnt) */

  QFILE_PUT_TUPLE_LENGTH (overflow_record->tpl, offset);

  ASSERT_NO_ERROR ();
  return NO_ERROR;
}

/*
 * hjoin_trace_start() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   start_stats(in/out): Profiling data captured at the start of the step.
 */
static void
hjoin_trace_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats)
{
  assert (thread_p != NULL);
  assert (start_stats != NULL);

  tsc_getticks (&start_stats->tick);
  start_stats->fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
  start_stats->ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
  start_stats->fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
}

/*
 * hjoin_trace_end() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   stats(in/out): Profiling data to accumulate.
 *   start_stats(in): Profiling data captured at the start of the step.
 */
static void
hjoin_trace_end (THREAD_ENTRY * thread_p, HASHJOIN_INPUT_STATS * stats, HASHJOIN_START_STATS * start_stats)
{
  TSC_TICKS end_tick;
  TSCTIMEVAL tv_diff;

  assert (thread_p != NULL);
  assert (stats != NULL);
  assert (start_stats != NULL);

  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_stats->tick);

  TSC_ADD_TIMEVAL (stats->elapsed_time, tv_diff);
  TSC_ADD_TIMEVAL (stats->time, tv_diff);
  stats->fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - start_stats->fetches;
  stats->ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - start_stats->ioreads;
  stats->fetch_time += (UINT64) ((perfmon_get_from_statistic (thread_p,
							      PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) -
				  start_stats->fetch_time) / 1000);
}

/*
 * hjoin_trace_skew() -
 *   return: None.
 *   list_id(in): List identifier before partitioning.
 *   part_list_id(in): Array of List identifiers after partitioning.
 *   part_cnt(in): Number of partitions.
 *   skew(out):  Partition imbalance.
 */
static void
hjoin_trace_skew (QFILE_LIST_ID * list_id, QFILE_LIST_ID ** part_list_id, unsigned int part_cnt,
		  HASHJOIN_INPUT_STATS * stats)
{
  UINT64 sum = 0, max = 0;
  double avg, max_avg;
  unsigned int part_index;

  assert (list_id != NULL);
  assert (part_list_id != NULL);
  assert (part_cnt > 0);
  assert (stats != NULL);

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      max = MAX (max, part_list_id[part_index]->tuple_cnt);
      sum += part_list_id[part_index]->tuple_cnt;
    }

  avg = (double) list_id->tuple_cnt / part_cnt;
  max_avg = (avg != 0.0) ? (float) max / avg : 0.0;

  stats->rows = sum;
  stats->skew = max_avg;
}

#if HASHJOIN_PROFILE_TIME
/*
 * HJOIN_PROFILE_START() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   start_stats(in/out): Profiling data captured at the start of the step.
 *   step(in): Hash join profiling step to measure.
 */
static void
hjoin_profile_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats, HASHJOIN_PROFILE_STEP step)
{
  assert (thread_p != NULL);
  assert (start_stats != NULL);

  tsc_getticks (&start_stats->tick);
  start_stats->step = step;
}
#endif /* HASHJOIN_PROFILE_TIME */

#if HASHJOIN_PROFILE_TIME
/*
 * hjoin_profile_end() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   stats(in/out): Profiling data to accumulate.
 *   start_stats(in): Profiling data captured at the start of the step.
 *   step(in): Hash join profiling step being measured.
 */
static void
hjoin_profile_end (THREAD_ENTRY * thread_p, HASHJOIN_PROFILE_STATS * stats,
		   HASHJOIN_START_STATS * start_stats, HASHJOIN_PROFILE_STEP step)
{
  TSC_TICKS end_tick;
  TSCTIMEVAL tv_diff;

  assert (thread_p != NULL);
  assert (stats != NULL);
  assert (start_stats != NULL);
  assert (start_stats->step == step);

  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_stats->tick);

  switch (step)
    {
    case HASHJOIN_PROFILE_BUILD_FETCH:
      TSC_ADD_TIMEVAL (stats->build.fetch, tv_diff);
      break;

    case HASHJOIN_PROFILE_BUILD_HASH:
      TSC_ADD_TIMEVAL (stats->build.hash, tv_diff);
      break;

    case HASHJOIN_PROFILE_BUILD_INSERT:
      TSC_ADD_TIMEVAL (stats->build.insert, tv_diff);
      break;

    case HASHJOIN_PROFILE_PROBE_FETCH:
      TSC_ADD_TIMEVAL (stats->probe.fetch, tv_diff);
      break;

    case HASHJOIN_PROFILE_PROBE_HASH:
      TSC_ADD_TIMEVAL (stats->probe.hash, tv_diff);
      break;

    case HASHJOIN_PROFILE_PROBE_SEARCH:
      TSC_ADD_TIMEVAL (stats->probe.search, tv_diff);
      break;

    case HASHJOIN_PROFILE_PROBE_MATCH:
      TSC_ADD_TIMEVAL (stats->probe.match, tv_diff);
      break;

    case HASHJOIN_PROFILE_PROBE_ADD:
      TSC_ADD_TIMEVAL (stats->probe.add, tv_diff);
      break;

    default:
      /* impossible case */
      assert_release (false);
      break;
    }				/* switch (step) */
}
#endif /* HASHJOIN_PROFILE_TIME */

/*
 * hjoin_trace_merge_stats() -
 *   return: None.
 *   stats(in/out): Profiling data to be merged.
 *   context_stats(in): Profiling data per-partition.
 */
static void
hjoin_trace_merge_stats (HASHJOIN_STATS * stats, HASHJOIN_STATS * context_stats)
{
  assert (stats != NULL);
  assert (context_stats != NULL);

  TSC_ADD_TIMEVAL (stats->build.elapsed_time, context_stats->build.elapsed_time);
  TSC_ADD_TIMEVAL (stats->build.time, context_stats->build.time);
  stats->build.fetches += context_stats->build.fetches;
  stats->build.fetch_time += context_stats->build.fetch_time;
  stats->build.ioreads += context_stats->build.ioreads;
  stats->build.part_rows += context_stats->build.part_rows;
  /* stats->build.readkeys - Unused */
  stats->build.rows += context_stats->build.rows;
  /* stats->build.max_collisions - Unused */

#if HASHJOIN_PROFILE_TIME
  TSC_ADD_TIMEVAL (stats->profile.build.fetch, context_stats->profile.build.fetch);
  TSC_ADD_TIMEVAL (stats->profile.build.hash, context_stats->profile.build.hash);
  TSC_ADD_TIMEVAL (stats->profile.build.insert, context_stats->profile.build.insert);
#endif /* HASHJOIN_PROFILE_TIME */

  TSC_ADD_TIMEVAL (stats->probe.elapsed_time, context_stats->probe.elapsed_time);
  TSC_ADD_TIMEVAL (stats->probe.time, context_stats->probe.time);
  stats->probe.fetches += context_stats->probe.fetches;
  stats->probe.fetch_time += context_stats->probe.fetch_time;
  stats->probe.ioreads += context_stats->probe.ioreads;
  stats->probe.part_rows += context_stats->probe.part_rows;
  stats->probe.readkeys += context_stats->probe.readkeys;
  stats->probe.rows += context_stats->probe.rows;
  stats->probe.max_collisions = MAX (stats->probe.max_collisions, context_stats->probe.max_collisions);

#if HASHJOIN_PROFILE_TIME
  TSC_ADD_TIMEVAL (stats->profile.probe.fetch, context_stats->profile.probe.fetch);
  TSC_ADD_TIMEVAL (stats->profile.probe.hash, context_stats->profile.probe.hash);
  TSC_ADD_TIMEVAL (stats->profile.probe.search, context_stats->profile.probe.search);
  TSC_ADD_TIMEVAL (stats->profile.probe.match, context_stats->profile.probe.match);
  TSC_ADD_TIMEVAL (stats->profile.probe.add, context_stats->profile.probe.add);
#endif /* HASHJOIN_PROFILE_TIME */
}

#if HASHJOIN_DUMP_HASH_TABLE
/*
 * hjoin_dump_hash_table() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   hash_scan(in): Hash scan structure containing the hash table.
 *   list_id(in): List identifier used as build input.
 */
static void
hjoin_dump_hash_table (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_ID * list_id)
{
  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_id != NULL);

  if (list_id->tuple_cnt > DUMP_HASH_TABLE_LIMIT)
    {
      return;
    }

  switch (hash_scan->hash_list_scan_type)
    {
    case HASH_METH_IN_MEM:
    case HASH_METH_HYBRID:
      assert (hash_scan->memory.hash_table != NULL);
      mht_dump_hls (thread_p, stdout, hash_scan->memory.hash_table, 1, qdata_print_hash_scan_entry, &list_id->type_list,
		    (void *) &hash_scan->hash_list_scan_type);
      printf ("temp file : tuple count = %ld, file_size = %dK\n", list_id->tuple_cnt, list_id->page_cnt * 16);
      break;

    case HASH_METH_HASH_FILE:
      assert (hash_scan->file.hash_table != NULL);
      fhs_dump (thread_p, hash_scan->file.hash_table);
      break;

    case HASH_METH_NOT_USE:
      /* Nothing to do */
      break;

    default:
      /* impossible case */
      assert_release (false);
      break;
    }
}
#endif /* HASHJOIN_DUMP_HASH_TABLE */

#if !defined(NDEBUG) && HASHJOIN_DUMP_PROBE
/*
 * hjoin_print_tuple() -
 *   return: None.
 *   list_scan_id(in): Scan identifier for the given tuple.
 *   tuple(in): Tuple to be printed.
 *   step(in): Step at which the tuple is printed.
 */
static void
hjoin_print_tuple (QFILE_LIST_SCAN_ID * list_scan_id, QFILE_TUPLE tuple, HASHJOIN_PRINT_STEP step)
{
  assert (list_scan_id != NULL);
  assert (list_scan_id->list_id != NULL);
  assert (tuple != NULL);

  if (list_scan_id->list_id.tuple_cnt > DUMP_PROBE_LIMIT)
    {
      return;
    }

  switch (step)
    {
    case HASHJOIN_PRINT_READ_KEY:
      fprintf (stdout, "\nRead Key (Probe): ");
      break;

    case HASHJOIN_PRINT_NOT_MATCHED_KEY:
      fprintf (stdout, "\nNot Matched Key (Build): ");
      break;

    case HASHJOIN_PRINT_NOT_QUALIFIED_KEY:
      fprintf (stdout, "\nNot Qualified Key (Build): ");
      break;

    case HASHJOIN_PRINT_QUALIFIED_KEY:
      fprintf (stdout, "\nQualified Key (Build): ");
      break;

    case HASHJOIN_PRINT_FILL_EMPTY_KEY:
      fprintf (stdout, "\nFill Empty Key (Probe): ");
      break;

    default:
      /* impossible case */
      assert (false);
      /* Nothing to do */
      break;
    }

  qfile_print_tuple (list_scan_id->list_id.type_list, tuple);
}
#endif /* !NDEBUG && HASHJOIN_DUMP_PROBE */
