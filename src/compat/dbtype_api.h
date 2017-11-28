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
 *  dbtype_api.h - Holds declarations for API functions.
 */

#ifndef _DBTYPE_API_H_
#define _DBTYPE_API_H_

#include "dbtype_common.h"

extern int db_get_int (const DB_VALUE * value);
extern DB_C_SHORT db_get_short (const DB_VALUE * value);
extern DB_BIGINT db_get_bigint (const DB_VALUE * value);
extern DB_C_CHAR db_get_string (const DB_VALUE * value);
extern DB_C_FLOAT db_get_float (const DB_VALUE * value);
extern DB_C_DOUBLE db_get_double (const DB_VALUE * value);
extern DB_OBJECT *db_get_object (const DB_VALUE * value);
extern DB_COLLECTION *db_get_set (const DB_VALUE * value);
extern DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value);
extern DB_C_POINTER db_get_pointer (const DB_VALUE * value);
extern DB_TIME *db_get_time (const DB_VALUE * value);
extern DB_TIMETZ *db_get_timetz (const DB_VALUE * value);
extern DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value);
extern DB_TIMESTAMPTZ *db_get_timestamptz (const DB_VALUE * value);
extern DB_DATETIME *db_get_datetime (const DB_VALUE * value);
extern DB_DATETIMETZ *db_get_datetimetz (const DB_VALUE * value);
extern DB_DATE *db_get_date (const DB_VALUE * value);
extern DB_MONETARY *db_get_monetary (const DB_VALUE * value);
extern int db_get_error (const DB_VALUE * value);
extern DB_ELO *db_get_elo (const DB_VALUE * value);
extern DB_C_NUMERIC db_get_numeric (const DB_VALUE * value);
extern DB_C_BIT db_get_bit (const DB_VALUE * value, int *length);
extern DB_C_CHAR db_get_char (const DB_VALUE * value, int *length);
extern DB_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length);
extern int db_get_string_size (const DB_VALUE * value);
extern unsigned short db_get_enum_short (const DB_VALUE * value);
extern DB_C_CHAR db_get_enum_string (const DB_VALUE * value);
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

extern int db_make_db_char (DB_VALUE * value, INTL_CODESET codeset, const int collation_id, const char *str,
                            const int size);

extern int db_make_null (DB_VALUE * value);
extern int db_make_int (DB_VALUE * value, const int num);
extern int db_make_float (DB_VALUE * value, const DB_C_FLOAT num);
extern int db_make_double (DB_VALUE * value, const DB_C_DOUBLE num);
extern int db_make_object (DB_VALUE * value, DB_C_OBJECT * obj);
extern int db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey);
extern int db_make_timetz (DB_VALUE * value, const DB_TIMETZ * timetz_value);
extern int db_make_timeltz (DB_VALUE * value, const DB_TIME * time_value);
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
extern int db_make_bit (DB_VALUE * value, const int bit_length, const DB_C_BIT bit_str, const int bit_str_bit_size);
extern int db_make_varbit (DB_VALUE * value, const int max_bit_length, const DB_C_BIT bit_str,
                           const int bit_str_bit_size);
extern int db_make_char (DB_VALUE * value, const int char_length, const DB_C_CHAR str, const int char_str_byte_size,
                         const int codeset, const int collation_id);
extern int db_make_varchar (DB_VALUE * value, const int max_char_length, const DB_C_CHAR str,
                            const int char_str_byte_size, const int codeset, const int collation_id);
extern int db_make_nchar (DB_VALUE * value, const int nchar_length, const DB_C_NCHAR str,
                          const int nchar_str_byte_size, const int codeset, const int collation_id);
extern int db_make_varnchar (DB_VALUE * value, const int max_nchar_length, const DB_C_NCHAR str,
                             const int nchar_str_byte_size, const int codeset, const int collation_id);
extern int db_make_enumeration (DB_VALUE * value, unsigned short index, DB_C_CHAR str, int size,
                                unsigned char codeset, const int collation_id);
extern int db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle);

extern int db_make_string (DB_VALUE * value, const char *str);
extern int db_make_string_copy (DB_VALUE * value, const char *str);

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

#endif             /* _DBTYPE_API_H_ */
