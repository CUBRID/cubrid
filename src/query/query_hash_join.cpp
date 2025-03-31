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

#include "query_hash_join.hpp"

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

/**
 * Enum & Typedef Definitions
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

/**
 * Struct & Typedef Definitions
 */

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
  PRED_EXPR *remaining_join_pred;
  PRED_EXPR *instnum_pred;

  HASH_LIST_SCAN hash_scan;
  bool is_build_outer;

  HJ_STATS *stats;
};

typedef struct hashjoin_manager HASHJOIN_MANAGER;
typedef struct hashjoin_manager HJ_MANAGER;
struct hashjoin_manager
{
  HJ_INPUT *outer;
  HJ_INPUT *inner;
  QFILE_LIST_MERGE_INFO *merge_info;

  HJ_CONTEXT single_context;
  HJ_CONTEXT *contexts;
  int context_cnt;

  QUERY_ID query_id;
  VAL_DESCR *vd;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;

  HJ_STATS_GROUP *stats_group;
};

/**
 * Function Declarations
 */

/* Hash Join Execution */
static QFILE_LIST_ID *qexec_hash_join_partition (THREAD_ENTRY *thread_p, HJ_MANAGER *manager);
static QFILE_LIST_ID *qexec_hash_join_context (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context);
static QFILE_LIST_ID *qexec_hash_outer_join_fill_empty (THREAD_ENTRY *thread_p, HJ_MANAGER *manager,
    HJ_CONTEXT *context);
static QFILE_LIST_ID *qexec_hash_join_internal (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context);
static QFILE_LIST_ID *qexec_hash_outer_join_internal (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context);

/* Hash Join Manager */
static int qexec_hash_join_init_manager (THREAD_ENTRY *thread_p, XASL_NODE *xasl, HJ_MANAGER *manager,
    QUERY_ID query_id, VAL_DESCR *vd);
static void qexec_hash_join_clear_manager (THREAD_ENTRY *thread_p, HJ_MANAGER *manager);

/* Hash Join Domain Info */
static int qexec_hash_join_init_domain_info (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_DOMAIN_INFO *domain_info);

/* Hash Join Partitioning */
static HJ_STATUS qexec_hash_join_partition_inputs (THREAD_ENTRY *thread_p, HJ_MANAGER *manager);
static int qexec_hash_join_partition_input (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_PARTITION_INFO *part_info,
    HJ_FETCH_INFO *fetch_info, bool is_null_allowed, HASH_SCAN_KEY *key);

/* Hash Join Context */
static int qexec_hash_join_init_context (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context);
static void qexec_hash_join_clear_contexts (THREAD_ENTRY *thread_p, HJ_CONTEXT *context);

/* Hash List Scan */
static int qexec_hash_join_scan_init (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan, int key_cnt,
				      QFILE_LIST_ID *list_id);
static void qexec_hash_join_scan_clear (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan);

/* Hash Join Processing */
static HJ_STATUS qexec_hash_join_check_empty_inputs (HJ_CONTEXT *context);
static int qexec_hash_join_fetch_key (THREAD_ENTRY *thread_p, HJ_FETCH_INFO *fetch_info,
				      QFILE_TUPLE_RECORD *tuple_record, HASH_SCAN_KEY *key, HASH_SCAN_KEY *compare_key, bool *exit_on_next);
static void qexec_hash_join_merge_stats (HJ_STATS *stats, HJ_STATS *context_stats);
static void qexec_hash_join_is_valid_part_info (HJ_PARTITION_INFO *info);

/* Build Phase */
static int qexec_hash_join_build (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context,
				  QFILE_LIST_SCAN_ID *list_scan_id);
static int qexec_hash_join_build_key (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan,
				      QFILE_TUPLE_RECORD *tuple_record, QFILE_LIST_SCAN_ID *list_scan_id);

/* Probe Phase */
static int qexec_hash_join_probe (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context,
				  SCAN_ID *build_scan_id, SCAN_ID *probe_scan_id, QFILE_LIST_ID *list_id);
static int qexec_hash_outer_join_probe (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context,
					SCAN_ID *build_scan_id, SCAN_ID *probe_scan_id, QFILE_LIST_ID *list_id);
static int qexec_hash_join_probe_key (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan,
				      QFILE_TUPLE_RECORD *tuple_record, QFILE_LIST_SCAN_ID *list_scan_id);

/* Merge QFILE_LIST_ID */
static int qexec_hash_join_merge_tuple_add_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id,
    QFILE_TUPLE_RECORD *tplrec1, QFILE_TUPLE_RECORD *tplrec2, QFILE_LIST_MERGE_INFO *merge_info,
    QFILE_TUPLE_RECORD *tplrec);
static int qexec_hash_join_merge_tuple (QFILE_TUPLE_RECORD *tplrec1, QFILE_TUPLE_RECORD *tplrec2,
					QFILE_LIST_MERGE_INFO *merge_info, QFILE_TUPLE_RECORD *tplrec);
static long qexec_hash_join_size_remaining (QFILE_TUPLE_RECORD *tplrec1, QFILE_TUPLE_RECORD *tplrec2,
    QFILE_LIST_MERGE_INFO *merge_info, int k);

/**
 * Function Definitions
 */

