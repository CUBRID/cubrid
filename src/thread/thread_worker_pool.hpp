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
#include <queue>
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
  //    // note that worker_pool must be specialized with a thread context
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
  //    the worker pool can be partitioned into cores - a middle layer above a group of workers. this is an
  //    optimization for high-contention systems and only one core can be set if that's not the case.
  //
  //    core manages its workers and tracks available resource - free active workers and inactive workers.
  //
  //    workers start by being inactive (does not have a thread running). it starts a thread with its first task,
  //    becoming active, and stays active as long as it finds more tasks to execute.
  //
  //    This is how a new task is processed:
  //
  //      1. Worker pool assigns the task to a core via round robin method. The core can then accept the task (if an
  //         available worker is found) or reject it. If task is rejected, then it is stored in a queue to be processed
  //         later
  //
  //      2. Core checks if a thread is available
  //          - first checks free active list
  //          - if no free active worker is found, it checks inactive list
  //          - if a worker is found, then it is assigned the task
  //          - if no worker is found, task is rejected
  //
  //      3. Task is executed by worker in one of three ways:
  //          1 worker was inactive and starts a new thread to execute the task
  //            after it finishes its first task, it tries to find new ones:
  //            2 first checks worker pool queue for any rejected tasks
  //            3 if no task is found, it notifies parent core of its status (free and active) and waits for new task.
  //          Worker stops if waiting for new task times out (and becomes inactive).
  //
  //    NOTE: core class is private nested to worker pool and cannot be instantiated outside it.
  //          also worker class is private nested to core class.
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

      worker_pool (std::size_t pool_size, std::size_t task_max_count, context_manager_type &context_mgr,
		   std::size_t core_count = 1, bool debug_logging = false);
      ~worker_pool ();

      // try to execute task; executes only if there is an available thread; otherwise returns false
      // note: when there are multiple cores, only one core is checked. if first try fails, try_execute also fails and
      //       no other core is checked
      bool try_execute (task_type *work_arg);

      // execute task; execution is guaranteed, even if job queue is full. debug will crash though
      // read implementation in class comment for details
      void execute (task_type *work_arg);
      // execute on give core. real core index is core_hash module core count.
      // note: regular execute chooses a core through round robin scheduling. this may not be a good fit for all
      //       execution patterns.
      //       execute_on_core provides control on core scheduling.
      void execute_on_core (task_type *work_arg, std::size_t core_hash);

      // stop worker pool; stop all running threads; discard any tasks in queue
      void stop_execution (void);

      // is_running = is not stopped; when created, a worker pool starts running.
      // worker is stopped after stop_execution () is called
      bool is_running (void) const;

      // is_full = work queue is full
      bool is_full (void) const;

      // get maximum number of threads that can run concurrently in this worker pool
      std::size_t get_max_count (void) const;

      // get worker pool statistics
      // note: the statistics are collected from all cores and all their workers adding up all local statistics
      void get_stats (wpstat &sum_inout);

      //////////////////////////////////////////////////////////////////////////
      // context management
      //////////////////////////////////////////////////////////////////////////

      // map functions over all running contexts
      //
      // WARNING:
      //    this is a dangerous functionality. please note that context retirement and mapping function is not
      //    synchronized. mapped context may be retired or in process of retirement.
      //
      //    make sure your case is handled properly
      //
      template <typename Func, typename ... Args>
      void map_running_contexts (Func &&func, Args &&... args);

    private:
      using atomic_context_ptr = std::atomic<context_type *>;

      // forward definition for nested core class; he's a friend
      class core;
      friend class core;

      // get next core
      std::size_t get_next_core_for_execution (void);

      // maximum number of concurrent workers
      std::size_t m_max_workers;

      // work queue to store tasks that cannot be immediately executed
      std::size_t m_task_max_count;
      std::atomic<std::size_t> m_task_count;

      // thread context manager
      context_manager_type &m_context_manager;

      // core variables
      core *m_core_array;                                   // all cores
      std::size_t m_core_count;                             // core count
      std::atomic<std::size_t> m_round_robin_counter;       // round robin counter used to dispatch tasks on cores

      // set to true when stopped
      std::atomic<bool> m_stopped;

      // true to do debug logging
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

      // statistic ID's - each for a different step of worker lifetime and task execution
      // for each ID, a counter and a timer may be stored
      enum class id
      {
	START_THREAD,
	CREATE_CONTEXT,
	EXECUTE_TASK,
	RETIRE_TASK,
	SEARCH_TASK_IN_QUEUE,
	WAKEUP_WITH_TASK,
	RETIRE_CONTEXT,
	COUNT
      };
      static const std::size_t STATS_COUNT = static_cast<size_t> (id::COUNT);
      static const std::size_t TOTAL_STATS_COUNT = 2 * STATS_COUNT;  // counters + timers

      // helper functions
      static inline const char *get_id_name (id statid);
      static inline std::size_t to_index (id &statid);
      static inline id to_id (std::size_t index);

      // collect functions
      // counter++, timer += delta
      inline void collect (id statid, duration_type delta);
      // counter++, timer += end - start
      inline void collect (id statid, time_point_type start, time_point_type end);
      // collect_and_timer - incremental collection that resets start time point on each call
      // counter++, timer += now () - start, start = now ()
      inline void collect_and_time (id statid, time_point_type &start);

      // accumulate other_stats
      void operator+= (const wpstat &other_stat);

    private:

      stat_type *m_counters;    // counters
      stat_type *m_timers;      // timers
      stat_type *m_own_stats;   // statistics buffer allocated when constructor doesn't receive a buffer
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

      // forward definition of nested class worker
      class worker;

      // ctor/dtor
      core ();
      ~core (void);

      // init function
      void init_pool_and_workers (worker_pool<Context> &parent, std::size_t worker_count);

      // interface for worker pool
      // task management
      // execute task; returns true if task is accepted, false if it is rejected (no available workers)
      void execute_task (task_type *task_p);
      // context management
      // map function to all workers (and their contexts)
      template <typename Func, typename ... Args>
      void map_running_contexts (Func &&func, Args &&... args);
      // worker management
      // notify workers to stop
      void notify_stop (void);
      // count the workers that stopped (by claiming inactive workers)
      void count_stopped_workers (std::size_t &count_inout);
      void retire_queued_tasks (void);
      // statistics
      void get_stats (wpstat &sum_inout);

      // interface for workers
      // task management
      void finished_task_notification (void);
      task_type *get_task (void);
      // worker management
      // get a task or add worker to free active list (still running, but ready to execute another task)
      task_type *get_task_or_add_to_free_active_list (worker &worker_arg);
      // get a task or add worker to inactive list (no longer running)
      task_type *get_task_or_add_to_inactive_list (worker &worker_arg);
      // remove worker from active list (when waiting for task times out).
      // return true if worker was found, false otherwise
      bool remove_worker_from_active_list (worker &worker_arg);
      // context management
      // pass request from worker to worker pool context manager to create, recycle, retire contexts
      context_type &create_context (void);
      void recycle_context (context_type &context);
      void retire_context (context_type &context);

    private:

      worker_pool_type *m_parent_pool;                // pointer to parent pool
      std::size_t m_max_workers;                      // maximum number of workers running at once
      worker *m_worker_array;                         // all core workers
      std::list<worker *> m_free_active_list;         // list of active and available workers
      std::forward_list<worker *> m_inactive_list;    // list of inactive workers (are also available)
      std::queue<task_type *> m_task_queue;           // list of tasks pushed while all workers were occupied
      std::mutex m_workers_mutex;                     // mutex to synchronize activity on worker lists
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
  //    a more sensible part of task execution is pushing task to running thread, due to limit cases. there are up to
  //    three threads implicated into this process:
  //
  //      1. task pusher
  //          - whenever worker is claimed from free active list, the task is directly assigned (m_task_p is set)!
  //            (as a consequence, waiting thread cannot reject the task)
  //
  //      2. worker pool stopper
  //          - thread requests worker pool to stop. the signal is passed to all cores and workers; including workers
  //            that are waiting for tasks. that is signaled using m_stop field
  //
  //      3. task waiter
  //         the thread "lingers" waiting for new arriving tasks. the worker first adds itself to core's free active
  //         list and then waits until one of next conditions happen
  //          - task is assigned (m_task_p is not nil). it executes the task (regardless of m_stop).
  //          - thread is stopped (m_stop is true).
  //          - wait times out
  //         after waking up, if no task was assigned up to this point, the thread will attempt to remove its worker
  //         from free active list. a task may yet be assigned until the worker is removed from this list; if worker is
  //         not found to be removed, it means the worker was claimed and a task was pushed or is being pushed. thread
  //         is forced to wait for assigned task.
  //         if worker is removed successfully from free active list, its thread will stop and it will be added to
  //         inactive list.
  //         note that after wake up, m_stop does not affect the course of action in any way, only m_task_p. In most
  //         cases, m_stop being true will be equivalent to m_task_p being nil. in the very limited case when m_stop is
  //         true and a task is also assigned, we let the worker execute the task.
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

      // init
      void init_core (core_type &parent);

      // start task execution on a new thread (push_time is provided by core)
      void push_task_on_new_thread (task<Context> *work_p, wpstat::time_point_type push_time);
      // run task on current thread (push_time is provided by core)
      void push_task_on_running_thread (task<Context> *work_p, wpstat::time_point_type push_time);
      // stop execution
      void stop_execution (void);
      // map function to context (if context is available)
      template <typename Func, typename ... Args>
      void map_context (Func &&func, Args &&... args);
      // add own stats to given argument
      void get_stats (wpstat &sum_inout);

    private:

      // run function invoked by spawned thread
      void run (void);
      // run initialization (creating execution context)
      void init_run (void);
      // finishing initialization (retiring execution context, worker becomes inactive)
      void finish_run (void);
      // execute m_task_p
      void execute_current_task (void);
      // retire m_task_p
      void retire_current_task (void);
      // get new task from 1. worker pool task queue or 2. wait for incoming tasks
      bool get_new_task (void);

      core_type *m_parent_core;               // parent core
      Context *m_context_p;                   // execution context (same lifetime as spawned thread)

      task_type *m_task_p;                    // current task

      // synchronization on task wait
      std::condition_variable m_task_cv;      // condition variable used to notify when a task is assigned or when
      // worker is stopped
      std::mutex m_task_mutex;                // mutex to protect waiting task condition
      bool m_stop;                            // stop execution (set to true when worker pool is stopped)

      // statistics
      wpstat::stat_type m_stats_array[wpstat::TOTAL_STATS_COUNT];   // buffer to store statistics
      wpstat m_statistics;                                          // statistic collector
      wpstat::time_point_type m_push_time;                          // push time point (provided by core)
      wpstat::time_point_type m_time_point;                         // current time point
  };

  // system_core_count - return system core counts or 1 (if system core count cannot be obtained).
  //
  // use it as core count if the task execution must be highly tuned.
  // does not return 0
  std::size_t system_core_count (void);

  /************************************************************************/
  /* Template/inline implementation                                       */
  /************************************************************************/

