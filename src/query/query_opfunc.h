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


#ifndef _QUERY_OPFUNC_H_
#define _QUERY_OPFUNC_H_

#ident "$Id$"

#include "oid.h"
#include "dbtype.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "heap_file.h"
#include "query_evaluator.h"
#include "parse_tree.h"
#include "query_list.h"

#define UNBOUND(x) ((x)->val_flag == V_UNBOUND || (x)->type == DB_TYPE_NULL)

#define BOUND(x) (! UNBOUND(x))

#define SECONDS_OF_ONE_DAY      86400	/* 24 * 60 * 60 */
#define MILLISECONDS_OF_ONE_DAY 86400000	/* 24 * 60 * 60 * 1000 */

typedef enum
{
  VACOMM_BUFFER_SEND = 1,
  VACOMM_BUFFER_ABORT
} VACOMM_BUFFER_CLIENT_ACTION;

typedef enum
{				/* Responses to a query */
  QUERY_END = 1,		/* Normal end of query */
  METHOD_CALL,			/* Invoke methods */
  ASYNC_OBTAIN_USER_INPUT,	/* server needs info from operator */
  GET_NEXT_LOG_PAGES,		/* log writer uses this type of request */
  END_CALLBACK,			/* normal end of non-query callback */
  CONSOLE_OUTPUT
} QUERY_SERVER_REQUEST;

typedef enum
{
  METHOD_SUCCESS = 1,
  METHOD_EOF,
  METHOD_ERROR
} METHOD_CALL_STATUS;

#define VACOMM_BUFFER_HEADER_SIZE           (OR_INT_SIZE * 3)
#define VACOMM_BUFFER_HEADER_LENGTH_OFFSET  (0)
#define VACOMM_BUFFER_HEADER_STATUS_OFFSET  (OR_INT_SIZE)
#define VACOMM_BUFFER_HEADER_NO_VALS_OFFSET (OR_INT_SIZE * 2)
#define VACOMM_BUFFER_HEADER_ERROR_OFFSET   (OR_INT_SIZE * 2)

typedef enum
{
  METHOD_IS_INSTANCE_METHOD = 1,
  METHOD_IS_CLASS_METHOD
} METHOD_TYPE;

typedef struct method_sig_node METHOD_SIG;
struct method_sig_node
{				/* method signature */
  struct method_sig_node *next;
  char *method_name;		/* method name */
  char *class_name;		/* class for the method */
  METHOD_TYPE method_type;	/* instance or class method */
  int num_method_args;		/* number of arguments */
  int *method_arg_pos;		/* arg position in list file */
};

typedef struct method_sig_list METHOD_SIG_LIST;
struct method_sig_list
{				/* signature for methods */
  METHOD_SIG *method_sig;	/* one method signature */
  int num_methods;		/* number of signatures */
};

typedef enum
{
  SEQUENTIAL,			/* sequential scan access */
  INDEX,			/* indexed access */
  SCHEMA,			/* schema access */
  SEQUENTIAL_RECORD_INFO,	/* sequential scan that will read record info */
  SEQUENTIAL_PAGE_SCAN,		/* sequential scan access that only scans pages without accessing record data */
  INDEX_KEY_INFO,		/* indexed access to obtain key information */
  INDEX_NODE_INFO		/* indexed access to obtain b-tree node info */
} ACCESS_METHOD;

#define IS_ANY_INDEX_ACCESS(access_) \
  ((access_) == INDEX || (access_) == INDEX_KEY_INFO || (access_) == INDEX_NODE_INFO)

typedef enum
{
  NO_SCHEMA,
  INDEX_SCHEMA,
  COLUMNS_SCHEMA,
  FULL_COLUMNS_SCHEMA
} ACCESS_SCHEMA_TYPE;

typedef enum
{
  QPROC_QUALIFIED = 0,		/* fetch a qualified item; default */
  QPROC_NOT_QUALIFIED,		/* fetch a not-qualified item */
  QPROC_QUALIFIED_OR_NOT	/* fetch either a qualified or not-qualified item */
} QPROC_QUALIFICATION;

