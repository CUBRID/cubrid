/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cas_util.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef WIN32
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "cas_util.h"

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
