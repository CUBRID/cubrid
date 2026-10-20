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
#include "qfile_tuple_layout.h"

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
 * Macro Function Declarations
 */

#if HASHJOIN_DUMP_HASH_TABLE
#define HJOIN_DUMP_HASH_TABLE(thread_p, hash_scan_p, list_id_p) \
  hjoin_dump_hash_table ((thread_p), (hash_scan_p), (list_id_p))
#else
#define HJOIN_DUMP_HASH_TABLE(thread_p, hash_scan_p, list_id_p) ((void) 0)
#endif /* HASHJOIN_DUMP_HASH_TABLE */

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
static HASHJOIN_STATUS hjoin_try_parallel_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager,
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


/* Dump */
#if HASHJOIN_DUMP_HASH_TABLE
static void hjoin_dump_hash_table (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_LIST_ID * list_id);
#endif /* HASHJOIN_DUMP_HASH_TABLE */

#if !defined(NDEBUG) && HASHJOIN_DUMP_PROBE
static void hjoin_print_tuple (QFILE_LIST_ID * list_id, QFILE_TUPLE tuple, HASHJOIN_PRINT_STEP step);
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
      assert (!XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) || QFILE_LIST_IS_BACKWARD (single_context->list_id));
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
	  hjoin_trace_merge_stats (stats, current_context->stats, manager->single_context.status);
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
			       (manager->single_context.list_id !=
				nullptr) ? manager->single_context.list_id->tuple_cnt : 0);

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

  /* In joins that NULL-fill one side,
   * tuples with NULL in any join column are placed in the last partition.
   * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition.
   */
  if (IS_NULL_FILL_JOIN_TYPE (manager->join_type) && context == &manager->contexts[manager->context_cnt - 1])
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
  QFILE_TUPLE_RECORD overflow_record = QFILE_TUPLE_RECORD_INITIALIZER;
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

  if (manager->contexts != NULL)
    {
      contexts = manager->contexts;
      context_cnt = manager->context_cnt;
      assert (context_cnt > 1);

      for (context_index = 0; context_index < context_cnt; context_index++)
	{
	  current_context = &contexts[context_index];

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

#if defined (SERVER_MODE)
  status = hjoin_try_parallel (thread_p, manager, single_context);
  single_context->status = status;
#endif /* defined (SERVER_MODE) */

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
  UINT64 per_entry_size;
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

  /* Per-entry HYBRID size: ~2 slots (load factor 1/0.7 * power-of-two rounding) + one entry + a tuple position.
   * Linear estimate is enough: min_tuple_cnt is INT64, and the partition count is only approximate
   * (each partition re-selects its method). */
  per_entry_size = 2 * sizeof (MHT_HLS_SLOT) + sizeof (MHT_HLS_ENTRY) + sizeof (QFILE_TUPLE_SIMPLE_POS);

  part_cnt = CEIL_PTVDIV (per_entry_size * min_tuple_cnt, mem_limit * PARTITION_FILL_FACTOR);
  if (part_cnt > 1)
    {
      if (IS_NULL_FILL_JOIN_TYPE (manager->join_type))
	{
	  /* In joins that NULL-fill one side,
	   * tuples with NULL in any join column are placed in the last partition.
	   * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition.
	   */
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
	qfile_open_list (thread_p, &outer_list_id->type_list, NULL, outer_list_id->query_id,
			 QFILE_FLAG_ALL | QFILE_LIST_BACKWARD_FLAG (outer_list_id), NULL);
      if (outer_part_list_id[part_index] == NULL)
	{
	  goto error_exit;
	}

      inner_part_list_id[part_index] =
	qfile_open_list (thread_p, &inner_list_id->type_list, NULL, inner_list_id->query_id,
			 QFILE_FLAG_ALL | QFILE_LIST_BACKWARD_FLAG (inner_list_id), NULL);
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
      current_context->after_join_pred = single_context->after_join_pred;
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
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  SCAN_CODE scan_code;

  unsigned int hash_key;
  UINT32 part_cnt, part_index, part_id;

  bool has_null_fill_side = false;
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

  has_null_fill_side = IS_NULL_FILL_JOIN_TYPE (manager->join_type);

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

	  if (has_null_fill_side)
	    {
	      /* In joins that NULL-fill one side,
	       * tuples with NULL in any join column are placed in the last partition.
	       * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition.
	       */
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
	  part_id = (has_null_fill_side) ? hash_key % (part_cnt - 1) : hash_key % (part_cnt);

	  hjoin_update_tuple_hash_key (thread_p, &tuple_record, hash_key);
	}

      /* overflow page */
      if (QFILE_GET_OVERFLOW_PAGE_ID (list_scan_id.curr_pgptr) != NULL_PAGEID)
	{
	  assert (part_list_id[part_id]->last_pgptr == NULL);

	  error = qfile_reopen_list_as_append_mode (thread_p, part_list_id[part_id]);
	  if (error != NO_ERROR)
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
	    qfile_open_list (thread_p, &list_id->type_list, NULL, list_id->query_id,
			     QFILE_FLAG_ALL | QFILE_LIST_BACKWARD_FLAG (list_id), NULL);
	  if (temp_part_list_id[part_id] == NULL)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      error = er_errid ();
	      break;		/* error_exit */
	    }
	}

      /* without a memory buffer, a fixed last page per partition exhausts the page buffer; no-op with membuf */
      error = qfile_reopen_list_as_append_mode (thread_p, temp_part_list_id[part_id]);
      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}

      error = qfile_add_tuple_to_list (thread_p, temp_part_list_id[part_id], tuple_record.tpl);

      /* close before the error check so that the list is closed on every path */
      qfile_close_list (thread_p, temp_part_list_id[part_id]);

      if (error != NO_ERROR)
	{
	  break;		/* error_exit */
	}

      /* without a memory buffer, the page is in the temp file */
      assert (temp_part_list_id[part_id]->tfile_vfid->membuf_npages == 0
	      || VFID_ISNULL (&temp_part_list_id[part_id]->tfile_vfid->temp_vfid));
    }				/* while (qfile_scan_list_next (list_scan_id)) */

  /* After qfile_open_list_scan, if an error occurs,
   * ensure qfile_close_scan runs here
   * before jumping to error_exit. */
  qfile_close_scan (thread_p, &list_scan_id);

  /* skip the merge on error, as the parallel split does; a successful append below would overwrite the error */
  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto error_exit;
    }

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

  degree = MIN (degree, manager->context_cnt /* part_cnt */ );

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
  assert (manager->context_cnt == 0);
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
int
hjoin_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt, QFILE_LIST_ID * list_id)
{
  UINT64 mem_limit;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_id == NULL || list_id->tuple_cnt > 0);
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

  if (list_id != NULL)
    {
      UINT64 in_mem_size = 0;
      UINT64 hybrid_size = 0;

      /* the slot array is int-indexed; the estimates are computable only when tuple_cnt fits in int */
      if (list_id->tuple_cnt <= INT_MAX)
	{
	  UINT64 slot_array_size = (UINT64) mht_hls_slot_count ((int) list_id->tuple_cnt) * sizeof (MHT_HLS_SLOT);
	  UINT64 entries_size = (UINT64) list_id->tuple_cnt * sizeof (MHT_HLS_ENTRY);
	  UINT64 payload_size;

	  /* IN_MEM: slot array + one entry (header) per row + the tuples themselves */
	  payload_size = (UINT64) list_id->page_cnt * DB_PAGESIZE;
	  in_mem_size = slot_array_size + entries_size + payload_size;

	  /* HYBRID: slot array + one entry (header) per row + a tuple position per row; tuples stay on the temp file */
	  payload_size = (UINT64) list_id->tuple_cnt * sizeof (QFILE_TUPLE_SIMPLE_POS);
	  hybrid_size = slot_array_size + entries_size + payload_size;
	}

      if (list_id->tuple_cnt <= INT_MAX && in_mem_size <= mem_limit)
	{
#if HASHJOIN_DUMP_BUILD
	  fprintf (stdout, "\nHash Join Method: In Memory\n");
	  fprintf (stdout, "  - in_mem_size %lu <= mem_limit %lu\n", in_mem_size, mem_limit);
#endif /* HASHJOIN_DUMP_BUILD */

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
#if HASHJOIN_DUMP_BUILD
	  fprintf (stdout, "\nHash Join Method: Hybrid\n");
	  fprintf (stdout, "  - in_mem_size %lu > mem_limit %lu\n", in_mem_size, mem_limit);
	  fprintf (stdout, "  - hybrid_size %lu <= mem_limit %lu\n", hybrid_size, mem_limit);
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
	  fprintf (stdout, "  - hybrid_size %lu > mem_limit %lu\n", hybrid_size, mem_limit);
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
static QFILE_TUPLE
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
#if HASHJOIN_PROFILE_TIME
  HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
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
	  tuple_value = hjoin_locate_tuple_hash_key (&build->tuple_record);
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
 */
static int
hjoin_probe (THREAD_ENTRY * thread_p, HASHJOIN_MANAGER * manager, HASHJOIN_CONTEXT * context)
{
  QFILE_LIST_ID *list_id = NULL;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->list_id == NULL);

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
      list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, manager->qlist_flag, NULL);
      if (list_id == NULL)
	{
	  goto error_exit;
	}

      if (IS_NULL_FILL_JOIN_TYPE (manager->join_type))
	{
	  error = hjoin_outer_probe (thread_p, manager, context, list_id);
	}
      else
	{
	  error = hjoin_inner_probe (thread_p, manager, context, list_id);
	}

      qfile_close_list (thread_p, list_id);
      context->list_id = list_id;
    }

