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
#endif /* ENABLE_UNUSED_FUNCTION */