typedef enum
{
  QPROC_TPLDESCR_SUCCESS = 1,	/* success generating tuple descriptor */
  QPROC_TPLDESCR_FAILURE = 0,	/* error, give up */
  QPROC_TPLDESCR_RETRY_SET_TYPE = -1,	/* error, retry for SET data-type */
  QPROC_TPLDESCR_RETRY_BIG_REC = -2	/* error, retry for BIG RECORD */
} QPROC_TPLDESCR_STATUS;

typedef struct xasl_node XASL_NODE;
typedef struct pred_expr_with_context PRED_EXPR_WITH_CONTEXT;


typedef struct xasl_stream XASL_STREAM;
struct xasl_stream
{
  XASL_ID *xasl_id;
  XASL_NODE_HEADER *xasl_header;

  char *xasl_stream;
  int xasl_stream_size;
};


/*
 * EXECUTION_INFO is used for receive server execution info
 * If you want get another info from server (ex. server statdump)
 * add item in this structure
 */
typedef struct execution_info EXECUTION_INFO;
struct execution_info
{
  char *sql_hash_text;		/* rewrited query string which is used as hash key */
  char *sql_user_text;		/* original query statement that user input */
  char *sql_plan_text;		/* plans for this query */
};
#define EXEINFO_HASH_TEXT_STRING(einfo) ((einfo)->sql_hash_text ? (einfo)->sql_hash_text : "UNKNOWN HASH TEXT")
#define EXEINFO_USER_TEXT_STRING(einfo) ((einfo)->sql_user_text ? (einfo)->sql_user_text : "UNKNOWN USER TEXT")
#define EXEINFO_PLAN_TEXT_STRING(einfo) ((einfo)->sql_plan_text ? (einfo)->sql_plan_text : "UNKNOWN PLAN TEXT")

#define EXEINFO_AS_ARGS(einfo)	\
  EXEINFO_USER_TEXT_STRING(einfo), EXEINFO_PLAN_TEXT_STRING(einfo), EXEINFO_HASH_TEXT_STRING(einfo)

/* Object for enabling performing aggregate optimizations on class
 * hierarchies
 */
typedef struct hierarchy_aggregate_helper HIERARCHY_AGGREGATE_HELPER;
struct hierarchy_aggregate_helper
{
  BTID *btids;			/* hierarchy indexes */
  HFID *hfids;			/* HFIDs for classes in the hierarchy */
  int count;			/* number of classes in the hierarchy */
};

extern void qdata_set_value_list_to_null (VAL_LIST * val_list);
extern int qdata_copy_db_value (DB_VALUE * dbval1, DB_VALUE * dbval2);

extern int qdata_copy_db_value_to_tuple_value (DB_VALUE * dbval, bool clear_compressed_string, char *tvalp,
					       int *tval_size);
extern int qdata_copy_valptr_list_to_tuple (THREAD_ENTRY * thread_p, VALPTR_LIST * valptr_list, VAL_DESCR * vd,
					    QFILE_TUPLE_RECORD * tplrec);
extern QPROC_TPLDESCR_STATUS qdata_generate_tuple_desc_for_valptr_list (THREAD_ENTRY * thread_p,
									VALPTR_LIST * valptr_list, VAL_DESCR * vd,
									QFILE_TUPLE_DESCRIPTOR * tdp);
extern int qdata_set_valptr_list_unbound (THREAD_ENTRY * thread_p, VALPTR_LIST * valptr_list, VAL_DESCR * vd);

extern int qdata_add_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_concatenate_dbval (THREAD_ENTRY * thread_p, DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res,
				    TP_DOMAIN * domain, const int max_allowed_size, const char *warning_context);
