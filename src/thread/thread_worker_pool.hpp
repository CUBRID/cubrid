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

#include "error_manager.h"
#include "lockfree_circular_queue.hpp"
#include "resource_shared_pool.hpp"
#include "thread_task.hpp"

#include <atomic>
#include <chrono>
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
  //    [Optional] Define a way to stop worker pool, but to finish executing everything it has in queue.
  //
  template <typename Context>
  class worker_pool
  {
    public:
      using context_type = Context;
      using task_type = task<Context>;
      using context_manager_type = context_manager<Context>;

      worker_pool (std::size_t pool_size, std::size_t work_queue_size, context_manager_type *context_mgr,
		   bool debug_logging = false);
      ~worker_pool ();

      // try to execute task; executes only if there is an available thread; otherwise returns false
      bool try_execute (task_type *work_arg);

      // execute task; execution is guaranteed, even if job queue is full. debug will crash though
      void execute (task_type *work_arg);

      // stop worker pool; stop all running threads and join
      // note: do not call concurrently
      void stop_execution (void);

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
      using atomic_context_ptr = std::atomic<context_type *>;

      // function executed by worker; executes first task and then continues with any task it finds in queue
      static void run (worker_pool<Context> &pool, std::thread &thread_arg, task_type *work_arg);

      // push task to immediate execution
      inline void push_execute (std::thread &thread_arg, task_type *work_arg);

      // register a new worker; return NULL if no worker is available
      inline std::thread *register_worker (void);

      // deregister worker when it stops running
      inline void deregister_worker (std::thread &thread_arg);

      // thread index
      inline size_t get_thread_index (std::thread &thread_arg);

      void stop_all_contexts (void);

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

      // contexts being used, one for each thread. thread <-> context matching based on index
      atomic_context_ptr *m_context_pointers;

      // set to true when stopped
      std::atomic<bool> m_stopped;

      bool m_log;
  };

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_HPP_

/************************************************************************/
/* Template implementation                                              */
/************************************************************************/

#define THREAD_WP_LOG(func, msg, ...) if (m_log) _er_log_debug (ARG_FILE_LINE, func ": " msg "\n", __VA_ARGS__)

namespace cubthread
{

