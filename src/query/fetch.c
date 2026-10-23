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
 * fetch.c - Object/Tuple value fetch routines
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif

#include "fetch.h"
#include "fetch_arith_ops.h"
#include "qfile_tuple_layout.h"

#include "error_manager.h"
#include "system_parameter.h"
#include "storage_common.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "arithmetic.h"
#include "numeric_opfunc.h"
#include "serial.h"
#include "session.h"
#include "string_opfunc.h"
#include "server_interface.h"
#include "query_opfunc.h"
#include "regu_var.hpp"
#include "tz_support.h"
#include "db_date.h"
#include "xasl.h"
#include "xasl_predicate.hpp"
#include "query_executor.h"
#include "thread_entry.hpp"
#include "subquery_cache.h"
#include "pl_executor.hpp"

#include "dbtype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int fetch_peek_arith (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * obj_oid,
			     QFILE_TUPLE_RECORD * tplrec, DB_VALUE ** peek_dbval);

/* The type group for aggregate operand evaluation, derived from the root
 * result domain. The tree shape is validated once per type group at marking
 * time; the run-time hook re-derives the type group with one switch. */
typedef enum
{
  AGG_EXPR_TYPE_GROUP_NONE = 0,
  AGG_EXPR_TYPE_GROUP_NUMERIC,	/* {+,-,*} over NUMERIC; u128 coefficient registers */
  AGG_EXPR_TYPE_GROUP_INT,	/* SHORT/INTEGER/BIGINT nodes; int64 registers */
  AGG_EXPR_TYPE_GROUP_DOUBLE	/* DOUBLE nodes (FLOAT only as a leaf); double registers */
} AGG_EXPR_TYPE_GROUP;

static AGG_EXPR_TYPE_GROUP fetch_agg_expr_type_group (const REGU_VARIABLE * regu_var);
static bool fetch_is_agg_expr_node (const REGU_VARIABLE * regu_var, AGG_EXPR_TYPE_GROUP type_group, int budget);
static bool fetch_agg_expr_eval_numeric (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd,
					 OID * obj_oid, QFILE_TUPLE_RECORD * tplrec, NUMERIC_AGG_EXPR_VAL * out);
static bool fetch_agg_expr_int_from_dbv (const DB_VALUE * dbv, int64_t * out);
static bool fetch_agg_expr_eval_int (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd,
				     OID * obj_oid, QFILE_TUPLE_RECORD * tplrec, int64_t * out);
static bool fetch_agg_expr_dbl_from_dbv (const DB_VALUE * dbv, double *out);
static bool fetch_agg_expr_eval_dbl (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd,
				     OID * obj_oid, QFILE_TUPLE_RECORD * tplrec, double *out);

static int fetch_peek_dbval_pos (regu_variable_list_node * regu_list, QFILE_TUPLE_RECORD * tplrec,
				 const VAL_DESCR * vd);

/*
 * fetch_agg_expr_type_group () - determine the type group from the tree's result domain
 *   return: the type_group, or AGG_EXPR_TYPE_GROUP_NONE
 *
 * FLOAT does not select a type_group of its own: the legacy behavior rounds each
 * FLOAT operation's result to float, so only float-by-float evaluation reproduces
 * the same behavior. A FLOAT leaf under a DOUBLE tree is fine. The legacy behavior
 * widens it exactly once, which (double) reproduces verbatim.
 */
static AGG_EXPR_TYPE_GROUP
fetch_agg_expr_type_group (const REGU_VARIABLE * regu_var)
{
  const TP_DOMAIN *domain = regu_produced_domain (regu_var);
  if (domain == NULL)
    {
      return AGG_EXPR_TYPE_GROUP_NONE;
    }

  switch (TP_DOMAIN_TYPE (domain))
    {
    case DB_TYPE_NUMERIC:
      return AGG_EXPR_TYPE_GROUP_NUMERIC;
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      return AGG_EXPR_TYPE_GROUP_INT;
    case DB_TYPE_DOUBLE:
      return AGG_EXPR_TYPE_GROUP_DOUBLE;
    default:
      return AGG_EXPR_TYPE_GROUP_NONE;
    }
}

/* The operators a fused tree may contain: {+, -, *, /} for non-NUMERIC types,
 * and {+, -, *} for NUMERIC. NUMERIC division falls back to the legacy path
 * because its scale and rounding rules are not easily reproduced.
 */
#define FETCH_IS_AGG_EXPR_OP(op, group) \
  ((op) == T_ADD || (op) == T_SUB || (op) == T_MUL \
   || ((op) == T_DIV && (group) != AGG_EXPR_TYPE_GROUP_NUMERIC))

/*
 * fetch_is_agg_expr_shape () - can this expression tree be fused into a
 *                              register chain for its type group?
 *   return: true if it is an arithmetic tree FETCH_IS_AGG_EXPR_OP allows
 *           over plain value references
 *   regu_var(in): root of the expression tree
 *   budget(in): remaining recursion allowance
 *
 * The shape is checked before any operand is read, so that a tree which only
 * turns out to be unfusable later (a value too wide, say) can be re-evaluated by
 * the legacy path without any operand being evaluated twice. A leaf is either a
 * plain value reference or an integer that type checking wrapped for a NUMERIC
 * operator, and reading one has no side effect, which is what makes that
 * re-evaluation safe. Operators such as T_INCR never reach the agg-expr path.
 *
 * The agg-expr walk descends on its own without touching the recursion counter that
 * fetch_peek_arith () maintains, so a tree deep enough to exhaust
 * max_recursion_sql_depth is left to the legacy path, which raises
 * ER_MAX_RECURSION_SQL_DEPTH as it would without the agg-expr path.
 */
bool
fetch_is_agg_expr_shape (const REGU_VARIABLE * regu_var, int budget)
{
  AGG_EXPR_TYPE_GROUP type_group;
  OPERATOR_TYPE op;

  /* a lone cast root (e.g. SUM(CAST(i)) with no arithmetic) has nothing to fuse;
   * the general path already handles it in one step */
  if (regu_var->type != TYPE_INARITH || regu_var->value.arithptr == NULL)
    {
      return false;
    }

  type_group = fetch_agg_expr_type_group (regu_var);
  if (type_group == AGG_EXPR_TYPE_GROUP_NONE)
    {
      return false;
    }

  op = regu_var->value.arithptr->opcode;
  if (!FETCH_IS_AGG_EXPR_OP (op, type_group))
    {
      return false;
    }

  return fetch_is_agg_expr_node (regu_var, type_group, budget);
}

/*
 * fetch_is_agg_expr_int_source () - check whether the cast source is an integer type
 *   return: true for a SHORT, INTEGER, or BIGINT operand
 */
static bool
fetch_is_agg_expr_int_source (const REGU_VARIABLE * regu_var)
{
  if (regu_var->domain == NULL)
    {
      return false;
    }

  switch (TP_DOMAIN_TYPE (regu_var->domain))
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      return true;
    default:
      /* The domain only decides whether to try; the value type is rechecked on read,
       * so a mismatched domain safely falls back. */
      return false;
    }
}

/*
 * fetch_is_agg_expr_wrap_leaf () - can this T_CAST_WRAP node stand as a leaf?
 *   return: true when the evaluator may absorb the wrap and read its source directly
 *
 * Only implicit widening wraps are absorbed. Explicit casts remain on the legacy
 * path to preserve narrowing and overflow semantics.
 *
 * For NUMERIC, only the default common-type wrap over an integer source is
 * supported. A wrap to a NUMERIC with an explicit scale is left on the legacy
 * path.
 *
 * For typed groups, the implicit widening wrap (integer to BIGINT or DOUBLE,
 * FLOAT or NUMERIC to DOUBLE) is absorbed: the evaluator widens the source
 * itself and produces the exact value the cast would.
 */
static bool
fetch_is_agg_expr_wrap_leaf (const REGU_VARIABLE * regu_var, AGG_EXPR_TYPE_GROUP type_group)
{
  const ARITH_TYPE *arithptr = regu_var->value.arithptr;

  if (arithptr->rightptr == NULL)
    {
      return false;
    }

  if (type_group == AGG_EXPR_TYPE_GROUP_NUMERIC)
    {
      const TP_DOMAIN *domain = regu_produced_domain (regu_var);
      return (domain != NULL && TP_DOMAIN_TYPE (domain) == DB_TYPE_NUMERIC
	      && domain->precision == DB_DEFAULT_NUMERIC_PRECISION && domain->scale == DB_DEFAULT_NUMERIC_SCALE
	      && fetch_is_agg_expr_int_source (arithptr->rightptr));
    }

  if (fetch_agg_expr_type_group (regu_var) != type_group)
    {
      return false;
    }
  if (fetch_is_agg_expr_int_source (arithptr->rightptr))
    {
      return true;
    }
  return (type_group == AGG_EXPR_TYPE_GROUP_DOUBLE && arithptr->rightptr->domain != NULL
	  && (TP_DOMAIN_TYPE (arithptr->rightptr->domain) == DB_TYPE_FLOAT
	      || TP_DOMAIN_TYPE (arithptr->rightptr->domain) == DB_TYPE_NUMERIC));
}

/*
 * fetch_is_agg_expr_node () - the recursive half of fetch_is_agg_expr_shape ()
 *   return: true if this subtree can be fused under the given type_group
 */
