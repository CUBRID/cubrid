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
 * cnvlex.l - lexical scanner
 */
%
{

#ident "$Id$"

#include <stdlib.h>
#include <assert.h>

#include "config.h"

/* Set up custom prefix */
#define YY_PREFIX dbcnv

#include "cnvlex.h"
#include "util_func.h"

  static int cnv_fmt_lex_start (FMT_LEX_MODE mode);
  void cnv_fmt_exit (void);

/* Global variables */
  static int cnv_fmt__init = 0;
  static const char *cnv_fmt__input = NULL;
  static const char *cnv_fmt__next = NULL;
  static int cnv_fmt__unlex = 0;
  static FMT_LEX_MODE cnv_fmt__mode = FL_LOCAL_NUMBER;

#undef YY_READ_BUF_SIZE
#define YY_READ_BUF_SIZE 512

#undef YY_DECL
#define YY_DECL \
FMT_TOKEN_TYPE cnv_fmt_lex( FMT_TOKEN *token)

#undef YY_INPUT
#define YY_INPUT( buf, result, max) \
result = (*(buf) = *cnv_fmt__input)? (cnv_fmt__input++, 1) : YY_NULL;

#undef YY_USER_ACTION
#define YY_USER_ACTION \
token->text   = token->raw_text = (const char *)yytext; \
token->length = yyleng;

#undef YY_USER_INIT
#define YY_USER_INIT \
if( !cnv_fmt__init) { \
  atexit( cnv_fmt_exit); \
  cnv_fmt__init = 1; \
  } \
BEGIN( cnv_fmt_lex_start( cnv_fmt__mode));


#define DB_FMT_LEX_RETURN( t) \
token->type   = t; \
cnv_fmt__next  += yyleng; \
return t;

%}

%s SC_US_ENG_NUMBER
  % s SC_KO_KR_NUMBER
  % s SC_US_ENG_TIME
  % s SC_KO_KR_TIME
  % s SC_BIT_STRING
  % s SC_INTEGER_FORMAT
  % s SC_TIME_FORMAT
  % s SC_BIT_STRING_FORMAT
  % s SC_VALIDATE_DATE_FORMAT
  % s SC_VALIDATE_FLOAT_FORMAT
  % s SC_VALIDATE_INTEGER_FORMAT
  % s SC_VALIDATE_MONETARY_FORMAT
  % s SC_VALIDATE_TIME_FORMAT
  % s SC_VALIDATE_TIMESTAMP_FORMAT
  % s SC_VALIDATE_BIT_STRING_FORMAT
  DATE_SPEC %[%aAbBdDEemwxyY]
  TIME_SPEC %[%HIklMprRSTXZ] TIMESTAMP_SPEC %[cC] |
{
DATE_SPEC} |

{
TIME_SPEC}

TIME_PATTERN[^%]BINARY_PATTERN[bB] HEX_PATTERN[hHxX] BIT_STRING_SPEC % ("%" |
									{
									BINARY_PATTERN} |

									{
									HEX_PATTERN})
  ANY_DIGITS
{
INTEGRAL_DIGITS} ? ("."

		    {
		    DIGITS} ?) ? INTEGRAL_DIGITS

{
DIGITS} |

{
Z_THOUS} |

{
N_THOUS} |

{
S_THOUS}

DIGITS
{
Z_SEQ} |

{
N_SEQ} |

{
S_SEQ}
Z_SEQ Z + N_SEQ 9 + S_SEQ "*" + Z_THOUS Z,
{
Z_SEQ} ? N_THOUS 9,
{
N_SEQ} ? S_THOUS "*",

{
S_SEQ} ?
  INTEGER_PATTERN[^X \\] | "\\X" TIME_SEPARATOR ":" DATE_SEPARATOR "/" %%
  /* Preliminary "actions" */
  if (cnv_fmt__unlex && yy_current_buffer->yy_eof_status != EOF_DONE)
  {
    /*
     * Push back current token so that it can be read again.
     * See cnv_fmt_unlex().
     */
    int i;
    for (i = yyleng - 1; i >= 0; --i)
      {
	unput (yytext[i]);
      }
    cnv_fmt__next -= yyleng;
    cnv_fmt__unlex = 0;
  }


