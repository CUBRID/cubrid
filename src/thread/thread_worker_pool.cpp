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

#include <thread>

bool
thread::worker_pool::try_execute (work * work_arg)
{
  if (register_worker ())
    {
      std::thread (run, std::ref (*this), std::forward (work_arg)).detach ();
      return true;
    }
  return false;
}

void
thread::worker_pool::execute (work * work_arg)
{
  if (register_worker ())
    {
      std::thread (run, std::ref (*this), std::forward (work_arg)).detach ();
    }
  else if (!m_work_queue.produce (work_arg))
    {
      /* failed to produce... this is really unfortunate */
      assert (false);
      m_work_queue.force_produce (work_arg);
    }
}

void
thread::worker_pool::run (worker_pool & pool, work * work_arg)
{
  do
    {
      work_arg->execute_task ();
      work_arg->retire ();
    }
  while (pool.m_work_queue.consume (work_arg));

  // no work in queue. deregister worker
  deregister_worker ();
}

inline bool
thread::worker_pool::register_worker (void)
{
  size_t worker_count;
  do
    {
      worker_count = m_worker_count.load ();
      if (worker_count >= m_max_workers)
        {
          assert (worker_count == m_max_workers);
          return false;
        }
    }
  while (!m_worker_count.compare_exchange_weak (worker_count, worker_count + 1));

  // worker registered
  return true;
}

inline void
thread::worker_pool::deregister_worker (void)
{
  --m_worker_count;
}
