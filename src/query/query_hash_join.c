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

/**
 * Struct, Enum & Typedef Definitions
 */

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

typedef struct hashjoin_fetch_info HASHJOIN_FETCH_INFO;
typedef struct hashjoin_fetch_info HJ_FETCH_INFO;
struct hashjoin_fetch_info
{
  HJ_INPUT_DOMAIN_INFO *input;

  /* The common domains between the domains of values used in the build and probe inputs. */
  TP_DOMAIN **coerce_domains;

  /* Whether there is a need to use the coerce domain. */
  bool need_coerce_domains;
};
#define HASHJOIN_FETCH_KEY_INFO_INITIALIZER { NULL, NULL, false, false }
#define HJ_FETCH_KEY_INFO_INITIALIZER HASHJOIN_FETCH_KEY_INFO_INITIALIZER

typedef struct hashjoin_partition_info HASHJOIN_PARTITION_INFO;
typedef struct hashjoin_partition_info HJ_PARTITION_INFO;
struct hashjoin_partition_info
{
  QFILE_LIST_ID *list_id;
  QFILE_LIST_ID **part_list_id;
  int part_cnt;
};
#define HASHJOIN_PARTITION_INFO_INITIALIZER { NULL, NULL, 0 }
#define HJ_PARTITION_INFO_INITIALIZER HASHJOIN_PARTITION_INFO_INITIALIZER

typedef struct hashjoin_context HASHJOIN_CONTEXT;
typedef struct hashjoin_context HJ_CONTEXT;
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

typedef struct hashjoin_manager HASHJOIN_MANAGER;
typedef struct hashjoin_manager HJ_MANAGER;
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

/**
 * Function Declarations
 */

static QFILE_LIST_ID *qexec_hash_join_internal (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_CONTEXT * context,
						QUERY_ID query_id, HJ_STATS * stats);
static QFILE_LIST_ID *qexec_hash_outer_join_fill_empty (THREAD_ENTRY * thread_p, HJ_MANAGER * manager,
							HJ_CONTEXT * context, QUERY_ID query_id, HJ_STATS * stats);

/* HJ_MANAGER */
static int qexec_hash_join_init_manager (THREAD_ENTRY * thread_p, XASL_NODE * xasl, HJ_MANAGER * manager,
					 QUERY_ID query_id);
static void qexec_hash_join_clear_manager (THREAD_ENTRY * thread_p, HJ_MANAGER * manager);

/* HJ_CONTEXT */
static int qexec_hash_join_init_context (THREAD_ENTRY * thread_p, HJ_CONTEXT * context, HJ_STATS * stats);
static void qexec_hash_join_clear_contexts (THREAD_ENTRY * thread_p, HJ_CONTEXT * context);

/* HASH_LIST_SCAN */
static int qexec_hash_join_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt,
				      QFILE_LIST_ID * list_id);
static void qexec_hash_join_scan_clear (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan);

static HJ_STATUS qexec_hash_join_check_empty_inputs (HJ_MANAGER * manager, HJ_CONTEXT * context);

static HJ_STATUS qexec_hash_join_partition_inputs (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_STATS * stats);
static int qexec_hash_join_partition_input (THREAD_ENTRY * thread_p, HJ_PARTITION_INFO * part_info,
					    HJ_FETCH_INFO * fetch_info, bool is_null_allowed, HASH_SCAN_KEY * key,
					    HJ_STATS * stats);
static int qexec_hash_join_fetch_key (THREAD_ENTRY * thread_p, HJ_FETCH_INFO * fetch_info,
				      QFILE_TUPLE_RECORD * tuple_record, HASH_SCAN_KEY * key,
				      HASH_SCAN_KEY * compare_key, bool * exit_on_next);
static int qexec_hash_join_build (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_CONTEXT * context,
				  QFILE_LIST_SCAN_ID * list_scan_id, HJ_STATS * stats);
static int qexec_hash_join_build_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan,
				      QFILE_TUPLE_RECORD * tuple_record, QFILE_LIST_SCAN_ID * list_scan_id);
static int qexec_hash_join_probe (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_CONTEXT * context,
				  QFILE_LIST_SCAN_ID * build_scan_id, QFILE_LIST_SCAN_ID * probe_scan_id,
				  QFILE_LIST_ID * list_id, HJ_STATS * stats);
static int qexec_hash_join_probe_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan,
				      QFILE_TUPLE_RECORD * tuple_record, QFILE_LIST_SCAN_ID * list_scan_id);

static int
qexec_hash_join_merge_tuple_add_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, QFILE_TUPLE_RECORD * tplrec1,
				      QFILE_TUPLE_RECORD * tplrec2, QFILE_LIST_MERGE_INFO * merge_info,
				      QFILE_TUPLE_RECORD * tplrec);
static int
qexec_hash_join_merge_tuple (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2,
			     QFILE_LIST_MERGE_INFO * merge_info, QFILE_TUPLE_RECORD * tplrec);
static long
qexec_hash_join_size_remaining (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2,
				QFILE_LIST_MERGE_INFO * merge_info, int k);

/**
 * Function Definitions
 */

