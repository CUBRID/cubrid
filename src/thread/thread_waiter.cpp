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
#include "perf.hpp"

#include <cassert>

// todo: fix time unit

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // statistics collector
  //////////////////////////////////////////////////////////////////////////

  static cubperf::stat_id STAT_LOCK_WAKEUP_COUNT = 0;
  static cubperf::stat_id STAT_SLEEP_COUNT = 0;
  static cubperf::stat_id STAT_TIMEOUT_COUNT = 0;
  static cubperf::stat_id STAT_NO_SLEEP_COUNT = 0;
  static cubperf::stat_id STAT_AWAKEN_COUNT_AND_TIME = 0;
  static cubperf::statset_definition waiter_statistics =
  {
    cubperf::statset_definition::stat_count ("waiter_lock_wakeup_count", STAT_LOCK_WAKEUP_COUNT),
    cubperf::statset_definition::stat_count ("waiter_sleep_count", STAT_SLEEP_COUNT),
    cubperf::statset_definition::stat_count ("waiter_timeout_count", STAT_TIMEOUT_COUNT),
    cubperf::statset_definition::stat_count ("waiter_no_sleep_count", STAT_NO_SLEEP_COUNT),
    cubperf::statset_definition::stat_count_time ("waiter_awaken_count", "waiter_awaken_delay",
    STAT_AWAKEN_COUNT_AND_TIME)
  };

  static cubperf::stat_id ATOMIC_STAT_WAKEUP_COUNT = 0;
  cubperf::statset_definition waiter_atomic_statistics =
  {
    cubperf::statset_definition::stat_count ("waiter_wakeup_count", ATOMIC_STAT_WAKEUP_COUNT)
  };

  //////////////////////////////////////////////////////////////////////////
  // waiter
  //////////////////////////////////////////////////////////////////////////

  waiter::waiter ()
    : m_mutex ()
    , m_condvar ()
    , m_status (RUNNING)
    , m_stats_p (NULL)
    , m_atomic_stats_p (NULL)
    , m_was_awaken (false)
  {
    m_stats_p = new cubperf::statset (waiter_statistics);
    m_atomic_stats_p = new cubperf::atomic_statset (waiter_atomic_statistics);
  }

  waiter::~waiter ()
  {
  }

  void
  waiter::wakeup (void)
  {
    m_atomic_stats_p->increment (ATOMIC_STAT_WAKEUP_COUNT);

    // early out if not sleeping
    if (m_status != SLEEPING)
      {
	/* not sleeping */
	return;
      }

    std::unique_lock<std::mutex> lock (m_mutex);
    m_stats_p->increment (STAT_LOCK_WAKEUP_COUNT);

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
    m_stats_p->increment (STAT_SLEEP_COUNT);
  }

  void
  waiter::awake (void)
  {
    assert (m_status == SLEEPING);

    m_status = AWAKENING;

    // for statistics
    m_stats_p->reset_timepoint ();
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
	m_stats_p->increment_and_time (STAT_AWAKEN_COUNT_AND_TIME);
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
    m_atomic_stats_p->get_stats (stats_out);
    m_stats_p->get_stats (stats_out + waiter_atomic_statistics.get_value_count ());
  }

  bool
  waiter::wait_for (const std::chrono::system_clock::duration &delta)
  {
    if (delta == std::chrono::microseconds (0))
      {
	m_stats_p->increment (STAT_NO_SLEEP_COUNT);
	return true;
      }

    bool ret;

    std::unique_lock<std::mutex> lock (m_mutex);    // mutex is also locked
    goto_sleep ();

    ret = m_condvar.wait_for (lock, delta, [this] { return m_status == AWAKENING; });
    if (!ret)
      {
	m_stats_p->increment (STAT_TIMEOUT_COUNT);
      }

    run ();

    // mutex is automatically unlocked
    return ret;
  }

  bool
  waiter::wait_until (const std::chrono::system_clock::time_point &timeout_time)
  {
    std::unique_lock<std::mutex> lock (m_mutex);    // mutex is also locked
    goto_sleep ();

    bool ret = m_condvar.wait_until (lock, timeout_time, [this] { return m_status == AWAKENING; });
    if (!ret)
      {
	m_stats_p->increment (STAT_TIMEOUT_COUNT);
      }

    run ();

    // mutex is automatically unlocked
    return ret;
  }

  //////////////////////////////////////////////////////////////////////////
  // waiter stats
  //////////////////////////////////////////////////////////////////////////

} // namespace cubthread
