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
 * log_applier_worker.cpp - implementation of parallel apply worker threads
 */

#include "config.h"

#include "log_applier_worker.hpp"
#include "log_applier.h"	/* LA_IS_FLUSH_ERROR */
#include "log_applier_task_queue.hpp"
#include "log_applier_clock.hpp"
#include "log_applier_result_queue.hpp"
#include "log_applier_debug.hpp"
#include "error_manager.h"
#include "error_context.hpp"
#include "db.h"
#include "locator_cl.h"
#include "trigger_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static pthread_mutex_t la_Worker_session_mutex = PTHREAD_MUTEX_INITIALIZER;	/* brings sessions up one at a time */

static LA_WORKER_CONFIG la_Worker_config;	/* copied at start; read by every worker thread */
static int la_Worker_count = 0;	/* workers started */

/* workers that can take tasks. Set to the number started; a worker that cannot open its session or apply
 * context takes itself out. Only when the last one is gone does the applier stop. The coordinator waits until
 * every worker has started or failed (la_worker_wait_started) and then sends tasks only to the workers that
 * started, so fewer workers only cost parallelism, never correctness. */
static volatile INT32 la_Live_worker_count = 0;

/* how far each worker got in its startup; la_worker_wait_started waits until none is still starting */
typedef enum
{
  LA_WORKER_STARTING = 0,
  LA_WORKER_LIVE,
  LA_WORKER_START_FAILED
} LA_WORKER_START_STATE;

static pthread_mutex_t la_Worker_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t la_Worker_start_cond = PTHREAD_COND_INITIALIZER;	/* on CLOCK_MONOTONIC from la_worker_start */
static LA_WORKER_START_STATE la_Worker_start_state[LA_MAX_WORKER_COUNT];

/* how often la_worker_wait_started looks at the shutdown request while a worker is still starting */
#define LA_WORKER_START_WAIT_MSEC 100

static void la_worker_init (LA_WORKER *worker, int index);
static void *la_worker_main (void *arg);
static int la_worker_run (LA_WORKER *worker, LA_APPLY_WORKER_CONTEXT *context,
			  unsigned long long *worker_applied_item_count);
static void la_worker_startup_failed (const LA_WORKER *worker, const char *reason, int error);
static void la_worker_set_start_state (int index, LA_WORKER_START_STATE state);
#if !defined (NDEBUG)
static int la_worker_test_fail_startup (const LA_WORKER *worker);
#endif /* !NDEBUG */

/*
 * la_worker_start() - reset the worker slots and start one thread per slot
 *   return: NO_ERROR, or ER_FAILED when a thread cannot be created
 *   workers(out): worker slots to start
 *   count(in): number of slots
 *   config(in): what every worker needs: buffer sizes, trace level and the applier's callbacks
 *
 * Note:
 *   Returns without waiting for the workers to open their sessions. Before it hands out the first task, the
 *   caller waits with la_worker_wait_started and gives tasks only to the workers that started; a worker that
 *   cannot start steps out on its own (la_worker_startup_failed), so fewer workers only cost parallelism.
 *   On failure the threads already started keep running; they are marked started, so the caller's
 *   shutdown path still joins them through la_worker_join.
 */
int
la_worker_start (LA_WORKER *workers, int count, const LA_WORKER_CONFIG *config)
{
  int i;

  assert (workers != NULL && config != NULL);
  assert (config->run_task != NULL && config->commit_batch != NULL && config->abort_batch != NULL
	  && config->request_shutdown != NULL && config->shutdown_requested != NULL && config->end_context != NULL);
  assert (count > 0 && count <= LA_MAX_WORKER_COUNT);

  la_Worker_config = *config;
  la_Worker_count = count;
  la_Live_worker_count = count;

  /* no worker thread exists yet, so nothing waits on the condition while it is set up again */
  (void) pthread_cond_destroy (&la_Worker_start_cond);
  la_cond_init_monotonic (&la_Worker_start_cond);

  pthread_mutex_lock (&la_Worker_start_mutex);
  for (i = 0; i < count; i++)
    {
      la_Worker_start_state[i] = LA_WORKER_STARTING;
    }
  pthread_mutex_unlock (&la_Worker_start_mutex);

  for (i = 0; i < count; i++)
    {
      assert (workers[i].started == false);

      la_worker_init (&workers[i], i);

      if (pthread_create (&workers[i].tid, NULL, la_worker_main, &workers[i]) != 0)
	{
	  return ER_FAILED;
	}

      workers[i].started = true;
    }

  return NO_ERROR;
}