int
qexec_hash_join (THREAD_ENTRY * thread_p, XASL_NODE * xasl, UINT64 query_id)
{
  QFILE_LIST_ID *list_id = NULL, *t_list_id = NULL, *context_list_id = NULL;

  HASHJOIN_PROC_NODE *proc;
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;

  HJ_MANAGER manager;
  HJ_CONTEXT *single_context, *current_context;
  HJ_STATS *stats = NULL, *current_stats;
  HJ_STATUS status, part_status;

  int context_cnt, context_index;

  int error = NO_ERROR;

  bool on_trace = thread_is_on_trace (thread_p);
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  assert (thread_p != NULL);
  assert (xasl != NULL);

  proc = &xasl->proc.hashjoin;
  merge_info = &proc->merge_info;

  if (on_trace)
    {
      stats = &proc->stats;
      memset (stats, 0, sizeof (HJ_STATS));
    }

  single_context = &manager.single_context;

  error = qexec_hash_join_init_manager (thread_p, xasl, &manager, query_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  status = qexec_hash_join_check_empty_inputs (&manager, single_context);

  switch (status)
    {
    case HASHJOIN_FILL_EMPTY:
      list_id = qexec_hash_outer_join_fill_empty (thread_p, &manager, single_context, query_id, stats);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      goto exit_on_end;

    case HASHJOIN_TRY:
      part_status = qexec_hash_join_partition_inputs (thread_p, &manager, stats);

      switch (part_status)
	{
	case HASHJOIN_SINGLE:
	  list_id = qexec_hash_join_internal (thread_p, &manager, single_context, query_id, stats);
	  if (list_id == NULL)
	    {
	      goto exit_on_error;
	    }

	  goto exit_on_end;

	case HASHJOIN_PARTITION:
	  /* fall through */
	  break;

	default:
	  goto exit_on_error;
	}

      break;

    case HASHJOIN_END:
      goto exit_on_end;

    case HASHJOIN_ERROR:
    default:
      goto exit_on_error;
    }

  context_cnt = manager.context_cnt;

  if (on_trace)
    {
      proc->part_stats = (HJ_STATS *) db_private_alloc (thread_p, sizeof (HJ_STATS) * context_cnt);
      if (proc->part_stats == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (HJ_STATS) * context_cnt);
	  goto exit_on_error;
	}
      memset (proc->part_stats, 0, sizeof (HJ_STATS) * context_cnt);
    }


  for (context_index = 0; context_index < context_cnt; context_index++)
    {
      current_context = &manager.contexts[context_index];
      current_stats = (on_trace) ? &proc->part_stats[context_index] : NULL;

      context_list_id = qexec_hash_join_internal (thread_p, &manager, current_context, query_id, stats);
      if (list_id == NULL)
	{
	  goto exit_on_error;
	}

      qfile_close_list (thread_p, context_list_id);

      if (list_id->tuple_cnt > 0)
	{
	  t_list_id = qfile_combine_two_list (thread_p, list_id, context_list_id, QFILE_FLAG_ALL | QFILE_FLAG_UNION);
	  if (list_id == NULL)
	    {
	      goto exit_on_error;
	    }

	  qfile_clear_list_id (list_id);
	  qfile_copy_list_id (list_id, t_list_id, false);
	}
      else
	{
	  qfile_copy_list_id (list_id, context_list_id, false);
	}

      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
      QFILE_FREE_AND_INIT_LIST_ID (context_list_id);

      if (on_trace && current_stats != NULL)
	{
	  TSC_ADD_TIMEVAL (stats->build.elapsed_time, current_stats->build.elapsed_time);
	  TSC_ADD_TIMEVAL (stats->build.build_time, current_stats->build.build_time);
	  stats->build.fetches += current_stats->build.fetches;
	  stats->build.fetch_time += current_stats->build.fetch_time;
	  stats->build.ioreads += current_stats->build.ioreads;

#if HASH_JOIN_PROFILE_TIME
	  TSC_ADD_TIMEVAL (stats->build.profile.fetch, current_stats->build.profile.fetch);
	  TSC_ADD_TIMEVAL (stats->build.profile.hash, current_stats->build.profile.hash);
	  TSC_ADD_TIMEVAL (stats->build.profile.insert, current_stats->build.profile.insert);
#endif

	  TSC_ADD_TIMEVAL (stats->probe.elapsed_time, current_stats->probe.elapsed_time);
	  TSC_ADD_TIMEVAL (stats->probe.probe_time, current_stats->probe.probe_time);
	  stats->probe.fetches += current_stats->probe.fetches;
	  stats->probe.fetch_time += current_stats->probe.fetch_time;
	  stats->probe.ioreads += current_stats->probe.ioreads;
	  stats->probe.readkeys += current_stats->probe.readkeys;
	  stats->probe.rows += current_stats->probe.rows;
	  stats->probe.max_collisions = MAX (stats->probe.max_collisions, current_stats->probe.max_collisions);

#if HASH_JOIN_PROFILE_TIME
	  TSC_ADD_TIMEVAL (stats->probe.profile.fetch, current_stats->probe.profile.fetch);
	  TSC_ADD_TIMEVAL (stats->probe.profile.hash, current_stats->probe.profile.hash);
	  TSC_ADD_TIMEVAL (stats->probe.profile.search, current_stats->probe.profile.search);
	  TSC_ADD_TIMEVAL (stats->probe.profile.match, current_stats->probe.profile.match);
	  TSC_ADD_TIMEVAL (stats->probe.profile.add, current_stats->probe.profile.add);
#endif
	}
    }

exit_on_end:
  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_copy_list_id (xasl->list_id, list_id, false);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  qexec_hash_join_clear_manager (thread_p, &manager);

  return error;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static QFILE_LIST_ID *
qexec_hash_join_internal (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_CONTEXT * context, QUERY_ID query_id,
			  HJ_STATS * stats)
{
  QFILE_LIST_ID *build_list_id, *probe_list_id, *list_id = NULL;
  QFILE_LIST_SCAN_ID build_list_scan_id, probe_list_scan_id;
  HJ_STATUS status;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  /* Prevent faults when qfile_close_scan is called */
  build_list_scan_id.status = S_CLOSED;
  probe_list_scan_id.status = S_CLOSED;

  status = qexec_hash_join_check_empty_inputs (manager, context);

  switch (status)
    {
    case HASHJOIN_FILL_EMPTY:
      list_id = qexec_hash_outer_join_fill_empty (thread_p, manager, context, query_id, stats);
      if (list_id == NULL)
	{
	  goto exit_on_error;
	}

      goto exit_on_end;

    case HASHJOIN_TRY:
      break;

    case HASHJOIN_END:
      goto exit_on_end;

    case HASHJOIN_ERROR:
    default:
      goto exit_on_error;
    }

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, QFILE_FLAG_ALL, NULL);
  if (list_id == NULL)
    {
      goto exit_on_error;
    }

  error = qexec_hash_join_init_context (thread_p, context, stats);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
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

  /* build */
  error = qfile_open_list_scan (build_list_id, &build_list_scan_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = qexec_hash_join_build (thread_p, manager, context, &build_list_scan_id, stats);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  qfile_close_scan (thread_p, &build_list_scan_id);

  /* probe */
  error = qfile_open_list_scan (probe_list_id, &probe_list_scan_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  // qexec_hash_outer_join_internal 고려해야 한다.
  error = qexec_hash_join_probe (thread_p, manager, context, &build_list_scan_id, &probe_list_scan_id, list_id, stats);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  qfile_close_scan (thread_p, &build_list_scan_id);

exit_on_end:
  return list_id;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static QFILE_LIST_ID *
qexec_hash_outer_join_fill_empty (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_CONTEXT * context,
				  QUERY_ID query_id, HJ_STATS * stats)
{
  QFILE_LIST_ID *list_id, *result_list_id;
  QFILE_LIST_SCAN_ID list_scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_TUPLE_RECORD result_record = { NULL, 0 };
  QFILE_TUPLE_RECORD *outer_record;
  QFILE_TUPLE_RECORD *inner_record;
  SCAN_CODE scan_code;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (stats == NULL || stats->hash_method == HASH_METH_NOT_USE);

  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;

  /* Prevent faults when qfile_close_scan is called */
  list_scan_id.status = S_CLOSED;

  switch (context->join_type)
    {
    case JOIN_LEFT:
      list_id = context->outer_list_id;
      outer_record = &tuple_record;
      inner_record = NULL;
      break;

    case JOIN_RIGHT:
      list_id = context->inner_list_id;
      outer_record = NULL;
      inner_record = &tuple_record;
      break;

    default:
      assert (false);
      goto exit_on_error;
    }

  result_list_id = qfile_open_list (thread_p, &manager->type_list, NULL, query_id, QFILE_FLAG_ALL, NULL);
  if (result_list_id == NULL)
    {
      goto exit_on_error;
    }

  error = qfile_reallocate_tuple (&result_record, DB_PAGESIZE);
  if (error != NO_ERROR)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, DB_PAGESIZE);
      goto exit_on_error;
    }


  error = qfile_open_list_scan (list_id, &list_scan_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (on_trace)
    {
      tsc_getticks (&start_tick);

      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error =
	qexec_hash_join_merge_tuple_add_list (thread_p, result_list_id, outer_record, inner_record, manager->merge_info,
					      &result_record);
      if (error != NO_ERROR)
	{
	  break;
	}

      if (on_trace)
	{
	  stats->probe.rows++;
	}
    }

  if (on_trace)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->probe.probe_time, tv_diff);
      TSC_ADD_TIMEVAL (stats->probe.elapsed_time, tv_diff);

      stats->probe.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->probe.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->probe.fetch_time +=
	(UINT64) ((perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) -
		   old_fetch_time) / 1000);
    }

  qfile_close_scan (thread_p, &list_scan_id);

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto exit_on_error;
    }

  qfile_close_list (thread_p, list_id);
  qfile_destroy_list (thread_p, list_id);

