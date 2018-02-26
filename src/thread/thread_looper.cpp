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

#include <cassert>

namespace cubthread
{

  looper::looper ()
    : m_wait_pattern (wait_pattern::INFINITE_WAITS)
    , m_periods_count (0)
    , m_period_index (0)
    , m_stop (false)
    , m_woke_up (false)
  {
    // infinite waits
  }

  looper::looper (const looper &other)
    : m_wait_pattern (other.m_wait_pattern)
    , m_periods_count (other.m_periods_count)
    , m_periods ()
    , m_period_index (0)
    , m_stop (false)
    , m_woke_up (false)
  {
    for (std::size_t i = 0; i < m_periods_count; i++)
      {
	this->m_periods[i] = other.m_periods[i];
      }
  }

  void
  looper::put_to_sleep (waiter &waiter_arg)
  {
    m_woke_up = false;

    if (is_stopped ())
      {
	// stopped; don't put to sleep
	return;
      }

    if (m_period_index >= m_periods_count)
      {
	assert (m_period_index == m_periods_count);
	waiter_arg.wait_inf ();
	m_woke_up = true;
      }
    else
      {
	m_woke_up = waiter_arg.wait_for (m_periods[m_period_index]);
      }
    if (m_wait_pattern == wait_pattern::FIXED_PERIODS || m_wait_pattern == wait_pattern::INFINITE_WAITS)
      {
	assert (m_period_index == 0);
	return;
      }
    if (!m_woke_up)
      {
	/* increment */
	++m_period_index;
      }
    else
      {
	/* reset */
	m_period_index = 0;
      }
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
  looper::woke_up (void) const
  {
    return m_woke_up;
  }

} // namespace cubthread
