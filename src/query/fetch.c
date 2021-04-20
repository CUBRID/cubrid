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

#include "error_manager.h"
#include "system_parameter.h"
#include "storage_common.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "arithmetic.h"
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

#include "dbtype.h"

static int fetch_peek_arith (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * obj_oid,
			     QFILE_TUPLE tpl, DB_VALUE ** peek_dbval);
static int fetch_peek_dbval_pos (REGU_VARIABLE * regu_var, QFILE_TUPLE tpl, int pos, DB_VALUE ** peek_dbval,
				 QFILE_TUPLE * next_tpl);
static int fetch_peek_min_max_value_of_width_bucket_func (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
							  val_descr * vd, OID * obj_oid, QFILE_TUPLE tpl,
							  DB_VALUE ** min, DB_VALUE ** max);

static bool is_argument_wrapped_with_cast_op (const REGU_VARIABLE * regu_var);
static int get_hour_minute_or_second (const DB_VALUE * datetime, OPERATOR_TYPE op_type, DB_VALUE * db_value);
static int get_year_month_or_day (const DB_VALUE * src_date, OPERATOR_TYPE op, DB_VALUE * result);
static int get_date_weekday (const DB_VALUE * src_date, OPERATOR_TYPE op, DB_VALUE * result);

/*
 * fetch_peek_arith () -
 *   return: NO_ERROR or ER_code
 *   regu_var(in/out): Regulator Variable of an ARITH node.
 *   vd(in): Value Descriptor
 *   obj_oid(in): Object Identifier
 *   tpl(in): Tuple
 *   peek_dbval(out): Set to the value resulting from the fetch operation
 */
