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
 * string_opfunc.h - Manipulate arbitrary strings
 */

#ifndef _STRING_OPFUNC_H_
#define _STRING_OPFUNC_H_

#ident "$Id$"

/* todo(rem) this doesn't belong to query module */

#include "config.h"
#include "dbtype_def.h"
#include "intl_support.h"
#include "language_support.h"
#include "numeric_opfunc.h"
#include "object_domain.h"
#include "thread_compat.hpp"

#ifdef  __cplusplus
#include <functional>
#include "string_regex.hpp"
#else
typedef struct cub_compiled_regex cub_compiled_regex;
#endif

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

#define QSTR_CHAR_COMPARE(id, string1, size1, string2, size2, ti) \
	QSTR_COMPARE(id, string1, size1, string2, size2, ti)

#define QSTR_NCHAR_COMPARE(id, string1, size1, string2, size2, codeset, ti) \
        QSTR_COMPARE(id, string1, size1, string2, size2, ti)

#define QSTR_COMPARE(id, string1, size1, string2, size2, ti) \
  (LANG_GET_COLLATION (id))->fastcmp ((LANG_GET_COLLATION (id)), (string1), \
				      (size1), (string2), (size2), ti)
#define QSTR_MATCH(id, string1, size1, string2, size2, esc, has_last_escape, \
		   match_size) \
  (LANG_GET_COLLATION (id))->strmatch ((LANG_GET_COLLATION (id)), true, \
				       (string1), (size1), \
				       (string2), (size2), (esc), \
				       (has_last_escape), (match_size), false)
#define QSTR_NEXT_ALPHA_CHAR(id, cur_chr, size, next_chr, len) \
  (LANG_GET_COLLATION (id))->next_coll_seq ((LANG_GET_COLLATION (id)), \
					(cur_chr), (size), (next_chr), (len), false)
#define QSTR_SPLIT_KEY(id, is_desc, str1, size1, str2, size2, k, s, ti) \
  (LANG_GET_COLLATION (id))->split_key ((LANG_GET_COLLATION (id)), is_desc, \
					(str1), (size1), (str2), (size2), \
					(k), (s), ti)


/*
 * These are the sizes for scratch buffers for formatting numbers, dates,
 * etc. inside db_value_get() and db_value_put().
 */
#define NUM_BUF_SIZE            64
#define TIME_BUF_SIZE           64
#define DATE_BUF_SIZE           64
#define TIMESTAMP_BUF_SIZE      (TIME_BUF_SIZE + DATE_BUF_SIZE)
#define DATETIME_BUF_SIZE       (TIMESTAMP_BUF_SIZE + 4)
#define TIMEZONE_BUF_SIZE	64
#define TIMESTAMPTZ_BUF_SIZE	(TIMESTAMP_BUF_SIZE + TIMEZONE_BUF_SIZE)
#define DATETIMETZ_BUF_SIZE	(DATETIME_BUF_SIZE + TIMEZONE_BUF_SIZE)

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

/*
 * Timestamp format
 */
typedef enum
{
  DT_END = -2,			/* format string end */
  DT_INVALID = -1,		/* invalid format */
  DT_NORMAL,
  DT_YYYY,
  DT_YY,
  DT_MM,
  DT_MONTH,
  DT_MON,
  DT_DD,
  DT_CC,
  DT_Q,
  DT_DAY,
  DT_DY,			/* So far, DATE format */
  DT_AM,
  DT_A_M,
  DT_PM,
  DT_P_M,
  DT_HH,
  DT_H,
  DT_HH12,
  DT_HH24,
  DT_MI,
  DT_SS,
  DT_MS,			/* So far, TIME format */
  DT_TEXT,
  DT_PUNCTUATION,
  DT_D,
  DT_TZR,
  DT_TZD,
  DT_TZH,
  DT_TZM
} TIMESTAMP_FORMAT;

#define  LIKE_WILDCARD_MATCH_MANY '%'
#define LIKE_WILDCARD_MATCH_ONE '_'

#define QSTR_IS_LIKE_WILDCARD_CHAR(ch)	((ch) == LIKE_WILDCARD_MATCH_ONE || \
					 (ch) == LIKE_WILDCARD_MATCH_MANY)

extern int qstr_compare (const unsigned char *string1, int size1, const unsigned char *string2, int size2);
extern int char_compare (const unsigned char *string1, int size1, const unsigned char *string2, int size2);
extern int varnchar_compare (const unsigned char *string1, int size1, const unsigned char *string2, int size2,
			     INTL_CODESET codeset);
extern int nchar_compare (const unsigned char *string1, int size1, const unsigned char *string2, int size2,
			  INTL_CODESET codeset);
