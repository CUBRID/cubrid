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

/* this must be the last header file included!!! */
#include "dbval.h"

static int db_mod_short (DB_VALUE * value, DB_VALUE * value1,
			 DB_VALUE * value2);
static int db_mod_int (DB_VALUE * value, DB_VALUE * value1,
		       DB_VALUE * value2);
static int db_mod_bigint (DB_VALUE * value, DB_VALUE * value1,
			  DB_VALUE * value2);
static int db_mod_float (DB_VALUE * value, DB_VALUE * value1,
			 DB_VALUE * value2);
static int db_mod_double (DB_VALUE * value, DB_VALUE * value1,
			  DB_VALUE * value2);
static int db_mod_string (DB_VALUE * value, DB_VALUE * value1,
			  DB_VALUE * value2);
static int db_mod_numeric (DB_VALUE * value, DB_VALUE * value1,
			   DB_VALUE * value2);
static int db_mod_monetary (DB_VALUE * value, DB_VALUE * value1,
			    DB_VALUE * value2);
static double round_double (double num, double integer);
static double truncate_double (double num, double integer);
static DB_BIGINT truncate_bigint (DB_BIGINT num, DB_BIGINT integer);
static DB_DATE *truncate_date (DB_DATE * date, const DB_VALUE * format_str);
static int get_number_dbval_as_double (double *d, const DB_VALUE * value);

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
      er_status = tp_value_str_auto_cast_to_number (value, &cast_value,
						    &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && res_type != DB_TYPE_DOUBLE))
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
	    numeric_coerce_num_to_dec_str (DB_PULL_NUMERIC (value),
					   num_str_p);
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
		/* To decrement a negative value, the absolute value (the
		 * digits) actually has to be incremented. */

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
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type,
				    dtmp);
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
      er_status = tp_value_str_auto_cast_to_number (value, &cast_value,
						    &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && res_type != DB_TYPE_DOUBLE))
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
	    numeric_coerce_num_to_dec_str (db_locate_numeric (value),
					   num_str_p);
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
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type,
				    dtmp);
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
      numeric_coerce_num_to_double (db_locate_numeric (value),
				    DB_VALUE_SCALE (value), &dtmp);
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
      er_status = tp_value_str_auto_cast_to_number (value, &cast_value,
						    &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && res_type != DB_TYPE_DOUBLE))
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
	DB_MAKE_NUMERIC (result, num,
			 DB_VALUE_PRECISION (value), DB_VALUE_SCALE (value));
	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      dtmp = fabs (dtmp);
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type,
				    dtmp);
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
      numeric_coerce_num_to_double (db_locate_numeric (value),
				    DB_VALUE_SCALE (value), &d);
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
      numeric_coerce_num_to_double (db_locate_numeric (value),
				    DB_VALUE_SCALE (value), &d);
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR,
	      1, "sqrt()");
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
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2,
						    &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type2 != DB_TYPE_DOUBLE))
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
      numeric_coerce_num_to_double (db_locate_numeric (value2),
				    DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (s1, d2);
	  (void) numeric_internal_double_to_num (dtmp,
						 DB_VALUE_SCALE (value2),
						 num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type, s1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					(double) fmod (s1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
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
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2,
						    &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type2 != DB_TYPE_DOUBLE))
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
      numeric_coerce_num_to_double (db_locate_numeric (value2),
				    DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (i1, d2);
	  (void) numeric_internal_double_to_num (dtmp,
						 DB_VALUE_SCALE (value2),
						 num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type, i1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					(double) fmod (i1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
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
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2,
						    &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type2 != DB_TYPE_DOUBLE))
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
      numeric_coerce_num_to_double (db_locate_numeric (value2),
				    DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod ((double) bi1, d2);
	  (void) numeric_internal_double_to_num (dtmp,
						 DB_VALUE_SCALE (value2),
						 num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					(double) bi1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					(double) fmod ((double) bi1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
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
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2,
						    &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type2 != DB_TYPE_DOUBLE))
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
      numeric_coerce_num_to_double (db_locate_numeric (value2),
				    DB_VALUE_SCALE (value2), &d2);
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
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type, f1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					(double) fmod ((double) f1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
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
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2,
						    &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type2 != DB_TYPE_DOUBLE))
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
      numeric_coerce_num_to_double (db_locate_numeric (value2),
				    DB_VALUE_SCALE (value2), &d2);
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
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type, d1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					(double) fmod (d1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
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
      || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true
	  && type1 != DB_TYPE_DOUBLE))
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

  numeric_coerce_num_to_double (db_locate_numeric (value1),
				DB_VALUE_SCALE (value1), &d1);

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
	  (void) numeric_internal_double_to_num (dtmp,
						 DB_VALUE_SCALE (value1),
						 num, &p, &s);
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
	  (void) numeric_internal_double_to_num (dtmp,
						 DB_VALUE_SCALE (value1),
						 num, &p, &s);
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
	  (void) numeric_internal_double_to_num (dtmp,
						 DB_VALUE_SCALE (value1),
						 num, &p, &s);
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
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2,
						    &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type2 != DB_TYPE_DOUBLE))
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
      numeric_coerce_num_to_double (db_locate_numeric (value2),
				    DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, d2);
	  (void) numeric_internal_double_to_num (dtmp,
						 MAX (DB_VALUE_SCALE
						      (value1),
						      DB_VALUE_SCALE
						      (value2)), num, &p, &s);
	  DB_MAKE_NUMERIC (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      if (d2 == 0)
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type, d1);
	}
      else
	{
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					(double) fmod (d1, d2));
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
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
      er_status = tp_value_str_auto_cast_to_number (value2, &cast_value2,
						    &type2);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type2 != DB_TYPE_DOUBLE))
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
      numeric_coerce_num_to_double (db_locate_numeric (value2),
				    DB_VALUE_SCALE (value2), &d2);
      break;
    case DB_TYPE_MONETARY:
      d2 = (DB_GET_MONETARY (value2))->amount;
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
	  er_status = ER_QPROC_INVALID_DATATYPE;
	}
      goto exit;
    }

  if (d2 == 0)
    {
      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
				    (DB_GET_MONETARY (value1))->type, d1);
    }
  else
    {
      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
				    (DB_GET_MONETARY (value1))->type,
				    (double) fmod (d1, d2));
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
	      tp_Double_domain.type->name);
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
  DB_BIGINT bi1, bi2;
  double dtmp;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  char num_string[(2 * DB_MAX_NUMERIC_PRECISION) + 4];
  char *ptr, *end;
  int need_round = 0;
  int p, s;
  DB_VALUE cast_value;
  int er_status = NO_ERROR;

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  type2 = DB_VALUE_DOMAIN_TYPE (value2);
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
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }

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
      DB_MAKE_BIGINT (result, (DB_BIGINT) dtmp);
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
      er_status = tp_value_str_auto_cast_to_number (value1, &cast_value,
						    &type1);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type1 != DB_TYPE_DOUBLE))
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
      else
	{
	  bi2 = DB_GET_SHORT (value2);
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
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				  ER_NUM_OVERFLOW, 0);
			  return ER_NUM_OVERFLOW;
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
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value1))->type,
				    dtmp);
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (er_errid () < 0
      && prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
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
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
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
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
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
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
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
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
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
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
	  break;
	}
      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value1),
				    DB_VALUE_SCALE (value1), &d1);
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
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
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
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
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
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1,
	  "log()");
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
 *   return: truncated date
 *   date(in)    :
 *   fmt(in):
 */
