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
 * cnvlex.h - Lexical scanning interface for string conversion API.
 */

#ifndef _CNVLEX_H_
#define _CNVLEX_H_

#ident "$Id$"

#include "intl_support.h"

/*
 * Lexical scan modes. These correspond directly to start conditions defined
 * in the scanner definition file. Be sure to update db_fmt_lex_start() to
 * maintain the correspondence.
 */

typedef enum
{
  FL_LOCAL_NUMBER = 0,
  FL_US_ENG_NUMBER = 1,
  FL_KO_KR_NUMBER = 2,
  FL_LOCAL_TIME = 3,
  FL_US_ENG_TIME = 4,
  FL_KO_KR_TIME = 5,
  FL_INTEGER_FORMAT = 6,
  FL_TIME_FORMAT = 7,
  FL_BIT_STRING_FORMAT = 8,
  FL_BIT_STRING = 9,
  FL_VALIDATE_DATE_FORMAT = 10,
  FL_VALIDATE_FLOAT_FORMAT = 11,
  FL_VALIDATE_INTEGER_FORMAT = 12,
  FL_VALIDATE_MONETARY_FORMAT = 13,
  FL_VALIDATE_TIME_FORMAT = 14,
  FL_VALIDATE_TIMESTAMP_FORMAT = 15,
  FL_VALIDATE_BIT_STRING_FORMAT = 16
} FMT_LEX_MODE;


typedef enum
{
  FT_NONE = 0,
  FT_AM_PM = 1,
  FT_BLANKS = 2,
  FT_BINARY_DIGITS = 3,
  FT_BIT_STRING_FORMAT = 4,
  FT_CURRENCY = 5,
  FT_DATE = 6,
  FT_DATE_FORMAT = 7,
  FT_DATE_SEPARATOR = 8,
  FT_LOCAL_DATE_SEPARATOR = 9,
  FT_DECIMAL = 10,
  FT_FLOAT_FORMAT = 11,
  FT_HOUR = 12,
  FT_HEX_DIGITS = 13,
  FT_INTEGER_FORMAT = 14,
  FT_MINUS = 15,
  FT_MINUTE = 16,
  FT_MONETARY_FORMAT = 17,
  FT_MONTH = 18,
  FT_MONTHDAY = 19,
  FT_MONTH_LONG = 20,
  FT_NUMBER = 21,
  FT_PATTERN = 22,
  FT_PLUS = 23,
  FT_SECOND = 24,
  FT_SPACES = 25,
  FT_STARS = 26,
  FT_THOUSANDS = 27,
  FT_TIME = 28,
  FT_TIME_DIGITS = 29,
  FT_TIME_DIGITS_ANY = 30,
  FT_TIME_DIGITS_0 = 31,
  FT_TIME_DIGITS_BLANK = 32,
  FT_TIME_FORMAT = 33,
  FT_TIME_SEPARATOR = 34,
  FT_LOCAL_TIME_SEPARATOR = 35,
  FT_UNKNOWN = 36,
  FT_TIMESTAMP = 37,
  FT_TIMESTAMP_FORMAT = 38,
  FT_WEEKDAY = 39,
  FT_WEEKDAY_LONG = 40,
  FT_YEAR = 41,
  FT_ZEROES = 42,
  FT_ZONE = 43,
  FT_MILLISECOND = 44
} FMT_TOKEN_TYPE;

typedef struct fmt_token FMT_TOKEN;
struct fmt_token
{
  FMT_TOKEN_TYPE type;
  const char *text;
  int length;
  const char *raw_text;
  int value;
};

extern void cnv_fmt_analyze (const char *instring, FMT_LEX_MODE mode);
extern FMT_TOKEN_TYPE cnv_fmt_lex (FMT_TOKEN * token);
extern void cnv_fmt_unlex (void);
extern const char *cnv_fmt_next_token (void);
extern FMT_LEX_MODE cnv_fmt_number_mode (INTL_ZONE zone);
extern FMT_LEX_MODE cnv_fmt_time_mode (INTL_ZONE zone);
extern void cnv_fmt_exit (void);
#endif /* _CNVLEX_H_ */
