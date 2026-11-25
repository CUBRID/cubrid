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
#include <atomic>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct la_task_queue LA_TASK_QUEUE;
struct la_task_queue
{
  LA_TASK tasks[LA_TASK_QUEUE_CAPACITY];
  int head;
  int tail;
  int count;
  std::atomic<int> queued;	/* count, published for la_task_queue_get_queued to read without the lock */
  pthread_cond_t not_empty;
  pthread_cond_t not_full;	/* the coordinator waits on it for room (la_task_queue_wait_room) */
};

static LA_TASK_QUEUE *la_Task_queues = NULL;
static int la_Task_queue_count = 0;
static int la_Task_queue_total = 0;	/* tasks in all the queues; kept with the queues under la_Task_queue_mutex */
static int la_Task_queue_max_total = 0;
static pthread_mutex_t la_Task_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool la_Task_queue_shutdown = false;
/* bumped by la_task_queue_wake_holding; a worker keeping its batch open on an empty queue stops waiting when it
 * changes */
static UINT64 la_Task_queue_wake_seq = 0;

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
      la_Task_queues[i].queued.store (0);
      la_cond_init_monotonic (&la_Task_queues[i].not_empty);
      la_cond_init_monotonic (&la_Task_queues[i].not_full);
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
      pthread_cond_destroy (&la_Task_queues[i].not_full);
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
  queue->queued.store (queue->count, std::memory_order_relaxed);
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
 * la_task_queue_get_queued() - number of tasks waiting in a worker's queue, read without the lock
 *   return: the count as of the last push or take; never below the real count when read by the only thread that
 *           pushes (the coordinator), since the workers can only lower it
 *   worker(in): the worker
 *
 * Note: lets the coordinator check for room on every dispatch without taking the lock the workers take tasks under.
 */
int
la_task_queue_get_queued (int worker)
{
  assert (worker >= 0 && worker < la_Task_queue_count);

  return la_Task_queues[worker].queued.load (std::memory_order_relaxed);
}

/*
 * la_task_queue_wait_room() - wait a while for a worker's queue to have room for one more task
 *   return: NO_ERROR, or ER_FAILED when the queues are shutting down
 *   worker(in): the worker
 *   timeout_msec(in): longest wait
 *   has_room(out): true when the queue has room
 *
 * Note: with one worker the coordinator dispatches every task to it directly and waits here when its queue is
 *       full; the worker frees a slot each time it takes a task, whether or not it has committed the ones before.
 *       The wait is bounded because a worker that stops on an error never frees a slot again: the caller checks
 *       for shutdown and collects results between waits.
 */
int
la_task_queue_wait_room (int worker, int timeout_msec, bool *has_room)
{
  LA_TASK_QUEUE *queue;
  struct timespec abstime;

  assert (worker >= 0 && worker < la_Task_queue_count);
  assert (has_room != NULL && timeout_msec > 0);

  *has_room = false;

  pthread_mutex_lock (&la_Task_queue_mutex);

  queue = &la_Task_queues[worker];
  if (queue->count >= LA_TASK_QUEUE_CAPACITY && la_Task_queue_shutdown == false)
    {
      la_monotonic_deadline ((UINT64) timeout_msec * 1000000ULL, &abstime);
      while (queue->count >= LA_TASK_QUEUE_CAPACITY && la_Task_queue_shutdown == false)
	{
	  if (pthread_cond_timedwait (&queue->not_full, &la_Task_queue_mutex, &abstime) != 0)
	    {
	      break;
	    }
	}
    }

  if (la_Task_queue_shutdown)
    {
      pthread_mutex_unlock (&la_Task_queue_mutex);
      return ER_FAILED;
    }

  *has_room = (queue->count < LA_TASK_QUEUE_CAPACITY);

  pthread_mutex_unlock (&la_Task_queue_mutex);

  return NO_ERROR;
}

/*
 * la_task_queue_wake_holding() - wake the workers that keep an open transaction while their queue is empty
 *
 * Note: called by the coordinator when it starts waiting for results, reaches the end of the log or parks a task;
 *       a worker woken this way commits its open transaction instead of waiting out the commit interval.
 */
void
la_task_queue_wake_holding (void)
{
  pthread_mutex_lock (&la_Task_queue_mutex);

  la_Task_queue_wake_seq++;
  for (int i = 0; i < la_Task_queue_count; i++)
    {
      pthread_cond_broadcast (&la_Task_queues[i].not_empty);
    }

  pthread_mutex_unlock (&la_Task_queue_mutex);
}