cleanup:
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

      HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_READ_KEY);

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
	  tuple_value = hjoin_locate_tuple_hash_key (&probe->tuple_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	}

      do
	{
	  build->is_ready = false;

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
	      HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

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

	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Search the next hash entry if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
		  continue;
		}
	    }			/* if (context->after_join_pred != NULL) */

	  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, &outer->tuple_record, &inner->tuple_record,
					  manager->merge_info, &overflow_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

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

      HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_READ_KEY);

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

		  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
		  ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);
		  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

		  if (ev_res == V_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      error = er_errid ();
		      break;	/* error_exit */
		    }

		  /* Fetch the next row if additional conditions are not satisfied */
		  if (ev_res != V_TRUE)
		    {
		      HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
		      continue;
		    }
		}		/* if (context->after_join_pred != NULL) */

	      HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

	      /* NULL key on preserved side — emit fill_record (null on null-supplying side) */
	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	      error =
		hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record,
					      manager->merge_info, &overflow_record);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

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

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	  hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	}
      else
	{
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	  tuple_value = hjoin_locate_tuple_hash_key (&probe->tuple_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);

	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	  hash_scan->curr_hash_key = (UINT32) OR_GET_INT (tuple_value);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	}

      any_key_matched = false;

      do
	{
	  build->is_ready = false;

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
		  break;	/* error_exit */
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
	      HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

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
	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->during_join_pred, context->val_descr);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Search the next hash entry if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
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

	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Search the next hash entry if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
		  continue;
		}
	    }			/* if (context->after_join_pred != NULL) */

	  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

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

	      HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
	      ev_res = hjoin_eval_pred (thread_p, probe, build, context->after_join_pred, context->val_descr);
	      HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

	      if (ev_res == V_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  error = er_errid ();
		  break;	/* error_exit */
		}

	      /* Fetch the next row if additional conditions are not satisfied */
	      if (ev_res != V_TRUE)
		{
		  HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
		  continue;
		}
	    }			/* if (context->after_join_pred != NULL) */

	  HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

	  /* no match — emit fill_record (null on null-supplying side) */
	  HJOIN_PROFILE_START (thread_p, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
	  error =
	    hjoin_merge_tuple_to_list_id (thread_p, list_id, outer->fill_record, inner->fill_record,
					  manager->merge_info, &overflow_record);
	  HJOIN_PROFILE_END (thread_p, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

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

  if (status != HASHJOIN_STATUS_PARALLEL && status != HASHJOIN_STATUS_PARALLEL_PROBE)
    {
      TSC_ADD_TIMEVAL (stats->probe.elapsed_time, context_stats->probe.elapsed_time);
    }
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
 *   list_id(in): List identifier for the given tuple.
 *   tuple(in): Tuple to be printed.
 *   step(in): Step at which the tuple is printed.
 */
static void
hjoin_print_tuple (QFILE_LIST_ID * list_id, QFILE_TUPLE tuple, HASHJOIN_PRINT_STEP step)
{
  assert (list_id != NULL);
  assert (tuple != NULL);

  if (list_id->tuple_cnt > DUMP_PROBE_LIMIT)
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

  qfile_print_tuple (&list_id->type_list, tuple);
}
#endif /* !NDEBUG && HASHJOIN_DUMP_PROBE */

/*
 * Bloom join filter (CBRD-27659, prototype)
 *
 * The two inputs of a hash join are materialized one after the other before the join runs (the aptr loop of
 * qexec_execute_mainblock_internal). Once the first is done, its join keys can tell the second which of its rows
 * can never find a partner. When the second input is the side the join does not preserve (either side of an
 * inner join, the inner side of a left join, the outer side of a right join), such rows never reach the result,
 * so they need not be written to its list at all.
 *
 * Correctness rests on hashing both sides exactly as the join compares them: the same common domains
 * (hjoin_init_domain_info), the same key extraction and coercion (hjoin_fetch_key) and the same hash
 * (qdata_hash_scan_key). A Bloom filter has no false negatives, so a row it rejects has no equal key on the
 * first side; a false positive only lets a row through.
 *
 * The filter is decided at run time, in two steps. Set-up (the common domains and the keys) happens at the second
 * input's first row - the second list's type list is open by then and the common domains need both - or before its
 * parallel scan starts. Filling the bits costs a pass over the first input, so it waits until the second input has
 * given threshold rows, max (HJOIN_BLOOM_MIN_SECOND_ROWS, first rows / HJOIN_BLOOM_FIRST_PER_SECOND): a second input
 * that stays small, or much smaller than the first, never pays for it, and the rows given before pass unchecked.
 * Once filled, the first HJOIN_BLOOM_SAMPLE_ROWS checked rows decide whether checking goes on: when fewer than
 * HJOIN_BLOOM_MIN_SKIP_PERCENT of them are dropped, the rest go through unchecked.
 *
 * Parallel paths. Under a parallel subquery executor the two inputs run as concurrent jobs on other threads. The
 * filter is attached to both before they start: the first input's job, once its list is final, publishes its row
 * count (hjoin_bloom_input_done) - only then can the bits be filled - and the second input counts the rows it
 * gives meanwhile, unchecked. The thread that runs the second input (the owner) fills the bits when the first is
 * done and the second has given threshold rows, so a first input that finishes late, or a second that stays
 * small, costs nothing but the counting. When the second input scans in parallel, its workers write rows to lists
 * of their own from cloned xasl nodes: they add the rows they see to the shared count in batches, the owner, which
 * polls for the workers' end, fills the bits (hjoin_bloom_poll) and publishes them, and from then on each worker
 * checks its rows against the shared bits with a key and a row buffer of its own (hjoin_bloom_worker_*), turns the
 * filter off for all when its own sample drops too few rows, and adds its counts at the end. Everything the owner
 * allocates it frees when the second input is done, on its own thread; the hash join's thread frees the filter
 * itself after both inputs are done (hjoin_bloom_finish).
 */


#define HJOIN_BLOOM_MAX_FIRST_ROWS	10000000	/* 2^27 bits (16 MB) at 10 bits per key, rounded up to a power of two */
#define HJOIN_BLOOM_BITS_PER_KEY	10
#define HJOIN_BLOOM_HASHES		3
#define HJOIN_BLOOM_MIN_SECOND_ROWS	1024	/* the second input must give this many rows before the bits are filled */
#define HJOIN_BLOOM_FIRST_PER_SECOND	2	/* ... and at least first rows / this */
#define HJOIN_BLOOM_SAMPLE_ROWS		4096	/* checked rows that decide whether checking goes on */
#define HJOIN_BLOOM_MIN_SKIP_PERCENT	50	/* ... it stops when fewer of them are dropped */
#define HJOIN_BLOOM_SEEN_BATCH		256	/* a worker adds the rows it saw to the shared count this often */

enum
{
  HJOIN_BLOOM_WAITING = 0,	/* the bits are not filled: rows go through unchecked */
  HJOIN_BLOOM_READY,		/* filled and published: rows are checked */
  HJOIN_BLOOM_OFF		/* not applicable, failed or not worth it: rows go through unchecked */
};

struct hjoin_bloom
{
  XASL_NODE *hj_xasl;		/* the hash join; its thread allocates and frees this struct */
  XASL_NODE *first_xasl;	/* the input whose keys fill the bits */
  XASL_NODE *owner_xasl;	/* the input whose rows are checked */
  THREAD_ENTRY *owner_thread;	/* the thread running owner_xasl, bound at its first use: reads the first input and
				 * owns key, row_buf and bits */
  bool first_is_outer;
  bool set_up;			/* domains and the owner's key are decided */

  /* HJOIN_BLOOM_WAITING -> READY or OFF; READY -> OFF. Read by the workers (acquire), published by the owner
   * (release) once the bits are filled; any thread may turn it off. */
  volatile INT64 state;
  INT64 off_reason;		/* HASHJOIN_BLOOM_*, written once before the state goes off */

  /* published by the first input's thread once its list is final (release), read by the owner (acquire) */
  volatile INT64 first_done;
  INT64 first_rows;		/* the first input's row count */
  INT64 threshold;		/* second-input rows to see before the bits are filled */
  volatile INT64 seen;		/* second-input rows seen so far (the owner's and the workers' batches) */

  UINT64 *bits;
  UINT32 mask;			/* number of bits - 1, a power of two minus one */

  HASH_SCAN_KEY *key;		/* the owner's */
  HASHJOIN_FETCH_INFO first;
  HASHJOIN_FETCH_INFO second;

  /* the checked input's key columns as plain column references in its output list (the hash join thread's, read
   * only): the row is checked before the projection, from these values. NULL: some key is computed - the row is
   * checked after the projection, from its serialized tuple. */
  REGU_VARIABLE **key_regus;
  DB_VALUE *key_cast;		/* the owner's: per key, a value not of its list column's domain is cast here */

  char *row_buf;		/* the owner's: the second input's row serialized for hjoin_fetch_key */
  int row_buf_size;

  INT64 keys;
  volatile INT64 tested;
  volatile INT64 skipped;
};

static INT64
hjoin_bloom_state (HJOIN_BLOOM * bloom)
{
  return ATOMIC_LOAD_64_ACQUIRE (&bloom->state);
}

/* off for every thread; the first reason given is the one kept */
static void
hjoin_bloom_turn_off (HJOIN_BLOOM * bloom, INT64 reason)
{
  INT64 cur = hjoin_bloom_state (bloom);

  while (cur != HJOIN_BLOOM_OFF)
    {
      if (ATOMIC_CAS (&bloom->state, cur, (INT64) HJOIN_BLOOM_OFF))
	{
	  bloom->off_reason = reason;
	  return;
	}
      cur = hjoin_bloom_state (bloom);
    }
}

/*
 * hjoin_bloom_find_key_regus () - the regu variables of the key columns in an input's output list, when every one is
 *				    a plain reference to a value the scan already holds (no evaluation to fetch it)
 *   return: false when a key column is computed or hidden, or the list does not hold it
 *   value_indexes(in): the key columns' positions in the input's list (hidden columns are not in the list)
 */
static bool
hjoin_bloom_find_key_regus (OUTPTR_LIST * outptr_list, const int *value_indexes, int key_cnt, REGU_VARIABLE ** regus)
{
  REGU_VARIABLE_LIST reg;
  int k, list_index;

  if (outptr_list == NULL)
    {
      return false;
    }
  for (k = 0; k < key_cnt; k++)
    {
      regus[k] = NULL;
      for (reg = outptr_list->valptrp, list_index = 0; reg != NULL; reg = reg->next)
	{
	  if (reg->value.flags & REGU_VARIABLE_HIDDEN_COLUMN)
	    {
	      continue;
	    }
	  if (list_index++ == value_indexes[k])
	    {
	      regus[k] = &reg->value;
	      break;
	    }
	}
      if (regus[k] == NULL)
	{
	  return false;
	}
      switch (regus[k]->type)
	{
	case TYPE_CONSTANT:
	  if (regus[k]->xasl != NULL)
	    {
	      /* a linked subquery computes it (a scalar subquery's value): fetching it here would run the subquery
	       * before the projection, and a failure's error would go with the check's er_clear */
	      return false;
	    }
	  break;
	case TYPE_ATTR_ID:
	case TYPE_POSITION:
	case TYPE_DBVAL:
	  break;
	default:
	  return false;
	}
      if (REGU_VARIABLE_IS_FLAGED (regus[k], REGU_VARIABLE_ANALYTIC_WINDOW)
	  || REGU_VARIABLE_IS_FLAGED (regus[k], REGU_VARIABLE_UPD_INS_LIST))
	{
	  return false;
	}
    }
  return true;
}

/* the checked input's key positions in its list */
static const int *
hjoin_bloom_second_value_indexes (const HJOIN_BLOOM * bloom)
{
  const QFILE_LIST_MERGE_INFO *merge_info = &bloom->hj_xasl->proc.hashjoin.merge_info;

  return bloom->first_is_outer ? merge_info->ls_inner_column : merge_info->ls_outer_column;
}

/* does the value already have the list column's domain, as the list would hand it back? */
static bool
hjoin_bloom_value_in_domain (const DB_VALUE * value, const TP_DOMAIN * domain)
{
  DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);

  if (type != TP_DOMAIN_TYPE (domain))
    {
      return false;
    }
  if (type == DB_TYPE_NUMERIC)
    {
      return value->domain.numeric_info.precision == domain->precision
	&& value->domain.numeric_info.scale == domain->scale;
    }
  if (TP_IS_CHAR_TYPE (type))
    {
      return db_get_string_collation (value) == domain->collation_id
	&& db_get_string_codeset (value) == TP_DOMAIN_CODESET (domain);
    }
  return true;
}

/*
 * hjoin_bloom_fetch_key_values () - hjoin_fetch_key for a row that is not serialized yet: the key values straight
 *				      from the row, brought to the list column's domain (as the list will hold
 *				      them) and then coerced as hjoin_fetch_key coerces what it reads from the list
 *   regus(in): the key columns' plain references (hjoin_bloom_find_key_regus)
 *   cast(in/out): per key, a value to cast into when the row's value is not of the list column's domain
 */
static int
hjoin_bloom_fetch_key_values (THREAD_ENTRY * thread_p, HASHJOIN_FETCH_INFO * fetch_info, REGU_VARIABLE ** regus,
			      DB_VALUE * cast, VAL_DESCR * vd, HASH_SCAN_KEY * key, bool * need_skip_next)
{
  TP_DOMAIN **domains = fetch_info->input->domains;
  TP_DOMAIN **coerce_domains = fetch_info->coerce_domains;
  DB_VALUE *value, pre_coerce_value;
  TP_DOMAIN_STATUS domain_status;
  int key_index;

  for (key_index = 0; key_index < key->val_count; key_index++)
    {
      pr_clear_value (key->values[key_index]);

      if (fetch_peek_dbval (thread_p, regus[key_index], vd, NULL, NULL, NULL, &value) != NO_ERROR)
	{
	  return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	}
      if (DB_IS_NULL (value))
	{
	  /* a NULL key never joins */
	  *need_skip_next = true;
	  return NO_ERROR;
	}
      if (!hjoin_bloom_value_in_domain (value, domains[key_index]))
	{
	  pr_clear_value (&cast[key_index]);
	  if (DB_VALUE_DOMAIN_TYPE (value) == TP_DOMAIN_TYPE (domains[key_index])
	      && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (domains[key_index])))
	    {
	      /* a string of the column's type: the projection writes its bytes as they are (it casts only another type
	       * or a NUMERIC of another precision / scale) and the list reads them back under the column's codeset and
	       * collation - relabel, do not convert */
	      pr_share_value (value, &cast[key_index]);
	      db_string_put_cs_and_collation (&cast[key_index], TP_DOMAIN_CODESET (domains[key_index]),
					      TP_DOMAIN_COLLATION (domains[key_index]));
	    }
	  else if ((domain_status = tp_value_coerce (value, &cast[key_index], domains[key_index])) != DOMAIN_COMPATIBLE)
	    {
	      tp_domain_status_er_set (domain_status, ARG_FILE_LINE, value, domains[key_index]);
	      return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	    }
	  value = &cast[key_index];
	}

      if (fetch_info->need_coerce_domains && coerce_domains[key_index] != NULL
	  && coerce_domains[key_index] != domains[key_index])
	{
	  /* as hjoin_fetch_key does after it reads the value from the list */
	  pre_coerce_value = *value;
	  if (coerce_domains[key_index]->type->id == DB_TYPE_NUMERIC
	      && pre_coerce_value.domain.general_info.type == DB_TYPE_NUMERIC
	      && coerce_domains[key_index]->precision == DB_DEFAULT_NUMERIC_PRECISION
	      && pre_coerce_value.domain.numeric_info.precision != DB_DEFAULT_NUMERIC_PRECISION
	      && pre_coerce_value.domain.numeric_info.scale < 0)
	    {
	      pre_coerce_value.domain.numeric_info.precision = DB_HJOIN_NUMERIC_PRECISION_DEFERRED;
	    }
	  domain_status = tp_value_coerce (&pre_coerce_value, key->values[key_index], coerce_domains[key_index]);
	  if (domain_status != DOMAIN_COMPATIBLE)
	    {
	      tp_domain_status_er_set (domain_status, ARG_FILE_LINE, &pre_coerce_value, coerce_domains[key_index]);
	      return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	    }
	}
      else
	{
	  /* read without a copy, as hjoin_fetch_key peeks into the list's page */
	  pr_share_value (value, key->values[key_index]);
	}
    }
  return NO_ERROR;
}