static int
fetch_peek_arith (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * obj_oid, QFILE_TUPLE tpl,
		  DB_VALUE ** peek_dbval)
{
  ARITH_TYPE *arithptr;
  DB_VALUE *peek_left, *peek_right, *peek_third, *peek_fourth;
  DB_VALUE tmp_value;
  TP_DOMAIN *original_domain = NULL;
  TP_DOMAIN_STATUS dom_status;

  assert (regu_var != NULL);
  arithptr = regu_var->value.arithptr;
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST))
    {
      *peek_dbval = arithptr->value;

      return NO_ERROR;
    }

  assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));

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
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (!DB_IS_NULL (peek_left))
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
	      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
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
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
	{
	  goto error;
	}
      /* FALLTHRU */

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
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (DB_IS_NULL (peek_left))
	{
	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING)
	      && (arithptr->opcode == T_STRCAT || arithptr->opcode == T_ADD))
	    {
	      /* check for result type. */
	      if (TP_DOMAIN_TYPE (regu_var->domain) == DB_TYPE_VARIABLE
		  || QSTR_IS_ANY_CHAR_OR_BIT (TP_DOMAIN_TYPE (regu_var->domain)))
		{
		  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
		    {
		      goto error;
		    }
		}
	    }
	}
      else
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_TRIM:
    case T_LTRIM:
    case T_RTRIM:
      /* fetch lhs and rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_FROM_UNIXTIME:

      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      if (arithptr->thirdptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_SUBSTRING_INDEX:
    case T_CONCAT_WS:
    case T_FIELD:
    case T_INDEX_CARDINALITY:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CONV:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LOCATE:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->thirdptr != NULL)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_CONCAT:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (DB_IS_NULL (peek_left))
	{
	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
	    {
	      if (arithptr->rightptr != NULL)
		{
		  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
	      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->leftptr))
	    {
	      /* cast might have failed: set ER_DATE_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	    }
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->leftptr))
	    {
	      /* cast might have failed: set ER_TIME_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	    }
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	{
	  if (is_argument_wrapped_with_cast_op (arithptr->rightptr))
	    {
	      /* cast might have failed: set ER_TIME_CONVERSION */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	    }
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
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
      /* fetch rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_TIMESTAMP:
    case T_LIKE_LOWER_BOUND:
    case T_LIKE_UPPER_BOUND:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (arithptr->rightptr)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
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
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (!DB_IS_NULL (peek_left) && arithptr->rightptr)
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_BIT_TO_BLOB:
    case T_CHAR_TO_CLOB:
    case T_LOB_LENGTH:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_ENUMERATION_VALUE:
      if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_WIDTH_BUCKET:
      if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}

      /* get peek_righ, peed_third we use PT_BETWEEN with PT_BETWEEN_GE_LT to represent the two args. */
      if (fetch_peek_min_max_value_of_width_bucket_func (thread_p, arithptr->rightptr, vd, obj_oid, tpl, &peek_right,
							 &peek_third) != NO_ERROR)
	{
	  goto error;
	}

      if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_fourth) != NO_ERROR)
	{
	  goto error;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto error;
    }

  /* evaluate arithmetic expression */

  /* clear any previous result */
  pr_clear_value (arithptr->value);
  if (regu_var->domain != NULL && TP_DOMAIN_TYPE (regu_var->domain) == DB_TYPE_VARIABLE)
    {
      original_domain = regu_var->domain;
      regu_var->domain = NULL;
    }
  switch (arithptr->opcode)
    {
    case T_ADD:
      {
	bool check_empty_string;

	check_empty_string = (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) ? true : false);

	/* check for result type. */
	if (check_empty_string && regu_var->domain != NULL
	    && QSTR_IS_ANY_CHAR_OR_BIT (TP_DOMAIN_TYPE (regu_var->domain)))
	  {
	    /* at here, T_ADD is really T_STRCAT */
	    if (qdata_strcat_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	      {
		goto error;
	      }
	  }
	else
	  {
	    if (qdata_add_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	      {
		goto error;
	      }
	  }
      }
      break;

    case T_BIT_NOT:
      if (qdata_bit_not_dbval (peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_BIT_AND:
      if (qdata_bit_and_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_BIT_OR:
      if (qdata_bit_or_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_BIT_XOR:
      if (qdata_bit_xor_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_BITSHIFT_LEFT:
    case T_BITSHIFT_RIGHT:
      if (qdata_bit_shift_dbval (peek_left, peek_right, arithptr->opcode, arithptr->value, regu_var->domain) !=
	  NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INTDIV:
    case T_INTMOD:
      if (qdata_divmod_dbval (peek_left, peek_right, arithptr->opcode, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_BIT_COUNT:
      if (db_bit_count_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SUB:
      if (qdata_subtract_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_MUL:
      if (qdata_multiply_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DIV:
      if (qdata_divide_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

#if defined(ENABLE_UNUSED_FUNCTION)
    case T_UNPLUS:
      if (!qdata_copy_db_value (arithptr->value, peek_right))
	{
	  goto error;
	}
      break;
#endif
    case T_UNPLUS:
      break;


    case T_UNMINUS:
      if (qdata_unary_minus_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_PRIOR:
      if (!qdata_copy_db_value (arithptr->value, peek_right))
	{
	  goto error;
	}
      break;

    case T_CONNECT_BY_ROOT:
      if (!qdata_evaluate_connect_by_root (thread_p, (void *) arithptr->thirdptr->xasl, arithptr->rightptr,
					   arithptr->value, vd))
	{
	  goto error;
	}
      break;

    case T_QPRIOR:
      if (!qdata_evaluate_qprior (thread_p, (void *) arithptr->thirdptr->xasl, arithptr->rightptr, arithptr->value, vd))
	{
	  goto error;
	}
      break;

    case T_MOD:
      if (db_mod_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_FLOOR:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_floor_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CEIL:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_ceil_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SIGN:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_sign_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ABS:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_abs_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_EXP:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_exp_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SQRT:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_sqrt_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SIN:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_sin_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_COS:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_cos_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TAN:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_tan_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_COT:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_cot_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LN:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_log_generic_dbval (arithptr->value, peek_right, -1 /* convention for e base */ ) !=
	       NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LOG2:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_log_generic_dbval (arithptr->value, peek_right, 2) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LOG10:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_log_generic_dbval (arithptr->value, peek_right, 10) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ACOS:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_acos_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ASIN:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_asin_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DEGREES:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_degrees_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DATE:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_date_dbval (arithptr->value, peek_right, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TIME:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_time_dbval (arithptr->value, peek_right, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DEFAULT:
      qdata_copy_db_value (arithptr->value, peek_right);
      break;

    case T_RADIANS:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_radians_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_POWER:
      if (db_power_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ROUND:
      if (db_round_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LOG:
      if (db_log_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INCR:
    case T_DECR:
      /* incr/decr is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
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
	  *peek_dbval = peek_left;

	  goto fetch_peek_arith_end;
	}
      break;

    case T_TRUNC:
      if (db_trunc_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CHR:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_chr (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INSTR:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_instr (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_POSITION:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_position (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_FINDINSET:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_find_string_in_in_set (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SUBSTRING:
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
	      if (QSTR_IS_BIT (TP_DOMAIN_TYPE (arithptr->leftptr->domain)))
		{
		  if (db_string_bit_length (peek_left, &tmp_len) != NO_ERROR)
		    {
		      goto error;
		    }
		}
	      else
		{
		  if (db_string_char_length (peek_left, &tmp_len) != NO_ERROR)
		    {
		      goto error;
		    }
		}
	      if (DB_IS_NULL (&tmp_len))
		{
		  goto error;
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

	  if (db_string_substring (arithptr->misc_operand, peek_left, &tmp_arg2, &tmp_arg3, arithptr->value) !=
	      NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  if (db_string_substring (arithptr->misc_operand, peek_left, peek_right, peek_third, arithptr->value) !=
	      NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_OCTET_LENGTH:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  db_make_int (arithptr->value, db_get_string_size (peek_right));
	}
      break;

    case T_BIT_LENGTH:
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
      else
	{
	  /* must be a char string type */
	  db_make_int (arithptr->value, 8 * db_get_string_size (peek_right));
	}
      break;

    case T_CHAR_LENGTH:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  db_make_int (arithptr->value, db_get_string_length (peek_right));
	}
      break;

    case T_LOWER:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_lower (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_UPPER:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_upper (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_HEX:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_hex (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ASCII:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_ascii (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CONV:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_conv (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_BIN:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_bigint_to_binary_string (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_MD5:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_md5 (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SHA_ONE:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_sha_one (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_AES_ENCRYPT:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_aes_encrypt (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_AES_DECRYPT:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_aes_decrypt (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SHA_TWO:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_sha_two (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_BASE64:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_to_base64 (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_FROM_BASE64:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_from_base64 (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SPACE:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_space (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TRIM:
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
	      goto error;
	    }
	}
      if (DB_IS_NULL (peek_left) || (peek_right && DB_IS_NULL (peek_right)))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_trim (arithptr->misc_operand, peek_right, peek_left, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LTRIM:
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
	      goto error;
	    }
	}
      if (DB_IS_NULL (peek_left) || (peek_right && DB_IS_NULL (peek_right)))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_trim (LEADING, peek_right, peek_left, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_RTRIM:
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
	      goto error;
	    }
	}
      if (DB_IS_NULL (peek_left) || (peek_right && DB_IS_NULL (peek_right)))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_trim (TRAILING, peek_right, peek_left, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_FROM_UNIXTIME:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_from_unixtime (peek_left, peek_right, peek_third, arithptr->value, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LPAD:
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
	      goto error;
	    }
	}
      if (DB_IS_NULL (peek_left) || (peek_third && DB_IS_NULL (peek_third)))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_pad (LEADING, peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_RPAD:
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
	      goto error;
	    }
	}
      if (DB_IS_NULL (peek_left) || (peek_third && DB_IS_NULL (peek_third)))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_pad (TRAILING, peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_REPLACE:
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
	      goto error;
	    }
	}
      if (DB_IS_NULL (peek_left) || (peek_third && DB_IS_NULL (peek_third)))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_replace (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TRANSLATE:
      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_INFER_COLLATION) && !DB_IS_NULL (peek_left))
	{
	  TP_DOMAIN_STATUS status = tp_value_change_coll_and_codeset (peek_third, peek_third,
								      db_get_string_collation (peek_left),
								      db_get_string_codeset (peek_left));

	  if (status != DOMAIN_COMPATIBLE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (DB_VALUE_TYPE (peek_third)),
		      pr_type_name (DB_VALUE_TYPE (peek_left)));
	      goto error;
	    }
	}
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_translate (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ADD_MONTHS:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_add_months (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LAST_DAY:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_last_day (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TIME_FORMAT:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_time_format (peek_left, peek_right, peek_third, arithptr->value, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_YEAR:
    case T_MONTH:
    case T_DAY:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (get_year_month_or_day (peek_right, arithptr->opcode, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_HOUR:
    case T_MINUTE:
    case T_SECOND:
      {
	/* T_HOUR, T_MINUTE and T_SECOND must be kept consecutive in OPERATOR_TYPE enum */
	if (get_hour_minute_or_second (peek_right, arithptr->opcode, arithptr->value) != NO_ERROR)
	  {
	    goto error;
	  }
      }
      break;

    case T_QUARTER:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_get_date_quarter (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_WEEKDAY:
    case T_DAYOFWEEK:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (get_date_weekday (peek_right, arithptr->opcode, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DAYOFYEAR:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_get_date_dayofyear (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TODAYS:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_get_date_totaldays (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_FROMDAYS:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_get_date_from_days (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TIMETOSEC:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_convert_time_to_sec (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SECTOTIME:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_convert_sec_to_time (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TIMESTAMP:
      if (DB_IS_NULL (peek_left) || (peek_right != NULL && DB_IS_NULL (peek_right)))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_timestamp (peek_left, peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_LIKE_LOWER_BOUND:
    case T_LIKE_UPPER_BOUND:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  const bool compute_lower_bound = (arithptr->opcode == T_LIKE_LOWER_BOUND);

	  if (db_like_bound (peek_left, peek_right, arithptr->value, compute_lower_bound) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_MAKEDATE:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_add_days_to_year (peek_left, peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_ADDTIME:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_add_time (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_MAKETIME:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_convert_to_time (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_WEEK:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_get_date_week (peek_left, peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_UNIX_TIMESTAMP:
      if (arithptr->rightptr)
	{
	  if (DB_IS_NULL (peek_right))
	    {
	      PRIM_SET_NULL (arithptr->value);
	    }
	  else if (db_unix_timestamp (peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
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
      break;

    case T_MONTHS_BETWEEN:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_months_between (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ATAN2:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_atan2_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ATAN:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_atan_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_FORMAT:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_format (peek_left, peek_right, peek_third, arithptr->value, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DATE_FORMAT:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_date_format (peek_left, peek_right, peek_third, arithptr->value, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}

      break;

    case T_STR_TO_DATE:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_str_to_date (peek_left, peek_right, peek_third, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ADDDATE:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_date_add_interval_days (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DATE_ADD:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  int unit = db_get_int (peek_third);
	  if (db_date_add_interval_expr (arithptr->value, peek_left, peek_right, unit) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_SUBDATE:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_date_sub_interval_days (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DATEDIFF:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_date_diff (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TIMEDIFF:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_time_diff (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DATE_SUB:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  int unit = db_get_int (peek_third);

	  if (db_date_sub_interval_expr (arithptr->value, peek_left, peek_right, unit) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_SYS_DATE:
      db_value_put_encoded_date (arithptr->value, &vd->sys_datetime.date);
      break;

    case T_SYS_TIME:
      {
	DB_TIME db_time;

	db_time = vd->sys_datetime.time / 1000;
	db_value_put_encoded_time (arithptr->value, &db_time);
	break;
      }

    case T_SYS_DATETIME:
      {
	db_make_datetime (arithptr->value, &vd->sys_datetime);
      }
      break;

    case T_SYS_TIMESTAMP:
      {
	DB_TIMESTAMP db_timestamp;
	DB_TIME db_time;

	db_time = vd->sys_datetime.time / 1000;

	db_timestamp_encode_ses (&vd->sys_datetime.date, &db_time, &db_timestamp, NULL);
	db_make_timestamp (arithptr->value, db_timestamp);
      }
      break;

    case T_CURRENT_DATE:
      {
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
	break;
      }

    case T_CURRENT_TIME:
      {
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
	break;
      }

    case T_CURRENT_TIMESTAMP:
      {
	DB_TIMESTAMP db_timestamp;
	DB_TIME db_time;

	db_time = vd->sys_datetime.time / 1000;

	db_timestamp_encode_sys (&vd->sys_datetime.date, &db_time, &db_timestamp, NULL);

	db_make_timestamp (arithptr->value, db_timestamp);
      }
      break;

    case T_CURRENT_DATETIME:
      {
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
      }
      break;

    case T_UTC_TIME:
      {
	DB_TIME db_time;
	db_time = (DB_TIME) (vd->sys_epochtime % SECONDS_OF_ONE_DAY);
	db_value_put_encoded_time (arithptr->value, &db_time);
	break;
      }

    case T_UTC_DATE:
      {
	DB_DATE date;
	int year, month, day, hour, minute, second;

	tz_timestamp_decode_no_leap_sec (vd->sys_epochtime, &year, &month, &day, &hour, &minute, &second);
	date = julian_encode (month + 1, day, year);
	db_value_put_encoded_date (arithptr->value, &date);
	break;
      }

    case T_LOCAL_TRANSACTION_ID:
      db_make_int (arithptr->value, logtb_find_current_tranid (thread_p));
      break;

    case T_TO_CHAR:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_char (peek_left, peek_right, peek_third, arithptr->value, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}

      break;

    case T_BLOB_TO_BIT:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_blob_to_bit (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}

      break;

    case T_CLOB_TO_CHAR:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_clob_to_char (peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}

      break;

    case T_BIT_TO_BLOB:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_BIT || DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_VARBIT)
	{
	  if (db_bit_to_blob (peek_left, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else			/* (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_CHAR || DB_TYPE_VARCHAR) */
	{
	  if (db_char_to_blob (peek_left, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      break;

    case T_CHAR_TO_CLOB:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_char_to_clob (peek_left, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}

      break;

    case T_LOB_LENGTH:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_BLOB)
	{

	  if (db_blob_length (peek_left, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else			/* (DB_VALUE_DOMAIN_TYPE (peek_left) == DB_TYPE_BLOB) */
	{
	  if (db_clob_length (peek_left, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      break;

    case T_TO_DATE:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_date (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_TIME:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_time (peek_left, peek_right, peek_third, DB_TYPE_TIME, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_TIMESTAMP:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_timestamp (peek_left, peek_right, peek_third, DB_TYPE_TIMESTAMP, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_DATETIME:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_datetime (peek_left, peek_right, peek_third, DB_TYPE_DATETIME, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_NUMBER:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_to_number (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	  regu_var->domain->precision = arithptr->value->domain.numeric_info.precision;
	  regu_var->domain->scale = arithptr->value->domain.numeric_info.scale;
	}
      break;

    case T_CURRENT_VALUE:
      /* serial.current_value() is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
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
	      goto error;
	    }
	}
      break;

    case T_NEXT_VALUE:
      /* serial.next_value() is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
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
	      goto error;
	    }
	}
      break;

    case T_CAST:
    case T_CAST_WRAP:
      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION))
	{
	  pr_clone_value (peek_right, arithptr->value);

	  if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (regu_var->domain)))
	    {
	      if (!DB_IS_NULL (arithptr->value))
		{
		  assert (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (arithptr->value)));
		  assert (regu_var->domain->codeset == db_get_string_codeset (arithptr->value));
		}
	      db_string_put_cs_and_collation (arithptr->value, regu_var->domain->codeset,
					      regu_var->domain->collation_id);
	    }
	  else
	    {
	      assert (TP_DOMAIN_TYPE (regu_var->domain) == DB_TYPE_ENUMERATION);

	      if (!DB_IS_NULL (arithptr->value))
		{
		  assert (DB_VALUE_DOMAIN_TYPE (arithptr->value) == DB_TYPE_ENUMERATION);
		  assert (regu_var->domain->codeset == db_get_enum_codeset (arithptr->value));
		}
	      db_enum_put_cs_and_collation (arithptr->value, regu_var->domain->codeset, regu_var->domain->collation_id);

	    }
	}
      else
	{
	  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_STRICT_TYPE_CAST) && arithptr->opcode == T_CAST_WRAP)
	    {
	      dom_status = tp_value_cast (peek_right, arithptr->value, arithptr->domain, false);
	    }
	  else
	    {
	      dom_status = tp_value_cast_force (peek_right, arithptr->value, arithptr->domain, false);
	    }

	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, arithptr->domain);
	      goto error;
	    }
	}
      break;

    case T_CAST_NOFAIL:
      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION))
	{
	  pr_clone_value (peek_right, arithptr->value);

	  if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (regu_var->domain)))
	    {
	      if (!DB_IS_NULL (arithptr->value))
		{
		  assert (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (arithptr->value)));
		  assert (regu_var->domain->codeset == db_get_string_codeset (arithptr->value));
		}
	      db_string_put_cs_and_collation (arithptr->value, regu_var->domain->codeset,
					      regu_var->domain->collation_id);
	    }
	  else
	    {
	      assert (TP_DOMAIN_TYPE (regu_var->domain) == DB_TYPE_ENUMERATION);

	      if (!DB_IS_NULL (arithptr->value))
		{
		  assert (DB_VALUE_DOMAIN_TYPE (arithptr->value) == DB_TYPE_ENUMERATION);
		  assert (regu_var->domain->codeset == db_get_enum_codeset (arithptr->value));
		}
	      db_enum_put_cs_and_collation (arithptr->value, regu_var->domain->codeset, regu_var->domain->collation_id);

	    }
	}
      else
	{
	  TP_DOMAIN_STATUS status;

	  status = tp_value_cast (peek_right, arithptr->value, arithptr->domain, false);
	  if (status != NO_ERROR)
	    {
	      PRIM_SET_NULL (arithptr->value);
	    }
	}
      break;

    case T_CASE:
    case T_DECODE:
    case T_IF:
      /* set pred is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      /* fetch values */
      switch (eval_pred (thread_p, arithptr->pred, vd, obj_oid))
	{
	case V_FALSE:
	case V_UNKNOWN:	/* unknown pred result, including cases of NULL pred operands */
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	    {
	      goto error;
	    }
	  break;

	case V_TRUE:
	  if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	    {
	      goto error;
	    }
	  break;

	default:
	  goto error;
	}

      dom_status = tp_value_auto_cast (peek_left, arithptr->value, regu_var->domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, regu_var->domain);
	  goto error;
	}
      break;

    case T_PREDICATE:
      /* set pred is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
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
	  goto error;
	}

      dom_status = tp_value_auto_cast (peek_left, arithptr->value, regu_var->domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, regu_var->domain);
	  goto error;
	}
      break;

    case T_NVL:
    case T_IFNULL:
    case T_COALESCE:
      {
	DB_VALUE *src;
	TP_DOMAIN *target_domain;

	target_domain = regu_var->domain;

	if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	  {
	    goto error;
	  }

	if (DB_IS_NULL (peek_left) || target_domain == NULL)
	  {
	    if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	      {
		goto error;
	      }
	  }

	if (target_domain == NULL)
	  {
	    TP_DOMAIN *arg1, *arg2, tmp_arg1, tmp_arg2;

	    arg1 = tp_domain_resolve_value (peek_left, &tmp_arg1);
	    arg2 = tp_domain_resolve_value (peek_right, &tmp_arg2);

	    target_domain = tp_infer_common_domain (arg1, arg2);
	  }

	src = DB_IS_NULL (peek_left) ? peek_right : peek_left;
	dom_status = tp_value_cast (src, arithptr->value, target_domain, false);
	if (dom_status != DOMAIN_COMPATIBLE)
	  {
	    (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, target_domain);
	    goto error;
	  }
      }
      break;
    case T_NVL2:
      {
	DB_VALUE *src;
	TP_DOMAIN *target_domain;

	target_domain = regu_var->domain;

	if (fetch_peek_dbval (thread_p, arithptr->leftptr, vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	  {
	    goto error;
	  }

	if (target_domain == NULL)
	  {
	    TP_DOMAIN *arg1, *arg2, *arg3, tmp_arg1, tmp_arg2, tmp_arg3;

	    if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	      {
		goto error;
	      }

	    if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
	      {
		goto error;
	      }

	    arg1 = tp_domain_resolve_value (peek_left, &tmp_arg1);
	    arg2 = tp_domain_resolve_value (peek_right, &tmp_arg2);

	    target_domain = tp_infer_common_domain (arg1, arg2);

	    arg3 = NULL;
	    if (peek_third)
	      {
		TP_DOMAIN *tmp_domain;

		arg3 = tp_domain_resolve_value (peek_third, &tmp_arg3);
		tmp_domain = tp_infer_common_domain (target_domain, arg3);

		target_domain = tmp_domain;
	      }
	  }

	if (DB_IS_NULL (peek_left))
	  {
	    if (peek_third == NULL)
	      {
		if (fetch_peek_dbval (thread_p, arithptr->thirdptr, vd, NULL, obj_oid, tpl, &peek_third) != NO_ERROR)
		  {
		    goto error;
		  }
	      }
	    src = peek_third;
	  }
	else
	  {
	    if (peek_right == NULL)
	      {
		if (fetch_peek_dbval (thread_p, arithptr->rightptr, vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
		  {
		    goto error;
		  }
	      }

	    src = peek_right;
	  }

	dom_status = tp_value_cast (src, arithptr->value, target_domain, false);
	if (dom_status != DOMAIN_COMPATIBLE)
	  {
	    (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, src, target_domain);

	    goto error;
	  }
      }
      break;

    case T_ISNULL:
      if (DB_IS_NULL (peek_right))
	{
	  db_make_int (arithptr->value, 1);
	}
      else
	{
	  db_make_int (arithptr->value, 0);
	}
      break;

    case T_CONCAT:
      if (arithptr->rightptr != NULL)
	{
	  if (qdata_strcat_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  dom_status = tp_value_auto_cast (peek_left, arithptr->value, regu_var->domain);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, regu_var->domain);
	      goto error;
	    }
	}
      break;

    case T_CONCAT_WS:
      if (DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	  break;
	}
      if (arithptr->rightptr != NULL)
	{
	  if (DB_IS_NULL (peek_left) && DB_IS_NULL (peek_right))
	    {
	      PRIM_SET_NULL (arithptr->value);
	    }
	  else if (DB_IS_NULL (peek_left))
	    {
	      dom_status = tp_value_auto_cast (peek_right, arithptr->value, regu_var->domain);
	      if (dom_status != DOMAIN_COMPATIBLE)
		{
		  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, regu_var->domain);
		  goto error;
		}
	    }
	  else if (DB_IS_NULL (peek_right))
	    {
	      dom_status = tp_value_auto_cast (peek_left, arithptr->value, regu_var->domain);
	      if (dom_status != DOMAIN_COMPATIBLE)
		{
		  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, regu_var->domain);
		  goto error;
		}
	    }
	  else
	    {
	      DB_VALUE tmp_val;

	      db_make_null (&tmp_val);
	      if (qdata_strcat_dbval (peek_left, peek_third, &tmp_val, regu_var->domain) != NO_ERROR)
		{
		  goto error;
		}
	      if (qdata_strcat_dbval (&tmp_val, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
		{
		  (void) pr_clear_value (&tmp_val);
		  goto error;
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
	      dom_status = tp_value_auto_cast (peek_left, arithptr->value, regu_var->domain);
	      if (dom_status != DOMAIN_COMPATIBLE)
		{
		  (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, regu_var->domain);
		  goto error;
		}
	    }
	}
      break;

    case T_FIELD:
      if (DB_IS_NULL (peek_third))
	{
	  db_make_int (arithptr->value, 0);
	  break;
	}
      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FIELD_COMPARE))
	{
	  bool can_compare = false;
	  int cmp_res = DB_UNK;

	  cmp_res = tp_value_compare_with_error (peek_third, peek_left, 1, 0, &can_compare);
	  if (cmp_res == DB_EQ)
	    {
	      db_make_int (arithptr->value, 1);
	      break;
	    }
	  else if (cmp_res == DB_UNK && can_compare == false)
	    {
	      goto error;
	    }


	  cmp_res = tp_value_compare_with_error (peek_third, peek_right, 1, 0, &can_compare);
	  if (cmp_res == DB_EQ)
	    {
	      db_make_int (arithptr->value, 2);
	      break;
	    }
	  else if (cmp_res == DB_UNK && can_compare == false)
	    {
	      goto error;
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

	      cmp_res = tp_value_compare_with_error (peek_third, peek_right, 1, 0, &can_compare);
	      if (cmp_res == DB_EQ)
		{
		  /* match */
		  db_make_int (arithptr->value, -i);
		}
	      else if (cmp_res == DB_UNK && can_compare == false)
		{
		  goto error;
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
      break;

    case T_REPEAT:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_string_repeat (peek_left, peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_LEFT:
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
	      goto error;
	    }

	  db_make_int (&tmp_val, 0);
	  if (db_string_substring (SUBSTRING, peek_left, &tmp_val, &tmp_val2, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_RIGHT:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  DB_VALUE tmp_val, tmp_val2;

	  if (QSTR_IS_BIT (TP_DOMAIN_TYPE (arithptr->leftptr->domain)))
	    {
	      if (db_string_bit_length (peek_left, &tmp_val) != NO_ERROR)
		{
		  goto error;
		}
	    }
	  else
	    {
	      if (db_string_char_length (peek_left, &tmp_val) != NO_ERROR)
		{
		  goto error;
		}
	    }
	  if (DB_IS_NULL (&tmp_val))
	    {
	      PRIM_SET_NULL (arithptr->value);
	      break;
	    }

	  dom_status = tp_value_auto_cast (peek_right, &tmp_val2, &tp_Integer_domain);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_right, &tp_Integer_domain);
	      goto error;
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
	      goto error;
	    }
	}
      break;

    case T_LOCATE:
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
		  goto error;
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
		  goto error;
		}
	      if (DB_IS_NULL (&tmp_len))
		{
		  goto error;
		}

	      db_make_int (&tmp_len, db_get_int (&tmp_len) - db_get_int (&tmp_arg3) + 1);

	      if (db_string_substring (SUBSTRING, peek_right, &tmp_arg3, &tmp_len, &tmp_val) != NO_ERROR)
		{
		  goto error;
		}

	      if (db_string_position (peek_left, &tmp_val, arithptr->value) != NO_ERROR)
		{
		  (void) pr_clear_value (&tmp_val);
		  goto error;
		}
	      if (db_get_int (arithptr->value) > 0)
		{
		  db_make_int (arithptr->value, db_get_int (arithptr->value) + db_get_int (&tmp_arg3) - 1);
		}

	      (void) pr_clear_value (&tmp_val);
	    }
	}
      break;

    case T_SUBSTRING_INDEX:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_string_substring_index (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_MID:
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
	      if (QSTR_IS_BIT (TP_DOMAIN_TYPE (arithptr->leftptr->domain)))
		{
		  if (db_string_bit_length (peek_left, &tmp_len) != NO_ERROR)
		    {
		      goto error;
		    }
		}
	      else
		{
		  if (db_string_char_length (peek_left, &tmp_len) != NO_ERROR)
		    {
		      goto error;
		    }
		}
	      if (DB_IS_NULL (&tmp_len))
		{
		  goto error;
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
	      goto error;
	    }
	}
      break;

    case T_STRCMP:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  int cmp;

	  if (db_string_compare (peek_left, peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
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
      break;

    case T_REVERSE:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_string_reverse (peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_DISK_SIZE:
      if (DB_IS_NULL (peek_right))
	{
	  db_make_int (arithptr->value, 0);
	}
      else
	{
	  db_make_int (arithptr->value, pr_data_writeval_disk_size (peek_right));
	  /* call pr_data_writeval_disk_size function to return the size on disk */
	}
      break;

    case T_NULLIF:		/* when a = b then null else a end */
      {
	TP_DOMAIN *target_domain;
	bool can_compare = false;
	int cmp_res = DB_UNK;

	target_domain = regu_var->domain;
	if (DB_IS_NULL (peek_left))
	  {
	    PRIM_SET_NULL (arithptr->value);
	    break;
	  }
	else if (target_domain == NULL)
	  {
	    TP_DOMAIN *arg1, *arg2, tmp_arg1, tmp_arg2;

	    arg1 = tp_domain_resolve_value (peek_left, &tmp_arg1);
	    arg2 = tp_domain_resolve_value (peek_right, &tmp_arg2);

	    target_domain = tp_infer_common_domain (arg1, arg2);
	  }

	cmp_res = tp_value_compare_with_error (peek_left, peek_right, 1, 0, &can_compare);
	if (cmp_res == DB_EQ)
	  {
	    PRIM_SET_NULL (arithptr->value);
	  }
	else if (cmp_res == DB_UNK && can_compare == false)
	  {
	    goto error;
	  }
	else
	  {
	    dom_status = tp_value_cast (peek_left, arithptr->value, target_domain, false);
	    if (dom_status != DOMAIN_COMPATIBLE)
	      {
		(void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_left, target_domain);
		goto error;
	      }
	  }
      }
      break;

    case T_EXTRACT:
      if (qdata_extract_dbval (arithptr->misc_operand, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LEAST:
      {
	int error;
	TP_DOMAIN *target_domain;

	error = db_least_or_greatest (peek_left, peek_right, arithptr->value, true);
	if (error != NO_ERROR)
	  {
	    goto error;
	  }

	target_domain = regu_var->domain;
	if (target_domain == NULL)
	  {
	    TP_DOMAIN *arg1, *arg2, tmp_arg1, tmp_arg2;

	    arg1 = tp_domain_resolve_value (peek_left, &tmp_arg1);
	    arg2 = tp_domain_resolve_value (peek_right, &tmp_arg2);

	    target_domain = tp_infer_common_domain (arg1, arg2);
	  }

	dom_status = tp_value_cast (arithptr->value, arithptr->value, target_domain, false);
	if (dom_status != DOMAIN_COMPATIBLE)
	  {
	    (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, arithptr->value, target_domain);
	    goto error;
	  }
	break;
      }

    case T_GREATEST:
      {
	int error;
	TP_DOMAIN *target_domain;

	error = db_least_or_greatest (peek_left, peek_right, arithptr->value, false);
	if (error != NO_ERROR)
	  {
	    goto error;
	  }

	target_domain = regu_var->domain;
	if (target_domain == NULL)
	  {
	    TP_DOMAIN *arg1, *arg2, tmp_arg1, tmp_arg2;

	    arg1 = tp_domain_resolve_value (peek_left, &tmp_arg1);
	    arg2 = tp_domain_resolve_value (peek_right, &tmp_arg2);

	    target_domain = tp_infer_common_domain (arg1, arg2);
	  }

	dom_status = tp_value_cast (arithptr->value, arithptr->value, target_domain, false);
	if (dom_status != DOMAIN_COMPATIBLE)
	  {
	    (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, arithptr->value, target_domain);
	    goto error;
	  }
	break;
      }

    case T_SYS_CONNECT_BY_PATH:
      {
	if (!qdata_evaluate_sys_connect_by_path (thread_p, (void *) arithptr->thirdptr->xasl, arithptr->leftptr,
						 peek_right, arithptr->value, vd))
	  {
	    goto error;
	  }

	dom_status = tp_value_auto_cast (arithptr->value, arithptr->value, regu_var->domain);
	if (dom_status != DOMAIN_COMPATIBLE)
	  {
	    (void) tp_domain_status_er_set (dom_status, ARG_FILE_LINE, arithptr->value, regu_var->domain);
	    goto error;
	  }
	break;
      }

    case T_STRCAT:
      if (qdata_strcat_dbval (peek_left, peek_right, arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_PI:
      db_make_double (arithptr->value, PI);
      break;

    case T_ROW_COUNT:
      /* session info is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      {
	int row_count = -1;
	if (session_get_row_count (thread_p, &row_count) != NO_ERROR)
	  {
	    goto error;
	  }
	db_make_int (arithptr->value, row_count);
      }
      break;

    case T_LAST_INSERT_ID:
      /* session info is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      if (session_get_last_insert_id (thread_p, arithptr->value, true) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_EVALUATE_VARIABLE:
      /* session info is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      if (session_get_variable (thread_p, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DEFINE_VARIABLE:
      /* session info is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      if (session_define_variable (thread_p, peek_left, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_RAND:
    case T_RANDOM:
      /* random(), drandom() is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
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
		      goto error;
		    }

		  srand48_r ((long) db_get_int (peek_right), arithptr->rand_seed);
		}

	      lrand48_r (arithptr->rand_seed, &r);
	      db_make_int (arithptr->value, r);
	    }
	}
      break;

    case T_DRAND:
    case T_DRANDOM:
      /* random(), drandom() is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
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
		      goto error;
		    }

		  srand48_r ((long) db_get_int (peek_right), arithptr->rand_seed);
		}

	      drand48_r (arithptr->rand_seed, &r);
	      db_make_double (arithptr->value, r);
	    }
	}
      break;

    case T_LIST_DBS:
      if (qdata_list_dbs (thread_p, arithptr->value, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SYS_GUID:
      /* sys_guid() is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      if (db_guid (thread_p, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TYPEOF:
      if (db_typeof_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INDEX_CARDINALITY:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (qdata_get_cardinality (thread_p, peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_EXEC_STATS:
      /* session info is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      if (session_get_exec_stats_and_clear (thread_p, peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_ENUMERATION_VALUE:
      if (db_value_to_enumeration_value (peek_right, arithptr->value, arithptr->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INET_ATON:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_inet_aton (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INET_NTOA:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_inet_ntoa (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CHARSET:
    case T_COLLATION:
      if (db_get_cs_coll_info (arithptr->value, peek_right, (arithptr->opcode == T_COLLATION) ? 1 : 0) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TRACE_STATS:
      /* session info is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      if (session_get_trace_stats (thread_p, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_WIDTH_BUCKET:
      if (db_width_bucket (arithptr->value, peek_left, peek_right, peek_third, peek_fourth) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INDEX_PREFIX:
      if (db_string_index_prefix (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SLEEP:
      /* sleep() is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      if (db_sleep (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DBTIMEZONE:
    case T_SESSIONTIMEZONE:
      {
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
	break;
      }

    case T_TZ_OFFSET:
      {
	DB_DATETIME tmp_datetime, utc_datetime;
	DB_VALUE timezone;
	int timezone_milis;

	/* extract the timezone part */
	if (db_sys_timezone (&timezone) != NO_ERROR)
	  {
	    goto error;
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
		goto error;
	      }
	  }
      }
      break;

    case T_NEW_TIME:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right) || DB_IS_NULL (peek_third))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_new_time (peek_left, peek_right, peek_third, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_FROM_TZ:
      if (DB_IS_NULL (peek_left) || DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_from_tz (peek_left, peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_CONV_TZ:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  if (db_conv_tz (peek_right, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_TO_DATETIME_TZ:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_datetime (peek_left, peek_right, peek_third, DB_TYPE_DATETIMETZ, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_TIMESTAMP_TZ:
      if (DB_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_timestamp (peek_left, peek_right, peek_third, DB_TYPE_TIMESTAMPTZ, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_UTC_TIMESTAMP:
      {
	DB_DATE date;
	DB_TIME time;
	int year, month, day, hour, minute, second;
	DB_TIMESTAMP timestamp;

	tz_timestamp_decode_no_leap_sec (vd->sys_epochtime, &year, &month, &day, &hour, &minute, &second);
	date = julian_encode (month + 1, day, year);
	db_time_encode (&time, hour, minute, second);
	db_timestamp_encode_ses (&date, &time, &timestamp, NULL);
	db_make_timestamp (arithptr->value, timestamp);
	break;
      }

    case T_CRC32:
      if (DB_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_crc32_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    default:
      break;
    }

  *peek_dbval = arithptr->value;

  if (original_domain != NULL && TP_DOMAIN_TYPE (original_domain) == DB_TYPE_VARIABLE)
    {
      TP_DOMAIN *resolved_dom = tp_domain_resolve_value (arithptr->value, NULL);

      /* keep DB_TYPE_VARIABLE if resolved domain is NULL */
      if (TP_DOMAIN_TYPE (resolved_dom) != DB_TYPE_NULL)
	{
	  regu_var->domain = arithptr->domain = resolved_dom;
	}
      else
	{
	  regu_var->domain = arithptr->domain = original_domain;
	}
    }

  if (arithptr->domain != NULL && arithptr->domain->collation_flag != TP_DOMAIN_COLL_NORMAL
      && !DB_IS_NULL (arithptr->value))
    {
      TP_DOMAIN *resolved_dom;

      assert (TP_TYPE_HAS_COLLATION (arithptr->domain->type->id));

      resolved_dom = tp_domain_resolve_value (arithptr->value, NULL);

      /* keep DB_TYPE_VARIABLE if resolved domain is NULL */
      if (TP_DOMAIN_TYPE (resolved_dom) != DB_TYPE_NULL)
	{
	  regu_var->domain = resolved_dom;
	}
    }

  /* check for the first time */
  if (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST)
      && !REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST))
    {
      int not_const = 0;

      assert (arithptr->pred == NULL);

      if (arithptr->leftptr == NULL || REGU_VARIABLE_IS_FLAGED (arithptr->leftptr, REGU_VARIABLE_FETCH_ALL_CONST))
	{
	  ;			/* is_const, go ahead */
	}
      else
	{
	  not_const++;
	}

      if (arithptr->rightptr == NULL || REGU_VARIABLE_IS_FLAGED (arithptr->rightptr, REGU_VARIABLE_FETCH_ALL_CONST))
	{
	  ;			/* is_const, go ahead */
	}
      else
	{
	  not_const++;
	}

      if (arithptr->thirdptr == NULL || REGU_VARIABLE_IS_FLAGED (arithptr->thirdptr, REGU_VARIABLE_FETCH_ALL_CONST))
	{
	  ;			/* is_const, go ahead */
	}
      else
	{
	  not_const++;
	}

      if (not_const == 0)
	{
	  REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
	  assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
	}
      else
	{
	  REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
	  assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
	}
    }

fetch_peek_arith_end:

  assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST)
	  || REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));

#if !defined(NDEBUG)
  switch (arithptr->opcode)
    {
      /* incr/decr is not constant */
    case T_INCR:
    case T_DECR:
      /* serial.current_value(), serial.next_value() is not constant */
    case T_CURRENT_VALUE:
    case T_NEXT_VALUE:
      /* set pred is not constant */
    case T_CASE:
    case T_DECODE:
    case T_IF:
    case T_PREDICATE:
      /* session info is not constant */
    case T_ROW_COUNT:
    case T_LAST_INSERT_ID:
    case T_EVALUATE_VARIABLE:
    case T_DEFINE_VARIABLE:
    case T_EXEC_STATS:
    case T_TRACE_STATS:
      /* random(), drandom() is not constant */
    case T_RAND:
    case T_RANDOM:
    case T_DRAND:
    case T_DRANDOM:
      /* sys_guid() is not constant */
    case T_SYS_GUID:
      /* sleep() is not constant */
    case T_SLEEP:

      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
      break;
    default:
      break;
    }

  /* set pred is not constant */
  if (arithptr->pred != NULL)
    {
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
    }
#endif

  thread_dec_recursion_depth (thread_p);

  return NO_ERROR;

error:
  thread_dec_recursion_depth (thread_p);

  if (original_domain)
    {
      /* restores regu variable domain */
      regu_var->domain = original_domain;
    }

  return ER_FAILED;
}

/*
 * fetch_peek_dbval () - returns a POINTER to an existing db_value
 *   return: NO_ERROR or ER_code
 *   regu_var(in/out): Regulator Variable
 *   vd(in): Value Descriptor
 *   cls_oid(in): Class Identifier
 *   obj_oid(in): Object Identifier
 *   tpl(in): Tuple
 *   peek_dbval(out): Set to the value ref resulting from the fetch operation
 *
 */
int
fetch_peek_dbval (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd, OID * class_oid, OID * obj_oid,
		  QFILE_TUPLE tpl, DB_VALUE ** peek_dbval)
{
  int length;
  PR_TYPE *pr_type;
  OR_BUF buf;
  QFILE_TUPLE_VALUE_FLAG flag;
  char *ptr;
  REGU_VARIABLE *head_regu = NULL, *regu = NULL;
  int error = NO_ERROR;
  REGU_VALUE_LIST *reguval_list = NULL;
  DB_TYPE head_type, cur_type;
  FUNCTION_TYPE *funcp = NULL;

  switch (regu_var->type)
    {
    case TYPE_ATTR_ID:		/* fetch object attribute value */
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      /* is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      *peek_dbval = regu_var->value.attr_descr.cache_dbvalp;
      if (*peek_dbval != NULL)
	{
	  /* we have a cached pointer already */
	  break;
	}
      else
	{
	  *peek_dbval = heap_attrinfo_access (regu_var->value.attr_descr.id, regu_var->value.attr_descr.cache_attrinfo);
	  if (*peek_dbval == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      regu_var->value.attr_descr.cache_dbvalp = *peek_dbval;	/* cache */
      break;

    case TYPE_OID:		/* fetch object identifier value */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
      *peek_dbval = &regu_var->value.dbval;
      db_make_oid (*peek_dbval, obj_oid);
      break;

    case TYPE_CLASSOID:	/* fetch class identifier value */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
      *peek_dbval = &regu_var->value.dbval;
      db_make_oid (*peek_dbval, class_oid);
      break;

    case TYPE_POSITION:	/* fetch list file tuple value */
      /* is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));

      pr_clear_value (regu_var->vfetch_to);

      *peek_dbval = regu_var->vfetch_to;

      flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tpl, regu_var->value.pos_descr.pos_no, &ptr, &length);
      if (flag == V_BOUND)
	{
	  pr_type = regu_var->value.pos_descr.dom->type;
	  if (pr_type == NULL)
	    {
	      goto exit_on_error;
	    }

	  OR_BUF_INIT (buf, ptr, length);

	  if (pr_type->data_readval (&buf, *peek_dbval, regu_var->value.pos_descr.dom, -1, false /* Don't copy */ ,
				     NULL, 0) != NO_ERROR)
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

      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
      *peek_dbval = (DB_VALUE *) vd->dbval_ptr + regu_var->value.val_pos;
      break;

    case TYPE_CONSTANT:	/* fetch constant-column value */
      /* is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));

      /* execute linked query */
      EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);
      if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
	{
	  goto exit_on_error;
	}
      *peek_dbval = regu_var->value.dbvalptr;
      break;

    case TYPE_ORDERBY_NUM:
      /* is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
      *peek_dbval = regu_var->value.dbvalptr;
      break;

    case TYPE_DBVAL:		/* fetch db_value */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
      *peek_dbval = &regu_var->value.dbval;
      break;

    case TYPE_REGUVAL_LIST:
      /* is not constant */
      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
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

      error = fetch_peek_dbval (thread_p, regu, vd, class_oid, obj_oid, tpl, peek_dbval);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case TYPE_INARITH:		/* compute and fetch arithmetic expr. value */
    case TYPE_OUTARITH:
      error = fetch_peek_arith (thread_p, regu_var, vd, obj_oid, tpl, peek_dbval);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST)
	      || REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
      break;

    case TYPE_FUNC:		/* fetch function value */
      if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST))
	{
	  funcp = regu_var->value.funcp;
	  assert (funcp != NULL);

	  *peek_dbval = funcp->value;

	  return NO_ERROR;
	}

      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));

      error = qdata_evaluate_function (thread_p, regu_var, vd, obj_oid, tpl);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      funcp = regu_var->value.funcp;
      assert (funcp != NULL);

      *peek_dbval = funcp->value;

      /* check for the first time */
      if (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST)
	  && !REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST))
	{
	  int not_const = 0;

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
	      {
		regu_variable_list_node *operand;

		operand = funcp->operand;

		while (operand != NULL)
		  {
		    if (!REGU_VARIABLE_IS_FLAGED (&(operand->value), REGU_VARIABLE_FETCH_ALL_CONST))
		      {
			not_const++;
			break;
		      }
		    operand = operand->next;
		  }
	      }
	      break;

	    case F_INSERT_SUBSTRING:
	      /* should sync with qdata_insert_substring_function () */
	      {
		REGU_VARIABLE *regu_array[NUM_F_INSERT_SUBSTRING_ARGS];
		int i;
		int num_regu = 0;

		/* initialize the argument array */
		for (i = 0; i < NUM_F_INSERT_SUBSTRING_ARGS; i++)
		  {
		    regu_array[i] = NULL;
		  }

		error = qdata_regu_list_to_regu_array (funcp, NUM_F_INSERT_SUBSTRING_ARGS, regu_array, &num_regu);
		if (num_regu != NUM_F_INSERT_SUBSTRING_ARGS)
		  {
		    assert (false);
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_GENERIC_FUNCTION_FAILURE, 0);
		    goto exit_on_error;
		  }
		if (error != NO_ERROR)
		  {
		    goto exit_on_error;
		  }

		for (i = 0; i < NUM_F_INSERT_SUBSTRING_ARGS; i++)
		  {
		    if (!REGU_VARIABLE_IS_FLAGED (regu_array[i], REGU_VARIABLE_FETCH_ALL_CONST))
		      {
			not_const++;
			break;	/* exit for-loop */
		      }
		  }		/* for (i = 0; ...) */
	      }
	      break;

	    case F_ELT:
	      /* should sync with qdata_elt () */
	      {
		regu_variable_list_node *operand;
		DB_VALUE *index = NULL;
		DB_TYPE index_type;
		DB_BIGINT idx = 0;
		bool is_null_elt = false;

		assert (funcp->operand != NULL);
		if (!REGU_VARIABLE_IS_FLAGED (&funcp->operand->value, REGU_VARIABLE_FETCH_ALL_CONST))
		  {
		    not_const++;
		  }
		else
		  {
		    error = fetch_peek_dbval (thread_p, &funcp->operand->value, vd, NULL, obj_oid, tpl, &index);
		    if (error != NO_ERROR)
		      {
			goto exit_on_error;
		      }

		    index_type = DB_VALUE_DOMAIN_TYPE (index);

		    switch (index_type)
		      {
		      case DB_TYPE_SMALLINT:
			idx = db_get_short (index);
			break;
		      case DB_TYPE_INTEGER:
			idx = db_get_int (index);
			break;
		      case DB_TYPE_BIGINT:
			idx = db_get_bigint (index);
			break;
		      case DB_TYPE_NULL:
			is_null_elt = true;
			break;
		      default:
			assert (false);	/* is impossible */
			error = ER_QPROC_INVALID_DATATYPE;
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			if (index != NULL && index->need_clear == true)
			  {
			    pr_clear_value (index);
			  }
			goto exit_on_error;
		      }

		    if (!is_null_elt)
		      {
			if (idx <= 0)
			  {
			    /* index is 0 or is negative */
			    is_null_elt = true;
			  }
			else
			  {
			    idx--;
			    operand = funcp->operand->next;

			    while (idx > 0 && operand != NULL)
			      {
				operand = operand->next;
				idx--;
			      }

			    if (operand == NULL)
			      {
				/* index greater than number of arguments */
				is_null_elt = true;
			      }
			    else
			      {
				assert (operand != NULL);
				if (!REGU_VARIABLE_IS_FLAGED (&(operand->value), REGU_VARIABLE_FETCH_ALL_CONST))
				  {
				    not_const++;
				  }
			      }	/* operand != NULL */
			  }
		      }		/* if (!is_null_elt) */
		  }		/* else */

#if !defined(NDEBUG)
		if (is_null_elt)
		  {
		    assert (not_const == 0);
		  }
#endif
	      }
	      break;

	    default:
	      not_const++;	/* is not constant */
	      break;
	    }

	  if (not_const == 0)
	    {
	      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
	      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
	    }
	  else
	    {
	      REGU_VARIABLE_SET_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);
	      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
	    }
	}

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
	  /* is not constant */
	  assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
	  assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
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

  assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST)
	  || REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));

  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_APPLY_COLLATION))
    {
      if (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (regu_var->domain)))
	{
	  if (!DB_IS_NULL (*peek_dbval))
	    {
	      assert (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (*peek_dbval)));
	      assert (regu_var->domain->codeset == db_get_string_codeset (*peek_dbval));
	    }
	  db_string_put_cs_and_collation (*peek_dbval, regu_var->domain->codeset, regu_var->domain->collation_id);
	}
      else
	{
	  assert (TP_DOMAIN_TYPE (regu_var->domain) == DB_TYPE_ENUMERATION);

	  if (!DB_IS_NULL (*peek_dbval))
	    {
	      assert (DB_VALUE_DOMAIN_TYPE (*peek_dbval) == DB_TYPE_ENUMERATION);
	      assert (regu_var->domain->codeset == db_get_enum_codeset (*peek_dbval));
	    }
	  db_enum_put_cs_and_collation (*peek_dbval, regu_var->domain->codeset, regu_var->domain->collation_id);

	}
    }

  if (*peek_dbval != NULL && !DB_IS_NULL (*peek_dbval))
    {
      if (TP_DOMAIN_TYPE (regu_var->domain) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_COLLATION_FLAG (regu_var->domain) != TP_DOMAIN_COLL_NORMAL)
	{
	  regu_var->domain = tp_domain_resolve_value (*peek_dbval, NULL);
	}

      /* for REGUVAL_LIST compare type with the corresponding column of first row if not compatible, raise an error
       * This is the same behavior as "union" */
      if (regu_var->type == TYPE_REGUVAL_LIST)
	{
	  head_regu = reguval_list->regu_list->value;
	  regu = reguval_list->current_value->value;

	  if (regu->domain == NULL || TP_DOMAIN_TYPE (regu->domain) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_COLLATION_FLAG (regu->domain) != TP_DOMAIN_COLL_NORMAL)
	    {
	      regu->domain = tp_domain_resolve_value (*peek_dbval, NULL);
	    }
	  head_type = TP_DOMAIN_TYPE (head_regu->domain);
	  cur_type = TP_DOMAIN_TYPE (regu->domain);

	  /* compare the type */
	  if (head_type != DB_TYPE_NULL && cur_type != DB_TYPE_NULL && head_regu->domain != regu->domain)
	    {
	      if (head_type != cur_type || !pr_is_string_type (head_type) || !pr_is_variable_type (head_type))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INCOMPATIBLE_TYPES, 0);
		  goto exit_on_error;
		}
	      else if (TP_DOMAIN_COLLATION (head_regu->domain) != TP_DOMAIN_COLLATION (regu->domain))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
		  goto exit_on_error;
		}
	    }
	}
    }

  assert (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST)
	  || REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));

  return NO_ERROR;

exit_on_error:

  return ER_FAILED;
}

/*
 * fetch_peek_dbval_pos () -
 *   return: NO_ERROR or ER_code
 *   regu_var(in/out): Regulator Variable
 *   tpl(in): Tuple
 *   pos(in):
 *   peek_dbval(out): Set to the value ref resulting from the fetch operation
 *   next_tpl(out): Set to the next tuple ref
 */
static int
fetch_peek_dbval_pos (REGU_VARIABLE * regu_var, QFILE_TUPLE tpl, int pos, DB_VALUE ** peek_dbval,
		      QFILE_TUPLE * next_tpl)
{
  int length;
  PR_TYPE *pr_type;
  OR_BUF buf;
  char *ptr;
  QFILE_TUPLE_VALUE_POSITION *pos_descr;

  /* assume regu_var->type == TYPE_POSITION */
  pos_descr = &regu_var->value.pos_descr;
  pr_clear_value (regu_var->vfetch_to);
  *peek_dbval = regu_var->vfetch_to;

  /* locate value position in the tuple */
  if (qfile_locate_tuple_value_r (tpl, pos, &ptr, &length) == V_BOUND)
    {
      pr_type = pos_descr->dom->type;
      if (pr_type == NULL)
	{
	  return ER_FAILED;
	}

      OR_BUF_INIT (buf, ptr, length);
      /* read value from the tuple */
      if (pr_type->data_readval (&buf, *peek_dbval, pos_descr->dom, -1, false /* Don't copy */ , NULL, 0) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* next position pointer */
  *next_tpl = ptr + length;

  return NO_ERROR;
}

/*
 * fetch_peek_min_max_value_of_width_bucket_func () -
 *   return: NO_ERROR or ER_code
 *   regu_var(in): Regulator Variable of an ARITH node.
 *   vd(in): Value Descriptor
 *   obj_oid(in): Object Identifier
 *   tpl(in): Tuple
 *   min(out): the lower bound of width_bucket
 *   max(out): the upper bound of width_bucket
 */
static int
fetch_peek_min_max_value_of_width_bucket_func (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var, val_descr * vd,
					       OID * obj_oid, QFILE_TUPLE tpl, DB_VALUE ** min, DB_VALUE ** max)
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
  er_status = fetch_peek_dbval (thread_p, eval_term1->et.et_comp.rhs, vd, NULL, obj_oid, tpl, min);
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
  er_status = fetch_peek_dbval (thread_p, eval_term2->et.et_comp.rhs, vd, NULL, obj_oid, tpl, max);
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
 *   tpl(in): Tuple
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
		  QFILE_TUPLE tpl, DB_VALUE * dbval)
{
  int result;
  DB_VALUE *readonly_val, copy_val, *tmp;

  db_make_null (&copy_val);

  result = fetch_peek_dbval (thread_p, regu_var, vd, class_oid, obj_oid, tpl, &readonly_val);
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
 *   tpl(in): Tuple
 *   peek(int):
 */
int
fetch_val_list (THREAD_ENTRY * thread_p, regu_variable_list_node * regu_list, val_descr * vd, OID * class_oid,
		OID * obj_oid, QFILE_TUPLE tpl, int peek)
{
  regu_variable_list_node *regup;
  QFILE_TUPLE next_tpl;
  int rc, pos, next_pos;
  DB_VALUE *tmp;

  if (peek)
    {
      next_tpl = tpl + QFILE_TUPLE_LENGTH_SIZE;
      next_pos = 0;

      for (regup = regu_list; regup != NULL; regup = regup->next)
	{
	  if (regup->value.type == TYPE_POSITION)
	    {
	      pos = regup->value.value.pos_descr.pos_no;
	      if (pos >= next_pos)
		{
		  pos -= next_pos;
		  next_pos = regup->value.value.pos_descr.pos_no + 1;
		}
	      else
		{
		  next_tpl = tpl + QFILE_TUPLE_LENGTH_SIZE;
		  next_pos = 0;
		}

	      /* at fetch_peek_dbval_pos(), regup->value.vfetch_to is cleared */
	      rc = fetch_peek_dbval_pos (&regup->value, next_tpl, pos, &tmp, &next_tpl);
	    }
	  else
	    {
	      if (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (regup->value.vfetch_to)))
		{
		  pr_clear_value (regup->value.vfetch_to);
		}
	      rc = fetch_peek_dbval (thread_p, &regup->value, vd, class_oid, obj_oid, tpl, &tmp);
	    }

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
	  if (fetch_copy_dbval (thread_p, &regup->value, vd, class_oid, obj_oid, tpl, regup->value.vfetch_to) !=
	      NO_ERROR)
	    {
	      return ER_FAILED;
	    }
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
 *   tpl(in): Tuple
 *   peek(int):
 */
static bool
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

int
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

// *INDENT-OFF*
// C++ implementation stuff
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
        REGU_VARIABLE_SET_FLAG (&regu, REGU_VARIABLE_FETCH_NOT_CONST);
        break;
      default:
        break;
      }
    };
  reguvar.map_regu (map_func);
}
// *INDENT-ON*
