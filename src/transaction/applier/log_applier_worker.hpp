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
 *   - the transaction has been open for the commit interval (LA_WORKER_CONFIG.batch_max_msec, which is
 *     ha_applylogdb_max_commit_interval_in_msecs, the interval the serial applier commits at)
 *   - the transaction holds LA_WORKER_BATCH_MAX_TASKS tasks
 *   - no task is waiting once the transaction has been open LA_WORKER_BATCH_EMPTY_HOLD_USEC, unless the coordinator
 *     is still reading log, no task waits for another to commit and the coordinator is not waiting for results
 *     (LA_WORKER_CONFIG.keep_batch_open); the coordinator wakes a worker waiting on an empty queue when that changes
 *   - a task waits on the coordinator's pending list and the transaction has been open
 *     LA_WORKER_BATCH_PARKED_MAX_MSEC (LA_WORKER_CONFIG.tasks_parked)
 * The first condition bounds how late the changes show on the slave and how long their locks are held. While a
 * backlog is read, a worker whose queue runs dry for a moment keeps its transaction open, so the slave commits about
 * as often as the serial applier did: a commit costs a log flush, and on a device with slow syncs small commits
 * made the backlog apply at half the serial applier's rate. Once the coordinator has read to the end of the log,
 * or a task waits for another to commit, an empty queue ends the transaction after the short hold, so the changes
 * of a lightly loaded master show without the interval's delay.
 *
 * The task limit bounds the results a worker keeps and the apply units its open transaction holds in memory. It
 * is part of a worker's share of the commit-order ring (LA_COMMIT_ORDER_CAPACITY), since an uncommitted task has
 * not retired.
 */
#define LA_WORKER_BATCH_MAX_TASKS   10000
#define LA_WORKER_BATCH_EMPTY_HOLD_USEC 500
/* while a task is parked, a worker commits its open transaction after this long even if its queue keeps receiving
 * tasks (la_worker_batch_is_due) */
#define LA_WORKER_BATCH_PARKED_MAX_MSEC 100

/* Size of the gate's commit-order ring: the most apply units read from the log that the gap-free frontier has
 * not passed yet, whether parked, queued, running, or finished but waiting behind an earlier one. Each worker may
 * hold a full queue and a full open transaction. The in-flight list and the result queue are sized by it too, so
 * none of them can fill. When the ring is full the coordinator stops reading the log and only collects results
 * until the frontier moves. */
#define LA_COMMIT_ORDER_CAPACITY(worker_count)  ((worker_count) * (LA_TASK_QUEUE_CAPACITY + LA_WORKER_BATCH_MAX_TASKS))

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

/* Tells whether a worker with an open transaction may keep it open through an empty queue: the coordinator has
 * more log to read and no task waits for another to commit. Owned by the coordinator; called by worker threads. */
typedef bool (*LA_WORKER_KEEP_BATCH_OPEN_FUNC) (void);

/* Tells whether a task waits on the coordinator's pending list. Owned by the coordinator; called by worker threads. */
typedef bool (*LA_WORKER_TASKS_PARKED_FUNC) (void);

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
  LA_WORKER_KEEP_BATCH_OPEN_FUNC keep_batch_open;
  LA_WORKER_TASKS_PARKED_FUNC tasks_parked;
  int batch_max_msec;		/* commit interval: ha_applylogdb_max_commit_interval_in_msecs, read once at start */
};

extern int la_worker_start (LA_WORKER *workers, int count, const LA_WORKER_CONFIG *config);
extern int la_worker_wait_started (const LA_WORKER *workers, int count, int *live, int *live_count);
extern void la_worker_join (LA_WORKER *workers, int count);
extern int la_worker_start_session (LA_WORKER_SESSION *session, int sub_index);
extern void la_worker_end_session (LA_WORKER_SESSION *session);

#endif /* _LOG_APPLIER_WORKER_HPP_ */
