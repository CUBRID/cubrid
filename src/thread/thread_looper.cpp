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

namespace cubthread
{

  looper::looper ()
    : m_wait_pattern (wait_pattern::INFINITE_WAITS)
    , m_periods_count (0)
    , m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_was_woken_up (false)
    , m_period_function ()
    , m_start_execution_time ()
  {
    // infinite waits
  }

  looper::looper (const looper &other)
    : m_wait_pattern (other.m_wait_pattern)
    , m_periods_count (other.m_periods_count)
    , m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_was_woken_up (false)
    , m_period_function (other.m_period_function)
    , m_start_execution_time (other.m_start_execution_time)
  {
    for (std::size_t i = 0; i < m_periods_count; i++)
      {
	this->m_periods[i] = other.m_periods[i];
      }
  }

  looper::looper (const std::function<int ()> &period_function)
    : m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_was_woken_up (false)
    , m_period_function (period_function)
    , m_start_execution_time ()
  {
    setup_period ();
  }

  void
  looper::put_to_sleep (waiter &waiter_arg)
  {
    if (is_stopped ())
      {
	// stopped; don't put to sleep
	return;
      }

    // refresh period based on period function
    setup_period ();

    if (m_period_index >= m_periods_count)
      {
	assert (m_period_index == m_periods_count);
	waiter_arg.wait_inf ();
	m_was_woken_up = true;
      }
    else
      {
	delta_time delta = get_wait_for ();
	m_was_woken_up = waiter_arg.wait_for (delta);
      }
    if (m_wait_pattern == wait_pattern::FIXED_PERIODS || m_wait_pattern == wait_pattern::INFINITE_WAITS)
      {
	assert (m_period_index == 0);
	return;
      }
    if (!m_was_woken_up)
      {
	/* increment */
	++m_period_index;
      }
    else
      {
	/* reset */
	m_period_index = 0;
      }

    // register start of the task execution time
    m_start_execution_time = std::chrono::system_clock::now ();
  }

  void
  looper::reset (void)
  {
    assert (m_wait_pattern == wait_pattern::INCREASING_PERIODS);
    m_period_index = 0;
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

  looper::delta_time
  looper::get_wait_for (void)
  {
    delta_time execution_time;

    if (m_start_execution_time == std::chrono::system_clock::time_point ())
      {
	// assume execution_time is 0 (zero) for first run
	// since m_start_execution_time is not yet registered
	execution_time = delta_time (0);
      }
    else
      {
	execution_time = std::chrono::system_clock::now () - m_start_execution_time;
      }

    delta_time wait_for;
    delta_time period = m_periods[m_period_index];

    // compute task execution time
    if (execution_time < period)
      {
	wait_for = period - execution_time;
      }
    else
      {
	// if execution_time is greater than the period then immediately execute task without sleeping
	wait_for = delta_time (0);
      }

    assert (wait_for <= period);

    return wait_for;
  }

  void
  looper::setup_period (void)
  {
    if (!m_period_function || m_wait_pattern == wait_pattern::INCREASING_PERIODS)
      {
	// do nothing for increasing periods waiter or whether m_period_function has a valid callable target
	return;
      }

    assert (m_period_index == 0);

    int new_period_msec = m_period_function ();

    // refresh period based on new interval value
    if (new_period_msec < 0)
      {
	// set members to reflect infinite wait
	m_wait_pattern = wait_pattern::INFINITE_WAITS;
	m_periods_count = 0;
	m_periods[m_period_index] = delta_time (0);
      }
    else
      {
	// set members to reflect fixed period
	m_wait_pattern = wait_pattern::FIXED_PERIODS;
	m_periods_count = 1;
	m_periods[m_period_index] = std::chrono::milliseconds (new_period_msec);
      }
  }

} // namespace cubthread
