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
 * thread_worker_pool_impl.hpp
 */

#ifndef _THREAD_WORKER_POOL_IMPL_HPP_
#define _THREAD_WORKER_POOL_IMPL_HPP_

#if !defined (SERVER_MODE)
#error Wrong module
#endif // not SERVER_MODE

// same module include
#include "thread_task.hpp"
#include "thread_entry.hpp"
#include "thread_worker_pool.hpp"

// cubrid includes
#include "perf.hpp"
#include "resources.hpp"
#include "error_manager.h"

// system includes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <optional>
#include <algorithm>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <system_error>
#include <thread>
#include <sstream>
#include <cassert>
#include <cstring>
#include <pthread.h>

#ifndef TASK_COMM_LEN
#define TASK_COMM_LEN 16
#endif

namespace cubthread
{
  // cubthread::worker_pool_impl<Stats>
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
  //    cubthread::worker_pool<true> thread_pool (...);
  //    cubthread::worker_pool<false> thread_pool (...);
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
  template <bool Stats>
  class worker_pool_impl : public worker_pool
  {
    public:
      // forward definition for nested core class
      friend class manager;

    public:
      // forward definition
      class core_impl;
      class wrapped_task;
      class stats_base;
      class stats;

      virtual ~worker_pool_impl ();

      // init
      virtual void initialize (std::size_t worker_count, std::size_t core_count) override;

      // execute task; execution is guaranteed, even if maximum number of tasks is reached.
      void execute (task_type *work_arg) override;

      // execute on give core.
      virtual void execute_on_core (task_type *work_arg, std::size_t core_hash, bool is_temp = false) override;

      // ensure every available worker has a live thread waiting for tasks.
      // workers currently executing a task are skipped — they already have a thread.
      void warmup (void) override;

      // stop worker pool; stop all running threads; discard any tasks in queue
      void stop_execution (void) override;

      // worker is stopped after stop_execution () is called
      bool is_running (void) const override;

      // get maximum number of threads that can run concurrently in this worker pool
      std::size_t get_worker_count (void) const override;
      // get the number of cores
      std::size_t get_core_count (void) const override;

      // get worker pool statistics
      void get_stats (cubperf::stat_value *stats_out) const override;
      // log stats to error log file

      void er_log_stats (void) const;

      bool get_pool_threads () const
      {
	return m_pool_threads;
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
      worker_pool_impl (std::size_t pool_size, std::size_t core_count, const char *name, entry_manager &entry_mgr,
			bool pool_threads = false, wait_seconds idle_timeout = std::chrono::seconds (5));

      // override this if want to change core type
      virtual std::unique_ptr<core> allocate_core (bool pool_threads);

      virtual void allocate_cores (std::size_t core_count);
      virtual void assign_workers_to_cores (std::size_t worker_count);

      // get next core by policy
      virtual std::size_t get_next_core (void);
      // get next core by round robin scheduling (default policy)
      std::size_t get_round_robin_core_hash (void);

      // core variables
      std::vector<std::unique_ptr<core>> m_cores;

      // maximum number of concurrent workers
      std::size_t m_max_workers;

      // set to true when stopped
      std::atomic<bool> m_stopped;

      // [optional] round robin counter used to dispatch tasks on cores
      std::atomic<std::size_t> m_round_robin_counter;
  };

  // worker_pool_impl<Stats>::stats_base
  //
  // description
  //    empty if Stats is false
  //
  template <bool Stats>
  class worker_pool_impl<Stats>::stats_base
  {
    public:
      stats_base () = default;
      ~stats_base () = default;

      stats_base (const stats_base &) = delete;
      stats_base &operator= (const stats_base &) = delete;
      stats_base (stats_base &&) = default;
      stats_base &operator= (stats_base &&) = default;

      // empty base
  };

  template <>
  class worker_pool_impl<true>::stats_base
  {
    public:
      stats_base ();
      ~stats_base ();

      stats_base (const stats_base &) = delete;
      stats_base &operator= (const stats_base &) = delete;

      stats_base (stats_base &&other);
      stats_base &operator= (stats_base &&other);

      // stats
      cubperf::statset *statset;
      // timing
      cubperf::time_point time;
  };

  // worker_pool_impl<Stats>::core
  //
  // description
  //    a worker pool core execution. manages a sub-group of workers.
  //    acts as middleman between worker pool and workers
  //
  //    task in: execute_task
  //    task out: execute_task (immediately execute) or get_task_or_become_available (queued task)
  //
  template <bool Stats>
  class worker_pool_impl<Stats>::core_impl : public worker_pool::core
  {
      friend class worker_pool_impl;

    public:
      // forward definition of nested class worker
      class worker_impl;

      virtual ~core_impl (void);