static bool
fetch_is_agg_expr_node (const REGU_VARIABLE * regu_var, AGG_EXPR_TYPE_GROUP type_group, int budget)
{
  const ARITH_TYPE *arithptr;

  if (budget <= 0)
    {
      return false;
    }

  switch (regu_var->type)
    {
    case TYPE_CONSTANT:
    case TYPE_DBVAL:
    case TYPE_POSITION:
    case TYPE_ATTR_ID:
      return true;

    case TYPE_INARITH:
      if (regu_var->conv != NULL)
	{
	  /* CBRD-27583: the fused tree computes an interior node without fetching it, so its conversion would not run */
	  return false;
	}
      break;

    default:
      return false;
    }

  arithptr = regu_var->value.arithptr;
  if (arithptr == NULL)
    {
      return false;
    }

  if (arithptr->opcode == T_CAST_WRAP)
    {
      return fetch_is_agg_expr_wrap_leaf (regu_var, type_group);
    }

  if (type_group != AGG_EXPR_TYPE_GROUP_NUMERIC && fetch_agg_expr_type_group (regu_var) != type_group)
    {
      /* Interior nodes must match the root's type group; NUMERIC is exempt because
       * its wraps keep the interior in a uniform NUMERIC evaluation.
       */
      return false;
    }

  if (arithptr->leftptr == NULL || arithptr->rightptr == NULL || !FETCH_IS_AGG_EXPR_OP (arithptr->opcode, type_group))
    {
      return false;
    }

  return (fetch_is_agg_expr_node (arithptr->leftptr, type_group, budget - 1)
	  && fetch_is_agg_expr_node (arithptr->rightptr, type_group, budget - 1));
}

/*
 * fetch_agg_expr_eval_numeric () - evaluate a NUMERIC-group {+,-,*} tree entirely in the word domain
 *   return: true when the whole tree was fused, false to fall back
 *   out(out): fused result
 *
 * Operands must be NUMERIC values that fit in an agg-expr value, or integers
 * that type checking wrapped for a NUMERIC operation. Bare integer leaves are
 * rejected because the legacy path derives their precision differently. The
 * wrap distinguishes the two cases and indicates that the operation is NUMERIC.
 */
static bool
fetch_agg_expr_eval_numeric (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * obj_oid,
			     QFILE_TUPLE_RECORD * tplrec, NUMERIC_AGG_EXPR_VAL * out)
{
  ARITH_TYPE *arithptr;
  NUMERIC_AGG_EXPR_VAL left, right;

  if (regu_var->type != TYPE_INARITH)
    {
      DB_VALUE *peek_leaf = NULL;

      if (fetch_peek_dbval (thread_p, regu_var, vd, NULL, obj_oid, tplrec, &peek_leaf) != NO_ERROR)
	{
	  return false;
	}

      return numeric_agg_expr_from_dbv (peek_leaf, out);
    }

  arithptr = regu_var->value.arithptr;

  if (arithptr->opcode == T_CAST_WRAP)
    {
      DB_VALUE *peek_int = NULL;

      /* Read the integer directly into the register instead of evaluating the cast.
       * The general path creates and then unpacks a 17-byte NUMERIC value; here, the
       * conversion is reduced to an int64 read and widening.
       */
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_int) != NO_ERROR)
	{
	  return false;
	}
      return numeric_agg_expr_from_int_dbv (peek_int, out);
    }

  if (!fetch_agg_expr_eval_numeric (thread_p, arithptr->leftptr, vd, obj_oid, tplrec, &left)
      || !fetch_agg_expr_eval_numeric (thread_p, arithptr->rightptr, vd, obj_oid, tplrec, &right))
    {
      return false;
    }

  if (arithptr->opcode == T_MUL)
    {
      return numeric_agg_expr_mul (&left, &right, out);
    }

  return numeric_agg_expr_add (&left, &right, arithptr->opcode == T_SUB, out);
}

/*
 * fetch_agg_expr_int_from_dbv () - load an integer-type_group value into an int64 register
 *   return: true, or false for a NULL or a non-integer (that row falls back)
 */
static bool
fetch_agg_expr_int_from_dbv (const DB_VALUE * dbv, int64_t * out)
{
  if (dbv == NULL || DB_IS_NULL (dbv))
    {
      return false;
    }

  switch (DB_VALUE_DOMAIN_TYPE (dbv))
    {
    case DB_TYPE_SHORT:
      *out = (int64_t) db_get_short (dbv);
      return true;
    case DB_TYPE_INTEGER:
      *out = (int64_t) db_get_int (dbv);
      return true;
    case DB_TYPE_BIGINT:
      *out = (int64_t) db_get_bigint (dbv);
      return true;
    default:
      return false;
    }
}

/*
 * fetch_agg_expr_eval_int () - evaluate an integer {+,-,*,/} tree in int64 registers
 *   return: true when the whole tree was fused, false to fall back
 *   out(out): the root's value; already verified to fit the root's domain
 *
 * Each operation is evaluated in its own result domain. The int64 register only
 * carries the value, while each operation checks the same overflow conditions as
 * the legacy operator. Rows that would overflow, divide by zero, or produce
 * INT64_MIN / -1 fall back to the legacy path. Division truncates toward zero,
 * matching the legacy operator.
 */
static bool
fetch_agg_expr_eval_int (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * obj_oid,
			 QFILE_TUPLE_RECORD * tplrec, int64_t * out)
{
  ARITH_TYPE *arithptr;
  int64_t left, right, result;

  if (regu_var->type != TYPE_INARITH)
    {
      DB_VALUE *peek_leaf = NULL;

      if (fetch_peek_dbval (thread_p, regu_var, vd, NULL, obj_oid, tplrec, &peek_leaf) != NO_ERROR)
	{
	  return false;
	}
      return fetch_agg_expr_int_from_dbv (peek_leaf, out);
    }

  arithptr = regu_var->value.arithptr;

  if (arithptr->opcode == T_CAST_WRAP)
    {
      DB_VALUE *peek_int = NULL;

      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_int) != NO_ERROR)
	{
	  return false;
	}
      return fetch_agg_expr_int_from_dbv (peek_int, out);
    }

  if (!fetch_agg_expr_eval_int (thread_p, arithptr->leftptr, vd, obj_oid, tplrec, &left)
      || !fetch_agg_expr_eval_int (thread_p, arithptr->rightptr, vd, obj_oid, tplrec, &right))
    {
      return false;
    }

  switch (arithptr->opcode)
    {
    case T_ADD:
      if (__builtin_add_overflow (left, right, &result))
	{
	  return false;
	}
      break;
    case T_SUB:
      if (__builtin_sub_overflow (left, right, &result))
	{
	  return false;
	}
      break;
    case T_MUL:
      if (__builtin_mul_overflow (left, right, &result))
	{
	  return false;
	}
      break;
    case T_DIV:
      if (right == 0 || (left == INT64_MIN && right == -1))
	{
	  return false;
	}
      result = left / right;
      break;
    default:
      return false;
    }

  switch (TP_DOMAIN_TYPE (regu_produced_domain (regu_var)))
    {
    case DB_TYPE_SHORT:
      if (result > DB_INT16_MAX || result < DB_INT16_MIN)
	{
	  return false;
	}
      break;
    case DB_TYPE_INTEGER:
      if (result > DB_INT32_MAX || result < DB_INT32_MIN)
	{
	  return false;
	}
      break;
    default:
      /* BIGINT needs no range check: __builtin_*_overflow above already tested the int64 width */
      break;
    }

  *out = result;
  return true;
}

/*
 * fetch_agg_expr_dbl_from_dbv () - load a value into a double register
 *   return: true, or false for NULL or an unsupported type (the row falls back)
 *
 * Integer and FLOAT leaves are widened with a single C conversion, matching
 * the legacy path regardless of whether the value is wrapped.
 */
static bool
fetch_agg_expr_dbl_from_dbv (const DB_VALUE * dbv, double *out)
{
  if (dbv == NULL || DB_IS_NULL (dbv))
    {
      return false;
    }

  switch (DB_VALUE_DOMAIN_TYPE (dbv))
    {
    case DB_TYPE_DOUBLE:
      *out = db_get_double (dbv);
      return true;
    case DB_TYPE_FLOAT:
      *out = (double) db_get_float (dbv);
      return true;
    case DB_TYPE_SHORT:
      *out = (double) db_get_short (dbv);
      return true;
    case DB_TYPE_INTEGER:
      *out = (double) db_get_int (dbv);
      return true;
    case DB_TYPE_BIGINT:
      *out = (double) db_get_bigint (dbv);
      return true;
    case DB_TYPE_NUMERIC:
      {
	double adouble;

	numeric_coerce_num_to_double (dbv, db_get_numeric_scale (dbv, NULL), &adouble);
	if (OR_CHECK_DOUBLE_OVERFLOW (adouble))
	  {
	    /* On overflow, fall back to let the legacy path raise the error. */
	    return false;
	  }
	*out = adouble;
	return true;
      }
    default:
      return false;
    }
}

/*
 * fetch_agg_expr_eval_dbl () - evaluate a DOUBLE {+,-,*,/} tree in double registers
 *   return: true when the whole tree was fused, false to fall back
 *   out(out): the root's value; finite
 *
 * Each operation uses the same IEEE operation and order as the legacy path,
 * producing bit-identical results. Overflow is checked with
 * OR_CHECK_DOUBLE_OVERFLOW; overflow and division by zero fall back so the
 * legacy path can raise the error.
 */
static bool
fetch_agg_expr_eval_dbl (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * obj_oid,
			 QFILE_TUPLE_RECORD * tplrec, double *out)
{
  ARITH_TYPE *arithptr;
  double left, right, result;

  if (regu_var->type != TYPE_INARITH)
    {
      DB_VALUE *peek_leaf = NULL;

      if (fetch_peek_dbval (thread_p, regu_var, vd, NULL, obj_oid, tplrec, &peek_leaf) != NO_ERROR)
	{
	  return false;
	}
      return fetch_agg_expr_dbl_from_dbv (peek_leaf, out);
    }

  arithptr = regu_var->value.arithptr;

  if (arithptr->opcode == T_CAST_WRAP)
    {
      DB_VALUE *peek_src = NULL;

      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_src) != NO_ERROR)
	{
	  return false;
	}
      return fetch_agg_expr_dbl_from_dbv (peek_src, out);
    }

  if (!fetch_agg_expr_eval_dbl (thread_p, arithptr->leftptr, vd, obj_oid, tplrec, &left)
      || !fetch_agg_expr_eval_dbl (thread_p, arithptr->rightptr, vd, obj_oid, tplrec, &right))
    {
      return false;
    }

  switch (arithptr->opcode)
    {
    case T_ADD:
      result = left + right;
      break;
    case T_SUB:
      result = left - right;
      break;
    case T_MUL:
      result = left * right;
      break;
    case T_DIV:
      if (right == 0.0)
	{
	  return false;
	}
      result = left / right;
      break;
    default:
      return false;
    }

  if (OR_CHECK_DOUBLE_OVERFLOW (result))
    {
      return false;
    }

  *out = result;
  return true;
}

