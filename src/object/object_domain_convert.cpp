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
 * object_domain_convert.cpp - the value converters: one function per pair of a value's type and a target
 *                             domain's type, and the switch that finds a pair's converter
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include "object_domain.h"
#include "object_domain_convert.h"
#include "db_date_status.h"
#include <utility>
#include "object_primitive.h"
#include "object_representation.h"
#include "numeric_opfunc.h"
#include "tz_support.h"
#include "db_date.h"
#include "mprec.h"
#include "porting_inline.hpp"
#include "set_object.h"
#include "string_opfunc.h"
#include "chartype.h"
#include "db_json.hpp"
#include "string_buffer.hpp"
#include "db_value_printer.hpp"

#if !defined (SERVER_MODE)
#include "work_space.h"
#include "virtual_object.h"
#include "schema_manager.h"
#include "locator_cl.h"
#include "object_template.h"
#include "dbi.h"
#endif /* !defined (SERVER_MODE) */

#include "dbtype.h"
#include "error_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define DBL_MAX_DIGITS    ((int)ceil(DBL_MAX_EXP * log10((double) FLT_RADIX)))
#define DB_DATETIMETZ_INITIALIZER { {0, 0}, 0 }
#define ROUND(x)		  ((x) > 0 ? ((x) + .5) : ((x) - .5))
#define SECONDS_IN_A_DAY	  (long)(86400)	/* 24L * 60L * 60L */

/* Numeric converters share the legacy formulas; source, destination and mode are compile-time parameters. */
template <DB_TYPE TYPE> struct tp_numeric_value;

template <> struct tp_numeric_value<DB_TYPE_SHORT>
{
  static auto get (const DB_VALUE *value)
  {
    return db_get_short (value);
  }
  static void make (DB_VALUE *value, short number)
  {
    db_make_short (value, number);
  }
};
template <> struct tp_numeric_value<DB_TYPE_INTEGER>
{
  static auto get (const DB_VALUE *value)
  {
    return db_get_int (value);
  }
  static void make (DB_VALUE *value, int number)
  {
    db_make_int (value, number);
  }
};
template <> struct tp_numeric_value<DB_TYPE_BIGINT>
{
  static auto get (const DB_VALUE *value)
  {
    return db_get_bigint (value);
  }
  static void make (DB_VALUE *value, DB_BIGINT number)
  {
    db_make_bigint (value, number);
  }
};
template <> struct tp_numeric_value<DB_TYPE_FLOAT>
{
  static auto get (const DB_VALUE *value)
  {
    return db_get_float (value);
  }
  static void make (DB_VALUE *value, float number)
  {
    db_make_float (value, number);
  }
};
template <> struct tp_numeric_value<DB_TYPE_DOUBLE>
{
  static auto get (const DB_VALUE *value)
  {
    return db_get_double (value);
  }
  static void make (DB_VALUE *value, double number)
  {
    db_make_double (value, number);
  }
};
template <> struct tp_numeric_value<DB_TYPE_MONETARY>
{
  static auto get (const DB_VALUE *value)
  {
    return db_get_monetary (value)->amount;
  }
  static void make (DB_VALUE *value, double number)
  {
    db_make_monetary (value, DB_CURRENCY_DEFAULT, number);
  }
};

template <DB_TYPE DST, typename T>
static bool
tp_numeric_overflow (T number)
{
  if constexpr (DST == DB_TYPE_SHORT)
    {
      return OR_CHECK_SHORT_OVERFLOW (number);
    }
  else if constexpr (DST == DB_TYPE_INTEGER)
    {
      return OR_CHECK_INT_OVERFLOW (number);
    }
  else if constexpr (DST == DB_TYPE_BIGINT)
    {
      return OR_CHECK_BIGINT_OVERFLOW (number);
    }
  else if constexpr (DST == DB_TYPE_FLOAT)
    {
      return OR_CHECK_FLOAT_OVERFLOW (number);
    }
  else
    {
      static_assert (DST == DB_TYPE_DOUBLE || DST == DB_TYPE_MONETARY, "missing numeric range check");
      return false;
    }
}

template <DB_TYPE DST, bool STRICT>
static int
tp_numeric_from_num (const DB_VALUE *src, DB_VALUE *target)
{
  int scale = db_get_numeric_scale (src, NULL);
  if constexpr (DST == DB_TYPE_SHORT)
    {
      if constexpr (STRICT)
	{
	  return numeric_coerce_num_to_short_strict (src, scale, target);
	}
      else
	{
	  return numeric_coerce_num_to_short (src, scale, target);
	}
    }
  else if constexpr (DST == DB_TYPE_INTEGER)
    {
      if constexpr (STRICT)
	{
	  return numeric_coerce_num_to_int_strict (src, scale, target);
	}
      else
	{
	  return numeric_coerce_num_to_int (src, scale, target);
	}
    }
  else if constexpr (DST == DB_TYPE_BIGINT)
    {
      if constexpr (STRICT)
	{
	  return numeric_coerce_num_to_bigint_strict (src, scale, target);
	}
      else
	{
	  return numeric_coerce_num_to_bigint (src, scale, target);
	}
    }
  else if constexpr (DST == DB_TYPE_FLOAT)
    {
      if constexpr (STRICT)
	{
	  return numeric_coerce_num_to_float_strict (src, scale, target);
	}
      else
	{
	  return numeric_coerce_num_to_float (src, scale, target);
	}
    }
  else if constexpr (DST == DB_TYPE_DOUBLE)
    {
      if constexpr (STRICT)
	{
	  return numeric_coerce_num_to_double_strict (src, scale, target);
	}
      else
	{
	  return numeric_coerce_num_to_double (src, scale, target);
	}
    }
  else
    {
      static_assert (DST == DB_TYPE_MONETARY, "missing numeric target operation");
      if constexpr (STRICT)
	{
	  return numeric_coerce_num_to_monetary_strict (src, scale, target);
	}
      else
	{
	  return numeric_coerce_num_to_monetary (src, scale, target);
	}
    }
}

template <DB_TYPE SRC, DB_TYPE DST, DOMAIN_CONVERT_MODE MODE>
TP_DOMAIN_STATUS
tp_value_convert_number (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			 date_conversion_error *date_error)
{
  static_assert (MODE == DOMAIN_CONVERT_ASSIGN || MODE == DOMAIN_CONVERT_COMPARE || MODE == DOMAIN_CONVERT_OPERAND,
		 "missing numeric conversion mode");
  static_assert (SRC == DB_TYPE_SHORT || SRC == DB_TYPE_INTEGER || SRC == DB_TYPE_BIGINT || SRC == DB_TYPE_FLOAT
		 || SRC == DB_TYPE_DOUBLE || SRC == DB_TYPE_MONETARY || SRC == DB_TYPE_NUMERIC
		 || SRC == DB_TYPE_CHAR || SRC == DB_TYPE_VARCHAR, "missing numeric source cell");
  static_assert (DST == DB_TYPE_SHORT || DST == DB_TYPE_INTEGER || DST == DB_TYPE_BIGINT || DST == DB_TYPE_FLOAT
		 || DST == DB_TYPE_DOUBLE || DST == DB_TYPE_MONETARY || DST == DB_TYPE_NUMERIC,
		 "missing numeric destination cell");
  constexpr bool integer_target = DST == DB_TYPE_SHORT || DST == DB_TYPE_INTEGER || DST == DB_TYPE_BIGINT;
  constexpr bool integer_source = SRC == DB_TYPE_SHORT || SRC == DB_TYPE_INTEGER || SRC == DB_TYPE_BIGINT;
  constexpr bool string_source = SRC == DB_TYPE_CHAR || SRC == DB_TYPE_VARCHAR;
  constexpr bool strict = MODE != DOMAIN_CONVERT_ASSIGN;

  if constexpr (DST == DB_TYPE_NUMERIC)
    {
      if constexpr (string_source)
	{
	  DB_VALUE number;
	  const char *string = db_get_string (src);
	  if (string == NULL)
	    {
	      return DOMAIN_INCOMPATIBLE;
	    }
	  int error = numeric_coerce_string_to_num_status (string, db_get_string_size (src),
		      db_get_string_codeset (src), &number);
	  if (error != NO_ERROR)
	    {
	      return error == ER_IT_DATA_OVERFLOW ? DOMAIN_OVERFLOW : DOMAIN_INCOMPATIBLE;
	    }
	  /* The legacy strict string path also rounds when fitting the parsed NUMERIC to the target. */
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_NUMERIC, DOMAIN_CONVERT_ASSIGN>
		 (&number, target, desired_domain, date_error);
	}
      else if constexpr (MODE == DOMAIN_CONVERT_COMPARE
			 && (SRC == DB_TYPE_FLOAT || SRC == DB_TYPE_DOUBLE || SRC == DB_TYPE_MONETARY))
	{
	  return DOMAIN_INCOMPATIBLE;
	}
      else
	{
	  if constexpr (SRC == DB_TYPE_NUMERIC && MODE != DOMAIN_CONVERT_COMPARE)
	    {
	      if (desired_domain->precision == DB_VALUE_PRECISION (src)
		  && desired_domain->scale == DB_VALUE_SCALE (src))
		{
		  /* NUMERIC owns its inline buffer; no generic value/type dispatch is necessary. */
		  *target = *src;
		  return DOMAIN_COMPATIBLE;
		}
	    }
	  DB_DATA_STATUS data_stat = DATA_STATUS_OK;
	  int error = numeric_coerce_value_to_num<SRC> (src, target, &data_stat);
	  if (error == ER_IT_DATA_OVERFLOW || data_stat == DATA_STATUS_TRUNCATED)
	    {
	      return DOMAIN_OVERFLOW;
	    }
	  return error == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
	}
    }
  else if constexpr (SRC == DB_TYPE_NUMERIC)
    {
      int error = tp_numeric_from_num<DST, strict> (src, target);
      return error == NO_ERROR ? DOMAIN_COMPATIBLE : (strict ? DOMAIN_INCOMPATIBLE : DOMAIN_OVERFLOW);
    }
  else if constexpr (string_source)
    {
      DB_DATA_STATUS data_stat = DATA_STATUS_OK;
      if constexpr (DST == DB_TYPE_BIGINT && !strict)
	{
	  DB_BIGINT number = 0;
	  if (tp_atobi (src, &number, &data_stat) != NO_ERROR)
	    {
	      return er_errid () != NO_ERROR ? DOMAIN_ERROR : DOMAIN_INCOMPATIBLE;
	    }
	  if (data_stat == DATA_STATUS_TRUNCATED)
	    {
	      return DOMAIN_OVERFLOW;
	    }
	  db_make_bigint (target, number);
	  return DOMAIN_COMPATIBLE;
	}
      else
	{
	  double number = 0.0;
	  if (tp_atof (src, &number, &data_stat) != NO_ERROR || data_stat == DATA_STATUS_NOT_CONSUMED)
	    {
	      if constexpr (strict)
		{
		  return DOMAIN_INCOMPATIBLE;
		}
	      else
		{
		  return er_errid () != NO_ERROR ? DOMAIN_ERROR : DOMAIN_INCOMPATIBLE;
		}
	    }
	  if (data_stat == DATA_STATUS_TRUNCATED || tp_numeric_overflow<DST> (number))
	    {
	      return DOMAIN_OVERFLOW;
	    }
	  if constexpr (integer_target)
	    {
	      if constexpr (strict)
		{
		  double integral = 0.0;
		  if (modf (number, &integral) != 0)
		    {
		      return DOMAIN_INCOMPATIBLE;
		    }
		  tp_numeric_value<DST>::make (target, integral);
		}
	      else
		{
		  tp_numeric_value<DST>::make (target, ROUND (number));
		}
	    }
	  else
	    {
	      tp_numeric_value<DST>::make (target, number);
	    }
	  return DOMAIN_COMPATIBLE;
	}
    }
  else
    {
      const auto number = tp_numeric_value<SRC>::get (src);
      if constexpr (SRC == DST)
	{
	  /* Includes the currency field of a MONETARY identity. */
	  *target = *src;
	  return DOMAIN_COMPATIBLE;
	}
      else if constexpr (integer_target)
	{
	  if (tp_numeric_overflow<DST> (number))
	    {
	      return DOMAIN_OVERFLOW;
	    }
	  if constexpr (integer_source)
	    {
	      tp_numeric_value<DST>::make (target, number);
	    }
	  else if constexpr (strict)
	    {
	      auto integral = number;
	      if constexpr (SRC == DB_TYPE_FLOAT)
		{
		  if (modff (number, &integral) != 0)
		    {
		      return DOMAIN_INCOMPATIBLE;
		    }
		}
	      else
		{
		  if (modf (number, &integral) != 0)
		    {
		      return DOMAIN_INCOMPATIBLE;
		    }
		}
	      tp_numeric_value<DST>::make (target, integral);
	    }
	  else
	    {
	      using target_type = decltype (tp_numeric_value<DST>::get (target));
	      target_type integral = static_cast<target_type> (ROUND (number));
	      /* Preserve the legacy assignment overflow checks, including AIX's saturating casts. */
	      if constexpr (DST == DB_TYPE_BIGINT || (DST == DB_TYPE_INTEGER && SRC == DB_TYPE_FLOAT))
		{
#if defined (AIX)
		  if constexpr (SRC == DB_TYPE_FLOAT || SRC == DB_TYPE_DOUBLE)
		    {
		      if constexpr (DST == DB_TYPE_BIGINT)
			{
			  if (number == static_cast<decltype (number)> (DB_BIGINT_MAX))
			    {
			      integral = DB_BIGINT_MIN;
			    }
			}
		      else
			{
			  if (number == static_cast<decltype (number)> (DB_INT32_MAX))
			    {
			      integral = DB_INT32_MIN;
			    }
			}
		    }
#endif
		  if (OR_CHECK_ASSIGN_OVERFLOW (integral, number))
		    {
		      return DOMAIN_OVERFLOW;
		    }
		}
	      tp_numeric_value<DST>::make (target, integral);
	    }
	}
      else if constexpr (DST == DB_TYPE_FLOAT && (SRC == DB_TYPE_DOUBLE || SRC == DB_TYPE_MONETARY))
	{
	  /* These two source types are absent from the legacy strict FLOAT switch. */
	  if constexpr (strict)
	    {
	      return DOMAIN_INCOMPATIBLE;
	    }
	  else
	    {
	      if (tp_numeric_overflow<DST> (number))
		{
		  return DOMAIN_OVERFLOW;
		}
	      tp_numeric_value<DST>::make (target, number);
	    }
	}
      else
	{
	  tp_numeric_value<DST>::make (target, number);
	}
      return DOMAIN_COMPATIBLE;
    }
}

static TP_DOMAIN_STATUS
tp_value_convert_blob_to_varchar (const DB_VALUE *, DB_VALUE *, const TP_DOMAIN *, date_conversion_error *)
{
  return DOMAIN_INCOMPATIBLE;
}