<INITIAL, SC_US_ENG_NUMBER, SC_KO_KR_NUMBER > "-"
{
  DB_FMT_LEX_RETURN (FT_MINUS);
}

<INITIAL, SC_US_ENG_NUMBER, SC_KO_KR_NUMBER > "+"
{
  DB_FMT_LEX_RETURN (FT_PLUS);
}

<INITIAL, SC_US_ENG_NUMBER, SC_KO_KR_NUMBER > "."
{
  DB_FMT_LEX_RETURN (FT_DECIMAL);
}

<INITIAL, SC_US_ENG_NUMBER > "$"
{
  DB_FMT_LEX_RETURN (FT_CURRENCY);
}

<SC_KO_KR_NUMBER > "\\" | "\243\334"
{
  DB_FMT_LEX_RETURN (FT_CURRENCY);
}

<INITIAL, SC_US_ENG_NUMBER, SC_KO_KR_NUMBER > ","[0 - 9]
{
3}

{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_THOUSANDS);
}

<INITIAL, SC_US_ENG_NUMBER, SC_KO_KR_NUMBER >[1 - 9] ([0 - 9] *[1 - 9]) ?
{
  /* Number with no leading or trailing zeroes. */
  DB_FMT_LEX_RETURN (FT_NUMBER);
}

<INITIAL, SC_US_ENG_NUMBER, SC_KO_KR_NUMBER > "*" +
{
  DB_FMT_LEX_RETURN (FT_STARS);
}

<INITIAL, SC_US_ENG_NUMBER, SC_KO_KR_NUMBER > "0" +
{
  DB_FMT_LEX_RETURN (FT_ZEROES);
}

<SC_US_ENG_TIME, SC_KO_KR_TIME > "0"[0 - 9]
{
  DB_FMT_LEX_RETURN (FT_TIME_DIGITS_0);
}

<SC_US_ENG_TIME, SC_KO_KR_TIME > " "[0 - 9]
{
  DB_FMT_LEX_RETURN (FT_TIME_DIGITS_BLANK);
}

<SC_US_ENG_TIME, SC_KO_KR_TIME >[1 - 9][0 - 9]
{
  DB_FMT_LEX_RETURN (FT_TIME_DIGITS);
}

<SC_US_ENG_TIME, SC_KO_KR_TIME > "0" | ([1 - 9][0 - 9] *)
{
  DB_FMT_LEX_RETURN (FT_TIME_DIGITS_ANY);
}

<SC_US_ENG_TIME, SC_KO_KR_TIME >
{
DATE_SEPARATOR}

{
  DB_FMT_LEX_RETURN (FT_DATE_SEPARATOR);
}

<SC_US_ENG_TIME, SC_KO_KR_TIME >
{
TIME_SEPARATOR}

{
  DB_FMT_LEX_RETURN (FT_TIME_SEPARATOR);
}

<SC_KO_KR_TIME > "\xb3\xe2" | "\xbf\xf9" | "\xc0\xcf"
{
  DB_FMT_LEX_RETURN (FT_LOCAL_DATE_SEPARATOR);
}

<SC_US_ENG_TIME > "AM" | "am"
{
  token->value = 0;
  DB_FMT_LEX_RETURN (FT_AM_PM);
}

<SC_US_ENG_TIME > "PM" | "pm"
{
  token->value = 1;
  DB_FMT_LEX_RETURN (FT_AM_PM);
}


<SC_US_ENG_TIME > "Sunday"
{
  token->value = 0;
  DB_FMT_LEX_RETURN (FT_WEEKDAY_LONG);
}

<SC_US_ENG_TIME > "Monday"
{
  token->value = 1;
  DB_FMT_LEX_RETURN (FT_WEEKDAY_LONG);
}

<SC_US_ENG_TIME > "Tuesday"
{
  token->value = 2;
  DB_FMT_LEX_RETURN (FT_WEEKDAY_LONG);
}

<SC_US_ENG_TIME > "Wednesday"
{
  token->value = 3;
  DB_FMT_LEX_RETURN (FT_WEEKDAY_LONG);
}

<SC_US_ENG_TIME > "Thursday"
{
  token->value = 4;
  DB_FMT_LEX_RETURN (FT_WEEKDAY_LONG);
}

