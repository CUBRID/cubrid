/*
 *
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
#include <algorithm>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <system_error>
#include <thread>

#include <cassert>
#include <cstring>
#include <pthread.h>

#ifndef TASK_COMM_LEN
#define TASK_COMM_LEN 16
#endif

namespace cubthread
{
  // cubtread::worker_pool
  //
  //  description
  //    a pool of threads to execute tasks in parallel
  //    for high-loads (more tasks than threads), stores tasks in queues to be executed when a thread is freed.
  //    for low-loads (fewer tasks than threads), retires thread when no new tasks are available and creates new
  //      threads when tasks are added again
  //    in high-loads, thread context is shared between task
  //
  // how to use
  //
  //    // define the task
  //    class custom_task : public task<custom_context>
  //    {
  //      void execute (Context &) override { ... }
  //      void create_context (void) override { ... }
  //      void retire_context (Context &) override { ... }
  //    };
  //
  //    // create worker pool
  //    cubthread::worker_pool thread_pool (THREAD_COUNT, MAX_TASKS);
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
  // interface
  //
  //    the worker pool can be partitioned into cores - a middle layer above a group of workers. this is an
  //    optimization for high-contention systems and only one core can be set if that's not the case.
  //
  //    core manages a number of workers, tracks available resource - free active workers and inactive workers and
  //    queues tasks that could not be executed immediately.
  //
  class worker_pool
  {
    public:
      using context_type = entry;
      using task_type = task<context_type>;

      // forward definition
      class core;

      worker_pool (std::size_t pool_size, std::size_t core_count, const char *name, entry_manager &entry_mgr,
		   bool pool_threads = false, wait_seconds wait_for_task_time = std::chrono::seconds (5));
      virtual ~worker_pool ();

      // get name
      const std::string &get_name (void) const;

      // init
      virtual void initialize (std::size_t worker_count, std::size_t core_count);

      // execute task; execution is guaranteed, even if maximum number of tasks is reached.
      void execute (task_type *work_arg);

      // execute on give core.
      virtual void execute_on_core (task_type *work_arg, std::size_t core_hash, bool is_temp = false);

      // is_running = is not stopped; when created, a worker pool starts running.
      // worker is stopped after stop_execution () is called
      bool is_running (void) const;

      // stop worker pool; stop all running threads; discard any tasks in queue
      void stop_execution (void);

      // get maximum number of threads that can run concurrently in this worker pool
      std::size_t get_worker_count (void) const;
      // get the number of cores
      std::size_t get_core_count (void) const;

      bool get_pool_threads () const
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

    protected:
      // forward definition for nested core class; he's a friend
      friend class core;

      // override this if want to change core type
      virtual std::unique_ptr<core> allocate_core ();

      virtual void allocate_cores (std::size_t core_count);
      virtual void assign_workers_to_cores (std::size_t core_count, std::size_t worker_count);

      // get next core by policy
      virtual std::size_t get_next_core (void);
      // get next core by round robin scheduling (default policy)
      std::size_t get_round_robin_core_hash (void);

      // worker pool name
      std::string m_name;

      // thread entry manager
      entry_manager &m_entry_manager;

      // maximum number of concurrent workers
      std::size_t m_max_workers;

      // core variables
      std::vector<std::unique_ptr<core>> m_cores;

      // [optional] round robin counter used to dispatch tasks on cores
      std::atomic<std::size_t> m_round_robin_counter;

      // set to true when stopped
      std::atomic<bool> m_stopped;

      // true to keep all threads alive
      bool m_pool_threads;

      // transition time period between active and inactive
      wait_seconds m_wait_for_task_time;
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

      core ();
      virtual ~core (void);

      // override this if want to change worker type
      virtual std::unique_ptr<worker> allocate_worker (bool is_temp = false);

      virtual void allocate_workers (std::size_t worker_count);
      virtual void initialize_workers ();
      virtual void initialize (std::size_t worker_count);

      void set_worker_pool (worker_pool &parent);

      // task management
      // execute task
      virtual void execute_task (task_type *task_p, bool is_temp);

      // worker management
      // notify workers to stop; if any of core's workers are still running, outputs is_not_stopped = true
      void notify_stop (bool &is_not_stopped);
      void retire_queued_tasks (void);

      // worker management
      // get a task or add worker to free active list (still running, but ready to execute another task)
      virtual task_type *get_task_or_become_available (worker &worker_arg);
      virtual void become_available (worker &worker_arg);
      // is worker available?
      void check_worker_not_available (const worker &worker_arg);

      // context management
      entry_manager &get_entry_manager (void);

      // getters
      std::size_t get_worker_count (void) const;
      inline worker_pool *get_parent_pool (void) const
      {
	return m_parent_pool;
      }

      // temp worker
      void register_free_temp_list (worker *w);
      void free_all_temp_list ();

      // context management
      // map function to all workers (and their contexts)
      template <typename Func, typename ... Args>
      void map_running_contexts (bool &stop, Func &&func, Args &&... args) const;

    protected:
      // execute task for method/stored procedure by recursive call; This task is not pooled and executes in a temporary created thread.
      virtual void execute_task_as_temp (task_type *task_p);

      friend worker_pool;

      worker_pool *m_parent_pool;		      // pointer to parent pool

      std::vector<std::unique_ptr<worker>> m_workers;
      std::vector<worker *> m_available_workers;
      std::queue<task_type *> m_task_queue;           // list of tasks pushed while all workers were occupied
      mutable std::mutex m_workers_mutex;             // mutex to synchronize activity on worker lists

      std::vector<std::unique_ptr<worker>> m_temp_workers;	      // temporary executed workers for method/stored procedure
      std::vector<std::unique_ptr<worker>> m_free_temp_workers;
      mutable std::mutex m_temp_workers_mutex;        // mutex to synchronize temp worker lists
  };

  // worker_pool<Context>::worker
  //
  // description
  //    the worker is a worker pool nested class and represents one instance of execution. its purpose is to store the
  //    context, manage multiple task executions of a single thread and collect statistics.
  //
  class worker_pool::core::worker
  {
    public:
      worker (bool is_temp = false);
      virtual ~worker (void);

      // init
      void set_core (core &parent);

      // start thread for current worker
      virtual void start_thread (void);

      // assign task (can be NULL) to running thread or start thread
      virtual void assign_task (task_type *work_p);
      // run task on current thread (push_time is provided by core)
      virtual void push_task_on_running_thread (task_type *work_p);

      // stop execution; if worker has a thread running, it outputs is_not_stopped = true
      void stop_execution (bool &is_not_stopped);

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

      // map function to context (if a task is running and if context is available)
      //
      // note - sometimes a thread has a context assigned, but it is waiting for tasks. if that's the case, the
      //        function will not be applied, since it is not considered a "running" context.
      //
      template <typename Func, typename ... Args>
      void map_context_if_running (bool &stop, Func &&func, Args &&... args);

    protected:

      // run function invoked by spawned thread
      virtual void run (void);

      // run initialization (creating execution context)
      virtual void init_run (void);
      // finishing initialization (retiring execution context, worker becomes inactive)
      virtual void finish_run (void);

      // execute m_task_p
      virtual void execute_current_task (void);
      // retire m_task_p
      virtual void retire_current_task (void);
      // get new task from 1. worker pool task queue or 2. wait for incoming tasks
      virtual bool get_new_task (void);

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
  };

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
    for (auto it = m_cores.begin (); it != m_cores.end (); it++)
      {
	(*it)->map_running_contexts (stop, func, args...);
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
    for (auto it = m_cores.begin (); it != m_cores.end (); it++)
      {
	core_p = it->get ();
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
    std::unique_lock<std::mutex> worker_guard (m_workers_mutex);

    for (auto it = m_workers.begin (); it != m_workers.end (); it++)
      {
	(*it)->map_context_if_running (stop, func, args...);
	if (stop)
	  {
	    // stop mapping
	    return;
	  }
      }
    worker_guard.unlock ();

    std::unique_lock<std::mutex> temp_guard (m_temp_workers_mutex);

    for (auto it = m_temp_workers.begin (); it != m_temp_workers.end (); it++)
      {
	(*it)->map_context_if_running (stop, func, args...);
	if (stop)
	  {
	    // stop mapping
	    return;
	  }
      }
    temp_guard.unlock ();
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

