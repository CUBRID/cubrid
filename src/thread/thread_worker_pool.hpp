/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * thread_worker_pool.hpp
 */

#ifndef _THREAD_WORKER_POOL_HPP_
#define _THREAD_WORKER_POOL_HPP_

// same module include
#include "thread_task.hpp"
#include "thread_waiter.hpp"

// cubrid includes
#include "perf_def.hpp"
#include "extensible_array.hpp"

// system includes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <forward_list>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <system_error>
#include <thread>
#include <set>

#include <cassert>
#include <cstring>

namespace cubthread
{
  // cubtread::worker_pool<Context>
  //
  //  templates
  //    Context - thread context; a class to cache helpful information for task execution
  //
  //  description
  //    a pool of threads to execute tasks in parallel
  //    for high-loads (more tasks than threads), stores tasks in queues to be executed when a thread is freed.
  //    for low-loads (fewer tasks than threads), retires thread when no new tasks are available and creates new
  //      threads when tasks are added again
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
  //    core manages a number of workers, tracks available resource - free active workers and inactive workers and
  //    queues tasks that could not be executed immediately.
  //
  //    a worker starts by being inactive (does not have a thread running). it spawns a thread on its first task,
  //    becoming active, and stays active as long as it finds more tasks to execute.
  //
  //    This is how a new task is processed:
  //
  //      1. Worker pool assigns the task to a core via round robin method*. The core can then accept the task (if an
  //         available worker is found) or reject it. If task is rejected, then it is stored in a queue to be processed
  //         later.
  //         *round robin scheduling behavior may be overwritten by using execute_on_core instead of execute. it is
  //         recommended to understand how cores and workers work before trying it.
  //         sometimes however, if current tasks may be blocked until other incoming tasks are finished, a more careful
  //         core management is required.
  //
  //      2. Core checks if a thread is available
  //          - first checks free active list
  //          - if no free active worker is found, it checks inactive list
  //          - if a worker is found, then it is assigned the task
  //          - if no worker is found, task is saved in queue
  //
  //      3. Task is executed by worker in one of three ways:
  //          3.1. worker was inactive and starts a new thread to execute the task
  //               after it finishes its first task, it tries to find new ones:
  //          3.2. gets a queued task on its parent core
  //          3.3. if there is no queue task, notifies core of its status (free and active) and waits for new task.
  //          note: 3.2. and 3.3. together is an atomic operation (protected by mutex)
  //          Worker stops if waiting for new task times out (and becomes inactive).
  //
  //    NOTE: core class is private nested to worker pool and cannot be instantiated outside it.
  //          worker class is private nested to core class.
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

      // forward definition
      class core;

      worker_pool (std::size_t pool_size, std::size_t task_max_count, context_manager_type &context_mgr,
		   const char *name, std::size_t core_count = 1, bool debug_logging = false, bool pool_threads = false,
		   wait_seconds wait_for_task_time = std::chrono::seconds (5));
      ~worker_pool ();

      // try to execute task; executes only if the maximum number of tasks is not reached.
      // it return true when task is executed, false otherwise
      bool try_execute (task_type *work_arg);

      // execute task; execution is guaranteed, even if maximum number of tasks is reached.
      // read implementation in class comment for details
      void execute (task_type *work_arg);
      // execute on give core. real core index is core_hash module core count.
      // note: regular execute chooses a core through round robin scheduling. this may not be a good fit for all
      //       execution patterns.
      //       execute_on_core provides control on core scheduling.
      void execute_on_core (task_type *work_arg, std::size_t core_hash, bool method_mode = false);

      // try to execute task on given core
      // it return true when task is executed, false otherwise
      bool try_execute_on_core (task_type *work_arg, std::size_t core_hash);

      // stop worker pool; stop all running threads; discard any tasks in queue
      void stop_execution (void);

      // start all worker threads to be ready for future tasks
      void start_all_workers (void);

      // is_running = is not stopped; when created, a worker pool starts running.
      // worker is stopped after stop_execution () is called
      bool is_running (void) const;

      // is_full = the maximum number of tasks is reached
      bool is_full (void) const;

