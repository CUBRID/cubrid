/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

#include <time.h>

#include "xasl.h"

#define QEXEC_NULL_COMMAND_ID   -1	/* Invalid command identifier */

typedef struct upddel_class_instances_lock_info UPDDEL_CLASS_INSTANCE_LOCK_INFO;
struct upddel_class_instances_lock_info
{
  OID class_oid;
  bool instances_locked;
};

extern QFILE_LIST_ID *qexec_execute_query (THREAD_ENTRY * thread_p, XASL_NODE * xasl, int dbval_cnt,
					   const DB_VALUE * dbval_ptr, QUERY_ID query_id);
extern int qexec_execute_mainblock (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				    UPDDEL_CLASS_INSTANCE_LOCK_INFO * p_class_instance_lock_info);
extern int qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
extern int qexec_clear_xasl (THREAD_ENTRY * thread_p, XASL_NODE * xasl, bool is_final);
extern int qexec_clear_pred_context (THREAD_ENTRY * thread_p, PRED_EXPR_WITH_CONTEXT * pred_filter,
				     bool dealloc_dbvalues);
extern int qexec_clear_func_pred (THREAD_ENTRY * thread_p, FUNC_PRED * pred_filter);
extern int qexec_clear_partition_expression (THREAD_ENTRY * thread_p, REGU_VARIABLE * expr);

extern QFILE_LIST_ID *qexec_get_xasl_list_id (XASL_NODE * xasl);
#if defined(CUBRID_DEBUG)
extern void get_xasl_dumper_linked_in ();
#endif

extern int qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p, const OID * class_oid);

#if defined(CUBRID_DEBUG)
extern bool qdump_check_xasl_tree (XASL_NODE * xasl);
#endif /* CUBRID_DEBUG */

extern int qexec_get_tuple_column_value (QFILE_TUPLE tpl, int index, DB_VALUE * valp, TP_DOMAIN * domain);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int qexec_set_tuple_column_value (QFILE_TUPLE tpl, int index, DB_VALUE * valp, TP_DOMAIN * domain);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int qexec_insert_tuple_into_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, OUTPTR_LIST * outptr_list,
					 VAL_DESCR * vd, QFILE_TUPLE_RECORD * tplrec);
extern void qexec_replace_prior_regu_vars_prior_expr (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu, XASL_NODE * xasl,
						      XASL_NODE * connect_by_ptr);

#endif /* _QUERY_EXECUTOR_H_ */
