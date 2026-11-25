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
 * log_applier_result_queue.cpp - implementation of the shared worker-to-coordinator result queue
 *
 * The queue has its own mutex and condition variables; no thread holds this mutex and the task queue's
 * at the same time.
 */

#include "config.h"

#include "log_applier_result_queue.hpp"
#include "log_applier_clock.hpp"
#include "log_applier_worker.hpp"
#include "log_applier_debug.hpp"
#include "error_manager.h"
#include "porting.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Sized to the commit-order capacity at start. Every queued result belongs to a dispatched, unretired
 * transaction, and there are never more of those than the commit-order capacity, so the queue never fills
 * and a worker never blocks on it. */
typedef struct la_result_queue LA_RESULT_QUEUE;
struct la_result_queue
{
  LA_RESULT *results;
  int capacity;
  int head;
  int tail;
  int count;
};

static LA_RESULT_QUEUE la_Shared_result_queue = { NULL, 0, 0, 0, 0 };
static pthread_mutex_t la_Shared_result_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t la_Shared_result_not_empty = PTHREAD_COND_INITIALIZER;	/* the coordinator waits for a result;
										 * on CLOCK_MONOTONIC from init */
static pthread_cond_t la_Shared_result_not_full = PTHREAD_COND_INITIALIZER;	/* workers wait for a free slot */
static bool la_Shared_result_shutdown = false;

/*
 * la_result_queue_init() - allocate the queue for the commit-order capacity
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   capacity(in): most results the queue must hold at once, the commit-order capacity
 *
 * Note:
 *   Called before the workers start, so no lock is needed. A previous allocation is released.
 */
int
la_result_queue_init (int capacity)
{
  assert (capacity > 0);

  la_result_queue_final ();

  /* the coordinator's timed wait for a result reads its deadline on the monotonic clock */
  (void) pthread_cond_destroy (&la_Shared_result_not_empty);
  la_cond_init_monotonic (&la_Shared_result_not_empty);

  la_Shared_result_queue.results = (LA_RESULT *) malloc ((size_t) capacity * sizeof (LA_RESULT));
  if (la_Shared_result_queue.results == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) capacity * sizeof (LA_RESULT));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  la_Shared_result_queue.capacity = capacity;
  la_Shared_result_queue.head = 0;
  la_Shared_result_queue.tail = 0;
  la_Shared_result_queue.count = 0;
  la_Shared_result_shutdown = false;

  return NO_ERROR;
}

/*
 * la_result_queue_final() - release the queue
 *
 * Note: called with no worker thread left, so no lock is taken.
 */
void
la_result_queue_final (void)
{
  if (la_Shared_result_queue.results != NULL)
    {
      free_and_init (la_Shared_result_queue.results);
    }
  la_Shared_result_queue.capacity = 0;
  la_Shared_result_queue.head = 0;
  la_Shared_result_queue.tail = 0;
  la_Shared_result_queue.count = 0;
}

/*
 * la_result_queue_push() - hand the result of a finished transaction to the coordinator
 *   return: NO_ERROR, or ER_FAILED when the queue is shutting down
 *   result(in): result to deliver; queued as a copy
 *
 * Note:
 *   The queue can hold every unretired transaction's result, so it does not fill.
 *   If it ever does, the worker waits instead of dropping the result, because
 *   retire would stop at the missing result forever.
 */
int
la_result_queue_push (const LA_RESULT *result)
{
  assert (result != NULL);

  pthread_mutex_lock (&la_Shared_result_mutex);

  /* safeguard only: the queue is sized never to fill (see la_result_queue_init) */
  while (la_Shared_result_queue.count >= la_Shared_result_queue.capacity && la_Shared_result_shutdown == false)
    {
      pthread_cond_wait (&la_Shared_result_not_full, &la_Shared_result_mutex);
    }

  if (la_Shared_result_shutdown)
    {
      pthread_mutex_unlock (&la_Shared_result_mutex);
      return ER_FAILED;
    }

  la_Shared_result_queue.results[la_Shared_result_queue.tail] = *result;
  la_Shared_result_queue.tail = (la_Shared_result_queue.tail + 1) % la_Shared_result_queue.capacity;
  la_Shared_result_queue.count++;

  pthread_cond_signal (&la_Shared_result_not_empty);
  pthread_mutex_unlock (&la_Shared_result_mutex);

  return NO_ERROR;
}

