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
 * db_date.c - Julian date conversion routines and added functions for
 *             handling relative time values.
 */

#ident "$Id$"

#include <stdio.h>
#include <math.h>
#include <time.h>

#include "system.h"
#include "tz_support.h"
#include "db_date.h"

#include <assert.h>

#include "error_manager.h"
#include "chartype.h"
#include "tz_support.h"
#include "numeric_opfunc.h"
#include "object_representation.h"
#include "dbtype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/* used in conversion to julian */
#define IGREG1     	(15 + 31L * (10 + 12L * 1582))
/* used in conversion from julian */
#define IGREG2     	2299161	/* 10/15/1582 */
/* used in special zero date */
#define IGREG_SPECIAL 	0	/* 01/01/-4713 DATE || 01/01/1970 00:00:00 TIMESTAMP */

#define FLOOR(d1) (int) (d1 + 4000000.0) - 4000000

#define YBIAS   	1900

#define MMDDYYYY        0
#define YYYYMMDD        1

/* affects DATE/TIME parsing (from string) for mysql compatibility */
#define DATETIME_FIELD_LIMIT 1000000

typedef struct ampm_buf
{
  char str[10];
  int len;
} AMPM_BUF;

/* should be regarded as const */
static char local_am_str[10] = "am", local_pm_str[10] = "pm";
static int local_am_strlen = 2, local_pm_strlen = 2;

static void fill_local_ampm_str (char str[10], bool am);
static void decode_time (int timeval, int *hourp, int *minutep, int *secondp);
static int encode_time (int hour, int minute, int second);
static void decode_mtime (int mtimeval, int *hourp, int *minutep, int *secondp, int *millisecondp);
static unsigned int encode_mtime (int hour, int minute, int second, int millisecond);
static int init_tm (struct tm *);
static int get_current_year (void);
static const char *parse_date (const char *buf, int buf_len, DB_DATE * date);
static const char *parse_time (const char *buf, int buf_len, DB_TIME * mtime);
static const char *parse_mtime (const char *buf, int buf_len, unsigned int *mtime, bool * is_msec, bool * is_explicit);
static const char *parse_for_timestamp (const char *buf, int buf_len, DB_DATE * date, DB_TIME * time, bool allow_msec);
static const char *parse_datetime (const char *buf, int buf_len, DB_DATETIME * datetime);
static char const *parse_date_separated (char const *str, char const *strend, DB_DATE * date, char const **syntax_check,
					 int *year_digits, char *sep_ch);
static char const *parse_mtime_separated (char const *str, char const *strend, unsigned int *mtime,
					  char const **syntax_check, int *time_parts, char *sep_ch,
					  bool * has_explicit_msec, bool is_datetime);
static char const *parse_explicit_mtime_separated (char const *str, char const *strend, unsigned int *mtime,
						   char const **syntax_check, bool * has_explicit_msec);
static char const *parse_explicit_mtime_compact (char const *str, char const *strend, unsigned int *mtime);
static char const *parse_timestamp_compact (char const *str, char const *strend, DB_DATE * date, unsigned int *mtime,
					    bool * has_explicit_time, bool * has_explicit_msec);
static char const *parse_timedate_separated (char const *str, char const *strend, DB_DATE * date, unsigned int *mtime,
					     char const **syntax_check, bool * has_explicit_msec);
static int get_end_of_week_one_of_year (int year, int mode);
static bool is_local_am_str (const char *p, const char *p_end);
static bool is_local_pm_str (const char *p, const char *p_end);
static int db_timestamp_encode_w_reg (const DB_DATE * date, const DB_TIME * timeval, const TZ_REGION * tz_region,
				      DB_TIMESTAMP * utime, TZ_ID * dest_tz_id);

/*
 * julian_encode() - Generic routine for calculating a julian date given
 *    separate month/day/year values.
 * return : encoded julian date
 * m(in): month (1 - 12)
 * d(in): day(1 - 31)
 * y(in): year
 */
int
julian_encode (int m, int d, int y)
{
  int jul;
  int ja, jy, jm;

  if (y == 0)
    {
      return (0L);
    }

  if (m == 1 && d == 1 && y == -4713)
    {
      /* used for special meaning (IGREG_SPECIAL) */
      return (0L);
    }

  if (y < 0)
    {
      ++y;
    }

  if (m > 2)
    {
      jy = y;
      jm = m + 1;
    }
  else
    {
      jy = y - 1;
      jm = m + 13;
    }

  jul = (int) (FLOOR (365.25 * jy) + FLOOR (30.6001 * jm) + d + 1720995);

  /*
   * Test whether to convert to gregorian calander, started Oct 15, 1582
   */
  if ((d + 31L * (m + 12L * y)) >= IGREG1)
    {
      ja = (int) (0.01 * jy);
      jul += 2 - ja + (int) (0.25 * ja);
    }

  return (jul);
}

/*
 * day_of_week() - Returns the day of the week, relative to Sunday, which
 *                 this day occurred on.
 * return :
 *                 return value          day of the week
 *                 ------------          ---------------
 *                     0                  Sunday
 *                     1                  Monday
 *                     2                  Tuesday
 *                     3                  Wednesday
 *                     4                  Thursday
 *                     5                  Friday
 *                     6                  Saturday
 *
 * jul_day(in): the julian day to reason over
 */
int
day_of_week (int jul_day)
{
  if (jul_day == IGREG_SPECIAL)
    {
      return IGREG_SPECIAL;
    }
  return ((int) ((jul_day + 1) % 7));
}

/*
 * julian_decode() - Generic function for decoding a julian date into
 *    interesting pieces.
 * return : void
 * jul(in): encoded julian value
 * monthp(out): pointer to month value
 * dayp(out): pointer to day value
 * yearp(out): pointer to year value
 * weekp(out): pointer to weekday value
 */
void
julian_decode (int jul, int *monthp, int *dayp, int *yearp, int *weekp)
{
  int ja, jalpha, jb, jc, jd, je;
  int day, month, year;

  if (jul >= IGREG2)
    {
      /* correction if Gregorian conversion occurred */
      jalpha = (int) (((float) (jul - 1867216) - 0.25) / 36524.25);
      ja = jul + 1 + jalpha - (int) (0.25 * jalpha);
    }
  else
    {
      ja = jul;			/* else no conversion necessary */
    }

  if (ja == IGREG_SPECIAL)
    {
      month = day = year = 0;
    }
  else
    {
      jb = ja + 1524;
      jc = (int) (6680.0 + ((float) (jb - 2439870) - 122.1) / 365.25);
      jd = (int) (365 * jc + (0.25 * jc));
      je = (int) ((jb - jd) / 30.6001);

      day = jb - jd - (int) (30.6001 * je);
      month = je - 1;
      if (month > 12)
	{
	  month -= 12;
	}
      year = jc - 4715;
      if (month > 2)
	{
	  --year;
	}
      if (year <= 0)
	{
	  --year;
	}
    }

  if (monthp != NULL)
    {
      *monthp = month;
    }
  if (dayp != NULL)
    {
      *dayp = day;
    }
  if (yearp != NULL)
    {
      *yearp = year;
    }
  if (weekp != NULL)
    {
      if (jul == IGREG_SPECIAL)
	{
	  *weekp = 0;
	}
      *weekp = (int) ((jul + 1) % 7);
    }
}

/*
 * DB_DATE FUNCTIONS
 */

/*
 * db_date_encode() -
 * return : error code
 * date(out):
 * month(in): month (1 - 12)
 * day(in): day (1 - 31)
 * year(in):
 */
int
db_date_encode (DB_DATE * date, int month, int day, int year)
{
  DB_DATE tmp;
  int tmp_month, tmp_day, tmp_year;

  if (date == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  *date = 0;
  if (year < 0 || year > 9999 || month < 0 || month > 12 || day < 0 || day > 31)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }
  else
    {
      tmp = julian_encode (month, day, year);
      /*
       * Now turn around and decode the produced date; if it doesn't
       * jive with the parameters that came in, someone tried to give
       * us some bogus data.
       */
      julian_decode (tmp, &tmp_month, &tmp_day, &tmp_year, NULL);
      if (month == tmp_month && day == tmp_day && year == tmp_year)
	{
	  *date = tmp;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	  return ER_DATE_CONVERSION;
	}
    }

  return NO_ERROR;
}

/*
 * db_date_weekday() - Please refer to the day_of_week() function
 * return : weekday
 * date(in): pointer to DB_DATE
 */
int
db_date_weekday (DB_DATE * date)
{
  int retval;

  retval = (day_of_week (*date));
  return (retval);
}

/*
 * db_date_decode() - Decodes a DB_DATE value into interesting sub fields.
 * return : void
 * date(in): pointer to DB_DATE
 * monthp(out): pointer to month
 * dayp(out): pointer to day
 * yearp(out): pointer to year
 */
void
db_date_decode (const DB_DATE * date, int *monthp, int *dayp, int *yearp)
{
  julian_decode (*date, monthp, dayp, yearp, NULL);
}

/*
 * DB_TIME FUNCTIONS
 */

/*
 * encode_time() -
 * return : time value
 * hour(in): hour
 * minute(in): minute
 * second(in): second
 */
static int
encode_time (int hour, int minute, int second)
{
  return ((((hour * 60) + minute) * 60) + second);
}

/*
 * db_time_encode() - Converts hour/minute/second into an encoded relative
 *    time value.
 * return : error code
 * timeval(out) : time value
 * hour(in): hour
 * minute(in): minute
 * second(in): second
 */
int
db_time_encode (DB_TIME * timeval, int hour, int minute, int second)
{
  if (timeval == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
      return ER_TIME_CONVERSION;
    }

  if (hour >= 0 && minute >= 0 && second >= 0 && hour < 24 && minute < 60 && second < 60)
    {
      *timeval = (((hour * 60) + minute) * 60) + second;
    }
  else
    {
      *timeval = -1;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
      return ER_TIME_CONVERSION;
    }

  return NO_ERROR;
}

/*
 * db_time_decode() - Converts encoded time into hour/minute/second values.
 * return : void
 * time(in): encoded relative time value
 * hourp(out): hour pointer
 * minutep(out) : minute pointer
 * secondp(out) : second pointer
 */
static void
decode_time (int timeval, int *hourp, int *minutep, int *secondp)
{
  int minutes, hours, seconds;

  seconds = timeval % 60;
  minutes = (timeval / 60) % 60;
  hours = (timeval / 3600) % 24;

  if (hourp != NULL)
    {
      *hourp = hours;
    }
  if (minutep != NULL)
    {
      *minutep = minutes;
    }
  if (secondp != NULL)
    {
      *secondp = seconds;
    }
}

/*
 * db_time_decode() - Converts encoded time into hour/minute/second values.
 * return: void
 * timeval(in) : encoded relative time value
 * hourp(out) : hour pointer
 * minutep(out) : minute pointer
 * secondp(out) : second pointer
 */
void
db_time_decode (DB_TIME * timeval, int *hourp, int *minutep, int *secondp)
{
  decode_time (*timeval, hourp, minutep, secondp);
}

/*
 * DB_TIMESTAMP FUNCTIONS
 */

/*
 * tm_encode() - This function is used in conjunction with Unix mktime to
 *    convert julian/time pairs into a universal time constant.
 *    Be careful not to pass pointers to the tm_ structure to the decoding
 *    functions.  When these go through the PC int-to-long filter, they
 *    expect long* pointers which isn't what the fields in tm_ are.
 * return : error code
 * c_time_struct(out): c time structure from time.h
 * retval(out): time value (time_t)
 * date(in) : database date structure
 * time(in) : database time structure
 *
 * Note:  This function is kept only for DB Interface (backward compatibility)
 *	  Do not use it in CUBRID code since is using libc library. Prefer
 *	  db_timestamp_encode... functions.
 */
static int
tm_encode (struct tm *c_time_struct, time_t * retval, DB_DATE * date, DB_TIME * timeval)
{
  int mon, day, year, hour, min, sec;
  struct tm loc;

  *retval = -1;

  if (c_time_struct == NULL || ((date == NULL) && (timeval == NULL)) || init_tm (c_time_struct) == -1)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  if (date != NULL)
    {
      julian_decode (*date, &mon, &day, &year, NULL);
      c_time_struct->tm_mon = mon;
      c_time_struct->tm_mday = day;
      c_time_struct->tm_year = year;

      if (c_time_struct->tm_year < 1900)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	  return ER_DATE_CONVERSION;
	}
      c_time_struct->tm_year -= 1900;
      c_time_struct->tm_mon -= 1;
      c_time_struct->tm_isdst = -1;	/* Don't know if DST applies or not */
    }

  if (timeval != NULL)
    {
      decode_time (*timeval, &hour, &min, &sec);
      c_time_struct->tm_hour = hour;
      c_time_struct->tm_min = min;
      c_time_struct->tm_sec = sec;
    }

  loc = *c_time_struct;

  /* mktime() on Sun anomalously returns negative values other than -1. */
  *retval = mktime (&loc);
  if (*retval < (time_t) 0)	/* get correct tm_isdst */
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  /* If tm_isdst equals to zero, we do not need to convert the broken-down time (loc) again. */
  if (loc.tm_isdst == 0)
    {
      return NO_ERROR;
    }

  c_time_struct->tm_isdst = loc.tm_isdst;

  *retval = mktime (c_time_struct);
  if (*retval < (time_t) 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
}

/*
 * db_tm_encode() - This function is used in conjunction with Unix mktime to
 *    convert julian/time pairs into a universal time constant.
 *    Be careful not to pass pointers to the tm_ structure to the decoding
 *    functions.  When these go through the PC int-to-long filter, they
 *    expect long* pointers which isn't what the fields in tm_ are.
 * return : error code
 * c_time_struct(out): c time structure from time.h
 * date(in) : database date structure
 * time(in) : database time structure
 */
int
db_tm_encode (struct tm *c_time_struct, DB_DATE * date, DB_TIME * timeval)
{
  time_t retval;
  return tm_encode (c_time_struct, &retval, date, timeval);
}

/*
 * db_mktime() -  Converts the date and time arguments into the representation
 *              used by time() and returns it.
 * return : time_t
 * date(in): database date structure
 * time(in): database time structure
 *
 * note : This obeys the same convention as mktime(), returning a
 *        value of (time_t)-1 if an error occurred.  Consult errid()
 *        for the db error code.
 * Note2: This function is kept only for DB Interface (backward compatibility)
 *	  Do not use it in CUBRID code since is using libc library. Prefer
 *	  db_timestamp_encode... functions.
 */
time_t
db_mktime (DB_DATE * date, DB_TIME * timeval)
{
  time_t retval;
  struct tm temp;

  if (tm_encode (&temp, &retval, date, timeval) != NO_ERROR)
    {
      return -1;
    }

  return (retval);
}

/*
 * db_timestamp_encode() - This function is used to construct DB_TIMESTAMP
 *    from DB_DATE and DB_TIME.
 * return : error code
 * utime(out): pointer to universal time value
 * date(in): encoded julian date
 * time(in): relative time
 */
int
db_timestamp_encode (DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval)
{
  return db_timestamp_encode_ses (date, timeval, utime, NULL);
}


/*
 * db_timestamp_encode_ses() - This function is used to construct DB_TIMESTAMP
 *    from DB_DATE and DB_TIME, considering the time and date in session
 *    local timezone
 * return : error code
 * date(in): encoded julian date
 * timeval(in): relative time
 * utime(out): pointer to universal time value
 * dest_tz_id(out): pointer to packed timezone identifier of the result
 *		    (can be NULL, in which case no identifier is provided)
 */
int
db_timestamp_encode_ses (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime, TZ_ID * dest_tz_id)
{
  TZ_REGION ses_tz_region;
  tz_get_session_tz_region (&ses_tz_region);

  return db_timestamp_encode_w_reg (date, timeval, &ses_tz_region, utime, dest_tz_id);
}

/*
 * db_timestamp_encode_sys() - This function is used to construct DB_TIMESTAMP
 *    from DB_DATE and DB_TIME, considering the time and date in system timezone
 *
 * return : error code
 * date(in): encoded julian date
 * timeval(in): relative time
 * utime(out): pointer to universal time value
 * dest_tz_id(out): pointer to packed timezone identifier of the result
 *		    (can be NULL, in which case no identifier is provided)
 */