int
qexec_hash_join (THREAD_ENTRY *thread_p, XASL_NODE *xasl, QUERY_ID query_id, VAL_DESCR *vd)
{
  QFILE_LIST_ID *list_id = NULL;

  HJ_MANAGER manager;
  HJ_STATUS status, part_status;
  HJ_CONTEXT *single_context;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (xasl != NULL);
  assert (query_id != NULL_QUERY_ID);

  error = qexec_hash_join_init_manager (thread_p, xasl, &manager, query_id, vd);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  single_context = &manager.single_context;

  status = qexec_hash_join_check_empty_inputs (single_context);
  switch (status)
    {
    case HASHJOIN_FILL_EMPTY:
      list_id = qexec_hash_outer_join_fill_empty (thread_p, &manager, single_context);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      goto exit_on_end;

    case HASHJOIN_TRY:
      part_status = qexec_hash_join_partition_inputs (thread_p, &manager);
      switch (part_status)
	{
	case HASHJOIN_SINGLE:
	  list_id = qexec_hash_join_context (thread_p, &manager, single_context);
	  if (list_id == NULL)
	    {
	      goto exit_on_error;
	    }
	  goto exit_on_end;

	case HASHJOIN_PARTITION:
	  list_id = qexec_hash_join_partition (thread_p, &manager);
	  if (list_id == NULL)
	    {
	      goto exit_on_error;
	    }
	  goto exit_on_end;

	default:
	  assert (false);
	  goto exit_on_error;
	}
      break;

    case HASHJOIN_END:
      goto exit_on_end;

    case HASHJOIN_ERROR:
    default:
      goto exit_on_error;
    }

exit_on_end:
  if (list_id != NULL)
    {
      qfile_destroy_list (thread_p, xasl->list_id);
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
qexec_hash_join_partition (THREAD_ENTRY *thread_p, HJ_MANAGER *manager)
{
  QFILE_LIST_ID *list_id = NULL, *t_list_id = NULL, *context_list_id = NULL;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  HJ_CONTEXT *current_context;
  int context_cnt, context_index;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  HJ_STATS *total_stats = &manager->stats_group->stats;
  HJ_STATS *current_stats;

  context_cnt = manager->context_cnt;

  for (context_index = 0; context_index < context_cnt; context_index++)
    {
      current_context = &manager->contexts[context_index];

      current_stats = current_context->stats;
      assert (current_stats != NULL || !thread_is_on_trace (thread_p));

      context_list_id = qexec_hash_join_context (thread_p, manager, current_context);

      if (thread_is_on_trace (thread_p))
	{
	  qexec_hash_join_merge_stats (total_stats, current_stats);
	}

      if (context_list_id == NULL)
	{
	  // list_id가 NULL이면서 에러가 아닌 경우가 있을까?
	  error = er_errid ();
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  else
	    {
	      continue;
	    }
	}

      qfile_close_list (thread_p, context_list_id);

      if (list_id != NULL)
	{
	  t_list_id = qfile_combine_two_list (thread_p, list_id, context_list_id, QFILE_FLAG_ALL | QFILE_FLAG_UNION);
	  if (t_list_id == NULL)
	    {
	      goto exit_on_error;
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

  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  if (context_list_id != NULL)
    {
      qfile_close_list (thread_p, context_list_id);
      qfile_destroy_list (thread_p, context_list_id);
      QFILE_FREE_AND_INIT_LIST_ID (context_list_id);
    }

  goto exit_on_end;
}

static QFILE_LIST_ID *
qexec_hash_join_context (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context)
{
  QFILE_LIST_ID *list_id = NULL;
  HJ_STATUS status;
  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  status = qexec_hash_join_check_empty_inputs (context);
  switch (status)
    {
    case HASHJOIN_FILL_EMPTY:
      assert (context != &manager->single_context);
      list_id = qexec_hash_outer_join_fill_empty (thread_p, manager, context);
      if (list_id == NULL)
	{
	  goto exit_on_error;
	}
      goto exit_on_end;

    case HASHJOIN_TRY:
      if (IS_OUTER_JOIN_TYPE (context->join_type))
	{
	  list_id = qexec_hash_outer_join_internal (thread_p, manager, context);
	}
      else
	{
	  list_id = qexec_hash_join_internal (thread_p, manager, context);
	}

      if (list_id == NULL)
	{
	  goto exit_on_error;
	}
      goto exit_on_end;

    case HASHJOIN_END:
      goto exit_on_end;

    case HASHJOIN_ERROR:
    default:
      goto exit_on_error;
    }

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

  assert (list_id == NULL);

  goto exit_on_end;
}

static QFILE_LIST_ID *
qexec_hash_outer_join_fill_empty (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context)
{
  QFILE_LIST_ID *outer_list_id, *list_id = NULL;
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

  HJ_STATS *stats = context->stats;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  /* Prevent faults when qfile_close_scan is called */
  outer_scan_id.status = S_CLOSED;

  switch (context->join_type)
    {
    case JOIN_LEFT:
      outer_list_id = context->outer_list_id;
      left_record = &tuple_record;
      right_record = NULL;
      break;

    case JOIN_RIGHT:
      outer_list_id = context->inner_list_id;
      left_record = NULL;
      right_record = &tuple_record;
      break;

    default:
      assert (false);
      goto exit_on_error;
    }

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, QFILE_FLAG_ALL, NULL);
  if (list_id == NULL)
    {
      goto exit_on_error;
    }

  // TODO: 필요한 경우에만
  error = qfile_reallocate_tuple (&overflow_record, DB_PAGESIZE);
  if (error != NO_ERROR)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, DB_PAGESIZE);
      goto exit_on_error;
    }

  error = qfile_open_list_scan (outer_list_id, &outer_scan_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);
      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &outer_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error = qexec_hash_join_merge_tuple_add_list (thread_p, list_id, left_record, right_record, manager->merge_info,
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
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->probe.probe_time, tv_diff);
      TSC_ADD_TIMEVAL (stats->probe.elapsed_time, tv_diff);
      stats->probe.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->probe.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->probe.fetch_time += (UINT64) ((perfmon_get_from_statistic (thread_p,
					    PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) - old_fetch_time) / 1000);
    }

  qfile_close_scan (thread_p, &outer_scan_id);

  if (scan_code == S_ERROR || error != NO_ERROR)
    {
      goto exit_on_error;
    }

exit_on_end:
  if (overflow_record.tpl)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
    }

  qfile_close_list (thread_p, outer_list_id);
  qfile_destroy_list (thread_p, outer_list_id);

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

  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  goto exit_on_end;
}

