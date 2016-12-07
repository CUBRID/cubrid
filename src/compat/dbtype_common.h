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


/* dbtype_common.h - Definitions related to the memory representations of database
 * attribute values. This is an application interface file. It should contain
 * only definitions available to CUBRID customer applications.
 * This will probably be the new interface for dbtype.h actually. Need to discuss
 * how should we name this.
 */

#ifndef _DBTYPE_COMMON_H_
#define _DBTYPE_COMMON_H_

#ident "$Id$"

#include "config.h"

#include "porting.h"
#include "error_manager.h"
#include "intl_support.h"
#include "dbtype.h"

/* MACROS FOR ERROR CHECKING */
/* These should be used at the start of every db_ function so we can check
   various validations before executing. */
/* CHECK CONNECT */
#define CHECK_CONNECT_VOID()                                            \
  do {                                                                  \
    if (db_Connect_status != DB_CONNECTION_STATUS_CONNECTED)            \
    {                                                                   \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_CONNECT, 0);   \
      return;                                                           \
    }                                                                   \
  } while (0)

#define CHECK_CONNECT_AND_RETURN_EXPR(return_expr_)                     \
  do {                                                                  \
    if (db_Connect_status != DB_CONNECTION_STATUS_CONNECTED)            \
    {                                                                   \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_CONNECT, 0);   \
      return (return_expr_);                                            \
    }                                                                   \
  } while (0)

#define CHECK_CONNECT_ERROR()     \
  CHECK_CONNECT_AND_RETURN_EXPR((DB_TYPE) ER_OBJ_NO_CONNECT)

#define CHECK_CONNECT_NULL()      \
  CHECK_CONNECT_AND_RETURN_EXPR(NULL)

#define CHECK_CONNECT_ZERO()      \
  CHECK_CONNECT_AND_RETURN_EXPR(0)

#define CHECK_CONNECT_ZERO_TYPE(TYPE)      \
  CHECK_CONNECT_AND_RETURN_EXPR((TYPE)0)

#define CHECK_CONNECT_MINUSONE()  \
  CHECK_CONNECT_AND_RETURN_EXPR(-1)

#define CHECK_CONNECT_FALSE()     \
  CHECK_CONNECT_AND_RETURN_EXPR(false)

/* CHECK MODIFICATION */
#define CHECK_MODIFICATION_VOID()                                            \
  do {                                                                       \
    if (db_Disable_modifications) {                                          \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0);   \
      return;                                                                \
    }                                                                        \
  } while (0)

#define CHECK_MODIFICATION_AND_RETURN_EXPR(return_expr_)                     \
  if (db_Disable_modifications) {                                            \
    er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0);     \
    return (return_expr_);                                                   \
  }

#define CHECK_MODIFICATION_ERROR()   \
  CHECK_MODIFICATION_AND_RETURN_EXPR(ER_DB_NO_MODIFICATIONS)

#define CHECK_MODIFICATION_NULL()   \
  CHECK_MODIFICATION_AND_RETURN_EXPR(NULL)

#define CHECK_MODIFICATION_MINUSONE() \
  CHECK_MODIFICATION_AND_RETURN_EXPR(-1)

#ifndef CHECK_MODIFICATION_NO_RETURN
#if defined (SA_MODE)
#define CHECK_MODIFICATION_NO_RETURN(error) \
  error = NO_ERROR;
#else /* SA_MODE */
#define CHECK_MODIFICATION_NO_RETURN(error)                                  \
  if (db_Disable_modifications) {                                            \
    er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0);     \
    er_log_debug (ARG_FILE_LINE, "db_Disable_modification = %d\n",           \
		  db_Disable_modifications);                                  \
    error = ER_DB_NO_MODIFICATIONS;                                          \
  } else {                                                                   \
    error = NO_ERROR;                                                        \
  }
#endif /* !SA_MODE */
#endif /* CHECK_MODIFICATION_NO_RETURN */

