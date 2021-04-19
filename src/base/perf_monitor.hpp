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
 * perf_monitor.hpp - c++ helper classes over c code
 */

#ifndef PERF_MONITOR_HPP
#define PERF_MONITOR_HPP

#include "perf_monitor.h"

/* RAII perf stat monitoring utility for counter timer
 *
 * NOTE: the global 'perfmon_is_perf_tracking' is only checked in the ctor
 * and used for the rest of the lifetime as is
 */
class perfmon_tracker_counter_timer
{
  public:
    perfmon_tracker_counter_timer (PERF_STAT_ID a_stat_id);

    ~perfmon_tracker_counter_timer ();

    /* mark result and start over
     * useful in loops
     */
    void rewind ();

  private:
    void do_start ();
    void do_stop ();

  private:
    const PERF_STAT_ID m_stat_id;
    const bool m_is_perf_tracking;

    TSC_TICKS m_start_tick;
};

#endif // PERF_MONITOR_HPP
