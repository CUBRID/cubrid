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
#include "error_manager.h"	/* er_errid, NO_ERROR, assert_release_error */
#include "fetch.h"		/* fetch_val_list */
#include "list_file.h"		/* qfile_open_list, qfile_close_list */
#include "memory_alloc.h"	/* CEIL_PTVDIV */
#include "object_representation.h"	/* TP_DOMAIN */
#include "perf_monitor.h"	/* perfmon_get_from_statistic, PSTAT_... */
#include "px_hash_join.hpp"	/* parallel_query::hash_join::... */
#include "px_parallel.hpp"	/* parallel_query::compute_parallel_degree */
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager */
#include "query_list.h"		/* JOIN_TYPE */
#include "query_manager.h"	/* QMGR_TEMP_FILE */
#include "system_parameter.h"	/* prm_get_bigint_value, PRM_ID_... */
#include "thread_entry.hpp"	/* THREAD_ENTRY */
#include "xasl.h"		/* XASL_NODE, HASHJOIN_PROC_NODE */

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

typedef enum hashjoin_print_step
{
  HASHJOIN_PRINT_NONE = 0,
  HASHJOIN_PRINT_READ_KEY,
  HASHJOIN_PRINT_NOT_MATCHED_KEY,
  HASHJOIN_PRINT_NOT_QUALIFIED_KEY,
  HASHJOIN_PRINT_QUALIFIED_KEY,
  HASHJOIN_PRINT_FILL_EMPTY_KEY
} HASHJOIN_PRINT_STEP;

/*
 * Macro Function Declarations
 */

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
static int hjoin_execute_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);
static int hjoin_outer_fill_null_values (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
					 HASHJOIN_CONTEXT * context);
static int hjoin_execute_internal (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);

/* Hash Join Manager */
static int hjoin_init_manager (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, XASL_NODE * xasl,
			       QUERY_ID query_id, VAL_DESCR * val_descr);
static void hjoin_clear_manager (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);

/* Hash Join Domain Info */
static int hjoin_init_domain_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				   HASHJOIN_DOMAIN_INFO * domain_info);

/* Hash Join Partitioning */
static HASHJOIN_STATUS hjoin_try_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
					    HASHJOIN_CONTEXT * single_context);
static HASHJOIN_STATUS hjoin_check_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
					      HASHJOIN_CONTEXT * single_context);
static int hjoin_prepare_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				    HASHJOIN_SPLIT_INFO * split_info);
static int hjoin_build_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				   HASHJOIN_SPLIT_INFO * split_info);
static int hjoin_split_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
			      HASHJOIN_INPUT_SPLIT_INFO * split_info, QFILE_LIST_ID ** temp_part_list_id,
			      HASH_SCAN_KEY * temp_key);

/* Hash Join Parallel */
static HASHJOIN_STATUS hjoin_try_parallel (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
					   HASHJOIN_CONTEXT * single_context);

/* Hash Join Split Info */
static int hjoin_init_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				  HASHJOIN_SPLIT_INFO * split_info);
static void hjoin_clear_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				    HASHJOIN_SPLIT_INFO * split_info, bool clear_all);

/* Hash Join Context */
static int hjoin_init_context (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
static void hjoin_clear_context (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context);
static void hjoin_destroy_qlist (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context);

/* Hash List Scan */
static int hjoin_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt, QFILE_LIST_ID * list_id);
static void hjoin_scan_clear (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan);

/* Hash Join Processing */
static HASHJOIN_STATUS hjoin_check_empty_inputs (HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);

/* Build Phase */
static int hjoin_build (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
static int hjoin_build_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan,
			    QFILE_LIST_SCAN_ID * list_scan_id, QFILE_TUPLE_RECORD * tuple_record);

/* Probe Phase */
static int hjoin_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			QFILE_LIST_ID * list_id);
static int hjoin_outer_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			      QFILE_LIST_ID * list_id);
static int hjoin_probe_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_SCAN_ID * list_scan_id,
			    QFILE_TUPLE_RECORD * tuple_record);

/* Merge QFILE_LIST_ID */
static int hjoin_merge_tuple_to_list_id (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
					 QFILE_TUPLE_RECORD * outer_record, QFILE_TUPLE_RECORD * inner_record,
					 QFILE_LIST_MERGE_INFO * merge_info, QFILE_TUPLE_RECORD * overflow_record);
static int hjoin_merge_tuple (THREAD_ENTRY * thread_p, QFILE_TUPLE_RECORD * outer_record,
			      QFILE_TUPLE_RECORD * inner_record, QFILE_LIST_MERGE_INFO * merge_info,
			      QFILE_TUPLE_RECORD * overflow_record);

/* Dump */
#if HASHJOIN_DUMP_HASH_TABLE
static void hjoin_dump_hash_table (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_ID * list_id);
#endif /* HASHJOIN_DUMP_HASH_TABLE */

#if !defined(NDEBUG) && HASHJOIN_DUMP_PROBE
static void hjoin_print_tuple (QFILE_LIST_SCAN_ID * list_scan_id, QFILE_TUPLE tuple, HASHJOIN_PRINT_STEP step);
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
 *   val_descr(in): Value descriptor for positional values.
 */
int
qexec_hash_join (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_ID query_id, VAL_DESCR * val_descr)
{
  HASHJOIN_MANAGER manager;
  HASHJOIN_CONTEXT *single_context;
  HASHJOIN_STATUS status, part_status;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (xasl != NULL);
  assert (query_id != NULL_QUERY_ID);

  error = hjoin_init_manager (thread_p, &manager, xasl, query_id, val_descr);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  single_context = &manager.single_context;

  status = hjoin_check_empty_inputs (&manager, single_context);
  single_context->status = status;
  switch (status)
    {
    case HASHJOIN_STATUS_FILL_NULL_VALUES:
      error = hjoin_outer_fill_null_values (thread_p, &manager, single_context);
      break;

    case HASHJOIN_STATUS_TRY:
      part_status = hjoin_try_partition (thread_p, &manager, single_context);
      single_context->status = part_status;
      switch (part_status)
	{
	case HASHJOIN_STATUS_SINGLE:
	  /* monitor */
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_HASHJOINS);

	  error = hjoin_execute (thread_p, &manager, single_context);
	  break;

	case HASHJOIN_STATUS_PARTITION:
	  /* monitor */
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_HASHJOINS_PARTITIONED);

	  error = hjoin_execute_partitions (thread_p, &manager);
	  break;

#if defined (SERVER_MODE)
	case HASHJOIN_STATUS_PARALLEL:
	  /* monitor */
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_HASHJOINS_PARALLEL);

	  if (thread_is_on_trace (thread_p))
	    {
	      xasl->executed_parallelism = manager.num_parallel_threads;
	    }

	  // *INDENT-OFF*
	  error = parallel_query::hash_join::execute_partitions (*thread_p, &manager);
	  // *INDENT-ON*
	  break;
#endif /* defined (SERVER_MODE) */

	case HASHJOIN_STATUS_END:
	  /* impossible case */
	  /* hjoin_check_empty_inputs guarantees HASHJOIN_STATUS_END cannot occur here */
	  assert_release_error (false);
	  goto error_exit;

	case HASHJOIN_STATUS_ERROR:
	  /* hjoin_try_partition always retries as HASHJOIN_STATUS_SINGLE;
	   * except for ER_INTERRUPTED, never returns HASHJOIN_STATUS_ERROR */
	  error = er_errid ();
	  assert_release_error (error == ER_INTERRUPTED);
	  goto error_exit;

	default:
	  /* impossible case */
	  assert_release_error (false);
	  goto error_exit;
	}
      break;

    case HASHJOIN_STATUS_END:
      /* Nothing to do */
      assert (single_context->list_id == NULL);
      break;

    case HASHJOIN_STATUS_ERROR:
      [[fallthrough]];
    default:
      /* impossible case */
      assert_release_error (false);
      goto error_exit;
    }

  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (single_context->list_id != NULL)
    {
      /* Check if qfile_close_list was called */
      assert (single_context->list_id->last_pgptr == NULL);

      qfile_destroy_list (thread_p, xasl->list_id);	/* may be unnecessary */
      qfile_copy_list_id (xasl->list_id, single_context->list_id, false, QFILE_MOVE_DEPENDENT);
      QFILE_FREE_AND_INIT_LIST_ID (single_context->list_id);

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
    }
  else if (status == HASHJOIN_STATUS_END)
    {
      ASSERT_NO_ERROR_OR_INTERRUPTED ();
    }
  else
    {
      /* list_id can be NULL when the join result is empty.
       * In this case, it is NO_ERROR.
       * Otherwise, an error may be set. */
      error = er_errid ();
    }

cleanup:
  hjoin_clear_manager (thread_p, &manager);

  return error;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  goto cleanup;
}

/*
 * hjoin_execute_partitions() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 */
static int
hjoin_execute_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  HASHJOIN_CONTEXT *current_context;
  UINT32 context_cnt, context_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  HASHJOIN_STATS *stats = manager->single_context.stats;
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  context_cnt = manager->context_cnt;

  for (context_index = 0; context_index < context_cnt; context_index++)
    {
      current_context = &manager->contexts[context_index];

      error = hjoin_execute (thread_p, manager, current_context);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      if (thread_is_on_trace (thread_p))
	{
	  hjoin_trace_merge_stats (stats, current_context->stats);
	}

      if (current_context->list_id == NULL)
	{
	  error = er_errid ();
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  else
	    {
	      /* list_id can be NULL when the join result is empty.
	       * In this case, it is NO_ERROR. */
	      continue;
	    }
	}

      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_MERGE);
      error = hjoin_merge_qlist (thread_p, manager, current_context);
      HJOIN_PROFILE_MERGE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_MERGE,
			       manager->single_context.list_id->tuple_cnt);

      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_execute() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
