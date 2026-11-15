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
 * test_log_applier_coordinator.cpp - coordinator scheduling without a server connection
 */

#include "catch2/catch.hpp"

#include "test_log_applier_common.hpp"
#include "log_applier_coordinator.hpp"
#include "log_applier_gate.hpp"
#include "log_applier_pending.hpp"
#include "log_applier_result_queue.hpp"
#include "log_applier_task_queue.hpp"
#include "scope_exit.hpp"

#include "error_code.h"
#include "memory_alloc.h"

#include <atomic>
#include <cstdlib>
#include <thread>

typedef struct test_coordinator_context TEST_COORDINATOR_CONTEXT;
struct test_coordinator_context
{
  int dispatched;
  int retired;
  int stored;
  bool shutdown;
  LOG_LSA frontier;
  int commit_order_tranid;
  int retired_before_commit_order_dispatch;
  int dispatch_error;
  int discarded;
  int store_error;
  int shutdown_queue_tranid;
};

static int
test_before_dispatch (void *arg, const LA_TASK *task)
{
  TEST_COORDINATOR_CONTEXT *context = (TEST_COORDINATOR_CONTEXT *) arg;

  REQUIRE (task != NULL);
  if (task->tranid == context->commit_order_tranid)
    {
      context->retired_before_commit_order_dispatch = context->retired;
    }
  context->dispatched++;
  if (task->tranid == context->shutdown_queue_tranid)
    {
      la_task_queue_signal_shutdown ();
    }

  return context->dispatch_error;
}

static void
test_retire_entry (void *arg, LA_INFLIGHT_ENTRY *entry, const LOG_LSA *frontier)
{
  TEST_COORDINATOR_CONTEXT *context = (TEST_COORDINATOR_CONTEXT *) arg;

  REQUIRE (entry != NULL);
  REQUIRE (frontier != NULL);
  context->retired++;
  LSA_COPY (&context->frontier, frontier);
}

static int
test_store_progress (void *arg)
{
  TEST_COORDINATOR_CONTEXT *context = (TEST_COORDINATOR_CONTEXT *) arg;

  context->stored++;

  return context->store_error;
}

static bool
test_shutdown_requested (void *arg)
{
  return ((TEST_COORDINATOR_CONTEXT *) arg)->shutdown;
}

static void
test_discard_task (void *arg, LA_TASK *task)
{
  TEST_COORDINATOR_CONTEXT *context = (TEST_COORDINATOR_CONTEXT *) arg;
  context->discarded++;
  free_and_init (task->apply);
}

static void
test_initialize_coordinator (TEST_COORDINATOR_CONTEXT *context, const LOG_LSA *restart_lsa)
{
  LA_COORDINATOR_CONFIG config;

  memset (context, 0, sizeof (*context));
  LSA_SET_NULL (&context->frontier);
  la_pending_clear ();

  config.commit_order_capacity = 8;
  config.worker_count = 2;
  config.run_each_task_alone = false;
  config.restart_lsa = restart_lsa;
  config.before_dispatch = test_before_dispatch;
  config.retire_entry = test_retire_entry;
  config.store_progress = test_store_progress;
  config.shutdown_requested = test_shutdown_requested;
  config.merge_task = NULL;
  config.discard_task = test_discard_task;
  config.context = context;

  REQUIRE (la_coordinator_initialize (&config) == NO_ERROR);
}

/* Return queue results without a server connection. failed_tranid selects an apply failure. */
static void
test_complete_tasks (int count, int failed_tranid, std::atomic<int> &worker_error)
{
  for (int i = 0; i < count; i++)
    {
      LA_TASK task;
      int error = la_task_queue_pop (&task);
      if (error != NO_ERROR)
	{
	  worker_error.store (error);
	  return;
	}

      LA_RESULT result;
      la_result_init (&result, &task, 0);
      result.error = task.tranid == failed_tranid ? ER_FAILED : NO_ERROR;
      result.apply_end_ns = task.queue_enter_ns;
      error = la_result_queue_push (&result);
      if (error != NO_ERROR)
	{
	  worker_error.store (error);
	  return;
	}
    }
}

