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

extern qfile_list_id *qexec_execute_query (THREAD_ENTRY * thread_p, xasl_node * xasl, int dbval_cnt,
					   const DB_VALUE * dbval_ptr, QUERY_ID query_id);
extern int qexec_execute_mainblock (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate,
				    UPDDEL_CLASS_INSTANCE_LOCK_INFO * p_class_instance_lock_info);
extern int qexec_execute_subquery_for_result_cache (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate);
extern int qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate);
extern int qexec_clear_xasl (THREAD_ENTRY * thread_p, xasl_node * xasl, bool is_final);
extern int qexec_clear_pred_context (THREAD_ENTRY * thread_p, pred_expr_with_context * pred_filter,
				     bool dealloc_dbvalues);
extern int qexec_clear_func_pred (THREAD_ENTRY * thread_p, func_pred * pred_filter);
extern int qexec_clear_partition_expression (THREAD_ENTRY * thread_p, regu_variable_node * expr);

extern qfile_list_id *qexec_get_xasl_list_id (xasl_node * xasl);
#if defined(CUBRID_DEBUG)
extern void get_xasl_dumper_linked_in ();
#endif

extern int qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p, const OID * class_oid);

#if defined(CUBRID_DEBUG)
extern bool qdump_check_xasl_tree (xasl_node * xasl);
#endif /* CUBRID_DEBUG */

extern int qexec_get_tuple_column_value (QFILE_TUPLE tpl, int index, DB_VALUE * valp, tp_domain * domain);
extern int qexec_insert_tuple_into_list (THREAD_ENTRY * thread_p, qfile_list_id * list_id,
					 valptr_list_node * outptr_list, val_descr * vd, qfile_tuple_record * tplrec);
extern void qexec_replace_prior_regu_vars_prior_expr (THREAD_ENTRY * thread_p, regu_variable_node * regu,
						      xasl_node * xasl, xasl_node * connect_by_ptr);
#endif /* _QUERY_EXECUTOR_H_ */
