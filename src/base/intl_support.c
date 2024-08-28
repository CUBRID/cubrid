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
#include "charset_converters.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

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

#if defined (ENABLE_UNUSED_FUNCTION)
/* EUC-KR characters may be used with ISO-88591-1 charset when
 * PRM_SINGLE_BYTE_COMPARE is 'no'
 * EUC-KR have either 3 (when first byte is SS3) or two bytes (use this macro
 * to check the byte range) */
#define IS_PSEUDO_KOREAN(ch) \
          ( ((unsigned char) ch >= (unsigned char) 0xa1)       \
              && ((unsigned char) ch <= (unsigned char) 0xfe) )
#endif

/* conversion from turkish ISO 8859-9 to UTF-8 */
#define ISO_8859_9_FIRST_CP 0x11e
#define ISO_8859_9_LAST_CP 0x15f

static CONV_CP_TO_BYTES iso8859_9_To_utf8_conv[256];
static CONV_CP_TO_BYTES utf8_Cp_to_iso_8859_9_conv[ISO_8859_9_LAST_CP - ISO_8859_9_FIRST_CP + 1];

/* conversion from Latin 1 ISO 8859-1 to UTF-8: */
static CONV_CP_TO_BYTES iso8859_1_To_utf8_conv[256];


/* identifiers : support for multibyte chars in INTL_CODESET_ISO88591 codeset
 * (default legacy codeset) */
bool intl_Mbs_support = true;
bool intl_String_validation = false;

/* General EUC string manipulations */
static int intl_tolower_euc (const unsigned char *src, unsigned char *d, int byte_size);
static int intl_toupper_euc (const unsigned char *src, unsigned char *d, int byte_size);
static int intl_count_euc_chars (const unsigned char *s, int length_in_bytes);
static int intl_count_euc_bytes (const unsigned char *s, int length_in_chars);
#if defined (ENABLE_UNUSED_FUNCTION)
static wchar_t *intl_copy_lowercase (const wchar_t * ws, size_t n);
static int intl_is_korean (unsigned char ch);
#endif /* ENABLE_UNUSED_FUNCTION */

/* UTF-8 string manipulations */
static int intl_tolower_utf8 (const ALPHABET_DATA * a, const unsigned char *s, unsigned char *d, int length_in_chars,
			      int *d_size);
static int intl_toupper_utf8 (const ALPHABET_DATA * a, const unsigned char *s, unsigned char *d, int length_in_chars,
			      int *d_size);
static int intl_count_utf8_bytes (const unsigned char *s, int length_in_chars);
static int intl_char_tolower_utf8 (const ALPHABET_DATA * a, const unsigned char *s, const int size, unsigned char *d,
				   unsigned char **next);
static int intl_char_toupper_utf8 (const ALPHABET_DATA * a, const unsigned char *s, const int size, unsigned char *d,
				   unsigned char **next);
static int intl_strcasecmp_utf8_one_cp (const ALPHABET_DATA * alphabet, unsigned char *str1, unsigned char *str2,
					const int size_str1, const int size_str2, unsigned int cp1, unsigned int cp2,
					int *skip_size1, int *skip_size2);
static void intl_init_conv_iso8859_9_to_utf8 (void);
static void intl_init_conv_iso8859_1_to_utf8 (void);


TEXT_CONVERSION con_Iso_8859_9_conv = {
  TEXT_CONV_ISO_88599_BUILTIN,	/* type */
  (char *) "28599",		/* Windows Code page */
  (char *) "iso88599",		/* Linux charset identifiers */
  {0},				/* byte flags : not used for ISO */
  0, 0, NULL,			/* UTF-8 to console : filled by init function */
  0, 0, NULL,			/* console to UTF-8 : filled by init function */
  intl_text_utf8_to_single_byte,	/* UTF-8 to console conversion function */
  intl_text_single_byte_to_utf8,	/* console to UTF-8 conversion function */
  intl_init_conv_iso8859_9_to_utf8,	/* init function */
};