extern int bit_compare (const unsigned char *string1, int size1, const unsigned char *string2, int size2);
extern int varbit_compare (const unsigned char *string1, int size1, const unsigned char *string2, int size2);
extern int get_last_day (int month, int year);
extern int get_day (int month, int day, int year);

extern int db_string_compare (const DB_VALUE * string1, const DB_VALUE * string2, DB_VALUE * result);
extern int db_string_unique_prefix (const DB_VALUE * db_string1, const DB_VALUE * db_string2, DB_VALUE * db_result,
				    TP_DOMAIN * key_domain);
extern int db_string_concatenate (const DB_VALUE * string1, const DB_VALUE * string2, DB_VALUE * result,
				  DB_DATA_STATUS * data_status);
extern int db_string_chr (DB_VALUE * res, DB_VALUE * dbval1, DB_VALUE * dbval2);
extern int db_string_instr (const DB_VALUE * src_string, const DB_VALUE * sub_string, const DB_VALUE * start_pos,
			    DB_VALUE * result);
extern int db_string_position (const DB_VALUE * sub_string, const DB_VALUE * src_string, DB_VALUE * result);
extern int db_string_substring (const MISC_OPERAND substr_operand, const DB_VALUE * src_string,
				const DB_VALUE * start_position, const DB_VALUE * extraction_length,
				DB_VALUE * sub_string);
extern int db_string_quote (const DB_VALUE * src_string, DB_VALUE * res);
extern int db_string_repeat (const DB_VALUE * src_string, const DB_VALUE * count, DB_VALUE * result);
extern int db_string_substring_index (DB_VALUE * src_string, DB_VALUE * delim_string, const DB_VALUE * count,
				      DB_VALUE * result);
extern int db_string_aes_encrypt (DB_VALUE const *src, DB_VALUE const *key, DB_VALUE * result);
extern int db_string_aes_decrypt (DB_VALUE const *src, DB_VALUE const *key, DB_VALUE * result);
extern int db_string_sha_one (DB_VALUE const *val, DB_VALUE * result);
extern int db_string_sha_two (DB_VALUE const *src, DB_VALUE const *len, DB_VALUE * result);
extern int db_string_md5 (DB_VALUE const *val, DB_VALUE * result);
extern int db_string_space (DB_VALUE const *count, DB_VALUE * result);
extern int db_string_insert_substring (DB_VALUE * src_string, const DB_VALUE * position, const DB_VALUE * length,
				       DB_VALUE * sub_string, DB_VALUE * result);
extern int db_string_elt (DB_VALUE * result, DB_VALUE * args[], int const num_args);
extern int db_string_escape_str (const char *src_str, size_t src_size, char **res_string, size_t * dest_size);

#if defined (ENABLE_UNUSED_FUNCTION)
extern int db_string_byte_length (const DB_VALUE * string, DB_VALUE * byte_count);
#endif
extern int db_string_bit_length (const DB_VALUE * string, DB_VALUE * bit_count);
extern int db_string_char_length (const DB_VALUE * string, DB_VALUE * char_count);

extern int db_string_lower (const DB_VALUE * string, DB_VALUE * lower_string);
extern int db_string_upper (const DB_VALUE * string, DB_VALUE * upper_string);
extern int db_string_trim (const MISC_OPERAND tr_operand, const DB_VALUE * trim_charset, const DB_VALUE * src_string,
			   DB_VALUE * trimmed_string);
extern int db_string_pad (const MISC_OPERAND pad_operand, const DB_VALUE * src_string, const DB_VALUE * pad_length,
			  const DB_VALUE * pad_charset, DB_VALUE * padded_string);
extern int db_string_like (const DB_VALUE * src_string, const DB_VALUE * pattern, const DB_VALUE * esc_char,
			   int *result);

//***********************************************************************************************
// Regular Expression Functions
//***********************************************************************************************
// using db_regex_func = std::function<int (DB_VALUE *, DB_VALUE*[], const int, cub_compiled_regex **, char **)>;
extern int db_string_rlike (const DB_VALUE * src_string, const DB_VALUE * pattern, const DB_VALUE * case_sensitive,
			    cub_compiled_regex ** comp_regex, int *result);
extern int db_string_regexp_count (DB_VALUE * result, DB_VALUE * args[], const int num_args,
				   cub_compiled_regex ** comp_regex);
extern int db_string_regexp_instr (DB_VALUE * result, DB_VALUE * args[], const int num_args,
				   cub_compiled_regex ** comp_regex);
extern int db_string_regexp_like (DB_VALUE * result, DB_VALUE * args[], const int num_args,
				  cub_compiled_regex ** comp_regex);
