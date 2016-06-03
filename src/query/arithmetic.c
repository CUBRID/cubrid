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
 * arithmetic.c - arithmetic functions
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>
#if defined(SOLARIS)
#include <ieeefp.h>
#endif

#include "arithmetic.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "object_domain.h"
#include "numeric_opfunc.h"
#include "db.h"
#include "query_opfunc.h"
#include "crypt_opfunc.h"

/* this must be the last header file included!!! */
#include "dbval.h"

static int db_mod_short (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static int db_mod_int (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static int db_mod_bigint (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static int db_mod_float (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static int db_mod_double (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static int db_mod_string (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static int db_mod_numeric (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static int db_mod_monetary (DB_VALUE * value, DB_VALUE * value1, DB_VALUE * value2);
static double round_double (double num, double integer);
static int move_n_days (int *monthp, int *dayp, int *yearp, const int interval);
static int round_date (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2);
static double truncate_double (double num, double integer);
static DB_BIGINT truncate_bigint (DB_BIGINT num, DB_BIGINT integer);
static int truncate_date (DB_DATE * date, const DB_VALUE * format_str);
static int get_number_dbval_as_double (double *d, const DB_VALUE * value);
static int get_number_dbval_as_long_double (long double *ld, const DB_VALUE * value);
static int db_width_bucket_calculate_numeric (double *result, const DB_VALUE * value1, const DB_VALUE * value2,
					      const DB_VALUE * value3, const DB_VALUE * value4);

/*
 * db_floor_dbval () - take floor of db_value
 *   return: NO_ERROR
 *   result(out) : resultant db_value
 *   value(in)   : input db_value
 */
int
db_floor_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE res_type;
  double dtmp;
  int er_status = NO_ERROR;
  DB_VALUE cast_value;

  res_type = DB_VALUE_DOMAIN_TYPE (value);
  if (res_type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      return er_status;
    }

  switch (res_type)
    {
    case DB_TYPE_SHORT:
      DB_MAKE_SHORT (result, DB_GET_SHORT (value));
      break;
    case DB_TYPE_INTEGER:
      DB_MAKE_INT (result, DB_GET_INT (value));
      break;
    case DB_TYPE_BIGINT:
      DB_MAKE_BIGINT (result, DB_GET_BIGINT (value));
      break;
    case DB_TYPE_FLOAT:
      dtmp = floor (DB_GET_FLOAT (value));
      DB_MAKE_FLOAT (result, (float) dtmp);
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      DB_MAKE_NULL (&cast_value);
      er_status = tp_value_str_auto_cast_to_number (value, &cast_value, &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && res_type != DB_TYPE_DOUBLE))
	{
	  return er_status;
	}

      assert (res_type == DB_TYPE_DOUBLE);

      value = &cast_value;

      /* fall through */

    case DB_TYPE_DOUBLE:
      dtmp = floor (DB_GET_DOUBLE (value));
      DB_MAKE_DOUBLE (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      {
	int p = DB_VALUE_PRECISION (value), s = DB_VALUE_SCALE (value);

	if (s)
	  {
	    unsigned char num[DB_NUMERIC_BUF_SIZE];
	    char num_str[DB_MAX_NUMERIC_PRECISION * 4 + 2] = { '\0' };
	    char *num_str_p;
	    int num_str_len;
	    bool decrement = false;

	    num_str_p = num_str + 1;
	    numeric_coerce_num_to_dec_str (DB_PULL_NUMERIC (value), num_str_p);
	    num_str_len = strlen (num_str_p);

	    num_str_p += num_str_len - s;

	    while (*num_str_p)
	      {
		if (*num_str_p != '0')
		  {
		    *num_str_p = '0';
		    decrement = true;
		  }

		num_str_p++;
	      }

	    if (decrement && num_str[1] == '-')
	      {
		/* To decrement a negative value, the absolute value (the digits) actually has to be incremented. */

		char *num_str_digits = num_str + num_str_len - p;
		bool carry = true;

		num_str_p = num_str + num_str_len - s;
		while (*num_str_p == '9')
		  {
		    *num_str_p-- = '0';
		  }

		if (*num_str_p == '-')
		  {
		    num_str[0] = '-';
		    *num_str_p = '1';
		  }
		else
		  {
		    (*num_str_p)++;
		    carry = false;
		  }

		if (carry || num_str_p <= num_str_digits)
		  {
		    if (p < DB_MAX_NUMERIC_PRECISION)
		      {
			p++;
		      }
		    else
		      {
			s--;
			num_str[num_str_len] = '\0';
		      }
		  }

		if (num_str[0])
		  {
		    num_str_p = num_str;
		  }
		else
		  {
		    num_str_p = num_str + 1;
		  }

		numeric_coerce_dec_str_to_num (num_str_p, num);
		DB_MAKE_NUMERIC (result, num, p, s);
	      }
	    else
	      {
		/* given numeric is positive or already rounded */
		numeric_coerce_dec_str_to_num (num_str + 1, num);
		DB_MAKE_NUMERIC (result, num, p, s);
	      }
	  }
	else
	  {
	    /* given numeric number is already of integral type */
	    DB_MAKE_NUMERIC (result, DB_PULL_NUMERIC (value), p, 0);
	  }

	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      dtmp = floor (dtmp);
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type, dtmp);
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_status = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	}
      break;
    }

  return er_status;
}

/*
 * db_ceil_dbval () - take ceil of db_value
 *   return: NO_ERROR
 *   result(out) : resultant db_value
 *   value(in)   : input db_value
 */
int
db_ceil_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE res_type;
  double dtmp;
  int er_status = NO_ERROR;
  DB_VALUE cast_value;

  res_type = DB_VALUE_DOMAIN_TYPE (value);
  if (res_type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      return er_status;
    }

  switch (res_type)
    {
    case DB_TYPE_SHORT:
      DB_MAKE_SHORT (result, DB_GET_SHORT (value));
      break;
    case DB_TYPE_INTEGER:
      DB_MAKE_INT (result, DB_GET_INT (value));
      break;
    case DB_TYPE_BIGINT:
      DB_MAKE_BIGINT (result, DB_GET_BIGINT (value));
      break;
    case DB_TYPE_FLOAT:
      dtmp = ceil (DB_GET_FLOAT (value));
      DB_MAKE_FLOAT (result, (float) dtmp);
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      DB_MAKE_NULL (&cast_value);
      er_status = tp_value_str_auto_cast_to_number (value, &cast_value, &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && res_type != DB_TYPE_DOUBLE))
	{
	  return er_status;
	}

      assert (res_type == DB_TYPE_DOUBLE);

      value = &cast_value;

      /* fall through */

    case DB_TYPE_DOUBLE:
      dtmp = ceil (DB_GET_DOUBLE (value));
      DB_MAKE_DOUBLE (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      {
	int s = DB_VALUE_SCALE (value), p = DB_VALUE_PRECISION (value);

	if (s)
	  {
	    char num_str[DB_MAX_NUMERIC_PRECISION * 4 + 2] = { '\0' };
	    char *num_str_p;
	    int num_str_len = 0;
	    bool increment = false;

	    num_str_p = num_str + 1;
	    numeric_coerce_num_to_dec_str (db_locate_numeric (value), num_str_p);
	    if (num_str_p[0] == '-')
	      {
		num_str_p++;
	      }

	    num_str_len = strlen (num_str_p);
	    num_str_p += num_str_len - s;

	    while (*num_str_p)
	      {
		if (*num_str_p != '0')
		  {
		    increment = true;
		    *num_str_p = '0';
		  }

		num_str_p++;
	      }

	    if (increment)
	      {
		unsigned char num[DB_NUMERIC_BUF_SIZE];
		if (num_str[1] == '-')
		  {
		    /* CEIL(-3.1) is -3.0, as opposed to CEIL(+3.1) which is 4 */
		    numeric_coerce_dec_str_to_num (num_str + 1, num);
		    DB_MAKE_NUMERIC (result, num, p, s);
		  }
		else
		  {
		    bool carry = true;
		    char *num_str_digits = num_str + 1 + num_str_len - p;

		    /* position num_str_p one digit in front of the decimal point */
		    num_str_p = num_str;
		    num_str_p += num_str_len - s;

		    while (*num_str_p == '9')
		      {
			*num_str_p-- = '0';
		      }

		    if (*num_str_p)
		      {
			(*num_str_p)++;
			carry = false;
		      }

		    if (carry || num_str_p < num_str_digits)
		      {
			if (carry)
			  {
			    *num_str_p = '1';
			  }

			if (p < DB_MAX_NUMERIC_PRECISION)
			  {
			    p++;
			  }
			else
			  {
			    num_str[num_str_len] = '\0';
			    s--;
			  }
		      }
		    else
		      {
			num_str_p = num_str + 1;
		      }

		    numeric_coerce_dec_str_to_num (num_str_p, num);
		    DB_MAKE_NUMERIC (result, num, p, s);
		  }
	      }
	    else
	      {
		/* the given numeric value is already an integer */
		DB_MAKE_NUMERIC (result, db_locate_numeric (value), p, s);
	      }
	  }
	else
	  {
	    /* the given numeric value has a scale of 0 */
	    DB_MAKE_NUMERIC (result, db_locate_numeric (value), p, 0);
	  }

	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      dtmp = ceil (dtmp);
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type, dtmp);
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_status = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	}
      break;
    }

  return er_status;
}

/*
 * db_sign_dbval - take sign of db_value
 *   return: NO_ERROR
 *   result(out) : resultant db_value
 *   value(in)   : input db_value
 */
int
db_sign_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE res_type;
  int itmp;
  DB_BIGINT bitmp;
  double dtmp;
  int er_status = NO_ERROR;

  res_type = DB_VALUE_DOMAIN_TYPE (value);

  if (res_type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      return er_status;
    }

  switch (res_type)
    {
    case DB_TYPE_SHORT:
      itmp = DB_GET_SHORT (value);
      if (itmp == 0)
	{
	  DB_MAKE_INT (result, 0);
	}
      else if (itmp < 0)
	{
	  DB_MAKE_INT (result, -1);
	}
      else
	{
	  DB_MAKE_INT (result, 1);
	}
      break;
    case DB_TYPE_INTEGER:
      itmp = DB_GET_INTEGER (value);
      if (itmp == 0)
	{
	  DB_MAKE_INT (result, 0);
	}
      else if (itmp < 0)
	{
	  DB_MAKE_INT (result, -1);
	}
      else
	{
	  DB_MAKE_INT (result, 1);
	}
      break;
    case DB_TYPE_BIGINT:
      bitmp = DB_GET_BIGINT (value);
      if (bitmp == 0)
	{
	  DB_MAKE_INT (result, 0);
	}
      else if (bitmp < 0)
	{
	  DB_MAKE_INT (result, -1);
	}
      else
	{
	  DB_MAKE_INT (result, 1);
	}
      break;
    case DB_TYPE_FLOAT:
      dtmp = DB_GET_FLOAT (value);
      if (dtmp == 0)
	{
	  DB_MAKE_INT (result, 0);
	}
      else if (dtmp < 0)
	{
	  DB_MAKE_INT (result, -1);
	}
      else
	{
	  DB_MAKE_INT (result, 1);
	}
      break;
    case DB_TYPE_DOUBLE:
      dtmp = DB_GET_DOUBLE (value);
      if (dtmp == 0)
	{
	  DB_MAKE_INT (result, 0);
	}
      else if (dtmp < 0)
	{
	  DB_MAKE_INT (result, -1);
	}
      else
	{
	  DB_MAKE_INT (result, 1);
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value), DB_VALUE_SCALE (value), &dtmp);
      if (dtmp == 0)
	{
	  DB_MAKE_INT (result, 0);
	}
      else if (dtmp < 0)
	{
	  DB_MAKE_INT (result, -1);
	}
      else
	{
	  DB_MAKE_INT (result, 1);
	}
      break;
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      if (dtmp == 0)
	{
	  DB_MAKE_INT (result, 0);
	}
      else if (dtmp < 0)
	{
	  DB_MAKE_INT (result, -1);
	}
      else
	{
	  DB_MAKE_INT (result, 1);
	}
      break;
    default:
      break;
    }

  return er_status;
}

/*
 * db_abs_dbval () - take absolute value of db_value
 *   return: NO_ERROR
 *   result(out) : resultant db_value
 *   value(in)   : input db_value
 */
int
db_abs_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE res_type;
  short stmp;
  int itmp;
  DB_BIGINT bitmp;
  double dtmp;
  int er_status = NO_ERROR;
  DB_VALUE cast_value;

  res_type = DB_VALUE_DOMAIN_TYPE (value);
  if (res_type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      return er_status;
    }

  switch (res_type)
    {
    case DB_TYPE_SHORT:
      stmp = DB_GET_SHORT (value);
      stmp = abs (stmp);
      DB_MAKE_SHORT (result, stmp);
      break;
    case DB_TYPE_INTEGER:
      itmp = DB_GET_INT (value);
      itmp = abs (itmp);
      DB_MAKE_INT (result, itmp);
      break;
    case DB_TYPE_BIGINT:
      bitmp = DB_GET_BIGINT (value);
      bitmp = llabs (bitmp);
      DB_MAKE_BIGINT (result, bitmp);
      break;
    case DB_TYPE_FLOAT:
      dtmp = DB_GET_FLOAT (value);
      dtmp = fabs (dtmp);
      DB_MAKE_FLOAT (result, (float) dtmp);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      DB_MAKE_NULL (&cast_value);
      er_status = tp_value_str_auto_cast_to_number (value, &cast_value, &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && res_type != DB_TYPE_DOUBLE))
	{
	  return er_status;
	}

      assert (res_type == DB_TYPE_DOUBLE);

      value = &cast_value;

      /* fall through */

    case DB_TYPE_DOUBLE:
      dtmp = DB_GET_DOUBLE (value);
      dtmp = fabs (dtmp);
      DB_MAKE_DOUBLE (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      {
	unsigned char num[DB_NUMERIC_BUF_SIZE];

	numeric_db_value_abs (db_locate_numeric (value), num);
	DB_MAKE_NUMERIC (result, num, DB_VALUE_PRECISION (value), DB_VALUE_SCALE (value));
	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      dtmp = fabs (dtmp);
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type, dtmp);
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_status = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	}
      break;
    }

  return er_status;
}

