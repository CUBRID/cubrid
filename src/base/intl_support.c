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
 * intl_support.c : platform independent internationalization functions.
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <ctype.h>
#include <wctype.h>

#include "error_manager.h"
#include "intl_support.h"
#include "language_support.h"
#include "chartype.h"
#include "system_parameter.h"

#define IS_8BIT(c)              ((c) >> 7)
/* Special values for EUC encodings */
#ifndef SS3
#define SS3                     143
#endif

#define LOCALE_C        "C"
#if defined(AIX)
#define LOCALE_KOREAN   "ko_KR.IBM-eucKR"
#else
#define LOCALE_KOREAN   "korean"
#endif

bool intl_Mbs_support = true;

/* ASCII/ISO 8859-1 string manipulations */
static int intl_tolower_iso8859 (unsigned char *s, int length);
static int intl_toupper_iso8859 (unsigned char *s, int length);
/* General EUC string manipulations */
static int intl_tolower_euc (unsigned char *s, int length_in_chars);
static int intl_toupper_euc (unsigned char *s, int length_in_chars);
static int intl_count_euc_chars (unsigned char *s, int length_in_bytes);
static int intl_count_euc_bytes (unsigned char *s, int length_in_chars);
#if defined (ENABLE_UNUSED_FUNCTION)
static wchar_t *intl_copy_lowercase (const wchar_t * ws, size_t n);
#endif /* ENABLE_UNUSED_FUNCTION */
static int intl_is_korean (unsigned char ch);

/*
 * intl_mbs_chr() - find first occurrence of the given character
 *   return: a pointer to the first occurrence of the given character in
 *           the given multibyte string, or NULL if no occurrence is found
 *   mbs(in)
 *   wc(in)
 */
char *
intl_mbs_chr (const char *mbs, wchar_t wc)
{
  int nbytes;
  wchar_t cur_wc;

  assert (mbs != NULL);

  if (!intl_Mbs_support)
    {
      return (char *) (strchr (mbs, (int) wc));
    }

  for (nbytes = 0;
       (nbytes = mbtowc (&cur_wc, mbs, MB_LEN_MAX)) > 0
       && cur_wc != L'\0' && cur_wc != wc; mbs += nbytes)
    {
      continue;
    }

  if (!*mbs && wc)
    {
      mbs = NULL;
    }

  return (char *) mbs;
}

/*
 * intl_mbs_len() - computes the number of multibyte character sequences in the multibyte
 *             character string, not including the terminating zero byte
 *   return: number of characters if  success.
 *           On error, 0 is returned and errno is set.
 *              EINVAL  : mbs contains an invalid byte sequence.
 *   mbs(in)
 */
int
intl_mbs_len (const char *mbs)
{
  int num_of_chars;
  int clen;

  assert (mbs != NULL);

  if (!intl_Mbs_support)
    {
      return strlen (mbs);
    }

  for (num_of_chars = 0;
       (clen = mblen (mbs, MB_LEN_MAX)) > 0 && *mbs;
       mbs += clen, num_of_chars++)
    {
      continue;
    }

  if (clen < 0)
    {
      errno = EINVAL;
      num_of_chars = 0;
    }

  return num_of_chars;
}

/*
 * intl_mbs_nth() - finds the nth multibyte character in the multibyte string
 *   return: a pointer to the nth character in n.
 *           NULL if either an error occurs or there are not n characters
 *                in the string
 *   mbs(in)
 *   n(in)
 */

const char *
intl_mbs_nth (const char *mbs, size_t n)
{
  size_t num_of_chars;
  int clen;

  assert (mbs != NULL);

  if (!intl_Mbs_support)
    {
      if (strlen (mbs) < n)
	{
	  errno = EINVAL;
	  return NULL;
	}
      return &mbs[n];
    }

  for (num_of_chars = 0, clen = 0;
       num_of_chars < n
       && (clen = mblen (mbs, MB_LEN_MAX)) > 0 && *mbs;
       mbs += clen, num_of_chars++)
    {
      continue;
    }

  if (clen < 0)
    {
      errno = EINVAL;
      mbs = NULL;
    }
  else if (num_of_chars < n)
    {
      mbs = NULL;
    }

  return mbs;
}