/*
 * fetch_arith_node_domains () - the domains an arithmetic node computes in: the compiled ones in regu_var->domain and
 *   arithptr->domain, which never change
 */
void
fetch_arith_node_domains (val_descr * vd, FETCH_ARITH_ARGS * a)
{
  /* CBRD-27583: a converting regu's domain is its consumer's; the node computes in the one it is read in */
  a->domain = regu_produced_domain (a->regu_var);
  a->arith_domain = a->arithptr->domain;
}

/*
 * fetch_arith_node_resolve () - the node's result slot cleared before its operator runs; a node whose compiled domain
 *   is variable computes without one
 *   return: NO_ERROR
 *
 * The interpreter (fetch_peek_arith) and a program's generic step share this protocol around an operator: the operand
 * values differ in where they come from, the node's domains do not.
 */
int
fetch_arith_node_resolve (val_descr * vd, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  /* clear any previous result */
  pr_clear_value (arithptr->value);
  assert (a->domain == NULL || TP_DOMAIN_TYPE (a->domain) != DB_TYPE_VARIABLE);
  if (a->domain != NULL && TP_DOMAIN_TYPE (a->domain) == DB_TYPE_VARIABLE)
    {
      /* exception to deciding before the rows: the operands leave their types to the data, so the operator computes
       * without a domain and its values give the result's (a cast into a fixed target keeps the target) */
      const bool cast = arithptr->opcode == T_CAST || arithptr->opcode == T_CAST_WRAP
	|| arithptr->opcode == T_CAST_NOFAIL;
      a->domain = cast && arithptr->domain != NULL && TP_DOMAIN_TYPE (arithptr->domain) != DB_TYPE_VARIABLE
	? arithptr->domain : NULL;
    }
  return NO_ERROR;
}

/*
 * fetch_arith_caches () - whether an arithmetic node over constant operands keeps its value for the rest of the
 *   execution (FETCH_ALL_CONST): not an operator whose value changes without its operands (a serial, a variable, a
 *   random number, a statistic, a predicate the fetch does not analyze, another tuple of the CONNECT BY list)
 */
static bool
fetch_arith_caches (const ARITH_TYPE * arithptr)
{
  if (arithptr->pred != NULL)
    {
      return false;
    }
  switch (arithptr->opcode)
    {
    case T_INCR:
    case T_DECR:
    case T_CURRENT_VALUE:
    case T_NEXT_VALUE:
    case T_CASE:
    case T_DECODE:
    case T_IF:
    case T_PREDICATE:
    case T_ROW_COUNT:
    case T_LAST_INSERT_ID:
    case T_EVALUATE_VARIABLE:
    case T_DEFINE_VARIABLE:
    case T_RAND:
    case T_RANDOM:
    case T_DRAND:
    case T_DRANDOM:
    case T_SYS_GUID:
    case T_UUID:
    case T_EXEC_STATS:
    case T_TRACE_STATS:
    case T_SLEEP:
    case T_PRIOR:
    case T_QPRIOR:
    case T_CONNECT_BY_ROOT:
    case T_SYS_CONNECT_BY_PATH:
      return false;
    default:
      return true;
    }
}

/* an operand a constant node reads: none, or one this execution marked constant */
static bool
fetch_regu_is_all_const (const REGU_VARIABLE * regu)
{
  return regu == NULL || REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_FETCH_ALL_CONST);
}

/*
 * fetch_function_caches () - whether a function keeps its value for the rest of the execution (FETCH_ALL_CONST): a
 *   JSON, REGEXP, INSERT or ELT function whose operands are constant; an operand it did not fetch (an ELT branch it did
 *   not pick) counts when it is a value or a bind
 */
static bool
fetch_function_caches (const FUNCTION_TYPE * funcp)
{
  switch (funcp->ftype)
    {
    case F_JSON_ARRAY:
    case F_JSON_ARRAY_APPEND:
    case F_JSON_ARRAY_INSERT:
    case F_JSON_CONTAINS:
    case F_JSON_CONTAINS_PATH:
    case F_JSON_DEPTH:
    case F_JSON_EXTRACT:
    case F_JSON_GET_ALL_PATHS:
    case F_JSON_KEYS:
    case F_JSON_INSERT:
    case F_JSON_LENGTH:
    case F_JSON_MERGE:
    case F_JSON_MERGE_PATCH:
    case F_JSON_OBJECT:
    case F_JSON_PRETTY:
    case F_JSON_QUOTE:
    case F_JSON_REMOVE:
    case F_JSON_REPLACE:
    case F_JSON_SEARCH:
    case F_JSON_SET:
    case F_JSON_TYPE:
    case F_JSON_UNQUOTE:
    case F_JSON_VALID:
    case F_REGEXP_COUNT:
    case F_REGEXP_INSTR:
    case F_REGEXP_LIKE:
    case F_REGEXP_REPLACE:
    case F_REGEXP_SUBSTR:
    case F_INSERT_SUBSTRING:
    case F_ELT:
      break;
    default:
      return false;
    }
  for (REGU_VARIABLE_LIST operand = funcp->operand; operand != NULL; operand = operand->next)
    {
      if (!fetch_regu_is_all_const (&operand->value) && operand->value.type != TYPE_DBVAL
	  && operand->value.type != TYPE_POS_VALUE)
	{
	  return false;
	}
    }
  return true;
}

/*
 * fetch_set_fast_peek () - after the first fetch of this execution, a value the regu holds or points at, unconverted,
 *   is peeked by the inline fetch_peek_dbval (); cleared with the constant marks (qexec_clear_regu_var)
 */
static void
fetch_set_fast_peek (REGU_VARIABLE * regu_var)
{
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION) || regu_var->conv != NULL)
    {
      return;
    }
  switch (regu_var->type)
    {
    case TYPE_ATTR_ID:
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
    case TYPE_DBVAL:
    case TYPE_POS_VALUE:
      break;
    case TYPE_CONSTANT:
      if (regu_var->xasl != NULL || regu_var->value.dbvalptr == NULL)
	{
	  return;
	}
      break;
    default:
      return;
    }
  REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FAST_PEEK);
}

// *INDENT-OFF*
void
fetch_force_not_const_recursive (REGU_VARIABLE & reguvar)
{
  auto map_func = [&] (regu_variable_node &regu, bool & stop)
    {
    switch (regu.type)
      {
      case TYPE_INARITH:
      case TYPE_OUTARITH:
      case TYPE_FUNC:
      case TYPE_SP:
        REGU_VARIABLE_SET_FLAG (&regu, REGU_VARIABLE_FETCH_NOT_CONST);
        break;
      default:
        break;
      }
    };
  reguvar.map_regu (map_func);
}
// *INDENT-ON*

/*
 * fetch_peek_arith () -
 *   return: NO_ERROR or ER_code
 *   regu_var(in/out): Regulator Variable of an ARITH node.
 *   vd(in): Value Descriptor
 *   obj_oid(in): Object Identifier
 *   tplrec(in): tuple slot (record + layout descriptor)
 *   peek_dbval(out): Set to the value resulting from the fetch operation
 */
