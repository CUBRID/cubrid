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

#include "config.h"
#include "domain_resolver.h"
#include "object_domain_convert.h"
#include "db_date_status.h"
#include "db_function.hpp"
#include "dbtype.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "chartype.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "language_support.h"
#include "error_manager.h"
#include "perf_monitor.h"
#include "string_opfunc.h"
#include "thread_manager.hpp"
#include <atomic>
#include <cstddef>
#include <mutex>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int domain_character_result (int opcode, const DOMAIN_OPERAND * operands, int n_operands,
				    const TP_DOMAIN * compiled, RESOLVED_DOMAIN * result);
static const TP_DOMAIN *domain_variable_string_value (const TP_DOMAIN * domain);

/* D-325-01/02: CAST and pre-cast consumers supply ASSIGN explicitly. */
DOMAIN_CONV_FUNC
domain_lookup_converter (DB_TYPE source, const TP_DOMAIN * target, DOMAIN_CTX context)
{
  DOMAIN_CONVERT_MODE mode = context == DOMAIN_CTX_ASSIGN ? DOMAIN_CONVERT_ASSIGN
    : context == DOMAIN_CTX_COMPARE || context == DOMAIN_CTX_KEY_ELEM ? DOMAIN_CONVERT_COMPARE : DOMAIN_CONVERT_OPERAND;
  return domain_lookup_converter (source, target, mode);
}

static DB_TYPE
domain_operand_type (const DOMAIN_OPERAND * operand)
{
  if (operand->val_type != DB_TYPE_NULL || operand->domain == NULL)
    {
      return operand->val_type;
    }
  return TP_DOMAIN_TYPE (operand->domain);
}

/* The operand's own domain when it describes its type, otherwise the default domain of that type
 * (a classified slot keeps its value domain while val_type is the class, D-328-06). */
static const TP_DOMAIN *
domain_operand_domain (const DOMAIN_OPERAND * operand)
{
  DB_TYPE type = domain_operand_type (operand);
  if (operand->domain != NULL && TP_DOMAIN_TYPE (operand->domain) == type)
    {
      return operand->domain;
    }
  return tp_domain_resolve_default (type);
}

static void
domain_set_operand (RESOLVED_DOMAIN * result, int i, const DOMAIN_OPERAND * operand, DB_TYPE target,
		    DOMAIN_CONVERT_MODE mode)
{
  if (target == domain_operand_type (operand))
    {
      result->operand_domain[i] = domain_operand_domain (operand);
      result->conv[i] = NULL;
      return;
    }
  result->operand_domain[i] = tp_domain_resolve_default (target);
  result->conv[i] = domain_lookup_converter (domain_operand_type (operand), result->operand_domain[i], mode);
}

/* Result of two numbers after the pre-cast: the typed dispatch of qdata_{add,subtract,multiply,divide}_*_to_dbval. */
static DB_TYPE
domain_arith_number (int opcode, DB_TYPE left, DB_TYPE right)
{
  if (left == DB_TYPE_MONETARY || right == DB_TYPE_MONETARY)
    {
      return DB_TYPE_MONETARY;
    }
  if (left == DB_TYPE_DOUBLE || right == DB_TYPE_DOUBLE)
    {
      return DB_TYPE_DOUBLE;
    }
  if (left == DB_TYPE_FLOAT || right == DB_TYPE_FLOAT)
    {
      DB_TYPE other = left == DB_TYPE_FLOAT ? right : left;
      /* FLOAT with NUMERIC goes through qdata_coerce_numeric_to_double; FLOAT + BIGINT (not BIGINT + FLOAT) is
       * qdata_add_double in qdata_add_float_to_dbval. */
      if (other == DB_TYPE_NUMERIC || (opcode == T_ADD && left == DB_TYPE_FLOAT && right == DB_TYPE_BIGINT))
	{
	  return DB_TYPE_DOUBLE;
	}
      return DB_TYPE_FLOAT;
    }
  if (left == DB_TYPE_NUMERIC || right == DB_TYPE_NUMERIC)
    {
      return DB_TYPE_NUMERIC;
    }
  if (left == DB_TYPE_BIGINT || right == DB_TYPE_BIGINT)
    {
      return DB_TYPE_BIGINT;
    }
  if (left == DB_TYPE_INTEGER || right == DB_TYPE_INTEGER)
    {
      return DB_TYPE_INTEGER;
    }
  return DB_TYPE_SHORT;
}

/* db_string_concatenate on two values (so:4004): character strings give VARCHAR (qstr_make_typed_string, so:1274;
 * CHAR comes only from the NULL/empty-string path) and bit strings BIT or VARBIT; a character with a bit string is
 * ER_QSTR_INCOMPATIBLE_CODE_SETS and any other operand ER_QSTR_INVALID_DATA_TYPE. */
static int
domain_arith_concat (DB_TYPE left, DB_TYPE right, DB_TYPE * result_type)
{
  if (!TP_IS_CHAR_BIT_TYPE (left) || !TP_IS_CHAR_BIT_TYPE (right))
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }
  if (TP_IS_CHAR_TYPE (left) != TP_IS_CHAR_TYPE (right))
    {
      return ER_QSTR_INCOMPATIBLE_CODE_SETS;
    }
  if (TP_IS_CHAR_TYPE (left))
    {
      *result_type = DB_TYPE_VARCHAR;
    }
  else
    {
      *result_type = (left == DB_TYPE_VARBIT || right == DB_TYPE_VARBIT) ? DB_TYPE_VARBIT : DB_TYPE_BIT;
    }
  return NO_ERROR;
}

/* Date/time subtraction after the pre-cast (qdata_subtract_*_to_dbval): one side is a date/time, the other a date/time
 * or a discrete number. */
static DB_TYPE
domain_arith_subtract_datetime (DB_TYPE left, DB_TYPE right)
{
  if (TP_IS_DISCRETE_NUMBER_TYPE (left))
    {
      switch (right)
	{
	case DB_TYPE_TIME:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_TIMESTAMPTZ:
	  return right;
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_DATETIMETZ:
	  return left == DB_TYPE_BIGINT ? DB_TYPE_NULL : DB_TYPE_BIGINT;
	case DB_TYPE_DATE:
	  return left == DB_TYPE_SHORT ? DB_TYPE_TIME : DB_TYPE_DATE;
	default:
	  return DB_TYPE_NULL;
	}
    }

  bool right_is_number = TP_IS_DISCRETE_NUMBER_TYPE (right);
  bool right_is_timestamp = right == DB_TYPE_TIMESTAMP || right == DB_TYPE_TIMESTAMPLTZ || right == DB_TYPE_TIMESTAMPTZ;
  bool right_is_datetime = right == DB_TYPE_DATETIME || right == DB_TYPE_DATETIMELTZ || right == DB_TYPE_DATETIMETZ;
  switch (left)
    {
    case DB_TYPE_TIME:
      return right_is_number ? DB_TYPE_TIME : right == DB_TYPE_TIME ? DB_TYPE_INTEGER : DB_TYPE_NULL;
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      return right_is_number ? left : right_is_timestamp ? DB_TYPE_INTEGER : right_is_datetime ? DB_TYPE_BIGINT
	: DB_TYPE_NULL;
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      if (right_is_number)
	{
	  /* DATETIMELTZ is subtracted as DATETIMETZ (qdata_subtract_dbval) */
	  return left == DB_TYPE_DATETIME ? DB_TYPE_DATETIME : DB_TYPE_DATETIMETZ;
	}
      return (right_is_timestamp || right_is_datetime || right == DB_TYPE_DATE) ? DB_TYPE_BIGINT : DB_TYPE_NULL;
    case DB_TYPE_DATE:
      return right_is_number ? DB_TYPE_DATE : right == DB_TYPE_DATE ? DB_TYPE_INTEGER : DB_TYPE_NULL;
    default:
      return DB_TYPE_NULL;
    }
}

/*
 * domain_arith_dispatch - the typed dispatch of qdata_{add,subtract,multiply,divide}_dbval after the pre-cast
 *   return: NO_ERROR, or the error the dispatcher raises for the pair (D-335-02)
 *   first, second(in): operand types after the pre-cast and, for addition, the swap
 *   result_type(out): the result type; DB_TYPE_NULL when a typed helper passes the pair over without an error
 *
 * A dispatcher rejects a first operand it has no helper for: addition always (qo:2724), the others unless
 * return_null_on_function_errors; a collection with a non-collection always. A typed helper leaves no value for a
 * second operand it has no case for (DATETIME - TIME), except the date addition, which rejects it like the
 * dispatchers (qdata_add_date_to_dbval).
 */
static int
domain_arith_dispatch (int opcode, DB_TYPE first, DB_TYPE second, DB_TYPE * result_type)
{
  const int reject = prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ? NO_ERROR : ER_QPROC_INVALID_DATATYPE;

  *result_type = DB_TYPE_NULL;
  if (TP_IS_NUMERIC_TYPE (first))
    {
      if (TP_IS_NUMERIC_TYPE (second))
	{
	  *result_type = domain_arith_number (opcode, first, second);
	}
      else if (opcode == T_SUB && TP_IS_DATE_OR_TIME_TYPE (second))
	{
	  /* the pre-cast made a floating first operand BIGINT */
	  *result_type = domain_arith_subtract_datetime (first, second);
	}
      return NO_ERROR;
    }
  if (TP_IS_SET_TYPE (first) && opcode != T_DIV)
    {
      if (!TP_IS_SET_TYPE (second))
	{
	  return ER_QPROC_INVALID_DATATYPE;
	}
      /* partial resolve of a late-bound collection result (domain_p == NULL) */
      *result_type = (opcode == T_ADD ? first == second : (first == second && first == DB_TYPE_SET))
	? first : DB_TYPE_MULTISET;
      return NO_ERROR;
    }

  switch (opcode)
    {
    case T_ADD:
      if (TP_IS_CHAR_BIT_TYPE (first))
	{
	  return domain_arith_concat (first, second, result_type);
	}
      if (first == DB_TYPE_DATE)
	{
	  if (!TP_IS_DISCRETE_NUMBER_TYPE (second))
	    {
	      return reject;
	    }
	  *result_type = first;
	  return NO_ERROR;
	}
      if (TP_IS_DATE_OR_TIME_TYPE (first))
	{
	  /* TIMESTAMPLTZ and DATETIMETZ with anything but an integer read an unset value today (qo:2651, 2325): P0
	   * exception, no value like their siblings */
	  if (TP_IS_DISCRETE_NUMBER_TYPE (second))
	    {
	      *result_type = first;
	    }
	  return NO_ERROR;
	}
      return ER_QPROC_INVALID_DATATYPE;

    case T_SUB:
      if (TP_IS_DATE_OR_TIME_TYPE (first))
	{
	  *result_type = domain_arith_subtract_datetime (first, second);
	  return NO_ERROR;
	}
      return reject;

    default:
      return reject;
    }
}

/*
 * domain_arith_binary - the pre-cast and typed dispatch of the four binary operators
 *   return: NO_ERROR, or the error the operator raises for the pair (D-335-02)
 *   left, right(in): operand types
 *   left_target, right_target(out): type each operand is cast to (qo:2438~2560, 4818~4910, 5512~5560, 6134~6260)
 *   result_type(out): type the operator produces; DB_TYPE_NULL for a NULL operand or a pair it passes over
 */
static int
domain_arith_binary (int opcode, DB_TYPE left, DB_TYPE right, DB_TYPE * left_target, DB_TYPE * right_target,
		     DB_TYPE * result_type)
{
  bool is_add = opcode == T_ADD;

  *left_target = left;
  *right_target = right;
  *result_type = DB_TYPE_NULL;

  if (!is_add && (left == DB_TYPE_NULL || right == DB_TYPE_NULL))
    {
      return NO_ERROR;
    }

  /* ENUM: the name when added to a string, the ordinal otherwise; multiply and divide take no ENUM */
  if ((is_add || opcode == T_SUB) && (left == DB_TYPE_ENUMERATION || right == DB_TYPE_ENUMERATION))
    {
      if (left == DB_TYPE_ENUMERATION)
	{
	  DB_TYPE step = (is_add && TP_IS_CHAR_BIT_TYPE (right)) ? DB_TYPE_VARCHAR : DB_TYPE_SHORT;
	  return domain_arith_binary (opcode, step, right, left_target, right_target, result_type);
	}
      DB_TYPE step = (is_add && TP_IS_CHAR_BIT_TYPE (left)) ? DB_TYPE_VARCHAR : DB_TYPE_SHORT;
      return domain_arith_binary (opcode, left, step, left_target, right_target, result_type);
    }

  if (is_add && prm_get_bool_value (PRM_ID_PLUS_AS_CONCAT) && TP_IS_CHAR_BIT_TYPE (left) && TP_IS_CHAR_BIT_TYPE (right))
    {
      return domain_arith_concat (left, right, result_type);
    }

  if (left == DB_TYPE_NULL || right == DB_TYPE_NULL)
    {
      return NO_ERROR;
    }

  /* addition handles STRING + NUMBER, NUMBER + DATE and STRING + DATE with the operands swapped */
  DB_TYPE *first_target = left_target, *second_target = right_target;
  DB_TYPE first = left, second = right;
  if (is_add && ((TP_IS_CHAR_TYPE (left) && TP_IS_NUMERIC_TYPE (right))
		 || (TP_IS_NUMERIC_TYPE (left) && TP_IS_DATE_OR_TIME_TYPE (right))
		 || (TP_IS_CHAR_TYPE (left) && TP_IS_DATE_OR_TIME_TYPE (right))))
    {
      first = right;
      second = left;
      first_target = right_target;
      second_target = left_target;
    }

  if (TP_IS_NUMERIC_TYPE (first) && TP_IS_CHAR_TYPE (second))
    {
      second = DB_TYPE_DOUBLE;
    }
  else if (!is_add && TP_IS_CHAR_TYPE (first) && TP_IS_NUMERIC_TYPE (second))
    {
      first = DB_TYPE_DOUBLE;
    }
  else if (TP_IS_CHAR_TYPE (first) && TP_IS_CHAR_TYPE (second))
    {
      first = DB_TYPE_DOUBLE;
      second = DB_TYPE_DOUBLE;
    }
  else if (is_add && TP_IS_DATE_OR_TIME_TYPE (first) && (TP_IS_FLOATING_NUMBER_TYPE (second)
							 || TP_IS_CHAR_TYPE (second)))
    {
      second = DB_TYPE_BIGINT;
    }
  else if (opcode == T_SUB && TP_IS_DATE_OR_TIME_TYPE (first) && TP_IS_FLOATING_NUMBER_TYPE (second))
    {
      second = DB_TYPE_BIGINT;
    }
  else if (opcode == T_SUB && TP_IS_FLOATING_NUMBER_TYPE (first) && TP_IS_DATE_OR_TIME_TYPE (second))
    {
      first = DB_TYPE_BIGINT;
    }
  else if (opcode == T_SUB && TP_IS_DATE_OR_TIME_TYPE (first) && TP_IS_CHAR_TYPE (second))
    {
      second = first == DB_TYPE_TIME ? DB_TYPE_TIME : DB_TYPE_DATETIME;
      first = first == DB_TYPE_TIME ? DB_TYPE_TIME : DB_TYPE_DATETIME;
    }
  else if (opcode == T_SUB && TP_IS_CHAR_TYPE (first) && TP_IS_DATE_OR_TIME_TYPE (second))
    {
      first = second == DB_TYPE_TIME ? DB_TYPE_TIME : DB_TYPE_DATETIME;
      second = second == DB_TYPE_TIME ? DB_TYPE_TIME : DB_TYPE_DATETIME;
    }
  else if (opcode == T_DIV && prm_get_bool_value (PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR)
	   && TP_IS_DISCRETE_NUMBER_TYPE (first) && TP_IS_DISCRETE_NUMBER_TYPE (second))
    {
      first = DB_TYPE_NUMERIC;
      second = DB_TYPE_NUMERIC;
    }
  *first_target = first;
  *second_target = second;

  return domain_arith_dispatch (opcode, first, second, result_type);
}

