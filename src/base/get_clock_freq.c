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
 * get_clock_freq.c - get_clock_freq() function implementation
 */

#ident "$Id$"

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include "tsc_timer.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * get_clock_freq() - get the CPU clock rate
 *   return: the CPU or Mainboard clock rate (KHz)
 */
TSC_UINT64
get_clock_freq (void)
{
#if defined (WINDOWS)
  /*
   * Note: It has been implemented for Windows.
   */
  LARGE_INTEGER fr;
  QueryPerformanceFrequency (&fr);
  return (TSC_UINT64) fr.QuadPart;

#elif defined (LINUX)
  /* We read the information from the /proc filesystem. It contains at least one line like cpu MHz : 497.840237 or
   * also cpu MHz : 497.841 We search for this line and convert the number in an integer.  */
  TSC_UINT64 clock_freq = 0;
  int fd;
  char buf[4096], hz[32];
  char *src, *dest, *ovf, *dp = NULL;
  ssize_t n;

  fd = open ("/proc/cpuinfo", O_RDONLY);
  if (fd == -1)
    {
      goto exit;
    }

  n = read (fd, buf, sizeof (buf));
  if (n <= 0)
    {
      goto exit;
    }

  ovf = buf + n;

  src = strstr (buf, "cpu MHz");
  if (src == NULL)
    {
      goto exit;
    }

  dest = hz;

  while (*src != '\n')
    {
      if (*src < '0' || *src > '9')
	{
	  if (*src == '.')
	    {
	      dp = dest;
	    }

	  src++;
	}
      else
	{
	  *dest++ = *src++;
	}

      if (src >= ovf)
	{
	  goto exit;
	}
    }

  if (dp == NULL)
    {
      dp = dest;
    }

  while ((dest - dp) < 6)
    {
      *dest++ = '0';
    }

  *dest = '\0';

  clock_freq = strtoull (hz, NULL, 10);
  if (clock_freq == 0)
    {
      goto exit;
    }

exit:

  if (fd != -1)
    {
      close (fd);
    }

  return clock_freq;

#else
  /*
   * Note: Unknown OS. the return value will not be used.
   */
  return 1;

#endif
}
