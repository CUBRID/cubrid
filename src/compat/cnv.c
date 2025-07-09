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
 * cnv.c - String conversion functions
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <wchar.h>

#include "porting.h"
#include "adjustable_array.h"
#include "intl_support.h"
#include "memory_alloc.h"
#include "string_opfunc.h"
#include "object_domain.h"
#include "language_support.h"
#include "object_primitive.h"
#include "cnv.h"
#include "cnvlex.h"
#include "cnverr.h"
#if defined(SERVER_MODE)
#include "critical_section.h"
#endif
#include "tz_support.h"
#include "db_date.h"
#include "dbtype.h"
#if defined (SERVER_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif // SERVER_MODE
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define BITS_IN_BYTE		8
#define HEX_IN_BYTE		2
#define BITS_IN_HEX		4

/*
 * 2**3.1 ~ 10.  Thus a string with one decimal value per byte will be (8/3.1)
 * times longer than a string with 8 binary places per byte.
 * A 'decimal string' needs to be 3 times longer than a raw numeric string
 * plus a sign and a NULL termination.
 */
#define DEC_BUFFER_SIZE		(DB_NUMERIC_BUF_SIZE * 6) + 2
#define GET_MIN(a, b)		(a < b) ? a : b
#define BYTE_COUNT(bit_cnt)	(((bit_cnt)+BITS_IN_BYTE-1)/BITS_IN_BYTE)
#define BYTE_COUNT_HEX(bit_cnt)	(((bit_cnt)+BITS_IN_HEX-1)/BITS_IN_HEX)

#define CNV_ERR_BAD_BINARY_DIGIT	-1
#define CNV_ERR_BAD_HEX_DIGIT	-2
#define CNV_ERR_NO_MEMORY		-3

/* Maximum number of digits in a numeric string. */
#define FMT_MAX_DIGITS \
(fmt_max_digits?       \
   fmt_max_digits :    \
     (fmt_max_digits = (int)ceil(DBL_MAX_EXP * log10( (double) FLT_RADIX))))

/* Maximum number of CHARACTERS in a local date value string. */
#define FMT_MAX_DATE_STRING     32

/* Maximum number of CHARACTERS in a local time value string. */
#define FMT_MAX_TIME_STRING     32

/* Maximum number of CHARACTERS in a local mtime value string. */
#define FMT_MAX_MTIME_STRING     36

/* Maximum number of CHARACTERS in a local timestamp value string. */
#define FMT_MAX_TIMESTAMP_STRING \
  (FMT_MAX_DATE_STRING + FMT_MAX_TIME_STRING + 1)

/* Maximum number of CHARACTERS in a local datetime value string. */
#define FMT_MAX_DATETIME_STRING \
  (FMT_MAX_DATE_STRING + FMT_MAX_MTIME_STRING + 1)

#define mbs_eql(s1, s2)       !strcmp(s1, s2)
#define wcs_eql(ws1, ws2)     !wcscmp(ws1, ws2)

/* US Zone Definitions */
#define LOCAL_STAR        "*"
#define LOCAL_STAR_LENGTH (1)	// strlen (LOCAL_STAR)
#define LOCAL_MINUS       "-"
#define LOCAL_PLUS        "+"
#define LOCAL_SPACE       " "
#define LOCAL_SPACE_LENGTH (1)	// strlen (LOCAL_SPACE)
#define LOCAL_0           "0"
#define LOCAL_EXP_LENGTH  "E+dd"
#define LOCAL_EXP         "E"
#define LOCAL_SLASH       "/"
#define LOCAL_COLON       ":"
#define WCSCAT( buffer, wcs1, wcs2) \
  (wcscpy( buffer, wcs1), wcscpy( buffer + wcslen( wcs1), wcs2), buffer)

#define FMT_SCIENTIFIC() L"E"
#define FMT_THOUSANDS() L","
#define FMT_DECIMAL() L"."
#define FMT_Z() L"Z"
#define FMT_9() L"9"
#define FMT_X() L"X"
#define FMT_STAR() L"*"
#define FMT_PLUS() L"+"
#define FMT_CURRENCY() L"$"
#define FMT_DIGITS() L"0123456789"


#define KOREAN_EUC_YEAR_SYMBOL      "\xb3\xe2"	/* nyeon */
#define KOREAN_EUC_MONTH_SYMBOL     "\xbf\xf9"	/* wol */
#define KOREAN_EUC_DAY_SYMBOL       "\xc0\xcf"	/* il */

#if !defined (SERVER_MODE)
/* no critical section */
#define csect_enter(a, b, c) NO_ERROR
#define csect_exit(a, b)
#endif /* !defined (SERVER_MODE) */


/* Format Descriptors */
typedef enum
{
  DIGIT_Z,
  DIGIT_9,
  DIGIT_STAR
} FORMAT_DIGIT;

typedef enum
{
  CURRENCY_NONE,
  CURRENCY_FIRST,
  CURRENCY_LAST
} FORMAT_CURRENCY;

typedef struct float_format_s FLOAT_FORMAT;
struct float_format_s
{
  bool sign_required;
  bool scientific;
  bool thousands;
  bool decimal;
  int integral_digits;
  FORMAT_DIGIT integral_type;
  int fractional_digits;
  FORMAT_DIGIT fractional_type;
};

typedef struct integer_format_s INTEGER_FORMAT;
struct integer_format_s
{
  bool sign_required;
  bool thousands;
  int integral_digits;
  FORMAT_DIGIT integral_type;
  const char *pattern;
};

typedef struct monetary_format_s MONETARY_FORMAT;
struct monetary_format_s
{
  bool thousands;
  bool decimal;
  int integral_digits;
  FORMAT_DIGIT integral_type;
  int fractional_digits;
  FORMAT_DIGIT fractional_type;
  FORMAT_CURRENCY format;
  DB_CURRENCY currency;
  FMT_LEX_MODE mode;
};

typedef enum bit_string_format_e
{
  BIT_STRING_BINARY = 0,
  BIT_STRING_HEX = 1
} BIT_STRING_FORMAT;

/*
 * Utility Functions
 */
/* US Zone Functions */
static const char *us_date_string (int month, int day, int year);
static int us_date_value (int *the_month, int *the_day, int *the_year);
static const char *us_time_string (const DB_TIME * the_time);
static int us_time_value (int *the_hour, int *the_min, int *the_sec);
#if defined(ENABLE_UNUSED_FUNCTION)
static const char *us_alt_date_string (int month, int day, int year);
static int us_alt_date_value (int *the_month, int *the_day, int *the_year);
static const char *us_mtime_string (int hour, int minute, int second, int millisecond);
static int us_mtime_value (int *the_hour, int *the_min, int *the_sec, int *the_msec);
static const char *us_timestamp_string (const DB_TIMESTAMP * the_timestamp);
static int us_timestamp_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec);
static const char *us_datetime_string (const DB_DATETIME * the_datetime);
static int us_datetime_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec,
			      int *the_msec);
#endif

/* KO Zone Functions */
static const char *ko_date_string (int month, int day, int year);
static int ko_date_value (int *the_month, int *the_day, int *the_year);
static const char *ko_time_string (const DB_TIME * the_time);
static int ko_time_value (int *the_hour, int *the_min, int *the_sec);
#if defined(ENABLE_UNUSED_FUNCTION)
static const char *ko_mtime_string (int hour, int minute, int second, int millisecond);
static const char *ko_alt_date_string (int month, int day, int year);
static int ko_alt_date_value (int *the_month, int *the_day, int *the_year);
static int ko_mtime_value (int *the_hour, int *the_min, int *the_sec, int *the_msec);
static const char *ko_timestamp_string (const DB_TIMESTAMP * the_timestamp);
static int ko_timestamp_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec);
static const char *ko_datetime_string (const DB_DATETIME * the_datetime);
static int ko_datetime_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec,
			      int *the_msec);
static wchar_t ko_euc_year_wc (void);
static wchar_t ko_euc_month_wc (void);
static wchar_t ko_euc_day_wc (void);
#endif
static const char *local_am (void);
static const char *local_pm (void);

/* Utility Functions */
static int fmt_minute_value (const char *, int *);
static int fmt_second_value (const char *, int *);
#if defined (ENABLE_UNUSED_FUNCTION)
static int fmt_millisecond_value (const char *descriptor, int *the_msec);
#endif

static const char *local_am_pm_string (const DB_TIME * the_time);
static int local_am_pm_value (bool *);

static const char *local_grouping (void);
static const char *local_thousands (void);
static const char *local_decimal (void);
static const char *local_date_string (int month, int day, int year);
static int local_date_value (int *, int *, int *);
static const char *local_time_string (const DB_TIME * the_time);
static int local_time_value (int *, int *, int *);
#if defined(ENABLE_UNUSED_FUNCTION)
static const char *local_timestamp_string (const DB_TIMESTAMP * the_timestamp);
static int local_timestamp_value (int *, int *, int *, int *, int *, int *);
static const char *local_long_month_name (int month);
static const char *local_long_weekday_name (int weekday);
static const char *local_short_month_name (int month);
static const char *local_short_weekday_name (int weekday);
static const char *local_datetime_string (const DB_DATETIME * the_timestamp);
static const char *local_alt_date_string (int month, int day, int year);
static int local_alt_date_value (int *, int *, int *);
static int local_datetime_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec,
				 int *the_msec);
#endif

static const wchar_t *cnv_wcs (const char *mbs);
static ADJ_ARRAY *cnv_get_value_string_buffer (int nchars);
static int cnv_bad_char (const char *string, bool unknown);
static INTL_ZONE cnv_currency_zone (DB_CURRENCY currency);
#if defined (ENABLE_UNUSED_FUNCTION)
static ADJ_ARRAY *cnv_get_string_buffer (int nchars);
static const char *cnv_currency_symbol (DB_CURRENCY currency);
static bool cnv_valid_timestamp (DB_DATE * the_date, DB_TIME * the_time);
static int fmt_validate (const char *format, FMT_LEX_MODE mode, FMT_TOKEN_TYPE token_type, DB_TYPE data_type);
#endif /* ENABLE_UNUSED_FUNCTION */
static bool cnv_valid_currency (DB_CURRENCY currency);

static int fmt_integral_digits (FORMAT_DIGIT digit_type, int ndigits, bool sign_required, bool thousands,
				double *the_value, int *nfound);
static int fmt_integral_value (FORMAT_DIGIT type, int ndigits, bool sign_required, bool thousands, double *the_value);
static int fmt_fractional_value (FORMAT_DIGIT type, int ndigits, double *the_value);
static int fmt_fractional_digits (FORMAT_DIGIT type, int ndigits, double *the_value, int *nfound);
#if defined (ENABLE_UNUSED_FUNCTION)
static void fmt_add_thousands (ADJ_ARRAY * string, int *position);
static void fmt_drop_thousands (ADJ_ARRAY * string, int *position);
static void fmt_add_integral (ADJ_ARRAY *, int *, int, FORMAT_DIGIT, FMT_LEX_MODE);
static void fmt_drop_integral (ADJ_ARRAY * string, int *pos, int ndigits, FMT_LEX_MODE mode);
static int fmt_decimals (ADJ_ARRAY * string);
static int fmt_fraction_position (ADJ_ARRAY * string, int start);
static bool fmt_add_decimal (ADJ_ARRAY * string, int *position);
static void fmt_add_fractional (ADJ_ARRAY *, int *, int, FORMAT_DIGIT);
static void fmt_drop_fractional (ADJ_ARRAY *, int *, int);
static void fmt_add_currency (ADJ_ARRAY *, int *, MONETARY_FORMAT *);
#endif /* ENABLE_UNUSED_FUNCTION */
static void cnvutil_cleanup (void);

static const char *fmt_date_string (const DB_DATE * the_date, const char *descriptor);
#if defined (ENABLE_UNUSED_FUNCTION)
static const char *fmt_year_string (int year, const char *descriptor);
static const char *fmt_month_string (int month, const char *descriptor);
static int fmt_month_value (const char *descriptor, int *the_month);
static const char *fmt_monthday_string (int day, const char *descriptor);
static int fmt_monthday_value (const char *descriptor, int *the_day);
static const char *fmt_weekday_string (int weekday, const char *descriptor);
static int fmt_year_value (const char *descriptor, int *the_year);
static int fmt_weekday_value (const char *descriptor, int *the_day);
static DB_DATE fmt_weekday_date (int month, int day, int year, int weekday);
#endif
static int fmt_date_value (const char *descriptor, int *the_month, int *the_day, int *the_year);

static const char *fmt_time_string (const DB_TIME * the_time, const char *descriptor);
#if defined (ENABLE_UNUSED_FUNCTION)
static const char *fmt_hour_string (const DB_TIME * the_time, const char *descriptor);
static int fmt_hour_value (const char *descriptor, int *the_hour);
static const char *fmt_minute_string (const DB_TIME * the_time, const char *descriptor);
static const char *fmt_second_string (const DB_TIME * the_time, const char *descriptor);
#endif
static int fmt_time_value (const char *descriptor, int *the_hour, int *the_min, int *the_sec);
static const char *fmt_timestamp_string (const DB_TIMESTAMP * the_timestamp, const char *descriptor);
static int fmt_timestamp_value (const char *descriptor, int *the_month, int *the_day, int *the_year, int *the_hour,
				int *the_min, int *the_sec);
static const FMT_TOKEN *tfmt_new (const char *format);

static bool ffmt_valid_char (FMT_TOKEN * token);
static void ffmt_new (FLOAT_FORMAT * ffmt, const char *format);
static const char *ffmt_value (FLOAT_FORMAT * ffmt, const char *string, double *the_double);
#if defined (ENABLE_UNUSED_FUNCTION)
static int ffmt_print (FLOAT_FORMAT * ffmt, double the_double, char *string, int max_size);
static int mfmt_print (MONETARY_FORMAT * mfmt, double the_double, char *string, int max_size);
#endif
static bool mfmt_valid_char (FMT_TOKEN * token);
static void mfmt_new (MONETARY_FORMAT * mfmt, const char *format, DB_CURRENCY currency_type);
static const char *mfmt_value (MONETARY_FORMAT * mfmt, const char *string, double *the_double);

static bool ifmt_valid_char (FMT_TOKEN * token);
static void ifmt_new (INTEGER_FORMAT * ifmt, const char *format);
static const char *ifmt_value (INTEGER_FORMAT * ifmt, const char *string, int *the_integer);
static const char *bifmt_value (INTEGER_FORMAT * bifmt, const char *string, DB_BIGINT * the_bigint);
static const char *ifmt_numeric_value (INTEGER_FORMAT * ifmt, const char *string, int *the_integer);
static const char *bifmt_numeric_value (INTEGER_FORMAT * ifmt, const char *string, DB_BIGINT * the_bigint);
#if defined (ENABLE_UNUSED_FUNCTION)
static int ifmt_text_numeric (INTEGER_FORMAT * ifmt, ADJ_ARRAY * string);
static const char *ifmt_text_value (INTEGER_FORMAT * ifmt, const char *string, int *the_integer);
static const char *bifmt_text_value (INTEGER_FORMAT * ifmt, const char *string, DB_BIGINT * the_bigint);
static int ifmt_print (INTEGER_FORMAT * ifmt, DB_BIGINT the_bigint, char *string, int max_size);
static int ifmt_text_print (INTEGER_FORMAT * ifmt, DB_BIGINT the_bigint, char *string, int max_size);
static void ifmt_numeric_text (INTEGER_FORMAT * ifmt, ADJ_ARRAY * numeric_string);
static int ifmt_numeric_print (INTEGER_FORMAT * ifmt, DB_BIGINT the_bigint, char *string, int max_size);
#endif
static bool bfmt_valid_char (FMT_TOKEN * token);
static void bfmt_new (BIT_STRING_FORMAT * bfmt, const char *format);
static const char *bfmt_value (BIT_STRING_FORMAT bfmt, const char *string, DB_VALUE * the_db_bit);
static int bfmt_print (BIT_STRING_FORMAT * bfmt, const DB_VALUE * the_db_bit, char *string, int max_size);

static int num_fmt_print (FLOAT_FORMAT * ffmt, const DB_VALUE * the_numeric, char *string, int max_size);
#if defined (ENABLE_UNUSED_FUNCTION)
static const char *num_fmt_value (FLOAT_FORMAT * ffmt, const char *string, DB_VALUE * the_numeric);
static int nfmt_integral_value (FORMAT_DIGIT digit_type, int ndigits, bool sign_required, bool thousands,
				char *the_value);
static int nfmt_integral_digits (FORMAT_DIGIT digit_type, int ndigits, bool sign_required, bool thousands,
				 char *the_value, int *nfound);
static int nfmt_fractional_digits (FORMAT_DIGIT digit_type, int ndigits, char *the_value, int *nfound);
static int nfmt_fractional_value (FORMAT_DIGIT digit_type, int ndigits, char *the_value);
#endif
static int fmt_max_digits;

#if defined (SERVER_MODE)
static ADJ_ARRAY *cnv_get_thread_local_adj_buffer (int idx);
static void cnv_set_thread_local_adj_buffer (int idx, ADJ_ARRAY * buffer_p);
#endif // SERVER_MODE

/* Internal buffers.
 *
 * These were formerly declared as static within the functions that used
 * them.  They were moved outside and placed together so we know what
 * they are all so they can be freed by cnvutil_cleanup() when we don't
 * need them anymore.  This was important on the PC since we like to
 * clean up after ourselves after the database connection is shut down.
 *
 * Its not clear if each of the functions needs to have its own buffer or
 * if we can just share one of these in all places.
 */

/* used by cnv_wcs */
#if !defined(SERVER_MODE)
static ADJ_ARRAY *cnv_adj_buffer1 = NULL;

/* used by cnv_get_string_buffer */
static ADJ_ARRAY *cnv_adj_buffer2 = NULL;

/* used by cnv_get_value_string_buffer */
static ADJ_ARRAY *cnv_adj_buffer3 = NULL;
#endif

static const char *kor_weekday_names[] = {
  "(\xc0\xcf)",			/* il */
  "(\xbf\xf9)",			/* wol */
  "(\xc8\xad)",			/* hwa */
  "(\xbc\xf6)",			/* su */
  "(\xb8\xf1)",			/* mok */
  "(\xb1\xdd)",			/* geum */
  "(\xc5\xe4)"			/* to */
};

static const char *kr_short_month_names[] = {
  "1\xbf\xf9",			/* 1 wol */
  "2\xbf\xf9",			/* 2 wol */
  "3\xbf\xf9",			/* 3 wol */
  "4\xbf\xf9",			/* 4 wol */
  "5\xbf\xf9",			/* 5 wol */
  "6\xbf\xf9",			/* 6 wol */
  "7\xbf\xf9",			/* 7 wol */
  "8\xbf\xf9",			/* 8 wol */
  "9\xbf\xf9",			/* 9 wol */
  "10\xbf\xf9",			/* 10 wol */
  "11\xbf\xf9",			/* 11 wol */
  "12\xbf\xf9"			/* 12 wol */
};

static const char *kr_long_month_names[] = {
  "1\xbf\xf9",			/* 1 wol */
  "2\xbf\xf9",			/* 2 wol */
  "3\xbf\xf9",			/* 3 wol */
  "4\xbf\xf9",			/* 4 wol */
  "5\xbf\xf9",			/* 5 wol */
  "6\xbf\xf9",			/* 6 wol */
  "7\xbf\xf9",			/* 7 wol */
  "8\xbf\xf9",			/* 8 wol */
  "9\xbf\xf9",			/* 9 wol */
  "10\xbf\xf9",			/* 10 wol */
  "11\xbf\xf9",			/* 11 wol */
  "12\xbf\xf9"			/* 12 wol */
};

static const char *eng_short_month_names[] = {
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jul",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec"
};

static const char *eng_long_month_names[] = {
  "January",
  "February",
  "March",
  "April",
  "May",
  "June",
  "July",
  "August",
  "September",
  "October",
  "November",
  "December"
};

static const char *eng_short_weekday_names[] = {
  "Sun",
  "Mon",
  "Tue",
  "Wed",
  "Thu",
  "Fri",
  "Sat"
};

static const char *eng_long_weekday_names[] = {
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday"
};

/*
 * us_date_string() - Return a string representing the given date in the US
 *    date format.
 * return:
 * month(in) :
 * day(in) :
 * year(in) :
 */
static const char *
us_date_string (int month, int day, int year)
{
  static char date_string[FMT_MAX_DATE_STRING * MB_LEN_MAX + 1];

  sprintf (date_string, "%d/%d/%d", month, day, year);
  assert (strlen (date_string) < (int) sizeof (date_string));

  return date_string;
}

/*
 * us_date_value() - Scan tokens and parse a date value in the US date format.
 *   If a valid value can't be found, then return an error condition.
 *   otherwise, set the_month, the_day, and the_year to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 */
static int
us_date_value (int *the_month, int *the_day, int *the_year)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;

  do
    {
      FMT_TOKEN_TYPE type;

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY || type == FT_TIME_DIGITS_0)
	|| (*the_month = atoi (token.text)) > 12 || *the_month < 1;
      if (bad_value)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_DATE_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY || type == FT_TIME_DIGITS_0)
	|| (*the_day = atoi (token.text)) > 31 || *the_day < 1;
      if (bad_value)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_DATE_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY);
      if (bad_value)
	{
	  break;
	}

      *the_year = atoi (token.text);
      /* Year 0 doesn't exist! */
      bad_value = type == FT_TIME_DIGITS_ANY && *the_year == 0;
      if (bad_value)
	{
	  break;
	}

      if (type != FT_TIME_DIGITS_ANY)
	{
	  /* Abbreviated year: add current century. */
	  struct tm tm_val, *today;
	  time_t now = time (NULL);
	  today = localtime_r (&now, &tm_val);

	  if (today != NULL)
	    {
	      *the_year += ((today->tm_year + 1900) / 100) * 100;
	    }
	  else
	    {
	      bad_value = true;
	    }
	}

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_DATE;
      co_signal (error, CNV_ER_FMT_BAD_DATE, "x");
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * us_alt_date_string() - Return a string representing the given date in the
 *    US alternate date format.
 * return:
 * month(in) :
 * day(in) :
 * year(in) :
 */
static const char *
us_alt_date_string (int month, int day, int year)
{
  return us_date_string (month, day, year);
}

/*
 * us_alt_date_value() - Scan tokens and parse a date value in the US
 *    alternate date format. If a valid value can't be found, then
 *    return an error condition.
 *    otherwise, set the_month, the_day, and the_year to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 */
static int
us_alt_date_value (int *the_month, int *the_day, int *the_year)
{
  return us_date_value (the_month, the_day, the_year);
}
#endif

/*
 * us_time_string() - Return a string representing the given time in the US
 *    time format.
 * return:
 * the_time(in) :
 */
static const char *
us_time_string (const DB_TIME * the_time)
{
  static char time_string[FMT_MAX_TIME_STRING * MB_LEN_MAX + 1];
  int hour, min, sec;

  db_time_decode ((DB_TIME *) the_time, &hour, &min, &sec);

  sprintf (time_string, "%d:%02d:%02d %s", hour % 12 ? hour % 12 : 12, min, sec, local_am_pm_string (the_time));
  assert (strlen (time_string) < (int) sizeof (time_string));

  return time_string;
}

/*
 * us_time_value() - Scan tokens and parse a time value in the US time format.
 *    If a valid value can't be found, then return an error condition.
 *    otherwise, set the_hour, the_min, and the_sec to the value found.
 * return:
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
us_time_value (int *the_hour, int *the_min, int *the_sec)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  bool pm;

  do
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY || type == FT_TIME_DIGITS_0)
	|| (*the_hour = atoi (token.text)) > 23 || *the_hour < 0;
      if (bad_value)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      error = fmt_minute_value ("M", the_min);
      if (error)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      error = fmt_second_value ("S", the_sec);
      if (error)
	{
	  break;
	}

      /* Skip blank "pattern" character. */
      if (strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH) == 0)
	{
	  cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_LOCAL_TIME);
	}
      else
	{
	  cnv_fmt_analyze (cnv_fmt_next_token (), FL_LOCAL_TIME);
	}

      /* we used to use local_am_pm_value() here, but it wasn't flexible enough to handle 24 hour time strings (no "AM"
       * or "PM" designator). */

      type = cnv_fmt_lex (&token);

      if (type == FT_NONE)
	{
	  /* do nothing to hour, no "AM" or "PM" follows */
	  ;
	}
      else if (type == FT_AM_PM && *the_hour >= 1 && *the_hour <= 12)
	{
	  pm = token.value;
	  /* convert 12 to 0 hour before adding 12 for PM values */
	  *the_hour %= 12;
	  if (pm)
	    {
	      *the_hour += 12;
	    }
	}
      else
	{
	  error = CNV_ERR_BAD_AM_PM;
	  co_signal (error, CNV_ER_FMT_BAD_AM_PM, "p");
	  break;
	}

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIME;
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * us_mtime_string() - Return a string representing the given time in the US
 *    time format.
 * return:
 * the_time(in) :
 */
static const char *
us_mtime_string (int hour, int minute, int second, int millisecond)
{
  static char time_string[FMT_MAX_MTIME_STRING * MB_LEN_MAX + 1];

  sprintf (time_string, "%d:%02d:%02d.%03d %s", hour % 12 ? hour % 12 : 12, minute, second, millisecond,
	   hour > 12 ? local_pm () : local_am ());
  assert (strlen (time_string) < sizeof time_string);

  return time_string;
}

/*
 * us_mtime_value() - Scan tokens and parse a time value in the US time format.
 *    If a valid value can't be found, then return an error condition.
 *    otherwise, set the_hour, the_min, and the_sec to the value found.
 * return:
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
us_mtime_value (int *the_hour, int *the_min, int *the_sec, int *the_msec)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  bool pm;

  type = cnv_fmt_lex (&token);
  if (!(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY || type == FT_TIME_DIGITS_0)
      || (*the_hour = atoi (token.text)) > 23 || *the_hour < 0)
    {
      error = CNV_ERR_BAD_TIME;
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
      goto end;
    }

  if (cnv_fmt_lex (&token) != FT_TIME_SEPARATOR)
    {
      error = CNV_ERR_BAD_TIME;
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
      goto end;
    }

  error = fmt_minute_value ("M", the_min);
  if (error)
    {
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
      goto end;
    }

  if (cnv_fmt_lex (&token) != FT_TIME_SEPARATOR)
    {
      error = CNV_ERR_BAD_TIME;
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
      goto end;
    }

  error = fmt_second_value ("S", the_sec);
  if (error)
    {
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
      goto end;
    }

  /* TODO: DATETIME MILLISECOND SEPARATOR ?? */
  if (cnv_fmt_lex (&token) != FT_TIME_SEPARATOR)
    {
      *the_msec = 0;
    }
  else
    {
      error = fmt_millisecond_value ("MS", the_msec);
      if (error)
	{
	  co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
	  goto end;
	}
    }

  /* Skip blank "pattern" character. */
  if (strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH) == 0)
    {
      cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_LOCAL_TIME);
    }
  else
    {
      cnv_fmt_analyze (cnv_fmt_next_token (), FL_LOCAL_TIME);
    }

  /* we used to use local_am_pm_value() here, but it wasn't flexible enough to handle 24 hour time strings (no "AM" or
   * "PM" designator). */

  type = cnv_fmt_lex (&token);
  if (type == FT_NONE)
    {
      /* do nothing to hour, no "AM" or "PM" follows */
      ;
    }
  else if (type == FT_AM_PM && *the_hour >= 1 && *the_hour <= 12)
    {
      pm = token.value;
      /* convert 12 to 0 hour before adding 12 for PM values */
      *the_hour %= 12;
      if (pm)
	{
	  *the_hour += 12;
	}
    }
  else
    {
      error = CNV_ERR_BAD_AM_PM;
      co_signal (error, CNV_ER_FMT_BAD_AM_PM, "p");
    }

end:
  return error;
}

/*
 * us_timestamp_string() - Return a string representing the given timestamp in
 *    the US timestamp format.
 * return:
 * the_timestamp(in) :
 */
static const char *
us_timestamp_string (const DB_TIMESTAMP * the_timestamp)
{
  static char timestamp_string[FMT_MAX_TIMESTAMP_STRING * MB_LEN_MAX + 1];

  DB_DATE the_date;
  DB_TIME the_time;
  int month;
  int day;
  int year;

  db_timestamp_decode_ses ((DB_TIMESTAMP *) the_timestamp, &the_date, &the_time);
  db_date_decode (&the_date, &month, &day, &year);

  sprintf (timestamp_string, "%s %s", us_date_string (month, day, year), us_time_string (&the_time));
  assert (strlen (timestamp_string) < sizeof timestamp_string);

  return timestamp_string;
}