TEXT_CONVERSION con_Iso_8859_1_conv = {
  TEXT_CONV_ISO_88591_BUILTIN,	/* type */
  (char *) "28591",		/* Windows Code page */
  (char *) "iso88591",		/* Linux charset identifiers */
  {0},				/* byte flags : not used for ISO */
  0, 0, NULL,			/* UTF-8 to console : filled by init function */
  0, 0, NULL,			/* console to UTF-8 : filled by init function */
  intl_text_utf8_to_single_byte,	/* UTF-8 to console conversion function */
  intl_text_single_byte_to_utf8,	/* console to UTF-8 conversion function */
  intl_init_conv_iso8859_1_to_utf8,	/* init function */
};


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

  for (nbytes = 0; (nbytes = mbtowc (&cur_wc, mbs, MB_LEN_MAX)) > 0 && cur_wc != L'\0' && cur_wc != wc; mbs += nbytes)
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

  for (num_of_chars = 0; (clen = mblen (mbs, MB_LEN_MAX)) > 0 && *mbs; mbs += clen, num_of_chars++)
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
      if (strlen (mbs) < (int) n)
	{
	  errno = EINVAL;
	  return NULL;
	}
      return &mbs[n];
    }

  for (num_of_chars = 0, clen = 0; num_of_chars < n && (clen = mblen (mbs, MB_LEN_MAX)) > 0 && *mbs;
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

  for (size = 0; (clen = mbtowc (&wc, mbs, MB_LEN_MAX)) > 0 && *mbs && wcschr (chars, wc); mbs += clen, size += clen)
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

  for (mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX), mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
       mb1_len > 0 && mb2_len > 0 && wc1 && wc2 && !(towlower (wc1) - towlower (wc2));)
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

  for (mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX), mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
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

  for (num_of_chars = 1, mb1_len = mbtowc (&wc1, mbs1, MB_LEN_MAX), mb2_len = mbtowc (&wc2, mbs2, MB_LEN_MAX);
       mb1_len > 0 && mb2_len > 0 && wc1 && wc2 && num_of_chars < n && !(towlower (wc1) - towlower (wc2));
       num_of_chars++)
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

  for (num_of_bytes = 0, clen = mblen (mbs2, MB_LEN_MAX), dest = mbs1; clen > 0 && (num_of_bytes + clen) <= n - 1;
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
      intl_char_count ((unsigned char *) mbs1, length_in_bytes, lang_charset (), &char_count);
      intl_lower_string ((unsigned char *) mbs1, (unsigned char *) mbs2, char_count, lang_charset ());
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
      intl_char_count ((unsigned char *) src, length_in_bytes, lang_charset (), &char_count);
      intl_lower_string ((unsigned char *) src, (unsigned char *) dest, char_count, lang_charset ());
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
      intl_char_count ((unsigned char *) mbs1, length_in_bytes, lang_charset (), &char_count);
      intl_upper_string ((unsigned char *) mbs1, (unsigned char *) mbs2, char_count, lang_charset ());
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
 * ISO 8859-1 encoding functions
 */

/*
 * intl_tolower_iso8859() - replaces all upper case ISO88591 characters
 *                          with their lower case codes.
 *   return: character counts
 *   s(in/out): string to lowercase
 *   length(in): length of the string
 */
int
intl_tolower_iso8859 (unsigned char *s, int length)
{
  int char_count = length;
  unsigned char *end;

  assert (s != NULL);

  for (end = s + length; s < end; s++)
    {
      *s = char_tolower_iso8859 (*s);
    }

  return char_count;
}

/*
 * intl_toupper_iso8859() - replaces all lower case ISO88591 characters
 *                          with their upper case codes.
 *   return: character counts
 *   s(in/out): string to uppercase
 *   length(in): length of the string
 */
int
intl_toupper_iso8859 (unsigned char *s, int length)
{
  int char_count = length;
  unsigned char *end;

  assert (s != NULL);

  for (end = s + length; s < end; s++)
    {
      *s = char_toupper_iso8859 (*s);
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
const unsigned char *
intl_nextchar_euc (const unsigned char *s, int *curr_char_length)
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
 *   s_start(in) : start of buffer string
 *   prev_char_length(out): length of the previous character
 */
const unsigned char *
intl_prevchar_euc (const unsigned char *s, const unsigned char *s_start, int *prev_char_length)
{
  assert (s != NULL);
  assert (s > s_start);

  if (s - 3 >= s_start && *(s - 3) == SS3)
    {
      *prev_char_length = 3;
      return s - 3;
    }
  else if (s - 2 >= s_start && IS_8BIT (*(s - 2)))
    {
      *prev_char_length = 2;
      return s - 2;
    }

  *prev_char_length = 1;
  return --s;
}

/*
 * intl_tolower_euc() - Replaces all upper case ASCII characters inside an EUC
 *                      encoded string with their lower case codes.
 *   return: character counts
 *   src(in): EUC string to lowercase
 *   byte_size(in): size in bytes of source string
 */
static int
intl_tolower_euc (const unsigned char *src, unsigned char *d, int byte_size)
{
  int byte_count;
  const unsigned char *s = src;

  assert (src != NULL);

  for (byte_count = 0; byte_count < byte_size; byte_count++)
    {
      *d = char_tolower (*s);
      s++;
      d++;
    }

  return intl_count_euc_chars (src, byte_size);
}

/*
 * intl_toupper_euc() - Replaces all upper case ASCII characters inside an EUC
 *                      encoded string with their upper case codes.
 *   return: character counts
 *   src(in): EUC string to uppercase
 *   byte_size(in): size in bytes of source string
 */
static int
intl_toupper_euc (const unsigned char *src, unsigned char *d, int byte_size)
{
  int byte_count;
  const unsigned char *s = src;

  assert (src != NULL);

  for (byte_count = 0; byte_count < byte_size; byte_count++)
    {
      *d = char_toupper (*s);
      s++;
      d++;
    }

  return intl_count_euc_chars (src, byte_size);;
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
intl_count_euc_chars (const unsigned char *s, int length_in_bytes)
{
  const unsigned char *end;
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
intl_count_euc_bytes (const unsigned char *s, int length_in_chars)
{
  int char_count;
  int char_width;
  int byte_count;

  assert (s != NULL);

  for (char_count = 0, byte_count = 0; char_count < length_in_chars; char_count++)
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
intl_convert_charset (const unsigned char *src, int length_in_chars, INTL_CODESET src_codeset, unsigned char *dest,
		      INTL_CODESET dest_codeset, int *unconverted)
{
  int error_code = NO_ERROR;

  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_KSC5601_EUC:
    case INTL_CODESET_UTF8:
    case INTL_CODESET_RAW_BYTES:
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
intl_char_count (const unsigned char *src, int length_in_bytes, INTL_CODESET src_codeset, int *char_count)
{
  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      *char_count = length_in_bytes;
      break;

    case INTL_CODESET_KSC5601_EUC:
      *char_count = intl_count_euc_chars (src, length_in_bytes);
      break;

    case INTL_CODESET_UTF8:
      *char_count = intl_count_utf8_chars (src, length_in_bytes);
      break;

    default:
      assert (false);
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
 *   bytes_count(out): number of byets used for encode the number of
 *                     characters specified
 *
 * Note: Embedded NULL's are counted as characters.
 */
int
intl_char_size (const unsigned char *src, int length_in_chars, INTL_CODESET src_codeset, int *byte_count)
{
  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      *byte_count = length_in_chars;
      break;

    case INTL_CODESET_KSC5601_EUC:
      *byte_count = intl_count_euc_bytes (src, length_in_chars);
      break;

    case INTL_CODESET_UTF8:
      *byte_count = intl_count_utf8_bytes (src, length_in_chars);
      break;

    default:
      assert (false);
      *byte_count = 0;
      break;
    }

  return *byte_count;
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
intl_char_size_pseudo_kor (const unsigned char *src, int length_in_chars, INTL_CODESET src_codeset, int *byte_count)
{
  switch (src_codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!prm_get_bool_value (PRM_ID_SINGLE_BYTE_COMPARE))
	{
	  int b_count = 0;
	  while (length_in_chars-- > 0)
	    {
	      if (*src == SS3)
		{
		  b_count += 3;
		  src += 3;
		}
	      else if (IS_PSEUDO_KOREAN (*src))
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
      assert (false);
      *byte_count = 0;
      break;
    }

  return *byte_count;
}
#endif

/*
 * intl_prev_char() - returns pointer to the previous char in string
 *
 *   return : pointer to previous character
 *   s(in) : string
 *   s_start(in) : start of buffer string
 *   codeset(in) : enumeration of src codeset
 *   prev_char_size(out) : size of previous character
 */
const unsigned char *
intl_prev_char (const unsigned char *s, const unsigned char *s_start, INTL_CODESET codeset, int *prev_char_size)
{
  assert (s > s_start);

  switch (codeset)
    {
    case INTL_CODESET_KSC5601_EUC:
      return intl_prevchar_euc (s, s_start, prev_char_size);

    case INTL_CODESET_UTF8:
      return intl_prevchar_utf8 (s, s_start, prev_char_size);

    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      break;
    default:
      assert (false);
    }

  *prev_char_size = 1;
  return --s;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * intl_prev_char_pseudo_kor() - returns pointer to the previous char in
 *				 string
 *
 *   return : pointer to previous character
 *   s(in) : string
 *   s_start(in) : start of buffer string
 *   codeset(in) : enumeration of src codeset
 *   prev_char_size(out) : size of previous character
 *
 * Note: This is similar to 'intl_prev_char' except with INTL_CODESET_ISO88591
 *	 codeset, some bytes are considered korean characters
 *	 This function is used in context of some specific string functions.
 */
unsigned char *
intl_prev_char_pseudo_kor (const unsigned char *s, const unsigned char *s_start, INTL_CODESET codeset,
			   int *prev_char_size)
{
  assert (s > s_start);

  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!prm_get_bool_value (PRM_ID_SINGLE_BYTE_COMPARE) && IS_PSEUDO_KOREAN (*(s - 1)))
	{
	  if (s - 2 >= s_start && *(s - 2) == SS3)
	    {
	      *prev_char_size = 3;
	      return s - 3;
	    }
	  else if (s - 1 >= s_start && IS_PSEUDO_KOREAN (*(s - 1)))
	    {
	      *prev_char_size = 2;
	      return s - 2;
	    }
	}

      break;

    case INTL_CODESET_KSC5601_EUC:
      return intl_prevchar_euc (s, s_start, prev_char_size);

    case INTL_CODESET_UTF8:
      return intl_prevchar_utf8 (s, s_start, prev_char_size);

    default:
      assert (false);
    }

  *prev_char_size = 1;
  return --s;
}
#endif

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
const unsigned char *
intl_next_char (const unsigned char *s, INTL_CODESET codeset, int *current_char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      *current_char_size = 1;
      return ++s;

    case INTL_CODESET_KSC5601_EUC:
      return intl_nextchar_euc (s, current_char_size);

    case INTL_CODESET_UTF8:
      return intl_nextchar_utf8 (s, current_char_size);

    default:
      assert (false);
      *current_char_size = 0;
      return s;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
intl_next_char_pseudo_kor (const unsigned char *s, INTL_CODESET codeset, int *current_char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!prm_get_bool_value (PRM_ID_SINGLE_BYTE_COMPARE) && IS_PSEUDO_KOREAN (*s))
	{
	  if (*s == SS3)
	    {
	      *current_char_size = 3;
	      return s + 3;
	    }
	  else if (IS_PSEUDO_KOREAN (*s))
	    {
	      *current_char_size = 2;
	      return s + 2;
	    }
	}

      *current_char_size = 1;
      return ++s;

    case INTL_CODESET_KSC5601_EUC:
      return intl_nextchar_euc (s, current_char_size);

    case INTL_CODESET_UTF8:
      return intl_nextchar_utf8 (s, current_char_size);

    default:
      assert (false);
      *current_char_size = 0;
      return s;
    }
}
#endif

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
intl_cmp_char (const unsigned char *s1, const unsigned char *s2, INTL_CODESET codeset, int *char_size)
{

  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      *char_size = 1;
      return *s1 - *s2;

    case INTL_CODESET_KSC5601_EUC:
      (void) intl_nextchar_euc (s1, char_size);
      return memcmp (s1, s2, *char_size);

    case INTL_CODESET_UTF8:
      *char_size = intl_Len_utf8_char[*s1];
      return memcmp (s1, s2, *char_size);

    default:
      assert (false);
      *char_size = 1;
      return 0;
    }

  return 0;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * intl_cmp_char_pseudo_kor() - compares the first character of two strings
 *   return: zero if character are equal, non-zero otherwise
 *   s1(in):
 *   s2(in):
 *   codeset:
 *   char_size(out): size of char in bytes of the first character in s1
 *
 *  Note: same as intl_cmp_char, except that with ISO-8859-1 codeset, some
 *	  bytes are handled as Korean characters.
 *
 */
int
intl_cmp_char_pseudo_kor (const unsigned char *s1, const unsigned char *s2, INTL_CODESET codeset, int *char_size)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      if (!prm_get_bool_value (PRM_ID_SINGLE_BYTE_COMPARE) && IS_PSEUDO_KOREAN (*s1))
	{
	  if (*s1 == SS3)
	    {
	      *char_size = 3;
	      return memcmp (s1, s2, 3);
	    }
	  else if (IS_PSEUDO_KOREAN (*s1))
	    {
	      *char_size = 2;
	      return memcmp (s1, s2, 2);
	    }
	}
      *char_size = 1;
      return *s1 - *s2;

    case INTL_CODESET_KSC5601_EUC:
      (void) intl_nextchar_euc ((unsigned char *) s1, char_size);
      return memcmp (s1, s2, *char_size);

    case INTL_CODESET_UTF8:
      *char_size = intl_Len_utf8_char[*s1];
      return memcmp (s1, s2, *char_size);

    default:
      assert (false);
      *char_size = 1;
      return 0;
    }

  return 0;
}

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
      if (!prm_get_bool_value (PRM_ID_SINGLE_BYTE_COMPARE) && IS_PSEUDO_KOREAN (*s1) && IS_PSEUDO_KOREAN (*s2))
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
      else if ((prm_get_bool_value (PRM_ID_SINGLE_BYTE_COMPARE) || !IS_PSEUDO_KOREAN (*s1)) && *s1 == *s2)
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
intl_pad_char (const INTL_CODESET codeset, unsigned char *pad_char, int *pad_size)
{
  switch (codeset)
    {
    case INTL_CODESET_RAW_BITS:
    case INTL_CODESET_RAW_BYTES:
      pad_char[0] = '\0';
      *pad_size = 1;
      break;

    case INTL_CODESET_KSC5601_EUC:
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
      assert (false);
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
    case INTL_CODESET_KSC5601_EUC:
      size = 2;
      break;
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_UTF8:
    case INTL_CODESET_RAW_BYTES:
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
 *   alphabet(in): alphabet data
 *   src(in): string to uppercase
 *   src_size(in): buffer size
 *   src_length(in): length of the string measured in characters
 */
int
intl_upper_string_size (const ALPHABET_DATA * alphabet, const unsigned char *src, int src_size, int src_length)
{
  int char_count;
  int req_size = src_size;

  assert (alphabet != NULL);

  switch (alphabet->codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      break;

    case INTL_CODESET_KSC5601_EUC:
      break;

    case INTL_CODESET_UTF8:
      {
	unsigned char upper[INTL_UTF8_MAX_CHAR_SIZE];
	unsigned char *next = NULL;

	req_size = 0;
	for (char_count = 0; char_count < src_length && src_size > 0; char_count++)
	  {
	    req_size += intl_char_toupper_utf8 (alphabet, src, src_size, upper, &next);
	    src_size -= CAST_STRLEN (next - src);
	    src = next;
	  }
      }
      break;

    default:
      assert (false);
      break;
    }

  return req_size;
}

/*
 * intl_upper_string() - replace all lower case characters with their
 *                       upper case characters
 *   return: character counts
 *   alphabet(in): alphabet data
 *   src(in/out): string source to uppercase
 *   dst(in/out): output string
 *   length_in_chars(in): length of the string measured in characters
 */
int
intl_upper_string (const ALPHABET_DATA * alphabet, const unsigned char *src, unsigned char *dst, int length_in_chars)
{
  int char_count = 0;

  assert (alphabet != NULL);

  switch (alphabet->codeset)
    {
    case INTL_CODESET_RAW_BYTES:
      memcpy (dst, src, length_in_chars);
      char_count = length_in_chars;
      break;

    case INTL_CODESET_ISO88591:
      {
	unsigned char *d;
	const unsigned char *s;

	for (d = dst, s = src; d < dst + length_in_chars; d++, s++)
	  {
	    *d = char_toupper_iso8859 (*s);
	  }
	char_count = length_in_chars;
      }
      break;

    case INTL_CODESET_KSC5601_EUC:
      {
	int byte_count;
	intl_char_size (src, length_in_chars, INTL_CODESET_KSC5601_EUC, &byte_count);
	if (byte_count > 0)
	  {
	    char_count = intl_toupper_euc (src, dst, byte_count);
	  }
      }
      break;

    case INTL_CODESET_UTF8:
      {
	int dummy_size;
	char_count = intl_toupper_utf8 (alphabet, src, dst, length_in_chars, &dummy_size);
      }
      break;

    default:
      assert (false);
      break;
    }

  return char_count;
}

/*
 * intl_lower_string_size() - determine the size required for holding
 *			     lower case of the input string
 *   return: required size
 *   alphabet(in): alphabet data
 *   src(in): string to lowercase
 *   src_size(in): buffer size
 *   src_length(in): length of the string measured in characters
 */
int
intl_lower_string_size (const ALPHABET_DATA * alphabet, const unsigned char *src, int src_size, int src_length)
{
  int char_count;
  int req_size = src_size;

  assert (alphabet != NULL);

  switch (alphabet->codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      break;

    case INTL_CODESET_KSC5601_EUC:
      break;

    case INTL_CODESET_UTF8:
      {
	unsigned char lower[INTL_UTF8_MAX_CHAR_SIZE];
	unsigned char *next;

	req_size = 0;
	for (char_count = 0; char_count < src_length && src_size > 0; char_count++)
	  {
	    req_size += intl_char_tolower_utf8 (alphabet, src, src_size, lower, &next);
	    src_size -= CAST_STRLEN (next - src);
	    src = next;
	  }
      }
      break;

    default:
      assert (false);
      break;
    }

  return req_size;
}

/*
 * intl_lower_string() - replace all upper case characters with their
 *                      lower case characters
 *   return: character counts
 *   alphabet(in): alphabet data
 *   src(in/out): string to lowercase
 *   dst(out): output string
 *   length_in_chars(in): length of the string measured in characters
 */
int
intl_lower_string (const ALPHABET_DATA * alphabet, const unsigned char *src, unsigned char *dst, int length_in_chars)
{
  int char_count = 0;

  assert (alphabet != NULL);

  switch (alphabet->codeset)
    {
    case INTL_CODESET_ISO88591:
      {
	unsigned char *d;
	const unsigned char *s;

	for (d = dst, s = src; d < dst + length_in_chars; d++, s++)
	  {
	    *d = char_tolower_iso8859 (*s);
	  }
	char_count = length_in_chars;
      }
      break;

    case INTL_CODESET_RAW_BYTES:
      memcpy (dst, src, length_in_chars);
      break;

    case INTL_CODESET_KSC5601_EUC:
      {
	int byte_count;
	intl_char_size (src, length_in_chars, INTL_CODESET_KSC5601_EUC, &byte_count);
	if (byte_count > 0)
	  {
	    char_count = intl_tolower_euc (src, dst, byte_count);
	  }
      }
      break;

    case INTL_CODESET_UTF8:
      {
	int dummy_size;
	char_count = intl_tolower_utf8 (alphabet, src, dst, length_in_chars, &dummy_size);
      }
      break;

    default:
      assert (false);
      break;
    }

  return char_count;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * intl_is_korean() - test for a korean character
 *   return: non-zero if ch is a korean character,
 *           0 otherwise.
 *   ch(in): the character to be tested
 */
static int
intl_is_korean (unsigned char ch)
{
  if (prm_get_bool_value (PRM_ID_SINGLE_BYTE_COMPARE))
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
intl_zone (int category)
{
  switch (lang_id ())
    {
    case INTL_LANG_ENGLISH:
      return INTL_ZONE_US;
    case INTL_LANG_KOREAN:
      return INTL_ZONE_KR;
    default:
      return INTL_ZONE_US;
    }
  return INTL_ZONE_US;
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
intl_reverse_string (const unsigned char *src, unsigned char *dst, int length_in_chars, int size_in_bytes,
		     INTL_CODESET codeset)
{
  const unsigned char *end, *s;
  unsigned char *d;
  int char_count = 0;
  int char_size, i;

  assert (src != NULL);
  assert (dst != NULL);

  s = src;

  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
      d = dst + length_in_chars - 1;
      end = src + length_in_chars;
      for (; s < end; char_count++)
	{
	  *d = *s;
	  s++;
	  d--;
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
      assert (false);
      break;
    }

  return char_count;
}

/*
 * intl_is_max_bound_chr () -
 *
 * return: check if chr points to a char representing the upper bound
 *	   codepoint in the selected codeset, for LIKE index optimization.
 *
 * codeset(in) : the codeset to consider
 * chr(in) : upper bound, as bytes
 */
bool
intl_is_max_bound_chr (INTL_CODESET codeset, const unsigned char *chr)
{
  switch (codeset)
    {
    case INTL_CODESET_UTF8:
      if ((*chr == 0xf4) && (*(chr + 1) == 0x8f) && (*(chr + 2) == 0xbf) && (*(chr + 3) == 0xbf))
	{
	  return true;
	}
      return false;
    case INTL_CODESET_KSC5601_EUC:
      if (((*chr == 0xff) && (*(chr + 1) == 0xff)))
	{
	  return true;
	}
      return false;
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
    default:
      if (*chr == 0xff)
	{
	  return true;
	}
      return false;
    }

  return false;
}

/*
 * intl_is_min_bound_chr () -
 *
 * return: check if chr points to a ISO char / UTF-8 codepoint representing
 *	   the lower bound codepoint in the selected codeset, for LIKE
 *         index optimization.
 *
 * codeset(in) : the codeset to consider
 * chr(in) : upper bound, as UTF-8 bytes
 *
 * Note: 'chr' buffer should be able to store at least 1 more byte, for
 *	  one space char.
 */
bool
intl_is_min_bound_chr (INTL_CODESET codeset, const unsigned char *chr)
{
  if (*chr == ' ')
    {
      return true;
    }

  return false;
}

/*
 * intl_set_min_bound_chr () - sets chr to a byte array representing
 *			       the lowest bound codepoint in the selected
 *			       codeset, for LIKE index optimization.
 *
 * return: the number of bytes added to chr
 *
 * codeset(in) : the codeset to consider
 * chr(in) : char pointer where to place the bound, as UTF-8 bytes
 */
int
intl_set_min_bound_chr (INTL_CODESET codeset, char *chr)
{
  *chr = ' ';

  return 1;
}

/*
 * intl_set_max_bound_chr () - sets chr to a byte array representing
 *			       the up-most bound codepoint in the selected
 *			       codeset, for LIKE index optimization.
 *
 * return: the number of bytes added to chr
 *
 * codeset(in) : the codeset to consider
 * chr(in) : char pointer where to place the bound
 *
 * Note: 'chr' buffer should be able to store at least one more char:
 *	 4 bytes (UTF-8), 2 bytes (EUC-KR), 1 byte (ISO-8859-1).
 *
 */
int
intl_set_max_bound_chr (INTL_CODESET codeset, char *chr)
{
  switch (codeset)
    {
    case INTL_CODESET_UTF8:
      *chr = (char) 0xf4;
      *(chr + 1) = (char) 0x8f;
      *(chr + 2) = (char) 0xbf;
      *(chr + 3) = (char) 0xbf;
      return 4;
    case INTL_CODESET_KSC5601_EUC:
      *chr = (char) 0xff;
      *(chr + 1) = (char) 0xff;
      return 2;
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
    default:
      *chr = (char) 0xff;
      return 1;
    }

  return 1;
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
const unsigned char *
intl_nextchar_utf8 (const unsigned char *s, int *curr_char_length)
{
  INTL_GET_NEXTCHAR_UTF8 (s, *curr_char_length);
  return s;
}

/*
 * intl_prevchar_utf8() - returns a pointer to the previous character in the
 *                   UTF-8 encoded string.
 *   return: pointer to the previous character
 *   s(in): string
 *   s_start(in) : start of buffer string
 *   prev_char_length(out): length of the previous character
 */
const unsigned char *
intl_prevchar_utf8 (const unsigned char *s, const unsigned char *s_start, int *prev_char_length)
{
  int l = 0;

  do
    {
      l++;
    }
  while (l < 6 && s - l >= s_start && (*(s - l) & 0xc0) == 0x80);

  l = (*(s - l) & 0xc0) == 0x80 ? 1 : l;
  s -= l;
  *prev_char_length = l;

  return s;
}

/*
 * intl_tolower_utf8() - Replaces all upper case characters inside an UTF-8
 *			 encoded string with their lower case codes.
 *   return: character counts
 *   alphabet(in): alphabet to use
 *   s(in): UTF-8 string to lowercase
 *   d(out): output string
 *   length_in_chars(in): length of the string measured in characters
 *   d_size(out): size in bytes of destination
 */
static int
intl_tolower_utf8 (const ALPHABET_DATA * alphabet, const unsigned char *s, unsigned char *d, int length_in_chars,
		   int *d_size)
{
  int char_count, size;
  int s_size;
  unsigned char *next = NULL;

  assert (s != NULL);
  assert (d_size != NULL);

  intl_char_size (s, length_in_chars, INTL_CODESET_UTF8, &s_size);
  *d_size = 0;

  for (char_count = 0; char_count < length_in_chars; char_count++)
    {
      if (s_size <= 0)
	{
	  break;
	}
      size = intl_char_tolower_utf8 (alphabet, s, s_size, d, &next);
      d += size;
      *d_size += size;

      s_size -= CAST_STRLEN (next - s);
      s = next;
    }

  return char_count;
}

/*
 * intl_toupper_utf8() - Replaces all lower case characters inside an UTF-8
 *			 encoded string with their upper case codes.
 *   return: character counts
 *   alphabet(in): alphabet to use
 *   s(in): UTF-8 string to uppercase
 *   d(out): output string
 *   length_in_chars(in): length of the string measured in characters
 *   d_size(out): size in bytes of destination
 */
static int
intl_toupper_utf8 (const ALPHABET_DATA * alphabet, const unsigned char *s, unsigned char *d, int length_in_chars,
		   int *d_size)
{
  int char_count, size;
  int s_size;
  unsigned char *next = NULL;

  assert (s != NULL);
  assert (d_size != NULL);

  intl_char_size (s, length_in_chars, INTL_CODESET_UTF8, &s_size);
  *d_size = 0;

  for (char_count = 0; char_count < length_in_chars; char_count++)
    {
      if (s_size <= 0)
	{
	  break;
	}
      size = intl_char_toupper_utf8 (alphabet, s, s_size, d, &next);
      d += size;
      *d_size += size;

      s_size -= CAST_STRLEN (next - s);
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
int
intl_count_utf8_chars (const unsigned char *s, int length_in_bytes)
{
  const unsigned char *end;
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
intl_count_utf8_bytes (const unsigned char *s, int length_in_chars)
{
  int char_count;
  int char_width;
  int byte_count;

  assert (s != NULL);

  for (char_count = 0, byte_count = 0; char_count < length_in_chars; char_count++)
    {
      s = intl_nextchar_utf8 (s, &char_width);
      byte_count += char_width;
    }

  return byte_count;
}

/*
 * intl_char_tolower_utf8() - convert uppercase character to lowercase
 *   return: size of UTF-8 lowercase character corresponding to the argument
 *   alphabet(in): alphabet to use
 *   s (in): the UTF-8 buffer holding character to be converted
 *   size(in): size of UTF-8 buffer
 *   d (out): output buffer
 *   next (out): pointer to next character
 *
 *  Note : allocated size of 'd' is assumed to be large enough to fit any
 *	   UTF-8 character
 */
static int
intl_char_tolower_utf8 (const ALPHABET_DATA * alphabet, const unsigned char *s, const int size, unsigned char *d,
			unsigned char **next)
{
  unsigned int cp = intl_utf8_to_cp (s, size, next);

  assert (alphabet != NULL);

  if (cp < (unsigned int) (alphabet->l_count))
    {
      if (alphabet->lower_multiplier == 1)
	{
	  unsigned int lower_cp = alphabet->lower_cp[cp];

	  return intl_cp_to_utf8 (lower_cp, d);
	}
      else
	{
	  const unsigned int *case_p;
	  int count = 0;
	  int bytes;
	  int total_bytes = 0;

	  assert (alphabet->lower_multiplier > 1 && alphabet->lower_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);

	  case_p = &(alphabet->lower_cp[cp * alphabet->lower_multiplier]);

	  do
	    {
	      bytes = intl_cp_to_utf8 (*case_p, d);
	      d += bytes;
	      total_bytes += bytes;
	      case_p++;
	      count++;
	    }
	  while (count < alphabet->lower_multiplier && *case_p != 0);

	  return total_bytes;
	}
    }
  else if (cp == 0xffffffff)
    {
      /* this may happen when UTF-8 text validation is disabled (by default) */
      *d = *s;
      return 1;
    }

  return intl_cp_to_utf8 (cp, d);
}

/*
 * intl_char_toupper_utf8() - convert lowercase character to uppercase
 *   return: size of UTF-8 uppercase character corresponding to the argument
 *   alphabet(in): alphabet to use
 *   s (in): the UTF-8 buffer holding character to be converted
 *   size(in): size of UTF-8 buffer
 *   d (out): output buffer
 *   next (out): pointer to next character
 *
 *  Note : allocated size of 'd' is assumed to be large enough to fit any
 *	   UTF-8 character
 */
static int
intl_char_toupper_utf8 (const ALPHABET_DATA * alphabet, const unsigned char *s, const int size, unsigned char *d,
			unsigned char **next)
{
  unsigned int cp = intl_utf8_to_cp (s, size, next);

  assert (alphabet != NULL);

  if (cp < (unsigned int) (alphabet->l_count))
    {
      if (alphabet->upper_multiplier == 1)
	{
	  unsigned upper_cp = alphabet->upper_cp[cp];

	  return intl_cp_to_utf8 (upper_cp, d);
	}
      else
	{
	  const unsigned int *case_p;
	  int count = 0;
	  int bytes;
	  int total_bytes = 0;

	  assert (alphabet->upper_multiplier > 1 && alphabet->upper_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);

	  case_p = &(alphabet->upper_cp[cp * alphabet->upper_multiplier]);
	  do
	    {
	      bytes = intl_cp_to_utf8 (*case_p, d);
	      d += bytes;
	      total_bytes += bytes;
	      case_p++;
	      count++;
	    }
	  while (count < alphabet->upper_multiplier && *case_p != 0);

	  return total_bytes;
	}
    }
  else if (cp == 0xffffffff)
    {
      /* this may happen when UTF-8 text validation is disabled (by default) */
      *d = *s;
      return 1;
    }

  return intl_cp_to_utf8 (cp, d);
}

/*
 * intl_identifier_casecmp_w_size()
 *   return:  0 if strings are equal, -1 if str1 < str2 , 1 if str1 > str2
 *   str1(in):
 *   str2(in):
 *   size_str1(in): size in bytes of str1
 *   size_str2(in): size in bytes of str2
 *
 */
int
intl_identifier_casecmp_w_size (const INTL_LANG lang_id, unsigned char *str1, unsigned char *str2, const int size_str1,
				const int size_str2)
{
#if INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER <= 1
  if (size_str1 != size_str2)
    {
      return (size_str1 < size_str2) ? -1 : 1;
    }
#endif

  switch (lang_charset ())
    {
    case INTL_CODESET_UTF8:
      {
	unsigned char *str1_end, *str2_end;
	unsigned char *dummy;
	unsigned int cp1, cp2;
	const LANG_LOCALE_DATA *loc = lang_get_specific_locale (lang_id, INTL_CODESET_UTF8);
	const ALPHABET_DATA *alphabet;

	assert (loc != NULL);

	alphabet = &(loc->ident_alphabet);

	str1_end = str1 + size_str1;
	str2_end = str2 + size_str2;

	for (; str1 < str1_end && str2 < str2_end;)
	  {
	    int skip_size1 = 0, skip_size2 = 0;
	    int res;

	    cp1 = intl_utf8_to_cp (str1, CAST_STRLEN (str1_end - str1), &dummy);
	    cp2 = intl_utf8_to_cp (str2, CAST_STRLEN (str2_end - str2), &dummy);

	    res =
	      intl_strcasecmp_utf8_one_cp (alphabet, str1, str2, CAST_STRLEN (str1_end - str1),
					   CAST_STRLEN (str2_end - str2), cp1, cp2, &skip_size1, &skip_size2);

	    if (res != 0)
	      {
		return res;
	      }

	    str1 += skip_size1;
	    str2 += skip_size2;
	  }

	return (str1 < str1_end) ? 1 : ((str2 < str2_end) ? -1 : 0);
      }
      break;

    case INTL_CODESET_ISO88591:
      {
	unsigned char *str1_end, *str2_end;
	unsigned char lower1, lower2;

	if (size_str1 != size_str2)
	  {
	    return (size_str1 < size_str2) ? -1 : 1;
	  }

	str1_end = str1 + size_str1;
	str2_end = str2 + size_str2;

	for (; str1 < str1_end && str2 < str2_end; str1++, str2++)
	  {
	    if (*str1 != *str2)
	      {
		lower1 = char_tolower_iso8859 (*str1);
		lower2 = char_tolower_iso8859 (*str2);
		if (lower1 != lower2)
		  {
		    return (lower1 < lower2) ? -1 : 1;
		  }
	      }
	  }

	return (str1 < str1_end) ? 1 : ((str2 < str2_end) ? -1 : 0);
      }
    case INTL_CODESET_KSC5601_EUC:
    default:
      /* ASCII */
      if (size_str1 != size_str2)
	{
	  return (size_str1 < size_str2) ? -1 : 1;
	}

      return strncasecmp ((char *) str1, (char *) str2, size_str1);
    }

  return 0;
}

/*
 * intl_is_case_match() - performs case insensitive matching
 *   return:  0 if strings are equal, -1 if str1 < str2 , 1 if str1 > str2
 *   lang_id(in):
 *   codeset(in):
 *   tok(in): token to check
 *   src(in): string to check for token
 *   size_tok(in): size in bytes of token
 *   size_src(in): size in bytes of source string
 *   matched_size_src(out): size in bytes of matched token in source
 *
 *  Note : Matching is performed by folding to LOWER case;
 *	   it takes into account case expansion (length in chars may differ).
 */
int
intl_case_match_tok (const INTL_LANG lang_id, const INTL_CODESET codeset, unsigned char *tok, unsigned char *src,
		     const int size_tok, const int size_src, int *matched_size_src)
{
  assert (tok != NULL);
  assert (src != NULL);

  assert (size_tok > 0);
  assert (size_src >= 0);

  assert (matched_size_src != NULL);

  *matched_size_src = 0;

  switch (codeset)
    {
    case INTL_CODESET_UTF8:
      {
	unsigned char *tok_end, *src_end;
	unsigned char *dummy;
	unsigned int cp1, cp2;
	const LANG_LOCALE_DATA *loc = lang_get_specific_locale (lang_id, INTL_CODESET_UTF8);
	const ALPHABET_DATA *alphabet;

	assert (loc != NULL);

	alphabet = &(loc->alphabet);

	tok_end = tok + size_tok;
	src_end = src + size_src;

	for (; tok < tok_end && src < src_end;)
	  {
	    int skip_size_tok = 0, skip_size_src = 0;
	    int res;

	    cp1 = intl_utf8_to_cp (tok, CAST_STRLEN (tok_end - tok), &dummy);
	    cp2 = intl_utf8_to_cp (src, CAST_STRLEN (src_end - src), &dummy);

	    res =
	      intl_strcasecmp_utf8_one_cp (alphabet, tok, src, CAST_STRLEN (tok_end - tok), CAST_STRLEN (src_end - src),
					   cp1, cp2, &skip_size_tok, &skip_size_src);

	    if (res != 0)
	      {
		return res;
	      }

	    tok += skip_size_tok;
	    src += skip_size_src;
	    *matched_size_src += skip_size_src;
	  }

	return (tok < tok_end) ? 1 : 0;
      }
      break;

    case INTL_CODESET_ISO88591:
      {
	unsigned char *tok_end, *src_end;
	unsigned char lower1, lower2;
	tok_end = tok + size_tok;
	src_end = src + size_src;

	if (size_tok > size_src)
	  {
	    return 1;
	  }

	*matched_size_src = size_tok;
	for (; tok < tok_end && src < src_end; tok++, src++)
	  {
	    if (*tok != *src)
	      {
		lower1 = char_tolower_iso8859 (*tok);
		lower2 = char_tolower_iso8859 (*src);
		if (lower1 != lower2)
		  {
		    return (lower1 < lower2) ? -1 : 1;
		  }
	      }
	  }
      }
      break;

    case INTL_CODESET_KSC5601_EUC:
    default:
      if (size_tok > size_src)
	{
	  return 1;
	}

      *matched_size_src = size_tok;
      return strncasecmp ((char *) tok, (char *) src, size_tok);
    }

  return 0;
}

/*
 * intl_strcasecmp_utf8_one_cp() - compares the first codepoints from two
 *				   strings case insensitive
 *   return:  0 if strings are equal, -1 if cp1 < cp2 , 1 if cp1 > cp2
 *   str1(in):
 *   str2(in):
 *   size_str1(in): size in bytes of str1
 *   size_str2(in): size in bytes of str2
 *   cp1(in): first codepoint in str1
 *   cp2(in): first codepoint in str2
 *   skip_size1(out):  bytes to skip from str1
 *   skip_size2(out):  bytes to skip from str2
 *   identifier_mode(in): true if compares identifiers, false otherwise
 *
 *  Note : skip_size1, skip_size2 are valid only when strings are equal
 *	   (returned value is zero).
 */
static int
intl_strcasecmp_utf8_one_cp (const ALPHABET_DATA * alphabet, unsigned char *str1, unsigned char *str2,
			     const int size_str1, const int size_str2, unsigned int cp1, unsigned int cp2,
			     int *skip_size1, int *skip_size2)
{
  int alpha_cnt;
  unsigned int l_array_1[INTL_CASING_EXPANSION_MULTIPLIER];
  unsigned int l_array_2[INTL_CASING_EXPANSION_MULTIPLIER];
  int skip_len1 = 1, skip_len2 = 1;
  int l_count_1 = 0, l_count_2 = 0, l_count = 0;
  int res;
  bool use_original_str1, use_original_str2;

  unsigned int *casing_arr;
  int casing_multiplier;

  assert (alphabet != NULL);
  assert (str1 != NULL);
  assert (str2 != NULL);
  assert (skip_size1 != NULL);
  assert (skip_size2 != NULL);

  if (cp1 == cp2)
    {
      (void) intl_char_size (str1, 1, INTL_CODESET_UTF8, skip_size1);
      (void) intl_char_size (str2, 1, INTL_CODESET_UTF8, skip_size2);

      return 0;
    }

  alpha_cnt = alphabet->l_count;

  if (alphabet->lower_multiplier == 1 && alphabet->upper_multiplier == 1)
    {
      if (cp1 < (unsigned int) alpha_cnt)
	{
	  cp1 = alphabet->lower_cp[cp1];
	}

      if (cp2 < (unsigned int) alpha_cnt)
	{
	  cp2 = alphabet->lower_cp[cp2];
	}

      if (cp1 != cp2)
	{
	  return (cp1 < cp2) ? (-1) : 1;
	}

      (void) intl_char_size (str1, 1, INTL_CODESET_UTF8, skip_size1);
      (void) intl_char_size (str2, 1, INTL_CODESET_UTF8, skip_size2);

      return 0;
    }

  /*
   * Multipliers can be either 1 or 2, as imposed by the LDML parsing code.
   * Currently, alphabets with both multipliers equal to 2 are not supported
   * for case sensitive comparisons.
   */
  assert (alphabet->lower_multiplier == 1 || alphabet->upper_multiplier == 1);
  if (alphabet->lower_multiplier > alphabet->upper_multiplier)
    {
      casing_arr = alphabet->lower_cp;
      casing_multiplier = alphabet->lower_multiplier;
    }
  else
    {
      casing_arr = alphabet->upper_cp;
      casing_multiplier = alphabet->upper_multiplier;
    }

  use_original_str1 = true;
  if (cp1 < (unsigned int) alpha_cnt)
    {
      memcpy (l_array_1, &(casing_arr[cp1 * casing_multiplier]), casing_multiplier * sizeof (unsigned int));

      if (cp1 != l_array_1[0])
	{
	  l_count_1 = casing_multiplier;
	  while (l_count_1 > 1 && l_array_1[l_count_1 - 1] == 0)
	    {
	      l_count_1--;
	    }

	  use_original_str1 = false;
	}
    }

  use_original_str2 = true;
  if (cp2 < (unsigned int) alpha_cnt)
    {
      memcpy (l_array_2, &(casing_arr[cp2 * casing_multiplier]), casing_multiplier * sizeof (unsigned int));

      if (cp2 != l_array_2[0])
	{
	  l_count_2 = casing_multiplier;
	  while (l_count_2 > 1 && l_array_2[l_count_2 - 1] == 0)
	    {
	      l_count_2--;
	    }

	  use_original_str2 = false;
	}
    }

  if (use_original_str1)
    {
      (void) intl_utf8_to_cp_list (str1, size_str1, l_array_1, casing_multiplier, &l_count_1);
    }

  if (use_original_str2)
    {
      (void) intl_utf8_to_cp_list (str2, size_str2, l_array_2, casing_multiplier, &l_count_2);
    }

  l_count = MIN (l_count_1, l_count_2);

  if (use_original_str1)
    {
      l_count_1 = MIN (l_count, l_count_1);
      skip_len1 = l_count_1;
    }
  else
    {
      skip_len1 = 1;
    }

  if (use_original_str2)
    {
      l_count_2 = MIN (l_count, l_count_2);
      skip_len2 = l_count_2;
    }
  else
    {
      skip_len2 = 1;
    }

  if (l_count_1 != l_count_2)
    {
      return (l_count_1 < l_count_2) ? (-1) : (1);
    }

  assert (l_count_1 == l_count_2);

  /* compare lower codepoints */
  res = memcmp (l_array_1, l_array_2, l_count * sizeof (unsigned int));
  if (res != 0)
    {
      return res;
    }

  /* convert supplementary characters in bytes size to skip */
  (void) intl_char_size (str1, skip_len1, INTL_CODESET_UTF8, skip_size1);
  (void) intl_char_size (str2, skip_len2, INTL_CODESET_UTF8, skip_size2);

  return 0;
}

/*
 * intl_identifier_casecmp_for_dblinke() - compares two identifiers strings
 *			       case insensitive excluding double quote for dblink
 *
 *   return: 0 if dblink_col_name equals to remote_col_name
 *   dblink_col_name(in):
 *   remote_col_name(in):
 *
 * NOTE: this routine is the same as intl_identifier_casecmp
 *       the first argument dblink_col_name may start with double quote
 *       but the remote_col_name never
 */
int
intl_identifier_casecmp_for_dblink (const char *dblink_col_name, const char *remote_col_name)
{
  int str1_size;
  int str2_size;
  char *str1 = (char *) dblink_col_name;
  char *str2 = (char *) remote_col_name;

  assert (str1 != NULL);
  assert (str2 != NULL);

  str1_size = strlen (str1);
  str2_size = strlen (str2);

  if (*str1 == '\"' || *str1 == '`')
    {
      str1_size = str1_size - 2;
      str1 = str1 + 1;
    }

  return intl_identifier_casecmp_w_size (lang_id (), (unsigned char *) str1, (unsigned char *) str2, str1_size,
					 str2_size);
}

/*
 * intl_identifier_casecmp() - compares two identifiers strings
 *			       case insensitive
 *   return: 0 if strings are equal, -1 if str1 < str2 , 1 if str1 > str2
 *   str1(in):
 *   str2(in):
 *
 * NOTE: identifier comparison is special, see intl_identifier_casecmp_w_size
 *	 for details on comparing identifiers of different length.
 */
int
intl_identifier_casecmp (const char *str1, const char *str2)
{
  int str1_size;
  int str2_size;

  assert (str1 != NULL);
  assert (str2 != NULL);

  str1_size = strlen (str1);
  str2_size = strlen (str2);

  return intl_identifier_casecmp_w_size (lang_id (), (unsigned char *) str1, (unsigned char *) str2, str1_size,
					 str2_size);
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
  int str1_size, str2_size;

  (void) intl_char_size ((unsigned char *) str1, len, lang_charset (), &str1_size);
  (void) intl_char_size ((unsigned char *) str2, len, lang_charset (), &str2_size);

  return intl_identifier_casecmp_w_size (lang_id (), (unsigned char *) str1, (unsigned char *) str2, str1_size,
					 str2_size);
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


#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * intl_identifier_namecmp() - compares two identifier string
 *   return: 0 if the identifiers are the "same",
 *           positive number if str1 is greater than str1,
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

  return intl_identifier_casecmp_w_size (lang_id (), (unsigned char *) cp1, (unsigned char *) cp2, str1_size,
					 str2_size);
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
	const unsigned char *s;
	const LANG_LOCALE_DATA *locale = lang_locale ();
	const ALPHABET_DATA *alphabet = &(locale->ident_alphabet);
	int s_size = src_size;
	unsigned int cp;
	int src_len;

	const unsigned char *usrc = REINTERPRET_CAST (const unsigned char *, src);
	intl_char_count (usrc, src_size, codeset, &src_len);

	src_lower_size = 0;

	for (s = usrc; s < usrc + src_size;)
	  {
	    assert (s_size > 0);

	    cp = intl_utf8_to_cp (s, s_size, &next);

	    if (cp < (unsigned int) (alphabet->l_count))
	      {
		int lower_cnt;
		unsigned int *lower_cp = &(alphabet->lower_cp[cp * alphabet->lower_multiplier]);

		for (lower_cnt = 0; lower_cnt < alphabet->lower_multiplier && *lower_cp != 0; lower_cnt++, lower_cp++)
		  {
		    src_lower_size += intl_cp_to_utf8 (*lower_cp, lower);
		  }
	      }
	    else
	      {
		src_lower_size += intl_cp_to_utf8 (cp, lower);
	      }

	    s_size -= CAST_STRLEN (next - s);
	    s = next;
	  }
      }
#else
      src_lower_size = src_size;
#endif
      break;

    case INTL_CODESET_RAW_BYTES:
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
 *
 *  Note : 'dst' has always enough size
 */
int
intl_identifier_lower (const char *src, char *dst)
{
  int d_size = 0;
  int length_in_bytes = 0;
  int length_in_chars = 0;
  unsigned char *d;
  const unsigned char *s;

  if (src)
    {
      length_in_bytes = strlen (src);
    }

  unsigned char *udst = REINTERPRET_CAST (unsigned char *, dst);
  const unsigned char *usrc = REINTERPRET_CAST (const unsigned char *, src);

  switch (lang_charset ())
    {
    case INTL_CODESET_UTF8:
      {
	const LANG_LOCALE_DATA *locale = lang_locale ();
	const ALPHABET_DATA *alphabet = &(locale->ident_alphabet);
	length_in_chars = intl_count_utf8_chars (usrc, length_in_bytes);
	(void) intl_tolower_utf8 (alphabet, usrc, udst, length_in_chars, &d_size);
	d = udst + d_size;
      }
      break;

    case INTL_CODESET_ISO88591:
      {
	for (d = udst, s = usrc; d < udst + length_in_bytes; d++, s++)
	  {
	    *d = char_tolower_iso8859 (*s);
	  }
      }
      break;

    case INTL_CODESET_KSC5601_EUC:
    default:
      {
	for (d = udst, s = usrc; d < udst + length_in_bytes; d++, s++)
	  {
	    *d = char_tolower (*s);
	  }
      }
      break;
    }

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

  const unsigned char *usrc = REINTERPRET_CAST (const unsigned char *, src);

  switch (codeset)
    {
    case INTL_CODESET_UTF8:
#if (INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER > 1)
      {
	unsigned char upper[INTL_UTF8_MAX_CHAR_SIZE];
	unsigned char *next;
	const unsigned char *s;
	const LANG_LOCALE_DATA *locale = lang_locale ();
	const ALPHABET_DATA *alphabet = &(locale->ident_alphabet);
	int s_size = src_size;
	unsigned int cp;
	int src_len;

	intl_char_count (usrc, src_size, codeset, &src_len);

	src_upper_size = 0;

	for (s = usrc; s < usrc + src_size;)
	  {
	    assert (s_size > 0);

	    cp = intl_utf8_to_cp (s, s_size, &next);

	    if (cp < (unsigned int) (alphabet->l_count))
	      {
		int upper_cnt;
		unsigned int *upper_cp = &(alphabet->upper_cp[cp * alphabet->upper_multiplier]);

		for (upper_cnt = 0; upper_cnt < alphabet->upper_multiplier && *upper_cp != 0; upper_cnt++, upper_cp++)
		  {
		    src_upper_size += intl_cp_to_utf8 (*upper_cp, upper);
		  }
	      }
	    else
	      {
		src_upper_size += intl_cp_to_utf8 (cp, upper);
	      }

	    s_size -= CAST_STRLEN (next - s);
	    s = next;
	  }
      }
#else
      src_upper_size = src_size;
#endif
      break;

    case INTL_CODESET_RAW_BYTES:
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
 *   src(in):
 *   dst(out):
 *
 *  Note : 'dst' has always enough size;
 */
int
intl_identifier_upper (const char *src, char *dst)
{
  int d_size = 0;
  int length_in_bytes = 0;
  int length_in_chars = 0;
  unsigned char *d;
  const unsigned char *s;

  if (src)
    {
      length_in_bytes = strlen (src);
    }

  unsigned char *udst = REINTERPRET_CAST (unsigned char *, dst);
  const unsigned char *usrc = REINTERPRET_CAST (const unsigned char *, src);

  switch (lang_charset ())
    {
    case INTL_CODESET_UTF8:
      {
	const LANG_LOCALE_DATA *locale = lang_locale ();
	const ALPHABET_DATA *alphabet = &(locale->ident_alphabet);
	length_in_chars = intl_count_utf8_chars (usrc, length_in_bytes);
	(void) intl_toupper_utf8 (alphabet, usrc, udst, length_in_chars, &d_size);
	d = udst + d_size;
      }
      break;
    case INTL_CODESET_ISO88591:
      {
	for (d = udst, s = usrc; d < udst + length_in_bytes; d++, s++)
	  {
	    *d = char_toupper_iso8859 (*s);
	  }
      }
      break;
    case INTL_CODESET_KSC5601_EUC:
    default:
      {
	for (d = udst, s = usrc; d < udst + length_in_bytes; d++, s++)
	  {
	    *d = char_toupper (*s);
	  }
      }
      break;
    }

  *d = '\0';

  return 0;
}

/*
 * intl_identifier_fix - Checks if a string can be an identifier;
 *			 Truncates the string to a desired size in bytes,
 *			 while making sure that the last char is not truncated
 *			 Checks that lower and upper case versions of string
 *			 do not exceed maximum allowed size.
 *
 *   return: error code : ER_GENERIC_ERROR or NO_ERROR
 *   name(in): identifier name, nul-terminated C string
 *   ident_max_size(in): allowed size of this identifier, may be -1 in which
 *			 case the maximum allowed system size is used
 *   error_on_case_overflow(in): if true, will return error if the lower or
 *				 upper version of truncated identifier exceeds
 *				 allowed size
 *
 *  Note : Identifier string may be truncated if lexer previously truncated it
 *	   in the middle of the last character;
 *	   No error message is outputed by this function - in case of error,
 *	   the error message should be output by the caller.
 *	   DB_MAX_IDENTIFIER_LENGTH is the buffer size for string identifier
 *	   This includes the nul-terminator byte; the useful bytes are
 *	   (DB_MAX_IDENTIFIER_LENGTH - 1).
 */
int
intl_identifier_fix (char *name, int ident_max_size, bool error_on_case_overflow)
{
  int truncated_size = 0, original_size = 0, char_size = 0;
  const unsigned char *cname = (unsigned char *) name;
  INTL_CODESET codeset = lang_charset ();

  assert (name != NULL);

  if (ident_max_size == -1)
    {
      ident_max_size = DB_MAX_IDENTIFIER_LENGTH - 1;
    }

  assert (ident_max_size > 0 && ident_max_size < DB_MAX_IDENTIFIER_LENGTH);

  original_size = strlen (name);
  if (INTL_CODESET_MULT (codeset) == 1)
    {
      if (original_size > ident_max_size)
	{
	  name[ident_max_size] = '\0';
	}
      return NO_ERROR;
    }

  assert (INTL_CODESET_MULT (codeset) > 1);

  /* we do not check contents of non-ASCII if codeset is UTF-8 or EUC; valid codeset sequences are checked with
   * 'intl_check_string' when enabled */

check_truncation:
  /* check if last char of identifier may have been truncated */
  if (original_size + INTL_CODESET_MULT (codeset) > ident_max_size)
    {
      if (ident_max_size < original_size)
	{
	  original_size = ident_max_size;
	}

      /* count original size based on the size given by first byte of each char */
      for (truncated_size = 0; truncated_size < original_size;)
	{
	  INTL_NEXT_CHAR (cname, cname, codeset, &char_size);
	  truncated_size += char_size;
	}
      assert (truncated_size >= original_size);

      /* truncated_size == original_size means last character fit entirely in 'original_size'
       * otherwise assume the last character was truncated */
      if (truncated_size > original_size)
	{
	  assert (truncated_size < original_size + INTL_CODESET_MULT (codeset));
	  assert ((unsigned char) *(cname - char_size) > 0x80);
	  /* truncate after the last full character */
	  truncated_size -= char_size;
	  original_size = truncated_size;
	}
      name[original_size] = '\0';
    }

  /* ensure that lower or upper versions of identifier do not exceed maximum allowed size of an identifier */
#if (INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER > 1)
  if (intl_identifier_upper_string_size (name) > ident_max_size
      || intl_identifier_lower_string_size (name) > ident_max_size)
    {
      if (error_on_case_overflow)
	{
	  /* this is grammar context : reject the identifier string */
	  return ER_GENERIC_ERROR;
	}
      else
	{
	  /* decrease the initial allowed size and try again */
	  ident_max_size -= INTL_CODESET_MULT (codeset);
	  if (ident_max_size <= INTL_CODESET_MULT (codeset))
	    {
	      /* we make sure we have room for at least one character */
	      return ER_GENERIC_ERROR;
	    }
	  goto check_truncation;
	}
    }
#endif

  return NO_ERROR;
}

/*
 * intl_identifier_mht_1strhash - hash a identifier key (in lowercase)
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Charset dependent version of 'mht_1strlowerhashTaken' function
 */
unsigned int
intl_identifier_mht_1strlowerhash (const void *key, const unsigned int ht_size)
{
  unsigned int hash;
  unsigned const char *byte_p = (unsigned char *) key;
  unsigned int ch;

  assert (key != NULL);

  switch (lang_charset ())
    {
    case INTL_CODESET_UTF8:
      {
	const LANG_LOCALE_DATA *locale = lang_locale ();
	const ALPHABET_DATA *alphabet = &(locale->ident_alphabet);
	int key_size = strlen ((const char *) key);
	unsigned char *next;

	for (hash = 0; key_size > 0;)
	  {
	    ch = intl_utf8_to_cp (byte_p, key_size, &next);
	    if (ch < (unsigned int) (alphabet->l_count))
	      {
		assert (alphabet->lower_multiplier == 1);
		ch = alphabet->lower_cp[ch];
	      }

	    key_size -= CAST_STRLEN (next - byte_p);
	    byte_p = next;

	    hash = (hash << 5) - hash + ch;
	  }
      }
      break;
    case INTL_CODESET_ISO88591:
      for (hash = 0; *byte_p; byte_p++)
	{
	  ch = char_tolower_iso8859 (*byte_p);
	  hash = (hash << 5) - hash + ch;
	}
      break;
    case INTL_CODESET_RAW_BYTES:
      for (hash = 0; *byte_p; byte_p++)
	{
	  ch = *byte_p;
	  hash = (hash << 5) - hash + ch;
	}
      break;
    case INTL_CODESET_KSC5601_EUC:
    default:
      for (hash = 0; *byte_p; byte_p++)
	{
	  ch = char_tolower (*byte_p);
	  hash = (hash << 5) - hash + ch;
	}
      break;
    }

  return hash % ht_size;
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
#endif

/*
 * intl_put_char() - puts a character into a string buffer
 *   return: size of character
 *   dest(in/out): destination buffer
 *   char_p(in): pointer to character
 *   codeset(in): codeset of character
 *
 *  Note : It is assumed that 'dest' buffer can fit the character.
 *
 */
int
intl_put_char (unsigned char *dest, const unsigned char *char_p, const INTL_CODESET codeset)
{
  int char_len;

  assert (char_p != NULL);

  switch (codeset)
    {
    case INTL_CODESET_UTF8:
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
      break;

    case INTL_CODESET_KSC5601_EUC:
      (void) intl_nextchar_euc (char_p, &char_len);
      memcpy (dest, char_p, char_len);
      return char_len;

    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
    default:
      *dest = *char_p;
      return 1;
    }

  return 1;
}


/*
 * intl_is_space() - checks if character is white-space
 *   return:
 *   str(in):
 *   str_end(in): end of string (pointer to first character after last
 *		  character of string) or NULL if str is null terminated
 *   codeset(in): codeset of string
 *   space_size(out): size in bytes of 'whitespace' character
 *
 *  Note : White spaces are: ASCII space, TAB character, CR and LF
 *	   If codeset is EUC also the double byte character space (A1 A1) is
 *	   considered;
 *
 */
bool
intl_is_space (const char *str, const char *str_end, const INTL_CODESET codeset, int *space_size)
{
  assert (str != NULL);

  if (space_size != NULL)
    {
      *space_size = 1;
    }

  switch (codeset)
    {
    case INTL_CODESET_KSC5601_EUC:
      if (str_end == NULL)
	{
	  if (*((unsigned char *) str) == 0xa1 && *((unsigned char *) (str + 1)) == 0xa1)
	    {
	      if (space_size != NULL)
		{
		  *space_size = 2;
		}
	      return true;
	    }
	  else if (char_isspace (*str))
	    {
	      return true;
	    }
	}
      else
	{
	  if (str < str_end)
	    {
	      if (*((const unsigned char *) str) == 0xa1 && str + 1 < str_end
		  && *((const unsigned char *) (str + 1)) == 0xa1)
		{
		  if (space_size != NULL)
		    {
		      *space_size = 2;
		    }
		  return true;
		}
	      else if (char_isspace (*str))
		{
		  return true;
		}
	    }
	}
      break;
    case INTL_CODESET_UTF8:
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
    default:
      if (str_end == NULL)
	{
	  if (char_isspace (*str))
	    {
	      return true;
	    }
	}
      else
	{
	  if (str < str_end && char_isspace (*str))
	    {
	      return true;
	    }
	}
      break;
    }

  return false;
}

/*
 * intl_skip_spaces() - skips white spaces in string
 *   return: begining of non-whitespace characters or end of string
 *   str(in):
 *   str_end(in): end of string (pointer to first character after last
 *		  character of string) or NULL if str is null terminated
 *   codeset(in): codeset of string
 *
 *  Note : White spaces are: ASCII space, TAB character, CR and LF
 *	   If codeset is EUC also the double byte character space (A1 A1) is
 *	   considered;
 *
 */
const char *
intl_skip_spaces (const char *str, const char *str_end, const INTL_CODESET codeset)
{
  assert (str != NULL);

  switch (codeset)
    {
    case INTL_CODESET_KSC5601_EUC:
      if (str_end == NULL)
	{
	  while (*str != '\0')
	    {
	      if (*((unsigned char *) str) == 0xa1 && *((unsigned char *) (str + 1)) == 0xa1)
		{
		  str++;
		  str++;
		}
	      else if (char_isspace (*str))
		{
		  str++;
		}
	      else
		{
		  break;
		}
	    }
	}
      else
	{
	  while (str < str_end)
	    {
	      if (*((const unsigned char *) str) == 0xa1 && str + 1 < str_end
		  && *((const unsigned char *) (str + 1)) == 0xa1)
		{
		  str++;
		  str++;
		}
	      else if (char_isspace (*str))
		{
		  str++;
		}
	      else
		{
		  break;
		}
	    }
	}
      break;
    case INTL_CODESET_UTF8:
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
    default:
      if (str_end == NULL)
	{
	  while (char_isspace (*str))
	    {
	      str++;
	    }
	}
      else
	{
	  while (str < str_end && char_isspace (*str))
	    {
	      str++;
	    }
	}
      break;
    }

  return str;
}

/*
 * intl_backskip_spaces() - skips trailing white spaces in end of string
 *   return: end of non-whitespace characters or end of string
 *   str_begin(in): start of string
 *   str_end(in): end of string (pointer to last character)
 *   codeset(in): codeset of string
 *
 *  Note : White spaces are: ASCII space, TAB character, CR and LF
 *	   If codeset is EUC also the double byte character space (A1 A1) is
 *	   considered;
 *
 */
const char *
intl_backskip_spaces (const char *str_begin, const char *str_end, const INTL_CODESET codeset)
{
  assert (str_begin != NULL);
  assert (str_end != NULL);

  switch (codeset)
    {
    case INTL_CODESET_KSC5601_EUC:
      while (str_end > str_begin)
	{
	  if (*((const unsigned char *) str_end) == 0xa1 && str_end - 1 > str_begin
	      && *((const unsigned char *) (str_end - 1)) == 0xa1)
	    {
	      str_end--;
	      str_end--;
	    }
	  else if (char_isspace (*str_end))
	    {
	      str_end--;
	    }
	  else
	    {
	      break;
	    }
	}
      break;
    case INTL_CODESET_UTF8:
    case INTL_CODESET_ISO88591:
    case INTL_CODESET_RAW_BYTES:
    default:
      while (str_end > str_begin && char_isspace (*str_end))
	{
	  str_end++;
	}
      break;
    }

  return str_end;
}

/*
 * intl_cp_to_utf8() - converts a unicode codepoint to its
 *                            UTF-8 encoding
 *  return: number of bytes for UTF-8; 0 means not encoded
 *  codepoint(in) : Unicode code point (32 bit value)
 *  utf8_seq(in/out) : pre-allocated buffer for UTF-8 sequence
 *
 */
int
intl_cp_to_utf8 (const unsigned int codepoint, unsigned char *utf8_seq)
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
 * intl_cp_to_dbcs() - converts a codepoint to DBCS encoding
 *  return: number of bytes for encoding; 0 means not encoded
 *  codepoint(in) : code point (16 bit value)
 *  byte_flag(in): flag array : 0: single byte char,
 *				1: is a leading byte for DBCS,
 *				2: byte value not used
 *  seq(in/out) : pre-allocated buffer for DBCS sequence
 *
 */
int
intl_cp_to_dbcs (const unsigned int codepoint, const unsigned char *byte_flag, unsigned char *seq)
{
  assert (seq != NULL);

  /* is_lead_byte is assumed to have 256 elements */
  assert (byte_flag != NULL);

  if (codepoint <= 0xff)
    {
      if (byte_flag[codepoint] == 0)
	{
	  /* 1 byte */
	  *seq = (unsigned char) codepoint;
	}
      else
	{
	  /* undefined or lead byte */
	  *seq = '?';
	}
      return 1;
    }
  if (codepoint <= 0xffff)
    {
      /* 2 bytes */
      *seq++ = (unsigned char) (0xff & (codepoint >> 8));
      *seq = (unsigned char) (codepoint & 0xff);
      return 2;
    }

  assert (false);
  *seq = '?';
  return 1;
}

/*
 * intl_utf8_to_cp() - converts a UTF-8 encoded char to unicode codepoint
 *  return: unicode code point; 0xffffffff means error
 *  utf8(in) : buffer for UTF-8 char
 *  size(in) : size of buffer
 *  next_char(in/out): pointer to next character
 *
 */
unsigned int
intl_utf8_to_cp (const unsigned char *utf8, const int size, unsigned char **next_char)
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
      return (unsigned int) (((utf8[0] & 0x0f) << 12) | ((utf8[1] & 0x3f) << 6) | (utf8[2] & 0x3f));
    }
  else if (size >= 4 && utf8[0] >= 0xf0 && utf8[0] < 0xf8)
    {
      *next_char = (unsigned char *) utf8 + 4;
      return (unsigned int) (((utf8[0] & 0x07) << 18) | ((utf8[1] & 0x3f) << 12) | ((utf8[2] & 0x3f) << 6) |
			     (utf8[3] & 0x3f));
    }
#if INTL_UTF8_MAX_CHAR_SIZE > 4
  else if (size >= 5 && utf8[0] >= 0xf8 && utf8[0] < 0xfc)
    {
      *next_char = (unsigned char *) utf8 + 5;
      return (unsigned int) (((utf8[0] & 0x03) << 24) | ((utf8[1] & 0x3f) << 18) | ((utf8[2] & 0x3f) << 12) |
			     ((utf8[3] & 0x3f) << 6) | (utf8[4] & 0x3f));
    }
  else if (size >= 6 && utf8[0] >= 0xfc && utf8[0] < 0xfe)
    {
      *next_char = (unsigned char *) utf8 + 6;
      return (unsigned int) (((utf8[0] & 0x01) << 30) | ((utf8[1] & 0x3f) << 24) | ((utf8[2] & 0x3f) << 18) |
			     ((utf8[3] & 0x3f) << 12) | ((utf8[4] & 0x3f) << 6) | (utf8[5] & 0x3f));
    }
#endif

  *next_char = (unsigned char *) utf8 + 1;
  return 0xffffffff;
}

/*
 * intl_back_utf8_to_cp() - converts a UTF-8 encoded char to unicode codepoint
 *			    but starting from the last byte of a character
 *  return: unicode code point; 0xffffffff means error
 *
 *  utf8_start(in) : start of buffer
 *  utf8_last(in) : pointer to last byte of buffer (and last byte of last
 *		    character)
 *  last_byte__prev_char(in/out) : pointer to last byte of previous character
 *
 */
unsigned int
intl_back_utf8_to_cp (const unsigned char *utf8_start, const unsigned char *utf8_last,
		      unsigned char **last_byte__prev_char)
{
  int char_size = 1;
  unsigned char *dummy;

  assert (utf8_start != NULL);
  assert (utf8_last != NULL);
  assert (utf8_start <= utf8_last);
  assert (last_byte__prev_char != NULL);

  if (*utf8_last < 0x80)
    {
      *last_byte__prev_char = ((unsigned char *) utf8_last) - 1;
      return *utf8_last;
    }

  /* multibyte character */
  do
    {
      if (((*utf8_last--) & 0xc0) != 0x80)
	{
	  break;
	}
      if (utf8_last < utf8_start)
	{
	  /* broken char, invalid CP */
	  *last_byte__prev_char = ((unsigned char *) utf8_start) - 1;
	  return 0xffffffff;
	}
    }
  while (++char_size < INTL_UTF8_MAX_CHAR_SIZE);

  *last_byte__prev_char = (unsigned char *) utf8_last;
  return intl_utf8_to_cp (utf8_last + 1, char_size, &dummy);
}

/*
 * intl_dbcs_to_cp() - converts a DBCS encoded char to DBCS codepoint
 *  return: DBCS code point; 0xffffffff means error
 *  seq(in) : buffer for DBCS char
 *  size(in) : size of buffer
 *  byte_flag(in) : array of flags for lead bytes
 *  next_char(in/out): pointer to next character
 *
 */
unsigned int
intl_dbcs_to_cp (const unsigned char *seq, const int size, const unsigned char *byte_flag, unsigned char **next_char)
{
  assert (seq != NULL);
  assert (size > 0);
  assert (next_char != NULL);

  assert (byte_flag != NULL);

  if (byte_flag[seq[0]] == 1 && size >= 2)
    {
      *next_char = (unsigned char *) seq + 2;
      return (unsigned int) (((seq[0]) << 8) | (seq[1]));
    }

  *next_char = (unsigned char *) seq + 1;
  return (unsigned int) (seq[0]);
}


/*
 * intl_utf8_to_cp_list() - converts a UTF-8 encoded string to a list of
 *                          unicode codepoint
 *  return: number of codepoints found in string
 *  utf8(in) : buffer for UTF-8 char
 *  size(in) : size of string buffer
 *  cp_array(in/out) : preallocated array to store computed codepoints list
 *  max_array_size(in) : maximum size of computed codepoints list
 *  cp_count(out) : number of codepoints found in string
 *  array_count(out) : number of elements in codepoints list
 */
int
intl_utf8_to_cp_list (const unsigned char *utf8, const int size, unsigned int *cp_array, const int max_array_size,
		      int *array_count)
{
  unsigned char *next = NULL;
  const unsigned char *utf8_end = utf8 + size;
  int i;

  assert (utf8 != NULL);
  assert (size > 0);
  assert (cp_array != NULL);
  assert (max_array_size > 0);
  assert (array_count != NULL);

  for (i = 0, *array_count = 0; utf8 < utf8_end; i++)
    {
      unsigned int cp;
      assert (utf8_end - utf8 > 0);

      cp = intl_utf8_to_cp (utf8, CAST_STRLEN (utf8_end - utf8), &next);
      utf8 = next;

      if (i < max_array_size)
	{
	  cp_array[i] = cp;
	  (*array_count)++;
	}
    }

  return i;
}

#define UTF8_BYTE_IN_RANGE(b, r1, r2) (!(b < r1 || b > r2))

/*
 * intl_check_utf8 - Checks if a string contains valid UTF-8 sequences
 *
 *   return: 0 if valid,
 *	     1 if contains and invalid byte in one char
 *	     2 if last char is truncated (missing bytes)
 *   buf(in): buffer
 *   size(out): size of buffer (negative values accepted, in this case buffer
 *		is assumed to be NUL terminated)
 *   pos(out): pointer to beginning of invalid character
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
INTL_UTF8_VALIDITY
intl_check_utf8 (const unsigned char *buf, int size, char **pos)
{
#define OUTPUT(charp_out) if (pos != NULL) *pos = (char *) charp_out

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
      /* range C0 - C1 overlaps 1 byte 00 - 20 (2 byte overflow) */
      if (*p < 0xc2)
	{
	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}

      /* check 2 bytes sequences */
      /* 2 bytes sequence allowed : C2 - DF , 80 - BF */
      if (UTF8_BYTE_IN_RANGE (*p, 0xc2, 0xdf))
	{
	  p++;
	  if (p >= p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      continue;
	    }
	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}

      /* check 3 bytes sequences */
      /* 3 bytes sequence : E0 , A0 - BF , 80 - BF */
      if (*p == 0xe0)
	{
	  p++;
	  if (p >= p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0xa0, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  OUTPUT (curr_char);
		  return INTL_UTF8_TRUNCATED;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  continue;
		}
	    }

	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}
      /* 3 bytes sequence : E1 - EC , 80 - BF , 80 - BF */
      /* 3 bytes sequence : EE - EF , 80 - BF , 80 - BF */
      else if (UTF8_BYTE_IN_RANGE (*p, 0xe1, 0xec) || UTF8_BYTE_IN_RANGE (*p, 0xee, 0xef))
	{
	  p++;
	  if (p >= p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  OUTPUT (curr_char);
		  return INTL_UTF8_TRUNCATED;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  continue;
		}
	    }
	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}
      /* 3 bytes sequence : ED , 80 - 9F , 80 - BF */
      else if (*p == 0xed)
	{
	  p++;
	  if (p >= p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0x9f))
	    {
	      p++;
	      if (p >= p_end)
		{
		  OUTPUT (curr_char);
		  return INTL_UTF8_TRUNCATED;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  continue;
		}
	    }
	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}

      /* 4 bytes sequence : F0 , 90 - BF , 80 - BF , 80 - BF */
      if (*p == 0xf0)
	{
	  p++;
	  if (p >= p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x90, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  OUTPUT (curr_char);
		  return INTL_UTF8_TRUNCATED;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      OUTPUT (curr_char);
		      return INTL_UTF8_TRUNCATED;
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      p++;
		      continue;
		    }
		}
	    }
	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}
      /* 4 bytes sequence : F1 - F3 , 80 - BF , 80 - BF , 80 - BF */
      if (UTF8_BYTE_IN_RANGE (*p, 0xf1, 0xf3))
	{
	  p++;
	  if (p >= p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  OUTPUT (curr_char);
		  return INTL_UTF8_TRUNCATED;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      OUTPUT (curr_char);
		      return INTL_UTF8_TRUNCATED;
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      p++;
		      continue;
		    }
		}
	    }
	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}
      /* 4 bytes sequence : F4 , 80 - 8F , 80 - BF , 80 - BF */
      else if (*p == 0xf4)
	{
	  p++;
	  if (p >= p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0x8f))
	    {
	      p++;
	      if (p >= p_end)
		{
		  OUTPUT (curr_char);
		  return INTL_UTF8_TRUNCATED;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      OUTPUT (curr_char);
		      return INTL_UTF8_TRUNCATED;
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      p++;
		      continue;
		    }
		}
	    }
	  OUTPUT (curr_char);
	  return INTL_UTF8_INVALID;
	}

      assert (*p > 0xf4);
      OUTPUT (curr_char);
      return INTL_UTF8_INVALID;
    }

  return INTL_UTF8_VALID;

#undef OUTPUT
}

/*
 * intl_check_euckr - Checks if a string contains valid EUC-KR sequences
 *
 *
 *   return: 0 if valid,
 *	     1 if contains and invalid byte in one char
 *	     2 if last char is truncated (missing bytes)
 *   buf(in): buffer
 *   size(out): size of buffer (negative values accepted, in this case buffer is assumed to be NUL terminated)
 *   pos(out): pointer to beginning of invalid character
 *
 *  Valid ranges:
 *    - 1 byte : 00 - 8E ; 90 - A0
 *    - 2 bytes: A1 - FE , 00 - FF
 *    - 3 bytes: 8F	 , 00 - FF , 00 - FF
 */
INTL_UTF8_VALIDITY
intl_check_euckr (const unsigned char *buf, int size, char **pos)
{
#define OUTPUT(charp_out) if (pos != NULL) *pos = (char *) charp_out

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

      /* SS3 byte value starts a 3 bytes character */
      if (*p == SS3)
	{
	  p++;
	  p++;
	  p++;
	  if (p > p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }
	  continue;
	}

      /* check 2 bytes sequences */
      if (UTF8_BYTE_IN_RANGE (*p, 0xa1, 0xfe))
	{
	  p++;
	  p++;
	  if (p > p_end)
	    {
	      OUTPUT (curr_char);
	      return INTL_UTF8_TRUNCATED;
	    }
	  continue;
	}

      OUTPUT (curr_char);
      return INTL_UTF8_INVALID;
    }

  return INTL_UTF8_VALID;

#undef OUTPUT
}

/*
 * intl_check_string - Checks if a string contains valid sequences in current codeset
 *
 *   return: 0 - if valid, non-zero otherwise : 1 - if invalid byte in char
 *	     2 - if last char is truncated
 *   buf(in): buffer
 *   size(out): size of buffer (negative values accepted, in this case buffer
 *		is assumed to be NUL terminated)
 *   codeset(in): codeset assumed for buf
 */
INTL_UTF8_VALIDITY
intl_check_string (const char *buf, int size, char **pos, const INTL_CODESET codeset)
{
  if (!intl_String_validation)
    {
      // this function is currently used either in client-modes or for loaddb. if it will be used in other server-mode
      // contexts, that can impact the result of queries, global variable should be replaced with a session parameter.
      return INTL_UTF8_VALID;
    }

  switch (codeset)
    {
    case INTL_CODESET_UTF8:
      return intl_check_utf8 ((const unsigned char *) buf, size, pos);

    case INTL_CODESET_KSC5601_EUC:
      return intl_check_euckr ((const unsigned char *) buf, size, pos);

    case INTL_CODESET_RAW_BYTES:
    default:
      break;
    }

  return INTL_UTF8_VALID;
}

#if !defined (SERVER_MODE)
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
  const char BOM[] = { (char) 0xef, (char) 0xbb, (char) 0xbf };
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
#endif /* SERVER_MODE */

/* UTF-8 to console routines */

/*
 * intl_text_single_byte_to_utf8() - converts a buffer containing text with ISO
 *				     8859-X encoding to UTF-8
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
intl_text_single_byte_to_utf8 (const char *in_buf, const int in_size, char **out_buf, int *out_size)
{
  return intl_text_single_byte_to_utf8_ext (lang_get_txt_conv (), (const unsigned char *) in_buf, in_size,
					    (unsigned char **) out_buf, out_size);
}

/*
 * intl_text_single_byte_to_utf8_ext() - converts a buffer containing text
 *					 with ISO 8859-X encoding to UTF-8
 *
 *   return: error code
 *   t(in): text conversion data
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(in/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(in/out): size of string (NUL terminator not included)
 */
int
intl_text_single_byte_to_utf8_ext (void *t, const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
				   int *out_size)
{

  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;
  TEXT_CONVERSION *txt_conv;
  bool is_ascii = true;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);
  assert (t != NULL);

  txt_conv = (TEXT_CONVERSION *) t;

  p_in = in_buf;
  while (p_in < in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  is_ascii = false;
	  break;
	}
    }

  if (is_ascii)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      /* a ISO8859-X character is encoded on maximum 2 bytes in UTF-8 */
      *out_buf = (unsigned char *) malloc (in_size * 2 + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (in_size * 2 + 1));
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

  assert (txt_conv->text_last_cp > 0);
  for (p_in = in_buf, p_out = *out_buf; p_in < in_buf + in_size; p_in++)
    {
      if (*p_in >= txt_conv->text_first_cp && *p_in <= txt_conv->text_last_cp)
	{
	  unsigned char *utf8_bytes = txt_conv->text_to_utf8[*p_in - txt_conv->text_first_cp].bytes;
	  int utf8_size = txt_conv->text_to_utf8[*p_in - txt_conv->text_first_cp].size;

	  do
	    {
	      *p_out++ = *utf8_bytes++;
	    }
	  while (--utf8_size > 0);
	}
      else
	{
	  if (*p_in < 0x80)
	    {
	      *p_out++ = *p_in;
	    }
	  else
	    {
	      assert (false);
	      *p_out++ = '?';
	    }
	}
    }

  *(p_out) = '\0';
  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return NO_ERROR;
}

/*
 * intl_text_utf8_to_single_byte() - converts a buffer containing UTF-8 text
 *				     to ISO 8859-X encoding
 *
 *   return: error code
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(in/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(in/out): size of output string (NUL terminator not counted)
 */
int
intl_text_utf8_to_single_byte (const char *in_buf, const int in_size, char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;
  unsigned char *p_next = NULL;
  TEXT_CONVERSION *txt_conv = lang_get_txt_conv ();
  bool is_ascii = true;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);
  assert (txt_conv != NULL);

  p_in = (const unsigned char *) in_buf;
  while (p_in < (const unsigned char *) in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  is_ascii = false;
	  break;
	}
    }

  if (is_ascii)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      *out_buf = (char *) malloc (in_size + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (in_size + 1));
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

  for (p_in = (const unsigned char *) in_buf, p_out = (unsigned char *) *out_buf;
       p_in < (const unsigned char *) in_buf + in_size;)
    {
      unsigned int cp = 0;

      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in++;
	  continue;
	}

      cp = intl_utf8_to_cp (p_in, CAST_STRLEN (in_buf + in_size - (char *) p_in), &p_next);
      if (cp >= txt_conv->utf8_first_cp && cp <= txt_conv->utf8_last_cp)
	{
	  assert (txt_conv->utf8_to_text[cp - txt_conv->utf8_first_cp].size == 1);
	  cp = (unsigned int) *(txt_conv->utf8_to_text[cp - txt_conv->utf8_first_cp].bytes);
	}

      if (cp > 0xff)
	{
	  *p_out++ = '?';
	}
      else
	{
	  *p_out++ = (unsigned char) cp;
	}
      p_in = p_next;
    }

  *(p_out) = '\0';
  *out_size = CAST_STRLEN (p_out - (unsigned char *) *(out_buf));

  return NO_ERROR;
}

