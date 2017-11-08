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
 *
 */

#include "thread_looper.hpp"

#include "thread_waiter.hpp"

#include <cassert>

namespace thread
{

looper::looper ()
  : m_wait_pattern (wait_pattern::INFINITE_WAITS)
  , m_periods_count (0)
  , m_period_index (0)
  , m_stop (false)
{
  // infinite waits
}

template<class Rep, class Period>
looper::looper (std::chrono::duration<Rep, Period>& fixed_period)
  : m_wait_pattern (wait_pattern::FIXED_PERIODS)
  , m_periods_count (1)
  , m_periods ({fixed_period})
  , m_period_index (0)
  , m_stop (false)
{
  // fixed period waits
}

template<class Rep, class Period, size_t Count>
looper::looper (std::array<std::chrono::duration<Rep, Period>, Count> periods)
  : m_wait_pattern (wait_pattern::INCREASING_PERIODS)
  , m_periods_count (Count)
  , m_period_index (0)
  , m_stop (false)
{
  static_assert (Count <= MAX_PERIODS, "Count template cannot exceed MAX_PERIODS=3");
  m_periods_count = std::min (Count, MAX_PERIODS);

  // wait increasing period on timeouts
  for (size_t i = 0; i < m_periods_count; i++)
    {
      m_periods[i] = periods[i];
      // check increasing periods
      assert (i == 0 || m_periods[i - 1] < m_periods[i]);
    }
}

looper::looper (looper & other)
  : m_wait_pattern (other.m_wait_pattern)
  , m_periods_count (other.m_periods_count)
  , m_periods ()
  , m_period_index (0)
  , m_stop (false)
{
  *this->m_periods = *other.m_periods;
}

void
looper::put_to_sleep (waiter & waiter_arg)
{
  bool timeout = false;

  if (is_stopped ())
    {
      // stopped; don't put to sleep
      return;
    }

  if (m_period_index >= m_periods_count)
    {
      assert (m_period_index == m_periods_count);
      waiter_arg.wait_inf ();
    }
  else
    {
      timeout = waiter_arg.wait_for (m_periods[m_period_index]);
    }
  if (m_wait_pattern == wait_pattern::FIXED_PERIODS || m_wait_pattern == wait_pattern::INFINITE_WAITS)
    {
      assert (m_period_index == 0);
      return;
    }
  if (timeout)
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

void
looper::stop (void)
{
  m_stop = true;
}

bool
looper::is_stopped (void) const
{
  return m_stop;
}

} // namespace thread