      // get maximum number of threads that can run concurrently in this worker pool
      std::size_t get_max_count (void) const;
      // get the number of cores
      std::size_t get_core_count (void) const;

      // get worker pool statistics
      // note: the statistics are collected from all cores and all their workers adding up all local statistics
      void get_stats (cubperf::stat_value *stats_out) const;

      // log stats to error log file
      void er_log_stats (void) const;

      inline bool is_pooling_threads () const
      {
	return m_pool_threads;
      }
      inline const wait_seconds &get_wait_for_task_time () const
      {
	return m_wait_for_task_time;
      }

      //////////////////////////////////////////////////////////////////////////
      // context management
      //////////////////////////////////////////////////////////////////////////

      // map functions over all running contexts
      //
      // function signature is:
      //    cubthread::worker_pool::context_type & (in/out)    : running thread context
      //    bool & (in/out)                                    : input is usually false, output true to stop mapping
      //    typename ... args (in/out)                         : variadic arguments based on needs
      //
      // WARNING:
      //    this is a dangerous functionality. please note that context retirement and mapping function is not
      //    synchronized. mapped context may be retired or in process of retirement.
      //
      //    make sure your case is handled properly
      //
      template <typename Func, typename ... Args>
      void map_running_contexts (Func &&func, Args &&... args);

      // map functions over all cores
      //
      // function signature is:
      //    const cubthread::worker_pool::core & (in) : core
      //    bool & (in/out)                           : input is usually false, output true to stop mapping
      //    typename ... args (in/out)                : variadic arguments based on needs
      //
      template <typename Func, typename ... Args>
      void map_cores (Func &&func, Args &&... args);

    private:
      using atomic_context_ptr = std::atomic<context_type *>;

      // forward definition for nested core class; he's a friend
      friend class core;

      // get next core by round robin scheduling
      std::size_t get_round_robin_core_hash (void);

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

      // true to start threads at init
      bool m_pool_threads;

      // transition time period between active and inactive
      wait_seconds m_wait_for_task_time;

      std::string m_name;
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

      // init function
      void init_pool_and_workers (worker_pool<Context> &parent, std::size_t worker_count);

      // interface for worker pool
      // task management
      // execute task; returns true if task is accepted, false if it is rejected (no available workers)
      void execute_task (task_type *task_p, bool method_mode);
      // try to execute task; returns true if task is accepted, false if it is rejected
      bool try_execute_task (task_type *task_p);

      // context management
      // map function to all workers (and their contexts)
      template <typename Func, typename ... Args>
      void map_running_contexts (bool &stop, Func &&func, Args &&... args) const;
      // worker management
      // notify workers to stop; if any of core's workers are still running, outputs is_not_stopped = true
      void notify_stop (bool &is_not_stopped);
      void retire_queued_tasks (void);

      void start_all_workers (void);

      // statistics
      void get_stats (cubperf::stat_value *sum_inout) const;

      // interface for workers
      // task management
      void finished_task_notification (void);
      // worker management
      // get a task or add worker to free active list (still running, but ready to execute another task)
      task_type *get_task_or_become_available (worker &worker_arg);
      void become_available (worker &worker_arg);
      // is worker available?
      void check_worker_not_available (const worker &worker_arg);
      // context management
      context_manager<context_type> &get_context_manager (void);

      // getters
      std::size_t get_max_worker_count (void) const;
      inline worker_pool_type *get_parent_pool (void) const
      {
	return m_parent_pool;
      }

      // temp worker
      void register_free_temp_list (worker *w);
      void free_all_temp_list ();

    private:
      // execute task for method/stored procedure by recursive call; This task is not pooled and executes in a temporary created thread.
      void execute_temp_task (task_type *task_p, const cubperf::time_point &push_time);

      friend worker_pool;

      // ctor/dtor
      core ();
      ~core (void);

      worker_pool_type *m_parent_pool;                // pointer to parent pool
      std::size_t m_max_workers;                      // maximum number of workers running at once
      worker *m_worker_array;                         // all core workers
      worker **m_available_workers;
      std::size_t m_available_count;
      std::queue<task_type *> m_task_queue;           // list of tasks pushed while all workers were occupied
      std::mutex m_workers_mutex;                     // mutex to synchronize activity on worker lists

