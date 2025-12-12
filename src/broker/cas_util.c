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
 * cas_util.c - Utility functions shared by CAS and CGW
 * 
 * This file contains:
 * - Basic utility functions (IP, string, time operations)
 * - Type conversion functions (DB type to CAS type)
 * - SQL parsing functions
 * - Error handling functions
 * - Protocol utility functions
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

#include "cas_util.h"
#include "cas_common_execute.h"
#include "perf_monitor.h"
#include "dbi.h"
#include <strings.h>
#include <stdbool.h>
#include <stddef.h>

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


/*****************************
  move from cas_log.c
 *****************************/


/* ========================================================================
 * Protocol handler utility functions
 * ======================================================================== */

/* Shared string arrays for protocol handler functions */
static const char *schema_type_str[] = {
  "CLASS",
  "VCLASS",
  "QUERY_SPEC",
  "ATTRIBUTE",
  "CLASS_ATTRIBUTE",
  "METHOD",
  "CLASS_METHOD",
  "METHOD_FILE",
  "SUPERCLASS",
  "SUBCLASS",
  "CONSTRAINT",
  "TRIGGER",
  "CLASS_PRIVILEGE",
  "ATTR_PRIVILEGE",
  "DIRECT_SUPER_CLASS",
  "PRIMARY_KEY",
  "IMPORTED_KEYS",
  "EXPORTED_KEYS",
  "CROSS_REFERENCE"
};

static const char *tran_type_str[] = { "COMMIT", "ROLLBACK" };

const char *
get_schema_type_str (int schema_type)
{
  if (schema_type < 1 || schema_type > CCI_SCH_LAST)
    {
      return "";
    }

  return (schema_type_str[schema_type - 1]);
}

const char *
get_tran_type_str (int tran_type)
{
  if (tran_type < CCI_TRAN_COMMIT || tran_type > CCI_TRAN_ROLLBACK)
    {
      return "";
    }

  return (tran_type_str[tran_type - 1]);
}