static QFILE_LIST_ID *
qexec_hash_join_internal (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context)
{
  ACCESS_SPEC_TYPE *build_spec = NULL, *probe_spec = NULL;
  VAL_LIST *build_val_list, *probe_val_list;
  QFILE_LIST_ID *build_list_id = NULL, *probe_list_id = NULL, *list_id = NULL;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, QFILE_FLAG_ALL, NULL);
  if (list_id == NULL)
    {
      goto exit_on_error;
    }

  error = qexec_hash_join_init_context (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (context->is_build_outer)
    {
      build_spec = manager->outer->spec_list;
      build_val_list = manager->outer->val_list;
      build_list_id = context->outer_list_id;

      probe_spec = manager->inner->spec_list;
      probe_val_list = manager->inner->val_list;
      probe_list_id = context->inner_list_id;
    }
  else
    {
      build_spec = manager->inner->spec_list;
      build_val_list = manager->inner->val_list;
      build_list_id = context->inner_list_id;

      probe_spec = manager->outer->spec_list;
      probe_val_list = manager->outer->val_list;
      probe_list_id = context->outer_list_id;
    }

  error =  scan_open_list_scan (thread_p, &build_spec->s_id, build_spec->grouped_scan,
				build_spec->single_fetch, build_spec->s_dbval, build_val_list, manager->vd, build_list_id,
				build_spec->s.list_node.list_regu_list_pred,
				build_spec->where_pred, build_spec->s.list_node.list_regu_list_rest,
				build_spec->s.list_node.list_regu_list_build,
				build_spec->s.list_node.list_regu_list_probe,
				build_spec->s.list_node.hash_list_scan_yn);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = scan_start_scan (thread_p, &build_spec->s_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = qexec_hash_join_build (thread_p, manager, context, &build_spec->s_id.s.llsid.lsid);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error =  scan_open_list_scan (thread_p, &probe_spec->s_id, probe_spec->grouped_scan,
				probe_spec->single_fetch, probe_spec->s_dbval, probe_val_list, manager->vd, probe_list_id,
				probe_spec->s.list_node.list_regu_list_pred,
				probe_spec->where_pred, probe_spec->s.list_node.list_regu_list_rest,
				probe_spec->s.list_node.list_regu_list_build,
				probe_spec->s.list_node.list_regu_list_probe,
				probe_spec->s.list_node.hash_list_scan_yn);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = scan_start_scan (thread_p, &probe_spec->s_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = qexec_hash_join_probe (thread_p, manager, context, &build_spec->s_id, &probe_spec->s_id, list_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

exit_on_end:
  scan_close_scan (thread_p, &build_spec->s_id);
  scan_close_scan (thread_p, &probe_spec->s_id);

  qfile_close_list (thread_p, build_list_id);
  qfile_destroy_list (thread_p, build_list_id);

  qfile_close_list (thread_p, probe_list_id);
  qfile_destroy_list (thread_p, probe_list_id);

  qexec_hash_join_scan_clear (thread_p, &context->hash_scan);

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

  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  goto exit_on_end;
}

static QFILE_LIST_ID *
qexec_hash_outer_join_internal (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context)
{
  ACCESS_SPEC_TYPE *build_spec = NULL, *probe_spec = NULL;
  VAL_LIST *build_val_list, *probe_val_list;
  QFILE_LIST_ID *build_list_id = NULL, *probe_list_id = NULL, *list_id = NULL;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);

  list_id = qfile_open_list (thread_p, &manager->type_list, NULL, manager->query_id, QFILE_FLAG_ALL, NULL);
  if (list_id == NULL)
    {
      goto exit_on_error;
    }

  error = qexec_hash_join_init_context (thread_p, manager, context);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (context->is_build_outer)
    {
      build_spec = manager->outer->spec_list;
      build_val_list = manager->outer->val_list;
      build_list_id = context->outer_list_id;

      probe_spec = manager->inner->spec_list;
      probe_val_list = manager->inner->val_list;
      probe_list_id = context->inner_list_id;
    }
  else
    {
      build_spec = manager->inner->spec_list;
      build_val_list = manager->inner->val_list;
      build_list_id = context->inner_list_id;

      probe_spec = manager->outer->spec_list;
      probe_val_list = manager->outer->val_list;
      probe_list_id = context->outer_list_id;
    }

  error =  scan_open_list_scan (thread_p, &build_spec->s_id, build_spec->grouped_scan,
				build_spec->single_fetch, build_spec->s_dbval, build_val_list, manager->vd, build_list_id,
				build_spec->s.list_node.list_regu_list_pred,
				build_spec->where_pred, build_spec->s.list_node.list_regu_list_rest,
				build_spec->s.list_node.list_regu_list_build,
				build_spec->s.list_node.list_regu_list_probe,
				build_spec->s.list_node.hash_list_scan_yn);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = scan_start_scan (thread_p, &build_spec->s_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = qexec_hash_join_build (thread_p, manager, context, &build_spec->s_id.s.llsid.lsid);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error =  scan_open_list_scan (thread_p, &probe_spec->s_id, probe_spec->grouped_scan,
				probe_spec->single_fetch, probe_spec->s_dbval, probe_val_list, manager->vd, probe_list_id,
				probe_spec->s.list_node.list_regu_list_pred,
				probe_spec->where_pred, probe_spec->s.list_node.list_regu_list_rest,
				probe_spec->s.list_node.list_regu_list_build,
				probe_spec->s.list_node.list_regu_list_probe,
				probe_spec->s.list_node.hash_list_scan_yn);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = scan_start_scan (thread_p, &probe_spec->s_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = qexec_hash_outer_join_probe (thread_p, manager, context, &build_spec->s_id, &probe_spec->s_id, list_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

exit_on_end:
  scan_close_scan (thread_p, &build_spec->s_id);
  scan_close_scan (thread_p, &probe_spec->s_id);

  qfile_close_list (thread_p, build_list_id);
  qfile_destroy_list (thread_p, build_list_id);

  qfile_close_list (thread_p, probe_list_id);
  qfile_destroy_list (thread_p, probe_list_id);

  qexec_hash_join_scan_clear (thread_p, &context->hash_scan);

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

  if (list_id != NULL)
    {
      qfile_close_list (thread_p, list_id);
      qfile_destroy_list (thread_p, list_id);
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
    }

  goto exit_on_end;
}

static int
qexec_hash_join_init_manager (THREAD_ENTRY *thread_p, XASL_NODE *xasl, HJ_MANAGER *manager, QUERY_ID query_id,
			      VAL_DESCR *vd)
{
  HASHJOIN_PROC_NODE *proc;
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  HJ_DOMAIN_INFO *domain_info;
  HJ_CONTEXT *context;
  QFILE_TUPLE_VALUE_TYPE_LIST *type_list = NULL;
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
  manager->inner = &proc->inner;
  assert (manager->outer->xasl != NULL);
  assert (manager->inner->xasl != NULL);

  outer_list_id = manager->outer->xasl->list_id;
  inner_list_id = manager->inner->xasl->list_id;
  assert (outer_list_id != NULL);
  assert (inner_list_id != NULL);

  /**
   * When aptr_list is executed in qexec_execute_mainblock_internal,
   * it checks the results from outer_xasl and inner_xasl in merge_info.
   * If either one has no result, the execution of the other is skipped.
   * In this case, the list_id.type_list.type_cnt of the skipped one can be 0.
   */
  if (outer_list_id->type_list.type_cnt == 0 || inner_list_id->type_list.type_cnt == 0)
    {
      goto exit_on_end;
    }

  assert (outer_list_id->type_list.domp != NULL);
  assert (inner_list_id->type_list.domp != NULL);

  domain_info = &proc->domain_info;
  error = qexec_hash_join_init_domain_info (thread_p, manager, domain_info);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

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
  context->remaining_join_pred = proc->remaining_join_pred;
  context->instnum_pred = proc->instnum_pred;

  assert (context->hash_scan.hash_list_scan_type == HASH_METH_NOT_USE);
  assert (context->is_build_outer == false);

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

  /* stats_group */
  manager->stats_group = &proc->stats_group;
  memset (manager->stats_group, 0, sizeof (HJ_STATS_GROUP));

  context->stats = &manager->stats_group->stats;

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

  assert (type_list == NULL || type_list->domp == NULL);

  goto exit_on_end;
}

static void
qexec_hash_join_clear_manager (THREAD_ENTRY *thread_p, HJ_MANAGER *manager)
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
qexec_hash_join_init_domain_info (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_DOMAIN_INFO *domain_info)
{
  QFILE_LIST_MERGE_INFO *merge_info;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;

  TP_DOMAIN **outer_domains, **inner_domains, **coerce_domains;
  int *outer_value_indexes, *inner_value_indexes;
  int outer_value_index, inner_value_index;
  int domain_cnt, domain_index, skip_index;
  bool need_coerce_domains;

  DB_TYPE outer_type, inner_type, common_type;
  int outer_precision, inner_precision;
  int outer_scale, inner_scale;
  int outer_integral, inner_integral;
  int common_precision, common_scale;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (domain_info != NULL);

  /* NULL checks not needed. HJ_MANAGER is already verified in qexec_hash_join_init_manager. */
  merge_info = manager->merge_info;
  outer_list_id = manager->outer->xasl->list_id;
  inner_list_id = manager->inner->xasl->list_id;

  /**
   * domain_info
   */
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
      else if (outer_type == DB_TYPE_NULL)
	{
	  assert (false);
	  need_coerce_domains = true;
	  coerce_domains[domain_index] = inner_domains[domain_index];
	  continue;
	}
      else if (inner_type == DB_TYPE_NULL)
	{
	  assert (false);
	  need_coerce_domains = true;
	  coerce_domains[domain_index] = outer_domains[domain_index];
	  continue;
	}
      else if ((TP_IS_BIT_TYPE (outer_type) && TP_IS_BIT_TYPE (inner_type))
	       || (TP_IS_CHAR_TYPE (outer_type) && TP_IS_CHAR_TYPE (inner_type))
	       || (TP_IS_DATE_TYPE (outer_type) && TP_IS_DATE_TYPE (inner_type))
	       || (TP_IS_SET_TYPE (outer_type) && TP_IS_SET_TYPE (inner_type))
	       || (TP_IS_NUMERIC_TYPE (outer_type) && TP_IS_NUMERIC_TYPE (inner_type)))
	{
	  common_type = (tp_more_general_type (outer_type, inner_type) > 0) ? outer_type : inner_type;
	}
      else
	{
	  assert (false);
	  need_coerce_domains = true;
	  coerce_domains[domain_index] = &tp_String_domain;
	  continue;
	}

      /* outer_precision, outer_scale */
      outer_precision = outer_domains[domain_index]->precision;
      outer_scale = outer_domains[domain_index]->scale;

      inner_precision = inner_domains[domain_index]->precision;
      inner_scale = inner_domains[domain_index]->scale;

      if (outer_precision == inner_precision && outer_scale == inner_scale)
	{
	  coerce_domains[domain_index] = NULL;
	  continue;
	}
      else
	{
	  need_coerce_domains = true;
	}

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

      /* need_coerce_domains, coerce_domains */
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
	      goto exit_on_error;
	    }

	  coerce_domains[domain_index]->precision = common_precision;
	  coerce_domains[domain_index]->scale = common_scale;

	  coerce_domains[domain_index] = tp_domain_cache (coerce_domains[domain_index]);
	}
    }

#if !defined (NDEBUG)
  if (!need_coerce_domains)
    {
      for (domain_index = 0; domain_index < domain_cnt; domain_index++)
	{
	  assert (coerce_domains[domain_index] == NULL);
	}
    }
#endif /* !NDEBUG */

  /*
   * If any join predicate compares different types, need_coerce_domains is set to true.
   * Otherwise, need_coerce_domains is false.
   *
   * If need_coerce_domains is true, coerce_domains is either inner_domains,
   * outer_domains, or a common domain for comparison.
   *
   * If either inner_domains or outer_domains matches coerce_domains,
   * value coercion is unnecessary for that domain.
   * Otherwise, values must be coerced to the common domain.
   */
  domain_info->need_coerce_domains = need_coerce_domains;

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

static HJ_STATUS
qexec_hash_join_partition_inputs (THREAD_ENTRY *thread_p, HJ_MANAGER *manager)
{
  QFILE_LIST_MERGE_INFO *merge_info;
  HJ_CONTEXT *single_context, *contexts = NULL;
  QFILE_LIST_ID *outer_list_id, *inner_list_id;
  QFILE_LIST_ID **outer_part_list_id = NULL, **inner_part_list_id = NULL;

  UINT64 mem_limit = prm_get_bigint_value (PRM_ID_MAX_HASH_LIST_SCAN_SIZE);
  INT64 max_tuple_cnt;
  int part_cnt, part_index;

  HJ_STATUS status;

  HJ_PARTITION_INFO part_info = HJ_PARTITION_INFO_INITIALIZER;
  HASH_SCAN_KEY *part_key = NULL;
  bool is_outer_join;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);

  /* NULL checks not needed. Already verified in qexec_hash_join_init_manager. */
  merge_info = manager->merge_info;
  single_context = &manager->single_context;
  outer_list_id = single_context->outer_list_id;
  inner_list_id = single_context->inner_list_id;
  is_outer_join = IS_OUTER_JOIN_TYPE (single_context->join_type);

  max_tuple_cnt = (outer_list_id->tuple_cnt > inner_list_id->tuple_cnt) ? outer_list_id->tuple_cnt :
		  inner_list_id->tuple_cnt;

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

  if (is_outer_join)
    {
      /* Add a partition to store tuples with NULL in the join predicate. */
      part_cnt += 1;
    }

  manager->context_cnt = part_cnt;

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
      memcpy (&contexts[part_index], single_context, sizeof (HJ_CONTEXT));

      outer_part_list_id[part_index] = qfile_open_list (thread_p, &outer_list_id->type_list, NULL, outer_list_id->query_id,
				       QFILE_FLAG_ALL, NULL);
      if (outer_part_list_id[part_index] == NULL)
	{
	  goto exit_on_error;
	}
      contexts[part_index].outer_list_id = outer_part_list_id[part_index];

      inner_part_list_id[part_index] = qfile_open_list (thread_p, &inner_list_id->type_list, NULL, inner_list_id->query_id,
				       QFILE_FLAG_ALL, NULL);
      if (inner_part_list_id[part_index] == NULL)
	{
	  goto exit_on_error;
	}
      contexts[part_index].inner_list_id = inner_part_list_id[part_index];
    }

  manager->contexts = contexts;

  if (thread_is_on_trace (thread_p))
    {
      manager->stats_group->context_stats = (HJ_STATS *) db_private_alloc (thread_p, sizeof (HJ_STATS) * part_cnt);
      if (manager->stats_group->context_stats == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (HJ_STATS) * part_cnt);
	  goto exit_on_error;
	}
      memset (manager->stats_group->context_stats, 0, sizeof (HJ_STATS) * part_cnt);

      manager->stats_group->context_cnt = part_cnt;

      for (part_index = 0; part_index < part_cnt; part_index++)
	{
	  contexts[part_index].stats = &manager->stats_group->context_stats[part_index];
	}
    }

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

  error = qexec_hash_join_partition_input (thread_p, manager, &part_info, &single_context->outer_fetch_info,
	  is_outer_join, part_key);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* inner */
  part_info.list_id = inner_list_id;
  part_info.part_list_id = inner_part_list_id;

  error = qexec_hash_join_partition_input (thread_p, manager, &part_info, &single_context->inner_fetch_info,
	  is_outer_join, part_key);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  status = HASHJOIN_PARTITION;

exit_on_end:
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

  if (thread_is_on_trace (thread_p))
    {
      if (manager->stats_group->context_stats != NULL)
	{
	  db_private_free_and_init (thread_p, manager->stats_group->context_stats);

	  for (part_index = 0; part_index < part_cnt; part_index++)
	    {
	      contexts[part_index].stats = NULL;
	    }
	}
    }

  /* retry */
  status = HASHJOIN_SINGLE;

  goto exit_on_end;
}