/*
 * db_exp_dbval () - take exponential value of db_value
 *   return: NO_ERROR
 *   result(out) : resultant db_value
 *   value(in)   : input db_value
 */
int
db_exp_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  short s;
  int i;
  float f;
  double d;
  double dtmp;
  DB_BIGINT bi;

  type = DB_VALUE_DOMAIN_TYPE (value);

  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      return NO_ERROR;
    }

  switch (type)
    {
    case DB_TYPE_SHORT:
      s = DB_GET_SHORT (value);
      dtmp = exp ((double) s);
      break;
    case DB_TYPE_INTEGER:
      i = DB_GET_INT (value);
      dtmp = exp ((double) i);
      break;
    case DB_TYPE_BIGINT:
      bi = DB_GET_BIGINT (value);
      dtmp = exp ((double) bi);
      break;
    case DB_TYPE_FLOAT:
      f = DB_GET_FLOAT (value);
      dtmp = exp (f);
      break;
    case DB_TYPE_DOUBLE:
      d = DB_GET_DOUBLE (value);
      dtmp = exp (d);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value), DB_VALUE_SCALE (value), &d);
      dtmp = exp (d);
      break;
    case DB_TYPE_MONETARY:
      d = (DB_GET_MONETARY (value))->amount;
      dtmp = exp (d);
      break;
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      goto exp_overflow;
    }

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;

exp_overflow:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_EXP, 0);
  return ER_FAILED;
}

/*
 * db_sqrt_dbval () - take sqrt value of db_value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_sqrt_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  short s;
  int i;
  float f;
  double d;
  double dtmp;
  DB_BIGINT bi;

  type = DB_VALUE_DOMAIN_TYPE (value);

  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      return NO_ERROR;
    }

  switch (type)
    {
    case DB_TYPE_SHORT:
      s = DB_GET_SHORT (value);
      if (s < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt ((double) s);
      break;
    case DB_TYPE_INTEGER:
      i = DB_GET_INT (value);
      if (i < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt ((double) i);
      break;
    case DB_TYPE_BIGINT:
      bi = DB_GET_BIGINT (value);
      if (bi < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt ((double) bi);
      break;
    case DB_TYPE_FLOAT:
      f = DB_GET_FLOAT (value);
      if (f < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt (f);
      break;
    case DB_TYPE_DOUBLE:
      d = DB_GET_DOUBLE (value);
      if (d < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt (d);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value), DB_VALUE_SCALE (value), &d);
      if (d < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt (d);
      break;
    case DB_TYPE_MONETARY:
      d = (DB_GET_MONETARY (value))->amount;
      if (d < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt (d);
      break;
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
      break;
    }

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;

sqrt_error:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1, "sqrt()");
    }
  return ER_FAILED;
}

/*
 * db_power_dbval () - take power value of db_value
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : first db_value
 *   value2(in)  : second db_value
 */
int
db_power_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
  double d1, d2;
  double dtmp;
  int error = NO_ERROR;

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  error = get_number_dbval_as_double (&d1, value1);
  if (error != NO_ERROR)
    {
      goto pow_error;
    }
  error = get_number_dbval_as_double (&d2, value2);
  if (error != NO_ERROR)
    {
      goto pow_error;
    }

  if (d1 < 0 && d2 != ceil (d2))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_POWER_ERROR, 0);
      goto pow_error;
    }

  dtmp = pow (d1, d2);
  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      goto pow_overflow;
    }

  DB_MAKE_DOUBLE (result, dtmp);

  return NO_ERROR;

pow_overflow:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_POWER, 0);
      return ER_FAILED;
    }

pow_error:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }
}

