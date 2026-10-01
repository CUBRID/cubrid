/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "log_applier_dispatch_order.hpp"

#include "error_code.h"

#include <cassert>
#include <cstdint>
#include <new>

struct la_dispatch_entry
{
  la_dispatch_entry () : apply (NULL), record_type (0), result_ready (false) {}

  std::uint64_t dispatch_sequence;
  la_apply *apply;
  int record_type;
  bool result_ready;
  la_apply_result result;
};

struct la_dispatch_order
{
  la_dispatch_order () : entries (NULL), capacity (0), head (0), tail (0), count (0), next_sequence (1) {}

  la_dispatch_entry *entries;
  std::size_t capacity;
  std::size_t head;
  std::size_t tail;
  std::size_t count;
  std::uint64_t next_sequence;
};

/*
 * la_dispatch_order_create - create coordinator-owned dispatch order
 *
 * return: NO_ERROR or an error code
 *
 * capacity(in): maximum number of entries retained until ordered retirement
 * order_out(out): created dispatch order
 */
int
la_dispatch_order_create (std::size_t capacity, la_dispatch_order **order_out)
{
  assert (order_out != NULL);
  *order_out = NULL;

  if (capacity == 0)
    {
      return ER_FAILED;
    }

  la_dispatch_order *order = new (std::nothrow) la_dispatch_order ();
  if (order == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  order->entries = new (std::nothrow) la_dispatch_entry[capacity] ();
  if (order->entries == NULL)
    {
      delete order;
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  order->capacity = capacity;

  *order_out = order;

  return NO_ERROR;
}

/*
 * la_dispatch_order_destroy - release a dispatch order
 *
 * return: none
 *
 * order(in): dispatch order, or NULL
 */
void
la_dispatch_order_destroy (la_dispatch_order *order)
{
  if (order == NULL)
    {
      return;
    }

  delete[] order->entries;
  delete order;
}

/*
 * la_dispatch_order_try_enqueue_task - enqueue a task and record its dispatch order
 *
 * return: dispatch status
 *
 * order(in/out): coordinator dispatch order
 * task_queue(in/out): shared worker queue
 * task(in/out): task receiving its dispatch sequence
 *
 * Note: The dispatch-order entry is created only after the queue accepts the task.
 */
la_dispatch_status
la_dispatch_order_try_enqueue_task (la_dispatch_order *order, la_task_queue *task_queue, la_apply_task &task)
{
  assert (order != NULL);
  assert (task_queue != NULL);

  if (order->count == order->capacity)
    {
      return LA_DISPATCH_LIMIT_REACHED;
    }

  task.dispatch_sequence = order->next_sequence;
  la_queue_enqueue_status queue_status = la_task_queue_try_enqueue (task_queue, task);
  if (queue_status == LA_QUEUE_ENQUEUE_FULL)
    {
      return LA_DISPATCH_FULL;
    }
  if (queue_status == LA_QUEUE_ENQUEUE_STOPPED)
    {
      return LA_DISPATCH_STOPPED;
    }

  la_dispatch_entry &entry = order->entries[order->tail];
  entry.dispatch_sequence = task.dispatch_sequence;
  entry.apply = task.apply;
  entry.record_type = task.record_type;
  entry.result_ready = false;

  order->tail = (order->tail + 1) % order->capacity;
  order->count++;
  order->next_sequence++;

  return LA_DISPATCH_ACCEPTED;
}

/*
 * la_dispatch_order_record_task_result - record a worker result in its task entry
 *
 * return: NO_ERROR, or ER_FAILED for an unknown or duplicate sequence
 *
 * order(in/out): coordinator dispatch order
 * result(in): worker result
 */
int
la_dispatch_order_record_task_result (la_dispatch_order *order, const la_apply_result &result)
{
  assert (order != NULL);

  std::size_t index = order->head;
  for (std::size_t i = 0; i < order->count; i++)
    {
      la_dispatch_entry &entry = order->entries[index];
      if (entry.dispatch_sequence == result.dispatch_sequence)
	{
	  if (entry.result_ready)
	    {
	      return ER_FAILED;
	    }

	  entry.result = result;
	  entry.result_ready = true;

	  return NO_ERROR;
	}
      index = (index + 1) % order->capacity;
    }

  return ER_FAILED;
}

/*
 * la_dispatch_order_try_retire_task - retire the oldest task when its result is ready
 *
 * return: true if an entry was retired
 *
 * order(in/out): coordinator dispatch order
 * retired(out): result and task-owned references needed by coordinator cleanup
 */
bool
la_dispatch_order_try_retire_task (la_dispatch_order *order, la_retired_task &retired)
{
  assert (order != NULL);

  if (order->count == 0 || !order->entries[order->head].result_ready)
    {
      return false;
    }

  la_dispatch_entry &entry = order->entries[order->head];
  retired.apply = entry.apply;
  retired.record_type = entry.record_type;
  retired.result = entry.result;

  entry = la_dispatch_entry ();
  order->head = (order->head + 1) % order->capacity;
  order->count--;

  return true;
}

/*
 * la_dispatch_order_unretired_count - return the number of entries not yet retired
 *
 * return: unretired entry count
 *
 * order(in): coordinator dispatch order
 */
std::size_t
la_dispatch_order_unretired_count (const la_dispatch_order *order)
{
  assert (order != NULL);
  return order->count;
}
