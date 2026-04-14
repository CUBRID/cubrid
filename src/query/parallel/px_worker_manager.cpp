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

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "px_worker_manager.hpp"
#include "px_worker_manager_global.hpp"

#include "memory_alloc.h"
#include "thread_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  worker_manager::worker_manager()
    : m_active_tasks (0),
      m_reserved_workers (0)
  {
  }

  worker_manager::~worker_manager()
  {
    release_workers ();
  }

  worker_manager *worker_manager::try_reserve_workers (int num_workers)
  {
    assert (num_workers > 0);

    int reserved = worker_manager_global::get_manager().try_reserve_workers (num_workers);
    if (reserved == 0)
      {
	return nullptr;
      }

    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    assert (thread_p != nullptr);

    worker_manager *manager = (worker_manager *) db_private_alloc (thread_p, sizeof (worker_manager));
    if (manager == nullptr)
      {
	worker_manager_global::get_manager().release_workers (reserved);
	return nullptr;
      }

    manager = placement_new (manager);

    assert (manager->m_reserved_workers == 0);
    manager->m_reserved_workers = reserved;
    assert (manager->m_active_tasks.load () == 0);

    return manager;
  }

  void worker_manager::release_workers ()
  {
    if (m_reserved_workers == 0)
      {
	return;
      }

    wait_workers ();

    worker_manager_global::get_manager().release_workers (m_reserved_workers);
    m_reserved_workers = 0;

    THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
    assert (thread_p != nullptr);

    this->~worker_manager();
    db_private_free (thread_p, this);
  }

  void worker_manager::wait_workers ()
  {
    assert (m_reserved_workers > 0);
    while (m_active_tasks.load (std::memory_order_acquire) > 0)
      {
	std::this_thread::yield ();
      }
  }

  void worker_manager::push_task (cubthread::entry_task *task)
  {
    assert (task != nullptr);
    assert (m_reserved_workers > 0);
    assert (m_active_tasks.load () < m_reserved_workers);
    m_active_tasks.fetch_add (1, std::memory_order_release);
    worker_manager_global::get_manager().push_task (task);
  }
}