int
db_timestamp_encode_sys (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime, TZ_ID * dest_tz_id)
{
  TZ_REGION sys_tz_region;
  tz_get_system_tz_region (&sys_tz_region);

  return db_timestamp_encode_w_reg (date, timeval, &sys_tz_region, utime, dest_tz_id);
}

/*
 * db_timestamp_encode_utc() - This function is used to construct DB_TIMESTAMP
 *			       from DB_DATE and DB_TIME considering the date and
 *			       time in UTC
 * return : error code
 * date(in): encoded julian date
 * time(in): relative time
 * utime(out): pointer to universal time value
 */
int
db_timestamp_encode_utc (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime)
{
  DB_BIGINT t = 0;
  int mon, day, year, hour, min, sec;
  const int year_century = 1900;
  const int year_max_epoch = 2040;
  const int secs_per_day = 24 * 3600;
  const int secs_in_a_year = secs_per_day * 365;
  const int days_up_to_month[] = { 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };

  assert (date != NULL && timeval != NULL);

  if (*date == IGREG_SPECIAL && *timeval == 0)
    {
      *utime = IGREG_SPECIAL;
      return NO_ERROR;
    }

  julian_decode (*date, &mon, &day, &year, NULL);

  year -= year_century;
  if (year < 70 || year > year_max_epoch - year_century)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  mon -= 1;
  decode_time (*timeval, &hour, &min, &sec);

  /* The first item adds the days off all the years between 1970 and the given year considering that each year has 365
   * days. The second item adds a day every 4 years starting from 1973. The third item subtracts a day back out every
   * 100 years starting with 2001. The fourth item adds a day back every 400 years starting with 2001 */
  t = ((year - 70) * secs_in_a_year + ((year - 69) / 4) * secs_per_day - ((year - 1) / 100) * secs_per_day
       + ((year + 299) / 400) * secs_per_day);

  if (mon > TZ_MON_JAN)
    {
      t += days_up_to_month[mon - 1] * secs_per_day;
      if (IS_LEAP_YEAR (year_century + year) && mon > TZ_MON_FEB)
	{
	  t += secs_per_day;
	}
    }

  t += (day - 1) * secs_per_day;
  t += hour * 3600;
  t += min * 60;
  t += sec;

  t += tz_timestamp_encode_leap_sec_adj (year_century, year, mon, day);

  if (t < 0 || OR_CHECK_INT_OVERFLOW (t))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }
  else
    {
      *utime = (DB_TIMESTAMP) t;
    }
  return NO_ERROR;
}

/*
 * db_timestamp_encode_w_reg() - This function is used to construct
 *    DB_TIMESTAMP from DB_DATE and DB_TIME considering the date and time in
 *    timezone tz_region
 * return : error code
 * date(in): encoded julian date
 * time(in): relative time
 * tz_region(in): timezone region of date and time
 * utime(out): pointer to universal time value
 * dest_tz_id(out): pointer to packed timezone identifier of the result
*		    (can be NULL, in which case no identifier is provided)
 */
static int
db_timestamp_encode_w_reg (const DB_DATE * date, const DB_TIME * timeval, const TZ_REGION * tz_region,
			   DB_TIMESTAMP * utime, TZ_ID * dest_tz_id)
{
  int err = NO_ERROR;
  DB_DATETIME datetime, utc_datetime;
  DB_DATE utc_date;
  DB_TIME utc_time;

  if (*date == IGREG_SPECIAL && *timeval == 0)
    {
      *utime = IGREG_SPECIAL;
      return NO_ERROR;
    }

  datetime.date = *date;
  datetime.time = *timeval * 1000;

  if (!TZ_IS_UTC_TZ_REGION (tz_region))
    {
      /* convert datetime from source timezone to UTC */
      err =
	tz_conv_tz_datetime_w_region (&datetime, tz_region, tz_get_utc_tz_region (), &utc_datetime, dest_tz_id, NULL);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }
  else
    {
      utc_datetime = datetime;
      if (dest_tz_id != NULL)
	{
	  *dest_tz_id = *tz_get_utc_tz_id ();
	}
    }

  utc_date = utc_datetime.date;
  utc_time = utc_datetime.time / 1000;

  return db_timestamp_encode_utc (&utc_date, &utc_time, utime);
}

/*
 * db_timestamp_decode_ses() - This function converts a DB_TIMESTAMP into
 *			       a DB_DATE and DB_TIME pair, considering the
 *                             session local timezone
 * return : void
 * time(in): universal time
 * date(out): return julian date or zero date
 * time(out): return relative time
 */
int
db_timestamp_decode_ses (const DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval)
{
  TZ_REGION ses_tz_region;

  tz_get_session_tz_region (&ses_tz_region);
  return db_timestamp_decode_w_reg (utime, &ses_tz_region, date, timeval);
}

/*
 * db_timestamp_decode_utc() - This function converts a DB_TIMESTAMP into
 *    a DB_DATE and DB_TIME pair using UTC time reference
 * return : void
 * time(in): universal time
 * date(out): return julian date or zero date
 * time(out): return relative time
 */
void
db_timestamp_decode_utc (const DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval)
{
  int timestamp;
  int year, months, day;
  int hours, minutes, seconds;

  assert (utime != NULL);

  if (*utime == IGREG_SPECIAL)
    {
      if (date != NULL)
	{
	  *date = IGREG_SPECIAL;
	}
      if (timeval != NULL)
	{
	  *timeval = 0;
	}
      return;
    }

  timestamp = *utime;

  tz_timestamp_decode_sec (timestamp, &year, &months, &day, &hours, &minutes, &seconds);

  if (date != NULL)
    {
      *date = julian_encode (months + 1, day, year);
    }

  if (timeval != NULL)
    {
      *timeval = encode_time (hours, minutes, seconds);
    }
}

/*
 * db_timestamp_decode_w_reg() - This function converts a DB_TIMESTAMP into
 *    a DB_DATE and DB_TIME pair, directly into a time zone specified by
 *    tz_region
 * return : error code
 * utime(in): universal time
 * tz_region(in): timezone region of destination date and time
 * date(out): return julian date or zero date
 * time(out): return relative time
 */
int
db_timestamp_decode_w_reg (const DB_TIMESTAMP * utime, const TZ_REGION * tz_region, DB_DATE * date, DB_TIME * timeval)
{
  int err = NO_ERROR;
  DB_DATETIME datetime, utc_datetime;
  DB_DATE tmp_date;
  DB_TIME tmp_time;

  db_timestamp_decode_utc (utime, &tmp_date, &tmp_time);

  if (tmp_date == IGREG_SPECIAL)
    {
      if (date != NULL)
	{
	  *date = IGREG_SPECIAL;
	}
      if (timeval != NULL)
	{
	  *timeval = 0;
	}
      return err;
    }

  utc_datetime.date = tmp_date;
  utc_datetime.time = tmp_time * 1000;

  if (!TZ_IS_UTC_TZ_REGION (tz_region))
    {
      /* convert datetime from UTC to destination timezone */
      err = tz_conv_tz_datetime_w_region (&utc_datetime, tz_get_utc_tz_region (), tz_region, &datetime, NULL, NULL);
    }
  else
    {
      datetime = utc_datetime;
    }

  if (err != NO_ERROR)
    {
      /* error condition */
      if (date != NULL)
	{
	  *date = 0;
	}
      if (timeval != NULL)
	{
	  *timeval = 0;
	}
    }
  else
    {
      if (date != NULL)
	{
	  *date = datetime.date;
	}

      if (timeval != NULL)
	{
	  *timeval = datetime.time / 1000;
	}
    }

  return err;
}

/*
 * db_timestamp_decode_w_tz_id() - This function converts a DB_TIMESTAMP into
 *    a DB_DATE and DB_TIME pair, directly into a time zone specified by
 *    tz_id
 * return : error code
 * utime(in): universal time
 * tz_id(in): timezone id of destination date and time
 * date(out): return julian date or zero date
 * time(out): return relative time
 */
int
db_timestamp_decode_w_tz_id (const DB_TIMESTAMP * utime, const TZ_ID * tz_id, DB_DATE * date, DB_TIME * timeval)
{
  int err = NO_ERROR;
  DB_DATETIME datetime, utc_datetime;
  DB_DATE v_date;
  DB_TIME v_time;

  db_timestamp_decode_utc (utime, &v_date, &v_time);

  if (v_date == IGREG_SPECIAL)
    {
      if (date != NULL)
	{
	  *date = IGREG_SPECIAL;
	}
      if (timeval != NULL)
	{
	  *timeval = 0;
	}
      return err;
    }

  utc_datetime.date = v_date;
  utc_datetime.time = v_time * 1000;

  err = tz_utc_datetimetz_to_local (&utc_datetime, tz_id, &datetime);

  if (err != NO_ERROR)
    {
      /* error condition */
      if (date != NULL)
	{
	  *date = 0;
	}
      if (timeval != NULL)
	{
	  *timeval = 0;
	}
    }
  else
    {
      if (date != NULL)
	{
	  *date = datetime.date;
	}

      if (timeval != NULL)
	{
	  *timeval = datetime.time / 1000;
	}
    }

  return err;
}

/*
 * UNIX COMPATIBILITY FUNCTIONS
 */

/*
 * db_strftime() - This is a database interface for the standard C library
 *    routine strftime().  Either date or time can be NULL, depending on the
 *    desired conversion.  The format string is the same as used by strftime()
 * return : error code
 * s(out): string to print to
 * smax(in): maximum size of this the string s
 * fmt(in): format string
 * date(in): date
 * time(in): time
 */
int
db_strftime (char *s, int smax, const char *fmt, DB_DATE * date, DB_TIME * timeval)
{
  int retval;
  struct tm date_and_time;
  int conversion_error;

  conversion_error = db_tm_encode (&date_and_time, date, timeval);
  if (conversion_error != NO_ERROR)
    {
      return ((int) conversion_error);
    }

  retval = ((int) strftime (s, (size_t) smax, fmt, &date_and_time));

  return (retval);
}

/*
 * db_localtime() - Converts the time since epoch (jan 1 1970) into database
 *    date and time structure.  date or time can be NULL if conversion to that
 *    structure is not desired.
 * return : void
 * epoch_time(in): number of seconds since epoch
 * date(out): database date structure
 * timeval(out): database time structure
 */
void
db_localtime (time_t * epoch_time, DB_DATE * date, DB_TIME * timeval)
{
  struct tm *temp;
  struct tm t;

  temp = localtime_r (epoch_time, &t);
  if (temp == NULL)
    {
      return;
    }

  if (date != NULL)
    {
      *date = julian_encode (temp->tm_mon + 1, temp->tm_mday, temp->tm_year + 1900);
    }
  if (timeval != NULL)
    {
      *timeval = encode_time (temp->tm_hour, temp->tm_min, temp->tm_sec);
    }
}


/*
 * db_localdatetime() - Converts the time since epoch (jan 1 1970) into database
 *    datetime structure.
 * return : void
 * epoch_time(in): number of seconds since epoch
 * datetime(out): database datetime structure
 */
void
db_localdatetime (time_t * epoch_time, DB_DATETIME * datetime)
{
  db_localdatetime_msec (epoch_time, 0, datetime);
}

/*
 * db_localdatetime_msec() - Converts the time include milliseconds since
 *    epoch (jan 1 1970) into database datetime structure.
 * return : void
 * epoch_time(in): number of seconds since epoch
 * millisecond(in): part of extra milliseconds
 * datetime(out): database datetime structure
 */
void
db_localdatetime_msec (time_t * epoch_time, int millisecond, DB_DATETIME * datetime)
{
  struct tm *temp;
  struct tm t;

  temp = localtime_r (epoch_time, &t);
  if (temp == NULL)
    {
      return;
    }

  if (datetime != NULL)
    {
      db_datetime_encode (datetime, temp->tm_mon + 1, temp->tm_mday, temp->tm_year + 1900, temp->tm_hour, temp->tm_min,
			  temp->tm_sec, millisecond);
    }
}

/*
 * DATE/TIME/TIMESTAMP PARSING FUNCTIONS
 */

/*
 * init_tm() - This function uses time() and localtime() to initialize a struct
 *   tm with the current time.
 * return : zero or -1(on error)
 * tm(in) : a pointer to a struct tm to be modified.
 */
static int
init_tm (struct tm *tm)
{
  time_t tloc;
  struct tm *tmp;
  struct tm t;

  if (time (&tloc) == -1)
    {
      return -1;
    }

  tmp = localtime_r (&tloc, &t);
  if (tmp == NULL)
    {
      return -1;
    }

  *tm = *tmp;

  return 0;
}

/*
 * get_current_year() - This function returns current year.
 * return : current year or -1(on error)
 */
static int
get_current_year (void)
{
  struct tm tm;
  return (init_tm (&tm) == -1) ? -1 : tm.tm_year + YBIAS;
}

/*
 * parse_date() - Parse an ordinary date string (e.g., '10/15/86').
 *    Whitespace is permitted between components.  If the year is omitted, the
 *    current year will be discovered and used.
 * return : NULL on error
 * buf(in): a buffer containing a date to be parsed
 * buf_len(in): the length of the string to be parsed
 * date(out): a pointer to a DB_DATE to be modified
 */
static const char *
parse_date (const char *buf, int buf_len, DB_DATE * date)
{
  int part[3] = { 0, 0, 0 };
  int part_char_len[3] = { 0, 0, 0 };
  int month, day, year;
  int julian_date;
  unsigned int i;
  const char *p;
  int date_style = -1;
  int year_part, month_part, day_part;
  int separator = '\0';
  const char *strend = buf + buf_len;

  if (buf == NULL)
    {
      return NULL;
    }

  /* for each date part (year, month, day) */
  for (i = 0, p = buf; i < DIM (part); i++)
    {
      /* skip leading space */
      for (; p < strend && char_isspace (*p); ++p)
	{
	  ;
	}

      /* read found decimal field value */
      for (; p < strend && char_isdigit (*p); ++p)
	{
	  part[i] = part[i] * 10 + (*p - '0');
	  part_char_len[i]++;
	}

      if (i < DIM (part) - 1)
	{
	  /* skip inter-field space */
	  for (; p < strend && char_isspace (*p); ++p)
	    {
	      ;
	    }

	  if (p == strend)
	    {
	      break;
	    }

	  /* check date separator ('/' or '-'), if any */
	  if (separator != '\0' && *p != '\0' && *p != separator)
	    {
	      return NULL;
	    }

	  /* find and skip the separator */
	  if (*p == '/')
	    {
	      separator = *p;
	      ++p;
	      date_style = MMDDYYYY;
	    }
	  else if (*p == '-')
	    {
	      separator = *p;
	      ++p;
	      date_style = YYYYMMDD;
	    }
	  else
	    {
	      break;
	    }
	}
    }

  /* skip trailing space */
  for (; p < strend && char_isspace (*p); ++p)
    {
      ;
    }

  if (date_style == MMDDYYYY)
    {
      if (i == 1)
	{
	  year_part = -1;
	}
      else
	{
	  year_part = 2;
	}
      month_part = 0;
      day_part = 1;
    }
  else if (date_style == YYYYMMDD)
    {
      if (i == 1)
	{
	  year_part = -1;
	  month_part = 0;
	  day_part = 1;
	}
      else
	{
	  year_part = 0;
	  month_part = 1;
	  day_part = 2;
	}
    }
  else
    {
      return NULL;
    }

  /* stop parsing if year is present and over 10000 */
  if (0 <= year_part && 10000 <= part[year_part])
    {
      return NULL;
    }

  /* fill the year if not present */
  if (year_part == -1)
    {
      year = get_current_year ();
    }
  else
    {
      year = part[year_part];
      if (part_char_len[year_part] == 2)
	{
	  if (year < 70)
	    {
	      year += 2000;
	    }
	  else
	    {
	      year += 1900;
	    }
	}
    }

  month = part[month_part];
  day = part[day_part];

  /*
   * 0000-00-00 00:00:00 means timestamp 1970-01-01 00:00:00
   * but 0000-00-00 XX:XX:XX (one of X is not zero) means error
   * so in this parse_date return OK but set date as 0
   * (It should be treated differ with '1970-01-01')
   */
  if (year == 0 && month == 0 && day == 0)
    {
      *date = IGREG_SPECIAL;
      return p;
    }

  /*
   * Now encode it and then decode it again and see if we get the same
   * day; if not, it was a bogus specification, like 2/29 on a non-leap
   * year.
   */
  julian_date = julian_encode (month, day, year);
  julian_decode (julian_date, &part[0], &part[1], &part[2], NULL);

  if (month == part[0] && day == part[1] && year == part[2])
    {
      *date = julian_date;
      return p;
    }
  else
    {
      return NULL;
    }

}