/*
 * db_mod_short () - take mod value of value1(short) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : short db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_short (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
#if !defined(NDEBUG)
  DB_TYPE type1;
#endif
  DB_TYPE type2;
  short s1, s2;
  int i2;
  float f2;
  double d2;
  DB_BIGINT bi2;
  double dtmp;
  DB_DATA_STATUS data_stat;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int p, s;
  int er_status = NO_ERROR;
  DB_VALUE cast_value2;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_SHORT);
#endif

  s1 = DB_GET_SHORT (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = DB_GET_SHORT (value2);
      if (s2 == 0)
	{
	  DB_MAKE_SHORT (result, s1);
	}
      else
	{
	  DB_MAKE_SHORT (result, (short) (s1 % s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = DB_GET_INT (value2);
      if (i2 == 0)
	{
	  DB_MAKE_INT (result, s1);
	}
      else
	{
	  DB_MAKE_INT (result, (int) (s1 % i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = DB_GET_BIGINT (value2);
      if (bi2 == 0)
	{
	  DB_MAKE_BIGINT (result, s1);
	}
      else
	{
	  DB_MAKE_BIGINT (result, (DB_BIGINT) (s1 % bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = DB_GET_FLOAT (value2);
      if (f2 == 0)
	{
	  DB_MAKE_FLOAT (result, s1);
	}
      else
	{
	  DB_MAKE_FLOAT (result, (float) fmod (s1, f2));
	}
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2, &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type2 != DB_TYPE_DOUBLE))
	{
	  goto exit;
	}

      assert (type2 == DB_TYPE_DOUBLE);

      value2 = &cast_value2;

      /* fall through */

    case DB_TYPE_DOUBLE:
      d2 = DB_GET_DOUBLE (value2);
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, s1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (s1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (s1, d2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2), num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, s1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, (double) fmod (s1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

exit:
  return er_status;
}

/*
 * db_mod_int () - take mod value of value1(int) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : int db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_int (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
#if !defined(NDEBUG)
  DB_TYPE type1;
#endif
  DB_TYPE type2;
  short s2;
  int i1, i2;
  float f2;
  double d2;
  DB_BIGINT bi2;
  double dtmp;
  DB_DATA_STATUS data_stat;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int p, s;
  int er_status = NO_ERROR;
  DB_VALUE cast_value2;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_INTEGER);
#endif

  i1 = DB_GET_INT (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = DB_GET_SHORT (value2);
      if (s2 == 0)
	{
	  DB_MAKE_INT (result, i1);
	}
      else
	{
	  DB_MAKE_INT (result, (int) (i1 % s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = DB_GET_INT (value2);
      if (i2 == 0)
	{
	  DB_MAKE_INT (result, i1);
	}
      else
	{
	  DB_MAKE_INT (result, (int) (i1 % i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = DB_GET_BIGINT (value2);
      if (bi2 == 0)
	{
	  DB_MAKE_BIGINT (result, i1);
	}
      else
	{
	  DB_MAKE_BIGINT (result, (DB_BIGINT) (i1 % bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = DB_GET_FLOAT (value2);
      if (f2 == 0)
	{
	  DB_MAKE_FLOAT (result, (float) i1);
	}
      else
	{
	  DB_MAKE_FLOAT (result, (float) fmod (i1, f2));
	}
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2, &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type2 != DB_TYPE_DOUBLE))
	{
	  goto exit;
	}

      assert (type2 == DB_TYPE_DOUBLE);

      value2 = &cast_value2;

      /* fall through */

    case DB_TYPE_DOUBLE:
      d2 = DB_GET_DOUBLE (value2);
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, i1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (i1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (i1, d2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2), num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, i1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, (double) fmod (i1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

exit:
  return er_status;
}

/*
 * db_mod_bigint () - take mod value of value1(bigint) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : bigint db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_bigint (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
#if !defined(NDEBUG)
  DB_TYPE type1;
#endif
  DB_TYPE type2;
  short s2;
  int i2;
  float f2;
  double d2;
  DB_BIGINT bi1, bi2;
  double dtmp;
  DB_DATA_STATUS data_stat;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int p, s;
  int er_status = NO_ERROR;
  DB_VALUE cast_value2;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_BIGINT);
#endif

  bi1 = DB_GET_BIGINT (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = DB_GET_SHORT (value2);
      if (s2 == 0)
	{
	  DB_MAKE_BIGINT (result, bi1);
	}
      else
	{
	  DB_MAKE_BIGINT (result, (DB_BIGINT) (bi1 % s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = DB_GET_INT (value2);
      if (i2 == 0)
	{
	  DB_MAKE_BIGINT (result, bi1);
	}
      else
	{
	  DB_MAKE_BIGINT (result, (DB_BIGINT) (bi1 % i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = DB_GET_BIGINT (value2);
      if (bi2 == 0)
	{
	  DB_MAKE_BIGINT (result, bi1);
	}
      else
	{
	  DB_MAKE_BIGINT (result, (DB_BIGINT) (bi1 % bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = DB_GET_FLOAT (value2);
      if (f2 == 0)
	{
	  DB_MAKE_FLOAT (result, (float) bi1);
	}
      else
	{
	  DB_MAKE_FLOAT (result, (float) fmod ((double) bi1, f2));
	}
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2, &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type2 != DB_TYPE_DOUBLE))
	{
	  goto exit;
	}

      assert (type2 == DB_TYPE_DOUBLE);

      value2 = &cast_value2;

      /* fall through */

    case DB_TYPE_DOUBLE:
      d2 = DB_GET_DOUBLE (value2);
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, (double) bi1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod ((double) bi1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod ((double) bi1, d2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2), num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, (double) bi1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, (double) fmod ((double) bi1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

exit:
  return er_status;
}

/*
 * db_mod_float () - take mod value of value1(float) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : float db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_float (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
#if !defined(NDEBUG)
  DB_TYPE type1;
#endif
  DB_TYPE type2;
  short s2;
  int i2;
  float f1, f2;
  double d2;
  DB_BIGINT bi2;
  int er_status = NO_ERROR;
  DB_VALUE cast_value2;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_FLOAT);
#endif

  f1 = DB_GET_FLOAT (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = DB_GET_SHORT (value2);
      if (s2 == 0)
	{
	  DB_MAKE_FLOAT (result, f1);
	}
      else
	{
	  DB_MAKE_FLOAT (result, (float) fmod (f1, s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = DB_GET_INT (value2);
      if (i2 == 0)
	{
	  DB_MAKE_FLOAT (result, f1);
	}
      else
	{
	  DB_MAKE_FLOAT (result, (float) fmod (f1, i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = DB_GET_BIGINT (value2);
      if (bi2 == 0)
	{
	  DB_MAKE_FLOAT (result, f1);
	}
      else
	{
	  DB_MAKE_FLOAT (result, (float) fmod (f1, (double) bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = DB_GET_FLOAT (value2);
      if (f2 == 0)
	{
	  DB_MAKE_FLOAT (result, f1);
	}
      else
	{
	  DB_MAKE_FLOAT (result, (float) fmod (f1, f2));
	}
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2, &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type2 != DB_TYPE_DOUBLE))
	{
	  goto exit;
	}

      assert (type2 == DB_TYPE_DOUBLE);

      value2 = &cast_value2;

      /* fall through */

    case DB_TYPE_DOUBLE:
      d2 = DB_GET_DOUBLE (value2);
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, f1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod ((double) f1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      /* common type of float and numeric is double. */
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, f1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, fmod (f1, d2));
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, f1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, (double) fmod ((double) f1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

exit:
  return er_status;
}

/*
 * db_mod_double () - take mod value of value1(double) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : double db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_double (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
#if !defined(NDEBUG)
  DB_TYPE type1;
#endif
  DB_TYPE type2;
  short s2;
  int i2;
  float f2;
  double d1, d2;
  DB_BIGINT bi2;
  int er_status = NO_ERROR;
  DB_VALUE cast_value2;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_DOUBLE);
#endif

  d1 = DB_GET_DOUBLE (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = DB_GET_SHORT (value2);
      if (s2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (d1, s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = DB_GET_INT (value2);
      if (i2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (d1, i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = DB_GET_BIGINT (value2);
      if (bi2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (d1, (double) bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = DB_GET_FLOAT (value2);
      if (f2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (d1, (double) f2));
	}
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2, &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type2 != DB_TYPE_DOUBLE))
	{
	  goto exit;
	}

      assert (type2 == DB_TYPE_DOUBLE);

      value2 = &cast_value2;

      /* fall through */

    case DB_TYPE_DOUBLE:
      d2 = DB_GET_DOUBLE (value2);
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (d1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (d1, d2));
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, d1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, (double) fmod (d1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

exit:
  return er_status;
}

/*
 * db_mod_string () - take mod value of value1(string) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : string db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_string (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
  DB_TYPE type1;
  int er_status = NO_ERROR;
  DB_VALUE cast_value1;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value1);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (TP_IS_CHAR_TYPE (type1));
#endif

  er_status = tp_value_str_auto_cast_to_number (value1, &cast_value1, &type1);
  if (er_status != NO_ERROR
      || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type1 != DB_TYPE_DOUBLE))
    {
      return er_status;
    }

  assert (type1 == DB_TYPE_DOUBLE);

  value1 = &cast_value1;

  return db_mod_double (result, value1, value2);
}

/*
 * db_mod_numeric () - take mod value of value1(numeric) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : numeric db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_numeric (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
#if !defined(NDEBUG)
  DB_TYPE type1;
#endif
  DB_TYPE type2;
  short s2;
  int i2;
  float f2;
  double d1, d2;
  DB_BIGINT bi2;
  double dtmp;
  DB_DATA_STATUS data_stat;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int p, s;
  int er_status = NO_ERROR;
  DB_VALUE cast_value2;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_NUMERIC);
#endif

  numeric_coerce_num_to_double (db_locate_numeric (value1), DB_VALUE_SCALE (value1), &d1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = DB_GET_SHORT (value2);
      if (s2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, s2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1), num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = DB_GET_INT (value2);
      if (i2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, i2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1), num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = DB_GET_BIGINT (value2);
      if (bi2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, (double) bi2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1), num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = DB_GET_FLOAT (value2);
      /* common type of float and numeric is double */
      if (f2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, fmod (d1, f2));
	}
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2, &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type2 != DB_TYPE_DOUBLE))
	{
	  goto exit;
	}

      assert (type2 == DB_TYPE_DOUBLE);

      value2 = &cast_value2;

      /* fall through */

    case DB_TYPE_DOUBLE:
      d2 = DB_GET_DOUBLE (value2);
      if (d2 == 0)
	{
	  DB_MAKE_DOUBLE (result, d1);
	}
      else
	{
	  DB_MAKE_DOUBLE (result, (double) fmod (d1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, d2);
	  (void) numeric_internal_double_to_num (dtmp, MAX (DB_VALUE_SCALE (value1), DB_VALUE_SCALE (value2)), num, &p,
						 &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, d1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value2))->type, (double) fmod (d1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

exit:
  return er_status;
}

/*
 * db_mod_monetary () - take mod value of value1(monetary) with value2
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : monetary db_value
 *   value2(in)  : second db_value
 */
static int
db_mod_monetary (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
#if !defined(NDEBUG)
  DB_TYPE type1;
#endif
  DB_TYPE type2;
  double d1, d2;
  int er_status = NO_ERROR;
  DB_VALUE cast_value2;

  assert (result != NULL && value1 != NULL && value2 != NULL);

  DB_MAKE_NULL (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_MONETARY);
#endif

  d1 = (DB_GET_MONETARY (value1))->amount;
  d2 = 0;

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      d2 = DB_GET_SHORT (value2);
      break;
    case DB_TYPE_INTEGER:
      d2 = DB_GET_INT (value2);
      break;
    case DB_TYPE_BIGINT:
      d2 = (double) DB_GET_BIGINT (value2);
      break;
    case DB_TYPE_FLOAT:
      d2 = DB_GET_FLOAT (value2);
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2, &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type2 != DB_TYPE_DOUBLE))
	{
	  goto exit;
	}

      assert (type2 == DB_TYPE_DOUBLE);

      value2 = &cast_value2;

      /* fall through */

    case DB_TYPE_DOUBLE:
      d2 = DB_GET_DOUBLE (value2);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

  if (d2 == 0)
    {
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value1))->type, d1);
    }
  else
    {
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value1))->type, (double) fmod (d1, d2));
    }

exit:
  return er_status;
}

/*
 * db_mod_dbval () - take mod value of db_value
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   value1(in)  : first db_value
 *   value2(in)  : second db_value
 */
int
db_mod_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
  DB_TYPE type1;

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  switch (type1)
    {
    case DB_TYPE_SHORT:
      return db_mod_short (result, value1, value2);

    case DB_TYPE_INTEGER:
      return db_mod_int (result, value1, value2);

    case DB_TYPE_BIGINT:
      return db_mod_bigint (result, value1, value2);

    case DB_TYPE_FLOAT:
      return db_mod_float (result, value1, value2);

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      return db_mod_string (result, value1, value2);

    case DB_TYPE_DOUBLE:
      return db_mod_double (result, value1, value2);

    case DB_TYPE_NUMERIC:
      return db_mod_numeric (result, value1, value2);

    case DB_TYPE_MONETARY:
      return db_mod_monetary (result, value1, value2);

    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
      else
	{
	  return NO_ERROR;
	}
    }
}

/*
 * round_double ()
 *   return: num rounded to integer places to the right of the decimal point
 *   num(in)    :
 *   integer(in):
 */
static double
round_double (double num, double integer)
{
  /* 
   * Under high optimization level, some optimizers (e.g, gcc -O3 on linux)
   * generates a wrong result without "volatile".
   */
  volatile double scale_down, num_scale_up, result;

  if (num == 0)
    {
      return num;
    }

  scale_down = pow (10, -integer);
  num_scale_up = num / scale_down;
  if (!FINITE (num_scale_up))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, tp_Double_domain.type->name);
    }

  if (num_scale_up > 0)
    {
      result = floor (num_scale_up + 0.5) * scale_down;
    }
  else
    {
      result = ceil (num_scale_up - 0.5) * scale_down;
    }

  return result;
}

/*
 * move_n_days () - move forward or backward n days from a given date,
 *
 *   return: error code
 *   yearp(in/out):
 *   monthp(in/out) :
 *   dayp(in/out) :
 *   interval(in) : how many days to move, negative number means back
 */
static int
move_n_days (int *monthp, int *dayp, int *yearp, const int interval)
{
  /* no need to judge if arguments are illegal as it has been done previously */
  DB_DATE date;
  int error;

  error = db_date_encode (&date, *monthp, *dayp, *yearp);
  if (error != NO_ERROR)
    {
      return error;
    }
  date += interval;
  db_date_decode (&date, monthp, dayp, yearp);

  return NO_ERROR;
}

/*
 * db_round_date () - returns a date round by value2('year' | 'month' | 'day')
 *
 *   return: NO_ERROR, ER_FAILED
 *   result(out): resultant db_value
 *   value1(in) : first db_value
 *   value2(in) : second db_value
 */

