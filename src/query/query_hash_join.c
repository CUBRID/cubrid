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
#include "qfile_tuple_layout.h"	/* qfile_slot_read_column_value, qfile_slot_get_column_data */
#include "query_hash_join_constants.hpp"	/* PARTITION_FILL_FACTOR, HASHJOIN_PART_MAX_CNT, HASHJOIN_SPLIT_MAX_LEVEL */
#include "query_hash_join_split.hpp"	/* hjoin_get_split_part_id, HASHJOIN_SPLIT_WRITER, ... */
#include "query_list.h"		/* JOIN_TYPE */
#include "query_manager.h"	/* QMGR_TEMP_FILE */
#include "system_parameter.h"	/* prm_get_bigint_value, PRM_ID_... */
#include "thread_entry.hpp"	/* THREAD_ENTRY */
#include "xasl.h"		/* XASL_NODE, HASHJOIN_PROC_NODE */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
static int hjoin_check_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
static UINT64 hjoin_calc_part_cnt (QFILE_LIST_ID * build_list_id, UINT64 mem_limit, double fill_factor);
static int hjoin_prepare_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
				    HASHJOIN_SPLIT_INFO * split_info);
static int hjoin_build_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
				   HASHJOIN_SPLIT_INFO * split_info);
static int hjoin_split_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			      HASHJOIN_INPUT_SPLIT_INFO * split_info, HASHJOIN_SPLIT_STAGING_BUFFER * staging_buffer,
			      HASH_SCAN_KEY * temp_key);
static int hjoin_resplit_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_STATUS status);

/* Hash Join Parallel */
static HASHJOIN_STATUS hjoin_try_parallel (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
					   HASHJOIN_CONTEXT * single_context);
static HASHJOIN_STATUS hjoin_try_parallel_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
						 HASHJOIN_CONTEXT * single_context);

/* Hash Join Worker Temp Files */
static int hjoin_init_worker_tfiles (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);
static void hjoin_clear_worker_tfiles (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);

/* Hash Join Split Info */
static int hjoin_init_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
				  UINT32 part_cnt, HASHJOIN_SPLIT_INFO * split_info);
static void hjoin_clear_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
				    HASHJOIN_SPLIT_INFO * split_info);

/* Hash Join Context */
static int hjoin_init_context (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
static void hjoin_clear_context (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context);
static void hjoin_destroy_qlist (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * context);
static int hjoin_init_part_contexts (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
				     UINT32 part_cnt, HASHJOIN_SPLIT_INFO * split_info,
				     HASHJOIN_CONTEXT ** part_contexts);
static void hjoin_clear_part_contexts (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * part_contexts, UINT32 part_cnt);
static int hjoin_init_exec_contexts (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);
static void hjoin_clear_exec_contexts (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager);

/* Hash List Scan */
static UINT64 hjoin_estimate_hash_table_size (QFILE_LIST_ID * list_id);
static UINT64 hjoin_calc_hash_table_size (QFILE_LIST_ID * list_id, HASH_METHOD method);

/* Hash Join Processing */
static HASHJOIN_STATUS hjoin_check_empty_inputs (HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);

/* Build Phase */
static int hjoin_build (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
static int hjoin_build_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan,
			    QFILE_LIST_SCAN_ID * list_scan_id, QFILE_TUPLE_RECORD * tuple_record);

/* Probe Phase */
static int hjoin_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context);
static int hjoin_inner_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			      QFILE_LIST_ID * list_id);
static int hjoin_outer_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			      QFILE_LIST_ID * list_id);

/*
 * Function Definitions
 */

/*
 * qexec_hash_join() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   xasl(in): XASL node for hash join execution.
 *   query_id(in): Query identifier.
 *   val_descr(in): Value descriptor for query evaluation.
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
  if (status == HASHJOIN_STATUS_END)
    {
      /* nothing to join: xasl->list_id stays as it is */
      goto cleanup;
    }

  /* the result list: the single context and the partitions append to it,
   * and the join tasks merge their lists into it */
  single_context->list_id =
    qfile_open_list (thread_p, &manager.type_list, NULL, manager.query_id, manager.qlist_flag, NULL);
  if (single_context->list_id == NULL)
    {
      goto error_exit;
    }

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

	  if (thread_is_on_trace (thread_p))
	    {
	      xasl->executed_parallelism = manager.num_parallel_threads;
	    }
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

	case HASHJOIN_STATUS_ERROR:
	  /* hjoin_try_partition always retries as HASHJOIN_STATUS_SINGLE;
	   * except for ER_INTERRUPTED, never returns HASHJOIN_STATUS_ERROR */
	  error = er_errid ();
	  assert_release_error (error == ER_INTERRUPTED);
	  goto error_exit;

	case HASHJOIN_STATUS_END:
	  /* hjoin_check_empty_inputs guarantees HASHJOIN_STATUS_END cannot occur here */
	default:
	  /* impossible case */
	  assert_release_error (false);
	  goto error_exit;
	}
      break;

    case HASHJOIN_STATUS_END:
      /* hjoin_check_empty_inputs returning HASHJOIN_STATUS_END has gone to cleanup already */
    case HASHJOIN_STATUS_ERROR:
    default:
      /* impossible case */
      assert_release_error (false);
      goto error_exit;
    }

  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Check if qfile_close_list was called */
  assert (single_context->list_id->last_pgptr == NULL);

  qfile_destroy_list (thread_p, xasl->list_id);	/* may be unnecessary */
  assert (!XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) || QFILE_LIST_IS_BACKWARD (single_context->list_id));
  qfile_copy_list_id (xasl->list_id, single_context->list_id, false, QFILE_MOVE_DEPENDENT);
  QFILE_FREE_AND_INIT_LIST_ID (single_context->list_id);

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  if (thread_is_on_trace (thread_p) && manager.stats_group != NULL)
    {
      manager.stats_group->status = single_context->status;
    }

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
  UINT32 exec_cnt, exec_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  HASHJOIN_STATS *stats = manager->single_context.stats;
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  exec_cnt = manager->exec_cnt;

  /* the contexts are executed one by one, so all of them append to the final result list */
  assert (manager->single_context.list_id != NULL);

  for (exec_index = 0; exec_index < exec_cnt; exec_index++)
    {
      current_context = manager->exec_contexts[exec_index];

      current_context->list_id = manager->single_context.list_id;	/* lend the result list */
      error = hjoin_execute (thread_p, manager, current_context);
      current_context->list_id = NULL;	/* take it back */

      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      if (thread_is_on_trace (thread_p))
	{
	  hjoin_trace_merge_stats (stats, current_context->stats, manager->single_context.status);
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
  assert (context->list_id != NULL);

  status = hjoin_check_empty_inputs (manager, context);

  /* In joins that NULL-fill one side,
   * tuples with NULL in any join column are placed in the last partition.
   * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition.
   */
  if (IS_NULL_FILL_JOIN_TYPE (manager->join_type)
      && context == &manager->single_context.child_contexts[manager->single_context.child_cnt - 1])
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
  assert (context->list_id->last_pgptr == NULL);

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
  QFILE_LIST_ID *list_id;
  INT64 start_tuple_cnt = 0;
  QFILE_TUPLE_RECORD overflow_record = QFILE_TUPLE_RECORD_INITIALIZER;
  SCAN_CODE scan_code;

  HASHJOIN_FETCH_INFO *outer, *inner;
  HASHJOIN_FETCH_INFO *build, *probe;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->list_id != NULL);

  HASHJOIN_STATS *stats = context->stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  list_id = context->list_id;
  error = qfile_reopen_list_as_append_mode (thread_p, list_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  start_tuple_cnt = list_id->tuple_cnt;

  outer = &context->outer;
  inner = &context->inner;

  switch (manager->join_type)
    {
    case JOIN_LEFT:
    case JOIN_ANTI:
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
  probe->tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  // *INDENT-ON*

  build->fill_record = NULL;
  probe->fill_record = &probe->tuple_record;

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

  if (context->after_join_pred != NULL)
    {
      /* No build match: null-pad the build side, then evaluate the after-join predicate. */
      for (REGU_VARIABLE_LIST regu_var_p = build->regu_list_pred; regu_var_p != NULL; regu_var_p = regu_var_p->next)
	{
	  pr_clear_value (regu_var_p->value.vfetch_to);
	}
      build->is_ready = true;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &probe->list_scan_id, &probe->tuple_record, PEEK)) == S_SUCCESS)
    {
      if (context->after_join_pred != NULL)
	{
	  probe->is_ready = false;

	  DB_LOGICAL ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);
	  if (ev_res == V_ERROR)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      error = er_errid ();
	      break;		/* error_exit */
	    }

	  if (ev_res != V_TRUE)
	    {
	      continue;
	    }
	}			/* if (context->after_join_pred != NULL) */

      /* NULL key on preserved side — emit fill_record (null on null-supplying side) */
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
      stats->probe.qualified_rows = list_id->tuple_cnt - start_tuple_cnt;
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

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  if (overflow_record.tpl != NULL)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  hjoin_destroy_qlist (thread_p, context);

  /* Check if qfile_close_list was called */
  assert (list_id->last_pgptr == NULL);

  return error;

