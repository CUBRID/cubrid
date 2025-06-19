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

#ifndef _PX_HASH_JOIN_H_
#define _PX_HASH_JOIN_H_

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "error_context.hpp"		/* cuberr::context */
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager_with_dedicated_pool */
#include "query_hash_join.h"
#include "thread_entry.hpp"		/* cubthread::entry */
#include "thread_entry_task.hpp"	/* cubthread::entry_task */

namespace parallel_hash_join
{
  /* Forward Declarations */
  class task_manager;

  class base_task: public cubthread::entry_task
  {
    protected:
      cubthread::entry &m_main_thread_ref;
      HASHJOIN_MANAGER *m_manager;
      task_manager &m_task_manager;

      void emulate_main_thread (cubthread::entry &thread_ref);

    public:
      base_task (cubthread::entry &main_thread_ref, HASHJOIN_MANAGER *manager, task_manager &task_manager);
      virtual ~base_task () = default;

      void retire () override;
  };

  class task_manager
  {
    private:
      parallel_query::worker_manager_with_dedicated_pool &m_worker_manager;
      int m_active_tasks;
      std::mutex m_mutex;
      std::condition_variable m_cv;
      cuberr::context &m_main_error_context;
      std::atomic<bool> m_has_error;

    public:
      task_manager (parallel_query::worker_manager_with_dedicated_pool &worker_manager, cuberr::context &main_error_context);
      ~task_manager () = default;

      void push_task (base_task *task);
      void task_done ();
      void join ();

      bool has_error ();
      bool check_interrupt (cubthread::entry &thread_ref);
      void handle_error (cubthread::entry &thread_ref);
  };

  class partition_task: public base_task
  {
    private:
      HASHJOIN_INPUT_SPLIT_INFO *m_split_info;

    public:
      partition_task (cubthread::entry &main_thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_INPUT_SPLIT_INFO *split_info,
		      task_manager &task_manager);
      ~partition_task () = default;

      void execute (cubthread::entry &thread_ref) override;
  };

  class join_task: public base_task
  {
    private:
      HASHJOIN_CONTEXT *m_context;

    public:
      join_task (cubthread::entry &main_thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context,
		 task_manager &task_manager);
      ~join_task () = default;

      void execute (cubthread::entry &thread_ref) override;
  };

  int
  build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info);

  int
  execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);
} /* namespace parallel_hash_join */

#endif /* _PX_HASH_JOIN_H_ */
