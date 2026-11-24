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
 * log_applier_clock.hpp - timed waits of the parallel log applier on the monotonic clock
 *
 * A timed wait measured on the wall clock gets longer or shorter when the system time is set. The applier's
 * condition variables wait on CLOCK_MONOTONIC instead: create them with la_cond_init_monotonic and compute
 * their deadlines with la_monotonic_deadline.
 */

#ifndef _LOG_APPLIER_CLOCK_HPP_
#define _LOG_APPLIER_CLOCK_HPP_

#include "porting.h"

#include <assert.h>
#include <pthread.h>
#include <time.h>

/* Initialize cond so that pthread_cond_timedwait on it reads its deadline on CLOCK_MONOTONIC. */
inline void
la_cond_init_monotonic (pthread_cond_t *cond)
{
  pthread_condattr_t attr;
  int rc;

  rc = pthread_condattr_init (&attr);
  assert (rc == 0);
  rc = pthread_condattr_setclock (&attr, CLOCK_MONOTONIC);
  assert (rc == 0);
  rc = pthread_cond_init (cond, &attr);
  assert (rc == 0);
  (void) pthread_condattr_destroy (&attr);
  (void) rc;
}

/* Read CLOCK_MONOTONIC in nanoseconds; the applier times its scheduling and statistics with it. */
inline UINT64
la_monotonic_now_ns (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  return (UINT64) ts.tv_sec * 1000000000ULL + (UINT64) ts.tv_nsec;
}

/* Set abstime to nsec from now on CLOCK_MONOTONIC, the deadline for a condition from la_cond_init_monotonic. */
inline void
la_monotonic_deadline (UINT64 nsec, struct timespec *abstime)
{
  clock_gettime (CLOCK_MONOTONIC, abstime);
  abstime->tv_sec += (time_t) (nsec / 1000000000ULL);
  abstime->tv_nsec += (long) (nsec % 1000000000ULL);
  if (abstime->tv_nsec >= 1000000000L)
    {
      abstime->tv_sec++;
      abstime->tv_nsec -= 1000000000L;
    }
}

#endif /* _LOG_APPLIER_CLOCK_HPP_ */