/*
 * la_worker_wait_started() - wait until every started worker has either started or failed to start
 *   return: NO_ERROR, ER_HA_GENERIC_ERROR when no worker started, or ER_INTERRUPTED when the applier is
 *           shutting down
 *   workers(in): worker slots passed to la_worker_start
 *   count(in): number of slots
 *   live(out): indexes of the workers that started, in increasing order; at least count entries
 *   live_count(out): number of entries in live
 *
 * Note:
 *   A worker has started once its session and apply context are open and it is about to take tasks. A task
 *   given to a worker that failed to start would never be taken, and the tasks after it would wait for it
 *   forever, so the caller must hand out no task before this returns and then only to the workers in live.
 *   A worker that is still opening its session may wait for the server, so the shutdown request is checked
 *   every LA_WORKER_START_WAIT_MSEC; the caller then stops the applier as for any other startup error.
 */
int
la_worker_wait_started (const LA_WORKER *workers, int count, int *live, int *live_count)
{
  int starting;
  int i;

  assert (workers != NULL && live != NULL && live_count != NULL);
  assert (count > 0 && count <= LA_MAX_WORKER_COUNT);

  *live_count = 0;

  pthread_mutex_lock (&la_Worker_start_mutex);
  while (true)
    {
      struct timespec abstime;

      starting = 0;
      for (i = 0; i < count; i++)
	{
	  if (workers[i].started && la_Worker_start_state[i] == LA_WORKER_STARTING)
	    {
	      starting++;
	    }
	}
      if (starting == 0)
	{
	  break;
	}

      if (la_Worker_config.shutdown_requested ())
	{
	  pthread_mutex_unlock (&la_Worker_start_mutex);
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  return ER_INTERRUPTED;
	}

      la_monotonic_deadline ((UINT64) LA_WORKER_START_WAIT_MSEC * 1000000ULL, &abstime);
      (void) pthread_cond_timedwait (&la_Worker_start_cond, &la_Worker_start_mutex, &abstime);
    }

  for (i = 0; i < count; i++)
    {
      if (workers[i].started && la_Worker_start_state[i] == LA_WORKER_LIVE)
	{
	  live[(*live_count)++] = i;
	}
    }
  pthread_mutex_unlock (&la_Worker_start_mutex);

  if (*live_count == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "no apply worker could start");
      return ER_HA_GENERIC_ERROR;
    }

  return NO_ERROR;
}

/*
 * la_worker_set_start_state() - record how far a worker got in its startup and wake la_worker_wait_started
 *   index(in): the worker's slot number
 *   state(in): LA_WORKER_LIVE or LA_WORKER_START_FAILED
 */
static void
la_worker_set_start_state (int index, LA_WORKER_START_STATE state)
{
  assert (index >= 0 && index < LA_MAX_WORKER_COUNT);

  pthread_mutex_lock (&la_Worker_start_mutex);
  la_Worker_start_state[index] = state;
  pthread_cond_broadcast (&la_Worker_start_cond);
  pthread_mutex_unlock (&la_Worker_start_mutex);
}

/*
 * la_worker_join() - wait for every started worker thread to exit
 *   workers(in/out): worker slots; slots that were never started are left alone
 *   count(in): number of slots
 */
void
la_worker_join (LA_WORKER *workers, int count)
{
  int i;

  assert (workers != NULL);

  for (i = 0; i < count; i++)
    {
      if (workers[i].started == false)
	{
	  continue;
	}

      pthread_join (workers[i].tid, NULL);
      workers[i].started = false;
    }
}

/*
 * la_worker_start_session() - open the calling worker thread's own client connection
 *   return: NO_ERROR or the error from db_restart_sub
 *   session(out): session state of the worker
 *   sub_index(in): sub-client index, the worker's slot number
 *
 * Note:
 *   db_restart_sub leaves trigger initialization out, so the worker sets triggers up and then
 *   disables them itself; replication must not fire triggers again on the slave.
 */
