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
 * fetch_arith_ops.c - the operators of an arithmetic node: one function per opcode, the bodies of the former
 *   fetch_peek_arith () compute switch. An operator reads the operands the node's fetch rule fetched (or fetches
 *   its own, a branch or a lazy operand) and writes arithptr->value.
 */

#ident "$Id$"

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>
#include <sys/time.h>
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

static int get_hour_minute_or_second (const DB_VALUE * datetime, OPERATOR_TYPE op_type, DB_VALUE * db_value);
static int get_year_month_or_day (const DB_VALUE * src_date, OPERATOR_TYPE op, DB_VALUE * result);
static int get_date_weekday (const DB_VALUE * src_date, OPERATOR_TYPE op, DB_VALUE * result);
/* An arithmetic node's row: the typed operator inline, the operand coercion out of line */
STATIC_INLINE int fetch_arith_binary (THREAD_ENTRY * thread_p, const val_descr * vd, ARITH_TYPE * arithptr,
				      DB_VALUE * left, DB_VALUE * right, TP_DOMAIN * domain)
  __attribute__ ((ALWAYS_INLINE));
static int fetch_arith_binary_operand_coercion (THREAD_ENTRY * thread_p, const val_descr * vd, ARITH_TYPE * arithptr,
						const RESOLVED_DOMAIN * plan, DB_VALUE * left, DB_VALUE * right,
						TP_DOMAIN * domain) __attribute__ ((noinline));

/*
 * fetch_session_read_value () - a session variable read's value in the type resolve_domains gave the variable for the
 *   statement
 *   return: NO_ERROR, or ER_QPROC_SESSION_VARIABLE_TYPE for a value of another type
 *
 * resolve_domains gave every variable the statement reads one type, from its value when the execution started and the
 * values the statement assigns it (qexec_resolve_session_variables), so a value of another type comes from a writer
 * resolve_domains does not see (a stored procedure the statement calls): the statement stops, as it stops at
 * resolve_domains for an assignment of another type. A value is of the variable's type when it has the type's DB type,
 * a string also its codeset and collation, whatever its length. A fixed-length string of a variable the statement
 * assigns converts into the variable-length string resolve_domains gave it, whose longest precision holds it. A read
 * outside an execution with resolved-domain state reads the value as it is.
 */
static int
fetch_session_read_value (const VAL_DESCR * vd, ARITH_TYPE * arithptr, const DB_VALUE * name)
{
  DB_VALUE *value = arithptr->value;
  const RESOLVED_DOMAIN *resolved_domain = qexec_late_bind_domain (vd, arithptr->plan_item);
  if (DB_IS_NULL (value) || resolved_domain == NULL)
    {
      return NO_ERROR;
    }
  const TP_DOMAIN *type = resolved_domain->domain;
  const DB_TYPE value_type = DB_VALUE_DOMAIN_TYPE (value);
  const bool same_collation = !TP_IS_CHAR_TYPE (value_type) || !TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (type))
    || (db_get_string_codeset (value) == TP_DOMAIN_CODESET (type)
	&& db_get_string_collation (value) == TP_DOMAIN_COLLATION (type));
  if (value_type == TP_DOMAIN_TYPE (type) && same_collation)
    {
      return NO_ERROR;
    }
  const TP_DOMAIN *value_domain = tp_domain_resolve_value (value, NULL);
  if (value_domain == NULL)
    {
      return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
    }
  if (value_type == DB_TYPE_CHAR && TP_DOMAIN_TYPE (type) == DB_TYPE_VARCHAR
      && type->precision == DB_MAX_VARCHAR_PRECISION && same_collation
      && tp_value_cast (value, value, type, false) == DOMAIN_COMPATIBLE)
    {
      return NO_ERROR;
    }
  return qexec_session_variable_type_error (name, type, value_domain);
}

/*
 * fetch_convert_to_branch_value () - a value a row picked among branches whose collations resolve_domains merged into
 *   one domain (domain_resolve_branch_merge), brought into it by the converter resolve_domains chose for its string
 *   type
 *   return: NO_ERROR, the conversion's error, or ER_QPROC_DOMAIN_UNRESOLVED (the unresolved-domain check (execution))
 *	     for a value of a type resolve_domains chose no converter for
 */
static int
fetch_convert_to_branch_value (const val_descr * vd, const DOMAIN_PLAN_ITEM * item,
			       const RESOLVED_DOMAIN * resolved_domain, DB_VALUE * value)
{
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);
  const TP_VALUE_CONVERTER converter = type == DB_TYPE_VARCHAR ? resolved_domain->conv[0]
    : type == DB_TYPE_CHAR ? resolved_domain->conv[1] : NULL;
  if (converter == NULL)
    {
      return domain_unresolved_error (qexec_query_alias (vd), qexec_item_index (vd, item), type);
    }
  DB_VALUE converted;
  const TP_DOMAIN_STATUS status = tp_value_convert (converter, resolved_domain->domain, value, &converted);
  if (status != DOMAIN_COMPATIBLE)
    {
      pr_clear_value (&converted);
      return tp_domain_status_er_set (status, ARG_FILE_LINE, value, resolved_domain->domain);
    }
  pr_clear_value (value);
  *value = converted;
  return NO_ERROR;
}

/* The row's test before fetch_convert_to_branch_value, inline: a collation late-binding node's resolution carries
 * converters only for merged branches (a type-dependent node's are its operands'), so any other node costs a flag
 * test. */
static inline int
fetch_convert_to_resolved_branch (const val_descr * vd, const DOMAIN_PLAN_ITEM * item, DB_VALUE * value)
{
  if (item == NULL || !(item->flags & DOMAIN_PLAN_LATE_BIND_COLLATION))
    {
      return NO_ERROR;
    }
  const RESOLVED_DOMAIN *resolved_domain = qexec_late_bind_domain (vd, item);
  if (resolved_domain == NULL || (resolved_domain->conv[0] == NULL && resolved_domain->conv[1] == NULL)
      || DB_IS_NULL (value))
    {
      return NO_ERROR;
    }
  return fetch_convert_to_branch_value (vd, item, resolved_domain, value);
}

/* the same test for a function result (fetch_peek_dbval_slow's TYPE_FUNC), out of line: that path's one caller
 * outside this file */
int
fetch_arith_convert_to_resolved_branch (const val_descr * vd, const DOMAIN_PLAN_ITEM * item, DB_VALUE * value)
{
  return fetch_convert_to_resolved_branch (vd, item, value);
}

/* The resolved comparison k an arithmetic node makes: its plan item carries them. A stream's
 * FIELD, NULLIF, LEAST or GREATEST has a bare item for them. */
static inline const DOMAIN_COMPARE_PLAN *
fetch_arith_compare (const ARITH_TYPE * arithptr, int k)
{
  const DOMAIN_PLAN_ITEM *item = arithptr->plan_item;
  if (item == NULL || !(item->flags & DOMAIN_PLAN_ITEM_COMPARES))
    {
      return NULL;
    }
  assert (arithptr->opcode == T_FIELD || arithptr->opcode == T_NULLIF || arithptr->opcode == T_LEAST
	  || arithptr->opcode == T_GREATEST);
  return item->compares[k];
}

/*
 * fetch_least_or_greatest () - LEAST or GREATEST of an arithmetic node's two operands, compared as the load or
 *				resolve_domains resolved
 *   return: NO_ERROR, or ER_FAILED where they do not compare
 */
static int
fetch_least_or_greatest (THREAD_ENTRY * thread_p, ARITH_TYPE * arithptr, val_descr * vd, DB_VALUE * peek_left,
			 DB_VALUE * peek_right, bool least)
{
  bool can_compare = true;
  const DB_VALUE_COMPARE_RESULT cmp_result =
    eval_compare_values_resolved (thread_p, fetch_arith_compare (arithptr, 0), vd, peek_left, peek_right, 0,
				  &can_compare);
  return db_least_or_greatest_by (peek_left, peek_right, cmp_result, can_compare, arithptr->value, least);
}

/*
 * fetch_arith_binary () - an addition, subtraction, multiplication or division: its operands' operand coercion,
 *   resolved before any row, then the typed operator, which casts nothing (qdata_coerce_arith_operands)
 *   return: NO_ERROR or ER_code
 *
 * The plan is the resolved domain for a node whose type resolve_domains resolves, the load's for any other node - a
 * compiled node, one whose collation alone resolve_domains resolves, a stream's node (domain_plan_operand_coercion). A
 * NULL operand converts nothing, resolved or not: an operator answers a NULL operand before any operand coercion.
 *
 * Inline at every call, with the operator called directly: the operand coercion's frame - its execution temporary
 * arrays, the stack protector they bring, the calls it makes - is fetch_arith_binary_operand_coercion's, which only a
 * plan that converts an operand, or a missing plan, reaches.
 */
static inline int
fetch_arith_binary (THREAD_ENTRY * thread_p, const val_descr * vd, ARITH_TYPE * arithptr, DB_VALUE * left,
		    DB_VALUE * right, TP_DOMAIN * domain)
{
  const DOMAIN_PLAN_ITEM *item = arithptr->plan_item;
  const RESOLVED_DOMAIN *plan = item != NULL ? &item->fixed : NULL;
  if (item != NULL && (item->flags & DOMAIN_PLAN_LATE_BIND) && !(item->flags & DOMAIN_PLAN_LATE_BIND_COLLATION))
    {
      plan = qexec_late_bind_domain (vd, item);
    }
  if (plan != NULL && plan->arith.kind == DOMAIN_ARITH_UNRESOLVED)
    {
      /* a plan nothing resolved - the load met a variable operand and resolve_domains gave no entry: the
       * unresolved-domain check (execution), as a missing plan */
      plan = NULL;
    }
  if (plan != NULL && plan->conv[0] == NULL && plan->conv[1] == NULL)
    {
      /* an operand coercion that converts neither operand calls the operator directly, which dispatches on the plan's
       * value (arith); its optdebug check still holds the plan to the rule over the values (qdata_assert_arith_planned)
       */
      return qdata_arith_dbval (arithptr->opcode, &plan->arith, left, right, arithptr->value, domain);
    }
  return fetch_arith_binary_operand_coercion (thread_p, vd, arithptr, plan, left, right, domain);
}

/* fetch_arith_binary's operand coercion: the operands the plan converts, a scope's execution temporary, or the
 * unresolved-domain check (execution) of a node without its plan (out of line) */
