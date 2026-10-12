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
 * log_applier_worker.cpp - parallel applylogdb worker threads
 */

#include "config.h"

#include "log_applier_worker.hpp"
#include "log_applier.h"	/* LA_IS_FLUSH_ERROR */
#include "log_applier_task_queue.hpp"
#include "log_applier_result_queue.hpp"
#include "log_applier_debug.hpp"
#include "error_manager.h"
#include "error_context.hpp"
#include "db.h"
#include "locator_cl.h"
#include "trigger_manager.h"
#include "file_io.h"		/* IO_MIN_PAGE_SIZE, IO_MAX_PAGE_SIZE */

#include <assert.h>
#include <stdio.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static pthread_mutex_t la_Worker_session_mutex = PTHREAD_MUTEX_INITIALIZER;	/* brings sessions up one at a time */

static LA_WORKER_CONFIG la_Worker_config;	/* copied at start; read by every worker thread */
static int la_Worker_count = 0;	/* workers started */

/* workers that can take tasks. Set to the number started; a worker that cannot open its session or apply
 * context takes itself out. Only when the last one is gone does the applier stop: a task goes to whichever
 * live worker takes it, so fewer workers only cost parallelism, never correctness. */
static volatile INT32 la_Live_worker_count = 0;

static void la_worker_init (LA_WORKER *worker, int index);
static void *la_worker_main (void *arg);
static int la_worker_run (LA_WORKER *worker, LA_APPLY_WORKER_CONTEXT *context,
			  unsigned long long *worker_applied_item_count);
static void la_worker_startup_failed (const LA_WORKER *worker, const char *reason, int error);
static int la_worker_context_init (LA_APPLY_WORKER_CONTEXT *context, int db_page_size, int recdes_pool_size);
static void la_worker_context_final (LA_APPLY_WORKER_CONTEXT *context);
static int la_init_recdes_pool (LA_APPLY_WORKER_CONTEXT *context, int page_size, int num_recdes);
static void la_clear_recdes_pool (LA_APPLY_WORKER_CONTEXT *context);

/*
 * la_worker_start() - reset the worker slots and start one thread per slot
 *   return: NO_ERROR, or ER_FAILED when a thread cannot be created
 *   workers(out): worker slots to start
 *   count(in): number of slots
 *   config(in): what every worker needs: buffer sizes, trace level and the applier's callbacks
 *
 * Note:
 *   The caller does not wait for the workers to open their sessions: tasks queue up until a worker
 *   takes them, and a worker that cannot start steps out on its own (la_worker_startup_failed).
 *   On failure the threads already started keep running; they are marked started, so the caller's
 *   shutdown path still joins them through la_worker_join.
 */
int
la_worker_start (LA_WORKER *workers, int count, const LA_WORKER_CONFIG *config)
{
  int i;

  assert (workers != NULL && config != NULL);
  assert (config->run_task != NULL && config->request_shutdown != NULL);

  la_Worker_config = *config;
  la_Worker_count = count;
  la_Live_worker_count = count;

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

  error = db_restart_sub (sub_index);
  if (error != NO_ERROR)
    {
      pthread_mutex_unlock (&la_Worker_session_mutex);
      return error;
    }

  session->client_context_started = true;

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
  cuberr::context *er_context_p = NULL;
  bool session_started = false;
  bool worker_context_started = false;
  int error = NO_ERROR;
  unsigned long long worker_applied_item_count = 0;
  const char *exit_reason = "normal_shutdown";

  er_context_p = new cuberr::context ();
  er_context_p->register_thread_local ();

  error = la_worker_start_session (&session, worker->index);
  if (error != NO_ERROR)
    {
      exit_reason = "start_session_failed";
      la_worker_startup_failed (worker, exit_reason, error);
      goto end;
    }

  session_started = true;
  error = la_worker_context_init (&worker_context, la_Worker_config.db_page_size, la_Worker_config.recdes_pool_size);
  if (error != NO_ERROR)
    {
      exit_reason = "worker_context_init_failed";
      la_worker_startup_failed (worker, exit_reason, error);
      goto end;
    }

  worker_context.worker_idx = worker->index;
  worker_context_started = true;

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
      la_worker_context_final (&worker_context);
    }

  if (session_started)
    {
      la_worker_end_session (&session);
    }

  /* The locator area cache is one per process and shared by every worker; it is released once by the
   * client finalization after all workers have been joined. */

  er_context_p->deregister_thread_local ();
  delete er_context_p;

  return NULL;
}

/*
 * la_worker_run() - take tasks from the task queue, apply each and hand its result back until shutdown
 *   return: NO_ERROR when a queue shuts down, or the flush or server-down error that stopped the worker
 *   worker(in/out): the calling worker; busy is set while a task is in hand
 *   context(in/out): the worker's apply context
 *   worker_applied_item_count(in/out): items this worker has applied so far
 *
 * Note:
 *   Returns when either queue shuts down. A result that cannot be queued then is dropped; it was
 *   never retired, so the restart point stays behind it and a restart applies it again.
 *
 *       A flush or server-down error stops the worker after its result is queued, as it stops the
 *       serial applier. The worker takes no further task, so the failed transaction is never
 *       committed together with a later one and is rolled back when the connection closes.
 */