exit_on_end:
  if (result_record.tpl)
    {
      db_private_free_and_init (thread_p, result_record.tpl);
    }

  return list_id;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static int
qexec_hash_join_init_manager (THREAD_ENTRY * thread_p, XASL_NODE * xasl, HJ_MANAGER * manager, QUERY_ID query_id)
{
  HASHJOIN_PROC_NODE *proc;
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  HJ_DOMAIN_INFO *domain_info;
  HJ_CONTEXT *context;

  QFILE_TUPLE_VALUE_TYPE_LIST *type_list;
  int type_cnt, type_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (xasl != NULL);
  assert (manager != NULL);

  memset (manager, 0, sizeof (HJ_MANAGER));

  proc = &xasl->proc.hashjoin;

  merge_info = &proc->merge_info;
  assert (merge_info->ls_pos_cnt > 0);
  assert (merge_info->ls_pos_list != NULL);
  manager->merge_info = merge_info;

  manager->outer = &proc->outer;
  assert (manager->outer->xasl != NULL);

  outer_list_id = manager->outer->xasl->list_id;
  assert (outer_list_id != NULL);

  manager->inner = &proc->inner;
  assert (manager->inner->xasl != NULL);

  inner_list_id = manager->outer->xasl->list_id;
  assert (inner_list_id != NULL);

  domain_info = &proc->domain_info;
  assert (domain_info->outer.domains != NULL);
  assert (domain_info->outer.value_indexes != NULL);
  assert (domain_info->inner.domains != NULL);
  assert (domain_info->inner.value_indexes != NULL);
  assert (domain_info->need_coerce_domains == false || domain_info->coerce_domains != NULL);
  assert (domain_info->need_coerce_domains == true || domain_info->coerce_domains == NULL);

  /* single_context */
  context = &manager->single_context;

  context->outer_list_id = outer_list_id;
  context->inner_list_id = inner_list_id;

  context->outer_fetch_info.input = &domain_info->outer;
  context->outer_fetch_info.coerce_domains = domain_info->coerce_domains;
  context->outer_fetch_info.need_coerce_domains = domain_info->need_coerce_domains;

  context->inner_fetch_info.input = &domain_info->inner;
  context->inner_fetch_info.coerce_domains = domain_info->coerce_domains;
  context->inner_fetch_info.need_coerce_domains = domain_info->need_coerce_domains;

  context->key_cnt = merge_info->ls_column_cnt;

  context->join_type = merge_info->join_type;
  context->during_join_pred = xasl->during_join_pred;

  context->is_single_context = true;
  assert (context->is_build_outer == false);

  /* contexts */
  assert (manager->contexts == NULL);
  assert (manager->context_cnt == 0);

  /* query_id */
  manager->query_id = query_id;

  /* type_list */
  type_list = &manager->type_list;
  assert (type_list->domp == NULL);
  assert (type_list->type_cnt == 0);

  /**
   * When aptr_list is executed in qexec_execute_mainblock_internal,
   * it checks the results from outer_xasl and inner_xasl in merge_info.
   * If either one has no result, the execution of the other is skipped.
   * In this case, the list_id.type_list.type_cnt of the skipped one can be 0.
   */
  if (outer_list_id->type_list.type_cnt > 0 && inner_list_id->type_list.type_cnt > 0)
    {
      assert (outer_list_id->type_list.domp != NULL);
      assert (inner_list_id->type_list.domp != NULL);

      type_cnt = merge_info->ls_pos_cnt;

      type_list->domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *) * type_cnt);
      if (type_list->domp == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (TP_DOMAIN *) * type_cnt);
	  goto exit_on_error;
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
    }

exit_on_end:
  return error;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static void