static inline void
hjoin_bloom_positions (UINT32 hash, UINT32 * h1, UINT32 * h2)
{
  /* double hashing over one 32-bit hash: the second stride is odd, so with a power-of-two table the probes
   * visit distinct positions. It is drawn from the hash multiplied by the golden ratio constant, not from the hash
   * itself: a plain rotation shares the low bits with the first probe and doubled the false positive rate. */
  UINT32 mixed = hash * 0x9E3779B1U;

  *h1 = hash;
  *h2 = ((mixed >> 16) | (mixed << 16)) | 1;
}

static void
hjoin_bloom_add (HJOIN_BLOOM * bloom, UINT32 hash)
{
  UINT32 h1, h2, pos;
  int i;

  hjoin_bloom_positions (hash, &h1, &h2);
  for (i = 0; i < HJOIN_BLOOM_HASHES; i++)
    {
      pos = (h1 + (UINT32) i * h2) & bloom->mask;
      bloom->bits[pos >> 6] |= ((UINT64) 1) << (pos & 63);
    }
}

static bool
hjoin_bloom_may_contain (const HJOIN_BLOOM * bloom, UINT32 hash)
{
  UINT32 h1, h2, pos;
  int i;

  hjoin_bloom_positions (hash, &h1, &h2);
  for (i = 0; i < HJOIN_BLOOM_HASHES; i++)
    {
      pos = (h1 + (UINT32) i * h2) & bloom->mask;
      if ((bloom->bits[pos >> 6] & (((UINT64) 1) << (pos & 63))) == 0)
	{
	  return false;
	}
    }
  return true;
}