/*
 * parse_time() - If parse_time() succeeds, it returns a pointer to the
 *   rest of the buffer following the parsed time expr. This accepts both
 *   12 and 24 hour times, with optional am/pm designators.
 *   An am designator on a 24 hour time after 12pm is considered an
 *   error, e.g., "13:45am" will fail.
 *   Minutes and seconds can be omitted; they will default to 0.
 * return : const char or NULL on error
 * buf(in): pointer to time expression
 * buf_len(in): the length of the string to be parsed
 * time(out): pointer to DB_TIME to be updated with the parsed time
 */
static const char *
parse_time (const char *buf, int buf_len, DB_TIME * time)
{
  unsigned int mtime;
  const char *p;
  bool is_msec;

  p = parse_mtime (buf, buf_len, &mtime, &is_msec, NULL);

  if (p != NULL)
    {
      if (is_msec == true)
	{
	  return NULL;
	}

      *time = mtime / 1000;
    }

  return p;
}

/*
 * db_string_check_explicit_time() -
 * return : true if explicit time expression
 * str(in): pointer to time expression
 * str_len(in): the length of the string to be checked
 */
bool
db_string_check_explicit_time (const char *str, int str_len)
{
  unsigned int mtime;
  bool is_explicit;

  const char *result = parse_mtime (str, str_len, &mtime, NULL, &is_explicit);
  if (result == NULL || *result != '\0')
    {
      return false;
    }
  return is_explicit;
}

/*
 * parse_mtime() -
 * return : const char or NULL on error
 * buf(in): pointer to time expression
 * buf_len(in): the length of the string to be parsed
 * mtime(out): pointer to unsigned int to be updated with the parsed time
 */
static const char *
parse_mtime (const char *buf, int buf_len, unsigned int *mtime, bool * is_msec, bool * is_explicit)
{
  int part[4] = { 0, 0, 0, 0 };
  unsigned int i;
  const char *p;
  double fraction = 100;
  const char *strend = buf + buf_len;

  if (buf == NULL)
    {
      return NULL;
    }

  if (is_msec != NULL)
    {
      *is_msec = false;
    }

  if (is_explicit != NULL)
    {
      *is_explicit = true;
    }

  for (i = 0, p = buf; i < DIM (part); i++)
    {
      for (; p < strend && char_isspace (*p); ++p)
	;
      for (; p < strend && char_isdigit (*p); ++p)
	{
	  if (i != 3)
	    {
	      part[i] = part[i] * 10 + (*p - '0');
	    }
	  else
	    {
	      part[i] += (int) (fraction * (*p - '0') + 0.5);
	      fraction /= 10;
	    }
	}
      if (i < DIM (part) - 1)
	{
	  for (; p < strend && char_isspace (*p); ++p)
	    ;

	  if (p < strend && *p == ':')
	    {
	      if (i == 2)
		{
		  return NULL;
		}
	      ++p;
	    }
	  else if (p < strend && *p == '.' && i == 2)
	    {
	      ++p;
	      if (is_msec != NULL)
		{
		  *is_msec = true;
		}
	    }
	  else
	    {
	      /* This allows time' ' to be interpreted as 0 which means 12:00:00 AM. */
	      ++i;

	      /* This means time string format is not completed (like 0, 01:00) * This flag will be used by operate
	       * like [select 1 + '1'] which should not be converted to time */
	      if (is_explicit != NULL && i < 3)
		{
		  *is_explicit = false;
		}

	      break;
	    }
	}
    }

  for (; p < strend && char_isspace (*p); ++p)
    ;
  if (is_local_am_str (p, strend) && ((*(p + local_am_strlen) == ' ') || p + local_am_strlen == strend))
    {
      p += local_am_strlen;
      if (part[0] == 12)
	{
	  part[0] = 0;
	}
      else if (part[0] > 12)
	{
	  part[0] = -1;
	}
    }
  else if (is_local_pm_str (p, strend) && ((*(p + local_pm_strlen) == ' ') || p + local_pm_strlen == strend))
    {
      p += local_pm_strlen;
      if (part[0] < 12)
	{
	  part[0] += 12;
	}
      else if (part[0] == 0)
	{
	  part[0] = -1;
	}
    }
  else if (i == 0 && *buf)
    {
      /* buf is "[0-9]*" */
      return NULL;
    }

  if (part[0] < 0 || part[0] > 23)
    {
      return NULL;
    }
  if (part[1] < 0 || part[1] > 59)
    {
      return NULL;
    }
  if (part[2] < 0 || part[2] > 59)
    {
      return NULL;
    }
  if (part[3] < 0 || part[3] > 999)
    {
      return NULL;
    }

  *mtime = encode_mtime (part[0], part[1], part[2], part[3]);

  return p;
}

/*
 * fill_local_ampm_str() - writes the "am" or "pm" string as by the current
 *			    locale into the given string
 * str(out):	string buffer to receive the "am" or the "pm" string from the
 *		current locale
 * am(in):	true to return the current "am" string, "false" to return the
 *		current "pm" string from the current locale
 */
static void
fill_local_ampm_str (char str[10], bool am)
{
  struct tm tm;
  if (init_tm (&tm) == -1)
    {
      if (am)
	{
	  strcpy (str, "am");
	}
      else
	{
	  strcpy (str, "pm");
	}
    }
  else
    {
      /*
       * Use strftime() to try to find out this locale's idea of
       * the am/pm designators.
       */
      if (am)
	{
	  tm.tm_hour = 0;
	  strftime (str, 10, "%p", &tm);
	}
      else
	{
	  tm.tm_hour = 12;
	  strftime (str, 10, "%p", &tm);
	}
    }
}

/*
 * db_date_locale_init() - Initializes the am/pm strings from the current
 *			    locale, to be used when parsing TIME values.
 *			    Should be invoked when CUBRID starts up
 */
void
db_date_locale_init (void)
{
  fill_local_ampm_str (local_am_str, true);
  local_am_strlen = strlen (local_am_str);

  fill_local_ampm_str (local_pm_str, false);
  local_pm_strlen = strlen (local_pm_str);
}

/*
 * is_local_am_str() - checks if a string is the local "am" string
 *
 * returns : 0 if matches the local string, non-zero otherwise
 * p(in):    null terminated string
 */
static bool
is_local_am_str (const char *p, const char *p_end)
{
  return ((p + local_am_strlen - 1 < p_end) && (intl_mbs_ncasecmp (p, local_am_str, local_am_strlen) == 0));
}

/*
 * is_local_pm_str() - checks if a string is the local "pm" string
 *
 * returns : 0 if matches the local string, non-zero otherwise
 * p(in):    null terminated string
 */
static bool
is_local_pm_str (const char *p, const char *p_end)
{
  return ((p + local_pm_strlen - 1 < p_end) && (intl_mbs_ncasecmp (p, local_pm_str, local_pm_strlen) == 0));
}

/*
 * parse_date_separated() - Reads  DATE in '09-10-12' format
 *
 * returns:	position in str where parsing stopped, if successfull,
 *		NULL otherwise
 * str(in):	string with the date to be parsed
 * strend(in):  the end of the string to be parsed
 * date(out):   resulting date, after parsing, if successfull
 * syntax_check(out):
 *		if not null and conversion fails, will be set to
 *		the last read character from the input string, when the
 *		syntax (parsing) was successfull, but resulting
 *		value is incorrect (for example for 2009-120-21). Should be
 *		set to NULL before making the function call.
 * year_digits(out):
 *		if not null will receive the number of digits that the year in
 *		the date string was specified with. The number can be received
 *		even if conversion fails. If not null it will also turn any
 *		date part found larger than DATETIME_FIELD_LIMIT into a syntax
 *		error (as opposed to an invalid, but correctly delimited, date
 *		value).
 * sep_ch(out): separator character that the parsed date is using, or '\0' if
 *		parsed date uses more than one separator. Can be modified even
 *	        if parsing fails.
 */
static char const *
parse_date_separated (char const *str, char const *strend, DB_DATE * date, char const **syntax_check, int *year_digits,
		      char *sep_ch)
{
  DB_DATE cdate;
  unsigned char separator = 0;	/* 0 - yet to be read 1 - only single slashes read 2 - non-slashes (or multiple
				 * slashes) read */

  int date_parts[3] = { 0, 0, 0 };
  unsigned char date_parts_len[3] = { 0, 0, 0 };
  unsigned char parts_found = 0, year_part, month_part, day_part;

  char const *p = str, *q;

  assert (!syntax_check || *syntax_check == NULL);

  /* skip leading spaces in string */
  while (p < strend && char_isspace (*p))
    {
      p++;
    }

  if (p == strend || !char_isdigit (*p))
    {
      return NULL;		/* date should begin with a digit */
    }

  /* read up to three date parts (year, month, day) from string, separated by non-blank, non-alphanumeric characters */
  if (sep_ch)
    {
      *sep_ch = '0';		/* a digit can not be a separator */
    }

  q = p;

  while (p < strend && q == p && !char_isspace (*p) && !char_isalpha (*p) && parts_found < DIM (date_parts))
    {
      unsigned char new_separator = separator;

      /* read any separator */
      while (p < strend && !char_isspace (*p) && !char_isdigit (*p) && !char_isalpha (*p))
	{
	  if (*p == '/')
	    {
	      /* '/' separator found */
	      if (separator == 0)
		{
		  new_separator = 1;
		}
	    }
	  else
	    {
	      /* non-slash separator found */
	      new_separator = 2;
	    }

	  if (sep_ch)
	    {
	      if (*sep_ch)
		{
		  if (*sep_ch == '0')
		    {
		      *sep_ch = *p;
		    }
		  else
		    {
		      if (*sep_ch != *p)
			{
			  /* more than one separator found */
			  *sep_ch = '\0';
			}
		    }
		}
	    }

	  p++;
	}

      /* read the number (date part value) */
      if (p < strend && char_isdigit (*p))
	{
	  separator = new_separator;

	  while (p < strend && char_isdigit (*p))
	    {
	      if (date_parts[parts_found] < DATETIME_FIELD_LIMIT)
		{
		  date_parts[parts_found] *= 10;
		  date_parts[parts_found] += *p - '0';
		}

	      if (date_parts_len[parts_found] < 4)
		{
		  date_parts_len[parts_found]++;
		}

	      p++;
	    }

	  if (year_digits && date_parts[parts_found] >= DATETIME_FIELD_LIMIT)
	    {
	      /* In a context where the number of digits specified for the year is requested (when reading a time from
	       * a datetime string), having any date part larger than the field limit is not only an invalid value, but
	       * also a parsing error. */
	      return NULL;
	    }

	  parts_found++;
	  q = p;
	}
    }

  p = q;

  if (parts_found < 2)
    {
      /* Insufficient number of year/month/day fields could be read */
      return NULL;
    }

  /* Infer the order of date fields: MM/DD[/YY] or [YY-]MM-DD */
  if (separator == 1)
    {
      /* mm/dd[/yy] date format */
      year_part = 2;
      month_part = 0;
      day_part = 1;

    }
  else
    {
      /* [yy-]mm-dd format */
      year_part = 0;
      month_part = 1;
      day_part = 2;

      if (parts_found == 2)
	{
	  date_parts[2] = date_parts[1];
	  date_parts[1] = date_parts[0];
	  date_parts_len[2] = date_parts_len[1];
	  date_parts_len[1] = date_parts_len[0];
	}
    }

  /* Fill in the year or the century, if omitted */
  if (parts_found == 2)
    {
      if (year_digits)
	{
	  *year_digits = 0;
	}
      date_parts[year_part] = get_current_year ();
    }
  else
    {
      if (year_digits)
	{
	  *year_digits = date_parts_len[year_part];
	}

      if (date_parts_len[year_part] == 2)
	{
	  if (date_parts[year_part] < 70)
	    {
	      date_parts[year_part] += 2000;
	    }
	  else
	    {
	      date_parts[year_part] += 1900;
	    }
	}
    }

  if (date_parts[month_part] == 0 && date_parts[day_part] == 0 && date_parts[year_part] == 0)
    {
      *date = IGREG_SPECIAL;
      return p;
    }

  /* Check and return the resulting date */
  if (date_parts[year_part] >= 10000 || date_parts[month_part] > 12 || date_parts[month_part] < 1
      || date_parts[day_part] > 31 || date_parts[day_part] < 1)
    {
      /* malformed or invalid date string */
      if (syntax_check)
	{
	  /* parsing successfull, but unexpected value */
	  *syntax_check = p;
	}
      return NULL;
    }
  else
    {
      int year, month, day;

      cdate = julian_encode (date_parts[month_part], date_parts[day_part], date_parts[year_part]);
      julian_decode (cdate, &month, &day, &year, NULL);

      if (day == date_parts[day_part] && month == date_parts[month_part] && year == date_parts[year_part])
	{
	  *date = cdate;
	  return p;
	}
      else
	{
	  /* parsing successfull, but unexpected value */
	  if (syntax_check)
	    {
	      *syntax_check = p;
	    }

	  return NULL;
	}
    }
}

/*
 * parse_mtime_separated() - Reads a TIME from a string, reading hours first
 *				and accepting any separators
 * returns: pointer to the next character in the input string that fallows the
 *	    parsed time value if successfull, NULL otherwise
 * str(in): string with the time value to be parsed
 * strend(in):  the end of the string to be parsed
 * mtime(out): the converted time
 * syntax_check(out): if not null, upon conversion failure it will be filled
 *			with the pointer to the last character read if the
 *			time could be parsed but the time value
 *			found is invalid (like 10:65:24). Should be set to
 *			NULL before the function invocation
 * time_parts(out): if not NULL will receive the number of time components
 *		    (hour, minute, second) found in the string. The number can
 *		    be received even if parsing later fails. If not NULL this
 *		    parameter will also turn any TIME part larger than
 *		    DATETIME_FIELD_LIMIT into a syntax error (as opposed to an
 *		    invalid, but correctly delimited, time value).
 * sep_ch(out):	    if not NULL will receive the separator character used for
 *		    the time, or '\0' if more separators are used.
 * has_explicit_msec(out):
 *		    If not NULL and parsing successfull, will receive true if
 *		    the input string explicitly sepcifies a number of
 *		    milliseconds or the decimal point for time
 */
