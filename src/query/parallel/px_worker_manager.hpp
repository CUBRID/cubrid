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
      static worker_manager &get_manager (int tran_index);
      static worker_manager *get_manager_p (int tran_index);
      static worker_manager &get_manager ();
      static worker_manager *get_manager_p ();

      bool try_reserve_workers (int parallelism);
      void release_workers ();
      void push_task (cubthread::entry_task *task);
      void pop_task ()
      {
	m_working_workers--;
      }

      worker_manager();
      ~worker_manager();

      worker_manager (worker_manager &&other) noexcept
	: m_reserved_workers (other.m_reserved_workers),
	  m_working_workers (other.m_working_workers.load())
      {
	other.m_reserved_workers = 0;
	other.m_working_workers = 0;
      }

      worker_manager &operator= (worker_manager &&other) noexcept
      {
	if (this != &other)
	  {
	    m_reserved_workers = other.m_reserved_workers;
	    m_working_workers = other.m_working_workers.load();
	    other.m_reserved_workers = 0;
	    other.m_working_workers = 0;
	  }
	return *this;
      }

      worker_manager (const worker_manager &) = delete;
      worker_manager &operator= (const worker_manager &) = delete;

    private:
      int m_reserved_workers;
      std::atomic<int> m_working_workers;
  };

  class worker_manager_with_dedicated_pool
  {
    public:
      static worker_manager_with_dedicated_pool &get_manager (int tran_index);
      static worker_manager_with_dedicated_pool *get_manager_p (int tran_index);
      static worker_manager_with_dedicated_pool &get_manager ();
      static worker_manager_with_dedicated_pool *get_manager_p ();

      bool try_reserve_workers (int parallelism, int task_queue_size);
      void release_workers ();
      void push_task (cubthread::entry_task *task);
      void pop_task ()
      {
	m_active_tasks--;
      }

      worker_manager_with_dedicated_pool();
      ~worker_manager_with_dedicated_pool();

      worker_manager_with_dedicated_pool (worker_manager_with_dedicated_pool &&other) noexcept
	: m_reserved_workers (other.m_reserved_workers),
	  m_active_tasks (other.m_active_tasks.load()),
	  m_worker_pool (other.m_worker_pool)
      {
	other.m_reserved_workers = 0;
	other.m_active_tasks = 0;
	other.m_worker_pool = nullptr;
      }

      worker_manager_with_dedicated_pool &operator= (worker_manager_with_dedicated_pool &&other) noexcept
      {
	if (this != &other)
	  {
	    m_reserved_workers = other.m_reserved_workers;
	    m_active_tasks = other.m_active_tasks.load();
	    m_worker_pool = other.m_worker_pool;
	    other.m_reserved_workers = 0;
	    other.m_active_tasks = 0;
	    other.m_worker_pool = nullptr;
	  }
	return *this;
      }

      worker_manager_with_dedicated_pool (const worker_manager_with_dedicated_pool &) = delete;
      worker_manager_with_dedicated_pool &operator= (const worker_manager_with_dedicated_pool &) = delete;

    private:
      int m_reserved_workers;
      std::atomic<int> m_active_tasks;
      cubthread::entry_workpool *m_worker_pool;
  };
}

#endif /*_PX_WORKER_MANAGER_HPP_ */
