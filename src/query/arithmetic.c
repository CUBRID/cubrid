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

/* this must be the last header file included!!! */
#include "dbval.h"

static double round_double (double num, double integer);
static double truncate_double (double num, double integer);
static DB_BIGINT truncate_bigint (DB_BIGINT num, DB_BIGINT integer);

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
    case DB_TYPE_DOUBLE:
      dtmp = floor (DB_GET_DOUBLE (value));
      DB_MAKE_DOUBLE (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      {
	unsigned char numeric_digit[DB_NUMERIC_BUF_SIZE];
	int p, s;

	numeric_coerce_num_to_double (db_locate_numeric (value),
				      DB_VALUE_SCALE (value), &dtmp);
	dtmp = floor (dtmp);
	(void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value),
					       numeric_digit, &p, &s);
	DB_MAKE_NUMERIC (result, numeric_digit, p, s);
	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      dtmp = floor (dtmp);
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type,
				    dtmp);
      break;
    default:
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
    case DB_TYPE_DOUBLE:
      dtmp = ceil (DB_GET_DOUBLE (value));
      DB_MAKE_DOUBLE (result, (double) dtmp);
      break;
    case DB_TYPE_NUMERIC:
      {
	unsigned char num[DB_NUMERIC_BUF_SIZE];
	int p, s;

	numeric_coerce_num_to_double (db_locate_numeric (value),
				      DB_VALUE_SCALE (value), &dtmp);
	dtmp = ceil (dtmp);
	(void) numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value),
					       num, &p, &s);
	DB_MAKE_NUMERIC (result, num, p, s);
	break;
      }
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (value))->amount;
      dtmp = ceil (dtmp);
      DB_MAKE_MONETARY_TYPE_AMOUNT (result, (DB_GET_MONETARY (value))->type,
				    dtmp);
      break;
    default:
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
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_FUNCTION_ARG_ERROR, 1,
	  "sqrt()");
  return ER_FAILED;
}

/*
 * db_power_dbval () - take power value of db_value
 *   return: NO_ERROR, ER_FAILED
 *   result(out) : resultant db_value
 *   domain(in) : resultant db_value domain
 *   value1(in)  : first db_value
 *   value2(in)  : second db_value
 */
