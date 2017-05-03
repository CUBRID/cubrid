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
 * db_macro.i - API functions related to db_make and DB_GET
 *
 */
 
#ifndef _DB_MACRO_I_
#define _DB_MACRO_I_

#include "porting.h"
#include "dbtype.h"

#ifdef SERVER_MODE
#define DB_MACRO_INLINE STATIC_INLINE
#else
#define DB_MACRO_INLINE
#endif

/*
 * db_make_null() -
 * return :
 * value(out) :
 */
DB_MACRO_INLINE int
db_make_null (DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_NULL;
  value->domain.general_info.is_null = 1;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_int() -
 * return :
 * value(out) :
 * num(in):
 */
DB_MACRO_INLINE int
db_make_int (DB_VALUE * value, const int num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_INTEGER;
  value->data.i = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_short() -
 * return :
 * value(out) :
 * num(in) :
 */
DB_MACRO_INLINE int
db_make_short (DB_VALUE * value, const short num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_SHORT;
  value->data.sh = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_bigint() -
 * return :
 * value(out) :
 * num(in) :
 */
DB_MACRO_INLINE int
db_make_bigint (DB_VALUE * value, const DB_BIGINT num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_BIGINT;
  value->data.bigint = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_float() -
 * return :
 * value(out) :
 * num(in):
 */
DB_MACRO_INLINE int
db_make_float (DB_VALUE * value, const float num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_FLOAT;
  value->data.f = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_double() -
 * return :
 * value(out) :
 * num(in):
 */
DB_MACRO_INLINE int
db_make_double (DB_VALUE * value, const double num)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DOUBLE;
  value->data.d = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_numeric() -
 * return :
 * value(out) :
 * num(in):
 * precision(in):
 * scale(in):
 */
DB_MACRO_INLINE int
db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_NUMERIC, precision, scale);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (num)
    {
      value->domain.general_info.is_null = 0;
      memcpy (value->data.num.d.buf, num, DB_NUMERIC_BUF_SIZE);
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  return error;
}

/*
 * db_make_string_copy() - alloc buffer and copy str into the buffer.
 *                         need_clear will set as true.
 * return :
 * value(out) :
 * str(in):
 */
DB_MACRO_INLINE int
db_make_string_copy (DB_VALUE * value, const char *str)
{
  int error;
  DB_VALUE tmp_value;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_make_string (&tmp_value, str);
  if (error == NO_ERROR)
    {
      error = pr_clone_value (&tmp_value, value);
    }

  return error;
}

/*
 * db_make_char() -
 * return :
 * value(out) :
 * char_length(in):
 * str(in):
 * char_str_byte_size(in):
 */
DB_MACRO_INLINE int
db_make_char (DB_VALUE * value, const int char_length, const DB_C_CHAR str, const int char_str_byte_size,
	      const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_CHAR, char_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, char_str_byte_size);
    }

  return error;
}

/*
 * db_make_varchar() -
 * return :
 * value(out) :
 * max_char_length(in):
 * str(in):
 * char_str_byte_size(in):
 */
DB_MACRO_INLINE int
db_make_varchar (DB_VALUE * value, const int max_char_length, const DB_C_CHAR str, const int char_str_byte_size,
		 const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARCHAR, max_char_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, char_str_byte_size);
    }

  return error;
}

/*
 * db_make_nchar() -
 * return :
 * value(out) :
 * nchar_length(in):
 * str(in):
 * nchar_str_byte_size(in):
 */
DB_MACRO_INLINE int
db_make_nchar (DB_VALUE * value, const int nchar_length, const DB_C_NCHAR str, const int nchar_str_byte_size,
	       const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_NCHAR, nchar_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, nchar_str_byte_size);
    }

  return error;
}

/*
 * db_make_varnchar() -
 * return :
 * value(out) :
 * max_nchar_length(in):
 * str(in):
 * nchar_str_byte_size(in):
 */
DB_MACRO_INLINE int
db_make_varnchar (DB_VALUE * value, const int max_nchar_length, const DB_C_NCHAR str, const int nchar_str_byte_size,
		  const int codeset, const int collation_id)
{
  int error;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARNCHAR, max_nchar_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, codeset, collation_id, str, nchar_str_byte_size);
    }

  return error;
}

/*
 * db_make_object() -
 * return :
 * value(out) :
 * obj(in):
 */
