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
 * px_worker_manager.hpp - module that manages parallel worker threads.
 */

#ifndef _PX_WORKER_MANAGER_HPP_
#define _PX_WORKER_MANAGER_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "thread_manager.hpp"

namespace parallel_query
{
  class worker_manager
  {
    public:
      static worker_manager &get_manager()
      {
	thread_local static worker_manager instance;
	return instance;
      }

      bool try_reserve_workers (int parallelism);
      void release_workers ();
      void push_task (cubthread::entry_task *task);
      void pop_task ()
      {
	m_working_workers--;
      }

    private:
      int m_reserved_workers;
      std::atomic<int> m_working_workers;
      worker_manager();
      ~worker_manager();
      worker_manager (const worker_manager &) = delete;
      worker_manager &operator= (const worker_manager &) = delete;
  };

  class worker_manager_with_dedicated_pool
  {
    public:
      static worker_manager_with_dedicated_pool &get_manager()
      {
	thread_local static worker_manager_with_dedicated_pool instance;
	return instance;
      }

      bool try_reserve_workers (int parallelism, int task_queue_size);
      void release_workers ();
      void push_task (cubthread::entry_task *task);
      void pop_task ()
      {
	m_active_tasks--;
      }
    private:
      int m_reserved_workers;
      std::atomic<int> m_active_tasks;
      cubthread::entry_workpool *m_worker_pool;
      worker_manager_with_dedicated_pool();
      ~worker_manager_with_dedicated_pool();
      worker_manager_with_dedicated_pool (const worker_manager_with_dedicated_pool &) = delete;
      worker_manager_with_dedicated_pool &operator= (const worker_manager_with_dedicated_pool &) = delete;
  };

  class worker_manager_reserver
  {
    public:
      static worker_manager_reserver &get_manager()
      {
	thread_local static worker_manager_reserver instance;
	return instance;
      }

      bool try_reserve_workers (int parallelism);
      void release_workers ();

    private:
      int m_reserved_workers;

      worker_manager_reserver();
      ~worker_manager_reserver();
      worker_manager_reserver (const worker_manager_reserver &) = delete;
      worker_manager_reserver &operator= (const worker_manager_reserver &) = delete;
  };
}

#endif /*_PX_WORKER_MANAGER_HPP_ */