TEST_CASE ("coordinator releases a dependent task after its predecessor completes", "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LOG_LSA null_lsa = test_null_lsa ();
  LA_TASK first = test_task (1, test_lsa (20), null_lsa);
  LA_TASK second = test_task (2, test_lsa (30), first.commit_lsa);
  LA_TASK queued;
  LA_RESULT result;

  test_initialize_coordinator (&context, &restart_lsa);

  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (context.dispatched == 1);
  REQUIRE_FALSE (la_pending_is_empty ());

  REQUIRE (la_task_queue_pop (&queued) == NO_ERROR);
  REQUIRE (queued.tranid == first.tranid);
  la_result_init (&result, &queued, 0);
  result.error = NO_ERROR;
  result.apply_end_ns = queued.queue_enter_ns;
  REQUIRE (la_result_queue_push (&result) == NO_ERROR);

  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 1);
  REQUIRE (context.stored == 1);
  REQUIRE (context.dispatched == 2);
  REQUIRE (la_pending_is_empty ());

  REQUIRE (la_task_queue_pop (&queued) == NO_ERROR);
  REQUIRE (queued.tranid == second.tranid);
  la_result_init (&result, &queued, 1);
  result.error = NO_ERROR;
  result.apply_end_ns = queued.queue_enter_ns;
  REQUIRE (la_result_queue_push (&result) == NO_ERROR);

  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 2);
  REQUIRE (context.stored == 2);
  REQUIRE (LSA_EQ (&context.frontier, &second.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}

TEST_CASE ("commit-order submission retires preceding and pending tasks before dispatch and waits for its result",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK dependent = test_task (2, test_lsa (30), first.commit_lsa);
  LA_TASK fragment = test_task (3, test_lsa (40), test_null_lsa ());
  LA_TASK later = test_task (4, test_lsa (50), test_null_lsa ());
  fragment.rectype = TEST_RECTYPE_SYSOP_END;

  test_initialize_coordinator (&context, &restart_lsa);
  context.commit_order_tranid = fragment.tranid;
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE_FALSE (la_pending_is_empty ());

  std::atomic<int> worker_error { NO_ERROR };
  std::thread worker ([&] { test_complete_tasks (4, 0, worker_error); });
  scope_exit cleanup
  {
    [&]
    {
      la_task_queue_signal_shutdown ();
      la_result_queue_signal_shutdown ();
      if (worker.joinable ())
	{
	  worker.join ();
	}
    }
  };

  REQUIRE (la_coordinator_submit_task (&fragment, true) == NO_ERROR);
  REQUIRE (context.retired_before_commit_order_dispatch == 2);
  REQUIRE (context.retired == 3);
  REQUIRE (context.dispatched == 3);
  REQUIRE (la_pending_is_empty ());
  REQUIRE (LSA_EQ (&context.frontier, &fragment.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());

  REQUIRE (la_coordinator_submit_task (&later) == NO_ERROR);
  worker.join ();
  REQUIRE (worker_error.load () == NO_ERROR);
  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 4);
  REQUIRE (LSA_EQ (&context.frontier, &later.commit_lsa));
}

TEST_CASE ("commit-order submission returns apply errors without completing the fragment",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK fragment = test_task (1, test_lsa (20), test_null_lsa ());
  fragment.rectype = TEST_RECTYPE_SYSOP_END;
  test_initialize_coordinator (&context, &restart_lsa);

  std::atomic<int> worker_error { NO_ERROR };
  std::thread worker ([&] { test_complete_tasks (1, fragment.tranid, worker_error); });
  scope_exit cleanup
  {
    [&]
    {
      la_task_queue_signal_shutdown ();
      la_result_queue_signal_shutdown ();
      worker.join ();
    }
  };

  REQUIRE (la_coordinator_submit_task (&fragment, true) == ER_FAILED);
  REQUIRE (worker_error.load () == NO_ERROR);
  REQUIRE (context.retired == 0);
  REQUIRE (context.stored == 0);
  REQUIRE_FALSE (la_coordinator_drain_complete ());

  LOG_LSA frontier;
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &restart_lsa));
}

TEST_CASE ("submission transfers apply-slot ownership at dispatch and preserves it on pre-dispatch failure",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK task = test_task (1, test_lsa (20), test_null_lsa ());
  /* The coordinator treats an apply slot as opaque; only the legacy callbacks access its fields. */
  LA_APPLY *apply = reinterpret_cast<LA_APPLY *> (&context);
  task.apply = apply;
  test_initialize_coordinator (&context, &restart_lsa);
  /* A merge attempt can reserve an array before deciding not to add a fragment. */
  LOG_LSA *merged = static_cast<LOG_LSA *> (malloc (sizeof (LOG_LSA)));
  REQUIRE (merged != NULL);
  task.merged_lsa = merged;
  task.merged_capacity = 1;
  scope_exit cleanup
  {
    [&]
    {
      free_and_init (task.merged_lsa);
      la_inflight_clear ();
      (void) la_inflight_init (8);
    }
  };

  SECTION ("a failure before dispatch keeps the caller's ownership")
  {
    context.dispatch_error = ER_FAILED;
    REQUIRE (la_coordinator_submit_task (&task, true) == ER_FAILED);
    REQUIRE (task.apply == apply);
    REQUIRE (task.merged_lsa == merged);
    REQUIRE (task.merged_capacity == 1);
    REQUIRE (context.retired == 0);
  }

  SECTION ("the in-flight entry owns a dispatched slot")
  {
    REQUIRE (la_coordinator_submit_task (&task) == NO_ERROR);
    REQUIRE (task.apply == NULL);
    REQUIRE (task.merged_lsa == NULL);
    REQUIRE (task.merged_count == 0);
    REQUIRE (task.merged_capacity == 0);

    LA_TASK queued;
    REQUIRE (la_task_queue_pop (&queued) == NO_ERROR);
    REQUIRE (queued.apply == apply);
    REQUIRE (queued.merged_lsa == merged);

    LA_RESULT result;
    la_result_init (&result, &queued, 0);
    result.error = NO_ERROR;
    result.apply_end_ns = queued.queue_enter_ns;
    REQUIRE (la_result_queue_push (&result) == NO_ERROR);
    REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
    REQUIRE (context.retired == 1);
    REQUIRE (context.discarded == 0);
    REQUIRE (la_coordinator_drain_complete ());
  }
}