/*
 * us_timestamp_value() - Scan tokens and parse timestamp value in the US
 *    timestamp format. If a valid value can't be found, then return an
 *    error condition. otherwise, set the_day, the_month, the_year,
 *    the_hour (0-23), the_min, and the_sec to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
us_timestamp_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec)
{
  bool bad_value = false;
  int error = 0;

  do
    {
      error = us_date_value (the_month, the_day, the_year);
      if (error)
	{
	  break;
	}

      /* Skip blank "pattern" character. */
      bad_value = strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH);
      if (bad_value)
	{
	  break;
	}
      cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_LOCAL_TIME);

      error = us_time_value (the_hour, the_min, the_sec);

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIMESTAMP;
      co_signal (error, CNV_ER_FMT_BAD_TIMESTAMP, "C");
    }

  return error;
}

/*
 * us_datetime_string() - Return a string representing the given datetime in
 *    the US datetime format.
 * return:
 * the_timestamp(in) :
 */
static const char *
us_datetime_string (const DB_DATETIME * the_datetime)
{
  static char datetime_string[FMT_MAX_DATETIME_STRING * MB_LEN_MAX + 1];

  int month, day, year;
  int hour, minute, second, millisecond;

  db_datetime_decode ((DB_DATETIME *) the_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

  sprintf (datetime_string, "%s %s", us_date_string (month, day, year),
	   us_mtime_string (hour, minute, second, millisecond));

  return datetime_string;
}

/*
 * us_datetime_value() - Scan tokens and parse datetime value in the US
 *    datetime format. If a valid value can't be found, then return an
 *    error condition. otherwise, set the_day, the_month, the_year,
 *    the_hour (0-23), the_min, the_sec, and the_msec to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
us_datetime_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec,
		   int *the_msec)
{
  bool bad_value = false;
  int error = 0;

  do
    {
      error = us_date_value (the_month, the_day, the_year);
      if (error)
	{
	  break;
	}

      /* Skip blank "pattern" character. */
      bad_value = strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH);
      if (bad_value)
	{
	  break;
	}
      cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_LOCAL_TIME);

      error = us_mtime_value (the_hour, the_min, the_sec, the_msec);

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_DATETIME;
      co_signal (error, CNV_ER_FMT_BAD_DATETIME, "C");
    }

  return error;
}

/*
 *  KR Zone Functions
 */

/*
 * ko_euc_year_wc() -
 * return:
 */
static wchar_t
ko_euc_year_wc (void)
{
  static wchar_t ko_euc_year = 0;

  if (!ko_euc_year)
    {
      mbtowc (&ko_euc_year, KOREAN_EUC_YEAR_SYMBOL, 1);
    }

  return ko_euc_year;
}

/*
 * ko_euc_month_wc() -
 * return:
 */
static wchar_t
ko_euc_month_wc (void)
{
  static wchar_t ko_euc_month = 0;

  if (!ko_euc_month)
    {
      mbtowc (&ko_euc_month, KOREAN_EUC_MONTH_SYMBOL, 1);
    }

  return ko_euc_month;
}

/*
 * ko_euc_day_wc() -
 * return:
 */
static wchar_t
ko_euc_day_wc (void)
{
  static wchar_t ko_euc_day = 0;

  if (!ko_euc_day)
    {
      mbtowc (&ko_euc_day, KOREAN_EUC_DAY_SYMBOL, 1);
    }

  return ko_euc_day;
}
#endif

/*
 * ko_date_string() - Return a string representing the given date in the
 *    Korean date format.
 * return:
 * month(in) :
 * day(in) :
 * year(in) :
 */
static const char *
ko_date_string (int month, int day, int year)
{
  static char date_string[FMT_MAX_DATE_STRING * MB_LEN_MAX + 1];

  sprintf (date_string, "%04d/%02d/%02d", year, month, day);
  assert (strlen (date_string) < (int) sizeof (date_string));

  return date_string;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ko_alt_date_string() - Return a string representing the given date in the
 *    Korean date format.
 * return:
 * month(in) :
 * day(in) :
 * year(in) :
 */
static const char *
ko_alt_date_string (int month, int day, int year)
{
  static char date_string[FMT_MAX_DATE_STRING * MB_LEN_MAX + 1];

  sprintf (date_string, "%04d%s%02d%s%02d%s", year, KOREAN_EUC_YEAR_SYMBOL, month, KOREAN_EUC_MONTH_SYMBOL, day,
	   KOREAN_EUC_DAY_SYMBOL);
  assert (strlen (date_string) < sizeof date_string);

  return date_string;
}
#endif

/*
 * ko_date_value() - Scan tokens and parse a date value in the Korean date
 *    format. If a valid value can't be found, then return an error condition.
 *   otherwise, set the_year, the_month, and the_day to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 */
static int
ko_date_value (int *the_month, int *the_day, int *the_year)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;

  do
    {
      FMT_TOKEN_TYPE type;

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY);
      if (bad_value)
	{
	  break;
	}

      *the_year = atoi (token.text);
      /* Year 0 doesn't exist! */
      bad_value = type == FT_TIME_DIGITS_ANY && *the_year == 0;
      if (bad_value)
	{
	  break;
	}

      if (type != FT_TIME_DIGITS_ANY)
	{
	  /* Abbreviated year: add current century. */
	  struct tm tm_val, *today;
	  time_t now = time (NULL);
	  today = localtime_r (&now, &tm_val);

	  if (today != NULL)
	    {
	      *the_year += ((today->tm_year + 1900) / 100) * 100;
	    }
	  else
	    {
	      bad_value = true;
	      break;
	    }
	}

      bad_value = cnv_fmt_lex (&token) != FT_DATE_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS_ANY
		    || type == FT_TIME_DIGITS_BLANK) || (*the_month = atoi (token.text)) > 12 || *the_month < 1;
      if (bad_value)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_DATE_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS_ANY
		    || type == FT_TIME_DIGITS_BLANK) || (*the_day = atoi (token.text)) > 31 || *the_day < 1;
      if (bad_value)
	{
	  break;
	}

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_DATE;
      co_signal (error, CNV_ER_FMT_BAD_DATE, "x");
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ko_alt_date_value() - Scan tokens and parse a date value in the Korean
 *    date format. If a valid value can't be found, then return an error
 *    condition. otherwise, set the_year, the_month, and the_day to the value
 *    found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 */
static int
ko_alt_date_value (int *the_month, int *the_day, int *the_year)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;

  do
    {
      FMT_TOKEN_TYPE type;

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY);
      if (bad_value)
	{
	  break;
	}

      *the_year = atoi (token.text);
      /* Year 0 doesn't exist! */
      bad_value = type == FT_TIME_DIGITS_ANY && *the_year == 0;
      if (bad_value)
	{
	  break;
	}

      if (type != FT_TIME_DIGITS_ANY)
	{

	  /* Abbreviated year: add current century. */
	  struct tm tm_val, *today;
	  time_t now = time (NULL);
	  today = localtime_r (&now, &tm_val);

	  if (today != NULL)
	    {
	      *the_year += ((today->tm_year + 1900) / 100) * 100;
	    }
	  else
	    {
	      bad_value = true;
	      break;
	    }
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_LOCAL_DATE_SEPARATOR && token.text == intl_mbs_chr (token.text, ko_euc_year_wc ()));
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS_ANY
		    || type == FT_TIME_DIGITS_BLANK) || (*the_month = atoi (token.text)) > 12 || *the_month < 1;
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_LOCAL_DATE_SEPARATOR && token.text == intl_mbs_chr (token.text, ko_euc_month_wc ()));
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS_ANY
		    || type == FT_TIME_DIGITS_BLANK) || (*the_day = atoi (token.text)) > 31 || *the_day < 1;
      if (bad_value)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_LOCAL_DATE_SEPARATOR && token.text == intl_mbs_chr (token.text, ko_euc_day_wc ()));
      if (bad_value)
	{
	  break;
	}

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_DATE;
      co_signal (error, CNV_ER_FMT_BAD_DATE, "E");
    }

  return error;
}
#endif

/*
 * ko_time_string() - Return a string representing the given time in the
 *    Korean time format.
 * return:
 * the_time(in) :
 */
static const char *
ko_time_string (const DB_TIME * the_time)
{
  static char time_string[FMT_MAX_TIME_STRING * MB_LEN_MAX + 1];
  int hour, min, sec;

  db_time_decode ((DB_TIME *) the_time, &hour, &min, &sec);

  sprintf (time_string, "%s %d\xbd\xc3%02d\xba\xd0%02d\xc3\xca",	/* ????/????/???? */
	   local_am_pm_string (the_time), (hour % 12 ? hour % 12 : 12), min, sec);
  assert (strlen (time_string) < (int) sizeof (time_string));

  return time_string;
}

/*
 * ko_time_value() - Scan tokens and parse a time value in the Korean time
 *    format. If a valid value can't be found, then return an error condition.
 *   otherwise, set the_hour, the_min, and the_sec to the value found.
 * return:
 * the_hour(in) :
 * the_min(in) :
 * the_sec(in) :
 */
static int
ko_time_value (int *the_hour, int *the_min, int *the_sec)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  bool pm;

  do
    {
      error = local_am_pm_value (&pm);	/* ????????/???????? parsing */
      if (error)
	{
	  break;
	}

      *the_hour %= 12;
      if (pm)
	{
	  *the_hour += 12;
	}
      /* Skip blank "pattern" character. */
      while (!strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH))
	cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_KO_KR_TIME);

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_BLANK || type == FT_TIME_DIGITS_0
		    || type == FT_TIME_DIGITS_ANY) || (*the_hour = atoi (token.text)) > 12 || *the_hour < 1;
      if (bad_value)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      error = fmt_minute_value ("M", the_min);
      if (error)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      error = fmt_second_value ("S", the_sec);
      if (error)
	{
	  break;
	}

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIME;
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * ko_mtime_string() - Return a string representing the given time in the
 *    Korean time format.
 * return:
 * hour(in) :
 * minute(in) :
 * millisecond(in) :
 * millisecond(in) :
 */
static const char *
ko_mtime_string (int hour, int minute, int second, int millisecond)
{
  static char mtime_string[FMT_MAX_MTIME_STRING * MB_LEN_MAX + 1];

  sprintf (mtime_string, "%s %d\xbd\xc3%02d\xba\xd0%02d\xc3\xca.%03d",	/* ????/????/???? */
	   hour >= 12 ? local_pm () : local_am (), (hour % 12 ? hour % 12 : 12), minute, second, millisecond);
  assert (strlen (mtime_string) < sizeof (mtime_string));

  return mtime_string;
}

/*
 * ko_mtime_value() - Scan tokens and parse a time value in the Korean time
 *    format. If a valid value can't be found, then return an error condition.
 *   otherwise, set the_hour, the_min, the_sec and the_msec to the value found.
 * return:
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 * the_msec(out) :
 */
static int
ko_mtime_value (int *the_hour, int *the_min, int *the_sec, int *the_msec)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  bool pm;

  do
    {
      error = local_am_pm_value (&pm);	/* ????????/???????? parsing */
      if (error)
	{
	  break;
	}

      *the_hour %= 12;
      if (pm)
	{
	  *the_hour += 12;
	}
      /* Skip blank "pattern" character. */
      while (!strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH))
	cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_KO_KR_TIME);

      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_BLANK || type == FT_TIME_DIGITS_0
		    || type == FT_TIME_DIGITS_ANY) || (*the_hour = atoi (token.text)) > 12 || *the_hour < 1;
      if (bad_value)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      error = fmt_minute_value ("M", the_min);
      if (error)
	{
	  break;
	}

      bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
      if (bad_value)
	{
	  break;
	}

      error = fmt_second_value ("S", the_sec);
      if (error)
	{
	  break;
	}

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIME;
      co_signal (error, CNV_ER_FMT_BAD_TIME, "X");
    }

  return error;
}

/*
 * ko_timestamp_string() - Return a string representing the given timestamp in
 *    the Korean timestamp format.
 * return:
 * the_timestamp(in) :
 */
static const char *
ko_timestamp_string (const DB_TIMESTAMP * the_timestamp)
{
  static char timestamp_string[FMT_MAX_TIMESTAMP_STRING * MB_LEN_MAX + 1];

  DB_DATE the_date;
  DB_TIME the_time;
  int month;
  int day;
  int year;

  db_timestamp_decode_ses ((DB_TIMESTAMP *) the_timestamp, &the_date, &the_time);
  db_date_decode (&the_date, &month, &day, &year);

  sprintf (timestamp_string, "%s %s", ko_date_string (month, day, year), ko_time_string (&the_time));
  assert (strlen (timestamp_string) < sizeof timestamp_string);

  return timestamp_string;
}

/*
 * ko_timestamp_value() - Scan tokens and parse timestamp value in the Korean
 *    timestamp format. If a valid value can't be found, then return an error
 *    condition.
 *    otherwise, set the_year, the_month, the_day, the_hour (0-23),
 *    the_min, and the_sec to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
ko_timestamp_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec)
{
  bool bad_value = false;
  int error = 0;

  do
    {
      error = ko_date_value (the_month, the_day, the_year);
      if (error)
	{
	  break;
	}

      /* Skip blank "pattern" character. */
      while (!strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH))
	{
	  cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_KO_KR_TIME);
	}

      error = ko_time_value (the_hour, the_min, the_sec);

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIMESTAMP;
      co_signal (error, CNV_ER_FMT_BAD_TIMESTAMP, "C");
    }

  return error;
}

/*
 * ko_datetime_string() - Return a string representing the given datetime in
 *    the Korean datetime format.
 * return:
 * the_timestamp(in) :
 */
static const char *
ko_datetime_string (const DB_DATETIME * the_datetime)
{
  static char datetime_string[FMT_MAX_TIMESTAMP_STRING * MB_LEN_MAX + 1];

  int month, day, year;
  int hour, minute, second, millisecond;

  db_datetime_decode ((DB_DATETIME *) the_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

  sprintf (datetime_string, "%s %s", ko_date_string (month, day, year),
	   ko_mtime_string (hour, minute, second, millisecond));
  assert (strlen (datetime_string) < sizeof (datetime_string));

  return datetime_string;
}

/*
 * ko_datetime_value() - Scan tokens and parse datetime value in the Korean
 *    datetime format. If a valid value can't be found, then return an error
 *    condition.
 *    otherwise, set the_year, the_month, the_day, the_hour (0-23),
 *    the_min, the_sec and the_msec to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
ko_datetime_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec,
		   int *the_msec)
{
  bool bad_value = false;
  int error = 0;

  do
    {
      error = ko_date_value (the_month, the_day, the_year);
      if (error)
	{
	  break;
	}

      /* Skip blank "pattern" character. */
      while (!strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH))
	{
	  cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_KO_KR_TIME);
	}

      error = ko_time_value (the_hour, the_min, the_sec);

    }
  while (0);

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIMESTAMP;
      co_signal (error, CNV_ER_FMT_BAD_TIMESTAMP, "C");
    }

  return error;
}
#endif

/*
 * Locale-Dependent Functions
 */

/*
 * local_grouping() - Return digit grouping array for the current locale.
 *    See localeconv() for a definition of array contents.
 * return:
 */
const char *
local_grouping (void)
{
  char *local_value = localeconv ()->grouping;

  return strlen (local_value) ? (const char *) local_value : "\3";
}

/*
 * local_thousands() - Returns thousands separator for current locale.
 * return:
 */
const char *
local_thousands (void)
{
  char *local_value = localeconv ()->thousands_sep;

  return strlen (local_value) ? (const char *) local_value : ",";
}

/*
 * local_decimal() - Return decimal separator for current locale.
 * return:
 */
const char *
local_decimal (void)
{
  char *local_value = localeconv ()->decimal_point;

  return strlen (local_value) ? (const char *) local_value : ".";
}

/*
 * local_am() - Return AM affix for current locale.
 * return:
 * note : nl_langinfo() not implemented. Must fix to support other locales.
 *        local_value = nl_langinfo(AM_STR);
 */
static const char *
local_am (void)
{
  const char *local_value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	local_value = "am";
	break;
      }
    case INTL_ZONE_KR:
      {
	local_value = "\xbf\xc0\xc0\xfc";	/* ???????? */
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	local_value = "";
	break;
      }
    }

  return local_value;
}

/*
 * local_pm() - Return PM affix for current locale.
 * return:
 *
 * note : nl_langinfo() not implemented. Must fix to support other locales.
 *        local_value = nl_langinfo( PM_STR);
 */
static const char *
local_pm (void)
{
  const char *local_value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	local_value = "pm";
	break;
      }
    case INTL_ZONE_KR:
      {
	local_value = "\xbf\xc0\xc8\xc4";	/* ???????? */
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	local_value = "";
	break;
      }
    }

  return local_value;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * local_short_month_name() - Return the short month name for the given
 *     month number (0-11)
 * return:
 * month(in) :
 */
static const char *
local_short_month_name (int month)
{
  const char *month_name;

  assert (month >= 0 && month <= 11);

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	month_name = eng_short_month_names[month];
	break;
      }
    case INTL_ZONE_KR:
      {
	month_name = kr_short_month_names[month];
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	month_name = "";
	break;
      }
    }

  return month_name;
}

/*
 * local_long_month_name() - Return the long month name for the given
 *     month number (0-11)
 * return:
 * month(in) :
 */
static const char *
local_long_month_name (int month)
{
  const char *month_name;

  assert (month >= 0 && month <= 11);

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	month_name = eng_long_month_names[month];
	break;
      }
    case INTL_ZONE_KR:
      {
	month_name = kr_long_month_names[month];
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	month_name = "";
	break;
      }
    }

  return month_name;
}

/*
 * local_short_weekday_name() - Return the short weekday name for the given
 *     weekday number (0-6)
 * return:
 * weekday(in) :
 */
static const char *
local_short_weekday_name (int weekday)
{
  const char *weekday_name;

  assert (weekday >= 0 && weekday <= 6);

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	weekday_name = eng_short_weekday_names[weekday];
	break;
      }
    case INTL_ZONE_KR:
      {
	weekday_name = kor_weekday_names[weekday];
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	weekday_name = "";
	break;
      }
    }

  return weekday_name;
}

/*
 * local_long_weekday_name() - Return the long weekday name for the given
 *     weekday number (0-6)
 * return:
 * weekday(in) :
 */
static const char *
local_long_weekday_name (int weekday)
{
  const char *weekday_name;

  assert (weekday >= 0 && weekday <= 6);

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	weekday_name = eng_long_weekday_names[weekday];
	break;
      }
    case INTL_ZONE_KR:
      {
	weekday_name = kor_weekday_names[weekday];
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	weekday_name = "";
	break;
      }
    }

  return weekday_name;
}
#endif

/*
 * local_date_string() - Return a string representing the given date in the
 *    locale's date format.
 * return:
 * month(in) :
 * day(in) :
 * year(in) :
 */
static const char *
local_date_string (int month, int day, int year)
{
  const char *value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_date_string (month, day, year);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_date_string (month, day, year);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = "";
	break;
      }
    }

  return value;
}

/*
 * local_date_value() - Scan tokens and parse a date value in the locale's
 *    date format. If a valid value can't be found, then return an error
 *    condition. otherwise, set the_month, the_day, and the_year to the
 *    value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 */
static int
local_date_value (int *the_month, int *the_day, int *the_year)
{
  int value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_date_value (the_month, the_day, the_year);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_date_value (the_month, the_day, the_year);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = 0;
	break;
      }
    }

  return value;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * local_alt_date_string() - Return a string representing the given date in the
 *     locale's alternate date format.
 * return:
 * month(in) :
 * day(in) :
 * year(in) :
 */
static const char *
local_alt_date_string (int month, int day, int year)
{
  const char *value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_alt_date_string (month, day, year);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_alt_date_string (month, day, year);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = "";
	break;
      }
    }

  return value;
}

/*
 * local_alt_date_value() - Scan tokens and parse a date value in the locale's
 *     alternate date format. If a valid value can't be found, then return an
 *     error condition. otherwise, set the_month, the_day, and the_year to the
 *     value found.
 * return:
 * the_month(in) :
 * the_day(in) :
 * the_year(in) :
 */
static int
local_alt_date_value (int *the_month, int *the_day, int *the_year)
{
  int value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_alt_date_value (the_month, the_day, the_year);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_alt_date_value (the_month, the_day, the_year);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = 0;
	break;
      }
    }

  return value;
}
#endif

/*
 * local_time_string() - Return a string representing the given time in the
 *    locale's time format.
 * return:
 * the_time(in) :
 */
static const char *
local_time_string (const DB_TIME * the_time)
{
  const char *value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_time_string (the_time);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_time_string (the_time);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = "";
	break;
      }
    }

  return value;
}

/*
 * local_time_value() - Scan tokens and parse a time value in the locale's
 *    time format. If a valid value can't be found, then return an error
 *    condition. otherwise, set the_hour, the_min, and the_sec to the value
 *    found.
 * return:
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
local_time_value (int *the_hour, int *the_min, int *the_sec)
{
  int value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_time_value (the_hour, the_min, the_sec);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_time_value (the_hour, the_min, the_sec);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = 0;
	break;
      }
    }

  return value;
}

/*
 * local_am_pm_string() - Return a string representing AM/PM for
 *    the given time.
 * return:
 * the_time(in) :
 */
static const char *
local_am_pm_string (const DB_TIME * the_time)
{
  int hour, min, sec;
  db_time_decode ((DB_TIME *) the_time, &hour, &min, &sec);
  return hour >= 12 ? local_pm () : local_am ();
}

/*
 * local_am_pm_value() - Scan tokens and parse an AM/PM value in the locale's
 *    time format. If a valid value can't be found, then return an error
 *    condition. otherwise, set pm to true/false if PM/AM found.
 * return:
 * pm(out):
 */
static int
local_am_pm_value (bool * pm)
{
  int error = 0;
  FMT_TOKEN token;

  if (cnv_fmt_lex (&token) != FT_AM_PM)
    {
      error = CNV_ERR_BAD_AM_PM;
      co_signal (error, CNV_ER_FMT_BAD_AM_PM, "p");
    }
  else
    {
      *pm = (bool) token.value;
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * local_timestamp_string() - Return a string representing the given timestamp
 *    in the locale's timestamp format.
 * return:
 * the_timestamp(in) :
 */
static const char *
local_timestamp_string (const DB_TIMESTAMP * the_timestamp)
{
  const char *value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_timestamp_string (the_timestamp);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_timestamp_string (the_timestamp);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = "";
	break;
      }
    }

  return value;
}

/*
 * local_timestamp_value() - Scan tokens and parse timestamp value in the
 *    locale's timestamp format. If a valid value can't be found, then
 *    return an error condition. otherwise, set the_day, the_month, the_year,
 *    the_hour (0-23), the_min, and the_sec to the value found.
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
local_timestamp_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec)
{
  int value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_timestamp_value (the_month, the_day, the_year, the_hour, the_min, the_sec);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_timestamp_value (the_month, the_day, the_year, the_hour, the_min, the_sec);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = 0;
	break;
      }
    }

  return value;
}

/*
 * local_datetime_string() - Return a string representing the given timestamp
 *    in the locale's timestamp format.
 * return:
 * the_timestamp(in) :
 */
static const char *
local_datetime_string (const DB_DATETIME * the_datetime)
{
  const char *value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_datetime_string (the_datetime);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_datetime_string (the_datetime);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = "";
	break;
      }
    }

  return value;
}

/*
 * local_datetime_value() -
 * return:
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
local_datetime_value (int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min, int *the_sec,
		      int *the_msec)
{
  int value;

  switch (intl_zone (LC_TIME))
    {
    case INTL_ZONE_US:
      {
	value = us_datetime_value (the_month, the_day, the_year, the_hour, the_min, the_sec, the_msec);
	break;
      }
    case INTL_ZONE_KR:
      {
	value = ko_datetime_value (the_month, the_day, the_year, the_hour, the_min, the_sec, the_msec);
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	value = 0;
	break;
      }
    }

  return value;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 *                            Basic Utility Functions
 */

/*
 * cnv_wcs() - Return the result of converting the given multibyte string
 *    to wide characters.
 * return:
 * mbs(in) :
 */
static const wchar_t *
cnv_wcs (const char *mbs)
{
#if defined(SERVER_MODE)
  ADJ_ARRAY *buffer = cnv_get_thread_local_adj_buffer (0);
#else
  ADJ_ARRAY *buffer = cnv_adj_buffer1;
#endif
  size_t nchars;
  wchar_t *wchars;

  assert (mbs);

  /* Initialize buffer. */
  if (!buffer)
    {
#if defined(SERVER_MODE)
      buffer = adj_ar_new (sizeof (wchar_t), 0, 1.0);
      cnv_set_thread_local_adj_buffer (0, buffer);
#else
      buffer = cnv_adj_buffer1 = adj_ar_new (sizeof (wchar_t), 0, 1.0);
#endif
    }
  adj_ar_replace (buffer, NULL, strlen (mbs) + 1, 0, ADJ_AR_EOA);

  wchars = (wchar_t *) adj_ar_get_buffer (buffer);
  nchars = mbstowcs (wchars, mbs, adj_ar_length (buffer));
  assert ((int) nchars < adj_ar_length (buffer));

  return (const wchar_t *) wchars;
}

/*
 * cnv_currency_zone() - Return the locale zone associated with the given
 *    currency type.
 * return:
 * currency(in) :
 */