/*
 * hjoin_bloom_attach () - attach a Bloom filter to the hash join input about to be materialized second
 *   hj_xasl(in): the hash join
 *   second_xasl(in): the input the aptr loop is about to execute; the other one is already materialized
 *
 * Nothing is attached unless the parameter is on, the second input is a side the join does not preserve, the
 * first input has finished (XASL_SUCCESS: a list still being written by a parallel worker would give the filter
 * false negatives) with rows (an empty one already skips the second) and not too many, and the second input writes
 * its rows straight to its list (no grouping, analytic, top-n, limit, ORDERBY_NUM () or distinct between the row and
 * the list: dropping a row there would change which rows those keep or how they are numbered). Nor when its list
 * comes from the subquery result cache (another execution's) or it is the SELECT of an UPDATE / DELETE (its rows
 * carry the OIDs to change).
 */
/* can the second input take a filter at all: a side the join does not preserve, rows straight to its list */
static bool
hjoin_bloom_second_eligible (const HASHJOIN_PROC_NODE * proc, const XASL_NODE * second_xasl, bool first_is_outer)
{
  JOIN_TYPE join_type = proc->merge_info.join_type;

  if (!(join_type == JOIN_INNER || (join_type == JOIN_LEFT && first_is_outer)
	|| (join_type == JOIN_RIGHT && !first_is_outer)))
    {
      /* the second input is a preserved side: its unmatched rows are part of the result */
      return false;
    }

  if (second_xasl->type != BUILDLIST_PROC || second_xasl->proc.buildlist.groupby_list != NULL
      || second_xasl->proc.buildlist.a_eval_list != NULL || second_xasl->proc.buildlist.g_hash_eligible
      || second_xasl->limit_row_count != NULL || second_xasl->limit_offset != NULL
      || second_xasl->option == Q_DISTINCT || second_xasl->connect_by_ptr != NULL
      || second_xasl->ordbynum_pred != NULL || second_xasl->ordbynum_val != NULL || second_xasl->orderby_limit != NULL
      || second_xasl->selected_upd_list != NULL || second_xasl->upd_del_class_cnt > 0
      || second_xasl->sub_xasl_id != NULL || XASL_IS_FLAGED (second_xasl, XASL_TOP_MOST_XASL))
    {
      return false;
    }
  return true;
}

/*
 * hjoin_bloom_attach () - attach a Bloom filter to the inputs of a hash join before they run
 *   hj_xasl(in): a hash join whose aptr loop is about to run its inputs (inline or as parallel jobs)
 *
 * The input its aptr list runs first gives the keys, the other one is checked. Nothing is attached unless the
 * parameter is on and the checked input is a side the join does not preserve and writes its rows straight to its
 * list (no grouping, analytic, top-n, limit, ORDERBY_NUM () or distinct between the row and the list: dropping a
 * row there would change which rows those keep or how they are numbered), nor when its list comes from the
 * subquery result cache (another execution's) or it is the SELECT of an UPDATE / DELETE (its rows carry the OIDs
 * to change). Whether the bits are ever filled is decided as the inputs run.
 */
