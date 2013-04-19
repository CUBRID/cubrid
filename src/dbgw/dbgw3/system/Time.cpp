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

#include "dbgw3/Common.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/Time.h"

namespace dbgw
{

  namespace system
  {

    uint64_t getCurrTimeMilSec()
    {
      struct timeval now;
      gettimeofday(&now, NULL);

      uint64_t ulCurrTimeMilSec = ((uint64_t) now.tv_sec) * 1000;
      ulCurrTimeMilSec += ((uint64_t) now.tv_usec) / 1000;

      return ulCurrTimeMilSec;
    }

    uint64_t getdifftimeofday(struct timeval &begin)
    {
      struct timeval now;
      gettimeofday(&now, NULL);

      uint64_t ulDiffTimeMilSec = (now.tv_sec - begin.tv_sec) * 1000;
      ulDiffTimeMilSec += (now.tv_usec - begin.tv_usec) / 1000;

      return ulDiffTimeMilSec;
    }

    std::string getTimeStrFromMilSec(uint64_t ulMilSec)
    {
      struct tm cal;
      char buf[25];
      time_t now = ulMilSec / 1000;

      localtime_r(&now, &cal);
      cal.tm_year += 1900;
      cal.tm_mon += 1;
      snprintf(buf, 25, "%4d-%02d-%02d %02d:%02d:%02d.%03d",
          cal.tm_year, cal.tm_mon, cal.tm_mday, cal.tm_hour, cal.tm_min,
          cal.tm_sec, (int)(ulMilSec % 1000));

      return buf;
    }

  }

}
