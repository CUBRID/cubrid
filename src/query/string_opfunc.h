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
				    DB_VALUE * db_result, int is_reverse);
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

#if defined (ENABLE_UNUSED_FUNCTION)
extern int db_string_byte_length (const DB_VALUE * string,
				  DB_VALUE * byte_count);
extern int db_string_bit_length (const DB_VALUE * string,
				 DB_VALUE * bit_count);
extern int db_string_char_length (const DB_VALUE * string,
				  DB_VALUE * char_count);
#endif

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
extern int db_string_convert (const DB_VALUE * src_string,
			      DB_VALUE * dest_string);
extern int qstr_pad_size (INTL_CODESET codeset);
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
extern int db_months_between (const DB_VALUE * start_mon,
			      const DB_VALUE * end_mon,
			      DB_VALUE * result_mon);
extern int db_sys_date (DB_VALUE * result_date);
extern int db_sys_time (DB_VALUE * result_time);
extern int db_sys_timestamp (DB_VALUE * result_timestamp);
extern int db_sys_datetime (DB_VALUE * result_datetime);
extern int db_to_char (const DB_VALUE * src_value,
		       const DB_VALUE * format_str,
		       const DB_VALUE * lang_str, DB_VALUE * result_str);
extern int db_to_date (const DB_VALUE * src_str,
		       const DB_VALUE * format_str,
		       const DB_VALUE * date_lang, DB_VALUE * result_date);
extern int db_to_time (const DB_VALUE * src_str,
		       const DB_VALUE * format_str,
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

#endif /* _STRING_OPFUNC_H_ */
