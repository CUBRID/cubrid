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
 * px_hash_join_task_manager.hpp
 */

#pragma once

#include "query_hash_join.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "error_context.hpp"		/* cuberr::context */
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager */
#include "storage_common.h"		/* NULL_TRAN_INDEX */
#include "thread_entry.hpp"		/* cubthread::entry */
#include "thread_entry_task.hpp"	/* cubthread::entry_task */

/*
 * Forward Declarations
 */

struct qmgr_temp_file;

typedef struct qmgr_temp_file QMGR_TEMP_FILE;

/*
 * Class Definitions
 */

namespace parallel_query
{
  namespace hash_join
  {
    /* Forward Declarations */
    class base_task;

    /*
     * task_manager
     */

    class task_manager
    {
      public:
	task_manager (worker_manager *worker_manager, cubthread::entry &main_thread_ref);

	inline cubthread::entry &get_main_thread_ref () const noexcept
	{
	  return m_main_thread_ref;
	}

	void push_task (base_task *task);
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
	  m_thread_ref.conn_entry = nullptr;
	  m_thread_ref.on_trace = false;

	  m_thread_ref.pop_resource_tracks ();
	}

      private:
	cubthread::entry &m_thread_ref;
    };

    /*
     * base_task
     */

    class base_task: public cubthread::entry_task
    {
      public:
	base_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, int index);
	void retire () override;

      protected:
	task_manager &m_task_manager;
	HASHJOIN_MANAGER *m_manager;
	const int m_index;
    };

    /*
     * split_task
     */

    class split_task: public base_task
    {
      public:
	split_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_INPUT_SPLIT_INFO *split_info,
		    HASHJOIN_SHARED_SPLIT_INFO *shared_info, int index);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_INPUT_SPLIT_INFO *m_split_info;
	HASHJOIN_SHARED_SPLIT_INFO *m_shared_info;

	/* per-thread membuf iteration state: -1 = not owner, (>= 0) = current membuf page index */
	int m_membuf_index;

	/* per-thread sector iteration state */
	int m_sector_index;		/* current sector index in page_map, -1 = need next sector */
	UINT64 m_current_bitmap;	/* remaining page bits in current sector */
	VSID m_current_vsid;		/* current sector VSID */
	QMGR_TEMP_FILE *m_current_tfile;	/* tfile that owns the last returned page */

	PAGE_PTR get_next_page (cubthread::entry &thread_ref);
    };

    /*
     * join_task
     */

    class join_task: public base_task
    {
      public:
	join_task (task_manager &task_manager, HASHJOIN_MANAGER *manager,HASHJOIN_CONTEXT *contexts,
		   HASHJOIN_SHARED_JOIN_INFO *shared_info, int index);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_CONTEXT *m_contexts;
	HASHJOIN_SHARED_JOIN_INFO *m_shared_info;

	HASHJOIN_CONTEXT *get_next_context ();
    };
  } /* namespace hash_join */
} /* namespace parallel_query */
