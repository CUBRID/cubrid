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

extern bool intl_Mbs_support;

typedef enum intl_lang INTL_LANG;
enum intl_lang
{
  INTL_LANG_ENGLISH,
  INTL_LANG_KOREAN
};

typedef enum intl_zone INTL_ZONE;
enum intl_zone
{
  INTL_ZONE_US,
  INTL_ZONE_KR,
  INTL_ZONE_GB
};

typedef enum intl_codeset INTL_CODESET;
enum intl_codeset
{
  INTL_CODESET_NONE = -1,
  INTL_CODESET_ASCII,		/* US English charset, ASCII encoding */
  INTL_CODESET_RAW_BITS,	/* Uninterpreted bits, Raw encoding */
  INTL_CODESET_RAW_BYTES,	/* Uninterpreted bytes, Raw encoding */
  INTL_CODESET_ISO88591,	/* Latin 1 charset, ISO 8859 encoding */
  INTL_CODESET_KSC5601_EUC	/* KSC 5601 1990 charset , EUC encoding */
};


#ifdef __cplusplus
extern "C"
{
#endif

  extern unsigned char *intl_nextchar_euc (unsigned char *s,
					   int *curr_length);
  extern unsigned char *intl_prevchar_euc (unsigned char *s,
					   int *prev_length);

  extern INTL_LANG intl_language (int category);
  extern INTL_ZONE intl_zone (int category);
  extern INTL_CODESET intl_codeset (int category);

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
  extern int intl_upper_string (unsigned char *src, int length_in_chars,
				INTL_CODESET src_codeset);
  extern int intl_lower_string (unsigned char *src, int length_in_chars,
				INTL_CODESET src_codeset);

  extern int intl_mbs_lower (const char *mbs1, char *mbs2);
  extern int intl_mbs_nlower (char *dest, const char *src, const int max_len);
  extern int intl_mbs_upper (const char *mbs1, char *mbs2);

  extern char *intl_mbs_chr (const char *mbs, wchar_t the_char);
  extern int intl_mbs_spn (const char *mbs, const wchar_t * chars);
  extern int intl_mbs_len (const char *mbs);

  extern const char *intl_mbs_nth (const char *mbs, size_t n);
  extern char *intl_mbs_ncpy (char *mbs1, const char *mbs2, size_t n);
  extern int intl_mbs_casecmp (const char *mbs1, const char *mbs2);
  extern int intl_mbs_ncasecmp (const char *mbs1, const char *mbs2, size_t n);

#ifdef __cplusplus
}
#endif

#endif				/* _INTL_SUPPORT_H_ */
