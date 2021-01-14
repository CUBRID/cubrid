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
 * broker_log_time.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "cas_common.h"
#include "broker_log_time.h"

#if defined (ENABLE_UNUSED_FUNCTION)
int
log_time_make (char *str, T_LOG_TIME * ltm)
{
  int hour, min, sec, msec;

  if (sscanf (str, "%*d%*c%*d%*c%d%*c%d%*c%d%*c%d", &hour, &min, &sec, &msec) < 4)
    {
      fprintf (stderr, "time format error[%s]\n", str);
      return -1;
    }

  ltm->hour = hour;
  ltm->min = min;
  ltm->sec = sec;
  ltm->msec = msec;

  return 0;
}

int
log_time_diff (T_LOG_TIME * t1, T_LOG_TIME * t2)
{
  int diff;

  diff = (t2->hour - t1->hour) * 3600;
  diff += (t2->min - t1->min) * 60;
  diff += (t2->sec - t1->sec);
  diff *= 1000;
  diff += (t2->msec - t1->msec);
  return diff;
}
#endif /* ENABLE_UNUSED_FUNCTION */
