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

//
// wrapping of cubperf basic functionality to allow one-point forking between actual
//    performance monitoring and 'nop's
//
// implemented specifically for log recovery redo purposes
//

enum PERF_STAT_RECOVERY_ID : cubperf::stat_id
{
  PERF_STAT_ID_FETCH_PAGE,
  PERF_STAT_ID_READ_LOG,
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
    inline log_recovery_redo_perf_stat ()
      : log_recovery_redo_perf_stat ((pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO)
				     == PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO)
    {
    }

    inline log_recovery_redo_perf_stat (bool a_do_record)
      : m_definition
    {
      cubperf::stat_definition (PERF_STAT_ID_FETCH_PAGE, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter fetch_page", "Timer fetch_page (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_READ_LOG, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter read_log", "Timer read_log (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_PREP, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_prep", "Timer redo_or_push_prep (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_do_sync", "Timer redo_or_push_do_sync (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_POP_REUSABLE, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_pop_reusable", "Timer redo_or_push_pop_reusable (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter redo_or_push_do_async", "Timer redo_or_push_do_async (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_COMMIT_ABORT, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter commit_abort", "Timer commit_abort (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_WAIT_FOR_PARALLEL, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter wait_for_parallel", "Timer wait_for_parallel (μs)"),
      cubperf::stat_definition (PERF_STAT_ID_FINALIZE, cubperf::stat_definition::COUNTER_AND_TIMER,
				"Counter finalize", "Timer finalize (μs)"),
    }
    , m_values { nullptr }
    {
      if (a_do_record)
	{
	  m_values = m_definition.create_statset ();
	}
    }

    log_recovery_redo_perf_stat (const log_recovery_redo_perf_stat &) = delete;
    log_recovery_redo_perf_stat (log_recovery_redo_perf_stat &&) = delete;

    inline ~log_recovery_redo_perf_stat ()
    {
      if (m_values != nullptr)
	{
	  delete m_values;
	  m_values = nullptr;
	}
    }

    log_recovery_redo_perf_stat &operator = (const log_recovery_redo_perf_stat &) = delete;
    log_recovery_redo_perf_stat &operator = (log_recovery_redo_perf_stat &&) = delete;

  public:
    inline void time_and_increment (cubperf::stat_id a_stat_id) const
    {
      if (m_values != nullptr)
	{
	  m_definition.time_and_increment (*m_values, a_stat_id);
	}
    }

    inline void log () const
    {
      if (m_values != nullptr)
	{
	  std::vector < cubperf::stat_value > perf_stat_results;
	  perf_stat_results.resize (m_definition.get_value_count (), 0LL);
	  m_definition.get_stat_values_with_converted_timers<std::chrono::microseconds> (
		  *m_values, perf_stat_results.data ());

	  std::stringstream perf_stat_ss;
	  perf_stat_ss << "Log Recovery Redo statistics:" << std::endl;
	  for (std::size_t perf_stat_idx = 0; perf_stat_idx < m_definition.get_value_count (); ++perf_stat_idx)
	    {
	      perf_stat_ss << '\t' << m_definition.get_value_name (perf_stat_idx)
			   << ": " << perf_stat_results[perf_stat_idx] << std::endl;
	    }
	  const std::string perf_stat_str = perf_stat_ss.str ();
	  _er_log_debug (ARG_FILE_LINE, perf_stat_str.c_str ());
	}
    }

  private:
    const cubperf::statset_definition m_definition;
    cubperf::statset *m_values;
};

#endif // _LOG_RECOVERY_REDO_PERF_HPP_