error_exit:
  qfile_close_list (thread_p, list_id);

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
  HASHJOIN_FETCH_INFO *outer, *inner;
  HASHJOIN_FETCH_INFO *build = NULL, *probe = NULL;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->list_id != NULL);

  outer = &context->outer;
  inner = &context->inner;

  /* Prevent faults when qfile_close_scan is called */
  outer->list_scan_id.status = S_CLOSED;
  inner->list_scan_id.status = S_CLOSED;

  error = hjoin_init_context (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  build = context->build;
  probe = context->probe;
  assert (build != NULL);
  assert (probe != NULL);

  /* the hash scan lives in this function only: hjoin_scan_clear () at cleanup frees it on every path */
  error = hjoin_scan_init (thread_p, &context->hash_scan, manager->key_cnt, manager->mem_limit, build->list_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (thread_is_on_trace (thread_p))
    {
      context->stats->hash_method = context->hash_scan.hash_list_scan_type;
      context->stats->swap_join_inputs = (context->build == outer) ? true : false;
    }

#if defined (SERVER_MODE)
  if (context == &manager->single_context)
    {
      context->status = hjoin_try_parallel_probe (thread_p, manager, context);
      switch (context->status)
	{
	case HASHJOIN_STATUS_SINGLE:
	  if (thread_is_on_trace (thread_p))
	    {
	      assert (context->stats != NULL);
	      assert (context->stats->num_parallel_threads == 0);
	    }
	  break;

	case HASHJOIN_STATUS_PARALLEL_PROBE:
	  if (thread_is_on_trace (thread_p))
	    {
	      assert (context->stats != NULL);
	      context->stats->num_parallel_threads = manager->num_parallel_threads;
	    }
	  break;

	case HASHJOIN_STATUS_ERROR:
	  goto error_exit;

	default:
	  /* impossible case */
	  assert_release_error (false);
	  goto error_exit;
	}
    }
#endif /* defined (SERVER_MODE) */

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

  error = hjoin_probe (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  qfile_close_scan (thread_p, &build->list_scan_id);
  qfile_close_scan (thread_p, &probe->list_scan_id);

  hjoin_destroy_qlist (thread_p, context);

  hjoin_scan_clear (thread_p, &context->hash_scan);

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
 * hjoin_init_manager() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in/out): Hash join manager to initialize.
 *   xasl(in): XASL node for hash join execution.
 *   query_id(in): Query identifier.
 *   val_descr(in): Value descriptor for query evaluation.
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
  assert (xasl->spec_list == NULL);

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
  manager->after_join_pred = xasl->after_join_pred;
  manager->num_parallel_threads = xasl->parallelism;

  manager->query_id = query_id;
  manager->val_descr = val_descr;

  manager->mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);

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
  context->after_join_pred = manager->after_join_pred;
  context->val_descr = manager->val_descr;

  context->divisor = 1;		/* the mixed hash key itself */

  assert (context->status == HASHJOIN_STATUS_NONE);

  /* contexts */
  assert (context->child_contexts == NULL);
  assert (context->child_cnt == 0);

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
    (manager->qlist_merge_method == HASHJOIN_MERGE_CONNECT) ? QFILE_FLAG_ALL | QFILE_FLAG_NO_MEMBUF : QFILE_FLAG_ALL;
  manager->qlist_flag |= XASL_LIST_BACKWARD_FLAG (xasl);	/* result lists are promoted to xasl->list_id */

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
  HASHJOIN_CONTEXT *contexts = NULL, *current_context;
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

  /* the references before the contexts */
  hjoin_clear_exec_contexts (thread_p, manager);

  if (single_context->child_contexts != NULL)
    {
      contexts = single_context->child_contexts;
      context_cnt = single_context->child_cnt;
      assert (context_cnt > 1);

      for (context_index = 0; context_index < context_cnt; context_index++)
	{
	  current_context = &contexts[context_index];

	  /* A parallel probe context owns its result list, left here if the join failed before it was merged.
	   * A partition context owns its input lists only: its result list is lent for the join. */
#if defined (SERVER_MODE)
	  if (current_context->status == HASHJOIN_STATUS_PARALLEL_PROBE)
	    {
	      parallel_query::hash_join::clear_context (*thread_p, current_context);
	    }
	  else
#endif /* defined (SERVER_MODE) */
	    {
	      hjoin_clear_context (thread_p, current_context);
	    }
	}

      free_and_init (contexts);

      single_context->child_contexts = NULL;
      single_context->child_cnt = 0;
    }
  else
    {
      assert (single_context->child_cnt == 0);
    }

  /* after the contexts: their partition lists borrow the worker tfiles */
  hjoin_clear_worker_tfiles (thread_p, manager);

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
#else
  assert (manager->px_worker_manager == NULL);
  assert (manager->px_worker_stats == NULL);
  assert (thread_p->m_px_stats == NULL);
#endif /* defined (SERVER_MODE) */

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

  /* This code references tp_infer_common_domain but reduces unnecessary calls to tp_domain_new.
   * the key columns' domains are the plan's - the join's lists opened with them (qdata_get_valptr_type_list)
   * - so the common domain follows from resolutions made before any row. */
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
	      if (outer_precision == DB_DEFAULT_NUMERIC_PRECISION || inner_precision == DB_DEFAULT_NUMERIC_PRECISION)
		{
		  common_precision = DB_DEFAULT_NUMERIC_PRECISION;
		  common_scale = DB_DEFAULT_NUMERIC_SCALE;
		}
	      else
		{
		  common_scale = MAX (outer_scale, inner_scale);

		  outer_integral = outer_precision - outer_scale;
		  inner_integral = inner_precision - inner_scale;

		  common_precision = MAX (outer_integral, inner_integral) + common_scale;
		  if (common_precision > DB_MAX_FIXED_NUMERIC_PRECISION)
		    {
		      common_precision = DB_DEFAULT_NUMERIC_PRECISION;
		      common_scale = DB_DEFAULT_NUMERIC_SCALE;
		    }
		}
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

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (single_context != NULL);
  assert (single_context == &manager->single_context);

  error = hjoin_check_partition (thread_p, manager, single_context);
  if (error != NO_ERROR || single_context->child_cnt == 0)
    {
      /* an error is impossible: hjoin_check_empty_inputs has checked the join type and the inputs */
      assert (error == NO_ERROR);

      return HASHJOIN_STATUS_SINGLE;
    }

  status = HASHJOIN_STATUS_PARTITION;

  error = hjoin_prepare_partition (thread_p, manager, single_context, &split_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

#if defined (SERVER_MODE)
  status = hjoin_try_parallel (thread_p, manager, single_context);
  single_context->status = status;
  if (status == HASHJOIN_STATUS_ERROR)
    {
      goto error_exit;
    }
#endif /* defined (SERVER_MODE) */

  /* after hjoin_try_parallel: one worker tfile is created for each split worker it reserved */
  error = hjoin_init_worker_tfiles (thread_p, manager);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  switch (status)
    {
    case HASHJOIN_STATUS_PARTITION:
      if (thread_is_on_trace (thread_p))
	{
	  assert (single_context->stats != NULL);
	  assert (single_context->stats->num_parallel_threads == 0);
	}

      error = hjoin_build_partitions (thread_p, manager, single_context, &split_info);
      break;

#if defined (SERVER_MODE)
    case HASHJOIN_STATUS_PARALLEL:
      if (thread_is_on_trace (thread_p))
	{
	  assert (single_context->stats != NULL);
	  single_context->stats->num_parallel_threads = manager->num_parallel_threads;
	}

      // *INDENT-OFF*
      error = parallel_query::hash_join::build_partitions (*thread_p, manager, single_context, &split_info);
      // *INDENT-ON*
      break;
#endif /* defined (SERVER_MODE) */

    case HASHJOIN_STATUS_ERROR:
      /* hjoin_try_parallel returning HASHJOIN_STATUS_ERROR has gone to error_exit already */
    default:
      /* impossible case */
      assert_release_error (false);
      goto error_exit;
    }

  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = hjoin_resplit_partitions (thread_p, manager, status);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = hjoin_init_exec_contexts (thread_p, manager);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* call hjoin_destroy_qlist after all the splits are done,
   * since HASHJOIN_STATUS_SINGLE may retry on error*/
  hjoin_destroy_qlist (thread_p, single_context);

  assert (status == HASHJOIN_STATUS_PARTITION || status == HASHJOIN_STATUS_PARALLEL);

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  hjoin_clear_split_info (thread_p, manager, &split_info);

  return status;

error_exit:
#if defined (SERVER_MODE)
  if (manager->px_worker_manager != NULL)
    {
      manager->px_worker_manager->release_workers ();
      manager->px_worker_manager = NULL;
    }

  /* reserved with the workers by hjoin_try_parallel; a parallel probe of the single join reserves its own */
  if (manager->px_worker_stats != NULL)
    {
      db_private_free_and_init (thread_p, manager->px_worker_stats);
    }
#else
  assert (manager->px_worker_manager == NULL);
#endif /* defined (SERVER_MODE) */

  hjoin_clear_split_info (thread_p, manager, &split_info);

  /* the references before the contexts */
  hjoin_clear_exec_contexts (thread_p, manager);

  if (single_context->child_contexts != NULL)
    {
      assert (single_context->child_cnt > 1);

      hjoin_clear_part_contexts (thread_p, single_context->child_contexts, single_context->child_cnt);
      single_context->child_contexts = NULL;
    }

  /* also when the partitions failed before the contexts were made: a parallel probe expects no partition count */
  single_context->child_cnt = 0;

  /* the single join that follows needs the temporary space the worker tfiles hold */
  hjoin_clear_worker_tfiles (thread_p, manager);

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
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in/out): The single context or a partition; part_cnt is set to the number of partitions, or 0.
 */
static int
hjoin_check_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *build_list_id;
  UINT64 part_cnt;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->level < HASHJOIN_SPLIT_MAX_LEVEL);
  assert (context->child_cnt == 0);
  assert (context->child_contexts == NULL);

  if (manager->mem_limit == 0)
    {
      /* 0 turns hash list scans off and no hash join is planned under it, but a statement prepared under another
       * value still runs its hash join plan: do not partition, and hjoin_scan_init chooses HASH_FILE */
      assert (context == &manager->single_context);
      return NO_ERROR;
    }

  /* only a context joined by hash has a hash table to size (hjoin_execute takes the same status) */
  if (hjoin_check_empty_inputs (manager, context) != HASHJOIN_STATUS_TRY)
    {
      return NO_ERROR;
    }

  /* only the first split makes the partition of NULL join keys: the single context (level 0) has no partitions yet,
   * and a partition split again (level 2) gets only the tuples with hash keys */
  if (context->level == 1)
    {
      HASHJOIN_CONTEXT *last_context = &manager->single_context.child_contexts[manager->single_context.child_cnt - 1];

      /* In joins that NULL-fill one side,
       * tuples with NULL in any join column are placed in the last partition of the single context.
       * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition, so it is not split again.
       */
      if (IS_NULL_FILL_JOIN_TYPE (manager->join_type) && context == last_context)
	{
	  return NO_ERROR;
	}
    }

  /* count by the build input hjoin_execute will choose: the smaller input of an inner join, or the input the join
   * type fixes, which may be the larger one */
  error = hjoin_init_context (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      return error;
    }

  build_list_id = context->build->list_id;

  part_cnt = hjoin_calc_part_cnt (build_list_id, manager->mem_limit, PARTITION_FILL_FACTOR);
  if (part_cnt == 0)
    {
      return NO_ERROR;
    }

  /* only the first split makes the partition of NULL join keys: a partition split again (level 1) has only the tuples
   * with hash keys */
  if (context->level == 0)
    {
      if (IS_NULL_FILL_JOIN_TYPE (manager->join_type))
	{
	  /* the partition of NULL join keys */
	  part_cnt += 1;
	}
    }

  context->child_cnt = (UINT32) MIN (part_cnt, HASHJOIN_PART_MAX_CNT);

  return NO_ERROR;
}