qexec_hash_join_clear_manager (THREAD_ENTRY * thread_p, HJ_MANAGER * manager)
{
  HJ_CONTEXT *contexts = NULL;
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
	  qexec_hash_join_clear_contexts (thread_p, &contexts[context_index]);
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

static int
qexec_hash_join_init_context (THREAD_ENTRY * thread_p, HJ_CONTEXT * context, HJ_STATS * stats)
{
  QFILE_LIST_ID *outer_list_id, *inner_list_id, *build_list_id;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (context != NULL);

  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;

  /* NULL checks not needed. HJ_MANAGER is already verified in qexec_hash_join_init_manager. */
  outer_list_id = context->outer_list_id;
  inner_list_id = context->inner_list_id;
  assert (outer_list_id != NULL && outer_list_id->tuple_cnt > 0);
  assert (inner_list_id != NULL && inner_list_id->tuple_cnt > 0);

  /**
   * The build input may need to be changed even if the cached xasl is reused,
   * if the value of the bind variable changes.
   */
  switch (context->join_type)
    {
    case JOIN_INNER:
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

      break;

    case JOIN_LEFT:
      context->is_build_outer = false;
      break;

    case JOIN_RIGHT:
      context->is_build_outer = true;
      break;

    default:
      assert (false);
      goto exit_on_error;
    }

  // qexec_hash_join_init_context 밖에서 수행하도록 빼야 한다.
  if (on_trace)
    {
      stats->is_build_outer = context->is_build_outer;
    }

  build_list_id = (context->is_build_outer) ? outer_list_id : inner_list_id;

  /*
   * hash_scan
   */
  error = qexec_hash_join_scan_init (thread_p, &context->hash_scan, context->key_cnt, build_list_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  return NO_ERROR;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  qexec_hash_join_scan_clear (thread_p, &context->hash_scan);

  return error;
}

static void
qexec_hash_join_clear_contexts (THREAD_ENTRY * thread_p, HJ_CONTEXT * context)
{
  assert (thread_p != NULL);
  assert (context != NULL);

  if (context->outer_list_id != NULL)
    {
      qfile_close_list (thread_p, context->outer_list_id);
      qfile_destroy_list (thread_p, context->outer_list_id);
    }

  if (context->inner_list_id != NULL)
    {
      qfile_close_list (thread_p, context->inner_list_id);
      qfile_destroy_list (thread_p, context->inner_list_id);
    }

  qexec_hash_join_scan_clear (thread_p, &context->hash_scan);
}

static int
qexec_hash_join_scan_init (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, int key_cnt, QFILE_LIST_ID * list_id)
{
  UINT64 mem_limit;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (list_id != NULL && list_id->tuple_cnt > 0);
  assert (key_cnt > 0);

  // TODO: 이미 만들어진 XASL을 재수핼할 때, 시스템 파라미터 값이 0으로 바뀌는 경우에 대해 대비해야 한다.
  mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
  assert (mem_limit > 0);

  assert (hash_scan->build_regu_list == NULL);	/* Unused. */
  assert (hash_scan->probe_regu_list == NULL);	/* Unused. */

  hash_scan->temp_key = qdata_alloc_hscan_key (thread_p, key_cnt, true);
  if (hash_scan->temp_key == NULL)
    {
      goto exit_on_error;
    }

  hash_scan->temp_new_key = qdata_alloc_hscan_key (thread_p, key_cnt, true);
  if (hash_scan->temp_new_key == NULL)
    {
      goto exit_on_error;
    }

  if ((UINT64) list_id->page_cnt * DB_PAGESIZE <= mem_limit)
    {
#if HASH_JOIN_DUMP_BUILD
      fprintf (stdout, "\n[DEBUG] Hash Join Method: In Memory\n");
      fprintf (stdout, "  - Page Count: %d <= %lu\n", list_id->page_cnt, mem_limit / 16344);
#endif

      hash_scan->hash_list_scan_type = HASH_METH_IN_MEM;

      hash_scan->memory.hash_table = mht_create_hls ("Hash Join", list_id->tuple_cnt, NULL, NULL);
      if (hash_scan->memory.hash_table == NULL)
	{
	  goto exit_on_error;
	}

      hash_scan->memory.curr_hash_entry = NULL;
    }
  else if ((UINT64) list_id->tuple_cnt * (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)) <= mem_limit)
    {
#if HASH_JOIN_DUMP_BUILD
      fprintf (stdout, "\n[DEBUG] Hash Join Method: Hybrid\n");
      fprintf (stdout, "  - Page Count: %d > %lu\n", list_id->page_cnt, mem_limit / 16344);
      fprintf (stdout, "  - Tuple Count: %ld <= %lu\n", list_id->tuple_cnt,
	       mem_limit / (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)));
#endif

      hash_scan->hash_list_scan_type = HASH_METH_HYBRID;

      hash_scan->memory.hash_table = mht_create_hls ("Hash Join", list_id->tuple_cnt, NULL, NULL);
      if (hash_scan->memory.hash_table == NULL)
	{
	  goto exit_on_error;
	}

      hash_scan->memory.curr_hash_entry = NULL;
    }
  else
    {
#if HASH_JOIN_DUMP_BUILD
      fprintf (stdout, "\n[DEBUG] Hash Join Method: File\n");
      fprintf (stdout, "  - Page Count: %d > %lu\n", list_id->page_cnt, mem_limit / 16344);
      fprintf (stdout, "  - Tuple Count: %ld > %lu\n", list_id->tuple_cnt,
	       mem_limit / (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)));
#endif

      hash_scan->hash_list_scan_type = HASH_METH_HASH_FILE;

      hash_scan->file.hash_table = (FHSID *) db_private_alloc (thread_p, sizeof (FHSID));
      if (hash_scan->file.hash_table == NULL)
	{
	  goto exit_on_error;
	}

      if (fhs_create (thread_p, hash_scan->file.hash_table, list_id->tuple_cnt) == NULL)
	{
	  goto exit_on_error;
	}

      hash_scan->file.curr_oid = OID_INITIALIZER;
      hash_scan->file.is_dk_bucket = false;
    }

  hash_scan->curr_hash_key = 0;
  hash_scan->need_coerce_type = false;

  return NO_ERROR;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  qexec_hash_join_scan_clear (thread_p, hash_scan);

  return error;
}

static void
qexec_hash_join_scan_clear (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan)
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
      /* nothing to do */
      break;
    }

  hash_scan->hash_list_scan_type == HASH_METH_NOT_USE;
}

static HJ_STATUS
qexec_hash_join_check_empty_inputs (HJ_MANAGER * manager, HJ_CONTEXT * context)
{
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  HJ_STATUS status;

  assert (manager != NULL);
  assert (context != NULL);

  outer_list_id = context->outer_list_id;
  inner_list_id = context->inner_list_id;
  assert (outer_list_id != NULL);
  assert (inner_list_id != NULL);

  switch (manager->merge_info->join_type)
    {
    case JOIN_INNER:
      /* build: inner, probe: outer - default */
      context->is_build_outer = false;
      status = (outer_list_id->tuple_cnt == 0 || inner_list_id->tuple_cnt == 0) ? HASHJOIN_END : HASHJOIN_TRY;
      break;

    case JOIN_LEFT:
      /* build: inner, probe: outer */
      context->is_build_outer = false;
      /* In OUTER JOIN, HASHJOIN_END must be checked first. */
      status =
	(outer_list_id->tuple_cnt == 0) ? HASHJOIN_END : (inner_list_id->tuple_cnt ==
							  0) ? HASHJOIN_FILL_EMPTY : HASHJOIN_TRY;
      break;

    case JOIN_RIGHT:
      /* build: outer, probe: inner */
      context->is_build_outer = true;
      /* In OUTER JOIN, HASHJOIN_END must be checked first. */
      status =
	(inner_list_id->tuple_cnt == 0) ? HASHJOIN_END : (outer_list_id->tuple_cnt ==
							  0) ? HASHJOIN_FILL_EMPTY : HASHJOIN_TRY;
      break;

    case JOIN_OUTER:
      /* FULL OUTER JOIN is not supported. */
    default:
      assert (false);
      status = HASHJOIN_ERROR;
      break;
    }

  return status;
}

