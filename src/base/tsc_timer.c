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
#include <string.h>
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
#if defined (__SIZEOF_INT128__)
/* microseconds per tick in 64.64 fixed point, (10^6 << 64) / cpu_Clock_rate; 0 falls back to the division */
static TSC_UINT64 usec_Per_tick = 0;
#endif

static int check_power_savings (void);
#if defined (LINUX)
static bool cpu_has_flag (const char *flags, const char *flag);
#endif
static inline TSC_UINT64 tsc_ticks_to_usec (TSC_UINT64 tc);
static inline TSC_UINT64 tsc_elapsed_ticks_usec (TSC_TICKS end_tick, TSC_TICKS start_tick);

/*
 * tsc_init() - initialize the tsc_timer
 *
 * Note: the tick source is chosen once per process; a start tick taken before a change of source would not pair
 *       with an end tick taken after it.
 */
void
tsc_init (void)
{
  int power_savings;

  if (power_Savings != -1)
    {
      return;
    }

  power_savings = check_power_savings ();
  if (power_savings == 0)
    {
      cpu_Clock_rate = get_clock_freq ();
      if (cpu_Clock_rate == 0)
	{
	  power_savings = 1;
	}
#if defined (__SIZEOF_INT128__)
      else if (cpu_Clock_rate > 1000000)
	{
	  usec_Per_tick = (TSC_UINT64) ((((unsigned __int128) 1000000) << 64) / cpu_Clock_rate);
	}
#endif
    }

  power_Savings = power_savings;
  return;
}

/*
 * tsc_ticks_to_usec() - convert a tick counter value to microseconds
 *   return: microseconds since the counter started, rounded down
 *   tc(in): tick counter value
 */
static inline TSC_UINT64
tsc_ticks_to_usec (TSC_UINT64 tc)
{
#if defined (__SIZEOF_INT128__)
  if (usec_Per_tick != 0)
    {
      return (TSC_UINT64) (((unsigned __int128) tc * usec_Per_tick) >> 64);
    }
#endif

  CHECK_CPU_FREQ (cpu_Clock_rate);
  return (tc / cpu_Clock_rate) * 1000000 + ((tc % cpu_Clock_rate) * 1000000) / cpu_Clock_rate;
}

/*
 * tsc_elapsed_ticks_usec() - measure the elapsed microseconds between two tick counter values
 *   return: the difference of the two times in microseconds (rounded down), the elapsed time rounded down or up
 *   end_tick(in)  : end time
 *   start_tick(in): start time
 *
 * Note: rounding each interval down would lose half a microsecond per interval on average, and the time of a
 *       row is mostly below one; the difference of rounded times, like clock_gettime microseconds, is right on
 *       average.
 */
static inline TSC_UINT64
tsc_elapsed_ticks_usec (TSC_TICKS end_tick, TSC_TICKS start_tick)
{
  /* Sometimes the time goes backwards in the MULTI-CORE processor world. But it is a negligible level. */
  if (end_tick.tc < start_tick.tc)
    {
      return 0;
    }
  return tsc_ticks_to_usec ((TSC_UINT64) end_tick.tc) - tsc_ticks_to_usec ((TSC_UINT64) start_tick.tc);
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
#if defined (WINDOWS)
      gettimeofday (&(tck->tv), NULL);
#else
      struct timespec ts;
      /* replace gettimeofday with clock_gettime(CLOCK_MONOTONIC) for performance and monotonicity,
       * avoiding CLOCK_REALTIME_COARSE's low resolution and exposure to NTP jumps */
      clock_gettime (CLOCK_MONOTONIC, &ts);
      tck->tv.tv_sec = ts.tv_sec;
      tck->tv.tv_usec = ts.tv_nsec / 1000;
#endif
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
      TSC_UINT64 usec = tsc_elapsed_ticks_usec (end_tick, start_tick);

      tv->tv_sec = (long) (usec / 1000000);
      tv->tv_usec = (long) (usec % 1000000);
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

  if (power_Savings == 0)
    {
      return tsc_elapsed_ticks_usec (end_tick, start_tick);
    }

  CALCULATE_ELAPSED_TIMEVAL (&tv, end_tick.tv, start_tick.tv);
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

  tsc_elapsed_time_usec (tv, end_tick, start_tick);
  return;
}

/*
 * check_power_savings() - check power saving options
 *   return: 0 when the CPU tick counter can serve as a clock, 1 when gettimeofday/clock_gettime must be used
 */
static int
check_power_savings (void)
{
#if defined (WINDOWS)
  /*
   * Note: Windows's QueryPerformanceFrequency always returns
   *       the stable CPU or mainboard clock rate.
   */
  return 0;

#elif defined (LINUX)
  /*
   * Note: the TSC is a clock when its rate does not follow the core clock (constant_tsc) and it keeps counting in
   *       deep C-states (nonstop_tsc); both are in the "flags" line of /proc/cpuinfo. The TSCs of the CPUs must
   *       also agree: the kernel checks that and keeps the TSC as its clock source only while they do.
   */
  char buf[16384];
  char *flags, *eol;
  ssize_t n, len = 0;
  int fd;

  fd = open ("/proc/cpuinfo", O_RDONLY);
  if (fd == -1)
    {
      return 1;
    }
  while (len < (ssize_t) sizeof (buf) - 1 && (n = read (fd, buf + len, sizeof (buf) - 1 - len)) > 0)
    {
      len += n;
    }
  close (fd);
  buf[len] = '\0';

  flags = strstr (buf, "\nflags");
  if (flags == NULL || (eol = strchr (flags + 1, '\n')) == NULL)
    {
      return 1;
    }
  *eol = '\0';
  if (!cpu_has_flag (flags, "constant_tsc") || !cpu_has_flag (flags, "nonstop_tsc"))
    {
      return 1;
    }

  /* the kernel keeps both flags when it finds the TSCs out of step; it only changes its clock source */
  fd = open ("/sys/devices/system/clocksource/clocksource0/current_clocksource", O_RDONLY);
  if (fd == -1)
    {
      return 1;
    }
  n = read (fd, buf, sizeof (buf) - 1);
  close (fd);

  return (n == 4 && strncmp (buf, "tsc\n", 4) == 0) ? 0 : 1;

#else
  /*
   * Note: We assume that the unknown OS performs the power-saving policy.
   */
  return 1;

#endif
}

#if defined (LINUX)
/*
 * cpu_has_flag() - is flag a whole word of the cpuinfo flags line
 *   flags(in): the "flags" line, starting at a newline
 *   flag(in) : flag name
 */
static bool
cpu_has_flag (const char *flags, const char *flag)
{
  size_t len = strlen (flag);
  const char *p;

  for (p = strstr (flags, flag); p != NULL; p = strstr (p + len, flag))
    {
      if (p[-1] == ' ' && (p[len] == ' ' || p[len] == '\0'))
	{
	  return true;
	}
    }
  return false;
}
#endif
