/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "log_applier_coordinator.hpp"

#include "error_code.h"
#include "scope_exit.hpp"

#include <cassert>
#include <new>

struct la_coordinator
{
  la_coordinator () : task_queue (NULL), result_queue (NULL), workers (NULL), dispatch_order (NULL), started (false) {}

  la_task_queue *task_queue;
  la_result_queue *result_queue;
  la_workers *workers;
  la_dispatch_order *dispatch_order;
  bool started;
};

/*
 * la_coordinator_create - create resources owned by the applylogdb coordinator
 *
 * return: NO_ERROR or an error code
 *
 * worker_count(in): number of worker threads
 * task_queue_capacity(in): maximum number of queued tasks
 * dispatch_order_capacity(in): maximum number of tasks retained until ordered retirement
 * worker_operations(in): fixed apply callbacks provided by log_applier.c
 * shared_context(in): context shared by all worker operations
 * coordinator_out(out): handle to the created coordinator-owned resources
 *
 * Note: The caller remains the coordinator; this function does not create a coordinator thread.
 */
int
la_coordinator_create (std::size_t worker_count, std::size_t task_queue_capacity,
		       std::size_t dispatch_order_capacity, const la_worker_operations &worker_operations,
		       void *shared_context, la_coordinator **coordinator_out)
{
  assert (coordinator_out != NULL);
  *coordinator_out = NULL;

  la_coordinator *coordinator = new (std::nothrow) la_coordinator ();
  if (coordinator == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  scope_exit cleanup {[&] { la_coordinator_destroy (coordinator); }};

  int error = la_task_queue_create (task_queue_capacity, &coordinator->task_queue);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = la_result_queue_create (&coordinator->result_queue);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = la_dispatch_order_create (dispatch_order_capacity, &coordinator->dispatch_order);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = la_worker_create (worker_count, coordinator->task_queue, coordinator->result_queue, worker_operations,
			    shared_context, &coordinator->workers);
  if (error != NO_ERROR)
    {
      return error;
    }

  *coordinator_out = coordinator;
  cleanup.release ();

  return NO_ERROR;
}

/*
 * la_coordinator_start_workers - start all workers and wait until they are ready
 *
 * return: NO_ERROR or an error code
 *
 * coordinator(in/out): coordinator
 */
int
la_coordinator_start_workers (la_coordinator *coordinator)
{
  assert (coordinator != NULL);

  int error = la_worker_start (coordinator->workers);
  if (error == NO_ERROR)
    {
      coordinator->started = true;
    }

  return error;
}

/*
 * la_coordinator_stop_workers - stop task delivery and join all workers
 *
 * return: none
 *
 * coordinator(in/out): coordinator, or NULL
 */
void
la_coordinator_stop_workers (la_coordinator *coordinator)
{
  if (coordinator == NULL || !coordinator->started)
    {
      return;
    }

  la_task_queue_shutdown (coordinator->task_queue);
  la_worker_stop (coordinator->workers);
  coordinator->started = false;
}

/*
 * la_coordinator_destroy - stop remaining workers and release coordinator-owned resources
 *
 * return: none
 *
 * coordinator(in): coordinator, or NULL
 */
void
la_coordinator_destroy (la_coordinator *coordinator)
{
  if (coordinator == NULL)
    {
      return;
    }

  la_coordinator_stop_workers (coordinator);
  la_worker_destroy (coordinator->workers);
  la_dispatch_order_destroy (coordinator->dispatch_order);
  la_result_queue_destroy (coordinator->result_queue);
  la_task_queue_destroy (coordinator->task_queue);
  delete coordinator;
}

/*
 * la_coordinator_try_enqueue_task - submit a task without waiting for capacity
 *
 * return: dispatch status
 *
 * coordinator(in/out): coordinator
 * task(in/out): task receiving its dispatch sequence
 */
la_dispatch_status
la_coordinator_try_enqueue_task (la_coordinator *coordinator, la_apply_task &task)
{
  assert (coordinator != NULL);

  if (!coordinator->started)
    {
      return LA_DISPATCH_STOPPED;
    }

  return la_dispatch_order_try_enqueue_task (coordinator->dispatch_order, coordinator->task_queue, task);
}

/*
 * la_coordinator_collect_results - attach available worker results to dispatched tasks
 *
 * return: NO_ERROR or an error code
 *
 * coordinator(in/out): coordinator
 * collected_count(out): number of collected results
 */
int
la_coordinator_collect_results (la_coordinator *coordinator, std::size_t *collected_count)
{
  assert (coordinator != NULL);
  assert (collected_count != NULL);

  *collected_count = 0;

  la_apply_result result;
  while (la_result_queue_try_dequeue (coordinator->result_queue, result))
    {
      int error = la_dispatch_order_record_task_result (coordinator->dispatch_order, result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      (*collected_count)++;
    }

  return NO_ERROR;
}

/*
 * la_coordinator_try_retire_task - retire the oldest dispatched task when its result is ready
 *
 * return: true if a task was retired
 *
 * coordinator(in/out): coordinator
 * retired(out): retired task state
 */
bool
la_coordinator_try_retire_task (la_coordinator *coordinator, la_retired_task &retired)
{
  assert (coordinator != NULL);

  return la_dispatch_order_try_retire_task (coordinator->dispatch_order, retired);
}

/*
 * la_coordinator_unretired_count - return the number of dispatched tasks not yet retired
 *
 * return: unretired task count
 *
 * coordinator(in): coordinator
 */
std::size_t
la_coordinator_unretired_count (const la_coordinator *coordinator)
{
  assert (coordinator != NULL);

  return la_dispatch_order_unretired_count (coordinator->dispatch_order);
}
