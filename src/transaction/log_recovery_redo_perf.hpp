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

  /* parameterized class; not to be used directly
   */
  class log_recovery_redo_perf_stat_param
  {
    public:
      inline log_recovery_redo_perf_stat_param (const char *a_title, bool a_do_record,
	  const cubperf::statset_definition::stat_definition_vec_t &a_defs);

      log_recovery_redo_perf_stat_param (const log_recovery_redo_perf_stat_param &) = delete;
      log_recovery_redo_perf_stat_param (log_recovery_redo_perf_stat_param &&) = delete;

      inline ~log_recovery_redo_perf_stat_param ();

      log_recovery_redo_perf_stat_param &operator = (const log_recovery_redo_perf_stat_param &) = delete;
      log_recovery_redo_perf_stat_param &operator = (log_recovery_redo_perf_stat_param &&) = delete;

    public:
      inline void time_and_increment (cubperf::stat_id a_stat_id) const;
      inline void log () const;
      inline void accumulate (cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const;

    private:
      const char *const m_title;
      const cubperf::statset_definition m_definition;
      cubperf::statset *m_values;
  };

  /* used to evaluate log recovery redo performance on the main thread
   */
  enum : cubperf::stat_id
  {
    PERF_STAT_ID_FETCH_PAGE,
    PERF_STAT_ID_READ_LOG,
    PERF_STAT_ID_REDO_OR_PUSH,
    PERF_STAT_ID_REDO_OR_PUSH_PREP,
    PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC,
    PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE,
    PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC,
    PERF_STAT_ID_COMMIT_ABORT,
    PERF_STAT_ID_WAIT_FOR_PARALLEL,
    PERF_STAT_ID_FINALIZE,
  };

  class log_recovery_redo_perf_stat
  {
    public:
      inline log_recovery_redo_perf_stat ();
      inline log_recovery_redo_perf_stat (bool a_do_record);

      log_recovery_redo_perf_stat (const log_recovery_redo_perf_stat &) = delete;
      log_recovery_redo_perf_stat (log_recovery_redo_perf_stat &&) = delete;

      log_recovery_redo_perf_stat &operator = (const log_recovery_redo_perf_stat &) = delete;
      log_recovery_redo_perf_stat &operator = (log_recovery_redo_perf_stat &&) = delete;

    public:
      inline void time_and_increment (cubperf::stat_id a_stat_id) const;
      inline void log () const;

    private:
      log_recovery_redo_perf_stat_param m_;
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

  class log_recovery_redo_parallel_perf_stat
  {
    public:
      static constexpr cubperf::statset_definition::stat_definition_init_list_t m_stats_definition_init_list
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

    public:
      inline log_recovery_redo_parallel_perf_stat ();

      log_recovery_redo_parallel_perf_stat (const log_recovery_redo_parallel_perf_stat &) = delete;
      log_recovery_redo_parallel_perf_stat (log_recovery_redo_parallel_perf_stat &&) = delete;

      log_recovery_redo_parallel_perf_stat &operator = (const log_recovery_redo_parallel_perf_stat &) = delete;
      log_recovery_redo_parallel_perf_stat &operator = (log_recovery_redo_parallel_perf_stat &&) = delete;

    public:
      inline void time_and_increment (cubperf::stat_id a_stat_id) const;
      inline void log () const;
      inline void accumulate (cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const;

    private:
      log_recovery_redo_perf_stat_param m_;
  };

  /*********************************************************************
   * definitions
   *********************************************************************/

  log_recovery_redo_perf_stat_param::log_recovery_redo_perf_stat_param (const char *a_title,
      bool a_do_record, const cubperf::statset_definition::stat_definition_vec_t &a_defs)
    : m_title { a_title }
    , m_definition { a_defs }
    , m_values { nullptr }
  {
    assert (a_title != nullptr);
    if (a_do_record)
      {
	m_values = m_definition.create_statset ();
      }
  }

  log_recovery_redo_perf_stat_param::~log_recovery_redo_perf_stat_param ()
  {
    if (m_values != nullptr)
      {
	delete m_values;
	m_values = nullptr;
      }
  }

  void log_recovery_redo_perf_stat_param::time_and_increment (cubperf::stat_id a_stat_id) const
  {
    if (m_values != nullptr)
      {
	m_definition.time_and_increment (*m_values, a_stat_id);
      }
  }

  inline void log_perf_stats_values_with_definition (const cubperf::statset_definition &a_definition,
      const cubperf::stat_value *a_stats_values,
      std::size_t a_stats_values_size)
  {
    if (a_definition.get_value_count () == a_stats_values_size)
      {
	std::stringstream ss;
	for (std::size_t perf_stat_idx = 0; perf_stat_idx < a_definition.get_value_count (); ++perf_stat_idx)
	  {
	    ss << '\t' << a_definition.get_value_name (perf_stat_idx)
	       << ": " << a_stats_values[perf_stat_idx] << std::endl;
	  }
	const std::string ss_str = ss.str ();
	_er_log_debug (ARG_FILE_LINE, ss_str.c_str ());
      }
  }

  void log_recovery_redo_perf_stat_param::log () const
  {
    if (m_values != nullptr)
      {
	std::vector < cubperf::stat_value > perf_stat_results;
	perf_stat_results.resize (m_definition.get_value_count (), 0LL);
	m_definition.get_stat_values_with_converted_timers<std::chrono::milliseconds> (
		*m_values, perf_stat_results.data ());

	std::stringstream perf_stat_ss;
	perf_stat_ss << m_title << ":" << std::endl;
	for (std::size_t perf_stat_idx = 0; perf_stat_idx < m_definition.get_value_count (); ++perf_stat_idx)
	  {
	    perf_stat_ss << '\t' << m_definition.get_value_name (perf_stat_idx)
			 << ": " << perf_stat_results[perf_stat_idx] << std::endl;
	  }
	const std::string perf_stat_str = perf_stat_ss.str ();
	_er_log_debug (ARG_FILE_LINE, perf_stat_str.c_str ());
      }
  }

  void log_recovery_redo_perf_stat_param::accumulate (
	  cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const
  {
    if (a_output_stats != nullptr && a_output_stats_size == m_definition.get_value_count ())
      {
	m_definition.add_stat_values_with_converted_timers<std::chrono::milliseconds> (*m_values, a_output_stats);
      }
  }

  log_recovery_redo_perf_stat::log_recovery_redo_perf_stat ()
    : log_recovery_redo_perf_stat
  {
    (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_MAIN)
    == PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_MAIN
  }
  {}

  log_recovery_redo_perf_stat::log_recovery_redo_perf_stat (bool a_do_record)
    : m_
  {
    "Log recovery redo main thread perf stats",
    a_do_record,
    {
      cubperf::stat_definition (PERF_STAT_ID_FETCH_PAGE, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter fetch_page", "Timer fetch_page (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_READ_LOG, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter read_log", "Timer read_log (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push", "Timer redo_or_push (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_PREP, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_prep", "Timer redo_or_push_prep (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_do_sync", "Timer redo_or_push_do_sync (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_pop_reusable", "Timer redo_or_push_pop_reusable (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_do_async", "Timer redo_or_push_do_async (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_COMMIT_ABORT, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter commit_abort", "Timer commit_abort (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_WAIT_FOR_PARALLEL, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter wait_for_parallel", "Timer wait_for_parallel (ms)"),
      cubperf::stat_definition (PERF_STAT_ID_FINALIZE, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter finalize", "Timer finalize (ms)"),
    },
  }
  {}

  void log_recovery_redo_perf_stat::time_and_increment (cubperf::stat_id a_stat_id) const
  {
    m_.time_and_increment (a_stat_id);
  }

  void log_recovery_redo_perf_stat::log () const
  {
    m_.log ();
  }

  log_recovery_redo_parallel_perf_stat::log_recovery_redo_parallel_perf_stat ()
    : m_
  {
    "Log recovery redo worker thread perf stats",
    ((pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_ASYNC)
     == PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_ASYNC),
    m_stats_definition_init_list,
  }
  {}

  void log_recovery_redo_parallel_perf_stat::time_and_increment (cubperf::stat_id a_stat_id) const
  {
    m_.time_and_increment (a_stat_id);
  }

  void log_recovery_redo_parallel_perf_stat::log () const
  {
    m_.log ();
  }

  void log_recovery_redo_parallel_perf_stat::accumulate (
	  cubperf::stat_value *a_output_stats, std::size_t a_output_stats_size) const
  {
    m_.accumulate (a_output_stats, a_output_stats_size);
  }
}

#endif // _LOG_RECOVERY_REDO_PERF_HPP_
