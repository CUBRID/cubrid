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
#include <condition_variable>
#include <forward_list>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include <cassert>

namespace cubthread
{
  // wpstat - namespace for worker pool statistics
  //
  namespace wpstat
  {
    using stat_type = std::uint64_t;
    using clock_type = std::chrono::high_resolution_clock;
    using time_point_type = clock_type::time_point;
    using duration_type = std::chrono::nanoseconds;

    enum class id
    {
      GET_WORKER_FROM_ACTIVE_QUEUE,
      GET_WORKER_FROM_INACTIVE_QUEUE,
      GET_WORKER_FAILED,
      START_THREAD,
      CREATE_CONTEXT,
      EXECUTE_TASK,
      RETIRE_TASK,
      SEARCH_TASK_IN_QUEUE,
      WAKEUP_WITH_TASK,
      RETIRE_CONTEXT,
      DEACTIVATE_WORKER,
      COUNT
    };
    static const std::size_t STATS_COUNT = static_cast<size_t> (id::COUNT);

    inline const char *
    get_id_name (id statid)
    {
      switch (statid)
	{
	case id::GET_WORKER_FROM_ACTIVE_QUEUE:
	  return "GET_WORKER_FROM_ACTIVE_QUEUE";
	case id::GET_WORKER_FROM_INACTIVE_QUEUE:
	  return "GET_WORKER_FROM_INACTIVE_QUEUE";
	case id::GET_WORKER_FAILED:
	  return "GET_WORKER_FAILED";
	case id::START_THREAD:
	  return "START_THREAD";
	case id::CREATE_CONTEXT:
	  return "CREATE_CONTEXT";
	case id::EXECUTE_TASK:
	  return "EXECUTE_TASK";
	case id::RETIRE_TASK:
	  return "RETIRE_TASK";
	case id::SEARCH_TASK_IN_QUEUE:
	  return "SEARCH_TASK_IN_QUEUE";
	case id::WAKEUP_WITH_TASK:
	  return "WAKEUP_WITH_TASK";
	case id::RETIRE_CONTEXT:
	  return "RETIRE_CONTEXT";
	case id::DEACTIVATE_WORKER:
	  return "DEACTIVATE_WORKER";
	case id::COUNT:
	default:
	  assert (false);
	  return "UNKNOW";
	}
    }

    inline std::size_t
    to_index (id &statid)
    {
      return static_cast<size_t> (statid);
    }

    inline id
    to_id (std::size_t index)
    {
      assert (index >= 0 && index < STATS_COUNT);
      return static_cast<id> (index);
    }

    // statistics collector
    struct collector
    {
      stat_type m_counters[STATS_COUNT];
      stat_type m_timers[STATS_COUNT];

      collector (void)
	: m_counters { 0 }
	, m_timers { 0 }
      {
	//
      }

      inline void
      add (id statid, duration_type delta)
      {
	size_t index = to_index (statid);
	m_counters[i]++;
	m_timers[i] += delta.count ();
      }

      inline void
      add (id statid, time_point_type start, time_point_type end)
      {
	add (statid, end - start);
      }

      inline void
      add (const collector &col_arg)
      {
	for (std::size_t it = 0; it < STATS_COUNT; it++)
	  {
	    m_counters[it] += col_arg.m_counters[it];
	    m_timers[it] += col_arg.m_timers[it];
	  }
      }

      inline void
      register_now (id statid, time_point_type &start)
      {
	time_point_type nowpt = clock_type::now ();
	add (statid, nowpt - start);
	start = nowpt;
      }
    };

  } // namespace wpstat

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

      void get_stats (wpstat::collector &sum_out);

      //////////////////////////////////////////////////////////////////////////
      // context management
      //////////////////////////////////////////////////////////////////////////

      template <typename Func, typename ... Args>
      void map_running_contexts (Func &&func, Args &&... args);

    private:
      using atomic_context_ptr = std::atomic<context_type *>;

      // forward definition for worker class
      // only visible to worker pool
      // see more details on class definition
      class worker;

