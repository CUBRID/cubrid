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

#include "log_applier_worker.hpp"

#include "catch2/catch.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace
{
  constexpr int TEST_INITIALIZE_ERROR = -7001;

  struct fake_apply_context
  {
    std::atomic<int> initialize_count { 0 };
    std::atomic<int> finalize_count { 0 };
    std::atomic<int> execute_count { 0 };
    int fail_worker = -1;
    std::mutex mutex;
    std::condition_variable condition;
    bool block_first = false;
    bool first_started = false;
    bool release_first = false;
  };

  int
  initialize_worker (void *shared_context, std::size_t worker_index, void **worker_context)
  {
    fake_apply_context &context = *static_cast<fake_apply_context *> (shared_context);
    std::size_t *index = new std::size_t (worker_index);

    *worker_context = index;
    context.initialize_count++;
    if (static_cast<int> (worker_index) == context.fail_worker)
      {
	delete index;
	*worker_context = NULL;
	return TEST_INITIALIZE_ERROR;
      }

    return 0;
  }

  int
  execute_task (void *shared_context, void *, const la_apply_task &task, la_apply_result &result)
  {
    fake_apply_context &context = *static_cast<fake_apply_context *> (shared_context);

    if (context.block_first && task.dispatch_sequence == 1)
      {
	std::unique_lock<std::mutex> lock (context.mutex);
	context.first_started = true;
	context.condition.notify_all ();
	context.condition.wait (lock, [&context] { return context.release_first; });
      }

    context.execute_count++;
    result.committed_rep_lsa = task.commit_lsa;
    return 0;
  }

  void
  finalize_worker (void *shared_context, void *worker_context)
  {
    fake_apply_context &context = *static_cast<fake_apply_context *> (shared_context);

    delete static_cast<std::size_t *> (worker_context);
    context.finalize_count++;
  }

  la_worker_operations
  make_operations ()
  {
    return { initialize_worker, execute_task, finalize_worker };
  }

  struct worker_fixture
  {
    la_task_queue *task_queue = NULL;
    la_result_queue *result_queue = NULL;
    la_workers *workers = NULL;
  };

  int
  create_worker_fixture (std::size_t worker_count, std::size_t queue_capacity,
			 const la_worker_operations &worker_operations, void *shared_context,
			 worker_fixture **fixture_out)
  {
    if (fixture_out == NULL)
      {
	return ER_FAILED;
      }

    *fixture_out = NULL;
    worker_fixture *fixture = new worker_fixture ();
    int error = la_task_queue_create (queue_capacity, &fixture->task_queue);
    if (error == NO_ERROR)
      {
	error = la_result_queue_create (&fixture->result_queue);
      }
    if (error == NO_ERROR)
      {
	error = la_worker_create (worker_count, fixture->task_queue, fixture->result_queue,
				  worker_operations, shared_context, &fixture->workers);
      }
    if (error != NO_ERROR)
      {
	la_worker_destroy (fixture->workers);
	la_result_queue_destroy (fixture->result_queue);
	la_task_queue_destroy (fixture->task_queue);
	delete fixture;
	return error;
      }

    *fixture_out = fixture;
    return NO_ERROR;
  }

  void
  destroy_worker_fixture (worker_fixture *fixture)
  {
    if (fixture == NULL)
      {
	return;
      }

    la_task_queue_shutdown (fixture->task_queue);
    la_worker_destroy (fixture->workers);
    la_result_queue_destroy (fixture->result_queue);
    la_task_queue_destroy (fixture->task_queue);
    delete fixture;
  }

  int
  start_worker_fixture (worker_fixture *fixture)
  {
    return la_worker_start (fixture->workers);
  }

  void
  stop_worker_fixture (worker_fixture *fixture)
  {
    la_task_queue_shutdown (fixture->task_queue);
    la_worker_stop (fixture->workers);
  }

  la_queue_enqueue_status
  enqueue_task (worker_fixture *fixture, const la_apply_task &task)
  {
    return la_task_queue_try_enqueue (fixture->task_queue, task);
  }

  bool
  dequeue_result (worker_fixture *fixture, la_apply_result &result)
  {
    return la_result_queue_try_dequeue (fixture->result_queue, result);
  }

  la_apply_task
  make_task (std::uint64_t dispatch_sequence)
  {
    la_apply_task task = {};

    task.dispatch_sequence = dispatch_sequence;
    task.tranid = static_cast<int> (dispatch_sequence);
    task.record_type = 1;
    task.commit_lsa = LOG_LSA (static_cast<std::int64_t> (dispatch_sequence), 0);
    LSA_SET_NULL (&task.dependency_seq);
    task.dependency_is_ref = false;
    task.final_pageid = static_cast<LOG_PAGEID> (dispatch_sequence);
    task.log_record_time = static_cast<std::time_t> (dispatch_sequence);
    task.apply = NULL;
    return task;
  }

  std::vector<la_apply_result>
  collect_results (worker_fixture *fixture, std::size_t expected)
  {
    std::vector<la_apply_result> results;
    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);

    while (results.size () < expected && std::chrono::steady_clock::now () < deadline)
      {
	la_apply_result result;
	if (dequeue_result (fixture, result))
	  {
	    results.push_back (result);
	  }
	else
	  {
	    std::this_thread::yield ();
	  }
      }
    return results;
  }

  void
  wait_until_first_task_starts (fake_apply_context &context)
  {
    std::unique_lock<std::mutex> lock (context.mutex);
    REQUIRE (context.condition.wait_for (lock, std::chrono::seconds (5),
					 [&context] { return context.first_started; }));
  }

  void
  release_first_task (fake_apply_context &context)
  {
    std::lock_guard<std::mutex> lock (context.mutex);
    context.release_first = true;
    context.condition.notify_all ();
  }

  void
  wait_until_executed (fake_apply_context &context, int expected)
  {
    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (context.execute_count < expected && std::chrono::steady_clock::now () < deadline)
      {
	std::this_thread::yield ();
      }
    REQUIRE (context.execute_count == expected);
  }
}