static int
fetch_peek_arith (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * obj_oid,
		  QFILE_TUPLE_RECORD * tplrec, DB_VALUE ** peek_dbval)
{
  ARITH_TYPE *arithptr;
  DB_VALUE *peek_left, *peek_right, *peek_third, *peek_fourth;

  assert (regu_var != NULL);
  arithptr = regu_var->value.arithptr;
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST))
    {
      *peek_dbval = arithptr->value;

      return NO_ERROR;
    }

  /* the node and the domains it computes in (fetch_arith_node_domains); the operand fetch rules read the node's
   * domain (T_ADD's oracle_style_empty_string) */
  FETCH_ARITH_ARGS args;
  args.regu_var = regu_var;
  args.arithptr = arithptr;
  args.vd = vd;
  args.obj_oid = obj_oid;
  args.tplrec = tplrec;
  fetch_arith_node_domains (vd, &args);
  TP_DOMAIN *domain = args.domain;

  /* An aggregate operand expression is evaluated in one register pass.
   * Unlike the general path, which materializes a DB_VALUE at each operation,
   * the agg-expr path keeps the running value in a register appropriate for its
   * type group (128-bit words, int64, or double) and packs one result.
   *
   * This path is limited to aggregate operands, and its shape is settled at
   * unpack (stx_mark_aggregate_operands (), once per XASL unpack).
   *
   * The result is written where the general path would write it, so all consumers
   * remain unchanged. NULL result domains stay on the general path. */
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_AGG_OPERAND)
      && domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL)
    {
      bool fused = false;

      switch (fetch_agg_expr_type_group (regu_var))
	{
	case AGG_EXPR_TYPE_GROUP_NUMERIC:
	  {
	    NUMERIC_AGG_EXPR_VAL expr_val;

	    if (fetch_agg_expr_eval_numeric (thread_p, regu_var, vd, obj_oid, tplrec, &expr_val))
	      {
		numeric_agg_expr_to_dbv (&expr_val, arithptr->value);
		fused = true;
	      }
	  }
	  break;

	case AGG_EXPR_TYPE_GROUP_INT:
	  {
	    int64_t int_val;

	    if (fetch_agg_expr_eval_int (thread_p, regu_var, vd, obj_oid, tplrec, &int_val))
	      {
		switch (TP_DOMAIN_TYPE (domain))
		  {
		  case DB_TYPE_SHORT:
		    db_make_short (arithptr->value, (short) int_val);
		    break;
		  case DB_TYPE_INTEGER:
		    db_make_int (arithptr->value, (int) int_val);
		    break;
		  default:
		    db_make_bigint (arithptr->value, (DB_BIGINT) int_val);
		    break;
		  }
		fused = true;
	      }
	  }
	  break;

	case AGG_EXPR_TYPE_GROUP_DOUBLE:
	  {
	    double dbl_val;

	    if (fetch_agg_expr_eval_dbl (thread_p, regu_var, vd, obj_oid, tplrec, &dbl_val))
	      {
		db_make_double (arithptr->value, dbl_val);
		fused = true;
	      }
	  }
	  break;

	default:
	  break;
	}

      if (fused)
	{
	  /* aggregate operands read columns: computed for each row */
	  REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
	  *peek_dbval = arithptr->value;

	  return NO_ERROR;
	}
    }

  peek_left = NULL;
  peek_right = NULL;
  peek_third = NULL;
  peek_fourth = NULL;

  if (thread_get_recursion_depth (thread_p) > prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH))
    {
      int error = ER_MAX_RECURSION_SQL_DEPTH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH));
      return error;
    }
  thread_inc_recursion_depth (thread_p);

  /* fetch values */
  switch (arithptr->opcode)
    {
    case T_SUBSTRING:
    case T_LPAD:
    case T_RPAD:
    case T_REPLACE:
    case T_TRANSLATE:
    case T_TO_CHAR:
    case T_TO_DATE:
    case T_TO_TIME:
    case T_TO_TIMESTAMP:
    case T_TO_DATETIME:
    case T_TO_NUMBER:
    case T_INSTR:
    case T_MID:
    case T_DATE_ADD:
    case T_DATE_SUB:
    case T_NEXT_VALUE:
    case T_INDEX_PREFIX:
    case T_TO_DATETIME_TZ:
    case T_TO_TIMESTAMP_TZ:

      /* fetch lhs, rhs, and third value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (!DB_IS_NULL (peek_left))
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	  if ((prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL || arithptr->opcode == T_SUBSTRING)
	      && !arithptr->thirdptr)
	    {
	      break;
	    }
	  if (arithptr->thirdptr != NULL)
	    {
	      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
		{
		  goto error;
		}
	    }
	}
      break;

    case T_STR_TO_DATE:
    case T_DATE_FORMAT:
    case T_TIME_FORMAT:
    case T_FORMAT:
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	{
	  goto error;
	}
      [[fallthrough]];

    case T_ADD:
    case T_SUB:
    case T_MUL:
    case T_DIV:
    case T_MOD:
    case T_POSITION:
    case T_FINDINSET:
    case T_ADD_MONTHS:
    case T_MONTHS_BETWEEN:
    case T_AES_ENCRYPT:
    case T_AES_DECRYPT:
    case T_SHA_TWO:
    case T_POWER:
    case T_ROUND:
    case T_LOG:
    case T_TRUNC:
    case T_STRCAT:
    case T_NULLIF:
    case T_INCR:
    case T_DECR:
    case T_BIT_AND:
    case T_BIT_OR:
    case T_BIT_XOR:
    case T_BITSHIFT_LEFT:
    case T_BITSHIFT_RIGHT:
    case T_INTDIV:
    case T_INTMOD:
    case T_STRCMP:
    case T_ATAN2:
    case T_ADDDATE:
    case T_SUBDATE:
    case T_DATEDIFF:
    case T_TIMEDIFF:
    case T_CURRENT_VALUE:
    case T_CHR:
      /* fetch lhs and rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (DB_IS_NULL (peek_left))
	{
	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING)
	      && (arithptr->opcode == T_STRCAT || arithptr->opcode == T_ADD))
	    {
	      /* check for result type: a variable domain reads the values' domains */
	      const TP_DOMAIN *result_domain = domain;
	      if (TP_DOMAIN_TYPE (result_domain) == DB_TYPE_VARIABLE
		  || QSTR_IS_ANY_CHAR_OR_BIT (TP_DOMAIN_TYPE (result_domain)))
		{
		  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) !=
		      NO_ERROR)
		    {
		      goto error;
		    }
		}
	    }
	}
      else
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_TRIM:
    case T_LTRIM:
    case T_RTRIM:
      /* fetch lhs and rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_FROM_UNIXTIME:

      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      if (arithptr->thirdptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_SUBSTRING_INDEX:
    case T_CONCAT_WS:
    case T_FIELD:
    case T_INDEX_CARDINALITY:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CONV:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LOCATE:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->thirdptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_CONCAT:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (DB_IS_NULL (peek_left))
	{
	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
	    {
	      if (arithptr->rightptr != NULL)
		{
		  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) !=
		      NO_ERROR)
		    {
		      goto error;
		    }
		}
	    }
	}
      else
	{
	  if (arithptr->rightptr != NULL)
	    {
	      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
		{
		  goto error;
		}
	    }
	}
      break;

    case T_REPEAT:
    case T_LEAST:
    case T_GREATEST:
    case T_SYS_CONNECT_BY_PATH:
    case T_LEFT:
    case T_RIGHT:
      /* fetch both lhs and rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_MAKEDATE:
    case T_ADDTIME:
    case T_WEEK:
    case T_DEFINE_VARIABLE:
    case T_FROM_TZ:
      /* fetch both lhs and rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->leftptr))
	    {
	      /* cast might have failed: set ER_DATE_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	    }
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->rightptr))
	    {
	      /* cast might have failed: set ER_DATE_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	    }
	  goto error;
	}
      break;

    case T_MAKETIME:
    case T_NEW_TIME:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->leftptr))
	    {
	      /* cast might have failed: set ER_TIME_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	    }
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->rightptr))
	    {
	      /* cast might have failed: set ER_TIME_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	    }
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->thirdptr))
	    {
	      /* cast might have failed: set ER_TIME_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	    }
	  goto error;
	}
      break;

    case T_CASE:
    case T_DECODE:
    case T_IF:
    case T_IFNULL:
    case T_NVL:
    case T_PREDICATE:
    case T_COALESCE:
    case T_NVL2:
      /* defer fetch values */
      break;

    case T_LAST_DAY:
#if defined(ENABLE_UNUSED_FUNCTION)
    case T_UNPLUS:
#endif /* ENABLE_UNUSED_FUNCTION */
    case T_UNMINUS:
    case T_OCTET_LENGTH:
    case T_BIT_LENGTH:
    case T_CHAR_LENGTH:
    case T_LOWER:
    case T_UPPER:
    case T_HEX:
    case T_ASCII:
    case T_SPACE:
    case T_MD5:
    case T_UUID_FORMAT:
    case T_UUID:
    case T_SHA_ONE:
    case T_TO_BASE64:
    case T_FROM_BASE64:
    case T_BIN:
    case T_CAST:
    case T_CAST_NOFAIL:
    case T_CAST_WRAP:
    case T_EXTRACT:
    case T_FLOOR:
    case T_CEIL:
    case T_SIGN:
    case T_ABS:
    case T_EXP:
    case T_SQRT:
    case T_PRIOR:
    case T_BIT_NOT:
    case T_REVERSE:
    case T_DISK_SIZE:
    case T_BIT_COUNT:
    case T_ACOS:
    case T_ASIN:
    case T_SIN:
    case T_COS:
    case T_TAN:
    case T_COT:
    case T_DEGREES:
    case T_RADIANS:
    case T_LN:
    case T_LOG2:
    case T_LOG10:
    case T_ATAN:
    case T_DATE:
    case T_TIME:
    case T_ISNULL:
    case T_RAND:
    case T_DRAND:
    case T_RANDOM:
    case T_DRANDOM:
    case T_TYPEOF:
    case T_EXEC_STATS:
    case T_INET_ATON:
    case T_INET_NTOA:
    case T_CHARSET:
    case T_COLLATION:
    case T_TZ_OFFSET:
    case T_SLEEP:
    case T_CRC32:
    case T_CONV_TZ:
    case T_ESTIMATED_TABLE_ROWS:
    case T_ESTIMATED_AVG_ROW_LENGTH:
    case T_ESTIMATED_DATA_LENGTH:
    case T_ESTIMATED_DATA_FREE:
    case T_COLLECTION_TO_STRING:
      /* fetch rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_YEAR:
    case T_MONTH:
    case T_DAY:
    case T_QUARTER:
    case T_WEEKDAY:
    case T_DAYOFWEEK:
    case T_DAYOFYEAR:
    case T_TODAYS:
    case T_FROMDAYS:
    case T_EVALUATE_VARIABLE:
      /* fetch rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->rightptr))
	    {
	      /* cast might have failed: set ER_DATE_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	    }
	  goto error;
	}
      break;

    case T_HOUR:
    case T_MINUTE:
    case T_SECOND:
    case T_TIMETOSEC:
    case T_SECTOTIME:
      /* fetch rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->rightptr))
	    {
	      /* if cast failed, set ER_TIME_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	    }
	  goto error;
	}
      break;

    case T_UNIX_TIMESTAMP:
    case T_DEFAULT:
      if (arithptr->rightptr)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_TIMESTAMP:
    case T_LIKE_LOWER_BOUND:
    case T_LIKE_UPPER_BOUND:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_CONNECT_BY_ROOT:
    case T_QPRIOR:
    case T_SYS_DATE:
    case T_SYS_TIME:
    case T_SYS_TIMESTAMP:
    case T_SYS_DATETIME:
    case T_CURRENT_DATE:
    case T_CURRENT_TIME:
    case T_CURRENT_TIMESTAMP:
    case T_CURRENT_DATETIME:
    case T_UTC_TIME:
    case T_UTC_DATE:
    case T_LOCAL_TRANSACTION_ID:
    case T_PI:
    case T_ROW_COUNT:
    case T_LAST_INSERT_ID:
    case T_LIST_DBS:
    case T_TRACE_STATS:
    case T_DBTIMEZONE:
    case T_SESSIONTIMEZONE:
    case T_SYS_GUID:
    case T_UTC_TIMESTAMP:
      /* nothing to fetch */
      break;

    case T_BLOB_TO_BIT:
    case T_CLOB_TO_CHAR:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (!DB_IS_NULL (peek_left) && arithptr->rightptr)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_BIT_TO_BLOB:
    case T_CHAR_TO_CLOB:
    case T_LOB_LENGTH:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_ENUMERATION_VALUE:
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_WIDTH_BUCKET:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  goto error;
	}

      /* get peek_righ, peed_third we use PT_BETWEEN with PT_BETWEEN_GE_LT to represent the two args. */
      if (fetch_peek_min_max_value_of_width_bucket_func (thread_p, arithptr->rightptr, vd, obj_oid, tplrec, &peek_right,
							 &peek_third) != NO_ERROR)
	{
	  goto error;
	}

      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_fourth) != NO_ERROR)
	{
	  goto error;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto error;
    }

  if (arithptr->fails != NO_ERROR)
    {
      /* CBRD-27583 (spec 5): the operator rejects its operands' types; over values it raises the error, as it did at
       * the row, and a NULL operand gives no value (the fetch skips the operands after a NULL one) */
      pr_clear_value (arithptr->value);
      const bool null_operand = (peek_left != NULL && DB_IS_NULL (peek_left))
	|| (peek_right != NULL && DB_IS_NULL (peek_right)) || (peek_third != NULL && DB_IS_NULL (peek_third));
      if (!null_operand)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, arithptr->fails, 0);
	  goto error;
	}
      *peek_dbval = arithptr->value;
      goto fetch_peek_arith_end;
    }

  if (fetch_arith_node_resolve (vd, &args) != NO_ERROR)
    {
      goto error;
    }
  /* the operator's body: one function per opcode (fetch_arith_ops.c); an opcode without one keeps the cleared
   * value */
  {
    FETCH_ARITH_OP op = fetch_arith_op (arithptr->opcode);
    if (op != NULL)
      {
	int status;

	args.peek_left = peek_left;
	args.peek_right = peek_right;
	args.peek_third = peek_third;
	args.peek_fourth = peek_fourth;
	status = op (thread_p, &args);
	if (status != NO_ERROR)
	  {
	    if (status == FETCH_ARITH_RAW_LEFT)
	      {
		/* T_INCR, T_DECR: the node's value is its left operand itself, outside the domain epilogue */
		*peek_dbval = peek_left;
		goto fetch_peek_arith_end;
	      }
	    goto error;
	  }
      }
  }

  *peek_dbval = arithptr->value;

