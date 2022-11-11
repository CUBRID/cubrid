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
 * misc_string.c : case insensitive string comparison routines for 8-bit
 *                 character sets
 */

#ident "$Id$"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc_string.h"

/*
 * ustr_casestr() - find a substring, case ignored
 *   return: char*
 *   s1(in)
 *   s2(in)
 */
char *
ustr_casestr (const char *s1, const char *s2)
{
  size_t len1, len2;
  const char *p1, *p2;
  int c;

  if (!s1 || !s2)
    return NULL;
  if (s1 == s2)
    return (char *) s1;
  if (!*s2)
    return (char *) s1;
  if (!*s1)
    return NULL;

  len1 = strlen (s1);
  len2 = strlen (s2);
  c = tolower (*s2);
  while (len1 >= len2)
    {
      while (tolower (*s1) != c)
	{
	  if (--len1 < len2)
	    return NULL;
	  s1++;
	}

      p1 = s1;
      p2 = s2;
      while (tolower (*p1) == tolower (*p2))
	{
	  p1++;
	  p2++;
	  if (!*p2)
	    return (char *) s1;
	}

      if (p1 == s1)
	{
	  len1--;
	  s1--;
	}
      else
	{
	  len1 -= (p1 - s1) + 1;
	  s1 += (p1 - s1) + 1;
	}
    }
  return NULL;
}

/*
 * ustr_upper() - replace all lower case characters with upper case characters
 *   return: char *
 *   s(in/out)
 */
char *
ustr_upper (char *s)
{
  char *t;

  if (!s)
    return NULL;

  for (t = s; *t; t++)
    *t = toupper (*t);
  return s;
}

/*
 * ustr_lower() - replace all upper case characters with lower case characters
 *   return: char *
 *   s(in/out)
 */
char *
ustr_lower (char *s)
{
  char *t;

  if (!s)
    return NULL;

  for (t = s; *t; t++)
    *t = tolower (*t);
  return s;
}


/* TODO: move to proper place */
/*
 * ansisql_strcasecmp - Case-insensitive string comparison according to ANSI SQL
 *   return: an integer value which is less than zero
 *           if s is lexicographically less than t,
 *           equal to zero if s is equal to t,
 *           and greater than zero if s is greater than zero.
 *   s(in): first string to be compared
 *   t(in): second string to be compared
 *
 * Note: The contents of the null-terminated string s are compared with
 *       the contents of the null-terminated string t, using the ANSI
 *       SQL semantics. That is, if the lengths of the strings are not
 *       the same, the shorter string is considered to be extended
 *       with the blanks on the right, so that both strings have the
 *       same length.
 */
int
ansisql_strcasecmp (const char *s, const char *t)
{
  size_t s_length, t_length, min_length;
  int cmp_val;

  s_length = strlen (s);
  t_length = strlen (t);

  min_length = s_length < t_length ? s_length : t_length;

  cmp_val = intl_identifier_ncasecmp (s, t, (int) min_length);

  /* If not equal for shorter length, return */
  if (cmp_val)
    {
      return cmp_val;
    }

  /* If equal and same size, return */
  if (s_length == t_length)
    {
      return 0;
    }

  /* If equal for shorter length and not same size, look for trailing blanks */
  s += min_length;
  t += min_length;

  if (*s == '\0')
    {
      while (*t != '\0')
	{
	  if (*t++ != ' ')
	    {
	      return -1;
	    }
	}
      return 0;
    }
  else
    {
      while (*s != '\0')
	{
	  if (*s++ != ' ')
	    {
	      return 1;
	    }
	}
      return 0;
    }
}

/* TODO: move to proper place */
/*
 * ansisql_strcmp - String comparison according to ANSI SQL
 *   return: an integer value which is less than zero
 *           if s is lexicographically less than t,
 *           equal to zero if s is equal to t,
 *           and greater than zero if s is greater than zero.
 *   s(in): first string to be compared
 *   t(in): second string to be compared
 *
 * Note: The contents of the null-terminated string s are compared with
 *       the contents of the null-terminated string t, using the ANSI
 *       SQL semantics. That is, if the lengths of the strings are not
 *       the same, the shorter string is considered to be extended
 *       with the blanks on the right, so that both strings have the
 *       same length.
 */
int
ansisql_strcmp (const char *s, const char *t)
{
  for (; *s == *t; s++, t++)
    {
      if (*s == '\0')
	{
	  return 0;
	}
    }

  if (*s == '\0')
    {
      while (*t != '\0')
	{
	  if (*t++ != ' ')
	    {
	      return -1;
	    }
	}
      return 0;
    }
  else if (*t == '\0')
    {
      while (*s != '\0')
	{
	  if (*s++ != ' ')
	    {
	      return 1;
	    }
	}
      return 0;
    }
  else
    {
      return (*(unsigned const char *) s < *(unsigned const char *) t) ? -1 : 1;
    }
}
