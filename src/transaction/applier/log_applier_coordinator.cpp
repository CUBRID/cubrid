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

/*
 * log_applier_coordinator.cpp - dispatch, collect and retire parallel apply tasks
 */

#include "config.h"

#include "log_applier_coordinator.hpp"

#include "log_applier_gate.hpp"
#include "log_applier_pending.hpp"
#include "log_applier_result_queue.hpp"
#include "log_applier_stat.hpp"
#include "log_applier_task_queue.hpp"
#include "error_manager.h"
#include "log_record.hpp"
#include "system_parameter.h"

#include <assert.h>
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

static UINT64
la_coordinator_clock_ns (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  return (UINT64) ts.tv_sec * 1000000000ULL + (UINT64) ts.tv_nsec;
}

/* Initialize the coordinator-owned scheduling state. The restart LSA is borrowed and read on first submit. */
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

/* Dispatch pending tasks whose dependency is now satisfied, preserving their pending order. */
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

/* Stop reading new commits while the order ring is full, but keep collecting worker results. */
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

/* Record a runnable task before enqueue so every worker result has a matching in-flight entry. */
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

static void
la_coordinator_trace_schedule (const LA_TASK *task, LA_SCHED_STATUS sched_status, UINT64 dispatch_seq)
{
  if (prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL) < 1)
    {
      return;
    }

  _er_log_debug (ARG_FILE_LINE,
		 "[WSET] reader schedule trid=%d commit_lsa=%lld|%d type=%d dependency=%lld|%d "
		 "status=%d dispatch=%llu\n",
		 task->tranid, (long long) task->commit_lsa.pageid, (int) task->commit_lsa.offset, task->rectype,
		 (long long) task->dependency_seq.pageid, (int) task->dependency_seq.offset, (int) sched_status,
		 (unsigned long long) dispatch_seq);
}

bool
la_coordinator_drain_complete (void)
{
  return (la_pending_is_empty () && la_inflight_get_count () == 0 && la_gate_order_get_count () == 0);
}

/* Collect completed worker results, advance the frontier and retire entries in dispatch order. */
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
    }

  return NO_ERROR;
}

/* Put a taskless SYSOP_END on the order ring so dependencies on that boundary can be released. */
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

  if (prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL) >= 1)
    {
      _er_log_debug (ARG_FILE_LINE, "[WSET] reader complete_without_task lsa=%lld|%d\n", (long long) lsa->pageid,
		     (int) lsa->offset);
    }

  return la_coordinator_drain_ready ();
}

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

/* Retire only the completed prefix; later results cannot release earlier transaction state. */
static int
la_coordinator_retire_results (void)
{
  LA_INFLIGHT_ENTRY *entry;
  LA_RESULT *result;
  LOG_LSA frontier;
  int error;

  while ((entry = la_inflight_peek ()) != NULL && entry->result_ready)
    {
      result = &entry->result;

      if (result->error != NO_ERROR)
	{
	  if (prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL) >= 1)
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "[WSET] coordinator result trid=%d commit_lsa=%lld|%d type=%d worker=%d error=%d\n",
			     result->tranid, (long long) result->commit_lsa.pageid,
			     (int) result->commit_lsa.offset, result->rectype, result->worker_idx, result->error);
	    }
	  return result->error;
	}

      if (prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL) >= 1)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "[WSET] coordinator retire trid=%d commit_lsa=%lld|%d type=%d worker=%d dispatch=%llu\n",
			 result->tranid, (long long) result->commit_lsa.pageid, (int) result->commit_lsa.offset,
			 result->rectype, result->worker_idx, (unsigned long long) entry->seq);
	}

      assert (la_gate_get_frontier (&frontier));
      la_Coordinator_config.retire_entry (la_Coordinator_config.context, entry, &frontier);
      la_inflight_pop ();

      error = la_Coordinator_config.store_progress (la_Coordinator_config.context);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

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

/* Register a completed log task and either dispatch it or retain it until its dependency is satisfied. */
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

  LSA_COPY (&label, &task->dependency_seq);
  if (la_gate_bound_dependency (&task->dependency_seq)
      && prm_get_integer_value (PRM_ID_HA_WRITESET_TRACE_LEVEL) >= 1)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "[WSET] coordinator label lowered trid=%d commit_lsa=%lld|%d label=%lld|%d "
		     "dependency=%lld|%d\n",
		     task->tranid, (long long) task->commit_lsa.pageid, (int) task->commit_lsa.offset,
		     (long long) label.pageid, (int) label.offset, (long long) task->dependency_seq.pageid,
		     (int) task->dependency_seq.offset);
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

  dependency_ready = la_gate_is_satisfied (&task->dependency_seq);
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
