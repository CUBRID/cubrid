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
#include "log_applier_task_queue.hpp"	/* LA_TASK_QUEUE_CAPACITY */
#include "log_applier_result.hpp"
#include "log_applier_worker_context.hpp"

#define LA_MAX_WORKER_COUNT         HB_MAX_APPLY_WORKER_COUNT	/* per-worker arrays are this long; the number of
								 * workers actually started is ha_apply_worker_count */

/* A worker applies the tasks it takes into one open slave transaction and commits them together, so a
 * transaction on the master does not cost a commit and a log flush on the slave. It commits when:
 *   - no task is waiting once the transaction has been open LA_WORKER_BATCH_EMPTY_HOLD_USEC; until then it waits
 *     that long for the next task, so a queue that runs dry for a moment does not end the batch
 *   - the transaction holds LA_WORKER_BATCH_MAX_TASKS tasks
 *   - the transaction has been open LA_WORKER_BATCH_MAX_MSEC
 * The last two bound how late the changes show on the slave and how long their locks are held.
 *
 * The limits are fixed, not parameters, because they follow from the applier rather than from the workload:
 *   - with a backlog of single-row transactions, 500 tasks was the fastest for one and for ten workers;
 *     100 commits too often, and 1000 saves almost no commit
 *   - a backlog reaches the task limit long before the time limit, which only bounds an open transaction while
 *     the queue keeps receiving tasks
 *   - the task limit must stay well below one worker's share of the commit-order ring (LA_COMMIT_ORDER_CAPACITY)
 */
#define LA_WORKER_BATCH_MAX_TASKS   500
#define LA_WORKER_BATCH_MAX_MSEC    100
#define LA_WORKER_BATCH_EMPTY_HOLD_USEC 500

/* Size of the gate's commit-order ring: the most apply units read from the log that the gap-free frontier has
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

/* Flushes and commits the tasks applied in the worker's open slave transaction; stats are those of the last task.
 * Owned by log_applier.c. */
typedef int (*LA_WORKER_COMMIT_BATCH_FUNC) (LA_APPLY_WORKER_CONTEXT *context, int task_count,
    unsigned long long applied_item_count, LA_STATS *stats);

/* Rolls back the worker's open slave transaction; owned by log_applier.c. */
typedef void (*LA_WORKER_ABORT_BATCH_FUNC) (LA_APPLY_WORKER_CONTEXT *context);

/* Tells the applier to stop: no worker is left, or a worker hit an error it cannot continue after. */
typedef void (*LA_WORKER_REQUEST_SHUTDOWN_FUNC) (void);

/* Tells whether the applier is stopping, so a worker-module wait can give up; owned by log_applier.c. */
typedef bool (*LA_WORKER_SHUTDOWN_REQUESTED_FUNC) (void);

/* Releases what run_task kept in the context; called while the worker's connection is still open. */
typedef void (*LA_WORKER_END_CONTEXT_FUNC) (LA_APPLY_WORKER_CONTEXT *context);

typedef struct la_worker_config LA_WORKER_CONFIG;
struct la_worker_config
{
  int db_page_size;		/* sizes each worker's unzip buffers and record pool */
  int recdes_pool_size;		/* records per worker; the flush batch size */
  int trace_level;		/* ha_writeset_trace_level, read once at start */
  LA_WORKER_RUN_TASK_FUNC run_task;
  LA_WORKER_COMMIT_BATCH_FUNC commit_batch;
  LA_WORKER_ABORT_BATCH_FUNC abort_batch;
  LA_WORKER_REQUEST_SHUTDOWN_FUNC request_shutdown;
  LA_WORKER_SHUTDOWN_REQUESTED_FUNC shutdown_requested;
  LA_WORKER_END_CONTEXT_FUNC end_context;
};

extern int la_worker_start (LA_WORKER *workers, int count, const LA_WORKER_CONFIG *config);
extern int la_worker_wait_started (const LA_WORKER *workers, int count, int *live, int *live_count);
extern void la_worker_join (LA_WORKER *workers, int count);
extern int la_worker_start_session (LA_WORKER_SESSION *session, int sub_index);
extern void la_worker_end_session (LA_WORKER_SESSION *session);

#endif /* _LOG_APPLIER_WORKER_HPP_ */