static char const *
parse_mtime_separated (char const *str, char const *strend, unsigned int *mtime, char const **syntax_check,
		       int *time_parts, char *sep_ch, bool * has_explicit_msec, bool is_datetime)
{
  int h = 0, m = 0, s = 0, msec = 0;
  char const *p = str, *q;

  assert (!syntax_check || !*syntax_check);

  if (sep_ch)
    {
      *sep_ch = '0';		/* a digit can not be a separator */
    }

  if (p == strend || !char_isdigit (*p))
    {
      /* no time value written in this string */
      return NULL;
    }

  /* read hours value */
  while (p < strend && char_isdigit (*p))
    {
      if (h < DATETIME_FIELD_LIMIT)
	{
	  h *= 10;
	  h += *p - '0';
	}

      p++;
    }

  /* numeric datetime field overflowed and failed parsing */
  if (time_parts && h >= DATETIME_FIELD_LIMIT)
    {
      return NULL;
    }


  /* skip separators */
  q = p;
  while (p < strend && !char_isspace (*p) && !char_isalpha (*p) && !char_isdigit (*p))
    {
      if (sep_ch)
	{
	  if (*sep_ch)
	    {
	      if (*sep_ch == '0')
		{
		  *sep_ch = *p;
		}
	      else
		{
		  if (*sep_ch != *p)
		    {
		      *sep_ch = '\0';
		    }
		}
	    }
	}

      p++;
    }

  if (is_datetime && p < strend && char_isspace (*p))
    {
      *syntax_check = p;
      return NULL;
    }

  /* read minutes value */
  if (p < strend && char_isdigit (*p))
    {
      while (p < strend && char_isdigit (*p) && DATETIME_FIELD_LIMIT - (*p - '0') / 10 > m)
	{
	  if (m < DATETIME_FIELD_LIMIT)
	    {
	      m *= 10;
	      m += *p - '0';
	    }

	  p++;
	}

      if (time_parts && m >= DATETIME_FIELD_LIMIT)
	{
	  return NULL;
	}

      /* skip any separators */
      q = p;

      while (p < strend && !char_isspace (*p) && !char_isalpha (*p) && !char_isdigit (*p))
	{
	  if (sep_ch)
	    {
	      if (*sep_ch)
		{
		  if (*sep_ch == '0')
		    {
		      *sep_ch = *p;
		    }
		  else
		    {
		      if (*sep_ch != *p)
			{
			  *sep_ch = '\0';
			}
		    }
		}
	    }
	  p++;
	}

      if (is_datetime && p < strend && char_isspace (*p))
	{
	  *syntax_check = p;
	  return NULL;
	}

      /* read seconds value */
      if (p < strend && char_isdigit (*p))
	{
	  while (p < strend && char_isdigit (*p) && (DATETIME_FIELD_LIMIT - (*p - '0') / 10 > s))
	    {
	      if (s < DATETIME_FIELD_LIMIT)
		{
		  s *= 10;
		  s += *p - '0';
		}
	      p++;
	    }

	  /* numeric datetime field overflowed and failed parsing */
	  if (time_parts && s >= DATETIME_FIELD_LIMIT)
	    {
	      return NULL;
	    }


	  if (time_parts)
	    {
	      *time_parts = 3;	/* hh:mm:ss */
	    }

	  /* read milliseconds value */
	  if (p < strend && *p == '.')
	    {
	      p++;
	      if (p < strend && char_isdigit (*p))
		{
		  msec += (*p - '0') * 100;
		  p++;

		  if (p < strend && char_isdigit (*p))
		    {
		      msec += (*p - '0') * 10;
		      p++;

		      if (p < strend && char_isdigit (*p))
			{
			  msec += *p - '0';
			  p++;

			  /* attempting to round, instead of truncate, the number of milliseconds will result in
			   * userspace problems when '11-02-24 23:59:59.9999' is converted to DATETIME, and then
			   * '11-02-24' is converted to DATE and '23:59:59:9999' is converted to TIME User might expect
			   * to get the same results in both cases, when in fact the wrap-around for the time value
			   * will give different results if the number of milliseconds is rounded. */
			  while (p < strend && char_isdigit (*p))
			    {
			      p++;
			    }
			}
		    }
		}
	      if (has_explicit_msec)
		{
		  *has_explicit_msec = true;
		}
	    }
	}
      else
	{
	  /* No seconds present after the minutes and the separator. */

	  if (time_parts)
	    {
	      *time_parts = 2;
	    }
	}
    }
  else
    {
      /* No minutes value present after the hour and separator */

      if (time_parts)
	{
	  if (char_isdigit (*(p - 1)))
	    {
	      *time_parts = 1;	/* hh */
	    }
	  else
	    {
	      *time_parts = 2;	/* { hh sep } is just like { hh sep mm } */
	    }
	}
    }

  /* look for the am/pm string following the time */
  q = p;			/* save p just in case there is no am/pm string */

  while (p < strend && char_isspace (*p))
    {
      p++;
    }

  /* look for either the local or the English am/pm strings */
  if (is_local_am_str (p, strend) && (p + local_am_strlen == strend || !char_isalpha (p[local_am_strlen])))
    {
      p += local_am_strlen;
      if (h == 12)
	{
	  /* 12:01am means 00:01 */
	  h = 0;
	}
    }
  else if (is_local_pm_str (p, strend) && (p + local_pm_strlen == strend || !char_isalpha (p[local_pm_strlen])))
    {
      p += local_pm_strlen;
      if (h < 12)
	{
	  /* only a 12-hour clock uses the am/pm string */
	  h += 12;
	}
    }
  else
    {
      /* no "am"/"pm" string found */
      p = q;

      /* check if an incomplete time is followed by a space */
      if (time_parts && *time_parts < 3)
	{
	  if (p < strend && char_isspace (*p))
	    {
	      /* turn off parsing just the time off the timestamp */
	      *time_parts = 1;
	    }
	}
    }

  /* check the numeric values allowing an hours value of 24, to be treated as 00, would no longer be mysql-compatible,
   * since mysql actually stores the value '24', not 00 */
  if (h > 23 || m > 59 || s > 59)
    {
      /* time parsed successfully, but the found value was unexpected */
      if (syntax_check)
	{
	  *syntax_check = p;
	}
      return NULL;
    }

  /* encode (from the components) and return the parsed time */
  *mtime = encode_mtime (h, m, s, msec);
  return p;
}

/*
 * parse_explicit_mtime_separated() - Reads a TIME in '10:22:10.45 am' format.
 *				    Hours (and minutes) field may be omitted,
 *				    but only together with the separator.
 * returns:	pointer to the next character in the input string that follows
 *		the parsed time
 * str(in):	string with the time to be parsed
 * strend(in):  the end of the string to be parsed
 * mtime(out):	the converted time
 * syntax_check(out):
 *		if not NULL, upon conversion failure it will be set to
 *		the pointer in the input string to the last character parsed
 *		if the time could be parsed but the time value
 *		found is invalid (like 10:65:24). Should be set to
 *		false before the function invocation
 * has_explicit_msec(out):
 *		if not NULL and converstion successfull, will receive true if
 *		the input string explicitly specifies the number of
 *		milliseconds or the decimal point for time
 */
static char const *
parse_explicit_mtime_separated (char const *str, char const *strend, unsigned int *mtime, char const **syntax_check,
				bool * has_explicit_msec)
{
  int h = 0, m = 0, s = 0, msec = 0, time_parts[3] = { 0, 0, 0 };
  unsigned time_parts_found = 0;
  char const *p = str, *q;
  int msec_digit_order = 100;

  /* skip leading spaces */
  while (p < strend && char_isspace (*p))
    {
      p++;
    }

  if (p >= strend)
    {
      return NULL;
    }

  /* read up to 3 time parts (hours, minutes, seconds) from the string */
  if (p[0] == ':')
    {
      /* allow for ':MM:SS.ssss' format */
      time_parts_found++;
    }

  while (((time_parts_found && p[0] == ':' && (++p < strend && char_isdigit (*p)))
	  || (!time_parts_found && char_isdigit (p[0]))) && time_parts_found < DIM (time_parts))
    {
      do
	{
	  time_parts[time_parts_found] *= 10;
	  time_parts[time_parts_found] += *p - '0';
	  p++;
	}
      while (time_parts[time_parts_found] < DATETIME_FIELD_LIMIT && p < strend && char_isdigit (*p));

      if (p < strend && char_isdigit (*p))
	{
	  /* invalid number of seconds/minutes/hours */
	  return NULL;
	}
      else
	{
	  time_parts_found++;
	}

      if (p >= strend)
	{
	  break;
	}
    }

  /* Allow trailing ':' separator if time is incomplete */
  if (time_parts_found == DIM (time_parts) && *(p - 1) == ':')
    {
      p--;
    }

  if (p < strend && *p == '.')
    {
      p++;

      while (msec_digit_order && p < strend && char_isdigit (*p))
	{
	  msec += (*p++ - '0') * msec_digit_order;
	  msec_digit_order /= 10;
	}

      /* skip remaining digits in the fractional seconds part, if any trying to round, instead of truncate, the
       * milliseconds part can lead to user space problems if wrap-around is needed. */
      while (p < strend && char_isdigit (*p))
	{
	  p++;
	}

      if (has_explicit_msec)
	{
	  *has_explicit_msec = true;
	}
    }

  /* infer the hours, minutes and seconds from the number of time parts read */
  switch (time_parts_found)
    {
    case 3:
      h = time_parts[0];
      m = time_parts[1];
      s = time_parts[2];
      break;
    case 2:
      h = time_parts[0];
      m = time_parts[1];
      break;
    case 1:
    case 0:
      return NULL;
    }

  /* skip optional whitespace before the am/pm string following the numeric time value */
  q = p;			/* save p in case there is no am/pm string */

  while (p < strend && char_isspace (*p))
    {
      p++;
    }

  /* look for either the local or the English am/pm strings */
  if (is_local_am_str (p, strend) && (p + local_am_strlen == strend || !char_isalpha (p[local_am_strlen])))
    {
      p += local_am_strlen;
      if (h == 12)
	{
	  h = 0;		/* 12:01am means 00:01 */
	}
      else if (h > 12)
	{
	  if (syntax_check)
	    {
	      *syntax_check = p;
	    }
	  return NULL;
	}
    }
  else if (is_local_pm_str (p, strend) && (p + local_pm_strlen == strend || !char_isalpha (p[local_pm_strlen])))
    {
      p += local_pm_strlen;
      if (h < 12)
	{
	  /* only a 12-hour clock uses the am/pm string */
	  h += 12;
	}
    }
  else
    {
      p = q;
    }

  /* check the time parts read */
  if (h > 23 || m > 59 || s > 59)
    {
      if (syntax_check)
	{
	  *syntax_check = p;
	}
      return NULL;
    }

  *mtime = encode_mtime (h, m, s, msec);

  return p;
}

/*
 * parse_explicit_mtime_compact()   - Reads a time in
 *				     [[[[YY MM DD] HH] MM] SS[[.][sss]
 *					format
 *				    '.0' is a valid time that can also be
 *				    written as '.'
 * returns:	pointer in the input string to the character after
 *		the converted time, if successfull, or NULL on error
 * str(in):	the [DATE]TIME string, in compact form, to be converted
 * strend(in):  the end of the string to be parsed
 * mtime(out):	pointer to the converted time
 *
 */
static char const *
parse_explicit_mtime_compact (char const *str, char const *strend, unsigned int *mtime)
{
  char const *p = str, *q = str, *r = str;
  int y = 0, mo = 0, d = 0, h = 0, m = 0, s = 0, msec = 0;
  int ndigits = 0;

  /* skip leading whitespace */
  while (p < strend && char_isspace (*p))
    {
      p++;
    }

  /* count number of decimal digits in the string */
  q = p;
  r = p;

  while (q < strend && char_isdigit (*q))
    {
      q++;
    }

  if (p != q || q[0] == '.')
    {
      if (q - p > 14)
	{
	  /* YYYY MM DD HH MM SS [.] [ssss..] */
	  /* if (q[0] != '.') */
	  {
	    y = DECODE (p[0]) * 1000 + DECODE (p[1]) * 100 + DECODE (p[2]) * 10 + DECODE (p[3]);
	    mo = DECODE (p[4]) * 10 + DECODE (p[5]);
	    d = DECODE (p[6]) * 10 + DECODE (p[7]);
	    h = DECODE (p[8]) * 10 + DECODE (p[9]);
	    m = DECODE (p[10]) * 10 + DECODE (p[11]);
	    s = DECODE (p[12]) * 10 + DECODE (p[13]);

	    p += 14;
	    msec += DECODE (*p++) * 100;
	    if (p < strend && char_isdigit (*p))
	      {
		msec += DECODE (*p++) * 10;
		if (p < strend && char_isdigit (*p))
		  {
		    /* reading 3 digits after decimal point is not mysql compatible (which only reads 2 digits) but is
		     * the expected CUBRID behaviour */
		    msec += DECODE (*p++);

		    /* skil all other digits in the milliseconds field. */
		    p = q;
		  }
	      }
	  }
	}
      else
	{
	  switch (q - p)
	    {
	    case 14:
	      /* YYYY-MM-DD HH:MM:SS */
	      y += DECODE (*p++);
	      /* FALLTHRU */
	    case 13:
	      y *= 10;
	      y += DECODE (*p++);
	      /* FALLTHRU */
	    case 12:
	      /* YY-MM-DD HH:MM:SS */
	      y *= 10;
	      y += DECODE (*p++);
	      /* FALLTHRU */
	    case 11:
	      y *= 10;
	      y += DECODE (*p++);
	      /* FALLTHRU */
	    case 10:
	      /* MM-DD HH:MM:SS */
	      mo += DECODE (*p++);
	      /* FALLTHRU */
	    case 9:
	      /* M-DD HH:MM:SS */
	      mo *= 10;
	      mo += DECODE (*p++);
	      d += DECODE (*p++) * 10;
	      d += DECODE (*p++);
	      /* FALLTHRU */
	    case 6:
	      /* HH:MM:SS */
	      h += DECODE (*p++);
	      /* FALLTHRU */
	    case 5:
	      /* H:MM:SS */
	      h *= 10;
	      h += DECODE (*p++);
	      /* FALLTHRU */
	    case 4:
	      /* MM:SS */
	      m += DECODE (*p++);
	      /* FALLTHRU */
	    case 3:
	      /* M:SS */
	      m *= 10;
	      m += DECODE (*p++);
	      /* FALLTHRU */
	    case 2:
	      /* SS */
	      s += DECODE (*p++);
	      /* FALLTHRU */
	    case 1:
	      /* S */
	      s *= 10;
	      s += DECODE (*p++);
	      /* FALLTHRU */
	    case 0:
	      if (*p == '.')
		{
		  /* read number of milliseconds */

		  p++;
		  if (p < strend && char_isdigit (*p))
		    {
		      msec += DECODE (*p++) * 100;
		      if (p < strend && char_isdigit (*p))
			{
			  msec += DECODE (*p++) * 10;
			  if (p < strend && char_isdigit (*p))
			    {
			      msec += DECODE (*p++);

			      while (p < strend && char_isdigit (*p))
				{
				  p++;
				}
			    }
			}
		    }
		}
	      break;
	    case 7:
	      /* YY-MM-DD H */
	      y = DECODE (p[0]) * 10 + DECODE (p[1]);
	      mo = DECODE (p[2]) * 10 + DECODE (p[3]);
	      d = DECODE (p[4]) * 10 + DECODE (p[5]);
	      h = DECODE (p[6]);
	      p += 7;
	      break;
	    default:
	      /* should not be reached */
	    case 8:
	      /* DD HH:MM:SS */
	      return NULL;
	    }
	}

      /* year, month, day, hour, minute, seconds have been read */
      ndigits = CAST_BUFLEN (q - r);
      if (ndigits > 6)
	{
	  /* a date precedes the time in the input string */

	  DB_DATE cdate;
	  int year, month, day;

	  /* year is also specified in the date */
	  if ((ndigits == 12) || (ndigits == 7))
	    {
	      /* 2-digits year specified, fill in the century */
	      if (y < 70)
		{
		  y += 2000;
		}
	      else
		{
		  y += 1900;
		}
	    }
	  else if (ndigits <= 10)
	    {
	      /* No year specified with the date, fill in the current year */
	      y = get_current_year ();
	    }

	  /* check for a valid date preceding the time in the input string */
	  cdate = julian_encode (mo, d, y);
	  julian_decode (cdate, &month, &day, &year, NULL);

	  if (y != year || mo != month || d != day)
	    {
	      /* invalid date specified in front of the time in the input string */
	      return NULL;
	    }
	}

      /* look for either the local or the English am/pm strings */
      /* skip optional whitespace before the am/pm string following the numeric time value */
      q = p;			/* save p just in case there is no am/pm string */

      while (p < strend && char_isspace (*p))
	{
	  p++;
	}

      if (is_local_am_str (p, strend) && (p + local_am_strlen == strend || !char_isalpha (p[local_am_strlen])))
	{
	  p += local_am_strlen;
	  if (h == 12)
	    {
	      h = 0;		/* 12:01am means 00:01 */
	    }
	}
      else if (is_local_pm_str (p, strend) && (p + local_pm_strlen == strend || !char_isalpha (p[local_pm_strlen])))
	{
	  p += local_pm_strlen;
	  if (h < 12)
	    {
	      /* only a 12-hour clock uses the am/pm string */
	      h += 12;
	    }
	}
      else
	{
	  p = q;
	}

      /* check and return the parsed time value */
      if (h > 23 || m > 59 || s > 59)
	{
	  return NULL;
	}
      else
	{
	  *mtime = encode_mtime (h, m, s, msec);
	  return p;
	}
    }
  else
    {
      /* No time field nor a decimal point are present */
      return NULL;
    }
}