int
hjoin_execute (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  HASHJOIN_STATUS status;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  status = hjoin_check_empty_inputs (manager, context);

  /* In outer joins, tuples with NULL in any join column are placed in the last partition.
   * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition. */
  if (IS_OUTER_JOIN_TYPE (manager->join_type) && context == &manager->contexts[manager->context_cnt - 1])
    {
      status = (status == HASHJOIN_STATUS_TRY) ? HASHJOIN_STATUS_FILL_NULL_VALUES : status;
    }

  context->status = status;

  switch (status)
    {
    case HASHJOIN_STATUS_FILL_NULL_VALUES:
      assert (context != &manager->single_context);
      error = hjoin_outer_fill_null_values (thread_p, manager, context);
      break;

    case HASHJOIN_STATUS_TRY:
      error = hjoin_execute_internal (thread_p, manager, context);
      break;

    case HASHJOIN_STATUS_END:
      /* Nothing to do */
      break;

    case HASHJOIN_STATUS_ERROR:
    default:
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
      break;
    }

  /* Check if qfile_close_list was called */
  assert (context->list_id == NULL || context->list_id->last_pgptr == NULL);

  return error;
}

/*
 * hjoin_outer_fill_null_values() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
static int
hjoin_outer_fill_null_values (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *list_id = NULL;
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  SCAN_CODE scan_code;

  HASHJOIN_FETCH_INFO *outer, *inner;
  HASHJOIN_FETCH_INFO *build, *probe;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  outer = &context->outer;
  inner = &context->inner;

  switch (manager->join_type)
    {
    case JOIN_LEFT:
      context->build = inner;
      context->probe = outer;
      break;

    case JOIN_RIGHT:
      context->build = outer;
      context->probe = inner;
      break;

    default:
      /* impossible case */
      assert_release_error (false);
      goto error_exit;
    }

  build = context->build;
  probe = context->probe;

  /* Prevent faults when qfile_close_scan is called */
  probe->list_scan_id.status = S_CLOSED;

  // *INDENT-OFF*
  probe->tuple_record = { NULL, 0 };
  // *INDENT-ON*

  build->fill_record = NULL;
  probe->fill_record = &probe->tuple_record;

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, manager->qlist_flag, NULL);
  if (list_id == NULL)
    {
      goto error_exit;
    }

  error = qfile_open_list_scan (probe->list_id, &probe->list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);

      assert (stats->build.read_rows == 0);
      assert (stats->build.read_keys == 0);
      stats->build.qualified_rows = build->list_id->tuple_cnt;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &probe->list_scan_id, &probe->tuple_record, PEEK)) == S_SUCCESS)
    {
      error =
	hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record, manager->merge_info,
				      &overflow_record);
      if (error != NO_ERROR)
	{
	  break;
	}
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->probe, &start_stats);
      stats->probe.read_rows = probe->list_id->tuple_cnt;
      assert (stats->probe.read_keys == 0);
      stats->probe.qualified_rows = list_id->tuple_cnt;
    }

  /* After qfile_open_list_scan, if an error occurs,
   * ensure qfile_close_scan runs here
   * before jumping to error_exit. */
  qfile_close_scan (thread_p, &probe->list_scan_id);

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto error_exit;
    }

  qfile_close_list (thread_p, list_id);
  context->list_id = list_id;

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  if (overflow_record.tpl != NULL)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  hjoin_destroy_qlist (thread_p, context);

  /* Check if qfile_close_list was called */
  assert (list_id == NULL || list_id->last_pgptr == NULL);

  return error;

error_exit:
  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  goto cleanup;
}