/*
 * hjoin_calc_part_cnt() - number of partitions a context is split into, by the size hjoin_scan_init gives its hash
 *                         table
 *   return: 0 if its hash table fits in max_hash_list_scan_size as it is; otherwise the number of partitions, neither
 *           limited by HASHJOIN_PART_MAX_CNT nor counting the partition of NULL join keys: the caller adds both.
 *   build_list_id(in): Build input.
 *   mem_limit(in): max_hash_list_scan_size.
 *   fill_factor(in): Part of max_hash_list_scan_size a partition aims at.
 */
static UINT64
hjoin_calc_part_cnt (QFILE_LIST_ID * build_list_id, UINT64 mem_limit, double fill_factor)
{
  UINT64 in_mem_size, hybrid_size;

  assert (build_list_id != NULL);
  assert (mem_limit > 0);

  if (build_list_id->tuple_cnt > INT_MAX)
    {
      /* it never fits (hjoin_scan_init chooses HASH_FILE), and its exact size takes an int row count */
      hybrid_size = hjoin_estimate_hash_table_size (build_list_id);
    }
  else
    {
      in_mem_size = hjoin_calc_hash_table_size (build_list_id, HASH_METH_IN_MEM);
      hybrid_size = hjoin_calc_hash_table_size (build_list_id, HASH_METH_HYBRID);
      if (in_mem_size <= mem_limit || hybrid_size <= mem_limit)
	{
	  return 0;
	}
    }

  /* as many partitions as its hash table needs, with room */
  return CEIL_PTVDIV (hybrid_size, mem_limit * fill_factor);
}

/*
 * hjoin_prepare_partition() - make the contexts of the partitions of a context and their statistics
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in/out): The single context or a partition; child_cnt is set (hjoin_check_partition).
 *   split_info(out): The partition lists of each input, referred to by the split.
 */
static int
hjoin_prepare_partition (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			 HASHJOIN_SPLIT_INFO * split_info)
{
  HASHJOIN_CONTEXT *contexts = NULL;
  HASHJOIN_STATS *context_stats = NULL;

  UINT32 part_cnt, part_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->child_contexts == NULL);
  assert (split_info != NULL);

  part_cnt = context->child_cnt;
  assert (part_cnt > 1);

  error = hjoin_init_part_contexts (thread_p, manager, context, part_cnt, split_info, &contexts);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  context->child_contexts = contexts;

  if (thread_is_on_trace (thread_p))
    {
      /* calloc, as the contexts are (hjoin_clear_context and hjoin_clear_manager free them) */
      context_stats = (HASHJOIN_STATS *) calloc (part_cnt, sizeof (HASHJOIN_STATS));
      if (context_stats == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, part_cnt * sizeof (HASHJOIN_STATS));
	  goto error_exit;
	}

      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  contexts[part_index].stats = &context_stats[part_index];
	}

      if (context->level == 0)
	{
	  /* the statistics of the partitions of the single context are dumped after the join (qdump_print_stats) */
	  assert (manager->stats_group != NULL);
	  manager->stats_group->context_stats = context_stats;
	  manager->stats_group->context_cnt = part_cnt;
	}
      else
	{
	  /* those of a partition split again are merged into it once they are executed */
	  context->child_stats = context_stats;
	}
    }
  else
    {
      assert (manager->stats_group == NULL);
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  /* nothing fails after context_stats is allocated, so it is never freed here */
  assert (context_stats == NULL);

  context->child_contexts = NULL;
  context->child_cnt = 0;

  hjoin_clear_split_info (thread_p, manager, split_info);

  if (contexts != NULL)
    {
      hjoin_clear_part_contexts (thread_p, contexts, part_cnt);
    }

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
 *   context(in): The single context or a partition, split into its child contexts.
 *   split_info(in): Split information.
 */
static int
hjoin_build_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			HASHJOIN_SPLIT_INFO * split_info)
{
  HASHJOIN_INPUT_SPLIT_INFO *input;
  HASH_SCAN_KEY *temp_key = NULL;
  HASHJOIN_SPLIT_STAGING_BUFFER staging_buffer;
  int side;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (split_info != NULL);

  /* Prevent faults when hjoin_clear_split_staging_buffer is called */
  memset (&staging_buffer, 0, sizeof (HASHJOIN_SPLIT_STAGING_BUFFER));

  HASHJOIN_STATS *stats = manager->single_context.stats;
  HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_start (thread_p, &start_stats);
    }

  temp_key = qdata_alloc_hscan_key (thread_p, manager->key_cnt, true);
  if (temp_key == NULL)
    {
      goto error_exit;
    }

  error = hjoin_init_split_staging_buffer (thread_p, &staging_buffer, context->child_cnt);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  for (side = 0; side < 2; side++)
    {
      input = (side == 0) ? &split_info->outer : &split_info->inner;

      error = hjoin_split_qlist (thread_p, manager, context, input, &staging_buffer, temp_key);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
  if (thread_is_on_trace (thread_p))
    {
      hjoin_trace_end (thread_p, &stats->split, &start_stats);
    }

  hjoin_clear_split_staging_buffer (thread_p, &staging_buffer);

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
 *   context(in): The single context or a partition, split into its child contexts.
 *   split_info(in): Split information.
 *   staging_buffer(in/out): Staged last pages of the partition lists, kept from one input to the next.
 *   key(in/out): Space for reading join column values.
 */
static int
hjoin_split_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
		   HASHJOIN_INPUT_SPLIT_INFO * split_info, HASHJOIN_SPLIT_STAGING_BUFFER * staging_buffer,
		   HASH_SCAN_KEY * temp_key)
{
  QFILE_LIST_ID *list_id;
  QFILE_LIST_ID **part_list_id;
  QFILE_LIST_SCAN_ID list_scan_id;
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  SCAN_CODE scan_code;
  HASHJOIN_SPLIT_LANDING_BUFFER landing_buffer;
  HASHJOIN_SPLIT_WRITER split_writer;

  UINT32 part_cnt, part_id;
  bool is_skipped = false;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (split_info != NULL);
  assert (split_info->fetch_info != NULL);
  assert (staging_buffer != NULL);
  assert (temp_key != NULL);

  list_id = split_info->fetch_info->list_id;
  part_list_id = split_info->part_list_id;
  part_cnt = context->child_cnt;
  assert (list_id != NULL);
  assert (part_list_id != NULL);
  assert (part_cnt > 1);

  /* Prevent faults when qfile_close_scan and hjoin_clear_split_landing_buffer are called */
  list_scan_id.status = S_CLOSED;
  memset (&landing_buffer, 0, sizeof (HASHJOIN_SPLIT_LANDING_BUFFER));

  error = hjoin_init_split_landing_buffer (thread_p, &landing_buffer, part_cnt);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  hjoin_init_split_writer (&split_writer, &landing_buffer, staging_buffer, NULL, manager->worker_tfiles[0],
			   part_list_id);

  error = qfile_open_list_scan (list_id, &list_scan_id);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      if (context->level == 0)
	{
	  error = hjoin_get_split_part_id (thread_p, manager, split_info->fetch_info, &tuple_record, temp_key,
					   part_cnt, &part_id, &is_skipped);
	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	  else if (is_skipped)
	    {
	      /* next tuple */
	      continue;
	    }
	}
      else
	{
	  /* a partition split again: by the hash key stored in the tuple at the first split */
	  part_id = hjoin_get_resplit_part_id (&tuple_record, context->divisor, part_cnt);
	}

      error = hjoin_add_to_split_writer (thread_p, &split_writer, part_id, tuple_record.tpl);
      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}
    }				/* while (qfile_scan_list_next (list_scan_id)) */

  /* After qfile_open_list_scan, if an error occurs,
   * ensure qfile_close_scan runs here
   * before jumping to error_exit. */
  qfile_close_scan (thread_p, &list_scan_id);

  if (scan_code != S_ERROR && error == NO_ERROR)
    {
      error = hjoin_flush_split_writer (thread_p, &split_writer);
    }
  hjoin_clear_split_landing_buffer (thread_p, &landing_buffer);

  if (scan_code != S_ERROR && error == NO_ERROR)
    {
      error = hjoin_flush_split_staging_buffer (thread_p, staging_buffer, part_list_id, manager->worker_tfiles[0]);
    }

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto error_exit;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  hjoin_clear_split_landing_buffer (thread_p, &landing_buffer);

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_resplit_partitions() - split again the partitions whose hash tables do not fit (hjoin_check_partition), before
 *                              the join; each owns its partitions (HASHJOIN_CONTEXT)
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   status(in): HASHJOIN_STATUS_PARTITION or HASHJOIN_STATUS_PARALLEL: the way the partitions were split.
 */