extern int db_string_regexp_replace (DB_VALUE * result, DB_VALUE * args[], const int num_args,
				     cub_compiled_regex ** comp_regex);
extern int db_string_regexp_substr (DB_VALUE * result, DB_VALUE * args[], const int num_args,
				    cub_compiled_regex ** comp_regex);
//***********************************************************************************************

extern int db_string_limit_size_string (DB_VALUE * src_string, DB_VALUE * result, const int new_size, int *spare_bytes);
extern int db_string_fix_string_size (DB_VALUE * src_string);
extern int db_string_replace (const DB_VALUE * src_string, const DB_VALUE * srch_string, const DB_VALUE * repl_string,
			      DB_VALUE * replaced_string);
extern int db_string_translate (const DB_VALUE * src_string, const DB_VALUE * from_string, const DB_VALUE * to_string,
				DB_VALUE * transed_string);
extern int db_bit_string_coerce (const DB_VALUE * src_string, DB_VALUE * dest_string, DB_DATA_STATUS * data_status);
extern int db_char_string_coerce (const DB_VALUE * src_string, DB_VALUE * dest_string, DB_DATA_STATUS * data_status);
extern int db_string_make_empty_typed_string (DB_VALUE * db_val, const DB_TYPE db_type, int precision, int codeset,
					      int collation_id);
extern int db_find_string_in_in_set (const DB_VALUE * needle, const DB_VALUE * stack, DB_VALUE * result);
extern int db_bigint_to_binary_string (const DB_VALUE * src_bigint, DB_VALUE * result);
extern int db_add_time (const DB_VALUE * left, const DB_VALUE * right, DB_VALUE * result, const TP_DOMAIN * domain);
extern int db_tz_offset (const DB_VALUE * src_str, DB_VALUE * result_str, DB_DATETIME * date_time);
extern int db_from_tz (DB_VALUE * time_val, DB_VALUE * tz, DB_VALUE * time_val_with_tz);
extern int db_new_time (DB_VALUE * time_val, DB_VALUE * tz_source, DB_VALUE * tz_dest, DB_VALUE * result_time);
extern int db_conv_tz (DB_VALUE * time_val, DB_VALUE * result_time);
extern int db_json_convert_to_utf8 (DB_VALUE * dbval);
extern int db_json_copy_and_convert_to_utf8 (const DB_VALUE * src_dbval, DB_VALUE * dest_dbval,
					     const DB_VALUE ** json_str_dbval);
extern int db_string_convert_to (const DB_VALUE * src_string, DB_VALUE * dest_string, INTL_CODESET dest_codeset,
				 int dest_col);

#if defined(ENABLE_UNUSED_FUNCTION)
extern int db_string_convert (const DB_VALUE * src_string, DB_VALUE * dest_string);
#endif
extern unsigned char *qstr_pad_string (unsigned char *s, int length, INTL_CODESET codeset);
extern int qstr_bin_to_hex (char *dest, int dest_size, const char *src, int src_size);
extern int qstr_hex_to_bin (char *dest, int dest_size, const char *src, int src_size);
extern int qstr_bit_to_bin (char *dest, int dest_size, const char *src, int src_size);
extern void qstr_bit_to_hex_coerce (char *buffer, int buffer_size, const char *src, int src_length, int pad_flag,
				    int *copy_size, int *truncation);
extern int db_get_string_length (const DB_VALUE * value);
extern void qstr_make_typed_string (const DB_TYPE db_type, DB_VALUE * value, const int precision, DB_CONST_C_CHAR src,
				    const int s_unit, const int codeset, const int collation_id);
extern int db_add_months (const DB_VALUE * src_date, const DB_VALUE * nmonth, DB_VALUE * result_date);
extern int db_last_day (const DB_VALUE * src_date, DB_VALUE * result_day);
extern int db_str_to_date (const DB_VALUE * src_date, const DB_VALUE * src_format, const DB_VALUE * date_lang,
			   DB_VALUE * result_date, TP_DOMAIN * domain);
extern int db_time_format (const DB_VALUE * src_time, const DB_VALUE * src_format, const DB_VALUE * date_lang,
			   DB_VALUE * result_time, const TP_DOMAIN * domain);