fetch_peek_arith_end:

  /* check for the first time */
  if (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST))
    {
      const bool all_const = fetch_arith_caches (arithptr) && fetch_regu_is_all_const (arithptr->leftptr)
	&& fetch_regu_is_all_const (arithptr->rightptr) && fetch_regu_is_all_const (arithptr->thirdptr);
      REGU_VARIABLE_SET_FLAG (regu_var, all_const ? REGU_VARIABLE_FETCH_ALL_CONST : REGU_VARIABLE_FETCH_NOT_CONST);
    }

  thread_dec_recursion_depth (thread_p);

  return NO_ERROR;

error:
  thread_dec_recursion_depth (thread_p);

  /* nothing to restore: a late or unbound computation took no execution domain */
  return ER_FAILED;
}

/*
 * fetch_convert_regu_value () - CBRD-27583: a regu's value converted into the domain its consumer wants, in the regu's
 *				 own holder (vfetch_to), which stays valid until the regu's next fetch
 *   return: NO_ERROR or the conversion's error; an arithmetic operand's failure is NULL under
 *	     return_null_on_function_errors, as qdata_coerce_arith_operands (REGU_CONV_NULL_ON_FUNCTION_ERRORS)
 *   regu_var(in): a regu with a converter
 *   peek_dbval(in/out): the value read in the regu's original domain; the converted value
 */
static int
fetch_convert_regu_value (const REGU_VARIABLE * regu_var, DB_VALUE ** peek_dbval)
{
  DB_VALUE *converted = regu_var->vfetch_to;
  assert (converted != NULL && converted != *peek_dbval);
  pr_clear_value (converted);
  if (DB_IS_NULL (*peek_dbval))
    {
      db_value_domain_init (converted, TP_DOMAIN_TYPE (regu_var->domain), regu_var->domain->precision,
			    regu_var->domain->scale);
      *peek_dbval = converted;
      return NO_ERROR;
    }
  /* the converter was found for the domain the compiler says the value is read in */
  assert (DB_VALUE_DOMAIN_TYPE (*peek_dbval) == TP_DOMAIN_TYPE (regu_var->original_domain)
	  || (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (*peek_dbval))
	      && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (regu_var->original_domain))));
  const TP_DOMAIN_STATUS status = tp_value_convert (regu_var->conv, regu_var->domain, *peek_dbval, converted);
  if (status != DOMAIN_COMPATIBLE)
    {
      if (!(regu_var->conv_mode & (REGU_CONV_NULL_ON_FUNCTION_ERRORS | REGU_CONV_INVALID_DATATYPE)))
	{
	  /* an assignment's failure, as fetch_cast_operand's caller reports it */
	  const int error = tp_domain_status_er_set (status, ARG_FILE_LINE, *peek_dbval, regu_var->domain);
	  pr_clear_value (converted);
	  return error;
	}
      if (!prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  int error = ER_QPROC_INVALID_DATATYPE;
	  if (regu_var->conv_mode & REGU_CONV_INVALID_DATATYPE)
	    {
	      /* the error ROUND's and TRUNC's tp_value_coerce gave */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  else
	    {
	      error = qdata_operand_coercion_error (status, *peek_dbval, regu_var->domain);
	    }
	  pr_clear_value (converted);
	  return error;
	}
      pr_clear_value (converted);
      db_value_domain_init (converted, TP_DOMAIN_TYPE (regu_var->domain), regu_var->domain->precision,
			    regu_var->domain->scale);
      er_clear ();
    }
  *peek_dbval = converted;
  return NO_ERROR;
}

/*
 * fetch_peek_dbval_slow () - full fetch path for every regu_var type; the hot cached-attribute case is
 *                            served by the inline fetch_peek_dbval () wrapper in fetch.h.
 *   return: NO_ERROR or ER_code
 *   regu_var(in/out): Regulator Variable
 *   vd(in): Value Descriptor
 *   cls_oid(in): Class Identifier
 *   obj_oid(in): Object Identifier
 *   tplrec(in): tuple slot (record + layout descriptor)
 *   peek_dbval(out): Set to the value ref resulting from the fetch operation
 *
 */
int
fetch_peek_dbval_slow (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * class_oid,
		       OID * obj_oid, QFILE_TUPLE_RECORD * tplrec, DB_VALUE ** peek_dbval)
{
  bool is_null;
  REGU_VARIABLE *regu = NULL;
  int error = NO_ERROR;
  REGU_VALUE_LIST *reguval_list = NULL;
  FUNCTION_TYPE *funcp = NULL;
  XASL_NODE *xasl;
  DB_VALUE position_value;

  switch (regu_var->type)
    {
    case TYPE_ATTR_ID:		/* fetch object attribute value */
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      if (regu_var->value.attr_descr.cache_slot != NULL
	  && regu_var->value.attr_descr.cache_slot->state == HEAP_LAZY_ATTRVALUE
	  && regu_var->value.attr_descr.cache_attrinfo->lazy_recdes != NULL)
	{
	  *peek_dbval = heap_attrvalue_peek_lazy (regu_var->value.attr_descr.cache_slot,
						  regu_var->value.attr_descr.cache_attrinfo);
	  if (*peek_dbval == NULL)
	    {
	      goto exit_on_error;
	    }
	  break;
	}
      *peek_dbval = regu_var->value.attr_descr.cache_dbvalp;
      if (*peek_dbval != NULL)
	{
	  /* we have a cached pointer already */
	  break;
	}
      else if (regu_var->value.attr_descr.cache_attrinfo != NULL
	       && regu_var->value.attr_descr.cache_attrinfo->lazy_recdes != NULL)
	{
	  /* first fetch of a predicate column in a lazy scan: locate its slot once. A first-term column is
	   * already read in place; a deferred one is read here and keeps its slot for the inline fast path. */
	  HEAP_ATTRVALUE *slot =
	    heap_attrvalue_locate (regu_var->value.attr_descr.id, regu_var->value.attr_descr.cache_attrinfo);
	  if (slot == NULL || slot->state == HEAP_UNINIT_ATTRVALUE)
	    {
	      er_log_debug (ARG_FILE_LINE, "fetch_peek_dbval_slow: unreadable attr = %d",
			    regu_var->value.attr_descr.id);
	      if (er_errid () == NO_ERROR)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		}
	      goto exit_on_error;
	    }
	  if (!slot->lazy_always_eager
	      && heap_attrvalue_peek_lazy (slot, regu_var->value.attr_descr.cache_attrinfo) == NULL)
	    {
	      goto exit_on_error;
	    }
	  *peek_dbval = &slot->dbvalue;
	  regu_var->value.attr_descr.cache_slot = slot->lazy_always_eager ? NULL : slot;
	}
      else
	{
	  *peek_dbval = heap_attrinfo_access (regu_var->value.attr_descr.id, regu_var->value.attr_descr.cache_attrinfo);
	  if (*peek_dbval == NULL)
	    {
	      goto exit_on_error;
	    }
	  regu_var->value.attr_descr.cache_slot = NULL;	/* eager fetch: no deferred slot for this regu */
	}
      regu_var->value.attr_descr.cache_dbvalp = *peek_dbval;	/* cache */
      break;

    case TYPE_OID:		/* fetch object identifier value */
      /* the row's object: not constant (develop's answers; before #8022 it was marked constant) */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      *peek_dbval = &regu_var->value.dbval;
      db_make_oid (*peek_dbval, obj_oid);
      break;

    case TYPE_CLASSOID:	/* fetch class identifier value */
      /* the scan's class (a partition's): not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      *peek_dbval = &regu_var->value.dbval;
      db_make_oid (*peek_dbval, class_oid);
      break;

    case TYPE_POSITION:	/* fetch list file tuple value */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);

      pr_clear_value (regu_var->vfetch_to);

      /* a converting regu reads into a temporary (a conversion's result is not its source), which a NULL column
       * leaves as it is */
      *peek_dbval = regu_var->vfetch_to;
      if (regu_var->conv != NULL)
	{
	  db_make_null (&position_value);
	  *peek_dbval = &position_value;
	}

      /* slot accessor: layout from the bound descriptor, domain from the position; NULL column leaves vfetch_to as
       * is */
      {
	TP_DOMAIN *column_domain = regu_var->value.pos_descr.dom;
	assert (TP_DOMAIN_TYPE (column_domain) != DB_TYPE_VARIABLE);
	if (TP_DOMAIN_TYPE (column_domain) == DB_TYPE_VARIABLE)
	  {
	    /* exception to deciding before the rows: a position its producer leaves untyped reads the list's column */
	    column_domain = tplrec->type_list->domp[regu_var->value.pos_descr.pos_no];
	  }
	if (column_domain->type == NULL)
	  {
	    goto exit_on_error;
	  }
	if (qfile_slot_read_column_value (tplrec, regu_var->value.pos_descr.pos_no, column_domain, *peek_dbval,
					  false /* Don't copy */ , &is_null) != NO_ERROR)
	  {
	    goto exit_on_error;
	  }
      }
      break;

    case TYPE_POS_VALUE:	/* fetch positional value */