/* parse_timestamp_compact() - Reads a DATETIME in
 *				    [YYYY] MM DD [HH [MM [SS ["."] [msec]]]]
 *				    format
 * returns:	    pointer into the input string pointer to the char after
 *		    the read timestamp, if successfull, or NULL otherwise
 * str(in):	    the input string with the compact timestamp to be
 *		    converted
 * strend(in):  the end of the string to be parsed
 * date(out):	    the parsed julian date, if successfull
 * mtime(out):	    the parsed time of day (with milliseconds) if successfull
 * has_explicit_time(out):
 *		    If not NULL and parsing sucessfull, will receive true if
 *		    the input string explicitly specifies the time part
 * has_explicit_msec(out):
 *		    If not NULL and parsing successfull, will receive true if
 *		    the input string explicitly specifies the number of
 *		    milliseconds or the decimal point for time
 *
 */
static char const *
parse_timestamp_compact (char const *str, char const *strend, DB_DATE * date, unsigned int *mtime,
			 bool * has_explicit_time, bool * has_explicit_msec)
{
  int y = 0, mo = 0, d = 0, h = 0, m = 0, s = 0, msec = 0, ndigits = 0;
  char const *p = str, *q = str;

  /* skip leading whitespace */
  while (p < strend && char_isspace (*p))
    {
      p++;
    }

  /* cound number of continous digits in the string */
  q = p;

  while (q < strend && char_isdigit (*q))
    {
      q++;
    }

  ndigits = CAST_BUFLEN (q - p);

  if (ndigits < 3)
    {
      /* no, or too few, datetime fields present */
      return NULL;
    }

  switch (ndigits)
    {
    case 7:
      /* YY MM DD H */
      y = DECODE (p[0]) * 10 + DECODE (p[1]);
      mo = DECODE (p[2]) * 10 + DECODE (p[3]);
      d = DECODE (p[4]) * 10 + DECODE (p[5]);
      h = DECODE (p[6]);
      p += 7;
      break;
    case 8:
      /* YYYY MM DD */
      y += DECODE (*p++);
      /* YYY MM DD */
      y *= 10;
      y += DECODE (*p++);
      /* FALLTHRU */
    case 6:
      /* YY MM DD */
      y *= 10;
      y += DECODE (*p++);
      /* FALLTHRU */
    case 5:
      /* Y MM DD */
      y *= 10;
      y += DECODE (*p++);
      /* FALLTHRU */
    case 4:
      /* MM DD */
      mo += DECODE (*p++);
      /* FALLTHRU */
    case 3:
      /* M DD */
      mo *= 10;
      mo += DECODE (*p++);

      d += DECODE (*p++);
      d *= 10;
      d += DECODE (*p++);

      /* HH:MM:SS remain 00:00:00 */
      break;
    default:
      /* YYYY MM DD HH [MM [SS ["."] [sssss]]] */

      /* read year */
      if (ndigits < 14)
	{
	  y = DECODE (p[0]) * 10 + DECODE (p[1]);
	  if (y < 70)
	    {
	      y += 2000;
	    }
	  else
	    {
	      y += 1900;
	    }

	  p += 2;
	}
      else
	{
	  y = DECODE (p[0]) * 1000 + DECODE (p[1]) * 100 + DECODE (p[2]) * 10 + DECODE (p[3]);

	  p += 4;
	}

      /* read month, day, hour, minute */
      mo = DECODE (p[0]) * 10 + DECODE (p[1]);
      d = DECODE (p[2]) * 10 + DECODE (p[3]);
      h = DECODE (p[4]) * 10 + DECODE (p[5]);
      p += 6;

      m = DECODE (*p++);
      if (p < strend && char_isdigit (*p))
	{
	  m *= 10;
	  m += DECODE (*p++);

	  if (p < strend && char_isdigit (*p))
	    {
	      s += DECODE (*p++);

	      if (p < strend && char_isdigit (*p))
		{
		  s *= 10;
		  s += DECODE (*p++);
		}

	      /* read milliseconds */
	      if (*p == '.')
		{
		  if (has_explicit_msec)
		    {
		      *has_explicit_msec = true;
		    }
		  p++;
		}

	      if (p < strend && char_isdigit (*p))
		{
		  if (has_explicit_msec)
		    {
		      *has_explicit_msec = true;
		    }

		  msec += DECODE (*p++) * 100;

		  if (p < strend && char_isdigit (*p))
		    {
		      msec += DECODE (*p++) * 10;

		      if (p < strend && char_isdigit (*p))
			{
			  msec += DECODE (*p);

			  /* skip remaining digits */
			  while (p < strend && char_isdigit (*p))
			    {
			      p++;
			    }
			}
		    }
		}
	    }
	}
    }

  if (has_explicit_time)
    {
      *has_explicit_time = (ndigits == 7 || ndigits > 8);
    }

  if (ndigits < 5)
    {
      /* [M]M DD format */
      y = get_current_year ();
    }
  else
    {
      if (ndigits == 6 || ndigits == 7)
	{
	  /* YY MM DD */
	  if (y < 70)
	    {
	      y += 2000;
	    }
	  else
	    {
	      y += 1900;
	    }
	}
    }


  /* y, mo, d, h, m, s and msec are now read from string, p is pointing past last digit read */

  if (ndigits > 8 || ndigits == 7)
    {
      /* the hour or the time-of-day are included in the compact string look for either the local or the English am/pm
       * strings */

      /* skip optional whitespace before the am/pm string following the numeric time value */
      q = p;			/* save p just in case there is no am/pm string */

      while (p < strend && char_isspace (*p))
	{
	  p++;
	}

      if (is_local_am_str (p, strend) && (p + local_am_strlen == strend || !char_isalpha (p[local_am_strlen])))
	{
	  p += local_am_strlen;
	  if (h == 12)
	    {
	      h = 0;		/* 12:01am means 00:01 */
	    }
	}
      else if (is_local_pm_str (p, strend) && (p + local_pm_strlen == strend || !char_isalpha (p[local_pm_strlen])))
	{
	  p += local_pm_strlen;
	  if (h < 12)
	    {
	      /* only a 12-hour clock uses the am/pm string */
	      h += 12;
	    }
	}
      else
	{
	  p = q;
	}
    }

  /* check and return the date and time read */
  if (mo <= 12 && d <= 31 && h <= 23 && m <= 59 && s <= 59)
    {
      DB_DATE cdate = julian_encode (mo, d, y);
      int year, month, day;

      julian_decode (cdate, &month, &day, &year, NULL);

      if (y != year || mo != month || d != day)
	{
	  /* invalid date in the input string */
	  return NULL;
	}

      *date = cdate;
      *mtime = encode_mtime (h, m, s, msec);

      return (p == strend ? "" : p);
    }
  else
    {
      /* Invalid date or time specified */
      return NULL;
    }
}


/*
 * parse_timedate_separated() - Reads a time and a date from a time-date
 *				string. Note that there is no compact timedate
 *				possible, only separated timedate strings.
 * returns:	    pointer to the character in str immediately following the
 *		    parsed time and date
 * str(in):	    the time-date string with the time and the date to be
 *		    parsed (read)
 * strend(in):      the end of the string to be parsed
 * date(out):	    the parsed date from the input string str
 * mtime(out):	    the parsed time from the input string str
 * syntax_check(out):
 *		    If not NULL and if parsing was successfull but the found
 *		    value invalid (like '10:80:12 2011-12-18') it will receive
 *		    the pointer to the character in str immediately following
 *		    the parsed time and date.
 * has_explicit_msec(out):
 *		    If not NULL and parsing successfull will receive the value
 *		    true if the input string explicitly specifies the number
 *		    of milliseconds for time or specifies the decimal point
 *		    for milliseconds
 */
static char const *
parse_timedate_separated (char const *str, char const *strend, DB_DATE * date, unsigned int *mtime, char const **syntax,
			  bool * has_explicit_msec)
{
  char sep_ch = '0';
  char const *p = str, *syntax_check = NULL;

  /* attempts to read time in explicit format */
  p = parse_explicit_mtime_separated (p, strend, mtime, &syntax_check, has_explicit_msec);

  if (!p && !syntax_check)
    {
      /* attempt to read time in the relaxed format if explicit format failed */
      if (has_explicit_msec)
	{
	  *has_explicit_msec = false;
	}

      p = parse_mtime_separated (str, strend, mtime, &syntax_check, NULL, &sep_ch, has_explicit_msec, false);

      if (p || syntax_check)
	{
	  if (sep_ch != '0' && sep_ch != ':')
	    {
	      /* the parsed time uses no ':' separator, so fallback to reading a date-time string instead of a
	       * time-date string */
	      p = NULL;
	      syntax_check = NULL;
	    }
	}
    }

  if (p || syntax_check)
    {
      bool space_separator = false;

      if (!syntax_check)
	{
	  syntax_check = p;
	}

      while (syntax_check < strend && !char_isalpha (*syntax_check) && !char_isdigit (*syntax_check))
	{
	  if (char_isspace (*syntax_check++))
	    {
	      space_separator = true;
	    }
	}

      if (space_separator)
	{
	  char const *q = syntax_check;

	  syntax_check = NULL;
	  sep_ch = '0';

	  if (p)
	    {
	      p = parse_date_separated (q, strend, date, &syntax_check, NULL, &sep_ch);

	      if (p || syntax_check)
		{
		  if (sep_ch == '-' || sep_ch == '/')
		    {
		      if (p)
			{
			  /* parsed a time-date string with valid values */
			  return p;
			}
		      else
			{
			  /* parsed a time-date string, with an invalid date value */
			  *syntax = syntax_check;
			  return NULL;
			}
		    }
		}
	    }
	  else
	    {
	      p = parse_date_separated (q, strend, date, &syntax_check, NULL, &sep_ch);

	      if ((p || syntax_check) && (sep_ch == '-' || sep_ch == '/'))
		{
		  if (p)
		    {
		      *syntax = p;
		    }
		  else
		    {
		      *syntax = syntax_check;
		    }

		  /* parsed a time-date string, with an invalid time value */
		  return NULL;
		}
	    }
	}
    }

  return NULL;
}

/*
 * db_date_parse_time() - Reads a TIME from a time or date-time (or
 *			  time-date) string, in any of the separated
 *			  or compact formats the string is in.
 * returns:	0 on success, ER_DATE_CONVERSION on error.
 * str(in):	the time or date-time string to be converted
 * str_len(in):	the length of the string to be converted
 * time(out):	the converted time.
 * millisecond(out):   the milliseconds part of the converted time
 */
int
db_date_parse_time (char const *str, int str_len, DB_TIME * time, int *millisecond)
{
  char const *syntax_check = NULL;
  int year_digits = 0;
  DB_DATE date = 0;
  unsigned int mtime = 0;
  char sep_ch = '0';
  char const *strend = str + str_len;
  /* attempt to read a time-date string first */
  char const *p = parse_timedate_separated (str, strend, &date, &mtime, &syntax_check,
					    NULL);

  if (p)
    {
      *time = mtime / 1000;
      *millisecond = mtime % 1000;
      return NO_ERROR;
    }
  else
    {
      if (syntax_check)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	  return ER_TIME_CONVERSION;
	}
      else
	{
	  date = 0;
	  mtime = 0;
	}
    }

  /* attempt to read a separated DATETIME string after parsing a time-date string failed */

  p = parse_date_separated (str, strend, &date, &syntax_check, &year_digits, &sep_ch);

  if (p || syntax_check)
    {
      bool space_separator = false;
      bool has_explicit_date_part = false;

      /* check whether string has explicit date part */
      if (sep_ch == '-' || sep_ch == '/')
	{
	  has_explicit_date_part = true;
	}

      if (!syntax_check)
	{
	  syntax_check = p;
	}

      while (syntax_check < strend && !char_isalpha (*syntax_check) && !char_isdigit (*syntax_check))
	{
	  if (char_isspace (*syntax_check++))
	    {
	      space_separator = true;
	    }
	}

      if (space_separator)
	{
	  if (p)
	    {
	      /* Valid date followed by separator */
	      int time_parts = 0;

	      p = syntax_check;
	      syntax_check = NULL;
	      p = parse_mtime_separated (p, strend, &mtime, &syntax_check, &time_parts, NULL, NULL, false);

	      if (p)
		{
		  while (p < strend && char_isspace (*p))
		    {
		      p++;
		    }

		  /* if there is one non-space character in remaining characters */
		  if (p != strend)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
		      return ER_TIME_CONVERSION;
		    }

		  if (year_digits >= 3 || (time_parts >= 2 && year_digits))
		    {
		      *time = mtime / 1000;
		      *millisecond = mtime % 1000;
		      return NO_ERROR;
		    }
		}
	      else
		{
		  if (syntax_check)
		    {
		      /* date-time string with an invalid time */
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
		      return ER_TIME_CONVERSION;
		    }
		  else
		    {
		      /* no time value found following the date and separator check whether is a Date string with space
		       * suffix for example,"2012-8-15 " */
		      if (has_explicit_date_part)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
			  return ER_TIME_CONVERSION;
			}
		    }
		}
	    }
	  else
	    {
	      /* Invalid date followed by separator, */
	      int time_parts = 0;

	      p = syntax_check;
	      syntax_check = NULL;
	      p = parse_mtime_separated (p, strend, &mtime, &syntax_check, &time_parts, NULL, NULL, false);
	      if (p || syntax_check)
		{
		  /* date-time string with an invalid date and/or time */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
		  return ER_TIME_CONVERSION;
		}
	      else
		{
		  if (has_explicit_date_part)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
		      return ER_TIME_CONVERSION;
		    }
		}
	    }
	}
      else
	{
	  if (p && has_explicit_date_part)
	    {
	      /* only explicit Date type, should return an error. */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	      return ER_TIME_CONVERSION;
	    }
	}
    }

  /* attempt to read an explicit separated TIME string (a time-only string) */
  mtime = 0;
  syntax_check = NULL;

  p = parse_explicit_mtime_separated (str, strend, &mtime, &syntax_check, NULL);

  if (p)
    {
      if (p < strend && p[0] == ' ')
	{
	  while (p < strend && char_isspace (*p))
	    {
	      p++;
	    }

	  /* if there is one non-space character in remaining characters */
	  if (p != strend)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	      return ER_TIME_CONVERSION;
	    }
	}

      *time = mtime / 1000;
      *millisecond = mtime % 1000;
      return NO_ERROR;
    }
  else
    {
      if (syntax_check)
	{
	  /* Time could be parsed as [[HH : ]MM] : SS[.sss], but is an invalid time value */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	  return ER_TIME_CONVERSION;
	}
      else
	{
	  /* read a compact TIME string */
	  p = parse_explicit_mtime_compact (str, strend, &mtime);

	  if (p)
	    {
	      while (p < strend && char_isspace (*p))
		{
		  p++;
		}

	      /* if remaining characters includes one non-space character */
	      if (p != strend)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
		  return ER_TIME_CONVERSION;
		}

	      *time = mtime / 1000;
	      *millisecond = mtime % 1000;
	      return NO_ERROR;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	      return ER_TIME_CONVERSION;
	    }
	}
    }
}

/*
 * db_date_parse_datetime_parts() - Reads a DATETIME from a DATE, DATETIME or
 *			    time-date string, in any of the separated or
 *			    compact formats the string might be in.
 * returns:	0 on success, ER_DATE_CONVERSION on error.
 * str(in):	the date or date-time string to be read and converted
 * str_len(in): the length of the string to be converted
 * datetime(out):
 *		the read and converted datetime
 * is_explicit_time(out):
 *		If not NULL and parsing successfull will receive true if the
 *		input string explicitly specifies the time part
 * has_explicit_msec(out):
 *		If not NULL and parsing successfull will receive true if the
 *		input string explicitly specifies the number of milliseconds
 *		or the decimal point for the time part
 * fits_as_timestamp(out):
 *		If not NULL and has_explicit_msec is not NULL and parsing
 *		is successfull will receive true if the	value parsed from
 *		the given input string can be represented exactly as a
 *		TIMESTAMP value
 * endp(out):	if the datetime is found to have compact format (110918, as
 *		opposed to 11-09-18), endp receives the pointer to the end of
 *		the source string or to the trailing character in the source
 *		string that was no longer part of the datetime value to be
 *		read. if given, the pointed value should be NULL before entry
 *		to the function
 */
