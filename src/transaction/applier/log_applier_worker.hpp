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
 * log_applier_worker.hpp - parallel applylogdb worker threads
 *
 * A worker is one applier thread with its own server connection. The module owns the thread's life
 * cycle (start, session, apply context, task loop, exit) and the per-worker apply context; how one
 * transaction is applied stays in log_applier.c and is handed in as the run_task callback.
 */

#ifndef _LOG_APPLIER_WORKER_HPP_
#define _LOG_APPLIER_WORKER_HPP_

#include <pthread.h>

#include "porting.h"
#include "storage_common.h"	/* RECDES */
#include "log_compress.h"	/* LOG_ZIP */
#include "heartbeat.h"		/* HB_MAX_APPLY_WORKER_COUNT */
#include "log_applier_task.hpp"
#include "log_applier_result.hpp"

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

/* Record buffers of one worker. Replication items are not sent one by one: they collect on the client
 * side until la_flush_repl_items sends a batch, so every item's record must stay allocated until then.
 * The pool therefore holds as many records as a flush batch, each one db page long, and hands them out
 * in a ring. A record larger than a page gets its own allocation, which the next assignment of that slot
 * frees again. */
typedef struct la_recdes_pool LA_RECDES_POOL;
struct la_recdes_pool
{
  RECDES *recdes_arr;
  char *area;			/* continuous area for recdes data */
  int next_idx;
  int db_page_size;
  int num_recdes;
  bool is_initialized;
};

/* State one worker thread needs while it rebuilds and applies records. In the serial applier these
 * were globals; each worker has its own copy so the threads never share them. */
typedef struct la_apply_worker_context LA_APPLY_WORKER_CONTEXT;
struct la_apply_worker_context
{
  int worker_idx;
  INT16 rec_type;		/* record type of the row being rebuilt, written by la_get_recdes */
  LOG_ZIP *undo_unzip_ptr;
  LOG_ZIP *redo_unzip_ptr;
  LA_RECDES_POOL recdes_pool;
};

/* Applies one transaction on the calling worker and fills in its result; owned by log_applier.c. */
typedef void (*LA_WORKER_RUN_TASK_FUNC) (LA_APPLY_WORKER_CONTEXT *context, LA_TASK *task, int trace_level,
    unsigned long long *applied_item_count, LA_RESULT *result);

/* Tells the applier to stop: no worker is left, or a worker hit an error it cannot continue after. */
typedef void (*LA_WORKER_REQUEST_SHUTDOWN_FUNC) (void);

typedef struct la_worker_config LA_WORKER_CONFIG;
struct la_worker_config
{
  int db_page_size;		/* sizes each worker's unzip buffers and record pool */
  int recdes_pool_size;		/* records per worker; the flush batch size */
  int trace_level;		/* ha_writeset_trace_level, read once at start */
  LA_WORKER_RUN_TASK_FUNC run_task;
  LA_WORKER_REQUEST_SHUTDOWN_FUNC request_shutdown;
};

extern int la_worker_start (LA_WORKER *workers, int count, const LA_WORKER_CONFIG *config);
extern void la_worker_join (LA_WORKER *workers, int count);
extern int la_worker_start_session (LA_WORKER_SESSION *session, int sub_index);
extern void la_worker_end_session (LA_WORKER_SESSION *session);

extern RECDES *la_assign_recdes_from_pool (LA_APPLY_WORKER_CONTEXT *context);
extern int la_realloc_recdes_data (LA_APPLY_WORKER_CONTEXT *context, RECDES *recdes, int data_size);

#endif /* _LOG_APPLIER_WORKER_HPP_ */
