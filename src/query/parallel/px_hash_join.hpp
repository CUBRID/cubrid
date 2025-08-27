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

    /*
     * entry_manager
     */

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

	void emulate_main_thread (cubthread::entry &thread_ref) noexcept;
    };

    /*
     * worker_pool_manager
     */

    class worker_pool_manager
    {
      public:
	worker_pool_manager (cubthread::entry &main_thread_ref);
	~worker_pool_manager ();

	bool try_reserve_workers (int pool_size);
	void release_workers ();

	cubthread::entry_workpool *get_worker_pool () const noexcept;

      private:
	entry_manager m_entry_manager;
	cubthread::entry_workpool *m_worker_pool;
    };

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
	base_task (task_manager &task_manager, HASHJOIN_MANAGER *manager);
	void retire () override;

      protected:
	task_manager &m_task_manager;
	HASHJOIN_MANAGER *m_manager;
    };

    /*
     * split_task
     */

    class split_task: public base_task
    {
      public:
	split_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_INPUT_SPLIT_INFO *split_info,
		    HASHJOIN_SHARED_SPLIT_INFO *shared_info);
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
	join_task (task_manager &task_manager, HASHJOIN_MANAGER *manager,HASHJOIN_CONTEXT *context);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_CONTEXT *m_context;
    };

    /*
     * spawn_manager
     */

    class spawn_manager
    {
      public:
	explicit spawn_manager (cubthread::entry &thread_ref);
	~spawn_manager () noexcept;

	spawn_manager (const spawn_manager &) = delete;
	spawn_manager &operator= (const spawn_manager &) = delete;
	spawn_manager (spawn_manager &&) = delete;
	spawn_manager &operator= (spawn_manager &&) = delete;

	static spawn_manager *get_instance (cubthread::entry &thread_ref) noexcept;
	static void destroy_instance () noexcept;

	cubthread::entry &get_thread_ref () const noexcept;

	/* get_val_descr must be called first,
	 * because it creates a DB_VALUE reused by other spawned structures. */
	VAL_DESCR *get_val_descr (VAL_DESCR *src);
	PRED_EXPR *get_during_join_pred (PRED_EXPR *src);
	REGU_VARIABLE_LIST get_outer_regu_list_pred (REGU_VARIABLE_LIST src);
	REGU_VARIABLE_LIST get_inner_regu_list_pred (REGU_VARIABLE_LIST src);

      private:
	cubthread::entry &m_thread_ref;

	cubxasl::spawner *m_spawner;
	VAL_DESCR *m_val_descr;
	PRED_EXPR *m_during_join_pred;
	REGU_VARIABLE_LIST m_outer_regu_list_pred;
	REGU_VARIABLE_LIST m_inner_regu_list_pred;

	inline static thread_local spawn_manager *tls_spawn_manager = nullptr;

	cubxasl::spawner *get_spawner() noexcept;

	template <typename T>
	T *spawn (T *src, T *&dest) noexcept;
    };

    /*
     * build_partitions
     */

    int build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info);

    /*
     * execute_partitions
     */

    int execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);
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
    spawn_manager::spawn (T *src, T *&dest) noexcept
    {
      if (dest != nullptr)
	{
	  return dest;
	}

      auto *spawner = get_spawner ();
      if (spawner == nullptr)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return nullptr;
	}

      try
	{
	  dest = spawner->spawn (src);
	}
      catch (...)
	{
	  assert_release_error (er_errid() != NO_ERROR);
	  dest = nullptr;
	}

      return dest;
    }
  } /* namespace hash_join */
} /* namespace parallel_query */
