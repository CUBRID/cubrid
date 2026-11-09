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
#include "log_applier_pending.hpp"
#include "log_applier_result_queue.hpp"
#include "log_applier_task_queue.hpp"

#include "error_code.h"

typedef struct test_coordinator_context TEST_COORDINATOR_CONTEXT;
struct test_coordinator_context
{
  int dispatched;
  int retired;
  int stored;
  bool shutdown;
  LOG_LSA frontier;
};

static int
test_before_dispatch (void *arg, const LA_TASK *task)
{
  TEST_COORDINATOR_CONTEXT *context = (TEST_COORDINATOR_CONTEXT *) arg;

  REQUIRE (task != NULL);
  context->dispatched++;

  return NO_ERROR;
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

  return NO_ERROR;
}

static bool
test_shutdown_requested (void *arg)
{
  return ((TEST_COORDINATOR_CONTEXT *) arg)->shutdown;
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
  config.context = context;

  REQUIRE (la_coordinator_initialize (&config) == NO_ERROR);
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
