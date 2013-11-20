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

extern void tsc_init (void);
extern void tsc_getticks (TSC_TICKS * tck);
extern void tsc_elapsed_time_usec (TSCTIMEVAL * tv, TSC_TICKS end_tick,
				   TSC_TICKS start_tick);
extern void tsc_start_time_usec (TSC_TICKS * tck);
extern void tsc_end_time_usec (TSCTIMEVAL * tv, TSC_TICKS start_tick);
extern TSC_UINT64 get_clockfreq (void);

#endif /* _TSC_TIMER_H_ */