int
db_power_dbval (DB_VALUE * result, TP_DOMAIN * domain,
		DB_VALUE * value1, DB_VALUE * value2)
{
  DB_TYPE type1, type2, res_type;
  short s1, s2;
  int i1, i2;
  float f1, f2;
  double d1, d2;
  double dtmp;
  DB_BIGINT bi1, bi2;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int p, s;
  int er_status;

  if (domain->type->id == DB_TYPE_NULL
      || DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  type2 = DB_VALUE_DOMAIN_TYPE (value2);

  switch (type1)
    {
    case DB_TYPE_SHORT:
      s1 = DB_GET_SHORT (value1);

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  dtmp = pow ((double) s1, s2);
	  if (OR_CHECK_SHORT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_SHORT (result, (short) dtmp);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  dtmp = pow ((double) s1, i2);
	  if (OR_CHECK_INT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_INT (result, (int) dtmp);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  dtmp = pow ((double) s1, (double) bi2);
	  if (OR_CHECK_BIGINT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_BIGINT (result, (DB_BIGINT) dtmp);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (s1 < 0 && f2 != ceil (f2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (s1, f2);
	  if (OR_CHECK_FLOAT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_FLOAT (result, (float) dtmp);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (s1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (s1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
	  if (s1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (s1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  er_status =
	    numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2),
					    num, &p, &s);
	  if (er_status != NO_ERROR)
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_NUMERIC (result, num, p, s);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (s1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (s1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					dtmp);
	  break;
	default:
	  break;
	}
      break;

    case DB_TYPE_INTEGER:
      i1 = DB_GET_INT (value1);

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  i2 = (int) DB_GET_SHORT (value2);
	  dtmp = pow ((double) i1, i2);
	  if (OR_CHECK_INT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_INT (result, (int) dtmp);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  dtmp = pow ((double) i1, i2);
	  if (OR_CHECK_INT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_INT (result, (int) dtmp);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  dtmp = pow ((double) i1, (double) bi2);
	  if (OR_CHECK_BIGINT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_BIGINT (result, (DB_BIGINT) dtmp);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (i1 < 0 && f2 != ceil (f2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (i1, f2);
	  if (OR_CHECK_FLOAT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_FLOAT (result, (float) dtmp);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (i1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (i1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
	  if (i1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (i1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  er_status =
	    numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2),
					    num, &p, &s);
	  if (er_status != NO_ERROR)
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_NUMERIC (result, num, p, s);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (i1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (i1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					dtmp);
	  break;
	default:
	  break;
	}
      break;

    case DB_TYPE_BIGINT:
      bi1 = DB_GET_BIGINT (value1);

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  i2 = (int) DB_GET_SHORT (value2);
	  dtmp = pow ((double) bi1, i2);
	  if (OR_CHECK_BIGINT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_BIGINT (result, (DB_BIGINT) dtmp);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  dtmp = pow ((double) bi1, i2);
	  if (OR_CHECK_BIGINT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_BIGINT (result, (DB_BIGINT) dtmp);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  dtmp = pow ((double) bi1, (double) bi2);
	  if (OR_CHECK_BIGINT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_BIGINT (result, (DB_BIGINT) dtmp);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (bi1 < 0 && f2 != ceil (f2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) bi1, f2);
	  if (OR_CHECK_FLOAT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_FLOAT (result, (float) dtmp);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (bi1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) bi1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
	  if (bi1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) bi1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  er_status =
	    numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value2),
					    num, &p, &s);
	  if (er_status != NO_ERROR)
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_NUMERIC (result, num, p, s);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (bi1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) bi1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					dtmp);
	  break;
	default:
	  break;
	}
      break;

    case DB_TYPE_FLOAT:
      f1 = DB_GET_FLOAT (value1);

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  dtmp = pow (f1, s2);
	  if (OR_CHECK_FLOAT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_FLOAT (result, (float) dtmp);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  dtmp = pow (f1, i2);
	  if (OR_CHECK_FLOAT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_FLOAT (result, (float) dtmp);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  dtmp = pow (f1, (double) bi2);
	  if (OR_CHECK_FLOAT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_FLOAT (result, (float) dtmp);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (f1 < 0 && f2 != ceil (f2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (f1, f2);
	  if (OR_CHECK_FLOAT_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_FLOAT (result, (float) dtmp);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (f1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) f1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
	  if (f1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) f1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (f1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) f1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					dtmp);
	  break;
	default:
	  break;
	}
      break;

    case DB_TYPE_DOUBLE:
      d1 = DB_GET_DOUBLE (value1);

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  dtmp = pow (d1, s2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  dtmp = pow (d1, i2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  dtmp = pow (d1, (double) bi2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (d1 < 0 && f2 != ceil (f2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) d1, (double) f2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					dtmp);
	  break;
	default:
	  break;
	}
      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value1),
				    DB_VALUE_SCALE (value1), &d1);

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  dtmp = pow (d1, s2);
	  er_status =
	    numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1),
					    num, &p, &s);
	  if (er_status != NO_ERROR)
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_NUMERIC (result, num, p, s);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  dtmp = pow (d1, i2);
	  er_status =
	    numeric_internal_double_to_num (dtmp, DB_VALUE_SCALE (value1),
					    num, &p, &s);
	  if (er_status != NO_ERROR)
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_NUMERIC (result, num, p, s);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  dtmp = pow (d1, (double) bi2);
	  er_status = numeric_internal_double_to_num (dtmp,
						      DB_VALUE_SCALE (value1),
						      num, &p, &s);
	  if (er_status != NO_ERROR)
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_NUMERIC (result, num, p, s);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (d1 < 0 && f2 != ceil (f2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow ((double) d1, (double) f2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_DOUBLE (result, dtmp);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  er_status = numeric_internal_double_to_num (dtmp,
						      DB_VALUE_SCALE (value1)
						      +
						      DB_VALUE_SCALE (value2),
						      num, &p, &s);
	  if (er_status != NO_ERROR)
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_NUMERIC (result, num, p, s);
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value2))->type,
					dtmp);
	  break;
	default:
	  break;
	}
      break;

    case DB_TYPE_MONETARY:
      d1 = (DB_GET_MONETARY (value1))->amount;

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  dtmp = pow (d1, s2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value1))->type,
					dtmp);
	  break;
	case DB_TYPE_INTEGER:
	  i2 = DB_GET_INT (value2);
	  dtmp = pow (d1, i2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value1))->type,
					dtmp);
	  break;
	case DB_TYPE_BIGINT:
	  bi2 = DB_GET_BIGINT (value2);
	  dtmp = pow (d1, (double) bi2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value1))->type,
					dtmp);
	  break;
	case DB_TYPE_FLOAT:
	  f2 = DB_GET_FLOAT (value2);
	  if (d1 < 0 && f2 != ceil (f2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, (double) f2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value1))->type,
					dtmp);
	  break;
	case DB_TYPE_DOUBLE:
	  d2 = DB_GET_DOUBLE (value2);
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value1))->type,
					dtmp);
	  break;
	case DB_TYPE_NUMERIC:
	  numeric_coerce_num_to_double (db_locate_numeric (value2),
					DB_VALUE_SCALE (value2), &d2);
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value1))->type,
					dtmp);
	  break;
	case DB_TYPE_MONETARY:
	  /*
	   * NOTE: we probably should return an error if the two monetaries
	   * have different montetary types.
	   */
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d1 < 0 && d2 != ceil (d2))
	    {
	      goto pow_error;
	    }
	  dtmp = pow (d1, d2);
	  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
	    {
	      goto pow_overflow;
	    }
	  DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					(DB_GET_MONETARY (value1))->type,
					dtmp);
	  break;
	default:
	  break;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  /*
   * if control reaches here, 'result' has the result of 'dbval1 op dbval2'.
   * if 'result' is not in the given domain, coerce 'result' into domain.
   */

  /* save result's type as tp_value_coerce loses it if the coercion fails */
  res_type = PRIM_TYPE (result);
  if (tp_value_coerce (result, result, domain) != DOMAIN_COMPATIBLE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
	      pr_type_name (res_type), pr_type_name (domain->type->id));
      return ER_FAILED;
    }
  return NO_ERROR;

pow_overflow:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_POWER, 0);
  return ER_FAILED;

pow_error:
  /* need new error code */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_POWER_ERROR, 0);
  return ER_FAILED;
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
  DB_TYPE type1, type2;
  short s1, s2;
  int i1, i2;
  float f1, f2;
  double d1, d2;
  DB_BIGINT bi1, bi2;
  double dtmp;
  DB_DATA_STATUS data_stat;
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int p, s;

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
	  bi2 = DB_GET_INT (value2);
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
	      (void) numeric_db_value_coerce_to_num (value1, result,
						     &data_stat);
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
					    (DB_GET_MONETARY (value2))->type,
					    s1);
	    }
	  else
	    {
	      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					    (DB_GET_MONETARY (value2))->type,
					    (double) fmod (s1, d2));
	    }
	  break;
	default:
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
                  0);
          return ER_FAILED;
	}
      break;

    case DB_TYPE_INTEGER:
      i1 = DB_GET_INT (value1);

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
	      (void) numeric_db_value_coerce_to_num (value1, result,
						     &data_stat);
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
					    (DB_GET_MONETARY (value2))->type,
					    i1);
	    }
	  else
	    {
	      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					    (DB_GET_MONETARY (value2))->type,
					    (double) fmod (i1, d2));
	    }
	  break;
	default:
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
                  0);
          return ER_FAILED;
	}
      break;

    case DB_TYPE_BIGINT:
      bi1 = DB_GET_BIGINT (value1);

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
	      (void) numeric_db_value_coerce_to_num (value1, result,
						     &data_stat);
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
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
                  0);
          return ER_FAILED;
	}
      break;

    case DB_TYPE_FLOAT:
      f1 = DB_GET_FLOAT (value1);

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
					    (DB_GET_MONETARY (value2))->type,
					    f1);
	    }
	  else
	    {
	      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					    (DB_GET_MONETARY (value2))->type,
					    (double) fmod ((double) f1, d2));
	    }
	  break;
	default:
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
                  0);
          return ER_FAILED;
	}
      break;

    case DB_TYPE_DOUBLE:
      d1 = DB_GET_DOUBLE (value1);

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
					    (DB_GET_MONETARY (value2))->type,
					    d1);
	    }
	  else
	    {
	      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					    (DB_GET_MONETARY (value2))->type,
					    (double) fmod (d1, d2));
	    }
	  break;
	default:
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
                  0);
          return ER_FAILED;
	}
      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (value1),
				    DB_VALUE_SCALE (value1), &d1);

      switch (type2)
	{
	case DB_TYPE_SHORT:
	  s2 = DB_GET_SHORT (value2);
	  if (s2 == 0)
	    {
	      (void) numeric_db_value_coerce_to_num (value1, result,
						     &data_stat);
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
	      (void) numeric_db_value_coerce_to_num (value1, result,
						     &data_stat);
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
	      (void) numeric_db_value_coerce_to_num (value1, result,
						     &data_stat);
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
	      (void) numeric_db_value_coerce_to_num (value1, result,
						     &data_stat);
	    }
	  else
	    {
	      dtmp = fmod (d1, d2);
	      (void) numeric_internal_double_to_num (dtmp,
						     MAX (DB_VALUE_SCALE
							  (value1),
							  DB_VALUE_SCALE
							  (value2)), num, &p,
						     &s);
	      DB_MAKE_NUMERIC (result, num, p, s);
	    }
	  break;
	case DB_TYPE_MONETARY:
	  d2 = (DB_GET_MONETARY (value2))->amount;
	  if (d2 == 0)
	    {
	      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					    (DB_GET_MONETARY (value2))->type,
					    d1);
	    }
	  else
	    {
	      DB_MAKE_MONETARY_TYPE_AMOUNT (result,
					    (DB_GET_MONETARY (value2))->type,
					    (double) fmod (d1, d2));
	    }
	  break;
	default:
          er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
                  0);
          return ER_FAILED;
	}
      break;

    case DB_TYPE_MONETARY:
      d1 = (DB_GET_MONETARY (value1))->amount;
      d2 = 0;

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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
	  return ER_FAILED;
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

      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  return NO_ERROR;
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
      return ER_FAILED;
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
      bi2 = (type2 == DB_TYPE_BIGINT) ?
	DB_GET_BIGINT (value2) : DB_GET_INT (value2);
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
	    *ptr++ = '0';
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  if (er_errid () < 0)
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
 * db_trunc_dbval () - return dbval1 truncated to dbval2 decimal places
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

  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (value1);
  type2 = DB_VALUE_DOMAIN_TYPE (value2);
  if (type2 != DB_TYPE_SHORT && type2 != DB_TYPE_INTEGER
      && type2 != DB_TYPE_BIGINT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  if (type2 == DB_TYPE_INTEGER)
    {
      bi2 = DB_GET_INT (value2);
    }
  else if (type2 == DB_TYPE_BIGINT)
    {
      bi2 = DB_GET_BIGINT (value2);
    }
  else
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
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
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