static int
la_worker_run (LA_WORKER *worker, LA_APPLY_WORKER_CONTEXT *context, unsigned long long *worker_applied_item_count)
{
  int trace_level = la_Worker_config.trace_level;
  int error;

  while (true)
    {
      LA_TASK task;
      LA_RESULT result;
      struct timeval task_begin;
      bool trace_task;

      if (la_task_queue_pop (&task) != NO_ERROR)
	{
	  break;
	}

      worker->busy = true;

      /* A task without replicated items (an empty commit) is not traced: it applies nothing and would only
       * add lines. */
      trace_task = (trace_level >= 1 && task.has_items);

      if (trace_task)
	{
	  LA_TIME_BEGIN (task_begin);
	  _er_log_debug (ARG_FILE_LINE,
			 "[WSET] worker apply begin trid=%d commit_lsa=%lld|%d type=%d worker=%zu dispatch=%llu\n",
			 task.tranid, (long long) task.commit_lsa.pageid, (int) task.commit_lsa.offset, task.rectype,
			 (size_t) context->worker_idx, (unsigned long long) task.seq);
	}

      la_result_init (&result, &task, context->worker_idx);

      la_Worker_config.run_task (context, &task, trace_level, worker_applied_item_count, &result);

      if (trace_task)
	{
	  UINT64 elapsed_usec = 0;
	  /* the table is named only when the transaction touched exactly one; otherwise trid identifies it */
	  bool one_class = (!result.stats.multi_class && result.stats.last_class_name[0] != '\0');

	  LA_TIME_ACCUM_USEC (task_begin, elapsed_usec);
	  _er_log_debug (ARG_FILE_LINE,
			 "[WSET] worker apply done trid=%d commit_lsa=%lld|%d type=%d worker=%zu dispatch=%llu "
			 "error=%d elapsed_usec=%llu%s%s\n",
			 task.tranid, (long long) task.commit_lsa.pageid, (int) task.commit_lsa.offset,
			 task.rectype, (size_t) context->worker_idx, (unsigned long long) task.seq, result.error,
			 (unsigned long long) elapsed_usec, one_class ? " class=" : "",
			 one_class ? result.stats.last_class_name : "");
	}

      error = la_result_queue_push (&result);

      worker->busy = false;

      if (error != NO_ERROR)
	{
	  /* the result queue shuts down only when the coordinator stops the workers */
	  return NO_ERROR;
	}

      if (LA_IS_FLUSH_ERROR (result.error) || result.error == ER_NET_CANT_CONNECT_SERVER
	  || ER_IS_SERVER_DOWN_ERROR (result.error))
	{
	  return result.error;
	}

    }

  return NO_ERROR;
}

/*
 * la_worker_startup_failed() - take a worker that could not start out of the pool
 *   worker(in): the worker leaving
 *   reason(in): which startup step failed
 *   error(in): its error
 *
 * Note:
 *   The remaining workers keep applying; the failure is only logged. When no worker is left nothing
 *   would ever take a task, so the applier is told to stop.
 */
static void
la_worker_startup_failed (const LA_WORKER *worker, const char *reason, int error)
{
  char buffer[256];
  INT32 remaining = ATOMIC_INC_32 (&la_Live_worker_count, -1);

  snprintf (buffer, sizeof (buffer), "apply worker %d could not start (%s, error %d); %d of %d workers remain",
	    worker->index, reason, error, (int) remaining, la_Worker_count);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, buffer);

  if (remaining <= 0)
    {
      la_Worker_config.request_shutdown ();
    }
}

/*
 * la_worker_context_init() - allocate the calling worker's unzip buffers and record pool
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   context(out): context to set up
 *   db_page_size(in): database page size; sizes the buffers and each pooled record
 *   recdes_pool_size(in): records in the pool
 */
static int
la_worker_context_init (LA_APPLY_WORKER_CONTEXT *context, int db_page_size, int recdes_pool_size)
{
  int error = NO_ERROR;

  memset (context, 0, sizeof (*context));
  context->worker_idx = -1;

  context->undo_unzip_ptr = log_zip_alloc (db_page_size);
  context->redo_unzip_ptr = log_zip_alloc (db_page_size);
  if (context->undo_unzip_ptr == NULL || context->redo_unzip_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, db_page_size);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_return;
    }

  error = la_init_recdes_pool (context, db_page_size, recdes_pool_size);
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  return NO_ERROR;

error_return:
  la_worker_context_final (context);

  return error;
}

/*
 * la_worker_context_final() - free what la_worker_context_init allocated; safe on a partly built context
 *   context(in/out): context to tear down
 */