#if defined(QP_DEBUG)
      if (regu_var->value.val_pos < 0 || regu_var->value.val_pos > (vd->dbval_cnt - 1))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_VALLIST_INDEX, 1, regu_var->value.val_pos);
	  goto exit_on_error;
	}
#endif

      /* a peek reads the bound value (val_descr.dbval_ptr is const) */
      *peek_dbval = (DB_VALUE *) vd->dbval_ptr + regu_var->value.val_pos;
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
      break;

    case TYPE_CONSTANT:	/* fetch constant-column value */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      xasl = regu_var->xasl;
      if (xasl && XASL_IS_FLAGED (xasl, XASL_USES_SQ_CACHE) && !(SQ_CACHE_HT (xasl) && !SQ_CACHE_ENABLED (xasl)))
	{
	  if (!sq_get (thread_p, SQ_CACHE_KEY_STRUCT (xasl), xasl, regu_var))
	    {
	      SQ_KEY *key;
	      /* execute linked query */
	      EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);
	      if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
		{
		  goto exit_on_error;
		}
	      if ((key = sq_make_key (thread_p, xasl)) == NULL)
		{
		  XASL_CLEAR_FLAG (xasl, XASL_USES_SQ_CACHE);
		  goto exit_on_error;
		}
	      if (sq_put (thread_p, key, xasl, regu_var) == ER_FAILED)
		{
		  sq_free_key (thread_p, key);
		}
	    }
	  else
	    {
	      /* FOUND */
	      xasl->status = XASL_SUCCESS;
	    }
	}
      else
	{
	  EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);
	  if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
	    {
	      goto exit_on_error;
	    }
	}

      *peek_dbval = regu_var->value.dbvalptr;
      break;

    case TYPE_ORDERBY_NUM:
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      *peek_dbval = regu_var->value.dbvalptr;
      break;

    case TYPE_DBVAL:		/* fetch db_value */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
      *peek_dbval = &regu_var->value.dbval;
      break;

    case TYPE_REGUVAL_LIST:
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      reguval_list = regu_var->value.reguval_list;
      assert (reguval_list != NULL);
      assert (reguval_list->current_value != NULL);

      regu = reguval_list->current_value->value;
      assert (regu != NULL);

      if (regu->type != TYPE_DBVAL && regu->type != TYPE_INARITH && regu->type != TYPE_POS_VALUE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	  goto exit_on_error;
	}

      error = fetch_peek_dbval (thread_p, regu, vd, class_oid, obj_oid, tplrec, peek_dbval);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case TYPE_INARITH:		/* compute and fetch arithmetic expr. value */
    case TYPE_OUTARITH:
      error = fetch_peek_arith (thread_p, regu_var, vd, obj_oid, tplrec, peek_dbval);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case TYPE_SP:		/* fetch stored procedure value */
      {
	TSC_TICKS start_tick, end_tick;
	TSCTIMEVAL tv_diff;
	UINT64 old_fetches = 0, old_ioreads = 0;

	/* clear any value from a previous iteration */
	pr_clear_value (regu_var->value.sp_ptr->value);
	fetch_force_not_const_recursive (*regu_var);

	if (thread_is_on_trace (thread_p))
	  {
	    old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
	    old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
	    tsc_getticks (&start_tick);
	  }

	cubpl::executor executor (*regu_var->value.sp_ptr->sig);
	if (er_errid () != NO_ERROR)
	  {
	    goto exit_on_error;
	  }

	error = executor.fetch_args_peek (regu_var->value.sp_ptr->args, vd, obj_oid, tplrec);
	if (error != NO_ERROR || er_errid () != NO_ERROR)
	  {
	    /* the stack can be null when the executor refused to start (interrupt, expired session) */
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1,
		    executor.get_stack ()? executor.get_stack ()->get_error_message ().c_str () : "");
	    goto exit_on_error;
	  }

	error = executor.execute (*regu_var->value.sp_ptr->value);
	if (error != NO_ERROR || er_errid () != NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1,
		    executor.get_stack ()? executor.get_stack ()->get_error_message ().c_str () : "");
	    goto exit_on_error;
	  }

	if (thread_is_on_trace (thread_p))
	  {
	    tsc_getticks (&end_tick);
	    tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);

	    perfmon_inc_stat_for_trace (thread_p, PSTAT_REGU_NUM_CALL_EVALS);
	    UINT64 time = tv_diff.tv_sec * 1000000LL + tv_diff.tv_usec;
	    perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_REGU_EVAL_TIME_10USEC].start_offset, time);
	    perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_REGU_NUM_FETCHES].start_offset,
					    perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches);
	    perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_REGU_NUM_IOREADS].start_offset,
					    perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads);
	  }

	*peek_dbval = regu_var->value.sp_ptr->value;
      }
      break;

    case TYPE_FUNC:		/* fetch function value */
      funcp = regu_var->value.funcp;
      assert (funcp != NULL);
      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST))
	{
	  *peek_dbval = funcp->value;
	  break;
	}

      error = qdata_evaluate_function (thread_p, regu_var, vd, obj_oid, tplrec);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      *peek_dbval = funcp->value;

      /* check for the first time */
      if (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST))
	{
	  REGU_VARIABLE_SET_FLAG (regu_var, fetch_function_caches (funcp) ? REGU_VARIABLE_FETCH_ALL_CONST
				  : REGU_VARIABLE_FETCH_NOT_CONST);
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto exit_on_error;
    }

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION))
    {
      /* the collation the value is read in, before a converting regu converts it */
      const TP_DOMAIN *collate = regu_produced_domain (regu_var);
      if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (collate)))
	{
	  if (!DB_IS_NULL (*peek_dbval))
	    {
	      assert (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (*peek_dbval)));
	      assert (collate->codeset == db_get_string_codeset (*peek_dbval));
	    }
	  db_string_put_cs_and_collation (*peek_dbval, collate->codeset, collate->collation_id);
	}
      else
	{
	  assert (TP_DOMAIN_TYPE (collate) == DB_TYPE_ENUMERATION);

	  if (!DB_IS_NULL (*peek_dbval))
	    {
	      assert (DB_VALUE_DOMAIN_TYPE (*peek_dbval) == DB_TYPE_ENUMERATION);
	      assert (collate->codeset == db_get_enum_codeset (*peek_dbval));
	    }
	  db_enum_put_cs_and_collation (*peek_dbval, collate->codeset, collate->collation_id);

	}
    }

  if (regu_var->conv != NULL && fetch_convert_regu_value (regu_var, peek_dbval) != NO_ERROR)
    {
      goto exit_on_error;
    }

  assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST)
	  || REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
  fetch_set_fast_peek (regu_var);

  return NO_ERROR;

exit_on_error:

  return ER_FAILED;
}

/*
 * fetch_peek_min_max_value_of_width_bucket_func () -
 *   return: NO_ERROR or ER_code
 *   regu_var(in): Regulator Variable of an ARITH node.
 *   vd(in): Value Descriptor
 *   obj_oid(in): Object Identifier
 *   tplrec(in): tuple slot (record + layout descriptor)
 *   min(out): the lower bound of width_bucket
 *   max(out): the upper bound of width_bucket
 */
int
fetch_peek_min_max_value_of_width_bucket_func (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd,
					       OID * obj_oid, QFILE_TUPLE_RECORD * tplrec, DB_VALUE ** min,
					       DB_VALUE ** max)
{
  int er_status = NO_ERROR;
  PRED_EXPR *pred_expr;
  PRED *pred;
  EVAL_TERM *eval_term1, *eval_term2;

  assert (min != NULL && max != NULL);

  if (regu_var == NULL || regu_var->type != TYPE_INARITH)
    {
      er_status = ER_QPROC_INVALID_XASLNODE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);

      goto error;
    }

  pred_expr = regu_var->value.arithptr->pred;
  if (pred_expr == NULL || pred_expr->type != T_PRED)
    {
      er_status = ER_QPROC_INVALID_XASLNODE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);

      goto error;
    }

  pred = &pred_expr->pe.m_pred;
  if (pred->lhs == NULL || pred->lhs->type != T_EVAL_TERM)
    {
      er_status = ER_QPROC_INVALID_XASLNODE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);

      goto error;
    }

  eval_term1 = &pred->lhs->pe.m_eval_term;
  if (eval_term1->et_type != T_COMP_EVAL_TERM)
    {
      er_status = ER_QPROC_INVALID_XASLNODE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);

      goto error;
    }

  /* lower bound, error info is already set in fetch_peek_dbval */
  er_status = fetch_peek_dbval (thread_p, eval_term1->et.et_comp.rhs, vd, NULL, obj_oid, tplrec, min);
  if (er_status != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_status = ER_QPROC_INVALID_XASLNODE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	}

      goto error;
    }

  eval_term2 = &pred->rhs->pe.m_eval_term;
  if (eval_term2->et_type != T_COMP_EVAL_TERM)
    {
      er_status = ER_QPROC_INVALID_XASLNODE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);

      goto error;
    }

  /* upper bound, error info is already set in fetch_peek_dbval */
  er_status = fetch_peek_dbval (thread_p, eval_term2->et.et_comp.rhs, vd, NULL, obj_oid, tplrec, max);
  if (er_status != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_status = ER_QPROC_INVALID_XASLNODE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	}

      goto error;
    }

