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
 * XASL (eXtented Access Specification Language) interpreter internal
 * definitions.
 * For a brief description of ASL principles see "Access Path Selection in a
 * Relational Database Management System" by P. Griffiths Selinger et al
 */

#ifndef _QUERY_EXECUTOR_H_
#define _QUERY_EXECUTOR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "query_list.h"
#include "system.h"
#include "thread_compat.hpp"

#include <time.h>

// forward definitions
struct func_pred;
struct pred_expr_with_context;
struct qfile_list_id;
struct qfile_tuple_record;
class regu_variable_node;
struct tp_domain;
struct valptr_list_node;
struct xasl_node;
struct xasl_state;
using XASL_STATE = xasl_state;

#define QEXEC_NULL_COMMAND_ID   -1	/* Invalid command identifier */

typedef enum
{
  TOPN_SUCCESS,
  TOPN_OVERFLOW,
  TOPN_FAILURE
} TOPN_STATUS;

struct topn_tuples;
typedef struct topn_tuples TOPN_TUPLES;

typedef struct upddel_class_instances_lock_info UPDDEL_CLASS_INSTANCE_LOCK_INFO;
struct upddel_class_instances_lock_info
{
  OID class_oid;
  bool instances_locked;
};

typedef struct val_descr VAL_DESCR;
struct val_descr
{
  DB_VALUE *dbval_ptr;		/* Array of values */
  int dbval_cnt;		/* Value Count */
  DB_DATETIME sys_datetime;
  DB_TIMESTAMP sys_epochtime;
  long lrand;
  double drand;
  XASL_STATE *xasl_state;	/* XASL_STATE pointer */
};				/* Value Descriptor */

// XASL_STATE
typedef struct xasl_state XASL_STATE;
struct xasl_state
{
  VAL_DESCR vd;			/* Value Descriptor */
  QUERY_ID query_id;		/* Query associated with XASL */
  int qp_xasl_line;		/* Error line */
};

extern qfile_list_id *qexec_execute_query (THREAD_ENTRY * thread_p, xasl_node * xasl, int dbval_cnt,
					   const DB_VALUE * dbval_ptr, QUERY_ID query_id);
extern int qexec_execute_mainblock (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate,
				    UPDDEL_CLASS_INSTANCE_LOCK_INFO * p_class_instance_lock_info);
extern int qexec_execute_subquery_for_result_cache (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate);
extern int qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate);
extern int qexec_clear_xasl (THREAD_ENTRY * thread_p, xasl_node * xasl, bool is_final, bool for_parallel_aptr);
extern void qexec_clear_topn_items (THREAD_ENTRY * thread_p, xasl_node * xasl);
extern int qexec_clear_pred_context (THREAD_ENTRY * thread_p, pred_expr_with_context * pred_filter,
				     bool dealloc_dbvalues);
extern int qexec_clear_func_pred (THREAD_ENTRY * thread_p, func_pred * pred_filter);
extern int qexec_clear_partition_expression (THREAD_ENTRY * thread_p, regu_variable_node * expr);
extern int qexec_resolve_domains_for_aggregation_for_parallel_heap_scan_g_agg (THREAD_ENTRY * thread_p,
									       xasl_node * xasl, void *vd,
									       int *resolved);
extern int qexec_resolve_domains_for_aggregation_for_parallel_heap_scan_buildvalue_proc (THREAD_ENTRY * thread_p,
											 xasl_node * xasl, void *vd,
											 int *resolved);
extern int qexec_clear_xasl_for_parallel_aptr (THREAD_ENTRY * thread_p, xasl_node * xasl, bool is_final);
extern qfile_list_id *qexec_get_xasl_list_id (xasl_node * xasl);
/* merge join primitives shared with the parallel merge (px_merge_join) */
extern DB_VALUE_COMPARE_RESULT qexec_cmp_tpl_vals_merge (QFILE_TUPLE_RECORD * left, int *left_ind,
							 tp_domain ** left_dom, QFILE_TUPLE_RECORD * rght,
							 int *rght_ind, tp_domain ** rght_dom, int tval_cnt);

/* one input side (outer or inner) of an inner merge */
typedef struct qexec_merge_side QEXEC_MERGE_SIDE;
struct qexec_merge_side
{
  QFILE_LIST_ID *list_id;
  QFILE_LIST_SCAN_ID sid;
  QFILE_TUPLE_RECORD tplrec;
  int *indp;			/* merge column positions, borrowed from QFILE_LIST_MERGE_INFO */
  tp_domain **domp;
  char **valp;
  int *lenp;			/* 0 == NULL */
  int nvals;
  SCAN_CODE scan;		/* result of the last scan move */
};

extern int qexec_merge_side_init (THREAD_ENTRY * thread_p, QEXEC_MERGE_SIDE * side, QFILE_LIST_ID * list_id, int *indp,
				  int nvals);
