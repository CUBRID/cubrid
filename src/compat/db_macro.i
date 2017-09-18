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
 * db_macro.i - API/inlined functions related to db_make and DB_GET
 *
 */
 
#ident "$Id$"

#ifndef _DB_MACRO_I_
#define _DB_MACRO_I_

#include "dbtype.h"

#ifdef SERVER_MODE
#define DB_MACRO_INLINE STATIC_INLINE
#else
#define DB_MACRO_INLINE
#endif
 
/*
 * db_get_int() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_int (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
  assert (value->domain.general_info.type == DB_TYPE_INTEGER);

  return value->data.i;
}

/*
 * db_get_short() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE short
db_get_short (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
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
  CHECK_1ARG_ZERO (value);
  assert (value->domain.general_info.type == DB_TYPE_BIGINT);

  return value->data.bigint;
}

/*
 * db_get_string() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE char *
db_get_string (const DB_VALUE * value)
{
  char *str = NULL;
  CHECK_1ARG_NULL (value);

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      str = (char *) value->data.ch.sm.buf;
      break;
    case MEDIUM_STRING:
      str = value->data.ch.medium.buf;
      break;
    case LARGE_STRING:
      /* Currently not implemented */
      str = NULL;
      break;
    }

  return str;
}
/*
 * db_get_float() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE float
db_get_float (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
  assert (value->domain.general_info.type == DB_TYPE_FLOAT);

  return value->data.f;
}

/*
 * db_get_double() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE double
db_get_double (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
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
  CHECK_1ARG_NULL (value);

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
DB_MACRO_INLINE DB_SET *
db_get_set (const DB_VALUE * value)
{
  CHECK_1ARG_NULL (value);

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
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
  CHECK_1ARG_NULL (value);

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      return (DB_MIDXKEY *) (&(value->data.midxkey));
    }
}

/*
 * db_get_pointer() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE void *
db_get_pointer (const DB_VALUE * value)
{
  CHECK_1ARG_NULL (value);

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
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
  CHECK_1ARG_NULL (value);
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
  CHECK_1ARG_NULL (value);

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
  CHECK_1ARG_NULL (value);
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
  CHECK_1ARG_NULL (value);
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
  CHECK_1ARG_NULL (value);
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
  CHECK_1ARG_NULL (value);

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
  CHECK_1ARG_NULL (value);
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
  CHECK_1ARG_NULL (value);
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
  CHECK_1ARG_ZERO (value);
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
  CHECK_1ARG_NULL (value);

  if (value->domain.general_info.is_null)
    {
      return NULL;
    }
  else if (value->data.elo.type == ELO_NULL)
    {
      return NULL;
    }
  else
    {
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
  CHECK_1ARG_ZERO (value);

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      return (DB_C_NUMERIC) value->data.num.d.buf;
    }
}

/*
 * db_get_bit() -
 * return :
 * value(in):
 * length(out):
 */
DB_MACRO_INLINE char *
db_get_bit (const DB_VALUE * value, int *length)
{
  char *str = NULL;

  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);

  if (value->domain.general_info.is_null)
    {
      return NULL;
    }

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      {
	*length = value->data.ch.sm.size;
	str = (char *) value->data.ch.sm.buf;
      }
      break;
    case MEDIUM_STRING:
      {
	*length = value->data.ch.medium.size;
	str = value->data.ch.medium.buf;
      }
      break;
    case LARGE_STRING:
      {
	/* Currently not implemented */
	*length = 0;
	str = NULL;
      }
      break;
    }

  return str;
}
/*
 * db_get_char() -
 * return :
 * value(in):
 * length(out):
 */
DB_MACRO_INLINE char *
db_get_char (const DB_VALUE * value, int *length)
{
  char *str = NULL;

  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      {
	str = (char *) value->data.ch.sm.buf;
	intl_char_count ((unsigned char *) str, value->data.ch.sm.size,
			 (INTL_CODESET) value->data.ch.info.codeset, length);
      }
      break;
    case MEDIUM_STRING:
      {
	str = value->data.ch.medium.buf;
	intl_char_count ((unsigned char *) str, value->data.ch.medium.size,
			 (INTL_CODESET) value->data.ch.info.codeset, length);
      }
      break;
    case LARGE_STRING:
      {
	/* Currently not implemented */
	str = NULL;
	*length = 0;
      }
      break;
    }

  return str;
}

/*
 * db_get_nchar() -
 * return :
 * value(in):
 * length(out):
 */
DB_MACRO_INLINE char *
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

  CHECK_1ARG_ZERO (value);

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
 * db_get_enum_short () -
 * return :
 * value(in):
 */
DB_MACRO_INLINE short
db_get_enum_short (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
  assert (value->domain.general_info.type == DB_TYPE_ENUMERATION);

  return value->data.enumeration.short_val;
}

/*
 * db_get_enum_string () -
 * return :
 * value(in):
 */
DB_MACRO_INLINE char *
db_get_enum_string (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  return value->data.enumeration.str_val.medium.buf;
}

/*
 * db_get_enum_string_size () -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_enum_string_size (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
  assert (value->domain.general_info.type == DB_TYPE_ENUMERATION);

  return value->data.enumeration.str_val.medium.size;
}

/*
 * db_get_method_error_msg() -
 * return :
 */
DB_MACRO_INLINE char *
db_get_method_error_msg (void)
{
#if !defined(SERVER_MODE)
  return obj_Method_error_msg;
#else
  return NULL;
#endif
}

/*
 * db_get_resultset() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE DB_RESULTSET
db_get_resultset (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO (value);
  assert (value->domain.general_info.type == DB_TYPE_RESULTSET);

  return value->data.rset;
}
/*
 * db_get_string_codeset() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_string_codeset (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);

  return (int) value->data.ch.info.codeset;
}

/*
 * db_get_string_collation() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_string_collation (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);

  return value->domain.char_info.collation_id;
}
/*
 * db_get_enum_codeset() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_enum_codeset (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);

  return value->data.enumeration.str_val.info.codeset;
}

/*
 * db_get_enum_collation() -
 * return :
 * value(in):
 */
DB_MACRO_INLINE int
db_get_enum_collation (const DB_VALUE * value)
{
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);

  return value->domain.char_info.collation_id;
}

#endif          /* _DB_MACRO_I_*/