  template <typename Context>
  worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t work_queue_size,
				     context_manager_type *context_mgr, bool debug_log)
    : m_max_workers (pool_size)
    , m_worker_count (0)
    , m_work_queue (work_queue_size)
    , m_context_manager (*context_mgr)
    , m_threads (new std::thread[m_max_workers])
    , m_thread_dispatcher (m_threads, m_max_workers, true)
    , m_context_pointers (NULL)
    , m_stopped (false)
    , m_log (debug_log)
  {
    // new pool with worker count and work queue size

    // m_running_context array => all nulls
    m_context_pointers = new atomic_context_ptr [m_max_workers];
    for (std::size_t i = 0; i < m_max_workers; ++i)
      {
	m_context_pointers[i] = NULL;
      }
  }

  template <typename Context>
  worker_pool<Context>::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);
    assert (m_work_queue.is_empty ());
    delete [] m_threads;
    delete [] m_context_pointers;
  }

  template <typename Context>
  bool
  worker_pool<Context>::try_execute (task_type *work_arg)
  {
    if (m_stopped)
      {
	return false;
      }
    std::thread *thread_p = register_worker ();
    if (thread_p != NULL)
      {
	push_execute (*thread_p, work_arg);
      }
    else
      {
	THREAD_WP_LOG ("try_execute", "drop task = %p", work_arg);
      }
    return false;
  }

  template <typename Context>
  void
  worker_pool<Context>::execute (task_type *work_arg)
  {
    std::thread *thread_p = register_worker ();
    if (thread_p != NULL)
      {
	push_execute (*thread_p, work_arg);
      }
    else if (m_stopped)
      {
	// do not accept other new requests
	assert (false);
	work_arg->retire ();
	return;
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
    THREAD_WP_LOG ("push_execute", "thread = %zu, task = %p", get_thread_index (thread_arg), work_arg);

    thread_arg = std::thread (worker_pool<Context>::run,
			      std::ref (*this),
			      std::ref (thread_arg),
			      std::forward<task_type *> (work_arg));
  }

  template <typename Context>
  void
  worker_pool<Context>::stop_all_contexts (void)
  {
    context_type *context_p = NULL;
    for (std::size_t i = 0; i < m_max_workers; ++i)
      {
	context_p = m_context_pointers[i];
	if (context_p != NULL)
	  {
	    // indicate execution context to stop
	    context_p->interrupt_execution ();
	  }
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::stop_execution (void)
  {
    if (m_stopped.exchange (true))
      {
	// already stopped
	THREAD_WP_LOG ("stop", "stop was %s", "true");
	return;
      }
    else
      {
	THREAD_WP_LOG ("stop", "stop was %s", "false");
	// I am responsible with stopping threads
      }

    const std::chrono::seconds time_wait_to_thread_stop (30);   // timeout duration = 30 secs
    const std::chrono::milliseconds time_spin_sleep (10);       // slip between spins for 10 milliseconds

    // stop all thread execution
    stop_all_contexts ();

    // we need to register all workers to be sure all threads are stopped and nothing new starts
    auto timeout = std::chrono::system_clock::now () + time_wait_to_thread_stop; // timeout time

    // spin until all threads are registered.
    std::thread *thread_p;
    std::size_t stop_count = 0;
    while (stop_count < m_max_workers && std::chrono::system_clock::now () < timeout)
      {
	thread_p = register_worker ();
	if (thread_p == NULL)
	  {
	    // in case new threads have started, tell them to stop
	    stop_all_contexts ();

	    // sleep for 10 milliseconds to give running threads a chance to finish
	    std::this_thread::sleep_for (time_spin_sleep);
	  }
	else
	  {
	    ++stop_count;
	  }
      }

    if (stop_count < m_max_workers)
      {
	assert (false);
      }
    else
      {
	// all threads are joined
      }

    // retire all tasks that have not been executed; at this point, no new tasks are produced
    task_type *task = NULL;
    while (m_work_queue.consume (task))
      {
	THREAD_WP_LOG ("stop", "retire without execution task = %p", task);
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
#define THREAD_WP_STATIC_LOG(msg, ...) if (pool.m_log) _er_log_debug (ARG_FILE_LINE, "run: " msg, __VA_ARGS__)

    if (!pool.is_running())
      {
	// task must not be executed if it was pushed for execution after worker pool was stopped
	task_arg->retire();

	// deregister worker
	pool.deregister_worker (thread_arg);
	return;
      }

    // create context for task execution
    std::size_t thread_index = pool.get_thread_index (thread_arg);
    Context &context = pool.m_context_manager.create_context ();
    bool first_loop = true;

    // save to pool contexts too
    pool.m_context_pointers[thread_index] = &context;

    // loop as long as pool is running and there are tasks in queue
    task_type *task_p = task_arg;
    do
      {
	THREAD_WP_STATIC_LOG ("loop on thread = %zu, context = %p, task = %p", thread_index, &context, task_p);

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

    THREAD_WP_STATIC_LOG ("stop on thread = %zu, context = %p", thread_index, &context);

    // remove context from pool and retire
    pool.m_context_pointers[thread_index] = NULL;
    pool.m_context_manager.retire_context (context);

    // end of run; deregister worker
    pool.deregister_worker (thread_arg);

#undef THREAD_WP_STATIC_LOG
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
	THREAD_WP_LOG ("register_worker", "thread_p = %p", NULL);
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

    THREAD_WP_LOG ("register_worker", "thread = %zu", get_thread_index (*thread_p));
    return thread_p;
  }

  template <typename Context>
  inline void
  worker_pool<Context>::deregister_worker (std::thread &thread_arg)
  {
    // THREAD_WP_LOG ("deregister_worker", "thread = %zu", get_thread_index (thread_arg));
    // no logging here; no thread & error context
    m_thread_dispatcher.retire (thread_arg);
#if defined (NO_GCC_44)
    --m_worker_count;
#else
    (void) ATOMIC_INC (&m_worker_count, -1);
#endif
  }

  template<typename Context>
  inline size_t
  cubthread::worker_pool<Context>::get_thread_index (std::thread &thread_arg)
  {
    size_t index = &thread_arg - m_threads;
    assert (index >= 0 && index < m_max_workers);
    return index;
  }

#undef THREAD_WP_LOG

} // namespace cubthread
