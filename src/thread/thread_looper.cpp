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
 * thread_looper.cpp
 */

#include "thread_looper.hpp"
#include "thread_waiter.hpp"

#include "perf.hpp"

// todo: fix time conversion
void foo (void)
{
  cubperf::stat_id dummy;
  cubperf::statset_definition fact = { cubperf::stat_definition (dummy, cubperf::stat_definition::type::COUNTER, "stat") };
  cubperf::stat_value *vals = NULL;
}

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // statistics
  //////////////////////////////////////////////////////////////////////////

  static cubperf::stat_id STAT_LOOPER_SLEEP_COUNT_AND_TIME = 0;
  static cubperf::stat_id STAT_LOOPER_RESET_COUNT = 0;
  static const cubperf::statset_definition looper_statdef =
  {
    cubperf::stat_definition (STAT_LOOPER_SLEEP_COUNT_AND_TIME, cubperf::stat_definition::COUNTER_AND_TIMER,
    "looper_sleep_count", "looper_sleep_time"),
    cubperf::stat_definition (STAT_LOOPER_RESET_COUNT, cubperf::stat_definition::COUNTER, "looper_reset_count"),
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
    , m_stats (*looper_statdef.create_statset ())
  {
    // infinite waits
    m_setup_period = std::bind (&looper::setup_infinite_wait, *this, std::placeholders::_1, std::placeholders::_2);
  }

  looper::looper (const looper &other)
    : looper ()
  {
    m_periods_count = other.m_periods_count;
    m_setup_period = other.m_setup_period;
    m_start_execution_time = other.m_start_execution_time;
    std::copy (std::begin (other.m_periods), std::end (other.m_periods), std::begin (m_periods));
  }

  looper::looper (const period_function &setup_period_function)
    : looper ()
  {
    m_setup_period = setup_period_function;
  }

  looper::looper (const delta_time &fixed_period)
    : looper ()
  {
    m_periods[0] = fixed_period;
    m_setup_period = std::bind (&looper::setup_fixed_waits, *this, std::placeholders::_1, std::placeholders::_2);
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

    m_stats.reset_timept ();

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
    looper_statdef.time_and_increment (m_stats, STAT_LOOPER_SLEEP_COUNT_AND_TIME);
  }

  void
  looper::reset (void)
  {
    m_period_index = 0;
    looper_statdef.increment (m_stats, STAT_LOOPER_RESET_COUNT);
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
    looper_statdef.get_stat_values (m_stats, stats_out);
  }

} // namespace cubthread