extern int db_timestamp (const DB_VALUE * src_datetime1, const DB_VALUE * src_time2, DB_VALUE * result_datetime);
extern int db_unix_timestamp (const DB_VALUE * src_date, DB_VALUE * result_timestamp);
extern int db_datetime_to_timestamp (const DB_VALUE * src_datetime, DB_VALUE * result_timestamp);
extern int db_months_between (const DB_VALUE * start_mon, const DB_VALUE * end_mon, DB_VALUE * result_mon);
extern int db_sys_date (DB_VALUE * result_date);
extern int db_sys_time (DB_VALUE * result_time);
extern int db_sys_timestamp (DB_VALUE * result_timestamp);
extern int db_sys_datetime (DB_VALUE * result_datetime);
extern int db_sys_date_and_epoch_time (DB_VALUE * dt_dbval, DB_VALUE * ts_dbval);
extern int db_sys_timezone (DB_VALUE * result_timezone);
extern int db_to_char (const DB_VALUE * src_value, const DB_VALUE * format_or_length, const DB_VALUE * lang_str,
		       DB_VALUE * result_str, const TP_DOMAIN * domain);
extern int db_to_date (const DB_VALUE * src_str, const DB_VALUE * format_str, const DB_VALUE * date_lang,
		       DB_VALUE * result_date);
extern int db_to_time (const DB_VALUE * src_str, const DB_VALUE * format_str, const DB_VALUE * date_lang,
		       const DB_TYPE type, DB_VALUE * result_time);
extern int db_to_timestamp (const DB_VALUE * src_str, const DB_VALUE * format_str, const DB_VALUE * date_lang,
			    const DB_TYPE type, DB_VALUE * result_timestamp);
extern int db_to_datetime (const DB_VALUE * src_str, const DB_VALUE * format_str, const DB_VALUE * date_lang,
			   const DB_TYPE type, DB_VALUE * result_datetime);
extern int db_to_number (const DB_VALUE * src_str, const DB_VALUE * format_str, const DB_VALUE * number_lang,
			 DB_VALUE * result_num);
extern int db_string_reverse (const DB_VALUE * src_str, DB_VALUE * result_str);
extern int db_format (const DB_VALUE * number_text, const DB_VALUE * decimals, const DB_VALUE * number_lang,
		      DB_VALUE * result, const TP_DOMAIN * domain);
/* datetime functions */
extern int db_date_add_interval_days (DB_VALUE * result, const DB_VALUE * date, const DB_VALUE * days);
extern int db_date_sub_interval_days (DB_VALUE * result, const DB_VALUE * date, const DB_VALUE * days);
extern int db_date_add_interval_expr (DB_VALUE * result, const DB_VALUE * date, const DB_VALUE * expr, const int unit);
extern int db_date_sub_interval_expr (DB_VALUE * result, const DB_VALUE * date, const DB_VALUE * expr, const int unit);
extern int db_date_format (const DB_VALUE * date_value, const DB_VALUE * format, const DB_VALUE * date_lang,
			   DB_VALUE * result, const TP_DOMAIN * domain);
extern int db_date_dbval (DB_VALUE * result, const DB_VALUE * date_value, const TP_DOMAIN * domain);
extern int db_time_dbval (DB_VALUE * result, const DB_VALUE * datetime_value, const TP_DOMAIN * domain);
extern int count_leap_years_up_to (int year);
extern int count_nonleap_years_up_to (int year);
extern int db_date_diff (const DB_VALUE * date_value1, const DB_VALUE * date_value2, DB_VALUE * result);
extern int db_from_unixtime (const DB_VALUE * src_date, const DB_VALUE * format, const DB_VALUE * date_lang,
			     DB_VALUE * result, const TP_DOMAIN * domain);
extern int db_time_diff (const DB_VALUE * datetime_value1, const DB_VALUE * datetime_value2, DB_VALUE * result);
extern int db_bit_to_blob (const DB_VALUE * src_value, DB_VALUE * result_value);
extern int db_char_to_blob (const DB_VALUE * src_value, DB_VALUE * result_value);
extern int db_blob_to_bit (const DB_VALUE * src_value, const DB_VALUE * length_value, DB_VALUE * result_value);
extern int db_blob_from_file (const DB_VALUE * src_value, DB_VALUE * result_value);
extern int db_blob_length (const DB_VALUE * src_value, DB_VALUE * result_value);
extern int db_char_to_clob (const DB_VALUE * src_value, DB_VALUE * result_value);
extern int db_clob_to_char (const DB_VALUE * src_value, const DB_VALUE * codeset_value, DB_VALUE * result_value);
extern int db_clob_from_file (const DB_VALUE * src_value, DB_VALUE * result_value);
extern int db_clob_length (const DB_VALUE * src_value, DB_VALUE * result_value);
extern int db_get_date_quarter (const DB_VALUE * src_date, DB_VALUE * result);
#if !defined (SERVER_MODE)
extern int db_get_date_weekday (const DB_VALUE * src_date, const int type, DB_VALUE * result);
#endif /* !defined (SERVER_MODE) */
extern int db_get_date_dayofyear (const DB_VALUE * src_date, DB_VALUE * result);
extern int db_get_date_totaldays (const DB_VALUE * src_date, DB_VALUE * result);
extern int db_convert_time_to_sec (const DB_VALUE * src_date, DB_VALUE * result);
extern int db_convert_sec_to_time (const DB_VALUE * src, DB_VALUE * result);
extern int db_get_date_from_days (const DB_VALUE * src, DB_VALUE * result);
extern int db_add_days_to_year (const DB_VALUE * src_year, const DB_VALUE * src_days, DB_VALUE * result);
extern int db_convert_to_time (const DB_VALUE * src_hour, const DB_VALUE * src_minute, const DB_VALUE * src_second,
			       DB_VALUE * result);
