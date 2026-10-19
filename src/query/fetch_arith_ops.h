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
#include "error_manager.h"
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
  REGU_VARIABLE *regu_var;	/* the node's regu: its flags and compiled domain */
  ARITH_TYPE *arithptr;		/* the node: operands, predicate, result slot (value) */
  val_descr *vd;
  OID *obj_oid;			/* for the operands an operator fetches itself (a branch, a lazy operand) */
  QFILE_TUPLE_RECORD *tplrec;
  DB_VALUE *peek_left;
  DB_VALUE *peek_right;
  DB_VALUE *peek_third;
  DB_VALUE *peek_fourth;
  TP_DOMAIN *domain;		/* the node's domain; NULL where its operands leave its type to the data */
  TP_DOMAIN *arith_domain;	/* the ARITH_TYPE's */
};

/* an operator returns NO_ERROR with its result in arithptr->value, an error code (the error is set), or this: the node's
 * value is the left operand itself and the node's domain epilogue does not apply (T_INCR, T_DECR) */
#define FETCH_ARITH_RAW_LEFT 1

typedef int (*FETCH_ARITH_OP) (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a);

extern FETCH_ARITH_OP fetch_arith_op (OPERATOR_TYPE opcode);

/* CBRD-27583 (D-77): the cast an operator's kernel makes of a value: tp_value_cast, tp_value_cast_force or
 * tp_value_auto_cast */
enum arith_cast_kind
{
  ARITH_CAST_PLAIN,
  ARITH_CAST_FORCE,
  ARITH_CAST_AUTO
};

/*
 * arith_cast_value () - CBRD-27583 (D-77): an operator kernel's cast of a value of operand i into domain, the cast it
 *   makes (kind), with the converter unpack found (ARITH_TYPE.conv[i]) when domain is the one it was found for
 *   (planned); another domain or a value of another type looks its converter up, as the cast did at every row
 */
static inline TP_DOMAIN_STATUS
arith_cast_value (const ARITH_TYPE * arith, int i, const DB_VALUE * value, DB_VALUE * result,
		  const TP_DOMAIN * domain, const TP_DOMAIN * planned, arith_cast_kind kind)
{
  const TP_VALUE_CONVERTER conv = domain == planned ? arith->conv[i] : NULL;
#if !defined (NDEBUG)
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);
  /* the cast clones a value of the domain's type when the domain is not parameterized */
  const bool looks_up = domain != NULL && !DB_IS_NULL (value) && !(conv != NULL && type == arith->conv_type[i])
    && !(type == TP_DOMAIN_TYPE (domain) && !domain->is_parameterized);
  if (looks_up)
    {
      /* CBRD-27583 census (removed at S3-14): a kernel cast that looks its converter up at the row */
      er_log_debug (ARG_FILE_LINE, "CBRD-27583 row converter lookup %d %d\n", (int) arith->opcode, i);
    }
#endif
  return kind == ARITH_CAST_AUTO ? tp_value_auto_cast_with_converter (value, result, domain, arith->conv_type[i], conv)
    : tp_value_cast_with_converter (value, result, domain, kind == ARITH_CAST_FORCE, arith->conv_type[i], conv);
}

/* The node protocol around an operator (fetch.c), shared by the interpreter and a program's generic step: the node's
 * domains, then its result slot cleared and a variable domain left to the values. */
extern void fetch_arith_node_domains (val_descr * vd, FETCH_ARITH_ARGS * a);
extern int fetch_arith_node_resolve (val_descr * vd, FETCH_ARITH_ARGS * a);

/* two of the interpreter's operand rules the generic step's emitter mirrors (fetch.c): the operand whose fetch error
 * a date/time function replaces with a conversion error, and WIDTH_BUCKET's two bounds out of its PT_BETWEEN operand */
extern bool is_argument_wrapped_with_cast_op (const REGU_VARIABLE * regu_var);
extern int fetch_peek_min_max_value_of_width_bucket_func (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
							  val_descr * vd, OID * obj_oid, QFILE_TUPLE_RECORD * tplrec,
							  DB_VALUE ** min, DB_VALUE ** max);

#endif /* _FETCH_ARITH_OPS_H_ */
