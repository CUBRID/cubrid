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

#include "method_worker_manager.hpp"

#include "thread_entry_task.hpp"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include "system_parameter.h"
#include "session.h"

namespace cubmethod
{
  static cubthread::entry_workpool *g_worker_pool = nullptr;

  cubthread::entry_workpool *worker_pool ()
  {
    return g_worker_pool;
  }

  void
  start_workers ()
  {
    if (g_worker_pool != nullptr)
      {
	return;
      }

    // get thread manager
    cubthread::manager *thread_manager = cubthread::get_manager ();

    int worker_count = prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_WORKER_COUNT);

    // create thread pool
    g_worker_pool =
	    thread_manager->create_worker_pool (worker_count, worker_count * 5, "method callback request workers",
		NULL, 1, false, true);
    assert (g_worker_pool != nullptr);
  }

  void
  stop_workers ()
  {
    // stop work pool
    if (g_worker_pool != nullptr)
      {
#if defined (SERVER_MODE)
	g_worker_pool->er_log_stats ();
	g_worker_pool->stop_execution ();
#endif // SERVER_MODE

	cubthread::get_manager ()->destroy_worker_pool (g_worker_pool);
      }

    delete g_worker_pool;
    g_worker_pool = nullptr;
  }
};