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
 * px_hash_join.hpp
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "error_context.hpp"		/* cuberr::context */
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager_with_dedicated_pool */
#include "query_hash_join.h"
#include "thread_entry.hpp"		/* cubthread::entry */
#include "thread_entry_task.hpp"	/* cubthread::entry_task */
#include "thread_worker_pool.hpp"	/* cubthread::entry_workpool */
#include "xasl_spawner.hpp"		/* cubxasl::spawner */

namespace parallel_query
{
  namespace hash_join
  {
    /* Forward Declarations */
    class base_task;

    class entry_manager : public cubthread::entry_manager
    {
      public:
	entry_manager (cubthread::entry &main_thread_ref);

      protected:
	void on_create (cubthread::entry &context) override;
	void on_retire (cubthread::entry &context) override;
	void on_recycle (cubthread::entry &context) override;

      private:
	cubthread::entry &m_main_thread_ref;

	void emulate_main_thread (cubthread::entry &thread_ref);
    };

    class worker_pool_manager
    {
      public:
	worker_pool_manager (cubthread::entry &main_thread_ref);
	~worker_pool_manager ();

	bool try_reserve_workers (size_t pool_size);
	void release_workers ();

	cubthread::entry_workpool *get_worker_pool () const;

      private:
	entry_manager m_entry_manager;
	cubthread::entry_workpool *m_worker_pool;
    };

    class task_manager
    {
      public:
	task_manager (cubthread::entry_workpool *worker_pool, cuberr::context &main_error_context);

	void push_task (base_task *task);
	void end_task ();
	void join ();

	bool has_error ();
	bool check_interrupt (cubthread::entry &thread_ref);
	void handle_error (cubthread::entry &thread_ref);

      private:
	cubthread::entry_workpool *m_worker_pool;
	int m_active_tasks;

	std::mutex m_mutex;
	std::condition_variable m_cv;

	std::atomic<bool> m_has_error;
	cuberr::context &m_main_error_context;
    };

    class base_task: public cubthread::entry_task
    {
      public:
	base_task (HASHJOIN_MANAGER *manager, task_manager &task_manager);
	void retire () override;

      protected:
	HASHJOIN_MANAGER *m_manager;
	task_manager &m_task_manager;
    };

    class partition_task: public base_task
    {
      public:
	partition_task (HASHJOIN_MANAGER *manager, HASHJOIN_INPUT_SPLIT_INFO *split_info, task_manager &task_manager);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_INPUT_SPLIT_INFO *m_split_info;
    };

    class join_task: public base_task
    {
      public:
	join_task (HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context, task_manager &task_manager);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_CONTEXT *m_context;
    };

    int build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info);
    int execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);

    cubxasl::spawner *get_spawner (cubthread::entry &thread_ref);
    void clear_spawner ();
    VAL_DESCR *get_val_descr (cubthread::entry &thread_ref, VAL_DESCR *val_descr);
    PRED_EXPR *get_during_join_pred (cubthread::entry &thread_ref, PRED_EXPR *during_join_pred);
    REGU_VARIABLE_LIST get_outer_regu_list_pred (cubthread::entry &thread_ref, REGU_VARIABLE_LIST outer_regu_list_pred);
    REGU_VARIABLE_LIST get_inner_regu_list_pred (cubthread::entry &thread_ref, REGU_VARIABLE_LIST inner_regu_list_pred);

    template <typename T>
    T *get_tls (cubthread::entry &thread_ref, T *src, T *&dest);
  } /* namespace hash_join */
} /* namespace parallel_query */

/*
 * Function Definitions
 */

namespace parallel_query
{
  namespace hash_join
  {
    template <typename T>
    T *
    get_tls (cubthread::entry &thread_ref, T *src, T *&dest)
    {
      if (dest != nullptr)
	{
	  return dest;
	}

      auto *spawner = get_spawner (thread_ref);
      if (spawner == nullptr)
	{
	  assert_release (er_errid () != NO_ERROR);
	  return nullptr;
	}

      dest = spawner->spawn (src);

      return dest;
    }
  } /* namespace hash_join */
} /* namespace parallel_query */
