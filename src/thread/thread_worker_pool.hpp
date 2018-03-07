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
  // forward definitions

  // wpstat - class used to collect worker pool statistics
  class wpstat;

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
  //
  // implementation
  //
  //
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

      worker_pool (std::size_t pool_size, std::size_t work_queue_size, context_manager_type &context_mgr,
		   std::size_t core_count = 1, bool debug_logging = false);
      ~worker_pool ();

      // try to execute task; executes only if there is an available thread; otherwise returns false
      bool try_execute (task_type *work_arg);

      // execute task; execution is guaranteed, even if job queue is full. debug will crash though
      void execute (task_type *work_arg);

      // stop worker pool; stop all running threads
      void stop_execution (void);

      // is_running = is not stopped; when created, a worker pool starts running
      bool is_running (void) const;

      // is_full = work queue is full
      bool is_full (void) const;

      // get number of threads currently running
      // note: this count may change after call
      std::size_t get_max_count (void) const;

      void get_stats (wpstat &sum_inout);

      //////////////////////////////////////////////////////////////////////////
      // context management
      //////////////////////////////////////////////////////////////////////////

      // map functions over all running contexts
      //
      // WARNING:
      //    this is a dangerous functionality. please not that context retirement and mapping function is not
      //    synchronized. mapped context may be retired or in process of retirement.
      //
      //    make sure your case is handled properly
      //
      template <typename Func, typename ... Args>
      void map_running_contexts (Func &&func, Args &&... args);

    private:
      using atomic_context_ptr = std::atomic<context_type *>;

      // forward definition for core class; he's a friend
      class core;
      friend class core;

      // get next core
      core &get_next_core_for_execution (void);

      // maximum number of concurrent workers
      std::size_t m_max_workers;

      // work queue to store tasks that cannot be immediately executed
      lockfree::circular_queue<task_type *> m_work_queue;

      // thread context manager
      context_manager_type &m_context_manager;

      // core variables
      core *m_core_array;
      std::size_t m_core_count;
      std::atomic<std::size_t> m_round_robin_counter;

      // set to true when stopped
      std::atomic<bool> m_stopped;

      bool m_log;
  };

  // wpstat - class used to collect worker pool statistics
  //
  class wpstat
  {
    public:
      // types
      using stat_type = std::uint64_t;
      using clock_type = std::chrono::high_resolution_clock;
      using time_point_type = clock_type::time_point;
      using duration_type = std::chrono::nanoseconds;

      // ctor/dtor
      wpstat (stat_type *stats_p = NULL);
      ~wpstat (void);

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
      static const std::size_t TOTAL_STATS_COUNT = 2 * STATS_COUNT;  // counters + timers

      static const char *get_id_name (id statid);
      static std::size_t to_index (id &statid);
      static id to_id (std::size_t index);

      inline void collect (id statid, duration_type delta);
      inline void collect (id statid, time_point_type start, time_point_type end);
      inline void collect_and_time (id statid, time_point_type &start);

      void operator+= (const wpstat &other_stat);

    private:

      stat_type *m_counters;
      stat_type *m_timers;
      stat_type *m_own_stats;
  };

  // worker_pool<Context>::core
  //
  // description
  //    a worker pool core execution. manages a sub-group of workers.
  //    acts as middleman between worker pool and workers
  //
  template <typename Context>
  class worker_pool<Context>::core
  {
    public:
      using context_type = Context;
      using task_type = task<Context>;
      using worker_pool_type = worker_pool<Context>;

      // forward definition
      class worker;

      // ctor/dtor
      core ();
      ~core (void);

      void init (worker_pool<Context> &parent, std::size_t worker_count);

      // interface for worker pool
      // task management
      bool execute_task (task_type *task_p);
      // context management
      template <typename Func, typename ... Args>
      void map_running_contexts (Func &&func, Args &&... args);
      // worker management
      void notify_stop (void);
      void count_stopped_workers (std::size_t &count_inout);
      // statistics
      void get_stats (wpstat &sum_inout);

      // interface for workers
      // task management
      task_type *get_task (void);
      // worker management
      void add_worker_to_free_active_list (worker &worker_arg);
      void add_worker_to_inactive_list (worker &worker_arg);
      void move_worker_to_inactive_list (worker &worker_arg);
      bool is_stopped (void) const;
      // context management
      context_type &create_context (void);
      void recycle_context (context_type &context);
      void retire_context (context_type &context);

    private:

      worker_pool_type *m_parent_pool;
      std::size_t m_max_workers;
      worker *m_worker_array;
      std::list<worker *> m_free_active_list;
      std::forward_list<worker *> m_inactive_list;
      std::mutex m_workers_mutex;
  };

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
  class worker_pool<Context>::core::worker
  {
    public:
      using core_type = typename worker_pool<Context>::core;

      worker ();

      void init (core_type &parent);

      void start_execution (task<Context> *work_p, wpstat::time_point_type push_time);
      void stop_execution (void);
      template <typename Func, typename ... Args>
      void map_context (Func &&func, Args &&... args);
      void get_stats (wpstat &sum_inout);

    private:

      void run (void);
      void init_run (void);
      void finish_run (void);
      void execute_current_task (void);
      void retire_current_task (void);
      bool get_new_task (void);
      void wait_for_task (void);


      // the pool
      core_type *m_parent_core;
      Context *m_context_p;

      // assigned task
      task_type *m_task_p;

      // synchronization on task wait
      std::condition_variable m_task_cv;
      std::mutex m_task_mutex;
      bool m_waiting_task;

      // statistics
      wpstat::stat_type m_stats_array[wpstat::TOTAL_STATS_COUNT];
      wpstat m_statistics;
      wpstat::time_point_type m_push_time;
      wpstat::time_point_type m_time_point;
  };

  /************************************************************************/
  /* Template/inline implementation                                       */
  /************************************************************************/