/*
 * intl_mbs_spn() - return the size of the prefix of the given multibyte string
 *             consisting of the given wide characters.
 *   return: size in bytes.
 *           If mbs contains an invalid byte sequence,
 *           errno is set and 0 is returned.
 *   mbs(in)
 *   chars(in)
 */
int
intl_mbs_spn (const char *mbs, const wchar_t * chars)
{
  int clen;
  wchar_t wc;
  int size;

  assert (mbs != NULL && chars != NULL);

  if (!intl_Mbs_support)
    {
      return (int) strspn (mbs, (const char *) chars);
    }

  for (size = 0;
       (clen = mbtowc (&wc, mbs, MB_LEN_MAX)) > 0 && *mbs
       && wcschr (chars, wc); mbs += clen, size += clen)
    {
      continue;
    }

  if (clen < 0)
    {
      errno = EINVAL;
      size = 0;
    }

  return size;
}

/*
 * intl_mbs_casecmp() - compares successive multi-byte character elements
 *                 from two multi-byte strings
 *   return: 0 if all the multi-byte character elements are the same,
 *           positive number if mbs1 is greater than mbs2,
 *           negative number otherwise.
 *   mbs1(in)
 *   mbs2(in)
 *
 * Note: This function does not use the collating sequences specified
 *       in the LC_COLLATE category of the current locale.
 *       This function set errno if mbs1 or mbs2 contain one or more
 *       invalid multi-byte characters.
 */
int
intl_mbs_casecmp (const char *mbs1, const char *mbs2)
{
  wchar_t wc1, wc2;
  int mb1_len, mb2_len;

  assert (mbs1 != NULL && mbs2 != NULL);

  if (!intl_Mbs_support)
    {
#if defined(WINDOWS)
      return _stricmp (mbs1, mbs2);
#else
      return strcasecmp (mbs1, mbs2);
#endif
    }

  for (mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX),
       mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
       mb1_len > 0 && mb2_len > 0 && wc1 && wc2
       && !(towlower (wc1) - towlower (wc2));)
    {
      mbs1 += mb1_len;
      mbs2 += mb2_len;

      mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX);
      mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
    }

  if (mb1_len < 0 || mb2_len < 0)
    {
      errno = EINVAL;
    }

  return (int) (towlower (wc1) - towlower (wc2));
}

/*
 * intl_mbs_ncasecmp() - compares the first n successive multi-byte character elements
 *                  from two multi-byte strings
 *   return: 0 if the first n multi-byte character elements are the same,
 *           positive number if mbs1 is greater than mbs2,
 *           negative number otherwise.
 *   mbs1(in)
 *   mbs2(in)
 *   n (in)
 *
 * Note: This function does not use the collating sequences specified
 *       in the LC_COLLATE category of the current locale.
 *       This function set errno if mbs1 or mbs2 contain one or more
 *       invalid multi-byte characters.
 */
int
intl_mbs_ncasecmp (const char *mbs1, const char *mbs2, size_t n)
{
  wchar_t wc1, wc2;
  int mb1_len, mb2_len;
  size_t num_of_chars;

  assert (mbs1 != NULL && mbs2 != NULL);

  if (!intl_Mbs_support)
    {
#if defined(WINDOWS)
      return _strnicmp (mbs1, mbs2, n);
#else
      return strncasecmp (mbs1, mbs2, n);
#endif
    }

  for (num_of_chars = 1,
       mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX),
       mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
       mb1_len > 0 && mb2_len > 0 && wc1 && wc2 && num_of_chars < n
       && !(towlower (wc1) - towlower (wc2)); num_of_chars++)
    {
      mbs1 += mb1_len;
      mbs2 += mb2_len;

      mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX);
      mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
    }

  if (mb1_len < 0 || mb2_len < 0)
    {
      errno = EINVAL;
    }

  return (int) (towlower (wc1) - towlower (wc2));
}

/*
 * intl_mbs_ncpy() - Copy characters from mbs2 to mbs1 at most n bytes
 *   return: mbs1, null-terminated string.
 *   mbs1(out)
 *   mbs2(in)
 *   n(in)
 *
 * Note: If mbs2 contains an invalid multi-byte character, errno is set and the
 *   function returns NULL.  In this case, the contents of mbs1 are undefined.
 */

