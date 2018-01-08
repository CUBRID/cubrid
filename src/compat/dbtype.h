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
 * dbtype.h - Definitions related to the memory representations of database
 * attribute values. This is an application interface file. It should contain
 * only definitions available to CUBRID customer applications.
 */

#ifndef _DBTYPE_H_
#define _DBTYPE_H_

#ident "$Id$"

#include "config.h"

#include "system_parameter.h"
#include "dbdef.h"
#include "error_manager.h"
#include "system.h"
#include "dbtype_def.h"

typedef enum
{
  SMALL_STRING,
  MEDIUM_STRING,
  LARGE_STRING
} STRING_STYLE;

#define DB_CURRENCY_DEFAULT db_get_currency_default()

#define db_set db_collection

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

#define DB_MAKE_TIMETZ(value, timetz_value) \
    db_make_timetz(value, timetz_value)

#define DB_MAKE_TIMELTZ(value, time_value) \
    db_make_timeltz(value, time_value)

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

#define DB_MAKE_VARBIT(value, max_bit_length, bit_str, bit_str_bit_size)\
        db_make_varbit(value, max_bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_CHAR(value, char_length, str, char_str_byte_size, \
		     codeset, collation) \
        db_make_char(value, char_length, str, char_str_byte_size, \
		     codeset, collation)

#define DB_MAKE_VARCHAR(value, max_char_length, str, char_str_byte_size, \
		        codeset, collation) \
        db_make_varchar(value, max_char_length, str, char_str_byte_size, \
			codeset, collation)

#define DB_MAKE_STRING(value, str) db_make_string(value, str)

#define DB_MAKE_NCHAR(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation) \
        db_make_nchar(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation)

#define DB_MAKE_VARNCHAR(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)\
        db_make_varnchar(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)

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
#define DB_GET_TIMETZ(v) db_get_timetz(v)
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

  /* Macros from dbval.h */

#define DB_NEED_CLEAR(v) \
      ((!DB_IS_NULL(v) \
	&& ((v)->need_clear == true \
	    || ((DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARCHAR || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARNCHAR) \
		 && (v)->data.ch.info.compressed_need_clear != 0))))

#define DB_GET_COMPRESSED_STRING(v) \
      ((DB_VALUE_DOMAIN_TYPE(v) != DB_TYPE_VARCHAR) && (DB_VALUE_DOMAIN_TYPE(v) != DB_TYPE_VARNCHAR) \
	? NULL : (v)->data.ch.medium.compressed_buf)


#define DB_GET_STRING_PRECISION(v) \
    ((v)->domain.char_info.length)

#define DB_GET_ENUMERATION(v) \
      ((v)->data.enumeration)
#define DB_GET_ENUM_ELEM_SHORT(elem) \
      ((elem)->short_val)
#define DB_GET_ENUM_ELEM_DBCHAR(elem) \
      ((elem)->str_val)
#define DB_GET_ENUM_ELEM_STRING(elem) \
      ((elem)->str_val.medium.buf)
#define DB_GET_ENUM_ELEM_STRING_SIZE(elem) \
      ((elem)->str_val.medium.size)

#define DB_GET_ENUM_ELEM_CODESET(elem) \
      ((elem)->str_val.info.codeset)

#define DB_SET_ENUM_ELEM_CODESET(elem, cs) \
      ((elem)->str_val.info.codeset = (cs))

#define DB_SET_ENUM_ELEM_SHORT(elem, sv) \
      ((elem)->short_val = (sv))
#define DB_SET_ENUM_ELEM_STRING(elem, str) \
      ((elem)->str_val.medium.buf = (str),  \
       (elem)->str_val.info.style = MEDIUM_STRING)
#define DB_SET_ENUM_ELEM_STRING_SIZE(elem, sz) \
      ((elem)->str_val.medium.size = (sz))

#define DB_PULL_SEQUENCE(v) db_get_set(v)

#define DB_GET_STRING_SAFE(v) \
      ((DB_IS_NULL (v) \
	|| DB_VALUE_DOMAIN_TYPE (v) == DB_TYPE_ERROR) ? "" \
       : ((assert (DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_CHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARNCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_NCHAR \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_VARBIT \
		   || DB_VALUE_DOMAIN_TYPE(v) == DB_TYPE_BIT)), \
	  (v)->data.ch.medium.buf))

