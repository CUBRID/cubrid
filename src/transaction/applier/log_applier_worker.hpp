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
 * log_applier_worker.hpp - interface for parallel apply worker threads
 *
 * A worker owns one thread and one server connection. This module manages thread and session life
 * cycles and the task loop. Worker-local apply buffers are managed by log_applier_worker_context;
 * log_applier.c supplies the operation that applies one task.
 */

#ifndef _LOG_APPLIER_WORKER_HPP_
#define _LOG_APPLIER_WORKER_HPP_

#include <pthread.h>

#include "porting.h"
#include "heartbeat.h"		/* HB_MAX_APPLY_WORKER_COUNT */
#include "log_applier_task.hpp"
#include "log_applier_result.hpp"
#include "log_applier_worker_context.hpp"

#define LA_MAX_WORKER_COUNT         HB_MAX_APPLY_WORKER_COUNT	/* per-worker arrays are this long; the number of
								 * workers actually started is ha_apply_worker_count */
#define LA_TASK_QUEUE_CAPACITY      1024	/* tasks handed to the workers but not yet taken by one */

/* Size of the gate's commit-order ring: the most commits read from the log that the gap-free frontier has
 * not passed yet, whether parked, queued, running, or finished but waiting behind an earlier one. The
 * in-flight list and the result queue are sized by it too, so none of them can fill. When the ring is full
 * the coordinator stops reading the log and only collects results until the frontier moves. */
#define LA_COMMIT_ORDER_CAPACITY(worker_count)  ((worker_count) * LA_TASK_QUEUE_CAPACITY)

typedef struct la_worker LA_WORKER;
struct la_worker
{
  pthread_t tid;
  int index;			/* slot in the worker array, set at start; identifies the worker and attributes
				 * per-worker stats */
  bool started;			/* a thread exists for this slot and must be joined */

  /* debug only: loose hint for status output; drain decisions use the coordinator's in-flight list, so no
   * strict synchronization is needed */
  bool busy;
};

/* a worker thread's own client connection to the server */
typedef struct la_worker_session LA_WORKER_SESSION;
struct la_worker_session
{
  bool client_context_started;
};

/* Applies one transaction on the calling worker and fills in its result; owned by log_applier.c. */
typedef void (*LA_WORKER_RUN_TASK_FUNC) (LA_APPLY_WORKER_CONTEXT *context, LA_TASK *task, int trace_level,
    unsigned long long *applied_item_count, LA_RESULT *result);

/* Tells the applier to stop: no worker is left, or a worker hit an error it cannot continue after. */
typedef void (*LA_WORKER_REQUEST_SHUTDOWN_FUNC) (void);

/* Releases what run_task kept in the context; called while the worker's connection is still open. */
typedef void (*LA_WORKER_END_CONTEXT_FUNC) (LA_APPLY_WORKER_CONTEXT *context);

typedef struct la_worker_config LA_WORKER_CONFIG;
struct la_worker_config
{
  int db_page_size;		/* sizes each worker's unzip buffers and record pool */
  int recdes_pool_size;		/* records per worker; the flush batch size */
  int trace_level;		/* ha_writeset_trace_level, read once at start */
  LA_WORKER_RUN_TASK_FUNC run_task;
  LA_WORKER_REQUEST_SHUTDOWN_FUNC request_shutdown;
  LA_WORKER_END_CONTEXT_FUNC end_context;
};

extern int la_worker_start (LA_WORKER *workers, int count, const LA_WORKER_CONFIG *config);
extern void la_worker_join (LA_WORKER *workers, int count);
extern int la_worker_start_session (LA_WORKER_SESSION *session, int sub_index);
extern void la_worker_end_session (LA_WORKER_SESSION *session);

#endif /* _LOG_APPLIER_WORKER_HPP_ */