static int
round_date (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
  DB_DATETIME *pdatetime, local_dt;
  DB_DATETIMETZ *pdatetimetz;
  DB_TIMESTAMP *pstamp;
  DB_TIMESTAMPTZ *pstamptz;
  DB_DATE *pdate;
  DB_DATE date;
  DB_TIME time;
  DB_TYPE type;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;
  int weekday = 0;
  int error = NO_ERROR;
  TIMESTAMP_FORMAT format;

  /* get format */
  error = db_get_date_format (value2, &format);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* get all values in the date */
  type = DB_VALUE_DOMAIN_TYPE (value1);

  if (type == DB_TYPE_TIMESTAMP || type == DB_TYPE_TIMESTAMPLTZ)
    {
      pstamp = db_get_timestamp (value1);
      (void) db_timestamp_decode_ses (pstamp, &date, &time);
      db_date_decode (&date, &month, &day, &year);
      db_time_decode (&time, &hour, &minute, &second);
    }
  else if (type == DB_TYPE_TIMESTAMPTZ)
    {
      pstamptz = db_get_timestamptz (value1);
      error = db_timestamp_decode_w_tz_id (&pstamptz->timestamp, &pstamptz->tz_id, &date, &time);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      db_date_decode (&date, &month, &day, &year);
      db_time_decode (&time, &hour, &minute, &second);
    }
  else if (type == DB_TYPE_DATETIME)
    {
      pdatetime = db_get_datetime (value1);
      db_datetime_decode (pdatetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
    }
  else if (type == DB_TYPE_DATETIMELTZ)
    {
      pdatetime = db_get_datetime (value1);

      error = tz_datetimeltz_to_local (pdatetime, &local_dt);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      db_datetime_decode (&local_dt, &month, &day, &year, &hour, &minute, &second, &millisecond);
    }
  else if (type == DB_TYPE_DATETIMETZ)
    {
      pdatetimetz = db_get_datetimetz (value1);

      error = tz_utc_datetimetz_to_local (&pdatetimetz->datetime, &pdatetimetz->tz_id, &local_dt);

      if (error != NO_ERROR)
	{
	  goto end;
	}

      db_datetime_decode (&local_dt, &month, &day, &year, &hour, &minute, &second, &millisecond);
    }
  else if (type == DB_TYPE_DATE)
    {
      pdate = db_get_date (value1);
      db_date_decode (pdate, &month, &day, &year);
      hour = minute = second = millisecond = 0;
    }
  else
    {
      error = ER_QPROC_INVALID_DATATYPE;
      goto end;
    }

  /* apply round according to format */
  switch (format)
    {
    case DT_YYYY:
    case DT_YY:
      if (month >= 7)		/* rounds up on July 1 */
	{
	  year++;
	}
      month = 1;
      day = 1;
      break;
    case DT_MONTH:
    case DT_MON:
    case DT_MM:
      if (day >= 16)		/* rounds up on the 16th days */
	{
	  if (month == 12)
	    {
	      year++;
	      month = 1;
	    }
	  else
	    {
	      month++;
	    }
	}
      day = 1;
      break;
    case DT_DD:
      if (hour >= 12)		/* rounds up on 12:00 AM */
	{
	  error = move_n_days (&month, &day, &year, 1);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	}
      break;
    case DT_HH24:
    case DT_HH12:
    case DT_HH:
    case DT_H:
      if (minute >= 30)		/* rounds up on HH:30 */
	{
	  if (++hour == 24)	/* rounds up to next day */
	    {
	      error = move_n_days (&month, &day, &year, 1);
	      if (error != NO_ERROR)
		{
		  goto end;
		}
	    }
	}
      break;
    case DT_MI:
      if (second >= 30)		/* rounds up on HH:MM:30 */
	{
	  if (++minute == 60)
	    {
	      if (++hour == 24)
		{
		  error = move_n_days (&month, &day, &year, 1);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }
		}
	    }
	}
      break;
    case DT_SS:
      if (millisecond >= 500)	/* rounds up on HH:MM:SS.500 */
	{
	  if (++second == 60)
	    {
	      if (++minute == 60)
		{
		  if (++hour == 24)
		    {
		      error = move_n_days (&month, &day, &year, 1);
		      if (error != NO_ERROR)
			{
			  goto end;
			}
		    }
		}
	    }
	}
      break;
    case DT_MS:		/* do nothing */
      break;
    case DT_Q:			/* quarter */
      /* rounds up on the 16th day of the second month of the quarter */
      if (month < 2 || (month == 2 && day < 16))
	{
	  month = 1;
	}
      else if (month < 5 || (month == 5 && day < 16))
	{
	  month = 4;
	}
      else if (month < 8 || (month == 8 && day < 16))
	{
	  month = 7;
	}
      else if (month < 11 || (month == 11 && day < 16))
	{
	  month = 10;
	}
      else
	{
	  month = 1;
	  year++;
	}
      day = 1;
      break;
    case DT_DAY:
    case DT_DY:		/* rounds up on thursday of a week */
    case DT_D:
      if (hour >= 12)		/* first round 'dd' */
	{
	  error = move_n_days (&month, &day, &year, 1);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	}

      weekday = day_of_week (julian_encode (month, day, year));
      if (weekday < 4)
	{
	  error = move_n_days (&month, &day, &year, -weekday);
	}
      else
	{
	  error = move_n_days (&month, &day, &year, 7 - weekday);
	}
      if (error != NO_ERROR)
	{
	  goto end;
	}
      break;
    case DT_CC:
    default:
      error = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  /* check for boundary and throw overflow */
  if (year < 0 || year > 9999)
    {
      error = ER_IT_DATA_OVERFLOW;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, tp_Date_domain.type->name);
      goto end;
    }

  /* re-create new date */
  error = DB_MAKE_DATE (result, month, day, year);

end:
  if (error != NO_ERROR && prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      error = NO_ERROR;
      DB_MAKE_NULL (result);
      er_clear ();
    }

  return error;
}

/*
 * db_round_dbval () - returns value1 rounded to value2 places right of
 *                     the decimal point
 *   return: NO_ERROR, ER_FAILED
 *   result(out): resultant db_value
 *   value1(in) : first db_value
 *   value2(in) : second db_value
 */
int
db_round_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
  DB_TYPE type1, type2;
  short s1;
  int i1;
  float f1;
  double d1, d2 = 0.0;
  DB_BIGINT bi1, bi2, bi_tmp;
  double dtmp;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  char num_string[(2 * DB_MAX_NUMERIC_PRECISION) + 4];
  char *ptr, *end;
  int need_round = 0;
  int p, s;
  DB_VALUE cast_value, cast_format;
  int er_status = NO_ERROR;
  TP_DOMAIN *domain = NULL;

  DB_MAKE_NULL (&cast_value);
  DB_MAKE_NULL (&cast_format);

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  type2 = DB_VALUE_DOMAIN_TYPE (value2);

  /* first check if round a date: */
  if (type1 == DB_TYPE_DATETIME || type1 == DB_TYPE_DATETIMELTZ || type1 == DB_TYPE_DATETIMETZ
      || type1 == DB_TYPE_TIMESTAMP || type1 == DB_TYPE_TIMESTAMPLTZ || type1 == DB_TYPE_TIMESTAMPTZ
      || type1 == DB_TYPE_DATE)
    {				/* round date */
      if (QSTR_IS_ANY_CHAR (type2) && strcasecmp (DB_GET_STRING_SAFE (value2), "default") == 0)
	{
	  DB_MAKE_STRING (&cast_format, "dd");
	  value2 = &cast_format;
	}
      return round_date (result, value1, value2);
    }

  /* cast value1 to double */
  if (!TP_IS_NUMERIC_TYPE (type1))
    {
      type1 = DB_TYPE_UNKNOWN;
      /* try type double */
      domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
      if (tp_value_coerce (value1, &cast_value, domain) != DOMAIN_COMPATIBLE)
	{
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      er_clear ();
	      return NO_ERROR;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	      return ER_QPROC_INVALID_DATATYPE;
	    }
	}
      type1 = DB_TYPE_DOUBLE;
      value1 = &cast_value;
    }

  /* get value2 */
  if (type2 == DB_TYPE_INTEGER)
    {
      d2 = (double) DB_GET_INT (value2);
    }
  else if (type2 == DB_TYPE_BIGINT)
    {
      d2 = (double) DB_GET_BIGINT (value2);
    }
  else if (type2 == DB_TYPE_SHORT)
    {
      d2 = (double) DB_GET_SHORT (value2);
    }
  else if (type2 == DB_TYPE_DOUBLE)
    {
      d2 = DB_GET_DOUBLE (value2);
    }
  else				/* cast to INTEGER */
    {
      if (QSTR_IS_ANY_CHAR (type2) && strcasecmp (DB_GET_STRING_SAFE (value2), "default") == 0)
	{
	  DB_MAKE_INT (&cast_format, 0);
	  value2 = &cast_format;
	  type2 = DB_TYPE_INTEGER;
	  d2 = 0;
	}
      else
	{
	  /* try type int */
	  type2 = DB_TYPE_UNKNOWN;
	  domain = tp_domain_resolve_default (DB_TYPE_INTEGER);
	  if (tp_value_coerce (value2, &cast_format, domain) != DOMAIN_COMPATIBLE)
	    {
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
		{
		  er_clear ();
		  return NO_ERROR;
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
		  return ER_QPROC_INVALID_DATATYPE;
		}
	    }
	  type2 = DB_TYPE_INTEGER;
	  value2 = &cast_format;
	  d2 = DB_GET_INTEGER (value2);
	}
    }

  /* round double */
  switch (type1)
    {
    case DB_TYPE_SHORT:
      s1 = DB_GET_SHORT (value1);
      dtmp = round_double (s1, d2);
      DB_MAKE_SHORT (result, (short) dtmp);
      break;
    case DB_TYPE_INTEGER:
      i1 = DB_GET_INT (value1);
      dtmp = round_double (i1, d2);
      DB_MAKE_INT (result, (int) dtmp);
      break;
    case DB_TYPE_BIGINT:
      bi1 = DB_GET_BIGINT (value1);
      dtmp = round_double ((double) bi1, d2);
      bi_tmp = (DB_BIGINT) dtmp;
#if defined(AIX)
      /* in AIX, double to long will not overflow, make it the same as linux. */
      if (dtmp == (double) DB_BIGINT_MAX)
	{
	  bi_tmp = DB_BIGINT_MIN;
	}
#endif
      DB_MAKE_BIGINT (result, bi_tmp);
      break;
    case DB_TYPE_FLOAT:
      f1 = DB_GET_FLOAT (value1);
      dtmp = round_double (f1, d2);
      DB_MAKE_FLOAT (result, (float) dtmp);
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      DB_MAKE_NULL (&cast_value);
      er_status = tp_value_str_auto_cast_to_number (value1, &cast_value, &type1);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && type1 != DB_TYPE_DOUBLE))
	{
	  return er_status;
	}

      assert (type1 == DB_TYPE_DOUBLE);
      value1 = &cast_value;

      /* fall through */
    case DB_TYPE_DOUBLE:
      d1 = DB_GET_DOUBLE (value1);
      dtmp = round_double (d1, d2);
      DB_MAKE_DOUBLE (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      memset (num_string, 0, sizeof (num_string));
      numeric_coerce_num_to_dec_str (db_locate_numeric (value1), num_string);
      p = DB_VALUE_PRECISION (value1);
      s = DB_VALUE_SCALE (value1);
      end = num_string + strlen (num_string);

      if (type2 == DB_TYPE_BIGINT)
	{
	  bi2 = DB_GET_BIGINT (value2);
	}
      else if (type2 == DB_TYPE_INTEGER)
	{
	  bi2 = DB_GET_INT (value2);
	}
      else if (type2 == DB_TYPE_SHORT)
	{
	  bi2 = DB_GET_SHORT (value2);
	}
      else			/* double */
	{
	  bi2 = (DB_BIGINT) DB_GET_DOUBLE (value2);
	}
      ptr = end - s + bi2;

      if (end < ptr)
	{			/* no need to round, return as it is */
	  *result = *value1;
	  break;
	}
      else if (ptr < num_string)
	{			/* return zero */
	  memset (num_string, 0, sizeof (num_string));
	}
      else
	{
	  if (*ptr >= '5')
	    {
	      need_round = 1;
	    }
	  while (ptr < end)
	    {
	      *ptr++ = '0';
	    }
	  if (need_round)
	    {
	      /* round up */
	      int done = 0;

	      for (ptr = end - s + bi2 - 1; ptr >= num_string && !done; ptr--)
		{
		  if (*ptr == '9')
		    {
		      *ptr = '0';
		    }
		  else
		    {
		      *ptr += 1;
		      done = 1;
		    }
		}

	      for (ptr = num_string; ptr < end; ptr++)
		{
		  if ('1' <= *ptr && *ptr <= '9')
		    {
		      if (strlen (ptr) > DB_MAX_NUMERIC_PRECISION)
			{
			  /* overflow happened during round up */
			  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
			    {
			      er_clear ();
			      return NO_ERROR;
			    }
			  else
			    {
			      domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
				      pr_type_name (TP_DOMAIN_TYPE (domain)));
			      return ER_IT_DATA_OVERFLOW;
			    }
			}
		      break;
		    }
		}
	    }
	}

      numeric_coerce_dec_str_to_num (num_string, num);
      DB_MAKE_NUMERIC (result, num, p, s);
      break;
    case DB_TYPE_MONETARY:
      d1 = (DB_GET_MONETARY (value1))->amount;
      dtmp = round_double (d1, d2);
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value1))->type, dtmp);
      break;
    default:
      if (!prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }


  if (er_errid () != NO_ERROR && prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      er_clear ();
      DB_MAKE_NULL (result);
    }

  return er_errid ();
}