/*
 * hjoin_execute_internal() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
static int
hjoin_execute_internal (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *list_id = NULL;

  HASHJOIN_FETCH_INFO *outer, *inner;
  HASHJOIN_FETCH_INFO *build = NULL, *probe = NULL;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->list_id == NULL);

  outer = &context->outer;
  inner = &context->inner;

  /* Prevent faults when qfile_close_scan is called */
  outer->list_scan_id.status = S_CLOSED;
  inner->list_scan_id.status = S_CLOSED;

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, manager->qlist_flag, NULL);
  if (list_id == NULL)
    {
      goto error_exit;
    }

  error = hjoin_init_context (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  build = context->build;
  probe = context->probe;
  assert (build != NULL);
  assert (probe != NULL);

  error = qfile_open_list_scan (build->list_id, &build->list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = hjoin_build (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = qfile_open_list_scan (probe->list_id, &probe->list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (IS_OUTER_JOIN_TYPE (manager->join_type))
    {
      error = hjoin_outer_probe (thread_p, manager, context, list_id);
    }
  else
    {
      error = hjoin_probe (thread_p, manager, context, list_id);
    }
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  qfile_close_list (thread_p, list_id);
  context->list_id = list_id;

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  qfile_close_scan (thread_p, &build->list_scan_id);
  qfile_close_scan (thread_p, &probe->list_scan_id);

  hjoin_destroy_qlist (thread_p, context);

  hjoin_scan_clear (thread_p, &context->hash_scan);

  /* Check if qfile_close_list was called */
  assert (list_id == NULL || list_id->last_pgptr == NULL);

  return error;

error_exit:
  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
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
 *   val_descr(in): Value descriptor for positional values.
 */
static int
hjoin_init_manager (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, XASL_NODE * xasl, QUERY_ID query_id,
		    VAL_DESCR * val_descr)
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

  manager->join_type = merge_info->join_type;
  manager->key_cnt = merge_info->ls_column_cnt;

  manager->during_join_pred = xasl->during_join_pred;
  manager->num_parallel_threads = xasl->parallelism;

  manager->query_id = query_id;
  manager->val_descr = val_descr;

  domain_info = &proc->domain_info;
  error = hjoin_init_domain_info (thread_p, manager, domain_info);
  if (error != NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* single_context */
  context = &manager->single_context;

  assert (context->list_id == NULL);

  context->outer.list_id = outer_list_id;
  context->outer.input = &domain_info->outer;
  context->outer.coerce_domains = domain_info->coerce_domains;
  context->outer.need_coerce_domains = domain_info->need_coerce_domains;
  context->outer.regu_list_pred = proc->outer.regu_list_pred;

  context->inner.list_id = inner_list_id;
  context->inner.input = &domain_info->inner;
  context->inner.coerce_domains = domain_info->coerce_domains;
  context->inner.need_coerce_domains = domain_info->need_coerce_domains;
  context->inner.regu_list_pred = proc->inner.regu_list_pred;

  /* Set in hjoin_init_context or hjoin_outer_fill_null_values. */
  assert (context->build == NULL);
  assert (context->probe == NULL);

  context->during_join_pred = manager->during_join_pred;
  context->val_descr = manager->val_descr;

  assert (context->status == HASHJOIN_STATUS_NONE);

  /* contexts */
  assert (manager->contexts == NULL);
  assert (manager->context_cnt == 0);

  /* type_list */
  type_list = &manager->type_list;
  assert (type_list->domp == NULL);
  assert (type_list->type_cnt == 0);

  type_cnt = merge_info->ls_pos_cnt;

  type_list->domp = (TP_DOMAIN **) db_private_alloc (thread_p, type_cnt * sizeof (TP_DOMAIN *));
  if (type_list->domp == NULL)
    {
      assert_release_error (er_errid () != NO_ERROR);
      return er_errid ();
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

  manager->qlist_merge_method = HASHJOIN_MERGE_CONNECT;
  manager->qlist_flag =
    (manager->qlist_merge_method == HASHJOIN_MERGE_CONNECT) ? QFILE_FLAG_ALL | QFILE_NOT_USE_MEMBUF : QFILE_FLAG_ALL;

  assert (manager->px_worker_manager == NULL);

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

#if defined (SERVER_MODE) && HASHJOIN_DUMP_HASH_TABLE
  pthread_mutex_init (&manager->dump_hash_table_mutex, NULL);
#endif /* defined (SERVER_MODE) && HASHJOIN_DUMP_HASH_TABLE */

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
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
  HASHJOIN_CONTEXT *single_context;
  HASHJOIN_CONTEXT *contexts = NULL;
  UINT32 context_cnt, context_index;

  assert (thread_p != NULL);
  assert (manager != NULL);

  single_context = &manager->single_context;

  if (single_context->list_id != NULL)
    {
      qfile_close_list (thread_p, single_context->list_id);
      qfile_destroy_list (thread_p, single_context->list_id);
      QFILE_FREE_AND_INIT_LIST_ID (single_context->list_id);
    }

  hjoin_destroy_qlist (thread_p, single_context);

  if (manager->contexts != NULL)
    {
      contexts = manager->contexts;
      context_cnt = manager->context_cnt;
      assert (context_cnt > 1);

      for (context_index = 0; context_index < context_cnt; context_index++)
	{
	  hjoin_clear_context (thread_p, &contexts[context_index]);
	}

      db_private_free_and_init (thread_p, contexts);

      manager->contexts = NULL;
      manager->context_cnt = 0;
    }
  else
    {
      assert (manager->context_cnt == 0);
    }

  if (manager->type_list.domp != NULL)
    {
      db_private_free_and_init (thread_p, manager->type_list.domp);
    }

#if defined (SERVER_MODE)
  if (manager->px_worker_manager != NULL)
    {
      manager->px_worker_manager->release_workers ();
      manager->px_worker_manager = NULL;
    }

  if (manager->px_worker_stats != NULL)
    {
      db_private_free_and_init (thread_p, manager->px_worker_stats);
    }

  THREAD_ENTRY *main_thread_p = thread_get_main_thread (thread_p);

  /* only top-level parent */
  if (main_thread_p == thread_p)
    {
      if (thread_p->m_px_stats != NULL && !thread_p->m_uses_px_stats)
	{
	  perfmon_merge_parallel_stats_to_tran_stats (thread_p);
	  free_and_init (thread_p->m_px_stats);
	}
    }
#else
  assert (manager->px_worker_manager == NULL);
  assert (manager->px_worker_stats == NULL);
  assert (thread_p->m_px_stats == NULL);
#endif /* defined (SERVER_MODE) */

#if defined (SERVER_MODE) && HASHJOIN_DUMP_HASH_TABLE
  pthread_mutex_destroy (&manager->dump_hash_table_mutex);
#endif /* defined (SERVER_MODE) && HASHJOIN_DUMP_HASH_TABLE */
}

/*
 * hjoin_init_domain_info() -
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
  bool need_coerce_domains;

  DB_TYPE outer_type, inner_type, common_type;
  int outer_precision, inner_precision;
  int outer_scale, inner_scale;
  int outer_integral, inner_integral;
  int common_precision, common_scale;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (domain_info != NULL);

  merge_info = manager->merge_info;
  assert (merge_info != NULL);

  assert (manager->outer != NULL);
  assert (manager->outer->xasl != NULL);
  assert (manager->outer->xasl->list_id != NULL);
  outer_list_id = manager->outer->xasl->list_id;

  assert (manager->inner != NULL);
  assert (manager->inner->xasl != NULL);
  assert (manager->inner->xasl->list_id != NULL);
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

  memset (coerce_domains, 0, domain_cnt * sizeof (TP_DOMAIN *));

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

      /* common_precision, common_scale */
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
		  assert_release_error (er_errid () != NO_ERROR);
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

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hjoin_try_partition() -
 *   return: One of the following HASHJOIN_STATUS values:
 *           - HASHJOIN_STATUS_SINGLE: Partitioning is not needed or falls back on error.
 *           - HASHJOIN_STATUS_PARTITION: Partitioning is applied.
 *           - HASHJOIN_STATUS_PARALLEL: Parallel execution is applied.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   single_context(in): Hash join context for single-threaded execution.
 */
static HASHJOIN_STATUS
hjoin_try_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * single_context)
{
  HASHJOIN_STATUS status;
  HASHJOIN_SPLIT_INFO split_info;
  UINT32 context_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (single_context != NULL);
  assert (single_context == &manager->single_context);

  status = hjoin_check_partition (thread_p, manager, single_context);
  if (status == HASHJOIN_STATUS_SINGLE)
    {
      return status;
    }

  assert (status == HASHJOIN_STATUS_PARTITION);

  error = hjoin_prepare_partition (thread_p, manager, &split_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

#if defined (SERVER_MODE) && !defined (WINDOWS)
  status = hjoin_try_parallel (thread_p, manager, single_context);
  single_context->status = status;
  if (status == HASHJOIN_STATUS_ERROR)
    {
      goto error_exit;
    }
#endif /* defined (SERVER_MODE) && !defined (WINDOWS) */

  switch (status)
    {
    case HASHJOIN_STATUS_PARTITION:
      if (thread_is_on_trace (thread_p))
	{
	  assert (single_context->stats != NULL);
	  assert (single_context->stats->num_parallel_threads == 0);
	}

      error = hjoin_build_partitions (thread_p, manager, &split_info);
      break;

#if defined (SERVER_MODE)
    case HASHJOIN_STATUS_PARALLEL:
      if (thread_is_on_trace (thread_p))
	{
	  assert (single_context->stats != NULL);
	  single_context->stats->num_parallel_threads = manager->num_parallel_threads;
	}

      // *INDENT-OFF*
      error = parallel_query::hash_join::build_partitions (*thread_p, manager, &split_info);
      // *INDENT-ON*
      break;
#endif /* defined (SERVER_MODE) */

    case HASHJOIN_STATUS_ERROR:
      goto error_exit;

    default:
      /* impossible case */
      assert_release_error (false);
      goto error_exit;
    }

  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* call hjoin_destroy_qlist after build_partitions is done,
   * since HASHJOIN_STATUS_SINGLE may retry on error*/
  hjoin_destroy_qlist (thread_p, single_context);

  assert (status == HASHJOIN_STATUS_PARTITION || status == HASHJOIN_STATUS_PARALLEL);

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  hjoin_clear_split_info (thread_p, manager, &split_info, false);

  return status;

error_exit:
#if defined (SERVER_MODE)
  if (manager->px_worker_manager != NULL)
    {
      manager->px_worker_manager->release_workers ();
      manager->px_worker_manager = NULL;
    }
#else
  assert (manager->px_worker_manager == NULL);
#endif /* defined (SERVER_MODE) */

  hjoin_clear_split_info (thread_p, manager, &split_info, true);

  if (manager->contexts != NULL)
    {
      assert (manager->context_cnt > 1);

      for (context_index = 0; context_index < manager->context_cnt; context_index++)
	{
	  hjoin_clear_context (thread_p, &manager->contexts[context_index]);
	}

      db_private_free_and_init (thread_p, manager->contexts);
      manager->context_cnt = 0;
    }

  if (thread_is_on_trace (thread_p))
    {
      assert (manager->stats_group != NULL);

      if (manager->stats_group->context_stats != NULL)
	{
	  free_and_init (manager->stats_group->context_stats);
	}
      manager->stats_group->context_cnt = 0;
    }
  else
    {
      assert (manager->stats_group == NULL);
    }

  if (error == ER_INTERRUPTED || er_errid () == ER_INTERRUPTED)
    {
      status = HASHJOIN_STATUS_ERROR;
    }
  else
    {
      /* fallback to HASHJOIN_STATUS_SINGLE */
      er_clear ();
      status = HASHJOIN_STATUS_SINGLE;
    }

  goto cleanup;
}

/*
 * hjoin_check_partition() -
 *   return: One of the following HASHJOIN_STATUS values:
 *           - HASHJOIN_STATUS_SINGLE: Partitioning is not needed.
 *           - HASHJOIN_STATUS_PARTITION: Partitioning is applied.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   single_context(in): Hash join context for single-threaded execution.
 */
static HASHJOIN_STATUS
hjoin_check_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * single_context)
{
  QFILE_LIST_ID *outer_list_id, *inner_list_id;

  UINT64 mem_limit;
  INT64 min_tuple_cnt;
  UINT32 part_cnt;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (single_context != NULL);
  assert (single_context == &manager->single_context);

  outer_list_id = single_context->outer.list_id;
  inner_list_id = single_context->inner.list_id;
  assert (outer_list_id != NULL);
  assert (inner_list_id != NULL);

  mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
  assert (mem_limit > 0);

  min_tuple_cnt =
    (outer_list_id->tuple_cnt < inner_list_id->tuple_cnt) ? outer_list_id->tuple_cnt : inner_list_id->tuple_cnt;
  assert (min_tuple_cnt >= 0);

  part_cnt =
    CEIL_PTVDIV ((sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)) * min_tuple_cnt,
		 mem_limit * PARTITION_FILL_FACTOR);
  if (part_cnt > 1)
    {
      if (IS_OUTER_JOIN_TYPE (manager->join_type))
	{
	  /* In outer joins, tuples with NULL in any join column are placed in the last partition.
	   * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in this partition. */
	  part_cnt += 1;
	}

      manager->context_cnt = part_cnt;

      return HASHJOIN_STATUS_PARTITION;
    }
  else
    {
      assert (manager->context_cnt == 0);

      return HASHJOIN_STATUS_SINGLE;
    }
}

/*
 * hjoin_prepare_partition() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   split_info(in): Split information.
 */
static int
hjoin_prepare_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_SPLIT_INFO * split_info)
{
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  QFILE_LIST_ID **outer_part_list_id = NULL, **inner_part_list_id = NULL;

  HASHJOIN_CONTEXT *single_context;
  HASHJOIN_CONTEXT *contexts = NULL, *current_context;
  HASHJOIN_STATS *context_stats = NULL;

  UINT32 part_cnt, part_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (split_info != NULL);

  single_context = &manager->single_context;
  outer_list_id = single_context->outer.list_id;
  inner_list_id = single_context->inner.list_id;
  assert (outer_list_id != NULL);
  assert (inner_list_id != NULL);

  error = hjoin_init_split_info (thread_p, manager, split_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  outer_part_list_id = split_info->outer.part_list_id;
  inner_part_list_id = split_info->inner.part_list_id;
  assert (outer_part_list_id != NULL);
  assert (inner_part_list_id != NULL);

  part_cnt = manager->context_cnt;
  assert (part_cnt > 1);

  contexts = (HASHJOIN_CONTEXT *) db_private_alloc (thread_p, part_cnt * sizeof (HASHJOIN_CONTEXT));
  if (contexts == NULL)
    {
      goto error_exit;
    }
  memset (contexts, 0, part_cnt * sizeof (HASHJOIN_CONTEXT));

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      current_context = &contexts[part_index];

      outer_part_list_id[part_index] =
	qfile_open_list (thread_p, &outer_list_id->type_list, NULL, outer_list_id->query_id, QFILE_FLAG_ALL, NULL);
      if (outer_part_list_id[part_index] == NULL)
	{
	  goto error_exit;
	}

      inner_part_list_id[part_index] =
	qfile_open_list (thread_p, &inner_list_id->type_list, NULL, inner_list_id->query_id, QFILE_FLAG_ALL, NULL);
      if (inner_part_list_id[part_index] == NULL)
	{
	  goto error_exit;
	}

      assert (current_context->list_id == NULL);

      current_context->outer.list_id = outer_part_list_id[part_index];
      current_context->outer.input = single_context->outer.input;
      current_context->outer.coerce_domains = single_context->outer.coerce_domains;
      current_context->outer.need_coerce_domains = single_context->outer.need_coerce_domains;
      current_context->outer.regu_list_pred = single_context->outer.regu_list_pred;

      current_context->inner.list_id = inner_part_list_id[part_index];
      current_context->inner.input = single_context->inner.input;
      current_context->inner.coerce_domains = single_context->inner.coerce_domains;
      current_context->inner.need_coerce_domains = single_context->inner.need_coerce_domains;
      current_context->inner.regu_list_pred = single_context->inner.regu_list_pred;

      assert (current_context->build == NULL);
      assert (current_context->probe == NULL);

      current_context->during_join_pred = single_context->during_join_pred;
      current_context->val_descr = single_context->val_descr;
    }

  manager->contexts = contexts;

  if (thread_is_on_trace (thread_p))
    {
      context_stats = (HASHJOIN_STATS *) malloc (part_cnt * sizeof (HASHJOIN_STATS));
      if (context_stats == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, part_cnt * sizeof (HASHJOIN_STATS));
	  goto error_exit;
	}
      memset (context_stats, 0, part_cnt * sizeof (HASHJOIN_STATS));

      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  contexts[part_index].stats = &context_stats[part_index];
	}

      assert (manager->stats_group != NULL);
      manager->stats_group->context_stats = context_stats;
      manager->stats_group->context_cnt = part_cnt;
    }
  else
    {
      assert (manager->stats_group == NULL);
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  if (contexts != NULL)
    {
      db_private_free_and_init (thread_p, contexts);
    }

  hjoin_clear_split_info (thread_p, manager, split_info, true);

  if (thread_is_on_trace (thread_p))
    {
      if (context_stats != NULL)
	{
	  free_and_init (context_stats);
	}

      assert (manager->stats_group != NULL);
      manager->stats_group->context_stats = NULL;
      manager->stats_group->context_cnt = 0;
    }
  else
    {
      assert (context_stats == NULL);
      assert (manager->stats_group == NULL);
    }

  manager->contexts = NULL;

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_build_partitions() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   split_info(in): Split information.
 */
static int
hjoin_build_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_SPLIT_INFO * split_info)
{
  QFILE_LIST_ID **temp_part_list_id = NULL;
  HASH_SCAN_KEY *temp_key = NULL;
  UINT32 part_cnt, part_index;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (split_info != NULL);

  HASHJOIN_STATS *stats = manager->single_context.stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  part_cnt = manager->context_cnt;

  temp_part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, part_cnt * sizeof (QFILE_LIST_ID *));
  if (temp_part_list_id == NULL)
    {
      goto error_exit;
    }
  memset (temp_part_list_id, 0, part_cnt * sizeof (QFILE_LIST_ID *));

  temp_key = qdata_alloc_hscan_key (thread_p, manager->key_cnt, true);
  if (temp_key == NULL)
    {
      goto error_exit;
    }

  error = hjoin_split_qlist (thread_p, manager, &split_info->outer, temp_part_list_id, temp_key);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = hjoin_split_qlist (thread_p, manager, &split_info->inner, temp_part_list_id, temp_key);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->split, &start_stats);
    }

  if (temp_part_list_id != NULL)
    {
      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  if (temp_part_list_id[part_index] != NULL)
	    {
	      qfile_close_list (thread_p, temp_part_list_id[part_index]);
	      qfile_destroy_list (thread_p, temp_part_list_id[part_index]);
	      QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_index]);
	    }
	}
      db_private_free_and_init (thread_p, temp_part_list_id);
    }

  if (temp_key != NULL)
    {
      qdata_free_hscan_key (thread_p, temp_key, manager->key_cnt);
    }

  return error;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  goto cleanup;
}