/*
 * la_task_queue_get_wake_seq() - read the wakeup count la_task_queue_pop_many compares against
 *   return: the number of la_task_queue_wake_holding calls so far
 */
UINT64
la_task_queue_get_wake_seq (void)
{
  UINT64 seq;

  pthread_mutex_lock (&la_Task_queue_mutex);
  seq = la_Task_queue_wake_seq;
  pthread_mutex_unlock (&la_Task_queue_mutex);

  return seq;
}

/*
 * la_task_queue_pop_many() - take up to max of the oldest tasks of a worker's queue at once
 *   return: NO_ERROR, or ER_FAILED when the queues are shutting down
 *   tasks(out): the dequeued tasks, oldest first
 *   max(in): room in tasks
 *   count(out): number of tasks taken; 0 only when wait is false and none arrived within hold_nsec, or
 *               la_task_queue_wake_holding woke the worker first
 *   worker(in): index of the calling worker
 *   wait(in): wait until a task arrives (a worker with no uncommitted task)
 *   hold_nsec(in): without wait, how long to wait for a task on an empty queue, 0 for none
 *   wake_seq(in): la_task_queue_get_wake_seq read before hold_nsec was decided; the hold ends at once if it has
 *                 changed since, so a wakeup sent in between is not missed
 *
 * Note: a worker takes every waiting task under one lock instead of one lock per task, as the coordinator pushes
 *       them one by one under the same lock. Tasks taken but not yet applied when the queues shut down are dropped
 *       with the queued ones; they were never retired, so a restart reads them again.
 */
int
la_task_queue_pop_many (LA_TASK *tasks, int max, int *count, int worker, bool wait, UINT64 hold_nsec, UINT64 wake_seq)
{
  LA_TASK_QUEUE *queue;
  int n;

  assert (tasks != NULL && count != NULL && max > 0);
  assert (worker >= 0 && worker < la_Task_queue_count);

  *count = 0;

  pthread_mutex_lock (&la_Task_queue_mutex);

  queue = &la_Task_queues[worker];
  if (wait)
    {
      while (queue->count == 0 && la_Task_queue_shutdown == false)
	{
	  pthread_cond_wait (&queue->not_empty, &la_Task_queue_mutex);
	}
    }
  else if (hold_nsec > 0 && queue->count == 0 && la_Task_queue_shutdown == false)
    {
      /* keep the batch open a little longer in case the next task is about to arrive */
      struct timespec abstime;

      la_monotonic_deadline (hold_nsec, &abstime);
      while (queue->count == 0 && la_Task_queue_shutdown == false && la_Task_queue_wake_seq == wake_seq)
	{
	  if (pthread_cond_timedwait (&queue->not_empty, &la_Task_queue_mutex, &abstime) != 0)
	    {
	      break;
	    }
	}
    }

  if (la_Task_queue_shutdown)
    {
      pthread_mutex_unlock (&la_Task_queue_mutex);
      return ER_FAILED;
    }

  n = MIN (queue->count, max);
  for (int i = 0; i < n; i++)
    {
      tasks[i] = queue->tasks[queue->head];
      queue->head = (queue->head + 1) % LA_TASK_QUEUE_CAPACITY;
    }
  queue->count -= n;
  queue->queued.store (queue->count, std::memory_order_relaxed);
  la_Task_queue_total -= n;
  if (n > 0)
    {
      pthread_cond_signal (&queue->not_full);
    }

  pthread_mutex_unlock (&la_Task_queue_mutex);

  *count = n;

  return NO_ERROR;
}

/*
 * la_task_queue_signal_shutdown() - mark the queues as shutting down and wake every waiting worker
 *
 * Note:
 *   A waiting thread cannot see the flag until it is woken. Without the broadcast an idle
 *   worker, or the coordinator waiting for room, would sleep forever and la_stop_apply_workers would hang in
 *   pthread_join.
 */
void
la_task_queue_signal_shutdown (void)
{
  pthread_mutex_lock (&la_Task_queue_mutex);

  la_Task_queue_shutdown = true;
  for (int i = 0; i < la_Task_queue_count; i++)
    {
      pthread_cond_broadcast (&la_Task_queues[i].not_empty);
      pthread_cond_broadcast (&la_Task_queues[i].not_full);
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
