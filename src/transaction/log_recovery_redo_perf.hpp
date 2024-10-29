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

#ifndef _LOG_RECOVERY_REDO_PERF_HPP_
#define _LOG_RECOVERY_REDO_PERF_HPP_

#include "error_manager.h"
#include "perf.hpp"
#include "perf_monitor.h"

#include <sstream>

namespace cublog
{
  /*
   * wrapping of cubperf basic functionality to allow one-point forking between actual
   *        performance monitoring and 'nop's
   * implemented specifically for log recovery redo purposes
   */

  /* used to evaluate log recovery redo performance on the main thread
   */
  enum : cubperf::stat_id
  {
    PERF_STAT_ID_FETCH_PAGE,
    PERF_STAT_ID_READ_LOG,
    PERF_STAT_ID_REDO_OR_PUSH_PREP,
    PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC,
    PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE_DIRECT,
    PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE_WAIT,
    PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC,
    PERF_STAT_ID_COMMIT_ABORT,
    PERF_STAT_ID_WAIT_FOR_PARALLEL,
    PERF_STAT_ID_FINALIZE,
  };

  static const cubperf::statset_definition::init_list_t perf_stats_main_definition_init_list
  {
    cubperf::stat_definition (PERF_STAT_ID_FETCH_PAGE, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter fetch_page", "Timer fetch_page (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_READ_LOG, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter read_log", "Timer read_log (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_PREP, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter redo_or_push_prep", "Timer redo_or_push_prep (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter redo_or_push_do_sync", "Timer redo_or_push_do_sync (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE_DIRECT, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter redo_or_push_pop_reusable_direct", "Timer redo_or_push_pop_reusable_direct (μs)"),
    cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE_WAIT, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter redo_or_push_pop_reusable_wait", "Timer redo_or_push_pop_reusable_wait (μs)"),
    cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter redo_or_push_do_async", "Timer redo_or_push_do_async (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_COMMIT_ABORT, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter commit_abort", "Timer commit_abort (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_WAIT_FOR_PARALLEL, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter wait_for_parallel", "Timer wait_for_parallel (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_FINALIZE, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter finalize", "Timer finalize (ms)"),
  };

  /* used to evaluate log recovery redo performance on the async (worker) threads
   */
  enum : cubperf::stat_id
  {
    PERF_STAT_ID_PARALLEL_POP,
    PERF_STAT_ID_PARALLEL_SLEEP,
    PERF_STAT_ID_PARALLEL_EXECUTE,
    PERF_STAT_ID_PARALLEL_RETIRE,
  };

  static const cubperf::statset_definition::init_list_t perf_stats_async_definition_init_list
  {
    cubperf::stat_definition (PERF_STAT_ID_PARALLEL_POP, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter pop", "Timer pop (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_PARALLEL_SLEEP, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter sleep", "Timer sleep (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_PARALLEL_EXECUTE, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter execute", "Timer execute (ms)"),
    cubperf::stat_definition (PERF_STAT_ID_PARALLEL_RETIRE, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "Counter retire", "Timer retire (ms)"),
  };

  /* collect and log performance statistics based on a supplied definition
   *
   * Usage:
   *  - define a definition initialization list
   *  - instantiate a stats set definition with the definition initialization list
   *  - instantiate this class with a definition
   */
  class perf_stats
  {
    public:
      class do_not_record_t {};

    public:
      inline perf_stats (do_not_record_t);
      inline perf_stats (bool a_do_record, const cubperf::statset_definition &a_definition);

      perf_stats (const perf_stats &) = delete;
      perf_stats (perf_stats &&) = delete;

      inline ~perf_stats ();

      perf_stats &operator = (const perf_stats &) = delete;
      perf_stats &operator = (perf_stats &&) = delete;

      inline void time_and_increment (cubperf::stat_id a_stat_id) const;
      inline void log (const char *a_title) const;
      inline void accumulate (cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const;

    private:
      const cubperf::statset_definition &m_definition;
      cubperf::statset *m_stats_set;
  };

  inline void log_perf_stats_values_with_definition (const char *a_title, const cubperf::statset_definition &a_definition,
      const cubperf::stat_value *a_stats_values, std::size_t a_stats_values_size);

  inline bool perf_stats_is_active_for_main ();
  inline bool perf_stats_is_active_for_async ();

  /*********************************************************************
   * definitions
   *********************************************************************/

  perf_stats::perf_stats (do_not_record_t)
    : perf_stats { false, cubperf::statset_definition {
      cubperf::statset_definition::init_list_t {} } }
  {
  }

  perf_stats::perf_stats (bool a_do_record,
			  const cubperf::statset_definition &a_definition)
    : m_definition { a_definition }
    , m_stats_set { nullptr }
  {
    if (a_do_record)
      {
	m_stats_set = m_definition.create_statset ();
      }
  }

  perf_stats::~perf_stats ()
  {
    if (m_stats_set != nullptr)
      {
	delete m_stats_set;
	m_stats_set = nullptr;
      }
  }

  void perf_stats::time_and_increment (cubperf::stat_id a_stat_id) const
  {
    if (m_stats_set != nullptr)
      {
	m_definition.time_and_increment (*m_stats_set, a_stat_id);
      }
  }

  void perf_stats::log (const char *a_title) const
  {
    if (m_stats_set != nullptr)
      {
	std::vector<cubperf::stat_value> perf_stat_values;
	perf_stat_values.resize (m_definition.get_value_count (), 0LL);
	m_definition.get_stat_values_with_converted_timers<std::chrono::milliseconds> (
		*m_stats_set, perf_stat_values.data ());

	log_perf_stats_values_with_definition (a_title, m_definition,
					       perf_stat_values.data (), perf_stat_values.size ());
      }
  }

  void perf_stats::accumulate (cubperf::stat_value *a_output_stats,
			       std::size_t a_output_stats_size) const
  {
    if (m_stats_set != nullptr)
      {
	// minimal checking to ensure there is enough space for the underlying logic to write data
	if (a_output_stats != nullptr && a_output_stats_size == m_definition.get_value_count ())
	  {
	    m_definition.add_stat_values_with_converted_timers<std::chrono::milliseconds> (*m_stats_set, a_output_stats);
	  }
      }
  }

  inline void log_perf_stats_values_with_definition (const char *a_title,
      const cubperf::statset_definition &a_definition,
      const cubperf::stat_value *a_stats_values, std::size_t a_stats_values_size)
  {
    assert (a_title != nullptr);
    assert (a_stats_values != nullptr);
    assert (a_definition.get_value_count () == a_stats_values_size);

    std::stringstream ss;
    ss << a_title << ":" << std::endl;
    for (std::size_t perf_stat_idx = 0; perf_stat_idx < a_definition.get_value_count (); ++perf_stat_idx)
      {
	ss << '\t' << a_definition.get_value_name (perf_stat_idx)
	   << ": " << a_stats_values[perf_stat_idx] << std::endl;
      }
    const std::string ss_str = ss.str ();
    _er_log_debug (ARG_FILE_LINE, ss_str.c_str ());
  }

  inline bool perf_stats_is_active_for_main ()
  {
    return (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_MAIN) ==
	   PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_MAIN;
  }

  inline bool perf_stats_is_active_for_async ()
  {
    return (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_ASYNC) ==
	   PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_ASYNC;
  }
}

#endif // _LOG_RECOVERY_REDO_PERF_HPP_