/*
 * hjoin_split_qlist() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   split_info(in): Split information.
 *   key(in/out): Space for reading join column values.
 */
static int
hjoin_split_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_INPUT_SPLIT_INFO * split_info,
		   QFILE_LIST_ID ** temp_part_list_id, HASH_SCAN_KEY * temp_key)
{
  QFILE_LIST_ID *list_id;
  QFILE_LIST_ID **part_list_id;
  QFILE_LIST_SCAN_ID list_scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE scan_code;

  unsigned int hash_key;
  UINT32 part_cnt, part_index, part_id;

  bool is_outer_join = false;
  bool need_skip_next = false;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (split_info != NULL);
  assert (split_info->fetch_info != NULL);
  assert (temp_part_list_id != NULL);
  assert (temp_key != NULL);

  list_id = split_info->fetch_info->list_id;
  part_list_id = split_info->part_list_id;
  part_cnt = manager->context_cnt;
  assert (list_id != NULL);
  assert (part_list_id != NULL);
  assert (part_cnt > 1);

  /* Prevent faults when qfile_close_scan is called */
  list_scan_id.status = S_CLOSED;

  is_outer_join = IS_OUTER_JOIN_TYPE (manager->join_type);

  error = qfile_open_list_scan (list_id, &list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error = hjoin_fetch_key (thread_p, split_info->fetch_info, &tuple_record, temp_key, NULL /* compare_key */ ,
			       &need_skip_next);
      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
      else if (need_skip_next)
	{
	  need_skip_next = false;	/* init */

	  if (is_outer_join)
	    {
	      /* In outer joins, tuples with NULL in any join column are placed in the last partition.
	       * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition. */
	      part_id = part_cnt - 1;
	    }
	  else
	    {
	      /* next tuple */
	      continue;
	    }
	}			/* else if (need_skip_next) */
      else
	{
	  hash_key = qdata_hash_scan_key (temp_key, UINT_MAX, HASH_METH_IN_MEM);
	  part_id = (is_outer_join) ? hash_key % (part_cnt - 1) : hash_key % (part_cnt);

	  hjoin_update_tuple_hash_key (thread_p, &tuple_record, hash_key);
	}

      /* overflow page */
      if (QFILE_GET_OVERFLOW_PAGE_ID (list_scan_id.curr_pgptr) != NULL_PAGEID)
	{
	  assert (part_list_id[part_id]->last_pgptr == NULL);

	  if (qfile_reopen_list_as_append_mode (thread_p, part_list_id[part_id]) != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  error = qfile_add_tuple_to_list (thread_p, part_list_id[part_id], tuple_record.tpl);
	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  qfile_close_list (thread_p, part_list_id[part_id]);

	  /* next tuple */
	  continue;
	}

      if (temp_part_list_id[part_id] != NULL &&
	  (temp_part_list_id[part_id]->tfile_vfid->membuf_last ==
	   temp_part_list_id[part_id]->tfile_vfid->membuf_npages - 1) &&
	  (temp_part_list_id[part_id]->last_offset + QFILE_GET_TUPLE_LENGTH (tuple_record.tpl)) > DB_PAGESIZE)
	{
	  qfile_close_list (thread_p, temp_part_list_id[part_id]);	/* may be meaningless since only memory buffer is used */

	  assert (part_list_id[part_id]->last_pgptr == NULL);

	  if (part_list_id[part_id]->tuple_cnt > 0)
	    {
	      error = qfile_append_list (thread_p, part_list_id[part_id], temp_part_list_id[part_id]);
	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}

	      error = qfile_truncate_list (thread_p, temp_part_list_id[part_id]);
	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}
	    }
	  else
	    {
	      qfile_destroy_list (thread_p, part_list_id[part_id]);
	      qfile_copy_list_id (part_list_id[part_id], temp_part_list_id[part_id], false, QFILE_PROHIBIT_DEPENDENT);
	      QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_id]);
	    }
	}

      if (temp_part_list_id[part_id] == NULL)
	{
	  temp_part_list_id[part_id] =
	    qfile_open_list (thread_p, &list_id->type_list, NULL, list_id->query_id, QFILE_FLAG_ALL, NULL);
	  if (temp_part_list_id[part_id] == NULL)
	    {
	      break;		/* error_exit */
	    }
	}

      error = qfile_add_tuple_to_list (thread_p, temp_part_list_id[part_id], tuple_record.tpl);
      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
      assert (VFID_ISNULL (&temp_part_list_id[part_id]->tfile_vfid->temp_vfid));
    }				/* while (qfile_scan_list_next (list_scan_id)) */

  /* After qfile_open_list_scan, if an error occurs,
   * ensure qfile_close_scan runs here
   * before jumping to error_exit. */
  qfile_close_scan (thread_p, &list_scan_id);

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      if (temp_part_list_id[part_index] != NULL)
	{
	  qfile_close_list (thread_p, temp_part_list_id[part_index]);	/* may be meaningless since only memory buffer is used */

	  if (temp_part_list_id[part_index]->tuple_cnt > 0)
	    {
	      assert (part_list_id[part_index]->last_pgptr == NULL);

	      if (part_list_id[part_index]->tuple_cnt > 0)
		{
		  error = qfile_append_list (thread_p, part_list_id[part_index], temp_part_list_id[part_index]);
		  if (error != NO_ERROR)
		    {
		      break;	/* error_exit */
		    }

		  qfile_destroy_list (thread_p, temp_part_list_id[part_index]);
		}
	      else
		{
		  qfile_destroy_list (thread_p, part_list_id[part_index]);
		  qfile_copy_list_id (part_list_id[part_index], temp_part_list_id[part_index], false,
				      QFILE_PROHIBIT_DEPENDENT);
		}
	    }
	  else
	    {
	      qfile_destroy_list (thread_p, temp_part_list_id[part_index]);
	    }

	  QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_index]);
	}
    }

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto error_exit;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_merge_qlist() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
int
hjoin_merge_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *new_list_id = NULL;

  HASHJOIN_CONTEXT *single_context;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  /* Check if qfile_close_list was called */
  assert (context->list_id != NULL);
  assert (context->list_id->last_pgptr == NULL);

  single_context = &manager->single_context;
  assert (single_context != context);

  if (single_context->list_id == NULL)
    {
      single_context->list_id = context->list_id;
      context->list_id = NULL;
      return NO_ERROR;
    }
  else if (single_context->list_id->tuple_cnt == 0)
    {
      qfile_destroy_list (thread_p, single_context->list_id);
      QFILE_FREE_AND_INIT_LIST_ID (single_context->list_id);

      single_context->list_id = context->list_id;
      context->list_id = NULL;
      return NO_ERROR;
    }
  else if (context->list_id->tuple_cnt == 0)
    {
      qfile_destroy_list (thread_p, context->list_id);
      QFILE_FREE_AND_INIT_LIST_ID (context->list_id);
      return NO_ERROR;
    }

  /* Check if qfile_close_list was called */
  assert (single_context->list_id->last_pgptr == NULL);

  switch (manager->qlist_merge_method)
    {
    case HASHJOIN_MERGE_COMBINE:
      {
	new_list_id =
	  qfile_combine_two_list (thread_p, single_context->list_id, context->list_id,
				  QFILE_FLAG_ALL | QFILE_FLAG_UNION);
	if (new_list_id == NULL)
	  {
	    goto error_exit;
	  }

	qfile_destroy_list (thread_p, single_context->list_id);
	QFILE_FREE_AND_INIT_LIST_ID (single_context->list_id);

	qfile_destroy_list (thread_p, context->list_id);
	QFILE_FREE_AND_INIT_LIST_ID (context->list_id);

	single_context->list_id = new_list_id;
	new_list_id = NULL;

	break;
      }

    case HASHJOIN_MERGE_APPEND:
      {
	error = qfile_append_list (thread_p, single_context->list_id, context->list_id);
	if (error != NO_ERROR)
	  {
	    goto error_exit;
	  }

	qfile_destroy_list (thread_p, context->list_id);
	QFILE_FREE_AND_INIT_LIST_ID (context->list_id);

	break;
      }

    case HASHJOIN_MERGE_CONNECT:
      {
	error = qfile_connect_list (thread_p, single_context->list_id, context->list_id);
	if (error != NO_ERROR)
	  {
	    goto error_exit;
	  }

	/* Do not call QFILE_FREE_AND_INIT_LIST_ID; it must be called through single_context->list_id. */
	context->list_id = NULL;
	break;
      }

    default:
      /* impossible case */
      assert_release_error (false);
      goto error_exit;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_try_parallel() -
 *   return: One of the following HASHJOIN_STATUS values:
 *           - HASHJOIN_STATUS_PARTITION: Parallel execution is not applied or falls back on error.
 *           - HASHJOIN_STATUS_PARALLEL: Parallel execution is applied.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   single_context(in): Hash join context for single-threaded execution.
 */
static HASHJOIN_STATUS
hjoin_try_parallel (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * single_context)
{
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  INT64 min_page_cnt;

  parallel_query::worker_manager * px_worker_manager = NULL;
  UINT64 *px_worker_stats = NULL;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (single_context != NULL);
  assert (single_context == &manager->single_context);

#if !defined (SERVER_MODE)
  assert (false);
#endif /* defined (SERVER_MODE) */

  outer_list_id = single_context->outer.list_id;
  inner_list_id = single_context->inner.list_id;
  assert (outer_list_id != NULL);
  assert (inner_list_id != NULL);

  /* immutable */
  static const size_t stats_size = perfmon_get_number_of_statistic_values () * sizeof (UINT64);

  /* check if pages are enough for parallel-thread hash join */
  min_page_cnt =
    (outer_list_id->page_cnt < inner_list_id->page_cnt) ? outer_list_id->page_cnt : inner_list_id->page_cnt;
  assert (min_page_cnt >= 0);

  manager->num_parallel_threads =
    parallel_query::compute_parallel_degree (parallel_query::parallel_type::HASH_JOIN, min_page_cnt,
					     manager->num_parallel_threads);
  if (manager->num_parallel_threads < 2)
    {
      /* try single-thread hash join */
      assert (manager->num_parallel_threads == 0);
      assert (manager->px_worker_manager == NULL);
      return HASHJOIN_STATUS_PARTITION;
    }

  manager->num_parallel_threads = MIN (manager->num_parallel_threads, manager->context_cnt /* part_cnt */ );

  px_worker_manager = parallel_query::worker_manager::try_reserve_workers (manager->num_parallel_threads);
  if (px_worker_manager == NULL)
    {
      goto error_exit;
    }

  /* update to actual reserved workers */
  manager->num_parallel_threads = px_worker_manager->get_reserved_workers ();

  if (thread_is_on_trace (thread_p))
    {
      px_worker_stats = (UINT64 *) db_private_alloc (thread_p, manager->num_parallel_threads * stats_size);
      if (px_worker_stats == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  goto error_exit;
	}
      memset (px_worker_stats, 0, manager->num_parallel_threads * stats_size);

      /* only top-level parent */
      if (thread_p->m_px_stats == NULL)
	{
	  thread_p->m_px_stats = perfmon_allocate_values ();
	  if (thread_p->m_px_stats == NULL)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      goto error_exit;
	    }
	  memset (thread_p->m_px_stats, 0, stats_size);
	}

      manager->px_worker_stats = px_worker_stats;
    }
  else
    {
      assert (manager->px_worker_stats == NULL);
    }

  manager->px_worker_manager = px_worker_manager;

  return HASHJOIN_STATUS_PARALLEL;

error_exit:
  manager->num_parallel_threads = 0;

  if (px_worker_manager != NULL)
    {
      px_worker_manager->release_workers ();
    }

  if (px_worker_stats != NULL)
    {
      db_private_free_and_init (thread_p, px_worker_stats);
    }

  if (er_errid () == ER_INTERRUPTED)
    {
      return HASHJOIN_STATUS_ERROR;
    }
  else
    {
      /* fallback to HASHJOIN_STATUS_PARTITION */
      er_clear ();
      return HASHJOIN_STATUS_PARTITION;
    }
}

