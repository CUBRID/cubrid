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
 * log_applier_task_queue.cpp - shared task queue feeding the parallel applier workers
 *
 * The coordinator pushes only tasks the dependency gate has let pass, and any idle worker takes the next
 * one. The gate has already made sure that every predecessor is applied, so it does not matter which
 * worker takes a task. The queue has its own mutex and condition variables; no thread holds this mutex
 * and the result queue's at the same time, so the two queues cannot deadlock.
 */

#include "config.h"

#include "log_applier_task_queue.hpp"
#include "log_applier_worker.hpp"	/* LA_TASK_QUEUE_CAPACITY */
#include "error_manager.h"
#include "porting.h"

#include <assert.h>
#include <pthread.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct la_task_queue LA_TASK_QUEUE;
struct la_task_queue
{
  LA_TASK tasks[LA_TASK_QUEUE_CAPACITY];
  int head;
  int tail;
  int count;
};

static LA_TASK_QUEUE la_Shared_task_queue;
static pthread_mutex_t la_Shared_task_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t la_Shared_task_not_empty = PTHREAD_COND_INITIALIZER;	/* workers wait for a task */
static pthread_cond_t la_Shared_task_not_full = PTHREAD_COND_INITIALIZER;	/* the coordinator waits for a free slot */
static bool la_Shared_task_shutdown = false;

/*
 * la_task_queue_init() - empty the queue before the workers start
 *
 * Note:
 *   Called by the coordinator before any worker thread exists, so no lock is taken. The queue's
 *   storage is static; this resets its indexes and clears a shutdown flag left by a previous run.
 */
void
la_task_queue_init (void)
{
  la_Shared_task_queue.head = 0;
  la_Shared_task_queue.tail = 0;
  la_Shared_task_queue.count = 0;
  la_Shared_task_shutdown = false;
}

/*
 * la_task_queue_push() - hand a transaction that passed the gate to the workers
 *   return: NO_ERROR, or ER_FAILED when the queue is shutting down or full
 *   task(in): task to dispatch; copied into the queue
 *
 * Note:
 *   The gate has already confirmed that every transaction this one depends on is applied,
 *   so whichever idle worker takes it is safe and no worker is chosen here. The push never
 *   waits: the coordinator checks la_task_queue_is_full first and parks the task when there is no
 *   room, so it keeps collecting results instead of sleeping on the queue. Only the coordinator
 *   pushes, so a room it saw cannot be taken by anyone else before it pushes.
 */
int
la_task_queue_push (const LA_TASK *task)
{
  assert (task != NULL);

  pthread_mutex_lock (&la_Shared_task_mutex);

  if (la_Shared_task_shutdown)
    {
      pthread_mutex_unlock (&la_Shared_task_mutex);
      return ER_FAILED;
    }

  if (la_Shared_task_queue.count >= LA_TASK_QUEUE_CAPACITY)
    {
      /* callers check la_task_queue_is_full first */
      pthread_mutex_unlock (&la_Shared_task_mutex);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "task queue full");
      assert (false);
      return ER_FAILED;
    }

  la_Shared_task_queue.tasks[la_Shared_task_queue.tail] = *task;
  la_Shared_task_queue.tail = (la_Shared_task_queue.tail + 1) % LA_TASK_QUEUE_CAPACITY;
  la_Shared_task_queue.count++;

  pthread_cond_signal (&la_Shared_task_not_empty);
  pthread_mutex_unlock (&la_Shared_task_mutex);

  return NO_ERROR;
}

/*
 * la_task_queue_is_full() - check whether one more task fits
 *
 * Note:
 *   Only the coordinator pushes, so a false answer stays valid until the coordinator pushes.
 */
bool
la_task_queue_is_full (void)
{
  bool full;

  pthread_mutex_lock (&la_Shared_task_mutex);
  full = (la_Shared_task_queue.count >= LA_TASK_QUEUE_CAPACITY);
  pthread_mutex_unlock (&la_Shared_task_mutex);

  return full;
}

/*
 * la_task_queue_pop() - take the oldest task, waiting until one arrives
 *   return: NO_ERROR when a task was taken, ER_FAILED when the queue is shutting down
 *   task(out): the dequeued task
 *
 * Note:
 *   On shutdown, queued tasks are abandoned. They were never retired, so
 *   committed_rep_lsa stays behind them and a restart applies them again.
 */
int
la_task_queue_pop (LA_TASK *task)
{
  assert (task != NULL);

  pthread_mutex_lock (&la_Shared_task_mutex);

  while (la_Shared_task_queue.count == 0 && la_Shared_task_shutdown == false)
    {
      pthread_cond_wait (&la_Shared_task_not_empty, &la_Shared_task_mutex);
    }

  if (la_Shared_task_shutdown)
    {
      pthread_mutex_unlock (&la_Shared_task_mutex);
      return ER_FAILED;
    }

  *task = la_Shared_task_queue.tasks[la_Shared_task_queue.head];
  la_Shared_task_queue.head = (la_Shared_task_queue.head + 1) % LA_TASK_QUEUE_CAPACITY;
  la_Shared_task_queue.count--;

  pthread_cond_signal (&la_Shared_task_not_full);
  pthread_mutex_unlock (&la_Shared_task_mutex);

  return NO_ERROR;
}

/*
 * la_task_queue_signal_shutdown() - mark the queue as shutting down and wake every waiter
 *
 * Note:
 *   A waiting thread cannot see the flag until it is woken. Without the broadcast an idle
 *   worker would sleep forever and la_stop_apply_workers would hang in pthread_join.
 */
void
la_task_queue_signal_shutdown (void)
{
  pthread_mutex_lock (&la_Shared_task_mutex);

  la_Shared_task_shutdown = true;

  pthread_cond_broadcast (&la_Shared_task_not_empty);
  pthread_cond_broadcast (&la_Shared_task_not_full);
  pthread_mutex_unlock (&la_Shared_task_mutex);
}

/*
 * la_task_queue_get_count() - return the current queue depth
 *
 * Note:
 *   Read without the lock, so the value may be stale; it is used only for status output.
 */
int
la_task_queue_get_count (void)
{
  return la_Shared_task_queue.count;
}