static int
hjoin_resplit_partitions (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_STATUS status)
{
  HASHJOIN_CONTEXT *single_context, *current_context;
  HASHJOIN_SPLIT_INFO split_info;
  UINT32 context_index;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  single_context = &manager->single_context;

  for (context_index = 0; context_index < single_context->child_cnt; context_index++)
    {
      current_context = &single_context->child_contexts[context_index];

      error = hjoin_check_partition (thread_p, manager, current_context);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      if (current_context->child_cnt == 0)
	{
	  continue;
	}

      error = hjoin_prepare_partition (thread_p, manager, current_context, &split_info);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      switch (status)
	{
	case HASHJOIN_STATUS_PARTITION:
	  error = hjoin_build_partitions (thread_p, manager, current_context, &split_info);
	  break;

#if defined (SERVER_MODE)
	case HASHJOIN_STATUS_PARALLEL:
	  // *INDENT-OFF*
	  error = parallel_query::hash_join::build_partitions (*thread_p, manager, current_context, &split_info);
	  // *INDENT-ON*
	  break;
#endif /* defined (SERVER_MODE) */

	default:
	  /* impossible case */
	  assert_release_error (false);
	  error = er_errid ();
	  break;
	}

      /* the references to the partition lists; the partitions that own them stay with the context, even on error */
      hjoin_clear_split_info (thread_p, manager, &split_info);

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
 * hjoin_merge_qlist() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 */
int
hjoin_merge_qlist (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  assert (manager != NULL);

  assert (context != NULL && context != &manager->single_context);

  return hjoin_merge_qlist_into (thread_p, manager, &manager->single_context.list_id, &context->list_id);
}

/*
 * hjoin_merge_qlist_into() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   dest_list_id(in/out): Result list that receives the result; NULL if there is none yet.
 *   list_id(in/out): Result list to merge; NULL after the merge.
 */
int
hjoin_merge_qlist_into (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, QFILE_LIST_ID ** dest_list_id,
			QFILE_LIST_ID ** list_id)
{
  QFILE_LIST_ID *new_list_id = NULL;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (dest_list_id != NULL && list_id != NULL);

  /* Check if qfile_close_list was called */
  assert (*list_id != NULL);
  assert ((*list_id)->last_pgptr == NULL);

  if (*dest_list_id == NULL)
    {
      *dest_list_id = *list_id;
      *list_id = NULL;
      return NO_ERROR;
    }
  else if ((*dest_list_id)->tuple_cnt == 0)
    {
      qfile_destroy_list (thread_p, *dest_list_id);
      QFILE_FREE_AND_INIT_LIST_ID (*dest_list_id);

      *dest_list_id = *list_id;
      *list_id = NULL;
      return NO_ERROR;
    }
  else if ((*list_id)->tuple_cnt == 0)
    {
      qfile_destroy_list (thread_p, *list_id);
      QFILE_FREE_AND_INIT_LIST_ID (*list_id);
      return NO_ERROR;
    }

  /* Check if qfile_close_list was called */
  assert ((*dest_list_id)->last_pgptr == NULL);

  switch (manager->qlist_merge_method)
    {
    case HASHJOIN_MERGE_COMBINE:
      {
	new_list_id = qfile_combine_two_list (thread_p, *dest_list_id, *list_id, QFILE_FLAG_ALL | QFILE_FLAG_UNION);
	if (new_list_id == NULL)
	  {
	    goto error_exit;
	  }

	qfile_destroy_list (thread_p, *dest_list_id);
	QFILE_FREE_AND_INIT_LIST_ID (*dest_list_id);

	qfile_destroy_list (thread_p, *list_id);
	QFILE_FREE_AND_INIT_LIST_ID (*list_id);

	*dest_list_id = new_list_id;
	new_list_id = NULL;

	break;
      }

    case HASHJOIN_MERGE_APPEND:
      {
	error = qfile_append_list (thread_p, *dest_list_id, *list_id);
	if (error != NO_ERROR)
	  {
	    goto error_exit;
	  }

	qfile_destroy_list (thread_p, *list_id);
	QFILE_FREE_AND_INIT_LIST_ID (*list_id);

	break;
      }

    case HASHJOIN_MERGE_CONNECT:
      {
	error = qfile_connect_list (thread_p, *dest_list_id, *list_id);
	if (error != NO_ERROR)
	  {
	    goto error_exit;
	  }

	/* Do not call QFILE_FREE_AND_INIT_LIST_ID; it must be called through *dest_list_id. */
	*list_id = NULL;
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
  INT64 max_page_cnt;

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
  max_page_cnt =
    (outer_list_id->page_cnt > inner_list_id->page_cnt) ? outer_list_id->page_cnt : inner_list_id->page_cnt;
  assert (max_page_cnt >= 0);

  UINT32 degree = parallel_query::compute_parallel_degree (parallel_query::parallel_type::HASH_JOIN, max_page_cnt,
							   manager->num_parallel_threads);
  if (degree < 2)
    {
      /* try single-thread hash join */
      assert (degree == 0);
      manager->num_parallel_threads = 0;
      assert (manager->px_worker_manager == NULL);
      return HASHJOIN_STATUS_PARTITION;
    }

  degree = MIN (degree, manager->single_context.child_cnt /* part_cnt */ );

  px_worker_manager = parallel_query::worker_manager::try_reserve_workers (degree);
  if (px_worker_manager == NULL)
    {
      goto error_exit;
    }

  /* update to actual reserved workers */
  degree = px_worker_manager->get_reserved_workers ();
  manager->num_parallel_threads = degree;

  /* one statistics slice per worker whenever anyone reads them, trace or not; drained after each phase */
  if (perfmon_parallel_stats_needed (thread_p))
    {
      px_worker_stats = (UINT64 *) db_private_alloc (thread_p, degree * stats_size);
      if (px_worker_stats == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  goto error_exit;
	}
      memset (px_worker_stats, 0, degree * stats_size);

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
 * hjoin_try_parallel_probe() -
 *   return: One of the following HASHJOIN_STATUS values:
 *           - HASHJOIN_STATUS_SINGLE: Parallel probe is not applied or falls back on error.
 *           - HASHJOIN_STATUS_PARALLEL_PROBE: Parallel probe is applied.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   single_context(in): Hash join context for single-threaded execution.
 */
static HASHJOIN_STATUS
hjoin_try_parallel_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * single_context)
{
  parallel_query::worker_manager * px_worker_manager = NULL;
  UINT64 *px_worker_stats = NULL;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (manager->single_context.child_cnt == 0);
  assert (single_context != NULL);
  assert (single_context == &manager->single_context);

#if !defined (SERVER_MODE)
  assert (false);
#endif /* defined (SERVER_MODE) */

  /* immutable */
  static const size_t stats_size = perfmon_get_number_of_statistic_values () * sizeof (UINT64);

  UINT32 degree = parallel_query::compute_parallel_degree (parallel_query::parallel_type::HASH_JOIN,
							   single_context->probe->list_id->page_cnt,
							   manager->num_parallel_threads);
  if (degree < 2)
    {
      /* try single-thread hash join */
      assert (degree == 0);
      manager->num_parallel_threads = 0;
      assert (manager->px_worker_manager == NULL);
      return HASHJOIN_STATUS_SINGLE;
    }

  px_worker_manager = parallel_query::worker_manager::try_reserve_workers (degree);
  if (px_worker_manager == NULL)
    {
      goto error_exit;
    }

  /* update to actual reserved workers */
  degree = px_worker_manager->get_reserved_workers ();
  manager->num_parallel_threads = degree;

  /* one statistics slice per worker whenever anyone reads them, trace or not; drained after each phase */
  if (perfmon_parallel_stats_needed (thread_p))
    {
      px_worker_stats = (UINT64 *) db_private_alloc (thread_p, degree * stats_size);
      if (px_worker_stats == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  goto error_exit;
	}
      memset (px_worker_stats, 0, degree * stats_size);

      manager->px_worker_stats = px_worker_stats;
    }
  else
    {
      assert (manager->px_worker_stats == NULL);
    }

  manager->px_worker_manager = px_worker_manager;

  return HASHJOIN_STATUS_PARALLEL_PROBE;

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
      /* fallback to HASHJOIN_STATUS_SINGLE */
      er_clear ();
      return HASHJOIN_STATUS_SINGLE;
    }
}

/*
 * hjoin_init_worker_tfiles() - create the temporary files the partition lists are written to
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager; px_worker_manager is set if the split is parallel.
 */
static int
hjoin_init_worker_tfiles (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  int worker_cnt, worker_index;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (manager->worker_tfiles == NULL);

  /* one per reserved split worker, one for a serial split */
  worker_cnt = (manager->px_worker_manager != NULL) ? manager->num_parallel_threads : 1;
  assert (worker_cnt > 0);

  manager->worker_tfiles = (QMGR_TEMP_FILE **) db_private_alloc (thread_p, worker_cnt * sizeof (QMGR_TEMP_FILE *));
  if (manager->worker_tfiles == NULL)
    {
      goto error_exit;
    }

  for (worker_index = 0; worker_index < worker_cnt; worker_index++)
    {
      manager->worker_tfiles[worker_index] =
	qmgr_create_new_temp_file (thread_p, manager->query_id, TEMP_FILE_MEMBUF_NONE);
      if (manager->worker_tfiles[worker_index] == NULL)
	{
	  goto error_exit;
	}
      manager->worker_tfile_cnt = worker_index + 1;
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  hjoin_clear_worker_tfiles (thread_p, manager);

  assert_release_error (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * hjoin_clear_worker_tfiles() - free the worker tfiles, after the partition lists that borrow them
 *   return: None.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager.
 */
static void
hjoin_clear_worker_tfiles (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  int worker_index;

  assert (thread_p != NULL);
  assert (manager != NULL);

  /* the contexts that own the partition lists are gone */
  assert (manager->single_context.child_contexts == NULL);

  if (manager->worker_tfiles == NULL)
    {
      assert (manager->worker_tfile_cnt == 0);
      return;
    }

  for (worker_index = 0; worker_index < manager->worker_tfile_cnt; worker_index++)
    {
      (void) qmgr_free_list_temp_file (thread_p, manager->query_id, manager->worker_tfiles[worker_index]);
    }

  db_private_free_and_init (thread_p, manager->worker_tfiles);
  manager->worker_tfile_cnt = 0;
}

/*
 * hjoin_init_split_info() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Context whose inputs are split.
 *   part_cnt(in): Number of partitions.
 *   split_info(in/out): Split information.
 */
static int
hjoin_init_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context, UINT32 part_cnt,
		       HASHJOIN_SPLIT_INFO * split_info)
{
  HASHJOIN_INPUT_SPLIT_INFO *input;
  int side;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (part_cnt > 1);
  assert (split_info != NULL);

  /* Prevent faults when hjoin_clear_split_info is called */
  memset (split_info, 0, sizeof (HASHJOIN_SPLIT_INFO));

  for (side = 0; side < 2; side++)
    {
      input = (side == 0) ? &split_info->outer : &split_info->inner;
      input->fetch_info = (side == 0) ? &context->outer : &context->inner;
      input->part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, part_cnt * sizeof (QFILE_LIST_ID *));
      if (input->part_list_id == NULL)
	{
	  goto error_exit;
	}
      memset (input->part_list_id, 0, part_cnt * sizeof (QFILE_LIST_ID *));
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  hjoin_clear_split_info (thread_p, manager, split_info);

  assert_release_error (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * hjoin_clear_split_info() -
 *   return: None.
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   split_info(in): Split information.
 */
static void
hjoin_clear_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_SPLIT_INFO * split_info)
{
  HASHJOIN_INPUT_SPLIT_INFO *input;
  int side;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (split_info != NULL);

  for (side = 0; side < 2; side++)
    {
      input = (side == 0) ? &split_info->outer : &split_info->inner;
      if (input->part_list_id != NULL)
	{
	  db_private_free_and_init (thread_p, input->part_list_id);
	}
    }
}

/*
 * hjoin_init_shared_split_info() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   part_cnt(in): Number of partitions the split writes to.
 *   shared_info(in/out): Shared split information.
 */
int
hjoin_init_shared_split_info (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, UINT32 part_cnt,
			      HASHJOIN_SHARED_SPLIT_INFO * shared_info)
{
  UINT32 part_index;
  UINT32 init_cnt = 0;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (part_cnt > 1);
  assert (shared_info != NULL);

  shared_info->part_cnt = part_cnt;

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
  qfile_close_list_sector_scan (thread_p, &shared_info->sector_scan);

  part_cnt = shared_info->part_cnt;
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

    case JOIN_SEMI:
      context->build = inner;
      context->probe = outer;
      break;

    case JOIN_LEFT:
    case JOIN_ANTI:
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

  if (context->child_contexts != NULL)
    {
      hjoin_clear_part_contexts (thread_p, context->child_contexts, context->child_cnt);
      context->child_contexts = NULL;
    }

  if (context->child_stats != NULL)
    {
      free_and_init (context->child_stats);
    }

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
 * hjoin_init_part_contexts() - make the contexts of the partitions of a context, each over its own partition lists
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Context whose inputs are split; the single context, or a context that is split again.
 *   part_cnt(in): Number of partitions.
 *   split_info(out): The partition lists of each input, referred to by the split.
 *   part_contexts(out): Contexts of the partitions; each owns its partition lists (hjoin_clear_part_contexts).
 */
static int
hjoin_init_part_contexts (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
			  UINT32 part_cnt, HASHJOIN_SPLIT_INFO * split_info, HASHJOIN_CONTEXT ** part_contexts)
{
  HASHJOIN_INPUT_SPLIT_INFO *input;
  HASHJOIN_FETCH_INFO *fetch_info, *part_fetch_info;
  QFILE_LIST_ID *list_id, *part_list_id;

  HASHJOIN_CONTEXT *contexts = NULL, *current_context;

  UINT32 hash_part_cnt, part_index;
  int side;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->outer.list_id != NULL);
  assert (context->inner.list_id != NULL);
  assert (part_cnt > 1);
  assert (split_info != NULL);
  assert (part_contexts != NULL);

  /* the partition of NULL join keys, last among the partitions of the single context, takes no hash value */
  hash_part_cnt = part_cnt - ((context->level == 0 && IS_NULL_FILL_JOIN_TYPE (manager->join_type)) ? 1 : 0);

  error = hjoin_init_split_info (thread_p, manager, context, part_cnt, split_info);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* calloc, not db_private_alloc: the partitions of a context split again in a worker are freed by the main thread */
  contexts = (HASHJOIN_CONTEXT *) calloc (part_cnt, sizeof (HASHJOIN_CONTEXT));
  if (contexts == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, part_cnt * sizeof (HASHJOIN_CONTEXT));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_exit;
    }

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      current_context = &contexts[part_index];

      for (side = 0; side < 2; side++)
	{
	  input = (side == 0) ? &split_info->outer : &split_info->inner;

	  /* context */
	  fetch_info = (side == 0) ? &context->outer : &context->inner;
	  list_id = fetch_info->list_id;

	  /* current_context */
	  part_fetch_info = (side == 0) ? &current_context->outer : &current_context->inner;

	  part_list_id =
	    qfile_open_list (thread_p, &list_id->type_list, NULL, list_id->query_id,
			     QFILE_FLAG_ALL | QFILE_FLAG_BORROWED_TFILE | QFILE_LIST_BACKWARD_FLAG (list_id), NULL);
	  if (part_list_id == NULL)
	    {
	      goto error_exit;
	    }
	  assert (part_list_id->tfile_vfid == NULL);
	  input->part_list_id[part_index] = part_list_id;

	  part_fetch_info->list_id = part_list_id;
	  part_fetch_info->input = fetch_info->input;
	  part_fetch_info->coerce_domains = fetch_info->coerce_domains;
	  part_fetch_info->need_coerce_domains = fetch_info->need_coerce_domains;
	  part_fetch_info->regu_list_pred = fetch_info->regu_list_pred;
	}

      current_context->during_join_pred = context->during_join_pred;
      current_context->after_join_pred = context->after_join_pred;
      current_context->val_descr = context->val_descr;

      current_context->level = context->level + 1;
      current_context->divisor = context->divisor * hash_part_cnt;
    }

  *part_contexts = contexts;

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;

error_exit:
  /* the references to the partition lists, before the contexts that own them */
  hjoin_clear_split_info (thread_p, manager, split_info);

  if (contexts != NULL)
    {
      hjoin_clear_part_contexts (thread_p, contexts, part_cnt);
    }

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  return error;
}

/*
 * hjoin_clear_part_contexts() - clear the contexts of the partitions with the partition lists they own, and free them
 *   return: None.
 *   thread_p(in): Thread entry.
 *   part_contexts(in): Contexts made by hjoin_init_part_contexts.
 *   part_cnt(in): Number of partitions.
 */
static void
hjoin_clear_part_contexts (THREAD_ENTRY * thread_p, HASHJOIN_CONTEXT * part_contexts, UINT32 part_cnt)
{
  UINT32 part_index;

  assert (thread_p != NULL);
  assert (part_contexts != NULL);

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      hjoin_clear_context (thread_p, &part_contexts[part_index]);
    }

  free_and_init (part_contexts);
}

/*
 * hjoin_init_exec_contexts() - list the contexts the join executes, once all the splits are done
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   manager(in/out): Hash join manager; exec_contexts refers to the contexts not split again and to the partitions
 *                    of those split again, in the order of the contexts.
 */
static int
hjoin_init_exec_contexts (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  HASHJOIN_CONTEXT *current_context;
  UINT32 context_index, part_index, exec_cnt, exec_index;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (manager->single_context.child_contexts != NULL);
  assert (manager->exec_contexts == NULL);

  exec_cnt = 0;
  for (context_index = 0; context_index < manager->single_context.child_cnt; context_index++)
    {
      current_context = &manager->single_context.child_contexts[context_index];
      exec_cnt += (current_context->child_cnt == 0) ? 1 : current_context->child_cnt;
    }

  manager->exec_contexts = (HASHJOIN_CONTEXT **) db_private_alloc (thread_p, exec_cnt * sizeof (HASHJOIN_CONTEXT *));
  if (manager->exec_contexts == NULL)
    {
      assert_release_error (er_errid () != NO_ERROR);
      return er_errid ();
    }

  exec_index = 0;
  for (context_index = 0; context_index < manager->single_context.child_cnt; context_index++)
    {
      current_context = &manager->single_context.child_contexts[context_index];

      if (current_context->child_cnt == 0)
	{
	  manager->exec_contexts[exec_index++] = current_context;
	  continue;
	}

      for (part_index = 0; part_index < current_context->child_cnt; part_index++)
	{
	  manager->exec_contexts[exec_index++] = &current_context->child_contexts[part_index];
	}
    }
  assert (exec_index == exec_cnt);
  manager->exec_cnt = exec_cnt;

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hjoin_clear_exec_contexts() - free the list of the contexts the join executes; the contexts stay with their owners
 *   return: None.
 *   thread_p(in): Thread entry.
 *   manager(in/out): Hash join manager.
 */
static void
hjoin_clear_exec_contexts (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager)
{
  assert (thread_p != NULL);
  assert (manager != NULL);

  if (manager->exec_contexts != NULL)
    {
      db_private_free_and_init (thread_p, manager->exec_contexts);
    }
  manager->exec_cnt = 0;
}

/*
 * hjoin_estimate_hash_table_size() - memory the HYBRID hash table of a build input would take, estimated linearly
 *   return: Size in bytes.
 *   list_id(in): Build input; its tuple_cnt may be over INT_MAX, which hjoin_calc_hash_table_size cannot take.
 */
static UINT64
hjoin_estimate_hash_table_size (QFILE_LIST_ID * list_id)
{
  UINT64 per_entry_size;

  assert (list_id != NULL);
  assert (list_id->tuple_cnt >= 0);

  /* Per-entry HYBRID size: ~2 slots (load factor 1/0.7 * power-of-two rounding) + one entry + a tuple position.
   * Linear estimate is enough: tuple_cnt is INT64, and the partition count is only approximate. */
  per_entry_size = 2 * sizeof (MHT_HLS_SLOT) + sizeof (MHT_HLS_ENTRY) + sizeof (QFILE_TUPLE_SIMPLE_POS);

  return per_entry_size * list_id->tuple_cnt;
}

/*
 * hjoin_calc_hash_table_size() - memory the hash table of a build input takes, as hjoin_scan_init compares it
 *   return: Size in bytes.
 *   list_id(in): Build input; its tuple_cnt fits in int.
 *   method(in): HASH_METH_IN_MEM or HASH_METH_HYBRID.
 */
static UINT64
hjoin_calc_hash_table_size (QFILE_LIST_ID * list_id, HASH_METHOD method)
{
  UINT64 slot_array_size, entries_size, payload_size;

  assert (list_id != NULL);
  assert (list_id->tuple_cnt <= INT_MAX);

  slot_array_size = (UINT64) mht_hls_slot_count ((int) list_id->tuple_cnt) * sizeof (MHT_HLS_SLOT);
  entries_size = (UINT64) list_id->tuple_cnt * sizeof (MHT_HLS_ENTRY);

  if (method == HASH_METH_IN_MEM)
    {
      /* IN_MEM: slot array + one entry (header) per row + the tuples themselves */
      payload_size = (UINT64) list_id->page_cnt * DB_PAGESIZE;
    }
  else
    {
      /* HYBRID: slot array + one entry (header) per row + a tuple position per row; tuples stay on the temp file */
      assert (method == HASH_METH_HYBRID);
      payload_size = (UINT64) list_id->tuple_cnt * sizeof (QFILE_TUPLE_SIMPLE_POS);
    }

  return slot_array_size + entries_size + payload_size;
}

/*
 * hjoin_scan_init() -
 *   return: Error code (NO_ERROR if successful, error code otherwise).
 *   thread_p(in): Thread entry.
 *   hash_scan(in/out): Hash scan structure to initialize.
 *   key_cnt(in): Number of join columns.
 *   mem_limit(in): max_hash_list_scan_size; 0 is possible (see hjoin_check_partition), and then every size check
 *                  fails and HASH_FILE is chosen.
 *   list_id(in): List identifier to be used as build input.
 */
int
hjoin_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt, UINT64 mem_limit,
		 QFILE_LIST_ID * list_id)
{
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_id == NULL || list_id->tuple_cnt > 0);
  assert (key_cnt > 0);

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

  if (list_id != NULL)
    {
      UINT64 in_mem_size = 0;
      UINT64 hybrid_size = 0;

      /* the slot array is int-indexed; the estimates are computable only when tuple_cnt fits in int */
      if (list_id->tuple_cnt <= INT_MAX)
	{
	  in_mem_size = hjoin_calc_hash_table_size (list_id, HASH_METH_IN_MEM);
	  hybrid_size = hjoin_calc_hash_table_size (list_id, HASH_METH_HYBRID);
	}

      if (list_id->tuple_cnt <= INT_MAX && in_mem_size <= mem_limit)
	{

	  hash_scan->hash_list_scan_type = HASH_METH_IN_MEM;

	  hash_scan->memory.hash_table = mht_create_hls ("Hash Join", list_id->tuple_cnt, NULL, NULL);
	  if (hash_scan->memory.hash_table == NULL)
	    {
	      goto error_exit;
	    }

	  hash_scan->memory.curr_hash_entry = NULL;
	}
      else if (list_id->tuple_cnt <= INT_MAX && hybrid_size <= mem_limit)
	{

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
    }
  else
    {
      /* skip hash table */
      hash_scan->hash_list_scan_type = HASH_METH_NOT_USE;
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
void
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
      /* fall through */
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
    case JOIN_SEMI:
      status = (outer_tuple_cnt == 0 || inner_tuple_cnt == 0) ? HASHJOIN_STATUS_END : HASHJOIN_STATUS_TRY;
      break;

    case JOIN_LEFT:
    case JOIN_ANTI:
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

  int value_index, key_index;
  bool value_is_null;

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

  /* value_indexes is non-decreasing (a value may serve several keys), so the slot cache keeps locates O(1) */
  for (key_index = 0; key_index < key->val_count; key_index++)
    {
      value_index = value_indexes[key_index];
      pr_clear_value (key->values[key_index]);

      if (need_coerce_domains && coerce_domains[key_index] != NULL && coerce_domains[key_index] != domains[key_index])
	{
	  error = qfile_slot_read_column_value (tuple_record, value_index, domains[key_index], &pre_coerce_value, false,
						&value_is_null);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  /* Skip the tuple if any value is NULL */
	  if (value_is_null)
	    {
	      goto skip_next;
	    }

	  if (coerce_domains[key_index]->type->id == DB_TYPE_NUMERIC
	      && pre_coerce_value.domain.general_info.type == DB_TYPE_NUMERIC
	      && coerce_domains[key_index]->precision == DB_DEFAULT_NUMERIC_PRECISION
	      && pre_coerce_value.domain.numeric_info.precision != DB_DEFAULT_NUMERIC_PRECISION
	      && pre_coerce_value.domain.numeric_info.scale < 0)
	    {
	      /* 
	       * for float numeric and fixed numeric, this is used to recalculate the fixed
	       * numeric's precision later, as it must be known accurately during normalization.
	       * 
	       * note: A value in numeric(38,0) column does not guarantee precision 38.
	       */
	      pre_coerce_value.domain.numeric_info.precision = DB_HJOIN_NUMERIC_PRECISION_DEFERRED;
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
	    qfile_slot_read_column_value (tuple_record, value_index, domains[key_index], key->values[key_index], false,
					  &value_is_null);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  /* Skip the tuple if any value is NULL */
	  if (value_is_null)
	    {
	      goto skip_next;
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
 * hjoin_locate_tuple_hash_key() - locate the body of the leading hash-key column (INT, always bound)
 *   return: pointer to the 4-byte hash key inside the tuple
 *   tuple_record(in): tuple containing the hash key as its first column
 */
QFILE_TUPLE
hjoin_locate_tuple_hash_key (QFILE_TUPLE_RECORD * tuple_record)
{
  int len;
  bool is_null;
  QFILE_TUPLE body;

  body = (QFILE_TUPLE) qfile_slot_get_column_data (tuple_record, 0, &len, &is_null);
  assert (!is_null);
  assert (len == tp_Integer.disksize);	/* FIXED INT column: the body is the aligned 4-byte value itself */

  return body;
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

  tuple_value = hjoin_locate_tuple_hash_key (tuple_record);
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
  assert (!thread_is_on_trace (thread_p) || stats != NULL);

  build = context->build;
  assert (build != NULL);
  assert (build->list_scan_id.status != S_CLOSED);

  // *INDENT-OFF*
  build->tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
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

  if (manager->single_context.child_cnt == 0)	/* HASHJOIN_STATUS_SINGLE */
    {
      while ((scan_code =
	      qfile_scan_list_next (thread_p, &build->list_scan_id, &build->tuple_record, PEEK)) == S_SUCCESS)
	{
	  error =
	    hjoin_fetch_key (thread_p, build, &build->tuple_record, key, NULL /* compare_key */ , &need_skip_next);

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
	      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);

	      error = hjoin_build_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);

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
	  tuple_value = hjoin_locate_tuple_hash_key (&build->tuple_record);

	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);

	  error = hjoin_build_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);

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
    }

  /* qfile_close_scan is called by the caller. */

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      error = (error == NO_ERROR) ? er_errid () : error;
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
  MHT_HLS_ENTRY *entry = NULL;
  TFTID tftid;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_scan_id != NULL);
  assert (tuple_record != NULL && tuple_record->tpl != NULL);

  switch (hash_scan->hash_list_scan_type)
    {
    case HASH_METH_IN_MEM:
      assert (hash_scan->memory.hash_table != NULL);

      entry = qdata_alloc_hscan_value (thread_p, hash_scan->memory.hash_table->heap_id, tuple_record->tpl);
      if (entry == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, entry) == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      break;

    case HASH_METH_HYBRID:
      assert (hash_scan->memory.hash_table != NULL);

      entry = qdata_alloc_hscan_value_OID (thread_p, hash_scan->memory.hash_table->heap_id, list_scan_id);
      if (entry == NULL)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, entry) == NULL)
	{
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
 */
static int
hjoin_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *list_id;
  INT64 start_tuple_cnt;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->list_id != NULL);

  list_id = context->list_id;

#if defined (SERVER_MODE)
  if (context->status == HASHJOIN_STATUS_PARALLEL_PROBE)
    {
      assert (context == &manager->single_context);

      // *INDENT-OFF*
      error = parallel_query::hash_join::probe_prepare (*thread_p, manager);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      error = parallel_query::hash_join::probe_execute (*thread_p, manager);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      // *INDENT-ON*
    }
  else
#endif /* defined (SERVER_MODE) */
    {
      error = qfile_reopen_list_as_append_mode (thread_p, list_id);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      start_tuple_cnt = list_id->tuple_cnt;

      if (IS_NULL_FILL_JOIN_TYPE (manager->join_type))
	{
	  error = hjoin_outer_probe (thread_p, manager, context, list_id);
	}
      else
	{
	  error = hjoin_inner_probe (thread_p, manager, context, list_id);
	}

      qfile_close_list (thread_p, list_id);

      /* the probe counted the tuples of the shared list */
      if (context->stats != NULL)
	{
	  context->stats->probe.qualified_rows -= start_tuple_cnt;
	}
    }

cleanup:
  /* Check if qfile_close_list was called */
  assert (list_id->last_pgptr == NULL);

  return error;

error_exit:
  qfile_close_list (thread_p, list_id);

  if (error == NO_ERROR || er_errid () == NO_ERROR)
    {
      assert_release_error (er_errid () != NO_ERROR);
      error = er_errid ();
    }

  goto cleanup;
}

/*
 * hjoin_inner_probe() -
 *   return: Error code (NO_ERROR if successful, error code otherwise)
 *   thread_p(in): Thread entry.
 *   manager(in): Hash join manager containing shared state.
 *   context(in): Hash join context containing per-partition state.
 *   list_id(in/out): List identifier containing the join result.
 */
static int
hjoin_inner_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context,
		   QFILE_LIST_ID * list_id)
{
  QFILE_TUPLE_RECORD overflow_record = QFILE_TUPLE_RECORD_INITIALIZER;
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
  probe->tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  build->tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
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
      probe->is_ready = false;


      if (manager->single_context.child_cnt == 0)	/* HASHJOIN_STATUS_SINGLE */
	{
	  error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
				   &need_skip_next);

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

	  hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
	}
      else
	{
	  tuple_value = hjoin_locate_tuple_hash_key (&probe->tuple_record);

	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);
	}

      do
	{
	  build->is_ready = false;

	  error = hjoin_probe_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);

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

	  if (manager->single_context.child_cnt != 0)	/* HASHJOIN_STATUS_PARTITION or HASHJOIN_STATUS_PARALLEL */
	    {
	      error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
				       &need_skip_next);

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

	  error = hjoin_fetch_key (thread_p, build, &build->tuple_record, found_key, key /* compare_key */ ,
				   &need_skip_next);

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

	  if (context->after_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Search the next hash entry if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  continue;
		}
	    }			/* if (context->after_join_pred != NULL) */


	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, &outer->tuple_record, &inner->tuple_record,
					  manager->merge_info, &overflow_record);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }

	  if (manager->join_type == JOIN_SEMI)
	    {
	      /* semi join: one match is enough to emit this row, so stop scanning. */
	      qfile_slot_reset (&build->tuple_record);
	      break;
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
  QFILE_TUPLE_RECORD overflow_record = QFILE_TUPLE_RECORD_INITIALIZER;
  SCAN_CODE scan_code;
  bool need_skip_next = false;
  bool any_key_matched;

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
  probe->tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  build->tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
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
      probe->is_ready = false;


      if (manager->single_context.child_cnt == 0)	/* HASHJOIN_STATUS_SINGLE */
	{
	  error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
				   &need_skip_next);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	  else if (need_skip_next)
	    {
	      need_skip_next = false;	/* init */

	      if (context->after_join_pred != NULL)
		{
		  DB_LOGICAL ev_res;

		  /* No build match: null-pad the build side, then evaluate the after-join predicate. */
		  for (REGU_VARIABLE_LIST regu_var_p = build->regu_list_pred; regu_var_p != NULL;
		       regu_var_p = regu_var_p->next)
		    {
		      pr_clear_value (regu_var_p->value.vfetch_to);
		    }
		  build->is_ready = true;

		  ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);

		  if (ev_res == V_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      error = er_errid ();
		      break;	/* error_exit */
		    }

		  /* Fetch the next row if additional conditions are not satisfied */
		  if (ev_res != V_TRUE)
		    {
		      continue;
		    }
		}		/* if (context->after_join_pred != NULL) */


	      /* NULL key on preserved side — emit fill_record (null on null-supplying side) */
	      error =
		hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record,
					      manager->merge_info, &overflow_record);

	      if (error != NO_ERROR)
		{
		  break;	/* error_exit */
		}

	      continue;
	    }			/* else if (need_skip_next) */
	  else
	    {
	      /* fall through */
	    }

	  hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
	}
      else
	{
	  tuple_value = hjoin_locate_tuple_hash_key (&probe->tuple_record);

	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);
	}

      any_key_matched = false;

      do
	{
	  build->is_ready = false;

	  error = hjoin_probe_key (thread_p, hash_scan, &build->list_scan_id, &build->tuple_record);

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

	  if (manager->single_context.child_cnt != 0)	/* HASHJOIN_STATUS_PARTITION or HASHJOIN_STATUS_PARALLEL */
	    {
	      error = hjoin_fetch_key (thread_p, probe, &probe->tuple_record, key, NULL /* compare_key */ ,
				       &need_skip_next);

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
		  break;	/* error_exit */
		}
	      else
		{
		  /* fall through */
		}
	    }

	  error = hjoin_fetch_key (thread_p, build, &build->tuple_record, found_key, key /* compare_key */ ,
				   &need_skip_next);

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

	  if (context->during_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->during_join_pred, context->val_descr);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Search the next hash entry if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  continue;
		}
	    }			/* if (context->during_join_pred != NULL) */

	  any_key_matched = true;

	  if (manager->join_type == JOIN_ANTI)
	    {
	      /* anti join: one match is enough to suppress this row, so stop scanning.
	       * The unnested subquery condition becomes the ON clause,
	       * so after_join_pred does not decide the match.
	       */
	      qfile_slot_reset (&build->tuple_record);
	      break;
	    }

	  if (context->after_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Search the next hash entry if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  continue;
		}
	    }			/* if (context->after_join_pred != NULL) */


	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, &outer->tuple_record, &inner->tuple_record,
					  manager->merge_info, &overflow_record);

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

      if (!any_key_matched)
	{
	  if (context->after_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

	      /* No build match: null-pad the build side, then evaluate the after-join predicate. */
	      for (REGU_VARIABLE_LIST regu_var_p = build->regu_list_pred; regu_var_p != NULL;
		   regu_var_p = regu_var_p->next)
		{
		  pr_clear_value (regu_var_p->value.vfetch_to);
		}
	      build->is_ready = true;

	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Fetch the next row if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  continue;
		}
	    }			/* if (context->after_join_pred != NULL) */


	  /* no match — emit fill_record (null on null-supplying side) */
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record,
					  manager->merge_info, &overflow_record);

	  if (error != NO_ERROR)
	    {
	      break;		/* error_exit */
	    }
	}			/* if (!any_key_matched) */
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
int
hjoin_probe_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_SCAN_ID * list_scan_id,
		 QFILE_TUPLE_RECORD * tuple_record)
{
  MHT_HLS_ENTRY *entry = NULL;
  TFTID tftid;
  EH_SEARCH eh_search;
  QFILE_TUPLE_SIMPLE_POS *simple_pos;
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
	  entry =
	    mht_get_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
			 &hash_scan->memory.curr_hash_entry);
	}
      else
	{
	  entry =
	    mht_get_next_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
			      &hash_scan->memory.curr_hash_entry);
	}

      if (entry != NULL)
	{
	  /* in-memory hash entry payload: a raw tuple of the build list, bind it to that list's descriptor */
	  qfile_slot_set_tuple_ptr_and_layout (tuple_record, (QFILE_TUPLE) MHT_HLS_ENTRY_PAYLOAD (entry),
					       0 /* PEEK: the payload is owned by the hash table */ ,
					       &list_scan_id->list_id.type_list);
	}
      else
	{
	  /* not found */
	  qfile_slot_reset (tuple_record);
	}
      break;			/* HASH_METH_IN_MEM */

    case HASH_METH_HYBRID:
      assert (hash_scan->memory.hash_table != NULL);

      if (tuple_record->tpl == NULL)
	{
	  entry =
	    mht_get_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
			 &hash_scan->memory.curr_hash_entry);
	}
      else
	{
	  entry =
	    mht_get_next_hls (hash_scan->memory.hash_table,
			      (void *) &hash_scan->curr_hash_key, &hash_scan->memory.curr_hash_entry);
	}

      if (entry != NULL)
	{
	  simple_pos = (QFILE_TUPLE_SIMPLE_POS *) MHT_HLS_ENTRY_PAYLOAD (entry);
	  MAKE_TUPLE_POSTION (tuple_position, simple_pos, list_scan_id);
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
	  qfile_slot_reset (tuple_record);
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
	  qfile_slot_reset (tuple_record);
	}
      else
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      break;			/* HASH_METH_HASH_FILE */

    case HASH_METH_NOT_USE:
      /* fall through */
    default:
      /* impossible case */
      assert_release_error (false);
      return er_errid ();
    }

  ASSERT_NO_ERROR_OR_INTERRUPTED ();
  return NO_ERROR;
}

