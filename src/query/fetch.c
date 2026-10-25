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
  if (regu_var->domain == NULL)
    {
      return AGG_EXPR_TYPE_GROUP_NONE;
    }

  switch (TP_DOMAIN_TYPE (regu_var->domain))
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
      return (regu_var->domain != NULL && TP_DOMAIN_TYPE (regu_var->domain) == DB_TYPE_NUMERIC
	      && regu_var->domain->precision == DB_DEFAULT_NUMERIC_PRECISION
	      && regu_var->domain->scale == DB_DEFAULT_NUMERIC_SCALE
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

  switch (TP_DOMAIN_TYPE (regu_var->domain))
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

/* How a late-binding node takes its domain at its first computation in an execution */
enum FETCH_RESOLVED_READING
{
  FETCH_RESOLVED_DOMAIN,	/* the resolved domain, or a cast's compiled target */
  FETCH_RESOLVED_NO_VALUE,	/* resolve_domains found the node takes no value: a NULL operand, a pair its operator
				 * rejects, a collation pair that does not merge, a cast into a target left variable;
				 * the row computes the node unbound and gives NULL or the operator's error */
  FETCH_RESOLVED_UNRESOLVED	/* no resolution where the plan promised one: the unresolved-domain check (execution) */
};

/* The row path's readings of the resolved domains: inlined at every call in a release build. */
STATIC_INLINE FETCH_RESOLVED_READING fetch_arith_resolved_domain (const VAL_DESCR * vd, const ARITH_TYPE * arithptr,
								  const TP_DOMAIN ** resolved_domain)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool fetch_constant_evaluated (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int fetch_read_plan_domain (REGU_VARIABLE * regu_var, val_descr * vd, const DB_VALUE * value)
  __attribute__ ((ALWAYS_INLINE));

/*
 * fetch_arith_resolved_domain () - how an arithmetic node whose compiled domain is variable takes its domain now
 *   return: the reading; *resolved is set for FETCH_RESOLVED_DOMAIN
 *
 * resolve_domains resolved every late-binding node before the main block (qexec_resolve_domains); the row reads that
 * resolution and never derives one from a value, except where the reading says LATE.
 */
static inline FETCH_RESOLVED_READING
fetch_arith_resolved_domain (const VAL_DESCR * vd, const ARITH_TYPE * arithptr, const TP_DOMAIN ** resolved_domain)
{
  if (arithptr->opcode == T_CAST || arithptr->opcode == T_CAST_WRAP || arithptr->opcode == T_CAST_NOFAIL)
    {
      /* the value is cast into the compiled target, so the target is the node's domain; a target the compiler left
       * variable casts no value: a NULL stays NULL and any other value fails (-181) */
      if (arithptr->domain != NULL && TP_DOMAIN_TYPE (arithptr->domain) != DB_TYPE_VARIABLE)
	{
	  *resolved_domain = arithptr->domain;
	  return FETCH_RESOLVED_DOMAIN;
	}
      return FETCH_RESOLVED_NO_VALUE;
    }
  /* every execution-time descriptor carries its resolved-domain state: a PX worker's and a hash join worker's inherit
   * it through qexec_deep_copy_xasl_state. A regu fetched without one is a stream's, whose domains are all fixed, or a
   * temporary one over a typed list: none has a variable domain */
  const DOMAIN_PLAN_ITEM *item = arithptr->plan_item;
  if (vd == NULL || vd->xasl_state == NULL)
    {
      return FETCH_RESOLVED_UNRESOLVED;
    }
  const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved_domain;
  if (item == NULL || !(item->flags & DOMAIN_PLAN_LATE_BIND) || !qexec_owns_resolved_index (resolved, item))
    {
      return FETCH_RESOLVED_UNRESOLVED;
    }
  const TP_DOMAIN *domain = resolved.domains[item->resolved_index].domain;
  if (domain == NULL || TP_DOMAIN_TYPE (domain) == DB_TYPE_NULL)
    {
      return FETCH_RESOLVED_NO_VALUE;
    }
  *resolved_domain = domain;
  return FETCH_RESOLVED_DOMAIN;
}

/* Whether resolve_domains already evaluated this constant expression: its value is in resolve_domains' array.
 * It replaces fetch's FETCH_ALL_CONST mark, which the first computation set on the plan. The value's index is the
 * plan's, inside every execution's array, a PX worker's copy included: asserted, not tested at every row. */
static inline bool
fetch_constant_evaluated (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  if (item == NULL || item->ref < 0 || vd == NULL)
    {
      return false;
    }
  /* an item with a value slot (ref) belongs to a plan resolve_domains resolved, so its descriptor holds that state;
   * a descriptor without one (a predicate stream's) meets bare items, whose ref is -1 */
  assert (vd->xasl_state != NULL);
  const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved_domain;
  assert (resolved.value_states != NULL && item->ref < resolved.n_vals);
  return resolved.value_states[item->ref] == DOMAIN_VALUE_EVALUATED;
}

/* fetch_constant_evaluated for a program's step (out of line; the inline one is this file's) */
bool
fetch_arith_constant_evaluated (const val_descr * vd, const DOMAIN_PLAN_ITEM * item)
{
  return fetch_constant_evaluated (vd, item);
}

/*
 * fetch_arith_node_domains () - the domains an arithmetic node has now in this execution: its execution domains, or
 *   the compiled ones in regu_var->domain and arithptr->domain, which never change
 */
void
fetch_arith_node_domains (val_descr * vd, FETCH_ARITH_ARGS * a)
{
  a->domain = qexec_get_node_domain (vd, a->regu_var->domain, a->regu_var->plan_item);
  a->arith_domain = qexec_get_node_domain (vd, a->arithptr->domain, a->arithptr->plan_item);
  a->no_value_domain = NULL;
}

/*
 * fetch_arith_node_resolve () - the node's result slot cleared and, for a node whose compiled domain is variable, the
 *   domain resolve_domains resolved before the main block taken as its execution domain; the operator runs next
 *   return: NO_ERROR, or ER_FAILED at the unresolved-domain check (execution)
 *
 * The interpreter (fetch_peek_arith) and a program's generic step share this protocol around an operator: the operand
 * values differ in where they come from, the node's domains and the epilogue (fetch_arith_node_end) do not.
 */
int
fetch_arith_node_resolve (val_descr * vd, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *arith_domain = a->arith_domain;
  TP_DOMAIN *no_value_domain = a->no_value_domain;

  /* evaluate arithmetic expression */

  /* clear any previous result */
  pr_clear_value (arithptr->value);
  if (domain != NULL && TP_DOMAIN_TYPE (domain) == DB_TYPE_VARIABLE)
    {
      /* the node's first computation in this execution takes the domain resolve_domains resolved before the main block
       * as its execution domain for the rest of the execution. A character result carries its merged
       * collation. */
      const TP_DOMAIN *resolved_domain = NULL;
      FETCH_RESOLVED_READING reading = fetch_arith_resolved_domain (vd, arithptr, &resolved_domain);
      switch (reading)
	{
	case FETCH_RESOLVED_DOMAIN:
	  domain = (TP_DOMAIN *) resolved_domain;
	  qexec_set_node_domain (vd, regu_var->plan_item, regu_var->domain, resolved_domain);
	  if (resolved_domain != arith_domain)
	    {
	      arith_domain = (TP_DOMAIN *) resolved_domain;
	      qexec_set_node_domain (vd, arithptr->plan_item, arithptr->domain, resolved_domain);
	    }
	  break;

	case FETCH_RESOLVED_NO_VALUE:
	  no_value_domain = domain;
	  domain = NULL;
	  break;

	case FETCH_RESOLVED_UNRESOLVED:
	default:
	  (void) domain_unresolved_error (qexec_query_alias (vd), qexec_item_index (vd, arithptr->plan_item),
					  DB_TYPE_VARIABLE);
	  return ER_FAILED;
	}
    }
  a->domain = domain;
  a->arith_domain = arith_domain;
  a->no_value_domain = no_value_domain;
  return NO_ERROR;
}

/*
 * fetch_arith_node_end () - the node's epilogue after its operator: a node resolve_domains found to take no value must
 *   have given NULL, and a string whose collation resolve_domains merged takes the merged domain for the execution
 *   return: NO_ERROR, or ER_FAILED at the unresolved-domain check (execution)
 */
int
fetch_arith_node_end (val_descr * vd, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *arith_domain = a->arith_domain;
  TP_DOMAIN *no_value_domain = a->no_value_domain;


  if (no_value_domain != NULL)
    {
      /* FETCH_RESOLVED_NO_VALUE: resolve_domains found the node takes no value, so the row gave NULL or raised the
       * operator's error (a return_null_on_function_errors NULL included); the node binds nothing: an unbound node
       * whose value is NULL keeps its variable domain */
      assert (DB_IS_NULL (arithptr->value));
      if (!DB_IS_NULL (arithptr->value))
	{
	  (void) domain_unresolved_error (qexec_query_alias (vd), qexec_item_index (vd, arithptr->plan_item),
					  DB_TYPE_NULL);
	  return ER_FAILED;
	}
      domain = no_value_domain;
    }

  if (arith_domain != NULL && arith_domain->collation_flag != TP_DOMAIN_COLL_NORMAL && !DB_IS_NULL (arithptr->value))
    {
      assert (TP_TYPE_HAS_COLLATION (arith_domain->type->id));

      const TP_DOMAIN *resolved_domain = qexec_resolved_domain (vd, arithptr->plan_item);
      if (resolved_domain != NULL)
	{
	  /* resolve_domains resolved this string's domain once for the execution from its operands' resolved domains;
	   * the row reads it in place of its value's. A NULL value keeps the compiled domain. */
#if !defined (NDEBUG)
	  /* debug cross-check: the resolution is the value's domain but for a string's precision, which stays the
	   * value's */
	  {
	    const TP_DOMAIN *value_domain = tp_domain_resolve_value (arithptr->value, NULL);
	    assert (value_domain == NULL || (TP_DOMAIN_TYPE (value_domain) == TP_DOMAIN_TYPE (resolved_domain)
					     && (!TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (resolved_domain))
						 || (value_domain->codeset == resolved_domain->codeset
						     && value_domain->collation_id == resolved_domain->collation_id))));
	  }
#endif
	  domain = (TP_DOMAIN *) resolved_domain;
	  qexec_set_node_domain (vd, regu_var->plan_item, regu_var->domain, domain);
	}
      else
	{
	  /* resolve_domains resolves every string, a string over a session variable read too: a string
	   * without a resolution fails the unresolved-domain check (execution) */
	  (void) domain_unresolved_error (qexec_query_alias (vd), qexec_item_index (vd, arithptr->plan_item),
					  TP_DOMAIN_TYPE (arith_domain));
	  return ER_FAILED;
	}
    }

  a->domain = domain;
  return NO_ERROR;
}

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
  if (fetch_constant_evaluated (vd, arithptr->plan_item))
    {
      /* a constant expression resolve_domains evaluated once */
      *peek_dbval = vd->dbval_ptr + arithptr->plan_item->ref;
      return NO_ERROR;
    }
  /* the node and the domains it has now in this execution (fetch_arith_node_domains); the operand fetch rules read
   * the node's domain (T_ADD's oracle_style_empty_string) */
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
   * load time (domain_mark_aggregate_operands (), once per XASL load).
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
	      /* check for result type: a variable domain is the resolved domain; a node resolve_domains resolved
	       * nothing for reads the values' domains */
	      const TP_DOMAIN *result_domain = domain;
	      const TP_DOMAIN *resolved_domain = NULL;
	      if (TP_DOMAIN_TYPE (result_domain) == DB_TYPE_VARIABLE
		  && fetch_arith_resolved_domain (vd, arithptr, &resolved_domain) == FETCH_RESOLVED_DOMAIN)
		{
		  result_domain = resolved_domain;
		}
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
  if (fetch_arith_node_end (vd, &args) != NO_ERROR)
    {
      goto error;
    }

fetch_peek_arith_end:

#if !defined(NDEBUG)
  /* the operators a fetch never caches are the load's non-cacheable ones, which resolve_domains evaluates none of */
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
    case T_EXEC_STATS:
    case T_TRACE_STATS:
    case T_RAND:
    case T_RANDOM:
    case T_DRAND:
    case T_DRANDOM:
    case T_SYS_GUID:
    case T_UUID:
    case T_SLEEP:
      assert (arithptr->plan_item == NULL || arithptr->plan_item->operand_class == OPERAND_NON_CACHEABLE);
      break;
    default:
      break;
    }
  if (arithptr->pred != NULL)
    {
      assert (arithptr->plan_item == NULL || arithptr->plan_item->operand_class == OPERAND_NON_CACHEABLE);
    }
