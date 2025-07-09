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

  sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip_addr[0], (unsigned char) ip_addr[1], (unsigned char) ip_addr[2],
	   (unsigned char) ip_addr[3]);
  return (ip_str);
}

void
ut_tolower (char *str)
{
  char *p;

  if (str == NULL)
    return;

  for (p = str; *p; p++)
    {
      *p = char_tolower (*p);
    }
}

void
ut_timeval_diff (struct timeval *start, struct timeval *end, int *res_sec, int *res_msec)
{
  int sec, msec;

  assert (start != NULL && end != NULL && res_sec != NULL && res_msec != NULL);

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
ut_check_timeout (struct timeval *start_time, struct timeval *end_time, int timeout_msec, int *res_sec, int *res_msec)
{
  struct timeval cur_time;
  int diff_msec;

  assert (start_time != NULL && res_sec != NULL && res_msec != NULL);

  if (end_time == NULL)
    {
      end_time = &cur_time;
      gettimeofday (end_time, NULL);
    }
  ut_timeval_diff (start_time, end_time, res_sec, res_msec);

  if (timeout_msec > 0)
    {
      diff_msec = *res_sec * 1000 + *res_msec;
    }
  else
    {
      diff_msec = -1;
    }

  return (diff_msec >= timeout_msec) ? diff_msec : -1;
}
