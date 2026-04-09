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
  template <bool Logging>
  class worker_pool
  {
    public:
      // forward definition for nested core class
      friend class thread_manager;
      class core;

      using context_type = entry;
      using task_type = task<context_type>;

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

      // get worker pool statistics
      void get_stats (cubperf::stat_value *stats_out) const;
      // log stats to error log file
      void er_log_stats (void) const;

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
      worker_pool (std::size_t pool_size, std::size_t core_count, const char *name, entry_manager &entry_mgr,
		   bool pool_threads = false, wait_seconds wait_for_task_time = std::chrono::seconds (5));

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
  template <bool Logging>
  class worker_pool<Logging>::core
  {
    public:
      // forward definition of nested class worker
      friend class worker_pool<Logging>;
      class worker;

      virtual ~core (void);

      virtual void initialize (std::size_t worker_count);

      void set_worker_pool (worker_pool &parent);

      // task management
      // execute task
      void execute_task (task_type *task_p, bool is_temp);

      // worker management
      // notify workers to stop; if any of core's workers are still running, outputs is_not_stopped = true
      void notify_stop (bool &is_not_stopped);
      void retire_queued_tasks (void);

      // worker management
      // get a task or add worker to free active list (still running, but ready to execute another task)
      task_type *get_task_or_become_available (worker &worker_arg);
      void become_available (worker &worker_arg);
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
      core ();

      // override this if want to change worker type
      virtual std::unique_ptr<worker> allocate_worker (bool is_temp = false);

      virtual void allocate_workers (std::size_t worker_count);
      virtual void initialize_workers ();

      // execute task for method/stored procedure by recursive call; This task is not pooled and executes in a temporary created thread.
      virtual void execute_task_as_temp (task_type *task_p);

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
  template <bool Logging>
  class worker_pool<Logging>::core::worker
  {
    public:
      friend class core;

      virtual ~worker (void);

      // init
      void set_core (core &parent);

      // start thread for current worker
      void start_thread (void);

      // assign task (can be NULL) to running thread or start thread
      void assign_task (task_type *work_p);
      // run task on current thread (push_time is provided by core)
      void push_task_on_running_thread (task_type *work_p);

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
      worker (bool is_temp = false);

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
  //////////////////////////////////////////////////////////////////////////
  // worker_pool<Logging>
  //////////////////////////////////////////////////////////////////////////

  template <bool Logging>
  worker_pool<Logging>::worker_pool (std::size_t pool_size, std::size_t core_count, const char *name,
				     entry_manager &entry_mgr, bool pool_threads, wait_seconds wait_for_task_time)
    : m_name (name == NULL ? "" : name)
    , m_entry_manager (entry_mgr)
    , m_max_workers (pool_size)
    , m_round_robin_counter (0)
    , m_stopped (false)
    , m_pool_threads (pool_threads)
    , m_wait_for_task_time (wait_for_task_time)
  {
    assert (core_count > 0 && core_count <= pool_size);
  }

  template <bool Logging>
  worker_pool<Logging>::~worker_pool ()
  {
    // not safe to destroy running pools
    assert (m_stopped);
  }

  template <bool Logging>
  const std::string &
  worker_pool<Logging>::get_name (void) const
  {
    return m_name;
  }

  template <bool Logging>
  void
  worker_pool<Logging>::initialize (std::size_t worker_count, std::size_t core_count)
  {
    allocate_cores (core_count);
    assign_workers_to_cores (core_count, worker_count);

    // [optional] this option must be useful using perf
    if (wp_is_thread_always_alive_forced ())
      {
	// override pooling/wait time options to keep threads always alive
	m_pool_threads = true;
	m_wait_for_task_time.set_infinite_wait ();
      }
  }

  template <bool Logging>
  void
  worker_pool<Logging>::execute (task_type *work_arg)
  {
    execute_on_core (work_arg, get_next_core ());
  }

  template <bool Logging>
  void
  worker_pool<Logging>::execute_on_core (task_type *work_arg, std::size_t core_hash, bool is_temp)
  {
    std::size_t core_index;

    core_index = core_hash % m_cores.size ();
    m_cores[core_index]->execute_task (work_arg, is_temp);
  }

  template <bool Logging>
  bool
  worker_pool<Logging>::is_running (void) const
  {
    return !m_stopped;
  }

  template <bool Logging>
  void
  worker_pool<Logging>::stop_execution (void)
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

    auto timeout = std::chrono::system_clock::now () + time_wait_to_thread_stop;

    bool is_not_stopped;
    while (true)
      {
	// notify all cores to stop
	is_not_stopped = false;     // assume all are stopped
	for (std::size_t it = 0; it < m_cores.size (); it++)
	  {
	    // notify all workers to stop. if any worker is still running, is_not_stopped = true is output
	    m_cores[it]->notify_stop (is_not_stopped);
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
    for (std::size_t it = 0; it < m_cores.size (); it++)
      {
	m_cores[it]->retire_queued_tasks ();
      }
  }

  template <bool Logging>
  std::size_t
  worker_pool<Logging>::get_worker_count (void) const
  {
    return m_max_workers;
  }

  template <bool Logging>
  std::size_t
  worker_pool<Logging>::get_core_count (void) const
  {
    return m_cores.size ();
  }

  template <bool Logging>
  template <typename Func, typename ... Args>
  void
  worker_pool<Logging>::map_running_contexts (Func &&func, Args &&... args)
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

  template <bool Logging>
  template <typename Func, typename ... Args>
  void
  worker_pool<Logging>::map_cores (Func &&func, Args &&... args)
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

  template <bool Logging>
  std::unique_ptr<typename worker_pool<Logging>::core>
  worker_pool<Logging>::allocate_core (void)
  {
    return std::unique_ptr<core> (new core ());
  }

  template <bool Logging>
  void
  worker_pool<Logging>::allocate_cores (std::size_t core_count)
  {
    std::size_t it;

    m_cores.reserve (core_count);
    for (it = 0; it < core_count; it++)
      {
	m_cores.push_back (allocate_core ());
      }
  }

  template <bool Logging>
  void
  worker_pool<Logging>::assign_workers_to_cores (std::size_t core_count, std::size_t worker_count)
  {
    std::size_t quotient, remainder;
    std::size_t it;

    quotient = worker_count / m_cores.size ();
    remainder = worker_count % m_cores.size ();

    for (it = 0; it < remainder; it++)
      {
	m_cores[it]->set_worker_pool (*this);
	// worker pool is referenced in worker to get m_pool_threads when initializing core
	m_cores[it]->initialize (quotient + 1);
      }
    for (; it < m_cores.size (); it++)
      {
	m_cores[it]->set_worker_pool (*this);
	// worker pool is referenced in worker to get m_pool_threads when initializing core
	m_cores[it]->initialize (quotient);
      }
  }

  template <bool Logging>
  std::size_t
  worker_pool<Logging>::get_next_core (void)
  {
    return get_round_robin_core_hash ();
  }

  template <bool Logging>
  std::size_t
  worker_pool<Logging>::get_round_robin_core_hash (void)
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
  // worker_pool<Logging>::core
  //////////////////////////////////////////////////////////////////////////

  template <bool Logging>
  worker_pool<Logging>::core::core ()
    : m_parent_pool (NULL)
  {
  }

  template <bool Logging>
  worker_pool<Logging>::core::~core ()
  {
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::initialize (std::size_t worker_count)
  {
    assert (worker_count > 0);

    // resources reserve
    m_available_workers.reserve (worker_count);

    // workers
    allocate_workers (worker_count);
    initialize_workers ();
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::set_worker_pool (worker_pool &parent)
  {
    m_parent_pool = &parent;
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::execute_task (task_type *task_p, bool is_temp)
  {
    // find an available worker
    // 1. one already active is preferable
    // 2. inactive will do too
    // 3. if no workers, enqueue the task

    assert (task_p != NULL);

    worker *refp = NULL;

    if (m_parent_pool->m_stopped)
      {
	// reject task
	task_p->retire ();
	return;
      }

    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_available_workers.empty ())
      {
	refp = m_available_workers.back ();
	m_available_workers.pop_back ();
	ulock.unlock ();

	assert (refp != NULL);
	refp->assign_task (task_p);
      }
    else
      {
	if (is_temp)
	  {
	    // no need to hold the mutex (prevent deadlock)
	    ulock.unlock ();

	    execute_task_as_temp (task_p);
	  }
	else
	  {
	    // save to queue
	    m_task_queue.push (task_p);
	  }
      }
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::notify_stop (bool &is_not_stopped)
  {
    // stop all temp workers first
    std::unique_lock<std::mutex> temp_guard (m_temp_workers_mutex);

    for (auto it = m_temp_workers.begin (); it != m_temp_workers.end (); it++)
      {
	(*it)->stop_execution (is_not_stopped);
      }
    temp_guard.unlock ();

    // tell all workers to stop
    std::unique_lock<std::mutex> worker_guard (m_workers_mutex);

    for (auto it = m_workers.begin (); it != m_workers.end (); it++)
      {
	(*it)->stop_execution (is_not_stopped);
      }
    worker_guard.unlock ();
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::retire_queued_tasks (void)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    while (!m_task_queue.empty ())
      {
	m_task_queue.front ()->retire ();
	m_task_queue.pop ();
      }
  }

  template <bool Logging>
  typename worker_pool<Logging>::task_type *
  worker_pool<Logging>::core::get_task_or_become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_task_queue.empty ())
      {
	task_type *task_p = m_task_queue.front ();
	assert (task_p != NULL);
	m_task_queue.pop ();

	return task_p;
      }

    m_available_workers.push_back (&worker_arg);
    assert (m_available_workers.size () <= m_workers.size ());

    return NULL;
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    m_available_workers.push_back (&worker_arg);
    assert (m_available_workers.size () <= m_workers.size ());
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::check_worker_not_available (const worker &worker_arg)
  {
#if !defined (NDEBUG)
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    for (auto it = m_available_workers.begin (); it != m_available_workers.end (); it++)
      {
	assert (*it != &worker_arg);
      }
#endif // DEBUG
  }

  template <bool Logging>
  entry_manager &
  worker_pool<Logging>::core::get_entry_manager (void)
  {
    return m_parent_pool->m_entry_manager;
  }

  template <bool Logging>
  std::size_t
  worker_pool<Logging>::core::get_worker_count (void) const
  {
    return m_workers.size ();
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::register_free_temp_list (worker *w)
  {
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

    auto it = std::find_if (m_temp_workers.begin (), m_temp_workers.end (), [w] (const std::unique_ptr<worker> &p)
    {
      return p.get () == w;
    });

    assert (it != m_temp_workers.end ());

    m_free_temp_workers.push_back (std::move (*it));
    m_temp_workers.erase (it);
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::free_all_temp_list ()
  {
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

    m_free_temp_workers.clear ();
  }

  template <bool Logging>
  template <typename Func, typename ... Args>
  void
  worker_pool<Logging>::core::map_running_contexts (bool &stop, Func &&func, Args &&... args) const
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

  template <bool Logging>
  std::unique_ptr<typename worker_pool<Logging>::core::worker>
  worker_pool<Logging>::core::allocate_worker (bool is_temp)
  {
    return std::unique_ptr<worker> (new worker (is_temp));
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::allocate_workers (std::size_t worker_count)
  {
    std::size_t it;

    m_workers.reserve (worker_count);
    for (it = 0; it < worker_count; it++)
      {
	m_workers.push_back (allocate_worker ());
      }
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::initialize_workers ()
  {
    for (auto it = m_workers.begin (); it != m_workers.end (); it++)
      {
	(*it)->set_core (*this);

	if (m_parent_pool->get_pool_threads ())
	  {
	    // assign task / start thread
	    // it will add itself to available workers
	    (*it)->assign_task (NULL);
	  }
	else
	  {
	    // add to available workers
	    m_available_workers.push_back (it->get ());
	  }
      }
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::execute_task_as_temp (task_type *task_p)
  {
    auto w = allocate_worker (true);
    w->set_core (*this);

    std::lock_guard<std::mutex> ulock (m_temp_workers_mutex);

    m_temp_workers.push_back (std::move (w));
    m_temp_workers.back ()->assign_task (task_p);
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool<Logging>::core::worker
  //////////////////////////////////////////////////////////////////////////

  template <bool Logging>
  worker_pool<Logging>::core::worker::worker (bool is_temp)
    : m_parent_core (NULL)
    , m_context_p (NULL)
    , m_task_p (NULL)
    , m_stop (false)
    , m_has_thread (false)
    , m_is_temp (is_temp)
  {
  }

  template <bool Logging>
  worker_pool<Logging>::core::worker::~worker (void)
  {
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::set_core (core &parent)
  {
    m_parent_core = &parent;
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::start_thread (void)
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

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::assign_task (task_type *work_p)
  {
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

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::push_task_on_running_thread (task_type *work_p)
  {
    // must lock task mutex
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    assert (work_p != NULL);
    // make sure worker is in a valid state
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // set task
    m_task_p = work_p;

    // mutex is not needed for notify
    ulock.unlock ();
    // notify waiting thread
    m_task_cv.notify_one ();
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::stop_execution (bool &is_not_stopped)
  {
    context_type *context_p = m_context_p;

    if (context_p != NULL)
      {
	// notify context to stop
	m_parent_core->get_entry_manager ().stop_execution (*context_p);
      }

    // make sure thread is not waiting for tasks
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    if (m_has_thread)
      {
	// this thread is still running
	is_not_stopped = true;
      }

    // stop worker
    m_stop = true;
    // mutex is not needed for notify
    ulock.unlock ();

    if (m_is_temp)
      {
	// not to notify one if it is for temp
	return;
      }
    m_task_cv.notify_one ();
  }

  template <bool Logging>
  template <typename Func, typename ... Args>
  void
  worker_pool<Logging>::core::worker::map_context_if_running (bool &stop, Func &&func, Args &&... args)
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

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::run (void)
  {
    // clear the affinity at start
    os::resources::cpu::clearaffinity ();
    pthread_setname_np (pthread_self (), m_parent_core->get_parent_pool ()->get_name ().c_str ());

    // do stuff at the beginning like creating context
    init_run ();

    // do task and terminate if this is temp worker
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

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::init_run (void)
  {
    // safe-guard - we have a thread
    assert (m_has_thread);

#if !defined (NDEBUG)
    // safe-guard - threads should [no longer] be available
    if (!m_is_temp)
      {
	m_parent_core->check_worker_not_available (*this);
      }
#endif

    // a context is required
    m_context_p = &m_parent_core->get_entry_manager ().create_context ();
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::finish_run (void)
  {
    assert (m_task_p == NULL);
    assert (m_context_p != NULL);

    // retire context
    m_parent_core->get_entry_manager ().retire_context (*m_context_p);
    m_context_p = NULL;

    if (m_is_temp)
      {
	m_parent_core->register_free_temp_list (this);
      }
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::execute_current_task (void)
  {
    assert (m_task_p != NULL);

    // execute task
    m_task_p->execute (*m_context_p);

    // and retire task
    retire_current_task ();

    // and recycle context before getting another task
    m_parent_core->get_entry_manager ().recycle_context (*m_context_p);

    // notify core one task was finished
    if (m_is_temp == false)
      {
	m_parent_core->free_all_temp_list ();
      }
  }

  template <bool Logging>
  void
  worker_pool<Logging>::core::worker::retire_current_task (void)
  {
    assert (m_task_p != NULL);

    // retire task
    m_task_p->retire ();
    m_task_p = NULL;
  }

  template <bool Logging>
  bool
  worker_pool<Logging>::core::worker::get_new_task (void)
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
	return true;
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

