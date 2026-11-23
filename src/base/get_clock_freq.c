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

#if defined (LINUX)
/*
 * clock_paired_read() - read the tick counter and CLOCK_MONOTONIC at the same moment
 *   return: CLOCK_MONOTONIC in nanoseconds, the midpoint of two reads around the tick read
 *   tc(out): tick counter
 *
 * Note: a pair the thread was preempted in the middle of is read again.
 */
static INT64
clock_paired_read (ticks * tc)
{
  struct timespec before, after;
  INT64 before_ns, after_ns;
  int retry;

  for (retry = 0;; retry++)
    {
      clock_gettime (CLOCK_MONOTONIC, &before);
      *tc = getticks ();
      clock_gettime (CLOCK_MONOTONIC, &after);
      before_ns = before.tv_sec * 1000000000LL + before.tv_nsec;
      after_ns = after.tv_sec * 1000000000LL + after.tv_nsec;
      if (after_ns - before_ns < 10000 || retry >= 10)
	{
	  return before_ns + (after_ns - before_ns) / 2;
	}
    }
}
#endif /* LINUX */

/*
 * get_clock_freq() - get the CPU clock rate
 *   return: the rate of the counter getticks () reads (Hz), or 0 when unknown
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
  /* Measure the tick counter against CLOCK_MONOTONIC over about 2 msec. "cpu MHz" of /proc/cpuinfo is the current
   * core clock of one CPU, which is not the TSC rate on a CPU that changes its frequency. */
  ticks start_tc, end_tc;
  INT64 start_ns, end_ns;
  struct timespec ts;

  start_ns = clock_paired_read (&start_tc);
  do
    {
      clock_gettime (CLOCK_MONOTONIC, &ts);
    }
  while (ts.tv_sec * 1000000000LL + ts.tv_nsec - start_ns < 2000000);
  end_ns = clock_paired_read (&end_tc);

  if (end_tc <= start_tc || end_ns <= start_ns)
    {
      return 0;
    }
  return (TSC_UINT64) ((double) (end_tc - start_tc) * 1000000000.0 / (double) (end_ns - start_ns));

#else
  /*
   * Note: Unknown OS. the return value will not be used.
   */
  return 1;

#endif
}