/* The result of the db_mod_<type> helpers for two numbers (ar:1965~); a character operand is DOUBLE by then. */
static DB_TYPE
domain_arith_mod_number (DB_TYPE left, DB_TYPE right)
{
  if (left == DB_TYPE_MONETARY || right == DB_TYPE_MONETARY)
    {
      return DB_TYPE_MONETARY;
    }
  if (left == DB_TYPE_DOUBLE || right == DB_TYPE_DOUBLE)
    {
      return DB_TYPE_DOUBLE;
    }
  if (left == DB_TYPE_FLOAT)
    {
      return right == DB_TYPE_NUMERIC ? DB_TYPE_DOUBLE : DB_TYPE_FLOAT;
    }
  if (left == DB_TYPE_NUMERIC)
    {
      return right == DB_TYPE_FLOAT ? DB_TYPE_DOUBLE : DB_TYPE_NUMERIC;
    }
  /* an integer first operand */
  if (right == DB_TYPE_FLOAT || right == DB_TYPE_NUMERIC)
    {
      return right;
    }
  if (left == DB_TYPE_BIGINT || right == DB_TYPE_BIGINT)
    {
      return DB_TYPE_BIGINT;
    }
  if (left == DB_TYPE_INTEGER || right == DB_TYPE_INTEGER)
    {
      return DB_TYPE_INTEGER;
    }
  return DB_TYPE_SHORT;
}

/*
 * domain_arith_mod - db_mod_dbval (ar:1965): a character first operand is taken as DOUBLE (db_mod_string), the typed
 *		      helpers take a number or character second operand as DOUBLE; any other pair is rejected unless
 *		      return_null_on_function_errors
 */
static int
domain_arith_mod (DB_TYPE left, DB_TYPE right, DB_TYPE * left_target, DB_TYPE * right_target, DB_TYPE * result_type)
{
  *left_target = TP_IS_CHAR_TYPE (left) ? DB_TYPE_DOUBLE : left;
  *right_target = TP_IS_CHAR_TYPE (right) ? DB_TYPE_DOUBLE : right;
  *result_type = DB_TYPE_NULL;

  if (left == DB_TYPE_NULL || right == DB_TYPE_NULL)
    {
      *left_target = left;
      *right_target = right;
      return NO_ERROR;
    }
  if (!TP_IS_NUMERIC_TYPE (*left_target) || !TP_IS_NUMERIC_TYPE (*right_target))
    {
      *left_target = left;
      *right_target = right;
      return prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ? NO_ERROR : ER_QPROC_INVALID_DATATYPE;
    }
  *result_type = domain_arith_mod_number (*left_target, *right_target);
  return NO_ERROR;
}

static int
domain_resolve_arith (int opcode, const DOMAIN_OPERAND * operands, int n_operands, RESOLVED_DOMAIN * result)
{
  DB_TYPE result_type;

  switch (opcode)
    {
    case T_ADD:
    case T_SUB:
    case T_MUL:
    case T_DIV:
    case T_MOD:
      {
	DB_TYPE left_target, right_target;
	const DB_TYPE left = domain_operand_type (&operands[0]), right = domain_operand_type (&operands[1]);
	assert (n_operands == 2);
	int error = opcode == T_MOD ? domain_arith_mod (left, right, &left_target, &right_target, &result_type)
	  : domain_arith_binary (opcode, left, right, &left_target, &right_target, &result_type);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
	/* D-328-04: the pre-cast is tp_value_auto_cast, ASSIGN (ROUND) */
	domain_set_operand (result, 0, &operands[0], left_target, DOMAIN_CONVERT_ASSIGN);
	domain_set_operand (result, 1, &operands[1], right_target, DOMAIN_CONVERT_ASSIGN);
	/* D-335-05: an ENUM added to a string without plus_as_concat reaches DOUBLE through its name, not its ordinal */
	for (int i = 0; i < 2 && opcode == T_ADD; i++)
	  {
	    if (domain_operand_type (&operands[i]) == DB_TYPE_ENUMERATION
		&& TP_DOMAIN_TYPE (result->operand_domain[i]) == DB_TYPE_DOUBLE)
	      {
		result->conv[i] = domain_enumeration_name_converter ();
	      }
	  }
	break;
      }

    case T_UNMINUS:
    case T_ABS:
    case T_FLOOR:
    case T_CEIL:
    case T_ROUND:
    case T_TRUNC:
      {
	/* the value is the right operand of the unary operators and the left one of ROUND/TRUNC */
	int arg = (opcode == T_ROUND || opcode == T_TRUNC) ? 0 : n_operands - 1;
	DB_TYPE type = domain_operand_type (&operands[arg]);
	DB_TYPE target;
	if (type == DB_TYPE_NULL || TP_IS_NUMERIC_TYPE (type))
	  {
	    result_type = target = type;
	  }
	else if ((opcode == T_ROUND || opcode == T_TRUNC) && TP_IS_DATE_TYPE (type))
	  {
	    result_type = DB_TYPE_DATE;
	    target = type;
	  }
	else if (TP_IS_CHAR_TYPE (type) || opcode == T_ROUND || opcode == T_TRUNC)
	  {
	    /* tp_value_str_auto_cast_to_number, or ROUND/TRUNC try DOUBLE for anything else */
	    result_type = target = DB_TYPE_DOUBLE;
	  }
	else if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	  {
	    result_type = DB_TYPE_NULL;
	    target = type;
	  }
	else
	  {
	    return ER_QPROC_INVALID_DATATYPE;
	  }
	domain_set_operand (result, arg, &operands[arg], target, DOMAIN_CONVERT_ASSIGN);
	for (int i = 0; i < n_operands && i < 3; i++)
	  {
	    if (i != arg)
	      {
		domain_set_operand (result, i, &operands[i], domain_operand_type (&operands[i]), DOMAIN_CONVERT_ASSIGN);
	      }
	  }
	break;
      }

    default:
      /* an operator the grid does not know: the gate must not guess (#335) */
      return ER_QPROC_DOMAIN_UNRESOLVED;
    }

  if (opcode == T_ADD && TP_IS_CHAR_TYPE (result_type))
    {
      /* plus as concatenation (qdata_strcat_dbval, db_string_concatenate so:1194): the operands' collations merge
       * and their precisions add, as CONCAT's do (#338) */
      return domain_character_result (T_CONCAT, operands, n_operands, NULL, result);
    }
  /* NUMERIC results stay floating: the value operation decides p/s (converters §3) */
  result->domain = tp_domain_resolve_default (result_type);
  return NO_ERROR;
}

static const TP_DOMAIN *
domain_char_with_collation (DB_TYPE type, int codeset, int collation_id)
{
  TP_DOMAIN *domain = tp_domain_copy (tp_domain_resolve_default (type), false);
  if (domain == NULL)
    {
      return NULL;
    }
  domain->codeset = codeset;
  domain->collation_id = collation_id;
  return tp_domain_cache (domain);
}

/* The coerced side's target of tp_value_compare_with_error: the default domain of the other side's type, carrying
 * the collation of the coerced side when a character or ENUM becomes a character string (od:10580~10608). */
static const TP_DOMAIN *
domain_compare_target (const DOMAIN_OPERAND * coerced, DB_TYPE target_type)
{
  DB_TYPE coerced_type = domain_operand_type (coerced);
  if (TP_TYPE_HAS_COLLATION (coerced_type) && TP_IS_CHAR_TYPE (target_type))
    {
      const TP_DOMAIN *source = domain_operand_domain (coerced);
      int collation_id = coerced->coll_id >= 0 ? coerced->coll_id : source->collation_id;
      return domain_char_with_collation (target_type, source->codeset, collation_id);
    }
  return tp_domain_resolve_default (target_type);
}

