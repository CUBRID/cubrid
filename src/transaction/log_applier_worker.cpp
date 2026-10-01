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

#include "log_applier_worker.hpp"

#include "error_code.h"
#include "scope_exit.hpp"

#include <cassert>
#include <new>
#include <pthread.h>

struct la_worker
{
  pthread_t thread;
  la_workers *owner;
  la_task_queue *task_queue;     /* coordinator-owned */
  la_result_queue *result_queue; /* coordinator-owned */
  std::size_t index;
  bool started;
};

struct la_workers
{
  la_workers (std::size_t count, const la_worker_operations &worker_operations, void *context)
    : entries (NULL), worker_count (count), started_count (0), ready_count (0), startup_error (NO_ERROR),
      start_attempted (false), operations (worker_operations), shared_context (context)
  {
  }

  la_worker *entries;
  std::size_t worker_count;
  std::size_t started_count;
  std::size_t ready_count;
  int startup_error;
  bool start_attempted;
  la_worker_operations operations;
  void *shared_context;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
};

/*
 * la_worker_report_ready - report one worker's initialization result
 *
 * return: none
 *
 * workers(in/out): workers
 * error(in): worker initialization result
 */
static void
la_worker_report_ready (la_workers *workers, int error)
{
  pthread_mutex_lock (&workers->mutex);
  if (error != NO_ERROR && workers->startup_error == NO_ERROR)
    {
      workers->startup_error = error;
    }

  workers->ready_count++;
  pthread_cond_broadcast (&workers->condition);
  pthread_mutex_unlock (&workers->mutex);
}

/*
 * la_worker_wait_ready - wait until every worker reports initialization
 *
 * return: NO_ERROR or the first worker initialization error
 *
 * workers(in/out): workers
 */
static int
la_worker_wait_ready (la_workers *workers)
{
  pthread_mutex_lock (&workers->mutex);
  while (workers->ready_count < workers->worker_count)
    {
      pthread_cond_wait (&workers->condition, &workers->mutex);
    }

  int error = workers->startup_error;
  pthread_mutex_unlock (&workers->mutex);

  return error;
}

/*
 * la_worker_join_threads - join every worker thread that was started
 *
 * return: none
 *
 * workers(in/out): workers
 */
static void
la_worker_join_threads (la_workers *workers)
{
  while (workers->started_count > 0)
    {
      std::size_t worker_index = workers->started_count - 1;
      la_worker &worker = workers->entries[worker_index];
      if (worker.started)
	{
	  pthread_join (worker.thread, NULL);
	  worker.started = false;
	}
      workers->started_count--;
    }
}

/*
 * la_worker_run - initialize worker state and process tasks until shutdown
 *
 * return: NULL
 *
 * argument(in): worker descriptor
 */
static void *
la_worker_run (void *argument)
{
  la_worker *worker = static_cast<la_worker *> (argument);
  la_workers *workers = worker->owner;
  void *worker_context = NULL;
  bool initialized = false;
  int error = NO_ERROR;

  if (workers->operations.initialize != NULL)
    {
      error = workers->operations.initialize (workers->shared_context, worker->index, &worker_context);
      initialized = error == NO_ERROR;
    }

  la_worker_report_ready (workers, error);
  if (error != NO_ERROR)
    {
      return NULL;
    }

  la_apply_task task;
  while (la_task_queue_dequeue (worker->task_queue, task))
    {
      la_apply_result result = {};
      result.dispatch_sequence = task.dispatch_sequence;
      result.worker_index = worker->index;
      result.tranid = task.tranid;
      result.record_type = task.record_type;
      result.commit_lsa = task.commit_lsa;
      result.log_record_time = task.log_record_time;
      LSA_SET_NULL (&result.committed_rep_lsa);
      result.error = workers->operations.execute (workers->shared_context, worker_context, task, result);
      la_result_queue_enqueue (worker->result_queue, result);
    }

  if (initialized && workers->operations.finalize != NULL)
    {
      workers->operations.finalize (workers->shared_context, worker_context);
    }

  return NULL;
}