void
hjoin_bloom_attach (THREAD_ENTRY * thread_p, XASL_NODE * hj_xasl)
{
  HASHJOIN_PROC_NODE *proc;
  XASL_NODE *xptr, *first_xasl = NULL, *second_xasl = NULL;
  HJOIN_BLOOM *bloom;

  if (hj_xasl == NULL || hj_xasl->type != HASHJOIN_PROC)
    {
      return;
    }
  proc = &hj_xasl->proc.hashjoin;
  /* the trace of this execution starts empty (hjoin_bloom_finish fills it) */
  memset (&proc->bloom_stats, 0, sizeof (proc->bloom_stats));

  if (!prm_get_bool_value (PRM_ID_HASH_JOIN_BLOOM_FILTER))
    {
      return;
    }

  for (xptr = hj_xasl->aptr_list; xptr != NULL; xptr = xptr->next)
    {
      if (xptr != proc->outer.xasl && xptr != proc->inner.xasl)
	{
	  continue;
	}
      if (first_xasl == NULL)
	{
	  first_xasl = xptr;
	}
      else
	{
	  second_xasl = xptr;
	  break;
	}
    }
  if (first_xasl == NULL || second_xasl == NULL || XASL_IS_FLAGED (first_xasl, XASL_LINK_TO_REGU_VARIABLE)
      || XASL_IS_FLAGED (second_xasl, XASL_LINK_TO_REGU_VARIABLE) || first_xasl->hj_bloom != NULL
      || second_xasl->hj_bloom != NULL || !IS_XASL_INITIAL_STATUS (first_xasl->status)
      || !IS_XASL_INITIAL_STATUS (second_xasl->status)
      || !hjoin_bloom_second_eligible (proc, second_xasl, first_xasl == proc->outer.xasl))
    {
      return;
    }

  bloom = (HJOIN_BLOOM *) db_private_alloc (thread_p, sizeof (HJOIN_BLOOM));
  if (bloom == NULL)
    {
      er_clear ();		/* the filter is an optimization: run without it */
      return;
    }
  memset (bloom, 0, sizeof (HJOIN_BLOOM));
  bloom->hj_xasl = hj_xasl;
  bloom->first_xasl = first_xasl;
  bloom->owner_xasl = second_xasl;
  bloom->first_is_outer = (first_xasl == proc->outer.xasl);
  bloom->state = HJOIN_BLOOM_WAITING;
  bloom->threshold = HJOIN_BLOOM_MIN_SECOND_ROWS;	/* raised when the first input's row count is known */

  bloom->key_regus =
    (REGU_VARIABLE **) db_private_alloc (thread_p, proc->merge_info.ls_column_cnt * sizeof (REGU_VARIABLE *));
  if (bloom->key_regus == NULL)
    {
      er_clear ();		/* a failed allocation only means the later check */
    }
  else if (!hjoin_bloom_find_key_regus (second_xasl->outptr_list, hjoin_bloom_second_value_indexes (bloom),
					proc->merge_info.ls_column_cnt, bloom->key_regus))
    {
      /* a computed key: checked after the projection */
      db_private_free_and_init (thread_p, bloom->key_regus);
    }

  first_xasl->hj_bloom = bloom;
  second_xasl->hj_bloom = bloom;
}

/* the owner's key, buffer and bits, freed on the owner's thread */
static void
hjoin_bloom_free_owned (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom)
{
  if (bloom->key != NULL)
    {
      qdata_free_hscan_key (thread_p, bloom->key, bloom->hj_xasl->proc.hashjoin.merge_info.ls_column_cnt);
      bloom->key = NULL;
    }
  if (bloom->bits != NULL)
    {
      db_private_free_and_init (thread_p, bloom->bits);
    }
  if (bloom->row_buf != NULL)
    {
      db_private_free_and_init (thread_p, bloom->row_buf);
    }
  if (bloom->key_cast != NULL)
    {
      int k;

      for (k = 0; k < bloom->hj_xasl->proc.hashjoin.merge_info.ls_column_cnt; k++)
	{
	  pr_clear_value (&bloom->key_cast[k]);
	}
      db_private_free_and_init (thread_p, bloom->key_cast);
    }
}

/*
 * hjoin_bloom_input_done () - an input of a hash join with a filter has finished running, on the thread that ran it
 *   xasl(in): the input; its list is final (after a parallel job's own list hand-over)
 *   success(in): it ran to the end
 *
 * The first input publishes its row count, which sets the threshold and lets the bits be filled. The checked input
 * frees what its thread allocated, and records that the first input was not done in time when the bits were never
 * filled for that reason.
 */
void
hjoin_bloom_input_done (THREAD_ENTRY * thread_p, XASL_NODE * xasl, bool success)
{
  HJOIN_BLOOM *bloom = xasl->hj_bloom;

  if (bloom == NULL)
    {
      return;
    }

  if (xasl == bloom->first_xasl)
    {
      if (!success || xasl->list_id == NULL || xasl->list_id->tuple_cnt <= 0
	  || xasl->list_id->tuple_cnt > HJOIN_BLOOM_MAX_FIRST_ROWS)
	{
	  hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_OFF_FIRST_ROWS);
	}
      bloom->first_rows = xasl->list_id != NULL ? xasl->list_id->tuple_cnt : 0;
      bloom->threshold = MAX ((INT64) HJOIN_BLOOM_MIN_SECOND_ROWS, bloom->first_rows / HJOIN_BLOOM_FIRST_PER_SECOND);
      /* the row count and the list before the flag */
      ATOMIC_STORE_64_RELEASE (&bloom->first_done, (INT64) 1);
    }
  else if (xasl == bloom->owner_xasl)
    {
      if (hjoin_bloom_state (bloom) == HJOIN_BLOOM_WAITING && ATOMIC_LOAD_64_ACQUIRE (&bloom->first_done) == 0)
	{
	  hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_FIRST_LATE);
	}
      if (bloom->owner_thread == thread_p)
	{
	  hjoin_bloom_free_owned (thread_p, bloom);
	}
    }
}

/* the owner, before the bits are filled: the common domains from both lists and the owner's key */
static bool
hjoin_bloom_setup (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom)
{
  HASHJOIN_PROC_NODE *proc = &bloom->hj_xasl->proc.hashjoin;
  HASHJOIN_MANAGER manager;
  HASHJOIN_DOMAIN_INFO *domain_info = &proc->domain_info;

  bloom->set_up = true;

  if (proc->outer.xasl->list_id->type_list.type_cnt == 0 || proc->inner.xasl->list_id->type_list.type_cnt == 0)
    {
      return false;
    }

  memset (&manager, 0, sizeof (HASHJOIN_MANAGER));
  manager.merge_info = &proc->merge_info;
  manager.outer = &proc->outer;
  manager.inner = &proc->inner;
  if (hjoin_init_domain_info (thread_p, &manager, domain_info) != NO_ERROR)
    {
      return false;
    }

  bloom->key = qdata_alloc_hscan_key (thread_p, proc->merge_info.ls_column_cnt, true);
  if (bloom->key == NULL)
    {
      return false;
    }
  if (bloom->key_regus != NULL)
    {
      int k;

      bloom->key_cast = (DB_VALUE *) db_private_alloc (thread_p, proc->merge_info.ls_column_cnt * sizeof (DB_VALUE));
      if (bloom->key_cast == NULL)
	{
	  return false;
	}
      for (k = 0; k < proc->merge_info.ls_column_cnt; k++)
	{
	  db_make_null (&bloom->key_cast[k]);
	}
    }

  bloom->first.input = bloom->first_is_outer ? &domain_info->outer : &domain_info->inner;
  bloom->first.coerce_domains = domain_info->coerce_domains;
  bloom->first.need_coerce_domains = domain_info->need_coerce_domains;
  bloom->second.input = bloom->first_is_outer ? &domain_info->inner : &domain_info->outer;
  bloom->second.coerce_domains = domain_info->coerce_domains;
  bloom->second.need_coerce_domains = domain_info->need_coerce_domains;
  return true;
}

/* set up and hash every first-input key into the bits; false when any of it fails (a filter missing some of the
 * first input's keys would drop rows that join) */