static DB_DATE *
truncate_date (DB_DATE * date, const DB_VALUE * format_str)
{
  int year, month, day;
  int error = NO_ERROR;
  DB_DATE *retval = date;
  TIMESTAMP_FORMAT format;
  int weekday;
  int days_months[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  assert (date != NULL);
  assert (format_str != NULL);

  error = db_get_truncate_format (format_str, &format);
  if (error != NO_ERROR)
    {
      retval = NULL;
      goto end;
    }

  if (format == DT_INVALID)
    {
      error = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      retval = NULL;
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
	  if (month == 2
	      && (year % 400 == 0 || (year % 100 != 0 && year % 4 == 0)))
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
      retval = NULL;
      goto end;
    }

  error = db_date_encode (date, month, day, year);
  if (error != NO_ERROR)
    {
      retval = NULL;
      goto end;
    }

end:

  return retval;
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
  DB_VALUE cast_value;
  int er_status = NO_ERROR;
  DB_DATE *date_p, date;

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  if (type2 != DB_TYPE_SHORT && type2 != DB_TYPE_INTEGER
      && type2 != DB_TYPE_BIGINT && !QSTR_IS_ANY_CHAR (type2))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
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

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      DB_MAKE_NULL (&cast_value);
      er_status = tp_value_str_auto_cast_to_number (value1, &cast_value,
						    &type1);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      true && type1 != DB_TYPE_DOUBLE))
	{
	  return er_status;
	}

      assert (type1 == DB_TYPE_DOUBLE);

      value1 = &cast_value;

      /* fall through */

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
	numeric_coerce_num_to_dec_str (db_locate_numeric (value1),
				       num_string);
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
	DB_MAKE_MONETARY_TYPE_AMOUNT (result,
				      (DB_GET_MONETARY (value1))->type, dtmp);
      }
      break;
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_TIMESTAMP:
      if (type1 == DB_TYPE_DATE)
	{
	  date_p = DB_GET_DATE (value1);
	}
      else if (type1 == DB_TYPE_DATETIME)
	{
	  date_p = &(DB_GET_DATETIME (value1)->date);
	}
      else			/* DB_TYPE_TIMESTAMP */
	{
	  db_timestamp_decode (DB_GET_TIMESTAMP (value1), &date, NULL);
	  date_p = &date;
	}

      date_p = truncate_date (date_p, value2);
      if (date_p == NULL)
	{
	  er_status = er_errid ();

	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      false)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	      return er_status;
	    }
	}
      else
	{
	  db_value_put_encoded_date (result, date_p);
	}
      break;
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  return NO_ERROR;
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
      numeric_coerce_num_to_double ((DB_C_NUMERIC) db_locate_numeric (value),
				    DB_VALUE_SCALE (value), &dtmp);
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR,
	      1, "acos()");
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR,
	      1, "asin()");
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

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR,
	      1, log_func);
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
	  if (tp_value_cast (value, &tmpval,
			     &tp_Double_domain, false) != DOMAIN_COMPATIBLE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_INVALID_DATATYPE, 0);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, 128);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      if (type == DB_TYPE_NUMERIC)
	{
	  snprintf (buf, 128, "%s (%u, %u)", type_name,
		    value->domain.numeric_info.precision,
		    value->domain.numeric_info.scale);
	}
      else
	{
	  snprintf (buf, 128, "%s (%d)", type_name,
		    value->domain.char_info.length);
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
 * db_width_bucket() -
 *   return:
 *   result(out):
 *   value1-4(in) : input db_value
 */
int
db_width_bucket (DB_VALUE * result, DB_VALUE * value1,
		 DB_VALUE * value2, DB_VALUE * value3, DB_VALUE * value4)
{
  double d1, d2, d3;
  int i_ret, i4;
  DB_TYPE type;

  assert (result != NULL && value1 != NULL
	  && value2 != NULL && value3 != NULL && value4 != NULL);

  if (DB_VALUE_TYPE (value1) == DB_TYPE_NULL
      || DB_VALUE_TYPE (value2) == DB_TYPE_NULL
      || DB_VALUE_TYPE (value3) == DB_TYPE_NULL
      || DB_VALUE_TYPE (value4) == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  i4 = DB_GET_INT (value4);
  if (i4 < 1 || i4 == DB_INT32_MAX)
    {
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  DB_MAKE_NULL (result);
	  return NO_ERROR;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_PROC_WIDTH_BUCKET_COUNT, 0);
	  return ER_PROC_WIDTH_BUCKET_COUNT;
	}
    }

  type = DB_VALUE_DOMAIN_TYPE (value1);
  switch (type)
    {
    case DB_TYPE_DATE:
      d1 = (double) *DB_GET_DATE (value1);
      d2 = (double) *DB_GET_DATE (value2);
      d3 = (double) *DB_GET_DATE (value3);
      break;

    case DB_TYPE_DATETIME:
      /* double can hold datetime type */
      d1 = ((double) DB_GET_DATETIME (value1)->date)
	* MILLISECONDS_OF_ONE_DAY + DB_GET_DATETIME (value1)->time;
      d2 = ((double) DB_GET_DATETIME (value2)->date)
	* MILLISECONDS_OF_ONE_DAY + DB_GET_DATETIME (value2)->time;
      d3 = ((double) DB_GET_DATETIME (value3)->date)
	* MILLISECONDS_OF_ONE_DAY + DB_GET_DATETIME (value3)->time;
      break;

    case DB_TYPE_TIMESTAMP:
      d1 = (double) *DB_GET_TIMESTAMP (value1);
      d2 = (double) *DB_GET_TIMESTAMP (value2);
      d3 = (double) *DB_GET_TIMESTAMP (value3);
      break;

    case DB_TYPE_TIME:
      d1 = (double) *DB_GET_TIME (value1);
      d2 = (double) *DB_GET_TIME (value2);
      d3 = (double) *DB_GET_TIME (value3);
      break;

    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_BIGINT:
    case DB_TYPE_NUMERIC:
      get_number_dbval_as_double (&d1, value1);
      get_number_dbval_as_double (&d2, value2);
      get_number_dbval_as_double (&d3, value3);
      break;

    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  DB_MAKE_NULL (result);
	  return NO_ERROR;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (d2 <= d3)
    {
      if (d1 < d2)
	{
	  i_ret = 0;
	}
      else if (d3 <= d1)
	{
	  i_ret = i4 + 1;
	}
      else
	{
	  i_ret = floor ((d1 - d2) / (d3 - d2) * i4) + 1;
	}
    }
  else
    {
      if (d2 < d1)
	{
	  i_ret = 0;
	}
      else if (d1 <= d3)
	{
	  i_ret = i4 + 1;
	}
      else
	{
	  i_ret = floor ((d2 - d1) / (d2 - d3) * i4) + 1;
	}
    }

  DB_MAKE_INT (result, i_ret);

  return NO_ERROR;
}