DB_MACRO_INLINE int
db_make_object (DB_VALUE * value, DB_OBJECT * obj)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_OBJECT;
  value->data.op = obj;
  if (obj)
    {
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_collection() -
 * return :
 * value(out) :
 * col(in):
 */
DB_MACRO_INLINE int
db_make_collection (DB_VALUE * value, DB_COLLECTION * col)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  /* Rather than being DB_TYPE_COLLECTION, the value type is taken from the base type of the collection. */
  if (col == NULL)
    {
      value->domain.general_info.type = DB_TYPE_SEQUENCE;	/* undefined */
      value->data.set = NULL;
      value->domain.general_info.is_null = 1;
    }
  else
    {
      value->domain.general_info.type = db_col_type (col);
      value->data.set = col;
      /* note, we have been testing set->set for non-NULL here in order to set the is_null flag, this isn't
       * appropriate, the set pointer can be NULL if the set has been swapped out.The existance of a set handle alone
       * determines the nullness of the value.  Actually, the act of calling db_col_type above will have resulted in a
       * re-fetch of the referenced set if it had been swapped out. */
      value->domain.general_info.is_null = 0;
    }
  value->need_clear = false;

  return error;
}

/*
 * db_make_midxkey() -
 * return :
 * value(out) :
 * midxkey(in):
 */
