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
#include "thread_entry.hpp"		/* cubthread::entry */
#include "thread_entry_task.hpp"	/* cubthread::entry_task */
#include "thread_worker_pool.hpp"	/* cubthread::entry_workpool */

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
	task_manager (cubthread::entry_workpool *worker_pool, cuberr::context &main_error_context);

	void push_task (base_task *task);
	void end_task ();
	void join ();

	bool has_error () const noexcept;
	bool check_interrupt (cubthread::entry &thread_ref);
	void clear_interrupt (cubthread::entry &thread_ref);
	void handle_error (cubthread::entry &thread_ref);
	void notify_stop ();
	void stop_execution ();

      private:
	cubthread::entry_workpool *m_worker_pool;

	std::condition_variable m_all_tasks_done_cv;
	std::mutex m_active_tasks_mutex;
	int m_active_tasks;

	std::atomic<bool> m_has_error;
	cuberr::context &m_main_error_context;
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
	int m_index;
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
