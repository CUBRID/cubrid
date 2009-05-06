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
 * cnv.h - String conversion function header
 */

#ifndef _CNV_H_
#define _CNV_H_

#ident "$Id$"

#include "dbtype.h"
#include "condition_handler.h"

extern const char *db_string_value (const char *string, const char *format,
				    DB_VALUE * value);

extern DB_VALUE *db_string_to_value (const char *string, const char *format,
				     DB_TYPE type, ...);

extern int db_value_string (const DB_VALUE * value, const char *format,
			    char *string, int max_size);

extern const char *db_value_to_string (const DB_VALUE * value,
				       const char *format);

extern const char *db_string_date (const char *date_string,
				   const char *date_format,
				   DB_DATE * the_date);

extern int db_date_string (const DB_DATE * the_date, const char *date_format,
			   char *string, int max_size);

extern const char *db_string_double (const char *double_string,
				     const char *double_format,
				     double *the_double);

extern int db_double_string (double the_double, const char *double_format,
			     char *string, int max_size);

extern const char *db_string_float (const char *float_string,
				    const char *float_format,
				    float *the_float);

extern int db_float_string (float the_float, const char *float_format,
			    char *string, int max_size);

extern const char *db_string_integer (const char *integer_string,
				      const char *integer_format,
				      int *the_integer);

extern int db_integer_string (int the_integer, const char *integer_format,
			      char *string, int max_size);

extern const char *db_string_bigint (const char *bitint_string,
				     const char *bigint_format,
				     DB_BIGINT * the_bigint);
extern int db_bigint_string (DB_BIGINT the_bigint, const char *bigint_format,
			     char *string, int max_size);

extern const char *db_string_monetary (const char *monetary_string,
				       const char *monetary_format,
				       DB_MONETARY * the_monetary);

extern int db_monetary_string (const DB_MONETARY * the_monetary,
			       const char *monetary_format, char *string,
			       int max_size);

extern const char *db_string_short (const char *short_string,
				    const char *short_format,
				    short *the_short);

extern int db_short_string (short the_short, const char *short_format,
			    char *string, int max_size);

extern const char *db_string_time (const char *time_string,
				   const char *time_format,
				   DB_TIME * the_time);

extern int db_time_string (const DB_TIME * the_time, const char *time_format,
			   char *string, int max_size);

extern const char *db_string_timestamp (const char *timestamp_string,
					const char *timestamp_format,
					DB_TIMESTAMP * the_time);

extern int db_timestamp_string (const DB_TIMESTAMP * the_timestamp,
				const char *timestamp_format, char *string,
				int max_size);

extern const char *db_string_datetime (const char *datetime_string,
				       const char *datetime_format,
				       DB_DATETIME * the_datetime);

extern int db_datetime_string (const DB_DATETIME * the_datetime,
			       const char *datetime_format, char *string,
			       int max_size);

extern const char *db_string_bit (const char *string, const char *bit_format,
				  DB_VALUE * the_db_bit);

extern int db_bit_string (const DB_VALUE * the_db_bit,
			  const char *bit_format, char *string, int max_size);

extern const char *db_string_numeric (const char *string,
				      const char *numeric_format,
				      DB_VALUE * the_numeric);

extern int db_numeric_string (const DB_VALUE * the_numeric,
			      const char *numeric_format, char *string,
			      int max_size);

extern int db_validate_format (const char *format, DB_TYPE type);

extern const char *db_reformat (const char *string, int *pos,
				const char *format, DB_TYPE type, ...);

/* cleanup function, should only be called by bo_shutdown */
extern void cnv_cleanup (void);

#endif /* _CNV_H_ */