/*
 * hjoin_init_split_info() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   split_info(in/out): Split information.
 */
static int
hjoin_init_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_SPLIT_INFO * split_info)
{
  HASHJOIN_CONTEXT *single_context;
  HASHJOIN_INPUT_SPLIT_INFO *outer, *inner;
  UINT32 part_cnt;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (split_info != NULL);

  single_context = &manager->single_context;

  outer = &split_info->outer;
  inner = &split_info->inner;

  outer->fetch_info = &single_context->outer;
  outer->part_list_id = NULL;

  inner->fetch_info = &single_context->inner;
  inner->part_list_id = NULL;

  part_cnt = manager->context_cnt;
  assert (part_cnt > 1);

  outer->part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, part_cnt * sizeof (QFILE_LIST_ID *));
  if (outer->part_list_id == NULL)
    {
      goto error_exit;
    }
  memset (outer->part_list_id, 0, part_cnt * sizeof (QFILE_LIST_ID *));

  inner->part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, part_cnt * sizeof (QFILE_LIST_ID *));
  if (inner->part_list_id == NULL)
    {
      goto error_exit;
    }
  memset (inner->part_list_id, 0, part_cnt * sizeof (QFILE_LIST_ID *));

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  hjoin_clear_split_info (thread_p, manager, split_info, true);

  assert_release_error (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * hjoin_clear_split_info() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   split_info(in): Split information.
 *   clear_all(in): True to destroy list identifiers; false otherwise.
 */
static void
hjoin_clear_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_SPLIT_INFO * split_info,
			bool clear_all)
{
  HASHJOIN_INPUT_SPLIT_INFO *outer, *inner;
  UINT32 part_cnt, part_index;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (split_info != NULL);

  outer = &split_info->outer;
  inner = &split_info->inner;

  part_cnt = manager->context_cnt;
  if (part_cnt <= 1)
    {
      assert (outer->part_list_id == NULL);
      assert (inner->part_list_id == NULL);
      return;			/* nothing to do */
    }

  /* The list identifier is still used through contexts[].outer/inner.list_id,
   * even though part_list_id is freed. */

  if (outer->part_list_id != NULL)
    {
      if (clear_all)
	{
	  for (part_index = 0; part_index < part_cnt; part_index++)
	    {
	      qfile_close_list (thread_p, outer->part_list_id[part_index]);
	      qfile_destroy_list (thread_p, outer->part_list_id[part_index]);
	      QFILE_FREE_AND_INIT_LIST_ID (outer->part_list_id[part_index]);

	      if (manager->contexts != NULL)
		{
		  manager->contexts[part_index].outer.list_id = NULL;
		}
	    }
	}
      db_private_free_and_init (thread_p, outer->part_list_id);
    }

  if (inner->part_list_id != NULL)
    {
      if (clear_all)
	{
	  for (part_index = 0; part_index < part_cnt; part_index++)
	    {
	      qfile_close_list (thread_p, inner->part_list_id[part_index]);
	      qfile_destroy_list (thread_p, inner->part_list_id[part_index]);
	      QFILE_FREE_AND_INIT_LIST_ID (inner->part_list_id[part_index]);

	      if (manager->contexts != NULL)
		{
		  manager->contexts[part_index].inner.list_id = NULL;
		}
	    }
	}
      db_private_free_and_init (thread_p, inner->part_list_id);
    }
}

/*
 * hjoin_init_shared_split_info() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   shared_info(in/out): Shared split information.
 */
