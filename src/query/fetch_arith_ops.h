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
 * fetch_arith_ops.h - the operators of an arithmetic node (fetch_arith_ops.c)
 */

#ifndef _FETCH_ARITH_OPS_H_
#define _FETCH_ARITH_OPS_H_

#ident "$Id$"

#include "storage_common.h"	/* OPERATOR_TYPE */
#include "oid.h"
#include "object_domain.h"
#include "dbtype_def.h"
#include "qfile_tuple_layout.h"
#include "regu_var.hpp"		/* REGU_VARIABLE, ARITH_TYPE */
#include "query_executor.h"	/* val_descr */
#include "thread_compat.hpp"

/* What an operator reads: the node and the operands its fetch rule fetched, as fetch_peek_arith () holds them when it
 * enters the operator. A pointer an operand rule did not fetch is NULL. */
typedef struct fetch_arith_args FETCH_ARITH_ARGS;
struct fetch_arith_args
{
  REGU_VARIABLE *regu_var;	/* the node's regu: its flags, compiled domain and plan item */
  ARITH_TYPE *arithptr;		/* the node: operands, predicate, result slot (value) */
  val_descr *vd;
  OID *obj_oid;			/* for the operands an operator fetches itself (a branch, a lazy operand) */
  QFILE_TUPLE_RECORD *tplrec;
  DB_VALUE *peek_left;
  DB_VALUE *peek_right;
  DB_VALUE *peek_third;
  DB_VALUE *peek_fourth;
  TP_DOMAIN *domain;		/* the node's execution domain; NULL when resolve_domains found it takes no value */
  TP_DOMAIN *arith_domain;	/* the ARITH_TYPE's, as the execution has it */
  TP_DOMAIN *no_value_domain;	/* FETCH_RESOLVED_NO_VALUE: the compiled domain the node keeps */
};

/* an operator returns NO_ERROR with its result in arithptr->value, an error code (the error is set), or this: the node's
 * value is the left operand itself and the node's domain epilogue does not apply (T_INCR, T_DECR) */
#define FETCH_ARITH_RAW_LEFT 1

typedef int (*FETCH_ARITH_OP) (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a);

extern FETCH_ARITH_OP fetch_arith_op (OPERATOR_TYPE opcode);

/* The node protocol around an operator (fetch.c), shared by the interpreter and a program's generic step: the node's
 * domains in this execution, its result slot cleared and a variable domain resolved, then after the operator the
 * no-value check and the merged-collation relabel. */
extern void fetch_arith_node_domains (val_descr * vd, FETCH_ARITH_ARGS * a);
extern int fetch_arith_node_resolve (val_descr * vd, FETCH_ARITH_ARGS * a);
extern int fetch_arith_node_end (val_descr * vd, FETCH_ARITH_ARGS * a);
/* whether resolve_domains evaluated this constant expression in this execution (its value is at vd->dbval_ptr + ref) */
extern bool fetch_arith_constant_evaluated (const val_descr * vd, const DOMAIN_PLAN_ITEM * item);

/* two of the interpreter's operand rules the generic step's emitter mirrors (fetch.c): the operand whose fetch error
 * a date/time function replaces with a conversion error, and WIDTH_BUCKET's two bounds out of its PT_BETWEEN operand */
extern bool is_argument_wrapped_with_cast_op (const REGU_VARIABLE * regu_var);
extern int fetch_peek_min_max_value_of_width_bucket_func (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
							  val_descr * vd, OID * obj_oid, QFILE_TUPLE_RECORD * tplrec,
							  DB_VALUE ** min, DB_VALUE ** max);
/* a value a row picked among branches whose collations resolve_domains merged, brought into the merged domain */
extern int fetch_arith_convert_to_resolved_branch (const val_descr * vd, const DOMAIN_PLAN_ITEM * item,
						   DB_VALUE * value);

#endif /* _FETCH_ARITH_OPS_H_ */