DB_MACRO_INLINE int
db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey)
{
  int error = NO_ERROR;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_MIDXKEY;

  if (midxkey == NULL)
    {
      value->domain.general_info.is_null = 1;
      value->data.midxkey.ncolumns = -1;
      value->data.midxkey.domain = NULL;
      value->data.midxkey.size = 0;
      value->data.midxkey.buf = NULL;
      value->data.midxkey.min_max_val.position = -1;
      value->data.midxkey.min_max_val.type = MIN_COLUMN;
    }
  else
    {
      value->domain.general_info.is_null = 0;
      value->data.midxkey = *midxkey;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_pointer() -
 * return :
 * value(out) :
 * ptr(in):
 */
DB_MACRO_INLINE int
db_make_pointer (DB_VALUE * value, void *ptr)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_POINTER;
  value->data.p = ptr;
  if (ptr)
    {
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_time() -
 * return :
 * value(out) :
 * hour(in):
 * min(in):
 * sec(in):
 */
DB_MACRO_INLINE int
db_make_time (DB_VALUE * value, const int hour, const int min, const int sec)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIME;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_time_encode (&value->data.time, hour, min, sec);
}

/*
 * db_make_timetz() -
 * return :
 * value(out) :
 * hour(in):
 * min(in):
 * sec(in):
 */
DB_MACRO_INLINE int
db_make_timetz (DB_VALUE * value, const DB_TIMETZ * timetz_value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMETZ;
  value->need_clear = false;
  if (timetz_value)
    {
      value->data.timetz.time = timetz_value->time;
      value->data.timetz.tz_id = timetz_value->tz_id;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  return NO_ERROR;
}

/*
 * db_make_timeltz() -
 * return :
 * value(out) :
 * hour(in):
 * min(in):
 * sec(in):
 */
DB_MACRO_INLINE int
db_make_timeltz (DB_VALUE * value, const DB_TIME * time_value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMELTZ;
  value->need_clear = false;
  if (time_value)
    {
      value->data.time = *time_value;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  return NO_ERROR;
}

/*
 * db_make_date() -
 * return :
 * value(out):
 * mon(in):
 * day(in):
 * year(in):
 */
DB_MACRO_INLINE int
db_make_date (DB_VALUE * value, const int mon, const int day, const int year)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATE;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_date_encode (&value->data.date, mon, day, year);
}

/*
 * db_make_monetary() -
 * return :
 * value(out):
 * type(in):
 * amount(in):
 */
DB_MACRO_INLINE int
db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount)
{
  int error;
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  /* check for valid currency type don't put default case in the switch!!! */
  error = ER_INVALID_CURRENCY_TYPE;
  switch (type)
    {
    case DB_CURRENCY_DOLLAR:
    case DB_CURRENCY_YEN:
    case DB_CURRENCY_WON:
    case DB_CURRENCY_TL:
    case DB_CURRENCY_BRITISH_POUND:
    case DB_CURRENCY_CAMBODIAN_RIEL:
    case DB_CURRENCY_CHINESE_RENMINBI:
    case DB_CURRENCY_INDIAN_RUPEE:
    case DB_CURRENCY_RUSSIAN_RUBLE:
    case DB_CURRENCY_AUSTRALIAN_DOLLAR:
    case DB_CURRENCY_CANADIAN_DOLLAR:
    case DB_CURRENCY_BRASILIAN_REAL:
    case DB_CURRENCY_ROMANIAN_LEU:
    case DB_CURRENCY_EURO:
    case DB_CURRENCY_SWISS_FRANC:
    case DB_CURRENCY_DANISH_KRONE:
    case DB_CURRENCY_NORWEGIAN_KRONE:
    case DB_CURRENCY_BULGARIAN_LEV:
    case DB_CURRENCY_VIETNAMESE_DONG:
    case DB_CURRENCY_CZECH_KORUNA:
    case DB_CURRENCY_POLISH_ZLOTY:
    case DB_CURRENCY_SWEDISH_KRONA:
    case DB_CURRENCY_CROATIAN_KUNA:
    case DB_CURRENCY_SERBIAN_DINAR:
      error = NO_ERROR;		/* it's a type we expect */
      break;
    default:
      break;
    }

  if (error != NO_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, type);
      return error;
    }

  value->domain.general_info.type = DB_TYPE_MONETARY;
  value->data.money.type = type;
  value->data.money.amount = amount;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return error;
}

/*
 * db_make_timestamp() -
 * return :
 * value(out):
 * timeval(in):
 */
DB_MACRO_INLINE int
db_make_timestamp (DB_VALUE * value, const DB_TIMESTAMP timeval)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMESTAMP;
  value->data.utime = timeval;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_timestampltz() -
 * return :
 * value(out):
 * timeval(in):
 */
DB_MACRO_INLINE int
db_make_timestampltz (DB_VALUE * value, const DB_TIMESTAMP ts_val)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMESTAMPLTZ;
  value->data.utime = ts_val;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_timestamptz() -
 * return :
 * value(out):
 * timeval(in):
 */
DB_MACRO_INLINE int
db_make_timestamptz (DB_VALUE * value, const DB_TIMESTAMPTZ * ts_tz_val)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIMESTAMPTZ;
  if (ts_tz_val)
    {
      value->data.timestamptz = *ts_tz_val;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_datetime() -
 * return :
 * value(out):
 * date(in):
 */
DB_MACRO_INLINE int
db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATETIME;
  if (datetime)
    {
      value->data.datetime = *datetime;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_datetimeltz() -
 * return :
 * value(out):
 * date(in):
 */
DB_MACRO_INLINE int
db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATETIMELTZ;
  if (datetime)
    {
      value->data.datetime = *datetime;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_datetimetz() -
 * return :
 * value(out):
 * date(in):
 */
DB_MACRO_INLINE int
db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATETIMETZ;
  if (datetimetz)
    {
      value->data.datetimetz = *datetimetz;
      value->domain.general_info.is_null = 0;
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_enumeration() -
 * return :
 * value(out):
 * index(in):
 * str(in):
 * size(in):
 * codeset(in):
 * collation_id(in):
 */
DB_MACRO_INLINE int
db_make_enumeration (DB_VALUE * value, unsigned short index, DB_C_CHAR str, int size, unsigned char codeset,
		     const int collation_id)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_ENUMERATION;
  value->data.enumeration.short_val = index;
  value->data.enumeration.str_val.info.codeset = codeset;
  value->domain.char_info.collation_id = collation_id;
  value->data.enumeration.str_val.info.style = MEDIUM_STRING;
  value->data.ch.info.is_max_string = false;
  value->data.ch.info.compressed_need_clear = false;
  value->data.ch.medium.compressed_buf = NULL;
  value->data.ch.medium.compressed_size = 0;
  value->data.enumeration.str_val.medium.size = size;
  value->data.enumeration.str_val.medium.buf = str;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_error() -
 * return :
 * value(out):
 * errcode(in):
 */
DB_MACRO_INLINE int
db_make_error (DB_VALUE * value, const int errcode)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  assert (errcode != NO_ERROR);

  value->domain.general_info.type = DB_TYPE_ERROR;
  value->data.error = errcode;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_method_error() -
 * return :
 * value(out):
 * errcode(in):
 * errmsg(in);
 */
DB_MACRO_INLINE int
db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_ERROR;
  value->data.error = errcode;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

#if !defined(SERVER_MODE)
  if (obj_Method_error_msg)
    {
      free (obj_Method_error_msg);	/* free old last error */
    }
  obj_Method_error_msg = NULL;
  if (errmsg)
    {
      obj_Method_error_msg = strdup (errmsg);
    }
#endif

  return NO_ERROR;
}

/*
 * db_make_resultset() -
 * return :
 * value(out):
 * handle(in):
 */
DB_MACRO_INLINE int
db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_RESULTSET;
  value->data.rset = handle;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}


/*
 * db_get_int() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_int (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_INTEGER);

  return value->data.i;
}

/*
 * db_get_short() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_C_SHORT
db_get_short (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_SHORT);

  return value->data.sh;
}

/*
 * db_get_bigint() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_BIGINT
db_get_bigint (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_BIGINT);

  return value->data.bigint;
}

/*
 * db_get_float() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_C_FLOAT
db_get_float (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_FLOAT);

  return value->data.f;
}

/*
 * db_get_double() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_C_DOUBLE
db_get_double (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DOUBLE);

  return value->data.d;
}

/*
 * db_get_object() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_OBJECT *
db_get_object (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      return value->data.op;
    }
}

/*
 * db_get_set() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_COLLECTION *
db_get_set (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_SET || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_MULTISET
	      || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_SEQUENCE || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_VOBJ);

      return value->data.set;
    }
}

/*
 * db_get_midxkey() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_MIDXKEY *
db_get_midxkey (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* This one needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_MIDXKEY);
      return (DB_MIDXKEY *) (&(value->data.midxkey));
    }
}

/*
 * db_get_pointer() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_C_POINTER
db_get_pointer (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_POINTER);
      return value->data.p;
    }
}

/*
 * db_get_time() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_TIME *
db_get_time (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIME || value->domain.general_info.type == DB_TYPE_TIMELTZ);

  return ((DB_TIME *) (&value->data.time));
}

/*
 * db_get_timetz() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_TIMETZ *
db_get_timetz (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIMETZ);

  return ((DB_TIMETZ *) (&value->data.timetz));
}

/*
 * db_get_timestamp() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_TIMESTAMP *
db_get_timestamp (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIMESTAMP
	  || value->domain.general_info.type == DB_TYPE_TIMESTAMPLTZ);

  return ((DB_TIMESTAMP *) (&value->data.utime));
}

/*
 * db_get_timestamptz() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_TIMESTAMPTZ *
db_get_timestamptz (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIMESTAMPTZ);

  return ((DB_TIMESTAMPTZ *) (&value->data.timestamptz));
}

/*
 * db_get_datetime() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_DATETIME *
db_get_datetime (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DATETIME
	  || value->domain.general_info.type == DB_TYPE_DATETIMELTZ);

  return ((DB_DATETIME *) (&value->data.datetime));
}

/*
 * db_get_datetimetz() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_DATETIMETZ *
db_get_datetimetz (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DATETIMETZ);

  return ((DB_DATETIMETZ *) (&value->data.datetimetz));
}

/*
 * db_get_date() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_DATE *
db_get_date (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_DATE);

  return ((DB_DATE *) (&value->data.date));
}

/*
 * db_get_monetary() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_MONETARY *
db_get_monetary (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_MONETARY);

  return ((DB_MONETARY *) (&value->data.money));
}

/*
 * db_get_error() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_error (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_ERROR);

  return value->data.error;
}

/*
 * db_get_elo() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_ELO *
db_get_elo (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else if (value->data.elo.type == ELO_NULL)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_ELO || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_CLOB
	      || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_BLOB);

      return (DB_ELO *) (&value->data.elo);
    }
}

/*
 * db_get_numeric() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_C_NUMERIC
db_get_numeric (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      /* Needs to be checked !! */
      assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_NUMERIC);
      return (DB_C_NUMERIC) value->data.num.d.buf;
    }
}

