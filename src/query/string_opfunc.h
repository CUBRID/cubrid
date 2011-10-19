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
 * string_opfunc.h - Manipulate arbitrary strings
 */

#ifndef _STRING_OPFUNC_H_
#define _STRING_OPFUNC_H_

#ident "$Id$"

#include "config.h"

#include "intl_support.h"
#include "dbtype.h"
#include "numeric_opfunc.h"

#define QSTR_IS_CHAR(s)          (((s)==DB_TYPE_CHAR) || \
                                 ((s)==DB_TYPE_VARCHAR))
#define QSTR_IS_NATIONAL_CHAR(s) (((s)==DB_TYPE_NCHAR) || \
                                 ((s)==DB_TYPE_VARNCHAR))
#define QSTR_IS_BIT(s)           (((s)==DB_TYPE_BIT) || \
                                 ((s)==DB_TYPE_VARBIT))
#define QSTR_IS_ANY_CHAR(s)	(QSTR_IS_CHAR(s) || QSTR_IS_NATIONAL_CHAR(s))
#define QSTR_IS_ANY_CHAR_OR_BIT(s)		(QSTR_IS_ANY_CHAR(s) \
                                                 || QSTR_IS_BIT(s))

#define QSTR_IS_FIXED_LENGTH(s)         (((s)==DB_TYPE_CHAR)  || \
                                     ((s)==DB_TYPE_NCHAR) || \
                                     ((s)==DB_TYPE_BIT))

#define QSTR_IS_VARIABLE_LENGTH(s)      (((s)==DB_TYPE_VARCHAR)  || \
                                     ((s)==DB_TYPE_VARNCHAR) || \
                                     ((s)==DB_TYPE_VARBIT))

#define QSTR_NUM_BYTES(a)            (((a) + 7) / 8)

#define QSTR_CHAR_COMPARE(string1, size1, string2, size2) \
  (lang_locale ())->fastcmp ((string1), (size1), (string2), (size2))
#define QSTR_NCHAR_COMPARE(string1, size1, string2, size2, codeset) \
  (lang_locale ())->fastcmp ((string1), (size1), (string2), (size2))
#define QSTR_COMPARE(string1, size1, string2, size2) \
  (lang_locale ())->fastcmp ((string1), (size1), (string2), (size2))
#define QSTR_NEXT_ALPHA_CHAR(cur_chr, next_chr) \
  (lang_locale ())->next_alpha_char ((cur_chr), (next_chr))

/*
 * These are the sizes for scratch buffers for formatting numbers, dates,
 * etc. inside db_value_get() and db_value_put().
 */
#define NUM_BUF_SIZE            64
#define TIME_BUF_SIZE           64
#define DATE_BUF_SIZE           64
#define TIMESTAMP_BUF_SIZE      (TIME_BUF_SIZE + DATE_BUF_SIZE)
#define DATETIME_BUF_SIZE       (TIMESTAMP_BUF_SIZE + 4)

/*
 *  For the trim operation, db_string_trim(), this operand specifies
 *  that the trim character should be removed from the front, back
 *  or from both ends. In addition, this operand specifies an
 *  extract component (field) from the given datetime source.
 */
#define NUM_MISC_OPERANDS      12

typedef enum
{
  LEADING,			/* trim operand */
  TRAILING,
  BOTH,
  YEAR,				/* extract operand */
  MONTH,
  DAY,
  HOUR,
  MINUTE,
  SECOND,
  MILLISECOND,

  SUBSTRING,
  SUBSTR
} MISC_OPERAND;

#define  LIKE_WILDCARD_MATCH_MANY '%'
#define LIKE_WILDCARD_MATCH_ONE '_'

#define QSTR_IS_LIKE_WILDCARD_CHAR(ch)	((ch) == LIKE_WILDCARD_MATCH_ONE || \
					 (ch) == LIKE_WILDCARD_MATCH_MANY)

extern int qstr_compare (const unsigned char *string1, int size1,
			 const unsigned char *string2, int size2);
extern int char_compare (const unsigned char *string1, int size1,
			 const unsigned char *string2, int size2);
extern int varnchar_compare (const unsigned char *string1, int size1,
			     const unsigned char *string2, int size2,
			     INTL_CODESET codeset);
extern int nchar_compare (const unsigned char *string1, int size1,
			  const unsigned char *string2, int size2,
			  INTL_CODESET codeset);
