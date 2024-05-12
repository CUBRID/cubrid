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
 * thread_daemon - interface for daemon threads
 */

// own header
#include "thread_daemon.hpp"

// module headers
#include "thread_task.hpp"

// cubrid headers
#include "perf.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // daemon statistics
  //////////////////////////////////////////////////////////////////////////
  static const cubperf::stat_id STAT_LOOP_EXECUTE_COUNT_AND_TIME = 0;
  static const cubperf::stat_id STAT_LOOP_PAUSE_TIME = 1; // count is same as for execute
  static const cubperf::statset_definition Daemon_statistics =
  {
    cubperf::stat_definition (STAT_LOOP_EXECUTE_COUNT_AND_TIME, cubperf::stat_definition::COUNTER_AND_TIMER,
    "daemon_loop_count", "daemon_execute_time"),
    cubperf::stat_definition (STAT_LOOP_PAUSE_TIME, cubperf::stat_definition::TIMER,
    "daemon_pause_time")
  };

  //////////////////////////////////////////////////////////////////////////
  // daemon implementation
  //////////////////////////////////////////////////////////////////////////

  daemon::daemon (const looper &loop_pattern_arg, task_without_context *exec_arg, const char *name)
    : m_waiter ()
    , m_looper (loop_pattern_arg)
    , m_func_on_stop ()
    , m_thread ()
    , m_name (name)
    , m_stats (daemon::create_statset ())
  {
    m_thread = std::thread (daemon::loop_without_context, this, exec_arg, m_name.c_str ());
  }

  daemon::~daemon ()
  {
    // thread must be stopped
    stop_execution ();

    delete &m_stats;
  }

  void
  daemon::wakeup (void)
  {
    m_waiter.wakeup ();
  }

  void
  daemon::stop_execution (void)
  {
    // note: this must not be called concurrently

    if (m_looper.stop ())
      {
	// already stopped
	return;
      }

    if (m_func_on_stop)
      {
	// to interrupt execution context
	m_func_on_stop ();
      }

    // make sure thread will wakeup
    wakeup ();

    // then wait for thread to finish
    m_thread.join ();
  }

  void daemon::pause (void)
  {
    m_looper.put_to_sleep (m_waiter);
  }

  bool
  daemon::was_woken_up (void)
  {
    return m_looper.was_woken_up ();
  }

  void
  daemon::reset_looper (void)
  {
    m_looper.reset ();
  }

  void
  daemon::get_stats (cubperf::stat_value *stats_out)
  {
    std::size_t i = 0;

    // get daemon stats
    Daemon_statistics.get_stat_values_with_converted_timers<std::chrono::microseconds> (m_stats, stats_out);
    i += Daemon_statistics.get_value_count ();

    // get looper stats
    m_looper.get_stats (&stats_out[i]);
    i += looper::get_stats_value_count ();

    // get waiter stats
    m_waiter.get_stats (&stats_out[i]);
    // i += waiter::STAT_COUNT;
  }

  bool
  daemon::is_running (void)
  {
    return m_waiter.is_running ();
  }

  std::size_t
  daemon::get_stats_value_count (void)
  {
    return Daemon_statistics.get_value_count () + looper::get_stats_value_count() + waiter::get_stats_value_count ();
  }

  const char *
  daemon::get_stat_name (std::size_t stat_index)
  {
    assert (stat_index < get_stats_value_count ());

    // is from daemon?
    if (stat_index < Daemon_statistics.get_value_count ())
      {
	return Daemon_statistics.get_value_name (stat_index);
      }
    else
      {
	stat_index -= Daemon_statistics.get_value_count ();
      }

    // is from looper?
    if (stat_index < looper::get_stats_value_count ())
      {
	return looper::get_stat_name (stat_index);
      }
    else
      {
	stat_index -= looper::get_stats_value_count ();
      }

    // must be from waiter
    assert (stat_index < waiter::get_stats_value_count ());
    return waiter::get_stat_name (stat_index);
  }

  cubperf::statset &
  daemon::create_statset (void)
  {
    return *Daemon_statistics.create_statset ();
  }

  void
  daemon::register_stat_start (void)
  {
    cubperf::reset_timept (m_stats.m_timept);
  }

  void
  daemon::register_stat_pause (void)
  {
    Daemon_statistics.time (m_stats, STAT_LOOP_PAUSE_TIME);
  }

  void
  daemon::register_stat_execute (void)
  {
    Daemon_statistics.time_and_increment (m_stats, STAT_LOOP_EXECUTE_COUNT_AND_TIME);
  }

  void
  daemon::loop_without_context (daemon *daemon_arg, task_without_context *exec_arg, const char *name)
  {
    (void) name;  // suppress unused parameter warning
    // its purpose is to help visualize daemon thread stacks

    daemon_arg->register_stat_start ();

    while (!daemon_arg->m_looper.is_stopped ())
      {
	// execute task
	exec_arg->execute ();
	daemon_arg->register_stat_execute ();

	// take a break
	daemon_arg->pause ();
	daemon_arg->register_stat_pause ();
      }

    // retire task
    exec_arg->retire ();
  }

} // namespace cubthread