static int
fetch_arith_binary_operand_coercion (THREAD_ENTRY * thread_p, const val_descr * vd, ARITH_TYPE * arithptr,
				     const RESOLVED_DOMAIN * plan, DB_VALUE * left, DB_VALUE * right,
				     TP_DOMAIN * domain)
{
  const DOMAIN_PLAN_ITEM *item = arithptr->plan_item;
  if (plan == NULL && left != NULL && right != NULL && !DB_IS_NULL (left) && !DB_IS_NULL (right))
    {
      /* every node of a loaded tree or stream carries its operand coercion, and resolve_domains resolved every node
       * whose type it resolves before the main block: the unresolved-domain check (execution) */
      return qexec_domain_unresolved (vd, item, arithptr->domain);
    }
  assert (item == NULL || !(item->flags & DOMAIN_PLAN_ITEM_COMPARES));
  if (plan != NULL && (item->temporaries[0] >= 0 || item->temporaries[1] >= 0) && left != NULL && right != NULL
      && !DB_IS_NULL (left) && !DB_IS_NULL (right))
    {
      /* an operand a scope fixes - a constant for the execution, a correlated value for its
       * outer row - is converted once per scope */
      DB_VALUE *const operands[2] = { left, right };
      const DB_VALUE *temporaries[2] = { NULL, NULL };
      for (int i = 0; i < 2; i++)
	{
	  if (item->temporaries[i] >= 0 && plan->conv[i] != NULL)
	    {
	      temporaries[i] =
		qexec_execution_temporary (thread_p, vd, item->temporaries[i], plan->conv[i], plan->operand_domain[i],
					   operands[i]);
	    }
	}
      return qdata_coerce_arith_operands (arithptr->opcode, plan->conv, plan->operand_domain, &plan->arith, left,
					  right, arithptr->value, domain, temporaries);
    }
  /* without a plan an operand is NULL (the check above): the operator answers it before any dispatch */
  return qdata_coerce_arith_operands (arithptr->opcode, plan != NULL ? plan->conv : NULL,
				      plan != NULL ? plan->operand_domain : NULL, plan != NULL ? &plan->arith : NULL,
				      left, right, arithptr->value, domain);
}

/*
 * fetch_cast_operand () - the cast of operand i's value into the node's domain (a CAST; the operand a NVL,
 *   IFNULL, COALESCE or NVL2 row picks), with the converter the load found for the operand's compiled type
 *   (domain_fixed_operand): the cast skips its per-value lookup for a value of that type
 *   force(in): tp_value_cast_force's coercion, else tp_value_cast's
 */
static inline TP_DOMAIN_STATUS
fetch_cast_operand (const val_descr * vd, const ARITH_TYPE * arithptr, int i, const DB_VALUE * value,
		    DB_VALUE * result, const TP_DOMAIN * domain, bool force)
{
  const DOMAIN_PLAN_ITEM *item = arithptr->plan_item;
  const REGU_VARIABLE *operand = i == 0 ? arithptr->leftptr : i == 1 ? arithptr->rightptr : arithptr->thirdptr;
  if (item != NULL && item->fixed.conv[i] != NULL && item->fixed.operand_domain[i] == domain && operand != NULL
      && operand->domain != NULL)
    {
      return tp_value_cast_with_converter (value, result, domain, force, TP_DOMAIN_TYPE (operand->domain),
					   item->fixed.conv[i]);
    }
  if (item != NULL && (item->flags & DOMAIN_PLAN_LATE_BIND_COERCION) && value != NULL && !DB_IS_NULL (value))
    {
      /* an operand the compiler did not type: resolve_domains found the converter from its resolved type
       * (qexec_resolve_cast_coercion) */
      const RESOLVED_DOMAIN *resolved = qexec_late_bind_domain (vd, item);
      if (resolved != NULL && resolved->conv[i] != NULL && resolved->operand_domain[i] == domain)
	{
	  return tp_value_cast_with_converter (value, result, domain, force, DB_VALUE_DOMAIN_TYPE (value),
					       resolved->conv[i]);
	}
    }
  return force ? tp_value_cast_force (value, result, domain, false) : tp_value_cast (value, result, domain, false);
}

/*
 * get_hour_minute_or_second () - extract hour, minute or second
 *				  information from datetime depending on
 *				  the value of the op_type variable
 *   return: error or no error
 *   datetime(in): datetime value
 *   op_type(in): operation type
 *   db_value(out): output of the operation
 */
static int
get_hour_minute_or_second (const DB_VALUE * datetime, OPERATOR_TYPE op_type, DB_VALUE * db_value)
{
  int error = NO_ERROR;
  int hour, minute, second, millisecond;

  if (DB_IS_NULL (datetime))
    {
      PRIM_SET_NULL (db_value);
      return NO_ERROR;
    }

  if (DB_VALUE_DOMAIN_TYPE (datetime) == DB_TYPE_DATE)
    {
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
	{
	  db_make_null (db_value);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	  error = ER_TIME_CONVERSION;
	}
      return error;
    }

  error = db_get_time_from_dbvalue (datetime, &hour, &minute, &second, &millisecond);
  if (error != NO_ERROR)
    {
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
	{
	  db_make_null (db_value);
	  error = NO_ERROR;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	  error = ER_TIME_CONVERSION;
	}
      return error;
    }
  switch (op_type)
    {
    case T_HOUR:
      db_make_int (db_value, hour);
      break;
    case T_MINUTE:
      db_make_int (db_value, minute);
      break;
    case T_SECOND:
      db_make_int (db_value, second);
      break;
    default:
      assert (false);
      db_make_null (db_value);
      error = ER_FAILED;
      break;
    }

  return error;
}

/*
 * get_year_month_or_day () - get year or month or day from value
 *
 * return        : error code
 * src_date (in) : input value
 * op (in)       : operation type - year, month or day
 * result (out)  : value with year, month or day
 */
