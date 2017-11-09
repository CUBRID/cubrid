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
{
}

waiter::~waiter ()
{
}

void
waiter::wakeup (void)
{
  // early out if not sleeping
  if (m_status != SLEEPING)
    {
      /* not sleeping */
      return;
    }

  std::unique_lock<std::mutex> lock (m_mutex);
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
waiter::sleep (void)
{
  assert (m_status == RUNNING);
  m_status = SLEEPING;
}

void
waiter::awake (void)
{
  if (m_status != SLEEPING)
    {
      // somebody else woke it
      return;
    }
  m_status = AWAKENING;
}

void
waiter::run (void)
{
  assert (m_status == AWAKENING);
  m_status = RUNNING;
}

void
waiter::wait_inf (void)
{
  std::unique_lock<std::mutex> lock (m_mutex);    /* mutex is also locked */
  sleep ();

  // wait
  m_condvar.wait (lock, [this] { return m_status == AWAKENING; });

  run ();

  // mutex is automatically unlocked
}

} // namespace cubthread