static INTL_ZONE
cnv_currency_zone (DB_CURRENCY currency)
{
  /* quiet compiler with assignment */
  INTL_ZONE zone = INTL_ZONE_US;

  assert (cnv_valid_currency (currency));

  switch (currency)
    {
    case DB_CURRENCY_DOLLAR:
      {
	zone = INTL_ZONE_US;
	break;
      }
    case DB_CURRENCY_YEN:
      {
	break;
      }
    case DB_CURRENCY_WON:
      {
	zone = INTL_ZONE_KR;
	break;
      }
    case DB_CURRENCY_TL:
      {
	zone = INTL_ZONE_TR;
	break;
      }
    case DB_CURRENCY_BRITISH_POUND:
    case DB_CURRENCY_CAMBODIAN_RIEL:
    case DB_CURRENCY_CHINESE_RENMINBI:
    case DB_CURRENCY_INDIAN_RUPEE:
    case DB_CURRENCY_RUSSIAN_RUBLE:
    case DB_CURRENCY_AUSTRALIAN_DOLLAR:
    case DB_CURRENCY_CANADIAN_DOLLAR:
    case DB_CURRENCY_BRASILIAN_REAL:
    case DB_CURRENCY_ROMANIAN_LEU:
    case DB_CURRENCY_EURO:
    case DB_CURRENCY_SWISS_FRANC:
    case DB_CURRENCY_DANISH_KRONE:
    case DB_CURRENCY_NORWEGIAN_KRONE:
    case DB_CURRENCY_BULGARIAN_LEV:
    case DB_CURRENCY_VIETNAMESE_DONG:
    case DB_CURRENCY_CZECH_KORUNA:
    case DB_CURRENCY_POLISH_ZLOTY:
    case DB_CURRENCY_SWEDISH_KRONA:
    case DB_CURRENCY_CROATIAN_KUNA:
    case DB_CURRENCY_SERBIAN_DINAR:
      {
      }
      break;
    default:
      break;
    }

  return zone;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * cnv_currency_symbol() - Return the currency symbol string for the given
 *    currency type.
 * return:
 * currency(in) :
 */
static const char *
cnv_currency_symbol (DB_CURRENCY currency)
{
  const char *symbol;

  assert (cnv_valid_currency (currency));

  switch (currency)
    {
    case DB_CURRENCY_DOLLAR:
      {
	symbol = "$";
	break;
      }
    case DB_CURRENCY_WON:
      {
	symbol = "\\";
	break;
      }

    case DB_CURRENCY_YEN:
    case DB_CURRENCY_POUND:
    default:
      {
	assert (!"Currency symbol not implemented!");
	symbol = "";
	break;
      }
    }

  return symbol;
}
#endif

/*
 * cnv_valid_currency() - Return true if currency type is valid.
 * return:
 * currency(in) :
 */
static bool
cnv_valid_currency (DB_CURRENCY currency)
{
  bool valid = true;

  switch (currency)
    {
    case DB_CURRENCY_DOLLAR:
    case DB_CURRENCY_YEN:
    case DB_CURRENCY_WON:
    case DB_CURRENCY_TL:
    case DB_CURRENCY_BRITISH_POUND:
    case DB_CURRENCY_CAMBODIAN_RIEL:
    case DB_CURRENCY_CHINESE_RENMINBI:
    case DB_CURRENCY_INDIAN_RUPEE:
    case DB_CURRENCY_RUSSIAN_RUBLE:
    case DB_CURRENCY_AUSTRALIAN_DOLLAR:
    case DB_CURRENCY_CANADIAN_DOLLAR:
    case DB_CURRENCY_BRASILIAN_REAL:
    case DB_CURRENCY_ROMANIAN_LEU:
    case DB_CURRENCY_EURO:
    case DB_CURRENCY_SWISS_FRANC:
    case DB_CURRENCY_DANISH_KRONE:
    case DB_CURRENCY_NORWEGIAN_KRONE:
    case DB_CURRENCY_BULGARIAN_LEV:
    case DB_CURRENCY_VIETNAMESE_DONG:
    case DB_CURRENCY_CZECH_KORUNA:
    case DB_CURRENCY_POLISH_ZLOTY:
    case DB_CURRENCY_SWEDISH_KRONA:
    case DB_CURRENCY_CROATIAN_KUNA:
    case DB_CURRENCY_SERBIAN_DINAR:
      {
	break;
      }

    default:
      {
	valid = false;
	break;
      }
    }

  return valid;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * cnv_valid_timestamp() - Return true if the give time lies in the range
 *    which can be encoded as a valid timestamp. If false, then signal an
 *    error condition.
 * return:
 * the_date(in) :
 * the_time(in) :
 */
static bool
cnv_valid_timestamp (DB_DATE * the_date, DB_TIME * the_time)
{
  static DB_TIMESTAMP min_timestamp = 0;
  static DB_TIMESTAMP max_timestamp = INT_MAX;
  static DB_DATE min_date;
  static DB_DATE max_date = 0;
  static DB_TIME min_time;
  static DB_TIME max_time;
  bool valid = false;

  if (!max_date)
    {
      /* Initialize timestamp range constants. */
      db_timestamp_decode_ses (&min_timestamp, &min_date, &min_time);
      db_timestamp_decode_ses (&max_timestamp, &max_date, &max_time);
    }

  if (*the_date < min_date || (*the_date == min_date && *the_time < min_time))
    {
      char limit[FMT_MAX_TIMESTAMP_STRING + 1];
      db_timestamp_string (&min_timestamp, "", limit, sizeof limit);
      co_signal (CNV_ERR_TIMESTAMP_UNDERFLOW, CNV_ER_FMT_TIMESTAMP_UNDERFLOW, limit);
    }
  else if (*the_date > max_date || (*the_date == max_date && *the_time > max_time))
    {
      char limit[FMT_MAX_TIMESTAMP_STRING + 1];
      db_timestamp_string (&max_timestamp, "", limit, sizeof limit);
      co_signal (CNV_ERR_TIMESTAMP_OVERFLOW, CNV_ER_FMT_TIMESTAMP_OVERFLOW, limit);
    }
  else
    {
      valid = true;
    }

  return valid;
}

/*
 * cnv_get_string_buffer() - Return an empty array big enough to store
 *    a NUL-terminated string of the given length.
 * return:
 * nchars(in) :
 */
static ADJ_ARRAY *
cnv_get_string_buffer (int nchars)
{
#if defined(SERVER_MODE)
  ADJ_ARRAY *buffer = cnv_get_thread_local_adj_buffer (1);
#else
  ADJ_ARRAY *buffer = cnv_adj_buffer2;
#endif

  assert (nchars >= 0);

  nchars++;

  if (!buffer)
    {
#if defined(SERVER_MODE)
      buffer = adj_ar_new (sizeof (char), nchars, 1.0);
      cnv_set_thread_local_adj_buffer (1, buffer);
#else
      buffer = cnv_adj_buffer2 = adj_ar_new (sizeof (char), nchars, 1.0);
#endif
    }
  adj_ar_replace (buffer, NULL, nchars, 0, ADJ_AR_EOA);

  return buffer;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * cnv_get_value_string_buffer() - Return an empty array used to accumulate a
 *    value string. Ensure array is big enough to hold the given number of
 *    chars.
 * return:
 * nchars(in):
 */
static ADJ_ARRAY *
cnv_get_value_string_buffer (int nchars)
{
#if defined(SERVER_MODE)
  ADJ_ARRAY *buffer = cnv_get_thread_local_adj_buffer (2);
#else
  ADJ_ARRAY *buffer = cnv_adj_buffer3;
#endif

  assert (nchars >= 0);

  if (nchars)
    {
      nchars++;
    }

  if (!buffer)
    {
#if defined(SERVER_MODE)
      buffer = adj_ar_new (sizeof (char), nchars, 1.0);
      cnv_set_thread_local_adj_buffer (2, buffer);
#else
      buffer = cnv_adj_buffer3 = adj_ar_new (sizeof (char), nchars, 1.0);
#endif
    }
  adj_ar_replace (buffer, NULL, nchars, 0, ADJ_AR_EOA);

  return buffer;
}

/*
 * cnv_bad_char() - Signal an error for an invalid character.
 *    If unknown is true, then the bad char is unrecognizable.
 *    otherwise, the bad char is a valid char that is out of place.
 * return:
 * string(in) :
 * unknown(in) :
 */
static int
cnv_bad_char (const char *string, bool unknown)
{
  int error = unknown ? CNV_ERR_BAD_CHAR : CNV_ERR_BAD_POSITION;

  char the_char[MB_LEN_MAX + 1];
  int nbytes;

  /* Find length of first char. */
  while ((nbytes = mblen (string, strlen (string))) < 0)
    {
      string = "?";
    }

  /* Create 1-char string. */
  strncpy (the_char, string, nbytes);
  strcpy (the_char + nbytes, "");
  assert (strlen (the_char) < (int) sizeof (the_char));

  co_signal (error, unknown ? CNV_ER_FMT_BAD_CHAR : CNV_ER_FMT_BAD_POSITION, the_char);
  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * fmt_validate() - Return an error if the given format is invalid.
 * return:
 * format(in) :
 * mode(in) :
 * fmt_type(in) :
 * data_type(in) :
 */
static int
fmt_validate (const char *format, FMT_LEX_MODE mode, FMT_TOKEN_TYPE fmt_type, DB_TYPE data_type)
{
  FMT_TOKEN token;
  FMT_TOKEN_TYPE ttype;
  int error = 0;

  cnv_fmt_analyze (format, mode);
  ttype = cnv_fmt_lex (&token);
  if (!(ttype == FT_NONE || (ttype == fmt_type && cnv_fmt_lex (&token) == FT_NONE)))
    {
      error = CNV_ERR_BAD_FORMAT;
      co_signal (CNV_ERR_BAD_FORMAT, CNV_ER_FMT_BAD_FORMAT, format, pr_type_name (data_type));
    }
  return error;
}
#endif

/*
 * fmt_integral_value() - Scan tokens and parse an integral value. If a valid
 *    value can't be found, then return an error condition. otherwise, set
 *    the_value to the value found.
 * return:
 * digit_type(in): the type of digit chars allowed
 * ndigits(in): the maximum number of digits
 * sign_required(in): If true ,then a positive/negative sign token must
 *    be present.
 * thousands(in): If true , then thousands separators must be included.
 * the_value(out):
 */
static int
fmt_integral_value (FORMAT_DIGIT digit_type, int ndigits, bool sign_required, bool thousands, double *the_value)
{
  int nfound;
  int error;

  error = fmt_integral_digits (digit_type, ndigits, sign_required, thousands, the_value, &nfound);

  if (!error && ndigits)
    {
      /* Too many digits? */
      if (nfound > ndigits)
	{
	  error = CNV_ERR_EXTRA_INTEGER;
	  co_signal (CNV_ERR_EXTRA_INTEGER, CNV_ER_FMT_EXTRA_INTEGER);
	}
      /* Enough digits found? */
      else if (digit_type != DIGIT_Z && nfound < ndigits)
	{
	  error = CNV_ERR_MISSING_INTEGER;
	  co_signal (CNV_ERR_MISSING_INTEGER, CNV_ER_FMT_MISSING_INTEGER);
	}
    }

  return error;
}

/*
 * fmt_integral_digits() - Scan tokens and parse an integral value. If a valid
 *     value can't be found, then return an error condition. otherwise, set
 *     the_value to the value found.
 * return:
 * digit_type(in) : the type of digit chars allowed
 * ndigits(in) : the maximum number of digits
 * sign_required(in) : If true, then a positive/negative sign token must be
 *    present.
 * thousands(in) : If true, then thousands separators must be included.
 * the_value(out) :
 * nfound(out) : the number of digits in the value.
 */
static int
fmt_integral_digits (FORMAT_DIGIT digit_type, int ndigits, bool sign_required, bool thousands, double *the_value,
		     int *nfound)
{
  int error = 0;

  *the_value = 0.0;

  do
    {
      FMT_TOKEN_TYPE leading = FT_NONE;
      int nleading = 0;
      FMT_TOKEN token;
      FMT_TOKEN_TYPE type = cnv_fmt_lex (&token);
      bool negative = (type == FT_MINUS);

      /* Sign? */
      if (negative || (sign_required && type == FT_PLUS))
	{
	  /* Yes, scan next token. */
	  type = cnv_fmt_lex (&token);
	  if (type == FT_PLUS || type == FT_MINUS)
	    {
	      error = CNV_ERR_EXTRA_SIGN;
	      co_signal (CNV_ERR_EXTRA_SIGN, CNV_ER_FMT_EXTRA_SIGN);
	      break;
	    }
	}
      else if (sign_required)
	{
	  cnv_fmt_unlex ();
	  error = CNV_ERR_NO_SIGN;
	  co_signal (CNV_ERR_NO_SIGN, CNV_ER_FMT_NO_SIGN);
	  break;
	}

      /* Leading fill chars? */
      *nfound = 0;
      if (type == FT_ZEROES || type == FT_STARS)
	{
	  /* Yes, scan next token. */
	  leading = type;
	  nleading = intl_mbs_len (token.text);
	  type = cnv_fmt_lex (&token);
	}

      /* Any numeric chars left? */
      if (type == FT_NUMBER)
	{
	  int initial_digits = 0;
	  int group_size = *local_grouping ();

	  /* Yes, add to numeric value. */
	  for (; type == FT_NUMBER || type == FT_ZEROES; type = cnv_fmt_lex (&token))
	    {
	      int tdigits = intl_mbs_len (token.text);
	      if (thousands && group_size != CHAR_MAX && ((initial_digits += tdigits) > group_size))
		{
		  error = CNV_ERR_BAD_THOUS;
		  co_signal (CNV_ERR_BAD_THOUS, CNV_ER_FMT_BAD_THOUS);
		  break;
		}

	      *the_value = *the_value * pow (10.0, tdigits) + strtod (token.text, NULL);

	      *nfound += tdigits;
	    }

	  /* Add thousands groups, if necessary. */
	  if (thousands)
	    {
	      for (; type == FT_THOUSANDS; type = cnv_fmt_lex (&token))
		{
		  int tdigits = intl_mbs_len (token.text);
		  *the_value = *the_value * pow (10.0, tdigits) + strtod (token.text, NULL);
		  *nfound += tdigits;
		}
	      if (type == FT_NUMBER || type == FT_ZEROES
		  || (type == FT_UNKNOWN && mbs_eql (token.text, local_thousands ())))
		{
		  error = CNV_ERR_BAD_THOUS;
		  co_signal (CNV_ERR_BAD_THOUS, CNV_ER_FMT_BAD_THOUS);
		  break;
		}
	    }

	  /* Apply sign. */
	  if (negative)
	    {
	      *the_value = -(*the_value);
	    }
	}

      /* Assert: we've scanned following token, so put it back. */
      cnv_fmt_unlex ();

      /* Valid leading fill chars? */
      if (leading != FT_NONE)
	{
	  if ((leading == FT_STARS && digit_type != DIGIT_STAR) || (leading == FT_ZEROES && !(digit_type == DIGIT_9 ||
											      /* allow singleton zero. */
											      (digit_type == DIGIT_Z
											       && nleading == 1
											       && *the_value == 0.0))))
	    {

	      error = CNV_ERR_BAD_LEADING;
	      co_signal (CNV_ERR_BAD_LEADING, CNV_ER_FMT_BAD_LEADING, leading == FT_ZEROES ? LOCAL_0 : LOCAL_STAR);
	      break;
	    }

	  *nfound += nleading;
	}
    }
  while (0);

  return error;
}

/*
 * fmt_fractional_digits() - Scan tokens and parse fractional value. If a
 *    valid value can't be found, then return an error condition. otherwise,
 *    set the_value to the value found.
 * return:
 * digit_type(in) :the type of digit chars allowed
 * ndigits(in) : the maximum number of digits
 * the_value(out) :
 * nfound(out) : set to the number of digits in the value.
 */
static int
fmt_fractional_digits (FORMAT_DIGIT digit_type, int ndigits, double *the_value, int *nfound)
{
  int error = 0;

  *the_value = 0.0;
  *nfound = 0;

  do
    {
      FMT_TOKEN token;
      FMT_TOKEN_TYPE type;
      FMT_TOKEN_TYPE last = FT_NONE;
      double exponent = 0.0;

      /* Any numeric chars? */
      for (; (type = cnv_fmt_lex (&token)) == FT_NUMBER || type == FT_ZEROES; last = type)
	{

	  /* Yes, add to numeric value. */
	  int tdigits = intl_mbs_len (token.text);
	  exponent -= tdigits;
	  *the_value = *the_value + strtod (token.text, NULL) * pow (10.0, exponent);

	  *nfound += tdigits;
	}

      /* Any trailing fill chars? */
      if (type == FT_STARS)
	{
	  if (last != FT_ZEROES)
	    {
	      /* Can't have trailing zeroes AND trailing stars! */
	      last = type;
	    }
	  *nfound += intl_mbs_len (token.text);
	}
      else
	{
	  /* No, retry this token later. */
	  cnv_fmt_unlex ();
	}

      /* Valid trailing fill chars? */
      if ((last == FT_ZEROES && (digit_type != DIGIT_9 && digit_type != DIGIT_Z))
	  || (last == FT_STARS && digit_type != DIGIT_STAR))
	{
	  error = CNV_ERR_BAD_TRAILING;
	  co_signal (CNV_ERR_BAD_TRAILING, CNV_ER_FMT_BAD_TRAILING, last == FT_ZEROES ? LOCAL_0 : LOCAL_STAR);
	  break;
	}
    }
  while (0);

  return error;
}

/*
 * fmt_fractional_value() - Scan tokens and parse fractional value. If a valid
 *    value can't be found, then return an error condition. otherwise, set
 *    the_value to the value found.
 * return:
 * digit_type(in) : the type of digit chars allowed
 * ndigits(in) : the maximum number of digits
 * the_value(out) :
 */
static int
fmt_fractional_value (FORMAT_DIGIT digit_type, int ndigits, double *the_value)
{
  int nfound;

  int error = fmt_fractional_digits (digit_type,
				     ndigits,
				     the_value,
				     &nfound);

  if (ndigits)
    {
      /* Too many digits? */
      if (nfound > ndigits)
	{
	  error = CNV_ERR_EXTRA_FRACTION;
	  co_signal (CNV_ERR_EXTRA_FRACTION, CNV_ER_FMT_EXTRA_FRACTION);
	}

      /* Enough digits found? */
      else if (digit_type != DIGIT_Z && nfound < ndigits)
	{
	  error = CNV_ERR_MISSING_FRACTION;
	  co_signal (CNV_ERR_MISSING_FRACTION, CNV_ER_FMT_MISSING_FRACTION);
	}
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * fmt_add_thousands() -
 * return:
 * string() :
 * position() :
 */
static void
fmt_add_thousands (ADJ_ARRAY * string, int *position)
{
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  int start;
  int end;
  int sep_pos;
  int nbytes;
  int maxbytes;
  int ndigits;
  const char *next_char;
  const char *vstring = (const char *) adj_ar_get_buffer (string);
  const char *thous = local_thousands ();
  const char *group_size = local_grouping ();

  /* Find position of first digit */
  cnv_fmt_analyze (vstring, FL_LOCAL_NUMBER);
  while ((ttype = cnv_fmt_lex (&token)) == FT_MINUS || ttype == FT_PLUS || ttype == FT_CURRENCY || ttype == FT_ZEROES
	 || ttype == FT_STARS);
  start = CAST_BUFLEN ((cnv_fmt_next_token () - token.length) - vstring);

  /* Find end of digits. */
  for (; ttype == FT_NUMBER || ttype == FT_ZEROES; ttype = cnv_fmt_lex (&token));
  end = CAST_BUFLEN ((cnv_fmt_next_token () - token.length) - vstring);

  /* Get number of digits. */
  for (ndigits = 0, next_char = vstring + start, maxbytes = end - start;
       maxbytes > 0 && (nbytes = mblen (next_char, maxbytes)); ndigits++, next_char += nbytes, maxbytes -= nbytes);

  /* Insert separators from right to left, according to grouping. */
  for (sep_pos = ndigits - *group_size; *group_size != CHAR_MAX && sep_pos > 0; sep_pos -= *group_size)
    {

      int insert;

      vstring = (const char *) adj_ar_get_buffer (string);
      insert = CAST_STRLEN (intl_mbs_nth (vstring + start, sep_pos) - vstring);

      adj_ar_insert (string, thous, strlen (thous), insert);
      if (position && *position > insert)
	{
	  *position += strlen (thous);
	}

      if (*(group_size + 1))
	{
	  group_size++;
	}
    }
}

/*
 * fmt_drop_thousands() -
 * return:
 * string() :
 * position() :
 */
static void
fmt_drop_thousands (ADJ_ARRAY * string, int *position)
{
  const char *thous = local_thousands ();
  int tl = strlen (thous);
  const char *vstring = (const char *) adj_ar_get_buffer (string);
  int maxbytes;
  int nbytes;

  /* Scan value string chars. */
  for (maxbytes = strlen (vstring); maxbytes > 0 && (nbytes = mblen (vstring, maxbytes)); maxbytes -= nbytes)
    {
      /* Next char is thousands separator? */
      if (!strncmp (vstring, thous, tl))
	{
	  /* Yes, remove from value string. */
	  int i = vstring - (const char *) adj_ar_get_buffer (string);
	  adj_ar_remove (string, i, i + tl);
	  if (position && *position > i)
	    {
	      *position -= tl;
	    }
	}
      else
	{
	  /* No, skip to next char. */
	  vstring += nbytes;
	}
    }
}

/*
 * fmt_add_integral() -
 * return:
 * string() :
 * position() :
 * ndigits() :
 * type() :
 * mode() :
 */
static void
fmt_add_integral (ADJ_ARRAY * string, int *position, int ndigits, FORMAT_DIGIT type, FMT_LEX_MODE mode)
{
  int i;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE ttype;
  const char *digit = type == DIGIT_9 ? LOCAL_0 : LOCAL_STAR;
  const char *vstring = (const char *) adj_ar_get_buffer (string);

  assert (type != DIGIT_Z);

  /* Find position of first integral digit. */
  cnv_fmt_analyze (vstring, mode);
  while ((ttype = cnv_fmt_lex (&token)) == FT_MINUS || ttype == FT_PLUS || ttype == FT_CURRENCY);
  i = (cnv_fmt_next_token () - strlen (token.raw_text)) - vstring;

  if (position && *position > i)
    {
      *position += strlen (digit) * ndigits;
    }
  while (ndigits-- > 0)
    {
      adj_ar_insert (string, digit, strlen (digit), i);
    }
}

/*
 * fmt_drop_integral() -
 * return:
 * string() :
 * position() :
 * ndigits() :
 * mode() :
 */
static void
fmt_drop_integral (ADJ_ARRAY * string, int *position, int ndigits, FMT_LEX_MODE mode)
{
  const char *vstring = (const char *) adj_ar_get_buffer (string);
  FMT_TOKEN token;
  FMT_TOKEN_TYPE ttype;
  int i;
  int nd;
  int ndbytes;
  int maxbytes;
  int nbytes;
  const char *sp;

  /* Find position of first integral digit. */
  cnv_fmt_analyze (vstring, mode);
  while ((ttype = cnv_fmt_lex (&token)) == FT_MINUS || ttype == FT_PLUS || ttype == FT_CURRENCY);
  i = (cnv_fmt_next_token () - strlen (token.raw_text)) - vstring;
  vstring += i;

  /* Determine number of bytes to drop. */
  for (nd = 0, sp = vstring, maxbytes = strlen (sp); nd < ndigits && (nbytes = mblen (sp, maxbytes));
       nd++, sp += nbytes, maxbytes -= nbytes);
  ndbytes = sp - vstring;

  /* Drop digit bytes. */
  adj_ar_remove (string, i, i + ndbytes);

  /* Adjust position. */
  if (position && *position > i)
    {
      if (ndbytes > *position - i)
	{
	  ndbytes = *position - i;
	}
      *position -= ndbytes;
    }
}

/*
 * fmt_add_decimal() -
 * return:
 * string() :
 * position() :
 */
static bool
fmt_add_decimal (ADJ_ARRAY * string, int *position)
{
  bool ok;
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  const char *vstring = (const char *) adj_ar_get_buffer (string);

  cnv_fmt_analyze (vstring, FL_LOCAL_NUMBER);
  while ((ttype = cnv_fmt_lex (&token)) == FT_MINUS || ttype == FT_PLUS || ttype == FT_CURRENCY);

  /*
   * Add decimal only if at most one digit already exists. This allows us to
   * automatically add a decimal when interactive input begins, but then reject
   * attempt to drop decimal later (when we wouldn't know where to put it
   * back).
   */
  ok = ttype == FT_NONE || ((ttype == FT_ZEROES || ttype == FT_NUMBER) && intl_mbs_len (token.text) == 1);

  if (ok)
    {
      const char *dp = local_decimal ();
      int insert = cnv_fmt_next_token () - vstring;
      adj_ar_insert (string, dp, strlen (dp), insert);

      if (position && *position > insert)
	{
	  *position += strlen (dp);
	}
    }

  return ok;
}

/*
 * fmt_fraction_position() - Return the position immediately following the
 *     first decimal in the given string after the given start position.
 *     Return ADJ_AR_EOA if string doesn't contain a decimal after the start
 *     position.
 * return:
 * string(in) :
 * start(in) :
 */
static int
fmt_fraction_position (ADJ_ARRAY * string, int start)
{
  int return_pos = (int) ADJ_AR_EOA;
  const char *vstring = (const char *) adj_ar_get_buffer (string);

  if (start >= 0 && start < (int) strlen (vstring))
    {
      FMT_TOKEN_TYPE ttype;
      FMT_TOKEN token;
      int pos;

      cnv_fmt_analyze (vstring + start, FL_LOCAL_NUMBER);

      for (pos = 0, ttype = FT_NONE; (ttype = cnv_fmt_lex (&token)) && ttype != FT_DECIMAL;
	   pos += strlen (token.raw_text));

      if (ttype == FT_DECIMAL)
	{
	  return_pos = start + pos + strlen (token.raw_text);
	}
    }

  return return_pos;
}

/*
 * fmt_decimals() - Return the number of decimals in the given string.
 * return :
 * string(in) :
 */
static int
fmt_decimals (ADJ_ARRAY * string)
{
  int n;
  int p;

  for (n = 0, p = 0; (p = fmt_fraction_position (string, p)) != ADJ_AR_EOA; n++);

  return n;
}

/*
 * fmt_add_fractional() -
 * return:
 * string() :
 * position() :
 * ndigits() :
 * type() :
 */
static void
fmt_add_fractional (ADJ_ARRAY * string, int *position, int ndigits, FORMAT_DIGIT type)
{
  int insert;
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  const char *vstring = (const char *) adj_ar_get_buffer (string);
  const char *digit = type == DIGIT_9 ? LOCAL_0 : LOCAL_STAR;

  assert (type != DIGIT_Z);

  cnv_fmt_analyze (vstring, FL_LOCAL_NUMBER);
  while (cnv_fmt_lex (&token) != FT_DECIMAL);
  while ((ttype = cnv_fmt_lex (&token)) == FT_ZEROES || ttype == FT_NUMBER || ttype == FT_STARS);
  insert = (cnv_fmt_next_token () - strlen (token.raw_text)) - vstring;

  if (position && *position > insert)
    {
      *position += strlen (digit) * ndigits;
    }
  while (ndigits-- > 0)
    {
      adj_ar_insert (string, digit, strlen (digit), insert);
    }
}

/*
 * fmt_drop_fractional() -
 * return:
 * string() :
 * position() :
 * max_digits() :
 */
static void
fmt_drop_fractional (ADJ_ARRAY * string, int *position, int max_digits)
{
  int first;
  int nd;
  int ndbytes;
  int nbytes;
  const char *sp;
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  const char *vstring = (const char *) adj_ar_get_buffer (string);

  /* Find first fractional digit. */
  cnv_fmt_analyze (vstring, FL_LOCAL_NUMBER);
  while (cnv_fmt_lex (&token) != FT_DECIMAL);
  first = cnv_fmt_next_token () - vstring;
  vstring += first;

  /* Find total bytes of fractional digits. */
  while ((ttype = cnv_fmt_lex (&token)) == FT_ZEROES || ttype == FT_NUMBER || ttype == FT_STARS);
  ndbytes = (cnv_fmt_next_token () - strlen (token.raw_text)) - vstring;

  /* Determine number of bytes to drop. */
  for (nd = 0, sp = vstring; nd < max_digits && (nbytes = mblen (sp, ndbytes)); nd++, sp += nbytes, ndbytes -= nbytes);
  first += sp - vstring;

  /* Drop digit bytes. */
  adj_ar_remove (string, first, first + ndbytes);

  if (position && *position > first)
    {
      /* Adjust position. */
      if (ndbytes > *position - first)
	{
	  ndbytes = *position - first;
	}
      *position -= ndbytes;
    }
}

/*
 * fmt_add_currency() -
 * return:
 * string() :
 * position() :
 * mfmt() :
 */
static void
fmt_add_currency (ADJ_ARRAY * string, int *position, MONETARY_FORMAT * mfmt)
{
  int i;
  const char *csymbol;

  if (mfmt->format == CURRENCY_FIRST)
    {
      i = 0;
    }
  else if (mfmt->format == CURRENCY_LAST)
    {
      i = strlen ((char *) adj_ar_get_buffer (string));
    }
  else
    {
      return;
    }

  csymbol = cnv_currency_symbol (mfmt->currency);
  adj_ar_insert (string, csymbol, strlen (csymbol), i);
  if (position && *position >= i)
    {
      *position += strlen (csymbol);
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/* cnvutil_cleanup() -This function cleans up any memory we may have allocated
 *    along the way. These will be the three adj_arrays that can be allocated
 *    by cnv_wcs, cnv_get_string_buffer and cnv_get_value_string_buffer.
 * return : void
 */
static void
cnvutil_cleanup (void)
{
#if defined(SERVER_MODE)
  ADJ_ARRAY *buffer;
  int i;

  for (i = 0; i < 3; i++)
    {
      buffer = cnv_get_thread_local_adj_buffer (i);
      if (buffer)
	{
	  adj_ar_free (buffer);
	}
      cnv_set_thread_local_adj_buffer (i, NULL);
    }

#else

  if (cnv_adj_buffer1 != NULL)
    {
      adj_ar_free (cnv_adj_buffer1);
      cnv_adj_buffer1 = NULL;
    }

  if (cnv_adj_buffer2 != NULL)
    {
      adj_ar_free (cnv_adj_buffer2);
      cnv_adj_buffer2 = NULL;
    }

  if (cnv_adj_buffer3 != NULL)
    {
      adj_ar_free (cnv_adj_buffer3);
      cnv_adj_buffer3 = NULL;
    }
#endif
}


/*
 * fmt_date_string() - Return a string representing the given date,
 *    according to the given format descriptor.
 * return:
 * the_date(in) :
 * descriptor(in) :
 */
static const char *
fmt_date_string (const DB_DATE * the_date, const char *descriptor)
{
  const char *string = NULL;
  int month, day, year;

  assert (mbs_eql (descriptor, "D") || mbs_eql (descriptor, "x") || mbs_eql (descriptor, "E"));

  db_date_decode ((DB_DATE *) the_date, &month, &day, &year);

#if defined (ENABLE_UNUSED_FUNCTION)
  if (mbs_eql (descriptor, "D"))
    {
      static char date_string[FMT_MAX_DATE_STRING * MB_LEN_MAX + 1];

      sprintf (date_string, "%s%s%s%s%s", fmt_month_string (month, "m"), LOCAL_SLASH, fmt_monthday_string (day, "d"),
	       LOCAL_SLASH, fmt_year_string (year, "y"));
      assert (strlen (date_string) < sizeof date_string);

      string = date_string;
    }
  else if (mbs_eql (descriptor, "E"))
    {
      string = local_alt_date_string (month, day, year);
    }
  else
#endif
  if (mbs_eql (descriptor, "x"))
    {
      string = local_date_string (month, day, year);
    }

  return string;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * fmt_year_string() - Return a string representing the year of the given date,
 *    according to the given format descriptor.
 * return:
 * the_date(in) :
 * descriptor(in) :
 */
static const char *
fmt_year_string (int year, const char *descriptor)
{
  static char year_string[8 * MB_LEN_MAX + 1];

  assert (mbs_eql (descriptor, "y") || mbs_eql (descriptor, "Y"));

  if (mbs_eql (descriptor, "y"))
    {
      sprintf (year_string, "%02d", year % 100);
    }
  else
    {
      sprintf (year_string, "%d", year);
    }
  assert (strlen (year_string) < sizeof year_string);

  return year_string;
}

/*
 * fmt_month_string() - Return a string representing the month of the given
 *    date, according to the given format descriptor.
 * return:
 * the_date(in) :
 * descriptor(in) :
 */
static const char *
fmt_month_string (int month, const char *descriptor)
{
  const char *month_string = NULL;

  assert (mbs_eql (descriptor, "b") || mbs_eql (descriptor, "B") || mbs_eql (descriptor, "m"));

  if (mbs_eql (descriptor, "b"))
    {
      month_string = local_short_month_name (month - 1);
    }
  else if (mbs_eql (descriptor, "B"))
    {
      month_string = local_long_month_name (month - 1);
    }
  else				/* if (mbs_eql (descriptor, "m")) */
    {
      static char month_number[2 * MB_LEN_MAX + 1];
      sprintf (month_number, "%02d", month);
      assert (strlen (month_number) < sizeof month_number);
      month_string = month_number;
    }

  return month_string;
}

/*
 * fmt_monthday_string() - Return a string representing the month day of
 *    the given date, according to the given format descriptor.
 * return:
 * the_date(in) :
 * descriptor(in) :
 */
static const char *
fmt_monthday_string (int day, const char *descriptor)
{
  static char day_number[2 * MB_LEN_MAX + 1];

  assert (mbs_eql (descriptor, "d") || mbs_eql (descriptor, "e"));

  sprintf (day_number, mbs_eql (descriptor, "d") ? "%02d" : "%2d", day);
  assert (strlen (day_number) < sizeof day_number);

  return day_number;
}

/*
 * fmt_weekday_string() - Return a string representing the week day of the
 *    given date, according to the given format descriptor.
 * return:
 * the_date(in) :
 * descriptor(in) :
 */
static const char *
fmt_weekday_string (int weekday, const char *descriptor)
{
  const char *day_string = NULL;

  assert (mbs_eql (descriptor, "a") || mbs_eql (descriptor, "A") || mbs_eql (descriptor, "w"));

  if (mbs_eql (descriptor, "a"))
    {
      day_string = local_short_weekday_name (weekday);
    }
  else if (mbs_eql (descriptor, "A"))
    {
      day_string = local_long_weekday_name (weekday);
    }
  else				/* if (mbs_eql (descriptor, "w")) */
    {
      static char day_number[MB_LEN_MAX + 1];
      sprintf (day_number, "%d", weekday);
      assert (strlen (day_number) < sizeof day_number);
      day_string = day_number;
    }

  return day_string;
}
#endif

/*
 * fmt_date_value() - Scan tokens and parse a date value according to given
 *    format descriptor. If a valid value can't be found, then return an error
 *    condition. otherwise, set the_month, the_day, and the_year to the
 *    value found.
 * return:
 * descriptor(in) :
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 */
static int
fmt_date_value (const char *descriptor, int *the_month, int *the_day, int *the_year)
{
  bool bad_value = false;
  int error = 0;

  assert (mbs_eql (descriptor, "D") || mbs_eql (descriptor, "x") || mbs_eql (descriptor, "E"));

#if defined (ENABLE_UNUSED_FUNCTION)
  if (mbs_eql (descriptor, "D"))
    {
      do
	{
	  FMT_TOKEN token;

	  error = fmt_month_value ("m", the_month);
	  if (error)
	    {
	      break;
	    }

	  bad_value = cnv_fmt_lex (&token) != FT_DATE_SEPARATOR;
	  if (bad_value)
	    {
	      break;
	    }

	  error = fmt_monthday_value ("d", the_day);
	  if (error)
	    {
	      break;
	    }

	  bad_value = cnv_fmt_lex (&token) != FT_DATE_SEPARATOR;
	  if (bad_value)
	    {
	      break;
	    }

	  error = fmt_year_value ("y", the_year);
	}
      while (0);
    }

  else
#endif

  if (mbs_eql (descriptor, "x"))
    {
      error = local_date_value (the_month, the_day, the_year);
    }

#if defined (ENABLE_UNUSED_FUNCTION)
  else if (mbs_eql (descriptor, "E"))
    {
      error = local_alt_date_value (the_month, the_day, the_year);
    }
#endif

  if (bad_value)
    {
      error = CNV_ERR_BAD_DATE;
      co_signal (error, CNV_ER_FMT_BAD_DATE, descriptor);
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * fmt_year_value() - Scan tokens and parse a year value according to given
 *    format descriptor. If a valid value can't be found, then return an error
 *    condition. otherwise, set the_year to the value found.
 * return:
 * descriptor(in) :
 * the_year(in/out) :
 *
 * note : if the string represents the year only within the century
 *   (i.e. 00-99), the input value of the_year is used to determine the
 *   century.
 */
static int
fmt_year_value (const char *descriptor, int *the_year)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  assert (mbs_eql (descriptor, "y") || mbs_eql (descriptor, "Y"));

  if (mbs_eql (descriptor, "Y"))
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY) || (*the_year = atoi (token.text)) == 0;
    }

  else if (mbs_eql (descriptor, "y"))
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS);
      if (!bad_value)
	{
	  int yr = atoi (token.text);
	  *the_year = (*the_year / 100) * 100 + (*the_year > 0 ? yr : -yr);
	}
    }

  if (bad_value)
    {
      error = CNV_ERR_BAD_YEAR;
      co_signal (error, CNV_ER_FMT_BAD_YEAR, descriptor);
    }

  return error;
}

/*
 * fmt_month_value() - set into the_month out variable with the month (1-12)
 *    represented by the given string and format descriptor.
 *    If a format error occurs return error code.
 * return: error code
 * descriptor(in) :
 * the_month(out) :
 */
static int
fmt_month_value (const char *descriptor, int *the_month)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  assert (mbs_eql (descriptor, "b") || mbs_eql (descriptor, "B") || mbs_eql (descriptor, "m"));

  if (mbs_eql (descriptor, "b"))
    {
      type = cnv_fmt_lex (&token);

      /* Careful: long/short names for May are the same! */
      bad_value = !(type == FT_MONTH || (type == FT_MONTH_LONG && token.value == 5));
      if (!bad_value)
	{
	  *the_month = token.value;
	}
    }

  else if (mbs_eql (descriptor, "B"))
    {
      bad_value = cnv_fmt_lex (&token) != FT_MONTH_LONG;
      if (!bad_value)
	{
	  *the_month = token.value;
	}
    }

  else if (mbs_eql (descriptor, "m"))
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS) || (*the_month = atoi (token.text)) > 12
	|| *the_month < 1;
    }

  if (bad_value)
    {
      error = CNV_ERR_BAD_MONTH;
      co_signal (error, CNV_ER_FMT_BAD_MONTH, descriptor);
    }
  return error;
}

/*
 * fmt_monthday_value() - Scan tokens and parse a monthday value (1-31)
 *    according to given format descriptor. If a valid value can't be
 *    found, then return an error condition.
 *    otherwise, set the_day to the value found.
 * return:
 * descriptor(in) :
 * the_day(out) :
 */
static int
fmt_monthday_value (const char *descriptor, int *the_day)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  assert (mbs_eql (descriptor, "d") || mbs_eql (descriptor, "e"));

  type = cnv_fmt_lex (&token);
  if (!(type == FT_TIME_DIGITS || type == (mbs_eql (descriptor, "d") ? FT_TIME_DIGITS_0 : FT_TIME_DIGITS_BLANK))
      || (*the_day = atoi (token.text)) > 31 || *the_day < 1)
    {
      error = CNV_ERR_BAD_MDAY;
      co_signal (error, CNV_ER_FMT_BAD_MDAY, descriptor);
    }

  return error;
}

/*
 * fmt_weekday_value() - Scan tokens and parse a weekday value (0-6) according
 *    to given format descriptor. If a valid value can't be found, then return
 *    an error condition. otherwise, set the_day to the value found.
 * return:
 * descriptor(in) :
 * the_day(out) :
 */
static int
fmt_weekday_value (const char *descriptor, int *the_day)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;

  assert (mbs_eql (descriptor, "a") || mbs_eql (descriptor, "A") || mbs_eql (descriptor, "w"));

  if (mbs_eql (descriptor, "a"))
    {
      bad_value = cnv_fmt_lex (&token) != FT_WEEKDAY;
      if (!bad_value)
	{
	  *the_day = token.value;
	}
    }

  else if (mbs_eql (descriptor, "A"))
    {
      bad_value = cnv_fmt_lex (&token) != FT_WEEKDAY_LONG;
      if (!bad_value)
	{
	  *the_day = token.value;
	}
    }

  else if (mbs_eql (descriptor, "w"))
    {
      bad_value = cnv_fmt_lex (&token) != FT_TIME_DIGITS_ANY || (*the_day = atoi (token.text)) > 6;
    }

  if (bad_value)
    {
      error = CNV_ERR_BAD_WDAY;
      co_signal (error, CNV_ER_FMT_BAD_WDAY, descriptor);
    }
  return error;
}

/*
 * fmt_weekday_date() - Return the date for the given weekday that is in the
 *    same week as the given month/day/year. For example, if the given date
 *    is 11/5/93 and the given weekday is Monday, then the date returned will
 *    be 11/1/93.
 * return:
 * month(in) :
 * day(in) :
 * year(in) :
 * weekday(in) :
 */
static DB_DATE
fmt_weekday_date (int month, int day, int year, int weekday)
{
  DB_DATE new_date;
  db_date_encode (&new_date, month, day, year);

  return new_date - db_date_weekday (&new_date) + weekday;
}
#endif

/*
 * fmt_time_string() - Return a string representing the given time, according
 *    to the given format descriptor.
 * return:
 * the_time(in) :
 * descriptor(in) :
 */
static const char *
fmt_time_string (const DB_TIME * the_time, const char *descriptor)
{
#if defined (ENABLE_UNUSED_FUNCTION)
  static char time_string[FMT_MAX_TIME_STRING * MB_LEN_MAX + 1];
#endif
  const char *string = NULL;

  assert (mbs_eql (descriptor, "R") || mbs_eql (descriptor, "r") || mbs_eql (descriptor, "T")
	  || mbs_eql (descriptor, "X"));

#if defined (ENABLE_UNUSED_FUNCTION)
  if (mbs_eql (descriptor, "T"))
    {
      sprintf (time_string, "%s%s%s%s%s", fmt_hour_string (the_time, "H"), LOCAL_COLON,
	       fmt_minute_string (the_time, "M"), LOCAL_COLON, fmt_second_string (the_time, "S"));
      assert (strlen (time_string) < sizeof time_string);
      string = time_string;
    }
  else
#endif
  if (mbs_eql (descriptor, "X"))
    {
      string = local_time_string (the_time);
    }
#if defined (ENABLE_UNUSED_FUNCTION)
  else if (mbs_eql (descriptor, "r"))
    {
      sprintf (time_string, "%s%s%s%s%s%s%s", fmt_hour_string (the_time, "I"), LOCAL_COLON,
	       fmt_minute_string (the_time, "M"), LOCAL_COLON, fmt_second_string (the_time, "S"), LOCAL_SPACE,
	       local_am_pm_string (the_time));
      assert (strlen (time_string) < sizeof time_string);
      string = time_string;
    }

  else if (mbs_eql (descriptor, "R"))
    {
      sprintf (time_string, "%s%s%s", fmt_hour_string (the_time, "H"), LOCAL_COLON, fmt_minute_string (the_time, "M"));
      assert (strlen (time_string) < sizeof time_string);
      string = time_string;
    }
#endif
  return string;
}

/*
 * fmt_time_value() - Scan tokens and parse a time value according to given
 *    format descriptor. If a valid value can't be found, then return an error
 *    condition. otherwise, set the_hour, the_min, and the_sec to the
 *    value found.
 * return:
 * descriptor(in) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
fmt_time_value (const char *descriptor, int *the_hour, int *the_min, int *the_sec)
{
  bool bad_value = false;
  int error = 0;
#if defined (ENABLE_UNUSED_FUNCTION)
  FMT_TOKEN token;
#endif

  assert (mbs_eql (descriptor, "R") || mbs_eql (descriptor, "r") || mbs_eql (descriptor, "T")
	  || mbs_eql (descriptor, "X"));

#if defined (ENABLE_UNUSED_FUNCTION)
  if (mbs_eql (descriptor, "T"))
    {
      do
	{
	  error = fmt_hour_value ("H", the_hour);
	  if (error)
	    {
	      break;
	    }

	  bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
	  if (bad_value)
	    {
	      break;
	    }

	  error = fmt_minute_value ("M", the_min);
	  if (error)
	    {
	      break;
	    }

	  bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
	  if (bad_value)
	    {
	      break;
	    }

	  error = fmt_second_value ("S", the_sec);
	}
      while (0);
    }

  else
#endif
  if (mbs_eql (descriptor, "X"))
    {
      error = local_time_value (the_hour, the_min, the_sec);
    }

#if defined (ENABLE_UNUSED_FUNCTION)
  else if (mbs_eql (descriptor, "r"))
    {
      do
	{
	  bool pm;

	  error = fmt_hour_value ("I", the_hour);
	  if (error)
	    {
	      break;
	    }

	  bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
	  if (bad_value)
	    {
	      break;
	    }

	  error = fmt_minute_value ("M", the_min);
	  if (error)
	    {
	      break;
	    }

	  bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
	  if (bad_value)
	    {
	      break;
	    }

	  error = fmt_second_value ("S", the_sec);
	  if (error)
	    {
	      break;
	    }

	  /* Skip blank "pattern" character. */
	  bad_value = strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH);
	  if (bad_value)
	    {
	      break;
	    }
	  cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_LOCAL_TIME);

	  error = local_am_pm_value (&pm);
	  if (error)
	    {
	      break;
	    }

	  *the_hour %= 12;
	  if (pm)
	    *the_hour += 12;
	}
      while (0);
    }

  else if (mbs_eql (descriptor, "R"))
    {
      do
	{
	  error = fmt_hour_value ("H", the_hour);
	  if (error)
	    {
	      break;
	    }

	  bad_value = cnv_fmt_lex (&token) != FT_TIME_SEPARATOR;
	  if (bad_value)
	    {
	      break;
	    }

	  error = fmt_minute_value ("M", the_min);
	}
      while (0);
    }