static HJ_STATUS
qexec_hash_join_partition_inputs (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_STATS * stats)
{
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  QFILE_LIST_ID **outer_part_list_id = NULL, **inner_part_list_id = NULL;
  HJ_CONTEXT *single_context, *contexts = NULL;

  HJ_PARTITION_INFO part_info = HJ_PARTITION_INFO_INITIALIZER;
  HASH_SCAN_KEY *part_key = NULL;
  INT64 max_tuple_cnt;
  int part_cnt, part_index;

  bool is_outer_join;

  UINT64 mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
  HJ_STATUS status;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;

  /* NULL checks not needed. Already verified in qexec_hash_join_init_manager. */
  merge_info = manager->merge_info;
  single_context = &manager->single_context;
  is_outer_join = IS_OUTER_JOIN_TYPE (single_context->join_type);
  outer_list_id = single_context->outer_list_id;
  inner_list_id = single_context->inner_list_id;

  max_tuple_cnt =
    (outer_list_id->tuple_cnt > inner_list_id->tuple_cnt) ? outer_list_id->tuple_cnt : inner_list_id->tuple_cnt;

#define RESERVE_RATIO 0.8
  part_cnt = CEIL_PTVDIV (max_tuple_cnt * (sizeof (HENTRY_HLS) + sizeof (QFILE_TUPLE_SIMPLE_POS)),
			  mem_limit * RESERVE_RATIO);
#undef RESERVE_RATIO

  if (part_cnt <= 1)
    {
      /* give up */
      assert (manager->context_cnt == 0);
      status = HASHJOIN_SINGLE;
      goto exit_on_end;
    }

  if (IS_OUTER_JOIN_TYPE (merge_info->join_type))
    {
      /* Add a partition to store tuples with NULL in the join predicate. */
      part_cnt += 1;
    }

  manager->context_cnt = part_cnt;

  if (on_trace)
    {
      stats->part.part_cnt = part_cnt;
    }

  contexts = (HJ_CONTEXT *) db_private_alloc (thread_p, sizeof (HJ_CONTEXT) * part_cnt);
  if (contexts == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (HJ_CONTEXT) * part_cnt);
      goto exit_on_error;
    }
  memset (contexts, 0, sizeof (HJ_CONTEXT) * part_cnt);

  outer_part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, sizeof (QFILE_LIST_ID *) * part_cnt);
  if (outer_part_list_id == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (QFILE_LIST_ID *) * part_cnt);
      goto exit_on_error;
    }
  memset (outer_part_list_id, 0, sizeof (QFILE_LIST_ID *) * part_cnt);

  inner_part_list_id = (QFILE_LIST_ID **) db_private_alloc (thread_p, sizeof (QFILE_LIST_ID *) * part_cnt);
  if (inner_part_list_id == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (QFILE_LIST_ID *) * part_cnt);
      goto exit_on_error;
    }
  memset (inner_part_list_id, 0, sizeof (QFILE_LIST_ID *) * part_cnt);

  for (part_index = 0; part_index < part_cnt; part_index++)
    {
      memcpy (&contexts[part_index].outer_fetch_info, &single_context->outer_fetch_info, sizeof (HJ_CONTEXT));

      outer_part_list_id[part_index] =
	qfile_open_list (thread_p, &outer_list_id->type_list, NULL, outer_list_id->query_id, QFILE_FLAG_ALL, NULL);
      if (outer_part_list_id[part_index] == NULL)
	{
	  goto exit_on_error;
	}
      contexts[part_index].outer_list_id = outer_part_list_id[part_index];


      inner_part_list_id[part_index] =
	qfile_open_list (thread_p, &inner_list_id->type_list, NULL, inner_list_id->query_id, QFILE_FLAG_ALL, NULL);
      if (inner_part_list_id[part_index] == NULL)
	{
	  goto exit_on_error;
	}
      contexts[part_index].inner_list_id = inner_part_list_id[part_index];

      /* single_context != context */
      contexts[part_index].is_single_context = false;
    }

  manager->contexts = contexts;

  part_key = qdata_alloc_hscan_key (thread_p, merge_info->ls_column_cnt, true);
  if (part_key == NULL)
    {
      goto exit_on_error;
    }

  /* common */
  part_info.part_cnt = part_cnt;

  /* outer */
  part_info.list_id = outer_list_id;
  part_info.part_list_id = outer_part_list_id;

  error = qexec_hash_join_partition_input (thread_p, &part_info, &single_context->outer_fetch_info, is_outer_join,
					   part_key, stats);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* inner */
  part_info.list_id = inner_list_id;
  part_info.part_list_id = inner_part_list_id;

  error = qexec_hash_join_partition_input (thread_p, &part_info, &single_context->inner_fetch_info, is_outer_join,
					   part_key, stats);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  status = HASHJOIN_PARTITION;

exit_on_end:
  if (part_key == NULL)
    {
      qdata_free_hscan_key (thread_p, part_key, merge_info->ls_column_cnt);
    }

  if (outer_part_list_id != NULL)
    {
      /* Shares pointers with contexts; open list_id is retired in contexts. */
      db_private_free_and_init (thread_p, outer_part_list_id);
    }

  if (inner_part_list_id != NULL)
    {
      /* Shares pointers with contexts; open list_id is retired in contexts. */
      db_private_free_and_init (thread_p, inner_part_list_id);
    }

  return status;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  if (contexts != NULL)
    {
      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  qexec_hash_join_clear_contexts (thread_p, &contexts[part_index]);
	}

      db_private_free_and_init (thread_p, contexts);

      manager->contexts = NULL;
      manager->context_cnt = 0;
    }

  /* retry */
  status = HASHJOIN_SINGLE;

  goto exit_on_end;
}

static int
qexec_hash_join_partition_input (THREAD_ENTRY * thread_p, HJ_PARTITION_INFO * part_info,
				 HJ_FETCH_INFO * fetch_info, bool is_null_allowed, HASH_SCAN_KEY * key,
				 HJ_STATS * stats)
{
  QFILE_LIST_SCAN_ID list_scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool exit_on_next = false;

  unsigned int hash_key, part_id;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (fetch_info != NULL);
  assert (part_info != NULL);
  assert (key != NULL);

  /* part_info */
  assert (part_info->list_id != NULL);
  assert (part_info->part_cnt > 1 && part_info->part_list_id != NULL);
#if !defined (NDEBUG)
  for (int part_index = 0; part_index < part_info->part_cnt; part_index++)
    {
      assert (part_info->part_list_id[part_index] != NULL);
    }
#endif /* !NDEBUG */

  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;

  error = qfile_open_list_scan (part_info->list_id, &list_scan_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (on_trace)
    {
      tsc_getticks (&start_tick);

      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error =
	qexec_hash_join_fetch_key (thread_p, fetch_info, &tuple_record, key, NULL /* compare_key */ , &exit_on_next);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else if (exit_on_next == true)
	{
	  if (is_null_allowed)
	    {
	      /* The last partition stores tuples with NULL in the join predicate. */
	      error =
		qfile_add_tuple_to_list (thread_p, part_info->part_list_id[part_info->part_cnt], tuple_record.tpl);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      /* Give up and read the next tuple. */
	    }

	  exit_on_next = false;
	  continue;
	}
      else
	{
	  /* fall through */
	}

      hash_key = qdata_hash_scan_key (key, UINT_MAX, HASH_METH_IN_MEM);
      part_id = (is_null_allowed) ? hash_key % (part_info->part_cnt - 1) : hash_key % (part_info->part_cnt);

      error = qfile_add_tuple_to_list (thread_p, part_info->part_list_id[part_id], tuple_record.tpl);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (on_trace)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->part.elapsed_time, tv_diff);

      stats->part.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->part.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->part.fetch_time +=
	(UINT64) ((perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) -
		   old_fetch_time) / 1000);
    }

  qfile_close_scan (thread_p, &list_scan_id);

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Discard to avoid duplication with part_list_id. */
  qfile_close_list (thread_p, part_info->list_id);
  qfile_destroy_list (thread_p, part_info->list_id);