static int
qexec_hash_join_partition_input (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_PARTITION_INFO *part_info,
				 HJ_FETCH_INFO *fetch_info, bool is_null_allowed, HASH_SCAN_KEY *key)
{
  QFILE_LIST_SCAN_ID list_scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool exit_on_next = false;

  unsigned int hash_key, part_id;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (part_info != NULL);
  assert (fetch_info != NULL);
  assert (key != NULL);

  qexec_hash_join_is_valid_part_info (part_info);

  HJ_STATS *stats = &manager->stats_group->stats;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;
  assert (stats != NULL || !thread_is_on_trace (thread_p));

  error = qfile_open_list_scan (part_info->list_id, &list_scan_id);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);
      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, &list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
      error = qexec_hash_join_fetch_key (thread_p, fetch_info, &tuple_record, key, NULL /* compare_key */, &exit_on_next);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else if (exit_on_next)
	{
	  if (is_null_allowed)
	    {
	      /* The last partition stores tuples with NULL in the join predicate. */
	      error = qfile_add_tuple_to_list (thread_p, part_info->part_list_id[part_info->part_cnt], tuple_record.tpl);
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

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->part.elapsed_time, tv_diff);
      stats->part.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->part.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->part.fetch_time += (UINT64) ((perfmon_get_from_statistic (thread_p,
					   PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) - old_fetch_time) / 1000);
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
qexec_hash_join_init_context (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context)
{
  QFILE_LIST_ID *outer_list_id, *inner_list_id, *build_list_id;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (context->stats != NULL || !thread_is_on_trace (thread_p)); // 여기서 stats가 왜 널이지?

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
      else if (outer_list_id->tuple_cnt == inner_list_id->tuple_cnt && outer_list_id->page_cnt < inner_list_id->page_cnt)
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

    case JOIN_OUTER:
    /* FULL OUTER JOIN is not supported. */
    default:
      assert (false);
      goto exit_on_error;
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

  if (thread_is_on_trace (thread_p))
    {
      context->stats->hash_method = context->hash_scan.hash_list_scan_type;
      context->stats->is_build_outer = context->is_build_outer;
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

  qexec_hash_join_scan_clear (thread_p, &context->hash_scan);

  goto exit_on_end;
}

static void
qexec_hash_join_clear_contexts (THREAD_ENTRY *thread_p, HJ_CONTEXT *context)
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
qexec_hash_join_scan_init (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan, int key_cnt, QFILE_LIST_ID *list_id)
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
      fprintf (stdout, "\nHash Join Method: In Memory\n");
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
      fprintf (stdout, "\nHash Join Method: Hybrid\n");
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
      fprintf (stdout, "\nHash Join Method: File\n");
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

  qexec_hash_join_scan_clear (thread_p, hash_scan);

  goto exit_on_end;
}

static void
qexec_hash_join_scan_clear (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan)
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
qexec_hash_join_check_empty_inputs (HJ_CONTEXT *context)
{
  INT64 outer_tuple_cnt, inner_tuple_cnt;
  HJ_STATUS status;

  assert (context != NULL);

  /**
   * When aptr_list is executed in qexec_execute_mainblock_internal,
   * it checks the results from outer_xasl and inner_xasl in merge_info.
   * If either one has no result, the execution of the other is skipped.
   * In this case, the list_id.type_list.type_cnt of the skipped one can be 0.
   */
  if (context->outer_list_id == NULL || context->inner_list_id == NULL)
    {
      return HASHJOIN_END;
    }

  outer_tuple_cnt = context->outer_list_id->tuple_cnt;
  inner_tuple_cnt = context->inner_list_id->tuple_cnt;

  /* HASHJOIN_END must be checked first. */
  switch (context->join_type)
    {
    case JOIN_INNER:
      status = (outer_tuple_cnt == 0 || inner_tuple_cnt == 0) ? HASHJOIN_END : HASHJOIN_TRY;
      break;

    case JOIN_LEFT:
      status = (outer_tuple_cnt == 0) ? HASHJOIN_END : (inner_tuple_cnt == 0) ? HASHJOIN_FILL_EMPTY : HASHJOIN_TRY;
      break;

    case JOIN_RIGHT:
      status = (inner_tuple_cnt == 0) ? HASHJOIN_END : (outer_tuple_cnt == 0) ? HASHJOIN_FILL_EMPTY : HASHJOIN_TRY;
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

static int
qexec_hash_join_fetch_key (THREAD_ENTRY *thread_p, HJ_FETCH_INFO *fetch_info, QFILE_TUPLE_RECORD *tuple_record,
			   HASH_SCAN_KEY *key, HASH_SCAN_KEY *compare_key, bool *exit_on_next)
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

  db_make_null (&pre_coerce_value);

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

	  if (need_coerce_domains && coerce_domains[key_index] != NULL
	      && coerce_domains[key_index] != domains[key_index])
	    {
	      error = domains[key_index]->type->data_readval (&buf, &pre_coerce_value, domains[key_index], -1, false, NULL, 0);
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
	      error = domains[key_index]->type->data_readval (&buf, key->values[key_index], domains[key_index], -1, false, NULL, 0);
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

static void
qexec_hash_join_merge_stats (HJ_STATS *stats, HJ_STATS *context_stats)
{
  assert (stats != NULL);
  assert (context_stats != NULL);

  TSC_ADD_TIMEVAL (stats->build.elapsed_time, context_stats->build.elapsed_time);
  TSC_ADD_TIMEVAL (stats->build.build_time, context_stats->build.build_time);
  stats->build.fetches += context_stats->build.fetches;
  stats->build.fetch_time += context_stats->build.fetch_time;
  stats->build.ioreads += context_stats->build.ioreads;

#if HASH_JOIN_PROFILE_TIME
  TSC_ADD_TIMEVAL (stats->build.profile.fetch, context_stats->build.profile.fetch);
  TSC_ADD_TIMEVAL (stats->build.profile.hash, context_stats->build.profile.hash);
  TSC_ADD_TIMEVAL (stats->build.profile.insert, context_stats->build.profile.insert);
#endif

  TSC_ADD_TIMEVAL (stats->probe.elapsed_time, context_stats->probe.elapsed_time);
  TSC_ADD_TIMEVAL (stats->probe.probe_time, context_stats->probe.probe_time);
  stats->probe.fetches += context_stats->probe.fetches;
  stats->probe.fetch_time += context_stats->probe.fetch_time;
  stats->probe.ioreads += context_stats->probe.ioreads;
  stats->probe.readkeys += context_stats->probe.readkeys;
  stats->probe.rows += context_stats->probe.rows;
  stats->probe.max_collisions = MAX (stats->probe.max_collisions, context_stats->probe.max_collisions);

#if HASH_JOIN_PROFILE_TIME
  TSC_ADD_TIMEVAL (stats->probe.profile.fetch, context_stats->probe.profile.fetch);
  TSC_ADD_TIMEVAL (stats->probe.profile.hash, context_stats->probe.profile.hash);
  TSC_ADD_TIMEVAL (stats->probe.profile.search, context_stats->probe.profile.search);
  TSC_ADD_TIMEVAL (stats->probe.profile.match, context_stats->probe.profile.match);
  TSC_ADD_TIMEVAL (stats->probe.profile.add, context_stats->probe.profile.add);
#endif
}

static void
qexec_hash_join_is_valid_part_info (HJ_PARTITION_INFO *info)
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

static int
qexec_hash_join_build (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context,
		       QFILE_LIST_SCAN_ID *list_scan_id)
{
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE scan_code;
  bool exit_on_next = false;

  HJ_FETCH_INFO *fetch_info;

  HASH_LIST_SCAN *hash_scan;
  HASH_METHOD hash_method;
  HASH_SCAN_KEY *key;

  int error = NO_ERROR;

  assert (thread_p != NULL);
  assert (manager != NULL);
  assert (context != NULL);
  assert (list_scan_id != NULL);

  HJ_STATS *stats = context->stats;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#if HASH_JOIN_PROFILE_TIME
  TSC_TICKS profile_start_tick, profile_end_tick;
  TSCTIMEVAL profile_tv_diff;
#endif
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;
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
      tsc_getticks (&start_tick);
      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  while ((scan_code = qfile_scan_list_next (thread_p, list_scan_id, &tuple_record, PEEK)) == S_SUCCESS)
    {
#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_start_tick);	/* build.profile.fetch */
	}
#endif

      error = qexec_hash_join_fetch_key (thread_p, fetch_info, &tuple_record, key, NULL /* compare_key */, &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_end_tick);
	  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	  TSC_ADD_TIMEVAL (stats->build.profile.fetch, profile_tv_diff);

	  tsc_getticks (&profile_start_tick);	/* build.profile.hash */
	}
#endif

      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else if (exit_on_next)
	{
	  /* Give up and read the next tuple. */
	  exit_on_next = false;
	  continue;
	}
      else
	{
	  /* fall through */
	}

      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_end_tick);
	  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	  TSC_ADD_TIMEVAL (stats->build.profile.hash, profile_tv_diff);

	  tsc_getticks (&profile_start_tick);	/* build.profile.insert */
	}
#endif

      error = qexec_hash_join_build_key (thread_p, hash_scan, &tuple_record, list_scan_id);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_end_tick);
	  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	  TSC_ADD_TIMEVAL (stats->build.profile.insert, profile_tv_diff);
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
	    mht_dump_hls (thread_p, stdout, hash_scan->memory.hash_table, 1, qdata_print_hash_scan_entry, &build_list_id->type_list,
			  (void *) &hash_method);
	    printf ("temp file : tuple count = %ld, file_size = %dK\n", build_list_id->tuple_cnt, build_list_id->page_cnt * 16);
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
  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->build.elapsed_time, tv_diff);
      TSC_ADD_TIMEVAL (stats->build.build_time, tv_diff);
      stats->build.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->build.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->build.fetch_time +=
	      (UINT64) ((perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) -
			 old_fetch_time) / 1000);
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
qexec_hash_join_build_key (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan, QFILE_TUPLE_RECORD *tuple_record,
			   QFILE_LIST_SCAN_ID *list_scan_id)
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
qexec_hash_join_probe (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context,
		       SCAN_ID *build_scan_id, SCAN_ID *probe_scan_id, QFILE_LIST_ID *list_id)
{
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_TUPLE_RECORD found_record = { NULL, 0 };
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  QFILE_TUPLE_RECORD *left_record;
  QFILE_TUPLE_RECORD *right_record;
  SCAN_CODE scan_code;

  HJ_FETCH_INFO *build_fetch_info, *probe_fetch_info;
  bool exit_on_next = false;

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

  HJ_STATS *stats = context->stats;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#if HASH_JOIN_PROFILE_TIME
  TSC_TICKS profile_start_tick, profile_end_tick;
  TSCTIMEVAL profile_tv_diff;
#endif
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;
  int max_collisions = 0;
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

  error = qfile_reallocate_tuple (&overflow_record, DB_PAGESIZE);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);
      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  build_scan_id->s.llsid.tplrecp = &found_record;
  probe_scan_id->s.llsid.tplrecp = &tuple_record;

  while (true)
    {
      probe_scan_id->qualification = QPROC_QUALIFIED_OR_NOT;

      scan_code = scan_next_scan (thread_p, probe_scan_id);
      switch (scan_code)
	{
	case S_SUCCESS:
	  break;

	case S_END:
	  goto exit_on_end;

	case S_ERROR:
	  goto exit_on_error;

	default:
	  assert (false);
	  goto exit_on_error;
	}

#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
      qfile_print_tuple (&probe_scan_id->s.llsid.list_id->type_list, tuple_record.tpl);
#endif

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_start_tick);	/* probe.profile.fetch */
	}
