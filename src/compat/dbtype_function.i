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
 * dbtype_function.i - API/inlined functions related to db_make and db_get
 *
 */

#include "dbtype_def.h"

#if !defined (_NO_INLINE_DBTYPE_FUNCTION_)
#include "porting_inline.hpp"

STATIC_INLINE int db_get_int (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_SHORT db_get_short (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_BIGINT db_get_bigint (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_CONST_C_CHAR db_get_string (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_FLOAT db_get_float (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_DOUBLE db_get_double (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_OBJECT *db_get_object (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_COLLECTION *db_get_set (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_POINTER db_get_pointer (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TIME *db_get_time (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TIMESTAMPTZ *db_get_timestamptz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_DATETIME *db_get_datetime (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_DATETIMETZ *db_get_datetimetz (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_DATE *db_get_date (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_MONETARY *db_get_monetary (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_error (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_ELO *db_get_elo (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_NUMERIC db_get_numeric (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_CONST_C_BIT db_get_bit (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_CONST_C_CHAR db_get_char (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_CONST_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_string_size (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE unsigned short db_get_enum_short (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_CONST_C_CHAR db_get_enum_string (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_enum_string_size (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_C_CHAR db_get_method_error_msg (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_RESULTSET db_get_resultset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE INTL_CODESET db_get_string_codeset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_string_collation (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_enum_codeset (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_get_enum_collation (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE OID *db_get_oid (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TYPE db_value_type (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_value_precision (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_value_scale (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE JSON_DOC *db_get_json_document (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_db_char (DB_VALUE * value, INTL_CODESET codeset, const int collation_id, DB_CONST_C_CHAR str,
				   const int size) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_null (DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_int (DB_VALUE * value, const int num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_float (DB_VALUE * value, const DB_C_FLOAT num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_double (DB_VALUE * value, const DB_C_DOUBLE num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_object (DB_VALUE * value, DB_C_OBJECT * obj) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timestamp (DB_VALUE * value, const DB_C_TIMESTAMP timeval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timestampltz (DB_VALUE * value, const DB_C_TIMESTAMP ts_val) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_timestamptz (DB_VALUE * value, const DB_C_TIMESTAMPTZ * ts_tz_val)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_pointer (DB_VALUE * value, DB_C_POINTER ptr) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_error (DB_VALUE * value, const int errcode) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_short (DB_VALUE * value, const DB_C_SHORT num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_bigint (DB_VALUE * value, const DB_BIGINT num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_bit (DB_VALUE * value, const int bit_length, DB_CONST_C_BIT bit_str,
			       const int bit_str_bit_size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_varbit (DB_VALUE * value, const int max_bit_length, DB_CONST_C_BIT bit_str,
				  const int bit_str_bit_size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_char (DB_VALUE * value, const int char_length, DB_CONST_C_CHAR str,
				const int char_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_varchar (DB_VALUE * value, const int max_char_length, DB_CONST_C_CHAR str,
				   const int char_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_nchar (DB_VALUE * value, const int nchar_length, DB_CONST_C_NCHAR str,
				 const int nchar_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_varnchar (DB_VALUE * value, const int max_nchar_length, DB_CONST_C_NCHAR str,
				    const int nchar_str_byte_size, const int codeset, const int collation_id)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_enumeration (DB_VALUE * value, unsigned short index, DB_CONST_C_CHAR str, int size,
				       unsigned char codeset, const int collation_id) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_string (DB_VALUE * value, DB_CONST_C_CHAR str) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_string_copy (DB_VALUE * value, DB_CONST_C_CHAR str) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_oid (DB_VALUE * value, const OID * oid) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_set (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_multiset (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_sequence (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_collection (DB_VALUE * value, DB_C_SET * set) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_elo (DB_VALUE * value, DB_TYPE type, const DB_ELO * elo) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_time (DB_VALUE * value, const int hour, const int minute, const int second)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int db_make_date (DB_VALUE * value, const int month, const int day, const int year)
  __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_make_json (DB_VALUE * value, JSON_DOC * json_document, bool need_clear)
  __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int db_get_compressed_size (DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void db_set_compressed_string (DB_VALUE * value, char *compressed_string,
					     int compressed_size, bool compressed_need_clear)
  __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE bool db_value_is_null (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_TYPE db_value_domain_type (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
#endif // !NO_INLINE_DBTYPE_FUNCTION

#include <assert.h>
#include "memory_cwrapper.h"

/*
 * db_get_int() -
 * return :
 * value(in):
 */
int
db_get_int (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
short
db_get_short (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_BIGINT
db_get_bigint (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_CONST_C_CHAR
db_get_string (const DB_VALUE * value)
{
  const char *str = NULL;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      str = value->data.ch.sm.buf;
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
float
db_get_float (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
double
db_get_double (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_OBJECT *
db_get_object (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_SET *
db_get_set (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
#endif

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
DB_MIDXKEY *
db_get_midxkey (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
#endif

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
void *
db_get_pointer (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
#endif

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
DB_TIME *
db_get_time (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_TIME);

  // todo: Assess how to better handle const types, here we should return explicit values, not pointers. Same for below.
  return (DB_TIME *) (&value->data.time);
}

/*
 * db_get_timestamp() -
 * return :
 * value(in):
 */
DB_TIMESTAMP *
db_get_timestamp (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_TIMESTAMPTZ *
db_get_timestamptz (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_DATETIME *
db_get_datetime (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_DATETIMETZ *
db_get_datetimetz (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_DATE *
db_get_date (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_MONETARY *
db_get_monetary (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_get_error (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
DB_ELO *
db_get_elo (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
      return ((DB_ELO *) (&value->data.elo));
    }
}

/*
 * db_get_numeric() -
 * return :
 * value(in):
 */
DB_C_NUMERIC
db_get_numeric (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }
  else
    {
      return (DB_C_NUMERIC) (value->data.num.d.buf);
    }
}

/*
 * db_get_bit() -
 * return :
 * value(in):
 * length(out):
 */
DB_CONST_C_BIT
db_get_bit (const DB_VALUE * value, int *length)
{
  const char *str = NULL;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);
#endif

  if (value->domain.general_info.is_null)
    {
      return NULL;
    }

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      {
	*length = value->data.ch.sm.size;
	str = value->data.ch.sm.buf;
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
DB_CONST_C_CHAR
db_get_char (const DB_VALUE * value, int *length)
{
  const char *str = NULL;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
  CHECK_1ARG_NULL (length);
#endif

  if (value->domain.general_info.is_null || value->domain.general_info.type == DB_TYPE_ERROR)
    {
      return NULL;
    }

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      {
	str = value->data.ch.sm.buf;
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
DB_CONST_C_NCHAR
db_get_nchar (const DB_VALUE * value, int *length)
{
  return db_get_char (value, length);
}

/*
 * db_get_string_size() -
 * return :
 * value(in):
 */
int
db_get_string_size (const DB_VALUE * value)
{
  int size = 0;

#if defined (API_ACTIVE_CHECKS)
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
 * db_get_enum_short () -
 * return :
 * value(in):
 */
unsigned short
db_get_enum_short (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_ENUMERATION);

  return value->data.enumeration.short_val;
}

/*
 * db_get_enum_string () -
 * return :
 * value(in):
 */
DB_CONST_C_CHAR
db_get_enum_string (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#endif

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
int
db_get_enum_string_size (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_ENUMERATION);

  return value->data.enumeration.str_val.medium.size;
}

/*
 * db_get_method_error_msg() -
 * return :
 */
char *
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
DB_RESULTSET
db_get_resultset (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_RESULTSET);

  return value->data.rset;
}

/*
 * db_get_string_codeset() -
 * return :
 * value(in):
 */
INTL_CODESET
db_get_string_codeset (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);
#endif

  return (INTL_CODESET) value->data.ch.info.codeset;
}

/*
 * db_get_string_collation() -
 * return :
 * value(in):
 */
int
db_get_string_collation (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);
#endif

  return value->domain.char_info.collation_id;
}

/*
 * db_get_enum_codeset() -
 * return :
 * value(in):
 */
int
db_get_enum_codeset (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO_WITH_TYPE (value, INTL_CODESET);
#endif

  return value->data.enumeration.str_val.info.codeset;
}

/*
 * db_get_enum_collation() -
 * return :
 * value(in):
 */
int
db_get_enum_collation (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO_WITH_TYPE (value, int);
#endif

  return value->domain.char_info.collation_id;
}

/*
 * db_get_oid() -
 * return :
 * value(in):
 */
OID *
db_get_oid (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_NULL (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_OID);

  return ((OID *) (&value->data.oid));
}

/*
 * db_value_type()
 * return     : DB_TYPE of value's domain or DB_TYPE_NULL
 * value(in)  : Pointer to a DB_VALUE
 */
DB_TYPE
db_value_type (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_UNKNOWN (value);
#else
  if (value == NULL)
    {
      // todo: Should this ever happen?
      assert (false);
      return DB_TYPE_NULL;
    }
#endif

  if (value->domain.general_info.is_null)
    {
      return DB_TYPE_NULL;
    }
  else
    {
      return DB_VALUE_DOMAIN_TYPE (value);
    }
}

/*
 * db_value_precision() - get the precision of value.
 * return     : precision of given value.
 * value(in)  : Pointer to a DB_VALUE.
 */
int
db_value_precision (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#else
  if (value == NULL)
    {
      // todo : Should this ever happen?
      assert (false);
      return 0;
    }
#endif

  switch (value->domain.general_info.type)
    {
    case DB_TYPE_NUMERIC:
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_MONETARY:
      return value->domain.numeric_info.precision;
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      return value->domain.char_info.length;
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_SUB:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_VOBJ:
    case DB_TYPE_OID:
    default:
      return 0;
    }
}

/*
 * db_value_scale() - get the scale of value.
 * return     : scale of given value.
 * value(in)  : Pointer to a DB_VALUE.
 */
int
db_value_scale (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#else
  if (value == NULL)
    {
      // todo: Should this ever happen?
      assert (false);
      return 0;
    }
#endif

  if (value->domain.general_info.type == DB_TYPE_NUMERIC
      || value->domain.general_info.type == DB_TYPE_DATETIME
      || value->domain.general_info.type == DB_TYPE_DATETIMETZ
      || value->domain.general_info.type == DB_TYPE_DATETIMELTZ)
    {
      return value->domain.numeric_info.scale;
    }
  else
    {
      return 0;
    }
}

JSON_DOC *
db_get_json_document (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ZERO (value);
#endif

  assert (value->domain.general_info.type == DB_TYPE_JSON);

  return value->data.json.document;
}

/***********************************************************/
/* db_make family of functions. */

/*
 * db_make_db_char() -
 * return :
 * value(out) :
 * codeset(in):
 * collation_id(in):
 * str(in):
 * size(in):
 */
int
db_make_db_char (DB_VALUE * value, const INTL_CODESET codeset, const int collation_id, DB_CONST_C_CHAR str,
		 const int size)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->data.ch.info.style = MEDIUM_STRING;
  value->data.ch.info.is_max_string = false;
  value->data.ch.info.compressed_need_clear = false;
  value->data.ch.medium.codeset = codeset;
  value->data.ch.medium.size = size;
  value->data.ch.medium.buf = str;
  value->data.ch.medium.compressed_buf = NULL;
  value->data.ch.medium.compressed_size = 0;
  value->domain.general_info.is_null = ((void *) str != NULL) ? 0 : 1;
  value->domain.general_info.is_null = ((size == 0 && prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
					? 1 : DB_IS_NULL (value));
  value->domain.char_info.collation_id = collation_id;
  value->need_clear = false;
  /*
   * Implemented it in a way that fills in the length value
   * during the operation of obtaining the length later, to avoid 
   * the significant overhead of intl_char_length for each db_make_db_char, 
   * if we directly calculate and insert the length value.
   */
  value->data.ch.medium.length = -1;

  return NO_ERROR;
}

/*
 * db_make_null() -
 * return :
 * value(out) :
 */
int
db_make_null (DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_int (DB_VALUE * value, const int num)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_INTEGER;
  value->data.i = num;
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
int
db_make_float (DB_VALUE * value, const float num)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_double (DB_VALUE * value, const double num)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DOUBLE;
  value->data.d = num;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_object() -
 * return :
 * value(out) :
 * obj(in):
 */
int
db_make_object (DB_VALUE * value, DB_OBJECT * obj)
{
#if defined (API_ACTIVE_CHECKS)
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
 * db_make_midxkey() -
 * return :
 * value(out) :
 * midxkey(in):
 */
int
db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey)
{
  int error = NO_ERROR;

#if defined (API_ACTIVE_CHECKS)
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
 * db_make_timestamp() -
 * return :
 * value(out):
 * timeval(in):
 */
int
db_make_timestamp (DB_VALUE * value, const DB_TIMESTAMP timeval)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_timestampltz (DB_VALUE * value, const DB_TIMESTAMP ts_val)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_timestamptz (DB_VALUE * value, const DB_TIMESTAMPTZ * ts_tz_val)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz)
{
#if defined (API_ACTIVE_CHECKS)
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
 * db_make_monetary() -
 * return :
 * value(out):
 * type(in):
 * amount(in):
 */
int
db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount)
{
  int error;

#if defined (API_ACTIVE_CHECKS)
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
 * db_make_pointer() -
 * return :
 * value(out) :
 * ptr(in):
 */
int
db_make_pointer (DB_VALUE * value, void *ptr)
{
#if defined (API_ACTIVE_CHECKS)
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
 * db_make_error() -
 * return :
 * value(out):
 * errcode(in):
 */
int
db_make_error (DB_VALUE * value, const int errcode)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg)
{
#if defined (API_ACTIVE_CHECKS)
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
 * db_make_short() -
 * return :
 * value(out) :
 * num(in) :
 */
int
db_make_short (DB_VALUE * value, const short num)
{
#if defined (API_ACTIVE_CHECKS)
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
int
db_make_bigint (DB_VALUE * value, const DB_BIGINT num)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_BIGINT;
  value->data.bigint = num;
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
int
db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale)
{
  int error = NO_ERROR;

#if defined (API_ACTIVE_CHECKS)
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
 * db_make_bit() -
 * return :
 * value(out) :
 * bit_length(in):
 * bit_str(in):
 * bit_str_bit_size(in):
 */
int
db_make_bit (DB_VALUE * value, const int bit_length, DB_CONST_C_BIT bit_str, const int bit_str_bit_size)
{
  int error;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_BIT, bit_length, 0);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = db_make_db_char (value, INTL_CODESET_RAW_BITS, 0, bit_str, bit_str_bit_size);
  return error;
}

/*
 * db_make_varbit() -
 * return :
 * value(out) :
 * max_bit_length(in):
 * bit_str(in):
 * bit_str_bit_size(in):
 */
int
db_make_varbit (DB_VALUE * value, const int max_bit_length, DB_CONST_C_BIT bit_str, const int bit_str_bit_size)
{
  int error;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARBIT, max_bit_length, 0);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = db_make_db_char (value, INTL_CODESET_RAW_BITS, 0, bit_str, bit_str_bit_size);

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
int
db_make_char (DB_VALUE * value, const int char_length, DB_CONST_C_CHAR str, const int char_str_byte_size,
	      const int codeset, const int collation_id)
{
  int error;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_CHAR, char_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, (INTL_CODESET) codeset, collation_id, str, char_str_byte_size);
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
int
db_make_varchar (DB_VALUE * value, const int max_char_length, DB_CONST_C_CHAR str, const int char_str_byte_size,
		 const int codeset, const int collation_id)
{
  int error;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARCHAR, max_char_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, (INTL_CODESET) codeset, collation_id, str, char_str_byte_size);
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
int
db_make_nchar (DB_VALUE * value, const int nchar_length, DB_CONST_C_NCHAR str, const int nchar_str_byte_size,
	       const int codeset, const int collation_id)
{
  int error;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_NCHAR, nchar_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, (INTL_CODESET) codeset, collation_id, str, nchar_str_byte_size);
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
int
db_make_varnchar (DB_VALUE * value, const int max_nchar_length, DB_CONST_C_NCHAR str, const int nchar_str_byte_size,
		  const int codeset, const int collation_id)
{
  int error;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARNCHAR, max_nchar_length, 0);
  if (error == NO_ERROR)
    {
      error = db_make_db_char (value, (INTL_CODESET) codeset, collation_id, str, nchar_str_byte_size);
    }

  return error;
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
int
db_make_enumeration (DB_VALUE * value, unsigned short index, DB_CONST_C_CHAR str, int size, unsigned char codeset,
		     const int collation_id)
{
#if defined (API_ACTIVE_CHECKS)
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
 * db_make_resultset() -
 * return :
 * value(out):
 * handle(in):
 */
int
db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_RESULTSET;
  value->data.rset = handle;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_oid() -
 * return :
 * value(out):
 * oid(in):
 */
int
db_make_oid (DB_VALUE * value, const OID * oid)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_2ARGS_ERROR (value, oid);
#endif

  if (oid == NULL)
    {
      value->domain.general_info.is_null = 1;
      return NO_ERROR;
    }

  value->domain.general_info.type = DB_TYPE_OID;
  value->data.oid.pageid = oid->pageid;
  value->data.oid.slotid = oid->slotid;
  value->data.oid.volid = oid->volid;
  value->domain.general_info.is_null = OID_ISNULL (oid);
  value->need_clear = false;

  return NO_ERROR;
}

/*
 * db_make_string() -
 * return :
 * value(out) :
 * str(in):
 */
int
db_make_string (DB_VALUE * value, DB_CONST_C_CHAR str)
{
  int error;
  int size;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  error = db_value_domain_init (value, DB_TYPE_VARCHAR, TP_FLOATING_PRECISION_VALUE, 0);
  if (error == NO_ERROR)
    {
      if (str)
	{
	  size = (int) strlen (str);
	}
      else
	{
	  size = 0;
	}
      error = db_make_db_char (value, LANG_SYS_CODESET, LANG_SYS_COLLATION, str, size);
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
int
db_make_string_copy (DB_VALUE * value, DB_CONST_C_CHAR str)
{
  int error;
  char *copy_str;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  if (str == NULL)
    {
      db_make_null (value);
      return NO_ERROR;
    }

  copy_str = db_private_strdup (NULL, str);

  if (copy_str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (str) + 1);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  error = db_make_string (value, copy_str);

  if (error != NO_ERROR)
    {
      db_private_free (NULL, copy_str);
      return error;
    }

  /* Set need_clear to true. */
  value->need_clear = true;

  return error;
}

/*
 * db_make_set() -
 * return :
 * value(out) :
 * set(in):
 */
int
db_make_set (DB_VALUE * value, DB_SET * set)
{
  int error = NO_ERROR;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_SET;
  value->data.set = set;
  if (set)
    {
      if ((set->set && setobj_type (set->set) == DB_TYPE_SET) || set->disk_set)
	{
	  value->domain.general_info.is_null = 0;
	}
      else
	{
	  error = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	}
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_multiset() -
 * return :
 * value(out) :
 * set(in):
 */
int
db_make_multiset (DB_VALUE * value, DB_SET * set)
{
  int error = NO_ERROR;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_MULTISET;
  value->data.set = set;
  if (set)
    {
      if ((set->set && setobj_type (set->set) == DB_TYPE_MULTISET) || set->disk_set)
	{
	  value->domain.general_info.is_null = 0;
	}
      else
	{
	  error = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	}
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_sequence() -
 * return :
 * value(out) :
 * set(in):
 */
int
db_make_sequence (DB_VALUE * value, DB_SET * set)
{
  int error = NO_ERROR;

#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_SEQUENCE;
  value->data.set = set;
  if (set)
    {
      if ((set->set && setobj_type (set->set) == DB_TYPE_SEQUENCE) || set->disk_set)
	{
	  value->domain.general_info.is_null = 0;
	}
      else
	{
	  error = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	}
    }
  else
    {
      value->domain.general_info.is_null = 1;
    }

  value->need_clear = false;

  return error;
}

/*
 * db_make_collection() -
 * return :
 * value(out) :
 * col(in):
 */
int
db_make_collection (DB_VALUE * value, DB_COLLECTION * col)
{
  int error = NO_ERROR;

#if defined (API_ACTIVE_CHECKS)
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
 * db_make_elo () -
 * return:
 * value(out):
 * type(in):
 * elo(in):`
 */
int
db_make_elo (DB_VALUE * value, DB_TYPE type, const DB_ELO * elo)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = type;
  if (elo == NULL || elo->size < 0 || elo->type == ELO_NULL)
    {
      elo_init_structure (&value->data.elo);
      value->domain.general_info.is_null = 1;
    }
  else
    {
      value->data.elo = *elo;
      value->domain.general_info.is_null = 0;
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
int
db_make_time (DB_VALUE * value, const int hour, const int min, const int sec)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_TIME;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_time_encode (&value->data.time, hour, min, sec);
}

/*
 * db_make_date() -
 * return :
 * value(out):
 * mon(in):
 * day(in):
 * year(in):
 */
int
db_make_date (DB_VALUE * value, const int mon, const int day, const int year)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#endif

  value->domain.general_info.type = DB_TYPE_DATE;
  value->domain.general_info.is_null = 0;
  value->need_clear = false;
  return db_date_encode (&value->data.date, mon, day, year);
}

int
db_make_json (DB_VALUE * value, JSON_DOC * json_document, bool need_clear)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_ERROR (value);
#else
  if (value == NULL)
    {
      /* todo: Should this happen? */
      assert (false);
      return ER_FAILED;
    }
#endif

  value->domain.general_info.type = DB_TYPE_JSON;
  value->domain.general_info.is_null = 0;
  value->data.json.document = json_document;
  value->data.json.schema_raw = NULL;
  value->need_clear = need_clear;

  return NO_ERROR;
}

int
db_get_compressed_size (DB_VALUE * value)
{
  DB_TYPE type;

  if (value == NULL || DB_IS_NULL (value))
    {
      return 0;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);

  /* Preliminary check */
  assert (type == DB_TYPE_VARCHAR || type == DB_TYPE_VARNCHAR);

  return value->data.ch.medium.compressed_size;
}

/*
 *  db_set_compressed_string() - Sets the compressed string, its size and its need for clear in the DB_VALUE
 *
 *  value(in/out)             : The DB_VALUE
 *  compressed_string(in)     :
 *  compressed_size(in)       :
 *  compressed_need_clear(in) :
 */
void
db_set_compressed_string (DB_VALUE * value, char *compressed_string, int compressed_size, bool compressed_need_clear)
{
  DB_TYPE type;

  if (value == NULL || DB_IS_NULL (value))
    {
      return;
    }
  type = DB_VALUE_DOMAIN_TYPE (value);

  /* Preliminary check */
  assert (type == DB_TYPE_VARCHAR || type == DB_TYPE_VARNCHAR);

  value->data.ch.medium.compressed_buf = compressed_string;
  value->data.ch.medium.compressed_size = compressed_size;
  value->data.ch.info.compressed_need_clear = compressed_need_clear;
}

/*
 * db_value_is_null() -
 * return :
 * value(in) :
 */
bool
db_value_is_null (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_TRUE (value);
#endif

  if (value == NULL)
    {
      return true;
    }

  return (value->domain.general_info.is_null != 0);
}

/*
 * db_value_domain_type() - get the type of value's domain.
 * return     : DB_TYPE of value's domain
 * value(in)  : Pointer to a DB_VALUE
 */
DB_TYPE
db_value_domain_type (const DB_VALUE * value)
{
#if defined (API_ACTIVE_CHECKS)
  CHECK_1ARG_UNKNOWN (value);
#else

  if (value == NULL)
    {
      // todo: does this ever happen?
      assert (false);
      return DB_TYPE_UNKNOWN;
    }
#endif

  return (DB_TYPE) value->domain.general_info.type;
}