/*
 * intl_init_conv_iso8859_1_to_utf8() - initializes conversion map from
 *				        ISO 8859-1 (Latin 1) to UTF-8
 *  return:
 */
static void
intl_init_conv_iso8859_1_to_utf8 (void)
{
  unsigned int i;

  /* 00 - 7E : mapped to ASCII */
  for (i = 0; i <= 0x7e; i++)
    {
      iso8859_1_To_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_1_To_utf8_conv[i].bytes)) = (unsigned char) i;
    }

  /* 7F - 9F : not mapped */
  for (i = 0x7f; i <= 0x9f; i++)
    {
      iso8859_1_To_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_1_To_utf8_conv[i].bytes)) = (unsigned char) '?';
    }

  /* A0 - FF : mapped to Unicode codepoint with the same value */
  for (i = 0xa0; i <= 0xff; i++)
    {
      iso8859_1_To_utf8_conv[i].size = intl_cp_to_utf8 (i, iso8859_1_To_utf8_conv[i].bytes);
    }

  con_Iso_8859_1_conv.text_first_cp = 0;
  con_Iso_8859_1_conv.text_last_cp = 0xff;
  con_Iso_8859_1_conv.text_to_utf8 = iso8859_1_To_utf8_conv;

  /* no specific mapping here : Unicode codepoints in range 00-FF map directly onto ISO-8859-1 */
  con_Iso_8859_1_conv.utf8_first_cp = 0;
  con_Iso_8859_1_conv.utf8_last_cp = 0;
  con_Iso_8859_1_conv.utf8_to_text = NULL;
}