#endif

      error = qexec_hash_join_fetch_key (thread_p, probe_fetch_info, &tuple_record, key, NULL /* compare_key */,
					 &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_end_tick);
	  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.fetch, profile_tv_diff);

	  tsc_getticks (&profile_start_tick);	/* stats->probe.profile.hash */
	}
#endif

      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else if (exit_on_next)
	{
	  /* Give up and read the next tuple. */
	  exit_on_next = false;
	  continue;
	}
      else
	{
	  /* fall through */
	}

      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);

      if (thread_is_on_trace (thread_p))
	{
#if HASH_JOIN_PROFILE_TIME
	  tsc_getticks (&profile_end_tick);
	  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.hash, profile_tv_diff);
#endif
	  max_collisions = 0;
	}

      do
	{
#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_start_tick);	/* probe.profile.search */
	    }
#endif

	  error = qexec_hash_join_probe_key (thread_p, hash_scan, &found_record, &build_scan_id->s.llsid.lsid);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_end_tick);
	      tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.search, profile_tv_diff);

	      tsc_getticks (&profile_start_tick);	/* probe.profile.match */
	    }
#endif

	  if (found_record.tpl == NULL)
	    {
	      /* The hash value was not found, so read the next tuple. */
	      break;
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      max_collisions++;
	    }

	  error = qexec_hash_join_fetch_key (thread_p, build_fetch_info, &found_record, found_key, key /* compare_key */,
					     &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_end_tick);
	      tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.match, profile_tv_diff);
	    }
