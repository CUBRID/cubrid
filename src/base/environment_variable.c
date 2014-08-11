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
#include <assert.h>

#include "porting.h"
#include "error_code.h"
#include "environment_variable.h"

/* available root directory symbols; NULL terminated array */
static const char envvar_Prefix_name[] = "CUBRID";
static const char *envvar_Prefix = NULL;
static const char *envvar_Root = NULL;

#define _ENVVAR_MAX_LENGTH      255

typedef enum
{
  ENV_INVALID_DIR,
  ENV_DONT_EXISTS_ROOT,
  ENV_MUST_ABS_PATH,
  ENV_TOO_LONG
} ENV_ERR_MSG;

static const char *env_msg[] = {
  "The directory in $%s is invalid. (%s)\n",
  "The root directory environment variable $%s is not set.\n",
  "The $%s should be an absolute path. (%s)\n",
  "The $%s is too long. (%s)\n"
};

static void
envvar_check_environment (void)
{
#if defined(WINDOWS)
  return;
#else
  const char *cubrid_tmp = envvar_get ("TMP");

  if (cubrid_tmp)
    {
      char name[_ENVVAR_MAX_LENGTH];
      size_t len = strlen (cubrid_tmp);
      size_t limit = 108 - 12;
      /* 108 = sizeof (((struct sockaddr_un *) 0)->sun_path) */
      /* 12  = ("CUBRID65384" + 1) */
      envvar_name (name, _ENVVAR_MAX_LENGTH, "TMP");
      if (!IS_ABS_PATH (cubrid_tmp))
	{
	  fprintf (stderr, env_msg[ENV_MUST_ABS_PATH], name, cubrid_tmp);
	  fflush (stderr);
	  exit (1);
	}
      if (len > limit)
	{
	  fprintf (stderr, env_msg[ENV_TOO_LONG], name, cubrid_tmp);
	  fflush (stderr);
	  exit (1);
	}
    }
#endif
}

/*
 * envvar_prefix - find a recognized prefix symbol
 *   return: prefix symbol
 */
const char *
envvar_prefix (void)
{
  if (!envvar_Prefix)
    {
#if !defined (DO_NOT_USE_CUBRIDENV)
      envvar_Root = getenv (envvar_Prefix_name);
      if (envvar_Root != NULL)
	{
#if !defined (WINDOWS)
	  if (access (envvar_Root, F_OK) != 0)
	    {
	      fprintf (stderr, env_msg[ENV_INVALID_DIR],
		       envvar_Prefix_name, envvar_Root);
	      fflush (stderr);
	      exit (1);
	    }
#endif

	  envvar_Prefix = envvar_Prefix_name;
	}
      else
	{
	  fprintf (stderr, env_msg[ENV_DONT_EXISTS_ROOT], envvar_Prefix_name);
	  fflush (stderr);
	  exit (1);
	}
      envvar_check_environment ();
#else
      envvar_Prefix = envvar_Prefix_name;
      envvar_Root = CUBRID_PREFIXDIR;
#endif
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
  if (envvar_Root == NULL)
    {
      envvar_prefix ();
    }

  return envvar_Root;
}

/*
 * envvar_name - add the prefix symbol to an environment variable name
 *   return: prefixed name
 *   buf(out): string buffer to store the prefixed name
 *   size(out): size of the buffer
 *   name(in): an environment variable name
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
  int ret;

  envvar_name (buf, _ENVVAR_MAX_LENGTH, name);
  ret = setenv (buf, val, 1);
  if (ret != 0)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
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

char *
envvar_bindir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/bin/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_BINDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_libdir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/lib/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_LIBDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_javadir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/java/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_JAVADIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_localedir_file (char *path, size_t size, const char *langpath,
		       const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/msg/%s/%s", envvar_Root, langpath, filename);
#else
  snprintf (path, size, "%s/%s/%s", CUBRID_LOCALEDIR, langpath, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_confdir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/conf/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_CONFDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_vardir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/var/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_VARDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_tmpdir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/tmp/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_TMPDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_logdir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/log/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_LOGDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

void
envvar_trim_char (char *env_val, const int c)
{
  char *buf;
  int size;

  if (env_val == NULL)
    {
      return;
    }

  size = strlen (env_val);

  if (*env_val == c && size > 2)
    {
      buf = (char *) malloc (1 + size);
      if (buf != NULL)
	{
	  strcpy (buf, env_val);
	  strncpy (env_val, buf + 1, size - 2);
	  env_val[size - 2] = '\0';

	  free (buf);
	}
    }
}

char *
envvar_ldmldir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/locales/data/ldml/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_CONFDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_codepagedir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/locales/data/codepages/%s", envvar_Root,
	    filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_CONFDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_localedatadir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/locales/data/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_CONFDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}

char *
envvar_loclib_dir_file (char *path, size_t size, const char *filename)
{
  assert (filename != NULL);

#if !defined (DO_NOT_USE_CUBRIDENV)
  if (envvar_Root == NULL)
    {
      envvar_root ();
    }
  snprintf (path, size, "%s/locales/loclib/%s", envvar_Root, filename);
#else
  snprintf (path, size, "%s/%s", CUBRID_CONFDIR, filename);
#endif

  path[size - 1] = '\0';
  return path;
}