int
la_worker_start_session (LA_WORKER_SESSION *session, int sub_index)
{
  int error = NO_ERROR;

  assert (session != NULL);

  pthread_mutex_lock (&la_Worker_session_mutex);

#if defined(CS_MODE) && defined(MULTI_CONN_TO_A_SERVER)
  memset (session, 0, sizeof (*session));

  /* db_shutdown_sub must follow db_restart_sub whether it succeeds or not: a failed restart keeps the
   * thread's error context so that the caller can report the error, and only db_shutdown_sub releases it */
  session->client_context_started = true;

  error = db_restart_sub (sub_index);
  if (error != NO_ERROR)
    {
      pthread_mutex_unlock (&la_Worker_session_mutex);
      return error;
    }

  tr_init ();
  (void) db_disable_trigger ();
#endif

  __gv_loc_repl.ws_init_repl_objs ();

  pthread_mutex_unlock (&la_Worker_session_mutex);

  return error;
}

/*
 * la_worker_end_session() - close the calling worker thread's client connection
 *   session(in/out): session state of the worker
 */
void
la_worker_end_session (LA_WORKER_SESSION *session)
{
  assert (session != NULL);

  __gv_loc_repl.ws_clear_all_repl_objs ();
  __gv_loc_repl.ws_clear_all_repl_errors_of_error_link ();

#if defined(CS_MODE) && defined(MULTI_CONN_TO_A_SERVER)
  if (session->client_context_started)
    {
      (void) db_shutdown_sub ();
      session->client_context_started = false;
    }
#endif
}

/*
 * la_worker_init() - reset a worker slot before its thread starts
 *   worker(out): worker slot to reset
 *   index(in): slot number in the worker array
 */
static void
la_worker_init (LA_WORKER *worker, int index)
{
  memset (worker, 0, sizeof (*worker));
  worker->index = index;
}

/*
 * la_worker_main() - thread body of one worker: open the session and context, run the task loop, clean up
 *   arg(in): the worker's slot (LA_WORKER *)
 *
 * Note:
 *   A startup failure takes this worker out of the pool and the thread exits; the others keep
 *   applying. An apply failure asks the whole applier to stop.
 */
static void *
la_worker_main (void *arg)
{
  LA_WORKER *worker = (LA_WORKER *) arg;
  LA_WORKER_SESSION session;
  LA_APPLY_WORKER_CONTEXT worker_context;
#if !defined(CS_MODE) || !defined(MULTI_CONN_TO_A_SERVER)
  cuberr::context *er_context_p = NULL;
#endif
  bool session_started = false;
  bool worker_context_started = false;
  int error = NO_ERROR;
  unsigned long long worker_applied_item_count = 0;
  const char *exit_reason = "normal_shutdown";

#if !defined(CS_MODE) || !defined(MULTI_CONN_TO_A_SERVER)
  /* with sub-clients, db_restart_sub creates and registers this thread's error context, and db_shutdown_sub
   * destroys it; a second one registered here would collide with it */
  er_context_p = new cuberr::context ();
  er_context_p->register_thread_local ();
#endif

  error = la_worker_start_session (&session, worker->index);

  /* closed at the end even when it failed to start; until then the failed sub-client keeps the error context
   * the failure is reported through */
  session_started = true;
#if !defined (NDEBUG)
  if (error == NO_ERROR)
    {
      error = la_worker_test_fail_startup (worker);
    }
#endif /* !NDEBUG */

  if (error != NO_ERROR)
    {
      exit_reason = "start_session_failed";
      la_worker_startup_failed (worker, exit_reason, error);
      goto end;
    }

  error = la_worker_context_init (&worker_context, la_Worker_config.db_page_size, la_Worker_config.recdes_pool_size);
  if (error != NO_ERROR)
    {
      exit_reason = "worker_context_init_failed";
      la_worker_startup_failed (worker, exit_reason, error);
      goto end;
    }

  worker_context.worker_idx = worker->index;
  worker_context_started = true;

  la_worker_set_start_state (worker->index, LA_WORKER_LIVE);

  error = la_worker_run (worker, &worker_context, &worker_applied_item_count);
  if (error != NO_ERROR)
    {
      exit_reason = "apply_failed";
      la_Worker_config.request_shutdown ();
    }

end:
  er_log_debug (ARG_FILE_LINE, "ws_worker exit worker=%d reason=%s error=%d applied=%llu\n",
		worker->index, exit_reason, error, worker_applied_item_count);

  if (worker_context_started)
    {
      la_Worker_config.end_context (&worker_context);
      la_worker_context_final (&worker_context);
    }

  if (session_started)
    {
      la_worker_end_session (&session);
    }

  /* The locator area cache is one per process and shared by every worker; it is released once by the
   * client finalization after all workers have been joined. */

#if !defined(CS_MODE) || !defined(MULTI_CONN_TO_A_SERVER)
  er_context_p->deregister_thread_local ();
  delete er_context_p;
#endif

  return NULL;
}

