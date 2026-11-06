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
#include "log_applier_worker.hpp"	/* LA_TASK_QUEUE_CAPACITY */

#include "error_code.h"

#include <atomic>
#include <chrono>
#include <thread>

static LA_RESULT
test_result (UINT64 seq)
{
  LA_RESULT result;

  memset (&result, 0, sizeof (result));
  result.seq = seq;
  return result;
}

TEST_CASE ("task queue hands tasks over in FIFO order and reports full at its capacity", "[log_applier_queues]")
{
  LA_TASK out;

  la_task_queue_init ();
  for (int t = 0; t < LA_TASK_QUEUE_CAPACITY; t++)
    {
      LA_TASK task = test_task (t, test_lsa (t + 1), test_null_lsa ());
      REQUIRE_FALSE (la_task_queue_is_full ());
      REQUIRE (la_task_queue_push (&task) == NO_ERROR);
    }
  REQUIRE (la_task_queue_is_full ());
  REQUIRE (la_task_queue_get_count () == LA_TASK_QUEUE_CAPACITY);

  for (int t = 0; t < LA_TASK_QUEUE_CAPACITY; t++)
    {
      REQUIRE (la_task_queue_pop (&out) == NO_ERROR);
      REQUIRE (out.tranid == t);
    }
  REQUIRE (la_task_queue_get_count () == 0);
}

TEST_CASE ("task queue wakes a waiting worker on push and on shutdown", "[log_applier_queues]")
{
  la_task_queue_init ();

  std::atomic<int> got (-1);
  std::thread worker ([&got] ()
  {
    LA_TASK out;
    if (la_task_queue_pop (&out) == NO_ERROR)
      {
	got = out.tranid;
      }
  });
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  LA_TASK task = test_task (42, test_lsa (1), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task) == NO_ERROR);
  worker.join ();
  REQUIRE (got == 42);

  std::atomic<int> rc (NO_ERROR);
  std::thread idle ([&rc] ()
  {
    LA_TASK out;
    rc = la_task_queue_pop (&out);
  });
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  la_task_queue_signal_shutdown ();
  idle.join ();
  REQUIRE (rc == ER_FAILED);
}

TEST_CASE ("task queue abandons queued tasks and refuses new ones after shutdown", "[log_applier_queues]")
{
  LA_TASK out;

  la_task_queue_init ();
  LA_TASK task = test_task (1, test_lsa (1), test_null_lsa ());
  REQUIRE (la_task_queue_push (&task) == NO_ERROR);
  la_task_queue_signal_shutdown ();

  /* never retired, so a restart applies them again */
  REQUIRE (la_task_queue_pop (&out) == ER_FAILED);
  REQUIRE (la_task_queue_push (&task) == ER_FAILED);

  la_task_queue_init ();
  REQUIRE (la_task_queue_push (&task) == NO_ERROR);
  REQUIRE (la_task_queue_pop (&out) == NO_ERROR);
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
      REQUIRE (out.seq == s);
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
  REQUIRE (out.seq == 7);
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
  REQUIRE (out.seq == 1);
  REQUIRE (la_result_queue_try_pop (&out) == NO_ERROR);
  REQUIRE (out.seq == 2);
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