      std::set<worker *> m_temp_workers;              // temporary executed workers for method/stored procedure
      std::vector<worker *> m_temp_free_workers;      //
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
  //      3. by being claimed from task queue; if no worker (active or inactive) is available, task is queued on core
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

      worker (bool is_temp = false);
      ~worker (void);

      // init
      void init_core (core_type &parent);

      // assign task (can be NULL) to running thread or start thread
      void assign_task (task<Context> *work_p, cubperf::time_point push_time);
      // start thread for current worker
      void start_thread (void);
      // run task on current thread (push_time is provided by core)
      void push_task_on_running_thread (task<Context> *work_p, cubperf::time_point push_time);
      // stop execution; if worker has a thread running, it outputs is_not_stopped = true
      void stop_execution (bool &is_not_stopped);

      // map function to context (if a task is running and if context is available)
      //
      // note - sometimes a thread has a context assigned, but it is waiting for tasks. if that's the case, the
      //        function will not be applied, since it is not considered a "running" context.
      //
      template <typename Func, typename ... Args>
      void map_context_if_running (bool &stop, Func &&func, Args &&... args);

      // add own stats to given argument
      void get_stats (cubperf::stat_value *sum_inout) const;

      std::mutex &get_mutex (void)
      {
	return m_task_mutex;
      }
      bool has_thread (void)
      {
	return m_has_thread;
      }
      void set_has_thread (void)
      {
	m_has_thread = true;
      }
      void set_push_time_now (void)
      {
	m_push_time = cubperf::clock::now ();
      }

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
      bool m_has_thread;                      // true if worker has a thread running
      bool m_is_temp;                         // true if worker is for temp task

