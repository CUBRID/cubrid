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

/* TODO : this is the same as function intl_is_korean : the first
 * interval is not necessary or the function call is not correct.
 * In the context where 'IS_PSEUDO_KOREAN' is used, the korean char size is 2
 * instead of 3 in context of 'intl_is_korean' */
#define IS_PSEUDO_KOREAN(ch) \
        (( ((unsigned char) ch >= (unsigned char) 0xb0)           \
             && ((unsigned char) ch <= (unsigned char) 0xc8) )    \
          || ( ((unsigned char) ch >= (unsigned char) 0xa1)       \
              && ((unsigned char) ch <= (unsigned char) 0xfe) ))

/* UTF-8 to console types */
typedef struct
{
  int size;			/* size in bytes of char in UTF-8 */
  unsigned char utf8_bytes[2];	/* encoding sequence in UTF-8;
				 * two bytes are enough for ISO8859-9 */
} CONV_8BIT_TO_UTF8;

typedef struct
{
  unsigned char val8bit;	/* encoded value on 8 bit */
  int size;			/* size in bytes of char in UTF-8 */
  unsigned char utf8_bytes[2];	/* encoding sequence in UTF-8;
				 * two bytes are enough for ISO8859-9 */
} CONV_UTF8_TO_8BIT;

typedef struct
{
  unsigned char val8bit;	/* value on 8 bit corresponding to an
				 * ISO xxxx-x encoding scheme */
  unsigned int codepoint;	/* unicode code-point mapped to the 8 bit ISO
				 * value*/
} CONV_8BIT_TO_CODEPOINT;

/* conversion from turkish ISO 8859-9 to UTF-8: uses an array of 256 elements
 * to find the UTF-8 bytes for each ISO value;
 * from UTF-8 to turkish ISO 8859-9: only the A0 - FF values in ISO
 * are needed; the others are not mapped (7f - 9f) or are ASCII codes (00-7E);
 * special mapping from turkish ISO to Unicode codepoints is used to build the 
 * mappings ISO <-> UTF-8 */
static CONV_8BIT_TO_UTF8 iso8859_9_to_utf8_conv[256];
static CONV_UTF8_TO_8BIT utf8_to_iso8859_9_conv[96];
static CONV_8BIT_TO_CODEPOINT iso8859_9_codepoints[] = {
  {0xd0, 0x11e},		/* capital G with breve */
  {0xdd, 0x130},		/* capital I with dot above */
  {0xde, 0x15e},		/* capital S with cedilla */
  {0xf0, 0x11f},		/* small g with breve */
  {0xfd, 0x131},		/* small i dotless */
  {0xfe, 0x15f}			/* small s with cedilla */
};

/* all chars of UTF8 to ISO-8859-9 conversion map require 2 bytes */
#define UTF8_ISO8859_9_SIZE 2

/* conversion from Latin 1 ISO 8859-1 to UTF-8: uses an array of 256 elements
 * to find the UTF-8 bytes for each ISO value;
 * from UTF-8 to Latin 1 ISO 8859-1: the codepoints that need conversion have 
 * the same values as the Latin 1 ISO value;
 */
static CONV_8BIT_TO_UTF8 iso8859_1_to_utf8_conv[256];


/* identifiers : support for multibyte chars in INTL_CODESET_ISO88591 codeset
 * (default legacy codeset) */
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

/* UTF-8 string manipulations */
static int intl_tolower_utf8 (unsigned char *s, unsigned char *d,
			      int length_in_chars);
static int intl_toupper_utf8 (unsigned char *s, unsigned char *d,
			      int length_in_chars);
static int intl_count_utf8_chars (unsigned char *s, int length_in_bytes);
static int intl_count_utf8_bytes (unsigned char *s, int length_in_chars);
static int intl_char_tolower_utf8 (unsigned char *s, const int size,
				   unsigned char *d, unsigned char **next);
static int intl_char_toupper_utf8 (unsigned char *s, const int size,
				   unsigned char *d, unsigned char **next);
static int intl_codepoint_to_utf8 (const unsigned int codepoint,
				   unsigned char *utf8_seq);

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
      return NULL;
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
  if (mbs == NULL)
    {
      return NULL;
    }

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

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * intl_mbs_namecmp() - compares successive multi-byte character
 *                 from two multi-byte identifier string
 *   return: 0 if all the multi-byte character identifier are the "same",
 *           positive number if mbs1 is greater than mbs2,
 *           negative number otherwise.
 *   mbs1(in)
 *   mbs2(in)
 *
 * Note: "same" means that this function ignores bracket '[', ']'
 *       so mbs1 = "[value]" and mbs2 = "value" returns 0
 */
int
intl_mbs_namecmp (const char *mbs1, const char *mbs2)
{
  const char *cp1 = mbs1;
  const char *cp2 = mbs2;
  int cp1_len, cp2_len;

  assert (mbs1 != NULL && mbs2 != NULL);

  cp1_len = strlen (cp1);
  cp2_len = strlen (cp2);

  if (cp1[0] == '[')
    {
      cp1++;
      cp1_len -= 2;
    }

  if (cp2[0] == '[')
    {
      cp2++;
      cp2_len -= 2;
    }

  if (cp1_len != cp2_len)
    {
      /* fail return */
      return intl_mbs_casecmp (cp1, cp2);
    }

  return intl_mbs_ncasecmp (cp1, cp2, cp1_len);
}
#endif

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

#if defined (ENABLE_UNUSED_FUNCTION)
int
intl_mbs_cmp (const char *mbs1, const char *mbs2)
{
  wchar_t wc1, wc2;
  int mb1_len, mb2_len;

  assert (mbs1 != NULL && mbs2 != NULL);

  if (!intl_Mbs_support)
    {
      return strcmp (mbs1, mbs2);
    }

  for (mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX),
       mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
       mb1_len > 0 && mb2_len > 0 && wc1 && wc2 && !(wc1 - wc2);)
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

  return (int) (wc1 - wc2);
}
#endif

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
 * intl_mbs_ncpy() - Copy characters from mbs2 to mbs1 at most (n-1) bytes
 *   return: mbs1, null-terminated string.
 *   mbs1(out)
 *   mbs2(in)
 *   n(in): size of destination buffer, including null-terminator
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
      size_t src_len = strlen (mbs2);

      strncpy (mbs1, mbs2, n - 1);
      if (src_len < n)
	{
	  mbs1[src_len] = '\0';
	}
      else
	{
	  mbs1[n - 1] = '\0';
	}

      return mbs1;
    }

  for (num_of_bytes = 0, clen = mblen (mbs2, MB_LEN_MAX), dest = mbs1;
       clen > 0 && (num_of_bytes + clen) <= n - 1;
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
  else
    {
      *dest = '\0';
    }

  return mbs1;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * intl_mbs_lower() - convert given characters to lowercase characters
 *   return: always 0
 *   mbs1(in)
 *   mbs2(out)
 */
int
intl_mbs_lower (const char *mbs1, char *mbs2)
{
  int char_count = 0;
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
      intl_char_count ((unsigned char *) mbs1, length_in_bytes,
		       lang_charset (), &char_count);
      intl_lower_string ((unsigned char *) mbs1, (unsigned char *) mbs2,
			 char_count, lang_charset ());
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
  int char_count = 0;
  int length_in_bytes = 0;

  if (src == NULL)
    {
      dest[0] = '\0';
      return 0;
    }

  if (!intl_Mbs_support)
    {
      int i = 0;
      for (i = 0; (src[i] != '\0') && (i < max_len - 1); ++i)
	{
	  dest[i] = char_tolower (src[i]);
	}
      dest[i] = '\0';
      return 0;
    }

  length_in_bytes = strlen (src);

  if (length_in_bytes >= max_len)
    {
      /* include null */
      length_in_bytes = max_len - 1;
    }

  if (length_in_bytes > 0)
    {
      intl_char_count ((unsigned char *) src, length_in_bytes,
		       lang_charset (), &char_count);
      intl_lower_string ((unsigned char *) src, (unsigned char *) dest,
			 char_count, lang_charset ());
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
  int char_count = 0;
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
      intl_char_count ((unsigned char *) mbs1, length_in_bytes,
		       lang_charset (), &char_count);
      intl_upper_string ((unsigned char *) mbs1, (unsigned char *) mbs2,
			 char_count, lang_charset ());
      mbs2[length_in_bytes] = '\0';
    }
  else
    {
      mbs2[0] = '\0';
    }
  return 0;
}

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
    case INTL_CODESET_UTF8:
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
 *   length_in_bytes(in): length of the string
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

    case INTL_CODESET_UTF8:
      *char_count = intl_count_utf8_chars (src, length_in_bytes);
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
 *   src_code_set(in): enumeration of src codeset
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

    case INTL_CODESET_UTF8:
      *byte_count = intl_count_utf8_bytes (src, length_in_chars);
      break;

    default:
      *byte_count = 0;
      break;
    }

  return *byte_count;
}

/*
 * intl_char_size_pseudo_kor() - returns the number of bytes in a string given
 *				 the start and character length of the string
 *
 *   return: none
 *   src(in): number of byets
 *   length_in_chars(in): legnth of the string in characters
 *   src_code_set(in): enumeration of src codeset
 *   bytes_count(out): number of byets used for encode teh number of
 *                     characters specified
 *
 * Note: Embedded NULL's are counted as characters.
 *	 This is similar to 'intl_char_size' except with INTL_CODESET_ISO88591
 *	 codeset, some bytes are considered korean characters
 *	 This function is used in context of some specific string functions.
 */
int
intl_char_size_pseudo_kor (unsigned char *src, int length_in_chars,
			   INTL_CODESET src_codeset, int *byte_count)
{
  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!PRM_SINGLE_BYTE_COMPARE)
	{
	  int b_count = 0;
	  while (length_in_chars-- > 0)
	    {
	      if (IS_PSEUDO_KOREAN (*src))
		{
		  b_count += 2;
		  src += 2;
		}
	      else
		{
		  b_count++;
		  src++;
		}
	    }
	  *byte_count = b_count;
	}
      else
	{
	  *byte_count = length_in_chars;
	}
      break;

    case INTL_CODESET_KSC5601_EUC:
      *byte_count = intl_count_euc_bytes (src, length_in_chars);
      break;

    case INTL_CODESET_UTF8:
      *byte_count = intl_count_utf8_bytes (src, length_in_chars);
      break;

    default:
      *byte_count = 0;
      break;
    }

  return *byte_count;
}

/*
 * intl_prev_char() - returns pointer to the previous char in string
 *
 *   return : pointer to previous character
 *   s(in) : string
 *   codeset(in) : enumeration of src codeset
 *   prev_char_size(out) : size of previous character
 */
