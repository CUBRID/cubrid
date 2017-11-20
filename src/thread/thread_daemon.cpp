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

#include "thread_executable.hpp"

namespace cubthread
{

  daemon::daemon (const looper &loop_pattern, task *exec)
    : m_waiter ()
    , m_looper (loop_pattern)
    , m_thread (daemon::loop, this, exec)
  {
    // starts a thread to execute daemon::loop
  }

  daemon::~daemon ()
  {
    // thread must be stopped
    stop ();
  }

  void
  daemon::wakeup (void)
  {
    m_waiter.wakeup ();
  }

  void
  daemon::stop (void)
  {
    // note: this must not be called concurrently

    if (m_looper.is_stopped ())
      {
	// already stopped
	return;
      }

    // first signal stop
    m_looper.stop ();
    // make sure thread will wakeup
    wakeup ();
    // then wait for thread to finish
    m_thread.join ();
  }

  void
  daemon::loop (daemon *daemon_arg, task *exec)
  {
    // loop until stopped
    while (!daemon_arg->m_looper.is_stopped ())
      {
        // execute task
	exec->execute ();

        // take a break
	daemon_arg->pause ();
      }

    // retire task
    exec->retire ();
  }

  void daemon::pause (void)
  {
    m_looper.put_to_sleep (m_waiter);
  }

} // namespace cubthread
