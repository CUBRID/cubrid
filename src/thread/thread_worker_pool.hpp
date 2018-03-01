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
#include "thread_task.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <cassert>

namespace cubthread
{
  // forward definition
  class worker;

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

      void get_stats (std::uint64_t *stats_out);

      //////////////////////////////////////////////////////////////////////////
      // context management
      //////////////////////////////////////////////////////////////////////////

      template <typename Func, typename ... Args>
      void map_running_contexts (Func &&func, Args &&... args);

    private:
      using atomic_context_ptr = std::atomic<context_type *>;
      friend class worker;

      // function executed by worker; executes first task and then continues with any task it finds in queue
      static void run (worker_pool<Context> &pool, worker &thread_arg, task_type *work_arg);

      // register a new worker; return NULL if no worker is available
      worker *register_worker (void);

      bool set_worker_available_for_new_work (worker &thread_arg);

      // deregister worker when it stops running
      void deregister_worker (worker &thread_arg);

      // thread index
      size_t get_thread_index (worker &thread_arg);

      void stop_all_contexts (void);

      // maximum number of concurrent workers
      const std::size_t m_max_workers;

      // current worker count
      std::atomic<std::size_t> m_worker_count;

      // work queue to store tasks that cannot be immediately executed
      lockfree::circular_queue<task_type *> m_work_queue;

      // thread context manager
      context_manager_type &m_context_manager;

      // thread objects
      worker *m_workers;

      // thread "dispatcher" - a pool of threads
      lockfree::circular_queue<worker *> m_worker_dispatcher;
      lockfree::circular_queue<worker *> m_sleepers;

      // contexts being used, one for each thread. thread <-> context matching based on index
      atomic_context_ptr *m_context_pointers;

      // statistics
      using stat_time_unit = std::uint64_t;
      using stat_count_type = std::atomic<std::uint64_t>;
      using stat_time_type = std::atomic<stat_time_unit>;

      stat_count_type m_stat_worker_count;
      stat_count_type m_stat_task_count;
      stat_time_type m_stat_register_time;
      stat_time_type m_stat_start_thread_time;
      stat_time_type m_stat_claim_context_time;
      stat_time_type m_stat_execute_time;
      stat_time_type m_stat_retire_context_time;
      stat_time_type m_stat_deregister_time;

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
  class worker
  {
    public:
      using clock_type = std::chrono::high_resolution_clock;
      using time_point_type = clock_type::time_point;

      worker ()
	: m_thread ()
	, m_state (state::FREE_FOR_REGISTER)
	, m_task_cv ()
	, m_task_mutex ()
	, m_task_p (NULL)
	, m_waiting_assignment (false)
	, m_push_time ()
	, m_register_time ()
	, m_run_time ()
	, m_execute_time ()
	, m_retire_time ()
	, m_deregister_time ()
      {
	//
      }

      void
      set_registered (time_point_type &push_time)
      {
	assert (m_state == state::FREE_FOR_REGISTER);

	if (m_thread.joinable ())
	  {
	    // need to join first; maybe it is still running
	    m_thread.join ();
	  }

	// now it is safe to do other operations
	m_push_time = push_time;
	m_register_time = clock_type::now ();

	m_state = state::REGISTERED_FOR_RUNNING;
      }

      void
      set_running (void)
      {
	assert (m_state == state::REGISTERED_FOR_RUNNING);
	m_run_time = clock_type::now ();

	m_state = state::CLAIMING_CONTEXT;
      }

      void
      set_executing (void)
      {
	assert (m_state == state::CLAIMING_CONTEXT);
	m_execute_time = clock_type::now ();

	m_state = state::EXECUTING_TASKS;
      }

      void
      set_retiring (void)
      {
	assert (m_state == state::EXECUTING_TASKS || m_state == state::REGISTERED_FOR_RUNNING);
	m_retire_time = clock_type::now ();

	m_state = state::RETIRING_CONTEXT;
      }

      void
      set_deregistering (void)
      {
	assert (m_state == state::RETIRING_CONTEXT);
	m_deregister_time = clock_type::now ();

	m_state = state::DEREGISTERING;
      }

      void
      set_free (void)
      {
	assert (m_state == state::DEREGISTERING || m_state == state::EXECUTING_TASKS);
	m_free_time = clock_type::now ();
	if (m_state == state::EXECUTING_TASKS)
	  {
	    m_retire_time = m_deregister_time = m_free_time;
	  }

	m_state = state::FREE_FOR_REGISTER;
      }

      template <typename Context>
      void
      start_thread (worker_pool<Context> &pool_ref, task<Context> *work_p)
      {
	assert (m_state == state::REGISTERED_FOR_RUNNING);

	if (assign_task (work_p))
	  {
	    // thread is running and took the task
	  }
	else
	  {
	    std::thread (worker_pool<Context>::run, std::ref (pool_ref), std::ref (*this), work_p).detach ();
	  }
      }

