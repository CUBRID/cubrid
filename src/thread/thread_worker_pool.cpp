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

#include "thread_executable.hpp"

#if 0

namespace cubthread
{

template <typename Context>
worker_pool::worker_pool (size_t pool_size, size_t work_queue_size)
  : m_max_workers (pool_size)
  , m_worker_count (0)
  , m_work_queue (work_queue_size)
  , m_threads (new std::thread[m_max_workers])
  , m_thread_dispatcher (m_threads, m_max_workers)
  , m_stopped (false)
{
}

template <typename Context>
worker_pool::~worker_pool ()
{
  // not safe to destroy running pools
  assert (m_stopped);
  delete[] m_threads;
}

template <typename Context>
bool
worker_pool::try_execute (task_type * work_arg)
{
  assert (!m_stopped);
  std::thread* thread_p = register_worker ();
  if (thread_p != NULL)
    {
      *thread_p = std::thread (worker_pool<Context>::run, std::ref (*this), std::ref (*thread_p), std::forward<task *> (work_arg));
    }
  return false;
}

template <typename Context>
void
worker_pool::execute (task_type * work_arg)
{
  assert (!m_stopped);
  std::thread* thread_p = register_worker ();
  if (thread_p != NULL)
    {
      *thread_p = std::thread (worker_pool<Context>::run, std::ref (*this), std::ref (*thread_p), std::forward<task *> (work_arg));
    }
  else if (!m_work_queue.produce (work_arg))
    {
      /* failed to produce... this is really unfortunate */
      assert (false);
      m_work_queue.force_produce (work_arg);
    }
}

template <typename Context>
void
worker_pool::stop (void)
{
  if (m_stopped)
    {
      // already closed
      return;
    }
  for (std::size_t i = 0; i < m_max_workers; i++)
    {
      if (m_threads[i].joinable ())
        {
          m_threads[i].join ();
        }
    }
  m_stopped = true;
}

template <typename Context>
bool
worker_pool::is_running (void)
{
  return !m_stopped;
}

template <typename Context>
void
worker_pool::run (worker_pool<Context> & pool, std::thread & thread_arg, task_type * work_arg)
{
  Context& context = work_arg->create_context ();
  task_type * prev_work = NULL;
  do
    {
      if (prev_work != NULL)
        {
          prev_work->retire ();
        }

      work_arg->execute_with_context (context);

      prev_work = work_arg;
    }
  while (pool.is_running() && pool.m_work_queue.consume (work_arg));

  work_arg->retire_context (context);
  work_arg->retire ();

  // no task in queue. deregister worker
  pool.deregister_worker (thread_arg);
}

template <typename Context>
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

template <typename Context>
inline void
worker_pool::deregister_worker (std::thread & thread_arg)
{
  m_thread_dispatcher.retire (thread_arg);
}

} // namespace cubthread

#endif // 0
