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
 */

/*
 * log_applier_coordinator.hpp - order and dispatch parallel apply tasks
 */

#ifndef _LOG_APPLIER_COORDINATOR_HPP_
#define _LOG_APPLIER_COORDINATOR_HPP_

#include "log_applier_inflight.hpp"
#include "log_applier_task.hpp"

typedef struct la_coordinator_config LA_COORDINATOR_CONFIG;
struct la_coordinator_config
{
  int commit_order_capacity;
  int worker_count;
  bool run_each_task_alone;
  const LOG_LSA *restart_lsa;	/* borrowed; loaded by log_applier.c before the first task is submitted */

  int (*before_dispatch) (void *context, const LA_TASK *task); /* prepare legacy DB state before enqueue */
  void (*retire_entry) (void *context, LA_INFLIGHT_ENTRY *entry,
			const LOG_LSA *frontier); /* apply ordered completion to legacy state */
  int (*store_progress) (void *context); /* persist retired progress when its interval expires */
  bool (*shutdown_requested) (void *context); /* stop coordinator waits when the applier is shutting down */
  void *context;
};

/* Initialize scheduling state before workers start. */
extern int la_coordinator_initialize (const LA_COORDINATOR_CONFIG *config);

/* Register a completed log task and dispatch or park it. */
extern int la_coordinator_submit_task (LA_TASK *task);

/* Complete an ordered SYSOP_END that has no replication items. */
extern int la_coordinator_complete_without_task (const LOG_LSA *lsa);

/* Collect worker results and retire the completed dispatch prefix. */
extern int la_coordinator_collect_and_retire (void);

/* Return true after pending, in-flight and commit-order state are empty. */
extern bool la_coordinator_drain_complete (void);

#endif /* _LOG_APPLIER_COORDINATOR_HPP_ */
