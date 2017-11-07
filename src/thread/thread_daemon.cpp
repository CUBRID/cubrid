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

#include "thread_daemon.hpp"

#include "thread_executable.hpp"

namespace thread
{

daemon::daemon (looper & loop_pattern, executable * exec)
  : m_waiter ()
  , m_looper (loop_pattern)
  , m_thread (daemon::loop, this, exec)
{
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
daemon::loop (daemon * daemon_arg, executable * exec)
{
  while (!daemon_arg->m_looper.is_stopped ())
    {
      exec->execute_task ();
      daemon_arg->pause ();
    }
  exec->retire ();
}

void daemon::pause (void)
{
  m_looper.put_to_sleep (m_waiter);
}

} // namespace thread
