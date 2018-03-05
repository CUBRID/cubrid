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
    : m_periods_count (0)
    , m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_was_woken_up (false)
    , m_start_execution_time ()
  {
    // infinite waits
    m_setup_period = std::bind (&looper::setup_infinite_wait, *this, std::placeholders::_1, std::placeholders::_2);
  }

  looper::looper (const looper &other)
    : m_periods_count (other.m_periods_count)
    , m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_was_woken_up (false)
    , m_setup_period (other.m_setup_period)
    , m_start_execution_time (other.m_start_execution_time)
  {
    std::copy (std::begin (other.m_periods), std::end (other.m_periods), std::begin (m_periods));
  }

  looper::looper (const period_function &setup_period_function)
    : m_periods_count (0)
    , m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_was_woken_up (false)
    , m_setup_period (setup_period_function)
    , m_start_execution_time ()
  {
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

    bool is_timed_wait = true;
    delta_time duration = delta_time (0);

    m_setup_period (is_timed_wait, duration);
    if (is_timed_wait)
      {
	delta_time delta = get_wait_for (duration);
	m_was_woken_up = waiter_arg.wait_for (delta);
      }
    else
      {
	waiter_arg.wait_inf ();
	m_was_woken_up = true;
      }

    // register start of the task execution time
    m_start_execution_time = std::chrono::system_clock::now ();
  }

  void
  looper::reset (void)
  {
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

  delta_time
  looper::get_wait_for (delta_time &period)
  {
    delta_time execution_time = delta_time (0);
    delta_time wait_for = delta_time (0);

    if (m_start_execution_time != std::chrono::system_clock::time_point ())
      {
	execution_time = std::chrono::system_clock::now () - m_start_execution_time;
      }

    // compute task execution time
    if (period > execution_time)
      {
	wait_for = period - execution_time;
      }

    assert (wait_for <= period);

    return wait_for;
  }

  void
  looper::setup_infinite_wait (bool &is_timed_wait, delta_time &period)
  {
    assert (m_period_index == 0);

    is_timed_wait = false;
  }

  void
  looper::setup_fixed_waits (bool &is_timed_wait, delta_time &period)
  {
    assert (m_period_index == 0);

    is_timed_wait = true;
    period = m_periods[m_period_index];
  }

  void
  looper::setup_increasing_waits (bool &is_timed_wait, delta_time &period)
  {
    if (m_period_index < m_periods_count)
      {
	is_timed_wait = true;
	period = m_periods[m_period_index++];
      }
    else
      {
	is_timed_wait = false;
	reset ();
      }
  }

} // namespace cubthread