unsigned char *
intl_prev_char (unsigned char *s, INTL_CODESET codeset, int *prev_char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      *prev_char_size = 1;
      return --s;

    case INTL_CODESET_KSC5601_EUC:
      return intl_prevchar_euc (s, prev_char_size);

    case INTL_CODESET_UTF8:
      return intl_prevchar_utf8 (s, prev_char_size);

    default:
      *prev_char_size = 0;
      return s;
    }
}

/*
 * intl_prev_char_pseudo_kor() - returns pointer to the previous char in
 *				 string
 *
 *   return : pointer to previous character
 *   s(in) : string
 *   codeset(in) : enumeration of src codeset
 *   prev_char_size(out) : size of previous character
 *
 * Note: This is similar to 'intl_prev_char' except with INTL_CODESET_ISO88591
 *	 codeset, some bytes are considered korean characters
 *	 This function is used in context of some specific string functions.
 */
unsigned char *
intl_prev_char_pseudo_kor (unsigned char *s, INTL_CODESET codeset,
			   int *prev_char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!PRM_SINGLE_BYTE_COMPARE && IS_PSEUDO_KOREAN (*(s - 1)))
	{
	  *prev_char_size = 2;
	  return s - 2;
	}
      else
	{
	  *prev_char_size = 1;
	  return --s;
	}

    case INTL_CODESET_KSC5601_EUC:
      return intl_prevchar_euc (s, prev_char_size);

    case INTL_CODESET_UTF8:
      return intl_prevchar_utf8 (s, prev_char_size);

    default:
      *prev_char_size = 0;
      return s;
    }
}

/*
 * intl_next_char () - returns pointer to the next char in string
 *
 *   return: Pointer to the next character in the string.
 *   s(in) : string
 *   codeset(in) : enumeration of the codeset of s
 *   current_char_size(out) : length of the character at s
 *
 * Note: Returns a pointer to the next character in the string.
 *	 curr_char_length is set to the byte length of the current character.
 */
unsigned char *
intl_next_char (unsigned char *s, INTL_CODESET codeset,
		int *current_char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      *current_char_size = 1;
      return ++s;

    case INTL_CODESET_KSC5601_EUC:
      return intl_nextchar_euc (s, current_char_size);

    case INTL_CODESET_UTF8:
      return intl_nextchar_utf8 (s, current_char_size);

    default:
      *current_char_size = 0;
      return s;
    }
}

/*
 * intl_next_char_pseudo_kor () - returns pointer to the next char in string
 *
 *   return: Pointer to the next character in the string.
 *   s(in) : string
 *   codeset(in) : enumeration of the codeset of s
 *   current_char_size(out) : length of the character at s
 *
 * Note: This is similar to 'intl_next_char' except with INTL_CODESET_ISO88591
 *	 codeset, some bytes are considered korean characters
 *	 This function should be used only in context of string functions
 *	 where korean characters are expected to be handled.
 */
unsigned char *
intl_next_char_pseudo_kor (unsigned char *s, INTL_CODESET codeset,
			   int *current_char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!PRM_SINGLE_BYTE_COMPARE && IS_PSEUDO_KOREAN (*s))
	{
	  *current_char_size = 2;
	  return s + 2;
	}
      else
	{
	  *current_char_size = 1;
	  return ++s;
	}

    case INTL_CODESET_KSC5601_EUC:
      return intl_nextchar_euc (s, current_char_size);

    case INTL_CODESET_UTF8:
      return intl_nextchar_utf8 (s, current_char_size);

    default:
      *current_char_size = 0;
      return s;
    }
}

/*
 * intl_cmp_char() - compares the first character of two strings
 *   return: zero if character are equal, non-zero otherwise
 *   s1(in):
 *   s2(in):
 *   codeset:
 *   char_size(in): size of char in bytes of the first character in s1
 *
 *  Note: it is assumed that both strings contain at least one character of
 *	  the given codeset.
 *	  
 */
int
intl_cmp_char (const unsigned char *s1, const unsigned char *s2,
	       INTL_CODESET codeset, int *char_size)
{

  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      *char_size = 1;
      return *s1 - *s2;

    case INTL_CODESET_KSC5601_EUC:
      *char_size = 2;
      return memcmp (s1, s2, 2);

    case INTL_CODESET_UTF8:
      {
	int len;

	len = intl_Len_utf8_char[*s1];
	*char_size = len;
	return memcmp (s1, s2, len);
      }

    default:
      *char_size = 1;
      return 0;
    }

  return 0;
}

/*
 * intl_cmp_char_pseudo_kor() - compares the first character of two strings
 *   return: zero if character are equal, non-zero otherwise
 *   s1(in):
 *   s2(in):
 *   codeset:
 *   char_size(in): size of char in bytes of the first character in s1
 *
 *  Note: same as intl_cmp_char, except that with ISO-8859-1 codeset, some
 *	  bytes are handled as Korean characters.
 *	  
 */
int
intl_cmp_char_pseudo_kor (unsigned char *s1, unsigned char *s2,
			  INTL_CODESET codeset, int *char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!PRM_SINGLE_BYTE_COMPARE && IS_PSEUDO_KOREAN (*s1))
	{
	  *char_size = 2;
	  return memcmp (s1, s2, 2);
	}
      else
	{
	  *char_size = 1;
	  return *s1 - *s2;
	}
      *char_size = 1;
      return *s1 - *s2;

    case INTL_CODESET_KSC5601_EUC:
      *char_size = 2;
      return memcmp (s1, s2, 2);

    case INTL_CODESET_UTF8:
      {
	int len;

	len = intl_Len_utf8_char[*s1];
	*char_size = len;
	return memcmp (s1, s2, len);
      }

    default:
      *char_size = 1;
      return 0;
    }

  return 0;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * intl_kor_cmp() - compares first characters of two strings
 *   return: required size
 *   s1(in):
 *   s2(in):
 *   size(in): max size in bytes to compare
 *
 *  Note: this function is used only in context of 'replace' string function
 *	  strncmp function should be used. 
 */
int
intl_kor_cmp (unsigned char *s1, unsigned char *s2, int size)
{
  int r;
  while (size > 0)
    {
      if (!PRM_SINGLE_BYTE_COMPARE && IS_PSEUDO_KOREAN (*s1) &&
	  IS_PSEUDO_KOREAN (*s2))
	{
	  r = memcmp (s1, s2, 2);
	  if (r == 0)
	    {
	      s1 += 2;
	      s2 += 2;
	      size -= 2;
	    }
	  else
	    {
	      return r;
	    }
	}
      else if ((PRM_SINGLE_BYTE_COMPARE || !IS_PSEUDO_KOREAN (*s1))
	       && *s1 == *s2)
	{
	  s1++;
	  s2++;
	  size--;
	}
      else
	{
	  return (*s1 - *s2);
	}
    }
  return 0;
}
#endif

/*
 * intl_pad_char() - returns the pad character of requested codeset
 *   return: none
 *   codeset(in): International codeset.
 *   pad_char(in/out): Pointer to array which will be filled with
 *		       the pad character.
 *   pad_size(out): Size of pad character.
 *
 * Note:
 *     There is a pad character associated with every character code
 *     set.  This function will retrieve the pad character for a given
 *     code set.  The pad character is written into an array that must
 *     allocated by the caller.
 *	  
 */
void
intl_pad_char (const INTL_CODESET codeset, unsigned char *pad_char,
	       int *pad_size)
{
  switch (codeset)
    {
    case INTL_CODESET_RAW_BITS:
    case INTL_CODESET_RAW_BYTES:
      pad_char[0] = '\0';
      *pad_size = 1;
      break;

    case INTL_CODESET_KSC5601_EUC:
      /* TODO : intl_pad_size return 1, but pad char has 2 bytes */
      pad_char[0] = pad_char[1] = '\241';
      *pad_size = 2;
      break;

    case INTL_CODESET_ASCII:
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_UTF8:
      pad_char[0] = ' ';
      *pad_size = 1;
      break;

    default:
      break;
    }
}

/*
 * intl_pad_size() - Returns the byte size of the pad character for the given
 *		     codeset.
 *   return: size of pading char
 *   codeset(in): International codeset.
 *
 * Note:
 *     There is a pad character associated with every character code
 *     set.  This function will retrieve the pad character for a given
 *     code set.  The pad character is written into an array that must
 *     allocated by the caller.
 *	  
 */
int
intl_pad_size (INTL_CODESET codeset)
{
  int size;

  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_KSC5601_EUC:
    case INTL_CODESET_UTF8:
    default:
      size = 1;
      break;
    }

  return size;
}

/*
 * intl_upper_string_size() - determine the size required for holding
 *			     upper case of the input string
 *   return: required size
 *   src(in): string to uppercase
 *   src_size(in): buffer size
 *   src_length(in): length of the string measured in characters
 *   src_codeset(in): enumeration of src string
 */
int
intl_upper_string_size (unsigned char *src, int src_size, int src_length,
			INTL_CODESET src_codeset)
{
  int char_count;
  int req_size = src_size;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      break;

    case INTL_CODESET_KSC5601_EUC:
      break;

    case INTL_CODESET_UTF8:
      {
	unsigned char upper[INTL_UTF8_MAX_CHAR_SIZE];
	unsigned char *next = NULL;

	req_size = 0;
	for (char_count = 0; char_count < src_length && src_size > 0;
	     char_count++)
	  {
	    req_size += intl_char_toupper_utf8 (src, src_size, upper, &next);
	    src_size -= (next - src);
	    src = next;
	  }
      }
      break;

    default:
      break;
    }

  return req_size;
}

/*
 * intl_upper_string() - replace all lower case ASCII characters with their
 *                      upper case characters
 *   return: character counts
 *   src(in/out): string source to uppercase
 *   dst(in/out): output string
 *   length_in_chars(in): length of the string measured in characters
 *   src_codeset(in): codeset of string
 */
int
intl_upper_string (unsigned char *src, unsigned char *dst,
		   int length_in_chars, INTL_CODESET src_codeset)
{
  int char_count = 0;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      {
	int byte_count;
	intl_char_size (src, length_in_chars, INTL_CODESET_ISO88591,
			&byte_count);
	if (byte_count > 0)
	  {
	    memcpy (dst, src, byte_count);
	    char_count = intl_toupper_iso8859 (dst, length_in_chars);
	  }
      }
      break;

    case INTL_CODESET_KSC5601_EUC:
      {
	int byte_count;
	intl_char_size (src, length_in_chars, INTL_CODESET_KSC5601_EUC,
			&byte_count);
	if (byte_count > 0)
	  {
	    memcpy (dst, src, byte_count);
	    char_count = intl_toupper_euc (dst, length_in_chars);
	  }
      }
      break;

    case INTL_CODESET_UTF8:
      char_count = intl_toupper_utf8 (src, dst, length_in_chars);
      break;

    default:
      break;
    }

  return char_count;
}

