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
 * thread_worker_pool.hpp
 */

#ifndef _THREAD_WORKER_POOL_HPP_
#define _THREAD_WORKER_POOL_HPP_

#include "lockfree_circular_queue.hpp"
#include "resource_shared_pool.hpp"
#include "thread_task.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace cubthread
{
  // cubtread::worker_pool<Context>
  //
  //  templates
  //    Context - thread context; a class to cache helpful information for task execution
  //
  //  description
  //    a pool of threads to execute tasks in parallel
  //    for high-loads (more tasks than threads), stores tasks in a queue to be executed when a thread is freed.
  //    for low-loads (fewer tasks than threads), retires thread when no new tasks are available and creates new threads
  //      when tasks are added again
  //    in high-loads, thread context is shared between task
  //
  // how to use
  //    // note that worker_pool is must be specialized with a thread context
  //
  //    // define the thread context; for CUBRID, that is usually cubthread::entry
  //    class custom_context { ... };
  //
  //    // then define the task
  //    class custom_task : public task<custom_context>
  //    {
  //      void execute (Context &) override { ... }
  //      void create_context (void) override { ... }
  //      void retire_context (Context &) override { ... }
  //    };
  //
  //    // create worker pool
  //    cubthread::worker_pool<custom_context> thread_pool (THREAD_COUNT, MAX_TASKS);
  //
  //    // push tasks
  //    for (std::size_t i = 0; i < task_count; i++)
  //      {
  //        thread_pool.execute (new custom_task ());   // tasks are deallocated after execution
  //
  //        // if you push more than worker pool can handle, assert is hit; release will wait for task to be pushed
  //      }
  //
  //    // on destroy, worker pools stops execution (jobs in queue are not executed) and joins any running threads
  //
  //  todo:
  //    [Optional] Specialize worker_pool with thread context.
  //
  //    [Optional] Define a way to stop worker pool, but to finish executing everything it has in queue.
  //
  template <typename Context>
  class worker_pool
  {
    public:
      using task_type = task<Context>;
      using context_manager_type = context_manager<Context>;

      worker_pool (std::size_t pool_size, std::size_t work_queue_size, context_manager_type *context_mgr);
      ~worker_pool ();

      // try to execute task; executes only if there is an available thread; otherwise returns false
      bool try_execute (task_type *work_arg);

      // execute task; execution is guaranteed, even if job queue is full. debug will crash though
      void execute (task_type *work_arg);

      // stop worker pool; stop all running threads and join
      // note: do not call concurrently
      void stop (void);

      // is_running = is not stopped; when created, a worker pool starts running
      bool is_running (void) const;

      // is_busy = is no thread available for a new task
      bool is_busy (void) const;

      // is_full = work queue is full
      bool is_full (void) const;

      // get number of threads currently running
      // note: this count may change after call
      std::size_t get_running_count (void) const;
      std::size_t get_max_count (void) const;

    private:

      // function executed by worker; executes first task and then continues with any task it finds in queue
      static void run (worker_pool<Context> &pool, std::thread &thread_arg, task_type *work_arg);

      // push task to immediate execution
      inline void push_execute (std::thread &thread_arg, task_type *work_arg);

      // register a new worker; return NULL if no worker is available
      inline std::thread *register_worker (void);

      // deregister worker when it stops running
      inline void deregister_worker (std::thread &thread_arg);

      // maximum number of concurrent workers
      const std::size_t m_max_workers;

#if defined (NO_GCC_44)
      // current worker count
      std::atomic<std::size_t> m_worker_count;
#else
      volatile std::size_t m_worker_count;
#endif

      // work queue to store tasks that cannot be immediately executed
      lockfree::circular_queue<task_type *> m_work_queue;

      // thread context manager
      context_manager_type &m_context_manager;

      // thread objects
      std::thread *m_threads;

      // thread "dispatcher" - a pool of threads
      resource_shared_pool<std::thread> m_thread_dispatcher;

      // set to true when stopped
      std::atomic<bool> m_stopped;
  };

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_HPP_

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

namespace cubthread
{

  template <typename Context>
  worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t work_queue_size,
				     context_manager_type *context_mgr)
    : m_max_workers (pool_size)
    , m_worker_count (0)
    , m_work_queue (work_queue_size)
    , m_context_manager (*context_mgr)
    , m_threads (new std::thread[m_max_workers])
    , m_thread_dispatcher (m_threads, m_max_workers)
    , m_stopped (false)
  {
    // new pool with worker count and work queue size
  }

  template <typename Context>
  worker_pool<Context>::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);
    assert (m_work_queue.is_empty ());
    delete[] m_threads;
  }

  template <typename Context>
  bool
  worker_pool<Context>::try_execute (task_type *work_arg)
  {
    assert (!m_stopped);
    std::thread *thread_p = register_worker ();
    if (thread_p != NULL)
      {
	push_execute (*thread_p, work_arg);
      }
    return false;
  }

  template <typename Context>
  void
  worker_pool<Context>::execute (task_type *work_arg)
  {
    assert (!m_stopped);
    std::thread *thread_p = register_worker ();
    if (thread_p != NULL)
      {
	push_execute (*thread_p, work_arg);
      }
    else if (!m_work_queue.produce (work_arg))
      {
	// failed to produce... this is really unfortunate (and unwanted)
	assert (false);
	m_work_queue.force_produce (work_arg);
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::push_execute (std::thread &thread_arg, task_type *work_arg)
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
    if (m_stopped.exchange (true))
      {
	// already stopped
	return;
      }
    else
      {
	// I am responsible with stopping threads
      }

    // join all threads
    for (std::size_t i = 0; i < m_max_workers; i++)
      {
	if (m_threads[i].joinable ())
	  {
	    m_threads[i].join ();
	  }
      }

    // retire all tasks that have not been executed
    task_type *task = NULL;
    while (m_work_queue.consume (task))
      {
	task->retire ();
      }
  }

  template <typename Context>
  bool
  worker_pool<Context>::is_running (void) const
  {
    return !m_stopped;
  }

  template<typename Context>
  inline bool
  worker_pool<Context>::is_busy (void) const
  {
    return m_worker_count == m_max_workers;
  }

  template<typename Context>
  inline bool worker_pool<Context>::is_full (void) const
  {
    return m_work_queue.is_full ();
  }

  template<typename Context>
  std::size_t
  worker_pool<Context>::get_running_count (void) const
  {
    return m_worker_count;
  }

  template<typename Context>
  inline std::size_t
  worker_pool<Context>::get_max_count (void) const
  {
    return m_max_workers;
  }

  template <typename Context>
  void
  worker_pool<Context>::run (worker_pool<Context> &pool, std::thread &thread_arg, task_type *task_arg)
  {
    // create context for task execution
    Context &context = pool.m_context_manager.create_context ();
    bool first_loop = true;

    // loop as long as pool is running and there are tasks in queue
    task_type *task_p = task_arg;
    do
      {
	if (!first_loop)
	  {
	    // make sure context can be reused
	    pool.m_context_manager.recycle_context (context);
	  }

	// execute current task
	task_p->execute (context);
	// and retire it
	task_p->retire ();

	// consume another task
      }
    while (pool.is_running() && pool.m_work_queue.consume (task_p));

    // retire thread context
    pool.m_context_manager.retire_context (context);

    // end of run; deregister worker
    pool.deregister_worker (thread_arg);
  }

  template <typename Context>
  inline std::thread *
  worker_pool<Context>::register_worker (void)
  {
    std::thread *thread_p;

    // claim a thread
    thread_p = m_thread_dispatcher.claim ();
    if (thread_p == NULL)
      {
	// no threads available
	return NULL;
      }

    // if thread was already used, we must join it before reusing
    if (thread_p->joinable ())
      {
	thread_p->join ();
      }

#if defined (NO_GCC_44)
    ++m_worker_count;
#else
    (void) ATOMIC_INC (&m_worker_count, 1);
#endif
    return thread_p;
  }

  template <typename Context>
  inline void
  worker_pool<Context>::deregister_worker (std::thread &thread_arg)
  {
    m_thread_dispatcher.retire (thread_arg);
#if defined (NO_GCC_44)
    --m_worker_count;
#else
    (void) ATOMIC_INC (&m_worker_count, -1);
#endif
  }

} // namespace cubthread