#endif

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIME;
      co_signal (error, CNV_ER_FMT_BAD_TIME, descriptor);
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * fmt_hour_string() - Return a string representing the hour of the given time,
 *    according to the given format descriptor.
 * return:
 * the_time(in) :
 * descriptor(in) :
 */
static const char *
fmt_hour_string (const DB_TIME * the_time, const char *descriptor)
{
  static char hour_string[2 * MB_LEN_MAX + 1];
  int hour, min, sec;

  assert (mbs_eql (descriptor, "H") || mbs_eql (descriptor, "I") || mbs_eql (descriptor, "k")
	  || mbs_eql (descriptor, "l"));

  db_time_decode ((DB_TIME *) the_time, &hour, &min, &sec);

  sprintf (hour_string, mbs_eql (descriptor, "k")
	   || mbs_eql (descriptor, "l") ? "%2d" : "%02d", mbs_eql (descriptor, "I")
	   || mbs_eql (descriptor, "l") ? (hour ? hour % 12 : 12) : hour);
  assert (strlen (hour_string) < sizeof hour_string);

  return hour_string;
}

/*
 * fmt_hour_value() - Scan tokens and parse an hour value according to given
 *    format descriptor. If a valid value can't be found, then return an error
 *    condition. otherwise, set the_hour to the value found.
 * return:
 * descriptor(in) :
 * the_hour(out) :
 */
static int
fmt_hour_value (const char *descriptor, int *the_hour)
{
  bool bad_value = false;
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  assert (mbs_eql (descriptor, "H") || mbs_eql (descriptor, "I") || mbs_eql (descriptor, "k")
	  || mbs_eql (descriptor, "l"));

  if (mbs_eql (descriptor, "H"))
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS) || (*the_hour = atoi (token.text)) > 23;
    }

  else if (mbs_eql (descriptor, "I"))
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS) || (*the_hour = atoi (token.text)) > 12
	|| *the_hour < 1;
    }

  else if (mbs_eql (descriptor, "k"))
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_BLANK || type == FT_TIME_DIGITS) || (*the_hour = atoi (token.text)) > 23;
    }

  else if (mbs_eql (descriptor, "l"))
    {
      type = cnv_fmt_lex (&token);
      bad_value = !(type == FT_TIME_DIGITS_BLANK || type == FT_TIME_DIGITS) || (*the_hour = atoi (token.text)) > 12
	|| *the_hour < 1;
    }

  if (bad_value)
    {
      error = CNV_ERR_BAD_HOUR;
      co_signal (error, CNV_ER_FMT_BAD_HOUR, descriptor);
    }

  return error;
}

/*
 * fmt_minute_string() - Return a string representing the minute of the given
 *    time, according to the given format descriptor.
 * return:
 * the_time(in) :
 * descriptor(in) :
 */
static const char *
fmt_minute_string (const DB_TIME * the_time, const char *descriptor)
{
  static char min_string[2 * MB_LEN_MAX + 1];
  int hour, min, sec;

  assert (mbs_eql (descriptor, "M"));

  db_time_decode ((DB_TIME *) the_time, &hour, &min, &sec);

  sprintf (min_string, "%02d", min);
  assert (strlen (min_string) < sizeof min_string);

  return min_string;
}
#endif

/*
 * fmt_minute_value() - Scan tokens and parse a minute value according
 *    to given format descriptor. If a valid value can't be found, then
 *    return an error condition. otherwise, set the_min to the value found.
 * return:
 * descriptor(in) :
 * the_min(out) :
 */
static int
fmt_minute_value (const char *descriptor, int *the_min)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  assert (mbs_eql (descriptor, "M"));

  type = cnv_fmt_lex (&token);
  if (!(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY)
      || (*the_min = atoi (token.text)) > 59)
    {
      error = CNV_ERR_BAD_MIN;
      co_signal (error, CNV_ER_FMT_BAD_MIN, descriptor);
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * fmt_second_string() - Return a string representing the second of the given
 *    time, according to the given format descriptor.
 * return:
 * the_time(in) :
 * descriptor(in) :
 */
static const char *
fmt_second_string (const DB_TIME * the_time, const char *descriptor)
{
  static char sec_string[2 * MB_LEN_MAX + 1];
  int hour, min, sec;

  assert (mbs_eql (descriptor, "S"));

  db_time_decode ((DB_TIME *) the_time, &hour, &min, &sec);

  sprintf (sec_string, "%02d", sec);
  assert (strlen (sec_string) < sizeof sec_string);

  return sec_string;
}
#endif

/*
 * fmt_second_value() - Scan tokens and parse a seconds value according
 *    to given format descriptor. If a valid value can't be found, then
 *    return an error condition. otherwise, set the_sec to the value found.
 * return:
 * descriptor(in) :
 * the_sec(out) :
 */
static int
fmt_second_value (const char *descriptor, int *the_sec)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  assert (mbs_eql (descriptor, "S"));

  type = cnv_fmt_lex (&token);
  if (!(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY)
      || (*the_sec = atoi (token.text)) > 59)
    {
      error = CNV_ERR_BAD_SEC;
      co_signal (error, CNV_ER_FMT_BAD_SEC, descriptor);
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * fmt_millisecond_value() - Scan tokens and parse a seconds value according
 *    to given format descriptor. If a valid value can't be found, then
 *    return an error condition. otherwise, set the_sec to the value found.
 * return:
 * descriptor(in) :
 * the_sec(out) :
 */
static int
fmt_millisecond_value (const char *descriptor, int *the_msec)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  assert (mbs_eql (descriptor, "MS"));

  type = cnv_fmt_lex (&token);
  if (!(type == FT_TIME_DIGITS_0 || type == FT_TIME_DIGITS || type == FT_TIME_DIGITS_ANY)
      || (*the_msec = atoi (token.text)) > 999)
    {
      error = CNV_ERR_BAD_MSEC;
      co_signal (error, CNV_ER_FMT_BAD_MSEC, descriptor);
    }

  return error;
}
#endif

/*
 * fmt_timestamp_string() - Return a string representing the given timestamp,
 *    according to the given format descriptor.
 * return:
 * the_timestamp(in) :
 * descriptor(in) :
 */
static const char *
fmt_timestamp_string (const DB_TIMESTAMP * the_timestamp, const char *descriptor)
{
  DB_DATE the_date;
  DB_TIME the_time;
  const char *string = NULL;

  assert (mbs_eql (descriptor, "c") || mbs_eql (descriptor, "C"));

  (void) db_timestamp_decode_ses ((DB_TIMESTAMP *) the_timestamp, &the_date, &the_time);

  if (mbs_eql (descriptor, "c"))
    {
      static char timestamp_string[FMT_MAX_TIMESTAMP_STRING * MB_LEN_MAX + 1];

      sprintf (timestamp_string, "%s%s%s", fmt_date_string (&the_date, "x"), LOCAL_SPACE,
	       fmt_time_string (&the_time, "X"));
      assert (strlen (timestamp_string) < (int) sizeof (timestamp_string));
      string = timestamp_string;
    }
#if defined (ENABLE_UNUSED_FUNCTION)
  else if (mbs_eql (descriptor, "C"))
    {
      string = local_timestamp_string (the_timestamp);
    }
#endif
  return string;
}

/*
 * fmt_timestamp_value() - Scan tokens and parse timestamp value according
 *    to given format descriptor. If a valid value can't be found, then return
 *    an error condition. otherwise, set the_day, the_month, the_year, the_hour
 *   (0-23), the_min, and the_sec to the value found.
 * return:
 * descriptor(in) :
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
fmt_timestamp_value (const char *descriptor, int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min,
		     int *the_sec)
{
  bool bad_value = false;
  int error = 0;

  assert (mbs_eql (descriptor, "c") || mbs_eql (descriptor, "C"));

  if (mbs_eql (descriptor, "c"))
    {
      do
	{
	  error = fmt_date_value ("x", the_month, the_day, the_year);
	  if (error)
	    {
	      break;
	    }

	  /* Skip blank "pattern" character. */
	  bad_value = strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH);
	  if (bad_value)
	    {
	      break;
	    }
	  cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_LOCAL_TIME);

	  error = fmt_time_value ("X", the_hour, the_min, the_sec);
	}
      while (0);
    }

#if defined (ENABLE_UNUSED_FUNCTION)
  else if (mbs_eql (descriptor, "C"))
    {
      error = local_timestamp_value (the_month, the_day, the_year, the_hour, the_min, the_sec);
    }
#endif

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIMESTAMP;
      co_signal (error, CNV_ER_FMT_BAD_TIMESTAMP, descriptor);
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * fmt_datetime_string() -
 * return:
 * the_timestamp(in) :
 * descriptor(in) :
 */
static const char *
fmt_datetime_string (const DB_DATETIME * the_datetime, const char *descriptor)
{
  int months, days, years;
  int hours, minutes, seconds, milliseconds;
  const char *string = NULL;

  assert (mbs_eql (descriptor, "c") || mbs_eql (descriptor, "C"));

  db_datetime_decode ((DB_DATETIME *) the_datetime, &months, &days, &years, &hours, &minutes, &seconds, &milliseconds);

  if (mbs_eql (descriptor, "c"))
    {
      static char datetime_string[FMT_MAX_DATETIME_STRING * MB_LEN_MAX + 1];

      sprintf (datetime_string, "%d/%d/%d %d:%d:%d.%d", months, days, years, hours, minutes, seconds, milliseconds);
      string = datetime_string;
    }
  else if (mbs_eql (descriptor, "C"))
    {
      string = local_datetime_string (the_datetime);
    }

  return string;
}

/*
 * fmt_timestamp_value() - Scan tokens and parse timestamp value according
 *    to given format descriptor. If a valid value can't be found, then return
 *    an error condition. otherwise, set the_day, the_month, the_year, the_hour
 *   (0-23), the_min, and the_sec to the value found.
 * return:
 * descriptor(in) :
 * the_month(out) :
 * the_day(out) :
 * the_year(out) :
 * the_hour(out) :
 * the_min(out) :
 * the_sec(out) :
 */
