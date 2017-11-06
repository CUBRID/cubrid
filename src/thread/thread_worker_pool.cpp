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

#include "thread_worker_pool.hpp"

namespace thread {

bool
worker_pool::try_execute (work * work_arg)
{
  assert (m_open);
  std::thread* thread_p = register_worker ();
  if (thread_p != NULL)
    {
      *thread_p = std::thread (run, std::ref (*this), std::ref (*thread_p), std::forward<work *> (work_arg));
    }
  return false;
}

void
worker_pool::execute (work * work_arg)
{
  assert (m_open);
  std::thread* thread_p = register_worker ();
  if (thread_p != NULL)
    {
      *thread_p = std::thread (run, std::ref (*this), std::ref (*thread_p), std::forward<work *> (work_arg));
    }
  else if (!m_work_queue.produce (work_arg))
    {
      /* failed to produce... this is really unfortunate */
      assert (false);
      m_work_queue.force_produce (work_arg);
    }
}

void
worker_pool::close (void)
{
  if (!m_open)
    {
      // already closed
      return;
    }
  for (int i = 0; i < m_max_workers; i++)
    {
      if (m_threads[i].joinable ())
        {
          m_threads[i].join ();
        }
    }
  m_open = false;
}

bool
worker_pool::is_open (void)
{
  return m_open;
}

void
worker_pool::run (worker_pool & pool, std::thread & thread_arg, work * work_arg)
{
  do
    {
      work_arg->execute_task ();
      work_arg->retire ();

      // loop while there are jobs in queue. there is no point in stopping and restarting another thread
    }
  while (pool.is_open () && pool.m_work_queue.consume (work_arg));

  // no work in queue. deregister worker
  pool.deregister_worker (thread_arg);
}

inline std::thread*
worker_pool::register_worker (void)
{
  std::thread *thread_p;
  thread_p = m_thread_dispatcher.claim ();
  if (thread_p == NULL)
    {
      return NULL;
    }
  if (thread_p->joinable ())
    {
      thread_p->join ();
    }
  return thread_p;
}

inline void
worker_pool::deregister_worker (std::thread & thread_arg)
{
  m_thread_dispatcher.retire (thread_arg);
}

} // namespace thread
