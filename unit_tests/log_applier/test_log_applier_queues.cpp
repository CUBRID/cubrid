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

  REQUIRE (test_task_pop (&out, 1) == NO_ERROR);
  REQUIRE (out.tranid == 5000);
  for (int t = 0; t < LA_TASK_QUEUE_CAPACITY; t++)
    {
      REQUIRE (test_task_pop (&out, 0) == NO_ERROR);
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
    if (test_task_pop (&out, 0) == NO_ERROR)
      {
	got0 = out.tranid;
      }
  });
  std::thread worker1 ([&rc1] ()
  {
    LA_TASK out;
    rc1 = test_task_pop (&out, 1);
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
  REQUIRE (test_task_try_pop (&out, &taken, 0, 0) == NO_ERROR);
  REQUIRE_FALSE (taken);

  auto start = std::chrono::steady_clock::now ();
  REQUIRE (test_task_try_pop (&out, &taken, 0, 50 * 1000 * 1000ULL) == NO_ERROR);
  auto waited = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start);
  REQUIRE_FALSE (taken);
  REQUIRE (waited.count () >= 40);

  LA_TASK task = test_task (7, test_lsa (1), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  REQUIRE (test_task_try_pop (&out, &taken, 0, 0) == NO_ERROR);
  REQUIRE (taken);
  REQUIRE (out.tranid == 7);

  la_task_queue_signal_shutdown ();
  REQUIRE (test_task_try_pop (&out, &taken, 0, 0) == ER_FAILED);
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
  REQUIRE (test_task_pop (&out, 0) == ER_FAILED);
  REQUIRE (la_task_queue_push (&task, 0) == ER_FAILED);

  REQUIRE (la_task_queue_init (1) == NO_ERROR);
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  REQUIRE (test_task_pop (&out, 0) == NO_ERROR);
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

TEST_CASE ("task queue pop_many takes up to max tasks in FIFO order and the lock-free count follows",
	   "[log_applier_queues]")
{
  LA_TASK out[4];
  int count;

  REQUIRE (la_task_queue_init (1) == NO_ERROR);
  for (int t = 0; t < 6; t++)
    {
      LA_TASK task = test_task (t, test_lsa (t + 1), test_null_lsa ());
      REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
    }
  REQUIRE (la_task_queue_get_queued (0) == 6);

  REQUIRE (la_task_queue_pop_many (out, 4, &count, 0, true, 0, 0) == NO_ERROR);
  REQUIRE (count == 4);
  for (int i = 0; i < 4; i++)
    {
      REQUIRE (out[i].tranid == i);
    }
  REQUIRE (la_task_queue_get_queued (0) == 2);

  /* fewer waiting than max: takes what is there without waiting */
  REQUIRE (la_task_queue_pop_many (out, 4, &count, 0, false, 0, la_task_queue_get_wake_seq ()) == NO_ERROR);
  REQUIRE (count == 2);
  REQUIRE (out[0].tranid == 4);
  REQUIRE (out[1].tranid == 5);
  REQUIRE (la_task_queue_get_queued (0) == 0);
  REQUIRE (la_task_queue_get_count () == 0);
  la_task_queue_final ();
}

TEST_CASE ("task queue ends a hold on an empty queue when woken, including a wakeup sent before the hold began",
	   "[log_applier_queues]")
{
  LA_TASK out;
  int count = -1;

  REQUIRE (la_task_queue_init (1) == NO_ERROR);

  /* a wakeup after the worker read the sequence but before it waits ends the hold at once */
  UINT64 seq = la_task_queue_get_wake_seq ();
  la_task_queue_wake_holding ();
  auto start = std::chrono::steady_clock::now ();
  REQUIRE (la_task_queue_pop_many (&out, 1, &count, 0, false, 5000 * 1000 * 1000ULL, seq) == NO_ERROR);
  auto waited = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start);
  REQUIRE (count == 0);
  REQUIRE (waited.count () < 1000);

  /* a wakeup during the hold ends it */
  std::thread waker ([] ()
  {
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    la_task_queue_wake_holding ();
  });
  start = std::chrono::steady_clock::now ();
  REQUIRE (la_task_queue_pop_many (&out, 1, &count, 0, false, 5000 * 1000 * 1000ULL, la_task_queue_get_wake_seq ())
	   == NO_ERROR);
  waited = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start);
  waker.join ();
  REQUIRE (count == 0);
  REQUIRE (waited.count () >= 40);
  REQUIRE (waited.count () < 1000);

  /* a worker with no open batch waits for a task and is not released by the wakeup */
  std::atomic<int> got (-1);
  std::thread worker ([&got] ()
  {
    LA_TASK task;
    int n;
    if (la_task_queue_pop_many (&task, 1, &n, 0, true, 0, 0) == NO_ERROR && n == 1)
      {
	got = task.tranid;
      }
  });
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  la_task_queue_wake_holding ();
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  REQUIRE (got == -1);
  LA_TASK task = test_task (9, test_lsa (1), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  worker.join ();
  REQUIRE (got == 9);
  la_task_queue_final ();
}

TEST_CASE ("task queue wait_room returns after the timeout on a full queue, sees room once a task is taken, and "
	   "fails on shutdown", "[log_applier_queues]")
{
  LA_TASK out;
  bool has_room = true;

  REQUIRE (la_task_queue_init (1) == NO_ERROR);
  REQUIRE (la_task_queue_wait_room (0, 100, &has_room) == NO_ERROR);
  REQUIRE (has_room);

  for (int t = 0; t < LA_TASK_QUEUE_CAPACITY; t++)
    {
      LA_TASK task = test_task (t, test_lsa (t + 1), test_null_lsa ());
      REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
    }

  auto start = std::chrono::steady_clock::now ();
  REQUIRE (la_task_queue_wait_room (0, 100, &has_room) == NO_ERROR);
  auto waited = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start);
  REQUIRE_FALSE (has_room);
  REQUIRE (waited.count () >= 90);

  /* a worker taking a task frees a slot and wakes the wait */
  std::atomic<int> rc (ER_FAILED);
  std::thread worker ([&rc] ()
  {
    LA_TASK task;
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    rc = test_task_pop (&task, 0);
  });
  REQUIRE (la_task_queue_wait_room (0, 5000, &has_room) == NO_ERROR);
  worker.join ();
  REQUIRE (rc == NO_ERROR);
  REQUIRE (has_room);
  REQUIRE (la_task_queue_get_queued (0) == LA_TASK_QUEUE_CAPACITY - 1);

  LA_TASK task = test_task (5000, test_lsa (5000), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
  la_task_queue_signal_shutdown ();
  REQUIRE (la_task_queue_wait_room (0, 100, &has_room) == ER_FAILED);
  REQUIRE_FALSE (has_room);
  REQUIRE (test_task_pop (&out, 0) == ER_FAILED);
  la_task_queue_final ();
}

TEST_CASE ("result queue push_many queues a commit's results in order and reports how many on shutdown",
	   "[log_applier_queues]")
{
  LA_RESULT results[3] = { test_result (10), test_result (11), test_result (12) };
  LA_RESULT out;
  int pushed = -1;

  REQUIRE (la_result_queue_init (8) == NO_ERROR);
  REQUIRE (la_result_queue_push_many (results, 3, &pushed) == NO_ERROR);
  REQUIRE (pushed == 3);
  for (UINT64 s = 10; s < 13; s++)
    {
      REQUIRE (la_result_queue_try_pop (&out) == NO_ERROR);
      REQUIRE (out.dispatch_seq == s);
    }

  REQUIRE (la_result_queue_push_many (results, 0, &pushed) == NO_ERROR);
  REQUIRE (pushed == 0);

  la_result_queue_signal_shutdown ();
  REQUIRE (la_result_queue_push_many (results, 3, &pushed) == ER_FAILED);
  REQUIRE (pushed == 0);

  REQUIRE (la_result_queue_init (8) == NO_ERROR);
}
