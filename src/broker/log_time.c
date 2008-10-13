/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * log_time.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "cas_common.h"
#include "log_time.h"

int
log_time_make (char *str, T_LOG_TIME * ltm)
{
  int hour, min, sec, msec;

  if (sscanf (str, "%*d%*c%*d%*c%d%*c%d%*c%d%*c%d", &hour, &min, &sec, &msec)
      < 4)
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