extern int bit_compare (const unsigned char *string1, int size1,
			const unsigned char *string2, int size2);
extern int varbit_compare (const unsigned char *string1, int size1,
			   const unsigned char *string2, int size2);
extern int get_last_day (int month, int year);
extern int get_day (int month, int day, int year);
extern int db_string_compare (const DB_VALUE * string1,
			      const DB_VALUE * string2, DB_VALUE * result);
extern int db_string_unique_prefix (const DB_VALUE * db_string1,
				    const DB_VALUE * db_string2,
				    DB_VALUE * db_result, int is_reverse,
				    TP_DOMAIN * key_domain);
extern int db_string_concatenate (const DB_VALUE * string1,
				  const DB_VALUE * string2,
				  DB_VALUE * result,
				  DB_DATA_STATUS * data_status);
extern int db_string_chr (DB_VALUE * res, DB_VALUE * dbval1);
extern int db_string_instr (const DB_VALUE * src_string,
			    const DB_VALUE * sub_string,
			    const DB_VALUE * start_pos, DB_VALUE * result);
extern int db_string_position (const DB_VALUE * sub_string,
			       const DB_VALUE * src_string,
			       DB_VALUE * result);
extern int db_string_substring (const MISC_OPERAND substr_operand,
				const DB_VALUE * src_string,
				const DB_VALUE * start_position,
				const DB_VALUE * extraction_length,
				DB_VALUE * sub_string);
extern int db_string_repeat (const DB_VALUE * src_string,
			     const DB_VALUE * count, DB_VALUE * result);
extern int db_string_substring_index (DB_VALUE * src_string,
				      DB_VALUE * delim_string,
				      const DB_VALUE * count,
				      DB_VALUE * result);
extern int db_string_md5 (DB_VALUE const *val, DB_VALUE * result);
extern int db_string_space (DB_VALUE const *count, DB_VALUE * result);
extern int db_string_insert_substring (DB_VALUE * src_string,
				       const DB_VALUE * position,
				       const DB_VALUE * length,
				       DB_VALUE * sub_string,
				       DB_VALUE * result);
extern int db_string_elt (DB_VALUE * result,
			  DB_VALUE * args[], int const num_args);

#if defined (ENABLE_UNUSED_FUNCTION)
extern int db_string_byte_length (const DB_VALUE * string,
				  DB_VALUE * byte_count);
#endif
extern int db_string_bit_length (const DB_VALUE * string,
				 DB_VALUE * bit_count);
extern int db_string_char_length (const DB_VALUE * string,
				  DB_VALUE * char_count);

extern int db_string_lower (const DB_VALUE * string, DB_VALUE * lower_string);
extern int db_string_upper (const DB_VALUE * string, DB_VALUE * upper_string);
extern int db_string_trim (const MISC_OPERAND tr_operand,
			   const DB_VALUE * trim_charset,
			   const DB_VALUE * src_string,
			   DB_VALUE * trimmed_string);
extern int db_string_pad (const MISC_OPERAND pad_operand,
			  const DB_VALUE * src_string,
			  const DB_VALUE * pad_length,
			  const DB_VALUE * pad_charset,
			  DB_VALUE * padded_string);
extern int db_string_like (const DB_VALUE * src_string,
			   const DB_VALUE * pattern,
			   const DB_VALUE * esc_char, int *result);
extern int db_string_limit_size_string (DB_VALUE * src_string,
					DB_VALUE * result,
					const int new_size, int *spare_bytes);
extern int db_string_fix_string_size (DB_VALUE * src_string);
extern int db_string_replace (const DB_VALUE * src_string,
			      const DB_VALUE * srch_string,
			      const DB_VALUE * repl_string,
			      DB_VALUE * replaced_string);
extern int db_string_translate (const DB_VALUE * src_string,
				const DB_VALUE * from_string,
				const DB_VALUE * to_string,
				DB_VALUE * transed_string);
extern int db_bit_string_coerce (const DB_VALUE * src_string,
				 DB_VALUE * dest_string,
				 DB_DATA_STATUS * data_status);
extern int db_char_string_coerce (const DB_VALUE * src_string,
				  DB_VALUE * dest_string,
				  DB_DATA_STATUS * data_status);
extern int db_string_make_empty_typed_string (THREAD_ENTRY * thread_p,
					      DB_VALUE * db_val,
					      const DB_TYPE db_type,
					      int precision);
