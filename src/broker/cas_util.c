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
 * cas_util.c -
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
#include <assert.h>

#include "cas_common.h"
#include "cas_util.h"
#include "cas_net_buf.h"

char *
ut_uchar2ipstr (unsigned char *ip_addr)
{
  static char ip_str[32];

  assert (ip_addr != NULL);

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
    memmove (str, s, strlen (s) + 1);

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
ut_timeval_diff (struct timeval *start, struct timeval *end, int *res_sec,
		 int *res_msec)
{
  int sec, msec;

  assert (start != NULL && end != NULL && res_sec != NULL
	  && res_msec != NULL);

  sec = end->tv_sec - start->tv_sec;
  msec = (end->tv_usec / 1000) - (start->tv_usec / 1000);
  if (msec < 0)
    {
      msec += 1000;
      sec--;
    }
  *res_sec = sec;
  *res_msec = msec;
}

int
ut_check_timeout (struct timeval *start_time, int timeout_msec, int *res_sec,
		  int *res_msec)
{
  struct timeval cur_time;
  int diff_msec;

  assert (start_time != NULL && res_sec != NULL && res_msec != NULL);

  gettimeofday (&cur_time, NULL);
  ut_timeval_diff (start_time, &cur_time, res_sec, res_msec);
  diff_msec = *res_sec * 1000 + *res_msec;

  return (diff_msec >= timeout_msec) ? diff_msec : -1;
}