static int
fmt_datetime_value (const char *descriptor, int *the_month, int *the_day, int *the_year, int *the_hour, int *the_min,
		    int *the_sec, int *the_msec)
{
  bool bad_value = false;
  int error = 0;
  assert (mbs_eql (descriptor, "c") || mbs_eql (descriptor, "C"));
  if (mbs_eql (descriptor, "c"))
    {
      do
	{
	  error = fmt_date_value ("x", the_month, the_day, the_year);
	  if (error)
	    {
	      break;
	    }

	  /* Skip blank "pattern" character. */
	  bad_value = strncmp (cnv_fmt_next_token (), LOCAL_SPACE, LOCAL_SPACE_LENGTH);
	  if (bad_value)
	    {
	      break;
	    }
	  cnv_fmt_analyze (cnv_fmt_next_token () + LOCAL_SPACE_LENGTH, FL_LOCAL_TIME);
	  error = fmt_time_value ("X", the_hour, the_min, the_sec);
	}
      while (0);
    }

  else if (mbs_eql (descriptor, "C"))
    {
      error = local_timestamp_value (the_month, the_day, the_year, the_hour, the_min, the_sec);
    }

  if (bad_value)
    {
      error = CNV_ERR_BAD_TIMESTAMP;
      co_signal (error, CNV_ER_FMT_BAD_TIMESTAMP, descriptor);
    }

  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * tfmt_new() - Converts a date/time/timestamp format string into an array of
 *   tokens. The token array is terminated by a token of type FT_NONE.
 * return:
 * format(in) :
 */
static const FMT_TOKEN *
tfmt_new (const char *format)
{
  static ADJ_ARRAY *tokens = NULL;
  static ADJ_ARRAY *strings = NULL;

  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  FMT_TOKEN *t;
  const char *string;

  assert (format);

  /*
   * Initialize arrays for tokens and token strings. We must copy all token
   * strings, because the token.text pointer is reused when the next token
   * is scanned.
   */
  if (!tokens)
    {
      tokens = adj_ar_new (sizeof (FMT_TOKEN), 2, 2.0);
      strings = adj_ar_new (1, 0, 2.0);
    }
  adj_ar_remove (tokens, 0, ADJ_AR_EOA);
  adj_ar_remove (strings, 0, ADJ_AR_EOA);

  /* Scan format string for tokens. */
  for (cnv_fmt_analyze (format, FL_TIME_FORMAT);
       (type = cnv_fmt_lex (&token), adj_ar_append (tokens, &token, 1), type != FT_NONE);)
    {
      /* Preserve a copy of token string. */
      adj_ar_append (strings, token.text, token.length + 1);
    }

  /* Update tokens to point to string copies. */
  for (t = (FMT_TOKEN *) adj_ar_get_buffer (tokens), string = (const char *) adj_ar_get_buffer (strings);
       t->type != FT_NONE; string += t->length + 1, t++)
    {
      t->text = string;
    }

  return (FMT_TOKEN *) adj_ar_get_buffer (tokens);
}

/*
 * Float Format Descriptor Functions
 */

/*
 * ffmt_valid_char() - Return true if token chars can legally appear in
 *    a float value string.
 * return:
 * token(in) :
 */
static bool
ffmt_valid_char (FMT_TOKEN * token)
{
  FMT_TOKEN_TYPE type = token->type;

  return type == FT_NUMBER || type == FT_ZEROES || type == FT_MINUS || type == FT_PLUS || type == FT_STARS
    || type == FT_DECIMAL || type == FT_THOUSANDS || mbs_eql (token->text, local_thousands ());
}

/*
 * ffmt_new() - Initialize a descriptor for the given float format string.
 * return:
 * ffmt(in) :
 * format(in) :
 */
static void
ffmt_new (FLOAT_FORMAT * ffmt, const char *format)
{
  wchar_t idc[3];
  const wchar_t *wfmt = cnv_wcs (format);
  const wchar_t *fraction_part = wcsstr (wfmt, FMT_DECIMAL ());
  const wchar_t *integer_part = wcsstr (wfmt, FMT_PLUS ());

  ffmt->scientific = wcsstr (wfmt, FMT_SCIENTIFIC ()) != NULL;
  ffmt->thousands = wcsstr (wfmt, FMT_THOUSANDS ()) != NULL;

  if (fraction_part)
    {
      fraction_part++;
    }
  ffmt->decimal = (fraction_part != NULL || !wcslen (wfmt));

  ffmt->fractional_type = DIGIT_Z;
  ffmt->fractional_digits = (fraction_part ? wcsspn (fraction_part, FMT_Z ()) : 0);
  if (fraction_part && !ffmt->fractional_digits)
    {
      ffmt->fractional_digits = wcsspn (fraction_part, FMT_9 ());
      if (ffmt->fractional_digits)
	{
	  ffmt->fractional_type = DIGIT_9;
	}
      else
	{
	  ffmt->fractional_digits = wcsspn (fraction_part, FMT_STAR ());
	  if (ffmt->fractional_digits)
	    {
	      ffmt->fractional_type = DIGIT_STAR;
	    }
	  else
	    {
	      ffmt->fractional_type = DIGIT_Z;
	    }
	}
    }

  integer_part = integer_part ? integer_part + 1 : wfmt;
  ffmt->sign_required = integer_part != wfmt;

  ffmt->integral_type = DIGIT_Z;
  ffmt->integral_digits = wcsspn (integer_part, WCSCAT (idc, FMT_Z (), FMT_THOUSANDS ())) - ffmt->thousands;
  if (ffmt->integral_digits <= 0)
    {
      ffmt->integral_digits = wcsspn (integer_part, WCSCAT (idc, FMT_9 (), FMT_THOUSANDS ())) - ffmt->thousands;
      if (ffmt->integral_digits > 0)
	{
	  ffmt->integral_type = DIGIT_9;
	}
      else
	{
	  ffmt->integral_digits = wcsspn (integer_part, WCSCAT (idc, FMT_STAR (), FMT_THOUSANDS ())) - ffmt->thousands;
	  if (ffmt->integral_digits > 0)
	    {
	      ffmt->integral_type = DIGIT_STAR;
	    }
	  else
	    {
	      ffmt->integral_type = DIGIT_Z;
	    }
	}
    }
}

/*
 * ffmt_value() - Get the double value represented by the value string.
 *    Return a pointer to the first char of the string after the last value
 *    char. If an error occurs, then the value is unchanged and NULL is
 *    returned.
 * return:
 * ffmt(in) :
 * string(in) :
 * the_double(out) :
 */
static const char *
ffmt_value (FLOAT_FORMAT * ffmt, const char *string, double *the_double)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  bool decimal_missing = false;

  cnv_fmt_analyze (string, FL_LOCAL_NUMBER);

  do
    {
      /* Get value of integer part. */
      error =
	fmt_integral_value (ffmt->integral_type, ffmt->integral_digits, ffmt->sign_required, ffmt->thousands,
			    the_double);

      if (error && error != CNV_ERR_MISSING_INTEGER)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);

      if (ffmt->decimal)
	{
	  /* Decimal point found? */
	  decimal_missing = (type != FT_DECIMAL);
	  if (!decimal_missing)
	    {
	      double fraction_part;

	      if (error)
		{
		  break;	/* integer digit really missing. */
		}

	      /* Yes, get value of fraction part. */
	      error = fmt_fractional_value (ffmt->fractional_type, ffmt->fractional_digits, &fraction_part);

	      if (error && error != CNV_ERR_MISSING_FRACTION)
		{
		  break;
		}

	      if (*the_double < 0.0)
		{
		  fraction_part = -fraction_part;
		}
	      *the_double += fraction_part;
	      type = cnv_fmt_lex (&token);

	      /* WARNING: must handle scientific format here. */
	    }
	}

      if (type != FT_NONE)
	{
	  /* Invalid chars at the end. */
	  error = cnv_bad_char (token.raw_text, !ffmt_valid_char (&token));
	  break;
	}

      if (decimal_missing)
	{
	  error = CNV_ERR_NO_DECIMAL;
	  co_signal (CNV_ERR_NO_DECIMAL, CNV_ER_FMT_NO_DECIMAL);
	  break;
	}
    }
  while (0);
  return error ? NULL : cnv_fmt_next_token ();
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ffmt_print() - Change the given string to a representation of the given
 *    double value in the given format.  if max_size is not long enough to
 *    contain the new double string, then an error is returned.
 * return:
 * ffmt(in) :
 * the_double(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *    the string (including final '\0' char)
 */
static int
ffmt_print (FLOAT_FORMAT * ffmt, double the_double, char *string, int max_size)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  double the_value;
  bool unlimited_fraction = ffmt->decimal && !ffmt->fractional_digits;
  int max_digits = (!ffmt->integral_digits
		    || unlimited_fraction) ? FMT_MAX_DIGITS : ffmt->integral_digits + ffmt->fractional_digits;
  DB_C_INT nchars = (the_double < 0.0
		     || ffmt->sign_required) + ffmt->decimal + max_digits + (unlimited_fraction ? FLT_DIG : 0) +
    (ffmt->scientific ? strlen (LOCAL_EXP_LENGTH) : 0);
  /* Create print format string. */
  const char *fmt_sign = ffmt->sign_required ? "+" : "";
  const char *fmt_zeroes = ffmt->integral_type != DIGIT_Z ? "0" : "";
  const char *fmt_type = ffmt->scientific ? "E" : "f";
  const char *fmt_precision = unlimited_fraction ? "*" : "*.*";
  const char *fmt = adj_ar_concat_strings ("%", fmt_sign, fmt_zeroes,
					   fmt_precision, fmt_type, NULL);
  /* Print undecorated value string. */
  ADJ_ARRAY *buffer = cnv_get_string_buffer (nchars - max_digits + FMT_MAX_DIGITS);
  if (unlimited_fraction)
    {
      sprintf ((char *) adj_ar_get_buffer (buffer), fmt, nchars, the_double);
    }
  else
    {
      sprintf ((char *) adj_ar_get_buffer (buffer), fmt, nchars, (DB_C_INT) (ffmt->fractional_digits), the_double);
    }

  /* Trim leading blanks... */
  if (!ffmt->integral_digits || ffmt->integral_type == DIGIT_Z)
    {
      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while (cnv_fmt_lex (&token) == FT_UNKNOWN && mbs_eql (token.text, LOCAL_SPACE));
      adj_ar_remove (buffer, 0, (int) (cnv_fmt_next_token () - token.length - (char *) adj_ar_get_buffer (buffer)));
    }

  /* ...or replace with leading stars. */
  else if (ffmt->integral_type == DIGIT_STAR)
    {
      int start;
      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while ((type = cnv_fmt_lex (&token)) == FT_MINUS || type == FT_PLUS);
      start = (int) (cnv_fmt_next_token () - token.length - (const char *) adj_ar_get_buffer (buffer));
      if (type == FT_ZEROES)
	{
	  int nzeroes;
	  int sl = LOCAL_STAR_LENGTH;
	  adj_ar_remove (buffer, start, start + token.length);
	  for (nzeroes = intl_mbs_len (token.text); nzeroes > 0; nzeroes--)
	    {
	      adj_ar_insert (buffer, LOCAL_STAR, sl, start);
	    }
	}
    }

  /* Replace trailing zeroes. */
  if (ffmt->decimal && ffmt->fractional_type != DIGIT_9)
    {
      int start;
      int length;
      int nzeroes;
      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while (cnv_fmt_lex (&token) != FT_DECIMAL);
      for (start = 0, nzeroes = 0, length = 0, type = cnv_fmt_lex (&token); type == FT_NUMBER || type == FT_ZEROES;
	   type = cnv_fmt_lex (&token))
	{
	  if (type == FT_ZEROES)
	    {
	      start = (int) (cnv_fmt_next_token () - token.length - (const char *) adj_ar_get_buffer (buffer));
	      length = token.length;
	      nzeroes = intl_mbs_len (token.text);
	    }
	}

      if (nzeroes > 0)
	{
	  adj_ar_remove (buffer, start, start + length);
	  if (ffmt->fractional_type == DIGIT_STAR)
	    {
	      int sl = LOCAL_STAR_LENGTH;
	      for (; nzeroes > 0; nzeroes--)
		{
		  adj_ar_insert (buffer, LOCAL_STAR, sl, start);
		}
	    }
	}
    }

  /* Add thousands separators. */
  if (ffmt->thousands)
    {
      fmt_add_thousands (buffer, NULL);
    }

  /* Too many digits? */
  if (!ffmt_value (ffmt, (const char *) adj_ar_get_buffer (buffer), &the_value))
    {
      error = co_code ();
    }

  /* Enough room to copy completed value string? */
  else if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }

  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
    }

  return error;
}
#endif

/* Monetary Format Descriptor Functions */

/*
 * mfmt_valid_char() - Return true if token chars can legally appear in
 *    a monetary value string.
 * return:
 * token(in) :
 */
static bool
mfmt_valid_char (FMT_TOKEN * token)
{
  FMT_TOKEN_TYPE type = token->type;

  return type == FT_CURRENCY || type == FT_NUMBER || type == FT_ZEROES || type == FT_MINUS || type == FT_STARS
    || type == FT_DECIMAL || type == FT_THOUSANDS || mbs_eql (token->text, local_thousands ());
}

/*
 * mfmt_new() - Initialize a descriptor for the given monetary format string.
 * return: void
 * mfmt(out) :
 * format(in) :
 * currency_type(in) :
 */
static void
mfmt_new (MONETARY_FORMAT * mfmt, const char *format, DB_CURRENCY currency_type)
{
  wchar_t idc[3];
  const wchar_t *wfmt = cnv_wcs (format);
  const wchar_t *fraction_part = wcsstr (wfmt, FMT_DECIMAL ());
  const wchar_t *currency_part = wcsstr (wfmt, FMT_CURRENCY ());
  const wchar_t *integer_part;

  mfmt->currency = currency_type;
  mfmt->mode = cnv_fmt_number_mode (cnv_currency_zone (currency_type));

  if (wcs_eql (wfmt, FMT_CURRENCY ()) || !wcslen (wfmt))
    {
      mfmt->format = CURRENCY_FIRST;
    }
  else if (currency_part == NULL)
    {
      mfmt->format = CURRENCY_NONE;
    }
  else if (currency_part == wfmt)
    {
      mfmt->format = CURRENCY_FIRST;
    }
  else
    {
      mfmt->format = CURRENCY_LAST;
    }

  mfmt->thousands = wcsstr (wfmt, FMT_THOUSANDS ()) != NULL;

  if (mfmt->format == CURRENCY_FIRST && wcslen (wfmt) && currency_part != NULL)
    {
      integer_part = currency_part + 1;
    }
  else
    {
      integer_part = wfmt;
    }

  if (fraction_part)
    {
      fraction_part++;
    }

  mfmt->decimal = (fraction_part != NULL || !wcslen (integer_part));
  mfmt->fractional_type = DIGIT_Z;

  if (fraction_part)
    {
      mfmt->fractional_digits = wcsspn (fraction_part, FMT_Z ());
    }
  else if (mfmt->decimal)
    {
      mfmt->fractional_type = DIGIT_9;
      mfmt->fractional_digits = 2;
    }
  else
    {
      mfmt->fractional_digits = 0;
    }

  if (fraction_part && !mfmt->fractional_digits)
    {
      mfmt->fractional_digits = wcsspn (fraction_part, FMT_9 ());
      if (mfmt->fractional_digits)
	{
	  mfmt->fractional_type = DIGIT_9;
	}
      else
	{
	  mfmt->fractional_digits = wcsspn (fraction_part, FMT_STAR ());
	  if (mfmt->fractional_digits)
	    {
	      mfmt->fractional_type = DIGIT_STAR;
	    }
	  else
	    {
	      mfmt->fractional_type = DIGIT_Z;
	    }
	}
    }

  mfmt->integral_type = DIGIT_Z;

  mfmt->integral_digits = wcsspn (integer_part, WCSCAT (idc, FMT_Z (), FMT_THOUSANDS ())) - mfmt->thousands;

  if (mfmt->integral_digits <= 0)
    {
      mfmt->integral_digits = wcsspn (integer_part, WCSCAT (idc, FMT_9 (), FMT_THOUSANDS ())) - mfmt->thousands;

      if (mfmt->integral_digits > 0)
	{
	  mfmt->integral_type = DIGIT_9;
	}
      else
	{
	  mfmt->integral_digits = wcsspn (integer_part, WCSCAT (idc, FMT_STAR (), FMT_THOUSANDS ())) - mfmt->thousands;
	  if (mfmt->integral_digits > 0)
	    {
	      mfmt->integral_type = DIGIT_STAR;
	    }
	  else
	    {
	      mfmt->integral_type = DIGIT_Z;
	    }
	}
    }
}

/*
 * mfmt_value() - Get the double value represented by the value string.
 *    Return a pointer to the first char of the string after the last value
 *    char. If an error occurs, then the value is unchanged and NULL is
 *    returned.
 * return:
 * mfmt(in) :
 * string(in) :
 * the_double(out) :
 */
static const char *
mfmt_value (MONETARY_FORMAT * mfmt, const char *string, double *the_double)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  bool decimal_missing = false;

  cnv_fmt_analyze (string, mfmt->mode);

  do
    {
      if (mfmt->format == CURRENCY_FIRST && cnv_fmt_lex (&token) != FT_CURRENCY)
	{
	  error = CNV_ERR_NO_CURRENCY;
	  co_signal (CNV_ERR_NO_CURRENCY, CNV_ER_FMT_NO_CURRENCY);
	  break;
	}

      /* Get value of integer part. */
      error = fmt_integral_value (mfmt->integral_type, mfmt->integral_digits, false, mfmt->thousands, the_double);

      if (error && error != CNV_ERR_MISSING_INTEGER)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);

      if (mfmt->decimal)
	{
	  /* Decimal point found? */
	  decimal_missing = (type != FT_DECIMAL);
	  if (!decimal_missing)
	    {
	      double fraction_part;

	      if (error)
		{
		  break;	/* integer digit really missing. */
		}

	      /* get value of fraction part. */
	      error = fmt_fractional_value (mfmt->fractional_type, mfmt->fractional_digits, &fraction_part);

	      if (error && error != CNV_ERR_MISSING_FRACTION)
		{
		  break;
		}

	      if (*the_double < 0.0)
		{
		  fraction_part = -fraction_part;
		}
	      *the_double += fraction_part;
	      type = cnv_fmt_lex (&token);
	    }
	}

      if (mfmt->format == CURRENCY_LAST)
	{
	  if (type == FT_NONE)
	    {
	      error = CNV_ERR_NO_CURRENCY;
	      co_signal (CNV_ERR_NO_CURRENCY, CNV_ER_FMT_NO_CURRENCY);
	      break;
	    }
	  if (type == FT_CURRENCY)
	    {
	      type = cnv_fmt_lex (&token);
	    }
	}

      if (type != FT_NONE)
	{
	  /* Invalid chars at the end. */
	  error = cnv_bad_char (token.raw_text, !mfmt_valid_char (&token));
	  break;
	}

      if (decimal_missing)
	{
	  error = CNV_ERR_NO_DECIMAL;
	  co_signal (CNV_ERR_NO_DECIMAL, CNV_ER_FMT_NO_DECIMAL);
	  break;
	}
    }
  while (0);

  return error ? NULL : cnv_fmt_next_token ();
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * mfmt_print() - Change the given string to a representation of the given
 *    double value in the given format. if the max_size is not long enough to
 *    contain the new double string, then an error is returned.
 * return:
 * mfmt(in) :
 * the_double(in) :
 * string(out) :
 * max_size(in) :the maximum number of chars that can be stored in
 *   the string (including final '\0' char);
 */
static int
mfmt_print (MONETARY_FORMAT * mfmt, double the_double, char *string, int max_size)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  double the_value;

  bool unlimited_fraction = mfmt->decimal && !mfmt->fractional_digits;

  int max_digits = (!mfmt->integral_digits
		    || unlimited_fraction) ? FMT_MAX_DIGITS : mfmt->integral_digits + mfmt->fractional_digits;
  DB_C_INT nchars = (the_double < 0.0) + mfmt->decimal + max_digits;

  /* Create print format string. */
  const char *fmt_sign = "";
  const char *fmt_zeroes = mfmt->integral_type != DIGIT_Z ? "0" : "";
  const char *fmt_type = "f";
  const char *fmt_precision = unlimited_fraction ? "*" : "*.*";

  const char *fmt = adj_ar_concat_strings ("%", fmt_sign, fmt_zeroes,
					   fmt_precision, fmt_type, NULL);
  /* Print undecorated value string. */
  ADJ_ARRAY *buffer = cnv_get_string_buffer (nchars - max_digits + FMT_MAX_DIGITS);
  if (unlimited_fraction)
    {
      sprintf ((char *) adj_ar_get_buffer (buffer), fmt, nchars, the_double);
    }
  else
    {
      sprintf ((char *) adj_ar_get_buffer (buffer), fmt, nchars, (DB_C_INT) (mfmt->fractional_digits), the_double);
    }

  /* Trim leading blanks... */
  if (!mfmt->integral_digits || mfmt->integral_type == DIGIT_Z)
    {
      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while (cnv_fmt_lex (&token) == FT_UNKNOWN && mbs_eql (token.text, LOCAL_SPACE));
      adj_ar_remove (buffer, 0,
		     CAST_STRLEN (cnv_fmt_next_token () - token.length - (char *) adj_ar_get_buffer (buffer)));
    }

  /* ...or replace with leading stars. */
  else if (mfmt->integral_type == DIGIT_STAR)
    {
      int start;

      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while ((type = cnv_fmt_lex (&token)) == FT_MINUS || type == FT_PLUS);
      start = CAST_STRLEN (cnv_fmt_next_token () - token.length - (const char *) adj_ar_get_buffer (buffer));

      if (type == FT_ZEROES)
	{
	  int nzeroes;
	  int sl = LOCAL_STAR_LENGTH;
	  adj_ar_remove (buffer, start, start + token.length);
	  for (nzeroes = intl_mbs_len (token.text); nzeroes > 0; nzeroes--)
	    {
	      adj_ar_insert (buffer, LOCAL_STAR, sl, start);
	    }
	}
    }

  /* Replace trailing zeroes. */
  if (mfmt->decimal && mfmt->fractional_type != DIGIT_9)
    {
      int start;
      int length;
      int nzeroes;

      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while (cnv_fmt_lex (&token) != FT_DECIMAL);

      for (start = 0, length = 0, nzeroes = 0, type = cnv_fmt_lex (&token); type == FT_NUMBER || type == FT_ZEROES;
	   type = cnv_fmt_lex (&token))
	{
	  if (type == FT_ZEROES)
	    {
	      start = CAST_STRLEN (cnv_fmt_next_token () - token.length - (const char *) adj_ar_get_buffer (buffer));
	      length = token.length;
	      nzeroes = intl_mbs_len (token.text);
	    }
	}
      if (nzeroes > 0)
	{
	  adj_ar_remove (buffer, start, start + length);
	  if (mfmt->fractional_type == DIGIT_STAR)
	    {
	      int sl = LOCAL_STAR_LENGTH;
	      for (; nzeroes > 0; nzeroes--)
		{
		  adj_ar_insert (buffer, LOCAL_STAR, sl, start);
		}
	    }
	}
    }

  /* Add thousands separators. */
  if (mfmt->thousands)
    {
      fmt_add_thousands (buffer, NULL);
    }

  fmt_add_currency (buffer, NULL, mfmt);

  /* Too many digits? */
  if (!mfmt_value (mfmt, (const char *) adj_ar_get_buffer (buffer), &the_value))
    {
      error = co_code ();
    }

  /* Enough room to copy completed value string? */
  else if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }

  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
    }

  return error;
}
#endif

/* Integer Format Descriptor Functions */

/*
 * ifmt_valid_char() - Return true if token chars can legally appear in
 *    an integer value string.
 * return:
 * token(in) :
 */
static bool
ifmt_valid_char (FMT_TOKEN * token)
{
  FMT_TOKEN_TYPE type = token->type;

  return type == FT_NUMBER || type == FT_ZEROES || type == FT_MINUS || type == FT_PLUS || type == FT_STARS
    || type == FT_THOUSANDS || mbs_eql (token->text, local_thousands ());
}

/*
 * ifmt_new() - Initialize a descriptor for the given integer format string.
 * return:
 * ifmt(in) :
 * format(in) :
 */
static void
ifmt_new (INTEGER_FORMAT * ifmt, const char *format)
{
  wchar_t idc[3];
  const wchar_t *wfmt = cnv_wcs (format);
  const wchar_t *x_part;

  x_part = wcsstr (wfmt, FMT_X ());
  if (!x_part)
    {
      /* Numeric format */
      const wchar_t *integer_part = wcsstr (wfmt, FMT_PLUS ());
      integer_part = integer_part ? integer_part + 1 : wfmt;

      ifmt->sign_required = integer_part != wfmt;
      ifmt->thousands = wcsstr (wfmt, FMT_THOUSANDS ()) != NULL;
      ifmt->pattern = NULL;

      ifmt->integral_type = DIGIT_Z;
      if (!
	  ((ifmt->integral_digits =
	    wcsspn (integer_part, WCSCAT (idc, FMT_Z (), FMT_THOUSANDS ())) - ifmt->thousands) > 0))
	{

	  ifmt->integral_type =
	    ((ifmt->integral_digits =
	      wcsspn (integer_part,
		      WCSCAT (idc, FMT_9 (),
			      FMT_THOUSANDS ())) - ifmt->thousands) > 0) ? DIGIT_9 : ((ifmt->integral_digits =
										       wcsspn (integer_part,
											       WCSCAT (idc, FMT_STAR (),
												       FMT_THOUSANDS
												       ())) -
										       ifmt->thousands) >
										      0) ? DIGIT_STAR : DIGIT_Z;
	}
    }
#if defined (ENABLE_UNUSED_FUNCTION)
  else
    {
      /* Text pattern format. */
      FMT_TOKEN_TYPE ttype;
      FMT_TOKEN token;

      ifmt->sign_required = false;
      ifmt->thousands = false;
      ifmt->integral_digits = 0;
      ifmt->integral_type = DIGIT_9;
      ifmt->pattern = format;

      /* Count max digits allowed. */
      cnv_fmt_analyze (format, FL_INTEGER_FORMAT);
      while ((ttype = cnv_fmt_lex (&token)) != FT_NONE)
	{
	  if (ttype == FT_NUMBER)
	    {
	      ifmt->integral_digits += intl_mbs_len (token.text);
	    }
	}
    }
#endif
}

/*
 * ifmt_value() - Get the integer value represented by the value string.
 *    Return a pointer to the first char of the string after the last value
 *    char. If an error occurs, then the value is unchanged and NULL is
 *    returned.
 * return:
 * ifmt(in) :
 * string(in) :
 * the_integer(out) :
 */
static const char *
ifmt_value (INTEGER_FORMAT * ifmt, const char *string, int *the_integer)
{
  return
#if defined (ENABLE_UNUSED_FUNCTION)
    ifmt->pattern ? ifmt_text_value (ifmt, string, the_integer) :
#endif
    ifmt_numeric_value (ifmt, string, the_integer);
}

/*
 * bifmt_value() - Get the big integer value represented by the value string.
 *    Return a pointer to the first char of the string after the last value
 *    char. If an error occurs, then the value is unchanged and NULL is
 *    returned.
 * return:
 * bifmt(in) :
 * string(in) :
 * the_bigint(out) :
 */
static const char *
bifmt_value (INTEGER_FORMAT * ifmt, const char *string, DB_BIGINT * the_bigint)
{
  return
#if defined (ENABLE_UNUSED_FUNCTION)
    ifmt->pattern ? bifmt_text_value (ifmt, string, the_bigint) :
#endif
    bifmt_numeric_value (ifmt, string, the_bigint);
}

/*
 * ifmt_numeric_value() - Get the integer value represented by the value
 *   string, using the given numeric format. Return a pointer to the first
 *   char of the string after the last value char. If an error occurs, then
 *   the value is unchanged and NULL is returned.
 * return:
 * ifmt(in) :
 * string(in) :
 * the_integer(out) :
 */
static const char *
ifmt_numeric_value (INTEGER_FORMAT * ifmt, const char *string, int *the_integer)
{
  int error = 0;
  FMT_TOKEN token;
  double the_double;

  cnv_fmt_analyze (string, FL_LOCAL_NUMBER);

  do
    {
      /* Get value of integer part. */
      error =
	fmt_integral_value (ifmt->integral_type, ifmt->integral_digits, ifmt->sign_required, ifmt->thousands,
			    &the_double);

      if ((!error || error == CNV_ERR_MISSING_INTEGER) && cnv_fmt_lex (&token) != FT_NONE)
	{
	  /* Invalid chars at the end. */
	  error = cnv_bad_char (token.raw_text, !ifmt_valid_char (&token));
	}
      if (error)
	{
	  break;
	}

      if (the_double > DB_INT32_MAX)
	{
	  error = CNV_ERR_INTEGER_OVERFLOW;
	  co_signal (error, CNV_ER_FMT_INTEGER_OVERFLOW, DB_INT32_MAX);
	  break;
	}
      if (the_double < DB_INT32_MIN)
	{
	  error = CNV_ERR_INTEGER_UNDERFLOW;
	  co_signal (error, CNV_ER_FMT_INTEGER_UNDERFLOW, DB_INT32_MIN);
	  break;
	}
      *the_integer = (int) the_double;
    }
  while (0);

  return error ? NULL : cnv_fmt_next_token ();
}

/*
 * bifmt_numeric_value() - Get the integer value represented by the value
 *   string, using the given numeric format. Return a pointer to the first
 *   char of the string after the last value char. If an error occurs, then
 *   the value is unchanged and NULL is returned.
 * return:
 * ifmt(in) :
 * string(in) :
 * the_integer(out) :
 */
