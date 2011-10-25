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
 * intl_support.h : internationalization support interfaces 
 */

#ifndef _INTL_SUPPORT_H_
#define _INTL_SUPPORT_H_

#ident "$Id$"

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <limits.h>

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

#include "dbtype.h"

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

/* maximum char size for UTF-8 */
#define INTL_UTF8_MAX_CHAR_SIZE	4
/* max multiplier for char size when performing lower / upper case on 
 * DB identifiers . Do not use for user string : see UTF-8 turkish for 
 * 'i' and 'I' */
#define INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER	1

/* next UTF-8 char */
#define INTL_NEXTCHAR_UTF8(c) \
  (unsigned char*)((c) + intl_Len_utf8_char[*(unsigned char*)(c)])

/* previous UTF-8 char */
#define INTL_PREVCHAR_UTF8(c) \
  (*((unsigned char*)(c)-1) & 0xc0) != 0x80 ? (unsigned char*)(c)-1 : \
    (*((unsigned char*)(c)-2) & 0xc0) != 0x80 ? (unsigned char*)(c)-2 : \
      (*((unsigned char*)(c)-3) & 0xc0) != 0x80 ? (unsigned char*)(c)-3 : \
	(*((unsigned char*)(c)-4) & 0xc0) != 0x80 ? (unsigned char*)(c)-4 : \
	  (*((unsigned char*)(c)-5) & 0xc0) != 0x80 ? (unsigned char*)(c)-5 : \
	    (*((unsigned char*)(c)-6) & 0xc0) != 0x80 ? (unsigned char*)(c)-6 : \
	      (unsigned char*)(c)-1

/* next UTF-8 char and its length */
#define INTL_GET_NEXTCHAR_UTF8(c,l) { \
    l = intl_Len_utf8_char[*(unsigned char*)(c)]; \
    c += (l); \
  }

/* previous UTF-8 char and its length */
#define INTL_GET_PREVCHAR_UTF8(c,l) { \
    l = 0; \
    do { \
      (l)++; \
    } while ((*((unsigned char*)(c)-l) & 0xc0) == 0x80 && (l) < 6); \
    l = (*((unsigned char*)(c)-l) & 0xc0) == 0x80 ? 1 : (l); \
    c -= (l); \
  }

extern bool intl_Mbs_support;
extern bool intl_String_validation;

typedef enum intl_lang INTL_LANG;
enum intl_lang
{
  INTL_LANG_ENGLISH,
  INTL_LANG_KOREAN,
  INTL_LANG_TURKISH
};

typedef enum intl_zone INTL_ZONE;
enum intl_zone
{
  INTL_ZONE_US,
  INTL_ZONE_KR,
  INTL_ZONE_GB,
  INTL_ZONE_TR
};

typedef enum intl_codeset INTL_CODESET;
enum intl_codeset
{
  INTL_CODESET_NONE = -1,
  INTL_CODESET_ASCII,		/* US English charset, ASCII encoding */
  INTL_CODESET_RAW_BITS,	/* Uninterpreted bits, Raw encoding */
  INTL_CODESET_RAW_BYTES,	/* Uninterpreted bytes, Raw encoding */
  INTL_CODESET_ISO88591,	/* Latin 1 charset, ISO 8859 encoding */
  INTL_CODESET_KSC5601_EUC,	/* KSC 5601 1990 charset , EUC encoding */
  INTL_CODESET_UTF8		/* UNICODE charset, UTF-8 encoding */
};

typedef enum currency_check_mode CURRENCY_CHECK_MODE;
enum currency_check_mode
{
  CURRENCY_CHECK_MODE_NONE = 0,
  CURRENCY_CHECK_MODE_CONSOLE = 0x1,
  CURRENCY_CHECK_MODE_UTF8 = 0x2,
  CURRENCY_CHECK_MODE_GRAMMAR = 0x4,
  CURRENCY_CHECK_MODE_ISO = 0x5
};

/* map of lengths of UTF-8 characters */
extern const unsigned char *const intl_Len_utf8_char;