/*
 * db_get_nchar() -
 * return :
 * value(in):
 * length(out):
 */
DB_MACRO_INLINE DB_C_NCHAR
db_get_nchar (const DB_VALUE * value, int *length)
{
  return db_get_char (value, length);
}

/*
 * db_get_string_size() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_string_size (const DB_VALUE * value)
{
  int size = 0;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      size = value->data.ch.sm.size;
      break;
    case MEDIUM_STRING:
      size = value->data.ch.medium.size;
      break;
    case LARGE_STRING:
      /* Currently not implemented */
      size = 0;
      break;
    }

  /* Convert the number of bits to the number of bytes */
  if (value->domain.general_info.type == DB_TYPE_BIT || value->domain.general_info.type == DB_TYPE_VARBIT)
    {
      size = (size + 7) / 8;
    }

  return size;
}

/*
 * db_get_resultset() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_RESULTSET
db_get_resultset (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  /* Needs to be checked !! */
  assert (value->domain.general_info.type == DB_TYPE_RESULTSET);

  return value->data.rset;
}

/*
 * db_get_enum_string () -
 * return :
 * value(in):
 */
DB_MACRO_INLINE char *
db_get_enum_string (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif
  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  return value->data.enumeration.str_val.medium.buf;
}

/*
 * db_get_enum_short () -
 * return :
 * value(in):
 */
DB_MACRO_INLINE unsigned short
db_get_enum_short (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif
  assert (value->domain.general_info.type == DB_TYPE_ENUMERATION);

  return value->data.enumeration.short_val;
}

/*
 * db_get_enum_string_size () -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_enum_string_size (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif
  assert (value->domain.general_info.type == DB_TYPE_ENUMERATION);

  return value->data.enumeration.str_val.medium.size;
}
#endif