static void
la_worker_context_final (LA_APPLY_WORKER_CONTEXT *context)
{
  la_clear_recdes_pool (context);

  if (context->undo_unzip_ptr != NULL)
    {
      log_zip_free (context->undo_unzip_ptr);
      context->undo_unzip_ptr = NULL;
    }

  if (context->redo_unzip_ptr != NULL)
    {
      log_zip_free (context->redo_unzip_ptr);
      context->redo_unzip_ptr = NULL;
    }
}

/*
 * la_clear_recdes_pool() - free allocated memory in recdes pool
 * 			    and clear recdes pool info
 *   return: error code
 */
static void
la_clear_recdes_pool (LA_APPLY_WORKER_CONTEXT *context)
{
  LA_RECDES_POOL *pool = &context->recdes_pool;
  int i;
  RECDES *recdes;

  if (pool->is_initialized == false)
    {
      return;
    }

  if (pool->recdes_arr != NULL)
    {
      for (i = 0; i < pool->num_recdes; i++)
	{
	  recdes = &pool->recdes_arr[i];
	  if (recdes->area_size > pool->db_page_size)
	    {
	      free_and_init (recdes->data);
	    }
	}
      free_and_init (pool->recdes_arr);
    }

  if (pool->area != NULL)
    {
      free_and_init (pool->area);
    }

  pool->db_page_size = 0;
  pool->next_idx = 0;
  pool->num_recdes = 0;
  pool->is_initialized = false;

  return;
}

/*
 * la_realloc_recdes_data() - realloc area of given recdes
 *   return: error code
 *
 */
int
la_realloc_recdes_data (LA_APPLY_WORKER_CONTEXT *context, RECDES *recdes, int data_size)
{
  LA_RECDES_POOL *pool = &context->recdes_pool;

  if (pool->is_initialized == false)
    {
      return ER_FAILED;
    }

  if (recdes->area_size < data_size)
    {
      if (recdes->area_size > pool->db_page_size)
	{
	  /* recdes->data was realloced by previous operation */
	  free_and_init (recdes->data);
	}

      recdes->data = (char *) malloc (data_size);
      if (recdes->data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, data_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      recdes->area_size = data_size;
    }
  recdes->length = 0;

  return NO_ERROR;
}

/*
 * la_assign_recdes_from_pool() - get a recdes from pool
 *   return: a recdes having area with size of db page size
 *
 * Note: if a recdes that is about to be assigned has an area
 * greater than db page size, then it first frees the area.
 */
RECDES *
la_assign_recdes_from_pool (LA_APPLY_WORKER_CONTEXT *context)
{
  LA_RECDES_POOL *pool = &context->recdes_pool;
  RECDES *recdes;

  if (pool->is_initialized == false)
    {
      return NULL;
    }

  recdes = &pool->recdes_arr[pool->next_idx];
  assert (recdes != NULL && recdes->data != NULL);

  if (recdes->area_size > pool->db_page_size)
    {
      /* recdes->data was realloced by previous operation */
      free_and_init (recdes->data);

      recdes->data = pool->area + pool->db_page_size * pool->next_idx;
      recdes->area_size = pool->db_page_size;
    }

  recdes->length = 0;
  pool->next_idx++;
  pool->next_idx %= pool->num_recdes;

  return recdes;
}

/*
 * la_init_recdes_pool() - initialize recdes pool
 *   return:
 *
 * Note:
 */
static int
la_init_recdes_pool (LA_APPLY_WORKER_CONTEXT *context, int page_size, int num_recdes)
{
  LA_RECDES_POOL *pool = &context->recdes_pool;
  int i;
  char *p;
  RECDES *recdes;

  assert (page_size >= IO_MIN_PAGE_SIZE && page_size <= IO_MAX_PAGE_SIZE);

  if (pool->is_initialized == false)
    {
      pool->area = (char *) malloc (page_size * num_recdes);
      if (pool->area == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, page_size * num_recdes);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      pool->recdes_arr = (RECDES *) malloc (sizeof (RECDES) * num_recdes);
      if (pool->recdes_arr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (RECDES) * num_recdes);
	  /* is_initialized is still false here, so la_clear_recdes_pool would skip this area */
	  free_and_init (pool->area);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      p = pool->area;
      for (i = 0; i < num_recdes; i++)
	{
	  recdes = &pool->recdes_arr[i];

	  recdes->data = p;
	  recdes->area_size = page_size;
	  recdes->length = 0;

	  p += page_size;
	}

      pool->db_page_size = page_size;
      pool->num_recdes = num_recdes;
      pool->is_initialized = true;
    }
  else if (pool->db_page_size != page_size || pool->num_recdes != num_recdes)
    {
      la_clear_recdes_pool (context);
      return la_init_recdes_pool (context, page_size, num_recdes);
    }

  pool->next_idx = 0;

  return NO_ERROR;
}

