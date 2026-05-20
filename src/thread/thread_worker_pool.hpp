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

#if !defined (SERVER_MODE)
#error Wrong module
#endif // not SERVER_MODE

// same module include
#include "thread_task.hpp"
#include "thread_waiter.hpp"
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"

// cubrid includes
#include "perf_def.hpp"

// system includes
#include <string>

namespace cubthread
{
  // cubthread::worker_pool
  //
  //  description
  //    a pool of threads to execute tasks in parallel
  //    for high-loads (more tasks than threads), stores tasks in queues to be executed when a thread is freed.
  //    for low-loads (fewer tasks than threads), retires thread when no new tasks are available and creates new
  //      threads when tasks are added again
  //    in high-loads, thread context is shared between task
  //

  class worker_pool
  {
    public:
      // forward definition for nested core class
      class core;

      using context_type = entry;
      using task_type = task<context_type>;

      virtual ~worker_pool () = default;

      // init
      virtual void initialize (std::size_t worker_count, std::size_t core_count) = 0;

      // execution
      virtual void execute (task_type *work_arg) = 0;
      virtual void execute_on_core (task_type *work_arg, std::size_t core_hash, bool is_temp = false) = 0;

      // pooling related
      virtual void warmup (void) = 0;

      // termination
      virtual void stop_execution (void) = 0;

      // information
      virtual bool is_running (void) const = 0;

      const std::string &get_name (void) const
      {
	return m_name;
      }

      entry_manager &get_entry_manager (void) const
      {
	return m_entry_manager;
      }

      const wait_seconds &get_idle_timeout () const
      {
	return m_idle_timeout;
      }

      virtual std::size_t get_worker_count (void) const = 0;
      virtual std::size_t get_core_count (void) const = 0;

      // stats
      virtual void get_stats (cubperf::stat_value *stats_out) const = 0;

    protected:
      worker_pool (const char *name, entry_manager &entry_mgr, bool pool_threads, wait_seconds idle_timeout)
	: m_name (name == NULL ? "" : name)
	, m_entry_manager (entry_mgr)
	, m_pool_threads (pool_threads)
	, m_idle_timeout (idle_timeout)
      {
      }

      // worker pool name (used as thread name)
      std::string m_name;

      // thread entry manager
      entry_manager &m_entry_manager;

      // true to keep all threads alive always
      bool m_pool_threads;
      // transition time period between active and inactive
      wait_seconds m_idle_timeout;
  };

  class worker_pool::core
  {
    public:
      // forward definition of nested class worker
      class worker;

      virtual ~core (void) = default;

      // init
      virtual void initialize (std::size_t worker_count) = 0;

      // execution
      virtual void execute_task (task_type *task_p, bool is_temp) = 0;

      // pooling related
      virtual void warmup (void) = 0;

      // termination
      virtual bool stop_execution (void) = 0;

      // information
      entry_manager &get_entry_manager (void) const
      {
	return get_parent_pool ()->get_entry_manager ();
      }

      void set_parent_pool (worker_pool &parent)
      {
	m_parent_pool = &parent;
      }

      worker_pool *get_parent_pool (void) const
      {
	return m_parent_pool;
      }

      virtual std::size_t get_worker_count (void) const = 0;

      // stats
      virtual void get_stats (cubperf::stat_value *stats_out) const = 0;

    protected:
      core (bool pool_threads)
	: m_parent_pool (nullptr)
	, m_pool_threads (pool_threads)
      {
      }

      // parent worker pool
      worker_pool *m_parent_pool;

      // true to keep all threads alive
      bool m_pool_threads;
  };

  class worker_pool::core::worker
  {
    public:
      virtual ~worker (void) = default;

      // init
      virtual void initialize () = 0;

      // termination
      virtual bool stop_execution (void) = 0;

      // information
      void set_parent_core (core &parent)
      {
	m_parent_core = &parent;
      }

      core *get_parent_core (void) const
      {
	return m_parent_core;
      }

      // stats
      virtual void get_stats (cubperf::stat_value *stats_out) const = 0;

    protected:
      worker ()
	: m_parent_core (nullptr)
      {
      }

      core *m_parent_core;
  };

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_HPP_

