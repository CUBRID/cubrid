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

  bool worker_manager::try_reserve_workers (int parallelism)
  {
    bool result = worker_manager_global::get_manager().try_reserve_workers (parallelism);
    if (result)
      {
	m_reserved_workers += parallelism;
      }
    return result;
  }

  void worker_manager::release_workers (int parallelism)
  {
    while (m_working_workers.load () > 0)
      {
	thread_sleep (1);
      }
    worker_manager_global::get_manager().release_workers (parallelism);
    m_reserved_workers -= parallelism;
  }

  void worker_manager::push_task (cubthread::entry_task *task)
  {
    m_working_workers.fetch_add (1);
    worker_manager_global::get_manager().push_task (task);
    assert (m_working_workers.load () <= m_reserved_workers);
  }

}
