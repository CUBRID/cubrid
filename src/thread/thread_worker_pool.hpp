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

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

// same module include
#include "thread_task.hpp"
#include "thread_waiter.hpp"
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"

// cubrid includes
#include "perf_def.hpp"
#include "extensible_array.hpp"
#include "resources.hpp"

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
#include <pthread.h>

#ifndef TASK_COMM_LEN
#define TASK_COMM_LEN 16
#endif

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
  class worker_pool
  {
    public:
      using context_type = entry;
      using task_type = task<context_type>;

      // forward definition
      class core;

      worker_pool (std::size_t pool_size, std::size_t task_max_count, entry_manager &entry_mgr,
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

      // stop worker pool; stop all running threads; discard any tasks in queue
      void stop_execution (void);

      // start all worker threads to be ready for future tasks
      void start_all_workers (void);

      // is_running = is not stopped; when created, a worker pool starts running.
      // worker is stopped after stop_execution () is called
      bool is_running (void) const;

      // get name
      const std::string &get_name (void) const;

      // is_full = the maximum number of tasks is reached
      bool is_full (void) const;

      // get maximum number of threads that can run concurrently in this worker pool
      std::size_t get_max_count (void) const;
      // get the number of cores
      std::size_t get_core_count (void) const;

      // get worker pool statistics
      // note: the statistics are collected from all cores and all their workers adding up all local statistics
      void get_stats (cubperf::stat_value *stats_out) const;
      void get_task_stats (uint64_t *stats_out) noexcept;

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
      // forward definition for nested core class; he's a friend
      friend class core;

      // get next core by round robin scheduling
      std::size_t get_round_robin_core_hash (void);

      // maximum number of concurrent workers
      std::size_t m_max_workers;

      // work queue to store tasks that cannot be immediately executed
      std::size_t m_task_max_count;
      std::atomic<std::size_t> m_task_count;

      // thread entry manager
      entry_manager &m_entry_manager;

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
  class worker_pool::core
  {
    public:
      // forward definition of nested class worker
      class worker;

      // init function
      void init_pool_and_workers (worker_pool &parent, std::size_t worker_count);

      // interface for worker pool
      // task management
      // execute task; returns true if task is accepted, false if it is rejected (no available workers)
      void execute_task (task_type *task_p, bool method_mode);

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
      entry_manager &get_entry_manager (void);

      // getters
      std::size_t get_max_worker_count (void) const;
      inline worker_pool *get_parent_pool (void) const
      {
	return m_parent_pool;
      }

      // temp worker
      void register_free_temp_list (worker *w);
      void free_all_temp_list ();

      inline uint64_t on_task_requested () noexcept;
      inline uint64_t on_task_started () noexcept;
      inline uint64_t on_task_completed () noexcept;

      std::tuple<uint64_t, uint64_t, uint64_t> get_task_stats () noexcept;

    private:
      // execute task for method/stored procedure by recursive call; This task is not pooled and executes in a temporary created thread.
      void execute_temp_task (task_type *task_p, const cubperf::time_point &push_time);

      friend worker_pool;

      // ctor/dtor
      core ();
      ~core (void);

      worker_pool *m_parent_pool;		      // pointer to parent pool
      std::size_t m_max_workers;                      // maximum number of workers running at once
      worker *m_worker_array;                         // all core workers
      worker **m_available_workers;
      std::size_t m_available_count;
      std::queue<task_type *> m_task_queue;           // list of tasks pushed while all workers were occupied
      mutable std::mutex m_workers_mutex;             // mutex to synchronize activity on worker lists

      std::set<worker *> m_temp_workers;              // temporary executed workers for method/stored procedure
      std::vector<worker *> m_temp_free_workers;      //
      mutable std::mutex m_temp_workers_mutex;        // mutex to synchronize temp worker lists

      struct
      {
	std::atomic<uint64_t> requested;
	std::atomic<uint64_t> started;
	std::atomic<uint64_t> completed;
      } m_task_metrics;
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
  class worker_pool::core::worker
  {
    public:
      worker (bool is_temp = false);
      ~worker (void);

      // init
      void init_core (core &parent);

      // assign task (can be NULL) to running thread or start thread
      void assign_task (task_type *work_p, cubperf::time_point push_time);
      // start thread for current worker
      void start_thread (void);
      // run task on current thread (push_time is provided by core)
      void push_task_on_running_thread (task_type *work_p, cubperf::time_point push_time);
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

      core *m_parent_core;		      // parent core
      context_type *m_context_p;              // execution context (same lifetime as spawned thread)

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

} // namespace cubthread

namespace cubthread
{
  /************************************************************************/
  /* Template/inline implementation                                       */
  /************************************************************************/

  template <typename Func, typename ... Args>
  void
  worker_pool::map_running_contexts (Func &&func, Args &&... args)
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

  template <typename Func, typename ... Args>
  void
  worker_pool::map_cores (Func &&func, Args &&... args)
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

  template <typename Func, typename ... Args>
  void
  worker_pool::core::map_running_contexts (bool &stop, Func &&func, Args &&... args) const
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

    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);
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

  template <typename Func, typename ... Args>
  void
  worker_pool::core::worker::map_context_if_running (bool &stop, Func &&func, Args &&... args)
  {
    if (m_task_p == NULL)
      {
	// not running
	return;
      }

    context_type *ctxp = m_context_p;

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