/*
 * intl_init_conv_iso8859_9_to_utf8() - initializes conversion map from
 *				        ISO 8859-9 (turkish) to UTF-8
 *  return:
 *
 */
static void
intl_init_conv_iso8859_9_to_utf8 (void)
{
  unsigned int i;
  const unsigned int iso8859_9_special_mapping[][2] = {
    {0xd0, 0x11e},		/* capital G with breve */
    {0xdd, 0x130},		/* capital I with dot above */
    {0xde, 0x15e},		/* capital S with cedilla */
    {0xf0, 0x11f},		/* small g with breve */
    {0xfd, 0x131},		/* small i dotless */
    {0xfe, 0x15f}		/* small s with cedilla */
  };

  /* 00 - 7E : mapped to ASCII */
  for (i = 0; i <= 0x7e; i++)
    {
      iso8859_9_To_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_9_To_utf8_conv[i].bytes)) = (unsigned char) i;
    }

  /* 7F - 9F : not mapped */
  for (i = 0x7f; i <= 0x9f; i++)
    {
      iso8859_9_To_utf8_conv[i].size = 1;
      *((unsigned char *) (iso8859_9_To_utf8_conv[i].bytes)) = (unsigned char) '?';
    }

  /* A0 - FF : mapped to Unicode codepoint with the same value */
  for (i = 0xa0; i <= 0xff; i++)
    {
      iso8859_9_To_utf8_conv[i].size = intl_cp_to_utf8 (i, iso8859_9_To_utf8_conv[i].bytes);
    }

  for (i = ISO_8859_9_FIRST_CP; i <= ISO_8859_9_LAST_CP; i++)
    {
      utf8_Cp_to_iso_8859_9_conv[i - ISO_8859_9_FIRST_CP].size = 1;
      *(utf8_Cp_to_iso_8859_9_conv[i - ISO_8859_9_FIRST_CP].bytes) = '?';
    }

  /* special mapping */
  for (i = 0; i < DIM (iso8859_9_special_mapping); i++)
    {
      unsigned int val8bit = iso8859_9_special_mapping[i][0];
      unsigned int cp = iso8859_9_special_mapping[i][1];

      iso8859_9_To_utf8_conv[val8bit].size = intl_cp_to_utf8 (cp, iso8859_9_To_utf8_conv[val8bit].bytes);

      *(utf8_Cp_to_iso_8859_9_conv[cp - ISO_8859_9_FIRST_CP].bytes) = val8bit;

      assert (utf8_Cp_to_iso_8859_9_conv[cp - ISO_8859_9_FIRST_CP].size == 1);
    }

  con_Iso_8859_9_conv.text_first_cp = 0;
  con_Iso_8859_9_conv.text_last_cp = 0xff;
  con_Iso_8859_9_conv.text_to_utf8 = iso8859_9_To_utf8_conv;

  con_Iso_8859_9_conv.utf8_first_cp = ISO_8859_9_FIRST_CP;
  con_Iso_8859_9_conv.utf8_last_cp = ISO_8859_9_LAST_CP;
  con_Iso_8859_9_conv.utf8_to_text = utf8_Cp_to_iso_8859_9_conv;
}

