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
 * arithmetic.c - arithmetic functions
 */

#ident "$Id$"

#include "arithmetic.h"

#include "config.h"
#include "crypt_opfunc.h"
#include "db_date.h"
#include "db_json.hpp"
#include "db_json_path.hpp"
#include "dbtype.h"
#include "error_manager.h"
#include "memory_private_allocator.hpp"
#include "memory_reference_store.hpp"
#include "numeric_opfunc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "string_opfunc.h"
#include "tz_support.h"

#include <algorithm>
#include <assert.h>
#include <cctype>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(SOLARIS)
#include <ieeefp.h>
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

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
static int is_str_find_all (DB_VALUE * val, bool & find_all);
static bool is_any_arg_null (DB_VALUE * const *args, int num_args);

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
      db_make_short (result, db_get_short (value));
      break;
    case DB_TYPE_INTEGER:
      db_make_int (result, db_get_int (value));
      break;
    case DB_TYPE_BIGINT:
      db_make_bigint (result, db_get_bigint (value));
      break;
    case DB_TYPE_FLOAT:
      dtmp = floor (db_get_float (value));
      db_make_float (result, (float) dtmp);
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      db_make_null (&cast_value);
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
      dtmp = floor (db_get_double (value));
      db_make_double (result, (double) dtmp);
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
	    numeric_coerce_num_to_dec_str (db_get_numeric (value), num_str_p);
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
		db_make_numeric (result, num, p, s);
	      }
	    else
	      {
		/* given numeric is positive or already rounded */
		numeric_coerce_dec_str_to_num (num_str + 1, num);
		db_make_numeric (result, num, p, s);
	      }
	  }
	else
	  {
	    /* given numeric number is already of integral type */
	    db_make_numeric (result, db_get_numeric (value), p, 0);
	  }

	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (db_get_monetary (value))->amount;
      dtmp = floor (dtmp);
      db_make_monetary (result, (db_get_monetary (value))->type, dtmp);
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
      db_make_short (result, db_get_short (value));
      break;
    case DB_TYPE_INTEGER:
      db_make_int (result, db_get_int (value));
      break;
    case DB_TYPE_BIGINT:
      db_make_bigint (result, db_get_bigint (value));
      break;
    case DB_TYPE_FLOAT:
      dtmp = ceil (db_get_float (value));
      db_make_float (result, (float) dtmp);
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      db_make_null (&cast_value);
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
      dtmp = ceil (db_get_double (value));
      db_make_double (result, (double) dtmp);
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
		    db_make_numeric (result, num, p, s);
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
		    db_make_numeric (result, num, p, s);
		  }
	      }
	    else
	      {
		/* the given numeric value is already an integer */
		db_make_numeric (result, db_locate_numeric (value), p, s);
	      }
	  }
	else
	  {
	    /* the given numeric value has a scale of 0 */
	    db_make_numeric (result, db_locate_numeric (value), p, 0);
	  }

	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (db_get_monetary (value))->amount;
      dtmp = ceil (dtmp);
      db_make_monetary (result, (db_get_monetary (value))->type, dtmp);
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
      itmp = db_get_short (value);
      if (itmp == 0)
	{
	  db_make_int (result, 0);
	}
      else if (itmp < 0)
	{
	  db_make_int (result, -1);
	}
      else
	{
	  db_make_int (result, 1);
	}
      break;
    case DB_TYPE_INTEGER:
      itmp = db_get_int (value);
      if (itmp == 0)
	{
	  db_make_int (result, 0);
	}
      else if (itmp < 0)
	{
	  db_make_int (result, -1);
	}
      else
	{
	  db_make_int (result, 1);
	}
      break;
    case DB_TYPE_BIGINT:
      bitmp = db_get_bigint (value);
      if (bitmp == 0)
	{
	  db_make_int (result, 0);
	}
      else if (bitmp < 0)
	{
	  db_make_int (result, -1);
	}
      else
	{
	  db_make_int (result, 1);
	}
      break;
    case DB_TYPE_FLOAT:
      dtmp = db_get_float (value);
      if (dtmp == 0)
	{
	  db_make_int (result, 0);
	}
      else if (dtmp < 0)
	{
	  db_make_int (result, -1);
	}
      else
	{
	  db_make_int (result, 1);
	}
      break;
    case DB_TYPE_DOUBLE:
      dtmp = db_get_double (value);
      if (dtmp == 0)
	{
	  db_make_int (result, 0);
	}
      else if (dtmp < 0)
	{
	  db_make_int (result, -1);
	}
      else
	{
	  db_make_int (result, 1);
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value), DB_VALUE_SCALE (value), &dtmp);
      if (dtmp == 0)
	{
	  db_make_int (result, 0);
	}
      else if (dtmp < 0)
	{
	  db_make_int (result, -1);
	}
      else
	{
	  db_make_int (result, 1);
	}
      break;
    case DB_TYPE_MONETARY:
      dtmp = (db_get_monetary (value))->amount;
      if (dtmp == 0)
	{
	  db_make_int (result, 0);
	}
      else if (dtmp < 0)
	{
	  db_make_int (result, -1);
	}
      else
	{
	  db_make_int (result, 1);
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
      stmp = db_get_short (value);
      stmp = abs (stmp);
      db_make_short (result, stmp);
      break;
    case DB_TYPE_INTEGER:
      itmp = db_get_int (value);
      itmp = abs (itmp);
      db_make_int (result, itmp);
      break;
    case DB_TYPE_BIGINT:
      bitmp = db_get_bigint (value);
      bitmp = llabs (bitmp);
      db_make_bigint (result, bitmp);
      break;
    case DB_TYPE_FLOAT:
      dtmp = db_get_float (value);
      dtmp = fabs (dtmp);
      db_make_float (result, (float) dtmp);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      db_make_null (&cast_value);
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
      dtmp = db_get_double (value);
      dtmp = fabs (dtmp);
      db_make_double (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      {
	unsigned char num[DB_NUMERIC_BUF_SIZE];

	numeric_db_value_abs (db_locate_numeric (value), num);
	db_make_numeric (result, num, DB_VALUE_PRECISION (value), DB_VALUE_SCALE (value));
	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (db_get_monetary (value))->amount;
      dtmp = fabs (dtmp);
      db_make_monetary (result, (db_get_monetary (value))->type, dtmp);
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
      s = db_get_short (value);
      dtmp = exp ((double) s);
      break;
    case DB_TYPE_INTEGER:
      i = db_get_int (value);
      dtmp = exp ((double) i);
      break;
    case DB_TYPE_BIGINT:
      bi = db_get_bigint (value);
      dtmp = exp ((double) bi);
      break;
    case DB_TYPE_FLOAT:
      f = db_get_float (value);
      dtmp = exp (f);
      break;
    case DB_TYPE_DOUBLE:
      d = db_get_double (value);
      dtmp = exp (d);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value), DB_VALUE_SCALE (value), &d);
      dtmp = exp (d);
      break;
    case DB_TYPE_MONETARY:
      d = (db_get_monetary (value))->amount;
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

  db_make_double (result, dtmp);
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
      s = db_get_short (value);
      if (s < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt ((double) s);
      break;
    case DB_TYPE_INTEGER:
      i = db_get_int (value);
      if (i < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt ((double) i);
      break;
    case DB_TYPE_BIGINT:
      bi = db_get_bigint (value);
      if (bi < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt ((double) bi);
      break;
    case DB_TYPE_FLOAT:
      f = db_get_float (value);
      if (f < 0)
	{
	  goto sqrt_error;
	}
      dtmp = sqrt (f);
      break;
    case DB_TYPE_DOUBLE:
      d = db_get_double (value);
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
      d = (db_get_monetary (value))->amount;
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

  db_make_double (result, dtmp);
  return NO_ERROR;

sqrt_error:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      db_make_null (result);
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
      db_make_null (result);
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

  db_make_double (result, dtmp);

  return NO_ERROR;

pow_overflow:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      db_make_null (result);
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
      db_make_null (result);
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

  db_make_null (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_SHORT);
#endif

  s1 = db_get_short (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = db_get_short (value2);
      if (s2 == 0)
	{
	  db_make_short (result, s1);
	}
      else
	{
	  db_make_short (result, (short) (s1 % s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = db_get_int (value2);
      if (i2 == 0)
	{
	  db_make_int (result, s1);
	}
      else
	{
	  db_make_int (result, (int) (s1 % i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = db_get_bigint (value2);
      if (bi2 == 0)
	{
	  db_make_bigint (result, s1);
	}
      else
	{
	  db_make_bigint (result, (DB_BIGINT) (s1 % bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = db_get_float (value2);
      if (f2 == 0)
	{
	  db_make_float (result, s1);
	}
      else
	{
	  db_make_float (result, (float) fmod ((float) s1, f2));
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
      d2 = db_get_double (value2);
      if (d2 == 0)
	{
	  db_make_double (result, s1);
	}
      else
	{
	  db_make_double (result, (double) fmod ((double) s1, d2));
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
	  dtmp = fmod ((double) s1, d2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2), num, &p, &s);
	  db_make_numeric (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (db_get_monetary (value2))->amount;
      if (d2 == 0)
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, s1);
	}
      else
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, (double) fmod (s1, d2));
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

  db_make_null (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_INTEGER);
#endif

  i1 = db_get_int (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = db_get_short (value2);
      if (s2 == 0)
	{
	  db_make_int (result, i1);
	}
      else
	{
	  db_make_int (result, (int) (i1 % s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = db_get_int (value2);
      if (i2 == 0)
	{
	  db_make_int (result, i1);
	}
      else
	{
	  db_make_int (result, (int) (i1 % i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = db_get_bigint (value2);
      if (bi2 == 0)
	{
	  db_make_bigint (result, i1);
	}
      else
	{
	  db_make_bigint (result, (DB_BIGINT) (i1 % bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = db_get_float (value2);
      if (f2 == 0)
	{
	  db_make_float (result, (float) i1);
	}
      else
	{
	  db_make_float (result, (float) fmod ((float) i1, f2));
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
      d2 = db_get_double (value2);
      if (d2 == 0)
	{
	  db_make_double (result, i1);
	}
      else
	{
	  db_make_double (result, (double) fmod ((double) i1, d2));
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
	  dtmp = fmod ((double) i1, d2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2), num, &p, &s);
	  db_make_numeric (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (db_get_monetary (value2))->amount;
      if (d2 == 0)
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, i1);
	}
      else
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, (double) fmod ((double) i1, d2));
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

  db_make_null (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_BIGINT);
#endif

  bi1 = db_get_bigint (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = db_get_short (value2);
      if (s2 == 0)
	{
	  db_make_bigint (result, bi1);
	}
      else
	{
	  db_make_bigint (result, (DB_BIGINT) (bi1 % s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = db_get_int (value2);
      if (i2 == 0)
	{
	  db_make_bigint (result, bi1);
	}
      else
	{
	  db_make_bigint (result, (DB_BIGINT) (bi1 % i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = db_get_bigint (value2);
      if (bi2 == 0)
	{
	  db_make_bigint (result, bi1);
	}
      else
	{
	  db_make_bigint (result, (DB_BIGINT) (bi1 % bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = db_get_float (value2);
      if (f2 == 0)
	{
	  db_make_float (result, (float) bi1);
	}
      else
	{
	  db_make_float (result, (float) fmod ((double) bi1, (double) f2));
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
      d2 = db_get_double (value2);
      if (d2 == 0)
	{
	  db_make_double (result, (double) bi1);
	}
      else
	{
	  db_make_double (result, (double) fmod ((double) bi1, d2));
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
	  db_make_numeric (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (db_get_monetary (value2))->amount;
      if (d2 == 0)
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, (double) bi1);
	}
      else
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, (double) fmod ((double) bi1, d2));
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

  db_make_null (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_FLOAT);
#endif

  f1 = db_get_float (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = db_get_short (value2);
      if (s2 == 0)
	{
	  db_make_float (result, f1);
	}
      else
	{
	  db_make_float (result, (float) fmod (f1, (float) s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = db_get_int (value2);
      if (i2 == 0)
	{
	  db_make_float (result, f1);
	}
      else
	{
	  db_make_float (result, (float) fmod (f1, (float) i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = db_get_bigint (value2);
      if (bi2 == 0)
	{
	  db_make_float (result, f1);
	}
      else
	{
	  db_make_float (result, (float) fmod ((double) f1, (double) bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = db_get_float (value2);
      if (f2 == 0)
	{
	  db_make_float (result, f1);
	}
      else
	{
	  db_make_float (result, (float) fmod (f1, f2));
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
      d2 = db_get_double (value2);
      if (d2 == 0)
	{
	  db_make_double (result, f1);
	}
      else
	{
	  db_make_double (result, (double) fmod ((double) f1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      /* common type of float and numeric is double. */
      if (d2 == 0)
	{
	  db_make_double (result, f1);
	}
      else
	{
	  db_make_double (result, fmod ((double) f1, d2));
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (db_get_monetary (value2))->amount;
      if (d2 == 0)
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, f1);
	}
      else
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, (double) fmod ((double) f1, d2));
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

  db_make_null (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_DOUBLE);
#endif

  d1 = db_get_double (value1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = db_get_short (value2);
      if (s2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, (double) fmod (d1, (double) s2));
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = db_get_int (value2);
      if (i2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, (double) fmod (d1, (double) i2));
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = db_get_bigint (value2);
      if (bi2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, (double) fmod (d1, (double) bi2));
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = db_get_float (value2);
      if (f2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, (double) fmod (d1, (double) f2));
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
      d2 = db_get_double (value2);
      if (d2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, (double) fmod (d1, d2));
	}
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      if (d2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, (double) fmod (d1, d2));
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (db_get_monetary (value2))->amount;
      if (d2 == 0)
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, d1);
	}
      else
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, (double) fmod (d1, d2));
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

  db_make_null (&cast_value1);

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

  db_make_null (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_NUMERIC);
#endif

  numeric_coerce_num_to_double (db_locate_numeric (value1), DB_VALUE_SCALE (value1), &d1);

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      s2 = db_get_short (value2);
      if (s2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, (double) s2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1), num, &p, &s);
	  db_make_numeric (result, num, p, s);
	}
      break;
    case DB_TYPE_INTEGER:
      i2 = db_get_int (value2);
      if (i2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, (double) i2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1), num, &p, &s);
	  db_make_numeric (result, num, p, s);
	}
      break;
    case DB_TYPE_BIGINT:
      bi2 = db_get_bigint (value2);
      if (bi2 == 0)
	{
	  (void) numeric_db_value_coerce_to_num (value1, result, &data_stat);
	}
      else
	{
	  dtmp = fmod (d1, (double) bi2);
	  (void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1), num, &p, &s);
	  db_make_numeric (result, num, p, s);
	}
      break;
    case DB_TYPE_FLOAT:
      f2 = db_get_float (value2);
      /* common type of float and numeric is double */
      if (f2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, fmod (d1, (double) f2));
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
      d2 = db_get_double (value2);
      if (d2 == 0)
	{
	  db_make_double (result, d1);
	}
      else
	{
	  db_make_double (result, (double) fmod (d1, d2));
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
	  db_make_numeric (result, num, p, s);
	}
      break;
    case DB_TYPE_MONETARY:
      d2 = (db_get_monetary (value2))->amount;
      if (d2 == 0)
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, d1);
	}
      else
	{
	  db_make_monetary (result, (db_get_monetary (value2))->type, (double) fmod (d1, d2));
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

  db_make_null (&cast_value2);

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  assert (type1 == DB_TYPE_MONETARY);
#endif

  d1 = (db_get_monetary (value1))->amount;
  d2 = 0;

  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  switch (type2)
    {
    case DB_TYPE_SHORT:
      d2 = db_get_short (value2);
      break;
    case DB_TYPE_INTEGER:
      d2 = db_get_int (value2);
      break;
    case DB_TYPE_BIGINT:
      d2 = (double) db_get_bigint (value2);
      break;
    case DB_TYPE_FLOAT:
      d2 = db_get_float (value2);
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
      d2 = db_get_double (value2);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value2), DB_VALUE_SCALE (value2), &d2);
      break;
    case DB_TYPE_MONETARY:
      d2 = (db_get_monetary (value2))->amount;
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
      db_make_monetary (result, (db_get_monetary (value1))->type, d1);
    }
  else
    {
      db_make_monetary (result, (db_get_monetary (value1))->type, (double) fmod (d1, d2));
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
  volatile double scale_up, result;

  if (num == 0)
    {
      return num;
    }

  scale_up = pow (10, integer);

  if (!FINITE (num * scale_up))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, tp_Double_domain.type->name);
    }

  result = round (num * scale_up) / scale_up;

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
  error = db_make_date (result, month, day, year);

end:
  if (error != NO_ERROR && prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      error = NO_ERROR;
      db_make_null (result);
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

  db_make_null (&cast_value);
  db_make_null (&cast_format);

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
	  db_make_string (&cast_format, "dd");
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
      d2 = (double) db_get_int (value2);
    }
  else if (type2 == DB_TYPE_BIGINT)
    {
      d2 = (double) db_get_bigint (value2);
    }
  else if (type2 == DB_TYPE_SHORT)
    {
      d2 = (double) db_get_short (value2);
    }
  else if (type2 == DB_TYPE_DOUBLE)
    {
      d2 = db_get_double (value2);
    }
  else				/* cast to INTEGER */
    {
      if (QSTR_IS_ANY_CHAR (type2) && strcasecmp (DB_GET_STRING_SAFE (value2), "default") == 0)
	{
	  db_make_int (&cast_format, 0);
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
	  d2 = db_get_int (value2);
	}
    }

  /* round double */
  switch (type1)
    {
    case DB_TYPE_SHORT:
      s1 = db_get_short (value1);
      dtmp = round_double (s1, d2);
      db_make_short (result, (short) dtmp);
      break;
    case DB_TYPE_INTEGER:
      i1 = db_get_int (value1);
      dtmp = round_double (i1, d2);
      db_make_int (result, (int) dtmp);
      break;
    case DB_TYPE_BIGINT:
      bi1 = db_get_bigint (value1);
      dtmp = round_double ((double) bi1, d2);
      bi_tmp = (DB_BIGINT) dtmp;
#if defined(AIX)
      /* in AIX, double to long will not overflow, make it the same as linux. */
      if (dtmp == (double) DB_BIGINT_MAX)
	{
	  bi_tmp = DB_BIGINT_MIN;
	}
#endif
      db_make_bigint (result, bi_tmp);
      break;
    case DB_TYPE_FLOAT:
      f1 = db_get_float (value1);
      dtmp = round_double (f1, d2);
      db_make_float (result, (float) dtmp);
      break;
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      db_make_null (&cast_value);
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
      d1 = db_get_double (value1);
      dtmp = round_double (d1, d2);
      db_make_double (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      memset (num_string, 0, sizeof (num_string));
      numeric_coerce_num_to_dec_str (db_locate_numeric (value1), num_string);
      p = DB_VALUE_PRECISION (value1);
      s = DB_VALUE_SCALE (value1);
      end = num_string + strlen (num_string);

      if (type2 == DB_TYPE_BIGINT)
	{
	  bi2 = db_get_bigint (value2);
	}
      else if (type2 == DB_TYPE_INTEGER)
	{
	  bi2 = db_get_int (value2);
	}
      else if (type2 == DB_TYPE_SHORT)
	{
	  bi2 = db_get_short (value2);
	}
      else			/* double */
	{
	  bi2 = (DB_BIGINT) db_get_double (value2);
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
      db_make_numeric (result, num, p, s);
      break;
    case DB_TYPE_MONETARY:
      d1 = (db_get_monetary (value1))->amount;
      dtmp = round_double (d1, d2);
      db_make_monetary (result, (db_get_monetary (value1))->type, dtmp);
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
      db_make_null (result);
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
      s1 = db_get_short (value1);
      if (s1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = db_get_short (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) s1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = db_get_int (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) s1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = db_get_bigint (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) s1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = db_get_float (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) s1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = db_get_double (value2);
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
	  d2 = (db_get_monetary (value2))->amount;
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
      bi1 = db_get_bigint (value1);
      if (bi1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = db_get_short (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = db_get_int (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = db_get_bigint (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = db_get_float (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) bi1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = db_get_double (value2);
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
	  d2 = (db_get_monetary (value2))->amount;
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
      i1 = db_get_int (value1);
      if (i1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = db_get_short (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) i1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = db_get_int (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) i1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = db_get_bigint (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) i1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = db_get_float (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) i1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = db_get_double (value2);
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
	  d2 = (db_get_monetary (value2))->amount;
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
      f1 = db_get_float (value1);
      if (f1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = db_get_short (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 ((double) f1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = db_get_int (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 ((double) f1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = db_get_bigint (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 ((double) f1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = db_get_float (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 ((double) f1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = db_get_double (value2);
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
	  d2 = (db_get_monetary (value2))->amount;
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
      d1 = db_get_double (value1);
      if (d1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = db_get_short (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 (d1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = db_get_int (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 (d1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = db_get_bigint (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 (d1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = db_get_float (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 (d1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = db_get_double (value2);
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
	  d2 = (db_get_monetary (value2))->amount;
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
	  s2 = db_get_short (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 (d1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = db_get_int (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 (d1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = db_get_bigint (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 (d1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = db_get_float (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 (d1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = db_get_double (value2);
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
	  d2 = (db_get_monetary (value2))->amount;
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
      d1 = (db_get_monetary (value1))->amount;
      if (d1 <= 1)
	{
	  goto log_error;
	}

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = db_get_short (value2);
	  if (s2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) s2) / log10 (d1);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = db_get_int (value2);
	  if (i2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) i2) / log10 (d1);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = db_get_bigint (value2);
	  if (bi2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) bi2) / log10 (d1);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = db_get_float (value2);
	  if (f2 <= 0)
	    {
	      goto log_error;
	    }
	  dtmp = log10 ((double) f2) / log10 (d1);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = db_get_double (value2);
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
	  d2 = (db_get_monetary (value2))->amount;
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

  db_make_double (result, dtmp);
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

  db_make_null (&cast_value);
  db_make_null (&cast_format);

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
      db_make_null (&cast_value);
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
	      db_make_null (result);
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
  if (type2 == DB_TYPE_CHAR && strcasecmp (db_get_string (value2), "default") == 0)
    {
      if (TP_IS_DATE_TYPE (type1))
	{
	  db_make_string (&cast_format, "dd");
	}
      else
	{
	  db_make_int (&cast_format, 0);
	  type2 = DB_TYPE_INTEGER;
	}

      value2 = &cast_format;
    }

  if (type2 == DB_TYPE_INTEGER)
    {
      bi2 = db_get_int (value2);
    }
  else if (type2 == DB_TYPE_BIGINT)
    {
      bi2 = db_get_bigint (value2);
    }
  else if (type2 == DB_TYPE_SHORT)
    {
      bi2 = db_get_short (value2);
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
	      db_make_null (result);
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

      bi2 = db_get_bigint (&cast_format);
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

	s1 = db_get_short (value1);
	dtmp = truncate_double (s1, (double) bi2);
	db_make_short (result, (short) dtmp);
      }
      break;
    case DB_TYPE_INTEGER:
      {
	int i1;

	i1 = db_get_int (value1);
	dtmp = truncate_double (i1, (double) bi2);
	db_make_int (result, (int) dtmp);
      }
      break;
    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bi1;

	bi1 = db_get_bigint (value1);
	bi1 = truncate_bigint (bi1, bi2);
	db_make_bigint (result, bi1);
      }
      break;
    case DB_TYPE_FLOAT:
      {
	float f1;

	f1 = db_get_float (value1);
	dtmp = truncate_double (f1, (double) bi2);
	db_make_float (result, (float) dtmp);
      }
      break;

    case DB_TYPE_DOUBLE:
      {
	double d1;

	d1 = db_get_double (value1);
	dtmp = truncate_double (d1, (double) bi2);
	db_make_double (result, (double) dtmp);
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
	db_make_numeric (result, num, p, s);
      }
      break;
    case DB_TYPE_MONETARY:
      {
	double d1;

	d1 = (db_get_monetary (value1))->amount;
	dtmp = truncate_double (d1, (double) bi2);
	db_make_monetary (result, (db_get_monetary (value1))->type, dtmp);
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
	  date = *(db_get_date (value1));
	}
      else if (type1 == DB_TYPE_DATETIME)
	{
	  date = db_get_datetime (value1)->date;
	}
      else if (type1 == DB_TYPE_DATETIMELTZ)
	{
	  DB_DATETIME local_dt, *p_dt;

	  p_dt = db_get_datetime (value1);

	  er_status = tz_datetimeltz_to_local (p_dt, &local_dt);
	  if (er_status != NO_ERROR)
	    {
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
		{
		  er_clear ();
		  db_make_null (result);
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

	  p_dt_tz = db_get_datetimetz (value1);

	  er_status = tz_utc_datetimetz_to_local (&p_dt_tz->datetime, &p_dt_tz->tz_id, &local_dt);

	  if (er_status != NO_ERROR)
	    {
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true)
		{
		  er_clear ();
		  db_make_null (result);
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
		  db_make_null (result);
		  er_status = NO_ERROR;
		}
	      goto end;
	    }
	}
      else
	{
	  assert (type1 == DB_TYPE_TIMESTAMP || type1 == DB_TYPE_TIMESTAMPLTZ);
	  (void) db_timestamp_decode_ses (db_get_timestamp (value1), &date, NULL);
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
  db_make_int (result, lrand48 ());

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
  db_make_double (result, drand48 ());

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
      s = db_get_short (value);
      dtmp = (double) s;
      break;
    case DB_TYPE_INTEGER:
      i = db_get_int (value);
      dtmp = (double) i;
      break;
    case DB_TYPE_BIGINT:
      bi = db_get_bigint (value);
      dtmp = (double) bi;
      break;
    case DB_TYPE_FLOAT:
      f = db_get_float (value);
      dtmp = (double) f;
      break;
    case DB_TYPE_DOUBLE:
      dtmp = db_get_double (value);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double ((DB_C_NUMERIC) db_locate_numeric (value), DB_VALUE_SCALE (value), &dtmp);
      break;
    case DB_TYPE_MONETARY:
      dtmp = (db_get_monetary (value))->amount;
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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = cos (dtmp);

  db_make_double (result, dtmp);
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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = sin (dtmp);

  db_make_double (result, dtmp);
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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = tan (dtmp);

  db_make_double (result, dtmp);
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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (dtmp == 0)
    {
      db_make_null (result);
    }
  else
    {
      dtmp = 1 / tan (dtmp);
      db_make_double (result, dtmp);
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
      db_make_null (result);
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

  db_make_double (result, dtmp);
  return NO_ERROR;

error:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      db_make_null (result);
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
      db_make_null (result);
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

  db_make_double (result, dtmp);
  return NO_ERROR;

error:
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      db_make_null (result);
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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = atan (dtmp);

  db_make_double (result, dtmp);
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
      db_make_null (result);
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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&d2, value2);
  if (err != NO_ERROR)
    {
      return err;
    }

  /* function call, all is double type */
  dtmp = atan2 (d, d2);

  db_make_double (result, dtmp);
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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = dtmp * (double) 57.295779513082320876798154814105;	/* 180 / PI */
  db_make_double (result, dtmp);

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
      db_make_null (result);
      return NO_ERROR;
    }

  err = get_number_dbval_as_double (&dtmp, value);
  if (err != NO_ERROR)
    {
      return err;
    }

  dtmp = dtmp * (double) 0.017453292519943295769236907684886;	/* PI / 180 */
  db_make_double (result, dtmp);

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
      db_make_null (result);
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
      db_make_double (result, dtmp);
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
      db_make_null (result);
    }
  else
    {
      switch (type)
	{
	case DB_TYPE_SHORT:
	  s = db_get_short (value);
	  for (c = 0; s; c++)
	    {
	      s &= s - 1;
	    }
	  break;

	case DB_TYPE_INTEGER:
	  i = db_get_int (value);
	  for (c = 0; i; c++)
	    {
	      i &= i - 1;
	    }
	  break;

	case DB_TYPE_BIGINT:
	  bi = db_get_bigint (value);
	  for (c = 0; bi; c++)
	    {
	      bi &= bi - 1;
	    }
	  break;

	case DB_TYPE_FLOAT:
	  f = db_get_float (value);
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
	  d = (db_get_monetary (value))->amount;
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
	  /* FALLTHRU */
	case DB_TYPE_DOUBLE:
	  d = db_get_double (tmpval_p);
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

      db_make_int (result, c);
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
      buf = (char *) db_private_alloc (NULL, 128);
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
      s = db_get_short (value);
      dtmp = (long double) s;
      break;

    case DB_TYPE_INTEGER:
      i = db_get_int (value);
      dtmp = (long double) i;
      break;

    case DB_TYPE_BIGINT:
      bi = db_get_bigint (value);
      dtmp = (long double) bi;
      break;

    case DB_TYPE_FLOAT:
      f = db_get_float (value);
      dtmp = (long double) f;
      break;

    case DB_TYPE_DOUBLE:
      dtmp = (long double) db_get_double (value);
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
      dtmp = (long double) (db_get_monetary (value))->amount;
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

  db_make_null (&cmp_result);
  db_make_null (&n1);
  db_make_null (&n2);
  db_make_null (&n3);
  db_make_null (&n4);

  er_status = numeric_db_value_compare (value2, value3, &cmp_result);
  if (er_status != NO_ERROR)
    {
      return er_status;
    }

  c = db_get_int (&cmp_result);
  if (c == 0 || c == -1)
    {
      /* value2 <= value3 */

      er_status = numeric_db_value_compare (value1, value2, &cmp_result);
      if (er_status != NO_ERROR)
	{
	  return er_status;
	}

      if (db_get_int (&cmp_result) < 0)
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

	  if (db_get_int (&cmp_result) < 1)
	    {
	      numeric_coerce_num_to_double ((DB_C_NUMERIC) db_get_numeric (value4), DB_VALUE_SCALE (value4), &res);
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

	      numeric_coerce_num_to_double (db_get_numeric (&n4), DB_VALUE_SCALE (&n4), &res);
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

      if (db_get_int (&cmp_result) < 0)
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

	  if (db_get_int (&cmp_result) < 1)
	    {
	      numeric_coerce_num_to_double ((DB_C_NUMERIC) db_get_numeric (value4), DB_VALUE_SCALE (value4), &res);
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

	      numeric_coerce_num_to_double (db_get_numeric (&n4), DB_VALUE_SCALE (&n4), &res);
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
	  db_make_null (result); \
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
          db_make_null (result); \
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

  assert (result != NULL && value1 != NULL && value2 != NULL && value3 != NULL && value4 != NULL);

  db_make_null (&cast_value1);
  db_make_null (&cast_value2);
  db_make_null (&cast_value3);
  db_make_null (&cast_value4);

  if (DB_VALUE_TYPE (value1) == DB_TYPE_NULL || DB_VALUE_TYPE (value2) == DB_TYPE_NULL
      || DB_VALUE_TYPE (value3) == DB_TYPE_NULL || DB_VALUE_TYPE (value4) == DB_TYPE_NULL)
    {
      db_make_null (result);
      return NO_ERROR;
    }

  d4 = db_get_double (value4);
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
	      er_clear ();	// forget previous error to try datetime

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
      d1 = (double) *db_get_date (value1);
      d2 = (double) *db_get_date (value2);
      d3 = (double) *db_get_date (value3);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      /* double can hold datetime type */
      d1 = ((double) db_get_datetime (value1)->date) * MILLISECONDS_OF_ONE_DAY + db_get_datetime (value1)->time;
      d2 = ((double) db_get_datetime (value2)->date) * MILLISECONDS_OF_ONE_DAY + db_get_datetime (value2)->time;
      d3 = ((double) db_get_datetime (value3)->date) * MILLISECONDS_OF_ONE_DAY + db_get_datetime (value3)->time;
      break;

    case DB_TYPE_DATETIMETZ:
      /* double can hold datetime type */
      d1 = (((double) db_get_datetimetz (value1)->datetime.date) * MILLISECONDS_OF_ONE_DAY
	    + db_get_datetimetz (value1)->datetime.time);
      d2 = (((double) db_get_datetimetz (value2)->datetime.date) * MILLISECONDS_OF_ONE_DAY
	    + db_get_datetimetz (value2)->datetime.time);
      d3 = (((double) db_get_datetimetz (value3)->datetime.date) * MILLISECONDS_OF_ONE_DAY
	    + db_get_datetimetz (value3)->datetime.time);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      d1 = (double) *db_get_timestamp (value1);
      d2 = (double) *db_get_timestamp (value2);
      d3 = (double) *db_get_timestamp (value3);
      break;

    case DB_TYPE_TIMESTAMPTZ:
      d1 = (double) (db_get_timestamptz (value1)->timestamp);
      d2 = (double) (db_get_timestamptz (value2)->timestamp);
      d3 = (double) (db_get_timestamptz (value3)->timestamp);
      break;

    case DB_TYPE_TIME:
      d1 = (double) *db_get_time (value1);
      d2 = (double) *db_get_time (value2);
      d3 = (double) *db_get_time (value3);
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

      db_make_int (&cast_value4, ((int) d4));
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

  db_make_int (result, ((int) d_ret));

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

  db_make_null (result);

  if (DB_IS_NULL (value) || db_get_double (value) < 0.0)
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);

      goto end;
    }

  million_sec = (long) (db_get_double (value) * 1000L);

  error = msleep (million_sec);
  if (error == NO_ERROR)
    {
      db_make_int (result, 0);
    }
  else
    {
      db_make_int (result, 1);

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
	  crypt_crc32 (db_get_string (value), db_get_string_size (value), &hash_result);
	  db_make_int (result, hash_result);
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

int
db_evaluate_json_contains (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int error_code = NO_ERROR;
  JSON_DOC_STORE source;

  db_make_null (result);
  if (num_args < 2)
    {
      assert (false);
      return ER_FAILED;
    }

  if (is_any_arg_null (arg, num_args))
    {
      return NO_ERROR;
    }

  const DB_VALUE *json = arg[0];
  const DB_VALUE *value = arg[1];
  const DB_VALUE *path = num_args == 3 ? arg[2] : NULL;

  error_code = db_value_to_json_doc (*json, false, source);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (path != NULL)
    {
      JSON_DOC_STORE extracted_doc;
      /* *INDENT-OFF* */
      std::string raw_path;
      error_code = db_value_to_json_path (*path, F_JSON_CONTAINS, raw_path);
      if (error_code != NO_ERROR)
        {
          ASSERT_ERROR ();
          return error_code;
        }
      error_code = db_json_extract_document_from_path (source.get_immutable (), raw_path, extracted_doc);
      source = std::move (extracted_doc);
      /* *INDENT-ON* */
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }
  else
    {
      //
    }

  if (source.get_immutable () != NULL)
    {
      bool has_member = false;
      JSON_DOC_STORE value_doc;

      int error_code = db_value_to_json_doc (*value, false, value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      error_code = db_json_value_is_contained_in_doc (source.get_immutable (), value_doc.get_immutable (), has_member);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      db_make_int (result, has_member ? 1 : 0);
    }
  return NO_ERROR;
}

int
db_evaluate_json_type_dbval (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  db_make_null (result);
  if (num_args != 1)
    {
      assert (false);
      return ER_FAILED;
    }
  DB_VALUE *json = arg[0];
  if (DB_IS_NULL (json))
    {
      return NO_ERROR;;
    }
  else
    {
      const char *type;
      unsigned int length;
      JSON_DOC_STORE doc;

      int error_code = db_value_to_json_doc (*json, false, doc);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      type = db_json_get_type_as_str (doc.get_immutable ());
      length = strlen (type);

      return db_make_varchar (result, length, type, length, LANG_COERCIBLE_CODESET, LANG_COERCIBLE_COLL);
    }
}

int
db_evaluate_json_valid (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  db_make_null (result);
  if (num_args != 1)
    {
      assert (false);
      return ER_FAILED;
    }
  DB_VALUE *value = arg[0];
  if (DB_IS_NULL (value))
    {
      return NO_ERROR;
    }
  DB_TYPE type = db_value_domain_type (value);
  bool valid;
  if (type == DB_TYPE_JSON)
    {
      valid = true;
    }
  else if (TP_IS_CHAR_TYPE (type))
    {
      /* *INDENT-OFF* */
      valid = db_json_is_valid (std::string (db_get_string (value), db_get_string_size (value)).c_str ());
      /* *INDENT-ON* */
    }
  else
    {
      valid = false;
    }
  db_make_int (result, valid ? 1 : 0);
  return NO_ERROR;
}

int
db_evaluate_json_length (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  JSON_DOC_STORE source_doc;
  int error_code = NO_ERROR;

  db_make_null (result);
  if (num_args < 1 || num_args > 2)
    {
      assert (false);
      return ER_FAILED;
    }

  if (is_any_arg_null (arg, num_args))
    {
      return NO_ERROR;
    }

  DB_VALUE *json = arg[0];
  DB_VALUE *path = (num_args == 1) ? NULL : arg[1];
  unsigned int length;

  error_code = db_value_to_json_doc (*json, false, source_doc);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (path != NULL)
    {
      JSON_DOC_STORE extracted_doc;
      /* *INDENT-OFF* */
      std::string raw_path;
      error_code = db_value_to_json_path (*path, F_JSON_LENGTH, raw_path);
      if (error_code != NO_ERROR)
        {
          ASSERT_ERROR ();
          return error_code;
        }
      error_code = db_json_extract_document_from_path (source_doc.get_immutable (), raw_path, extracted_doc, false);
      source_doc = std::move (extracted_doc);
      /* *INDENT-ON* */
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  if (source_doc.get_immutable () != NULL)
    {
      length = db_json_get_length (source_doc.get_immutable ());
      db_make_int (result, length);
    }
  return NO_ERROR;
}

int
db_evaluate_json_depth (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  db_make_null (result);
  if (num_args != 1)
    {
      assert (false);
      return ER_FAILED;
    }
  DB_VALUE *json = arg[0];
  if (DB_IS_NULL (json))
    {
      return NO_ERROR;
    }
  JSON_DOC_STORE source_doc;
  int error_code = db_value_to_json_doc (*json, false, source_doc);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  unsigned int depth = db_json_get_depth (source_doc.get_immutable ());

  return db_make_int (result, depth);
}

int
db_evaluate_json_quote (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  if (num_args != 1)
    {
      assert (false);
      return ER_FAILED;
    }
  return db_string_quote (arg[0], result);
}

int
db_evaluate_json_unquote (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int error_code = NO_ERROR;
  db_make_null (result);
  if (num_args != 1)
    {
      assert (false);
      return ER_FAILED;
    }
  DB_VALUE *json = arg[0];
  if (DB_IS_NULL (json))
    {
      return NO_ERROR;
    }
  char *str = NULL;
  JSON_DOC_STORE source_doc;
  error_code = db_value_to_json_doc (*json, false, source_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  error_code = db_json_unquote (*source_doc.get_immutable (), str);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = db_make_string (result, str);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  // db_json_unquote uses strdup, therefore set need_clear flag
  result->need_clear = true;
  return NO_ERROR;
}

int
db_evaluate_json_pretty (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int error_code = NO_ERROR;
  db_make_null (result);

  if (num_args != 1)
    {
      assert (false);
      return ER_FAILED;
    }
  DB_VALUE *json = arg[0];
  if (DB_IS_NULL (json))
    {
      return NO_ERROR;
    }
  char *str = NULL;
  JSON_DOC_STORE source_doc;
  error_code = db_value_to_json_doc (*json, false, source_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  db_json_pretty_func (*source_doc.get_immutable (), str);

  error_code = db_make_string (result, str);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  // db_json_pretty_func uses strdup, therefore set need_clear flag
  result->need_clear = true;

  return error_code;
}

int
db_accumulate_json_arrayagg (const DB_VALUE * json_db_val, DB_VALUE * json_res)
{
  int error_code = NO_ERROR;
  JSON_DOC_STORE val_doc;
  JSON_DOC_STORE result_doc;

  if (DB_IS_NULL (json_db_val))
    {
      // this case should not be possible because we already wrapped a NULL value into a JSON with type DB_JSON_NULL
      assert (false);
      db_make_null (json_res);
      return ER_FAILED;
    }

  // get the current value
  error_code = db_value_to_json_value (*json_db_val, val_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  // append to existing document
  // allocate only first time
  if (DB_IS_NULL (json_res))
    {
      result_doc.create_mutable_reference ();
    }
  else
    {
      result_doc.set_mutable_reference (db_get_json_document (json_res));
    }

  if (result_doc.get_immutable () == NULL)
    {
      db_make_null (json_res);
      return ER_FAILED;
    }

  db_json_add_element_to_array (result_doc.get_mutable (), val_doc.get_immutable ());

  db_make_json_from_doc_store_and_release (*json_res, result_doc);
  return error_code;
}

/*
 * db_accumulate_json_objectagg () - Construct a Member (key-value pair) and add it in the result_json
 *
 * return                  : error_code
 * json_key (in)           : the key of the pair
 * json_val (in)           : the value of the pair
 * json_res (in)           : the DB_VALUE that contains the document where we want to insert
 */
int
db_accumulate_json_objectagg (const DB_VALUE * json_key, const DB_VALUE * json_db_val, DB_VALUE * json_res)
{
  int error_code = NO_ERROR;
  JSON_DOC_STORE val_doc;
  JSON_DOC_STORE result_doc;

  // this case should not be possible because we checked before if the key is NULL
  // and wrapped the value with a JSON with DB_JSON_NULL type
  if (DB_IS_NULL (json_key) || DB_IS_NULL (json_db_val))
    {
      assert (false);
      db_make_null (json_res);
      return ER_FAILED;
    }

  // get the current key
  /* *INDENT-OFF* */
  std::string key_str;
  /* *INDENT-ON* */
  error_code = db_value_to_json_key (*json_key, key_str);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  // get the current value
  error_code = db_value_to_json_value (*json_db_val, val_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  // append to existing document
  // allocate only first time
  if (DB_IS_NULL (json_res))
    {
      result_doc.create_mutable_reference ();
    }
  else
    {
      result_doc.set_mutable_reference (db_get_json_document (json_res));
    }

  if (result_doc.get_immutable () == NULL)
    {
      db_make_null (json_res);
      return ER_FAILED;
    }

  error_code = db_json_add_member_to_object (result_doc.get_mutable (), key_str.c_str (), val_doc.get_immutable ());
  db_make_json_from_doc_store_and_release (*json_res, result_doc);
  if (error_code == ER_JSON_DUPLICATE_KEY)
    {
      // ignore
      er_clear ();
      error_code = NO_ERROR;
    }
  return error_code;
}

//
// db_evaluate_json_extract () - extract paths from JSON and return a JSON object if there is only one path or a JSON
//                               array if there are multiple paths
//
// return        : error code
// result (in)   : result
// args[] (in)   :
// num_args (in) :
//
// TODO: we need to change the args type of all JSON function to const DB_VALUE *[]
//
int
db_evaluate_json_extract (DB_VALUE * result, DB_VALUE * const *args, int num_args)
{
  db_make_null (result);

  if (num_args < 2)
    {
      // should be detected early
      assert (false);
      return ER_FAILED;
    }

  // there are multiple paths; the result of extract is a JSON_ARRAY with all extracted values
  int error_code = NO_ERROR;
  JSON_DOC_STORE source_doc;

  if (is_any_arg_null (args, num_args))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*args[0], false, source_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  /* *INDENT-OFF* */
  std::vector<std::string> paths;
  /* *INDENT-ON* */
  for (int path_idx = 1; path_idx < num_args; path_idx++)
    {
      const DB_VALUE *path_value = args[path_idx];
      paths.emplace_back ();
      error_code = db_value_to_json_path (*path_value, F_JSON_EXTRACT, paths.back ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  JSON_DOC_STORE res_doc;
  error_code = db_json_extract_document_from_path (source_doc.get_immutable (), paths, res_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (db_json_get_type (res_doc.get_immutable ()) != DB_JSON_NULL)
    {
      db_make_json_from_doc_store_and_release (*result, res_doc);
    }

  return NO_ERROR;
}

int
db_evaluate_json_object (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int i;
  int error_code = NO_ERROR;
  JSON_DOC_STORE value_doc;

  db_make_null (result);

  if (num_args % 2 != 0)
    {
      assert (false);		// should be caught earlier
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  JSON_DOC_STORE new_doc;
  new_doc.set_mutable_reference (db_json_make_json_object ());

  for (i = 0; i < num_args; i += 2)
    {
      if (DB_IS_NULL (arg[i]))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_OBJECT_NAME_IS_NULL, 0);
	  return ER_JSON_OBJECT_NAME_IS_NULL;
	}

      /* *INDENT-OFF* */
      std::string value_key;
      /* *INDENT-ON* */
      error_code = db_value_to_json_key (*arg[i], value_key);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      error_code = db_value_to_json_value (*arg[i + 1], value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      error_code =
	db_json_add_member_to_object (new_doc.get_mutable (), value_key.c_str (), value_doc.get_immutable ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_array (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int error_code;
  JSON_DOC_STORE new_doc;
  new_doc.set_mutable_reference (db_json_make_json_array ());
  JSON_DOC_STORE value_doc;

  db_make_null (result);

  for (int i = 0; i < num_args; i++)
    {
      error_code = db_value_to_json_value (*arg[i], value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      db_json_add_element_to_array (new_doc.get_mutable (), value_doc.get_immutable ());
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_insert (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int i, error_code = NO_ERROR;
  JSON_DOC_STORE new_doc;
  JSON_DOC_STORE value_doc;

  db_make_null (result);

  if (num_args < 3 || num_args % 2 == 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (DB_IS_NULL (arg[0]))
    {
      return db_make_null (result);
    }

  error_code = db_value_to_json_doc (*arg[0], true, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  for (i = 1; i < num_args; i += 2)
    {
      if (DB_IS_NULL (arg[i]))
	{
	  return db_make_null (result);
	}

      // extract path
      /* *INDENT-OFF* */
      std::string value_path;
      /* *INDENT-ON* */
      error_code = db_value_to_json_path (*arg[i], F_JSON_INSERT, value_path);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // extract json value
      error_code = db_value_to_json_value (*arg[i + 1], value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // insert into result the value at required path
      error_code = db_json_insert_func (value_doc.get_immutable (), *new_doc.get_mutable (), value_path.c_str ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_replace (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int i, error_code = NO_ERROR;
  JSON_DOC_STORE new_doc;
  JSON_DOC_STORE value_doc;

  db_make_null (result);

  if (num_args < 3 || num_args % 2 == 0)
    {
      assert_release (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (DB_IS_NULL (arg[0]))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*arg[0], true, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  for (i = 1; i < num_args; i += 2)
    {
      if (DB_IS_NULL (arg[i]))
	{
	  return db_make_null (result);
	}

      // extract path
      /* *INDENT-OFF* */
      std::string value_path;
      /* *INDENT-ON* */
      error_code = db_value_to_json_path (*arg[i], F_JSON_REPLACE, value_path);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // extract json value
      error_code = db_value_to_json_value (*arg[i + 1], value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // insert into result the value at requred path
      error_code = db_json_replace_func (value_doc.get_immutable (), *new_doc.get_mutable (), value_path.c_str ());
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_set (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int i, error_code = NO_ERROR;
  JSON_DOC_STORE new_doc;
  JSON_DOC_STORE value_doc;

  db_make_null (result);

  if (num_args < 3 || num_args % 2 == 0)
    {
      assert_release (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (DB_IS_NULL (arg[0]))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*arg[0], true, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  for (i = 1; i < num_args; i += 2)
    {
      if (DB_IS_NULL (arg[i]))
	{
	  return db_make_null (result);
	}

      // extract path
      /* *INDENT-OFF* */
      std::string value_path;
      /* *INDENT-ON* */
      error_code = db_value_to_json_path (*arg[i], F_JSON_SET, value_path);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // extract json value
      error_code = db_value_to_json_value (*arg[i + 1], value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // insert into result the value at requred path
      error_code = db_json_set_func (value_doc.get_immutable (), *new_doc.get_mutable (), value_path.c_str ());
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_keys (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int error_code = NO_ERROR;
  JSON_DOC_STORE new_doc;
  /* *INDENT-OFF* */
  std::string path;
  /* *INDENT-ON* */

  db_make_null (result);

  if (num_args > 2)
    {
      assert_release (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (is_any_arg_null (arg, num_args))
    {
      return NO_ERROR;
    }

  if (num_args == 1)
    {
      path = "";
    }
  else
    {
      error_code = db_value_to_json_path (*arg[1], F_JSON_KEYS, path);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  error_code = db_value_to_json_doc (*arg[0], false, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_DOC_STORE result_json;
  result_json.create_mutable_reference ();
  error_code = db_json_keys_func (*new_doc.get_immutable (), *result_json.get_mutable (), path.c_str ());
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  db_make_json_from_doc_store_and_release (*result, result_json);
  return NO_ERROR;
}

int
db_evaluate_json_remove (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int i, error_code;
  JSON_DOC_STORE new_doc;
  // *INDENT-OFF*
  std::string path;
  // *INDENT-ON*

  db_make_null (result);

  if (num_args < 2)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (is_any_arg_null (arg, num_args))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*arg[0], true, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  for (i = 1; i < num_args; i++)
    {
      error_code = db_value_to_json_path (*arg[i], F_JSON_REMOVE, path);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      error_code = db_json_remove_func (*new_doc.get_mutable (), path.c_str ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_array_append (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int i, error_code = NO_ERROR;
  JSON_DOC_STORE new_doc;
  JSON_DOC_STORE value_doc;

  db_make_null (result);

  if (num_args < 3 || num_args % 2 == 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (DB_IS_NULL (arg[0]))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*arg[0], true, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  for (i = 1; i < num_args; i += 2)
    {
      if (DB_IS_NULL (arg[i]))
	{
	  return db_make_null (result);
	}

      // extract path
      /* *INDENT-OFF* */
      std::string value_path;
      /* *INDENT-ON* */
      error_code = db_value_to_json_path (*arg[i], F_JSON_ARRAY_APPEND, value_path);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // extract json value
      error_code = db_value_to_json_value (*arg[i + 1], value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // insert into result the value at required path
      error_code = db_json_array_append_func (value_doc.get_immutable (), *new_doc.get_mutable (), value_path.c_str ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_array_insert (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int i, error_code = NO_ERROR;
  JSON_DOC_STORE new_doc;
  JSON_DOC_STORE value_doc;

  db_make_null (result);

  if (num_args < 3 || num_args % 2 == 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (DB_IS_NULL (arg[0]))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*arg[0], true, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  for (i = 1; i < num_args; i += 2)
    {
      if (DB_IS_NULL (arg[i]))
	{
	  return db_make_null (result);
	}

      // extract path
      /* *INDENT-OFF* */
      std::string value_path;
      /* *INDENT-ON* */
      error_code = db_value_to_json_path (*arg[i], F_JSON_ARRAY_INSERT, value_path);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // extract json value
      error_code = db_value_to_json_value (*arg[i + 1], value_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      // insert into result the value at required path
      error_code = db_json_array_insert_func (value_doc.get_immutable (), *new_doc.get_mutable (), value_path.c_str ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, new_doc);

  return NO_ERROR;
}

int
db_evaluate_json_contains_path (DB_VALUE * result, DB_VALUE * const *arg, const int num_args)
{
  bool exists = false;
  int error_code = NO_ERROR;
  JSON_DOC_STORE doc;
  /* *INDENT-OFF* */
  std::vector<std::string> paths;
  /* *INDENT-ON* */
  db_make_null (result);

  if (is_any_arg_null (arg, num_args))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*arg[0], false, doc);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  bool find_all;
  error_code = is_str_find_all (arg[1], find_all);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  for (int i = 2; i < num_args; ++i)
    {
      /* *INDENT-OFF* */
      paths.emplace_back ();
      /* *INDENT-ON* */
      error_code = db_value_to_json_path (*arg[i], F_JSON_CONTAINS_PATH, paths.back ());
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  error_code = db_json_contains_path (doc.get_immutable (), paths, find_all, exists);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  db_make_int (result, (int) exists);
  return error_code;
}

/*
 * db_evaluate_json_merge_preserve ()
 *
 * this function accumulate-merges jsons preserving members having duplicate keys
 * so merge (j1, j2, j3, j4) = merge (j1, (merge (j2, merge (j3, j4))))
 *
 * result (out): the merge result
 * arg (in): the arguments for the merge function
 * num_args (in)
 */
int
db_evaluate_json_merge_preserve (DB_VALUE * result, DB_VALUE * const *arg, const int num_args)
{
  int error_code;
  JSON_DOC *accumulator = nullptr;
  JSON_DOC_STORE accumulator_owner;
  JSON_DOC_STORE doc;

  if (num_args < 2)
    {
      db_make_null (result);
      return NO_ERROR;
    }

  if (is_any_arg_null (arg, num_args))
    {
      db_make_null (result);
      return NO_ERROR;
    }

  for (int i = 0; i < num_args; ++i)
    {
      error_code = db_value_to_json_doc (*arg[i], false, doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      error_code = db_json_merge_preserve_func (doc.get_immutable (), accumulator);
      accumulator_owner.set_mutable_reference (accumulator);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, accumulator_owner);

  return NO_ERROR;
}

/*
 * db_evaluate_json_merge_patch ()
 *
 * this function accumulate-merges jsons and patches members having duplicate keys
 * so merge (j1, j2, j3, j4) = merge (j1, (merge (j2, merge (j3, j4))))
 *
 * result (out): the merge result
 * arg (in): the arguments for the merge function
 * num_args (in)
 */
int
db_evaluate_json_merge_patch (DB_VALUE * result, DB_VALUE * const *arg, const int num_args)
{
  int error_code;
  JSON_DOC *accumulator = nullptr;
  JSON_DOC_STORE accumulator_owner;
  JSON_DOC_STORE doc;

  if (num_args < 2)
    {
      db_make_null (result);
      return NO_ERROR;
    }

  if (is_any_arg_null (arg, num_args))
    {
      db_make_null (result);
      return NO_ERROR;
    }

  for (int i = 0; i < num_args; ++i)
    {
      error_code = db_value_to_json_doc (*arg[i], false, doc);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      error_code = db_json_merge_patch_func (doc.get_immutable (), accumulator);
      accumulator_owner.set_mutable_reference (accumulator);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  db_make_json_from_doc_store_and_release (*result, accumulator_owner);

  return NO_ERROR;
}

/* *INDENT-OFF* */

/*
 * JSON_SEARCH (json_doc, one/all, pattern [, escape_char, path_1,... path_n])
 *
 * db_json_search_dbval ()
 * function that finds paths of json_values that match the pattern argument
 * result (out): json string or json array if there are more paths that match
 * args (in): the arguments for the json_search function
 * num_args (in)
 */

int
db_evaluate_json_search (DB_VALUE *result, DB_VALUE * const * args, const int num_args)
{
  int error_code = NO_ERROR;
  JSON_DOC_STORE doc;
  const size_t ESCAPE_CHAR_ARG_INDEX = 3;

  if (num_args < 3)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  for (int i = 0; i < num_args; ++i)
    {
      // only escape char might be null
      if (i != ESCAPE_CHAR_ARG_INDEX && DB_IS_NULL (args[i]))
        {
          return db_make_null (result);
        }
    }

  error_code = db_value_to_json_doc (*args[0], false, doc);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  bool find_all;
  error_code = is_str_find_all (args[1], find_all);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  DB_VALUE *pattern = args[2];
  const DB_VALUE *esc_char = nullptr;
  const char * slash_str = "\\";
  DB_VALUE default_slash_str_dbval;

  if (num_args >= 4)
    {
      esc_char = args[3];
    }
  else
    {
      // No escape char arg provided
      if (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) == false)
      {
	 // This is equivalent to compat_mode=mysql. In this mode '\\' is default escape character for LIKE pattern
	 db_make_string (&default_slash_str_dbval, slash_str);
	 esc_char = &default_slash_str_dbval;
      }
    }

  std::vector<std::string> starting_paths;
  for (int i = 4; i < num_args; ++i)
    {
      starting_paths.emplace_back ();
      error_code = db_value_to_json_path (*args[i], F_JSON_SEARCH, starting_paths.back ());
      if (error_code != NO_ERROR)
        {
          ASSERT_ERROR ();
          return error_code;
        }
    }

  if (starting_paths.empty ())
    {
      starting_paths.push_back ("$");
    }

  std::vector<JSON_PATH> paths;
  error_code = db_json_search_func (*doc.get_immutable (), pattern, esc_char, paths, starting_paths, find_all);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (paths.empty ())
    {
      return db_make_null (result);
    }

  JSON_DOC *result_json = nullptr;
  if (paths.size () == 1)
    {
      std::string path = paths[0].dump_json_path ();
      error_code = db_json_path_unquote_object_keys_external (path);
      if (error_code)
      {
	return error_code;
      }

      char *escaped;
      size_t escaped_size;
      error_code = db_string_escape_str (path.c_str (), path.size (), &escaped, &escaped_size);
      cubmem::private_unique_ptr<char> escaped_unique_ptr (escaped, NULL);
      if (error_code)
	{
	  return error_code;
	}
      error_code = db_json_get_json_from_str (escaped, result_json, escaped_size);
      if (error_code != NO_ERROR)
	{
          ASSERT_ERROR ();
	  return error_code;
	}
      return db_make_json (result, result_json, true);
    }

  JSON_DOC_STORE result_json_owner;
  JSON_DOC_STORE json_array_elem_owner;
  result_json_owner.create_mutable_reference ();
  for (std::size_t i = 0; i < paths.size (); ++i)
    {
      std::string path = paths[i].dump_json_path ();

      error_code = db_json_path_unquote_object_keys_external (path);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      char *escaped;
      size_t escaped_size;
      error_code = db_string_escape_str (path.c_str (), path.size (), &escaped, &escaped_size);
      cubmem::private_unique_ptr<char> escaped_unique_ptr (escaped, NULL);
      if (error_code)
	{
	  return error_code;
	}

      JSON_DOC *json_array_elem = nullptr;
      error_code = db_json_get_json_from_str (escaped, json_array_elem, escaped_size);
      json_array_elem_owner.set_mutable_reference (json_array_elem);
      if (error_code != NO_ERROR)
	{
          ASSERT_ERROR ();
	  return error_code;
	}

      db_json_add_element_to_array (result_json_owner.get_mutable (), json_array_elem_owner.get_immutable ());
    }

  db_make_json_from_doc_store_and_release (*result, result_json_owner);
  return NO_ERROR;
}
/* *INDENT-ON* */

int
db_evaluate_json_get_all_paths (DB_VALUE * result, DB_VALUE * const *arg, int const num_args)
{
  int error_code = NO_ERROR;
  JSON_DOC_STORE new_doc;

  db_make_null (result);

  if (num_args != 1)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_FAILED;
    }

  if (DB_IS_NULL (arg[0]))
    {
      return NO_ERROR;
    }

  error_code = db_value_to_json_doc (*arg[0], false, new_doc);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  JSON_DOC *result_json = db_json_allocate_doc ();
  error_code = db_json_get_all_paths_func (*new_doc.get_immutable (), result_json);

  db_make_json (result, result_json, true);

  return NO_ERROR;
}

int
db_least_or_greatest (DB_VALUE * arg1, DB_VALUE * arg2, DB_VALUE * result, bool least)
{
  int error_code = NO_ERROR;
  bool can_compare = false;
  DB_VALUE_COMPARE_RESULT cmp_result = DB_UNK;

  cmp_result = tp_value_compare_with_error (arg1, arg2, 1, 0, &can_compare);

  if (cmp_result == DB_EQ)
    {
      pr_clone_value (arg1, result);
    }
  else if (cmp_result == DB_GT)
    {
      if (least)
	{
	  pr_clone_value (arg2, result);
	}
      else
	{
	  pr_clone_value (arg1, result);
	}
    }
  else if (cmp_result == DB_LT)
    {
      if (least)
	{
	  pr_clone_value (arg1, result);
	}
      else
	{
	  pr_clone_value (arg2, result);
	}
    }
  else if (cmp_result == DB_UNK && can_compare == false)
    {
      return ER_FAILED;
    }
  else
    {
      assert_release (DB_IS_NULL (arg1) || DB_IS_NULL (arg2));
      db_make_null (result);
      return NO_ERROR;
    }

  return error_code;
}

static int
is_str_find_all (DB_VALUE * val, bool & find_all)
{
  if (DB_IS_NULL (val))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_ONE_ALL_ARGUMENT, 0);
      return ER_INVALID_ONE_ALL_ARGUMENT;
    }

  if (!DB_IS_STRING (val))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_ONE_ALL_ARGUMENT, 0);
      return ER_INVALID_ONE_ALL_ARGUMENT;
    }

  // *INDENT-OFF*
  std::string find_all_str (db_get_string (val), db_get_string_size (val));
  std::transform (find_all_str.begin (), find_all_str.end (), find_all_str.begin (), [] (unsigned char c)
  {
    return std::tolower (c);
  });
  // *INDENT-ON*

  find_all = false;
  if (find_all_str == "all")
    {
      find_all = true;
    }
  if (!find_all && find_all_str != "one")
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_ONE_ALL_ARGUMENT, 0);
      return ER_INVALID_ONE_ALL_ARGUMENT;
    }
  return NO_ERROR;
}

static bool
is_any_arg_null (DB_VALUE * const *args, int num_args)
{
  for (int i = 0; i < num_args; ++i)
    {
      if (DB_IS_NULL (args[i]))
	{
	  return true;
	}
    }
  return false;
}
