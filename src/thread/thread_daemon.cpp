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
  daemon::~daemon ()
  {
    // thread must be stopped
    stop_execution ();
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
    stats_out[0] = m_loop_count;
    stats_out[1] = m_execute_time / 1000000;  // nano => milli
    stats_out[2] = m_pause_time / 1000000;    // nano => milli

    m_looper.get_stats (&stats_out[3]); // get looper stats
    m_waiter.get_stats (&stats_out[3 + looper::STAT_COUNT]);  // get waiter stats
  }

} // namespace cubthread