/*
 * intl_text_dbcs_to_utf8() - converts a buffer containing text with DBCS
 *			      encoding to UTF-8
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
intl_text_dbcs_to_utf8 (const char *in_buf, const int in_size, char **out_buf, int *out_size)
{
  return intl_text_dbcs_to_utf8_ext (lang_get_txt_conv (), (const unsigned char *) in_buf, in_size,
				     (unsigned char **) out_buf, out_size);
}

/*
 * intl_text_dbcs_to_utf8_ext() - converts a buffer containing text with DBCS
 *				  encoding to UTF-8
 *
 *   return: error code
 *   t(in): text conversion data
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(in/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(in/out): size of string (NUL terminator not included)
 */
int
intl_text_dbcs_to_utf8_ext (void *t, const unsigned char *in_buf, const int in_size, unsigned char **out_buf,
			    int *out_size)
{
  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;
  TEXT_CONVERSION *txt_conv;
  bool is_ascii = true;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);
  assert (t != NULL);

  txt_conv = (TEXT_CONVERSION *) t;

  p_in = in_buf;
  while (p_in < in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  is_ascii = false;
	  break;
	}
    }

  if (is_ascii)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      /* a DBCS text may contain ASCII characters (encoded with 1 byte) which may expand to maximum 2 bytes in UTF-8
       * and DBCS characters (2 bytes) which may expand to maximum 3 bytes in UTF-8; Also it may contain single byte
       * characters which may expand to 3 bytes characters in UTF-8 Apply a safe expansion of 3 */
      *out_buf = (unsigned char *) malloc (in_size * 3 + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (in_size * 3 + 1));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      if (*out_size < in_size * 3 + 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}
    }

  assert (txt_conv->text_last_cp > 0);
  for (p_in = in_buf, p_out = *out_buf; p_in < in_buf + in_size;)
    {
      unsigned char *p_next;
      unsigned int text_cp =
	intl_dbcs_to_cp (p_in, CAST_STRLEN (in_buf + in_size - p_in), txt_conv->byte_flag, &p_next);

      if (text_cp >= txt_conv->text_first_cp && text_cp <= txt_conv->text_last_cp)
	{
	  unsigned char *utf8_bytes = txt_conv->text_to_utf8[text_cp - txt_conv->text_first_cp].bytes;
	  int utf8_size = txt_conv->text_to_utf8[text_cp - txt_conv->text_first_cp].size;

	  do
	    {
	      *p_out++ = *utf8_bytes++;
	    }
	  while (--utf8_size > 0);
	}
      else
	{
	  if (text_cp < 0x80)
	    {
	      *p_out++ = *p_in;
	    }
	  else
	    {
	      *p_out++ = '?';
	    }
	}

      assert (p_next <= in_buf + in_size);
      p_in = p_next;
    }

  *(p_out) = '\0';
  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return NO_ERROR;
}

