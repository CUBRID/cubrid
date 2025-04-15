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
 * px_worker_manager_global.hpp - module that manages parallel worker threads.
 */

#ifndef _PX_WORKER_MANAGER_GLOBAL_HPP_
#define _PX_WORKER_MANAGER_GLOBAL_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

namespace parallel_query
{
  class worker_manager_global
  {
    private:
      static const int TASK_QUEUE_SIZE_PER_CORE = 2;
      friend class worker_manager;
      friend class worker_manager_with_dedicated_pool;
      bool m_is_initialized;
      int m_max_parallel_workers;
      std::atomic<int> m_current_parallel_workers;
      cubthread::entry_workpool *m_worker_pool;
      worker_manager_global();
      ~worker_manager_global();

      worker_manager_global (const worker_manager_global &) = delete;
      worker_manager_global &operator= (const worker_manager_global &) = delete;

      bool try_reserve_workers (int parallelism);
      void release_workers (int parallelism);
      void push_task (cubthread::entry_task *task);
    public:
      static worker_manager_global &get_manager()
      {
	static worker_manager_global instance;
	return instance;
      }

      void init();
      void destroy();
  };
}

#endif /*_PX_WORKER_MANAGER_GLOBAL_HPP_ */