/* Argument checking macros */
#define CHECK_1ARG_RETURN_EXPR(obj, expr)                                      \
  do {                                                                         \
    if((obj) == NULL) {                                                        \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_2ARGS_RETURN_EXPR(obj1, obj2, expr)                              \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL) {                                     \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, expr)                        \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL || (obj3) == NULL) {                   \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_1ARG_NULL(obj)        \
  CHECK_1ARG_RETURN_EXPR(obj, NULL)

#define CHECK_2ARGS_NULL(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2,NULL)

#define CHECK_3ARGS_NULL(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1,obj2,obj3,NULL)

#define CHECK_1ARG_FALSE(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,false)

#define CHECK_1ARG_TRUE(obj)   \
  CHECK_1ARG_RETURN_EXPR(obj, true)

#define CHECK_1ARG_ERROR(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ERROR_WITH_TYPE(obj, TYPE)  \
  CHECK_1ARG_RETURN_EXPR(obj,(TYPE)ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_MINUSONE(obj) \
  CHECK_1ARG_RETURN_EXPR(obj,-1)

#define CHECK_2ARGS_ERROR(obj1, obj2)   \
  CHECK_2ARGS_RETURN_EXPR(obj1, obj2, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_3ARGS_ERROR(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ZERO(obj)     \
  CHECK_1ARG_RETURN_EXPR(obj, 0)

#define CHECK_1ARG_ZERO_WITH_TYPE(obj1, RETURN_TYPE)     \
  CHECK_1ARG_RETURN_EXPR(obj1, (RETURN_TYPE) 0)

#define CHECK_2ARGS_ZERO(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2, 0)

#define CHECK_1ARG_UNKNOWN(obj1)        \
  CHECK_1ARG_RETURN_EXPR(obj1, DB_TYPE_UNKNOWN)


INLINE int db_get_int (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_SHORT db_get_short (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_BIGINT db_get_bigint (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_CHAR db_get_string (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_FLOAT db_get_float (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_DOUBLE db_get_double (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_OBJECT *db_get_object (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_COLLECTION *db_get_set (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_POINTER db_get_pointer (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_TIME *db_get_time (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_TIMETZ *db_get_timetz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_TIMESTAMPTZ *db_get_timestamptz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));;
INLINE DB_DATETIME *db_get_datetime (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_DATETIMETZ *db_get_datetimetz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_DATE *db_get_date (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_MONETARY *db_get_monetary (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE int db_get_error (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_ELO *db_get_elo (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_NUMERIC db_get_numeric (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_BIT db_get_bit (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_CHAR db_get_char (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
INLINE DB_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
INLINE int db_get_string_size (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE int db_get_string_codeset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE int db_get_string_collation (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE DB_RESULTSET db_get_resultset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE int db_get_enum_codeset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
INLINE int db_get_enum_collation (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));

/*
 * db_get_int() -
 * return :
 * value(in):
 */
INLINE int
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
INLINE DB_C_SHORT
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
INLINE DB_BIGINT
db_get_bigint (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_BIGINT);

  return value->data.bigint;
}

/*
 * db_get_string() -
 * return :
 * value(in):
 */
INLINE DB_C_CHAR
db_get_string (const DB_VALUE * value)
{
  char *str = NULL;
  DB_TYPE type;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);

  /* Needs to be checked !! */
  assert (type == DB_TYPE_VARCHAR || type == DB_TYPE_CHAR || type == DB_TYPE_VARNCHAR
	  || type == DB_TYPE_NCHAR || type == DB_TYPE_VARBIT || type == DB_TYPE_BIT);

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
INLINE DB_C_FLOAT
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
INLINE DB_C_DOUBLE
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
INLINE DB_OBJECT *
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
INLINE DB_COLLECTION *
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
INLINE DB_MIDXKEY *
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
INLINE DB_C_POINTER
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
INLINE DB_TIME *
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
INLINE DB_TIMETZ *
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
INLINE DB_TIMESTAMP *
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
INLINE DB_TIMESTAMPTZ *
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
INLINE DB_DATETIME *
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
INLINE DB_DATETIMETZ *
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
INLINE DB_DATE *
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
INLINE DB_MONETARY *
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
INLINE int
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
INLINE DB_ELO *
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
INLINE DB_C_NUMERIC
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
 * db_get_bit() -
 * return :
 * value(in):
 * length(out):
 */
INLINE DB_C_BIT
db_get_bit (const DB_VALUE * value, int *length)
{
  char *str = NULL;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);
#endif

  if (value->domain.general_info.is_null)
    {
      return NULL;
    }

  /* Needs to be checked !! */
  assert (DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_BIT || DB_VALUE_DOMAIN_TYPE (value) == DB_TYPE_VARBIT);

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
INLINE DB_C_CHAR
db_get_char (const DB_VALUE * value, int *length)
{
  char *str = NULL;
  DB_TYPE type;

#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);

  assert (type == DB_TYPE_VARCHAR || type == DB_TYPE_CHAR || type == DB_TYPE_VARNCHAR
	  || type == DB_TYPE_NCHAR || type == DB_TYPE_VARBIT || type == DB_TYPE_BIT);

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      {
	str = (char *) value->data.ch.sm.buf;
	intl_char_count ((unsigned char *) str, value->data.ch.sm.size, (INTL_CODESET) value->data.ch.info.codeset,
			 length);
      }
      break;
    case MEDIUM_STRING:
      {
	str = value->data.ch.medium.buf;
	intl_char_count ((unsigned char *) str, value->data.ch.medium.size, (INTL_CODESET) value->data.ch.info.codeset,
			 length);
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
INLINE DB_C_NCHAR
db_get_nchar (const DB_VALUE * value, int *length)
{
  return db_get_char (value, length);
}

/*
 * db_get_string_size() -
 * return :
 * value(in):
 */
INLINE int
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
 * db_get_string_codeset() -
 * return :
 * value(in):
 */
INLINE int
db_get_string_codeset (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);
#endif

  return value->data.ch.info.codeset;
}

/*
 * db_get_string_collation() -
 * return :
 * value(in):
 */
INLINE int
db_get_string_collation (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);
#endif

  return value->domain.char_info.collation_id;
}

/*
 * db_get_resultset() -
 * return :
 * value(in):
 */
INLINE DB_RESULTSET
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
 * db_get_enum_codeset() -
 * return :
 * value(in):
 */
INLINE int
db_get_enum_codeset (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);
#endif

  return value->data.enumeration.str_val.info.codeset;
}

/*
 * db_get_enum_collation() -
 * return :
 * value(in):
 */
INLINE int
db_get_enum_collation (const DB_VALUE * value)
{
#if defined(NO_SERVER_OR_DEBUG_MODE)
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);
#endif

  return value->domain.char_info.collation_id;
}


#endif /* _DBTYPE_COMMON_H_ */
