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
 * px_task_manager.hpp - worker coordination shared by the parallel join operators
 */

#ifndef _PX_TASK_MANAGER_HPP_
#define _PX_TASK_MANAGER_HPP_

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "error_context.hpp"		/* cuberr::context */
#include "px_hash_join_spawn_manager.hpp"	/* parallel_query::hash_join::spawn_manager */
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager */
#include "storage_common.h"		/* NULL_TRAN_INDEX */
#include "thread_entry.hpp"		/* cubthread::entry */
#include "thread_entry_task.hpp"	/* cubthread::entry_task */

namespace parallel_query
{
  /*
   * task_manager - tracks in-flight worker tasks and fans out error/interrupt state across them
   */

  class task_manager
  {
    public:
      task_manager (worker_manager *worker_manager, cubthread::entry &main_thread_ref);

      inline cubthread::entry &get_main_thread_ref () const noexcept
      {
	return m_main_thread_ref;
      }

      void push_task (cubthread::entry_task *task);
      void end_task ();
      void join ();

      inline bool has_error () const noexcept
      {
	return m_has_error.load (std::memory_order_acquire);
      }

      void handle_error (cubthread::entry &thread_ref);
      void notify_stop ();

      bool check_interrupt (cubthread::entry &thread_ref);
      void clear_interrupt (cubthread::entry &thread_ref);

    private:
      worker_manager *m_worker_manager;

      cubthread::entry &m_main_thread_ref;
      cuberr::context &m_main_error_context;

      std::condition_variable m_all_tasks_done_cv;
      std::mutex m_active_tasks_mutex;
      int m_active_tasks;

      std::atomic<bool> m_has_error;
  };

  /*
   * task_execution_guard - RAII helper that sets up worker thread context (main thread emulation and resource tracking)
   */

  class task_execution_guard
  {
    public:
      inline task_execution_guard (cubthread::entry &thread_ref, task_manager &task_manager)
	: m_thread_ref (thread_ref)
      {
	cubthread::entry &main_thread_ref = task_manager.get_main_thread_ref ();

	m_thread_ref.m_px_orig_thread_entry = &main_thread_ref;
	m_thread_ref.conn_entry = main_thread_ref.conn_entry;
	m_thread_ref.tran_index = main_thread_ref.tran_index;
	m_thread_ref.on_trace = main_thread_ref.on_trace;

	assert (m_thread_ref.conn_entry != nullptr);
	assert (m_thread_ref.tran_index != NULL_TRAN_INDEX);

	m_thread_ref.push_resource_tracks ();
      }

      inline ~task_execution_guard ()
      {
	/* Tear down any spawn_manager TLS the task may have obtained via get_spawn_manager().
	 * Safe no-op when never acquired (NULL-guarded inside). */
	hash_join::spawn_manager::destroy_instance ();

	m_thread_ref.conn_entry = nullptr;
	m_thread_ref.on_trace = false;

	m_thread_ref.pop_resource_tracks ();
      }

      /* Lazily obtain the per-worker spawn_manager TLS owned by this guard. Returns nullptr
       * on allocation failure (er_errid set). Subsequent calls return the same instance. */
      inline hash_join::spawn_manager *get_spawn_manager ()
      {
	return hash_join::spawn_manager::get_instance (m_thread_ref);
      }

    private:
      cubthread::entry &m_thread_ref;
  };
} /* namespace parallel_query */

#endif /* _PX_TASK_MANAGER_HPP_ */