#endif

	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  else if (exit_on_next)
	    {
#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
	      fprintf (stdout, "\nNot Matched Key (collisions): ");
	      qfile_print_tuple (&build_scan_id->s.llsid.list_id->type_list, found_record.tpl);
#endif

	      /* Give up and read the next tuple. */
	      exit_on_next = false;
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

	  if (context->remaining_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_start_tick);	/* probe.profile.match */
		}
#endif

	      error = fetch_val_list (thread_p, build_scan_id->s.llsid.scan_pred.regu_list, build_scan_id->vd, NULL, NULL,
				      found_record.tpl, PEEK);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      if (context->remaining_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, context->remaining_join_pred, build_scan_id->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_end_tick);
		  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
		  TSC_ADD_TIMEVAL (stats->probe.profile.match, profile_tv_diff);
		}
#endif

	      if (ev_res != V_TRUE)
		{
#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
		  fprintf (stdout, "\nNot Matched Key (join_pred): ");
		  qfile_print_tuple (&build_scan_id->s.llsid.list_id->type_list, found_record.tpl);
#endif

		  /* Give up and read the next hash value. */
		  continue;
		}
	    }

#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nMatched Key: ");
	  qfile_print_tuple (&build_scan_id->s.llsid.list_id->type_list, found_record.tpl);