static const char *
bifmt_numeric_value (INTEGER_FORMAT * ifmt, const char *string, DB_BIGINT * the_bigint)
{
  int error = 0;
  FMT_TOKEN token;
  double the_double;

  cnv_fmt_analyze (string, FL_LOCAL_NUMBER);

  do
    {
      /* Get value of integer part. */
      error =
	fmt_integral_value (ifmt->integral_type, ifmt->integral_digits, ifmt->sign_required, ifmt->thousands,
			    &the_double);

      if ((!error || error == CNV_ERR_MISSING_INTEGER) && cnv_fmt_lex (&token) != FT_NONE)
	{
	  /* Invalid chars at the end. */
	  error = cnv_bad_char (token.raw_text, !ifmt_valid_char (&token));
	}
      if (error)
	{
	  break;
	}

      if (the_double > DB_BIGINT_MAX)
	{
	  error = CNV_ERR_INTEGER_OVERFLOW;
	  co_signal (error, CNV_ER_FMT_INTEGER_OVERFLOW, (long) DB_BIGINT_MAX);
	  break;
	}
      if (the_double < DB_BIGINT_MIN)
	{
	  error = CNV_ERR_INTEGER_UNDERFLOW;
	  co_signal (error, CNV_ER_FMT_INTEGER_UNDERFLOW, (long) DB_BIGINT_MIN);
	  break;
	}
      *the_bigint = (DB_BIGINT) the_double;
    }
  while (0);

  return error ? NULL : cnv_fmt_next_token ();
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ifmt_text_value() - Get the integer value represented by the value string,
 *     using the given text format. Return a pointer to the first char of the
 *     string after the last value char. If an error occurs, then the value
 *     is unchanged and NULL is returned.
 * return:
 * ifmt(in) :
 * string(in) :
 * the_integer(out) :
 */
static const char *
ifmt_text_value (INTEGER_FORMAT * ifmt, const char *string, int *the_integer)
{
  ADJ_ARRAY *vstring = cnv_get_value_string_buffer (0);
  int nchars;

  adj_ar_replace (vstring, string, strlen (string) + 1, 0, ADJ_AR_EOA);
  nchars = ifmt_text_numeric (ifmt, vstring);

  return (!nchars
	  || !ifmt_numeric_value (ifmt, (const char *) adj_ar_get_buffer (vstring),
				  the_integer)) ? NULL : string + nchars;
}

/*
 * ifmt_text_value() - Get the integer value represented by the value string,
 *     using the given text format. Return a pointer to the first char of the
 *     string after the last value char. If an error occurs, then the value
 *     is unchanged and NULL is returned.
 * return:
 * ifmt(in) :
 * string(in) :
 * the_integer(out) :
 */
static const char *
bifmt_text_value (INTEGER_FORMAT * ifmt, const char *string, DB_BIGINT * the_bigint)
{
  ADJ_ARRAY *vstring = cnv_get_value_string_buffer (0);
  int nchars;

  adj_ar_replace (vstring, string, strlen (string) + 1, 0, ADJ_AR_EOA);
  nchars = ifmt_text_numeric (ifmt, vstring);

  return (!nchars
	  || !bifmt_numeric_value (ifmt, (const char *) adj_ar_get_buffer (vstring),
				   the_bigint)) ? NULL : string + nchars;
}

/*
 * ifmt_text_numeric() - Convert a text format integer string into a numeric
 *    format string. If an error occurs, return 0; otherwise, return the number
 *    of text string bytes processed.
 * return:
 * ifmt(in) :
 * text_string(out) :
 */
static int
ifmt_text_numeric (INTEGER_FORMAT * ifmt, ADJ_ARRAY * text_string)
{
  int error = 0;
  int nbytes = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE ttype;
  const char *ts = (const char *) adj_ar_get_buffer (text_string);
  const char *sp;
  int sbytes = 0;

  /* Strip pattern chars from numeric value string according to format. */
  for (sp = ts, cnv_fmt_analyze (ifmt->pattern, FL_INTEGER_FORMAT); !error && (ttype = cnv_fmt_lex (&token)) != FT_NONE;
       nbytes += sbytes)
    {

      if (ttype == FT_PATTERN)
	{
	  if (strncmp (sp, token.text, token.length))
	    {
	      error = CNV_ERR_BAD_PATTERN;
	      co_signal (error, CNV_ER_FMT_BAD_PATTERN, token.text, sp - ts);
	    }
	  else
	    {
	      /* Remove pattern chars. */
	      int i = CAST_STRLEN (sp - ts);
	      adj_ar_remove (text_string, i, i + token.length);
	      sbytes = token.length;
	    }
	}
      else
	{
	  sbytes = intl_mbs_spn (sp, FMT_DIGITS ());
	  sp += sbytes;
	}
    }

  /* All value string chars matched? */
  if (!error && strlen (sp))
    {
      error = cnv_bad_char (sp, true);
    }

  return error ? 0 : nbytes;
}

/*
 * ifmt_print() - Change the given string to a representation of the given
 *   integer value in the given format. If max_size is not long enough to
 *   contain the new int string, then an error is returned.
 * return:
 * ifmt(in) :
 * the_integer(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char);
 */
static int
ifmt_print (INTEGER_FORMAT * ifmt, DB_BIGINT the_bigint, char *string, int max_size)
{
  return ifmt->pattern ? ifmt_text_print (ifmt, the_bigint, string, max_size) : ifmt_numeric_print (ifmt, the_bigint,
												    string, max_size);
}

/*
 * ifmt_numeric_print() - Change the given string to a representation of
 *    the given integer value in the given numeric format. if max_size is not
 *    long enough to contain the new int string, then an error is returned.
 * return:
 * ifmt(in) :
 * the_integer(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
static int
ifmt_numeric_print (INTEGER_FORMAT * ifmt, DB_BIGINT the_bigint, char *string, int max_size)
{
  int error = 0;
  int the_value;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;

  int max_digits = !ifmt->integral_digits ? FMT_MAX_DIGITS : ifmt->integral_digits;

  DB_C_INT nchars = (the_bigint < 0.0 || ifmt->sign_required) + max_digits;

  /* Create print format string. */
  const char *fmt_sign = ifmt->sign_required ? "+" : "";
  const char *fmt_zeroes = ifmt->integral_type != DIGIT_Z ? "0" : "";
  const char *fmt_width = "*";

  const char *fmt = adj_ar_concat_strings ("%", fmt_sign, fmt_zeroes, fmt_width, "d", NULL);

  /* Print undecorated value string. */
  ADJ_ARRAY *buffer = cnv_get_string_buffer (nchars - max_digits + FMT_MAX_DIGITS);
  sprintf ((char *) adj_ar_get_buffer (buffer), fmt, nchars, the_bigint);

  /* Trim leading blanks... */
  if (!ifmt->integral_digits || ifmt->integral_type == DIGIT_Z)
    {
      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while (cnv_fmt_lex (&token) == FT_UNKNOWN && mbs_eql (token.text, LOCAL_SPACE));
      adj_ar_remove (buffer, 0,
		     CAST_STRLEN (cnv_fmt_next_token () - token.length - (char *) adj_ar_get_buffer (buffer)));
    }

  /* ...or replace with leading stars. */
  else if (ifmt->integral_type == DIGIT_STAR)
    {
      int start;

      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while ((type = cnv_fmt_lex (&token)) == FT_MINUS || type == FT_PLUS);
      start = CAST_STRLEN (cnv_fmt_next_token () - token.length - (const char *) adj_ar_get_buffer (buffer));
      if (type == FT_ZEROES)
	{
	  int nzeroes;
	  int sl = LOCAL_STAR_LENGTH;
	  adj_ar_remove (buffer, start, start + token.length);
	  for (nzeroes = intl_mbs_len (token.text); nzeroes > 0; nzeroes--)
	    {
	      adj_ar_insert (buffer, LOCAL_STAR, sl, start);
	    }
	}
    }

  /* Add thousands separators. */
  if (ifmt->thousands)
    {
      fmt_add_thousands (buffer, NULL);
    }

  /* Too many digits? */
  if (!ifmt_numeric_value (ifmt, (const char *) adj_ar_get_buffer (buffer), &the_value))
    {
      error = co_code ();
    }

  /* Enough room to copy completed value string? */
  else if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }

  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
    }

  return error;
}

/*
 * ifmt_text_print() - Change the given string to a representation of
 *    the given integer value in the given text format. if max_size is not
 *    long enough to contain the new int string, then an error is returned.
 * return:
 * ifmt(in) :
 * the_integer(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
static int
ifmt_text_print (INTEGER_FORMAT * ifmt, DB_BIGINT the_bigint, char *string, int max_size)
{
  ADJ_ARRAY *vstring = cnv_get_value_string_buffer (strlen (ifmt->pattern));

  /* Get numeric value string. */
  int error = ifmt_numeric_print (ifmt, the_bigint,
				  (char *) adj_ar_get_buffer (vstring),
				  strlen (ifmt->pattern) + 1);

  if (!error)
    {
      /* Elaborate text value string according to format pattern. */
      ifmt_numeric_text (ifmt, vstring);

      /* Enough room to copy completed value string? */
      if ((int) strlen ((char *) adj_ar_get_buffer (vstring)) >= max_size)
	{
	  error = CNV_ERR_STRING_TOO_LONG;
	  co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
	}

      else
	{
	  strcpy (string, (char *) adj_ar_get_buffer (vstring));
	}
    }

  return error;
}

/*
 * ifmt_numeric_text() - Convert the numeric string into a text format integer
 *     string according to the given text format.
 * return:
 * ifmt(in) :
 * numeric_string(in/out) :
 */
static void
ifmt_numeric_text (INTEGER_FORMAT * ifmt, ADJ_ARRAY * numeric_string)
{
  FMT_TOKEN token;
  FMT_TOKEN_TYPE ttype;
  int i;

  /* Elaborate text value string according to format pattern. */
  for (i = 0, cnv_fmt_analyze (ifmt->pattern, FL_INTEGER_FORMAT); (ttype = cnv_fmt_lex (&token)) != FT_NONE;
       i += token.length)
    {

      if (ttype == FT_PATTERN)
	{
	  adj_ar_insert (numeric_string, token.text, token.length, i);
	}
    }
}
#endif

/* Bit String Format Descriptor Functions */

/*
 * bfmt_valid_char() - Return true if token chars can legally appear in
 *    a bit string.
 * return:
 * token(in) :
 */
static bool
bfmt_valid_char (FMT_TOKEN * token)
{
  FMT_TOKEN_TYPE type = token->type;
  return (type == FT_BINARY_DIGITS || type == FT_HEX_DIGITS);
}

/*
 * bfmt_new() - Initialize a descriptor for the given bit string
 *   format string.
 * return:
 * bfmt(in) :
 * format(in) :
 */
static void
bfmt_new (BIT_STRING_FORMAT * bfmt, const char *format)
{
  /* Text pattern format. */
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;

  cnv_fmt_analyze (format, FL_BIT_STRING_FORMAT);
  if ((ttype = cnv_fmt_lex (&token)) == FT_HEX_DIGITS)
    {
      *bfmt = BIT_STRING_HEX;
    }
  else
    {
      *bfmt = BIT_STRING_BINARY;
    }
}

/*
 * bin_string_to_int() - Take a string made up of 8 or less '0' or '1'
 *    characters and interprets them as an 8-bit integer.  <nbits> describes
 *    the number of characters that are to be extracted from <src>.  If the
 *    number of characters less than 8, the characters are assumed to be the
 *    MSB of the result.
 * return:
 * src(in) :
 * nbits(in) :
 */
static int
bin_string_to_int (const char *src, int nbits)
{
  int val;
  char my_bin[BITS_IN_BYTE + 1];

  strncpy (my_bin, src, BITS_IN_BYTE);
  my_bin[BITS_IN_BYTE] = '\0';

  parse_int (&val, my_bin, 2);

  return (val << (BITS_IN_BYTE - nbits));
}

/*
 * hex_string_to_int() - Take a string made up of 1 or 2 hexadecimal characters
 *   and interprets them as an 8-bit integer.  <nhex> describes the number
 *   of characters that are to be extracted from <src>.  If <src> has only one
 *   character, it is assumed to be in the MSB of the result.
 * return:
 * src(in) :
 * nhex(in) :
 */
static int
hex_string_to_int (const char *src, int nhex)
{
  int val;
  char my_hex[HEX_IN_BYTE + 1];

  strncpy (my_hex, src, HEX_IN_BYTE);
  my_hex[HEX_IN_BYTE] = '\0';

  parse_int (&val, my_hex, 16);

  return (val << ((HEX_IN_BYTE - nhex) * BITS_IN_HEX));
}

/*
 * bfmt_value() - Get the bit string value represented by the value string,
 *    using the given bit string format. Return a pointer to the first char
 *    of the string after the last value char. If an error occurs, then the
 *    value is unchanged and NULL is returned.
 * return:
 * bfmt(in) :
 * string(in) :
 * the_db_bit(out) :
 */
static const char *
bfmt_value (BIT_STRING_FORMAT bfmt, const char *string, DB_VALUE * the_db_bit)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE ttype;
  char *the_bit_string = NULL;
  int length = 0;
  int byte_index = 0;
  const char *end;
  const char *src;
  int ndigs;

  /* Check for special case - Empty string is not the same as NULL */
  if (string[0] == '\0')
    {
      the_bit_string = (char *) db_private_alloc (NULL, 1);
      if (the_bit_string == NULL)
	{
	  return NULL;
	}
      db_make_bit (the_db_bit, TP_FLOATING_PRECISION_VALUE, the_bit_string, 0);
      the_db_bit->need_clear = true;
      return cnv_fmt_next_token ();
    }

  cnv_fmt_analyze (string, FL_BIT_STRING);

  do
    {
      ttype = cnv_fmt_lex (&token);

      /* Process legal character according to the format */
      if (bfmt_valid_char (&token))
	{
	  if (bfmt == BIT_STRING_BINARY)
	    {
	      if (ttype != FT_BINARY_DIGITS)
		{
		  error = CNV_ERR_BAD_BINARY_DIGIT;
		  break;
		}
	      else
		{
		  length += token.length;
		  the_bit_string = (char *) db_private_alloc (NULL, BYTE_COUNT (length));
		  if (!the_bit_string)
		    {
		      return NULL;
		    }
		  end = token.text + token.length;
		  for (src = token.text; src < end; src += BITS_IN_BYTE)
		    {
		      ndigs = GET_MIN (BITS_IN_BYTE, CAST_STRLEN (end - src));
		      the_bit_string[byte_index] = bin_string_to_int (src, ndigs);
		      byte_index++;
		    }
		}
	    }
	  else if (bfmt == BIT_STRING_HEX)
	    {
	      length = length + (token.length * BITS_IN_HEX);
	      the_bit_string = (char *) db_private_alloc (NULL, BYTE_COUNT (length));
	      if (!the_bit_string)
		{
		  return NULL;
		}
	      end = token.text + token.length;
	      for (src = token.text; src < end; src += HEX_IN_BYTE)
		{
		  ndigs = GET_MIN (HEX_IN_BYTE, CAST_STRLEN (end - src));
		  the_bit_string[byte_index] = hex_string_to_int (src, ndigs);
		  byte_index++;
		}
	    }
	}
      /* End of string detected */
      else if (cnv_fmt_lex (&token) == FT_NONE)
	{
	  break;
	}
      /* Illegal character detected */
      else
	{
	  if (bfmt == BIT_STRING_BINARY)
	    {
	      error = CNV_ERR_BAD_BINARY_DIGIT;
	    }
	  else
	    {
	      error = CNV_ERR_BAD_HEX_DIGIT;
	    }
	  break;
	}
    }
  while (0);

  if (error)
    {
      if (the_bit_string)
	{
	  db_private_free_and_init (NULL, the_bit_string);
	}
      return NULL;
    }
  else
    {
      db_make_bit (the_db_bit, length, the_bit_string, length);
      the_db_bit->need_clear = true;
      return cnv_fmt_next_token ();
    }
}

/*
 * bfmt_print() - Change the given string to a representation of the given bit
 *    string value in the given format. if this is not long enough to contain
 *    the new string, then an error is returned.
 * return:
 * bfmt(in) :
 * the_db_bit(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
static int
bfmt_print (BIT_STRING_FORMAT * bfmt, const DB_VALUE * the_db_bit, char *string, int max_size)
{
  int length = 0;
  int string_index = 0;
  int byte_index;
  int bit_index;
  const char *bstring;
  int error = NO_ERROR;
  static char digits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };

  /* Get the buffer and the length from the_db_bit */
  bstring = db_get_bit (the_db_bit, &length);

  switch (*bfmt)
    {
    case BIT_STRING_BINARY:
      if (length + 1 > max_size)
	{
	  error = CNV_ERR_STRING_TOO_LONG;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      for (bit_index = 7; bit_index >= 0 && string_index < length; bit_index--)
		{
		  *string = digits[((bstring[byte_index] >> bit_index) & 0x1)];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    case BIT_STRING_HEX:
      if (BYTE_COUNT_HEX (length) + 1 > max_size)
	{
	  error = CNV_ERR_STRING_TOO_LONG;
	}
      else
	{
	  for (byte_index = 0; byte_index < BYTE_COUNT (length); byte_index++)
	    {
	      *string = digits[((bstring[byte_index] >> BITS_IN_HEX) & 0x0f)];
	      string++;
	      string_index++;
	      if (string_index < BYTE_COUNT_HEX (length))
		{
		  *string = digits[((bstring[byte_index] & 0x0f))];
		  string++;
		  string_index++;
		}
	    }
	  *string = '\0';
	}
      break;

    default:
      assert (!"possible to get here");
      break;
    }

  return error;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/* Numeric Format Descriptor Functions */

/*
 * nfmt_integral_value() - Scan tokens and parse an integral value. If a valid
 *    value can't be found, then return an error condition. otherwise, set
 *    the_value to the value found.
 * return:
 * digit_type(in) : the type of digit chars allowed
 * ndigits(in) : the maximum number of digits
 * sign_required(in) : If this is true, a positive/negative sign
 *                     token must be present
 * thousands(in) : If this is true, thousands separators must be included.
 * the_value(out) :
 */
static int
nfmt_integral_value (FORMAT_DIGIT digit_type, int ndigits, bool sign_required, bool thousands, char *the_value)
{
  int nfound;
  int error;

  error = nfmt_integral_digits (digit_type, ndigits, sign_required, thousands, the_value, &nfound);
  if (!error && ndigits)
    {
      /* Too many digits? */
      if (nfound > ndigits)
	{
	  error = CNV_ERR_EXTRA_INTEGER;
	  co_signal (CNV_ERR_EXTRA_INTEGER, CNV_ER_FMT_EXTRA_INTEGER);
	}
      /* Enough digits found? */
      else if (digit_type != DIGIT_Z && nfound < ndigits)
	{
	  error = CNV_ERR_MISSING_INTEGER;
	  co_signal (CNV_ERR_MISSING_INTEGER, CNV_ER_FMT_MISSING_INTEGER);
	}
    }

  return error;
}

/*
 * nfmt_integral_digits() - Scan tokens and parse an integral value. If a valid
 *    value can't be found, then return an error condition. otherwise, set
 *    the_value to the value found.
 * return:
 * digit_type(in) : the type of digit chars allowed
 * ndigits(in) : the maximum number of digits
 * sign_required(in) : If this is true, a positive/negative sign
 *                     token must be present
 * thousands(in) : If this is true, thousands separators must be included.
 * the_value(out) :
 * nfound(out) : The number of digits in the value
 */
static int
nfmt_integral_digits (FORMAT_DIGIT digit_type, int ndigits, bool sign_required, bool thousands, char *the_value,
		      int *nfound)
{
  int error = 0;

  do
    {
      FMT_TOKEN_TYPE leading = FT_NONE;
      int nleading = 0;
      FMT_TOKEN token;
      FMT_TOKEN_TYPE type = cnv_fmt_lex (&token);
      bool negative = (type == FT_MINUS);

      /* Sign? */
      if (negative || (sign_required && type == FT_PLUS))
	{
	  /* Yes, scan next token. */
	  type = cnv_fmt_lex (&token);
	  if (type == FT_PLUS || type == FT_MINUS)
	    {
	      error = CNV_ERR_EXTRA_SIGN;
	      co_signal (CNV_ERR_EXTRA_SIGN, CNV_ER_FMT_EXTRA_SIGN);
	      break;
	    }
	}
      else if (sign_required)
	{
	  cnv_fmt_unlex ();
	  error = CNV_ERR_NO_SIGN;
	  co_signal (CNV_ERR_NO_SIGN, CNV_ER_FMT_NO_SIGN);
	  break;
	}
      if (negative)
	{
	  strcpy (the_value, "-");
	}
      else
	{
	  strcpy (the_value, "");
	}

      /* Leading fill chars? */
      *nfound = 0;
      if (type == FT_ZEROES || type == FT_STARS)
	{
	  /* Yes, scan next token. */
	  leading = type;
	  nleading = intl_mbs_len (token.text);
	  type = cnv_fmt_lex (&token);
	}

      /* Any numeric chars left? */
      if (type == FT_NUMBER)
	{
	  int initial_digits = 0;
	  int group_size = *local_grouping ();

	  /* Yes, add to numeric value. */
	  for (; type == FT_NUMBER || type == FT_ZEROES; type = cnv_fmt_lex (&token))
	    {
	      int tdigits = intl_mbs_len (token.text);
	      if (thousands && group_size != CHAR_MAX && ((initial_digits += tdigits) > group_size))
		{
		  error = CNV_ERR_BAD_THOUS;
		  co_signal (CNV_ERR_BAD_THOUS, CNV_ER_FMT_BAD_THOUS);
		  break;
		}
	      strcat (the_value, token.text);
	      *nfound += tdigits;
	    }

	  /* Add thousands groups, if necessary. */
	  if (thousands)
	    {
	      for (; type == FT_THOUSANDS; type = cnv_fmt_lex (&token))
		{
		  int tdigits = intl_mbs_len (token.text);
		  strcat (the_value, token.text);
		  *nfound += tdigits;
		}
	      if (type == FT_NUMBER || type == FT_ZEROES
		  || (type == FT_UNKNOWN && mbs_eql (token.text, local_thousands ())))
		{
		  error = CNV_ERR_BAD_THOUS;
		  co_signal (CNV_ERR_BAD_THOUS, CNV_ER_FMT_BAD_THOUS);
		  break;
		}
	    }
	}

      /* Assert: we've scanned following token, so put it back. */
      cnv_fmt_unlex ();

      /* Valid leading fill chars? */
      if (leading != FT_NONE)
	{
	  if ((leading == FT_STARS && digit_type != DIGIT_STAR) || (leading == FT_ZEROES && !(digit_type == DIGIT_9 ||
											      /* allow singleton zero. */
											      (digit_type == DIGIT_Z
											       && nleading == 1
											       && *the_value == 0.0))))
	    {

	      error = CNV_ERR_BAD_LEADING;
	      co_signal (CNV_ERR_BAD_LEADING, CNV_ER_FMT_BAD_LEADING, leading == FT_ZEROES ? LOCAL_0 : LOCAL_STAR);
	      break;
	    }
	  *nfound += nleading;
	}
    }
  while (0);

  return error;
}

/*
 * nfmt_fractional_digits() - Scan tokens and parse fractional value.
 *    If a valid value can't be found, then return an error condition.
 *  otherwise, set the_value to the value found.
 * return:
 * digit_type(in) : the type of digit chars allowed
 * ndigits(in) : the maximum number of digits
 * the_value(out) :
 * nfound(out) : set to the number of digits in the value
 */
static int
nfmt_fractional_digits (FORMAT_DIGIT digit_type, int ndigits, char *the_value, int *nfound)
{
  int error = 0;

  the_value[0] = '\0';
  *nfound = 0;

  do
    {
      FMT_TOKEN token;
      FMT_TOKEN_TYPE type;
      FMT_TOKEN_TYPE last = FT_NONE;

      /* Any numeric chars? */
      for (; (type = cnv_fmt_lex (&token)) == FT_NUMBER || type == FT_ZEROES; last = type)
	{

	  /* Yes, add to numeric value. */
	  int tdigits = intl_mbs_len (token.text);
	  strcat (the_value, token.text);
	  *nfound += tdigits;
	}

      /* Any trailing fill chars? */
      if (type == FT_STARS)
	{
	  if (last != FT_ZEROES)
	    {
	      /* Can't have trailing zeroes AND trailing stars! */
	      last = type;
	    }
	  *nfound += intl_mbs_len (token.text);
	}
      else
	{
	  /* No, retry this token later. */
	  cnv_fmt_unlex ();
	}

      /* Valid trailing fill chars? */
      if (last == FT_STARS && digit_type != DIGIT_STAR)
	{
	  error = CNV_ERR_BAD_TRAILING;
	  co_signal (CNV_ERR_BAD_TRAILING, CNV_ER_FMT_BAD_TRAILING, last == FT_ZEROES ? LOCAL_0 : LOCAL_STAR);
	  break;
	}
    }
  while (0);

  return error;
}

/*
 * nfmt_fractional_value() - Scan tokens and parse fractional value.
 *   If a valid value can't be found, then return an error condition.
 *   otherwise, set the_value to the value found.
 * return:
 * digit_type(in) : the type of digit chars allowed
 * ndigits(in) : the maximum number of digits
 * the_value(out) :
 */
static int
nfmt_fractional_value (FORMAT_DIGIT digit_type, int ndigits, char *the_value)
{
  int nfound;
  int error;

  error = nfmt_fractional_digits (digit_type, ndigits, the_value, &nfound);

  if (ndigits)
    {
      /* Too many digits? */
      if (nfound > ndigits)
	{
	  error = CNV_ERR_EXTRA_FRACTION;
	  co_signal (CNV_ERR_EXTRA_FRACTION, CNV_ER_FMT_EXTRA_FRACTION);
	}

      /* Enough digits found? */
      else if (digit_type != DIGIT_Z && nfound < ndigits)
	{
	  error = CNV_ERR_MISSING_FRACTION;
	  co_signal (CNV_ERR_MISSING_FRACTION, CNV_ER_FMT_MISSING_FRACTION);
	}
    }

  return error;
}

/*
 * num_fmt_value() - Get the numeric value represented by the value string.
 *     Return a pointer to the first char of the string after the last value
 *     char. If an error occurs, then the value is unchanged and NULL is
 *     returned.
 * return:
 * ffmt(in) :
 * string(in) :
 * the_numeric(out) :
 */
static const char *
num_fmt_value (FLOAT_FORMAT * ffmt, const char *string, DB_VALUE * the_numeric)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  bool decimal_missing = false;
  char temp[DEC_BUFFER_SIZE];
  int precision;
  int scale;
  DB_VALUE local_numeric;

  cnv_fmt_analyze (string, FL_LOCAL_NUMBER);

  do
    {
      /* Get value of integer part. */
      error =
	nfmt_integral_value (ffmt->integral_type, ffmt->integral_digits, ffmt->sign_required, ffmt->thousands, temp);

      if (error && error != CNV_ERR_MISSING_INTEGER)
	{
	  break;
	}

      type = cnv_fmt_lex (&token);
      precision = strlen (temp);

      if (ffmt->decimal)
	{
	  /* Decimal point found? */
	  decimal_missing = (type != FT_DECIMAL);
	  if (!decimal_missing)
	    {
	      char fraction_part[DEC_BUFFER_SIZE];

	      if (error)
		{
		  break;	/* integer digit really missing. */
		}

	      /* Yes, get value of fraction part. */
	      error = nfmt_fractional_value (ffmt->fractional_type, ffmt->fractional_digits, fraction_part);
	      /*
	       * Digit really missing? Or did invalid char stop scan prematurely?
	       * Find out later. Important to ValueEditor to report
	       * CNV_ERR_MISSING_FRACTION correctly!
	       */
	      if (error && error != CNV_ERR_MISSING_FRACTION && error != CNV_ERR_EXTRA_FRACTION)
		{
		  break;
		}

	      strncat (temp, fraction_part, sizeof (temp) - precision - 1);
	      precision += strlen (fraction_part);
	      scale = strlen (fraction_part);
	      type = cnv_fmt_lex (&token);
	      numeric_coerce_dec_str_to_num (temp, db_locate_numeric (&local_numeric));
	      db_make_numeric (the_numeric, db_locate_numeric (&local_numeric), precision, scale);
	    }
	  /* No decimal point found.  Compute the integer portion */
	  else
	    {
	      numeric_coerce_dec_str_to_num (temp, db_locate_numeric (&local_numeric));
	      db_make_numeric (the_numeric, db_locate_numeric (&local_numeric), precision, 0);
	    }
	}

      if (type != FT_NONE)
	{
	  /* Invalid chars at the end. */
	  error = cnv_bad_char (token.raw_text, !ffmt_valid_char (&token));
	  break;
	}

      /* Decimal point missing? */
      if (decimal_missing)
	{
	  error = CNV_ERR_NO_DECIMAL;
	  co_signal (CNV_ERR_NO_DECIMAL, CNV_ER_FMT_NO_DECIMAL);
	  break;
	}
    }
  while (0);

  return error ? NULL : cnv_fmt_next_token ();
}
#endif

/*
 * num_fmt_print() - Change the given string to a representation of
 *   the given numeric value in the given format. if max_size is not long
 *   enough to contain the new numeric string, then an error is returned.
 * return:
 * ffmt(in) :
 * the_numeric(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char);
 */
static int
num_fmt_print (FLOAT_FORMAT * ffmt, const DB_VALUE * the_numeric, char *string, int max_size)
{
  int error = 0;
  FMT_TOKEN token;
  FMT_TOKEN_TYPE type;
  ADJ_ARRAY *buffer = cnv_get_value_string_buffer (DEC_BUFFER_SIZE + 2);
  int scale;
  int position;
  const char *dp;
  char num_dec_digits[DEC_BUFFER_SIZE];
  int integral_start_pos = 0;

  /* Copy the numeric decimal digits into the buffer in the default format */
  scale = DB_VALUE_SCALE (the_numeric);
  numeric_coerce_num_to_dec_str (db_locate_numeric ((DB_VALUE *) the_numeric), num_dec_digits);
  sprintf ((char *) adj_ar_get_buffer (buffer), "%s", num_dec_digits);

  /* Add the decimal point */
  if (scale > 0)
    {
      position = strlen (num_dec_digits) - scale;
      dp = local_decimal ();
      adj_ar_insert (buffer, dp, strlen (dp), position);
    }

  /* Trim leading zeroes */
  cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
  while ((type = cnv_fmt_lex (&token)) == FT_MINUS || type == FT_PLUS)
    integral_start_pos++;
  if (type == FT_ZEROES)
    {
      adj_ar_remove (buffer, integral_start_pos,
		     CAST_STRLEN (cnv_fmt_next_token () - (char *) adj_ar_get_buffer (buffer)));
    }

  /* Trim leading blanks... */
  if (!ffmt->integral_digits || ffmt->integral_type == DIGIT_Z)
    {
      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while (cnv_fmt_lex (&token) == FT_UNKNOWN && mbs_eql (token.text, LOCAL_SPACE));
      adj_ar_remove (buffer, 0,
		     CAST_STRLEN (cnv_fmt_next_token () - token.length - (char *) adj_ar_get_buffer (buffer)));
    }

  /* ...or replace with leading stars. */
  else if (ffmt->integral_type == DIGIT_STAR)
    {
      int start;

      cnv_fmt_analyze ((const char *) adj_ar_get_buffer (buffer), FL_LOCAL_NUMBER);
      while ((type = cnv_fmt_lex (&token)) == FT_MINUS || type == FT_PLUS);
      start = CAST_STRLEN (cnv_fmt_next_token () - token.length - (const char *) adj_ar_get_buffer (buffer));

      if (type == FT_ZEROES)
	{
	  int nzeroes;
	  int sl = LOCAL_STAR_LENGTH;
	  adj_ar_remove (buffer, start, start + token.length);
	  for (nzeroes = intl_mbs_len (token.text); nzeroes > 0; nzeroes--)
	    {
	      adj_ar_insert (buffer, LOCAL_STAR, sl, start);
	    }
	}
    }

#if defined (ENABLE_UNUSED_FUNCTION)
  /* Add thousands separators. */
  if (ffmt->thousands)
    {
      fmt_add_thousands (buffer, NULL);
    }
#endif

  /* Enough room to copy completed value string? */
  if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }

  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
      if (strlen (string) == 0)
	{
	  strcpy (string, "0");
	}
    }

  return error;
}