/*
 * intl_lower_string_size() - determine the size required for holding
 *			     lower case of the input string
 *   return: required size
 *   src(in): string to lowercase
 *   src_size(in): buffer size
 *   src_length(in): length of the string measured in characters
 *   src_codeset(in): enumeration of src string
 */
int
intl_lower_string_size (unsigned char *src, int src_size, int src_length,
			INTL_CODESET src_codeset)
{
  int char_count;
  int req_size = src_size;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      break;

    case INTL_CODESET_KSC5601_EUC:
      break;

    case INTL_CODESET_UTF8:
      {
	unsigned char lower[INTL_UTF8_MAX_CHAR_SIZE];
	unsigned char *next;

	req_size = 0;
	for (char_count = 0; char_count < src_length && src_size > 0;
	     char_count++)
	  {
	    req_size += intl_char_tolower_utf8 (src, src_size, lower, &next);
	    src_size -= (next - src);
	    src = next;
	  }
      }
      break;

    default:
      break;
    }

  return req_size;
}

/*
 * intl_lower_string() - replace all upper case characters with their
 *                      lower case characters
 *   return: character counts
 *   src(in/out): string to lowercase
 *   dst(out): output string
 *   length_in_chars(in): length of the string measured in characters
 *   src_codeset(in): codeset of string
 */
int
intl_lower_string (unsigned char *src, unsigned char *dst,
		   int length_in_chars, INTL_CODESET src_codeset)
{
  int char_count = 0;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      {
	int byte_count;
	intl_char_size (src, length_in_chars, INTL_CODESET_ISO88591,
			&byte_count);
	if (byte_count > 0)
	  {
	    memcpy (dst, src, byte_count);
	    char_count = intl_tolower_iso8859 (dst, length_in_chars);
	  }
      }
      break;

    case INTL_CODESET_KSC5601_EUC:
      {
	int byte_count;
	intl_char_size (src, length_in_chars, INTL_CODESET_KSC5601_EUC,
			&byte_count);
	if (byte_count > 0)
	  {
	    memcpy (dst, src, byte_count);
	    char_count = intl_tolower_euc (dst, length_in_chars);
	  }
      }
      break;

    case INTL_CODESET_UTF8:
      char_count = intl_tolower_utf8 (src, dst, length_in_chars);
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

#if defined (ENABLE_UNUSED_FUNCTION)
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
  if (loc != NULL && strcmp (loc, LOCALE_KOREAN) == 0)
    {
      return INTL_LANG_KOREAN;
    }
  else
    {
      return INTL_LANG_ENGLISH;
    }
#endif
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * intl_zone() - Return the zone for the given category of the
 *               current locale
 *   return: INTL_ZONE enumeration
 *   lang_id(in): language identifier
 */
INTL_ZONE
intl_zone (const INTL_LANG lang_id)
{
  INTL_ZONE zone_id;

  switch (lang_id)
    {
    case INTL_LANG_KOREAN:
      zone_id = INTL_ZONE_KR;
      break;
    case INTL_LANG_TURKISH:
      zone_id = INTL_ZONE_TR;
      break;
    default:
      zone_id = INTL_ZONE_US;
    }

  return zone_id;
}

/*
 * intl_reverse_string() - reverse characters of source string,
 *			   into destination string
 *   return: character counts
 *   src(in): source string
 *   dst(out): destination string
 *   length_in_chars(in): length of the string measured in characters
 *   size_in_bytes(in): size of the string in bytes
 *   codeset(in): enumeration of source string
 */
int
intl_reverse_string (unsigned char *src, unsigned char *dst,
		     int length_in_chars, int size_in_bytes,
		     INTL_CODESET codeset)
{
  unsigned char *end, *s, *d;
  int char_count = 0;
  int char_size, i;

  assert (src != NULL);
  assert (dst != NULL);

  s = src;

  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      {
	d = dst + length_in_chars - 1;
	end = src + length_in_chars;
	for (; s < end; s++, d--, char_count++)
	  {
	    if (intl_is_korean (*s))
	      {
		*(d - 2) = *s;
		*(d - 1) = *(s + 1);
		*d = *(s + 2);
		s += 2;
		d -= 2;
		continue;
	      }
	    else
	      {
		*d = *s;
	      }
	  }
      }
      break;

    case INTL_CODESET_KSC5601_EUC:
      {
	d = dst + size_in_bytes - 1;
	end = src + size_in_bytes;
	for (; s < end && char_count < length_in_chars; char_count++)
	  {
	    if (!IS_8BIT (*s))	/* ASCII character */
	      {
		*d-- = *s++;
	      }
	    else if (*s == SS3)	/* Code Set 3 character */
	      {
		*(d - 2) = *s;
		*(d - 1) = *(s + 1);
		*d = *(s + 2);
		s += 3;
		d -= 3;
	      }
	    else		/* 2 byte character (CS1 or CS2) */
	      {
		*(d - 1) = *s;
		*d = *(s + 1);
		s += 2;
		d -= 2;
	      }
	  }
      }
      break;

    case INTL_CODESET_UTF8:
      {
	d = dst + size_in_bytes - 1;
	end = src + size_in_bytes;
	for (; s < end && char_count < length_in_chars; char_count++)
	  {
	    intl_nextchar_utf8 (s, &char_size);

	    i = char_size;
	    while (i > 0)
	      {
		i--;
		*(d - i) = *s;
		s++;
	      }
	    d -= char_size;
	  }
      }
      break;

    default:
      break;
    }

  return char_count;
}

/*
 * general routines for UTF-8 encoding
 */

static const unsigned char len_utf8_char[256] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5,
  5, 5, 5, 6, 6, 1, 1
};
const unsigned char *const intl_Len_utf8_char = len_utf8_char;

/*
 * intl_nextchar_utf8() - returns a pointer to the next character in the
 *              UTF-8 encoded string.
 *   return: pointer to the next character
 *   s(in): input string
 *   curr_char_length(out): length of the character at s
 */
unsigned char *
intl_nextchar_utf8 (unsigned char *s, int *curr_char_length)
{
  INTL_GET_NEXTCHAR_UTF8 (s, *curr_char_length);
  return s;
}

/*
 * intl_prevchar_utf8() - returns a pointer to the previous character in the
 *                   UTF-8 encoded string.
 *   return: pointer to the previous character
 *   s(in): string
 *   prev_char_length(out): length of the previous character
 */
unsigned char *
intl_prevchar_utf8 (unsigned char *s, int *prev_char_length)
{
  INTL_GET_PREVCHAR_UTF8 (s, *prev_char_length);
  return s;
}

/*
 * intl_tolower_utf8() - Replaces all upper case characters inside an UTF-8
 *			 encoded string with their lower case codes.
 *   return: character counts
 *   s(in): UTF-8 string to lowercase
 *   d(out): output string
 *   length_in_chars(in): length of the string measured in characters
 */
static int
intl_tolower_utf8 (unsigned char *s, unsigned char *d, int length_in_chars)
{
  int char_count, size;
  int s_size;
  unsigned char *next = NULL;

  assert (s != NULL);

  s_size = strlen ((char *) s);

  for (char_count = 0; char_count < length_in_chars; char_count++)
    {
      if (s_size <= 0)
	{
	  break;
	}
      size = intl_char_tolower_utf8 (s, s_size, d, &next);
      d += size;

      s_size -= next - s;
      s = next;
    }

  return char_count;
}

/*
 * intl_toupper_utf8() - Replaces all lower case characters inside an UTF-8
 *			 encoded string with their upper case codes.
 *   return: character counts
 *   s(in): UTF-8 string to uppercase
 *   d(out): output string
 *   length_in_chars(in): length of the string measured in characters
 */
static int
intl_toupper_utf8 (unsigned char *s, unsigned char *d, int length_in_chars)
{
  int char_count, size;
  int s_size;
  unsigned char *next = NULL;

  assert (s != NULL);

  s_size = strlen ((char *) s);

  for (char_count = 0; char_count < length_in_chars; char_count++)
    {
      if (s_size <= 0)
	{
	  break;
	}
      size = intl_char_toupper_utf8 (s, s_size, d, &next);
      d += size;

      s_size -= next - s;
      s = next;
    }

  return char_count;
}

/*
 * intl_count_utf8_chars() - Counts the number of UTF-8 encoded characters in
 *                     the string. Embedded NULL characters are counted.
 *   return: none
 *   s(in): string
 *   length_in_bytes(in): length of the string
 *   char_count(out): number of UTF-8 encoded characters found
 *
 * Note: Only whole characters are counted.
 *       if s[length_in_bytes-1] is not the last byte of a multi-byte
 *       character or a single byte character, then that character is not
 *       counted.
 */
static int
intl_count_utf8_chars (unsigned char *s, int length_in_bytes)
{
  unsigned char *end;
  int dummy;
  int char_count;

  assert (s != NULL);

  for (end = s + length_in_bytes, char_count = 0; s < end;)
    {
      s = intl_nextchar_utf8 (s, &dummy);
      if (s <= end)
	{
	  char_count++;
	}
    }

  return char_count;
}

/*
 * intl_count_utf8_bytes() - Counts the number of bytes it takes to encode the
 *                     next <length_in_chars> UTF-8 characters in the string
 *   return: byte counts
 *   s(in): UTF-8 encoded string
 *   lenth_in_chars(in): length of the string in characters
 *   byte_count(out): number of bytes used for encode
 */
static int
intl_count_utf8_bytes (unsigned char *s, int length_in_chars)
{
  int char_count;
  int char_width;
  int byte_count;

  assert (s != NULL);

  for (char_count = 0, byte_count = 0; char_count < length_in_chars;
       char_count++)
    {
      s = intl_nextchar_utf8 (s, &char_width);
      byte_count += char_width;
    }

  return byte_count;
}

/*
 * intl_char_tolower_utf8() - convert uppercase character to lowercase
 *   return: size of UTF-8 lowercase character corresponding to the argument
 *   s (in): the UTF-8 buffer holding character to be converted
 *   size(in): size of UTF-8 buffer
 *   d (out): output buffer
 *   next (out): pointer to next character 
 *
 *  Note : allocated size of 'd' is assumed to be large enough to fit any
 *	   UTF-8 character
 */