int
db_date_parse_datetime_parts (char const *str, int str_len, DB_DATETIME * datetime, bool * has_explicit_time,
			      bool * has_explicit_msec, bool * fits_as_timestamp, char const **endp)
{
  DB_DATE date = 0;
  unsigned int mtime = 0;
  char const *strend = str + str_len;
  char const *syntax_check = NULL, *p;

  /* read a separated time-date string first */
  p = parse_timedate_separated (str, strend, &date, &mtime, &syntax_check, has_explicit_msec);

  if (p)
    {
      if (has_explicit_time)
	{
	  *has_explicit_time = true;
	}

      if (has_explicit_msec && !*has_explicit_msec && fits_as_timestamp)
	{
	  DB_TIMESTAMP timestamp;
	  DB_TIME time = mtime / 1000;

	  *fits_as_timestamp = db_timestamp_encode_utc (&date, &time, &timestamp);
	  if (*fits_as_timestamp != NO_ERROR)
	    {
	      er_clear ();
	    }
	}

      datetime->date = date;
      datetime->time = mtime;

      goto finalcheck;
    }
  else
    {
      if (syntax_check)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
	  return ER_TIMESTAMP_CONVERSION;
	}
    }

  /* read a separated DATETIME string */
  if (has_explicit_msec)
    {
      *has_explicit_msec = true;
    }

  p = parse_date_separated (str, strend, &datetime->date, &syntax_check, NULL, NULL);
  if (p)
    {
      char const *q, *r;
      char sep_ch = '0';

      syntax_check = NULL;

      /* skip the date and time separator from the string */
      while (p < strend && !char_isalpha (*p) && !char_isdigit (*p))
	{
	  p++;
	}

      /* parse the time portion in the string */
      q = parse_mtime_separated (p, strend, &datetime->time, &syntax_check, NULL, &sep_ch, has_explicit_msec, true);
      if (q)
	{
	  if (has_explicit_time)
	    {
	      r = q;
	      while (r < strend && ((*r == sep_ch) || (char_isdigit (*r))))
		{
		  r++;
		}
	      if ((r < strend) && (!char_isspace (*r)))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
		  return ER_TIMESTAMP_CONVERSION;
		}
	      *has_explicit_time = true;
	    }

	  if (has_explicit_msec && !*has_explicit_msec && fits_as_timestamp)
	    {
	      DB_TIMESTAMP timestamp;
	      DB_TIME time = datetime->time / 1000;

	      *fits_as_timestamp = db_timestamp_encode_utc (&datetime->date, &time, &timestamp);
	      if (*fits_as_timestamp != NO_ERROR)
		{
		  er_clear ();
		}
	    }

	  goto finalcheck;
	}
      else
	{
	  if (syntax_check)
	    {
	      /* Invalid time value present in the string */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
	      return ER_TIMESTAMP_CONVERSION;
	    }
	  else
	    {
	      /* no time value present */
	      if (has_explicit_time)
		{
		  *has_explicit_time = false;
		}

	      if (has_explicit_msec)
		{
		  *has_explicit_msec = false;
		}

	      if (fits_as_timestamp)
		{
		  DB_TIMESTAMP timestamp;
		  DB_TIME time = 0;

		  *fits_as_timestamp = db_timestamp_encode_utc (&datetime->date, &time, &timestamp);
		  if (*fits_as_timestamp != NO_ERROR)
		    {
		      er_clear ();
		    }
		}

	      datetime->time = 0;

	      goto finalcheck;
	    }
	}
    }
  else
    {
      char const *r;

      if (syntax_check)
	{
	  /* try to parse the first date portion as YY MM DD (or more) */
	  DB_DATETIME cdatetime;

	  while (str < strend && char_isspace (*str))
	    {
	      str++;
	    }

	  r =
	    parse_timestamp_compact (str, strend, &cdatetime.date, &cdatetime.time, has_explicit_time,
				     has_explicit_msec);

	  /* mysql prefers a large value here like 14, and returns NULL otherwise */
	  if (r && (*r == 0 || (*r && (r - str >= 6))))
	    {
	      if (has_explicit_msec && !*has_explicit_msec && fits_as_timestamp)
		{
		  DB_TIMESTAMP timestamp;
		  DB_TIME time = cdatetime.time / 1000;

		  *fits_as_timestamp = db_timestamp_encode_utc (&cdatetime.date, &time, &timestamp);
		  if (*fits_as_timestamp != NO_ERROR)
		    {
		      er_clear ();
		    }
		}

	      if (endp)
		{
		  *endp = r;
		}
	      *datetime = cdatetime;

	      goto finalcheck;
	    }
	  else
	    {
	      /* invalid date value present in the string */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
	      return ER_TIMESTAMP_CONVERSION;
	    }
	}
      else
	{
	  /* read a compact DATETIME string */
	  r =
	    parse_timestamp_compact (str, strend, &datetime->date, &datetime->time, has_explicit_time,
				     has_explicit_msec);

	  if (r)
	    {
	      if (has_explicit_msec && !*has_explicit_msec && fits_as_timestamp)
		{
		  DB_TIMESTAMP timestamp;
		  DB_TIME time = datetime->time / 1000;

		  *fits_as_timestamp = db_timestamp_encode_utc (&datetime->date, &time, &timestamp);
		  if (*fits_as_timestamp != NO_ERROR)
		    {
		      er_clear ();
		    }
		}

	      if (endp)
		{
		  *endp = r;
		}

	      goto finalcheck;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
	      return ER_TIMESTAMP_CONVERSION;
	    }
	}
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
  return ER_TIMESTAMP_CONVERSION;

finalcheck:
  if (datetime->date == IGREG_SPECIAL)
    {
      if (datetime->time != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
	  return ER_TIMESTAMP_CONVERSION;
	}
    }

  return NO_ERROR;
}

/*
 * db_date_parse_datetime() - Reads a DATETIME from a DATE or DATETIME string, in any
 *			    of the separated or compact formats the string
 *			    might be in.
 * returns:	0 on success, ER_DATE_CONVERSION on error.
 * str(in):	the date or date-time string to be read and converted
 * str_len(in): the length of the string to be converted
 * datetime(out):
 *		the read and converted datetime
 */
int
db_date_parse_datetime (char const *str, int str_len, DB_DATETIME * datetime)
{
  return db_date_parse_datetime_parts (str, str_len, datetime, NULL, NULL, NULL, NULL);
}

/*
 * db_date_parse_timestamp() - Reads a TIMESTAMP from a DATE or DATETIME
 *			    string, in any of the separated or compact formats
 *			    the string might be in.
 * returns:	0 on success, ER_DATE_CONVERSION on error.
 * str(in):	the date string or datetime string to be read and converted
 * str_len(in): the length of the string to be converted
 * utime(out):	the converted timestamp read from string
 */
int
db_date_parse_timestamp (char const *str, int str_len, DB_TIMESTAMP * utime)
{
  DB_DATETIME datetime;
  DB_TIME time;
  int err;

  err = db_date_parse_datetime (str, str_len, &datetime);
  if (err == NO_ERROR)
    {
      if (datetime.date == IGREG_SPECIAL && datetime.time == 0)
	{
	  *utime = IGREG_SPECIAL;
	  return NO_ERROR;
	}

      time = datetime.time / 1000;
      if (db_timestamp_encode_ses (&datetime.date, &time, utime, NULL) == NO_ERROR)
	{
	  return NO_ERROR;
	}
      else
	{
	  er_clear ();
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
	  return ER_TIMESTAMP_CONVERSION;
	}
    }

  er_clear ();
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION, 0);
  return ER_TIMESTAMP_CONVERSION;
}

/*
 * db_date_parse_date() - Reads a DATE from a DATE string or a DATETIME
 *			    string, in any of the separated or compact formats
 *			    the string might be in.
 * returns:	0 on success, ER_DATE_CONVERSION on error.
 * str(in):	the date or datetime string to be read and converted
 * str_len(in): the length of the string to be converted
 * date(out):	the read and converted date
 */
int
db_date_parse_date (char const *str, int str_len, DB_DATE * date)
{
  DB_DATETIME datetime = { 0, 0 };
  int err;

  err = db_date_parse_datetime (str, str_len, &datetime);
  if (err == NO_ERROR)
    {
      *date = datetime.date;
    }
  else
    {
      er_clear ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      err = ER_DATE_CONVERSION;
    }

  return err;
}

/*
 * parse_for_timestamp() - Tries to parse a timestamp by finding a date and a
 *			   time, in order.
 * Returns: const char or NULL on error
 * buf(in): pointer to a date-time expression
 * buf_len(in): the length of the string to be parsed
 * date(out): pointer to a DB_DATE
 * time(out): pointer to a DB_TIME
 * allow_msec(in): tells if milliseconds format is allowed
 */
static const char *
parse_for_timestamp (const char *buf, int buf_len, DB_DATE * date, DB_TIME * time, bool allow_msec)
{
  const char *p;

  /* First try to parse a date followed by a time. */
  p = parse_date (buf, buf_len, date);
  if (p)
    {
      if (allow_msec)
	{
	  p = parse_mtime (p, buf_len - CAST_BUFLEN (p - buf), time, NULL, NULL);
	  *time /= 1000;
	}
      else
	{
	  p = parse_time (p, buf_len - CAST_BUFLEN (p - buf), time);
	}
      if (p)
	{
	  goto finalcheck;
	}
    }

  /* If that fails, try to parse a time followed by a date. */
  if (allow_msec)
    {
      p = parse_mtime (buf, buf_len, time, NULL, NULL);
      *time /= 1000;
    }
  else
    {
      p = parse_time (buf, buf_len, time);
    }

  if (p)
    {
      p = parse_date (p, buf_len - CAST_BUFLEN (p - buf), date);
      if (p)
	{
	  goto finalcheck;
	}
    }

  return NULL;

finalcheck:
  if (*date == IGREG_SPECIAL)
    {
      if (*time != 0)
	{
	  return NULL;
	}
    }

  return p;
}

/*
 * parse_datetime() -
 * Returns: const char or NULL on error
 * buf(in): pointer to a date-time expression
 * buf_len(in): the length of the string to be parsed
 * datetime(out): pointer to a DB_DATETIME to be modified
 */
static const char *
parse_datetime (const char *buf, int buf_len, DB_DATETIME * datetime)
{
  DB_DATE date = 0;
  unsigned int mtime;
  const char *p;

  /* First try to parse a date followed by a time. */
  p = parse_date (buf, buf_len, &date);
  if (p)
    {
      p = parse_mtime (p, buf_len - CAST_BUFLEN (p - buf), &mtime, NULL, NULL);
      if (p)
	{
	  goto finalcheck;
	}
    }

  p = parse_mtime (buf, buf_len, &mtime, NULL, NULL);
  if (p)
    {
      p = parse_date (p, buf_len - CAST_BUFLEN (p - buf), &date);
      if (p)
	{
	  goto finalcheck;
	}
    }

  return NULL;

finalcheck:
  if (date == IGREG_SPECIAL)
    {
      if (mtime != 0)
	{
	  return NULL;
	}
    }

  datetime->date = date;
  datetime->time = mtime;

  return p;
}

/*
 * db_string_check_explicit_date() - check if a string is formated as a date
 * return : true if str is formated exactly as a date
 *	    (not a datetime or a timestamp)
 * str(in): the string to be checked
 * str_len(in): the length of the string to be parsed
 */
bool
db_string_check_explicit_date (const char *str, int str_len)
{
  DB_DATE date;
  const char *result = NULL;

  result = parse_date (str, str_len, &date);
  if (result)
    {
      while (char_isspace (result[0]))
	{
	  result++;
	}
    }
  if (result == NULL || result[0] != '\0')
    {
      return false;
    }

  return true;
}

/*
 * db_string_to_date_ex() - Parse an ordinary date string (e.g., '10/15/86').
 *    Whitespace is not permitted between slashed components. If the year is
 *    omitted, the current year is assumed.  Dates are currently accepted
 *    only in the slashified US style.
 * returns: 0 on success, ER_DATE_CONVERSION on error
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * date(out): a pointer to a DB_DATE to be modified
 */
int
db_string_to_date_ex (const char *str, int str_len, DB_DATE * date)
{
  const char *p;
  const char *p_end = str + str_len;

  p = parse_date (str, str_len, date);
  if (p != NULL)
    {
      while (p < p_end && char_isspace (p[0]))
	{
	  p++;
	}
    }
  if (p == NULL || (p < p_end && p[0] != '\0'))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
}

/*
 * db_string_to_date() - Parse an ordinary date string (e.g., '10/15/86').
 *    Whitespace is not permitted between slashed components. If the year is
 *    omitted, the current year is assumed.  Dates are currently accepted
 *    only in the slashified US style.
 * returns: 0 on success, ER_DATE_CONVERSION on error
 * str(in): a buffer containing a date to be parsed
 * date(out): a pointer to a DB_DATE to be modified
 */
int
db_string_to_date (const char *str, DB_DATE * date)
{
  return db_string_to_date_ex (str, strlen (str), date);
}

/*
 * db_string_to_time_ex() - Parse an ordinary time string (e.g., '3:30am').
 *    Whitespace is not permitted between numeric components, it is permitted
 *    between the last number and the optional am/pm designator.
 * return : 0 on success, ER_DATE_CONVERSION on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * time(out): a pointer to a DB_TIME to be modified
 */
int
db_string_to_time_ex (const char *str, int str_len, DB_TIME * time)
{
  const char *p;
  const char *p_end = str + str_len;

  p = parse_time (str, str_len, time);
  if (p != NULL)
    {
      while (p < p_end && char_isspace (p[0]))
	{
	  p++;
	}
    }
  if (p == NULL || (p < p_end && p[0] != '\0'))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
}

/*
 * db_string_to_time() - Parse an ordinary time string (e.g., '3:30am').
 *    Whitespace is not permitted between numeric components, it is permitted
 *    between the last number and the optional am/pm designator.
 * return : 0 on success, ER_DATE_CONVERSION on error.
 * str(in): a buffer containing a date to be parsed
 * time(out): a pointer to a DB_TIME to be modified
 */
int
db_string_to_time (const char *str, DB_TIME * time)
{
  return db_string_to_time_ex (str, strlen (str), time);
}

/*
 * db_string_to_timestamp_ex() - Parse a date and time string into a utime.
 *    The time and date are parsed according to the same rules as
 *    db_string_to_time() and db_string_to_date().
 *    they may appear in either order.
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * utime(out): a pointer to a DB_TIMESTAMP to be modified
 */
int
db_string_to_timestamp_ex (const char *str, int str_len, DB_TIMESTAMP * utime)
{
  DB_DATE date;
  DB_TIME time;
  int err = NO_ERROR;
  const char *p, *p_end;
  TZ_ID dummy_tz_id;

  p_end = str + str_len;
  p = parse_for_timestamp (str, str_len, &date, &time, false);
  if (p != NULL)
    {
      while (p < p_end && char_isspace (p[0]))
	{
	  p++;
	}
    }
  if (p == NULL || (p < p_end && p[0] != '\0'))
    {
      goto error_exit;
    }

  /* 0000-00-00 00:00:00 treated as time_t 0 */
  if (date == IGREG_SPECIAL)
    {
      *utime = IGREG_SPECIAL;
      return err;
    }

  err = db_timestamp_encode_ses (&date, &time, utime, &dummy_tz_id);
  return err;

error_exit:
  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
  return ER_DATE_CONVERSION;
}

/*
 * db_string_to_timestamp() - Parse a date and time string into a utime.
 *    The time and date are parsed according to the same rules as
 *    db_string_to_time() and db_string_to_date().
 *    they may appear in either order.
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * utime(out): a pointer to a DB_TIMESTAMP to be modified
 */
int
db_string_to_timestamp (const char *str, DB_TIMESTAMP * utime)
{
  return db_string_to_timestamp_ex (str, strlen (str), utime);
}

/*
 * db_string_to_timestamptz_ex() - Parse a date and time with time zone info
 *				   into a timestamp with TZ

 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * ts_tz(out): a pointer to a DB_TIMESTAMPTZ to be modified
 * has_zone(out): true if string had valid zone information to decode, false
 *		  otherwise
 * is_cast(in): true if the function is called in a casting context
 */