int
hjoin_init_shared_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
			      HASHJOIN_SHARED_SPLIT_INFO * shared_info)
{
  UINT32 part_cnt, part_index;
  UINT32 init_cnt = 0;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (shared_info != NULL);

  part_cnt = manager->context_cnt;
  assert (part_cnt > 1);

  if (manager->px_worker_manager != NULL)
    {
      assert (shared_info->part_mutexes == NULL);

      shared_info->part_mutexes = (std::mutex *) db_private_alloc (thread_p, part_cnt * sizeof (std::mutex));
      if (shared_info->part_mutexes == NULL)
	{
	  goto error_exit;
	}

      try
      {
	for (part_index = 0; part_index < part_cnt; part_index++)
	  {
	    placement_new < std::mutex > (&shared_info->part_mutexes[part_index]);
	    ++init_cnt;
	  }
      }
      catch ( ...)
      {
	goto error_exit;
      }
    }

  assert (shared_info->membuf_claimed.load () == false);
  assert (shared_info->next_sector_index.load () == 0);

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  if (shared_info->part_mutexes != NULL)
    {
      for (part_index = 0; part_index < init_cnt; part_index++)
	{
	  // *INDENT-OFF*
	  shared_info->part_mutexes[part_index].~mutex ();
	  // *INDENT-ON*
	}
      db_private_free_and_init (thread_p, shared_info->part_mutexes);
    }

  assert_release_error (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * hjoin_clear_shared_split_info() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   shared_info(in): Shared split information.
 */
void
hjoin_clear_shared_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
			       HASHJOIN_SHARED_SPLIT_INFO * shared_info)
{
  UINT32 part_cnt, part_index;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (shared_info != NULL);

  /* NOTE: sector_info must be freed BEFORE the early-return below.
   * Do not move this call into the (part_cnt > 1) branch — doing so would leak
   * sectors/tfiles arrays when part_cnt <= 1. */
  qfile_free_list_sector_info (thread_p, &shared_info->sector_info);

  part_cnt = manager->context_cnt;
  if (part_cnt <= 1)
    {
      assert (shared_info->part_mutexes == NULL);
      return;			/* nothing more to do */
    }

  if (shared_info->part_mutexes != NULL)
    {
      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  // *INDENT-OFF*
	  shared_info->part_mutexes[part_index].~mutex ();
	  // *INDENT-ON*
	}
      db_private_free_and_init (thread_p, shared_info->part_mutexes);
    }
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
  HASHJOIN_FETCH_INFO *outer, *inner;
  HASHJOIN_FETCH_INFO *build = NULL;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (!thread_is_on_trace (thread_p) || context->stats != NULL);

  outer = &context->outer;
  inner = &context->inner;
  assert (outer->list_id != NULL && outer->list_id->tuple_cnt > 0);
  assert (inner->list_id != NULL && inner->list_id->tuple_cnt > 0);

  switch (manager->join_type)
    {
    case JOIN_INNER:
      if (outer->list_id->tuple_cnt < inner->list_id->tuple_cnt)
	{
	  context->build = outer;
	  context->probe = inner;
	}
      else if (outer->list_id->tuple_cnt == inner->list_id->tuple_cnt
	       && outer->list_id->page_cnt < inner->list_id->page_cnt)
	{
	  context->build = outer;
	  context->probe = inner;
	}
      else
	{
	  context->build = inner;
	  context->probe = outer;
	}
      break;

    case JOIN_LEFT:
      outer->fill_record = &outer->tuple_record;
      inner->fill_record = NULL;

      context->build = inner;
      context->probe = outer;
      break;

    case JOIN_RIGHT:
      outer->fill_record = NULL;
      inner->fill_record = &inner->tuple_record;

      context->build = outer;
      context->probe = inner;
      break;

    default:
      /* impossible case */
      assert_release_error (false);
      goto error_exit;
    }

  build = context->build;
  assert (build != NULL);

  error = hjoin_scan_init (thread_p, &context->hash_scan, manager->key_cnt, build->list_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      context->stats->hash_method = context->hash_scan.hash_list_scan_type;
      context->stats->swap_join_inputs = (context->build == outer) ? true : false;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  hjoin_scan_clear (thread_p, &context->hash_scan);

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_clear_context() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   context(in): Hash join context to clear.
 */
static void
hjoin_clear_context (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context)
{
  HASHJOIN_FETCH_INFO *outer, *inner;

  assert (thread_p != NULL);
  assert (context != NULL);

  outer = &context->outer;
  inner = &context->inner;

  if (context->list_id != NULL)
    {
      qfile_close_list (thread_p, context->list_id);
      qfile_destroy_list (thread_p, context->list_id);
      QFILE_FREE_AND_INIT_LIST_ID (context->list_id);
    }

  if (outer->list_id != NULL)
    {
      qfile_close_list (thread_p, outer->list_id);
      qfile_destroy_list (thread_p, outer->list_id);
      QFILE_FREE_AND_INIT_LIST_ID (outer->list_id);
    }

  if (inner->list_id != NULL)
    {
      qfile_close_list (thread_p, inner->list_id);
      qfile_destroy_list (thread_p, inner->list_id);
      QFILE_FREE_AND_INIT_LIST_ID (inner->list_id);
    }
}

/*
 * hjoin_destroy_qlist() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   context(in): Hash join context to destroy.
 */
static void
hjoin_destroy_qlist (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context)
{
  HASHJOIN_FETCH_INFO *outer, *inner;

  assert (thread_p != NULL);
  assert (context != NULL);

  outer = &context->outer;
  inner = &context->inner;

  if (outer->list_id != NULL)
    {
      qfile_close_list (thread_p, outer->list_id);
      qfile_destroy_list (thread_p, outer->list_id);
    }

  if (inner->list_id != NULL)
    {
      qfile_close_list (thread_p, inner->list_id);
      qfile_destroy_list (thread_p, inner->list_id);
    }
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

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  hjoin_scan_clear (thread_p, hash_scan);

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

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

  hash_scan->hash_list_scan_type = HASH_METH_NOT_USE;
}

/*
 * hjoin_check_empty_inputs() -
 *   return: One of the following HASHJOIN_STATUS values:
 *           - HASHJOIN_STATUS_END: Inner join with one empty input, or outer join with empty preserved side.
 *           - HASHJOIN_STATUS_FILL_NULL_VALUES: Outer join with empty null-supplying side.
 *           - HASHJOIN_STATUS_TRY: Both inputs are non-empty; proceed with the join.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
static HASHJOIN_STATUS
hjoin_check_empty_inputs (HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  HASHJOIN_FETCH_INFO *outer, *inner;
  INT64 outer_tuple_cnt, inner_tuple_cnt;
  HASHJOIN_STATUS status;

  assert (manager != NULL);
  assert (context != NULL);

  outer = &context->outer;
  inner = &context->inner;

  /* When aptr_list is executed in qexec_execute_mainblock_internal,
   * it checks the results from outer_xasl and inner_xasl in merge_info.
   * If either has no result, the other is skipped,
   * and the skipped node can have a type count of 0 in list_id.type_list. */
  if (outer->list_id == NULL || inner->list_id == NULL)
    {
      return HASHJOIN_STATUS_END;
    }

  outer_tuple_cnt = outer->list_id->tuple_cnt;
  inner_tuple_cnt = inner->list_id->tuple_cnt;

  /* HASHJOIN_STATUS_END must be checked first. */

  switch (manager->join_type)
    {
    case JOIN_INNER:
      status = (outer_tuple_cnt == 0 || inner_tuple_cnt == 0) ? HASHJOIN_STATUS_END : HASHJOIN_STATUS_TRY;
      break;

    case JOIN_LEFT:
      status =
	(outer_tuple_cnt == 0) ? HASHJOIN_STATUS_END : (inner_tuple_cnt ==
							0) ? HASHJOIN_STATUS_FILL_NULL_VALUES : HASHJOIN_STATUS_TRY;
      break;

    case JOIN_RIGHT:
      status =
	(inner_tuple_cnt == 0) ? HASHJOIN_STATUS_END : (outer_tuple_cnt ==
							0) ? HASHJOIN_STATUS_FILL_NULL_VALUES : HASHJOIN_STATUS_TRY;
      break;

    default:
      /* impossible case */
      assert_release_error (false);
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
int
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

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (fetch_info != NULL);
  assert (fetch_info->input != NULL);
  assert (tuple_record != NULL);
  assert (tuple_record->tpl != NULL);
  assert (key != NULL);
  assert (need_skip_next != NULL);
  assert (*need_skip_next == false);

  domains = fetch_info->input->domains;
  value_indexes = fetch_info->input->value_indexes;
  coerce_domains = fetch_info->coerce_domains;
  need_coerce_domains = fetch_info->need_coerce_domains;
  assert (domains != NULL);
  assert (value_indexes != NULL);
  assert (coerce_domains != NULL);

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

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

skip_next:
  *need_skip_next = true;

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_update_tuple_hash_key() -
 *   return: None
 *   thread_p(in): Thread entry.
 *   tuple_record(in): Tuple containing the hash key tuple value.
 *   hash_key(in): Hash key to store in the tuple value.
 */
void
hjoin_update_tuple_hash_key (THREAD_ENTRY * thread_p, QFILE_TUPLE_RECORD * tuple_record, UINT32 hash_key)
{
  QFILE_TUPLE tuple_value;

  assert (thread_p != NULL);
  assert (tuple_record != NULL);

  tuple_value = tuple_record->tpl + QFILE_TUPLE_LENGTH_SIZE;
  assert (QFILE_GET_TUPLE_VALUE_FLAG (tuple_value) == V_BOUND);
  assert (QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value) == MAX_ALIGNMENT);

  tuple_value += QFILE_TUPLE_VALUE_HEADER_SIZE;
  assert (OR_GET_INT (tuple_value) == -1);

  OR_PUT_INT (tuple_value, hash_key);
}

/*
 * hjoin_build() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
static int
hjoin_build (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  SCAN_CODE scan_code;
  bool need_skip_next = false;

  HASHJOIN_FETCH_INFO *build = NULL;
  QFILE_TUPLE tuple_value;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  build = context->build;
  assert (build != NULL);
  assert (build->list_scan_id.status != S_CLOSED);

  // *INDENT-OFF*
  build->tuple_record = { NULL, 0 };
  // *INDENT-ON*

  hash_scan = &context->hash_scan;

  hash_method = hash_scan->hash_list_scan_type;
  assert (hash_method != HASH_METH_NOT_USE);

  key = hash_scan->temp_key;
  assert (key != NULL);

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  if (manager->context_cnt == 0)	/* HASHJOIN_STATUS_SINGLE */
    {
      while ((scan_code =
	      qfile_scan_list_next (thread_p, &build->list_scan_id, &build->tuple_record, PEEK)) == S_SUCCESS)
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_BUILD_FETCH);
	  error =
	    hjoin_fetch_key (thread_p, build, &build->tuple_record, key, NULL /* compare_key */ , &need_skip_next);
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
	      error = hjoin_build_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_BUILD_INSERT);

	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}
	    }
	}			/* while (qfile_scan_list_next (list_scan_id)) */
    }
  else
    {
      while ((scan_code =
	      qfile_scan_list_next (thread_p, &build->list_scan_id, &build->tuple_record, PEEK)) == S_SUCCESS)
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_BUILD_FETCH);
	  tuple_value = build->tuple_record.tpl + QFILE_TUPLE_LENGTH_SIZE;
	  assert (QFILE_GET_TUPLE_VALUE_FLAG (tuple_value) == V_BOUND);
	  assert (QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value) == MAX_ALIGNMENT);

	  tuple_value += QFILE_TUPLE_VALUE_HEADER_LENGTH;
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_BUILD_FETCH);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_BUILD_HASH);
	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_BUILD_HASH);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_BUILD_INSERT);
	  error = hjoin_build_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_BUILD_INSERT);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	}			/* while (qfile_scan_list_next (list_scan_id)) */
    }

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->build, &start_stats);
      stats->build.read_rows = build->list_id->tuple_cnt;
      assert (stats->build.read_keys == 0);
      stats->build.qualified_rows = build->list_id->tuple_cnt;

