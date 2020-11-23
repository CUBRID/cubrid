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
 * get_clock_freq.c - get_clock_freq() function implementation
 */

#ident "$Id$"

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include "tsc_timer.h"

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
