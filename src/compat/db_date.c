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
 * db_date.c - Julian date conversion routines and added functions for
 *             handling relative time values.
 */

#ident "$Id$"

#include <stdio.h>
#include <math.h>
#include <time.h>
#include "chartype.h"
#include "misc_string.h"
#include "error_manager.h"
#include "dbtype.h"
#include "db_date.h"
#include "dbi.h"
#include "system_parameter.h"
#include "intl_support.h"
#include "query_opfunc.h"

/* used in conversion to julian */
#define IGREG1     (15 + 31L * (10 + 12L * 1582))

/* used in conversion from julian */
#define IGREG2     2299161
#define FLOOR(d1) floor(d1)

#define YBIAS   1900

#define MMDDYYYY        0
#define YYYYMMDD        1

#if defined(WINDOWS)
#define snprintf _snprintf
#endif

typedef struct ampm_buf
{
  char str[10];
  int len;
} AMPM_BUF;

static void decode_time (int timeval, int *hourp, int *minutep, int *secondp);
static int encode_time (int hour, int minute, int second);
static void decode_mtime (int mtimeval, int *hourp, int *minutep,
			  int *secondp, int *millisecondp);
static unsigned int encode_mtime (int hour, int minute, int second,
				  int millisecond);
static int init_tm (struct tm *);
static int get_current_year (void);
static const char *parse_date (const char *buf, DB_DATE * date);
static const char *parse_time (const char *buf, DB_TIME * mtime);
static const char *parse_mtime (const char *buf, unsigned int *mtime);
static const char *parse_timestamp (const char *buf, DB_TIMESTAMP * utime);
static const char *parse_datetime (const char *buf, DB_DATETIME * datetime);

/*
 * julian_encode() - Generic routine for calculating a julian date given
 *    seperate month/day/year values.
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
   * Test whether to convert to gregorian calander, started Oct 15, 1982
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
      *weekp = (int) ((jul + 1) % 7);
    }
}

/*
 * DB_DATE FUNCTIONS
 */

/*
 * db_date_encode() -
 * return : void
 * date(out):
 * month(in): month (1 - 12)
 * day(in): day (1 - 31)
 * year(in):
 */