<SC_US_ENG_TIME > "Friday"
{
  token->value = 5;
  DB_FMT_LEX_RETURN (FT_WEEKDAY_LONG);
}

<SC_US_ENG_TIME > "Saturday"
{
  token->value = 6;
  DB_FMT_LEX_RETURN (FT_WEEKDAY_LONG);
}

<SC_US_ENG_TIME > "Sun"
{
  token->value = 0;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_US_ENG_TIME > "Mon"
{
  token->value = 1;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_US_ENG_TIME > "Tue"
{
  token->value = 2;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_US_ENG_TIME > "Wed"
{
  token->value = 3;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_US_ENG_TIME > "Thu"
{
  token->value = 4;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_US_ENG_TIME > "Fri"
{
  token->value = 5;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_US_ENG_TIME > "Sat"
{
  token->value = 6;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}



<SC_US_ENG_TIME > "January"
{
  token->value = 1;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "February"
{
  token->value = 2;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "March"
{
  token->value = 3;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "April"
{
  token->value = 4;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "May"
{
  token->value = 5;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "June"
{
  token->value = 6;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "July"
{
  token->value = 7;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "August"
{
  token->value = 8;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "September"
{
  token->value = 9;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "October"
{
  token->value = 10;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "November"
{
  token->value = 11;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "December"
{
  token->value = 12;
  DB_FMT_LEX_RETURN (FT_MONTH_LONG);
}

<SC_US_ENG_TIME > "Jan"
{
  token->value = 1;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Feb"
{
  token->value = 2;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Mar"
{
  token->value = 3;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Apr"
{
  token->value = 4;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "May"
{
  token->value = 5;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Jun"
{
  token->value = 6;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Jul"
{
  token->value = 7;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Aug"
{
  token->value = 8;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Sep"
{
  token->value = 9;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Oct"
{
  token->value = 10;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Nov"
{
  token->value = 11;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_US_ENG_TIME > "Dec"
{
  token->value = 12;
  DB_FMT_LEX_RETURN (FT_MONTH);
}



<SC_INTEGER_FORMAT > "X" +
{
  DB_FMT_LEX_RETURN (FT_NUMBER);
}

<SC_INTEGER_FORMAT >[^X \\] +
{
  DB_FMT_LEX_RETURN (FT_PATTERN);
}

<SC_INTEGER_FORMAT > "\\X"
{
  /* Escaped X is treated like a pattern char. */
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_PATTERN);
}



<SC_TIME_FORMAT >[^%] +
{
  DB_FMT_LEX_RETURN (FT_PATTERN);
}

<SC_TIME_FORMAT > "%%"
{
  /* Escaped % is treated like a pattern char. */
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_PATTERN);
}

<SC_TIME_FORMAT > %[DxE]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_DATE);
}

<SC_TIME_FORMAT > %[yY]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_YEAR);
}

<SC_TIME_FORMAT > %[bBm]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_MONTH);
}

<SC_TIME_FORMAT > %[de]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_MONTHDAY);
}

<SC_TIME_FORMAT > %[aAw]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_TIME_FORMAT > %[HIkl]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_HOUR);
}

<SC_TIME_FORMAT > %[M]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_MINUTE);
}

<SC_TIME_FORMAT > %[S]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_SECOND);
}

<SC_TIME_FORMAT > %[F]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_MILLISECOND);
}

<SC_TIME_FORMAT > %[p]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_AM_PM);
}

<SC_TIME_FORMAT > %[Z]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_ZONE);
}

<SC_TIME_FORMAT > %[rRTX]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_TIME);
}

<SC_TIME_FORMAT > %[cC]
{
  int csize = mblen (token->text, token->length);
  token->text += csize;
  token->length -= csize;
  DB_FMT_LEX_RETURN (FT_TIMESTAMP);
}




<SC_VALIDATE_DATE_FORMAT > (
			     {
			     TIME_PATTERN} *

			     {
			     DATE_SPEC} +

			     {
			     TIME_PATTERN} *) *

{
  DB_FMT_LEX_RETURN (FT_DATE_FORMAT);
}

<SC_VALIDATE_FLOAT_FORMAT > ("+") ?
{
ANY_DIGITS}

{
  DB_FMT_LEX_RETURN (FT_FLOAT_FORMAT);
}

<SC_VALIDATE_INTEGER_FORMAT > ("+") ?
{
INTEGRAL_DIGITS} ?

{
  DB_FMT_LEX_RETURN (FT_INTEGER_FORMAT);
}

<SC_VALIDATE_INTEGER_FORMAT > (
				{
				INTEGER_PATTERN} *X +

				{
				INTEGER_PATTERN} *) +

{
  DB_FMT_LEX_RETURN (FT_INTEGER_FORMAT);
}

<SC_VALIDATE_MONETARY_FORMAT > ("$") ?
{
ANY_DIGITS} |

{
ANY_DIGITS} "$"

{
  DB_FMT_LEX_RETURN (FT_MONETARY_FORMAT);
}

<SC_VALIDATE_TIME_FORMAT > (
			     {
			     TIME_PATTERN} *

			     {
			     TIME_SPEC} +

			     {
			     TIME_PATTERN} *) *

{
  DB_FMT_LEX_RETURN (FT_TIME_FORMAT);
}

<SC_VALIDATE_TIMESTAMP_FORMAT > (
				  {
				  TIME_PATTERN} *

				  {
				  TIMESTAMP_SPEC} +

				  {
				  TIME_PATTERN} *) *

{
  DB_FMT_LEX_RETURN (FT_TIMESTAMP_FORMAT);
}

<SC_VALIDATE_BIT_STRING_FORMAT >
{
BIT_STRING_SPEC}

{
  DB_FMT_LEX_RETURN (FT_BIT_STRING_FORMAT);
}

<SC_BIT_STRING_FORMAT > %("%" |
			  {
			  BINARY_PATTERN})

{
  DB_FMT_LEX_RETURN (FT_BINARY_DIGITS);
}

<SC_BIT_STRING_FORMAT > %
{
HEX_PATTERN}

{
  DB_FMT_LEX_RETURN (FT_HEX_DIGITS);
}

<SC_BIT_STRING >[01] *
{
  DB_FMT_LEX_RETURN (FT_BINARY_DIGITS);
}

<SC_BIT_STRING >[0 - 9 a - fA - F] *
{
  DB_FMT_LEX_RETURN (FT_HEX_DIGITS);
}

<SC_KO_KR_TIME > "\xbf\xc0\xc0\xfc"
{
  token->value = 0;
  DB_FMT_LEX_RETURN (FT_AM_PM);
}

<SC_KO_KR_TIME > "\xbf\xc0\xc8\xc4"
{
  token->value = 1;
  DB_FMT_LEX_RETURN (FT_AM_PM);
}

<SC_KO_KR_TIME > "(" "\xc0\xcf" ")"
{
  token->value = 0;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_KO_KR_TIME > "(" "\xbf\xf9" ")"
{
  token->value = 1;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_KO_KR_TIME > "(" "\xc8\xad" ")"
{
  token->value = 2;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_KO_KR_TIME > "(" "\xbc\xf6" ")"
{
  token->value = 3;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_KO_KR_TIME > "(" "\xb8\xf1" ")"
{
  token->value = 4;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_KO_KR_TIME > "(" "\xb1\xdd" ")"
{
  token->value = 5;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}

<SC_KO_KR_TIME > "(" "\xc5\xe4" ")"
{
  token->value = 6;
  DB_FMT_LEX_RETURN (FT_WEEKDAY);
}


.
{
  DB_FMT_LEX_RETURN (FT_UNKNOWN);
}


<<EOF >>
{
  cnv_fmt__next = cnv_fmt__input;
  token->type = FT_NONE;
  token->text = token->raw_text = cnv_fmt__input;
  token->length = 0;
  yyterminate ();
}


%%
/*
 * cnv_fmt_analyze() - Initialize lexical scan of instring, using
 *    the given scan mode.
 * return:
 * instring(in) :
 * mode(in) :
 */
  void
cnv_fmt_analyze (const char *instring, FMT_LEX_MODE mode)
{
  assert (instring);
  cnv_fmt__input = cnv_fmt__next = instring;
  yy_init = 1;
  cnv_fmt__unlex = 0;
  cnv_fmt__mode = mode;
}


/*
 * cnv_fmt_unlex() - Push back the current token so that it can be read again.
 * return:
 */
void
cnv_fmt_unlex ()
{
  cnv_fmt__unlex = 1;		/* see rules section */
}


/*
 * cnv_fmt_next_token() - Return pointer to next lexical token in value string.
 * return:
 */
const char *
cnv_fmt_next_token ()
{
  return cnv_fmt__next;
}


/*
 * cnv_fmt_lex_start() - Return the start condition for the given scan mode.
 * return:
 * mode(in) :
 */
static int
cnv_fmt_lex_start (FMT_LEX_MODE mode)
{
  return
    mode == FL_LOCAL_NUMBER ?
    cnv_fmt_lex_start (cnv_fmt_number_mode (intl_zone (LC_MONETARY))) :
    mode == FL_LOCAL_TIME ?
    cnv_fmt_lex_start (cnv_fmt_time_mode (intl_zone (LC_TIME))) :
    mode == FL_US_ENG_NUMBER ? SC_US_ENG_NUMBER :
    mode == FL_KO_KR_NUMBER ? SC_KO_KR_NUMBER :
    mode == FL_US_ENG_TIME ? SC_US_ENG_TIME :
    mode == FL_KO_KR_TIME ? SC_KO_KR_TIME :
    mode == FL_INTEGER_FORMAT ? SC_INTEGER_FORMAT :
    mode == FL_TIME_FORMAT ? SC_TIME_FORMAT :
    mode == FL_BIT_STRING_FORMAT ? SC_BIT_STRING_FORMAT :
    mode == FL_BIT_STRING ? SC_BIT_STRING :
    mode == FL_VALIDATE_DATE_FORMAT ? SC_VALIDATE_DATE_FORMAT :
    mode == FL_VALIDATE_FLOAT_FORMAT ? SC_VALIDATE_FLOAT_FORMAT :
    mode == FL_VALIDATE_INTEGER_FORMAT ? SC_VALIDATE_INTEGER_FORMAT :
    mode == FL_VALIDATE_MONETARY_FORMAT ? SC_VALIDATE_MONETARY_FORMAT :
    mode == FL_VALIDATE_TIME_FORMAT ? SC_VALIDATE_TIME_FORMAT :
    mode == FL_VALIDATE_TIMESTAMP_FORMAT ? SC_VALIDATE_TIMESTAMP_FORMAT :
    mode == FL_VALIDATE_BIT_STRING_FORMAT ? SC_VALIDATE_BIT_STRING_FORMAT :
    INITIAL;
}



/*
 * cnv_fmt_number_mode() - Return the number value scan mode for the given zone.
 * return:
 * zone(in) :
 */
FMT_LEX_MODE
cnv_fmt_number_mode (INTL_ZONE zone)
{
  FMT_LEX_MODE mode;

  switch (zone)
    {
    case INTL_ZONE_US:
      {
	mode = FL_US_ENG_NUMBER;
	break;
      }
    case INTL_ZONE_KR:
      {
	mode = FL_KO_KR_NUMBER;
	break;
      }
    default:
      {
	assert (!"Zone not implemented!");
	mode = FL_US_ENG_NUMBER;
	break;
      }
    }

  return mode;
}



/*
 * cnv_fmt_time_mode() - Return the time value scan mode for the given zone.
 * return:
 * zone(in) :
 */
FMT_LEX_MODE
cnv_fmt_time_mode (INTL_ZONE zone)
{
  FMT_LEX_MODE mode;

  switch (zone)
    {
    case INTL_ZONE_US:
      {
	mode = FL_US_ENG_TIME;
	break;
      }
    case INTL_ZONE_KR:
      {
	mode = FL_KO_KR_TIME;
	break;
      }

    default:
      {
	assert (!"Zone not implemented!");
	mode = FL_US_ENG_TIME;
	break;
      }
    }

  return mode;
}


/*
 * cnv_fmt_exit() - Free buffers when exit.
 * return:
 */
void
cnv_fmt_exit (void)
{
  if (YY_CURRENT_BUFFER != NULL)
    {
      yy_delete_buffer (YY_CURRENT_BUFFER);
      YY_CURRENT_BUFFER = NULL;
    }
}

