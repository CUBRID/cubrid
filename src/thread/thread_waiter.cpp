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

  static const cubperf::stat_id STAT_LOCK_WAKEUP_COUNT = 0;
  static const cubperf::stat_id STAT_SLEEP_COUNT = 1;
  static const cubperf::stat_id STAT_TIMEOUT_COUNT = 2;
  static const cubperf::stat_id STAT_NO_SLEEP_COUNT = 3;
  static const cubperf::stat_id STAT_AWAKEN_COUNT_AND_TIME = 4;
  static const cubperf::statset_definition Waiter_statistics =
  {
    cubperf::stat_definition (STAT_LOCK_WAKEUP_COUNT, cubperf::stat_definition::COUNTER, "waiter_lock_wakeup_count"),
    cubperf::stat_definition (STAT_SLEEP_COUNT, cubperf::stat_definition::COUNTER, "waiter_sleep_count"),
    cubperf::stat_definition (STAT_TIMEOUT_COUNT, cubperf::stat_definition::COUNTER,
			      "waiter_timeout_count"),
    cubperf::stat_definition (STAT_NO_SLEEP_COUNT, cubperf::stat_definition::COUNTER, "waiter_no_sleep_count"),
    cubperf::stat_definition (STAT_AWAKEN_COUNT_AND_TIME, cubperf::stat_definition::COUNTER_AND_TIMER,
			      "waiter_awake_count", "waiter_wakeup_delay_time")
  };

  // atomic statistics
  static const char *STAT_WAKEUP_CALL_COUNT_NAME = "waiter_wakeup_count";

  //////////////////////////////////////////////////////////////////////////
  // waiter
  //////////////////////////////////////////////////////////////////////////

  waiter::waiter ()
    : m_mutex ()
    , m_condvar ()
    , m_status (RUNNING)
    , m_stats (*Waiter_statistics.create_statset ())
    , m_wakeup_calls ("waiter_wakeup_count")
    , m_was_awaken (false)
  {
  }

  waiter::~waiter ()
  {
    delete &m_stats;
  }

  void
  waiter::wakeup (void)
  {
    m_wakeup_calls.increment ();

    // early out if not sleeping
    if (m_status != SLEEPING)
      {
	/* not sleeping */
	return;
      }

    std::unique_lock<std::mutex> lock (m_mutex);
    Waiter_statistics.increment (m_stats, STAT_LOCK_WAKEUP_COUNT);

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
    Waiter_statistics.increment (m_stats, STAT_SLEEP_COUNT);
  }

  void
  waiter::awake (void)
  {
    assert (m_status == SLEEPING);

    m_status = AWAKENING;

    // for statistics
    cubperf::reset_timept (m_stats.m_timept);
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
	Waiter_statistics.time_and_increment (m_stats, STAT_AWAKEN_COUNT_AND_TIME);
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
  waiter::get_stats (cubperf::stat_value *stats_out)
  {
    stats_out[0] = m_wakeup_calls.get_count ();
    Waiter_statistics.get_stat_values_with_converted_timers<std::chrono::microseconds> (m_stats, stats_out + 1);
  }

  std::size_t
  waiter::get_stats_value_count (void)
  {
    // total stats count:
    // one atomic count
    // + Waiter_statistics
    return 1 + Waiter_statistics.get_value_count ();
  }

  bool
  waiter::is_running (void)
  {
    std::unique_lock<std::mutex> lock (m_mutex);    /* mutex is also locked */

    return (m_status == RUNNING);
  }

  const char *
  waiter::get_stat_name (std::size_t stat_index)
  {
    assert (stat_index < get_stats_value_count ());
    if (stat_index == 0)
      {
	// atomic wakeup call count statistic is separate
	return STAT_WAKEUP_CALL_COUNT_NAME;
      }
    else
      {
	return Waiter_statistics.get_value_name (stat_index - 1);
      }
  }

  bool
  waiter::wait_for (const std::chrono::system_clock::duration &delta)
  {
    if (delta == std::chrono::microseconds (0))
      {
	Waiter_statistics.increment (m_stats, STAT_NO_SLEEP_COUNT);
	return true;
      }

    bool ret;

    std::unique_lock<std::mutex> lock (m_mutex);    // mutex is also locked
    goto_sleep ();

    ret = m_condvar.wait_for (lock, delta, [this] { return m_status == AWAKENING; });
    if (!ret)
      {
	Waiter_statistics.increment (m_stats, STAT_TIMEOUT_COUNT);
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
	Waiter_statistics.increment (m_stats, STAT_TIMEOUT_COUNT);
      }

    run ();

    // mutex is automatically unlocked
    return ret;
  }

  //////////////////////////////////////////////////////////////////////////
  // waiter stats
  //////////////////////////////////////////////////////////////////////////

} // namespace cubthread
