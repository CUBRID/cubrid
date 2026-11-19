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
 * test_log_applier_queues.cpp - task queue (coordinator to workers) and result queue (workers to coordinator)
 */

#include "catch2/catch.hpp"

#include "test_log_applier_common.hpp"
#include "log_applier_task_queue.hpp"
#include "log_applier_result_queue.hpp"

#include "error_code.h"

#include <atomic>
#include <chrono>
#include <thread>

static LA_RESULT
test_result (UINT64 dispatch_seq)
{
  LA_RESULT result;

  memset (&result, 0, sizeof (result));
  result.dispatch_seq = dispatch_seq;
  return result;
}

TEST_CASE ("task queue hands each worker its own tasks in FIFO order and reports full per worker",
	   "[log_applier_queues]")
{
  LA_TASK out;

  REQUIRE (la_task_queue_init (2) == NO_ERROR);
  for (int t = 0; t < LA_TASK_QUEUE_CAPACITY; t++)
    {
      LA_TASK task = test_task (t, test_lsa (t + 1), test_null_lsa ());
      REQUIRE_FALSE (la_task_queue_is_full (0));
      REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
    }
  REQUIRE (la_task_queue_is_full (0));

  /* a full queue of one worker leaves the other worker's queue open */
  REQUIRE_FALSE (la_task_queue_is_full (1));
  LA_TASK other = test_task (5000, test_lsa (5000), test_null_lsa ());
  REQUIRE (la_task_queue_push (&other, 1) == NO_ERROR);
  REQUIRE (la_task_queue_get_count () == LA_TASK_QUEUE_CAPACITY + 1);

  REQUIRE (la_task_queue_pop (&out, 1) == NO_ERROR);
  REQUIRE (out.tranid == 5000);
  for (int t = 0; t < LA_TASK_QUEUE_CAPACITY; t++)
    {
      REQUIRE (la_task_queue_pop (&out, 0) == NO_ERROR);
      REQUIRE (out.tranid == t);
    }
  REQUIRE (la_task_queue_get_count () == 0);
  la_task_queue_final ();
}

