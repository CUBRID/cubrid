/* Copyright (C) 2002-2013 Free Software Foundation, Inc.              
   This file is part of the GNU C Library.                             
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.           
                                                                       
   The GNU C Library is free software; you can redistribute it and/or  
   modify it under the terms of the GNU Lesser General Public          
   License as published by the Free Software Foundation; either        
   version 2.1 of the License, or (at your option) any later version.  

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of      
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   
   Lesser General Public License for more details.                     
                                                                       
   You should have received a copy of the GNU Lesser General Public    
   License along with the GNU C Library; if not, see                   
   <http://www.gnu.org/licenses/>.  */

/*
 * perf.c - get_clockfreq() function implementation
 */

#ident "$Id$"

#include "config.h"

#include <fcntl.h>
#include "tsc_timer.h"

/*
 * get_clockfreq() - get the CPU clock rate
 *   return: the CPU or Mainboard clock rate (KHz)
 */
TSC_UINT64
get_clockfreq (void)
{
#if defined (WINDOWS)
  /* 
   * Note: It has been implemented for Windows. 
   */
  LARGE_INTEGER fr;
  QueryPerformanceFrequency (&fr);
  return (TSC_UINT64) fr.QuadPart;

#elif defined (LINUX)
  /* 
   * Note: The implementation is derived from glibc-2.18. 
   */

  /* We read the information from the /proc filesystem.  It contains at
     least one line like
     cpu MHz         : 497.840237
     or also
     cpu MHz         : 497.841
     We search for this line and convert the number in an integer.  */
  TSC_UINT64 result = 0;
  int fd;

  fd = open ("/proc/cpuinfo", O_RDONLY);
  if (fd != -1)
    {
      /* XXX AFAIK the /proc filesystem can generate "files" only up
         to a size of 4096 bytes.  */
      char buf[4096];
      ssize_t n;

      n = read (fd, buf, sizeof buf);
      if (n > 0)
	{
	  char *mhz = strstr (buf, "cpu MHz");

	  if (mhz != NULL)
	    {
	      char *endp = buf + n;
	      int seen_decpoint = 0;
	      int ndigits = 0;

	      /* Search for the beginning of the string.  */
	      while (mhz < endp && (*mhz < '0' || *mhz > '9') && *mhz != '\n')
		++mhz;

	      while (mhz < endp && *mhz != '\n')
		{
		  if (*mhz >= '0' && *mhz <= '9')
		    {
		      result *= 10;
		      result += *mhz - '0';
		      if (seen_decpoint)
			++ndigits;
		    }
		  else if (*mhz == '.')
		    seen_decpoint = 1;

		  ++mhz;
		}

	      /* Compensate for missing digits at the end.  */
	      while (ndigits++ < 6)
		result *= 10;
	    }
	}

      close (fd);
    }

  return result;

#else
  /* 
   * Note: Unknown OS. the return value will not be used. 
   */
  return 1;

#endif
}
