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

#ifndef _THREAD_WORKER_POOL_HPP_
#define _THREAD_WORKER_POOL_HPP_

#include "lockfree_circular_queue.hpp"
#include "resource_shared_pool.hpp"

#include <mutex>
#include <thread>

namespace cubthread {

// forward definition
class task;
template <typename Context>
class contextual_task;

template <typename Context>
class worker_pool
{
public:
  typedef contextual_task<Context> task_type;

  worker_pool (std::size_t pool_size, std::size_t work_queue_size);
  ~worker_pool ();

  bool try_execute (task_type * work_arg);
  void execute (task_type * work_arg);
  void stop (void);
  bool is_running (void);
  bool is_busy (void);
  bool is_full (void);

private:
  static void run (worker_pool<Context> & pool, std::thread & thread_arg, task_type * work_arg);

  inline void push_execute (std::thread & thread_arg, task_type * work_arg);
  inline std::thread* register_worker (void);
  inline void deregister_worker (std::thread & thread_arg);

  const std::size_t m_max_workers;
  std::atomic<std::size_t> m_worker_count;
  lockfree::circular_queue<task_type *> m_work_queue;
  std::thread *m_threads;
  resource_shared_pool<std::thread> m_thread_dispatcher;
  bool m_stopped;
};

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_HPP_

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

namespace cubthread
{

template <typename Context>
worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t work_queue_size)
  : m_max_workers (pool_size)
  , m_worker_count (0)
  , m_work_queue (work_queue_size)
  , m_threads (new std::thread[m_max_workers])
  , m_thread_dispatcher (m_threads, m_max_workers)
  , m_stopped (false)
{
}

template <typename Context>
worker_pool<Context>::~worker_pool ()
{
  // not safe to destroy running pools
  assert (m_stopped);
  delete[] m_threads;
}

template <typename Context>
bool
worker_pool<Context>::try_execute (task_type * work_arg)
{
  assert (!m_stopped);
  std::thread* thread_p = register_worker ();
  if (thread_p != NULL)
    {
      push_execute (*thread_p, work_arg);
    }
  return false;
}

template <typename Context>
void
worker_pool<Context>::execute (task_type * work_arg)
{
  assert (!m_stopped);
  std::thread* thread_p = register_worker ();
  if (thread_p != NULL)
    {
      push_execute (*thread_p, work_arg);
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
worker_pool<Context>::push_execute (std::thread & thread_arg, task_type * work_arg)
{
  thread_arg = std::thread (worker_pool<Context>::run,
                            std::ref (*this),
                            std::ref (thread_arg),
                            std::forward<task_type *> (work_arg));
}

template <typename Context>
void
worker_pool<Context>::stop (void)
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
worker_pool<Context>::is_running (void)
{
  return !m_stopped;
}

template<typename Context>
inline bool
worker_pool<Context>::is_busy (void)
{
  return m_worker_count == m_max_workers;
}

template<typename Context>
inline bool worker_pool<Context>::is_full (void)
{
  return m_work_queue.is_full ();
}

template <typename Context>
void
worker_pool<Context>::run (worker_pool<Context> & pool, std::thread & thread_arg, task_type * work_arg)
{
  Context& context = work_arg->create_context ();
  task_type * prev_work = NULL;
  do
    {
      if (prev_work != NULL)
        {
          prev_work->retire ();
        }

      work_arg->execute (context);

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
worker_pool<Context>::register_worker (void)
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
worker_pool<Context>::deregister_worker (std::thread & thread_arg)
{
  m_thread_dispatcher.retire (thread_arg);
}

} // namespace cubthread
