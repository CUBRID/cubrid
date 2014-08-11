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
 * string_opfunc.c - Routines that manipulate arbitrary strings
 */

#ident "$Id$"

/* This includes bit strings, character strings, and national character strings
 */

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sys/timeb.h>

#include "chartype.h"
#include "system_parameter.h"
#include "intl_support.h"
#include "error_code.h"
#include "db.h"
#include "memory_alloc.h"
#include "language_support.h"
#include "query_evaluator.h"
#if defined(SERVER_MODE)
#include "thread.h"
#endif

#include "misc_string.h"
#include "md5.h"
#include "porting.h"
#include "crypt_opfunc.h"
#include "base64.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define BYTE_SIZE               (8)
#define QSTR_VALUE_PRECISION(value)                                       \
            ((DB_VALUE_PRECISION(value) == TP_FLOATING_PRECISION_VALUE)  \
                     ?      DB_GET_STRING_LENGTH(value)       :          \
                            DB_VALUE_PRECISION(value))

#define QSTR_MAX_PRECISION(str_type)                                         \
            (QSTR_IS_CHAR(str_type)          ?	DB_MAX_VARCHAR_PRECISION :  \
	     QSTR_IS_NATIONAL_CHAR(str_type) ?	DB_MAX_VARNCHAR_PRECISION : \
	                                        DB_MAX_VARBIT_PRECISION)

#define ABS(i) ((i) >= 0 ? (i) : -(i))

#define STACK_SIZE        100

#define LEAP(y)	  (((y) % 400 == 0) || ((y) % 100 != 0 && (y) % 4 == 0))

#define DBL_MAX_DIGITS    ((int)ceil(DBL_MAX_EXP * log10((double) FLT_RADIX)))
#define UINT64_MAX_HEX_DIGITS 16
#define UINT64_MAX_BIN_DIGITS 64

#define LOB_CHUNK_SIZE	(128 * 1024)
#define REGEX_MAX_ERROR_MSG_SIZE  100

/*
 *  This enumeration type is used to categorize the different
 *  string types into function like groups.
 *
 *      DB_STRING and DB_CHAR    become QSTR_CHAR
 *      DB_NCHAR and DB_VARNCHAR become QSTR_NATIONAL_CHAR
 *      DB_BIT and DB_VARBIT     become QSTR_BIT
 *      All others               become QSTR_UNKNOWN, although this
 *                                      categorizations doesn't apply to
 *                                      any other domain type.
 */
typedef enum
{
  QSTR_UNKNOWN,
  QSTR_CHAR,
  QSTR_NATIONAL_CHAR,
  QSTR_BIT
} QSTR_CATEGORY;

/* AM/PM position references */
enum
{ am_NAME = 0, pm_NAME, Am_NAME, Pm_NAME, AM_NAME, PM_NAME,
  a_m_NAME, p_m_NAME, A_m_NAME, P_m_NAME, A_M_NAME, P_M_NAME
};

/*
 * Number format
 */
typedef enum
{
  N_END = -2,			/*format string end */
  N_INVALID = -1,		/* invalid format */
  N_FORMAT,
  N_SPACE,
  N_TEXT
} NUMBER_FORMAT;

typedef enum
{
  SDT_DAY = 0,
  SDT_MONTH,
  SDT_DAY_SHORT,
  SDT_MONTH_SHORT,
  SDT_AM_PM
} STRING_DATE_TOKEN;

#define WHITE_CHARS             " \t\n"

#define QSTR_DATE_LENGTH 10
#define QSTR_TIME_LENGTH 11
#define QSTR_TIME_STAMPLENGTH 22
#define QSTR_DATETIME_LENGTH 26
/* multiplier ratio for TO_CHAR function : estimate result len/size based on
 * format string len/size : maximum multiplier is given by:
 * - format element : DAY (3)
 * - result :Wednesday (9) */
#define QSTR_TO_CHAR_LEN_MULTIPLIER_RATIO LOC_PARSE_FRMT_TO_TOKEN_MULT

#define MAX_TOKEN_SIZE 16000

#define GUID_STANDARD_BYTES_LENGTH 16

static int qstr_trim (MISC_OPERAND tr_operand,
		      const unsigned char *trim,
		      int trim_length,
		      int trim_size,
		      const unsigned char *src_ptr,
		      DB_TYPE src_type,
		      int src_length,
		      int src_size,
		      INTL_CODESET codeset,
		      unsigned char **res,
		      DB_TYPE * res_type, int *res_length, int *res_size);
static void trim_leading (const unsigned char *trim_charset_ptr,
			  int trim_charset_size,
			  const unsigned char *src_ptr, DB_TYPE src_type,
			  int src_length, int src_size,
			  INTL_CODESET codeset,
			  unsigned char **lead_trimmed_ptr,
			  int *lead_trimmed_length, int *lead_trimmed_size);
static int qstr_pad (MISC_OPERAND pad_operand,
		     int pad_length,
		     const unsigned char *pad_charset_ptr,
		     int pad_charset_length,
		     int pad_charset_size,
		     const unsigned char *src_ptr,
		     DB_TYPE src_type,
		     int src_length,
		     int src_size,
		     INTL_CODESET codeset,
		     unsigned char **result,
		     DB_TYPE * result_type,
		     int *result_length, int *result_size);
static int qstr_eval_like (const char *tar, int tar_length,
			   const char *expr, int expr_length,
			   const char *escape, INTL_CODESET codeset,
			   int coll_id);
#if defined(ENABLE_UNUSED_FUNCTION)
static int kor_cmp (unsigned char *src, unsigned char *dest, int size);
#endif
static int qstr_replace (unsigned char *src_buf,
			 int src_len,
			 int src_size,
			 INTL_CODESET codeset,
			 int coll_id,
			 unsigned char *srch_str_buf,
			 int srch_str_size,
			 unsigned char *repl_str_buf,
			 int repl_str_size,
			 unsigned char **result_buf,
			 int *result_len, int *result_size);
static int qstr_translate (unsigned char *src_ptr,
			   DB_TYPE src_type,
			   int src_size,
			   INTL_CODESET codeset,
			   unsigned char *from_str_ptr,
			   int from_str_size,
			   unsigned char *to_str_ptr,
			   int to_str_size,
			   unsigned char **result_ptr,
			   DB_TYPE * result_type,
			   int *result_len, int *result_size);
static QSTR_CATEGORY qstr_get_category (const DB_VALUE * s);
#if defined (ENABLE_UNUSED_FUNCTION)
static bool is_string (const DB_VALUE * s);
#endif /* ENABLE_UNUSED_FUNCTION */
static bool is_char_string (const DB_VALUE * s);
static bool is_integer (const DB_VALUE * i);
static bool is_number (const DB_VALUE * n);
static int qstr_grow_string (DB_VALUE * src_string,
			     DB_VALUE * result, int new_size);
#if defined (ENABLE_UNUSED_FUNCTION)
static int qstr_append (unsigned char *s1,
			int s1_length,
			int s1_precision,
			DB_TYPE s1_type,
			const unsigned char *s2,
			int s2_length,
			int s2_precision,
			DB_TYPE s2_type,
			INTL_CODESET codeset,
			int *result_length,
			int *result_size, DB_DATA_STATUS * data_status);
#endif
static int qstr_concatenate (const unsigned char *s1,
			     int s1_length,
			     int s1_precision,
			     DB_TYPE s1_type,
			     const unsigned char *s2,
			     int s2_length,
			     int s2_precision,
			     DB_TYPE s2_type,
			     INTL_CODESET codeset,
			     unsigned char **result,
			     int *result_length,
			     int *result_size,
			     DB_TYPE * result_type,
			     DB_DATA_STATUS * data_status);
static int qstr_bit_concatenate (const unsigned char *s1,
				 int s1_length,
				 int s1_precision,
				 DB_TYPE s1_type,
				 const unsigned char *s2,
				 int s2_length,
				 int s2_precision,
				 DB_TYPE s2_type,
				 unsigned char **result,
				 int *result_length,
				 int *result_size,
				 DB_TYPE * result_type,
				 DB_DATA_STATUS * data_status);
static bool varchar_truncated (const unsigned char *s,
			       DB_TYPE s_type,
			       int s_length,
			       int used_chars, INTL_CODESET codeset);
static bool varbit_truncated (const unsigned char *s,
			      int s_length, int used_bits);
static void bit_ncat (unsigned char *r, int offset, const unsigned char *s,
		      int n);
static int bstring_fls (const char *s, int n);
static int qstr_bit_coerce (const unsigned char *src,
			    int src_length,
			    int src_precision,
			    DB_TYPE src_type,
			    unsigned char **dest,
			    int *dest_length,
			    int dest_precision,
			    DB_TYPE dest_type, DB_DATA_STATUS * data_status);
static int qstr_coerce (const unsigned char *src, int src_length,
			int src_precision, DB_TYPE src_type,
			INTL_CODESET src_codeset, INTL_CODESET dest_codeset,
			unsigned char **dest, int *dest_length,
			int *dest_size, int dest_precision,
			DB_TYPE dest_type, DB_DATA_STATUS * data_status);
static int qstr_position (const char *sub_string, const int sub_size,
			  const int sub_length,
			  const char *src_string, const char *src_end,
			  const char *src_string_bound,
			  int src_length, int coll_id,
			  bool is_forward_search, int *position);
static int qstr_bit_position (const unsigned char *sub_string,
			      int sub_length,
			      const unsigned char *src_string,
			      int src_length, int *position);
static int shift_left (unsigned char *bit_string, int bit_string_size);
static int qstr_substring (const unsigned char *src,
			   int src_length,
			   int start,
			   int length,
			   INTL_CODESET codeset,
			   unsigned char **r, int *r_length, int *r_size);
static int qstr_bit_substring (const unsigned char *src,
			       int src_length,
			       int start,
			       int length, unsigned char **r, int *r_length);
static void left_nshift (const unsigned char *bit_string, int bit_string_size,
			 int shift_amount, unsigned char *r, int r_size);
static int qstr_ffs (int v);
static int hextoi (char hex_char);
static int adjust_precision (char *data, int precision, int scale);
static int date_to_char (const DB_VALUE * src_value,
			 const DB_VALUE * format_str,
			 const DB_VALUE * date_lang, DB_VALUE * result_str,
			 const TP_DOMAIN * domain);
static int number_to_char (const DB_VALUE * src_value,
			   const DB_VALUE * format_str,
			   const DB_VALUE * number_lang,
			   DB_VALUE * result_str, const TP_DOMAIN * domain);
static int lob_to_bit_char (const DB_VALUE * src_value,
			    DB_VALUE * result_value, DB_TYPE lob_type,
			    int max_length);
static int lob_from_file (const char *path, const DB_VALUE * src_value,
			  DB_VALUE * lob_value, DB_TYPE lob_type);
static int lob_length (const DB_VALUE * src_value, DB_VALUE * result_value);

static int make_number_to_char (const INTL_LANG lang, char *num_string,
				char *format_str, int *length,
				DB_CURRENCY currency, char **result_str);
static int make_scientific_notation (char *src_string, int cipher);
static int roundoff (const INTL_LANG lang, char *src_string, int flag,
		     int *cipher, char *format);
static int scientific_to_decimal_string (const INTL_LANG lang,
					 char *src_string,
					 char **scientific_str);
static int to_number_next_state (const int previous_state,
				 const int input_char,
				 const INTL_LANG number_lang_id);
static int make_number (char *src, char *last_src, INTL_CODESET codeset,
			char *token, int *token_length, DB_VALUE * r,
			const int precision, const int scale,
			const INTL_LANG number_lang_id);
static int get_number_token (const INTL_LANG lang, char *fsp, int *length,
			     char *last_position, char **next_fsp);
static int get_next_format (char *sp, const INTL_CODESET codeset,
			    DB_TYPE str_type, int *format_length,
			    char **next_pos);
static int get_cur_year (void);
static int get_cur_month (void);
/* utility functions */
static int add_and_normalize_date_time (int *years,
					int *months,
					int *days,
					int *hours,
					int *minutes,
					int *seconds,
					int *milliseconds,
					DB_BIGINT y,
					DB_BIGINT m,
					DB_BIGINT d,
					DB_BIGINT h,
					DB_BIGINT mi,
					DB_BIGINT s, DB_BIGINT ms);
static int sub_and_normalize_date_time (int *years,
					int *months,
					int *days,
					int *hours,
					int *minutes,
					int *seconds,
					int *milliseconds,
					DB_BIGINT y,
					DB_BIGINT m,
					DB_BIGINT d,
					DB_BIGINT h,
					DB_BIGINT mi,
					DB_BIGINT s, DB_BIGINT ms);
static void set_time_argument (struct tm *dest, int year, int month, int day,
			       int hour, int min, int sec);
static long calc_unix_timestamp (struct tm *time_argument);
#if defined (ENABLE_UNUSED_FUNCTION)
static int parse_for_next_int (char **ch, char *output);
#endif
static int db_str_to_millisec (const char *str);
static void copy_and_shift_values (int shift, int n, DB_BIGINT * first, ...);
static DB_BIGINT get_single_unit_value (char *expr, DB_BIGINT int_val);
static int db_date_add_sub_interval_expr (DB_VALUE * result,
					  const DB_VALUE * date,
					  const DB_VALUE * expr,
					  const int unit, bool is_add);
static int db_date_add_sub_interval_days (DB_VALUE * result,
					  const DB_VALUE * date,
					  const DB_VALUE * db_days,
					  bool is_add);
static int db_get_datetime_from_dbvalue (const DB_VALUE * src_date,
					 int *year, int *month, int *day,
					 int *hour, int *minute, int *second,
					 int *millisecond, const char **endp);
static int db_get_time_from_dbvalue (const DB_VALUE * src_date, int *hour,
				     int *minute, int *second,
				     int *millisecond);
static int db_round_dbvalue_to_int (const DB_VALUE * src, int *result);
static int db_get_next_like_pattern_character (const char *const pattern,
					       const int length,
					       const INTL_CODESET codeset,
					       const bool has_escape_char,
					       const char *escape_str,
					       int *const position,
					       char **crt_char_p,
					       bool * const is_escaped);
static bool is_safe_last_char_for_like_optimization (const char *chr,
						     const bool is_escaped,
						     INTL_CODESET codeset);
static int db_check_or_create_null_term_string (const DB_VALUE * str_val,
						char *pre_alloc_buf,
						int pre_alloc_buf_size,
						bool ignore_prec_spaces,
						bool ignore_trail_spaces,
						char **str_out,
						bool * do_alloc);
static bool is_str_valid_number (char *num_p, int base);
static bool is_valid_ip_slice (const char *ipslice);

/* reads cnt digits until non-digit char reached,
 * returns nr of characters traversed
 */
static int parse_digits (char *s, int *nr, int cnt);
static int parse_time_string (const char *timestr, int timestr_size,
			      int *sign, int *h, int *m, int *s, int *ms);
static int get_string_date_token_id (const STRING_DATE_TOKEN token_type,
				     const INTL_LANG intl_lang_id,
				     const char *cs,
				     const INTL_CODESET codeset,
				     int *token_id, int *token_size);
static int print_string_date_token (const STRING_DATE_TOKEN token_type,
				    const INTL_LANG intl_lang_id,
				    const INTL_CODESET codeset,
				    int token_id, int case_mode,
				    char *buffer, int *token_size);
static void convert_locale_number (char *sz, const int size,
				   const INTL_LANG src_locale,
				   const INTL_LANG dst_locale);

#define TRIM_FORMAT_STRING(sz, n) {if (strlen(sz) > n) sz[n] = 0;}
#define WHITESPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#define ALPHABETICAL(c) (((c) >= 'A' && (c) <= 'Z') || \
			 ((c) >= 'a' && (c) <= 'z'))
#define DIGIT(c) ((c) >= '0' && (c) <= '9')
/* concatenate a char to s */
#define STRCHCAT(s, c) \
  {\
    char __cch__[2];\
    __cch__[0] = c;__cch__[1] = 0; strcat(s, __cch__);\
  }

#define SKIP_SPACES(ch, end) 	do {\
	while (ch != end && char_isspace(*(ch))) (ch)++; \
}while(0)


/*
 *  Public Functions for Strings - Bit and Character
 */

/*
 * db_string_compare () -
 *
 * Arguments:
 *                string1: Left side of compare.
 *                string2: Right side of compare
 *                 result: Integer result of comparison.
 *            data_status: Status of errors.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE   :
 *        <string1> or <string2> are not character strings.
 *
 *    ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *        <string1> and <string2> have differing character code sets.
 *
 */

int
db_string_compare (const DB_VALUE * string1, const DB_VALUE * string2,
		   DB_VALUE * result)
{
  QSTR_CATEGORY string1_category, string2_category;
  int cmp_result = 0;
  DB_TYPE str1_type, str2_type;

  /* Assert that DB_VALUE structures have been allocated. */
  assert (string1 != (DB_VALUE *) NULL);
  assert (string2 != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  /* Categorize the two input parameters and check for errors.
     Verify that the parameters are both character strings.
     Verify that the input strings belong to compatible categories. */
  string1_category = qstr_get_category (string1);
  string2_category = qstr_get_category (string2);

  str1_type = DB_VALUE_DOMAIN_TYPE (string1);
  str2_type = DB_VALUE_DOMAIN_TYPE (string2);

  if (!QSTR_IS_ANY_CHAR_OR_BIT (str1_type)
      || !QSTR_IS_ANY_CHAR_OR_BIT (str2_type))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      return ER_QSTR_INVALID_DATA_TYPE;
    }
  if (string1_category != string2_category)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QSTR_INCOMPATIBLE_CODE_SETS, 0);
      return ER_QSTR_INCOMPATIBLE_CODE_SETS;
    }

  /* A string which is NULL (not the same as a NULL string) is
     ordered less than a string which is not NULL.  Two strings
     which are NULL are ordered equivalently.
     If both strings are not NULL, then the strings themselves
     are compared. */
  if (DB_IS_NULL (string1) && !DB_IS_NULL (string2))
    {
      cmp_result = -1;
    }
  else if (!DB_IS_NULL (string1) && DB_IS_NULL (string2))
    {
      cmp_result = 1;
    }
  else if (DB_IS_NULL (string1) && DB_IS_NULL (string2))
    {
      cmp_result = 0;
    }
  else if (DB_GET_STRING_CODESET (string1) != DB_GET_STRING_CODESET (string2))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QSTR_INCOMPATIBLE_CODE_SETS, 0);
      return ER_QSTR_INCOMPATIBLE_CODE_SETS;
    }
  else
    {
      int coll_id;

      switch (string1_category)
	{
	case QSTR_CHAR:
	case QSTR_NATIONAL_CHAR:

	  assert (DB_GET_STRING_CODESET (string1)
		  == DB_GET_STRING_CODESET (string2));

	  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (string1),
			       DB_GET_STRING_COLLATION (string2), coll_id);

	  if (coll_id == -1)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
	      return ER_QSTR_INCOMPATIBLE_COLLATIONS;
	    }

	  coll_id = DB_GET_STRING_COLLATION (string1);
	  assert (DB_GET_STRING_COLLATION (string1)
		  == DB_GET_STRING_COLLATION (string2));

	  cmp_result =
	    QSTR_COMPARE (coll_id,
			  (unsigned char *) DB_PULL_STRING (string1),
			  (int) DB_GET_STRING_SIZE (string1),
			  (unsigned char *) DB_PULL_STRING (string2),
			  (int) DB_GET_STRING_SIZE (string2));
	  break;
	case QSTR_BIT:
	  cmp_result =
	    varbit_compare ((unsigned char *) DB_PULL_STRING (string1),
			    (int) DB_GET_STRING_SIZE (string1),
			    (unsigned char *) DB_PULL_STRING (string2),
			    (int) DB_GET_STRING_SIZE (string2));
	  break;
	default:		/* QSTR_UNKNOWN */
	  break;
	}
    }

  if (cmp_result < 0)
    {
      cmp_result = -1;
    }
  else if (cmp_result > 0)
    {
      cmp_result = 1;
    }
  DB_MAKE_INTEGER (result, cmp_result);

  return NO_ERROR;
}

/*
 * db_string_unique_prefix () -
 *
 * Arguments:
 *                string1: (IN) Left side of compare.
 *                string2: (IN) Right side of compare.
 *                 result: (OUT) string such that > string1, and <= string2.
 *
 * Returns: int
 *
 * Errors:
 *    (TBD)
 *
 * Note:
 *    The purpose of this routine is to find a prefix that is greater
 *    than or equal to the first string but strictly less than the second
 *    string.
 *
 *    This routine assumes:
 *       a) The second string is strictly greater than the first
 *           (according to the ANSI SQL string comparison rules).
 *       b) The two strings are both of the same 'type', although one may be
 *           'fixed' and the other may be 'varying'.
 *       c) No padding is done.
 *
 * Assert:
 *
 *    1. string1 != (DB_VALUE *)NULL
 *    2. string2 != (DB_VALUE *)NULL
 *    3. result  != (DB_VALUE *)NULL
 *
 */
#if 1
int
db_string_unique_prefix (const DB_VALUE * db_string1,
			 const DB_VALUE * db_string2, DB_VALUE * db_result,
			 TP_DOMAIN * key_domain)
{
  DB_TYPE result_type = (DB_TYPE) 0;
  int error_status = NO_ERROR;
  int precision;
  DB_VALUE tmp_result;
  int c;

  /* Assertions */
  assert (db_string1 != (DB_VALUE *) NULL);
  assert (db_string2 != (DB_VALUE *) NULL);
  assert (db_result != (DB_VALUE *) NULL);

  error_status = db_string_compare (db_string1, db_string2, &tmp_result);
  if ((error_status != NO_ERROR) ||
      ((c = DB_GET_INTEGER (&tmp_result)) &&
       ((!key_domain->is_desc && c > 0) || (key_domain->is_desc && c < 0))))
    {
      DB_MAKE_NULL (db_result);
#if defined(CUBRID_DEBUG)
      if (error_status == ER_QSTR_INVALID_DATA_TYPE)
	{
	  printf ("db_string_unique_prefix(): non-string type: %s and %s\n",
		  pr_type_name (DB_VALUE_DOMAIN_TYPE (db_string1)),
		  pr_type_name (DB_VALUE_DOMAIN_TYPE (db_string2)));
	}
      if (error_status == ER_QSTR_INCOMPATIBLE_CODE_SETS)
	{
	  printf
	    ("db_string_unique_prefix(): incompatible types: %s and %s\n",
	     pr_type_name (DB_VALUE_DOMAIN_TYPE (db_string1)),
	     pr_type_name (DB_VALUE_DOMAIN_TYPE (db_string2)));
	}
      if (DB_GET_INTEGER (&tmp_result) > 0)
	{
	  printf
	    ("db_string_unique_prefix(): string1 %s, greater than string2 %s\n",
	     DB_GET_STRING (db_string1), DB_GET_STRING (db_string2));
	}
#endif
      return ER_GENERIC_ERROR;
    }

  precision = DB_VALUE_PRECISION (db_string1);
  /* Determine the result type */
  result_type = DB_VALUE_DOMAIN_TYPE (db_string1);
  if (QSTR_IS_CHAR (result_type))
    {
      result_type = DB_TYPE_VARCHAR;
    }
  else if (QSTR_IS_NATIONAL_CHAR (result_type))
    {
      result_type = DB_TYPE_VARNCHAR;
    }
  else if (QSTR_IS_BIT (result_type))
    {
      result_type = DB_TYPE_VARBIT;
    }
  else
    {
      DB_MAKE_NULL (db_result);
#if defined(CUBRID_DEBUG)
      printf ("db_string_unique_prefix(): non-string type: %s and %s\n",
	      pr_type_name (DB_VALUE_DOMAIN_TYPE (db_string1)),
	      pr_type_name (DB_VALUE_DOMAIN_TYPE (db_string2)));
#endif
      return ER_GENERIC_ERROR;
    }

  /* A string which is NULL (not the same as a NULL string) is
     ordered less than a string which is not NULL.  Since string2 is
     assumed to be strictly > string1, string2 can never be NULL. */
  if (DB_IS_NULL (db_string1))
    {
      db_value_domain_init (db_result, result_type, precision, 0);
    }

  /* Find the first byte where the 2 strings differ.  Set the result
     accordingly. */
  else
    {
      int size1, size2, result_size, pad_size = 0;
      unsigned char *string1, *string2, *result, *key = NULL, pad[2], *t;
      INTL_CODESET codeset;
      int num_bits = -1;
      int collation_id;
      bool bit_use_str2_size = false;

      string1 = (unsigned char *) DB_GET_STRING (db_string1);
      size1 = (int) DB_GET_STRING_SIZE (db_string1);
      string2 = (unsigned char *) DB_GET_STRING (db_string2);
      size2 = (int) DB_GET_STRING_SIZE (db_string2);
      codeset = (INTL_CODESET) DB_GET_STRING_CODESET (db_string1);
      collation_id = DB_GET_STRING_COLLATION (db_string1);

      assert (collation_id == DB_GET_STRING_COLLATION (db_string2));

      if (result_type == DB_TYPE_VARBIT)
	{
	  collation_id = LANG_COLL_ISO_BINARY;
	}

      if (string1 == NULL || string2 == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_PARAMETER, 0);
	  return ER_QPROC_INVALID_PARAMETER;
	}

      intl_pad_char (codeset, pad, &pad_size);

    trim_again:
      /* We need to implicitly trim both strings since we don't want padding
         for the result (its of varying type) and since padding can mask the
         logical end of both of the strings.  Trimming depends on codeset. */
      if (pad_size == 1)
	{
	  for (t = string1 + (size1 - 1); t >= string1 && *t == pad[0];
	       t--, size1--)
	    {
	      ;
	    }
	  for (t = string2 + (size2 - 1); t >= string2 && *t == pad[0];
	       t--, size2--)
	    {
	      ;
	    }
	}
      else
	{
	  assert (pad_size == 2);

	  for (t = string1 + (size1 - 2); t >= string1 && *t == pad[0]
	       && *(t + 1) == pad[1]; t--, t--, size1--, size1--)
	    {
	      ;
	    }

	  for (t = string2 + (size2 - 2); t >= string2 && *t == pad[0]
	       && *(t + 1) == pad[1]; t--, t--, size2--, size2--)
	    {
	      ;
	    }

	  if (codeset == INTL_CODESET_KSC5601_EUC)
	    {
	      /* trim also ASCII space */
	      intl_pad_char (INTL_CODESET_ISO88591, pad, &pad_size);
	      goto trim_again;
	    }
	}

      if (result_type == DB_TYPE_VARBIT)
	{
	  int size;
	  const unsigned char *t2;

	  for (size = 1, t = string1, t2 = string2;
	       size <= size1 && size <= size2; size++, t++, t2++)
	    {
	      if (*t != *t2)
		{
		  size++;
		  break;
		}
	    }

	  if (!(key_domain->is_desc))
	    {			/* normal index */
	      key = string2;

	      /* search until non-zero differentiating byte */
	      t2++;
	      size++;

	      bit_use_str2_size = false;
	      if (size >= size2)
		{
		  size = size2;
		  bit_use_str2_size = true;
		}
	    }
	  else
	    {
	      /* reverse index */
	      assert (key_domain->is_desc);

	      t++;
	      size++;

	      if (size >= size1)
		{
		  /* str1 exhaused or at last byte, we use string2 as key */
		  key = string2;
		  size = size2;
		  bit_use_str2_size = true;
		}
	      else
		{
		  /* pos is already at the next diffentiating byte */
		  assert (size < size1);
		  key = string1;
		  bit_use_str2_size = false;
		}
	    }

	  num_bits = bit_use_str2_size ? (db_string2->data.ch.medium.size)
	    : (size * BYTE_SIZE);

	  result_size = (num_bits + 7) / 8;
	}
      else
	{
	  error_status = QSTR_SPLIT_KEY (collation_id, key_domain->is_desc,
					 string1, size1, string2, size2, &key,
					 &result_size);
	}
      assert (error_status == NO_ERROR);

      assert (key != NULL);

      result = db_private_alloc (NULL, result_size + 1);
      if (result)
	{
	  if (result_size)
	    {
	      (void) memcpy (result, key, result_size);
	    }
	  result[result_size] = 0;
	  db_value_domain_init (db_result, result_type, precision, 0);
	  error_status = db_make_db_char (db_result, codeset, collation_id,
					  (const char *) result,
					  (result_type ==
					   DB_TYPE_VARBIT ? num_bits :
					   result_size));
	  db_result->need_clear = true;
	}
      else
	{
	  /* will already be set by memory mgr */
	  assert (er_errid () != NO_ERROR);
	  error_status = er_errid ();
	}
    }

#if !defined(NDEBUG)
  if (error_status == NO_ERROR)
    {
      int err_status2 = NO_ERROR;
      int c1 = 1, c2 = -1;

      err_status2 = db_string_compare (db_string1, db_result, &tmp_result);
      if (err_status2 == NO_ERROR)
	{
	  c1 = DB_GET_INTEGER (&tmp_result);
	}
      err_status2 = db_string_compare (db_result, db_string2, &tmp_result);
      if (err_status2 == NO_ERROR)
	{
	  c2 = DB_GET_INTEGER (&tmp_result);
	}

      if (!key_domain->is_desc)
	{
	  assert (c1 < 0 && c2 <= 0);
	}
      else
	{
	  assert (c1 > 0 && c2 >= 0);
	}
    }
#endif

  return (error_status);
}
#else
int
db_string_unique_prefix (const DB_VALUE * db_string1,
			 const DB_VALUE * db_string2, DB_VALUE * db_result)
{
  DB_TYPE result_type = 0;
  int error_status = NO_ERROR;
  int precision, num_bits = -1;
  DB_TYPE string_type;

  /* Assertions */
  assert (db_string1 != (DB_VALUE *) NULL);
  assert (db_string2 != (DB_VALUE *) NULL);
  assert (db_result != (DB_VALUE *) NULL);

  precision = DB_VALUE_PRECISION (db_string1);
  string_type = DB_VALUE_DOMAIN_TYPE (db_string1);

  /* Determine the result type */
  if (QSTR_IS_CHAR (string_type))
    {
      result_type = DB_TYPE_VARCHAR;
    }
  else if (QSTR_IS_NATIONAL_CHAR (string_type))
    {
      result_type = DB_TYPE_VARNCHAR;
    }
  else if (QSTR_IS_BIT (string_type))
    {
      result_type = DB_TYPE_VARBIT;
    }
  else
    {
      result_type = DB_TYPE_NULL;
      DB_MAKE_NULL (db_result);
#if defined(CUBRID_DEBUG)
      printf ("db_string_unique_prefix called with non-string type: %s\n",
	      pr_type_name (string_type));
#endif
      return ER_GENERIC_ERROR;
    }

  /*
   * A string which is NULL (not the same as a NULL string) is
   * ordered less than a string which is not NULL.  Since string2 is
   * assumed to be strictly > string1, string2 can never be NULL.
   */
  if (DB_IS_NULL (db_string1))
    {
      db_value_domain_init (db_result, result_type, precision, 0);
    }
  /*
   *  Find the first byte where the 2 strings differ.  Set the result
   *  accordingly.
   */
  else
    {
      int string1_size = DB_GET_STRING_SIZE (db_string1);
      int string2_size = DB_GET_STRING_SIZE (db_string2);
      const unsigned char *string1 =
	(const unsigned char *) DB_GET_STRING (db_string1);
      const unsigned char *string2 =
	(const unsigned char *) DB_GET_STRING (db_string2);
      unsigned char *result;
      const unsigned char *key;
      int result_size;
      INTL_CODESET codeset =
	(INTL_CODESET) DB_GET_STRING_CODESET ((DB_VALUE *) db_string1);

      /* We need to implicitly trim both strings since we don't want padding
       * for the result (its of varying type) and since padding can mask the
       * logical end of both of the strings.  We need to be careful how the
       * trimming is done.  Char and varchar can do the normal trim, nchar
       * and varnchar need to worry about codeset and pad chars, and bit
       * and varbit don't want to trim at all.
       */
      if (result_type == DB_TYPE_VARCHAR)
	{
	  for (;
	       string1_size && string1[string1_size - 1] == ' ';
	       string1_size--)
	    {
	      ;			/* do nothing */
	    }
	  for (;
	       string2_size && string2[string2_size - 1] == ' ';
	       string2_size--)
	    {
	      ;			/* do nothing */
	    }
	}
      else if (result_type == DB_TYPE_VARNCHAR)
	{
	  /* This is going to look a lot like qstr_trim_trailing.  We don't
	   * call qstr_trim_trailing because he works on length of characters
	   * and we need to work on length of bytes.  We could calculate the
	   * length in characters, but that requires a full scan of the
	   * strings which is not necessary.
	   */
	  int i, pad_size, trim_length, cmp_flag, prev_size;
	  unsigned char *prev_ptr, *current_ptr, pad[2];

	  intl_pad_char (codeset, pad, &pad_size);

	  trim_length = string1_size;
	  current_ptr = (unsigned char *) (string1 + string1_size);
	  for (i = 0, cmp_flag = 0;
	       (i < string1_size) && (cmp_flag == 0); i++)
	    {
	      prev_ptr = qstr_prev_char (current_ptr, codeset, &prev_size);
	      if (pad_size == prev_size)
		{
		  cmp_flag =
		    memcmp ((char *) prev_ptr, (char *) pad, pad_size);

		  if (cmp_flag == 0)
		    {
		      trim_length -= pad_size;
		    }
		}
	      else
		{
		  cmp_flag = 1;
		}

	      current_ptr = prev_ptr;
	    }
	  string1_size = trim_length;

	  trim_length = string2_size;
	  current_ptr = (unsigned char *) (string2 + string2_size);
	  for (i = 0, cmp_flag = 0;
	       (i < string2_size) && (cmp_flag == 0); i++)
	    {
	      prev_ptr = qstr_prev_char (current_ptr, codeset, &prev_size);
	      if (pad_size == prev_size)
		{
		  cmp_flag =
		    memcmp ((char *) prev_ptr, (char *) pad, pad_size);

		  if (cmp_flag == 0)
		    {
		      trim_length -= pad_size;
		    }
		}
	      else
		{
		  cmp_flag = 1;
		}

	      current_ptr = prev_ptr;
	    }
	  string2_size = trim_length;

	}

      /* now find the first byte where the strings differ */
      for (result_size = 0;
	   result_size < string1_size && result_size < string2_size &&
	   string1[result_size] == string2[result_size]; result_size++)
	{
	  ;			/* do nothing */
	}

      /* Check for string2 < string1.  This check can only be done
       * when we haven't exhausted one of the strings.  If string2
       * is exhausted it is an error.
       */
      if ((result_size != string1_size) &&
	  ((result_size == string2_size) ||
	   (string2[result_size] < string1[result_size])))
	{
#if defined(CUBRID_DEBUG)
	  printf ("db_string_unique_prefix called with ");
	  printf ("string1: %s, greater than string2: %s\n",
		  string1, string2);
#endif
	  error_status = ER_GENERIC_ERROR;
	}
      else
	{

	  if (result_size == string1_size || result_size == string2_size - 1)
	    {
	      key = string1;
	      result_size = string1_size;
	      /* if we have bits, we need all the string1 bits.  Remember
	       * that you may not use all of the bits in the last byte.
	       */
	      if (result_type == DB_TYPE_VARBIT)
		{
		  num_bits = db_string1->data.ch.medium.size;
		}
	    }
	  else
	    {
	      result_size += 1;
	      key = string2;
	      /* if we have bits, we will take all the bits for the
	       * differentiating byte.  This is fine since in this branch
	       * we are guaranteed not to be at the end of either string.
	       */
	      if (result_type == DB_TYPE_VARBIT)
		{
		  num_bits = result_size * BYTE_SIZE;
		}
	    }

	  result = db_private_alloc (NULL, result_size + 1);
	  if (result)
	    {
	      if (result_size)
		{
		  memcpy (result, key, result_size);
		}
	      result[result_size] = 0;
	      db_value_domain_init (db_result, result_type, precision, 0);
	      if (result_type == DB_TYPE_VARBIT)
		{
		  error_status = db_make_db_char (db_result, codeset,
						  (const char *) result,
						  num_bits);
		}
	      else
		{
		  error_status = db_make_db_char (db_result, codeset,
						  (const char *) result,
						  result_size);
		}

	      db_result->need_clear = true;
	    }
	  else
	    {
	      /* will already be set by memory mgr */
	      assert (er_errid () != NO_ERROR);
	      error_status = er_errid ();
	    }
	}
    }

  return (error_status);
}
#endif

/*
 * db_string_concatenate () -
 *
 * Arguments:
 *          string1: Left string to concatenate.
 *          string2: Right string to concatenate.
 *           result: Result of concatenation of both strings.
 *      data_status: DB_DATA_STATUS which indicates if truncation occurred.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *          <string1> or <string2> not string types.
 *
 *      ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *          <string1> or <string2> have different character code sets
 *          or are not all bit strings.
 *
 */

int
db_string_concatenate (const DB_VALUE * string1,
		       const DB_VALUE * string2,
		       DB_VALUE * result, DB_DATA_STATUS * data_status)
{
  QSTR_CATEGORY string1_code_set, string2_code_set;
  int error_status = NO_ERROR;
  DB_TYPE string_type1, string_type2;
  bool is_inplace_concat;

  /*
   *  Initialize status value
   */
  *data_status = DATA_STATUS_OK;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (string1 != (DB_VALUE *) NULL);
  assert (string2 != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  is_inplace_concat = false;	/* init */

  /* check iff is in-place update */
  if (string1 == result || string2 == result)
    {
      is_inplace_concat = true;
    }

  /*
   *  Categorize the parameters into respective code sets.
   *  Verify that the parameters are both character strings.
   *  Verify that the input strings belong to compatible code sets.
   */
  string1_code_set = qstr_get_category (string1);
  string2_code_set = qstr_get_category (string2);

  string_type1 = DB_VALUE_DOMAIN_TYPE (string1);
  string_type2 = DB_VALUE_DOMAIN_TYPE (string2);

  if (!QSTR_IS_ANY_CHAR_OR_BIT (string_type1)
      || !QSTR_IS_ANY_CHAR_OR_BIT (string_type2))
    {
      /* ORACLE7 ServerSQL Language Reference Manual 3-4;
       * Although ORACLE treats zero-length character strings as
       * nulls, concatenating a zero-length character string with another
       * operand always results in the other operand, rather than a null.
       * However, this may not continue to be true in future versions of
       * ORACLE. To concatenate an expression that might be null, use the
       * NVL function to explicitly convert the expression to a
       * zero-length string.
       */
      if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
	{
	  if (DB_IS_NULL (string1) && QSTR_IS_ANY_CHAR_OR_BIT (string_type2))
	    {
	      pr_clone_value ((DB_VALUE *) string2, result);
	    }
	  else if (DB_IS_NULL (string2)
		   && QSTR_IS_ANY_CHAR_OR_BIT (string_type1))
	    {
	      pr_clone_value ((DB_VALUE *) string1, result);
	    }
	  else
	    {
	      error_status = ER_QSTR_INVALID_DATA_TYPE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    }
	}
      else
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
    }
  else if ((string1_code_set != string2_code_set))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if (DB_IS_NULL (string1) || DB_IS_NULL (string2))
    {
      bool check_empty_string;

      /* ORACLE7 ServerSQL Language Reference Manual 3-4;
       * Although ORACLE treats zero-length character strings as
       * nulls, concatenating a zero-length character string with another
       * operand always results in the other operand, rather than a null.
       * However, this may not continue to be true in future versions of
       * ORACLE. To concatenate an expression that might be null, use the
       * NVL function to explicitly convert the expression to a
       * zero-length string.
       */
      check_empty_string =
	prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) ? true : false;

      if (check_empty_string && DB_IS_NULL (string1)
	  && QSTR_IS_ANY_CHAR_OR_BIT (string_type2))
	{
	  pr_clone_value ((DB_VALUE *) string2, result);
	}
      else if (check_empty_string && DB_IS_NULL (string2)
	       && QSTR_IS_ANY_CHAR_OR_BIT (string_type1))
	{
	  pr_clone_value ((DB_VALUE *) string1, result);
	}
      else
	{
	  if (QSTR_IS_CHAR (string_type1))
	    {
	      if (string_type1 == DB_TYPE_VARCHAR
		  || string_type2 == DB_TYPE_VARCHAR)
		{
		  db_value_domain_init (result, DB_TYPE_VARCHAR,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	      else
		{
		  db_value_domain_init (result, DB_TYPE_CHAR,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	    }
	  else if (QSTR_IS_NATIONAL_CHAR (string_type1))
	    {
	      if (string_type1 == DB_TYPE_VARNCHAR
		  || string_type2 == DB_TYPE_VARNCHAR)
		{
		  db_value_domain_init (result, DB_TYPE_VARNCHAR,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	      else
		{
		  db_value_domain_init (result, DB_TYPE_NCHAR,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	    }
	  else
	    {
	      if (string_type1 == DB_TYPE_VARBIT
		  || string_type2 == DB_TYPE_VARBIT)
		{
		  db_value_domain_init (result, DB_TYPE_VARBIT,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	      else
		{
		  db_value_domain_init (result, DB_TYPE_BIT,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	    }
	}
    }
  else
    {
      unsigned char *r;
      int r_length, r_size;
      DB_TYPE r_type;

      if (string1_code_set == QSTR_BIT)
	{
	  int result_domain_length;

	  error_status =
	    qstr_bit_concatenate ((unsigned char *) DB_PULL_STRING (string1),
				  (int) DB_GET_STRING_LENGTH (string1),
				  (int) QSTR_VALUE_PRECISION (string1),
				  DB_VALUE_DOMAIN_TYPE (string1),
				  (unsigned char *) DB_PULL_STRING (string2),
				  (int) DB_GET_STRING_LENGTH (string2),
				  (int) QSTR_VALUE_PRECISION (string2),
				  DB_VALUE_DOMAIN_TYPE (string2),
				  &r, &r_length,
				  &r_size, &r_type, data_status);

	  if (error_status == NO_ERROR)
	    {
	      if ((DB_VALUE_PRECISION (string1) ==
		   TP_FLOATING_PRECISION_VALUE) ||
		  (DB_VALUE_PRECISION (string2) ==
		   TP_FLOATING_PRECISION_VALUE))
		{
		  result_domain_length = TP_FLOATING_PRECISION_VALUE;
		}
	      else
		{
		  result_domain_length = MIN (DB_MAX_BIT_LENGTH,
					      DB_VALUE_PRECISION (string1) +
					      DB_VALUE_PRECISION (string2));
		}

	      qstr_make_typed_string (r_type,
				      result,
				      result_domain_length,
				      (char *) r, r_length,
				      DB_GET_STRING_CODESET (string1),
				      DB_GET_STRING_COLLATION (string1));
	      result->need_clear = true;
	    }
	}
      else
	{
	  DB_VALUE temp;
	  int result_domain_length;
	  int common_coll;
	  INTL_CODESET codeset = DB_GET_STRING_CODESET (string1);

	  DB_MAKE_NULL (&temp);

	  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (string1),
			       DB_GET_STRING_COLLATION (string2),
			       common_coll);
	  if (common_coll == -1)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
	      return ER_QSTR_INCOMPATIBLE_COLLATIONS;
	    }

	  if (DB_GET_STRING_CODESET (string1)
	      != DB_GET_STRING_CODESET (string2))
	    {
	      DB_DATA_STATUS data_status;

	      codeset = lang_get_collation (common_coll)->codeset;

	      if (DB_GET_STRING_CODESET (string1) != codeset)
		{
		  db_value_domain_init (&temp, string_type1,
					(int) QSTR_VALUE_PRECISION (string1),
					0);

		  db_string_put_cs_and_collation (&temp, codeset,
						  common_coll);
		  error_status =
		    db_char_string_coerce (string1, &temp, &data_status);

		  if (error_status != NO_ERROR)
		    {
		      pr_clear_value (&temp);
		      return error_status;
		    }

		  assert (data_status == DATA_STATUS_OK);

		  string1 = &temp;
		}
	      else
		{
		  assert (DB_GET_STRING_CODESET (string2) != codeset);

		  db_value_domain_init (&temp, string_type2,
					(int) QSTR_VALUE_PRECISION (string2),
					0);

		  db_string_put_cs_and_collation (&temp, codeset,
						  common_coll);
		  error_status =
		    db_char_string_coerce (string2, &temp, &data_status);

		  if (error_status != NO_ERROR)
		    {
		      pr_clear_value (&temp);
		      return error_status;
		    }

		  assert (data_status == DATA_STATUS_OK);

		  string2 = &temp;
		}
	    }

	  error_status =
	    qstr_concatenate ((unsigned char *) DB_PULL_STRING (string1),
			      (int) DB_GET_STRING_LENGTH (string1),
			      (int) QSTR_VALUE_PRECISION (string1),
			      DB_VALUE_DOMAIN_TYPE (string1),
			      (unsigned char *) DB_PULL_STRING (string2),
			      (int) DB_GET_STRING_LENGTH (string2),
			      (int) QSTR_VALUE_PRECISION (string2),
			      DB_VALUE_DOMAIN_TYPE (string2),
			      codeset,
			      &r, &r_length, &r_size, &r_type, data_status);

	  pr_clear_value (&temp);

	  if (error_status == NO_ERROR && r != NULL)
	    {
	      if ((DB_VALUE_PRECISION (string1) ==
		   TP_FLOATING_PRECISION_VALUE) ||
		  (DB_VALUE_PRECISION (string2) ==
		   TP_FLOATING_PRECISION_VALUE))
		{
		  result_domain_length = TP_FLOATING_PRECISION_VALUE;
		}
	      else
		{
		  result_domain_length = MIN (QSTR_MAX_PRECISION (r_type),
					      DB_VALUE_PRECISION (string1) +
					      DB_VALUE_PRECISION (string2));
		}

	      if (is_inplace_concat)
		{
		  /* clear value before in-place update */
		  (void) pr_clear_value (result);
		}

	      qstr_make_typed_string (r_type,
				      result,
				      result_domain_length,
				      (char *) r, r_size, codeset,
				      common_coll);
	      r[r_size] = 0;
	      result->need_clear = true;
	    }
	}
    }

  return error_status;
}

/*
 * db_string_chr () - take character of db_value
 *   return: NO_ERROR, or ER_code
 *   res(OUT)   : resultant db_value node
 *   dbval1(IN) : first db_value node
 *   dbval2(IN) : charset name to use
 */

int
db_string_chr (DB_VALUE * res, DB_VALUE * dbval1, DB_VALUE * dbval2)
{
  DB_TYPE arg_type;
  DB_BIGINT temp_bigint = 0;
  unsigned int temp_arg = 0, uint_arg = 0;
  int itmp = 0;

  DB_BIGINT bi = 0;

  double dtmp = 0;

  char *num_as_bytes = NULL;
  char *invalid_pos = NULL;
  int num_byte_count = 0;
  int i, codeset = -1, collation = -1;
  int err_status = NO_ERROR;

  arg_type = DB_VALUE_DOMAIN_TYPE (dbval1);

  assert (dbval1 != NULL && dbval2 != NULL);

  if (arg_type == DB_TYPE_NULL || DB_IS_NULL (dbval1))
    {
      goto exit;
    }

  assert (DB_VALUE_DOMAIN_TYPE (dbval2) == DB_TYPE_INTEGER);

  codeset = DB_GET_INTEGER (dbval2);
  if (codeset != INTL_CODESET_UTF8 && codeset != INTL_CODESET_ISO88591
      && codeset != INTL_CODESET_KSC5601_EUC)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      err_status = ER_OBJ_INVALID_ARGUMENTS;
      goto exit;
    }

  /* Get value according to DB_TYPE */
  switch (arg_type)
    {
    case DB_TYPE_SHORT:
      itmp = DB_GET_SHORT (dbval1);
      break;
    case DB_TYPE_INTEGER:
      itmp = DB_GET_INTEGER (dbval1);
      break;
    case DB_TYPE_BIGINT:
      bi = DB_GET_BIGINT (dbval1);
      break;
    case DB_TYPE_FLOAT:
      dtmp = DB_GET_FLOAT (dbval1);
      break;
    case DB_TYPE_DOUBLE:
      dtmp = DB_GET_DOUBLE (dbval1);
      break;
    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (db_locate_numeric (dbval1),
				    DB_VALUE_SCALE (dbval1), &dtmp);
      break;
    case DB_TYPE_MONETARY:
      dtmp = (DB_GET_MONETARY (dbval1))->amount;
      break;
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      err_status = ER_GENERIC_ERROR;
      goto exit;
    }				/* switch */

  /* bi, dtmp and itmp have the default value set to 0, so temp_bigint will
   * hold the numeric representation of the first argument, regardless of
   * its type. */
  temp_bigint = bi + (DB_BIGINT) round (fmod (dtmp, 0x100000000)) + itmp;

  if (temp_bigint >= 0)
    {
      temp_arg = DB_UINT32_MAX & temp_bigint;
    }
  else
    {
      temp_arg = DB_UINT32_MAX & (-temp_bigint);
      temp_arg = DB_UINT32_MAX - temp_arg + 1;
    }
  uint_arg = temp_arg;

  if (temp_arg == 0)
    {
      num_byte_count = 1;
    }
  else
    {
      while (temp_arg > 0)
	{
	  num_byte_count++;
	  temp_arg >>= 8;
	}
    }

  num_as_bytes = (char *)
    db_private_alloc (NULL, (1 + num_byte_count) * sizeof (char));
  if (num_as_bytes == NULL)
    {
      err_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }
  temp_arg = uint_arg;

  for (i = num_byte_count - 1; i >= 0; i--)
    {
      num_as_bytes[i] = (char) (temp_arg & 0xFF);
      temp_arg >>= 8;
    }
  num_as_bytes[num_byte_count] = '\0';

  if ((codeset == INTL_CODESET_UTF8 &&
       intl_check_utf8 ((const unsigned char *) num_as_bytes,
			num_byte_count, &invalid_pos) != 0)
      || (codeset == INTL_CODESET_KSC5601_EUC &&
	  intl_check_euckr ((const unsigned char *) num_as_bytes,
			    num_byte_count, &invalid_pos) != 0))
    {
      DB_MAKE_NULL (res);
      db_private_free (NULL, num_as_bytes);

      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  err_status = NO_ERROR;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  err_status = ER_OBJ_INVALID_ARGUMENTS;
	}
      goto exit;
    }

  collation = LANG_GET_BINARY_COLLATION (codeset);

  db_make_varchar (res, DB_DEFAULT_PRECISION, num_as_bytes, num_byte_count,
		   codeset, collation);
  res->need_clear = true;

exit:
  return err_status;
}

/*
 * db_string_instr () -
 *
 * Arguments:
 *      sub_string: String fragment to search for within <src_string>.
 *      src_string: String to be searched.
 *          result: Character or bit position of the first <sub_string>
 *                  occurance.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <sub_string> or <src_string> are not a character strings.
 *      ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *         <sub_string> and <src_string> have different character
 *         code sets, or are not both bit strings.
 *
 */

int
db_string_instr (const DB_VALUE * src_string,
		 const DB_VALUE * sub_string,
		 const DB_VALUE * start_pos, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  DB_TYPE str1_type, str2_type;
  DB_TYPE arg3_type;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_string != (DB_VALUE *) NULL);
  assert (sub_string != (DB_VALUE *) NULL);
  assert (start_pos != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  /*
   *  Categorize the parameters into respective code sets.
   *  Verify that the parameters are both character strings.
   *  Verify that the input strings belong to compatible code sets.
   */
  str1_type = DB_VALUE_DOMAIN_TYPE (src_string);
  str2_type = DB_VALUE_DOMAIN_TYPE (sub_string);
  arg3_type = DB_VALUE_DOMAIN_TYPE (start_pos);

  if (DB_IS_NULL (src_string) || DB_IS_NULL (sub_string) ||
      DB_IS_NULL (start_pos))
    {
      DB_MAKE_NULL (result);
    }
  else
    {
      if (!(str1_type == DB_TYPE_STRING || str1_type == DB_TYPE_CHAR
	    || str1_type == DB_TYPE_VARCHAR || str1_type == DB_TYPE_NCHAR
	    || str1_type == DB_TYPE_VARNCHAR)
	  || !(str2_type == DB_TYPE_STRING || str2_type == DB_TYPE_CHAR
	       || str2_type == DB_TYPE_VARCHAR || str2_type == DB_TYPE_NCHAR
	       || str2_type == DB_TYPE_VARNCHAR)
	  || !(arg3_type == DB_TYPE_INTEGER || arg3_type == DB_TYPE_SHORT
	       || arg3_type == DB_TYPE_BIGINT))
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      else if (qstr_get_category (src_string) !=
	       qstr_get_category (sub_string)
	       || (DB_GET_STRING_CODESET (src_string)
		   != DB_GET_STRING_CODESET (sub_string)))
	{
	  error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      else
	{
	  int position = 0;
	  int src_str_len;
	  int sub_str_len;
	  int offset = DB_GET_INT (start_pos);
	  INTL_CODESET codeset =
	    (INTL_CODESET) DB_GET_STRING_CODESET (src_string);
	  char *search_from, *src_buf, *sub_str;
	  int coll_id;
	  int sub_str_size = DB_GET_STRING_SIZE (sub_string);
	  int from_byte_offset;
	  int src_size = DB_GET_STRING_SIZE (src_string);

	  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (src_string),
			       DB_GET_STRING_COLLATION (sub_string), coll_id);
	  if (coll_id == -1)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
	      return ER_QSTR_INCOMPATIBLE_COLLATIONS;
	    }

	  src_str_len = DB_GET_STRING_LENGTH (src_string);
	  sub_str_len = DB_GET_STRING_LENGTH (sub_string);

	  src_buf = DB_PULL_STRING (src_string);
	  if (src_size < 0)
	    {
	      src_size = strlen (src_buf);
	    }

	  sub_str = DB_PULL_STRING (sub_string);
	  if (sub_str_size < 0)
	    {
	      sub_str_size = strlen (sub_str);
	    }

	  if (offset > 0)
	    {
	      offset--;
	      if (offset + sub_str_len > src_str_len)
		{		/* out of bound */
		  position = 0;
		}
	      else
		{
		  search_from = src_buf;

		  intl_char_size ((unsigned char *) search_from, offset,
				  codeset, &from_byte_offset);
		  search_from += from_byte_offset;

		  intl_char_count ((unsigned char *) search_from,
				   src_size - from_byte_offset, codeset,
				   &src_str_len);

		  /* forward search */
		  error_status =
		    qstr_position (sub_str, sub_str_size, sub_str_len,
				   search_from, src_buf + src_size,
				   src_buf + src_size, src_str_len,
				   coll_id, true, &position);
		  position += (position != 0) ? offset : 0;
		}
	    }
	  else if (offset < 0)
	    {
	      if (src_str_len + offset + 1 < sub_str_len)
		{
		  position = 0;
		}
	      else
		{
		  int real_offset = src_str_len + offset - (sub_str_len - 1);

		  search_from = src_buf;

		  intl_char_size ((unsigned char *) search_from, real_offset,
				  codeset, &from_byte_offset);

		  search_from += from_byte_offset;

		  /* backward search */
		  error_status =
		    qstr_position (sub_str, sub_str_size, sub_str_len,
				   search_from, src_buf + src_size, src_buf,
				   src_str_len + offset + 1, coll_id,
				   false, &position);
		  if (position != 0)
		    {
		      position = src_str_len - (-offset - 1)
			- (position - 1) - (sub_str_len - 1);
		    }
		}
	    }
	  else
	    {
	      /* offset == 0 */
	      position = 0;
	    }

	  if (error_status == NO_ERROR)
	    {
	      DB_MAKE_INTEGER (result, position);
	    }
	}
    }

  return error_status;
}

/*
 * db_string_space () -
 *  returns a VARCHAR string consisting of a number of space characters equals
 *  to the given argument
 *
 * Arguments:
 *	count: number of space characters in the returned string
 *
 * Returns: int
 *
 * Errors:
 *     ER_QSTR_INVALID_DATA_TYPE: count is not a discrete numeric type (integer)
 *			    ....  ...
 */

int
db_string_space (DB_VALUE const *count, DB_VALUE * result)
{
  assert (count != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (count))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  else
    {
      int len = 0;
      char *space_string_p = NULL;

      switch (DB_VALUE_DOMAIN_TYPE (count))
	{
	case DB_TYPE_SMALLINT:
	  len = DB_GET_SMALLINT (count);
	  break;
	case DB_TYPE_INTEGER:
	  len = DB_GET_INTEGER (count);
	  break;
	case DB_TYPE_BIGINT:
	  len = (int) DB_GET_BIGINT (count);
	  break;
	default:
	  return ER_QSTR_INVALID_DATA_TYPE;
	}

      if (len < 0)
	{
	  len = 0;
	}

      if (len > (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES))
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_STRING_SIZE_TOO_BIG, 2, len,
		  (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES));
	  DB_MAKE_NULL (result);
	  return NO_ERROR;
	}

      space_string_p = db_private_alloc (NULL, len + 1);

      if (space_string_p)
	{
	  if (len > 64)
	    {
	      /* if string is longer than 64 chars use memset to
	       * initialize it */
	      memset (space_string_p, ' ', len);
	    }
	  else
	    {
	      int i = 0;

	      while (i < len)
		space_string_p[i++] = ' ';
	    }
	  space_string_p[len] = '\0';

	  qstr_make_typed_string (DB_TYPE_VARCHAR, result, len,
				  space_string_p, len, LANG_COERCIBLE_CODESET,
				  LANG_COERCIBLE_COLL);
	  result->need_clear = true;
	  return NO_ERROR;
	}
      else
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }
}

/*
 * db_string_position () -
 *
 * Arguments:
 *      sub_string: String fragment to search for within <src_string>.
 *      src_string: String to be searched.
 *          result: Character or bit position of the first <sub_string>
 *                  occurance.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <sub_string> or <src_string> are not a character strings.
 *      ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *         <sub_string> and <src_string> have different character
 *         code sets, or are not both bit strings.
 *
 */

int
db_string_position (const DB_VALUE * sub_string,
		    const DB_VALUE * src_string, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  DB_TYPE str1_type, str2_type;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (sub_string != (DB_VALUE *) NULL);
  assert (src_string != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);


  /*
   *  Categorize the parameters into respective code sets.
   *  Verify that the parameters are both character strings.
   *  Verify that the input strings belong to compatible code sets.
   */
  str1_type = DB_VALUE_DOMAIN_TYPE (sub_string);
  str2_type = DB_VALUE_DOMAIN_TYPE (src_string);

  if (DB_IS_NULL (sub_string) || DB_IS_NULL (src_string))
    {
      DB_MAKE_NULL (result);
    }
  else if (!QSTR_IS_ANY_CHAR_OR_BIT (str1_type)
	   || !QSTR_IS_ANY_CHAR_OR_BIT (str2_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if ((qstr_get_category (sub_string) != qstr_get_category (src_string))
	   || (DB_GET_STRING_CODESET (src_string)
	       != DB_GET_STRING_CODESET (sub_string)))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else
    {
      int position;
      DB_TYPE src_type = DB_VALUE_DOMAIN_TYPE (src_string);

      if (QSTR_IS_CHAR (src_type) || QSTR_IS_NATIONAL_CHAR (src_type))
	{
	  char *src_str = DB_PULL_STRING (src_string);
	  int src_size = DB_GET_STRING_SIZE (src_string);
	  char *sub_str = DB_PULL_STRING (sub_string);
	  int sub_size = DB_GET_STRING_SIZE (sub_string);
	  int coll_id;

	  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (src_string),
			       DB_GET_STRING_COLLATION (sub_string), coll_id);
	  if (coll_id == -1)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
	      return ER_QSTR_INCOMPATIBLE_COLLATIONS;
	    }

	  if (src_size < 0)
	    {
	      src_size = strlen (src_str);
	    }

	  if (sub_size < 0)
	    {
	      sub_size = strlen (sub_str);
	    }

	  error_status =
	    qstr_position (sub_str, sub_size,
			   DB_GET_STRING_LENGTH (sub_string),
			   src_str, src_str + src_size, src_str + src_size,
			   DB_GET_STRING_LENGTH (src_string),
			   coll_id, true, &position);
	}
      else
	{
	  error_status =
	    qstr_bit_position ((unsigned char *) DB_PULL_STRING (sub_string),
			       DB_GET_STRING_LENGTH (sub_string),
			       (unsigned char *) DB_PULL_STRING (src_string),
			       DB_GET_STRING_LENGTH (src_string), &position);
	}

      if (error_status == NO_ERROR)
	{
	  DB_MAKE_INTEGER (result, position);
	}
    }

  return error_status;
}

/*
 * db_string_substring
 *
 * Arguments:
 *             src_string: String from which extraction will occur.
 *              start_pos: Character position to begin extraction from.
 *      extraction_length: Number of characters to extract (Optional).
 *             sub_string: Extracted subtring is returned here.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <src_string> is not a string type,
 *         <start_pos> or <extraction_length>  is not an integer type
 *      ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *         <src_string> have different character
 *         code sets or are not both bit strings.
 *
 */

int
db_string_substring (const MISC_OPERAND substr_operand,
		     const DB_VALUE * src_string,
		     const DB_VALUE * start_position,
		     const DB_VALUE * extraction_length,
		     DB_VALUE * sub_string)
{
  int error_status = NO_ERROR;
  int extraction_length_is_null = false;
  DB_TYPE result_type;
  DB_TYPE src_type;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_string != (DB_VALUE *) NULL);
  assert (start_position != (DB_VALUE *) NULL);
  assert (sub_string != (DB_VALUE *) NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);

  if ((extraction_length == NULL) || DB_IS_NULL (extraction_length))
    {
      extraction_length_is_null = true;
    }

  if (QSTR_IS_CHAR (src_type))
    {
      result_type = DB_TYPE_VARCHAR;
    }
  else if (QSTR_IS_NATIONAL_CHAR (src_type))
    {
      result_type = DB_TYPE_VARNCHAR;
    }
  else
    {
      result_type = DB_TYPE_VARBIT;
    }

  if (DB_IS_NULL (src_string) || DB_IS_NULL (start_position))
    {
#if defined(SERVER_MODE)
      assert (DB_IS_NULL (sub_string));
#endif
      DB_MAKE_NULL (sub_string);
    }
  else
    {
      if (!QSTR_IS_ANY_CHAR_OR_BIT (src_type)
	  || !is_integer (start_position)
	  || (!extraction_length_is_null && !is_integer (extraction_length)))
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      else
	{
	  unsigned char *sub;
	  int sub_length;

	  int extract_nchars = -1;

	  if (!extraction_length_is_null)
	    {
	      extract_nchars = (int) DB_GET_INTEGER (extraction_length);
	    }

	  /* Initialize the memory manager of the substring */
	  if (QSTR_IS_CHAR (src_type) || QSTR_IS_NATIONAL_CHAR (src_type))
	    {
	      int sub_size = 0;

	      unsigned char *string =
		(unsigned char *) DB_PULL_STRING (src_string);
	      int start_offset = DB_GET_INTEGER (start_position);
	      int string_len = DB_GET_STRING_LENGTH (src_string);

	      if (extraction_length_is_null)
		{
		  extract_nchars = string_len;
		}

	      if (substr_operand == SUBSTR)
		{
		  if (extract_nchars < 0 || string_len < ABS (start_offset))
		    {
		      return error_status;
		    }

		  if (start_offset < 0)
		    {
		      int byte_pos;
		      (void) intl_char_size (string,
					     string_len + start_offset,
					     DB_GET_STRING_CODESET
					     (src_string), &byte_pos);
		      string += byte_pos;
		      string_len = -start_offset;
		    }
		}

	      error_status =
		qstr_substring (string, string_len, start_offset,
				extract_nchars,
				DB_GET_STRING_CODESET (src_string), &sub,
				&sub_length, &sub_size);
	      if (error_status == NO_ERROR && sub != NULL)
		{
		  qstr_make_typed_string (result_type, sub_string,
					  DB_VALUE_PRECISION (src_string),
					  (char *) sub, sub_size,
					  DB_GET_STRING_CODESET (src_string),
					  DB_GET_STRING_COLLATION
					  (src_string));
		  sub[sub_size] = 0;
		  sub_string->need_clear = true;
		}
	    }
	  else
	    {
	      error_status =
		qstr_bit_substring ((unsigned char *)
				    DB_PULL_STRING (src_string),
				    (int) DB_GET_STRING_LENGTH (src_string),
				    (int) DB_GET_INTEGER (start_position),
				    extract_nchars, &sub, &sub_length);
	      if (error_status == NO_ERROR)
		{
		  qstr_make_typed_string (result_type,
					  sub_string,
					  DB_VALUE_PRECISION (src_string),
					  (char *) sub, sub_length,
					  DB_GET_STRING_CODESET (src_string),
					  DB_GET_STRING_COLLATION
					  (src_string));
		  sub_string->need_clear = true;
		}
	    }
	}
    }

  return error_status;
}

/*
 * db_string_repeat
 *
 * Arguments:
 *             src_string: String which repeats itself.
 *		    count: Number of repetions.
 *		   result: string containing the repeated original.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <src_string> is not a string type,
 *         <start_pos> or <extraction_length>  is not an integer type
 *
 */

int
db_string_repeat (const DB_VALUE * src_string,
		  const DB_VALUE * count, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  int src_length, count_i = 0, src_size = 0;
  DB_TYPE result_type = DB_TYPE_NULL;
  DB_TYPE src_type;
  INTL_CODESET codeset;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_string != (DB_VALUE *) NULL);
  assert (count != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  assert (!DB_IS_NULL (src_string) && !DB_IS_NULL (count));

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  src_length = (int) DB_GET_STRING_LENGTH (src_string);
  count_i = DB_GET_INTEGER (count);
  codeset = (INTL_CODESET) DB_GET_STRING_CODESET (src_string);

  if (QSTR_IS_CHAR (src_type))
    {
      result_type = DB_TYPE_VARCHAR;
    }
  else if (QSTR_IS_NATIONAL_CHAR (src_type))
    {
      result_type = DB_TYPE_VARNCHAR;
    }

  src_size = DB_GET_STRING_SIZE (src_string);
  if (src_size < 0)
    {
      intl_char_size ((unsigned char *) DB_PULL_STRING (result), src_length,
		      codeset, &src_size);
    }

  if (!QSTR_IS_ANY_CHAR (src_type) || !is_integer (count))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if (count_i <= 0 || src_length <= 0)
    {
      error_status =
	db_string_make_empty_typed_string (NULL, result, result_type,
					   src_length,
					   DB_GET_STRING_CODESET (src_string),
					   DB_GET_STRING_COLLATION
					   (src_string));
      if (error_status != NO_ERROR)
	{
	  return error_status;
	}
    }
  else
    {
      DB_VALUE dummy;
      unsigned char *res_ptr, *src_ptr;
      int expected_size;

      /* init dummy */
      DB_MAKE_NULL (&dummy);
      /* create an empy string for result */

      error_status =
	db_string_make_empty_typed_string (NULL, &dummy, result_type,
					   src_length * count_i,
					   DB_GET_STRING_CODESET (src_string),
					   DB_GET_STRING_COLLATION
					   (src_string));
      if (error_status != NO_ERROR)
	{
	  return error_status;
	}

      if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true
	  && DB_IS_NULL (&dummy)
	  && QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (&dummy)))
	{
	  /*intermediate value : clear is_null flag */
	  dummy.domain.general_info.is_null = 0;
	}

      expected_size = src_size * count_i;
      error_status = qstr_grow_string (&dummy, result, expected_size);
      if (error_status < 0)
	{
	  pr_clear_value (&dummy);
	  return error_status;
	}
      /* qstr_grow_string may return DB_NULL if size too big */
      if (DB_IS_NULL (result))
	{
	  pr_clear_value (&dummy);
	  return NO_ERROR;
	}

      pr_clear_value (&dummy);

      res_ptr = (unsigned char *) DB_PULL_STRING (result);
      src_ptr = (unsigned char *) DB_PULL_STRING (src_string);

      while (count_i--)
	{
	  memcpy (res_ptr, src_ptr, src_size);
	  res_ptr += src_size;
	}

      /* update size of string */
      qstr_make_typed_string (result_type,
			      result,
			      DB_VALUE_PRECISION (result),
			      DB_PULL_STRING (result),
			      (const int) expected_size,
			      DB_GET_STRING_CODESET (src_string),
			      DB_GET_STRING_COLLATION (src_string));
      result->need_clear = true;

    }

  return error_status;
}

/*
 * db_string_substring_index - returns the substring from a string before
 *			       count occurences of delimeter
 *
 * Arguments:
 *             src_string: String to search in.
 *	     delim_string: String delimiter
 *		    count: Number of occurences.
 *		   result: string containing reminder.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <str_string> or <delim_string> is not a string type,
 *         <count> is not an integer type
 *	ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *	   <str_string> or <delim_string> are not compatible
 *
 */

int
db_string_substring_index (DB_VALUE * src_string,
			   DB_VALUE * delim_string,
			   const DB_VALUE * count, DB_VALUE * result)
{
  QSTR_CATEGORY src_categ, delim_categ;
  int error_status = NO_ERROR, count_i = 0;
  DB_TYPE src_type, delim_type;
  unsigned char *buf = NULL;
  DB_VALUE empty_string1, empty_string2;
  INTL_CODESET src_cs, delim_cs;
  int src_coll, delim_coll;

  /*
   *  Initialize status value
   */
  DB_MAKE_NULL (result);
  DB_MAKE_NULL (&empty_string1);
  DB_MAKE_NULL (&empty_string2);

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_string != (DB_VALUE *) NULL);
  assert (delim_string != (DB_VALUE *) NULL);
  assert (count != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (count))
    {
      DB_MAKE_NULL (result);

      return NO_ERROR;
    }
  count_i = DB_GET_INT (count);

  /*
   *  Categorize the parameters into respective code sets.
   *  Verify that the parameters are both character strings.
   *  Verify that the input strings belong to compatible code sets.
   */
  src_categ = qstr_get_category (src_string);
  delim_categ = qstr_get_category (delim_string);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  delim_type = DB_VALUE_DOMAIN_TYPE (delim_string);

  src_cs = DB_IS_NULL (src_string) ? LANG_SYS_CODESET :
    DB_GET_STRING_CODESET (src_string);
  src_coll = DB_IS_NULL (src_string) ? LANG_SYS_COLLATION :
    DB_GET_STRING_COLLATION (src_string);

  delim_cs = DB_IS_NULL (delim_string) ? LANG_SYS_CODESET :
    DB_GET_STRING_CODESET (delim_string);
  delim_coll = DB_IS_NULL (delim_string) ? LANG_SYS_COLLATION :
    DB_GET_STRING_COLLATION (delim_string);

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true)
    {
      if (DB_IS_NULL (src_string))
	{
	  if (DB_IS_NULL (delim_string))
	    {
	      /* both strings are NULL (or empty): result is DB_TYPE_NULL */
	      assert (error_status == NO_ERROR);
	      goto empty_string;
	    }
	  /* convert to empty string */
	  src_string = &empty_string1;

	  src_type = delim_type;
	  src_categ = delim_categ;

	  error_status =
	    db_string_make_empty_typed_string (NULL, src_string, src_type,
					       TP_FLOATING_PRECISION_VALUE,
					       delim_cs, delim_coll);

	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  /*intermediate value : clear is_null flag */
	  if (QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (src_string)))
	    {
	      src_string->domain.general_info.is_null = 0;
	    }
	}

      if (DB_IS_NULL (delim_string))
	{
	  /* convert to empty string */
	  delim_string = &empty_string2;

	  delim_type = src_type;
	  delim_categ = src_categ;

	  error_status =
	    db_string_make_empty_typed_string (NULL, delim_string, delim_type,
					       TP_FLOATING_PRECISION_VALUE,
					       src_cs, src_coll);

	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  /*intermediate value : clear is_null flag */
	  if (QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (delim_string)))
	    {
	      delim_string->domain.general_info.is_null = 0;
	    }
	}
    }
  else if (DB_IS_NULL (src_string) || DB_IS_NULL (delim_string))
    {
      goto exit;
    }

  if (!QSTR_IS_ANY_CHAR (src_type) || !QSTR_IS_ANY_CHAR (delim_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if ((src_categ != delim_categ) || (src_cs != delim_cs))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if (count_i == 0)
    {
      /* return an empty string */
      goto empty_string;
    }
  else
    {
      DB_VALUE offset_val, interm_pos;
      int offset = 1, initial_count = 0;
      bool count_from_start;
      const int src_length = DB_GET_STRING_LENGTH (src_string);
      const int delim_length = DB_GET_STRING_LENGTH (delim_string);

      DB_MAKE_NULL (&interm_pos);
      initial_count = count_i;
      count_from_start = (count_i > 0) ? true : false;
      count_i = abs (count_i);

      assert (src_cs == delim_cs);

      if (count_from_start)
	{
	  while (count_i > 0)
	    {
	      DB_MAKE_INTEGER (&offset_val, offset);
	      error_status =
		db_string_instr (src_string, delim_string, &offset_val,
				 &interm_pos);
	      if (error_status < 0)
		{
		  goto exit;
		}
	      offset = DB_GET_INT (&interm_pos);
	      if (offset != 0)
		{
		  offset += delim_length;
		  DB_MAKE_INTEGER (&offset_val, offset);
		}
	      else
		{
		  break;
		}
	      count_i--;
	    }

	}
      else
	{
	  while (count_i > 0)
	    {
	      /* search from end */
	      DB_MAKE_INTEGER (&offset_val, -offset);
	      error_status =
		db_string_instr (src_string, delim_string, &offset_val,
				 &interm_pos);
	      if (error_status < 0)
		{
		  goto exit;
		}
	      offset = DB_GET_INT (&interm_pos);
	      if (offset != 0)
		{
		  /* adjust offset to indicate position relative to end */
		  offset = src_length - offset + 2;
		  DB_MAKE_INTEGER (&offset_val, offset);
		}
	      else
		{
		  break;
		}
	      count_i--;
	    }
	}

      assert (count_i >= 0);

      if (count_i == 0)
	{
	  /* found count occurences , return the string */
	  DB_VALUE start_val, len_val;
	  int start_pos = 1, end_pos = 0;

	  if (count_from_start)
	    {
	      start_pos = 1;
	      end_pos = offset - delim_length - 1;
	    }
	  else
	    {
	      start_pos = src_length - offset + 2 + delim_length;
	      end_pos = src_length;
	    }

	  if (start_pos > end_pos || start_pos < 1 || end_pos > src_length)
	    {
	      /* empty string */
	      goto empty_string;
	    }
	  else
	    {
	      DB_MAKE_INTEGER (&start_val, start_pos);
	      DB_MAKE_INTEGER (&len_val, end_pos - start_pos + 1);

	      error_status = db_string_substring (SUBSTRING, src_string,
						  &start_val, &len_val,
						  result);

	      result->need_clear = true;
	      if (error_status < 0)
		{
		  goto exit;
		}
	    }
	}
      else
	{
	  assert (count_i > 0);
	  /* not found at all or not enough number of occurences */
	  /* return the entire source string */

	  error_status = pr_clone_value ((DB_VALUE *) src_string, result);
	  if (src_type == DB_TYPE_CHAR || src_type == DB_TYPE_NCHAR)
	    {
	      /* convert CHARACTER(N) to CHARACTER VARYING(N) */
	      qstr_make_typed_string ((src_type == DB_TYPE_NCHAR
				       ? DB_TYPE_VARNCHAR : DB_TYPE_VARCHAR),
				      result, DB_VALUE_PRECISION (result),
				      DB_PULL_STRING (result),
				      DB_GET_STRING_SIZE (result),
				      src_cs, src_coll);
	      result->need_clear = true;
	    }

	  if (error_status < 0)
	    {
	      goto exit;
	    }
	}
    }

  pr_clear_value (&empty_string1);
  pr_clear_value (&empty_string2);

  return error_status;

empty_string:
  /* the result should always be varying type string */
  if (src_type == DB_TYPE_CHAR)
    {
      src_type = DB_TYPE_VARCHAR;
    }
  else if (src_type == DB_TYPE_NCHAR)
    {
      src_type = DB_TYPE_VARNCHAR;
    }
  error_status =
    db_string_make_empty_typed_string (NULL, result, src_type,
				       TP_FLOATING_PRECISION_VALUE,
				       src_cs, src_coll);
  pr_clear_value (&empty_string1);
  pr_clear_value (&empty_string2);

  return error_status;

exit:
  pr_clear_value (&empty_string1);
  pr_clear_value (&empty_string2);

  return error_status;
}

/*
 * db_string_shaone - sha1 encrypt function
 *   return: If success, return 0.
 *   src(in): source string
 *	 result(out): the encrypted data.
 * Note:
 */
int
db_string_sha_one (DB_VALUE const *src, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  char *result_strp = NULL;
  int result_len = 0;

  assert (src != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src))
    {
      DB_MAKE_NULL (result);	/* SH1(NULL) returns NULL */
      return error_status;
    }
  else
    {
      DB_TYPE val_type = DB_VALUE_DOMAIN_TYPE (src);

      if (QSTR_IS_ANY_CHAR (val_type))
	{
	  error_status =
	    crypt_sha_one (NULL, DB_PULL_STRING (src),
			   DB_GET_STRING_SIZE (src), &result_strp,
			   &result_len);
	  if (error_status != NO_ERROR)
	    {
	      goto error;
	    }

	  qstr_make_typed_string (DB_TYPE_CHAR, result, result_len,
				  result_strp, result_len,
				  DB_GET_STRING_CODESET (src),
				  DB_GET_STRING_COLLATION (src));
	  result->need_clear = true;
	}
      else
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  goto error;
	}
    }

  return error_status;

error:
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }
  else
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      return error_status;
    }
}

/*
 * db_string_shatwo - sha2 encrypt function
 *   return: If success, return 0.
 *   src(in): source string
 *	 hash_len(in): the hash length
 *	 result(out): the encrypted data.
 * Note:
 */
int
db_string_sha_two (DB_VALUE const *src, DB_VALUE const *hash_len,
		   DB_VALUE * result)
{
  int error_status = NO_ERROR;

  DB_TYPE src_type = DB_VALUE_DOMAIN_TYPE (src);
  DB_TYPE hash_len_type = DB_VALUE_DOMAIN_TYPE (hash_len);
  char *result_strp = NULL;
  int result_len = 0;
  int len = 0;

  assert (src != (DB_VALUE *) NULL);
  assert (hash_len != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src) || DB_IS_NULL (hash_len))
    {
      DB_MAKE_NULL (result);	/* sha2(NULL, ...) or sha2(..., NULL) returns NULL */
      return error_status;
    }

  switch (hash_len_type)
    {
    case DB_TYPE_SHORT:
      len = DB_GET_SHORT (hash_len);
      break;
    case DB_TYPE_INTEGER:
      len = DB_GET_INT (hash_len);
      break;
    case DB_TYPE_BIGINT:
      len = DB_GET_BIGINT (hash_len);
      break;
    default:
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  if (QSTR_IS_ANY_CHAR (src_type))
    {
      error_status =
	crypt_sha_two (NULL, DB_PULL_STRING (src), DB_GET_STRING_LENGTH (src),
		       len, &result_strp, &result_len);
      if (error_status != NO_ERROR)
	{
	  goto error;
	}

      /* It means that the hash_len is wrong. */
      if (result_strp == NULL)
	{
	  DB_MAKE_NULL (result);
	  return error_status;
	}

      qstr_make_typed_string (DB_TYPE_VARCHAR, result, result_len,
			      result_strp, result_len,
			      DB_GET_STRING_CODESET (src),
			      DB_GET_STRING_COLLATION (src));
      result->need_clear = true;
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  return error_status;

error:
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }
  else
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      return error_status;
    }
}

/*
 * db_string_aes_encrypt - aes encrypt function
 *   return: If success, return 0.
 *   src(in): source string
 *	 key(in): the encrypt key
 *	 result(out): the encrypted data.
 * Note:
 */
int
db_string_aes_encrypt (DB_VALUE const *src, DB_VALUE const *key,
		       DB_VALUE * result)
{
  int error_status = NO_ERROR;

  DB_TYPE src_type = DB_VALUE_DOMAIN_TYPE (src);
  DB_TYPE key_type = DB_VALUE_DOMAIN_TYPE (key);
  char *result_strp = NULL;
  int result_len = 0;

  assert (src != (DB_VALUE *) NULL);
  assert (key != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src) || DB_IS_NULL (key))
    {
      /* aes_encypt(NULL, ...) or aes_encypt(..., NULL) returns NULL */
      DB_MAKE_NULL (result);
      return error_status;
    }

  if (QSTR_IS_ANY_CHAR (src_type) && QSTR_IS_ANY_CHAR (key_type))
    {
      error_status =
	crypt_aes_default_encrypt (NULL, DB_PULL_STRING (src),
				   DB_GET_STRING_LENGTH (src),
				   DB_PULL_STRING (key),
				   DB_GET_STRING_LENGTH (key), &result_strp,
				   &result_len);
      if (error_status != NO_ERROR)
	{
	  goto error;
	}

      qstr_make_typed_string (DB_TYPE_VARCHAR, result, result_len,
			      result_strp, result_len,
			      DB_GET_STRING_CODESET (src),
			      DB_GET_STRING_COLLATION (src));
      result->need_clear = true;
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  return error_status;

error:
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }
  else
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      return error_status;
    }
}

/*
 * db_string_aes_decrypt - aes decrypt function
 *   return: If success, return 0.
 *   src(in): source string
 *	 key(in): the encrypt key
 *	 result(out): the decrypted data.
 * Note:
 */
int
db_string_aes_decrypt (DB_VALUE const *src, DB_VALUE const *key,
		       DB_VALUE * result)
{
  int error_status = NO_ERROR;

  DB_TYPE src_type = DB_VALUE_DOMAIN_TYPE (src);
  DB_TYPE key_type = DB_VALUE_DOMAIN_TYPE (key);
  char *result_strp = NULL;
  int result_len = 0;

  assert (src != (DB_VALUE *) NULL);
  assert (key != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src) || DB_IS_NULL (key))
    {
      /* aes_decypt(NULL, ...) or aes_decypt(..., NULL) returns NULL */
      DB_MAKE_NULL (result);
      return error_status;
    }

  if (QSTR_IS_ANY_CHAR (src_type) && QSTR_IS_ANY_CHAR (key_type))
    {
      error_status =
	crypt_aes_default_decrypt (NULL, DB_PULL_STRING (src),
				   DB_GET_STRING_LENGTH (src),
				   DB_PULL_STRING (key),
				   DB_GET_STRING_LENGTH (key), &result_strp,
				   &result_len);
      if (error_status != NO_ERROR)
	{
	  goto error;
	}

      if (result_strp == NULL)
	{
	  /* it means the src isn't aes_encrypted string, we return NULL like mysql */
	  DB_MAKE_NULL (result);
	  return error_status;
	}

      qstr_make_typed_string (DB_TYPE_VARCHAR, result, result_len,
			      result_strp, result_len,
			      DB_GET_STRING_CODESET (src),
			      DB_GET_STRING_COLLATION (src));
      result->need_clear = true;
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  return error_status;

error:
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }
  else
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	}
      return error_status;
    }
}


/*
 * MD5('str')
 * Arguments
 *	val: string to compute the MD5 (message digest) for
 *	result: DB_VALUE to receive the computed MD5 from the val argument
 */
int
db_string_md5 (DB_VALUE const *val, DB_VALUE * result)
{
  int error_status = NO_ERROR;

  assert (val != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  if (DB_IS_NULL (val))
    {
      DB_MAKE_NULL (result);	/* MD5(NULL) returns NULL */
      return error_status;
    }
  else
    {
      DB_TYPE val_type = DB_VALUE_DOMAIN_TYPE (val);

      if (QSTR_IS_ANY_CHAR (val_type))
	{
	  /* MD5 hash string buffer */
	  char hashString[32 + 1] = { '\0' };

	  DB_VALUE hash_string;

	  DB_MAKE_NULL (&hash_string);

	  md5_buffer (DB_PULL_STRING (val), DB_GET_STRING_LENGTH (val),
		      hashString);

	  md5_hash_to_hex (hashString, hashString);

	  /* dump result as hex string */
	  qstr_make_typed_string (DB_TYPE_CHAR, &hash_string, 32,
				  hashString, 32, DB_GET_STRING_CODESET (val),
				  DB_GET_STRING_COLLATION (val));
	  hash_string.need_clear = false;
	  pr_clone_value (&hash_string, result);
	}
      else
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	}
    }

  return error_status;
}

/*
 * db_string_insert_substring - insert a substring into a string replacing
 *				"length" characters starting at "position"
 *
 * Arguments:
 *             src_string: string to insert into. Its value will not be
 *                         modified as the output is the "result" parameter
 *		 position: starting position
 *		   length: number of character to replace
 *	       sub_string: string to be inserted
 *		   result: string containing result.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <str_string> or <delim_string> is not a string type,
 *         <count> is not an integer type
 *	ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *	   <str_string> or <delim_string> are not compatible
 *
 */

int
db_string_insert_substring (DB_VALUE * src_string,
			    const DB_VALUE * position,
			    const DB_VALUE * length,
			    DB_VALUE * sub_string, DB_VALUE * result)
{
  QSTR_CATEGORY src_categ, substr_categ;
  int error_status = NO_ERROR, position_i = 0, length_i = 0;
  DB_TYPE src_type, substr_type;
  DB_VALUE string1, string2;
  int src_length = 0;
  int result_size = 0;
  DB_VALUE empty_string1, empty_string2;
  DB_VALUE partial_result;
  INTL_CODESET src_cs, substr_cs;
  int src_coll, substr_coll;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_string != (DB_VALUE *) NULL);
  assert (sub_string != (DB_VALUE *) NULL);
  assert (position != (DB_VALUE *) NULL);
  assert (length != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  /*
   *  Initialize values
   */
  DB_MAKE_NULL (result);
  DB_MAKE_NULL (&string1);
  DB_MAKE_NULL (&string2);
  DB_MAKE_NULL (&empty_string1);
  DB_MAKE_NULL (&empty_string2);
  DB_MAKE_NULL (&partial_result);

  /*
   *  Categorize the parameters into respective code sets.
   *  Verify that the parameters are both character strings.
   *  Verify that the input strings belong to compatible code sets.
   */
  src_categ = qstr_get_category (src_string);
  substr_categ = qstr_get_category (sub_string);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  substr_type = DB_VALUE_DOMAIN_TYPE (sub_string);

  src_cs = DB_IS_NULL (src_string) ? LANG_SYS_CODESET :
    DB_GET_STRING_CODESET (src_string);
  src_coll = DB_IS_NULL (src_string) ? LANG_SYS_COLLATION :
    DB_GET_STRING_COLLATION (src_string);

  substr_cs = DB_IS_NULL (sub_string) ? LANG_SYS_CODESET :
    DB_GET_STRING_CODESET (sub_string);
  substr_coll = DB_IS_NULL (sub_string) ? LANG_SYS_COLLATION :
    DB_GET_STRING_COLLATION (sub_string);

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true)
    {
      if (DB_IS_NULL (src_string))
	{
	  if (DB_IS_NULL (sub_string))
	    {
	      /* both strings are NULL (or empty): result is DB_TYPE_NULL */
	      assert (error_status == NO_ERROR);
	      goto exit;
	    }
	  /* convert to empty string */
	  src_string = &empty_string1;

	  src_type = substr_type;
	  src_categ = substr_categ;

	  error_status =
	    db_string_make_empty_typed_string (NULL, src_string, src_type,
					       TP_FLOATING_PRECISION_VALUE,
					       substr_cs, substr_coll);

	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  /*intermediate value : clear is_null flag */
	  if (QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (src_string)))
	    {
	      src_string->domain.general_info.is_null = 0;
	    }
	}

      if (DB_IS_NULL (sub_string))
	{
	  /* convert to empty string */
	  sub_string = &empty_string2;

	  substr_type = src_type;
	  substr_categ = src_categ;

	  error_status =
	    db_string_make_empty_typed_string (NULL, sub_string, substr_type,
					       TP_FLOATING_PRECISION_VALUE,
					       src_cs, src_coll);

	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  /*intermediate value : clear is_null flag */
	  if (QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (sub_string)))
	    {
	      sub_string->domain.general_info.is_null = 0;
	    }
	}
    }
  else if (DB_IS_NULL (src_string) || DB_IS_NULL (sub_string))
    {
      /* result is DB_TYPE_NULL */
      assert (error_status == NO_ERROR);
      goto exit;
    }

  if (DB_IS_NULL (position) || DB_IS_NULL (length))
    {
      /* result is DB_TYPE_NULL */
      assert (error_status == NO_ERROR);
      goto exit;
    }
  if (!QSTR_IS_ANY_CHAR_OR_BIT (src_type)
      || !QSTR_IS_ANY_CHAR_OR_BIT (substr_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }
  if (src_categ != substr_categ)
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  position_i = DB_GET_INT (position);
  length_i = DB_GET_INT (length);
  src_length = DB_GET_STRING_LENGTH (src_string);

  if (position_i <= 0 || position_i > src_length + 1)
    {
      /* return the source string */
      error_status = pr_clone_value ((DB_VALUE *) src_string, result);
      result_size = DB_GET_STRING_SIZE (src_string);
    }
  else
    {
      DB_DATA_STATUS data_status;

      /*  result = string1 + substring + string2 */

      /* string1 = left(string,position) */

      if (position_i > 1)
	{
	  DB_VALUE start_val, len_val;

	  DB_MAKE_INTEGER (&start_val, 1);
	  DB_MAKE_INTEGER (&len_val, position_i - 1);

	  error_status = db_string_substring (SUBSTRING, src_string,
					      &start_val, &len_val, &string1);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }
	}

      if (DB_IS_NULL (&string1))	/* make dummy for concat */
	{
	  error_status =
	    db_string_make_empty_typed_string (NULL, &string1, src_type,
					       TP_FLOATING_PRECISION_VALUE,
					       src_cs, src_coll);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true
	      && DB_IS_NULL (&string1)
	      && QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (&string1)))
	    {
	      /*intermediate value : clear is_null flag */
	      string1.domain.general_info.is_null = 0;
	    }
	}

      /* string2 = susbtring(string,position+len) */

      /* get string2 if the conditions are fullfilled :
       * 1. length_i >= 0 - compatibility with MySql
       *                  (if len is negative, no remainder is concatenated)
       * 2. (position_i + length_i) <= src_length
       *                  - check the start boundary for substring
       */
      if ((length_i >= 0) && ((position_i + length_i) <= src_length))
	{
	  DB_VALUE start_val, len_val;

	  DB_MAKE_INTEGER (&start_val, position_i + length_i);
	  DB_MAKE_INTEGER (&len_val,
			   src_length - (position_i + length_i) + 1);

	  error_status = db_string_substring (SUBSTRING, src_string,
					      &start_val, &len_val, &string2);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }
	}

      if (DB_IS_NULL (&string2))	/* make dummy for concat */
	{
	  error_status =
	    db_string_make_empty_typed_string (NULL, &string2, src_type,
					       TP_FLOATING_PRECISION_VALUE,
					       src_cs, src_coll);

	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true
	      && DB_IS_NULL (&string2)
	      && QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (&string2)))
	    {
	      /*intermediate value : clear is_null flag */
	      string2.domain.general_info.is_null = 0;
	    }
	}

      /* partial_result = concat(string1,substring) */
      error_status =
	db_string_concatenate (&string1, sub_string, &partial_result,
			       &data_status);
      if (error_status != NO_ERROR)
	{
	  goto exit;
	}
      if (data_status != DATA_STATUS_OK)
	{
	  /* This should never happen as the partial_result is a VAR[N]CHAR */
	  assert (false);
	  error_status = ER_FAILED;
	  goto exit;
	}

      /* result = concat(partial_result,string2) */
      error_status = db_string_concatenate (&partial_result, &string2, result,
					    &data_status);
      if (error_status != NO_ERROR)
	{
	  goto exit;
	}

      if (data_status != DATA_STATUS_OK)
	{
	  /* This should never happen as the result is a VAR[N]CHAR */
	  assert (false);
	  error_status = ER_FAILED;
	  goto exit;
	}

      result_size = DB_GET_STRING_SIZE (result);
    }

  /* force type to variable string */
  if (src_type == DB_TYPE_CHAR || src_type == DB_TYPE_NCHAR)
    {
      /* convert CHARACTER(N) to CHARACTER VARYING(N) */
      qstr_make_typed_string ((src_type == DB_TYPE_NCHAR
			       ? DB_TYPE_VARNCHAR : DB_TYPE_VARCHAR),
			      result, TP_FLOATING_PRECISION_VALUE,
			      DB_PULL_STRING (result), result_size,
			      src_cs, src_coll);
    }
  else if (src_type == DB_TYPE_BIT)
    {
      /* convert BIT to BIT VARYING */
      qstr_make_typed_string (DB_TYPE_VARBIT, result,
			      TP_FLOATING_PRECISION_VALUE,
			      DB_PULL_STRING (result), result_size,
			      src_cs, src_coll);
    }

  result->need_clear = true;

exit:
  pr_clear_value (&string1);
  pr_clear_value (&string2);
  pr_clear_value (&empty_string1);
  pr_clear_value (&empty_string2);
  pr_clear_value (&partial_result);

  return error_status;
}

/*
    ELT(index, arg1, arg2, arg3, ...)

    Clones into result the argument with the index given by the first
    argument.

    Returns: NO_ERROR or an error code
*/
int
db_string_elt (DB_VALUE * result, DB_VALUE * arg[], int const num_args)
{
  DB_TYPE index_type = DB_VALUE_DOMAIN_TYPE (arg[0]);
  DB_BIGINT index = 0;

  if (num_args <= 0)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }


  if (DB_IS_NULL (arg[0]))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  switch (index_type)
    {
    case DB_TYPE_BIGINT:
      index = DB_GET_BIGINT (arg[0]);
      break;
    case DB_TYPE_INTEGER:
      index = DB_GET_INTEGER (arg[0]);
      break;
    case DB_TYPE_SMALLINT:
      index = DB_GET_SMALLINT (arg[0]);
      break;
    default:
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  if (index > 0 && index < num_args)
    {
      pr_clone_value (arg[index], result);
    }
  else
    {
      DB_MAKE_NULL (result);
    }

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * db_string_byte_length
 *
 * Arguments:
 *          string: (IN)  Input string of which the byte count is desired.
 *      byte_count: (OUT) The number of bytes in string.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE:
 *          <string> is not a string type
 *
 * Note:
 *   This function returns the number of bytes in <string>.
 *
 *   If the NULL flag is set for <string>, then the NULL flag
 *   for the <byte_count> is set.
 *
 * Assert:
 *   1. string     != (DB_VALUE *) NULL
 *   2. byte_count != (DB_VALUE *) NULL
 *
 */

int
db_string_byte_length (const DB_VALUE * string, DB_VALUE * byte_count)
{
  int error_status = NO_ERROR;
  DB_TYPE str_type;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (string != (DB_VALUE *) NULL);
  assert (byte_count != (DB_VALUE *) NULL);

  /*
   *  Verify that the input string is a valid character
   *  string.  Bit strings are not allowed.
   *
   *  If the input string is a NULL, then set
   *  the output null flag.
   *
   *  Otherwise, calculte the byte size.
   */

  str_type = DB_VALUE_DOMAIN_TYPE (string);
  if (!QSTR_IS_ANY_CHAR_OR_BIT (str_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
    }
  else if (DB_IS_NULL (string))
    {
      db_value_domain_init (byte_count, DB_TYPE_INTEGER, DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE);
    }
  else
    {
      DB_MAKE_INTEGER (byte_count, DB_GET_STRING_SIZE (string));
    }

  return error_status;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * db_string_bit_length () -
 *
 * Arguments:
 *          string: Inpute string of which the bit length is desired.
 *       bit_count: Bit count of string.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE:
 *          <string> is not a string type
 *
 * Note:
 *   This function returns the number of bits in <string>.
 *
 *   If the NULL flag is set for <string>, then the NULL flag
 *   for the <bit_count> is set.
 *
 * Assert:
 *   1. string    != (DB_VALUE *) NULL
 *   2. bit_count != (DB_VALUE *) NULL
 *
 */

int
db_string_bit_length (const DB_VALUE * string, DB_VALUE * bit_count)
{
  int error_status = NO_ERROR;
  DB_TYPE str_type;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (string != (DB_VALUE *) NULL);
  assert (bit_count != (DB_VALUE *) NULL);

  /*
   *  Verify that the input string is a valid character string.
   *  Bit strings are not allowed.
   *
   *  If the input string is a NULL, then set the output null flag.
   *
   *  If the input parameter is valid, then extract the byte length
   *  of the string.
   */

  str_type = DB_VALUE_DOMAIN_TYPE (string);
  if (!QSTR_IS_ANY_CHAR_OR_BIT (str_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
    }
  else if (DB_IS_NULL (string))
    {
      db_value_domain_init (bit_count, DB_TYPE_INTEGER,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      if (qstr_get_category (string) == QSTR_BIT)
	{
	  DB_MAKE_INTEGER (bit_count, DB_GET_STRING_LENGTH (string));
	}
      else
	{
	  DB_MAKE_INTEGER (bit_count,
			   (DB_GET_STRING_SIZE (string) * BYTE_SIZE));
	}
    }

  return error_status;
}

/*
 * db_string_char_length () -
 *
 * Arguments:
 *          string: String for which the number of characters is desired.
 *      char_count: Number of characters in string.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE:
 *          <string> is not a character string
 *
 * Note:
 *   This function returns the number of characters in <string>.
 *
 *   If the NULL flag is set for <string>, then the NULL flag
 *   for the <char_count> is set.
 *
 * Assert:
 *   1. string     != (DB_VALUE *) NULL
 *   2. char_count != (DB_VALUE *) NULL
 *
 */

int
db_string_char_length (const DB_VALUE * string, DB_VALUE * char_count)
{
  int error_status = NO_ERROR;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (string != (DB_VALUE *) NULL);
  assert (char_count != (DB_VALUE *) NULL);


  /*
   *  Verify that the input string is a valid character
   *  string.  Bit strings are not allowed.
   *
   *  If the input string is a NULL, then set the output null flag.
   *
   *  If the input parameter is valid, then extract the character
   *  length of the string.
   */
  if (!is_char_string (string))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
    }
  else if (DB_IS_NULL (string))
    {
      db_value_domain_init (char_count, DB_TYPE_INTEGER,
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      DB_MAKE_INTEGER (char_count, DB_GET_STRING_LENGTH (string));
    }

  return error_status;
}

/*
 * db_string_lower () -
 *
 * Arguments:
 *            string: Input string that will be converted to lower case.
 *      lower_string: Output converted string.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <string> is not a character string.
 *
 * Note:
 *   This function returns a string with all uppercase ASCII
 *   and LATIN alphabetic characters converted to lowercase.
 *
 *   If the NULL flag is set for <string>, then the NULL flag
 *   for the <lower_string> is set.
 *
 *   The <lower_string> value structure will be cloned from <string>.
 *   <lower_string> should be cleared with pr_clone_value() if it has
 *   already been initialized or DB_MAKE_NULL if it has not been
 *   previously used by the system.
 *
 * Assert:
 *
 *   1. string       != (DB_VALUE *) NULL
 *   2. lower_string != (DB_VALUE *) NULL
 *
 */

int
db_string_lower (const DB_VALUE * string, DB_VALUE * lower_string)
{
  int error_status = NO_ERROR;
  DB_TYPE str_type;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (string != (DB_VALUE *) NULL);
  assert (lower_string != (DB_VALUE *) NULL);

  /*
   *  Categorize the two input parameters and check for errors.
   *    Verify that the parameters are both character strings.
   */

  str_type = DB_VALUE_DOMAIN_TYPE (string);
  if (DB_IS_NULL (string))
    {
      DB_MAKE_NULL (lower_string);
    }
  else if (!QSTR_IS_ANY_CHAR (str_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
    }
  /*
   *  If the input parameters have been properly validated, then
   *  we are ready to operate.
   */
  else
    {
      unsigned char *lower_str;
      int lower_size;
      int src_length;
      const ALPHABET_DATA *alphabet =
	lang_user_alphabet_w_coll (DB_GET_STRING_COLLATION (string));

      src_length = DB_GET_STRING_LENGTH (string);
      lower_size =
	intl_lower_string_size (alphabet,
				(unsigned char *) DB_PULL_STRING (string),
				DB_GET_STRING_SIZE (string), src_length);

      lower_str = (unsigned char *) db_private_alloc (NULL, lower_size + 1);
      if (!lower_str)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	}
      else
	{
	  int lower_length = TP_FLOATING_PRECISION_VALUE;
	  intl_lower_string (alphabet,
			     (unsigned char *) DB_PULL_STRING (string),
			     lower_str, src_length);
	  lower_str[lower_size] = 0;

	  if (db_value_precision (string) != TP_FLOATING_PRECISION_VALUE)
	    {
	      intl_char_count (lower_str, lower_size,
			       (INTL_CODESET) DB_GET_STRING_CODESET (string),
			       &lower_length);
	    }
	  qstr_make_typed_string (str_type, lower_string, lower_length,
				  (char *) lower_str, lower_size,
				  DB_GET_STRING_CODESET (string),
				  DB_GET_STRING_COLLATION (string));
	  lower_string->need_clear = true;
	}
    }

  return error_status;
}

/*
 * db_string_upper () -
 *
 * Arguments:
 *            string: Input string that will be converted to upper case.
 *      lower_string: Output converted string.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     :
 *         <string> is not a character string.
 *
 * Note:
 *
 *   This function returns a string with all lowercase ASCII
 *   and LATIN alphabetic characters converted to uppercase.
 *
 *   If the NULL flag is set for <string>, then the NULL flag
 *   for the <upper_string> is set.
 *
 *   The <upper_string> value structure will be cloned from <string>.
 *   <upper_string> should be cleared with pr_clone_value() if it has
 *   already been initialized or DB_MAKE_NULL if it has not been
 *   previously used by the system.
 *
 * Assert:
 *
 *   1. string       != (DB_VALUE *) NULL
 *   2. upper_string != (DB_VALUE *) NULL
 *
 */

int
db_string_upper (const DB_VALUE * string, DB_VALUE * upper_string)
{
  int error_status = NO_ERROR;
  DB_TYPE str_type;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (string != (DB_VALUE *) NULL);
  assert (upper_string != (DB_VALUE *) NULL);

  /*
   *  Categorize the two input parameters and check for errors.
   *    Verify that the parameters are both character strings.
   */

  str_type = DB_VALUE_DOMAIN_TYPE (string);
  if (DB_IS_NULL (string))
    {
      DB_MAKE_NULL (upper_string);
    }
  else if (!QSTR_IS_ANY_CHAR (str_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
    }
  /*
   *  If the input parameters have been properly validated, then
   *  we are ready to operate.
   */
  else
    {
      unsigned char *upper_str;
      int upper_size, src_length;
      const ALPHABET_DATA *alphabet =
	lang_user_alphabet_w_coll (DB_GET_STRING_COLLATION (string));

      src_length = DB_GET_STRING_LENGTH (string);
      upper_size =
	intl_upper_string_size (alphabet,
				(unsigned char *) DB_PULL_STRING (string),
				DB_GET_STRING_SIZE (string), src_length);

      upper_str = (unsigned char *) db_private_alloc (NULL, upper_size + 1);
      if (!upper_str)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	}
      else
	{
	  int upper_length = TP_FLOATING_PRECISION_VALUE;
	  intl_upper_string (alphabet,
			     (unsigned char *) DB_PULL_STRING (string),
			     upper_str, src_length);

	  upper_str[upper_size] = 0;
	  if (db_value_precision (string) != TP_FLOATING_PRECISION_VALUE)
	    {
	      intl_char_count (upper_str, upper_size,
			       (INTL_CODESET) DB_GET_STRING_CODESET (string),
			       &upper_length);
	    }
	  qstr_make_typed_string (str_type, upper_string, upper_length,
				  (char *) upper_str, upper_size,
				  DB_GET_STRING_CODESET (string),
				  DB_GET_STRING_COLLATION (string));
	  upper_string->need_clear = true;
	}
    }

  return error_status;
}

/*
 * db_string_trim () -
 *
 * Arguments:
 *        trim_operand: Specifies whether the character to be trimmed is
 *                      removed from the beginning, ending or both ends
 *                      of the string.
 *        trim_charset: (Optional) The characters to be removed.
 *          src_string: String to remove trim character from.
 *      trimmed_string: Resultant trimmed string.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE     : <trim_char> or <src_string> are
 *                                     not character strings.
 *      ER_QSTR_INVALID_TRIM_OPERAND  : <trim_char> has char length > 1.
 *      ER_QSTR_INCOMPATIBLE_CODE_SETS: <trim_char>, <src_string> and
 *                                     <trimmed_string> have different
 *                                     character code sets.
 *
 */

int
db_string_trim (const MISC_OPERAND tr_operand,
		const DB_VALUE * trim_charset,
		const DB_VALUE * src_string, DB_VALUE * trimmed_string)
{
  int error_status = NO_ERROR;
  int trim_charset_is_null = false;

  unsigned char *result;
  int result_length, result_size = 0, result_domain_length;
  DB_TYPE result_type = DB_TYPE_NULL;

  unsigned char *trim_charset_ptr = NULL;
  int trim_charset_length = 0;
  int trim_charset_size = 0;
  DB_TYPE src_type, trim_type;

  /*
   * Assert DB_VALUE structures have been allocated
   */

  assert (src_string != (DB_VALUE *) NULL);
  assert (trimmed_string != (DB_VALUE *) NULL);
  assert (trim_charset != (DB_VALUE *) NULL);

  /* if source is NULL, return NULL */
  if (DB_IS_NULL (src_string))
    {
      if (QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (src_string)))
	{
	  db_value_domain_init (trimmed_string, DB_TYPE_VARCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else
	{
	  db_value_domain_init (trimmed_string, DB_TYPE_VARNCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      return error_status;
    }

  if (trim_charset == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER,
	      0);
      return ER_QPROC_INVALID_PARAMETER;
    }

  trim_type = DB_VALUE_DOMAIN_TYPE (trim_charset);
  if (trim_type == DB_TYPE_NULL)
    {
      trim_charset_is_null = true;
    }
  else if (DB_IS_NULL (trim_charset))
    {
      if (QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (src_string)))
	{
	  db_value_domain_init (trimmed_string, DB_TYPE_VARCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else
	{
	  db_value_domain_init (trimmed_string, DB_TYPE_VARNCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      return error_status;
    }

  /*
   * Verify input parameters are all char strings and are compatible
   */

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  if (!QSTR_IS_ANY_CHAR (src_type)
      || (!trim_charset_is_null && !QSTR_IS_ANY_CHAR (trim_type)))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      return error_status;
    }

  if (!trim_charset_is_null
      && (qstr_get_category (src_string) != qstr_get_category (trim_charset)
	  || DB_GET_STRING_CODESET (src_string)
	  != DB_GET_STRING_CODESET (trim_charset)))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QSTR_INCOMPATIBLE_CODE_SETS, 0);
      return error_status;
    }

  /*
   * begin of main codes
   */
  if (!trim_charset_is_null)
    {
      trim_charset_ptr = (unsigned char *) DB_PULL_STRING (trim_charset);
      trim_charset_length = DB_GET_STRING_LENGTH (trim_charset);
      trim_charset_size = DB_GET_STRING_SIZE (trim_charset);
    }

  error_status = qstr_trim (tr_operand,
			    trim_charset_ptr,
			    trim_charset_length,
			    trim_charset_size,
			    (unsigned char *) DB_PULL_STRING (src_string),
			    DB_VALUE_DOMAIN_TYPE (src_string),
			    DB_GET_STRING_LENGTH (src_string),
			    DB_GET_STRING_SIZE (src_string),
			    (INTL_CODESET) DB_GET_STRING_CODESET (src_string),
			    &result,
			    &result_type, &result_length, &result_size);

  if (error_status == NO_ERROR && result != NULL)
    {
      result_domain_length = MIN (QSTR_MAX_PRECISION (result_type),
				  DB_VALUE_PRECISION (src_string));
      qstr_make_typed_string (result_type,
			      trimmed_string,
			      result_domain_length,
			      (char *) result, result_size,
			      DB_GET_STRING_CODESET (src_string),
			      DB_GET_STRING_COLLATION (src_string));
      result[result_size] = 0;
      trimmed_string->need_clear = true;
    }

  return error_status;
}

/* qstr_trim () -
*/
static int
qstr_trim (MISC_OPERAND trim_operand,
	   const unsigned char *trim_charset,
	   int trim_charset_length,
	   int trim_charset_size,
	   const unsigned char *src_ptr,
	   DB_TYPE src_type,
	   int src_length,
	   int src_size,
	   INTL_CODESET codeset,
	   unsigned char **result,
	   DB_TYPE * result_type, int *result_length, int *result_size)
{
  unsigned char pad_char[2], *lead_trimmed_ptr, *trail_trimmed_ptr;
  int lead_trimmed_length, trail_trimmed_length;
  int lead_trimmed_size, trail_trimmed_size, pad_char_size = 0;
  int error_status = NO_ERROR;

  /* default case */
  intl_pad_char (codeset, pad_char, &pad_char_size);
  if (trim_charset_length == 0)
    {
      trim_charset = pad_char;
      trim_charset_length = 1;
      trim_charset_size = pad_char_size;
    }

  /* trim from front */
  lead_trimmed_ptr = (unsigned char *) src_ptr;
  lead_trimmed_length = src_length;
  lead_trimmed_size = src_size;

  if (trim_operand == LEADING || trim_operand == BOTH)
    {
      trim_leading (trim_charset, trim_charset_size,
		    src_ptr, src_type, src_length, src_size,
		    codeset,
		    &lead_trimmed_ptr,
		    &lead_trimmed_length, &lead_trimmed_size);
    }

  trail_trimmed_ptr = lead_trimmed_ptr;
  trail_trimmed_length = lead_trimmed_length;
  trail_trimmed_size = lead_trimmed_size;

  if (trim_operand == TRAILING || trim_operand == BOTH)
    {
      qstr_trim_trailing (trim_charset, trim_charset_size,
			  lead_trimmed_ptr,
			  src_type,
			  lead_trimmed_length,
			  lead_trimmed_size,
			  codeset,
			  &trail_trimmed_length, &trail_trimmed_size);
    }

  /* setup result */
  *result = (unsigned char *)
    db_private_alloc (NULL, (size_t) trail_trimmed_size + 1);
  if (*result == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
      return error_status;
    }

  (void) memcpy ((char *) (*result), (char *) trail_trimmed_ptr,
		 trail_trimmed_size);
  (*result)[trail_trimmed_size] = '\0';

  if (QSTR_IS_NATIONAL_CHAR (src_type))
    {
      *result_type = DB_TYPE_VARNCHAR;
    }
  else
    {
      *result_type = DB_TYPE_VARCHAR;
    }
  *result_length = trail_trimmed_length;
  *result_size = trail_trimmed_size;

  return error_status;
}

/*
 * trim_leading () -
 *
 * Arguments:
 *       trim_charset_ptr: (in)  Single character trim string.
 *      trim_charset_size: (in)  Size of trim string.
 *         src_string_ptr: (in)  Source string to be trimmed.
 *      src_string_length: (in)  Length of source string.
 *                codeset: (in)  International codeset of source string.
 *       lead_trimmed_ptr: (out) Pointer to start of trimmed string.
 *    lead_trimmed_length: (out) Length of trimmed string.
 *
 * Returns: nothing
 *
 * Errors:
 *
 * Note:
 *     Remove trim character from the front of the source string.  No
 *     characters are actually removed.  Instead, the function returns
 *     a pointer to the beginning of the source string after the trim
 *     characters and the resultant length of the string.
 *
 */
static void
trim_leading (const unsigned char *trim_charset_ptr,
	      int trim_charset_size,
	      const unsigned char *src_ptr,
	      DB_TYPE src_type,
	      int src_length,
	      int src_size,
	      INTL_CODESET codeset,
	      unsigned char **lead_trimmed_ptr,
	      int *lead_trimmed_length, int *lead_trimmed_size)
{
  int cur_src_char_size, cur_trim_char_size;
  unsigned char *cur_src_char_ptr, *cur_trim_char_ptr;

  int cmp_flag = 0;

  *lead_trimmed_ptr = (unsigned char *) src_ptr;
  *lead_trimmed_length = src_length;
  *lead_trimmed_size = src_size;

  /* iterate for source string */
  for (cur_src_char_ptr = (unsigned char *) src_ptr;
       cur_src_char_ptr < src_ptr + src_size;)
    {
      for (cur_trim_char_ptr = (unsigned char *) trim_charset_ptr;
	   cur_src_char_ptr < (src_ptr + src_size)
	   && (cur_trim_char_ptr < trim_charset_ptr + trim_charset_size);)
	{
	  intl_char_size (cur_src_char_ptr, 1, codeset, &cur_src_char_size);
	  intl_char_size (cur_trim_char_ptr, 1, codeset, &cur_trim_char_size);

	  if (cur_src_char_size != cur_trim_char_size)
	    {
	      return;
	    }

	  cmp_flag =
	    memcmp ((char *) cur_src_char_ptr, (char *) cur_trim_char_ptr,
		    cur_trim_char_size);
	  if (cmp_flag != 0)
	    {
	      return;
	    }

	  cur_src_char_ptr += cur_src_char_size;
	  cur_trim_char_ptr += cur_trim_char_size;
	}

      if (cur_trim_char_ptr >= trim_charset_ptr + trim_charset_size)
	{			/* all string matched */
	  *lead_trimmed_length -= trim_charset_size;
	  *lead_trimmed_size -= trim_charset_size;
	  *lead_trimmed_ptr += trim_charset_size;
	}
    }
}

/*
 * qstr_trim_trailing () -
 *
 * Arguments:
 *       trim_charset_ptr: (in)  Single character trim string.
 *      trim_charset_size: (in)  Size of trim string.
 *                src_ptr: (in)  Source string to be trimmed.
 *             src_length: (in)  Length of source string.
 *                codeset: (in)  International codeset of source string.
 *   trail_trimmed_length: (out) Length of trimmed string.
 *
 * Returns: nothing
 *
 * Errors:
 *
 * Note:
 *     Remove trim character from the end of the source string.  No
 *     characters are actually removed.  Instead, the function returns
 *     a pointer to the beginning of the source string after the trim
 *     characters and the resultant length of the string.
 *
 */
void
qstr_trim_trailing (const unsigned char *trim_charset_ptr,
		    int trim_charset_size,
		    const unsigned char *src_ptr,
		    DB_TYPE src_type,
		    int src_length,
		    int src_size,
		    INTL_CODESET codeset,
		    int *trail_trimmed_length, int *trail_trimmed_size)
{
  int prev_src_char_size, prev_trim_char_size;
  unsigned char *cur_src_char_ptr, *cur_trim_char_ptr;
  unsigned char *prev_src_char_ptr, *prev_trim_char_ptr;
  int cmp_flag = 0;

  *trail_trimmed_length = src_length;
  *trail_trimmed_size = src_size;

  /* iterate for source string */
  for (cur_src_char_ptr = (unsigned char *) src_ptr + src_size;
       cur_src_char_ptr > src_ptr;)
    {
      for (cur_trim_char_ptr =
	   (unsigned char *) trim_charset_ptr + trim_charset_size;
	   cur_trim_char_ptr > trim_charset_ptr
	   && cur_src_char_ptr > src_ptr;)
	{
	  /* get previous letter */
	  prev_src_char_ptr =
	    intl_prev_char (cur_src_char_ptr, src_ptr, codeset,
			    &prev_src_char_size);
	  prev_trim_char_ptr =
	    intl_prev_char (cur_trim_char_ptr, trim_charset_ptr, codeset,
			    &prev_trim_char_size);

	  if (prev_trim_char_size != prev_src_char_size)
	    {
	      return;
	    }

	  cmp_flag = memcmp ((char *) prev_src_char_ptr,
			     (char *) prev_trim_char_ptr,
			     prev_trim_char_size);
	  if (cmp_flag != 0)
	    {
	      return;
	    }

	  cur_src_char_ptr -= prev_src_char_size;
	  cur_trim_char_ptr -= prev_trim_char_size;
	}

      if (cur_trim_char_ptr <= trim_charset_ptr)
	{
	  *trail_trimmed_length -= trim_charset_size;
	  *trail_trimmed_size -= trim_charset_size;
	}
    }
}

/*
 * db_string_pad () -
 *
 * Arguments:
 *      pad_operand: (in)  Left or Right padding?
 *       src_string: (in)  Source string to be padded.
 *       pad_length: (in)  Length of padded string
 *      pad_charset: (in)  Padding char set
 *    padded_string: (out) Padded string
 *
 * Returns: nothing
 */
int
db_string_pad (const MISC_OPERAND pad_operand, const DB_VALUE * src_string,
	       const DB_VALUE * pad_length, const DB_VALUE * pad_charset,
	       DB_VALUE * padded_string)
{
  int error_status = NO_ERROR;
  int total_length;

  unsigned char *result;
  int result_length = 0, result_size = 0;
  DB_TYPE result_type;

  unsigned char *pad_charset_ptr = NULL;
  int pad_charset_length = 0;
  int pad_charset_size = 0;
  DB_TYPE src_type;

  assert (src_string != (DB_VALUE *) NULL);
  assert (padded_string != (DB_VALUE *) NULL);

  /* if source is NULL, return NULL */
  if (DB_IS_NULL (src_string) || DB_IS_NULL (pad_charset))
    {
      if (QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (src_string)))
	{
	  db_value_domain_init (padded_string, DB_TYPE_VARCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else
	{
	  db_value_domain_init (padded_string, DB_TYPE_VARNCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      return error_status;
    }

  if (DB_IS_NULL (pad_length) ||
      (total_length = DB_GET_INTEGER (pad_length)) <= 0)
    {
      /*error_status = ER_QPROC_INVALID_PARAMETER; */
      if (QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (src_string)))
	{
	  db_value_domain_init (padded_string, DB_TYPE_VARCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else
	{
	  db_value_domain_init (padded_string, DB_TYPE_VARNCHAR,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      return error_status;
    }

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  if (!QSTR_IS_ANY_CHAR (src_type) || !is_char_string (pad_charset))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      return error_status;
    }

  if ((qstr_get_category (src_string) != qstr_get_category (pad_charset))
      || (DB_GET_STRING_CODESET (src_string)
	  != DB_GET_STRING_CODESET (pad_charset)))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QSTR_INCOMPATIBLE_CODE_SETS, 0);
      return error_status;
    }

  error_status = qstr_pad (pad_operand, total_length,
			   (unsigned char *) DB_PULL_STRING (pad_charset),
			   DB_GET_STRING_LENGTH (pad_charset),
			   DB_GET_STRING_SIZE (pad_charset),
			   (unsigned char *) DB_PULL_STRING (src_string),
			   DB_VALUE_DOMAIN_TYPE (src_string),
			   DB_GET_STRING_LENGTH (src_string),
			   DB_GET_STRING_SIZE (src_string),
			   (INTL_CODESET) DB_GET_STRING_CODESET (src_string),
			   &result,
			   &result_type, &result_length, &result_size);

  if (error_status == NO_ERROR && result != NULL)
    {
      qstr_make_typed_string (result_type, padded_string, result_length,
			      (char *) result, result_size,
			      DB_GET_STRING_CODESET (src_string),
			      DB_GET_STRING_COLLATION (src_string));

      result[result_size] = 0;
      padded_string->need_clear = true;
    }

  return error_status;
}

/*
 * qstr_pad () -
 */
static int
qstr_pad (MISC_OPERAND pad_operand,
	  int pad_length,
	  const unsigned char *pad_charset_ptr,
	  int pad_charset_length,
	  int pad_charset_size,
	  const unsigned char *src_ptr,
	  DB_TYPE src_type,
	  int src_length,
	  int src_size,
	  INTL_CODESET codeset,
	  unsigned char **result,
	  DB_TYPE * result_type, int *result_length, int *result_size)
{
  unsigned char def_pad_char[2];
  unsigned char *cur_pad_char_ptr;
  int def_pad_char_size = 0;	/* default padding char */
  int truncate_size, pad_size, alloc_size, cnt;
  int length_to_be_padded;	/* length that will be really padded */
  int remain_length_to_be_padded;	/* remained length that will be padded */
  int pad_full_size = 0;
  int pad_reminder_size = 0;
  int error_status = NO_ERROR;

  intl_pad_char (codeset, def_pad_char, &def_pad_char_size);

  if (pad_charset_length == 0)
    {
      pad_charset_ptr = def_pad_char;
      pad_charset_length = 1;
      pad_charset_size = def_pad_char_size;
    }

  assert (pad_charset_length > 0);

  if (src_length >= pad_length)
    {
      alloc_size = src_size;
    }
  else
    {
      pad_full_size = ((pad_length - src_length) / pad_charset_length)
	* pad_charset_size;
      intl_char_size ((unsigned char *) pad_charset_ptr,
		      (pad_length - src_length) % pad_charset_length,
		      codeset, &pad_reminder_size);
      alloc_size = src_size + pad_full_size + pad_reminder_size;
    }

  if (QSTR_IS_NATIONAL_CHAR (src_type))
    {
      *result_type = DB_TYPE_VARNCHAR;
    }
  else
    {
      *result_type = DB_TYPE_VARCHAR;
    }

  *result =
    (unsigned char *) db_private_alloc (NULL, (size_t) alloc_size + 1);
  if (*result == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
      return error_status;
    }

  /*
   * now start padding
   */

  /* if source length is greater than pad_length */
  if (src_length >= pad_length)
    {
      truncate_size = 0;	/* SIZE to be cut */
      intl_char_size ((unsigned char *) src_ptr, pad_length, codeset,
		      &truncate_size);
      memcpy ((char *) (*result), (char *) src_ptr, truncate_size);

      *result_length = pad_length;
      *result_size = truncate_size;

      return error_status;
    }

  /*
   * Get real length to be paded
   * if source length is greater than pad_length
   */

  length_to_be_padded = pad_length - src_length;

  /* pad heading first */

  cnt = 0;			/* how many times copy pad_char_set */
  pad_size = 0;			/* SIZE of padded char */
  remain_length_to_be_padded = 0;

  for (; cnt < (length_to_be_padded / pad_charset_length); cnt++)
    {
      (void) memcpy ((char *) (*result) + pad_charset_size * cnt,
		     (char *) pad_charset_ptr, pad_charset_size);
    }
  pad_size = pad_charset_size * cnt;
  remain_length_to_be_padded = (pad_length - src_length) % pad_charset_length;

  if (remain_length_to_be_padded != 0)
    {
      int remain_size_to_be_padded = 0;

      assert (remain_length_to_be_padded > 0);

      cur_pad_char_ptr = (unsigned char *) pad_charset_ptr;

      intl_char_size (cur_pad_char_ptr, remain_length_to_be_padded,
		      codeset, &remain_size_to_be_padded);
      (void) memcpy ((char *) (*result) + pad_size,
		     (char *) cur_pad_char_ptr, remain_size_to_be_padded);
      cur_pad_char_ptr += remain_size_to_be_padded;
      pad_size += remain_size_to_be_padded;
    }

  memcpy ((char *) (*result) + pad_size, src_ptr, src_size);

  if (pad_operand == TRAILING)
    {				/* switch source and padded string */
      memmove ((char *) (*result) + src_size, (char *) (*result), pad_size);
      memcpy ((char *) (*result), src_ptr, src_size);
    }

  pad_size += src_size;

  *result_length = pad_length;
  *result_size = pad_size;

  return error_status;
}

/*
 * db_string_like () -
 *
 * Arguments:
 *             src_string:  (IN) Source string.
 *                pattern:  (IN) Pattern string which can contain % and _
 *                               characters.
 *               esc_char:  (IN) Optional escape character.
 *                 result: (OUT) Integer result.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE:
 *          <src_string>, <pattern>, or <esc_char> (if it's not NULL)
 *          is not a character string.
 *
 *      ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *          <src_string>, <pattern>, and <esc_char> (if it's not NULL)
 *          have different character code sets.
 *
 *      ER_QSTR_INVALID_ESCAPE_SEQUENCE:
 *          An illegal pattern is specified.
 *
 *      ER_QSTR_INVALID_ESCAPE_CHARACTER:
 *          If <esc_char> is not NULL and the length of E is > 1.
 *
 */
/* TODO ER_QSTR_INVALID_ESCAPE_CHARACTER is not checked for, although it
        probably should be (the escape sequence string should contain a single
	character)
*/

int
db_string_like (const DB_VALUE * src_string,
		const DB_VALUE * pattern,
		const DB_VALUE * esc_char, int *result)
{
  QSTR_CATEGORY src_category = QSTR_UNKNOWN;
  QSTR_CATEGORY pattern_category = QSTR_UNKNOWN;
  int error_status = NO_ERROR;
  DB_TYPE src_type = DB_TYPE_UNKNOWN;
  DB_TYPE pattern_type = DB_TYPE_UNKNOWN;
  char *src_char_string_p = NULL;
  char *pattern_char_string_p = NULL;
  char const *esc_char_p = NULL;
  int src_length = 0, pattern_length = 0;
  int coll_id;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_string != NULL);
  assert (pattern != NULL);

  src_category = qstr_get_category (src_string);
  pattern_category = qstr_get_category (pattern);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  pattern_type = DB_VALUE_DOMAIN_TYPE (pattern);

  if (!QSTR_IS_ANY_CHAR (src_type) || !QSTR_IS_ANY_CHAR (pattern_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      *result = V_ERROR;
      return error_status;
    }

  if (src_category != pattern_category
      || (DB_GET_STRING_CODESET (src_string)
	  != DB_GET_STRING_CODESET (pattern)))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      *result = V_ERROR;
      return error_status;
    }

  if (DB_IS_NULL (src_string))
    {
      *result = V_UNKNOWN;
      return error_status;
    }

  if (DB_IS_NULL (pattern))
    {
      *result = V_FALSE;
      return error_status;
    }

  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (src_string),
		       DB_GET_STRING_COLLATION (pattern), coll_id);
  if (coll_id == -1)
    {
      error_status = ER_QSTR_INCOMPATIBLE_COLLATIONS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      *result = V_ERROR;
      return error_status;
    }

  if (esc_char)
    {
      if (DB_IS_NULL (esc_char))
	{
	  /* The implicit escape character ('\\') is used if
	     (a LIKE b ESCAPE NULL) is given in the syntax */
	  esc_char_p = "\\";
	}
      else
	{
	  QSTR_CATEGORY esc_category = qstr_get_category (esc_char);
	  DB_TYPE esc_type = DB_VALUE_DOMAIN_TYPE (esc_char);
	  int esc_char_len, esc_char_size;

	  if (QSTR_IS_ANY_CHAR (esc_type))
	    {
	      if (src_category == esc_category)
		{
		  esc_char_p = DB_PULL_STRING (esc_char);
		  esc_char_size = DB_GET_STRING_SIZE (esc_char);

		  intl_char_count ((unsigned char *) esc_char_p,
				   esc_char_size,
				   DB_GET_STRING_CODESET (esc_char),
				   &esc_char_len);

		  assert (esc_char_p != NULL);
		  if (esc_char_len != 1)
		    {
		      error_status = ER_QSTR_INVALID_ESCAPE_SEQUENCE;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status,
			      0);
		      *result = V_ERROR;
		      return error_status;
		    }
		}
	      else
		{
		  error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
		  *result = V_ERROR;
		  return error_status;
		}
	    }
	  else
	    {
	      error_status = ER_QSTR_INVALID_DATA_TYPE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QSTR_INVALID_DATA_TYPE, 0);
	      *result = V_ERROR;
	      return error_status;
	    }
	}
    }

  src_char_string_p = DB_PULL_STRING (src_string);
  src_length = DB_GET_STRING_SIZE (src_string);

  pattern_char_string_p = DB_PULL_STRING (pattern);
  pattern_length = DB_GET_STRING_SIZE (pattern);

  *result = qstr_eval_like (src_char_string_p, src_length,
			    pattern_char_string_p, pattern_length,
			    (esc_char ? esc_char_p : NULL),
			    DB_GET_STRING_CODESET (src_string), coll_id);

  if (*result == V_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QSTR_INVALID_ESCAPE_SEQUENCE, 0);
    }

  return ((*result == V_ERROR) ? ER_QSTR_INVALID_ESCAPE_SEQUENCE
	  : error_status);
}

/*
 * db_string_rlike () - check for match between string and regex
 *
 * Arguments:
 *             src_string:     (IN) Source string.
 *                pattern:     (IN) Regular expression.
 *	   case_sensitive:     (IN) Perform case sensitive matching when 1
 *	       comp_regex: (IN/OUT) Compiled regex object
 *	     comp_pattern: (IN/OUT) Compiled regex pattern
 *                 result:    (OUT) Integer result.
 *
 * Returns: int
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE:
 *          <src_string>, <pattern> (if it's not NULL)
 *          is not a character string.
 *
 *      ER_QSTR_INCOMPATIBLE_CODE_SETS:
 *          <src_string>, <pattern> (if it's not NULL)
 *          have different character code sets.
 *
 *      ER_QSTR_INVALID_ESCAPE_SEQUENCE:
 *          An illegal pattern is specified.
 *
 */

int
db_string_rlike (const DB_VALUE * src_string, const DB_VALUE * pattern,
		 const DB_VALUE * case_sensitive, cub_regex_t ** comp_regex,
		 char **comp_pattern, int *result)
{
  QSTR_CATEGORY src_category = QSTR_UNKNOWN;
  QSTR_CATEGORY pattern_category = QSTR_UNKNOWN;
  int error_status = NO_ERROR;
  DB_TYPE src_type = DB_TYPE_UNKNOWN;
  DB_TYPE pattern_type = DB_TYPE_UNKNOWN;
  DB_TYPE case_sens_type = DB_TYPE_UNKNOWN;
  const char *src_char_string_p = NULL;
  const char *pattern_char_string_p = NULL;
  bool is_case_sensitive = false;
  int src_length = 0, pattern_length = 0;

  char rx_err_buf[REGEX_MAX_ERROR_MSG_SIZE] = { '\0' };
  int rx_err = CUB_REG_OKAY;
  int rx_err_len = 0;
  char *rx_compiled_pattern = NULL;
  cub_regex_t *rx_compiled_regex = NULL;

  /* check for allocated DB values */
  assert (src_string != NULL);
  assert (pattern != NULL);
  assert (case_sensitive != NULL);

  /* get compiled pattern */
  if (comp_pattern != NULL)
    {
      rx_compiled_pattern = *comp_pattern;
    }

  /* if regex object was specified, use local regex */
  if (comp_regex != NULL)
    {
      rx_compiled_regex = *comp_regex;
    }

  /* type checking */
  src_category = qstr_get_category (src_string);
  pattern_category = qstr_get_category (pattern);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  pattern_type = DB_VALUE_DOMAIN_TYPE (pattern);
  case_sens_type = DB_VALUE_DOMAIN_TYPE (case_sensitive);

  if (!QSTR_IS_ANY_CHAR (src_type) || !QSTR_IS_ANY_CHAR (pattern_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      *result = V_ERROR;
      goto cleanup;
    }

  if (src_category != pattern_category)
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      *result = V_ERROR;
      goto cleanup;
    }

  if (DB_IS_NULL (src_string) || DB_IS_NULL (pattern))
    {
      *result = V_UNKNOWN;
      goto cleanup;
    }

  if (DB_IS_NULL (case_sensitive) || case_sens_type != DB_TYPE_INTEGER)
    {
      error_status = ER_QPROC_INVALID_PARAMETER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      *result = V_ERROR;
      goto cleanup;
    }

  src_char_string_p = DB_PULL_STRING (src_string);
  src_length = DB_GET_STRING_SIZE (src_string);

  pattern_char_string_p = DB_PULL_STRING (pattern);
  pattern_length = DB_GET_STRING_SIZE (pattern);

  /* initialize regex library memory allocator */
  cub_regset_malloc ((CUB_REG_MALLOC) db_private_alloc_external);
  cub_regset_realloc ((CUB_REG_REALLOC) db_private_realloc_external);
  cub_regset_free ((CUB_REG_FREE) db_private_free_external);

  /* extract case sensitivity */
  is_case_sensitive = (case_sensitive->data.i != 0);

  /* check for recompile */
  if (rx_compiled_pattern == NULL || rx_compiled_regex == NULL
      || pattern_length != strlen (rx_compiled_pattern)
      || strncmp (rx_compiled_pattern, pattern_char_string_p,
		  pattern_length) != 0)
    {
      /* regex must be recompiled if regex object is not specified, pattern is
         not specified or compiled pattern does not match current pattern */

      /* update compiled pattern */
      if (rx_compiled_pattern != NULL)
	{
	  /* free old memory */
	  db_private_free_and_init (NULL, rx_compiled_pattern);
	}

      /* allocate new memory */
      rx_compiled_pattern =
	(char *) db_private_alloc (NULL, pattern_length + 1);

      if (rx_compiled_pattern == NULL)
	{
	  /* out of memory */
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  *result = V_ERROR;
	  goto cleanup;
	}

      /* copy string */
      memcpy (rx_compiled_pattern, pattern_char_string_p, pattern_length);
      rx_compiled_pattern[pattern_length] = '\0';

      /* update compiled regex */
      if (rx_compiled_regex != NULL)
	{
	  /* free previously allocated memory */
	  cub_regfree (rx_compiled_regex);
	  db_private_free_and_init (NULL, rx_compiled_regex);
	}

      /* allocate memory for new regex object */
      rx_compiled_regex =
	(cub_regex_t *) db_private_alloc (NULL, sizeof (cub_regex_t));

      if (rx_compiled_regex == NULL)
	{
	  /* out of memory */
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  *result = V_ERROR;
	  goto cleanup;
	}

      /* compile regex */
      rx_err = cub_regcomp (rx_compiled_regex, rx_compiled_pattern,
			    CUB_REG_EXTENDED | CUB_REG_NOSUB
			    | (is_case_sensitive ? 0 : CUB_REG_ICASE));

      if (rx_err != CUB_REG_OKAY)
	{
	  /* regex compilation error */
	  rx_err_len = cub_regerror (rx_err, rx_compiled_regex, rx_err_buf,
				     REGEX_MAX_ERROR_MSG_SIZE);
	  error_status = ER_REGEX_COMPILE_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1,
		  rx_err_buf);
	  *result = V_ERROR;
	  db_private_free_and_init (NULL, rx_compiled_regex);
	  goto cleanup;
	}
    }

  /* match against pattern; regexec returns zero on match */
  rx_err =
    cub_regexec (rx_compiled_regex, src_char_string_p, src_length, 0, NULL,
		 0);
  switch (rx_err)
    {
    case CUB_REG_OKAY:
      *result = V_TRUE;
      break;

    case CUB_REG_NOMATCH:
      *result = V_FALSE;
      break;

    default:
      rx_err_len = cub_regerror (rx_err, rx_compiled_regex, rx_err_buf,
				 REGEX_MAX_ERROR_MSG_SIZE);
      error_status = ER_REGEX_EXEC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, rx_err_buf);
      *result = V_ERROR;
      break;
    }

cleanup:

  if ((comp_regex == NULL || error_status != NO_ERROR)
      && rx_compiled_regex != NULL)
    {
      /* free memory if (using local regex) or (error occurred) */
      cub_regfree (rx_compiled_regex);
      db_private_free_and_init (NULL, rx_compiled_regex);
    }

  if ((comp_pattern == NULL || error_status != NO_ERROR)
      && rx_compiled_pattern != NULL)
    {
      /* free memory if (using local pattern) or (error occurred) */
      db_private_free_and_init (NULL, rx_compiled_pattern);
    }

  if (comp_regex != NULL)
    {
      /* pass compiled regex object out */
      *comp_regex = rx_compiled_regex;
    }

  if (comp_pattern != NULL)
    {
      /* pass compiled pattern out */
      *comp_pattern = rx_compiled_pattern;
    }

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS)
      && error_status != NO_ERROR)
    {
      /* we must not return an error code */
      *result = V_UNKNOWN;
      return NO_ERROR;
    }

  /* return */
  return error_status;
}

/*
 * db_string_limit_size_string () - limits the size of a string. It limits
 *				    the size of value, but in case of fixed
 *				    length values, it limits also the domain
 *				    precision.
 *
 * Arguments:
 *              src: (IN)  String variable.
 *	     result: (OUT) Variable with new size
 *	   new_size: (IN)  New size for the string (in bytes).
 *	spare_bytes: (OUT) the number of bytes that could fit from last
 *		      truncated character
 *
 * Returns:
 *
 * Errors:
 *	ER_QSTR_INVALID_DATA_TYPE:
 *		  <src_string> is not CHAR, NCHAR, VARCHAR, VARNCHAR, BIT or
 *		   VARBIT
 *
 * Note : result variable must already be created
 *	  operates directly on memory buffer
 *	  if the new size is greater than the source, it clones the input
 *	  The truncation of domain size in case of fixed domain argument
 *	  is needed in context of GROUP_CONCAT, when the result needs to be
 *	  truncated.
 *	  The full-char adjusting code in this function is specific to
 *	  GROUP_CONCAT.
 */
int
db_string_limit_size_string (DB_VALUE * src_string, DB_VALUE * result,
			     const int new_size, int *spare_bytes)
{
  int result_size = 0, src_size = 0, src_domain_precision = 0;
  unsigned char *r;
  int error_status = NO_ERROR;
  DB_TYPE src_type;
  int char_count = 0, adj_char_size = 0;

  assert (src_string != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);
  assert (new_size >= 0);
  assert (spare_bytes != NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);

  if (!QSTR_IS_ANY_CHAR_OR_BIT (src_type))
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  src_size = DB_GET_STRING_SIZE (src_string);
  src_domain_precision = DB_VALUE_PRECISION (src_string);

  if (new_size < 0)
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  *spare_bytes = 0;

  if (src_size <= 0 || new_size >= src_size)
    {
      assert (error_status == NO_ERROR);
      goto exit_copy;
    }

  result_size = new_size;

  /* Adjust size to a full character.
   */
  intl_char_count ((unsigned char *) DB_PULL_STRING (src_string), result_size,
		   DB_GET_STRING_CODESET (src_string), &char_count);
  intl_char_size ((unsigned char *) DB_PULL_STRING (src_string), char_count,
		  DB_GET_STRING_CODESET (src_string), &adj_char_size);

  assert (adj_char_size <= result_size);

  /* Allocate storage for the result string */
  r = (unsigned char *) db_private_alloc (NULL, (size_t) result_size + 1);
  if (r == NULL)
    {
      goto mem_error;
    }
  memset (r, 0, (size_t) result_size + 1);

  if (adj_char_size > 0)
    {
      memcpy ((char *) r, (char *) DB_PULL_STRING (src_string),
	      adj_char_size);
    }
  /* adjust also domain precision in case of fixed length types */
  if (QSTR_IS_FIXED_LENGTH (src_type))
    {
      src_domain_precision = MIN (src_domain_precision, char_count);
    }
  qstr_make_typed_string (src_type,
			  result,
			  src_domain_precision, (char *) r, adj_char_size,
			  DB_GET_STRING_CODESET (src_string),
			  DB_GET_STRING_COLLATION (src_string));
  result->need_clear = true;

  *spare_bytes = result_size - adj_char_size;

  assert (error_status == NO_ERROR);
  return error_status;

mem_error:
  assert (er_errid () != NO_ERROR);
  error_status = er_errid ();
  return error_status;

exit_copy:
  assert (error_status == NO_ERROR);
  error_status = pr_clone_value (src_string, result);
  return error_status;
}

/*
 * db_string_fix_string_size () - fixes the size of a string according to its
 *				  content (NULL terminator)
 *
 * Arguments:
 *            src: (IN/OUT)  String variable.
 *
 * Returns:
 *
 * Errors:
 *	ER_QSTR_INVALID_DATA_TYPE:
 *		  <src_string> is not CHAR, NCHAR, VARCHAR, VARNCHAR
 *
 * Note : Used in context of GROUP_CONCAT. It is complementary to
 *	  'db_string_limit_size_string' function
 */
int
db_string_fix_string_size (DB_VALUE * src_string)
{
  int val_size = 0;
  int string_size = 0;
  int error_status = NO_ERROR;
  DB_TYPE src_type;
  bool save_need_clear;

  assert (src_string != (DB_VALUE *) NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);

  if (!QSTR_IS_ANY_CHAR (src_type))
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  val_size = DB_GET_STRING_SIZE (src_string);
  /* this is a system generated string; it must have the null terminator */
  string_size = strlen (DB_PULL_STRING (src_string));
  assert (val_size >= string_size);

  save_need_clear = src_string->need_clear;
  qstr_make_typed_string (src_type, src_string,
			  DB_VALUE_PRECISION (src_string),
			  DB_PULL_STRING (src_string), string_size,
			  DB_GET_STRING_CODESET (src_string),
			  DB_GET_STRING_COLLATION (src_string));
  src_string->need_clear = save_need_clear;

  return error_status;
}

/*
 * qstr_eval_like () -
 */
/* TODO this function should be modified to not rely on the special value 1
        in the situation of no escape character. With the current
	implementation it will incorrectly process strings containing
	character 1.
*/
static int
qstr_eval_like (const char *tar, int tar_length,
		const char *expr, int expr_length,
		const char *escape, INTL_CODESET codeset, int coll_id)
{
  const int IN_CHECK = 0;
  const int IN_PERCENT = 1;

  int status = IN_CHECK;
  unsigned char *tarstack[STACK_SIZE], *exprstack[STACK_SIZE];
  int stackp = -1;

  unsigned char *tar_ptr, *end_tar;
  unsigned char *expr_ptr, *end_expr;
  int substrlen = 0;
  bool escape_is_match_one =
    ((escape != NULL) && *escape == LIKE_WILDCARD_MATCH_ONE);
  bool escape_is_match_many =
    ((escape != NULL) && *escape == LIKE_WILDCARD_MATCH_MANY);
  unsigned char pad_char[2];


  LANG_COLLATION *current_collation;

  int pad_char_size;

  current_collation = lang_get_collation (coll_id);
  intl_pad_char (codeset, pad_char, &pad_char_size);

  tar_ptr = (unsigned char *) tar;
  expr_ptr = (unsigned char *) expr;
  end_tar = (unsigned char *) (tar + tar_length);
  end_expr = (unsigned char *) (expr + expr_length);

  while (1)
    {
      int char_size = 1;
      int dummy = 1;

      if (status == IN_CHECK)
	{
	  bool go_back = true;
	  if (expr_ptr == end_expr)
	    {
	      go_back = false;

	      if (codeset != INTL_CODESET_KSC5601_EUC)
		{
		  while (tar_ptr < end_tar && *tar_ptr == ' ')
		    {
		      tar_ptr++;
		    }
		}
	      else
		{
		  while (tar_ptr < end_tar)
		    {
		      /* EUC-KR : both ASCII space and EUC-KR padding char
		       * are ignored */
		      if (*tar_ptr == ' ')
			{
			  tar_ptr++;
			}
		      else if (tar_ptr + pad_char_size <= end_tar
			       && memcmp (tar_ptr, pad_char, pad_char_size)
			       == 0)
			{
			  tar_ptr += pad_char_size;
			}
		      else
			{
			  break;
			}
		    }
		}

	      if (tar_ptr == end_tar)
		{
		  return V_TRUE;
		}
	      else
		{
		  if (stackp >= 0 && stackp < STACK_SIZE)
		    {
		      tar_ptr = tarstack[stackp];
		      INTL_NEXT_CHAR (tar_ptr, tar_ptr, codeset, &dummy);
		      expr_ptr = exprstack[stackp--];
		    }
		  else
		    {
		      return V_FALSE;
		    }
		}
	    }
	  else if (!escape_is_match_many && expr_ptr < end_expr
		   && *expr_ptr == LIKE_WILDCARD_MATCH_MANY)
	    {
	      go_back = false;
	      status = IN_PERCENT;
	      while ((expr_ptr + 1 < end_expr)
		     && *(expr_ptr + 1) == LIKE_WILDCARD_MATCH_MANY)
		{
		  expr_ptr++;
		}
	    }
	  else if (tar_ptr < end_tar && expr_ptr < end_expr)
	    {
	      if (!escape_is_match_one
		  && *expr_ptr == LIKE_WILDCARD_MATCH_ONE)
		{
		  INTL_NEXT_CHAR (tar_ptr, tar_ptr, codeset, &dummy);
		  expr_ptr++;
		  go_back = false;
		}
	      else
		{
		  unsigned char *expr_seq_end = expr_ptr;
		  int cmp;
		  int tar_matched_size;
		  unsigned char *match_escape = NULL;
		  bool inescape = false;
		  bool has_last_escape = false;

		  /* build sequence to check (until wildcard) */
		  do
		    {
		      if (!inescape &&
			  (((!escape_is_match_many &&
			     *expr_seq_end == LIKE_WILDCARD_MATCH_MANY)
			    || (!escape_is_match_one &&
				*expr_seq_end == LIKE_WILDCARD_MATCH_ONE))))
			{
			  break;
			}

		      /* set escape for match: if remains NULL, we don't check
		       * for escape in matching function */
		      if (!inescape && escape != NULL
			  && intl_cmp_char (expr_seq_end,
					    (unsigned char *) escape,
					    codeset, &dummy) == 0)
			{
			  /* last escape character is not considered escape,
			   * but normal character */
			  if (expr_seq_end + 1 >= end_expr)
			    {
			      has_last_escape = true;
			      inescape = false;
			    }
			  else
			    {
			      inescape = true;
			      match_escape = (unsigned char *) escape;
			    }
			}
		      else
			{
			  inescape = false;
			}
		      INTL_NEXT_CHAR (expr_seq_end, expr_seq_end, codeset,
				      &dummy);
		    }
		  while (expr_seq_end < end_expr);

		  assert (end_tar - tar_ptr > 0);
		  assert (expr_seq_end - expr_ptr > 0);

		  /* match using collation */
		  cmp =
		    current_collation->strmatch (current_collation, true,
						 tar_ptr, end_tar - tar_ptr,
						 expr_ptr,
						 expr_seq_end - expr_ptr,
						 match_escape,
						 has_last_escape,
						 &tar_matched_size);

		  if (cmp == 0)
		    {
		      tar_ptr += tar_matched_size;
		      expr_ptr = expr_seq_end;
		      go_back = false;
		    }

		  assert (tar_ptr <= end_tar);
		  assert (expr_ptr <= end_expr);
		}
	    }

	  if (go_back)
	    {
	      if (stackp >= 0 && stackp < STACK_SIZE)
		{
		  tar_ptr = tarstack[stackp];
		  INTL_NEXT_CHAR (tar_ptr, tar_ptr, codeset, &dummy);
		  expr_ptr = exprstack[stackp--];
		}
	      else if (stackp > STACK_SIZE)
		{
		  return V_ERROR;
		}
	      else
		{
		  return V_FALSE;
		}
	    }
	}
      else
	{
	  unsigned char *next_expr_ptr;
	  INTL_NEXT_CHAR (next_expr_ptr, expr_ptr, codeset, &dummy);

	  assert (status == IN_PERCENT);
	  if ((next_expr_ptr < end_expr)
	      && (!escape_is_match_one || escape == NULL)
	      && *next_expr_ptr == LIKE_WILDCARD_MATCH_ONE)
	    {
	      if (stackp >= STACK_SIZE - 1)
		{
		  return V_ERROR;
		}
	      tarstack[++stackp] = tar_ptr;
	      exprstack[stackp] = expr_ptr;
	      expr_ptr = next_expr_ptr;
	      INTL_NEXT_CHAR (next_expr_ptr, expr_ptr, codeset, &dummy);

	      if (stackp > STACK_SIZE)
		{
		  return V_ERROR;
		}
	      status = IN_CHECK;
	      continue;
	    }

	  if (next_expr_ptr == end_expr)
	    {
	      return V_TRUE;
	    }

	  if (tar_ptr < end_tar && next_expr_ptr < end_expr)
	    {
	      unsigned char *expr_seq_end = next_expr_ptr;
	      int cmp;
	      int tar_matched_size;
	      unsigned char *match_escape = NULL;
	      bool inescape = false;
	      bool has_last_escape = false;

	      /* build sequence to check (until wildcard) */
	      do
		{
		  if (!inescape &&
		      (((!escape_is_match_many &&
			 *expr_seq_end == LIKE_WILDCARD_MATCH_MANY)
			|| (!escape_is_match_one &&
			    *expr_seq_end == LIKE_WILDCARD_MATCH_ONE))))
		    {
		      break;
		    }

		  /* set escape for match: if remains NULL, we don't check
		   * for escape in matching function */
		  if (!inescape && escape != NULL
		      && intl_cmp_char (expr_seq_end,
					(unsigned char *) escape,
					codeset, &dummy) == 0)
		    {
		      /* last escape character is not considered escape,
		       * but normal character */
		      if (expr_seq_end + 1 >= end_expr)
			{
			  has_last_escape = true;
			  inescape = false;
			}
		      else
			{
			  inescape = true;
			  match_escape = (unsigned char *) escape;
			}
		    }
		  else
		    {
		      inescape = false;
		    }

		  INTL_NEXT_CHAR (expr_seq_end, expr_seq_end, codeset,
				  &dummy);
		}
	      while (expr_seq_end < end_expr);

	      assert (end_tar - tar_ptr > 0);
	      assert (expr_seq_end - next_expr_ptr > 0);

	      do
		{
		  /* match using collation */
		  cmp =
		    current_collation->strmatch (current_collation, true,
						 tar_ptr, end_tar - tar_ptr,
						 next_expr_ptr,
						 expr_seq_end - next_expr_ptr,
						 match_escape,
						 has_last_escape,
						 &tar_matched_size);

		  if (cmp == 0)
		    {
		      if (stackp >= STACK_SIZE - 1)
			{
			  return V_ERROR;
			}
		      tarstack[++stackp] = tar_ptr;
		      tar_ptr += tar_matched_size;

		      exprstack[stackp] = expr_ptr;
		      expr_ptr = expr_seq_end;

		      if (stackp > STACK_SIZE)
			{
			  return V_ERROR;
			}
		      status = IN_CHECK;
		      break;
		    }
		  else
		    {
		      /* check starting from next char */
		      INTL_NEXT_CHAR (tar_ptr, tar_ptr, codeset, &dummy);
		    }
		}
	      while (tar_ptr < end_tar);
	    }
	}

      if (tar_ptr == end_tar)
	{
	  while (expr_ptr < end_expr && *expr_ptr == LIKE_WILDCARD_MATCH_MANY)
	    {
	      expr_ptr++;
	    }

	  if (expr_ptr == end_expr)
	    {
	      return V_TRUE;
	    }
	  else
	    {
	      return V_FALSE;
	    }
	}
      else if (tar_ptr > end_tar)
	{
	  return V_FALSE;
	}
    }
}

/*
 * db_string_replace () -
 */
int
db_string_replace (const DB_VALUE * src_string, const DB_VALUE * srch_string,
		   const DB_VALUE * repl_string, DB_VALUE * replaced_string)
{
  int error_status = NO_ERROR;
  unsigned char *result_ptr = NULL;
  int result_length = 0, result_size = 0;
  DB_TYPE result_type = DB_TYPE_NULL;
  int coll_id, coll_id_tmp;
  DB_VALUE dummy_string;

  assert (src_string != (DB_VALUE *) NULL);
  assert (replaced_string != (DB_VALUE *) NULL);

  DB_MAKE_NULL (&dummy_string);

  if (DB_IS_NULL (src_string) || DB_IS_NULL (srch_string)
      || DB_IS_NULL (repl_string))
    {
      if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true
	  && !DB_IS_NULL (src_string) && is_char_string (src_string))
	/* srch_string or repl_string is null */
	{
	  error_status =
	    db_string_make_empty_typed_string (NULL, &dummy_string,
					       DB_VALUE_DOMAIN_TYPE
					       (src_string),
					       TP_FLOATING_PRECISION_VALUE,
					       DB_GET_STRING_CODESET
					       (src_string),
					       DB_GET_STRING_COLLATION
					       (src_string));
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (DB_IS_NULL (srch_string))
	    {
	      srch_string = &dummy_string;
	    }
	  if (DB_IS_NULL (repl_string))
	    {
	      repl_string = &dummy_string;
	    }
	}
      else
	{
	  if (QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (src_string)))
	    {
	      error_status =
		db_value_domain_init (replaced_string, DB_TYPE_VARCHAR,
				      DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	    }
	  else
	    {
	      error_status =
		db_value_domain_init (replaced_string, DB_TYPE_VARNCHAR,
				      DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	    }
	  goto exit;
	}
    }

  if (!is_char_string (srch_string) || !is_char_string (repl_string)
      || !is_char_string (src_string))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      goto exit;
    }

  if ((qstr_get_category (src_string) != qstr_get_category (srch_string))
      || (qstr_get_category (src_string) != qstr_get_category (repl_string))
      || (qstr_get_category (srch_string) != qstr_get_category (repl_string))
      || (DB_GET_STRING_CODESET (src_string)
	  != DB_GET_STRING_CODESET (srch_string))
      || (DB_GET_STRING_CODESET (src_string)
	  != DB_GET_STRING_CODESET (repl_string)))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QSTR_INCOMPATIBLE_CODE_SETS, 0);
      goto exit;
    }

  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (src_string),
		       DB_GET_STRING_COLLATION (srch_string), coll_id_tmp);
  if (coll_id_tmp == -1)
    {
      error_status = ER_QSTR_INCOMPATIBLE_COLLATIONS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  LANG_RT_COMMON_COLL (coll_id_tmp, DB_GET_STRING_COLLATION (repl_string),
		       coll_id);

  if (coll_id == -1)
    {
      error_status = ER_QSTR_INCOMPATIBLE_COLLATIONS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  result_type = QSTR_IS_NATIONAL_CHAR (DB_VALUE_DOMAIN_TYPE (src_string)) ?
    DB_TYPE_VARNCHAR : DB_TYPE_VARCHAR;

  error_status = qstr_replace ((unsigned char *) DB_PULL_STRING (src_string),
			       DB_GET_STRING_LENGTH (src_string),
			       DB_GET_STRING_SIZE (src_string),
			       (INTL_CODESET)
			       DB_GET_STRING_CODESET (src_string),
			       coll_id,
			       (unsigned char *) DB_PULL_STRING (srch_string),
			       DB_GET_STRING_SIZE (srch_string),
			       (unsigned char *) DB_PULL_STRING (repl_string),
			       DB_GET_STRING_SIZE (repl_string),
			       &result_ptr, &result_length, &result_size);

  if (error_status == NO_ERROR && result_ptr != NULL)
    {
      if (result_length == 0)
	{
	  qstr_make_typed_string (result_type,
				  replaced_string,
				  (DB_GET_STRING_LENGTH (src_string) == 0) ?
				  1 : DB_GET_STRING_LENGTH (src_string),
				  (char *) result_ptr, result_size,
				  DB_GET_STRING_CODESET (src_string),
				  coll_id);
	}
      else
	{
	  qstr_make_typed_string (result_type,
				  replaced_string,
				  result_length,
				  (char *) result_ptr, result_size,
				  DB_GET_STRING_CODESET (src_string),
				  coll_id);
	}
      result_ptr[result_size] = 0;
      replaced_string->need_clear = true;
    }

exit:
  pr_clear_value (&dummy_string);

  return error_status;
}

/* qstr_replace () -
 */
static int
qstr_replace (unsigned char *src_buf, int src_len, int src_size,
	      INTL_CODESET codeset, int coll_id,
	      unsigned char *srch_str_buf, int srch_str_size,
	      unsigned char *repl_str_buf, int repl_str_size,
	      unsigned char **result_buf, int *result_len, int *result_size)
{
#define REPL_POS_ARRAY_EXTENT 32

  int error_status = NO_ERROR;
  int char_size, i;
  unsigned char *matched_ptr, *matched_ptr_end, *target;
  int *repl_pos_array = NULL;
  int repl_pos_array_size;
  int repl_pos_array_cnt;
  unsigned char *src_ptr;
  int repl_str_len;

  assert (result_buf != NULL);

  *result_buf = NULL;

  /*
   * if search string is NULL or is longer than source string
   * copy source string as a result
   */
  if (srch_str_buf == NULL || src_size < srch_str_size)
    {
      *result_buf =
	(unsigned char *) db_private_alloc (NULL, (size_t) src_size + 1);
      if (*result_buf == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1,
		  src_size);
	  goto exit;
	}

      (void) memcpy ((char *) (*result_buf), (char *) src_buf, src_size);
      *result_len = src_len;
      *result_size = src_size;
      goto exit;
    }

  if (repl_str_buf == NULL)
    {
      repl_str_buf = (unsigned char *) "";
    }

  repl_pos_array_size = REPL_POS_ARRAY_EXTENT;
  repl_pos_array = (int *) db_private_alloc (NULL, 2 * sizeof (int)
					     * repl_pos_array_size);
  if (repl_pos_array == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1,
	      2 * sizeof (int) * repl_pos_array_size);
      goto exit;
    }

  intl_char_count (repl_str_buf, repl_str_size, codeset, &repl_str_len);

  repl_pos_array_cnt = 0;
  for (*result_size = 0, *result_len = 0, src_ptr = src_buf;
       src_size > 0 && srch_str_size > 0 && src_ptr < src_buf + src_size;)
    {
      int matched_size;

      if (QSTR_MATCH (coll_id, src_ptr, src_buf + src_size - src_ptr,
		      srch_str_buf, srch_str_size, NULL, false,
		      &matched_size) == 0)
	{
	  /* store byte position and size of matched string */
	  if (repl_pos_array_cnt >= repl_pos_array_size)
	    {
	      repl_pos_array_size += REPL_POS_ARRAY_EXTENT;
	      repl_pos_array =
		(int *) db_private_realloc (NULL, repl_pos_array,
					    2 * sizeof (int)
					    * repl_pos_array_size);
	      if (repl_pos_array == NULL)
		{
		  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1,
			  2 * sizeof (int) * repl_pos_array_size);
		  goto exit;
		}
	    }
	  repl_pos_array[repl_pos_array_cnt * 2] = src_ptr - src_buf;
	  repl_pos_array[repl_pos_array_cnt * 2 + 1] = matched_size;
	  src_ptr += matched_size;
	  repl_pos_array_cnt++;
	  *result_size += repl_str_size;
	  *result_len += repl_str_len;
	}
      else
	{
	  INTL_NEXT_CHAR (src_ptr, src_ptr, codeset, &char_size);
	  *result_size += char_size;
	  *result_len += 1;
	}
    }

  if (repl_pos_array_cnt == 0)
    {
      *result_size = src_size;
    }

  *result_buf = (unsigned char *) db_private_alloc (NULL,
						    (size_t) (*result_size) +
						    1);
  if (*result_buf == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1,
	      *result_size + 1);
      goto exit;
    }

  matched_ptr = matched_ptr_end = src_buf;
  target = *result_buf;
  for (i = 0; i < repl_pos_array_cnt; i++)
    {
      /* first, copy non matched original string preceeding matched part */
      matched_ptr = src_buf + repl_pos_array[2 * i];
      if ((matched_ptr - matched_ptr_end) > 0)
	{
	  (void) memcpy (target, matched_ptr_end,
			 matched_ptr - matched_ptr_end);
	  target += matched_ptr - matched_ptr_end;
	}

      /* second, copy replacing string */
      (void) memcpy (target, repl_str_buf, repl_str_size);
      target += repl_str_size;
      matched_ptr_end = matched_ptr + repl_pos_array[2 * i + 1];
    }

  /* append any trailing string (after last matched part) */
  if (matched_ptr_end < src_buf + src_size)
    {
      (void) memcpy (target, matched_ptr_end,
		     src_buf + src_size - matched_ptr_end);
      target += src_buf + src_size - matched_ptr_end;
    }

  assert (target - *result_buf == *result_size);

exit:
  if (repl_pos_array != NULL)
    {
      db_private_free (NULL, repl_pos_array);
    }

  return error_status;

#undef REPL_POS_ARRAY_EXTENT
}

/*
 * db_string_translate () -
 */
int
db_string_translate (const DB_VALUE * src_string,
		     const DB_VALUE * from_string, const DB_VALUE * to_string,
		     DB_VALUE * transed_string)
{
  int error_status = NO_ERROR;
  int from_string_is_null = false;
  int to_string_is_null = false;

  unsigned char *result_ptr = NULL;
  int result_length = 0, result_size = 0;
  DB_TYPE result_type = DB_TYPE_NULL;

  assert (src_string != (DB_VALUE *) NULL);
  assert (transed_string != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src_string) || DB_IS_NULL (from_string)
      || DB_IS_NULL (to_string))
    {
      if (QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (src_string)))
	{
	  error_status =
	    db_value_domain_init (transed_string, DB_TYPE_VARCHAR,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else
	{
	  error_status =
	    db_value_domain_init (transed_string, DB_TYPE_VARNCHAR,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      return error_status;
    }

  if (!is_char_string (from_string) || !is_char_string (to_string)
      || !is_char_string (src_string))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if ((qstr_get_category (src_string) != qstr_get_category (from_string))
      || (qstr_get_category (src_string) != qstr_get_category (to_string))
      || (qstr_get_category (from_string) != qstr_get_category (to_string))
      || (DB_GET_STRING_CODESET (src_string)
	  != DB_GET_STRING_CODESET (from_string))
      || (DB_GET_STRING_CODESET (src_string)
	  != DB_GET_STRING_CODESET (to_string)))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  error_status =
    qstr_translate ((unsigned char *) DB_PULL_STRING (src_string),
		    DB_VALUE_DOMAIN_TYPE (src_string),
		    DB_GET_STRING_SIZE (src_string),
		    (INTL_CODESET) DB_GET_STRING_CODESET (src_string),
		    (unsigned char *) DB_PULL_STRING (from_string),
		    DB_GET_STRING_SIZE (from_string),
		    (unsigned char *) DB_PULL_STRING (to_string),
		    DB_GET_STRING_SIZE (to_string),
		    &result_ptr, &result_type, &result_length, &result_size);

  if (error_status == NO_ERROR && result_ptr != NULL)
    {
      if (result_length == 0)
	{
	  qstr_make_typed_string (result_type,
				  transed_string,
				  (DB_GET_STRING_LENGTH (src_string) == 0) ?
				  1 : DB_GET_STRING_LENGTH (src_string),
				  (char *) result_ptr, result_size,
				  DB_GET_STRING_CODESET (src_string),
				  DB_GET_STRING_COLLATION (src_string));
	}
      else
	{
	  qstr_make_typed_string (result_type,
				  transed_string,
				  result_length,
				  (char *) result_ptr, result_size,
				  DB_GET_STRING_CODESET (src_string),
				  DB_GET_STRING_COLLATION (src_string));
	}
      result_ptr[result_size] = 0;
      transed_string->need_clear = true;
    }

  return error_status;
}

/*
 * qstr_translate () -
 */
static int
qstr_translate (unsigned char *src_ptr, DB_TYPE src_type, int src_size,
		INTL_CODESET codeset,
		unsigned char *from_str_ptr, int from_str_size,
		unsigned char *to_str_ptr, int to_str_size,
		unsigned char **result_ptr,
		DB_TYPE * result_type, int *result_len, int *result_size)
{
  int error_status = NO_ERROR;
  int j, offset, offset1, offset2;
  int from_char_loc, to_char_cnt, to_char_loc;
  unsigned char *srcp, *fromp, *target = NULL;
  int matched = 0, phase = 0;

  if ((from_str_ptr == NULL && to_str_ptr != NULL))
    {
      error_status = ER_QPROC_INVALID_PARAMETER;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (to_str_ptr == NULL)
    {
      to_str_ptr = (unsigned char *) "";
    }

  /* check from, to string */
  to_char_cnt = 0;
  for (j = 0; j < to_str_size;)
    {
      intl_char_size (to_str_ptr + j, 1, codeset, &offset2);
      j += offset2;
      to_char_cnt++;
    }

  /* calculate total length */
  *result_size = 0;
  phase = 0;

loop:
  srcp = src_ptr;
  for (srcp = src_ptr; srcp < src_ptr + src_size;)
    {
      intl_char_size (srcp, 1, codeset, &offset);

      matched = 0;
      from_char_loc = 0;
      for (fromp = from_str_ptr;
	   fromp != NULL && fromp < from_str_ptr + from_str_size;
	   from_char_loc++)
	{
	  intl_char_size (fromp, 1, codeset, &offset1);

	  /* if source and from char are matched, translate */
	  if ((offset == offset1) && (memcmp (srcp, fromp, offset) == 0))
	    {
	      matched = 1;
	      to_char_loc = 0;
	      for (j = 0; j < to_str_size;)
		{
		  intl_char_size (to_str_ptr + j, 1, codeset, &offset2);

		  if (to_char_loc == from_char_loc)
		    {		/* if matched char exist, replace */
		      if (phase == 0)
			{
			  *result_size += offset2;
			}
		      else
			{
			  memcpy (target, to_str_ptr + j, offset2);
			  target += offset2;
			}
		      break;
		    }
		  j += offset2;
		  to_char_loc++;
		}
	      break;
	    }
	  fromp += offset1;
	}
      if (!matched)
	{			/* preserve source char */
	  if (phase == 0)
	    {
	      *result_size += offset;
	    }
	  else
	    {
	      memcpy (target, srcp, offset);
	      target += offset;
	    }
	}
      srcp += offset;
    }

  if (phase == 1)
    {
      return error_status;
    }

  /* evaluate result string length */
  *result_type = QSTR_IS_NATIONAL_CHAR (src_type) ?
    DB_TYPE_VARNCHAR : DB_TYPE_VARCHAR;
  *result_ptr = (unsigned char *)
    db_private_alloc (NULL, (size_t) * result_size + 1);
  if (*result_ptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
      return error_status;
    }
  if (phase == 0)
    {
      phase = 1;
      target = *result_ptr;
      *result_len = *result_size;
      goto loop;
    }

  return error_status;
}

/*
 * db_bit_string_coerce () -
 *
 * Arguments:
 *        src_string:  (In) Source string
 *       dest_string: (Out) Coerced string
 *       data_status: (Out) Data status
 *
 * Returns: int
 *
 * Errors:
 *   ER_QSTR_INVALID_DATA_TYPE
 *      <src_string> is not a bit string
 *   ER_QSTR_INCOMPATIBLE_CODE_SETS
 *      <dest_domain> is not a compatible domain type
 *
 * Note:
 *
 *   This function coerces a bit string from one domain to another.
 *   A new DB_VALUE is created making use of the memory manager and
 *   domain information stored in <dest_value>, and coercing the
 *   data portion of <src_string>.
 *
 *   If any loss of data due to truncation occurs, <data_status>
 *   is set to DATA_STATUS_TRUNCATED.
 *
 *   The destination container should have the memory manager, precision
 *   and domain type initialized.
 *
 * Assert:
 *
 *   1. src_string  != (DB_VALUE *) NULL
 *   2. dest_value  != (DB_VALUE *) NULL
 *   3. data_status != (DB_DATA_STATUS *) NULL
 *
 */

int
db_bit_string_coerce (const DB_VALUE * src_string,
		      DB_VALUE * dest_string, DB_DATA_STATUS * data_status)
{
  DB_TYPE src_type, dest_type;

  int error_status = NO_ERROR;

  /* Assert that DB_VALUE structures have been allocated. */
  assert (src_string != (DB_VALUE *) NULL);
  assert (dest_string != (DB_VALUE *) NULL);
  assert (data_status != (DB_DATA_STATUS *) NULL);

  /* Initialize status value */
  *data_status = DATA_STATUS_OK;

  /* Categorize the two input parameters and check for errors.
     Verify that the parameters are both character strings. */
  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  dest_type = DB_VALUE_DOMAIN_TYPE (dest_string);

  if (!QSTR_IS_BIT (src_type) || !QSTR_IS_BIT (dest_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if (qstr_get_category (src_string) != qstr_get_category (dest_string))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if (DB_IS_NULL (src_string))
    {
      db_value_domain_init (dest_string, DB_VALUE_DOMAIN_TYPE (dest_string),
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      unsigned char *dest;
      int dest_prec;
      int dest_length;

      if (DB_VALUE_PRECISION (dest_string) == TP_FLOATING_PRECISION_VALUE)
	{
	  dest_prec = DB_GET_STRING_LENGTH (src_string);
	}
      else
	{
	  dest_prec = DB_VALUE_PRECISION (dest_string);
	}

      error_status =
	qstr_bit_coerce ((unsigned char *) DB_PULL_STRING (src_string),
			 DB_GET_STRING_LENGTH (src_string),
			 QSTR_VALUE_PRECISION (src_string),
			 src_type, &dest, &dest_length, dest_prec,
			 dest_type, data_status);

      if (error_status == NO_ERROR)
	{
	  qstr_make_typed_string (dest_type,
				  dest_string,
				  DB_VALUE_PRECISION (dest_string),
				  (char *) dest, dest_length,
				  INTL_CODESET_RAW_BITS, 0);
	  dest_string->need_clear = true;
	}
    }

  return error_status;
}

/*
 * db_char_string_coerce () -
 *
 * Arguments:
 *        src_string:  (In) Source string
 *       dest_string: (Out) Coerced string
 *       data_status: (Out) Data status
 *
 * Returns: int
 *
 * Errors:
 *   ER_QSTR_INVALID_DATA_TYPE
 *      <src_string> and <dest_string> are not both char strings
 *   ER_QSTR_INCOMPATIBLE_CODE_SETS
 *      <dest_domain> is not a compatible domain type
 *
 * Note:
 *
 *   This function coerces a char string from one domain to
 *   another.  A new DB_VALUE is created making use of the
 *   memory manager and domain information stored in
 *   <dest_value>, and coercing the data portion of
 *   <src_string>.
 *
 *   If any loss of data due to truncation occurs, <data_status>
 *   is set to DATA_STATUS_TRUNCATED.
 *
 * Assert:
 *
 *   1. src_string  != (DB_VALUE *) NULL
 *   2. dest_value  != (DB_VALUE *) NULL
 *   3. data_status != (DB_DATA_STATUS *) NULL
 *
 */

int
db_char_string_coerce (const DB_VALUE * src_string,
		       DB_VALUE * dest_string, DB_DATA_STATUS * data_status)
{
  int error_status = NO_ERROR;

  /* Assert that DB_VALUE structures have been allocated. */
  assert (src_string != (DB_VALUE *) NULL);
  assert (dest_string != (DB_VALUE *) NULL);
  assert (data_status != (DB_DATA_STATUS *) NULL);

  /* Initialize status value */
  *data_status = DATA_STATUS_OK;

  /*
   *  Categorize the two input parameters and check for errors.
   *    Verify that the parameters are both character strings.
   *    Verify that the source and destination strings are of
   *    the same character code set.
   *    Verify that the source string is not NULL.
   *    Otherwise, coerce.
   */
  if (!is_char_string (src_string) || !is_char_string (dest_string))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if (qstr_get_category (src_string) != qstr_get_category (dest_string))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  else if (DB_IS_NULL (src_string))
    {
      db_value_domain_init (dest_string, DB_VALUE_DOMAIN_TYPE (dest_string),
			    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      unsigned char *dest;
      int dest_prec;
      int dest_length;
      int dest_size;
      INTL_CODESET src_codeset = DB_GET_STRING_CODESET (src_string);
      INTL_CODESET dest_codeset = DB_GET_STRING_CODESET (dest_string);

      if (!INTL_CAN_COERCE_CS (src_codeset, dest_codeset))
	{
	  error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  return error_status;
	}

      /* Initialize the memory manager of the destination */
      if (DB_VALUE_PRECISION (dest_string) == TP_FLOATING_PRECISION_VALUE)
	{
	  dest_prec = DB_GET_STRING_LENGTH (src_string);
	}
      else
	{
	  dest_prec = DB_VALUE_PRECISION (dest_string);
	}

      error_status =
	qstr_coerce ((unsigned char *) DB_PULL_STRING (src_string),
		     DB_GET_STRING_LENGTH (src_string),
		     QSTR_VALUE_PRECISION (src_string),
		     DB_VALUE_DOMAIN_TYPE (src_string),
		     src_codeset, dest_codeset,
		     &dest, &dest_length, &dest_size, dest_prec,
		     DB_VALUE_DOMAIN_TYPE (dest_string), data_status);

      if (error_status == NO_ERROR && dest != NULL)
	{
	  qstr_make_typed_string (DB_VALUE_DOMAIN_TYPE (dest_string),
				  dest_string,
				  DB_VALUE_PRECISION (dest_string),
				  (char *) dest, dest_size,
				  DB_GET_STRING_CODESET (dest_string),
				  DB_GET_STRING_COLLATION (dest_string));
	  dest[dest_size] = 0;
	  dest_string->need_clear = true;
	}
    }

  return error_status;
}

/*
 * db_string_make_empty_typed_string() -
 *
 * Arguments:
 *	 thread_p   : (In) thread context (may be NULL)
 *       db_val	    : (In/Out) value to make
 *       db_type    : (In) Type of string (char,nchar,bit)
 *       precision  : (In)
 *       codeset    : (In)
 *       collation_id  : (In)
 *
 * Returns: int
 *
 * Errors:
 *   ER_QSTR_INVALID_DATA_TYPE
 *      <type> is not one of (char,nchar,bit)
 *   ER_OUT_OF_VIRTUAL_MEMORY
 *      out of memory
 *
 */

int
db_string_make_empty_typed_string (THREAD_ENTRY * thread_p, DB_VALUE * db_val,
				   const DB_TYPE db_type, int precision,
				   int codeset, int collation_id)
{
  int status = NO_ERROR;
  char *buf = NULL;

  /* handle bad cases */
  assert (db_val != NULL);
  assert (precision >= DB_DEFAULT_PRECISION);

  if (db_type != DB_TYPE_BIT && db_type != DB_TYPE_VARBIT
      && db_type != DB_TYPE_CHAR && db_type != DB_TYPE_VARCHAR
      && db_type != DB_TYPE_NCHAR && db_type != DB_TYPE_VARNCHAR)
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  if (db_val == NULL)
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  if (DB_IS_NULL (db_val))
    {
      db_value_domain_init (db_val, db_type, precision, 0);
    }
  precision = ((precision < DB_DEFAULT_PRECISION)
	       ? DB_DEFAULT_PRECISION : precision);

  /* create an empty string DB VALUE */
  buf = (char *) db_private_alloc (thread_p, 1);
  if (buf == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  *buf = '\0';

  qstr_make_typed_string (db_type, db_val, precision, buf, 0, codeset,
			  collation_id);
  db_val->need_clear = true;

  return status;
}

/*
 * db_find_string_in_in_set () - find the position of a string token in
 *				 a string containing comma separated tokens
 * return : error code or NO_ERROR
 * needle (in)	: the token to look for
 * stack (in)	: the set of tokens
 * result (in/out) : will hold the position of the token
 */
int
db_find_string_in_in_set (const DB_VALUE * needle, const DB_VALUE * stack,
			  DB_VALUE * result)
{
  int err = NO_ERROR;
  DB_TYPE needle_type, stack_type;
  int position = 1;
  int stack_size = 0, needle_size = 0;
  const char *stack_str = NULL;
  const char *needle_str = NULL;
  int cmp, coll_id, matched_stack_size;
  const char *stack_ptr, *elem_start;

  if (DB_IS_NULL (needle) || DB_IS_NULL (stack))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /*
   *  Categorize the parameters into respective code sets.
   *  Verify that the parameters are both character strings.
   *  Verify that the input strings belong to compatible code sets.
   */
  needle_type = DB_VALUE_DOMAIN_TYPE (needle);
  stack_type = DB_VALUE_DOMAIN_TYPE (stack);

  if (!QSTR_IS_ANY_CHAR_OR_BIT (needle_type)
      || !QSTR_IS_ANY_CHAR_OR_BIT (stack_type))
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      err = ER_QSTR_INVALID_DATA_TYPE;
      goto error_return;
    }

  if (qstr_get_category (needle) != qstr_get_category (stack)
      || DB_GET_STRING_CODESET (needle) != DB_GET_STRING_CODESET (stack))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QSTR_INCOMPATIBLE_CODE_SETS, 0);
      err = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      goto error_return;
    }

  LANG_RT_COMMON_COLL (DB_GET_STRING_COLLATION (stack),
		       DB_GET_STRING_COLLATION (needle), coll_id);
  if (coll_id == -1)
    {
      err = ER_QSTR_INCOMPATIBLE_COLLATIONS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto error_return;
    }

  stack_str = DB_PULL_STRING (stack);
  stack_size = DB_GET_STRING_SIZE (stack);
  needle_str = DB_PULL_STRING (needle);
  needle_size = DB_GET_STRING_SIZE (needle);

  if (stack_size == 0 && needle_size == 0)
    {
      /* if both are empty string, no match */
      goto match_not_found;
    }

  elem_start = stack_ptr = stack_str;

  for (;;)
    {
      if (*stack_ptr == ',' || stack_ptr >= stack_str + stack_size)
	{
	  assert (stack_ptr <= stack_str + stack_size);

	  if (stack_ptr == elem_start)
	    {
	      if (needle_size == 0)
		{
		  DB_MAKE_INT (result, position);
		  return NO_ERROR;
		}
	    }
	  else
	    {
	      assert (stack_ptr > elem_start);
	      /* check using collation */
	      if (needle_size > 0)
		{
		  cmp =
		    QSTR_MATCH (coll_id, (const unsigned char *) elem_start,
				stack_ptr - elem_start,
				(const unsigned char *) needle_str,
				needle_size, false, false,
				&matched_stack_size);
		  if (cmp == 0
		      && matched_stack_size == stack_ptr - elem_start)
		    {
		      DB_MAKE_INT (result, position);
		      return NO_ERROR;
		    }
		}
	    }

	  if (stack_ptr >= stack_str + stack_size)
	    {
	      break;
	    }

	  position++;
	  elem_start = ++stack_ptr;
	}
      else
	{
	  stack_ptr++;
	}
    }

match_not_found:
  /* if we didn't find it in the loop above, then there is no match */
  DB_MAKE_INTEGER (result, 0);
  return NO_ERROR;

error_return:
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      er_clear ();
      return NO_ERROR;
    }
  return err;
}

/*
 * db_bigint_to_binary_string () - compute the string representation of a
 *				   binary a value
 * return : error code or NO_ERROR
 * src_bigint (in)  : the binary value
 * result (out)	    : the string representation of the binary value
 */
int
db_bigint_to_binary_string (const DB_VALUE * src_bigint, DB_VALUE * result)
{
  int error = NO_ERROR;
  int i = 0;
  DB_BIGINT bigint_val = 0;
  int digits_count = 0;
  char *binary_form = NULL;

  if (DB_IS_NULL (src_bigint))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (DB_VALUE_TYPE (src_bigint) != DB_TYPE_BIGINT)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      error = ER_QSTR_INVALID_DATA_TYPE;
      goto error_return;
    }

  bigint_val = DB_GET_BIGINT (src_bigint);

  /* count the number of digits in bigint_val */
  if (bigint_val < (DB_BIGINT) 0)
    {
      /* MSB is the sign bit */
      digits_count = sizeof (DB_BIGINT) * 8;
    }
  else if (bigint_val == 0)
    {
      digits_count = 1;
    }
  else
    {
      i = 0;
      /* positive numbers have at most 8 * sizeof(DB_BIGINT) - 1 digits */
      while ((DB_BIGINT) 1 << i <= bigint_val
	     && i < (int) sizeof (DB_BIGINT) * 8 - 1)
	{
	  i++;
	}
      digits_count = i;
    }

  binary_form = (char *) db_private_alloc (NULL, digits_count + 1);
  if (binary_form == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_return;
    }
  memset (binary_form, 0, digits_count + 1);

  for (i = 0; i < digits_count; i++)
    {
      binary_form[digits_count - i - 1] =
	((DB_BIGINT) 1 << i) & bigint_val ? '1' : '0';
    }

  DB_MAKE_VARCHAR (result, digits_count, binary_form, digits_count,
		   LANG_COERCIBLE_CODESET, LANG_COERCIBLE_COLL);
  result->need_clear = true;
  return error;

error_return:
  if (binary_form != NULL)
    {
      db_private_free (NULL, binary_form);
    }
  pr_clear_value (result);
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      er_clear ();
      return NO_ERROR;
    }
  return error;
}

/*
 * db_add_time () - add the time represented by right to the value left
 * return : error code or NO_ERROR
 * left (in)	: left operand
 * right (in)	: right operand
 * result (out) : result
 * domain (in)	: the domain of the return type
 */
int
db_add_time (const DB_VALUE * left, const DB_VALUE * right, DB_VALUE * result,
	     const TP_DOMAIN * domain)
{
  int error = NO_ERROR;
  DB_DATETIME ldatetime = { 0, 0 };
  DB_DATETIME result_datetime = { 0, 0 };
  bool left_is_datetime = false;
  int month = 0, day = 0, year = 0;
  int lsecond = 0, lminute = 0, lhour = 0, lms = 0;
  bool is_time_decoded = false;
  bool is_datetime_decoded = false;
  int rsecond = 0, rminute = 0, rhour = 0, rms = 0;
  DB_TIME ltime = 0, rtime = 0;
  char *res_s = NULL;
  DB_TYPE result_type = DB_TYPE_NULL;
  INTL_CODESET codeset;
  int collation_id;

  if (DB_IS_NULL (left) || DB_IS_NULL (right))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  switch (DB_VALUE_TYPE (left))
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      {
	bool is_explicit_time = false;
	error =
	  db_date_parse_time (DB_PULL_STRING (left),
			      DB_GET_STRING_SIZE (left), &ltime, &lms);
	if (error != NO_ERROR)
	  {
	    /* left may be a date string, try it here */
	    error =
	      db_date_parse_datetime_parts (DB_PULL_STRING (left),
					    DB_GET_STRING_SIZE (left),
					    &ldatetime, &is_explicit_time,
					    NULL, NULL, NULL);
	    if (error != NO_ERROR || is_explicit_time)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION,
			0);
		error = ER_TIME_CONVERSION;
		goto error_return;
	      }

	    left_is_datetime = true;
	    db_date_decode (&ldatetime.date, &month, &day, &year);
	    is_datetime_decoded = true;
	    result_type = DB_TYPE_VARCHAR;
	    break;
	  }

	db_time_decode (&ltime, &lhour, &lminute, &lsecond);
	is_time_decoded = true;

	error =
	  db_date_parse_datetime_parts (DB_PULL_STRING (left),
					DB_GET_STRING_SIZE (left),
					&ldatetime, &is_explicit_time, NULL,
					NULL, NULL);
	if (error != NO_ERROR || !is_explicit_time)
	  {
	    left_is_datetime = false;
	  }
	else
	  {
	    int lsecond2 = 0, lminute2 = 0, lhour2 = 0, lms2 = 0;
	    left_is_datetime = true;
	    db_datetime_decode (&ldatetime, &month, &day, &year, &lhour2,
				&lminute2, &lsecond2, &lms2);
	    is_datetime_decoded = true;
	    if (lhour2 != lhour || lminute2 != lminute ||
		lsecond2 != lsecond || lms2 != lms)
	      {
		month = 0;
		day = 0;
		year = 0;
		left_is_datetime = false;
	      }
	  }
	result_type = DB_TYPE_VARCHAR;
	break;
      }

    case DB_TYPE_DATETIME:
      ldatetime = *(DB_GET_DATETIME (left));
      left_is_datetime = true;
      result_type = DB_TYPE_DATETIME;
      break;

    case DB_TYPE_TIMESTAMP:
      db_timestamp_to_datetime (DB_GET_TIMESTAMP (left), &ldatetime);
      left_is_datetime = true;
      result_type = DB_TYPE_DATETIME;
      break;

    case DB_TYPE_DATE:
      ldatetime.date = *(DB_GET_DATE (left));
      left_is_datetime = true;
      result_type = DB_TYPE_DATETIME;
      break;

    case DB_TYPE_TIME:
      ltime = *(DB_GET_TIME (left));
      left_is_datetime = false;
      result_type = DB_TYPE_TIME;
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      error = ER_QSTR_INVALID_DATA_TYPE;
      goto error_return;
      break;
    }

  if (db_get_time_from_dbvalue (right, &rhour, &rminute, &rsecond, &rms)
      != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      error = ER_QSTR_INVALID_DATA_TYPE;
      goto error_return;
    }

  if (left_is_datetime)
    {
      /* add a datetime to a time */
      if (!is_datetime_decoded)
	{
	  db_datetime_decode (&ldatetime, &month, &day, &year, &lhour,
			      &lminute, &lsecond, &lms);
	}

      if (month == 0 && day == 0 && year == 0
	  && lhour == 0 && lminute == 0 && lsecond == 0 && lms == 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_ATTEMPT_TO_USE_ZERODATE, 0);
	  error = ER_ATTEMPT_TO_USE_ZERODATE;
	  goto error_return;
	}

      error =
	add_and_normalize_date_time (&year, &month, &day, &lhour, &lminute,
				     &lsecond, &lms, 0, 0, 0, rhour, rminute,
				     rsecond, 0);
      if (error != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	  error = ER_DATE_CONVERSION;
	  goto error_return;
	}
      db_datetime_encode (&result_datetime, month, day, year, lhour, lminute,
			  lsecond, lms);
    }
  else
    {
      /* add two time values */
      int seconds = 0;
      if (!is_time_decoded)
	{
	  db_time_decode (&ltime, &lhour, &lminute, &lsecond);
	}
      seconds =
	(lhour + rhour) * 3600 + (lminute + rminute) * 60 + lsecond + rsecond;
      rhour = seconds / 3600;
      if (rhour > 23)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
	  error = ER_TIME_CONVERSION;
	  goto error_return;
	}
      rminute = (seconds - rhour * 3600) / 60;
      rsecond = seconds % 60;
    }

  /* depending on the first argument, the result is either result_date or
     result_time */

  if (domain != NULL)
    {
      assert (TP_DOMAIN_TYPE (domain) == result_type);
    }

  switch (result_type)
    {
    case DB_TYPE_DATETIME:
      if (!left_is_datetime)
	{
	  /* the result type can be DATETIME only if the first argument
	     is a DATE or a DATETIME */
	  assert (false);
	  DB_MAKE_NULL (result);
	}
      DB_MAKE_DATETIME (result, &result_datetime);
      break;

    case DB_TYPE_TIME:
      if (left_is_datetime)
	{
	  /* the result type can be DATETIME only if the first argument
	     is a TIME */
	  assert (false);
	  DB_MAKE_NULL (result);
	}
      DB_MAKE_TIME (result, rhour, rminute, rsecond);
      break;

    case DB_TYPE_VARCHAR:
      codeset = TP_DOMAIN_CODESET (domain);
      collation_id = TP_DOMAIN_COLLATION (domain);

      if (left_is_datetime)
	{
	  res_s = (char *) db_private_alloc (NULL, QSTR_DATETIME_LENGTH + 1);
	  if (res_s == NULL)
	    {
	      error = ER_DATE_CONVERSION;
	      goto error_return;
	    }

	  db_datetime_to_string (res_s, QSTR_DATETIME_LENGTH + 1,
				 &result_datetime);
	  DB_MAKE_VARCHAR (result, strlen (res_s), res_s, strlen (res_s),
			   codeset, collation_id);
	}
      else
	{
	  res_s = (char *) db_private_alloc (NULL, QSTR_TIME_LENGTH + 1);
	  if (res_s == NULL)
	    {
	      error = ER_TIME_CONVERSION;
	      goto error_return;
	    }
	  db_time_encode (&rtime, rhour, rminute, rsecond);
	  db_time_to_string (res_s, QSTR_TIME_LENGTH + 1, &rtime);
	  DB_MAKE_VARCHAR (result, strlen (res_s), res_s, strlen (res_s),
			   codeset, collation_id);
	}
      result->need_clear = true;
      break;
    default:
      assert (false);
      DB_MAKE_NULL (result);
      break;
    }

  return NO_ERROR;

error_return:
  if (res_s != NULL)
    {
      db_private_free (NULL, res_s);
    }
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      /* clear error and return NULL */
      er_clear ();
      return NO_ERROR;
    }
  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * db_string_convert () -
 *
 * Arguments:
 *        src_string:  (In) Source string
 *       dest_string: (Out) Converted string
 *       data_status: (Out) Data status
 *
 * Returns: int
 *
 * Errors:
 *   ER_QSTR_INVALID_DATA_TYPE
 *      <src_string> and <dest_string> are not both national char strings
 *   ER_QSTR_INCOMPATIBLE_CODE_SETS
 *      Conversion not supported between code sets of <src_string>
 *      and <dest_string>
 *
 * Note:
 *   This function converts a national character string from one
 *   set encoding to another.
 *
 *   A new DB_VALUE is created making use of the code set and
 *   memory manager stored in <dest_string>, and converting
 *   the characters in the data portion of <src_string>.
 *
 *   If the source string is fixed-length, the destination will be
 *   fixed-length with pad characters.  If the source string is
 *   variable-length, the result will also be variable length.
 *
 * Assert:
 *
 *   1. src_string  != (DB_VALUE *) NULL
 *   2. dest_value  != (DB_VALUE *) NULL
 *   3. data_status != (DB_DATA_STATUS *) NULL
 *
 */

int
db_string_convert (const DB_VALUE * src_string, DB_VALUE * dest_string)
{
  DB_TYPE src_type, dest_type;
  int error_status = NO_ERROR;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_string != (DB_VALUE *) NULL);
  assert (dest_string != (DB_VALUE *) NULL);

  /*
   *  Categorize the two input parameters and check for errors.
   *    Verify that the parameters are both character strings.
   */
  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  dest_type = DB_VALUE_DOMAIN_TYPE (dest_string);

  if (!QSTR_IS_NATIONAL_CHAR (src_type) || !QSTR_IS_NATIONAL_CHAR (dest_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
    }

  else if (DB_IS_NULL (src_string))
    {
      db_value_domain_init (dest_string, DB_VALUE_DOMAIN_TYPE (src_string), 0,
			    0);
    }
  else
    {
      unsigned char *src, *dest;
      int src_length = 0, src_precision;
      INTL_CODESET src_codeset, dest_codeset;
      int convert_status;
      int num_unconverted, cnv_size;


      src = (unsigned char *) DB_GET_NCHAR (src_string, &src_length);
      src_precision = QSTR_VALUE_PRECISION (src_string);

      src_codeset = (INTL_CODESET) DB_GET_STRING_CODESET (src_string);
      dest_codeset = (INTL_CODESET) DB_GET_STRING_CODESET (dest_string);

      /*  Fixed-length strings */

      if (QSTR_IS_FIXED_LENGTH (src_type))
	{
	  /*  Allocate enough room for a fully padded string */
	  dest = (unsigned char *)
	    db_private_alloc (NULL, (size_t) (2 * src_precision) + 1);
	  if (dest == NULL)
	    {
	      goto mem_error;
	    }

	  /*  Convert the string codeset */
	  convert_status = intl_convert_charset (src,
						 src_length,
						 src_codeset,
						 dest,
						 dest_codeset,
						 &num_unconverted);

	  /*  Pad the result */
	  if (convert_status == NO_ERROR)
	    {
	      intl_char_size (dest,
			      (src_length - num_unconverted),
			      dest_codeset, &cnv_size);
	      qstr_pad_string ((unsigned char *) &dest[cnv_size],
			       (src_precision - src_length + num_unconverted),
			       dest_codeset);
	      dest[src_precision] = 0;
	      DB_MAKE_NCHAR (dest_string,
			     src_precision, (char *) dest, src_precision);
	      dest_string->need_clear = true;
	    }
	  else
	    {
	      db_private_free_and_init (NULL, dest);
	    }
	}

      /*  Variable-length strings */
      else
	{
	  /*  Allocate enough room for the string */
	  dest = (unsigned char *)
	    db_private_alloc (NULL, (size_t) (2 * src_length) + 1);
	  if (dest == NULL)
	    {
	      goto mem_error;
	    }

	  /* Convert the string codeset */
	  convert_status = intl_convert_charset (src,
						 src_length,
						 src_codeset,
						 dest,
						 dest_codeset,
						 &num_unconverted);

	  if (convert_status == NO_ERROR)
	    {
	      dest[src_length - num_unconverted] = 0;
	      DB_MAKE_VARNCHAR (dest_string,
				src_precision,
				(char *) dest,
				(src_length - num_unconverted));
	      dest_string->need_clear = true;
	    }
	  else
	    {
	      db_private_free_and_init (NULL, dest);
	    }
	}

      /*
       *  If intl_convert_charset() returned an error, map
       *  to an ER_QSTR_INCOMPATIBLE_CODE_SETS error.
       */
      if (convert_status != NO_ERROR)
	{
	  error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
	}
    }

  return error_status;

  /*
   *  Error handling
   */
mem_error:
  assert (er_errid () != NO_ERROR);
  error_status = er_errid ();
  return error_status;
}
#endif

/*
 * qstr_pad_string () -
 *
 * Arguments:
 *            s: (IN OUT) Pointer to input string.
 *       length: (IN)     Size of input string.
 *      codeset: (IN)     International codeset of input string.
 *
 * Returns: unsigned char
 *
 * Errors:
 *
 * Note:
 *     This is a convenience function which will copy pad characters into
 *     the input string.  It is assumed that the pad character will consist
 *     of one or two bytes (this is currently true).
 *
 *     The address immediately after the padded string is returned.  Thus,
 *     If a NULL terminated string was desired, then a call could be made:
 *
 *         ptr = qstr_pad_string();
 *         *ptr = '\0';
 *
 */

unsigned char *
qstr_pad_string (unsigned char *s, int length, INTL_CODESET codeset)
{
  unsigned char pad[2];
  int i, j, pad_size = 0;

  if (length == 0)
    {
      return s;
    }

  assert (length > 0);

  intl_pad_char (codeset, pad, &pad_size);

  if (pad_size == 1)
    {
      (void) memset ((char *) s, (int) pad[0], length);
      s = s + length;
    }
  else
    {
      for (i = 0; i < length; i++)
	{
	  for (j = 0; j < pad_size; j++)
	    {
	      *(s++) = pad[j];
	    }
	}
    }

  return s;
}

/*
 * qstr_bin_to_hex () -
 *
 * arguments:
 *        dest: Pointer to destination hex buffer area
 *   dest_size: Size of destination buffer area in bytes
 *         src: Pointer to source binary buffer area
 *    src_size: Size of source buffer area in bytes
 *
 * returns/side-effects: int
 *    The number of converted source bytes is returned.  This value will
 *    equal src_size if (dest_size >= 2*src_size) and less otherwise.
 *
 * description:
 *    Convert the binary data in the source buffer to ASCII hex characters
 *    in the destination buffer.  The destination buffer should be at
 *    least 2 * src_size.  If not, as much of the source string is processed
 *    as possible.  The number of ASCII Hex characters in dest will
 *    equal two times the returned value.
 *
 */

int
qstr_bin_to_hex (char *dest, int dest_size, const char *src, int src_size)
{
  int i, copy_size;

  if (dest_size >= (2 * src_size))
    {
      copy_size = src_size;
    }
  else
    {
      copy_size = dest_size / 2;
    }

  for (i = 0; i < copy_size; i++)
    {
      sprintf (&(dest[2 * i]), "%02x", (unsigned char) (src[i]));
    }

  return copy_size;
}

/*
 * qstr_hex_to_bin () -
 *
 * arguments:
 *        dest: Pointer to destination hex buffer area
 *   dest_size: Size of destination buffer area in bytes
 *         src: Pointer to source binary buffer area
 *    src_size: Size of source buffer area in bytes
 *
 * returns/side-effects: int
 *    The number of converted hex characters is returned.
 *
 * description:
 *    Convert the string of hex characters to decimal values.  For each two
 *    characters, one unsigned character value is produced.  If the number
 *    of characters is odd, then the second nibble of the last byte will
 *    be 0 padded.  If the destination buffer is not large enough to hold
 *    the converted data, as much data is converted as possible.
 *
 */

int
qstr_hex_to_bin (char *dest, int dest_size, char *src, int src_size)
{
  int i, copy_size, src_index, required_size;

  required_size = (src_size + 1) / 2;

  if (dest_size >= required_size)
    {
      copy_size = required_size;
    }
  else
    {
      copy_size = dest_size;
    }

  src_index = 0;
  for (i = 0; i < copy_size; i++)
    {
      int hex_digit;

      hex_digit = hextoi (src[src_index++]);
      if (hex_digit < 0)
	{
	  return -1;
	}
      else
	{
	  dest[i] = hex_digit << 4;
	  if (src_index < src_size)
	    {
	      hex_digit = hextoi (src[src_index++]);
	      if (hex_digit < 0)
		{
		  return -1;
		}
	      else
		{
		  dest[i] += hex_digit;
		}
	    }
	}
    }

  return src_index;
}

/*
 * qstr_bit_to_bin () -
 *
 * arguments:
 *        dest: Pointer to destination buffer area
 *   dest_size: Size of destination buffer area in bytes
 *         src: Pointer to source binary buffer area
 *    src_size: Size of source buffer area in bytes
 *
 * returns/side-effects: int
 *    The number of converted binary characters is returned.
 *
 * description:
 *    Convert the string of '0's and '1's to decimal values.  For each 8
 *    characters, one unsigned character value is produced.  If the number
 *    of characters is not a multiple of 8, the result will assume trailing
 *    0 padding.  If the destination buffer is not large enough to hold
 *    the converted data, as much data is converted as possible.
 *
 */

int
qstr_bit_to_bin (char *dest, int dest_size, char *src, int src_size)
{
  int dest_byte, copy_size, src_index, required_size;

  required_size = (src_size + 7) / 8;

  if (dest_size >= required_size)
    {
      copy_size = required_size;
    }
  else
    {
      copy_size = dest_size;
    }

  src_index = 0;
  for (dest_byte = 0; dest_byte < copy_size; dest_byte++)
    {
      int bit_count;

      dest[dest_byte] = 0;
      for (bit_count = 0; bit_count < 8; bit_count++)
	{
	  dest[dest_byte] = dest[dest_byte] << 1;
	  if (src_index < src_size)
	    {
	      if (src[src_index] == '1')
		{
		  dest[dest_byte]++;
		}
	      else if (src[src_index] != '0')
		{
		  return -1;	/* Illegal digit */
		}
	      src_index++;
	    }
	}
    }

  return src_index;
}

/*
 * qstr_bit_to_hex_coerce () -
 *
 * arguments:
 *      buffer: Pointer to destination buffer area
 * buffer_size: Size of destination buffer area (in bytes, *including* null
 *              terminator)
 *         src: Pointer to source buffer area
 *  src_length: Length of source buffer area in bits
 *    pad_flag: TRUE if the buffer should be padded and FALSE otherwise
 *   copy_size: Number of bytes transfered from the src string to the dst
 *              buffer
 *  truncation: pointer to a int field.  *outlen will equal 0 if no
 *              truncation occurred and will equal the size of the dst buffer
 *              in bytes needed to avoid truncation (not including the
 *              terminating NULL), otherwise.
 *
 * returns/side-effects: void
 *
 * description:
 *    Transfers at most buffer_size bytes to the region pointed at by dst.
 *    If  pad_flag is TRUE, strings shorter than buffer_size will be
 *    blank-padded out to buffer_size-1 bytes.  All strings will be
 *    null-terminated.  If truncation is necessary (i.e., if buffer_size is
 *    less than or equal to src_length), *truncation is set to src_length;
 *    if truncation is is not necessary, *truncation is set to 0.
 *
 */

void
qstr_bit_to_hex_coerce (char *buffer,
			int buffer_size,
			const char *src,
			int src_length,
			int pad_flag, int *copy_size, int *truncation)
{
  int src_size = QSTR_NUM_BYTES (src_length);

  if (src == NULL)
    {
      buffer[0] = '\0';
      return;
    }

  if (buffer_size > (2 * src_size))
    {
      /*
       * No truncation; copy the data and blank pad if necessary.
       */
      qstr_bin_to_hex (buffer, buffer_size, src, src_size);
/*
	for (i=0; i<src_size; i++)
	    sprintf(&(buffer[2*i]), "%02x", (unsigned char)(src[i]));
*/
      if (pad_flag == true)
	{
	  memset (&(buffer[2 * src_size]), '0',
		  (buffer_size - (2 * src_size)));
	  *copy_size = buffer_size - 1;
	}
      else
	{
	  *copy_size = 2 * src_size;
	}
      buffer[*copy_size] = '\0';
      *truncation = 0;
    }
  else
    {
      /*
       * Truncation is necessary; put as many bytes as possible into
       * the receiving buffer and null-terminate it (i.e., it receives
       * at most dstsize-1 bytes).  If there is not outlen indicator by
       * which we can indicate truncation, this is an error.
       *
       */
      if (buffer_size % 2)
	{
	  src_size = buffer_size / 2;
	}
      else
	{
	  src_size = (buffer_size - 1) / 2;
	}

      qstr_bin_to_hex (buffer, buffer_size, src, src_size);
/*
	for (i=0; i<src_size; i++)
	    sprintf(&(buffer[2*i]), "%02x", (unsigned char)(src[i]));
*/
      *copy_size = 2 * src_size;
      buffer[*copy_size] = '\0';

      *truncation = src_size;
    }
}

/*
 * db_get_string_length
 *
 * Arguments:
 *        value: Value  container
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *     Returns the character length of the string in the container.
 *
 */

int
db_get_string_length (const DB_VALUE * value)
{
  DB_C_CHAR str;
  int size;
  INTL_CODESET codeset;
  int length = 0;

#if 0
  /* Currently, only the medium model is used */

  switch (value->data.ch.info.style)
    {
    case SMALL_STRING:
      str = value->data.ch.small.buf;
      length = size = value->data.ch.small.size;
      codeset = value->data.ch.small.codeset;
      break;

    case MEDIUM_STRING:
      str = value->data.ch.medium.buf;
      length = size = value->data.ch.medium.size;
      codeset = value->data.ch.medium.codeset;
      break;

    case LARGE_STRING:
      str = NULL;
      size = 0;
      break;

    default:
      break;
    }
#endif

  str = value->data.ch.medium.buf;
  length = size = value->data.ch.medium.size;
  codeset = (INTL_CODESET) value->data.ch.medium.codeset;

  if (value->domain.general_info.type != DB_TYPE_BIT &&
      value->domain.general_info.type != DB_TYPE_VARBIT)
    {
      intl_char_count ((unsigned char *) str, size, codeset, &length);
    }

  return length;
}

/*
 * qstr_make_typed_string () -
 *
 * Arguments:
 *      db_type: value type for the result.
 *        value: Value container for the result.
 *    precision: Length of the string precision.
 *          src: Pointer to string.
 *       s_unit: Size of the string.
 *	codeset: codeset
 * collation_id: collation
 *
 * Returns: void
 *
 * Errors:
 *
 * Note:
 *     Make a value container from the string of the given domain.
 *     This is a convenience function which allows for all string
 *     types given the proper domain type.
 *
 */

void
qstr_make_typed_string (const DB_TYPE db_type, DB_VALUE * value,
			const int precision, const DB_C_CHAR src,
			const int s_unit, const int codeset,
			const int collation_id)
{
  switch (db_type)
    {
    case DB_TYPE_CHAR:
      DB_MAKE_CHAR (value, precision, src, s_unit, codeset, collation_id);
      break;

    case DB_TYPE_VARCHAR:
      DB_MAKE_VARCHAR (value, precision, src, s_unit, codeset, collation_id);
      break;

    case DB_TYPE_NCHAR:
      DB_MAKE_NCHAR (value, precision, src, s_unit, codeset, collation_id);
      break;

    case DB_TYPE_VARNCHAR:
      DB_MAKE_VARNCHAR (value, precision, src, s_unit, codeset, collation_id);
      break;

    case DB_TYPE_BIT:
      DB_MAKE_BIT (value, precision, src, s_unit);
      break;

    case DB_TYPE_VARBIT:
      DB_MAKE_VARBIT (value, precision, src, s_unit);
      break;

    default:
      assert (false);
      DB_MAKE_NULL (value);
      break;
    }
}

/*
 *  Private Functions
 */

/*
 * qstr_get_category
 *
 * Arguments:
 *      s: DB_VALUE representation of a string.
 *
 * Returns: QSTR_CATEGORY
 *
 * Errors:
 *
 * Note:
 *   Returns the character code set of the string "s."  The character code
 *   set of strings is:
 *
 *       QSTR_CHAR, QSTR_NATIONAL_CHAR, QSTR_BIT
 *
 *   as defined in type QSTR_CATEGORY.  A value of QSTR_UNKNOWN is defined
 *   if the string does not fit into one of these categories.  This should
 *   never happen if is_string() returns TRUE.
 *
 */

static QSTR_CATEGORY
qstr_get_category (const DB_VALUE * s)
{
  QSTR_CATEGORY code_set;

  switch (DB_VALUE_DOMAIN_TYPE (s))
    {

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      code_set = QSTR_CHAR;
      break;

    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      code_set = QSTR_NATIONAL_CHAR;
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      code_set = QSTR_BIT;
      break;

    default:
      code_set = QSTR_UNKNOWN;
      break;
    }

  return code_set;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * is_string () -
 *
 * Arguments:
 *      s: (IN) DB_VALUE variable.
 *
 * Returns: bool
 *
 * Errors:
 *
 * Note:
 *   Verifies that the value is a string.  Returns TRUE if the
 *   domain type is one of:
 *
 *       DB_TYPE_STRING
 *       DB_TYPE_CHAR
 *       DB_TYPE_VARCHAR
 *       DB_TYPE_NCHAR
 *       DB_TYPE_VARNCHAR
 *       DB_TYPE_BIT
 *       DB_TYPE_VARBIT
 *
 *   Returns FALSE otherwise.
 *
 *   This function supports the older type DB_TYPE_STRING which
 *   has been replaced by DB_TYPE_VARCHAR.
 *
 */

static bool
is_string (const DB_VALUE * s)
{
  DB_TYPE domain_type = DB_VALUE_DOMAIN_TYPE (s);

  return QSTR_IS_ANY_CHAR_OR_BIT (domain_type);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * is_char_string () -
 *
 * Arguments:
 *      s: DB_VALUE variable.
 *
 * Returns: bool
 *
 * Errors:
 *
 * Note:
 *   Verifies that the value is a character string.  Returns TRUE if the
 *   value is of domain type is one of:
 *
 *       DB_TYPE_STRING
 *       DB_TYPE_VARCHAR
 *       DB_TYPE_CHAR
 *       DB_TYPE_NCHAR
 *       DB_TYPE_VARNCHAR
 *
 *   Returns FALSE otherwise.
 *
 *   This function supports the older type DB_TYPE_STRING which
 *   has been replaced by DB_TYPE_VARCHAR.
 *
 */

static bool
is_char_string (const DB_VALUE * s)
{
  DB_TYPE domain_type = DB_VALUE_DOMAIN_TYPE (s);

  return (QSTR_IS_ANY_CHAR (domain_type));
}

/*
 * is_integer () -
 *
 * Arguments:
 *      i: (IN) DB_VALUE variable.
 *
 * Returns: bool
 *
 * Errors:
 *
 * Note:
 *   Verifies that the value is an integer.  Returns TRUE if the
 *   value is of domain type is one of:
 *
 *       DB_TYPE_INTEGER
 *
 *   Returns FALSE otherwise.
 *
 */

static bool
is_integer (const DB_VALUE * i)
{
  return (DB_VALUE_DOMAIN_TYPE (i) == DB_TYPE_INTEGER);
}

/*
 * is_number () -
 *
 * Arguments:
 *      n: (IN) DB_VALUE variable.
 *
 * Returns: bool
 *
 * Errors:
 *
 * Note:
 *   Verifies that the value is an number.  Returns TRUE if the
 *   value is of domain type is one of:
 *
 *       DB_TYPE_NUMERIC
 *       DB_TYPE_INTEGER
 *       DB_TYPE_SMALLINT
 *       DB_TYPE_DOUBLE
 *       DB_TYPE_FLOAT
 *
 *   Returns FALSE otherwise.
 *
 */

static bool
is_number (const DB_VALUE * n)
{
  DB_TYPE domain_type = DB_VALUE_DOMAIN_TYPE (n);

  return ((domain_type == DB_TYPE_NUMERIC) ||
	  (domain_type == DB_TYPE_INTEGER) ||
	  (domain_type == DB_TYPE_SMALLINT) ||
	  (domain_type == DB_TYPE_BIGINT) ||
	  (domain_type == DB_TYPE_DOUBLE) ||
	  (domain_type == DB_TYPE_FLOAT) ||
	  (domain_type == DB_TYPE_MONETARY));
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * qstr_compare () - compare two character strings of DB_TYPE_STRING(tp_String)
 *
 * Arguments:
 *      string1: 1st character string
 *        size1: size of 1st string
 *      string2: 2nd character string
 *        size2: size of 2nd string
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 * Errors:
 *
 * Note:
 *   This function is similar to strcmp(3) or bcmp(3). It is designed to
 *   follow SQL_TEXT character set collation. Padding character(space ' ') is
 *   the smallest character in the set. (e.g.) "ab z" < "ab\t1"
 *
 */

int
qstr_compare (const unsigned char *string1, int size1,
	      const unsigned char *string2, int size2)
{
  int n, i, cmp;
  unsigned char c1, c2;

#define PAD ' '			/* str_pad_char(INTL_CODESET_ISO88591, pad, &pad_size) */
#define SPACE PAD		/* smallest character in the collation sequence */
#define ZERO '\0'		/* space is treated as zero */

  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      c1 = *string1++;
      if (c1 == SPACE)
	{
	  c1 = ZERO;
	}
      c2 = *string2++;
      if (c2 == SPACE)
	{
	  c2 = ZERO;
	}
      cmp = c1 - c2;
    }
  if (cmp != 0)
    {
      return cmp;
    }
  if (size1 == size2)
    {
      return cmp;
    }

  c1 = c2 = ZERO;
  if (size1 < size2)
    {
      n = size2 - size1;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c2 = *string2++;
	  if (c2 == PAD)
	    {
	      c2 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  else
    {
      n = size1 - size2;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c1 = *string1++;
	  if (c1 == PAD)
	    {
	      c1 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  return cmp;

#undef PAD
#undef SPACE
#undef ZERO
}				/* qstr_compare() */

/*
 * char_compare () - compare two character strings of DB_TYPE_CHAR(tp_Char)
 *
 * Arguments:
 *      string1: 1st character string
 *        size1: size of 1st string
 *      string2: 2nd character string
 *        size2: size of 2nd string
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 * Errors:
 *
 * Note:
 *   This function is identical to qstr_compare().
 *
 */

int
char_compare (const unsigned char *string1, int size1,
	      const unsigned char *string2, int size2)
{
  int n, i, cmp;
  unsigned char c1, c2;

  assert (size1 >= 0 && size2 >= 0);

#define PAD ' '			/* str_pad_char(INTL_CODESET_ISO88591, pad, &pad_size) */
#define SPACE PAD		/* smallest character in the collation sequence */
#define ZERO '\0'		/* space is treated as zero */

  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      c1 = *string1++;
      if (c1 == SPACE)
	{
	  c1 = ZERO;
	}
      c2 = *string2++;
      if (c2 == SPACE)
	{
	  c2 = ZERO;
	}
      cmp = c1 - c2;
    }
  if (cmp != 0)
    {
      return cmp;
    }
  if (size1 == size2)
    {
      return cmp;
    }

  c1 = c2 = ZERO;
  if (size1 < size2)
    {
      n = size2 - size1;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c2 = *string2++;
	  if (c2 == PAD)
	    {
	      c2 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  else
    {
      n = size1 - size2;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c1 = *string1++;
	  if (c1 == PAD)
	    {
	      c1 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  return cmp;

#undef PAD
#undef SPACE
#undef ZERO
}				/* char_compare() */

/*
 * varnchar_compare () - compare two national character strings of
 *                    DB_TYPE_VARNCHAR(tp_VarNChar)
 *
 * Arguments:
 *      string1: 1st national character string
 *        size1: size of 1st string
 *      string2: 2nd national character string
 *        size2: size of 2nd string
 *      codeset: codeset of strings
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 * Errors:
 *
 * Note:
 *   This function is identical to qstr_compare() except that it awares
 *   of the codeset.
 *
 */

int
varnchar_compare (const unsigned char *string1, int size1,
		  const unsigned char *string2, int size2,
		  INTL_CODESET codeset)
{
  int n, i, cmp, pad_size = 0;
  unsigned char c1, c2, pad[2];

  intl_pad_char (codeset, pad, &pad_size);
#define PAD pad[i % pad_size]
#define SPACE PAD		/* smallest character in the collation sequence */
#define ZERO '\0'		/* space is treated as zero */
  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      c1 = *string1++;
      if (c1 == SPACE)
	{
	  c1 = ZERO;
	}
      c2 = *string2++;
      if (c2 == SPACE)
	{
	  c2 = ZERO;
	}
      cmp = c1 - c2;
    }
  if (cmp != 0)
    {
      return cmp;
    }
  if (size1 == size2)
    {
      return cmp;
    }

  c1 = c2 = ZERO;
  if (size1 < size2)
    {
      n = size2 - size1;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c2 = *string2++;
	  if (c2 == PAD)
	    {
	      c2 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  else
    {
      n = size1 - size2;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c1 = *string1++;
	  if (c1 == PAD)
	    {
	      c1 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  return cmp;
#undef SPACE
#undef ZERO
#undef PAD
}				/* varnchar_compare() */

/*
 * nchar_compare () - compare two national character strings of
 *                 DB_TYPE_NCHAR(tp_NChar)
 *
 * Arguments:
 *      string1: 1st national character string
 *        size1: size of 1st string
 *      string2: 2nd national character string
 *        size2: size of 2nd string
 *      codeset: codeset of strings
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 * Errors:
 *
 * Note:
 *   This function is identical to qstr_compare() except that it awares
 *   of the codeset.
 *
 */

int
nchar_compare (const unsigned char *string1, int size1,
	       const unsigned char *string2, int size2, INTL_CODESET codeset)
{
  int n, i, cmp, pad_size = 0;
  unsigned char c1, c2, pad[2];

  assert (size1 >= 0 && size2 >= 0);

  intl_pad_char (codeset, pad, &pad_size);
#define PAD pad[i % pad_size]
#define SPACE PAD		/* smallest character in the collation sequence */
#define ZERO '\0'		/* space is treated as zero */
  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      c1 = *string1++;
      if (c1 == SPACE)
	{
	  c1 = ZERO;
	}
      c2 = *string2++;
      if (c2 == SPACE)
	{
	  c2 = ZERO;
	}
      cmp = c1 - c2;
    }
  if (cmp != 0)
    {
      return cmp;
    }
  if (size1 == size2)
    {
      return cmp;
    }

  c1 = c2 = ZERO;
  if (size1 < size2)
    {
      n = size2 - size1;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c2 = *string2++;
	  if (c2 == PAD)
	    {
	      c2 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  else
    {
      n = size1 - size2;
      for (i = 0; i < n && cmp == 0; i++)
	{
	  c1 = *string1++;
	  if (c1 == PAD)
	    {
	      c1 = ZERO;
	    }
	  cmp = c1 - c2;
	}
    }
  return cmp;
#undef SPACE
#undef ZERO
#undef PAD
}				/* nchar_compare() */
#endif /* ENABLE_UNUSED_FUNCTION */
/*
 * bit_compare () - compare two bit strings of DB_TYPE_BIT(tp_Bit)
 *
 * Arguments:
 *      string1: 1st bit string
 *        size1: size of 1st string
 *      string2: 2nd bit string
 *        size2: size of 2nd string
 *      codeset: codeset of strings
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 * Errors:
 *
 * Note:
 *   This function is identical to qstr_compare().
 *
 */

int
bit_compare (const unsigned char *string1, int size1,
	     const unsigned char *string2, int size2)
{
  int n, i, cmp;

  assert (size1 >= 0 && size2 >= 0);

#define PAD '\0'		/* str_pad_char(INTL_CODESET_RAW_BITS, pad, &pad_size) */
  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      cmp = (*string1++ - *string2++);
    }
  if (cmp != 0)
    {
      return cmp;
    }
  cmp = size1 - size2;
  return cmp;
#undef PAD
}				/* bit_compare() */

/*
 * varbit_compare () - compare two bit strings of DB_TYPE_VARBIT(tp_VarBit)
 *
 * Arguments:
 *      string1: 1st bit string
 *        size1: size of 1st string
 *      string2: 2nd bit string
 *        size2: size of 2nd string
 *      codeset: codeset of strings
 *
 * Returns:
 *   Greater than 0 if string1 > string2
 *   Equal to 0     if string1 = string2
 *   Less than 0    if string1 < string2
 *
 * Errors:
 *
 * Note:
 *   This function is identical to qstr_compare().
 *
 */

int
varbit_compare (const unsigned char *string1, int size1,
		const unsigned char *string2, int size2)
{
  int n, i, cmp;

#define PAD '\0'		/* str_pad_char(INTL_CODESET_RAW_BITS, pad, &pad_size) */
  n = size1 < size2 ? size1 : size2;
  for (i = 0, cmp = 0; i < n && cmp == 0; i++)
    {
      cmp = (*string1++ - *string2++);
    }
  if (cmp != 0)
    {
      return cmp;
    }
  cmp = size1 - size2;
  return cmp;
#undef PAD
}				/* varbit_compare() */


/*
 * qstr_grow_string () - grows the memory buffer of string value
 *
 * Arguments:
 *            src: (IN)  String variable.
 *         result: (IN/OUT) value with new size, or DB_NULL if requested size
 *		    exceeds PRM_STRING_MAX_SIZE_BYTES system parameter
 *       new_size: (IN)  New size to be reserved for the string (in bytes).
 *
 * Returns:
 *
 * Errors:
 *	ER_QSTR_INVALID_DATA_TYPE:
 *		  <src_string> is not CHAR, NCHAR, VARCHAR or VARNCHAR
 *
 * Note : src buffer is not freed, caller should be aware of this;
 *	  Result DB_VALUE must already be created.
 *	  It doesn't operate on BIT strings;
 *	  if requested size is larger than PRM_STRING_MAX_SIZE_BYTES,
 *	  DB_VALUE_NULL is returned
 */

static int
qstr_grow_string (DB_VALUE * src_string, DB_VALUE * result, int new_size)
{
  int result_size = 0, src_length = 0, result_domain_length = 0, src_size = 0;
  unsigned char *r = NULL;
  int error_status = NO_ERROR;
  DB_TYPE src_type, result_type;
  INTL_CODESET codeset;

  assert (src_string != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_string);
  src_length = (int) DB_GET_STRING_LENGTH (src_string);
  result_domain_length = DB_VALUE_PRECISION (src_string);

  if (!QSTR_IS_ANY_CHAR (src_type) || DB_IS_NULL (src_string))
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }
  if (QSTR_IS_NATIONAL_CHAR (src_type))
    {
      result_type = DB_TYPE_NCHAR;
    }
  else
    {
      result_type = DB_TYPE_CHAR;
    }

  codeset = DB_GET_STRING_CODESET (src_string);

  result_size = src_length * INTL_CODESET_MULT (codeset);

  src_size = DB_GET_STRING_SIZE (src_string);

  assert (new_size >= result_size);
  assert (new_size >= src_size);

  result_size = MAX (result_size, new_size);
  result_size = MAX (result_size, src_size);

  if (result_size > (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_QPROC_STRING_SIZE_TOO_BIG, 2, result_size,
	      (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES));

      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  /* Allocate storage for the result string */
  r = (unsigned char *) db_private_alloc (NULL, (size_t) result_size + 1);
  if (r == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  memset (r, 0, (size_t) result_size + 1);

  if (src_size > 0)
    {
      memcpy ((char *) r, (char *) DB_PULL_STRING (src_string), src_size);
    }
  qstr_make_typed_string (result_type,
			  result,
			  result_domain_length,
			  (char *) r, (int) MIN (result_size, src_size),
			  codeset, DB_GET_STRING_COLLATION (src_string));

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true
      && DB_IS_NULL (result)
      && QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (result)))
    {
      /*intermediate value : clear is_null flag */
      result->domain.general_info.is_null = 0;
    }
  result->need_clear = true;
  return error_status;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * qstr_append () - appends a string to another string. Doesn't operate on BIT
 *
 * Arguments:
 *             s1: (IN/OUT)  First string pointer.
 *      s1_length: (IN)  Character length of <s1>.
 *   s1_precision: (IN)  Max character length of <s1>.
 *        s1_type: (IN)  Domain type of <s1>.
 *             s2: (IN)  Second string pointer.
 *      s2_length: (IN)  Character length of <s2>.
 *   s2_precision: (IN)  Max character length of <s2>.
 *        s2_type: (IN)  Domain type of <s2>.
 *        codeset: (IN)  international codeset.
 *  result_length: (OUT) Character length of <result>.
 *    result_size: (OUT) Byte size of <result>.
 *    data_status: (OUT) status of truncation
 *
 * Returns:
 *	ER_QSTR_INVALID_DATA_TYPE:
 *		  <s1> and <s2> are not CHAR, NCHAR, VARCHAR or VARNCHAR
 *
 * Errors:
 *
 */

static int
qstr_append (unsigned char *s1,
	     int s1_length,
	     int s1_precision,
	     DB_TYPE s1_type,
	     const unsigned char *s2,
	     int s2_length,
	     int s2_precision,
	     DB_TYPE s2_type,
	     INTL_CODESET codeset,
	     int *result_length,
	     int *result_size, DB_DATA_STATUS * data_status)
{
  int copy_length, copy_size;
  int pad1_length, pad2_length;
  int length_left, cat_length, cat_size;
  int s1_logical_length, s2_logical_length;
  unsigned char *cat_ptr;
  int error_status = NO_ERROR;

  *data_status = DATA_STATUS_OK;

  /* Note : append logic is similar to concatenate, except the s1 string is
   * already copied into the result. However, the concatenate logic is
   * preserved in order to have the same type limits checking and padding.
   */
  /* functions qstr_append & qstr_concatenate are kept separately because of
   * different signatures and different purpose. However, a refactoring may
   * be necessary for the shared code
   */
  if (!QSTR_IS_ANY_CHAR (s1_type) || !QSTR_IS_ANY_CHAR (s2_type))
    {
      return ER_QSTR_INVALID_DATA_TYPE;
    }
  /*
   *  Categorize the source string into fixed and variable
   *  length.  Variable length strings are simple.  Fixed
   *  length strings have to be handled special since the
   *  strings may not have all of their pad character allocated
   *  yet.  We have to account for this and act as if all of the
   *  characters are present.  They all will be by the time
   *  we are through.
   */

  if (QSTR_IS_FIXED_LENGTH (s1_type))
    {
      s1_logical_length = s1_precision;
    }
  else
    {
      s1_logical_length = s1_length;
    }


  if (QSTR_IS_FIXED_LENGTH (s2_type))
    {
      s2_logical_length = s2_precision;
    }
  else
    {
      s2_logical_length = s2_length;
    }

  /*
   *  If both source strings are fixed-length, the concatenated
   *  result will be fixed-length.
   */
  if (QSTR_IS_FIXED_LENGTH (s1_type) && QSTR_IS_FIXED_LENGTH (s2_type))
    {
      /*
       *  The result will be a chararacter string of length =
       *  string1_precision + string2_precision.  If the result
       *  length is greater than the maximum allowed for a fixed
       *  length string, the TRUNCATED exception is raised and
       *  the string is  shortened appropriately.
       */
      *result_length = s1_logical_length + s2_logical_length;
      if (*result_length > QSTR_MAX_PRECISION (s1_type))
	{
	  *result_length = QSTR_MAX_PRECISION (s1_type);
	  *data_status = DATA_STATUS_TRUNCATED;
	}

      if (QSTR_IS_NATIONAL_CHAR (s1_type))
	{
	  *result_size = *result_length * 2;
	}
      else
	{
	  *result_size = *result_length;
	}

      /*
       *  Determine how much of s1 is already copied.
       *  Remember that this may or may not include needed padding.
       *  Then determine how much padding must be added to each
       *  source string.
       */
      copy_length = MIN (s1_length, *result_length);
      intl_char_size ((unsigned char *) s1, copy_length, codeset, &copy_size);

      pad1_length = MIN (s1_logical_length, *result_length) - copy_length;
      length_left = *result_length - copy_length - pad1_length;

      /*
       *  Determine how much of string2 can be concatenated after
       *  string1.  Remember that string2 is concatentated after
       *  the full length of string1 including any necessary pad
       *  characters.
       */
      cat_length = MIN (s2_length, length_left);
      intl_char_size ((unsigned char *) s2, cat_length, codeset, &cat_size);

      pad2_length = length_left - cat_length;

      /*
       *  Pad string s1, Copy the s2 string after the s1 string
       */
      cat_ptr = qstr_pad_string ((unsigned char *) &(s1[copy_size]),
				 pad1_length, codeset);

      memcpy ((char *) cat_ptr, (char *) s2, cat_size);
      (void) qstr_pad_string ((unsigned char *) &cat_ptr[cat_size],
			      pad2_length, codeset);
    }
  /*
   *  If either source string is variable-length, the concatenated
   *  result will be variable-length.
   */
  else
    {
      /*
       *  The result length will be the sum of the lengths of
       *  the two source strings.  If this is greater than the
       *  maximum length of a variable length string, then the
       *  result length is adjusted appropriately.  This does
       *  not necessarily indicate a truncation condition.
       */
      *result_length = MIN ((s1_logical_length + s2_logical_length),
			    QSTR_MAX_PRECISION (s1_type));

      if ((s1_type == DB_TYPE_NCHAR) || (s1_type == DB_TYPE_VARNCHAR))
	{
	  *result_size = *result_length * 2;
	}
      else
	{
	  *result_size = *result_length;
	}

      /*
       *  Calculate the number of characters from string1 that are already
       *  into the result.  If s1 string is larger than the expected entire
       *  string and if the portion of the string s1 contained anything but
       *  pad characters, then raise a truncation exception.
       */
      copy_length = s1_length;
      if (copy_length > *result_length)
	{
	  copy_length = *result_length;

	  if (varchar_truncated ((unsigned char *) s1,
				 s1_type, s1_length, copy_length, codeset))
	    {
	      *data_status = DATA_STATUS_TRUNCATED;
	    }
	}
      intl_char_size ((unsigned char *) s1, copy_length, codeset, &copy_size);

      pad1_length = MIN (s1_logical_length, *result_length) - copy_length;
      length_left = *result_length - copy_length - pad1_length;

      /*
       *  Processess string2 as we did for string1.
       */
      cat_length = s2_length;
      if (cat_length > (*result_length - copy_length))
	{
	  cat_length = *result_length - copy_length;

	  if (varchar_truncated ((unsigned char *) s2,
				 s2_type, s2_length, cat_length, codeset))
	    {
	      *data_status = DATA_STATUS_TRUNCATED;
	    }
	}
      intl_char_size ((unsigned char *) s2, cat_length, codeset, &cat_size);

      pad2_length = length_left - cat_length;

      /*
       *  Actually perform the copy operation.
       */
      cat_ptr = qstr_pad_string ((unsigned char *) &(s1[copy_size]),
				 pad1_length, codeset);

      memcpy ((char *) cat_ptr, (char *) s2, cat_size);
      (void) qstr_pad_string ((unsigned char *) &cat_ptr[cat_size],
			      pad2_length, codeset);
    }

  intl_char_size (s1, *result_length, codeset, result_size);

  return error_status;
}
#endif
/*
 * qstr_concatenate () -
 *
 * Arguments:
 *             s1: (IN)  First string pointer.
 *      s1_length: (IN)  Character length of <s1>.
 *   s1_precision: (IN)  Max character length of <s1>.
 *        s1_type: (IN)  Domain type of <s1>.
 *             s2: (IN)  Second string pointer.
 *      s2_length: (IN)  Character length of <s2>.
 *   s2_precision: (IN)  Max character length of <s2>.
 *        s2_type: (IN)  Domain type of <s2>.
 *        codeset: (IN)  international codeset.
 *         result: (OUT) Concatenated string pointer.
 *  result_length: (OUT) Character length of <result>.
 *    result_size: (OUT) Byte size of <result>.
 *    result_type: (OUT) Domain type of <result>
 *
 * Returns:
 *
 * Errors:
 *
 */

static int
qstr_concatenate (const unsigned char *s1,
		  int s1_length,
		  int s1_precision,
		  DB_TYPE s1_type,
		  const unsigned char *s2,
		  int s2_length,
		  int s2_precision,
		  DB_TYPE s2_type,
		  INTL_CODESET codeset,
		  unsigned char **result,
		  int *result_length,
		  int *result_size,
		  DB_TYPE * result_type, DB_DATA_STATUS * data_status)
{
  int copy_length, copy_size;
  int pad1_length, pad2_length;
  int length_left, cat_length, cat_size;
  int s1_logical_length, s2_logical_length;
  int s1_size, s2_size;
  unsigned char *cat_ptr;
  int error_status = NO_ERROR;

  *data_status = DATA_STATUS_OK;

  /*
   *  Categorize the source string into fixed and variable
   *  length.  Variable length strings are simple.  Fixed
   *  length strings have to be handled special since the
   *  strings may not have all of their pad character allocated
   *  yet.  We have to account for this and act as if all of the
   *  characters are present.  They all will be by the time
   *  we are through.
   */
  if (QSTR_IS_FIXED_LENGTH (s1_type))
    {
      s1_logical_length = s1_precision;
    }
  else
    {
      s1_logical_length = s1_length;
    }


  if (QSTR_IS_FIXED_LENGTH (s2_type))
    {
      s2_logical_length = s2_precision;
    }
  else
    {
      s2_logical_length = s2_length;
    }

  /*
   *  If both source strings are fixed-length, the concatenated
   *  result will be fixed-length.
   */
  if (QSTR_IS_FIXED_LENGTH (s1_type) && QSTR_IS_FIXED_LENGTH (s2_type))
    {
      /*
       *  The result will be a chararacter string of length =
       *  string1_precision + string2_precision.  If the result
       *  length is greater than the maximum allowed for a fixed
       *  length string, the TRUNCATED exception is raised and
       *  the string is  shortened appropriately.
       */
      *result_length = s1_logical_length + s2_logical_length;
      if (*result_length > QSTR_MAX_PRECISION (s1_type))
	{
	  *result_length = QSTR_MAX_PRECISION (s1_type);
	  *data_status = DATA_STATUS_TRUNCATED;
	}

      intl_char_size ((unsigned char *) s1, s1_logical_length, codeset,
		      &s1_size);
      intl_char_size ((unsigned char *) s2, s2_logical_length, codeset,
		      &s2_size);

      if (s1_size == 0)
	{
	  s1_size = s1_logical_length;
	}
      if (s2_size == 0)
	{
	  s2_size = s2_logical_length;
	}

      *result_size = s1_size + s2_size;

      if (QSTR_IS_NATIONAL_CHAR (s1_type))
	{
	  *result_type = DB_TYPE_NCHAR;
	}
      else
	{
	  *result_type = DB_TYPE_CHAR;
	}

      if (*result_size >
	  (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES))
	{
	  goto size_error;
	}
      /* Allocate storage for the result string */
      *result = (unsigned char *) db_private_alloc (NULL,
						    (size_t) * result_size +
						    1);
      if (*result == NULL)
	{
	  goto mem_error;
	}

      /*
       *  Determine how much of string1 needs to be copied.
       *  Remember that this may or may not include needed padding.
       *  Then determine how much padding must be added to each
       *  source string.
       */
      copy_length = MIN (s1_length, *result_length);
      intl_char_size ((unsigned char *) s1, copy_length, codeset, &copy_size);

      pad1_length = MIN (s1_logical_length, *result_length) - copy_length;
      length_left = *result_length - copy_length - pad1_length;

      /*
       *  Determine how much of string2 can be concatenated after
       *  string1.  Remember that string2 is concatentated after
       *  the full length of string1 including any necessary pad
       *  characters.
       */
      cat_length = MIN (s2_length, length_left);
      intl_char_size ((unsigned char *) s2, cat_length, codeset, &cat_size);

      pad2_length = length_left - cat_length;

      /*
       *  Copy the source strings into the result string
       */
      memcpy ((char *) *result, (char *) s1, copy_size);
      cat_ptr = qstr_pad_string ((unsigned char *) &((*result)[copy_size]),
				 pad1_length, codeset);

      memcpy ((char *) cat_ptr, (char *) s2, cat_size);
      (void) qstr_pad_string ((unsigned char *) &cat_ptr[cat_size],
			      pad2_length, codeset);
    }
  /*
   *  If either source string is variable-length, the concatenated
   *  result will be variable-length.
   */
  else
    {
      /*
       *  The result length will be the sum of the lengths of
       *  the two source strings.  If this is greater than the
       *  maximum length of a variable length string, then the
       *  result length is adjusted appropriately.  This does
       *  not necessarily indicate a truncation condition.
       */
      *result_length = MIN ((s1_logical_length + s2_logical_length),
			    QSTR_MAX_PRECISION (s1_type));

      if ((s1_type == DB_TYPE_NCHAR) || (s1_type == DB_TYPE_VARNCHAR))
	{
	  *result_type = DB_TYPE_VARNCHAR;
	}
      else
	{
	  *result_type = DB_TYPE_VARCHAR;
	}

      intl_char_size ((unsigned char *) s1, s1_logical_length, codeset,
		      &s1_size);
      intl_char_size ((unsigned char *) s2, s2_logical_length, codeset,
		      &s2_size);

      if (s1_size == 0)
	{
	  s1_size = s1_logical_length;
	}
      if (s2_size == 0)
	{
	  s2_size = s2_logical_length;
	}

      *result_size = s1_size + s2_size;

      if (*result_size >
	  (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES))
	{
	  goto size_error;
	}

      /*  Allocate the result string */
      *result = (unsigned char *) db_private_alloc (NULL,
						    (size_t) * result_size +
						    1);
      if (*result == NULL)
	{
	  goto mem_error;
	}


      /*
       *  Calculate the number of characters from string1 that can
       *  be copied to the result.  If we cannot copy the entire
       *  string and if the portion of the string which was not
       *  copied contained anything but pad characters, then raise
       *  a truncation exception.
       */
      copy_length = s1_length;
      if (copy_length > *result_length)
	{
	  copy_length = *result_length;

	  if (varchar_truncated ((unsigned char *) s1,
				 s1_type, s1_length, copy_length, codeset))
	    {
	      *data_status = DATA_STATUS_TRUNCATED;
	    }
	}
      intl_char_size ((unsigned char *) s1, copy_length, codeset, &copy_size);

      pad1_length = MIN (s1_logical_length, *result_length) - copy_length;
      length_left = *result_length - copy_length - pad1_length;

      /*
       *  Processess string2 as we did for string1.
       */
      cat_length = s2_length;
      if (cat_length > (*result_length - copy_length))
	{
	  cat_length = *result_length - copy_length;

	  if (varchar_truncated ((unsigned char *) s2,
				 s2_type, s2_length, cat_length, codeset))
	    {
	      *data_status = DATA_STATUS_TRUNCATED;
	    }
	}
      intl_char_size ((unsigned char *) s2, cat_length, codeset, &cat_size);

      pad2_length = length_left - cat_length;

      /*
       *  Actually perform the copy operations.
       */
      memcpy ((char *) *result, (char *) s1, copy_size);
      cat_ptr = qstr_pad_string ((unsigned char *) &((*result)[copy_size]),
				 pad1_length, codeset);

      memcpy ((char *) cat_ptr, (char *) s2, cat_size);
      (void) qstr_pad_string ((unsigned char *) &cat_ptr[cat_size],
			      pad2_length, codeset);
    }

  intl_char_size (*result, *result_length, codeset, result_size);

  return error_status;

size_error:
  error_status = ER_QPROC_STRING_SIZE_TOO_BIG;
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  error_status, 2, *result_size,
	  (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES));
  return error_status;
  /*
   * Error handler
   */
mem_error:
  assert (er_errid () != NO_ERROR);
  error_status = er_errid ();
  return error_status;
}

/*
 * qstr_bit_concatenate () -
 *
 * Arguments:
 *           s1: (IN)  First string pointer.
 *    s1_length: (IN)  Character length of <s1>.
 * s1_precision: (IN)  Max character length of <s1>.
 *      s1_type: (IN)  Domain type of <s1>.
 *           s2: (IN)  Second string pointer.
 *    s2_length: (IN)  Character length of <s2>.
 * s2_precision: (IN)  Max character length of <s2>.
 *      s2_type: (IN)  Domain type of <s2>.
 *       result: (OUT) Concatenated string pointer.
 * result_length: (OUT) Character length of <result>.
 *  result_size: (OUT) Byte size of <result>.
 *  result_type: (OUT) Domain type of <result>
 *
 * Returns:
 *
 * Errors:
 *
 */

static int
qstr_bit_concatenate (const unsigned char *s1,
		      int s1_length,
		      int s1_precision,
		      DB_TYPE s1_type,
		      const unsigned char *s2,
		      int s2_length,
		      int s2_precision,
		      DB_TYPE s2_type,
		      unsigned char **result,
		      int *result_length,
		      int *result_size,
		      DB_TYPE * result_type, DB_DATA_STATUS * data_status)
{
  int s1_size, s2_size;
  int copy_length, cat_length;
  int s1_logical_length, s2_logical_length;
  int error_status = NO_ERROR;

  *data_status = DATA_STATUS_OK;

  /*
   *  Calculate the byte size of the strings.
   *  Calculate the bit length and byte size needed to concatenate
   *  the two strings without truncation.
   */
  s1_size = QSTR_NUM_BYTES (s1_length);
  s2_size = QSTR_NUM_BYTES (s2_length);


  /*
   *  Categorize the source string into fixed and variable
   *  length.  Variable length strings are simple.  Fixed
   *  length strings have to be handled special since the
   *  strings may not have all of their pad character allocated
   *  yet.  We have to account for this and act as if all of the
   *  characters are present.  They all will be by the time
   *  we are through.
   */
  if ((s1_type == DB_TYPE_CHAR) || (s1_type == DB_TYPE_NCHAR))
    {
      s1_logical_length = s1_precision;
    }
  else
    {
      s1_logical_length = s1_length;
    }


  if ((s2_type == DB_TYPE_CHAR) || (s2_type == DB_TYPE_NCHAR))
    {
      s2_logical_length = s2_precision;
    }
  else
    {
      s2_logical_length = s2_length;
    }


  if ((s1_type == DB_TYPE_BIT) && (s2_type == DB_TYPE_BIT))
    {
      /*
       *  The result will be a bit string of length =
       *  string1_precision + string2_precision.  If the result
       *  length is greater than the maximum allowed for a fixed
       *  length string, the TRUNCATED exception is raised and
       *  the string is shortened appropriately.
       */
      *result_type = DB_TYPE_BIT;
      *result_length = s1_logical_length + s2_logical_length;

      if (*result_length > DB_MAX_BIT_LENGTH)
	{
	  *result_length = DB_MAX_BIT_LENGTH;
	  *data_status = DATA_STATUS_TRUNCATED;
	}
      *result_size = QSTR_NUM_BYTES (*result_length);


      if (*result_size >
	  (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES))
	{
	  goto size_error;
	}

      /*  Allocate the result string */
      *result = (unsigned char *) db_private_alloc (NULL,
						    (size_t) * result_size +
						    1);
      if (*result == NULL)
	{
	  goto mem_error;
	}

      /*
       *  The source strings may not be fully padded, so
       *  we pre-pad the result string.
       */
      (void) memset ((char *) *result, (int) 0, (int) *result_size);

      /*
       *  Determine how much of string1 needs to be copied.
       *  Remember that this may or may not include needed padding
       */
      copy_length = s1_length;
      if (copy_length > *result_length)
	{
	  copy_length = *result_length;
	}

      /*
       *  Determine how much of string2 can be concatenated after
       *  string1.  Remember that string2 is concatentated after
       *  the full length of string1 including any necessary pad
       *  characters.
       */
      cat_length = s2_length;
      if (cat_length > (*result_length - s1_logical_length))
	{
	  cat_length = *result_length - s1_logical_length;
	}


      /*
       *  Copy the source strings into the result string.
       *  We are being a bit sloppy here by performing a byte
       *  copy as opposed to a bit copy.  But this should be OK
       *  since the bit strings should be bit padded with 0' s */
      bit_ncat (*result, 0, (unsigned char *) s1, copy_length);
      bit_ncat (*result, s1_logical_length, (unsigned char *) s2, cat_length);
    }

  else				/* Assume DB_TYPE_VARBIT */
    {
      /*
       *  The result length will be the sum of the lengths of
       *  the two source strings.  If this is greater than the
       *  maximum length of a variable length string, then the
       *  result length is adjusted appropriately.  This does
       *  not necessarily indicate a truncation condition.
       */
      *result_type = DB_TYPE_VARBIT;
      *result_length = s1_logical_length + s2_logical_length;
      if (*result_length > DB_MAX_BIT_LENGTH)
	*result_length = DB_MAX_BIT_LENGTH;

      *result_size = QSTR_NUM_BYTES (*result_length);

      if (*result_size >
	  (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES))
	{
	  goto size_error;
	}
      /* Allocate storage for the result string */
      *result = (unsigned char *) db_private_alloc (NULL,
						    (size_t) * result_size +
						    1);
      if (*result == NULL)
	{
	  goto mem_error;
	}

      /*
       *  The source strings may not be fully padded, so
       *  we pre-pad the result string.
       */
      (void) memset ((char *) *result, (int) 0, (int) *result_size);

      /*
       *  Calculate the number of bits from string1 that can
       *  be copied to the result.  If we cannot copy the entire
       *  string and if the portion of the string which was not
       *  copied contained anything but 0's, then raise a
       *  truncation exception.
       */
      copy_length = s1_length;
      if (copy_length > *result_length)
	{
	  copy_length = *result_length;
	  if (varbit_truncated (s1, s1_length, copy_length))
	    {
	      *data_status = DATA_STATUS_TRUNCATED;
	    }
	}

      /*  Processess string2 as we did for string1. */
      cat_length = s2_length;
      if (cat_length > (*result_length - copy_length))
	{
	  cat_length = *result_length - copy_length;
	  if (varbit_truncated (s2, s2_length, cat_length))
	    {
	      *data_status = DATA_STATUS_TRUNCATED;
	    }
	}


      /*
       *  Actually perform the copy operations and
       *  place the result string in a container.
       */
      bit_ncat (*result, 0, (unsigned char *) s1, copy_length);
      bit_ncat (*result, copy_length, s2, cat_length);
    }

  return error_status;

size_error:
  error_status = ER_QPROC_STRING_SIZE_TOO_BIG;
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  error_status, 2, *result_size,
	  (int) prm_get_bigint_value (PRM_ID_STRING_MAX_SIZE_BYTES));
  return error_status;

  /*
   *  Error handling
   */
mem_error:
  assert (er_errid () != NO_ERROR);
  error_status = er_errid ();
  return error_status;
}

/*
 * varchar_truncated () -
 *
 * Arguments:
 *            s:  (IN) Pointer to input string.
 *     s_length:  (IN) Length of input string.
 *   used_chars:  (IN) Number of characters which were used by caller.
 *                     0 <= <used_chars> <= <s_length>
 *      codeset:  (IN) international codeset of input string.
 *
 * Returns: bool
 *
 * Errors:
 *
 * Note:
 *     This is a convenience function which is used by the concatenation
 *     function to determine if a variable length string has been
 *     truncated.  When concatenating variable length strings, the string
 *     is not considered truncated if only pad characters were omitted.
 *
 *     This function accepts a string <s>, its length <s_length>, and
 *     a count of characters <used_chars>.  If the remaining characters
 *     are all pad characters, then the function returns true value.
 *     A False value is returned otherwise.
 *
 */

static bool
varchar_truncated (const unsigned char *s,
		   DB_TYPE s_type,
		   int s_length, int used_chars, INTL_CODESET codeset)
{
  unsigned char pad[2];
  int pad_size = 0, trim_length, trim_size;
  int s_size;

  bool truncated = false;

  intl_pad_char (codeset, pad, &pad_size);
  intl_char_size ((unsigned char *) s, s_length, codeset, &s_size);

  qstr_trim_trailing (pad, pad_size,
		      s, s_type, s_length, s_size, codeset,
		      &trim_length, &trim_size);

  if (trim_length > used_chars)
    {
      truncated = true;
    }

  return truncated;
}

/*
 * varbit_truncated () -
 *
 * Arguments:
 *            s:  (IN) Pointer to input string.
 *     s_length:  (IN) Length of input string.
 *    used_bits:  (IN) Number of characters which were used by caller.
 *                     0 <= <used_chars> <= <s_length>
 *
 * Returns:
 *
 * Errors:
 *
 * Note:
 *     This is a convenience function which is used by the concatenation
 *     function to determine if a variable length string has been
 *     truncated.  When concatenating variable length strings, the bit
 *     string is not considered truncated if only 0's were omitted.
 *
 *     This function accepts a string <s>, its length <s_length>, and
 *     a count of characters <used_chars>.  If the remaining characters
 *     are all 0's, then the function returns true value.  A False value
 *     is returned otherwise.
 *
 */

static bool
varbit_truncated (const unsigned char *s, int s_length, int used_bits)
{
  int last_set_bit;
  bool truncated = false;


  last_set_bit = bstring_fls ((char *) s, QSTR_NUM_BYTES (s_length));

  if (last_set_bit > used_bits)
    {
      truncated = true;
    }

  return truncated;
}

/*
 * bit_ncat () -
 *
 * Arguments:
 *            r: Pointer to bit string 1
 *       offset: Number of bits in string1
 *            s: Pointer to bit string 2
 *            n: Number of bits in string 2
 *
 * Returns: void
 *
 * Errors:
 *
 * Note:
 *   Shift the bits of <s> onto the end of <r>.  This is a helper
 *   function to str_bit_concatenate.  This function shifts
 *   (concatenates) exactly the number of bits specified into the result
 *   buffer which must be preallocated to the correct size.
 *
 */

static void
bit_ncat (unsigned char *r, int offset, const unsigned char *s, int n)
{
  int i, copy_size, cat_size, total_size;
  unsigned int remainder, shift_amount;
  unsigned short tmp_shifted;
  unsigned char mask;

  copy_size = QSTR_NUM_BYTES (offset);
  cat_size = QSTR_NUM_BYTES (n);
  total_size = QSTR_NUM_BYTES (offset + n);

  remainder = offset % BYTE_SIZE;

  if (remainder == 0)
    {
      memcpy ((char *) &r[copy_size], (char *) s, cat_size);
    }
  else
    {
      int start_byte = copy_size - 1;

      shift_amount = BYTE_SIZE - remainder;
      mask = 0xff << shift_amount;

      /*
       *  tmp_shifted is loaded with a byte from the source
       *  string and shifted into poition.  The upper byte is
       *  used for the current destination location, while the
       *  lower byte is used by the next destination location.
       */
      for (i = start_byte; i < total_size; i++)
	{
	  tmp_shifted = (unsigned short) (s[i - start_byte]);
	  tmp_shifted = tmp_shifted << shift_amount;
	  r[i] = (r[i] & mask) | (tmp_shifted >> BYTE_SIZE);

	  if (i < (total_size - 1))
	    {
	      r[i + 1] =
		(unsigned char) (tmp_shifted & (unsigned short) 0xff);
	    }
	}
    }

  /*  Mask out the unused bits */
  mask = 0xff << (BYTE_SIZE - ((offset + n) % BYTE_SIZE));
  if (mask != 0)
    {
      r[total_size - 1] &= mask;
    }
}

/*
 * bstring_fls () -
 *
 * Arguments:
 *            s: Pointer to source bit string
 *            n: Number of bits in string1
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *   Find the last set bit in the bit string.  The bits are numbered left
 *   to right starting at 1.  A value of 0 indicates that no set bits were
 *   found in the string.
 *
 */

static int
bstring_fls (const char *s, int n)
{
  int byte_num, bit_num, inter_bit_num;


  /*
   *  We are looking for the first non-zero byte (starting at the end).
   */
  byte_num = n - 1;
  while ((byte_num >= 0) && ((int) (s[byte_num]) == 0))
    {
      byte_num--;
    }

  /*
   *  If byte_num is < 0, then the string is all 0's.
   *  Othersize, byte_num is the index for the first byte which has
   *  some bits set (from the end).
   */
  if (byte_num < 0)
    {
      bit_num = 0;
    }
  else
    {
      inter_bit_num = (int) qstr_ffs ((int) (s[byte_num]));
      bit_num = (byte_num * BYTE_SIZE) + (BYTE_SIZE - inter_bit_num + 1);
    }

  return bit_num;
}

/*
 * qstr_bit_coerce () -
 *
 * Arguments:
 *        src_string:  (In) Source string
 *       dest_string: (Out) Coerced string
 *
 * Returns: DB_DATA_STATUS
 *
 * Errors:
 *
 * Note:
 *   This is a helper function which performs the actual coercion for
 *   bit strings.  It is called from db_bit_string_coerce().
 *
 *   If any loss of data due to truncation occurs DATA_STATUS_TRUNCATED
 *   is returned.
 *
 */

static int
qstr_bit_coerce (const unsigned char *src,
		 int src_length,
		 int src_precision,
		 DB_TYPE src_type,
		 unsigned char **dest,
		 int *dest_length,
		 int dest_precision,
		 DB_TYPE dest_type, DB_DATA_STATUS * data_status)
{
  int src_padded_length, copy_size, dest_size, copy_length;
  int error_status = NO_ERROR;

  *data_status = DATA_STATUS_OK;

  /*
   *  <src_padded_length> is the length of the fully padded
   *  source string.
   */
  if (QSTR_IS_FIXED_LENGTH (src_type))
    {
      src_padded_length = src_precision;
    }
  else
    {
      src_padded_length = src_length;
    }

  /*
   *  If there is not enough precision in the destination string,
   *  then some bits will be omited from the source string.
   */
  if (src_padded_length > dest_precision)
    {
      src_padded_length = dest_precision;
      *data_status = DATA_STATUS_TRUNCATED;
    }

  copy_length = MIN (src_length, src_padded_length);
  copy_size = QSTR_NUM_BYTES (copy_length);

  /*
   *  For fixed-length destination strings...
   *    Allocate the destination precision size, copy the source
   *    string and pad the rest.
   *
   *  For variable-length destination strings...
   *    Allocate enough for a fully padded source string, copy
   *    the source string and pad the rest.
   */
  if (QSTR_IS_FIXED_LENGTH (dest_type))
    {
      *dest_length = dest_precision;
    }
  else
    {
      *dest_length = MIN (src_padded_length, dest_precision);
    }

  dest_size = QSTR_NUM_BYTES (*dest_length);

  *dest = (unsigned char *) db_private_alloc (NULL, dest_size + 1);
  if (*dest == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
    }
  else
    {
      bit_ncat (*dest, 0, src, copy_length);
      (void) memset ((char *) &((*dest)[copy_size]),
		     (int) 0, (dest_size - copy_size));
    }

  return error_status;
}

/*
 * qstr_coerce () -
 *
 * Arguments:
 *        src_string:  (In) Source string
 *       dest_string: (Out) Coerced string
 *
 * Returns: DB_DATA_STATUS
 *
 * Errors:
 *
 * Note:
 *   This is a helper function which performs the actual coercion for
 *   character strings.  It is called from db_char_string_coerce().
 *
 *   If any loss of data due to truncation occurs DATA_STATUS_TRUNCATED
 *   is returned.
 *
 */

static int
qstr_coerce (const unsigned char *src,
	     int src_length,
	     int src_precision,
	     DB_TYPE src_type,
	     INTL_CODESET src_codeset,
	     INTL_CODESET dest_codeset,
	     unsigned char **dest,
	     int *dest_length,
	     int *dest_size,
	     int dest_precision,
	     DB_TYPE dest_type, DB_DATA_STATUS * data_status)
{
  int src_padded_length, copy_length, copy_size;
  int alloc_size;
  char *end_of_string;
  int error_status = NO_ERROR;

  *data_status = DATA_STATUS_OK;
  *dest_size = 0;

  /*
   *  <src_padded_length> is the length of the fully padded
   *  source string.
   */
  if (QSTR_IS_FIXED_LENGTH (src_type))
    {
      src_padded_length = src_precision;
    }
  else
    {
      src_padded_length = src_length;
    }

  /*
   *  Some characters will be truncated if there is not enough
   *  precision in the destination string.  If any of the
   *  truncated characters are non-pad characters, a truncation
   *  exception is raised.
   */
  if (src_padded_length > dest_precision)
    {
      src_padded_length = dest_precision;
      if ((src_length > src_padded_length) &&
	  (varchar_truncated (src, src_type, src_length,
			      src_padded_length, src_codeset)))
	{
	  *data_status = DATA_STATUS_TRUNCATED;
	}
    }

  copy_length = MIN (src_length, src_padded_length);

  /*
   *  For fixed-length destination strings...
   *    Allocate the destination precision size, copy the source
   *    string and pad the rest.
   *
   *  For variable-length destination strings...
   *    Allocate enough for a fully padded source string, copy
   *    the source string and pad the rest.
   */
  if (QSTR_IS_FIXED_LENGTH (dest_type))
    {
      *dest_length = dest_precision;
    }
  else
    {
      *dest_length = src_padded_length;
    }

  if (dest_codeset == INTL_CODESET_ISO88591)
    {
      /* when coercing multibyte to ISO charset, we just reinterpret each
       * byte as one character */
      if (INTL_CODESET_MULT (src_codeset) > 1
	  && (copy_length < dest_precision
	      || dest_precision == TP_FLOATING_PRECISION_VALUE))
	{
	  intl_char_size ((unsigned char *) src, copy_length, src_codeset,
			  &copy_size);
	  copy_size = MIN (copy_size, dest_precision);
	  copy_length = copy_size;
	  if (QSTR_IS_VARIABLE_LENGTH (dest_type))
	    {
	      *dest_length = copy_length;
	    }
	}
      else
	{
	  copy_size = copy_length;
	}
    }
  else
    {
      /* copy_length = number of characters, count the bytes according to
       * source codeset */
      intl_char_size ((unsigned char *) src, copy_length, src_codeset,
		      &copy_size);
    }

  alloc_size = INTL_CODESET_MULT (dest_codeset) * (*dest_length);

  /* fix allocation size enough to fit copy size plus pad size */
  {
    unsigned char pad[2];
    int pad_size = 0;

    intl_pad_char (dest_codeset, pad, &pad_size);
    alloc_size =
      MAX (alloc_size, copy_size + (*dest_length - copy_length) * pad_size);
  }

  if (!alloc_size)
    {
      alloc_size = 1;
    }

  *dest = (unsigned char *) db_private_alloc (NULL, alloc_size + 1);
  if (*dest == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
    }
  else
    {
      int conv_status = 0;

      assert (copy_size >= 0);
      if (copy_size == 0)
	{
	  assert (alloc_size > 0);
	  **dest = '\0';
	}
      else if (src_codeset == INTL_CODESET_ISO88591
	       && dest_codeset == INTL_CODESET_UTF8)
	{
	  int conv_size = 0;

	  assert (copy_size > 0);
	  conv_status = intl_fast_iso88591_to_utf8 (src, copy_size, dest,
						    &conv_size);
	  copy_size = conv_size;
	}
      else if (src_codeset == INTL_CODESET_KSC5601_EUC
	       && dest_codeset == INTL_CODESET_UTF8)
	{
	  int conv_size = 0;

	  assert (copy_size > 0);
	  conv_status = intl_euckr_to_utf8 (src, copy_size, dest, &conv_size);
	  copy_size = conv_size;
	}
      else if (src_codeset == INTL_CODESET_UTF8
	       && dest_codeset == INTL_CODESET_KSC5601_EUC)
	{
	  int conv_size = 0;

	  assert (copy_size > 0);
	  conv_status = intl_utf8_to_euckr (src, copy_size, dest, &conv_size);
	  copy_size = conv_size;
	}
      else if (src_codeset == INTL_CODESET_ISO88591
	       && dest_codeset == INTL_CODESET_KSC5601_EUC)
	{
	  int conv_size = 0;

	  assert (copy_size > 0);
	  conv_status = intl_iso88591_to_euckr (src, copy_size, dest,
						&conv_size);
	  copy_size = conv_size;
	}
      else
	{
	  if (copy_size > alloc_size)
	    {
	      assert (INTL_CODESET_MULT (src_codeset) > 1
		      && dest_codeset == INTL_CODESET_ISO88591);

	      copy_size = alloc_size;
	      *data_status = DATA_STATUS_TRUNCATED;
	    }
	  (void) memcpy ((char *) *dest, (char *) src, (int) copy_size);
	}

      end_of_string = (char *) qstr_pad_string ((unsigned char *)
						&((*dest)[copy_size]),
						(*dest_length - copy_length),
						dest_codeset);
      *dest_size = CAST_STRLEN (end_of_string - (char *) (*dest));

      if (conv_status != 0)
	{
	  /* conversion error occured, re-count characters so that we comply
	   * to computed precision */
	  (void) intl_char_size (*dest, *dest_length, dest_codeset,
				 dest_size);
	  end_of_string = *dest + *dest_size;
	  *end_of_string = '\0';
	}

      assert (*dest_size <= alloc_size);

      if (conv_status != 0 && er_errid () != ER_CHAR_CONV_NO_MATCH)
	{
	  /* set a warning */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_CHAR_CONV_NO_MATCH,
		  2, lang_charset_cubrid_name (src_codeset),
		  lang_charset_cubrid_name (dest_codeset));
	}
    }

  return error_status;
}

/*
 * qstr_position () -
 *
 * Arguments:
 *        sub_string: String fragment to search for within <src_string>.
 *        sub_length: Number of characters in sub_string.
 *        src_string: String to be searched.
 *  src_string_bound: Bound of string buffer:
 *		      end of string buffer, if 'is_forward_search == true'
 *		      start of string buffer, if 'is_forward_search == false'
 *        src_length: Number of characters in src_string.
 * is_forward_search: forward search or backward search.
 *           codeset: Codeset of strings.
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *     This function accepts a source string <src_sring> and a string
 *     string fragment <sub_string> and returns the character position
 *     corresponding to the first occurance of <sub_string> within
 *     <src_string>.
 *
 *     This function works with National character strings.
 *
 */

static int
qstr_position (const char *sub_string, const int sub_size,
	       const int sub_length,
	       const char *src_string, const char *src_end,
	       const char *src_string_bound,
	       int src_length, int coll_id,
	       bool is_forward_search, int *position)
{
  int error_status = NO_ERROR;
  int dummy;

  *position = 0;

  if (sub_length == 0)
    {
      *position = 1;
    }
  else
    {
      int i, num_searches, current_position, result;
      unsigned char *ptr;
      int char_size;
      LANG_COLLATION *lc;
      INTL_CODESET codeset;

      lc = lang_get_collation (coll_id);
      assert (lc != NULL);

      codeset = lc->codeset;

      /*
       *  Since the entire sub-string must be matched, a reduced
       *  number of compares <num_searches> are needed.  A collation-based
       *  comparison will be used.
       */
      if (lc->coll.uca_exp_num > 1 || lc->coll.count_contr > 0)
	{
	  /* characters may not match one-by-one */
	  num_searches = src_length;
	}
      else
	{
	  num_searches = src_length - sub_length + 1;
	  if (sub_length > src_length)
	    {
	      *position = 0;
	      return error_status;
	    }
	}

      /*
       *  Starting at the first position of the string, match the
       *  sub-string to the source string.  If a match is not found,
       *  then increment into the source string by one character and
       *  try again.  This is repeated until a match is found, or
       *  there are no more comparisons to be made.
       */
      ptr = (unsigned char *) src_string;
      current_position = 0;
      result = 1;

      for (i = 0; i < num_searches; i++)
	{
	  result = QSTR_MATCH (coll_id, ptr, (unsigned char *) src_end - ptr,
			       (unsigned char *) sub_string, sub_size,
			       NULL, false, &dummy);
	  current_position++;
	  if (result == 0)
	    {
	      break;
	    }

	  if (is_forward_search)
	    {
	      if (ptr >= (unsigned char *) src_string_bound)
		{
		  break;
		}

	      INTL_NEXT_CHAR (ptr, (unsigned char *) ptr,
			      codeset, &char_size);
	    }
	  else
	    {
	      /* backward */
	      if (ptr > (unsigned char *) src_string_bound)
		{
		  ptr = intl_prev_char ((unsigned char *) ptr,
					(const unsigned char *)
					src_string_bound,
					codeset, &char_size);
		}
	      else
		{
		  break;
		}
	    }
	}

      /*
       *  Return the position of the match, if found.
       */
      if (result == 0)
	{
	  *position = current_position;
	}
    }

  return error_status;
}

/*
 * qstr_bit_position () -
 *
 * Arguments:
 *      sub_string: String fragment to search for within <src_string>.
 *      sub_length: Number of characters in sub_string.
 *      src_string: String to be searched.
 *      src_length: Number of characters in src_string.
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *     This function accepts a source string <src_sring> and a string
 *     string fragment <sub_string> and returns the bit position
 *     corresponding to the first occurance of <sub_string> within
 *     <src_string>.
 *
 */

static int
qstr_bit_position (const unsigned char *sub_string,
		   int sub_length,
		   const unsigned char *src_string,
		   int src_length, int *position)
{
  int error_status = NO_ERROR;

  *position = 0;

  if (sub_length == 0)
    {
      *position = 1;
    }

  else if (sub_length > src_length)
    {
      *position = 0;
    }

  else
    {
      int i, num_searches, result;
      int sub_size, sub_remainder, shift_amount;
      unsigned char *ptr, *tmp_string, tmp_byte, mask;

      num_searches = src_length - sub_length + 1;
      sub_size = QSTR_NUM_BYTES (sub_length);

      sub_remainder = sub_length % BYTE_SIZE;
      shift_amount = BYTE_SIZE - sub_remainder;
      mask = 0xff << shift_amount;

      /*
       *  We will be manipulating the source string prior to
       *  comparison.  So that we do not corrupt the source string,
       *  we'll allocate a storage area so that we can make a copy
       *  of the string.  This copy need only be he length of the
       *  sub-string since that is the limit of the comparison.
       */
      tmp_string = (unsigned char *) db_private_alloc (NULL,
						       (size_t) sub_size + 1);
      if (tmp_string == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error_status = er_errid ();
	}
      else
	{
	  ptr = (unsigned char *) src_string;

	  /*
	   *  Make a copy of the source string.
	   *  Initialize the bit index.
	   */
	  (void) memcpy ((char *) tmp_string, (char *) ptr, sub_size);
	  i = 0;
	  result = 1;

	  while ((i < num_searches) && (result != 0))
	    {
	      /* Pad the irrelevant bits of the source string with 0's */
	      tmp_byte = tmp_string[sub_size - 1];
	      tmp_string[sub_size - 1] &= mask;

	      /* Compare the source string with the sub-string */
	      result = memcmp (sub_string, tmp_string, sub_size);

	      /* Restore the padded byte to its original value */
	      tmp_string[sub_size - 1] = tmp_byte;

	      /* Shift the copied source string left one bit */
	      (void) shift_left (tmp_string, sub_size);

	      i++;

	      /*
	       *  Every time we hit a byte boundary,
	       *  Move on to the next byte of the source string.
	       */
	      if ((i % BYTE_SIZE) == 0)
		{
		  ptr++;
		  memcpy (tmp_string, ptr, sub_size);
		}
	    }


	  db_private_free_and_init (NULL, tmp_string);

	  /*
	   *  If a match was found, then return the position
	   *  of the match.
	   */
	  if (result == 0)
	    {
	      *position = i;
	    }
	}
    }

  return error_status;
}

/*
 * shift_left () -
 *
 * Arguments:
 *             bit_string: Byte array representing a bit string.
 *        bit_string_size: Number of bytes in the array.
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *     Shift the bit string left one bit.  The left most bit is shifted out
 *     and returned.  A 0 is inserted into the rightmost bit position.
 *     The entire array is shifted regardless of the number of significant
 *     bits in the array.
 *
 */

static int
shift_left (unsigned char *bit_string, int bit_string_size)
{
  int i, highest_bit;


  highest_bit = ((bit_string[0] & 0x80) != 0);
  bit_string[0] = bit_string[0] << 1;

  for (i = 1; i < bit_string_size; i++)
    {
      if (bit_string[i] & 0x80)
	{
	  bit_string[i - 1] |= 0x01;
	}

      bit_string[i] = bit_string[i] << 1;
    }

  return highest_bit;
}

/*
 * qstr_substring () -
 *
 * Arguments:
 *             src_string: Source string.
 *         start_position: Starting character position of sub-string.
 *      extraction_length: Length of sub-string.
 *             sub_string: Returned sub-string.
 *
 * Returns: void
 *
 * Errors:
 *
 * Note:
 *     Extract the sub-string from the source string.  The sub-string is
 *     specified by a starting position and length.
 *
 *     This functions works on character and national character strings.
 *
 */

static int
qstr_substring (const unsigned char *src,
		int src_length,
		int start,
		int length,
		INTL_CODESET codeset,
		unsigned char **r, int *r_length, int *r_size)
{
  int error_status = NO_ERROR;
  const unsigned char *sub;
  int src_size, leading_bytes;
  *r_size = 0;

  /* Get the size of the source string. */
  intl_char_size ((unsigned char *) src, src_length, codeset, &src_size);

  /*
   * Perform some error chaecking.
   * If the starting position is < 1, then set it to 1.
   * If the starting position is after the end of the source string,
   * then set the sub-string length to 0.
   * If the sub-string length will extend beyond the end of the source string,
   * then shorten the sub-string length to fit.
   */
  if (start < 1)
    {
      start = 1;
    }

  if (start > src_length)
    {
      start = 1;
      length = 0;
    }

  if ((length < 0) || ((start + length - 1) > src_length))
    {
      length = src_length - start + 1;
    }

  *r_length = length;

  /*
   *  Get a pointer to the start of the sub-string and the
   *  size of the sub-string.
   *
   *  Compute the starting byte of the sub-string.
   *  Compute the length of the sub-string in bytes.
   */
  intl_char_size ((unsigned char *) src, (start - 1), codeset,
		  &leading_bytes);
  sub = &(src[leading_bytes]);
  intl_char_size ((unsigned char *) sub, *r_length, codeset, r_size);

  *r = (unsigned char *) db_private_alloc (NULL, (size_t) ((*r_size) + 1));
  if (*r == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
    }
  else
    {
      (void) memcpy (*r, sub, *r_size);
    }

  return error_status;
}

/*
 * qstr_bit_substring () -
 *
 * Arguments:
 *             src_string: Source string.
 *         start_position: Starting character position of sub-string.
 *      extraction_length: Length of sub-string.
 *             sub_string: Returned sub-string.
 *
 * Returns: void
 *
 * Errors:
 *
 * Note:
 *     Extract the sub-string from the source string.  The sub-string is
 *     specified by a starting position and length.
 *
 *     This functions works on bit strings.
 *
 */

static int
qstr_bit_substring (const unsigned char *src,
		    int src_length,
		    int start, int length, unsigned char **r, int *r_length)
{
  int src_size, sub_size, rem;
  unsigned char trailing_mask;
  int error_status = NO_ERROR;

  src_size = QSTR_NUM_BYTES (src_length);

  /*
   *  Perform some error checking.
   *  If the starting position is < 1, then set it to 1.
   *  If the starting position is after the end of the source
   *    string, then set the sub-string length to 0.
   *  If the sub-string length will extend beyond the end of the
   *    source string, then shorten the sub-string length to fit.
   */
  if (start < 1)
    {
      start = 1;
    }

  if (start > src_length)
    {
      start = 1;
      length = 0;
    }

  if ((length < 0) || ((start + length - 1) > src_length))
    {
      length = src_length - start + 1;
    }

  sub_size = QSTR_NUM_BYTES (length);
  *r_length = length;

  rem = length % BYTE_SIZE;
  if (rem == 0)
    {
      trailing_mask = 0xff;
    }
  else
    {
      trailing_mask = 0xff << (BYTE_SIZE - rem);
    }

  /*
   *  Allocate storage for the sub-string.
   *  Copy the sub-string.
   */
  *r = (unsigned char *) db_private_alloc (NULL, (size_t) sub_size + 1);
  if (*r == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_status = er_errid ();
    }
  else
    {
      left_nshift (src, src_size, (start - 1), *r, sub_size);
      (*r)[sub_size - 1] &= trailing_mask;
    }

  return error_status;
}

/*
 * left_nshift () -
 *
 * Arguments:
 *             bit_string: Byte array containing the bit string.
 *        bit_string_size: Size of the bit array in bytes.
 *           shift_amount: Number of bit positions to shift by.
 *                             range: 0 <= shift_amount
 *                      r: Pointer to result buffer where the shifted bit
 *                         array will be stored.
 *                 r_size: Size of the result array in bytes.
 *
 * Returns: void
 *
 * Errors:
 *
 * Note:
 *     Shift the bit string left <shift_amount> bits.  The left most bits
 *     are shifted out.  0's are inserted into the rightmost bit positions.
 *     The entire array is shifted regardless of the number of significant
 *     bits in the array.
 *
 */

static void
left_nshift (const unsigned char *bit_string,
	     int bit_string_size,
	     int shift_amount, unsigned char *r, int r_size)
{
  int i, shift_bytes, shift_bits, adj_bit_string_size;
  const unsigned char *ptr;

  shift_bytes = shift_amount / BYTE_SIZE;
  shift_bits = shift_amount % BYTE_SIZE;
  ptr = &(bit_string[shift_bytes]);

  adj_bit_string_size = bit_string_size - shift_bytes;

  for (i = 0; i < r_size; i++)
    {
      if (i < (adj_bit_string_size - 1))
	{
	  r[i] = ((ptr[i] << shift_bits) |
		  (ptr[i + 1] >> (BYTE_SIZE - shift_bits)));
	}
      else if (i == (adj_bit_string_size - 1))
	{
	  r[i] = (ptr[i] << shift_bits);
	}
      else
	{
	  r[i] = 0;
	}
    }
}

/*
 *  The version below handles multibyte character sets by promoting all
 *  characters to two bytes each.  Unfortunately, the current implementation
 *  of the regular expression package has some limitations with characters
 *  that are not char sized.  The above version works with char sized
 *  sets only and therefore will not work with national character sets.
 */

/*
 * qstr_ffs () -
 *   Returns: int
 *   v: (IN) Source string.
 *
 *
 * Errors:
 *
 * Note:
 *     Finds the first bit set in the passed argument and returns
 *     the index of that bit.  Bits are numbered starting at 1
 *     from the right.  A return value of 0 indicates that the value
 *     passed is zero.
 *
 */

static int
qstr_ffs (int v)
{
  int nbits;

  int i = 0;
  int position = 0;
  unsigned int uv = (unsigned int) v;

  nbits = sizeof (int) * 8;

  if (uv != 0)
    {
      while ((i < nbits) && (position == 0))
	{
	  if (uv & 0x01)
	    {
	      position = i + 1;
	    }

	  i++;
	  uv >>= 1;
	}
    }

  return position;
}

/*
 * hextoi () -
 *
 * Arguments:
 *             hex_char: (IN) Character containing ASCII hex character
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *     Returns the decimal value associated with the ASCII hex character.
 *     Will return a -1 if hex_char is not a hexadecimal ASCII character.
 *
 */

static int
hextoi (char hex_char)
{
  if ((hex_char >= '0') && (hex_char <= '9'))
    {
      return (hex_char - '0');
    }
  else if ((hex_char >= 'A') && (hex_char <= 'F'))
    {
      return (hex_char - 'A' + 10);
    }
  else if ((hex_char >= 'a') && (hex_char <= 'f'))
    {
      return (hex_char - 'a' + 10);
    }
  else
    {
      return (-1);
    }
}

/*
 * set_time_argument() - construct struct tm
 *   return:
 *   dest(out):
 *   year(in):
 *   month(in):
 *   day(in):
 *   hour(in):
 *   min(in):
 *   sec(in):
 */
static void
set_time_argument (struct tm *dest, int year, int month, int day,
		   int hour, int min, int sec)
{
  if (year >= 1900)
    {
      dest->tm_year = year - 1900;
    }
  else
    {
      dest->tm_year = -1;
    }
  dest->tm_mon = month - 1;
  dest->tm_mday = day;
  dest->tm_hour = hour;
  dest->tm_min = min;
  dest->tm_sec = sec;
  dest->tm_isdst = -1;
}

/*
 * calc_unix_timestamp() - calculates UNIX timestamp
 *   return:
 *   time_argument(in):
 */
static long
calc_unix_timestamp (struct tm *time_argument)
{
  time_t result;

  if (time_argument != NULL)
    {
      /* validation for tm fields in order to cover for mktime conversion's
         like 40th of Sept equals 10th of Oct */
      if (time_argument->tm_year < 0 || time_argument->tm_year > 9999
	  || time_argument->tm_mon < 0 || time_argument->tm_mon > 11
	  || time_argument->tm_mday < 1 || time_argument->tm_mday > 31
	  || time_argument->tm_hour < 0 || time_argument->tm_hour > 23
	  || time_argument->tm_min < 0 || time_argument->tm_min > 59
	  || time_argument->tm_sec < 0 || time_argument->tm_sec > 59)
	{
	  return -1L;
	}
      result = mktime (time_argument);
    }
  else
    {
      result = time (NULL);
    }

  if (result < (time_t) 0)
    {
      return -1L;
    }
  return (long) result;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * parse_for_next_int () -
 *
 * Arguments:
 *         ch: char position from which we start parsing
 *	   output: integer read
 *
 * Returns: -1 if error, 0 if success
 *
 * Note:
 *  parses a string for integers while skipping non-alpha delimitators
 */
static int
parse_for_next_int (char **ch, char *output)
{
  int i;
  /* we need in fact only 6 (upper bound for the integers we want) */
  char buf[16];

  i = 0;
  memset (buf, 0, sizeof (buf));

  /* trailing zeroes - accept only 2 (for year 00 which is short for 2000) */
  while (**ch == '0')
    {
      if (i < 2)
	{
	  buf[i++] = **ch;
	}
      (*ch)++;
    }

  while (i < 6 && char_isdigit (**ch) && **ch != 0)
    {
      buf[i++] = **ch;
      (*ch)++;
    }
  if (i > 6)
    {
      return -1;
    }
  strcpy (output, buf);

  /* skip all delimitators */
  while (**ch != 0 && !char_isalpha (**ch) && !char_isdigit (**ch))
    {
      (*ch)++;
    }
  return 0;
}
#endif
/*
 * db_unix_timestamp () -
 *
 * Arguments:
 *         src_date: datetime from which we calculate timestamp
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 * Returns a Unix timestamp (seconds since '1970-01-01 00:00:00' UTC)
 */
int
db_unix_timestamp (const DB_VALUE * src_date, DB_VALUE * result_timestamp)
{
  DB_TYPE type = DB_TYPE_UNKNOWN;
  int error_status = NO_ERROR;
  int val = 0;
  time_t ts = 0;
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0, ms = 0;
  struct tm time_argument;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result_timestamp);
      return error_status;
    }

  type = DB_VALUE_DOMAIN_TYPE (src_date);
  switch (type)
    {
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      {
	DB_VALUE dt;
	TP_DOMAIN *tp_datetime;

	tp_datetime = db_type_to_db_domain (DB_TYPE_DATETIME);
	if (tp_value_cast (src_date, &dt, tp_datetime, false)
	    != DOMAIN_COMPATIBLE)
	  {
	    error_status = ER_OBJ_INVALID_ARGUMENTS;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    DB_MAKE_NULL (result_timestamp);
	    return ER_FAILED;
	  }
	error_status = db_datetime_decode (DB_GET_DATETIME (&dt), &month,
					   &day, &year, &hour, &minute,
					   &second, &ms);
	if (year == 0 && month == 0 && day == 0
	    && hour == 0 && minute == 0 && second == 0 && ms == 0)
	  {
	    /* This function should return 0 if the date is zero date */
	    DB_MAKE_INT (result_timestamp, 0);
	    return NO_ERROR;
	  }

	if (year < 1970 || year > 2038)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		    ER_OBJ_INVALID_ARGUMENTS, 0);
	    DB_MAKE_NULL (result_timestamp);
	    return ER_FAILED;
	  }

	set_time_argument (&time_argument, year, month, day, hour, minute,
			   second);
	val = (int) calc_unix_timestamp (&time_argument);
	if (val < 0)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		    ER_OBJ_INVALID_ARGUMENTS, 0);
	    return ER_FAILED;
	  }

	DB_MAKE_INT (result_timestamp, val);
	return NO_ERROR;
      }

      /* a TIMESTAMP format */
    case DB_TYPE_TIMESTAMP:
      /* The supported timestamp range is '1970-01-01 00:00:01'
       * UTC to '2038-01-19 03:14:07' UTC */
      ts = *DB_GET_TIMESTAMP (src_date);
      /* supplementary conversion from long to int will be needed on
       * 64 bit platforms.  */
      val = (int) ts;
      DB_MAKE_INT (result_timestamp, val);
      return NO_ERROR;

      /* a DATETIME format */
    case DB_TYPE_DATETIME:
      /* The supported datetime range is '1970-01-01 00:00:01'
       * UTC to '2038-01-19 03:14:07' UTC */

      error_status = db_datetime_decode (DB_GET_DATETIME (src_date),
					 &month, &day, &year, &hour,
					 &minute, &second, &ms);
      if (error_status != NO_ERROR)
	{
	  return error_status;
	}

      if (year == 0 && month == 0 && day == 0
	  && hour == 0 && minute == 0 && second == 0 && ms == 0)
	{
	  /* This function should return 0 if the date is zero date */
	  DB_MAKE_INT (result_timestamp, 0);
	  return NO_ERROR;
	}

      if (year < 1970 || year > 2038)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
		  0);
	  DB_MAKE_NULL (result_timestamp);
	  return ER_FAILED;
	}

      set_time_argument (&time_argument, year, month, day, hour,
			 minute, second);
      val = (int) calc_unix_timestamp (&time_argument);
      if (val < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  return ER_FAILED;
	}
      DB_MAKE_INT (result_timestamp, val);
      return NO_ERROR;

      /* a DATE format */
    case DB_TYPE_DATE:
      /* The supported datetime range is '1970-01-01 00:00:01'
       * UTC to '2038-01-19 03:14:07' UTC */

      db_date_decode (DB_GET_DATE (src_date), &month, &day, &year);

      if (year == 0 && month == 0 && day == 0
	  && hour == 0 && minute == 0 && second == 0 && ms == 0)
	{
	  /* This function should return 0 if the date is zero date */
	  DB_MAKE_INT (result_timestamp, 0);
	  return NO_ERROR;
	}

      if (year < 1970 || year > 2038)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
		  0);
	  DB_MAKE_NULL (result_timestamp);
	  return ER_FAILED;
	}

      set_time_argument (&time_argument, year, month, day, hour,
			 minute, second);
      val = (int) calc_unix_timestamp (&time_argument);
      if (val < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  return ER_FAILED;
	}
      DB_MAKE_INT (result_timestamp, val);
      return NO_ERROR;

    default:
      DB_MAKE_NULL (result_timestamp);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  DB_MAKE_NULL (result_timestamp);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
  return ER_FAILED;

}

/*
 * db_datetime_to_timestamp () - create a timestamp DB_VALUE from a datetime
 *	  DB_VALUE
 *
 * src_datetime(in):
 * result_timestamp(in):
 * return: ERROR_CODE
 */
int
db_datetime_to_timestamp (const DB_VALUE * src_datetime,
			  DB_VALUE * result_timestamp)
{
  DB_DATETIME *tmp_datetime;
  DB_DATE tmp_date;
  DB_TIME tmp_time;
  DB_TIMESTAMP tmp_timestamp;
  int error;
  DB_VALUE temp, *temp_p;
  bool same_argument = (src_datetime == result_timestamp);

  if (DB_IS_NULL (src_datetime))
    {
      db_make_null (result_timestamp);

      return NO_ERROR;
    }

  if (same_argument)
    {
      /* if the result argument is the same with the source argument, then use
       * a temporary value for creating the timestamp
       */
      temp_p = &temp;
    }
  else
    {
      /* the result_timestamp value can be used and no other temporary values
       * are needed
       */
      temp_p = result_timestamp;
    }

  error = db_value_domain_init (temp_p, DB_TYPE_TIMESTAMP,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  if (error != NO_ERROR)
    {
      /* error message has been set */
      return error;
    }

  tmp_datetime = db_get_datetime (src_datetime);
  tmp_date = tmp_datetime->date;
  tmp_time = tmp_datetime->time / 1000;
  error = db_timestamp_encode (&tmp_timestamp, &tmp_date, &tmp_time);
  if (error != NO_ERROR)
    {
      /* error message has been set */
      return error;
    }
  db_make_timestamp (temp_p, tmp_timestamp);

  if (same_argument)
    {
      /* if src_datetime was the same with result_timestamp, copy the result
       * from temp, and release the temporary value
       */
      pr_clone_value (temp_p, result_timestamp);
    }

  return NO_ERROR;
}

/*
 * db_get_date_dayofyear () - compute day of year from a date type value
 *
 * Arguments:
 *	  src_date: datetime from which to compute the day of year
 *
 * Returns: int
 */
int
db_get_date_dayofyear (const DB_VALUE * src_date, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;
  int day_of_year = 0;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* get the date/time information from src_date */
  error_status = db_get_datetime_from_dbvalue (src_date, &year, &month, &day,
					       &hour, &minute, &second, &ms,
					       NULL);
  if (error_status != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      goto error_exit;
    }

  if (year == 0 && month == 0 && day == 0
      && hour == 0 && minute == 0 && second == 0 && ms == 0)
    {
      error_status = ER_ATTEMPT_TO_USE_ZERODATE;
      goto error_exit;
    }

  day_of_year = db_get_day_of_year (year, month, day);
  DB_MAKE_INT (result, day_of_year);

  return NO_ERROR;

error_exit:
  /* This function should return NULL if src_date is an invalid parameter
   * or Zero date.
   * Clear the error generated by the function call and return null.
   */
  er_clear ();
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
  return error_status;
}

/*
 * db_get_date_weekday () - compute day of week from a date type value
 *
 * Arguments:
 *	  src_date: datetime from which to compute the week day
 *	  mode	  : the mode in which week days are numbered
 *		    0 = Monday, ..., 6 = Sunday or
 *		    1 = Sunday, ..., 7 = Saturday
 *
 * Returns: int
 */
int
db_get_date_weekday (const DB_VALUE * src_date, const int mode,
		     DB_VALUE * result)
{
  int error_status = NO_ERROR;
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;
  int day_of_week = 0;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* get the date/time information from src_date */
  error_status = db_get_datetime_from_dbvalue (src_date, &year, &month, &day,
					       &hour, &minute, &second, &ms,
					       NULL);
  if (error_status != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      goto error_exit;
    }

  if (year == 0 && month == 0 && day == 0
      && hour == 0 && minute == 0 && second == 0 && ms == 0)
    {
      error_status = ER_ATTEMPT_TO_USE_ZERODATE;
      goto error_exit;
    }

  /* 0 = Sunday, 1 = Monday, etc */
  day_of_week = db_get_day_of_week (year, month, day);

  switch (mode)
    {
    case PT_WEEKDAY:
      /* 0 = Monday, 1 = Tuesday, ..., 6 = Sunday */
      if (day_of_week == 0)
	{
	  day_of_week = 6;
	}
      else
	{
	  day_of_week--;
	}
      DB_MAKE_INT (result, day_of_week);
      break;

    case PT_DAYOFWEEK:
      /* 1 = Sunday, 2 = Monday, ..., 7 = Saturday */
      day_of_week++;
      DB_MAKE_INT (result, day_of_week);
      break;

    default:
      assert (false);
      DB_MAKE_NULL (result);
      break;
    }

  return NO_ERROR;

error_exit:
  /* This function should return NULL if src_date is an invalid parameter
   * or zero date.
   * Clear the error generated by the function call and return null.
   */
  er_clear ();
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
  return error_status;
}

/*
 * db_get_date_quarter () - compute quarter from a date type value
 *
 * Arguments:
 *	  src_date: datetime from which to compute the quarter
 *
 * Returns: int
 */
int
db_get_date_quarter (const DB_VALUE * src_date, DB_VALUE * result)
{
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;
  char const *endp = NULL;
  int retval;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  /* get the date/time information from src_date */
  retval = db_get_datetime_from_dbvalue (src_date, &year, &month, &day,
					 &hour, &minute, &second, &ms, &endp);
  if (retval != NO_ERROR || (endp && *endp && !char_isspace (*endp)))
    {
      er_clear ();
      DB_MAKE_NULL (result);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  if (year == 0 && month == 0 && day == 0
      && hour == 0 && minute == 0 && second == 0 && ms == 0)
    {
      /* This function should return 0 if src_date is zero date */
      DB_MAKE_INT (result, 0);
    }
  /* db_datetime_decode returned NO_ERROR so we can calculate the quarter */
  else if (month == 0)
    {
      assert (false);
      DB_MAKE_INT (result, 0);
    }
  else if (month < 0 || month > 12)
    {
      assert (false);
      DB_MAKE_NULL (result);
    }
  else
    {
      const int quarter = (month - 1) / 3 + 1;
      DB_MAKE_INT (result, quarter);
    }

  return NO_ERROR;
}

/*
 * db_get_date_totaldays () - compute the number of days from the date 0 AD
 *			      until the day represented by src_date
 *
 * Arguments:
 *	  src_date: datetime from which to compute the number of days
 *
 * Returns: int
 */
int
db_get_date_totaldays (const DB_VALUE * src_date, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;
  int leap_years = 0, total_days = 0, days_this_year = 0;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* get the date/time information from src_date */
  error_status = db_get_datetime_from_dbvalue (src_date, &year, &month, &day,
					       &hour, &minute, &second, &ms,
					       NULL);
  if (error_status != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      goto error_exit;
    }

  if (year == 0 && month == 0 && day == 0
      && hour == 0 && minute == 0 && second == 0 && ms == 0)
    {
      error_status = ER_ATTEMPT_TO_USE_ZERODATE;
      goto error_exit;
    }

  leap_years = count_leap_years_up_to (year - 1);
  days_this_year = db_get_day_of_year (year, month, day);
  total_days = year * 365 + leap_years + days_this_year;
  DB_MAKE_INT (result, total_days);

  return NO_ERROR;

error_exit:
  er_clear ();
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
  return error_status;
}

/*
 * db_get_date_from_days () - computes a date by adding the number of days
 *			      represented by src to the year 0 AD
 *
 * Arguments:
 *	  src: number from which to compute the date
 *
 * Returns: date
 */
int
db_get_date_from_days (const DB_VALUE * src, DB_VALUE * result)
{
  int year = 0;
  int month = 0;
  int day = 0;
  int week = 0;
  int int_value = 0;
  int julian_day = 0;

  if (DB_IS_NULL (src))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (db_round_dbvalue_to_int (src, &int_value) != NO_ERROR)
    {
      int_value = 0;
    }

  /* The count should start from day 0001-01-01.
   * Because year 0 is considered special,
   * this function should return 0000-00-00 for less than 366 days
   */
  if (int_value < 366)
    {
      DB_MAKE_DATE (result, 0, 0, 0);
      return NO_ERROR;
    }

  julian_day = julian_encode (1, 1, 1);

  /* Subtract 364 from the Julian Day to start counting from 0001-01-01 */
  julian_day += int_value - 364;

  julian_decode (julian_day, &month, &day, &year, &week);

  if (year > 9999)
    {
      DB_MAKE_DATE (result, 0, 0, 0);
      return NO_ERROR;
    }

  DB_MAKE_DATE (result, month, day, year);
  return NO_ERROR;
}

/*
 * db_add_days_to_year () - computes a date by adding the number of days
 *			    contained in src_days to the date 01/01/src_year
 *
 * Arguments:
 *	  src_year: the year to add days to
 *	  src_days: the number of days to add
 *
 * Returns: date
 */
int
db_add_days_to_year (const DB_VALUE * src_year, const DB_VALUE * src_days,
		     DB_VALUE * result)
{
  int year_value = 0;
  int days_value = 0;
  int julian_day = 0;
  int year = 0, month = 0, day = 0, week = 0;

  if (DB_IS_NULL (src_year) || DB_IS_NULL (src_days))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (db_round_dbvalue_to_int (src_year, &year_value) != NO_ERROR)
    {
      goto error;
    }

  if (db_round_dbvalue_to_int (src_days, &days_value) != NO_ERROR)
    {
      goto error;
    }

  /*days<=0 or year_value <0 are invalid values */
  if (days_value <= 0 || year_value < 0)
    {
      goto error;
    }

  /* correct the year value by applying the following rules:
     - if  0 <= year <= 69 then consider year as 20yy (e.g.: 33 is 2033)
     - if 70 <= year <= 99 then consider year as 19yy (e.g.: 71 is 1971)
   */
  if (year_value < 70)
    {
      year_value += 2000;
    }
  else if (year_value >= 70 && year_value < 100)
    {
      year_value += 1900;
    }

  julian_day = julian_encode (1, 1, year_value);
  julian_day += days_value - 1;
  julian_decode (julian_day, &month, &day, &year, &week);

  if (year > 9999)
    {
      goto error;
    }

  DB_MAKE_DATE (result, month, day, year);
  return NO_ERROR;

error:
  DB_MAKE_NULL (result);

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
  return ER_DATE_CONVERSION;
}

/*
 * db_convert_to_time () - creates a time value from the given hour, minute,
 *			   second
 *
 * Arguments:
 *	  src_hour: the hour
 *	  src_minute: the minute
 *	  src_second: the second
 *
 * Returns: date
 */
int
db_convert_to_time (const DB_VALUE * src_hour,
		    const DB_VALUE * src_minute,
		    const DB_VALUE * src_second, DB_VALUE * result)
{
  int hour = 0, minute = 0, second = 0;

  if (DB_IS_NULL (src_hour) || DB_IS_NULL (src_minute)
      || DB_IS_NULL (src_second))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (db_round_dbvalue_to_int (src_hour, &hour) != NO_ERROR
      || db_round_dbvalue_to_int (src_minute, &minute) != NO_ERROR
      || db_round_dbvalue_to_int (src_second, &second) != NO_ERROR)
    {
      goto error;
    }

  if (minute >= 60 || minute < 0 || second >= 60 || second < 0
      || hour >= 24 || hour < 0)
    {
      goto error;
    }

  DB_MAKE_TIME (result, hour, minute, second);
  return NO_ERROR;

error:
  DB_MAKE_NULL (result);

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
  return ER_TIME_CONVERSION;
}

/*
 * db_convert_sec_to_time() - convert a value that represents a number of
 *			      seconds into a time value
 *			      (hours:minutes:seconds)
 *
 * Arguments:
 *	  src : value to be converted to the time value
 *
 * Returns: int
 *
 * Note:
 *  This function returns values in the interval 00:00:00, 23:59:59. If the
 *  value passed as argument does not fall in this interval then this function
 *  returns the nearest interval limit.
 *
 */
int
db_convert_sec_to_time (const DB_VALUE * src, DB_VALUE * result)
{
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int int_value = 0;
  int err;

  if (DB_IS_NULL (src))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  err = db_round_dbvalue_to_int (src, &int_value);
  if (err != NO_ERROR)
    {
      int_value = 0;
    }

  if (int_value < 0 || err == ER_OUT_OF_VIRTUAL_MEMORY)
    {
      goto error;
    }

  hours = int_value / 3600;
  minutes = (int_value - hours * 3600) / 60;
  seconds = int_value % 60;

  if (hours > 23)
    {
      goto error;
    }

  DB_MAKE_TIME (result, hours, minutes, seconds);
  return NO_ERROR;

error:
  DB_MAKE_NULL (result);

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
  return ER_TIME_CONVERSION;
}

/*
 * db_convert_time_to_sec () - compute the number of seconds that have elapsed
 *			       since 00:00:00 to a given time
 *
 * Arguments:
 *	  src_date: time from which to compute the number of seconds
 *
 * Returns: int
 */
int
db_convert_time_to_sec (const DB_VALUE * src_date, DB_VALUE * result)
{
  int error_status = NO_ERROR;
  int second = 0, minute = 0, hour = 0, millisecond = 0;
  int total_seconds = 0;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  error_status = db_get_time_from_dbvalue (src_date, &hour, &minute, &second,
					   &millisecond);
  if (error_status != NO_ERROR)
    {
      er_clear ();
      DB_MAKE_NULL (result);

      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
      return ER_TIME_CONVERSION;
    }

  total_seconds = hour * 3600 + minute * 60 + second;
  DB_MAKE_INT (result, total_seconds);

  return NO_ERROR;
}

/*
 * db_round_dbvalue_to_int() - converts a db value to an integer rounding the
 *			       value to the nearest integer
 *
 * Arguments:
 *	  src : value to be converted to int
 * Return: NO_ERROR or error code.
 * Note: for string source values the function will return the converted
 *  number or 0 otherwise.
 */
static int
db_round_dbvalue_to_int (const DB_VALUE * src, int *result)
{
  DB_TYPE src_type = DB_VALUE_DOMAIN_TYPE (src);

  switch (src_type)
    {
    case DB_TYPE_SMALLINT:
      *result = DB_GET_SMALLINT (src);
      return NO_ERROR;

    case DB_TYPE_INTEGER:
      *result = DB_GET_INT (src);
      return NO_ERROR;

    case DB_TYPE_FLOAT:
      {
	float x = DB_GET_FLOAT (src);
	*result = (int) ((x) > 0 ? ((x) + .5) : ((x) - .5));
	return NO_ERROR;
      }

    case DB_TYPE_DOUBLE:
      {
	double x = DB_GET_DOUBLE (src);
	*result = (int) ((x) > 0 ? ((x) + .5) : ((x) - .5));
	return NO_ERROR;
      }

    case DB_TYPE_NUMERIC:
      {
	double x = 0;
	numeric_coerce_num_to_double (db_locate_numeric ((DB_VALUE *) src),
				      DB_VALUE_SCALE (src), &x);
	*result = (int) ((x) > 0 ? ((x) + .5) : ((x) - .5));
	return NO_ERROR;
      }

    case DB_TYPE_BIGINT:
      *result = (int) DB_GET_BIGINT (src);
      return NO_ERROR;

    case DB_TYPE_MONETARY:
      {
	double x = (DB_GET_MONETARY (src))->amount;
	*result = (int) ((x) > 0 ? ((x) + .5) : ((x) - .5));
	return NO_ERROR;
      }

    case DB_TYPE_STRING:
    case DB_TYPE_CHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
      {
	double x;
	DB_VALUE val;
	int error_status = tp_value_string_to_double (src, &val);

	if (error_status != NO_ERROR)
	  {
	    return error_status;
	  }

	x = DB_GET_DOUBLE (&val);
	*result = (int) ((x) > 0 ? ((x) + .5) : ((x) - .5));
	return NO_ERROR;
      }

    case DB_TYPE_DATE:
      {
	/* convert the date to yyyymmdd as integer */
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	int ms = 0;
	if (db_get_datetime_from_dbvalue
	    (src, &year, &month, &day, &hour, &second, &minute, &ms,
	     NULL) != NO_ERROR)
	  {
	    er_clear ();
	    *result = 0;
	  }
	else
	  {
	    *result = (int) (year * 10000 * month * 100 * day);
	  }
	return NO_ERROR;
      }
    case DB_TYPE_DATETIME:
    case DB_TYPE_TIMESTAMP:
      {
	/* convert the date to yyyymmddhhmmss as integer */
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	int ms = 0;

	if (db_get_datetime_from_dbvalue
	    (src, &year, &month, &day, &hour, &second, &minute, &ms,
	     NULL) != NO_ERROR)
	  {
	    er_clear ();
	    *result = 0;
	    return ER_FAILED;
	  }

	*result = (int) (year * 10000000000 + month * 100000000
			 + day * 1000000 + hour * 10000 + minute * 100
			 + second);
	return NO_ERROR;
      }

    case DB_TYPE_TIME:
      {
	int hour = 0, minute = 0, second = 0, millisecond = 0;
	if (db_get_time_from_dbvalue
	    (src, &hour, &minute, &second, &millisecond) != NO_ERROR)
	  {
	    er_clear ();
	    *result = 0;
	    return ER_FAILED;
	  }

	*result = hour * 10000 + minute * 100 + second;
	return NO_ERROR;
      }

    default:
      *result = 0;
      return ER_FAILED;
    }

  *result = 0;
  return ER_FAILED;
}

/*
 * db_get_date_week () - compute the week number of a given date time
 *
 * Arguments:
 *	  src_date: datetime from which to compute the week number
 *	  mode: specifies the mode in which to count the weeks
 *
 * Returns: int
 */
int
db_get_date_week (const DB_VALUE * src_date, const DB_VALUE * mode,
		  DB_VALUE * result)
{
  int error_status = NO_ERROR;
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;
  int calc_mode = prm_get_integer_value (PRM_ID_DEFAULT_WEEK_FORMAT);
  int week_number = 0;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* get the date/time information from src_date */
  error_status = db_get_datetime_from_dbvalue (src_date, &year, &month, &day,
					       &hour, &minute, &second, &ms,
					       NULL);
  if (error_status != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      goto error_exit;
    }

  if (year == 0 && month == 0 && day == 0
      && hour == 0 && minute == 0 && second == 0 && ms == 0)
    {
      error_status = ER_ATTEMPT_TO_USE_ZERODATE;
      goto error_exit;
    }

  if (DB_IS_NULL (mode))
    {
      calc_mode = prm_get_integer_value (PRM_ID_DEFAULT_WEEK_FORMAT);
    }
  else
    {
      error_status = db_round_dbvalue_to_int (mode, &calc_mode);
      if (error_status != NO_ERROR)
	{
	  error_status = ER_DATE_CONVERSION;
	  goto error_exit;
	}
    }
  /* check boundaries for calc_mode */
  if (calc_mode < 0 || calc_mode > 7)
    {
      error_status = ER_DATE_CONVERSION;
      goto error_exit;
    }

  week_number = db_get_week_of_year (year, month, day, calc_mode);

  DB_MAKE_INT (result, week_number);
  return NO_ERROR;

error_exit:
  er_clear ();
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
  return error_status;
}

/*
 * db_get_date_item () - compute an item from a datetime value
 *
 * Arguments:
 *	  src_date: datetime from which to calculate the item
 *	  item_type: one of year, month, day
 *
 * Returns: int
 */
int
db_get_date_item (const DB_VALUE * src_date, const int item_type,
		  DB_VALUE * result)
{
  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0;
  int ms = 0;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* get the date/time information from src_date */
  if (db_get_datetime_from_dbvalue (src_date, &year, &month, &day, &hour,
				    &minute, &second, &ms, NULL) != NO_ERROR)
    {
      /* This function should return NULL if src_date is an invalid parameter.
         Clear the error generated by the function call and return null.
       */
      er_clear ();
      DB_MAKE_NULL (result);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      /* set ER_DATE_CONVERSION */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  switch (item_type)
    {
    case PT_YEARF:
      DB_MAKE_INT (result, year);
      break;
    case PT_MONTHF:
      DB_MAKE_INT (result, month);
      break;
    case PT_DAYF:
      DB_MAKE_INT (result, day);
      break;
    default:
      assert (false);
      DB_MAKE_NULL (result);
      break;
    }

  return NO_ERROR;
}

/*
 * db_get_time_item () - compute an item from a datetime value
 *
 * Arguments:
 *	  src_date: datetime from which to calculate the item
 *	  item_type: one of hour, minute, second
 *
 * Returns: int
 */
int
db_get_time_item (const DB_VALUE * src_date, const int item_type,
		  DB_VALUE * result)
{
  int second = 0, minute = 0, hour = 0, millisecond = 0;

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (db_get_time_from_dbvalue (src_date, &hour, &minute, &second,
				&millisecond) != NO_ERROR)
    {
      er_clear ();
      DB_MAKE_NULL (result);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
      return ER_TIME_CONVERSION;
    }

  switch (item_type)
    {
    case PT_HOURF:
      DB_MAKE_INT (result, hour);
      break;
    case PT_MINUTEF:
      DB_MAKE_INT (result, minute);
      break;
    case PT_SECONDF:
      DB_MAKE_INT (result, second);
      break;
    default:
      assert (false);
      DB_MAKE_NULL (result);
      break;
    }

  return NO_ERROR;
}


/*
 * db_time_format ()
 *
 * Arguments:
 *         time_value: time from which we get the informations
 *         format: format specifiers string
 *	   result: output string
 *	   domain: domain of result
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *     This is used like the DATE_FORMAT() function, but the format
 *  string may contain format specifiers only for hours, minutes, seconds, and
 *  milliseconds. Other specifiers produce a NULL value or 0.
 */
int
db_time_format (const DB_VALUE * time_value, const DB_VALUE * format,
		const DB_VALUE * date_lang, DB_VALUE * result,
		const TP_DOMAIN * domain)
{
  DB_TIME db_time, *t_p;
  DB_TIMESTAMP *ts_p;
  DB_DATETIME *dt_p;
  DB_DATE db_date;
  DB_TYPE res_type, format_type;
  char *res, *res2, *format_s;
  char *strend;
  int format_s_len;
  int error_status = NO_ERROR, len;
  int h, mi, s, ms, year, month, day;
  char format_specifiers[256][64];
  int is_date, is_datetime, is_timestamp, is_time;
  char och = -1, ch;
  INTL_LANG date_lang_id;
  const LANG_LOCALE_DATA *lld;
  bool dummy;
  INTL_CODESET codeset;
  int res_collation;

  is_date = is_datetime = is_timestamp = is_time = 0;
  h = mi = s = ms = 0;
  memset (format_specifiers, 0, sizeof (format_specifiers));

  if (time_value == NULL || format == NULL
      || DB_IS_NULL (time_value) || DB_IS_NULL (format))
    {
      DB_MAKE_NULL (result);
      goto error;
    }

  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang), &dummy,
					     &dummy);
  if (domain != NULL && domain->collation_flag != TP_DOMAIN_COLL_LEAVE)
    {
      codeset = TP_DOMAIN_CODESET (domain);
      res_collation = TP_DOMAIN_COLLATION (domain);
    }
  else
    {
      codeset = DB_GET_STRING_CODESET (format);
      res_collation = DB_GET_STRING_COLLATION (format);
    }

  lld = lang_get_specific_locale (date_lang_id, codeset);
  if (lld == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (date_lang_id),
	      lang_charset_name (codeset));
      goto error;
    }

  res_type = DB_VALUE_DOMAIN_TYPE (time_value);

  /* 1. Get date values */
  switch (res_type)
    {
    case DB_TYPE_TIMESTAMP:
      ts_p = DB_GET_TIMESTAMP (time_value);
      db_timestamp_decode (ts_p, &db_date, &db_time);
      db_time_decode (&db_time, &h, &mi, &s);
      break;

    case DB_TYPE_DATETIME:
      dt_p = DB_GET_DATETIME (time_value);
      db_datetime_decode (dt_p, &month, &day, &year, &h, &mi, &s, &ms);
      break;

    case DB_TYPE_TIME:
      t_p = DB_GET_TIME (time_value);
      db_time_decode (t_p, &h, &mi, &s);
      break;

    case DB_TYPE_STRING:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      {
	DB_VALUE tm;
	TP_DOMAIN *tp_datetime = db_type_to_db_domain (DB_TYPE_TIME);

	if (tp_value_cast (time_value, &tm, tp_datetime, false)
	    != DOMAIN_COMPATIBLE)
	  {
	    error_status = ER_QSTR_INVALID_DATA_TYPE;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    goto error;
	  }
	db_time_decode (DB_GET_TIME (&tm), &h, &mi, &s);
      }
      break;

    default:
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error;
    }

  /* 2. Compute the value for each format specifier */
  if (mi < 0)
    {
      mi = -mi;
    }
  if (s < 0)
    {
      s = -s;
    }
  if (ms < 0)
    {
      ms = -ms;
    }

  /* %f       Milliseconds (000..999) */
  sprintf (format_specifiers['f'], "%03d", ms);

  /* %H       Hour (00..23) */
  if (h < 0)
    {
      sprintf (format_specifiers['H'], "-%02d", -h);
    }
  else
    {
      sprintf (format_specifiers['H'], "%02d", h);
    }
  if (h < 0)
    {
      h = -h;
    }

  /* %h       Hour (01..12) */
  sprintf (format_specifiers['h'], "%02d", (h % 12 == 0) ? 12 : (h % 12));

  /* %I       Hour (01..12) */
  sprintf (format_specifiers['I'], "%02d", (h % 12 == 0) ? 12 : (h % 12));

  /* %i       Minutes, numeric (00..59) */
  sprintf (format_specifiers['i'], "%02d", mi);

  /* %k       Hour (0..23) */
  sprintf (format_specifiers['k'], "%d", h);

  /* %l       Hour (1..12) */
  sprintf (format_specifiers['l'], "%d", (h % 12 == 0) ? 12 : (h % 12));

  /* %p       AM or PM */
  strcpy (format_specifiers['p'],
	  (h > 11) ? lld->am_pm[PM_NAME] : lld->am_pm[AM_NAME]);

  /* %r       Time, 12-hour (hh:mm:ss followed by AM or PM) */
  sprintf (format_specifiers['r'], "%02d:%02d:%02d %s",
	   (h % 12 == 0) ? 12 : (h % 12), mi, s,
	   (h > 11) ? lld->am_pm[PM_NAME] : lld->am_pm[AM_NAME]);

  /* %S       Seconds (00..59) */
  sprintf (format_specifiers['S'], "%02d", s);

  /* %s       Seconds (00..59) */
  sprintf (format_specifiers['s'], "%02d", s);

  /* %T       Time, 24-hour (hh:mm:ss) */
  sprintf (format_specifiers['T'], "%02d:%02d:%02d", h, mi, s);

  /* 3. Generate the output according to the format and the values */
  format_type = DB_VALUE_DOMAIN_TYPE (format);
  switch (format_type)
    {
    case DB_TYPE_STRING:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      format_s = DB_PULL_STRING (format);
      format_s_len = DB_GET_STRING_SIZE (format);
      break;

    default:
      /* we should not get a nonstring format */
      assert (false);
      return ER_FAILED;
    }

  len = 1024;
  res = (char *) db_private_alloc (NULL, len);
  if (!res)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  memset (res, 0, len);

  ch = *format_s;
  strend = format_s + format_s_len;

  while (format_s < strend)
    {
      format_s++;
      och = ch;
      ch = *format_s;

      if (och == '%' /* && (res[strlen(res) - 1] != '%') */ )
	{
	  if (ch == '%')
	    {
	      STRCHCAT (res, '%');

	      /* jump a character */
	      format_s++;
	      och = ch;
	      ch = *format_s;

	      continue;
	    }
	  /* parse the character */
	  if (strlen (format_specifiers[(unsigned char) ch]) == 0)
	    {
	      /* append the character itself */
	      STRCHCAT (res, ch);
	    }
	  else
	    {
	      strcat (res, format_specifiers[(unsigned char) ch]);
	    }

	  /* jump a character */
	  format_s++;
	  och = ch;
	  ch = *format_s;
	}
      else
	{
	  STRCHCAT (res, och);
	}

      /* chance of overflow ? */
      /* assume we can't add at a time mode than 16 chars */
      if (strlen (res) + 16 > len)
	{
	  /* realloc - copy temporary in res2 */
	  res2 = (char *) db_private_alloc (NULL, len);
	  if (!res2)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  memset (res2, 0, len);
	  strcpy (res2, res);
	  db_private_free_and_init (NULL, res);

	  len += 1024;
	  res = (char *) db_private_alloc (NULL, len);
	  if (!res)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  memset (res, 0, len);
	  strcpy (res, res2);
	  db_private_free_and_init (NULL, res2);
	}
    }
  /* finished string */

  /* 4. */

  DB_MAKE_STRING (result, res);
  db_string_put_cs_and_collation (result, codeset, res_collation);

  result->need_clear = true;

error:
  /* do not free res as it was moved to result and will be freed later */
  return error_status;
}

/*
 * db_timestamp() -
 *
 * Arguments:
 *         src_datetime1: date or datetime expression
 *         src_time2: time expression
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 * This function is used in the function TIMESTAMP().
 * It returns the date or datetime expression expr as a datetime value.
 * With both arguments, it adds the time expression src_time2 to the date or
 * datetime expression src_datetime1 and returns the result as a datetime value.
 */
int
db_timestamp (const DB_VALUE * src_datetime1, const DB_VALUE * src_time2,
	      DB_VALUE * result_datetime)
{
  int error_status = NO_ERROR;
  int year, month, day, hour, minute, second, millisecond;
  int y = 0, m = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
  DB_BIGINT amount = 0;
  double amount_d = 0;
  DB_TYPE type;
  DB_DATETIME datetime, calculated_datetime;
  /* if sign is 1 then we perform a subtraction */
  int sign = 0;

  if (result_datetime == (DB_VALUE *) NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER,
	      0);
      return ER_QPROC_INVALID_PARAMETER;
    }

  db_value_domain_init (result_datetime, DB_TYPE_DATETIME,
			DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  /* Return NULL if NULL is explicitly given as the second argument.
   * If no second argument is given, we consider it as 0.
   */
  if (DB_IS_NULL (src_datetime1) || (src_time2 && DB_IS_NULL (src_time2)))
    {
      DB_MAKE_NULL (result_datetime);
      return NO_ERROR;
    }

  year = month = day = hour = minute = second = millisecond = 0;

  error_status =
    db_get_datetime_from_dbvalue (src_datetime1, &year, &month, &day, &hour,
				  &minute, &second, &millisecond, NULL);
  if (error_status != NO_ERROR)
    {
      return error_status;
    }
  /* If no second argument is given, just encode the first argument. */
  if (src_time2 == NULL)
    {
      goto encode_result;
    }

  type = DB_VALUE_DOMAIN_TYPE (src_time2);
  switch (type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      parse_time_string ((const char *) DB_GET_STRING (src_time2),
			 DB_GET_STRING_SIZE (src_time2), &sign, &h, &mi, &s,
			 &ms);
      break;

    case DB_TYPE_TIME:
      db_time_decode (DB_GET_TIME (src_time2), &h, &mi, &s);
      break;

    case DB_TYPE_SMALLINT:
      amount = (DB_BIGINT) DB_GET_SMALLINT (src_time2);
      break;

    case DB_TYPE_INTEGER:
      amount = (DB_BIGINT) DB_GET_INTEGER (src_time2);
      break;

    case DB_TYPE_BIGINT:
      amount = DB_GET_BIGINT (src_time2);
      break;

    case DB_TYPE_FLOAT:
      amount_d = DB_GET_FLOAT (src_time2);
      break;

    case DB_TYPE_DOUBLE:
      amount_d = DB_GET_DOUBLE (src_time2);
      break;

    case DB_TYPE_MONETARY:
      amount_d = db_value_get_monetary_amount_as_double (src_time2);
      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double ((DB_C_NUMERIC)
				    db_locate_numeric (src_time2),
				    DB_VALUE_SCALE (src_time2), &amount_d);
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INVALID_DATA_TYPE, 0);
      return ER_QSTR_INVALID_DATA_TYPE;
    }

  if (type == DB_TYPE_DOUBLE || type == DB_TYPE_FLOAT ||
      type == DB_TYPE_MONETARY || type == DB_TYPE_NUMERIC)
    {
      amount = (DB_BIGINT) amount_d;
      ms = ((long) (amount_d * 1000.0)) % 1000;
    }

  if (type != DB_TYPE_VARCHAR && type != DB_TYPE_CHAR && type != DB_TYPE_TIME)
    {
      if (amount < 0)
	{
	  amount = -amount;
	  ms = -ms;
	  sign = 1;
	}
      s = (int) ((DB_BIGINT) amount % 100);
      amount /= 100;
      mi = (int) ((DB_BIGINT) amount % 100);
      amount /= 100;
      h = (int) amount;
    }

  /* validation of minute and second */
  if ((mi < 0 || mi > 59) || (s < 0 || s > 59))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_QPROC_INVALID_PARAMETER, 0);
      return ER_QPROC_INVALID_PARAMETER;
    }

  /* Convert time to milliseconds. */
  amount =
    ((DB_BIGINT) h * 60 * 60 * 1000) + ((DB_BIGINT) mi * 60 * 1000) +
    ((DB_BIGINT) s * 1000) + (DB_BIGINT) ms;

encode_result:

  db_datetime_encode (&datetime,
		      month, day, year, hour, minute, second, millisecond);
  if (amount > 0)
    {
      if (sign == 0)
	{
	  error_status =
	    db_add_int_to_datetime (&datetime, amount, &calculated_datetime);
	}
      else
	{
	  error_status =
	    db_subtract_int_from_datetime (&datetime, amount,
					   &calculated_datetime);
	}
      if (error_status != NO_ERROR)
	{
	  return error_status;
	}

      db_make_datetime (result_datetime, &calculated_datetime);
    }
  else
    {
      db_make_datetime (result_datetime, &datetime);
    }

  return error_status;
}

/*
 * db_add_months () -
 */
int
db_add_months (const DB_VALUE * src_date,
	       const DB_VALUE * nmonth, DB_VALUE * result_date)
{
  int error_status = NO_ERROR;
  int n;
  int month, day, year;
  int old_month, old_year;

  assert (src_date != (DB_VALUE *) NULL);
  assert (nmonth != (DB_VALUE *) NULL);
  assert (result_date != (DB_VALUE *) NULL);

  db_value_domain_init (result_date, DB_TYPE_DATE, DB_DEFAULT_PRECISION,
			DB_DEFAULT_SCALE);

  if (DB_IS_NULL (src_date) || DB_IS_NULL (nmonth))
    {
      DB_MAKE_NULL (result_date);
      return error_status;
    }

  n = DB_GET_INTEGER (nmonth);
  db_date_decode ((DB_DATE *) & src_date->data.date, &month, &day, &year);

  old_month = month;
  old_year = year;

  if ((month + n) >= 0)		/* Calculate month,year */
    {
      year = year + (month + n) / 12;
      month = (month + n) % 12;
      year = (month == 0) ? year - 1 : year;
      month = (month == 0) ? 12 : month;
    }
  else
    {
      year = year + (month + n - 12) / 12;
      month = 12 + (month + n) % 12;
    }
  /* Check last day of month */
  if (day == get_last_day (old_month, old_year)
      || day > get_last_day (month, year))
    {
      day = get_last_day (month, year);
    }

  if (0 < year && year < 10000)
    {
      DB_MAKE_DATE (result_date, month, day, year);
    }
  else
    {
      error_status = ER_DATE_EXCEED_LIMIT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }
  return error_status;
}

/*
 * db_last_day () -
 */
int
db_last_day (const DB_VALUE * src_date, DB_VALUE * result_day)
{
  int error_status = NO_ERROR;
  int month, day, year;
  int lastday;

  assert (src_date != (DB_VALUE *) NULL);
  assert (result_day != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src_date))
    {
      DB_MAKE_NULL (result_day);
      return error_status;
    }

  db_date_decode ((DB_DATE *) & src_date->data.date, &month, &day, &year);

  if (month == 0 && day == 0 && year == 0)
    {
      DB_MAKE_NULL (result_day);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_ATTEMPT_TO_USE_ZERODATE, 0);
      return ER_ATTEMPT_TO_USE_ZERODATE;
    }

  lastday = get_last_day (month, year);

  DB_MAKE_DATE (result_day, month, lastday, year);

  return error_status;
}

/*
 * db_months_between () -
 */
int
db_months_between (const DB_VALUE * start_mon,
		   const DB_VALUE * end_mon, DB_VALUE * result_mon)
{
  int error_status = NO_ERROR;
  double result_double;
  int start_month, start_day, start_year;
  int end_month, end_day, end_year;
  DB_DATE *start_date, *end_date;

  assert (start_mon != (DB_VALUE *) NULL);
  assert (end_mon != (DB_VALUE *) NULL);
  assert (result_mon != (DB_VALUE *) NULL);

  /* now return null */

  if (DB_IS_NULL (start_mon) || DB_IS_NULL (end_mon))
    {
      DB_MAKE_NULL (result_mon);
      return error_status;
    }

  db_date_decode ((DB_DATE *) & start_mon->data.date, &start_month,
		  &start_day, &start_year);
  db_date_decode ((DB_DATE *) & end_mon->data.date, &end_month, &end_day,
		  &end_year);

  if (start_day == end_day
      || (start_day == get_last_day (start_month, start_year)
	  && end_day == get_last_day (end_month, end_year)))
    {
      result_double = (double) (start_year * 12 + start_month -
				end_year * 12 - end_month);
    }
  else
    {
      start_date = DB_GET_DATE (start_mon);
      end_date = DB_GET_DATE (end_mon);

      result_double = (double) ((start_year - end_year) * 12.0) +
	(double) (start_month - end_month) +
	(double) ((start_day - end_day) / 31.0);
    }

  DB_MAKE_DOUBLE (result_mon, result_double);

  return error_status;
}

/*
 * db_sys_date () -
 */
int
db_sys_date (DB_VALUE * result_date)
{
  int error_status = NO_ERROR;
  time_t tloc;
  struct tm *c_time_struct, tm_val;

  assert (result_date != (DB_VALUE *) NULL);

  /* now return null */
  db_value_domain_init (result_date, DB_TYPE_DATE, DB_DEFAULT_PRECISION,
			DB_DEFAULT_SCALE);

  /* Need checking error */

  if (time (&tloc) == -1)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  c_time_struct = localtime_r (&tloc, &tm_val);
  if (c_time_struct == NULL)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  DB_MAKE_DATE (result_date, c_time_struct->tm_mon + 1,
		c_time_struct->tm_mday, c_time_struct->tm_year + 1900);

  return error_status;
}

/*
 * db_sys_time () -
 */
int
db_sys_time (DB_VALUE * result_time)
{
  int error_status = NO_ERROR;
  time_t tloc;
  struct tm *c_time_struct, tm_val;

  assert (result_time != (DB_VALUE *) NULL);

  /* now return null */
  db_value_domain_init (result_time, DB_TYPE_TIME, DB_DEFAULT_PRECISION,
			DB_DEFAULT_SCALE);

  /* Need checking error */

  if (time (&tloc) == -1)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  c_time_struct = localtime_r (&tloc, &tm_val);
  if (c_time_struct == NULL)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }
  DB_MAKE_TIME (result_time, c_time_struct->tm_hour, c_time_struct->tm_min,
		c_time_struct->tm_sec);

  return error_status;
}

/*
 * db_sys_timestamp () -
 */
int
db_sys_timestamp (DB_VALUE * result_timestamp)
{
  int error_status = NO_ERROR;
  time_t tloc;

  assert (result_timestamp != (DB_VALUE *) NULL);

  /* now return null */
  db_value_domain_init (result_timestamp, DB_TYPE_TIMESTAMP,
			DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  if (time (&tloc) == -1 || OR_CHECK_INT_OVERFLOW (tloc))
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  DB_MAKE_TIMESTAMP (result_timestamp, (DB_TIMESTAMP) tloc);

  return error_status;
}

/*
 * db_sys_datetime () -
 */
int
db_sys_datetime (DB_VALUE * result_datetime)
{
  int error_status = NO_ERROR;
  DB_DATETIME datetime;

  struct timeb tloc;
  struct tm *c_time_struct, tm_val;

  assert (result_datetime != (DB_VALUE *) NULL);

  /* now return null */
  db_value_domain_init (result_datetime, DB_TYPE_DATETIME,
			DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  if (ftime (&tloc) != 0)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  c_time_struct = localtime_r (&tloc.time, &tm_val);
  if (c_time_struct == NULL)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  db_datetime_encode (&datetime, c_time_struct->tm_mon + 1,
		      c_time_struct->tm_mday, c_time_struct->tm_year + 1900,
		      c_time_struct->tm_hour, c_time_struct->tm_min,
		      c_time_struct->tm_sec, tloc.millitm);
  DB_MAKE_DATETIME (result_datetime, &datetime);

  return error_status;
}

/*
 * db_sys_date_and_epoch_time () - This function returns current 
 *				   datetime and timestamp.
 *
 * return: status of the error
 *
 *   dt_dbval(out): datetime
 *   ts_dbval(out): timestamp
 */

int
db_sys_date_and_epoch_time (DB_VALUE * dt_dbval, DB_VALUE * ts_dbval)
{
  int error_status = NO_ERROR;
  DB_DATETIME datetime;
  struct timeb tloc;
  struct tm *c_time_struct, tm_val;

  assert (dt_dbval != NULL);
  assert (ts_dbval != NULL);

  if (ftime (&tloc) != 0)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  c_time_struct = localtime_r (&tloc.time, &tm_val);
  if (c_time_struct == NULL)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  db_datetime_encode (&datetime, c_time_struct->tm_mon + 1,
		      c_time_struct->tm_mday, c_time_struct->tm_year + 1900,
		      c_time_struct->tm_hour, c_time_struct->tm_min,
		      c_time_struct->tm_sec, tloc.millitm);

  DB_MAKE_DATETIME (dt_dbval, &datetime);
  DB_MAKE_TIMESTAMP (ts_dbval, (DB_TIMESTAMP) tloc.time);

  return error_status;
}

/*
 * This function return the current timezone , as an integer representing
 * the minutes away from GMT
 */
int
db_sys_timezone (DB_VALUE * result_timezone)
{
  int error_status = NO_ERROR;
  struct timeb tloc;

  assert (result_timezone != (DB_VALUE *) NULL);

  /* now return null */
  db_value_domain_init (result_timezone, DB_TYPE_INTEGER,
			DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  /* Need checking error                  */

  if (ftime (&tloc) == -1)
    {
      error_status = ER_SYSTEM_DATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  DB_MAKE_INTEGER (result_timezone, tloc.timezone);
  return NO_ERROR;
}

/*
 * get_last_day () -
 */
int
get_last_day (int month, int year)
{
  int lastday = 0;

  if (year >= 1700)
    {
      switch (month)
	{
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
	  lastday = 31;
	  break;
	case 4:
	case 6:
	case 9:
	case 11:
	  lastday = 30;
	  break;
	case 2:
	  if (year % 4 == 0)
	    {
	      if (year % 100 == 0)
		{
		  if (year % 400 == 0)
		    {
		      lastday = 29;
		    }
		  else
		    {
		      lastday = 28;
		    }
		}
	      else
		{
		  lastday = 29;
		}
	    }
	  else
	    {
	      lastday = 28;
	    }
	  break;
	default:
	  break;		/*  Need Error Checking          */
	}
    }
  else
    {
      switch (month)
	{
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
	  lastday = 31;
	  break;
	case 4:
	case 6:
	case 9:
	case 11:
	  lastday = 30;
	  break;
	case 2:
	  if (year % 4 == 0)
	    {
	      lastday = 29;
	    }
	  else
	    {
	      lastday = 28;
	    }
	  break;
	default:
	  break;		/*  Need Error Checking          */
	}
    }
  return lastday;
}

/*
 * db_to_char () -
 */
int
db_to_char (const DB_VALUE * src_value,
	    const DB_VALUE * format_or_length,
	    const DB_VALUE * lang_str, DB_VALUE * result_str,
	    const TP_DOMAIN * domain)
{
  int error_status = NO_ERROR;
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (type == DB_TYPE_NULL || is_number (src_value))
    {
      return number_to_char (src_value, format_or_length, lang_str,
			     result_str, domain);
    }
  else if (TP_IS_DATE_OR_TIME_TYPE (type))
    {
      return date_to_char (src_value, format_or_length, lang_str, result_str,
			   domain);
    }
  else if (TP_IS_CHAR_TYPE (type))
    {
      if (domain == NULL)
	{
	  error_status = pr_clone_value (src_value, result_str);
	  if (error_status != NO_ERROR)
	    {
	      return error_status;
	    }
	  db_string_put_cs_and_collation (result_str, LANG_COERCIBLE_CODESET,
					  LANG_COERCIBLE_COLL);
	}
      else
	{
	  error_status = db_value_coerce (src_value, result_str, domain);
	}

      return error_status;
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);

      return error_status;
    }
}

#define MAX_STRING_DATE_TOKEN_LEN  LOC_DATA_MONTH_WIDE_SIZE
const char *Month_name_ISO[][12] = {
  {"January", "February", "March", "April",
   "May", "June", "July", "August", "September", "October",
   "November", "December"},	/* US */
  {"1wol",
   "2wol",
   "3wol",
   "4wol",
   "5wol",
   "6wol",
   "7wol",
   "8wol",
   "9wol",
   "10wol",
   "11wol",
   "12wol"},			/* KR */
  {"Ocak",
   "Subat",
   "Mart",
   "Nisan",
   "Mayis",
   "Haziran",
   "Temmuz",
   "Agustos",
   "Eylul",
   "Ekim",
   "Kasim",
   "Aralik"}			/* TR */
};

const char *Month_name_UTF8[][12] = {
  {"January", "February", "March", "April",
   "May", "June", "July", "August", "September", "October",
   "November", "December"},	/* US */
  {"1\xec\x9b\x94",
   "2\xec\x9b\x94",
   "3\xec\x9b\x94",
   "4\xec\x9b\x94",
   "5\xec\x9b\x94",
   "6\xec\x9b\x94",
   "7\xec\x9b\x94",
   "8\xec\x9b\x94",
   "9\xec\x9b\x94",
   "10\xec\x9b\x94",
   "11\xec\x9b\x94",
   "12\xec\x9b\x94"},		/* KR */
  {"Ocak",
   "\xc5\x9e" "ubat",
   "Mart",
   "Nisan",
   "May" "\xc4\xb1" "s",
   "Haziran",
   "Temmuz",
   "A" "\xc4\x9f" "ustos",
   "Eyl" "\xc3\xbc" "l",
   "Ekim",
   "Kas" "\xc4\xb1" "m",
   "Aral" "\xc4\xb1" "k"}	/* TR */
};

const char *Month_name_EUCKR[][12] = {
  {"January", "February", "March", "April",
   "May", "June", "July", "August", "September", "October",
   "November", "December"},	/* US */
  {"1\xbf\xf9",
   "2\xbf\xf9",
   "3\xbf\xf9",
   "4\xbf\xf9",
   "5\xbf\xf9",
   "6\xbf\xf9",
   "7\xbf\xf9",
   "8\xbf\xf9",
   "9\xbf\xf9",
   "10\xbf\xf9",
   "11\xbf\xf9",
   "12\xbf\xf9"},		/* KR */
  {"Ocak",
   "Subat",
   "Mart",
   "Nisan",
   "Mayis",
   "Haziran",
   "Temmuz",
   "Agustos",
   "Eylul",
   "Ekim",
   "Kasim",
   "Aralik"}			/* TR */
};

const char Month_name_parse_order[][12] = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
};

const char *Day_name_ISO[][7] = {
  {"Sunday", "Monday", "Tuesday", "Wednesday",
   "Thursday", "Friday", "Saturday"},	/* US */
  {"Iryoil",
   "Woryoil",
   "Hwayoil",
   "Suyoil",
   "Mogyoil",
   "Geumyoil",
   "Toyoil"},			/* KR */
  {"Pazar", "Pazartesi", "Sali",
   "Carsamba",
   "Persembe", "Cuma",
   "Cumartesi"}			/* TR */
};

const char *Day_name_UTF8[][7] = {
  {"Sunday", "Monday", "Tuesday", "Wednesday",
   "Thursday", "Friday", "Saturday"},	/* US */
  {"\xec\x9d\xbc\xec\x9a\x94\xec\x9d\xbc",
   "\xec\x9b\x94\xec\x9a\x94\xec\x9d\xbc",
   "\xed\x99\x94\xec\x9a\x94\xec\x9d\xbc",
   "\xec\x88\x98\xec\x9a\x94\xec\x9d\xbc",
   "\xeb\xaa\xa9\xec\x9a\x94\xec\x9d\xbc",
   "\xea\xb8\x88\xec\x9a\x94\xec\x9d\xbc",
   "\xed\x86\xa0\xec\x9a\x94\xec\x9d\xbc"},	/* KR */
  {"Pazar", "Pazartesi", "Sal\xc4\xb1",
   "\xc3\x87" "ar" "\xc5\x9f" "amba",
   "Per" "\xc5\x9f" "embe", "Cuma",
   "Cumartesi"}			/* TR */
};

const char *Day_name_EUCKR[][7] = {
  {"Sunday", "Monday", "Tuesday", "Wednesday",
   "Thursday", "Friday", "Saturday"},	/* US */
  {"\xc0\xcf\xbf\xe4\xc0\xcf",
   "\xbf\xf9\xbf\xe4\xc0\xcf",
   "\xc8\xad\xbf\xe4\xc0\xcf",
   "\xbc\xf6\xbf\xe4\xc0\xcf",
   "\xb8\xf1\xbf\xe4\xc0\xcf",
   "\xb1\xdd\xbf\xe4\xc0\xcf",
   "\xc5\xe4\xbf\xe4\xc0\xcf"},	/* KR */
  {"Pazar", "Pazartesi", "Sali",
   "Carsamba",
   "Persembe", "Cuma",
   "Cumartesi"}			/* TR */
};

const char Day_name_parse_order[][7] = {
  {0, 1, 2, 3, 4, 5, 6},
  {0, 1, 2, 3, 4, 5, 6},
  {1, 0, 2, 3, 4, 6, 5}
};

const char *Short_Month_name_ISO[][12] = {
  {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},	/* US */
  {"1wol",
   "2wol",
   "3wol",
   "4wol",
   "5wol",
   "6wol",
   "7wol",
   "8wol",
   "9wol",
   "10wol",
   "11wol",
   "12wol"},			/* KR */
  {"Ock",
   "Sbt",
   "Mrt",
   "Nsn",
   "Mys",
   "Hzr",
   "Tmz",
   "Ags",
   "Eyl",
   "Ekm",
   "Ksm",
   "Arl"}			/* TR */
};

const char *Short_Month_name_UTF8[][12] = {
  {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},	/* US */
  {"1\xec\x9b\x94",
   "2\xec\x9b\x94",
   "3\xec\x9b\x94",
   "4\xec\x9b\x94",
   "5\xec\x9b\x94",
   "6\xec\x9b\x94",
   "7\xec\x9b\x94",
   "8\xec\x9b\x94",
   "9\xec\x9b\x94",
   "10\xec\x9b\x94",
   "11\xec\x9b\x94",
   "12\xec\x9b\x94"},		/* KR */
  {"Ock",
   "\xc5\x9e" "bt",
   "Mrt",
   "Nsn",
   "Mys",
   "Hzr",
   "Tmz",
   "A" "\xc4\x9f" "s",
   "Eyl",
   "Ekm",
   "Ksm",
   "Arl"}			/* TR */
};

const char *Short_Month_name_EUCKR[][12] = {
  {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},	/* US */
  {"1\xbf\xf9",
   "2\xbf\xf9",
   "3\xbf\xf9",
   "4\xbf\xf9",
   "5\xbf\xf9",
   "6\xbf\xf9",
   "7\xbf\xf9",
   "8\xbf\xf9",
   "9\xbf\xf9",
   "10\xbf\xf9",
   "11\xbf\xf9",
   "12\xbf\xf9"},		/* KR */
  {"Ock",
   "Sbt",
   "Mrt",
   "Nsn",
   "Mys",
   "Hzr",
   "Tmz",
   "Ags",
   "Eyl",
   "Ekm",
   "Ksm",
   "Arl"}			/* TR */
};

const char Short_Month_name_parse_order[][12] = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
};

const char *Short_Day_name_ISO[][7] = {
  {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},	/* US */
  {"Il",
   "Wol",
   "Hwa",
   "Su",
   "Mok",
   "Geum",
   "To"},			/* KR */
  {"Pz", "Pt", "Sa",
   "Ca",
   "Pe", "Cu", "Ct"}		/* TR */
};

const char *Short_Day_name_UTF8[][7] = {
  {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},	/* US */
  {"\xec\x9d\xbc",
   "\xec\x9b\x94",
   "\xed\x99\x94",
   "\xec\x88\x98",
   "\xeb\xaa\xa9",
   "\xea\xb8\x88",
   "\xed\x86\xa0"},		/* KR */
  {"Pz", "Pt", "Sa",
   "\xc3\x87" "a",
   "Pe", "Cu", "Ct"}		/* TR */
};

const char *Short_Day_name_EUCKR[][7] = {
  {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},	/* US */
  {"\xc0\xcf",
   "\xbf\xf9",
   "\xc8\xad",
   "\xbc\xf6",
   "\xb8\xf1",
   "\xb1\xdd",
   "\xc5\xe4"},			/* KR */
  {"Pz", "Pt", "Sa",
   "Ca",
   "Pe", "Cu", "Ct"}		/* TR */
};

const char Short_Day_name_parse_order[][7] = {
  {0, 1, 2, 3, 4, 5, 6},
  {0, 1, 2, 3, 4, 5, 6},
  {0, 1, 2, 3, 4, 5, 6}
};

#define AM_NAME_KR "ojeon"
#define PM_NAME_KR "ohu"

#define AM_NAME_KR_EUC "\xbf\xc0\xc0\xfc"
#define PM_NAME_KR_EUC "\xbf\xc0\xc8\xc4"

#define AM_NAME_KR_UTF8 "\xec\x98\xa4\xec\xa0\x84"
#define PM_NAME_KR_UTF8 "\xec\x98\xa4\xed\x9b\x84"

const char AM_PM_parse_order[][12] = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
};



const char *Am_Pm_name_ISO[][12] = {
  {"am", "pm", "Am", "Pm", "AM", "PM",
   "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."},	/* US */
  {AM_NAME_KR, PM_NAME_KR, AM_NAME_KR, PM_NAME_KR, AM_NAME_KR, PM_NAME_KR,
   AM_NAME_KR, PM_NAME_KR, AM_NAME_KR, PM_NAME_KR, AM_NAME_KR, PM_NAME_KR},
  /* KR */
  {"am", "pm", "Am", "Pm", "AM", "PM",
   "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."},	/* TR */
};

const char *Am_Pm_name_UTF8[][12] = {
  {"am", "pm", "Am", "Pm", "AM", "PM",
   "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."},	/* US */
  {AM_NAME_KR_UTF8, PM_NAME_KR_UTF8, AM_NAME_KR_UTF8,
   PM_NAME_KR_UTF8, AM_NAME_KR_UTF8, PM_NAME_KR_UTF8,
   AM_NAME_KR_UTF8, PM_NAME_KR_UTF8, AM_NAME_KR_UTF8,
   PM_NAME_KR_UTF8, AM_NAME_KR_UTF8, PM_NAME_KR_UTF8},
  /* KR */
  {"am", "pm", "Am", "Pm", "AM", "PM",
   "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."},	/* TR */
};

const char *Am_Pm_name_EUCKR[][12] = {
  {"am", "pm", "Am", "Pm", "AM", "PM",
   "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."},	/* US */
  {AM_NAME_KR_EUC, PM_NAME_KR_EUC, AM_NAME_KR_EUC,
   PM_NAME_KR_EUC, AM_NAME_KR_EUC, PM_NAME_KR_EUC,
   AM_NAME_KR_EUC, PM_NAME_KR_EUC, AM_NAME_KR_EUC,
   PM_NAME_KR_EUC, AM_NAME_KR_EUC, PM_NAME_KR_EUC},
  /* KR */
  {"am", "pm", "Am", "Pm", "AM", "PM",
   "a.m.", "p.m.", "A.m.", "P.m.", "A.M.", "P.M."},	/* TR */
};

/*
 * db_to_date () -
 */
int
db_to_date (const DB_VALUE * src_str,
	    const DB_VALUE * format_str,
	    const DB_VALUE * date_lang, DB_VALUE * result_date)
{
  int error_status = NO_ERROR;
  char *cur_format_str_ptr, *next_format_str_ptr;
  char *cs;			/*current source string pointer */
  char *last_src, *last_format;

  int cur_format;

  int cur_format_size;

  int month = 0, day = 0, year = 0, day_of_the_week = 0, week = -1;
  int monthcount = 0, daycount = 0, yearcount = 0, day_of_the_weekcount = 0;

  int i;
  bool no_user_format;
  INTL_LANG date_lang_id;
  INTL_CODESET codeset;
  INTL_CODESET frmt_codeset;
  char stack_buf_str[64], stack_buf_format[64];
  char *initial_buf_str = NULL, *initial_buf_format = NULL;
  bool do_free_buf_str = false, do_free_buf_format = false;
  DB_VALUE default_format;
  bool has_user_format = false;
  bool dummy;

  assert (src_str != (DB_VALUE *) NULL);
  assert (result_date != (DB_VALUE *) NULL);
  assert (date_lang != (DB_VALUE *) NULL);

  DB_MAKE_NULL (&default_format);

  if (DB_IS_NULL (src_str))
    {
      DB_MAKE_NULL (result_date);
      return error_status;
    }

  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang),
					     &has_user_format, &dummy);

  if (false == is_char_string (src_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_SRC_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  codeset = DB_GET_STRING_CODESET (src_str);
  if (lang_get_specific_locale (date_lang_id, codeset) == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (date_lang_id),
	      lang_charset_name (codeset));
      return error_status;
    }

  error_status =
    db_check_or_create_null_term_string (src_str, stack_buf_str,
					 sizeof (stack_buf_str),
					 true, true,
					 &initial_buf_str, &do_free_buf_str);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cs = initial_buf_str;
  last_src = cs + strlen (cs);

  last_src = (char *) intl_backskip_spaces (cs, last_src - 1, codeset);
  last_src = last_src + 1;

  no_user_format = (format_str == NULL) || (!has_user_format);

  if (no_user_format)
    {
      DB_DATE date_tmp;
      const char *default_format_str;

      /* try default CUBRID format first */
      if (NO_ERROR == db_string_to_date_ex ((char *) cs,
					    last_src - cs, &date_tmp))
	{
	  DB_MAKE_ENCODED_DATE (result_date, &date_tmp);
	  goto exit;
	}

      /* error parsing CUBRID default format, try the locale format, if any */
      default_format_str = lang_date_format_parse (date_lang_id, codeset,
						   DB_TYPE_DATE,
						   &frmt_codeset);
      if (default_format_str == NULL)
	{
	  error_status = ER_DATE_CONVERSION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      DB_MAKE_CHAR (&default_format, strlen (default_format_str),
		    default_format_str, strlen (default_format_str),
		    frmt_codeset, LANG_GET_BINARY_COLLATION (frmt_codeset));
      format_str = &default_format;
    }

  if (DB_IS_NULL (format_str))
    {
      DB_MAKE_NULL (result_date);
      goto exit;
    }

  if (false == is_char_string (format_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_FORMAT_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  frmt_codeset = DB_GET_STRING_CODESET (format_str);

  error_status =
    db_check_or_create_null_term_string (format_str, stack_buf_format,
					 sizeof (stack_buf_format),
					 true, true,
					 &initial_buf_format,
					 &do_free_buf_format);
  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cur_format_str_ptr = initial_buf_format;
  last_format = cur_format_str_ptr + strlen (cur_format_str_ptr);

  /* Skip space, tab, CR     */
  while (cs < last_src && strchr (WHITE_CHARS, *cs))
    {
      cs++;
    }

  /* Skip space, tab, CR     */
  while (cur_format_str_ptr < last_format &&
	 strchr (WHITE_CHARS, *cur_format_str_ptr))
    {
      cur_format_str_ptr++;
    }

  while (cs < last_src)
    {
      int token_size, cmp, cs_byte_size;
      int k;

      cur_format = get_next_format (cur_format_str_ptr, frmt_codeset,
				    DB_TYPE_DATE, &cur_format_size,
				    &next_format_str_ptr);
      switch (cur_format)
	{
	case DT_YYYY:
	  if (yearcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      yearcount++;
	    }

	  k = parse_digits (cs, &year, 4);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += k;
	  break;

	case DT_YY:
	  if (yearcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      yearcount++;
	    }

	  k = parse_digits (cs, &year, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += k;

	  i = get_cur_year ();
	  if (i == -1)
	    {
	      error_status = ER_SYSTEM_DATE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  year += (i / 100) * 100;
	  break;

	case DT_MM:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  k = parse_digits (cs, &month, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += k;

	  if (month < 1 || month > 12)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MONTH:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  error_status = get_string_date_token_id (SDT_MONTH,
						   date_lang_id, cs,
						   codeset,
						   &month, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (month == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MON:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  month = 0;


	  error_status = get_string_date_token_id (SDT_MONTH_SHORT,
						   date_lang_id, cs,
						   codeset,
						   &month, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  cs += token_size;


	  if (month == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_DD:
	  if (daycount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      daycount++;
	    }

	  k = parse_digits (cs, &day, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += k;

	  if (day < 0 || day > 31)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_TEXT:
	  if (codeset != frmt_codeset)
	    {
	      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cmp =
	    intl_case_match_tok (date_lang_id, codeset,
				 (unsigned char *)
				 (cur_format_str_ptr + 1),
				 (unsigned char *) cs,
				 cur_format_size - 2, strlen (cs),
				 &cs_byte_size);

	  if (cmp != 0)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      return error_status;
	    }

	  cs += cs_byte_size;
	  break;

	case DT_PUNCTUATION:
	  if (strncasecmp ((const char *) cur_format_str_ptr,
			   (const char *) cs, cur_format_size) != 0)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += cur_format_size;
	  break;

	case DT_CC:
	case DT_Q:
	  error_status = ER_QSTR_INVALID_FORMAT;
	  /* Does it need error message? */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;

	case DT_DAY:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  error_status = get_string_date_token_id (SDT_DAY, date_lang_id,
						   cs, codeset,
						   &day_of_the_week,
						   &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  cs += token_size;

	  if (day_of_the_week == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_DY:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  error_status =
	    get_string_date_token_id (SDT_DAY_SHORT, date_lang_id, cs,
				      codeset, &day_of_the_week, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (day_of_the_week == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_D:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  k = parse_digits (cs, &day_of_the_week, 1);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += k;

	  if (day_of_the_week < 1 || day_of_the_week > 7)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_INVALID:
	case DT_NORMAL:
	  error_status = ER_QSTR_INVALID_FORMAT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      /* Skip space, tab, CR     */
      while (cs < last_src && strchr (WHITE_CHARS, *cs))
	{
	  cs++;
	}

      cur_format_str_ptr = next_format_str_ptr;

      /* Skip space, tab, CR     */
      while (cur_format_str_ptr < last_format &&
	     strchr (WHITE_CHARS, *cur_format_str_ptr))
	{
	  cur_format_str_ptr++;
	}

      if (last_format == next_format_str_ptr)
	{
	  while (cs < last_src && strchr (WHITE_CHARS, *cs))
	    {
	      cs++;
	    }

	  if (cs != last_src)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;
	}
    }


  /* Both format and src should end at same time     */
  if (cs != last_src || cur_format_str_ptr != last_format)
    {
      error_status = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  year = (yearcount == 0) ? get_cur_year () : year;
  month = (monthcount == 0) ? get_cur_month () : month;
  day = (daycount == 0) ? 1 : day;
  week = (day_of_the_weekcount == 0) ? -1 : day_of_the_week - 1;

  if (week != -1 && week != db_get_day_of_week (year, month, day))
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  DB_MAKE_DATE (result_date, month, day, year);

  if (*(DB_GET_DATE (result_date)) == 0)
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

exit:
  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }
  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }

  return error_status;
}

/*
 * db_to_time () -
 */
int
db_to_time (const DB_VALUE * src_str,
	    const DB_VALUE * format_str,
	    const DB_VALUE * date_lang, DB_VALUE * result_time)
{
  int error_status = NO_ERROR;

  char *cur_format_str_ptr, *next_format_str_ptr;
  char *cs;			/*current source string pointer */
  char *last_format, *last_src;

  int cur_format;

  int cur_format_size;

  int second = 0, minute = 0, hour = 0;
  int time_count = 0;
  int mil_time_count = 0;
  int am = false;
  int pm = false;

  bool no_user_format;
  INTL_LANG date_lang_id;
  INTL_CODESET codeset;
  INTL_CODESET frmt_codeset;
  char stack_buf_str[64], stack_buf_format[64];
  char *initial_buf_str = NULL, *initial_buf_format = NULL;
  bool do_free_buf_str = false, do_free_buf_format = false;
  DB_VALUE default_format;
  bool has_user_format = false;
  bool dummy;

  assert (src_str != (DB_VALUE *) NULL);
  assert (result_time != (DB_VALUE *) NULL);
  assert (date_lang != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src_str))
    {
      DB_MAKE_NULL (result_time);
      return error_status;
    }

  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang),
					     &has_user_format, &dummy);

  /* now return null */
  if (false == is_char_string (src_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_SRC_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  codeset = DB_GET_STRING_CODESET (src_str);
  if (lang_get_specific_locale (date_lang_id, codeset) == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (date_lang_id),
	      lang_charset_name (codeset));
      return error_status;
    }
  error_status =
    db_check_or_create_null_term_string (src_str, stack_buf_str,
					 sizeof (stack_buf_str),
					 true, true,
					 &initial_buf_str, &do_free_buf_str);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cs = initial_buf_str;
  last_src = cs + strlen (cs);

  last_src = (char *) intl_backskip_spaces (cs, last_src - 1, codeset);
  last_src = last_src + 1;

  no_user_format = (format_str == NULL) || (!has_user_format);

  if (no_user_format)
    {
      DB_TIME time_tmp;
      const char *default_format_str;

      /* try default CUBRID format first */
      if (NO_ERROR == db_string_to_time_ex ((const char *) cs,
					    last_src - cs, &time_tmp))
	{
	  DB_MAKE_ENCODED_TIME (result_time, &time_tmp);
	  goto exit;
	}

      /* error parsing CUBRID default format, try the locale format, if any */
      default_format_str = lang_date_format_parse (date_lang_id, codeset,
						   DB_TYPE_TIME,
						   &frmt_codeset);
      if (default_format_str == NULL)
	{
	  error_status = ER_TIME_CONVERSION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}
      DB_MAKE_CHAR (&default_format, strlen (default_format_str),
		    default_format_str, strlen (default_format_str),
		    frmt_codeset, LANG_GET_BINARY_COLLATION (frmt_codeset));
      format_str = &default_format;
    }

  if (DB_IS_NULL (format_str))
    {
      DB_MAKE_NULL (result_time);
      goto exit;
    }

  if (false == is_char_string (format_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_FORMAT_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  frmt_codeset = DB_GET_STRING_CODESET (format_str);

  error_status =
    db_check_or_create_null_term_string (format_str, stack_buf_format,
					 sizeof (stack_buf_format),
					 true, true,
					 &initial_buf_format,
					 &do_free_buf_format);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cur_format_str_ptr = initial_buf_format;
  last_format = cur_format_str_ptr + strlen (cur_format_str_ptr);

  /* Skip space, tab, CR     */
  while (cs < last_src && strchr (WHITE_CHARS, *cs))
    {
      cs++;
    }

  /* Skip space, tab, CR     */
  while (cur_format_str_ptr < last_format &&
	 strchr (WHITE_CHARS, *cur_format_str_ptr))
    {
      cur_format_str_ptr++;
    }

  while (cs < last_src)
    {
      int cmp, cs_byte_size, token_size;
      int am_pm_id;
      int k;

      cur_format = get_next_format (cur_format_str_ptr, frmt_codeset,
				    DB_TYPE_TIME, &cur_format_size,
				    &next_format_str_ptr);
      switch (cur_format)
	{
	case DT_AM:
	case DT_A_M:
	case DT_PM:
	case DT_P_M:
	  if (mil_time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      mil_time_count++;
	    }

	  error_status =
	    get_string_date_token_id (SDT_AM_PM, date_lang_id, cs,
				      codeset, &am_pm_id, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (am_pm_id > 0)
	    {
	      if (am_pm_id % 2)
		{
		  am = true;
		}
	      else
		{
		  pm = true;
		}
	    }
	  else
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += token_size;
	  break;

	case DT_HH:
	case DT_HH12:
	  if (time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      time_count++;
	    }

	  k = parse_digits (cs, &hour, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (hour < 1 || hour > 12)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_HH24:
	  if (time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      time_count++;
	    }

	  if (mil_time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      mil_time_count++;
	    }

	  k = parse_digits (cs, &hour, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (hour < 0 || hour > 23)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MI:
	  k = parse_digits (cs, &minute, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (minute < 0 || minute > 59)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_SS:
	  k = parse_digits (cs, &second, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (second < 0 || second > 59)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_TEXT:
	  if (codeset != frmt_codeset)
	    {
	      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cmp =
	    intl_case_match_tok (date_lang_id, codeset,
				 (unsigned char *)
				 (cur_format_str_ptr + 1),
				 (unsigned char *) cs,
				 cur_format_size - 2, strlen (cs),
				 &cs_byte_size);

	  if (cmp != 0)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      return error_status;
	    }

	  cs += cs_byte_size;
	  break;

	case DT_PUNCTUATION:
	  if (strncasecmp ((const char *) cur_format_str_ptr,
			   (const char *) cs, cur_format_size) != 0)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += cur_format_size;
	  break;

	case DT_INVALID:
	  error_status = ER_TIME_CONVERSION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      /* Skip space, tab, CR     */
      while (cs < last_src && strchr (WHITE_CHARS, *cs))
	{
	  cs++;
	}

      cur_format_str_ptr = next_format_str_ptr;

      /* Skip space, tab, CR     */
      while (cur_format_str_ptr < last_format &&
	     strchr (WHITE_CHARS, *cur_format_str_ptr))
	{
	  cur_format_str_ptr++;
	}

      if (last_format == next_format_str_ptr)
	{
	  while (cs < last_src && strchr (WHITE_CHARS, *cs))
	    {
	      cs++;
	    }

	  if (cs != last_src)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;
	}
    }

  /* Both format and src should end at same time     */
  if (cs != last_src || cur_format_str_ptr != last_format)
    {
      error_status = ER_TIME_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (am == true && pm == false && hour <= 12)
    {				/* If A.M.    */
      hour = (hour == 12) ? 0 : hour;
    }
  else if (am == false && pm == true && hour <= 12)
    {				/* If P.M.    */
      hour = (hour == 12) ? hour : hour + 12;
    }
  else if (am == false && pm == false)
    {				/* If military time    */
      ;
    }
  else
    {
      error_status = ER_TIME_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  DB_MAKE_TIME (result_time, hour, minute, second);

exit:
  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }

  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }

  return error_status;
}

/*
 * db_to_timestamp () -
 */
int
db_to_timestamp (const DB_VALUE * src_str,
		 const DB_VALUE * format_str,
		 const DB_VALUE * date_lang, DB_VALUE * result_timestamp)
{
  int error_status = NO_ERROR;

  DB_DATE tmp_date;
  DB_TIME tmp_time;
  DB_TIMESTAMP tmp_timestamp;

  char *cur_format_str_ptr, *next_format_str_ptr;
  char *cs;			/*current source string pointer */
  char *last_format, *last_src;

  int cur_format_size;
  int cur_format;

  int month = 0, day = 0, year = 0, day_of_the_week = 0, week = -1;
  int monthcount = 0, daycount = 0, yearcount = 0, day_of_the_weekcount = 0;

  int second = 0, minute = 0, hour = 0;
  int time_count = 0;
  int mil_time_count = 0;
  int am = false;
  int pm = false;

  int i;
  bool no_user_format;
  INTL_LANG date_lang_id;
  INTL_CODESET codeset;
  INTL_CODESET frmt_codeset;
  char stack_buf_str[64], stack_buf_format[64];
  char *initial_buf_str = NULL, *initial_buf_format = NULL;
  bool do_free_buf_str = false, do_free_buf_format = false;
  DB_VALUE default_format;
  bool has_user_format = false;
  bool dummy;

  assert (src_str != (DB_VALUE *) NULL);
  assert (result_timestamp != (DB_VALUE *) NULL);
  assert (date_lang != (DB_VALUE *) NULL);

  DB_MAKE_NULL (&default_format);

  if (DB_IS_NULL (src_str))
    {
      DB_MAKE_NULL (result_timestamp);
      return error_status;
    }

  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang),
					     &has_user_format, &dummy);

  if (false == is_char_string (src_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_SRC_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  codeset = DB_GET_STRING_CODESET (src_str);
  if (lang_get_specific_locale (date_lang_id, codeset) == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (date_lang_id),
	      lang_charset_name (codeset));
      return error_status;
    }

  error_status =
    db_check_or_create_null_term_string (src_str, stack_buf_str,
					 sizeof (stack_buf_str),
					 true, true,
					 &initial_buf_str, &do_free_buf_str);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cs = initial_buf_str;
  last_src = cs + strlen (cs);

  last_src = (char *) intl_backskip_spaces (cs, last_src - 1, codeset);
  last_src = last_src + 1;

  no_user_format = (format_str == NULL) || (!has_user_format);

  if (no_user_format)
    {
      DB_TIMESTAMP timestamp_tmp;
      const char *default_format_str;

      /* try default CUBRID format first */
      if (NO_ERROR ==
	  db_string_to_timestamp_ex ((const char *) cs,
				     last_src - cs, &timestamp_tmp))
	{
	  DB_MAKE_TIMESTAMP (result_timestamp, timestamp_tmp);
	  goto exit;
	}

      default_format_str = lang_date_format_parse (date_lang_id, codeset,
						   DB_TYPE_TIMESTAMP,
						   &frmt_codeset);
      if (default_format_str == NULL)
	{
	  error_status = ER_TIMESTAMP_CONVERSION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      DB_MAKE_CHAR (&default_format, strlen (default_format_str),
		    default_format_str, strlen (default_format_str),
		    frmt_codeset, LANG_GET_BINARY_COLLATION (frmt_codeset));
      format_str = &default_format;
    }

  if (DB_IS_NULL (format_str))
    {
      DB_MAKE_NULL (result_timestamp);
      goto exit;
    }

  if (false == is_char_string (format_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_FORMAT_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  frmt_codeset = DB_GET_STRING_CODESET (format_str);

  error_status =
    db_check_or_create_null_term_string (format_str, stack_buf_format,
					 sizeof (stack_buf_format),
					 true, true,
					 &initial_buf_format,
					 &do_free_buf_format);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cur_format_str_ptr = initial_buf_format;
  last_format = cur_format_str_ptr + strlen (cur_format_str_ptr);

  /* Skip space, tab, CR     */
  while (cs < last_src && strchr (WHITE_CHARS, *cs))
    {
      cs++;
    }

  /* Skip space, tab, CR     */
  while (cur_format_str_ptr < last_format &&
	 strchr (WHITE_CHARS, *cur_format_str_ptr))
    {
      cur_format_str_ptr++;
    }

  while (cs < last_src)
    {
      int token_size, cmp, cs_byte_size;
      int am_pm_id;
      int k;

      cur_format = get_next_format (cur_format_str_ptr, frmt_codeset,
				    DB_TYPE_TIMESTAMP, &cur_format_size,
				    &next_format_str_ptr);
      switch (cur_format)
	{
	case DT_YYYY:
	  if (yearcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      yearcount++;
	    }

	  k = parse_digits (cs, &year, 4);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;
	  break;

	case DT_YY:
	  if (yearcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      yearcount++;
	    }

	  k = parse_digits (cs, &year, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  i = get_cur_year ();
	  if (i == -1)
	    {
	      error_status = ER_SYSTEM_DATE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  year += (i / 100) * 100;
	  break;

	case DT_MM:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  k = parse_digits (cs, &month, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (month < 1 || month > 12)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MONTH:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  error_status = get_string_date_token_id (SDT_MONTH,
						   date_lang_id, cs,
						   codeset,
						   &month, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (month == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MON:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  month = 0;

	  error_status = get_string_date_token_id (SDT_MONTH_SHORT,
						   date_lang_id, cs,
						   codeset,
						   &month, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (month == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_DD:
	  if (daycount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      daycount++;
	    }

	  k = parse_digits (cs, &day, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (day < 0 || day > 31)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_AM:
	case DT_A_M:
	case DT_PM:
	case DT_P_M:
	  if (mil_time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      mil_time_count++;
	    }

	  error_status = get_string_date_token_id (SDT_AM_PM,
						   date_lang_id, cs,
						   codeset,
						   &am_pm_id, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (am_pm_id > 0)
	    {
	      if (am_pm_id % 2)
		{
		  am = true;
		}
	      else
		{
		  pm = true;
		}
	    }
	  else
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += token_size;
	  break;

	case DT_HH:
	case DT_HH12:
	  if (time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      time_count++;
	    }

	  k = parse_digits (cs, &hour, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (hour < 1 || hour > 12)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_HH24:
	  if (time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      time_count++;
	    }

	  if (mil_time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      mil_time_count++;
	    }

	  k = parse_digits (cs, &hour, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (hour < 0 || hour > 23)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MI:
	  k = parse_digits (cs, &minute, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (minute < 0 || minute > 59)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_SS:
	  k = parse_digits (cs, &second, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (second < 0 || second > 59)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_TEXT:
	  if (codeset != frmt_codeset)
	    {
	      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cmp =
	    intl_case_match_tok (date_lang_id, codeset,
				 (unsigned char *)
				 (cur_format_str_ptr + 1),
				 (unsigned char *) cs,
				 cur_format_size - 2, strlen (cs),
				 &cs_byte_size);

	  if (cmp != 0)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      return error_status;
	    }

	  cs += cs_byte_size;
	  break;

	case DT_PUNCTUATION:
	  if (strncasecmp ((const char *) (void *) cur_format_str_ptr,
			   (const char *) cs, cur_format_size) != 0)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += cur_format_size;
	  break;

	case DT_CC:
	case DT_Q:
	  error_status = ER_QSTR_INVALID_FORMAT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;

	case DT_DAY:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  error_status = get_string_date_token_id (SDT_DAY,
						   date_lang_id, cs,
						   codeset,
						   &day_of_the_week,
						   &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (day_of_the_week == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_DY:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  error_status = get_string_date_token_id (SDT_DAY_SHORT,
						   date_lang_id, cs,
						   codeset,
						   &day_of_the_week,
						   &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (day_of_the_week == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_D:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  k = parse_digits (cs, &day_of_the_week, 1);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (day_of_the_week < 1 || day_of_the_week > 7)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_NORMAL:
	case DT_INVALID:
	  error_status = ER_QSTR_INVALID_FORMAT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      /* Skip space, tab, CR     */
      while (cs < last_src && strchr (WHITE_CHARS, *cs))
	{
	  cs++;
	}

      cur_format_str_ptr = next_format_str_ptr;

      /* Skip space, tab, CR     */
      while (cur_format_str_ptr < last_format &&
	     strchr (WHITE_CHARS, *cur_format_str_ptr))
	{
	  cur_format_str_ptr++;
	}

      if (last_format == next_format_str_ptr)
	{
	  while (cs < last_src && strchr (WHITE_CHARS, *cs))
	    {
	      cs++;
	    }

	  if (cs != last_src)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;
	}
    }

  /* Both format and src should end at same time     */
  if (cs != last_src || cur_format_str_ptr != last_format)
    {
      error_status = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  /**************            Check DATE        ****************/
  year = (yearcount == 0) ? get_cur_year () : year;
  month = (monthcount == 0) ? get_cur_month () : month;
  day = (daycount == 0) ? 1 : day;
  week = (day_of_the_weekcount == 0) ? -1 : day_of_the_week - 1;

  if (week != -1 && week != db_get_day_of_week (year, month, day))
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (db_date_encode (&tmp_date, month, day, year) != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  /**************            Check TIME        ****************/
  if (am == true && pm == false && hour <= 12)
    {				/* If A.M.    */
      hour = (hour == 12) ? 0 : hour;
    }
  else if (am == false && pm == true && hour <= 12)
    {				/* If P.M.    */
      hour = (hour == 12) ? hour : hour + 12;
    }
  else if (am == false && pm == false)
    {				/* If military time    */
      ;
    }
  else
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  db_time_encode (&tmp_time, hour, minute, second);

  /*************         Make TIMESTAMP        *****************/
  if (NO_ERROR != db_timestamp_encode (&tmp_timestamp, &tmp_date, &tmp_time))
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  DB_MAKE_TIMESTAMP (result_timestamp, tmp_timestamp);

exit:
  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }

  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }

  return error_status;
}

/*
 * db_to_datetime () -
 */
int
db_to_datetime (const DB_VALUE * src_str, const DB_VALUE * format_str,
		const DB_VALUE * date_lang, DB_VALUE * result_datetime)
{
  int error_status = NO_ERROR;

  DB_DATETIME tmp_datetime;

  char *cur_format_str_ptr, *next_format_str_ptr;
  char *cs;			/*current source string pointer */
  char *last_format, *last_src;

  int cur_format_size;
  int cur_format;

  int month = 0, day = 0, year = 0, day_of_the_week = 0, week = -1;
  int monthcount = 0, daycount = 0, yearcount = 0, day_of_the_weekcount = 0;

  double fraction;
  int millisecond = 0, second = 0, minute = 0, hour = 0;
  int time_count = 0;
  int mil_time_count = 0;
  int am = false;
  int pm = false;

  int i;
  bool no_user_format;
  INTL_LANG date_lang_id;
  INTL_CODESET codeset;
  INTL_CODESET frmt_codeset;
  char stack_buf_str[64], stack_buf_format[64];
  char *initial_buf_str = NULL, *initial_buf_format = NULL;
  bool do_free_buf_str = false, do_free_buf_format = false;
  DB_VALUE default_format;
  bool has_user_format = false;
  bool dummy;

  assert (src_str != (DB_VALUE *) NULL);
  assert (result_datetime != (DB_VALUE *) NULL);

  DB_MAKE_NULL (&default_format);

  if (DB_IS_NULL (src_str))
    {
      DB_MAKE_NULL (result_datetime);
      return error_status;
    }

  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang),
					     &has_user_format, &dummy);

  if (false == is_char_string (src_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_SRC_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  codeset = DB_GET_STRING_CODESET (src_str);
  if (lang_get_specific_locale (date_lang_id, codeset) == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (date_lang_id),
	      lang_charset_name (codeset));
      return error_status;
    }

  error_status =
    db_check_or_create_null_term_string (src_str, stack_buf_str,
					 sizeof (stack_buf_str),
					 true, true,
					 &initial_buf_str, &do_free_buf_str);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cs = initial_buf_str;
  last_src = cs + strlen (cs);

  last_src = (char *) intl_backskip_spaces (cs, last_src - 1, codeset);
  last_src = last_src + 1;

  no_user_format = (format_str == NULL) || (!has_user_format);

  if (no_user_format)
    {
      DB_DATETIME datetime_tmp;
      const char *default_format_str;

      /* try default CUBRID format first */
      if (db_string_to_datetime_ex ((const char *) cs,
				    last_src - cs, &datetime_tmp) == NO_ERROR)
	{
	  DB_MAKE_DATETIME (result_datetime, &datetime_tmp);
	  goto exit;
	}

      default_format_str = lang_date_format_parse (date_lang_id, codeset,
						   DB_TYPE_DATETIME,
						   &frmt_codeset);
      if (default_format_str == NULL)
	{
	  error_status = ER_TIMESTAMP_CONVERSION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      DB_MAKE_CHAR (&default_format, strlen (default_format_str),
		    default_format_str, strlen (default_format_str),
		    frmt_codeset, LANG_GET_BINARY_COLLATION (frmt_codeset));
      format_str = &default_format;
    }
  if (DB_IS_NULL (format_str))
    {
      DB_MAKE_NULL (result_datetime);
      goto exit;
    }

  if (false == is_char_string (format_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_FORMAT_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  if (DB_GET_STRING_SIZE (format_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  frmt_codeset = DB_GET_STRING_CODESET (format_str);

  error_status =
    db_check_or_create_null_term_string (format_str, stack_buf_format,
					 sizeof (stack_buf_format),
					 true, true,
					 &initial_buf_format,
					 &do_free_buf_format);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cur_format_str_ptr = initial_buf_format;
  last_format = cur_format_str_ptr + strlen (cur_format_str_ptr);

  /* Skip space, tab, CR     */
  while (cs < last_src && strchr (WHITE_CHARS, *cs))
    {
      cs++;
    }

  /* Skip space, tab, CR     */
  while (cur_format_str_ptr < last_format &&
	 strchr (WHITE_CHARS, *cur_format_str_ptr))
    {
      cur_format_str_ptr++;
    }

  while (cs < last_src)
    {
      int token_size, cmp, cs_byte_size;
      int am_pm_id;
      int k;

      cur_format = get_next_format (cur_format_str_ptr, frmt_codeset,
				    DB_TYPE_DATETIME, &cur_format_size,
				    &next_format_str_ptr);
      switch (cur_format)
	{
	case DT_YYYY:
	  if (yearcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      yearcount++;
	    }

	  k = parse_digits (cs, &year, 4);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;
	  break;

	case DT_YY:
	  if (yearcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      yearcount++;
	    }

	  k = parse_digits (cs, &year, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  i = get_cur_year ();
	  if (i == -1)
	    {
	      error_status = ER_SYSTEM_DATE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  year += (i / 100) * 100;
	  break;

	case DT_MM:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  k = parse_digits (cs, &month, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (month < 1 || month > 12)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MONTH:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  error_status = get_string_date_token_id (SDT_MONTH,
						   date_lang_id, cs,
						   codeset,
						   &month, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (month == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MON:
	  if (monthcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      monthcount++;
	    }

	  month = 0;

	  error_status = get_string_date_token_id (SDT_MONTH_SHORT,
						   date_lang_id, cs,
						   codeset,
						   &month, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (month == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_DD:
	  if (daycount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      daycount++;
	    }

	  k = parse_digits (cs, &day, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (day < 0 || day > 31)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_AM:
	case DT_A_M:
	case DT_PM:
	case DT_P_M:
	  if (mil_time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      mil_time_count++;
	    }

	  error_status =
	    get_string_date_token_id (SDT_AM_PM, date_lang_id, cs,
				      codeset, &am_pm_id, &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (am_pm_id > 0)
	    {
	      if (am_pm_id % 2)
		{
		  am = true;
		}
	      else
		{
		  pm = true;
		}
	    }
	  else
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += token_size;
	  break;

	case DT_H:
	  if (time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      time_count++;
	    }

	  k = parse_digits (cs, &hour, 1);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;
	  if (hour < 1 || hour > 12)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_HH:
	case DT_HH12:
	  if (time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      time_count++;
	    }

	  k = parse_digits (cs, &hour, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (hour < 1 || hour > 12)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_HH24:
	  if (time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      time_count++;
	    }

	  if (mil_time_count != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      mil_time_count++;
	    }

	  k = parse_digits (cs, &hour, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (hour < 0 || hour > 23)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MI:
	  k = parse_digits (cs, &minute, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (minute < 0 || minute > 59)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_SS:
	  k = parse_digits (cs, &second, 2);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (second < 0 || second > 59)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_MS:
	  if (!char_isdigit (*cs))
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  for (i = 0, fraction = 100; char_isdigit (*cs); cs++, i++)
	    {
	      millisecond += (int) ((*cs - '0') * fraction + 0.5);
	      fraction /= 10;
	    }

	  if (millisecond < 0 || millisecond > 999)
	    {
	      error_status = ER_TIME_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_TEXT:
	  if (codeset != frmt_codeset)
	    {
	      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cmp =
	    intl_case_match_tok (date_lang_id, codeset,
				 (unsigned char *)
				 (cur_format_str_ptr + 1),
				 (unsigned char *) cs,
				 cur_format_size - 2, strlen (cs),
				 &cs_byte_size);

	  if (cmp != 0)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      return error_status;
	    }

	  cs += cs_byte_size;
	  break;

	case DT_PUNCTUATION:
	  if (strncasecmp ((const char *) (void *) cur_format_str_ptr,
			   (const char *) cs, cur_format_size) != 0)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  cs += cur_format_size;
	  break;

	case DT_CC:
	case DT_Q:
	  error_status = ER_QSTR_INVALID_FORMAT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;

	case DT_DAY:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  error_status = get_string_date_token_id (SDT_DAY,
						   date_lang_id, cs,
						   codeset,
						   &day_of_the_week,
						   &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (day_of_the_week == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_DY:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  error_status = get_string_date_token_id (SDT_DAY_SHORT,
						   date_lang_id, cs,
						   codeset,
						   &day_of_the_week,
						   &token_size);
	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }

	  cs += token_size;

	  if (day_of_the_week == 0)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_D:
	  if (day_of_the_weekcount != 0)
	    {
	      error_status = ER_QSTR_FORMAT_DUPLICATION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  else
	    {
	      day_of_the_weekcount++;
	    }

	  k = parse_digits (cs, &day_of_the_week, 1);
	  if (k <= 0)
	    {
	      error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  cs += k;

	  if (day_of_the_week < 1 || day_of_the_week > 7)
	    {
	      error_status = ER_DATE_CONVERSION;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;

	case DT_NORMAL:
	case DT_INVALID:
	  error_status = ER_QSTR_INVALID_FORMAT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      while (cs < last_src && strchr (WHITE_CHARS, *cs))
	{
	  cs++;
	}

      cur_format_str_ptr = next_format_str_ptr;

      /* Skip space, tab, CR     */
      while (cur_format_str_ptr < last_format &&
	     strchr (WHITE_CHARS, *cur_format_str_ptr))
	{
	  cur_format_str_ptr++;
	}

      if (last_format == next_format_str_ptr)
	{
	  while (cs < last_src && strchr (WHITE_CHARS, *cs))
	    {
	      cs++;
	    }

	  if (cs != last_src)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }
	  break;
	}
    }

  /* Both format and src should end at same time     */
  if (cs != last_src || cur_format_str_ptr != last_format)
    {
      error_status = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  /**************            Check DATE        ****************/
  year = (yearcount == 0) ? get_cur_year () : year;
  month = (monthcount == 0) ? get_cur_month () : month;
  day = (daycount == 0) ? 1 : day;
  week = (day_of_the_weekcount == 0) ? -1 : day_of_the_week - 1;

  if (week != -1 && week != db_get_day_of_week (year, month, day))
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  /**************            Check TIME        ****************/
  if (am == true && pm == false && hour <= 12)
    {				/* If A.M.    */
      hour = (hour == 12) ? 0 : hour;
    }
  else if (am == false && pm == true && hour <= 12)
    {				/* If P.M.    */
      hour = (hour == 12) ? hour : hour + 12;
    }
  else if (am == false && pm == false)
    {				/* If military time    */
      ;
    }
  else
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

  /*************         Make DATETIME        *****************/
  error_status = db_datetime_encode (&tmp_datetime, month, day, year, hour,
				     minute, second, millisecond);
  if (error_status != NO_ERROR)
    {
      goto exit;
    }

  if (DB_MAKE_DATETIME (result_datetime, &tmp_datetime) != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto exit;
    }

exit:
  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }

  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }

  return error_status;
}


/*
 * adjust_precision () - Change representation of 'data' as of
 *    'precision' and 'scale'.
 *                       When data has invalid format, just return
 * return : DOMAIN_INCOMPATIBLE, DOMAIN_OVERFLOW, NO_ERROR
 *
 *  Note : This function is not localized in relation to fractional and digit
 *	   grouping symbols. It assumes the default symbols ('.' for fraction
 *	   symbol and ',' for digit grouping symbol)
 */
static int
adjust_precision (char *data, int precision, int scale)
{
  char tmp_data[DB_MAX_NUMERIC_PRECISION * 2 + 1];
  int scale_counter = 0;
  int i = 0;
  int before_dec_point = 0;
  int after_dec_point = 0;
  int space_started = false;

  if (data == NULL || precision < 0 || precision > DB_MAX_NUMERIC_PRECISION
      || scale < 0 || scale > DB_MAX_NUMERIC_PRECISION)
    {
      return DOMAIN_INCOMPATIBLE;
    }

  if (*data == '-')
    {
      tmp_data[0] = '-';
      i++;
    }
  else if (*data == '+')
    {
      i++;
    }

  for (; i < DB_MAX_NUMERIC_PRECISION && *(data + i) != '\0'
       && *(data + i) != '.'; i++)
    {
      if (char_isdigit (*(data + i)))
	{
	  tmp_data[i] = *(data + i);
	  before_dec_point++;
	}
      else if (char_isspace (*(data + i)))
	{
	  space_started = true;
	  break;
	}
      else
	{
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  if (space_started == true)
    {
      int j = i;
      while (char_isspace (*(data + j)))
	{
	  j++;
	}

      if (*(data + j) != '\0')
	{
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  if (*(data + i) == '.')
    {
      tmp_data[i] = '.';
      i++;
      while (*(data + i) != '\0' && scale_counter < scale)
	{
	  if (char_isdigit (*(data + i)))
	    {
	      tmp_data[i] = *(data + i);
	      after_dec_point++;
	    }
	  else if (char_isspace (*(data + i)))
	    {
	      space_started = true;
	      break;
	    }
	  else
	    {
	      return DOMAIN_INCOMPATIBLE;
	    }
	  scale_counter++;
	  i++;
	}

      if (space_started == true)
	{
	  int j = i;
	  while (char_isspace (*(data + j)))
	    {
	      j++;
	    }

	  if (*(data + j) != '\0')
	    {
	      return DOMAIN_INCOMPATIBLE;
	    }
	}

      while (scale_counter < scale)
	{
	  tmp_data[i] = '0';
	  scale_counter++;
	  i++;
	}

    }
  else if (*(data + i) == '\0')
    {
      tmp_data[i] = '.';
      i++;
      while (scale_counter < scale)
	{
	  tmp_data[i] = '0';
	  scale_counter++;
	  i++;
	}

    }
  else
    {
      return DOMAIN_COMPATIBLE;
    }

  if (before_dec_point + after_dec_point > DB_MAX_NUMERIC_PRECISION
      || after_dec_point > DB_DEFAULT_NUMERIC_PRECISION
      || before_dec_point > precision - scale)
    {
      return DOMAIN_OVERFLOW;
    }

  tmp_data[i] = '\0';
  strcpy (data, tmp_data);
  return NO_ERROR;
}

/*
 * db_to_number () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
int
db_to_number (const DB_VALUE * src_str, const DB_VALUE * format_str,
	      const DB_VALUE * number_lang, DB_VALUE * result_num)
{
  /* default precision and scale is (38, 0) */
  /* it is more profitable that the definition of this value is located in
     some header file */
  const char *dflt_format_str = "99999999999999999999999999999999999999";

  int error_status = NO_ERROR;

  char *cs;			/* current source string pointer        */
  char *last_cs;
  char *format_str_ptr;
  char *last_format;
  char *next_fsp;		/* next format string pointer   */
  int token_length;
  int count_format = 0;
  int cur_format;

  int precision = 0;		/* retain precision of format_str */
  int scale = 0;
  int loopvar, met_decptr = 0;
  int use_default_precision = 0;

  char *first_cs_for_error, *first_format_str_for_error;

  char stack_buf_str[64], stack_buf_format[64];
  char *initial_buf_str = NULL, *initial_buf_format = NULL;
  bool do_free_buf_str = false, do_free_buf_format = false;
  char digit_grouping_symbol;
  char fraction_symbol;
  bool has_user_format;
  bool dummy;
  int number_lang_id;
  TP_DOMAIN *domain;

  assert (src_str != (DB_VALUE *) NULL);
  assert (result_num != (DB_VALUE *) NULL);
  assert (number_lang != NULL);
  assert (format_str != NULL);

  if (DB_IS_NULL (src_str))
    {
      DB_MAKE_NULL (result_num);
      return error_status;
    }

  if (false == is_char_string (src_str))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) == 0)
    {
      error_status = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (DB_GET_STRING_SIZE (src_str) > MAX_TOKEN_SIZE)
    {
      error_status = ER_QSTR_SRC_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  assert (DB_VALUE_TYPE (number_lang) == DB_TYPE_INTEGER);
  number_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (number_lang),
					       &has_user_format, &dummy);
  digit_grouping_symbol = lang_digit_grouping_symbol (number_lang_id);
  fraction_symbol = lang_digit_fractional_symbol (number_lang_id);

  error_status =
    db_check_or_create_null_term_string (src_str, stack_buf_str,
					 sizeof (stack_buf_str),
					 true, false,
					 &initial_buf_str, &do_free_buf_str);

  if (error_status != NO_ERROR)
    {
      goto exit;
    }
  cs = initial_buf_str;
  last_cs = cs + strlen (cs);

  /* If there is no format */
  if (!has_user_format)
    {
      format_str_ptr = (char *) dflt_format_str;
      last_format = format_str_ptr + strlen (dflt_format_str);
    }
  else				/* format_str != NULL */
    {
      /*      Format string type checking     */
      if (is_char_string (format_str))
	{
	  if (DB_GET_STRING_SIZE (format_str) > MAX_TOKEN_SIZE)
	    {
	      error_status = ER_QSTR_FORMAT_TOO_LONG;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  error_status =
	    db_check_or_create_null_term_string (format_str, stack_buf_format,
						 sizeof (stack_buf_format),
						 true, false,
						 &initial_buf_format,
						 &do_free_buf_format);

	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  format_str_ptr = initial_buf_format;
	  last_format = format_str_ptr + strlen (format_str_ptr);
	}
      else
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      if (DB_GET_STRING_SIZE (format_str) == 0)
	{
	  error_status = ER_QSTR_EMPTY_STRING;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}
    }

  last_cs = (char *) intl_backskip_spaces (cs, last_cs - 1,
					   DB_GET_STRING_CODESET (src_str));
  last_cs = last_cs + 1;

  /* Skip space, tab, CR  */
  while (cs < last_cs && strchr (WHITE_CHARS, *cs))
    {
      cs++;
    }
  while (format_str_ptr < last_format
	 && strchr (WHITE_CHARS, *format_str_ptr))
    {
      format_str_ptr++;
    }
  first_cs_for_error = cs;
  first_format_str_for_error = format_str_ptr;

  /* get precision and scale of format_str */
  for (loopvar = 0; format_str_ptr + loopvar < last_format; loopvar++)
    {
      switch (*(format_str_ptr + loopvar))
	{
	case '9':
	case '0':
	  precision++;
	  if (met_decptr > 0)
	    {
	      scale++;
	    }
	  break;

	case 'c':
	case 'C':
	case 's':
	case 'S':
	  break;

	default:
	  if (*(format_str_ptr + loopvar) == digit_grouping_symbol)
	    {
	      break;
	    }
	  else if (*(format_str_ptr + loopvar) == fraction_symbol)
	    {
	      met_decptr++;
	      break;
	    }
	  precision = 0;
	  scale = 0;
	  use_default_precision = 1;
	}

      if (precision + scale > DB_MAX_NUMERIC_PRECISION)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		  pr_type_name (TP_DOMAIN_TYPE (domain)));
	  error_status = ER_IT_DATA_OVERFLOW;
	  goto exit;
	}

      if (use_default_precision == 1)
	{
	  /* scientific notation */
	  precision = DB_MAX_NUMERIC_PRECISION;
	  scale = DB_DEFAULT_NUMERIC_PRECISION;
	  break;
	}
    }

  /* Skip space, tab, CR  */
  while (cs < last_cs)
    {
      cur_format =
	get_number_token (number_lang_id, format_str_ptr, &token_length,
			  last_format, &next_fsp);
      switch (cur_format)
	{
	case N_FORMAT:
	  if (count_format != 0)
	    {
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      goto exit;
	    }

	  error_status = make_number (cs, last_cs,
				      DB_GET_STRING_CODESET (src_str),
				      format_str_ptr, &token_length,
				      result_num, precision,
				      scale, number_lang_id);
	  if (error_status == NO_ERROR)
	    {
	      count_format++;
	      cs += token_length;
	    }
	  else if (error_status == ER_IT_DATA_OVERFLOW)
	    {
	      domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1,
		      pr_type_name (TP_DOMAIN_TYPE (domain)));
	      goto exit;
	    }
	  else
	    {
	      goto format_mismatch;
	    }

	  break;

	case N_SPACE:
	  if (!strchr (WHITE_CHARS, *cs))
	    {
	      goto format_mismatch;
	    }

	  while (cs < last_cs && strchr (WHITE_CHARS, *cs))
	    {
	      cs++;
	    }
	  break;

	case N_TEXT:
	  if (strncasecmp ((format_str_ptr + 1), cs, token_length - 2) != 0)
	    {
	      goto format_mismatch;
	    }
	  cs += token_length - 2;
	  break;

	case N_INVALID:
	  error_status = ER_QSTR_INVALID_FORMAT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;

	case N_END:
	  /* Skip space, tab, CR  */
	  while (cs < last_cs && strchr (WHITE_CHARS, *cs))
	    {
	      cs++;
	    }

	  if (cs != last_cs)
	    {
	      goto format_mismatch;
	    }
	  break;
	}

      while (cs < last_cs && strchr (WHITE_CHARS, *cs))
	{
	  cs++;
	}

      format_str_ptr = next_fsp;

      /* Skip space, tab, CR  */
      while (format_str_ptr < last_format &&
	     strchr (WHITE_CHARS, *format_str_ptr))
	{
	  format_str_ptr++;
	}
    }

  /* Both format and src should end at same time  */
  if (cs != last_cs || format_str_ptr != last_format)
    {
      goto format_mismatch;
    }

  result_num->domain.numeric_info.precision = precision;
  result_num->domain.numeric_info.scale = scale;

  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }

  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }

  return error_status;

format_mismatch:
  while (strchr (WHITE_CHARS, *(last_cs - 1)))
    {
      last_cs--;
    }
  *last_cs = '\0';

  error_status = ER_QSTR_TONUM_FORMAT_MISMATCH;
  if (first_format_str_for_error == dflt_format_str)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      first_cs_for_error, "default");
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      first_cs_for_error, first_format_str_for_error);
    }

exit:
  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }

  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }

  return error_status;
}

/*
 * date_to_char () -
 */
static int
date_to_char (const DB_VALUE * src_value,
	      const DB_VALUE * format_str,
	      const DB_VALUE * date_lang, DB_VALUE * result_str,
	      const TP_DOMAIN * domain)
{
  int error_status = NO_ERROR;
  DB_TYPE src_type;
  char *cur_format_str_ptr, *next_format_str_ptr;
  char *last_format_str_ptr;

  int cur_format_size;
  int cur_format;

  char *result_buf = NULL;
  int result_len = 0;
  int result_size = 0;

  int month = 0, day = 0, year = 0;
  int second = 0, minute = 0, hour = 0, millisecond = 0;

  int i;

  unsigned int tmp_int;
  DB_DATE tmp_date;
  DB_TIME tmp_time;

  bool no_user_format;
  INTL_LANG date_lang_id;

  char stack_buf_format[64];
  char *initial_buf_format = NULL;
  bool do_free_buf_format = false;
  const INTL_CODESET codeset = TP_DOMAIN_CODESET (domain);
  const int collation_id = TP_DOMAIN_COLLATION (domain);
  bool has_user_format = false;
  bool dummy;

  assert (src_value != (DB_VALUE *) NULL);
  assert (result_str != (DB_VALUE *) NULL);
  assert (date_lang != (DB_VALUE *) NULL);

  if (DB_IS_NULL (src_value))
    {
      DB_MAKE_NULL (result_str);
      return error_status;
    }

  src_type = DB_VALUE_DOMAIN_TYPE (src_value);

  if (src_type != DB_TYPE_DATE && src_type != DB_TYPE_TIME
      && src_type != DB_TYPE_TIMESTAMP && src_type != DB_TYPE_DATETIME)
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if (date_lang == NULL || DB_IS_NULL (date_lang))
    {
      error_status = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang),
					     &has_user_format, &dummy);

  no_user_format = (format_str == NULL) || (!has_user_format);

  if (no_user_format)
    {
      int retval = 0;
      switch (src_type)
	{
	case DB_TYPE_DATE:
	  result_buf = (char *) db_private_alloc (NULL, QSTR_DATE_LENGTH + 1);
	  if (result_buf == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      return error_status;
	    }
	  result_len = QSTR_DATE_LENGTH;
	  retval = db_date_to_string (result_buf,
				      QSTR_DATE_LENGTH + 1,
				      DB_GET_DATE (src_value));
	  break;

	case DB_TYPE_TIME:
	  result_buf = (char *) db_private_alloc (NULL, QSTR_TIME_LENGTH + 1);
	  if (result_buf == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      return error_status;
	    }
	  result_len = QSTR_TIME_LENGTH;
	  retval = db_time_to_string (result_buf,
				      QSTR_TIME_LENGTH + 1,
				      DB_GET_TIME (src_value));
	  break;

	case DB_TYPE_TIMESTAMP:
	  result_buf =
	    (char *) db_private_alloc (NULL, QSTR_TIME_STAMPLENGTH + 1);
	  if (result_buf == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      return error_status;
	    }
	  result_len = QSTR_TIME_STAMPLENGTH;
	  retval = db_timestamp_to_string (result_buf,
					   QSTR_TIME_STAMPLENGTH + 1,
					   DB_GET_TIMESTAMP (src_value));
	  break;

	case DB_TYPE_DATETIME:
	  result_buf = (char *) db_private_alloc (NULL,
						  QSTR_DATETIME_LENGTH + 1);
	  if (result_buf == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      return error_status;
	    }
	  result_len = QSTR_DATETIME_LENGTH;
	  retval = db_datetime_to_string (result_buf,
					  QSTR_DATETIME_LENGTH + 1,
					  DB_GET_DATETIME (src_value));
	  break;

	default:
	  break;
	}

      if (retval == 0)
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  db_private_free_and_init (NULL, result_buf);
	  return error_status;
	}

      DB_MAKE_VARCHAR (result_str, result_len, result_buf, result_len,
		       codeset, collation_id);
    }
  else
    {
      INTL_CODESET frmt_codeset;

      assert (!DB_IS_NULL (date_lang));

      if (DB_IS_NULL (format_str))
	{
	  DB_MAKE_NULL (result_str);
	  goto exit;
	}

      /* compute allocation size : trade-off exact size (and small mem usage)
       * vs speed */
      result_len = (DB_GET_STRING_LENGTH (format_str)
		    * QSTR_TO_CHAR_LEN_MULTIPLIER_RATIO);
      result_size = result_len * INTL_CODESET_MULT (codeset);
      if (result_size > MAX_TOKEN_SIZE)
	{
	  error_status = ER_QSTR_FORMAT_TOO_LONG;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  return error_status;
	}

      if (DB_GET_STRING_SIZE (format_str) == 0)
	{
	  error_status = ER_QSTR_EMPTY_STRING;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto exit;
	}

      frmt_codeset = DB_GET_STRING_CODESET (format_str);

      error_status =
	db_check_or_create_null_term_string (format_str, stack_buf_format,
					     sizeof (stack_buf_format),
					     true, false,
					     &initial_buf_format,
					     &do_free_buf_format);

      if (error_status != NO_ERROR)
	{
	  goto exit;
	}
      cur_format_str_ptr = initial_buf_format;
      last_format_str_ptr = cur_format_str_ptr + strlen (cur_format_str_ptr);

      switch (src_type)
	{
	case DB_TYPE_DATE:
	  db_date_decode (DB_GET_DATE (src_value), &month, &day, &year);
	  break;
	case DB_TYPE_TIME:
	  db_time_decode (DB_GET_TIME (src_value), &hour, &minute, &second);
	  break;
	case DB_TYPE_TIMESTAMP:
	  db_timestamp_decode (DB_GET_TIMESTAMP (src_value), &tmp_date,
			       &tmp_time);
	  db_date_decode (&tmp_date, &month, &day, &year);
	  db_time_decode (&tmp_time, &hour, &minute, &second);
	  break;
	case DB_TYPE_DATETIME:
	  db_datetime_decode (DB_GET_DATETIME (src_value), &month, &day,
			      &year, &hour, &minute, &second, &millisecond);
	  break;
	default:
	  break;
	}

      result_buf = (char *) db_private_alloc (NULL, result_size + 1);
      if (result_buf == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      i = 0;
      cur_format = DT_NORMAL;

      while (i < result_size)
	{
	  int token_case_mode;
	  int token_size;

	  cur_format = get_next_format (cur_format_str_ptr, frmt_codeset,
					src_type, &cur_format_size,
					&next_format_str_ptr);
	  switch (cur_format)
	    {
	    case DT_CC:
	      if (month == 0 && day == 0 && year == 0)
		{
		  goto zerodate_exit;
		}

	      tmp_int = (year / 100) + 1;
	      sprintf (&result_buf[i], "%02d\n", tmp_int);
	      i += 2;
	      cur_format_str_ptr += 2;
	      break;

	    case DT_YYYY:
	      sprintf (&result_buf[i], "%04d\n", year);
	      i += 4;
	      break;

	    case DT_YY:
	      tmp_int = year - (year / 100) * 100;
	      sprintf (&result_buf[i], "%02d\n", tmp_int);
	      i += 2;
	      break;

	    case DT_MM:
	      sprintf (&result_buf[i], "%02d\n", month);
	      i += 2;
	      break;

	    case DT_MONTH:
	    case DT_MON:
	      if (month == 0 && day == 0 && year == 0)
		{
		  goto zerodate_exit;
		}

	      if (*cur_format_str_ptr == 'm')
		{
		  token_case_mode = 1;
		}
	      else if (*(cur_format_str_ptr + 1) == 'O')
		{
		  token_case_mode = 2;
		}
	      else
		{
		  token_case_mode = 0;
		}

	      if (cur_format == DT_MONTH)
		{
		  error_status =
		    print_string_date_token (SDT_MONTH, date_lang_id, codeset,
					     month - 1, token_case_mode,
					     &result_buf[i], &token_size);
		}
	      else		/* cur_format == DT_MON */
		{
		  error_status =
		    print_string_date_token (SDT_MONTH_SHORT, date_lang_id,
					     codeset, month - 1,
					     token_case_mode, &result_buf[i],
					     &token_size);
		}

	      if (error_status != NO_ERROR)
		{
		  db_private_free_and_init (NULL, result_buf);
		  goto exit;
		}

	      i += token_size;
	      break;

	    case DT_Q:
	      if (month == 0 && day == 0 && year == 0)
		{
		  goto zerodate_exit;
		}

	      result_buf[i] = '1' + ((month - 1) / 3);
	      i++;
	      break;

	    case DT_DD:
	      sprintf (&result_buf[i], "%02d\n", day);
	      i += 2;
	      break;

	    case DT_DAY:
	    case DT_DY:
	      if (month == 0 && day == 0 && year == 0)
		{
		  goto zerodate_exit;
		}

	      tmp_int = get_day (month, day, year);

	      if (*cur_format_str_ptr == 'd')
		{
		  token_case_mode = 1;
		}
	      else if (*(cur_format_str_ptr + 1) == 'A')	/* "DAY" */
		{
		  token_case_mode = 2;
		}
	      else if (*(cur_format_str_ptr + 1) == 'Y')	/* "DY" */
		{
		  token_case_mode = 2;
		}
	      else
		{
		  token_case_mode = 0;
		}

	      if (cur_format == DT_DAY)
		{
		  error_status =
		    print_string_date_token (SDT_DAY, date_lang_id, codeset,
					     tmp_int, token_case_mode,
					     &result_buf[i], &token_size);
		}
	      else		/* cur_format == DT_DY */
		{
		  error_status =
		    print_string_date_token (SDT_DAY_SHORT, date_lang_id,
					     codeset, tmp_int,
					     token_case_mode, &result_buf[i],
					     &token_size);
		}

	      if (error_status != NO_ERROR)
		{
		  db_private_free_and_init (NULL, result_buf);
		  goto exit;
		}

	      i += token_size;
	      break;

	    case DT_D:
	      if (month == 0 && day == 0 && year == 0)
		{
		  goto zerodate_exit;
		}

	      tmp_int = get_day (month, day, year);
	      result_buf[i] = '0' + tmp_int + 1;	/* sun=1 */
	      i += 1;
	      break;

	    case DT_AM:
	    case DT_PM:
	      {
		int am_pm_id = -1;
		int am_pm_len = 0;

		if (0 <= hour && hour <= 11)
		  {
		    if (*cur_format_str_ptr == 'a'
			|| *cur_format_str_ptr == 'p')
		      {
			am_pm_id = (int) am_NAME;
		      }
		    else if (*(cur_format_str_ptr + 1) == 'm')
		      {
			am_pm_id = (int) Am_NAME;
		      }
		    else
		      {
			am_pm_id = (int) AM_NAME;
		      }
		  }
		else if (12 <= hour && hour <= 23)
		  {
		    if (*cur_format_str_ptr == 'p'
			|| *cur_format_str_ptr == 'a')
		      {
			am_pm_id = (int) pm_NAME;
		      }
		    else if (*(cur_format_str_ptr + 1) == 'm')
		      {
			am_pm_id = (int) Pm_NAME;
		      }
		    else
		      {
			am_pm_id = (int) PM_NAME;
		      }
		  }
		else
		  {
		    error_status = ER_QSTR_INVALID_FORMAT;
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status,
			    0);
		    db_private_free_and_init (NULL, result_buf);
		    goto exit;
		  }

		assert (am_pm_id >= (int) am_NAME &&
			am_pm_id <= (int) P_M_NAME);

		error_status =
		  print_string_date_token (SDT_AM_PM, date_lang_id, codeset,
					   am_pm_id, 0, &result_buf[i],
					   &am_pm_len);

		if (error_status != NO_ERROR)
		  {
		    db_private_free_and_init (NULL, result_buf);
		    goto exit;
		  }

		i += am_pm_len;
	      }
	      break;

	    case DT_A_M:
	    case DT_P_M:
	      {
		int am_pm_id = -1;
		int am_pm_len = 0;

		if (0 <= hour && hour <= 11)
		  {
		    if (*cur_format_str_ptr == 'a'
			|| *cur_format_str_ptr == 'p')
		      {
			am_pm_id = (int) a_m_NAME;
		      }
		    else if (*(cur_format_str_ptr + 2) == 'm')
		      {
			am_pm_id = (int) A_m_NAME;
		      }
		    else
		      {
			am_pm_id = (int) A_M_NAME;
		      }
		  }
		else if (12 <= hour && hour <= 23)
		  {
		    if (*cur_format_str_ptr == 'p'
			|| *cur_format_str_ptr == 'a')
		      {
			am_pm_id = (int) p_m_NAME;
		      }
		    else if (*(cur_format_str_ptr + 2) == 'm')
		      {
			am_pm_id = (int) P_m_NAME;
		      }
		    else
		      {
			am_pm_id = (int) P_M_NAME;
		      }
		  }
		else
		  {
		    error_status = ER_QSTR_INVALID_FORMAT;
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status,
			    0);
		    db_private_free_and_init (NULL, result_buf);
		    goto exit;
		  }

		assert (am_pm_id >= (int) am_NAME &&
			am_pm_id <= (int) P_M_NAME);

		error_status =
		  print_string_date_token (SDT_AM_PM, date_lang_id, codeset,
					   am_pm_id, 0, &result_buf[i],
					   &am_pm_len);

		if (error_status != NO_ERROR)
		  {
		    db_private_free_and_init (NULL, result_buf);
		    goto exit;
		  }

		i += am_pm_len;
	      }
	      break;

	    case DT_HH:
	    case DT_HH12:
	      tmp_int = hour % 12;
	      if (tmp_int == 0)
		{
		  tmp_int = 12;
		}
	      sprintf (&result_buf[i], "%02d\n", tmp_int);
	      i += 2;
	      break;

	    case DT_HH24:
	      sprintf (&result_buf[i], "%02d\n", hour);
	      i += 2;
	      break;

	    case DT_MI:
	      sprintf (&result_buf[i], "%02d\n", minute);
	      i += 2;
	      break;

	    case DT_SS:
	      sprintf (&result_buf[i], "%02d\n", second);
	      i += 2;
	      break;

	    case DT_MS:
	      sprintf (&result_buf[i], "%03d\n", millisecond);
	      i += 3;
	      break;

	    case DT_INVALID:
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      db_private_free_and_init (NULL, result_buf);
	      goto exit;

	    case DT_NORMAL:
	      memcpy (&result_buf[i], cur_format_str_ptr, cur_format_size);
	      i += cur_format_size;
	      break;

	    case DT_TEXT:
	      memcpy (&result_buf[i], cur_format_str_ptr + 1,
		      cur_format_size - 2);
	      i += cur_format_size - 2;
	      break;

	    case DT_PUNCTUATION:
	      memcpy (&result_buf[i], cur_format_str_ptr, cur_format_size);
	      i += cur_format_size;
	      break;

	    default:
	      break;
	    }

	  cur_format_str_ptr = next_format_str_ptr;
	  if (next_format_str_ptr == last_format_str_ptr)
	    {
	      break;
	    }
	}

      DB_MAKE_VARCHAR (result_str, result_len, result_buf, i,
		       codeset, collation_id);
    }

  result_str->need_clear = true;

exit:
  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }
  return error_status;

zerodate_exit:
  if (result_buf != NULL)
    {
      db_private_free_and_init (NULL, result_buf);
    }
  DB_MAKE_NULL (result_str);
  goto exit;
}

/*
 * number_to_char () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
static int
number_to_char (const DB_VALUE * src_value,
		const DB_VALUE * format_str,
		const DB_VALUE * number_lang, DB_VALUE * result_str,
		const TP_DOMAIN * domain)
{
  int error_status = NO_ERROR;
  char tmp_str[64];
  char *tmp_buf;

  char *cs;			/* current source string pointer     */
  char *format_str_ptr, *last_format;
  char *next_fsp;		/* next format string pointer    */
  int token_length = 0;
  int cur_format;
  char *res_string, *res_ptr;
  int i, j;
  char stack_buf_format[64];
  char *initial_buf_format = NULL;
  bool do_free_buf_format = false;
  INTL_LANG number_lang_id;
  char fraction_symbol;
  char digit_grouping_symbol;
  bool has_user_format = false;
  bool dummy;
  const INTL_CODESET codeset = TP_DOMAIN_CODESET (domain);
  const int collation_id = TP_DOMAIN_COLLATION (domain);
  DB_CURRENCY currency = lang_currency ();

  assert (src_value != (DB_VALUE *) NULL);
  assert (result_str != (DB_VALUE *) NULL);
  assert (number_lang != (DB_VALUE *) NULL);

  if (number_lang == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* now return null */
  if (DB_IS_NULL (src_value))
    {
      DB_MAKE_NULL (result_str);
      return error_status;
    }

  number_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (number_lang),
					       &has_user_format, &dummy);
  fraction_symbol = lang_digit_fractional_symbol (number_lang_id);
  digit_grouping_symbol = lang_digit_grouping_symbol (number_lang_id);

  switch (DB_VALUE_TYPE (src_value))
    {
    case DB_TYPE_NUMERIC:
      tmp_buf = numeric_db_value_print ((DB_VALUE *) src_value);
      cs = (char *) db_private_alloc (NULL, strlen (tmp_buf) + 1);
      if (cs == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error_status;
	}
      if (number_lang_id != INTL_LANG_ENGLISH)
	{
	  convert_locale_number (tmp_buf, strlen (tmp_buf), INTL_LANG_ENGLISH,
				 number_lang_id);
	}
      strcpy (cs, tmp_buf);
      break;

    case DB_TYPE_INTEGER:
      sprintf (tmp_str, "%d", DB_GET_INTEGER (src_value));
      cs = (char *) db_private_alloc (NULL, strlen (tmp_str) + 1);
      if (cs == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error_status;
	}
      strcpy (cs, tmp_str);
      break;

    case DB_TYPE_BIGINT:
      sprintf (tmp_str, "%lld", (long long) DB_GET_BIGINT (src_value));
      cs = (char *) db_private_alloc (NULL, strlen (tmp_str) + 1);
      if (cs == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error_status;
	}
      strcpy (cs, tmp_str);
      break;

    case DB_TYPE_SMALLINT:
      sprintf (tmp_str, "%d", DB_GET_SMALLINT (src_value));
      cs = (char *) db_private_alloc (NULL, strlen (tmp_str) + 1);
      if (cs == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error_status;
	}
      strcpy (cs, tmp_str);
      break;

    case DB_TYPE_FLOAT:
      sprintf (tmp_str, "%.6e", DB_GET_FLOAT (src_value));
      if (number_lang_id != INTL_LANG_ENGLISH)
	{
	  convert_locale_number (tmp_str, strlen (tmp_str), INTL_LANG_ENGLISH,
				 number_lang_id);
	}
      if (scientific_to_decimal_string (number_lang_id, tmp_str, &cs) !=
	  NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      break;

    case DB_TYPE_DOUBLE:
      sprintf (tmp_str, "%.15e", DB_GET_DOUBLE (src_value));
      if (number_lang_id != INTL_LANG_ENGLISH)
	{
	  convert_locale_number (tmp_str, strlen (tmp_str), INTL_LANG_ENGLISH,
				 number_lang_id);
	}
      if (scientific_to_decimal_string (number_lang_id, tmp_str, &cs) !=
	  NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      break;

    case DB_TYPE_MONETARY:
      currency = (DB_GET_MONETARY (src_value))->type;
      sprintf (tmp_str, "%.15e", (DB_GET_MONETARY (src_value))->amount);
      if (number_lang_id != INTL_LANG_ENGLISH)
	{
	  convert_locale_number (tmp_str, strlen (tmp_str), INTL_LANG_ENGLISH,
				 number_lang_id);
	}
      if (scientific_to_decimal_string (number_lang_id, tmp_str, &cs) !=
	  NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      break;

    default:
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  /*        Remove    'trailing zero' source string    */
  for (i = 0; i < strlen (cs); i++)
    {
      if (cs[i] == fraction_symbol)
	{
	  i = strlen (cs);
	  i--;
	  while (cs[i] == '0')
	    {
	      i--;
	    }
	  if (cs[i] == fraction_symbol)
	    {
	      cs[i] = '\0';
	    }
	  else
	    {
	      i++;
	      cs[i] = '\0';
	    }
	  break;
	}
    }

  if (format_str == NULL || !has_user_format)
    {
      /*    Caution: VARCHAR's Size        */
      DB_MAKE_VARCHAR (result_str, (ssize_t) strlen (cs), cs, strlen (cs),
		       codeset, collation_id);
      result_str->need_clear = true;
      return error_status;
    }
  else
    {
      if (DB_IS_NULL (format_str))
	{
	  db_private_free_and_init (NULL, cs);
	  DB_MAKE_NULL (result_str);
	  return error_status;
	}

      /*    Format string type checking     */
      if (is_char_string (format_str))
	{
	  if (DB_GET_STRING_SIZE (format_str) > MAX_TOKEN_SIZE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      db_private_free_and_init (NULL, cs);
	      return error_status;
	    }

	  error_status =
	    db_check_or_create_null_term_string (format_str, stack_buf_format,
						 sizeof (stack_buf_format),
						 true, false,
						 &initial_buf_format,
						 &do_free_buf_format);

	  if (error_status != NO_ERROR)
	    {
	      goto exit;
	    }
	  format_str_ptr = initial_buf_format;
	  last_format = format_str_ptr + strlen (format_str_ptr);
	}
      else
	{
	  error_status = ER_QSTR_INVALID_DATA_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  db_private_free_and_init (NULL, cs);
	  return error_status;
	}

      if (DB_GET_STRING_SIZE (format_str) == 0)
	{
	  error_status = ER_QSTR_EMPTY_STRING;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  db_private_free_and_init (NULL, cs);
	  goto exit;
	}

      /*    Memory allocation for result                            */
      /*    size is bigger two times than strlen(format_str_ptr)    */
      /*        because of format 'C'(currency)                        */
      /*        'C' can be  expanded accoding to CODE_SET            */
      /*        +1 implies minus -                                     */
      res_string =
	(char *) db_private_alloc (NULL, strlen (format_str_ptr) * 2 + 1);
      if (res_string == NULL)
	{
	  db_private_free_and_init (NULL, cs);
	  assert (er_errid () != NO_ERROR);
	  error_status = er_errid ();
	  goto exit;
	}

      res_ptr = res_string;

      /* Skip space, tab, CR     */
      while (strchr (WHITE_CHARS, *cs))
	{
	  cs++;
	}

      while (format_str_ptr != last_format)
	{
	  cur_format = get_number_token (number_lang_id, format_str_ptr,
					 &token_length, last_format,
					 &next_fsp);
	  switch (cur_format)
	    {
	    case N_FORMAT:
	      if (make_number_to_char (number_lang_id, cs, format_str_ptr,
				       &token_length, currency,
				       &res_ptr) != NO_ERROR)
		{
		  error_status = ER_QSTR_INVALID_FORMAT;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
		  db_private_free_and_init (NULL, cs);
		  db_private_free_and_init (NULL, res_string);
		  goto exit;
		}
	      /*    Remove space character between sign,curerency and number */
	      i = 0;
	      j = 0;
	      while (i < token_length)
		{
		  DB_CURRENCY currency = DB_CURRENCY_NULL;
		  int symbol_size = 0;

		  /*    check currency symbols */
		  if (intl_is_currency_symbol (&(res_ptr[i]), &currency,
					       &symbol_size,
					       CURRENCY_CHECK_MODE_CONSOLE |
					       CURRENCY_CHECK_MODE_UTF8))
		    {
		      i += symbol_size;
		    }
		  else if (res_ptr[i] == '+' || res_ptr[i] == '-')
		    {
		      i += 1;
		    }
		  else if (res_ptr[i] == ' ')
		    {
		      while (res_ptr[i + j] == ' ')
			{
			  j++;
			}
		      while (i > 0)
			{
			  i--;
			  res_ptr[i + j] = res_ptr[i];
			  res_ptr[i] = ' ';
			}
		      break;
		    }
		  else
		    {
		      break;
		    }
		}
	      res_ptr += token_length;
	      break;
	    case N_SPACE:
	      strncpy (res_ptr, format_str_ptr, token_length);
	      res_ptr += token_length;
	      break;
	    case N_TEXT:
	      strncpy (res_ptr, (format_str_ptr + 1), token_length - 2);
	      res_ptr += token_length - 2;
	      break;
	    case N_INVALID:
	      error_status = ER_QSTR_INVALID_FORMAT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	      db_private_free_and_init (NULL, cs);
	      db_private_free_and_init (NULL, res_string);
	      goto exit;
	    case N_END:
	      *res_ptr = '\0';
	      break;
	    }

	  format_str_ptr = next_fsp;
	}

      *res_ptr = '\0';
    }

  /* Both format and src should end at same time     */
  if (format_str_ptr != last_format)
    {
      error_status = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      db_private_free_and_init (NULL, cs);
      db_private_free_and_init (NULL, res_string);

      goto exit;
    }

  DB_MAKE_VARCHAR (result_str, (ssize_t) strlen (res_string), res_string,
		   strlen (res_string), codeset, collation_id);
  result_str->need_clear = true;
  db_private_free_and_init (NULL, cs);

exit:
  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }
  return error_status;
}

/*
 * lob_to_bit_char ()
 */
static int
lob_to_bit_char (const DB_VALUE * src_value, DB_VALUE * result_value,
		 DB_TYPE lob_type, int max_length)
{
  int error_status = NO_ERROR;
  DB_ELO *elo;
  char *cs = NULL;		/* current source string pointer */
  INT64 size = 0LL;

  assert (lob_type == DB_TYPE_BLOB || lob_type == DB_TYPE_CLOB);

  elo = db_get_elo (src_value);
  if (elo)
    {
      size = db_elo_size (elo);
      if (size < 0)
	{
	  if (er_errid () == ER_ES_GENERAL)
	    {
	      /* by the spec, some lob handling functions treats the read
	         error as a NULL value */
	      DB_MAKE_NULL (result_value);
	      /* clear the error set before */
	      er_clear ();
	      return NO_ERROR;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QSTR_BAD_LENGTH, 1, size);
	  return ER_QSTR_BAD_LENGTH;
	}
      if (max_length < 0 || max_length > DB_MAX_STRING_LENGTH)
	{
	  max_length = DB_MAX_STRING_LENGTH;
	}
      if (lob_type == DB_TYPE_BLOB)
	{
	  /* convert max_length, which is a number of bits,
	   * to number of bytes to read */
	  max_length = QSTR_NUM_BYTES (max_length);
	}
      if (max_length > size)
	{
	  max_length = (int) size;
	}

      cs = (char *) db_private_alloc (NULL, max_length + 1);
      if (cs == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error_status;
	}
      if (max_length > 0)
	{
	  error_status = db_elo_read (elo, 0, cs, max_length, NULL);
	  if (error_status == ER_ES_GENERAL)
	    {
	      /* by the spec, some lob handling functions treats the read
	         error as a NULL value */
	      DB_MAKE_NULL (result_value);
	      db_private_free_and_init (NULL, cs);

	      /* clear the error set before */
	      er_clear ();
	      return NO_ERROR;
	    }
	  else if (error_status < 0)
	    {
	      db_private_free_and_init (NULL, cs);
	      return error_status;
	    }
	}
      cs[max_length] = '\0';

      if (lob_type == DB_TYPE_BLOB)
	{
	  /* convert the converted max_length to number of bits */
	  max_length *= 8;
	  DB_MAKE_VARBIT (result_value, max_length, cs, max_length);
	}
      else
	{
	  DB_MAKE_VARCHAR (result_value, max_length, cs, max_length,
			   LANG_COERCIBLE_CODESET, LANG_COERCIBLE_COLL);
	}
      result_value->need_clear = true;
    }
  else
    {
      DB_MAKE_NULL (result_value);
    }
  return error_status;
}

/*
 * lob_from_file () -
 */
static int
lob_from_file (const char *path, const DB_VALUE * src_value,
	       DB_VALUE * lob_value, DB_TYPE lob_type)
{
  int error_status = NO_ERROR;
  DB_ELO temp_elo, *result_elo;
  INT64 size, chk_size;
  off_t pos;
  char lob_chunk[LOB_CHUNK_SIZE + 1];

  assert (lob_type == DB_TYPE_BLOB || lob_type == DB_TYPE_CLOB);

  elo_init_structure (&temp_elo);
  temp_elo.type = ELO_FBO;
  temp_elo.locator = (char *) path;
  size = db_elo_size (&temp_elo);
  if (size < 0)
    {
      error_status = ER_ES_INVALID_PATH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status,
	      1, DB_PULL_STRING (src_value));
      return error_status;
    }

  error_status = db_create_fbo (lob_value, lob_type);
  if (error_status != NO_ERROR)
    {
      return error_status;
    }
  result_elo = db_get_elo (lob_value);

  pos = 0;
  while (size > 0)
    {
      chk_size = (size < LOB_CHUNK_SIZE) ? size : LOB_CHUNK_SIZE;
      error_status = db_elo_read (&temp_elo, pos, lob_chunk,
				  chk_size, &chk_size);
      if (error_status < 0)
	{
	  return error_status;
	}
      error_status = db_elo_write (result_elo, pos, lob_chunk,
				   chk_size, NULL);
      if (error_status < 0)
	{
	  return error_status;
	}
      size -= chk_size;
      pos += chk_size;
    }

  return NO_ERROR;
}

/*
 * lob_length () -
 */
static int
lob_length (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  int error_status = NO_ERROR;
  DB_ELO *elo;
  INT64 length;

  elo = db_get_elo (src_value);
  if (elo)
    {
      /*
       * Hack:
       * In order to check the existence of the file,
       * it is required to make to invoke real file operation.
       * Because elo_size() will return the cached elo->size,
       * we need to reset it to -1.
       */
      elo->size = -1;
      length = db_elo_size (elo);
      if (length < 0)
	{
	  if (er_errid () == ER_ES_GENERAL)
	    {
	      /* by the spec, some lob handling functions treats the read
	         error as a NULL value */
	      DB_MAKE_NULL (result_value);
	      /* clear the error set before */
	      er_clear ();
	      return NO_ERROR;
	    }
	  error_status = (int) length;
	}
      else
	{
	  db_make_bigint (result_value, length);
	}
    }
  else
    {
      DB_MAKE_NULL (result_value);
    }
  return error_status;
}

/*
 * make_number_to_char () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
static int
make_number_to_char (const INTL_LANG lang, char *num_string,
		     char *format_str, int *length, DB_CURRENCY currency,
		     char **result_str)
{
  int flag_sign = 1;
  int leadingzero = false;
  char *res_str = *result_str;
  char *num, *format, *res;
  char *init_format = format_str;

  char format_end_char = init_format[*length];
  const char fraction_symbol = lang_digit_fractional_symbol (lang);
  const char digit_grouping_symbol = lang_digit_grouping_symbol (lang);

  init_format[*length] = '\0';

  /* code for patch..     emm..   */
  if (strlen (format_str) == 5 && !strncasecmp (format_str, "seeee", 5))
    {
      return ER_FAILED;
    }
  else if (strlen (format_str) == 5 && !strncasecmp (format_str, "ceeee", 5))
    {
      return ER_FAILED;
    }
  else if (strlen (format_str) == 6 && !strncasecmp (format_str, "sceeee", 6))
    {
      return ER_FAILED;
    }

  /*              Check minus                     */
  if (*num_string == '-')
    {
      *res_str = '-';
      num_string++;
      res_str++;
      flag_sign = -1;
    }

  /*              Check sign                      */
  if (char_tolower (*format_str) == 's')
    {
      if (flag_sign == 1)
	{
	  *res_str = '+';
	  res_str++;
	}
      format_str++;
    }

  if (*format_str == '\0')
    {
      init_format[*length] = format_end_char;
      /* patch for format: '9999 s'   */
      *res_str = '\0';

      *length = strlen (*result_str);
      return NO_ERROR;
    }

  /*              Check currency          */
  if (char_tolower (*format_str) == 'c')
    {
      const char *money_symbol = intl_get_money_symbol (currency);

      strcpy (res_str, money_symbol);
      res_str += strlen (money_symbol);

      format_str++;
    }

  if (*format_str == '\0')
    {
      init_format[*length] = format_end_char;
      /* patch for format: '9999 s'   */
      *res_str = '\0';
      *length = strlen (*result_str);

      return NO_ERROR;
    }

  /* So far, format:'s','c' are settled   */
  if (*length > 4 && !strncasecmp (&init_format[*length - 4], "eeee", 4))
    {
      int cipher = 0;

      num = num_string;
      format = format_str;

      if (*num == '0')
	{
	  num++;
	  if (*num == '\0')
	    {
	      while (*format == '0' || *format == '9' ||
		     *format == digit_grouping_symbol)
		{
		  format++;
		}

	      if (*format == fraction_symbol)
		{
		  *res_str = '0';
		  res_str++;

		  format++;

		  *res_str = fraction_symbol;
		  res_str++;

		  while (1)
		    {
		      if (*format == '0' || *format == '9')
			{
			  *res_str = '0';
			  res_str++;
			  format++;
			}
		      else if (char_tolower (*format) == 'e')
			{
			  *res_str = '\0';
			  init_format[*length] = format_end_char;
			  make_scientific_notation (*result_str, cipher);
			  *length = strlen (*result_str);

			  return NO_ERROR;
			}
		      else
			{
			  return ER_FAILED;
			}
		    }
		}
	      else if (*format == 'e')
		{
		  *res_str = '0';
		  res_str++;
		  *res_str = '\0';
		  init_format[*length] = format_end_char;
		  make_scientific_notation (*result_str, cipher);
		  *length = strlen (*result_str);

		  return NO_ERROR;
		}
	      else
		{
		  return ER_FAILED;
		}
	    }
	  else if (*num == fraction_symbol)
	    {
	      num++;
	      while (1)
		{
		  if (*num == '0')
		    {
		      cipher--;
		      num++;
		    }
		  else if (char_isdigit (*num))
		    {
		      cipher--;
		      break;
		    }
		  else if (char_tolower (*num) == 'e')
		    {
		      break;
		    }
		  else if (*num == '\0')
		    {
		      return ER_FAILED;
		    }
		  else
		    {
		      return ER_FAILED;
		    }
		}
	    }
	  else
	    {
	      return ER_FAILED;
	    }
	}
      else
	{
	  while (1)
	    {
	      if (char_isdigit (*num))
		{
		  cipher++;
		  num++;
		}
	      else if (*num == fraction_symbol || *num == '\0')
		{
		  cipher--;
		  break;
		}
	      else
		{
		  return ER_FAILED;
		}
	    }
	}

      while (*format == '0' || *format == '9' ||
	     *format == digit_grouping_symbol)
	{
	  format++;
	}

      if (*format != fraction_symbol && char_tolower (*format) != 'e')
	{
	  return ER_FAILED;
	}

      num = num_string;
      res = res_str;

      while (1)
	{
	  if ('0' < *num && *num <= '9')
	    {
	      *res = *num;
	      res++;
	      num++;
	      break;
	    }
	  else
	    {
	      num++;
	    }
	}

      if (char_tolower (*format) == 'e')
	{
	  *res = '\0';
	  if (*num == fraction_symbol)
	    {
	      num++;
	      if (char_isdigit (*num) && *num - '0' > 4)
		{
		  roundoff (lang, *result_str, 1, &cipher, (char *) NULL);
		}
	    }
	  else if (char_isdigit (*num))
	    {
	      if (char_isdigit (*num) && *num - '0' > 4)
		{
		  roundoff (lang, *result_str, 1, &cipher, (char *) NULL);
		}
	    }
	  else if (*num == '\0')
	    {
	      /* do nothing */
	    }
	  else
	    {
	      return ER_FAILED;
	    }

	  /*      emm     */
	  init_format[*length] = format_end_char;
	  make_scientific_notation (*result_str, cipher);
	  *length = strlen (*result_str);

	  return NO_ERROR;
	}
      else
	{
	  *res = *format;
	  res++;
	  format++;
	}

      while (1)
	{
	  if (*format == '0' || *format == '9')
	    {
	      if (*num == fraction_symbol)
		{
		  num++;
		  *res = *num;
		}
	      else if (*num == '\0')
		{
		  while (*format == '0' || *format == '9')
		    {
		      *res = '0';
		      format++;
		      res++;
		    }

		  if (char_tolower (*format) != 'e')
		    {
		      return ER_FAILED;
		    }

		  *res = '\0';
		  init_format[*length] = format_end_char;
		  make_scientific_notation (*result_str, cipher);
		  *length = strlen (*result_str);

		  return NO_ERROR;
		}
	      else
		{
		  *res = *num;
		}

	      format++;
	      res++;
	      num++;
	    }
	  else if (char_tolower (*format) == 'e')
	    {
	      if (strlen (format) > 4)
		{
		  return ER_FAILED;
		}

	      if (*num == '\0')
		{
		  *res = '\0';
		  init_format[*length] = format_end_char;
		  make_scientific_notation (*result_str, cipher);
		  *length = strlen (*result_str);

		  return NO_ERROR;
		}
	      else
		{
		  *res = '\0';
		  /*      patch                   */
		  if (*num == fraction_symbol && *(num + 1) - '0' > 4)
		    {
		      roundoff (lang, *result_str, 1, &cipher, (char *) NULL);
		    }
		  if (*num - '0' > 4)
		    {
		      roundoff (lang, *result_str, 1, &cipher, (char *) NULL);
		    }
		  /*      emm     */
		  init_format[*length] = format_end_char;
		  make_scientific_notation (*result_str, cipher);
		  *length = strlen (*result_str);

		  return NO_ERROR;
		}
	    }
	  else
	    {
	      return ER_FAILED;
	    }
	}
    }
  /* So far, format:scientific notation are settled       */

  /*              Check leading zero              */
  if (*format_str == '0')
    {
      leadingzero = true;
    }

  num = num_string;
  format = format_str;

  /*      Scan unitl '.' or '\0' of both num or format    */
  while (char_isdigit (*num))
    {
      num++;
    }

  while (*format == '0' || *format == '9' || *format == digit_grouping_symbol)
    {
      format++;
    }

  if (*format != fraction_symbol && *format != '\0')
    {
      return ER_FAILED;
    }

  /* '.' or '\0' is copied into middle or last position of res_string */
  *(res_str + (format - format_str)) = *format;
  res = res_str + (format - format_str);

  /*      num: .xxx       format: .xxx    */
  if (format == format_str && num == num_string)
    {
      ;
    }
  /*      num: .xxx       format: xxx.xxx */
  else if (format != format_str && num == num_string)
    {
      if (leadingzero == true)
	{
	  while (format != format_str)
	    {
	      format--;

	      if (*format == '9' || *format == '0')
		{
		  *(res_str + (format - format_str)) = '0';
		}
	      else if (*format == digit_grouping_symbol)
		{
		  *(res_str + (format - format_str)) = digit_grouping_symbol;
		}
	      else
		{
		  return ER_FAILED;
		}
	    }
	}
      else
	{
	  while (format != format_str)
	    {
	      format--;
	      *(res_str + (format - format_str)) = ' ';
	    }
	}
    }
  /*      num: xxx.xxx    format: .xxx    */
  else if (format == format_str && num != num_string)
    {
      while (num != num_string)
	{
	  num--;
	  if (*num != '0')
	    {
	      /*      Make num be different from num_string   */
	      num = num_string + 1;
	      break;
	    }
	}
    }
  /*      num: xxx.xxx    format: xxx.xxx */
  else
    {
      format--;
      num--;
      /*      if      size of format string is 1              */
      if (format == format_str)
	{
	  *res_str = *num;
	}
      else
	{
	  while (format != format_str)
	    {
	      if (*format == digit_grouping_symbol)
		{
		  *(res_str + (format - format_str)) = *format;
		}
	      else if ((*format == '9' || *format == '0')
		       && num != num_string)
		{
		  *(res_str + (format - format_str)) = *num;
		  num--;
		}
	      else
		{
		  *(res_str + (format - format_str)) = *num;
		  if (leadingzero == true)
		    {
		      while (format != format_str)
			{
			  format--;
			  if (*format == '9' || *format == '0')
			    {
			      *(res_str + (format - format_str)) = '0';
			    }
			  else if (*format == digit_grouping_symbol)
			    {
			      *(res_str + (format - format_str)) =
				digit_grouping_symbol;
			    }
			  else
			    {
			      return ER_FAILED;
			    }
			}
		    }
		  else
		    {
		      while (format != format_str)
			{
			  format--;
			  *(res_str + (format - format_str)) = ' ';
			}
		    }
		  break;
		}
	      format--;
	      if (format == format_str && num == num_string)
		{
		  *(res_str + (format - format_str)) = *num;
		}
	    }
	}
    }

  if (num != num_string)
    {
      int i;

      i = strlen (init_format) - 1;
      while (init_format != &init_format[i])
	{
	  if (init_format[i] == fraction_symbol)
	    {
	      break;
	    }
	  else if (init_format[i] != '0' && init_format[i] != '9' &&
		   init_format[i] != 's' && init_format[i] != 'c' &&
		   init_format[i] != digit_grouping_symbol)
	    {
	      return ER_FAILED;
	    }
	  else
	    {
	      i--;
	    }
	}

      i = 0;
      while (i < *length)
	{
	  (*result_str)[i] = '#';
	  i++;
	}

      (*result_str)[*length] = '\0';
      init_format[*length] = format_end_char;

      return NO_ERROR;
    }
  /* So far, Left side of decimal point is settled        */

  while (char_isdigit (*num))
    {
      num++;
    }

  while (*format == '0' || *format == '9' || *format == digit_grouping_symbol)
    {
      format++;
    }

  if (*format != fraction_symbol && *format != '\0')
    {
      return ER_FAILED;
    }

  if (*format == fraction_symbol && *num == fraction_symbol)
    {
      res++;
      format++;
      num++;

      while (*format != '\0')
	{
	  if ((*format == '9' || *format == '0') && *num != '\0')
	    {
	      *res = *num;
	      num++;
	      res++;
	    }
	  else
	    {
	      while (*format != '\0')
		{
		  if (*format == '9' || *format == '0')
		    {
		      *res = '0';
		    }
		  else
		    {
		      return ER_FAILED;
		    }

		  format++;
		  res++;
		}

	      *res = '\0';
	      break;
	    }

	  format++;
	}

      *res = '\0';
      if (*num != '\0')
	{
	  /* rounding     */
	  if (*num - '0' > 4)
	    {
	      if (roundoff (lang, *result_str, 0, (int *) NULL, format_str)
		  != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	}
    }
  else if (*format == fraction_symbol && *num == '\0')
    {
      res++;
      format++;

      while (*format != '\0')
	{
	  if (*format == '9' || *format == '0')
	    {
	      *res = '0';
	    }
	  else
	    {
	      return ER_FAILED;
	    }

	  format++;
	  res++;
	}

      *res = '\0';
    }
  else if (*format == '\0' && *num == fraction_symbol)
    {
      if (*(num + 1) - '0' > 4)
	{
	  if (roundoff (lang, *result_str, 0, (int *) NULL, format_str) !=
	      NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      /*      rounding        */
    }
  else if (*format == '\0' && *num == '\0')
    {
      /* Nothing      */
    }
  else
    {
      return ER_FAILED;
    }

  init_format[*length] = format_end_char;
  *length = strlen (*result_str);

  return NO_ERROR;
}

/*
 * make_scientific_notation () -
 */
static int
make_scientific_notation (char *src_string, int cipher)
{
  int leng = strlen (src_string);

  src_string[leng] = 'E';
  leng++;

  if (cipher >= 0)
    {
      src_string[leng] = '+';
    }
  else
    {
      src_string[leng] = '-';
      cipher *= (-1);
    }

  leng++;

  if (cipher > 99)
    {
      sprintf (&src_string[leng], "%d", cipher);
    }
  else
    {
      sprintf (&src_string[leng], "%02d", cipher);
    }

  return NO_ERROR;
}

/*
 * roundoff () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
static int
roundoff (const INTL_LANG lang, char *src_string, int flag, int *cipher,
	  char *format)
{
  int loop_state = true;
  int is_overflow = false;
  char *res = &src_string[strlen (src_string)];
  char *for_ptr = NULL;
  int i;
  const char fraction_symbol = lang_digit_fractional_symbol (lang);
  const char digit_grouping_symbol = lang_digit_grouping_symbol (lang);

  if (flag == 0)
    {
      for_ptr = &format[strlen (format)];
    }

  if (*src_string == '\0')
    {
      return ER_FAILED;
    }
  if (flag == 0 && *format == '\0')
    {
      return ER_FAILED;
    }

  res--;

  if (flag == 0)
    {
      for_ptr--;
    }

  while (loop_state)
    {
      if ('0' <= *res && *res <= '9')
	{
	  switch (*res - '0' + 1)
	    {
	    case 1:
	    case 2:
	    case 3:
	    case 4:
	    case 5:
	    case 6:
	    case 7:
	    case 8:
	    case 9:
	      *res = *res + 1;
	      loop_state = false;
	      break;

	    case 10:
	      *res = '0';
	      if (res == src_string)
		{
		  loop_state = false;
		  is_overflow = true;
		}
	      else
		{
		  res--;
		  if (flag == 0)
		    {
		      for_ptr--;
		    }
		}
	      break;
	    }
	}
      else if (*res == fraction_symbol || *res == digit_grouping_symbol)
	{
	  if (res == src_string)
	    {
	      loop_state = false;
	      is_overflow = true;
	    }
	  else
	    {
	      res--;
	      if (flag == 0)
		{
		  for_ptr--;
		}
	    }
	}
      else if (*res == ' ')
	{
	  if (flag == 0 && *for_ptr == digit_grouping_symbol)
	    {
	      *res = digit_grouping_symbol;
	      res--;
	      for_ptr--;
	    }

	  *res = '1';
	  loop_state = false;
	}
      else
	{			/* in case of sign, currency     */
	  loop_state = false;
	  is_overflow = true;
	}
    }

  if (is_overflow)
    {
      if (flag == 0)
	{			/* if decimal format    */
	  i = 0;

	  while (i < strlen (src_string))
	    {
	      src_string[i] = '#';
	      i++;
	    }

	  src_string[i] = '\0';
	}
      else
	{			/*      if scientific format    */
	  i = 0;

	  res = src_string;
	  while (!('0' <= *res && *res <= '9'))
	    {
	      res++;
	    }

	  while (i < strlen (res))
	    {
	      if (i == 0)
		{
		  res[i] = '1';
		}
	      else if (i == 1)
		{
		  res[i] = fraction_symbol;
		}
	      else
		{
		  res[i] = '0';
		}
	      i++;
	    }

	  (*cipher)++;
	  res[i] = '\0';
	}
    }

  return NO_ERROR;
}

/*
 * scientific_to_decimal_string () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
static int
scientific_to_decimal_string (const INTL_LANG lang, char *src_string,
			      char **scientific_str)
{
#define PLUS 1
#define MINUS 0
  int src_len = strlen (src_string);
  int sign = PLUS, exponent_sign = PLUS, cipher = 0;
  char *ptr = src_string;
  char *result_str;
  int i;
  int tmp_digit;
  const char fraction_symbol = lang_digit_fractional_symbol (lang);
  const char digit_grouping_symbol = lang_digit_grouping_symbol (lang);

  while (char_isspace (*ptr))
    {
      ptr++;
    }

  if (*ptr == '+')
    {
      sign = PLUS;
      ptr++;
    }
  else if (*ptr == '-')
    {
      sign = MINUS;
      ptr++;
    }

  tmp_digit = 0;
  while (char_isdigit (*ptr))
    {
      tmp_digit = tmp_digit * 10 + (*ptr - '0');
      ptr++;
    }
  if (tmp_digit >= 10)
    {
      return ER_FAILED;
    }
  if (*ptr != fraction_symbol)
    {
      return ER_FAILED;
    }
  ptr++;
  while (char_isdigit (*ptr))
    {
      ptr++;
    }
  if (*ptr == 'e' || *ptr == 'E')
    {
      ptr++;
    }
  else
    {
      return ER_FAILED;
    }

  if (*ptr == '+')
    {
      exponent_sign = PLUS;
    }
  else if (*ptr == '-')
    {
      exponent_sign = MINUS;
    }
  else
    {
      return ER_FAILED;
    }

  ptr++;
  for (; char_isdigit (*ptr); ptr++)
    {
      cipher = cipher * 10 + (*ptr - '0');
    }
  /* So far, one pass     */
  /* Fron now, two pass   */
  while (char_isspace (*ptr))
    {
      ptr++;
    }
  if (*ptr != '\0')
    {
      return ER_FAILED;
    }
  ptr = src_string;
  while (char_isspace (*ptr))
    {
      ptr++;
    }
  *scientific_str = (char *) db_private_alloc (NULL, src_len + cipher);
  if (*scientific_str == NULL)
    {
      return ER_FAILED;
    }
  /* patch for MemoryTrash   */
  for (i = 0; i < src_len + cipher; i++)
    {
      (*scientific_str)[i] = '\0';
    }

  result_str = *scientific_str;
  if (sign == MINUS)
    {
      *result_str = '-';
      result_str++;
      ptr++;
    }
  if (exponent_sign == PLUS)
    {
      i = 0;
      while (char_isdigit (*ptr))
	{
	  *result_str = *ptr;
	  (result_str)++;
	  ptr++;
	}
      *(result_str + cipher) = fraction_symbol;
      ptr++;
      while (i < cipher || char_isdigit (*ptr))
	{
	  if (*result_str == fraction_symbol)
	    {
	      (result_str)++;
	      continue;
	    }
	  else if (char_isdigit (*ptr))
	    {
	      *result_str = *ptr;
	      ptr++;
	    }
	  else
	    {
	      *result_str = '0';
	    }
	  (result_str)++;
	  i++;
	}
    }
  else
    {
      *result_str = '0';
      result_str++;
      *result_str = fraction_symbol;
      result_str++;
      i = 0;
      while (i < cipher - 1)
	{
	  *result_str = '0';
	  result_str++;
	  i++;
	}
      while (char_isdigit (*ptr) || *ptr == fraction_symbol)
	{
	  if (*ptr == fraction_symbol)
	    {
	      ptr++;
	    }
	  *result_str = *ptr;
	  (result_str)++;
	  ptr++;
	}
    }
  *result_str = '\0';
  return NO_ERROR;
}

/*
 * to_number_next_state () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
static int
to_number_next_state (const int previous_state, const int input_char,
		      const INTL_LANG number_lang_id)
{
  int state_table[7][7] = { {4, 5, 2, 3, -1, 6, -1},
  {4, 5, -1, 3, -1, 6, -1},
  {4, 5, -1, -1, -1, 6, -1},
  {4, 4, -1, -1, 4, 6, 7},
  {5, 5, -1, -1, 5, 6, 7},
  {6, 6, -1, -1, 6, -1, 7},
  {0, 0, 0, 0, 0, 0, 0}
  };
  int state;
  const char fraction_symbol = lang_digit_fractional_symbol (number_lang_id);
  const char digit_grouping_symbol =
    lang_digit_grouping_symbol (number_lang_id);

  if (previous_state == -1)
    {
      return -1;
    }

  switch (char_tolower (input_char))
    {
    case '0':
      state = state_table[previous_state - 1][0];
      break;
    case '9':
      state = state_table[previous_state - 1][1];
      break;
    case 's':
      state = state_table[previous_state - 1][2];
      break;
    case 'c':
      state = state_table[previous_state - 1][3];
      break;
    default:
      if (input_char == digit_grouping_symbol)
	{
	  state = state_table[previous_state - 1][4];
	  break;
	}
      else if (input_char == fraction_symbol)
	{
	  state = state_table[previous_state - 1][5];
	  break;
	}
      state = state_table[previous_state - 1][6];
      break;
    }

  return state;
}

/*
 * to_number_next_state () -
 * Note: assume precision and scale are correct
 *	 This function is localized in relation to fractional and digit
 *	 grouping symbols.
 */
static int
make_number (char *src, char *last_src, INTL_CODESET codeset, char *token,
	     int *token_length, DB_VALUE * r, const int precision,
	     const int scale, const INTL_LANG number_lang_id)
{
  int error_status = NO_ERROR;
  int state = 1;
  int i, j, k;
  char result_str[DB_MAX_NUMERIC_PRECISION + 2];
  char *res_ptr;
  const char fraction_symbol = lang_digit_fractional_symbol (number_lang_id);
  const char digit_grouping_symbol =
    lang_digit_grouping_symbol (number_lang_id);

  result_str[0] = '\0';
  result_str[DB_MAX_NUMERIC_PRECISION] = '\0';
  result_str[DB_MAX_NUMERIC_PRECISION + 1] = '\0';
  *token_length = 0;

  while (state != 7 && src < last_src)
    {
      switch (to_number_next_state (state, *token, number_lang_id))
	{
	case 1:		/* Not reachable state  */
	  break;
	case 2:
	  if (*src == '-')
	    {
	      strncat (result_str, src, 1);
	      src++;
	      (*token_length)++;
	      token++;
	      state = 2;
	    }
	  else if (*src == '+')
	    {
	      src++;
	      (*token_length)++;
	      token++;
	      state = 2;
	    }
	  else
	    {
	      return ER_QSTR_MISMATCHING_ARGUMENTS;
	    }
	  break;
	case 3:
	  {
	    DB_CURRENCY currency = DB_CURRENCY_NULL;
	    int symbol_size = 0;

	    if (intl_is_currency_symbol (src, &currency, &symbol_size,
					 CURRENCY_CHECK_MODE_CONSOLE |
					 CURRENCY_CHECK_MODE_UTF8))
	      {
		src += symbol_size;
		(*token_length) += symbol_size;
		token++;
	      }
	  }
	  state = 3;
	  break;
	case 4:
	case 5:
	  if (*src == '-')
	    {
	      strncat (result_str, src, 1);
	      src++;
	      (*token_length)++;
	    }
	  j = 0;
	  k = 0;
	  while (token[j] == '0' || token[j] == '9' ||
		 token[j] == digit_grouping_symbol)
	    {
	      j++;
	    }
	  while ((&src[k] < last_src) &&
		 (char_isdigit (src[k]) || src[k] == digit_grouping_symbol))
	    {
	      k++;
	    }
	  i = j;

	  if (k > DB_MAX_NUMERIC_PRECISION)
	    {
	      return ER_IT_DATA_OVERFLOW;
	    }
	  if (k > 0)
	    {
	      k--;
	    }
	  j--;
	  while (k > 0 && j > 0)
	    {
	      if (token[j] == digit_grouping_symbol &&
		  src[k] != digit_grouping_symbol)
		{
		  return ER_QSTR_MISMATCHING_ARGUMENTS;
		}
	      k--;
	      j--;
	    }

	  if (k != 0)
	    {			/* format = '99' && src = '4444' */
	      return ER_QSTR_MISMATCHING_ARGUMENTS;
	    }
	  /* patch select to_number('30','9,9') from dual;                */
	  if ((src[k] == digit_grouping_symbol &&
	       token[j] != digit_grouping_symbol) ||
	      (token[j] == digit_grouping_symbol &&
	       src[k] != digit_grouping_symbol))
	    {
	      return ER_QSTR_MISMATCHING_ARGUMENTS;
	    }
	  if (j > 0)
	    {
	      j = 0;
	    }
	  while (src < last_src &&
		 (char_isdigit (*src) || *src == digit_grouping_symbol))
	    {
	      if (*src != digit_grouping_symbol)
		{
		  strncat (result_str, src, 1);
		}
	      (*token_length)++;
	      src++;
	    }
	  token = token + i;
	  state = 4;
	  break;
	case 6:
	  token++;
	  if (*src == fraction_symbol)
	    {
	      strncat (result_str, src, 1);
	      src++;
	      (*token_length)++;
	      while (src < last_src && char_isdigit (*src))
		{
		  if (*token == '0' || *token == '9')
		    {
		      strncat (result_str, src, 1);
		      token++;
		      src++;
		      (*token_length)++;
		    }
		  else
		    {
		      return ER_QSTR_MISMATCHING_ARGUMENTS;
		    }
		}
	    }
	  while (*token == '0' || *token == '9')
	    {
	      token++;
	    }
	  state = 6;
	  break;
	case 7:
	  state = 7;
	  break;
	case -1:
	  return ER_QSTR_MISMATCHING_ARGUMENTS;
	}			/* switch       */
    }				/* while        */

  /* For Scientific notation      */
  if (strlen (token) >= 4 && strncasecmp (token, "eeee", 4) == 0 &&
      char_tolower (*src) == 'e' && (*(src + 1) == '+' || *(src + 1) == '-'))
    {
      strncat (result_str, src, 2);
      src += 2;
      (*token_length) += 2;

      while (src < last_src && char_isdigit (*src))
	{
	  strncat (result_str, src, 1);
	  src += 1;
	  (*token_length) += 1;
	}

      if (scientific_to_decimal_string (number_lang_id, result_str, &res_ptr)
	  != NO_ERROR)
	{
	  return ER_QSTR_MISMATCHING_ARGUMENTS;
	  /* This line needs to be modified to reflect appropriate error */
	}

      /*
       * modify result_str to contain correct string value with respect to
       * the given precision and scale.
       */
      strncpy (result_str, res_ptr, sizeof (result_str) - 1);
      db_private_free_and_init (NULL, res_ptr);

      if (number_lang_id != INTL_LANG_ENGLISH)
	{
	  convert_locale_number (result_str, strlen (result_str),
				 number_lang_id, INTL_LANG_ENGLISH);
	}

      error_status = adjust_precision (result_str, precision, scale);
      if (error_status == DOMAIN_OVERFLOW)
	{
	  return ER_IT_DATA_OVERFLOW;
	}

      if (error_status != NO_ERROR ||
	  numeric_coerce_string_to_num (result_str, strlen (result_str),
					codeset, r) != NO_ERROR)
	{
	  /*       patch for to_number('-1.23e+03','9.99eeee')    */
	  return ER_QSTR_MISMATCHING_ARGUMENTS;
	}
      /* old comment
         DB_MAKE_NUMERIC(r,num,precision,scale);
       */
    }
  else
    {
      if (number_lang_id != INTL_LANG_ENGLISH)
	{
	  convert_locale_number (result_str, strlen (result_str),
				 number_lang_id, INTL_LANG_ENGLISH);
	}
      /*
       * modify result_str to contain correct string value with respect to
       * the given precision and scale.
       */
      error_status = adjust_precision (result_str, precision, scale);
      if (error_status == DOMAIN_OVERFLOW)
	{
	  return ER_IT_DATA_OVERFLOW;
	}

      if (error_status != NO_ERROR ||
	  numeric_coerce_string_to_num (result_str, strlen (result_str),
					codeset, r) != NO_ERROR)
	{
	  return ER_QSTR_MISMATCHING_ARGUMENTS;
	}
    }

  return error_status;
}

/*
 * get_number_token () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
static int
get_number_token (const INTL_LANG lang, char *fsp, int *length,
		  char *last_position, char **next_fsp)
{
  const char fraction_symbol = lang_digit_fractional_symbol (lang);
  const char digit_grouping_symbol = lang_digit_grouping_symbol (lang);
  char c;

  *length = 0;

  if (fsp == last_position)
    {
      return N_END;
    }

  c = char_tolower (fsp[*length]);
  switch (c)
    {
    case 'c':
    case 's':
      if (fsp[*length + 1] == digit_grouping_symbol)
	{
	  return N_INVALID;
	}

      if ((char_tolower (fsp[*length + 1]) == 'c' ||
	   char_tolower (fsp[*length + 1]) == 's') &&
	  fsp[*length + 2] == digit_grouping_symbol)
	{
	  return N_INVALID;
	}

    case '9':
    case '0':
      while (fsp[*length] == '9' || fsp[*length] == '0' ||
	     char_tolower (fsp[*length]) == 's' ||
	     char_tolower (fsp[*length]) == 'c' ||
	     fsp[*length] == fraction_symbol ||
	     fsp[*length] == digit_grouping_symbol)
	{
	  *length += 1;
	}

      *next_fsp = &fsp[*length];
      if (strlen (*next_fsp) >= 4 && !strncasecmp (*next_fsp, "eeee", 4))
	{
	  *length += 4;
	  *next_fsp = &fsp[*length];
	}
      return N_FORMAT;

    case ' ':
    case '\t':
    case '\n':
      while (last_position != &fsp[*length]
	     && (fsp[*length] == ' ' || fsp[*length] == '\t'
		 || fsp[*length] == '\n'))
	{
	  *length += 1;
	}
      *next_fsp = &fsp[*length];
      return N_SPACE;

    case '"':
      *length += 1;
      while (fsp[*length] != '"')
	{
	  if (&fsp[*length] == last_position)
	    {
	      return N_INVALID;
	    }
	  *length += 1;
	}
      *length += 1;
      *next_fsp = &fsp[*length];
      return N_TEXT;

    default:
      if (c == fraction_symbol)
	{
	  while (fsp[*length] == '9' || fsp[*length] == '0' ||
		 char_tolower (fsp[*length]) == 's' ||
		 char_tolower (fsp[*length]) == 'c' ||
		 fsp[*length] == fraction_symbol
		 || fsp[*length] == digit_grouping_symbol)
	    {
	      *length += 1;
	    }

	  *next_fsp = &fsp[*length];
	  if (strlen (*next_fsp) >= 4 && !strncasecmp (*next_fsp, "eeee", 4))
	    {
	      *length += 4;
	      *next_fsp = &fsp[*length];
	    }
	  return N_FORMAT;
	}
      return N_INVALID;
    }
}

/*
 * get_number_format () -
 */
static int
get_next_format (char *sp, const INTL_CODESET codeset, DB_TYPE str_type,
		 int *format_length, char **next_pos)
{
  /* sp : start position          */
  *format_length = 0;

  switch (char_tolower (*sp))
    {
    case 'y':
      if (str_type == DB_TYPE_TIME)
	{
	  return DT_INVALID;
	}

      if (strncasecmp (sp, "yyyy", 4) == 0)
	{
	  *format_length += 4;
	  *next_pos = sp + *format_length;
	  return DT_YYYY;
	}
      else if (strncasecmp (sp, "yy", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_YY;
	}
      else
	{
	  return DT_INVALID;
	}

    case 'd':
      if (str_type == DB_TYPE_TIME)
	{
	  return DT_INVALID;
	}

      if (strncasecmp (sp, "dd", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_DD;
	}
      else if (strncasecmp (sp, "dy", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_DY;
	}
      else if (strncasecmp (sp, "day", 3) == 0)
	{
	  *format_length += 3;
	  *next_pos = sp + *format_length;
	  return DT_DAY;
	}
      else
	{
	  *format_length += 1;
	  *next_pos = sp + *format_length;
	  return DT_D;
	}

    case 'c':
      if (str_type == DB_TYPE_TIME)
	{
	  return DT_INVALID;
	}

      if (strncasecmp (sp, "cc", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_CC;
	}
      else
	{
	  return DT_INVALID;
	}

    case 'q':
      if (str_type == DB_TYPE_TIME)
	{
	  return DT_INVALID;
	}

      *format_length += 1;
      *next_pos = sp + *format_length;
      return DT_Q;

    case 'm':
      if (str_type != DB_TYPE_TIME && strncasecmp (sp, "mm", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_MM;
	}
      else if (str_type != DB_TYPE_TIME && strncasecmp (sp, "month", 5) == 0)
	{
	  *format_length += 5;
	  *next_pos = sp + *format_length;
	  return DT_MONTH;
	}
      else if (str_type != DB_TYPE_TIME && strncasecmp (sp, "mon", 3) == 0)
	{
	  *format_length += 3;
	  *next_pos = sp + *format_length;
	  return DT_MON;
	}
      else if (str_type != DB_TYPE_DATE && strncasecmp (sp, "mi", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_MI;
	}
      else
	{
	  return DT_INVALID;
	}

    case 'a':
      if (str_type == DB_TYPE_DATE)
	{
	  return DT_INVALID;
	}

      if (strncasecmp (sp, "am", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_AM;
	}
      else if (strncasecmp (sp, "a.m.", 4) == 0)
	{
	  *format_length += 4;
	  *next_pos = sp + *format_length;
	  return DT_A_M;
	}
      else
	{
	  return DT_INVALID;
	}

    case 'p':
      if (str_type == DB_TYPE_DATE)
	{
	  return DT_INVALID;
	}

      if (strncasecmp (sp, "pm", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_PM;
	}
      else if (strncasecmp (sp, "p.m.", 4) == 0)
	{
	  *format_length += 4;
	  *next_pos = sp + *format_length;
	  return DT_P_M;
	}
      else
	{
	  return DT_INVALID;
	}

    case 'h':
      if (str_type == DB_TYPE_DATE)
	{
	  return DT_INVALID;
	}

      if (strncasecmp (sp, "hh24", 4) == 0)
	{
	  *format_length += 4;
	  *next_pos = sp + *format_length;
	  return DT_HH24;
	}
      else if (strncasecmp (sp, "hh12", 4) == 0)
	{
	  *format_length += 4;
	  *next_pos = sp + *format_length;
	  return DT_HH12;
	}
      else if (strncasecmp (sp, "hh", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_HH;
	}
      else if (strncasecmp (sp, "h", 1) == 0)
	{
	  *format_length += 1;
	  *next_pos = sp + *format_length;
	  return DT_H;
	}
      else
	{
	  return DT_INVALID;
	}

    case 's':
      if (str_type == DB_TYPE_DATE)
	{
	  return DT_INVALID;
	}

      if (strncasecmp (sp, "ss", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_SS;
	}
      else
	{
	  return DT_INVALID;
	}

    case 'f':
      if (str_type == DB_TYPE_DATETIME && strncasecmp (sp, "ff", 2) == 0)
	{
	  *format_length += 2;
	  *next_pos = sp + *format_length;
	  return DT_MS;
	}
      else
	{
	  return DT_INVALID;
	}

    case '"':
      *format_length += 1;
      while (sp[*format_length] != '"')
	{
	  int char_size;
	  unsigned char *ptr = (unsigned char *) sp + (*format_length);
	  if (sp[*format_length] == '\0')
	    {
	      return DT_INVALID;
	    }
	  INTL_NEXT_CHAR (ptr, ptr, codeset, &char_size);
	  *format_length += char_size;
	}
      *format_length += 1;
      *next_pos = &sp[*format_length];
      return DT_TEXT;

    case '-':
    case '/':
      /* this is not a numeric format: it is not necessary to localize point
       * and comma symbols here */
    case ',':
    case '.':
    case ';':
    case ':':
    case ' ':
    case '\t':
    case '\n':
      *format_length += 1;
      *next_pos = sp + *format_length;
      return DT_PUNCTUATION;

    default:
      return DT_INVALID;
    }
}

/*
 * get_cur_year () -
 */
static int
get_cur_year (void)
{
  time_t tloc;
  struct tm *tm, tm_val;

  if (time (&tloc) == -1)
    {
      return -1;
    }

  tm = localtime_r (&tloc, &tm_val);
  if (tm == NULL)
    {
      return -1;
    }

  return tm->tm_year + 1900;
}

/*
 * get_cur_month () -
 */
static int
get_cur_month (void)
{
  time_t tloc;
  struct tm *tm, tm_val;

  if (time (&tloc) == -1)
    {
      return -1;
    }

  tm = localtime_r (&tloc, &tm_val);
  if (tm == NULL)
    {
      return -1;
    }

  return tm->tm_mon + 1;
}

/*
 * get_day () -
 */
int
get_day (int month, int day, int year)
{
  return day_of_week (julian_encode (month, day, year));
}

/*
 * db_format () -
 *
 *  Note :  This function is localized in relation to fractional and digit
 *	    grouping symbols.
 */
int
db_format (const DB_VALUE * value, const DB_VALUE * decimals,
	   const DB_VALUE * number_lang, DB_VALUE * result,
	   const TP_DOMAIN * domain)
{
  DB_TYPE arg1_type, arg2_type;
  int error = NO_ERROR;
  int ndec = 0, i, j;
  const char *integer_format_max =
    "99,999,999,999,999,999,999,999,999,999,999,999,999";
  char format[128];
  DB_VALUE format_val, trim_charset, formatted_val, numeric_val, trimmed_val;
  const DB_VALUE *num_dbval_p = NULL;
  char fraction_symbol;
  char digit_grouping_symbol;
  bool dummy;
  INTL_LANG number_lang_id;

  assert (value != NULL);
  assert (decimals != NULL);
  assert (number_lang != NULL);

  arg1_type = DB_VALUE_DOMAIN_TYPE (value);
  arg2_type = DB_VALUE_DOMAIN_TYPE (decimals);

  if (arg1_type == DB_TYPE_NULL || DB_IS_NULL (value)
      || arg2_type == DB_TYPE_NULL || DB_IS_NULL (decimals))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  assert (DB_VALUE_TYPE (number_lang) == DB_TYPE_INTEGER);
  number_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (number_lang),
					       &dummy, &dummy);
  fraction_symbol = lang_digit_fractional_symbol (number_lang_id);
  digit_grouping_symbol = lang_digit_grouping_symbol (number_lang_id);

  DB_MAKE_NULL (&formatted_val);
  DB_MAKE_NULL (&trimmed_val);

  if (arg2_type == DB_TYPE_INTEGER)
    {
      ndec = DB_GET_INT (decimals);
    }
  else if (arg2_type == DB_TYPE_SHORT)
    {
      ndec = DB_GET_SHORT (decimals);
    }
  else if (arg2_type == DB_TYPE_BIGINT)
    {
      DB_BIGINT bi = DB_GET_BIGINT (decimals);
      if (bi > INT_MAX || bi < 0)
	{
	  goto invalid_argument_error;
	}
      ndec = (int) bi;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  if (ndec < 0)
    {
      goto invalid_argument_error;
    }
  /* 30 is the decimal limit for formating floating points with this function,
     in mysql */
  if (ndec > 30)
    {
      ndec = 30;
    }

  switch (arg1_type)
    {
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      {
	char *c;
	int len, dot = 0;
	/* Trim first because the input string can be given like below:
	 *  - ' 1.1 ', '1.1 ', ' 1.1'
	 */
	db_make_null (&trim_charset);
	error = db_string_trim (BOTH, &trim_charset, value, &trimmed_val);
	if (error != NO_ERROR)
	  {
	    return error;
	  }

	c = DB_GET_STRING (&trimmed_val);
	if (c == NULL)
	  {
	    goto invalid_argument_error;
	  }

	len = strlen (c);

	for (i = 0; i < len; i++)
	  {
	    if (c[i] == fraction_symbol)
	      {
		dot++;
		continue;
	      }
	    if (!char_isdigit (c[i]))
	      {
		goto invalid_argument_error;
	      }
	  }
	if (dot > 1)
	  {
	    goto invalid_argument_error;
	  }

	if (number_lang_id != INTL_LANG_ENGLISH)
	  {
	    convert_locale_number (c, len, number_lang_id, INTL_LANG_ENGLISH);
	  }

	error =
	  numeric_coerce_string_to_num (c, len,
					DB_GET_STRING_CODESET (&trimmed_val),
					&numeric_val);
	if (error != NO_ERROR)
	  {
	    pr_clear_value (&trimmed_val);
	    return error;
	  }

	num_dbval_p = &numeric_val;
	pr_clear_value (&trimmed_val);
      }
      break;

    case DB_TYPE_MONETARY:
      {
	double d = db_value_get_monetary_amount_as_double (value);
	db_make_double (&numeric_val, d);
	num_dbval_p = &numeric_val;
      }
      break;

    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_NUMERIC:
      num_dbval_p = value;
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  /* Make format string. */
  i = snprintf (format, sizeof (format) - 1, "%s", integer_format_max);
  if (number_lang_id != INTL_LANG_ENGLISH)
    {
      convert_locale_number (format, strlen (format), INTL_LANG_ENGLISH,
			     number_lang_id);
    }
  if (ndec > 0)
    {
      format[i++] = fraction_symbol;
      for (j = 0; j < ndec; j++)
	{
	  format[i++] = '9';
	}
      format[i] = '\0';
    }

  db_make_string (&format_val, format);

  error = number_to_char (num_dbval_p, &format_val, number_lang,
			  &formatted_val, domain);
  if (error == NO_ERROR)
    {
      /* number_to_char function returns a string with leading empty characters.
       * So, we need to remove them.
       */
      db_make_null (&trim_charset);
      error = db_string_trim (LEADING, &trim_charset, &formatted_val, result);

      pr_clear_value (&formatted_val);
    }

  return error;

invalid_argument_error:
  if (!DB_IS_NULL (&trimmed_val))
    {
      pr_clear_value (&trimmed_val);
    }
  if (!DB_IS_NULL (&formatted_val))
    {
      pr_clear_value (&formatted_val);
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
  return ER_FAILED;
}

/*
 * db_string_reverse () - reverse the source DB_VALUE string
 *
 *   return:
 *   src_str(in): source DB_VALUE string
 *   result_str(in/out): result DB_VALUE string
 */
int
db_string_reverse (const DB_VALUE * src_str, DB_VALUE * result_str)
{
  int error_status = NO_ERROR;
  DB_TYPE str_type;
  char *res = NULL;

  /*
   *  Assert that DB_VALUE structures have been allocated.
   */
  assert (src_str != (DB_VALUE *) NULL);
  assert (result_str != (DB_VALUE *) NULL);

  /*
   *  Categorize the two input parameters and check for errors.
   *    Verify that the parameters are both character strings.
   */

  str_type = DB_VALUE_DOMAIN_TYPE (src_str);
  if (DB_IS_NULL (src_str))
    {
      DB_MAKE_NULL (result_str);
    }
  else if (!QSTR_IS_ANY_CHAR (str_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
    }
  /*
   *  If the input parameters have been properly validated, then
   *  we are ready to operate.
   */
  else
    {
      res = (char *) db_private_alloc (NULL,
				       DB_GET_STRING_SIZE (src_str) + 1);
      if (res == NULL)
	{
	  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	}

      if (error_status == NO_ERROR)
	{
	  memset (res, 0, DB_GET_STRING_SIZE (src_str) + 1);
	  intl_reverse_string ((unsigned char *) DB_PULL_STRING (src_str),
			       (unsigned char *) res,
			       DB_GET_STRING_LENGTH (src_str),
			       DB_GET_STRING_SIZE (src_str),
			       (INTL_CODESET)
			       DB_GET_STRING_CODESET (src_str));
	  if (QSTR_IS_CHAR (str_type))
	    {
	      DB_MAKE_VARCHAR (result_str, DB_GET_STRING_PRECISION (src_str),
			       res, DB_GET_STRING_SIZE (src_str),
			       DB_GET_STRING_CODESET (src_str),
			       DB_GET_STRING_COLLATION (src_str));
	    }
	  else
	    {
	      DB_MAKE_VARNCHAR (result_str, DB_GET_STRING_PRECISION (src_str),
				res, DB_GET_STRING_SIZE (src_str),
				DB_GET_STRING_CODESET (src_str),
				DB_GET_STRING_COLLATION (src_str));
	    }
	  result_str->need_clear = true;
	}
    }

  return error_status;
}

/*
 * add_and_normalize_date_time ()
 *
 * Arguments: date & time values to modify,
 *	      date & time amounts to add
 *
 * Returns: NO_ERROR/ER_FAILED
 *
 * Errors:
 *
 * Note:
 *    transforms all values in a correct interval (h: 0..23, m: 0..59, etc)
 */
int
add_and_normalize_date_time (int *year, int *month,
			     int *day, int *hour,
			     int *minute, int *second,
			     int *millisecond, DB_BIGINT y, DB_BIGINT m,
			     DB_BIGINT d, DB_BIGINT h, DB_BIGINT mi,
			     DB_BIGINT s, DB_BIGINT ms)
{
  DB_BIGINT days[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  DB_BIGINT i;
  DB_BIGINT _y, _m, _d, _h, _mi, _s, _ms;
  DB_BIGINT old_day = *day;

  _y = *year;
  _m = *month;
  _d = *day;
  _h = *hour;
  _mi = *minute;
  _s = *second;
  _ms = *millisecond;

  _y += y;
  _m += m;
  _d += d;
  _h += h;
  _mi += mi;
  _s += s;
  _ms += ms;

  /* just years and/or months case */
  if (d == 0 && h == 0 && mi == 0 && s == 0 && ms == 0 && (m > 0 || y > 0))
    {
      if (_m % 12 == 0)
	{
	  _y += (_m - 12) / 12;
	  _m = 12;
	}
      else
	{
	  _y += _m / 12;
	  _m %= 12;
	}

      days[2] = LEAP (_y) ? 29 : 28;

      if (old_day > days[_m])
	{
	  _d = days[_m];
	}

      goto set_and_return;
    }

  /* time */
  _s += _ms / 1000;
  _ms %= 1000;

  _mi += _s / 60;
  _s %= 60;

  _h += _mi / 60;
  _mi %= 60;

  _d += _h / 24;
  _h %= 24;

  /* date */
  if (_m > 12)
    {
      _y += _m / 12;
      _m %= 12;

      if (_m == 0)
	{
	  _m = 1;
	}
    }

  days[2] = LEAP (_y) ? 29 : 28;

  if (_d > days[_m])
    {
      /* rewind to 1st january */
      for (i = 1; i < _m; i++)
	{
	  _d += days[i];
	}
      _m = 1;

      /* days for years */
      while (_d >= 366)
	{
	  days[2] = LEAP (_y) ? 29 : 28;
	  _d -= (days[2] == 29) ? 366 : 365;
	  _y++;
	  if (_y > 9999)
	    {
	      goto set_and_return;
	    }
	}

      /* days within a year */
      days[2] = LEAP (_y) ? 29 : 28;
      for (_m = 1;; _m++)
	{
	  if (_d <= days[_m])
	    {
	      break;
	    }
	  _d -= days[_m];
	}
    }

  if (_m == 0)
    {
      _m = 1;
    }
  if (_d == 0)
    {
      _d = 1;
    }

set_and_return:

  if (_y >= 10000 || _y < 0)
    {
      return ER_FAILED;
    }

  *year = (int) _y;
  *month = (int) _m;
  *day = (int) _d;
  *hour = (int) _h;
  *minute = (int) _mi;
  *second = (int) _s;
  *millisecond = (int) _ms;

  return NO_ERROR;
}

/*
 * sub_and_normalize_date_time ()
 *
 * Arguments: date & time values to modify,
 *	      date & time amounts to subtract
 *
 * Returns: NO_ERROR/ER_FAILED
 *
 * Errors:
 *
 * Note:
 *    transforms all values in a correct interval (h: 0..23, m: 0..59, etc)
 */
int
sub_and_normalize_date_time (int *year, int *month,
			     int *day, int *hour,
			     int *minute, int *second,
			     int *millisecond, DB_BIGINT y, DB_BIGINT m,
			     DB_BIGINT d, DB_BIGINT h, DB_BIGINT mi,
			     DB_BIGINT s, DB_BIGINT ms)
{
  DB_BIGINT days[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  DB_BIGINT i;
  DB_BIGINT old_day = *day;
  DB_BIGINT _y, _m, _d, _h, _mi, _s, _ms;

  _y = *year;
  _m = *month;
  _d = *day;
  _h = *hour;
  _mi = *minute;
  _s = *second;
  _ms = *millisecond;

  _y -= y;
  _m -= m;
  _d -= d;
  _h -= h;
  _mi -= mi;
  _s -= s;
  _ms -= ms;

  /* time */
  _s += _ms / 1000;
  _ms %= 1000;
  if (_ms < 0)
    {
      _ms += 1000;
      _s--;
    }

  _mi += _s / 60;
  _s %= 60;
  if (_s < 0)
    {
      _s += 60;
      _mi--;
    }

  _h += _mi / 60;
  _mi %= 60;
  if (_mi < 0)
    {
      _mi += 60;
      _h--;
    }

  _d += _h / 24;
  _h %= 24;
  if (_h < 0)
    {
      _h += 24;
      _d--;
    }

  if (_d == 0)
    {
      _m--;

      if (_m == 0)
	{
	  _y--;
	  days[2] = LEAP (_y) ? 29 : 28;
	  _m = 12;
	}
      _d = days[_m];
    }

  if (_m == 0)
    {
      _y--;
      days[2] = LEAP (_y) ? 29 : 28;
      _m = 12;
    }

  /* date */
  if (_m < 0)
    {
      _y += (_m / 12);
      if (_m % 12 == 0)
	{
	  _m = 1;
	}
      else
	{
	  _m %= 12;
	  if (_m < 0)
	    {
	      _m += 12;
	      _y--;
	    }
	}
    }

  /* just years and/or months case */
  if (d == 0 && h == 0 && mi == 0 && s == 0 && ms == 0 && (m > 0 || y > 0))
    {
      if (_m <= 0)
	{
	  _y += (_m / 12);
	  if (_m % 12 == 0)
	    {
	      _m = 1;
	    }
	  else
	    {
	      _m %= 12;
	      if (_m <= 0)
		{
		  _m += 12;
		  _y--;
		}
	    }
	}

      days[2] = LEAP (_y) ? 29 : 28;

      if (old_day > days[_m])
	{
	  _d = days[_m];
	}

      goto set_and_return;
    }

  days[2] = LEAP (_y) ? 29 : 28;

  if (_d > days[_m] || _d < 0)
    {
      /* rewind to 1st january */
      for (i = 1; i < _m; i++)
	{
	  _d += days[i];
	}
      _m = 1;

      /* days for years */
      while (_d < 0)
	{
	  _y--;
	  if (_y < 0)
	    {
	      goto set_and_return;
	    }
	  days[2] = LEAP (_y) ? 29 : 28;
	  _d += (days[2] == 29) ? 366 : 365;
	}

      /* days within a year */
      days[2] = LEAP (_y) ? 29 : 28;
      for (_m = 1;; _m++)
	{
	  if (_d <= days[_m])
	    {
	      break;
	    }
	  _d -= days[_m];
	}
    }

  if (_m == 0)
    {
      _m = 1;
    }
  if (_d == 0)
    {
      _d = 1;
    }

set_and_return:

  if (_y >= 10000 || _y < 0)
    {
      return ER_FAILED;
    }

  *year = (int) _y;
  *month = (int) _m;
  *day = (int) _d;
  *hour = (int) _h;
  *minute = (int) _mi;
  *second = (int) _s;
  *millisecond = (int) _ms;

  return NO_ERROR;
}

/*
 * db_date_add_sub_interval_days ()
 *
 * Arguments:
 *         date: starting date
 *         db_days: number of days to add
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *    Returns date + an interval of db_days days.
 */
static int
db_date_add_sub_interval_days (DB_VALUE * result, const DB_VALUE * date,
			       const DB_VALUE * db_days, bool is_add)
{
  int error_status = NO_ERROR;
  int days;
  DB_DATETIME db_datetime, *dt_p = NULL;
  DB_TIME db_time;
  DB_DATE db_date, *d_p;
  DB_TIMESTAMP db_timestamp, *ts_p = NULL;
  int is_dt = -1, is_d = -1, is_t = -1, is_timest = -1;
  DB_TYPE res_type;
  char *date_s = NULL, res_s[64];
  int y, m, d, h, mi, s, ms;
  int ret;
  char *res_final;

  res_type = DB_VALUE_DOMAIN_TYPE (date);
  if (res_type == DB_TYPE_NULL || DB_IS_NULL (date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (DB_VALUE_DOMAIN_TYPE (db_days) == DB_TYPE_NULL || DB_IS_NULL (db_days))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* simple case, where just a number of days is added to date */

  days = DB_GET_INT (db_days);

  switch (res_type)
    {
    case DB_TYPE_STRING:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      {
	bool has_explicit_time = false;
	int str_len = DB_GET_STRING_SIZE (date);
	date_s = DB_GET_STRING (date);

	/* try to figure out the string format */
	if (db_date_parse_datetime_parts (date_s, str_len, &db_datetime,
					  &has_explicit_time, NULL, NULL,
					  NULL))
	  {
	    is_dt = ER_TIMESTAMP_CONVERSION;
	    is_timest = ER_TIMESTAMP_CONVERSION;
	    is_d = ER_DATE_CONVERSION;
	    is_t = db_string_to_time_ex (date_s, str_len, &db_time);
	  }
	else
	  {
	    if (has_explicit_time)
	      {
		is_dt = NO_ERROR;
		is_timest = ER_TIMESTAMP_CONVERSION;
		is_d = ER_DATE_CONVERSION;
		is_t = ER_TIME_CONVERSION;
	      }
	    else
	      {
		db_date = db_datetime.date;
		is_dt = ER_TIMESTAMP_CONVERSION;
		is_timest = ER_TIMESTAMP_CONVERSION;
		is_d = NO_ERROR;
		is_t = ER_TIME_CONVERSION;
	      }
	  }

	if (is_dt && is_d && is_t && is_timest)
	  {
	    error_status = ER_OBJ_INVALID_ARGUMENTS;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    goto error;
	  }

	/* add date stuff to a time -> error */
	/* in fact, disable time operations, not available on mysql */
	if (is_t == 0)
	  {
	    error_status = ER_OBJ_INVALID_ARGUMENTS;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    goto error;
	  }

	dt_p = &db_datetime;
	d_p = &db_date;
	ts_p = &db_timestamp;

	/* except just TIME business, convert all to DATETIME */
      }
      break;

    case DB_TYPE_DATE:
      is_d = 1;
      d_p = DB_GET_DATE (date);
      break;

    case DB_TYPE_DATETIME:
      is_dt = 1;
      dt_p = DB_GET_DATETIME (date);
      break;

    case DB_TYPE_TIMESTAMP:
      is_timest = 1;
      ts_p = DB_GET_TIMESTAMP (date);
      break;

    case DB_TYPE_TIME:
      /* should not reach here */
      assert (0);
      break;

    default:
      error_status = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error;
    }

  if (is_d >= 0)
    {
      y = m = d = h = mi = s = ms = 0;
      db_date_decode (d_p, &m, &d, &y);

      if (m == 0 && d == 0 && y == 0)
	{
	  DB_MAKE_NULL (result);
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      return NO_ERROR;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_ATTEMPT_TO_USE_ZERODATE, 0);
	  return ER_ATTEMPT_TO_USE_ZERODATE;
	}

      if (is_add)
	{
	  if (days > 0)
	    {
	      ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, days, 0, 0, 0, 0);
	    }
	  else
	    {
	      ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, -days, 0, 0, 0, 0);
	    }
	}
      else
	{
	  if (days > 0)
	    {
	      ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, days, 0, 0, 0, 0);
	    }
	  else
	    {
	      ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, -days, 0, 0, 0, 0);
	    }
	}

      /* year should always be greater than 1 and less than 9999 */
      if (ret != NO_ERROR)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}

      db_date_encode (&db_date, m, d, y);

      if (res_type == DB_TYPE_STRING || res_type == DB_TYPE_CHAR)
	{
	  db_date_to_string (res_s, 64, &db_date);

	  res_final = db_private_alloc (NULL, strlen (res_s) + 1);
	  if (!res_final)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  strcpy (res_final, res_s);
	  DB_MAKE_STRING (result, res_final);
	  result->need_clear = true;
	}
      else
	{
	  DB_MAKE_DATE (result, m, d, y);
	}
    }
  else if (is_dt >= 0)
    {
      assert (dt_p != NULL);

      y = m = d = h = mi = s = ms = 0;
      db_datetime_decode (dt_p, &m, &d, &y, &h, &mi, &s, &ms);

      if (m == 0 && d == 0 && y == 0 && h == 0 && mi == 0 && s == 0
	  && ms == 0)
	{
	  DB_MAKE_NULL (result);
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      return NO_ERROR;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_ATTEMPT_TO_USE_ZERODATE, 0);
	  return ER_ATTEMPT_TO_USE_ZERODATE;
	}

      if (is_add)
	{
	  if (days > 0)
	    {
	      ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, days, 0, 0, 0, 0);
	    }
	  else
	    {
	      ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, -days, 0, 0, 0, 0);
	    }
	}
      else
	{
	  if (days > 0)
	    {
	      ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, days, 0, 0, 0, 0);
	    }
	  else
	    {
	      ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, -days, 0, 0, 0, 0);
	    }
	}
      /* year should always be greater than 1 and less than 9999 */
      if (ret != NO_ERROR)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}

      db_datetime.date = db_datetime.time = 0;
      db_datetime_encode (&db_datetime, m, d, y, h, mi, s, ms);

      if (res_type == DB_TYPE_STRING || res_type == DB_TYPE_CHAR)
	{
	  db_datetime_to_string (res_s, 64, &db_datetime);

	  res_final = db_private_alloc (NULL, strlen (res_s) + 1);
	  if (!res_final)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  strcpy (res_final, res_s);
	  DB_MAKE_STRING (result, res_final);
	  result->need_clear = true;
	}
      else
	{
	  /* datetime, date + time units, timestamp => return datetime */
	  DB_MAKE_DATETIME (result, &db_datetime);
	}
    }
  else if (is_timest >= 0)
    {
      assert (ts_p != NULL);

      y = m = d = h = mi = s = ms = 0;
      db_timestamp_decode (ts_p, &db_date, &db_time);
      db_date_decode (&db_date, &m, &d, &y);
      db_time_decode (&db_time, &h, &mi, &s);

      if (m == 0 && d == 0 && y == 0 && h == 0 && mi == 0 && s == 0)
	{
	  DB_MAKE_NULL (result);
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      return NO_ERROR;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_ATTEMPT_TO_USE_ZERODATE, 0);
	  return ER_ATTEMPT_TO_USE_ZERODATE;
	}

      if (is_add)
	{
	  if (days > 0)
	    {
	      ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, days, 0, 0, 0, 0);
	    }
	  else
	    {
	      ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, -days, 0, 0, 0, 0);
	    }
	}
      else
	{
	  if (days > 0)
	    {
	      ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, days, 0, 0, 0, 0);
	    }
	  else
	    {
	      ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
						 0, 0, -days, 0, 0, 0, 0);
	    }
	}

      /* year should always be greater than 1 and less than 9999 */
      if (ret != NO_ERROR)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}

      db_datetime.date = db_datetime.time = 0;
      db_datetime_encode (&db_datetime, m, d, y, h, mi, s, ms);

      if (res_type == DB_TYPE_STRING || res_type == DB_TYPE_CHAR)
	{
	  db_datetime_to_string (res_s, 64, &db_datetime);

	  res_final = db_private_alloc (NULL, strlen (res_s) + 1);
	  if (!res_final)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  strcpy (res_final, res_s);
	  DB_MAKE_STRING (result, res_final);
	  result->need_clear = true;
	}
      else
	{
	  /* datetime, date + time units, timestamp => return datetime */
	  DB_MAKE_DATETIME (result, &db_datetime);
	}
    }

error:
  return error_status;
}

int
db_date_add_interval_days (DB_VALUE * result, const DB_VALUE * date,
			   const DB_VALUE * db_days)
{
  return db_date_add_sub_interval_days (result, date, db_days, true);
}

int
db_date_sub_interval_days (DB_VALUE * result, const DB_VALUE * date,
			   const DB_VALUE * db_days)
{
  return db_date_add_sub_interval_days (result, date, db_days, false);
}

/*
 * db_str_to_millisec () -
 *
 * Arguments:
 *         str: millisecond format
 *
 * Returns: int
 *
 * Errors:
 */
static int
db_str_to_millisec (const char *str)
{
  int digit_num, value, ret;

  if (str == NULL || str[0] == '\0')
    {
      return 0;
    }

  digit_num = strlen (str);
  if (digit_num >= 1 && str[0] == '-')
    {
      digit_num--;
      ret = sscanf (str, "%4d", &value);
    }
  else
    {
      ret = sscanf (str, "%3d", &value);
    }

  if (ret != 1)
    {
      return 0;
    }

  switch (digit_num)
    {
    case 1:
      value *= 100;
      break;

    case 2:
      value *= 10;
      break;

    default:
      break;
    }

  return value;
}

/*
 * copy_and_shift_values () -
 *
 * Arguments:
 *         shift: the offset the values are shifted
 *         n: normal number of arguments
 *	   first...: arguments
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *    shifts all arguments by the given value
 */
static void
copy_and_shift_values (int shift, int n, DB_BIGINT * first, ...)
{
  va_list marker;
  DB_BIGINT *curr = first;
  DB_BIGINT *v[16];		/* will contain max 5 elements */
  int i, count = 0, cnt_src = 0;

  /*
   * numeric arguments from interval expression have a delimiter read also
   * as argument so out of N arguments there are actually (N + 1)/2 numeric
   * values (ex: 1:2:3:4 or 1:2 or 1:2:3)
   */
  shift = (shift + 1) / 2;

  if (shift == n)
    {
      return;
    }

  va_start (marker, first);	/* init variable arguments */
  while (cnt_src < n)
    {
      cnt_src++;
      v[count++] = curr;
      curr = va_arg (marker, DB_BIGINT *);
    }
  va_end (marker);

  cnt_src = shift - 1;
  /* move backwards to not overwrite values */
  for (i = count - 1; i >= 0; i--)
    {
      if (cnt_src >= 0)
	{
	  /* replace */
	  *v[i] = *v[cnt_src--];
	}
      else
	{
	  /* reset */
	  *v[i] = 0;
	}
    }
}

/*
 * get_single_unit_value () -
 *   return:
 *   expr (in): input as string
 *   int_val (in) : input as integer
 */
static DB_BIGINT
get_single_unit_value (char *expr, DB_BIGINT int_val)
{
  DB_BIGINT v = 0;

  if (expr == NULL)
    {
      v = int_val;
    }
  else
    {
      sscanf (expr, "%lld", (long long *) &v);
    }

  return v;
}

/*
 * db_date_add_sub_interval_expr () -
 *
 * Arguments:
 *         date: starting date
 *         expr: string with the amounts to add
 *	   unit: unit(s) of the amounts
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *    Returns date + the amounts from expr
 */
static int
db_date_add_sub_interval_expr (DB_VALUE * result, const DB_VALUE * date,
			       const DB_VALUE * expr, const int unit,
			       bool is_add)
{
  int sign = 0;
  int type = 0;			/* 1 -> time, 2 -> date, 3 -> both */
  DB_TYPE res_type, expr_type;
  char *date_s = NULL, *expr_s, res_s[64], millisec_s[64];
  int error_status = NO_ERROR;
  DB_BIGINT millisec, seconds, minutes, hours;
  DB_BIGINT days, weeks, months, quarters, years;
  DB_DATETIME db_datetime, *dt_p = NULL;
  DB_TIME db_time;
  DB_DATE db_date, *d_p;
  DB_TIMESTAMP db_timestamp, *ts_p = NULL;
  int narg, is_dt = -1, is_d = -1, is_t = -1, is_timest = -1;
  char delim;
  DB_VALUE trimed_expr, charset;
  DB_BIGINT unit_int_val;
  double dbl;
  int y, m, d, h, mi, s, ms;
  int ret;
  char *res_final;

  res_type = DB_VALUE_DOMAIN_TYPE (date);
  if (res_type == DB_TYPE_NULL || DB_IS_NULL (date))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  expr_type = DB_VALUE_DOMAIN_TYPE (expr);
  if (expr_type == DB_TYPE_NULL || DB_IS_NULL (expr))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  DB_MAKE_NULL (&trimed_expr);
  unit_int_val = 0;
  expr_s = NULL;

  /* 1. Prepare the input: convert expr to char */

  /*
   * expr is converted to char because it may contain a more complicated form
   * for the multiple unit formats, for example:
   * 'DAYS HOURS:MINUTES:SECONDS.MILLISECONDS'
   * For the simple unit tags, expr is integer
   */

  expr_type = DB_VALUE_DOMAIN_TYPE (expr);
  switch (expr_type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      DB_MAKE_NULL (&charset);
      error_status = db_string_trim (BOTH, &charset, expr, &trimed_expr);
      if (error_status != NO_ERROR)
	{
	  goto error;
	}

      /* db_string_trim builds a NULL terminated string, expr_s is NULL
       * terminated */
      expr_s = DB_GET_STRING (&trimed_expr);
      if (expr_s == NULL)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}
      break;

    case DB_TYPE_SHORT:
      unit_int_val = DB_GET_SHORT (expr);
      break;

    case DB_TYPE_INTEGER:
      unit_int_val = DB_GET_INTEGER (expr);
      break;

    case DB_TYPE_BIGINT:
      unit_int_val = DB_GET_BIGINT (expr);
      break;

    case DB_TYPE_FLOAT:
      unit_int_val = (DB_BIGINT) round (DB_GET_FLOAT (expr));
      break;

    case DB_TYPE_DOUBLE:
      unit_int_val = (DB_BIGINT) round (DB_GET_DOUBLE (expr));
      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double ((DB_C_NUMERIC) db_locate_numeric (expr),
				    DB_VALUE_SCALE (expr), &dbl);
      unit_int_val = (DB_BIGINT) round (dbl);
      break;

    default:
      error_status = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error;
    }

  /* 2. the big switch: according to unit, we parse expr and get amounts of
     ms/s/m/h/d/m/y/w/q to add or subtract */

  millisec_s[0] = '\0';
  millisec = seconds = minutes = hours = 0;
  days = weeks = months = quarters = years = 0;

  switch (unit)
    {
    case PT_MILLISECOND:
      millisec = get_single_unit_value (expr_s, unit_int_val);
      sign = (millisec >= 0);
      type |= 1;
      break;

    case PT_SECOND:
      seconds = get_single_unit_value (expr_s, unit_int_val);
      sign = (seconds >= 0);
      type |= 1;
      break;

    case PT_MINUTE:
      minutes = get_single_unit_value (expr_s, unit_int_val);
      sign = (minutes >= 0);
      type |= 1;
      break;

    case PT_HOUR:
      hours = get_single_unit_value (expr_s, unit_int_val);
      sign = (hours >= 0);
      type |= 1;
      break;

    case PT_DAY:
      days = get_single_unit_value (expr_s, unit_int_val);
      sign = (days >= 0);
      type |= 2;
      break;

    case PT_WEEK:
      weeks = get_single_unit_value (expr_s, unit_int_val);
      sign = (weeks >= 0);
      type |= 2;
      break;

    case PT_MONTH:
      months = get_single_unit_value (expr_s, unit_int_val);
      sign = (months >= 0);
      type |= 2;
      break;

    case PT_QUARTER:
      quarters = get_single_unit_value (expr_s, unit_int_val);
      sign = (quarters >= 0);
      type |= 2;
      break;

    case PT_YEAR:
      years = get_single_unit_value (expr_s, unit_int_val);
      sign = (years >= 0);
      type |= 2;
      break;

    case PT_SECOND_MILLISECOND:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%s", (long long *) &seconds, &delim,
			 millisec_s);
	  millisec = db_str_to_millisec (millisec_s);
	  copy_and_shift_values (narg, 2, &seconds, &millisec);
	}
      else
	{
	  millisec = unit_int_val;
	}
      sign = (seconds >= 0);
      type |= 1;
      break;

    case PT_MINUTE_MILLISECOND:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld%c%s", (long long *) &minutes,
			 &delim, (long long *) &seconds, &delim, millisec_s);
	  millisec = db_str_to_millisec (millisec_s);
	  copy_and_shift_values (narg, 3, &minutes, &seconds, &millisec);
	}
      else
	{
	  millisec = unit_int_val;
	}
      sign = (minutes >= 0);
      type |= 1;
      break;

    case PT_MINUTE_SECOND:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld", (long long *) &minutes,
			 &delim, (long long *) &seconds);
	  copy_and_shift_values (narg, 2, &minutes, &seconds);
	}
      else
	{
	  seconds = unit_int_val;
	}
      sign = (minutes >= 0);
      type |= 1;
      break;

    case PT_HOUR_MILLISECOND:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld%c%lld%c%s", (long long *) &hours,
			 &delim, (long long *) &minutes, &delim,
			 (long long *) &seconds, &delim, millisec_s);
	  millisec = db_str_to_millisec (millisec_s);
	  copy_and_shift_values (narg, 4, &hours, &minutes, &seconds,
				 &millisec);
	}
      else
	{
	  millisec = unit_int_val;
	}
      sign = (hours >= 0);
      type |= 1;
      break;

    case PT_HOUR_SECOND:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld%c%lld", (long long *) &hours,
			 &delim, (long long *) &minutes, &delim,
			 (long long *) &seconds);
	  copy_and_shift_values (narg, 3, &hours, &minutes, &seconds);
	}
      else
	{
	  seconds = unit_int_val;
	}
      sign = (hours >= 0);
      type |= 1;
      break;

    case PT_HOUR_MINUTE:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld", (long long *) &hours, &delim,
			 (long long *) &minutes);
	  copy_and_shift_values (narg, 2, &hours, &minutes);
	}
      else
	{
	  minutes = unit_int_val;
	}
      sign = (hours >= 0);
      type |= 1;
      break;

    case PT_DAY_MILLISECOND:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld%c%lld%c%lld%c%s",
			 (long long *) &days, &delim, (long long *) &hours,
			 &delim, (long long *) &minutes, &delim,
			 (long long *) &seconds, &delim, millisec_s);
	  millisec = db_str_to_millisec (millisec_s);
	  copy_and_shift_values (narg, 5, &days, &hours, &minutes, &seconds,
				 &millisec);
	}
      else
	{
	  millisec = unit_int_val;
	}
      sign = (days >= 0);
      type |= 1;
      type |= 2;
      break;

    case PT_DAY_SECOND:
      if (expr_s)
	{
	  narg =
	    sscanf (expr_s, "%lld%c%lld%c%lld%c%lld", (long long *) &days,
		    &delim, (long long *) &hours, &delim,
		    (long long *) &minutes, &delim, (long long *) &seconds);
	  copy_and_shift_values (narg, 4, &days, &hours, &minutes, &seconds);
	}
      else
	{
	  seconds = unit_int_val;
	}
      sign = (days >= 0);
      type |= 1;
      type |= 2;
      break;

    case PT_DAY_MINUTE:
      if (expr_s)
	{
	  narg =
	    sscanf (expr_s, "%lld%c%lld%c%lld", (long long *) &days, &delim,
		    (long long *) &hours, &delim, (long long *) &minutes);
	  copy_and_shift_values (narg, 3, &days, &hours, &minutes);
	}
      else
	{
	  minutes = unit_int_val;
	}
      sign = (days >= 0);
      type |= 1;
      type |= 2;
      break;

    case PT_DAY_HOUR:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld", (long long *) &days, &delim,
			 (long long *) &hours);
	  copy_and_shift_values (narg, 2, &days, &hours);
	}
      else
	{
	  hours = unit_int_val;
	}
      sign = (days >= 0);
      type |= 1;
      type |= 2;
      break;

    case PT_YEAR_MONTH:
      if (expr_s)
	{
	  narg = sscanf (expr_s, "%lld%c%lld", (long long *) &years, &delim,
			 (long long *) &months);
	  copy_and_shift_values (narg, 2, &years, &months);
	}
      else
	{
	  months = unit_int_val;
	}
      sign = (years >= 0);
      type |= 2;
      break;

    default:
      error_status = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error;
    }

  /* we have the sign of the amounts, turn them in absolute value */
  years = ABS (years);
  months = ABS (months);
  days = ABS (days);
  weeks = ABS (weeks);
  quarters = ABS (quarters);
  hours = ABS (hours);
  minutes = ABS (minutes);
  seconds = ABS (seconds);
  millisec = ABS (millisec);

  /* convert weeks and quarters to our units */
  if (weeks != 0)
    {
      days += weeks * 7;
      weeks = 0;
    }

  if (quarters != 0)
    {
      months += 3 * quarters;
      quarters = 0;
    }

  /* 3. Convert string with date to DateTime or Time */

  switch (res_type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      {
	bool has_explicit_time = false;
	int str_len = DB_GET_STRING_SIZE (date);
	date_s = DB_GET_STRING (date);

	/* try to figure out the string format */
	if (db_date_parse_datetime_parts (date_s, str_len, &db_datetime,
					  &has_explicit_time, NULL, NULL,
					  NULL))
	  {
	    is_dt = ER_TIMESTAMP_CONVERSION;
	    is_timest = ER_TIMESTAMP_CONVERSION;
	    is_d = ER_DATE_CONVERSION;
	    is_t = db_string_to_time_ex (date_s, str_len, &db_time);
	  }
	else
	  {
	    if (has_explicit_time)
	      {
		is_dt = NO_ERROR;
		is_timest = ER_TIMESTAMP_CONVERSION;
		is_d = ER_DATE_CONVERSION;
		is_t = ER_TIME_CONVERSION;
	      }
	    else
	      {
		db_date = db_datetime.date;
		is_dt = ER_TIMESTAMP_CONVERSION;
		is_timest = ER_TIMESTAMP_CONVERSION;
		is_d = NO_ERROR;
		is_t = ER_TIME_CONVERSION;
	      }
	  }

	if (is_dt && is_d && is_t && is_timest)
	  {
	    error_status = ER_DATE_CONVERSION;
	    goto error;
	  }

	/* add date stuff to a time -> error */
	/* in fact, disable time operations, not available on mysql */
	if (is_t == 0)
	  {
	    error_status = ER_OBJ_INVALID_ARGUMENTS;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    goto error;
	  }

	dt_p = &db_datetime;
	d_p = &db_date;
	ts_p = &db_timestamp;

	/* except just TIME business, convert all to DATETIME */
      }
      break;

    case DB_TYPE_DATE:
      is_d = 1;
      d_p = DB_GET_DATE (date);
      break;

    case DB_TYPE_DATETIME:
      is_dt = 1;
      dt_p = DB_GET_DATETIME (date);
      break;

    case DB_TYPE_TIMESTAMP:
      is_timest = 1;
      ts_p = DB_GET_TIMESTAMP (date);
      break;

    case DB_TYPE_TIME:
      /* should not reach here */
      assert (0);
      break;

    default:
      error_status = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error;
    }

  /* treat as date only if adding date units, else treat as datetime */
  if (is_d >= 0)
    {
      y = m = d = h = mi = s = ms = 0;
      db_date_decode (d_p, &m, &d, &y);

      if (m == 0 && d == 0 && y == 0)
	{
	  DB_MAKE_NULL (result);
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      return NO_ERROR;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	  return ER_DATE_CONVERSION;
	}

      if (sign ^ is_add)
	{
	  ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
					     years, months, days, hours,
					     minutes, seconds, millisec);
	}
      else
	{
	  ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
					     years, months, days, hours,
					     minutes, seconds, millisec);
	}

      /* year should always be greater than 1 and less than 9999 */
      if (ret != NO_ERROR)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}

      if (type == 2)
	{
	  db_date_encode (&db_date, m, d, y);

	  if (m == 0 && d == 0 && y == 0)
	    {
	      DB_MAKE_NULL (result);
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
		{
		  return NO_ERROR;
		}
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION,
		      0);
	      return ER_DATE_CONVERSION;
	    }

	  if (res_type == DB_TYPE_STRING || res_type == DB_TYPE_CHAR)
	    {
	      db_date_to_string (res_s, 64, &db_date);

	      res_final = db_private_alloc (NULL, strlen (res_s) + 1);
	      if (res_final == NULL)
		{
		  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto error;
		}
	      strcpy (res_final, res_s);
	      DB_MAKE_STRING (result, res_final);
	      result->need_clear = true;
	    }
	  else
	    {
	      DB_MAKE_DATE (result, m, d, y);
	    }
	}
      else if (type & 1)
	{
	  db_datetime.date = db_datetime.time = 0;
	  db_datetime_encode (&db_datetime, m, d, y, h, mi, s, ms);

	  if (m == 0 && d == 0 && y == 0
	      && h == 0 && mi == 0 && s == 0 && ms == 0)
	    {
	      DB_MAKE_NULL (result);
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
		{
		  return NO_ERROR;
		}
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION,
		      0);
	      return ER_DATE_CONVERSION;
	    }

	  if (res_type == DB_TYPE_STRING || res_type == DB_TYPE_CHAR)
	    {
	      db_datetime_to_string (res_s, 64, &db_datetime);

	      res_final = db_private_alloc (NULL, strlen (res_s) + 1);
	      if (res_final == NULL)
		{
		  error_status = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto error;
		}
	      strcpy (res_final, res_s);
	      DB_MAKE_STRING (result, res_final);
	      result->need_clear = true;
	    }
	  else
	    {
	      DB_MAKE_DATETIME (result, &db_datetime);
	    }
	}
    }
  else if (is_dt >= 0)
    {
      assert (dt_p != NULL);

      y = m = d = h = mi = s = ms = 0;
      db_datetime_decode (dt_p, &m, &d, &y, &h, &mi, &s, &ms);

      if (m == 0 && d == 0 && y == 0 && h == 0 && mi == 0 && s == 0
	  && ms == 0)
	{
	  DB_MAKE_NULL (result);
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      return NO_ERROR;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
	  return ER_DATE_CONVERSION;
	}

      if (sign ^ is_add)
	{
	  ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
					     years, months, days, hours,
					     minutes, seconds, millisec);
	}
      else
	{
	  ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
					     years, months, days, hours,
					     minutes, seconds, millisec);
	}

      /* year should always be greater than 1 and less than 9999 */
      if (ret != NO_ERROR)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}

      db_datetime.date = db_datetime.time = 0;
      db_datetime_encode (&db_datetime, m, d, y, h, mi, s, ms);

      if (res_type == DB_TYPE_STRING || res_type == DB_TYPE_CHAR)
	{
	  db_datetime_to_string (res_s, 64, &db_datetime);

	  res_final = db_private_alloc (NULL, strlen (res_s) + 1);
	  if (res_final == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  strcpy (res_final, res_s);
	  DB_MAKE_STRING (result, res_final);
	  result->need_clear = true;
	}
      else
	{
	  /* datetime, date + time units, timestamp => return datetime */
	  DB_MAKE_DATETIME (result, &db_datetime);
	}
    }
  else if (is_timest >= 0)
    {
      assert (ts_p != NULL);

      y = m = d = h = mi = s = ms = 0;
      db_timestamp_decode (ts_p, &db_date, &db_time);
      db_date_decode (&db_date, &m, &d, &y);
      db_time_decode (&db_time, &h, &mi, &s);

      if (m == 0 && d == 0 && y == 0 && h == 0 && mi == 0 && s == 0)
	{
	  DB_MAKE_NULL (result);
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      return NO_ERROR;
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIMESTAMP_CONVERSION,
		  0);
	  return ER_TIMESTAMP_CONVERSION;
	}

      if (sign ^ is_add)
	{
	  ret = sub_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
					     years, months, days, hours,
					     minutes, seconds, millisec);
	}
      else
	{
	  ret = add_and_normalize_date_time (&y, &m, &d, &h, &mi, &s, &ms,
					     years, months, days, hours,
					     minutes, seconds, millisec);
	}

      /* year should always be greater than 1 and less than 9999 */
      if (ret != NO_ERROR)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}

      db_datetime.date = db_datetime.time = 0;
      db_datetime_encode (&db_datetime, m, d, y, h, mi, s, ms);

      if (res_type == DB_TYPE_STRING || res_type == DB_TYPE_CHAR)
	{
	  db_datetime_to_string (res_s, 64, &db_datetime);

	  res_final = db_private_alloc (NULL, strlen (res_s) + 1);
	  if (res_final == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  strcpy (res_final, res_s);
	  DB_MAKE_STRING (result, res_final);
	  result->need_clear = true;
	}
      else
	{
	  /* datetime, date + time units, timestamp => return datetime */
	  DB_MAKE_DATETIME (result, &db_datetime);
	}
    }

error:
  pr_clear_value (&trimed_expr);
  return error_status;
}

/*
 * db_date_add_interval_expr ()
 *
 * Arguments:
 *         result(out):
 *         date(in): source date
 *         expr(in): to be added interval
 *         unit(in): unit of interval expr
 *
 * Returns: int
 *
 * Note:
 */
int
db_date_add_interval_expr (DB_VALUE * result, const DB_VALUE * date,
			   const DB_VALUE * expr, const int unit)
{
  return db_date_add_sub_interval_expr (result, date, expr, unit, true);
}

/*
 * db_date_sub_interval_expr ()
 *
 * Arguments:
 *         result(out):
 *         date(in): source date
 *         expr(in): to be substracted interval
 *         unit(in): unit of interval expr
 *
 * Returns: int
 *
 * Note:
 */
int
db_date_sub_interval_expr (DB_VALUE * result, const DB_VALUE * date,
			   const DB_VALUE * expr, const int unit)
{
  return db_date_add_sub_interval_expr (result, date, expr, unit, false);
}

/*
 * db_date_format ()
 *
 * Arguments:
 *         date_value: source date
 *         format: string with format specifiers
 *	   result: output string
 *	   domain: domain of result
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *    formats the date according to a specified format
 */
int
db_date_format (const DB_VALUE * date_value, const DB_VALUE * format,
		const DB_VALUE * date_lang, DB_VALUE * result,
		const TP_DOMAIN * domain)
{
  DB_DATETIME *dt_p;
  DB_DATE db_date, *d_p;
  DB_TIME db_time;
  DB_TIMESTAMP *ts_p;
  DB_TYPE res_type, format_type;
  char *res, *res2, *format_s;
  int format_s_len;
  char *strend;
  int error_status = NO_ERROR, len;
  int y, m, d, h, mi, s, ms;
  int days[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  char format_specifiers[256][64];
  int i, j;
  int dow, dow2;
  INTL_LANG date_lang_id;
  int tu, tv, tx, weeks, ld_fw, days_counter;
  char och = -1, ch;
  const LANG_LOCALE_DATA *lld;
  bool dummy;
  INTL_CODESET codeset;
  int res_collation;

  assert (date_lang != NULL);

  y = m = d = h = mi = s = ms = 0;
  memset (format_specifiers, 0, sizeof (format_specifiers));

  if (date_value == NULL || format == NULL
      || DB_IS_NULL (date_value) || DB_IS_NULL (format))
    {
      DB_MAKE_NULL (result);
      goto error;
    }

  if (!is_char_string (format))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang), &dummy,
					     &dummy);
  if (domain != NULL && domain->collation_flag != TP_DOMAIN_COLL_LEAVE)
    {
      codeset = TP_DOMAIN_CODESET (domain);
      res_collation = TP_DOMAIN_COLLATION (domain);
    }
  else
    {
      codeset = DB_GET_STRING_CODESET (format);
      res_collation = DB_GET_STRING_COLLATION (format);
    }

  lld = lang_get_specific_locale (date_lang_id, codeset);
  if (lld == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (date_lang_id),
	      lang_charset_name (codeset));
      return error_status;
    }

  res_type = DB_VALUE_DOMAIN_TYPE (date_value);

  /* 1. Get date values */
  switch (res_type)
    {
    case DB_TYPE_DATETIME:
      dt_p = DB_GET_DATETIME (date_value);
      db_datetime_decode (dt_p, &m, &d, &y, &h, &mi, &s, &ms);
      break;

    case DB_TYPE_DATE:
      d_p = DB_GET_DATE (date_value);
      db_date_decode (d_p, &m, &d, &y);
      break;

    case DB_TYPE_TIMESTAMP:
      ts_p = DB_GET_TIMESTAMP (date_value);
      db_timestamp_decode (ts_p, &db_date, &db_time);
      db_time_decode (&db_time, &h, &mi, &s);
      db_date_decode (&db_date, &m, &d, &y);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
      {
	DB_VALUE dt;
	TP_DOMAIN *tp_datetime = db_type_to_db_domain (DB_TYPE_DATETIME);

	if (tp_value_cast (date_value, &dt, tp_datetime, false)
	    != DOMAIN_COMPATIBLE)
	  {
	    error_status = ER_QSTR_INVALID_DATA_TYPE;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    goto error;
	  }
	db_datetime_decode (DB_GET_DATETIME (&dt), &m, &d, &y, &h, &mi, &s,
			    &ms);
	break;
      }

    default:
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error;
    }

  /* 2. Compute the value for each format specifier */
  days[2] += LEAP (y);
  dow = db_get_day_of_week (y, m, d);

  /* %a       Abbreviated weekday name (Sun..Sat) */
  strcpy (format_specifiers['a'], lld->day_short_name[dow]);

  /* %b       Abbreviated m name (Jan..Dec) */
  if (m > 0)
    {
      strcpy (format_specifiers['b'], lld->month_short_name[m - 1]);
    }

  /* %c       Month, numeric (0..12) - actually (1..12) */
  sprintf (format_specifiers['c'], "%d", m);

  /* %D       Day of the m with English suffix (0th, 1st, 2nd, 3rd,...) */
  sprintf (format_specifiers['D'], "%d", d);
  /* 11-19 are special */
  if (date_lang_id == INTL_LANG_ENGLISH)
    {
      if (d % 10 == 1 && d / 10 != 1)
	{
	  strcat (format_specifiers['D'], "st");
	}
      else if (d % 10 == 2 && d / 10 != 1)
	{
	  strcat (format_specifiers['D'], "nd");
	}
      else if (d % 10 == 3 && d / 10 != 1)
	{
	  strcat (format_specifiers['D'], "rd");
	}
      else
	{
	  strcat (format_specifiers['D'], "th");
	}
    }

  /* %d       Day of the m, numeric (00..31) */
  sprintf (format_specifiers['d'], "%02d", d);

  /* %e       Day of the m, numeric (0..31) - actually (1..31) */
  sprintf (format_specifiers['e'], "%d", d);

  /* %f       Milliseconds (000..999) */
  sprintf (format_specifiers['f'], "%03d", ms);

  /* %H       Hour (00..23) */
  sprintf (format_specifiers['H'], "%02d", h);

  /* %h       Hour (01..12) */
  sprintf (format_specifiers['h'], "%02d", (h % 12 == 0) ? 12 : (h % 12));

  /* %I       Hour (01..12) */
  sprintf (format_specifiers['I'], "%02d", (h % 12 == 0) ? 12 : (h % 12));

  /* %i       Minutes, numeric (00..59) */
  sprintf (format_specifiers['i'], "%02d", mi);

  /* %j       Day of y (001..366) */
  for (j = d, i = 1; i < m; i++)
    {
      j += days[i];
    }
  sprintf (format_specifiers['j'], "%03d", j);

  /* %k       Hour (0..23) */
  sprintf (format_specifiers['k'], "%d", h);

  /* %l       Hour (1..12) */
  sprintf (format_specifiers['l'], "%d", (h % 12 == 0) ? 12 : (h % 12));

  /* %M       Month name (January..December) */
  if (m > 0)
    {
      strcpy (format_specifiers['M'], lld->month_name[m - 1]);
    }

  /* %m       Month, numeric (00..12) */
  sprintf (format_specifiers['m'], "%02d", m);

  /* %p       AM or PM */
  strcpy (format_specifiers['p'],
	  (h > 11) ? lld->am_pm[PM_NAME] : lld->am_pm[AM_NAME]);

  /* %r       Time, 12-hour (hh:mm:ss followed by AM or PM) */
  sprintf (format_specifiers['r'], "%02d:%02d:%02d %s",
	   (h % 12 == 0) ? 12 : (h % 12), mi, s,
	   (h > 11) ? lld->am_pm[PM_NAME] : lld->am_pm[AM_NAME]);

  /* %S       Seconds (00..59) */
  sprintf (format_specifiers['S'], "%02d", s);

  /* %s       Seconds (00..59) */
  sprintf (format_specifiers['s'], "%02d", s);

  /* %T       Time, 24-hour (hh:mm:ss) */
  sprintf (format_specifiers['T'], "%02d:%02d:%02d", h, mi, s);

  /* %U       Week (00..53), where Sunday is the first d of the week */
  /* %V       Week (01..53), where Sunday is the first d of the week;
     used with %X  */
  /* %X       Year for the week where Sunday is the first day of the week,
     numeric, four digits; used with %V */

  dow2 = db_get_day_of_week (y, 1, 1);

  ld_fw = 7 - dow2;

  for (days_counter = d, i = 1; i < m; i++)
    {
      days_counter += days[i];
    }

  if (days_counter <= ld_fw)
    {
      weeks = dow2 == 0 ? 1 : 0;
    }
  else
    {
      days_counter -= (dow2 == 0) ? 0 : ld_fw;
      weeks = days_counter / 7 + (days_counter % 7 ? 1 : 0);
    }

  tu = tv = weeks;
  tx = y;
  if (tv == 0)
    {
      dow2 = db_get_day_of_week (y - 1, 1, 1);
      days_counter = 365 + LEAP (y - 1) - (dow2 == 0 ? 0 : 7 - dow2);
      tv = days_counter / 7 + (days_counter % 7 ? 1 : 0);
      tx = y - 1;
    }

  sprintf (format_specifiers['U'], "%02d", tu);
  sprintf (format_specifiers['V'], "%02d", tv);
  sprintf (format_specifiers['X'], "%04d", tx);

  /* %u       Week (00..53), where Monday is the first d of the week */
  /* %v       Week (01..53), where Monday is the first d of the week;
     used with %x  */
  /* %x       Year for the week, where Monday is the first day of the week,
     numeric, four digits; used with %v */

  dow2 = db_get_day_of_week (y, 1, 1);
  weeks = dow2 >= 1 && dow2 <= 4 ? 1 : 0;

  ld_fw = dow2 == 0 ? 1 : 7 - dow2 + 1;

  for (days_counter = d, i = 1; i < m; i++)
    {
      days_counter += days[i];
    }

  if (days_counter > ld_fw)
    {
      days_counter -= ld_fw;
      weeks += days_counter / 7 + (days_counter % 7 ? 1 : 0);
    }

  tu = weeks;
  tv = weeks;
  tx = y;
  if (tv == 0)
    {
      dow2 = db_get_day_of_week (y - 1, 1, 1);
      weeks = dow2 >= 1 && dow2 <= 4 ? 1 : 0;
      ld_fw = dow2 == 0 ? 1 : 7 - dow2 + 1;
      days_counter = 365 + LEAP (y - 1) - ld_fw;
      tv = weeks + days_counter / 7 + (days_counter % 7 ? 1 : 0);
      tx = y - 1;
    }
  else if (tv == 53)
    {
      dow2 = db_get_day_of_week (y + 1, 1, 1);
      if (dow2 >= 1 && dow2 <= 4)
	{
	  tv = 1;
	  tx = y + 1;
	}
    }

  sprintf (format_specifiers['u'], "%02d", tu);
  sprintf (format_specifiers['v'], "%02d", tv);
  sprintf (format_specifiers['x'], "%04d", tx);

  /* %W       Weekday name (Sunday..Saturday) */
  strcpy (format_specifiers['W'], lld->day_name[dow]);

  /* %w       Day of the week (0=Sunday..6=Saturday) */
  sprintf (format_specifiers['w'], "%d", dow);

  /* %Y       Year, numeric, four digits */
  sprintf (format_specifiers['Y'], "%04d", y);

  /* %y       Year, numeric (two digits) */
  sprintf (format_specifiers['y'], "%02d", y % 100);

  /* 3. Generate the output according to the format and the values */
  format_type = DB_VALUE_DOMAIN_TYPE (format);
  switch (format_type)
    {
    case DB_TYPE_STRING:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      format_s = DB_PULL_STRING (format);
      format_s_len = DB_GET_STRING_SIZE (format);
      break;

    default:
      /* we should not get a nonstring format */
      assert (false);
      return ER_FAILED;
    }

  len = 1024;
  res = (char *) db_private_alloc (NULL, len);
  if (res == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  memset (res, 0, len);

  ch = *format_s;
  strend = format_s + format_s_len;
  while (format_s < strend)
    {
      format_s++;
      och = ch;
      ch = *format_s;

      if (och == '%' /* && (res[strlen(res) - 1] != '%') */ )
	{
	  if (ch == '%')
	    {
	      STRCHCAT (res, '%');

	      /* jump a character */
	      format_s++;
	      och = ch;
	      ch = *format_s;

	      continue;
	    }
	  /* parse the character */
	  if (strlen (format_specifiers[(unsigned char) ch]) == 0)
	    {
	      /* append the character itself */
	      STRCHCAT (res, ch);
	    }
	  else
	    {
	      strcat (res, format_specifiers[(unsigned char) ch]);
	    }

	  /* jump a character */
	  format_s++;
	  och = ch;
	  ch = *format_s;
	}
      else
	{
	  STRCHCAT (res, och);
	}

      /* chance of overflow ? */
      /* assume we can't add at a time mode than 16 chars */
      if (strlen (res) + 16 > len)
	{
	  /* realloc - copy temporary in res2 */
	  res2 = (char *) db_private_alloc (NULL, len);
	  if (res2 == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  memset (res2, 0, len);
	  strcpy (res2, res);
	  db_private_free_and_init (NULL, res);

	  len += 1024;
	  res = (char *) db_private_alloc (NULL, len);
	  if (res == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }
	  memset (res, 0, len);
	  strcpy (res, res2);
	  db_private_free_and_init (NULL, res2);
	}
    }
  /* finished string */

  /* 4. */

  DB_MAKE_STRING (result, res);

  db_string_put_cs_and_collation (result, codeset, res_collation);

  result->need_clear = true;

error:
  /* do not free res as it was moved to result and will be freed later */

  return error_status;
}

/*
 * parse_digits ()
 *
 * Arguments:
 *         s: source string to parse
 *         nr: output number
 *	   cnt: length at which we trim the number (-1 if none)
 *
 * Returns: int - actual number of characters read
 *
 * Errors:
 *
 * Note:
 *    reads cnt digits until non-digit char reached
 */
int
parse_digits (char *s, int *nr, int cnt)
{
  int count = 0, len;
  char *ch;
  /* res[64] is safe because res has a max length of cnt, which is max 4 */
  char res[64];
  const int res_count = sizeof (res) / sizeof (char);

  ch = s;
  *nr = 0;

  memset (res, 0, sizeof (res));

  while (WHITESPACE (*ch))
    {
      ch++;
      count++;
    }

  /* do not support negative numbers because... they are not supported :) */
  while (*ch != 0 && (*ch >= '0' && *ch <= '9'))
    {
      STRCHCAT (res, *ch);

      ch++;
      count++;

      /* trim at cnt characters */
      len = strlen (res);
      if (len == cnt || len == res_count - 1)
	{
	  break;
	}
    }

  *nr = atol (res);

  return count;
}

/*
 * db_str_to_date ()
 *
 * Arguments:
 *         str: string from which we get the data
 *         format: format specifiers to match the str
 *         date_lang: id of language to use
 *	   domain: expected domain of result, may be NULL; If NULL the domain
 *		   output domain is determined according to format
 *
 * Returns: int
 *
 * Errors:
 *
 * Note:
 *    inverse function for date_format - compose a date/time from some format
 *    specifiers and some informations.
 */
int
db_str_to_date (const DB_VALUE * str, const DB_VALUE * format,
		const DB_VALUE * date_lang, DB_VALUE * result,
		TP_DOMAIN * domain)
{
  char *sstr = NULL, *format_s = NULL, *format2_s = NULL;
  int i, j, k, error_status = NO_ERROR;
  int type, len1, len2, h24 = 0, _v, _x;
  DB_TYPE res_type;
  int days[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int y, m, d, h, mi, s, ms, am /* 0 = AM, 1 = PM */ ;
  int u, U, v, V, dow, doy, w;
  char stack_buf_str[64];
  char *initial_buf_str = NULL;
  bool do_free_buf_str = false;
  INTL_CODESET codeset;
  INTL_LANG date_lang_id;
  bool dummy;

  if (str == NULL || format == NULL || date_lang == NULL)
    {
      error_status = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error;
    }

  DB_MAKE_NULL (result);

  if (DB_IS_NULL (str) || DB_IS_NULL (format))
    {
      return NO_ERROR;
    }

  codeset = DB_GET_STRING_CODESET (str);
  assert (DB_VALUE_TYPE (date_lang) == DB_TYPE_INTEGER);
  date_lang_id = lang_get_lang_id_from_flag (DB_GET_INT (date_lang), &dummy,
					     &dummy);
  if (lang_get_specific_locale (date_lang_id, codeset) == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (date_lang_id),
	      lang_charset_name (codeset));
      goto error;
    }

  y = m = d = V = v = U = u = -1;
  h = mi = s = ms = 0;
  dow = doy = am = -1;
  _v = _x = 0;

  error_status =
    db_check_or_create_null_term_string (str, stack_buf_str,
					 sizeof (stack_buf_str),
					 true, true,
					 &initial_buf_str, &do_free_buf_str);

  if (error_status != NO_ERROR)
    {
      goto error;
    }

  sstr = initial_buf_str;

  format2_s = DB_PULL_STRING (format);
  len2 = DB_GET_STRING_SIZE (format);
  len2 = (len2 < 0) ? strlen (format2_s) : len2;

  format_s = (char *) db_private_alloc (NULL, len2 + 1);
  if (!format_s)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  memset (format_s, 0, sizeof (char) * (len2 + 1));

  /* delete all whitespace from format */
  for (i = 0; i < len2; i++)
    {
      if (!WHITESPACE (format2_s[i]))
	{
	  STRCHCAT (format_s, format2_s[i]);
	}
      /* '%' without format specifier */
      else if (WHITESPACE (format2_s[i]) && i > 0 && format2_s[i - 1] == '%')
	{
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) ==
	      false)
	    {
	      error_status = ER_OBJ_INVALID_ARGUMENTS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	    }
	  goto error;
	}
    }

  if (domain == NULL)
    {
      type = db_check_time_date_format (format_s);
      if (type == 1)
	{
	  res_type = DB_TYPE_TIME;
	}
      else if (type == 2)
	{
	  res_type = DB_TYPE_DATE;
	}
      else if (type == 3)
	{
	  res_type = DB_TYPE_DATETIME;
	}
      else
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}
    }
  else
    {
      res_type = TP_DOMAIN_TYPE (domain);
      if (res_type != DB_TYPE_TIME && res_type != DB_TYPE_DATE &&
	  res_type != DB_TYPE_DATETIME)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error;
	}
    }

  /*
   * 1. Get information according to format specifiers
   *    iterate simultaneously through each string and sscanf when
   *    it is a format specifier.
   *    If a format specifier has more than one occurence, get the last value.
   */
  do
    {
      char sz[64];
      const int sz_count = sizeof (sz) / sizeof (char);

      len1 = strlen (sstr);
      len2 = strlen (format_s);

      i = j = k = 0;

      while (i < len1 && j < len2)
	{
	  while (WHITESPACE (sstr[i]))
	    {
	      i++;
	    }

	  while (WHITESPACE (format_s[j]))
	    {
	      j++;
	    }

	  if (j > 0 && format_s[j - 1] == '%')
	    {
	      int token_size;
	      int am_pm_id;

	      /* do not accept a double % */
	      if (j > 1 && format_s[j - 2] == '%')
		{
		  if (prm_get_bool_value
		      (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
		    {
		      error_status = ER_OBJ_INVALID_ARGUMENTS;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status,
			      0);
		    }
		  goto error;
		}

	      /* we have a format specifier */
	      switch (format_s[j])
		{
		case 'a':
		  /* %a Abbreviated weekday name (Sun..Sat) */
		  error_status = get_string_date_token_id (SDT_DAY_SHORT,
							   date_lang_id,
							   sstr + i,
							   codeset, &dow,
							   &token_size);
		  if (error_status != NO_ERROR)
		    {
		      goto conversion_error;
		    }

		  i += token_size;

		  if (dow == 0)	/* not found - error */
		    {
		      goto conversion_error;
		    }

		  dow = dow - 1;
		  break;

		case 'b':
		  /* %b Abbreviated month name (Jan..Dec) */
		  error_status = get_string_date_token_id (SDT_MONTH_SHORT,
							   date_lang_id,
							   sstr + i,
							   codeset, &m,
							   &token_size);
		  if (error_status != NO_ERROR)
		    {
		      goto conversion_error;
		    }

		  i += token_size;

		  if (m == 0)	/* not found - error */
		    {
		      goto conversion_error;
		    }
		  break;

		case 'c':
		  /* %c Month, numeric (0..12) */
		  k = parse_digits (sstr + i, &m, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'D':
		  /* %D Day of the month with English suffix (0th, 1st, 2nd,
		     3rd, ...) */
		  k = parse_digits (sstr + i, &d, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  /* need 2 necessary characters or whitespace (!) after */
		  i += 2;
		  break;

		case 'd':
		  /* %d Day of the month, numeric (00..31) */
		  k = parse_digits (sstr + i, &d, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'e':
		  /* %e Day of the month, numeric (0..31) */
		  k = parse_digits (sstr + i, &d, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'f':
		  /* %f Milliseconds (000..999) */
		  k = parse_digits (sstr + i, &ms, 3);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'H':
		  /* %H Hour (00..23) */
		  k = parse_digits (sstr + i, &h, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  h24 = 1;
		  break;

		case 'h':
		  /* %h Hour (01..12) */
		  k = parse_digits (sstr + i, &h, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'I':
		  /* %I Hour (01..12) */
		  k = parse_digits (sstr + i, &h, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'i':
		  /* %i Minutes, numeric (00..59) */
		  k = parse_digits (sstr + i, &mi, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'j':
		  /* %j Day of year (001..366) */
		  k = parse_digits (sstr + i, &doy, 3);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'k':
		  /* %k Hour (0..23) */
		  k = parse_digits (sstr + i, &h, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  h24 = 1;
		  break;

		case 'l':
		  /* %l Hour (1..12) */
		  k = parse_digits (sstr + i, &h, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'M':
		  /* %M Month name (January..December) */
		  error_status = get_string_date_token_id (SDT_MONTH,
							   date_lang_id,
							   sstr + i,
							   codeset, &m,
							   &token_size);
		  if (error_status != NO_ERROR)
		    {
		      goto conversion_error;
		    }

		  i += token_size;

		  if (m == 0)	/* not found - error */
		    {
		      goto conversion_error;
		    }
		  break;

		case 'm':
		  /* %m Month, numeric (00..12) */
		  k = parse_digits (sstr + i, &m, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'p':
		  /* %p AM or PM */
		  error_status = get_string_date_token_id (SDT_AM_PM,
							   date_lang_id,
							   sstr + i,
							   codeset,
							   &am_pm_id,
							   &token_size);
		  if (error_status != NO_ERROR)
		    {
		      goto conversion_error;
		    }

		  i += token_size;

		  if (am_pm_id > 0)
		    {
		      if (am_pm_id % 2)
			{
			  am = 0;
			}
		      else
			{
			  am = 1;
			}
		    }
		  else
		    {
		      goto conversion_error;
		    }

		  break;

		case 'r':
		  /* %r Time, 12-hour (hh:mm:ss followed by AM or PM) */

		  k = parse_digits (sstr + i, &h, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;

		  while (WHITESPACE (sstr[i]))
		    {
		      i++;
		    }

		  if (sstr[i] != ':')
		    {
		      goto conversion_error;
		    }
		  i++;

		  k = parse_digits (sstr + i, &mi, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;

		  while (WHITESPACE (sstr[i]))
		    {
		      i++;
		    }

		  if (sstr[i] != ':')
		    {
		      goto conversion_error;
		    }
		  i++;

		  k = parse_digits (sstr + i, &s, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;

		  error_status = get_string_date_token_id (SDT_AM_PM,
							   date_lang_id,
							   sstr + i,
							   codeset,
							   &am_pm_id,
							   &token_size);
		  if (error_status != NO_ERROR)
		    {
		      goto conversion_error;
		    }

		  i += token_size;

		  if (am_pm_id > 0)
		    {
		      if (am_pm_id % 2)
			{
			  am = 0;
			}
		      else
			{
			  am = 1;
			}
		    }
		  else
		    {
		      goto conversion_error;
		    }

		  break;

		case 'S':
		  /* %S Seconds (00..59) */
		  k = parse_digits (sstr + i, &s, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 's':
		  /* %s Seconds (00..59) */
		  k = parse_digits (sstr + i, &s, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'T':
		  /* %T Time, 24-hour (hh:mm:ss) */

		  k = parse_digits (sstr + i, &h, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;

		  while (WHITESPACE (sstr[i]))
		    {
		      i++;
		    }

		  if (sstr[i] != ':')
		    {
		      goto conversion_error;
		    }
		  i++;

		  k = parse_digits (sstr + i, &mi, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }

		  i += k;
		  while (WHITESPACE (sstr[i]))
		    {
		      i++;
		    }

		  if (sstr[i] != ':')
		    {
		      goto conversion_error;
		    }
		  i++;

		  k = parse_digits (sstr + i, &s, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  h24 = 1;

		  break;

		case 'U':
		  /* %U Week (00..53), where Sunday is the first day
		     of the week */
		  k = parse_digits (sstr + i, &U, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'u':
		  /* %u Week (00..53), where Monday is the first day
		     of the week */
		  k = parse_digits (sstr + i, &u, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'V':
		  /* %V Week (01..53), where Sunday is the first day
		     of the week; used with %X  */
		  k = parse_digits (sstr + i, &V, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  _v = 1;
		  break;

		case 'v':
		  /* %v Week (01..53), where Monday is the first day
		     of the week; used with %x  */
		  k = parse_digits (sstr + i, &v, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  _v = 2;
		  break;

		case 'W':
		  /* %W Weekday name (Sunday..Saturday) */
		  error_status = get_string_date_token_id (SDT_DAY,
							   date_lang_id,
							   sstr + i,
							   codeset, &dow,
							   &token_size);
		  if (error_status != NO_ERROR)
		    {
		      goto conversion_error;
		    }

		  i += token_size;

		  if (dow == 0)	/* not found - error */
		    {
		      goto conversion_error;
		    }
		  dow = dow - 1;
		  break;

		case 'w':
		  /* %w Day of the week (0=Sunday..6=Saturday) */
		  k = parse_digits (sstr + i, &dow, 1);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'X':
		  /* %X Year for the week where Sunday is the first day
		     of the week, numeric, four digits; used with %V  */
		  k = parse_digits (sstr + i, &y, 4);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  _x = 1;
		  break;

		case 'x':
		  /* %x Year for the week, where Monday is the first day
		     of the week, numeric, four digits; used with %v  */
		  k = parse_digits (sstr + i, &y, 4);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  _x = 2;
		  break;

		case 'Y':
		  /* %Y Year, numeric, four digits */
		  k = parse_digits (sstr + i, &y, 4);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;
		  break;

		case 'y':
		  /* %y Year, numeric (two digits) */
		  k = parse_digits (sstr + i, &y, 2);
		  if (k <= 0)
		    {
		      goto conversion_error;
		    }
		  i += k;

		  /* TODO: 70 convention always available? */
		  if (y < 70)
		    {
		      y = 2000 + y;
		    }
		  else
		    {
		      y = 1900 + y;
		    }

		  break;

		default:
		  goto conversion_error;
		  break;
		}
	    }
	  else if (sstr[i] != format_s[j] && format_s[j] != '%')
	    {
	      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS)
		  == false)
		{
		  error_status = ER_OBJ_INVALID_ARGUMENTS;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
		}
	      goto error;
	    }
	  else if (format_s[j] != '%')
	    {
	      i++;
	    }

	  /* when is a format specifier do not advance in sstr
	     because we need the entire value */
	  j++;
	}
    }
  while (0);

  /* 2. Validations */
  if (am != -1)			/* 24h time format and am/pm */
    {
      if (h24 == 1 || h == 0)
	{
	  goto conversion_error;
	}

      if (h == 12)
	{
	  h = 0;
	}
    }
  if (h24 == 0 && h > 12)
    {
      goto conversion_error;
    }

  if (_x != _v && _x != -1)	/* accept %v only if %x and %V only if %X */
    {
      goto conversion_error;
    }

  days[2] += LEAP (y);

  /*
   * validations are done here because they are done just on the last memorized
   * values (ie: if you supply a month 99 then a month 12 the 99 isn't validated
   * because it's overwritten by 12 which is correct).
   */

  /*
   * check only upper bounds, lower bounds will be checked later and
   * will return error
   */
  if (res_type == DB_TYPE_DATE || res_type == DB_TYPE_DATETIME)
    {
      /* replace invalid initial year with default year */
      y = (y == -1) ? 1 : y;
      /* year is validated becuase it's vital for m & d */
      if (y > 9999)
	{
	  goto conversion_error;
	}

      if (m > 12)
	{
	  goto conversion_error;
	}

      /* because we do not support invalid dates ... */
      if (m != -1 && d > days[m])
	{
	  goto conversion_error;
	}

      if (u > 53)
	{
	  goto conversion_error;
	}

      if (v > 53)
	{
	  goto conversion_error;
	}

      if (v == 0 || u > 53)
	{
	  goto conversion_error;
	}

      if (V == 0 || u > 53)
	{
	  goto conversion_error;
	}

      if (doy == 0 || doy > 365 + LEAP (y))
	{
	  goto conversion_error;
	}

      if (dow > 6)
	{
	  goto conversion_error;
	}
    }

  if (res_type == DB_TYPE_TIME || res_type == DB_TYPE_DATETIME)
    {
      if ((am != -1 && h > 12) || (am == -1 && h > 23))
	{
	  goto conversion_error;
	}
      if (am == 1 && h != -1)
	{
	  h += 12;
	  /* reset AM flag */
	  am = -1;
	}

      if (mi > 59)
	{
	  goto conversion_error;
	}

      if (s > 59)
	{
	  goto conversion_error;
	}
      /* milli does not need checking, it has all values from 0 to 999 */
    }


  /* 3. Try to compute a date according to the information from the format
     specifiers */

  if (res_type == DB_TYPE_TIME)
    {
      /* --- no job to do --- */
      goto write_results;
    }

  /* the year is fixed, compute the day and month from dow, doy, etc */
  /*
   * the day and month can be supplied specifically which suppres all other
   * informations or can be computed from dow and week or from doy
   */

  /* 3.1 - we have a valid day and month */
  if (m >= 1 && m <= 12 && d >= 1 && d <= days[m])
    {
      /* --- no job to do --- */
      goto write_results;
    }

  w = MAX (v, MAX (V, MAX (u, U)));
  /* 3.2 - we have the day of week and a week */
  if (dow != -1 && w != -1)
    {
      int dow2 = db_get_day_of_week (y, 1, 1);
      int ld_fw, save_dow, dowdiff;

      if (U == w || V == w)
	{
	  ld_fw = 7 - dow2;

	  if (w == 0)
	    {
	      dowdiff = dow - dow2;
	      d = dow2 == 0 ? 32 - (7 - dow) : dowdiff < 0 ?
		32 + dowdiff : 1 + dowdiff;
	      m = dow2 == 0 || dowdiff < 0 ? 12 : 1;
	      y = dow2 == 0 || dowdiff < 0 ? y - 1 : y;
	    }
	  else
	    {
	      d = dow2 == 0 ? 1 : ld_fw + 1;
	      m = 1;
	      if (db_add_weeks_and_days_to_date (&d, &m, &y, w - 1, dow) ==
		  ER_FAILED)
		{
		  goto conversion_error;
		}
	    }
	}
      else if (u == w || v == w)
	{
	  ld_fw = dow2 == 0 ? 1 : 7 - dow2 + 1;
	  if (w == 0 || w == 1)
	    {
	      save_dow = dow;
	      dow = dow == 0 ? 7 : dow;
	      dow2 = dow2 == 0 ? 7 : dow2;
	      dowdiff = dow - dow2;

	      if (dow2 >= 1 && dow2 <= 4)	/* start with week 1 */
		{
		  d = w == 0 ? 32 + dowdiff - 7 :
		    dowdiff < 0 ? 32 + dowdiff : 1 + dowdiff;
		  m = w == 0 || dowdiff < 0 ? 12 : 1;
		  y = w == 0 || dowdiff < 0 ? y - 1 : y;
		}
	      else
		{
		  d = dowdiff < 0 ? (w == 0 ? 32 + dowdiff : ld_fw + dow) :
		    (w == 0 ? 1 + dowdiff : 1 + dowdiff + 7);
		  m = dowdiff < 0 && w == 0 ? 12 : 1;
		  y = dowdiff < 0 && w == 0 ? y - 1 : y;
		}
	      dow = save_dow;
	    }
	  else
	    {
	      d = ld_fw + 1;
	      m = 1;

	      if (db_add_weeks_and_days_to_date (&d, &m, &y,
						 dow2 >= 1
						 && dow2 <= 4 ? w - 2 : w - 1,
						 dow == 0 ? 6 : dow - 1) ==
		  ER_FAILED)
		{
		  goto conversion_error;
		}
	    }
	}
      else
	{
	  goto conversion_error;	/* should not happen */
	}
    }
  /* 3.3 - we have the day of year */
  else if (doy != -1)
    {
      for (m = 1; doy > days[m] && m <= 12; m++)
	{
	  doy -= days[m];
	}

      d = doy;
    }

write_results:
  /* last validations before writing results - we need only complete data info */

  if (res_type == DB_TYPE_DATE || res_type == DB_TYPE_DATETIME)
    {
      /* replace invalid initial date (-1,-1,-1) with default date (1,1,1) */
      y = (y == -1) ? 1 : y;
      m = (m == -1) ? 1 : m;
      d = (d == -1) ? 1 : d;

      if (y < 0 || m < 0 || d < 0)
	{
	  goto conversion_error;
	}

      if (d > days[m])
	{
	  goto conversion_error;
	}
    }

  if (res_type == DB_TYPE_TIME || res_type == DB_TYPE_DATETIME)
    {
      if (h < 0 || mi < 0 || s < 0)
	{
	  goto conversion_error;
	}
    }

  if (res_type == DB_TYPE_DATE)
    {
      DB_MAKE_DATE (result, m, d, y);
    }
  else if (res_type == DB_TYPE_TIME)
    {
      DB_MAKE_TIME (result, h, mi, s);
    }
  else if (res_type == DB_TYPE_DATETIME)
    {
      DB_DATETIME db_datetime;

      db_datetime_encode (&db_datetime, m, d, y, h, mi, s, ms);

      DB_MAKE_DATETIME (result, &db_datetime);
    }

error:
  if (format_s)
    {
      db_private_free_and_init (NULL, format_s);
    }

  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }
  return error_status;

conversion_error:
  if (do_free_buf_str)
    {
      db_private_free (NULL, initial_buf_str);
    }

  if (format_s)
    {
      db_private_free_and_init (NULL, format_s);
    }

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      error_status = NO_ERROR;
    }
  else
    {
      error_status = ER_DATE_CONVERSION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  DB_MAKE_NULL (result);
  return error_status;
}

/*
 * db_time_dbval () - extract the time from input parameter.
 *   return: NO_ERROR, or error code
 *   result(out) : resultant db_value
 *   datetime_value(in) : time, timestamp or datetime expression
 *   domain(in): result domain
 */
int
db_time_dbval (DB_VALUE * result, const DB_VALUE * datetime_value,
	       const TP_DOMAIN * domain)
{
  DB_TYPE type;
  char *res_s;
  int hour = 0, min = 0, sec = 0, milisec = 0;
  int size, error_status = NO_ERROR;
  bool alloc_ok = true;

  if (DB_IS_NULL (datetime_value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  type = DB_VALUE_TYPE (datetime_value);

  if (db_get_time_from_dbvalue (datetime_value, &hour, &min, &sec, &milisec)
      != NO_ERROR)
    {
      DB_MAKE_NULL (result);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TIME_CONVERSION, 0);
      return ER_TIME_CONVERSION;
    }

  if (milisec != 0)
    {
      size = 12 + 1;		/* HH:MM:SS.MMM */
    }
  else
    {
      size = 8 + 1;		/* HH:MM:SS */
    }

  res_s = db_private_alloc (NULL, size);
  if (res_s == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (milisec != 0)
    {
      sprintf (res_s, "%02d:%02d:%02d.%03d", hour, min, sec, milisec);
    }
  else
    {
      sprintf (res_s, "%02d:%02d:%02d", hour, min, sec);
    }

  switch (type)
    {
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
      DB_MAKE_VARNCHAR (result, TP_FLOATING_PRECISION_VALUE, res_s,
			strlen (res_s),
			DB_GET_STRING_CODESET (datetime_value),
			DB_GET_STRING_COLLATION (datetime_value));
      break;

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      DB_MAKE_VARCHAR (result, TP_FLOATING_PRECISION_VALUE, res_s,
		       strlen (res_s),
		       DB_GET_STRING_CODESET (datetime_value),
		       DB_GET_STRING_COLLATION (datetime_value));
      break;

    default:
      DB_MAKE_STRING (result, res_s);
      break;
    }

  if (domain != NULL)
    {
      assert (TP_DOMAIN_TYPE (domain) == DB_VALUE_TYPE (result));

      db_string_put_cs_and_collation (result, TP_DOMAIN_CODESET (domain),
				      TP_DOMAIN_COLLATION (domain));
    }

  result->need_clear = true;

  return NO_ERROR;
}

/*
 * db_date_dbval () - extract the date from input parameter.
 *   return: NO_ERROR, or ER_code
 *   result(out) : resultant db_value
 *   date_value(in) : date or datetime expression
 *   domain: domain of result
 */
int
db_date_dbval (DB_VALUE * result, const DB_VALUE * date_value,
	       const TP_DOMAIN * domain)
{
  DB_TYPE type;
  char *res_s;
  int y, m, d, hour, min, sec, ms;
  int error_status = NO_ERROR;
  INTL_CODESET codeset;
  int collation_id;

  if (date_value == NULL || result == NULL)
    {
      return ER_FAILED;
    }

  y = m = d = 0;

  type = DB_VALUE_DOMAIN_TYPE (date_value);
  if (type == DB_TYPE_NULL || DB_IS_NULL (date_value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (db_get_datetime_from_dbvalue
      (date_value, &y, &m, &d, &hour, &min, &sec, &ms, NULL) != NO_ERROR)
    {
      DB_MAKE_NULL (result);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DATE_CONVERSION, 0);
      return ER_DATE_CONVERSION;
    }

  res_s = db_private_alloc (NULL, 10 + 1);	/* MM/DD/YYYY */
  if (res_s == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  sprintf (res_s, "%02d/%02d/%04d", m, d, y);

  if (domain != NULL)
    {
      codeset = TP_DOMAIN_CODESET (domain);
      collation_id = TP_DOMAIN_COLLATION (domain);
    }
  else if (TP_IS_STRING_TYPE (DB_VALUE_TYPE (date_value)))
    {
      codeset = DB_GET_STRING_CODESET (date_value);
      collation_id = DB_GET_STRING_COLLATION (date_value);
    }
  else
    {
      codeset = LANG_SYS_CODESET;
      collation_id = LANG_SYS_COLLATION;
    }

  if (QSTR_IS_NATIONAL_CHAR (type))
    {
      DB_MAKE_VARNCHAR (result, 10, res_s, 10, codeset, collation_id);
    }
  else
    {
      DB_MAKE_STRING (result, res_s);
      db_string_put_cs_and_collation (result, codeset, collation_id);
    }

  result->need_clear = true;

  return error_status;
}

/*
 *  count_leap_years_up_to - count the leap years up to year
 *  return: the counted value
 *  year(in) : the last year to evaluate
 */
int
count_leap_years_up_to (int year)
{
  return (year / 4 - year / 100 + year / 400);
}

/*
 *  count_nonleap_years_up_to - count the non leap years up to year
 *  return: the counted value
 *  year(in) : the last year to evaluate
 */
int
count_nonleap_years_up_to (int year)
{
  return (year - count_leap_years_up_to (year));
}

/*
 * db_date_diff () - expr1 ?? expr2 expressed as a value in days from
 *		     one date to the other.
 *   return: int
 *   result(out) : resultant db_value
 *   date_value1(in)   : first date
 *   date_value2(in)   : second date
 */
int
db_date_diff (const DB_VALUE * date_value1, const DB_VALUE * date_value2,
	      DB_VALUE * result)
{
  DB_TYPE type1, type2;
  int y1 = 0, m1 = 0, d1 = 0;
  int y2 = 0, m2 = 0, d2 = 0;
  int hour, min, sec, ms;
  int cly1, cly2, cnly1, cnly2, cdpm1, cdpm2, cdpy1, cdpy2, diff, i, cd1, cd2;
  int m_days[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int error_status = NO_ERROR;
  int retval;

  if (date_value1 == NULL || date_value2 == NULL || result == NULL)
    {
      error_status = ER_FAILED;
      goto error;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (date_value1);
  if (type1 == DB_TYPE_NULL || DB_IS_NULL (date_value1))
    {
      DB_MAKE_NULL (result);
      goto error;
    }

  type2 = DB_VALUE_DOMAIN_TYPE (date_value2);
  if (type2 == DB_TYPE_NULL || DB_IS_NULL (date_value2))
    {
      DB_MAKE_NULL (result);
      goto error;
    }

  retval = db_get_datetime_from_dbvalue
    (date_value1, &y1, &m1, &d1, &hour, &min, &sec, &ms, NULL);
  if (retval != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      DB_MAKE_NULL (result);
      goto error;
    }

  retval = db_get_datetime_from_dbvalue
    (date_value2, &y2, &m2, &d2, &hour, &min, &sec, &ms, NULL);
  if (retval != NO_ERROR)
    {
      error_status = ER_DATE_CONVERSION;
      DB_MAKE_NULL (result);
      goto error;
    }

  if ((y1 == 0 && m1 == 0 && d1 == 0
       && hour == 0 && min == 0 && sec == 0 && ms == 0)
      || (y2 == 0 && m2 == 0 && d2 == 0
	  && hour == 0 && min == 0 && sec == 0 && ms == 0))
    {
      er_clear ();
      DB_MAKE_NULL (result);
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  return NO_ERROR;
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_ATTEMPT_TO_USE_ZERODATE, 0);
      return ER_ATTEMPT_TO_USE_ZERODATE;
    }

  cly1 = count_leap_years_up_to (y1 - 1);
  cnly1 = count_nonleap_years_up_to (y1 - 1);
  cdpy1 = cly1 * 366 + cnly1 * 365;
  m_days[2] = LEAP (y1) ? 29 : 28;
  cdpm1 = 0;
  for (i = 1; i < m1; i++)
    {
      cdpm1 += m_days[i];
    }

  cly2 = count_leap_years_up_to (y2 - 1);
  cnly2 = count_nonleap_years_up_to (y2 - 1);
  cdpy2 = cly2 * 366 + cnly2 * 365;
  m_days[2] = LEAP (y2) ? 29 : 28;
  cdpm2 = 0;
  for (i = 1; i < m2; i++)
    {
      cdpm2 += m_days[i];
    }

  cd1 = cdpy1 + cdpm1 + d1;
  cd2 = cdpy2 + cdpm2 + d2;
  diff = cd1 - cd2;

  DB_MAKE_INTEGER (result, diff);

error:
  return error_status;
}

int
db_from_unixtime (const DB_VALUE * src_value, const DB_VALUE * format,
		  const DB_VALUE * date_lang, DB_VALUE * result,
		  const TP_DOMAIN * domain)
{
  time_t unix_timestamp;
  DB_TYPE format_type;
  int error_status = NO_ERROR;

  assert (src_value != NULL);

  if (DB_IS_NULL (src_value))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }
  if (DB_VALUE_TYPE (src_value) != DB_TYPE_INTEGER)
    {
      error_status = ER_TIMESTAMP_CONVERSION;
      goto error;
    }
  unix_timestamp = DB_GET_INT (src_value);
  if (unix_timestamp < 0)
    {
      error_status = ER_TIMESTAMP_CONVERSION;
      goto error;
    }

  if (format == NULL)
    {
      /* if unix_timestamp is called without a format argument, return the
         timestamp */
      DB_MAKE_TIMESTAMP (result, unix_timestamp);
      return NO_ERROR;
    }

  if (DB_IS_NULL (format))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  format_type = DB_VALUE_TYPE (format);
  switch (format_type)
    {
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      {
	DB_VALUE ts_val;
	DB_VALUE default_date_lang;

	DB_MAKE_TIMESTAMP (&ts_val, unix_timestamp);
	if (date_lang == NULL || DB_IS_NULL (date_lang))
	  {
	    /* use date_lang for en_US */
	    DB_MAKE_INTEGER (&default_date_lang, 0);
	    date_lang = &default_date_lang;
	  }

	error_status = db_date_format (&ts_val, format, date_lang, result,
				       domain);
	if (error_status != NO_ERROR)
	  {
	    goto error;
	  }
	return NO_ERROR;
      }

    default:
      error_status = ER_TIMESTAMP_CONVERSION;
      goto error;
    }

error:
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }
  if (er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }
  return error_status;
}

/*
 * db_time_diff () - return the difference between TIME values val1 and val2,
 *		     expressed as a TIME value
 *   return: NO_ERROR or error code
 *   result(out) : resultant db_value
 *   val1(in)    : first date/time value
 *   val2(in)    : second date/time value
 */
int
db_time_diff (const DB_VALUE * val1, const DB_VALUE * val2, DB_VALUE * result)
{
  int y1 = 0, m1 = 0, d1 = 0, hour1 = 0, min1 = 0, sec1 = 0;
  int y2 = 0, m2 = 0, d2 = 0, hour2 = 0, min2 = 0, sec2 = 0;
  int error_status = NO_ERROR;
  int leap_years1, leap_years2, days_this_year1, days_this_year2;
  int total_days1, total_days2;
  int total_seconds1, total_seconds2, time_diff, date_diff = 0;
  int min_res, sec_res, hour_res;
  int ret_int, ms;
  DB_TYPE val1_type = DB_TYPE_TIME, val2_type = DB_TYPE_TIME;
  int hour_aux, min_aux, sec_aux, ms_aux;

  assert (val1 != NULL);
  assert (val2 != NULL);

  if (DB_IS_NULL (val1) || DB_IS_NULL (val2))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  if (DB_VALUE_DOMAIN_TYPE (val1) != DB_VALUE_DOMAIN_TYPE (val2))
    {
      error_status = ER_QPROC_INVALID_PARAMETER;
      goto error;
    }

  /* get date/time information from val1 */
  if (db_get_time_from_dbvalue (val1, &hour1, &min1, &sec1, &ms) == NO_ERROR)
    {
      if (db_get_datetime_from_dbvalue (val1, &y1, &m1, &d1, &hour_aux,
					&min_aux, &sec_aux, &ms_aux, NULL)
	  == NO_ERROR)
	{
	  if (hour_aux != hour1 || min_aux != min1 || sec_aux != sec1)
	    {
	      y1 = 0;
	      m1 = 0;
	      d1 = 0;
	    }
	  else
	    {
	      val1_type = DB_TYPE_DATETIME;
	    }
	}
    }
  else
    {
      /* val1 may be Date type, try it here */
      if (db_get_datetime_from_dbvalue (val1, &y1, &m1, &d1, &hour_aux,
					&min_aux, &sec_aux, &ms_aux, NULL)
	  == NO_ERROR)
	{
	  val1_type = DB_TYPE_DATE;
	}
      else
	{
	  error_status = ER_TIME_CONVERSION;
	  goto error;
	}
    }

  /* get date/time information from val2 */
  if (db_get_time_from_dbvalue (val2, &hour2, &min2, &sec2, &ms) == NO_ERROR)
    {
      if (db_get_datetime_from_dbvalue (val2, &y2, &m2, &d2, &hour_aux,
					&min_aux, &sec_aux, &ms_aux, NULL)
	  == NO_ERROR)
	{
	  if (hour_aux != hour2 || min_aux != min2 || sec_aux != sec2)
	    {
	      y2 = 0;
	      m2 = 0;
	      d2 = 0;
	    }
	  else
	    {
	      val2_type = DB_TYPE_DATETIME;
	    }
	}
    }
  else
    {
      /* val2 may be Date type, try it here */
      if (db_get_datetime_from_dbvalue (val2, &y2, &m2, &d2, &hour_aux,
					&min_aux, &sec_aux, &ms_aux, NULL)
	  == NO_ERROR)
	{
	  val2_type = DB_TYPE_DATE;
	}
      else
	{
	  error_status = ER_TIME_CONVERSION;
	  goto error;
	}
    }
  if (val1_type != val2_type)
    {
      error_status = ER_QPROC_INVALID_PARAMETER;
      goto error;
    }

  if (val1_type != DB_TYPE_TIME)
    {
      /* convert dates to days */
      leap_years1 = count_leap_years_up_to (y1 - 1);
      days_this_year1 = db_get_day_of_year (y1, m1, d1);
      total_days1 = y1 * 365 + leap_years1 + days_this_year1;

      leap_years2 = count_leap_years_up_to (y2 - 1);
      days_this_year2 = db_get_day_of_year (y2, m2, d2);
      total_days2 = y2 * 365 + leap_years2 + days_this_year2;

      date_diff = total_days1 - total_days2;
    }

  total_seconds1 = sec1 + min1 * 60 + hour1 * 3600;
  total_seconds2 = sec2 + min2 * 60 + hour2 * 3600;
  time_diff = total_seconds1 - total_seconds2;

  date_diff = date_diff * 3600 * 24 + time_diff;

  hour_res = (date_diff / 3600);
  min_res = (date_diff % 3600) / 60;
  sec_res = date_diff - 3600 * hour_res - 60 * min_res;

  DB_MAKE_TIME (result, hour_res, min_res, sec_res);
  ret_int = (int) *(DB_GET_TIME (result));

  /* check time overflow on result */
  if (ret_int < 0)
    {
      error_status = ER_TIME_CONVERSION;
      goto error;
    }
  return NO_ERROR;

error:
  DB_MAKE_NULL (result);
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
  return error_status;
}

/*
 *  parse_time_string - parse a string given by the second argument of
 *                      timestamp function
 *  return: NO_ERROR
 *
 *  timestr(in)	    : input string
 *  timestr_size(in): input string size
 *  sign(out)	    : 0 if positive, -1 if negative
 *  h(out)	    : hours
 *  m(out)	    : minutes
 *  s(out)	    : seconds
 *  ms(out)	    : milliseconds
 */
static int
parse_time_string (const char *timestr, int timestr_size, int *sign, int *h,
		   int *m, int *s, int *ms)
{
  int args[4], num_args = 0, tmp;
  const char *ch;
  const char *dot = NULL, *end;

  assert (sign != NULL && h != NULL && m != NULL && s != NULL && ms != NULL);
  *sign = *h = *m = *s = *ms = 0;

  if (!timestr || !timestr_size)
    {
      return NO_ERROR;
    }

  ch = timestr;
  end = timestr + timestr_size;

  SKIP_SPACES (ch, end);

  if (*ch == '-')
    {
      *sign = 1;
      ch++;
    }

  /* Find dot('.') to separate milli-seconds part from whole string. */
  dot = ch;
  while (dot != end && *dot != '.')
    {
      dot++;
    }

  if (dot != end)
    {
      char ms_string[4];

      dot++;
      tmp = end - dot;
      if (tmp)
	{
	  tmp = (tmp < 3 ? tmp : 3);
	  strncpy (ms_string, dot, tmp);
	}
      ms_string[3] = '\0';

      switch (tmp)
	{
	case 0:
	  *ms = 0;
	  break;

	case 1:
	  ms_string[1] = '0';
	case 2:
	  ms_string[2] = '0';
	default:
	  *ms = atoi (ms_string);
	}
    }

  /* First ':' character means '0:'. */
  SKIP_SPACES (ch, end);
  if (ch != end && *ch == ':')
    {
      args[num_args++] = 0;
      ch++;
    }

  if (ch != end)
    {
      while (num_args < (int) (sizeof (args) / sizeof (*args))
	     && char_isdigit (*ch))
	{
	  tmp = 0;
	  do
	    {
	      /* check for overflow */
	      if (tmp >= INT_MAX / 10)
		{
		  tmp = INT_MAX;
		}
	      else
		{
		  tmp = tmp * 10 + *ch - '0';
		}
	      ch++;
	    }
	  while (ch != end && char_isdigit (*ch));

	  args[num_args++] = tmp;

	  /* Digits should be separated by ':' character.
	   * If we meet other characters, stop parsing.
	   */
	  if (ch == end || *ch != ':')
	    {
	      break;
	    }
	  ch++;
	}
    }

  switch (num_args)
    {
    case 1:
      /* Consider single value as H...HMMSS. */
      *s = args[0] % 100;
      args[0] /= 100;
      *m = args[0] % 100;
      *h = args[0] / 100;
      break;

    case 2:
      *h = args[0];
      *m = args[1];
      break;

    case 3:
      *h = args[0];
      *m = args[1];
      *s = args[2];
      break;

    case 0:
    default:
      /* do nothing */
      ;
    }
  return NO_ERROR;
}

/*
 * db_bit_to_blob - convert bit string value to blob value
 *   return: NO_ERROR or error code
 *   src_value(in): bit string value
 *   result_value(out): blob value
 */
int
db_bit_to_blob (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  DB_TYPE src_type;
  int error_status = NO_ERROR;
  DB_ELO *elo;
  char *src_str;
  int src_length = 0;

  assert (src_value != NULL && result_value != NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }
  else if (QSTR_IS_BIT (src_type))
    {
      error_status = db_create_fbo (result_value, DB_TYPE_BLOB);
      if (error_status == NO_ERROR)
	{
	  elo = db_get_elo (result_value);
	  src_str = db_get_bit (src_value, &src_length);
	  if (src_length > 0)
	    {
	      error_status = db_elo_write (elo, 0, src_str,
					   QSTR_NUM_BYTES (src_length), NULL);
	    }
	}
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_char_to_blob - convert char string value to blob value
 *   return: NO_ERROR or error code
 *   src_value(in): char string value
 *   result_value(out): blob value
 */
int
db_char_to_blob (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  DB_TYPE src_type;
  int error_status = NO_ERROR;
  DB_ELO *elo;
  char *src_str;
  int src_size;

  assert (src_value != NULL && result_value != NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (QSTR_IS_ANY_CHAR (src_type))
    {
      error_status = db_create_fbo (result_value, DB_TYPE_BLOB);
      if (error_status == NO_ERROR)
	{
	  elo = db_get_elo (result_value);
	  src_str = db_get_string (src_value);
	  src_size = DB_GET_STRING_SIZE (src_value);
	  if (src_size > 0)
	    {
	      error_status = db_elo_write (elo, 0, src_str, src_size, NULL);
	    }
	}
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_blob_to_bit - convert blob value to bit string value
 *   return: NO_ERROR or error code
 *   src_value(in): blob value
 *   length_value(in): the length to convert
 *   result_value(out): bit string value
 */
int
db_blob_to_bit (const DB_VALUE * src_value, const DB_VALUE * length_value,
		DB_VALUE * result_value)
{
  int error_status = NO_ERROR;
  DB_TYPE src_type, length_type;
  int max_length;

  assert (src_value != NULL && result_value != NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (length_value == NULL || DB_VALUE_TYPE (length_value) == DB_TYPE_NULL)
    {
      length_type = DB_TYPE_INTEGER;
      max_length = -1;
    }
  else
    {
      length_type = DB_VALUE_DOMAIN_TYPE (length_value);
      max_length = db_get_int (length_value);
    }
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (src_type == DB_TYPE_BLOB && length_type == DB_TYPE_INTEGER)
    {
      error_status = lob_to_bit_char (src_value, result_value, DB_TYPE_BLOB,
				      max_length);
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_blob_from_file - construct blob value from the file (char string literal)
 *   return: NO_ERROR or error code
 *   src_value(in): char string literal (file path)
 *   result_value(out): blob value
 */
int
db_blob_from_file (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  DB_TYPE src_type;
  int error_status = NO_ERROR;
  char path_buf[PATH_MAX + 1];	/* reserve buffer for '\0' */
  const char *default_prefix = ES_LOCAL_PATH_PREFIX;

  assert (src_value != NULL && result_value != NULL);

  path_buf[0] = '\0';
  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (QSTR_IS_CHAR (src_type))
    {
      int path_buf_len = 0;
      int src_size = DB_GET_STRING_SIZE (src_value);

      src_size =
	(src_size < 0) ? strlen (DB_PULL_STRING (src_value)) : src_size;

      if (DB_GET_STRING_SIZE (src_value) == 0)
	{
	  error_status = ER_QSTR_EMPTY_STRING;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  return error_status;
	}

      if (es_get_type (DB_PULL_STRING (src_value)) == ES_NONE)
	{
	  /* Set default prefix, if no valid prefix was set. */
	  strcpy (path_buf, default_prefix);
	  path_buf_len = strlen (path_buf);
	}

      strncat (path_buf, DB_PULL_STRING (src_value),
	       MIN (src_size, PATH_MAX - path_buf_len));
      path_buf[path_buf_len + MIN (src_size, PATH_MAX - path_buf_len)] = '\0';

      error_status =
	lob_from_file (path_buf, src_value, result_value, DB_TYPE_BLOB);
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_blob_length - get the length of blob value
 *   return: NO_ERROR or error code
 *   src_value(in): blob value
 *   result_value(out): bigint value
 */
int
db_blob_length (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  DB_TYPE src_type;
  int error_status = NO_ERROR;

  assert (src_value != NULL && result_value != NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (src_type == DB_TYPE_BLOB)
    {
      error_status = lob_length (src_value, result_value);
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_char_to_clob - convert char string value to clob value
 *   return: NO_ERROR or error code
 *   src_value(in): char string value
 *   result_value(out): clob value
 */
int
db_char_to_clob (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  DB_TYPE src_type;
  int error_status = NO_ERROR;
  DB_ELO *elo;
  char *src_str;
  int src_size;

  assert (src_value != NULL && result_value != NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (QSTR_IS_ANY_CHAR (src_type))
    {
      error_status = db_create_fbo (result_value, DB_TYPE_CLOB);
      if (error_status == NO_ERROR)
	{
	  elo = db_get_elo (result_value);
	  src_str = db_get_string (src_value);
	  src_size = DB_GET_STRING_SIZE (src_value);
	  if (src_size > 0)
	    {
	      error_status = db_elo_write (elo, 0, src_str, src_size, NULL);
	    }
	}
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_clob_to_char - convert clob value to char string value
 *   return: NO_ERROR or error code
 *   src_value(in): clob value
 *   codeset_value(in): the codeset of output string
 *   result_value(out): char string value
 */
int
db_clob_to_char (const DB_VALUE * src_value, const DB_VALUE * codeset_value,
		 DB_VALUE * result_value)
{
  int error_status = NO_ERROR;
  DB_TYPE src_type;
  int max_length;
  int cs = LANG_SYS_CODESET;

  assert (src_value != NULL && result_value != NULL);

  if (codeset_value != NULL)
    {
      assert (DB_VALUE_DOMAIN_TYPE (codeset_value) == DB_TYPE_INTEGER);

      cs = DB_GET_INTEGER (codeset_value);
      if (cs != INTL_CODESET_UTF8 && cs != INTL_CODESET_ISO88591
	  && cs != INTL_CODESET_KSC5601_EUC)
	{
	  error_status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  return error_status;
	}
    }
  src_type = DB_VALUE_DOMAIN_TYPE (src_value);

  max_length = -1;

  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (src_type == DB_TYPE_CLOB)
    {
      error_status = lob_to_bit_char (src_value, result_value, DB_TYPE_CLOB,
				      max_length);

      if (result_value != NULL
	  && DB_VALUE_DOMAIN_TYPE (result_value) == DB_TYPE_VARCHAR)
	{
	  db_string_put_cs_and_collation (result_value, cs,
					  LANG_GET_BINARY_COLLATION (cs));
	}
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_clob_from_file - construct clob value from the file (char string literal)
 *   return: NO_ERROR or error code
 *   src_value(in): char string literal (file path)
 *   result_value(out): clob value
 */
int
db_clob_from_file (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  DB_TYPE src_type;
  int error_status = NO_ERROR;
  char path_buf[PATH_MAX + 1];	/* reserve buffer for '\0' */
  const char *default_prefix = ES_LOCAL_PATH_PREFIX;

  assert (src_value != (DB_VALUE *) NULL);

  path_buf[0] = '\0';
  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (QSTR_IS_CHAR (src_type))
    {
      int path_buf_len = 0;
      int src_size = DB_GET_STRING_SIZE (src_value);

      src_size =
	(src_size < 0) ? strlen (DB_PULL_STRING (src_value)) : src_size;

      if (DB_GET_STRING_SIZE (src_value) == 0)
	{
	  error_status = ER_QSTR_EMPTY_STRING;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  return error_status;
	}

      if (es_get_type (DB_PULL_STRING (src_value)) == ES_NONE)
	{
	  /* Set default prefix, if no valid prefix was set. */
	  strcpy (path_buf, default_prefix);
	  path_buf_len = strlen (path_buf);
	}

      strncat (path_buf, DB_PULL_STRING (src_value),
	       MIN (src_size, PATH_MAX - path_buf_len));
      path_buf[path_buf_len + MIN (src_size, PATH_MAX - path_buf_len)] = '\0';

      error_status =
	lob_from_file (path_buf, src_value, result_value, DB_TYPE_CLOB);
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_clob_length - get the length of clob value
 *   return: NO_ERROR or error code
 *   src_value(in): clob value
 *   result_value(out): bigint value
 */
int
db_clob_length (const DB_VALUE * src_value, DB_VALUE * result_value)
{
  DB_TYPE src_type;
  int error_status = NO_ERROR;

  assert (src_value != NULL && result_value != NULL);

  src_type = DB_VALUE_DOMAIN_TYPE (src_value);
  if (src_type == DB_TYPE_NULL)
    {
      DB_MAKE_NULL (result_value);
      return NO_ERROR;
    }

  if (src_type == DB_TYPE_CLOB)
    {
      error_status = lob_length (src_value, result_value);
    }
  else
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  return error_status;
}

/*
 * db_get_datetime_from_dbvalue () - splits a generic DB_VALUE to
 *				     year, month, day, hour, minute, second
 * Arguments:
 *         src_date(in) : db_value to split
 *         year(out)	  : year
 *         month(out)	  : month
 *         day(out)	  : day
 *         hour(out)	  : hour
 *         minute(out)  : minute
 *         second(out)  : second
 *	   millisecond(out) : millisecond
 *	   endp(out)	: end pointer into src_date after parsing string
 * Returns: int
 * Note: Callers should not use the global error mechanism.
 *       This function returns ER_FAILED without setting the global error info
 */
int
db_get_datetime_from_dbvalue (const DB_VALUE * src_date,
			      int *year, int *month, int *day,
			      int *hour, int *minute, int *second,
			      int *millisecond, const char **endp)
{
  PT_TYPE_ENUM arg_type = PT_TYPE_NONE;
  DB_DATETIME datetime = { 0, 0 };
  int error_status = NO_ERROR;

  if (DB_IS_NULL (src_date))
    {
      /* return error if src_date is null */
      return ER_FAILED;
    }

  arg_type = DB_VALUE_DOMAIN_TYPE (src_date);
  switch (arg_type)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
      {
	DB_DATETIME db_datetime;
	int str_len;
	char *strp;

	strp = DB_PULL_STRING (src_date);
	str_len = DB_GET_STRING_SIZE (src_date);
	if (db_date_parse_datetime_parts
	    (strp, str_len, &db_datetime, NULL, NULL, NULL, endp) != NO_ERROR)
	  {
	    return ER_FAILED;
	  }

	return db_datetime_decode (&db_datetime, month, day,
				   year, hour, minute, second, millisecond);
      }

    case DB_TYPE_DATE:
      {
	*hour = 0;
	*minute = 0;
	*second = 0;
	*millisecond = 0;
	db_date_decode (DB_GET_DATE (src_date), month, day, year);

	return NO_ERROR;
      }
    case DB_TYPE_DATETIME:
      {
	return db_datetime_decode (DB_GET_DATETIME (src_date), month, day,
				   year, hour, minute, second, millisecond);
      }

    case DB_TYPE_TIMESTAMP:
      {
	DB_DATE db_date = 0;
	DB_TIME db_time = 0;
	DB_TIMESTAMP *ts_p = DB_GET_TIMESTAMP (src_date);

	db_timestamp_decode (ts_p, &db_date, &db_time);
	db_date_decode (&db_date, month, day, year);
	db_time_decode (&db_time, hour, minute, second);
	*millisecond = 0;

	return NO_ERROR;
      }
    default:
      return ER_FAILED;
    }

  return ER_FAILED;
}

/*
 * db_get_time_from_dbvalue () - splits a generic DB_VALUE to
 *				 hour, minute, second , millisecond
 * Arguments:
 *         src_date(in) : db_value to split
 *         hour(out)	: hour
 *         minute(out)  : minute
 *         second(out)  : second
 *         millisecond(out) : millisecond
 * Returns: int
 *
 * Note: Callers should not use the global error mechanism.
 *       This function returns ER_FAILED without setting the global error info
 */
int
db_get_time_from_dbvalue (const DB_VALUE * src_date, int *hour,
			  int *minute, int *second, int *millisecond)
{
  DB_TYPE arg_type = DB_TYPE_UNKNOWN;

  *millisecond = 0;

  if (DB_IS_NULL (src_date))
    {
      return ER_FAILED;
    }

  arg_type = DB_VALUE_DOMAIN_TYPE (src_date);
  switch (arg_type)
    {
    case DB_TYPE_DATE:
      {
	/* set all to 0 because we don't have any time information */
	*hour = 0;
	*minute = 0;
	*second = 0;
	return NO_ERROR;
      }

    case DB_TYPE_STRING:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
      {
	DB_TIME db_time;
	int str_len;
	char *strp;

	strp = DB_GET_STRING (src_date);
	str_len = DB_GET_STRING_SIZE (src_date);
	if (db_date_parse_time (strp, str_len, &db_time, millisecond)
	    != NO_ERROR)
	  {
	    return ER_FAILED;
	  }

	db_time_decode (&db_time, hour, minute, second);
	return NO_ERROR;
      }

    case DB_TYPE_DATETIME:
      {
	int month = 0, day = 0, year = 0;
	return db_datetime_decode (DB_GET_DATETIME (src_date), &month, &day,
				   &year, hour, minute, second, millisecond);
      }

    case DB_TYPE_TIME:
      {
	db_time_decode (DB_GET_TIME (src_date), hour, minute, second);
	return NO_ERROR;
      }

    case DB_TYPE_TIMESTAMP:
      {
	DB_DATE db_date = 0;
	DB_TIME db_time = 0;
	DB_TIMESTAMP *ts_p = DB_GET_TIMESTAMP (src_date);

	db_timestamp_decode (ts_p, &db_date, &db_time);
	db_time_decode (&db_time, hour, minute, second);

	return NO_ERROR;
      }

    default:
      return ER_FAILED;
    }

  return ER_FAILED;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * db_null_terminate_string () - create a null terminated c string from a
 *				 DB_VALUE of type DB_TYPE_CHAR or
 *				 DB_TYPE_NCHAR
 *    return	    : NO_ERROR or error code
 *    src_value(in) : DB_VALUE containing the string
 *    strp(out)	    : pointer for output
 *
 * Note: the strp argument should not be allocated before calling this
 *	 function and should be freed by the code calling this function
 */
int
db_null_terminate_string (const DB_VALUE * src_value, char **strp)
{
  int src_size = 0;
  DB_TYPE src_type = DB_TYPE_UNKNOWN;

  if (src_value == NULL)
    {
      return ER_FAILED;
    }

  src_size = DB_GET_STRING_SIZE (src_value);
  src_type = DB_VALUE_DOMAIN_TYPE (src_value);

  if (src_type != DB_TYPE_CHAR && src_type != DB_TYPE_NCHAR)
    {
      return ER_FAILED;
    }

  *strp = (char *) db_private_alloc (NULL, (size_t) src_size + 1);
  if (*strp == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (*strp, DB_PULL_STRING (src_value), src_size);
  (*strp)[src_size] = '\0';

  return NO_ERROR;
}
#endif

/*
 * db_get_next_like_pattern_character () - Iterates through a LIKE pattern
 *
 * returns: NO_ERROR or error code
 *
 * pattern(in): the pattern that will be iterated upon
 * length(in): the length of the pattern (bytes)
 * codeset(in): codeset oof pattern string
 * has_escape_char(in): whether the LIKE pattern can use an escape character
 * escape_str(in): if has_escape_char is true this is the escaping character
 *                 used in the pattern, otherwise the parameter has no
 *                 meaning and should have the value NULL
 * position(in/out): pointer to the pattern position counter. The initial
 *                   value of the counter should be 0, meaning no characters
 *                   have yet been iterated. When (*position == length) the
 *                   iteration has come to an end. While iterating the pattern
 *                   the position value should not be changed by the callers
 *                   of this function.
 * crt_char_p(out): when the function returns this is the current character in
 *                  the pattern
 * is_escaped(out): whether the current character pointed to by "character"
 *                  is escaped in the pattern
 */
static int
db_get_next_like_pattern_character (const char *const pattern,
				    const int length,
				    const INTL_CODESET codeset,
				    const bool has_escape_char,
				    const char *escape_str,
				    int *const position,
				    char **crt_char_p,
				    bool * const is_escaped)
{
  int error_code = NO_ERROR;
  int char_size = 1;

  if (pattern == NULL || length < 0 || position == NULL || crt_char_p == NULL
      || is_escaped == NULL || *position < 0 || *position >= length)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  assert (has_escape_char ^ (escape_str == NULL));

  *crt_char_p = NULL;
  *is_escaped = false;

  if (has_escape_char &&
      intl_cmp_char ((unsigned char *) &(pattern[*position]),
		     (unsigned char *) escape_str, codeset, &char_size) == 0)
    {
      *position += char_size;
      if (*position >= length)
	{
	  /* To keep MySQL compatibility, when the last character
	   * is escape char, do not return error.*/
	  *crt_char_p = (char *) (&(pattern[*position - char_size]));
	  return error_code;
	}
      *is_escaped = true;
    }

  *crt_char_p = (char *) (&(pattern[*position]));
  intl_char_size ((unsigned char *) *crt_char_p, 1, codeset, &char_size);
  *position += char_size;

  return error_code;

error_exit:
  return error_code;
}

/*
 * is_safe_last_char_for_like_optimization () -
 *
 * return: whether a character can be the last one in the string for LIKE
 *         index optimization. See db_get_info_for_like_optimization for
 *         details.
 *
 * chr(in) : the character to consider
 * is_escaped(in) : whether the character is escaped in the LIKE pattern
 */
static bool
is_safe_last_char_for_like_optimization (const char *chr,
					 const bool is_escaped,
					 INTL_CODESET codeset)
{
  assert (chr != NULL);

  if (!is_escaped && QSTR_IS_LIKE_WILDCARD_CHAR (*chr))
    {
      return false;
    }

  if (intl_is_max_bound_chr (codeset, (const unsigned char *) chr) ||
      intl_is_min_bound_chr (codeset, (const unsigned char *) chr))
    {
      return false;
    }
  return true;
}

/*
 * db_get_info_for_like_optimization () - Gathers the information required for
 *                                        performing the LIKE index
 *                                        optimization
 *
 * returns: NO_ERROR or error code
 *
 * pattern(in): the LIKE pattern
 * has_escape_char(in): whether the LIKE pattern can use an escape character
 * escape_str(in): if has_escape_char is true this is the escaping character
 *                 used in the pattern, otherwise the parameter has no
 *                 meaning and should have the value NULL
 * num_logical_chars(out): the number of logical characters in the pattern.
 *                         This is equal to the pattern length minus the
 *                         escaping characters.
 * last_safe_logical_pos(out): the last character that can be used for the
 *                             string in the predicate rewrite or a negative
 *                             value if that particular rewrite cannot be
 *                             performed
 * num_match_many(out): the number of LIKE_WILDCARD_MATCH_MANY logical
 *                      characters (not escaped '%' characters)
 * num_match_one(out): the number of LIKE_WILDCARD_MATCH_ONE logical
 *                     characters (not escaped '_' characters)
 *
 * Note: db_compress_like_pattern should be applied on the pattern before
 *       calling this function.
 *
 * Note: This function can be used for rewriting a LIKE predicate in order to
 *       maximize the chance of using an index scan. The possible rewrites for
 *       "expr LIKE pattern [ESCAPE escape]" are the following:
 *
 * 1)
 * if the pattern is '%' we match any non-null value; we can rewrite to:
 *      expr IS NOT NULL
 *
 * 2)
 * if the pattern has no wildcards (num_match_many == 0 && num_match_one == 0)
 * and there are no comparison issues caused by trailing pattern whitespace,
 * we can rewrite to a simple equality predicate:
 *      expr = remove_escaping (pattern [, escape])
 *
 * 3.1)
 * in most other cases we can rewrite to:
 *      expr >= like_lower_bound ( like_prefix (pattern [, escape]) ) &&
 *      expr <  like_upper_bound ( like_prefix (pattern [, escape]) ) &&
 *      expr LIKE pattern [ESCAPE escape]
 * The first two predicates provide early filtering of possible matches and
 * can be optimized through index scans. The last predicate provides an extra
 * filter to ensure that the expression actually matches the original pattern.
 *
 * This rewrite is only possible if there exist strings S_lower and S_upper
 * such that all LIKE matches are "BETWEEN S_lower GE_LT S_upper". We can
 * compute these strings (see db_get_like_optimization_bounds) based on the
 * longest prefix that does not contain a '%' character. The prefix itself can
 * generally serve as S_lower while the prefix with the last character
 * incremented by one can serve as S_upper. However, this imposes some
 * restrictions on the last character in the prefix: it must have a succesor
 * (it must not be the character 255), it must not cause issues during index
 * scans (the space character might cause such issues because of its collation
 * properties for VARCHAR). The special '_' wildcard can become the smallest
 * possible character of the collation in S_lower (a space character) and the
 * highest possible character in S_upper (character 255). Because of these
 * properties, the '_' wildcard cannot be the last character in the prefix.
 * Also see the is_safe_last_char_for_like_optimization function that codes
 * this logic used to compute the last_safe_logical_pos parameter value.
 *
 * 3.2)
 * If (pattern == like_prefix (pattern) + '%') and if the pattern does not
 * contain additional wildcards ('_') then we can exclude the LIKE predicate
 * and rewrite to:
 *      expr >= like_lower_bound ( like_prefix (pattern [, escape]) ) &&
 *      expr <  like_upper_bound ( like_prefix (pattern [, escape]) )
 *
 * 3.3)
 * If the rewrite 3.1 cannot be performed we can still use an index scan if
 * like_lower_bound would returns negative infinity and like_upper_bound
 * returns positive infinity, leading to:
 *      expr >= -infinity &&
 *      expr <  +infinity &&
 *      expr LIKE pattern [ESCAPE escape]
 * See db_get_like_optimization_bounds for details.
 *
 * Rewrite 3.1 (combined with the special case 3.3) is the most general,
 * covering all the possible combinations, although it might result in slower
 * execution than the alternatives.
 */
int
db_get_info_for_like_optimization (const DB_VALUE * const pattern,
				   const bool has_escape_char,
				   const char *escape_str,
				   int *const num_logical_chars,
				   int *const last_safe_logical_pos,
				   int *const num_match_many,
				   int *const num_match_one)
{
  int i = 0;
  int error_code = NO_ERROR;
  const char *pattern_str = NULL;
  int pattern_size = 0;

  if (pattern == NULL || num_logical_chars == NULL ||
      last_safe_logical_pos == NULL || num_match_many == NULL ||
      num_match_one == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  assert (has_escape_char ^ (escape_str == NULL));

  if (DB_IS_NULL (pattern) || !QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (pattern)))
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  *num_logical_chars = 0;
  *last_safe_logical_pos = -22;
  *num_match_many = 0;
  *num_match_one = 0;
  pattern_str = DB_GET_STRING (pattern);
  pattern_size = DB_GET_STRING_SIZE (pattern);

  for (i = 0; i < pattern_size;)
    {
      char *crt_char_p = NULL;
      bool is_escaped = false;

      error_code =
	db_get_next_like_pattern_character (pattern_str, pattern_size,
					    DB_GET_STRING_CODESET (pattern),
					    has_escape_char, escape_str, &i,
					    &crt_char_p, &is_escaped);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}

      if (!is_escaped)
	{
	  if (*crt_char_p == LIKE_WILDCARD_MATCH_MANY)
	    {
	      ++(*num_match_many);
	    }
	  else if (*crt_char_p == LIKE_WILDCARD_MATCH_ONE)
	    {
	      ++(*num_match_one);
	    }
	}

      if (*num_match_many == 0
	  && is_safe_last_char_for_like_optimization (crt_char_p,
						      is_escaped,
						      DB_GET_STRING_CODESET
						      (pattern)))
	{
	  *last_safe_logical_pos = *num_logical_chars;
	}

      ++(*num_logical_chars);
    }

  return error_code;

error_exit:
  return error_code;
}

/*
 * db_get_like_optimization_bounds () - Computes the bounding limits required
 *                                      for performing the LIKE index
 *                                      optimization
 *
 * returns: NO_ERROR or error code
 *
 * pattern(in): the LIKE pattern
 * bound(out): the computed upper or lower bound.
 * has_escape_char(in): whether the LIKE pattern can use an escape character
 * escape_str(in): if has_escape_char is true this is the escaping character
 *                 used in the pattern, otherwise the parameter has no
 *                 meaning and should have the value NULL
 * compute_lower_bound(in): whether to compute the upper or the lower bound
 * last_safe_logical_pos(in): the last character that can be used for the
 *                             string in the predicate rewrite or a negative
 *                             value if that particular rewrite cannot be
 *                             performed.
 *
 * Note: See the comments on db_get_info_for_like_optimization for details
 *       on what this function computes.
 *
 * Note: If last_safe_logical_pos is negative the lower bound of the index
 *       scan is negative infinity (equivalent to the empty string or the
 *       string ' ' for the CHAR/VARCHAR default collation) and the upper
 *       bound is positive infinity (currently approximated by a string of
 *       one character code 255).
 */
int
db_get_like_optimization_bounds (const DB_VALUE * const pattern,
				 DB_VALUE * bound,
				 const bool has_escape_char,
				 const char *escape_str,
				 const bool compute_lower_bound,
				 const int last_safe_logical_pos)
{
  int error_code = NO_ERROR;
  const char *original = NULL;
  int original_size = 0;
  char *result = NULL;
  int result_length = 0;
  int result_size = 0;
  int i = 0;
  int alloc_size;
  int char_count;
  INTL_CODESET codeset;
  int collation_id;

  if (pattern == NULL || bound == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  assert (has_escape_char ^ (escape_str == NULL));

  if (DB_IS_NULL (pattern))
    {
      DB_MAKE_NULL (bound);
      goto fast_exit;
    }

  codeset = DB_GET_STRING_CODESET (pattern);
  collation_id = DB_GET_STRING_COLLATION (pattern);

  if (!QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (pattern)))
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  if (last_safe_logical_pos < 0)
    {
      if (compute_lower_bound)
	{
	  error_code =
	    db_value_domain_min (bound, DB_TYPE_VARCHAR,
				 DB_VALUE_PRECISION (pattern),
				 DB_VALUE_SCALE (pattern),
				 codeset, collation_id, NULL);
	  if (error_code != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
      else
	{
	  error_code =
	    db_value_domain_max (bound, DB_TYPE_VARCHAR,
				 DB_VALUE_PRECISION (pattern),
				 DB_VALUE_SCALE (pattern),
				 codeset, collation_id, NULL);
	  if (error_code != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}

      goto fast_exit;
    }

  original = DB_PULL_STRING (pattern);
  original_size = DB_GET_STRING_SIZE (pattern);

  /* assume worst case scenario : all characters in output bound string are
   * stored on the maximum character size */
  intl_char_count ((unsigned char *) original, original_size, codeset,
		   &char_count);
  alloc_size = LOC_MAX_UCA_CHARS_SEQ * char_count
    * INTL_CODESET_MULT (codeset);
  assert (alloc_size >= original_size);

  result = db_private_alloc (NULL, alloc_size + 1);
  if (result == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error_exit;
    }

  assert (last_safe_logical_pos < char_count);

  for (i = 0, result_length = 0, result_size = 0;
       result_length <= last_safe_logical_pos;)
    {
      char *crt_char_p = NULL;
      bool is_escaped = false;

      error_code =
	db_get_next_like_pattern_character (original, original_size,
					    codeset, has_escape_char,
					    escape_str, &i, &crt_char_p,
					    &is_escaped);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}

      if (result_length == last_safe_logical_pos)
	{
	  assert (is_safe_last_char_for_like_optimization
		  (crt_char_p, is_escaped, codeset));
	}

      if (!is_escaped && *crt_char_p == LIKE_WILDCARD_MATCH_ONE)
	{
	  assert (result_length < last_safe_logical_pos);
	  if (compute_lower_bound)
	    {
	      result_size +=
		intl_set_min_bound_chr (codeset, result + result_size);
	    }
	  else
	    {
	      result_size +=
		intl_set_max_bound_chr (codeset, result + result_size);
	    }
	  result_length++;
	}
      else
	{
	  if (result_length == last_safe_logical_pos && !compute_lower_bound)
	    {
	      char *next_alpha_char_p = result + result_size;
	      int next_len = 0;

	      result_size +=
		QSTR_NEXT_ALPHA_CHAR (collation_id,
				      (unsigned char *) crt_char_p,
				      original + original_size - crt_char_p,
				      (unsigned char *) next_alpha_char_p,
				      &next_len);
	      result_length += next_len;
	    }
	  else
	    {
	      result_size += intl_put_char ((unsigned char *) result +
					    result_size,
					    (unsigned char *) crt_char_p,
					    codeset);
	      result_length++;
	    }

	}
    }

  assert (result_size <= alloc_size);
  qstr_make_typed_string (DB_TYPE_VARCHAR, bound,
			  DB_VALUE_PRECISION (pattern), result, result_size,
			  codeset, collation_id);
  result[result_size] = 0;
  bound->need_clear = true;

fast_exit:
  return error_code;

error_exit:
  if (result != NULL)
    {
      db_private_free_and_init (NULL, result);
    }
  return error_code;
}

/*
 * db_compress_like_pattern () - Optimizes a LIKE pattern for faster execution
 *                               and easier processing.
 *
 * returns: NO_ERROR or error code
 *
 * pattern(in): the LIKE pattern to be compressed
 * compressed_pattern(out): the optimized pattern (should be cleared before
 *                          being passed to this function)
 * has_escape_char(in): whether the LIKE pattern can use an escape character
 * escape_str(in): if has_escape_char is true this is the escaping character
 *                 used in the pattern, otherwise the parameter has no
 *                 meaning and should have the value NULL
 *
 * Note: This function removes all the unnecessary escape characters in
 *       order to ease subsequent processing of the pattern. Currently there
 *       are no such unnecessary escape sequences, but there might be in the
 *       future if supporting MySQL semantics. See the comments in
 *       db_get_next_like_pattern_character.
 */
/* TODO This function could perform an extra optimization. The pattern
 *      'a%___%b' can be compressed to either 'a___%b' or 'a%___b'. The first
 *      form is prefferable as it should execute faster than the second.
 *      Also, if 'a%___b' is initially present, it can be changed to 'a___%b'.
 */
int
db_compress_like_pattern (const DB_VALUE * const pattern,
			  DB_VALUE * compressed_pattern,
			  const bool has_escape_char, const char *escape_str)
{
  int error_code = NO_ERROR;
  const char *original = NULL;
  int original_size = 0;
  char *result = NULL;
  int result_length = 0;
  int result_size = 0;
  int i = 0;
  int alloc_size;
  bool in_percent_sequence = false;
  INTL_CODESET codeset;

  if (pattern == NULL || compressed_pattern == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  assert (has_escape_char ^ (escape_str == NULL));

  if (DB_IS_NULL (pattern))
    {
      DB_MAKE_NULL (compressed_pattern);
      goto fast_exit;
    }

  if (!QSTR_IS_ANY_CHAR (DB_VALUE_DOMAIN_TYPE (pattern)))
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  codeset = DB_GET_STRING_CODESET (pattern);
  original = DB_PULL_STRING (pattern);
  original_size = DB_GET_STRING_SIZE (pattern);

  if (has_escape_char)
    {
      int char_count;

      intl_char_count ((unsigned char *) original, original_size,
		       codeset, &char_count);
      /* assume worst case : each character in the compressed pattern is
       * precedeed by the escape char */
      alloc_size = original_size + char_count * strlen (escape_str);
    }
  else
    {
      alloc_size = original_size;
    }

  result = db_private_alloc (NULL, alloc_size + 1);
  if (result == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error_exit;
    }

  for (i = 0, result_length = 0, result_size = 0, in_percent_sequence = false;
       i < original_size;)
    {
      char *crt_char_p = NULL;
      bool keep_crt_char = false;
      bool needs_escape = false;
      bool is_escaped = false;

      error_code =
	db_get_next_like_pattern_character (original, original_size,
					    codeset, has_escape_char,
					    escape_str, &i, &crt_char_p,
					    &is_escaped);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}

      if (is_escaped)
	{
	  needs_escape = true;
	}

      assert (crt_char_p != NULL);

      if (!is_escaped && *crt_char_p == LIKE_WILDCARD_MATCH_MANY &&
	  in_percent_sequence)
	{
	  keep_crt_char = false;
	}
      else
	{
	  keep_crt_char = true;
	}

      if (keep_crt_char)
	{
	  if (needs_escape)
	    {
	      assert (has_escape_char);
	      result_size += intl_put_char ((unsigned char *) result +
					    result_size,
					    (unsigned char *) escape_str,
					    codeset);
	      result_length++;
	    }
	  result_size +=
	    intl_put_char ((unsigned char *) result + result_size,
			   (unsigned char *) crt_char_p, codeset);
	  result_length++;
	}

      if (!is_escaped && *crt_char_p == LIKE_WILDCARD_MATCH_MANY)
	{
	  in_percent_sequence = true;
	}
      else
	{
	  in_percent_sequence = false;
	}
    }

  assert (result_length <= alloc_size);
  result[result_size] = 0;
  DB_MAKE_VARCHAR (compressed_pattern, TP_FLOATING_PRECISION_VALUE,
		   result, result_size, codeset,
		   DB_GET_STRING_COLLATION (pattern));
  compressed_pattern->need_clear = true;

fast_exit:
  return error_code;

error_exit:
  if (result != NULL)
    {
      db_private_free_and_init (NULL, result);
    }
  return error_code;
}

/*
 * db_like_bound () - Computes the bounding limits required for performing the
 *                    LIKE index optimization
 *
 * returns: NO_ERROR or error code
 *
 * src_pattern(in): the LIKE pattern
 * src_escape(in): the escape character or NULL if there is no escaping
 * result_bound(out): the computed upper or lower bound.
 * compute_lower_bound(in): whether to compute the upper or the lower bound
 *
 * Note: See the comments on db_get_info_for_like_optimization for details
 *       on what this function computes.
 */
int
db_like_bound (const DB_VALUE * const src_pattern,
	       const DB_VALUE * const src_escape,
	       DB_VALUE * const result_bound, const bool compute_lower_bound)
{
  int error_code = NO_ERROR;
  bool has_escape_char = false;
  DB_VALUE compressed_pattern;
  int num_logical_chars = 0;
  int last_safe_logical_pos = 0;
  int num_match_many = 0;
  int num_match_one = 0;
  const char *escape_str = NULL;

  DB_MAKE_NULL (&compressed_pattern);

  if (src_pattern == NULL || result_bound == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  if (DB_IS_NULL (src_pattern))
    {
      DB_MAKE_NULL (result_bound);
      goto fast_exit;
    }

  if (!QSTR_IS_ANY_CHAR (DB_VALUE_DOMAIN_TYPE (src_pattern)))
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error_exit;
    }

  if (src_escape == NULL)
    {
      has_escape_char = false;
    }
  else
    {
      if (DB_IS_NULL (src_escape))
	{
	  /* a LIKE b ESCAPE NULL means use the default escape character
	   * '\\' */
	  has_escape_char = true;
	  escape_str = "\\";
	}
      else
	{
	  if (!QSTR_IS_ANY_CHAR (DB_VALUE_DOMAIN_TYPE (src_pattern)))
	    {
	      error_code = ER_QSTR_INVALID_DATA_TYPE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	      goto error_exit;
	    }

	  escape_str = DB_PULL_STRING (src_escape);

	  if (DB_GET_STRING_LENGTH (src_escape) != 1 || escape_str[0] == 0)
	    {
	      error_code = ER_QSTR_INVALID_ESCAPE_CHARACTER;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	      goto error_exit;
	    }

	  has_escape_char = true;
	}
    }

  error_code = db_compress_like_pattern (src_pattern, &compressed_pattern,
					 has_escape_char, escape_str);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  error_code =
    db_get_info_for_like_optimization (&compressed_pattern, has_escape_char,
				       escape_str, &num_logical_chars,
				       &last_safe_logical_pos,
				       &num_match_many, &num_match_one);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  error_code =
    db_get_like_optimization_bounds (&compressed_pattern, result_bound,
				     has_escape_char, escape_str,
				     compute_lower_bound,
				     last_safe_logical_pos);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

fast_exit:
  pr_clear_value (&compressed_pattern);
  return error_code;

error_exit:
  pr_clear_value (&compressed_pattern);
  return error_code;
}


/*
 * db_check_or_create_null_term_string () - checks if the buffer associated to
 *		      string DB_VALUE is null terminated; if it is returns it
 *                    LIKE index optimization
 *
 * returns: NO_ERROR or error code
 *
 * str_val(in): source string DB_VALUE
 * pre_alloc_buf(in): preallocated buffer to store null terminated string
 * pre_alloc_buf_size(in): size of preallocated buffer
 * ignore_prec_spaces(in): true if it should ignore preceding spaces
 *			   (used only when new buffer needs to be allocated)
 * ignore_trail_spaces(in): true if it should ignore trailing spaces
 *			   (used only when new buffer needs to be allocated)
 * str_out(out): pointer to null terminated string
 * do_alloc(out): set to true if new buffer was allocated
 *
 */
static int
db_check_or_create_null_term_string (const DB_VALUE * str_val,
				     char *pre_alloc_buf,
				     int pre_alloc_buf_size,
				     bool ignore_prec_spaces,
				     bool ignore_trail_spaces,
				     char **str_out, bool * do_alloc)
{
  char *val_buf;
  char *new_buf;
  char *val_buf_end = NULL, *val_buf_end_non_space = NULL;
  int val_size;

  assert (pre_alloc_buf != NULL);
  assert (pre_alloc_buf_size > 1);
  assert (str_out != NULL);
  assert (do_alloc != NULL);
  assert (QSTR_IS_ANY_CHAR (DB_VALUE_DOMAIN_TYPE (str_val)));

  *do_alloc = false;

  val_buf = DB_GET_STRING (str_val);
  if (val_buf == NULL)
    {
      *str_out = NULL;
      return NO_ERROR;
    }
  val_size = DB_GET_STRING_SIZE (str_val);

  /* size < 0 assumes a null terminated string */
  if (val_size < 0
      || ((val_size < DB_VALUE_PRECISION (str_val)
	   || DB_VALUE_PRECISION (str_val) == TP_FLOATING_PRECISION_VALUE)
	  && val_buf[val_size] == '\0'))
    {
      /* already null terminated , safe to use it */
      *str_out = val_buf;
      return NO_ERROR;
    }

  if (val_size < pre_alloc_buf_size)
    {
      /* use the preallocated buffer supplied to copy the content */
      strncpy (pre_alloc_buf, val_buf, val_size);
      pre_alloc_buf[val_size] = '\0';
      *str_out = pre_alloc_buf;
      return NO_ERROR;
    }

  /* trim preceding and trailing spaces */
  val_buf_end = val_buf + val_size;
  if (ignore_prec_spaces)
    {
      while (val_buf < val_buf_end &&
	     ((*val_buf) == ' ' || (*val_buf) == '\t' ||
	      (*val_buf) == '\r' || (*val_buf) == '\n'))
	{
	  val_buf++;
	}
      val_size = val_buf_end - val_buf;
      assert (val_size >= 0);
    }

  if (ignore_trail_spaces && val_size > 0)
    {
      val_buf_end_non_space = val_buf + val_size - 1;

      while (val_buf < val_buf_end_non_space &&
	     ((*val_buf_end_non_space) == ' ' ||
	      (*val_buf_end_non_space) == '\t' ||
	      (*val_buf_end_non_space) == '\r' ||
	      (*val_buf_end_non_space) == '\n'))
	{
	  val_buf_end_non_space--;
	}
      val_size = val_buf_end_non_space - val_buf + 1;
      assert (val_size >= 0);
    }

  if (val_size < pre_alloc_buf_size)
    {
      assert (ignore_prec_spaces || ignore_trail_spaces);

      /* use the preallocated buffer supplied to copy the content */
      strncpy (pre_alloc_buf, val_buf, val_size);
      pre_alloc_buf[val_size] = '\0';
      *str_out = pre_alloc_buf;
      return NO_ERROR;
    }

  new_buf = db_private_alloc (NULL, val_size + 1);
  if (new_buf == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  strncpy (new_buf, val_buf, val_size);
  new_buf[val_size] = '\0';
  *str_out = new_buf;
  *do_alloc = true;

  return NO_ERROR;
}

/*
 * get_string_date_token_id() - get the id of date token identifier
 *   return: NO_ERROR or error code
 *   token_type(in): string-to-date token type
 *   intl_lang_id(in):
 *   cs(in): input string to search for token (considered NULL terminated)
 *   token_id(out): id of token (if non-zero) or zero if not found;
 *		    range begins from 1 :days 1 - 7, months 1 - 12
 *   token_size(out): size in bytes ocupied by token in input string 'cs'
 */
static int
get_string_date_token_id (const STRING_DATE_TOKEN token_type,
			  const INTL_LANG intl_lang_id, const char *cs,
			  const INTL_CODESET codeset,
			  int *token_id, int *token_size)
{
  const char **p;
  int error_status = NO_ERROR;
  int search_size;
  const char *parse_order;
  int i;
  int cs_size;
  int skipped_leading_chars = 0;
  const LANG_LOCALE_DATA *lld =
    lang_get_specific_locale (intl_lang_id, codeset);

  assert (cs != NULL);
  assert (token_id != NULL);
  assert (token_size != NULL);

  if (lld == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (intl_lang_id),
	      lang_charset_name (codeset));
      return error_status;
    }

#if 0
  /* special case : korean short month name is read as digit */
  if (intl_lang_id == INTL_LANG_KOREAN && codeset == INTL_CODESET_ISO88591
      && token_type == SDT_MONTH_SHORT)
    {
      i = parse_digits ((char *) cs, token_id, 2);
      if (i <= 0)
	{
	  error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  return error_status;
	}

      if (*token_id < 1 || *token_id > 12)
	{
	  error_status = ER_QSTR_MISMATCHING_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  return error_status;
	}

      *token_size = i;

      return NO_ERROR;
    }
#endif

  switch (token_type)
    {
    case SDT_DAY:
      p = (const char **) lld->day_name;
      parse_order = lld->day_parse_order;
      search_size = 7;
      break;
    case SDT_DAY_SHORT:
      p = (const char **) lld->day_short_name;
      parse_order = lld->day_short_parse_order;
      search_size = 7;
      break;
    case SDT_MONTH:
      p = (const char **) lld->month_name;
      parse_order = lld->month_parse_order;
      search_size = 12;
      break;
    case SDT_MONTH_SHORT:
      p = (const char **) lld->month_short_name;
      parse_order = lld->month_short_parse_order;
      search_size = 12;
      break;
    case SDT_AM_PM:
      p = (const char **) lld->am_pm;
      parse_order = lld->am_pm_parse_order;
      search_size = 12;
      break;
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  *token_id = 0;

  while (WHITESPACE (*cs))
    {
      cs++;
      skipped_leading_chars++;
    }

  cs_size = strlen (cs);

  for (i = 0; i < search_size; i++)
    {
      int cmp = 0;
      int token_index = parse_order[i];
      cmp =
	intl_case_match_tok (intl_lang_id, codeset,
			     (unsigned char *) p[token_index],
			     (unsigned char *) cs, strlen (p[token_index]),
			     cs_size, token_size);

      assert (*token_size <= cs_size);

      if (cmp == 0)
	{
	  *token_id = token_index + 1;
	  *token_size += skipped_leading_chars;
	  break;
	}
    }

  return NO_ERROR;
}

/*
 * print_string_date_token() - prints a date token to a buffer
 *   return: NO_ERROR or error code
 *   token_type(in): string-to-date token type
 *   intl_lang_id(in): locale identifier
 *   codeset(in): codeset to use for string to print; paired with intl_lang_id
 *   token_id(in): id of token (zero-based index)
 *		   for days: 0 - 6, months: 0 - 11
 *   case_mode(in): casing for printing token:
 *		    0 : unchanged; 1 - force lowercase; 2 - force uppercase
 *   buffer(in/out) : buffer to print to
 *   token_size(out): size in bytes of token printed
 */
static int
print_string_date_token (const STRING_DATE_TOKEN token_type,
			 const INTL_LANG intl_lang_id,
			 const INTL_CODESET codeset, int token_id,
			 int case_mode, char *buffer, int *token_size)
{
  const char *p;
  int error_status = NO_ERROR;
  int token_len;
  int token_bytes;
  int print_len = -1;
  const LANG_LOCALE_DATA *lld =
    lang_get_specific_locale (intl_lang_id, codeset);

  assert (buffer != NULL);
  assert (token_id >= 0);
  assert (token_size != NULL);

  if (lld == NULL)
    {
      error_status = ER_LANG_CODESET_NOT_AVAILABLE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 2,
	      lang_get_lang_name_from_id (intl_lang_id),
	      lang_charset_name (codeset));
      return error_status;
    }

  switch (token_type)
    {
    case SDT_DAY:
      assert (token_id < 7);
      p = lld->day_name[token_id];

      /* day names for all language use at most 9 chars */
      print_len = 9;
      break;

    case SDT_DAY_SHORT:
      assert (token_id < 7);
      p = lld->day_short_name[token_id];

      switch (intl_lang_id)
	{
	case INTL_LANG_ENGLISH:
	  print_len = 3;
	  break;
	case INTL_LANG_TURKISH:
	  print_len = 2;
	  break;
	default:
	  print_len = -1;
	  break;
	}
      break;

    case SDT_MONTH:
      assert (token_id < 12);
      p = lld->month_name[token_id];

      switch (intl_lang_id)
	{
	case INTL_LANG_ENGLISH:
	  print_len = 9;
	  break;
	case INTL_LANG_TURKISH:
	  print_len = 7;
	  break;
	default:
	  print_len = -1;
	  break;
	}

      break;

    case SDT_MONTH_SHORT:
      assert (token_id < 12);
      p = lld->month_short_name[token_id];

      /* all short names for months have 3 chars */
      print_len = 3;
      break;

    case SDT_AM_PM:
      assert (token_id < 12);
      p = lld->am_pm[token_id];

      /* AM/PM tokens are printed without padding */
      print_len = -1;
      break;

    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

#if 0
  if (codeset == INTL_CODESET_KSC5601_EUC && intl_lang_id == INTL_LANG_KOREAN)
    {
      /* korean names dot not use compatible codeset, we use
       * specific code to print them */
      switch (token_type)
	{
	case SDT_DAY:
	  sprintf (buffer, "%-6s", p);
	  *token_size = 6;
	  break;
	case SDT_MONTH:
	  sprintf (buffer, "%-4s", p);
	  *token_size = 4;
	  break;
	case SDT_DAY_SHORT:
	  memcpy (buffer, p, 2);
	  *token_size = 2;
	  break;
	case SDT_MONTH_SHORT:
	  sprintf (buffer, "%d", token_id + 1);
	  *token_size = (token_id < 10) ? 1 : 2;
	  break;
	case SDT_AM_PM:
	  sprintf (buffer, "%s", p);
	  *token_size = strlen (p);
	  break;
	}

      return NO_ERROR;
    }
#endif

  /* determine length of token */
  token_bytes = strlen (p);
  intl_char_count ((unsigned char *) p, token_bytes, codeset, &token_len);

  if (case_mode == 2)
    {
      /* uppercase */
      intl_upper_string (&(lld->alphabet), (unsigned char *) p,
			 (unsigned char *) buffer, token_len);
      intl_char_size ((unsigned char *) buffer, token_len, codeset,
		      token_size);
    }
  else if (case_mode == 1)
    {
      /* lowercase */
      intl_lower_string (&(lld->alphabet), (unsigned char *) p,
			 (unsigned char *) buffer, token_len);
      intl_char_size ((unsigned char *) buffer, token_len, codeset,
		      token_size);
    }
  else
    {
      intl_char_size ((unsigned char *) p, token_len, codeset, token_size);
      memcpy (buffer, p, *token_size);
    }

  /* padding */
  if (token_len < print_len)
    {
      (void) qstr_pad_string ((unsigned char *) buffer + *token_size,
			      print_len - token_len, codeset);
      *token_size += intl_pad_size (codeset) * (print_len - token_len);
    }

  return NO_ERROR;
}

/*
 * convert_locale_number() - transforms a string containing a number in a
 *			     locale representation into another locale
 *   return: void
 *   sz(in/out): string to be transformed
 *   src_locale(in):
 *   dst_locale(in):
 *
 */
static void
convert_locale_number (char *sz, const int size,
		       const INTL_LANG src_locale, const INTL_LANG dst_locale)
{
  const char src_locale_group = lang_digit_grouping_symbol (src_locale);
  const char src_locale_frac = lang_digit_fractional_symbol (src_locale);

  const char dst_locale_group = lang_digit_grouping_symbol (dst_locale);
  const char dst_locale_frac = lang_digit_fractional_symbol (dst_locale);
  char *sz_end = sz + size;

  assert (src_locale != dst_locale);

  if (src_locale_group == dst_locale_group)
    {
      assert (src_locale_frac == dst_locale_frac);
      return;
    }

  assert (dst_locale_frac != dst_locale_group);

  for (; sz < sz_end && *sz != '\0'; sz++)
    {
      if (*sz == src_locale_group)
	{
	  *sz = dst_locale_group;
	}
      else if (*sz == src_locale_frac)
	{
	  *sz = dst_locale_frac;
	}
    }
}


/*
 * db_hex() - return hexadecimal representation
 *  returns: error code or NO_ERROR
 *   param(in): parameter to turn to hex
 *   result(out): varchar db_value with hex representation
 *
 * Note:
 *  If param is a generic string, the hex representation will be the
 *  concatenation of hex values of each byte.
 *  If param is a generic numeric, the hex representation will be that of
 *  param casted to bigint (64bit unsigned integer). if value exceeds UINT64
 *  capacity, the return value is 'FFFFFFFFFFFFFFFF'.
 */
int
db_hex (const DB_VALUE * param, DB_VALUE * result)
{
  /* String length limits for numeric values of param. When param is numeric,
   * it will be cast to BIGINT db type and then internally to UINT64.
   * hex_lenght_limits[i] is the upper limit of the closed set of integers
   * that can be represented in hex on i digits. */
  const UINT64 hex_length_limits[UINT64_MAX_HEX_DIGITS + 1] = {
    0x0, 0xF, 0xFF, 0xFFF, 0xFFFF,
    0xFFFFF, 0xFFFFFF, 0xFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFFF,
    0xFFFFFFFFFF, 0xFFFFFFFFFFF,
    0xFFFFFFFFFFFF, 0xFFFFFFFFFFFFF,
    0xFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFF,
    0xFFFFFFFFFFFFFFFF
  };

  /* hex digits */
  const char hex_digit[] = "0123456789ABCDEF";

  /* other variables */
  DB_TYPE param_type = DB_TYPE_UNKNOWN;
  char *str = NULL, *hexval = NULL;
  int str_size = 0, hexval_len = 0, i = 0, err = 0, error_code = NO_ERROR;

  /* check parameters for NULL values */
  if (param == NULL || result == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      goto error;
    }

  if (DB_IS_NULL (param))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* compute hex representation */
  param_type = DB_VALUE_DOMAIN_TYPE (param);

  if (TP_IS_CHAR_TYPE (param_type) || TP_IS_BIT_TYPE (param_type))
    {
      if (TP_IS_CHAR_TYPE (param_type))
	{
	  /* retrieve source string */
	  str = DB_PULL_STRING (param);
	  str_size = DB_GET_STRING_SIZE (param);

	  /* remove padding from end of string */
	  if (param_type == DB_TYPE_CHAR || param_type == DB_TYPE_NCHAR)
	    {
	      unsigned char pad_char[2];
	      int pad_char_size;
	      intl_pad_char (DB_GET_STRING_CODESET (param), pad_char,
			     &pad_char_size);

	      while (str_size >= pad_char_size
		     && memcmp (&(str[str_size - pad_char_size]),
				pad_char, pad_char_size) == 0)
		{
		  str_size -= pad_char_size;
		}
	    }
	}
      else
	{
	  /* get bytes of bitfield */
	  str = DB_PULL_BIT (param, &str_size);
	  str_size = QSTR_NUM_BYTES (str_size);
	}

      /* allocate hex string */
      hexval_len = str_size * 2;
      hexval = (char *) db_private_alloc (NULL, hexval_len + 1);
      if (hexval == NULL)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}
      hexval[hexval_len] = 0;

      /* compute hex representation */
      for (i = 0; i < str_size; i++)
	{
	  hexval[i * 2] = hex_digit[(str[i] >> 4) & 0xF];
	  hexval[i * 2 + 1] = hex_digit[str[i] & 0xF];
	}

      /* set return string */
      DB_MAKE_STRING (result, hexval);
      result->need_clear = true;
    }
  else if (TP_IS_NUMERIC_TYPE (param_type))
    {
      DB_VALUE param_db_bigint;
      TP_DOMAIN *domain, *param_domain;
      UINT64 param_bigint;

      /* try to convert to bigint */
      param_domain = tp_domain_resolve_default (param_type);
      domain = tp_domain_resolve_default (DB_TYPE_BIGINT);
      /* don't mind error code here, we need to know if param is
       * out of range */
      (void) tp_value_auto_cast (param, &param_db_bigint, domain);
      if (DB_IS_NULL (&param_db_bigint))
	{
	  /* param is out of range, set it to max */
	  param_bigint = hex_length_limits[UINT64_MAX_HEX_DIGITS];
	}
      else
	{
	  param_bigint = (UINT64) DB_GET_BIGINT (&param_db_bigint);
	}

      /* compute hex representation length */
      hexval_len = 1;
      while (param_bigint > hex_length_limits[hexval_len]
	     && hexval_len < UINT64_MAX_HEX_DIGITS)
	{
	  hexval_len++;
	}

      /* allocate memory */
      hexval = (char *) db_private_alloc (NULL, hexval_len + 1);
      if (hexval == NULL)
	{
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}
      hexval[hexval_len] = 0;

      /* compute hex representation */
      for (i = hexval_len - 1; i >= 0; --i)
	{
	  hexval[i] = hex_digit[param_bigint & 0xF];
	  param_bigint >>= 4;
	}

      /* set return string */
      DB_MAKE_STRING (result, hexval);
      result->need_clear = true;
    }
  else
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  /* all ok */
  return NO_ERROR;

error:
  if (result)
    {
      DB_MAKE_NULL (result);
    }
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  if (er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
    }
  return error_code;
}

/*
 * db_guid() - Generate a type 4 (randomly generated) UUID.
 *   return: error code or NO_ERROR
 *   thread_p(in): thread context
 *   result(out): HEX encoded UUID string
 * Note:
 */
int
db_guid (THREAD_ENTRY * thread_p, DB_VALUE * result)
{
  int i = 0, error_code = NO_ERROR;
  const char hex_digit[] = "0123456789ABCDEF";
  char guid_bytes[GUID_STANDARD_BYTES_LENGTH];
  char *guid_hex = NULL;

  if (result == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
      goto error;
    }

  DB_MAKE_NULL (result);

  /* Generate random bytes */
  error_code =
    crypt_generate_random_bytes (thread_p, guid_bytes,
				 GUID_STANDARD_BYTES_LENGTH);

  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Clear UUID version field */
  guid_bytes[6] &= 0x0F;
  /* Set UUID version according to UUID version 4 protocol */
  guid_bytes[6] |= 0x40;

  /* Clear variant field */
  guid_bytes[8] &= 0x3f;
  /* Set variant according to UUID version 4 protocol */
  guid_bytes[8] |= 0x80;

  guid_hex =
    (char *) db_private_alloc (thread_p, GUID_STANDARD_BYTES_LENGTH * 2 + 1);
  if (guid_hex == NULL)
    {
      error_code = er_errid ();
      goto error;
    }

  guid_hex[GUID_STANDARD_BYTES_LENGTH * 2] = '\0';

  /* Encode the bytes to HEX */
  for (i = 0; i < GUID_STANDARD_BYTES_LENGTH; i++)
    {
      guid_hex[i * 2] = hex_digit[(guid_bytes[i] >> 4) & 0xF];
      guid_hex[i * 2 + 1] = hex_digit[(guid_bytes[i] & 0xF)];
    }

  DB_MAKE_STRING (result, guid_hex);
  result->need_clear = true;

  return NO_ERROR;

error:
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      er_clear ();
      error_code = NO_ERROR;
    }

  return error_code;
}

/*
 * db_ascii() - return ASCII code of first character in string
 *  returns: error code or NO_ERROR
 *   param(in): string
 *   result(out): smallint db_value of ASCII code
 *
 * Note:
 *  If param is a zero-length string, result should be zero.
 *  If param is DB null, result should be DB null
 */
int
db_ascii (const DB_VALUE * param, DB_VALUE * result)
{
  /* other variables */
  DB_TYPE param_type = DB_TYPE_UNKNOWN;
  char *str = NULL;
  int str_size = 0, error_code = NO_ERROR;

  /* check parameters for NULL values */
  if (param == NULL || result == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      goto error;
    }

  if (DB_IS_NULL (param))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* get ASCII value */
  param_type = DB_VALUE_DOMAIN_TYPE (param);

  if (TP_IS_CHAR_TYPE (param_type))
    {
      /* get string and length */
      str = DB_PULL_STRING (param);
      str_size = DB_GET_STRING_SIZE (param);

      /* remove padding from end of string */
      if (param_type == DB_TYPE_CHAR || param_type == DB_TYPE_NCHAR)
	{
	  unsigned char pad_char[2];
	  int pad_char_size;
	  intl_pad_char (DB_GET_STRING_CODESET (param), pad_char,
			 &pad_char_size);

	  while (str_size >= pad_char_size
		 && memcmp (&(str[str_size - pad_char_size]), pad_char,
			    pad_char_size) == 0)
	    {
	      str_size -= pad_char_size;
	    }
	}

      /* return first character */
      if (str_size > 0)
	{
	  DB_MAKE_SMALLINT (result, (unsigned char) str[0]);
	}
      else
	{
	  DB_MAKE_SMALLINT (result, 0);
	}
    }
  else if (TP_IS_BIT_TYPE (param_type))
    {
      /* get bitfield as char array */
      str = DB_PULL_BIT (param, &str_size);

      /* return first byte */
      if (str_size > 0)
	{
	  DB_MAKE_SMALLINT (result, (unsigned char) str[0]);
	}
      else
	{
	  DB_MAKE_SMALLINT (result, 0);
	}
    }
  else
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  /* no error */
  return NO_ERROR;

error:
  if (result)
    {
      DB_MAKE_NULL (result);
    }
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      return NO_ERROR;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
  return error_code;
}


/*
 * db_conv() - convert number form one base to another
 *  returns: error code or NO_ERROR
 *   num(in): number to convert
 *   from_base(in): base of num
 *   to_base(in): base to convert num to
 *   result(out): string db_value with number in new base
 *
 * Note:
 *  From_base and to_base should satisfy 2 <= abs(base) <= 36
 */
int
db_conv (const DB_VALUE * num, const DB_VALUE * from_base,
	 const DB_VALUE * to_base, DB_VALUE * result)
{
  /* digit value lookup table vars */
  const unsigned char base_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  /* error handling vars */
  int error_code = NO_ERROR;

  /* type checking vars */
  DB_TYPE num_type = DB_TYPE_UNKNOWN;
  DB_TYPE from_base_type = DB_TYPE_UNKNOWN;
  DB_TYPE to_base_type = DB_TYPE_UNKNOWN;

  /* sign flags */
  bool num_is_signed = false, res_is_signed = false;
  bool res_has_minus = false;

  /* string representations of input number and result; size of buffer is
     maximum computable value in base 2 (64 digits) + sign (1 digit) + NULL
     terminator (1 byte) */
  unsigned char num_str[UINT64_MAX_BIN_DIGITS + 2] = { 0 };
  unsigned char res_str[UINT64_MAX_BIN_DIGITS + 2] = { 0 };
  char *num_p_str = (char *) num_str, *res_p_str = NULL;
  char *num_end_ptr = NULL;
  unsigned char swap = 0;
  int num_size = 0, res_size = 0;

  /* auxiliary variables */
  UINT64 base10 = 0;
  int from_base_int = 0, to_base_int = 0, i = 0;

  /* check parameters for NULL values */
  if (num == NULL || from_base == NULL || to_base == NULL || result == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      goto error;
    }

  if (DB_IS_NULL (num) || DB_IS_NULL (from_base) || DB_IS_NULL (to_base))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* type checking; do not check num_type here, we will do it later on */
  num_type = DB_VALUE_DOMAIN_TYPE (num);
  from_base_type = DB_VALUE_DOMAIN_TYPE (from_base);
  to_base_type = DB_VALUE_DOMAIN_TYPE (to_base);

  if (from_base_type != DB_TYPE_SMALLINT || to_base_type != DB_TYPE_SMALLINT)
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  /* from_base and to_base bounds checking */
  from_base_int = DB_GET_SMALLINT (from_base);
  to_base_int = DB_GET_SMALLINT (to_base);
  num_is_signed = (from_base_int < 0);
  res_is_signed = (to_base_int < 0);
  from_base_int = ABS (from_base_int);
  to_base_int = ABS (to_base_int);

  if (from_base_int < 2 || from_base_int > 36 || to_base_int < 2
      || to_base_int > 36)
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  /* compute input number string from either generic NUMERIC or generic
     STRING types */
  if (TP_IS_NUMERIC_TYPE (num_type))
    {
      /* generic number -> string */
      switch (num_type)
	{
	case DB_TYPE_SMALLINT:
	  snprintf (num_p_str, UINT64_MAX_BIN_DIGITS + 1, "%d",
		    DB_GET_SMALLINT (num));
	  break;

	case DB_TYPE_INTEGER:
	  snprintf (num_p_str, UINT64_MAX_BIN_DIGITS + 1, "%d",
		    DB_GET_INT (num));
	  break;

	case DB_TYPE_BIGINT:
	  snprintf (num_p_str, UINT64_MAX_BIN_DIGITS + 1, "%lld",
		    (long long int) DB_GET_BIGINT (num));
	  break;

	case DB_TYPE_NUMERIC:
	  num_p_str = numeric_db_value_print ((DB_VALUE *) num);
	  /* set the decimal point to '\0' to bypass end_ptr check, make it looks
	   * like we already trucated out the fractional part, as we do to float.
	   */
	  for (i = 0; num_p_str[i] != '\0'; ++i)
	    {
	      if (num_p_str[i] == '.')
		{
		  num_p_str[i] = '\0';
		  break;
		}
	    }
	  break;

	case DB_TYPE_FLOAT:
	  snprintf (num_p_str, UINT64_MAX_BIN_DIGITS + 1, "%.0f",
		    DB_GET_FLOAT (num));
	  break;

	case DB_TYPE_DOUBLE:
	  snprintf (num_p_str, UINT64_MAX_BIN_DIGITS + 1, "%.0f",
		    DB_GET_DOUBLE (num));
	  break;

	case DB_TYPE_MONETARY:
	  snprintf (num_p_str, UINT64_MAX_BIN_DIGITS + 1, "%.0f",
		    db_value_get_monetary_amount_as_double (num));
	  break;

	default:
	  DB_MAKE_NULL (result);
	  return NO_ERROR;
	}
    }
  else if (TP_IS_CHAR_TYPE (num_type))
    {
      /* copy into a null-terminated string */
      int str_size = DB_GET_STRING_SIZE (num);

      if (str_size >= 0)
	{
	  str_size = MIN (str_size, sizeof (num_str) - 1);
	}
      else
	{
	  str_size = sizeof (num_str) - 1;
	}
      strncpy (num_str, DB_PULL_STRING (num), str_size);
      num_str[str_size] = '\0';
      num_p_str = num_str;
      if (!is_str_valid_number (num_p_str, from_base_int))
	{
	  error_code = ER_OBJ_INVALID_ARGUMENTS;
	  goto error;
	}
    }
  else if (TP_IS_BIT_TYPE (num_type))
    {
      /* get raw bytes */
      num_p_str = DB_PULL_BIT (num, &num_size);
      num_size = QSTR_NUM_BYTES (num_size);

      /* convert to hex; NOTE: qstr_bin_to_hex returns number of converted
         bytes, not the size of the hex string; also, we convert at most 64
         digits even if we need only 16 in order to let strtoll handle
         overflow (weird stuff happens there ...) */
      num_size = qstr_bin_to_hex ((char *) num_str, UINT64_MAX_BIN_DIGITS,
				  num_p_str, num_size);
      num_str[num_size * 2] = '\0';

      /* set up variables for hex -> base10 conversion */
      num_p_str = (char *) num_str;
      from_base_int = 16;
      num_is_signed = false;
    }
  else
    {
      /* we cannot process the input in any way */
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  /* convert from string to INT64/UINT64 */
  errno = 0;
  if (num_is_signed)
    {
      base10 = (UINT64) strtoll (num_p_str, &num_end_ptr, from_base_int);
    }
  else
    {
      base10 = (UINT64) strtoull (num_p_str, &num_end_ptr, from_base_int);
    }

  if (errno != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE_OVERFLOW,
	      2, pr_type_name (num_type), pr_type_name (DB_TYPE_BIGINT));
      error_code = ER_TP_CANT_COERCE_OVERFLOW;
      goto error;
    }
  if (num_end_ptr != NULL && *num_end_ptr != '\0')
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      goto error;
    }

  /* compute signed part of number */
  if (res_is_signed && base10 > DB_BIGINT_MAX)
    {
      /* result should be signed and we DO have a negative INT64; compute
         complement and remember to add minus sign to string */
      base10 = ~base10;
      ++base10;
      res_has_minus = true;
    }

  /* convert base 10 -> to_base */
  if (base10 == 0)
    {
      /* number is zero? display it as such */
      res_str[res_size++] = '0';
      res_has_minus = false;
    }

  while (base10 > 0)
    {
      /* convert another digit */
      res_str[res_size++] = base_digits[base10 % to_base_int];
      base10 /= to_base_int;
    }

  if (res_has_minus)
    {
      /* add minus sign to number string */
      res_str[res_size++] = '-';
    }

  /* reverse string (duh!) */
  res_str[res_size] = 0;
  for (i = 0; i < res_size / 2; i++)
    {
      swap = res_str[i];
      res_str[i] = res_str[res_size - i - 1];
      res_str[res_size - i - 1] = swap;
    }

  /* return string */
  res_p_str = db_private_alloc (NULL, res_size + 1);
  if (res_p_str == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  memcpy (res_p_str, res_str, res_size + 1);
  DB_MAKE_STRING (result, res_p_str);
  result->need_clear = true;

  /* all ok */
  return NO_ERROR;

error:
  if (result)
    {
      DB_MAKE_NULL (result);
    }
  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      if (er_errid () != NO_ERROR)
	{
	  er_clear ();
	}
      return NO_ERROR;
    }

  if (er_errid () == NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
    }
  return error_code;
}

/*
 * is_str_valid_number() - check whether the given string is a valid number.
 *
 *   return: true if it's valid, false otherwise
 *   num_p(in): the number in string pointer
 *   base(in): from 2 to 36 inclusive
 *
 */
bool
is_str_valid_number (char *num_p, int base)
{
  char digit_max = (char) ('0' + base);
  char lower_char_max = (char) ('a' - 10 + base);
  char upper_char_max = (char) ('A' - 10 + base);
  bool has_decimal_point = false;

  /* skip leading space */
  for (; *num_p != '\0'; ++num_p)
    {
      if (!isspace (*num_p))
	{
	  break;
	}
    }

  /* just space, no digit */
  if (*num_p == '\0')
    {
      return false;
    }

  /* check number sign */
  if (*num_p == '-' || *num_p == '+')
    {
      ++num_p;
    }

  /* check base16 prefix '0x' */
  if (base == 16 && *num_p == '0'
      && (*(num_p + 1) == 'x' || *(num_p + 1) == 'X'))
    {
      num_p += 2;

      /* just space, no digit */
      if (*num_p == '\0')
	{
	  return false;
	}
    }

  /* check the digits */
  for (; *num_p != '\0'; ++num_p)
    {
      if (base < 10 && *num_p >= '0' && *num_p < digit_max)
	{
	  continue;
	}
      if (base >= 10 && *num_p >= '0' && *num_p <= '9')
	{
	  continue;
	}

      if (base > 10 && *num_p >= 'a' && *num_p < lower_char_max)
	{
	  continue;
	}
      if (base > 10 && *num_p >= 'A' && *num_p < upper_char_max)
	{
	  continue;
	}

      if (*num_p == '.' && !has_decimal_point)
	{
	  /* truncate out the fractional part */
	  *num_p = '\0';
	  has_decimal_point = true;
	  continue;
	}

      return false;
    }
  return true;
}

/*
 * init_builtin_calendar_names() - initializes builtin localizations for
 *				   calendar names
 *   return: void
 *   lld(in/out): locale data
 *
 */
void
init_builtin_calendar_names (LANG_LOCALE_DATA * lld)
{
  int i;

  assert (lld != NULL);

  if (lld->codeset == INTL_CODESET_UTF8)
    {
      for (i = 0; i < 7; i++)
	{
	  lld->day_short_name[i] = Short_Day_name_UTF8[lld->lang_id][i];
	  lld->day_name[i] = Day_name_UTF8[lld->lang_id][i];
	}

      for (i = 0; i < 12; i++)
	{
	  lld->month_short_name[i] = Short_Month_name_UTF8[lld->lang_id][i];
	  lld->month_name[i] = Month_name_UTF8[lld->lang_id][i];
	}

      for (i = 0; i < 12; i++)
	{
	  lld->am_pm[i] = Am_Pm_name_UTF8[lld->lang_id][i];
	}
    }
  else if (lld->codeset == INTL_CODESET_KSC5601_EUC)
    {
      for (i = 0; i < 7; i++)
	{
	  lld->day_short_name[i] = Short_Day_name_EUCKR[lld->lang_id][i];
	  lld->day_name[i] = Day_name_EUCKR[lld->lang_id][i];
	}

      for (i = 0; i < 12; i++)
	{
	  lld->month_short_name[i] = Short_Month_name_EUCKR[lld->lang_id][i];
	  lld->month_name[i] = Month_name_EUCKR[lld->lang_id][i];
	}

      for (i = 0; i < 12; i++)
	{
	  lld->am_pm[i] = Am_Pm_name_EUCKR[lld->lang_id][i];
	}
    }
  else
    {
      for (i = 0; i < 7; i++)
	{
	  lld->day_short_name[i] = Short_Day_name_ISO[lld->lang_id][i];
	  lld->day_name[i] = Day_name_ISO[lld->lang_id][i];
	}

      for (i = 0; i < 12; i++)
	{
	  lld->month_short_name[i] = Short_Month_name_ISO[lld->lang_id][i];
	  lld->month_name[i] = Month_name_ISO[lld->lang_id][i];
	}

      for (i = 0; i < 12; i++)
	{
	  lld->am_pm[i] = Am_Pm_name_ISO[lld->lang_id][i];
	}
    }

  lld->month_parse_order = Month_name_parse_order[lld->lang_id];
  lld->month_short_parse_order = Short_Month_name_parse_order[lld->lang_id];
  lld->day_parse_order = Day_name_parse_order[lld->lang_id];
  lld->day_short_parse_order = Short_Day_name_parse_order[lld->lang_id];
  lld->am_pm_parse_order = AM_PM_parse_order[lld->lang_id];
}

/*
 * db_value_to_enumeration_value () - convert a DB_VALUE to an enumeration
 *				      value ignoring out of range values
 * return : error code or NO_ERROR
 * src (in)	    : source value
 * result (in/out)  : enumeration value
 * enum_domain (in) : enumeration domain
 *
 * Note: The result value will hold the index of the enum if src has a
 * corespondent in the enumeration values or 0 otherwise.
 */
int
db_value_to_enumeration_value (const DB_VALUE * src, DB_VALUE * result,
			       const TP_DOMAIN * enum_domain)
{
  int error = NO_ERROR;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

  if (src == NULL || result == NULL || enum_domain == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  DB_MAKE_NULL (result);

  status = tp_value_cast (src, result, enum_domain, false);
  if (status != DOMAIN_COMPATIBLE)
    {
      if (status == DOMAIN_ERROR)
	{
	  return ER_FAILED;
	}
      DB_MAKE_ENUMERATION (result, DB_ENUM_OVERFLOW_VAL, NULL, 0,
			   TP_DOMAIN_CODESET (enum_domain),
			   TP_DOMAIN_COLLATION (enum_domain));
      er_clear ();
      /* continue, no error */
    }

  return NO_ERROR;
}


/*
 * db_inet_aton () - convert a string formatted IPv4 address
 *                    to a number formatted IPv4 address
 * Arguments:
 *  string (in)                 : source ip string
 *  result_numbered_ip (in/out) : result number
 *
 * Returns: int
 *	error code or NO_ERROR
 *
 * Errors:
 *      ER_OBJ_INVALID_ARGUMENTS
 *      ER_QSTR_INVALID_DATA_TYPE
 *      ER_OPFUNC_INET_NTOA_ARG
 *      ER_OUT_OF_VIRTUAL_MEMORY
 *
 * Note: the ip "226.000.000.037" is 226.0.0.31, not 226.0.0.37
 *       support "0x3d.037.12.25" format
 */
int
db_inet_aton (DB_VALUE * result_numbered_ip, const DB_VALUE * string)
{
  int error_code = NO_ERROR;
  DB_BIGINT numbered_ip = (DB_BIGINT) 0;
  char *ip_string = NULL;
  char *local_ipstring = NULL;
  char *local_ipslice = NULL;
  char *local_pivot = NULL;
  int slice = 0;
  int result = 0;
  const int ipsegmax = 256;
  DB_BIGINT ipbase;
  int slice_count = 0;
  int cnt;
  char *temp_tok;

  if (string == NULL || result_numbered_ip == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      goto error;
    }

  if (DB_IS_NULL (string))
    {
      DB_MAKE_NULL (result_numbered_ip);
      return NO_ERROR;
    }

  if (!is_char_string (string))
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  /* there is no need to check DB_GET_STRING_LENGTH
     or DB_GET_STRING_SIZE or cnt, we control ip format by ourselves */
  ip_string = DB_GET_CHAR (string, &cnt);
  local_ipstring = (char *) db_private_alloc (NULL, cnt + 1);
  if (local_ipstring == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  memcpy (local_ipstring, ip_string, cnt);
  local_ipstring[cnt] = '\0';

  ipbase = (DB_BIGINT) ipsegmax *ipsegmax * ipsegmax;
  for (temp_tok = local_ipstring;; temp_tok = NULL)
    {
      /* use ". \t" to be more tolerable of input format. */
      local_ipslice = strtok_r (temp_tok, ". \t", &local_pivot);
      if (local_ipslice == NULL)
	{
	  break;
	}

      if (!is_valid_ip_slice (local_ipslice))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OPFUNC_INET_ATON_ARG,
		  1, ip_string);
	  error_code = ER_OPFUNC_INET_ATON_ARG;
	  goto error;
	}

      result = parse_int (&slice, local_ipslice, 0);
      if (result != 0 || slice < 0 || slice >= ipsegmax)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OPFUNC_INET_ATON_ARG,
		  1, ip_string);
	  error_code = ER_OPFUNC_INET_ATON_ARG;
	  goto error;
	}
      numbered_ip += slice * ipbase;
      ipbase /= ipsegmax;
      slice_count++;
    }
  if (numbered_ip < 0
      || numbered_ip > (DB_BIGINT) ipsegmax * ipsegmax * ipsegmax * ipsegmax
      || slice_count != 4)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OPFUNC_INET_ATON_ARG,
	      1, ip_string);
      error_code = ER_OPFUNC_INET_ATON_ARG;
      goto error;
    }

  db_private_free (NULL, local_ipstring);
  DB_MAKE_BIGINT (result_numbered_ip, numbered_ip);
  return NO_ERROR;

error:
  if (local_ipstring != NULL)
    {
      db_private_free (NULL, local_ipstring);
    }
  DB_MAKE_NULL (result_numbered_ip);

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      er_clear ();
      return NO_ERROR;
    }
  return error_code;
}

/*
 * db_inet_ntoa () - convert a number formatted IPv4 address
 *                    to a string formatted IPv4 address
 * Arguments:
 *  number (in)               : source numbered ip
 *  result_ip_string (in/out) : result ip string
 *
 * Returns: int
 *	error code or NO_ERROR
 *
 * Errors:
 *      ER_QSTR_INVALID_DATA_TYPE
 *      ER_OBJ_INVALID_ARGUMENTS
 *      ER_OPFUNC_INET_NTOA_ARG
 *
 * Note:
 */
int
db_inet_ntoa (DB_VALUE * result_ip_string, const DB_VALUE * number)
{
  int error_code = NO_ERROR;
  DB_TYPE number_type = DB_TYPE_UNKNOWN;
  DB_BIGINT ip_number = 0;
  char ip_string[16] = { '\0' };
  char ip_seg_string[4] = { '\0' };
  const int ip_string_cnt = 16;
  const int ip_seg_string_cnt = 4;
  const DB_BIGINT ipmax = (DB_BIGINT) 256 * 256 * 256 * 256;
  const unsigned int ipv4_mask[] = { 0xFF000000, 0xFF0000, 0xFF00, 0xFF };
  const unsigned int ipfactor[] = { 256 * 256 * 256, 256 * 256, 256, 1 };
  unsigned int slice;
  int i;
  int ret_string_len;
  char *res_p_str;

  if (number == NULL || result_ip_string == NULL)
    {
      error_code = ER_OBJ_INVALID_ARGUMENTS;
      goto error;
    }

  if (DB_IS_NULL (number))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OPFUNC_INET_NTOA_ARG,
	      1, (long long int) ip_number);
      error_code = ER_OPFUNC_INET_NTOA_ARG;
      goto error;
    }

  number_type = DB_VALUE_DOMAIN_TYPE (number);
  if (number_type != DB_TYPE_BIGINT)
    {
      error_code = ER_QSTR_INVALID_DATA_TYPE;
      goto error;
    }

  ip_number = DB_GET_BIGINT (number);
  if (ip_number > ipmax || ip_number < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OPFUNC_INET_NTOA_ARG,
	      1, (long long int) ip_number);
      error_code = ER_OPFUNC_INET_NTOA_ARG;
      goto error;
    }

  for (i = 0; i < 4; i++)
    {
      slice = (ip_number & ipv4_mask[i]) / ipfactor[i];
      snprintf (ip_seg_string, ip_seg_string_cnt, "%u", slice);
      /* safe to use strcat rather than strncat */
      strcat (ip_string, ip_seg_string);
      if (i != 3)
	{
	  strcat (ip_string, ".");
	}
    }

  /* return string */
  ret_string_len = strlen (ip_string);
  res_p_str = db_private_alloc (NULL, ret_string_len + 1);
  if (res_p_str == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  memcpy (res_p_str, ip_string, ret_string_len + 1);
  DB_MAKE_STRING (result_ip_string, res_p_str);
  result_ip_string->need_clear = true;

  return NO_ERROR;

error:
  DB_MAKE_NULL (result_ip_string);

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      er_clear ();
      return NO_ERROR;
    }
  return error_code;
}

/*
 * is_valid_ip_slice () - check whether ip slice is valid
 *
 * Arguments:
 *  ipslice (in) : IP slice
 *
 * Returns: true or false
 */
static bool
is_valid_ip_slice (const char *ipslice)
{
  int pos = 0;
  int base_type = 10;		/* base type can be 8(oct), 10(dec), 16(hex) */

  assert (ipslice != NULL);
  if (ipslice[0] == '\0')
    {
      return false;
    }

  if (ipslice[0] == '0')
    {
      if (char_tolower (ipslice[1]) == 'x')
	{
	  if (ipslice[2] == '\0')
	    {
	      return false;
	    }
	  base_type = 16;
	  pos = 2;
	}
      else if (ipslice[1] != '\0')
	{
	  base_type = 8;
	  pos = 1;
	}
    }

  while (ipslice[pos] != '\0')
    {
      if (base_type == 10)
	{
	  if (!char_isdigit (ipslice[pos]))
	    {
	      return false;
	    }
	}
      else if (base_type == 8)
	{
	  if (!('0' <= ipslice[pos] && ipslice[pos] <= '7'))
	    {
	      return false;
	    }
	}
      else
	{			/* base_type = 16 */
	  if (!char_isxdigit (ipslice[pos]))
	    {
	      return false;
	    }
	}
      pos++;
    }

  return true;
}

/*
 * db_get_date_format () -
 * Returns: error number
 * format_str(in):
 * format(in/out):
 *
 */
int
db_get_date_format (const DB_VALUE * format_str, TIMESTAMP_FORMAT * format)
{
  char *fmt_str_ptr, *next_fmt_str_ptr, *last_fmt;
  INTL_CODESET codeset;
  char stack_buf_format[64];
  char *initial_buf_format = NULL;
  bool do_free_buf_format = false;
  int format_size;
  int error = NO_ERROR;

  assert (format_str != NULL);
  assert (format != NULL);

  if (DB_IS_NULL (format_str))
    {
      *format = DT_INVALID;
      goto end;
    }

  if (is_char_string (format_str) == false)
    {
      error = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      *format = DT_INVALID;
      goto end;
    }

  if (DB_GET_STRING_SIZE (format_str) == 0)
    {
      error = ER_QSTR_EMPTY_STRING;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      *format = DT_INVALID;
      goto end;
    }

  if (DB_GET_STRING_SIZE (format_str) > MAX_TOKEN_SIZE)
    {
      error = ER_QSTR_SRC_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      *format = DT_INVALID;
      goto end;
    }

  codeset = DB_GET_STRING_CODESET (format_str);

  error =
    db_check_or_create_null_term_string (format_str, stack_buf_format,
					 sizeof (stack_buf_format),
					 true, true,
					 &initial_buf_format,
					 &do_free_buf_format);
  if (error != NO_ERROR)
    {
      *format = DT_INVALID;
      goto end;
    }

  fmt_str_ptr = initial_buf_format;
  last_fmt = fmt_str_ptr + strlen (fmt_str_ptr);
  /* Skip space, tab, CR     */
  while (fmt_str_ptr < last_fmt && strchr (WHITE_CHARS, *fmt_str_ptr))
    {
      fmt_str_ptr++;
    }

  next_fmt_str_ptr = NULL;
  *format =
    get_next_format (fmt_str_ptr, codeset, DB_TYPE_DATETIME,
		     &format_size, &next_fmt_str_ptr);

  if (next_fmt_str_ptr != NULL && *next_fmt_str_ptr != 0)
    {
      error = ER_QSTR_INVALID_FORMAT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      *format = DT_INVALID;
      goto end;
    }

end:

  if (do_free_buf_format)
    {
      db_private_free (NULL, initial_buf_format);
    }

  return error;
}

/*
 * db_get_cs_coll_info() - get codeset or collation from a value
 *
 *   return: status
 *   result(out): result (string type)
 *   val(in): input value
 *   mode(in): 0 : get charset, 1 : get collation
 *
 */
int
db_get_cs_coll_info (DB_VALUE * result, const DB_VALUE * val, const int mode)
{
  int status = NO_ERROR;

  assert (result != NULL);
  assert (val != NULL);

  if (DB_IS_NULL (val))
    {
      DB_MAKE_NULL (result);
    }
  else if (!TP_TYPE_HAS_COLLATION (DB_VALUE_TYPE (val)))
    {
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  DB_MAKE_NULL (result);
	}
      else
	{
	  status = ER_OBJ_INVALID_ARGUMENTS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, status, 0);
	}
    }
  else
    {
      int cs, coll;

      if (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (val)))
	{
	  cs = DB_GET_STRING_CODESET (val);
	  coll = DB_GET_STRING_COLLATION (val);
	}
      else
	{
	  assert (DB_VALUE_TYPE (val) == DB_TYPE_ENUMERATION);
	  cs = DB_GET_ENUM_CODESET (val);
	  coll = DB_GET_ENUM_COLLATION (val);
	}

      if (mode == 0)
	{
	  DB_MAKE_STRING (result, lang_charset_cubrid_name (cs));
	}
      else
	{
	  assert (mode == 1);
	  DB_MAKE_STRING (result, lang_get_collation_name (coll));
	}
    }

  return status;
}

/*
 * db_string_index_prefix () -
 */
int
db_string_index_prefix (const DB_VALUE * string1,
			const DB_VALUE * string2,
			const DB_VALUE * index_type, DB_VALUE * prefix_index)
{
  int error_status = NO_ERROR;
  TP_DOMAIN key_domain;
  DB_VALUE db_cmp_res;
  int cmp_res;

  assert (string1 != (DB_VALUE *) NULL);
  assert (string2 != (DB_VALUE *) NULL);
  assert (index_type != (DB_VALUE *) NULL);

  if (DB_IS_NULL (string1) || DB_IS_NULL (string2) || DB_IS_NULL (index_type))
    {
      if (QSTR_IS_NATIONAL_CHAR (DB_VALUE_DOMAIN_TYPE (string1)))
	{
	  error_status =
	    db_value_domain_init (prefix_index, DB_TYPE_VARNCHAR,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else if (QSTR_IS_BIT (DB_VALUE_DOMAIN_TYPE (string1)))
	{
	  error_status =
	    db_value_domain_init (prefix_index, DB_TYPE_VARBIT,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else
	{
	  error_status =
	    db_value_domain_init (prefix_index, DB_TYPE_VARCHAR,
				  DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      return error_status;
    }

  if (!QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (string1))
      || !QSTR_IS_ANY_CHAR_OR_BIT (DB_VALUE_DOMAIN_TYPE (string2))
      || !is_char_string (index_type))
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  if ((qstr_get_category (string1) != qstr_get_category (string2))
      || (DB_GET_STRING_CODESET (string1) != DB_GET_STRING_CODESET (string2)))
    {
      error_status = ER_QSTR_INCOMPATIBLE_CODE_SETS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  key_domain.is_desc = false;
  if (strncasecmp (DB_PULL_STRING (index_type), "d", 1) == 0)
    {
      key_domain.is_desc = true;
    }

  error_status = db_string_compare (string1, string2, &db_cmp_res);
  if (error_status != NO_ERROR)
    {
      return error_status;
    }
  cmp_res = DB_GET_INT (&db_cmp_res);
  if ((key_domain.is_desc && cmp_res <= 0)
      || (!key_domain.is_desc && cmp_res >= 0))
    {
      error_status = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      return error_status;
    }

  error_status = db_string_unique_prefix (string1, string2, prefix_index,
					  &key_domain);

  return error_status;
}

/*
 *  db_string_to_base64 () - Using base64 to encode arbitrary input
 *   return: int(NO_ERROR if successful, other error status if fail)
 *   src(in):       source which holds plain-text string
 *   result(in/out): dest which holds encoded buffer
 *
 *   Note: handling of special cases:
 *         1. source string is NULL, result is NULL
 *         2. source string is empty string, result is empty string
 */
int
db_string_to_base64 (DB_VALUE const *src, DB_VALUE * result)
{
  int error_status, encode_len, src_len;
  const unsigned char *src_buf = NULL;
  unsigned char *encode_buf = NULL;
  DB_TYPE val_type;

  assert (src != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  error_status = NO_ERROR;

  if (DB_IS_NULL (src))
    {
      DB_MAKE_NULL (result);
      return error_status;
    }

  src_buf = (const unsigned char *) DB_PULL_STRING (src);

  /* length in bytes */
  src_len = DB_GET_STRING_SIZE (src);

  assert (src_len >= 0);

  /* if input is empty string, output is also empty string */
  if (src_len == 0)
    {
      db_string_make_empty_typed_string (NULL, result, DB_TYPE_VARCHAR, 0,
					 DB_GET_STRING_CODESET (src),
					 DB_GET_STRING_COLLATION (src));
      return NO_ERROR;
    }

  val_type = DB_VALUE_DOMAIN_TYPE (src);
  if (QSTR_IS_ANY_CHAR (val_type))
    {
      /* currently base64_encode always returns NO_ERROR except
       * for memory buffer allocation fail */
      error_status =
	base64_encode (src_buf, src_len, &encode_buf, &encode_len);

      if (error_status == NO_ERROR)
	{
	  qstr_make_typed_string (DB_TYPE_VARCHAR, result, encode_len,
				  encode_buf, encode_len,
				  DB_GET_STRING_CODESET (src),
				  DB_GET_STRING_COLLATION (src));

	  result->need_clear = true;

	  return NO_ERROR;
	}
    }
  else				/* val_type != QSTR_IS_ANY_CHAR  */
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;	/* reset error code */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
    }

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      DB_MAKE_NULL (result);
      er_clear ();		/* forget previous error */
      return NO_ERROR;
    }
  else
    {
      return error_status;
    }
}

/*
 *  db_string_from_base64 () - Convert a buffer into plain-text by base64 decoding
 *                             There is no assumption the input is base64 encoded,
 *                             in this case, result is NULL
 *   return:   int(NO_ERROR if successful, other error status if fail)
 *   src(in):       source which holds encoded buffer
 *   result(in/out): dest which holds plain-text string
 *
 *   Note: handling of special cases:
 *         1. source string is NULL, result is NULL
 *         2. source string is empty string, result is empty string
 *         3. source string contains invalid base64 encoded character,
 *            result is NULL
 *         4. source string has insufficient length even some bytes have been
 *            decoded, result is NULL
 */
int
db_string_from_base64 (DB_VALUE const *src, DB_VALUE * result)
{
  int error_status, err, decode_len, src_len;
  const unsigned char *src_buf = NULL;
  unsigned char *decode_buf = NULL;
  DB_TYPE val_type;

  assert (src != (DB_VALUE *) NULL);
  assert (result != (DB_VALUE *) NULL);

  error_status = NO_ERROR;

  /* source is NULL */
  if (DB_IS_NULL (src))
    {
      DB_MAKE_NULL (result);
      return NO_ERROR;
    }

  src_buf = (const unsigned char *) DB_PULL_STRING (src);

  /* length in bytes */
  src_len = DB_GET_STRING_SIZE (src);

  assert (src_len >= 0);

  /* source is empty string */
  if (src_len == 0)
    {
      db_string_make_empty_typed_string (NULL, result, DB_TYPE_VARCHAR, 0,
					 DB_GET_STRING_CODESET (src),
					 DB_GET_STRING_COLLATION (src));
      return NO_ERROR;
    }

  val_type = DB_VALUE_DOMAIN_TYPE (src);

  if (QSTR_IS_ANY_CHAR (val_type))
    {
      err = base64_decode (src_buf, src_len, &decode_buf, &decode_len);

      switch (err)
	{
	case BASE64_EMPTY_INPUT:
	  db_string_make_empty_typed_string (NULL, result, DB_TYPE_VARCHAR, 0,
					     DB_GET_STRING_CODESET (src),
					     DB_GET_STRING_COLLATION (src));
	  break;

	case NO_ERROR:
	  qstr_make_typed_string (DB_TYPE_VARCHAR, result, decode_len,
				  decode_buf, decode_len,
				  DB_GET_STRING_CODESET (src),
				  DB_GET_STRING_COLLATION (src));
	  result->need_clear = true;
	  break;

	case BASE64_INVALID_INPUT:
	  error_status = ER_QSTR_INVALID_FORMAT;	/* reset error code */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	  goto error_handling;

	default:
	  assert (er_errid () != NO_ERROR);
	  error_status = err;
	  goto error_handling;
	}
    }
  else				/* val_type != QSTR_IS_ANY_CHAR  */
    {
      error_status = ER_QSTR_INVALID_DATA_TYPE;	/* reset error code */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
      goto error_handling;
    }

  assert (error_status == NO_ERROR);
  return error_status;

error_handling:

  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
    {
      DB_MAKE_NULL (result);
      er_clear ();		/* forget previous error */
      return NO_ERROR;
    }
  else
    {
      return error_status;
    }
}