/*
 * db_log_dbval () -
 *   return: NO_ERROR, ER_FAILED
 *   result(out): resultant db_value
 *   value1(in) : first db_value
 *   value2(in) : second db_value
 */
int
db_log_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
  DB_TYPE type1, type2;
  short s1, s2;
  int i1, i2;
  float f1, f2;
  double d1, d2;
  DB_BIGINT bi1, bi2;
  double dtmp = 0.0;

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  type2 = DB_VALUE_DOMAIN_TYPE (value2);

  switch (type1)
    {
    case DB_TYPE_SHORT:
      s1 = DB_GET_SHORT (value1);
      if (s1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) s1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) s1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) s1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) s1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) d2) / log10 ((double) s1);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) d2) / log10 ((double) s1);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 ((double) s1);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  break;
	}
      break;

    case DB_TYPE_BIGINT:
      bi1 = DB_GET_BIGINT (value1);
      if (bi1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) d2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) d2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 ((double) bi1);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  break;
	}
      break;

    case DB_TYPE_INTEGER:
      i1 = DB_GET_INT (value1);
      if (i1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) i1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) i1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) i1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) i1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 ((double) i1);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 ((double) i1);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 ((double) i1);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  break;
	}
      break;

    case DB_TYPE_FLOAT:
      f1 = DB_GET_FLOAT (value1);
      if (f1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) f1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) f1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) f1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) f1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 ((double) f1);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (f1);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (f1);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  break;
	}
      break;

    case DB_TYPE_DOUBLE:
      d1 = DB_GET_DOUBLE (value1);
      if (d1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 (d1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 (d1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 (d1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 (d1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  break;
	}
      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value1), DB_VALUE_SCALE (value1), &d1);
      if (d1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 (d1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 (d1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 (d1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 (d1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  break;
	}
      break;

    case DB_TYPE_MONETARY:
      d1 = (DB_GET_MONETARY (value1))->amount;
      if (d1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 (d1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 (d1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 (d1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 (d1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 (d2) / log10 (d1);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  break;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;

log_error:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1, "log()");
  return ER_FAILED;
}

/*
 * truncate_double ()
 *   return: num truncated to integer places
 *   num(in)    :
 *   integer(in):
 */
static double
truncate_double (double num, double integer)
{
  /* 
   * Under high optimization level, some optimizers (e.g, gcc -O3 on linux)
   * generates a wrong result without "volatile".
   */
  double scale_up, num_scale_up, result;

  if (num == 0)
    {
      return num;
    }

  scale_up = pow (10, integer);
  num_scale_up = num * scale_up;
  if (num > 0)
    {
      result = floor (num_scale_up);
    }
  else
    {
      result = ceil (num_scale_up);
    }

  if (num_scale_up == result)	/* no need to calculate, return as it is */
    {
      result = num;		/* to avoid possible truncation */
    }
  else
    {
      result = result / scale_up;
    }

  return result;
}

/*
 * truncate_bigint ()
 *   return: num truncated to integer places
 *   num(in)    :
 *   integer(in):
 */
static DB_BIGINT
truncate_bigint (DB_BIGINT num, DB_BIGINT integer)
{
  if (num == 0 || integer >= 0)
    {
      return num;
    }

  integer = (DB_BIGINT) pow (10, (double) -integer);
  num -= num % integer;

  return num;
}

/*
 * truncate_date ()
 *   return: error or no error
 *   date(in)    :
 *   fmt(in):
 */
static int
truncate_date (DB_DATE * date, const DB_VALUE * format_str)
{
  int year, month, day;
  int error = NO_ERROR;
  TIMESTAMP_FORMAT format;
  int weekday;
  int days_months[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  assert (date != NULL);
  assert (format_str != NULL);

  error = db_get_date_format (format_str, &format);
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (format == DT_INVALID)
    {
      error = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  db_date_decode (date, &month, &day, &year);

  /* truncate datetime according to format */
  switch (format)
    {
    case DT_YYYY:
    case DT_YY:
      month = day = 1;
      break;
    case DT_MONTH:
    case DT_MON:
    case DT_MM:
      day = 1;
      break;
    case DT_DD:
    case DT_HH24:
    case DT_HH12:
    case DT_HH:
    case DT_H:
    case DT_MI:
    case DT_SS:
    case DT_MS:
      /* do nothing */
      break;
    case DT_Q:			/* quarter */
      month = (month - 1) / 3 * 3 + 1;
      day = 1;
      break;
    case DT_DAY:		/* week day */
    case DT_DY:
    case DT_D:
      weekday = day_of_week (*date);
      day = day - weekday;	/* Sunday is the first day of a week */

      /* need adjust */
      if (day < 1)
	{
	  month = month - 1;
	  if (month < 1)
	    {
	      year = year - 1;
	      month = 12;
	    }
	  if (month == 2 && (year % 400 == 0 || (year % 100 != 0 && year % 4 == 0)))
	    {
	      days_months[2] = 29;
	    }

	  day = day + days_months[month];
	}
      break;
    case DT_CC:		/* one greater than the first two digits of a four-digit year */
    default:
      error = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  error = db_date_encode (date, month, day, year);
  if (error != NO_ERROR)
    {
      goto end;
    }

end:

  return error;
}

/*
 * db_trunc_dbval () - return dbval1 truncated to dbval2 decimal places
 *
 * There are four overloads
 * The first one is used to truncate number
 *            trunc(PT_GENERIC_TYPE_NUMBER, PT_TYPE_INTEGER)
 * The second,third and fourth are used to truncate date/datetime/timestamp with formart
 *            trunc(PT_TYPE_DATE/PT_TYPE_DATETIME/PT_TYPE_TIMESTAMP, PT_TYPE_CHAR)
 *
 *   return: NO_ERROR, ER_FAILED
 *   result(out): resultant db_value
 *   value1(in) : first db_value
 *   value2(in) : second db_value
 */
int
db_trunc_dbval (DB_VALUE * result, DB_VALUE * value1, DB_VALUE * value2)
{
  DB_TYPE type1, type2;
  DB_BIGINT bi2;
  double dtmp;
  DB_VALUE cast_value, cast_format;
  int er_status = NO_ERROR;
  DB_DATE date;
  TP_DOMAIN *domain;
  TP_DOMAIN_STATUS cast_status;

  DB_MAKE_NULL (&cast_value);
  DB_MAKE_NULL (&cast_format);

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  if (type2 != DB_TYPE_SHORT && type2 != DB_TYPE_INTEGER && type2 != DB_TYPE_BIGINT && type2 != DB_TYPE_NUMERIC
      && type2 != DB_TYPE_FLOAT && type2 != DB_TYPE_DOUBLE && type2 != DB_TYPE_MONETARY && !QSTR_IS_ANY_CHAR (type2))
    {
      er_status = ER_QPROC_INVALID_DATATYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);

      goto end;
    }

  /* convert value1 to double when it's a string */
  switch (type1)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      break;
    default:			/* convert to double */
      type1 = DB_TYPE_UNKNOWN;

      /* try type double */
      DB_MAKE_NULL (&cast_value);
      domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
      cast_status = tp_value_coerce (value1, &cast_value, domain);
      if (cast_status == DOMAIN_COMPATIBLE)
	{
	  type1 = DB_TYPE_DOUBLE;
	  value1 = &cast_value;
	}

      /* convert fail */
      if (type1 == DB_TYPE_UNKNOWN)
	{
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
	    {
	      DB_MAKE_NULL (result);
	      er_clear ();
	      er_status = NO_ERROR;
	    }
	  else
	    {
	      er_status = ER_QPROC_INVALID_DATATYPE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	    }

	  goto end;
	}
    }

  /* translate default fmt */
  if (type2 == DB_TYPE_CHAR && strcasecmp (DB_PULL_STRING (value2), "default") == 0)
    {
      if (TP_IS_DATE_TYPE (type1))
	{
	  DB_MAKE_STRING (&cast_format, "dd");
	}
      else
	{
	  DB_MAKE_INT (&cast_format, 0);
	  type2 = DB_TYPE_INTEGER;
	}

      value2 = &cast_format;
    }

  if (type2 == DB_TYPE_INTEGER)
    {
      bi2 = DB_GET_INT (value2);
    }
  else if (type2 == DB_TYPE_BIGINT)
    {
      bi2 = DB_GET_BIGINT (value2);
    }
  else if (type2 == DB_TYPE_SHORT)
    {
      bi2 = DB_GET_SHORT (value2);
    }
  else if (type1 != DB_TYPE_DATE && type1 != DB_TYPE_DATETIME && type1 != DB_TYPE_DATETIMELTZ
	   && type1 != DB_TYPE_DATETIMETZ && type1 != DB_TYPE_TIMESTAMP && type1 != DB_TYPE_TIMESTAMPLTZ
	   && type1 != DB_TYPE_TIMESTAMPTZ)
    {
      domain = tp_domain_resolve_default (DB_TYPE_BIGINT);
      cast_status = tp_value_coerce (value2, &cast_format, domain);
      if (cast_status != DOMAIN_COMPATIBLE)
	{
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
	    {
	      DB_MAKE_NULL (result);
	      er_clear ();
	      er_status = NO_ERROR;
	    }
	  else
	    {
	      er_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	    }

	  goto end;
	}

      bi2 = DB_GET_BIGINT (&cast_format);
    }
  else
    {
      bi2 = 0;			/* to make compiler be silent */
    }

  switch (type1)
    {
    case DB_TYPE_SHORT:
      {
	short s1;

	s1 = DB_GET_SHORT (value1);
	dtmp = truncate_double (s1, (double) bi2);
	DB_MAKE_SHORT (result, (short) dtmp);
      }
      break;
    case DB_TYPE_INTEGER:
      {
	int i1;

	i1 = DB_GET_INT (value1);
	dtmp = truncate_double (i1, (double) bi2);
	DB_MAKE_INT (result, (int) dtmp);
      }
      break;
    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bi1;

	bi1 = DB_GET_BIGINT (value1);
	bi1 = truncate_bigint (bi1, bi2);
	DB_MAKE_BIGINT (result, bi1);
      }
      break;
    case DB_TYPE_FLOAT:
      {
	float f1;

	f1 = DB_GET_FLOAT (value1);
	dtmp = truncate_double (f1, (double) bi2);
	DB_MAKE_FLOAT (result, (float) dtmp);
      }
      break;

    case DB_TYPE_DOUBLE:
      {
	double d1;

	d1 = DB_GET_DOUBLE (value1);
	dtmp = truncate_double (d1, (double) bi2);
	DB_MAKE_DOUBLE (result, (double) dtmp);
      }
      break;
    case DB_TYPE_NUMERIC:
      {
	unsigned char num[DB_NUMERIC_BUF_SIZE];
	char num_string[(2 * DB_MAX_NUMERIC_PRECISION) + 4];
	char *ptr, *end;
	int p, s;

	memset (num_string, 0, sizeof (num_string));
	numeric_coerce_num_to_dec_str (db_locate_numeric (value1), num_string);
	p = DB_VALUE_PRECISION (value1);
	s = DB_VALUE_SCALE (value1);
	end = num_string + strlen (num_string);
	ptr = end - s + bi2;

	if (end < ptr)
	  {
	    /* no need to round, return as it is */
	    *result = *value1;
	    break;
	  }
	else if (ptr < num_string)
	  {
	    /* return zero */
	    memset (num_string, 0, sizeof (num_string));
	  }
	else
	  {
	    while (ptr < end)
	      {
		*ptr++ = '0';
	      }
	  }
	numeric_coerce_dec_str_to_num (num_string, num);
	DB_MAKE_NUMERIC (result, num, p, s);
      }
      break;
    case DB_TYPE_MONETARY:
      {
	double d1;

	d1 = (DB_GET_MONETARY (value1))->amount;
	dtmp = truncate_double (d1, (double) bi2);
	DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value1))->type, dtmp);
      }
      break;
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      if (type1 == DB_TYPE_DATE)
	{
	  date = *(DB_GET_DATE (value1));
	}
      else if (type1 == DB_TYPE_DATETIME)
	{
	  date = DB_GET_DATETIME (value1)->date;
	}
      else if (type1 == DB_TYPE_DATETIMELTZ)
	{
	  DB_DATETIME local_dt, *p_dt;

	  p_dt = DB_GET_DATETIME (value1);

	  er_status = tz_datetimeltz_to_local (p_dt, &local_dt);
	  if (er_status != NO_ERROR)
	    {
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
		{
		  er_clear ();
		  DB_MAKE_NULL (result);
		  er_status = NO_ERROR;
		}
	      goto end;
	    }

	  date = local_dt.date;
	}
      else if (type1 == DB_TYPE_DATETIMETZ)
	{
	  DB_DATETIME local_dt;
	  DB_DATETIMETZ *p_dt_tz;

	  p_dt_tz = DB_GET_DATETIMETZ (value1);

	  er_status = tz_utc_datetimetz_to_local (&p_dt_tz->datetime, &p_dt_tz->tz_id, &local_dt);

	  if (er_status != NO_ERROR)
	    {
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
		{
		  er_clear ();
		  DB_MAKE_NULL (result);
		  er_status = NO_ERROR;
		}
	      goto end;
	    }

	  date = local_dt.date;
	}
      else if (type1 == DB_TYPE_TIMESTAMPTZ)
	{
	  DB_TIMESTAMPTZ *p_ts_tz;

	  p_ts_tz = db_get_timestamptz (value1);
	  er_status = db_timestamp_decode_w_tz_id (&p_ts_tz->timestamp, &p_ts_tz->tz_id, &date, NULL);
	  if (er_status != NO_ERROR)
	    {
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
		{
		  er_clear ();
		  DB_MAKE_NULL (result);
		  er_status = NO_ERROR;
		}
	      goto end;
	    }
	}
      else
	{
	  assert (type1 == DB_TYPE_TIMESTAMP || type1 == DB_TYPE_TIMESTAMPLTZ);
	  (void) db_timestamp_decode_ses (DB_GET_TIMESTAMP (value1), &date, NULL);
	}

      er_status = truncate_date (&date, value2);
      if (er_status != NO_ERROR)
	{
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
	    {
	      er_clear ();
	      er_status = NO_ERROR;
	    }
	  goto end;
	}
      else
	{
	  db_value_put_encoded_date (result, &date);
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_status = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	  goto end;
	}
    }

end:
  pr_clear_value (&cast_value);
  pr_clear_value (&cast_format);

  return er_status;
}

/*
 * db_random_dbval () - take random integer
 *   return: NO_ERROR
 *   result(out) : resultant db_value
 */
int
db_random_dbval (DB_VALUE * result)
{
  DB_MAKE_INTEGER (result, lrand48 ());

  return NO_ERROR;
}

/*
 * db_drandom_dbval () - take random double
 *   return: NO_ERROR
 *   result(out) : resultant db_value
 */
int
db_drandom_dbval (DB_VALUE * result)
{
  DB_MAKE_DOUBLE (result, drand48 ());

  return NO_ERROR;
}

/*
 * get_number_dbval_as_double () -
 *   return: NO_ERROR/error code
 *   d(out) : double
 *   value(in) : input db_value
 */
static int
get_number_dbval_as_double (double *d, const DB_VALUE * value)
{
  short s;
  int i;
  float f;
  double dtmp;
  DB_BIGINT bi;

  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_SHORT:
      s = DB_GET_SHORT (value);
      dtmp = (double) s;
      break;
    case DB_TYPE_INTEGER:
      i = DB_GET_INT (value);
      dtmp = (double) i;
      break;
    case DB_TYPE_BIGINT:
      bi = DB_GET_BIGINT (value);
      dtmp = (double) bi;
      break;
    case DB_TYPE_FLOAT:
      f = DB_GET_FLOAT (value);
      dtmp = (double) f;
      break;
    case DB_TYPE_DOUBLE:
      dtmp = DB_GET_DOUBLE (value);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double ((DB_C_NUMERIC) db_locate_numeric (value), DB_VALUE_SCALE (value), &dtmp);
      break;
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      break;
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }

  *d = dtmp;
  return NO_ERROR;
}

/*
 * db_cos_dbval () - computes cosine value of db_value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_cos_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = cos (dtmp);

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;
}

/*
 * db_sin_dbval () - computes sine value of db_value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_sin_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = sin (dtmp);

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;
}

/*
 * db_tan_dbval () - computes tangent value of db_value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_tan_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = tan (dtmp);

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;
}

/*
 * db_cot_dbval () - computes cotangent value of db_value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_cot_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (dtmp == 0)
    {
      DB_MAKE_NULL (result);
    }
  else
    {
      dtmp = 1 / tan (dtmp);
      DB_MAKE_DOUBLE (result, dtmp);
    }

  return NO_ERROR;
}

/*
 * db_acos_dbval () - computes arc cosine value of db_value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_acos_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (dtmp < -1 || dtmp > 1)
    {
      goto error;
    }

  dtmp = acos (dtmp);

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;

error:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1, "acos()");
      return ER_QPROC_FUNCTION_ARG_ERROR;
    }
}

/*
 * db_asin_dbval () - computes arc sine value of db_value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_asin_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (dtmp < -1 || dtmp > 1)
    {
      goto error;
    }

  dtmp = asin (dtmp);

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;

error:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1, "asin()");
      return ER_QPROC_FUNCTION_ARG_ERROR;
    }
}

/*
 * db_atan_dbval () - computes arc tangent value of value2 / value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_atan_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = atan (dtmp);

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;
}

/*
 * db_atan2_dbval () - computes arc tangent value of value2 / value
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 *   value2(in) : second input db_value
 *  OBS: this should have been done like db_power_dbval, i.e. switch in switch
 *	  but this yields in very much code so we prefered to get all values
 *	  separated and then convert all to double. Then just one call of atan2.
 */
int
db_atan2_dbval (DB_VALUE * result, DB_VALUE * value, DB_VALUE * value2)
{
  DB_TYPE type, type2;
  int err;
  double d, d2, dtmp;

  /* arg1 */
  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&d, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  /* arg2 */
  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  if (type2 == DB_TYPE_NULL || DB_IS_NULL (value2))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&d2, value2);
  if (err != NO_ERROR)
    {
      return err;
    }

  /* function call, all is double type */
  dtmp = atan2 (d, d2);

  DB_MAKE_DOUBLE (result, dtmp);
  return NO_ERROR;
}

/*
 * db_degrees_dbval () - computes radians from value in degrees
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_degrees_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = dtmp * (double) 57.295779513082320876798154814105;	/* 180 / PI */
  DB_MAKE_DOUBLE (result, dtmp);

  return NO_ERROR;
}

/*
 * db_radians_dbval () - converts degrees in value to radians
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_radians_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int err;
  double dtmp;

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = dtmp * (double) 0.017453292519943295769236907684886;	/* PI / 180 */
  DB_MAKE_DOUBLE (result, dtmp);

  return NO_ERROR;
}

/*
 * db_log_generic_dbval () - computes log of db_value in base
 *   return: NO_ERROR
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_log_generic_dbval (DB_VALUE * result, DB_VALUE * value, long b)
{
  DB_TYPE type;
  int err;
  double dtmp;
  double base = ((b == -1) ? (2.7182818284590452353) : (double) b);

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (dtmp > 0)
    {
      dtmp = log10 (dtmp) / log10 (base);
      DB_MAKE_DOUBLE (result, dtmp);
    }
  else
    {
      const char *log_func;

      switch (b)
	{
	case -1:
	  log_func = "ln()";
	  break;
	case 2:
	  log_func = "log2()";
	  break;
	case 10:
	  log_func = "log10()";
	  break;
	default:
	  assert (0);
	  log_func = "unknown";
	  break;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1, log_func);
      return ER_QPROC_FUNCTION_ARG_ERROR;
    }

  return NO_ERROR;
}

/*
 * db_bit_count_dbval () - bit count of db_value
 *   return:
 *   result(out): resultant db_value
 *   value(in) : input db_value
 */
int
db_bit_count_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  short s;
  int i, c = 0;
  float f;
  double d;
  DB_BIGINT bi;
  DB_VALUE tmpval, *tmpval_p;

  if (value == NULL)
    {
      return ER_FAILED;
    }

  tmpval_p = value;
  type = DB_VALUE_DOMAIN_TYPE (value);

  if (DB_IS_NULL (value))
    {
      DB_MAKE_NULL (result);
    }
  else
    {
      switch (type)
	{
	case DB_TYPE_SHORT:
	  s = DB_GET_SHORT (value);
	  for (c = 0; s; c++)
	    {
	      s &= s - 1;
	    }
	  break;

	case DB_TYPE_INTEGER:
	  i = DB_GET_INTEGER (value);
	  for (c = 0; i; c++)
	    {
	      i &= i - 1;
	    }
	  break;

	case DB_TYPE_BIGINT:
	  bi = DB_GET_BIGINT (value);
	  for (c = 0; bi; c++)
	    {
	      bi &= bi - 1;
	    }
	  break;

	case DB_TYPE_FLOAT:
	  f = DB_GET_FLOAT (value);
	  if (f < 0)
	    {
	      i = (int) (f - 0.5f);
	    }
	  else
	    {
	      i = (int) (f + 0.5f);
	    }
	  for (c = 0; i; c++)
	    {
	      i &= i - 1;
	    }
	  break;

	case DB_TYPE_MONETARY:
	  d = (DB_GET_MONETARY (value))->amount;
	  if (d < 0)
	    {
	      bi = (DB_BIGINT) (d - 0.5f);
	    }
	  else
	    {
	      bi = (DB_BIGINT) (d + 0.5f);
	    }
	  for (c = 0; bi; c++)
	    {
	      bi &= bi - 1;
	    }
	  break;

	case DB_TYPE_NUMERIC:
	  if (tp_value_cast (value, &tmpval, &tp_Double_domain, false) != DOMAIN_COMPATIBLE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	      return ER_FAILED;
	    }
	  tmpval_p = &tmpval;
	  /* no break here */
	case DB_TYPE_DOUBLE:
	  d = DB_GET_DOUBLE (tmpval_p);
	  if (d < 0)
	    {
	      bi = (DB_BIGINT) (d - 0.5f);
	    }
	  else
	    {
	      bi = (DB_BIGINT) (d + 0.5f);
	    }
	  for (c = 0; bi; c++)
	    {
	      bi &= bi - 1;
	    }
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}

      DB_MAKE_INT (result, c);
    }

  return NO_ERROR;
}

/*
 * db_typeof_dbval() -
 *   return:
 *   result(out):
 *   value(in) : input db_value
 */
int
db_typeof_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  const char *type_name;
  char *buf;

  type = DB_VALUE_TYPE (value);
  type_name = pr_type_name (type);
  if (type_name == NULL)
    {
      db_make_null (result);
      return NO_ERROR;
    }

  switch (type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
    case DB_TYPE_NUMERIC:
      buf = db_private_alloc (NULL, 128);
      if (buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 128);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      if (type == DB_TYPE_NUMERIC)
	{
	  snprintf (buf, 128, "%s (%u, %u)", type_name, value->domain.numeric_info.precision,
		    value->domain.numeric_info.scale);
	}
      else
	{
	  snprintf (buf, 128, "%s (%d)", type_name, value->domain.char_info.length);
	}

      db_make_string (result, buf);
      result->need_clear = true;
      break;

    default:
      db_make_string (result, type_name);
    }

  return NO_ERROR;
}

