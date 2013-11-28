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
 * db_date.h -  Definitions for the date/time utilities.
 */

#ifndef _DB_DATE_H_
#define _DB_DATE_H_

#ident "$Id$"

#include <time.h>

#include "dbtype.h"

#define db_utime_to_string db_timestamp_to_string
#define db_string_to_utime db_string_to_timestamp
#define db_date_parse_utime db_date_parse_timestamp

extern void db_date_locale_init (void);

/* DB_DATE functions */
extern int db_date_encode (DB_DATE * date, int month, int day, int year);
extern void db_date_decode (const DB_DATE * date, int *monthp,
			    int *dayp, int *yearp);
extern int db_date_weekday (DB_DATE * date);
extern int db_date_to_string (char *buf, int bufsize, DB_DATE * date);
extern bool db_string_check_explicit_date (const char *str, int str_len);
extern int db_string_to_date (const char *buf, DB_DATE * date);
extern int db_string_to_date_ex (const char *buf, int str_len,
				 DB_DATE * date);
extern int db_date_parse_date (char const *str, int str_len, DB_DATE * date);

extern int db_timestamp_to_datetime (DB_TIMESTAMP * utime,
				     DB_DATETIME * datetime);

/* DB_DATETIME functions */
extern int db_datetime_encode (DB_DATETIME * datetime, int month, int day,
			       int year, int hour, int minute, int second,
			       int millisecond);
extern int db_datetime_decode (const DB_DATETIME * datetime, int *month,
			       int *day, int *year, int *hour, int *minute,
			       int *second, int *millisecond);
extern int db_datetime_to_string (char *buf, int bufsize,
				  DB_DATETIME * datetime);
extern int db_datetime_to_string2 (char *buf, int bufsize,
				   DB_DATETIME * datetime);
extern int db_string_to_datetime (const char *str, DB_DATETIME * datetime);
extern int db_string_to_datetime_ex (const char *str, int str_len,
				     DB_DATETIME * datetime);
extern int db_date_parse_datetime_parts (char const *str, int str_len,
					 DB_DATETIME * date,
					 bool * is_explicit_time,
					 bool * has_explicit_msec,
					 bool * fits_as_timestamp,
					 char const **endp);
extern int db_date_parse_datetime (char const *str, int str_len,
				   DB_DATETIME * datetime);
extern int db_subtract_int_from_datetime (DB_DATETIME * dt1, DB_BIGINT i2,
					  DB_DATETIME * result_datetime);
extern int db_add_int_to_datetime (DB_DATETIME * datetime, DB_BIGINT i2,
				   DB_DATETIME * result_datetime);
/* DB_TIMESTAMP functions */
extern int db_timestamp_encode (DB_TIMESTAMP * utime,
				DB_DATE * date, DB_TIME * timeval);
extern void db_timestamp_decode (const DB_TIMESTAMP * utime,
				 DB_DATE * date, DB_TIME * timeval);
extern int db_timestamp_to_string (char *buf, int bufsize,
				   DB_TIMESTAMP * utime);
extern int db_string_to_timestamp (const char *buf, DB_TIMESTAMP * utime);
extern int db_string_to_timestamp_ex (const char *buf, int buf_len,
				      DB_TIMESTAMP * utime);
extern int db_date_parse_timestamp (char const *str, int str_len,
				    DB_TIMESTAMP * utime);

/* DB_TIME functions */
extern int db_time_encode (DB_TIME * timeval,
			   int hour, int minute, int second);
extern void db_time_decode (DB_TIME * timeval, int *hourp,
			    int *minutep, int *secondp);
extern int db_time_to_string (char *buf, int bufsize, DB_TIME * dbtime);
extern bool db_string_check_explicit_time (const char *str, int str_len);
extern int db_string_to_time (const char *buf, DB_TIME * dbtime);
extern int db_string_to_time_ex (const char *buf, int buf_len,
				 DB_TIME * dbtime);
extern int db_date_parse_time (char const *str, int str_len, DB_TIME * time,
			       int *milisec);

/* Unix-like functions */
extern time_t db_mktime (DB_DATE * date, DB_TIME * timeval);
extern int db_strftime (char *s, int smax, const char *fmt,
			DB_DATE * date, DB_TIME * timeval);
extern void db_localtime (time_t * epoch_time,
			  DB_DATE * date, DB_TIME * timeval);
extern void db_localdatetime (time_t * epoch_time, DB_DATETIME * datetime);


/* generic calculation functions */
extern int julian_encode (int m, int d, int y);
extern void julian_decode (int jul, int *monthp, int *dayp,
			   int *yearp, int *weekp);
extern int day_of_week (int jul_day);
extern bool is_leap_year (int year);
extern int time_encode (int hour, int minute, int second);
extern void time_decode (int timeval, int *hourp, int *minutep, int *secondp);

extern int db_tm_encode (struct tm *c_time_struct,
			 DB_DATE * date, DB_TIME * timeval);
extern int db_get_day_of_year (int year, int month, int day);
extern int db_get_day_of_week (int year, int month, int day);
extern int db_get_week_of_year (int year, int month, int day, int mode);
extern int db_check_time_date_format (const char *format_s);
extern int db_add_weeks_and_days_to_date (int *day, int *month, int *year,
					  int weeks, int day_week);

#endif /* _DB_DATE_H_ */