static int
domain_resolve_compare (const DOMAIN_OPERAND * operands, RESOLVED_DOMAIN * result)
{
  DB_TYPE type1 = domain_operand_type (&operands[0]);
  DB_TYPE type2 = domain_operand_type (&operands[1]);
  const TP_DOMAIN *target1 = domain_operand_domain (&operands[0]);
  const TP_DOMAIN *target2 = domain_operand_domain (&operands[1]);

  if (type1 != DB_TYPE_NULL && type2 != DB_TYPE_NULL && type1 != type2)
    {
      switch (tp_value_compare_common_domain (type1, type2))
	{
	case TP_COMPARE_COERCE_NONE:
	  break;
	case TP_COMPARE_COERCE_TO_DOUBLE:
	  target1 = target2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	  break;
	case TP_COMPARE_COERCE_FIRST_TO_DATE:
	  target1 = tp_domain_resolve_default (type2);
	  break;
	case TP_COMPARE_COERCE_SECOND_TO_DATE:
	  target2 = tp_domain_resolve_default (type1);
	  break;
	case TP_COMPARE_COERCE_SECOND_TO_FIRST:
	  target2 = domain_compare_target (&operands[1], type1);
	  break;
	case TP_COMPARE_COERCE_FIRST_TO_SECOND:
	  target1 = domain_compare_target (&operands[0], type2);
	  break;
	}
      if (target1 == NULL || target2 == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  /* D-328-05: the planned comparison converters are ASSIGN cells (today's tp_value_coerce on reachable cells) */
  const TP_DOMAIN *targets[2] = { target1, target2 };
  for (int i = 0; i < 2; i++)
    {
      result->operand_domain[i] = targets[i];
      result->conv[i] = targets[i] == domain_operand_domain (&operands[i]) ? NULL
	: domain_lookup_converter (domain_operand_type (&operands[i]), targets[i], DOMAIN_CONVERT_ASSIGN);
    }
  result->domain = target1 != domain_operand_domain (&operands[0]) ? target1 : target2;
  return NO_ERROR;
}

/* tp_infer_common_domain folded left to right: NVL/NVL2/IFNULL/COALESCE/NULLIF/LEAST/GREATEST (fe:3306~3961). */
static int
domain_resolve_common_value (const DOMAIN_OPERAND * operands, int n_operands, RESOLVED_DOMAIN * result)
{
  TP_DOMAIN *common = (TP_DOMAIN *) domain_operand_domain (&operands[0]);
  for (int i = 1; i < n_operands && common != NULL; i++)
    {
      common = tp_infer_common_domain (common, (TP_DOMAIN *) domain_operand_domain (&operands[i]));
    }
  if (common == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (int i = 0; i < n_operands && i < 3; i++)
    {
      result->operand_domain[i] = common;
      result->conv[i] = domain_lookup_converter (domain_operand_type (&operands[i]), common, DOMAIN_CONVERT_OPERAND);
    }
  /* the value cast to the common domain reads as a floating string's maximum precision (#338) */
  result->domain = domain_variable_string_value (common);
  return NO_ERROR;
}

static bool
domain_is_interpolation_type (DB_TYPE type)
{
  return TP_IS_NUMERIC_TYPE (type) || TP_IS_DATE_OR_TIME_TYPE (type);
}

/*
 * domain_interpolation_final () - the domain MEDIAN / PERCENTILE_CONT / PERCENTILE_DISC takes from its first value
 *   (qdata_update_agg_interpolation_func_value_and_domain, qa:3329; the analytic first-execution block, qn:715)
 *   return: the final domain, or NULL when only a value could tell (a string nobody classified)
 *   domain(in): the domain the function holds before that value (never VARIABLE here)
 *   class_type(in): the class the gate gave a string value (DOUBLE, DATETIME or TIME), DB_TYPE_NULL otherwise
 *
 * A date or time stays; a DOUBLE (MEDIAN, PERCENTILE_CONT) or any number (PERCENTILE_DISC) stays; any other number
 * becomes DOUBLE, and a string becomes the first of DOUBLE, DATETIME, TIME it casts to (#337).
 */
static const TP_DOMAIN *
domain_interpolation_final (int function, const TP_DOMAIN * domain, DB_TYPE class_type)
{
  const DB_TYPE type = TP_DOMAIN_TYPE (domain);
  if (TP_IS_DATE_OR_TIME_TYPE (type)
      || (function == PT_PERCENTILE_DISC ? TP_IS_NUMERIC_TYPE (type) : type == DB_TYPE_DOUBLE))
    {
      return domain;
    }
  if (TP_IS_NUMERIC_TYPE (type))
    {
      return tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  if (class_type == DB_TYPE_DOUBLE || class_type == DB_TYPE_DATETIME || class_type == DB_TYPE_TIME)
    {
      return tp_domain_resolve_default (class_type);
    }
  return NULL;
}

/* Whether the late-binding update of qexec_resolve_domains_for_aggregation / the analytic late binding applies: the
 * operand was VARIABLE when compiled (opr_dbtype, not the function's domain) or the function domain leaves collation. */
static bool
domain_function_is_late_bound (const TP_DOMAIN * compiled, const DOMAIN_OPERAND * operand)
{
  return operand->is_gate_slot || compiled == NULL || TP_DOMAIN_COLLATION_FLAG (compiled) != TP_DOMAIN_COLL_NORMAL;
}

/*
 * domain_resolve_aggregate - domains of qexec_resolve_domains_for_aggregation (qx:21504~21630, 21716~21730)
 *   compiled(in): consumer = the aggregate's compiled domain (agg_p->domain, xasl_generation.c:4072)
 *   operand(in): the argument; domain = its compiled domain (opr_dbtype), or its value domain when the gate decides it
 *		  (is_gate_slot: opr_dbtype is VARIABLE today); val_type = value type, classified at the gate (D-328-06)
 *   result(out): domain = the function domain (agg_p->domain after the late-binding update, which the result is cast
 *		  to); operand_domain[0] / conv[0] = the accumulator domain (value_dom) the argument values are coerced
 *		  to (qa:645, 713). value2_dom is a per-function constant the load puts in domain_plan_acc (#333).
 */
static int
domain_resolve_aggregate (int function, const TP_DOMAIN * compiled, const DOMAIN_OPERAND * operand,
			  RESOLVED_DOMAIN * result)
{
  DB_TYPE val_type = domain_operand_type (operand);
  const TP_DOMAIN *function_domain = compiled;
  DB_TYPE operand_type = operand->domain != NULL ? TP_DOMAIN_TYPE (operand->domain) : val_type;
  const TP_DOMAIN *accumulator = NULL;

  if (function == PT_COUNT || function == PT_COUNT_STAR || function == PT_JSON_ARRAYAGG
      || function == PT_JSON_OBJECTAGG)
    {
      /* fixed signatures; the argument is counted or wrapped as it is */
      if (function_domain == NULL)
	{
	  function_domain = (function == PT_COUNT || function == PT_COUNT_STAR) ? &tp_Bigint_domain : &tp_Json_domain;
	}
      result->domain = function_domain;
      result->operand_domain[0] = domain_operand_domain (operand);
      result->conv[0] = NULL;
      return NO_ERROR;
    }

  if (domain_function_is_late_bound (compiled, operand))
    {
      if (TP_IS_CHAR_TYPE (val_type) && (function == PT_SUM || function == PT_AVG))
	{
	  function_domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	}
      else if (!TP_IS_CHAR_TYPE (val_type) && function == PT_GROUP_CONCAT)
	{
	  function_domain = tp_domain_resolve_default (DB_TYPE_VARCHAR);
	}
      else
	{
	  function_domain = domain_operand_domain (operand);
	}
      operand_type = TP_DOMAIN_TYPE (function_domain);
    }

  switch (function)
    {
    case PT_AVG:
    case PT_SUM:
      if (!TP_IS_NUMERIC_TYPE (val_type))
	{
	  accumulator = function_domain;
	}
      else if (TP_DOMAIN_TYPE (function_domain) == DB_TYPE_NUMERIC || val_type == DB_TYPE_NUMERIC)
	{
	  accumulator = tp_domain_resolve (DB_TYPE_NUMERIC, NULL, DB_DEFAULT_NUMERIC_PRECISION,
					   DB_DEFAULT_NUMERIC_SCALE, NULL, 0);
	}
      else if (val_type == DB_TYPE_FLOAT)
	{
	  accumulator = tp_domain_resolve (DB_TYPE_DOUBLE, NULL, DB_DOUBLE_DECIMAL_PRECISION, 0, NULL, 0);
	}
      else
	{
	  accumulator = tp_domain_resolve_default (val_type);
	}
      break;

    case PT_STDDEV:
    case PT_STDDEV_POP:
    case PT_STDDEV_SAMP:
    case PT_VARIANCE:
    case PT_VAR_POP:
    case PT_VAR_SAMP:
      accumulator = &tp_Double_domain;
      break;

    case PT_GROUPBY_NUM:
      accumulator = &tp_Null_domain;
      break;

    case PT_MEDIAN:
    case PT_PERCENTILE_CONT:
    case PT_PERCENTILE_DISC:
      /* keyed on the operand type (opr_dbtype), as today; a number or date operand leaves value_dom unset today
       * (F-333-06), so the accumulator here is the function domain */
      if (operand->val_type == DB_TYPE_NULL && operand->domain != NULL
	  && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (operand->domain)))
	{
	  /* #337: a string value none of DOUBLE, DATETIME, TIME takes (the gate's classification failed): no domain;
	   * the first value raises the error (qexec_resolve_domains_for_aggregation) */
	  return ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
	}
      if (!domain_is_interpolation_type (operand_type))
	{
	  if (domain_is_interpolation_type (val_type))
	    {
	      /* the gate classified the value as DOUBLE, DATETIME or TIME */
	      function_domain = tp_domain_resolve_default (val_type);
	    }
	  else if (TP_IS_CHAR_TYPE (val_type))
	    {
	      /* a string the gate has no value for (a column, an expression) is a number (D-335-10) */
	      function_domain = &tp_Double_domain;
	    }
	  else
	    {
	      return ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
	    }
	}
      else if (function_domain == NULL || TP_DOMAIN_TYPE (function_domain) == DB_TYPE_VARIABLE)
	{
	  /* #337: the compiler leaves the function open for a number or date argument (func_type.cpp) and the first
	   * value opens it with the default domain of its type (qa:3345) */
	  function_domain = tp_domain_resolve_default (operand_type);
	}
      /* #337: then the value takes the function's final class (qa:3355): the gate records that domain */
      {
	const TP_DOMAIN *final_domain = domain_interpolation_final (function, function_domain, val_type);
	if (final_domain != NULL)
	  {
	    function_domain = final_domain;
	  }
      }
      accumulator = function_domain;
      break;

    default:
      /* BIT_AND/OR/XOR, MIN, MAX, GROUP_CONCAT and the rest accumulate in the function domain */
      accumulator = function_domain;
      break;
    }

  if (accumulator == NULL || function_domain == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  result->domain = function_domain;
  result->operand_domain[0] = accumulator;
  /* D-335-04: a value-classified argument converts from its own type to the class (qx:21721 tp_value_cast), and so
   * does a string interpolation argument typed DOUBLE (D-335-10) */
  if (operand->domain != NULL && val_type != DB_TYPE_NULL
      && (TP_DOMAIN_TYPE (operand->domain) != val_type
	  || (TP_IS_CHAR_TYPE (val_type) && TP_DOMAIN_TYPE (accumulator) == DB_TYPE_DOUBLE
	      && (function == PT_MEDIAN || function == PT_PERCENTILE_CONT || function == PT_PERCENTILE_DISC))))
    {
      result->conv[0] = domain_lookup_converter (TP_DOMAIN_TYPE (operand->domain), accumulator, DOMAIN_CONVERT_ASSIGN);
    }
  else
    {
      result->conv[0] = domain_lookup_converter (val_type, accumulator, DOMAIN_CONVERT_OPERAND);
    }
  return NO_ERROR;
}

/* Late-bound analytic function domain (qn:197~259): its own rules, not the aggregate ones. Same inputs as the
 * aggregate; domain = operand_domain[0] = the function domain the operand value is coerced to (qn:252). */
static int
domain_resolve_analytic (int function, const TP_DOMAIN * compiled, const DOMAIN_OPERAND * operand,
			 RESOLVED_DOMAIN * result)
{
  DB_TYPE val_type = domain_operand_type (operand);
  const TP_DOMAIN *argument = compiled;

  if (domain_function_is_late_bound (compiled, operand))
    {
      switch (function)
	{
	case PT_COUNT:
	case PT_COUNT_STAR:
	  argument = tp_domain_resolve_default (DB_TYPE_BIGINT);
	  break;
	case PT_AVG:
	case PT_STDDEV:
	case PT_STDDEV_POP:
	case PT_STDDEV_SAMP:
	case PT_VARIANCE:
	case PT_VAR_POP:
	case PT_VAR_SAMP:
	  argument = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	  break;
	case PT_SUM:
	  argument = TP_IS_NUMERIC_TYPE (val_type) ? domain_operand_domain (operand)
	    : tp_domain_resolve_default (DB_TYPE_DOUBLE);
	  break;
	case PT_MEDIAN:
	case PT_PERCENTILE_CONT:
	  argument = TP_IS_NUMERIC_TYPE (val_type) ? tp_domain_resolve_default (DB_TYPE_DOUBLE)
	    : domain_operand_domain (operand);
	  break;
	default:
	  argument = domain_operand_domain (operand);
	  break;
	}
    }
  if (function == PT_MEDIAN || function == PT_PERCENTILE_CONT || function == PT_PERCENTILE_DISC)
    {
      /* #337: the first execution types an interpolation function (qn:715): a function the compiler left open takes
       * its operand's domain, then a number becomes DOUBLE (PERCENTILE_DISC keeps it) and a string the class the
       * gate gave its value, or DOUBLE when it has no value (D-335-10) */
      const TP_DOMAIN *open = argument == NULL || TP_DOMAIN_TYPE (argument) == DB_TYPE_VARIABLE
	? domain_operand_domain (operand) : argument;
      if (open != NULL && TP_DOMAIN_TYPE (open) != DB_TYPE_VARIABLE)
	{
	  if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (open)) && operand->val_type == DB_TYPE_NULL)
	    {
	      /* a string value none of DOUBLE, DATETIME, TIME takes: the first execution raises the error */
	      return ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
	    }
	  const TP_DOMAIN *final_domain = domain_interpolation_final (function, open, val_type);
	  argument = final_domain != NULL ? final_domain
	    : TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (open)) ? tp_domain_resolve_default (DB_TYPE_DOUBLE) : open;
	}
    }
  if (argument == NULL)
    {
      return ER_FAILED;
    }
  result->domain = argument;
  result->operand_domain[0] = argument;
  result->conv[0] = domain_lookup_converter (val_type, argument, DOMAIN_CONVERT_OPERAND);
  return NO_ERROR;
}

/*
 * domain_resolve_function - result types of the late-bound functions (pt_is_op_gate_dependent) and the value copies
 *   return: NO_ERROR, or the error the function raises for this argument type
 *
 * The value-overloaded ones take their class from the gate (so:7302~7420 ADDTIME, so:22578~22602 STR_TO_DATE); the
 * others have a fixed result type (converters §3) or the type of the argument they copy. fetch_peek_arith gives no
 * value for a NULL first argument, and for any NULL argument of ADDTIME, STR_TO_DATE, NEW_TIME, FROM_TZ and CONV.
 */
static int
domain_resolve_function (int opcode, const DOMAIN_OPERAND * operands, int n_operands,
			 const TP_DOMAIN * consumer_domain, RESOLVED_DOMAIN * result)
{
  DB_TYPE result_type = DB_TYPE_NULL;
  bool null_any = false;
  for (int i = 0; i < n_operands; i++)
    {
      null_any = null_any || domain_operand_type (&operands[i]) == DB_TYPE_NULL;
    }

  switch (opcode)
    {
    case T_ADDTIME:
    case T_STR_TO_DATE:
    case T_NEW_TIME:
    case T_FROM_TZ:
    case T_CONV:
      if (null_any)
	{
	  goto set_operands;
	}
      break;
    case T_TO_CHAR:
    case T_HEX:
    case T_ASCII:
    case T_BIT_LENGTH:
    case T_OCTET_LENGTH:
    case T_HOUR:
    case T_MINUTE:
    case T_SECOND:
    case T_TO_DATE:
    case T_TO_TIME:
    case T_TO_TIMESTAMP:
    case T_TO_TIMESTAMP_TZ:
    case T_TO_DATETIME:
    case T_TO_DATETIME_TZ:
      if (domain_operand_type (&operands[0]) == DB_TYPE_NULL)
	{
	  goto set_operands;
	}
      break;
    default:
      /* a copy takes its argument's domain, NULL included; any other operator is looked up below */
      break;
    }

  switch (opcode)
    {
    case T_ADDTIME:
      switch (domain_operand_type (&operands[0]))
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  /* a value is classified before this (VARCHAR without a zone, DATETIMETZ with one); a string the gate has no
	   * value for (a column, an expression) is VARCHAR, the manual's "date/time string" row (D-335-10) */
	  result_type = DB_TYPE_VARCHAR;
	  break;
	case DB_TYPE_DATETIME:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_DATE:
	  result_type = DB_TYPE_DATETIME;
	  break;
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  result_type = DB_TYPE_DATETIMELTZ;
	  break;
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_TIMESTAMPTZ:
	  result_type = DB_TYPE_DATETIMETZ;
	  break;
	case DB_TYPE_TIME:
	  result_type = DB_TYPE_TIME;
	  break;
	default:
	  return ER_QSTR_INVALID_DATA_TYPE;
	}
      break;

    case T_STR_TO_DATE:
      assert (n_operands >= 2);
      result_type = consumer_domain != NULL && TP_DOMAIN_TYPE (consumer_domain) != DB_TYPE_VARIABLE
	? TP_DOMAIN_TYPE (consumer_domain) : domain_operand_type (&operands[1]);
      if (result_type != DB_TYPE_TIME && result_type != DB_TYPE_DATE && result_type != DB_TYPE_DATETIME
	  && result_type != DB_TYPE_DATETIMETZ)
	{
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      break;

    case T_NEW_TIME:
      /* db_new_time (so:28216) converts DATETIME and TIME within their type */
      result_type = domain_operand_type (&operands[0]);
      if (result_type != DB_TYPE_DATETIME && result_type != DB_TYPE_TIME)
	{
	  return ER_QSTR_INVALID_DATA_TYPE;
	}
      break;

    case T_FROM_TZ:
      /* db_from_tz (so:28381) */
      if (domain_operand_type (&operands[0]) != DB_TYPE_DATETIME)
	{
	  return ER_QSTR_INVALID_DATA_TYPE;
	}
      result_type = DB_TYPE_DATETIMETZ;
      break;

    case T_TO_CHAR:
      /* db_to_char (so:12587): numbers and dates print to VARCHAR, a string comes back as it is */
      result_type = domain_operand_type (&operands[0]);
      if (TP_IS_NUMERIC_TYPE (result_type) || TP_IS_DATE_OR_TIME_TYPE (result_type))
	{
	  result_type = DB_TYPE_VARCHAR;
	}
      else if (!TP_IS_CHAR_TYPE (result_type))
	{
	  return ER_QSTR_INVALID_DATA_TYPE;
	}
      break;

    case T_HEX:
    case T_CONV:
      /* db_hex, db_conv: db_make_string, a floating VARCHAR in LANG_SYS (#338) */
      result->domain = tp_domain_resolve (DB_TYPE_VARCHAR, NULL, DB_MAX_VARCHAR_PRECISION, 0, NULL, LANG_SYS_COLLATION);
      if (result->domain == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      goto copy_operands;

    case T_ASCII:
      /* db_ascii: db_make_short */
      result_type = DB_TYPE_SHORT;
      break;

    case T_BIT_LENGTH:
    case T_OCTET_LENGTH:
    case T_HOUR:
    case T_MINUTE:
    case T_SECOND:
      result_type = DB_TYPE_INTEGER;
      break;

    case T_TO_DATE:
      result_type = DB_TYPE_DATE;
      break;

    case T_TO_TIME:
      result_type = DB_TYPE_TIME;
      break;

    case T_TO_TIMESTAMP:
      result_type = DB_TYPE_TIMESTAMP;
      break;

    case T_TO_TIMESTAMP_TZ:
      result_type = DB_TYPE_TIMESTAMPTZ;
      break;

    case T_TO_DATETIME:
      result_type = DB_TYPE_DATETIME;
      break;

    case T_TO_DATETIME_TZ:
      result_type = DB_TYPE_DATETIMETZ;
      break;

    case T_DEFINE_VARIABLE:
      /* session_define_variable returns the value it stores: (name, value) */
      assert (n_operands == 2);
      result->domain = domain_operand_domain (&operands[1]);
      goto copy_operands;

    case T_PRIOR:
    case T_CONNECT_BY_ROOT:
    case T_QPRIOR:
      /* the argument's value in another row */
      result->domain = domain_operand_domain (&operands[0]);
      goto copy_operands;

    case T_CAST:
    case T_CAST_NOFAIL:
    case T_CAST_WRAP:
      /* the value is cast into the compiled target (fetch_peek_arith, tp_value_cast_internal): a target the compiler
       * left VARIABLE (the set operation's CAST(x AS uncertain) wrapper, L-18) casts no value - a NULL stays NULL and
       * any other argument fails as develop's does (-181) - so the node holds no value (#340) */
      result->domain = consumer_domain != NULL && TP_DOMAIN_TYPE (consumer_domain) != DB_TYPE_VARIABLE
	? consumer_domain : &tp_Null_domain;
      goto copy_operands;

    default:
      /* a function the grid does not know: the gate must not guess (#335) */
      return ER_QPROC_DOMAIN_UNRESOLVED;
    }

set_operands:
  result->domain = tp_domain_resolve_default (result_type);

copy_operands:
  /* the functions take their arguments as they are; the classified slot keeps its value */
  for (int i = 0; i < n_operands && i < 3; i++)
    {
      result->operand_domain[i] = operands[i].domain;
      result->conv[i] = NULL;
    }
  result->domain = domain_variable_string_value (result->domain);
  return NO_ERROR;
}

/*
 * domain_resolve_list_column - a list column: its producer's domain (#323 ALIAS); a set-operation or CTE column
 *   unifies its branches as qfile_unify_types does (#337)
 *   return: NO_ERROR, or ER_QPROC_INCOMPATIBLE_TYPES when the branches differ
 *
 * A branch without a value (a NULL bind) takes the other's domain; one domain, or one variable string type, keeps
 * the first branch's. Two different domains are rejected before execution (#341, the user's decision): develop's
 * qfile_unify_types raised the error only when both branch lists held rows and took the other branch's domain when
 * one was empty, which no decision before the rows can follow.
 */
static int
domain_resolve_list_column (const DOMAIN_OPERAND * operands, int n_operands, RESOLVED_DOMAIN * result)
{
  const TP_DOMAIN *domain = NULL;
  for (int i = 0; i < n_operands; i++)
    {
      const TP_DOMAIN *branch = domain_operand_domain (&operands[i]);
      if (branch == NULL || TP_DOMAIN_TYPE (branch) == DB_TYPE_NULL)
	{
	  continue;
	}
      if (domain == NULL)
	{
	  domain = branch;
	}
      else if (branch != domain
	       && !(TP_DOMAIN_TYPE (branch) == TP_DOMAIN_TYPE (domain)
		    && ((pr_is_string_type (TP_DOMAIN_TYPE (domain)) && pr_is_variable_type (TP_DOMAIN_TYPE (domain)))
			|| TP_DOMAIN_TYPE (domain) == DB_TYPE_JSON)))
	{
	  return ER_QPROC_INCOMPATIBLE_TYPES;
	}
    }
  result->domain = result->operand_domain[0] = domain != NULL ? domain : &tp_Null_domain;
  return NO_ERROR;
}

/* develop compares through tp_value_coerce, which refuses these pairs before it converts
 * (TP_IMPLICIT_COERCION_NOT_ALLOWED): a comparison's converter fails there as develop's coercion does, where the ASSIGN
 * cell would convert (F-352-08) */
static bool
domain_implicit_coercion_refused (DB_TYPE source, DB_TYPE target)
{
  if (source == DB_TYPE_BLOB || source == DB_TYPE_CLOB || target == DB_TYPE_BLOB || target == DB_TYPE_CLOB)
    {
      return true;
    }
  if (TP_IS_CHAR_TYPE (source))
    {
      return !(TP_IS_CHAR_TYPE (target) || TP_IS_DATE_OR_TIME_TYPE (target) || TP_IS_NUMERIC_TYPE (target)
	       || target == DB_TYPE_ENUMERATION);
    }
  return source != DB_TYPE_ENUMERATION && TP_IS_CHAR_TYPE (target);
}

/* The converter a comparison runs on a side: the ASSIGN cell (D-328-05, today's tp_value_coerce on the cells a
 * comparison reaches), failing what implicit coercion refuses. */
static DOMAIN_CONV_FUNC
domain_compare_converter (DB_TYPE source, const TP_DOMAIN * target)
{
  if (domain_implicit_coercion_refused (source, TP_DOMAIN_TYPE (target)))
    {
      /* the incompatible cell */
      return domain_lookup_converter (source, NULL, DOMAIN_CONVERT_ASSIGN);
    }
  return domain_lookup_converter (source, target, DOMAIN_CONVERT_ASSIGN);
}

bool
domain_fixes_values (const TP_DOMAIN * domain)
{
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE
    && (!TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (domain)) || TP_DOMAIN_COLLATION_FLAG (domain) == TP_DOMAIN_COLL_NORMAL);
}

void
domain_compare_key_of (const TP_DOMAIN * domain, DOMAIN_COMPARE_KEY * key)
{
  key->type = domain != NULL ? TP_DOMAIN_TYPE (domain) : DB_TYPE_NULL;
  const bool has_collation = domain != NULL && TP_TYPE_HAS_COLLATION (key->type);
  key->codeset = has_collation ? TP_DOMAIN_CODESET (domain) : -1;
  key->collation = has_collation ? TP_DOMAIN_COLLATION (domain) : -1;
}

void
domain_compare_key_collate (DOMAIN_COMPARE_KEY * key, const TP_DOMAIN * collate)
{
  if (collate != NULL && TP_TYPE_HAS_COLLATION (key->type))
    {
      key->codeset = TP_DOMAIN_CODESET (collate);
      key->collation = TP_DOMAIN_COLLATION (collate);
    }
}

TP_DOMAIN_STATUS
domain_run_converter (DOMAIN_CONV_FUNC converter, const TP_DOMAIN * target, const DB_VALUE * source, DB_VALUE * result)
{
  db_value_domain_init (result, TP_DOMAIN_TYPE (target), target->precision, target->scale);
  if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (target)))
    {
      db_string_put_cs_and_collation (result, TP_DOMAIN_CODESET (target), TP_DOMAIN_COLLATION (target));
    }
  return converter (source, result, target);
}