extern void qexec_merge_side_clear (THREAD_ENTRY * thread_p, QEXEC_MERGE_SIDE * side);
extern void qexec_merge_side_pvals (QEXEC_MERGE_SIDE * side);
extern SCAN_CODE qexec_merge_side_next (THREAD_ENTRY * thread_p, QEXEC_MERGE_SIDE * side);
extern SCAN_CODE qexec_merge_side_prev (THREAD_ENTRY * thread_p, QEXEC_MERGE_SIDE * side);
extern SCAN_CODE qexec_merge_side_jump (THREAD_ENTRY * thread_p, QEXEC_MERGE_SIDE * side, QFILE_TUPLE_POSITION * pos);
extern SCAN_CODE qexec_merge_side_skip_null_keys (THREAD_ENTRY * thread_p, QEXEC_MERGE_SIDE * side);
extern DB_VALUE_COMPARE_RESULT qexec_merge_side_cmp (QEXEC_MERGE_SIDE * outer, QEXEC_MERGE_SIDE * inner);
extern SCAN_CODE qexec_merge_group_forward (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_idp,
					    QFILE_LIST_MERGE_INFO * merge_infop, QFILE_TUPLE_RECORD * tplrec,
					    QEXEC_MERGE_SIDE * outer, QEXEC_MERGE_SIDE * inner, int group_cnt,
					    int *cnt_out, DB_VALUE_COMPARE_RESULT * val_cmp);
extern int qexec_merge_group_backward (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_idp,
				       QFILE_LIST_MERGE_INFO * merge_infop, QFILE_TUPLE_RECORD * tplrec,
				       QEXEC_MERGE_SIDE * outer, QEXEC_MERGE_SIDE * inner, int group_cnt);

/* optional caller hooks of qexec_merge_inner_loop; NULL hooks == the plain serial merge */
typedef struct qexec_merge_hooks QEXEC_MERGE_HOOKS;
struct qexec_merge_hooks
{
  /* called right after a side moved to a new tuple outside a key-group walk; S_END ends the merge normally,
   * S_ERROR aborts it */
  SCAN_CODE (*after_advance) (THREAD_ENTRY * thread_p, QEXEC_MERGE_SIDE * side, void *arg);
  /* polled at the top of every loop iteration; true aborts the merge */
  bool (*should_stop) (THREAD_ENTRY * thread_p, void *arg);
  void *arg;
};

extern int qexec_merge_inner_loop (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_idp,
				   QFILE_LIST_MERGE_INFO * merge_infop, QFILE_TUPLE_RECORD * tplrec,
				   QEXEC_MERGE_SIDE * outer, QEXEC_MERGE_SIDE * inner, const QEXEC_MERGE_HOOKS * hooks);
extern xasl_state *qexec_deep_copy_xasl_state (THREAD_ENTRY * thread_p, xasl_state * xasl_state);
extern void qexec_free_xasl_state (THREAD_ENTRY * thread_p, xasl_state * xasl_state);
#if defined(CUBRID_DEBUG)
extern void get_xasl_dumper_linked_in ();
#endif

extern int qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p, const OID * class_oid);

#if defined(CUBRID_DEBUG)
extern bool qdump_check_xasl_tree (xasl_node * xasl);
#endif /* CUBRID_DEBUG */

extern int qexec_get_tuple_column_value (QFILE_TUPLE_RECORD * tplrec, int index, DB_VALUE * valp, tp_domain * domain);
extern int qexec_insert_tuple_into_list (THREAD_ENTRY * thread_p, qfile_list_id * list_id,
					 valptr_list_node * outptr_list, val_descr * vd, qfile_tuple_record * tplrec);
extern void qexec_replace_prior_regu_vars_prior_expr (THREAD_ENTRY * thread_p, regu_variable_node * regu,
						      xasl_node * xasl, xasl_node * connect_by_ptr);
extern SCAN_CODE qexec_execute_scan_ptr (THREAD_ENTRY * thread_p, xasl_node * xasl, XASL_STATE * xasl_state,
					 void *scan_func_ptr);
extern SCAN_CODE qexec_reset_sa_inner_scan_block (THREAD_ENTRY * thread_p, xasl_node * inner);
extern int qexec_execute_dptr_list (THREAD_ENTRY * thread_p, xasl_node * dptr_list, xasl_state * xstate, bool truncate);
extern void qexec_clear_scan_all_lists (THREAD_ENTRY * thread_p, xasl_node * xasl_list);
extern int qexec_alloc_agg_hash_context_buildlist_xasl (THREAD_ENTRY * thread_p, xasl_node * xasl,
							XASL_STATE * xasl_state, bool not_use_membuf);
extern int qexec_hash_gby_agg_tuple_public (THREAD_ENTRY * thread_p, xasl_node * xasl, XASL_STATE * xasl_state,
					    QFILE_TUPLE_RECORD * tplrec, QFILE_TUPLE_DESCRIPTOR * tpldesc,
					    QFILE_LIST_ID * groupby_list, bool * output_tuple);
extern void qexec_mark_aggregate_operand_expressions (xasl_node * xasl);
extern int qexec_setup_topn_proc (THREAD_ENTRY * thread_p, xasl_node * xasl, VAL_DESCR * vd);
extern TOPN_STATUS qexec_add_tuple_to_topn (THREAD_ENTRY * thread_p, TOPN_TUPLES * topn_items,
					    QFILE_TUPLE_DESCRIPTOR * tpldescr);
extern int qexec_topn_tuples_to_list_id (THREAD_ENTRY * thread_p, xasl_node * xasl, XASL_STATE * xasl_state,
					 bool is_final, QFILE_LIST_ID * merged_results);
#endif /* _QUERY_EXECUTOR_H_ */