#if HASHJOIN_COLLISION_RATE
      if (hash_scan->hash_list_scan_type == HASH_METH_IN_MEM || hash_scan->hash_list_scan_type == HASH_METH_HYBRID)
	{
	  stats->collision_rate = (double) hash_scan->memory.hash_table->ncollisions / build->list_id->tuple_cnt;
	}
      else
	{
	  stats->collision_rate = 0;
	}
#endif /* HASHJOIN_COLLISION_RATE */
    }

  /* qfile_close_scan is called by the caller. */

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
      goto error_exit;
    }

#if HASHJOIN_DUMP_HASH_TABLE
  if (build->list_id->tuple_cnt <= DUMP_HASH_TABLE_LIMIT)
    {
#if defined (SERVER_MODE)
      pthread_mutex_lock (&manager->dump_hash_table_mutex);
#endif /* defined (SERVER_MODE) */
      HJOIN_DUMP_HASH_TABLE (thread_p, hash_scan, build->list_id);
#if defined (SERVER_MODE)
      pthread_mutex_unlock (&manager->dump_hash_table_mutex);
#endif /* defined (SERVER_MODE) */
    }
#endif /* HASHJOIN_DUMP_HASH_TABLE */

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
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
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, (void *) hash_value) == NULL)
	{
	  qdata_free_hscan_value (thread_p, hash_value);

	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      break;

    case HASH_METH_HYBRID:
      assert (hash_scan->memory.hash_table != NULL);

      hash_value = qdata_alloc_hscan_value_OID (thread_p, list_scan_id);
      if (hash_value == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, (void *) hash_value) == NULL)
	{
	  qdata_free_hscan_value (thread_p, hash_value);

	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      break;

    case HASH_METH_HASH_FILE:
      assert (hash_scan->file.hash_table != NULL);

      SET_TFTID (tftid, list_scan_id->curr_vpid.volid, list_scan_id->curr_vpid.pageid, list_scan_id->curr_offset);
      if (fhs_insert (thread_p, hash_scan->file.hash_table, (void *) &hash_scan->curr_hash_key, &tftid) == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      break;

    case HASH_METH_NOT_USE:
      [[fallthrough]];
    default:
      /* impossible case */
      assert_release_error (false);
      return er_errid ();
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hjoin_probe() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 *   list_id(in/out): List identifier containing the join result.
 */
static int
hjoin_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context, QFILE_LIST_ID * list_id)
{
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool need_skip_next = false;

  HASHJOIN_FETCH_INFO *outer, *inner;
  HASHJOIN_FETCH_INFO *build = NULL, *probe = NULL;
  QFILE_TUPLE tuple_value;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key, *found_key;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (list_id != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  outer = &context->outer;
  inner = &context->inner;
  assert (outer->list_scan_id.status != S_CLOSED);
  assert (inner->list_scan_id.status != S_CLOSED);

  build = context->build;
  probe = context->probe;
  assert (build != NULL);
  assert (probe != NULL);

  // *INDENT-OFF*
  probe->tuple_record = { NULL, 0 };
  build->tuple_record = { NULL, 0 };
  // *INDENT-ON*

  hash_scan = &context->hash_scan;

  hash_method = hash_scan->hash_list_scan_type;
  assert (hash_method != HASH_METH_NOT_USE);

  key = hash_scan->temp_key;
  found_key = hash_scan->temp_new_key;
  assert (key != NULL);
  assert (found_key != NULL);

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &probe->list_scan_id, &probe->tuple_record, PEEK)) == S_SUCCESS)
    {
      HJOIN_PRINT_TUPLE (&probe->list_scan_id, probe->tuple_record.tpl, HASHJOIN_PRINT_READ_KEY);

      if (manager->context_cnt == 0)	/* HASHJOIN_STATUS_SINGLE */
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	  error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
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
	}
      else
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	  tuple_value = probe->tuple_record.tpl + QFILE_TUPLE_LENGTH_SIZE;
	  assert (QFILE_GET_TUPLE_VALUE_FLAG (tuple_value) == V_BOUND);
	  assert (QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value) == MAX_ALIGNMENT);

	  tuple_value += QFILE_TUPLE_VALUE_HEADER_LENGTH;
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	}

      do
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);
	  error = hjoin_probe_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (build->tuple_record.tpl == NULL)
	    {
	      break;		/* not found */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      stats->probe.read_keys++;	/* found */
	    }

	  if (manager->context_cnt != 0)	/* HASHJOIN_STATUS_PARTITION or HASHJOIN_STATUS_PARALLEL */
	    {
	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	      error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
				       &need_skip_next);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}
	      else if (need_skip_next)
		{
		  need_skip_next = false;	/* init */

		  /* impossible case */
		  assert_release_error (false);
		  error = er_errid ();
		  break;
		}
	      else
		{
		  /* fall through */
		}
	    }

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	  error = hjoin_fetch_key (thread_p, build, &build->tuple_record, found_key, key /* compare_key */ ,
				   &need_skip_next);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	  else if (need_skip_next)
	    {
	      HJOIN_PRINT_TUPLE (&build->list_scan_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

	      need_skip_next = false;	/* init */
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

	  HJOIN_PRINT_TUPLE (&build->list_scan_id, build->tuple_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, &outer->tuple_record, &inner->tuple_record,
					  manager->merge_info, &overflow_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	}
      while (true);

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
    }				/* while (qfile_scan_list_next (list_scan_id)) */

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->probe, &start_stats);
      stats->probe.read_rows = probe->list_id->tuple_cnt;
      stats->probe.qualified_rows = list_id->tuple_cnt;
    }

  /* qfile_close_scan is called by the caller. */

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
      goto error_exit;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  if (overflow_record.tpl != NULL)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  return error;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
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
 *   list_id(in/out): List identifier containing the join result.
 */
static int
hjoin_outer_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
		   QFILE_LIST_ID * list_id)
{
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool need_skip_next = false;
  bool any_record_added;

  HASHJOIN_FETCH_INFO *outer, *inner;
  HASHJOIN_FETCH_INFO *build = NULL, *probe = NULL;
  QFILE_TUPLE tuple_value;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key, *found_key;

  int error = NO_ERROR, save_error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (list_id != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  outer = &context->outer;
  inner = &context->inner;
  assert (outer->list_scan_id.status != S_CLOSED);
  assert (inner->list_scan_id.status != S_CLOSED);

  assert (outer->fill_record == NULL || outer->fill_record->tpl == NULL);

  build = context->build;
  probe = context->probe;
  assert (build != NULL);
  assert (probe != NULL);

  // *INDENT-OFF*
  probe->tuple_record = { NULL, 0 };
  build->tuple_record = { NULL, 0 };
  // *INDENT-ON*

  hash_scan = &context->hash_scan;

  hash_method = hash_scan->hash_list_scan_type;
  assert (hash_method != HASH_METH_NOT_USE);

  key = hash_scan->temp_key;
  found_key = hash_scan->temp_new_key;
  assert (key != NULL);
  assert (found_key != NULL);

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &probe->list_scan_id, &probe->tuple_record, PEEK)) == S_SUCCESS)
    {
      HJOIN_PRINT_TUPLE (&probe->list_scan_id, probe->tuple_record.tpl, HASHJOIN_PRINT_READ_KEY);

      if (manager->context_cnt == 0)	/* HASHJOIN_STATUS_SINGLE */
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	  error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
				   &need_skip_next);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	  else if (need_skip_next)
	    {
	      HJOIN_PRINT_TUPLE (&probe->list_scan_id, probe->tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	      error =
		hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record,
					      manager->merge_info, &overflow_record);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
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
	}
      else
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	  tuple_value = probe->tuple_record.tpl + QFILE_TUPLE_LENGTH_SIZE;
	  assert (QFILE_GET_TUPLE_VALUE_FLAG (tuple_value) == V_BOUND);
	  assert (QFILE_GET_TUPLE_VALUE_LENGTH (tuple_value) == MAX_ALIGNMENT);

	  tuple_value += QFILE_TUPLE_VALUE_HEADER_LENGTH;
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	}

      any_record_added = false;

      do
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);
	  error = hjoin_probe_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (build->tuple_record.tpl == NULL)
	    {
	      break;		/* not found */
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      stats->probe.read_keys++;	/* found */
	    }

	  if (manager->context_cnt != 0)	/* HASHJOIN_STATUS_PARTITION or HASHJOIN_STATUS_PARALLEL */
	    {
	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	      error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
				       &need_skip_next);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}
	      else if (need_skip_next)
		{
		  need_skip_next = false;	/* init */

		  /* impossible case */
		  assert_release_error (false);
		  save_error = er_errid ();

		  HJOIN_PRINT_TUPLE (&probe->list_scan_id, probe->tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

		  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
		  error =
		    hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record,
						  manager->merge_info, &overflow_record);
		  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

		  if (error != NO_ERROR)
		    {
		      break;	/* error_exit */
		    }

		  error = save_error;

		  any_record_added = true;	/* meaningless */
		  break;
		}
	      else
		{
		  /* fall through */
		}
	    }

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	  error = hjoin_fetch_key (thread_p, build, &build->tuple_record, found_key, key /* compare_key */ ,
				   &need_skip_next);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	  else if (need_skip_next)
	    {
	      HJOIN_PRINT_TUPLE (&build->list_scan_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

	      need_skip_next = false;	/* init */
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

	  if (context->during_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res = V_UNKNOWN;

	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	      do
		{
		  error =
		    fetch_val_list (thread_p, probe->regu_list_pred, context->val_descr, NULL, NULL,
				    probe->tuple_record.tpl, PEEK);
		  if (error != NO_ERROR)
		    {
		      break;	/* error_exit */
		    }

		  error =
		    fetch_val_list (thread_p, build->regu_list_pred, context->val_descr, NULL, NULL,
				    build->tuple_record.tpl, PEEK);
		  if (error != NO_ERROR)
		    {
		      break;	/* error_exit */
		    }

		  ev_res = eval_pred (thread_p, context->during_join_pred, context->val_descr, NULL);
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
		  HJOIN_PRINT_TUPLE (&build->list_scan_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
		  assert (need_skip_next == false);
		  continue;
		}
	    }			/* if (context->during_join_pred != NULL) */

	  HJOIN_PRINT_TUPLE (&build->list_scan_id, build->tuple_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, &outer->tuple_record, &inner->tuple_record,
					  manager->merge_info, &overflow_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  any_record_added = true;
	}
      while (true);

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}

      if (!any_record_added)
	{
	  HJOIN_PRINT_TUPLE (&probe->list_scan_id, probe->tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record,
					  manager->merge_info, &overflow_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	}			/* if (!any_record_added) */
    }				/* while (qfile_scan_list_next (probe_scan_id)) */

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->probe, &start_stats);
      stats->probe.read_rows = probe->list_id->tuple_cnt;
      stats->probe.qualified_rows = list_id->tuple_cnt;
    }

  /* qfile_close_scan is called by the caller. */

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
      goto error_exit;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  if (overflow_record.tpl != NULL)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  return error;