TEST_CASE ("task queue wakes only the worker whose queue got a task, and every worker on shutdown",
	   "[log_applier_queues]")
{
  REQUIRE (la_task_queue_init (2) == NO_ERROR);

  std::atomic<int> got0 (-1);
  std::atomic<int> rc1 (NO_ERROR);
  std::thread worker0 ([&got0] ()
  {
    LA_TASK out;
    if (la_task_queue_pop (&out, 0) == NO_ERROR)
      {
	got0 = out.tranid;
      }
  });
  std::thread worker1 ([&rc1] ()
  {
    LA_TASK out;
    rc1 = la_task_queue_pop (&out, 1);
  });
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  LA_TASK task = test_task (42, test_lsa (1), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  worker0.join ();
  REQUIRE (got0 == 42);

  /* worker 1 is still waiting on its empty queue; only shutdown releases it */
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  REQUIRE (rc1 == NO_ERROR);
  la_task_queue_signal_shutdown ();
  worker1.join ();
  REQUIRE (rc1 == ER_FAILED);
  la_task_queue_final ();
}

TEST_CASE ("task queue try_pop takes a waiting task, reports an empty queue after the hold, and fails on shutdown",
	   "[log_applier_queues]")
{
  LA_TASK out;
  bool taken = true;

  REQUIRE (la_task_queue_init (1) == NO_ERROR);
  REQUIRE (la_task_queue_try_pop (&out, &taken, 0, 0) == NO_ERROR);
  REQUIRE_FALSE (taken);

  auto start = std::chrono::steady_clock::now ();
  REQUIRE (la_task_queue_try_pop (&out, &taken, 0, 50 * 1000 * 1000ULL) == NO_ERROR);
  auto waited = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start);
  REQUIRE_FALSE (taken);
  REQUIRE (waited.count () >= 40);

  LA_TASK task = test_task (7, test_lsa (1), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  REQUIRE (la_task_queue_try_pop (&out, &taken, 0, 0) == NO_ERROR);
  REQUIRE (taken);
  REQUIRE (out.tranid == 7);

  la_task_queue_signal_shutdown ();
  REQUIRE (la_task_queue_try_pop (&out, &taken, 0, 0) == ER_FAILED);
  REQUIRE_FALSE (taken);
  la_task_queue_final ();
}

TEST_CASE ("task queue abandons queued tasks and refuses new ones after shutdown", "[log_applier_queues]")
{
  LA_TASK out;

  REQUIRE (la_task_queue_init (1) == NO_ERROR);
  LA_TASK task = test_task (1, test_lsa (1), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  la_task_queue_signal_shutdown ();

  /* never retired, so a restart applies them again */
  REQUIRE (la_task_queue_pop (&out, 0) == ER_FAILED);
  REQUIRE (la_task_queue_push (&task, 0) == ER_FAILED);

  REQUIRE (la_task_queue_init (1) == NO_ERROR);
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  REQUIRE (la_task_queue_pop (&out, 0) == NO_ERROR);
  la_task_queue_final ();
}

TEST_CASE ("result queue returns results in FIFO order and try_pop does not wait", "[log_applier_queues]")
{
  LA_RESULT out;

  REQUIRE (la_result_queue_init (4) == NO_ERROR);
  REQUIRE (la_result_queue_try_pop (&out) == ER_FAILED);
  for (UINT64 s = 0; s < 3; s++)
    {
      LA_RESULT r = test_result (s);
      REQUIRE (la_result_queue_push (&r) == NO_ERROR);
    }
  REQUIRE (la_result_queue_get_count () == 3);
  for (UINT64 s = 0; s < 3; s++)
    {
      REQUIRE (la_result_queue_try_pop (&out) == NO_ERROR);
      REQUIRE (out.dispatch_seq == s);
    }
  REQUIRE (la_result_queue_try_pop (&out) == ER_FAILED);
}

TEST_CASE ("result queue wait_for_result times out when empty and does not dequeue", "[log_applier_queues]")
{
  LA_RESULT out;

  REQUIRE (la_result_queue_init (4) == NO_ERROR);
  auto start = std::chrono::steady_clock::now ();
  REQUIRE_FALSE (la_result_queue_wait_for_result (100));
  auto waited = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start);
  REQUIRE (waited.count () >= 90);

  LA_RESULT r = test_result (7);
  REQUIRE (la_result_queue_push (&r) == NO_ERROR);
  REQUIRE (la_result_queue_wait_for_result (100));
  REQUIRE (la_result_queue_get_count () == 1);
  REQUIRE (la_result_queue_try_pop (&out) == NO_ERROR);
  REQUIRE (out.dispatch_seq == 7);
}

TEST_CASE ("result queue blocks a worker on a full queue until the coordinator takes one", "[log_applier_queues]")
{
  LA_RESULT out;

  REQUIRE (la_result_queue_init (2) == NO_ERROR);
  LA_RESULT r0 = test_result (0), r1 = test_result (1), r2 = test_result (2);
  REQUIRE (la_result_queue_push (&r0) == NO_ERROR);
  REQUIRE (la_result_queue_push (&r1) == NO_ERROR);

  std::atomic<bool> pushed (false);
  std::thread worker ([&pushed, &r2] ()
  {
    if (la_result_queue_push (&r2) == NO_ERROR)
      {
	pushed = true;
      }
  });
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  REQUIRE_FALSE (pushed);
  REQUIRE (la_result_queue_try_pop (&out) == NO_ERROR);
  worker.join ();
  REQUIRE (pushed);
  REQUIRE (la_result_queue_try_pop (&out) == NO_ERROR);
  REQUIRE (out.dispatch_seq == 1);
  REQUIRE (la_result_queue_try_pop (&out) == NO_ERROR);
  REQUIRE (out.dispatch_seq == 2);
}

TEST_CASE ("result queue shutdown releases a blocked worker and a waiting coordinator", "[log_applier_queues]")
{
  REQUIRE (la_result_queue_init (1) == NO_ERROR);
  LA_RESULT r0 = test_result (0), r1 = test_result (1);
  REQUIRE (la_result_queue_push (&r0) == NO_ERROR);

  std::atomic<int> rc (NO_ERROR);
  std::thread worker ([&rc, &r1] ()
  {
    rc = la_result_queue_push (&r1);
  });
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  la_result_queue_signal_shutdown ();
  worker.join ();
  REQUIRE (rc == ER_FAILED);
  REQUIRE (la_result_queue_wait_for_result (1000));

  /* the next run starts with an empty, open queue */
  REQUIRE (la_result_queue_init (1) == NO_ERROR);
  REQUIRE (la_result_queue_get_count () == 0);
  REQUIRE (la_result_queue_push (&r1) == NO_ERROR);
}
