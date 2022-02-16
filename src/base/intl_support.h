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
 * intl_support.h : internationalization support interfaces
 */

#ifndef _INTL_SUPPORT_H_
#define _INTL_SUPPORT_H_

#ident "$Id$"

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <limits.h>
#include <assert.h>

#if defined(AIX)
#include <ctype.h>
#elif defined(HPUX)
#include <wchar.h>
#elif defined(SOLARIS)
#include <wctype.h>
#include <wchar.h>
#include <widec.h>
#elif defined(LINUX)
#include <wchar.h>
#elif defined(WINDOWS)
#include <wchar.h>
#endif

#include "dbtype_def.h"
#include "locale_support.h"

#ifndef MB_LEN_MAX
#define MB_LEN_MAX            1
#endif

#ifndef WEOF
#define WEOF                  (wint_t)(-1)
#endif

#if defined(WINDOWS)
#if !defined(LC_MESSAGES)
#define LC_MESSAGES          LC_ALL
#endif /* !LC_MESSAGE */
#endif /* WINDOWS */

/* next UTF-8 char */
#define INTL_NEXTCHAR_UTF8(c) \
  (unsigned char*)((c) + intl_Len_utf8_char[*(unsigned char*)(c)])

/* next UTF-8 char and its length */
#define INTL_GET_NEXTCHAR_UTF8(c,l) { \
    l = intl_Len_utf8_char[*(unsigned char*)(c)]; \
    c += (l); \
  }

/* !!! Do not use this for BIT type !!! see STR_SIZE macro */
#define INTL_CODESET_MULT(codeset)                                         \
    (((codeset) == INTL_CODESET_UTF8) ? INTL_UTF8_MAX_CHAR_SIZE :	   \
     ((codeset) == INTL_CODESET_KSC5601_EUC) ? 3 : 1)

/* Checks if string having charset 'cs_from' can be safely reinterpreted as
 * having charset 'cs_to'.
 * All strings can be reinterpreted as Binary charset.
 * Other combinations are not compatible, since 8 bit values are starter for
 * mutibyte characters.
 */
#define INTL_CAN_STEAL_CS(cs_from,cs_to)  \
    ((cs_from) == (cs_to) || (cs_to) == INTL_CODESET_RAW_BYTES)

/* Checks if string having charset 'cs_from' can be coerced (transformed) as
 * having charset 'cs_to'.
 * All strings can be transformed to Binary charset by reinterpreting data
 * The other transformations require charset conversion.
 * In some cases, the destination charset may not contain an encoding of the
 * original character, and is replaced with '?' character (ASCII 3f)
 */
#define INTL_CAN_COERCE_CS(cs_from,cs_to)  true

#define INTL_NEXT_CHAR(ptr, s, codeset, current_char_size) \
  do \
    { \
      if (((codeset) == INTL_CODESET_ISO88591) \
       || ((codeset) == INTL_CODESET_RAW_BYTES)) \
	{ \
	  (*(current_char_size)) = 1; \
	  (ptr) = (s) + 1; \
	} \
      else if ((codeset) == INTL_CODESET_UTF8) \
	{ \
	  (ptr) = intl_nextchar_utf8 ((s), (current_char_size)); \
	} \
      else if ((codeset) == INTL_CODESET_KSC5601_EUC) \
	{ \
	  (ptr) = intl_nextchar_euc ((s), (current_char_size)); \
	} \
      else \
	{ \
	  assert (false); \
	  (*(current_char_size)) = 0; \
	  ptr = (s); \
	} \
    } \
  while (0)

extern bool intl_Mbs_support;
extern bool intl_String_validation;

/* language identifier : we support built-in languages and user defined
 * languages (through locale definition);
 * User defined languages are assigned IDs after built-in languages IDs
 * It is not guaranteed that user defined languages keep their IDs */
typedef unsigned int INTL_LANG;

enum intl_builtin_lang
{
  INTL_LANG_ENGLISH = 0,
  INTL_LANG_KOREAN,
  INTL_LANG_TURKISH,
  INTL_LANG_USER_DEF_START
};
typedef enum intl_builtin_lang INTL_BUILTIN_LANG;

