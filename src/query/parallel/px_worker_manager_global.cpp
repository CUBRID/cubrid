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
 * px_worker_manager_global.cpp - module that manages parallel worker threads.
 */

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "px_worker_manager_global.hpp"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  worker_manager_global::worker_manager_global()
  {
    m_is_initialized = false;
    m_max_parallel_workers = 0;
    m_worker_pool = nullptr;
    m_current_parallel_workers = 0;
  }

  worker_manager_global::~worker_manager_global()
  {
    if (m_worker_pool != nullptr)
      {
	cubthread::get_manager()->destroy_worker_pool (m_worker_pool);
      }
  }

  void worker_manager_global::init()
  {
    if (m_is_initialized)
      {
	return;
      }
    m_is_initialized = true;
    m_max_parallel_workers = prm_get_integer_value (PRM_ID_MAX_PARALLEL_WORKERS);
    if (m_max_parallel_workers <= 1)
      {
	return;
      }
    int pool_size = m_max_parallel_workers;
    int task_max_count = m_max_parallel_workers * TASK_QUEUE_SIZE_PER_CORE;
    m_worker_pool = cubthread::get_manager()->create_worker_pool (pool_size, task_max_count,
		    "parallel_query_worker_pool", NULL, 1, false);
  }

  void worker_manager_global::destroy()
  {
    assert (m_current_parallel_workers.load () == 0);
    if (m_worker_pool != nullptr)
      {
	cubthread::get_manager()->destroy_worker_pool (m_worker_pool);
	m_worker_pool = nullptr;
      }
    m_is_initialized = false;
  }

  bool worker_manager_global::try_reserve_workers (int parallelism)
  {
    int expected;
    while (true)
      {
	expected = m_current_parallel_workers.load ();
	if (expected + parallelism > m_max_parallel_workers)
	  {
	    return false;
	  }
	if (m_current_parallel_workers.compare_exchange_weak (expected, expected + parallelism))
	  {
	    break;
	  }
	else
	  {
	    std::this_thread::yield();
	  }
      }
    return true;
  }

  void worker_manager_global::release_workers (int parallelism)
  {
    m_current_parallel_workers.fetch_sub (parallelism);
  }

  void worker_manager_global::push_task (cubthread::entry_task *task)
  {
    cubthread::get_manager()->push_task (m_worker_pool, task);
  }

}