extern int qdata_increment_dbval (DB_VALUE * dbval1, DB_VALUE * res, int incval);
extern int qdata_subtract_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_multiply_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_divide_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_unary_minus_dbval (DB_VALUE * res, DB_VALUE * dbval1);
extern int qdata_extract_dbval (const MISC_OPERAND extr_operand, DB_VALUE * dbval, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_strcat_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_initialize_aggregate_list (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list, QUERY_ID query_id);
extern int qdata_aggregate_value_to_accumulator (THREAD_ENTRY * thread_p, AGGREGATE_ACCUMULATOR * acc,
						 AGGREGATE_ACCUMULATOR_DOMAIN * domain, FUNC_TYPE func_type,
						 TP_DOMAIN * func_domain, DB_VALUE * value);
extern int qdata_aggregate_accumulator_to_accumulator (THREAD_ENTRY * thread_p, AGGREGATE_ACCUMULATOR * acc,
						       AGGREGATE_ACCUMULATOR_DOMAIN * acc_dom, FUNC_TYPE func_type,
						       TP_DOMAIN * func_domain, AGGREGATE_ACCUMULATOR * new_acc);
extern int qdata_evaluate_aggregate_list (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list, VAL_DESCR * vd,
					  AGGREGATE_ACCUMULATOR * alt_acc_list);
extern int qdata_evaluate_aggregate_optimize (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_ptr, HFID * hfid,
					      OID * partition_cls_oid);
extern int qdata_evaluate_aggregate_hierarchy (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_ptr, HFID * root_hfid,
					       BTID * root_btid, HIERARCHY_AGGREGATE_HELPER * helper);
extern int qdata_finalize_aggregate_list (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list, bool keep_list_file);
extern int qdata_initialize_analytic_func (THREAD_ENTRY * thread_p, ANALYTIC_TYPE * func_p, QUERY_ID query_id);
extern int qdata_evaluate_analytic_func (THREAD_ENTRY * thread_p, ANALYTIC_TYPE * func_p, VAL_DESCR * vd);
extern int qdata_finalize_analytic_func (THREAD_ENTRY * thread_p, ANALYTIC_TYPE * func_p, bool is_same_group);
extern int qdata_get_single_tuple_from_list_id (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
						VAL_LIST * single_tuple);
extern int qdata_get_valptr_type_list (THREAD_ENTRY * thread_p, VALPTR_LIST * valptr_list,
				       QFILE_TUPLE_VALUE_TYPE_LIST * type_list);
extern int qdata_evaluate_function (THREAD_ENTRY * thread_p, REGU_VARIABLE * func, VAL_DESCR * vd, OID * obj_oid,
				    QFILE_TUPLE tpl);

extern void regu_set_error_with_zero_args (int err_type);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void regu_set_error_with_one_args (int err_type, const char *infor);
#endif
extern void regu_set_global_error (void);

extern int prepare_query (COMPILE_CONTEXT * context, XASL_STREAM * stream);
extern int execute_query (const XASL_ID * xasl_id, QUERY_ID * query_idp, int var_cnt, const DB_VALUE * varptr,
			  QFILE_LIST_ID ** list_idp, QUERY_FLAG flag, CACHE_TIME * clt_cache_time,
			  CACHE_TIME * srv_cache_time);
extern int prepare_and_execute_query (char *stream, int stream_size, QUERY_ID * query_id, int var_cnt,
				      DB_VALUE * varptr, QFILE_LIST_ID ** result, QUERY_FLAG flag);

extern bool qdata_evaluate_connect_by_root (THREAD_ENTRY * thread_p, void *xasl_p, REGU_VARIABLE * regu_p,
					    DB_VALUE * result_val_p, VAL_DESCR * vd);
extern bool qdata_evaluate_qprior (THREAD_ENTRY * thread_p, void *xasl_p, REGU_VARIABLE * regu_p,
				   DB_VALUE * result_val_p, VAL_DESCR * vd);
extern bool qdata_evaluate_sys_connect_by_path (THREAD_ENTRY * thread_p, void *xasl_p, REGU_VARIABLE * regu_p,
						DB_VALUE * value_char, DB_VALUE * result_p, VAL_DESCR * vd);
extern int qdata_bit_not_dbval (DB_VALUE * dbval, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_bit_and_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_bit_or_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_bit_xor_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, TP_DOMAIN * domain);
extern int qdata_bit_shift_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, OPERATOR_TYPE op, DB_VALUE * res,
				  TP_DOMAIN * domain);
extern int qdata_divmod_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, OPERATOR_TYPE op, DB_VALUE * res,
			       TP_DOMAIN * domain);

extern int qdata_list_dbs (THREAD_ENTRY * thread_p, DB_VALUE * result_p, TP_DOMAIN * domain_p);
extern int qdata_regu_list_to_regu_array (FUNCTION_TYPE * function_p, const int array_size,
					  REGU_VARIABLE * regu_array[], int *num_regu);