#endif

  thread_dec_recursion_depth (thread_p);

  return NO_ERROR;

error:
  thread_dec_recursion_depth (thread_p);

  /* nothing to restore: a late or unbound computation took no execution domain */
  return ER_FAILED;
}

/*
 * fetch_read_plan_domain () - a variable regu takes the domain resolve_domains resolved for it as its execution domain
 *   for the rest of the execution
 *   return: NO_ERROR, or ER_QPROC_DOMAIN_UNRESOLVED at the unresolved-domain check (execution)
 *   regu_var(in): a regu whose compiled domain is VARIABLE or leaves its collation variable
 *   value(in): its non-NULL value of this row
 *
 * resolve_domains recorded a variable POS's bound value domain (codeset and collation included), and a derived consumer
 * or a string function reads its producer's or its own resolution, so the domain the first value would give is
 * already there, a resolution over a session variable read included: the variable keeps the type resolve_domains gave
 * it for the statement. A bind value is the one resolve_domains saw: a comparison converts its constant into a value of
 * its own, not in place.
 */
static inline int
fetch_read_plan_domain (REGU_VARIABLE * regu_var, val_descr * vd, const DB_VALUE * value)
{
  /* a regu fetched without a descriptor with resolved-domain state has no resolution to read: the unresolved-domain
   * check */
  const DOMAIN_PLAN_ITEM *item = vd != NULL && vd->xasl_state != NULL ? regu_var->plan_item : NULL;
  const TP_DOMAIN *resolved = qexec_plan_domain (vd, item);
  if (resolved != NULL)
    {
#if !defined (NDEBUG)
      /* the resolution is the value's domain but for a string's precision, which stays the value's */
      const TP_DOMAIN *from_value = tp_domain_resolve_value (value, NULL);
      assert (from_value == NULL || (TP_DOMAIN_TYPE (from_value) == TP_DOMAIN_TYPE (resolved)
				     && (!TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (resolved))
					 || (from_value->codeset == resolved->codeset
					     && from_value->collation_id == resolved->collation_id))));
#endif
      /* a list position's resolution is its list scan's resolved column, which the position's execution domain holds
       * already */
      assert (regu_var->type != TYPE_POSITION
	      || qexec_get_node_domain (vd, regu_var->value.pos_descr.dom,
					regu_var->value.pos_descr.plan_item) == resolved
	      || domain_is_variable (regu_var->value.pos_descr.dom));
      qexec_set_node_domain (vd, regu_var->plan_item, regu_var->domain, resolved);
      return NO_ERROR;
    }
  /* resolve_domains resolves every variable regu, one over a session variable read too: none here fails the
   * unresolved-domain check (execution) */
  return domain_unresolved_error (qexec_query_alias (vd), qexec_item_index (vd, item),
				  TP_DOMAIN_TYPE (regu_var->domain));
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
  REGU_VARIABLE *head_regu = NULL, *regu = NULL;
  int error = NO_ERROR;
  REGU_VALUE_LIST *reguval_list = NULL;
  DB_TYPE head_type, cur_type;
  FUNCTION_TYPE *funcp = NULL;
  XASL_NODE *xasl;

  switch (regu_var->type)
    {
    case TYPE_ATTR_ID:		/* fetch object attribute value */
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
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
      *peek_dbval = &regu_var->value.dbval;
      db_make_oid (*peek_dbval, obj_oid);
      break;

    case TYPE_CLASSOID:	/* fetch class identifier value */
      *peek_dbval = &regu_var->value.dbval;
      db_make_oid (*peek_dbval, class_oid);
      break;

    case TYPE_POSITION:	/* fetch list file tuple value */

      pr_clear_value (regu_var->vfetch_to);

      *peek_dbval = regu_var->vfetch_to;

      /* slot accessor: layout from the bound descriptor, domain from the position's execution domain - the list scan
       * or the block that reads the list gave a variable position its resolved domain; NULL column leaves vfetch_to
       * as is */
      {
	TP_DOMAIN *column_domain =
	  qexec_get_node_domain (vd, regu_var->value.pos_descr.dom, regu_var->value.pos_descr.plan_item);
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

      /* each bind reference reads its own value. An operand-less aggregate's placeholder operand
       * (GROUPBY_NUM: an unset TYPE_POS_VALUE, no domain) is no bind reference and has no item; it reads its position
       * (none when there are no values) */
      *peek_dbval = regu_var->plan_item != NULL ? (DB_VALUE *) REGU_RESOLVED_VALUE (vd, regu_var)
	: (DB_VALUE *) vd->dbval_ptr + regu_var->value.val_pos;
      break;

    case TYPE_CONSTANT:	/* fetch constant-column value */
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
      *peek_dbval = regu_var->value.dbvalptr;
      break;

    case TYPE_DBVAL:		/* fetch db_value */
      *peek_dbval = &regu_var->value.dbval;
      break;

    case TYPE_REGUVAL_LIST:
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
      if (fetch_constant_evaluated (vd, regu_var->plan_item))
	{
	  /* a constant function resolve_domains evaluated once */
	  *peek_dbval = vd->dbval_ptr + regu_var->plan_item->ref;
	  return NO_ERROR;
	}

      error = qdata_evaluate_function (thread_p, regu_var, vd, obj_oid, tplrec);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      funcp = regu_var->value.funcp;
      assert (funcp != NULL);

      /* ELT over branches whose collations resolve_domains merged: the picked value takes the merged domain */
      if (fetch_arith_convert_to_resolved_branch (vd, regu_var->plan_item, funcp->value) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      *peek_dbval = funcp->value;

#if !defined(NDEBUG)
      switch (funcp->ftype)
	{
	case F_SET:
	case F_MULTISET:
	case F_SEQUENCE:
	case F_VID:
	case F_TABLE_SET:
	case F_TABLE_MULTISET:
	case F_TABLE_SEQUENCE:
	case F_GENERIC:
	case F_CLASS_OF:
	case F_BENCHMARK:
	  /* a fetch never caches these: the load marks them non-cacheable */
	  assert (regu_var->plan_item == NULL || regu_var->plan_item->operand_class == OPERAND_NON_CACHEABLE);
	  break;

	case F_INSERT_SUBSTRING:
	case F_ELT:
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
	  break;

	default:
	  assert (false);	/* is impossible */
	  break;
	}
#endif
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto exit_on_error;
    }

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION))
    {
      const TP_DOMAIN *collate = qexec_get_node_domain (vd, regu_var->domain, regu_var->plan_item);
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

  if (*peek_dbval != NULL && !DB_IS_NULL (*peek_dbval))
    {
      if (qexec_node_domain_is_variable (vd, regu_var->plan_item))
	{
	  /* the domain resolve_domains resolved before the main block - a variable POS's is its bound value's, a value
	   * pointer's or a list position's its producer's, a string function's its own */
	  if (fetch_read_plan_domain (regu_var, vd, *peek_dbval) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* for REGUVAL_LIST compare type with the corresponding column of first row if not compatible, raise an error
       * This is the same behavior as "union" */
      if (regu_var->type == TYPE_REGUVAL_LIST)
	{
	  head_regu = reguval_list->regu_list->value;
	  regu = reguval_list->current_value->value;

	  if (regu->domain == NULL || qexec_node_domain_is_variable (vd, regu->plan_item))
	    {
	      /* the row's own resolution, read as above */
	      if (fetch_read_plan_domain (regu, vd, *peek_dbval) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  const TP_DOMAIN *row_domain = qexec_get_node_domain (vd, regu->domain, regu->plan_item);
	  const TP_DOMAIN *head_domain = qexec_get_node_domain (vd, head_regu->domain, head_regu->plan_item);
	  head_type = TP_DOMAIN_TYPE (head_domain);
	  cur_type = TP_DOMAIN_TYPE (row_domain);

	  /* compare the type */
	  if (head_type != DB_TYPE_NULL && cur_type != DB_TYPE_NULL && head_domain != row_domain)
	    {
	      if (head_type != cur_type || !pr_is_string_type (head_type) || !pr_is_variable_type (head_type))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INCOMPATIBLE_TYPES, 0);
		  goto exit_on_error;
		}
	      else if (TP_DOMAIN_COLLATION (head_domain) != TP_DOMAIN_COLLATION (row_domain))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
		  goto exit_on_error;
		}
	    }
	}
    }

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
#if !defined(NDEBUG)
      assert (pos_descr->pos_no >= prev_pos);
      prev_pos = pos_descr->pos_no;
#endif
      /* the position's domains in this execution: its execution domain, which the list scan filled */
      const TP_DOMAIN *column_domain = qexec_get_node_domain (vd, pos_descr->dom, pos_descr->plan_item);
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
  dom_status = tp_value_coerce (peek_val, &coerced_val, bigint_domain);
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
      if (tp_value_coerce (peek_val, &coerced_val, tp_domain_resolve_default (DB_TYPE_DOUBLE)) == DOMAIN_COMPATIBLE)
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
 *   vd(in)      : value descriptor
 *   result(out) : BIGINT value on success
 */
static bool
key_limit_fetch_bigint (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, VAL_DESCR * vd, DB_BIGINT * result)
{
  DB_VALUE *peek_val;
  DB_VALUE coerced_val;

  if (fetch_peek_dbval (thread_p, regu_var, vd, NULL, NULL, NULL, &peek_val) != NO_ERROR)
    {
      return false;
    }

  db_make_null (&coerced_val);
  if (tp_value_coerce (peek_val, &coerced_val, tp_domain_resolve_default (DB_TYPE_BIGINT)) != DOMAIN_COMPATIBLE
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
 *   key_limit_u(in) : upper key limit expression, may be NULL
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
fetch_key_limits (THREAD_ENTRY * thread_p, REGU_VARIABLE * key_limit_l, REGU_VARIABLE * key_limit_u, VAL_DESCR * vd,
		  DB_BIGINT * lower, DB_BIGINT * upper, bool * is_null)
{
  int error_code;

  assert (vd != NULL);
  assert (is_null != NULL);
  assert (key_limit_l == NULL || lower != NULL);
  assert (key_limit_u == NULL || upper != NULL);

  *is_null = false;

  if ((key_limit_l == NULL || key_limit_fetch_bigint (thread_p, key_limit_l, vd, lower))
      && (key_limit_u == NULL || key_limit_fetch_bigint (thread_p, key_limit_u, vd, upper)))
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
