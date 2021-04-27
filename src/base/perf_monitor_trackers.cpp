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

#include "perf_monitor_trackers.hpp"

#include "thread_compat.hpp"
#include "thread_manager.hpp"

perfmon_manual_tracker_counter_timer::perfmon_manual_tracker_counter_timer (PERF_STAT_ID a_stat_id)
  : m_stat_id (a_stat_id)
  , m_is_perf_tracking (false)
{
  assert (pstat_Metadata[m_stat_id].valtype == PSTAT_COUNTER_TIMER_VALUE);
}

void perfmon_manual_tracker_counter_timer::start ()
{
  // only init flag at every start such as to record accurate values
  m_is_perf_tracking = perfmon_is_perf_tracking ();
  if (m_is_perf_tracking)
    {
      tsc_getticks (&m_start_tick);
    }
}

void perfmon_manual_tracker_counter_timer::track ()
{
  if (m_is_perf_tracking)
    {
      cubthread::entry *const thread_entry_p = &cubthread::get_entry ();

      TSC_TICKS end_tick {0};
      tsc_getticks (&end_tick);

      const auto elapsed_time_usec = tsc_elapsed_utime (end_tick, m_start_tick);

      perfmon_time_stat (thread_entry_p, m_stat_id, elapsed_time_usec);

      m_is_perf_tracking = false;
    }
}

void perfmon_manual_tracker_counter_timer::track_and_start ()
{
  track ();
  start ();
}

perfmon_raii_tracker_counter_timer::perfmon_raii_tracker_counter_timer (PERF_STAT_ID a_stat_id)
  : perfmon_manual_tracker_counter_timer (a_stat_id)
{
  start ();
}

perfmon_raii_tracker_counter_timer::~perfmon_raii_tracker_counter_timer ()
{
  track ();
}