/* Conversion Functions */
/*
 * db_string_value() - Change the given value to the result of converting
 *    the value string in the given format. Return a pointer to the first
 *    char of the string after the last value char. The string and format must
 *    be valid for the type of the given value. If an error occurs, then
 *    the value is unchanged and NULL is returned.
 * return:
 * string(in) :
 * str_size(in) :
 * format(in) :
 * value(out) :
 *
 */
const char *
db_string_value (const char *string, int str_size, const char *format, DB_VALUE * value)
{
  const char *next = NULL;

  assert (string != NULL);
  assert (value != NULL);

  /* Empty string is always NULL value. */
  if (!strlen (string))
    {
      db_value_domain_init (value, DB_VALUE_DOMAIN_TYPE (value), DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      next = string;
    }
  else
    {
      switch (DB_VALUE_DOMAIN_TYPE (value))
	{
	case DB_TYPE_DATE:
	  {
	    DB_DATE date = 0;

	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if (db_string_to_date (string, &date) == NO_ERROR)
	      {
		db_value_put_encoded_date (value, &date);
		next = string;
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_DOUBLE:
	  {
	    double num;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if ((next = db_string_double (string, format, &num)))
	      {
		db_make_double (value, num);
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_FLOAT:
	  {
	    float num;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if ((next = db_string_float (string, format, &num)))
	      {
		db_make_float (value, num);
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_INTEGER:
	  {
	    int num;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if ((next = db_string_integer (string, format, &num)))
	      {
		db_make_int (value, num);
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_BIGINT:
	  {
	    DB_BIGINT num;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if ((next = db_string_bigint (string, format, &num)))
	      {
		db_make_bigint (value, num);
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_MONETARY:
	  /* Check for valid currency.  If not, set it to the default */
	  if (!cnv_valid_currency (db_value_get_monetary_currency (value)))
	    {
	      db_make_monetary (value, DB_CURRENCY_DEFAULT, 0.0);
	    }
	  /* Extract the initialized monetary field and convert */
	  if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	    {
	      return NULL;
	    }
	  next = db_string_monetary (string, format, db_get_monetary (value));
	  csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	  break;

	case DB_TYPE_NULL:
	  if (strlen (string))
	    {
	      co_signal (CNV_ERR_BAD_NULL, CNV_ER_FMT_BAD_NULL);
	    }
	  else
	    {
	      next = string;
	    }
	  break;

	case DB_TYPE_SHORT:
	  {
	    short the_short;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if ((next = db_string_short (string, format, &the_short)))
	      {
		db_make_short (value, the_short);
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_CHAR:
	  {
	    int size = strlen (string);
	    db_make_char (value, size, string, size, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	    next = string + size;
	    break;
	  }

	case DB_TYPE_VARCHAR:
	  {
	    int size = strlen (string);
	    db_make_varchar (value, size, string, size, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	    next = string + size;
	    break;
	  }

	case DB_TYPE_NCHAR:
	  {
	    int size;
	    intl_char_count ((unsigned char *) string, strlen (string), LANG_SYS_CODESET, &size);
	    db_make_nchar (value, size, string, size, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	    next = string + strlen (string);
	    break;
	  }

	case DB_TYPE_VARNCHAR:
	  {
	    int char_count;
	    intl_char_count ((unsigned char *) string, strlen (string), LANG_SYS_CODESET, &char_count);
	    db_make_varnchar (value, char_count, string, char_count, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	    next = string + strlen (string);
	    break;
	  }

	case DB_TYPE_TIME:
	  {
	    DB_TIME time;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if (db_string_to_time (string, &time) == NO_ERROR)
	      {
		db_value_put_encoded_time (value, &time);
		next = string;
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_TIMESTAMP:
	  {
	    DB_TIMESTAMP timestamp;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if (db_string_to_timestamp (string, &timestamp) == NO_ERROR)
	      {
		db_make_timestamp (value, timestamp);
		next = string;
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_DATETIME:
	  {
	    DB_DATETIME datetime;
	    if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	      {
		return NULL;
	      }

	    if (db_string_to_datetime (string, &datetime) == NO_ERROR)
	      {
		db_make_datetime (value, &datetime);
		next = string;
	      }

	    csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	    break;
	  }

	case DB_TYPE_VARBIT:
	case DB_TYPE_BIT:
	  if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	    {
	      return NULL;
	    }
	  next = db_string_bit (string, format, value);
	  csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	  break;

	case DB_TYPE_NUMERIC:
#if defined (ENABLE_UNUSED_FUNCTION)
	  if (csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT) != NO_ERROR)
	    {
	      return NULL;
	    }
	  next = db_string_numeric (string, format, value);
	  csect_exit (NULL, CSECT_CNV_FMT_LEXER);
	  break;
#endif
	case DB_TYPE_BLOB:
	case DB_TYPE_CLOB:
	case DB_TYPE_ERROR:
	case DB_TYPE_DB_VALUE:
	case DB_TYPE_OBJECT:
	case DB_TYPE_OID:
	case DB_TYPE_POINTER:
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	case DB_TYPE_SUB:
	case DB_TYPE_VARIABLE:
	case DB_TYPE_VOBJ:
	  co_signal (CNV_ERR_BAD_TYPE, CNV_ER_FMT_BAD_TYPE, pr_type_name (DB_VALUE_DOMAIN_TYPE (value)));
	  break;
	default:
	  co_signal (CNV_ERR_BAD_TYPE, CNV_ER_FMT_BAD_TYPE, "UNKNWON TYPE");
	  break;
	}
    }

  return next;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_string_to_value() - Return a value of the specified type by converting
 *    the value string in the given format. If an error occurs, then return NULL.
 *    This function is equivalent to db_string_value, except that the value
 *    is allocated automatically.
 * return:
 * string(in) :
 * format(in) :
 *
 * note : If type is DB_TYPE_MONETARY, then an additional DB_CURRENCY arg
 *        must be given. otherwise, optional args are ignored.
 *        The contents of the returned value must not be freed. The
 *        contents of the returned value may be changed by calls to other
 *        conversion functions.
 */
DB_VALUE *
db_string_to_value (const char *string, const char *format, DB_TYPE type, ...)
{
  va_list args;
  static DB_VALUE value;
  DB_CURRENCY currency;

  db_value_domain_init (&value, type, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  if (type == DB_TYPE_MONETARY)
    {
      va_start (args, type);
      currency = va_arg (args, DB_CURRENCY);
      db_make_monetary (&value, currency, 0.0);
      va_end (args);
    }

  return db_string_value (string, format, &value) ? &value : NULL;
}

/*
 * db_value_string() - Change the given string to a representation of
 *    the given value in the given format. If an error occurs, then the
 *    contents of the string are undefined and an error condition is returned.
 *    if max_size is not long enough to contain the new string, then an error
 *    is returned.
 * return:
 * value(in) :
 * format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in the string
 *               (including final '\0' char)
 *
 */
int
db_value_string (const DB_VALUE * value, const char *format, char *string, int max_size)
{
  int error = 0;
  int dummy;
  char *p;

  assert (value != NULL);
  assert (string != NULL);
  assert (max_size > 0);

  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_DATE:
      error = db_date_string (db_get_date (value), format, string, max_size);
      break;

    case DB_TYPE_NUMERIC:
      error = db_numeric_string (value, format, string, max_size);
      break;

    case DB_TYPE_DOUBLE:
      error = db_double_string (db_get_double (value), format, string, max_size);
      break;

    case DB_TYPE_FLOAT:
      error = db_float_string (db_get_float (value), format, string, max_size);
      break;

    case DB_TYPE_INTEGER:
      error = db_integer_string (db_get_int (value), format, string, max_size);
      break;
    case DB_TYPE_BIGINT:
      error = db_bigint_string (db_get_bigint (value), format, string, max_size);
      break;

    case DB_TYPE_MONETARY:
      error = db_monetary_string (db_get_monetary (value), format, string, max_size);
      break;

    case DB_TYPE_NULL:
      strcpy (string, "");
      break;

    case DB_TYPE_SHORT:
      error = db_short_string (db_get_short (value), format, string, max_size);
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      p = db_get_char (value, &dummy);

      if (p == NULL)
	{
	  break;
	}

      if ((int) strlen (p) + 1 > max_size)
	{
	  error = CNV_ERR_STRING_TOO_LONG;
	  co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
	}
      else
	{
	  strcpy (string, p);
	}
      break;

    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
      p = db_get_nchar (value, &dummy);
      if (p != NULL)
	{
	  if ((int) strlen (p) + 1 > max_size)
	    {
	      error = CNV_ERR_STRING_TOO_LONG;
	      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
	    }
	  else
	    {
	      strcpy (string, p);
	    }
	}
      break;

    case DB_TYPE_TIME:
      error = db_time_string (db_get_time (value), format, string, max_size);
      break;

    case DB_TYPE_TIMESTAMP:
      error = db_timestamp_string (db_get_timestamp (value), format, string, max_size);
      break;

    case DB_TYPE_DATETIME:
      error = db_datetime_string (db_get_datetime (value), format, string, max_size);
      break;
    case DB_TYPE_VARBIT:
    case DB_TYPE_BIT:
      error = db_bit_string (value, format, string, max_size);
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_ERROR:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_OBJECT:
    case DB_TYPE_OID:
    case DB_TYPE_POINTER:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_SUB:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_VOBJ:
      error = CNV_ERR_BAD_TYPE;
      co_signal (error, CNV_ER_FMT_BAD_TYPE, pr_type_name (DB_VALUE_DOMAIN_TYPE (value)));
      break;

    default:
      error = CNV_ERR_BAD_TYPE;
      co_signal (error, CNV_ER_FMT_BAD_TYPE, "UNKNWON TYPE");
      break;
    }

  return error;
}
#endif

#if !defined(SERVER_MODE)
#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * db_value_to_string() - Return a string representing the given value in
 *     the given format. If an error occurs, then return NULL.
 *     This function is equivalent to db_value_string, except that the value
 *     string is allocated automatically.
 * return:
 * value(in) :
 * format(in) :
 */
const char *
db_value_to_string (const DB_VALUE * value, const char *format)
{
  static char *buffer;
  static int max_size = 0;

  int error;

  assert (value != NULL);

  if (max_size == 0)
    {
      /* Initialize buffer. */
      max_size = 32;
      if ((buffer = (char *) malloc (max_size)) == NULL)
	{
	  return NULL;
	}
    }

  /* Reallocate buffer until big enough. */
  while ((error = db_value_string (value, format, buffer, max_size)) == CNV_ERR_STRING_TOO_LONG)
    {
      max_size += max_size / 2;	/* Grow by 1.5x */
      static char *const realloc_buffer = (char *) realloc (buffer, max_size);
      if (realloc_buffer == NULL)
	{
	  return NULL;
	}
      else
	{
	  buffer = realloc_buffer;
	}
    }

  return error ? NULL : (const char *) buffer;
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* !SERVER_MODE */

/*
 * db_string_date() - Change the date value to the result of converting
 *     the date string in the given format. Return a pointer to the first
 *     char of the string after the last value char. If an error occurs,
 *     then the value is unchanged and NULL is returned.
 * return:
 * date_string(in) :
 * date_format(in) :
 * the_date(out) :
 */
const char *
db_string_date (const char *date_string, const char *date_format, DB_DATE * the_date)
{
  const FMT_TOKEN *fmt_token;
  int month;
  int day;
  int year;
  int error = 0;
#if defined (ENABLE_UNUSED_FUNCTION)
  int wday;
  const char *value_string;
  int i;
#endif

  assert (the_date != NULL);
  assert (date_string != NULL);

  /* Initialize to given date. */
  db_date_decode (the_date, &month, &day, &year);

  /* Scan value string according to format. */
  for (fmt_token = tfmt_new (strlen (date_format) ? date_format : "%x"), cnv_fmt_analyze (date_string, FL_LOCAL_TIME);
       !error && fmt_token->type != FT_NONE; fmt_token++)
    {

      switch (fmt_token->type)
	{

	case FT_DATE:
	  error = fmt_date_value (fmt_token->text, &month, &day, &year);
	  break;

#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  /* Pattern string found in value string? */
	  value_string = cnv_fmt_next_token ();
	  if (!strncmp (fmt_token->text, value_string, fmt_token->length))
	    {

	      /* Yes, restart scan after pattern. */
	      cnv_fmt_analyze (value_string + fmt_token->length, FL_LOCAL_TIME);
	    }
	  else
	    {
	      /* No, signal error showing where mismatch occurs. */
	      for (i = fmt_token->length - 1; i > 0 && strncmp (fmt_token->text, value_string, i); i--);
	      error = CNV_ERR_BAD_PATTERN;
	      co_signal (error, CNV_ER_FMT_BAD_PATTERN, fmt_token->text + i, value_string - date_string + i);
	    }
	  break;

	case FT_YEAR:
	  error = fmt_year_value (fmt_token->text, &year);
	  break;

	case FT_MONTH:
	  error = fmt_month_value (fmt_token->text, &month);
	  break;

	case FT_MONTHDAY:
	  error = fmt_monthday_value (fmt_token->text, &day);
	  break;

	case FT_WEEKDAY:
	  error = fmt_weekday_value (fmt_token->text, &wday);
	  if (!error)
	    {
	      DB_DATE new_date = fmt_weekday_date (month, day, year, wday);
	      db_date_decode (&new_date, &month, &day, &year);
	    }
	  break;
#endif
	default:
	  assert (!"possible to get here");
	}
    }

  if (!error)
    {
      int m, d, y;

      /* Is this a bogus date like 9/31? */
      error = db_date_encode (the_date, month, day, year);
      if (error != NO_ERROR)
	{
	  error = CNV_ERR_UNKNOWN_DATE;
	  co_signal (error, CNV_ER_FMT_UNKNOWN_DATE, local_date_string (month, day, year));
	  goto function_end;
	}
      db_date_decode (the_date, &m, &d, &y);
      if (!(month == m && day == d && year == y))
	{
	  error = CNV_ERR_UNKNOWN_DATE;
	  co_signal (error, CNV_ER_FMT_UNKNOWN_DATE, local_date_string (month, day, year));
	}
    }

function_end:
  return error ? NULL : cnv_fmt_next_token ();
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_date_string() - Change the given string to a representation of
 *    the given date value in the given format. If an error occurs, then
 *    the contents of the string are undefined and an error condition is
 *    returned. if max_size is not long enough to contain the new date
 *    string, then an error is returned.
 * return:
 * the_date(in) :
 * date_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char).
 */
int
db_date_string (const DB_DATE * the_date, const char *date_format, char *string, int max_size)
{
  int error = 0;
  ADJ_ARRAY *buffer = cnv_get_value_string_buffer (0);
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  const char *value_string;
  int month, day, year, weekday;

  assert (the_date != NULL);
  assert (string != NULL);
  assert (max_size > 0);

  error = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);	/* before using lexer */
  if (error != NO_ERROR)
    {
      return error;
    }

  db_date_decode ((DB_DATE *) the_date, &month, &day, &year);

  /* Print according to format. */
  for (cnv_fmt_analyze (strlen (date_format) ? date_format : "%x", FL_TIME_FORMAT);
       (ttype = cnv_fmt_lex (&token)) != FT_NONE; adj_ar_append (buffer, value_string, strlen (value_string)))
    {

      switch (ttype)
	{
	case FT_DATE:
	  value_string = fmt_date_string (the_date, token.text);
	  break;

#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  value_string = token.text;
	  break;

	case FT_YEAR:
	  value_string = fmt_year_string (year, token.text);
	  break;
	case FT_MONTH:
	  value_string = fmt_month_string (month, token.text);
	  break;

	case FT_MONTHDAY:
	  value_string = fmt_monthday_string (day, token.text);
	  break;
	case FT_WEEKDAY:
	  weekday = db_date_weekday ((DB_DATE *) the_date);
	  value_string = fmt_weekday_string (weekday, token.text);
	  break;
#endif
	default:
	  assert (!"possible to get here");
	  value_string = "";
	  break;
	}
    }

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);	/* after using lexer */

  adj_ar_append (buffer, "", 1);

  /* Enough room to copy completed value string? */
  if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }
  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
    }

  return error;
}
#endif

/*
 * db_string_double() - Change the double value to the result of converting
 *     the double string in the given format. Return a pointer to the first
 *     char of the string after the last value char. If an error occurs, then
 *     the value is unchanged and NULL is returned.
 * return:
 * double_string(in) :
 * double_format(in) :
 * the_double(out) :
 */
const char *
db_string_double (const char *double_string, const char *double_format, double *the_double)
{
  FLOAT_FORMAT ffmt;

  assert (the_double != NULL);
  assert (double_string != NULL);

  ffmt_new (&ffmt, double_format);
  if (!strchr (double_string, '.'))
    {
      ffmt.decimal = 0;
    }
  return ffmt_value (&ffmt, double_string, the_double);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_double_string() - Change the given string to a representation of
 *    the given double value in the given format. If an error occurs, then
 *    the contents of the string are undefined and an error condition
 *    is returned. if max_size is not long enough to contain the new double
 *    string, then an error is returned.
 * return:
 * the_double(in) :
 * double_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char).
 */
int
db_double_string (double the_double, const char *double_format, char *string, int max_size)
{
  FLOAT_FORMAT ffmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  ffmt_new (&ffmt, double_format);
  r = ffmt_print (&ffmt, the_double, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}

/*
 * db_string_numeric() - Change the numeric value to the result of converting
 *     the numeric string in the given format. Return a pointer to the first
 *     char of the string after the last value char. If an error occurs, then
 *     the value is unchanged and NULL is returned.
 * return: a pointer to the first char of the string after the last value char.
 *   error code :
 *   CNV_ERR_MISSING_INTEGER        Not enough digits in integer part
 *   CNV_ERR_MISSING_FRACTION       Not enough digits in fraction part
 *   CNV_ERR_NO_DECIMAL             Missing decimal point
 *
 * numeric_string(in) :
 * numeric_format(in) :
 * the_numeric(out) :
 */
const char *
db_string_numeric (const char *numeric_string, const char *numeric_format, DB_VALUE * the_numeric)
{
  FLOAT_FORMAT ffmt;

  assert (the_numeric != NULL);
  assert (numeric_string != NULL);

  ffmt_new (&ffmt, numeric_format);
  return num_fmt_value (&ffmt, numeric_string, the_numeric);
}
#endif

/*
 * db_numeric_string() - Change the given string to a representation of
 *    the given numeric value in the given format. If an error occurs,
 *    then the contents of the string are undefined and an error condition
 *    is returned. if max_size is not long enough to contain the new numeric
 *    string, then an error is returned.
 * return:
 * the_numeric(in) :
 * numeric_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char).
 */
int
db_numeric_string (const DB_VALUE * the_numeric, const char *numeric_format, char *string, int max_size)
{
  FLOAT_FORMAT ffmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  ffmt_new (&ffmt, numeric_format);
  r = num_fmt_print (&ffmt, the_numeric, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}

/*
 * db_string_float() - Change the float value to the result of converting
 *    the float string in the given format. Return a pointer to the first
 *    char of the string after the last value char. If an error occurs,
 *    then the value is unchanged and NULL is returned.
 * return:
 *    error code:
 *   CNV_ERR_MISSING_INTEGER        Not enough digits in integer part
 *   CNV_ERR_MISSING_FRACTION       Not enough digits in fraction part
 *   CNV_ERR_NO_DECIMAL             Missing decimal point
 *   CNV_ERR_FLOAT_OVERFLOW         Float value too large
 *   CNV_ERR_FLOAT_UNDERFLOW        Float value too small
 *
 * float_string(in) :
 * float_format(in) :
 * the_float(out) :
 */
const char *
db_string_float (const char *float_string, const char *float_format, float *the_float)
{
  FLOAT_FORMAT ffmt;
  const char *endp;
  double the_double;

  assert (the_float != NULL);
  assert (float_string != NULL);

  ffmt_new (&ffmt, float_format);
  if (!strchr (float_string, '.'))
    {
      ffmt.decimal = 0;
    }
  endp = ffmt_value (&ffmt, float_string, &the_double);

  if (endp)
    {
      if (the_double > FLT_MAX)
	{
	  co_signal (CNV_ERR_FLOAT_OVERFLOW, CNV_ER_FMT_FLOAT_OVERFLOW, FLT_MAX);
	  endp = NULL;
	}
      else if (the_double < -FLT_MAX)
	{
	  co_signal (CNV_ERR_FLOAT_UNDERFLOW, CNV_ER_FMT_FLOAT_UNDERFLOW, -FLT_MAX);
	  endp = NULL;
	}
      else
	{
	  *the_float = (float) the_double;
	}
    }
  return endp;

}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_float_string() - Change the given string to a representation of the given
 *    float value in the given format. If an error occurs, then the contents of
 *    the string are undefined and an error condition is returned.
 *    if max_size is not long enough to contain the new float string, then an
 *    error is returned.
 * return:
 * the_float(in) :
 * float_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_float_string (float the_float, const char *float_format, char *string, int max_size)
{
  FLOAT_FORMAT ffmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  ffmt_new (&ffmt, float_format);
  r = ffmt_print (&ffmt, the_float, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}
#endif

/*
 * db_string_integer() - Change the integer value to the result of converting
 *    the integer string in the given format. Return a pointer to the first
 *    char of the string after the last value char. If an error occurs, then
 *    the value is unchanged and NULL is returned.
 * return:
 * integer_string(in) :
 * integer_format(in) :
 * the_integer(out) :
 */
const char *
db_string_integer (const char *integer_string, const char *integer_format, int *the_integer)
{
  INTEGER_FORMAT ifmt;

  assert (the_integer != NULL);
  assert (integer_string != NULL);

  ifmt_new (&ifmt, integer_format);
  return ifmt_value (&ifmt, integer_string, the_integer);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_integer_string() - Change the given string to a representation of the
 *    given integer value in the given format. If an error occurs, then the
 *    contents of the string are undefined and an error condition is returned.
 *    if max_size is not long enough to contain the new float string, then an
 *    error is returned.
 * return:
 * the_integer(in) :
 * integer_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_integer_string (int the_integer, const char *integer_format, char *string, int max_size)
{
  INTEGER_FORMAT ifmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  ifmt_new (&ifmt, integer_format);
  r = ifmt_print (&ifmt, the_integer, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}
#endif

/*
 * db_string_bigint() - Change the big integer value to the result of converting
 *    the integer string in the given format. Return a pointer to the first
 *    char of the string after the last value char. If an error occurs, then
 *    the value is unchanged and NULL is returned.
 * return:
 * bitint_string(in) :
 * bigint_format(in) :
 * the_bigint(out) :
 */
const char *
db_string_bigint (const char *bitint_string, const char *bigint_format, DB_BIGINT * the_bigint)
{
  INTEGER_FORMAT ifmt;

  assert (the_bigint != NULL);
  assert (bitint_string != NULL);

  ifmt_new (&ifmt, bigint_format);
  return bifmt_value (&ifmt, bitint_string, the_bigint);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_bigint_string() - Change the given string to a representation of the
 *    given big integer value in the given format. If an error occurs, then the
 *    contents of the string are undefined and an error condition is returned.
 *    if max_size is not long enough to contain the new float string, then an
 *    error is returned.
 * return:
 * the_bigint(in) :
 * bigint_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_bigint_string (DB_BIGINT the_bigint, const char *bigint_format, char *string, int max_size)
{
  INTEGER_FORMAT ifmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  ifmt_new (&ifmt, bigint_format);
  r = ifmt_print (&ifmt, the_bigint, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}
#endif

/*
 * db_string_monetary() - Change the monetary value to the result of converting
 *    the monetary string in the given format. Return a pointer to the first
 *    char of the string after the last value char. If an error occurs, then
 *    the value is unchanged and NULL is returned.
 * return:
 *    error code :
 *   CNV_ERR_NO_CURRENCY            Missing required currency symbol
 *   CNV_ERR_MISSING_INTEGER        Not enough digits in integer part
 *   CNV_ERR_MISSING_FRACTION       Not enough digits in fraction part
 *   CNV_ERR_NO_DECIMAL             Missing decimal point
 *
 * monetary_string(in) :
 * monetary_format(in) :
 * the_monetary(out) :
 *
 * note : the currency type of the_monetary determines the currency symbol
 *        allowed in the monetary_string.
 */
const char *
db_string_monetary (const char *monetary_string, const char *monetary_format, DB_MONETARY * the_monetary)
{
  MONETARY_FORMAT mfmt;

  assert (the_monetary != NULL);
  assert (monetary_string != NULL);
  assert (cnv_valid_currency (the_monetary->type));

  mfmt_new (&mfmt, monetary_format, the_monetary->type);
  return mfmt_value (&mfmt, monetary_string, &(the_monetary->amount));
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_monetary_string() - Change the given string to a representation of
 *  the given monetary value in the given format. If an error occurs, then
 *  the contents of the string are undefined and an error condition is
 *  returned.
 *  if max_size is not long enough to contain the new float string, then an
 *  error is returned.
 * return:
 * the_monetary(in) :
 * monetary_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_monetary_string (const DB_MONETARY * the_monetary, const char *monetary_format, char *string, int max_size)
{
  MONETARY_FORMAT mfmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  assert (cnv_valid_currency (the_monetary->type));

  mfmt_new (&mfmt, monetary_format, the_monetary->type);
  r = mfmt_print (&mfmt, the_monetary->amount, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}
#endif

/*
 * db_string_short() - Change the short value to the result of converting
 *    the short string in the given format. Return a pointer to the first
 *    char of the string after the last value char. If an error occurs,
 *    then the value is unchanged and NULL is returned.
 * return:
 *    error code :
 *   CNV_ERR_BAD_PATTERN           Value string not in expected format
 *   CNV_ERR_MISSING_INTEGER       Not enough digits
 *   CNV_ERR_INTEGER_OVER_FLOW     Value too large
 *   CNV_ERR_INTEGER_UNDER_FLOW    Value too small
 * short_string(in) :
 * short_format(in) :
 * the_short(out) :
 */
const char *
db_string_short (const char *short_string, const char *short_format, short *the_short)
{
  INTEGER_FORMAT ifmt;
  const char *endp;
  int the_integer;

  assert (the_short != NULL);
  assert (short_string != NULL);

  ifmt_new (&ifmt, short_format);
  endp = ifmt_value (&ifmt, short_string, &the_integer);

  if (endp)
    {
      if (the_integer > DB_INT16_MAX)
	{
	  co_signal (CNV_ERR_INTEGER_OVERFLOW, CNV_ER_FMT_INTEGER_OVERFLOW, DB_INT16_MAX);
	  endp = NULL;
	}
      else if (the_integer < DB_INT16_MIN)
	{
	  co_signal (CNV_ERR_INTEGER_UNDERFLOW, CNV_ER_FMT_INTEGER_UNDERFLOW, DB_INT16_MIN);
	  endp = NULL;
	}
      else
	{
	  *the_short = (short) the_integer;
	}
    }

  return endp;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_short_string() - Change the given string to a representation of the given
 *    short value in the given format. If an error occurs, then the contents of
 *    the string are undefined and an error condition is returned.
 *    if max_size is not long enough to contain the new float string, then an
 *    error is returned.
 * return:
 * the_short(in) :
 * short_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_short_string (short the_short, const char *short_format, char *string, int max_size)
{
  INTEGER_FORMAT ifmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  ifmt_new (&ifmt, short_format);
  r = ifmt_print (&ifmt, the_short, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}
#endif

/*
 * db_string_time() - Change the time value to the result of converting
 *    the time string in the given format. Return a pointer to the first
 *    char of the string after the last value char. If an error occurs,
 *    then the value is unchanged and NULL is returned.
 * return:
 *    error code:
 *   CNV_ERR_BAD_PATTERN           Value string not in expected format
 *   CNV_ERR_BAD_TIME              Missing or invalid time
 *   CNV_ERR_BAD_HOUR              Missing or invalid hour
 *   CNV_ERR_BAD_MIN               Missing or invalid minute
 *   CNV_ERR_BAD_SEC               Missing or invalid second
 *   CNV_ERR_BAD_AM_PM             Missing or invalid AM/PM
 * time_string(in) :
 * time_format(in) :
 * the_time(out) :
 */
const char *
db_string_time (const char *time_string, const char *time_format, DB_TIME * the_time)
{
  const FMT_TOKEN *fmt_token;
  int hour;
  int min;
  int sec;
  bool pm = false;
  bool new_hour = false;
  int hrs = 24;
  int error = 0;
#if defined (ENABLE_UNUSED_FUNCTION)
  const char *value_string;
  int i;
#endif

  assert (the_time != NULL);
  assert (time_string != NULL);

  /* Initialize to given time. */
  db_time_decode (the_time, &hour, &min, &sec);

  /* Scan value string according to format. */
  for (fmt_token = tfmt_new (strlen (time_format) ? time_format : "%X"), cnv_fmt_analyze (time_string, FL_LOCAL_TIME);
       !error && fmt_token->type != FT_NONE; fmt_token++)
    {
      switch (fmt_token->type)
	{
	case FT_TIME:
	  error = fmt_time_value (fmt_token->text, &hour, &min, &sec);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;

#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  /* Pattern string found in value string? */
	  value_string = cnv_fmt_next_token ();
	  if (!strncmp (fmt_token->text, value_string, fmt_token->length))
	    {
	      /* Yes, restart scan after pattern. */
	      cnv_fmt_analyze (value_string + fmt_token->length, FL_LOCAL_TIME);
	    }
	  else
	    {
	      /* No, signal error showing where mismatch occurs. */
	      for (i = fmt_token->length - 1; i > 0 && strncmp (fmt_token->text, value_string, i); i--);
	      error = CNV_ERR_BAD_PATTERN;
	      co_signal (error, CNV_ER_FMT_BAD_PATTERN, fmt_token->text + i, value_string - time_string + i);
	    }
	  break;

	case FT_SECOND:
	  error = fmt_second_value (fmt_token->text, &sec);
	  break;

	case FT_HOUR:
	  error = fmt_hour_value (fmt_token->text, &hour);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;

	case FT_MINUTE:
	  error = fmt_minute_value (fmt_token->text, &min);
	  break;

	case FT_ZONE:
	  /* Not currently supported -- ignore this */
	  break;

	case FT_AM_PM:
	  error = local_am_pm_value (&pm);
	  if (!error)
	    {
	      hrs = 12;
	    }
	  break;
#endif
	default:
	  assert (!"possible to get here");
	}
    }

  if (!error)
    {
      /* Hour consistent with AM/PM? */
      if (hrs == 12 && new_hour && (hour > 12 || hour < 1))
	{
	  error = CNV_ERR_BAD_HOUR;
	  co_signal (error, CNV_ER_FMT_BAD_HOUR, "p");
	}
      else
	{
	  db_time_encode (the_time, hrs == 12 && pm ? hour % 12 + 12 : hour, min, sec);
	}
    }

  return error ? NULL : cnv_fmt_next_token ();
}

/*
 * db_time_string() - Change the given string to a representation of the given
 *    time value in the given format. If an error occurs, then the contents of
 *    the string are undefined and an error condition is returned.
 *    if max_size is not long enough to contain the new float string, then an
 *    error is returned.
 * return:
 * the_time(in) :
 * time_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_time_string (const DB_TIME * the_time, const char *time_format, char *string, int max_size)
{
  int error = 0;
  ADJ_ARRAY *buffer = cnv_get_value_string_buffer (0);
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  const char *value_string;

  assert (the_time != NULL);
  assert (string != NULL);
  assert (max_size > 0);

  error = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Print according to format. */
  for (cnv_fmt_analyze (strlen (time_format) ? time_format : "%X", FL_TIME_FORMAT);
       (ttype = cnv_fmt_lex (&token)) != FT_NONE; adj_ar_append (buffer, value_string, strlen (value_string)))
    {

      switch (ttype)
	{
	case FT_TIME:
	  value_string = fmt_time_string (the_time, token.text);
	  break;

#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  value_string = token.text;
	  break;

	case FT_SECOND:
	  value_string = fmt_second_string (the_time, token.text);
	  break;

	case FT_HOUR:
	  value_string = fmt_hour_string (the_time, token.text);
	  break;

	case FT_MINUTE:
	  value_string = fmt_minute_string (the_time, token.text);
	  break;

	case FT_ZONE:
	  /* Not currently supported -- ignore this */
	  value_string = "";
	  break;

	case FT_AM_PM:
	  value_string = local_am_pm_string (the_time);
	  break;
#endif
	default:
	  assert (!"possible to get here");
	  value_string = "";
	  break;
	}
    }

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  adj_ar_append (buffer, "", 1);

  /* Enough room to copy completed value string? */
  if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }
  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
    }

  return error;
}

/*
 * db_string_timestamp() - Change the timestamp value to the result of
 *    converting the timestamp string in the given format. Return a pointer
 *    to the first char of the string after the last value char. If an error
 *    occurs, then the value is unchanged and NULL is returned.
 * return:
 *    error code:
 *   CNV_ERR_BAD_PATTERN           Value string not in expected format
 *   CNV_ERR_BAD_TIME              Missing or invalid time
 *   CNV_ERR_BAD_HOUR              Missing or invalid hour
 *   CNV_ERR_BAD_MIN               Missing or invalid minute
 *   CNV_ERR_BAD_SEC               Missing or invalid second
 *   CNV_ERR_BAD_AM_PM             Missing or invalid AM/PM
 *   CNV_ERR_BAD_DATE              Missing or invalid date
 *   CNV_ERR_BAD_YEAR              Missing or invalid year
 *   CNV_ERR_BAD_MONTH             Missing or invalid month
 *   CNV_ERR_BAD_MDAY              Missing or invalid month day
 *   CNV_ERR_BAD_WDAY              Missing or invalid week day
 * timestamp_string(in) :
 * timestamp_format(in) :
 * the_timestamp(out) :
 */
const char *
db_string_timestamp (const char *timestamp_string, const char *timestamp_format, DB_TIMESTAMP * the_timestamp)
{
  const FMT_TOKEN *fmt_token;
  DB_DATE the_date;
  int month;
  int day;
  int year;
  DB_TIME the_time;
  int hour;
  int min;
  int sec;
  bool pm = false;
  bool new_hour = false;
  int hrs = 24;
  int error = 0;
#if defined (ENABLE_UNUSED_FUNCTION)
  int wday;
  const char *value_string;
  int i;
#endif

  assert (the_timestamp != NULL);
  assert (timestamp_string != NULL);

  /* Initialize to given timestamp. */
  (void) db_timestamp_decode_ses (the_timestamp, &the_date, &the_time);
  db_date_decode (&the_date, &month, &day, &year);
  db_time_decode (&the_time, &hour, &min, &sec);

  /* Scan value string according to format. */
  for (fmt_token =
       tfmt_new (strlen (timestamp_format) ? timestamp_format : "%c"), cnv_fmt_analyze (timestamp_string,
											FL_LOCAL_TIME);
       !error && fmt_token->type != FT_NONE; fmt_token++)
    {

      switch (fmt_token->type)
	{
	case FT_TIMESTAMP:
	  error = fmt_timestamp_value (fmt_token->text, &month, &day, &year, &hour, &min, &sec);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;

#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  /* Pattern string found in value string? */
	  value_string = cnv_fmt_next_token ();
	  if (!strncmp (fmt_token->text, value_string, fmt_token->length))
	    {

	      /* Yes, restart scan after pattern. */
	      cnv_fmt_analyze (value_string + fmt_token->length, FL_LOCAL_TIME);
	    }
	  else
	    {
	      /* No, signal error showing where mismatch occurs. */
	      for (i = fmt_token->length - 1; i > 0 && strncmp (fmt_token->text, value_string, i); i--)
		;
	      error = CNV_ERR_BAD_PATTERN;
	      co_signal (error, CNV_ER_FMT_BAD_PATTERN, fmt_token->text + i, value_string - timestamp_string + i);
	    }
	  break;

	case FT_DATE:
	  error = fmt_date_value (fmt_token->text, &month, &day, &year);
	  break;

	case FT_YEAR:
	  error = fmt_year_value (fmt_token->text, &year);
	  break;

	case FT_MONTH:
	  error = fmt_month_value (fmt_token->text, &month);
	  break;

	case FT_MONTHDAY:
	  error = fmt_monthday_value (fmt_token->text, &day);
	  break;

	case FT_WEEKDAY:
	  error = fmt_weekday_value (fmt_token->text, &wday);
	  if (!error)
	    {
	      DB_DATE new_date = fmt_weekday_date (month, day, year, wday);
	      db_date_decode (&new_date, &month, &day, &year);
	    }
	  break;

	case FT_TIME:
	  error = fmt_time_value (fmt_token->text, &hour, &min, &sec);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;

	case FT_SECOND:
	  error = fmt_second_value (fmt_token->text, &sec);
	  break;

	case FT_HOUR:
	  error = fmt_hour_value (fmt_token->text, &hour);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;

	case FT_MINUTE:
	  error = fmt_minute_value (fmt_token->text, &min);
	  break;

	case FT_ZONE:
	  /* Not currently supported */
	  break;

	case FT_AM_PM:
	  error = local_am_pm_value (&pm);
	  if (!error)
	    {
	      hrs = 12;
	    }
	  break;
#endif
	default:
	  assert (!"possible to get here");
	}
    }

  if (!error)
    {
      int m, d, y;

      error = db_date_encode (&the_date, month, day, year);
      if (error != NO_ERROR)
	{
	  error = CNV_ERR_UNKNOWN_DATE;
	  co_signal (error, CNV_ER_FMT_UNKNOWN_DATE, local_date_string (month, day, year));
	  goto function_end;
	}
      error = db_time_encode (&the_time, (hrs == 12 && pm ? hour % 12 + 12 : hour), min, sec);
      if (error != NO_ERROR)
	{
	  error = CNV_ERR_UNKNOWN_DATE;
	  co_signal (error, CNV_ER_FMT_UNKNOWN_DATE, local_date_string (month, day, year));
	  goto function_end;
	}

      /* Is this a bogus date like 9/31? */
      db_date_decode (&the_date, &m, &d, &y);
      if (!(month == m && day == d && year == y))
	{
	  error = CNV_ERR_UNKNOWN_DATE;
	  co_signal (error, CNV_ER_FMT_UNKNOWN_DATE, local_date_string (month, day, year));
	}
      /* Hour consistent with AM/PM? */
      else if (hrs == 12 && new_hour && (hour > 12 || hour < 1))
	{
	  error = CNV_ERR_BAD_HOUR;
	  co_signal (error, CNV_ER_FMT_BAD_HOUR, fmt_token->text);
	}
      else
	{
	  (void) db_timestamp_encode_ses (&the_date, &the_time, the_timestamp, NULL);
	}
    }

function_end:
  return (error ? NULL : cnv_fmt_next_token ());
}

/*
 * db_timestamp_string() - Change the given string to a representation of
 *    the given timestamp value in the given format. If an error occurs,
 *    then the contents of the string are undefined and an error condition
 *    is returned. if max_size is not long enough to contain the new float
 *    string, then an error is returned.
 * return:
 * the_timestamp(in) :
 * timestamp_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_timestamp_string (const DB_TIMESTAMP * the_timestamp, const char *timestamp_format, char *string, int max_size)
{
  int error = 0;
  ADJ_ARRAY *buffer = cnv_get_value_string_buffer (0);
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  const char *value_string;
  DB_DATE the_date;
  DB_TIME the_time;
  int month, day, year;
  int hour, minute, second;
#if defined (ENABLE_UNUSED_FUNCTION)
  int weekday;
#endif

  assert (the_timestamp != NULL);
  assert (string != NULL);
  assert (max_size > 0);

  error = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Reject timestamp encoding errors. */
  (void) db_timestamp_decode_ses ((DB_TIMESTAMP *) the_timestamp, &the_date, &the_time);
  db_date_decode (&the_date, &month, &day, &year);
  db_time_decode (&the_time, &hour, &minute, &second);

  /* Print according to format. */
  for (cnv_fmt_analyze (strlen (timestamp_format) ? timestamp_format : "%c", FL_TIME_FORMAT);
       (ttype = cnv_fmt_lex (&token)) != FT_NONE; adj_ar_append (buffer, value_string, strlen (value_string)))
    {

      switch (ttype)
	{
	case FT_TIMESTAMP:
	  value_string = fmt_timestamp_string (the_timestamp, token.text);
	  break;

#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  value_string = token.text;
	  break;

	case FT_DATE:
	  value_string = fmt_date_string (&the_date, token.text);
	  break;

	case FT_YEAR:
	  value_string = fmt_year_string (year, token.text);
	  break;
	case FT_MONTH:
	  value_string = fmt_month_string (month, token.text);
	  break;

	case FT_MONTHDAY:
	  value_string = fmt_monthday_string (day, token.text);
	  break;
	case FT_WEEKDAY:
	  weekday = db_date_weekday ((DB_DATE *) (&the_date));
	  value_string = fmt_weekday_string (weekday, token.text);
	  break;

	case FT_TIME:
	  value_string = fmt_time_string (&the_time, token.text);
	  break;

	case FT_SECOND:
	  value_string = fmt_second_string (&the_time, token.text);
	  break;

	case FT_HOUR:
	  value_string = fmt_hour_string (&the_time, token.text);
	  break;

	case FT_MINUTE:
	  value_string = fmt_minute_string (&the_time, token.text);
	  break;

	case FT_ZONE:
	  /* Not currently supported -- ignore this */
	  value_string = "";
	  break;

	case FT_AM_PM:
	  value_string = local_am_pm_string (&the_time);
	  break;
#endif
	default:
	  assert (!"possible to get here");
	  value_string = "";
	  break;
	}
    }

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  adj_ar_append (buffer, "", 1);

  /* Enough room to copy completed value string? */
  if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }
  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
    }

  return error;
}

/*
 * db_string_datetime() -
 * return:
 *    error code:
 *   CNV_ERR_BAD_PATTERN           Value string not in expected format
 *   CNV_ERR_BAD_TIME              Missing or invalid time
 *   CNV_ERR_BAD_HOUR              Missing or invalid hour
 *   CNV_ERR_BAD_MIN               Missing or invalid minute
 *   CNV_ERR_BAD_SEC               Missing or invalid second
 *   CNV_ERR_BAD_AM_PM             Missing or invalid AM/PM
 *   CNV_ERR_BAD_DATE              Missing or invalid date
 *   CNV_ERR_BAD_YEAR              Missing or invalid year
 *   CNV_ERR_BAD_MONTH             Missing or invalid month
 *   CNV_ERR_BAD_MDAY              Missing or invalid month day
 *   CNV_ERR_BAD_WDAY              Missing or invalid week day
 * timestamp_string(in) :
 * timestamp_format(in) :
 * the_timestamp(out) :
 */
const char *
db_string_datetime (const char *datetime_string, const char *datetime_format, DB_DATETIME * the_datetime)
{
  const FMT_TOKEN *fmt_token;
  DB_DATETIME tmp_datetime;
  int month;
  int day;
  int year;
  int hour;
  int min;
  int sec;
  int msec;
  bool pm = false;
  bool new_hour = false;
  int hrs = 24;
  int error = 0;
#if defined (ENABLE_UNUSED_FUNCTION)
  int wday;
  const char *value_string;
  int i;
#endif

  assert (the_datetime != NULL);
  assert (datetime_string != NULL);

  /* Initialize to given datetime. */
  db_datetime_decode (the_datetime, &month, &day, &year, &hour, &min, &sec, &msec);

  /* Scan value string according to format. */
  for (fmt_token =
       tfmt_new (strlen (datetime_format) ? datetime_format : "%c"), cnv_fmt_analyze (datetime_string, FL_LOCAL_TIME);
       !error && fmt_token->type != FT_NONE; fmt_token++)
    {
      switch (fmt_token->type)
	{
	case FT_TIMESTAMP:
	  error = fmt_timestamp_value (fmt_token->text, &month, &day, &year, &hour, &min, &sec);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;

#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  /* Pattern string found in value string? */
	  value_string = cnv_fmt_next_token ();
	  if (!strncmp (fmt_token->text, value_string, fmt_token->length))
	    {

	      /* Yes, restart scan after pattern. */
	      cnv_fmt_analyze (value_string + fmt_token->length, FL_LOCAL_TIME);
	    }
	  else
	    {
	      /* No, signal error showing where mismatch occurs. */
	      for (i = fmt_token->length - 1; i > 0 && strncmp (fmt_token->text, value_string, i); i--);
	      error = CNV_ERR_BAD_PATTERN;
	      co_signal (error, CNV_ER_FMT_BAD_PATTERN, fmt_token->text + i, value_string - datetime_string + i);
	    }
	  break;
	case FT_DATE:
	  error = fmt_date_value (fmt_token->text, &month, &day, &year);
	  break;
	case FT_YEAR:
	  error = fmt_year_value (fmt_token->text, &year);
	  break;
	case FT_MONTH:
	  error = fmt_month_value (fmt_token->text, &month);
	  break;
	case FT_MONTHDAY:
	  error = fmt_monthday_value (fmt_token->text, &day);
	  break;
	case FT_WEEKDAY:
	  error = fmt_weekday_value (fmt_token->text, &wday);
	  if (!error)
	    {
	      DB_DATE new_date = fmt_weekday_date (month, day, year, wday);
	      db_date_decode (&new_date, &month, &day, &year);
	    }
	  break;
	case FT_TIME:
	  error = fmt_time_value (fmt_token->text, &hour, &min, &sec);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;
	case FT_SECOND:
	  error = fmt_second_value (fmt_token->text, &sec);
	  break;
	case FT_HOUR:
	  error = fmt_hour_value (fmt_token->text, &hour);
	  if (!error)
	    {
	      new_hour = true;
	    }
	  break;
	case FT_MINUTE:
	  error = fmt_minute_value (fmt_token->text, &min);
	  break;
	case FT_ZONE:
	  /* Not currently supported */
	  break;
	case FT_AM_PM:
	  error = local_am_pm_value (&pm);
	  if (!error)
	    {
	      hrs = 12;
	    }
	  break;
#endif
	default:
	  assert (!"possible to get here");
	}
    }

  if (!error)
    {
      int m, d, y, hh, mm, ss, ms;
      db_datetime_encode (&tmp_datetime, month, day, year, (hrs == 12 && pm ? hour % 12 + 12 : hour), min, sec, msec);
      /* Is this a bogus date like 9/31? */
      db_datetime_decode (&tmp_datetime, &m, &d, &y, &hh, &mm, &ss, &ms);
      if (!(month == m && day == d && year == y))
	{
	  error = CNV_ERR_UNKNOWN_DATE;
	  co_signal (error, CNV_ER_FMT_UNKNOWN_DATE, local_date_string (month, day, year));
	}
      /* Hour consistent with AM/PM? */
      else if (hrs == 12 && new_hour && (hour > 12 || hour < 1))
	{
	  error = CNV_ERR_BAD_HOUR;
	  co_signal (error, CNV_ER_FMT_BAD_HOUR, fmt_token->text);
	}
      else
	{
	  *the_datetime = tmp_datetime;
	}
    }

  return (error ? NULL : cnv_fmt_next_token ());
}

/*
 * db_datetime_string() -
 * return:
 * the_datetime(in) :
 * datetime_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_datetime_string (const DB_DATETIME * the_datetime, const char *datetime_format, char *string, int max_size)
{
  int error = 0;
  ADJ_ARRAY *buffer = cnv_get_value_string_buffer (0);
  FMT_TOKEN_TYPE ttype;
  FMT_TOKEN token;
  const char *value_string;
  DB_DATE the_date;
  DB_TIMESTAMP the_timestamp;
  int month, day, year;
  int hour, minute, second, millisecond;
#if defined (ENABLE_UNUSED_FUNCTION)
  int weekday;
#endif
  unsigned int the_time;

  assert (the_datetime != NULL);
  assert (string != NULL);
  assert (max_size > 0);

  error = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Reject datetime encoding errors. */
  db_datetime_decode ((DB_DATETIME *) the_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

  /* Print according to format. */
  for (cnv_fmt_analyze ((strlen (datetime_format) ? datetime_format : "%c"), FL_TIME_FORMAT);
       (ttype = cnv_fmt_lex (&token)) != FT_NONE; adj_ar_append (buffer, value_string, strlen (value_string)))
    {
      switch (ttype)
	{
	case FT_TIMESTAMP:
	  db_date_encode (&the_date, month, day, year);
	  db_time_encode (&the_time, hour, minute, second);
	  (void) db_timestamp_encode_ses (&the_date, &the_time, &the_timestamp, NULL);

	  value_string = fmt_timestamp_string (&the_timestamp, token.text);
	  break;
#if defined (ENABLE_UNUSED_FUNCTION)
	case FT_PATTERN:
	  value_string = token.text;
	  break;
	case FT_DATE:
	  db_date_encode (&the_date, month, day, year);
	  value_string = fmt_date_string (&the_date, token.text);
	  break;
	case FT_YEAR:
	  value_string = fmt_year_string (year, token.text);
	  break;
	case FT_MONTH:
	  value_string = fmt_month_string (month, token.text);
	  break;
	case FT_MONTHDAY:
	  value_string = fmt_monthday_string (day, token.text);
	  break;
	case FT_WEEKDAY:
	  db_date_encode (&the_date, month, day, year);
	  weekday = db_date_weekday ((DB_DATE *) (&the_date));
	  value_string = fmt_weekday_string (weekday, token.text);
	  break;
	case FT_TIME:
	  db_time_encode (&the_time, hour, minute, second);
	  value_string = fmt_time_string (&the_time, token.text);
	  break;
	case FT_SECOND:
	  db_time_encode (&the_time, hour, minute, second);
	  value_string = fmt_second_string (&the_time, token.text);
	  break;
	case FT_MILLISECOND:
	  db_time_encode (&the_time, hour, minute, second);
	  value_string = fmt_second_string (&the_time, token.text);
	  break;
	case FT_HOUR:
	  db_time_encode (&the_time, hour, minute, second);
	  value_string = fmt_hour_string (&the_time, token.text);
	  break;
	case FT_MINUTE:
	  db_time_encode (&the_time, hour, minute, second);
	  value_string = fmt_minute_string (&the_time, token.text);
	  break;
	case FT_ZONE:
	  /* Not currently supported -- ignore this */
	  value_string = "";
	  break;
	case FT_AM_PM:
	  db_time_encode (&the_time, hour, minute, second);
	  value_string = local_am_pm_string (&the_time);
	  break;
#endif
	default:
	  assert (!"possible to get here");
	  value_string = "";
	  break;
	}
    }

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);
  adj_ar_append (buffer, "", 1);
  /* Enough room to copy completed value string? */
  if ((int) strlen ((char *) adj_ar_get_buffer (buffer)) >= max_size)
    {
      error = CNV_ERR_STRING_TOO_LONG;
      co_signal (error, CNV_ER_FMT_STRING_TOO_LONG, max_size - 1);
    }
  else
    {
      strcpy (string, (char *) adj_ar_get_buffer (buffer));
    }

  return error;
}

/*
 * db_string_bit() -
 * return:
 * bit_char_string(in) :
 * bit_format(in) :
 * the_db_bit(out) :
 */
const char *
db_string_bit (const char *bit_char_string, const char *bit_format, DB_VALUE * the_db_bit)
{
  BIT_STRING_FORMAT bfmt;

  assert (bit_char_string != NULL);
  assert (the_db_bit != NULL);

  bfmt_new (&bfmt, bit_format);
  return bfmt_value (bfmt, bit_char_string, the_db_bit);
}

/*
 * db_bit_string() - Change the given string to a representation of
 *    the given bit value in the given format. If an error occurs, then
 *    the contents of the string are undefined and an error condition is
 *    returned.
 *    if max_size is not long enough to contain the new float string, then an
 *    error is returned.
 * return:
 * the_db_bit(in) :
 * bit_format(in) :
 * string(out) :
 * max_size(in) : the maximum number of chars that can be stored in
 *   the string (including final '\0' char)
 */
int
db_bit_string (const DB_VALUE * the_db_bit, const char *bit_format, char *string, int max_size)
{
  BIT_STRING_FORMAT bfmt;
  int r;

  assert (string != NULL);
  assert (max_size > 0);

  r = csect_enter (NULL, CSECT_CNV_FMT_LEXER, INF_WAIT);
  if (r != NO_ERROR)
    {
      return r;
    }

  bfmt_new (&bfmt, bit_format);
  r = bfmt_print (&bfmt, the_db_bit, string, max_size);

  csect_exit (NULL, CSECT_CNV_FMT_LEXER);

  return r;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_validate_format() - If the given format string is valid for the given
 *   data type, then return 0. otherwise, signal and return an error condition.
 * return:
 * format(in) :
 * type(in) :
 */
int
db_validate_format (const char *format, DB_TYPE type)
{
  int error = 0;

  assert (type == DB_TYPE_STRING || type == DB_TYPE_NULL || format != NULL);

  switch (type)
    {
    case DB_TYPE_DATE:
      error = fmt_validate (format, FL_VALIDATE_DATE_FORMAT, FT_DATE_FORMAT, DB_TYPE_DATE);
      break;

    case DB_TYPE_DOUBLE:
    case DB_TYPE_FLOAT:
    case DB_TYPE_NUMERIC:
      error = fmt_validate (format, FL_VALIDATE_FLOAT_FORMAT, FT_FLOAT_FORMAT, DB_TYPE_FLOAT);
      break;

    case DB_TYPE_BIGINT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_SHORT:
      error = fmt_validate (format, FL_VALIDATE_INTEGER_FORMAT, FT_INTEGER_FORMAT, DB_TYPE_INTEGER);
      break;

    case DB_TYPE_MONETARY:
      error = fmt_validate (format, FL_VALIDATE_MONETARY_FORMAT, FT_MONETARY_FORMAT, DB_TYPE_MONETARY);
      break;

    case DB_TYPE_TIME:
      error = fmt_validate (format, FL_VALIDATE_TIME_FORMAT, FT_TIME_FORMAT, DB_TYPE_TIME);
      break;

    case DB_TYPE_TIMESTAMP:
      error = fmt_validate (format, FL_VALIDATE_TIMESTAMP_FORMAT, FT_TIMESTAMP_FORMAT, DB_TYPE_TIMESTAMP);
      break;

      /* TODO:DATETIME datetime format ?? */
    case DB_TYPE_DATETIME:
      error = fmt_validate (format, FL_VALIDATE_TIMESTAMP_FORMAT, FT_TIMESTAMP_FORMAT, DB_TYPE_DATETIME);
      break;

    case DB_TYPE_NULL:
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      break;

    case DB_TYPE_VARBIT:
    case DB_TYPE_BIT:
      error = fmt_validate (format, FL_VALIDATE_BIT_STRING_FORMAT, FT_BIT_STRING_FORMAT, DB_TYPE_BIT);
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_ERROR:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_OBJECT:
    case DB_TYPE_OID:
    case DB_TYPE_POINTER:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_SUB:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_VOBJ:
      error = CNV_ERR_BAD_TYPE;
      co_signal (error, CNV_ER_FMT_BAD_TYPE, pr_type_name (type));
      break;

    default:
      assert (!"Valid DB_TYPE");
      break;
    }

  return error;
}
#endif

/*
 * cnv_cleanup() - This function is called when the database connection is shut
 *   down so that anything we allocated by the cnv_ module and maintained in
 *   static variables can be reclaimed.
 * return:
 */
void
cnv_cleanup (void)
{
  cnv_fmt_exit ();
  cnvutil_cleanup ();
}

#if defined (SERVER_MODE)
/*
 * cnv_get_thread_local_adj_buffer() -
 *   return:
 *   idx(in):
 */
static ADJ_ARRAY *
cnv_get_thread_local_adj_buffer (int idx)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  return thread_p->cnv_adj_buffer[idx];
}

/*
 * cnv_set_thread_local_adj_buffer() -
 *   return: void
 *   idx(in):
 *   buffer_p(in):
 */
static void
cnv_set_thread_local_adj_buffer (int idx, ADJ_ARRAY * buffer_p)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  thread_p->cnv_adj_buffer[idx] = buffer_p;
}
#endif // SERVER_MODE