enum intl_zone
{
  INTL_ZONE_US,
  INTL_ZONE_KR,
  INTL_ZONE_GB,
  INTL_ZONE_TR
};
typedef enum intl_zone INTL_ZONE;

enum currency_check_mode
{
  CURRENCY_CHECK_MODE_NONE = 0,
  CURRENCY_CHECK_MODE_CONSOLE = 0x1,
  CURRENCY_CHECK_MODE_UTF8 = 0x2,
  CURRENCY_CHECK_MODE_GRAMMAR = 0x4,
  CURRENCY_CHECK_MODE_ISO = 0x8,
  CURRENCY_CHECK_MODE_ESC_ISO = 0x10,
  CURRENCY_CHECK_MODE_ISO88591 = 0x16
};
typedef enum currency_check_mode CURRENCY_CHECK_MODE;

enum intl_utf8_validity
{
  INTL_UTF8_VALID,
  INTL_UTF8_INVALID,
  INTL_UTF8_TRUNCATED,
};
typedef enum intl_utf8_validity INTL_UTF8_VALIDITY;

/* map of lengths of UTF-8 characters */
extern const unsigned char *const intl_Len_utf8_char;

enum intl_codeset
{
  INTL_CODESET_ERROR = -2,
  INTL_CODESET_NONE = -1,
  INTL_CODESET_ASCII,		/* US English charset, ASCII encoding */
  INTL_CODESET_RAW_BITS,	/* Uninterpreted bits, Raw encoding */
  INTL_CODESET_RAW_BYTES,	/* Uninterpreted bytes, Raw encoding */
  INTL_CODESET_ISO88591,	/* Latin 1 charset, ISO 8859 encoding */
  INTL_CODESET_KSC5601_EUC,	/* KSC 5601 1990 charset , EUC encoding */
  INTL_CODESET_UTF8,		/* UNICODE charset, UTF-8 encoding */

  INTL_CODESET_BINARY = INTL_CODESET_RAW_BYTES,

  INTL_CODESET_LAST = INTL_CODESET_UTF8
};
typedef enum intl_codeset INTL_CODESET;