#define THREAD_WP_LOG(func, msg, ...) if (m_log) _er_log_debug (ARG_FILE_LINE, func ": " msg "\n", __VA_ARGS__)


  //////////////////////////////////////////////////////////////////////////
  // worker_pool implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Context>
  worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t work_queue_size,
				     context_manager_type &context_mgr, std::size_t core_count, bool debug_log)
    : m_max_workers (pool_size)
    , m_work_queue (work_queue_size)
    , m_context_manager (context_mgr)
    , m_core_array (NULL)
    , m_core_count (core_count)
    , m_stopped (false)
    , m_log (debug_log)
  {
    // initialize cores; we'll try to distribute pool evenly to all cores. if core count is not fully contained in
    // pool size, some cores will have one additional worker
    m_core_array = new core[m_core_count];
    std::size_t quotient = m_max_workers / m_core_count;
    std::size_t remainder = m_max_workers % m_core_count;
    std::size_t it = 0;
    for (; it < remainder; it++)
      {
	m_core_array[it].init (*this, quotient + 1);
      }
    for (; it < m_core_count; it++)
      {
	m_core_array[it].init (*this, quotient);
      }
  }

  template <typename Context>
  worker_pool<Context>::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);
    assert (m_work_queue.is_empty ());
    delete [] m_core_array;
  }

  template <typename Context>
  bool
  worker_pool<Context>::try_execute (task_type *work_arg)
  {
    if (m_stopped)
      {
	return false;
      }
    core &core_ref = get_next_core_for_execution ();
    if (core_ref.execute_task (work_arg))
      {
	// task was pushed
	return true;
      }
    THREAD_WP_LOG ("try_execute", "drop task = %p", work_arg);
    return false;
  }

  template <typename Context>
  void
  worker_pool<Context>::execute (task_type *work_arg)
  {
    core &core_ref = get_next_core_for_execution ();
    if (core_ref.execute_task (work_arg))
      {
	// task was pushed
	return;
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

    // loop until all workers are stopped or until timeout expires
    std::size_t stop_count = 0;
    auto timeout = std::chrono::system_clock::now () + time_wait_to_thread_stop;
    while (true)
      {
	// notify all cores to stop
	for (std::size_t it = 0; it < m_core_count; it++)
	  {
	    m_core_array[it].notify_stop ();
	  }
	// verify how many have stopped
	for (std::size_t it = 0; it < m_core_count; it++)
	  {
	    m_core_array[it].count_stopped_workers (stop_count);
	  }
	if (stop_count == m_max_workers)
	  {
	    // all stopped
	    break;
	  }
	if (std::chrono::system_clock::now () > timeout)
	  {
	    // timed out
	    assert (false);
	    break;
	  }

	// sleep for a while to give running threads a chance to finish
	std::this_thread::sleep_for (time_spin_sleep);
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
  inline bool worker_pool<Context>::is_full (void) const
  {
    return m_work_queue.is_full ();
  }

  template<typename Context>
  inline std::size_t
  worker_pool<Context>::get_max_count (void) const
  {
    return m_max_workers;
  }

  template<typename Context>
  void
  worker_pool<Context>::get_stats (wpstat &sum_inout)
  {
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].get_stats (sum_inout);
      }
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  cubthread::worker_pool<Context>::map_running_contexts (Func &&func, Args &&... args)
  {
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].map_running_contexts (func, args...);
      }
  }

  template <typename Context>
  typename worker_pool<Context>::core &
  worker_pool<Context>::get_next_core_for_execution (void)
  {
    // get a core index atomically
    std::size_t index;
    std::size_t next_index;
    while (true)
      {
	index = m_round_robin_counter;
	next_index = index + 1;
	if (next_index == m_max_workers)
	  {
	    next_index = 0;
	  }
	if (m_round_robin_counter.compare_exchange_strong (index, next_index))
	  {
	    // my index is found
	    break;
	  }
      }
    return m_core_array[index % m_core_count];
  }

  //////////////////////////////////////////////////////////////////////////
  // wpstat
  //////////////////////////////////////////////////////////////////////////

  void
  wpstat::collect (id statid, duration_type delta)
  {
    size_t index = to_index (statid);
    m_counters[index]++;
    m_timers[index] += delta.count ();
  }

  void
  wpstat::collect (id statid, time_point_type start, time_point_type end)
  {
    collect (statid, end - start);
  }

  void
  wpstat::collect_and_time (id statid, time_point_type &start)
  {
    time_point_type nowpt = clock_type::now ();
    collect (statid, nowpt - start);
    start = nowpt;
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool::core
  //////////////////////////////////////////////////////////////////////////

  template <typename Context>
  worker_pool<Context>::core::core ()
    : m_parent_pool (NULL)
    , m_max_workers (0)
    , m_worker_array (NULL)
    , m_free_active_list ()
    , m_inactive_list ()
    , m_workers_mutex ()
  {
    //
  }

  template <typename Context>
  worker_pool<Context>::core::~core ()
  {
    delete [] m_worker_array;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::init (worker_pool<Context> &parent, std::size_t worker_count)
  {
    m_parent_pool = &parent;
    m_max_workers = worker_count;

    // allocate workers array
    m_worker_array = new worker[m_max_workers];

    // all workers are inactive
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_inactive_list.push_front (&m_worker_array[it]);
      }
  }

  template <typename Context>
  task<Context> *
  worker_pool<Context>::core::get_task (void)
  {
    task_type *task_p = NULL;
    if (m_parent_pool->m_work_queue.consume (task_p))
      {
	return task_p;
      }
    return NULL;
  }

  template <typename Context>
  bool
  worker_pool<Context>::core::execute_task (task_type *task_p)
  {
    wpstat::time_point_type push_time;
    worker *refp = NULL;
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    if (!m_free_active_list.empty ())
      {
	refp = m_free_active_list.front ();
	m_free_active_list.pop_front ();
      }
    else if (!m_inactive_list.empty ())
      {
	refp = m_inactive_list.front ();
	m_inactive_list.pop_front ();
      }
    ulock.unlock ();

    if (refp == NULL)
      {
	// not found
	return false;
      }

    refp->start_execution (task_p, push_time);
    return true;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::add_worker_to_free_active_list (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    m_free_active_list.push_back (&worker_arg);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::add_worker_to_inactive_list (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    m_inactive_list.push_front (&worker_arg);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::move_worker_to_inactive_list (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    m_free_active_list.remove (&worker_arg);
    m_inactive_list.push_front (&worker_arg);
  }

  template <typename Context>
  bool
  worker_pool<Context>::core::is_stopped (void) const
  {
    return m_parent_pool->m_stopped;
  }

  template <typename Context>
  typename Context &
  worker_pool<Context>::core::create_context (void)
  {
    return m_parent_pool->m_context_manager.create_context ();
  }

  template <typename Context>
  void
  worker_pool<Context>::core::recycle_context (context_type &context)
  {
    m_parent_pool->m_context_manager.recycle_context (context);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::retire_context (context_type &context)
  {
    m_parent_pool->m_context_manager.retire_context (context);
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  cubthread::worker_pool<Context>::core::map_running_contexts (Func &&func, Args &&... args)
  {
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].map_context (func, args...);
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::notify_stop (void)
  {
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].stop_execution ();
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::count_stopped_workers (std::size_t &count_inout)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    // claim as many workers as possible
    while (!m_free_active_list.empty ())
      {
	m_free_active_list.pop_front ();
	++count_inout;
      }
    while (!m_inactive_list.empty ())
      {
	m_inactive_list.pop_front ();
	++count_inout;
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::get_stats (wpstat &sum_inout)
  {
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].get_stats (sum_inout);
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool<Context>::core::worker
  //////////////////////////////////////////////////////////////////////////

  template <typename Context>
  worker_pool<Context>::core::worker::worker ()
    : m_parent_core (NULL)
    , m_context_p (NULL)
    , m_task_p (NULL)
    , m_task_cv ()
    , m_task_mutex ()
    , m_waiting_task (false)
    , m_stats_array {0}
    , m_statistics (reinterpret_cast<wpstat::stat_type *> (&m_stats_array))
    , m_push_time ()
    , m_time_point ()
  {
    //
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::init (core_type &parent)
  {
    m_parent_core = &parent;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::start_execution (task<Context> *work_p, wpstat::time_point_type push_time)
  {
    // start task execution. we have two options:
    //
    //  1. thread is already running and waiting for a new task notification. this case is faster.
    //  2. thread is not running or waiting for a new task notification. we must start a new one (thread start has
    //     a significant overhead)
    //

    // save push time
    m_push_time = push_time;

    // is thread started and waiting for task?
    if (m_waiting_task)
      {
	// lock task mutex
	std::unique_lock<std::mutex> ulock (m_task_mutex);
	if (m_waiting_task)
	  {
	    // still waiting. save task
	    m_task_p = work_p;

	    // unlock, notify thread and get out
	    ulock.unlock ();
	    m_task_cv.notify_one ();
	    return;
	  }
      }

    // a new thread is required
    std::thread (&worker::run, this).detach ();    // don't wait for it
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::stop_execution (void)
  {
    context_type *context_p = m_context_p;
    if (context_p != NULL)
      {
	// notify context to stop
	context_p->interrupt_execution ();
      }

    // make sure thread is not waiting for tasks
    std::unique_lock<std::mutex> ulock (m_task_mutex);
    if (!m_waiting_task)
      {
	return;
      }
    m_waiting_task = false;
    ulock.unlock ();    // mutex is not needed for notify
    m_task_cv.notify_one ();
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::init_run (void)
  {
    m_time_point = m_push_time;
    m_statistics.collect_and_time (wpstat::id::START_THREAD, m_time_point);

    // a context is required
    m_context_p = &m_parent_core->create_context ();
    m_statistics.collect_and_time (wpstat::id::CREATE_CONTEXT, m_time_point);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::finish_run (void)
  {
    // retire context
    m_parent_core->retire_context (*m_context_p);
    m_context_p = NULL;
    m_statistics.collect_and_time (wpstat::id::RETIRE_CONTEXT, m_time_point);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::retire_current_task (void)
  {
    // retire task
    m_task_p->retire ();
    m_task_p = NULL;
    m_statistics.collect_and_time (wpstat::id::RETIRE_TASK, m_time_point);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::execute_current_task (void)
  {
    // execute task
    m_task_p->execute (*m_context_p);
    m_statistics.collect_and_time (wpstat::id::EXECUTE_TASK, m_time_point);

    // and retire task
    retire_current_task ();
  }

  template <typename Context>
  bool
  worker_pool<Context>::core::worker::get_new_task (void)
  {
    assert (m_task_p == NULL);

    // check stop condition
    if (m_parent_core->is_stopped ())
      {
	// add to inactive and stop
	m_parent_core->add_worker_to_inactive_list (*this);

	return false;
      }

    // first try to get task from queue
    m_task_p = m_parent_core->get_task ();
    m_statistics.collect_and_time (wpstat::id::SEARCH_TASK_IN_QUEUE, m_time_point);

    if (m_task_p == NULL)
      {
	// no task in queue. maybe a new one will be available soon
	wait_for_task ();
      }

    if (m_task_p != NULL)
      {
	// a task will be executed; recycle context
	m_parent_core->recycle_context (*m_context_p);
	return true;
      }

    // task was not found
    // worker should have been added to inactive list already
    return false;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::wait_for_task (void)
  {
    // notify parent core I am free
    m_parent_core->add_worker_to_free_active_list (*this);

    // wait for task
    std::unique_lock<std::mutex> ulock (m_task_mutex);
    m_waiting_task = true;
    m_task_cv.wait_for (ulock, std::chrono::seconds (60),
			[this] { return m_waiting_task && m_task_p != NULL; });
    m_waiting_task = false;

    if (m_task_p == NULL)
      {
	// no task assigned; move from active list to inactive

	// while holding the task mutex, I must move to inactive list. that is required not to mess up the lists and
	// thread status when a task is pushed right before becoming inactive
	m_parent_core->move_worker_to_inactive_list (*this);
	ulock.unlock ();

	m_time_point = wpstat::clock_type::now ();
      }
    else
      {
	// found task
	ulock.unlock ();

	m_time_point = m_push_time;
	m_statistics.collect_and_time (wpstat::id::WAKEUP_WITH_TASK, m_time_point);
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::run (void)
  {
    init_run ();

    // loop and execute as many tasks as possible
    do
      {
	execute_current_task ();
      }
    while (get_new_task ());

    finish_run ();
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::get_stats (wpstat &sum_inout)
  {
    sum_inout += m_statistics;
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  worker_pool<Context>::core::worker::map_context (Func &&func, Args &&... args)
  {
    Context *ctxp = m_context_p;
    if (ctxp != NULL)
      {
	func (*ctxp, args...);
      }
  }

#undef THREAD_WP_LOG

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_HPP_