static int
get_year_month_or_day (const DB_VALUE * src_date, OPERATOR_TYPE op, DB_VALUE * result)
{
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;

  assert (op == T_YEAR || op == T_MONTH || op == T_DAY);

  if (DB_IS_NULL (src_date))
    {
      db_make_null (result);
      return NO_ERROR;
    }

  /* get the date/time information from src_date */
  if (db_get_datetime_from_dbvalue (src_date, &year, &month, &day, &hour, &minute, &second, &ms, NULL) != NO_ERROR)
    {
      /* This function should return NULL if src_date is an invalid parameter. Clear the error generated by the
       * function call and return null. */
      er_clear ();
      db_make_null (result);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      /* set ER_DATE_CONVERSION */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  switch (op)
    {
    case T_YEAR:
      db_make_int (result, year);
      break;
    case T_MONTH:
      db_make_int (result, month);
      break;
    case T_DAY:
      db_make_int (result, day);
      break;
    default:
      assert (false);
      db_make_null (result);
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
get_date_weekday (const DB_VALUE * src_date, OPERATOR_TYPE op, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;
  int day_of_week = 0;

  if (DB_IS_NULL (src_date))
    {
      db_make_null (result);
      return NO_ERROR;
    }

  /* get the date/time information from src_date */
  error_status = db_get_datetime_from_dbvalue (src_date, &year, &month, &day, &hour, &minute, &second, &ms, NULL);
  if (error_status != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      goto error_exit;
    }

  if (year == 0 && month == 0 && day == 0 && hour == 0 && minute == 0 && second == 0 && ms == 0)
    {
      error_status = ER_ATTEMPT_TO_USE_ZERODATE;
      goto error_exit;
    }

  /* 0 = Sunday, 1 = Monday, etc */
  day_of_week = db_get_day_of_week (year, month, day);

  switch (op)
    {
    case T_WEEKDAY:
      /* 0 = Monday, 1 = Tuesday, ..., 6 = Sunday */
      if (day_of_week == 0)
	{
	  day_of_week = 6;
	}
      else
	{
	  day_of_week--;
	}
      db_make_int (result, day_of_week);
      break;

    case T_DAYOFWEEK:
      /* 1 = Sunday, 2 = Monday, ..., 7 = Saturday */
      day_of_week++;
      db_make_int (result, day_of_week);
      break;

    default:
      assert (false);
      db_make_null (result);
      break;
    }

  return NO_ERROR;

error_exit:
  /* This function should return NULL if src_date is an invalid parameter or zero date. Clear the error generated by
   * the function call and return null. */
  er_clear ();
  db_make_null (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
  return error_status;
}

/* ---- the operators: the arms of the former compute switch, one function per arm group ---- */

/* T_ADD */
static int
fetch_op_add (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  bool check_empty_string;

  check_empty_string = (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) ? true : false);

  /* check for result type. */
  if (check_empty_string && domain != NULL && QSTR_IS_ANY_CHAR_OR_BIT (TP_DOMAIN_TYPE (domain)))
    {
      /* at here, T_ADD is really T_STRCAT */
      if (qdata_strcat_dbval (peek_left, peek_right, arithptr->value, domain) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      if (fetch_arith_binary (thread_p, vd, arithptr, peek_left, peek_right, domain) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}


/* T_BIT_NOT */
static int
fetch_op_bit_not (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_bit_not_dbval (peek_right, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_BIT_AND */
static int
fetch_op_bit_and (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_bit_and_dbval (peek_left, peek_right, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_BIT_OR */
static int
fetch_op_bit_or (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_bit_or_dbval (peek_left, peek_right, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_BIT_XOR */
static int
fetch_op_bit_xor (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_bit_xor_dbval (peek_left, peek_right, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_BITSHIFT_LEFT, T_BITSHIFT_RIGHT */
static int
fetch_op_bitshift_left (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_bit_shift_dbval (peek_left, peek_right, arithptr->opcode, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_INTDIV, T_INTMOD */
static int
fetch_op_intdiv (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_divmod_dbval (peek_left, peek_right, arithptr->opcode, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_BIT_COUNT */
static int
fetch_op_bit_count (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (db_bit_count_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SUB */
static int
fetch_op_sub (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (fetch_arith_binary (thread_p, vd, arithptr, peek_left, peek_right, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_MUL */
static int
fetch_op_mul (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (fetch_arith_binary (thread_p, vd, arithptr, peek_left, peek_right, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DIV */
static int
fetch_op_div (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (fetch_arith_binary (thread_p, vd, arithptr, peek_left, peek_right, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_UNPLUS */
static int
fetch_op_unplus (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  return NO_ERROR;
}


/* T_UNMINUS */
static int
fetch_op_unminus (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (qdata_unary_minus_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_PRIOR */
static int
fetch_op_prior (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (!qdata_copy_db_value (arithptr->value, peek_right))
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_CONNECT_BY_ROOT */
static int
fetch_op_connect_by_root (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  if (!qdata_evaluate_connect_by_root (thread_p, (void *) arithptr->thirdptr->xasl, arithptr->rightptr,
				       arithptr->value, vd))
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_QPRIOR */
static int
fetch_op_qprior (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  if (!qdata_evaluate_qprior (thread_p, (void *) arithptr->thirdptr->xasl, arithptr->rightptr, arithptr->value, vd))
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_MOD */
static int
fetch_op_mod (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (db_mod_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_FLOOR */
static int
fetch_op_floor (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_floor_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_CEIL */
static int
fetch_op_ceil (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_ceil_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SIGN */
static int
fetch_op_sign (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_sign_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ABS */
static int
fetch_op_abs (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_abs_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_EXP */
static int
fetch_op_exp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_exp_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SQRT */
static int
fetch_op_sqrt (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_sqrt_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SIN */
static int
fetch_op_sin (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_sin_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_COS */
static int
fetch_op_cos (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_cos_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TAN */
static int
fetch_op_tan (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_tan_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_COT */
static int
fetch_op_cot (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_cot_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LN */
static int
fetch_op_ln (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_log_generic_dbval (arithptr->value, peek_right, -1 /* convention for e base */ ) !=
	   NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LOG2 */
static int
fetch_op_log2 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_log_generic_dbval (arithptr->value, peek_right, 2) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LOG10 */
static int
fetch_op_log10 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_log_generic_dbval (arithptr->value, peek_right, 10) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ACOS */
static int
fetch_op_acos (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_acos_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ASIN */
static int
fetch_op_asin (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_asin_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DEGREES */
static int
fetch_op_degrees (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_degrees_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DATE */
static int
fetch_op_date (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_date_dbval (arithptr->value, peek_right, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TIME */
static int
fetch_op_time (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_time_dbval (arithptr->value, peek_right, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DEFAULT */
static int
fetch_op_default (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  qdata_copy_db_value (arithptr->value, peek_right);
  return NO_ERROR;
}


/* T_RADIANS */
static int
fetch_op_radians (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_radians_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_POWER */
static int
fetch_op_power (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (db_power_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ROUND */
static int
fetch_op_round (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (db_round_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LOG */
static int
fetch_op_log (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (db_log_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_INCR, T_DECR */
static int
fetch_op_incr (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      /* an instance does not exist to do increment */
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      /*
       * right argument is used at end of scan,
       * so keep it in the expr until that.
       */
      (void) pr_clone_value (peek_right, arithptr->value);
      return FETCH_ARITH_RAW_LEFT;
    }
  return NO_ERROR;
}


/* T_TRUNC */
static int
fetch_op_trunc (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (db_trunc_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_CHR */
static int
fetch_op_chr (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_chr (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_INSTR */
static int
fetch_op_instr (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_instr (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_POSITION */
static int
fetch_op_position (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_position (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_FINDINSET */
static int
fetch_op_findinset (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_find_string_in_in_set (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SUBSTRING */
static int
fetch_op_substring (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || (arithptr->thirdptr && DB_IS_NULL (peek_third)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      DB_VALUE tmp_len, tmp_arg2, tmp_arg3;
      int pos, len;

      pos = db_get_int (peek_right);
      if (pos < 0)
	{
	  if (QSTR_IS_BIT (TP_DOMAIN_TYPE (qexec_get_node_domain (vd, arithptr->leftptr->domain,
								  arithptr->leftptr->plan_item))))
	    {
	      if (db_string_bit_length (peek_left, &tmp_len) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	  else
	    {
	      if (db_string_char_length (peek_left, &tmp_len) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	  if (DB_IS_NULL (&tmp_len))
	    {
	      return ER_FAILED;
	    }
	  pos = pos + db_get_int (&tmp_len) + 1;
	}

      if (pos < 1)
	{
	  db_make_int (&tmp_arg2, 1);
	}
      else
	{
	  db_make_int (&tmp_arg2, pos);
	}

      if (arithptr->thirdptr)
	{
	  len = db_get_int (peek_third);
	  if (len < 1)
	    {
	      db_make_int (&tmp_arg3, 0);
	    }
	  else
	    {
	      db_make_int (&tmp_arg3, len);
	    }
	}
      else
	{
	  db_make_null (&tmp_arg3);
	}

      if (db_string_substring (arithptr->misc_operand, peek_left, &tmp_arg2, &tmp_arg3, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      if (db_string_substring (arithptr->misc_operand, peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_OCTET_LENGTH */
static int
fetch_op_octet_length (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (DB_VALUE_DOMAIN_TYPE (peek_right) == DB_TYPE_BLOB)
    {
      DB_VALUE tval;

      db_make_null (&tval);
      dom_status = tp_value_cast (peek_right, &tval, &tp_VarBit_domain, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, &tp_VarBit_domain);
	  db_value_clear (&tval);
	  return ER_FAILED;
	}

      db_make_int (arithptr->value, db_get_string_size (&tval));
      db_value_clear (&tval);
    }
  else if (!TP_IS_STRING_TYPE (DB_VALUE_TYPE (peek_right)))
    {
      DB_VALUE tval;

      db_make_null (&tval);
      dom_status = tp_value_cast (peek_right, &tval, &tp_Char_domain, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, &tp_Char_domain);
	  db_value_clear (&tval);
	  return ER_FAILED;
	}
      db_make_int (arithptr->value, db_get_string_size (&tval));
      db_value_clear (&tval);
    }
  else
    {
      db_make_int (arithptr->value, db_get_string_size (peek_right));
    }
  return NO_ERROR;
}


/* T_BIT_LENGTH */
static int
fetch_op_bit_length (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (DB_VALUE_DOMAIN_TYPE (peek_right) == DB_TYPE_BIT || DB_VALUE_DOMAIN_TYPE (peek_right) == DB_TYPE_VARBIT)
    {
      int len = 0;

      db_get_bit (peek_right, &len);
      db_make_int (arithptr->value, len);
    }
  else if (DB_VALUE_DOMAIN_TYPE (peek_right) == DB_TYPE_BLOB)
    {
      DB_VALUE tval;
      int len = 0;

      db_make_null (&tval);
      dom_status = tp_value_cast (peek_right, &tval, &tp_VarBit_domain, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, &tp_VarBit_domain);
	  db_value_clear (&tval);
	  return ER_FAILED;
	}

      db_get_bit (&tval, &len);
      db_make_int (arithptr->value, len);
      db_value_clear (&tval);
    }
  else if (!TP_IS_CHAR_TYPE (DB_VALUE_TYPE (peek_right)))
    {
      DB_VALUE tval;

      db_make_null (&tval);
      dom_status = tp_value_cast (peek_right, &tval, &tp_Char_domain, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, &tp_Char_domain);
	  db_value_clear (&tval);
	  return ER_FAILED;
	}
      db_make_int (arithptr->value, 8 * db_get_string_size (&tval));
      db_value_clear (&tval);
    }
  else
    {
      /* must be a char string type */
      db_make_int (arithptr->value, 8 * db_get_string_size (peek_right));
    }
  return NO_ERROR;
}


/* T_CHAR_LENGTH */
static int
fetch_op_char_length (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      db_make_int (arithptr->value, db_get_string_length (peek_right));
    }
  return NO_ERROR;
}


/* T_LOWER */
static int
fetch_op_lower (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_lower (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_UPPER */
static int
fetch_op_upper (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_upper (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_HEX */
static int
fetch_op_hex (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_hex (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ASCII */
static int
fetch_op_ascii (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_ascii (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_CONV */
static int
fetch_op_conv (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_conv (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_BIN */
static int
fetch_op_bin (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_bigint_to_binary_string (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_MD5 */
static int
fetch_op_md5 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_md5 (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_UUID_FORMAT */
static int
fetch_op_uuid_format (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_uuid_format (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SHA_ONE */
static int
fetch_op_sha_one (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_sha_one (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_AES_ENCRYPT */
static int
fetch_op_aes_encrypt (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_aes_encrypt (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_AES_DECRYPT */
static int
fetch_op_aes_decrypt (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_aes_decrypt (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SHA_TWO */
static int
fetch_op_sha_two (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_sha_two (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TO_BASE64 */
static int
fetch_op_to_base64 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_to_base64 (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_FROM_BASE64 */
static int
fetch_op_from_base64 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_from_base64 (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SPACE */
static int
fetch_op_space (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_space (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TRIM */
static int
fetch_op_trim (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left) && peek_right
      && !DB_IS_NULL (peek_right))
    {
      TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_right, peek_right,
								  db_get_string_collation (peek_left),
								  db_get_string_codeset (peek_left));

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_right)),
		  pr_type_name (DB_VALUE_TYPE (peek_left)));
	  return ER_FAILED;
	}
    }
  if (DB_IS_NULL (peek_left) || (peek_right && DB_IS_NULL (peek_right)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_trim (arithptr->misc_operand, peek_right, peek_left, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LTRIM */
static int
fetch_op_ltrim (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left) && peek_right
      && !DB_IS_NULL (peek_right))
    {
      TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_right, peek_right,
								  db_get_string_collation (peek_left),
								  db_get_string_codeset (peek_left));

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_right)),
		  pr_type_name (DB_VALUE_TYPE (peek_left)));
	  return ER_FAILED;
	}
    }
  if (DB_IS_NULL (peek_left) || (peek_right && DB_IS_NULL (peek_right)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_trim (LEADING, peek_right, peek_left, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_RTRIM */
static int
fetch_op_rtrim (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left) && peek_right
      && !DB_IS_NULL (peek_right))
    {
      TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_right, peek_right,
								  db_get_string_collation (peek_left),
								  db_get_string_codeset (peek_left));

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_right)),
		  pr_type_name (DB_VALUE_TYPE (peek_left)));
	  return ER_FAILED;
	}
    }
  if (DB_IS_NULL (peek_left) || (peek_right && DB_IS_NULL (peek_right)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_trim (TRAILING, peek_right, peek_left, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_FROM_UNIXTIME */
static int
fetch_op_from_unixtime (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_from_unixtime (peek_left, peek_right, peek_third, arithptr->value, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LPAD */
static int
fetch_op_lpad (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left) && peek_third
      && !DB_IS_NULL (peek_third))
    {
      TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_third, peek_third,
								  db_get_string_collation (peek_left),
								  db_get_string_codeset (peek_left));

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_third)),
		  pr_type_name (DB_VALUE_TYPE (peek_left)));
	  return ER_FAILED;
	}
    }
  if (DB_IS_NULL (peek_left) || (peek_third && DB_IS_NULL (peek_third)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_pad (LEADING, peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_RPAD */
static int
fetch_op_rpad (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left) && peek_third
      && !DB_IS_NULL (peek_third))
    {
      TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_third, peek_third,
								  db_get_string_collation (peek_left),
								  db_get_string_codeset (peek_left));

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_third)),
		  pr_type_name (DB_VALUE_TYPE (peek_left)));
	  return ER_FAILED;
	}
    }
  if (DB_IS_NULL (peek_left) || (peek_third && DB_IS_NULL (peek_third)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_pad (TRAILING, peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_REPLACE */
static int
fetch_op_replace (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left) && peek_third
      && !DB_IS_NULL (peek_third))
    {
      TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_third, peek_third,
								  db_get_string_collation (peek_left),
								  db_get_string_codeset (peek_left));

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_third)),
		  pr_type_name (DB_VALUE_TYPE (peek_left)));
	  return ER_FAILED;
	}
    }
  if (!prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING)
      && (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || (peek_third && DB_IS_NULL (peek_third))))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_replace (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TRANSLATE */
static int
fetch_op_translate (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left))
    {
      TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_third, peek_third,
								  db_get_string_collation (peek_left),
								  db_get_string_codeset (peek_left));

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_third)),
		  pr_type_name (DB_VALUE_TYPE (peek_left)));
	  return ER_FAILED;
	}
    }
  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_string_translate (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ADD_MONTHS */
static int
fetch_op_add_months (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_add_months (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LAST_DAY */
static int
fetch_op_last_day (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_last_day (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TIME_FORMAT */
static int
fetch_op_time_format (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_time_format (peek_left, peek_right, peek_third, arithptr->value, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_YEAR, T_MONTH, T_DAY */
static int
fetch_op_year (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (get_year_month_or_day (peek_right, arithptr->opcode, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_HOUR, T_MINUTE, T_SECOND */
static int
fetch_op_hour (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  /* T_HOUR, T_MINUTE and T_SECOND must be kept consecutive in OPERATOR_TYPE enum */
  if (get_hour_minute_or_second (peek_right, arithptr->opcode, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_QUARTER */
static int
fetch_op_quarter (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_get_date_quarter (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_WEEKDAY, T_DAYOFWEEK */
static int
fetch_op_weekday (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (get_date_weekday (peek_right, arithptr->opcode, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DAYOFYEAR */
static int
fetch_op_dayofyear (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_get_date_dayofyear (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TODAYS */
static int
fetch_op_todays (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_get_date_totaldays (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_FROMDAYS */
static int
fetch_op_fromdays (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_get_date_from_days (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TIMETOSEC */
static int
fetch_op_timetosec (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_convert_time_to_sec (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SECTOTIME */
static int
fetch_op_sectotime (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_convert_sec_to_time (peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TIMESTAMP */
static int
fetch_op_timestamp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || (peek_right != NULL && DB_IS_NULL (peek_right)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_timestamp (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_LIKE_LOWER_BOUND, T_LIKE_UPPER_BOUND */
static int
fetch_op_like_lower_bound (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      const bool compute_lower_bound = (arithptr->opcode == T_LIKE_LOWER_BOUND);

      if (db_like_bound (peek_left, peek_right, arithptr->value, compute_lower_bound) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_MAKEDATE */
static int
fetch_op_makedate (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_add_days_to_year (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_ADDTIME */
static int
fetch_op_addtime (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_add_time (peek_left, peek_right, arithptr->value, domain) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_MAKETIME */
static int
fetch_op_maketime (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_convert_to_time (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_WEEK */
static int
fetch_op_week (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_get_date_week (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_UNIX_TIMESTAMP */
static int
fetch_op_unix_timestamp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (arithptr->rightptr)
    {
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_unix_timestamp (peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      DB_TIMESTAMP db_timestamp;
      DB_DATETIME sys_datetime;
      DB_TIME db_time;
      time_t sec;
      int millisec;
      struct tm *c_time_struct, tm_val;

      /* get the local time of the system */
      util_get_second_and_ms_since_epoch (&sec, &millisec);
      c_time_struct = localtime_r (&sec, &tm_val);

      if (c_time_struct != NULL)
	{
	  db_datetime_encode (&sys_datetime, c_time_struct->tm_mon + 1, c_time_struct->tm_mday,
			      c_time_struct->tm_year + 1900, c_time_struct->tm_hour, c_time_struct->tm_min,
			      c_time_struct->tm_sec, millisec);
	}

      db_time = sys_datetime.time / 1000;

      /* convert to timestamp (this takes into account leap second) */
      db_timestamp_encode_sys (&sys_datetime.date, &db_time, &db_timestamp, NULL);

      db_make_int (arithptr->value, db_timestamp);
    }
  return NO_ERROR;
}


/* T_MONTHS_BETWEEN */
static int
fetch_op_months_between (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_months_between (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ATAN2 */
static int
fetch_op_atan2 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_atan2_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ATAN */
static int
fetch_op_atan (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_atan_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_FORMAT */
static int
fetch_op_format (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_format (peek_left, peek_right, peek_third, arithptr->value, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DATE_FORMAT */
static int
fetch_op_date_format (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_date_format (peek_left, peek_right, peek_third, arithptr->value, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_STR_TO_DATE */
static int
fetch_op_str_to_date (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *domain = a->domain;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_str_to_date (peek_left, peek_right, peek_third, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ADDDATE */
static int
fetch_op_adddate (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_date_add_interval_days (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DATE_ADD */
static int
fetch_op_date_add (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      int unit = db_get_int (peek_third);
      if (db_date_add_interval_expr (arithptr->value, peek_left, peek_right, unit) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_SUBDATE */
static int
fetch_op_subdate (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_date_sub_interval_days (arithptr->value, peek_left, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DATEDIFF */
static int
fetch_op_datediff (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_date_diff (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TIMEDIFF */
static int
fetch_op_timediff (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_time_diff (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DATE_SUB */
static int
fetch_op_date_sub (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      int unit = db_get_int (peek_third);

      if (db_date_sub_interval_expr (arithptr->value, peek_left, peek_right, unit) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_SYS_DATE */
static int
fetch_op_sys_date (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  db_value_put_encoded_date (arithptr->value, &vd->sys_datetime.date);
  return NO_ERROR;
}


/* T_SYS_TIME */
static int
fetch_op_sys_time (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  DB_TIME db_time;

  db_time = vd->sys_datetime.time / 1000;
  db_value_put_encoded_time (arithptr->value, &db_time);
  return NO_ERROR;
}


/* T_SYS_DATETIME */
static int
fetch_op_sys_datetime (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  db_make_datetime (arithptr->value, &vd->sys_datetime);

  return NO_ERROR;
}


/* T_SYS_TIMESTAMP */
static int
fetch_op_sys_timestamp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  DB_TIMESTAMP db_timestamp;
  DB_TIME db_time;

  db_time = vd->sys_datetime.time / 1000;

  db_timestamp_encode_ses (&vd->sys_datetime.date, &db_time, &db_timestamp, NULL);
  db_make_timestamp (arithptr->value, db_timestamp);

  return NO_ERROR;
}


/* T_CURRENT_DATE */
static int
fetch_op_current_date (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  TZ_REGION system_tz_region, session_tz_region;
  DB_DATETIME dest_dt;
  int err_status = 0;

  tz_get_system_tz_region (&system_tz_region);
  tz_get_session_tz_region (&session_tz_region);
  dest_dt = vd->sys_datetime;
  err_status =
    tz_conv_tz_datetime_w_region (&vd->sys_datetime, &system_tz_region, &session_tz_region, &dest_dt, NULL, NULL);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }
  db_value_put_encoded_date (arithptr->value, &dest_dt.date);
  return NO_ERROR;
}


/* T_CURRENT_TIME */
static int
fetch_op_current_time (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  DB_TIME cur_time, db_time;
  const char *t_source, *t_dest;
  int err_status = 0, len_source, len_dest;

  t_source = tz_get_system_timezone ();
  t_dest = tz_get_session_local_timezone ();
  len_source = (int) strlen (t_source);
  len_dest = (int) strlen (t_dest);
  db_time = vd->sys_datetime.time / 1000;

  err_status = tz_conv_tz_time_w_zone_name (&db_time, t_source, len_source, t_dest, len_dest, &cur_time);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }
  db_value_put_encoded_time (arithptr->value, &cur_time);
  return NO_ERROR;
}


/* T_CURRENT_TIMESTAMP */
static int
fetch_op_current_timestamp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  DB_TIMESTAMP db_timestamp;
  DB_TIME db_time;

  db_time = vd->sys_datetime.time / 1000;

  db_timestamp_encode_sys (&vd->sys_datetime.date, &db_time, &db_timestamp, NULL);

  db_make_timestamp (arithptr->value, db_timestamp);

  return NO_ERROR;
}


/* T_CURRENT_DATETIME */
static int
fetch_op_current_datetime (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  TZ_REGION system_tz_region, session_tz_region;
  DB_DATETIME dest_dt;
  int err_status = 0;

  tz_get_system_tz_region (&system_tz_region);
  tz_get_session_tz_region (&session_tz_region);
  err_status =
    tz_conv_tz_datetime_w_region (&vd->sys_datetime, &system_tz_region, &session_tz_region, &dest_dt, NULL, NULL);
  if (err_status != NO_ERROR)
    {
      return err_status;
    }
  db_make_datetime (arithptr->value, &dest_dt);

  return NO_ERROR;
}


/* T_UTC_TIME */
static int
fetch_op_utc_time (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  DB_TIME db_time;
  db_time = (DB_TIME) (vd->sys_epochtime % SECONDS_OF_ONE_DAY);
  db_value_put_encoded_time (arithptr->value, &db_time);
  return NO_ERROR;
}


/* T_UTC_DATE */
static int
fetch_op_utc_date (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  DB_DATE date;
  int year, month, day, hour, minute, second;

  tz_timestamp_decode_no_leap_sec (vd->sys_epochtime, &year, &month, &day, &hour, &minute, &second);
  date = julian_encode (month + 1, day, year);
  db_value_put_encoded_date (arithptr->value, &date);
  return NO_ERROR;
}


/* T_LOCAL_TRANSACTION_ID */
static int
fetch_op_local_transaction_id (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  db_make_int (arithptr->value, logtb_find_current_tranid (thread_p));
  return NO_ERROR;
}


/* T_TO_CHAR */
static int
fetch_op_to_char (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_to_char (peek_left, peek_right, peek_third, arithptr->value, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_BLOB_TO_BIT */
static int
fetch_op_blob_to_bit (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_blob_to_bit (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_CLOB_TO_CHAR */
static int
fetch_op_clob_to_char (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_clob_to_char (peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_BIT_TO_BLOB */
static int
fetch_op_bit_to_blob (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_BIT || DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_VARBIT)
    {
      if (db_bit_to_blob (peek_left, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else				/* (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_CHAR || DB_TYPE_VARCHAR) */
    {
      if (db_char_to_blob (peek_left, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}


/* T_CHAR_TO_CLOB */
static int
fetch_op_char_to_clob (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_char_to_clob (peek_left, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_LOB_LENGTH */
static int
fetch_op_lob_length (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_BLOB)
    {

      if (db_blob_length (peek_left, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else				/* (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_BLOB) */
    {
      if (db_clob_length (peek_left, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}


/* T_TO_DATE */
static int
fetch_op_to_date (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (!TP_IS_CHAR_TYPE (DB_VALUE_TYPE (peek_left)))
    {
      DB_VALUE tval;

      db_make_null (&tval);
      dom_status = tp_value_cast (peek_left, &tval, &tp_Char_domain, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, arith_domain);
	  return ER_FAILED;
	}
      if (db_to_date (&tval, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  db_value_clear (&tval);
	  return ER_FAILED;
	}
      db_value_clear (&tval);
    }
  else if (db_to_date (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TO_TIME */
static int
fetch_op_to_time (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (!TP_IS_CHAR_TYPE (DB_VALUE_TYPE (peek_left)))
    {
      DB_VALUE tval;

      db_make_null (&tval);
      dom_status = tp_value_cast (peek_left, &tval, &tp_Char_domain, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, arith_domain);
	  return ER_FAILED;
	}
      if (db_to_time (&tval, peek_right, peek_third, DB_TYPE_TIME, arithptr->value) != NO_ERROR)
	{
	  db_value_clear (&tval);
	  return ER_FAILED;
	}
      db_value_clear (&tval);
    }
  else if (db_to_time (peek_left, peek_right, peek_third, DB_TYPE_TIME, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TO_TIMESTAMP, T_TO_TIMESTAMP_TZ */
static int
fetch_op_to_timestamp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      DB_TYPE db_type = (arithptr->opcode == T_TO_TIMESTAMP) ? DB_TYPE_TIMESTAMP : DB_TYPE_TIMESTAMPTZ;
      if (!TP_IS_CHAR_TYPE (DB_VALUE_TYPE (peek_left)))
	{
	  DB_VALUE tval;

	  db_make_null (&tval);
	  dom_status = tp_value_cast (peek_left, &tval, &tp_Char_domain, false);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, arith_domain);
	      return ER_FAILED;
	    }
	  if (db_to_timestamp (&tval, peek_right, peek_third, db_type, arithptr->value) != NO_ERROR)
	    {
	      db_value_clear (&tval);
	      return ER_FAILED;
	    }
	  db_value_clear (&tval);
	}
      else if (db_to_timestamp (peek_left, peek_right, peek_third, db_type, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_TO_DATETIME, T_TO_DATETIME_TZ */
static int
fetch_op_to_datetime (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *arith_domain = a->arith_domain;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      DB_TYPE db_type = (arithptr->opcode == T_TO_DATETIME) ? DB_TYPE_DATETIME : DB_TYPE_DATETIMETZ;
      if (!TP_IS_CHAR_TYPE (DB_VALUE_TYPE (peek_left)))
	{
	  DB_VALUE tval;

	  db_make_null (&tval);
	  dom_status = tp_value_cast (peek_left, &tval, &tp_Char_domain, false);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, arith_domain);
	      return ER_FAILED;
	    }
	  if (db_to_datetime (&tval, peek_right, peek_third, db_type, arithptr->value) != NO_ERROR)
	    {
	      db_value_clear (&tval);
	      return ER_FAILED;
	    }
	  db_value_clear (&tval);
	}
      else if (db_to_datetime (peek_left, peek_right, peek_third, db_type, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_TO_NUMBER */
static int
fetch_op_to_number (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *domain = a->domain;

  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      /* the value carries the precision and scale its format gives: the node's domain, a cached one the plan
       * shares, stays as compiled */
      if (db_to_number (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_CURRENT_VALUE */
static int
fetch_op_current_value (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      OID *serial_oid;
      int cached_num;

      serial_oid = db_get_oid (peek_left);
      cached_num = db_get_int (peek_right);

      if (xserial_get_current_value (thread_p, arithptr->value, serial_oid, cached_num) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_NEXT_VALUE */
static int
fetch_op_next_value (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      OID *serial_oid;
      int cached_num;
      int num_alloc;

      serial_oid = db_get_oid (peek_left);
      cached_num = db_get_int (peek_right);
      num_alloc = db_get_int (peek_third);

      if (xserial_get_next_value (thread_p, arithptr->value, serial_oid, cached_num, num_alloc, GENERATE_SERIAL,
				  false) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_CAST, T_CAST_WRAP */
static int
fetch_op_cast (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *arith_domain = a->arith_domain;
  TP_DOMAIN_STATUS dom_status;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION))
    {
      pr_clone_value (peek_right, arithptr->value);

      if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (domain)))
	{
	  if (!DB_IS_NULL (arithptr->value))
	    {
	      assert (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (arithptr->value)));
	      assert (domain->codeset == db_get_string_codeset (arithptr->value));
	    }
	  db_string_put_cs_and_collation (arithptr->value, domain->codeset, domain->collation_id);
	}
      else
	{
	  assert (TP_DOMAIN_TYPE (domain) == DB_TYPE_ENUMERATION);

	  if (!DB_IS_NULL (arithptr->value))
	    {
	      assert (DB_VALUE_DOMAIN_TYPE (arithptr->value) == DB_TYPE_ENUMERATION);
	      assert (domain->codeset == db_get_enum_codeset (arithptr->value));
	    }
	  db_enum_put_cs_and_collation (arithptr->value, domain->codeset, domain->collation_id);

	}
    }
  else
    {
      dom_status = fetch_cast_operand (vd, arithptr, 1, peek_right, arithptr->value, arith_domain,
				       !REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_STRICT_TYPE_CAST)
				       || arithptr->opcode != T_CAST_WRAP);

      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, arith_domain);
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_CAST_NOFAIL */
static int
fetch_op_cast_nofail (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION))
    {
      pr_clone_value (peek_right, arithptr->value);

      if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (domain)))
	{
	  if (!DB_IS_NULL (arithptr->value))
	    {
	      assert (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (arithptr->value)));
	      assert (domain->codeset == db_get_string_codeset (arithptr->value));
	    }
	  db_string_put_cs_and_collation (arithptr->value, domain->codeset, domain->collation_id);
	}
      else
	{
	  assert (TP_DOMAIN_TYPE (domain) == DB_TYPE_ENUMERATION);

	  if (!DB_IS_NULL (arithptr->value))
	    {
	      assert (DB_VALUE_DOMAIN_TYPE (arithptr->value) == DB_TYPE_ENUMERATION);
	      assert (domain->codeset == db_get_enum_codeset (arithptr->value));
	    }
	  db_enum_put_cs_and_collation (arithptr->value, domain->codeset, domain->collation_id);

	}
    }
  else
    {
      /* the planned converter, as T_CAST takes it (fetch_cast_operand); a cast that fails leaves NULL */
      if (fetch_cast_operand (vd, arithptr, 1, peek_right, arithptr->value, arith_domain, false) != DOMAIN_COMPATIBLE)
	{
	  PRIM_SET_NULL (arithptr->value);
	}
    }
  return NO_ERROR;
}


/* T_CASE, T_DECODE, T_IF */
static int
fetch_op_case (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  OID *obj_oid = a->obj_oid;
  QFILE_TUPLE_RECORD *tplrec = a->tplrec;
  DB_VALUE *peek_left = a->peek_left;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN_STATUS dom_status;

  /* fetch values */
  switch (eval_pred (thread_p, arithptr->pred, vd, obj_oid))
    {
    case V_FALSE:
    case V_UNKNOWN:		/* unknown pred result, including cases of NULL pred operands */
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      break;

    case V_TRUE:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      break;

    default:
      return ER_FAILED;
    }

  dom_status = tp_value_auto_cast (peek_left, arithptr->value, domain);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, domain);
      return ER_FAILED;
    }
  /* branches whose collations resolve_domains merged: the picked value takes the merged domain */
  if (fetch_convert_to_resolved_branch (vd, arithptr->plan_item, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_PREDICATE */
static int
fetch_op_predicate (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  OID *obj_oid = a->obj_oid;
  DB_VALUE *peek_left = a->peek_left;
  TP_DOMAIN *domain = a->domain;
  DB_VALUE tmp_value;
  TP_DOMAIN_STATUS dom_status;

  /* return 0,1 or NULL accordingly */
  peek_left = &tmp_value;

  switch (eval_pred (thread_p, arithptr->pred, vd, obj_oid))
    {
    case V_UNKNOWN:
      db_make_null (peek_left);
      break;
    case V_FALSE:
      db_make_int (peek_left, 0);
      break;
    case V_TRUE:
      db_make_int (peek_left, 1);
      break;
    default:
      return ER_FAILED;
    }

  dom_status = tp_value_auto_cast (peek_left, arithptr->value, domain);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, domain);
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_NVL, T_IFNULL, T_COALESCE */
static int
fetch_op_nvl (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  OID *obj_oid = a->obj_oid;
  QFILE_TUPLE_RECORD *tplrec = a->tplrec;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *no_value_domain = a->no_value_domain;
  TP_DOMAIN_STATUS dom_status;

  DB_VALUE *src;
  TP_DOMAIN *target_domain;

  target_domain = domain;

  if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (DB_IS_NULL (peek_left) || target_domain == NULL)
    {
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (target_domain == NULL && no_value_domain != NULL && DB_IS_NULL (peek_left) && DB_IS_NULL (peek_right))
    {
      /* resolve_domains found no value: every argument is NULL */
      PRIM_SET_NULL (arithptr->value);
      return NO_ERROR;
    }
  if (target_domain == NULL)
    {
      /* no row infers a common domain: resolve_domains resolved the node, or found it takes no value, which only
       * NULL arguments give */
      (void) qexec_domain_unresolved (vd, arithptr->plan_item, regu_var->domain);
      return ER_FAILED;
    }

  src = DB_IS_NULL (peek_left) ? peek_right : peek_left;
  dom_status = fetch_cast_operand (vd, arithptr, DB_IS_NULL (peek_left) ? 1 : 0, src, arithptr->value,
				   target_domain, false);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, target_domain);
      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_NVL2 */
static int
fetch_op_nvl2 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  OID *obj_oid = a->obj_oid;
  QFILE_TUPLE_RECORD *tplrec = a->tplrec;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *no_value_domain = a->no_value_domain;
  TP_DOMAIN_STATUS dom_status;

  DB_VALUE *src;
  TP_DOMAIN *target_domain;

  target_domain = domain;

  if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tplrec, &peek_left) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (target_domain == NULL)
    {
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (no_value_domain != NULL && DB_IS_NULL (peek_right) && DB_IS_NULL (peek_third))
	{
	  /* resolve_domains found no value: both results are NULL */
	  PRIM_SET_NULL (arithptr->value);
	  return NO_ERROR;
	}
      /* no row infers a common domain: resolve_domains resolved the node, or found it takes no value, which only
       * NULL arguments give */
      (void) qexec_domain_unresolved (vd, arithptr->plan_item, regu_var->domain);
      return ER_FAILED;
    }

  if (DB_IS_NULL (peek_left))
    {
      if (peek_third == NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tplrec, &peek_third) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      src = peek_third;
    }
  else
    {
      if (peek_right == NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tplrec, &peek_right) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      src = peek_right;
    }

  dom_status = fetch_cast_operand (vd, arithptr, DB_IS_NULL (peek_left) ? 2 : 1, src, arithptr->value,
				   target_domain, false);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, target_domain);

      return ER_FAILED;
    }

  return NO_ERROR;
}


/* T_ISNULL */
static int
fetch_op_isnull (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      db_make_int (arithptr->value, 1);
    }
  else
    {
      db_make_int (arithptr->value, 0);
    }
  return NO_ERROR;
}


/* T_CONCAT */
static int
fetch_op_concat (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN_STATUS dom_status;

  if (arithptr->rightptr != NULL)
    {
      if (qdata_strcat_dbval (peek_left, peek_right, arithptr->value, domain) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      dom_status = tp_value_auto_cast (peek_left, arithptr->value, domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, domain);
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_CONCAT_WS */
static int
fetch_op_concat_ws (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
      return NO_ERROR;
    }
  if (arithptr->rightptr != NULL)
    {
      if (DB_IS_NULL (peek_left) && DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (DB_IS_NULL (peek_left))
	{
	  dom_status = tp_value_auto_cast (peek_right, arithptr->value, domain);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, domain);
	      return ER_FAILED;
	    }
	}
      else if (DB_IS_NULL (peek_right))
	{
	  dom_status = tp_value_auto_cast (peek_left, arithptr->value, domain);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, domain);
	      return ER_FAILED;
	    }
	}
      else
	{
	  DB_VALUE tmp_val;

	  db_make_null (&tmp_val);
	  if (qdata_strcat_dbval (peek_left, peek_third, &tmp_val, domain) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  if (qdata_strcat_dbval (&tmp_val, peek_right, arithptr->value, domain) != NO_ERROR)
	    {
	      (void) pr_clear_value (&tmp_val);
	      return ER_FAILED;
	    }
	  (void) pr_clear_value (&tmp_val);
	}
    }
  else
    {
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  dom_status = tp_value_auto_cast (peek_left, arithptr->value, domain);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, domain);
	      return ER_FAILED;
	    }
	}
    }
  return NO_ERROR;
}


/* T_FIELD */
static int
fetch_op_field (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_third))
    {
      db_make_int (arithptr->value, 0);
      return NO_ERROR;
    }
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FIELD_COMPARE))
    {
      bool can_compare = false;
      int cmp_res = DB_UNK;

      /* the comparisons the load or resolve_domains resolved */
      cmp_res =
	eval_compare_values_resolved (thread_p, fetch_arith_compare (arithptr, 0), vd, peek_third, peek_left, 0,
				      &can_compare);
      if (cmp_res == DB_EQ)
	{
	  db_make_int (arithptr->value, 1);
	  return NO_ERROR;
	}
      else if (cmp_res == DB_UNK && can_compare == false)
	{
	  return ER_FAILED;
	}


      cmp_res =
	eval_compare_values_resolved (thread_p, fetch_arith_compare (arithptr, 1), vd, peek_third, peek_right, 0,
				      &can_compare);
      if (cmp_res == DB_EQ)
	{
	  db_make_int (arithptr->value, 2);
	  return NO_ERROR;
	}
      else if (cmp_res == DB_UNK && can_compare == false)
	{
	  return ER_FAILED;
	}

      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FIELD_NESTED))
	{
	  /* we have a T_FIELD parent, return level */
	  db_make_int (arithptr->value, -3);
	}
      else
	{
	  /* no parent and no match */
	  db_make_int (arithptr->value, 0);
	}
    }
  else
    {
      int i = db_get_int (peek_left);
      if (i > 0)
	{
	  db_make_int (arithptr->value, i);
	}
      else
	{
	  bool can_compare = false;
	  int cmp_res = DB_UNK;

	  cmp_res =
	    eval_compare_values_resolved (thread_p, fetch_arith_compare (arithptr, 1), vd, peek_third, peek_right,
					  0, &can_compare);
	  if (cmp_res == DB_EQ)
	    {
	      /* match */
	      db_make_int (arithptr->value, -i);
	    }
	  else if (cmp_res == DB_UNK && can_compare == false)
	    {
	      return ER_FAILED;
	    }
	  else
	    {
	      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FIELD_NESTED))
		{
		  /* we have a T_FIELD parent, return level */
		  db_make_int (arithptr->value, i - 1);
		}
	      else
		{
		  /* no parent and no match */
		  db_make_int (arithptr->value, 0);
		}
	    }
	}
    }
  return NO_ERROR;
}


/* T_REPEAT */
static int
fetch_op_repeat (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_string_repeat (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_LEFT */
static int
fetch_op_left (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      DB_VALUE tmp_val, tmp_val2;

      dom_status = tp_value_auto_cast (peek_right, &tmp_val2, &tp_Integer_domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, &tp_Integer_domain);
	  return ER_FAILED;
	}

      db_make_int (&tmp_val, 0);
      if (db_string_substring (SUBSTRING, peek_left, &tmp_val, &tmp_val2, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_RIGHT */
static int
fetch_op_right (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN_STATUS dom_status;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      DB_VALUE tmp_val, tmp_val2;

      if (QSTR_IS_BIT (TP_DOMAIN_TYPE (qexec_get_node_domain (vd, arithptr->leftptr->domain,
							      arithptr->leftptr->plan_item))))
	{
	  if (db_string_bit_length (peek_left, &tmp_val) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      else
	{
	  if (db_string_char_length (peek_left, &tmp_val) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      if (DB_IS_NULL (&tmp_val))
	{
	  PRIM_SET_NULL (arithptr->value);
	  return NO_ERROR;
	}

      dom_status = tp_value_auto_cast (peek_right, &tmp_val2, &tp_Integer_domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, &tp_Integer_domain);
	  return ER_FAILED;
	}
      /* If len, defined as second argument, is negative value, RIGHT function returns the entire string. It's same
       * behavior with LEFT and SUBSTRING. */
      if (db_get_int (&tmp_val2) < 0)
	{
	  db_make_int (&tmp_val, 0);
	}
      else
	{
	  db_make_int (&tmp_val, db_get_int (&tmp_val) - db_get_int (&tmp_val2) + 1);
	}
      if (db_string_substring (SUBSTRING, peek_left, &tmp_val, &tmp_val2, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_LOCATE */
static int
fetch_op_locate (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || (arithptr->thirdptr && DB_IS_NULL (peek_third)))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (!arithptr->thirdptr)
	{
	  if (db_string_position (peek_left, peek_right, arithptr->value) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      else
	{
	  DB_VALUE tmp_len, tmp_val, tmp_arg3;
	  int tmp;

	  db_make_null (&tmp_val);

	  tmp = db_get_int (peek_third);
	  if (tmp < 1)
	    {
	      db_make_int (&tmp_arg3, 1);
	    }
	  else
	    {
	      db_make_int (&tmp_arg3, tmp);
	    }

	  if (db_string_char_length (peek_right, &tmp_len) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  if (DB_IS_NULL (&tmp_len))
	    {
	      return ER_FAILED;
	    }

	  db_make_int (&tmp_len, db_get_int (&tmp_len) - db_get_int (&tmp_arg3) + 1);

	  if (db_string_substring (SUBSTRING, peek_right, &tmp_arg3, &tmp_len, &tmp_val) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  if (db_string_position (peek_left, &tmp_val, arithptr->value) != NO_ERROR)
	    {
	      (void) pr_clear_value (&tmp_val);
	      return ER_FAILED;
	    }
	  if (db_get_int (arithptr->value) > 0)
	    {
	      db_make_int (arithptr->value, db_get_int (arithptr->value) + db_get_int (&tmp_arg3) - 1);
	    }

	  (void) pr_clear_value (&tmp_val);
	}
    }
  return NO_ERROR;
}


/* T_SUBSTRING_INDEX */
static int
fetch_op_substring_index (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_string_substring_index (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_MID */
static int
fetch_op_mid (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      DB_VALUE tmp_len, tmp_arg2, tmp_arg3;
      int pos, len;

      pos = db_get_int (peek_right);
      len = db_get_int (peek_third);

      if (pos < 0)
	{
	  if (QSTR_IS_BIT (TP_DOMAIN_TYPE (qexec_get_node_domain (vd, arithptr->leftptr->domain,
								  arithptr->leftptr->plan_item))))
	    {
	      if (db_string_bit_length (peek_left, &tmp_len) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	  else
	    {
	      if (db_string_char_length (peek_left, &tmp_len) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	  if (DB_IS_NULL (&tmp_len))
	    {
	      return ER_FAILED;
	    }
	  pos = pos + db_get_int (&tmp_len) + 1;
	}

      if (pos < 1)
	{
	  db_make_int (&tmp_arg2, 1);
	}
      else
	{
	  db_make_int (&tmp_arg2, pos);
	}

      if (len < 1)
	{
	  db_make_int (&tmp_arg3, 0);
	}
      else
	{
	  db_make_int (&tmp_arg3, len);
	}

      if (db_string_substring (SUBSTRING, peek_left, &tmp_arg2, &tmp_arg3, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_STRCMP */
static int
fetch_op_strcmp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      int cmp;

      if (db_string_compare (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      cmp = db_get_int (arithptr->value);
      if (cmp < 0)
	{
	  cmp = -1;
	}
      else if (cmp > 0)
	{
	  cmp = 1;
	}
      db_make_int (arithptr->value, cmp);
    }
  return NO_ERROR;
}


/* T_REVERSE */
static int
fetch_op_reverse (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_string_reverse (peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_DISK_SIZE */
static int
fetch_op_disk_size (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      db_make_int (arithptr->value, 0);
    }
  else
    {
      db_make_int (arithptr->value, pr_data_writeval_disk_size (peek_right));
      /* call pr_data_writeval_disk_size function to return the size on disk */
    }
  return NO_ERROR;
}


/* T_NULLIF -- when a = b then null else a end */
static int
fetch_op_nullif (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN_STATUS dom_status;

  TP_DOMAIN *target_domain;
  bool can_compare = false;
  int cmp_res = DB_UNK;

  target_domain = domain;
  if (DB_IS_NULL (peek_left))
    {
      PRIM_SET_NULL (arithptr->value);
      return NO_ERROR;
    }
  else if (target_domain == NULL)
    {
      /* no row infers a common domain: resolve_domains resolved the node, or found it takes no value, which only
       * NULL arguments give */
      (void) qexec_domain_unresolved (vd, arithptr->plan_item, regu_var->domain);
      return ER_FAILED;
    }

  /* the comparison the load or resolve_domains resolved */
  cmp_res =
    eval_compare_values_resolved (thread_p, fetch_arith_compare (arithptr, 0), vd, peek_left, peek_right, 0,
				  &can_compare);
  if (cmp_res == DB_EQ)
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (cmp_res == DB_UNK && can_compare == false)
    {
      return ER_FAILED;
    }
  else
    {
      dom_status = tp_value_cast (peek_left, arithptr->value, target_domain, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, target_domain);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}


/* T_EXTRACT */
static int
fetch_op_extract (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_extract_dbval (arithptr->misc_operand, peek_right, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_LEAST */
static int
fetch_op_least (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *no_value_domain = a->no_value_domain;
  TP_DOMAIN_STATUS dom_status;

  int error;
  TP_DOMAIN *target_domain;

  error = fetch_least_or_greatest (thread_p, arithptr, vd, peek_left, peek_right, true);
  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  target_domain = domain;
  if (target_domain == NULL && no_value_domain != NULL && DB_IS_NULL (arithptr->value))
    {
      /* resolve_domains found no value */
      return NO_ERROR;
    }
  if (target_domain == NULL)
    {
      /* no row infers a common domain: resolve_domains resolved the node, or found it takes no value, which only
       * NULL arguments give */
      (void) qexec_domain_unresolved (vd, arithptr->plan_item, regu_var->domain);
      return ER_FAILED;
    }

  dom_status = tp_value_cast (arithptr->value, arithptr->value, target_domain, false);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, arithptr->value, target_domain);
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_GREATEST */
static int
fetch_op_greatest (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  REGU_VARIABLE *regu_var = a->regu_var;
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN *no_value_domain = a->no_value_domain;
  TP_DOMAIN_STATUS dom_status;

  int error;
  TP_DOMAIN *target_domain;

  error = fetch_least_or_greatest (thread_p, arithptr, vd, peek_left, peek_right, false);
  if (error != NO_ERROR)
    {
      return ER_FAILED;
    }

  target_domain = domain;
  if (target_domain == NULL && no_value_domain != NULL && DB_IS_NULL (arithptr->value))
    {
      /* resolve_domains found no value */
      return NO_ERROR;
    }
  if (target_domain == NULL)
    {
      /* no row infers a common domain: resolve_domains resolved the node, or found it takes no value, which only
       * NULL arguments give */
      (void) qexec_domain_unresolved (vd, arithptr->plan_item, regu_var->domain);
      return ER_FAILED;
    }

  dom_status = tp_value_cast (arithptr->value, arithptr->value, target_domain, false);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, arithptr->value, target_domain);
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SYS_CONNECT_BY_PATH */
static int
fetch_op_sys_connect_by_path (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;
  TP_DOMAIN_STATUS dom_status;

  if (!qdata_evaluate_sys_connect_by_path (thread_p, (void *) arithptr->thirdptr->xasl, arithptr->leftptr,
					   peek_right, arithptr->value, vd))
    {
      return ER_FAILED;
    }

  dom_status = tp_value_auto_cast (arithptr->value, arithptr->value, domain);
  if (dom_status != DOMAIN_COMPATIBLE)
    {
      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, arithptr->value, domain);
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_STRCAT */
static int
fetch_op_strcat (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *domain = a->domain;

  if (qdata_strcat_dbval (peek_left, peek_right, arithptr->value, domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_PI */
static int
fetch_op_pi (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  db_make_double (arithptr->value, PI);
  return NO_ERROR;
}


/* T_ROW_COUNT */
static int
fetch_op_row_count (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  int row_count = -1;
  if (session_get_row_count (thread_p, &row_count) != NO_ERROR)
    {
      return ER_FAILED;
    }
  db_make_int (arithptr->value, row_count);

  return NO_ERROR;
}


/* T_LAST_INSERT_ID */
static int
fetch_op_last_insert_id (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  if (session_get_last_insert_id (thread_p, arithptr->value, true) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_EVALUATE_VARIABLE */
static int
fetch_op_evaluate_variable (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;

  if (session_get_variable (thread_p, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (fetch_session_read_value (vd, arithptr, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DEFINE_VARIABLE */
static int
fetch_op_define_variable (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (session_define_variable (thread_p, peek_left, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_RAND, T_RANDOM */
static int
fetch_op_rand (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      /* When random functions are called without a seed, peek_right is null. In this case, rand() or drand() uses
       * a random value stored on value descriptor to generate the same number during executing one SELECT
       * statement. But, each random() or drandom() picks up a seed value to generate different numbers at every
       * call. */
      if (arithptr->opcode == T_RAND)
	{
	  db_make_int (arithptr->value, (int) vd->lrand);
	}
      else
	{
	  long int r;
	  struct drand48_data *rand_buf_p;

	  rand_buf_p = qmgr_get_rand_buf (thread_p);
	  lrand48_r (rand_buf_p, &r);
	  db_make_int (arithptr->value, r);
	}
    }
  else
    {
      /* There are two types of seed: 1) given by user (rightptr's type is TYPE_DBVAL) e.g, select rand(1) from
       * table; 2) fetched from tuple (rightptr's type is TYPE_CONSTANT) e.g, select rand(i) from table; Regarding
       * the former case, rand(1) will generate a sequence of pseudo-random values up to the number of rows.
       * However, on the latter case, rand(i) generates values depending on the column i's value. If, for example,
       * there are three tuples which include column i of 1 in a table, results of the above statements are as
       * follows.  rand(1) rand(i) ============= ============= 89400484 89400484 976015093 89400484 1792756325
       * 89400484 */
      if (arithptr->rightptr->type == TYPE_CONSTANT || arithptr->rightptr->type == TYPE_ATTR_ID)
	{
	  struct drand48_data buf;
	  long int r;

	  srand48_r ((long) db_get_int (peek_right), &buf);
	  lrand48_r (&buf, &r);
	  db_make_int (arithptr->value, r);
	}
      else
	{
	  long int r;

	  if (arithptr->rand_seed == NULL)
	    {
	      arithptr->rand_seed = (struct drand48_data *) malloc (sizeof (struct drand48_data));

	      if (arithptr->rand_seed == NULL)
		{
		  return ER_FAILED;
		}

	      srand48_r ((long) db_get_int (peek_right), arithptr->rand_seed);
	    }

	  lrand48_r (arithptr->rand_seed, &r);
	  db_make_int (arithptr->value, r);
	}
    }
  return NO_ERROR;
}


/* T_DRAND, T_DRANDOM */
static int
fetch_op_drand (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      if (arithptr->opcode == T_DRAND)
	{
	  db_make_double (arithptr->value, (double) vd->drand);
	}
      else
	{
	  double r;
	  struct drand48_data *rand_buf_p;

	  rand_buf_p = qmgr_get_rand_buf (thread_p);
	  drand48_r (rand_buf_p, &r);
	  db_make_double (arithptr->value, r);
	}
    }
  else
    {
      if (arithptr->rightptr->type == TYPE_CONSTANT || arithptr->rightptr->type == TYPE_ATTR_ID)
	{
	  struct drand48_data buf;
	  double r;

	  srand48_r ((long) db_get_int (peek_right), &buf);
	  drand48_r (&buf, &r);
	  db_make_double (arithptr->value, r);
	}
      else
	{
	  double r;

	  if (arithptr->rand_seed == NULL)
	    {
	      arithptr->rand_seed = (struct drand48_data *) malloc (sizeof (struct drand48_data));

	      if (arithptr->rand_seed == NULL)
		{
		  return ER_FAILED;
		}

	      srand48_r ((long) db_get_int (peek_right), arithptr->rand_seed);
	    }

	  drand48_r (arithptr->rand_seed, &r);
	  db_make_double (arithptr->value, r);
	}
    }
  return NO_ERROR;
}


/* T_LIST_DBS */
static int
fetch_op_list_dbs (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (qdata_list_dbs (thread_p, arithptr->value, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SYS_GUID */
static int
fetch_op_sys_guid (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  if (db_uuidv4 (arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_UUID */
static int
fetch_op_uuid (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;

  int version;
  if (DB_IS_NULL (peek_right))
    {
      /* NULL version argument is not allowed; UUID() with no argument arrives as the constant 4 */
      version = -1;
    }
  else
    {
      version = db_get_int (peek_right);
    }

  if (version == 0 || version == 4)
    {
      if (db_uuid_bin (UUID_V4, NULL, 0, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else if (version == 7)
    {
      UUID_STATE uuid_state;
      uuid_state.last_ms = &thread_p->uuidv7_last_ms;
      uuid_state.seq = &thread_p->uuidv7_seq;

      if (db_uuid_bin
	  (UUID_V7, &uuid_state,
	   ((uint64_t) vd->sys_epochtime * 1000ULL) + (uint64_t) (vd->sys_datetime.time % 1000),
	   arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      if (db_uuid_bin (UUID_UNSUPPORTED, NULL, 0, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}


/* T_TYPEOF */
static int
fetch_op_typeof (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (db_typeof_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_INDEX_CARDINALITY */
static int
fetch_op_index_cardinality (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (qdata_get_cardinality (thread_p, peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_EXEC_STATS */
static int
fetch_op_exec_stats (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (session_get_exec_stats_and_clear (thread_p, peek_right, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TO_ENUMERATION_VALUE */
static int
fetch_op_to_enumeration_value (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;
  TP_DOMAIN *arith_domain = a->arith_domain;

  if (db_value_to_enumeration_value (peek_right, arithptr->value, arith_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_INET_ATON */
static int
fetch_op_inet_aton (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_inet_aton (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_INET_NTOA */
static int
fetch_op_inet_ntoa (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_inet_ntoa (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_CHARSET, T_COLLATION */
static int
fetch_op_charset (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (db_get_cs_coll_info (arithptr->value, peek_right, (arithptr->opcode == T_COLLATION) ? 1 : 0) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_TRACE_STATS */
static int
fetch_op_trace_stats (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  if (session_get_trace_stats (thread_p, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_WIDTH_BUCKET */
static int
fetch_op_width_bucket (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;
  DB_VALUE *peek_fourth = a->peek_fourth;

  if (db_width_bucket (arithptr->value, peek_left, peek_right, peek_third, peek_fourth) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_INDEX_PREFIX */
static int
fetch_op_index_prefix (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (db_string_index_prefix (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_SLEEP */
static int
fetch_op_sleep (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (db_sleep (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_DBTIMEZONE, T_SESSIONTIMEZONE */
static int
fetch_op_dbtimezone (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;

  char *source_str;

  if (arithptr->opcode == T_DBTIMEZONE)
    {
      source_str = (char *) tz_get_system_timezone ();
    }
  else
    {
      source_str = (char *) tz_get_session_local_timezone ();
    }
  db_make_string (arithptr->value, source_str);
  arithptr->value->need_clear = false;
  return NO_ERROR;
}


/* T_TZ_OFFSET */
static int
fetch_op_tz_offset (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;
  DB_VALUE *peek_right = a->peek_right;

  DB_DATETIME tmp_datetime, utc_datetime;
  DB_VALUE timezone;
  int timezone_milis;

  /* extract the timezone part */
  if (db_sys_timezone (&timezone) != NO_ERROR)
    {
      return ER_FAILED;
    }
  timezone_milis = db_get_int (&timezone) * 60000;
  tmp_datetime = vd->sys_datetime;
  db_add_int_to_datetime (&tmp_datetime, timezone_milis, &utc_datetime);

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_tz_offset (peek_right, arithptr->value, &utc_datetime) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}


/* T_NEW_TIME */
static int
fetch_op_new_time (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;
  DB_VALUE *peek_third = a->peek_third;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_new_time (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_FROM_TZ */
static int
fetch_op_from_tz (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_left = a->peek_left;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_from_tz (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_CONV_TZ */
static int
fetch_op_conv_tz (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else
    {
      if (db_conv_tz (peek_right, arithptr->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}


/* T_UTC_TIMESTAMP */
static int
fetch_op_utc_timestamp (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  val_descr *vd = a->vd;

  DB_DATE date;
  DB_TIME time;
  int year, month, day, hour, minute, second;
  DB_TIMESTAMP timestamp;

  tz_timestamp_decode_no_leap_sec (vd->sys_epochtime, &year, &month, &day, &hour, &minute, &second);
  date = julian_encode (month + 1, day, year);
  db_time_encode (&time, hour, minute, second);
  db_timestamp_encode_ses (&date, &time, &timestamp, NULL);
  db_make_timestamp (arithptr->value, timestamp);
  return NO_ERROR;
}


/* T_CRC32 */
static int
fetch_op_crc32 (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (DB_IS_NULL (peek_right))
    {
      PRIM_SET_NULL (arithptr->value);
    }
  else if (db_crc32_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_ESTIMATED_TABLE_ROWS, T_ESTIMATED_AVG_ROW_LENGTH, T_ESTIMATED_DATA_LENGTH, T_ESTIMATED_DATA_FREE */
static int
fetch_op_estimated_table_rows (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (qdata_get_estimated_heap_stat (thread_p, peek_right, arithptr->value, arithptr->opcode) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}


/* T_COLLECTION_TO_STRING */
static int
fetch_op_collection_to_string (THREAD_ENTRY * thread_p, FETCH_ARITH_ARGS * a)
{
  ARITH_TYPE *arithptr = a->arithptr;
  DB_VALUE *peek_right = a->peek_right;

  if (db_collection_to_string_dbval (arithptr->value, peek_right) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * fetch_arith_op () - the operator body of an opcode, NULL for an opcode without one (the node keeps the
 *   cleared value it had: the former switch's default)
 */
FETCH_ARITH_OP
fetch_arith_op (OPERATOR_TYPE opcode)
{
  switch (opcode)
    {
    case T_ADD:
      return fetch_op_add;
    case T_BIT_NOT:
      return fetch_op_bit_not;
    case T_BIT_AND:
      return fetch_op_bit_and;
    case T_BIT_OR:
      return fetch_op_bit_or;
    case T_BIT_XOR:
      return fetch_op_bit_xor;
    case T_BITSHIFT_LEFT:
    case T_BITSHIFT_RIGHT:
      return fetch_op_bitshift_left;
    case T_INTDIV:
    case T_INTMOD:
      return fetch_op_intdiv;
    case T_BIT_COUNT:
      return fetch_op_bit_count;
    case T_SUB:
      return fetch_op_sub;
    case T_MUL:
      return fetch_op_mul;
    case T_DIV:
      return fetch_op_div;
    case T_UNPLUS:
      return fetch_op_unplus;
    case T_UNMINUS:
      return fetch_op_unminus;
    case T_PRIOR:
      return fetch_op_prior;
    case T_CONNECT_BY_ROOT:
      return fetch_op_connect_by_root;
    case T_QPRIOR:
      return fetch_op_qprior;
    case T_MOD:
      return fetch_op_mod;
    case T_FLOOR:
      return fetch_op_floor;
    case T_CEIL:
      return fetch_op_ceil;
    case T_SIGN:
      return fetch_op_sign;
    case T_ABS:
      return fetch_op_abs;
    case T_EXP:
      return fetch_op_exp;
    case T_SQRT:
      return fetch_op_sqrt;
    case T_SIN:
      return fetch_op_sin;
    case T_COS:
      return fetch_op_cos;
    case T_TAN:
      return fetch_op_tan;
    case T_COT:
      return fetch_op_cot;
    case T_LN:
      return fetch_op_ln;
    case T_LOG2:
      return fetch_op_log2;
    case T_LOG10:
      return fetch_op_log10;
    case T_ACOS:
      return fetch_op_acos;
    case T_ASIN:
      return fetch_op_asin;
    case T_DEGREES:
      return fetch_op_degrees;
    case T_DATE:
      return fetch_op_date;
    case T_TIME:
      return fetch_op_time;
    case T_DEFAULT:
      return fetch_op_default;
    case T_RADIANS:
      return fetch_op_radians;
    case T_POWER:
      return fetch_op_power;
    case T_ROUND:
      return fetch_op_round;
    case T_LOG:
      return fetch_op_log;
    case T_INCR:
    case T_DECR:
      return fetch_op_incr;
    case T_TRUNC:
      return fetch_op_trunc;
    case T_CHR:
      return fetch_op_chr;
    case T_INSTR:
      return fetch_op_instr;
    case T_POSITION:
      return fetch_op_position;
    case T_FINDINSET:
      return fetch_op_findinset;
    case T_SUBSTRING:
      return fetch_op_substring;
    case T_OCTET_LENGTH:
      return fetch_op_octet_length;
    case T_BIT_LENGTH:
      return fetch_op_bit_length;
    case T_CHAR_LENGTH:
      return fetch_op_char_length;
    case T_LOWER:
      return fetch_op_lower;
    case T_UPPER:
      return fetch_op_upper;
    case T_HEX:
      return fetch_op_hex;
    case T_ASCII:
      return fetch_op_ascii;
    case T_CONV:
      return fetch_op_conv;
    case T_BIN:
      return fetch_op_bin;
    case T_MD5:
      return fetch_op_md5;
    case T_UUID_FORMAT:
      return fetch_op_uuid_format;
    case T_SHA_ONE:
      return fetch_op_sha_one;
    case T_AES_ENCRYPT:
      return fetch_op_aes_encrypt;
    case T_AES_DECRYPT:
      return fetch_op_aes_decrypt;
    case T_SHA_TWO:
      return fetch_op_sha_two;
    case T_TO_BASE64:
      return fetch_op_to_base64;
    case T_FROM_BASE64:
      return fetch_op_from_base64;
    case T_SPACE:
      return fetch_op_space;
    case T_TRIM:
      return fetch_op_trim;
    case T_LTRIM:
      return fetch_op_ltrim;
    case T_RTRIM:
      return fetch_op_rtrim;
    case T_FROM_UNIXTIME:
      return fetch_op_from_unixtime;
    case T_LPAD:
      return fetch_op_lpad;
    case T_RPAD:
      return fetch_op_rpad;
    case T_REPLACE:
      return fetch_op_replace;
    case T_TRANSLATE:
      return fetch_op_translate;
    case T_ADD_MONTHS:
      return fetch_op_add_months;
    case T_LAST_DAY:
      return fetch_op_last_day;
    case T_TIME_FORMAT:
      return fetch_op_time_format;
    case T_YEAR:
    case T_MONTH:
    case T_DAY:
      return fetch_op_year;
    case T_HOUR:
    case T_MINUTE:
    case T_SECOND:
      return fetch_op_hour;
    case T_QUARTER:
      return fetch_op_quarter;
    case T_WEEKDAY:
    case T_DAYOFWEEK:
      return fetch_op_weekday;
    case T_DAYOFYEAR:
      return fetch_op_dayofyear;
    case T_TODAYS:
      return fetch_op_todays;
    case T_FROMDAYS:
      return fetch_op_fromdays;
    case T_TIMETOSEC:
      return fetch_op_timetosec;
    case T_SECTOTIME:
      return fetch_op_sectotime;
    case T_TIMESTAMP:
      return fetch_op_timestamp;
    case T_LIKE_LOWER_BOUND:
    case T_LIKE_UPPER_BOUND:
      return fetch_op_like_lower_bound;
    case T_MAKEDATE:
      return fetch_op_makedate;
    case T_ADDTIME:
      return fetch_op_addtime;
    case T_MAKETIME:
      return fetch_op_maketime;
    case T_WEEK:
      return fetch_op_week;
    case T_UNIX_TIMESTAMP:
      return fetch_op_unix_timestamp;
    case T_MONTHS_BETWEEN:
      return fetch_op_months_between;
    case T_ATAN2:
      return fetch_op_atan2;
    case T_ATAN:
      return fetch_op_atan;
    case T_FORMAT:
      return fetch_op_format;
    case T_DATE_FORMAT:
      return fetch_op_date_format;
    case T_STR_TO_DATE:
      return fetch_op_str_to_date;
    case T_ADDDATE:
      return fetch_op_adddate;
    case T_DATE_ADD:
      return fetch_op_date_add;
    case T_SUBDATE:
      return fetch_op_subdate;
    case T_DATEDIFF:
      return fetch_op_datediff;
    case T_TIMEDIFF:
      return fetch_op_timediff;
    case T_DATE_SUB:
      return fetch_op_date_sub;
    case T_SYS_DATE:
      return fetch_op_sys_date;
    case T_SYS_TIME:
      return fetch_op_sys_time;
    case T_SYS_DATETIME:
      return fetch_op_sys_datetime;
    case T_SYS_TIMESTAMP:
      return fetch_op_sys_timestamp;
    case T_CURRENT_DATE:
      return fetch_op_current_date;
    case T_CURRENT_TIME:
      return fetch_op_current_time;
    case T_CURRENT_TIMESTAMP:
      return fetch_op_current_timestamp;
    case T_CURRENT_DATETIME:
      return fetch_op_current_datetime;
    case T_UTC_TIME:
      return fetch_op_utc_time;
    case T_UTC_DATE:
      return fetch_op_utc_date;
    case T_LOCAL_TRANSACTION_ID:
      return fetch_op_local_transaction_id;
    case T_TO_CHAR:
      return fetch_op_to_char;
    case T_BLOB_TO_BIT:
      return fetch_op_blob_to_bit;
    case T_CLOB_TO_CHAR:
      return fetch_op_clob_to_char;
    case T_BIT_TO_BLOB:
      return fetch_op_bit_to_blob;
    case T_CHAR_TO_CLOB:
      return fetch_op_char_to_clob;
    case T_LOB_LENGTH:
      return fetch_op_lob_length;
    case T_TO_DATE:
      return fetch_op_to_date;
    case T_TO_TIME:
      return fetch_op_to_time;
    case T_TO_TIMESTAMP:
    case T_TO_TIMESTAMP_TZ:
      return fetch_op_to_timestamp;
    case T_TO_DATETIME:
    case T_TO_DATETIME_TZ:
      return fetch_op_to_datetime;
    case T_TO_NUMBER:
      return fetch_op_to_number;
    case T_CURRENT_VALUE:
      return fetch_op_current_value;
    case T_NEXT_VALUE:
      return fetch_op_next_value;
    case T_CAST:
    case T_CAST_WRAP:
      return fetch_op_cast;
    case T_CAST_NOFAIL:
      return fetch_op_cast_nofail;
    case T_CASE:
    case T_DECODE:
    case T_IF:
      return fetch_op_case;
    case T_PREDICATE:
      return fetch_op_predicate;
    case T_NVL:
    case T_IFNULL:
    case T_COALESCE:
      return fetch_op_nvl;
    case T_NVL2:
      return fetch_op_nvl2;
    case T_ISNULL:
      return fetch_op_isnull;
    case T_CONCAT:
      return fetch_op_concat;
    case T_CONCAT_WS:
      return fetch_op_concat_ws;
    case T_FIELD:
      return fetch_op_field;
    case T_REPEAT:
      return fetch_op_repeat;
    case T_LEFT:
      return fetch_op_left;
    case T_RIGHT:
      return fetch_op_right;
    case T_LOCATE:
      return fetch_op_locate;
    case T_SUBSTRING_INDEX:
      return fetch_op_substring_index;
    case T_MID:
      return fetch_op_mid;
    case T_STRCMP:
      return fetch_op_strcmp;
    case T_REVERSE:
      return fetch_op_reverse;
    case T_DISK_SIZE:
      return fetch_op_disk_size;
    case T_NULLIF:
      return fetch_op_nullif;
    case T_EXTRACT:
      return fetch_op_extract;
    case T_LEAST:
      return fetch_op_least;
    case T_GREATEST:
      return fetch_op_greatest;
    case T_SYS_CONNECT_BY_PATH:
      return fetch_op_sys_connect_by_path;
    case T_STRCAT:
      return fetch_op_strcat;
    case T_PI:
      return fetch_op_pi;
    case T_ROW_COUNT:
      return fetch_op_row_count;
    case T_LAST_INSERT_ID:
      return fetch_op_last_insert_id;
    case T_EVALUATE_VARIABLE:
      return fetch_op_evaluate_variable;
    case T_DEFINE_VARIABLE:
      return fetch_op_define_variable;
    case T_RAND:
    case T_RANDOM:
      return fetch_op_rand;
    case T_DRAND:
    case T_DRANDOM:
      return fetch_op_drand;
    case T_LIST_DBS:
      return fetch_op_list_dbs;
    case T_SYS_GUID:
      return fetch_op_sys_guid;
    case T_UUID:
      return fetch_op_uuid;
    case T_TYPEOF:
      return fetch_op_typeof;
    case T_INDEX_CARDINALITY:
      return fetch_op_index_cardinality;
    case T_EXEC_STATS:
      return fetch_op_exec_stats;
    case T_TO_ENUMERATION_VALUE:
      return fetch_op_to_enumeration_value;
    case T_INET_ATON:
      return fetch_op_inet_aton;
    case T_INET_NTOA:
      return fetch_op_inet_ntoa;
    case T_CHARSET:
    case T_COLLATION:
      return fetch_op_charset;
    case T_TRACE_STATS:
      return fetch_op_trace_stats;
    case T_WIDTH_BUCKET:
      return fetch_op_width_bucket;
    case T_INDEX_PREFIX:
      return fetch_op_index_prefix;
    case T_SLEEP:
      return fetch_op_sleep;
    case T_DBTIMEZONE:
    case T_SESSIONTIMEZONE:
      return fetch_op_dbtimezone;
    case T_TZ_OFFSET:
      return fetch_op_tz_offset;
    case T_NEW_TIME:
      return fetch_op_new_time;
    case T_FROM_TZ:
      return fetch_op_from_tz;
    case T_CONV_TZ:
      return fetch_op_conv_tz;
    case T_UTC_TIMESTAMP:
      return fetch_op_utc_timestamp;
    case T_CRC32:
      return fetch_op_crc32;
    case T_ESTIMATED_TABLE_ROWS:
    case T_ESTIMATED_AVG_ROW_LENGTH:
    case T_ESTIMATED_DATA_LENGTH:
    case T_ESTIMATED_DATA_FREE:
      return fetch_op_estimated_table_rows;
    case T_COLLECTION_TO_STRING:
      return fetch_op_collection_to_string;
    default:
      return NULL;
    }
}