TEST_CASE ("worker fixture rejects invalid construction arguments", "[log_applier_worker]")
{
  fake_apply_context context;
  worker_fixture *fixture = NULL;

  CHECK (create_worker_fixture (0, 1, make_operations (), &context, &fixture) != 0);
  CHECK (create_worker_fixture (1, 0, make_operations (), &context, &fixture) != 0);

  CHECK (create_worker_fixture (1, 1, make_operations (), &context, NULL) != 0);
  CHECK (fixture == NULL);
}

TEST_CASE ("worker fixture executes accepted tasks and returns results", "[log_applier_worker]")
{
  fake_apply_context context;
  worker_fixture *fixture = NULL;
  constexpr std::size_t task_count = 32;

  REQUIRE (create_worker_fixture (4, task_count, make_operations (), &context, &fixture) == 0);
  REQUIRE (start_worker_fixture (fixture) == 0);

  for (std::size_t i = 1; i <= task_count; i++)
    {
      REQUIRE (enqueue_task (fixture, make_task (i)) == LA_QUEUE_ENQUEUE_ACCEPTED);
    }

  std::vector<la_apply_result> results = collect_results (fixture, task_count);
  stop_worker_fixture (fixture);
  std::set<std::uint64_t> sequences;
  for (const la_apply_result &result : results)
    {
      sequences.insert (result.dispatch_sequence);
      CHECK (result.error == 0);
      CHECK (result.worker_index < 4);
      CHECK (result.commit_lsa == result.committed_rep_lsa);
    }

  CHECK (results.size () == task_count);
  CHECK (sequences.size () == task_count);
  CHECK (context.execute_count == static_cast<int> (task_count));
  CHECK (context.initialize_count == 4);
  CHECK (context.finalize_count == 4);
  destroy_worker_fixture (fixture);
}

TEST_CASE ("one worker preserves task FIFO order", "[log_applier_worker]")
{
  fake_apply_context context;
  worker_fixture *fixture = NULL;

  REQUIRE (create_worker_fixture (1, 8, make_operations (), &context, &fixture) == 0);
  REQUIRE (start_worker_fixture (fixture) == 0);
  for (std::uint64_t dispatch_sequence = 1; dispatch_sequence <= 8; dispatch_sequence++)
    {
      REQUIRE (enqueue_task (fixture, make_task (dispatch_sequence)) == LA_QUEUE_ENQUEUE_ACCEPTED);
    }
  std::vector<la_apply_result> results = collect_results (fixture, 8);
  stop_worker_fixture (fixture);
  REQUIRE (results.size () == 8);
  for (std::size_t i = 0; i < results.size (); i++)
    {
      CHECK (results[i].dispatch_sequence == i + 1);
    }
  destroy_worker_fixture (fixture);
}