char *
intl_mbs_ncpy (char *mbs1, const char *mbs2, size_t n)
{
  size_t num_of_bytes;
  int clen, i;
  char *dest;

  assert (mbs1 != NULL && mbs2 != NULL);

  if (!intl_Mbs_support)
    {
      if (strlen (mbs2) >= n)
	{
	  errno = EINVAL;
	  return NULL;
	}
      return strncpy (mbs1, mbs2, n);
    }

  for (num_of_bytes = 0, clen = mblen (mbs2, MB_LEN_MAX), dest = mbs1;
       clen > 0 && (num_of_bytes + clen) <= n;
       clen = mblen (mbs2, MB_LEN_MAX))
    {
      /* copy the next multi-byte char */
      for (i = 0; i < clen; i++)
	{
	  *dest++ = *mbs2++;
	}

      /* advance the byte counter */
      num_of_bytes += clen;
    }

  if (clen < 0)
    {
      errno = EINVAL;
      mbs1 = NULL;
    }
  else if (num_of_bytes < n)
    {
      *dest = '\0';
    }

  return mbs1;
}

/*
 * intl_mbs_lower() - convert given characters to lowercase characters
 *   return: always 0
 *   mbs1(in)
 *   mbs2(out)
 */
int
intl_mbs_lower (const char *mbs1, char *mbs2)
{
  int char_count;
  int length_in_bytes = 0;

  if (!intl_Mbs_support)
    {
      char *s;
      s = strcpy (mbs2, mbs1);
      while (*s)
	{
	  *s = char_tolower (*s);
	  s++;
	}
      return 0;
    }

  if (mbs1)
    {
      length_in_bytes = strlen (mbs1);
    }

  if (length_in_bytes)
    {
      memcpy (mbs2, mbs1, length_in_bytes);
      intl_char_count ((unsigned char *) mbs2, length_in_bytes,
		       lang_charset (), &char_count);
      intl_lower_string ((unsigned char *) mbs2, char_count, lang_charset ());
      mbs2[length_in_bytes] = '\0';
    }
  else
    {
      mbs2[0] = '\0';
    }

  return 0;
}

/*
 * intl_mbs_nlower() - convert given characters to lowercase characters
 *   return: always 0
 *   dest(out) : destination buffer
 *   src(in) : source buffer
 *   max_len(in) : maximum buffer length
 */

int
intl_mbs_nlower (char *dest, const char *src, const int max_len)
{
  int char_count;
  int length_in_bytes = 0;

  if (!intl_Mbs_support)
    {
      char *s;
      s = strcpy (dest, src);
      while (*s)
	{
	  *s = char_tolower (*s);
	  s++;
	}
      return 0;
    }

  if (src)
    {
      length_in_bytes = strlen (src);
    }

  if (length_in_bytes >= max_len)
    {
      length_in_bytes = max_len - 1;	/* include null */
    }
  if (length_in_bytes)
    {
      memcpy (dest, src, length_in_bytes);
      intl_char_count ((unsigned char *) dest, length_in_bytes,
		       lang_charset (), &char_count);
      intl_lower_string ((unsigned char *) dest, char_count, lang_charset ());
      dest[length_in_bytes] = '\0';
    }
  else
    {
      dest[0] = '\0';
    }

  return 0;
}

/*
 * intl_mbs_upper() - convert given characters to uppercase characters
 *   return: always 0
 *   mbs1(in)
 *   mbs2(out)
 */