/* develop's outcome of a conversion that failed: the rank of the two sides' types at that point, and -181 where the
 * caller asks whether the values compare (tp_value_compare asks nothing and gets no error) */
static DB_VALUE_COMPARE_RESULT
domain_compare_conversion_failed (const DOMAIN_COMPARE * compare, bool first_converted, bool * can_compare)
{
  DB_TYPE type[2] = { (DB_TYPE) compare->source[0], (DB_TYPE) compare->source[1] };
  if (first_converted)
    {
      type[compare->first] = (DB_TYPE) compare->converted_first;
    }
  if (can_compare != NULL)
    {
      *can_compare = false;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (type[0]), pr_type_name (type[1]));
    }
  return tp_more_general_type (type[0], type[1]) > 0 ? DB_GT : DB_LT;
}

/*
 * domain_compare_converted () - kernel CONVERT: develop's coercion with its converters planned - the first side, then
 *				 the other, then an ENUM's codeset for the string it meets - and cmpval
 *
 * A constant side the gate converted comes in converted; one whose conversion failed gives develop's outcome at its
 * turn. Every conversion the row runs is counted (Num_planned_convert).
 */
static DB_VALUE_COMPARE_RESULT
domain_compare_converted (THREAD_ENTRY * thread_p, const DOMAIN_COMPARE * compare, const DB_VALUE * value1,
			  const DB_VALUE * value2, int total_order, bool * can_compare)
{
  const DB_VALUE *side[2] = { value1, value2 };
  DB_VALUE converted[2], codeset_value;
  int used = 0;			/* bit i: converted[i] holds a value, bit 2: codeset_value */
  DB_VALUE_COMPARE_RESULT result;

  for (int k = 0; k < 2; k++)
    {
      const int s = k == 0 ? compare->first : 1 - compare->first;
      if (compare->failed & (1 << s))
	{
	  result = domain_compare_conversion_failed (compare, k == 1, can_compare);
	  goto end;
	}
      if (compare->conv[s] == NULL)
	{
	  continue;
	}
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_PLANNED_CONVERT);
      used |= 1 << s;
      if (domain_run_converter (compare->conv[s], compare->target[s], side[s], &converted[s]) != DOMAIN_COMPATIBLE)
	{
	  result = domain_compare_conversion_failed (compare, k == 1, can_compare);
	  goto end;
	}
      side[s] = &converted[s];
    }
  if (compare->codeset_side >= 0)
    {
      /* an ENUM compared as a string of another codeset: develop brings the other string into the ENUM's */
      const DB_VALUE *text = side[compare->codeset_side];
      DB_DATA_STATUS data_status;
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_PLANNED_CONVERT);
      used |= 4;
      db_value_domain_init (&codeset_value, DB_VALUE_DOMAIN_TYPE (text), DB_VALUE_PRECISION (text), 0);
      db_string_put_cs_and_collation (&codeset_value, lang_get_collation (compare->collation)->codeset,
				      compare->collation);
      if (db_char_string_coerce (text, &codeset_value, &data_status) != NO_ERROR)
	{
	  result = DB_UNK;
	  goto end;
	}
      assert (data_status == DATA_STATUS_OK);
      side[compare->codeset_side] = &codeset_value;
    }
  result = compare->cmp->cmpval (side[0], side[1], compare->coercion, total_order, NULL, compare->collation);

end:
  if (used & 1)
    {
      pr_clear_value (&converted[0]);
    }
  if (used & 2)
    {
      pr_clear_value (&converted[1]);
    }
  if (used & 4)
    {
      pr_clear_value (&codeset_value);
    }
  return result;
}

DB_VALUE_COMPARE_RESULT
domain_compare_values (THREAD_ENTRY * thread_p, const DOMAIN_COMPARE * compare, const DB_VALUE * value1,
		       const DB_VALUE * value2, int total_order, bool * can_compare)
{
  switch (compare->kernel)
    {
    case DOMAIN_COMPARE_DIRECT:
      return compare->cmp->cmpval ((DB_VALUE *) value1, (DB_VALUE *) value2, compare->coercion, total_order, NULL,
				   compare->collation);
    case DOMAIN_COMPARE_CONVERT:
      return domain_compare_converted (thread_p, compare, value1, value2, total_order, can_compare);
    case DOMAIN_COMPARE_RANK:
      /* types that do not compare as they are, without coercion: develop answers by their rank */
      if (can_compare != NULL)
	{
	  *can_compare = false;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name ((DB_TYPE) compare->source[0]),
		  pr_type_name ((DB_TYPE) compare->source[1]));
	}
      return (DB_VALUE_COMPARE_RESULT) compare->rank;
    default:
      /* strings whose collations do not merge: develop's outcome at every row */
#if !defined (NDEBUG)
      if (compare->kernel != DOMAIN_COMPARE_COLLATIONS)
	{
	  fprintf (stderr, "planned comparison kernel: kernel=%d site=%d source=%d,%d values=%d,%d\n",
		   (int) compare->kernel, compare->site, (int) compare->source[0], (int) compare->source[1],
		   (int) DB_VALUE_DOMAIN_TYPE (value1), (int) DB_VALUE_DOMAIN_TYPE (value2));
	}
#endif
      assert (compare->kernel == DOMAIN_COMPARE_COLLATIONS);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
      if (can_compare != NULL)
	{
	  *can_compare = false;
	}
      return DB_UNK;
    }
}

void
domain_compare_key_of_value (const DB_VALUE * value, DOMAIN_COMPARE_KEY * key)
{
  key->type = DB_VALUE_DOMAIN_TYPE (value);
  key->codeset = key->collation = -1;
  if (TP_IS_CHAR_TYPE (key->type))
    {
      key->codeset = db_get_string_codeset (value);
      key->collation = db_get_string_collation (value);
    }
  else if (key->type == DB_TYPE_ENUMERATION)
    {
      key->codeset = db_get_enum_codeset (value);
      key->collation = db_get_enum_collation (value);
    }
}

static bool
domain_compare_key_equal (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs)
{
  return lhs->type == rhs->type && lhs->codeset == rhs->codeset && lhs->collation == rhs->collation;
}

/* Whether keys[i] is the first of its (column, key) among keys[0..i]. */
static bool
domain_key_first (const int *columns, const DOMAIN_COMPARE_KEY * keys, int i)
{
  for (int j = 0; j < i; j++)
    {
      if (columns[j] == columns[i] && domain_compare_key_equal (&keys[j], &keys[i]))
	{
	  return false;
	}
    }
  return true;
}

/* Whether (keys[i], keys[j]) is an entry of the table: two different keys of one column, each met first there. A
 * NULL key compares nothing (its values are NULL, which the B-tree and the range build answer first). */
static bool
domain_key_pair (const int *columns, const DOMAIN_COMPARE_KEY * keys, int i, int j)
{
  return j != i && columns[j] == columns[i] && keys[i].type != DB_TYPE_NULL && keys[j].type != DB_TYPE_NULL
    && domain_key_first (columns, keys, i) && domain_key_first (columns, keys, j)
    && !domain_compare_key_equal (&keys[i], &keys[j]);
}

static int
domain_key_compares_count (const int *columns, const DOMAIN_COMPARE_KEY * keys, int n_keys)
{
  int n_entries = 0;
  for (int i = 0; i < n_keys; i++)
    {
      for (int j = 0; j < n_keys; j++)
	{
	  if (domain_key_pair (columns, keys, i, j))
	    {
	      n_entries++;
	    }
	}
    }
  return n_entries;
}

size_t
domain_key_compares_bytes (const int *columns, const DOMAIN_COMPARE_KEY * keys, int n_keys)
{
  const int n_entries = domain_key_compares_count (columns, keys, n_keys);
  const size_t n_slots = n_entries > 0 ? (size_t) n_entries : 1;
  return offsetof (DOMAIN_KEY_COMPARES, entry) + sizeof (DOMAIN_KEY_COMPARE_ENTRY) * n_slots;
}