static int
intl_char_tolower_utf8 (unsigned char *s, const int size, unsigned char *d,
			unsigned char **next)
{
  const LANG_LOCALE_DATA *locale = lang_locale ();

  unsigned int cp = intl_utf8_to_codepoint (s, size, next);

  if (cp < (unsigned int) (locale->alpha_cnt))
    {
      unsigned lower_cp = locale->alpha_lower_cp[cp];

      return intl_codepoint_to_utf8 (lower_cp, d);
    }
  else if (cp == 0xffffffff)
    {
      /* TODO: this may happen now for hardcoded Koreean text (months name)
       * not converted to UTF-8 */
      *d = *s;
      return 1;
    }

  return intl_codepoint_to_utf8 (cp, d);
}

/*
 * intl_char_toupper_utf8() - convert lowercase character to uppercase
 *   return: size of UTF-8 uppercase character corresponding to the argument
 *   s (in): the UTF-8 buffer holding character to be converted
 *   size(in): size of UTF-8 buffer
 *   d (out): output buffer
 *   next (out): pointer to next character 
 *
 *  Note : allocated size of 'd' is assumed to be large enough to fit any
 *	   UTF-8 character
 */
static int
intl_char_toupper_utf8 (unsigned char *s, const int size, unsigned char *d,
			unsigned char **next)
{
  const LANG_LOCALE_DATA *locale = lang_locale ();

  unsigned int cp = intl_utf8_to_codepoint (s, size, next);

  if (cp < (unsigned int) (locale->alpha_cnt))
    {
      unsigned upper_cp = locale->alpha_upper_cp[cp];

      return intl_codepoint_to_utf8 (upper_cp, d);
    }
  else if (cp == 0xffffffff)
    {
      /* TODO: this may happen now for hardcoded Koreean text (months name)
       * not converted to UTF-8 */
      *d = *s;
      return 1;
    }

  return intl_codepoint_to_utf8 (cp, d);
}

/*
 * intl_strcmp_utf8() - string compare for UTF8
 *   return:
 *   string1(in):
 *   size1(in):
 *   string2(in):
 *   size2(in):
 */
int
intl_strcmp_utf8 (const unsigned char *str1, const int size1,
		  const unsigned char *str2, const int size2)
{
  const unsigned char *str1_end;
  const unsigned char *str2_end;
  unsigned char *str1_next, *str2_next;
  unsigned int cp1, cp2, w_cp1, w_cp2;
  const LANG_LOCALE_DATA *loc = lang_locale ();
  const int alpha_cnt = loc->alpha_cnt;
  const unsigned int *weight_ptr = loc->alpha_weight;

  str1_end = str1 + size1;
  str2_end = str2 + size2;

  for (; str1 < str1_end && str2 < str2_end;)
    {
      assert (str1_end - str1 > 0);
      assert (str2_end - str2 > 0);

      cp1 = intl_utf8_to_codepoint (str1, str1_end - str1, &str1_next);
      cp2 = intl_utf8_to_codepoint (str2, str2_end - str2, &str2_next);

      if (cp1 < (unsigned int) alpha_cnt)
	{
	  w_cp1 = weight_ptr[cp1];
	}
      else
	{
	  w_cp1 = cp1;
	}

      if (cp2 < (unsigned int) alpha_cnt)
	{
	  w_cp2 = weight_ptr[cp2];
	}
      else
	{
	  w_cp2 = cp2;
	}

      if (w_cp1 != w_cp2)
	{
	  return (w_cp1 < w_cp2) ? (-1) : 1;
	}

      str1 = str1_next;
      str2 = str2_next;
    }

  if (size1 == size2)
    {
      return 0;
    }
  else if (size1 < size2)
    {
      for (; str2 < str2_end;)
	{
	  /* ignore trailing white spaces */
	  if (*str2 != 32 && *str2 != 0)
	    {
	      return -1;
	    }
	  str2 += intl_Len_utf8_char[*str2];
	}
    }
  else
    {
      for (; str1 < str1_end;)
	{
	  /* ignore trailing white spaces */
	  if (*str1 != 32 && *str1 != 0)
	    {
	      return 1;
	    }
	  str1 += intl_Len_utf8_char[*str1];
	}
    }

  return 0;
}

/*
 * intl_next_alpha_char_utf8() - computes the next alphabetical char
 *   return: size in bytes of the next alphabetical char
 *   cur_char(in): pointer to current char
 *   next_char(in/out): buffer to return next alphabetical char
 *
 *  Note :  It is assumed that the input buffer (cur_char) contains at least
 *	    one UTF-8 character. Because of this, when the first character
 *	    is converted to codepoint value, the maximum value for a char size
 *	    in UTF-8 is used as buffer size: 6.
 */
int
intl_next_alpha_char_utf8 (const unsigned char *cur_char,
			   unsigned char *next_char)
{
  unsigned int cp_alpha_char, cp_next_alpha_char;
  const LANG_LOCALE_DATA *loc = lang_locale ();
  const int alpha_cnt = loc->alpha_cnt;
  const unsigned int *next_alpha_char = loc->alpha_next_char;
  unsigned char *dummy = NULL;

  assert (next_char != NULL);

  cp_alpha_char = intl_utf8_to_codepoint (cur_char, 6, &dummy);

  if (cp_alpha_char < (unsigned int) alpha_cnt)
    {
      cp_next_alpha_char = next_alpha_char[cp_alpha_char];
    }
  else
    {
      cp_next_alpha_char = cp_alpha_char + 1;
    }

  return intl_codepoint_to_utf8 (cp_next_alpha_char, next_char);
}

/*
 * intl_strcasecmp() -
 *   return:  0 if strings are equal, -1 if str1 < str2 , 1 if str1 > str2
 *   str1(in):
 *   str2(in):
 *   len(in): number of characters to compare
 *   identifier_mode(in): true if compares identifiers, false otherwise
 *
 *  Note : Comparing identifiers has some exceptions: the casing for some
 *	   letters overriddes the casing of current language.
 */
int
intl_strcasecmp (const INTL_LANG lang_id, unsigned char *str1,
		 unsigned char *str2, int len, bool identifier_mode)
{
  if (lang_charset () == INTL_CODESET_UTF8 && lang_id != INTL_LANG_KOREAN)
    {
      unsigned char *str1_end, *str2_end;
      unsigned char *str1_next, *str2_next;
      int size_str1, size_str2, i;
      unsigned int cp1, cp2, cp1_ex, cp2_ex;
      const LANG_LOCALE_DATA *loc =
	lang_get_specific_locale (lang_id, INTL_CODESET_UTF8);
      int alpha_cnt = loc->alpha_cnt;
      const unsigned int *lower_ptr = loc->alpha_lower_cp;

      intl_char_size (str1, len, INTL_CODESET_UTF8, &size_str1);
      intl_char_size (str2, len, INTL_CODESET_UTF8, &size_str2);
      str1_end = str1 + size_str1;
      str2_end = str2 + size_str2;

      for (i = 0; i < len; i++)
	{
	  assert (str1_end - str1 > 0);
	  assert (str2_end - str2 > 0);

	  cp1 = intl_utf8_to_codepoint (str1, str1_end - str1, &str1_next);
	  cp2 = intl_utf8_to_codepoint (str2, str2_end - str2, &str2_next);

	  if (cp1 != cp2)
	    {
	      if (cp1 < (unsigned int) alpha_cnt)
		{
		  cp1_ex =
		    identifier_mode ? lang_unicode_lower_case_ex_cp (cp1) :
		    0xffffffff;

		  if (cp1_ex == 0xffffffff)
		    {
		      cp1 = lower_ptr[cp1];
		    }
		  else
		    {
		      cp1 = cp1_ex;
		    }
		}

	      if (cp2 < (unsigned int) alpha_cnt)
		{
		  cp2_ex =
		    identifier_mode ? lang_unicode_lower_case_ex_cp (cp2) :
		    0xffffffff;

		  if (cp2_ex == 0xffffffff)
		    {
		      cp2 = lower_ptr[cp2];
		    }
		  else
		    {
		      cp2 = cp2_ex;
		    }
		}

	      if (cp1 != cp2)
		{
		  return (cp1 < cp2) ? (-1) : 1;
		}
	    }

	  str1 = str1_next;
	  str2 = str2_next;
	}

      return 0;
    }

  /* ASCII */
  return strncasecmp ((char *) str1, (char *) str2, len);
}

/*
 * intl_strcasecmp_w_size()
 *   return:  0 if strings are equal, -1 if str1 < str2 , 1 if str1 > str2
 *   str1(in):
 *   str2(in):
 *   size_str1(in): size in bytes of str1
 *   size_str2(in): size in bytes of str2
 *   identifier_mode(in): true if compares identifiers, false otherwise
 *
 *  Note : Comparing identifiers has some exceptions: the casing for some
 *	   letters overriddes the casing of current language.
 */
int
intl_strcasecmp_w_size (const INTL_LANG lang_id, unsigned char *str1,
			unsigned char *str2, const int size_str1,
			const int size_str2, bool identifier_mode)
{
#if INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER <= 1
  if (size_str1 != size_str2)
    {
      return (size_str1 < size_str2) ? -1 : 1;
    }
#endif

  if (lang_charset () == INTL_CODESET_UTF8 && lang_id != INTL_LANG_KOREAN)
    {
      unsigned char *str1_end, *str2_end;
      unsigned char *str1_next, *str2_next;
      unsigned int cp1, cp2, cp1_ex, cp2_ex;
      const LANG_LOCALE_DATA *loc =
	lang_get_specific_locale (lang_id, INTL_CODESET_UTF8);
      int alpha_cnt = loc->alpha_cnt;
      const unsigned int *lower_ptr = loc->alpha_lower_cp;

      str1_end = str1 + size_str1;
      str2_end = str2 + size_str2;

      for (; str1 < str1_end && str2 < str2_end;)
	{
	  cp1 = intl_utf8_to_codepoint (str1, str1_end - str1, &str1_next);
	  cp2 = intl_utf8_to_codepoint (str2, str2_end - str2, &str2_next);

	  if (cp1 != cp2)
	    {
	      if (cp1 < (unsigned int) alpha_cnt)
		{
		  cp1_ex =
		    identifier_mode ? lang_unicode_lower_case_ex_cp (cp1) :
		    0xffffffff;

		  if (cp1_ex == 0xffffffff)
		    {
		      cp1 = lower_ptr[cp1];
		    }
		  else
		    {
		      cp1 = cp1_ex;
		    }
		}

	      if (cp2 < (unsigned int) alpha_cnt)
		{
		  cp2_ex =
		    identifier_mode ? lang_unicode_lower_case_ex_cp (cp2) :
		    0xffffffff;

		  if (cp2_ex == 0xffffffff)
		    {
		      cp2 = lower_ptr[cp2];
		    }
		  else
		    {
		      cp2 = cp2_ex;
		    }
		}

	      if (cp1 != cp2)
		{
		  return (cp1 < cp2) ? (-1) : 1;
		}
	    }

	  str1 = str1_next;
	  str2 = str2_next;
	}

      return 0;
    }

  /* ASCII */
  if (size_str1 != size_str2)
    {
      return (size_str1 < size_str2) ? -1 : 1;
    }

  return strncasecmp ((char *) str1, (char *) str2, size_str1);
}