/*
 * la_result_queue_push_many() - hand the results of the transactions one slave commit made durable to the coordinator
 *   return: NO_ERROR, or ER_FAILED when the queue shut down before all of them were queued
 *   results(in): results to deliver, in the order the tasks were taken; queued as copies
 *   count(in): number of results
 *   pushed(out): how many were queued
 *
 * Note: one lock and one wakeup for the whole commit instead of one per result. Waits like la_result_queue_push
 *       if the queue is ever full.
 */
int
la_result_queue_push_many (const LA_RESULT *results, int count, int *pushed)
{
  int i;

  assert (results != NULL && pushed != NULL && count >= 0);

  *pushed = 0;

  pthread_mutex_lock (&la_Shared_result_mutex);

  for (i = 0; i < count; i++)
    {
      /* safeguard only: the queue is sized never to fill (see la_result_queue_init) */
      while (la_Shared_result_queue.count >= la_Shared_result_queue.capacity && la_Shared_result_shutdown == false)
	{
	  pthread_cond_signal (&la_Shared_result_not_empty);
	  pthread_cond_wait (&la_Shared_result_not_full, &la_Shared_result_mutex);
	}

      if (la_Shared_result_shutdown)
	{
	  break;
	}

      la_Shared_result_queue.results[la_Shared_result_queue.tail] = results[i];
      la_Shared_result_queue.tail = (la_Shared_result_queue.tail + 1) % la_Shared_result_queue.capacity;
      la_Shared_result_queue.count++;
    }

  *pushed = i;
  if (i > 0)
    {
      pthread_cond_signal (&la_Shared_result_not_empty);
    }
  pthread_mutex_unlock (&la_Shared_result_mutex);

  return (i == count) ? NO_ERROR : ER_FAILED;
}

/*
 * la_result_queue_try_pop() - take the oldest result without waiting
 *   return: NO_ERROR when a result was taken, ER_FAILED when the queue is empty
 *   result(out): the dequeued result
 */
int
la_result_queue_try_pop (LA_RESULT *result)
{
  assert (result != NULL);

  pthread_mutex_lock (&la_Shared_result_mutex);

  if (la_Shared_result_queue.count == 0)
    {
      pthread_mutex_unlock (&la_Shared_result_mutex);
      return ER_FAILED;
    }

  *result = la_Shared_result_queue.results[la_Shared_result_queue.head];
  la_Shared_result_queue.head = (la_Shared_result_queue.head + 1) % la_Shared_result_queue.capacity;
  la_Shared_result_queue.count--;

  pthread_cond_signal (&la_Shared_result_not_full);
  pthread_mutex_unlock (&la_Shared_result_mutex);

  return NO_ERROR;
}

/*
 * la_result_queue_wait_for_result() - wait at most timeout_msec for a worker to queue a result, without dequeuing it
 *   return: true when a result is queued or the queue is shutting down, false on timeout
 *   timeout_msec(in): longest time to wait
 */
bool
la_result_queue_wait_for_result (int timeout_msec)
{
  struct timespec abstime;
  bool available;

  la_monotonic_deadline ((UINT64) timeout_msec * 1000000ULL, &abstime);

  pthread_mutex_lock (&la_Shared_result_mutex);

  while (la_Shared_result_queue.count == 0 && la_Shared_result_shutdown == false)
    {
      if (pthread_cond_timedwait (&la_Shared_result_not_empty, &la_Shared_result_mutex, &abstime) == ETIMEDOUT)
	{
	  break;
	}
    }

  available = (la_Shared_result_queue.count > 0 || la_Shared_result_shutdown);
  pthread_mutex_unlock (&la_Shared_result_mutex);

  return available;
}

/*
 * la_result_queue_signal_shutdown() - mark the queue as shutting down and wake every waiter
 */
void
la_result_queue_signal_shutdown (void)
{
  pthread_mutex_lock (&la_Shared_result_mutex);

  la_Shared_result_shutdown = true;

  pthread_cond_broadcast (&la_Shared_result_not_empty);
  pthread_cond_broadcast (&la_Shared_result_not_full);
  pthread_mutex_unlock (&la_Shared_result_mutex);
}

/*
 * la_result_queue_get_count() - return the current queue depth
 *
 * Note:
 *   Read without the lock, so the value may be stale; it is used only for status output.
 */
int
la_result_queue_get_count (void)
{
  return la_Shared_result_queue.count;
}