int
domain_resolve_key_compares (const int *columns, const DOMAIN_COMPARE_KEY * keys, int n_keys,
			     DOMAIN_KEY_COMPARES * table, size_t bytes)
{
  assert (bytes == domain_key_compares_bytes (columns, keys, n_keys));
  table->bytes = (int) bytes;
  table->n_entries = 0;
  for (int i = 0; i < n_keys; i++)
    {
      for (int j = 0; j < n_keys; j++)
	{
	  if (!domain_key_pair (columns, keys, i, j))
	    {
	      continue;
	    }
	  DOMAIN_KEY_COMPARE_ENTRY *entry = &table->entry[table->n_entries++];
	  entry->column = columns[i];
	  entry->key[0] = keys[i];
	  entry->key[1] = keys[j];
	  const int error = domain_resolve_comparison (&keys[i], &keys[j], &entry->compare);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  return NO_ERROR;
}

static const DOMAIN_COMPARE *
domain_key_compare_find_keys (const DOMAIN_KEY_COMPARES * table, int column, const DOMAIN_COMPARE_KEY * key)
{
  for (int i = 0; table != NULL && i < table->n_entries; i++)
    {
      const DOMAIN_KEY_COMPARE_ENTRY *entry = &table->entry[i];
      if (entry->column == column && domain_compare_key_equal (&entry->key[0], &key[0])
	  && domain_compare_key_equal (&entry->key[1], &key[1]))
	{
	  return &entry->compare;
	}
    }
  return NULL;
}

const DOMAIN_COMPARE *
domain_key_compare_find (const DOMAIN_KEY_COMPARES * table, int column, const DB_VALUE * value1,
			 const DB_VALUE * value2)
{
  DOMAIN_COMPARE_KEY key[2];
  domain_compare_key_of_value (value1, &key[0]);
  domain_compare_key_of_value (value2, &key[1]);
  return domain_key_compare_find_keys (table, column, key);
}

const TP_DOMAIN *
domain_key_value_domain (const TP_DOMAIN * domain)
{
  return domain != NULL && TP_DOMAIN_TYPE (domain) == DB_TYPE_OBJECT ? tp_domain_resolve_default (DB_TYPE_OID) : domain;
}

const TP_DOMAIN *
domain_key_column (const TP_DOMAIN * key_type, int column)
{
  if (key_type == NULL || TP_DOMAIN_TYPE (key_type) != DB_TYPE_MIDXKEY)
    {
      return column == 0 ? key_type : NULL;
    }
  const TP_DOMAIN *domain = key_type->setdomain;
  for (; domain != NULL && column > 0; column--)
    {
      domain = domain->next;
    }
  return domain;
}

const TP_DOMAIN *
domain_in_key_direction (const TP_DOMAIN * domain, const TP_DOMAIN * column)
{
  if (domain == NULL || column == NULL || domain->is_desc == column->is_desc)
    {
      return domain;
    }
  TP_DOMAIN *copy = domain_copy_one (domain);
  if (copy == NULL)
    {
      return NULL;
    }
  copy->is_desc = column->is_desc;
  return tp_domain_cache (copy);
}

const TP_DOMAIN *
domain_ascending_key_type (const TP_DOMAIN * key_type)
{
  bool descending = false;
  const bool midxkey = TP_DOMAIN_TYPE (key_type) == DB_TYPE_MIDXKEY;
  for (const TP_DOMAIN * column = midxkey ? key_type->setdomain : key_type; column != NULL;
       column = midxkey ? column->next : NULL)
    {
      descending = descending || column->is_desc;
    }
  if (!descending)
    {
      return key_type;
    }
  if (!midxkey)
    {
      TP_DOMAIN *ascending = tp_domain_copy (key_type, false);
      if (ascending == NULL)
	{
	  return NULL;
	}
      ascending->is_desc = 0;
      return tp_domain_cache (ascending);
    }
  TP_DOMAIN *columns = tp_domain_copy (key_type->setdomain, false);
  if (columns == NULL)
    {
      return NULL;
    }
  for (TP_DOMAIN * column = columns; column != NULL; column = column->next)
    {
      column->is_desc = 0;
    }
  TP_DOMAIN *ascending = tp_domain_construct (DB_TYPE_MIDXKEY, NULL, key_type->precision, key_type->scale, columns);
  if (ascending == NULL)
    {
      while (columns != NULL)
	{
	  TP_DOMAIN *next = columns->next;
	  tp_domain_free (columns);
	  columns = next;
	}
      return NULL;
    }
  return tp_domain_cache (ascending);
}

TP_DOMAIN *
domain_copy_one (const TP_DOMAIN * domain)
{
  TP_DOMAIN one = *domain;
  one.next = NULL;
  return tp_domain_copy (&one, false);
}

DOMAIN_CONV_FUNC
domain_key_strict_converter (DB_TYPE source, const TP_DOMAIN * column)
{
  const DB_TYPE target = TP_DOMAIN_TYPE (column);
  /* tp_value_coerce_strict refuses any other target */
  if (!TP_IS_NUMERIC_TYPE (target) && !TP_IS_DATE_OR_TIME_TYPE (target))
    {
      return NULL;
    }
  return domain_lookup_converter (source, column, DOMAIN_CTX_KEY_ELEM);
}

DOMAIN_KEY_RULE
domain_key_rule (const TP_DOMAIN * element, const TP_DOMAIN * column, bool midxkey, DOMAIN_CONV_FUNC * strict_conv)
{
  *strict_conv = NULL;
  if (!midxkey)
    {
      /* a single-column key takes its value as it is (#321 section 4.2) */
      return DOMAIN_KEY_INDEX;
    }
  const DB_TYPE type = TP_DOMAIN_TYPE (element);
  const DB_TYPE column_type = TP_DOMAIN_TYPE (column);
  if (type != column_type)
    {
      *strict_conv = domain_key_strict_converter (type, column);
      return *strict_conv != NULL ? DOMAIN_KEY_STRICT : DOMAIN_KEY_KEEP;
    }
  if (column_type == DB_TYPE_NUMERIC || column_type == DB_TYPE_CHAR || column_type == DB_TYPE_BIT)
    {
      /* the parameters too: precision and scale, length, collation */
      return tp_domain_match_ignore_order (column, element, TP_EXACT_MATCH) ? DOMAIN_KEY_INDEX : DOMAIN_KEY_KEEP;
    }
  return DOMAIN_KEY_INDEX;
}

bool
domain_key_compares_as_is (const DOMAIN_COMPARE_KEY * a, const DOMAIN_COMPARE_KEY * b)
{
  if (!TP_ARE_COMPARABLE_KEY_TYPES (a->type, b->type))
    {
      return false;
    }
  return !(TP_IS_CHAR_TYPE (a->type) && TP_IS_CHAR_TYPE (b->type) && a->collation != b->collation);
}

DB_VALUE_COMPARE_RESULT
domain_search_key_compare (const DOMAIN_SEARCH_KEYS * keys, int column, DB_VALUE * value1, DB_VALUE * value2,
			   int do_coercion, int total_order, bool * can_compare)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  DOMAIN_COMPARE_KEY key[2];
  domain_compare_key_of_value (value1, &key[0]);
  domain_compare_key_of_value (value2, &key[1]);
  if (domain_compare_key_equal (&key[0], &key[1]))
    {
      /* one type and one collation: nothing to coerce or merge (a kept column of the index column's type with other
       * parameters, whose precision or length alone differs) */
      return tp_value_compare_with_error (value1, value2, do_coercion, total_order, can_compare);
    }
  const DOMAIN_COMPARE *compare = domain_key_compare_find_keys (keys != NULL ? keys->compares : NULL, column, key);
  if (compare != NULL)
    {
      if (compare->kernel == DOMAIN_COMPARE_OBJECT)
	{
	  /* an object side: develop's comparison, which meets OIDs on the server */
	  return tp_value_compare_with_error (value1, value2, do_coercion, total_order, can_compare);
	}
      return domain_compare_values (thread_p, compare, value1, value2, total_order, can_compare);
    }
  if (keys != NULL && keys->values_decide)
    {
      /* a column took develop's rule from its value in this scan (D-336-E): develop's comparison, counted */
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_DOMAIN_KEY_COERCE);
      return tp_value_compare_with_error (value1, value2, do_coercion, total_order, can_compare);
    }
  /* the execution boundary (b): the plan knows every key a column's values take */
  assert (false);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DOMAIN_UNRESOLVED, 4, "execute", "", column,
	  pr_type_name (key[0].type));
  *can_compare = false;
  return DB_UNK;
}

DB_VALUE_COMPARE_RESULT
domain_search_key_compare_element (const void *arg, int column, DB_VALUE * value1, DB_VALUE * value2, int do_coercion,
				   int total_order, bool * can_compare)
{
  return domain_search_key_compare ((const DOMAIN_SEARCH_KEYS *) arg, column, value1, value2, do_coercion,
				    total_order, can_compare);
}

/*
 * domain_resolve_comparison () - the comparison develop's tp_value_compare_with_error makes between a value of each
 *   key, decided before any row (D-352-01)
 *   return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY when a target domain cannot be cached
 *
 * Follows od:tp_value_compare_with_error step by step: the direction of tp_value_compare_common_domain (TO_DOUBLE
 * converts the string first and the other side only after it), the target a string or an ENUM keeps its codeset and
 * collation in, the collation rule of its tail (equal collations, the ENUM's, LANG_RT_COMMON_COLL on one codeset,
 * otherwise -1), and the type whose cmpval compares. A NULL key (a side whose values are NULL) and an OBJECT key keep
 * develop's comparison: NULL answers before any coercion, and the server holds an object as its OID.
 */