TEST_CASE ("drain closes an out-of-order gap and dispatches every generation of pending work",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK second = test_task (2, test_lsa (30), first.commit_lsa);
  LA_TASK third = test_task (3, test_lsa (40), second.commit_lsa);
  LA_TASK independent = test_task (4, test_lsa (50), test_null_lsa ());
  LA_TASK queued_first, queued_later;
  LA_RESULT result;

  test_initialize_coordinator (&context, &restart_lsa);
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&third) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&independent) == NO_ERROR);
  REQUIRE (la_pending_get_count () == 2);
  REQUIRE (la_task_queue_pop (&queued_first) == NO_ERROR);
  REQUIRE (la_task_queue_pop (&queued_later) == NO_ERROR);
  REQUIRE (queued_later.tranid == independent.tranid);

  /* A completed later commit must not release either dependent through the gap. */
  la_result_init (&result, &queued_later, 1);
  REQUIRE (la_result_queue_push (&result) == NO_ERROR);
  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 0);
  REQUIRE (context.dispatched == 2);
  REQUIRE (la_pending_get_count () == 2);

  la_result_init (&result, &queued_first, 0);
  REQUIRE (la_result_queue_push (&result) == NO_ERROR);
  std::atomic<int> worker_error { NO_ERROR };
  std::thread worker ([&] { test_complete_tasks (2, 0, worker_error); });
  scope_exit cleanup
  {
    [&]
    {
      la_task_queue_signal_shutdown ();
      la_result_queue_signal_shutdown ();
      worker.join ();
    }
  };

  REQUIRE (la_coordinator_drain () == NO_ERROR);
  REQUIRE (worker_error.load () == NO_ERROR);
  REQUIRE (context.dispatched == 4);
  REQUIRE (context.retired == 4);
  REQUIRE (context.stored == 4);
  REQUIRE (LSA_EQ (&context.frontier, &independent.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}

TEST_CASE ("drain propagates errors instead of reporting an unfinished stream drained",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK dependent = test_task (2, test_lsa (30), first.commit_lsa);
  LA_TASK queued;
  LA_RESULT result;

  test_initialize_coordinator (&context, &restart_lsa);
  scope_exit cleanup
  {
    []
    {
      la_pending_clear ();
      la_inflight_clear ();
    }
  };
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE (la_task_queue_pop (&queued) == NO_ERROR);

  SECTION ("a failed worker leaves the gap and its dependent unfinished")
  {
    la_result_init (&result, &queued, 0);
    result.error = ER_FAILED;
    REQUIRE (la_result_queue_push (&result) == NO_ERROR);
    REQUIRE (la_coordinator_drain () == ER_FAILED);
    REQUIRE (context.retired == 0);
    REQUIRE (context.stored == 0);
    REQUIRE (context.dispatched == 1);
    REQUIRE (la_pending_get_count () == 1);
    LOG_LSA frontier;
    REQUIRE (la_gate_get_frontier (&frontier));
    REQUIRE (LSA_EQ (&frontier, &restart_lsa));
  }

  SECTION ("shutdown interrupts the wait for the missing worker result")
  {
    context.shutdown = true;
    REQUIRE (la_coordinator_drain () == ER_FAILED);
    REQUIRE (context.retired == 0);
    REQUIRE (context.dispatched == 1);
    REQUIRE (la_pending_get_count () == 1);
  }

  SECTION ("a progress-store error is returned before pending work is dispatched")
  {
    context.store_error = ER_FAILED;
    la_result_init (&result, &queued, 0);
    REQUIRE (la_result_queue_push (&result) == NO_ERROR);
    REQUIRE (la_coordinator_drain () == ER_FAILED);
    REQUIRE (context.retired == 1);
    REQUIRE (context.stored == 1);
    REQUIRE (context.dispatched == 1);
    REQUIRE (la_pending_get_count () == 1);
  }

  REQUIRE_FALSE (la_coordinator_drain_complete ());
}

TEST_CASE ("released pending tasks discard only resources not transferred to in-flight",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK pending = test_task (2, test_lsa (30), first.commit_lsa);
  LA_TASK queued;
  LA_RESULT result;

  test_initialize_coordinator (&context, &restart_lsa);
  scope_exit cleanup
  {
    []
    {
      la_pending_clear ();
      /* Shutdown frees apply slots separately from the in-flight merged arrays. */
      for (int i = 0; i < la_inflight_get_count (); i++)
	{
	  LA_APPLY *apply = la_inflight_entry_take_apply (la_inflight_entry_at (i));
	  free_and_init (apply);
	}
      la_inflight_clear ();
      /* Remove stale copies so LSan can detect an unfreed merged allocation. */
      (void) la_inflight_init (8);
    }
  };
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_task_queue_pop (&queued) == NO_ERROR);
  pending.apply = reinterpret_cast<LA_APPLY *> (malloc (1));
  REQUIRE (pending.apply != NULL);
  pending.merged_lsa = static_cast<LOG_LSA *> (malloc (sizeof (LOG_LSA)));
  REQUIRE (pending.merged_lsa != NULL);
  pending.merged_lsa[0] = test_lsa (40);
  pending.merged_count = pending.merged_capacity = 1;
  REQUIRE (la_coordinator_submit_task (&pending) == NO_ERROR);
  REQUIRE (la_pending_get_count () == 1);
  /* The pending-list copy now owns both allocations. */
  pending.apply = NULL;
  pending.merged_lsa = NULL;

  SECTION ("failure before dispatch discards the popped task exactly once")
  {
    context.dispatch_error = ER_FAILED;
    la_result_init (&result, &queued, 0);
    REQUIRE (la_result_queue_push (&result) == NO_ERROR);
    REQUIRE (la_coordinator_collect_and_retire () == ER_FAILED);
    REQUIRE (context.discarded == 1);
    REQUIRE (la_pending_is_empty ());
    REQUIRE (la_inflight_get_count () == 1);
  }

  SECTION ("queue failure after in-flight registration leaves shutdown as the sole owner")
  {
    context.shutdown_queue_tranid = pending.tranid;
    la_result_init (&result, &queued, 0);
    REQUIRE (la_result_queue_push (&result) == NO_ERROR);
    REQUIRE (la_coordinator_collect_and_retire () == ER_FAILED);
    REQUIRE (context.discarded == 0);
    REQUIRE (la_pending_is_empty ());
    REQUIRE (la_inflight_get_count () == 2);
    LA_INFLIGHT_ENTRY *entry = la_inflight_entry_at (1);
    REQUIRE (la_inflight_entry_get_tranid (entry) == pending.tranid);
    REQUIRE (la_inflight_entry_get_apply (entry) != NULL);
    REQUIRE_FALSE (la_inflight_entry_has_result (entry));
  }
}