      virtual void initialize (std::size_t worker_count) override;

      // execute task
      void execute_task (task_type *task_p, bool is_temp) override;

      // ensure every available worker has a live thread waiting for tasks.
      // workers currently executing a task are skipped — they already have a thread.
      void warmup (void) override;

      // notify workers to stop; if any of core's workers are still running, returns true
      bool stop_execution (void) override;
      void retire_queued_tasks (void);

      // get a task or add worker to free active list (still running, but ready to execute another task)
      std::optional<wrapped_task> get_task_or_become_available (worker &worker_arg);
      void become_available (worker &worker_arg);

      // is worker available?
      void check_worker_not_available (const worker &worker_arg);

      std::size_t get_worker_count (void) const override;

      // temp worker
      void register_free_temp_list (worker *w);
      void free_all_temp_list ();

      // stats
      void get_stats (cubperf::stat_value *stats_out) const override;

      // context management
      // map function to all workers (and their contexts)
      template <typename Func, typename ... Args>
      void map_running_contexts (bool &stop, Func &&func, Args &&... args) const;

    protected:
      core_impl (bool pool_threads);

      // override this if want to change worker type
      virtual std::unique_ptr<worker> allocate_worker (bool is_temp = false);

      virtual void allocate_workers (std::size_t worker_count);
      virtual void initialize_workers ();

      // execute task for method/stored procedure by recursive call; This task is not pooled and executes in a temporary created thread.
      virtual void execute_task_as_temp (wrapped_task &&task_ref);

      std::vector<std::unique_ptr<worker>> m_workers;
      std::vector<worker *> m_available_workers;
      std::queue<wrapped_task> m_task_queue;
      // mutex to synchronize activity on worker lists
      mutable std::mutex m_workers_mutex;

      // temporary executed workers for method/stored procedure
      std::vector<std::unique_ptr<worker>> m_temp_workers;
      std::vector<std::unique_ptr<worker>> m_free_temp_workers;
      // mutex to synchronize temp worker lists
      mutable std::mutex m_temp_workers_mutex;
  };

  // worker_pool_impl<Stats>::core_impl::worker_impl
  //
  // description
  //    the worker is a worker pool nested class and represents one instance of execution. its purpose is to store the
  //    context, manage multiple task executions of a single thread and collect statistics.
  //
  template <bool Stats>
  class worker_pool_impl<Stats>::core_impl::worker_impl : public worker_pool::core::worker
  {
      friend class core_impl;

    public:
      virtual ~worker_impl (void);

      // init
      void initialize () override;

      // start thread for current worker
      void start_thread (void);
      bool has_thread (void);

      // assign task to worker; wake a running thread or start a new one.
      void assign_task (wrapped_task &&task_ref);
      // [optional] used only to prestart pooled threads.
      void assign_task (void);

      // stop execution; if worker has a thread running, returns true
      bool stop_execution (void) override;

      std::mutex &get_mutex (void)
      {
	return m_task_mutex;
      }

      // stats
      void get_stats (cubperf::stat_value *stats_out) const override;

      // map function to context (if a task is running and if context is available)
      //
      // note - sometimes a thread has a context assigned, but it is waiting for tasks. if that's the case, the
      //        function will not be applied, since it is not considered a "running" context.
      //
      template <typename Func, typename ... Args>
      void map_context_if_running (bool &stop, Func &&func, Args &&... args);

    protected:
      worker_impl (bool is_temp = false);

      // run function invoked by spawned thread
      void run (void);

      // run initialization (creating execution context)
      void init_run (void);
      // finishing initialization (retiring execution context, worker becomes inactive)
      void finish_run (void);

      // execute m_wrapped_task
      void execute_current_task (void);
      // retire m_wrapped_task
      void retire_current_task (void);
      // get new task from 1. worker pool task queue or 2. wait for incoming tasks
      bool get_new_task (void);

      context_type *m_context_p;		    // execution context (same lifetime as spawned thread)
      std::optional<wrapped_task> m_wrapped_task;   // current task and metadata

      // synchronization on task wait
      std::condition_variable m_task_cv;	    // condition variable used to notify when a task is assigned or when
      // worker is stopped
      std::mutex m_task_mutex;			    // mutex to protect waiting task condition

      bool m_stop;				    // stop execution (set to true when worker pool is stopped)
      bool m_has_thread;			    // true if worker has a thread running

      bool m_is_temp;				    // true if worker is for temp task

      stats_base m_stats;			    // bool if Stats is false
  };

  // worker_pool_impl<Stats>::wrapped_task
  //
  // description
  //    wrapper task for timing.
  //
  template <bool Stats>
  class worker_pool_impl<Stats>::wrapped_task
  {
    private:
      struct task_only
      {
	task_type *task;
      };