exit_on_end:
  return error;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static int
qexec_hash_join_fetch_key (THREAD_ENTRY * thread_p, HJ_FETCH_INFO * fetch_info,
			   QFILE_TUPLE_RECORD * tuple_record, HASH_SCAN_KEY * key, HASH_SCAN_KEY * compare_key,
			   bool * exit_on_next)
{
  TP_DOMAIN **domains, **coerce_domains;
  int *value_indexes;
  bool need_coerce_domains;

  OR_BUF iterator, buf;
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
  assert (exit_on_next != NULL && *exit_on_next == false);

  /**
   * NULL checks not needed.
   * HJ_FETCH_KEY_INFO must be verified by the caller.
   */
  domains = fetch_info->input->domains;
  value_indexes = fetch_info->input->value_indexes;
  coerce_domains = fetch_info->coerce_domains;
  need_coerce_domains = fetch_info->need_coerce_domains;

  // db_make_null (&pre_coerce_value);

  or_init (&iterator, tuple_record->tpl, QFILE_GET_TUPLE_LENGTH (tuple_record->tpl));

  /* Skip the header of the tuple. */
  error = or_advance (&iterator, QFILE_TUPLE_LENGTH_SIZE);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Since the number of values ​​in the tuple is unknown, this routine is executed until ptr reaches endptr. */
  for (value_index = 0; iterator.ptr < iterator.endptr; value_index++)
    {
      for (key_index = 0; key_index < key->val_count; key_index++)
	{
	  /* The same value can be used repeatedly for different keys.
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

	  /* If any of the tuple values ​​are null, exit this routine and read the next tuple. */
	  if (QFILE_GET_TUPLE_VALUE_FLAG (iterator.ptr) == V_UNBOUND)
	    {
	      /* Give up and read the next tuple. */
	      goto exit_on_next;
	    }

	  value_size = QFILE_GET_TUPLE_VALUE_LENGTH (iterator.ptr);
	  assert (value_size > 0);

	  /* Skip the header of the tuple value. */
	  or_init (&buf, iterator.ptr + QFILE_TUPLE_VALUE_HEADER_SIZE, value_size);

	  pr_clear_value (key->values[key_index]);

	  if (need_coerce_domains == true && coerce_domains[key_index] != NULL
	      && coerce_domains[key_index] != domains[key_index])
	    {
	      error =
		domains[key_index]->type->data_readval (&buf, &pre_coerce_value, domains[key_index], -1, false,
							NULL, 0);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      domain_status = tp_value_coerce (&pre_coerce_value, key->values[key_index], coerce_domains[key_index]);

	      pr_clear_value (&pre_coerce_value);

	      if (domain_status != DOMAIN_COMPATIBLE)
		{
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      error =
		domains[key_index]->type->data_readval (&buf, key->values[key_index], domains[key_index], -1, false,
							NULL, 0);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  if (compare_key != NULL)
	    {
	      /* If any of the tuple values ​​are not equal, exit this routine and read the next tuple. */
	      compare_result = tp_value_compare (key->values[key_index], compare_key->values[key_index], 0, 0);
	      if (compare_result != DB_EQ)
		{
		  /* Give up and read the next tuple. */
		  goto exit_on_next;
		}
	    }
	}

      /* Skip the current tuple. */
      error = or_advance (&iterator, QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (iterator.ptr));
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* When ptr reaches endptr, exit this routine. */
    }

exit_on_end:
  return error;

exit_on_next:
  *exit_on_next = true;
  goto exit_on_end;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static int
qexec_hash_join_build (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_CONTEXT * context,
		       QFILE_LIST_SCAN_ID * list_scan_id, HJ_STATS * stats)
{
  HJ_FETCH_INFO *fetch_info;

  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool exit_on_next;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (list_scan_id != NULL);

#if HASH_JOIN_PROFILE_TIME
  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#endif

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
      fetch_info = &context->inner_fetch_info;;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
#if HASH_JOIN_PROFILE_TIME
      if (on_trace)
	{
	  tsc_getticks (&start_tick);
	}
#endif

      error =
	qexec_hash_join_fetch_key (thread_p, fetch_info, &tuple_record, key, NULL /* compare_key */ , &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
      if (on_trace)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (stats->build.profile.fetch, tv_diff);

	  tsc_getticks (&start_tick);
	}
#endif

      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else if (exit_on_next == true)
	{
	  /* Give up and read the next tuple. */
	  continue;
	}
      else
	{
	  /* fall through */
	}

      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);

#if HASH_JOIN_PROFILE_TIME
      if (on_trace)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (stats->build.profile.hash, tv_diff);

	  tsc_getticks (&start_tick);
	}
#endif

      error = qexec_hash_join_build_key (thread_p, hash_scan, &tuple_record, list_scan_id);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

#if HASH_JOIN_PROFILE_TIME
      if (on_trace)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (stats->build.profile.insert, tv_diff);
	}
#endif
    }

  if (scan_code == S_ERROR)
    {
      goto exit_on_error;
    }

  assert (scan_code == S_END);

#if HASH_JOIN_DUMP_HASH_TABLE
  {
    XASL_NODE *build_xasl = (context->is_build_outer) ? manager->outer->xasl : manager->inner->xasl;
    assert (build_xasl != NULL);

    QFILE_LIST_ID *build_list_id = &list_scan_id->list_id;
    assert (build_list_id != NULL);

    if (build_list_id->tuple_cnt <= 100)
      {
	switch (hash_method)
	  {
	  case HASH_METH_IN_MEM:
	  case HASH_METH_HYBRID:
	    mht_dump_hls (thread_p, stdout, hash_scan->memory.hash_table, 1, qdata_print_hash_scan_entry,
			  &build_list_id->type_list, (void *) &hash_method);
	    printf ("temp file : tuple count = %ld, file_size = %dK\n", build_list_id->tuple_cnt,
		    build_list_id->page_cnt * 16);
	    break;

	  case HASH_METH_HASH_FILE:
	    fhs_dump (thread_p, hash_scan->file.hash_table);
	    break;

	  case HASH_METH_NOT_USE:
	  default:
	    /* nothing to do */
	    break;
	  }
      }
  }
#endif

exit_on_end:
  return error;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static int
