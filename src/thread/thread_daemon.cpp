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
 * thread_daemon - interface for daemon threads
 */

#include "thread_daemon.hpp"

#include "thread_task.hpp"

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // daemon statistics
  //////////////////////////////////////////////////////////////////////////
  static cubperf::stat_id STAT_LOOP_EXECUTE_COUNT_AND_TIME;
  static cubperf::stat_id STAT_LOOP_PAUSE_TIME; // count is same as for execute
  static const cubperf::statset_definition Daemon_statistics =
  {
    cubperf::stat_definition (STAT_LOOP_EXECUTE_COUNT_AND_TIME, cubperf::stat_definition::COUNTER_AND_TIMER,
    "daemon_loop_count", "daemon_execution_time"),
    cubperf::stat_definition (STAT_LOOP_PAUSE_TIME, cubperf::stat_definition::TIMER, "daemon_pause_time")
  };

  //////////////////////////////////////////////////////////////////////////
  // daemon implementation
  //////////////////////////////////////////////////////////////////////////

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
  daemon::get_stats (stat_type *stats_out)
  {
    int i = 0;

    // get daemon stats
    stats_out[i++] = m_loop_count;
    stats_out[i++] = m_execute_time / 1000000;  // nano => milli
    stats_out[i++] = m_pause_time / 1000000;    // nano => milli

    // get looper stats
    m_looper.get_stats (&stats_out[i]);
    i += looper::STAT_COUNT;

    // get waiter stats
    m_waiter.get_stats (&stats_out[i]);
    // i += waiter::STAT_COUNT;
  }

  cubperf::statset &
  daemon::create_statset (void)
  {
    // TODO: insert return statement here
  }

} // namespace cubthread
