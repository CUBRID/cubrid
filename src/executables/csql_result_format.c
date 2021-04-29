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
 * csql_result_format.c : string formatting function
 */

#ident "$Id$"

#include "config.h"

#include <float.h>
#include <time.h>

#include "csql.h"
#include "cnv.h"
#include "memory_alloc.h"
#include "language_support.h"
#include "string_opfunc.h"
#include "unicode_support.h"

#include "dbtype.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#ifndef UX_CHAR
#define UX_CHAR wchar_t
#endif

#define CMSB(x) ((x) & 0x80)
	/* Checks if MSB of the character value is ON/OFF */
#define FORMERBYTE(x)   ((UX_CHAR)(((unsigned)(x) & 0xff00) >> 8))
#define LATTERBYTE(x)   ((UX_CHAR)((x) & 0xff))

#define COMMAS_OFFSET(COND, N)   (COND == TRUE ? (N / 3) : 0)

#define TIME_STRING_MAX         20

#define OID_LENGTH      15

#define COMMA_CHAR      ','

#define SHORT_TO_INT(short_val)          ((int)short_val)
#define OBJECT_SYMBOL_MAX             512

#define DATE_ABREV_NAME_LENGTH       3
#define DATE_TEMP_BUFFER_LENGTH 50

/*
 * double, float, numeric type conversion profile
 */
typedef struct db_type_double_profile DB_TYPE_DOUBLE_PROFILE;
typedef struct db_type_double_profile DB_TYPE_FLOAT_PROFILE;
typedef struct db_type_double_profile DB_TYPE_NUMERIC_PROFILE;

struct db_type_double_profile
{
  char format;			/* Use the following macros */
  int fieldwidth;		/* the width of the entire return string */
  int precision;		/* how many places after the decimal point */
  bool leadingsign;		/* whether or not to print '+' for positive numbers */
  bool leadingzeros;		/* whether or not to print leading zeros */
  bool trailingzeros;		/* whether or not to print trailing zeros */
  bool commas;			/* whether or not to print commas */
};

/* double conversion 'format' macros */
#define DOUBLE_FORMAT_SCIENTIFIC   'e'
#define DOUBLE_FORMAT_DECIMAL      'f'

static DB_TYPE_DOUBLE_PROFILE default_double_profile = {
  DOUBLE_FORMAT_SCIENTIFIC, 0, DBL_DIG, false, false, true, false
};

static DB_TYPE_FLOAT_PROFILE default_float_profile = {
  DOUBLE_FORMAT_SCIENTIFIC, 0, FLT_DIG, false, false, true, false
};

static DB_TYPE_NUMERIC_PROFILE default_numeric_profile = {
  DOUBLE_FORMAT_DECIMAL, -1, -1, false, false, true, true
};


/*
 * integer, short type conversion profile
 */
typedef struct db_type_integer_profile DB_TYPE_BIGINT_PROFILE;
typedef struct db_type_integer_profile DB_TYPE_INTEGER_PROFILE;
typedef struct db_type_integer_profile DB_TYPE_SHORT_PROFILE;

struct db_type_integer_profile
{
  char format;			/* Use the following macros */
  int fieldwidth;		/* the width of the entire return string */
  bool leadingsymbol;		/* whether or not to print the leading symbol */
  bool leadingzeros;		/* whether or not to print leading zeros */
  bool commas;			/* whether or not to print commas */
};

/* integer conversion 'format' macros */
#define INT_FORMAT_UNSIGNED_DECIMAL     'u'
#define INT_FORMAT_SIGNED_DECIMAL       'd'
#define INT_FORMAT_OCTAL                'o'
#define INT_FORMAT_HEXADECIMAL          'x'
#define INT_FORMAT_CHARACTER            'c'

static DB_TYPE_BIGINT_PROFILE default_bigint_profile = {
  INT_FORMAT_SIGNED_DECIMAL, 0, false, false, false
};

static DB_TYPE_INTEGER_PROFILE default_int_profile = {
  INT_FORMAT_SIGNED_DECIMAL, 0, false, false, false
};

static DB_TYPE_SHORT_PROFILE default_short_profile = {
  INT_FORMAT_SIGNED_DECIMAL, 0, false, false, false
};


/*
 * monetary type conversion profile
 */

typedef struct
{
  int fieldwidth;		/* the width of the entire return string */
  int decimalplaces;		/* how many places after the decimal point */
  bool leadingsign;		/* whether or not to print + symbol */
  bool currency_symbol;		/* whether or not to print currency symbol */
  bool leadingzeros;		/* whether or not to print leading zeros */
  bool trailingzeros;		/* whether or not to print trailing zeros */
  bool commas;			/* whether or not to print commas */
} DB_TYPE_MONETARY_PROFILE;

static DB_TYPE_MONETARY_PROFILE default_monetary_profile = {
  0, 2, false, true, false, true, true
};


/*
 * DB_TIME type conversion profile
 */

typedef struct
{
  const char *format;		/* Use the following macros */
} DB_TYPE_TIME_PROFILE;

/* DB_TIME conversion 'format' macros */
#define TIME_FORMAT_TWELVE_HOUR                 "%I:%M:%S %p"
#define TIME_FORMAT_TWELVE_HOUR_W_TIMEZONE      "%I:%M:%S %p %Z"
#define TIME_FORMAT_TWENTY_FOUR_HOUR            "%H:%M:%S"
#define TIME_FORMAT_TWENTY_FOUR_HOUR_W_TIMEZONE "%H:%M:%S %Z"

static DB_TYPE_TIME_PROFILE default_time_profile = {
  TIME_FORMAT_TWELVE_HOUR
};


/*
 * DB_DATE type conversion profile
 */

typedef struct
{
  int format;			/* Use the following enumeration value */
} DB_TYPE_DATE_PROFILE;

