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
 * thread_waiter - implementation for suspending/waking threads
 */

#include "thread_waiter.hpp"

#include <cassert>

namespace cubthread
{

  waiter::waiter ()
    : m_mutex ()
    , m_condvar ()
    , m_status (RUNNING)
    , m_wakeup_count (0)
    , m_wakeup_lock_count (0)
    , m_awake_count (0)
    , m_wait_count (0)
    , m_timeout_count (0)
    , m_wait_zero (0)
    , m_wakeup_delay (0)
    , m_wait_time (0)
    , m_awake_time (0)
    , m_was_awaken (false)
  {
  }

  waiter::~waiter ()
  {
  }

  void
  waiter::wakeup (void)
  {
    ++m_wakeup_count;

    // early out if not sleeping
    if (m_status != SLEEPING)
      {
	/* not sleeping */
	return;
      }

    std::unique_lock<std::mutex> lock (m_mutex);
    ++m_wakeup_lock_count;

    if (m_status != SLEEPING)
      {
	return;
      }

    awake ();
    // unlock before notifying to avoid blocking the thread on mutex
    lock.unlock ();
    m_condvar.notify_one ();
  }

  bool
  waiter::check_wake (void)
  {
    return m_status == AWAKENING;
  }

  void
  waiter::goto_sleep (void)
  {
    assert (m_status == RUNNING);
    m_status = SLEEPING;
    // for statistics
    m_was_awaken = false;
    ++m_wait_count;
  }

  void
  waiter::awake (void)
  {
    assert (m_status == SLEEPING);
    m_status = AWAKENING;
    // for statistics
    m_awake_time = clock_type::now ();
    ++m_awake_count;
    m_was_awaken = true;
  }

  void
  waiter::run (void)
  {
    assert (m_status == AWAKENING || m_status == SLEEPING);
    m_status = RUNNING;
    // for statistics
    if (m_was_awaken)
      {
	m_wakeup_delay += (clock_type::now () - m_awake_time).count ();
      }
  }

  void
  waiter::wait_inf (void)
  {
    std::unique_lock<std::mutex> lock (m_mutex);    /* mutex is also locked */
    goto_sleep ();

    // wait
    m_condvar.wait (lock, [this] { return m_status == AWAKENING; });

    run ();

    // mutex is automatically unlocked
  }

  void
  waiter::get_stats (stat_type *stats_out)
  {
    stats_out[0] = m_wakeup_count;
    stats_out[1] = m_wakeup_lock_count;
    stats_out[2] = m_awake_count;
    stats_out[3] = m_wait_count;
    stats_out[4] = m_timeout_count;
    stats_out[5] = m_wait_zero;
    stats_out[6] = m_wakeup_delay;
    stats_out[7] = m_wait_time;
  }

} // namespace cubthread
