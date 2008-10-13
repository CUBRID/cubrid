/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * Value fetch routines
 */

#ifndef _FETCH_H_
#define _FETCH_H_

#ident "$Id$"

#include "common.h"
#include "oid.h"
#include "query_evaluator.h"

extern int fetch_peek_dbval (THREAD_ENTRY * thread_p,
			     REGU_VARIABLE * regu_var, VAL_DESCR * vd,
			     OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl,
			     DB_VALUE ** peek_dbval);
extern int fetch_copy_dbval (THREAD_ENTRY * thread_p,
			     REGU_VARIABLE * regu_var, VAL_DESCR * vd,
			     OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl,
			     DB_VALUE * dbval);
extern int fetch_val_list (THREAD_ENTRY * thread_p,
			   REGU_VARIABLE_LIST regu_list, VAL_DESCR * vd,
			   OID * class_oid, OID * obj_oid, QFILE_TUPLE tpl,
			   int peek);
extern void fetch_init_val_list (REGU_VARIABLE_LIST regu_list);

#endif /* _FETCH_H_ */