qexec_hash_join_build_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_TUPLE_RECORD * tuple_record,
			   QFILE_LIST_SCAN_ID * list_scan_id)
{
  HASH_SCAN_VALUE *hash_value = NULL;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (tuple_record != NULL && tuple_record->tpl != NULL);
  assert (list_scan_id != NULL);

  switch (hash_scan->hash_list_scan_type)
    {
    case HASH_METH_IN_MEM:
      assert (hash_scan->memory.hash_table != NULL);

      hash_value = qdata_alloc_hscan_value (thread_p, tuple_record->tpl);
      if (hash_value == NULL)
	{
	  goto exit_on_error;
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, (void *) hash_value) == NULL)
	{
	  goto exit_on_error;
	}

      break;

    case HASH_METH_HYBRID:
      assert (hash_scan->memory.hash_table != NULL);

      hash_value = qdata_alloc_hscan_value_OID (thread_p, list_scan_id);
      if (hash_value == NULL)
	{
	  goto exit_on_error;
	}

      if (mht_put_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key, (void *) hash_value) == NULL)
	{
	  goto exit_on_error;
	}

      break;

    case HASH_METH_HASH_FILE:
      TFTID tftid;

      assert (hash_scan->file.hash_table != NULL);

      SET_TFTID (tftid, list_scan_id->curr_vpid.volid, list_scan_id->curr_vpid.pageid, list_scan_id->curr_offset);

      if (fhs_insert (thread_p, hash_scan->file.hash_table, (void *) &hash_scan->curr_hash_key, &tftid) == NULL)
	{
	  goto exit_on_error;
	}

      break;

    case HASH_METH_NOT_USE:
    default:
      assert (false);
      goto exit_on_error;
    }

exit_on_end:
  return error;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  if (hash_value != NULL)
    {
      qdata_free_hscan_value (thread_p, hash_value);
    }

  goto exit_on_end;
}

static int
qexec_hash_join_probe (THREAD_ENTRY * thread_p, HJ_MANAGER * manager, HJ_CONTEXT * context,
		       QFILE_LIST_SCAN_ID * build_scan_id, QFILE_LIST_SCAN_ID * probe_scan_id, QFILE_LIST_ID * list_id,
		       HJ_STATS * stats)
{
  HJ_FETCH_INFO *build_fetch_info, *probe_fetch_info;

  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_TUPLE_RECORD found_record = { NULL, 0 };
  QFILE_TUPLE_RECORD result_record = { NULL, 0 };
  QFILE_TUPLE_RECORD *outer_record;
  QFILE_TUPLE_RECORD *inner_record;
  SCAN_CODE scan_code;
  bool exit_on_next;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key, *found_key;
  int max_collisions;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (build_scan_id != NULL);
  assert (probe_scan_id != NULL);
  assert (list_id != NULL);

  /* stats may be NULL, meaning tracing is disabled. */
  bool on_trace = (stats != NULL) ? true : false;
#if HASH_JOIN_PROFILE_TIME
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#endif

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

      outer_record = &found_record;
      inner_record = &tuple_record;
    }
  else
    {
      build_fetch_info = &context->inner_fetch_info;
      probe_fetch_info = &context->outer_fetch_info;

      outer_record = &tuple_record;
      inner_record = &found_record;
    }

  error = qfile_reallocate_tuple (&result_record, DB_PAGESIZE);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  while ((scan_code = qfile_scan_list_next (thread_p, probe_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
#if HASH_JOIN_DUMP_PROBE
      qfile_print_tuple (&probe_scan_id->list_id.type_list, tuple_record.tpl);
#endif

#if HASH_JOIN_PROFILE_TIME
      if (on_trace)
	{
	  tsc_getticks (&start_tick);
	}
#endif

      error =
	qexec_hash_join_fetch_key (thread_p, probe_fetch_info, &tuple_record, key, NULL /* compare_key */ ,
				   &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
      if (on_trace)
	{
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.fetch, tv_diff);

	  tsc_getticks (&start_tick);
	}
#endif

      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else if (exit_on_next == true)
	{
	  /* Give up and read the next tuple. */
	  continue;
	}
      else
	{
	  /* fall through */
	}

      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);

      if (on_trace)
	{
#if HASH_JOIN_PROFILE_TIME
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.hash, tv_diff);
#endif

	  max_collisions = 0;
	}

      do
	{
#if HASH_JOIN_PROFILE_TIME
	  if (on_trace)
	    {
	      tsc_getticks (&start_tick);
	    }
#endif

	  error = qexec_hash_join_probe_key (thread_p, hash_scan, &found_record, build_scan_id);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

#if HASH_JOIN_PROFILE_TIME
	  if (on_trace)
	    {
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.search, tv_diff);

	      tsc_getticks (&start_tick);
	    }
#endif

	  if (found_record.tpl == NULL)
	    {
	      /* The hash value was not found, so read the next tuple. */
	      break;
	    }

	  if (on_trace)
	    {
	      max_collisions++;
	    }

	  error = qexec_hash_join_fetch_key (thread_p, build_fetch_info, &found_record, found_key,
					     key /* compare_key */ , &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
	  if (on_trace)
	    {
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.match, tv_diff);

	      tsc_getticks (&start_tick);
	    }
#endif

	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  else if (exit_on_next == true)
	    {
#if HASH_JOIN_DUMP_PROBE
	      fprintf (stdout, "\nNot Matched Key: ");
	      qfile_print_tuple (&build_scan_id->list_id.type_list, found_record.tpl);
#endif

	      /* Give up and read the next tuple. */
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

#if HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nMatched Key: ");
	  qfile_print_tuple (&build_scan_id->list_id.type_list, found_record.tpl);
#endif

#if HASH_JOIN_PROFILE_TIME
	  if (on_trace)
	    {
	      tsc_getticks (&start_tick);
	    }
#endif

	  error =
	    qexec_hash_join_merge_tuple_add_list (thread_p, list_id, outer_record, inner_record, manager->merge_info,
						  &result_record);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (on_trace)
	    {
#if HASH_JOIN_PROFILE_TIME
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.add, tv_diff);
#endif

	      stats->probe.rows++;
	    }
	}
      while (true);

      if (on_trace)
	{
	  stats->probe.readkeys += max_collisions;
	  stats->probe.max_collisions = MAX (stats->probe.max_collisions, max_collisions);
	}
    }

  if (scan_code == S_ERROR)
    {
      goto exit_on_error;
    }

  assert (scan_code == S_END);

exit_on_end:
  if (result_record.tpl)
    {
      db_private_free_and_init (thread_p, result_record.tpl);
    }

  return error;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}

