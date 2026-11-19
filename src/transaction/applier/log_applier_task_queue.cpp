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
 * log_applier_task_queue.cpp - implementation of the coordinator-to-worker task queues
 *
 * Every worker has a queue of its own. The coordinator chooses the worker of each task when it dispatches it: a task
 * whose predecessors are all applied goes to the least busy worker, and a task whose unfinished predecessors are all
 * in one worker's queue or open slave transaction goes to that worker, which applies it after them in the same
 * transaction (see la_coordinator_route). With one worker every task goes to worker 0 in commit order.
 * One mutex guards every queue; each worker waits on its own condition variable. No thread holds this mutex and
 * the result queue's at the same time, so the two cannot deadlock.
 */

#include "config.h"

#include "log_applier_task_queue.hpp"
#include "log_applier_clock.hpp"
#include "error_manager.h"
#include "memory_alloc.h"	/* free_and_init */
#include "porting.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct la_task_queue LA_TASK_QUEUE;
struct la_task_queue
{
  LA_TASK tasks[LA_TASK_QUEUE_CAPACITY];
  int head;
  int tail;
  int count;
  pthread_cond_t not_empty;
};

static LA_TASK_QUEUE *la_Task_queues = NULL;
static int la_Task_queue_count = 0;
static int la_Task_queue_total = 0;	/* tasks in all the queues; kept with the queues under la_Task_queue_mutex */
static int la_Task_queue_max_total = 0;
static pthread_mutex_t la_Task_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool la_Task_queue_shutdown = false;

/*
 * la_task_queue_init() - allocate and empty one queue per worker before the workers start
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   worker_count(in): number of workers
 *
 * Note: called by the coordinator before any worker thread exists, so no lock is taken.
 */
int
la_task_queue_init (int worker_count)
{
  assert (worker_count > 0);

  la_task_queue_final ();

  la_Task_queues = (LA_TASK_QUEUE *) malloc (sizeof (LA_TASK_QUEUE) * worker_count);
  if (la_Task_queues == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LA_TASK_QUEUE) * worker_count);
      la_Task_queue_count = 0;
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (int i = 0; i < worker_count; i++)
    {
      la_Task_queues[i].head = 0;
      la_Task_queues[i].tail = 0;
      la_Task_queues[i].count = 0;
      la_cond_init_monotonic (&la_Task_queues[i].not_empty);
    }
  la_Task_queue_count = worker_count;
  la_Task_queue_total = 0;
  la_Task_queue_max_total = 0;
  la_Task_queue_shutdown = false;

  return NO_ERROR;
}

/*
 * la_task_queue_final() - release the queues
 *
 * Note: called with no worker thread left, so no lock is taken. Tasks still queued are dropped; they were
 *       never retired, so a restart reads them again.
 */
void
la_task_queue_final (void)
{
  if (la_Task_queues == NULL)
    {
      return;
    }

  for (int i = 0; i < la_Task_queue_count; i++)
    {
      pthread_cond_destroy (&la_Task_queues[i].not_empty);
    }
  free_and_init (la_Task_queues);
  la_Task_queue_count = 0;
}

/*
 * la_task_queue_push() - enqueue a task for one worker
 *   return: NO_ERROR, or ER_FAILED when the queues are shutting down or that queue is full
 *   task(in): task to dispatch; copied into the queue
 *   worker(in): index of the worker that applies it
 *
 * Note: the push never waits. Only the coordinator pushes, so a room it saw (la_task_queue_is_full) cannot be
 *       taken by anyone else before it pushes.
 */
int
la_task_queue_push (const LA_TASK *task, int worker)
{
  LA_TASK_QUEUE *queue;

  assert (task != NULL);
  assert (worker >= 0 && worker < la_Task_queue_count);

  pthread_mutex_lock (&la_Task_queue_mutex);

  if (la_Task_queue_shutdown)
    {
      pthread_mutex_unlock (&la_Task_queue_mutex);
      return ER_FAILED;
    }

  queue = &la_Task_queues[worker];
  if (queue->count >= LA_TASK_QUEUE_CAPACITY)
    {
      pthread_mutex_unlock (&la_Task_queue_mutex);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, "task queue full");
      assert (false);
      return ER_FAILED;
    }

  queue->tasks[queue->tail] = *task;
  queue->tail = (queue->tail + 1) % LA_TASK_QUEUE_CAPACITY;
  queue->count++;
  la_Task_queue_total++;
  la_Task_queue_max_total = MAX (la_Task_queue_max_total, la_Task_queue_total);

  pthread_cond_signal (&queue->not_empty);
  pthread_mutex_unlock (&la_Task_queue_mutex);

  return NO_ERROR;
}

/*
 * la_task_queue_is_full() - check whether one more task fits in a worker's queue
 *
 * Note: takes the queue mutex the workers pop under; the coordinator asks only when the worker has at least
 *       LA_TASK_QUEUE_CAPACITY unanswered tasks (la_coordinator_queue_has_room), since fewer cannot fill it.
 */
