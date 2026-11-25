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
#include "log_applier_clock.hpp"
#include "log_applier_debug.hpp"

#include "log_applier_gate.hpp"
#include "log_applier_pending.hpp"
#include "log_applier_result_queue.hpp"
#include "log_applier_stat.hpp"
#include "log_applier_task_queue.hpp"
#include "log_applier_worker.hpp"	/* LA_MAX_WORKER_COUNT */
#include "error_manager.h"
#include "memory_alloc.h"
#include "log_record.hpp"
#include "system_parameter.h"

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <atomic>

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

/* Tasks dispatched to each worker and not answered yet; a task whose predecessors are all applied goes to the
 * worker with the fewest of them (la_coordinator_route) */
static int la_Coordinator_outstanding[LA_MAX_WORKER_COUNT];

/* position in la_Coordinator_live_workers where the next search for a worker starts */
static int la_Coordinator_next_worker = 0;

/* Indexes of the workers that take tasks, in increasing order. Every task goes to one of them: a worker that
 * failed to start never pops its queue, so a task given to it would stall the gap-free frontier for good. */
static int la_Coordinator_live_workers[LA_MAX_WORKER_COUNT];
static int la_Coordinator_live_worker_count = 0;

/* What the last pass over the parked tasks saw. la_coordinator_pending_may_release compares it and skips the
 * rescan when nothing they wait for has changed, so that the coordinator does not spend its time rescanning the
 * whole list. */
/* false until the first pass, so there is nothing to compare with yet */
static bool la_Coordinator_pending_scan_valid = false;
/* gate completion version at the last pass; a change means a unit completed */
static UINT64 la_Coordinator_pending_seen_completion = 0;
/* gate dispatch version at the last pass; a change means a unit went to a worker */
static UINT64 la_Coordinator_pending_seen_dispatch = 0;
/* a task left in the last pass waits for an earlier unit to be dispatched */
static bool la_Coordinator_pending_waits_dispatch = false;
/* workers whose full queue left a task parked in the last pass, one bit per worker. Note that one bit per worker
 * in a UINT64 holds at most 64 workers; the static_assert below stops the build if the worker limit goes past it. */
static UINT64 la_Coordinator_pending_room_workers = 0;
static_assert (LA_MAX_WORKER_COUNT <= 64, "la_Coordinator_pending_room_workers keeps one bit per worker");

/* the bit for worker w in la_Coordinator_pending_room_workers */
#define LA_COORDINATOR_WORKER_BIT(w) (1ULL << (w))

/* A worker whose full queue parked a task is looked at again once its queue holds this many tasks or fewer: the queue
 * then has room for half a queue of tasks, so one pass can release many of them instead of rescanning every parked
 * task each time a single slot frees. Half a queue is still waiting in it, so the worker does not run dry. The
 * queue is counted, not the worker's unanswered tasks, which also include its uncommitted batch and would keep the
 * parked tasks waiting until the worker commits. */
#define LA_COORDINATOR_ROOM_RESCAN_QUEUED (LA_TASK_QUEUE_CAPACITY / 2)

/* Read by the worker threads through la_coordinator_keep_batch_open and la_coordinator_tasks_parked; written only
 * by the coordinator, through la_coordinator_set_hold_flag. */
static std::atomic<bool> la_Coordinator_log_drained (false);	/* read to the end of the copied log */
static std::atomic<bool> la_Coordinator_tasks_parked (false);	/* a task waits on the pending list */
static std::atomic<bool> la_Coordinator_waiting_results (false);	/* blocked until a worker reports */

/* how long the coordinator waits at a time for room in the only worker's queue before it checks for shutdown and
 * collects results */
#define LA_COORDINATOR_ROOM_WAIT_MSEC 100