TEST_CASE ("shared result queue grows independently from worker queue capacity", "[log_applier_worker]")
{
  fake_apply_context context;
  worker_fixture *fixture = NULL;
  constexpr std::uint64_t task_count = 32;

  REQUIRE (create_worker_fixture (2, 2, make_operations (), &context, &fixture) == 0);
  REQUIRE (start_worker_fixture (fixture) == 0);

  for (std::uint64_t dispatch_sequence = 1; dispatch_sequence <= task_count; dispatch_sequence++)
    {
      la_queue_enqueue_status status = LA_QUEUE_ENQUEUE_FULL;
      auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
      while (status == LA_QUEUE_ENQUEUE_FULL && std::chrono::steady_clock::now () < deadline)
	{
	  status = enqueue_task (fixture, make_task (dispatch_sequence));
	  std::this_thread::yield ();
	}
      REQUIRE (status == LA_QUEUE_ENQUEUE_ACCEPTED);
    }

  std::vector<la_apply_result> results = collect_results (fixture, task_count);
  stop_worker_fixture (fixture);
  CHECK (results.size () == task_count);
  CHECK (context.execute_count == static_cast<int> (task_count));
  destroy_worker_fixture (fixture);
}

TEST_CASE ("worker dequeue releases shared worker queue capacity", "[log_applier_worker]")
{
  fake_apply_context context;
  context.block_first = true;
  worker_fixture *fixture = NULL;

  REQUIRE (create_worker_fixture (1, 1, make_operations (), &context, &fixture) == 0);
  REQUIRE (start_worker_fixture (fixture) == 0);
  REQUIRE (enqueue_task (fixture, make_task (1)) == LA_QUEUE_ENQUEUE_ACCEPTED);
  wait_until_first_task_starts (context);
  REQUIRE (enqueue_task (fixture, make_task (2)) == LA_QUEUE_ENQUEUE_ACCEPTED);
  CHECK (enqueue_task (fixture, make_task (3)) == LA_QUEUE_ENQUEUE_FULL);

  release_first_task (context);
  la_queue_enqueue_status enqueue_status = LA_QUEUE_ENQUEUE_FULL;
  auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
  while (enqueue_status == LA_QUEUE_ENQUEUE_FULL && std::chrono::steady_clock::now () < deadline)
    {
      enqueue_status = enqueue_task (fixture, make_task (3));
      std::this_thread::yield ();
    }
  CHECK (enqueue_status == LA_QUEUE_ENQUEUE_ACCEPTED);
  CHECK (collect_results (fixture, 3).size () == 3);
  stop_worker_fixture (fixture);
  destroy_worker_fixture (fixture);
}

TEST_CASE ("shutdown rejects new tasks and leaves queued tasks unexecuted",
	   "[log_applier_worker]")
{
  fake_apply_context context;
  context.block_first = true;
  worker_fixture *fixture = NULL;

  REQUIRE (create_worker_fixture (1, 1, make_operations (), &context, &fixture) == 0);
  REQUIRE (start_worker_fixture (fixture) == 0);
  REQUIRE (enqueue_task (fixture, make_task (1)) == LA_QUEUE_ENQUEUE_ACCEPTED);
  wait_until_first_task_starts (context);
  REQUIRE (enqueue_task (fixture, make_task (2)) == LA_QUEUE_ENQUEUE_ACCEPTED);
  CHECK (enqueue_task (fixture, make_task (3)) == LA_QUEUE_ENQUEUE_FULL);

  std::future<void> stop = std::async (std::launch::async, [fixture] { stop_worker_fixture (fixture); });
  la_queue_enqueue_status enqueue_status = LA_QUEUE_ENQUEUE_FULL;
  auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
  while (enqueue_status == LA_QUEUE_ENQUEUE_FULL && std::chrono::steady_clock::now () < deadline)
    {
      enqueue_status = enqueue_task (fixture, make_task (3));
      std::this_thread::yield ();
    }
  CHECK (enqueue_status == LA_QUEUE_ENQUEUE_STOPPED);
  release_first_task (context);
  REQUIRE (stop.wait_for (std::chrono::seconds (5)) == std::future_status::ready);
  stop.get ();

  CHECK (collect_results (fixture, 1).size () == 1);
  CHECK (context.execute_count == 1);
  destroy_worker_fixture (fixture);
}

TEST_CASE ("worker initialization failure joins all created threads", "[log_applier_worker]")
{
  fake_apply_context context;
  context.fail_worker = 2;
  worker_fixture *fixture = NULL;

  REQUIRE (create_worker_fixture (4, 8, make_operations (), &context, &fixture) == 0);
  CHECK (start_worker_fixture (fixture) == TEST_INITIALIZE_ERROR);
  CHECK (context.initialize_count == 4);
  CHECK (context.finalize_count == 3);
  CHECK (context.execute_count == 0);
  CHECK (enqueue_task (fixture, make_task (1)) == LA_QUEUE_ENQUEUE_STOPPED);
  destroy_worker_fixture (fixture);
}
