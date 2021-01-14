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
 * tsc_timer.h - Time Stamp Counter(TSC) timer definitions
 */

#ifndef _TSC_TIMER_H_
#define _TSC_TIMER_H_

#ident "$Id$"

#include "porting.h"
#include "cycle.h"

#define TSC_ADD_TIMEVAL(total, diff) \
do { \
  (total).tv_usec += (diff).tv_usec; \
  (total).tv_sec += (diff).tv_sec; \
  (total).tv_sec += (total).tv_usec / 1000000; \
  (total).tv_usec %= 1000000; \
} while (0)

typedef UINT64 TSC_UINT64;
typedef struct timeval TSCTIMEVAL;

typedef union tsc_ticks TSC_TICKS;
union tsc_ticks
{
  ticks tc;			/* ticks from cycle.h */
  struct timeval tv;		/* microseconds */
};

/* todo: inline functions */

extern void tsc_init (void);
extern void tsc_getticks (TSC_TICKS * tck);
extern void tsc_elapsed_time_usec (TSCTIMEVAL * tv, TSC_TICKS end_tick, TSC_TICKS start_tick);
extern UINT64 tsc_elapsed_utime (TSC_TICKS end_tick, TSC_TICKS start_tick);
extern void tsc_start_time_usec (TSC_TICKS * tck);
extern void tsc_end_time_usec (TSCTIMEVAL * tv, TSC_TICKS start_tick);

extern TSC_UINT64 get_clock_freq (void);
#endif /* _TSC_TIMER_H_ */