static int la_coordinator_drain_ready (void);
static int la_coordinator_wait_for_commit_order_slot (void);
static int la_coordinator_enqueue_task (LA_TASK *task_in, LA_SCHED_STATUS sched_status, int worker);
static bool la_coordinator_route (const LA_TASK *task, int *worker);
static bool la_coordinator_queue_has_room (int worker);
static void la_coordinator_trace_schedule (const LA_TASK *task, LA_SCHED_STATUS sched_status, UINT64 dispatch_seq);
static int la_coordinator_collect_results (void);
static int la_coordinator_record_result (LA_RESULT *result);
static int la_coordinator_pop_result (LA_RESULT *result);
static void la_coordinator_set_hold_flag (std::atomic<bool> *flag, bool value);
static int la_coordinator_wait_until_retired (void);
static int la_coordinator_wait_for_inflight_slot (void);
static int la_coordinator_retire_results (void);
static int la_coordinator_run_alone (LA_TASK *task);

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
  assert (config->discard_task != NULL);

  la_Coordinator_config = *config;

  error = la_task_queue_init (config->worker_count);
  if (error != NO_ERROR)
    {
      return error;
    }
  la_pending_reset ();
  for (int i = 0; i < config->worker_count; i++)
    {
      la_Coordinator_outstanding[i] = 0;
      la_Coordinator_live_workers[i] = i;
    }
  la_Coordinator_live_worker_count = config->worker_count;
  la_Coordinator_next_worker = 0;
  la_Coordinator_pending_scan_valid = false;
  la_Coordinator_pending_room_workers = 0;
  la_Coordinator_log_drained.store (false);
  la_Coordinator_tasks_parked.store (false);
  la_Coordinator_waiting_results.store (false);

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
 * la_coordinator_signal_shutdown - wake every worker waiting on a coordinator queue so that it stops
 *
 * return: none
 *
 * Note: a worker waits for a task in its queue, or for room in the result queue; both see the shutdown flag
 *       once woken and leave their loop. Called before the workers are joined.
 */
void
la_coordinator_signal_shutdown (void)
{
  la_task_queue_signal_shutdown ();
  la_result_queue_signal_shutdown ();
}

/*
 * la_coordinator_finalize - release the coordinator's queues
 *
 * return: none
 *
 * Note: called after the workers are joined. Tasks still queued are dropped; they were never retired, so a
 *       restart reads them again.
 */
void
la_coordinator_finalize (void)
{
  la_task_queue_final ();
  la_result_queue_final ();
  la_Coordinator_initialized = false;
}

/*
 * la_coordinator_set_live_workers - limit dispatch to the workers that started
 *
 * return: none
 *
 *   live_workers(in): indexes of the workers that started, in increasing order
 *   live_worker_count(in): number of entries in live_workers; at least one
 *
 * Note: called once after the workers have started and before the first task is submitted; until then every
 *       configured worker is taken as live.
 */
void
la_coordinator_set_live_workers (const int *live_workers, int live_worker_count)
{
  assert (la_Coordinator_initialized);
  assert (live_workers != NULL);
  assert (live_worker_count > 0 && live_worker_count <= la_Coordinator_config.worker_count);

  for (int i = 0; i < live_worker_count; i++)
    {
      assert (live_workers[i] >= 0 && live_workers[i] < la_Coordinator_config.worker_count);
      la_Coordinator_live_workers[i] = live_workers[i];
    }
  la_Coordinator_live_worker_count = live_worker_count;
  la_Coordinator_next_worker = 0;
}

/*
 * la_coordinator_pending_may_release - whether a pass over the parked tasks may release one now
 *
 * return: true when something a parked task waits for may have changed since the last pass
 *
 * Note: a parked task is released by a completion, by the dispatch of an earlier task it waits for, or by room
 *       in a worker queue. The checks below, in order:
 *       1. No pass has run since the coordinator started, so there is nothing to compare with.
 *       2. A task waits for room in a worker queue, and that worker's queue has dropped to
 *          LA_COORDINATOR_ROOM_RESCAN_QUEUED tasks.
 *       3. A task completed since the last pass, so the frontier may have moved.
 *       4. A task waits for an earlier task to be dispatched, and a task was dispatched since the last pass.
 *          Dispatches happen on almost every call, so they count only while such a task waits.
 */
static bool
la_coordinator_pending_may_release (void)
{
  if (!la_Coordinator_pending_scan_valid)
    {
      return true;
    }

  for (int w = 0; la_Coordinator_pending_room_workers != 0 && w < la_Coordinator_config.worker_count; w++)
    {
      if ((la_Coordinator_pending_room_workers & LA_COORDINATOR_WORKER_BIT (w))
	  && la_task_queue_get_queued (w) <= LA_COORDINATOR_ROOM_RESCAN_QUEUED)
	{
	  return true;
	}
    }

  if (la_Coordinator_pending_seen_completion != la_gate_get_completion_version ())
    {
      return true;
    }

  if (la_Coordinator_pending_waits_dispatch
      && la_Coordinator_pending_seen_dispatch != la_gate_get_dispatch_version ())
    {
      return true;
    }

  return false;
}