      struct task_with_stats
      {
	task_type *task;

	cubperf::time_point time;

	// Other stats might be added here
      };

      using inner_type = typename std::conditional_t<Stats, task_with_stats, task_only>;

    public:
      explicit wrapped_task (task_type *task_p);
      ~wrapped_task ();

      wrapped_task (const wrapped_task &) = delete;
      wrapped_task &operator= (const wrapped_task &) = delete;

      wrapped_task (wrapped_task &&other);
      wrapped_task &operator= (wrapped_task &&other) = delete;

      cubperf::time_point &get_time (void);

      // helper
      void execute (context_type &thread_ref);
      void retire (void);

    private:
      inner_type m_inner;
  };

  //////////////////////////////////////////////////////////////////////////
  // statistics
  //////////////////////////////////////////////////////////////////////////

  template <bool Stats>
  class worker_pool_impl<Stats>::stats
  {
    public:
      enum class id : cubperf::stat_id
      {
	start_thread = 0,
	create_context = 1,
	execute_task = 2,
	retire_task = 3,
	found_in_queue = 4,
	wakeup_with_task = 5,
	recycle_context = 6,
	retire_context = 7,

	// must be last
	type_count
      };

      static const cubperf::statset_definition statdef;

      static constexpr cubperf::stat_definition make_def (id stat_id, cubperf::stat_definition::type stat_type,
	  const char *first_name, const char *second_name);

      static stats_base create (void);
      static void destroy (stats_base &base);

      static void time_and_increment (stats_base &base, id stat_id);
      static void time_and_increment (stats_base &base, id stat_id, cubperf::duration d);
      static void accumulate (const stats_base &base, cubperf::stat_value *where);

      static std::size_t get_count (void);
      static const char *get_name (std::size_t stat_index);

    private:
      stats () = delete;
  };

  //////////////////////////////////////////////////////////////////////////
  // base functions
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
  // worker_pool_impl<Stats>
  //////////////////////////////////////////////////////////////////////////

  template <bool Stats>
  worker_pool_impl<Stats>::worker_pool_impl (std::size_t pool_size, std::size_t core_count, const char *name,
      entry_manager &entry_mgr, bool pool_threads, wait_seconds idle_timeout)
    : worker_pool (name, entry_mgr, pool_threads, idle_timeout)
    , m_max_workers (pool_size)
    , m_stopped (false)
    , m_round_robin_counter (0)
  {
    assert (core_count > 0 && core_count <= pool_size);

    // [optional] this option must be useful using perf
    if (wp_is_thread_always_alive_forced ())
      {
	// override pooling/wait time options to keep threads always alive
	m_pool_threads = true;
	m_idle_timeout.set_infinite_wait ();
      }
  }

