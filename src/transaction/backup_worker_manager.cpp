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
 * backup_worker_manager.cpp - Thread manager of the backupdb session
 */

#include "backup_worker_manager.hpp"

#include "thread_worker_pool.hpp"
#include "thread_worker_pool_taskcap.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbackup
{
  cubthread::entry_workpool * g_backup_worker_pool = nullptr;

  static cubthread::worker_pool_task_capper < cubthread::entry > *g_backup_read_task_capper;

  void worker_backup_wp_push_task (cubthread::entry_task * task)
  {
    assert (g_backup_worker_pool != NULL);
    g_backup_read_task_capper->push_task (task);
  }

  void worker_create_task_capper ()
  {
    assert (g_backup_worker_pool != nullptr);
    g_backup_read_task_capper = new cubthread::worker_pool_task_capper < cubthread::entry > (g_backup_worker_pool);
  }
}				// namespace cubbackup