/* tasks a worker has applied into its open slave transaction and not committed yet */
typedef struct la_worker_batch LA_WORKER_BATCH;
struct la_worker_batch
{
  LA_RESULT *results;		/* LA_WORKER_BATCH_MAX_TASKS slots, in the order the tasks were taken */
  int count;
  UINT64 begin_ns;		/* when the first of them was applied */
};

/*
 * la_worker_empty_hold_nsec() - how long an open batch may wait on an empty queue before it is committed
 *   return: nanoseconds to wait for another task, 0 to commit now
 *   batch(in): the open batch
 *
 * Note: the batch is committed on an empty queue only once it has been open LA_WORKER_BATCH_EMPTY_HOLD_USEC.
 */
static UINT64
la_worker_empty_hold_nsec (const LA_WORKER_BATCH *batch)
{
  UINT64 now_ns, deadline_ns;

  deadline_ns = batch->begin_ns + (UINT64) LA_WORKER_BATCH_EMPTY_HOLD_USEC * 1000ULL;
  now_ns = la_monotonic_now_ns ();

  return (now_ns < deadline_ns) ? deadline_ns - now_ns : 0;
}

/*
 * la_worker_commit_and_report() - commit the worker's open slave transaction and hand back the results of its tasks
 *   return: NO_ERROR, or the commit error
 *   context(in): the worker's apply context
 *   batch(in/out): the uncommitted tasks; empty on return
 *   worker_applied_item_count(in): items this worker has applied so far
 *   queue_closed(out): the result queue shut down while the results were handed back
 *
 * Note:
 *   Results go back only after the commit, so no task is completed while the slave could still lose it; the
 *   commit time counts as busy time of the last task. A failed commit reports the first task with the error and
 *   stops the worker; a restart applies them all again. Results the closed result queue cannot take are dropped:
 *   their changes and markers are committed, so a restart skips them.
 */
static int
la_worker_commit_and_report (LA_APPLY_WORKER_CONTEXT *context, LA_WORKER_BATCH *batch,
			     unsigned long long worker_applied_item_count, bool *queue_closed)
{
  UINT64 commit_begin_ns;
  UINT64 commit_end_ns;
  int error;
  int i;

  assert (batch->count > 0);

  *queue_closed = false;

  commit_begin_ns = la_monotonic_now_ns ();
  error = la_Worker_config.commit_batch (context, batch->count, worker_applied_item_count,
				       &batch->results[batch->count - 1].stats);
  if (error != NO_ERROR)
    {
      batch->results[0].error = error;
      (void) la_result_queue_push (&batch->results[0]);
      batch->count = 0;
      return error;
    }

  commit_end_ns = la_monotonic_now_ns ();
  if (commit_end_ns >= commit_begin_ns)
    {
      batch->results[batch->count - 1].busy_usec += (commit_end_ns - commit_begin_ns) / 1000ULL;
    }

  for (i = 0; i < batch->count; i++)
    {
      if (la_result_queue_push (&batch->results[i]) != NO_ERROR)
	{
	  /* the result queue shuts down only when the coordinator stops the workers */
	  *queue_closed = true;
	  break;
	}
    }

  batch->count = 0;

  return NO_ERROR;
}

/*
 * la_worker_run() - take tasks from the task queue, apply them and hand their results back until shutdown
 *   return: NO_ERROR when a queue shuts down, or the error that stopped the worker
 *   worker(in/out): the calling worker; busy is set while a task is in hand
 *   context(in/out): the worker's apply context
 *   worker_applied_item_count(in/out): items this worker has applied so far
 *
 * Note:
 *   The worker applies the tasks it takes into one open slave transaction and commits them together when no
 *   task is waiting in the queue and the first of them was applied LA_WORKER_BATCH_EMPTY_HOLD_USEC ago (until then
 *   it waits for the next task), when it holds LA_WORKER_BATCH_MAX_TASKS of them, or when the first of them was
 *   applied LA_WORKER_BATCH_MAX_MSEC ago. Each task's marker is in the same transaction as its changes, so
 *   a restart skips exactly the tasks that were committed. A task with a statement item is applied in a
 *   transaction of its own: the statement takes la_sql_compile_mutex, and the worker must not hold a lock of
 *   another task then (see la_sql_compile_mutex in log_applier.c).
 *
 *   Returns when either queue shuts down. Tasks not committed then are rolled back and results that cannot
 *   be queued are dropped; they were never retired, so the restart point stays behind them.
 *
 *   Any error stops the worker, as it stops the serial applier. A failed task rolls back the whole open
 *   transaction, so the tasks taken before it in that transaction are undone too. The first task of the
 *   transaction is reported with the error and the others not at all, so none of them is retired and a restart
 *   applies them again in order. Going on would let a later task commit while an earlier one was undone.
 */
