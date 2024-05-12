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
 * thread_looper.cpp
 */

#include "thread_looper.hpp"
#include "thread_waiter.hpp"

#include "perf.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // statistics
  //////////////////////////////////////////////////////////////////////////

  static const cubperf::stat_id STAT_LOOPER_SLEEP_COUNT_AND_TIME = 0;
  static cubperf::stat_id STAT_LOOPER_RESET_COUNT = 1;
  static const cubperf::statset_definition Looper_statistics =
  {
    cubperf::stat_definition (STAT_LOOPER_SLEEP_COUNT_AND_TIME, cubperf::stat_definition::COUNTER_AND_TIMER,
    "looper_sleep_count", "looper_sleep_time"),
    cubperf::stat_definition (STAT_LOOPER_RESET_COUNT, cubperf::stat_definition::COUNTER, "looper_reset_count")
  };

  //////////////////////////////////////////////////////////////////////////
  // looper implementation
  //////////////////////////////////////////////////////////////////////////

  looper::looper ()
    : m_periods_count (0)
    , m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_was_woken_up (false)
    , m_setup_period ()
    , m_start_execution_time ()
    , m_stats (*Looper_statistics.create_statset ())
    , m_wait_type (INF_WAITS)
  {
    // infinite waits
    m_setup_period = std::bind (&looper::setup_infinite_wait, std::ref (*this), std::placeholders::_1,
				std::placeholders::_2);
  }

  looper::looper (const looper &other)
    : looper ()
  {
    m_periods_count = other.m_periods_count;
    m_start_execution_time = other.m_start_execution_time;
    std::copy (std::begin (other.m_periods), std::end (other.m_periods), std::begin (m_periods));

    // we need to use same target function, however for default setup function first argument must be this and not
    // other
    m_wait_type = other.m_wait_type;
    switch (other.m_wait_type)
      {
      case INF_WAITS:
	// already bound to looper::setup_infinite_wait
	break;
      case FIXED_WAITS:
	m_setup_period = std::bind (&looper::setup_fixed_waits, std::ref (*this), std::placeholders::_1,
				    std::placeholders::_2);
	break;
      case INCREASING_WAITS:
	m_setup_period = std::bind (&looper::setup_increasing_waits, std::ref (*this), std::placeholders::_1,
				    std::placeholders::_2);
	break;
      case CUSTOM_WAITS:
	m_setup_period = other.m_setup_period;    // just copy function
	break;
      default:
	assert (false);
	break;
      }
  }

  looper::looper (const period_function &setup_period_function)
    : looper ()
  {
    m_setup_period = setup_period_function;
    m_wait_type = CUSTOM_WAITS;
  }

  looper::looper (const delta_time &fixed_period)
    : looper ()
  {
    m_periods[0] = fixed_period;
    m_setup_period = std::bind (&looper::setup_fixed_waits, std::ref (*this), std::placeholders::_1,
				std::placeholders::_2);
    m_wait_type = FIXED_WAITS;
  }

  looper::~looper (void)
  {
    delete &m_stats;
  }

  void
  looper::put_to_sleep (waiter &waiter_arg)
  {
    if (is_stopped ())
      {
	// stopped; don't put to sleep
	return;
      }

    assert (m_setup_period);

    cubperf::reset_timept (m_stats.m_timept);

    bool is_timed_wait = true;
    delta_time period = delta_time (0);

    m_setup_period (is_timed_wait, period);

    if (is_timed_wait)
      {
	delta_time wait_time = delta_time (0);
	delta_time execution_time = delta_time (0);

	if (m_start_execution_time != std::chrono::system_clock::time_point ())
	  {
	    execution_time = std::chrono::system_clock::now () - m_start_execution_time;
	  }

	// compute task execution time
	if (period > execution_time)
	  {
	    wait_time = period - execution_time;
	  }

	m_was_woken_up = waiter_arg.wait_for (wait_time);
      }
    else
      {
	waiter_arg.wait_inf ();
	m_was_woken_up = true;
      }

    // register start of the task execution time
    m_start_execution_time = std::chrono::system_clock::now ();
    Looper_statistics.time_and_increment (m_stats, STAT_LOOPER_SLEEP_COUNT_AND_TIME);
  }

  void
  looper::reset (void)
  {
    m_period_index = 0;
    Looper_statistics.increment (m_stats, STAT_LOOPER_RESET_COUNT);
  }

  bool
  looper::stop (void)
  {
    return m_stop.exchange (true);
  }

  bool
  looper::is_stopped (void) const
  {
    return m_stop;
  }

  bool
  looper::was_woken_up (void) const
  {
    return m_was_woken_up;
  }

  void
  looper::setup_fixed_waits (bool &is_timed_wait, delta_time &period)
  {
    assert (m_period_index == 0);

    is_timed_wait = true;
    period = m_periods[m_period_index];
  }

  void
  looper::setup_infinite_wait (bool &is_timed_wait, delta_time &period)
  {
    assert (m_period_index == 0);

    is_timed_wait = false;
  }

  void
  looper::setup_increasing_waits (bool &is_timed_wait, delta_time &period)
  {
    if (m_was_woken_up)
      {
	reset ();
      }

    if (m_period_index < m_periods_count)
      {
	is_timed_wait = true;
	period = m_periods[m_period_index++];
      }
    else
      {
	is_timed_wait = false;
      }
  }

  void
  looper::get_stats (cubperf::stat_value *stats_out)
  {
    Looper_statistics.get_stat_values_with_converted_timers<std::chrono::microseconds> (m_stats, stats_out);
  }

  std::size_t
  looper::get_stats_value_count (void)
  {
    return Looper_statistics.get_value_count ();
  }

  const char *
  looper::get_stat_name (std::size_t stat_index)
  {
    return Looper_statistics.get_value_name (stat_index);
  }

} // namespace cubthread
