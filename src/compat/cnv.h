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
 * cnv.h - String conversion function header
 */

#ifndef _CNV_H_
#define _CNV_H_

#ident "$Id$"

#include "dbtype_def.h"
#include "condition_handler.h"

extern const char *db_string_value (const char *string, int str_size, const char *format, DB_VALUE * value);
#if defined(ENABLE_UNUSED_FUNCTION)
extern DB_VALUE *db_string_to_value (const char *string, const char *format, DB_TYPE type, ...);

extern int db_value_string (const DB_VALUE * value, const char *format, char *string, int max_size);

extern const char *db_value_to_string (const DB_VALUE * value, const char *format);
extern int db_date_string (const DB_DATE * the_date, const char *date_format, char *string, int max_size);
extern int db_double_string (double the_double, const char *double_format, char *string, int max_size);
extern int db_float_string (float the_float, const char *float_format, char *string, int max_size);
extern int db_integer_string (int the_integer, const char *integer_format, char *string, int max_size);
extern int db_bigint_string (DB_BIGINT the_bigint, const char *bigint_format, char *string, int max_size);
extern int db_monetary_string (const DB_MONETARY * the_monetary, const char *monetary_format, char *string,
			       int max_size);
extern int db_short_string (short the_short, const char *short_format, char *string, int max_size);
extern int db_validate_format (const char *format, DB_TYPE type);
#endif

extern const char *db_string_date (const char *date_string, const char *date_format, DB_DATE * the_date);
extern const char *db_string_double (const char *double_string, const char *double_format, double *the_double);
extern const char *db_string_float (const char *float_string, const char *float_format, float *the_float);
extern const char *db_string_integer (const char *integer_string, const char *integer_format, int *the_integer);
extern const char *db_string_bigint (const char *bitint_string, const char *bigint_format, DB_BIGINT * the_bigint);
extern const char *db_string_monetary (const char *monetary_string, const char *monetary_format,
				       DB_MONETARY * the_monetary);
extern const char *db_string_short (const char *short_string, const char *short_format, short *the_short);
extern const char *db_string_time (const char *time_string, const char *time_format, DB_TIME * the_time);
extern int db_time_string (const DB_TIME * the_time, const char *time_format, char *string, int max_size);
extern const char *db_string_timestamp (const char *timestamp_string, const char *timestamp_format,
					DB_TIMESTAMP * the_time);
extern int db_timestamp_string (const DB_TIMESTAMP * the_timestamp, const char *timestamp_format, char *string,
				int max_size);
extern const char *db_string_datetime (const char *datetime_string, const char *datetime_format,
				       DB_DATETIME * the_datetime);
extern int db_datetime_string (const DB_DATETIME * the_datetime, const char *datetime_format, char *string,
			       int max_size);
extern const char *db_string_bit (const char *string, const char *bit_format, DB_VALUE * the_db_bit);
extern int db_bit_string (const DB_VALUE * the_db_bit, const char *bit_format, char *string, int max_size);
extern const char *db_string_numeric (const char *string, const char *numeric_format, DB_VALUE * the_numeric);
extern int db_numeric_string (const DB_VALUE * the_numeric, const char *numeric_format, char *string, int max_size);

/* cleanup function, should only be called by bo_shutdown */
extern void cnv_cleanup (void);

#endif /* _CNV_H_ */