/*
 * hjoin_eval_pred() -
 *   return: Predicate result (V_TRUE/V_FALSE/V_UNKNOWN), or V_ERROR on a fetch/eval failure.
 *   thread_p(in): Thread entry.
 *   probe(in/out): Probe-side fetch info; is_ready memoizes its fetch.
 *   build(in/out): Build-side fetch info; is_ready memoizes its fetch.
 *   pred(in): Predicate to evaluate.
 *   val_descr(in): Value descriptor for query evaluation.
 */
DB_LOGICAL
hjoin_eval_pred (THREAD_ENTRY * thread_p, HASHJOIN_FETCH_INFO * probe, HASHJOIN_FETCH_INFO * build, PRED_EXPR * pred,
		 VAL_DESCR * val_descr)
{
  assert (thread_p != NULL);
  assert (probe != NULL);
  assert (build != NULL);
  assert (pred != NULL);
  assert (val_descr != NULL);

  if (!probe->is_ready)
    {
      if (fetch_val_list (thread_p, probe->regu_list_pred, val_descr, NULL, NULL, &probe->tuple_record, PEEK)
	  != NO_ERROR)
	{
	  return V_ERROR;
	}
      probe->is_ready = true;
    }

  if (!build->is_ready)
    {
      if (fetch_val_list (thread_p, build->regu_list_pred, val_descr, NULL, NULL, &build->tuple_record, PEEK)
	  != NO_ERROR)
	{
	  return V_ERROR;
	}
      build->is_ready = true;
    }

  return eval_pred (thread_p, pred, val_descr, NULL);
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
int
hjoin_merge_tuple_to_list_id (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
			      QFILE_TUPLE_RECORD * outer_record,
			      QFILE_TUPLE_RECORD * inner_record, QFILE_LIST_MERGE_INFO * merge_info,
			      QFILE_TUPLE_RECORD * overflow_record)
{
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (list_id != NULL);
  assert (outer_record != NULL || inner_record != NULL);
  assert (merge_info != NULL);
  assert (overflow_record != NULL);

  /* merge path: one deform per input, exact size, page or private buffer */
  error = qfile_merge_tuple_add_list (thread_p, list_id, outer_record, inner_record, merge_info, overflow_record);
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

/*
 * hjoin_trace_merge_stats() -
 *   return: None.
 *   stats(in/out): Profiling data to be merged.
 *   context_stats(in): Profiling data per-partition.
 */
void
hjoin_trace_merge_stats (HASHJOIN_STATS * stats, HASHJOIN_STATS * context_stats, HASHJOIN_STATUS status)
{
  assert (stats != NULL);
  assert (context_stats != NULL);

  if (stats == NULL || context_stats == NULL)
    {
      /* impossible case */
      assert (false);
      return;
    }

  if (status != HASHJOIN_STATUS_PARALLEL && status != HASHJOIN_STATUS_PARALLEL_PROBE)
    {
      TSC_ADD_TIMEVAL (stats->build.elapsed_time, context_stats->build.elapsed_time);
    }
  stats->build.fetches += context_stats->build.fetches;
  stats->build.ioreads += context_stats->build.ioreads;
  stats->build.read_rows += context_stats->build.read_rows;
  stats->build.read_keys += context_stats->build.read_keys;
  stats->build.qualified_rows += context_stats->build.qualified_rows;

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

  /* a context split again carries the methods of its sub-contexts, merged into it the same way */
  stats->use_hash_memory |= context_stats->use_hash_memory;
  stats->use_hash_hybrid |= context_stats->use_hash_hybrid;
  stats->use_hash_file |= context_stats->use_hash_file;
  stats->use_hash_skip |= context_stats->use_hash_skip;

  if (status != HASHJOIN_STATUS_PARALLEL && status != HASHJOIN_STATUS_PARALLEL_PROBE)
    {
      TSC_ADD_TIMEVAL (stats->probe.elapsed_time, context_stats->probe.elapsed_time);
    }
  stats->probe.fetches += context_stats->probe.fetches;
  stats->probe.ioreads += context_stats->probe.ioreads;
  stats->probe.read_rows += context_stats->probe.read_rows;
  stats->probe.read_keys += context_stats->probe.read_keys;
  stats->probe.qualified_rows += context_stats->probe.qualified_rows;

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

  assert (manager != NULL);
  assert (manager->num_parallel_threads > 1);
  assert (manager->px_worker_stats != NULL);

  /* immutable */
  static const size_t stats_size = perfmon_get_number_of_statistic_values () * sizeof (UINT64);

  task_cnt = manager->num_parallel_threads;

  for (task_index = 0; task_index < task_cnt; task_index++)
    {
      worker_stats = hjoin_trace_get_worker_stats (manager, task_index);

      /* everything the worker counted in this phase */
      perfmon_add_values_to_local (thread_p, worker_stats);
      memset (worker_stats, 0, stats_size);
    }
}