static int
tp_atotime_core (const DB_VALUE *src, DB_TIME *temp, date_conversion_error *error)
{
  int milisec;
  const char *strp = db_get_string (src);
  int str_len = db_get_string_size (src);
  int status = NO_ERROR;

  if (db_date_parse_time_core (strp, str_len, temp, &milisec, error) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

static int
tp_atodate_core (const DB_VALUE *src, DB_DATE *temp, date_conversion_error *error)
{
  const char *strp = db_get_string (src);
  int str_len = db_get_string_size (src);
  int status = NO_ERROR;

  if (db_date_parse_date_core (strp, str_len, temp, error) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

static int
tp_atoutime_core (const DB_VALUE *src, DB_UTIME *temp, date_conversion_error *error)
{
  const char *strp = db_get_string (src);
  int str_len = db_get_string_size (src);
  int status = NO_ERROR;

  if (db_date_parse_timestamp_core (strp, str_len, temp, error) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

static int
tp_atotimestamptz_core (const DB_VALUE *src, DB_TIMESTAMPTZ *temp, date_conversion_error *error)
{
  const char *strp = db_get_string (src);
  int str_len = db_get_string_size (src);
  int status = NO_ERROR;
  bool dummy_has_zone;

  if (db_string_to_timestamptz_ex_core (strp, str_len, temp, &dummy_has_zone, true, error) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

static int
tp_atoudatetime_core (const DB_VALUE *src, DB_DATETIME *temp, date_conversion_error *error)
{
  const char *strp = db_get_string (src);
  int str_len = db_get_string_size (src);
  int status = NO_ERROR;

  if (db_date_parse_datetime_core (strp, str_len, temp, error) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

static int
tp_atodatetimetz_core (const DB_VALUE *src, DB_DATETIMETZ *temp, date_conversion_error *error)
{
  const char *strp = db_get_string (src);
  int str_len = db_get_string_size (src);
  int status = NO_ERROR;
  bool dummy_has_zone;

  if (db_string_to_datetimetz_ex_core (strp, str_len, temp, &dummy_has_zone, error) != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

static void
tp_make_char_conversion (const TP_DOMAIN *desired_domain, const char *new_string, DB_VALUE *target,
			 TP_DOMAIN_STATUS *status, DB_DATA_STATUS *data_stat)
{
  DB_VALUE temp;

  assert (desired_domain->collation_flag == TP_DOMAIN_COLL_NORMAL
	  || desired_domain->collation_flag == TP_DOMAIN_COLL_LEAVE);

  *status = DOMAIN_COMPATIBLE;

  db_make_char (&temp, desired_domain->precision, new_string, strlen (new_string),
		TP_DOMAIN_CODESET (desired_domain), TP_DOMAIN_COLLATION (desired_domain));

  temp.need_clear = true;
  if (db_char_string_coerce (&temp, target, data_stat) != NO_ERROR)
    {
      *status = DOMAIN_INCOMPATIBLE;
    }
  else
    {
      *status = DOMAIN_COMPATIBLE;
    }
  pr_clear_value (&temp);
}

static void
tp_make_varchar_conversion (const TP_DOMAIN *desired_domain, const char *new_string, DB_VALUE *target,
			    TP_DOMAIN_STATUS *status, DB_DATA_STATUS *data_stat)
{
  DB_VALUE temp;

  assert (desired_domain->collation_flag == TP_DOMAIN_COLL_NORMAL
	  || desired_domain->collation_flag == TP_DOMAIN_COLL_LEAVE);

  *status = DOMAIN_COMPATIBLE;

  db_make_varchar (&temp, desired_domain->precision, new_string, strlen (new_string),
		   TP_DOMAIN_CODESET (desired_domain), TP_DOMAIN_COLLATION (desired_domain));

  temp.need_clear = true;
  if (db_char_string_coerce (&temp, target, data_stat) != NO_ERROR)
    {
      *status = DOMAIN_INCOMPATIBLE;
    }
  else
    {
      *status = DOMAIN_COMPATIBLE;
    }
  pr_clear_value (&temp);
}

char *
tp_ftoa_buffer (const DB_VALUE *src, DB_VALUE *result)
{
  /* dtoa() appears to ignore the requested number of digits... */
  const int ndigits = TP_FLOAT_MANTISA_DECIMAL_PRECISION;
  char *str_float, *rve;
  int decpt, sign;

  assert (DB_VALUE_TYPE (src) == DB_TYPE_FLOAT);
  assert (DB_VALUE_TYPE (result) == DB_TYPE_NULL);

  rve = str_float = (char *) db_private_alloc (NULL, TP_FLOAT_AS_CHAR_LENGTH + 1);
  if (str_float == NULL)
    {
      db_make_null (result);
      return nullptr;
    }

  /* _dtoa just returns the digits sequence and the exponent as for a number in the form 0.4321344e+14 */
  _dtoa (db_get_float (src), 0, ndigits, &decpt, &sign, &rve, str_float, 1);

  /* rounding should also be performed here */
  str_float[ndigits] = '\0';	/* _dtoa() disregards ndigits */

  format_floating_point (str_float, str_float + strlen (str_float), ndigits, decpt, sign);

  return str_float;
}

void
tp_ftoa_char (const DB_VALUE *src, DB_VALUE *result)
{
  char *str_float = tp_ftoa_buffer (src, result);
  if (str_float == nullptr)
    {
      return;
    }

  db_make_char (result, DB_VALUE_PRECISION (result), str_float, strlen (str_float), db_get_string_codeset (result),
		db_get_string_collation (result));
  result->need_clear = true;
}

void
tp_ftoa_varchar (const DB_VALUE *src, DB_VALUE *result)
{
  char *str_float = tp_ftoa_buffer (src, result);
  if (str_float == nullptr)
    {
      return;
    }

  db_make_varchar (result, DB_VALUE_PRECISION (result), str_float, strlen (str_float),
		   db_get_string_codeset (result), db_get_string_collation (result));
  result->need_clear = true;
}

char *
tp_dtoa_buffer (const DB_VALUE *src, DB_VALUE *result)
{
  /* dtoa() appears to ignore the requested number of digits... */
  const int ndigits = TP_DOUBLE_MANTISA_DECIMAL_PRECISION;
  char *str_double, *rve;
  int decpt, sign;

  assert (DB_VALUE_TYPE (src) == DB_TYPE_DOUBLE);
  assert (DB_VALUE_TYPE (result) == DB_TYPE_NULL);

  rve = str_double = (char *) db_private_alloc (NULL, TP_DOUBLE_AS_CHAR_LENGTH + 1);
  if (str_double == NULL)
    {
      db_make_null (result);
      return nullptr;
    }

  _dtoa (db_get_double (src), 0, ndigits, &decpt, &sign, &rve, str_double, 0);
  /* rounding should also be performed here */
  str_double[ndigits] = '\0';	/* _dtoa() disregards ndigits */

  format_floating_point (str_double, str_double + strlen (str_double), ndigits, decpt, sign);

  return str_double;
}

void
tp_dtoa_char (const DB_VALUE *src, DB_VALUE *result)
{
  char *str_double = tp_dtoa_buffer (src, result);
  if (str_double == nullptr)
    {
      return;
    }

  db_make_char (result, DB_VALUE_PRECISION (result), str_double, strlen (str_double),
		db_get_string_codeset (result), db_get_string_collation (result));
  result->need_clear = true;
}

void
tp_dtoa_varchar (const DB_VALUE *src, DB_VALUE *result)
{
  char *str_double = tp_dtoa_buffer (src, result);
  if (str_double == nullptr)
    {
      return;
    }

  db_make_varchar (result, DB_VALUE_PRECISION (result), str_double, strlen (str_double),
		   db_get_string_codeset (result), db_get_string_collation (result));
  result->need_clear = true;
}

static int
tp_make_date_conversion (DB_VALUE *value, int month, int day, int year, date_conversion_error *error)
{
  value->domain.general_info.type = DB_TYPE_DATE;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_date_encode_core (&value->data.date, month, day, year, error);
}

static int
tp_make_time_conversion (DB_VALUE *value, int hour, int minute, int second, date_conversion_error *error)
{
  value->domain.general_info.type = DB_TYPE_TIME;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_time_encode_core (&value->data.time, hour, minute, second, error);
}

static TP_DOMAIN_STATUS
tp_finish_enumeration_conversion (DB_VALUE *target, const TP_DOMAIN *desired_domain, DB_VALUE &conv_val,
				  unsigned short val_idx, const char *val_str, int val_str_size,
				  TP_DOMAIN_STATUS status, bool exit)
{
  bool alloc_string = true, ti = true;
  bool ignore_trailing_space = tp_conversion_ignore_trailing_space ();

  if (exit)
    {
      return status;
    }

  if (status == DOMAIN_COMPATIBLE)
    {
      if (val_str != NULL)
	{
	  /* We have to search through the elements of the desired domain to find the index for val_str. */
	  int i, size;
	  DB_ENUM_ELEMENT *db_enum = NULL;
	  int elem_count = DOM_GET_ENUM_ELEMS_COUNT (desired_domain);

	  for (i = 1; i <= elem_count; i++)
	    {
	      db_enum = &DOM_GET_ENUM_ELEM (desired_domain, i);
	      size = DB_GET_ENUM_ELEM_STRING_SIZE (db_enum);

	      if (!ignore_trailing_space)
		{
		  ti = false;
		}

	      /* use collation from the PT_TYPE_ENUMERATION */
	      if (QSTR_COMPARE (desired_domain->collation_id, (const unsigned char *) val_str, val_str_size,
				(const unsigned char *) DB_GET_ENUM_ELEM_STRING (db_enum), size, ti) == 0)
		{
		  break;
		}
	    }

	  val_idx = i;
	  if (i > elem_count)
	    {
	      if (val_str[0] == 0)
		{
		  /* The source value is string with length 0 and can be matched with enum "special error value"
		   * if it's not a valid ENUM value */
		  db_make_enumeration (target, 0, NULL, 0, TP_DOMAIN_CODESET (desired_domain),
				       TP_DOMAIN_COLLATION (desired_domain));
		  return status;
		}
	      else
		{
		  status = DOMAIN_INCOMPATIBLE;
		}
	    }
	}
      else
	{
	  /* We have the index, we need to get the actual string value from the desired domain */
	  if (val_idx > DOM_GET_ENUM_ELEMS_COUNT (desired_domain))
	    {
	      status = DOMAIN_INCOMPATIBLE;
	    }
	  else if (val_idx == 0)
	    {
	      /* ENUM Special error value */
	      db_make_enumeration (target, 0, NULL, 0, TP_DOMAIN_CODESET (desired_domain),
				   TP_DOMAIN_COLLATION (desired_domain));
	      return status;
	    }
	  else
	    {
	      val_str_size = DB_GET_ENUM_ELEM_STRING_SIZE (&DOM_GET_ENUM_ELEM (desired_domain, val_idx));
	      val_str = DB_GET_ENUM_ELEM_STRING (&DOM_GET_ENUM_ELEM (desired_domain, val_idx));
	    }
	}

      if (status == DOMAIN_COMPATIBLE)
	{
	  const char *enum_str;

	  assert (val_str != NULL);

	  if (!DB_IS_NULL (&conv_val))
	    {
	      /* if charset conversion, than use the converted value buffer to avoid an additional copy */
	      alloc_string = false;
	      conv_val.need_clear = false;
	    }

	  if (alloc_string)
	    {
	      char *enum_str_tmp = (char *) db_private_alloc (NULL, val_str_size + 1);
	      if (enum_str_tmp == NULL)
		{
		  status = DOMAIN_ERROR;
		  pr_clear_value (&conv_val);
		  return status;
		}
	      else
		{
		  memcpy (enum_str_tmp, val_str, val_str_size);
		  enum_str_tmp[val_str_size] = 0;
		}

	      enum_str = enum_str_tmp;
	    }
	  else
	    {
	      enum_str = val_str;
	    }

	  db_make_enumeration (target, val_idx, enum_str, val_str_size, TP_DOMAIN_CODESET (desired_domain),
			       TP_DOMAIN_COLLATION (desired_domain));
	  target->need_clear = true;
	}
    }
  pr_clear_value (&conv_val);

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_numeric (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;
  int result = numeric_coerce_value_to_num < DB_TYPE_ENUMERATION > (src, target, &data_stat);
  if (result == ER_IT_DATA_OVERFLOW || data_stat == DATA_STATUS_TRUNCATED)
    {
      return DOMAIN_OVERFLOW;
    }
  return result == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_short (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *,
				       date_conversion_error *)
{
  db_make_short (target, db_get_enum_short (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_integer (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *,
    date_conversion_error *)
{
  db_make_int (target, db_get_enum_short (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_bigint (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *,
					date_conversion_error *)
{
  db_make_bigint (target, db_get_enum_short (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_float (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *,
				       date_conversion_error *)
{
  db_make_float (target, (float) db_get_enum_short (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_double (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *,
					date_conversion_error *)
{
  db_make_double (target, (double) db_get_enum_short (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_monetary (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *,
    date_conversion_error *)
{
  db_make_monetary (target, DB_CURRENCY_DEFAULT, db_get_enum_short (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_SHORT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestamp (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestamp (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestamp (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_FLOAT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestamp (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestamp (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_monetary_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_MONETARY, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestamp (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_numeric_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_NUMERIC, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestamp (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;

  if (tp_atoutime_core (src, &v_utime, error) != NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }
  else
    {
      db_make_timestamp (target, v_utime);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_TIME v_time;
  DB_DATE v_date;

  {
    {
      v_date = *db_get_date (src);
      db_time_encode_core (&v_time, 0, 0, 0, error);
    }

    if (db_timestamp_encode_ses_core (&v_date, &v_time, &v_utime, NULL, error) == NO_ERROR)
      {
	db_make_timestamp (target, v_utime);
      }
    else
      {
	status = DOMAIN_OVERFLOW;
      }
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestampltz_to_timestamp (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  /* copy timestamp (UTC) */
  db_make_timestamp (target, *db_get_timestamp (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_timestamp (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;

  v_timestamptz = *db_get_timestamptz (src);
  /* copy timestamp (UTC) */
  db_make_timestamp (target, v_timestamptz.timestamp);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_timestamp (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  {
    {
      v_datetime = *db_get_datetime (src);
      v_date = v_datetime.date;
      v_time = v_datetime.time / 1000;
    }

    if (db_timestamp_encode_ses_core (&v_date, &v_time, &v_utime, NULL, error) == NO_ERROR)
      {
	db_make_timestamp (target, v_utime);
      }
    else
      {
	status = DOMAIN_OVERFLOW;
      }
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_timestamp (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  v_datetime = *db_get_datetime (src);
  v_date = v_datetime.date;
  v_time = v_datetime.time / 1000;

  if (db_timestamp_encode_utc_core (&v_date, &v_time, &v_utime, error) == NO_ERROR)
    {
      db_make_timestamp (target, v_utime);
    }
  else
    {
      status = DOMAIN_OVERFLOW;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_timestamp (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATETIMETZ v_datetimetz;
  DB_TIME v_time;
  DB_DATE v_date;

  v_datetimetz = *db_get_datetimetz (src);
  v_date = v_datetimetz.datetime.date;
  v_time = v_datetimetz.datetime.time / 1000;

  if (db_timestamp_encode_utc_core (&v_date, &v_time, &v_utime, error) == NO_ERROR)
    {
      db_make_timestamp (target, v_utime);
    }
  else
    {
      status = DOMAIN_OVERFLOW;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_timestamp (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_timestamp (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  status =
	  tp_value_convert_number < DB_TYPE_SHORT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;

      tmpint = db_get_int (target);
      if (tmpint < 0)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}
      v_timestamptz.timestamp = (DB_UTIME) tmpint;

      if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
	  NO_ERROR)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}

      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  status =
	  tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;

      tmpint = db_get_int (target);
      if (tmpint < 0)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}
      v_timestamptz.timestamp = (DB_UTIME) tmpint;

      if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
	  NO_ERROR)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}

      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  status =
	  tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;

      tmpint = db_get_int (target);
      if (tmpint < 0)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}
      v_timestamptz.timestamp = (DB_UTIME) tmpint;

      if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
	  NO_ERROR)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}

      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  status =
	  tp_value_convert_number < DB_TYPE_FLOAT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;

      tmpint = db_get_int (target);
      if (tmpint < 0)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}
      v_timestamptz.timestamp = (DB_UTIME) tmpint;

      if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
	  NO_ERROR)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}

      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  status =
	  tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;

      tmpint = db_get_int (target);
      if (tmpint < 0)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}
      v_timestamptz.timestamp = (DB_UTIME) tmpint;

      if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
	  NO_ERROR)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}

      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_monetary_to_timestamptz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  status =
	  tp_value_convert_number < DB_TYPE_MONETARY, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;

      tmpint = db_get_int (target);
      if (tmpint < 0)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}
      v_timestamptz.timestamp = (DB_UTIME) tmpint;

      if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
	  NO_ERROR)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}

      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_numeric_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  status =
	  tp_value_convert_number < DB_TYPE_NUMERIC, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;

      tmpint = db_get_int (target);
      if (tmpint < 0)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}
      v_timestamptz.timestamp = (DB_UTIME) tmpint;

      if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
	  NO_ERROR)
	{
	  status = DOMAIN_INCOMPATIBLE;
	  return status;
	}

      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;

  if (tp_atotimestamptz_core (src, &v_timestamptz, error) != NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }
  else
    {
      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_timestamptz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;
  DB_TIME v_time;
  DB_DATE v_date;

  /* convert from session to UTC */
  {
    assert (DB_TYPE_DATE == DB_TYPE_DATE);
    v_date = *db_get_date (src);
    v_time = 0;
  }

  if (db_timestamp_encode_ses_core (&v_date, &v_time, &v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
      NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }
  else
    {
      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_timestamptz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;

  /* copy TS value and create TZ_ID for system TZ */
  v_timestamptz.timestamp = *db_get_timestamp (src);

  if (tz_create_session_tzid_for_timestamp_core (&v_timestamptz.timestamp, & (v_timestamptz.tz_id), error) !=
      NO_ERROR)
    {
      return DOMAIN_ERROR;
    }

  db_make_timestamptz (target, &v_timestamptz);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_timestamptz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  /* convert from session to UTC */
  {
    v_datetime = *db_get_datetime (src);
    v_date = v_datetime.date;
    v_time = v_datetime.time / 1000;
  }

  if (db_timestamp_encode_ses_core (&v_date, &v_time, &v_timestamptz.timestamp, &v_timestamptz.tz_id, error) !=
      NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }
  else
    {
      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_timestamptz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  v_datetime = *db_get_datetime (src);
  v_date = v_datetime.date;
  v_time = v_datetime.time / 1000;

  /* encode DT as UTC and the TZ of session */
  if (db_timestamp_encode_utc_core (&v_date, &v_time, &v_timestamptz.timestamp, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  if (tz_create_session_tzid_for_datetime_core (&v_datetime, true, & (v_timestamptz.tz_id), error) != NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }
  else
    {
      db_make_timestamptz (target, &v_timestamptz);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_timestamptz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATETIMETZ v_datetimetz;
  DB_TIME v_time;
  DB_DATE v_date;

  v_datetimetz = *db_get_datetimetz (src);
  v_date = v_datetimetz.datetime.date;
  v_time = v_datetimetz.datetime.time / 1000;

  /* encode TS to DT (UTC) and copy TZ from DT_TZ */
  if (db_timestamp_encode_utc_core (&v_date, &v_time, &v_timestamptz.timestamp, error) == NO_ERROR)
    {
      v_timestamptz.tz_id = v_datetimetz.tz_id;
      db_make_timestamptz (target, &v_timestamptz);
    }
  else
    {
      status = DOMAIN_OVERFLOW;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_timestamptz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_timestamptz (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_timestampltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_SHORT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestampltz (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestampltz (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_timestampltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestampltz (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_timestampltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_FLOAT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestampltz (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_timestampltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestampltz (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_monetary_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_MONETARY, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestampltz (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_numeric_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  status =
	  tp_value_convert_number < DB_TYPE_NUMERIC, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		  src, target,
		  &tp_Integer_domain, error)
	  ;
  if (status == DOMAIN_COMPATIBLE)
    {
      int tmpint;
      tmpint = db_get_int (target);
      if (tmpint >= 0)
	{
	  db_make_timestampltz (target, (DB_UTIME) tmpint);
	}
      else
	{
	  status = DOMAIN_INCOMPATIBLE;
	}
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_timestampltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;

  /* read as DATETIMETZ */
  if (tp_atotimestamptz_core (src, &v_timestamptz, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  else
    {
      db_make_timestampltz (target, v_timestamptz.timestamp);
    }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_timestampltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_TIME v_time;
  DB_DATE v_date;

  {
    {
      assert (DB_TYPE_DATE == DB_TYPE_DATE);
      v_date = *db_get_date (src);
      v_time = 0;
    }

    if (db_timestamp_encode_ses_core (&v_date, &v_time, &v_utime, NULL, error) != NO_ERROR)
      {
	status = DOMAIN_OVERFLOW;
	return status;
      }

    db_make_timestampltz (target, v_utime);
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  /* original value stored in UTC, copy it */
  db_make_timestampltz (target, *db_get_timestamp (src));

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;

  v_timestamptz = *db_get_timestamptz (src);
  /* original value stored in UTC, copy it */
  db_make_timestampltz (target, v_timestamptz.timestamp);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  {
    {
      v_datetime = *db_get_datetime (src);
      v_date = v_datetime.date;
      v_time = v_datetime.time / 1000;
    }

    if (db_timestamp_encode_ses_core (&v_date, &v_time, &v_utime, NULL, error) != NO_ERROR)
      {
	status = DOMAIN_OVERFLOW;
	return status;
      }

    db_make_timestampltz (target, v_utime);
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  {
    v_datetime = *db_get_datetime (src);
    v_date = v_datetime.date;
    v_time = v_datetime.time / 1000;
  }

  /* both values are in UTC */
  if (db_timestamp_encode_utc_core (&v_date, &v_time, &v_utime, error) == NO_ERROR)
    {
      db_make_timestampltz (target, v_utime);
    }
  else
    {
      status = DOMAIN_OVERFLOW;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATETIMETZ v_datetimetz;
  DB_TIME v_time;
  DB_DATE v_date;

  {
    assert (DB_TYPE_DATETIMETZ == DB_TYPE_DATETIMETZ);
    v_datetimetz = *db_get_datetimetz (src);
    v_date = v_datetimetz.datetime.date;
    v_time = v_datetimetz.datetime.time / 1000;
  }

  /* both values are in UTC */
  if (db_timestamp_encode_utc_core (&v_date, &v_time, &v_utime, error) == NO_ERROR)
    {
      db_make_timestampltz (target, v_utime);
    }
  else
    {
      status = DOMAIN_OVERFLOW;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_timestampltz (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_datetime (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATETIME v_datetime;

  if (tp_atoudatetime_core (src, &v_datetime, error) != NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }
  else
    {
      db_make_datetime (target, &v_datetime);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_datetime (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  DB_DATETIME v_datetime;

  v_datetime.date = *db_get_date (src);
  v_datetime.time = 0;
  db_make_datetime (target, &v_datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_datetime (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  DB_UTIME v_utime;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  v_utime = *db_get_timestamp (src);
  if (db_timestamp_decode_ses_core (&v_utime, &v_date, &v_time, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  v_datetime.date = v_date;
  v_datetime.time = v_time * 1000;
  db_make_datetime (target, &v_datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_datetime (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  v_timestamptz = *db_get_timestamptz (src);
  if (db_timestamp_decode_w_tz_id_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, &v_date, &v_time, error)
      != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  v_datetime.date = v_date;
  v_datetime.time = v_time * 1000;
  db_make_datetime (target, &v_datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_datetime (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_DATETIME v_datetime;

  {
    DB_DATETIME utc_dt;

    /* DATETIMELTZ store in UTC, DATETIME in session TZ */
    utc_dt = *db_get_datetime (src);
    if (tz_datetimeltz_to_local_core (&utc_dt, &v_datetime, error) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    db_make_datetime (target, &v_datetime);
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_datetime (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATETIME v_datetime;
  DB_DATETIMETZ v_datetimetz;

  /* DATETIMETZ store in UTC, DATETIME in session TZ */
  v_datetimetz = *db_get_datetimetz (src);
  if (tz_utc_datetimetz_to_local_core (&v_datetimetz.datetime, &v_datetimetz.tz_id, &v_datetime, error) ==
      NO_ERROR)
    {
      db_make_datetime (target, &v_datetime);
    }
  else
    {
      status = DOMAIN_ERROR;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_datetime (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_datetime (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATETIMETZ v_datetimetz;

  if (tp_atodatetimetz_core (src, &v_datetimetz, error) != NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }
  else
    {
      db_make_datetimeltz (target, &v_datetimetz.datetime);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_DATETIME v_datetime;
  DB_DATETIMETZ v_datetimetz;

  {
    v_datetime.date = *db_get_date (src);
    v_datetime.time = 0;
  }

  if (tz_create_datetimetz_from_ses_core (&v_datetime, &v_datetimetz, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_make_datetimeltz (target, &v_datetimetz.datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_UTIME v_utime;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  v_utime = *db_get_timestamp (src);

  (void) db_timestamp_decode_utc (&v_utime, &v_date, &v_time);
  v_datetime.time = v_time * 1000;
  v_datetime.date = v_date;
  db_make_datetimeltz (target, &v_datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATETIME v_datetime;
  DB_TIME v_time;
  DB_DATE v_date;

  v_timestamptz = *db_get_timestamptz (src);
  (void) db_timestamp_decode_utc (&v_timestamptz.timestamp, &v_date, &v_time);
  v_datetime.time = v_time * 1000;
  v_datetime.date = v_date;
  db_make_datetimeltz (target, &v_datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_DATETIME v_datetime;
  DB_DATETIMETZ v_datetimetz;

  {
    v_datetime = *db_get_datetime (src);
  }

  if (tz_create_datetimetz_from_ses_core (&v_datetime, &v_datetimetz, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_make_datetimeltz (target, &v_datetimetz.datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_DATETIMETZ v_datetimetz;

  /* copy (UTC) */
  v_datetimetz = *db_get_datetimetz (src);
  db_make_datetimeltz (target, &v_datetimetz.datetime);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_datetimeltz (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_datetimetz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  DB_DATETIMETZ v_datetimetz;

  {
    if (tp_atodatetimetz_core (src, &v_datetimetz, error) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    db_make_datetimetz (target, &v_datetimetz);
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_datetimetz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  DB_DATETIME v_datetime;
  DB_DATETIMETZ v_datetimetz;

  v_datetime.date = *db_get_date (src);
  v_datetime.time = 0;

  if (tz_create_datetimetz_from_ses_core (&v_datetime, &v_datetimetz, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_make_datetimetz (target, &v_datetimetz);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_datetimetz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_UTIME v_utime;
  DB_DATETIMETZ v_datetimetz;
  DB_TIME v_time;
  DB_DATE v_date;

  {
    v_utime = *db_get_timestamp (src);
    db_timestamp_decode_utc (&v_utime, &v_date, &v_time);
    v_datetimetz.datetime.time = v_time * 1000;
    v_datetimetz.datetime.date = v_date;

    if (tz_create_session_tzid_for_datetime_core (&v_datetimetz.datetime, true, &v_datetimetz.tz_id, error) !=
	NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    db_make_datetimetz (target, &v_datetimetz);
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_datetimetz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATETIMETZ v_datetimetz;
  DB_TIME v_time;
  DB_DATE v_date;

  v_timestamptz = *db_get_timestamptz (src);
  (void) db_timestamp_decode_utc (&v_timestamptz.timestamp, &v_date, &v_time);
  v_datetimetz.datetime.time = v_time * 1000;
  v_datetimetz.datetime.date = v_date;
  v_datetimetz.tz_id = v_timestamptz.tz_id;
  db_make_datetimetz (target, &v_datetimetz);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_datetimetz (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  DB_DATETIMETZ v_datetimetz;

  if (tz_create_datetimetz_from_ses_core (db_get_datetime (src), &v_datetimetz, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_make_datetimetz (target, &v_datetimetz);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_datetimetz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_DATETIMETZ v_datetimetz;

  v_datetimetz.datetime = *db_get_datetime (src);
  if (tz_create_session_tzid_for_datetime_core (&v_datetimetz.datetime, true, &v_datetimetz.tz_id, error) !=
      NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_make_datetimetz (target, &v_datetimetz);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_datetimetz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_datetimetz (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATE v_date;
  int year;
  int month;
  int day;

  if (tp_atodate_core (src, &v_date, error) == NO_ERROR)
    {
      db_date_decode (&v_date, &month, &day, &year);
    }
  else
    {
      return DOMAIN_ERROR;
    }

  if (tp_make_date_conversion (target, month, day, year, error) != NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  DB_DATE v_date;
  int year;
  int month;
  int day;

  (void) db_timestamp_decode_ses_core (db_get_timestamp (src), &v_date, NULL, error);
  db_date_decode (&v_date, &month, &day, &year);
  tp_make_date_conversion (target, month, day, year, error);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATE v_date;
  int year;
  int month;
  int day;

  v_timestamptz = *db_get_timestamptz (src);
  if (db_timestamp_decode_w_tz_id_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, &v_date, NULL, error) !=
      NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_date_decode (&v_date, &month, &day, &year);
  tp_make_date_conversion (target, month, day, year, error);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  int hour;
  int minute;
  int second;
  int millisecond;
  int year;
  int month;
  int day;

  db_datetime_decode ((DB_DATETIME *) db_get_datetime (src), &month, &day, &year, &hour, &minute, &second,
		      &millisecond);
  tp_make_date_conversion (target, month, day, year, error);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_DATETIME v_datetime;
  int hour;
  int minute;
  int second;
  int millisecond;
  int year;
  int month;
  int day;

  {
    DB_DATETIME *utc_dt_p;
    TZ_ID tz_id;

    /* DATETIMELTZ and DATETIMETZ store in UTC, convert to session */
    {
      utc_dt_p = db_get_datetime (src);
      if (tz_create_session_tzid_for_datetime_core (utc_dt_p, true, &tz_id, error) != NO_ERROR)
	{
	  return DOMAIN_ERROR;
	}
    }

    if (tz_utc_datetimetz_to_local_core (utc_dt_p, &tz_id, &v_datetime, error) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    db_datetime_decode (&v_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

    tp_make_date_conversion (target, month, day, year, error);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  DB_DATETIME v_datetime;
  int hour;
  int minute;
  int second;
  int millisecond;
  int year;
  int month;
  int day;

  {
    DB_DATETIME *utc_dt_p;
    DB_DATETIMETZ *dt_tz_p;
    TZ_ID tz_id;

    /* DATETIMELTZ and DATETIMETZ store in UTC, convert to session */
    {
      dt_tz_p = db_get_datetimetz (src);
      utc_dt_p = &dt_tz_p->datetime;
      tz_id = dt_tz_p->tz_id;
    }

    if (tz_utc_datetimetz_to_local_core (utc_dt_p, &tz_id, &v_datetime, error) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    db_datetime_decode (&v_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

    tp_make_date_conversion (target, month, day, year, error);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_date (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  DB_TIME v_time;
  int hour;
  int minute;
  int second;

  v_time = db_get_short (src) % SECONDS_IN_A_DAY;
  db_time_decode (&v_time, &hour, &minute, &second);
  tp_make_time_conversion (target, hour, minute, second, error);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  DB_TIME v_time;
  int hour;
  int minute;
  int second;

  v_time = db_get_int (src) % SECONDS_IN_A_DAY;
  db_time_decode (&v_time, &hour, &minute, &second);
  tp_make_time_conversion (target, hour, minute, second, error);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  DB_TIME v_time;
  int hour;
  int minute;
  int second;

  v_time = db_get_bigint (src) % SECONDS_IN_A_DAY;
  db_time_decode (&v_time, &hour, &minute, &second);
  tp_make_time_conversion (target, hour, minute, second, error);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIME v_time;
  int hour;
  int minute;
  int second;

  {
    float ftmp = db_get_float (src);
    if (OR_CHECK_INT_OVERFLOW (ftmp))
      {
	status = DOMAIN_OVERFLOW;
      }
    else
      {
	v_time = ((int) ROUND (ftmp)) % SECONDS_IN_A_DAY;
	db_time_decode (&v_time, &hour, &minute, &second);
	tp_make_time_conversion (target, hour, minute, second, error);
      }
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIME v_time;
  int hour;
  int minute;
  int second;

  {
    double dtmp = db_get_double (src);
    if (OR_CHECK_INT_OVERFLOW (dtmp))
      {
	status = DOMAIN_OVERFLOW;
      }
    else
      {
	v_time = ((int) ROUND (dtmp)) % SECONDS_IN_A_DAY;
	db_time_decode (&v_time, &hour, &minute, &second);
	tp_make_time_conversion (target, hour, minute, second, error);
      }
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_monetary_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  const DB_MONETARY *v_money;
  DB_TIME v_time;
  int hour;
  int minute;
  int second;

  v_money = db_get_monetary (src);
  if (OR_CHECK_INT_OVERFLOW (v_money->amount))
    {
      status = DOMAIN_OVERFLOW;
    }
  else
    {
      v_time = (int) ROUND (v_money->amount) % SECONDS_IN_A_DAY;
      db_time_decode (&v_time, &hour, &minute, &second);
      tp_make_time_conversion (target, hour, minute, second, error);
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIME v_time;
  int hour;
  int minute;
  int second;

  if (tp_atotime_core (src, &v_time, error) == NO_ERROR)
    {
      db_time_decode (&v_time, &hour, &minute, &second);
    }
  else
    {
      return DOMAIN_ERROR;
    }

  if (tp_make_time_conversion (target, hour, minute, second, error) != NO_ERROR)
    {
      status = DOMAIN_ERROR;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  DB_TIME v_time;

  if (db_timestamp_decode_ses_core (db_get_timestamp (src), NULL, &v_time, error) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_value_put_encoded_time (target, &v_time);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_TIMESTAMPTZ v_timestamptz;
  DB_TIME v_time;

  /* convert TS from UTC to value TZ */
  v_timestamptz = *db_get_timestamptz (src);
  if (db_timestamp_decode_w_tz_id_core (&v_timestamptz.timestamp, &v_timestamptz.tz_id, NULL, &v_time, error) !=
      NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  db_value_put_encoded_time (target, &v_time);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  int hour;
  int minute;
  int second;
  int millisecond;
  int year;
  int month;
  int day;

  db_datetime_decode ((DB_DATETIME *) db_get_datetime (src), &month, &day, &year, &hour, &minute, &second,
		      &millisecond);
  tp_make_time_conversion (target, hour, minute, second, error);

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_DATETIME v_datetime;
  int hour;
  int minute;
  int second;
  int millisecond;
  int year;
  int month;
  int day;

  {
    DB_DATETIME dt_local;

    v_datetime = *db_get_datetime (src);

    if (tz_datetimeltz_to_local_core (&v_datetime, &dt_local, error) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    db_datetime_decode (&dt_local, &month, &day, &year, &hour, &minute, &second, &millisecond);
    tp_make_time_conversion (target, hour, minute, second, error);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  DB_DATETIMETZ v_datetimetz;
  int hour;
  int minute;
  int second;
  int millisecond;
  int year;
  int month;
  int day;

  {
    DB_DATETIME dt_local;

    v_datetimetz = *db_get_datetimetz (src);
    if (tz_utc_datetimetz_to_local_core (&v_datetimetz.datetime, &v_datetimetz.tz_id, &dt_local, error) !=
	NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    db_datetime_decode (&dt_local, &month, &day, &year, &hour, &minute, &second, &millisecond);
    tp_make_time_conversion (target, hour, minute, second, error);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_time (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

#if !defined (SERVER_MODE)
static TP_DOMAIN_STATUS
tp_value_convert_object_to_object (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_OBJECT *v_obj = NULL;
    int is_vclass = 0;

    /* Make sure the domains are compatible.  Coerce view objects to real objects. */

    if (!sm_coerce_object_domain ((TP_DOMAIN *) desired_domain, db_get_object (src), &v_obj))
      {
	status = DOMAIN_INCOMPATIBLE;
      }

    {
      /* check we got an object in a proper class */
      if (v_obj && desired_domain->class_mop)
	{
	  DB_OBJECT *obj_class;

	  obj_class = db_get_class (v_obj);
	  if (obj_class == desired_domain->class_mop)
	    {
	      /* everything is fine */
	    }
	  else if (db_is_subclass (obj_class, desired_domain->class_mop) > 0)
	    {
	      /* everything is also ok */
	    }
	  else
	    {
	      is_vclass = db_is_vclass (desired_domain->class_mop);
	      if (is_vclass < 0)
		{
		  return DOMAIN_ERROR;
		}
	      if (is_vclass)
		{
		  /*
		   * This should still be an error, and the above
		   * code should have constructed a virtual mop.
		   * I'm not sure the rest of the code is consistent
		   * in this regard.
		   */
		}
	      else
		{
		  status = DOMAIN_INCOMPATIBLE;
		}
	    }
	}
      db_make_object (target, v_obj);
    }
  }

  return status;
}
#endif /* !defined (SERVER_MODE) */

#if !defined (SERVER_MODE)
static TP_DOMAIN_STATUS
tp_value_convert_oid_to_object (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_OBJECT *v_obj = NULL;
    int is_vclass = 0;

    /* Make sure the domains are compatible.  Coerce view objects to real objects. */

    vid_oid_to_object (src, &v_obj);

    {
      /* check we got an object in a proper class */
      if (v_obj && desired_domain->class_mop)
	{
	  DB_OBJECT *obj_class;

	  obj_class = db_get_class (v_obj);
	  if (obj_class == desired_domain->class_mop)
	    {
	      /* everything is fine */
	    }
	  else if (db_is_subclass (obj_class, desired_domain->class_mop) > 0)
	    {
	      /* everything is also ok */
	    }
	  else
	    {
	      is_vclass = db_is_vclass (desired_domain->class_mop);
	      if (is_vclass < 0)
		{
		  return DOMAIN_ERROR;
		}
	      if (is_vclass)
		{
		  /*
		   * This should still be an error, and the above
		   * code should have constructed a virtual mop.
		   * I'm not sure the rest of the code is consistent
		   * in this regard.
		   */
		}
	      else
		{
		  status = DOMAIN_INCOMPATIBLE;
		}
	    }
	}
      db_make_object (target, v_obj);
    }
  }

  return status;
}
#endif /* !defined (SERVER_MODE) */

#if !defined (SERVER_MODE)
static TP_DOMAIN_STATUS
tp_value_convert_pointer_to_object (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    /* Make sure the domains are compatible.  Coerce view objects to real objects. */
    if (!sm_check_class_domain ((TP_DOMAIN *) desired_domain, ((DB_OTMPL *) db_get_pointer (src))->classobj))
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	db_make_pointer (target, db_get_pointer (src));
      }

  }

  return status;
}
#endif /* !defined (SERVER_MODE) */

#if !defined (SERVER_MODE)
static TP_DOMAIN_STATUS
tp_value_convert_vobj_to_object (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_OBJECT *v_obj = NULL;
    int is_vclass = 0;

    /* Make sure the domains are compatible.  Coerce view objects to real objects. */
    vid_vobj_to_object (src, &v_obj);
    is_vclass = db_is_vclass (desired_domain->class_mop);
    if (is_vclass < 0)
      {
	status = DOMAIN_ERROR;
      }
    else if (!is_vclass)
      {
	v_obj = db_real_instance (v_obj);
      }
    {
      /* check we got an object in a proper class */
      if (v_obj && desired_domain->class_mop)
	{
	  DB_OBJECT *obj_class;

	  obj_class = db_get_class (v_obj);
	  if (obj_class == desired_domain->class_mop)
	    {
	      /* everything is fine */
	    }
	  else if (db_is_subclass (obj_class, desired_domain->class_mop) > 0)
	    {
	      /* everything is also ok */
	    }
	  else
	    {
	      is_vclass = db_is_vclass (desired_domain->class_mop);
	      if (is_vclass < 0)
		{
		  return DOMAIN_ERROR;
		}
	      if (is_vclass)
		{
		  /*
		   * This should still be an error, and the above
		   * code should have constructed a virtual mop.
		   * I'm not sure the rest of the code is consistent
		   * in this regard.
		   */
		}
	      else
		{
		  status = DOMAIN_INCOMPATIBLE;
		}
	    }
	}
      db_make_object (target, v_obj);
    }
  }

  return status;
}
#endif /* !defined (SERVER_MODE) */

#if !defined (SERVER_MODE)
static TP_DOMAIN_STATUS
tp_value_convert_object_to_vobj (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    if (vid_object_to_vobj (db_get_object (src), target) < 0)
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	status = DOMAIN_COMPATIBLE;
      }
    return status;
  }

  return status;
}
#endif /* !defined (SERVER_MODE) */

static TP_DOMAIN_STATUS
tp_value_convert_oid_to_vobj (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE view_oid;
    DB_VALUE class_oid;
    DB_VALUE keys;
    OID nulloid;
    DB_SEQ *seq;

    OID_SET_NULL (&nulloid);
    db_make_oid (&class_oid, &nulloid);
    db_make_oid (&view_oid, &nulloid);
    seq = db_seq_create (NULL, NULL, 3);
    keys = *src;

    /*
     * if we are on the server, and get a DB_TYPE_OBJECT,
     * then its only possible representation is a DB_TYPE_OID,
     * and it may be treated that way. However, this should
     * not really be a case that can happen. It may still
     * for historical reasons, so is not falgged as an error.
     * On the client, a worskapce based scheme must be used,
     * which is just above in a conditional compiled section.
     */

    if ((db_seq_put (seq, 0, &view_oid) != NO_ERROR) || (db_seq_put (seq, 1, &class_oid) != NO_ERROR)
	|| (db_seq_put (seq, 2, &keys) != NO_ERROR))
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	db_make_sequence (target, seq);
	db_value_alter_type (target, DB_TYPE_VOBJ);
	status = DOMAIN_COMPATIBLE;
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_vobj_to_vobj (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    /*
     * We should try and convert the view of the src to match
     * the view of the desired_domain. However, the desired
     * domain generally does not contain this information.
     * We will detect domain incompatibly later on assignment,
     * so we treat casting any DB_TYPE_VOBJ to DB_TYPE_VOBJ
     * as success.
     */
    status = DOMAIN_COMPATIBLE;
    {
      pr_clone_value ((DB_VALUE *) src, target);
    }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_bit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    DB_VALUE temp;
    char *bit_char_string;
    int src_size = db_get_string_size (src);
    int dst_size = (src_size + 1) / 2;

    bit_char_string = (char *) db_private_alloc (NULL, dst_size + 1);
    if (bit_char_string)
      {
	if (qstr_hex_to_bin (bit_char_string, dst_size, db_get_string (src), src_size) != src_size)
	  {
	    status = DOMAIN_ERROR;
	    db_private_free_and_init (NULL, bit_char_string);
	  }
	else
	  {
	    db_make_bit (&temp, TP_FLOATING_PRECISION_VALUE, bit_char_string, src_size * 4);
	    temp.need_clear = true;
	    if (db_bit_string_coerce (&temp, target, &data_stat) != NO_ERROR)
	      {
		status = DOMAIN_INCOMPATIBLE;
	      }
	    else if (data_stat == DATA_STATUS_TRUNCATED)
	      {
		status = DOMAIN_TRUNCATED;
	      }
	    else
	      {
		status = DOMAIN_COMPATIBLE;
	      }
	    pr_clear_value (&temp);
	  }
      }
    else
      {
	/* Couldn't allocate space for bit_char_string */
	status = DOMAIN_INCOMPATIBLE;
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_bit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;

    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_bit (&varchar_val, target, desired_domain, error);
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bit_to_bit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  /* develop coerces a bit string of the same precision too: a value can hold more bits than its precision (a DBLink
   * BIT(n) value holds whole bytes, dblink_scan.c), which the coercion truncates */

  if (db_bit_string_coerce (src, target, &data_stat) != NO_ERROR)
    {
      status = DOMAIN_INCOMPATIBLE;
    }
  else if (data_stat == DATA_STATUS_TRUNCATED)
    {
      status = DOMAIN_TRUNCATED;
    }
  else
    {
      status = DOMAIN_COMPATIBLE;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_varbit_to_bit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  if (db_bit_string_coerce (src, target, &data_stat) != NO_ERROR)
    {
      status = DOMAIN_INCOMPATIBLE;
    }
  else if (data_stat == DATA_STATUS_TRUNCATED)
    {
      status = DOMAIN_TRUNCATED;
    }
  else
    {
      status = DOMAIN_COMPATIBLE;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_blob_to_bit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			      date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE tmpval;

    db_make_null (&tmpval);

    err = db_blob_to_bit (src, NULL, &tmpval);
    if (err == NO_ERROR)
      {
	TP_DOMAIN_STATUS nested_status =
		tp_value_convert_varbit_to_bit (&tmpval, target, desired_domain, error);
	if (nested_status == DOMAIN_COMPATIBLE || nested_status == DOMAIN_TRUNCATED)
	  {
	    status = nested_status;
	  }
	else
	  {
	    /* The old outer LOB cast returned success with a NULL result. */
	    pr_clear_value (target);
	    db_make_null (target);
	  }
      }
    (void) pr_clear_value (&tmpval);
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_varbit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;

    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_bit (&varchar_val, target, desired_domain, error);
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_blob_to_varbit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE tmpval;

    db_make_null (&tmpval);

    err = db_blob_to_bit (src, NULL, &tmpval);
    if (err == NO_ERROR)
      {
	TP_DOMAIN_STATUS nested_status =
		tp_value_convert_bit_to_bit (&tmpval, target, desired_domain, error);
	if (nested_status == DOMAIN_COMPATIBLE || nested_status == DOMAIN_TRUNCATED)
	  {
	    status = nested_status;
	  }
	else
	  {
	    /* The old outer LOB cast returned success with a NULL result. */
	    pr_clear_value (target);
	    db_make_null (target);
	  }
      }
    (void) pr_clear_value (&tmpval);
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = TP_BIGINT_PRECISION + 2 + 1;
    char *new_string;
    DB_BIGINT num;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    {
      num = (DB_BIGINT) db_get_short (src);
    }

    if (tp_ltoa (num, new_string, 10))
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && db_value_precision (target) < (int) strlen (new_string))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = TP_BIGINT_PRECISION + 2 + 1;
    char *new_string;
    DB_BIGINT num;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    {
      num = (DB_BIGINT) db_get_int (src);
    }

    if (tp_ltoa (num, new_string, 10))
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && db_value_precision (target) < (int) strlen (new_string))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = TP_BIGINT_PRECISION + 2 + 1;
    char *new_string;
    DB_BIGINT num;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    {
      num = db_get_bigint (src);
    }

    if (tp_ltoa (num, new_string, 10))
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && db_value_precision (target) < (int) strlen (new_string))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    {
      tp_ftoa_varchar (src, target);
    }

    if (DB_IS_NULL (target))
      {
	if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	  {
	    /* no way to report "out of memory" from tp_value_cast_internal() ?? */
	    status = DOMAIN_ERROR;
	  }
	else
	  {
	    status = DOMAIN_INCOMPATIBLE;
	  }
      }
    else if (DB_VALUE_PRECISION (target) != TP_FLOATING_PRECISION_VALUE
	     && (db_get_string_length (target) > DB_VALUE_PRECISION (target)))
      {
	status = DOMAIN_OVERFLOW;
	pr_clear_value (target);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    {
      tp_dtoa_varchar (src, target);
    }

    if (DB_IS_NULL (target))
      {
	if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	  {
	    /* no way to report "out of memory" from tp_value_cast_internal() ?? */
	    status = DOMAIN_ERROR;
	  }
	else
	  {
	    status = DOMAIN_INCOMPATIBLE;
	  }
      }
    else if (DB_VALUE_PRECISION (target) != TP_FLOATING_PRECISION_VALUE
	     && (db_get_string_length (target) > DB_VALUE_PRECISION (target)))
      {
	status = DOMAIN_OVERFLOW;
	pr_clear_value (target);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_monetary_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    /* monetary symbol = 3 sign = 1 dot = 1 fraction digits = 2 NUL terminator = 1 */
    int max_size = DBL_MAX_DIGITS + 3 + 1 + 1 + 2 + 1;
    char *new_string;
    char *p;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    snprintf (new_string, max_size - 1, "%s%.*f", lang_currency_symbol (db_get_monetary (src)->type), 2,
	      db_get_monetary (src)->amount);
    new_string[max_size - 1] = '\0';

    p = new_string + strlen (new_string);
    for (--p; p >= new_string && *p == '0'; p--)
      {
	/* remove trailing zeros */
	*p = '\0';
      }
    if (*p == '.')	/* remove point */
      {
	*p = '\0';
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_numeric_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    char str_buf[NUMERIC_MAX_STRING_SIZE];
    char *new_string;
    int max_size;

    numeric_db_value_print (src, str_buf);

    max_size = strlen (str_buf) + 1;
    new_string = (char *) db_private_alloc (NULL, max_size);
    if (new_string == NULL)
      {
	return DOMAIN_ERROR;
      }

    strcpy (new_string, str_buf);

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < max_size - 1)
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  if (db_char_string_coerce (src, target, &data_stat) != NO_ERROR)
    {
      status = DOMAIN_INCOMPATIBLE;
    }
  else if (data_stat == DATA_STATUS_TRUNCATED)
    {
      status = DOMAIN_TRUNCATED;
    }
  else if (desired_domain->collation_flag != TP_DOMAIN_COLL_LEAVE)
    {
      db_string_put_cs_and_collation (target, TP_DOMAIN_CODESET (desired_domain),
				      TP_DOMAIN_COLLATION (desired_domain));
      status = DOMAIN_COMPATIBLE;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_varchar_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  if (DB_VALUE_PRECISION (src) == desired_domain->precision
      && (desired_domain->collation_flag == TP_DOMAIN_COLL_LEAVE
	  || db_get_string_codeset (src) == TP_DOMAIN_CODESET (desired_domain)))
    {
      pr_clone_value (src, target);
      if (desired_domain->collation_flag != TP_DOMAIN_COLL_LEAVE)
	db_string_put_cs_and_collation (target, TP_DOMAIN_CODESET (desired_domain),
					TP_DOMAIN_COLLATION (desired_domain));
      return DOMAIN_COMPATIBLE;
    }

  if (db_char_string_coerce (src, target, &data_stat) != NO_ERROR)
    {
      status = DOMAIN_INCOMPATIBLE;
    }
  else if (data_stat == DATA_STATUS_TRUNCATED)
    {
      status = DOMAIN_TRUNCATED;
    }
  else if (desired_domain->collation_flag != TP_DOMAIN_COLL_LEAVE)
    {
      db_string_put_cs_and_collation (target, TP_DOMAIN_CODESET (desired_domain),
				      TP_DOMAIN_COLLATION (desired_domain));
      status = DOMAIN_COMPATIBLE;
    }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_date_to_string (new_string, max_size, (DB_DATE *) db_get_date (src));

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_time_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_time_to_string (new_string, max_size, (DB_TIME *) db_get_time (src));

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_timestamp_to_string_core (new_string, max_size, (DB_TIMESTAMP *) db_get_timestamp (src), error);

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestampltz_to_varchar (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;
  TZ_ID ses_tz_id;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_utime = *db_get_timestamp (src);
    err = tz_create_session_tzid_for_timestamp_core (&v_utime, &ses_tz_id, error);
    if (err == NO_ERROR)
      {
	db_timestamptz_to_string_core (new_string, max_size, &v_utime, &ses_tz_id, error);
      }

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_timestamptz = *db_get_timestamptz (src);
    db_timestamptz_to_string_core (new_string, max_size, &v_timestamptz.timestamp, &v_timestamptz.tz_id,
				   error);

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_datetime_to_string (new_string, max_size, (DB_DATETIME *) db_get_datetime (src));

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATETIME v_datetime;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;
  TZ_ID ses_tz_id;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_datetime = *db_get_datetime (src);
    err = tz_create_session_tzid_for_datetime_core (&v_datetime, true, &ses_tz_id, error);
    if (err == NO_ERROR)
      {
	db_datetimetz_to_string_core (new_string, max_size, &v_datetime, &ses_tz_id, error);
      }

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATETIMETZ v_datetimetz;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_datetimetz = *db_get_datetimetz (src);
    db_datetimetz_to_string_core (new_string, max_size, &v_datetimetz.datetime, &v_datetimetz.tz_id, error);

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;

    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	status = DOMAIN_ERROR;
      }
    else
      {
	status = tp_value_convert_varchar_to_varchar (&varchar_val, target, desired_domain, error);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bit_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size;
    char *new_string;
    int convert_error;

    max_size = ((db_get_string_length (src) + 3) / 4) + 1;
    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    convert_error = bfmt_print (1 /* BIT_STRING_HEX */, src,
				new_string, max_size);

    if (convert_error == NO_ERROR)
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && (db_value_precision (target) < (int) strlen (new_string)))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_varchar_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else if (convert_error == -1)
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_clob_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE tmpval;
    DB_VALUE cs;

    db_make_null (&tmpval);
    /* convert directly from CLOB into charset of desired domain string */
    db_make_int (&cs, desired_domain->codeset);
    err = db_clob_to_char (src, &cs, &tmpval);
    if (err == NO_ERROR)
      {
	TP_DOMAIN_STATUS nested_status =
		tp_value_convert_varchar_to_varchar (&tmpval, target, desired_domain, error);
	if (nested_status == DOMAIN_COMPATIBLE || nested_status == DOMAIN_TRUNCATED)
	  {
	    status = nested_status;
	  }
	else
	  {
	    /* The old outer LOB cast returned success with a NULL result. */
	    pr_clear_value (target);
	    db_make_null (target);
	  }
      }

    pr_clear_value (&tmpval);
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    char *json_str;
    int len;

    json_str = db_json_get_raw_json_body_from_document (db_get_json_document (src));
    len = strlen (json_str);

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE && db_value_precision (target) < len)
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, json_str);
      }
    else
      {
	tp_make_varchar_conversion (desired_domain, json_str, target, &status, &data_stat);
	target->need_clear = true;
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = TP_BIGINT_PRECISION + 2 + 1;
    char *new_string;
    DB_BIGINT num;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    {
      num = (DB_BIGINT) db_get_short (src);
    }

    if (tp_ltoa (num, new_string, 10))
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && db_value_precision (target) < (int) strlen (new_string))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = TP_BIGINT_PRECISION + 2 + 1;
    char *new_string;
    DB_BIGINT num;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    {
      num = (DB_BIGINT) db_get_int (src);
    }

    if (tp_ltoa (num, new_string, 10))
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && db_value_precision (target) < (int) strlen (new_string))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = TP_BIGINT_PRECISION + 2 + 1;
    char *new_string;
    DB_BIGINT num;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    {
      num = db_get_bigint (src);
    }

    if (tp_ltoa (num, new_string, 10))
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && db_value_precision (target) < (int) strlen (new_string))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    {
      tp_ftoa_char (src, target);
    }

    if (DB_IS_NULL (target))
      {
	if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	  {
	    /* no way to report "out of memory" from tp_value_cast_internal() ?? */
	    status = DOMAIN_ERROR;
	  }
	else
	  {
	    status = DOMAIN_INCOMPATIBLE;
	  }
      }
    else if (DB_VALUE_PRECISION (target) != TP_FLOATING_PRECISION_VALUE
	     && (db_get_string_length (target) > DB_VALUE_PRECISION (target)))
      {
	status = DOMAIN_OVERFLOW;
	pr_clear_value (target);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    {
      tp_dtoa_char (src, target);
    }

    if (DB_IS_NULL (target))
      {
	if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
	  {
	    /* no way to report "out of memory" from tp_value_cast_internal() ?? */
	    status = DOMAIN_ERROR;
	  }
	else
	  {
	    status = DOMAIN_INCOMPATIBLE;
	  }
      }
    else if (DB_VALUE_PRECISION (target) != TP_FLOATING_PRECISION_VALUE
	     && (db_get_string_length (target) > DB_VALUE_PRECISION (target)))
      {
	status = DOMAIN_OVERFLOW;
	pr_clear_value (target);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_monetary_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    /* monetary symbol = 3 sign = 1 dot = 1 fraction digits = 2 NUL terminator = 1 */
    int max_size = DBL_MAX_DIGITS + 3 + 1 + 1 + 2 + 1;
    char *new_string;
    char *p;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    snprintf (new_string, max_size - 1, "%s%.*f", lang_currency_symbol (db_get_monetary (src)->type), 2,
	      db_get_monetary (src)->amount);
    new_string[max_size - 1] = '\0';

    p = new_string + strlen (new_string);
    for (--p; p >= new_string && *p == '0'; p--)
      {
	/* remove trailing zeros */
	*p = '\0';
      }
    if (*p == '.')	/* remove point */
      {
	*p = '\0';
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_numeric_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    char str_buf[NUMERIC_MAX_STRING_SIZE];
    char *new_string;
    int max_size;

    numeric_db_value_print (src, str_buf);

    max_size = strlen (str_buf) + 1;
    new_string = (char *) db_private_alloc (NULL, max_size);
    if (new_string == NULL)
      {
	return DOMAIN_ERROR;
      }

    strcpy (new_string, str_buf);

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < max_size - 1)
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_date_to_string (new_string, max_size, (DB_DATE *) db_get_date (src));

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_time_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_time_to_string (new_string, max_size, (DB_TIME *) db_get_time (src));

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				    date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_timestamp_to_string_core (new_string, max_size, (DB_TIMESTAMP *) db_get_timestamp (src), error);

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestampltz_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_UTIME v_utime;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;
  TZ_ID ses_tz_id;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_utime = *db_get_timestamp (src);
    err = tz_create_session_tzid_for_timestamp_core (&v_utime, &ses_tz_id, error);
    if (err == NO_ERROR)
      {
	db_timestamptz_to_string_core (new_string, max_size, &v_utime, &ses_tz_id, error);
      }

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_TIMESTAMPTZ v_timestamptz;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_timestamptz = *db_get_timestamptz (src);
    db_timestamptz_to_string_core (new_string, max_size, &v_timestamptz.timestamp, &v_timestamptz.tz_id,
				   error);

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				   date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    db_datetime_to_string (new_string, max_size, (DB_DATETIME *) db_get_datetime (src));

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATETIME v_datetime;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;
  TZ_ID ses_tz_id;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_datetime = *db_get_datetime (src);
    err = tz_create_session_tzid_for_datetime_core (&v_datetime, true, &ses_tz_id, error);
    if (err == NO_ERROR)
      {
	db_datetimetz_to_string_core (new_string, max_size, &v_datetime, &ses_tz_id, error);
      }

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATETIMETZ v_datetimetz;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size = DATETIMETZ_BUF_SIZE;
    char *new_string;

    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    err = NO_ERROR;

    v_datetimetz = *db_get_datetimetz (src);
    db_datetimetz_to_string_core (new_string, max_size, &v_datetimetz.datetime, &v_datetimetz.tz_id, error);

    if (err != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	&& db_value_precision (target) < (int) strlen (new_string))
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;

    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	status = DOMAIN_ERROR;
      }
    else
      {
	status = tp_value_convert_char_to_varchar (&varchar_val, target, desired_domain, error);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bit_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    int max_size;
    char *new_string;
    int convert_error;

    max_size = ((db_get_string_length (src) + 3) / 4) + 1;
    new_string = (char *) db_private_alloc (NULL, max_size);
    if (!new_string)
      {
	return DOMAIN_ERROR;
      }

    convert_error = bfmt_print (1 /* BIT_STRING_HEX */, src,
				new_string, max_size);

    if (convert_error == NO_ERROR)
      {
	if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE
	    && (db_value_precision (target) < (int) strlen (new_string)))
	  {
	    status = DOMAIN_OVERFLOW;
	    db_private_free_and_init (NULL, new_string);
	  }
	else
	  {
	    tp_make_char_conversion (desired_domain, new_string, target, &status, &data_stat);
	  }
      }
    else if (convert_error == -1)
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, new_string);
      }
    else
      {
	status = DOMAIN_ERROR;
	db_private_free_and_init (NULL, new_string);
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_clob_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE tmpval;
    DB_VALUE cs;

    db_make_null (&tmpval);
    /* convert directly from CLOB into charset of desired domain string */
    db_make_int (&cs, desired_domain->codeset);
    err = db_clob_to_char (src, &cs, &tmpval);
    if (err == NO_ERROR)
      {
	TP_DOMAIN_STATUS nested_status =
		tp_value_convert_char_to_varchar (&tmpval, target, desired_domain, error);
	if (nested_status == DOMAIN_COMPATIBLE || nested_status == DOMAIN_TRUNCATED)
	  {
	    status = nested_status;
	  }
	else
	  {
	    /* The old outer LOB cast returned success with a NULL result. */
	    pr_clear_value (target);
	    db_make_null (target);
	  }
      }

    pr_clear_value (&tmpval);
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  DB_DATA_STATUS data_stat = DATA_STATUS_OK;

  {
    char *json_str;
    int len;

    json_str = db_json_get_raw_json_body_from_document (db_get_json_document (src));
    len = strlen (json_str);

    if (db_value_precision (target) != TP_FLOATING_PRECISION_VALUE && db_value_precision (target) < len)
      {
	status = DOMAIN_OVERFLOW;
	db_private_free_and_init (NULL, json_str);
      }
    else
      {
	tp_make_char_conversion (desired_domain, json_str, target, &status, &data_stat);
	target->need_clear = true;
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_blob (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  err = db_char_to_blob (src, target);

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_blob (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;

    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_blob (&varchar_val, target, desired_domain, error);
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bit_to_blob (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			      date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  err = db_bit_to_blob (src, target);

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_clob (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  err = db_char_to_clob (src, target);

  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_clob (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    DB_VALUE varchar_val;
    if (tp_enumeration_to_varchar (src, &varchar_val) != NO_ERROR)
      {
	return DOMAIN_ERROR;
      }
    status = tp_value_convert_char_to_clob (&varchar_val, target, desired_domain, error);
    return status;
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    val_idx = (unsigned short) db_get_short (src);

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    if (OR_CHECK_USHRT_OVERFLOW (db_get_int (src)))
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	val_idx = (unsigned short) db_get_int (src);
      }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    if (OR_CHECK_USHRT_OVERFLOW (db_get_bigint (src)))
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	val_idx = (unsigned short) db_get_bigint (src);
      }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    if (OR_CHECK_USHRT_OVERFLOW (floor (db_get_float (src))))
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	val_idx = (unsigned short) floor (db_get_float (src));
      }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    if (OR_CHECK_USHRT_OVERFLOW (floor (db_get_double (src))))
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	val_idx = (unsigned short) floor (db_get_double (src));
      }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_monetary_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  const DB_MONETARY *v_money;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    v_money = db_get_monetary (src);
    if (OR_CHECK_USHRT_OVERFLOW (floor (v_money->amount)))
      {
	status = DOMAIN_INCOMPATIBLE;
      }
    else
      {
	val_idx = (unsigned short) floor (v_money->amount);
      }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_numeric_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return err < 0 ? DOMAIN_ERROR : status;
      }

    {
      DB_VALUE val;

      db_make_double (&val, 0);
      err = numeric_coerce_num_to_double (src, db_get_numeric_scale (src, NULL), &val);
      if (err != NO_ERROR)
	{
	  status = DOMAIN_ERROR;
	}
      else
	{
	  if (OR_CHECK_USHRT_OVERFLOW (floor (db_get_double (&val))))
	    {
	      status = DOMAIN_INCOMPATIBLE;
	    }
	  else
	    {
	      val_idx = (unsigned short) floor (db_get_double (&val));
	    }
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return err < 0 ? DOMAIN_ERROR : status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    if (db_get_string_codeset (src) != TP_DOMAIN_CODESET (desired_domain))
      {
	DB_DATA_STATUS data_status = DATA_STATUS_OK;

	if (TP_DOMAIN_CODESET (desired_domain) == INTL_CODESET_RAW_BYTES)
	  {
	    /* avoid data truncation when converting to binary charset */
	    db_value_domain_init (&conv_val, DB_TYPE_CHAR, db_get_string_size (src), 0);
	  }
	else
	  {
	    db_value_domain_init (&conv_val, DB_TYPE_CHAR, DB_VALUE_PRECISION (src), 0);
	  }

	db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (desired_domain),
					TP_DOMAIN_COLLATION (desired_domain));

	if (db_char_string_coerce (src, &conv_val, &data_status) != NO_ERROR || data_status != DATA_STATUS_OK)
	  {
	    status = DOMAIN_ERROR;
	    pr_clear_value (&conv_val);
	  }
	else
	  {
	    val_str = db_get_string (&conv_val);
	    val_str_size = db_get_string_size (&conv_val);
	  }
      }
    else
      {
	val_str = db_get_string (src);
	val_str_size = db_get_string_size (src);
      }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_varchar_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    if (db_get_string_codeset (src) != TP_DOMAIN_CODESET (desired_domain))
      {
	DB_DATA_STATUS data_status = DATA_STATUS_OK;

	if (TP_DOMAIN_CODESET (desired_domain) == INTL_CODESET_RAW_BYTES)
	  {
	    /* avoid data truncation when converting to binary charset */
	    db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, db_get_string_size (src), 0);
	  }
	else
	  {
	    db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, DB_VALUE_PRECISION (src), 0);
	  }

	db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (desired_domain),
					TP_DOMAIN_COLLATION (desired_domain));

	if (db_char_string_coerce (src, &conv_val, &data_status) != NO_ERROR || data_status != DATA_STATUS_OK)
	  {
	    status = DOMAIN_ERROR;
	    pr_clear_value (&conv_val);
	  }
	else
	  {
	    val_str = db_get_string (&conv_val);
	    val_str_size = db_get_string_size (&conv_val);
	  }
      }
    else
      {
	val_str = db_get_string (src);
	val_str_size = db_get_string_size (src);
      }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_date_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_time_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_time_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_timestamp_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestampltz_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_timestampltz_to_varchar (src, &conv_val,
		  tp_domain_resolve_default (DB_TYPE_STRING), error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_timestamptz_to_varchar (src, &conv_val,
		  tp_domain_resolve_default (DB_TYPE_STRING), error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_datetime_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_datetimeltz_to_varchar (src, &conv_val,
		  tp_domain_resolve_default (DB_TYPE_STRING), error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_datetimetz_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_enumeration_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return status;
      }

    if (DOM_GET_ENUM_ELEMS_COUNT (desired_domain) == 0)
      {
	pr_clone_value (src, target);
	exit = true;
      }
    else
      {
	val_str = db_get_enum_string (src);
	val_str_size = db_get_enum_string_size (src);
	if (val_str == NULL)
	  {
	    /* src has a short value or a string value or both. We prefer to use the string value when matching
	     * against the desired domain but, if this is not set, we will use the value index */
	    val_idx = db_get_enum_short (src);
	  }
	else
	  {
	    if (db_get_enum_codeset (src) != TP_DOMAIN_CODESET (desired_domain))
	      {
		/* first convert charset of the original value to charset of destination domain */
		DB_VALUE tmp;
		DB_DATA_STATUS data_status = DATA_STATUS_OK;

		/* charset conversion can handle only CHAR/VARCHAR DB_VALUEs, create a STRING value with max
		 * precision (so that no truncation occurs) from the ENUM source string */
		db_make_varchar (&tmp, DB_MAX_STRING_LENGTH, val_str, val_str_size, db_get_enum_codeset (src),
				 db_get_enum_collation (src));

		/* initialize destination value of conversion */
		db_value_domain_init (&conv_val, DB_TYPE_STRING, DB_MAX_STRING_LENGTH, 0);
		db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (desired_domain),
						TP_DOMAIN_COLLATION (desired_domain));

		if (db_char_string_coerce (&tmp, &conv_val, &data_status) != NO_ERROR
		    || data_status != DATA_STATUS_OK)
		  {
		    status = DOMAIN_ERROR;
		    pr_clear_value (&conv_val);
		    val_str = NULL;
		    val_idx = 0;
		  }
		else
		  {
		    val_str = db_get_string (&conv_val);
		    val_str_size = db_get_string_size (&conv_val);
		  }
		pr_clear_value (&tmp);
	      }
	  }
      }
    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_bit_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_bit_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_varbit_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_bit_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_blob_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_blob_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_clob_to_enumeration (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    unsigned short val_idx = 0;
    int val_str_size = 0;
    const char *val_str = NULL;
    bool exit = false;
    DB_VALUE conv_val;

    db_make_null (&conv_val);

    if (src->domain.general_info.is_null)
      {
	db_make_null (target);
	return DOMAIN_COMPATIBLE;
      }

    {
      const TP_DOMAIN *string_domain = tp_domain_resolve_default (DB_TYPE_STRING);
      db_value_domain_init (&conv_val, DB_TYPE_VARCHAR, string_domain->precision, string_domain->scale);
      db_string_put_cs_and_collation (&conv_val, TP_DOMAIN_CODESET (string_domain),
				      TP_DOMAIN_COLLATION (string_domain));
      status =
	      tp_value_convert_clob_to_varchar (src, &conv_val, tp_domain_resolve_default (DB_TYPE_STRING),
		  error);
      if (status == DOMAIN_COMPATIBLE)
	{
	  val_str = db_get_string (&conv_val);
	  val_str_size = db_get_string_size (&conv_val);
	}
    }

    return tp_finish_enumeration_conversion (target, desired_domain, conv_val, val_idx, val_str, val_str_size,
	   status, exit);
  }
  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_short_to_json (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    JSON_DOC *doc = NULL;

    doc = db_json_allocate_doc ();
    db_json_set_int_to_doc (doc, db_get_short (src));

    if (status == DOMAIN_COMPATIBLE)
      {
	db_make_json (target, doc, true);
      }
    else
      {
	if (doc != NULL)
	  {
	    db_json_delete_doc (doc);
	  }
      }
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_integer_to_json (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    JSON_DOC *doc = NULL;

    doc = db_json_allocate_doc ();
    db_json_set_int_to_doc (doc, db_get_int (src));

    if (status == DOMAIN_COMPATIBLE)
      {
	db_make_json (target, doc, true);
      }
    else
      {
	if (doc != NULL)
	  {
	    db_json_delete_doc (doc);
	  }
      }
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_bigint_to_json (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    JSON_DOC *doc = NULL;

    doc = db_json_allocate_doc ();
    db_json_set_bigint_to_doc (doc, db_get_bigint (src));

    if (status == DOMAIN_COMPATIBLE)
      {
	db_make_json (target, doc, true);
      }
    else
      {
	if (doc != NULL)
	  {
	    db_json_delete_doc (doc);
	  }
      }
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_float_to_json (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    JSON_DOC *doc = NULL;

    {
      DB_VALUE double_value;

      doc = db_json_allocate_doc ();
      db_make_double (&double_value, 0);
      tp_value_convert_number < DB_TYPE_FLOAT, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN > (
	      src, &double_value,
	      &tp_Double_domain, error)
      ;
      db_json_set_double_to_doc (doc, db_get_double (&double_value));
      pr_clear_value (&double_value);
    }

    if (status == DOMAIN_COMPATIBLE)
      {
	db_make_json (target, doc, true);
      }
    else
      {
	if (doc != NULL)
	  {
	    db_json_delete_doc (doc);
	  }
      }
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_double_to_json (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				 date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    JSON_DOC *doc = NULL;

    doc = db_json_allocate_doc ();
    db_json_set_double_to_doc (doc, db_get_double (src));

    if (status == DOMAIN_COMPATIBLE)
      {
	db_make_json (target, doc, true);
      }
    else
      {
	if (doc != NULL)
	  {
	    db_json_delete_doc (doc);
	  }
      }
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_numeric_to_json (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				  date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    JSON_DOC *doc = NULL;

    {
      DB_VALUE double_value;

      doc = db_json_allocate_doc ();
      db_make_double (&double_value, 0);
      tp_value_convert_number < DB_TYPE_NUMERIC, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN > (
	      src, &double_value,
	      &tp_Double_domain, error)
      ;
      db_json_set_double_to_doc (doc, db_get_double (&double_value));
      pr_clear_value (&double_value);
    }

    if (status == DOMAIN_COMPATIBLE)
      {
	db_make_json (target, doc, true);
      }
    else
      {
	if (doc != NULL)
	  {
	    db_json_delete_doc (doc);
	  }
      }
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_json (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			       date_conversion_error *error)
{
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    JSON_DOC *doc = NULL;

    DB_VALUE utf8_str;
    const DB_VALUE *json_str_val = &utf8_str;
    int error_code = db_json_copy_and_convert_to_utf8 (src, &utf8_str, &json_str_val);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	status = DOMAIN_ERROR;
      }
    else
      {
	unsigned int str_size = db_get_string_size (json_str_val);
	const char *original_str = db_get_string (json_str_val);

	error_code = db_json_get_json_from_str (original_str, doc, str_size);
	if (error_code != NO_ERROR)
	  {
	    pr_clear_value (&utf8_str);
	    assert (doc == NULL);
	    status = DOMAIN_ERROR;
	  }
	else if (desired_domain->json_validator
		 && db_json_validate_doc (desired_domain->json_validator, doc) != NO_ERROR)
	  {
	    ASSERT_ERROR ();
	    pr_clear_value (&utf8_str);
	    db_json_delete_doc (doc);
	    status = DOMAIN_ERROR;
	  }
	else
	  {
	    pr_clear_value (&utf8_str);
	  }
      }

    if (status == DOMAIN_COMPATIBLE)
      {
	db_make_json (target, doc, true);
      }
    else
      {
	if (doc != NULL)
	  {
	    db_json_delete_doc (doc);
	  }
      }
  }

  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_time_strict (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  {
    DB_TIME time = 0;
    if (tp_atotime_core (src, &time, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_value_put_encoded_time (target, &time);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_date_strict (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  {
    DB_DATE date = 0;

    if (tp_atodate_core (src, &date, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_value_put_encoded_date (target, &date);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_date_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATE date = 0;
    DB_TIME time = 0;
    DB_TIMESTAMP *ts = NULL;

    ts = db_get_timestamp (src);
    (void) db_timestamp_decode_ses_core (ts, &date, &time, error);
    if (time != 0)
      {
	/* only "downcast" if time is 0 */
	return DOMAIN_INCOMPATIBLE;
      }
    db_value_put_encoded_date (target, &date);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_date_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_DATE date = 0;
    DB_TIME time = 0;
    DB_TIMESTAMPTZ *ts_tz = NULL;

    ts_tz = db_get_timestamptz (src);
    err = db_timestamp_decode_w_tz_id_core (&ts_tz->timestamp, &ts_tz->tz_id, &date, &time, error);
    if (err != NO_ERROR || time != 0)
      {
	/* only "downcast" if time is 0 */
	return DOMAIN_INCOMPATIBLE;
      }
    db_value_put_encoded_date (target, &date);
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_date_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME *src_dt = NULL;

    src_dt = db_get_datetime (src);
    if (src_dt->time != 0)
      {
	/* only "downcast" if time is 0 */
	return DOMAIN_INCOMPATIBLE;
      }
    db_value_put_encoded_date (target, (DB_DATE *) (&src_dt->date));
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_date_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME *utc_dt_p;
    DB_DATETIME local_dt;
    TZ_ID tz_id;

    /* DATETIMELTZ and DATETIMETZ store in UTC, convert to session */
    {
      utc_dt_p = db_get_datetime (src);
      if (tz_create_session_tzid_for_datetime_core (utc_dt_p, true, &tz_id, error) != NO_ERROR)
	{
	  return DOMAIN_INCOMPATIBLE;
	}
    }

    if (tz_utc_datetimetz_to_local_core (utc_dt_p, &tz_id, &local_dt, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }

    if (local_dt.time != 0)
      {
	/* only "downcast" if time is 0 */
	return DOMAIN_INCOMPATIBLE;
      }

    db_value_put_encoded_date (target, (DB_DATE *) (&local_dt.date));
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_date_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME *utc_dt_p;
    DB_DATETIMETZ *dt_tz_p;
    DB_DATETIME local_dt;
    TZ_ID tz_id;

    /* DATETIMELTZ and DATETIMETZ store in UTC, convert to session */
    {
      dt_tz_p = db_get_datetimetz (src);
      utc_dt_p = &dt_tz_p->datetime;
      tz_id = dt_tz_p->tz_id;
    }

    if (tz_utc_datetimetz_to_local_core (utc_dt_p, &tz_id, &local_dt, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }

    if (local_dt.time != 0)
      {
	/* only "downcast" if time is 0 */
	return DOMAIN_INCOMPATIBLE;
      }

    db_value_put_encoded_date (target, (DB_DATE *) (&local_dt.date));
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_datetime_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME datetime = { 0, 0 };
    if (tp_atoudatetime_core (src, &datetime, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_datetime (target, &datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_datetime_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME datetime = { 0, 0 };
    datetime.date = *db_get_date (src);
    datetime.time = 0;
    db_make_datetime (target, &datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_datetime_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME datetime = { 0, 0 };
    DB_TIMESTAMP *utime = db_get_timestamp (src);
    DB_DATE date;
    DB_TIME time;

    if (db_timestamp_decode_ses_core (utime, &date, &time, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    datetime.time = time * 1000;
    datetime.date = date;
    db_make_datetime (target, &datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_datetime_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME datetime = { 0, 0 };
    DB_DATE date;
    DB_TIME time;
    DB_TIMESTAMPTZ *ts_tz = db_get_timestamptz (src);

    if (db_timestamp_decode_w_tz_id_core (&ts_tz->timestamp, &ts_tz->tz_id, &date, &time, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }

    datetime.time = time * 1000;
    datetime.date = date;
    db_make_datetime (target, &datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_datetime_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME *dt = db_get_datetime (src);
    db_make_datetime (target, dt);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_datetime_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIMETZ *dt_tz = db_get_datetimetz (src);
    db_make_datetime (target, &dt_tz->datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_datetimetz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;

    if (tp_atodatetimetz_core (src, &dt_tz, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }

    db_make_datetimetz (target, &dt_tz);
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_datetimetz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;

    {
      dt_tz.datetime.date = *db_get_date (src);
      dt_tz.datetime.time = 0;
    }

    err = tz_create_datetimetz_from_ses_core (& (dt_tz.datetime), &dt_tz, error);
    if (err == NO_ERROR)
      {
	db_make_datetimetz (target, &dt_tz);
      }
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_datetimetz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;
    DB_TIMESTAMP *utime = db_get_timestamp (src);
    DB_DATE date;
    DB_TIME time;

    /* convert DT to TS in UTC reference */
    db_timestamp_decode_utc (utime, &date, &time);
    dt_tz.datetime.date = date;
    dt_tz.datetime.time = time * 1000;
    err = tz_create_session_tzid_for_datetime_core (&dt_tz.datetime, true, & (dt_tz.tz_id), error);
    if (err == NO_ERROR)
      {
	db_make_datetimetz (target, &dt_tz);
      }
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_datetimetz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;
    DB_TIMESTAMPTZ *ts_tz = db_get_timestamptz (src);
    DB_DATE date;
    DB_TIME time;

    (void) db_timestamp_decode_utc (&ts_tz->timestamp, &date, &time);
    dt_tz.datetime.time = time * 1000;
    dt_tz.datetime.date = date;
    dt_tz.tz_id = ts_tz->tz_id;
    db_make_datetimetz (target, &dt_tz);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_datetimetz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;

    {
      dt_tz.datetime = *db_get_datetime (src);
    }

    err = tz_create_datetimetz_from_ses_core (& (dt_tz.datetime), &dt_tz, error);
    if (err == NO_ERROR)
      {
	db_make_datetimetz (target, &dt_tz);
      }
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_datetimetz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;
    DB_DATETIME *dt = db_get_datetime (src);

    dt_tz.datetime = *dt;
    err = tz_create_session_tzid_for_datetime_core (dt, false, &dt_tz.tz_id, error);
    if (err == NO_ERROR)
      {
	db_make_datetimetz (target, &dt_tz);
      }
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_datetimeltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIMETZ dt_tz = DB_DATETIMETZ_INITIALIZER;

    if (tp_atodatetimetz_core (src, &dt_tz, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_datetimeltz (target, &dt_tz.datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_datetimeltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_DATETIME datetime;
    DB_DATETIMETZ dt_tz;

    {
      datetime.date = *db_get_date (src);
      datetime.time = 0;
    }

    err = tz_create_datetimetz_from_ses_core (&datetime, &dt_tz, error);
    if (err != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }

    db_make_datetimeltz (target, &dt_tz.datetime);
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_datetimeltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME datetime = { 0, 0 };
    DB_TIMESTAMP *utime = db_get_timestamp (src);
    DB_DATE date;
    DB_TIME time;

    (void) db_timestamp_decode_utc (utime, &date, &time);
    datetime.time = time * 1000;
    datetime.date = date;
    db_make_datetimeltz (target, &datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_datetimeltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  {
    DB_DATETIME datetime = { 0, 0 };
    DB_TIMESTAMPTZ *ts_tz = db_get_timestamptz (src);
    DB_DATE date;
    DB_TIME time;

    (void) db_timestamp_decode_utc (&ts_tz->timestamp, &date, &time);
    datetime.time = time * 1000;
    datetime.date = date;
    db_make_datetimeltz (target, &datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_datetimeltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_DATETIME datetime;
    DB_DATETIMETZ dt_tz;

    {
      datetime = *db_get_datetime (src);
    }

    err = tz_create_datetimetz_from_ses_core (&datetime, &dt_tz, error);
    if (err != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }

    db_make_datetimeltz (target, &dt_tz.datetime);
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_datetimeltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIMETZ *dt_tz = db_get_datetimetz (src);

    /* copy datetime (UTC) */
    db_make_datetimeltz (target, &dt_tz->datetime);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_timestamp_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMP ts = 0;

    if (tp_atoutime_core (src, &ts, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamp (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_timestamp_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIME tm = 0;
    DB_DATE date = *db_get_date (src);
    DB_TIMESTAMP ts = 0;

    db_time_encode_core (&tm, 0, 0, 0, error);
    if (db_timestamp_encode_ses_core (&date, &tm, &ts, NULL, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamp (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestampltz_to_timestamp_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMP *ts = db_get_timestamp (src);

    /* copy timestamp value (UTC) */
    db_make_timestamp (target, *ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_timestamp_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ *ts_tz = db_get_timestamptz (src);

    /* copy timestamp value (UTC) */
    db_make_timestamp (target, ts_tz->timestamp);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_timestamp_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME dt = *db_get_datetime (src);
    DB_DATE date = dt.date;
    DB_TIME time = dt.time / 1000;
    DB_TIMESTAMP ts = 0;

    if (db_timestamp_encode_ses_core (&date, &time, &ts, NULL, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamp (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_timestamp_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME dt = *db_get_datetime (src);
    DB_DATE date = dt.date;
    DB_TIME time = dt.time / 1000;
    DB_TIMESTAMP ts = 0;

    if (db_timestamp_encode_utc_core (&date, &time, &ts, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamp (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_timestamp_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIMETZ *dt_tz = db_get_datetimetz (src);
    DB_DATE date = dt_tz->datetime.date;
    DB_TIME time = dt_tz->datetime.time / 1000;
    DB_TIMESTAMP ts = 0;

    if (db_timestamp_encode_utc_core (&date, &time, &ts, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamp (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_timestampltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ ts_tz = { 0, 0 };

    if (tp_atotimestamptz_core (src, &ts_tz, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestampltz (target, ts_tz.timestamp);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_timestampltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIME tm = 0;
    DB_DATE date = *db_get_date (src);
    DB_TIMESTAMP ts = 0;

    db_time_encode_core (&tm, 0, 0, 0, error);
    if (db_timestamp_encode_ses_core (&date, &tm, &ts, NULL, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestampltz (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_timestampltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMP *ts = db_get_timestamp (src);

    /* copy val timestamp value (UTC) */
    db_make_timestampltz (target, *ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamptz_to_timestampltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ *ts_tz = db_get_timestamptz (src);

    /* copy val timestamp value (UTC) */
    db_make_timestampltz (target, ts_tz->timestamp);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_timestampltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_DATETIME dt = *db_get_datetime (src);
    DB_DATE date = dt.date;
    DB_TIME time = dt.time / 1000;
    DB_TIMESTAMP ts = 0;

    if (db_timestamp_encode_ses_core (&date, &time, &ts, NULL, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestampltz (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_timestampltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  {
    DB_DATETIME dt = *db_get_datetime (src);
    DB_DATE date = dt.date;
    DB_TIME time = dt.time / 1000;
    DB_TIMESTAMP ts = 0;

    if (db_timestamp_encode_utc_core (&date, &time, &ts, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestampltz (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_timestampltz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  {
    DB_DATETIMETZ *dt_tz = db_get_datetimetz (src);
    DB_DATE date = dt_tz->datetime.date;
    DB_TIME time = dt_tz->datetime.time / 1000;
    DB_TIMESTAMP ts = 0;

    if (db_timestamp_encode_utc_core (&date, &time, &ts, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestampltz (target, ts);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_char_to_timestamptz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ ts_tz = { 0, 0 };

    if (tp_atotimestamptz_core (src, &ts_tz, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamptz (target, &ts_tz);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_date_to_timestamptz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
    DB_TIME tm = 0;
    DB_DATE date = *db_get_date (src);

    db_time_encode_core (&tm, 0, 0, 0, error);
    if (db_timestamp_encode_ses_core (&date, &tm, &ts_tz.timestamp, &ts_tz.tz_id, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamptz (target, &ts_tz);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_timestamp_to_timestamptz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  int err = NO_ERROR;

  {
    DB_TIMESTAMPTZ ts_tz = { 0, 0 };

    ts_tz.timestamp = *db_get_timestamp (src);

    err = tz_create_session_tzid_for_timestamp_core (& (ts_tz.timestamp), & (ts_tz.tz_id), error);

    if (err != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamptz (target, &ts_tz);
    return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
  }

  return err == NO_ERROR ? DOMAIN_COMPATIBLE : DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetime_to_timestamptz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
    DB_DATETIME dt = *db_get_datetime (src);
    DB_DATE date = dt.date;
    DB_TIME time = dt.time / 1000;

    if (db_timestamp_encode_ses_core (&date, &time, &ts_tz.timestamp, &ts_tz.tz_id, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    db_make_timestamptz (target, &ts_tz);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimeltz_to_timestamptz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
    DB_DATETIME dt = *db_get_datetime (src);
    DB_DATE date = dt.date;
    DB_TIME time = dt.time / 1000;

    if (db_timestamp_encode_utc_core (&date, &time, &ts_tz.timestamp, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    ts_tz.tz_id = *tz_get_utc_tz_id ();
    db_make_timestamptz (target, &ts_tz);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_datetimetz_to_timestamptz_strict (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  {
    DB_TIMESTAMPTZ ts_tz = { 0, 0 };
    DB_DATETIMETZ *dt_tz = db_get_datetimetz (src);
    DB_DATE date = dt_tz->datetime.date;
    DB_TIME time = dt_tz->datetime.time / 1000;

    if (db_timestamp_encode_utc_core (&date, &time, &ts_tz.timestamp, error) != NO_ERROR)
      {
	return DOMAIN_INCOMPATIBLE;
      }
    ts_tz.tz_id = dt_tz->tz_id;
    db_make_timestamptz (target, &ts_tz);
    return DOMAIN_COMPATIBLE;
  }

  return DOMAIN_COMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_incompatible (const DB_VALUE *, DB_VALUE *, const TP_DOMAIN *, date_conversion_error *)
{
  return DOMAIN_INCOMPATIBLE;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_validate (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				date_conversion_error *)
{
  if (desired_domain->json_validator != nullptr
      && db_json_validate_doc (desired_domain->json_validator, src->data.json.document) != NO_ERROR)
    {
      return DOMAIN_ERROR;
    }
  if (src != target)
    {
      pr_clone_value (src, target);
    }
  return DOMAIN_COMPATIBLE;
}

bool
tp_json_unwrap_scalar (const DB_VALUE *src, bool bool_as_string, DB_VALUE *scalar)
{
  DB_JSON_TYPE json_type = db_json_get_type (db_get_json_document (src));
  JSON_DOC *src_doc = db_get_json_document (src);
  bool use_replacement = true;

  switch (json_type)
    {
    case DB_JSON_DOUBLE:
      db_make_double (scalar, db_json_get_double_from_document (src_doc));
      break;
    case DB_JSON_INT:
      db_make_int (scalar, db_json_get_int_from_document (src_doc));
      break;
    case DB_JSON_BIGINT:
      db_make_bigint (scalar, db_json_get_bigint_from_document (src_doc));
      break;
    case DB_JSON_BOOL:
      if (bool_as_string)
	{
	  db_make_string (scalar, db_json_get_bool_as_str_from_document (src_doc));
	  scalar->need_clear = true;

	}
      else
	{

	  db_make_int (scalar, db_json_get_bool_from_document (src_doc) ? 1 : 0);

	}
      break;
    case DB_JSON_STRING:
      db_make_string_copy (scalar, db_json_get_string_from_document (src_doc));
      break;
    default:
      use_replacement = false;
      /* do nothing */
      break;
    }

  return use_replacement;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_short (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status =
	      tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_SHORT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_INT:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_SHORT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BIGINT:
      status =
	      tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_SHORT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BOOL:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_SHORT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_STRING:
      status =
	      tp_value_convert_number < DB_TYPE_VARCHAR, DB_TYPE_SHORT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_integer (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status =
	      tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_INT:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BIGINT:
      status =
	      tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BOOL:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_STRING:
      status =
	      tp_value_convert_number < DB_TYPE_VARCHAR, DB_TYPE_INTEGER, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_bigint (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status =
	      tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_BIGINT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_INT:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_BIGINT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BIGINT:
      status =
	      tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_BIGINT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BOOL:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_BIGINT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_STRING:
      status =
	      tp_value_convert_number < DB_TYPE_VARCHAR, DB_TYPE_BIGINT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_float (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				       date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status =
	      tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_FLOAT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_INT:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_FLOAT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BIGINT:
      status =
	      tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_FLOAT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BOOL:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_FLOAT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_STRING:
      status =
	      tp_value_convert_number < DB_TYPE_VARCHAR, DB_TYPE_FLOAT, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_double (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status =
	      tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_INT:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BIGINT:
      status =
	      tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BOOL:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_STRING:
      status =
	      tp_value_convert_number < DB_TYPE_VARCHAR, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_monetary (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status =
	      tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_MONETARY, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_INT:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_MONETARY, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BIGINT:
      status =
	      tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_MONETARY, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BOOL:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_MONETARY, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_STRING:
      status =
	      tp_value_convert_number < DB_TYPE_VARCHAR, DB_TYPE_MONETARY, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_numeric (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status =
	      tp_value_convert_number < DB_TYPE_DOUBLE, DB_TYPE_NUMERIC, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_INT:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_NUMERIC, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BIGINT:
      status =
	      tp_value_convert_number < DB_TYPE_BIGINT, DB_TYPE_NUMERIC, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_BOOL:
      status =
	      tp_value_convert_number < DB_TYPE_INTEGER, DB_TYPE_NUMERIC, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    case DB_JSON_STRING:
      status =
	      tp_value_convert_number < DB_TYPE_VARCHAR, DB_TYPE_NUMERIC, DOMAIN_CONVERT_ASSIGN > (
		      &scalar, target,
		      desired_domain, error)
	      ;
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_char (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, true, &scalar))
    {
      return tp_value_convert_json_to_char (src, target, desired_domain, error);
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status = tp_value_convert_double_to_char (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_INT:
      status = tp_value_convert_integer_to_char (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BIGINT:
      status = tp_value_convert_bigint_to_char (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BOOL:
      status = tp_value_convert_char_to_varchar (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_varchar (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_varchar (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, true, &scalar))
    {
      return tp_value_convert_json_to_varchar (src, target, desired_domain, error);
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status = tp_value_convert_double_to_varchar (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_INT:
      status = tp_value_convert_integer_to_varchar (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BIGINT:
      status = tp_value_convert_bigint_to_varchar (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BOOL:
      status = tp_value_convert_varchar_to_varchar (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_varchar_to_varchar (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_date (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_date (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_time (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status = tp_value_convert_double_to_time (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_INT:
      status = tp_value_convert_integer_to_time (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BIGINT:
      status = tp_value_convert_bigint_to_time (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BOOL:
      status = tp_value_convert_integer_to_time (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_time (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_timestamp (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status = tp_value_convert_double_to_timestamp (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_INT:
      status = tp_value_convert_integer_to_timestamp (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BIGINT:
      status = tp_value_convert_bigint_to_timestamp (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BOOL:
      status = tp_value_convert_integer_to_timestamp (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_timestamp (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_timestampltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status = tp_value_convert_double_to_timestampltz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_INT:
      status = tp_value_convert_integer_to_timestampltz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BIGINT:
      status = tp_value_convert_bigint_to_timestampltz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BOOL:
      status = tp_value_convert_integer_to_timestampltz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_timestampltz (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_timestamptz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status = tp_value_convert_double_to_timestamptz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_INT:
      status = tp_value_convert_integer_to_timestamptz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BIGINT:
      status = tp_value_convert_bigint_to_timestamptz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BOOL:
      status = tp_value_convert_integer_to_timestamptz (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_timestamptz (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_datetime (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_datetime (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_datetimeltz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_datetimeltz (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_datetimetz (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_datetimetz (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_enumeration (const DB_VALUE *src, DB_VALUE *target,
    const TP_DOMAIN *desired_domain, date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      status = tp_value_convert_double_to_enumeration (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_INT:
      status = tp_value_convert_integer_to_enumeration (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BIGINT:
      status = tp_value_convert_bigint_to_enumeration (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_BOOL:
      status = tp_value_convert_integer_to_enumeration (&scalar, target, desired_domain, error);
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_varchar_to_enumeration (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_bit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				     date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_bit (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_varbit (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
					date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_bit (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_blob (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_blob (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

static TP_DOMAIN_STATUS
tp_value_convert_json_scalar_to_clob (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
				      date_conversion_error *error)
{
  DB_VALUE scalar;
  db_make_null (&scalar);
  if (!tp_json_unwrap_scalar (src, false, &scalar))
    {
      return DOMAIN_INCOMPATIBLE;
    }
  TP_DOMAIN_STATUS status = DOMAIN_INCOMPATIBLE;
  switch (db_json_get_type (db_get_json_document (src)))
    {
    case DB_JSON_DOUBLE:
      break;
    case DB_JSON_INT:
      break;
    case DB_JSON_BIGINT:
      break;
    case DB_JSON_BOOL:
      break;
    case DB_JSON_STRING:
      status = tp_value_convert_char_to_clob (&scalar, target, desired_domain, error);
      break;
    default:
      break;
    }
  pr_clear_value (&scalar);
  return status;
}

#if !defined (SERVER_MODE)
static TP_DOMAIN_STATUS
tp_value_convert_oid_to_oid (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *, date_conversion_error *)
{
  pr_clone_value (src, target);
  return DOMAIN_COMPATIBLE;
}
#endif /* !defined (SERVER_MODE) */

/*
 * tp_value_convert_enumeration_name_to_double () - an ENUM added to a string without plus_as_concat: its name, then
 *   the name read as a number (qdata_add_dbval casts the ENUM to VARCHAR, then both strings to DOUBLE). The
 *   converter of (ENUM, DOUBLE) stays the ordinal; the type rules pick this one for that position.
 */
static TP_DOMAIN_STATUS
tp_value_convert_enumeration_name_to_double (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
    date_conversion_error *error)
{
  TP_DOMAIN *varchar_domain = tp_domain_resolve_default (DB_TYPE_VARCHAR);
  DB_VALUE name;

  db_value_domain_init (&name, DB_TYPE_VARCHAR, varchar_domain->precision, 0);
  TP_DOMAIN_STATUS status = tp_value_convert_enumeration_to_varchar (src, &name, varchar_domain, error);
  if (status == DOMAIN_COMPATIBLE)
    {
      status = tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_DOUBLE, DOMAIN_CONVERT_ASSIGN> (&name, target,
	       desired_domain, error);
    }
  pr_clear_value (&name);
  return status;
}

TP_VALUE_CONVERTER
domain_enumeration_name_converter (void)
{
  return tp_value_convert_enumeration_name_to_double;
}

/*
 * tp_value_convert_collection () - a set, multiset or sequence into a collection of type COLLECTION: the same set
 *   under the target domain when the domains are compatible, a coerced copy otherwise; IMPLICIT_ELEMENTS coerces the
 *   elements implicitly (tp_value_coerce), not explicitly (tp_value_cast)
 */
template <DB_TYPE COLLECTION, bool IMPLICIT_ELEMENTS>
static TP_DOMAIN_STATUS
tp_value_convert_collection (const DB_VALUE *src, DB_VALUE *target, const TP_DOMAIN *desired_domain,
			     date_conversion_error *error)
{
  int err = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  {
    SETREF *setref;

    setref = db_get_set (src);
    if (setref)
      {
	TP_DOMAIN *set_domain;

	set_domain = setobj_domain (setref->set);
	{
	  if (tp_domain_compatible (set_domain, desired_domain))
	    {
	      /*
	       * Well, we can't use the exact same set, but we don't
	       * have to do the whole hairy coerce thing either: we
	       * can just make a copy and then take the more general
	       * domain.  setobj_put_domain() guards against null
	       * pointers, there's no need to check first.
	       */
	      setref = set_copy (setref);
	      if (setref)
		{
		  setobj_put_domain (setref->set, (TP_DOMAIN *) desired_domain);
		}
	    }
	  else
	    {
	      /*
	       * Well, now we have to use the whole hairy coercion
	       * thing.  Too bad...
	       *
	       * This case will crop up when someone tries to cast a
	       * "set of int" as a "set of float", for example.
	       */
	      setref =
		      set_coerce (setref, (TP_DOMAIN *) desired_domain, IMPLICIT_ELEMENTS);
	    }

	  if (setref == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      err = er_errid ();
	    }
	  else
	    {
	      if constexpr (COLLECTION == DB_TYPE_SET)
		{
		  err = db_make_set (target, setref);
		}
	      else if constexpr (COLLECTION == DB_TYPE_MULTISET)
		{
		  err = db_make_multiset (target, setref);
		}
	      else
		{
		  static_assert (COLLECTION == DB_TYPE_SEQUENCE, "not a collection type");
		  err = db_make_sequence (target, setref);
		}
	    }
	}
	if (!setref || err < 0)
	  {
	    status = DOMAIN_INCOMPATIBLE;
	  }
      }
  }

  return err < 0 ? DOMAIN_ERROR : status;
}

/*
 * tp_value_find_converter () - the converter of a value of type src into a domain of type dst in a mode
 *   return: the converter; nullptr for the same type with nothing to convert; tp_value_convert_incompatible
 *	     for a pair that does not convert in the mode
 *
 * Every pair that converts is named once, grouped by its target. The mode picks within a case: COMPARE and
 * OPERAND take the strict converters, IMPLICIT is ASSIGN but for the pairs implicit coercion refuses and the
 * collection targets, which coerce the elements implicitly.
 */
template <DOMAIN_CONVERT_MODE MODE>
static TP_VALUE_CONVERTER
tp_value_find_converter (DB_TYPE src, DB_TYPE dst)
{
  constexpr bool strict = MODE == DOMAIN_CONVERT_COMPARE || MODE == DOMAIN_CONVERT_OPERAND;
  /* the numeric converters of IMPLICIT are ASSIGN's */
  constexpr DOMAIN_CONVERT_MODE CONVERTER_MODE = MODE == DOMAIN_CONVERT_IMPLICIT ? DOMAIN_CONVERT_ASSIGN : MODE;

  if (MODE == DOMAIN_CONVERT_IMPLICIT && TP_IMPLICIT_COERCION_NOT_ALLOWED (src, dst))
    {
      return tp_value_convert_incompatible;
    }
  switch (dst)
    {
    case DB_TYPE_SHORT:
      switch (src)
	{
	case DB_TYPE_INTEGER:
	  return tp_value_convert_number<DB_TYPE_INTEGER, DB_TYPE_SHORT, CONVERTER_MODE>;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_number<DB_TYPE_BIGINT, DB_TYPE_SHORT, CONVERTER_MODE>;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_number<DB_TYPE_FLOAT, DB_TYPE_SHORT, CONVERTER_MODE>;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_number<DB_TYPE_DOUBLE, DB_TYPE_SHORT, CONVERTER_MODE>;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_number<DB_TYPE_MONETARY, DB_TYPE_SHORT, CONVERTER_MODE>;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_SHORT, CONVERTER_MODE>;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_SHORT, CONVERTER_MODE>;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_short;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_short;
	default:
	  break;
	}
      break;
    case DB_TYPE_INTEGER:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_number<DB_TYPE_SHORT, DB_TYPE_INTEGER, CONVERTER_MODE>;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_number<DB_TYPE_BIGINT, DB_TYPE_INTEGER, CONVERTER_MODE>;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_number<DB_TYPE_FLOAT, DB_TYPE_INTEGER, CONVERTER_MODE>;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_number<DB_TYPE_DOUBLE, DB_TYPE_INTEGER, CONVERTER_MODE>;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_number<DB_TYPE_MONETARY, DB_TYPE_INTEGER, CONVERTER_MODE>;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_INTEGER, CONVERTER_MODE>;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_INTEGER, CONVERTER_MODE>;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_integer;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_integer;
	default:
	  break;
	}
      break;
    case DB_TYPE_BIGINT:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_number<DB_TYPE_SHORT, DB_TYPE_BIGINT, CONVERTER_MODE>;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_number<DB_TYPE_INTEGER, DB_TYPE_BIGINT, CONVERTER_MODE>;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_number<DB_TYPE_FLOAT, DB_TYPE_BIGINT, CONVERTER_MODE>;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_number<DB_TYPE_DOUBLE, DB_TYPE_BIGINT, CONVERTER_MODE>;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_number<DB_TYPE_MONETARY, DB_TYPE_BIGINT, CONVERTER_MODE>;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_BIGINT, CONVERTER_MODE>;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_BIGINT, CONVERTER_MODE>;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_bigint;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_bigint;
	default:
	  break;
	}
      break;
    case DB_TYPE_FLOAT:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_number<DB_TYPE_SHORT, DB_TYPE_FLOAT, CONVERTER_MODE>;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_number<DB_TYPE_INTEGER, DB_TYPE_FLOAT, CONVERTER_MODE>;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_number<DB_TYPE_BIGINT, DB_TYPE_FLOAT, CONVERTER_MODE>;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_number<DB_TYPE_DOUBLE, DB_TYPE_FLOAT, CONVERTER_MODE>;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_number<DB_TYPE_MONETARY, DB_TYPE_FLOAT, CONVERTER_MODE>;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_FLOAT, CONVERTER_MODE>;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_FLOAT, CONVERTER_MODE>;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_float;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_float;
	default:
	  break;
	}
      break;
    case DB_TYPE_DOUBLE:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_number<DB_TYPE_SHORT, DB_TYPE_DOUBLE, CONVERTER_MODE>;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_number<DB_TYPE_INTEGER, DB_TYPE_DOUBLE, CONVERTER_MODE>;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_number<DB_TYPE_BIGINT, DB_TYPE_DOUBLE, CONVERTER_MODE>;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_number<DB_TYPE_FLOAT, DB_TYPE_DOUBLE, CONVERTER_MODE>;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_number<DB_TYPE_MONETARY, DB_TYPE_DOUBLE, CONVERTER_MODE>;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_DOUBLE, CONVERTER_MODE>;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_DOUBLE, CONVERTER_MODE>;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_double;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_double;
	default:
	  break;
	}
      break;
    case DB_TYPE_MONETARY:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_number<DB_TYPE_SHORT, DB_TYPE_MONETARY, CONVERTER_MODE>;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_number<DB_TYPE_INTEGER, DB_TYPE_MONETARY, CONVERTER_MODE>;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_number<DB_TYPE_BIGINT, DB_TYPE_MONETARY, CONVERTER_MODE>;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_number<DB_TYPE_FLOAT, DB_TYPE_MONETARY, CONVERTER_MODE>;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_number<DB_TYPE_DOUBLE, DB_TYPE_MONETARY, CONVERTER_MODE>;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_MONETARY, CONVERTER_MODE>;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_MONETARY, CONVERTER_MODE>;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_monetary;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_monetary;
	default:
	  break;
	}
      break;
    case DB_TYPE_NUMERIC:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_number<DB_TYPE_SHORT, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_number<DB_TYPE_INTEGER, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_number<DB_TYPE_BIGINT, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_number<DB_TYPE_FLOAT, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_number<DB_TYPE_DOUBLE, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_number<DB_TYPE_MONETARY, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_number<DB_TYPE_NUMERIC, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_number<DB_TYPE_VARCHAR, DB_TYPE_NUMERIC, CONVERTER_MODE>;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_numeric;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_numeric;
	default:
	  break;
	}
      break;
    case DB_TYPE_CHAR:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_short_to_char;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_integer_to_char;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_bigint_to_char;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_float_to_char;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_double_to_char;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_monetary_to_char;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_numeric_to_char;
	case DB_TYPE_CHAR:
	  return tp_value_convert_varchar_to_varchar;
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_char_to_varchar;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  return tp_value_convert_bit_to_char;
	case DB_TYPE_TIME:
	  return tp_value_convert_time_to_char;
	case DB_TYPE_DATE:
	  return tp_value_convert_date_to_char;
	case DB_TYPE_TIMESTAMP:
	  return tp_value_convert_timestamp_to_char;
	case DB_TYPE_TIMESTAMPLTZ:
	  return tp_value_convert_timestampltz_to_char;
	case DB_TYPE_TIMESTAMPTZ:
	  return tp_value_convert_timestamptz_to_char;
	case DB_TYPE_DATETIME:
	  return tp_value_convert_datetime_to_char;
	case DB_TYPE_DATETIMELTZ:
	  return tp_value_convert_datetimeltz_to_char;
	case DB_TYPE_DATETIMETZ:
	  return tp_value_convert_datetimetz_to_char;
	case DB_TYPE_CLOB:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_clob_to_char;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_char;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_char;
	default:
	  break;
	}
      break;
    case DB_TYPE_VARCHAR:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_short_to_varchar;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_integer_to_varchar;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_bigint_to_varchar;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_float_to_varchar;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_double_to_varchar;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_monetary_to_varchar;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_numeric_to_varchar;
	case DB_TYPE_CHAR:
	  return tp_value_convert_char_to_varchar;
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_varchar_to_varchar;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  return tp_value_convert_bit_to_varchar;
	case DB_TYPE_TIME:
	  return tp_value_convert_time_to_varchar;
	case DB_TYPE_DATE:
	  return tp_value_convert_date_to_varchar;
	case DB_TYPE_TIMESTAMP:
	  return tp_value_convert_timestamp_to_varchar;
	case DB_TYPE_TIMESTAMPLTZ:
	  return tp_value_convert_timestampltz_to_varchar;
	case DB_TYPE_TIMESTAMPTZ:
	  return tp_value_convert_timestamptz_to_varchar;
	case DB_TYPE_DATETIME:
	  return tp_value_convert_datetime_to_varchar;
	case DB_TYPE_DATETIMELTZ:
	  return tp_value_convert_datetimeltz_to_varchar;
	case DB_TYPE_DATETIMETZ:
	  return tp_value_convert_datetimetz_to_varchar;
	case DB_TYPE_CLOB:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_clob_to_varchar;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_varchar;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_varchar;
	default:
	  break;
	}
      break;
    case DB_TYPE_NCHAR_DEPRECATED:
      switch (src)
	{
	case DB_TYPE_NCHAR_DEPRECATED:
	  return tp_value_convert_incompatible;
	default:
	  break;
	}
      break;
    case DB_TYPE_VARNCHAR_DEPRECATED:
      switch (src)
	{
	case DB_TYPE_VARNCHAR_DEPRECATED:
	  return tp_value_convert_incompatible;
	default:
	  break;
	}
      break;
    case DB_TYPE_BIT:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_char_to_bit;
	case DB_TYPE_BIT:
	  return tp_value_convert_bit_to_bit;
	case DB_TYPE_VARBIT:
	  return tp_value_convert_varbit_to_bit;
	case DB_TYPE_BLOB:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_blob_to_bit;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_bit;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_bit;
	default:
	  break;
	}
      break;
    case DB_TYPE_VARBIT:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_char_to_bit;
	case DB_TYPE_BIT:
	  return tp_value_convert_varbit_to_bit;
	case DB_TYPE_VARBIT:
	  return tp_value_convert_bit_to_bit;
	case DB_TYPE_BLOB:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_blob_to_varbit;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_varbit;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_varbit;
	default:
	  break;
	}
      break;
    case DB_TYPE_TIME:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_short_to_time;
	case DB_TYPE_INTEGER:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_integer_to_time;
	case DB_TYPE_BIGINT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_bigint_to_time;
	case DB_TYPE_FLOAT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_float_to_time;
	case DB_TYPE_DOUBLE:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_double_to_time;
	case DB_TYPE_MONETARY:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_monetary_to_time;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_time_strict : tp_value_convert_char_to_time;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_timestamp_to_time;
	case DB_TYPE_TIMESTAMPTZ:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_timestamptz_to_time;
	case DB_TYPE_DATETIME:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_datetime_to_time;
	case DB_TYPE_DATETIMELTZ:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_datetimeltz_to_time;
	case DB_TYPE_DATETIMETZ:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_datetimetz_to_time;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_time;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_time;
	default:
	  break;
	}
      break;
    case DB_TYPE_DATE:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_date_strict : tp_value_convert_char_to_date;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  return strict ? tp_value_convert_timestamp_to_date_strict : tp_value_convert_timestamp_to_date;
	case DB_TYPE_TIMESTAMPTZ:
	  return strict ? tp_value_convert_timestamptz_to_date_strict : tp_value_convert_timestamptz_to_date;
	case DB_TYPE_DATETIME:
	  return strict ? tp_value_convert_datetime_to_date_strict : tp_value_convert_datetime_to_date;
	case DB_TYPE_DATETIMELTZ:
	  return strict ? tp_value_convert_datetimeltz_to_date_strict : tp_value_convert_datetimeltz_to_date;
	case DB_TYPE_DATETIMETZ:
	  return strict ? tp_value_convert_datetimetz_to_date_strict : tp_value_convert_datetimetz_to_date;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_date;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_date;
	default:
	  break;
	}
      break;
    case DB_TYPE_TIMESTAMP:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_short_to_timestamp;
	case DB_TYPE_INTEGER:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_integer_to_timestamp;
	case DB_TYPE_BIGINT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_bigint_to_timestamp;
	case DB_TYPE_FLOAT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_float_to_timestamp;
	case DB_TYPE_DOUBLE:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_double_to_timestamp;
	case DB_TYPE_MONETARY:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_monetary_to_timestamp;
	case DB_TYPE_NUMERIC:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_numeric_to_timestamp;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_timestamp_strict : tp_value_convert_char_to_timestamp;
	case DB_TYPE_DATE:
	  return strict ? tp_value_convert_date_to_timestamp_strict : tp_value_convert_date_to_timestamp;
	case DB_TYPE_TIMESTAMPLTZ:
	  return strict ? tp_value_convert_timestampltz_to_timestamp_strict : tp_value_convert_timestampltz_to_timestamp;
	case DB_TYPE_TIMESTAMPTZ:
	  return strict ? tp_value_convert_timestamptz_to_timestamp_strict : tp_value_convert_timestamptz_to_timestamp;
	case DB_TYPE_DATETIME:
	  return strict ? tp_value_convert_datetime_to_timestamp_strict : tp_value_convert_datetime_to_timestamp;
	case DB_TYPE_DATETIMELTZ:
	  return strict ? tp_value_convert_datetimeltz_to_timestamp_strict : tp_value_convert_datetimeltz_to_timestamp;
	case DB_TYPE_DATETIMETZ:
	  return strict ? tp_value_convert_datetimetz_to_timestamp_strict : tp_value_convert_datetimetz_to_timestamp;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_timestamp;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_timestamp;
	default:
	  break;
	}
      break;
    case DB_TYPE_TIMESTAMPLTZ:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_short_to_timestampltz;
	case DB_TYPE_INTEGER:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_integer_to_timestampltz;
	case DB_TYPE_BIGINT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_bigint_to_timestampltz;
	case DB_TYPE_FLOAT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_float_to_timestampltz;
	case DB_TYPE_DOUBLE:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_double_to_timestampltz;
	case DB_TYPE_MONETARY:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_monetary_to_timestampltz;
	case DB_TYPE_NUMERIC:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_numeric_to_timestampltz;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_timestampltz_strict : tp_value_convert_char_to_timestampltz;
	case DB_TYPE_DATE:
	  return strict ? tp_value_convert_date_to_timestampltz_strict : tp_value_convert_date_to_timestampltz;
	case DB_TYPE_TIMESTAMP:
	  return strict ? tp_value_convert_timestamp_to_timestampltz_strict : tp_value_convert_timestamp_to_timestampltz;
	case DB_TYPE_TIMESTAMPTZ:
	  return strict ? tp_value_convert_timestamptz_to_timestampltz_strict : tp_value_convert_timestamptz_to_timestampltz;
	case DB_TYPE_DATETIME:
	  return strict ? tp_value_convert_datetime_to_timestampltz_strict : tp_value_convert_datetime_to_timestampltz;
	case DB_TYPE_DATETIMELTZ:
	  return strict ? tp_value_convert_datetimeltz_to_timestampltz_strict : tp_value_convert_datetimeltz_to_timestampltz;
	case DB_TYPE_DATETIMETZ:
	  return strict ? tp_value_convert_datetimetz_to_timestampltz_strict : tp_value_convert_datetimetz_to_timestampltz;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_timestampltz;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_timestampltz;
	default:
	  break;
	}
      break;
    case DB_TYPE_TIMESTAMPTZ:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_short_to_timestamptz;
	case DB_TYPE_INTEGER:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_integer_to_timestamptz;
	case DB_TYPE_BIGINT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_bigint_to_timestamptz;
	case DB_TYPE_FLOAT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_float_to_timestamptz;
	case DB_TYPE_DOUBLE:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_double_to_timestamptz;
	case DB_TYPE_MONETARY:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_monetary_to_timestamptz;
	case DB_TYPE_NUMERIC:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_numeric_to_timestamptz;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_timestamptz_strict : tp_value_convert_char_to_timestamptz;
	case DB_TYPE_DATE:
	  return strict ? tp_value_convert_date_to_timestamptz_strict : tp_value_convert_date_to_timestamptz;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  return strict ? tp_value_convert_timestamp_to_timestamptz_strict : tp_value_convert_timestamp_to_timestamptz;
	case DB_TYPE_DATETIME:
	  return strict ? tp_value_convert_datetime_to_timestamptz_strict : tp_value_convert_datetime_to_timestamptz;
	case DB_TYPE_DATETIMELTZ:
	  return strict ? tp_value_convert_datetimeltz_to_timestamptz_strict : tp_value_convert_datetimeltz_to_timestamptz;
	case DB_TYPE_DATETIMETZ:
	  return strict ? tp_value_convert_datetimetz_to_timestamptz_strict : tp_value_convert_datetimetz_to_timestamptz;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_timestamptz;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_timestamptz;
	default:
	  break;
	}
      break;
    case DB_TYPE_DATETIME:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_datetime_strict : tp_value_convert_char_to_datetime;
	case DB_TYPE_DATE:
	  return strict ? tp_value_convert_date_to_datetime_strict : tp_value_convert_date_to_datetime;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  return strict ? tp_value_convert_timestamp_to_datetime_strict : tp_value_convert_timestamp_to_datetime;
	case DB_TYPE_TIMESTAMPTZ:
	  return strict ? tp_value_convert_timestamptz_to_datetime_strict : tp_value_convert_timestamptz_to_datetime;
	case DB_TYPE_DATETIMELTZ:
	  return strict ? tp_value_convert_datetimeltz_to_datetime_strict : tp_value_convert_datetimeltz_to_datetime;
	case DB_TYPE_DATETIMETZ:
	  return strict ? tp_value_convert_datetimetz_to_datetime_strict : tp_value_convert_datetimetz_to_datetime;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_datetime;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_datetime;
	default:
	  break;
	}
      break;
    case DB_TYPE_DATETIMELTZ:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_datetimeltz_strict : tp_value_convert_char_to_datetimeltz;
	case DB_TYPE_DATE:
	  return strict ? tp_value_convert_date_to_datetimeltz_strict : tp_value_convert_date_to_datetimeltz;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  return strict ? tp_value_convert_timestamp_to_datetimeltz_strict : tp_value_convert_timestamp_to_datetimeltz;
	case DB_TYPE_TIMESTAMPTZ:
	  return strict ? tp_value_convert_timestamptz_to_datetimeltz_strict : tp_value_convert_timestamptz_to_datetimeltz;
	case DB_TYPE_DATETIME:
	  return strict ? tp_value_convert_datetime_to_datetimeltz_strict : tp_value_convert_datetime_to_datetimeltz;
	case DB_TYPE_DATETIMETZ:
	  return strict ? tp_value_convert_datetimetz_to_datetimeltz_strict : tp_value_convert_datetimetz_to_datetimeltz;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_datetimeltz;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_datetimeltz;
	default:
	  break;
	}
      break;
    case DB_TYPE_DATETIMETZ:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_char_to_datetimetz_strict : tp_value_convert_char_to_datetimetz;
	case DB_TYPE_DATE:
	  return strict ? tp_value_convert_date_to_datetimetz_strict : tp_value_convert_date_to_datetimetz;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  return strict ? tp_value_convert_timestamp_to_datetimetz_strict : tp_value_convert_timestamp_to_datetimetz;
	case DB_TYPE_TIMESTAMPTZ:
	  return strict ? tp_value_convert_timestamptz_to_datetimetz_strict : tp_value_convert_timestamptz_to_datetimetz;
	case DB_TYPE_DATETIME:
	  return strict ? tp_value_convert_datetime_to_datetimetz_strict : tp_value_convert_datetime_to_datetimetz;
	case DB_TYPE_DATETIMELTZ:
	  return strict ? tp_value_convert_datetimeltz_to_datetimetz_strict : tp_value_convert_datetimeltz_to_datetimetz;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_datetimetz;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_datetimetz;
	default:
	  break;
	}
      break;
    case DB_TYPE_BLOB:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_char_to_blob;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_bit_to_blob;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_blob;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_blob;
	default:
	  break;
	}
      break;
    case DB_TYPE_CLOB:
      switch (src)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_char_to_clob;
	case DB_TYPE_ENUMERATION:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_enumeration_to_clob;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_clob;
	default:
	  break;
	}
      break;
    case DB_TYPE_ENUMERATION:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_short_to_enumeration;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_integer_to_enumeration;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_bigint_to_enumeration;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_float_to_enumeration;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_double_to_enumeration;
	case DB_TYPE_MONETARY:
	  return tp_value_convert_monetary_to_enumeration;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_numeric_to_enumeration;
	case DB_TYPE_CHAR:
	  return tp_value_convert_char_to_enumeration;
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_varchar_to_enumeration;
	case DB_TYPE_BIT:
	  return tp_value_convert_bit_to_enumeration;
	case DB_TYPE_VARBIT:
	  return tp_value_convert_varbit_to_enumeration;
	case DB_TYPE_TIME:
	  return tp_value_convert_time_to_enumeration;
	case DB_TYPE_DATE:
	  return tp_value_convert_date_to_enumeration;
	case DB_TYPE_TIMESTAMP:
	  return tp_value_convert_timestamp_to_enumeration;
	case DB_TYPE_TIMESTAMPLTZ:
	  return tp_value_convert_timestampltz_to_enumeration;
	case DB_TYPE_TIMESTAMPTZ:
	  return tp_value_convert_timestamptz_to_enumeration;
	case DB_TYPE_DATETIME:
	  return tp_value_convert_datetime_to_enumeration;
	case DB_TYPE_DATETIMELTZ:
	  return tp_value_convert_datetimeltz_to_enumeration;
	case DB_TYPE_DATETIMETZ:
	  return tp_value_convert_datetimetz_to_enumeration;
	case DB_TYPE_BLOB:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_blob_to_enumeration;
	case DB_TYPE_CLOB:
	  return strict ? tp_value_convert_incompatible : tp_value_convert_clob_to_enumeration;
	case DB_TYPE_ENUMERATION:
	  return tp_value_convert_enumeration_to_enumeration;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_scalar_to_enumeration;
	default:
	  break;
	}
      break;
    case DB_TYPE_JSON:
      switch (src)
	{
	case DB_TYPE_SHORT:
	  return tp_value_convert_short_to_json;
	case DB_TYPE_INTEGER:
	  return tp_value_convert_integer_to_json;
	case DB_TYPE_BIGINT:
	  return tp_value_convert_bigint_to_json;
	case DB_TYPE_FLOAT:
	  return tp_value_convert_float_to_json;
	case DB_TYPE_DOUBLE:
	  return tp_value_convert_double_to_json;
	case DB_TYPE_NUMERIC:
	  return tp_value_convert_numeric_to_json;
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  return tp_value_convert_char_to_json;
	case DB_TYPE_JSON:
	  return tp_value_convert_json_validate;
	default:
	  break;
	}
      break;
    case DB_TYPE_SET:
      switch (src)
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  return tp_value_convert_collection<DB_TYPE_SET, MODE == DOMAIN_CONVERT_IMPLICIT>;
	default:
	  break;
	}
      break;
    case DB_TYPE_MULTISET:
      switch (src)
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  return tp_value_convert_collection<DB_TYPE_MULTISET, MODE == DOMAIN_CONVERT_IMPLICIT>;
	default:
	  break;
	}
      break;
    case DB_TYPE_SEQUENCE:
      switch (src)
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  return tp_value_convert_collection<DB_TYPE_SEQUENCE, MODE == DOMAIN_CONVERT_IMPLICIT>;
	default:
	  break;
	}
      break;
    case DB_TYPE_OBJECT:
      switch (src)
	{
#if !defined (SERVER_MODE)
	case DB_TYPE_OBJECT:
	  return tp_value_convert_object_to_object;
#else /* !defined (SERVER_MODE) */
	case DB_TYPE_OBJECT:
	  return tp_value_convert_incompatible;
#endif /* !defined (SERVER_MODE) */
#if !defined (SERVER_MODE)
	case DB_TYPE_OID:
	  return tp_value_convert_oid_to_object;
#endif /* !defined (SERVER_MODE) */
#if !defined (SERVER_MODE)
	case DB_TYPE_VOBJ:
	  return tp_value_convert_vobj_to_object;
#endif /* !defined (SERVER_MODE) */
#if !defined (SERVER_MODE)
	case DB_TYPE_POINTER:
	  return tp_value_convert_pointer_to_object;
#endif /* !defined (SERVER_MODE) */
	default:
	  break;
	}
      break;
    case DB_TYPE_OID:
      switch (src)
	{
#if !defined (SERVER_MODE)
	case DB_TYPE_OID:
	  return tp_value_convert_oid_to_oid;
#else /* !defined (SERVER_MODE) */
	case DB_TYPE_OID:
	  return tp_value_convert_incompatible;
#endif /* !defined (SERVER_MODE) */
	default:
	  break;
	}
      break;
    case DB_TYPE_VOBJ:
      switch (src)
	{
#if !defined (SERVER_MODE)
	case DB_TYPE_OBJECT:
	  return tp_value_convert_object_to_vobj;
#endif /* !defined (SERVER_MODE) */
	case DB_TYPE_OID:
	  return tp_value_convert_oid_to_vobj;
	case DB_TYPE_VOBJ:
	  return tp_value_convert_vobj_to_vobj;
	default:
	  break;
	}
      break;
    case DB_TYPE_VARIABLE:
      switch (src)
	{
	case DB_TYPE_VARIABLE:
	  return tp_value_convert_incompatible;
	default:
	  break;
	}
      break;
    case DB_TYPE_SUB:
      switch (src)
	{
	case DB_TYPE_SUB:
	  return tp_value_convert_incompatible;
	default:
	  break;
	}
      break;
    case DB_TYPE_DB_VALUE:
      switch (src)
	{
	case DB_TYPE_DB_VALUE:
	  return tp_value_convert_incompatible;
	default:
	  break;
	}
      break;
    case DB_TYPE_MIDXKEY:
      switch (src)
	{
	case DB_TYPE_MIDXKEY:
	  return tp_value_convert_incompatible;
	default:
	  break;
	}
      break;
    case DB_TYPE_TABLE:
      switch (src)
	{
	case DB_TYPE_TABLE:
	  return tp_value_convert_incompatible;
	default:
	  break;
	}
      break;
    default:
      break;
    }
  return src == dst ? nullptr : tp_value_convert_incompatible;
}

/*
 * tp_value_find_converter () - the converter of a value of type src_type into desired_domain in a mode
 *   return: the converter; nullptr for the same type with nothing to convert; tp_value_convert_incompatible for a
 *	     pair that does not convert, a missing domain or a type out of range
 */
TP_VALUE_CONVERTER
tp_value_find_converter (DB_TYPE src_type, const TP_DOMAIN *desired_domain, DOMAIN_CONVERT_MODE mode)
{
  if (desired_domain == nullptr || src_type < DB_TYPE_NULL || src_type > DB_TYPE_LAST)
    {
      return tp_value_convert_incompatible;
    }
  const DB_TYPE dst_type = TP_DOMAIN_TYPE (desired_domain);
  if (dst_type < DB_TYPE_NULL || dst_type > DB_TYPE_LAST)
    {
      return tp_value_convert_incompatible;
    }
  switch (mode)
    {
    case DOMAIN_CONVERT_ASSIGN:
      return tp_value_find_converter<DOMAIN_CONVERT_ASSIGN> (src_type, dst_type);
    case DOMAIN_CONVERT_IMPLICIT:
      return tp_value_find_converter<DOMAIN_CONVERT_IMPLICIT> (src_type, dst_type);
    case DOMAIN_CONVERT_COMPARE:
      return tp_value_find_converter<DOMAIN_CONVERT_COMPARE> (src_type, dst_type);
    case DOMAIN_CONVERT_OPERAND:
      return tp_value_find_converter<DOMAIN_CONVERT_OPERAND> (src_type, dst_type);
    default:
      return tp_value_convert_incompatible;
    }
}
