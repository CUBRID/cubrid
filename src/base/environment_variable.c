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
 * environment_variable.c : Functions for manipulating the environment variable
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include "porting.h"
#ifndef HAVE_STRLCPY
#include "stringl.h"
#endif
#include "error_code.h"
#include "environment_variable.h"

/* available root directory symbols; NULL terminated array */
static const char *envvar_Prefixes[] = {
  "CUBRID", NULL
};
static const char *envvar_Prefix = NULL;

#define _ENVVAR_MAX_LENGTH      255

/*
 * envvar_prefix - find a recognized prefix symbol
 *   return: prefix symbol
 */
const char *
envvar_prefix (void)
{
  const char **candidate;

  if (!envvar_Prefix)
    {
      for (candidate = envvar_Prefixes; *candidate; candidate++)
	{
	  if (getenv (*candidate) != NULL)
	    {
	      envvar_Prefix = *candidate;
	      return envvar_Prefix;
	    }
	}

      fprintf (stderr,
	       "The root directory environment variable $%s is not set.\n",
	       envvar_Prefixes[0]);
      fflush (stderr);
      exit (1);
    }

  return envvar_Prefix;
}

/*
 * envvar_root - get value of the root directory environment variable
 *   return: root directory
 */
const char *
envvar_root (void)
{
  const char *root = getenv (envvar_prefix ());
  return root;
}

/*
 * envvar_name - add the prefix symbol to an environment variable name
 *   return: prefixed name
 *   buf(out): string buffer to store the prefixed name
 *   size(out): size of the buffer
 *   name(in): an environment vraible name
 */
const char *
envvar_name (char *buf, size_t size, const char *name)
{
  const char *prefix;
  char *pname;

  pname = buf;
  prefix = envvar_prefix ();
  while (size > 1 && *prefix)
    {
      *pname++ = *prefix++;
      size--;
    }
  if (size > 1)
    {
      *pname++ = '_';
      size--;
    }
  while (size > 1 && *name)
    {
      *pname++ = *name++;
      size--;
    }
  *pname = '\0';

  return buf;
}

/*
 * envvar_get - get value of an prefixed environment variable
 *   return: a string containing the value for the specified environment
 *           variable
 *   name(in): environment variable name without prefix
 *
 * Note: the prefix symbol will be added to the name
 */
const char *
envvar_get (const char *name)
{
  char buf[_ENVVAR_MAX_LENGTH];

  return getenv (envvar_name (buf, _ENVVAR_MAX_LENGTH, name));
}

/*
 * enclosing_method - change value of an prefixed environment variable
 *   return: error code
 *   name(in): environment variable name without prefix
 *   val(in): the value to be set to the environment variable
 *
 * Note: the prefix symbol will be added to the name
 */
int
envvar_set (const char *name, const char *val)
{
  char buf[_ENVVAR_MAX_LENGTH];
  char *env_buf = (char *) malloc (_ENVVAR_MAX_LENGTH);

  envvar_name (buf, _ENVVAR_MAX_LENGTH, name);
  snprintf (env_buf, _ENVVAR_MAX_LENGTH, "%s=%s", buf, val);

  return (putenv (env_buf) == 0) ? NO_ERROR : ER_FAILED;
}

/*
 * envvar_expand - expand environment variables (${ENV}) with their values
 *                 within the string
 *   return: error code
 *   string(in): string containing environment variables
 *   buffer(out): output buffer
 *   maxlen(in): maximum length of output buffer
 *
 * Note:
 *   The environment variables must be prefixed with a '$' and are
 *   enclosed by '{' and '}' or terminated by a non alpha numeric character
 *   except for '_'.
 *   If all of the referenced environment variables were expanded,
 *   NO_ERROR is returned.  If a reference could not be expanded, the output
 *   buffer will contain the name of the unresolved variable.
 *   This function can handle up to 10 environment variables. It is an error
 *   if exceeds.
 */
int
envvar_expand (const char *string, char *buffer, size_t maxlen)
{
#define _ENVVAR_MAX_EXPANSION   (10 * 2 + 1)
  struct _fragment
  {
    const char *str;
    size_t len;
  } fragments[_ENVVAR_MAX_EXPANSION], *cur, *fen;
  char *s, *e, env[_ENVVAR_MAX_LENGTH], *val;

  s = strchr (string, '$');
  if (!s)
    {
      /* no environment variable in the string */
      (void) strlcpy (buffer, string, maxlen);
      return NO_ERROR;
    }

  cur = fragments;
  fen = fragments + _ENVVAR_MAX_EXPANSION;

  do
    {
      env[0] = '\0';

      if (s[1] == '{')
	{
	  for (e = s + 1; *e && *e != '}'; e++)
	    ;
	  if (*e && (e - s - 2) < _ENVVAR_MAX_LENGTH)
	    {
	      /* ${ENV}; copy the name of the environment variable */
	      (void) strlcpy (env, s + 2, e - s - 1);
	      e++;
	    }
	}
      else
	{
	  for (e = s + 1; *e && (isalnum (*e) || *e == '_'); e++)
	    ;
	  if ((e - s - 1) < _ENVVAR_MAX_LENGTH)
	    {
	      /* $ENV; copy the name of the environment variable */
	      (void) strlcpy (env, s + 1, e - s);
	    }
	}

      if (env[0])
	{
	  /* a environment variable is referred; get the value */
	  val = getenv (env);
	  if (!val)
	    {
	      /* error */
	      (void) strlcpy (buffer, env, maxlen);
	      return ER_FAILED;
	    }
	  if (s > string)
	    {
	      /* a fragment */
	      cur->str = string;
	      cur->len = s - string;
	      if (++cur > fen)
		{
		  /* error */
		  *buffer = '\0';
		  return ER_FAILED;
		}
	    }
	  /* a fragment of the value */
	  cur->str = val;
	  cur->len = strlen (val);
	  if (++cur > fen)
	    {
	      /* error */
	      *buffer = '\0';
	      return ER_FAILED;
	    }
	}
      else
	{
	  cur->str = string;
	  cur->len = e - string;
	  if (++cur > fen)
	    {
	      /* error */
	      *buffer = '\0';
	      return ER_FAILED;
	    }
	}

      string = e;
      s = strchr (string, '$');
    }
  while (s);

  if (*string)
    {
      cur->str = string;
      cur->len = strlen (string);
      cur++;
    }
  cur->str = NULL;
  cur->len = 0;

  /* assemble fragments */
  for (cur = fragments; cur->len && maxlen > 1; cur++)
    {
      if (cur->len >= maxlen)
	{
	  cur->len = maxlen - 1;
	}
      (void) memcpy (buffer, cur->str, cur->len);
      buffer += cur->len;
      maxlen -= cur->len;
    }
  *buffer = '\0';

  return NO_ERROR;
}
