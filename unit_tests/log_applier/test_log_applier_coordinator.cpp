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

/* number of worker queues of the coordinator under test; the fake workers below look at every one */
static int test_Worker_count = 0;

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
test_initialize_coordinator (TEST_COORDINATOR_CONTEXT *context, const LOG_LSA *restart_lsa, int worker_count = 2)
{
  LA_COORDINATOR_CONFIG config;

  memset (context, 0, sizeof (*context));
  LSA_SET_NULL (&context->frontier);
  la_pending_clear ();

  config.commit_order_capacity = 8;
  config.worker_count = worker_count;
  config.run_each_task_alone = false;
  config.restart_lsa = restart_lsa;
  config.before_dispatch = test_before_dispatch;
  config.retire_entry = test_retire_entry;
  config.store_progress = test_store_progress;
  config.shutdown_requested = test_shutdown_requested;
  config.discard_task = test_discard_task;
  config.context = context;

  REQUIRE (la_coordinator_initialize (&config) == NO_ERROR);
  test_Worker_count = worker_count;
}

/* Take the next task from whichever worker queue has one, as some worker would; fails on queue shutdown. */
static int
test_pop_any (LA_TASK *task, int *worker)
{
  bool taken;
  int error;

  while (true)
    {
      for (int w = 0; w < test_Worker_count; w++)
	{
	  error = la_task_queue_try_pop (task, &taken, w, 1000 * 1000ULL);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  if (taken)
	    {
	      *worker = w;
	      return NO_ERROR;
	    }
	}
    }
}

/* Answer a task as the worker that took it, without applying anything. */
static void
test_push_result (const LA_TASK *task, int worker, int error = NO_ERROR)
{
  LA_RESULT result;

  la_result_init (&result, task, worker);
  result.error = error;
  result.apply_end_ns = task->queue_enter_ns;
  REQUIRE (la_result_queue_push (&result) == NO_ERROR);
}