/* DB_DATE conversion 'format' enumeration */
enum
{
  DATE_FORMAT_FULL_TEXT,	/* standard US text format. "September 15, 2008" */
  DATE_FORMAT_ABREV_TEXT,	/* abbreviated US text format. "Sept. 15, 2008" */
  DATE_FORMAT_FULL_TEXT_W_DAY,	/* standard US format with day name. "Thursday, March 20, 2008" */
  DATE_FORMAT_ABREV_TEXT_W_DAY,	/* abbreviated US format with day name. "Thu, Mar 20, 2008" */
  DATE_FORMAT_FULL_EURO_TEXT,	/* standard European text format. "10. June 2008" */
  DATE_FORMAT_ABREV_EURO_TEXT,	/* abbreviated European format "15. Sep 1990" */
  DATE_FORMAT_YYMMDD,		/* YY/MM/DD format "08/06/10" */
  DATE_FORMAT_MMDDYY,		/* MM/DD/YY format "06/10/08" */
  DATE_FORMAT_DDMMYY,		/* DD/MM/YY format "10/06/08" */
  DATE_FORMAT_MMDDYYYY		/* MM/DD/YYYY format "06/10/2008" */
};

static DB_TYPE_DATE_PROFILE default_date_profile = {
  DATE_FORMAT_MMDDYYYY
};

/* object conversion 'format' enumeration */
enum
{
  OBJECT_FORMAT_OID,
  OBJECT_FORMAT_CLASSNAME
};

/*
 * set type conversion profile
 */

typedef struct
{
  char begin_notation;		/* what character to use to denote begin of set '\0' for no character */
  char end_notation;		/* what character to use to denote end of set '\0' for no character */
  int max_entries;		/* max no. of entries to display, (-1 for all) */
} DB_TYPE_SET_PROFILE;

static DB_TYPE_SET_PROFILE default_set_profile = {
  '{', '}', -1
};


/*
 * string type conversion profile
 */

typedef struct
{
  char string_delimiter;	/* the character to use for a string delimiter ('\0' for none). */
} DB_TYPE_STRING_PROFILE;

static DB_TYPE_STRING_PROFILE default_string_profile = {
  '\''
};

/* TODO: locale ?*/
static const char *day_of_week_names[] = {
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday"
};