#ifdef __cplusplus
extern "C"
{
#endif

  extern unsigned char *intl_nextchar_euc (unsigned char *s,
					   int *curr_length);
  extern unsigned char *intl_prevchar_euc (unsigned char *s,
					   int *prev_length);
  extern unsigned char *intl_nextchar_utf8 (unsigned char *s,
					    int *curr_length);
  extern unsigned char *intl_prevchar_utf8 (unsigned char *s,
					    int *prev_length);

#if defined (ENABLE_UNUSED_FUNCTION)
  extern INTL_LANG intl_language (int category);
#endif				/* ENABLE_UNUSED_FUNCTION */
  extern INTL_ZONE intl_zone (const INTL_LANG lang_id);

  extern int intl_convert_charset (unsigned char *src,
				   int length_in_chars,
				   INTL_CODESET src_codeset,
				   unsigned char *dest,
				   INTL_CODESET dest_codeset,
				   int *unconverted);
  extern int intl_char_count (unsigned char *src, int length_in_bytes,
			      INTL_CODESET src_codeset, int *char_count);
  extern int intl_char_size (unsigned char *src, int length_in_chars,
			     INTL_CODESET src_codeset, int *byte_count);
  extern int intl_char_size_pseudo_kor (unsigned char *src,
					int length_in_chars,
					INTL_CODESET src_codeset,
					int *byte_count);
  extern unsigned char *intl_prev_char (unsigned char *s,
					INTL_CODESET codeset,
					int *prev_char_size);
  extern unsigned char *intl_prev_char_pseudo_kor (unsigned char *s,
						   INTL_CODESET codeset,
						   int *prev_char_size);
  extern unsigned char *intl_next_char (unsigned char *s,
					INTL_CODESET codeset,
					int *current_char_size);
  extern unsigned char *intl_next_char_pseudo_kor (unsigned char *s,
						   INTL_CODESET codeset,
						   int *current_char_size);
  extern int intl_cmp_char (const unsigned char *s1, const unsigned char *s2,
			    INTL_CODESET codeset, int *char_size);
  extern int intl_cmp_char_pseudo_kor (unsigned char *s1, unsigned char *s2,
				       INTL_CODESET codeset, int *char_size);
  extern void intl_pad_char (const INTL_CODESET codeset,
			     unsigned char *pad_char, int *pad_size);
  extern int intl_pad_size (INTL_CODESET codeset);
  extern int intl_upper_string_size (unsigned char *src, int src_size,
				     int src_length,
				     INTL_CODESET src_codeset);
  extern int intl_upper_string (unsigned char *src, unsigned char *dst,
				int length_in_chars,
				INTL_CODESET src_codeset);
  extern int intl_lower_string_size (unsigned char *src, int src_size,
				     int src_length,
				     INTL_CODESET src_codeset);
  extern int intl_lower_string (unsigned char *src, unsigned char *dst,
				int length_in_chars,
				INTL_CODESET src_codeset);
  extern int intl_reverse_string (unsigned char *src, unsigned char *dst,
				  int length_in_chars, int size_in_bytes,
				  INTL_CODESET codeset);
  extern int intl_strcmp_utf8 (const unsigned char *str1, const int size1,
			       const unsigned char *str2, const int size2);
  extern int intl_next_alpha_char_utf8 (const unsigned char *cur_char,
					unsigned char *next_char);
  extern int intl_strcasecmp (const INTL_LANG lang_id, unsigned char *str1,
			      unsigned char *str2, int len,
			      bool identifier_mode);
  extern int intl_strcasecmp_w_size (const INTL_LANG lang_id,
				     unsigned char *str1, unsigned char *str2,
				     const int size_str1, const int size_str2,
				     bool identifier_mode);
  extern int intl_identifier_casecmp (const char *str1, const char *str2);
  extern int intl_identifier_ncasecmp (const char *str1, const char *str2,
				       const int len);
  extern int intl_identifier_cmp (const char *str1, const char *str2);
  extern int intl_identifier_namecmp (const char *str1, const char *str2);
  extern int intl_identifier_lower_string_size (const char *src);
  extern int intl_identifier_lower (const char *src, char *dst);
  extern int intl_identifier_upper_string_size (const char *src);
  extern int intl_identifier_upper (const char *src, char *dst);
  extern int intl_identifier_fix (char *name);
  extern int intl_strncat (unsigned char *dest, const unsigned char *src,
			   int len);
  extern int intl_put_char (unsigned char *dest, const unsigned char *char_p);
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
  extern bool intl_is_letter (const INTL_LANG lang,
			      const INTL_CODESET codeset,
			      const unsigned char *s, const int size);
  extern int intl_check_string (const char *buf, int size, char **pos);
  extern bool intl_is_bom_magic (const char *buf, const int size);
  extern unsigned int intl_utf8_to_codepoint (const unsigned char *utf8,
					      const int size,
					      unsigned char **next_char);
  extern void intl_init_conv_iso8859_9_to_utf8 (void);
  extern int intl_text_iso8859_9_to_utf8 (const char *in_buf,
					  const int in_size, char **out_buf,
					  int *out_size);
  extern int intl_text_utf8_to_iso8859_9 (const char *in_buf,
					  const int in_size, char **out_buf,
					  int *out_size);
  extern void intl_init_conv_iso8859_1_to_utf8 (void);
  extern int intl_text_iso8859_1_to_utf8 (const char *in_buf,
					  const int in_size,
					  char **out_buf, int *out_size);
  extern int intl_text_utf8_to_iso8859_1 (const char *in_buf,
					  const int in_size, char **out_buf,
					  int *out_size);
  extern bool intl_is_currency_symbol (const char *src,
				       DB_CURRENCY * currency,
				       int *symbol_size,
				       const CURRENCY_CHECK_MODE check_mode);
  extern char *intl_get_money_symbol (const DB_CURRENCY currency);
  extern char *intl_get_money_symbol_console (const DB_CURRENCY currency);
  extern char *intl_get_money_symbol_grammar (const DB_CURRENCY currency);
  extern int intl_get_currency_symbol_position (const DB_CURRENCY currency);

#ifdef __cplusplus
}
#endif

#endif				/* _INTL_SUPPORT_H_ */