      // register a new worker; return NULL if no worker is available
      worker *register_worker (void);

      task_type *set_worker_available_for_new_work (worker &thread_arg);

      // deregister worker when it stops running
      void deactivate_worker (worker &thread_arg);

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
      // m_active_workers - worker is active (has running thread) and waits for new task
      std::list<worker &> m_active_workers;
      std::forward_list<worker &> m_inactive_workers;
      std::mutex m_workers_mutex;

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
  // worker_pool<Context>::worker
  //
  // description
  //    the worker is a worker pool nested class and represents one instance of execution. its purpose is to store the
  //    context, manage multiple task executions of a single thread and collect statistics.
  //
  // how it works
  //    the worker is assigned a task and a new thread is started. when task is finished, the worker tries to execute
  //    more tasks, either by consuming one from task queue or by waiting for one. if it waits too long and it is given
  //    no task, the thread stops
  //
  //    there are two types of workers in regard with the thread status:
  //
  //      1. inactive worker (initial state), thread is not running and must be started before executing task
  //      2. active worker, either executing task or waiting for a new task.
  //
  //    there are three ways task is executed:
  //
  //      1. by an inactive worker; it goes through next phases:
  //          - claiming from inactive list of workers
  //          - starting thread
  //          - claiming context
  //          - executing task
  //
  //      2. by an active worker (thread is running); it goes through next phases:
  //          - claiming from active list of workers
  //          - notifying and waking thread
  //          - executing task
  //
  //      3. by being claimed from task queue; if no worker (active or inactive) is available, task is added to queue
  //         to be executed when a worker finishes its current task; it goes through next phases
  //          - adding task to queue
  //          - claiming task from queue
  //          - executing task
  //
  // note
  //    class is nested to worker pool and cannot be used outside it
  //
  template <typename Context>
  class worker_pool<Context>::worker
  {
    public:
      worker (worker_pool<Context> &pool)
	: m_pool_ref (pool)
	, m_context_p (NULL)
	, m_task_p (NULL)
	, m_task_cv ()
	, m_task_mutex ()
	, m_waiting_task (false)
	, m_statistics ()
      {
	//
      }

      void execute (task<Context> *work_p, wpstat::time_point_type push_time)
      {
	m_push_time = push_time;
	if (assign_task (work_p))
	  {
	    // assigned directly
	  }
	else
	  {
	    // need to start thread
	    m_task_p = work_p;
	    std::thread (worker::run, this).detach ();  // don't block
	  }
      }

      void
      run ()
      {
	// register start thread timer
	wpstat::time_point_type timept = m_push_time;
	m_statistics.register_now (wpstat::id::START_THREAD, timept);

	// get task
	task<Context> *task_p = m_task_p->release ();


	bool found_task = false;

	// claim a context
	m_context_p = &m_pool_ref.m_context_manager.create_context ();
	m_statistics.register_now (wpstat::id::CREATE_CONTEXT, timept);

	// loop and execute as many tasks as possible
	while (true)
	  {
	    // check if pool was stopped meanwhile
	    if (!m_pool_ref.is_running ())
	      {
		// task must not be executed if it was pushed for execution after worker pool was stopped
		task_p->retire ();
		m_statistics.register_now (wpstat::id::RETIRE_TASK, timept);

		// stop
		break;
	      }

	    // execute current task
	    task_p->execute ();
	    m_statistics.register_now (wpstat::id::EXECUTE_TASK, timept);
	    // and retire it
	    task_p->retire ();
	    m_statistics.register_now (wpstat::id::RETIRE_TASK, timept);

	    // try to get new task from queue
	    found_task = m_pool_ref.m_work_queue.consume (task_p);
	    m_statistics.register_now (wpstat::id::SEARCH_TASK_IN_QUEUE, timept);
	    if (found_task)
	      {
		// execute task from queue
		continue;
	      }

	    // try to wait for a task, it may come soon
	    task_p = wait_for_task ();
	    timept = wpstat::clock_type::now ();
	    if (task_p == NULL)
	      {
		// no task found; stop
		break;
	      }
	  }

	// retire context; we must first reset m_context_p
	Context &ctx_ref = *m_context_p;
	m_context_p = NULL;
	m_pool_ref.m_context_manager.retire_context (ctx_ref);
	m_statistics.register_now (wpstat::id::RETIRE_CONTEXT, timept);

	// worker becomes inactive
	m_pool_ref.deactivate_worker (*this);
	m_statistics.add (wpstat::id::DEACTIVATE_WORKER, timept, wpstat::clock_type::now ());
      }