extern int db_find_string_in_in_set (const DB_VALUE * needle,
				     const DB_VALUE * stack,
				     DB_VALUE * result);
extern int db_bigint_to_binary_string (const DB_VALUE * src_bigint,
				       DB_VALUE * result);
extern int db_add_time (const DB_VALUE * left, const DB_VALUE * right,
			DB_VALUE * result, const TP_DOMAIN * domain);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int db_string_convert (const DB_VALUE * src_string,
			      DB_VALUE * dest_string);
#endif
extern unsigned char *qstr_pad_string (unsigned char *s, int length,
				       INTL_CODESET codeset);
extern int qstr_bin_to_hex (char *dest, int dest_size, const char *src,
			    int src_size);
extern int qstr_hex_to_bin (char *dest, int dest_size, char *src,
			    int src_size);
extern int qstr_bit_to_bin (char *dest, int dest_size, char *src,
			    int src_size);
extern void qstr_bit_to_hex_coerce (char *buffer, int buffer_size,
				    const char *src, int src_length,
				    int pad_flag, int *copy_size,
				    int *truncation);
extern int db_get_string_length (const DB_VALUE * value);
extern void qstr_make_typed_string (DB_TYPE domain, DB_VALUE * value,
				    int precision, const DB_C_CHAR src,
				    const int s_unit);
extern int db_add_months (const DB_VALUE * src_date,
			  const DB_VALUE * nmonth, DB_VALUE * result_date);
extern int db_last_day (const DB_VALUE * src_date, DB_VALUE * result_day);
extern int db_str_to_date (const DB_VALUE * src_date,
			   const DB_VALUE * src_format,
			   DB_VALUE * result_date, TP_DOMAIN * domain);
extern int db_time_format (const DB_VALUE * src_time,
			   const DB_VALUE * src_format,
			   DB_VALUE * result_time);
extern int db_timestamp (const DB_VALUE * src_datetime1,
			 const DB_VALUE * src_time2,
			 DB_VALUE * result_datetime);
extern int db_unix_timestamp (const DB_VALUE * src_date,
			      DB_VALUE * result_timestamp);
extern int db_months_between (const DB_VALUE * start_mon,
			      const DB_VALUE * end_mon,
			      DB_VALUE * result_mon);
extern int db_sys_date (DB_VALUE * result_date);
extern int db_sys_time (DB_VALUE * result_time);
extern int db_sys_timestamp (DB_VALUE * result_timestamp);
extern int db_sys_datetime (DB_VALUE * result_datetime);
extern int db_sys_timezone (DB_VALUE * result_timezone);
extern int db_to_char (const DB_VALUE * src_value,
		       const DB_VALUE * format_or_length,
		       const DB_VALUE * lang_str, DB_VALUE * result_str);
extern int db_to_date (const DB_VALUE * src_str,
		       const DB_VALUE * format_str,
		       const DB_VALUE * date_lang, DB_VALUE * result_date);
extern int db_to_time (const DB_VALUE * src_str, const DB_VALUE * format_str,
		       const DB_VALUE * date_lang, DB_VALUE * result_time);
extern int db_to_timestamp (const DB_VALUE * src_str,
			    const DB_VALUE * format_str,
			    const DB_VALUE * date_lang,
			    DB_VALUE * result_timestamp);
extern int db_to_datetime (const DB_VALUE * src_str,
			   const DB_VALUE * format_str,
			   const DB_VALUE * date_lang,
			   DB_VALUE * result_datetime);
extern int db_to_number (const DB_VALUE * src_str,
			 const DB_VALUE * format_str, DB_VALUE * result_num);
extern int db_string_reverse (const DB_VALUE * src_str,
			      DB_VALUE * result_str);
extern int db_format (const DB_VALUE * number_text, const DB_VALUE * decimals,
		      DB_VALUE * result);
/* datetime functions */
extern int db_date_add_interval_days (DB_VALUE * result,
				      const DB_VALUE * date,
				      const DB_VALUE * days);
extern int db_date_add_interval_expr (DB_VALUE * result,
				      const DB_VALUE * date,
				      const DB_VALUE * expr, const int unit);
extern int db_date_sub_interval_days (DB_VALUE * result,
				      const DB_VALUE * date,
				      const DB_VALUE * days);