#endif

#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_start_tick);	/* probe.profile.add */
	    }
#endif

	  error = qexec_hash_join_merge_tuple_add_list (thread_p, list_id, left_record, right_record, manager->merge_info,
		  &overflow_record);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (thread_is_on_trace (thread_p))
	    {
#if HASH_JOIN_PROFILE_TIME
	      tsc_getticks (&profile_end_tick);
	      tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.add, profile_tv_diff);
#endif
	      stats->probe.rows++;
	    }
	}
      while (true);

      if (thread_is_on_trace (thread_p))
	{
	  stats->probe.readkeys += max_collisions;
	  stats->probe.max_collisions = MAX (stats->probe.max_collisions, max_collisions);
	}
    }

exit_on_end:
  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->probe.elapsed_time, tv_diff);
      TSC_ADD_TIMEVAL (stats->probe.probe_time, tv_diff);
      stats->probe.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->probe.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->probe.fetch_time += (UINT64) ((perfmon_get_from_statistic (thread_p,
					    PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) - old_fetch_time) / 1000);
    }

  scan_end_scan (thread_p, build_scan_id);
  scan_end_scan (thread_p, probe_scan_id);

  if (overflow_record.tpl)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
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
qexec_hash_outer_join_probe (THREAD_ENTRY *thread_p, HJ_MANAGER *manager, HJ_CONTEXT *context,
			     SCAN_ID *build_scan_id, SCAN_ID *probe_scan_id, QFILE_LIST_ID *list_id)
{
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_TUPLE_RECORD found_record = { NULL, 0 };
  QFILE_TUPLE_RECORD overflow_record = { NULL, 0 };
  QFILE_TUPLE_RECORD *left_record;
  QFILE_TUPLE_RECORD *right_record;
  SCAN_CODE scan_code;

  HJ_FETCH_INFO *build_fetch_info, *probe_fetch_info;
  bool exit_on_next = false;
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

  HJ_STATS *stats = context->stats;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#if HASH_JOIN_PROFILE_TIME
  TSC_TICKS profile_start_tick, profile_end_tick;
  TSCTIMEVAL profile_tv_diff;
#endif
  UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;
  int max_collisions;
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

  error = qfile_reallocate_tuple (&overflow_record, DB_PAGESIZE);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);
      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
      old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC);
    }

  build_scan_id->s.llsid.tplrecp = &found_record;
  probe_scan_id->s.llsid.tplrecp = &tuple_record;

  while (true)
    {
      probe_scan_id->qualification = QPROC_QUALIFIED_OR_NOT;

      scan_code = scan_next_scan (thread_p, probe_scan_id);
      switch (scan_code)
	{
	case S_SUCCESS:
	  break;

	case S_END:
	  goto exit_on_end;

	case S_ERROR:
	  goto exit_on_error;

	default:
	  assert (false);
	  goto exit_on_error;
	}

#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
      qfile_print_tuple (&probe_scan_id->s.llsid.list_id->type_list, tuple_record.tpl);
#endif

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_start_tick);	/* probe.profile.fetch */
	}
#endif

      error = qexec_hash_join_fetch_key (thread_p, probe_fetch_info, &tuple_record, key, NULL /* compare_key */,
					 &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_end_tick);
	  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.fetch, profile_tv_diff);
	}
#endif

      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else if (exit_on_next)
	{
#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nFill Outer Key: ");
	  qfile_print_tuple (&probe_scan_id->s.llsid.list_id->type_list, tuple_record.tpl);
#endif

	  switch (context->join_type)
	    {
	    case JOIN_LEFT:
#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_start_tick);	/* probe.profile.add */
		}
#endif

	      error = qexec_hash_join_merge_tuple_add_list (thread_p, list_id, &tuple_record, NULL, manager->merge_info,
		      &overflow_record);

	      if (thread_is_on_trace (thread_p))
		{
#if HASH_JOIN_PROFILE_TIME
		  tsc_getticks (&profile_end_tick);
		  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
		  TSC_ADD_TIMEVAL (stats->probe.profile.add, profile_tv_diff);
#endif
		  stats->probe.rows++;
		}

	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      break;

	    case JOIN_RIGHT:
#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_start_tick);	/* probe.profile.add */
		}
#endif

	      error = qexec_hash_join_merge_tuple_add_list (thread_p, list_id, NULL, &tuple_record, manager->merge_info,
		      &overflow_record);

	      if (thread_is_on_trace (thread_p))
		{
#if HASH_JOIN_PROFILE_TIME
		  tsc_getticks (&profile_end_tick);
		  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
		  TSC_ADD_TIMEVAL (stats->probe.profile.add, profile_tv_diff);
#endif
		  stats->probe.rows++;
		}

	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      break;

	    case JOIN_INNER:
	      break;	/* exit_on_next */

	    case JOIN_OUTER:
	    /* FULL OUTER JOIN is not supported. */
	    default:
	      assert (false);
	      goto exit_on_error;
	    }

	  /* Give up and read the next tuple. */
	  exit_on_next = false;
	  continue;
	}
      else
	{
	  /* fall through */
	}

#if HASH_JOIN_PROFILE_TIME
      if (thread_is_on_trace (thread_p))
	{
	  tsc_getticks (&profile_start_tick);	/* stats->probe.profile.hash */
	}
#endif

      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);

      if (thread_is_on_trace (thread_p))
	{
#if HASH_JOIN_PROFILE_TIME
	  tsc_getticks (&profile_end_tick);
	  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	  TSC_ADD_TIMEVAL (stats->probe.profile.hash, profile_tv_diff);
#endif
	  max_collisions = 0;
	}

      any_record_added = false;

      do
	{
#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_start_tick);	/* probe.profile.search */
	    }
#endif

	  error = qexec_hash_join_probe_key (thread_p, hash_scan, &found_record, &build_scan_id->s.llsid.lsid);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_end_tick);
	      tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.search, profile_tv_diff);

	      tsc_getticks (&profile_start_tick);	/* probe.profile.match */
	    }
#endif

	  if (found_record.tpl == NULL)
	    {
	      /* The hash value was not found, so read the next tuple. */
	      break;
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      max_collisions++;
	    }

	  error = qexec_hash_join_fetch_key (thread_p, build_fetch_info, &found_record, found_key, key /* compare_key */,
					     &exit_on_next);