      task<Context> *
      wait_for_task (wpstat::time_point_type &timept_inout)
      {
	std::unique_lock<std::mutex> ulock (m_task_mutex);
	m_waiting_task = true;
	m_task_cv.wait_for (ulock, std::chrono::seconds (60),
			    [this] { return m_waiting_task && m_task_p != NULL; });
	m_waiting_task = false;

	task<Context> *ret_p = m_task_p;
	m_task_p = NULL;

	timept_inout = wpstat::clock_type::now ();
	if (ret_p != NULL)
	  {
	    m_statistics.add (wpstat::id::WAKEUP_WITH_TASK, m_push_time, timept_inout);
	  }
	return ret_p;
      }

      void
      stop_waiting (void)
      {
	{
	  std::unique_lock<std::mutex> ulock (m_task_mutex);
	  if (!m_waiting_task)
	    {
	      return;
	    }
	  m_waiting_task = false;
	}
	m_task_cv.notify_one ();
      }

      bool
      assign_task (void *task_p)
      {
	{
	  std::unique_lock<std::mutex> ulock (m_task_mutex);
	  if (!m_waiting_task)
	    {
	      return false;
	    }
	  if (m_task_p != NULL)
	    {
	      abort ();
	    }
	  m_task_p = task_p;
	}
	m_task_cv.notify_one ();
	return true;
      }

    private:

      // the pool
      worker_pool<Context> &m_pool_ref;
      Context *m_context_p;

      // assigned task
      std::unique_ptr<task<Context>> m_task_p;

      // synchronization on task wait
      std::condition_variable m_task_cv;
      std::mutex m_task_mutex;
      bool m_waiting_task;
      wpstat::time_point_type m_push_time;

      // statistics
      wpstat::collector m_statistics;
  };

  template <typename Context>
  worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t work_queue_size,
				     context_manager_type *context_mgr, bool debug_log)
    : m_max_workers (pool_size)
    , m_worker_count (0)
    , m_work_queue (work_queue_size)
    , m_context_manager (*context_mgr)
    , m_workers (new worker[m_max_workers])
    , m_active_workers (pool_size)
    , m_inactive_workers (pool_size)
    , m_workers_mutex ()
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

    // m_sleepers - add all threads
    for (std::size_t i = 0; i < m_max_workers; ++i)
      {
	if (!m_inactive_workers.produce (&m_workers[i]))
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
	worker_p->execute (*this, work_arg);
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
	worker_p->execute (*this, work_arg);
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
  worker_pool<Context>::get_stats (wpstat::collector &sum_out)
  {
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	sum_out.add (m_workers->m_statistics);
      }
  }

  template <typename Context>
  typename worker_pool<Context>::worker *
  worker_pool<Context>::register_worker (void)
  {
    worker *worker_p;
    auto push_time = worker::clock_type::now ();

    // claim a running thread
    if (m_active_workers.consume (worker_p))
      {
	// this is best option
      }
    // claim a sleeper thread
    else if (m_inactive_workers.consume (worker_p))
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
    if (!m_active_workers.produce (&worker_arg))
      {
	return NULL;
      }
    worker_arg.set_free ();
    return reinterpret_cast<task<Context>*> (worker_arg.wait_for_task ());
  }

  template <typename Context>
  void
  worker_pool<Context>::deactivate_worker (worker &worker_arg)
  {
    m_inactive_workers.force_produce (&worker_arg);
    --m_worker_count;
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
