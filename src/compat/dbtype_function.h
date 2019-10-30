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
 *  dbtype_function.h - Holds declarations for API functions.
 */

#ifndef _NO_INLINE_DBTYPE_FUNCTION_
#define _NO_INLINE_DBTYPE_FUNCTION_

#include <stdio.h>

#include "db_set_function.h"
#include "dbtype_def.h"

#define DB_CURRENCY_DEFAULT db_get_currency_default()

// for backward compatibility
#define db_collection db_set

#define db_make_utime db_make_timestamp

#define DB_MAKE_NULL(value) db_make_null(value)

#define DB_VALUE_CLONE_AS_NULL(src_value, dest_value)                   \
  do {                                                                  \
    if ((db_value_domain_init(dest_value,                               \
                              db_value_domain_type(src_value),          \
                              db_value_precision(src_value),            \
                              db_value_scale(src_value)))               \
        == NO_ERROR)                                                    \
      (void)db_value_put_null(dest_value);                              \
  } while (0)

#define DB_MAKE_INTEGER(value, num) db_make_int(value, num)

#define DB_MAKE_INT DB_MAKE_INTEGER

#define DB_MAKE_BIGINT(value, num) db_make_bigint(value, num)

#define DB_MAKE_BIGINTEGER DB_MAKE_BIGINT

#define DB_MAKE_FLOAT(value, num) db_make_float(value, num)

#define DB_MAKE_DOUBLE(value, num) db_make_double(value, num)

#define DB_MAKE_OBJECT(value, obj) db_make_object(value, obj)

#define DB_MAKE_OBJ DB_MAKE_OBJECT

#define DB_MAKE_SET(value, set) db_make_set(value, set)

#define DB_MAKE_MULTISET(value, set) db_make_multiset(value, set)

/* obsolete */
#define DB_MAKE_MULTI_SET DB_MAKE_MULTISET

#define DB_MAKE_SEQUENCE(value, set) db_make_sequence(value, set)

#define DB_MAKE_LIST DB_MAKE_SEQUENCE

/* obsolete */
#define DB_MAKE_SEQ DB_MAKE_SEQUENCE

/* new preferred interface */
  /*  todo: This following macro had in its previous version another call to
   *  db_value_domain_init(). Now it has been removed but it needs to be
   *  checked if its still correct!!!.
   */
#define DB_MAKE_OID(value, oid)	\
      (((oid) == NULL) ? ((value)->domain.general_info.is_null = 1, NO_ERROR) : \
          db_make_oid((value), (oid)))

#define DB_GET_OID(value)		(db_get_oid(value))
#define DB_MAKE_COLLECTION(value, col) db_make_collection(value, col)

#define DB_MAKE_MIDXKEY(value, midxkey) db_make_midxkey(value, midxkey)

#define DB_MAKE_ELO(value, type, elo) db_make_elo(value, type, elo)

#define DB_MAKE_TIME(value, hour, minute, second) \
    db_make_time(value, hour, minute, second)

#define DB_MAKE_ENCODED_TIME(value, time_value) \
    db_value_put_encoded_time(value, time_value)

#define DB_MAKE_DATE(value, month, day, year) \
    db_make_date(value, month, day, year)

#define DB_MAKE_ENCODED_DATE(value, date_value) \
    db_value_put_encoded_date(value, date_value)

#define DB_MAKE_TIMESTAMP(value, timeval) \
    db_make_timestamp(value, timeval)

#define DB_MAKE_UTIME DB_MAKE_TIMESTAMP

#define DB_MAKE_TIMESTAMPTZ(value, ts_tz) \
    db_make_timestamptz(value, ts_tz)

#define DB_MAKE_TIMESTAMPLTZ(value, timeval) \
    db_make_timestampltz(value, timeval)

#define DB_MAKE_MONETARY_AMOUNT(value, amount) \
    db_make_monetary(value, DB_CURRENCY_DEFAULT, amount)

#define DB_MAKE_DATETIME(value, datetime_value) \
    db_make_datetime(value, datetime_value)

#define DB_MAKE_DATETIMETZ(value, datetimetz_value) \
    db_make_datetimetz(value, datetimetz_value)