error_exit:
  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
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
	  tuple_record->tpl = hash_value->tuple;
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
	      assert_release_error (er_errid () != NO_ERROR);
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
	      assert_release_error (er_errid () != NO_ERROR);
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
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      break;			/* HASH_METH_HASH_FILE */

    case HASH_METH_NOT_USE:
      [[fallthrough]];
    default:
      /* impossible case */
      assert_release_error (false);
      return er_errid ();
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
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

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  assert_release_error (er_errid () != NO_ERROR);
  return er_errid ();
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
  int available_size, realloc_size, offset, value_size;
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
	  assert_release_error (false);
	  return er_errid ();
	}

      if (tuple_record != NULL)
	{
	  value_index = merge_info->ls_pos_list[pos_index];

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
      available_size = overflow_record->size - offset;

      if (value_size > available_size)
	{
	  realloc_size = CEIL_PTVDIV (overflow_record->size + (value_size - available_size), DB_PAGESIZE) * DB_PAGESIZE;

	  /* overflow_record is managed and cleaned up by the caller. */
	  error = qfile_reallocate_tuple (overflow_record, realloc_size);
	  if (error != NO_ERROR)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      memcpy (overflow_record->tpl + offset, tuple_value, value_size);
      offset += value_size;
    }				/* for (pos_index < merge_info->ls_pos_cnt) */

  QFILE_PUT_TUPLE_LENGTH (overflow_record->tpl, offset);

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hjoin_trace_start() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   start_stats(in/out): Profiling data captured at the start of the step.
 */
void
hjoin_trace_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats)
{
  assert (thread_p != NULL);
  assert (start_stats != NULL);

  tsc_getticks (&start_stats->tick);
  start_stats->fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
  start_stats->ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
}

/*
 * hjoin_trace_end() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   stats(in/out): Profiling data to accumulate.
 *   start_stats(in): Profiling data captured at the start of the step.
 */
void
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
  stats->fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - start_stats->fetches;
  stats->ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - start_stats->ioreads;
}

#if HASHJOIN_PROFILE_TIME
/*
 * hjoin_profile_start() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   start_stats(in/out): Profiling data captured at the start of the step.
 *   step(in): Hash join profiling step to measure.
 */
void
hjoin_profile_start (THREAD_ENTRY * thread_p, HASHJOIN_START_STATS * start_stats, HASHJOIN_PROFILE_STEP step)
{
  assert (thread_p != NULL);
  assert (start_stats != NULL);

  tsc_getticks (&start_stats->tick);
  start_stats->step = step;

  switch (step)
    {
    case HASHJOIN_PROFILE_BUILD_FETCH:
    case HASHJOIN_PROFILE_BUILD_HASH:
    case HASHJOIN_PROFILE_BUILD_INSERT:
    case HASHJOIN_PROFILE_PROBE_FETCH:
    case HASHJOIN_PROFILE_PROBE_HASH:
    case HASHJOIN_PROFILE_PROBE_SEARCH:
    case HASHJOIN_PROFILE_PROBE_MATCH:
    case HASHJOIN_PROFILE_PROBE_ADD:
      /* nothing to do */
      break;

    case HASHJOIN_PROFILE_MERGE:
      start_stats->fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      start_stats->ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      break;

    default:
      /* impossible case */
      assert (false);
      break;
    }				/* switch (step) */
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
void
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

    case HASHJOIN_PROFILE_MERGE:
      TSC_ADD_TIMEVAL (stats->merge.elapsed_time, tv_diff);
      stats->merge.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - start_stats->fetches;
      stats->merge.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - start_stats->ioreads;
      break;

    default:
      /* impossible case */
      assert (false);
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
void
hjoin_trace_merge_stats (HASHJOIN_STATS * stats, HASHJOIN_STATS * context_stats)
{
  assert (stats != NULL);
  assert (context_stats != NULL);

  if (stats == NULL || context_stats == NULL)
    {
      /* impossible case */
      assert (false);
      return;
    }

  TSC_ADD_TIMEVAL (stats->build.elapsed_time, context_stats->build.elapsed_time);
  stats->build.fetches += context_stats->build.fetches;
  stats->build.ioreads += context_stats->build.ioreads;
  stats->build.read_rows += context_stats->build.read_rows;
  stats->build.read_keys += context_stats->build.read_keys;
  stats->build.qualified_rows += context_stats->build.qualified_rows;

#if HASHJOIN_COLLISION_RATE
  stats->collision_rate = MAX (stats->collision_rate, context_stats->collision_rate);
#endif /* HASHJOIN_COLLISION_RATE */

#if HASHJOIN_PROFILE_TIME
  TSC_ADD_TIMEVAL (stats->profile.build.fetch, context_stats->profile.build.fetch);
  TSC_ADD_TIMEVAL (stats->profile.build.hash, context_stats->profile.build.hash);
  TSC_ADD_TIMEVAL (stats->profile.build.insert, context_stats->profile.build.insert);
#endif /* HASHJOIN_PROFILE_TIME */

  switch (context_stats->hash_method)
    {
    case HASH_METH_IN_MEM:
      stats->use_hash_memory = true;
      break;

    case HASH_METH_HYBRID:
      stats->use_hash_hybrid = true;
      break;

    case HASH_METH_HASH_FILE:
      stats->use_hash_file = true;
      break;

    case HASH_METH_NOT_USE:
      stats->use_hash_skip = true;
      break;

    default:
      /* impossible case */
      assert (false);
      return;
    }

  TSC_ADD_TIMEVAL (stats->probe.elapsed_time, context_stats->probe.elapsed_time);
  stats->probe.fetches += context_stats->probe.fetches;
  stats->probe.ioreads += context_stats->probe.ioreads;
  stats->probe.read_rows += context_stats->probe.read_rows;
  stats->probe.read_keys += context_stats->probe.read_keys;
  stats->probe.qualified_rows += context_stats->probe.qualified_rows;

#if HASHJOIN_PROFILE_TIME
  TSC_ADD_TIMEVAL (stats->profile.probe.fetch, context_stats->profile.probe.fetch);
  TSC_ADD_TIMEVAL (stats->profile.probe.hash, context_stats->profile.probe.hash);
  TSC_ADD_TIMEVAL (stats->profile.probe.search, context_stats->profile.probe.search);
  TSC_ADD_TIMEVAL (stats->profile.probe.match, context_stats->profile.probe.match);
  TSC_ADD_TIMEVAL (stats->profile.probe.add, context_stats->profile.probe.add);
#endif /* HASHJOIN_PROFILE_TIME */
}

/*
 * hjoin_trace_get_worker_stats() -
 *   return: Parallel worker stats at index.
 *   manager(in): Hash join manager containing shared state.
 *   index(in): Parallel worker index.
 */
UINT64 *
hjoin_trace_get_worker_stats (HASHJOIN_MANAGER * manager, int index)
{
  assert (manager != NULL);
  assert (manager->num_parallel_threads > 1);
  assert (index >= 0 && index < manager->num_parallel_threads);

  if (manager->px_worker_stats == NULL)
    {
      assert (false);
      return NULL;
    }

  /* immutable */
  static const int n_stat_values = perfmon_get_number_of_statistic_values ();

  return manager->px_worker_stats + index * n_stat_values;
}

/*
 * hjoin_trace_drain_worker_stats() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 */
void
hjoin_trace_drain_worker_stats (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  UINT64 *worker_stats;
  int task_cnt, task_index;
  int stats_cnt, stats_index;

  assert (thread_p->m_px_stats != NULL);
  assert (manager != NULL);
  assert (manager->num_parallel_threads > 1);
  assert (manager->px_worker_stats != NULL);

  /* immutable */
  static const int offsets[] = {
    pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset,
    pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset,
    pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset
  };

  task_cnt = manager->num_parallel_threads;
  stats_cnt = sizeof (offsets) / sizeof (offsets[0]);

  for (task_index = 0; task_index < task_cnt; task_index++)
    {
      worker_stats = hjoin_trace_get_worker_stats (manager, task_index);

      for (stats_index = 0; stats_index < stats_cnt; stats_index++)
	{
	  const int offset = offsets[stats_index];
	  thread_p->m_px_stats[offset] += worker_stats[offset];
	  worker_stats[offset] = 0;
	}
    }

  perfmon_merge_parallel_stats_to_tran_stats (thread_p);
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
      assert (false);
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

  qfile_print_tuple (&list_scan_id->list_id.type_list, tuple);
}
#endif /* !NDEBUG && HASHJOIN_DUMP_PROBE */
