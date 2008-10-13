/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * db_date.h -  Definitions for the date/time utilities.
 */

#ifndef _DB_DATE_H_
#define _DB_DATE_H_

#ident "$Id$"

#include <time.h>

#include "dbtype.h"

#define db_utime_to_string db_timestamp_to_string
#define db_string_to_utime db_string_to_timestamp

/* DB_DATE functions */
extern void db_date_encode (DB_DATE * date, int month, int day, int year);
extern void db_date_decode (DB_DATE * date, int *monthp,
			    int *dayp, int *yearp);
extern int db_date_weekday (DB_DATE * date);
extern int db_date_to_string (char *buf, int bufsize, DB_DATE * date);
extern int db_string_to_date (const char *buf, DB_DATE * date);

/* DB_TIMESTAMP functions */
extern int db_timestamp_encode (DB_TIMESTAMP * utime,
				DB_DATE * date, DB_TIME * timeval);
extern void db_timestamp_decode (DB_TIMESTAMP * utime,
				 DB_DATE * date, DB_TIME * timeval);
extern int db_timestamp_to_string (char *buf, int bufsize,
				   DB_TIMESTAMP * utime);
extern int db_string_to_timestamp (const char *buf, DB_TIMESTAMP * utime);

/* DB_TIME functions */
extern void db_time_encode (DB_TIME * timeval,
			    int hour, int minute, int second);
extern void db_time_decode (DB_TIME * timeval, int *hourp,
			    int *minutep, int *secondp);
extern int db_time_to_string (char *buf, int bufsize, DB_TIME * dbtime);
extern int db_string_to_time (const char *buf, DB_TIME * dbtime);

/* Unix-like functions */
extern time_t db_mktime (DB_DATE * date, DB_TIME * timeval);
extern int db_strftime (char *s, int smax, const char *fmt,
			DB_DATE * date, DB_TIME * timeval);
extern void db_localtime (time_t * epoch_time,
			  DB_DATE * date, DB_TIME * timeval);

/* generic calculation functions */
extern int julian_encode (int m, int d, int y);
extern void julian_decode (int jul, int *monthp, int *dayp,
			   int *yearp, int *weekp);
extern int day_of_week (int jul_day);

extern int time_encode (int hour, int minute, int second);
extern void time_decode (int timeval, int *hourp, int *minutep, int *secondp);

extern int db_tm_encode (struct tm *c_time_struct,
			 DB_DATE * date, DB_TIME * timeval);

#endif /* _DB_DATE_H_ */