/*
 * intl_text_utf8_to_dbcs() - converts a buffer containing UTF-8 text
 *			      to DBCS encoding
 *
 *   return: error code
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(in/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(in/out): size of output string (NUL terminator not counted)
 */
int
intl_text_utf8_to_dbcs (const char *in_buf, const int in_size, char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  unsigned char *p_out = NULL;
  unsigned char *p_next = NULL;
  TEXT_CONVERSION *txt_conv = lang_get_txt_conv ();
  bool is_ascii = true;

  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);
  assert (txt_conv != NULL);

  p_in = (const unsigned char *) in_buf;
  while (p_in < (const unsigned char *) in_buf + in_size)
    {
      if (*p_in++ >= 0x80)
	{
	  is_ascii = false;
	  break;
	}
    }

  if (is_ascii)
    {
      *out_buf = NULL;
      return NO_ERROR;
    }

  if (*out_buf == NULL)
    {
      *out_buf = (char *) malloc (in_size + 1);
      if (*out_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (in_size + 1));
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

  assert (txt_conv->utf8_last_cp > 0);

  for (p_in = (const unsigned char *) in_buf, p_out = (unsigned char *) *out_buf;
       p_in < (const unsigned char *) in_buf + in_size;)
    {
      unsigned int cp = 0;

      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in++;
	  continue;
	}

      cp = intl_utf8_to_cp (p_in, CAST_STRLEN (in_buf + in_size - (char *) p_in), &p_next);
      if (cp >= txt_conv->utf8_first_cp && cp <= txt_conv->utf8_last_cp)
	{
	  unsigned char *text_bytes = txt_conv->utf8_to_text[cp - txt_conv->utf8_first_cp].bytes;
	  int text_size = txt_conv->utf8_to_text[cp - txt_conv->utf8_first_cp].size;

	  assert (text_size >= 1);
	  do
	    {
	      *p_out++ = *text_bytes++;
	    }
	  while (--text_size > 0);
	}
      else if (cp > 0x80)
	{
	  *p_out++ = '?';
	}
      else
	{
	  *p_out++ = (unsigned char) cp;
	}
      p_in = p_next;
    }

  *(p_out) = '\0';
  *out_size = CAST_STRLEN (p_out - (unsigned char *) *(out_buf));

  return NO_ERROR;
}

/*
 * intl_fast_iso88591_to_utf8() - converts a buffer containing text with ISO
 *				  8859-1 encoding to UTF-8
 *
 *   return: 0 conversion ok, 1 conversion done, but invalid characters where
 *	     found
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer; NULL if conversion
 *			is not required
 *   out_size(out): size of string (NUL terminator not included)
 */
