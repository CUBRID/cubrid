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

#include "log_applier_queue.hpp"

#include "error_code.h"
#include "scope_exit.hpp"

#include <cassert>
#include <deque>
#include <new>
#include <pthread.h>

struct la_task_ring
{
  la_task_ring () : entries (NULL), capacity (0), head (0), tail (0), count (0) {}

  la_apply_task *entries;
  std::size_t capacity;
  std::size_t head;
  std::size_t tail;
  std::size_t count;
};

struct la_task_queue
{
  la_task_queue () : accepting (true), stopping (false) {}

  la_task_ring ring;
  bool accepting;
  bool stopping;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
};

struct la_result_queue
{
  std::deque<la_apply_result> entries;
  pthread_mutex_t mutex;
};

/*
 * la_task_ring_enqueue - append a task to the ring
 *
 * return: true if the task was appended
 *
 * ring(in/out): task ring
 * task(in): task to append
 */
static bool
la_task_ring_enqueue (la_task_ring &ring, const la_apply_task &task)
{
  if (ring.count == ring.capacity)
    {
      return false;
    }

  ring.entries[ring.tail] = task;
  ring.tail = (ring.tail + 1) % ring.capacity;
  ring.count++;

  return true;
}

/*
 * la_task_ring_dequeue - remove the oldest task from the ring
 *
 * return: true if a task was removed
 *
 * ring(in/out): task ring
 * task(out): removed task
 */
static bool
la_task_ring_dequeue (la_task_ring &ring, la_apply_task &task)
{
  if (ring.count == 0)
    {
      return false;
    }

  task = ring.entries[ring.head];
  ring.head = (ring.head + 1) % ring.capacity;
  ring.count--;

  return true;
}

/*
 * la_task_queue_create - create a fixed-capacity shared task queue
 *
 * return: NO_ERROR or an error code
 *
 * capacity(in): maximum number of queued tasks
 * queue_out(out): created task queue
 */
int
la_task_queue_create (std::size_t capacity, la_task_queue **queue_out)
{
  assert (queue_out != NULL);
  *queue_out = NULL;

  if (capacity == 0)
    {
      return ER_FAILED;
    }

  la_task_queue *queue = new (std::nothrow) la_task_queue ();
  if (queue == NULL)
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
	  pthread_cond_destroy (&queue->condition);
	}
      if (mutex_initialized)
	{
	  pthread_mutex_destroy (&queue->mutex);
	}
      delete[] queue->ring.entries;
      delete queue;
    }
  };

  queue->ring.entries = new (std::nothrow) la_apply_task[capacity] ();
  if (queue->ring.entries == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  queue->ring.capacity = capacity;

  if (pthread_mutex_init (&queue->mutex, NULL) != 0)
    {
      return ER_FAILED;
    }
  mutex_initialized = true;

  if (pthread_cond_init (&queue->condition, NULL) != 0)
    {
      return ER_FAILED;
    }
  condition_initialized = true;

  *queue_out = queue;
  cleanup.release ();
  return NO_ERROR;
}

/*
 * la_task_queue_destroy - release a task queue
 *
 * return: none
 *
 * queue(in): task queue, or NULL
 */
void
la_task_queue_destroy (la_task_queue *queue)
{
  if (queue == NULL)
    {
      return;
    }

  pthread_cond_destroy (&queue->condition);
  pthread_mutex_destroy (&queue->mutex);
  delete[] queue->ring.entries;
  delete queue;
}

/*
 * la_task_queue_shutdown - reject new tasks and wake waiting workers
 *
 * return: none
 *
 * queue(in): task queue
 */
void
la_task_queue_shutdown (la_task_queue *queue)
{
  assert (queue != NULL);

  pthread_mutex_lock (&queue->mutex);
  queue->accepting = false;
  queue->stopping = true;
  pthread_cond_broadcast (&queue->condition);
  pthread_mutex_unlock (&queue->mutex);
}

/*
 * la_task_queue_try_enqueue - enqueue a task without waiting for capacity
 *
 * return: accepted, full, or stopped
 *
 * queue(in/out): task queue
 * task(in): task to enqueue
 */
la_queue_enqueue_status
la_task_queue_try_enqueue (la_task_queue *queue, const la_apply_task &task)
{
  assert (queue != NULL);

  pthread_mutex_lock (&queue->mutex);
  if (!queue->accepting)
    {
      pthread_mutex_unlock (&queue->mutex);
      return LA_QUEUE_ENQUEUE_STOPPED;
    }

  bool enqueued = la_task_ring_enqueue (queue->ring, task);
  if (enqueued)
    {
      pthread_cond_signal (&queue->condition);
    }

  pthread_mutex_unlock (&queue->mutex);

  return enqueued ? LA_QUEUE_ENQUEUE_ACCEPTED : LA_QUEUE_ENQUEUE_FULL;
}

/*
 * la_task_queue_dequeue - wait for and dequeue a task
 *
 * return: true if a task was dequeued, false after queue shutdown
 *
 * queue(in/out): task queue
 * task(out): dequeued task
 */
bool
la_task_queue_dequeue (la_task_queue *queue, la_apply_task &task)
{
  assert (queue != NULL);

  pthread_mutex_lock (&queue->mutex);
  while (queue->ring.count == 0 && !queue->stopping)
    {
      pthread_cond_wait (&queue->condition, &queue->mutex);
    }

  if (queue->stopping)
    {
      pthread_mutex_unlock (&queue->mutex);
      return false;
    }

  bool dequeued = la_task_ring_dequeue (queue->ring, task);
  pthread_mutex_unlock (&queue->mutex);

  return dequeued;
}

/*
 * la_result_queue_create - create a shared result queue
 *
 * return: NO_ERROR or an error code
 *
 * queue_out(out): created result queue
 */
int
la_result_queue_create (la_result_queue **queue_out)
{
  assert (queue_out != NULL);

  *queue_out = NULL;

  la_result_queue *queue = new (std::nothrow) la_result_queue ();
  if (queue == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  scope_exit cleanup {[&] { delete queue; }};
  if (pthread_mutex_init (&queue->mutex, NULL) != 0)
    {
      return ER_FAILED;
    }

  *queue_out = queue;
  cleanup.release ();
  return NO_ERROR;
}

/*
 * la_result_queue_destroy - release a result queue
 *
 * return: none
 *
 * queue(in): result queue, or NULL
 */
void
la_result_queue_destroy (la_result_queue *queue)
{
  if (queue == NULL)
    {
      return;
    }

  pthread_mutex_destroy (&queue->mutex);
  delete queue;
}

/*
 * la_result_queue_enqueue - enqueue a completed task result
 *
 * return: none
 *
 * queue(in/out): result queue
 * result(in): result to enqueue
 */
void
la_result_queue_enqueue (la_result_queue *queue, const la_apply_result &result)
{
  assert (queue != NULL);

  pthread_mutex_lock (&queue->mutex);
  queue->entries.push_back (result);
  pthread_mutex_unlock (&queue->mutex);
}

/*
 * la_result_queue_try_dequeue - dequeue a result without waiting
 *
 * return: true if a result was dequeued
 *
 * queue(in/out): result queue
 * result(out): dequeued result
 */
bool
la_result_queue_try_dequeue (la_result_queue *queue, la_apply_result &result)
{
  assert (queue != NULL);

  pthread_mutex_lock (&queue->mutex);
  bool dequeued = !queue->entries.empty ();
  if (dequeued)
    {
      result = queue->entries.front ();
      queue->entries.pop_front ();
    }

  pthread_mutex_unlock (&queue->mutex);

  return dequeued;
}