#define DB_GET_NUMERIC_PRECISION(val) \
    ((val)->domain.numeric_info.precision)

#define DB_GET_NUMERIC_SCALE(val) \
    ((val)->domain.numeric_info.scale)

#define DB_GET_STRING_PRECISION(v) \
    ((v)->domain.char_info.length)

#define DB_GET_BIT_PRECISION(v) \
    ((v)->domain.char_info.length)

#define DB_GET_JSON_SCHEMA(v) \
	((v)->data.json.schema_raw)

#define db_get_json_schema(v) DB_GET_JSON_SCHEMA(v)
#define DB_GET_JSON_RAW_BODY(v) db_get_json_raw_body(v)

#ifdef __cplusplus
extern "C"
{
#endif

  extern DB_TYPE setobj_type (COL * set);
  /********************************************************/
  /* From elo.h */

  extern void elo_init_structure (DB_ELO * elo);
  /********************************************************/
  /* From db_date.h */

  extern int db_date_encode (DB_DATE * date, int month, int day, int year);
  extern int db_time_encode (DB_TIME * timeval, int hour, int minute, int second);
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
  extern int db_value_put_null (DB_VALUE * value);
  extern int db_value_put (DB_VALUE * value, const DB_TYPE_C c_type, void *input, const int input_length);
  extern bool db_value_type_is_collection (const DB_VALUE * value);
  extern bool db_value_type_is_numeric (const DB_VALUE * value);
  extern bool db_value_type_is_bit (const DB_VALUE * value);
  extern bool db_value_type_is_char (const DB_VALUE * value);
  extern bool db_value_type_is_internal (const DB_VALUE * value);
  extern int db_value_get (DB_VALUE * value, const DB_TYPE_C type, void *buf, const int buflen, int *transferlen,
			   int *outputlen);
  extern int db_value_size (const DB_VALUE * value, DB_TYPE_C type, int *size);
  extern int db_value_char_size (const DB_VALUE * value, int *size);
  extern DB_CURRENCY db_value_get_monetary_currency (const DB_VALUE * value);
  extern double db_value_get_monetary_amount_as_double (const DB_VALUE * value);
  extern int db_value_put_monetary_currency (DB_VALUE * value, const DB_CURRENCY type);
  extern int db_value_put_monetary_amount_as_double (DB_VALUE * value, const double amount);
  extern int db_value_alter_type (DB_VALUE * value, DB_TYPE type);

/*
 * DB_MAKE_ value constructors.
 * These macros are provided to make the construction of DB_VALUE
 * structures easier.  They will fill in the fields from the supplied
 * arguments. It is not necessary to use these macros but is usually more
 * convenient.
 */
  extern int db_value_put_encoded_time (DB_VALUE * value, const DB_TIME * time_value);
  extern int db_value_put_encoded_date (DB_VALUE * value, const DB_DATE * date_value);
  extern int db_value_put_numeric (DB_VALUE * value, DB_C_NUMERIC num);
  extern int db_value_put_bit (DB_VALUE * value, DB_C_BIT str, int size);
  extern int db_value_put_varbit (DB_VALUE * value, DB_C_BIT str, int size);
  extern int db_value_put_char (DB_VALUE * value, DB_C_CHAR str, int size);
  extern int db_value_put_varchar (DB_VALUE * value, DB_C_CHAR str, int size);
  extern int db_value_put_nchar (DB_VALUE * value, DB_C_NCHAR str, int size);
  extern int db_value_put_varnchar (DB_VALUE * value, DB_C_NCHAR str, int size);

  extern DB_CURRENCY db_get_currency_default (void);

/* Collection functions */
  extern DB_COLLECTION *db_col_create (DB_TYPE type, int size, DB_DOMAIN * domain);
  extern DB_COLLECTION *db_col_copy (DB_COLLECTION * col);
  extern int db_col_filter (DB_COLLECTION * col);
  extern int db_col_free (DB_COLLECTION * col);
  extern int db_col_coerce (DB_COLLECTION * col, DB_DOMAIN * domain);

  extern int db_col_size (DB_COLLECTION * col);
  extern int db_col_cardinality (DB_COLLECTION * col);
  extern DB_TYPE db_col_type (DB_COLLECTION * col);
  extern DB_DOMAIN *db_col_domain (DB_COLLECTION * col);
  extern int db_col_ismember (DB_COLLECTION * col, DB_VALUE * value);
  extern int db_col_find (DB_COLLECTION * col, DB_VALUE * value, int starting_index, int *found_index);
  extern int db_col_add (DB_COLLECTION * col, DB_VALUE * value);
  extern int db_col_drop (DB_COLLECTION * col, DB_VALUE * value, int all);
  extern int db_col_drop_element (DB_COLLECTION * col, int element_index);

  extern int db_col_drop_nulls (DB_COLLECTION * col);

  extern int db_col_get (DB_COLLECTION * col, int element_index, DB_VALUE * value);
  extern int db_col_put (DB_COLLECTION * col, int element_index, DB_VALUE * value);
  extern int db_col_insert (DB_COLLECTION * col, int element_index, DB_VALUE * value);

  extern int db_col_print (DB_COLLECTION * col);
  extern int db_col_fprint (FILE * fp, DB_COLLECTION * col);

/* Set and sequence functions.
   These are now obsolete. Please use the generic collection functions
   "db_col*" instead */
  extern int db_set_compare (const DB_VALUE * value1, const DB_VALUE * value2);
  extern DB_COLLECTION *db_set_create (DB_OBJECT * classobj, const char *name);
  extern DB_COLLECTION *db_set_create_basic (DB_OBJECT * classobj, const char *name);
  extern DB_COLLECTION *db_set_create_multi (DB_OBJECT * classobj, const char *name);
  extern DB_COLLECTION *db_seq_create (DB_OBJECT * classobj, const char *name, int size);
  extern int db_set_free (DB_COLLECTION * set);
  extern int db_set_filter (DB_COLLECTION * set);
  extern int db_set_add (DB_COLLECTION * set, DB_VALUE * value);
  extern int db_set_get (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_set_drop (DB_COLLECTION * set, DB_VALUE * value);
  extern int db_set_size (DB_COLLECTION * set);
  extern int db_set_cardinality (DB_COLLECTION * set);
  extern int db_set_ismember (DB_COLLECTION * set, DB_VALUE * value);
  extern int db_set_isempty (DB_COLLECTION * set);
  extern int db_set_has_null (DB_COLLECTION * set);
  extern int db_set_print (DB_COLLECTION * set);
  extern DB_TYPE db_set_type (DB_COLLECTION * set);
  extern DB_COLLECTION *db_set_copy (DB_COLLECTION * set);
  extern int db_seq_get (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_seq_put (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_seq_insert (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_seq_drop (DB_COLLECTION * set, int element_index);
  extern int db_seq_size (DB_COLLECTION * set);
  extern int db_seq_cardinality (DB_COLLECTION * set);
  extern int db_seq_print (DB_COLLECTION * set);
  extern int db_seq_find (DB_COLLECTION * set, DB_VALUE * value, int element_index);
  extern int db_seq_free (DB_SEQ * seq);
  extern int db_seq_filter (DB_SEQ * seq);
  extern DB_SEQ *db_seq_copy (DB_SEQ * seq);

  extern DB_DOMAIN *db_type_to_db_domain (DB_TYPE type);
  extern const char *db_default_expression_string (DB_DEFAULT_EXPR_TYPE default_expr_type);

  extern int db_get_deep_copy_of_json (const DB_JSON * src, DB_JSON * dst);
  extern int db_init_db_json_pointers (DB_JSON * val);
  extern int db_convert_json_into_scalar (const DB_VALUE * src, DB_VALUE * dest);
  extern bool db_is_json_value_type (DB_TYPE type);
  extern bool db_is_json_doc_type (DB_TYPE type);

/* Use the inline version of the functions. */
#include "dbtype_function.i"
#ifdef __cplusplus
}
#endif

#endif				/* _DBTYPE_H_ */