/*
 * intl_identifier_casecmp() - compares two identifiers strings 
 *			       case insensitive 
 *   return:
 *   str1(in):
 *   str2(in):
 *
 */
int
intl_identifier_casecmp (const char *str1, const char *str2)
{
  int str1_size = strlen (str1);
  int str2_size = strlen (str2);

  return intl_strcasecmp_w_size (lang_id (), (unsigned char *) str1,
				 (unsigned char *) str2, str1_size, str2_size,
				 true);
}

/*
 * intl_identifier_ncasecmp() - compares two identifiers strings 
 *				case insensitive 
 *   return:
 *   str1(in):
 *   str2(in):
 *   len(in): number of chars to compare
 *
 */
int
intl_identifier_ncasecmp (const char *str1, const char *str2, const int len)
{
  return intl_strcasecmp (lang_id (), (unsigned char *) str1,
			  (unsigned char *) str2, len, true);
}

/*
 * intl_identifier_cmp() - compares two identifiers strings
 *			   case sensitive
 *   return:
 *   str1(in):
 *   str2(in):
 *
 */
int
intl_identifier_cmp (const char *str1, const char *str2)
{
  /* when comparing identifiers, order of current collation is not important */
  return strcmp (str1, str2);
}

/*
 * intl_identifier_namecmp() - compares two identifier string
 *   return: 0 if the identifiers are the "same",
 *           positive number if mbs1 is greater than mbs2,
 *           negative number otherwise.
 *   str1(in)
 *   str2(in)
 *
 * Note: "same" means that this function ignores bracket '[', ']'
 *       so str1 = "[value]" and str2 = "value" returns 0
 */
int
intl_identifier_namecmp (const char *str1, const char *str2)
{
  const char *cp1 = str1;
  const char *cp2 = str2;
  int str1_size, str2_size;

  assert (str1 != NULL && str2 != NULL);

  str1_size = strlen (cp1);
  str2_size = strlen (cp2);

  if (cp1[0] == '[')
    {
      cp1++;
      str1_size -= 2;
    }

  if (cp2[0] == '[')
    {
      cp2++;
      str2_size -= 2;
    }

  return intl_strcasecmp_w_size (lang_id (), (unsigned char *) cp1,
				 (unsigned char *) cp2, str1_size, str2_size,
				 true);
}

/*
 * intl_identifier_lower_string_size() - determine the size required for holding
 *					 lower case of the input string
 *   return: required size
 *   src(in): string to lowercase
 */
int
intl_identifier_lower_string_size (const char *src)
{
  int src_size, src_lower_size;
  INTL_CODESET codeset = lang_charset ();

  src_size = strlen (src);

  switch (codeset)
    {
    case INTL_CODESET_UTF8:
#if (INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER > 1)
      {
	unsigned char lower[INTL_UTF8_MAX_CHAR_SIZE];
	unsigned char *next;
	unsigned char *s;
	const LANG_LOCALE_DATA *locale = lang_locale ();
	int s_size = src_size;
	unsigned int cp, lower_cp;
	int src_len;

	intl_char_count ((unsigned char *) src, src_size, codeset, &src_len);

	src_lower_size = 0;

	for (s = (unsigned char *) src; s < (unsigned char *) src + src_size;)
	  {
	    assert (s_size > 0);

	    cp = intl_utf8_to_codepoint (s, s_size, &next);
	    if (cp < (unsigned int) (locale->alpha_cnt))
	      {
		lower_cp = lang_unicode_lower_case_ex_cp (cp);
		if (lower_cp == 0xffffffff)
		  {
		    lower_cp = locale->alpha_lower_cp[cp];
		  }
	      }
	    else
	      {
		lower_cp = cp;
	      }

	    src_lower_size += intl_codepoint_to_utf8 (lower_cp, lower);

	    s_size -= next - s;
	    s = next;
	  }
      }
#else
      src_lower_size = src_size;
#endif
      break;

    case INTL_CODESET_ISO88591:
    case INTL_CODESET_KSC5601_EUC:
    default:
      src_lower_size = src_size;
      break;
    }

  return src_lower_size;
}

/*
 * intl_identifier_lower() - convert given characters to lowercase characters
 *   return: always 0
 *   src(in) : source buffer
 *   dst(out) : destination buffer
 */
int
intl_identifier_lower (const char *src, char *dst)
{
  int char_count = 0;
  int length_in_bytes = 0;
  unsigned char *d;

  if (src)
    {
      length_in_bytes = strlen (src);
    }

  switch (lang_charset ())
    {
    case INTL_CODESET_UTF8:
      {
	const LANG_LOCALE_DATA *locale = lang_locale ();
	unsigned char *next, *s;
	int s_size = length_in_bytes;
	unsigned int cp;
	unsigned int lower_cp;

	for (s = (unsigned char *) src, d = (unsigned char *) dst;
	     s < (unsigned char *) src + length_in_bytes;)
	  {
	    assert (s_size > 0);

	    cp = intl_utf8_to_codepoint (s, s_size, &next);
	    if (cp < (unsigned int) (locale->alpha_cnt))
	      {
		lower_cp = lang_unicode_lower_case_ex_cp (cp);
		if (lower_cp == 0xffffffff)
		  {
		    lower_cp = locale->alpha_lower_cp[cp];
		  }
	      }
	    else
	      {
		lower_cp = cp;
	      }

	    d += intl_codepoint_to_utf8 (lower_cp, d);

	    s_size -= next - s;
	    s = next;
	  }
      }
      break;
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_KSC5601_EUC:
    default:
      {
	memcpy (dst, src, length_in_bytes);

	for (d = (unsigned char *) dst;
	     d < (unsigned char *) dst + length_in_bytes; d++)
	  {
	    if (IS_PSEUDO_KOREAN (*d))
	      {
		/* safe : skip only current byte since "korean" encoding in QA
		 * scenarios is not consistent : either 2 or 3 bytes */
		continue;
	      }
	    if (char_isupper (*d))
	      {
		*d = char_tolower (*d);
	      }
	    else if (char_isupper_iso8859 (*d))
	      {
		*d = char_tolower_iso8859 (*d);
	      }
	  }
      }
      break;
    }

  assert ((char *) d - dst <= length_in_bytes);

  *d = '\0';

  return 0;
}

/*
 * intl_identifier_upper_string_size() - determine the size required for holding
 *					 upper case of the input string
 *   return: required size
 *   src(in): string to lowercase
 */
int
intl_identifier_upper_string_size (const char *src)
{
  int src_size, src_upper_size;
  INTL_CODESET codeset = lang_charset ();

  src_size = strlen (src);

  switch (codeset)
    {
    case INTL_CODESET_UTF8:
#if (INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER > 1)
      {
	unsigned char upper[INTL_UTF8_MAX_CHAR_SIZE];
	unsigned char *next;
	unsigned char *s;
	const LANG_LOCALE_DATA *locale = lang_locale ();
	int s_size = src_size;
	unsigned int cp, upper_cp;
	int src_len;

	intl_char_count ((unsigned char *) src, src_size, codeset, &src_len);

	src_upper_size = 0;

	for (s = (unsigned char *) src; s < (unsigned char *) src + src_size;)
	  {
	    assert (s_size > 0);

	    cp = intl_utf8_to_codepoint (s, s_size, &next);
	    if (cp < (unsigned int) (locale->alpha_cnt))
	      {
		upper_cp = lang_unicode_upper_case_ex_cp (cp);
		if (upper_cp == 0xffffffff)
		  {
		    upper_cp = locale->alpha_upper_cp[cp];
		  }
	      }
	    else
	      {
		upper_cp = cp;
	      }

	    src_upper_size += intl_codepoint_to_utf8 (upper_cp, upper);

	    s_size -= next - s;
	    s = next;
	  }
      }
#else
      src_upper_size = src_size;
#endif
      break;

    case INTL_CODESET_ISO88591:
    case INTL_CODESET_KSC5601_EUC:
    default:
      src_upper_size = src_size;
      break;
    }

  return src_upper_size;
}

/*
 * intl_identifier_upper() - convert given characters to uppercase characters
 *   return: always 0
 *   src(in)
 *   dst(out)
 */
int
intl_identifier_upper (const char *src, char *dst)
{
  int char_count = 0;
  int length_in_bytes = 0;
  unsigned char *d;

  if (src)
    {
      length_in_bytes = strlen (src);
    }

  switch (lang_charset ())
    {
    case INTL_CODESET_UTF8:
      {
	const LANG_LOCALE_DATA *locale = lang_locale ();
	unsigned char *next, *s;
	int s_size = length_in_bytes;
	unsigned int cp;
	unsigned int upper_cp;

	for (s = (unsigned char *) src, d = (unsigned char *) dst;
	     s < (unsigned char *) src + length_in_bytes;)
	  {
	    assert (s_size > 0);

	    cp = intl_utf8_to_codepoint (s, s_size, &next);
	    if (cp < (unsigned int) (locale->alpha_cnt))
	      {
		upper_cp = lang_unicode_upper_case_ex_cp (cp);
		if (upper_cp == 0xffffffff)
		  {
		    upper_cp = locale->alpha_upper_cp[cp];
		  }
	      }
	    else
	      {
		upper_cp = cp;
	      }

	    d += intl_codepoint_to_utf8 (upper_cp, d);

	    s_size -= next - s;
	    s = next;
	  }
      }
      break;
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_KSC5601_EUC:
    default:
      {
	memcpy (dst, src, length_in_bytes);

	for (d = (unsigned char *) dst;
	     d < (unsigned char *) dst + length_in_bytes; d++)
	  {
	    if (IS_PSEUDO_KOREAN (*d))
	      {
		/* safe : skip only current byte since "korean" encoding in QA
		 * scenarios is not consistent : either 2 or 3 bytes */
		continue;
	      }
	    if (char_islower (*d))
	      {
		*d = char_toupper (*d);
	      }
	    else if (char_islower_iso8859 (*d))
	      {
		*d = char_toupper_iso8859 (*d);
	      }
	  }
      }
      break;
    }

  assert ((char *) d - dst <= length_in_bytes);

  *d = '\0';

  return 0;
}