/*
 * get_number_dbval_as_long_double () -
 *   return:
 *   long double(out):
 *   value(in) :
 */
static int
get_number_dbval_as_long_double (long double *ld, const DB_VALUE * value)
{
  short s;
  int i;
  float f;
  long double dtmp;
  DB_BIGINT bi;
  char num_string[2 * DB_MAX_NUMERIC_PRECISION + 2];
  char *tail_ptr = NULL;

  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_SHORT:
      s = DB_GET_SHORT (value);
      dtmp = (long double) s;
      break;

    case DB_TYPE_INTEGER:
      i = DB_GET_INT (value);
      dtmp = (long double) i;
      break;

    case DB_TYPE_BIGINT:
      bi = DB_GET_BIGINT (value);
      dtmp = (long double) bi;
      break;

    case DB_TYPE_FLOAT:
      f = DB_GET_FLOAT (value);
      dtmp = (long double) f;
      break;

    case DB_TYPE_DOUBLE:
      dtmp = (long double) DB_GET_DOUBLE (value);
      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_dec_str ((DB_C_NUMERIC) db_locate_numeric (value), num_string);
#ifdef _ISOC99_SOURCE
      dtmp = strtold (num_string, &tail_ptr) / powl (10.0, DB_VALUE_SCALE (value));
#else
      dtmp = atof (num_string) / pow (10.0, DB_VALUE_SCALE (value));
#endif
      break;

    case DB_TYPE_MONETARY:
      dtmp = (long double) (DB_GET_MONETARY (value))->amount;
      break;

    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }

  *ld = dtmp;
  return NO_ERROR;
}