extern int qdata_get_cardinality (THREAD_ENTRY * thread_p, DB_VALUE * db_class_name, DB_VALUE * db_index_name,
				  DB_VALUE * db_key_position, DB_VALUE * result_p);
extern int qdata_tuple_to_values_array (THREAD_ENTRY * thread_p, QFILE_TUPLE_DESCRIPTOR * tuple, DB_VALUE ** values);
extern int qdata_get_tuple_value_size_from_dbval (DB_VALUE * dbval_p);
extern int qdata_apply_interpolation_function_coercion (DB_VALUE * f_value, TP_DOMAIN ** result_dom, DB_VALUE * result,
							FUNC_TYPE function);
extern int qdata_interpolation_function_values (DB_VALUE * f_value, DB_VALUE * c_value, double row_num_d,
						double f_row_num_d, double c_row_num_d, TP_DOMAIN ** result_dom,
						DB_VALUE * result, FUNC_TYPE function);
extern int qdata_get_interpolation_function_result (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id,
						    TP_DOMAIN * domain, int pos, double row_num_d, double f_row_num_d,
						    double c_row_num_d, DB_VALUE * result, TP_DOMAIN ** result_dom,
						    FUNC_TYPE function);
extern int qdata_update_interpolation_func_value_and_domain (DB_VALUE * src_val, DB_VALUE * dest_val,
							     TP_DOMAIN ** domain);

/* hash aggregate evaluation routines */
extern AGGREGATE_HASH_KEY *qdata_alloc_agg_hkey (THREAD_ENTRY * thread_p, int val_cnt, bool alloc_vals);
extern void qdata_free_agg_hkey (THREAD_ENTRY * thread_p, AGGREGATE_HASH_KEY * key);
extern AGGREGATE_HASH_VALUE *qdata_alloc_agg_hvalue (THREAD_ENTRY * thread_p, int func_cnt);
extern void qdata_free_agg_hvalue (THREAD_ENTRY * thread_p, AGGREGATE_HASH_VALUE * value);
extern int qdata_get_agg_hkey_size (AGGREGATE_HASH_KEY * key);
extern int qdata_get_agg_hvalue_size (AGGREGATE_HASH_VALUE * value, bool ret_delta);
extern int qdata_free_agg_hentry (const void *key, void *data, void *args);
extern unsigned int qdata_hash_agg_hkey (const void *key, unsigned int ht_size);
extern DB_VALUE_COMPARE_RESULT qdata_agg_hkey_compare (AGGREGATE_HASH_KEY * ckey1, AGGREGATE_HASH_KEY * ckey2,
						       int *diff_pos);
extern int qdata_agg_hkey_eq (const void *key1, const void *key2);
extern AGGREGATE_HASH_KEY *qdata_copy_agg_hkey (THREAD_ENTRY * thread_p, AGGREGATE_HASH_KEY * key);
extern void qdata_load_agg_hvalue_in_agg_list (AGGREGATE_HASH_VALUE * value, AGGREGATE_TYPE * agg_list, bool copy_vals);
extern int qdata_save_agg_hentry_to_list (THREAD_ENTRY * thread_p, AGGREGATE_HASH_KEY * key,
					  AGGREGATE_HASH_VALUE * value, DB_VALUE * temp_dbval_array,
					  QFILE_LIST_ID * list_id);
extern int qdata_load_agg_hentry_from_tuple (THREAD_ENTRY * thread_p, QFILE_TUPLE tuple, AGGREGATE_HASH_KEY * key,
					     AGGREGATE_HASH_VALUE * value, TP_DOMAIN ** key_dom,
					     AGGREGATE_ACCUMULATOR_DOMAIN ** acc_dom);
extern SCAN_CODE qdata_load_agg_hentry_from_list (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * list_scan_id,
						  AGGREGATE_HASH_KEY * key, AGGREGATE_HASH_VALUE * value,
						  TP_DOMAIN ** key_dom, AGGREGATE_ACCUMULATOR_DOMAIN ** acc_dom);
extern int qdata_save_agg_htable_to_list (THREAD_ENTRY * thread_p, MHT_TABLE * hash_table,
					  QFILE_LIST_ID * tuple_list_id, QFILE_LIST_ID * partial_list_id,
					  DB_VALUE * temp_dbval_array);
#endif /* _QUERY_OPFUNC_H_ */