int
intl_fast_iso88591_to_utf8 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  const unsigned char *p_end;
  unsigned char *p_out = NULL;
  int status = 0;

  assert (in_size > 0);
  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  for (p_in = in_buf, p_end = p_in + in_size, p_out = (unsigned char *) *out_buf; p_in < p_end; p_in++)
    {
      if (*p_in < 0x7f)
	{
	  *p_out++ = *p_in;
	}
      else if (*p_in < 0xa0)
	{
	  /* ISO 8859-1 characters in this range are not valid */
	  *p_out++ = '?';
	  status = 1;
	}
      else
	{
	  *p_out++ = (unsigned char) (0xc0 | (*p_in >> 6));
	  *p_out++ = (unsigned char) (0x80 | (*p_in & 0x3f));
	}
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return status;
}

/*
 * intl_euckr_to_iso88591() - converts a buffer containing EUCKR text to
 *			      ISO88591
 *
 *   return: 0 conversion ok, 1 conversion done, but invalid characters where
 *	     found
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer;
 *   out_size(out): size of string (NUL terminator not included)
 */
int
intl_euckr_to_iso88591 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  const unsigned char *p_end;
  unsigned char *p_out = NULL;
  unsigned int unicode_cp;
  int status = 0;

  assert (in_size > 0);
  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  for (p_in = in_buf, p_end = p_in + in_size, p_out = (unsigned char *) *out_buf; p_in < p_end; p_in++)
    {
      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in;
	}
      else if (*p_in >= 0xa1 && *p_in < 0xff && p_end - p_in >= 2)
	{
	  if (*(p_in + 1) >= 0xa1 && *(p_in + 1) < 0xff)
	    {
	      /* KSC5601 two-bytes character */
	      unsigned char ksc_buf[2];

	      ksc_buf[0] = *p_in - 0x80;
	      ksc_buf[1] = *(p_in + 1) - 0x80;

	      if (ksc5601_mbtowc (&unicode_cp, ksc_buf, 2) <= 0)
		{
		  *p_out++ = '?';
		  status = 1;
		}
	      else
		{
		  if ((unicode_cp <= 0x1F) || (unicode_cp > 0xFF) || ((unicode_cp >= 0x7F) && (unicode_cp <= 0x9F)))
		    {
		      *p_out++ = '?';
		      status = 1;
		    }
		  else
		    {
		      *p_out++ = unicode_cp;
		    }
		}
	    }
	  else
	    {
	      *p_out++ = '?';
	      status = 1;
	    }

	  /* skip one additional byte */
	  p_in++;
	}
      else if (*p_in == 0x8f && p_end - p_in >= 3)
	{
	  if (*(p_in + 1) >= 0xa1 && *(p_in + 1) < 0xff && *(p_in + 2) >= 0xa1 && *(p_in + 2) < 0xff)
	    {
	      /* JISX0212 three bytes character */
	      unsigned char jis_buf[2];

	      jis_buf[0] = *(p_in + 1) - 0x80;
	      jis_buf[1] = *(p_in + 2) - 0x80;

	      if (jisx0212_mbtowc (&unicode_cp, jis_buf, 2) <= 0)
		{
		  *p_out++ = '?';
		  status = 1;
		}
	      else
		{
		  if ((unicode_cp <= 0x1F) || (unicode_cp > 0xFF) || ((unicode_cp >= 0x7F) && (unicode_cp <= 0x9F)))
		    {
		      *p_out++ = '?';
		      status = 1;
		    }
		  else
		    {
		      *p_out++ = unicode_cp;
		    }
		}
	    }
	  else
	    {
	      *p_out++ = '?';
	      status = 1;
	    }

	  /* skip two additional bytes */
	  p_in++;
	  p_in++;
	}
      else
	{
	  /* EUC-KR byte not valid */
	  *p_out++ = '?';
	  status = 1;
	}
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return status;
}

/*
 * intl_euckr_to_utf8() - converts a buffer containing text with EUC-KR
 *			  + JISX0212 to UTF-8
 *
 *   return: 0 conversion ok, 1 conversion done, but invalid characters where
 *	     found
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer;
 *   out_size(out): size of string (NUL terminator not included)
 */
int
intl_euckr_to_utf8 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  const unsigned char *p_end;
  unsigned char *p_out = NULL;
  unsigned int unicode_cp;
  int utf8_size;
  int status = 0;

  assert (in_size > 0);
  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  for (p_in = in_buf, p_end = p_in + in_size, p_out = (unsigned char *) *out_buf; p_in < p_end; p_in++)
    {
      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in;
	}
      else if (*p_in >= 0xa1 && *p_in < 0xff && p_end - p_in >= 2)
	{
	  if (*(p_in + 1) >= 0xa1 && *(p_in + 1) < 0xff)
	    {
	      /* KSC5601 two-bytes character */
	      unsigned char ksc_buf[2];

	      ksc_buf[0] = *p_in - 0x80;
	      ksc_buf[1] = *(p_in + 1) - 0x80;

	      if (ksc5601_mbtowc (&unicode_cp, ksc_buf, 2) <= 0)
		{
		  *p_out++ = '?';
		  status = 1;
		}
	      else
		{
		  utf8_size = intl_cp_to_utf8 (unicode_cp, p_out);
		  p_out += utf8_size;
		}
	    }
	  else
	    {
	      *p_out++ = '?';
	      status = 1;
	    }

	  /* skip one additional byte */
	  p_in++;
	}
      else if (*p_in == 0x8f && p_end - p_in >= 3)
	{
	  if (*(p_in + 1) >= 0xa1 && *(p_in + 1) < 0xff && *(p_in + 2) >= 0xa1 && *(p_in + 2) < 0xff)
	    {
	      /* JISX0212 three bytes character */
	      unsigned char jis_buf[2];

	      jis_buf[0] = *(p_in + 1) - 0x80;
	      jis_buf[1] = *(p_in + 2) - 0x80;

	      if (jisx0212_mbtowc (&unicode_cp, jis_buf, 2) <= 0)
		{
		  *p_out++ = '?';
		  status = 1;
		}
	      else
		{
		  utf8_size = intl_cp_to_utf8 (unicode_cp, p_out);
		  p_out += utf8_size;
		}
	    }
	  else
	    {
	      *p_out++ = '?';
	      status = 1;
	    }

	  /* skip two additional bytes */
	  p_in++;
	  p_in++;
	}
      else
	{
	  /* EUC-KR byte not valid */
	  *p_out++ = '?';
	  status = 1;
	}
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return status;
}

/*
 * intl_utf8_to_iso88591() - converts a buffer containing UTF8 text to ISO88591
 *
 *   return: 0 conversion ok, 1 conversion done, but invalid characters where
 *	     found
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer;
 *   out_size(out): size of string (NUL terminator not included)
 */
int
intl_utf8_to_iso88591 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  const unsigned char *p_end;
  unsigned char *p_out = NULL;
  unsigned char *next_utf8;
  int status = 0;
  unsigned int unicode_cp = 0;

  assert (in_size > 0);
  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  for (p_in = in_buf, p_end = in_buf + in_size, p_out = (unsigned char *) *out_buf; p_in < p_end;)
    {
      unicode_cp = intl_utf8_to_cp (p_in, CAST_STRLEN (p_end - p_in), &next_utf8);

      if ((unicode_cp > 0xFF) || ((unicode_cp >= 0x7F) && (unicode_cp <= 0x9F)))
	{
	  *p_out++ = '?';
	  status = 1;
	}
      else
	{
	  *p_out++ = unicode_cp;
	}

      p_in = next_utf8;
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return status;
}

/*
 * intl_utf8_to_euckr() - converts a buffer containing UTF8 text to EUC-KR
 *			  + JISX0212 encoding
 *
 *   return: 0 conversion ok, 1 conversion done, but invalid characters where
 *	     found
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer;
 *   out_size(out): size of string (NUL terminator not included)
 */
int
intl_utf8_to_euckr (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  const unsigned char *p_end;
  unsigned char *p_out = NULL;
  int status = 0;

  assert (in_size > 0);
  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  for (p_in = in_buf, p_end = p_in + in_size, p_out = (unsigned char *) *out_buf; p_in < p_end;)
    {
      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in++;
	}
      else
	{
	  unsigned char euc_buf[2];
	  int euc_bytes;
	  unsigned int unicode_cp;
	  unsigned char *next_utf8;

	  unicode_cp = intl_utf8_to_cp (p_in, CAST_STRLEN (p_end - p_in), &next_utf8);
	  if (unicode_cp == 0xffffffff)
	    {
	      goto illegal_char;
	    }

	  /* try to convert to KSC5601 */
	  euc_bytes = ksc5601_wctomb (euc_buf, unicode_cp, CAST_STRLEN (next_utf8 - p_in));

	  assert (euc_bytes != 0);
	  if (euc_bytes == 2)
	    {
	      *p_out = euc_buf[0] + 0x80;
	      *(p_out + 1) = euc_buf[1] + 0x80;
	      p_out++;
	      p_out++;
	      p_in = next_utf8;
	      continue;
	    }

	  if (euc_bytes != RET_ILUNI)
	    {
	      goto illegal_char;
	    }
	  assert (euc_bytes == RET_ILUNI);
	  /* not found as KSC encoding, try as JISX0212 */
	  euc_bytes = jisx0212_wctomb (euc_buf, unicode_cp, CAST_STRLEN (next_utf8 - p_in));

	  assert (euc_bytes != 0);
	  if (euc_bytes == 2)
	    {
	      *p_out = 0x8f;
	      *(p_out + 1) = euc_buf[0] + 0x80;
	      *(p_out + 2) = euc_buf[1] + 0x80;
	      p_out += 3;
	      p_in = next_utf8;
	      continue;
	    }

	  /* illegal Unicode or impossible to convert to EUC */
	illegal_char:
	  p_in = next_utf8;
	  *p_out = '?';
	  p_out++;
	  status = 1;
	}
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return status;
}

/*
 * intl_iso88591_to_euckr() - converts a buffer containing ISO88591 text to
 *			      EUC-KR encoding
 *
 *   return: 0 conversion ok, 1 conversion done, but invalid characters where
 *	     found
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer;
 *   out_size(out): size of string (NUL terminator not included)
 */
int
intl_iso88591_to_euckr (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p_in = NULL;
  const unsigned char *p_end;
  unsigned char *p_out = NULL;
  int status = 0;

  assert (in_size > 0);
  assert (in_buf != NULL);
  assert (out_buf != NULL);
  assert (out_size != NULL);

  for (p_in = in_buf, p_end = p_in + in_size, p_out = (unsigned char *) *out_buf; p_in < p_end; p_in++)
    {
      if (*p_in < 0x80)
	{
	  *p_out++ = *p_in;
	}
      else
	{
	  unsigned char euc_buf[2];
	  int euc_bytes;

	  if (*p_in < 0xa0)
	    {
	      *p_out = '?';
	      p_out++;
	      status = 1;
	      continue;
	    }

	  /* try to convert to KSC5601 */
	  euc_bytes = ksc5601_wctomb (euc_buf, *p_in, 2);

	  assert (euc_bytes != 0);
	  if (euc_bytes == 2)
	    {
	      *p_out = euc_buf[0] + 0x80;
	      *(p_out + 1) = euc_buf[1] + 0x80;
	      p_out++;
	      p_out++;
	      continue;
	    }

	  /* illegal ISO8859-1 or impossible to convert to KSC */
	  if (euc_bytes != RET_ILUNI)
	    {
	      goto illegal_char;
	    }
	  assert (euc_bytes == RET_ILUNI);

	  /* try to convert to JISX0212 */
	  euc_bytes = jisx0212_wctomb (euc_buf, *p_in, 2);

	  assert (euc_bytes != 0);
	  if (euc_bytes == 2)
	    {
	      *p_out = 0x8f;
	      *(p_out + 1) = euc_buf[0] + 0x80;
	      *(p_out + 2) = euc_buf[1] + 0x80;
	      p_out++;
	      p_out++;
	      p_out++;
	      continue;
	    }

	illegal_char:
	  *p_out = '?';
	  p_out++;
	  status = 1;
	}
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));

  return status;
}

/* monetary symbols */

/* UTF-8 encoding of money symbols - maps to DB_CURRENCY enum type */
static char moneysymbols_utf8[][4] = {
  "$",				/* dollar sign */
  "\xc2\xa5",			/* Japan money symbols */
  "\xc2\xa3",			/* pound sterling - British money symbols */
  "\xe2\x82\xa9",		/* won - Korean money symbols */
  "TL",				/* TL - Turkish money symbols */
  "KHR",			/* KHR - Cambodian money symbols */
  "CNY",			/* chinese money symbols */
  "INR",			/* indian money symbols */
  "RUB",			/* russian money symbols */
  "AUD",			/* australian money symbols */
  "CAD",			/* canadian money symbols */
  "BRL",			/* brasilian money symbols */
  "RON",			/* romanian money symbols */
  "EUR",			/* euro symbol */
  "CHF",			/* swiss money symbols */
  "DKK",			/* danish money symbols */
  "NOK",			/* norwegian money symbols */
  "BGN",			/* bulgarian money symbols */
  "VND",			/* vietnamese dong symbol */
  "CZK",			/* Czech koruna symbol */
  "PLN",			/* Polish zloty symbol */
  "SEK",			/* Swedish krona symbol */
  "HRK",			/* Croatian kuna symbol */
  "RSD",			/* serbian dinar symbol */
  "\xc2\xa4"			/* generic curency symbol */
};

/* encoding (for console output) of money symbols - maps to DB_CURRENCY enum
 * type */
/* used for values printing in CSQL */
static char moneysymbols_console[][4] = {
  "$",				/* dollar sign */
  "Y",				/* japanese yen */
  "&",				/* british pound */
  "\\",				/* Korean won */
  "TL",				/* turkish lira */
  "KHR",			/* cambodian riel */
  "CNY",			/* chinese renminbi */
  "INR",			/* indian rupee */
  "RUB",			/* russian ruble */
  "AUD",			/* australian dollar */
  "CAD",			/* canadian dollar */
  "BRL",			/* brasilian real */
  "RON",			/* romanian leu */
  "EUR",			/* euro */
  "CHF",			/* swiss franc */
  "DKK",			/* danish krone */
  "NOK",			/* norwegian krone */
  "BGN",			/* bulgarian lev */
  "VND",			/* vietnamese dong */
  "CZK",			/* Czech koruna */
  "PLN",			/* Polish zloty */
  "SEK",			/* Swedish krona */
  "HRK",			/* Croatian kuna */
  "RSD",			/* serbian dinar */
  ""				/* generic currency symbol - add new symbols before this */
};

/* encoding (for grammars) of money symbols - maps to DB_CURRENCY enum type */
/* used for values printing in CSQL */
static char moneysymbols_grammar[][5] = {
  "$",				/* dollar sign */
  "\xa1\xef",			/* japanese yen */
  "\\GBP",			/* british pound */
  "\\KRW",			/* Korean won */
  "\\TL",			/* turkish lira */
  "\\KHR",			/* cambodian riel */
  "\\CNY",			/* chinese renminbi */
  "\\INR",			/* indian rupee */
  "\\RUB",			/* russian ruble */
  "\\AUD",			/* australian dollar */
  "\\CAD",			/* canadian dollar */
  "\\BRL",			/* brasilian real */
  "\\RON",			/* romanian leu */
  "\\EUR",			/* euro */
  "\\CHF",			/* swiss franc */
  "\\DKK",			/* danish krone */
  "\\NOK",			/* norwegian krone */
  "\\BGN",			/* bulgarian lev */
  "\\VND",			/* vietnamese dong */
  "\\CZK",			/* Czech koruna */
  "\\PLN",			/* Polish zloty */
  "\\SEK",			/* Swedish krona */
  "\\HRK",			/* Croatian kuna */
  "\\RSD",			/* serbian dinar */
  ""				/* generic currency symbol - add new symbols before this */
};