static int
la_worker_run (LA_WORKER *worker, LA_APPLY_WORKER_CONTEXT *context, unsigned long long *worker_applied_item_count)
{
  int trace_level = la_Worker_config.trace_level;
  LA_WORKER_BATCH batch;
  bool queue_closed = false;
  int error = NO_ERROR;

  batch.results = (LA_RESULT *) malloc (sizeof (LA_RESULT) * LA_WORKER_BATCH_MAX_TASKS);
  if (batch.results == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (LA_RESULT) * LA_WORKER_BATCH_MAX_TASKS);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  batch.count = 0;
  batch.begin_ns = 0;

  while (true)
    {
      LA_TASK task;
      LA_RESULT result;
      struct timeval task_begin;
      bool trace_task;
      bool taken;

      if (batch.count > 0)
	{
	  UINT64 hold_nsec = la_worker_empty_hold_nsec (&batch);

	  if (la_task_queue_try_pop (&task, &taken, context->worker_idx, hold_nsec) != NO_ERROR)
	    {
	      break;
	    }

	  if (!taken)
	    {
	      /* nothing to add: commit rather than hold their locks */
	      error = la_worker_commit_and_report (context, &batch, *worker_applied_item_count, &queue_closed);
	      if (error != NO_ERROR || queue_closed)
		{
		  break;
		}
	      continue;
	    }
	}
      else if (la_task_queue_pop (&task, context->worker_idx) != NO_ERROR)
	{
	  break;
	}

      worker->busy = true;

      if (task.has_statement && batch.count > 0)
	{
	  error = la_worker_commit_and_report (context, &batch, *worker_applied_item_count, &queue_closed);
	  if (error != NO_ERROR || queue_closed)
	    {
	      /* the task taken is dropped with the queued ones; it was never retired */
	      break;
	    }
	}

      la_result_init (&result, &task, context->worker_idx);
      result.dequeue_ns = la_monotonic_now_ns ();

      /* A task without replicated items (an empty commit) is not traced: it applies nothing and would only
       * add lines. */
      trace_task = (trace_level >= 1 && task.has_items);

      if (trace_task)
	{
	  LA_TIME_BEGIN (task_begin);
	  LA_WSET_TRACE_IF (trace_task,
			    "worker apply begin trid=%d commit_lsa=%lld|%d type=%d worker=%zu dispatch=%llu\n",
			    task.tranid, LSA_AS_ARGS (&task.commit_lsa), task.rectype,
			    (size_t) context->worker_idx, (unsigned long long) task.dispatch_seq);
	}

      result.apply_begin_ns = la_monotonic_now_ns ();
      la_Worker_config.run_task (context, &task, trace_level, worker_applied_item_count, &result);

      if (trace_task)
	{
	  UINT64 elapsed_usec = 0;
	  /* the table is named only when the transaction touched exactly one; otherwise trid identifies it */
	  bool one_class = (!result.stats.multi_class && result.stats.last_class_name[0] != '\0');

	  LA_TIME_ACCUM_USEC (task_begin, elapsed_usec);
	  LA_WSET_TRACE_IF (trace_task,
			    "worker apply done trid=%d commit_lsa=%lld|%d type=%d worker=%zu dispatch=%llu "
			    "error=%d elapsed_usec=%llu batch=%d%s%s\n",
			    task.tranid, LSA_AS_ARGS (&task.commit_lsa),
			    task.rectype, (size_t) context->worker_idx, (unsigned long long) task.dispatch_seq,
			    result.error,
			    (unsigned long long) elapsed_usec, batch.count + 1, one_class ? " class=" : "",
			    one_class ? result.stats.last_class_name : "");
	}

      if (result.error != NO_ERROR)
	{
	  /* run_task rolled back the open transaction, the tasks taken before this one included */
	  if (batch.count > 0)
	    {
	      char buf[256];

	      /* The error goes back under the first task of the batch, the one retire reaches first; that result
	       * names another transaction, so log the one that failed. */
	      snprintf (buf, sizeof (buf), "apply worker %d: transaction %d (commit %lld|%d) failed with error %d; "
			"the %d tasks before it in the same slave transaction were rolled back with it",
			context->worker_idx, task.tranid, LSA_AS_ARGS (&task.commit_lsa), result.error, batch.count);
	      er_stack_push ();
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, buf);
	      er_stack_pop ();

	      batch.results[0].error = result.error;
	      (void) la_result_queue_push (&batch.results[0]);
	      batch.count = 0;
	    }
	  else
	    {
	      (void) la_result_queue_push (&result);
	    }
	  worker->busy = false;
	  error = result.error;
	  break;
	}

      if (batch.count == 0)
	{
	  batch.begin_ns = result.apply_begin_ns;
	}
      batch.results[batch.count++] = result;

      worker->busy = false;

      if (task.has_statement || batch.count >= LA_WORKER_BATCH_MAX_TASKS
	  || la_monotonic_now_ns () - batch.begin_ns >= (UINT64) LA_WORKER_BATCH_MAX_MSEC * 1000000ULL)
	{
	  error = la_worker_commit_and_report (context, &batch, *worker_applied_item_count, &queue_closed);
	  if (error != NO_ERROR || queue_closed)
	    {
	      break;
	    }
	}
    }

  if (batch.count > 0)
    {
      la_Worker_config.abort_batch (context);
      batch.count = 0;
    }

  free_and_init (batch.results);

  return error;
}