error:

  return er_status;
}

/*
 * fetch_copy_dbval () - returns a COPY of a db_value which the caller
 *                    must clear
 *   return: NO_ERROR or ER_code
 *   regu_var(in/out): Regulator Variable
 *   vd(in): Value Descriptor
 *   cls_oid(in): Class Identifier
 *   obj_oid(in): Object Identifier
 *   tplrec(in): tuple slot (record + layout descriptor)
 *   dbval(out): Set to the value resulting from the fetch operation
 *
 * This routine uses the value description indicated by the regulator variable
 * to fetch the indicated value and store it in the dbval parameter.
 * The value may be fetched from either a heap file object instance,
 * or from a list file tuple, or from an all constants regulator variable
 * content. If the value is fetched from a heap file object instance,
 * then tpl parameter should be given NULL. Likewise, if the value is fetched
 * from a list file tuple, then obj_oid parameter should be given NULL.
 * If the value is fetched from all constant values referred to
 * by the regulator variable, then all of the parameters obj_oid,
 * tpl should be given NULL values.
 *
 * If the value description in the regulator variable refers other cases
 * such as constant value, arithmetic expression, the resultant value
 * is computed and stored in the db_value.
 *
 * see fetch_peek_dbval().
 */
int
fetch_copy_dbval (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * class_oid, OID * obj_oid,
		  QFILE_TUPLE_RECORD * tplrec, DB_VALUE * dbval)
{
  int result;
  DB_VALUE *readonly_val, copy_val, *tmp;

  db_make_null (&copy_val);

  result = fetch_peek_dbval (thread_p, regu_var, vd, class_oid, obj_oid, tplrec, &readonly_val);
  if (result != NO_ERROR)
    {
      return result;
    }

  /*
   * This routine needs to ensure that a copy happens.  If readonly_val
   * points to the same db_value as dbval, qdata_copy_db_value() won't copy.
   * This can happen with scans that are PEEKING and routines that
   * are sending the COPY flag to fetch_val_list() like the group by
   * code.  If this happens we use a stack variable for the copy and
   * then transfer ownership to the returned dbval
   */
  if (dbval == readonly_val)
    {
      tmp = &copy_val;
    }
  else
    {
      tmp = dbval;
    }

  if (!qdata_copy_db_value (tmp, readonly_val))
    {
      result = ER_FAILED;
    }

  if (tmp == &copy_val)
    {
      /*
       * transfer ownership to the real db_value via a
       * structure copy.  Make sure you clear the previous value.
       */
      pr_clear_value (dbval);
      *dbval = *tmp;
    }

  return result;
}

/*
 * fetch_val_list () - fetches all the values for the given regu variable list
 *   return: NO_ERROR or ER_code
 *   regu_list(in/out): Regulator Variable list
 *   vd(in): Value Descriptor
 *   class_oid(in): Class Identifier
 *   obj_oid(in): Object Identifier
 *   tplrec(in): tuple slot (record + layout descriptor)
 *   peek(int):
 */
int
fetch_val_list (THREAD_ENTRY * thread_p, regu_variable_list_node * regu_list, val_descr * vd, OID * class_oid,
		OID * obj_oid, QFILE_TUPLE_RECORD * tplrec, int peek)
{
  regu_variable_list_node *regup;
  int rc;
  DB_VALUE *tmp;

  if (peek)
    {
      if (regu_list && regu_list->value.type == TYPE_POSITION)
	{
	  /* the list is all TYPE_POSITION (existing invariant): one sequential pass over the tuple */
	  return fetch_peek_dbval_pos (regu_list, tplrec, vd);
	}
      for (regup = regu_list; regup != NULL; regup = regup->next)
	{
	  if (regup->value.vfetch_to && unlikely (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (regup->value.vfetch_to))))
	    {
	      pr_clear_value (regup->value.vfetch_to);
	    }
	  if (DB_NEED_CLEAR (regup->value.vfetch_to))
	    {
	      pr_clear_value (regup->value.vfetch_to);
	    }
	  rc = fetch_peek_dbval (thread_p, &regup->value, vd, class_oid, obj_oid, tplrec, &tmp);

	  if (rc != NO_ERROR)
	    {
	      pr_clear_value (regup->value.vfetch_to);
	      return ER_FAILED;
	    }

	  pr_share_value (tmp, regup->value.vfetch_to);
	}
    }
  else
    {
      /*
       * These DB_VALUES must persist across object fetches, so we must
       * use fetch_copy_dbval and NOT peek here.
       */
      for (regup = regu_list; regup != NULL; regup = regup->next)
	{
	  if (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (regup->value.vfetch_to)))
	    {
	      pr_clear_value (regup->value.vfetch_to);
	    }
	  if (fetch_copy_dbval (thread_p, &regup->value, vd, class_oid, obj_oid, tplrec, regup->value.vfetch_to) !=
	      NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }
  return NO_ERROR;
}

/*
 * fetch_peek_dbval_pos () - fetch_val_list (peek) for an all-TYPE_POSITION regu list: reads columns in pos_no order
 *   return: NO_ERROR or ER_code
 *   vd(in): the execution's value descriptor: a position reads its column with its execution domain
 */
