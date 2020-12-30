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
 * load_worker_manager.hpp - Thread manager of the loaddb session
 */

#ifndef _LOAD_WORKER_MANAGER_HPP_
#define _LOAD_WORKER_MANAGER_HPP_

#include "system.h"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

namespace cubload
{
  // forward definitions
  class session;

  bool worker_manager_try_task (cubthread::entry_task *task);
  void worker_manager_stop_all ();

  void worker_manager_register_session (session &load_session);
  void worker_manager_unregister_session (session &load_session);

  void worker_manager_get_stats (UINT64 *stats_out);
}

#endif /* _LOAD_WORKER_MANAGER_HPP_ */