bool
la_task_queue_is_full (int worker)
{
  bool full;

  assert (worker >= 0 && worker < la_Task_queue_count);

  pthread_mutex_lock (&la_Task_queue_mutex);
  full = (la_Task_queues[worker].count >= LA_TASK_QUEUE_CAPACITY);
  pthread_mutex_unlock (&la_Task_queue_mutex);

  return full;
}

/*
 * la_task_queue_pop() - take the oldest task of a worker's queue, waiting until one arrives
 *   return: NO_ERROR when a task was taken, ER_FAILED when the queues are shutting down
 *   task(out): the dequeued task
 *   worker(in): index of the calling worker
 *
 * Note: the worker calls this only with no uncommitted task.
 *       On shutdown, queued tasks are abandoned. They were never retired, so committed_lsa stays behind them
 *       and a restart reads them again.
 */
int
la_task_queue_pop (LA_TASK *task, int worker)
{
  LA_TASK_QUEUE *queue;

  assert (task != NULL);
  assert (worker >= 0 && worker < la_Task_queue_count);

  pthread_mutex_lock (&la_Task_queue_mutex);

  queue = &la_Task_queues[worker];
  while (queue->count == 0 && la_Task_queue_shutdown == false)
    {
      pthread_cond_wait (&queue->not_empty, &la_Task_queue_mutex);
    }

  if (la_Task_queue_shutdown)
    {
      pthread_mutex_unlock (&la_Task_queue_mutex);
      return ER_FAILED;
    }

  *task = queue->tasks[queue->head];
  queue->head = (queue->head + 1) % LA_TASK_QUEUE_CAPACITY;
  queue->count--;
  la_Task_queue_total--;

  pthread_mutex_unlock (&la_Task_queue_mutex);

  return NO_ERROR;
}

/*
 * la_task_queue_try_pop() - take the oldest task of a worker's queue if one is waiting, without waiting
 *   return: NO_ERROR, or ER_FAILED when the queues are shutting down
 *   task(out): the dequeued task, valid only when taken is set
 *   taken(out): a task was dequeued
 *   worker(in): index of the calling worker
 *   hold_nsec(in): how long to wait for a task on an empty queue before reporting it empty, 0 for none
 *
 * Note:
 *   A worker with uncommitted tasks calls this instead of la_task_queue_pop: when no task arrives within
 *   hold_nsec, it commits them rather than take more.
 */
int
la_task_queue_try_pop (LA_TASK *task, bool *taken, int worker, UINT64 hold_nsec)
{
  LA_TASK_QUEUE *queue;

  assert (task != NULL && taken != NULL);
  assert (worker >= 0 && worker < la_Task_queue_count);

  *taken = false;

  pthread_mutex_lock (&la_Task_queue_mutex);

  if (la_Task_queue_shutdown)
    {
      pthread_mutex_unlock (&la_Task_queue_mutex);
      return ER_FAILED;
    }

  queue = &la_Task_queues[worker];
  if (hold_nsec > 0 && queue->count == 0)
    {
      /* keep the batch open a little longer in case the next task is about to arrive */
      struct timespec abstime;

      la_monotonic_deadline (hold_nsec, &abstime);
      while (queue->count == 0 && la_Task_queue_shutdown == false)
	{
	  if (pthread_cond_timedwait (&queue->not_empty, &la_Task_queue_mutex, &abstime) != 0)
	    {
	      break;
	    }
	}
      if (la_Task_queue_shutdown)
	{
	  pthread_mutex_unlock (&la_Task_queue_mutex);
	  return ER_FAILED;
	}
    }

  if (queue->count > 0)
    {
      *task = queue->tasks[queue->head];
      queue->head = (queue->head + 1) % LA_TASK_QUEUE_CAPACITY;
      queue->count--;
      la_Task_queue_total--;
      *taken = true;
    }

  pthread_mutex_unlock (&la_Task_queue_mutex);

  return NO_ERROR;
}

/*
 * la_task_queue_signal_shutdown() - mark the queues as shutting down and wake every waiting worker
 *
 * Note:
 *   A waiting thread cannot see the flag until it is woken. Without the broadcast an idle
 *   worker would sleep forever and la_stop_apply_workers would hang in pthread_join.
 */
void
la_task_queue_signal_shutdown (void)
{
  pthread_mutex_lock (&la_Task_queue_mutex);

  la_Task_queue_shutdown = true;
  for (int i = 0; i < la_Task_queue_count; i++)
    {
      pthread_cond_broadcast (&la_Task_queues[i].not_empty);
    }

  pthread_mutex_unlock (&la_Task_queue_mutex);
}

/*
 * la_task_queue_get_count() - return the number of queued tasks over all workers
 *
 * Note: read without the lock, so the value may be stale; it is used only for status output.
 */
int
la_task_queue_get_count (void)
{
  return la_Task_queues != NULL ? la_Task_queue_total : 0;
}

int
la_task_queue_get_max_count (void)
{
  return la_Task_queue_max_total;
}