/*
 * db_width_bucket_calculate_numeric() -
 *   return:
 *   result(out):
 *   value1-4(in) : input db_value
 */
static int
db_width_bucket_calculate_numeric (double *result, const DB_VALUE * value1, const DB_VALUE * value2,
				   const DB_VALUE * value3, const DB_VALUE * value4)
{
  int er_status = NO_ERROR, c;
  DB_VALUE cmp_result;
  DB_VALUE n1, n2, n3, n4;
  double res = 0.0;

  assert (value1 != NULL && value2 != NULL && value3 != NULL && value4 != NULL && result != NULL);

  assert (DB_VALUE_TYPE (value1) == DB_TYPE_NUMERIC && DB_VALUE_TYPE (value2) == DB_TYPE_NUMERIC
	  && DB_VALUE_TYPE (value3) == DB_TYPE_NUMERIC && DB_VALUE_TYPE (value4) == DB_TYPE_NUMERIC);

  DB_MAKE_NULL (&cmp_result);
  DB_MAKE_NULL (&n1);
  DB_MAKE_NULL (&n2);
  DB_MAKE_NULL (&n3);
  DB_MAKE_NULL (&n4);

  er_status = numeric_db_value_compare (value2, value3, &cmp_result);
  if (er_status != NO_ERROR)
    {
      return er_status;
    }

  c = DB_GET_INTEGER (&cmp_result);
  if (c == 0 || c == -1)
    {
      /* value2 <= value3 */

      er_status = numeric_db_value_compare (value1, value2, &cmp_result);
      if (er_status != NO_ERROR)
	{
	  return er_status;
	}

      if (DB_GET_INTEGER (&cmp_result) < 0)
	{
	  res = 0.0;
	}
      else
	{
	  er_status = numeric_db_value_compare (value3, value1, &cmp_result);
	  if (er_status != NO_ERROR)
	    {
	      return er_status;
	    }

	  if (DB_GET_INTEGER (&cmp_result) < 1)
	    {
	      numeric_coerce_num_to_double ((DB_C_NUMERIC) DB_GET_NUMERIC (value4), DB_VALUE_SCALE (value4), &res);
	      res += 1.0;
	    }
	  else
	    {
	      /* floor ((v1-v2)/((v3-v2)/v4)) + 1 */
	      er_status = numeric_db_value_sub (value1, value2, &n1);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      er_status = numeric_db_value_sub (value3, value2, &n2);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      er_status = numeric_db_value_div (&n2, value4, &n3);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      er_status = numeric_db_value_div (&n1, &n3, &n4);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      numeric_coerce_num_to_double (DB_GET_NUMERIC (&n4), DB_VALUE_SCALE (&n4), &res);
	      if (OR_CHECK_DOUBLE_OVERFLOW (res))
		{
		  return ER_IT_DATA_OVERFLOW;
		}

	      res = floor (res) + 1.0;
	    }
	}
    }
  else
    {
      /* value2 > value3 */
      assert (c == 1);

      er_status = numeric_db_value_compare (value2, value1, &cmp_result);
      if (er_status != NO_ERROR)
	{
	  return er_status;
	}

      if (DB_GET_INTEGER (&cmp_result) < 0)
	{
	  res = 0.0;
	}
      else
	{
	  er_status = numeric_db_value_compare (value2, value3, &cmp_result);
	  if (er_status != NO_ERROR)
	    {
	      return er_status;
	    }

	  if (DB_GET_INTEGER (&cmp_result) < 1)
	    {
	      numeric_coerce_num_to_double ((DB_C_NUMERIC) DB_GET_NUMERIC (value4), DB_VALUE_SCALE (value4), &res);
	      res += 1.0;
	    }
	  else
	    {
	      /* floor ((v2-v1)/((v2-v3)/v4)) + 1 */
	      er_status = numeric_db_value_sub (value2, value1, &n1);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      er_status = numeric_db_value_sub (value2, value3, &n2);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      er_status = numeric_db_value_div (&n2, value4, &n3);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      er_status = numeric_db_value_div (&n1, &n3, &n4);
	      if (er_status != NO_ERROR)
		{
		  return er_status;
		}

	      numeric_coerce_num_to_double (DB_GET_NUMERIC (&n4), DB_VALUE_SCALE (&n4), &res);
	      if (OR_CHECK_DOUBLE_OVERFLOW (res))
		{
		  return ER_IT_DATA_OVERFLOW;
		}

	      res = floor (res) + 1.0;
	    }
	}
    }

  if (OR_CHECK_DOUBLE_OVERFLOW (res))
    {
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  *result = res;
  return NO_ERROR;
}

/*
 * db_width_bucket() -
 *   return:
 *   result(out):
 *   value1-4(in) : input db_value
 */
int
db_width_bucket (DB_VALUE * result, const DB_VALUE * value1, const DB_VALUE * value2, const DB_VALUE * value3,
		 const DB_VALUE * value4)
{
#define RETURN_ERROR(err) \
  do \
    { \
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true) \
	{ \
	  DB_MAKE_NULL (result); \
	  er_clear (); \
	  return NO_ERROR; \
	} \
      else \
	{ \
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (err), 0); \
	  return (err); \
	} \
    } \
  while (0)

#define RETURN_ERROR_WITH_ARG(err, arg) \
  do \
    { \
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true) \
        { \
          DB_MAKE_NULL (result); \
          er_clear (); \
          return NO_ERROR; \
        } \
      else \
        { \
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (err), 1, (arg)); \
          return (err); \
        } \
    } \
  while (0)

