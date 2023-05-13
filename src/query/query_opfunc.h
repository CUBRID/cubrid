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

#ifndef _QUERY_OPFUNC_H_
#define _QUERY_OPFUNC_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "db_function.hpp"
#include "query_list.h"
#include "storage_common.h"
#include "string_opfunc.h"
#include "thread_compat.hpp"

// forward definitions
struct function_node;
class regu_variable_node;
struct tp_domain;
struct val_descr;
struct val_list_node;
struct valptr_list_node;
struct xasl_state;

#define UNBOUND(x) ((x)->val_flag == V_UNBOUND || (x)->type == DB_TYPE_NULL)

#define BOUND(x) (! UNBOUND(x))

typedef enum
{
  QPROC_TPLDESCR_SUCCESS = 1,	/* success generating tuple descriptor */
  QPROC_TPLDESCR_FAILURE = 0,	/* error, give up */
  QPROC_TPLDESCR_RETRY_SET_TYPE = -1,	/* error, retry for SET data-type */
  QPROC_TPLDESCR_RETRY_BIG_REC = -2	/* error, retry for BIG RECORD */
} QPROC_TPLDESCR_STATUS;

extern void qdata_set_value_list_to_null (val_list_node * val_list);
extern bool qdata_copy_db_value (DB_VALUE * dbval1, const DB_VALUE * dbval2);

extern int qdata_copy_db_value_to_tuple_value (DB_VALUE * dbval, bool clear_compressed_string, char *tvalp,
					       int *tval_size);
extern int qdata_copy_valptr_list_to_tuple (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list, val_descr * vd,
					    qfile_tuple_record * tplrec);
extern QPROC_TPLDESCR_STATUS qdata_generate_tuple_desc_for_valptr_list (THREAD_ENTRY * thread_p,
									valptr_list_node * valptr_list, val_descr * vd,
									qfile_tuple_descriptor * tdp);
extern int qdata_set_valptr_list_unbound (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list, val_descr * vd);

extern int qdata_add_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);
extern int qdata_concatenate_dbval (THREAD_ENTRY * thread_p, DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res,
				    tp_domain * domain, const int max_allowed_size, const char *warning_context);
extern int qdata_increment_dbval (DB_VALUE * dbval1, DB_VALUE * res, int incval);
extern int qdata_subtract_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);
extern int qdata_multiply_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);
extern int qdata_divide_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);
extern int qdata_unary_minus_dbval (DB_VALUE * res, DB_VALUE * dbval1);
extern int qdata_extract_dbval (const MISC_OPERAND extr_operand, DB_VALUE * dbval, DB_VALUE * res, tp_domain * domain);
extern int qdata_strcat_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);

extern int qdata_get_single_tuple_from_list_id (THREAD_ENTRY * thread_p, qfile_list_id * list_id,
						val_list_node * single_tuple);
extern int qdata_get_valptr_type_list (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list,
				       qfile_tuple_value_type_list * type_list);
extern int qdata_evaluate_function (THREAD_ENTRY * thread_p, regu_variable_node * func, val_descr * vd, OID * obj_oid,
				    QFILE_TUPLE tpl);


#if defined (ENABLE_UNUSED_FUNCTION)
extern void regu_set_error_with_one_args (int err_type, const char *infor);
#endif
extern void regu_set_global_error (void);

extern bool qdata_evaluate_connect_by_root (THREAD_ENTRY * thread_p, void *xasl_p, regu_variable_node * regu_p,
					    DB_VALUE * result_val_p, val_descr * vd);
extern bool qdata_evaluate_qprior (THREAD_ENTRY * thread_p, void *xasl_p, regu_variable_node * regu_p,
				   DB_VALUE * result_val_p, val_descr * vd);
extern bool qdata_evaluate_sys_connect_by_path (THREAD_ENTRY * thread_p, void *xasl_p, regu_variable_node * regu_p,
						DB_VALUE * value_char, DB_VALUE * result_p, val_descr * vd);
extern int qdata_bit_not_dbval (DB_VALUE * dbval, DB_VALUE * res, tp_domain * domain);
extern int qdata_bit_and_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);
extern int qdata_bit_or_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);
extern int qdata_bit_xor_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, DB_VALUE * res, tp_domain * domain);
extern int qdata_bit_shift_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, OPERATOR_TYPE op, DB_VALUE * res,
				  tp_domain * domain);
extern int qdata_divmod_dbval (DB_VALUE * dbval1, DB_VALUE * dbval2, OPERATOR_TYPE op, DB_VALUE * res,
			       tp_domain * domain);

extern int qdata_list_dbs (THREAD_ENTRY * thread_p, DB_VALUE * result_p, tp_domain * domain_p);
extern int qdata_regu_list_to_regu_array (function_node * function_p, const int array_size,
					  regu_variable_node * regu_array[], int *num_regu);
extern int qdata_get_cardinality (THREAD_ENTRY * thread_p, DB_VALUE * db_class_name, DB_VALUE * db_index_name,
				  DB_VALUE * db_key_position, DB_VALUE * result_p);
extern int qdata_tuple_to_values_array (THREAD_ENTRY * thread_p, qfile_tuple_descriptor * tuple, DB_VALUE ** values);
extern int qdata_get_tuple_value_size_from_dbval (DB_VALUE * dbval_p);
extern int qdata_apply_interpolation_function_coercion (DB_VALUE * f_value, tp_domain ** result_dom, DB_VALUE * result,
							FUNC_TYPE function);
extern int qdata_interpolation_function_values (DB_VALUE * f_value, DB_VALUE * c_value, double row_num_d,
						double f_row_num_d, double c_row_num_d, tp_domain ** result_dom,
						DB_VALUE * result, FUNC_TYPE function);
extern int qdata_get_interpolation_function_result (THREAD_ENTRY * thread_p, qfile_list_scan_id * scan_id,
						    tp_domain * domain, int pos, double row_num_d, double f_row_num_d,
						    double c_row_num_d, DB_VALUE * result, tp_domain ** result_dom,
						    FUNC_TYPE function);
extern int qdata_update_interpolation_func_value_and_domain (DB_VALUE * src_val, DB_VALUE * dest_val,
							     tp_domain ** domain);

#endif /* _QUERY_OPFUNC_H_ */
