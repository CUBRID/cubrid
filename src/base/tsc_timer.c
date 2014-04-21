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
 * tsc_timer.c - Time Stamp Counter(TSC) timer implementations
 */

#ident "$Id$"

#include "config.h"

#include <fcntl.h>
#include "tsc_timer.h"

#define CHECK_CPU_FREQ(v) \
do { \
  if ((v) == 0) \
    { \
      (v) = get_clockfreq (); \
    } \
} while (0)

#define CALCULATE_ELAPSED_TIME_USEC(time, diff, freq) \
do { \
  (time)->tv_sec = (diff) / (freq); \
  (time)->tv_usec = (((diff) % (freq)) * (TSC_UINT64) (1000000)) / (freq); \
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
  cpu_Clock_rate = get_clockfreq ();
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
tsc_elapsed_time_usec (TSCTIMEVAL * tv, TSC_TICKS end_tick,
		       TSC_TICKS start_tick)
{
  if (power_Savings == 0)
    {
      TSC_UINT64 diff_tsc;

      /* Sometimes the time goes backwards in the MULTI-CORE processor world.
         But it is a negligible level. */
      if (end_tick.tc < start_tick.tc)
	{
	  tv->tv_sec = 0;
	  tv->tv_usec = 0;
	  return;
	}

      CHECK_CPU_FREQ (cpu_Clock_rate);
      diff_tsc =
	(TSC_UINT64) elapsed ((ticks) end_tick.tc, (ticks) start_tick.tc);
      CALCULATE_ELAPSED_TIME_USEC (tv, diff_tsc, cpu_Clock_rate);
    }
  else
    {
      CALCULATE_ELAPSED_TIMEVAL (tv, end_tick.tv, start_tick.tv);
    }
  return;
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
      diff_tsc =
	(TSC_UINT64) elapsed ((ticks) end_tick.tc, (ticks) start_tick.tc);
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