      void
      get_stats (std::chrono::nanoseconds &delta_register, std::chrono::nanoseconds &delta_start_thread,
		 std::chrono::nanoseconds &delta_register_context, std::chrono::nanoseconds &delta_execute,
		 std::chrono::nanoseconds &delta_retire_context, std::chrono::nanoseconds &delta_deregister)
      {
	assert (m_state == state::FREE_FOR_REGISTER);

	delta_register = m_register_time - m_push_time;
	delta_start_thread = m_run_time - m_register_time;
	delta_register_context = m_execute_time - m_run_time;
	delta_execute = m_retire_time - m_execute_time;
	delta_retire_context = m_deregister_time - m_retire_time;
	delta_deregister = m_free_time - m_deregister_time;
      }

      void *
      wait_for_task (void)
      {
	std::unique_lock<std::mutex> ulock (m_task_mutex);
	m_waiting_assignment = true;
	m_task_cv.wait_for (ulock, std::chrono::seconds (60),
			    [this] { return m_waiting_assignment && m_task_p != NULL; });
	m_waiting_assignment = false;

	return m_task_p;
      }

      void
      stop_waiting (void)
      {
	{
	  std::unique_lock<std::mutex> ulock (m_task_mutex);
	  if (!m_waiting_assignment)
	    {
	      return;
	    }
	  m_waiting_assignment = false;
	}
	m_task_cv.notify_one ();
      }

      bool
      assign_task (void *task_p)
      {
	{
	  std::unique_lock<std::mutex> ulock (m_task_mutex);
	  if (!m_waiting_assignment)
	    {
	      return false;
	    }
	  m_task_p = task_p;
	}
	m_task_cv.notify_one ();
      }

    private:

      enum class state
      {
	FREE_FOR_REGISTER,
	REGISTERED_FOR_RUNNING,
	CLAIMING_CONTEXT,
	EXECUTING_TASKS,
	RETIRING_CONTEXT,
	DEREGISTERING,
      };

      std::thread m_thread;
      state m_state;

      std::condition_variable m_task_cv;
      std::mutex m_task_mutex;
      void *m_task_p;
      bool m_waiting_assignment;

      // timers
      time_point_type m_push_time;
      time_point_type m_register_time;
      time_point_type m_run_time;
      time_point_type m_execute_time;
      time_point_type m_retire_time;
      time_point_type m_deregister_time;
      time_point_type m_free_time;
  };