TEST_CASE ("a taskless boundary releases dependents only after the preceding gap closes",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LOG_LSA empty_boundary = test_lsa (30);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK dependent = test_task (2, test_lsa (40), empty_boundary);
  LA_TASK queued;
  LA_RESULT result;
  LOG_LSA frontier;

  test_initialize_coordinator (&context, &restart_lsa);
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_complete_without_task (&empty_boundary) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &restart_lsa));
  REQUIRE (context.dispatched == 1);
  REQUIRE (la_pending_get_count () == 1);

  REQUIRE (la_task_queue_pop (&queued) == NO_ERROR);
  la_result_init (&result, &queued, 0);
  REQUIRE (la_result_queue_push (&result) == NO_ERROR);
  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.dispatched == 2);
  REQUIRE (context.retired == 1);
  REQUIRE (la_pending_is_empty ());
  REQUIRE (LSA_EQ (&context.frontier, &empty_boundary));

  REQUIRE (la_task_queue_pop (&queued) == NO_ERROR);
  REQUIRE (queued.tranid == dependent.tranid);
  la_result_init (&result, &queued, 1);
  REQUIRE (la_result_queue_push (&result) == NO_ERROR);
  REQUIRE (la_coordinator_drain () == NO_ERROR);
  REQUIRE (context.retired == 2);
  REQUIRE (context.stored == 2);
  REQUIRE (LSA_EQ (&context.frontier, &dependent.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}
