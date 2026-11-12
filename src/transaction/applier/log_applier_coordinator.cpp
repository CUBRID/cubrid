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
 * log_applier_coordinator.cpp - implementation of the parallel apply coordinator
 */

#include "config.h"

#include "log_applier_coordinator.hpp"
#include "log_applier_debug.hpp"

#include "log_applier_gate.hpp"
#include "log_applier_pending.hpp"
#include "log_applier_result_queue.hpp"
#include "log_applier_stat.hpp"
#include "log_applier_task_queue.hpp"
#include "error_manager.h"
#include "log_record.hpp"
#include "system_parameter.h"

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef enum
{
  LA_SCHED_DISPATCHED = 0,
  LA_SCHED_RELEASED = 1,
  LA_SCHED_PARKED = 2
} LA_SCHED_STATUS;

static LA_COORDINATOR_CONFIG la_Coordinator_config;
static bool la_Coordinator_initialized = false;

static UINT64 la_coordinator_clock_ns (void);
static int la_coordinator_drain_ready (void);
static int la_coordinator_wait_for_commit_order_slot (void);
static int la_coordinator_enqueue_task (const LA_TASK *task_in, LA_SCHED_STATUS sched_status);
static void la_coordinator_trace_schedule (const LA_TASK *task, LA_SCHED_STATUS sched_status, UINT64 dispatch_seq);
static int la_coordinator_collect_results (void);
static int la_coordinator_record_result (LA_RESULT *result);
static int la_coordinator_pop_result (LA_RESULT *result);
static int la_coordinator_wait_until_retired (void);
static int la_coordinator_wait_for_inflight_slot (void);
static int la_coordinator_retire_results (void);
static int la_coordinator_run_alone (LA_TASK *task);
static bool la_coordinator_merge_into_pending (LA_TASK *task, int *error);

/*
 * la_coordinator_clock_ns - read the monotonic clock used by scheduling statistics
 *
 * return: monotonic time in nanoseconds
 */
static UINT64
la_coordinator_clock_ns (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  return (UINT64) ts.tv_sec * 1000000000ULL + (UINT64) ts.tv_nsec;
}

/*
 * la_coordinator_initialize - initialize coordinator-owned scheduling state
 *
 * return: NO_ERROR or an error code
 *
 *   config(in): queue capacities, callbacks and restart position
 *
 * Note: restart_lsa remains owned by the caller and is read when the first task is submitted.
 */
int
la_coordinator_initialize (const LA_COORDINATOR_CONFIG *config)
{
  int error;

  assert (config != NULL);
  assert (config->commit_order_capacity > 0);
  assert (config->worker_count > 0);
  assert (config->restart_lsa != NULL);
  assert (config->before_dispatch != NULL);
  assert (config->retire_entry != NULL);
  assert (config->store_progress != NULL);
  assert (config->shutdown_requested != NULL);

  la_Coordinator_config = *config;

  la_task_queue_init ();
  la_pending_reset ();

  error = la_result_queue_init (config->commit_order_capacity);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = la_inflight_init (config->commit_order_capacity);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = la_gate_init (config->commit_order_capacity);
  if (error != NO_ERROR)
    {
      return error;
    }

  la_Coordinator_initialized = true;

  return NO_ERROR;
}

/*
 * la_coordinator_drain_ready - dispatch pending tasks whose dependency is satisfied
 *
 * return: NO_ERROR or an error code
 *
 * Note: ready tasks are dispatched in pending-list order.
 */