static bool
hjoin_bloom_fill_bits (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom)
{
  QFILE_LIST_ID *first_list_id = bloom->first_xasl->list_id;
  QFILE_LIST_SCAN_ID scan_id;
  // *INDENT-OFF*
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  // *INDENT-ON*
  UINT64 nbits;
  bool need_skip_next = false;
  SCAN_CODE scan_code;

  if (!bloom->set_up && !hjoin_bloom_setup (thread_p, bloom))
    {
      return false;
    }

  /* power of two >= keys * bits-per-key, at least one word */
  nbits = 64;
  while (nbits < (UINT64) first_list_id->tuple_cnt * HJOIN_BLOOM_BITS_PER_KEY)
    {
      nbits <<= 1;
    }
  bloom->mask = (UINT32) (nbits - 1);
  bloom->bits = (UINT64 *) db_private_alloc (thread_p, (size_t) (nbits / 64) * sizeof (UINT64));
  if (bloom->bits == NULL)
    {
      return false;
    }
  memset (bloom->bits, 0, (size_t) (nbits / 64) * sizeof (UINT64));

  if (qfile_open_list_scan (first_list_id, &scan_id) != NO_ERROR)
    {
      return false;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      if (hjoin_fetch_key (thread_p, &bloom->first, &tuple_record, bloom->key, NULL, &need_skip_next) != NO_ERROR)
	{
	  scan_code = S_ERROR;
	  break;
	}
      if (need_skip_next)
	{
	  /* a NULL key never joins */
	  need_skip_next = false;
	  continue;
	}
      hjoin_bloom_add (bloom, qdata_hash_scan_key (bloom->key, UINT_MAX, HASH_METH_IN_MEM));
      bloom->keys++;
    }
  qfile_close_scan (thread_p, &scan_id);

  /* the error stack frame of hjoin_bloom_fill starts clear: an error id here was set by the fill itself */
  return scan_code == S_END && er_errid () == NO_ERROR;
}

/* the owner, once the first input is done and the second has given threshold rows: fill the bits and publish the
 * filter, or turn it off. The fill runs in an error stack frame of its own, so what it may set is dropped and an
 * error the query had already set stays as it was. */
static void
hjoin_bloom_fill (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom)
{
  bool filled;

  assert (bloom->state == HJOIN_BLOOM_WAITING && bloom->first_done != 0);

  er_stack_push ();
  filled = hjoin_bloom_fill_bits (thread_p, bloom);
  er_stack_pop ();

  if (!filled)
    {
      hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_OFF_FAILED);
      return;
    }
  /* the bits before the state: a worker that reads READY reads them filled */
  ATOMIC_STORE_64_RELEASE (&bloom->state, (INT64) HJOIN_BLOOM_READY);
}

/* fill when the first input is done and the second has given threshold rows */
static void
hjoin_bloom_fill_if_due (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom, INT64 seen)
{
  if (hjoin_bloom_state (bloom) == HJOIN_BLOOM_WAITING && ATOMIC_LOAD_64_ACQUIRE (&bloom->first_done) != 0
      && seen >= bloom->threshold)
    {
      hjoin_bloom_fill (thread_p, bloom);
    }
}

/* is thread_p, running xasl, the owner? Binds the owner at its first use. */
static bool
hjoin_bloom_is_owner (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom, XASL_NODE * xasl)
{
  if (bloom == NULL || bloom->owner_xasl != xasl)
    {
      return false;
    }
  if (bloom->owner_thread == NULL)
    {
      bloom->owner_thread = thread_p;
    }
  return bloom->owner_thread == thread_p;
}

/* the decision on a key already fetched */
static bool
hjoin_bloom_key_absent (HJOIN_BLOOM * bloom, HASH_SCAN_KEY * key)
{
  return !hjoin_bloom_may_contain (bloom, qdata_hash_scan_key (key, UINT_MAX, HASH_METH_IN_MEM));
}

/* the decision on a row before its projection, from its key columns' values */
static bool
hjoin_bloom_check_values (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom, REGU_VARIABLE ** regus, DB_VALUE * cast,
			  VAL_DESCR * vd, HASH_SCAN_KEY * key, bool * failed)
{
  bool need_skip_next = false;

  if (hjoin_bloom_fetch_key_values (thread_p, &bloom->second, regus, cast, vd, key, &need_skip_next) != NO_ERROR)
    {
      er_clear ();
      *failed = true;
      return false;
    }
  return need_skip_next || hjoin_bloom_key_absent (bloom, key);
}

/* the decision on one serialized second-input row; key is the caller's (the owner's or a worker's). The bits and
 * the fetch info are only read. */
static bool
hjoin_bloom_check (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom, HASH_SCAN_KEY * key, QFILE_TUPLE_RECORD * record,
		   bool * failed)
{
  bool need_skip_next = false;

  if (hjoin_fetch_key (thread_p, &bloom->second, record, key, NULL, &need_skip_next) != NO_ERROR)
    {
      er_clear ();
      *failed = true;
      return false;
    }

  /* a NULL key, or a key no first-input row has */
  return need_skip_next || !hjoin_bloom_may_contain (bloom, qdata_hash_scan_key (key, UINT_MAX, HASH_METH_IN_MEM));
}

/* a checker's sample is in: too few rows dropped turns the filter off for everyone */
static void
hjoin_bloom_judge_sample (HJOIN_BLOOM * bloom, INT64 tested, INT64 skipped)
{
  if (tested == HJOIN_BLOOM_SAMPLE_ROWS && skipped * 100 < tested * HJOIN_BLOOM_MIN_SKIP_PERCENT)
    {
      hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_OFF_LOW_SKIP);
    }
}

/* the owner's view: NULL unless xasl is the checked input, on the thread running it, and the filter is checking.
 * Counts the row and fills the bits when they are due. */
static HJOIN_BLOOM *
hjoin_bloom_owner_row (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  HJOIN_BLOOM *bloom = xasl->hj_bloom;

  if (!hjoin_bloom_is_owner (thread_p, bloom, xasl))
    {
      return NULL;
    }
  hjoin_bloom_fill_if_due (thread_p, bloom, ATOMIC_INC_64 (&bloom->seen, 1));
  return hjoin_bloom_state (bloom) == HJOIN_BLOOM_READY ? bloom : NULL;
}

static bool
hjoin_bloom_skip_record (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom, QFILE_TUPLE_RECORD * record)
{
  bool failed = false;
  bool skip = hjoin_bloom_check (thread_p, bloom, bloom->key, record, &failed);

  if (failed)
    {
      hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_OFF_FAILED);
      return false;
    }
  bloom->tested++;
  if (skip)
    {
      bloom->skipped++;
    }
  hjoin_bloom_judge_sample (bloom, bloom->tested, bloom->skipped);
  return skip;
}

/*
 * hjoin_bloom_skip_row () - should the row xasl is about to project be left out of its list? Checked before the
 *			      projection from the key columns' values, when every key is a plain column reference.
 *   vd(in): the value descriptor the projection would use
 */
bool
hjoin_bloom_skip_row (THREAD_ENTRY * thread_p, XASL_NODE * xasl, VAL_DESCR * vd)
{
  HJOIN_BLOOM *bloom = xasl->hj_bloom;
  bool failed = false;
  bool skip;

  if (bloom == NULL || bloom->key_regus == NULL)
    {
      return false;
    }
  bloom = hjoin_bloom_owner_row (thread_p, xasl);
  if (bloom == NULL)
    {
      return false;
    }
  skip = hjoin_bloom_check_values (thread_p, bloom, bloom->key_regus, bloom->key_cast, vd, bloom->key, &failed);
  if (failed)
    {
      hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_OFF_FAILED);
      return false;
    }
  bloom->tested++;
  if (skip)
    {
      bloom->skipped++;
    }
  hjoin_bloom_judge_sample (bloom, bloom->tested, bloom->skipped);
  return skip;
}

/*
 * hjoin_bloom_skip_descr () - should the row in xasl's tuple descriptor be left out of its list?
 *   xasl(in): a BUILDLIST xasl whose qexec_generate_tuple_descriptor succeeded for the current row
 *
 * The descriptor's values are already evaluated; they are serialized as they would be written to the list, so
 * hjoin_fetch_key reads the key exactly as the join will.
 */
