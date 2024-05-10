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
 * tsc_timer.c - Time Stamp Counter(TSC) timer implementations
 */

#ident "$Id$"

#include "config.h"

#include <fcntl.h>
#include "tsc_timer.h"
#if !defined(WINDOWS)
#include <sys/time.h>
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define CHECK_CPU_FREQ(v) \
do { \
  if ((v) == 0) \
    { \
      (v) = get_clock_freq (); \
    } \
} while (0)

#define CALCULATE_ELAPSED_TIME_USEC(time, diff, freq) \
do { \
  (time)->tv_sec = (long)((diff) / (freq)); \
  (time)->tv_usec = (long)((((diff) % (freq)) * (TSC_UINT64) (1000000)) / (freq)); \
} while (0)

#define CALCULATE_ELAPSED_TIMEVAL(time, end, start) \
do { \
  (time)->tv_sec = (end).tv_sec - (start).tv_sec; \
  (time)->tv_usec = (end).tv_usec - (start).tv_usec; \
  if ((time)->tv_usec < 0) \
    { \
      (time)->tv_sec--; \
      (time)->tv_usec += 1000000; \
    } \
} while (0)

static int power_Savings = -1;
static TSC_UINT64 cpu_Clock_rate = 0;

static void check_power_savings (void);

/*
 * tsc_init() - initialize the tsc_timer
 */
void
tsc_init (void)
{
  check_power_savings ();
  cpu_Clock_rate = get_clock_freq ();
  return;
}

/*
 * tsc_getticks() - get the current Time Stamp Counter
 *   tck(out): current CPU ticks or timeval
 *
 * Note: Ticks does not mean the current TIME.
 */
void
tsc_getticks (TSC_TICKS * tck)
{
  if (power_Savings == 0)
    {
      tck->tc = getticks ();
    }
  else
    {
      gettimeofday (&(tck->tv), NULL);
    }
  return;
}

/*
 * tsc_elapsed_time_usec() - measure the elapsed time in microseconds
 *   tv(out)       : elapsed time (sec, usec)
 *   end_tick(in)  : end time
 *   start_tick(in): start time
 */
void
tsc_elapsed_time_usec (TSCTIMEVAL * tv, TSC_TICKS end_tick, TSC_TICKS start_tick)
{
  if (power_Savings == 0)
    {
      TSC_UINT64 diff_tsc;

      /* Sometimes the time goes backwards in the MULTI-CORE processor world. But it is a negligible level. */
      if (end_tick.tc < start_tick.tc)
	{
	  tv->tv_sec = 0;
	  tv->tv_usec = 0;
	  return;
	}

      CHECK_CPU_FREQ (cpu_Clock_rate);
      diff_tsc = (TSC_UINT64) elapsed ((ticks) end_tick.tc, (ticks) start_tick.tc);
      CALCULATE_ELAPSED_TIME_USEC (tv, diff_tsc, cpu_Clock_rate);
    }
  else
    {
      CALCULATE_ELAPSED_TIMEVAL (tv, end_tick.tv, start_tick.tv);
    }
  return;
}

/*
 * tsc_elapsed_utime () - measure the elapsed time in microseconds (not
 *			  seconds, microseconds like tsc_elapsed_time_usec).
 *
 * return	   : Elapsed time (microseconds).
 * end_tick (in)   : End time.
 * start_tick (in) : Start time.
 */
UINT64
tsc_elapsed_utime (TSC_TICKS end_tick, TSC_TICKS start_tick)
{
  TSCTIMEVAL tv;
  tsc_elapsed_time_usec (&tv, end_tick, start_tick);
  return tv.tv_sec * 1000000LL + tv.tv_usec;
}

/*
 * tsc_start_time_usec() - get the current Time Stamp Counter
 *   tck(out): current CPU ticks or timeval
 *
 * Note: This function does exactly same thing with tsc_getticks().
 *       It pairs up with tsc_end_time_usec().
 */
void
tsc_start_time_usec (TSC_TICKS * tck)
{
  tsc_getticks (tck);
  return;
}

/*
 * tsc_end_time_usec() - measure the elapsed time in microseconds
 *   tv(out)       : elapsed time (sec, usec)
 *   start_tick(in): start time
 */
void
tsc_end_time_usec (TSCTIMEVAL * tv, TSC_TICKS start_tick)
{
  TSC_TICKS end_tick;
  tsc_getticks (&end_tick);

  if (power_Savings == 0)
    {
      TSC_UINT64 diff_tsc;
      if (end_tick.tc < start_tick.tc)
	{
	  tv->tv_sec = 0;
	  tv->tv_usec = 0;
	  return;
	}

      CHECK_CPU_FREQ (cpu_Clock_rate);
      diff_tsc = (TSC_UINT64) elapsed ((ticks) end_tick.tc, (ticks) start_tick.tc);
      CALCULATE_ELAPSED_TIME_USEC (tv, diff_tsc, cpu_Clock_rate);
    }
  else
    {
      CALCULATE_ELAPSED_TIMEVAL (tv, end_tick.tv, start_tick.tv);
    }
  return;
}

/*
 * check_power_savings() - check power saving options
 */
static void
check_power_savings (void)
{
#if defined (WINDOWS)
  /*
   * Note: Windows's QueryPerformanceFrequency always returns
   *       the stable CPU or mainboard clock rate.
   */
  power_Savings = 0;

#elif defined (LINUX)
  /*
   * Note: 'power_saving value == zero' means that the CPU clock rate is fixed.
   */
  int fd_mc, fd_smt;
  char mc = 0, smt = 0;

  fd_mc = open ("/sys/devices/system/cpu/sched_mc_power_savings", O_RDONLY);
  if (fd_mc != -1)
    {
      char tmp = 0;
      ssize_t n;

      n = read (fd_mc, &tmp, 1);
      if (n > 0)
	{
	  mc = tmp;
	}
      close (fd_mc);
    }

  fd_smt = open ("/sys/devices/system/cpu/sched_smt_power_savings", O_RDONLY);
  if (fd_smt != -1)
    {
      char tmp = 0;
      ssize_t n;

      n = read (fd_smt, &tmp, 1);
      if (n > 0)
	{
	  smt = tmp;
	}
      close (fd_smt);
    }

  if (mc == '0' && smt == '0')
    {
      power_Savings = 0;
      return;
    }

  power_Savings = 1;

#else
  /*
   * Note: We assume that the unknown OS performs the power-saving policy.
   */
  power_Savings = 1;

#endif
  return;
}
