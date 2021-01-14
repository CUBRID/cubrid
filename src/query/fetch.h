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
 * Value fetch routines
 */

#ifndef _FETCH_H_
#define _FETCH_H_

#ident "$Id$"

#include "storage_common.h"
#include "oid.h"
#include "query_evaluator.h"
#include "query_list.h"

// forward definitions
class regu_variable_node;
struct val_descr;
struct regu_variable_list_node;

extern int fetch_peek_dbval (THREAD_ENTRY * thread_p, regu_variable_node * regu_var, val_descr * vd, OID * class_oid,
			     OID * obj_oid, QFILE_TUPLE tpl, DB_VALUE ** peek_dbval);
extern int fetch_copy_dbval (THREAD_ENTRY * thread_p, regu_variable_node * regu_var, val_descr * vd, OID * class_oid,
			     OID * obj_oid, QFILE_TUPLE tpl, DB_VALUE * dbval);
extern int fetch_val_list (THREAD_ENTRY * thread_p, regu_variable_list_node * regu_list, val_descr * vd,
			   OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl, int peek);
extern void fetch_init_val_list (regu_variable_list_node * regu_list);

extern void fetch_force_not_const_recursive (regu_variable_node & reguvar);

#endif /* _FETCH_H_ */
