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
 * broker_log_util.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(WINDOWS)
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "broker_log_util.h"
#include "cas_cci.h"

static bool is_bind_with_size (char *buf, int *tot_val_size, int *info_size);

char *
ut_trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str;
       *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
       s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    memcpy (str, s, strlen (s) + 1);

  return (str);
}

void
ut_tolower (char *str)
{
  char *p;

  if (str == NULL)
    return;

  for (p = str; *p; p++)
    {
      if (*p >= 'A' && *p <= 'Z')
	*p = *p - 'A' + 'a';
    }
}

#if defined(BROKER_LOG_RUNNER)
static bool
is_bind_with_size (char *buf, int *tot_val_size, int *info_size)
{
  char *p;
  int type;
  int size;

  if (tot_val_size)
    {
      *tot_val_size = 0;
    }

  if (strncmp (buf, "B ", 1) != 0)
    {
      return false;
    }

  type = atoi (buf + 2);
  if ((type != CCI_U_TYPE_CHAR) && (type != CCI_U_TYPE_STRING)
      && (type != CCI_U_TYPE_NCHAR) && (type != CCI_U_TYPE_VARNCHAR)
      && (type != CCI_U_TYPE_BIT) && (type != CCI_U_TYPE_VARBIT)
      && (type != CCI_U_TYPE_ENUM))
    {
      return false;
    }

  p = strchr (buf + 2, ' ');
  if (p == NULL)
    {
      goto error_on_val_size;
    }

  size = atoi (p);
  p = strchr (p + 1, ' ');
  if (p == NULL)
    {
      goto error_on_val_size;
    }

  if (info_size)
    {
      *info_size = (char *) (p + 1) - (char *) buf;
    }
  if (tot_val_size)
    {
      *tot_val_size = size;
    }
  return true;

error_on_val_size:
  if (tot_val_size)
    {
      *tot_val_size = -1;
    }
  return false;
}
#else /* BROKER_LOG_RUNNER */
static bool
is_bind_with_size (char *buf, int *tot_val_size, int *info_size)
{
  char *msg;
  char *p, *q;
  char size[256];
  char *value_p;
  char *size_begin;
  char *size_end;
  char *info_end;
  int len;

  if (info_size)
    {
      *info_size = 0;
    }
  if (tot_val_size)
    {
      *tot_val_size = 0;
    }

  GET_MSG_START_PTR (msg, buf);
  if (strncmp (msg, "bind ", 5) != 0)
    {
      return false;
    }

  p = strchr (msg, ':');
  if (p == NULL)
    {
      return false;
    }
  p += 2;

  if ((strncmp (p, "CHAR", 4) != 0) && (strncmp (p, "VARCHAR", 7) != 0)
      && (strncmp (p, "NCHAR", 5) != 0) && (strncmp (p, "VARNCHAR", 8) != 0)
      && (strncmp (p, "BIT", 3) != 0) && (strncmp (p, "VARBIT", 6) != 0))
    {
      return false;
    }

  q = strchr (p, ' ');
  if (q == NULL)
    {
      /* log error case or NULL bind type */
      return false;
    }

  *q = '\0';
  value_p = q + 1;

  size_begin = strstr (value_p, "(");
  if (size_begin == NULL)
    {
      goto error_on_val_size;
    }
  size_begin += 1;
  size_end = strstr (value_p, ")");
  if (size_end == NULL)
    {
      goto error_on_val_size;
    }

  info_end = size_end + 1;

  if (info_size)
    {
      *info_size = (char *) info_end - (char *) buf;
    }
  if (tot_val_size)
    {
      len = size_end - size_begin;
      if (len > (int) sizeof (size))
	{
	  goto error_on_val_size;
	}
      if (len > 0)
	{
	  memcpy (size, size_begin, len);
	  size[len] = '\0';
	}
      *tot_val_size = atoi (size);
      if (*tot_val_size < 0)
	{
	  goto error_on_val_size;
	}
    }

  return true;

error_on_val_size:
  if (info_size)
    {
      *info_size = -1;
    }
  if (tot_val_size)
    {
      *tot_val_size = -1;
    }
  return false;
}
#endif /* BROKER_LOG_RUNNER */

int
ut_get_line (FILE * fp, T_STRING * t_str, char **out_str, int *lineno)
{
  char buf[1024];
  int out_str_len;
  bool is_first, bind_with_size;
  int tot_val_size, info_size;
  long position;

  t_string_clear (t_str);

  is_first = true;
  while (1)
    {
      memset (buf, 0, sizeof (buf));
      position = ftell (fp);
      if (fgets (buf, sizeof (buf), fp) == NULL)
	break;
      /* if it is (debug) line, skip it */
      if (strncmp (buf + 19, "(debug)", 7) == 0)
	{
	  continue;
	}
      if (is_first)
	{
	  bind_with_size = is_bind_with_size (buf, &tot_val_size, &info_size);
	  if (tot_val_size < 0 || (tot_val_size + info_size) > INT_MAX)
	    {
	      fprintf (stderr, "log error\n");
	      return -1;
	    }
	  is_first = false;
	}

      if (bind_with_size)
	{
	  size_t rlen;
	  char *value = NULL;

	  value = (char *) MALLOC (info_size + tot_val_size + 1);
	  if (value == NULL)
	    {
	      fprintf (stderr, "memory allocation error.\n");
	      return -1;
	    }
	  fseek (fp, position, SEEK_SET);
	  rlen =
	    fread ((void *) value, sizeof (char), info_size + tot_val_size,
		   fp);
	  if (t_bind_string_add
	      (t_str, value, info_size + tot_val_size, tot_val_size) < 0)
	    {
	      fprintf (stderr, "memory allocation error.\n");
	      FREE_MEM (value);
	      return -1;
	    }
	  FREE_MEM (value);
	  break;
	}
      else
	{
	  if (t_string_add (t_str, buf, strlen (buf)) < 0)
	    {
	      fprintf (stderr, "memory allocation error.\n");
	      return -1;
	    }
	  if (buf[sizeof (buf) - 2] == '\0' || buf[sizeof (buf) - 2] == '\n')
	    break;
	}
    }

  out_str_len = t_string_len (t_str);
  if (out_str)
    *out_str = t_string_str (t_str);
  if (lineno)
    *lineno = *lineno + 1;
  return out_str_len;
}