/*
 * intl_identifier_fix - Checks if an string can be an identifier;
 *			 Fixes the string if the last char is truncated
 *			 Checks that lower and upper case versions of string
 *			 do not exceed maximum allowed size.
 *
 *   return: error code : ER_GENERIC_ERROR or NO_ERROR
 *   name(in): identifier name
 *
 *  Note : Identifier string may be truncated if lexer previously truncated it
 *	   in the middle of the last character;
 *	   No error message is outputed by this function - in case of error,
 *	   the error message should be output by the caller.
 *	   Only UTF-8 charset are handled.
 */
int
intl_identifier_fix (char *name)
{
  int i, length_bytes;
  unsigned char *name_char = (unsigned char *) name;
  int char_size;
  INTL_CODESET codeset = lang_charset ();

  assert (name != NULL);

  if (codeset != INTL_CODESET_UTF8)
    {
      return NO_ERROR;
    }

  length_bytes = strlen (name);

  /* check if last char of identifier may have been truncated */
  if (length_bytes + INTL_UTF8_MAX_CHAR_SIZE > DB_MAX_IDENTIFIER_LENGTH)
    {
      /* count original size based on the size given by first byte of each
       * char */
      for (i = 0; i < length_bytes;)
	{
	  name_char = intl_next_char (name_char, codeset, &char_size);
	  i += char_size;
	}

      assert (i >= length_bytes);

      /* assume the original last character was truncated */
      if (i > length_bytes && i < length_bytes + INTL_UTF8_MAX_CHAR_SIZE &&
	  *name_char > 0x80)
	{
	  /* truncate after the last full character */
	  i -= char_size;
	  length_bytes = i;
	  name[i] = '\0';
	}
    }

  /* ensure that lower or upper versions of identifier do not exceed maximum 
   * allowed size of an identifier */
#if (INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER > 1)
  if (intl_identifier_upper_string_size (name) > DB_MAX_IDENTIFIER_LENGTH
      || intl_identifier_lower_string_size (name) > DB_MAX_IDENTIFIER_LENGTH)
    {
      return ER_GENERIC_ERROR;
    }
#endif

  return NO_ERROR;
}

/*
 * intl_strncat() - concatenates at most len characters from 'src' to 'dest'
 *   return: number of bytes copied
 *   dest(in/out): 
 *   src(in);
 *   len(in): length to concatenate (in chars)
 *
 *  Note : the NULL terminator is always appended to 'dest';
 *	   it is assumed that 'dest' allocated size can fit appended chars
 *
 */
int
intl_strncat (unsigned char *dest, const unsigned char *src, int len)
{
  int result = 0;

  if (lang_charset () == INTL_CODESET_UTF8)
    {
      int copy_len = 0;
      unsigned char *p_dest = dest + strlen ((char *) dest);
      const unsigned char *p_char = NULL;
      int char_len;

      while (*src && copy_len < len)
	{
	  if (*src < 0x80)
	    {
	      *p_dest++ = *src++;
	    }
	  else
	    {
	      p_char = src;
	      INTL_GET_NEXTCHAR_UTF8 (src, char_len);
	      memcpy (p_dest, p_char, char_len);
	      p_dest += char_len;
	    }
	  copy_len++;
	}
      result = p_dest - dest;
    }
  else
    {
      strncat ((char *) dest, (char *) src, len);
      result = len;
    }

  return result;
}

/*
 * intl_put_char() - puts a character into a string buffer
 *   return: size of character
 *   dest(in/out): destination buffer
 *   char_p(in): pointer to character
 *
 *  Note : It is assumed that 'dest' buffer can fit the character.
 *
 */
int
intl_put_char (unsigned char *dest, const unsigned char *char_p)
{
  assert (char_p != NULL);

  switch (lang_charset ())
    {
    case INTL_CODESET_UTF8:
      {
	int char_len;

	if (*char_p < 0x80)
	  {
	    *dest = *char_p;
	    return 1;
	  }
	else
	  {
	    char_len = intl_Len_utf8_char[*char_p];
	    memcpy (dest, char_p, char_len);
	    return char_len;
	  }
      }
      break;

    case INTL_CODESET_KSC5601_EUC:
      *dest++ = *char_p++;
      *dest = *char_p;
      return 2;

    case INTL_CODESET_ISO88591:
    default:
      *dest = *char_p;
      return 1;
    }

  return 1;
}

/*
 * intl_codepoint_to_utf8() - converts a unicode codepoint to its
 *                            UTF-8 encoding 
 *  return: number for bytes for UTF-8; 0 means not encoded
 *  codepoint(in) : Unicode code point (32 bit value)
 *  utf8_seq(in/out) : pre-allocated buffer for UTF-8 sequence
 *
 */
static int
intl_codepoint_to_utf8 (const unsigned int codepoint, unsigned char *utf8_seq)
{
  assert (utf8_seq != NULL);

  if (codepoint <= 0x7f)
    {
      /* 1 byte */
      *utf8_seq = (unsigned char) codepoint;
      return 1;
    }
  if (codepoint <= 0x7ff)
    {
      /* 2 bytes */
      *utf8_seq++ = (unsigned char) (0xc0 | (codepoint >> 6));
      *utf8_seq = (unsigned char) (0x80 | (codepoint & 0x3f));
      return 2;
    }
  if (codepoint <= 0xffff)
    {
      /* 3 bytes */
      *utf8_seq++ = (unsigned char) (0xe0 | (codepoint >> 12));
      *utf8_seq++ = (unsigned char) (0x80 | ((codepoint >> 6) & 0x3f));
      *utf8_seq = (unsigned char) (0x80 | (codepoint & 0x3f));
      return 3;
    }
  if (codepoint <= 0x10ffff)
    {
      /* 4 bytes */
      *utf8_seq++ = (unsigned char) (0xf0 | (codepoint >> 18));
      *utf8_seq++ = (unsigned char) (0x80 | ((codepoint >> 12) & 0x3f));
      *utf8_seq++ = (unsigned char) (0x80 | ((codepoint >> 6) & 0x3f));
      *utf8_seq = (unsigned char) (0x80 | (codepoint & 0x3f));
      return 4;
    }

  assert (false);
  *utf8_seq = '?';
  return 1;
}

/*
 * intl_utf8_to_codepoint() - converts a UTF-8 encoded char to
 *                            unicode codepoint
 *  return: unicode code point; 0xffffffff means error 
 *  utf8(in) : buffer for UTF-8 char
 *  size(in) : size of buffer
 *  next_char(in/out): pointer to next character
 *
 */
unsigned int
intl_utf8_to_codepoint (const unsigned char *utf8, const int size,
			unsigned char **next_char)
{
  assert (utf8 != NULL);
  assert (size > 0);
  assert (next_char != NULL);

  if (utf8[0] < 0x80)
    {
      *next_char = (unsigned char *) utf8 + 1;
      return (unsigned int) (utf8[0]);
    }
  else if (size >= 2 && utf8[0] >= 0xc0 && utf8[0] < 0xe0)
    {
      *next_char = (unsigned char *) utf8 + 2;
      return (unsigned int) (((utf8[0] & 0x1f) << 6) | (utf8[1] & 0x3f));
    }
  else if (size >= 3 && utf8[0] >= 0xe0 && utf8[0] < 0xf0)
    {
      *next_char = (unsigned char *) utf8 + 3;
      return (unsigned int) (((utf8[0] & 0x0f) << 12) |
			     ((utf8[1] & 0x3f) << 6) | (utf8[2] & 0x3f));
    }
  else if (size >= 4 && utf8[0] >= 0xf0 && utf8[0] < 0xf8)
    {
      *next_char = (unsigned char *) utf8 + 4;
      return (unsigned int) (((utf8[0] & 0x07) << 18)
			     | ((utf8[1] & 0x3f) << 12)
			     | ((utf8[2] & 0x3f) << 6) | (utf8[3] & 0x3f));
    }
  else if (size >= 5 && utf8[0] >= 0xf8 && utf8[0] < 0xfc)
    {
      *next_char = (unsigned char *) utf8 + 5;
      return (unsigned int) (((utf8[0] & 0x03) << 24)
			     | ((utf8[1] & 0x3f) << 18)
			     | ((utf8[2] & 0x3f) << 12)
			     | ((utf8[3] & 0x3f) << 6) | (utf8[4] & 0x3f));
    }
  else if (size >= 6 && utf8[0] >= 0xfc && utf8[0] < 0xfe)
    {
      *next_char = (unsigned char *) utf8 + 6;
      return (unsigned int) (((utf8[0] & 0x01) << 30)
			     | ((utf8[1] & 0x3f) << 24)
			     | ((utf8[2] & 0x3f) << 18)
			     | ((utf8[3] & 0x3f) << 12)
			     | ((utf8[4] & 0x3f) << 6) | (utf8[5] & 0x3f));
    }

  *next_char = (unsigned char *) utf8 + 1;
  return 0xffffffff;
}

/*
 * intl_is_letter() - checks if a character is a letter in the current
 *		      locale alphabet
 *   return: true if letter, false otherwise
 *   lang(in): language identifier
 *   codeset(in): character set
 *   buf(in): buffer of character
 *   size(in): size of buffer
 *
 */
bool
intl_is_letter (const INTL_LANG lang, const INTL_CODESET codeset,
		const unsigned char *s, const int size)
{
  if (codeset == INTL_CODESET_UTF8)
    {
      unsigned char *dummy;
      unsigned int cp = intl_utf8_to_codepoint (s, size, &dummy);

      switch (lang)
	{
	case INTL_LANG_KOREAN:
	  /* TODO: Korean in UTF8 ? */
	  return true;
	case INTL_LANG_ENGLISH:
	  if (cp >= LANG_CHAR_COUNT_EN)
	    {
	      return false;
	    }
	  return lang_get_specific_locale (lang,
					   INTL_CODESET_UTF8)->
	    alpha_is_letter_cp[cp];
	case INTL_LANG_TURKISH:
	  if (cp >= LANG_CHAR_COUNT_TR)
	    {
	      return false;
	    }
	  return lang_get_specific_locale (lang,
					   INTL_CODESET_UTF8)->
	    alpha_is_letter_cp[cp];
	default:
	  return false;
	}
    }
  else if (codeset == INTL_CODESET_KSC5601_EUC)
    {
      /* TODO : korean letters */
      return true;
    }
  else
    {
      assert (codeset == INTL_CODESET_ISO88591);

      if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))
	{
	  return true;
	}
    }

  return false;
}

#define UTF8_BYTE_IN_RANGE(b, r1, r2) (!(b < r1 || b > r2))
#define UTF8_RETURN_INVALID_BYTE(p, pos) \
  do { \
    if ((char **)pos != NULL) { \
	* ((char **)pos) = (char *) p; \
    } \
    return 1; \
  } while (0)

#define UTF8_RETURN_CHAR_TRUNCATED(p, pos) \
  do { \
    if ((char **)pos != NULL) { \
	* ((char **)pos) = (char *) p; \
    } \
    return 2; \
  } while (0)