void
db_date_encode (DB_DATE * date, int month, int day, int year)
{
  DB_DATE tmp;
  int tmp_month, tmp_day, tmp_year;

  if (month < 0 || month > 12 || day < 0 || day > 31)
    {
      *date = 0;
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
      *date = (month == tmp_month && day == tmp_day && year == tmp_year)
	? tmp : 0;
    }
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
 * return : void
 * timeval(out) : time value
 * hour(in): hour
 * minute(in): minute
 * second(in): second
 */
void
db_time_encode (DB_TIME * timeval, int hour, int minute, int second)
{
  if (timeval != NULL)
    {
      if (hour >= 0 && minute >= 0 && second >= 0 &&
	  hour < 24 && minute < 60 && second < 60)
	{
	  *timeval = (((hour * 60) + minute) * 60) + second;
	}
      else
	{
	  *timeval = -1;
	}
    }
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
 * db_tm_encode() - This funciton is used in conjunction with Unix mktime to
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
  int mon, day, year, hour, min, sec;
  struct tm loc;

  if (c_time_struct == NULL
      || ((date == NULL) && (timeval == NULL))
      || init_tm (c_time_struct) == -1)
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
  if (mktime (&loc) < (time_t) 0)	/* get correct tm_isdst */
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }
  c_time_struct->tm_isdst = loc.tm_isdst;
  if (mktime (c_time_struct) < (time_t) 0)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return (NO_ERROR);
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
 */
time_t
db_mktime (DB_DATE * date, DB_TIME * timeval)
{
  time_t retval;
  struct tm temp;

  if (db_tm_encode (&temp, date, timeval) != NO_ERROR)
    {
      return (time_t) - 1;
    }

  retval = (mktime (&temp));

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
  time_t tmp_utime;

  tmp_utime = db_mktime (date, timeval);
  if (tmp_utime < 0 || OR_CHECK_INT_OVERFLOW (tmp_utime))
    {
      /* mktime couldn't handle this date & time */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }
  else
    {
      *utime = (DB_TIMESTAMP) tmp_utime;
    }

  return NO_ERROR;
}

/*
 * db_timestamp_decode() - This function converts a DB_TIMESTAMP into
 *    a DB_DATE and DB_TIME pair.
 * return : void
 * time(in): universal time
 * date(out): return julian date
 * time(out): return relative time
 */
void
db_timestamp_decode (const DB_TIMESTAMP * utime, DB_DATE * date,
		     DB_TIME * timeval)
{
  struct tm *temp;
  time_t tmp_time = *utime;

#if defined(SERVER_MODE) && !defined(WINDOWS)
  struct tm t;

  temp = localtime_r (&tmp_time, &t);
#else
  temp = localtime (&tmp_time);
#endif
  if (temp)
    {
      if (date != NULL)
	{
	  *date = julian_encode (temp->tm_mon + 1, temp->tm_mday,
				 temp->tm_year + 1900);
	}
      if (timeval != NULL)
	{
	  *timeval = encode_time (temp->tm_hour, temp->tm_min, temp->tm_sec);
	}
    }
  else
    {
      /* error condition */
      if (date)
	{
	  *date = 0;
	}
      if (timeval)
	{
	  *timeval = 0;
	}
    }
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
db_strftime (char *s, int smax, const char *fmt, DB_DATE * date,
	     DB_TIME * timeval)
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

#if defined(SERVER_MODE) && !defined(WINDOWS)
  struct tm t;

  temp = localtime_r (epoch_time, &t);
#else
  temp = localtime (epoch_time);
#endif

  if (temp == NULL)
    {
      return;
    }

  if (date != NULL)
    {
      *date = julian_encode (temp->tm_mon + 1, temp->tm_mday,
			     temp->tm_year + 1900);
    }
  if (timeval != NULL)
    {
      *timeval = encode_time (temp->tm_hour, temp->tm_min, temp->tm_sec);
    }
}


/*
 * db_localtime() - Converts the time since epoch (jan 1 1970) into database
 *    datetime structure.
 * return : void
 * epoch_time(in): number of seconds since epoch
 * datetime(out): database datetime structure
 */
void
db_localdatetime (time_t * epoch_time, DB_DATETIME * datetime)
{
  struct tm *temp;

#if defined(SERVER_MODE) && !defined(WINDOWS)
  struct tm t;

  temp = localtime_r (epoch_time, &t);
#else
  temp = localtime (epoch_time);
#endif

  if (temp == NULL)
    {
      return;
    }

  if (datetime != NULL)
    {
      db_datetime_encode (datetime, temp->tm_mon + 1, temp->tm_mday,
			  temp->tm_year + 1900, temp->tm_hour, temp->tm_min,
			  temp->tm_sec, 0);
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
#if defined(SERVER_MODE) && !defined(WINDOWS)
  struct tm t;
#endif

  if (time (&tloc) == -1)
    {
      return -1;
    }

#if defined(SERVER_MODE) && !defined(WINDOWS)
  tmp = localtime_r (&tloc, &t);
#else
  tmp = localtime (&tloc);
#endif
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
 * date(out): a pointer to a DB_DATE to be modified
 */
static const char *
parse_date (const char *buf, DB_DATE * date)
{
  int part[3] = { 0, 0, 0 };
  int month, day, year;
  int us_style = 1;
  int julian_date;
  unsigned int i;
  int c;
  const char *p;
  int date_style = -1;
  int year_part, month_part, day_part;

  for (i = 0, p = buf; i < DIM (part); i++)
    {
      for (c = *p; char_isspace (c); c = *++p)
	;
      for (c = *p; char_isdigit (c); c = *++p)
	part[i] = part[i] * 10 + (c - '0');
      if (i < DIM (part) - 1)
	{
	  for (c = *p; char_isspace (c); c = *++p)
	    ;
	  if (c == '/')
	    {
	      ++p;
	      date_style = MMDDYYYY;
	    }
	  else if (c == '-')
	    {
	      ++p;
	      date_style = YYYYMMDD;
	    }
	  else
	    {
	      break;
	    }
	}
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
      month_part = us_style ? 0 : 1;
      day_part = us_style ? 1 : 0;
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

  if (0 <= year_part && 10000 <= part[year_part])
    {
      return NULL;
    }

  year = (year_part == -1) ? get_current_year () : part[year_part];
  month = part[month_part];
  day = part[day_part];

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
 * time(out): pointer to DB_TIME to be updated with the parsed time
 */
static const char *
parse_time (const char *buf, DB_TIME * time)
{
  unsigned int mtime;
  const char *p;

  p = parse_mtime (buf, &mtime);

  if (p != NULL)
    {
      *time = mtime / 1000;
    }

  return p;
}

/*
 * parse_mtime() -
 * return : const char or NULL on error
 * buf(in): pointer to time expression
 * mtime(out): pointer to unsigned int to be updated with the parsed time
 */
static const char *
parse_mtime (const char *buf, unsigned int *mtime)
{
  static AMPM_BUF ampm[2];
  static int initialized = 0;

  int part[4] = { 0, 0, 0, 0 };
  unsigned int i;
  int c;
  const char *p;
  double fraction = 100;

  if (!initialized)
    {
      struct tm tm;
      initialized = 1;
      if (init_tm (&tm) == -1)
	{
	  strcpy (ampm[0].str, "am");
	  strcpy (ampm[1].str, "pm");
	}
      else
	{
	  /*
	   * Use strftime() to try to find out this locale's idea of
	   * the am/pm designators.
	   */
	  tm.tm_hour = 0;
	  strftime (ampm[0].str, sizeof (ampm[0].str), "%p", &tm);
	  tm.tm_hour = 12;
	  strftime (ampm[1].str, sizeof (ampm[1].str), "%p", &tm);
	}
      ampm[0].len = strlen (ampm[0].str);
      ampm[1].len = strlen (ampm[1].str);
    }

  for (i = 0, p = buf; i < DIM (part); i++)
    {
      for (c = *p; char_isspace (c); c = *++p)
	;
      for (c = *p; char_isdigit (c); c = *++p)
	{
	  if (i != 3)
	    {
	      part[i] = part[i] * 10 + (c - '0');
	    }
	  else
	    {
	      part[i] += (int) ((c - '0') * fraction + 0.5);
	      fraction /= 10;
	    }
	}
      if (i < DIM (part) - 1)
	{
	  for (c = *p; char_isspace (c); c = *++p)
	    ;
	  if (c == ':')
	    {
	      ++p;
	    }
	  else if (c == '.' && i == 2)
	    {
	      ++p;
	    }
	  else
	    {
	      break;
	    }
	}
    }

  for (c = *p; char_isspace (c); c = *++p)
    ;
  if (intl_mbs_ncasecmp (p, ampm[0].str, ampm[0].len) == 0)
    {
      p += ampm[0].len;
      if (part[0] == 12)
	{
	  part[0] = 0;
	}
      else if (part[0] > 12)
	{
	  part[0] = -1;
	}
    }
  else if (intl_mbs_ncasecmp (p, ampm[1].str, ampm[1].len) == 0)
    {
      p += ampm[1].len;
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
 * parse_timestamp() - Tries to parse a utime by finding a date and a time, in
 *              order.
 * Returns: const char or NULL on error
 * buf(in): pointer to a date-time expression
 * utime(out): pointer to a DB_TIMESTAMP to be modified
 */
static const char *
parse_timestamp (const char *buf, DB_TIMESTAMP * utime)
{
  DB_DATE date;
  DB_TIME time;
  DB_TIMESTAMP tmp_utime;
  int error = NO_ERROR;
  const char *p;

  /* First try to parse a date followed by a time. */
  p = parse_date (buf, &date);
  if (p)
    {
      p = parse_time (p, &time);
      if (p)
	{
	  goto finalcheck;
	}
    }

  /* If that fails, try to parse a time followed by a date. */
  p = parse_time (buf, &time);
  if (p)
    {
      p = parse_date (p, &date);
      if (p)
	{
	  goto finalcheck;
	}
    }

  return NULL;

finalcheck:
  /*
   * Since parse_date() willingly accepts dates that won't fit into a
   * utime, we have to do one final check here before we accept the
   * utime.
   */
  error = db_timestamp_encode (&tmp_utime, &date, &time);
  if (error == NO_ERROR)
    {
      *utime = tmp_utime;
      return p;
    }
  else
    {
      return NULL;
    }
}

/*
 * parse_datetime() -
 * Returns: const char or NULL on error
 * buf(in): pointer to a date-time expression
 * datetime(out): pointer to a DB_DATETIME to be modified
 */
static const char *
parse_datetime (const char *buf, DB_DATETIME * datetime)
{
  DB_DATE date = 0;
  unsigned int mtime;
  int error = NO_ERROR;
  const char *p;

  /* First try to parse a date followed by a time. */
  p = parse_date (buf, &date);
  if (p)
    {
      p = parse_mtime (p, &mtime);
      if (p)
	{
	  goto finalcheck;
	}
    }

  p = parse_mtime (buf, &mtime);
  if (p)
    {
      p = parse_date (p, &date);
      if (p)
	{
	  goto finalcheck;
	}
    }

  return NULL;

finalcheck:

  if (date == 0)
    {
      return NULL;
    }

  datetime->date = date;
  datetime->time = mtime;

  return p;
}

/*
 * db_string_to_date() - Parse an ordinary date string (e.g., '10/15/86').
 *    Whitespace is not permitted between slashed components. If the year is
 *    omitted, the current year is assumed.  Dates are currently accepted
 *    only in the slashified US style.
 * returns: 0 on success, ER_DATE_CONVERSION on error
 * buf(in): a buffer containing a date to be parsed
 * date(out): a pointer to a DB_DATE to be modified
 */
int
db_string_to_date (const char *str, DB_DATE * date)
{
  const char *p;

  p = parse_date (str, date);
  if (p)
    {
      while (char_isspace (p[0]))
	{
	  p++;
	}
    }
  if (p == NULL || p[0] != '\0')
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
 * buf(in): a buffer containing a date to be parsed
 * time(out): a pointer to a DB_TIME to be modified
 */
int
db_string_to_time (const char *str, DB_TIME * time)
{
  const char *p;

  p = parse_time (str, time);
  if (p)
    {
      while (char_isspace (p[0]))
	p++;
    }
  if (p == NULL || p[0] != '\0')
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
}

/*
 * db_string_to_timestamp() - Parse a date and time string into a utime.
 *    The time and date are parsed according to the same rules as
 *    db_string_to_time() and db_string_to_date().
 *    they may appear in either order.
 * return : 0 on success, -1 on error.
 * buf(in): a buffer containing a date to be parsed
 * utime(out): a pointer to a DB_TIMESTAMP to be modified
 */
int
db_string_to_timestamp (const char *str, DB_TIMESTAMP * utime)
{
  const char *p;

  p = parse_timestamp (str, utime);
  if (p)
    {
      while (char_isspace (p[0]))
	p++;
    }
  if (p == NULL || p[0] != '\0')
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
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
  int retval;

  if (buf == NULL || bufsize == 0)
    {
      return 0;
    }

  db_date_decode (date, &mon, &day, &year);
  retval = snprintf (buf, bufsize, "%02d/%02d/%04d", mon, day, year);
  if (bufsize < retval)
    {
      retval = 0;
    }

  return retval;
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
  int retval;

  if (buf == NULL || bufsize == 0)
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
  retval = snprintf (buf, bufsize, "%02d:%02d:%02d %s",
		     hour, min, sec, (pm) ? "PM" : "AM");

  if (bufsize < retval)
    {
      retval = 0;
    }

  return retval;
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

  db_timestamp_decode (utime, &date, &time);
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
 * db_timestamp_to_datetime() -
 * return : error code
 * utime(in): timestamp
 * datetime(out): datetime
 */
int
db_timestamp_to_datetime (DB_TIMESTAMP * utime, DB_DATETIME * datetime)
{
  struct tm *temp;
  time_t tmp_time = *utime;

#if defined(SERVER_MODE) && !defined(WINDOWS)
  struct tm t;

  temp = localtime_r (&tmp_time, &t);
#else
  temp = localtime (&tmp_time);
#endif

  if (temp)
    {
      if (datetime)
	{
	  db_datetime_encode (datetime, temp->tm_mon + 1, temp->tm_mday,
			      temp->tm_year + 1900, temp->tm_hour,
			      temp->tm_min, temp->tm_sec, 0);
	}
    }
  else
    {
      /* error condition */
      if (datetime)
	{
	  datetime->date = 0;
	  datetime->time = 0;
	}
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
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
decode_mtime (int mtimeval, int *hourp, int *minutep,
	      int *secondp, int *millisecondp)
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
  int retval;
  bool pm;

  if (buf == NULL || bufsize == 0)
    {
      return 0;
    }

  db_datetime_decode (datetime, &mon, &day, &year,
		      &hour, &minute, &second, &millisecond);
  pm = (hour >= 12) ? true : false;
  if (hour == 0)
    {
      hour = 12;
    }
  else if (hour > 12)
    {
      hour -= 12;
    }
  retval = snprintf (buf, bufsize, "%02d:%02d:%02d.%03d %s %02d/%02d/%04d ",
		     hour, minute, second, millisecond, pm ? "PM" : "AM",
		     mon, day, year);
  if (bufsize < retval)
    {
      retval = 0;
    }

  return retval;
}

/*
 * db_string_to_datetime() -
 * return : 0 on success, -1 on error.
 * buf(in): a buffer containing a date to be parsed
 * datetime(out): a pointer to a DB_DATETIME to be modified
 */
int
db_string_to_datetime (const char *str, DB_DATETIME * datetime)
{
  const char *p;

  p = parse_datetime (str, datetime);
  if (p)
    {
      while (char_isspace (p[0]))
	p++;
    }
  if (p == NULL || p[0] != '\0')
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  return NO_ERROR;
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
db_datetime_decode (const DB_DATETIME * datetime, int *month, int *day,
		    int *year, int *hour, int *minute, int *second,
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
db_datetime_encode (DB_DATETIME * datetime, int month, int day, int year,
		    int hour, int minute, int second, int millisecond)
{
  db_date_encode (&datetime->date, month, day, year);
  datetime->time = encode_mtime (hour, minute, second, millisecond);

  return NO_ERROR;
}

/*
 * db_subtract_int_from_datetime() -
 * return : error code
 * datetime(in):
 * i2(in):
 * result_datetime(out):
 */
int
db_subtract_int_from_datetime (DB_DATETIME * dt1, DB_BIGINT bi2,
			       DB_DATETIME * result_datetime)
{
  DB_BIGINT bi1, result_bi, tmp_bi;

  if (bi2 < 0)
    {
      if (bi2 == DB_BIGINT_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_TIME_UNDERFLOW, 0);
	  return ER_QPROC_TIME_UNDERFLOW;
	}

      db_add_int_to_datetime (dt1, -bi2, result_datetime);
    }

  bi1 = ((DB_BIGINT) dt1->date) * MILLISECONDS_OF_ONE_DAY + dt1->time;

  result_bi = bi1 - bi2;
  if (OR_CHECK_SUB_UNDERFLOW (bi1, bi2, result_bi))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }

  tmp_bi = (DB_BIGINT) (result_bi / MILLISECONDS_OF_ONE_DAY);
  if (OR_CHECK_INT_OVERFLOW (tmp_bi))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }
  result_datetime->date = (int) tmp_bi;
  result_datetime->time = (int) (result_bi % MILLISECONDS_OF_ONE_DAY);

  return NO_ERROR;
}

/*
 * db_add_datetime_to_int() -
 * return : error code
 * datetime(in):
 * i2(in):
 * result_datetime(out):
 */
int
db_add_int_to_datetime (DB_DATETIME * datetime, DB_BIGINT bi2,
			DB_DATETIME * result_datetime)
{
  DB_BIGINT bi1, result_bi, tmp_bi;

  if (bi2 < 0)
    {
      if (bi2 == DB_BIGINT_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_TIME_UNDERFLOW, 0);
	  return ER_QPROC_TIME_UNDERFLOW;
	}

      return db_subtract_int_from_datetime (datetime, -bi2, result_datetime);
    }

  bi1 = ((DB_BIGINT) datetime->date) * MILLISECONDS_OF_ONE_DAY
    + datetime->time;

  result_bi = bi1 + bi2;
  if (OR_CHECK_ADD_OVERFLOW (bi1, bi2, result_bi))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }

  tmp_bi = (DB_BIGINT) (result_bi / MILLISECONDS_OF_ONE_DAY);
  if (OR_CHECK_INT_OVERFLOW (tmp_bi))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_QPROC_TIME_UNDERFLOW;
    }

  result_datetime->date = (int) tmp_bi;
  result_datetime->time = (int) (result_bi % MILLISECONDS_OF_ONE_DAY);

  return NO_ERROR;
}
