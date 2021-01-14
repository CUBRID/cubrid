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
 * internal_tasks_worker_pool.hpp
 */

#ifndef _INTERNAL_TASKS_WORKER_POOL_HPP_
#define _INTERNAL_TASKS_WORKER_POOL_HPP_

#include "thread_manager.hpp"

namespace cubthread
{
  namespace internal_tasks_worker_pool
  {
    void initialize ();
    void finalize ();
    entry_workpool *get_instance ();
  }
}

#endif // _INTERNAL_TASKS_WORKER_POOL_HPP_