/* Return queue results without a server connection. failed_tranid selects an apply failure. */
static void
test_complete_tasks (int count, int failed_tranid, std::atomic<int> &worker_error)
{
  for (int i = 0; i < count; i++)
    {
      LA_TASK task;
      int worker;
      int error = test_pop_any (&task, &worker);
      if (error != NO_ERROR)
	{
	  worker_error.store (error);
	  return;
	}

      LA_RESULT result;
      la_result_init (&result, &task, worker);
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

/* whether a worker's queue is empty right now; takes the task when it is not */
static bool
test_queue_empty (int worker)
{
  LA_TASK task;
  bool taken;

  REQUIRE (la_task_queue_try_pop (&task, &taken, worker, 0) == NO_ERROR);
  return !taken;
}

TEST_CASE ("coordinator spreads independent tasks over the workers and keeps a dependent on its predecessor's worker",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK dependent = test_task (2, test_lsa (30), first.commit_lsa);
  LA_TASK independent = test_task (3, test_lsa (40), test_null_lsa ());
  LA_TASK queued;

  test_initialize_coordinator (&context, &restart_lsa);

  /* worker 1 is idle, yet the dependent goes after its unfinished predecessor on worker 0 */
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&independent) == NO_ERROR);
  REQUIRE (context.dispatched == 3);
  REQUIRE (la_pending_is_empty ());

  REQUIRE (la_task_queue_pop (&queued, 0) == NO_ERROR);
  REQUIRE (queued.tranid == first.tranid);
  test_push_result (&queued, 0);
  REQUIRE (la_task_queue_pop (&queued, 0) == NO_ERROR);
  REQUIRE (queued.tranid == dependent.tranid);
  test_push_result (&queued, 0);
  REQUIRE (la_task_queue_pop (&queued, 1) == NO_ERROR);
  REQUIRE (queued.tranid == independent.tranid);
  test_push_result (&queued, 1);

  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 3);
  REQUIRE (LSA_EQ (&context.frontier, &independent.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}

TEST_CASE ("coordinator dispatches only to the live workers", "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  int live[] = { 0, 2 };
  int taken[3] = { 0, 0, 0 };

  test_initialize_coordinator (&context, &restart_lsa, 3);
  scope_exit cleanup
  {
    []
    {
      la_pending_clear ();
      la_inflight_clear ();
    }
  };

  /* worker 1 failed to start and never pops its queue */
  la_coordinator_set_live_workers (live, 2);
  for (int t = 0; t < 4; t++)
    {
      LA_TASK task = test_task (t + 1, test_lsa (20 + t * 10), test_null_lsa ());
      REQUIRE (la_coordinator_submit_task (&task) == NO_ERROR);
    }
  REQUIRE (context.dispatched == 4);

  for (int w = 0; w < 3; w++)
    {
      while (!test_queue_empty (w))
	{
	  taken[w]++;
	}
    }
  REQUIRE (taken[0] == 2);
  REQUIRE (taken[1] == 0);
  REQUIRE (taken[2] == 2);
}

TEST_CASE ("coordinator parks a task whose predecessors are on several workers and releases it to the one left",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK second = test_task (2, test_lsa (30), test_null_lsa ());
  LA_TASK dependent = test_task (3, test_lsa (40), second.commit_lsa);
  LA_TASK queued;

  test_initialize_coordinator (&context, &restart_lsa);

  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE (context.dispatched == 2);
  REQUIRE (la_pending_get_count () == 1);

  REQUIRE (la_task_queue_pop (&queued, 0) == NO_ERROR);
  REQUIRE (queued.tranid == first.tranid);
  test_push_result (&queued, 0);

  /* only worker 1 still holds an unfinished predecessor, so the task follows it there */
  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 1);
  REQUIRE (context.stored == 1);
  REQUIRE (context.dispatched == 3);
  REQUIRE (la_pending_is_empty ());

  REQUIRE (la_task_queue_pop (&queued, 1) == NO_ERROR);
  REQUIRE (queued.tranid == second.tranid);
  test_push_result (&queued, 1);
  REQUIRE (la_task_queue_pop (&queued, 1) == NO_ERROR);
  REQUIRE (queued.tranid == dependent.tranid);
  test_push_result (&queued, 1);

  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 3);
  REQUIRE (context.stored == 3);
  REQUIRE (LSA_EQ (&context.frontier, &dependent.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}

TEST_CASE ("commit-order submission retires preceding and pending tasks before dispatch and waits for its result",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK second = test_task (2, test_lsa (25), test_null_lsa ());
  LA_TASK dependent = test_task (3, test_lsa (30), second.commit_lsa);
  LA_TASK fragment = test_task (4, test_lsa (40), test_null_lsa ());
  LA_TASK later = test_task (5, test_lsa (50), test_null_lsa ());
  fragment.rectype = TEST_RECTYPE_SYSOP_END;

  test_initialize_coordinator (&context, &restart_lsa);
  context.commit_order_tranid = fragment.tranid;
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE_FALSE (la_pending_is_empty ());

  std::atomic<int> worker_error { NO_ERROR };
  std::thread worker ([&] { test_complete_tasks (5, 0, worker_error); });
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
  REQUIRE (context.retired_before_commit_order_dispatch == 3);
  REQUIRE (context.retired == 4);
  REQUIRE (context.dispatched == 4);
  REQUIRE (la_pending_is_empty ());
  REQUIRE (LSA_EQ (&context.frontier, &fragment.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());

  REQUIRE (la_coordinator_submit_task (&later) == NO_ERROR);
  worker.join ();
  REQUIRE (worker_error.load () == NO_ERROR);
  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 5);
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
  scope_exit cleanup
  {
    []
    {
      la_inflight_clear ();
      (void) la_inflight_init (8);
    }
  };

  SECTION ("a failure before dispatch keeps the caller's ownership")
  {
    context.dispatch_error = ER_FAILED;
    REQUIRE (la_coordinator_submit_task (&task, true) == ER_FAILED);
    REQUIRE (task.apply == apply);
    REQUIRE (context.retired == 0);
  }

  SECTION ("the in-flight entry owns a dispatched slot")
  {
    REQUIRE (la_coordinator_submit_task (&task) == NO_ERROR);
    REQUIRE (task.apply == NULL);

    LA_TASK queued;
    REQUIRE (la_task_queue_pop (&queued, 0) == NO_ERROR);
    REQUIRE (queued.apply == apply);

    test_push_result (&queued, 0);
    REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
    REQUIRE (context.retired == 1);
    REQUIRE (context.discarded == 0);
    REQUIRE (la_coordinator_drain_complete ());
  }
}

TEST_CASE ("an out-of-order completion releases dependents only to the worker holding the gap, and drain retires all",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK second = test_task (2, test_lsa (30), test_null_lsa ());
  LA_TASK third = test_task (3, test_lsa (40), second.commit_lsa);
  LA_TASK fourth = test_task (4, test_lsa (50), third.commit_lsa);
  LA_TASK queued_second;

  test_initialize_coordinator (&context, &restart_lsa);
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  /* the third waits for units on both workers, the fourth for the third, which has no worker yet */
  REQUIRE (la_coordinator_submit_task (&third) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&fourth) == NO_ERROR);
  REQUIRE (context.dispatched == 2);
  REQUIRE (la_pending_get_count () == 2);

  /* The later commit finishes first. The frontier stays behind the first task, so nothing retires, and both
   * parked tasks may run only after it, on worker 0. */
  REQUIRE (la_task_queue_pop (&queued_second, 1) == NO_ERROR);
  REQUIRE (queued_second.tranid == second.tranid);
  test_push_result (&queued_second, 1);
  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.retired == 0);
  REQUIRE (context.dispatched == 4);
  REQUIRE (la_pending_is_empty ());
  REQUIRE (test_queue_empty (1));

  std::atomic<int> worker_error { NO_ERROR };
  std::thread worker ([&] { test_complete_tasks (3, 0, worker_error); });
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
  REQUIRE (context.retired == 4);
  REQUIRE (context.stored == 4);
  REQUIRE (LSA_EQ (&context.frontier, &fourth.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}

TEST_CASE ("drain dispatches every generation of pending work once their predecessors retire",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK second = test_task (2, test_lsa (30), test_null_lsa ());
  LA_TASK third = test_task (3, test_lsa (40), second.commit_lsa);
  LA_TASK fourth = test_task (4, test_lsa (50), third.commit_lsa);

  test_initialize_coordinator (&context, &restart_lsa);
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&third) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&fourth) == NO_ERROR);
  REQUIRE (la_pending_get_count () == 2);

  std::atomic<int> worker_error { NO_ERROR };
  std::thread worker ([&] { test_complete_tasks (4, 0, worker_error); });
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
  REQUIRE (LSA_EQ (&context.frontier, &fourth.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}

TEST_CASE ("drain propagates errors instead of reporting an unfinished stream drained",
	   "[log_applier_coordinator]")
{
  TEST_COORDINATOR_CONTEXT context;
  LOG_LSA restart_lsa = test_lsa (10);
  LA_TASK first = test_task (1, test_lsa (20), test_null_lsa ());
  LA_TASK second = test_task (2, test_lsa (25), test_null_lsa ());
  LA_TASK dependent = test_task (3, test_lsa (30), second.commit_lsa);
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
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE (la_pending_get_count () == 1);
  REQUIRE (la_task_queue_pop (&queued, 0) == NO_ERROR);
  REQUIRE (queued.tranid == first.tranid);

  SECTION ("a failed worker leaves the gap and its dependent unfinished")
  {
    test_push_result (&queued, 0, ER_FAILED);
    REQUIRE (la_coordinator_drain () == ER_FAILED);
    REQUIRE (context.retired == 0);
    REQUIRE (context.stored == 0);
    REQUIRE (context.dispatched == 2);
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
    REQUIRE (context.dispatched == 2);
    REQUIRE (la_pending_get_count () == 1);
  }

  SECTION ("a progress-store error is returned before pending work is dispatched")
  {
    context.store_error = ER_FAILED;
    test_push_result (&queued, 0);
    REQUIRE (la_coordinator_drain () == ER_FAILED);
    REQUIRE (context.retired == 1);
    REQUIRE (context.stored == 1);
    REQUIRE (context.dispatched == 2);
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
  LA_TASK second = test_task (2, test_lsa (25), test_null_lsa ());
  LA_TASK pending = test_task (3, test_lsa (30), second.commit_lsa);
  LA_TASK queued;

  test_initialize_coordinator (&context, &restart_lsa);
  scope_exit cleanup
  {
    []
    {
      la_pending_clear ();
      /* shutdown frees the apply slots the in-flight entries own */
      for (int i = 0; i < la_inflight_get_count (); i++)
	{
	  LA_APPLY *apply = la_inflight_entry_take_apply (la_inflight_entry_at (i));
	  free_and_init (apply);
	}
      la_inflight_clear ();
    }
  };
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (la_task_queue_pop (&queued, 0) == NO_ERROR);
  REQUIRE (queued.tranid == first.tranid);
  pending.apply = reinterpret_cast<LA_APPLY *> (malloc (1));
  REQUIRE (pending.apply != NULL);
  REQUIRE (la_coordinator_submit_task (&pending) == NO_ERROR);
  REQUIRE (la_pending_get_count () == 1);
  /* the pending-list copy now owns the slot */
  pending.apply = NULL;

  SECTION ("failure before dispatch discards the popped task exactly once")
  {
    context.dispatch_error = ER_FAILED;
    test_push_result (&queued, 0);
    REQUIRE (la_coordinator_collect_and_retire () == ER_FAILED);
    REQUIRE (context.discarded == 1);
    REQUIRE (la_pending_is_empty ());
    REQUIRE (la_inflight_get_count () == 2);
  }

  SECTION ("queue failure after in-flight registration leaves shutdown as the sole owner")
  {
    context.shutdown_queue_tranid = pending.tranid;
    test_push_result (&queued, 0);
    REQUIRE (la_coordinator_collect_and_retire () == ER_FAILED);
    REQUIRE (context.discarded == 0);
    REQUIRE (la_pending_is_empty ());
    REQUIRE (la_inflight_get_count () == 3);
    LA_INFLIGHT_ENTRY *entry = la_inflight_entry_at (2);
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
  LA_TASK second = test_task (2, test_lsa (25), test_null_lsa ());
  LA_TASK dependent = test_task (3, test_lsa (40), empty_boundary);
  LA_TASK queued;
  int worker;
  LOG_LSA frontier;

  test_initialize_coordinator (&context, &restart_lsa);
  REQUIRE (la_coordinator_submit_task (&first) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&second) == NO_ERROR);
  REQUIRE (la_coordinator_complete_without_task (&empty_boundary) == NO_ERROR);
  REQUIRE (la_coordinator_submit_task (&dependent) == NO_ERROR);
  REQUIRE (la_gate_get_frontier (&frontier));
  REQUIRE (LSA_EQ (&frontier, &restart_lsa));
  REQUIRE (context.dispatched == 2);
  REQUIRE (la_pending_get_count () == 1);

  REQUIRE (la_task_queue_pop (&queued, 0) == NO_ERROR);
  test_push_result (&queued, 0);
  REQUIRE (la_task_queue_pop (&queued, 1) == NO_ERROR);
  test_push_result (&queued, 1);
  REQUIRE (la_coordinator_collect_and_retire () == NO_ERROR);
  REQUIRE (context.dispatched == 3);
  REQUIRE (context.retired == 2);
  REQUIRE (la_pending_is_empty ());
  REQUIRE (LSA_EQ (&context.frontier, &empty_boundary));

  REQUIRE (test_pop_any (&queued, &worker) == NO_ERROR);
  REQUIRE (queued.tranid == dependent.tranid);
  test_push_result (&queued, worker);
  REQUIRE (la_coordinator_drain () == NO_ERROR);
  REQUIRE (context.retired == 3);
  REQUIRE (context.stored == 3);
  REQUIRE (LSA_EQ (&context.frontier, &dependent.commit_lsa));
  REQUIRE (la_coordinator_drain_complete ());
}