/*
 * la_worker_create - create workers that use coordinator-owned queues
 *
 * return: NO_ERROR or an error code
 *
 * worker_count(in): number of worker threads
 * task_queue(in): shared task queue owned by the coordinator
 * result_queue(in): shared result queue owned by the coordinator
 * worker_operations(in): fixed apply callbacks provided by log_applier.c
 * shared_context(in): context shared by all worker operations
 * workers_out(out): created workers
 */
int
la_worker_create (std::size_t worker_count, la_task_queue *task_queue, la_result_queue *result_queue,
		  const la_worker_operations &worker_operations, void *shared_context, la_workers **workers_out)
{
  assert (task_queue != NULL);
  assert (result_queue != NULL);
  assert (worker_operations.execute != NULL);
  assert (workers_out != NULL);
  *workers_out = NULL;

  if (worker_count == 0)
    {
      return ER_FAILED;
    }

  la_workers *workers = new (std::nothrow) la_workers (worker_count, worker_operations, shared_context);
  if (workers == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  bool mutex_initialized = false;
  bool condition_initialized = false;
  scope_exit cleanup
  {
    [&]
    {
      if (condition_initialized)
	{
	  pthread_cond_destroy (&workers->condition);
	}
      if (mutex_initialized)
	{
	  pthread_mutex_destroy (&workers->mutex);
	}
      delete[] workers->entries;
      delete workers;
    }
  };

  workers->entries = new (std::nothrow) la_worker[worker_count] ();
  if (workers->entries == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (pthread_mutex_init (&workers->mutex, NULL) != 0)
    {
      return ER_FAILED;
    }
  mutex_initialized = true;

  if (pthread_cond_init (&workers->condition, NULL) != 0)
    {
      return ER_FAILED;
    }
  condition_initialized = true;

  for (std::size_t i = 0; i < worker_count; i++)
    {
      workers->entries[i].owner = workers;
      workers->entries[i].task_queue = task_queue;
      workers->entries[i].result_queue = result_queue;
      workers->entries[i].index = i;
      workers->entries[i].started = false;
    }

  *workers_out = workers;
  cleanup.release ();

  return NO_ERROR;
}

/*
 * la_worker_start - start all workers and wait for initialization
 *
 * return: NO_ERROR or an error code
 *
 * workers(in/out): workers
 */
int
la_worker_start (la_workers *workers)
{
  assert (workers != NULL);

  if (workers->start_attempted)
    {
      return ER_FAILED;
    }
  workers->start_attempted = true;

  for (std::size_t i = 0; i < workers->worker_count; i++)
    {
      la_worker &worker = workers->entries[i];
      if (pthread_create (&worker.thread, NULL, la_worker_run, &worker) != 0)
	{
	  la_task_queue_shutdown (workers->entries[0].task_queue);
	  la_worker_join_threads (workers);
	  return ER_FAILED;
	}
      worker.started = true;
      workers->started_count++;
    }

  int startup_error = la_worker_wait_ready (workers);
  if (startup_error != NO_ERROR)
    {
      la_task_queue_shutdown (workers->entries[0].task_queue);
      la_worker_join_threads (workers);
      return startup_error;
    }

  return NO_ERROR;
}

/*
 * la_worker_stop - join workers after the owner shuts down the task queue
 *
 * return: none
 *
 * workers(in/out): workers, or NULL
 */
void
la_worker_stop (la_workers *workers)
{
  if (workers == NULL || workers->started_count == 0)
    {
      return;
    }

  la_worker_join_threads (workers);
}

/*
 * la_worker_destroy - release stopped worker-owned state
 *
 * return: none
 *
 * workers(in): workers, or NULL
 */
void
la_worker_destroy (la_workers *workers)
{
  if (workers == NULL)
    {
      return;
    }

  assert (workers->started_count == 0);
  pthread_cond_destroy (&workers->condition);
  pthread_mutex_destroy (&workers->mutex);
  delete[] workers->entries;
  delete workers;
}