#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_end_tick);
	      tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.match, profile_tv_diff);
	    }
#endif

	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  else if (exit_on_next)
	    {
#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
	      fprintf (stdout, "\nNot Matched Key (collisions): ");
	      qfile_print_tuple (&build_scan_id->s.llsid.list_id->type_list, found_record.tpl);
#endif

	      /* Give up and read the next tuple. */
	      exit_on_next = false;
	      continue;
	    }
	  else
	    {
	      /* fall through */
	    }

	  if (context->during_join_pred != NULL || context->remaining_join_pred != NULL)
	    {
	      DB_LOGICAL ev_res;

#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_start_tick);	/* probe.profile.match */
		}
#endif

	      error = fetch_val_list (thread_p, build_scan_id->s.llsid.scan_pred.regu_list, build_scan_id->vd, NULL, NULL,
				      found_record.tpl, PEEK);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      if (context->during_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, context->during_join_pred, build_scan_id->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

	      if (context->remaining_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, context->remaining_join_pred, build_scan_id->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_end_tick);
		  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
		  TSC_ADD_TIMEVAL (stats->probe.profile.match, profile_tv_diff);
		}
#endif

	      if (ev_res != V_TRUE)
		{
#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
		  fprintf (stdout, "\nNot Matched Key (join_pred): ");
		  qfile_print_tuple (&build_scan_id->s.llsid.list_id->type_list, found_record.tpl);
#endif

		  /* Give up and read the next hash value. */
		  continue;
		}
	    }

#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nMatched Key: ");
	  qfile_print_tuple (&build_scan_id->s.llsid.list_id->type_list, found_record.tpl);
#endif

#if HASH_JOIN_PROFILE_TIME
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&profile_start_tick);	/* probe.profile.add */
	    }
#endif

	  error = qexec_hash_join_merge_tuple_add_list (thread_p, list_id, left_record, right_record, manager->merge_info,
		  &overflow_record);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  any_record_added = true;

	  if (thread_is_on_trace (thread_p))
	    {
#if HASH_JOIN_PROFILE_TIME
	      tsc_getticks (&profile_end_tick);
	      tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
	      TSC_ADD_TIMEVAL (stats->probe.profile.add, profile_tv_diff);
#endif
	      stats->probe.rows++;
	    }
	}
      while (true);

      if (thread_is_on_trace (thread_p))
	{
	  stats->probe.readkeys += max_collisions;
	  stats->probe.max_collisions = MAX (stats->probe.max_collisions, max_collisions);
	}

      if (!any_record_added)
	{
#if !defined(NDEBUG) && HASH_JOIN_DUMP_PROBE
	  fprintf (stdout, "\nFill Outer Key: ");
	  qfile_print_tuple (&probe_scan_id->s.llsid.list_id->type_list, tuple_record.tpl);
#endif

	  switch (context->join_type)
	    {
	    case JOIN_LEFT:
#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_start_tick);	/* probe.profile.add */
		}
#endif

	      error = qexec_hash_join_merge_tuple_add_list (thread_p, list_id, &tuple_record, NULL, manager->merge_info,
		      &overflow_record);

	      if (thread_is_on_trace (thread_p))
		{
#if HASH_JOIN_PROFILE_TIME
		  tsc_getticks (&profile_end_tick);
		  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
		  TSC_ADD_TIMEVAL (stats->probe.profile.add, profile_tv_diff);
#endif
		  stats->probe.rows++;
		}

	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      break;

	    case JOIN_RIGHT:
#if HASH_JOIN_PROFILE_TIME
	      if (thread_is_on_trace (thread_p))
		{
		  tsc_getticks (&profile_start_tick);	/* probe.profile.add */
		}
#endif

	      error = qexec_hash_join_merge_tuple_add_list (thread_p, list_id, NULL, &tuple_record, manager->merge_info,
		      &overflow_record);

	      if (thread_is_on_trace (thread_p))
		{
#if HASH_JOIN_PROFILE_TIME
		  tsc_getticks (&profile_end_tick);
		  tsc_elapsed_time_usec (&profile_tv_diff, profile_end_tick, profile_start_tick);
		  TSC_ADD_TIMEVAL (stats->probe.profile.add, profile_tv_diff);
#endif
		  stats->probe.rows++;
		}

	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      break;

	    case JOIN_INNER:
	      break;	/* exit_on_next */

	    case JOIN_OUTER:
	    /* FULL OUTER JOIN is not supported. */
	    default:
	      assert (false);
	      goto exit_on_error;
	    }
	}
    }

exit_on_end:
  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (stats->probe.elapsed_time, tv_diff);
      TSC_ADD_TIMEVAL (stats->probe.probe_time, tv_diff);
      stats->probe.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      stats->probe.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
      stats->probe.fetch_time += (UINT64) ((perfmon_get_from_statistic (thread_p,
					    PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) - old_fetch_time) / 1000);
    }

  scan_end_scan (thread_p, build_scan_id);
  scan_end_scan (thread_p, probe_scan_id);

  if (overflow_record.tpl)
    {
      db_private_free_and_init (thread_p, overflow_record.tpl);
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
qexec_hash_join_probe_key (THREAD_ENTRY *thread_p, HASH_LIST_SCAN *hash_scan, QFILE_TUPLE_RECORD *tuple_record,
			   QFILE_LIST_SCAN_ID *list_scan_id)
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
	  hash_value = (HASH_SCAN_VALUE *) mht_get_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
		       (void **) &hash_scan->memory.curr_hash_entry);
	}
      else
	{
	  hash_value = (HASH_SCAN_VALUE *) mht_get_next_hls (hash_scan->memory.hash_table, (void *) &hash_scan->curr_hash_key,
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
	  MAKE_TFTID_TO_TUPLE_POSTION (tuple_position, tftid, list_scan_id);

	  qp_scan = qfile_jump_scan_tuple_position (thread_p, list_scan_id, &tuple_position, tuple_record, PEEK);
	  if (qp_scan != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }
	  break;

	case EH_KEY_NOTFOUND:
	  tuple_record->tpl = NULL;
	  tuple_record->size = 0;

	  /* The hash value was not found, so read the next tuple. */
	  goto exit_on_end;

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
qexec_hash_join_merge_tuple_add_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id, QFILE_TUPLE_RECORD *tplrec1,
				      QFILE_TUPLE_RECORD *tplrec2, QFILE_LIST_MERGE_INFO *merge_info,
				      QFILE_TUPLE_RECORD *tplrec)
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
qexec_hash_join_merge_tuple (QFILE_TUPLE_RECORD *tplrec1, QFILE_TUPLE_RECORD *tplrec2,
			     QFILE_LIST_MERGE_INFO *merge_info, QFILE_TUPLE_RECORD *tplrec)
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
qexec_hash_join_size_remaining (QFILE_TUPLE_RECORD *tplrec1, QFILE_TUPLE_RECORD *tplrec2,
				QFILE_LIST_MERGE_INFO *merge_info, int k)
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