extern int db_date_sub_interval_expr (DB_VALUE * result,
				      const DB_VALUE * date,
				      const DB_VALUE * expr, const int unit);
extern int db_date_format (const DB_VALUE * date_value,
			   const DB_VALUE * format, DB_VALUE * result);
extern int db_date_dbval (DB_VALUE * result, const DB_VALUE * date_value);
extern int db_time_dbval (DB_VALUE * result, const DB_VALUE * datetime_value);
extern int count_leap_years_up_to (int year);
extern int count_nonleap_years_up_to (int year);
extern int db_date_diff (const DB_VALUE * date_value1,
			 const DB_VALUE * date_value2, DB_VALUE * result);
extern int db_from_unixtime (const DB_VALUE * src_date,
			     const DB_VALUE * format, DB_VALUE * result);
extern int db_time_diff (const DB_VALUE * datetime_value1,
			 const DB_VALUE * datetime_value2, DB_VALUE * result);
extern int db_bit_to_blob (const DB_VALUE * src_value,
			   DB_VALUE * result_value);
extern int db_char_to_blob (const DB_VALUE * src_value,
			    DB_VALUE * result_value);
extern int db_blob_to_bit (const DB_VALUE * src_value,
			   const DB_VALUE * length_value,
			   DB_VALUE * result_value);
extern int db_blob_from_file (const DB_VALUE * src_value,
			      DB_VALUE * result_value);
extern int db_blob_length (const DB_VALUE * src_value,
			   DB_VALUE * result_value);
extern int db_char_to_clob (const DB_VALUE * src_value,
			    DB_VALUE * result_value);
extern int db_clob_to_char (const DB_VALUE * src_value,
			    const DB_VALUE * length_value,
			    DB_VALUE * result_value);
extern int db_clob_from_file (const DB_VALUE * src_value,
			      DB_VALUE * result_value);
extern int db_clob_length (const DB_VALUE * src_value,
			   DB_VALUE * result_value);
extern int db_get_date_quarter (const DB_VALUE * src_date, DB_VALUE * result);
extern int db_get_date_weekday (const DB_VALUE * src_date, const int type,
				DB_VALUE * result);
extern int db_get_date_dayofyear (const DB_VALUE * src_date,
				  DB_VALUE * result);
extern int db_get_date_totaldays (const DB_VALUE * src_date,
				  DB_VALUE * result);
extern int db_convert_time_to_sec (const DB_VALUE * src_date,
				   DB_VALUE * result);
extern int db_convert_sec_to_time (const DB_VALUE * src, DB_VALUE * result);
extern int db_get_date_from_days (const DB_VALUE * src, DB_VALUE * result);
extern int db_add_days_to_year (const DB_VALUE * src_year,
				const DB_VALUE * src_days, DB_VALUE * result);
extern int db_convert_to_time (const DB_VALUE * src_hour,
			       const DB_VALUE * src_minute,
			       const DB_VALUE * src_second,
			       DB_VALUE * result);
extern int db_get_date_week (const DB_VALUE * src_date, const DB_VALUE * mode,
			     DB_VALUE * result);
extern int db_get_date_item (const DB_VALUE * src_date, const int item_type,
			     DB_VALUE * result);
extern int db_get_time_item (const DB_VALUE * src_date, const int item_type,
			     DB_VALUE * result);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int db_null_terminate_string (const DB_VALUE * src_value, char **strp);
#endif

extern int db_get_info_for_like_optimization (const DB_VALUE * const pattern,
					      const bool has_escape_char,
					      const char *escape_str,
					      int *const num_logical_chars,
					      int *const
					      last_safe_logical_pos,
					      int *const num_match_many,
					      int *const num_match_one);
extern int db_compress_like_pattern (const DB_VALUE * const pattern,
				     DB_VALUE * compressed_pattern,
				     const bool has_escape_char,
				     const char *escape_str);
extern int db_get_like_optimization_bounds (const DB_VALUE * const pattern,
					    DB_VALUE * bound,
					    const bool has_escape_char,
					    const char *escape_str,
					    const bool compute_lower_bound,
					    const int last_safe_logical_pos);
extern int db_like_bound (const DB_VALUE * const src_pattern,
			  const DB_VALUE * const src_escape,
			  DB_VALUE * const result_bound,
			  const bool compute_lower_bound);
#endif /* _STRING_OPFUNC_H_ */