int
domain_resolve_comparison (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs, DOMAIN_COMPARE * result)
{
  const DOMAIN_COMPARE_KEY *key[2] = { lhs, rhs };
  DB_TYPE after[2] = { lhs->type, rhs->type };
  int enum_side = -1;

  *result = DOMAIN_COMPARE
  {
  };
  result->value[0] = result->value[1] = -1;
  result->codeset_side = -1;
  result->site = -1;
  result->source[0] = (unsigned char) lhs->type;
  result->source[1] = (unsigned char) rhs->type;
  result->kernel = DOMAIN_COMPARE_DIRECT;
  result->coercion = 1;

  if (lhs->type == DB_TYPE_NULL || rhs->type == DB_TYPE_NULL)
    {
      /* its value is NULL, and develop's comparison answers NULL before it counts or coerces */
      result->kernel = DOMAIN_COMPARE_VALUES;
      return NO_ERROR;
    }
  if (lhs->type == DB_TYPE_OBJECT || rhs->type == DB_TYPE_OBJECT)
    {
      /* an OBJECT domain's values are OIDs on the server (and OID against OBJECT compares by OID on the client) */
      result->kernel = DOMAIN_COMPARE_OBJECT;
      return NO_ERROR;
    }
  if (lhs->type != rhs->type)
    {
      const TP_COMPARE_COERCION coercion = tp_value_compare_common_domain (lhs->type, rhs->type);
      const TP_DOMAIN *target = NULL;
      int side = 0;
      switch (coercion)
	{
	case TP_COMPARE_COERCE_NONE:
	  break;

	case TP_COMPARE_COERCE_TO_DOUBLE:
	  side = TP_IS_CHAR_TYPE (lhs->type) ? 0 : 1;
	  target = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	  result->first = (unsigned char) side;
	  result->target[side] = target;
	  result->conv[side] = domain_compare_converter (key[side]->type, target);
	  if (key[1 - side]->type != DB_TYPE_DOUBLE)
	    {
	      result->target[1 - side] = target;
	      result->conv[1 - side] = domain_compare_converter (key[1 - side]->type, target);
	    }
	  after[0] = after[1] = DB_TYPE_DOUBLE;
	  break;

	case TP_COMPARE_COERCE_FIRST_TO_DATE:
	case TP_COMPARE_COERCE_SECOND_TO_DATE:
	  side = coercion == TP_COMPARE_COERCE_FIRST_TO_DATE ? 0 : 1;
	  target = tp_domain_resolve_default (key[1 - side]->type);
	  result->first = (unsigned char) side;
	  result->target[side] = target;
	  result->conv[side] = domain_compare_converter (key[side]->type, target);
	  after[side] = key[1 - side]->type;
	  break;

	case TP_COMPARE_COERCE_SECOND_TO_FIRST:
	case TP_COMPARE_COERCE_FIRST_TO_SECOND:
	  side = coercion == TP_COMPARE_COERCE_SECOND_TO_FIRST ? 1 : 0;
	  target = tp_domain_resolve_default (key[1 - side]->type);
	  if (TP_TYPE_HAS_COLLATION (key[side]->type) && TP_IS_CHAR_TYPE (key[1 - side]->type))
	    {
	      /* the coerced string or ENUM keeps its codeset and collation; an ENUM's collation is the comparison's */
	      target = domain_char_with_collation (key[1 - side]->type, key[side]->codeset, key[side]->collation);
	      if (key[side]->type == DB_TYPE_ENUMERATION)
		{
		  enum_side = side;
		}
	    }
	  if (target == NULL)
	    {
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  result->first = (unsigned char) side;
	  result->target[side] = target;
	  result->conv[side] = domain_compare_converter (key[side]->type, target);
	  after[side] = key[1 - side]->type;
	  break;
	}
    }
  result->converted_first = (unsigned char) after[result->first];

  /* the tail on the converted values: the first side's cmpval under the comparison's collation */
  result->cmp = pr_type_from_id (after[0]);
  if (!TP_IS_CHAR_TYPE (after[0]))
    {
      result->collation = 0;
    }
  else if (key[0]->collation == key[1]->collation)
    {
      result->collation = key[0]->collation;
    }
  else if (enum_side >= 0)
    {
      const int codeset = lang_get_collation (key[enum_side]->collation)->codeset;
      result->collation = key[enum_side]->collation;
      result->codeset_side = key[0]->codeset != codeset ? 0 : key[1]->codeset != codeset ? 1 : -1;
    }
  else if (key[0]->codeset == key[1]->codeset)
    {
      int common;
      LANG_RT_COMMON_COLL (key[0]->collation, key[1]->collation, common);
      result->collation = common;
    }
  else
    {
      result->collation = -1;
    }

  /* develop checks the collations of the coerced values: a conversion implicit coercion refuses fails before that,
   * into a string type too (its outcome, not -1150) */
  bool refused = false;
  for (int side = 0; side < 2; side++)
    {
      refused = refused || (result->conv[side] != NULL
			    && domain_implicit_coercion_refused (key[side]->type,
								 TP_DOMAIN_TYPE (result->target[side])));
    }
  if (result->collation == -1 && !refused)
    {
      result->kernel = DOMAIN_COMPARE_COLLATIONS;
    }
  else if (result->conv[0] != NULL || result->conv[1] != NULL || result->codeset_side >= 0)
    {
      result->kernel = DOMAIN_COMPARE_CONVERT;
    }
  return NO_ERROR;
}

static_assert (DOMAIN_ELEMENT_COLLATIONS == LANG_MAX_COLLATIONS, "element table collations");

/* A type a collection element can have as a value: NULL answers before any comparison, and the national character
 * types have no values (the parser takes NCHAR for CHAR). */
static bool
domain_element_type (int type)
{
  switch (type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_ELO:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_VOBJ:
    case DB_TYPE_OID:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
    case DB_TYPE_CHAR:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DATETIME:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_ENUMERATION:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_JSON:
      return true;
    default:
      return false;
    }
}

/* A collation a value can carry: a registered one (an id nothing registered holds ISO binary's placeholder). */
static bool
domain_collation_registered (int collation)
{
  const LANG_COLLATION *lang_coll = lang_get_collation (collation);
  return lang_coll != NULL && lang_coll->coll.coll_id == collation;
}

/* The element types a table covers and the collations its string and ENUM entries go by (#352). */
struct DOMAIN_ELEMENT_LAYOUT
{
  bool type[DOMAIN_ELEMENT_TYPES];
  short ordinal[DOMAIN_ELEMENT_COLLATIONS];
  int collation_of[DOMAIN_ELEMENT_COLLATIONS];	/* an ordinal's collation: a representative one when one entry serves
						 * every collation */
  int n_collations;
  int n_entries;
};

static void
domain_element_layout (const DOMAIN_COMPARE_KEY * item, const DOMAIN_COMPARE_KEY * keys, int n_keys,
		       DOMAIN_ELEMENT_LAYOUT * layout)
{
  layout->n_collations = 0;
  layout->n_entries = 0;
  for (int c = 0; c < DOMAIN_ELEMENT_COLLATIONS; c++)
    {
      layout->ordinal[c] = -1;
    }
  for (int t = 0; t < DOMAIN_ELEMENT_TYPES; t++)
    {
      layout->type[t] = keys == NULL && domain_element_type (t);
    }
  for (int i = 0; keys != NULL && i < n_keys; i++)
    {
      if (domain_element_type (keys[i].type))
	{
	  layout->type[keys[i].type] = true;
	  /* the server holds an object as its OID */
	  layout->type[DB_TYPE_OID] = layout->type[DB_TYPE_OID] || keys[i].type == DB_TYPE_OBJECT;
	}
    }
  if (!TP_TYPE_HAS_COLLATION (item->type))
    {
      /* an element's collation changes nothing in its comparison with this item: one entry serves every collation */
      for (int c = 0; c < DOMAIN_ELEMENT_COLLATIONS; c++)
	{
	  if (domain_collation_registered (c))
	    {
	      layout->ordinal[c] = 0;
	      if (layout->n_collations == 0)
		{
		  layout->collation_of[layout->n_collations++] = c;
		}
	    }
	}
    }
  else
    {
      /* each collation an element can have: the keys', or every registered one */
      for (int i = 0; i < (keys == NULL ? DOMAIN_ELEMENT_COLLATIONS : n_keys); i++)
	{
	  const int c = keys == NULL ? i : TP_TYPE_HAS_COLLATION (keys[i].type) ? keys[i].collation : -1;
	  if (c >= 0 && c < DOMAIN_ELEMENT_COLLATIONS && layout->ordinal[c] < 0
	      && (keys != NULL || domain_collation_registered (c)))
	    {
	      layout->ordinal[c] = (short) layout->n_collations;
	      layout->collation_of[layout->n_collations++] = c;
	    }
	}
    }
  for (int t = 0; t < DOMAIN_ELEMENT_TYPES; t++)
    {
      if (layout->type[t])
	{
	  layout->n_entries += TP_TYPE_HAS_COLLATION (t) ? layout->n_collations : 1;
	}
    }
}

size_t
domain_element_table_bytes (const DOMAIN_COMPARE_KEY * item, const DOMAIN_COMPARE_KEY * keys, int n_keys)
{
  DOMAIN_ELEMENT_LAYOUT layout;
  domain_element_layout (item, keys, n_keys, &layout);
  const size_t n_entries = layout.n_entries > 0 ? layout.n_entries : 1;
  return offsetof (DOMAIN_ELEMENT_TABLE, entry) + sizeof (DOMAIN_COMPARE) * n_entries;
}

/*
 * domain_resolve_element_table () - the comparisons of an item of key `item` (the left side) against every element
 *   key a collection can hold (the right side), decided now (#352, D-352-03)
 *   return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY when a target domain cannot be cached
 *   keys(in): the keys the collection's elements can have (its element domains', a set function's operands'); NULL:
 *	       any key
 *   table(out): a block of `bytes`, domain_element_table_bytes of the same keys
 *
 * A string or ENUM type has an entry per collation the table covers - the keys' collations, or every registered one
 * when any key can come - and a single entry when the item's comparison does not depend on it.
 */
int
domain_resolve_element_table (const DOMAIN_COMPARE_KEY * item, const DOMAIN_COMPARE_KEY * keys, int n_keys,
			      DOMAIN_ELEMENT_TABLE * table, size_t bytes)
{
  DOMAIN_ELEMENT_LAYOUT layout;
  domain_element_layout (item, keys, n_keys, &layout);
  assert (bytes == domain_element_table_bytes (item, keys, n_keys));
  table->bytes = (int) bytes;
  table->n_entries = layout.n_entries;
  memcpy (table->ordinal, layout.ordinal, sizeof (table->ordinal));
  int next = 0;
  for (int t = 0; t < DOMAIN_ELEMENT_TYPES; t++)
    {
      table->first[t] = -1;
      const int n = !layout.type[t] ? 0 : TP_TYPE_HAS_COLLATION (t) ? layout.n_collations : 1;
      if (n == 0)
	{
	  continue;
	}
      table->first[t] = (short) next;
      for (int o = 0; o < n; o++)
	{
	  DOMAIN_COMPARE_KEY element = { (DB_TYPE) t, -1, -1 };
	  if (TP_TYPE_HAS_COLLATION (t))
	    {
	      element.collation = layout.collation_of[o];
	      element.codeset = lang_get_collation (element.collation)->codeset;
	    }
	  const int error = domain_resolve_comparison (item, &element, &table->entry[next++]);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }
  assert (next == layout.n_entries);
  return NO_ERROR;
}

void
domain_resolve_comparison_uncoerced (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs,
				     DOMAIN_COMPARE * result)
{
  *result = DOMAIN_COMPARE
  {
  };
  result->value[0] = result->value[1] = -1;
  result->codeset_side = -1;
  result->site = -1;
  result->source[0] = (unsigned char) lhs->type;
  result->source[1] = (unsigned char) rhs->type;
  result->coercion = 0;

  if (lhs->type == DB_TYPE_NULL || rhs->type == DB_TYPE_NULL)
    {
      /* its value is NULL, and develop's comparison answers NULL first */
      result->kernel = DOMAIN_COMPARE_VALUES;
      return;
    }
  if (lhs->type == DB_TYPE_OBJECT || rhs->type == DB_TYPE_OBJECT)
    {
      /* an OBJECT's values are OIDs on the server (and OID against OBJECT compares by OID on the client) */
      result->kernel = DOMAIN_COMPARE_OBJECT;
      return;
    }
  if (lhs->type != rhs->type && !(TP_IS_CHAR_TYPE (lhs->type) && TP_IS_CHAR_TYPE (rhs->type)))
    {
      /* od:tp_value_compare_with_error without coercion: types that do not compare as they are answer by their rank */
      result->kernel = DOMAIN_COMPARE_RANK;
      result->rank = (signed char) (tp_more_general_type (lhs->type, rhs->type) > 0 ? DB_GT : DB_LT);
      return;
    }
  /* the first value's cmpval under the collation of the tail, as with coercion when nothing is converted */
  result->kernel = DOMAIN_COMPARE_DIRECT;
  result->cmp = pr_type_from_id (lhs->type);
  if (!TP_IS_CHAR_TYPE (lhs->type))
    {
      result->collation = 0;
    }
  else if (lhs->collation == rhs->collation)
    {
      result->collation = lhs->collation;
    }
  else if (lhs->codeset == rhs->codeset)
    {
      int common;
      LANG_RT_COMMON_COLL (lhs->collation, rhs->collation, common);
      result->collation = common;
    }
  else
    {
      result->collation = -1;
    }
  if (result->collation == -1)
    {
      result->kernel = DOMAIN_COMPARE_COLLATIONS;
    }
}

/*
 * The key pair table (#354, D-354-01): the comparison of every pair of keys a value can have - an element type
 * (domain_element_type) and, for a string or an ENUM, a registered collation - with coercion and without. A pair's
 * entry indexes a pool of the distinct decisions: most pairs share one (a string's collation does not change how it
 * compares with a number). Its entries depend on no value and no plan, so it is made once, the first time a comparison
 * needs it, and freed before the type module whose cached domains its string targets are (domain_key_pairs_final).
 */
struct DOMAIN_KEY_PAIRS
{
  short first[DOMAIN_ELEMENT_TYPES];	/* a type's key, a string or ENUM type's first; -1: no element type */
  short ordinal[DOMAIN_ELEMENT_COLLATIONS];	/* a registered collation's key among its type's; -1 */
  int n_keys;
  int *entry[2];		/* [coercion][key1 * n_keys + key2]: the pair's decision in pool; the diagonal is unused */
  DOMAIN_COMPARE *pool;
  int n_pool;
};

/* *INDENT-OFF* */
static std::atomic<DOMAIN_KEY_PAIRS *> domain_Key_pairs (NULL);
static std::mutex domain_Key_pairs_mutex;
/* *INDENT-ON* */

static bool
domain_compare_equal (const DOMAIN_COMPARE * a, const DOMAIN_COMPARE * b)
{
  return a->kernel == b->kernel && a->first == b->first && a->source[0] == b->source[0]
    && a->source[1] == b->source[1] && a->converted_first == b->converted_first && a->failed == b->failed
    && a->reason == b->reason && a->coercion == b->coercion && a->rank == b->rank && a->collation == b->collation
    && a->value[0] == b->value[0] && a->value[1] == b->value[1] && a->codeset_side == b->codeset_side
    && a->site == b->site && a->cmp == b->cmp && a->conv[0] == b->conv[0] && a->conv[1] == b->conv[1]
    && a->target[0] == b->target[0] && a->target[1] == b->target[1] && a->volatile_reads == b->volatile_reads;
}

static void
domain_key_pairs_free (DOMAIN_KEY_PAIRS * pairs)
{
  if (pairs != NULL)
    {
      free (pairs->entry[0]);
      free (pairs->entry[1]);
      free (pairs->pool);
      free (pairs);
    }
}

/* The table itself: NULL when it cannot be made (no memory). */
static DOMAIN_KEY_PAIRS *
domain_key_pairs_make (void)
{
  DOMAIN_KEY_PAIRS *pairs = (DOMAIN_KEY_PAIRS *) calloc (1, sizeof (*pairs));
  if (pairs == NULL)
    {
      return NULL;
    }
  int collation_of[DOMAIN_ELEMENT_COLLATIONS];
  int n_collations = 0;
  for (int c = 0; c < DOMAIN_ELEMENT_COLLATIONS; c++)
    {
      pairs->ordinal[c] = -1;
      if (domain_collation_registered (c))
	{
	  pairs->ordinal[c] = (short) n_collations;
	  collation_of[n_collations++] = c;
	}
    }
  for (int t = 0; t < DOMAIN_ELEMENT_TYPES; t++)
    {
      pairs->first[t] = -1;
      if (domain_element_type (t))
	{
	  pairs->first[t] = (short) pairs->n_keys;
	  pairs->n_keys += TP_TYPE_HAS_COLLATION (t) ? n_collations : 1;
	}
    }
  const int n = pairs->n_keys;
  DOMAIN_COMPARE_KEY *keys = (DOMAIN_COMPARE_KEY *) malloc (sizeof (*keys) * n);
  pairs->entry[0] = (int *) malloc (sizeof (int) * n * n);
  pairs->entry[1] = (int *) malloc (sizeof (int) * n * n);
  pairs->pool = (DOMAIN_COMPARE *) malloc (sizeof (DOMAIN_COMPARE) * 2 * n * n);
  bool ok = keys != NULL && pairs->entry[0] != NULL && pairs->entry[1] != NULL && pairs->pool != NULL;
  for (int t = 0; ok && t < DOMAIN_ELEMENT_TYPES; t++)
    {
      for (int o = 0; pairs->first[t] >= 0 && o < (TP_TYPE_HAS_COLLATION (t) ? n_collations : 1); o++)
	{
	  DOMAIN_COMPARE_KEY *key = &keys[pairs->first[t] + o];
	  key->type = (DB_TYPE) t;
	  key->codeset = key->collation = -1;
	  if (TP_TYPE_HAS_COLLATION (t))
	    {
	      key->collation = collation_of[o];
	      key->codeset = lang_get_collation (key->collation)->codeset;
	    }
	}
    }
  /* type pair by type pair, so the decisions of one pair are contiguous in the pool and a key pair looks for its
   * decision only among its type pair's */
  for (int mode = 0; ok && mode < 2; mode++)
    {
      for (int t1 = 0; ok && t1 < DOMAIN_ELEMENT_TYPES; t1++)
	{
	  for (int t2 = 0; ok && pairs->first[t1] >= 0 && t2 < DOMAIN_ELEMENT_TYPES; t2++)
	    {
	      if (pairs->first[t2] < 0)
		{
		  continue;
		}
	      const int pair_first = pairs->n_pool;
	      const int n1 = TP_TYPE_HAS_COLLATION (t1) ? n_collations : 1;
	      const int n2 = TP_TYPE_HAS_COLLATION (t2) ? n_collations : 1;
	      for (int i = pairs->first[t1]; ok && i < pairs->first[t1] + n1; i++)
		{
		  for (int j = pairs->first[t2]; ok && j < pairs->first[t2] + n2; j++)
		    {
		      int *entry = &pairs->entry[mode][i * n + j];
		      *entry = -1;
		      if (i == j)
			{
			  /* one key: the comparison's own reading (domain_compare_by_keys) */
			  continue;
			}
		      DOMAIN_COMPARE decision;
		      if (mode == 1)
			{
			  ok = domain_resolve_comparison (&keys[i], &keys[j], &decision) == NO_ERROR;
			}
		      else
			{
			  domain_resolve_comparison_uncoerced (&keys[i], &keys[j], &decision);
			}
		      for (int p = pair_first; ok && p < pairs->n_pool && *entry < 0; p++)
			{
			  if (domain_compare_equal (&pairs->pool[p], &decision))
			    {
			      *entry = p;
			    }
			}
		      if (ok && *entry < 0)
			{
			  pairs->pool[pairs->n_pool] = decision;
			  *entry = pairs->n_pool++;
			}
		    }
		}
	    }
	}
    }
  free (keys);
  if (!ok)
    {
      domain_key_pairs_free (pairs);
      return NULL;
    }
  DOMAIN_COMPARE *pool = (DOMAIN_COMPARE *) realloc (pairs->pool, sizeof (DOMAIN_COMPARE) * pairs->n_pool);
  if (pool != NULL)
    {
      pairs->pool = pool;
    }
  return pairs;
}

/* The table, made the first time a comparison needs it. */
static const DOMAIN_KEY_PAIRS *
domain_key_pairs (void)
{
  DOMAIN_KEY_PAIRS *pairs = domain_Key_pairs.load (std::memory_order_acquire);
  if (pairs == NULL)
    {
      std::lock_guard < std::mutex > lock (domain_Key_pairs_mutex);
      pairs = domain_Key_pairs.load (std::memory_order_relaxed);
      if (pairs == NULL)
	{
	  pairs = domain_key_pairs_make ();
	  domain_Key_pairs.store (pairs, std::memory_order_release);
	}
    }
  return pairs;
}

void
domain_key_pairs_final (void)
{
  std::lock_guard < std::mutex > lock (domain_Key_pairs_mutex);
  domain_key_pairs_free (domain_Key_pairs.exchange (NULL));
}

/* A key's row and column in the table; -1 for a key no value of an element type carries. */
static inline int
domain_key_pair_index (const DOMAIN_KEY_PAIRS * pairs, const DOMAIN_COMPARE_KEY * key)
{
  const int first = key->type >= 0 && key->type < DOMAIN_ELEMENT_TYPES ? pairs->first[key->type] : -1;
  if (first < 0 || !TP_TYPE_HAS_COLLATION (key->type))
    {
      return first;
    }
  const int ordinal = key->collation >= 0 && key->collation < DOMAIN_ELEMENT_COLLATIONS
    ? pairs->ordinal[key->collation] : -1;
  return ordinal >= 0 ? first + ordinal : -1;
}

DB_VALUE_COMPARE_RESULT
domain_compare_by_keys (const DB_VALUE * value1, const DB_VALUE * value2, int do_coercion, int total_order,
			bool * can_compare)
{
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value1);
  if (type == DB_VALUE_DOMAIN_TYPE (value2) && !TP_TYPE_HAS_COLLATION (type))
    {
      /* one type without a collation, a homogeneous collection's elements: develop compares them as they are, and
       * decides nothing */
      return tp_value_compare_with_error (value1, value2, do_coercion, total_order, can_compare);
    }
  if (can_compare != NULL)
    {
      *can_compare = true;
    }
  if (DB_IS_NULL (value1))
    {
      return DB_IS_NULL (value2) ? (total_order ? DB_EQ : DB_UNK) : (total_order ? DB_LT : DB_UNK);
    }
  if (DB_IS_NULL (value2))
    {
      return total_order ? DB_GT : DB_UNK;
    }
  DOMAIN_COMPARE_KEY key[2];
  domain_compare_key_of_value (value1, &key[0]);
  domain_compare_key_of_value (value2, &key[1]);
  if (key[0].type == key[1].type && key[0].collation == key[1].collation)
    {
      /* one type and one collation: develop compares them as they are, and decides nothing */
      return tp_value_compare_with_error (value1, value2, do_coercion, total_order, can_compare);
    }
  const DOMAIN_KEY_PAIRS *pairs = domain_key_pairs ();
  const int row = pairs != NULL ? domain_key_pair_index (pairs, &key[0]) : -1;
  const int column = pairs != NULL ? domain_key_pair_index (pairs, &key[1]) : -1;
  if (row < 0 || column < 0)
    {
      /* every value of an element type has a key the table covers: the table is missing only when it could not be
       * made (no memory), and develop's comparison answers, counted */
      assert (pairs == NULL);
      return tp_value_compare_with_error (value1, value2, do_coercion, total_order, can_compare);
    }
  const DOMAIN_COMPARE *compare = &pairs->pool[pairs->entry[do_coercion ? 1 : 0][row * pairs->n_keys + column]];
  if (compare->kernel == DOMAIN_COMPARE_OBJECT)
    {
      /* an object side: develop's comparison, which meets OIDs on the server */
      return tp_value_compare_with_error (value1, value2, do_coercion, total_order, can_compare);
    }
  const DB_VALUE_COMPARE_RESULT result =
    domain_compare_values (thread_get_thread_entry_info (), compare, value1, value2, total_order, can_compare);
#if !defined (NDEBUG)
  {
    /* shadow check (optdebug): develop's comparison of the same values, uncounted, gives the table's result,
     * comparability and error */
    const bool comparable = can_compare != NULL ? *can_compare : true;
    const int error = comparable ? NO_ERROR : er_errid ();
    bool develop_comparable = true;
    bool *const develop_comparable_p = can_compare != NULL ? &develop_comparable : NULL;
    er_stack_push ();
    const DB_VALUE_COMPARE_RESULT develop =
      tp_value_compare_uncounted (value1, value2, do_coercion, total_order, develop_comparable_p);
    const int develop_error = develop_comparable ? NO_ERROR : er_errid ();
    er_stack_pop ();
    if (develop != result || develop_comparable != comparable || develop_error != error)
      {
	fprintf (stderr, "key pair comparison: types %d/%d collations %d/%d coercion=%d kernel=%d result=%d/%d "
		 "comparable=%d/%d error=%d/%d\n", (int) key[0].type, (int) key[1].type, key[0].collation,
		 key[1].collation, do_coercion, (int) compare->kernel, (int) result, (int) develop, (int) comparable,
		 (int) develop_comparable, error, develop_error);
      }
    assert (develop == result && develop_comparable == comparable && develop_error == error);
  }
#endif
  return result;
}

int
domain_resolve (DOMAIN_CTX context, int opcode, const DOMAIN_OPERAND * operands, int n_operands,
		const TP_DOMAIN * consumer_domain, RESOLVED_DOMAIN * result, bool * needs_gate)
{
  assert (operands != NULL && n_operands > 0 && result != NULL && needs_gate != NULL);

  *needs_gate = false;
  for (int i = 0; i < n_operands; i++)
    {
      if (operands[i].val_type == DB_TYPE_NULL
	  && (operands[i].domain == NULL || TP_DOMAIN_TYPE (operands[i].domain) == DB_TYPE_VARIABLE))
	{
	  *needs_gate = true;
	  return NO_ERROR;
	}
    }

  *result = RESOLVED_DOMAIN
  {
  };

  switch (context)
    {
    case DOMAIN_CTX_ARITH:
      return domain_resolve_arith (opcode, operands, n_operands, result);

    case DOMAIN_CTX_COMPARE:
      assert (n_operands == 2);
      return domain_resolve_compare (operands, result);

    case DOMAIN_CTX_COMMON_VALUE:
      return domain_resolve_common_value (operands, n_operands, result);

    case DOMAIN_CTX_AGG:
      return domain_resolve_aggregate (opcode, consumer_domain, &operands[0], result);

    case DOMAIN_CTX_ANALYTIC:
      return domain_resolve_analytic (opcode, consumer_domain, &operands[0], result);

    case DOMAIN_CTX_FUNC_ARG:
      return domain_resolve_function (opcode, operands, n_operands, consumer_domain, result);

    case DOMAIN_CTX_ASSIGN:
    case DOMAIN_CTX_KEY_ELEM:
      /* the consumer (assignment target, index element) is the target */
      assert (consumer_domain != NULL);
      result->domain = result->operand_domain[0] = consumer_domain;
      result->conv[0] = domain_lookup_converter (domain_operand_type (&operands[0]), consumer_domain, context);
      return NO_ERROR;

    case DOMAIN_CTX_LIST_COLUMN:
      return domain_resolve_list_column (operands, n_operands, result);
    }

  assert (false);
  return ER_FAILED;
}

/* MEDIAN/PERCENTILE: DOUBLE, then DATETIME, then TIME, as tp_value_cast (…, false) would (qx:21713~21735). */
static DB_TYPE
domain_classify_interpolation (const DB_VALUE * value)
{
  static const DB_TYPE candidates[] = { DB_TYPE_DOUBLE, DB_TYPE_DATETIME, DB_TYPE_TIME };
  DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);

  if (domain_is_interpolation_type (type))
    {
      return type;
    }
for (DB_TYPE candidate:candidates)
    {
      const TP_DOMAIN *target = tp_domain_resolve_default (candidate);
      DOMAIN_CONVERTER converter = domain_lookup_converter (type, target, DOMAIN_CONVERT_ASSIGN);
      if (converter == NULL)
	{
	  return candidate;
	}
      DB_VALUE converted;
      db_value_domain_init (&converted, candidate, target->precision, target->scale);
      TP_DOMAIN_STATUS status = converter (value, &converted, target);
      pr_clear_value (&converted);
      if (status == DOMAIN_COMPATIBLE)
	{
	  return candidate;
	}
    }
  return DB_TYPE_NULL;
}

