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
 * px_worker_manager.cpp - module that manages parallel worker threads.
 */

#include "thread_manager.hpp"
#include "memory_alloc.h"
#include <chrono>
#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "px_worker_manager.hpp"
#include "px_worker_manager_global.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{

  worker_manager::worker_manager()
  {
    m_reserved_workers = 0;
    m_working_workers = 0;
  }

  worker_manager::~worker_manager()
  {
    assert (m_reserved_workers == 0);
  }

  worker_manager *worker_manager::try_reserve_workers (int n_workers)
  {
    bool result = worker_manager_global::get_manager().try_reserve_workers (n_workers);
    worker_manager *manager = nullptr;
    THREAD_ENTRY *thread_p;
    if (result)
      {
	thread_p = thread_get_thread_entry_info();
	manager = (worker_manager *) db_private_alloc (thread_p, sizeof (worker_manager));
	manager = placement_new (manager);
	manager->m_reserved_workers = n_workers;
	return manager;
      }
    else
      {
	return nullptr;
      }
  }

  void worker_manager::release_workers (int n_workers)
  {
    THREAD_ENTRY *thread_p;
    thread_p = thread_get_thread_entry_info();
    if (m_reserved_workers == 0)
      {
	this->~worker_manager();
	db_private_free (thread_p, this);
	return;
      }

    while (m_working_workers.load () > 0)
      {
	;
      }
    worker_manager_global::get_manager().release_workers (n_workers);
    m_reserved_workers -= n_workers;
    assert (m_reserved_workers == 0);
    this->~worker_manager();
    db_private_free (thread_p, this);
  }

  void worker_manager::wait_workers ()
  {
    if (m_reserved_workers == 0)
      {
	return;
      }

    while (m_working_workers.load () > 0)
      {
	;
      }
  }

  void worker_manager::push_task (cubthread::entry_task *task)
  {
    m_working_workers.fetch_add (1, std::memory_order_release);
    worker_manager_global::get_manager().push_task (task);
    assert (m_working_workers.load () <= m_reserved_workers);
  }

  worker_manager_with_dedicated_pool::worker_manager_with_dedicated_pool()
  {
    m_reserved_workers = 0;
    m_active_tasks = 0;
    m_worker_pool = nullptr;
  }

  worker_manager_with_dedicated_pool::~worker_manager_with_dedicated_pool()
  {
    assert (m_reserved_workers == 0);
    assert (m_active_tasks.load () == 0);
    assert (m_worker_pool == nullptr);
  }

  bool worker_manager_with_dedicated_pool::try_reserve_workers (int parallelism, int task_queue_size)
  {
    bool result = worker_manager_global::get_manager().try_reserve_workers (parallelism);
    if (result)
      {
	assert (m_worker_pool == nullptr);
	m_reserved_workers += parallelism;
	m_worker_pool = cubthread::get_manager()->create_worker_pool (parallelism, task_queue_size,
			"parallel_query_worker_pool_with_task_queue", NULL, 1, false);
      }
    return result;
  }

  void worker_manager_with_dedicated_pool::release_workers ()
  {
    cubthread::get_manager()->destroy_worker_pool (m_worker_pool);
    m_worker_pool = nullptr;
    worker_manager_global::get_manager().release_workers (m_reserved_workers);
    m_reserved_workers = 0;
    assert (m_active_tasks.load () == 0);
  }

  void worker_manager_with_dedicated_pool::push_task (cubthread::entry_task *task)
  {
    m_active_tasks.fetch_add (1);
    cubthread::get_manager()->push_task (m_worker_pool, task);
  }

  worker_manager_reserver::worker_manager_reserver()
  {
    m_reserved_workers = 0;
  }

  worker_manager_reserver::~worker_manager_reserver()
  {
    release_workers ();
    assert (m_reserved_workers == 0);
  }

  bool worker_manager_reserver::try_reserve_workers (int parallelism)
  {
    if (parallelism <= 0 || m_reserved_workers > 0)
      {
	assert (false);
	return false;
      }

    if (worker_manager_global::get_manager().try_reserve_workers (parallelism))
      {
	m_reserved_workers = parallelism;
	return true;
      }

    assert (m_reserved_workers == 0);
    return false;
  }

  void worker_manager_reserver::release_workers ()
  {
    worker_manager_global::get_manager().release_workers (m_reserved_workers);
    m_reserved_workers = 0;
  }
}
