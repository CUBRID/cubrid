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
#include "error_manager.h"
#include "system.h"
#include "dbtype_def.h"
#include "elo.h"
#include "object_domain.h"
#include "language_support.h"
#include "intl_support.h"
#include "memory_alloc.h"

#define DB_CURRENCY_DEFAULT db_get_currency_default()

#define db_set db_collection

#define db_make_utime db_make_timestamp

#define DB_VALUE_CLONE_AS_NULL(src_value, dest_value)                   \
  do {                                                                  \
    if ((db_value_domain_init(dest_value,                               \
                              db_value_domain_type(src_value),          \
                              db_value_precision(src_value),            \
                              db_value_scale(src_value)))               \
        == NO_ERROR)                                                    \
      (void)db_value_put_null(dest_value);                              \
  } while (0)

#define db_get_collection db_get_set
#define db_get_utime db_get_timestamp

#define DB_IS_NULL(value)               db_value_is_null(value)

#define DB_VALUE_DOMAIN_TYPE(value)     db_value_domain_type(value)

#define DB_VALUE_TYPE(value)            db_value_type(value)
#define DB_VALUE_PRECISION(value)       db_value_precision(value)
#define DB_VALUE_SCALE(value)           db_value_scale(value)

#define DB_SET_COMPRESSED_STRING(value, compressed_string, compressed_size, compressed_need_clear) \
	db_set_compressed_string(value, compressed_string, compressed_size, compressed_need_clear)

#define DB_TRIED_COMPRESSION(value) (db_get_compressed_size(value) != DB_NOT_YET_COMPRESSED)

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
  extern void db_value_domain_init_default (DB_VALUE * value, const DB_TYPE type);
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
   * These are now obsolete. Please use the generic collection functions "db_col*" instead.
   */
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
  extern char *db_get_json_raw_body (const DB_VALUE * value);

  extern bool db_value_is_corrupted (const DB_VALUE * value);

  extern int db_json_val_from_str (const char *raw_str, const int str_size, DB_VALUE * json_val);

/* Use the inline version of the functions. */
#include "dbtype_function.i"

#ifdef __cplusplus

  // todo - find a better solution
  inline void pr_share_value (DB_VALUE * src, DB_VALUE * dst)
  {
    if (src == NULL || dst == NULL || src == dst)
      {
	// do nothing
	return;
      }
     *dst = *src;
    dst->need_clear = false;

    DB_TYPE type = db_value_domain_type (src);
    if (type == DB_TYPE_STRING || type == DB_TYPE_VARNCHAR)
      {
	dst->data.ch.info.compressed_need_clear = false;
      }

    if ((TP_IS_SET_TYPE (type) || type == DB_TYPE_VOBJ) && !DB_IS_NULL (src))
      {
	src->data.set->ref_count++;
      }
  }
}
#endif

#endif /* _DBTYPE_H_ */