/* ADDTIME left string: DATETIMETZ with a zone, VARCHAR otherwise; not a time/date string → DB_TYPE_NULL. */
static DB_TYPE
domain_classify_addtime (const DB_VALUE * value)
{
  if (!TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (value)))
    {
      return DB_VALUE_DOMAIN_TYPE (value);
    }

  const char *str = db_get_string (value);
  int size = db_get_string_size (value);
  date_conversion_error date_error;
  DB_DATETIMETZ datetimetz;
  bool has_zone = false;

  if (db_string_to_datetimetz_ex_core (str, size, &datetimetz, &has_zone, &date_error) == NO_ERROR && has_zone)
    {
      return DB_TYPE_DATETIMETZ;
    }

  DB_TIME time;
  int millisecond;
  if (db_date_parse_time_core (str, size, &time, &millisecond, &date_error) == NO_ERROR)
    {
      return DB_TYPE_VARCHAR;
    }

  DB_DATETIME datetime;
  bool has_explicit_time = false;
  if (db_date_parse_datetime_parts_core (str, size, &datetime, &has_explicit_time, NULL, NULL, NULL,
					 &date_error) != NO_ERROR)
    {
      return DB_TYPE_NULL;
    }
  return has_zone ? DB_TYPE_DATETIMETZ : DB_TYPE_VARCHAR;
}

/* STR_TO_DATE format: the specifiers left after removing white space decide the result type. */
static DB_TYPE
domain_classify_str_to_date_format (const DB_VALUE * value)
{
  if (!TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (value)))
    {
      return DB_TYPE_NULL;
    }

  const char *format = db_get_string (value);
  int length = db_get_string_size (value);
  length = length < 0 ? (int) strlen (format) : length;

  char *compact = (char *) db_private_alloc (NULL, length + 1);
  if (compact == NULL)
    {
      return DB_TYPE_NULL;
    }
  int k = 0;
  bool is_valid = true;
  for (int i = 0; i < length && is_valid; i++)
    {
      if (!char_isspace2 (format[i]))
	{
	  compact[k++] = format[i];
	}
      else if (i > 0 && format[i - 1] == '%')
	{
	  /* '%' without format specifier */
	  is_valid = false;
	}
    }
  compact[k] = '\0';

  DB_TYPE type = DB_TYPE_NULL;
  if (is_valid)
    {
      switch (db_check_time_date_format (compact))
	{
	case TIME_SPECIFIER:
	  type = DB_TYPE_TIME;
	  break;
	case DATE_SPECIFIER:
	  type = DB_TYPE_DATE;
	  break;
	case DATETIME_SPECIFIER:
	  type = DB_TYPE_DATETIME;
	  break;
	case DATETIMETZ_SPECIFIER:
	  type = DB_TYPE_DATETIMETZ;
	  break;
	default:
	  break;
	}
    }
  db_private_free (NULL, compact);
  return type;
}

DB_TYPE
domain_classify_value (DOMAIN_CTX context, int opcode, int arg_index, const DB_VALUE * value)
{
  assert (value != NULL && !DB_IS_NULL (value));

  if ((context == DOMAIN_CTX_AGG || context == DOMAIN_CTX_ANALYTIC) && arg_index == 0
      && (opcode == PT_MEDIAN || opcode == PT_PERCENTILE_CONT || opcode == PT_PERCENTILE_DISC))
    {
      /* an analytic interpolation function classifies its first value as the aggregate does (qn:807, #337) */
      return domain_classify_interpolation (value);
    }
  if (context == DOMAIN_CTX_FUNC_ARG && opcode == T_ADDTIME && arg_index == 0)
    {
      return domain_classify_addtime (value);
    }
  if (context == DOMAIN_CTX_FUNC_ARG && opcode == T_STR_TO_DATE && arg_index == 1)
    {
      return domain_classify_str_to_date_format (value);
    }
  return DB_VALUE_DOMAIN_TYPE (value);
}

/*
 * #338: the character results of compiled nodes whose collation the compiler left to the values (LEAVE) or enforced
 * over an operand it could not type (ENFORCE). Each rule is what the operator gives its value at execution today
 * (string_opfunc.c, tp_value_cast_internal); the gate applies it once to the operands' decided domains, as
 * tp_domain_resolve_value would read the value (D-338-01).
 */

/* A variable string or bit string at floating precision reads as its maximum (tp_domain_resolve_value so:3313). */
static const TP_DOMAIN *
domain_variable_string_value (const TP_DOMAIN * domain)
{
  if (domain == NULL)
    {
      return NULL;
    }
  const bool floating = domain->precision == 0 || domain->precision == TP_FLOATING_PRECISION_VALUE;
  if (TP_DOMAIN_TYPE (domain) == DB_TYPE_VARCHAR && (floating || domain->precision > DB_MAX_VARCHAR_PRECISION))
    {
      return tp_domain_resolve (DB_TYPE_VARCHAR, NULL, DB_MAX_VARCHAR_PRECISION, 0, NULL, domain->collation_id);
    }
  if (TP_DOMAIN_TYPE (domain) == DB_TYPE_VARBIT && (floating || domain->precision > DB_MAX_VARBIT_PRECISION))
    {
      return tp_domain_resolve (DB_TYPE_VARBIT, NULL, DB_MAX_VARBIT_PRECISION, 0, NULL, 0);
    }
  return domain;
}

const TP_DOMAIN *
domain_as_value_domain (const TP_DOMAIN * domain)
{
  if (domain != NULL && TP_DOMAIN_TYPE (domain) == DB_TYPE_ENUMERATION)
    {
      /* a value carries no element list (tp_domain_resolve_value so:3335) */
      return tp_domain_resolve_default (DB_TYPE_ENUMERATION);
    }
  return domain_variable_string_value (domain);
}

/* Where an operator's result takes its collation (and codeset) from. */
enum DOMAIN_CHAR_SOURCE
{
  DOMAIN_CHAR_MERGE,		/* LANG_RT_COMMON_COLL over every character operand in operand order
				 * (db_string_concatenate so:1194, db_string_pad, db_string_replace) */
  DOMAIN_CHAR_FIRST,		/* the first character operand: the string being cut, cased, trimmed, reversed,
				 * translated, repeated, hashed or bounded keeps its codeset and collation */
  DOMAIN_CHAR_FORMAT,		/* the format argument, the second operand (db_date_format, db_time_format) */
  DOMAIN_CHAR_SYSTEM,		/* LANG_SYS: a string the operator makes itself (db_make_string, LANG_COERCIBLE_COLL) */
  DOMAIN_CHAR_BRANCH,		/* one operand's value, chosen per row (IF, CASE, DECODE, ELT) */
  DOMAIN_CHAR_FIRST_BINARY	/* the binary collation of the first character operand's codeset (db_from_unixtime) */
};

/* The precision the value takes. */
enum DOMAIN_CHAR_PRECISION
{
  DOMAIN_PREC_FLOATING,		/* the value's own length: floating (D-338-03) */
  DOMAIN_PREC_SOURCE,		/* the first character operand's (db_string_substring, db_string_trim,
				 * db_string_reverse) */
  DOMAIN_PREC_SUM,		/* the character operands' added, floating if one is (db_string_concatenate so:1262) */
  DOMAIN_PREC_COMPILED,		/* the compiled one (MD5 and SHA1 give CHAR of the digest length, UUID_FORMAT 36) */
  DOMAIN_PREC_CHAR_SOURCE	/* a fixed CHAR source's, else floating */
};

