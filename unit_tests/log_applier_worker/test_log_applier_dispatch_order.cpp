/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "log_applier_dispatch_order.hpp"

#include "catch2/catch.hpp"
#include "error_code.h"

#include <cstdint>

namespace
{
  la_apply_task
  make_task (int tranid)
  {
    la_apply_task task = {};

    task.tranid = tranid;
    task.record_type = tranid;
    task.commit_lsa = LOG_LSA (tranid, 0);
    task.apply = reinterpret_cast<la_apply *> (static_cast<std::uintptr_t> (tranid));
    return task;
  }

  la_apply_result
  make_result (const la_apply_task &task)
  {
    la_apply_result result = {};

    result.dispatch_sequence = task.dispatch_sequence;
    result.tranid = task.tranid;
    result.record_type = task.record_type;
    result.commit_lsa = task.commit_lsa;
    return result;
  }
}

TEST_CASE ("dispatch order matches out-of-order results and retires in dispatch order",
	   "[log_applier_dispatch_order]")
{
  la_task_queue *queue = NULL;
  la_dispatch_order *order = NULL;

  REQUIRE (la_task_queue_create (2, &queue) == NO_ERROR);
  REQUIRE (la_dispatch_order_create (2, &order) == NO_ERROR);

  la_apply_task first = make_task (1);
  la_apply_task second = make_task (2);
  REQUIRE (la_dispatch_order_try_enqueue_task (order, queue, first) == LA_DISPATCH_ACCEPTED);
  REQUIRE (la_dispatch_order_try_enqueue_task (order, queue, second) == LA_DISPATCH_ACCEPTED);
  CHECK (first.dispatch_sequence == 1);
  CHECK (second.dispatch_sequence == 2);

  REQUIRE (la_dispatch_order_record_task_result (order, make_result (second)) == NO_ERROR);
  la_retired_task retired;
  CHECK_FALSE (la_dispatch_order_try_retire_task (order, retired));

  REQUIRE (la_dispatch_order_record_task_result (order, make_result (first)) == NO_ERROR);
  REQUIRE (la_dispatch_order_try_retire_task (order, retired));
  CHECK (retired.result.dispatch_sequence == first.dispatch_sequence);
  CHECK (retired.apply == first.apply);
  REQUIRE (la_dispatch_order_try_retire_task (order, retired));
  CHECK (retired.result.dispatch_sequence == second.dispatch_sequence);
  CHECK (retired.apply == second.apply);
  CHECK (la_dispatch_order_unretired_count (order) == 0);

  la_dispatch_order_destroy (order);
  la_task_queue_destroy (queue);
}

TEST_CASE ("dispatch order does not track a task rejected by the shared queue",
	   "[log_applier_dispatch_order]")
{
  la_task_queue *queue = NULL;
  la_dispatch_order *order = NULL;

  REQUIRE (la_task_queue_create (1, &queue) == NO_ERROR);
  REQUIRE (la_dispatch_order_create (2, &order) == NO_ERROR);

  la_apply_task first = make_task (1);
  la_apply_task second = make_task (2);
  REQUIRE (la_dispatch_order_try_enqueue_task (order, queue, first) == LA_DISPATCH_ACCEPTED);
  CHECK (la_dispatch_order_try_enqueue_task (order, queue, second) == LA_DISPATCH_FULL);
  CHECK (la_dispatch_order_unretired_count (order) == 1);

  la_apply_task dequeued;
  REQUIRE (la_task_queue_dequeue (queue, dequeued));
  REQUIRE (la_dispatch_order_try_enqueue_task (order, queue, second) == LA_DISPATCH_ACCEPTED);
  CHECK (second.dispatch_sequence == 2);

  la_dispatch_order_destroy (order);
  la_task_queue_destroy (queue);
}

TEST_CASE ("dispatch order rejects unknown and duplicate results", "[log_applier_dispatch_order]")
{
  la_task_queue *queue = NULL;
  la_dispatch_order *order = NULL;

  REQUIRE (la_task_queue_create (1, &queue) == NO_ERROR);
  REQUIRE (la_dispatch_order_create (1, &order) == NO_ERROR);

  la_apply_task task = make_task (1);
  REQUIRE (la_dispatch_order_try_enqueue_task (order, queue, task) == LA_DISPATCH_ACCEPTED);
  la_apply_result result = make_result (task);
  REQUIRE (la_dispatch_order_record_task_result (order, result) == NO_ERROR);
  CHECK (la_dispatch_order_record_task_result (order, result) != NO_ERROR);

  result.dispatch_sequence++;
  CHECK (la_dispatch_order_record_task_result (order, result) != NO_ERROR);

  la_dispatch_order_destroy (order);
  la_task_queue_destroy (queue);
}