#define MAX_DOMAIN_NAME_SIZE 150

  double d1, d2, d3, d4, d_ret;
  double tmp_d1 = 0.0, tmp_d2 = 0.0, tmp_d3 = 0.0, tmp_d4 = 0.0;
  DB_TYPE type, cast_type;
  DB_VALUE cast_value1, cast_value2, cast_value3, cast_value4;
  TP_DOMAIN *cast_domain = NULL;
  TP_DOMAIN *numeric_domain = NULL;
  TP_DOMAIN_STATUS cast_status;
  bool is_deal_with_numeric = false;
  int er_status = NO_ERROR;
  char buf[MAX_DOMAIN_NAME_SIZE];
  DB_TIME time_local;

  assert (result != NULL && value1 != NULL && value2 != NULL && value3 != NULL && value4 != NULL);

  DB_MAKE_NULL (&cast_value1);
  DB_MAKE_NULL (&cast_value2);
  DB_MAKE_NULL (&cast_value3);
  DB_MAKE_NULL (&cast_value4);

  if (DB_VALUE_TYPE (value1) == DB_TYPE_NULL || DB_VALUE_TYPE (value2) == DB_TYPE_NULL
      || DB_VALUE_TYPE (value3) == DB_TYPE_NULL || DB_VALUE_TYPE (value4) == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  d4 = DB_GET_DOUBLE (value4);
  if (d4 < 1 || d4 >= DB_INT32_MAX)
    {
      RETURN_ERROR (ER_PROC_WIDTH_BUCKET_COUNT);
    }

  d4 = (int) floor (d4);

  /* find the common type of value1, value2 and value3 and cast them to the common type */
  type = DB_VALUE_DOMAIN_TYPE (value1);
  switch (type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      /* try double */
      cast_type = DB_TYPE_UNKNOWN;
      cast_domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
      cast_status = tp_value_coerce (value1, &cast_value1, cast_domain);
      if (cast_status == DOMAIN_COMPATIBLE)
	{
	  cast_type = DB_TYPE_DOUBLE;
	}
      else
	{
	  /* try datetime date, timestamp is compatible with datetime */
	  cast_domain = tp_domain_resolve_default (DB_TYPE_DATETIME);
	  cast_status = tp_value_coerce (value1, &cast_value1, cast_domain);
	  if (cast_status == DOMAIN_COMPATIBLE)
	    {
	      cast_type = DB_TYPE_DATETIME;
	    }
	  else
	    {
	      /* try time */
	      cast_domain = tp_domain_resolve_default (DB_TYPE_TIME);
	      cast_status = tp_value_coerce (value1, &cast_value1, cast_domain);
	      if (cast_status == DOMAIN_COMPATIBLE)
		{
		  cast_type = DB_TYPE_TIME;
		}
	      else
		{
		  RETURN_ERROR (ER_QPROC_INVALID_DATATYPE);
		}
	    }
	}

      value1 = &cast_value1;

      /* coerce value2 with the type of value1 */
      if (cast_type != DB_VALUE_DOMAIN_TYPE (value2))
	{
	  cast_domain = tp_domain_resolve_default (cast_type);
	  cast_status = tp_value_coerce (value2, &cast_value2, cast_domain);
	  if (cast_status != DOMAIN_COMPATIBLE)
	    {
	      RETURN_ERROR (ER_QPROC_INVALID_DATATYPE);
	    }

	  value2 = &cast_value2;
	}

      /* coerce value3 with the type of value1 */
      if (cast_type != DB_VALUE_DOMAIN_TYPE (value3))
	{
	  cast_domain = tp_domain_resolve_default (cast_type);
	  cast_status = tp_value_coerce (value3, &cast_value3, cast_domain);
	  if (cast_status != DOMAIN_COMPATIBLE)
	    {
	      RETURN_ERROR (ER_QPROC_INVALID_DATATYPE);
	    }

	  value3 = &cast_value3;
	}
      break;

    default:
      break;
    }

  /* the type of value1 is fixed */
  type = DB_VALUE_DOMAIN_TYPE (value1);
  switch (type)
    {
    case DB_TYPE_DATE:
      d1 = (double) *DB_GET_DATE (value1);
      d2 = (double) *DB_GET_DATE (value2);
      d3 = (double) *DB_GET_DATE (value3);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      /* double can hold datetime type */
      d1 = ((double) DB_GET_DATETIME (value1)->date) * MILLISECONDS_OF_ONE_DAY + DB_GET_DATETIME (value1)->time;
      d2 = ((double) DB_GET_DATETIME (value2)->date) * MILLISECONDS_OF_ONE_DAY + DB_GET_DATETIME (value2)->time;
      d3 = ((double) DB_GET_DATETIME (value3)->date) * MILLISECONDS_OF_ONE_DAY + DB_GET_DATETIME (value3)->time;
      break;

    case DB_TYPE_DATETIMETZ:
      /* double can hold datetime type */
      d1 = (((double) DB_GET_DATETIMETZ (value1)->datetime.date) * MILLISECONDS_OF_ONE_DAY
	    + DB_GET_DATETIMETZ (value1)->datetime.time);
      d2 = (((double) DB_GET_DATETIMETZ (value2)->datetime.date) * MILLISECONDS_OF_ONE_DAY
	    + DB_GET_DATETIMETZ (value2)->datetime.time);
      d3 = (((double) DB_GET_DATETIMETZ (value3)->datetime.date) * MILLISECONDS_OF_ONE_DAY
	    + DB_GET_DATETIMETZ (value3)->datetime.time);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      d1 = (double) *DB_GET_TIMESTAMP (value1);
      d2 = (double) *DB_GET_TIMESTAMP (value2);
      d3 = (double) *DB_GET_TIMESTAMP (value3);
      break;

    case DB_TYPE_TIMESTAMPTZ:
      d1 = (double) (DB_GET_TIMESTAMPTZ (value1)->timestamp);
      d2 = (double) (DB_GET_TIMESTAMPTZ (value2)->timestamp);
      d3 = (double) (DB_GET_TIMESTAMPTZ (value3)->timestamp);
      break;


    case DB_TYPE_TIME:
      d1 = (double) *DB_GET_TIME (value1);
      d2 = (double) *DB_GET_TIME (value2);
      d3 = (double) *DB_GET_TIME (value3);
      break;

    case DB_TYPE_TIMELTZ:
      er_status = tz_timeltz_to_local (DB_GET_TIME (value1), &time_local);
      if (er_status == NO_ERROR)
	{
	  d1 = (double) time_local;
	  er_status = tz_timeltz_to_local (DB_GET_TIME (value2), &time_local);
	}

      if (er_status == NO_ERROR)
	{
	  d2 = (double) time_local;
	  er_status = tz_timeltz_to_local (DB_GET_TIME (value3), &time_local);
	}

      if (er_status == NO_ERROR)
	{
	  d3 = (double) time_local;
	}
      else
	{
	  RETURN_ERROR (er_status);
	}
      break;

    case DB_TYPE_TIMETZ:
      er_status = tz_utc_timetz_to_local (&DB_GET_TIMETZ (value1)->time, &DB_GET_TIMETZ (value1)->tz_id, &time_local);
      if (er_status == NO_ERROR)
	{
	  d1 = (double) time_local;
	  er_status =
	    tz_utc_timetz_to_local (&DB_GET_TIMETZ (value2)->time, &DB_GET_TIMETZ (value2)->tz_id, &time_local);
	}

      if (er_status == NO_ERROR)
	{
	  d2 = (double) time_local;
	  er_status =
	    tz_utc_timetz_to_local (&DB_GET_TIMETZ (value3)->time, &DB_GET_TIMETZ (value3)->tz_id, &time_local);
	}

      if (er_status == NO_ERROR)
	{
	  d3 = (double) time_local;
	}
      else
	{
	  RETURN_ERROR (er_status);
	}
      break;

    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
      if (get_number_dbval_as_double (&d1, value1) != NO_ERROR)
	{
	  RETURN_ERROR (ER_QPROC_INVALID_DATATYPE);
	}
      if (get_number_dbval_as_double (&d2, value2) != NO_ERROR)
	{
	  RETURN_ERROR (ER_QPROC_INVALID_DATATYPE);
	}
      if (get_number_dbval_as_double (&d3, value3) != NO_ERROR)
	{
	  RETURN_ERROR (ER_QPROC_INVALID_DATATYPE);
	}
      break;

    case DB_TYPE_BIGINT:
    case DB_TYPE_NUMERIC:
      d1 = d2 = d3 = 0;		/* to make compiler be silent */

      /* gcc fully support long double (80 or 128bits) if long double is not fully supported, do calculation with
       * numeric */
      numeric_domain = tp_domain_new (DB_TYPE_NUMERIC);
      if (numeric_domain == NULL)
	{
	  RETURN_ERROR (er_errid ());
	}

      cast_domain = numeric_domain;

      cast_domain->precision = 2 * DB_BIGINT_PRECISION;
      cast_domain->scale = DB_FLOAT_DECIMAL_PRECISION;

      if (type == DB_TYPE_BIGINT)
	{
	  /* cast bigint to numeric Compiler doesn't support long double (80 or 128bits), so we use numeric instead. If 
	   * a high precision lib is introduced or long double is full supported, remove this part and use the lib or
	   * long double to calculate. */
	  /* convert value1 */
	  cast_status = tp_value_coerce (value1, &cast_value1, cast_domain);
	  if (cast_status != DOMAIN_COMPATIBLE)
	    {
	      tp_domain_name (numeric_domain, buf, MAX_DOMAIN_NAME_SIZE);
	      tp_domain_free (numeric_domain);
	      RETURN_ERROR_WITH_ARG (ER_IT_DATA_OVERFLOW, buf);
	    }

	  value1 = &cast_value1;
	}

      /* cast value2, value3, value4 to numeric to make the calculation */
      if (DB_VALUE_DOMAIN_TYPE (value2) != DB_TYPE_NUMERIC)
	{
	  cast_status = tp_value_coerce (value2, &cast_value2, cast_domain);
	  if (cast_status != DOMAIN_COMPATIBLE)
	    {
	      tp_domain_name (numeric_domain, buf, MAX_DOMAIN_NAME_SIZE);
	      tp_domain_free (numeric_domain);
	      RETURN_ERROR_WITH_ARG (ER_IT_DATA_OVERFLOW, buf);
	    }

	  value2 = &cast_value2;
	}

      if (DB_VALUE_DOMAIN_TYPE (value3) != DB_TYPE_NUMERIC)
	{
	  cast_status = tp_value_coerce (value3, &cast_value3, cast_domain);
	  if (cast_status != DOMAIN_COMPATIBLE)
	    {
	      tp_domain_name (numeric_domain, buf, MAX_DOMAIN_NAME_SIZE);
	      tp_domain_free (numeric_domain);
	      RETURN_ERROR_WITH_ARG (ER_IT_DATA_OVERFLOW, buf);
	    }

	  value3 = &cast_value3;
	}

      DB_MAKE_INT (&cast_value4, ((int) d4));
      cast_domain->precision = DB_INTEGER_PRECISION;
      cast_domain->scale = 0;
      cast_status = tp_value_coerce (&cast_value4, &cast_value4, cast_domain);
      if (cast_status != DOMAIN_COMPATIBLE)
	{
	  tp_domain_free (numeric_domain);
	  RETURN_ERROR (ER_QPROC_OVERFLOW_ADDITION);
	}

      value4 = &cast_value4;

      is_deal_with_numeric = true;

      tp_domain_free (numeric_domain);
      numeric_domain = NULL;
      break;

    default:
      RETURN_ERROR (ER_QPROC_INVALID_DATATYPE);
    }

  if (is_deal_with_numeric)
    {
      er_status = db_width_bucket_calculate_numeric (&d_ret, value1, value2, value3, value4);
      if (er_status != NO_ERROR)
	{
	  RETURN_ERROR (er_status);
	}
    }
  else
    {
      if (d2 <= d3)
	{
	  if (d1 < d2)
	    {
	      d_ret = 0.0;
	    }
	  else if (d3 <= d1)
	    {
	      d_ret = d4 + 1.0;
	    }
	  else
	    {
	      /* d_ret = floor ((d1 - d2) / ((d3 - d2) / d4)) + 1.0 */
	      tmp_d1 = d1 - d2;
	      tmp_d2 = d3 - d2;
	      tmp_d3 = tmp_d2 / d4;
	      tmp_d4 = tmp_d1 / tmp_d3;
	      d_ret = floor (tmp_d4) + 1.0;
	    }
	}
      else
	{
	  if (d2 < d1)
	    {
	      d_ret = 0.0;
	    }
	  else if (d1 <= d3)
	    {
	      d_ret = d4 + 1.0;
	    }
	  else
	    {
	      /* d_ret = floor ((d2 - d1) / ((d2 - d3) / d4)) + 1.0 */
	      tmp_d1 = d2 - d1;
	      tmp_d2 = d2 - d3;
	      tmp_d3 = tmp_d2 / d4;
	      tmp_d4 = tmp_d1 / tmp_d3;
	      d_ret = floor (tmp_d4) + 1.0;
	    }
	}
    }

  /* check overflow */
  if (OR_CHECK_DOUBLE_OVERFLOW (tmp_d1) || OR_CHECK_DOUBLE_OVERFLOW (tmp_d2))
    {
      RETURN_ERROR (ER_QPROC_OVERFLOW_SUBTRACTION);
    }
  else if (OR_CHECK_DOUBLE_OVERFLOW (tmp_d3) || OR_CHECK_DOUBLE_OVERFLOW (tmp_d4))
    {
      RETURN_ERROR (ER_QPROC_OVERFLOW_DIVISION);
    }
  else if (OR_CHECK_INT_OVERFLOW (d_ret))
    {
      RETURN_ERROR (ER_QPROC_OVERFLOW_ADDITION);
    }

  DB_MAKE_INT (result, ((int) d_ret));

  return er_status;

#undef RETURN_ERROR
#undef RETURN_ERROR_WITH_ARG
}

/*
 * db_sleep() - sleep milli-secs
 *   return:
 *   result(out):
 *   value(in) : input db_value
 */
int
db_sleep (DB_VALUE * result, DB_VALUE * value)
{
  int error = NO_ERROR;
  long million_sec = 0;

  assert (result != NULL && value != NULL);
  assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_NULL || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_DOUBLE);

  DB_MAKE_NULL (result);

  if (DB_IS_NULL (value) || DB_GET_DOUBLE (value) < 0.0)
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);

      goto end;
    }

  million_sec = (long) (DB_GET_DOUBLE (value) * 1000L);

  error = msleep (million_sec);
  if (error == NO_ERROR)
    {
      DB_MAKE_INT (result, 0);
    }
  else
    {
      DB_MAKE_INT (result, 1);

      error = NO_ERROR;
    }

end:

  return error;
}

/*
 * db_crc32_dbval() - crc32
 *   return: error code
 *   result(out):
 *   value(in) : input db_value
 */
int
db_crc32_dbval (DB_VALUE * result, DB_VALUE * value)
{
  DB_TYPE type;
  int error_status = NO_ERROR;
  int hash_result = 0;

  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (value))
    {
      PRIM_SET_NULL (result);
      return error_status;
    }
  else
    {
      type = DB_VALUE_DOMAIN_TYPE (value);

      if (QSTR_IS_ANY_CHAR (type))
	{
	  error_status = crypt_crc32 (NULL, DB_PULL_STRING (value), DB_GET_STRING_SIZE (value), &hash_result);
	  if (error_status != NO_ERROR)
	    {
	      goto error;
	    }

	  DB_MAKE_INT (result, hash_result);
	}
      else
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  goto error;
	}
    }

  return error_status;

error:
  PRIM_SET_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      er_clear ();
      return NO_ERROR;
    }
  else
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      return error_status;
    }
}