int
db_string_to_timestamptz_ex (const char *str, int str_len, DB_TIMESTAMPTZ * ts_tz, bool * has_zone, bool is_cast)
{
  DB_DATE date;
  DB_TIME time;
  int err = NO_ERROR;
  int str_zone_size = 0;
  const char *p, *p_end, *str_zone;
  TZ_REGION session_tz_region;

  str_zone = NULL;
  p_end = str + str_len;
  *has_zone = false;

  tz_get_session_tz_region (&session_tz_region);
  ts_tz->tz_id = 0;

  p = parse_for_timestamp (str, str_len, &date, &time, is_cast);

  if (p == NULL)
    {
      goto error_exit;
    }

  assert (p != NULL);
  while (p < p_end && char_isspace (p[0]))
    {
      p++;
    }

  if (p < p_end)
    {
      *has_zone = true;
      str_zone = p;
      str_zone_size = CAST_BUFLEN (p_end - str_zone);
    }

  err = tz_create_timestamptz (&date, &time, str_zone, str_zone_size, &session_tz_region, ts_tz, &p);
  if (err != NO_ERROR || str_zone == NULL)
    {
      /* error or no timezone in user string (no trailing chars to check) */
      return err;
    }

  if (p != NULL)
    {
      while (p < p_end && char_isspace (p[0]))
	{
	  p++;
	}
    }

  if (p == NULL || (p < p_end && p[0] != '\0'))
    {
      goto error_exit;
    }

  return err;

error_exit:
  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
  return ER_DATE_CONVERSION;
}

/*
 * db_string_to_timestamptz() - Parse a date and time with time zone info into
 *				a timestamp with TZ

 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * ts_tz(out): a pointer to a DB_TIMESTAMPTZ to be modified
 * has_zone(out): true if string had valid zone information to decode, false
 *		  otherwise
 */
int
db_string_to_timestamptz (const char *str, DB_TIMESTAMPTZ * ts_tz, bool * has_zone)
{
  return db_string_to_timestamptz_ex (str, strlen (str), ts_tz, has_zone, false);
}

/*
 * db_string_to_timestampltz_ex() - Parse a date and time into a timestamp
 *				    with local timezone
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * ts(out): a pointer to a DB_TIMESTAMP to be modified
 */
int
db_string_to_timestampltz_ex (const char *str, int str_len, DB_TIMESTAMP * ts)
{
  int error = NO_ERROR;
  DB_TIMESTAMPTZ ts_tz;
  bool dummy_has_zone;

  error = db_string_to_timestamptz_ex (str, str_len, &ts_tz, &dummy_has_zone, false);
  if (error != NO_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  *ts = ts_tz.timestamp;

  return error;
}

/*
 * db_string_to_timestampltz() - Parse a date and time into a timestamp with
 *				 local timezone

 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * ts(out): a pointer to a DB_TIMESTAMP to be modified
 */
int
db_string_to_timestampltz (const char *str, DB_TIMESTAMP * ts)
{
  return db_string_to_timestampltz_ex (str, strlen (str), ts);
}

/*
 * db_date_to_string() - Print a DB_DATE into a char buffer using strftime().
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * date(in): a pointer to a DB_DATE to be printed
 *
 * note: The format string MUST contain a %d in WINDOWS. Keep this file
 *    from passing through the %ld filter.
 *    Do note pass pointers to the tm structure to db_date_decode.
 */
int
db_date_to_string (char *buf, int bufsize, DB_DATE * date)
{
  int mon, day, year;
  const int len_out = 10;
  int cnt = 0;

  if (buf == NULL || bufsize == 0)
    {
      return 0;
    }
  if (bufsize <= len_out)
    {
      return 0;
    }

  db_date_decode (date, &mon, &day, &year);
  buf[cnt++] = mon / 10 + '0';
  buf[cnt++] = mon % 10 + '0';
  buf[cnt++] = '/';
  buf[cnt++] = day / 10 + '0';
  buf[cnt++] = day % 10 + '0';
  buf[cnt++] = '/';
  buf[cnt++] = year / 1000 + '0';
  buf[cnt++] = (year / 100) % 10 + '0';
  buf[cnt++] = (year / 10) % 10 + '0';
  buf[cnt++] = year % 10 + '0';
  buf[cnt] = '\0';

  return cnt;
}

/*
 * db_time_to_string() - Print a DB_TIME into a char buffer using strftime().
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * time(in): a pointer to a DB_TIME to be printed
 *
 * note : DO NOT pass pointers to the tm structure to db_time_decode.
 */
int
db_time_to_string (char *buf, int bufsize, DB_TIME * time)
{
  int hour, min, sec;
  bool pm;
  int cnt = 0;
  const int len_out = 11;

  if (buf == NULL || bufsize == 0)
    {
      return 0;
    }
  if (bufsize <= len_out)
    {
      return 0;
    }

  db_time_decode (time, &hour, &min, &sec);
  pm = (hour >= 12) ? true : false;
  if (hour == 0)
    {
      hour = 12;
    }
  else if (hour > 12)
    {
      hour -= 12;
    }

  buf[cnt++] = hour / 10 + '0';
  buf[cnt++] = hour % 10 + '0';
  buf[cnt++] = ':';
  buf[cnt++] = min / 10 + '0';
  buf[cnt++] = min % 10 + '0';
  buf[cnt++] = ':';
  buf[cnt++] = sec / 10 + '0';
  buf[cnt++] = sec % 10 + '0';
  buf[cnt++] = ' ';
  if (pm)
    {
      buf[cnt++] = 'P';
      buf[cnt++] = 'M';
    }
  else
    {
      buf[cnt++] = 'A';
      buf[cnt++] = 'M';
    }
  buf[cnt] = '\0';

  return cnt;
}

/*
 * db_timestamp_to_string() - Print a DB_TIMESTAMP into a char buffer using
 *    strftime().
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * utime(in): a pointer to a DB_TIMESTAMP to be printed
 */
int
db_timestamp_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime)
{
  DB_DATE date;
  DB_TIME time;
  int m, n;

  (void) db_timestamp_decode_ses (utime, &date, &time);
  m = db_time_to_string (buf, bufsize, &time);
  if (m == 0)
    {
      return 0;
    }
  if (bufsize - m < 2)
    {
      return 0;
    }
  buf[m] = ' ';
  m += 1;
  n = db_date_to_string (buf + m, bufsize - m, &date);
  if (n == 0)
    {
      return 0;
    }
  return m + n;
}

/*
 * db_timestamptz_to_string() - Print a DB_TIMESTAMP and a time zone into a
 *				 char buffer
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * utime(in): a pointer to a DB_TIMESTAMP to be printed
 * tz_id(in): reference timezone
 */
int
db_timestamptz_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime, const TZ_ID * tz_id)
{
  int n, res;
  DB_DATE date;
  DB_TIME time;
  int err = NO_ERROR;

  err = db_timestamp_decode_w_tz_id (utime, tz_id, &date, &time);
  if (err != NO_ERROR)
    {
      return 0;
    }

  res = db_time_to_string (buf, bufsize, &time);
  if (res == 0)
    {
      return 0;
    }

  if (bufsize - res < 2)
    {
      return 0;
    }
  n = res;
  buf[n] = ' ';
  n += 1;
  res = db_date_to_string (buf + n, bufsize - n, &date);
  if (res == 0)
    {
      return 0;
    }

  n += res;

  buf[n] = ' ';
  n += 1;

  res = tz_id_to_str (tz_id, buf + n, bufsize - n);
  if (res < 0)
    {
      return 0;
    }

  n += res;
  return n;
}

/*
 * db_timestampltz_to_string() - Print a DB_TIMESTAMP with session time zone
 *				 into a char buffer using
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * utime(in): a pointer to a DB_TIMESTAMP to be printed
 */
int
db_timestampltz_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime)
{
  int err = NO_ERROR;
  TZ_ID ses_tz_id;

  err = tz_create_session_tzid_for_timestamp (utime, &ses_tz_id);
  if (err != NO_ERROR)
    {
      return 0;
    }

  return db_timestamptz_to_string (buf, bufsize, utime, &ses_tz_id);
}

/*
 * DB_DATETIME FUNCTIONS
 */

/*
 * encode_mtime() -
 * return : millisecond time value
 * hour(in): hour
 * minute(in): minute
 * second(in): second
 * millisecond(in): millisecond
 */
static unsigned int
encode_mtime (int hour, int minute, int second, int millisecond)
{
  return ((((((hour * 60) + minute) * 60) + second) * 1000) + millisecond);
}

/*
 * decode_mtime() - Converts encoded millisecond time into
 *                  hour/minute/second/millisecond values.
 * return : void
 * time(in): encoded relative time value
 * hourp(out): hour pointer
 * minutep(out) : minute pointer
 * secondp(out) : second pointer
 * millisecondp(out) : millisecond pointer
 */
static void
decode_mtime (int mtimeval, int *hourp, int *minutep, int *secondp, int *millisecondp)
{
  int hours, minutes, seconds, milliseconds;

  milliseconds = mtimeval % 1000;
  seconds = (mtimeval / 1000) % 60;
  minutes = (mtimeval / 60000) % 60;
  hours = (mtimeval / 3600000) % 24;

  if (hourp != NULL)
    {
      *hourp = hours;
    }
  if (minutep != NULL)
    {
      *minutep = minutes;
    }
  if (secondp != NULL)
    {
      *secondp = seconds;
    }
  if (millisecondp != NULL)
    {
      *millisecondp = milliseconds;
    }
}

/*
 * db_datetime_to_string() - Print a DB_DATETIME into a char buffer using
 *    strftime().
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * datetime(in): a pointer to a DB_DATETIME to be printed
 */
int
db_datetime_to_string (char *buf, int bufsize, DB_DATETIME * datetime)
{
  int mon, day, year;
  int hour, minute, second, millisecond;
  bool pm;
  int cnt = 0;
  const int len_out = 26;

  if (buf == NULL || bufsize == 0)
    {
      return 0;
    }
  if (bufsize <= len_out)
    {
      return 0;
    }

  db_datetime_decode (datetime, &mon, &day, &year, &hour, &minute, &second, &millisecond);
  pm = (hour >= 12) ? true : false;
  if (hour == 0)
    {
      hour = 12;
    }
  else if (hour > 12)
    {
      hour -= 12;
    }

  buf[cnt++] = hour / 10 + '0';
  buf[cnt++] = hour % 10 + '0';
  buf[cnt++] = ':';
  buf[cnt++] = minute / 10 + '0';
  buf[cnt++] = minute % 10 + '0';
  buf[cnt++] = ':';
  buf[cnt++] = second / 10 + '0';
  buf[cnt++] = second % 10 + '0';
  buf[cnt++] = '.';
  buf[cnt++] = millisecond / 100 + '0';
  buf[cnt++] = (millisecond / 10) % 10 + '0';
  buf[cnt++] = millisecond % 10 + '0';
  buf[cnt++] = ' ';
  if (pm)
    {
      buf[cnt++] = 'P';
      buf[cnt++] = 'M';
    }
  else
    {
      buf[cnt++] = 'A';
      buf[cnt++] = 'M';
    }
  buf[cnt++] = ' ';
  buf[cnt++] = mon / 10 + '0';
  buf[cnt++] = mon % 10 + '0';
  buf[cnt++] = '/';
  buf[cnt++] = day / 10 + '0';
  buf[cnt++] = day % 10 + '0';
  buf[cnt++] = '/';
  buf[cnt++] = year / 1000 + '0';
  buf[cnt++] = (year / 100) % 10 + '0';
  buf[cnt++] = (year / 10) % 10 + '0';
  buf[cnt++] = year % 10 + '0';
  buf[cnt] = '\0';

  return cnt;
}

/*
 * db_datetimetz_to_string() - Print a DB_DATETIME with time zone into a char
 *			       buffer using timezone specified
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * dt(in): a pointer to a DB_DATETIME to be printed
 * tz_id(in): zone identifier
 */
int
db_datetimetz_to_string (char *buf, int bufsize, DB_DATETIME * dt, const TZ_ID * tz_id)
{
  int retval, n;
  DB_DATETIME dt_local;

  retval = tz_utc_datetimetz_to_local (dt, tz_id, &dt_local);
  if (retval == ER_QPROC_TIME_UNDERFLOW)
    {
      db_datetime_encode (&dt_local, 0, 0, 0, 0, 0, 0, 0);
      retval = NO_ERROR;
      er_clear ();
    }

  if (retval != NO_ERROR)
    {
      return 0;
    }

  retval = db_datetime_to_string (buf, bufsize, &dt_local);

  if (retval <= 0 || retval > bufsize + 1)
    {
      return retval;
    }
  n = retval;

  buf[n] = ' ';
  n++;

  retval = tz_id_to_str (tz_id, buf + n, bufsize - n);
  if (retval < 0)
    {
      return 0;
    }

  n += retval;
  return n;
}

/*
 * db_datetimeltz_to_string() - Print a DB_DATETIME with time zone into a char
 *				buffer using session local timezone
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * dt(in): a pointer to a DB_DATETIME to be printed
 */
int
db_datetimeltz_to_string (char *buf, int bufsize, DB_DATETIME * dt)
{
  int retval;
  TZ_ID tz_id;

  retval = tz_create_session_tzid_for_datetime (dt, true, &tz_id);
  if (retval != NO_ERROR)
    {
      return 0;
    }

  return db_datetimetz_to_string (buf, bufsize, dt, &tz_id);
}

/*
 * db_datetime_to_string2() - Print a DB_DATETIME into a char buffer using
 *    strftime().
 * return : the number of characters actually printed.
 * buf(out): a buffer to receive the printed representation
 * bufsize(in): the size of that buffer
 * datetime(in): a pointer to a DB_DATETIME to be printed
 *
 * Note: version without PM and AM and formatted YYYY-MM-DD HH:MM:SS.MMM
 */