bool
hjoin_bloom_skip_descr (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  HJOIN_BLOOM *bloom = (xasl->hj_bloom != NULL && xasl->hj_bloom->key_regus != NULL)
    ? NULL : hjoin_bloom_owner_row (thread_p, xasl);
  QFILE_TUPLE_DESCRIPTOR *descr;
  // *INDENT-OFF*
  QFILE_TUPLE_RECORD record = QFILE_TUPLE_RECORD_INITIALIZER;
  // *INDENT-ON*

  if (bloom == NULL)
    {
      return false;
    }

  descr = &xasl->list_id->tpl_descr;
  if (bloom->row_buf_size < descr->tpl_size)
    {
      char *buf = (char *) db_private_realloc (thread_p, bloom->row_buf, descr->tpl_size);
      if (buf == NULL)
	{
	  er_clear ();
	  hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_OFF_FAILED);
	  return false;
	}
      bloom->row_buf = buf;
      bloom->row_buf_size = descr->tpl_size;
    }

  if (qfile_tuple_fill_from_values (&xasl->list_id->type_list, descr->f_valp, descr->f_len, descr->f_cnt,
				    bloom->row_buf, descr->tpl_size, descr->has_null) != NO_ERROR)
    {
      er_clear ();
      hjoin_bloom_turn_off (bloom, HASHJOIN_BLOOM_OFF_FAILED);
      return false;
    }

  /* hjoin_fetch_key finds the columns through the record's layout binding: bind it to the list the row is for */
  qfile_slot_set_tuple_ptr_and_layout (&record, bloom->row_buf, 0 /* PEEK: the buffer is the filter's */ ,
				       &xasl->list_id->type_list);
  return hjoin_bloom_skip_record (thread_p, bloom, &record);
}

/* the same for a row already serialized into a tuple (the big-record / set path) */
bool
hjoin_bloom_skip_tuple (THREAD_ENTRY * thread_p, XASL_NODE * xasl, char *tuple)
{
  HJOIN_BLOOM *bloom;
  // *INDENT-OFF*
  QFILE_TUPLE_RECORD record = QFILE_TUPLE_RECORD_INITIALIZER;
  // *INDENT-ON*

  if (tuple == NULL || (xasl->hj_bloom != NULL && xasl->hj_bloom->key_regus != NULL))
    {
      /* no tuple, or the row was checked before its projection (hjoin_bloom_skip_row) */
      return false;
    }
  bloom = hjoin_bloom_owner_row (thread_p, xasl);
  if (bloom == NULL)
    {
      return false;
    }
  qfile_slot_set_tuple_ptr_and_layout (&record, tuple, 0 /* PEEK: the caller's tuple */ , &xasl->list_id->type_list);
  return hjoin_bloom_skip_record (thread_p, bloom, &record);
}

/*
 * hjoin_bloom_prepare () - before a parallel scan of xasl starts its workers: the filter they are to share, or NULL
 *			     when xasl is not the checked input on this thread or the filter is off (the bits are filled
 *			     later, by hjoin_bloom_poll)
 */
HJOIN_BLOOM *
hjoin_bloom_prepare (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  HJOIN_BLOOM *bloom = (xasl != NULL) ? xasl->hj_bloom : NULL;

  if (!hjoin_bloom_is_owner (thread_p, bloom, xasl))
    {
      return NULL;
    }
  return hjoin_bloom_state (bloom) == HJOIN_BLOOM_OFF ? NULL : bloom;
}

/*
 * hjoin_bloom_poll () - the owner, while it waits for a parallel scan's workers: fill the bits once they are due
 */
void
hjoin_bloom_poll (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom)
{
  if (bloom == NULL || bloom->owner_thread != thread_p)
    {
      return;
    }
  hjoin_bloom_fill_if_due (thread_p, bloom, ATOMIC_LOAD_64 (&bloom->seen));
}

/* one parallel scan worker's view of a shared filter */
struct hjoin_bloom_worker
{
  HJOIN_BLOOM *bloom;		/* shared: its bits only read, its state read and turned off */
  HASH_SCAN_KEY *key;		/* this worker's */
  char *row_buf;
  int row_buf_size;
  bool disabled;		/* this worker gave up (a failed key or buffer): its rows go through */
  bool ready;			/* the filter's state as of the last batch: checking */
  REGU_VARIABLE **key_regus;	/* its clone's key columns (when the owner checks before the projection) */
  DB_VALUE *key_cast;
  INT64 seen_unflushed;
  INT64 tested;
  INT64 skipped;
};

/* a worker's state, on the worker's own heap; NULL lets every row through */
HJOIN_BLOOM_WORKER *
hjoin_bloom_worker_begin (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom, OUTPTR_LIST * outptr_list)
{
  HJOIN_BLOOM_WORKER *worker;

  if (bloom == NULL)
    {
      return NULL;
    }
  worker = (HJOIN_BLOOM_WORKER *) db_private_alloc (thread_p, sizeof (HJOIN_BLOOM_WORKER));
  if (worker == NULL)
    {
      er_clear ();
      return NULL;
    }
  memset (worker, 0, sizeof (HJOIN_BLOOM_WORKER));
  worker->bloom = bloom;
  worker->key = qdata_alloc_hscan_key (thread_p, bloom->hj_xasl->proc.hashjoin.merge_info.ls_column_cnt, true);
  if (worker->key == NULL)
    {
      er_clear ();
      db_private_free (thread_p, worker);
      return NULL;
    }
  if (bloom->key_regus != NULL)
    {
      int key_cnt = bloom->hj_xasl->proc.hashjoin.merge_info.ls_column_cnt, k;

      worker->key_regus = (REGU_VARIABLE **) db_private_alloc (thread_p, key_cnt * sizeof (REGU_VARIABLE *));
      worker->key_cast = (DB_VALUE *) db_private_alloc (thread_p, key_cnt * sizeof (DB_VALUE));
      if (worker->key_regus == NULL || worker->key_cast == NULL
	  || !hjoin_bloom_find_key_regus (outptr_list, hjoin_bloom_second_value_indexes (bloom), key_cnt,
					  worker->key_regus))
	{
	  /* the clone's list does not match the owner's: this worker lets its rows through */
	  er_clear ();
	  worker->disabled = true;
	}
      if (worker->key_cast != NULL)
	{
	  for (k = 0; k < key_cnt; k++)
	    {
	      db_make_null (&worker->key_cast[k]);
	    }
	}
    }
  worker->ready = hjoin_bloom_state (bloom) == HJOIN_BLOOM_READY;
  return worker;
}

static bool hjoin_bloom_worker_row (HJOIN_BLOOM_WORKER * worker);

/* a worker's row before its projection (see hjoin_bloom_skip_row) */
bool
hjoin_bloom_worker_skip_row (THREAD_ENTRY * thread_p, HJOIN_BLOOM_WORKER * worker, VAL_DESCR * vd)
{
  bool failed = false;
  bool skip;

  if (worker == NULL || worker->bloom->key_regus == NULL || !hjoin_bloom_worker_row (worker))
    {
      return false;
    }
  skip = hjoin_bloom_check_values (thread_p, worker->bloom, worker->key_regus, worker->key_cast, vd, worker->key,
				   &failed);
  if (failed)
    {
      worker->disabled = true;
      return false;
    }
  worker->tested++;
  if (skip)
    {
      worker->skipped++;
    }
  hjoin_bloom_judge_sample (worker->bloom, worker->tested, worker->skipped);
  return skip;
}

/* count the row; every batch hand the count over and look at the filter's state again. True when this row is to
 * be checked. */
static bool
hjoin_bloom_worker_row (HJOIN_BLOOM_WORKER * worker)
{
  if (++worker->seen_unflushed >= HJOIN_BLOOM_SEEN_BATCH)
    {
      ATOMIC_INC_64 (&worker->bloom->seen, worker->seen_unflushed);
      worker->seen_unflushed = 0;
      worker->ready = hjoin_bloom_state (worker->bloom) == HJOIN_BLOOM_READY;
    }
  return worker->ready && !worker->disabled;
}