extern int db_get_date_week (const DB_VALUE * src_date, const DB_VALUE * mode, DB_VALUE * result);
#if !defined (SERVER_MODE)
extern int db_get_date_item (const DB_VALUE * src_date, const int item_type, DB_VALUE * result);
extern int db_get_time_item (const DB_VALUE * src_date, const int item_type, DB_VALUE * result);
#endif /* !defined (SERVER_MODE) */
#if defined(ENABLE_UNUSED_FUNCTION)
extern int db_null_terminate_string (const DB_VALUE * src_value, char **strp);
#endif

extern int db_get_info_for_like_optimization (const DB_VALUE * const pattern, const bool has_escape_char,
					      const char *escape_str, int *const num_logical_chars,
					      int *const last_safe_logical_pos, int *const num_match_many,
					      int *const num_match_one);
extern int db_compress_like_pattern (const DB_VALUE * const pattern, DB_VALUE * compressed_pattern,
				     const bool has_escape_char, const char *escape_str);
extern int db_get_like_optimization_bounds (const DB_VALUE * const pattern, DB_VALUE * bound,
					    const bool has_escape_char, const char *escape_str,
					    const bool compute_lower_bound, const int last_safe_logical_pos);
extern int db_like_bound (const DB_VALUE * const src_pattern, const DB_VALUE * const src_escape,
			  DB_VALUE * const result_bound, const bool compute_lower_bound);
extern int db_hex (const DB_VALUE * param, DB_VALUE * result);
#if !defined (CS_MODE)
/* todo(rem): this does not belong here */
extern int db_guid (THREAD_ENTRY * thread_p, DB_VALUE * result);
#endif /* !defined (CS_MODE) */
extern int db_ascii (const DB_VALUE * param, DB_VALUE * result);
extern int db_conv (const DB_VALUE * num, const DB_VALUE * from_base, const DB_VALUE * to_base, DB_VALUE * result);
extern void init_builtin_calendar_names (LANG_LOCALE_DATA * lld);
extern int db_value_to_enumeration_value (const DB_VALUE * src, DB_VALUE * result, const TP_DOMAIN * enum_domain);
extern int db_inet_aton (DB_VALUE * result_numbered_ip, const DB_VALUE * string);
extern int db_inet_ntoa (DB_VALUE * result_ip_string, const DB_VALUE * number);

extern void qstr_trim_trailing (const unsigned char *trim_charset_ptr, int trim_charset_size,
				const unsigned char *src_ptr, DB_TYPE src_type, int src_length, int src_size,
				INTL_CODESET codeset, int *trail_trimmed_length, int *trail_trimmed_size,
				bool skip_spaces);
extern int db_get_date_format (const DB_VALUE * format_str, TIMESTAMP_FORMAT * format);
extern int db_get_time_from_dbvalue (const DB_VALUE * src_date, int *hour, int *minute, int *second, int *millisecond);
extern int db_get_datetime_from_dbvalue (const DB_VALUE * src_date, int *year, int *month, int *day, int *hour,
					 int *minute, int *second, int *millisecond, const char **endp);
extern int db_get_cs_coll_info (DB_VALUE * result, const DB_VALUE * val, const int mode);
extern int db_string_index_prefix (const DB_VALUE * string1, const DB_VALUE * string2, const DB_VALUE * index_type,
				   DB_VALUE * prefix_index);
extern int db_string_to_base64 (DB_VALUE const *val, DB_VALUE * result);
extern int db_string_from_base64 (DB_VALUE const *val, DB_VALUE * result);
extern int db_string_extract_dbval (const MISC_OPERAND extr_operand, DB_VALUE * dbval_p, DB_VALUE * result_p,
				    TP_DOMAIN * domain_p);
#endif /* _STRING_OPFUNC_H_ */