static int
fetch_peek_dbval_pos (regu_variable_list_node * regu_list, QFILE_TUPLE_RECORD * tplrec, const VAL_DESCR * vd)
{
  regu_variable_list_node *regup;
  REGU_VARIABLE *regu_var;
  QFILE_TUPLE_VALUE_POSITION *pos_descr;
  bool is_null;
#if !defined(NDEBUG)
  int prev_pos = -1;
#endif

  for (regup = regu_list; regup != NULL; regup = regup->next)
    {
      regu_var = &regup->value;
      pos_descr = &regu_var->value.pos_descr;
      assert_release (regu_var->type == TYPE_POSITION);
      /* a list's producers convert nothing: a consumer that wants another domain has its own regu */
      assert (regu_var->conv == NULL);
#if !defined(NDEBUG)
      assert (pos_descr->pos_no >= prev_pos);
      prev_pos = pos_descr->pos_no;
#endif
      /* the position's domain; one its producer leaves untyped reads the list's column (an exception to deciding
       * before the rows) */
      assert (TP_DOMAIN_TYPE (pos_descr->dom) != DB_TYPE_VARIABLE);
      const TP_DOMAIN *column_domain = TP_DOMAIN_TYPE (pos_descr->dom) != DB_TYPE_VARIABLE ? pos_descr->dom
	: tplrec->type_list->domp[pos_descr->pos_no];
      if (column_domain->type == NULL)
	{
	  return ER_FAILED;
	}

      /* a need_clear-free FIXED column is just marked NULL; VAR columns always call pr_clear_value */
      if (tplrec->type_list->column_layout_array[pos_descr->pos_no].kind != QFILE_COL_FIXED
	  || regu_var->vfetch_to->need_clear || DB_NEED_CLEAR (regu_var->vfetch_to))
	{
	  pr_clear_value (regu_var->vfetch_to);
	}
      else
	{
	  PRIM_SET_NULL (regu_var->vfetch_to);
	}
      if (qfile_slot_read_column_value
	  (tplrec, pos_descr->pos_no, column_domain, regu_var->vfetch_to, false /* Don't copy */ ,
	   &is_null) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}

/*
 * fetch_init_val_list () -
 *   return:
 *   regu_list(in/out): Regulator Variable list
 */
void
fetch_init_val_list (regu_variable_list_node * regu_list)
{
  regu_variable_list_node *regu_p;
  REGU_VARIABLE *regu_var;

  for (regu_p = regu_list; regu_p; regu_p = regu_p->next)
    {
      regu_var = &regu_p->value;
      regu_var->value.attr_descr.cache_dbvalp = NULL;
      regu_var->value.attr_descr.cache_slot = NULL;	/* keep lazy slot in lock-step with cache_dbvalp */
    }
}

/*
 * is_argument_wrapped_with_cast_op () - check if regu_var is a cast
 *					 expression
 *   return: true/false
 *   regu_list(in/out): Regulator Variable list
 *   vd(in): Value Descriptor
 *   class_oid(in): Class Identifier
 *   obj_oid(in): Object Identifier
 *   tplrec(in): tuple slot (record + layout descriptor)
 *   peek(int):
 */
bool
is_argument_wrapped_with_cast_op (const REGU_VARIABLE * regu_var)
{
  if (regu_var == NULL)
    {
      return false;
    }

  if (regu_var->type == TYPE_INARITH || regu_var->type == TYPE_OUTARITH)
    {
      return (regu_var->value.arithptr->opcode == T_CAST || regu_var->value.arithptr->opcode == T_CAST_WRAP);
    }

  return false;
}

/* TODO: next functions are duplicate from string op func that get as argument PT_OP_TYPE. Please try to merge
 *       PT_OP_TYPE and OPERATOR_TYPE.
 */

/*
 * key_limit_coerce () - CBRD-27583 (D-94): convert a key limit value by its regu's compiled type
 *   return: the conversion's status
 *   regu_var(in): the regu the value was fetched from
 *   value(in): the fetched value, of the regu's type (a bind type signature's plan)
 *   dest(out): the converted value
 *   target(in): BIGINT, or DOUBLE for an overflowed bound's sign
 *   conv(in): the converter the unpack found (KEY_INFO key_limit_l_conv, key_limit_u_conv), NULL for none
 *
 * Note: a bound that does not fit BIGINT or is NULL falls back to key_limit_eval (), whose operands and the DOUBLE
 *       sign have no unpack converter: those are found by the compiled type at scan open, never by the value.
 */
static TP_DOMAIN_STATUS
key_limit_coerce (const REGU_VARIABLE * regu_var, const DB_VALUE * value, DB_VALUE * dest, const TP_DOMAIN * target,
		  TP_VALUE_CONVERTER conv)
{
  const DB_TYPE planned = TP_DOMAIN_TYPE (regu_var->domain);
  assert (DB_VALUE_DOMAIN_TYPE (value) == planned
	  || (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (value)) && TP_IS_CHAR_TYPE (planned)));
  if (conv == NULL && planned != TP_DOMAIN_TYPE (target))
    {
      conv = tp_value_find_converter (planned, target, DOMAIN_CONVERT_IMPLICIT);
    }
  return tp_value_coerce_with_converter (value, dest, target, planned, conv);
}

/*
 * key_limit_eval () - evaluate a key limit expression tree in BIGINT arithmetic
 *
 *   return        : NO_ERROR or error code
 *   thread_p(in)  : thread entry
 *   regu_var(in)  : key limit expression (lower or upper)
 *   vd(in)        : value descriptor
 *   result(out)   : evaluated BIGINT, valid when *is_null is false
 *   is_null(out)  : set to true when a NULL takes part in the expression
 *
 * Note: Key limits are built from T_SUB (rownum < N is N - 1, a range is upper - lower), T_LEAST and
 *       T_GREATEST (merge with a user KEYLIMIT). The optimizer marks those nodes with
 *       REGU_VARIABLE_KEY_LIMIT_ARITH and they are computed here in BIGINT, so a bound beyond the BIGINT
 *       range never reaches the generic arithmetic. Any other node, including arithmetic written by the
 *       user such as rownum <= ? - ?, is fetched as a whole and only its value is looked at.
 *       A negative bound only means "nothing to skip" or "nothing to read", so it becomes -1 whatever its
 *       magnitude. The sign is kept for the KEYLIMIT check and the upper += lower adjustment in
 *       scan_init_index_key_limit (). A positive bound beyond the BIGINT range becomes DB_BIGINT_MAX.
 */
static int
key_limit_eval (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, VAL_DESCR * vd, DB_BIGINT * result, bool * is_null)
{
  TP_DOMAIN *bigint_domain = tp_domain_resolve_default (DB_TYPE_BIGINT);
  TP_DOMAIN_STATUS dom_status;
  DB_VALUE *peek_val;
  DB_VALUE coerced_val;
  DB_BIGINT left, right, diff;
  int error_code;

  assert (regu_var != NULL);

  if (regu_var->type == TYPE_INARITH && regu_var->value.arithptr != NULL
      && REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_KEY_LIMIT_ARITH))
    {
      ARITH_TYPE *arithptr = regu_var->value.arithptr;

      switch (arithptr->opcode)
	{
	case T_SUB:
	case T_LEAST:
	case T_GREATEST:
	  error_code = key_limit_eval (thread_p, arithptr->leftptr, vd, &left, is_null);
	  if (error_code != NO_ERROR || *is_null)
	    {
	      return error_code;
	    }
	  error_code = key_limit_eval (thread_p, arithptr->rightptr, vd, &right, is_null);
	  if (error_code != NO_ERROR || *is_null)
	    {
	      return error_code;
	    }

	  if (arithptr->opcode == T_SUB)
	    {
	      if (__builtin_sub_overflow (left, right, &diff))
		{
		  /* keep an overflowed difference in BIGINT: unbounded if positive, nothing to read if negative
		   * ex) rownum between -H and H: scan count = DB_BIGINT_MAX - (-2) */
		  diff = (right < 0) ? DB_BIGINT_MAX : -1;
		}
	      *result = diff;
	    }
	  else if (arithptr->opcode == T_LEAST)
	    {
	      *result = (left < right) ? left : right;
	    }
	  else
	    {
	      *result = (left > right) ? left : right;
	    }
	  return NO_ERROR;

	default:
	  break;
	}
    }

  error_code = fetch_peek_dbval (thread_p, regu_var, vd, NULL, NULL, NULL, &peek_val);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (DB_IS_NULL (peek_val))
    {
      *is_null = true;
      return NO_ERROR;
    }

  db_make_null (&coerced_val);
  dom_status = key_limit_coerce (regu_var, peek_val, &coerced_val, bigint_domain, NULL);
  if (dom_status == DOMAIN_COMPATIBLE)
    {
      *result = db_get_bigint (&coerced_val);
      if (*result < 0)
	{
	  /* a negative bound skips or reads nothing whatever its magnitude, so keep only the sign
	   * ex) -9223372036854775807 < rownum < 5: lower -1, upper (5 - 1) - (-1) = 5, then 4 rows */
	  *result = -1;
	}
      return NO_ERROR;
    }

  if (dom_status == DOMAIN_OVERFLOW)
    {
      /* beyond the BIGINT range: only the sign matters, whatever the type (NUMERIC, DOUBLE, string)
       * ex) rownum < 1e46: upper DB_BIGINT_MAX - 1, all rows.  rownum < -1e46: upper -1 - 1, no rows */
      db_make_null (&coerced_val);
      if (key_limit_coerce (regu_var, peek_val, &coerced_val, tp_domain_resolve_default (DB_TYPE_DOUBLE), NULL)
	  == DOMAIN_COMPATIBLE)
	{
	  *result = (db_get_double (&coerced_val) < 0) ? -1 : DB_BIGINT_MAX;
	  return NO_ERROR;
	}
    }

  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_val, bigint_domain);
  return ER_FAILED;
}

/*
 * key_limit_fetch_bigint () - fetch a key limit expression as a whole and coerce it to BIGINT
 *   return      : true on success; false when the fetch fails, the value does not fit in BIGINT or is NULL.
 *                 No error is set here for the last two cases
 *   thread_p(in): thread entry
 *   regu_var(in): key limit expression
 *   conv(in)    : its converter into BIGINT, found at unpack
 *   vd(in)      : value descriptor
 *   result(out) : BIGINT value on success
 */
static bool
key_limit_fetch_bigint (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, TP_VALUE_CONVERTER conv, VAL_DESCR * vd,
			DB_BIGINT * result)
{
  DB_VALUE *peek_val;
  DB_VALUE coerced_val;

  if (fetch_peek_dbval (thread_p, regu_var, vd, NULL, NULL, NULL, &peek_val) != NO_ERROR)
    {
      return false;
    }

  db_make_null (&coerced_val);
  if (DB_IS_NULL (peek_val)
      || key_limit_coerce (regu_var, peek_val, &coerced_val, &tp_Bigint_domain, conv) != DOMAIN_COMPATIBLE
      || DB_IS_NULL (&coerced_val) || DB_VALUE_DOMAIN_TYPE (&coerced_val) != DB_TYPE_BIGINT)
    {
      return false;
    }

  *result = db_get_bigint (&coerced_val);
  return true;
}

/*
 * fetch_key_limits () - fetch the lower and upper key limits as BIGINT
 *
 *   return          : NO_ERROR or error code
 *   thread_p(in)    : thread entry
 *   key_limit_l(in) : lower key limit expression, may be NULL
 *   l_conv(in)      : its converter into BIGINT, found at unpack (KEY_INFO key_limit_l_conv)
 *   key_limit_u(in) : upper key limit expression, may be NULL
 *   u_conv(in)      : its converter into BIGINT, found at unpack (KEY_INFO key_limit_u_conv)
 *   vd(in)          : value descriptor
 *   lower(out)      : lower limit, set when key_limit_l is not NULL
 *   upper(out)      : upper limit, set when key_limit_u is not NULL
 *   is_null(out)    : set to true when a NULL takes part in either bound (no row can match)
 *
 * Note: Each bound is first fetched as a whole and coerced to BIGINT, so every in-range query behaves as
 *       before. When either bound fails that way (a value beyond the BIGINT range, or NULL), the error
 *       left by the fetch is cleared and both bounds are evaluated again with key_limit_eval (), which
 *       reports the error if the value is really bad. Both bounds, because the upper bound of a range is
 *       built as upper - lower and the two sides have to see the same lower value.
 */
int
fetch_key_limits (THREAD_ENTRY * thread_p, REGU_VARIABLE * key_limit_l, TP_VALUE_CONVERTER l_conv,
		  REGU_VARIABLE * key_limit_u, TP_VALUE_CONVERTER u_conv, VAL_DESCR * vd, DB_BIGINT * lower,
		  DB_BIGINT * upper, bool * is_null)
{
  int error_code;

  assert (vd != NULL);
  assert (is_null != NULL);
  assert (key_limit_l == NULL || lower != NULL);
  assert (key_limit_u == NULL || upper != NULL);

  *is_null = false;

  if ((key_limit_l == NULL || key_limit_fetch_bigint (thread_p, key_limit_l, l_conv, vd, lower))
      && (key_limit_u == NULL || key_limit_fetch_bigint (thread_p, key_limit_u, u_conv, vd, upper)))
    {
      return NO_ERROR;
    }

  er_clear ();

  if (key_limit_l != NULL)
    {
      error_code = key_limit_eval (thread_p, key_limit_l, vd, lower, is_null);
      if (error_code != NO_ERROR || *is_null)
	{
	  return error_code;
	}
    }

  if (key_limit_u != NULL)
    {
      error_code = key_limit_eval (thread_p, key_limit_u, vd, upper, is_null);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  return NO_ERROR;
}