#define THREAD_WP_LOG(func, msg, ...) if (m_log) _er_log_debug (ARG_FILE_LINE, func ": " msg "\n", __VA_ARGS__)

  //////////////////////////////////////////////////////////////////////////
  // worker_pool implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Context>
  worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t task_max_count,
				     context_manager_type &context_mgr, std::size_t core_count, bool debug_log)
    : m_max_workers (pool_size)
    , m_task_max_count (task_max_count)
    , m_task_count (0)
    , m_context_manager (context_mgr)
    , m_core_array (NULL)
    , m_core_count (core_count)
    , m_round_robin_counter (0)
    , m_stopped (false)
    , m_log (debug_log)
  {
    // initialize cores; we'll try to distribute pool evenly to all cores. if core count is not fully contained in
    // pool size, some cores will have one additional worker

    if (m_core_count == 0)
      {
	assert (false);
	m_core_count = 1;
      }

    if (m_core_count > pool_size)
      {
	m_core_count = pool_size;
      }

    m_core_array = new core[m_core_count];

    std::size_t quotient = m_max_workers / m_core_count;
    std::size_t remainder = m_max_workers % m_core_count;
    std::size_t it = 0;

    for (; it < remainder; it++)
      {
	m_core_array[it].init_pool_and_workers (*this, quotient + 1);
      }
    for (; it < m_core_count; it++)
      {
	m_core_array[it].init_pool_and_workers (*this, quotient);
      }
  }

  template <typename Context>
  worker_pool<Context>::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);

    delete [] m_core_array;
    m_core_array = NULL;
  }

  template <typename Context>
  bool
  worker_pool<Context>::try_execute (task_type *work_arg)
  {
    if (is_full ())
      {
	return false;
      }

    execute (work_arg);
    return true;
  }

  template <typename Context>
  void
  worker_pool<Context>::execute (task_type *work_arg)
  {
    execute_on_core (work_arg, get_next_core_for_execution ());
  }

  template <typename Context>
  void
  worker_pool<Context>::execute_on_core (task_type *work_arg, std::size_t core_hash)
  {
    // increment task count
    ++m_task_count;

    std::size_t core_index = core_hash % m_core_count;
    m_core_array[core_index].execute_task (work_arg);
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
    const std::chrono::milliseconds time_spin_sleep (10);       // sleep between spins for 10 milliseconds

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
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].retire_queued_tasks ();
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
  worker_pool<Context>::is_full (void) const
  {
    return m_task_count >= m_task_max_count;
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
  std::size_t
  worker_pool<Context>::get_next_core_for_execution (void)
  {
    // cores are not necessarily equal, so we try to preserve the assignments proportional to their size.
    // if the worker pool size is 15 and there are four cores, three of them will have four workers and one only three.
    // task are dispatched in this order:
    //
    // core 1  |  core 2  |  core 3  |  core 4
    //      1  |       2  |       3  |       4
    //      5  |       6  |       7  |       8
    //      9  |      10  |      11  |      12
    //     13  |      14  |      15                   // last one is skipped this round to keep proportions
    //     16  |      17  |      18  |      19
    //  ...
    //

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

    return index;
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
    start = nowpt;    // reset time point
  }

  std::size_t
  wpstat::to_index (id &statid)
  {
    return static_cast<size_t> (statid);
  }

  wpstat::id
  wpstat::to_id (std::size_t index)
  {
    assert (index < STATS_COUNT);

    return static_cast<id> (index);
  }

  const char *
  wpstat::get_id_name (id statid)
  {
    switch (statid)
      {
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
      case id::COUNT:
      default:
	assert (false);
	return "UNKNOW";
      }
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
    , m_task_queue ()
    , m_workers_mutex ()
  {
    //
  }

  template <typename Context>
  worker_pool<Context>::core::~core ()
  {
    delete [] m_worker_array;
    m_worker_array = NULL;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::init_pool_and_workers (worker_pool<Context> &parent, std::size_t worker_count)
  {
    assert (worker_count > 0);

    m_parent_pool = &parent;
    m_max_workers = worker_count;

    // allocate workers array
    m_worker_array = new worker[m_max_workers];

    // all workers are inactive
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].init_core (*this);
	m_inactive_list.push_front (&m_worker_array[it]);
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::finished_task_notification (void)
  {
    // decrement task count
    -- (m_parent_pool->m_task_count);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::execute_task (task_type *task_p)
  {
    assert (task_p != NULL);

    // find an available worker
    // 1. one already active is preferable
    // 2. inactive will do too
    // 3. if no workers, reject task (returns false)

    wpstat::time_point_type push_time = wpstat::clock_type::now ();
    worker *refp = NULL;

    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (m_parent_pool->m_stopped)
      {
	// reject task
	task_p->retire ();
	return;
      }

    if (!m_free_active_list.empty ())
      {
	// found free active; remove from list
	refp = m_free_active_list.front ();
	m_free_active_list.pop_front ();

	ulock.unlock ();

	// push task on current worker
	refp->push_task_on_running_thread (task_p, push_time);
	return;
      }
    if (!m_inactive_list.empty ())
      {
	// found free inactive; remove from list
	refp = m_inactive_list.front ();
	m_inactive_list.pop_front ();

	ulock.unlock ();

	// start new thread
	refp->push_task_on_new_thread (task_p, push_time);
	return; // worker found
      }

    // save to queue
    m_task_queue.push (task_p);
  }

  template <typename Context>
  typename worker_pool<Context>::core::task_type *
  worker_pool<Context>::core::get_task_or_add_to_free_active_list (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_task_queue.empty ())
      {
	task_type *task_p = m_task_queue.front ();
	assert (task_p != NULL);
	m_task_queue.pop ();
	return task_p;
      }

    m_free_active_list.push_back (&worker_arg);

    assert (m_free_active_list.size () <= m_max_workers);
    return NULL;
  }

  template <typename Context>
  typename worker_pool<Context>::core::task_type *
  worker_pool<Context>::core::get_task_or_add_to_inactive_list (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_task_queue.empty ())
      {
	// start new task
	task_type *task_p = m_task_queue.front ();
	m_task_queue.pop ();

	ulock.unlock ();

	return task_p;
      }

    m_inactive_list.push_front (&worker_arg);

    // assert (m_inactive_list.size () <= m_max_workers);  // forward_list has no size (); must be counted manually
    return NULL;
  }

  template <typename Context>
  bool
  worker_pool<Context>::core::remove_worker_from_active_list (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    // we need to remove from active. this operation is slow. however, since we're here, it means thread timed out
    // waiting for task. system is not overloaded so it should not matter
    //
    // there is a very small window of opportunity when a task may be pushed right before removing worker from list.

    for (auto it = m_free_active_list.begin (); it != m_free_active_list.end (); ++it)
      {
	if (*it == &worker_arg)
	  {
	    // found worker
	    (void) m_free_active_list.erase (it);
	    return true;
	  }
      }

    // not in the list
    return false;
  }

  template <typename Context>
  typename worker_pool<Context>::core::context_type &
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
    // tell all workers to stop
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

    // claim all inactive workers as possible; this will guarantee those workers are stopped
    while (!m_inactive_list.empty ())
      {
	m_inactive_list.pop_front ();
	++count_inout;
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::retire_queued_tasks (void)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    while (m_task_queue.empty ())
      {
	m_task_queue.front ()->retire ();
	m_task_queue.pop ();
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
    , m_stop (false)
    , m_stats_array {0}
    , m_statistics (reinterpret_cast<wpstat::stat_type *> (&m_stats_array))
    , m_push_time ()
    , m_time_point ()
  {
    //
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::init_core (core_type &parent)
  {
    m_parent_core = &parent;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::push_task_on_new_thread (task<Context> *work_p, wpstat::time_point_type push_time)
  {
    // start new thread and run task
    assert (work_p != NULL);

    // make sure worker is in a valid state
    assert (m_task_p == NULL);
    assert (m_context_p == NULL);

    // save push time
    m_push_time = push_time;

    // save task
    m_task_p = work_p;

    // start thread
    std::thread (&worker::run, this).detach ();    // don't wait for it
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::push_task_on_running_thread (task<Context> *work_p,
      wpstat::time_point_type push_time)
  {
    // run on current thread
    assert (work_p != NULL);

    m_push_time = push_time;

    // must lock task mutex
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    // make sure worker is in a valid state
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // set task
    m_task_p = work_p;

    // notify waiting thread
    ulock.unlock (); // mutex is not needed for notify
    m_task_cv.notify_one ();
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
    m_stop = true;    // stop worker
    ulock.unlock ();    // mutex is not needed for notify

    m_task_cv.notify_one ();
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::init_run (void)
  {
    // thread was started
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
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // retire context
    m_parent_core->retire_context (*m_context_p);
    m_context_p = NULL;
    m_statistics.collect_and_time (wpstat::id::RETIRE_CONTEXT, m_time_point);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::retire_current_task (void)
  {
    assert (m_task_p != NULL);

    // retire task
    m_task_p->retire ();
    m_task_p = NULL;
    m_statistics.collect_and_time (wpstat::id::RETIRE_TASK, m_time_point);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::execute_current_task (void)
  {
    assert (m_task_p != NULL);

    // execute task
    m_task_p->execute (*m_context_p);
    m_statistics.collect_and_time (wpstat::id::EXECUTE_TASK, m_time_point);

    // and retire task
    retire_current_task ();

    // notify core one task was finished
    m_parent_core->finished_task_notification ();
  }

  template <typename Context>
  bool
  worker_pool<Context>::core::worker::get_new_task (void)
  {
    assert (m_task_p == NULL);

    // check stop condition
    if (m_stop)
      {
	// stop
	return false;
      }

    // get a queued task or wait for one to come
    const std::chrono::seconds WAIT_TIME = std::chrono::seconds (5);

    // either get a queued task or add to free active list
    // note: returned task cannot be saved directly to m_task_p. if worker is added to wait queue and NULL is returned,
    //       current thread may be preempted. worker is then claimed from free active list and worker is assigned
    //       a task. this changes expected behavior and can have unwanted consequences.
    task_type *task_p = m_parent_core->get_task_or_add_to_free_active_list (*this);
    if (task_p != NULL)
      {
	m_statistics.collect_and_time (wpstat::id::SEARCH_TASK_IN_QUEUE, m_time_point);
	// it is safe to set here
	m_task_p = task_p;
	return true;
      }

    // wait for task
    std::unique_lock<std::mutex> ulock (m_task_mutex);
    if (m_task_p == NULL && !m_stop)
      {
	// wait until a task is received or stopped ...
	// ... or time out
	m_task_cv.wait_for (ulock, WAIT_TIME, [this] { return m_task_p != NULL || m_stop; });
      }
    else
      {
	// no need to wait
      }
    // unlock mutex
    ulock.unlock ();

    if (m_task_p == NULL)
      {
	// not task received, remove from active list
	if (m_parent_core->remove_worker_from_active_list (*this))
	  {
	    // removed from active list
	  }
	else
	  {
	    // I was not removed from active list... that means somebody claimed me right before removing from list
	    // now I have to wait for the task
	    ulock.lock ();
	    if (m_task_p == NULL)
	      {
		// wait until task is set
		m_task_cv.wait (ulock, [this] { return m_task_p != NULL; });
	      }
	    else
	      {
		// task already set, don't wait
	      }
	    ulock.unlock ();
	  }
      }

    if (m_task_p == NULL)
      {
	// no task assigned
	m_time_point = wpstat::clock_type::now ();
	return false;
      }
    else
      {
	// found task
	m_time_point = m_push_time;
	m_statistics.collect_and_time (wpstat::id::WAKEUP_WITH_TASK, m_time_point);

	return true;
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::run (void)
  {
    task_type *task_p = NULL;
    do
      {
	init_run ();    // do stuff at the beginning like creating context

	// loop and execute as many tasks as possible
	do
	  {
	    execute_current_task ();
	  }
	while (get_new_task ());

	finish_run ();    // do stuff on end like retiring context

	// last thing to do is to add to inactive list; we may find a new task still and restart the execution loop
	task_p = m_parent_core->get_task_or_add_to_inactive_list (*this);
	if (task_p == NULL)
	  {
	    break;
	  }

	// it is safe to set here
	m_task_p = task_p;
	m_statistics.collect_and_time (wpstat::id::SEARCH_TASK_IN_QUEUE, m_time_point);
      }
    while (true);
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
