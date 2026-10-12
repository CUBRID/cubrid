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
 * log_applier_debug.hpp - logging and timing macros for the parallel applier
 */

#ifndef _LOG_APPLIER_DEBUG_HPP_
#define _LOG_APPLIER_DEBUG_HPP_

#include "porting.h"

#include <sys/time.h>

#if !defined (NDEBUG)
#define LA_DEBUG_LOG(...) er_log_debug (__VA_ARGS__)
#else /* !NDEBUG */
#define LA_DEBUG_LOG(...) ((void) 0)
#endif /* !NDEBUG */

#define LA_TIME_BEGIN(tv) gettimeofday (&(tv), NULL)
#define LA_TIME_ACCUM_USEC(tv_begin, total_var) \
  do { \
    struct timeval _tv_end; \
    gettimeofday (&_tv_end, NULL); \
    (total_var) += (UINT64) (((INT64) _tv_end.tv_sec - (INT64) (tv_begin).tv_sec) * 1000000LL \
                             + ((INT64) _tv_end.tv_usec - (INT64) (tv_begin).tv_usec)); \
  } while (0)

#endif /* _LOG_APPLIER_DEBUG_HPP_ */
