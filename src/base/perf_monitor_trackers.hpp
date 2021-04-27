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
 * perf_monitor_trackers.hpp  - helper c++ classes over c code that ease tracking of resources
 */

#ifndef PERF_MONITOR_HPP
#define PERF_MONITOR_HPP

#include "perf_monitor.h"

/* manual perf stat monitoring utility for counter timer values
 *
 * NOTE: the global 'perfmon_is_perf_tracking' is only checked in the ctor
 * and used for the rest of the lifetime as is
 */
class perfmon_manual_tracker_counter_timer
{
  public:
    perfmon_manual_tracker_counter_timer (PERF_STAT_ID a_stat_id);

    /* re-init without tracking time
     */
    void start ();

    /* manually track time without re-init
     *
     * NOTE: internally will:
     *  - record time since previous start
     *  - also add whatever internally kept time has been recorded so far
     */
    void track ();

    /* track time and re-init
     * useful in loops
     */
    void track_and_start ();

  private:
    const PERF_STAT_ID m_stat_id;
    /* must be re-inited at every 'start' to model the dynamic nature of perfmon tracking
     */
    bool m_is_perf_tracking;

    TSC_TICKS m_start_tick;
};


/* RAII perf stat monitoring utility for counter timer values
 *
 * NOTE: the global 'perfmon_is_perf_tracking' is only checked in the ctor
 * and used for the rest of the lifetime as is
 */
class perfmon_raii_tracker_counter_timer final : private perfmon_manual_tracker_counter_timer
{
  public:
    perfmon_raii_tracker_counter_timer (PERF_STAT_ID a_stat_id);
    ~perfmon_raii_tracker_counter_timer ();
};

#endif // PERF_MONITOR_HPP
