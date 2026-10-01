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

#ifndef _LOG_APPLIER_WORKER_HPP_
#define _LOG_APPLIER_WORKER_HPP_

#include "log_applier_queue.hpp"

#include <cstddef>

struct la_workers;

/* Bridges workers to the apply routines that remain in log_applier.c. Production uses one fixed
 * operation set; tests may replace it with fakes.
 * TODO(CBRD-27508): Remove this callback bridge if the apply routines move into the worker module. */
struct la_worker_operations
{
  int (*initialize) (void *shared_context, std::size_t worker_index, void **worker_context);
  int (*execute) (void *shared_context, void *worker_context, const la_apply_task &task, la_apply_result &result);
  void (*finalize) (void *shared_context, void *worker_context);
};

/* TODO(CBRD-27508): Remove shared_context if the production callbacks need only worker-local state. */

int la_worker_create (std::size_t worker_count, la_task_queue *task_queue, la_result_queue *result_queue,
		      const la_worker_operations &worker_operations, void *shared_context, la_workers **workers_out);
int la_worker_start (la_workers *workers);
void la_worker_stop (la_workers *workers);
void la_worker_destroy (la_workers *workers);

#endif /* _LOG_APPLIER_WORKER_HPP_ */