  template <typename Context>
  worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t work_queue_size,
				     context_manager_type *context_mgr, bool debug_log)
    : m_max_workers (pool_size)
    , m_worker_count (0)
    , m_work_queue (work_queue_size)
    , m_context_manager (*context_mgr)
    , m_workers (new worker[m_max_workers])
    , m_worker_dispatcher (pool_size)
    , m_sleepers (pool_size)
    , m_context_pointers (NULL)
    , m_stat_worker_count (0)
    , m_stat_task_count (0)
    , m_stat_register_time (0)
    , m_stat_start_thread_time (0)
    , m_stat_claim_context_time (0)
    , m_stat_execute_time (0)
    , m_stat_retire_context_time (0)
    , m_stat_deregister_time (0)
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

    // m_sleepers - add all threads
    for (std::size_t i = 0; i < m_max_workers; ++i)
      {
	if (!m_sleepers.produce (&m_workers[i]))
	  {
	    assert (false);
	  }
      }
  }

  template <typename Context>
  worker_pool<Context>::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);
    assert (m_work_queue.is_empty ());
    delete [] m_workers;
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
    worker *worker_p = register_worker ();
    if (worker_p != NULL)
      {
	worker_p->start_thread (*this, work_arg);
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
    worker *worker_p = register_worker ();
    if (worker_p != NULL)
      {
	worker_p->start_thread (*this, work_arg);
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
    worker *worker_p;
    std::size_t stop_count = 0;
    while (stop_count < m_max_workers && std::chrono::system_clock::now () < timeout)
      {
	worker_p = register_worker ();
	if (worker_p == NULL)
	  {
	    // in case new threads have started, tell them to stop
	    stop_all_contexts ();

	    // sleep for 10 milliseconds to give running threads a chance to finish
	    std::this_thread::sleep_for (time_spin_sleep);
	  }
	else
	  {
	    worker_p->stop_waiting ();
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

  template<typename Context>
  void
  worker_pool<Context>::get_stats (std::uint64_t *stats_out)
  {
#define TO_MILLIS(val) static_cast<std::uint64_t> ((val) / 1000000)

    stats_out[0] = static_cast<std::uint64_t> (m_stat_worker_count);
    stats_out[1] = static_cast<std::uint64_t> (m_stat_task_count);
    stats_out[2] = TO_MILLIS (m_stat_register_time);
    stats_out[3] = TO_MILLIS (m_stat_start_thread_time);
    stats_out[4] = TO_MILLIS (m_stat_claim_context_time);
    stats_out[5] = TO_MILLIS (m_stat_execute_time);
    stats_out[6] = TO_MILLIS (m_stat_retire_context_time);
    stats_out[7] = TO_MILLIS (m_stat_deregister_time);

#undef TO_MILLIS
  }

  template <typename Context>
  void
  worker_pool<Context>::run (worker_pool<Context> &pool, worker &worker_arg, task_type *task_arg)
  {
#define THREAD_WP_STATIC_LOG(msg, ...) if (pool.m_log) _er_log_debug (ARG_FILE_LINE, "run: " msg, __VA_ARGS__)

    if (!pool.is_running())
      {
	// task must not be executed if it was pushed for execution after worker pool was stopped
	task_arg->retire();

	// deregister worker
	pool.deregister_worker (worker_arg);
	return;
      }

    worker_arg.set_running ();

    // create context for task execution
    std::size_t thread_index = pool.get_thread_index (worker_arg);
    Context &context = pool.m_context_manager.create_context ();
    bool first_loop = true;

    // save to pool contexts too
    pool.m_context_pointers[thread_index] = &context;

    // loop as long as pool is running and there are tasks in queue
    task_type *task_p = task_arg;
    unsigned int task_count = 0;
    worker_arg.set_executing ();
    while (true)
      {
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

	    ++task_count;

	    // consume another task
	  }
	while (pool.is_running() && pool.m_work_queue.consume (task_p));

	// don't die just yet, wait for new tasks
	task_p = pool.set_worker_available_for_new_work (worker_arg);
	if (task_p == NULL)
	  {
	    // no new task, stop thread
	    break;
	  }
      }

    THREAD_WP_STATIC_LOG ("stop on thread = %zu, context = %p", thread_index, &context);

    pool.m_stat_worker_count++;
    pool.m_stat_task_count++;

    worker_arg.set_retiring ();

    // remove context from pool and retire
    pool.m_context_pointers[thread_index] = NULL;
    pool.m_context_manager.retire_context (context);

    // end of run; deregister worker
    pool.deregister_worker (worker_arg);

#undef THREAD_WP_STATIC_LOG
  }

  template <typename Context>
  worker *
  worker_pool<Context>::register_worker (void)
  {
    worker *worker_p;
    auto push_time = worker::clock_type::now ();

    // claim a running thread
    if (m_worker_dispatcher.consume (worker_p))
      {
	// this is best option
      }
    // claim a sleeper thread
    else if (m_sleepers.consume (worker_p))
      {
	// a thread will be spawned
      }
    else
      {
	// no threads available
	THREAD_WP_LOG ("register_worker", "worker_p = %p", NULL);
	return NULL;
      }

    worker_p->set_registered (push_time);

    ++m_worker_count;

    THREAD_WP_LOG ("register_worker", "thread = %zu", get_thread_index (*worker_p));
    return worker_p;
  }

  template <typename Context>
  task<Context> *
  worker_pool<Context>::set_worker_available_for_new_work (worker &worker_arg)
  {
    if (!m_worker_dispatcher.produce (&worker_arg))
      {
	return NULL;
      }
    worker_arg.set_free ();
    return reinterpret_cast<task<Context>*> (worker_arg.wait_for_task ());
  }

  template <typename Context>
  void
  worker_pool<Context>::deregister_worker (worker &worker_arg)
  {
    worker_arg.set_deregistering ();

    // THREAD_WP_LOG ("deregister_worker", "thread = %zu", get_thread_index (thread_arg));
    // no logging here; no thread & error context
    m_sleepers.force_produce (&worker_arg);
    --m_worker_count;

    worker_arg.set_free ();

    // get statistics
    std::chrono::nanoseconds register_time;
    std::chrono::nanoseconds start_thread_time;
    std::chrono::nanoseconds claim_context_time;
    std::chrono::nanoseconds execute_time;
    std::chrono::nanoseconds retire_context_time;
    std::chrono::nanoseconds deregister_time;

    worker_arg.get_stats (register_time, start_thread_time, claim_context_time, execute_time, retire_context_time,
			  deregister_time);
    m_stat_register_time += register_time.count ();
    m_stat_start_thread_time += start_thread_time.count ();
    m_stat_claim_context_time += claim_context_time.count ();
    m_stat_execute_time += execute_time.count ();
    m_stat_retire_context_time += retire_context_time.count ();
    m_stat_deregister_time += deregister_time.count ();
  }

  template<typename Context>
  size_t
  cubthread::worker_pool<Context>::get_thread_index (worker &thread_arg)
  {
    size_t index = &thread_arg - m_workers;
    assert (index >= 0 && index < m_max_workers);
    return index;
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  cubthread::worker_pool<Context>::map_running_contexts (Func &&func, Args &&... args)
  {
    context_type *ctx_p;
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	ctx_p = m_context_pointers[it];
	if (ctx_p != NULL)
	  {
	    func (*ctx_p, std::forward<Args> (args)...);
	  }
      }
  }

#undef THREAD_WP_LOG

} // namespace cubthread
