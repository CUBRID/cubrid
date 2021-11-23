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
 * load_worker_manager.hpp - Thread manager of the loaddb session
 */

#ifndef _METHOD_WORKER_MANAGER_HPP_
#define _METHOD_WORKER_MANAGER_HPP_

#include "system.h"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

namespace cubmethod
{
  cubthread::entry_workpool *worker_pool ();
  void start_workers ();
  void stop_workers ();
}

#endif /* _METHOD_WORKER_MANAGER_HPP_ */