int
db_datetime_to_string2 (char *buf, int bufsize, DB_DATETIME * datetime)
{
  int mon, day, year;
  int hour, minute, second, millisecond;
  int retval;

  if (buf == NULL || bufsize == 0)
    {
      return 0;
    }

  db_datetime_decode (datetime, &mon, &day, &year, &hour, &minute, &second, &millisecond);

  if (millisecond > 0)
    {
      retval =
	snprintf (buf, bufsize, "%04d-%02d-%02d %02d:%02d:%02d.%03d", year, mon, day, hour, minute, second,
		  millisecond);
    }
  else
    {
      retval = snprintf (buf, bufsize, "%04d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, minute, second);
    }
  if (bufsize < retval)
    {
      retval = 0;
    }

  return retval;
}

/*
 * db_string_to_datetime_ex() -
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * datetime(out): a pointer to a DB_DATETIME to be modified
 */
int
db_string_to_datetime_ex (const char *str, int str_len, DB_DATETIME * datetime)
{
  const char *p;
  const char *p_end = str + str_len;

  p = parse_datetime (str, str_len, datetime);
  if (p != NULL)
    {
      while (p < p_end && char_isspace (p[0]))
	p++;
    }
  if (p == NULL || (p < p_end && p[0] != '\0'))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
}

/*
 * db_string_to_datetime() -
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * datetime(out): a pointer to a DB_DATETIME to be modified
 */
int
db_string_to_datetime (const char *str, DB_DATETIME * datetime)
{
  return db_string_to_datetime_ex (str, strlen (str), datetime);
}

/*
 * db_string_to_datetimetz_ex() -
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * dt_tz(out): a pointer to a DB_DATETIMETZ to be modified
 * has_zone(out): true if string has valid zone information, false otherwise
 */
int
db_string_to_datetimetz_ex (const char *str, int str_len, DB_DATETIMETZ * dt_tz, bool * has_zone)
{
  int er_status = NO_ERROR;
  int str_zone_size = 0;
  const char *p, *p_end;
  const char *str_zone = NULL;
  TZ_REGION session_tz_region;

  p_end = str + str_len;
  dt_tz->tz_id = 0;
  *has_zone = false;

  tz_get_session_tz_region (&session_tz_region);

  p = parse_datetime (str, str_len, &dt_tz->datetime);
  if (p == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  while (p < p_end && char_isspace (p[0]))
    {
      p++;
    }

  if (p < p_end)
    {
      *has_zone = true;
      str_zone = p;
      str_zone_size = CAST_BUFLEN (p_end - str_zone);
    }

  er_status = tz_create_datetimetz (&dt_tz->datetime, str_zone, str_zone_size, &session_tz_region, dt_tz, &p);
  if (er_status != NO_ERROR || str_zone == NULL)
    {
      /* error or no timezone in user string (no trailing chars to check) */
      return er_status;
    }

  if (p != NULL)
    {
      while (p < p_end && char_isspace (p[0]))
	{
	  p++;
	}
    }

  if (p == NULL || (p < p_end && p[0] != '\0'))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return er_status;
}

/*
 * db_string_to_datetimetz() -
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * dt_tz(out): a pointer to a DB_DATETIMETZ to be modified
 * has_zone(out): true if string has valid zone information, false otherwise
 */
int
db_string_to_datetimetz (const char *str, DB_DATETIMETZ * dt_tz, bool * has_zone)
{
  return db_string_to_datetimetz_ex (str, strlen (str), dt_tz, has_zone);
}

/*
 * db_string_to_datetimeltz_ex() -
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * str_len(in): the length of the string to be parsed
 * datetime(out): a pointer to a DB_DATETIME to be modified
 */
int
db_string_to_datetimeltz_ex (const char *str, int str_len, DB_DATETIME * datetime)
{
  int error = NO_ERROR;
  DB_DATETIMETZ dt_tz;
  bool dummy_has_zone;

  error = db_string_to_datetimetz_ex (str, str_len, &dt_tz, &dummy_has_zone);
  if (error == NO_ERROR)
    {
      *datetime = dt_tz.datetime;
    }

  return error;
}

/*
 * db_string_to_datetimeltz() -
 * return : 0 on success, -1 on error.
 * str(in): a buffer containing a date to be parsed
 * datetime(out): a pointer to a DB_DATETIME to be modified
 */
int
db_string_to_datetimeltz (const char *str, DB_DATETIME * datetime)
{
  return db_string_to_datetimeltz_ex (str, strlen (str), datetime);
}

/*
 * db_datetime_decode() - Converts encoded datetime into
 *                        month/day/year/hour/minute/second/millisecond values.
 * return : error code
 * datetime(in): pointer to DB_DATETIME
 * month(out): pointer to month
 * day(out): pointer to day
 * year(out): pointer to year
 * hour(out): pointer to hour
 * minute(out): pointer to minute
 * second(out): pointer to second
 * millisecond(out): pointer to millisecond
 */
int
db_datetime_decode (const DB_DATETIME * datetime, int *month, int *day, int *year, int *hour, int *minute, int *second,
		    int *millisecond)
{
  db_date_decode (&datetime->date, month, day, year);
  decode_mtime (datetime->time, hour, minute, second, millisecond);

  return NO_ERROR;
}

/*
 * db_datetime_encode() - Converts month/day/year/hour/minute/second/millisecond
 *                        into an encoded relative
 * return : error code
 * datetime(in): pointer to DB_DATETIME
 * month(out): month
 * day(out): day
 * year(out): year
 * hour(out): hour
 * minute(out): minute
 * second(out): second
 * millisecond(out): millisecond
 */
int
db_datetime_encode (DB_DATETIME * datetime, int month, int day, int year, int hour, int minute, int second,
		    int millisecond)
{
  datetime->time = encode_mtime (hour, minute, second, millisecond);
  return db_date_encode (&datetime->date, month, day, year);
}

/*
 * db_subtract_int_from_datetime() -
 * return : error code
 * datetime(in):
 * i2(in):
 * result_datetime(out):
 */
int
db_subtract_int_from_datetime (DB_DATETIME * dt1, DB_BIGINT bi2, DB_DATETIME * result_datetime)
{
  DB_BIGINT bi1, result_bi, tmp_bi;

  if (bi2 < 0)
    {
      if (bi2 == DB_BIGINT_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
	  return ER_QPROC_TIME_UNDERFLOW;
	}

      return db_add_int_to_datetime (dt1, -bi2, result_datetime);
    }

  bi1 = ((DB_BIGINT) dt1->date) * MILLISECONDS_OF_ONE_DAY + dt1->time;

  result_bi = bi1 - bi2;
  if (OR_CHECK_SUB_UNDERFLOW (bi1, bi2, result_bi))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }

  tmp_bi = (DB_BIGINT) (result_bi / MILLISECONDS_OF_ONE_DAY);
  if (OR_CHECK_INT_OVERFLOW (tmp_bi) || tmp_bi > DB_DATE_MAX || tmp_bi < DB_DATE_MIN)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }
  result_datetime->date = (int) tmp_bi;
  result_datetime->time = (int) (result_bi % MILLISECONDS_OF_ONE_DAY);

  return NO_ERROR;
}

/*
 * db_add_int_to_datetime() -
 * return : error code
 * datetime(in):
 * i2(in):
 * result_datetime(out):
 */
int
db_add_int_to_datetime (DB_DATETIME * datetime, DB_BIGINT bi2, DB_DATETIME * result_datetime)
{
  DB_BIGINT bi1, result_bi, tmp_bi;

  if (bi2 < 0)
    {
      if (bi2 == DB_BIGINT_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
	  return ER_QPROC_TIME_UNDERFLOW;
	}

      return db_subtract_int_from_datetime (datetime, -bi2, result_datetime);
    }

  bi1 = ((DB_BIGINT) datetime->date) * MILLISECONDS_OF_ONE_DAY + datetime->time;

  result_bi = bi1 + bi2;
  if (OR_CHECK_ADD_OVERFLOW (bi1, bi2, result_bi))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }

  tmp_bi = (DB_BIGINT) (result_bi / MILLISECONDS_OF_ONE_DAY);
  if (OR_CHECK_INT_OVERFLOW (tmp_bi) || tmp_bi > DB_DATE_MAX || tmp_bi < DB_DATE_MIN)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }

  result_datetime->date = (int) tmp_bi;
  result_datetime->time = (int) (result_bi % MILLISECONDS_OF_ONE_DAY);

  return NO_ERROR;
}

/*
 *  db_get_day_of_year() - returns the day of the year (1 to 365(6))
 */
int
db_get_day_of_year (int year, int month, int day)
{
  int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30 };
  int day_of_year = 0;
  int i;

  for (i = 1; i < month; i++)
    {
      day_of_year += days[i - 1];
    }

  day_of_year += day;

  /* for leap years, add one extra day if we're past February. A leap year i a year that is divisible by 4 and if it is
   * divisible 100 it is also divisible by 400 */
  if (month > 2 && IS_LEAP_YEAR (year))
    {
      day_of_year++;
    }
  return day_of_year;
}

/*
 *  db_get_day_of_week() - returns the day of week (0 = Sunday, 6 = Saturday)
 */
int
db_get_day_of_week (int year, int month, int day)
{
  if (year == 0 && month == 0 && day == 0)
    {
      return 0;
    }

  if (month < 3)
    {
      month = month + 12;
      year = year - 1;
    }

  return (day + (2 * month) + (int) (6 * (month + 1) / 10) + year + (int) (year / 4) - (int) (year / 100) +
	  (int) (year / 400) + 1) % 7;
}

/*
 *  get_end_of_week_one_of_year() - returns the day number (1-15) at which
 *                                  week 1 in the year ends
 *    year(in)	: year
 *    mode	: specifies the way in which to consider what "week 1" means
 *
 *	    mode  First day   Range Week 1 is the first week
 *		  of week
 *	    0	  Sunday      0-53  with a Sunday in this year
 *	    1	  Monday      0-53  with more than 3 days this year
 *	    2	  Sunday      1-53  with a Sunday in this year
 *	    3	  Monday      1-53  with more than 3 days this year
 *	    4	  Sunday      0-53  with more than 3 days this year
 *	    5	  Monday      0-53  with a Monday in this year
 *	    6	  Sunday      1-53  with more than 3 days this year
 *	    7	  Monday      1-53  with a Monday in this year
 */
static int
get_end_of_week_one_of_year (int year, int mode)
{
  int dow_1Jan = 1 + db_get_day_of_week (year, 1, 1);

  switch (mode)
    {
    case 0:
    case 2:
      if (dow_1Jan == 1)
	{
	  return 7;
	}
      else
	{
	  return 7 + 8 - dow_1Jan;
	}
    case 1:
    case 3:
      if (dow_1Jan == 1)
	{
	  return 7 + 1;
	}
      else if (dow_1Jan > 5)
	{
	  return 16 - dow_1Jan;
	}
      else
	{
	  return 9 - dow_1Jan;
	}
    case 4:
      if (dow_1Jan > 4)
	{
	  return 7 + 8 - dow_1Jan;
	}
      else
	{
	  return 8 - dow_1Jan;
	}
    case 5:
      if (dow_1Jan == 1)
	{
	  return 7 + 1;
	}
      else if (dow_1Jan == 2)
	{
	  return 7;
	}
      else
	{
	  return 16 - dow_1Jan;
	}
    case 6:
      if (dow_1Jan > 4)
	{
	  return 7 + 8 - dow_1Jan;
	}
      else
	{
	  return 8 - dow_1Jan;
	}
    case 7:
      if (dow_1Jan == 2)
	{
	  return 7;
	}
      else
	{
	  return 16 - dow_1Jan;
	}
    }

  assert (false);

  return 7;
}

/*
 *  db_get_week_of_year() - returns the week number
 *    year(in)	: year
 *    month(in)	: month
 *    day(in)	: day of month
 *    mode	: specifies the way in which to compute the week number:
 *
 *	    mode  First day   Range Week 1 is the first week
 *		  of week
 *	    0	  Sunday      0-53  with a Sunday in this year
 *	    1	  Monday      0-53  with more than 3 days this year
 *	    2	  Sunday      1-53  with a Sunday in this year
 *	    3	  Monday      1-53  with more than 3 days this year
 *	    4	  Sunday      0-53  with more than 3 days this year
 *	    5	  Monday      0-53  with a Monday in this year
 *	    6	  Sunday      1-53  with more than 3 days this year
 *	    7	  Monday      1-53  with a Monday in this year
 */
int
db_get_week_of_year (int year, int month, int day, int mode)
{
  const int day_of_year = db_get_day_of_year (year, month, day);
  const int end_of_first_week = get_end_of_week_one_of_year (year, mode);
  int week_number = 0;
  int last_week_of_year = 0;
  int days_last_year = 0;

  assert ((0 <= mode) && (mode <= 7));

  if (day_of_year > end_of_first_week)
    {
      /* if it isn't in the first week, compute week number */
      week_number = 1 + (day_of_year - end_of_first_week) / 7;
      if ((day_of_year - end_of_first_week) % 7)
	{
	  week_number++;
	}
      return week_number;
    }

  if (day_of_year > end_of_first_week - 7)
    {
      /* it is in the first week */
      return 1;
    }

  /* day_of_year is in the last week of the previous year */
  if (mode == 0 || mode == 1 || mode == 4 || mode == 5)
    {
      /* return 0 for those modes */
      return 0;
    }

  last_week_of_year = get_end_of_week_one_of_year (year - 1, mode);

  days_last_year = 365;
  /* if it is a leap year */
  if (IS_LEAP_YEAR (year - 1))
    {
      days_last_year++;
    }

  week_number = 1 + (days_last_year - last_week_of_year) / 7;
  if ((days_last_year - last_week_of_year) % 7 != 0)
    {
      week_number++;
    }
  return week_number;
}

/* db_check_time_date_format()
  returns:
    1 if it has only time specifiers,
    2 if it has only date specifiers,
    3 if it has them both
    4 if it has both time and timezone specifiers
    5 if it has time, date and timezone specifiers
  */
int
db_check_time_date_format (const char *format_s)
{
  int i, res = 0, len;
  int format_type[256];
  bool has_timezone = false;

  len = strlen (format_s);
  memset (format_type, 0, sizeof (format_type));

  /* time */
  format_type['f'] = TIME_SPECIFIER;
  format_type['H'] = TIME_SPECIFIER;
  format_type['h'] = TIME_SPECIFIER;
  format_type['I'] = TIME_SPECIFIER;
  format_type['i'] = TIME_SPECIFIER;
  format_type['k'] = TIME_SPECIFIER;
  format_type['l'] = TIME_SPECIFIER;
  format_type['p'] = TIME_SPECIFIER;
  format_type['r'] = TIME_SPECIFIER;
  format_type['S'] = TIME_SPECIFIER;
  format_type['s'] = TIME_SPECIFIER;
  format_type['T'] = TIME_SPECIFIER;

  /* date */
  format_type['a'] = DATE_SPECIFIER;
  format_type['b'] = DATE_SPECIFIER;
  format_type['c'] = DATE_SPECIFIER;
  format_type['D'] = DATE_SPECIFIER;
  format_type['d'] = DATE_SPECIFIER;
  format_type['e'] = DATE_SPECIFIER;
  format_type['j'] = DATE_SPECIFIER;
  format_type['M'] = DATE_SPECIFIER;
  format_type['m'] = DATE_SPECIFIER;
  format_type['U'] = DATE_SPECIFIER;
  format_type['u'] = DATE_SPECIFIER;
  format_type['V'] = DATE_SPECIFIER;
  format_type['v'] = DATE_SPECIFIER;
  format_type['W'] = DATE_SPECIFIER;
  format_type['w'] = DATE_SPECIFIER;
  format_type['X'] = DATE_SPECIFIER;
  format_type['x'] = DATE_SPECIFIER;
  format_type['Y'] = DATE_SPECIFIER;
  format_type['y'] = DATE_SPECIFIER;

  for (i = 1; i < len; i++)
    {
      if (format_s[i - 1] != '%')	/* %x */
	continue;
      if (i > 1 && format_s[i - 2] == '%')	/* but not %%x */
	continue;

      if (format_type[(unsigned char) format_s[i]] != 0)
	{
	  res |= format_type[(unsigned char) format_s[i]];
	}
      if (i + 2 < len && format_s[i] == 'T' && format_s[i + 1] == 'Z')
	{
	  switch (format_s[i + 2])
	    {
	    case 'R':
	    case 'D':
	    case 'H':
	    case 'M':
	      has_timezone = true;
	      break;
	    default:
	      break;
	    }
	}
    }

  if (has_timezone)
    {
      if (res == DATE_SPECIFIER || res == DATETIME_SPECIFIER)
	{
	  res = DATETIMETZ_SPECIFIER;
	}
    }

  return res;
}


/*
 * db_add_weeks_and_days_to_date () - add weeks and days to a gived date
 *   return: ER_FAILED error, NO_ERROR ok
 *   day(in,out) : day of date
 *   month(in,out) : month of date
 *   year(in,out) : year of date
 *   weeks(in) : how many weeks will be added to date
 *   day_weeks : how many days will be added to date
 * Note :
 *   day, month and year will be updated just in case of no error
 */
int
db_add_weeks_and_days_to_date (int *day, int *month, int *year, int weeks, int day_week)
{
  int days_months[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int d, m, y, i;

  if (day == NULL || month == NULL || year == NULL)
    {
      return ER_FAILED;
    }

  if (*year < 0 || *year > 9999)
    {
      return ER_FAILED;
    }

  if (*month < 1 || *month > 12)
    {
      return ER_FAILED;
    }

  if (IS_LEAP_YEAR (*year))
    {
      days_months[2] += 1;
    }

  if (*day < 0 || *day > days_months[*month])
    {
      return ER_FAILED;
    }

  if (weeks < 0)
    {
      return ER_FAILED;
    }

  if (day_week < 0 || day_week > 6)
    {
      return ER_FAILED;
    }

  d = *day;
  m = *month;
  y = *year;
  for (i = 1; i <= weeks; i++)
    {
      d = d + 7;
      if (d > days_months[m])
	{
	  d = d - days_months[m];
	  m = m + 1;

	  if (m > 12)
	    {
	      m = 1;
	      y = y + 1;
	      if ((y % 400 == 0) || (y % 100 != 0 && y % 4 == 0))
		{
		  days_months[2] = 29;
		}
	      else
		{
		  days_months[2] = 28;
		}
	    }
	}
    }

  d = d + day_week;
  if (d > days_months[m])
    {
      d = d - days_months[m];
      m = m + 1;
      if (m > 12)
	{
	  m = 1;
	  y = y + 1;
	}
    }

  if (y < 0 || y > 9999)
    {
      return ER_FAILED;
    }

  *day = d;
  *month = m;
  *year = y;

  return NO_ERROR;
}