/*
 * la_worker_startup_failed() - take a worker that could not start out of the pool
 *   worker(in): the worker leaving
 *   reason(in): which startup step failed
 *   error(in): its error
 *
 * Note:
 *   The remaining workers keep applying; the failure is only logged. The coordinator gives no task to this
 *   worker, since it hands out tasks only after la_worker_wait_started has seen every worker start or fail.
 *   When no worker is left nothing would ever take a task, so the applier is told to stop.
 *
 *   TODO: Start a replacement worker so that the configured worker count is kept, instead of running on
 *         with fewer workers. The replacement needs a retry interval and limit; a cause outside the
 *         process, such as an unreachable server, makes it fail again, and when it gives up the applier
 *         must still stop as it does now.
 */
static void
la_worker_startup_failed (const LA_WORKER *worker, const char *reason, int error)
{
  char buffer[256];
  INT32 remaining = ATOMIC_INC_32 (&la_Live_worker_count, -1);

  snprintf (buffer, sizeof (buffer), "apply worker %d could not start (%s, error %d); %d of %d workers remain",
	    worker->index, reason, error, (int) remaining, la_Worker_count);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, buffer);

  la_worker_set_start_state (worker->index, LA_WORKER_START_FAILED);

  if (remaining <= 0)
    {
      la_Worker_config.request_shutdown ();
    }
}

#if !defined (NDEBUG)
/*
 * la_worker_test_fail_startup() - test hook that makes one worker fail its startup
 *   return: NO_ERROR, or ER_FAILED when CUBRID_HA_TEST_FAIL_WORKER names this worker
 *   worker(in): the worker starting
 *
 * Note:
 *   Debug builds only. Called right after the worker opened its session, so the worker leaves through the same
 *   path as when db_restart_sub fails: it is taken out of the pool and its session is closed. The session is
 *   opened for real first, because closing a session that was never opened is not the state a failed
 *   db_restart_sub leaves.
 */
static int
la_worker_test_fail_startup (const LA_WORKER *worker)
{
  const char *fail_worker = getenv ("CUBRID_HA_TEST_FAIL_WORKER");
  char buffer[128];

  if (fail_worker == NULL || fail_worker[0] == '\0' || atoi (fail_worker) != worker->index)
    {
      return NO_ERROR;
    }

  snprintf (buffer, sizeof (buffer), "test: apply worker %d fails its startup (CUBRID_HA_TEST_FAIL_WORKER)",
	    worker->index);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, buffer);

  return ER_FAILED;
}
#endif /* !NDEBUG */