static bool
hjoin_bloom_worker_skip_record (THREAD_ENTRY * thread_p, HJOIN_BLOOM_WORKER * worker, QFILE_TUPLE_RECORD * record)
{
  bool failed = false;
  bool skip;

  skip = hjoin_bloom_check (thread_p, worker->bloom, worker->key, record, &failed);
  if (failed)
    {
      worker->disabled = true;
      return false;
    }
  worker->tested++;
  if (skip)
    {
      worker->skipped++;
    }
  hjoin_bloom_judge_sample (worker->bloom, worker->tested, worker->skipped);
  return skip;
}

/* the row in the worker's list's tuple descriptor (see hjoin_bloom_skip_descr) */
bool
hjoin_bloom_worker_skip_descr (THREAD_ENTRY * thread_p, HJOIN_BLOOM_WORKER * worker, QFILE_LIST_ID * list_id)
{
  QFILE_TUPLE_DESCRIPTOR *descr = &list_id->tpl_descr;
  // *INDENT-OFF*
  QFILE_TUPLE_RECORD record = QFILE_TUPLE_RECORD_INITIALIZER;
  // *INDENT-ON*

  if (worker == NULL || worker->bloom->key_regus != NULL || !hjoin_bloom_worker_row (worker))
    {
      return false;
    }
  if (worker->row_buf_size < descr->tpl_size)
    {
      char *buf = (char *) db_private_realloc (thread_p, worker->row_buf, descr->tpl_size);
      if (buf == NULL)
	{
	  er_clear ();
	  worker->disabled = true;
	  return false;
	}
      worker->row_buf = buf;
      worker->row_buf_size = descr->tpl_size;
    }
  if (qfile_tuple_fill_from_values (&list_id->type_list, descr->f_valp, descr->f_len, descr->f_cnt, worker->row_buf,
				    descr->tpl_size, descr->has_null) != NO_ERROR)
    {
      er_clear ();
      worker->disabled = true;
      return false;
    }
  qfile_slot_set_tuple_ptr_and_layout (&record, worker->row_buf, 0 /* PEEK: the buffer is the worker's */ ,
				       &list_id->type_list);
  return hjoin_bloom_worker_skip_record (thread_p, worker, &record);
}

/* a row the worker already serialized into a tuple for its list (the big-record / set path) */
bool
hjoin_bloom_worker_skip_tuple (THREAD_ENTRY * thread_p, HJOIN_BLOOM_WORKER * worker, QFILE_LIST_ID * list_id,
			       char *tuple)
{
  // *INDENT-OFF*
  QFILE_TUPLE_RECORD record = QFILE_TUPLE_RECORD_INITIALIZER;
  // *INDENT-ON*

  if (worker == NULL || tuple == NULL || worker->bloom->key_regus != NULL || !hjoin_bloom_worker_row (worker))
    {
      return false;
    }
  qfile_slot_set_tuple_ptr_and_layout (&record, tuple, 0 /* PEEK: the caller's tuple */ , &list_id->type_list);
  return hjoin_bloom_worker_skip_record (thread_p, worker, &record);
}

/* the worker is done: its counts go to the shared filter (other workers may end at the same time) */
void
hjoin_bloom_worker_end (THREAD_ENTRY * thread_p, HJOIN_BLOOM_WORKER * worker)
{
  if (worker == NULL)
    {
      return;
    }
  ATOMIC_INC_64 (&worker->bloom->seen, worker->seen_unflushed);
  ATOMIC_INC_64 (&worker->bloom->tested, worker->tested);
  ATOMIC_INC_64 (&worker->bloom->skipped, worker->skipped);
  qdata_free_hscan_key (thread_p, worker->key, worker->bloom->hj_xasl->proc.hashjoin.merge_info.ls_column_cnt);
  if (worker->row_buf != NULL)
    {
      db_private_free (thread_p, worker->row_buf);
    }
  if (worker->key_cast != NULL)
    {
      int k;

      for (k = 0; k < worker->bloom->hj_xasl->proc.hashjoin.merge_info.ls_column_cnt; k++)
	{
	  pr_clear_value (&worker->key_cast[k]);
	}
      db_private_free (thread_p, worker->key_cast);
    }
  if (worker->key_regus != NULL)
    {
      db_private_free (thread_p, worker->key_regus);
    }
  db_private_free (thread_p, worker);
}

/* unlink the filter from both inputs and free it - the hash join's thread; whatever the owner allocated is freed
 * here only when the owner is this thread (otherwise the owner freed it in hjoin_bloom_input_done) */
static void
hjoin_bloom_free (THREAD_ENTRY * thread_p, HJOIN_BLOOM * bloom)
{
  if (bloom->owner_thread == NULL || bloom->owner_thread == thread_p)
    {
      hjoin_bloom_free_owned (thread_p, bloom);
    }
  if (bloom->first_xasl->hj_bloom == bloom)
    {
      bloom->first_xasl->hj_bloom = NULL;
    }
  if (bloom->owner_xasl->hj_bloom == bloom)
    {
      bloom->owner_xasl->hj_bloom = NULL;
    }
  if (bloom->key_regus != NULL)
    {
      db_private_free (thread_p, bloom->key_regus);
    }
  db_private_free (thread_p, bloom);
}

/* an error path's clear of either input: the filter goes with the checked one */
void
hjoin_bloom_release (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  HJOIN_BLOOM *bloom = xasl->hj_bloom;

  if (bloom == NULL)
    {
      return;
    }
  if (xasl != bloom->owner_xasl && xasl != bloom->first_xasl)
    {
      /* a cloned node carrying the original's pointer */
      xasl->hj_bloom = NULL;
      return;
    }
  hjoin_bloom_free (thread_p, bloom);
}

/*
 * hjoin_bloom_finish () - both inputs of the hash join are done (inline, or every parallel job joined): keep what
 *			    the filter did for trace and free it, on the hash join's thread
 */
void
hjoin_bloom_finish (THREAD_ENTRY * thread_p, XASL_NODE * hj_xasl)
{
  HASHJOIN_PROC_NODE *proc;
  HJOIN_BLOOM *bloom;
  HASHJOIN_BLOOM_STATS *stats;

  if (hj_xasl == NULL || hj_xasl->type != HASHJOIN_PROC)
    {
      return;
    }
  proc = &hj_xasl->proc.hashjoin;
  bloom = proc->outer.xasl != NULL ? proc->outer.xasl->hj_bloom : NULL;
  if (bloom == NULL && proc->inner.xasl != NULL)
    {
      bloom = proc->inner.xasl->hj_bloom;
    }
  if (bloom == NULL || bloom->hj_xasl != hj_xasl)
    {
      return;
    }

  stats = &proc->bloom_stats;
  stats->attached = true;
  if (bloom->state == HJOIN_BLOOM_OFF)
    {
      stats->state = (int) bloom->off_reason;
    }
  else
    {
      if (bloom->state == HJOIN_BLOOM_READY)
	{
	  stats->state = (int) HASHJOIN_BLOOM_CHECKED;
	}
      else if (bloom->first_done != 0 && bloom->seen >= bloom->threshold)
	{
	  /* due, but the second input ended before the owner got to fill it (a parallel scan's last batch) */
	  stats->state = (int) HASHJOIN_BLOOM_ENDED_FIRST;
	}
      else
	{
	  stats->state = (int) HASHJOIN_BLOOM_NOT_BUILT;
	}
    }
  stats->keys = bloom->keys;
  stats->threshold = bloom->threshold;
  stats->seen = bloom->seen;
  stats->tested = bloom->tested;
  stats->skipped = bloom->skipped;

  hjoin_bloom_free (thread_p, bloom);
}