static const char *month_of_year_names[] = {
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

static void add_commas (char *string);
static void strip_trailing_zeros (char *numeric_string);
static char *double_to_string (double double_value, int field_width, int precision, const bool leading_sign,
			       const char *leading_str, const char *trailing_str, bool leading_zeros,
			       bool trailing_zeros, bool commas, char conversion);
#if defined (ENABLE_UNUSED_FUNCTION)
static char *time_as_string (DB_TIME * time_value, const char *conversion);
#endif
static char *date_as_string (DB_DATE * date_value, int format);
static char *bigint_to_string (DB_BIGINT int_value, int field_width, bool leading_zeros, bool leading_symbol,
			       bool commas, char conversion);
static char *object_to_string (DB_OBJECT * object, int format);
static char *numeric_to_string (DB_VALUE * value, bool commas);
static char *bit_to_string (DB_VALUE * value, char string_delimiter, bool plain_string);
static char *set_to_string (DB_VALUE * value, char begin_notation, char end_notation, int max_entries,
			    bool plain_string, CSQL_OUTPUT_TYPE output_type, char column_encolser);
static char *duplicate_string (const char *string);
static char *string_to_string (const char *string_value, char string_delimiter, char string_introducer, int length,
			       int *result_length, bool plain_string, bool change_single_quote);
static int get_object_print_format (void);

/*
 * add_commas() - counts the digits in this string and adds the commas
 *   return: none
 *   string(in/out): string to add commas to
 */
static void
add_commas (char *string)
{
  int i, num_of_digits, last_digit, num_of_commas, string_len;

  if (string == NULL)
    {
      return;
    }

  num_of_digits = last_digit = num_of_commas = 0;
  string_len = strlen (string);

  /*
   * First count the digits before the decimal place
   */
  for (i = 0; i < string_len; i++)
    {
      /* checking for CMSB is necessary for internationalization */
      if (!(CMSB (string[i])) && isdigit (string[i]))
	{
	  if (num_of_digits)
	    {
	      last_digit = i;	/* keep track of last digit found */
	    }
	  num_of_digits++;	/* increment total number of digits found */
	}
      else
	{
	  if (num_of_digits)	/* check to see if any found yet, if not keep checking */
	    {
	      break;		/* otherwise stop */
	    }
	}
    }

  /*
   * If no digits, exit
   */
  if (!num_of_digits)
    return;

  /*
   * Calculate the number of commas we are going to insert
   */
  num_of_commas = num_of_digits / 3;
  if (!(num_of_digits % 3))
    {
      num_of_commas--;
    }

  /*
   * Add them if necessary
   */
  if (num_of_commas)
    {
      char *temp;
      int l1, l2;

      l1 = string_len;
      l2 = string_len + num_of_commas;

      temp = string;

      do
	{
	  temp[l2--] = string[l1--];
	}
      while (string[l1] != '.');

      temp[l2--] = string[l1--];

      for (i = 0; num_of_commas; i++)
	{
	  if (i && !(i % 3) && num_of_commas)
	    {
	      temp[l2--] = COMMA_CHAR;
	      --num_of_commas;
	    }
	  if (l1 && l2)
	    {
	      temp[l2--] = string[l1--];
	    }
	}
    }
}

/*
 * strip_trailing_zeros() - Strip the trailing zeros from the end of a numeric string
 *   return: none
 *   numeric_string(in/out): the numeric string to strip trailing zeros from
 *
 * Note: Strip the trailing zeros from the end of a numeric string.
 *       This function will only strip them from a string with
 *       a decimal place.  The numeric string can either be in
 *       decimal notation or scientific notation. The string can
 *       also have trailing characters (e.g., 123.45 DM).
 */
static void
strip_trailing_zeros (char *numeric_string)
{
  char *prefix;
  size_t remainder_len;

  if (numeric_string == NULL)
    {
      return;
    }

  /*
   * First check to see if this is even necessary
   */
  if ((prefix = strchr (numeric_string, '.')) == NULL)
    {
      return;
    }

  /*
   * Now count the number of trailing zeros
   */

  if ((remainder_len = strcspn (prefix + 1, "0123456789")) == 0)
    {
      /* No trailing characters */
      char *remainder = numeric_string + strlen (numeric_string);

      while (remainder-- > prefix)
	{
	  if (*remainder == '0')
	    {
	      *remainder = '\0';
	    }
	  else
	    {
	      break;
	    }
	}
    }
  else
    {
      int num_of_trailing_zeros = 0;
      char *remainder = numeric_string + remainder_len;
      char *end = numeric_string + strlen (numeric_string);

      while (remainder-- > prefix)
	{
	  if (*remainder == '0')
	    {
	      num_of_trailing_zeros++;
	    }
	  else
	    {
	      break;
	    }
	}

      while ((remainder++ + num_of_trailing_zeros) < end)
	{
	  *remainder = *(remainder + num_of_trailing_zeros);
	}
    }
}

/*
 * double_to_string() - convert double value to string
 *   return: formatted string
 *   double_value(in): double value to convert
 *   field_width(in): the overall fieldwidth
 *   precision(in): the number of places after the decimal point
 *   leading_sign(in): true if leading sign '+' should be forced to show
 *   leading_str(in): the leading symbols to show, NULL if none desired
 *   trailing_str(in): the traling symbols to show, NULL if none desired
 *   leading_zeros(in): whether or not to show leading zeros
 *   trailing_zeros(in): whether or not to show trailing zeros
 *   commas(in): whether or not to show commas (every three digits)
 *   conversion(in): conversion format character, scientific or decimal
 */
static char *
double_to_string (double double_value, int field_width, int precision, const bool leading_sign, const char *leading_str,
		  const char *trailing_str, bool leading_zeros, bool trailing_zeros, bool commas, char conversion)
{
  char numeric_conversion_string[1024];
  char precision_string[16];
  char format_string[32];
  int i, overall_fieldwidth;

  if (field_width < 0)
    {
      field_width = 0;
    }

  overall_fieldwidth = field_width;

  if (precision < 0)
    {
      precision = 0;
    }

  snprintf (precision_string, sizeof (precision_string) - 1, ".%u", (int) precision);

  i = 0;

  format_string[i++] = '%';

  if ((double_value < (double) 0) || (leading_sign == true))
    {
      format_string[i++] = '+';
      if (overall_fieldwidth)
	{
	  overall_fieldwidth++;
	}
    }

  if (leading_zeros == true)
    {
      format_string[i++] = '0';
    }

  if ((trailing_zeros == true) && (precision))
    {
      format_string[i++] = '#';
    }

  format_string[i++] = '*';
  format_string[i] = 0;


  strcat ((char *) format_string, (char *) precision_string);
  i = strlen (format_string);
  format_string[i++] = conversion;
  format_string[i] = 0;
  if ((overall_fieldwidth > 0) && (conversion == DOUBLE_FORMAT_SCIENTIFIC))
    {
      overall_fieldwidth += 4;
    }

  if (!sprintf ((char *) numeric_conversion_string, (char *) format_string, (int) field_width, double_value))
    {
      return (NULL);
    }
  else
    {
      char *return_string;
      int actual_fieldwidth = strlen (numeric_conversion_string);
      int leading_size = (leading_str != NULL) ? strlen (leading_str) : 0;
      int trailing_size = (trailing_str != NULL) ? strlen (trailing_str) : 0;

      if ((size_t) (leading_size + actual_fieldwidth + 1) > sizeof (numeric_conversion_string))
	{
	  return NULL;
	}
      if (leading_size > 0)
	{
	  memmove (numeric_conversion_string + leading_size, numeric_conversion_string, actual_fieldwidth);
	  memcpy (numeric_conversion_string, leading_str, leading_size);

	  numeric_conversion_string[actual_fieldwidth + leading_size] = '\0';
	  actual_fieldwidth += leading_size;
	}
#if defined(HPUX)
      /* workaround for HP's broken printf */
      if (strstr (numeric_conversion_string, "+.+") || strstr (numeric_conversion_string, "++"))
	sprintf (numeric_conversion_string, "Inf");
      if (strstr (numeric_conversion_string, "-.-") || strstr (numeric_conversion_string, "--"))
	sprintf (numeric_conversion_string, "-Inf");
#endif

      if (trailing_zeros == false)
	{
	  strip_trailing_zeros (numeric_conversion_string);
	  actual_fieldwidth = strlen (numeric_conversion_string);
	}

      if ((size_t) (trailing_size + actual_fieldwidth + 1) > sizeof (numeric_conversion_string))
	{
	  return NULL;
	}

      if (trailing_size > 0)
	{
	  memcpy (numeric_conversion_string + actual_fieldwidth, trailing_str, trailing_size);
	  numeric_conversion_string[actual_fieldwidth + trailing_size] = '\0';
	  actual_fieldwidth += trailing_size;
	}

      if (field_width == 0)
	{
	  if ((return_string =
	       (char *) malloc (actual_fieldwidth + COMMAS_OFFSET (commas, actual_fieldwidth) + 1)) == NULL)
	    {
	      return (NULL);
	    }
	  (void) strcpy (return_string, numeric_conversion_string);
	}
      else
	{
	  if ((return_string =
	       (char *) malloc (overall_fieldwidth + COMMAS_OFFSET (commas, actual_fieldwidth) + 1)) == NULL)
	    {
	      return (NULL);
	    }
	  if (actual_fieldwidth <= overall_fieldwidth)
	    {
	      return_string = strcpy (return_string, numeric_conversion_string);
	    }
	  else
	    {
	      return_string[overall_fieldwidth] = numeric_conversion_string[actual_fieldwidth];
	      while (overall_fieldwidth)
		{
		  return_string[--overall_fieldwidth] = numeric_conversion_string[--actual_fieldwidth];
		}
	    }
	}
      if (commas == true)
	{
	  add_commas (return_string);
	}
      return (return_string);
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * time_as_string() - convert time value to string
 *   return: formatted string
 *   time_value(in): time value to convert
 *   conversion(in): conversion format string
 */
static char *
time_as_string (DB_TIME * time_value, const char *conversion)
{
  char temp_string[TIME_STRING_MAX];

  if (time_value == NULL)
    {
      return NULL;
    }

  if (!db_strftime (temp_string, (int) TIME_STRING_MAX, conversion, (DB_DATE *) NULL, time_value))
    {
      return (NULL);
    }
  return (duplicate_string (temp_string));

}
#endif

/*
 * date_as_string() - convert date value to string
 *   return: formatted string
 *   date_value(in): date value to convert
 *   format(in): conversion format type
 */
static char *
date_as_string (DB_DATE * date_value, int format)
{
  char temp_buffer[DATE_TEMP_BUFFER_LENGTH];
  int month, day, year;

  if (date_value == NULL)
    {
      return (NULL);
    }

  db_date_decode (date_value, &month, &day, &year);
  switch (format)
    {
    case DATE_FORMAT_MMDDYYYY:
      {
	(void) sprintf (temp_buffer, "%02d/%02d/%04d", month, day, year);
      }
      break;
    case DATE_FORMAT_FULL_TEXT:
      (void) sprintf (temp_buffer, "%s %d, %04d", month_of_year_names[month - 1], day, year);
      break;
    case DATE_FORMAT_ABREV_TEXT:
      {
	char month_name[DATE_ABREV_NAME_LENGTH + 1];
	(void) strncpy (month_name, month_of_year_names[month - 1], DATE_ABREV_NAME_LENGTH);
	month_name[DATE_ABREV_NAME_LENGTH] = '\0';
	(void) sprintf (temp_buffer, "%s %d, %04d", month_name, day, year);
      }
      break;
    case DATE_FORMAT_FULL_TEXT_W_DAY:
      {
	int dayofweek = db_date_weekday (date_value);
	if (dayofweek < 0 || dayofweek > 6)
	  {
	    return (NULL);
	  }
	(void) sprintf (temp_buffer, "%s, %s %d, %04d", day_of_week_names[dayofweek], month_of_year_names[month - 1],
			day, year);
      }
      break;
    case DATE_FORMAT_ABREV_TEXT_W_DAY:
      {
	char day_name[DATE_ABREV_NAME_LENGTH + 1];
	char month_name[DATE_ABREV_NAME_LENGTH + 1];

	int dayofweek = db_date_weekday (date_value);
	if (dayofweek < 0 || dayofweek > 6)
	  {
	    return (NULL);
	  }
	(void) strncpy (day_name, day_of_week_names[dayofweek], DATE_ABREV_NAME_LENGTH);
	day_name[DATE_ABREV_NAME_LENGTH] = '\0';
	(void) strncpy (month_name, month_of_year_names[month - 1], DATE_ABREV_NAME_LENGTH);
	month_name[DATE_ABREV_NAME_LENGTH] = '\0';
	(void) sprintf (temp_buffer, "%s, %s %d, %04d", day_name, month_name, day, year);
      }
      break;
    case DATE_FORMAT_FULL_EURO_TEXT:
      (void) sprintf (temp_buffer, "%d. %s %04d", day, month_of_year_names[month - 1], year);
      break;
    case DATE_FORMAT_ABREV_EURO_TEXT:
      {
	char month_name[DATE_ABREV_NAME_LENGTH + 1];
	(void) strncpy (month_name, month_of_year_names[month - 1], DATE_ABREV_NAME_LENGTH);
	month_name[DATE_ABREV_NAME_LENGTH] = '\0';
	(void) sprintf (temp_buffer, "%d. %s %04d", day, month_name, year);
      }
      break;
    case DATE_FORMAT_YYMMDD:
      {
	(void) sprintf (temp_buffer, "%02d/%02d/%02d", year % 100, month, day);
      }
      break;
    case DATE_FORMAT_MMDDYY:
      {
	(void) sprintf (temp_buffer, "%02d/%02d/%02d", month, day, year % 100);
      }
      break;
    case DATE_FORMAT_DDMMYY:
      {
	(void) sprintf (temp_buffer, "%02d/%02d/%02d", day, month, year % 100);
      }
      break;
    default:
      /* invalid format type */
      temp_buffer[0] = '\0';
      break;
    }
  return (duplicate_string (temp_buffer));
}

/*
 * int_to_string() - convert integer value to string
 *   return: formatted string
 *   int_value(in): integer value to convert
 *   field_width(in): the desired field width
 *   leading_zeros(in): whether or not to show leading zeros
 *   leading_symbol(in): whether or not to show the leading symbol
 *   commas(in): whether or not to display commas
 *   conversion(in): conversion format charactern
 */
static char *
bigint_to_string (DB_BIGINT int_value, int field_width, bool leading_zeros, bool leading_symbol, bool commas,
		  char conversion)
{
  char numeric_conversion_string[1024];
  char format_string[10];
  char long_decimal = 'l';
  int i = 0;
  int overall_fieldwidth;

  if (field_width < 0)
    {
      field_width = 0;
    }

  overall_fieldwidth = field_width;
  format_string[i++] = '%';

  if (leading_zeros == true)
    {
      format_string[i++] = '0';
    }

  if (leading_symbol == true)
    {
      format_string[i++] = '#';
      if (overall_fieldwidth)
	{
	  switch (conversion)
	    {
	    case INT_FORMAT_SIGNED_DECIMAL:
	      {
		if (overall_fieldwidth && (int_value < 0))
		  {
		    overall_fieldwidth++;
		  }
		break;
	      }
	    case INT_FORMAT_HEXADECIMAL:
	      {
		if (overall_fieldwidth)
		  {
		    overall_fieldwidth += 2;
		  }
		break;
	      }
	    default:
	      break;
	    }
	}
    }

  if (int_value < 0)
    {
      format_string[i++] = '+';
    }

  format_string[i++] = '*';

  format_string[i++] = long_decimal;
  format_string[i++] = long_decimal;
  format_string[i++] = conversion;
  format_string[i++] = (char) 0;

  if (!sprintf (numeric_conversion_string, (char *) format_string, (int) field_width, int_value))
    {
      return (NULL);
    }
  else
    {
      char *return_string;
      int actual_fieldwidth = strlen (numeric_conversion_string);

      if (field_width == 0)
	{
	  if ((return_string =
	       (char *) malloc (actual_fieldwidth + COMMAS_OFFSET (commas, actual_fieldwidth) + 1)) == NULL)
	    {
	      return (NULL);
	    }
	  (void) strcpy (return_string, (const char *) &numeric_conversion_string[0]);
	}
      else
	{
	  if ((return_string =
	       (char *) malloc (overall_fieldwidth + COMMAS_OFFSET (commas, actual_fieldwidth) + 1)) == NULL)
	    {
	      return (NULL);
	    }
	  if (actual_fieldwidth <= overall_fieldwidth)
	    {
	      return_string = strcpy (return_string, numeric_conversion_string);
	    }
	  else
	    {
	      return_string[overall_fieldwidth] = numeric_conversion_string[actual_fieldwidth];
	      while (overall_fieldwidth)
		{
		  return_string[--overall_fieldwidth] = numeric_conversion_string[--actual_fieldwidth];
		}
	    }
	}
      if (commas == true)
	{
	  add_commas (return_string);
	}
      return (return_string);
    }
}

/*
 * object_to_string() - convert object to string
 *   return: formatted string
 *   object(in): object value to convert
 *   format(in): conversion format type
 */
static char *
object_to_string (DB_OBJECT * object, int format)
{
  if (object == NULL)
    return NULL;

  if (format == OBJECT_FORMAT_OID)
    {
      char temp_string[OBJECT_SYMBOL_MAX];

      if (!db_print_mop (object, temp_string, OBJECT_SYMBOL_MAX))
	{
	  return (NULL);
	}
      return (duplicate_string (temp_string));
    }
  else
    {
      char *name;

      name = (char *) db_get_class_name (object);
      if (name == NULL)
	{
	  return (NULL);
	}
      else
	{
	  return (duplicate_string (name));
	}
    }
}

/*
 * numeric_to_string() - convert numeric value to string
 *   return:  formatted string
 *   value(in): numeric value to convert
 *   commas(in): whether or not to display commas
 */
static char *
numeric_to_string (DB_VALUE * value, bool commas)
{
  char str_buf[NUMERIC_MAX_STRING_SIZE];
  char *return_string;
  int prec;
  int comma_length;
  int max_length;

  /*
   * Allocate string length based on precision plus the commas plus a
   * character for each of the sign, decimal point, and NULL terminator.
   */
  prec = DB_VALUE_PRECISION (value);
  comma_length = COMMAS_OFFSET (commas, prec);
  max_length = prec + comma_length + 3;
  return_string = (char *) malloc (max_length);
  if (return_string == NULL)
    {
      return (NULL);
    }

  numeric_db_value_print (value, str_buf);
  if (strlen (str_buf) > max_length - 1)
    {
      free_and_init (return_string);
      return (duplicate_string ("NUM OVERFLOW"));
    }
  strcpy (return_string, str_buf);

  return return_string;
}

/*
 * bit_to_string() - convert bit value to string
 *   return: formatted string
 *   value(in): bit value to convert
 */
static char *
bit_to_string (DB_VALUE * value, char string_delimiter, bool plain_string)
{
  char *temp_string;
  char *return_string;
  int max_length;

  /*
   * Allocate string length based on precision plus the the leading
   * introducer plus quotes, and NULL terminator.  Precision / 4 (rounded up)
   * represents the number of bytes needed to represent the bit string in
   * hexadecimal.
   */
  max_length = ((db_get_string_length (value) + 3) / 4) + 4;
  temp_string = (char *) malloc (max_length);
  if (temp_string == NULL)
    {
      return (NULL);
    }

  if (db_bit_string (value, "%X", temp_string, max_length) != CSQL_SUCCESS)
    {
      free_and_init (temp_string);
      return (NULL);		/* Should never get here */
    }

  return_string =
    string_to_string (temp_string, string_delimiter, 'X', strlen (temp_string), NULL, plain_string, false);
  free_and_init (temp_string);

  return (return_string);
}

/*
 * set_to_string() - convert set value to string
 *   return: formatted string
 *   value(in): set value to convert
 *   begin_notation(in): character to use to denote begin of set
 *   end_notation(in): character to use to denote end of set
 *   max_entries(in): maximum number of entries to convert. -1 for all
 *   plain_string(in): refine string for plain output
 *   output_type(in): query output or loaddb output
 *   column_enclosure(in): column enclosure for query output
 */
static char *
set_to_string (DB_VALUE * value, char begin_notation, char end_notation, int max_entries, bool plain_string,
	       CSQL_OUTPUT_TYPE output_type, char column_enclosure)
{
  int cardinality, total_string_length, i;
  char **string_array;
  char *return_string = NULL;
  DB_VALUE element;
  int set_error;
  DB_SET *set;

  set = db_get_set (value);
  if (set == NULL)
    {
      return (NULL);
    }

  /* pre-fetch any objects in the set, this will prevent multiple server calls during set rendering */
  db_fetch_set (set, DB_FETCH_READ, 0);

  /* formerly we filtered out deleted elements here, now just use db_set_size to get the current size, including NULL &
   * deleted elements */
  cardinality = db_set_size (set);

  if (cardinality < 0)
    {
      return (NULL);
    }
  else if (cardinality == 0)
    {
      char temp_buffer[4];

      i = 0;
      if (begin_notation != '\0')
	{
	  temp_buffer[i++] = begin_notation;
	}
      if (end_notation != '\0')
	{
	  temp_buffer[i++] = end_notation;
	}
      temp_buffer[i] = '\0';
      return (duplicate_string ((const char *) &(temp_buffer[0])));
    }

  if (max_entries != -1 && max_entries < cardinality)
    {
      cardinality = max_entries;
    }
  string_array = (char **) malloc ((cardinality + 2) * sizeof (char *));
  if (string_array == NULL)
    {
      return (NULL);
    }

  memset (string_array, 0, (cardinality + 2) * sizeof (char *));

  total_string_length = cardinality * 2;
  for (i = 0; i < cardinality; i++)
    {
      set_error = db_set_get (set, i, &element);
      if (set_error != NO_ERROR)
	{
	  goto finalize;
	}
      string_array[i] = csql_db_value_as_string (&element, NULL, plain_string, output_type, column_enclosure);
      db_value_clear (&element);
      if (string_array[i] == NULL)
	{
	  string_array[i] = duplicate_string ("NULL");
	  if (string_array[i] == NULL)
	    {
	      goto finalize;
	    }
	}
      total_string_length += strlen (string_array[i]);
    }				/* for (i = 0; i < cardinality... */

  return_string = (char *) malloc (total_string_length + 4);
  if (return_string == NULL)
    {
      goto finalize;
    }

  if (begin_notation != '\0')
    {
      (void) sprintf (return_string, "%c%s", begin_notation, string_array[0]);
    }
  else
    {
      (void) strcpy (return_string, string_array[0]);
    }

  for (i = 1; i < cardinality; i++)
    {
      (void) strcat (return_string, ", ");
      (void) strcat (return_string, string_array[i]);
    }
  if (end_notation != '\0')
    {
      int len = strlen (return_string);

      return_string[len++] = end_notation;
      return_string[len] = '\0';
    }

finalize:
  for (i = 0; i < cardinality; i++)
    {
      if (string_array[i] == NULL)
	{
	  break;
	}
      free_and_init (string_array[i]);
    }
  free_and_init (string_array);

  return return_string;
}

/*
 * duplicate_string() - Return an allocated copy of the string
 *   return: new string
 *   string(in): string value
 */
static char *
duplicate_string (const char *string)
{

  char *new_string;

  if (string == NULL)
    {
      return NULL;
    }

  new_string = (char *) malloc (strlen (string) + 1);
  if (new_string)
    {
      strcpy (new_string, string);
    }

  return (new_string);

}

/*
 * csql_string_to_plain_string() - Refine the string and return it
 *   return: refined plain string
 *   string_value(in): source string to duplicate
 *   length(in): length of the source string
 *   result_length(out): : length of output string
 *
 *   note: replace newline and tab with escaped string
 */
char *
csql_string_to_plain_string (const char *string_value, int length, int *result_length)
{
  char *return_string;
  char *ptr;
  char *con_buf_ptr = NULL;
  int con_buf_size = 0;
  int num_found = 0;
  int i;

  if (string_value == NULL)
    {
      return NULL;
    }

  ptr = (char *) string_value;
  while (*ptr != '\0')
    {
      if (*ptr == '\t' || *ptr == '\n' || *ptr == '\\')
	{
	  num_found++;
	}
      ptr++;
    }

  if (num_found == 0)
    {
      if (result_length != NULL)
	{
	  *result_length = length;
	}
      return duplicate_string (string_value);
    }

  return_string = (char *) malloc (length + num_found + 1);
  if (return_string == NULL)
    {
      return NULL;
    }

  ptr = return_string;
  for (i = 0; i < length; i++)
    {
      if (string_value[i] == '\t')
	{
	  ptr += sprintf (ptr, "\\t");
	}
      else if (string_value[i] == '\n')
	{
	  ptr += sprintf (ptr, "\\n");
	}
      else if (string_value[i] == '\\')
	{
	  ptr += sprintf (ptr, "\\\\");
	}
      else
	{
	  *(ptr++) = string_value[i];
	}
    }
  *ptr = '\0';

  if (csql_text_utf8_to_console != NULL
      && (*csql_text_utf8_to_console) (return_string, strlen (return_string), &con_buf_ptr, &con_buf_size) == NO_ERROR)
    {
      if (con_buf_ptr != NULL)
	{
	  free (return_string);
	  return_string = con_buf_ptr;
	  ptr = con_buf_ptr + con_buf_size;
	}
    }

  if (result_length)
    {
      *result_length = CAST_STRLEN (ptr - return_string);
    }

  return return_string;
}

/*
 * string_to_string() - Copy the string and return it
 *   return: formatted string
 *   string_value(in): source string to duplicate
 *   string_delimiter(in): delimiter to surround string with (0 if none)
 *   string_introducer(in): introducer for the string (0 if none)
 *   length(in): length of the source string
 *   result_length(out): : length of output string
 *   plain_string(in): refine string for plain output
 *   change_single_quote(in): refine string for query output
 */
static char *
string_to_string (const char *string_value, char string_delimiter, char string_introducer, int length,
		  int *result_length, bool plain_string, bool change_single_quote)
{
  char *return_string;
  char *ptr;
  char *con_buf_ptr = NULL;
  int con_buf_size = 0;
  int num_found = 0, i = 0;

  if (plain_string == true)
    {
      return csql_string_to_plain_string (string_value, length, result_length);
    }

  if (string_delimiter == '\0')
    {
      return (duplicate_string (string_value));
    }

  if (string_value == NULL)
    {
      return NULL;
    }

  if (change_single_quote == true)
    {
      ptr = (char *) string_value;

      while (*ptr != '\0')
	{
	  if (*ptr == '\'')
	    {
	      num_found++;
	    }
	  ptr++;
	}
    }

  if ((return_string = (char *) malloc (length + 4 + num_found)) == NULL)
    {
      return (NULL);
    }

  ptr = return_string;
  if (string_introducer)
    {
      *ptr++ = string_introducer;
    }
  *ptr++ = string_delimiter;

  if (change_single_quote == true)
    {
      for (i = 0; i < length; i++)
	{
	  if (string_value[i] == '\'')
	    {
	      *(ptr++) = string_value[i];
	      *(ptr++) = '\'';
	    }
	  else
	    {
	      *(ptr++) = string_value[i];
	    }
	}
      *(ptr++) = string_delimiter;
      *ptr = '\0';
    }
  else
    {
      memcpy (ptr, string_value, length);
      ptr[length] = string_delimiter;
      ptr = ptr + length + 1;
      *ptr = '\0';
    }

  if (csql_text_utf8_to_console != NULL
      && (*csql_text_utf8_to_console) (return_string, strlen (return_string), &con_buf_ptr, &con_buf_size) == NO_ERROR)
    {
      if (con_buf_ptr != NULL)
	{
	  free (return_string);
	  return_string = con_buf_ptr;
	  ptr = con_buf_ptr + con_buf_size;
	}
    }

  if (result_length)
    {
      *result_length = CAST_STRLEN (ptr - return_string);
    }
  return return_string;
}

/*
 * csql_db_value_as_string() - convert DB_VALUE to string
 *   return: formatted string
 *   value(in): value to convert
 *   length(out): length of output string
 *   plain_output(in): refine string for plain output
 *   output_type(in): query output or loaddb output
 *   column_enclosure(in): column enclosure for query output
 */
char *
csql_db_value_as_string (DB_VALUE * value, int *length, bool plain_string, CSQL_OUTPUT_TYPE output_type,
			 char column_enclosure)
{
  char *result = NULL;
  char *json_body = NULL;
  int len = 0;
  char string_delimiter =
    (output_type != CSQL_UNKNOWN_OUTPUT) ? column_enclosure : default_string_profile.string_delimiter;
  bool change_single_quote = (output_type != CSQL_UNKNOWN_OUTPUT && column_enclosure == '\'');

  if (value == NULL)
    {
      return (NULL);
    }

  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_BIGINT:
      result =
	bigint_to_string (db_get_bigint (value), default_bigint_profile.fieldwidth, default_bigint_profile.leadingzeros,
			  default_bigint_profile.leadingsymbol, default_bigint_profile.commas,
			  default_bigint_profile.format);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_INTEGER:
      result =
	bigint_to_string (db_get_int (value), default_int_profile.fieldwidth, default_int_profile.leadingzeros,
			  default_int_profile.leadingsymbol, default_int_profile.commas, default_int_profile.format);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_SHORT:
      result =
	bigint_to_string (SHORT_TO_INT (db_get_short (value)), default_short_profile.fieldwidth,
			  default_short_profile.leadingzeros, default_short_profile.leadingsymbol,
			  default_short_profile.commas, default_short_profile.format);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_FLOAT:
      result =
	double_to_string ((double) db_get_float (value), default_float_profile.fieldwidth,
			  default_float_profile.precision, default_float_profile.leadingsign, nullptr, nullptr,
			  default_float_profile.leadingzeros, default_float_profile.trailingzeros,
			  default_float_profile.commas, default_float_profile.format);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_DOUBLE:
      result =
	double_to_string (db_get_double (value), default_double_profile.fieldwidth, default_double_profile.precision,
			  default_double_profile.leadingsign, nullptr, nullptr, default_double_profile.leadingzeros,
			  default_double_profile.trailingzeros, default_double_profile.commas,
			  default_double_profile.format);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_NUMERIC:
      result = numeric_to_string (value, default_numeric_profile.commas);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      {
	int dummy, bytes_size, decomp_size;
	bool need_decomp = false;
	const char *str;
	char *decomposed = NULL;

	str = db_get_char (value, &dummy);
	bytes_size = db_get_string_size (value);
	if (bytes_size > 0 && db_get_string_codeset (value) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose (str, bytes_size, &decomp_size, lang_get_generic_unicode_norm ());
	  }

	if (need_decomp)
	  {
	    decomposed = (char *) malloc (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string (str, bytes_size, decomposed, &decomp_size, lang_get_generic_unicode_norm ());

		str = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		return NULL;
	      }
	  }

	result = string_to_string (str, string_delimiter, '\0', bytes_size, &len, plain_string, change_single_quote);

	if (decomposed != NULL)
	  {
	    free_and_init (decomposed);
	  }

      }
      break;
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
      {
	int dummy, bytes_size, decomp_size;
	bool need_decomp = false;
	const char *str;
	char *decomposed = NULL;

	str = db_get_char (value, &dummy);
	bytes_size = db_get_string_size (value);
	if (bytes_size > 0 && db_get_string_codeset (value) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose (str, bytes_size, &decomp_size, lang_get_generic_unicode_norm ());
	  }

	if (need_decomp)
	  {
	    decomposed = (char *) malloc (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string (str, bytes_size, decomposed, &decomp_size, lang_get_generic_unicode_norm ());

		str = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		return NULL;
	      }
	  }

	result = string_to_string (str, string_delimiter, 'N', bytes_size, &len, plain_string, change_single_quote);

	if (decomposed != NULL)
	  {
	    free_and_init (decomposed);
	  }
      }
      break;
    case DB_TYPE_VARBIT:
    case DB_TYPE_BIT:
      result = bit_to_string (value, string_delimiter, plain_string);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_OBJECT:
      result = object_to_string (db_get_object (value), get_object_print_format ());
      if (result == NULL)
	{
	  result = duplicate_string ("NULL");
	}
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_VOBJ:
      result = object_to_string (db_get_object (value), get_object_print_format ());
      if (result == NULL)
	{
	  result = duplicate_string ("NULL");
	}
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_JSON:
      json_body = db_get_json_raw_body (value);
      result = duplicate_string (json_body);
      db_private_free (NULL, json_body);
      if (result)
	{
	  if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	    {
	      char *new_result;

	      new_result =
		string_to_string (result, column_enclosure, '\0', strlen (result), &len, false, change_single_quote);

	      if (new_result)
		{
		  free (result);
		  result = new_result;
		}
	    }
	  len = strlen (result);
	}
      break;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      result =
	set_to_string (value, default_set_profile.begin_notation, default_set_profile.end_notation,
		       default_set_profile.max_entries, plain_string, output_type, column_enclosure);
      if (result)
	{
	  len = strlen (result);
	}
      break;
    case DB_TYPE_TIME:
      {
	char buf[TIME_BUF_SIZE];
	if (db_time_to_string (buf, sizeof (buf), db_get_time (value)))
	  {
	    if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	      {
		result = string_to_string (buf, column_enclosure, '\0', strlen (buf), &len, false, false);
	      }
	    else
	      {
		result = duplicate_string (buf);
	      }
	  }
	if (result)
	  {
	    len = strlen (result);
	  }
	break;
      }
    case DB_TYPE_MONETARY:
      {
	char *leading_str = NULL;
	char *trailing_str = NULL;
	DB_MONETARY *monetary_val = db_get_monetary (value);
	DB_CURRENCY currency = monetary_val->type;

	if (default_monetary_profile.currency_symbol)
	  {
	    if (intl_get_currency_symbol_position (currency) == 1)
	      {
		trailing_str = intl_get_money_symbol_console (currency);
	      }
	    else
	      {
		leading_str = intl_get_money_symbol_console (currency);
	      }
	  }

	if (db_get_monetary (value) == NULL)
	  {
	    result = NULL;
	  }
	else
	  {
	    result = double_to_string (monetary_val->amount, default_monetary_profile.fieldwidth,
				       default_monetary_profile.decimalplaces, default_monetary_profile.leadingsign,
				       leading_str, trailing_str, default_monetary_profile.leadingzeros,
				       default_monetary_profile.trailingzeros, default_monetary_profile.commas,
				       DOUBLE_FORMAT_DECIMAL);
	  }

	if (result)
	  {
	    len = strlen (result);
	  }
      }
      break;
    case DB_TYPE_DATE:
      /* default format for all locales */
      result = date_as_string (db_get_date (value), default_date_profile.format);
      if (result)
	{
	  if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	    {
	      char *new_result;

	      new_result = string_to_string (result, column_enclosure, '\0', strlen (result), &len, false, false);

	      if (new_result)
		{
		  free (result);
		  result = new_result;
		}
	    }
	  len = strlen (result);
	}
      break;
    case DB_TYPE_TIMESTAMP:
      {
	char buf[TIMESTAMP_BUF_SIZE];
	if (db_utime_to_string (buf, sizeof (buf), db_get_timestamp (value)))
	  {
	    if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	      {
		result = string_to_string (buf, column_enclosure, '\0', strlen (buf), &len, false, false);
	      }
	    else
	      {
		result = duplicate_string (buf);
	      }
	  }

	if (result)
	  {
	    len = strlen (result);
	  }
      }
      break;
    case DB_TYPE_TIMESTAMPTZ:
      {
	char buf[TIMESTAMPTZ_BUF_SIZE];
	DB_TIMESTAMPTZ *ts_tz = db_get_timestamptz (value);
	if (db_timestamptz_to_string (buf, sizeof (buf), &(ts_tz->timestamp), &(ts_tz->tz_id)))
	  {
	    if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	      {
		result = string_to_string (buf, column_enclosure, '\0', strlen (buf), &len, false, false);
	      }
	    else
	      {
		result = duplicate_string (buf);
	      }
	  }

	if (result)
	  {
	    len = strlen (result);
	  }
      }
      break;
    case DB_TYPE_TIMESTAMPLTZ:
      {
	char buf[TIMESTAMPTZ_BUF_SIZE];

	if (db_timestampltz_to_string (buf, sizeof (buf), db_get_timestamp (value)))
	  {
	    if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	      {
		result = string_to_string (buf, column_enclosure, '\0', strlen (buf), &len, false, false);
	      }
	    else
	      {
		result = duplicate_string (buf);
	      }
	  }

	if (result)
	  {
	    len = strlen (result);
	  }
      }
      break;
    case DB_TYPE_DATETIME:
      {
	char buf[DATETIME_BUF_SIZE];
	if (db_datetime_to_string (buf, sizeof (buf), db_get_datetime (value)))
	  {
	    if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	      {
		result = string_to_string (buf, column_enclosure, '\0', strlen (buf), &len, false, false);
	      }
	    else
	      {
		result = duplicate_string (buf);
	      }
	  }

	if (result)
	  {
	    len = strlen (result);
	  }
      }
      break;
    case DB_TYPE_DATETIMETZ:
      {
	char buf[DATETIMETZ_BUF_SIZE];
	DB_DATETIMETZ *dt_tz = db_get_datetimetz (value);
	if (db_datetimetz_to_string (buf, sizeof (buf), &(dt_tz->datetime), &(dt_tz->tz_id)))
	  {
	    if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	      {
		result = string_to_string (buf, column_enclosure, '\0', strlen (buf), &len, false, false);
	      }
	    else
	      {
		result = duplicate_string (buf);
	      }
	  }

	if (result)
	  {
	    len = strlen (result);
	  }
      }
      break;
    case DB_TYPE_DATETIMELTZ:
      {
	char buf[DATETIMETZ_BUF_SIZE];

	if (db_datetimeltz_to_string (buf, sizeof (buf), db_get_datetime (value)))
	  {
	    if (output_type == CSQL_QUERY_OUTPUT || output_type == CSQL_LOADDB_OUTPUT)
	      {
		result = string_to_string (buf, column_enclosure, '\0', strlen (buf), &len, false, false);
	      }
	    else
	      {
		result = duplicate_string (buf);
	      }
	  }

	if (result)
	  {
	    len = strlen (result);
	  }
      }
      break;
    case DB_TYPE_NULL:
      result = duplicate_string ("NULL");
      if (result)
	{
	  len = strlen (result);
	}
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      {
	DB_ELO *elo = db_get_elo (value);

	if (elo != NULL)
	  {
	    result = duplicate_string (elo->locator);
	  }

	if (result != NULL)
	  {
	    len = strlen (result);
	  }
      }
      break;

    case DB_TYPE_ENUMERATION:
      {
	int bytes_size, decomp_size;
	bool need_decomp = false;
	const char *str;
	char *decomposed = NULL;

	if (output_type == CSQL_LOADDB_OUTPUT)
	  {
	    result = bigint_to_string (SHORT_TO_INT (db_get_enum_short (value)), default_short_profile.fieldwidth,
				       default_short_profile.leadingzeros, default_short_profile.leadingsymbol,
				       default_short_profile.commas, default_short_profile.format);
	    if (result)
	      {
		len = strlen (result);
	      }
	    break;
	  }

	if (db_get_enum_short (value) == 0 && db_get_enum_string (value) == NULL)
	  {
	    /* ENUM special error value */
	    str = "";
	    bytes_size = 0;
	  }
	else
	  {
	    str = db_get_enum_string (value);
	    bytes_size = db_get_enum_string_size (value);
	  }
	if (bytes_size > 0 && db_get_enum_codeset (value) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose ((char *) str, bytes_size, &decomp_size, lang_get_generic_unicode_norm ());
	  }

	if (need_decomp)
	  {
	    decomposed = (char *) malloc (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string ((char *) str, bytes_size, decomposed, &decomp_size,
					  lang_get_generic_unicode_norm ());

		str = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		return NULL;
	      }
	  }

	result = string_to_string (str, string_delimiter, '\0', bytes_size, &len, plain_string, change_single_quote);
	if (decomposed != NULL)
	  {
	    free_and_init (decomposed);
	  }
      }
      break;

    default:
      {
	char temp_buffer[256];

	(void) sprintf (temp_buffer, "<%s>", db_get_type_name (DB_VALUE_TYPE (value)));
	result = duplicate_string (temp_buffer);
	if (result)
	  {
	    len = strlen (result);
	  }
      }
    }

  if (length)
    {
      *length = len;
    }
  return result;
}

static int
get_object_print_format (void)
{
  return prm_get_bool_value (PRM_ID_OBJECT_PRINT_FORMAT_OID) ? OBJECT_FORMAT_OID : OBJECT_FORMAT_CLASSNAME;
}
