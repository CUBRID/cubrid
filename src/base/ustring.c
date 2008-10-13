/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * ustring.c : case insensitive string comparison routines for 8-bit
 *             character sets
 *
 */

#ident "$Id$"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ustring.h"

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
