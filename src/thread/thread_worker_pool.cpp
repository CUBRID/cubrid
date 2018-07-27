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
 * thread_worker_pool.cpp
 */

#include "thread_worker_pool.hpp"

#include "error_manager.h"
#include "perf.hpp"

#include <sstream>

#include <cstring>

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // statistics
  //////////////////////////////////////////////////////////////////////////

  static const cubperf::statset_definition Worker_pool_statdef =
  {
    cubperf::stat_definition (Wpstat_start_thread, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_start_thread", "Timer_start_thread"),
    cubperf::stat_definition (Wpstat_create_context, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_create_context", "Timer_create_context"),
    cubperf::stat_definition (Wpstat_execute_task, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_execute_task", "Timer_execute_task"),
    cubperf::stat_definition (Wpstat_retire_task, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_retire_task", "Timer_retire_task"),
    cubperf::stat_definition (Wpstat_found_in_queue, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_found_task_in_queue", "Timer_found_task_in_queue"),
    cubperf::stat_definition (Wpstat_wakeup_with_task, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_wakeup_with_task", "Timer_wakeup_with_task"),
    cubperf::stat_definition (Wpstat_recycle_context, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_recycle_context", "Timer_recycle_context"),
    cubperf::stat_definition (Wpstat_retire_context, cubperf::stat_definition::COUNTER_AND_TIMER,
    "Counter_retire_context", "Timer_retire_context")
  };

  cubperf::statset &
  wp_worker_statset_create (void)
  {
    return *Worker_pool_statdef.create_statset ();
  }

  void
  wp_worker_statset_destroy (cubperf::statset &stats)
  {
    delete &stats;
  }

  void
  wp_worker_statset_time_and_increment (cubperf::statset &stats, cubperf::stat_id id)
  {
    Worker_pool_statdef.time_and_increment (stats, id);
  }

  void
  wp_worker_statset_accumulate (const cubperf::statset &what, cubperf::stat_value *where)
  {
    Worker_pool_statdef.add_stat_values_with_converted_timers<std::chrono::microseconds> (what, where);
  }

  std::size_t
  wp_worker_statset_get_count (void)
  {
    return Worker_pool_statdef.get_value_count ();
  }

  const char *
  wp_worker_statset_get_name (std::size_t stat_index)
  {
    return Worker_pool_statdef.get_value_name (stat_index);
  }

  //////////////////////////////////////////////////////////////////////////
  // functions
  //////////////////////////////////////////////////////////////////////////

  std::size_t
  system_core_count (void)
  {
    std::size_t count = std::thread::hardware_concurrency ();

    if (count == 0)
      {
	count = 1;
      }

    return count;
  }

  void
  wp_handle_system_error (const char *message, const std::system_error &e)
  {
    er_print_callstack (ARG_FILE_LINE, "%s - throws err = %d: %s\n", message, e.code (), e.what ());
    assert (false);
    throw e;
  }

  void
  wp_er_log_stats (const char *header, cubperf::stat_value *statsp)
  {
    std::stringstream ss;

    ss << "Worker pool statistics: " << header << std::endl;

    for (std::size_t index = 0; index < Worker_pool_statdef.get_value_count (); index++)
      {
	ss << "\t" << Worker_pool_statdef.get_value_name (index) << ": ";
	ss << statsp[index] << std::endl;
      }

    _er_log_debug (ARG_FILE_LINE, ss.str ().c_str ());
  }

} // namespace cubthread