#ifdef __cplusplus
extern "C"
{
#endif
  extern int intl_char_count (const unsigned char *src, int length_in_bytes, INTL_CODESET src_codeset, int *char_count);
  extern int intl_char_size (const unsigned char *src, int length_in_chars, INTL_CODESET src_codeset, int *byte_count);

  extern int intl_tolower_iso8859 (unsigned char *s, int length);
  extern int intl_toupper_iso8859 (unsigned char *s, int length);

  extern const unsigned char *intl_nextchar_euc (const unsigned char *s, int *curr_length);
  extern const unsigned char *intl_prevchar_euc (const unsigned char *s, const unsigned char *s_start,
						 int *prev_length);
  extern const unsigned char *intl_nextchar_utf8 (const unsigned char *s, int *curr_length);
  extern const unsigned char *intl_prevchar_utf8 (const unsigned char *s, const unsigned char *s_start,
						  int *prev_length);

#if defined (ENABLE_UNUSED_FUNCTION)
  extern INTL_LANG intl_language (int category);
#endif				/* ENABLE_UNUSED_FUNCTION */
  extern INTL_ZONE intl_zone (int category);

  extern int intl_convert_charset (const unsigned char *src, int length_in_chars, INTL_CODESET src_codeset,
				   unsigned char *dest, INTL_CODESET dest_codeset, int *unconverted);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int intl_char_size_pseudo_kor (const unsigned char *src, int length_in_chars, INTL_CODESET src_codeset,
					int *byte_count);
#endif
  extern const unsigned char *intl_prev_char (const unsigned char *s, const unsigned char *s_start,
					      INTL_CODESET codeset, int *prev_char_size);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern unsigned char *intl_prev_char_pseudo_kor (const unsigned char *s, const unsigned char *s_start,
						   INTL_CODESET codeset, int *prev_char_size);
#endif
  extern const unsigned char *intl_next_char (const unsigned char *s, INTL_CODESET codeset, int *current_char_size);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern unsigned char *intl_next_char_pseudo_kor (const unsigned char *s, INTL_CODESET codeset,
						   int *current_char_size);
#endif
  extern int intl_cmp_char (const unsigned char *s1, const unsigned char *s2, INTL_CODESET codeset, int *char_size);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int intl_cmp_char_pseudo_kor (const unsigned char *s1, const unsigned char *s2, INTL_CODESET codeset,
				       int *char_size);
#endif
  extern void intl_pad_char (const INTL_CODESET codeset, unsigned char *pad_char, int *pad_size);
  extern int intl_pad_size (INTL_CODESET codeset);
  extern int intl_upper_string_size (const ALPHABET_DATA * alphabet, const unsigned char *src, int src_size,
				     int src_length);
  extern int intl_upper_string (const ALPHABET_DATA * alphabet, const unsigned char *src, unsigned char *dst,
				int length_in_chars);
  extern int intl_lower_string_size (const ALPHABET_DATA * alphabet, const unsigned char *src, int src_size,
				     int src_length);
  extern int intl_lower_string (const ALPHABET_DATA * alphabet, const unsigned char *src, unsigned char *dst,
				int length_in_chars);
  extern int intl_reverse_string (const unsigned char *src, unsigned char *dst, int length_in_chars, int size_in_bytes,
				  INTL_CODESET codeset);
  extern bool intl_is_max_bound_chr (INTL_CODESET codeset, const unsigned char *chr);
  extern bool intl_is_min_bound_chr (INTL_CODESET codeset, const unsigned char *chr);
  extern int intl_set_min_bound_chr (INTL_CODESET codeset, char *chr);
  extern int intl_set_max_bound_chr (INTL_CODESET codeset, char *chr);
  extern int intl_identifier_casecmp_w_size (const INTL_LANG lang_id, unsigned char *str1, unsigned char *str2,
					     const int size_str1, const int size_str2);
  extern int intl_case_match_tok (const INTL_LANG lang_id, const INTL_CODESET codeset, unsigned char *tok,
				  unsigned char *src, const int size_tok, const int size_src, int *matched_size_src);
  extern int intl_identifier_casecmp (const char *str1, const char *str2);
  extern int intl_identifier_ncasecmp (const char *str1, const char *str2, const int len);
  extern int intl_identifier_cmp (const char *str1, const char *str2);
#if defined(ENABLE_UNUSED_FUNCTION)
  extern int intl_identifier_namecmp (const char *str1, const char *str2);
#endif
  extern int intl_identifier_lower_string_size (const char *src);
  extern int intl_identifier_lower (const char *src, char *dst);
  extern int intl_identifier_upper_string_size (const char *src);
  extern int intl_identifier_upper (const char *src, char *dst);
  extern int intl_identifier_fix (char *name, int ident_max_size, bool error_on_case_overflow);
  extern unsigned int intl_identifier_mht_1strlowerhash (const void *key, const unsigned int ht_size);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int intl_strncat (unsigned char *dest, const unsigned char *src, int len);
#endif
  extern int intl_put_char (unsigned char *dest, const unsigned char *char_p, const INTL_CODESET codeset);
  extern bool intl_is_space (const char *str, const char *str_end, const INTL_CODESET codeset, int *space_size);
  extern const char *intl_skip_spaces (const char *str, const char *str_end, const INTL_CODESET codeset);
  extern const char *intl_backskip_spaces (const char *str_begin, const char *str_end, const INTL_CODESET codeset);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int intl_mbs_lower (const char *mbs1, char *mbs2);
  extern int intl_mbs_nlower (char *dest, const char *src, const int max_len);
  extern int intl_mbs_upper (const char *mbs1, char *mbs2);
#endif
  extern char *intl_mbs_chr (const char *mbs, wchar_t the_char);
  extern int intl_mbs_spn (const char *mbs, const wchar_t * chars);
  extern int intl_mbs_len (const char *mbs);

  extern const char *intl_mbs_nth (const char *mbs, size_t n);
  extern char *intl_mbs_ncpy (char *mbs1, const char *mbs2, size_t n);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int intl_mbs_namecmp (const char *mbs1, const char *mbs2);
#endif
  extern int intl_mbs_casecmp (const char *mbs1, const char *mbs2);
#if defined (ENABLE_UNUSED_FUNCTION)
  extern int intl_mbs_cmp (const char *mbs1, const char *mbs2);
#endif
  extern int intl_mbs_ncasecmp (const char *mbs1, const char *mbs2, size_t n);
  extern INTL_UTF8_VALIDITY intl_check_string (const char *buf, int size, char **pos, const INTL_CODESET codeset);
#if !defined (SERVER_MODE)
  extern bool intl_is_bom_magic (const char *buf, const int size);
#endif
  extern int intl_cp_to_utf8 (const unsigned int codepoint, unsigned char *utf8_seq);
  extern int intl_cp_to_dbcs (const unsigned int codepoint, const unsigned char *byte_flag, unsigned char *seq);
  extern unsigned int intl_utf8_to_cp (const unsigned char *utf8, const int size, unsigned char **next_char);
  extern unsigned int intl_back_utf8_to_cp (const unsigned char *utf8_start, const unsigned char *utf8_last,
					    unsigned char **last_byte__prev_char);
  extern unsigned int intl_dbcs_to_cp (const unsigned char *seq, const int size, const unsigned char *byte_flag,
				       unsigned char **next_char);
  extern int intl_utf8_to_cp_list (const unsigned char *utf8, const int size, unsigned int *cp_array,
				   const int max_array_size, int *array_count);
  extern int intl_text_single_byte_to_utf8 (const char *in_buf, const int in_size, char **out_buf, int *out_size);
  extern int intl_text_single_byte_to_utf8_ext (void *txt_conv, const unsigned char *in_buf, const int in_size,
						unsigned char **out_buf, int *out_size);
  extern int intl_text_utf8_to_single_byte (const char *in_buf, const int in_size, char **out_buf, int *out_size);
  extern int intl_text_dbcs_to_utf8 (const char *in_buf, const int in_size, char **out_buf, int *out_size);
  extern int intl_text_dbcs_to_utf8_ext (void *t, const unsigned char *in_buf, const int in_size,
					 unsigned char **out_buf, int *out_size);
  extern int intl_text_utf8_to_dbcs (const char *in_buf, const int in_size, char **out_buf, int *out_size);
  extern int intl_fast_iso88591_to_utf8 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
					 int *out_size);
  extern int intl_euckr_to_iso88591 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				     int *out_size);
  extern int intl_euckr_to_utf8 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				 int *out_size);
  extern int intl_utf8_to_euckr (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				 int *out_size);
  extern int intl_iso88591_to_utf8 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				    int *out_size);
  extern int intl_iso88591_to_euckr (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				     int *out_size);
  extern bool intl_is_currency_symbol (const char *src, DB_CURRENCY * currency, int *symbol_size,
				       const CURRENCY_CHECK_MODE check_mode);
  extern char *intl_get_money_symbol (const DB_CURRENCY currency, INTL_CODESET codeset);
  extern char *intl_get_money_ISO_symbol (const DB_CURRENCY currency);
  extern char *intl_get_money_esc_ISO_symbol (const DB_CURRENCY currency);
  extern char *intl_get_money_symbol_console (const DB_CURRENCY currency);
  extern char *intl_get_money_symbol_grammar (const DB_CURRENCY currency);
  extern char *intl_get_money_UTF8_symbol (const DB_CURRENCY currency);
  extern char *intl_get_money_ISO88591_symbol (const DB_CURRENCY currency);
  extern int intl_get_currency_symbol_position (const DB_CURRENCY currency);
  extern int intl_count_utf8_chars (const unsigned char *s, int length_in_bytes);
  extern INTL_UTF8_VALIDITY intl_check_utf8 (const unsigned char *buf, int size, char **pos);
  extern INTL_UTF8_VALIDITY intl_check_euckr (const unsigned char *buf, int size, char **pos);
  extern int intl_utf8_to_iso88591 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				    int *out_size);
  extern void intl_binary_to_utf8 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				   int *out_size);
  extern void intl_binary_to_euckr (const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				    int *out_size);
#ifdef __cplusplus
}
#endif

#endif				/* _INTL_SUPPORT_H_ */