int
intl_mbs_upper (const char *mbs1, char *mbs2)
{
  int char_count;
  int length_in_bytes = 0;

  if (!intl_Mbs_support)
    {
      char *s;

      for (s = strcpy (mbs2, mbs1); *s; s++)
	{
	  *s = char_toupper (*s);
	}
      return 0;
    }

  if (mbs1)
    {
      length_in_bytes = strlen (mbs1);
    }

  if (length_in_bytes)
    {
      memcpy (mbs2, mbs1, length_in_bytes);
      intl_char_count ((unsigned char *) mbs2, length_in_bytes,
		       lang_charset (), &char_count);
      intl_upper_string ((unsigned char *) mbs2, char_count, lang_charset ());
      mbs2[length_in_bytes] = '\0';
    }
  else
    {
      mbs2[0] = '\0';
    }
  return 0;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * intl_copy_lowercase() - converts the given wide character string to
 *                    a lowercase wide character string
 *   return: new wide character string.
 *           At most n wide characters will be converted and the new wide
 *           character string is null terminated.
 *   ws(in)
 *   n (in)
 *
 * Note: The returned pointer must be freed using wcs_delete().
 */
static wchar_t *
intl_copy_lowercase (const wchar_t * ws, size_t n)
{
  size_t i;
  wchar_t *lower_ws;

  lower_ws = (wchar_t *) malloc (sizeof (wchar_t) * (n + 1));
  if (lower_ws)
    {
      for (i = 0; ws[i] && i < n; i++)
	{
	  lower_ws[i] = towlower (ws[i]);
	}
      lower_ws[i] = L'\0';
    }

  return lower_ws;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * ASCII/ISO 8859-1 encoding functions
 */

/*
 * intl_tolower_iso8859() - replaces all upper case ASCII characters
 *                        with their lower case codes.
 *   return: character counts
 *   s(in/out): string to lowercase
 *   length(in): length of the string
 */
static int
intl_tolower_iso8859 (unsigned char *s, int length)
{
  int char_count = 0;
  unsigned char *end;

  assert (s != NULL);

  for (end = s + length; s < end; s++, char_count++)
    {
      if (intl_is_korean (*s))
	{
	  s += 2;
	  continue;
	}
      if (char_isupper (*s))
	{
	  *s = char_tolower (*s);
	}
      else if (char_isupper_iso8859 (*s))
	{
	  *s = char_tolower_iso8859 (*s);
	}
    }

  return char_count;
}

/*
 * intl_toupper_iso8859() - replaces all lower case ASCII characters
 *                        with their upper case codes.
 *   return: character counts
 *   s(in/out): string to uppercase
 *   length(in): length of the string
 */
static int
intl_toupper_iso8859 (unsigned char *s, int length)
{
  int char_count = 0;
  unsigned char *end;

  assert (s != NULL);

  for (end = s + length; s < end; s++, char_count++)
    {
      if (intl_is_korean (*s))
	{
	  s += 2;
	  continue;
	}
      if (char_islower (*s))
	{
	  *s = char_toupper (*s);
	}
      else if (char_islower_iso8859 (*s))
	{
	  *s = char_toupper_iso8859 (*s);
	}
    }

  return char_count;
}


/*
 * general routines for EUC encoding
 */

/*
 * intl_nextchar_euc() - returns a pointer to the next character in the EUC encoded
 *              string.
 *   return: pointer to the next EUC character in the string.
 *   s(in): string
 *   curr_char_length(out): length of the character at s
 */
unsigned char *
intl_nextchar_euc (unsigned char *s, int *curr_char_length)
{
  assert (s != NULL);

  if (!IS_8BIT (*s))		/* Detected ASCII character */
    {
      *curr_char_length = 1;
    }
  else if (*s == SS3)		/* Detected Code Set 3 character */
    {
      *curr_char_length = 3;
    }
  else				/* Detected 2 byte character (CS1 or CS2) */
    {
      *curr_char_length = 2;
    }

  return (s + (*curr_char_length));
}

/*
 * intl_prevchar_euc() - returns a pointer to the previous character in the EUC
 *                   encoded string.
 *   return: pointer to the previous EUC character in the string s.
 *   s(in): string
 *   prev_char_length(out): length of the previous character
 */
unsigned char *
intl_prevchar_euc (unsigned char *s, int *prev_char_length)
{
  assert (s != NULL);

  s--;
  if (!IS_8BIT (*s))		/* Detected ASCII character */
    {
      *prev_char_length = 1;
    }
  else if (*((--s) - 1) == SS3)
    {				/* Detected Code Set 3 char */
      s--;
      *prev_char_length = 3;
    }
  else
    {				/* Detected 2 byte character (CS1 or CS2) */
      *prev_char_length = 2;
    }

  return s;
}

/*
 * intl_tolower_euc() - Replaces all upper case ASCII characters inside an EUC
 *                    encoded string with their lower case codes.
 *   return: character counts
 *   s(in/out): EUC string to lowercase
 *   length_in_chars(in): length of the string measured in characters
 */
static int
intl_tolower_euc (unsigned char *s, int length_in_chars)
{
  int char_count;
  int dummy;

  assert (s != NULL);

  for (char_count = 0; char_count < length_in_chars; char_count++)
    {
      *s = char_tolower (*s);
      s = intl_nextchar_euc (s, &dummy);
    }

  return char_count;
}

/*
 * intl_toupper_euc() - Replaces all upper case ASCII characters inside an EUC
 *                    encoded string with their upper case codes.
 *   return: character counts
 *   s(in/out): EUC string to uppercase
 *   length_in_chars(in): length of the string measured in characters
 */
static int
intl_toupper_euc (unsigned char *s, int length_in_chars)
{
  int char_count;
  int dummy;

  assert (s != NULL);

  for (char_count = 0; char_count < length_in_chars; char_count++)
    {
      *s = char_toupper (*s);
      s = intl_nextchar_euc (s, &dummy);
    }

  return char_count;
}

/*
 * intl_count_euc_chars() - Counts the number of EUC encoded characters in the
 *                     string.  Embedded NULL characters are counted.
 *   return: none
 *   s(in): string
 *   length_in_bytes(in): length of the string
 *   char_count(out): number of EUC encoded characters found
 *
 * Note: Only whole characters are counted.
 *       if s[length_in_bytes-1] is not the last byte of a multi-byte
 *       character or a single byte character, then that character is not
 *       counted.
 */
static int
intl_count_euc_chars (unsigned char *s, int length_in_bytes)
{
  unsigned char *end;
  int dummy;
  int char_count;

  assert (s != NULL);

  for (end = s + length_in_bytes, char_count = 0; s < end;)
    {
      s = intl_nextchar_euc (s, &dummy);
      if (s <= end)
	{
	  char_count++;
	}
    }

  return char_count;
}

/*
 * intl_count_euc_bytes() - Counts the number of bytes it takes to encode the
 *                     next <length_in_chars> EUC characters in the string
 *   return:  byte counts
 *   s(in): EUC encoded string
 *   lenth_in_chars(in): length of the string in characters
 *   byte_count(out): number of bytes used for encode
 */
static int
intl_count_euc_bytes (unsigned char *s, int length_in_chars)
{
  int char_count;
  int char_width;
  int byte_count;

  assert (s != NULL);

  for (char_count = 0, byte_count = 0; char_count < length_in_chars;
       char_count++)
    {
      s = intl_nextchar_euc (s, &char_width);
      byte_count += char_width;
    }

  return byte_count;
}

/*
 * string handling functions
 */

/*
 * intl_convert_charset() - converts a character string from one codeset to another
 *   return: error code
 *   src(in): string to convert
 *   length_in_chars(in): number of characters from src to convert
 *   src_codeset(IN): enumeration of src codeset
 *   dest(out): string of converted characters
 *   dest_codeset(in): enumeration of dest codeset
 *   unconverted(out): number of chars that could not be converted
 *
 * Note: Currently, codeset conversion is not supported
 */
int
intl_convert_charset (unsigned char *src, int length_in_chars,
		      INTL_CODESET src_codeset, unsigned char *dest,
		      INTL_CODESET dest_codeset, int *unconverted)
{
  int error_code = NO_ERROR;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_KSC5601_EUC:
    default:
      error_code = ER_QSTR_BAD_SRC_CODESET;
      break;
    }

  return (error_code);
}

/*
 * intl_char_count() - Counts the number of characters in the string
 *   return: number of characters found
 *   src(in): string of characters to count
 *   length_in_byrtes(in): length of the string
 *   src_codeset(in): enumeration of src codeset
 *   char_count(out): number of characters found
 *
 * Note: Embedded NULL characters are counted.
 */
int
intl_char_count (unsigned char *src, int length_in_bytes,
		 INTL_CODESET src_codeset, int *char_count)
{
  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      *char_count = length_in_bytes;
      break;

    case INTL_CODESET_KSC5601_EUC:
      *char_count = intl_count_euc_chars (src, length_in_bytes);
      break;

    default:
      *char_count = 0;
      break;
    }

  return *char_count;
}