/*
 * intl_check_utf8 - Checks if a string contains valid UTF-8 sequences
 *
 *   return: 0 if valid,
 *	     1 if contains and invalid byte in one char
 *	     2 if last char is truncated (missing bytes)
 *   buf(in): buffer
 *   size(out): size of buffer (negative values accepted, in this case buffer
 *		is assumed to be NUL terminated)
 *   pos(out): pointer to begining of invalid character
 *
 *  Valid ranges:
 *    - 1 byte : 00 - 7F
 *    - 2 bytes: C2 - DF , 80 - BF		       (U +80 .. U+7FF)
 *    - 3 bytes: E0	 , A0 - BF , 80 - BF	       (U +800 .. U+FFF)
 *		 E1 - EC , 80 - BF , 80 - BF	       (U +1000 .. +CFFF)
 *		 ED	 , 80 - 9F , 80 - BF	       (U +D000 .. +D7FF)
 *		 EE - EF , 80 - BF , 80 - BF	       (U +E000 .. +FFFF)
 *    - 4 bytes: F0	 , 90 - BF , 80 - BF , 80 - BF (U +10000 .. +3FFFF)
 *		 F1 - F3 , 80 - BF , 80 - BF , 80 - BF (U +40000 .. +FFFFF)
 *		 F4	 , 80 - 8F , 80 - BF , 80 - BF (U +100000 .. +10FFFF)
 *
 *  Note:
 *  This function should be used only when the UTF-8 string enters the CUBRID
 *  system.
 */
static bool
intl_check_utf8 (const unsigned char *buf, int size, char **pos)
{
  const unsigned char *p = buf;
  const unsigned char *p_end = NULL;
  const unsigned char *curr_char = NULL;

  if (pos != NULL)
    {
      *pos = NULL;
    }

  if (size < 0)
    {
      size = strlen ((char *) buf);
    }

  p_end = buf + size;

  while (p < p_end)
    {
      curr_char = p;

      if (*p < 0x80)
	{
	  p++;
	  continue;
	}

      /* range 80 - BF is not valid UTF-8 first byte */
      /* range C0 - C1 overlaps 1 byte 00 - 20 (2 byte overlongs) */
      if (*p < 0xc2)
	{
	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}

      /* check 2 bytes sequences */
      /* 2 bytes sequence allowed : C2 - DF , 80 - BF */
      if (UTF8_BYTE_IN_RANGE (*p, 0xc2, 0xdf))
	{
	  p++;
	  if (p >= p_end)
	    {
	      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      continue;
	    }
	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}

      /* check 3 bytes sequences */
      /* 3 bytes sequence : E0   , A0 - BF , 80 - BF */
      if (*p == 0xe0)
	{
	  p++;
	  if (p >= p_end)
	    {
	      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0xa0, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  continue;
		}
	    }

	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}
      /* 3 bytes sequence : E1 - EC , 80 - BF , 80 - BF */
      /* 3 bytes sequence : EE - EF , 80 - BF , 80 - BF */
      else if (UTF8_BYTE_IN_RANGE (*p, 0xe1, 0xec) ||
	       UTF8_BYTE_IN_RANGE (*p, 0xee, 0xef))
	{
	  p++;
	  if (p >= p_end)
	    {
	      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  continue;
		}
	    }
	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}
      /* 3 bytes sequence : ED   , 80 - 9F , 80 - BF */
      else if (*p == 0xed)
	{
	  p++;
	  if (p >= p_end)
	    {
	      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0x9f))
	    {
	      p++;
	      if (p >= p_end)
		{
		  UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  continue;
		}
	    }
	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}

      /* 4 bytes sequence : F0   , 90 - BF , 80 - BF , 80 - BF */
      if (*p == 0xf0)
	{
	  p++;
	  if (p >= p_end)
	    {
	      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x90, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      p++;
		      continue;
		    }
		}
	    }
	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}
      /* 4 bytes sequence : F1 - F3 , 80 - BF , 80 - BF , 80 - BF */
      if (UTF8_BYTE_IN_RANGE (*p, 0xf1, 0xf3))
	{
	  p++;
	  if (p >= p_end)
	    {
	      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      p++;
		      continue;
		    }
		}
	    }
	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}
      /* 4 bytes sequence : F4 , 80 - 8F , 80 - BF , 80 - BF */
      else if (*p == 0xf4)
	{
	  p++;
	  if (p >= p_end)
	    {
	      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0x8f))
	    {
	      p++;
	      if (p >= p_end)
		{
		  UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      UTF8_RETURN_CHAR_TRUNCATED (curr_char, pos);
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      p++;
		      continue;
		    }
		}
	    }
	  UTF8_RETURN_INVALID_BYTE (curr_char, pos);
	}

      assert (*p > 0xf4);
      UTF8_RETURN_INVALID_BYTE (curr_char, pos);
    }

  return 0;
}

/*
 * intl_check_string - Checks if a string contains valid sequences in current
 *		       codeset
 *
 *   return: 0 - if valid, non-zero otherwise : 1 - if invalid byte in char
 *	     2 - if last char is truncated
 *   buf(in): buffer
 *   size(out): size of buffer (negative values accepted, in this case buffer
 *		is assumed to be NUL terminated)
 */
int
intl_check_string (const char *buf, int size, char **pos)
{
  /* only check when intl_Mbs_support is disabled */
  if (!intl_Mbs_support)
    {
      return 0;
    }
  if (lang_charset () == INTL_CODESET_UTF8)
    {
      return intl_check_utf8 ((const unsigned char *) buf, size, pos);
    }
  return 0;
}

/*
 * intl_is_bom_magic - Returns 1 if the buffer contains BOM magic for UTF-8
 *
 *   return: true if BOM, false otherwise
 *   buf(in): buffer
 *   size(out): size of buffer (negative means buffer is NUL terminated)
 */
bool
intl_is_bom_magic (const char *buf, const int size)
{
  const char BOM[] = { 0xef, 0xbb, 0xbf };
  if (size >= 3)
    {
      return (memcmp (buf, BOM, 3) == 0) ? true : false;
    }
  else if (size < 0)
    {
      if (*buf == BOM[0] && buf[1] == BOM[1] && buf[2] == BOM[2])
	{
	  return true;
	}
    }

  return false;
}

/* UTF-8 to console routines */

/*
 * intl_init_conv_iso8859_1_to_utf8() - initializes conversion map from 
 *				        ISO 8859-1 (Latin 1) to UTF-8
 *  return:
 */
void
intl_init_conv_iso8859_1_to_utf8 ()
{
  unsigned int i;

  /* 00 - 7E : mapped to ASCII */
  for (i = 0; i <= 0x7e; i++)
    {
      iso8859_1_to_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_1_to_utf8_conv[i].utf8_bytes)) =
	(unsigned char) i;
    }

  /* 7F - 9F : not mapped */
  for (i = 0x7f; i <= 0x9f; i++)
    {
      iso8859_1_to_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_1_to_utf8_conv[i].utf8_bytes)) =
	(unsigned char) '?';
    }

  /* A0 - FF : mapped to Unicode codepoint with the same value */
  for (i = 0xa0; i <= 0xff; i++)
    {
      iso8859_1_to_utf8_conv[i].size =
	intl_codepoint_to_utf8 (i, iso8859_1_to_utf8_conv[i].utf8_bytes);
    }
}

/*
 * intl_text_iso8859_1_to_utf8() - converts a buffer containing text with ISO
 *				   8859-1 (Latin 1) encoding to UTF-8
 *
 *   return: error code
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(out): size of string (NUL terminator not included)
 */
int
intl_text_iso8859_1_to_utf8 (const char *in_buf, const int in_size,
			     char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  p_in = (const unsigned char *) in_buf;
  while (p_in < (const unsigned char *) in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  break;
	}
    }

  if (p_in >= (const unsigned char *) in_buf + in_size)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      /* a Latin1 ISO8859-1 character is encoded on maximum 2 bytes in UTF-8 */
      *out_buf = malloc (in_size * 2 + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, in_size * 2 + 1);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      if (*out_size < in_size * 2 + 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  for (p_in = (const unsigned char *) in_buf,
       p_out = (unsigned char *) *out_buf;
       p_in < (const unsigned char *) in_buf + in_size; p_in++)
    {
      unsigned char *utf8_bytes = iso8859_1_to_utf8_conv[*p_in].utf8_bytes;
      int utf8_size = iso8859_1_to_utf8_conv[*p_in].size;

      do
	{
	  *p_out++ = *utf8_bytes++;
	}
      while (--utf8_size > 0);
    }

  *(p_out) = '\0';
  *out_size = p_out - (unsigned char *) *(out_buf);

  return NO_ERROR;
}

/*
 * intl_text_utf8_to_iso8859_1() - converts a buffer containing UTF-8 text 
 *				   to ISO 8859-1 (Latin 1) encoding
 *
 *   return: error code
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(out): size of output string (NUL terminator not counted)
 */
int
intl_text_utf8_to_iso8859_1 (const char *in_buf, const int in_size,
			     char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;
  unsigned char *p_next = NULL;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  p_in = (const unsigned char *) in_buf;
  while (p_in < (const unsigned char *) in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  break;
	}
    }

  if (p_in >= (const unsigned char *) in_buf + in_size)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      *out_buf = malloc (in_size + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, in_size + 1);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      if (*out_size < in_size + 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  for (p_in = (const unsigned char *) in_buf,
       p_out = (unsigned char *) *out_buf;
       p_in < (const unsigned char *) in_buf + in_size;)
    {
      unsigned int codepoint = 0L;

      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in++;
	  continue;
	}

      codepoint = intl_utf8_to_codepoint (p_in,
					  in_buf + in_size - (char *) p_in,
					  &p_next);
      if (codepoint > 0xff)
	{
	  *p_out++ = '?';
	}
      else
	{
	  *p_out++ = (unsigned char) codepoint;
	}
      p_in = p_next;
    }

  *(p_out) = '\0';
  *out_size = p_out - (unsigned char *) *(out_buf);

  return NO_ERROR;
}

/*
 * intl_init_conv_iso8859_9_to_utf8() - initializes conversion map from 
 *				        ISO 8859-9 (turkish) to UTF-8
 *  return:
 *
 */