#define DB_MAKE_DATETIMELTZ(value, datetime_value) \
    db_make_datetimeltz(value, datetime_value)

#define DB_MAKE_MONETARY DB_MAKE_MONETARY_AMOUNT

#define DB_MAKE_MONETARY_TYPE_AMOUNT(value, type, amount) \
    db_make_monetary(value, type, amount)

#define DB_MAKE_POINTER(value, ptr) db_make_pointer(value, ptr)

#define DB_MAKE_ERROR(value, errcode) db_make_error(value, errcode)

#define DB_MAKE_METHOD_ERROR(value, errcode, errmsg) \
           db_make_method_error(value, errcode, errmsg)

#define DB_MAKE_SMALLINT(value, num) db_make_short(value, num)

#define DB_MAKE_SHORT DB_MAKE_SMALLINT

#define DB_MAKE_NUMERIC(value, num, precision, scale) \
        db_make_numeric(value, num, precision, scale)

#define DB_MAKE_BIT(value, bit_length, bit_str, bit_str_bit_size) \
        db_make_bit(value, bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_VARBIT(value, max_bit_length, bit_str, bit_str_bit_size) \
        db_make_varbit(value, max_bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_CHAR(value, char_length, str, char_str_byte_size, codeset, collation) \
        db_make_char(value, char_length, str, char_str_byte_size, codeset, collation)

#define DB_MAKE_VARCHAR(value, max_char_length, str, char_str_byte_size, codeset, collation) \
        db_make_varchar(value, max_char_length, str, char_str_byte_size, codeset, collation)

#define DB_MAKE_STRING(value, str) db_make_string(value, str)

#define DB_MAKE_NCHAR(value, nchar_length, str, nchar_str_byte_size, codeset, collation) \
        db_make_nchar(value, nchar_length, str, nchar_str_byte_size, codeset, collation)

#define DB_MAKE_VARNCHAR(value, max_nchar_length, str, nchar_str_byte_size, codeset, collation) \
        db_make_varnchar(value, max_nchar_length, str, nchar_str_byte_size, codeset, collation)

#define DB_MAKE_ENUMERATION(value, index, str, size, codeset, collation) \
	db_make_enumeration(value, index, str, size, codeset, collation)

#define DB_MAKE_RESULTSET(value, handle) db_make_resultset(value, handle)

#define db_get_collection db_get_set
#define db_get_utime db_get_timestamp

#define DB_IS_NULL(value)               db_value_is_null(value)

#define DB_VALUE_DOMAIN_TYPE(value)     db_value_domain_type(value)

/* New preferred interface for DB_GET macros. */
#define DB_GET_INT(v) db_get_int(v)
#define DB_GET_SHORT(v) db_get_short(v)
#define DB_GET_BIGINT(v) db_get_bigint(v)
#define DB_GET_FLOAT(v) db_get_float(v)
#define DB_GET_STRING(v) db_get_string(v)
#define DB_GET_STRING_LENGTH(v) db_get_string_length(v)
#define DB_GET_DOUBLE(v) db_get_double(v)
#define DB_GET_OBJECT(v) db_get_object(v)
#define DB_GET_SET(v) db_get_set(v)
#define DB_GET_MIDXKEY(v) db_get_midxkey(v)
#define DB_GET_POINTER(v) db_get_pointer(v)
#define DB_GET_TIME(v) db_get_time(v)
#define DB_GET_TIMESTAMP(v) db_get_timestamp(v)
#define DB_GET_TIMESTAMPTZ(v) db_get_timestamptz(v)
#define DB_GET_DATETIME(v) db_get_datetime(v)
#define DB_GET_DATETIMETZ(v) db_get_datetimetz(v)
#define DB_GET_DATE(v) db_get_date(v)
#define DB_GET_MONETARY(v) db_get_monetary(v)
#define DB_GET_ERROR(v) db_get_error(v)
#define DB_GET_ELO(v) db_get_elo(v)
#define DB_GET_NUMERIC(v) db_get_numeric(v)
#define DB_GET_BIT(v, l) db_get_bit(v, l)
#define DB_GET_CHAR(v, l) db_get_char(v, l)
#define DB_GET_NCHAR(v, l) db_get_nchar(v, l)
#define DB_GET_STRING_SIZE(v) db_get_string_size(v)
#define DB_GET_ENUM_SHORT(v) db_get_enum_short(v)
#define DB_GET_ENUM_STRING(v) db_get_enum_string(v)
#define DB_GET_ENUM_STRING_SIZE(v) db_get_enum_string_size(v)
#define DB_GET_METHOD_ERROR_MSG() db_get_method_error_msg()
#define DB_GET_RESULTSET(v) db_get_resultset(v)
#define DB_GET_STRING_CODESET(v) ((INTL_CODESET) db_get_string_codeset(v))
#define DB_GET_STRING_COLLATION(v) db_get_string_collation(v)
#define DB_GET_ENUM_CODESET(v) db_get_enum_codeset(v)
#define DB_GET_ENUM_COLLATION(v) db_get_enum_collation(v)
#define DB_VALUE_TYPE(value) db_value_type(value)
#define DB_VALUE_PRECISION(value) db_value_precision(value)
#define DB_VALUE_SCALE(value) db_value_scale(value)

#define DB_GET_INTEGER(value)           db_get_int(value)
#define DB_GET_BIGINTEGER               DB_GET_BIGINT
#define DB_GET_OBJ DB_GET_OBJECT
#define DB_GET_MULTISET(value)          db_get_set(value)
#define DB_GET_LIST(value)              db_get_set(value)
#define DB_GET_SEQUENCE DB_GET_LIST
#define DB_GET_COLLECTION(value)        db_get_set(value)
#define DB_GET_UTIME DB_GET_TIMESTAMP
#define DB_GET_SMALLINT(value)          db_get_short(value)

#define DB_GET_COMPRESSED_SIZE(value) db_get_compressed_size(value)

#define DB_GET_JSON_DOCUMENT(value) db_get_json_document(value)

#define DB_GET_SEQ DB_GET_SEQUENCE

#define DB_SET_COMPRESSED_STRING(value, compressed_string, compressed_size, compressed_need_clear) \
	db_set_compressed_string(value, compressed_string, compressed_size, compressed_need_clear)

#define DB_TRIED_COMPRESSION(value) (DB_GET_COMPRESSED_SIZE(value) != DB_NOT_YET_COMPRESSED)

#ifdef __cplusplus
extern "C"
{
#endif
  /********************************************************/
  /* From db_date.h */

  extern int db_date_encode (DB_DATE * date, int month, int day, int year);
  extern int db_time_encode (DB_TIME * timeval, int hour, int minute, int second);
  extern void db_date_decode (const DB_DATE * date, int *monthp, int *dayp, int *yearp);
  extern void db_time_decode (DB_TIME * timeval, int *hourp, int *minutep, int *secondp);
  /********************************************************/

  extern DB_VALUE *db_value_create (void);
  extern DB_VALUE *db_value_copy (DB_VALUE * value);
  extern int db_value_clone (DB_VALUE * src, DB_VALUE * dest);
  extern int db_value_clear (DB_VALUE * value);
  extern int db_value_free (DB_VALUE * value);
  extern int db_value_clear_array (DB_VALUE_ARRAY * value_array);
  extern void db_value_print (const DB_VALUE * value);
  extern void db_value_fprint (FILE * fp, const DB_VALUE * value);
  extern int db_value_coerce (const DB_VALUE * src, DB_VALUE * dest, const DB_DOMAIN * desired_domain);

  extern int db_value_equal (const DB_VALUE * value1, const DB_VALUE * value2);
  extern int db_value_compare (const DB_VALUE * value1, const DB_VALUE * value2);
  extern int db_value_domain_init (DB_VALUE * value, DB_TYPE type, const int precision, const int scale);
  extern int db_value_domain_min (DB_VALUE * value, DB_TYPE type, const int precision, const int scale,
				  const int codeset, const int collation_id, const DB_ENUMERATION * enumeration);
  extern int db_value_domain_max (DB_VALUE * value, DB_TYPE type, const int precision, const int scale,
				  const int codeset, const int collation_id, const DB_ENUMERATION * enumeration);
  extern int db_value_domain_default (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale,
				      const int codeset, const int collation_id, DB_ENUMERATION * enumeration);
  extern int db_value_domain_zero (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale);
  extern int db_string_truncate (DB_VALUE * value, const int max_precision);
  extern DB_TYPE db_value_domain_type (const DB_VALUE * value);
  extern int db_value_put_null (DB_VALUE * value);
  extern int db_value_put (DB_VALUE * value, const DB_TYPE_C c_type, void *input, const int input_length);
  extern bool db_value_type_is_collection (const DB_VALUE * value);
  extern bool db_value_is_null (const DB_VALUE * value);
  extern int db_value_get (DB_VALUE * value, const DB_TYPE_C type, void *buf, const int buflen, int *transferlen,
			   int *outputlen);
  extern DB_CURRENCY db_value_get_monetary_currency (const DB_VALUE * value);
  extern double db_value_get_monetary_amount_as_double (const DB_VALUE * value);
  extern int db_value_put_monetary_currency (DB_VALUE * value, const DB_CURRENCY type);
  extern int db_value_put_monetary_amount_as_double (DB_VALUE * value, const double amount);
  extern int db_value_alter_type (DB_VALUE * value, DB_TYPE type);

  extern int db_value_put_encoded_time (DB_VALUE * value, const DB_TIME * time_value);
  extern int db_value_put_encoded_date (DB_VALUE * value, const DB_DATE * date_value);

  extern DB_CURRENCY db_get_currency_default (void);

  extern DB_DOMAIN *db_type_to_db_domain (DB_TYPE type);
  extern const char *db_default_expression_string (DB_DEFAULT_EXPR_TYPE default_expr_type);

  extern int db_get_deep_copy_of_json (const DB_JSON * src, DB_JSON * dst);
  extern int db_init_db_json_pointers (DB_JSON * val);
  extern int db_convert_json_into_scalar (const DB_VALUE * src, DB_VALUE * dest);
  extern bool db_is_json_value_type (DB_TYPE type);
  extern bool db_is_json_doc_type (DB_TYPE type);

  extern int db_get_int (const DB_VALUE * value);
  extern DB_C_SHORT db_get_short (const DB_VALUE * value);
  extern DB_BIGINT db_get_bigint (const DB_VALUE * value);
  extern DB_CONST_C_CHAR db_get_string (const DB_VALUE * value);
  extern DB_C_FLOAT db_get_float (const DB_VALUE * value);
  extern DB_C_DOUBLE db_get_double (const DB_VALUE * value);
  extern DB_OBJECT *db_get_object (const DB_VALUE * value);
  extern DB_COLLECTION *db_get_set (const DB_VALUE * value);
  extern DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value);
  extern DB_C_POINTER db_get_pointer (const DB_VALUE * value);
  extern DB_TIME *db_get_time (const DB_VALUE * value);
  extern DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value);
  extern DB_TIMESTAMPTZ *db_get_timestamptz (const DB_VALUE * value);
  extern DB_DATETIME *db_get_datetime (const DB_VALUE * value);
  extern DB_DATETIMETZ *db_get_datetimetz (const DB_VALUE * value);
  extern DB_DATE *db_get_date (const DB_VALUE * value);
  extern DB_MONETARY *db_get_monetary (const DB_VALUE * value);
  extern int db_get_error (const DB_VALUE * value);
  extern DB_ELO *db_get_elo (const DB_VALUE * value);
  extern DB_C_NUMERIC db_get_numeric (const DB_VALUE * value);
  extern DB_CONST_C_BIT db_get_bit (const DB_VALUE * value, int *length);
  extern DB_CONST_C_CHAR db_get_char (const DB_VALUE * value, int *length);
  extern DB_CONST_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length);
  extern int db_get_string_size (const DB_VALUE * value);
  extern unsigned short db_get_enum_short (const DB_VALUE * value);
  extern DB_CONST_C_CHAR db_get_enum_string (const DB_VALUE * value);
  extern int db_get_enum_string_size (const DB_VALUE * value);
  extern DB_C_CHAR db_get_method_error_msg (void);
  extern DB_RESULTSET db_get_resultset (const DB_VALUE * value);
  extern int db_get_string_codeset (const DB_VALUE * value);
  extern int db_get_string_collation (const DB_VALUE * value);
  extern int db_get_enum_codeset (const DB_VALUE * value);
  extern int db_get_enum_collation (const DB_VALUE * value);
  extern OID *db_get_oid (const DB_VALUE * value);
  extern DB_TYPE db_value_type (const DB_VALUE * value);
  extern int db_value_precision (const DB_VALUE * value);
  extern int db_value_scale (const DB_VALUE * value);
  extern JSON_DOC *db_get_json_document (const DB_VALUE * value);

  extern int db_make_null (DB_VALUE * value);
  extern int db_make_int (DB_VALUE * value, const int num);
  extern int db_make_float (DB_VALUE * value, const DB_C_FLOAT num);
  extern int db_make_double (DB_VALUE * value, const DB_C_DOUBLE num);
  extern int db_make_object (DB_VALUE * value, DB_C_OBJECT * obj);
  extern int db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey);
  extern int db_make_timestamp (DB_VALUE * value, const DB_C_TIMESTAMP timeval);
  extern int db_make_timestampltz (DB_VALUE * value, const DB_C_TIMESTAMP ts_val);
  extern int db_make_timestamptz (DB_VALUE * value, const DB_C_TIMESTAMPTZ * ts_tz_val);
  extern int db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime);
  extern int db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime);
  extern int db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz);
  extern int db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount);
  extern int db_make_pointer (DB_VALUE * value, DB_C_POINTER ptr);
  extern int db_make_error (DB_VALUE * value, const int errcode);
  extern int db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg);
  extern int db_make_short (DB_VALUE * value, const DB_C_SHORT num);
  extern int db_make_bigint (DB_VALUE * value, const DB_BIGINT num);
  extern int db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale);
  extern int db_make_bit (DB_VALUE * value, const int bit_length, DB_CONST_C_BIT bit_str, const int bit_str_bit_size);
  extern int db_make_varbit (DB_VALUE * value, const int max_bit_length, DB_CONST_C_BIT bit_str,
			     const int bit_str_bit_size);
  extern int db_make_char (DB_VALUE * value, const int char_length, DB_CONST_C_CHAR str, const int char_str_byte_size,
			   const int codeset, const int collation_id);
  extern int db_make_varchar (DB_VALUE * value, const int max_char_length, DB_CONST_C_CHAR str,
			      const int char_str_byte_size, const int codeset, const int collation_id);
  extern int db_make_nchar (DB_VALUE * value, const int nchar_length, DB_CONST_C_NCHAR str,
			    const int nchar_str_byte_size, const int codeset, const int collation_id);
  extern int db_make_varnchar (DB_VALUE * value, const int max_nchar_length, DB_CONST_C_NCHAR str,
			       const int nchar_str_byte_size, const int codeset, const int collation_id);
  extern int db_make_enumeration (DB_VALUE * value, unsigned short index, DB_CONST_C_CHAR str, int size,
				  unsigned char codeset, const int collation_id);
  extern int db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle);

  extern int db_make_string (DB_VALUE * value, DB_CONST_C_CHAR str);
  extern int db_make_string_copy (DB_VALUE * value, DB_CONST_C_CHAR str);

  extern int db_make_oid (DB_VALUE * value, const OID * oid);

  extern int db_make_set (DB_VALUE * value, DB_C_SET * set);
  extern int db_make_multiset (DB_VALUE * value, DB_C_SET * set);
  extern int db_make_sequence (DB_VALUE * value, DB_C_SET * set);
  extern int db_make_collection (DB_VALUE * value, DB_C_SET * set);

  extern int db_make_elo (DB_VALUE * value, DB_TYPE type, const DB_ELO * elo);

  extern int db_make_time (DB_VALUE * value, const int hour, const int minute, const int second);
  extern int db_make_date (DB_VALUE * value, const int month, const int day, const int year);

  extern int db_get_compressed_size (DB_VALUE * value);
  extern void db_set_compressed_string (DB_VALUE * value, char *compressed_string,
					int compressed_size, bool compressed_need_clear);

  extern int db_make_json (DB_VALUE * value, JSON_DOC * json_document, bool need_clear);

#ifdef __cplusplus
}
#endif				/* C++ */

#endif				/* _NO_INLINE_DBTYPE_FUNCTION_ */