struct DOMAIN_CHAR_RULE
{
  DB_TYPE type;			/* DB_TYPE_NULL: the first character operand's type (UPPER, LOWER keep CHAR) */
  unsigned char source;		/* DOMAIN_CHAR_SOURCE */
  unsigned char precision;	/* DOMAIN_CHAR_PRECISION */
};

/* The rule of a character operator; an operator not listed makes a string from its character operands, merged. */
static DOMAIN_CHAR_RULE
domain_character_rule (int opcode)
{
  switch (opcode)
    {
    case T_CONCAT:
    case T_STRCAT:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_MERGE, DOMAIN_PREC_SUM };
    case T_SUBSTRING:
    case T_MID:
    case T_LEFT:
    case T_RIGHT:
    case T_TRIM:
    case T_LTRIM:
    case T_RTRIM:
    case T_REVERSE:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_FIRST, DOMAIN_PREC_SOURCE };
    case T_UPPER:
    case T_LOWER:
      /* db_string_upper/lower keep the string's type; a fixed CHAR keeps its length (the count of the cased
       * characters), anything else its own length */
      return DOMAIN_CHAR_RULE { DB_TYPE_NULL, DOMAIN_CHAR_FIRST, DOMAIN_PREC_CHAR_SOURCE };
    case T_TRANSLATE:
      /* db_string_translate makes the result in the source string's codeset and collation */
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_FIRST, DOMAIN_PREC_FLOATING };
    case T_UUID_FORMAT:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_FIRST, DOMAIN_PREC_COMPILED };
    case T_REPEAT:
    case T_LIKE_LOWER_BOUND:
    case T_LIKE_UPPER_BOUND:
    case T_SHA_TWO:
    case T_AES_ENCRYPT:
    case T_AES_DECRYPT:
    case T_TO_BASE64:
    case T_FROM_BASE64:
    case F_REGEXP_REPLACE:
    case F_REGEXP_SUBSTR:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_FIRST, DOMAIN_PREC_FLOATING };
    case T_MD5:
    case T_SHA_ONE:
      return DOMAIN_CHAR_RULE { DB_TYPE_CHAR, DOMAIN_CHAR_FIRST, DOMAIN_PREC_COMPILED };
    case T_DATE_FORMAT:
    case T_TIME_FORMAT:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_FORMAT, DOMAIN_PREC_FLOATING };
    case T_SPACE:
    case T_TZ_OFFSET:
    case T_DATE_ADD:
    case T_DATE_SUB:
    case T_ADDDATE:
    case T_SUBDATE:
    case T_DATE:
    case T_TIME:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_SYSTEM, DOMAIN_PREC_FLOATING };
    case T_FROM_UNIXTIME:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_FIRST_BINARY, DOMAIN_PREC_FLOATING };
    case T_IF:
    case T_CASE:
    case T_DECODE:
    case T_PRIOR:
    case T_CONNECT_BY_ROOT:
    case T_QPRIOR:
    case F_ELT:
      return DOMAIN_CHAR_RULE { DB_TYPE_NULL, DOMAIN_CHAR_BRANCH, DOMAIN_PREC_SOURCE };
    default:
      return DOMAIN_CHAR_RULE { DB_TYPE_VARCHAR, DOMAIN_CHAR_MERGE, DOMAIN_PREC_FLOATING };
    }
}

/* The character operands' collation, merged as the string operators merge their argument values: operands without
 * a collation (NULL, a number, a date) take no part. return: false when two do not merge; *collation_id is -1 when no
 * operand has a collation. */
static bool
domain_merge_collations (const DOMAIN_OPERAND * operands, int n_operands, int *collation_id)
{
  int merged = -1;
  for (int i = 0; i < n_operands; i++)
    {
      const TP_DOMAIN *domain = domain_operand_domain (&operands[i]);
      if (domain == NULL || !TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (domain)))
	{
	  continue;
	}
      int common = domain->collation_id;
      if (merged >= 0)
	{
	  LANG_RT_COMMON_COLL (merged, domain->collation_id, common);
	  if (common == -1)
	    {
	      return false;
	    }
	}
      merged = common;
    }
  *collation_id = merged;
  return true;
}

/* The first operand that has a collation, or NULL. */
static const TP_DOMAIN *
domain_first_character_operand (const DOMAIN_OPERAND * operands, int n_operands)
{
  for (int i = 0; i < n_operands; i++)
    {
      const TP_DOMAIN *domain = domain_operand_domain (&operands[i]);
      if (domain != NULL && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (domain)))
	{
	  return domain;
	}
    }
  return NULL;
}

/* CAST, CAST_WRAP, CAST_NOFAIL to a character target: tp_value_cast_internal (od:29018). ENFORCE keeps a character
 * source's type and precision under the target's collation, and leaves any other value as it is; LEAVE makes the
 * target type and precision under a character source's collation, or the target's own for any other source. */
static const TP_DOMAIN *
domain_character_cast (const DOMAIN_OPERAND * source, const TP_DOMAIN * compiled)
{
  const TP_DOMAIN *from = domain_operand_domain (source);
  const bool char_source = from != NULL && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (from));
  if (TP_DOMAIN_COLLATION_FLAG (compiled) == TP_DOMAIN_COLL_ENFORCE)
    {
      if (!char_source)
	{
	  return from;
	}
      if (from->codeset != compiled->codeset)
	{
	  /* a string recoded into the target codeset comes out in the target type (CTP _07_session_var: an
	   * iso88591 or binary CHAR bind cast to a utf8 VARCHAR) */
	  return tp_domain_resolve (TP_DOMAIN_TYPE (compiled), NULL, compiled->precision, 0, NULL,
				    compiled->collation_id);
	}
      return tp_domain_resolve (TP_DOMAIN_TYPE (from), NULL, from->precision, 0, NULL, compiled->collation_id);
    }
  return tp_domain_resolve (TP_DOMAIN_TYPE (compiled), NULL, compiled->precision, compiled->scale, NULL,
			    char_source ? from->collation_id : compiled->collation_id);
}

/* The character result of opcode over the operands' decided domains; compiled is the node's compiled domain (NULL
 * for a gate-dependent node: the rule's type and precision). */
static int
domain_character_result (int opcode, const DOMAIN_OPERAND * operands, int n_operands, const TP_DOMAIN * compiled,
			 RESOLVED_DOMAIN * result)
{
  const TP_DOMAIN *domain = NULL;
  if (compiled != NULL && (opcode == T_CAST || opcode == T_CAST_WRAP || opcode == T_CAST_NOFAIL))
    {
      domain = n_operands > 0 ? domain_character_cast (&operands[n_operands - 1], compiled) : NULL;
    }
  else if (compiled != NULL && opcode == T_TO_CHAR)
    {
      /* db_to_char (so:12608) over the node's compiled domain: a string is coerced to it, keeping its collation
       * (LEAVE); a number or a date prints into the compiled domain's codeset and collation */
      const TP_DOMAIN *value = n_operands > 0 ? domain_operand_domain (&operands[0]) : NULL;
      if (value != NULL && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (value)))
	{
	  domain = tp_domain_resolve (TP_DOMAIN_TYPE (compiled), NULL, compiled->precision, 0, NULL,
				      value->collation_id);
	}
      else
	{
	  domain = tp_domain_resolve (DB_TYPE_VARCHAR, NULL, DB_MAX_VARCHAR_PRECISION, 0, NULL, compiled->collation_id);
	}
    }
  else if (compiled != NULL && opcode == PT_GROUP_CONCAT)
    {
      /* a reader of a GROUP_CONCAT accumulator (#340): qdata_group_concat_first_value makes the accumulator in its
       * compiled string type under the function domain's codeset and collation, of its own length; a function the
       * gate saw no value for gives no value */
      const TP_DOMAIN *function = n_operands > 0 ? domain_operand_domain (&operands[0]) : NULL;
      if (function == NULL || !TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (function)))
	{
	  result->domain = &tp_Null_domain;
	  return NO_ERROR;
	}
      domain = tp_domain_resolve (TP_DOMAIN_TYPE (compiled), NULL, TP_FLOATING_PRECISION_VALUE, 0, NULL,
				  function->collation_id);
    }
  else
    {
      const DOMAIN_CHAR_RULE rule = domain_character_rule (opcode);
      const TP_DOMAIN *first = domain_first_character_operand (operands, n_operands);
      int collation_id = -1;
      switch (rule.source)
	{
	case DOMAIN_CHAR_MERGE:
	  if (!domain_merge_collations (operands, n_operands, &collation_id))
	    {
	      return ER_QSTR_INCOMPATIBLE_COLLATIONS;
	    }
	  break;
	case DOMAIN_CHAR_FIRST:
	  collation_id = first != NULL ? first->collation_id : -1;
	  break;
	case DOMAIN_CHAR_FORMAT:
	  {
	    const TP_DOMAIN *format = n_operands > 1 ? domain_operand_domain (&operands[1]) : NULL;
	    collation_id = format != NULL && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (format))
	      ? format->collation_id : -1;
	  }
	  break;
	case DOMAIN_CHAR_SYSTEM:
	  collation_id = LANG_SYS_COLLATION;
	  break;
	case DOMAIN_CHAR_FIRST_BINARY:
	  collation_id = first != NULL ? LANG_GET_BINARY_COLLATION (first->codeset) : -1;
	  break;
	case DOMAIN_CHAR_BRANCH:
	  {
	    /* the row takes one branch's value: one domain for every character branch, or the row decides */
	    const TP_DOMAIN *branch = NULL;
	    for (int i = 0; i < n_operands; i++)
	      {
		const TP_DOMAIN *d = domain_as_value_domain (domain_operand_domain (&operands[i]));
		if (d == NULL || !TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (d)))
		  {
		    continue;
		  }
		if (branch != NULL && branch != d)
		  {
		    return ER_QPROC_DOMAIN_UNRESOLVED;
		  }
		branch = d;
	      }
	    if (branch == NULL)
	      {
		result->domain = &tp_Null_domain;
		return NO_ERROR;
	      }
	    result->domain = branch;
	    return NO_ERROR;
	  }
	}
      if (collation_id < 0)
	{
	  /* no operand carries a collation: the value is NULL (the operators return NULL for NULL arguments) */
	  result->domain = &tp_Null_domain;
	  return NO_ERROR;
	}

      DB_TYPE type = rule.type;
      if (type == DB_TYPE_NULL)
	{
	  type = first != NULL && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (first)) ? TP_DOMAIN_TYPE (first) : DB_TYPE_VARCHAR;
	}
      int precision = TP_FLOATING_PRECISION_VALUE;
      switch (rule.precision)
	{
	case DOMAIN_PREC_SOURCE:
	  precision = first != NULL ? first->precision : TP_FLOATING_PRECISION_VALUE;
	  break;
	case DOMAIN_PREC_SUM:
	  precision = 0;
	  for (int i = 0; i < n_operands; i++)
	    {
	      const TP_DOMAIN *d = domain_operand_domain (&operands[i]);
	      if (d == NULL || !TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (d)))
		{
		  continue;
		}
	      if (d->precision == TP_FLOATING_PRECISION_VALUE || precision == TP_FLOATING_PRECISION_VALUE
		  || TP_DOMAIN_TYPE (d) == DB_TYPE_ENUMERATION)
		{
		  precision = TP_FLOATING_PRECISION_VALUE;
		}
	      else
		{
		  precision = (int) MIN ((INT64) DB_MAX_VARCHAR_PRECISION, (INT64) precision + d->precision);
		}
	    }
	  break;
	case DOMAIN_PREC_COMPILED:
	  precision = compiled != NULL ? compiled->precision : TP_FLOATING_PRECISION_VALUE;
	  break;
	case DOMAIN_PREC_CHAR_SOURCE:
	  precision = first != NULL && TP_DOMAIN_TYPE (first) == DB_TYPE_CHAR ? first->precision
	    : TP_FLOATING_PRECISION_VALUE;
	  break;
	default:
	  break;
	}
      if (type == DB_TYPE_CHAR && precision == 0)
	{
	  precision = TP_FLOATING_PRECISION_VALUE;
	}
      domain = tp_domain_resolve (type, NULL, precision, 0, NULL, collation_id);
    }

  if (domain == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  result->domain = domain_as_value_domain (domain);
  return NO_ERROR;
}

int
domain_resolve_character (int opcode, const DOMAIN_OPERAND * operands, int n_operands, const TP_DOMAIN * compiled,
			  RESOLVED_DOMAIN * result)
{
  assert (compiled != NULL && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (compiled)));
  assert (TP_DOMAIN_COLLATION_FLAG (compiled) != TP_DOMAIN_COLL_NORMAL);

  *result = RESOLVED_DOMAIN
  {
  };
  for (int i = 0; i < n_operands && i < 3; i++)
    {
      result->operand_domain[i] = operands[i].domain;
    }
  return domain_character_result (opcode, operands, n_operands, compiled, result);
}

int
domain_resolve_branch_pick (const DOMAIN_OPERAND * operands, int n_operands, int branch, RESOLVED_DOMAIN * result)
{
  *result = RESOLVED_DOMAIN
  {
  };
  const TP_DOMAIN *domain = branch > 0 && branch < n_operands
    ? domain_as_value_domain (domain_operand_domain (&operands[branch])) : NULL;
  /* no branch, or a branch without a string value (a NULL bind): every row gives NULL (qdata_elt) */
  result->domain = domain != NULL && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (domain)) ? domain : &tp_Null_domain;
  return NO_ERROR;
}

int
domain_resolve_branch_merge (const DOMAIN_OPERAND * operands, int n_operands, RESOLVED_DOMAIN * result)
{
  *result = RESOLVED_DOMAIN
  {
  };
  int collation_id = -1;
  if (!domain_merge_collations (operands, n_operands, &collation_id))
    {
      return ER_QSTR_INCOMPATIBLE_COLLATIONS;
    }
  if (collation_id < 0)
    {
      result->domain = &tp_Null_domain;
      return NO_ERROR;
    }
  /* the branches' string type (the compiler casts every branch to it) of the value's own length (D-338-03) */
  const TP_DOMAIN *first = domain_first_character_operand (operands, n_operands);
  const DB_TYPE type = first != NULL && TP_DOMAIN_TYPE (first) == DB_TYPE_CHAR ? DB_TYPE_CHAR : DB_TYPE_VARCHAR;
  const TP_DOMAIN *domain = tp_domain_resolve (type, NULL, TP_FLOATING_PRECISION_VALUE, 0, NULL, collation_id);
  if (domain == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  result->domain = domain_as_value_domain (domain);
  result->conv[0] = domain_lookup_converter (DB_TYPE_VARCHAR, result->domain, DOMAIN_CTX_ASSIGN);
  result->conv[1] = domain_lookup_converter (DB_TYPE_CHAR, result->domain, DOMAIN_CTX_ASSIGN);
  return NO_ERROR;
}