void
intl_init_conv_iso8859_9_to_utf8 ()
{
  unsigned int i;

  /* 00 - 7E : mapped to ASCII */
  for (i = 0; i <= 0x7e; i++)
    {
      iso8859_9_to_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_9_to_utf8_conv[i].utf8_bytes)) =
	(unsigned char) i;
    }

  /* 7F - 9F : not mapped */
  for (i = 0x7f; i <= 0x9f; i++)
    {
      iso8859_9_to_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_9_to_utf8_conv[i].utf8_bytes)) =
	(unsigned char) '?';
    }

  /* A0 - FF : mapped to Unicode codepoint with the same value */
  for (i = 0xa0; i <= 0xff; i++)
    {
      iso8859_9_to_utf8_conv[i].size =
	intl_codepoint_to_utf8 (i, iso8859_9_to_utf8_conv[i].utf8_bytes);
      memcpy (utf8_to_iso8859_9_conv[i - 0xa0].utf8_bytes,
	      iso8859_9_to_utf8_conv[i].utf8_bytes,
	      iso8859_9_to_utf8_conv[i].size);
      utf8_to_iso8859_9_conv[i - 0xa0].size = iso8859_9_to_utf8_conv[i].size;

      assert (utf8_to_iso8859_9_conv[i - 0xa0].size == UTF8_ISO8859_9_SIZE);

      utf8_to_iso8859_9_conv[i - 0xa0].val8bit = i;
    }

  /* special mapping */
  for (i = 0; i < DIM (iso8859_9_codepoints); i++)
    {
      int val8bit = iso8859_9_codepoints[i].val8bit;
      unsigned int codepoint = iso8859_9_codepoints[i].codepoint;

      iso8859_9_to_utf8_conv[val8bit].size =
	intl_codepoint_to_utf8 (codepoint,
				iso8859_9_to_utf8_conv[val8bit].utf8_bytes);
      memcpy (utf8_to_iso8859_9_conv[val8bit - 0xa0].utf8_bytes,
	      iso8859_9_to_utf8_conv[val8bit].utf8_bytes,
	      iso8859_9_to_utf8_conv[val8bit].size);
      utf8_to_iso8859_9_conv[val8bit - 0xa0].size =
	iso8859_9_to_utf8_conv[val8bit].size;
      utf8_to_iso8859_9_conv[val8bit - 0xa0].val8bit = val8bit;
    }
}

/*
 * intl_text_iso8859_9_to_utf8() - converts a buffer containing text with
 *                                 ISO8859-9 (Latin 5) encoding to UTF-8
 *
 *   return: error code
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NULL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(out): size of string (NULL terminator not included)
 */
int
intl_text_iso8859_9_to_utf8 (const char *in_buf, const int in_size,
			     char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  p_in = (const unsigned char *) in_buf;
  while (p_in < (const unsigned char *) in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  break;
	}
    }

  if (p_in >= (const unsigned char *) in_buf + in_size)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      /* a turkish ISO8859-9 character is encoded on maximum 2 bytes in UTF-8 */
      *out_buf = malloc (in_size * 2 + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, in_size * 2 + 1);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      if (*out_size < in_size * 2 + 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  for (p_in = (const unsigned char *) in_buf,
       p_out = (unsigned char *) *out_buf;
       p_in < (unsigned char *) in_buf + in_size; p_in++)
    {
      unsigned char *utf8_bytes = iso8859_9_to_utf8_conv[*p_in].utf8_bytes;
      int utf8_size = iso8859_9_to_utf8_conv[*p_in].size;

      do
	{
	  *p_out++ = *utf8_bytes++;
	}
      while (--utf8_size > 0);
    }

  *(p_out) = '\0';
  *out_size = p_out - (unsigned char *) *(out_buf);

  return NO_ERROR;
}

/*
 * intl_text_utf8_to_iso8859_9() - converts a buffer containing UTF-8 text 
 *				   to ISO 8859-9 (Latin 5) encoding
 *
 *   return: error code
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NULL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(out): size of output string (NULL terminator not counted)
 */
int
intl_text_utf8_to_iso8859_9 (const char *in_buf, const int in_size,
			     char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  p_in = (const unsigned char *) in_buf;
  while (p_in < (const unsigned char *) in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  break;
	}
    }

  if (p_in >= (const unsigned char *) in_buf + in_size)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      *out_buf = malloc (in_size + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, in_size + 1);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      if (*out_size < in_size + 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  for (p_in = (const unsigned char *) in_buf,
       p_out = (unsigned char *) *out_buf;
       p_in < (const unsigned char *) in_buf + in_size;)
    {
      int seq_no;
      bool utf8_seq_found = false;

      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in++;
	  continue;
	}

      if (p_in + UTF8_ISO8859_9_SIZE <= in_buf + in_size)
	{
	  for (seq_no = 0; seq_no < DIM (utf8_to_iso8859_9_conv); seq_no++)
	    {
	      if (!memcmp (utf8_to_iso8859_9_conv[seq_no].utf8_bytes,
			   p_in, utf8_to_iso8859_9_conv[seq_no].size))
		{
		  *p_out++ = utf8_to_iso8859_9_conv[seq_no].val8bit;
		  p_in += utf8_to_iso8859_9_conv[seq_no].size;
		  utf8_seq_found = true;
		  break;
		}
	    }
	}

      if (!utf8_seq_found)
	{
	  int dummy_utf8_char_size;

	  p_in = intl_nextchar_utf8 ((unsigned char *) p_in,
				     &dummy_utf8_char_size);
	  *p_out++ = '?';
	}
    }

  *(p_out) = '\0';
  *out_size = p_out - (unsigned char *) *(out_buf);

  return NO_ERROR;
}

/* monetary symbols */

/* UTF-8 encoding of money symbols - maps to DB_CURRENCY enum type */
static char moneysymbols_utf8[][4] = {
  "$",				/* dollar sign */
  "\xc2\xa5",			/* Japan money symbols  */
  "\xc2\xa3",			/* lira - english money symbols */
  "\xe2\x82\xa9",		/* won - Korean money symbols */
  "TL",				/* TL - Turkish money symbols */
  "\xc2\xa4"			/* generic curency symbol */
};

/* encoding (for console output) of money symbols - maps to DB_CURRENCY enum
 * type */
/* used for values printing in CSQL */
static char moneysymbols_console[][3] = {
  "$",				/* dollar sign */
  "Y",				/* japanese yen */
  "&",				/* english pound */
  "\\",				/* koreean won */
  "TL",				/* turkish lira */
  ""				/* generic currency symbol - add new symbols
				 * before this */
};

/* encoding (for grammars) of money symbols - maps to DB_CURRENCY enum type */
/* used for values printing in CSQL */
static char moneysymbols_grammar[][4] = {
  "$",				/* dollar sign */
  "\xa1\xef",			/* japanese yen */
  "&",				/* english pound */
  "\\",				/* koreean won */
  "\\TL",			/* turkish lira */
  ""				/* generic currency symbol - add new symbols
				 * before this */
};

/* ISO encoding of money symbols - maps to DB_CURRENCY enum type */
static char moneysymbols_iso_codes[][4] = {
  "USD",			/* dollar sign */
  "JPY",			/* japanese yen */
  "GBP",			/* english pound */
  "KRW",			/* koreean won */
  "TRY",			/* turkish lira */
  ""				/* generic currency symbol - add new symbols
				 * before this */
};

/*
 * intl_is_currency_symbol() - check if a string matches a currency
 *                             symbol (UTF-8)
 *   return: true if a match is found
 *   src(in): NUL terminated string
 *   currency(out): currency found
 */
bool
intl_is_currency_symbol (const char *src, DB_CURRENCY * currency,
			 int *symbol_size,
			 const CURRENCY_CHECK_MODE check_mode)
{
  int sym_currency;
  int src_len = strlen (src);

  assert (currency != NULL);
  assert (symbol_size != NULL);

  *currency = DB_CURRENCY_NULL;
  *symbol_size = 0;

  if (check_mode & CURRENCY_CHECK_MODE_ISO)
    {
      for (sym_currency = 0;
	   src_len > 0 && sym_currency < (int) DIM (moneysymbols_iso_codes);
	   sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_iso_codes[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0 &&
	      !memcmp (src, moneysymbols_iso_codes[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  if (check_mode & CURRENCY_CHECK_MODE_UTF8)
    {
      for (sym_currency = 0;
	   src_len > 0 && sym_currency < (int) DIM (moneysymbols_utf8);
	   sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_utf8[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0 &&
	      !memcmp (src, moneysymbols_utf8[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  if (check_mode & CURRENCY_CHECK_MODE_CONSOLE)
    {
      for (sym_currency = 0;
	   src_len > 0 && sym_currency < (int) DIM (moneysymbols_console);
	   sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_console[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0 &&
	      !memcmp (src, moneysymbols_console[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  /* search backwards : "\TL" (turkish lira) symbol may be
   * miss-interpreted as "\" (korean won) */
  if (check_mode & CURRENCY_CHECK_MODE_GRAMMAR)
    {
      for (sym_currency = (int) DIM (moneysymbols_grammar) - 1;
	   src_len > 0 && sym_currency >= 0; sym_currency--)
	{
	  int symbol_len = strlen (moneysymbols_grammar[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0 &&
	      !memcmp (src, moneysymbols_grammar[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  return false;
}

/*
 * intl_get_money_symbol() - returns a string representing the currency symbol
 *   return: currency symbol
 *   currency(int): currency code
 */
char *
intl_get_money_symbol (const DB_CURRENCY currency)
{
  if (currency < 0 || currency >= (int) DIM (moneysymbols_utf8))
    {
      return moneysymbols_utf8[DB_CURRENCY_NULL];
    }
  return moneysymbols_utf8[currency];
}

/*
 * intl_get_money_symbol_console() - returns a string representing the
 *				     currency symbol printable on console
 *   return: currency symbol
 *   currency(int): currency code
 */
char *
intl_get_money_symbol_console (const DB_CURRENCY currency)
{
  if (currency < 0 || currency >= (int) DIM (moneysymbols_console))
    {
      return moneysymbols_console[DB_CURRENCY_NULL];
    }
  return moneysymbols_console[currency];
}

/*
 * intl_get_money_symbol_grammar() - returns a string representing the
 *				     currency symbol recognizable by grammar
 *   return: currency symbol
 *   currency(int): currency code
 */
char *
intl_get_money_symbol_grammar (const DB_CURRENCY currency)
{
  if (currency < 0 || currency >= (int) DIM (moneysymbols_grammar))
    {
      return moneysymbols_grammar[DB_CURRENCY_NULL];
    }
  return moneysymbols_grammar[currency];
}

/*
 * intl_get_currency_symbol_position() - returns an indication of the position
 *					 of currency symbol symbol when
 *					 is printed
 *   return: position indicator : 0 : before value, 1 : after value
 *   currency(int): currency code
 *
 *  Note : currently ony the turkish lira is printed after the value
 */
int
intl_get_currency_symbol_position (const DB_CURRENCY currency)
{
  if (currency == DB_CURRENCY_TL)
    {
      return 1;
    }

  return 0;
}