  template <bool Stats>
  worker_pool_impl<Stats>::~worker_pool_impl ()
  {
    // not safe to destroy running pools
    assert (m_stopped);
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::initialize (std::size_t worker_count, std::size_t core_count)
  {
    allocate_cores (core_count);
    assign_workers_to_cores (worker_count);
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::execute (task_type *work_arg)
  {
    execute_on_core (work_arg, get_next_core ());
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::execute_on_core (task_type *work_arg, std::size_t core_hash, bool is_temp)
  {
    std::size_t core_index;

    core_index = core_hash % m_cores.size ();
    m_cores[core_index]->execute_task (work_arg, is_temp);
  }

  template <bool Stats>
  bool
  worker_pool_impl<Stats>::is_running (void) const
  {
    return !m_stopped;
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::warmup (void)
  {
    for (auto &it : m_cores)
      {
	it->warmup ();
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::stop_execution (void)
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
    bool has_running_workers;

    while (true)
      {
	// notify all cores to stop
	has_running_workers = false;

	for (const auto &it : m_cores)
	  {
	    // notify all workers to stop. if any worker is still running, is_not_stopped = true is output
	    has_running_workers |= it->stop_execution ();
	  }

	if (!has_running_workers)
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
    for (const auto &it : m_cores)
      {
	assert (dynamic_cast<core_impl *> (it.get ()));

	static_cast<core_impl *> (it.get ())->retire_queued_tasks ();
      }
  }

  template <bool Stats>
  std::size_t
  worker_pool_impl<Stats>::get_worker_count (void) const
  {
    return m_max_workers;
  }

  template <bool Stats>
  std::size_t
  worker_pool_impl<Stats>::get_core_count (void) const
  {
    return m_cores.size ();
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::get_stats (cubperf::stat_value *stats_out) const
  {
    for (const auto &it : m_cores)
      {
	it->get_stats (stats_out);
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::er_log_stats (void) const
  {
    if constexpr (Stats)
      {
	std::vector<cubperf::stat_value> statset (stats::get_count (), 0);
	std::stringstream ss;

	std::memset (statset.data (), 0, stats::get_count () * sizeof (cubperf::stat_value));
	get_stats (statset.data ());

	ss << "Worker pool statistics: " << m_name << std::endl;

	for (std::size_t index = 0; index < stats::get_count (); index++)
	  {
	    ss << "\t" << stats::get_name (index) << ": ";
	    ss << statset[index] << std::endl;
	  }

	_er_log_debug (ARG_FILE_LINE, ss.str ().c_str ());
      }
  }

  template <bool Stats>
  template <typename Func, typename ... Args>
  void
  worker_pool_impl<Stats>::map_running_contexts (Func &&func, Args &&... args)
  {
    bool stop = false;
    for (const auto &it : m_cores)
      {
	assert (dynamic_cast<core_impl *> (it.get ()));

	static_cast<core_impl *> (it.get ())->map_running_contexts (stop, func, args...);
	if (stop)
	  {
	    // mapping is stopped
	    return;
	  }
      }
  }

  template <bool Stats>
  template <typename Func, typename ... Args>
  void
  worker_pool_impl<Stats>::map_cores (Func &&func, Args &&... args)
  {
    bool stop = false;
    for (const auto &it : m_cores)
      {
	func (*static_cast<core_impl *> (it.get ()), stop, args...);
	if (stop)
	  {
	    // mapping is stopped
	    return;
	  }
      }
  }

  template <bool Stats>
  std::unique_ptr<typename worker_pool::core>
  worker_pool_impl<Stats>::allocate_core (bool pool_threads)
  {
    return std::unique_ptr<core> (new core_impl (pool_threads));
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::allocate_cores (std::size_t core_count)
  {
    std::size_t it;

    m_cores.reserve (core_count);
    for (it = 0; it < core_count; it++)
      {
	m_cores.push_back (allocate_core (m_pool_threads));
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::assign_workers_to_cores (std::size_t worker_count)
  {
    std::size_t quotient, remainder;
    std::size_t it;

    quotient = worker_count / m_cores.size ();
    remainder = worker_count % m_cores.size ();

    for (it = 0; it < remainder; it++)
      {
	m_cores[it]->set_parent_pool (*this);
	m_cores[it]->initialize (quotient + 1);
      }
    for (; it < m_cores.size (); it++)
      {
	m_cores[it]->set_parent_pool (*this);
	m_cores[it]->initialize (quotient);
      }
  }

  template <bool Stats>
  std::size_t
  worker_pool_impl<Stats>::get_next_core (void)
  {
    return get_round_robin_core_hash ();
  }

  template <bool Stats>
  std::size_t
  worker_pool_impl<Stats>::get_round_robin_core_hash (void)
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
  // worker_pool_impl<Stats>::stats_base
  //////////////////////////////////////////////////////////////////////////

  inline
  worker_pool_impl<true>::stats_base::stats_base ()
    : statset (nullptr)
    , time ()
  {
  }

  inline
  worker_pool_impl<true>::stats_base::~stats_base ()
  {
  }

  inline
  worker_pool_impl<true>::stats_base::stats_base (stats_base &&other)
    : statset (other.statset)
    , time (other.time)
  {
    other.statset = nullptr;
  }

  inline worker_pool_impl<true>::stats_base &
  worker_pool_impl<true>::stats_base::operator= (stats_base &&other)
  {
    if (this != &other)
      {
	delete statset;
	statset = other.statset;
	time = other.time;
	other.statset = nullptr;
      }
    return *this;
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_impl<Stats>::core_impl
  //////////////////////////////////////////////////////////////////////////

  template <bool Stats>
  worker_pool_impl<Stats>::core_impl::core_impl (bool pool_threads)
    : worker_pool::core (pool_threads)
  {
  }

  template <bool Stats>
  worker_pool_impl<Stats>::core_impl::~core_impl ()
  {
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::initialize (std::size_t worker_count)
  {
    assert (worker_count > 0);

    // resources reserve
    m_available_workers.reserve (worker_count);

    // workers
    allocate_workers (worker_count);
    initialize_workers ();
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::execute_task (task_type *task_p, bool is_temp)
  {
    // find an available worker
    // 1. one already active is preferable
    // 2. inactive will do too
    // 3. if no workers, enqueue the task

    assert (task_p != nullptr);

    worker_impl *refp = nullptr;

    if (!m_parent_pool->is_running ())
      {
	// reject task
	task_p->retire ();
	return;
      }

    wrapped_task task_ref (task_p);
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_available_workers.empty ())
      {
	refp = static_cast<worker_impl *> (m_available_workers.back ());
	m_available_workers.pop_back ();
	ulock.unlock ();

	assert (refp != nullptr);

	refp->assign_task (std::move (task_ref));
      }
    else
      {
	if (is_temp)
	  {
	    // no need to hold the mutex (prevent deadlock)
	    ulock.unlock ();

	    execute_task_as_temp (std::move (task_ref));
	  }
	else
	  {
	    // save to queue
	    m_task_queue.push (std::move (task_ref));
	  }
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::warmup (void)
  {
    std::lock_guard<std::mutex> lock (m_workers_mutex);
    worker_impl *w;

    for (auto it = m_available_workers.begin (); it != m_available_workers.end (); )
      {
	assert (dynamic_cast<worker_impl *> (*it));
	w = static_cast<worker_impl *> (*it);
	if (!w->has_thread ())
	  {
	    w->assign_task ();
	    it = m_available_workers.erase (it);
	  }
	else
	  {
	    ++it;
	  }
      }
  }

  template <bool Stats>
  bool
  worker_pool_impl<Stats>::core_impl::stop_execution (void)
  {
    bool has_running_workers = false;

    // stop all temp workers first
    {
      std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

      for (const auto &it : m_temp_workers)
	{
	  has_running_workers |= it->stop_execution ();
	}
    }

    // tell all workers to stop
    {
      std::unique_lock<std::mutex> ulock (m_workers_mutex);

      for (const auto &it : m_workers)
	{
	  has_running_workers |= it->stop_execution ();
	}
    }

    return has_running_workers;
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::retire_queued_tasks (void)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    while (!m_task_queue.empty ())
      {
	wrapped_task queued_task = std::move (m_task_queue.front ());
	m_task_queue.pop ();
	queued_task.retire ();
      }
  }

  template <bool Stats>
  std::optional<typename worker_pool_impl<Stats>::wrapped_task>
  worker_pool_impl<Stats>::core_impl::get_task_or_become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    if (!m_task_queue.empty ())
      {
	wrapped_task queued_task = std::move (m_task_queue.front ());
	m_task_queue.pop ();

	return std::optional<wrapped_task> (std::in_place, std::move (queued_task));
      }

    m_available_workers.push_back (&worker_arg);
    assert (m_available_workers.size () <= m_workers.size ());

    return std::nullopt;
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::become_available (worker &worker_arg)
  {
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    m_available_workers.push_back (&worker_arg);
    assert (m_available_workers.size () <= m_workers.size ());
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::check_worker_not_available (const worker &worker_arg)
  {
#if !defined (NDEBUG)
    std::unique_lock<std::mutex> ulock (m_workers_mutex);

    for (const auto it : m_available_workers)
      {
	assert (it != &worker_arg);
      }
#endif // DEBUG
  }

  template <bool Stats>
  std::size_t
  worker_pool_impl<Stats>::core_impl::get_worker_count (void) const
  {
    return m_workers.size ();
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::register_free_temp_list (worker *w)
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

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::free_all_temp_list ()
  {
    std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

    m_free_temp_workers.clear ();
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::get_stats (cubperf::stat_value *stats_out) const
  {
    {
      std::unique_lock<std::mutex> ulock (m_workers_mutex);

      for (const auto &it : m_workers)
	{
	  it->get_stats (stats_out);
	}
    }

    {
      std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

      for (const auto &it : m_temp_workers)
	{
	  it->get_stats (stats_out);
	}
    }
  }

  template <bool Stats>
  template <typename Func, typename ... Args>
  void
  worker_pool_impl<Stats>::core_impl::map_running_contexts (bool &stop, Func &&func, Args &&... args) const
  {
    {
      std::unique_lock<std::mutex> ulock (m_workers_mutex);

      for (const auto &worker : m_workers)
	{
	  assert (dynamic_cast<worker_impl *> (worker.get ()));

	  static_cast<worker_impl *> (worker.get ())->map_context_if_running (stop, func, args...);
	  if (stop)
	    {
	      // stop mapping
	      return;
	    }
	}
    }

    {
      std::unique_lock<std::mutex> ulock (m_temp_workers_mutex);

      for (const auto &worker : m_temp_workers)
	{
	  assert (dynamic_cast<worker_impl *> (worker.get ()));

	  static_cast<worker_impl *> (worker.get ())->map_context_if_running (stop, func, args...);
	  if (stop)
	    {
	      // stop mapping
	      return;
	    }
	}
    }
  }

  template <bool Stats>
  std::unique_ptr<typename worker_pool::core::worker>
  worker_pool_impl<Stats>::core_impl::allocate_worker (bool is_temp)
  {
    return std::unique_ptr<worker> (new worker_impl (is_temp));
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::allocate_workers (std::size_t worker_count)
  {
    std::size_t it;

    m_workers.reserve (worker_count);
    for (it = 0; it < worker_count; it++)
      {
	m_workers.push_back (allocate_worker (false));
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::initialize_workers ()
  {
    for (const auto &worker : m_workers)
      {
	worker->set_parent_core (*this);

	if (m_pool_threads)
	  {
	    assert (dynamic_cast<worker_impl *> (worker.get ()));

	    // assign task / start thread
	    // it will add itself to available workers
	    static_cast<worker_impl *> (worker.get ())->assign_task ();
	  }
	else
	  {
	    // add to available workers
	    m_available_workers.push_back (worker.get ());
	  }
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::execute_task_as_temp (wrapped_task &&task_ref)
  {
    auto w = allocate_worker (true);
    w->set_parent_core (*this);

    std::lock_guard<std::mutex> ulock (m_temp_workers_mutex);

    m_temp_workers.push_back (std::move (w));
    static_cast<worker_impl *> (m_temp_workers.back ().get ())->assign_task (std::move (task_ref));
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_impl<Stats>::core_impl::worker_impl
  //////////////////////////////////////////////////////////////////////////

  template <bool Stats>
  worker_pool_impl<Stats>::core_impl::worker_impl::worker_impl (bool is_temp)
    : worker_pool::core::worker ()
    , m_context_p (nullptr)
    , m_wrapped_task (std::nullopt)
    , m_stop (false)
    , m_has_thread (false)
    , m_is_temp (is_temp)
    , m_stats (stats::create ())
  {
  }

  template <bool Stats>
  worker_pool_impl<Stats>::core_impl::worker_impl::~worker_impl (void)
  {
    stats::destroy (m_stats);
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::initialize (void)
  {
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::start_thread (void)
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

    auto lambda_create = [&] (void) -> void { t = std::thread (&worker_impl::run, this); };
    auto lambda_detach = [&] (void) -> void { t.detach (); };

    wp_call_func_throwing_system_error ("starting thread", lambda_create);
    wp_call_func_throwing_system_error ("detaching thread", lambda_detach);
  }

  template <bool Stats>
  bool
  worker_pool_impl<Stats>::core_impl::worker_impl::has_thread (void)
  {
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    return m_has_thread;
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::assign_task (wrapped_task &&task_ref)
  {
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    assert (!m_wrapped_task.has_value ());

    // save task
    m_wrapped_task.emplace (std::move (task_ref));

    if (m_is_temp)
      {
	m_has_thread = true;
	assert (m_context_p == nullptr);
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

	assert (m_context_p == nullptr);

	start_thread ();
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::assign_task (void)
  {
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    assert (!m_wrapped_task.has_value ());
    assert (!m_is_temp);

    if (m_has_thread)
      {
	ulock.unlock ();
      }
    else
      {
	m_has_thread = true;
	ulock.unlock ();

	assert (m_context_p == nullptr);

	start_thread ();
      }
  }

  template <bool Stats>
  bool
  worker_pool_impl<Stats>::core_impl::worker_impl::stop_execution (void)
  {
    context_type *context_p = m_context_p;
    bool has_thread = false;

    if (context_p != nullptr)
      {
	// notify context to stop
	m_parent_core->get_entry_manager ().stop_execution (*context_p);
      }

    // make sure thread is not waiting for tasks
    std::unique_lock<std::mutex> ulock (m_task_mutex);

    if (m_has_thread)
      {
	// this thread is still running
	has_thread = true;
      }

    // stop worker
    m_stop = true;
    // mutex is not needed for notify
    ulock.unlock ();

    // The temp worker doesn't wait on task_cv; it executes the task immediately and terminates.
    if (!m_is_temp)
      {
	m_task_cv.notify_one ();
      }
    return has_thread;
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::get_stats (cubperf::stat_value *stats_out) const
  {
    stats::accumulate (m_stats, stats_out);
  }

  template <bool Stats>
  template <typename Func, typename ... Args>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::map_context_if_running (bool &stop, Func &&func, Args &&... args)
  {
    if (!m_wrapped_task.has_value ())
      {
	// not running
	return;
      }

    context_type *ctxp = m_context_p;

    if (ctxp != nullptr)
      {
	func (*ctxp, stop, args...);
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::run (void)
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

    if (!m_wrapped_task.has_value ())
      {
	// started without task; get one
	if (get_new_task ())
	  {
	    assert (m_wrapped_task.has_value ());
	  }
      }

    if (m_wrapped_task.has_value ())
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
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::init_run (void)
  {
    // safe-guard - we have a thread
    assert (m_has_thread);

#if !defined (NDEBUG)
    // safe-guard - threads should [no longer] be available
    if (!m_is_temp)
      {
	assert (dynamic_cast<core_impl *> (m_parent_core));

	static_cast<core_impl *> (m_parent_core)->check_worker_not_available (*this);
      }
#endif

    // stats: start thread
    stats::time_and_increment (m_stats, stats::id::start_thread);

    // a context is required
    m_context_p = &m_parent_core->get_entry_manager ().create_context ();

    // stats: context create
    stats::time_and_increment (m_stats, stats::id::create_context);
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::finish_run (void)
  {
    assert (!m_wrapped_task.has_value ());
    assert (m_context_p != nullptr);

    // retire context
    m_parent_core->get_entry_manager ().retire_context (*m_context_p);
    m_context_p = nullptr;

    // stats: context retire
    stats::time_and_increment (m_stats, stats::id::retire_context);

    if (m_is_temp)
      {
	assert (dynamic_cast<core_impl *> (m_parent_core));

	static_cast<core_impl *> (m_parent_core)->register_free_temp_list (this);
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::execute_current_task (void)
  {
    assert (m_wrapped_task.has_value ());

    // execute task
    m_wrapped_task->execute (*m_context_p);
    // stats: execute task
    stats::time_and_increment (m_stats, stats::id::execute_task);

    // and retire task
    retire_current_task ();

    // and recycle context before getting another task
    m_parent_core->get_entry_manager ().recycle_context (*m_context_p);
    // stats: context recycle
    stats::time_and_increment (m_stats, stats::id::recycle_context);

    // notify core one task was finished
    if (m_is_temp == false)
      {
	assert (dynamic_cast<core_impl *> (m_parent_core));

	static_cast<core_impl *> (m_parent_core)->free_all_temp_list ();
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::core_impl::worker_impl::retire_current_task (void)
  {
    assert (m_wrapped_task.has_value ());

    // retire task
    m_wrapped_task->retire ();
    m_wrapped_task = std::nullopt;

    // stats: retire task
    stats::time_and_increment (m_stats, stats::id::retire_task);
  }

  template <bool Stats>
  bool
  worker_pool_impl<Stats>::core_impl::worker_impl::get_new_task (void)
  {
    assert (!m_wrapped_task.has_value ());
    assert (dynamic_cast<core_impl *> (m_parent_core));

    std::unique_lock<std::mutex> ulock (m_task_mutex, std::defer_lock);

    // check stop condition
    if (!m_stop)
      {
	// get a queued task or wait for one to come

	// either get a queued task or add to free active list
	// note: returned task cannot be saved directly to m_task_p. if worker is added to wait queue and nullptr is returned,
	//       current thread may be preempted. worker is then claimed from free active list and worker is assigned
	//       a task. this changes expected behavior and can have unwanted consequences.

	std::optional<wrapped_task> queued_task =
		static_cast<core_impl *> (m_parent_core)->get_task_or_become_available (*this);
	if (queued_task.has_value ())
	  {
	    // stats: found in queue
	    stats::time_and_increment (m_stats, stats::id::found_in_queue);

	    // it is safe to set here
	    m_wrapped_task.emplace (std::move (*queued_task));
	    return true;
	  }

	// wait for task
	ulock.lock ();
	if (!m_wrapped_task.has_value () && !m_stop)
	  {
	    // wait until a task is received or stopped ...
	    // ... or time out
	    condvar_wait (m_task_cv, ulock, m_parent_core->get_parent_pool ()->get_idle_timeout (),
			  [this] () -> bool { return m_wrapped_task.has_value () || m_stop; });
	  }
	else
	  {
	    // no need to wait
	  }
      }
    else
      {
	// we need to add to available list
	static_cast<core_impl *> (m_parent_core)->become_available (*this);

	ulock.lock ();
      }

    // did I get a task?
    if (!m_wrapped_task.has_value ())
      {
	// no; this thread will stop. from this point forward, if a new task is assigned, a new thread must be spawned
	m_has_thread = false;

	// we need to retire context before another thread uses this worker
	finish_run ();

	return false;
      }
    else
      {
	// unlock mutex
	ulock.unlock ();

	// safe-guard - threads should no longer be available
	static_cast<core_impl *> (m_parent_core)->check_worker_not_available (*this);

	// stats: wake up with task
	stats::time_and_increment (m_stats, stats::id::wakeup_with_task);

	// found task
	return true;
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // worker_pool_impl<Stats>::wrapped_task
  //////////////////////////////////////////////////////////////////////////

  template <bool Stats>
  worker_pool_impl<Stats>::wrapped_task::wrapped_task (task_type *task_p)
  {
    assert (task_p != nullptr);

    m_inner.task = task_p;
    if constexpr (Stats)
      {
	m_inner.time = cubperf::clock::now ();
      }
  }

  template <bool Stats>
  worker_pool_impl<Stats>::wrapped_task::~wrapped_task (void)
  {
    assert (m_inner.task == nullptr);
  }

  template <bool Stats>
  worker_pool_impl<Stats>::wrapped_task::wrapped_task (wrapped_task &&other)
  {
    m_inner.task = other.m_inner.task;
    other.m_inner.task = nullptr;

    if constexpr (Stats)
      {
	m_inner.time = other.m_inner.time;
      }
  }

  template <bool Stats>
  cubperf::time_point &
  worker_pool_impl<Stats>::wrapped_task::get_time (void)
  {
    static_assert (Stats, "get_time() requires Stats == true");

    return m_inner.time;
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::wrapped_task::execute (context_type &thread_ref)
  {
    assert (m_inner.task != nullptr);

    m_inner.task->execute (thread_ref);
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::wrapped_task::retire (void)
  {
    assert (m_inner.task != nullptr);

    m_inner.task->retire ();
    m_inner.task = nullptr;
  }

  //////////////////////////////////////////////////////////////////////////
  // statistics
  //////////////////////////////////////////////////////////////////////////

  template <bool Stats>
  constexpr cubperf::stat_definition worker_pool_impl<Stats>::stats::make_def (id stat_id,
      cubperf::stat_definition::type stat_type, const char *first_name, const char *second_name)
  {
    return cubperf::stat_definition (static_cast<cubperf::stat_id> (stat_id), stat_type, first_name, second_name);
  }

  template <bool Stats>
  inline const cubperf::statset_definition worker_pool_impl<Stats>::stats::statdef =
  {
    make_def (id::start_thread, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_start_thread", "Timer_start_thread"),
    make_def (id::create_context, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_create_context", "Timer_create_context"),
    make_def (id::execute_task, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_execute_task", "Timer_execute_task"),
    make_def (id::retire_task, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_retire_task", "Timer_retire_task"),
    make_def (id::found_in_queue, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_found_task_in_queue", "Timer_found_task_in_queue"),
    make_def (id::wakeup_with_task, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_wakeup_with_task", "Timer_wakeup_with_task"),
    make_def (id::recycle_context, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_recycle_context", "Timer_recycle_context"),
    make_def (id::retire_context, cubperf::stat_definition::COUNTER_AND_TIMER,
	      "Counter_retire_context", "Timer_retire_context")
  };

  template <bool Stats>
  typename worker_pool_impl<Stats>::stats_base
  worker_pool_impl<Stats>::stats::create (void)
  {
    stats_base base;

    if constexpr (Stats)
      {
	base.statset = statdef.create_statset ();
	base.time = cubperf::clock::now ();
      }
    return base;
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::stats::destroy (stats_base &base)
  {
    if constexpr (Stats)
      {
	delete base.statset;
	base.statset = nullptr;
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::stats::time_and_increment (stats_base &base, id stat_id)
  {
    if constexpr (Stats)
      {
	base.statset->m_timept = base.time;
	statdef.time_and_increment (*base.statset, static_cast<cubperf::stat_id> (stat_id));
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::stats::time_and_increment (stats_base &base, id stat_id, cubperf::duration d)
  {
    if constexpr (Stats)
      {
	base.statset->m_timept = base.time;
	statdef.time_and_increment (*base.statset, static_cast<cubperf::stat_id> (stat_id), d);
      }
  }

  template <bool Stats>
  void
  worker_pool_impl<Stats>::stats::accumulate (const stats_base &base, cubperf::stat_value *where)
  {
    if constexpr (Stats)
      {
	statdef.add_stat_values_with_converted_timers<std::chrono::microseconds> (*base.statset, where);
      }
  }

  template <bool Stats>
  std::size_t
  worker_pool_impl<Stats>::stats::get_count (void)
  {
    if constexpr (Stats)
      {
	return statdef.get_value_count ();
      }
    else
      {
	return 0;
      }
  }

  template <bool Stats>
  const char *
  worker_pool_impl<Stats>::stats::get_name (std::size_t stat_index)
  {
    if constexpr (Stats)
      {
	return statdef.get_value_name (stat_index);
      }
    else
      {
	return nullptr;
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // base functions
  //////////////////////////////////////////////////////////////////////////

  template <typename Func>
  void
  wp_call_func_throwing_system_error (const char *message, Func &func)
  {
#if !defined (NDEBUG)
    try
      {
#endif
	func ();  // no exception catching on release
#if !defined (NDEBUG)
      }
    catch (const std::system_error &e)
      {
	wp_handle_system_error (message, e);
      }
#endif
  }

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_IMPL_HPP_