/*
 * intl_char_size() - returns the number of bytes in a string given the
 *                   start and character length of the string
 *   return: none
 *   src(in): number of byets
 *   length_in_chars(in): legnth of the string in characters
 *   src_code_setL(in): enumeration of src codeset
 *   bytes_count(out): number of byets used for encode teh number of
 *                     characters specified
 *
 * Note: Embedded NULL's are counted as characters.
 */
int
intl_char_size (unsigned char *src, int length_in_chars,
		INTL_CODESET src_codeset, int *byte_count)
{
  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      *byte_count = length_in_chars;
      break;

    case INTL_CODESET_KSC5601_EUC:
      *byte_count = intl_count_euc_bytes (src, length_in_chars);
      break;

    default:
      *byte_count = 0;
      break;
    }

  return *byte_count;
}

/*
 * intl_upper_string() - replace all lower case ASCII characters with their
 *                      upper case characters
 *   return: character counts
 *   src(in/out): string to uppercase
 *   length_in_chars(in): length of the string measured in characters
 *   src_codeset(in): enumeration of src string
 */
int
intl_upper_string (unsigned char *src, int length_in_chars,
		   INTL_CODESET src_codeset)
{
  int char_count = 0;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      char_count = intl_toupper_iso8859 (src, length_in_chars);
      break;

    case INTL_CODESET_KSC5601_EUC:
      char_count = intl_toupper_euc (src, length_in_chars);
      break;

    default:
      break;
    }

  return char_count;
}

