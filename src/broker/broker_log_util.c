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

char *
ut_uchar2ipstr (unsigned char *ip_addr)
{
  static char ip_str[32];

  sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip_addr[0],
	   (unsigned char) ip_addr[1],
	   (unsigned char) ip_addr[2], (unsigned char) ip_addr[3]);
  return (ip_str);
}

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

void
ut_timeval_diff (T_TIMEVAL * start, T_TIMEVAL * end, int *res_sec,
		 int *res_msec)
{
  int sec, msec;

  sec = TIMEVAL_GET_SEC (end) - TIMEVAL_GET_SEC (start);
  msec = TIMEVAL_GET_MSEC (end) - TIMEVAL_GET_MSEC (start);

  if (msec < 0)
    {
      msec += 1000;
      sec--;
    }

  *res_sec = sec;
  *res_msec = msec;
}

int
ut_get_line (FILE * fp, T_STRING * t_str, char **out_str, int *lineno)
{
  char buf[1024];
  int out_str_len;

  t_string_clear (t_str);

  while (1)
    {
      memset (buf, 0, sizeof (buf));
      if (fgets (buf, sizeof (buf), fp) == NULL)
	break;
      /* if it is (debug) line, skip it */
      if (strncmp (buf + 19, "(debug)", 7) == 0)
	{
	  continue;
	}
      if (t_string_add (t_str, buf, strlen (buf)) < 0)
	{
	  fprintf (stderr, "memory allocation error.\n");
	  return -1;
	}
      if (buf[sizeof (buf) - 2] == '\0' || buf[sizeof (buf) - 2] == '\n')
	break;
    }

  out_str_len = t_string_len (t_str);
  if (out_str)
    *out_str = t_string_str (t_str);
  if (lineno)
    *lineno = *lineno + 1;
  return out_str_len;
}
