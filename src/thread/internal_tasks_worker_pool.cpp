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
 * internal_tasks_worker_pool.cpp
 */

#include "internal_tasks_worker_pool.hpp"
#include "thread_worker_pool.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  constexpr size_t WORKER_COUNT = 2;
  constexpr size_t TASK_COUNT = 10;
  constexpr size_t CORE_COUNT = 1;
  constexpr bool ENABLE_LOGGING = true;

  entry_workpool *instance = NULL;

  namespace internal_tasks_worker_pool
  {
    void initialize ()
    {
      if (instance != NULL)
	{
	  return;
	}

      instance = cubthread::get_manager ()->create_worker_pool (WORKER_COUNT,
		 TASK_COUNT, "internal_tasks_worker_pool", NULL, CORE_COUNT, ENABLE_LOGGING);
    }

    entry_workpool *get_instance ()
    {
      assert (instance != NULL);
      return instance;
    }

    void finalize ()
    {
      delete instance;
      instance = NULL;
    }
  }
}