static int
la_coordinator_drain_ready (void)
{
  LA_TASK task;
  int error;

  la_pending_mark_ready (la_coordinator_clock_ns ());

  while (!la_task_queue_is_full () && la_pending_pop_ready (&task))
    {
      error = la_coordinator_enqueue_task (&task, LA_SCHED_RELEASED);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * la_coordinator_wait_for_commit_order_slot - make room in the commit-order ring
 *
 * return: NO_ERROR or an error code
 *
 * Note: the coordinator stops accepting new commits but keeps collecting results so the frontier can advance.
 */
static int
la_coordinator_wait_for_commit_order_slot (void)
{
  int error;

  while (la_gate_order_get_count () >= la_Coordinator_config.commit_order_capacity)
    {
      if (la_Coordinator_config.shutdown_requested (la_Coordinator_config.context))
	{
	  return ER_FAILED;
	}

      error = la_coordinator_collect_and_retire ();
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (la_gate_order_get_count () >= la_Coordinator_config.commit_order_capacity)
	{
	  (void) la_result_queue_wait_for_result (100);
	}
    }

  return NO_ERROR;
}

/*
 * la_coordinator_enqueue_task - record and enqueue one runnable task
 *
 * return: NO_ERROR or an error code
 *
 *   task_in(in): task released by the dependency gate
 *   sched_status(in): reason the task is being dispatched
 *
 * Note: the in-flight entry is created first so every worker result has a matching dispatch number.
 */
static int
la_coordinator_enqueue_task (const LA_TASK *task_in, LA_SCHED_STATUS sched_status)
{
  LA_TASK task;
  UINT64 dispatch_seq = 0;
  int error;

  assert (task_in != NULL);

  task = *task_in;
  if (task.ready_ns == 0)
    {
      task.ready_ns = la_coordinator_clock_ns ();
    }

  error = la_Coordinator_config.before_dispatch (la_Coordinator_config.context, &task);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = la_coordinator_wait_for_inflight_slot ();
  if (error != NO_ERROR)
    {
      return error;
    }

  error = la_inflight_push (task.apply, task.tranid, task.rectype, &dispatch_seq);
  if (error != NO_ERROR)
    {
      return error;
    }
  task.seq = dispatch_seq;

  if (sched_status == LA_SCHED_DISPATCHED && task.has_items && task.rectype == LOG_COMMIT)
    {
      la_stat_count_dispatch_direct ();
    }
  la_coordinator_trace_schedule (&task, sched_status, dispatch_seq);

  task.queue_enter_ns = la_coordinator_clock_ns ();

  return la_task_queue_push (&task);
}

/*
 * la_coordinator_trace_schedule - trace one scheduling decision
 *
 * return: none
 */
static void
la_coordinator_trace_schedule (const LA_TASK *task, LA_SCHED_STATUS sched_status, UINT64 dispatch_seq)
{
  LA_WSET_TRACE (1, "coordinator schedule trid=%d commit_lsa=%lld|%d type=%d dependency=%lld|%d "
		 "status=%d dispatch=%llu\n",
		 task->tranid, LSA_AS_ARGS (&task->commit_lsa), task->rectype,
		 LSA_AS_ARGS (&task->dependency_lsa), (int) sched_status,
		 (unsigned long long) dispatch_seq);
}

/*
 * la_coordinator_drain_complete - check whether all accepted work is retired
 *
 * return: true if pending, in-flight and commit-order state are empty
 */
bool
la_coordinator_drain_complete (void)
{
  return (la_pending_is_empty () && la_inflight_get_count () == 0 && la_gate_order_get_count () == 0);
}

/*
 * la_coordinator_collect_and_retire - collect results and retire the completed dispatch prefix
 *
 * return: NO_ERROR or an error code
 */
int
la_coordinator_collect_and_retire (void)
{
  int error;

  error = la_coordinator_collect_results ();
  if (error != NO_ERROR)
    {
      return error;
    }

  return la_coordinator_retire_results ();
}

/*
 * la_coordinator_collect_results - consume all currently queued worker results
 *
 * return: NO_ERROR or an error code
 */
static int
la_coordinator_collect_results (void)
{
  LA_RESULT result;
  int error;

  while (la_result_queue_try_pop (&result) == NO_ERROR)
    {
      error = la_coordinator_record_result (&result);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  la_gate_advance_frontier ();

  return la_coordinator_drain_ready ();
}

/*
 * la_coordinator_record_result - attach one result and update completion state
 *
 * return: NO_ERROR or an error code
 */
static int
la_coordinator_record_result (LA_RESULT *result)
{
  int error;

  assert (result != NULL);

  result->collected_ns = la_coordinator_clock_ns ();

  error = la_inflight_attach_result (result);
  if (error != NO_ERROR)
    {
      return error;
    }

  la_stat_note_result_collected (result);

  if (result->error == NO_ERROR && (result->rectype == LOG_COMMIT || result->rectype == LOG_SYSOP_END))
    {
      la_gate_mark_completed (&result->commit_lsa);
      for (int i = 0; i < result->merged_count; i++)
	{
	  la_gate_mark_completed (&result->merged_lsa[i]);
	}
    }

  return NO_ERROR;
}

/*
 * la_coordinator_complete_without_task - complete a taskless SYSOP_END boundary
 *
 * return: NO_ERROR or an error code
 *
 *   lsa(in): SYSOP_END position to record and complete
 *
 * Note: the boundary still enters the commit-order ring so later dependencies can be released in order.
 */
int
la_coordinator_complete_without_task (const LOG_LSA *lsa)
{
  int error;

  assert (lsa != NULL);

  error = la_coordinator_wait_for_commit_order_slot ();
  if (error != NO_ERROR)
    {
      return error;
    }

  la_gate_set_initial_frontier (la_Coordinator_config.restart_lsa);

  error = la_gate_order_push (lsa);
  if (error != NO_ERROR)
    {
      return error;
    }

  la_gate_mark_completed (lsa);
  la_gate_advance_frontier ();

  LA_WSET_TRACE (1, "coordinator complete_without_task lsa=%lld|%d\n",
		 LSA_AS_ARGS (lsa));

  return la_coordinator_drain_ready ();
}

/*
 * la_coordinator_pop_result - wait until one worker result can be removed
 *
 * return: NO_ERROR or ER_FAILED when shutdown is requested
 */
static int
la_coordinator_pop_result (LA_RESULT *result)
{
  assert (result != NULL);

  while (true)
    {
      if (la_result_queue_try_pop (result) == NO_ERROR)
	{
	  return NO_ERROR;
	}

      if (la_Coordinator_config.shutdown_requested (la_Coordinator_config.context))
	{
	  return ER_FAILED;
	}

      (void) la_result_queue_wait_for_result (100);
    }
}

/*
 * la_coordinator_wait_until_retired - wait until every in-flight task is retired
 *
 * return: NO_ERROR or an error code
 */
static int
la_coordinator_wait_until_retired (void)
{
  LA_RESULT result;
  int error;

  while (la_inflight_get_count () > 0)
    {
      error = la_coordinator_pop_result (&result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = la_coordinator_record_result (&result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      la_gate_advance_frontier ();

      error = la_coordinator_retire_results ();
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * la_coordinator_wait_for_inflight_slot - make room in the in-flight list
 *
 * return: NO_ERROR or an error code
 */
static int
la_coordinator_wait_for_inflight_slot (void)
{
  LA_RESULT result;
  int error;

  while (la_inflight_get_count () >= la_inflight_get_capacity ())
    {
      error = la_coordinator_retire_results ();
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (la_inflight_get_count () < la_inflight_get_capacity ())
	{
	  break;
	}

      error = la_coordinator_pop_result (&result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = la_coordinator_record_result (&result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      la_gate_advance_frontier ();

      error = la_coordinator_retire_results ();
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * la_coordinator_retire_results - retire the completed prefix of the in-flight list
 *
 * return: NO_ERROR or an error code
 *
 * Note: a later result cannot release transaction state that precedes an unfinished entry.
 */
static int
la_coordinator_retire_results (void)
{
  LA_INFLIGHT_ENTRY *entry;
  const LA_RESULT *result;
  LOG_LSA frontier;
  bool has_frontier;
  int error;

  while ((entry = la_inflight_peek ()) != NULL && la_inflight_entry_has_result (entry))
    {
      result = la_inflight_entry_get_result (entry);
      assert (result != NULL);

      if (result->error != NO_ERROR)
	{
	  LA_WSET_TRACE (1, "coordinator result trid=%d commit_lsa=%lld|%d type=%d worker=%d error=%d\n",
			 result->tranid, LSA_AS_ARGS (&result->commit_lsa), result->rectype,
			 result->worker_idx, result->error);
	  return result->error;
	}

      LA_WSET_TRACE (1, "coordinator retire trid=%d commit_lsa=%lld|%d type=%d worker=%d dispatch=%llu\n",
		     result->tranid, LSA_AS_ARGS (&result->commit_lsa),
		     result->rectype, result->worker_idx,
		     (unsigned long long) la_inflight_entry_get_sequence (entry));

      /* the call must stay outside assert(): a release build drops the whole assert expression */
      has_frontier = la_gate_get_frontier (&frontier);
      assert (has_frontier);
      la_Coordinator_config.retire_entry (la_Coordinator_config.context, entry, has_frontier ? &frontier : NULL);
      if (result->merged_lsa != NULL)
	{
	  free ((void *) result->merged_lsa);
	}
      la_inflight_pop ();

      error = la_Coordinator_config.store_progress (la_Coordinator_config.context);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * la_coordinator_run_alone - drain existing work and run one task by itself
 *
 * return: NO_ERROR or an error code
 */
static int
la_coordinator_run_alone (LA_TASK *task)
{
  int error;

  assert (task != NULL);

  do
    {
      error = la_coordinator_drain_ready ();
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = la_coordinator_wait_until_retired ();
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  while (!la_pending_is_empty ());

  error = la_coordinator_enqueue_task (task, LA_SCHED_DISPATCHED);
  if (error != NO_ERROR)
    {
      return error;
    }

  return la_coordinator_wait_until_retired ();
}

/*
 * la_coordinator_merge_into_pending - let a fragment join the parked fragment of the previous change of its key
 *
 * return: true if the task was merged (error tells whether that succeeded), false to schedule it on its own
 *
 *   task(in/out): fragment task just read from the log, already on the commit-order ring
 *   error(out): NO_ERROR or an error code when merged
 *
 * Note: Independent replication units of one key (updates of one serial, INCR/DECR of one row) are ordered one
 *       after another, so a backlog of them is applied one slave transaction at a time. When the previous one is
 *       still parked, this one is applied by the same task right after it: one slave transaction instead of one
 *       per unit. Its LSA stays on the commit-order ring and is completed with the task, so whatever waits for it
 *       (the next unit, its outer commit) is released as before. The task only waits for the dependency of the
 *       parked task: the merged fragment has that single key (the master's label says so), whose earlier writers
 *       are the parked task's own fragments or lie at or below that dependency.
 */
static bool
la_coordinator_merge_into_pending (LA_TASK *task, int *error)
{
  LA_TASK *target;
  LOG_LSA *grown;

  assert (task != NULL && error != NULL);
  *error = NO_ERROR;

  if (task->unit_key_hash == 0 || la_Coordinator_config.merge_task == NULL)
    {
      return false;
    }

  target = la_pending_find_merge_target (&task->dependency_lsa, task->unit_key_hash);
  if (target == NULL)
    {
      return false;
    }

  if (target->merged_count == target->merged_capacity)
    {
      int capacity = (target->merged_capacity == 0) ? 16 : target->merged_capacity * 2;

      grown = (LOG_LSA *) realloc (target->merged_lsa, capacity * sizeof (LOG_LSA));
      if (grown == NULL)
	{
	  /* not merged: the fragment is scheduled on its own */
	  return false;
	}
      target->merged_lsa = grown;
      target->merged_capacity = capacity;
    }

  if (!la_Coordinator_config.merge_task (la_Coordinator_config.context, target, task))
    {
      return false;
    }

  LSA_COPY (&target->merged_lsa[target->merged_count], &task->commit_lsa);
  target->merged_count++;

  if (prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL) >= 1)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "[WSET] coordinator merge trid=%d sysop_lsa=%lld|%d into trid=%d sysop_lsa=%lld|%d merged=%d\n",
		     task->tranid, (long long) task->commit_lsa.pageid, (int) task->commit_lsa.offset, target->tranid,
		     (long long) target->commit_lsa.pageid, (int) target->commit_lsa.offset, target->merged_count);
    }

  return true;
}

/*
 * la_coordinator_submit_task - register and schedule one complete log task
 *
 * return: NO_ERROR or an error code
 *
 *   task(in/out): task to dispatch now or retain until its dependency is complete
 */
int
la_coordinator_submit_task (LA_TASK *task)
{
  LOG_LSA label;
  bool dependency_ready;
  int error;

  assert (la_Coordinator_initialized);
  assert (task != NULL);

  task->created_ns = la_coordinator_clock_ns ();

  error = la_coordinator_wait_for_commit_order_slot ();
  if (error != NO_ERROR)
    {
      return error;
    }

  la_gate_set_initial_frontier (la_Coordinator_config.restart_lsa);

  LSA_COPY (&label, &task->dependency_lsa);
  if (la_gate_bound_dependency (&task->dependency_lsa))
    {
      LA_WSET_TRACE (1, "coordinator label lowered trid=%d commit_lsa=%lld|%d label=%lld|%d "
		     "dependency=%lld|%d\n",
		     task->tranid, LSA_AS_ARGS (&task->commit_lsa),
		     LSA_AS_ARGS (&label), LSA_AS_ARGS (&task->dependency_lsa));
    }

  error = la_gate_order_push (&task->commit_lsa);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (la_Coordinator_config.worker_count == 1)
    {
      assert (la_pending_is_empty ());
      if (la_Coordinator_config.run_each_task_alone)
	{
	  return la_coordinator_run_alone (task);
	}
      return la_coordinator_enqueue_task (task, LA_SCHED_DISPATCHED);
    }

  error = la_coordinator_drain_ready ();
  if (error != NO_ERROR)
    {
      return error;
    }

  if (la_coordinator_merge_into_pending (task, &error))
    {
      return error;
    }

  dependency_ready = la_gate_is_satisfied (&task->dependency_lsa);
  if (dependency_ready)
    {
      task->ready_ns = la_coordinator_clock_ns ();
    }

  if (dependency_ready && !la_task_queue_is_full ())
    {
      return la_coordinator_enqueue_task (task, LA_SCHED_DISPATCHED);
    }

  if (!dependency_ready)
    {
      task->pending_begin_ns = la_coordinator_clock_ns ();
    }

  error = la_pending_push (task);
  if (error == NO_ERROR && task->has_items && task->rectype == LOG_COMMIT)
    {
      la_stat_count_dispatch_parked ();
    }
  la_coordinator_trace_schedule (task, LA_SCHED_PARKED, UINT64_MAX);

  return error;
}