/*
 * la_coordinator_drain_ready - dispatch the parked tasks that may run now
 *
 * return: NO_ERROR or an error code
 *
 * Note: a parked task runs now when its earlier units are all applied (any worker), or when the unfinished ones
 *       are all on one worker (that worker). Tasks are released in pending-list order.
 */
static int
la_coordinator_drain_ready (void)
{
  LA_TASK task;
  int worker;
  int error;

  /* la_coordinator_pending_may_release compares what the parked tasks wait for (completions, dispatches, queue
   * room) with what the last pass saw; no change, or no parked task at all, means a pass would release nothing */
  if (la_pending_is_empty () || !la_coordinator_pending_may_release ())
    {
      la_coordinator_set_hold_flag (&la_Coordinator_tasks_parked, !la_pending_is_empty ());
      return NO_ERROR;
    }

  la_pending_mark_ready (la_monotonic_now_ns ());

  while (true)
    {
      /* the route calls of this pass record what the tasks left parked wait for */
      la_Coordinator_pending_waits_dispatch = false;
      la_Coordinator_pending_room_workers = 0;
      if (!la_pending_pop_routable (&task, &worker, la_coordinator_route))
	{
	  /* nothing was dispatched in this pass, so the counters are the ones it scanned with */
	  la_Coordinator_pending_seen_completion = la_gate_get_completion_version ();
	  la_Coordinator_pending_seen_dispatch = la_gate_get_dispatch_version ();
	  la_Coordinator_pending_scan_valid = true;
	  break;
	}

      error = la_coordinator_enqueue_task (&task, LA_SCHED_RELEASED, worker);
      if (error != NO_ERROR)
	{
	  if (task.apply != NULL)
	    {
	      la_Coordinator_config.discard_task (la_Coordinator_config.context, &task);
	    }
	  return error;
	}
    }

  la_coordinator_set_hold_flag (&la_Coordinator_tasks_parked, !la_pending_is_empty ());

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
	  /* the frontier moves only when the workers commit, so they must not hold their transactions open */
	  la_coordinator_note_waiting_results ();
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
 *   task_in(in/out): task to dispatch; apply is cleared when in-flight takes ownership
 *   sched_status(in): reason the task is being dispatched
 *   worker(in): the worker whose queue takes the task
 *
 * Note: the steps, in order:
 *       1. Lock the slave DB name through before_dispatch when the task applies rows not yet committed.
 *       2. Wait until the in-flight list has a free entry, retiring worker results meanwhile.
 *       3. Record the worker as the owner of the task on the commit-order ring.
 *       4. Push the task into the in-flight list, which takes its apply and gives the dispatch number.
 *       5. Count one more unanswered task of the worker and push the task into its queue.
 */
static int
la_coordinator_enqueue_task (LA_TASK *task_in, LA_SCHED_STATUS sched_status, int worker)
{
  LA_TASK task;
  UINT64 dispatch_seq = 0;
  int error;

  assert (task_in != NULL);

  task = *task_in;
  if (task.ready_ns == 0)
    {
      task.ready_ns = la_monotonic_now_ns ();
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

  /* record the owner on the ring, so a later unit waiting for this one can follow it to the same worker; before
   * the in-flight list takes the task, so a failure leaves the task with the caller */
  assert (worker >= 0 && worker < la_Coordinator_config.worker_count);
  error = la_gate_set_owner (&task.commit_lsa, worker);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* before the queue push, so every worker result has a matching dispatch number */
  error = la_inflight_push (task.apply, task.tranid, task.rectype, &dispatch_seq);
  if (error != NO_ERROR)
    {
      return error;
    }
  task_in->apply = NULL;
  task.dispatch_seq = dispatch_seq;

  if (sched_status == LA_SCHED_DISPATCHED && task.has_items && task.rectype == LOG_COMMIT)
    {
      la_stat_count_dispatch_direct ();
    }
  la_coordinator_trace_schedule (&task, sched_status, dispatch_seq);

  task.queue_enter_ns = la_monotonic_now_ns ();

  la_Coordinator_outstanding[worker]++;

  return la_task_queue_push (&task, worker);
}

/*
 * la_coordinator_queue_has_room - check whether one more task fits in a worker's queue
 *
 * return: true when the task queue of worker has room
 *
 *   worker(in): the worker
 *
 * Note: reads the queue's count without the lock the workers take tasks under. Only the coordinator pushes, so the
 *       count it reads is never below the real one, and room it sees is still there when it pushes.
 */
static bool
la_coordinator_queue_has_room (int worker)
{
  return la_task_queue_get_queued (worker) < LA_TASK_QUEUE_CAPACITY;
}

/*
 * la_coordinator_route - decide whether a task may be dispatched now, and to which worker
 *
 * return: true with *worker set when the task may be dispatched now
 *
 *   task(in): task to dispatch
 *   worker(out): the chosen worker
 *
 * Note:
 *   A task whose predecessors are all applied goes to the live worker with the fewest unanswered tasks whose
 *   queue has room. A task whose unfinished predecessors were all dispatched to one worker goes to that worker: it
 *   applies its queue in order on one connection, so the task runs after them, in the same open slave
 *   transaction or a later one. Otherwise the task waits. A waiting task does not ask the workers that hold its
 *   predecessors to commit early: they commit by their own batch limits, so independent tasks keep filling their
 *   batches.
 *
 *   Sending a task to another worker never breaks the order: while a predecessor is unfinished on worker A, the
 *   task can go only to A, and it may go elsewhere only after every predecessor is committed, so their changes
 *   are visible to any worker. What another worker costs is waiting: each worker applies in its own slave
 *   transaction, so a task on worker B cannot see A's uncommitted changes and has to wait until A commits by its
 *   own batch limits. Keeping a chain on one worker lets it continue in the same open transaction and commit in
 *   one batch.
 */
static bool
la_coordinator_route (const LA_TASK *task, int *worker)
{
  int live_worker_count = la_Coordinator_live_worker_count;
  int wait_reason;
  LA_GATE_ROUTE route;
  int chosen = -1;

  assert (task != NULL);
  assert (worker != NULL);
  /* with one worker, la_coordinator_submit_task dispatches every task directly and nothing is parked */
  assert (la_Coordinator_config.worker_count > 1);

  route = la_gate_route (&task->dependency_lsa, worker, &wait_reason);
  if (route == LA_GATE_ROUTE_WORKER)
    {
      if (!la_coordinator_queue_has_room (*worker))
	{
	  la_Coordinator_pending_room_workers |= LA_COORDINATOR_WORKER_BIT (*worker);
	  return false;
	}

      return true;
    }

  if (route == LA_GATE_ROUTE_WAIT)
    {
      LA_WSET_TRACE (2, "coordinator route wait trid=%d commit_lsa=%lld|%d type=%d dependency=%lld|%d reason=%d\n",
		     task->tranid, LSA_AS_ARGS (&task->commit_lsa), task->rectype, LSA_AS_ARGS (&task->dependency_lsa),
		     wait_reason);
      if (wait_reason & 1)
	{
	  la_Coordinator_pending_waits_dispatch = true;
	}

      return false;
    }

  /* least busy live worker with room; ties go round-robin */
  *worker = -1;
  for (int k = 0; k < live_worker_count; k++)
    {
      int pos = (la_Coordinator_next_worker + k) % live_worker_count;
      int i = la_Coordinator_live_workers[pos];

      if (!la_coordinator_queue_has_room (i))
	{
	  continue;
	}

      /* the first worker with room becomes the candidate; a later one replaces it only with fewer unanswered
       * tasks, so on a tie the worker met first from la_Coordinator_next_worker stays */
      if (*worker < 0 || la_Coordinator_outstanding[i] < la_Coordinator_outstanding[*worker])
	{
	  *worker = i;
	  chosen = pos;
	}
    }
  if (*worker < 0)
    {
      /* every live worker's queue is full */
      for (int k = 0; k < live_worker_count; k++)
	{
	  la_Coordinator_pending_room_workers |= LA_COORDINATOR_WORKER_BIT (la_Coordinator_live_workers[k]);
	}

      return false;
    }
  la_Coordinator_next_worker = (chosen + 1) % live_worker_count;

  return true;
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
 * la_coordinator_set_log_drained - record whether the coordinator has read to the end of the copied log
 *
 *   drained(in): true when no log record is left to read, false once a record is read again
 *
 * Note: called by the main loop where it notes the end of the log; read by the workers through
 *       la_coordinator_keep_batch_open.
 */
void
la_coordinator_set_log_drained (bool drained)
{
  la_coordinator_set_hold_flag (&la_Coordinator_log_drained, drained);
}

/*
 * la_coordinator_note_waiting_results - record that the coordinator dispatches nothing until a worker reports
 *
 * Note: called where the coordinator waits for results instead of reading the log (to run a task alone, for room on
 *       the commit-order ring or in the in-flight list, or for a configured replica delay). The workers then commit
 *       on an empty queue; the flag is cleared when the coordinator submits the next task, so a wait that ends on an
 *       error leaves it set only until then.
 */
void
la_coordinator_note_waiting_results (void)
{
  la_coordinator_set_hold_flag (&la_Coordinator_waiting_results, true);
}

/*
 * la_coordinator_set_hold_flag - set one of the flags that decide whether a worker may keep its transaction open
 *
 *   flag(in/out): la_Coordinator_log_drained, la_Coordinator_tasks_parked or la_Coordinator_waiting_results
 *   value(in): the new value
 *
 * Note: each flag set means the workers should commit on an empty queue. A worker already waiting on an empty queue
 *       decided before the flag was set, so the change from false to true wakes it (la_task_queue_wake_holding).
 */
static void
la_coordinator_set_hold_flag (std::atomic<bool> *flag, bool value)
{
  if (flag->exchange (value) == false && value)
    {
      la_task_queue_wake_holding ();
    }
}

/*
 * la_coordinator_keep_batch_open - tell a worker whether an empty queue may leave its transaction open
 *
 * return: true while the coordinator still has log to read, no task waits on the pending list and the coordinator
 *         is not waiting for a result
 *
 * Note: called by worker threads (LA_WORKER_CONFIG.keep_batch_open). While a backlog is read a queue that runs dry
 *       is about to be refilled, so committing then would only add a commit. At the end of the log the changes
 *       should show without waiting for the commit interval, a parked task may be waiting for this worker's commit,
 *       and a coordinator waiting for results (to run a task alone, or for room on the commit-order ring) dispatches
 *       nothing until the workers commit, so the worker commits on an empty queue in all three cases.
 */
bool
la_coordinator_keep_batch_open (void)
{
  return !la_Coordinator_log_drained.load () && !la_Coordinator_tasks_parked.load ()
    && !la_Coordinator_waiting_results.load ();
}

/*
 * la_coordinator_tasks_parked - tell a worker whether a task waits on the pending list
 *
 * return: true while the pending list is not empty
 *
 * Note: called by worker threads (LA_WORKER_CONFIG.tasks_parked). A parked task may wait for the commit of a worker
 *       whose queue never runs dry, so such a worker commits sooner while one is parked.
 */
bool
la_coordinator_tasks_parked (void)
{
  return la_Coordinator_tasks_parked.load ();
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
  assert (result->worker_idx >= 0 && result->worker_idx < la_Coordinator_config.worker_count);
  assert (la_Coordinator_outstanding[result->worker_idx] > 0);

  result->collected_ns = la_monotonic_now_ns ();

  error = la_inflight_attach_result (result);
  if (error != NO_ERROR)
    {
      return error;
    }

  la_stat_note_result_collected (result);

  la_Coordinator_outstanding[result->worker_idx]--;

  if (result->error == NO_ERROR && (result->rectype == LOG_COMMIT || result->rectype == LOG_SYSOP_END))
    {
      la_gate_mark_completed (&result->commit_lsa);
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

      /* nothing is dispatched until a result arrives, so a worker holding its transaction open would only delay it */
      la_coordinator_note_waiting_results ();
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
		     (unsigned long long) la_inflight_entry_get_dispatch_seq (entry));

      /* the call must stay outside assert(): a release build drops the whole assert expression */
      has_frontier = la_gate_get_frontier (&frontier);
      assert (has_frontier);
      la_Coordinator_config.retire_entry (la_Coordinator_config.context, entry, has_frontier ? &frontier : NULL);
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
 * la_coordinator_drain - dispatch pending tasks and wait for all accepted work to retire
 *
 * return: NO_ERROR or an error code
 */
int
la_coordinator_drain (void)
{
  int error;

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
  assert (task != NULL);

  int error = la_coordinator_drain ();
  if (error != NO_ERROR)
    {
      return error;
    }

  /* everything before it is retired, so any live worker may run it */
  error = la_coordinator_enqueue_task (task, LA_SCHED_DISPATCHED, la_Coordinator_live_workers[0]);
  if (error != NO_ERROR)
    {
      return error;
    }

  return la_coordinator_wait_until_retired ();
}

/*
 * la_coordinator_submit_task - register and schedule one assembled task
 *
 * return: NO_ERROR or an error code
 *
 *   task(in/out): task to dispatch now or retain until its dependency is complete
 *   commit_order(in): finish preceding work, then apply and retire this task before returning
 */
int
la_coordinator_submit_task (LA_TASK *task, bool commit_order)
{
  LOG_LSA dependency_lsa;
  bool dependency_ready;
  int worker;
  int error;

  assert (la_Coordinator_initialized);
  assert (task != NULL);

  task->created_ns = la_monotonic_now_ns ();

  /* the coordinator dispatches again, so the wait that raised the flag is over */
  if (la_Coordinator_waiting_results.load (std::memory_order_relaxed))
    {
      la_coordinator_set_hold_flag (&la_Coordinator_waiting_results, false);
    }

  error = la_coordinator_wait_for_commit_order_slot ();
  if (error != NO_ERROR)
    {
      return error;
    }

  la_gate_set_initial_frontier (la_Coordinator_config.restart_lsa);

  LSA_COPY (&dependency_lsa, &task->dependency_lsa);
  if (la_gate_bound_dependency (&task->dependency_lsa))
    {
      LA_WSET_TRACE (1, "coordinator label lowered trid=%d commit_lsa=%lld|%d label=%lld|%d "
		     "dependency=%lld|%d\n",
		     task->tranid, LSA_AS_ARGS (&task->commit_lsa),
		     LSA_AS_ARGS (&dependency_lsa), LSA_AS_ARGS (&task->dependency_lsa));
    }

  error = la_gate_order_push (&task->commit_lsa);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (commit_order)
    {
      return la_coordinator_run_alone (task);
    }

  if (la_Coordinator_config.worker_count == 1)
    {
      assert (la_pending_is_empty ());
      if (la_Coordinator_config.run_each_task_alone)
	{
	  return la_coordinator_run_alone (task);
	}
      /* the worker's queue is smaller than its share of the ring, which also holds its open transaction */
      while (!la_coordinator_queue_has_room (la_Coordinator_live_workers[0]))
	{
	  bool has_room;

	  error = la_task_queue_wait_room (la_Coordinator_live_workers[0], LA_COORDINATOR_ROOM_WAIT_MSEC, &has_room);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  if (has_room)
	    {
	      break;
	    }

	  /* a worker that stopped on an error frees no slot: its result or the shutdown request ends the wait */
	  if (la_Coordinator_config.shutdown_requested (la_Coordinator_config.context))
	    {
	      return ER_FAILED;
	    }

	  error = la_coordinator_collect_and_retire ();
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      return la_coordinator_enqueue_task (task, LA_SCHED_DISPATCHED, la_Coordinator_live_workers[0]);
    }

  error = la_coordinator_drain_ready ();
  if (error != NO_ERROR)
    {
      return error;
    }

  dependency_ready = la_gate_is_satisfied (&task->dependency_lsa);
  if (dependency_ready)
    {
      task->ready_ns = la_monotonic_now_ns ();
    }

  /* dispatch now when the dependency is satisfied or every unfinished predecessor is in one
   * worker's queue or open slave transaction */
  if (la_coordinator_route (task, &worker))
    {
      return la_coordinator_enqueue_task (task, LA_SCHED_DISPATCHED, worker);
    }

  if (!dependency_ready)
    {
      task->pending_begin_ns = la_monotonic_now_ns ();
    }

  error = la_pending_push (task);
  la_coordinator_set_hold_flag (&la_Coordinator_tasks_parked, !la_pending_is_empty ());
  if (error == NO_ERROR && task->has_items && task->rectype == LOG_COMMIT)
    {
      la_stat_count_dispatch_parked ();
    }
  la_coordinator_trace_schedule (task, LA_SCHED_PARKED, UINT64_MAX);

  return error;
}
