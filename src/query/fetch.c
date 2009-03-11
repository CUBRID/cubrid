/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
#if !defined(WINDOWS)
#include <sys/time.h>
#endif

#include "porting.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "db.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "oid.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "arithmetic.h"
#include "fetch.h"
#include "list_file.h"

/* this must be the last header file included!!! */
#include "dbval.h"

static int fetch_peek_arith (THREAD_ENTRY * thread_p,
			     REGU_VARIABLE * regu_var, VAL_DESCR * vd,
			     OID * obj_oid, QFILE_TUPLE tpl,
			     DB_VALUE ** peek_dbval);
static int fetch_peek_dbval_pos (REGU_VARIABLE * regu_var, QFILE_TUPLE tpl,
				 int pos, DB_VALUE ** peek_dbval,
				 QFILE_TUPLE * next_tpl);

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
fetch_peek_arith (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
		  VAL_DESCR * vd, OID * obj_oid, QFILE_TUPLE tpl,
		  DB_VALUE ** peek_dbval)
{
  ARITH_TYPE *arithptr = regu_var->value.arithptr;
  DB_VALUE *peek_left, *peek_right, *peek_third;

  peek_left = NULL;
  peek_right = NULL;
  peek_third = NULL;

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
    case T_TO_NUMBER:
    case T_INSTR:
      /* fetch lhs, rhs, and third value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr,
			    vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (!PRIM_IS_NULL (peek_left))
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr,
				vd, NULL, obj_oid, tpl,
				&peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	  if (fetch_peek_dbval (thread_p, arithptr->thirdptr,
				vd, NULL, obj_oid, tpl,
				&peek_third) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_ADD:
    case T_SUB:
    case T_MUL:
    case T_DIV:
    case T_MOD:
    case T_POSITION:
    case T_ADD_MONTHS:
    case T_MONTHS_BETWEEN:
    case T_TRIM:
    case T_LTRIM:
    case T_RTRIM:
    case T_POWER:
    case T_ROUND:
    case T_LOG:
    case T_TRUNC:
    case T_STRCAT:
    case T_NULLIF:
    case T_INCR:
    case T_DECR:
      /* fetch lhs and rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr,
			    vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (PRIM_IS_NULL (peek_left))
	{
	  if (PRM_ORACLE_STYLE_EMPTY_STRING
	      && (arithptr->opcode == T_STRCAT || arithptr->opcode == T_ADD))
	    {
	      /* check for result type. */
	      if (QSTR_IS_ANY_CHAR_OR_BIT (regu_var->domain->type->id))
		{
		  if (fetch_peek_dbval (thread_p, arithptr->rightptr,
					vd, NULL, obj_oid, tpl,
					&peek_right) != NO_ERROR)
		    {
		      goto error;
		    }
		}
	    }
	}
      else
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr,
				vd, NULL, obj_oid, tpl,
				&peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case T_COALESCE:
    case T_NVL:
    case T_NVL2:
      /* fetch only lhs value here */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr,
			    vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LEAST:
    case T_GREATEST:
      /* fetch both lhs and rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->leftptr,
			    vd, NULL, obj_oid, tpl, &peek_left) != NO_ERROR)
	{
	  goto error;
	}
      if (fetch_peek_dbval (thread_p, arithptr->rightptr,
			    vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CASE:
    case T_DECODE:
      /* defer fetch values */
      break;

    case T_LAST_DAY:
    case T_CURRENT_VALUE:
    case T_NEXT_VALUE:
    case T_UNPLUS:
    case T_UNMINUS:
    case T_OCTET_LENGTH:
    case T_BIT_LENGTH:
    case T_CHAR_LENGTH:
    case T_LOWER:
    case T_UPPER:
    case T_CAST:
    case T_EXTRACT:
    case T_FLOOR:
    case T_CEIL:
    case T_SIGN:
    case T_ABS:
    case T_CHR:
    case T_RANDOM:
    case T_DRANDOM:
    case T_EXP:
    case T_SQRT:
      /* fetch rhs value */
      if (fetch_peek_dbval (thread_p, arithptr->rightptr,
			    vd, NULL, obj_oid, tpl, &peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_RAND:
    case T_DRAND:
    case T_SYS_DATE:
    case T_SYS_TIME:
    case T_SYS_TIMESTAMP:
    case T_LOCAL_TRANSACTION_ID:
      /* nothing to fetch */
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto error;
    }

  /* evaluate arithmetic expression */

  /* clear any previous result */
  pr_clear_value (arithptr->value);
  switch (arithptr->opcode)
    {
    case T_ADD:
      {
	bool check_empty_string;

	check_empty_string = (PRM_ORACLE_STYLE_EMPTY_STRING ? true : false);

	/* check for result type. */
	if (check_empty_string
	    && QSTR_IS_ANY_CHAR_OR_BIT (regu_var->domain->type->id))
	  {
	    /* at here, T_ADD is really T_STRCAT */
	    if (qdata_strcat_dbval (peek_left, peek_right,
				    arithptr->value,
				    regu_var->domain) != NO_ERROR)
	      {
		goto error;
	      }
	  }
	else
	  {
	    if (qdata_add_dbval (peek_left, peek_right,
				 arithptr->value,
				 regu_var->domain) != NO_ERROR)
	      {
		goto error;
	      }
	  }
      }
      break;

    case T_SUB:
      if (qdata_subtract_dbval (peek_left, peek_right,
				arithptr->value,
				regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_MUL:
      if (qdata_multiply_dbval (peek_left, peek_right,
				arithptr->value,
				regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DIV:
      if (qdata_divide_dbval (peek_left, peek_right,
			      arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_UNPLUS:
      if (!qdata_copy_db_value (arithptr->value, peek_right))
	{
	  goto error;
	}
      break;

    case T_UNMINUS:
      if (qdata_unary_minus_dbval (arithptr->value, peek_right) != NO_ERROR)
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
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_floor_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CEIL:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_ceil_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SIGN:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_sign_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ABS:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_abs_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_EXP:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_exp_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SQRT:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_sqrt_dbval (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_POWER:
      if (db_power_dbval (arithptr->value,
			  regu_var->domain,
			  peek_left, peek_right) != NO_ERROR)
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
      if (peek_right == NULL || PRIM_IS_NULL (peek_right))
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
	  return NO_ERROR;
	}
      break;

    case T_TRUNC:
      if (db_trunc_dbval (arithptr->value, peek_left, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CHR:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_chr (arithptr->value, peek_right) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_INSTR:
      if (PRIM_IS_NULL (peek_left) || PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_instr (peek_left, peek_right, peek_third,
				arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_POSITION:
      if (PRIM_IS_NULL (peek_left) || PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_position (peek_left, peek_right,
				   arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SUBSTRING:
      if (PRIM_IS_NULL (peek_left) || PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_substring (arithptr->misc_operand,
				    peek_left, peek_right, peek_third,
				    arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_OCTET_LENGTH:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  db_make_int (arithptr->value, db_get_string_size (peek_right));
	}
      break;

    case T_BIT_LENGTH:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (PRIM_TYPE (peek_right) == DB_TYPE_BIT
	       || PRIM_TYPE (peek_right) == DB_TYPE_VARBIT)
	{
	  int len;

	  db_get_bit (peek_right, &len);
	  db_make_int (arithptr->value, len);
	}
      else
	{
	  /* must be a char string type */
	  db_make_int (arithptr->value,
		       8 * DB_GET_STRING_LENGTH (peek_right));
	}
      break;

    case T_CHAR_LENGTH:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else
	{
	  db_make_int (arithptr->value, DB_GET_STRING_LENGTH (peek_right));
	}
      break;

    case T_LOWER:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_lower (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_UPPER:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_upper (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TRIM:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_trim (arithptr->misc_operand,
			       peek_right, peek_left,
			       arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LTRIM:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_trim (LEADING,
			       peek_right, peek_left,
			       arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_RTRIM:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_trim (TRAILING,
			       peek_right, peek_left,
			       arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LPAD:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_pad (LEADING, peek_left, peek_right, peek_third,
			      arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_RPAD:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_pad (TRAILING, peek_left, peek_right, peek_third,
			      arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_REPLACE:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_replace (peek_left, peek_right, peek_third,
				  arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TRANSLATE:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_string_translate (peek_left, peek_right, peek_third,
				    arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_ADD_MONTHS:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_add_months (peek_left, peek_right,
			      arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LAST_DAY:
      if (PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_last_day (peek_right, arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_MONTHS_BETWEEN:
      if (PRIM_IS_NULL (peek_left) || PRIM_IS_NULL (peek_right))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_months_between (peek_left, peek_right,
				  arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_SYS_DATE:
      {
	DB_DATE db_date;

	db_timestamp_decode (&vd->sys_timestamp, &db_date, NULL);
	DB_MAKE_ENCODED_DATE (arithptr->value, &db_date);
      }
      break;

    case T_SYS_TIME:
      {
	DB_DATE db_time;

	db_timestamp_decode (&vd->sys_timestamp, NULL, &db_time);
	DB_MAKE_ENCODED_TIME (arithptr->value, &db_time);
      }
      break;

    case T_SYS_TIMESTAMP:
      db_make_timestamp (arithptr->value, vd->sys_timestamp);
      break;

    case T_LOCAL_TRANSACTION_ID:
      db_make_int (arithptr->value, logtb_find_current_tranid (thread_p));
      break;

    case T_TO_CHAR:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_char (peek_left, peek_right, peek_third,
			   arithptr->value) != NO_ERROR)
	{
	  goto error;
	}

      break;

    case T_TO_DATE:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_date (peek_left, peek_right, peek_third,
			   arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_TIME:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_time (peek_left, peek_right, peek_third,
			   arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_TIMESTAMP:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (db_to_timestamp (peek_left, peek_right, peek_third,
				arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_TO_NUMBER:
      if (PRIM_IS_NULL (peek_left))
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (DB_GET_INT (peek_third) == 1)
	{
	  peek_right->domain.general_info.type = DB_TYPE_NULL;
	  if (db_to_number (peek_left, 0, arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	  regu_var->domain->precision =
	    arithptr->value->domain.numeric_info.precision;
	  regu_var->domain->scale =
	    arithptr->value->domain.numeric_info.scale;
	}
      else
	{
	  if (db_to_number (peek_left, peek_right,
			    arithptr->value) != NO_ERROR)
	    {
	      goto error;
	    }
	  regu_var->domain->precision =
	    arithptr->value->domain.numeric_info.precision;
	  regu_var->domain->scale =
	    arithptr->value->domain.numeric_info.scale;
	}
      break;

    case T_CURRENT_VALUE:
      if (xqp_get_serial_current_value (thread_p, peek_right, arithptr->value)
	  != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_NEXT_VALUE:
      if (xqp_get_serial_next_value (thread_p, peek_right, arithptr->value) !=
	  NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_CAST:
      if (tp_value_cast (peek_right, arithptr->value,
			 arithptr->domain, false) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		  pr_type_name (PRIM_TYPE (peek_right)),
		  pr_type_name (arithptr->domain->type->id));
	  goto error;
	}
      break;

    case T_CASE:
    case T_DECODE:
      /* fetch values */
      switch (eval_pred (thread_p, arithptr->pred, vd, obj_oid))
	{
	case V_FALSE:
	case V_UNKNOWN:	/* unknown pred result, including cases of NULL pred operands */
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr,
				vd, NULL, obj_oid, tpl,
				&peek_left) != NO_ERROR)
	    {
	      goto error;
	    }
	  break;
	case V_TRUE:
	  if (fetch_peek_dbval (thread_p, arithptr->leftptr,
				vd, NULL, obj_oid, tpl,
				&peek_left) != NO_ERROR)
	    {
	      goto error;
	    }
	  break;
	default:
	  goto error;
	}

      if (tp_value_coerce (peek_left,
			   arithptr->value,
			   regu_var->domain) != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		  pr_type_name (PRIM_TYPE (peek_left)),
		  pr_type_name (arithptr->domain->type->id));
	  goto error;
	}
      break;

    case T_NULLIF:		/* when a = b then null else a end */
      if (tp_value_compare (peek_left, peek_right, 1, 0) == DB_EQ)
	{
	  PRIM_SET_NULL (arithptr->value);
	}
      else if (tp_value_coerce (peek_left,
				arithptr->value,
				regu_var->domain) != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		  pr_type_name (PRIM_TYPE (peek_left)),
		  pr_type_name (arithptr->domain->type->id));
	  goto error;
	}
      break;

    case T_COALESCE:
    case T_NVL:		/* when a is not null then a else b end */
      if (PRIM_IS_NULL (peek_left))
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr,
				vd, NULL, obj_oid, tpl,
				&peek_left) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (tp_value_coerce (peek_left,
			   arithptr->value,
			   regu_var->domain) != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		  pr_type_name (PRIM_TYPE (peek_left)),
		  pr_type_name (arithptr->domain->type->id));
	  goto error;
	}
      break;

    case T_NVL2:		/* when a is not null then b else c end */
      if (PRIM_IS_NULL (peek_left))
	{
	  if (fetch_peek_dbval (thread_p, arithptr->thirdptr,
				vd, NULL, obj_oid, tpl,
				&peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  if (fetch_peek_dbval (thread_p, arithptr->rightptr,
				vd, NULL, obj_oid, tpl,
				&peek_right) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (tp_value_coerce (peek_right,
			   arithptr->value,
			   regu_var->domain) != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		  pr_type_name (PRIM_TYPE (peek_right)),
		  pr_type_name (arithptr->domain->type->id));
	  goto error;
	}
      break;

    case T_EXTRACT:
      if (qdata_extract_dbval (arithptr->misc_operand, peek_right,
			       arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_LEAST:
      {
	int cmp_result;

	cmp_result = tp_value_compare (peek_left, peek_right, 1, 0);
	if (cmp_result == DB_EQ || cmp_result == DB_LT)
	  {
	    pr_clone_value (peek_left, arithptr->value);
	  }
	else if (cmp_result == DB_GT)
	  {
	    pr_clone_value (peek_right, arithptr->value);
	  }
	else
	  {
	    break;
	  }

	tp_value_coerce (arithptr->value, arithptr->value, regu_var->domain);
	break;
      }

    case T_GREATEST:
      {
	int cmp_result;

	cmp_result = tp_value_compare (peek_left, peek_right, 1, 0);
	if (cmp_result == DB_EQ || cmp_result == DB_GT)
	  {
	    pr_clone_value (peek_left, arithptr->value);
	  }
	else if (cmp_result == DB_LT)
	  {
	    pr_clone_value (peek_right, arithptr->value);
	  }
	else
	  {
	    break;
	  }

	tp_value_coerce (arithptr->value, arithptr->value, regu_var->domain);
	break;
      }

    case T_STRCAT:
      if (qdata_strcat_dbval (peek_left, peek_right,
			      arithptr->value, regu_var->domain) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_RAND:
      db_make_int (arithptr->value, (int) vd->lrand);
      break;

    case T_DRAND:
      db_make_double (arithptr->value, (double) vd->drand);
      break;

    case T_RANDOM:
      if (PRIM_IS_NULL (peek_right))
	{
	  struct timeval t;

	  gettimeofday (&t, NULL);
	  srand48 (t.tv_usec);
	  db_make_int (peek_right, t.tv_usec);
	}

      if (db_random_dbval (arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;

    case T_DRANDOM:
      if (PRIM_IS_NULL (peek_right))
	{
	  struct timeval t;

	  gettimeofday (&t, NULL);
	  srand48 (t.tv_usec);
	  db_make_int (peek_right, t.tv_usec);
	}

      if (db_drandom_dbval (arithptr->value) != NO_ERROR)
	{
	  goto error;
	}
      break;
    }

  *peek_dbval = arithptr->value;

  return NO_ERROR;

error:

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
fetch_peek_dbval (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
		  VAL_DESCR * vd, OID * class_oid, OID * obj_oid,
		  QFILE_TUPLE tpl, DB_VALUE ** peek_dbval)
{
  int length;
  PR_TYPE *pr_type;
  OR_BUF buf;
  QFILE_TUPLE_VALUE_FLAG flag;
  char *ptr;

  switch (regu_var->type)
    {
    case TYPE_ATTR_ID:		/* fetch object attribute value */
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      *peek_dbval = regu_var->value.attr_descr.cache_dbvalp;
      if (*peek_dbval != NULL)
	{
	  /* we have a cached pointer already */
	  break;
	}
      else
	{
	  *peek_dbval =
	    heap_attrinfo_access (regu_var->value.attr_descr.id,
				  regu_var->value.attr_descr.cache_attrinfo);
	  if (*peek_dbval == NULL)
	    {
	      goto error;
	    }
	}
      regu_var->value.attr_descr.cache_dbvalp = *peek_dbval;	/* cache */
      break;

    case TYPE_OID:		/* fetch object identifier value */
      *peek_dbval = &regu_var->value.dbval;
      DB_MAKE_OID (*peek_dbval, obj_oid);
      break;

    case TYPE_CLASSOID:	/* fetch class identifier value */
      *peek_dbval = &regu_var->value.dbval;
      DB_MAKE_OID (*peek_dbval, class_oid);
      break;

    case TYPE_POSITION:	/* fetch list file tuple value */
      pr_clear_value (regu_var->vfetch_to);

      *peek_dbval = regu_var->vfetch_to;

      flag =
	(QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tpl,
							   regu_var->value.
							   pos_descr.pos_no,
							   &ptr, &length);
      if (flag == V_BOUND)
	{
	  pr_type = regu_var->value.pos_descr.dom->type;
	  if (pr_type == NULL)
	    {
	      goto error;
	    }

	  OR_BUF_INIT (buf, ptr, length);

	  if ((*(pr_type->readval)) (&buf, *peek_dbval,
				     regu_var->value.pos_descr.dom, -1,
				     false /* Don't copy */ ,
				     NULL, 0) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      break;

    case TYPE_POS_VALUE:	/* fetch positional value */
#if defined(QP_DEBUG)
      if (regu_var->value.val_pos < 0 ||
	  regu_var->value.val_pos > (vd->dbval_cnt - 1))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_VALLIST_INDEX, 1, regu_var->value.val_pos);
	  goto error;
	}
#endif

      *peek_dbval = (DB_VALUE *) vd->dbval_ptr + regu_var->value.val_pos;
      break;

    case TYPE_CONSTANT:	/* fetch constant value */
      /* execute linked query */
      EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);
      if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
	{
	  goto error;
	}
      *peek_dbval = regu_var->value.dbvalptr;
      break;

    case TYPE_ORDERBY_NUM:
      *peek_dbval = regu_var->value.dbvalptr;
      break;

    case TYPE_DBVAL:		/* fetch db_value */
      *peek_dbval = &regu_var->value.dbval;
      break;

    case TYPE_INARITH:		/* compute and fetch arithmetic expr. value */
    case TYPE_OUTARITH:
      return fetch_peek_arith (thread_p, regu_var, vd, obj_oid, tpl,
			       peek_dbval);

    case TYPE_AGGREGATE:	/* fetch aggregation function value */
      /* The result value of the aggregate node MUST already have been evaluated */
      *peek_dbval = regu_var->value.aggptr->value;
      break;

    case TYPE_FUNC:		/* fetch function value */
      if (qdata_evaluate_function (thread_p, regu_var, vd, obj_oid, tpl) !=
	  NO_ERROR)
	{
	  goto error;
	}
      *peek_dbval = regu_var->value.funcp->value;
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto error;
    }

  return NO_ERROR;

error:

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
fetch_peek_dbval_pos (REGU_VARIABLE * regu_var, QFILE_TUPLE tpl,
		      int pos, DB_VALUE ** peek_dbval, QFILE_TUPLE * next_tpl)
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
	  goto error;
	}

      OR_BUF_INIT (buf, ptr, length);
      /* read value from the tuple */
      if ((*(pr_type->readval)) (&buf, *peek_dbval, pos_descr->dom,
				 -1, false /* Don't copy */ ,
				 NULL, 0) != NO_ERROR)
	{
	  goto error;
	}
    }

  /* next position pointer */
  *next_tpl = ptr + length;

  return NO_ERROR;

error:

  return ER_FAILED;
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
fetch_copy_dbval (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
		  VAL_DESCR * vd, OID * class_oid, OID * obj_oid,
		  QFILE_TUPLE tpl, DB_VALUE * dbval)
{
  int result;
  DB_VALUE *readonly_val, copy_val, *tmp;

  db_make_null (&copy_val);

  result = fetch_peek_dbval (thread_p, regu_var, vd, class_oid, obj_oid, tpl,
			     &readonly_val);
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
fetch_val_list (THREAD_ENTRY * thread_p, REGU_VARIABLE_LIST regu_list,
		VAL_DESCR * vd, OID * class_oid, OID * obj_oid,
		QFILE_TUPLE tpl, int peek)
{
  REGU_VARIABLE_LIST regup;
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
	      rc = fetch_peek_dbval_pos (&regup->value, next_tpl, pos, &tmp,
					 &next_tpl);
	    }
	  else
	    {
	      if (pr_is_set_type
		  (DB_VALUE_DOMAIN_TYPE (regup->value.vfetch_to)))
		{
		  pr_clear_value (regup->value.vfetch_to);
		}
	      rc =
		fetch_peek_dbval (thread_p, &regup->value, vd, class_oid,
				  obj_oid, tpl, &tmp);
	    }

	  if (rc != NO_ERROR)
	    {
	      pr_clear_value (regup->value.vfetch_to);
	      return ER_FAILED;
	    }
	  PR_SHARE_VALUE (tmp, regup->value.vfetch_to);
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
	  if (fetch_copy_dbval
	      (thread_p, &regup->value, vd, class_oid, obj_oid, tpl,
	       regup->value.vfetch_to) != NO_ERROR)
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
fetch_init_val_list (REGU_VARIABLE_LIST regu_list)
{
  REGU_VARIABLE_LIST regu_p;
  REGU_VARIABLE *regu_var;

  for (regu_p = regu_list; regu_p; regu_p = regu_p->next)
    {
      regu_var = &regu_p->value;
      regu_var->value.attr_descr.cache_dbvalp = NULL;
    }
}