/* ISO encoding of money symbols - maps to DB_CURRENCY enum type */
static char moneysymbols_iso_codes[][4] = {
  "USD",			/* dollar sign */
  "JPY",			/* japanese yen */
  "GBP",			/* british pound */
  "KRW",			/* Korean won */
  "TRY",			/* turkish lira */
  "KHR",			/* cambodian riel */
  "CNY",			/* chinese renminbi */
  "INR",			/* indian rupee */
  "RUB",			/* russian ruble */
  "AUD",			/* australian dollar */
  "CAD",			/* canadian dollar */
  "BRL",			/* brasilian real */
  "RON",			/* romanian leu */
  "EUR",			/* euro */
  "CHF",			/* swiss franc */
  "DKK",			/* danish krone */
  "NOK",			/* norwegian krone */
  "BGN",			/* bulgarian lev */
  "VND",			/* vietnamese dong */
  "CZK",			/* Czech koruna */
  "PLN",			/* Polish zloty */
  "SEK",			/* Swedish krona */
  "HRK",			/* Croatian kuna */
  "RSD",			/* serbian dinar */
  ""				/* generic currency symbol - add new symbols before this */
};

/* escaped ISO encoding of money symbols - maps to DB_CURRENCY enum type */
static char moneysymbols_esc_iso_codes[][5] = {
  "\\USD",			/* dollar sign */
  "\\JPY",			/* japanese yen */
  "\\GBP",			/* british pound */
  "\\KRW",			/* Korean won */
  "\\TRY",			/* turkish lira */
  "\\KHR",			/* cambodian riel */
  "\\CNY",			/* chinese renminbi */
  "\\INR",			/* indian rupee */
  "\\RUB",			/* russian ruble */
  "\\AUD",			/* australian dollar */
  "\\CAD",			/* canadian dollar */
  "\\BRL",			/* brasilian real */
  "\\RON",			/* romanian leu */
  "\\EUR",			/* euro */
  "\\CHF",			/* swiss franc */
  "\\DKK",			/* danish krone */
  "\\NOK",			/* norwegian krone */
  "\\BGN",			/* bulgarian lev */
  "\\VND",			/* vietnamese dong */
  "\\CZK",			/* Czech koruna */
  "\\PLN",			/* Polish zloty */
  "\\SEK",			/* Swedish krona */
  "\\HRK",			/* Croatian kuna */
  "\\RSD",			/* serbian dinar */
  ""				/* generic currency symbol - add new symbols before this */
};

/* ISO88591 encoding of money symbols - maps to DB_CURRENCY enum type */
static char moneysymbols_iso88591_codes[][4] = {
  "$",				/* dollar sign */
  "\xa5",			/* japanese yen */
  "\xa3",			/* british pound */
  "KRW",			/* Korean won */
  "TL",				/* turkish lira */
  "KHR",			/* cambodian riel */
  "CNY",			/* chinese renminbi */
  "INR",			/* indian rupee */
  "RUB",			/* russian ruble */
  "AUD",			/* australian dollar */
  "CAD",			/* canadian dollar */
  "BRL",			/* brasilian real */
  "RON",			/* romanian leu */
  "EUR",			/* euro */
  "CHF",			/* swiss franc */
  "DKK",			/* danish krone */
  "NOK",			/* norwegian krone */
  "BGN",			/* bulgarian lev */
  "VND",			/* vietnamese dong */
  "CZK",			/* Czech koruna */
  "PLN",			/* Polish zloty */
  "SEK",			/* Swedish krona */
  "HRK",			/* Croatian kuna */
  "RSD",			/* serbian dinar */
  ""				/* generic currency symbol - add new symbols before this */
};

/*
 * intl_is_currency_symbol() - check if a string matches a currency
 *                             symbol (UTF-8)
 *   return: true if a match is found
 *   src(in): NUL terminated string
 *   currency(out): currency found
 */
bool
intl_is_currency_symbol (const char *src, DB_CURRENCY * currency, int *symbol_size,
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
      for (sym_currency = 0; src_len > 0 && sym_currency < (int) DIM (moneysymbols_iso_codes); sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_iso_codes[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0
	      && !memcmp (src, moneysymbols_iso_codes[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  if (check_mode & CURRENCY_CHECK_MODE_ESC_ISO)
    {
      for (sym_currency = 0; src_len > 0 && sym_currency < (int) DIM (moneysymbols_esc_iso_codes); sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_esc_iso_codes[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0
	      && !memcmp (src, moneysymbols_esc_iso_codes[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  if (check_mode & CURRENCY_CHECK_MODE_UTF8)
    {
      for (sym_currency = 0; src_len > 0 && sym_currency < (int) DIM (moneysymbols_utf8); sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_utf8[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0 && !memcmp (src, moneysymbols_utf8[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  if (check_mode & CURRENCY_CHECK_MODE_CONSOLE)
    {
      for (sym_currency = 0; src_len > 0 && sym_currency < (int) DIM (moneysymbols_console); sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_console[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0 && !memcmp (src, moneysymbols_console[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  /* search backwards : "\TL" (turkish lira) symbol may be miss-interpreted as "\" (korean won) */
  if (check_mode & CURRENCY_CHECK_MODE_GRAMMAR)
    {
      for (sym_currency = (int) DIM (moneysymbols_grammar) - 1; src_len > 0 && sym_currency >= 0; sym_currency--)
	{
	  int symbol_len = strlen (moneysymbols_grammar[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0 && !memcmp (src, moneysymbols_grammar[sym_currency], symbol_len))
	    {
	      *currency = (DB_CURRENCY) sym_currency;
	      *symbol_size = symbol_len;
	      return (*currency == DB_CURRENCY_NULL) ? false : true;
	    }
	}
    }

  if (check_mode & CURRENCY_CHECK_MODE_ISO88591)
    {
      for (sym_currency = 0; src_len > 0 && sym_currency < (int) DIM (moneysymbols_iso88591_codes); sym_currency++)
	{
	  int symbol_len = strlen (moneysymbols_iso88591_codes[sym_currency]);
	  if (src_len >= symbol_len && symbol_len > 0
	      && !memcmp (src, moneysymbols_iso88591_codes[sym_currency], symbol_len))
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
 *   codeset (in): required codeset
 */
char *
intl_get_money_symbol (const DB_CURRENCY currency, INTL_CODESET codeset)
{
  switch (codeset)
    {
    case INTL_CODESET_ISO88591:
      return intl_get_money_ISO88591_symbol (currency);
    case INTL_CODESET_UTF8:
      return intl_get_money_UTF8_symbol (currency);
    default:
      return intl_get_money_symbol_console (currency);
    }
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
  if (currency >= (int) DIM (moneysymbols_console))
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
  if (currency >= (int) DIM (moneysymbols_grammar))
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

/*
 * intl_get_money_ISO_symbol() - returns a string representing the currency
 *				 ISO symbol, as a 3 letter string.
 *   return: currency ISO symbol
 *   currency(int): currency code
 */
char *
intl_get_money_ISO_symbol (const DB_CURRENCY currency)
{
  if (currency >= (int) DIM (moneysymbols_iso_codes))
    {
      return moneysymbols_iso_codes[DB_CURRENCY_NULL];
    }
  return moneysymbols_iso_codes[currency];
}

/*
 * intl_get_money_esc_ISO_symbol() - returns a string representing the
 *				     currency with escaped ISO symbol
 *   return: currency escaped ISO symbol
 *   currency(int): currency code
 */
char *
intl_get_money_esc_ISO_symbol (const DB_CURRENCY currency)
{
  if (currency >= (int) DIM (moneysymbols_esc_iso_codes))
    {
      return moneysymbols_esc_iso_codes[DB_CURRENCY_NULL];
    }
  return moneysymbols_esc_iso_codes[currency];
}

/*
 * intl_get_money_UTF8_symbol() - returns a string representing the currency
 *				 UTF8 symbol, as a 3 letter string.
 *   return: currency UTF8 symbol
 *   currency(int): currency code
 */
char *
intl_get_money_UTF8_symbol (const DB_CURRENCY currency)
{
  if (currency >= (int) DIM (moneysymbols_utf8))
    {
      return moneysymbols_utf8[DB_CURRENCY_NULL];
    }
  return moneysymbols_utf8[currency];
}

/*
 * intl_get_money_ISO88591_symbol() - returns a string representing the currency
 *				 ISO88591 symbol, as a 3 letter string.
 *   return: currency ISO88591 symbol
 *   currency(int): currency code
 */
char *
intl_get_money_ISO88591_symbol (const DB_CURRENCY currency)
{
  if (currency >= (int) DIM (moneysymbols_iso88591_codes))
    {
      return moneysymbols_iso88591_codes[DB_CURRENCY_NULL];
    }
  return moneysymbols_iso88591_codes[currency];
}

/*
 * intl_binary_to_utf8 - converts a buffer from binary to utf8, replacing
 *			 invalid UTF-8 sequences with '?'
 *
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer;
 *   out_size(out): size of string (NUL terminator not included)
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
 */
void
intl_binary_to_utf8 (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p = in_buf;
  const unsigned char *p_end = NULL;
  const unsigned char *curr_char = NULL;
  unsigned char *p_out = NULL;

  p_out = (unsigned char *) *out_buf;
  p_end = in_buf + in_size;

  while (p < p_end)
    {
      curr_char = p;

      if (*p < 0x80)
	{
	  *p_out++ = *p++;
	  continue;
	}

      /* range 80 - BF is not valid UTF-8 first byte */
      /* range C0 - C1 overlaps 1 byte 00 - 20 (2 byte overlongs) */
      if (*p < 0xc2)
	{
	  *p_out++ = '?';
	  p++;
	  continue;
	}

      /* check 2 bytes sequences */
      /* 2 bytes sequence allowed : C2 - DF , 80 - BF */
      if (UTF8_BYTE_IN_RANGE (*p, 0xc2, 0xdf))
	{
	  p++;
	  if (p >= p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      *p_out++ = *(p - 1);
	      *p_out++ = *p;
	      p++;
	      continue;
	    }
	  p++;
	  *p_out++ = '?';
	  continue;
	}

      /* check 3 bytes sequences */
      /* 3 bytes sequence : E0 , A0 - BF , 80 - BF */
      if (*p == 0xe0)
	{
	  p++;
	  if (p >= p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0xa0, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  *p_out++ = '?';
		  continue;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  *p_out++ = *(p - 2);
		  *p_out++ = *(p - 1);
		  *p_out++ = *p;
		  p++;
		  continue;
		}
	    }
	  p++;
	  if (p < p_end)
	    {
	      *p_out++ = '?';
	    }
	  continue;
	}
      /* 3 bytes sequence : E1 - EC , 80 - BF , 80 - BF */
      /* 3 bytes sequence : EE - EF , 80 - BF , 80 - BF */
      else if (UTF8_BYTE_IN_RANGE (*p, 0xe1, 0xec) || UTF8_BYTE_IN_RANGE (*p, 0xee, 0xef))
	{
	  p++;
	  if (p >= p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  *p_out++ = '?';
		  continue;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  *p_out++ = *(p - 2);
		  *p_out++ = *(p - 1);
		  *p_out++ = *p;
		  p++;
		  continue;
		}
	    }
	  p++;
	  *p_out++ = '?';
	  continue;
	}
      /* 3 bytes sequence : ED , 80 - 9F , 80 - BF */
      else if (*p == 0xed)
	{
	  p++;
	  if (p >= p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0x9f))
	    {
	      p++;
	      if (p >= p_end)
		{
		  *p_out++ = '?';
		  continue;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  *p_out++ = *(p - 2);
		  *p_out++ = *(p - 1);
		  *p_out++ = *p;
		  p++;
		  continue;
		}
	    }
	  p++;
	  *p_out++ = '?';
	  continue;
	}

      /* 4 bytes sequence : F0 , 90 - BF , 80 - BF , 80 - BF */
      if (*p == 0xf0)
	{
	  p++;
	  if (p >= p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x90, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  *p_out++ = '?';
		  continue;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      *p_out++ = '?';
		      continue;
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      *p_out++ = *(p - 3);
		      *p_out++ = *(p - 2);
		      *p_out++ = *(p - 1);
		      *p_out++ = *p;
		      p++;
		      continue;
		    }
		}
	    }
	  p++;
	  *p_out++ = '?';
	  continue;
	}
      /* 4 bytes sequence : F1 - F3 , 80 - BF , 80 - BF , 80 - BF */
      if (UTF8_BYTE_IN_RANGE (*p, 0xf1, 0xf3))
	{
	  p++;
	  if (p >= p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
	    {
	      p++;
	      if (p >= p_end)
		{
		  *p_out++ = '?';
		  continue;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      *p_out++ = '?';
		      continue;
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      *p_out++ = *(p - 3);
		      *p_out++ = *(p - 2);
		      *p_out++ = *(p - 1);
		      *p_out++ = *p;
		      p++;
		      continue;
		    }
		}
	    }
	  p++;
	  *p_out++ = '?';
	  continue;
	}
      /* 4 bytes sequence : F4 , 80 - 8F , 80 - BF , 80 - BF */
      else if (*p == 0xf4)
	{
	  p++;
	  if (p >= p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }

	  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0x8f))
	    {
	      p++;
	      if (p >= p_end)
		{
		  *p_out++ = '?';
		  continue;
		}

	      if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		{
		  p++;
		  if (p >= p_end)
		    {
		      *p_out++ = '?';
		      continue;
		    }

		  if (UTF8_BYTE_IN_RANGE (*p, 0x80, 0xbf))
		    {
		      *p_out++ = *(p - 3);
		      *p_out++ = *(p - 2);
		      *p_out++ = *(p - 1);
		      *p_out++ = *p;
		      p++;
		      continue;
		    }
		}
	    }
	  p++;
	  *p_out++ = '?';
	  continue;
	}

      assert (*p > 0xf4);
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));
}

/*
 * intl_binary_to_euckr - converts a buffer from binary to euckr, replacing
 *			 invalid euckr sequences with '?'
 *
 *   in_buf(in): buffer
 *   in_size(in): size of input string (NUL terminator not included)
 *   out_buf(int/out) : output buffer : uses the pre-allocated buffer passed
 *			as input or a new allocated buffer;
 *   out_size(out): size of string (NUL terminator not included)
 *
 *  Valid ranges:
 *    - 1 byte : 00 - 8E ; 90 - A0
 *    - 2 bytes: A1 - FE , 00 - FF
 *    - 3 bytes: 8F	 , 00 - FF , 00 - FF
 */
void
intl_binary_to_euckr (const unsigned char *in_buf, const int in_size, unsigned char **out_buf, int *out_size)
{
  const unsigned char *p = in_buf;
  const unsigned char *p_end = NULL;
  const unsigned char *curr_char = NULL;
  unsigned char *p_out = NULL;

  p_out = (unsigned char *) *out_buf;
  p_end = in_buf + in_size;

  while (p < p_end)
    {
      curr_char = p;

      if (*p < 0x80)
	{
	  *p_out++ = *p++;
	  continue;
	}

      /* SS3 byte value starts a 3 bytes character */
      if (*p == SS3)
	{
	  p++;
	  p++;
	  p++;
	  if (p > p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }
	  *p_out++ = *(p - 3);
	  *p_out++ = *(p - 2);
	  *p_out++ = *(p - 1);
	  continue;
	}

      /* check 2 bytes sequences */
      if (UTF8_BYTE_IN_RANGE (*p, 0xa1, 0xfe))
	{
	  p++;
	  p++;
	  if (p > p_end)
	    {
	      *p_out++ = '?';
	      continue;
	    }
	  *p_out++ = *(p - 2);
	  *p_out++ = *(p - 1);
	  continue;
	}
      p++;
      *p_out++ = '?';
    }

  *out_size = CAST_STRLEN (p_out - *(out_buf));
}