      // statistics
      cubperf::statset &m_statistics;                                          // statistic collector
      cubperf::time_point m_push_time;                          // push time point (provided by core)
  };

  //////////////////////////////////////////////////////////////////////////
  // statistics
  //////////////////////////////////////////////////////////////////////////

  // collected workers
  static const cubperf::stat_id Wpstat_start_thread = 0;
  static const cubperf::stat_id Wpstat_create_context = 1;
  static const cubperf::stat_id Wpstat_execute_task = 2;
  static const cubperf::stat_id Wpstat_retire_task = 3;
  static const cubperf::stat_id Wpstat_found_in_queue = 4;
  static const cubperf::stat_id Wpstat_wakeup_with_task = 5;
  static const cubperf::stat_id Wpstat_recycle_context = 6;
  static const cubperf::stat_id Wpstat_retire_context = 7;

  cubperf::statset &wp_worker_statset_create (void);
  void wp_worker_statset_destroy (cubperf::statset &stats);
  void wp_worker_statset_time_and_increment (cubperf::statset &stats, cubperf::stat_id id);
  void wp_worker_statset_accumulate (const cubperf::statset &what, cubperf::stat_value *where);
  std::size_t wp_worker_statset_get_count (void);
  const char *wp_worker_statset_get_name (std::size_t stat_index);

  //////////////////////////////////////////////////////////////////////////
  // other functions
  //////////////////////////////////////////////////////////////////////////

  // system_core_count - return system core counts or 1 (if system core count cannot be obtained).
  //
  // use it as core count if the task execution must be highly tuned.
  // does not return 0
  std::size_t system_core_count (void);

  // custom worker pool exception handler
  void wp_handle_system_error (const char *message, const std::system_error &e);
  template <typename Func>
  void wp_call_func_throwing_system_error (const char *message, Func &func);

  // dump worker pool statistics to error log
  void wp_er_log_stats (const char *header, cubperf::stat_value *statsp);

  bool wp_is_thread_always_alive_forced ();
  void wp_set_force_thread_always_alive ();

  /************************************************************************/
  /* Template/inline implementation                                       */
  /************************************************************************/

  //////////////////////////////////////////////////////////////////////////
  // worker_pool implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename Context>
  worker_pool<Context>::worker_pool (std::size_t pool_size, std::size_t task_max_count,
				     context_manager_type &context_mgr, const char *name, std::size_t core_count,
				     bool debug_log, bool pool_threads, wait_seconds wait_for_task_time)
    : m_max_workers (pool_size)
    , m_task_max_count (task_max_count)
    , m_task_count (0)
    , m_context_manager (context_mgr)
    , m_core_array (NULL)
    , m_core_count (core_count)
    , m_round_robin_counter (0)
    , m_stopped (false)
    , m_log (debug_log)
    , m_pool_threads (pool_threads)
    , m_wait_for_task_time (wait_for_task_time)
    , m_name (name == NULL ? "" : name)
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

    if (wp_is_thread_always_alive_forced ())
      {
	// override pooling/wait time options to keep threads always alive
	m_pool_threads = true;
	m_wait_for_task_time.set_infinite_wait ();
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
  bool
  worker_pool<Context>::try_execute_on_core (task_type *work_arg, std::size_t core_hash)
  {
    std::size_t core_index = core_hash % m_core_count;

    bool is_success = m_core_array[core_index].try_execute_task (work_arg, core_index);
    if (is_success)
      {
	++m_task_count;
	return true;
      }
    else
      {
	return false;
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::execute (task_type *work_arg)
  {
    execute_on_core (work_arg, get_round_robin_core_hash ());
  }

  template <typename Context>
  void
  worker_pool<Context>::execute_on_core (task_type *work_arg, std::size_t core_hash, bool for_method)
  {
    // increment task count
    ++m_task_count;

    std::size_t core_index = core_hash % m_core_count;
    m_core_array[core_index].execute_task (work_arg, for_method);
  }

  template <typename Context>
  void
  worker_pool<Context>::stop_execution (void)
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

#if defined (NDEBUG)
    const std::chrono::seconds time_wait_to_thread_stop (30);   // timeout duration = 30 secs on release mode
    const std::chrono::milliseconds time_spin_sleep (10);       // sleep between spins for 10 milliseconds
#else // DEBUG
    const std::chrono::seconds time_wait_to_thread_stop (60);   // timeout duration = 60 secs on debug mode
    const std::chrono::milliseconds time_spin_sleep (10);       // sleep between spins for 10 milliseconds
#endif

    // loop until all workers are stopped or until timeout expires
    std::size_t stop_count = 0;
    auto timeout = std::chrono::system_clock::now () + time_wait_to_thread_stop;

    bool is_not_stopped;
    while (true)
      {
	// notify all cores to stop
	is_not_stopped = false;     // assume all are stopped
	for (std::size_t it = 0; it < m_core_count; it++)
	  {
	    // notify all workers to stop. if any worker is still running, is_not_stopped = true is output
	    m_core_array[it].notify_stop (is_not_stopped);
	  }

	if (!is_not_stopped)
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
  void
  worker_pool<Context>::start_all_workers (void)
  {
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].start_all_workers ();
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
  std::size_t
  worker_pool<Context>::get_max_count (void) const
  {
    return m_max_workers;
  }

  template<typename Context>
  std::size_t
  worker_pool<Context>::get_core_count (void) const
  {
    return m_core_count;
  }

  template<typename Context>
  void
  worker_pool<Context>::get_stats (cubperf::stat_value *stats_out) const
  {
    for (std::size_t it = 0; it < m_core_count; it++)
      {
	m_core_array[it].get_stats (stats_out);
      }
  }

  template<typename Context>
  void
  worker_pool<Context>::er_log_stats (void) const
  {
    if (!m_log)
      {
	return;
      }

    const std::size_t MAX_SIZE = 32;
    cubperf::stat_value stats[MAX_SIZE];
    std::memset (stats, 0, sizeof (stats));
    get_stats (stats);
    wp_er_log_stats (m_name.c_str (), stats);
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  cubthread::worker_pool<Context>::map_running_contexts (Func &&func, Args &&... args)
  {
    bool stop = false;
    for (std::size_t it = 0; it < m_core_count && !stop; it++)
      {
	m_core_array[it].map_running_contexts (stop, func, args...);
	if (stop)
	  {
	    // mapping is stopped
	    return;
	  }
      }
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  cubthread::worker_pool<Context>::map_cores (Func &&func, Args &&... args)
  {
    bool stop = false;
    const core *core_p;
    for (std::size_t it = 0; it < m_core_count && !stop; it++)
      {
	core_p = &m_core_array[it];
	func (*core_p, stop, args...);
	if (stop)
	  {
	    // mapping is stopped
	    return;
	  }
      }
  }

  template <typename Context>
  std::size_t
  worker_pool<Context>::get_round_robin_core_hash (void)
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
  // worker_pool::core
  //////////////////////////////////////////////////////////////////////////

  template <typename Context>
  worker_pool<Context>::core::core ()
    : m_parent_pool (NULL)
    , m_max_workers (0)
    , m_worker_array (NULL)
    , m_available_workers (NULL)
    , m_available_count (0)
    , m_task_queue ()
    , m_workers_mutex ()
    , m_temp_workers ()
  {
    //
  }

  template <typename Context>
  worker_pool<Context>::core::~core ()
  {
    delete [] m_worker_array;
    m_worker_array = NULL;

    delete [] m_available_workers;
    m_available_workers = NULL;

    for (worker *w : m_temp_workers)
      {
	if (w)
	  {
	    delete w;
	  }
      }
    m_temp_workers.clear ();
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
    m_available_workers = new worker*[m_max_workers];

    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].init_core (*this);
	if (m_parent_pool->m_pool_threads)
	  {
	    // assign task / start thread
	    // it will add itself to available workers
	    m_worker_array[it].assign_task (NULL, cubperf::clock::now ());
	  }
	else
	  {
	    // add to available workers
	    m_available_workers[m_available_count++] = &m_worker_array[it];
	  }
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
  worker_pool<Context>::core::execute_task (task_type *task_p, bool for_method)
  {
    assert (task_p != NULL);

    // find an available worker
    // 1. one already active is preferable
    // 2. inactive will do too
    // 3. if no workers, reject task (returns false)

    cubperf::time_point push_time = cubperf::clock::now ();
    worker *refp = NULL;

    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (m_parent_pool->m_stopped)
      {
	// reject task
	task_p->retire ();
	return;
      }

    if (m_available_count > 0)
      {
	refp = m_available_workers[--m_available_count];
	ulock.unlock ();

	assert (refp != NULL);
	refp->assign_task (task_p, push_time);
      }
    else
      {
	if (for_method)
	  {
	    execute_temp_task (task_p, push_time);
	  }
	else
	  {
	    // save to queue
	    m_task_queue.push (task_p);
	  }
      }
  }

  template <typename Context>
  bool
  worker_pool<Context>::core::try_execute_task (task_type *task_p)
  {
    assert (task_p != NULL);

    cubperf::time_point push_time = cubperf::clock::now ();
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (m_parent_pool->m_stopped)
      {
	// reject task
	task_p->retire ();
	return false;
      }

    if (m_available_count > 0)
      {
	worker *refp = m_available_workers[--m_available_count];
	ulock.unlock ();

	assert (refp != NULL);
	refp->assign_task (task_p, push_time);
	return true;
      }
    else
      {
	return false;
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::execute_temp_task (task_type *task_p, const cubperf::time_point &push_time)
  {
    worker *w = new worker (true);
    m_temp_workers.insert (w);
    w->init_core (*this);
    w->assign_task (task_p, push_time);
  }

  template <typename Context>
  typename worker_pool<Context>::core::task_type *
  worker_pool<Context>::core::get_task_or_become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_task_queue.empty ())
      {
	task_type *task_p = m_task_queue.front ();
	assert (task_p != NULL);
	m_task_queue.pop ();
	return task_p;
      }

    m_available_workers[m_available_count++] = &worker_arg;
    assert (m_available_count <= m_max_workers);
    return NULL;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);
    m_available_workers[m_available_count++] = &worker_arg;
    assert (m_available_count <= m_max_workers);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::check_worker_not_available (const worker &worker_arg)
  {
#if !defined (NDEBUG)
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    for (std::size_t idx = 0; idx < m_available_count; idx++)
      {
	assert (m_available_workers[idx] != &worker_arg);
      }
#endif // DEBUG
  }

  template <typename Context>
  context_manager<typename worker_pool<Context>::core::context_type> &
  worker_pool<Context>::core::get_context_manager (void)
  {
    return m_parent_pool->m_context_manager;
  }

  template <typename Context>
  std::size_t
  worker_pool<Context>::core::get_max_worker_count (void) const
  {
    return m_max_workers;
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  cubthread::worker_pool<Context>::core::map_running_contexts (bool &stop, Func &&func, Args &&... args) const
  {
    for (std::size_t it = 0; it < m_max_workers && !stop; it++)
      {
	m_worker_array[it].map_context_if_running (stop, func, args...);
	if (stop)
	  {
	    // stop mapping
	    return;
	  }
      }

    for (worker *w : m_temp_workers)
      {
	w->map_context_if_running (stop, func, args...);
	if (stop)
	  {
	    // stop mapping
	    return;
	  }
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::notify_stop (bool &is_not_stopped)
  {
    // stop all temp workers first
    for (worker *w : m_temp_workers)
      {
	w->stop_execution (is_not_stopped);
      }

    // tell all workers to stop
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].stop_execution (is_not_stopped);
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::start_all_workers (void)
  {
    worker *refp = NULL;
    const std::size_t AVAILABLE_STACK_DEFAULT_SIZE = 1024;
    cubmem::appendable_array<worker *, AVAILABLE_STACK_DEFAULT_SIZE> available_stack;

    // how this works:
    //
    // we need to start all workers, but we need to consider the fact that some may be already running. what we need
    // to do is process the available workers and start threads for all those that don't have a thread started.
    //
    // workers that already have threads are saved and added back after processing all available workers.
    //
    // NOTE: this function does not guarantee that at the end all workers have threads. workers that stop their threads
    //       during processing available workers are not restarted. however, we end up starting all or almost all
    //       threads, which is good enough.
    //

    while (true)
      {
	// processing is done in two steps:
	//
	//    1. retrieve worker from available workers holding workers mutex
	//    2. verify if worker has a thread started
	//        2.1. if it doesn't have a thread, start one
	//        2.2. if it does have thread, save it to available_stack.
	//

	std::unique_lock<std::mutex> core_lock (m_workers_mutex);
	if (m_available_count == 0)
	  {
	    break;
	  }
	refp = m_available_workers[--m_available_count];
	core_lock.unlock ();

	if (refp->has_thread ())
	  {
	    // stack to make available at the end
	    available_stack.append (&refp, 1);

	    // note: this worker's thread may stop soon or may have stopped already. this case is accepted.
	  }
	else
	  {
	    // this thread is already stopped and we can start its thread
	    refp->set_push_time_now ();
	    refp->set_has_thread ();
	    refp->start_thread ();
	  }
      }

    // copy all workers having threads back to available array.
    if (available_stack.get_size () > 0)
      {
	std::unique_lock<std::mutex> core_lock (m_workers_mutex);
	if (m_available_count > 0)
	  {
	    // move current available to make room for older ones
	    std::memmove (m_available_workers + available_stack.get_size (), m_available_workers,
			  m_available_count * sizeof (worker *));
	  }

	// copy from stack at the beginning of m_available_workers
	std::memcpy (m_available_workers, available_stack.get_array (), available_stack.get_memsize ());

	// update available count
	m_available_count += available_stack.get_size ();
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::retire_queued_tasks (void)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    while (!m_task_queue.empty ())
      {
	m_task_queue.front ()->retire ();
	m_task_queue.pop ();
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::get_stats (cubperf::stat_value *stats_out) const
  {
    for (std::size_t it = 0; it < m_max_workers; it++)
      {
	m_worker_array[it].get_stats (stats_out);
      }

    for (worker *w: m_temp_workers)
      {
	w->get_stats (stats_out);
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::register_free_temp_list (worker *w)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    m_temp_workers.erase (w);
    m_temp_free_workers.push_back (w);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::free_all_temp_list ()
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    for (worker *w: m_temp_free_workers)
      {
	delete w;
      }
    m_temp_free_workers.clear ();
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool<Context>::core::worker
  //////////////////////////////////////////////////////////////////////////

  template <typename Context>
  worker_pool<Context>::core::worker::worker (bool is_temp)
    : m_parent_core (NULL)
    , m_context_p (NULL)
    , m_task_p (NULL)
    , m_task_cv ()
    , m_task_mutex ()
    , m_stop (false)
    , m_has_thread (false)
    , m_statistics (wp_worker_statset_create ())
    , m_push_time ()
    , m_is_temp (is_temp)
  {
    //
  }

  template <typename Context>
  worker_pool<Context>::core::worker::~worker (void)
  {
    wp_worker_statset_destroy (m_statistics);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::init_core (core_type &parent)
  {
    m_parent_core = &parent;
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::assign_task (task<Context> *work_p, cubperf::time_point push_time)
  {
    // save push time
    m_push_time = push_time;

    std::unique_lock<std::mutex> ulock (m_task_mutex);

    // save task
    m_task_p = work_p;

    if (m_is_temp)
      {
	m_has_thread = true;
	assert (m_context_p == NULL);
	start_thread ();
      }

    if (m_has_thread)
      {
	// notify waiting thread
	ulock.unlock (); // mutex is not needed for notify
	m_task_cv.notify_one ();
      }
    else
      {
	m_has_thread = true;
	ulock.unlock ();

	assert (m_context_p == NULL);

	start_thread ();
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::start_thread (void)
  {
    assert (m_has_thread);

    //
    // the next code tries to help visualizing any system errors that can occur during create or detach in debug
    // mode
    //
    // release will basically be reduced to:
    // std::thread (&worker::run, this).detach ();
    //

    std::thread t;

    auto lambda_create = [&] (void) -> void { t = std::thread (&worker::run, this); };
    auto lambda_detach = [&] (void) -> void { t.detach (); };

    wp_call_func_throwing_system_error ("starting thread", lambda_create);
    wp_call_func_throwing_system_error ("detaching thread", lambda_detach);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::push_task_on_running_thread (task<Context> *work_p,
      cubperf::time_point push_time)
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
  worker_pool<Context>::core::worker::stop_execution (bool &is_not_stopped)
  {
    context_type *context_p = m_context_p;

    if (context_p != NULL)
      {
	// notify context to stop
	m_parent_core->get_context_manager ().stop_execution (*context_p);
      }

    // make sure thread is not waiting for tasks
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    if (m_has_thread)
      {
	/// this thread is still running
	is_not_stopped = true;
      }

    m_stop = true;    // stop worker
    ulock.unlock ();    // mutex is not needed for notify

    if (m_is_temp)
      {
	// not to notify one if it is for temp
	return;
      }
    m_task_cv.notify_one ();
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::init_run (void)
  {
    // safe-guard - we have a thread
    assert (m_has_thread);

    // safe-guard - threads should [no longer] be available
    if (m_is_temp == false)
      {
	m_parent_core->check_worker_not_available (*this);
      }

    // thread was started
    m_statistics.m_timept = m_push_time;
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_start_thread);

    // a context is required
    m_context_p = &m_parent_core->get_context_manager ().create_context ();
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_create_context);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::finish_run (void)
  {
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // retire context
    m_parent_core->get_context_manager ().retire_context (*m_context_p);
    m_context_p = NULL;
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_retire_context);

    if (m_is_temp)
      {
	m_parent_core->register_free_temp_list (this);
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::retire_current_task (void)
  {
    assert (m_task_p != NULL);

    // retire task
    m_task_p->retire ();
    m_task_p = NULL;
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_retire_task);
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::execute_current_task (void)
  {
    assert (m_task_p != NULL);

    // execute task
    m_task_p->execute (*m_context_p);
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_execute_task);

    // and retire task
    retire_current_task ();

    // and recycle context before getting another task
    m_parent_core->get_context_manager ().recycle_context (*m_context_p);
    wp_worker_statset_time_and_increment (m_statistics, Wpstat_recycle_context);

    // notify core one task was finished
    if (m_is_temp == false)
      {
	m_parent_core->finished_task_notification ();
	m_parent_core->free_all_temp_list ();
      }
  }

  template <typename Context>
  bool
  worker_pool<Context>::core::worker::get_new_task (void)
  {
    assert (m_task_p == NULL);

    std::unique_lock<std::mutex> ulock (m_task_mutex, std::defer_lock);

    // check stop condition
    if (!m_stop)
      {
	// get a queued task or wait for one to come

	// either get a queued task or add to free active list
	// note: returned task cannot be saved directly to m_task_p. if worker is added to wait queue and NULL is returned,
	//       current thread may be preempted. worker is then claimed from free active list and worker is assigned
	//       a task. this changes expected behavior and can have unwanted consequences.
	task_type *task_p = m_parent_core->get_task_or_become_available (*this);
	if (task_p != NULL)
	  {
	    wp_worker_statset_time_and_increment (m_statistics, Wpstat_found_in_queue);

	    // it is safe to set here
	    m_task_p = task_p;
	    return true;
	  }

	// wait for task
	ulock.lock ();
	if (m_task_p == NULL && !m_stop)
	  {
	    // wait until a task is received or stopped ...
	    // ... or time out
	    condvar_wait (m_task_cv, ulock, m_parent_core->get_parent_pool ()->get_wait_for_task_time (),
			  [this] () -> bool { return m_task_p != NULL || m_stop; });
	  }
	else
	  {
	    // no need to wait
	  }
      }
    else
      {
	// we need to add to available list
	m_parent_core->become_available (*this);

	ulock.lock ();
      }

    // did I get a task?
    if (m_task_p == NULL)
      {
	// no; this thread will stop. from this point forward, if a new task is assigned, a new thread must be spawned
	m_has_thread = false;

	// finish_run; we neet to retire context before another thread uses this worker
	m_statistics.m_timept = cubperf::clock::now ();
	finish_run ();

	return false;
      }
    else
      {
	// unlock mutex
	ulock.unlock ();

	// safe-guard - threads should no longer be available
	m_parent_core->check_worker_not_available (*this);

	// found task
	m_statistics.m_timept = m_push_time;
	wp_worker_statset_time_and_increment (m_statistics, Wpstat_wakeup_with_task);
	return true;
      }
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::run (void)
  {
    task_type *task_p = NULL;

    init_run ();    // do stuff at the beginning like creating context

    if (m_is_temp)
      {
	execute_current_task ();
	finish_run ();
	return;
      }

    if (m_task_p == NULL)
      {
	// started without task; get one
	if (get_new_task ())
	  {
	    assert (m_task_p != NULL);
	  }
      }

    if (m_task_p != NULL)
      {
	// loop and execute as many tasks as possible
	do
	  {
	    execute_current_task ();
	  }
	while (get_new_task ());
      }
    else
      {
	// never got a task
      }

    // finish_run ();    // do stuff on end like retiring context
  }

  template <typename Context>
  void
  worker_pool<Context>::core::worker::get_stats (cubperf::stat_value *sum_inout) const
  {
    wp_worker_statset_accumulate (m_statistics, sum_inout);
  }

  template <typename Context>
  template <typename Func, typename ... Args>
  void
  worker_pool<Context>::core::worker::map_context_if_running (bool &stop, Func &&func, Args &&... args)
  {
    if (m_task_p == NULL)
      {
	// not running
	return;
      }

    Context *ctxp = m_context_p;

    if (ctxp != NULL)
      {
	func (*ctxp, stop, args...);
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // other functions
  //////////////////////////////////////////////////////////////////////////

  template <typename Func>
  void
  wp_call_func_throwing_system_error (const char *message, Func &func)
  {
#if !defined (NDEBUG)
    try
      {
#endif // DEBUG

	func ();  // no exception catching on release

#if !defined (NDEBUG)
      }
    catch (const std::system_error &e)
      {
	wp_handle_system_error (message, e);
      }
#endif // DEBUG
  }

} // namespace cubthread




#endif // _THREAD_WORKER_POOL_HPP_
