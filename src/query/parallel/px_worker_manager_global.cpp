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
    : m_worker_pool (nullptr),
      m_init_flag (),
      m_available (0),
      m_capacity (0)
  {
  }

  worker_manager_global::~worker_manager_global()
  {
    destroy();
  }

  REGISTER_WORKERPOOL (parallel_query, []()
  {
    return prm_get_integer_value (PRM_ID_MAX_PARALLEL_WORKERS);
  });

  void worker_manager_global::init()
  {
    std::call_once (m_init_flag, [this] ()
    {
      int max_parallel_workers = prm_get_integer_value (PRM_ID_MAX_PARALLEL_WORKERS);
      if (max_parallel_workers < 2)
	{
	  /* parallel execution requires at least 2 workers */
	  assert (m_worker_pool == nullptr);
	  return;
	}

      int pool_size = max_parallel_workers;
      int task_max_count = max_parallel_workers * TASK_QUEUE_SIZE_PER_CORE;

      assert (m_worker_pool == nullptr);
      m_worker_pool = cubthread::get_manager()->create_worker_pool (
			      pool_size, task_max_count,
			      "parallel-query", NULL, 1, false);
      if (m_worker_pool == nullptr)
	{
	  return;
	}

      m_capacity = max_parallel_workers;
      m_available = max_parallel_workers;
    });
  }

  void worker_manager_global::destroy()
  {
    if (m_worker_pool == nullptr)
      {
	/* init() was not called or failed */
	return;
      }

    /* all workers should be released before destroy */
    assert (m_available.load () == m_capacity);

    cubthread::get_manager()->destroy_worker_pool (m_worker_pool);
    m_worker_pool = nullptr;
  }

  int worker_manager_global::try_reserve_workers (const int num_workers)
  {
    assert (num_workers > 0);
    assert (num_workers <= PRM_MAX_PARALLELISM);

    /* safe-guard */
    if (num_workers <= 0)
      {
	return 0;
      }

    /* safe-guard */
    int requested = MIN (num_workers, PRM_MAX_PARALLELISM);

    /* minimum parallel degree:
     * - 2 for parallel execution (heap scan, hash join, sort)
     * - 1 for parallel subquery (main thread + 1 worker for uncorrelated subquery)
     */
    const int min_degree = (requested == 1) ? 1 : 2;

    int available = m_available.load ();

    while (true)
      {
	/* check if enough workers available */
	if (available < min_degree)
	  {
	    return 0;
	  }

	/* reserve as many as possible, up to requested */
	int reserved = (requested <= available) ? requested : available;

	if (m_available.compare_exchange_weak (available, available - reserved))
	  {
	    return reserved;
	  }

	/* CAS failed: available is updated with actual value, retry */
	std::this_thread::yield ();
      }
  }

  void worker_manager_global::release_workers (const int num_workers)
  {
    assert (num_workers > 0);
    assert (m_worker_pool != nullptr);
    assert (m_available.load () + num_workers <= m_capacity);

    /* safe-guard */
    if (num_workers <= 0)
      {
	return;
      }

    m_available.fetch_add (num_workers);
  }

  void worker_manager_global::push_task (cubthread::entry_task *task)
  {
    assert (task != nullptr);
    assert (m_worker_pool != nullptr);
    cubthread::get_manager()->push_task (m_worker_pool, task);
  }
}