static int
qexec_hash_join_probe_key (THREAD_ENTRY * thread_p, HASH_LIST_SCAN * hash_scan, QFILE_TUPLE_RECORD * tuple_record,
			   QFILE_LIST_SCAN_ID * list_scan_id)
{
  HASH_SCAN_VALUE *hash_value = NULL;

  SCAN_CODE qp_scan;
  QFILE_TUPLE_POSITION tuple_position;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (hash_scan != NULL);
  assert (tuple_record != NULL);
  assert (list_scan_id != NULL);

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
	    (HASH_SCAN_VALUE *) mht_get_next_hls (hash_scan->memory.hash_table,
						  (void *) &hash_scan->curr_hash_key,
						  (void **) &hash_scan->memory.curr_hash_entry);
	}

      if (hash_value == NULL)
	{
	  tuple_record->tpl = NULL;
	  tuple_record->size = 0;

	  /* The hash value was not found, so read the next tuple. */
	  goto exit_on_end;
	}

      tuple_record->tpl = ((HASH_SCAN_VALUE *) hash_scan->memory.curr_hash_entry->data)->tuple;
      tuple_record->size = QFILE_GET_TUPLE_VALUE_LENGTH (tuple_record->tpl);

      break;

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

      if (hash_value == NULL)
	{
	  tuple_record->tpl = NULL;
	  tuple_record->size = 0;

	  /* The hash value was not found, so read the next tuple. */
	  goto exit_on_end;
	}

      MAKE_TUPLE_POSTION (tuple_position, hash_value->pos, list_scan_id);

      qp_scan = qfile_jump_scan_tuple_position (thread_p, list_scan_id, &tuple_position, tuple_record, PEEK);
      if (qp_scan != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      break;

    case HASH_METH_HASH_FILE:
      TFTID tftid;
      EH_SEARCH eh_search;

      assert (hash_scan->file.hash_table != NULL);

      if (tuple_record->tpl == NULL)
	{
	  eh_search = fhs_search (thread_p, hash_scan, &tftid);
	}
      else
	{
	  eh_search = fhs_search_next (thread_p, hash_scan, &tftid);
	}

      switch (eh_search)
	{
	case EH_KEY_FOUND:
	  {
	    MAKE_TFTID_TO_TUPLE_POSTION (tuple_position, tftid, list_scan_id);

	    qp_scan = qfile_jump_scan_tuple_position (thread_p, list_scan_id, &tuple_position, tuple_record, PEEK);
	    if (qp_scan != S_SUCCESS)
	      {
		goto exit_on_error;
	      }

	    break;
	  }

	case EH_KEY_NOTFOUND:
	  {
	    tuple_record->tpl = NULL;
	    tuple_record->size = 0;

	    /* The hash value was not found, so read the next tuple. */
	    goto exit_on_end;
	  }

	case EH_ERROR_OCCURRED:
	default:
	  goto exit_on_error;
	}

      break;

    case HASH_METH_NOT_USE:
    default:
      assert (false);
      goto exit_on_error;
    }

exit_on_end:
  return error;

exit_on_error:
  if (error == NO_ERROR)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  goto exit_on_end;
}














static int
qexec_hash_join_merge_tuple_add_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, QFILE_TUPLE_RECORD * tplrec1,
				      QFILE_TUPLE_RECORD * tplrec2, QFILE_LIST_MERGE_INFO * merge_info,
				      QFILE_TUPLE_RECORD * tplrec)
{
  int ret;
  QFILE_TUPLE_DESCRIPTOR *tdp;
  int tplrec1_max_size;
  int tplrec2_max_size;

  /* get tuple descriptor */
  tdp = &list_id->tpl_descr;

  if (tplrec1)
    {
      tplrec1_max_size = QFILE_GET_TUPLE_LENGTH (tplrec1->tpl);
    }
  else
    {
      tplrec1_max_size = QFILE_TUPLE_VALUE_HEADER_SIZE * (merge_info->ls_pos_cnt);
    }

  if (tplrec2)
    {
      tplrec2_max_size = QFILE_GET_TUPLE_LENGTH (tplrec2->tpl);
    }
  else
    {
      tplrec2_max_size = QFILE_TUPLE_VALUE_HEADER_SIZE * (merge_info->ls_pos_cnt);
    }

  tdp->tpl_size = DB_ALIGN (tplrec1_max_size + tplrec2_max_size, MAX_ALIGNMENT);

  if (tdp->tpl_size < QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      /* SMALL QFILE_TUPLE */
      /* set tuple descriptor */
      tdp->tplrec1 = tplrec1;
      tdp->tplrec2 = tplrec2;
      tdp->merge_info = merge_info;

      /* build merged tuple into the list file page */
      ret = qfile_generate_tuple_into_list (thread_p, list_id, T_MERGE);
    }
  else
    {
      /* BIG QFILE_TUPLE */
      /* merge two tuples, and form a new tuple */
      ret = qexec_hash_join_merge_tuple (tplrec1, tplrec2, merge_info, tplrec);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      /* add merged tuple to the resultant list file */
      ret = qfile_add_tuple_to_list (thread_p, list_id, tplrec->tpl);
    }

  return ret;
}

static int
qexec_hash_join_merge_tuple (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2,
			     QFILE_LIST_MERGE_INFO * merge_info, QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_TUPLE tplp;
  char *t_valhp;
  int t_val_size;
  int tpl_size, offset;
  int k;
  INT32 ls_unbound[2] = { 0, 0 };

  /* merge two tuples, and form a new tuple */
  tplp = tplrec->tpl;
  offset = 0;
  QFILE_PUT_TUPLE_LENGTH (tplp, QFILE_TUPLE_LENGTH_SIZE);	/* set tuple length */
  tplp += QFILE_TUPLE_LENGTH_SIZE;
  offset += QFILE_TUPLE_LENGTH_SIZE;

  QFILE_PUT_TUPLE_VALUE_FLAG ((char *) ls_unbound, V_UNBOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH ((char *) ls_unbound, 0);

  /* copy tuple values from the first and second list file tuples */
  for (k = 0; k < merge_info->ls_pos_cnt; k++)
    {

      if (merge_info->ls_outer_inner_list[k] == QFILE_OUTER_LIST)
	{
	  if (tplrec1)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec1->tpl, merge_info->ls_pos_list[k], t_valhp);
	    }
	  else
	    {
	      t_valhp = (char *) ls_unbound;
	    }
	}
      else
	{
	  /* copy from the second tuple */
	  if (tplrec2)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec2->tpl, merge_info->ls_pos_list[k], t_valhp);
	    }
	  else
	    {
	      t_valhp = (char *) ls_unbound;
	    }
	}

      t_val_size = QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
      if ((tplrec->size - offset) < t_val_size)
	{
	  /* no space left */
	  tpl_size = offset + qexec_hash_join_size_remaining (tplrec1, tplrec2, merge_info, k);
	  if (qfile_reallocate_tuple (tplrec, tpl_size) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  tplp = (QFILE_TUPLE) tplrec->tpl + offset;
	}

      memcpy (tplp, t_valhp, t_val_size);
      tplp += t_val_size;
      offset += t_val_size;
    }				/* for */

  /* set tuple length */
  QFILE_PUT_TUPLE_LENGTH (tplrec->tpl, offset);

  return NO_ERROR;
}

static long
qexec_hash_join_size_remaining (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2,
				QFILE_LIST_MERGE_INFO * merge_info, int k)
{
  int i, tpl_size;
  char *t_valhp;

  tpl_size = 0;
  for (i = k; i < merge_info->ls_pos_cnt; i++)
    {
      tpl_size += QFILE_TUPLE_VALUE_HEADER_SIZE;
      if (merge_info->ls_outer_inner_list[i] == QFILE_OUTER_LIST)
	{
	  if (tplrec1)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec1->tpl, merge_info->ls_pos_list[i], t_valhp);
	      tpl_size += QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
	    }
	}
      else
	{
	  if (tplrec2)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec2->tpl, merge_info->ls_pos_list[i], t_valhp);
	      tpl_size += QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
	    }
	}
    }

  return tpl_size;
}
