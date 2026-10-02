/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "log_applier_coordinator.hpp"

#include "catch2/catch.hpp"
#include "error_code.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
  struct coordinator_test_context
  {
    std::atomic<int> executed { 0 };
  };

  int
  execute_task (void *shared_context, void *, const la_apply_task &task, la_apply_result &result)
  {
    coordinator_test_context &context = *static_cast<coordinator_test_context *> (shared_context);

    context.executed++;
    result.committed_rep_lsa = task.commit_lsa;
    return NO_ERROR;
  }

  la_worker_operations
  make_operations ()
  {
    return { NULL, execute_task, NULL };
  }

  la_apply_task
  make_task (int tranid)
  {
    la_apply_task task = {};

    task.tranid = tranid;
    task.record_type = 1;
    task.commit_lsa = LOG_LSA (tranid, 0);
    return task;
  }
}

TEST_CASE ("coordinator owns worker queues and retires completed tasks", "[log_applier_coordinator]")
{
  coordinator_test_context context;
  la_coordinator *coordinator = NULL;

  REQUIRE (la_coordinator_create (2, 4, 4, make_operations (), &context, &coordinator) == NO_ERROR);

  la_apply_task first = make_task (1);
  CHECK (la_coordinator_try_enqueue_task (coordinator, first) == LA_DISPATCH_STOPPED);
  REQUIRE (la_coordinator_start_workers (coordinator) == NO_ERROR);

  la_apply_task second = make_task (2);
  REQUIRE (la_coordinator_try_enqueue_task (coordinator, first) == LA_DISPATCH_ACCEPTED);
  REQUIRE (la_coordinator_try_enqueue_task (coordinator, second) == LA_DISPATCH_ACCEPTED);

  std::size_t collected = 0;
  auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
  while (collected < 2 && std::chrono::steady_clock::now () < deadline)
    {
      std::size_t current = 0;
      REQUIRE (la_coordinator_collect_results (coordinator, &current) == NO_ERROR);
      collected += current;
      std::this_thread::yield ();
    }
  REQUIRE (collected == 2);

  la_retired_task retired;
  REQUIRE (la_coordinator_try_retire_task (coordinator, retired));
  CHECK (retired.result.dispatch_sequence == first.dispatch_sequence);
  REQUIRE (la_coordinator_try_retire_task (coordinator, retired));
  CHECK (retired.result.dispatch_sequence == second.dispatch_sequence);
  CHECK (la_coordinator_unretired_count (coordinator) == 0);
  CHECK (context.executed == 2);

  la_coordinator_stop_workers (coordinator);
  CHECK (la_coordinator_try_enqueue_task (coordinator, first) == LA_DISPATCH_STOPPED);
  la_coordinator_destroy (coordinator);
}