/*
 * intl_lower_string() - replace all upper case ASCII characters with their
 *                      lower case characters
 *   return: character counts
 *   src(in/out): string to lowercase
 *   length_in_chars(in): length of the string measured in characters
 *   src_codeset(in): enumeration of src string
 */
int
intl_lower_string (unsigned char *src, int length_in_chars,
		   INTL_CODESET src_codeset)
{
  int char_count = 0;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      char_count = intl_tolower_iso8859 (src, length_in_chars);
      break;

    case INTL_CODESET_KSC5601_EUC:
      char_count = intl_tolower_euc (src, length_in_chars);
      break;

    default:
      break;
    }

  return char_count;
}

/*
 * intl_is_korean() - test for a korean character
 *   return: non-zero if ch is a korean character,
 *           0 otherwise.
 *   ch(in): the character to be tested
 */
static int
intl_is_korean (unsigned char ch)
{
  if (PRM_SINGLE_BYTE_COMPARE)
    {
      return 0;
    }
  return (ch >= 0xb0 && ch <= 0xc8) || (ch >= 0xa1 && ch <= 0xfe);
}

/*
 * intl_language() - Returns the language for the given category of the
 *                   current locale
 *   return: INTL_LANG enumeration
 *   category(in): category argument to setlocale()
 */
INTL_LANG
intl_language (int category)
{
  char *loc = setlocale (category, NULL);

#if defined(WINDOWS) || defined(SOLARIS)
  return INTL_LANG_ENGLISH;
#else /* !WINDOWS && !SOLARIS */
  if (strcmp (loc, LOCALE_KOREAN) == 0)
    {
      return INTL_LANG_KOREAN;
    }
  else
    {
      return INTL_LANG_ENGLISH;
    }
#endif
}

/*
 * intl_zone() - Return the zone for the given category of the
 *               current locale
 *   return: INTL_ZONE enumeration
 *   category(in): category argument to setlocale()
 */
INTL_ZONE
intl_zone (int category)
{
  char *loc = setlocale (category, NULL);

#if defined(WINDOWS) || defined(SOLARIS)
  return INTL_ZONE_US;
#else /* !WINDOWS && !SOLARIS */
  if (strcmp (loc, LOCALE_KOREAN) == 0)
    {
      return INTL_ZONE_KR;
    }
  else
    {
      return INTL_ZONE_US;
    }
#endif
}

/*
 * intl_codeset() - Return the codeset for the given category of the
 *                  current locale
 *   return: INTL_CODESET enumeration
 *   category(in): category argument to setlocale()
 */
INTL_CODESET
intl_codeset (int category)
{
  char *loc = setlocale (category, NULL);

#if defined(WINDOWS) || defined(SOLARIS)
  return INTL_CODESET_ISO88591;
#else /* !WINDOWS && !SOLARIS */
  if (strcmp (loc, LOCALE_KOREAN) == 0)
    {
      return INTL_CODESET_KSC5601_EUC;
    }
  else
    {
      return INTL_CODESET_ISO88591;
    }
#endif
}
