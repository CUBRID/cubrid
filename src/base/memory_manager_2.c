/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * memory_alloc.c - Memory allocation module
 * TODO: rename this file to memory_alloc.c
 * TODO: include object/memory_manager_4.c and remove it
 * TODO: include object/memory_manager_5.c and remove it
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "ustring.h"
#include "dbtype.h"
#include "memory_manager_2.h"
#include "util_func.h"
#include "error_manager.h"
#include "intl.h"
#include "memory_manager_4.h"

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
    if (*s == '\0')
      return 0;

  if (*s == '\0')
    {
      while (*t != '\0')
	if (*t++ != ' ')
	  return -1;
      return 0;
    }
  else if (*t == '\0')
    {
      while (*s != '\0')
	if (*s++ != ' ')
	  return 1;
      return 0;
    }
  else
    return (*(unsigned const char *) s < *(unsigned const char *) t) ? -1 : 1;
}

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
  cmp_val = intl_mbs_ncasecmp (s, t, min_length);
  /* If not equal for shorter length, return */
  if (cmp_val)
    return cmp_val;
  /* If equal and same size, return */
  if (s_length == t_length)
    return 0;
  /* If equal for shorter length and not same size, look for trailing blanks */
  s += min_length;
  t += min_length;

  if (*s == '\0')
    {
      while (*t != '\0')
	if (*t++ != ' ')
	  return -1;
      return 0;
    }
  else
    {
      while (*s != '\0')
	if (*s++ != ' ')
	  return 1;
      return 0;
    }
}
