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
 * test_log_applier_worker.cpp - worker start and stop over the multi-connection client
 *
 * The worker opens its own sub-client with db_restart_sub and closes it with db_shutdown_sub. A sub-client needs
 * a server, so this test defines both, together with the two trigger calls the worker makes after a restart,
 * in place of the client library's: the executable's definitions take precedence. They keep what the library
 * does with the calling thread's error context, which is what the worker has to fit: db_restart_sub creates and
 * registers one for the thread (er_init_sub_client_context) and keeps it when it fails, and db_shutdown_sub
 * destroys it (er_final_sub_client_context).
 */

#include "catch2/catch.hpp"

#include "log_applier_worker.hpp"
#include "log_applier_task_queue.hpp"
#include "log_applier_result_queue.hpp"
#include "test_log_applier_common.hpp"

#include "dbi.h"
#include "error_code.h"
#include "error_manager.h"
#include "trigger_manager.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{
  std::atomic<int> restart_calls;
  std::atomic<int> shutdown_calls;
  std::atomic<int> shutdown_requests;
  std::atomic<unsigned> fail_mask;	/* bit i: db_restart_sub of sub-client index i fails */
  int blocked_from;		/* db_restart_sub of this index and above waits for release_restart */

  std::mutex block_mutex;
  std::condition_variable block_cv;
  bool block_released;

  void
  reset_stubs (unsigned fail, int block_from)
  {
    restart_calls = 0;
    shutdown_calls = 0;
    shutdown_requests = 0;
    fail_mask = fail;
    blocked_from = block_from;
    block_released = false;
  }

  void
  release_restart ()
  {
    std::lock_guard<std::mutex> lock (block_mutex);
    block_released = true;
    block_cv.notify_all ();
  }

  void
  run_task_never (LA_APPLY_WORKER_CONTEXT *, LA_TASK *, int, unsigned long long *, LA_RESULT *)
  {
    FAIL ("no task is queued in these tests");
  }

  int
  commit_batch_never (LA_APPLY_WORKER_CONTEXT *, int, unsigned long long, LA_STATS *)
  {
    FAIL ("no task is queued in these tests");
    return NO_ERROR;
  }

  void
  abort_batch_noop (LA_APPLY_WORKER_CONTEXT *)
  {
  }

  bool
  shutdown_requested_never ()
  {
    return false;
  }

  void
  request_shutdown ()
  {
    shutdown_requests++;
  }

  void
  end_context_noop (LA_APPLY_WORKER_CONTEXT *)
  {
  }

  bool
  keep_batch_open_never ()
  {
    return false;
  }

  bool
  tasks_parked_never ()
  {
    return false;
  }

  LA_WORKER_CONFIG
  test_config ()
  {
    LA_WORKER_CONFIG config;

    config.db_page_size = 16 * 1024;
    config.recdes_pool_size = 4;
    config.trace_level = 0;
    config.run_task = run_task_never;
    config.commit_batch = commit_batch_never;
    config.abort_batch = abort_batch_noop;
    config.request_shutdown = request_shutdown;
    config.shutdown_requested = shutdown_requested_never;
    config.end_context = end_context_noop;
    config.keep_batch_open = keep_batch_open_never;
    config.tasks_parked = tasks_parked_never;
    config.batch_max_msec = 100;
    config.batch_max_tasks = 10000;
    return config;
  }

  /* batch tests: tasks apply without a server, commits are counted, and the coordinator's answers are flags */
  std::atomic<int> commits;
  std::atomic<int> committed_tasks;
  std::atomic<bool> keep_open;
  std::atomic<bool> parked;

  void
  reset_batch_stubs ()
  {
    commits = 0;
    committed_tasks = 0;
    keep_open = false;
    parked = false;
  }

  void
  run_task_ok (LA_APPLY_WORKER_CONTEXT *, LA_TASK *, int, unsigned long long *, LA_RESULT *result)
  {
    result->error = NO_ERROR;
  }

  int
  commit_batch_count (LA_APPLY_WORKER_CONTEXT *, int task_count, unsigned long long, LA_STATS *)
  {
    committed_tasks += task_count;
    commits++;
    return NO_ERROR;
  }

  bool
  keep_batch_open_flag ()
  {
    return keep_open.load ();
  }

  bool
  tasks_parked_flag ()
  {
    return parked.load ();
  }

  /* start one worker that applies with the stubs above and keeps a batch open for up to 10 s */
  void
  start_batch_worker (LA_WORKER *workers)
  {
    LA_WORKER_CONFIG config = test_config ();
    int live[1], live_count;

    config.run_task = run_task_ok;
    config.commit_batch = commit_batch_count;
    config.keep_batch_open = keep_batch_open_flag;
    config.tasks_parked = tasks_parked_flag;
    config.batch_max_msec = 10000;

    REQUIRE (la_task_queue_init (1) == NO_ERROR);
    REQUIRE (la_result_queue_init (4096) == NO_ERROR);
    REQUIRE (la_worker_start (workers, 1, &config) == NO_ERROR);
    REQUIRE (la_worker_wait_started (workers, 1, live, &live_count) == NO_ERROR);
  }

  void
  stop_batch_worker (LA_WORKER *workers)
  {
    la_task_queue_signal_shutdown ();
    la_result_queue_signal_shutdown ();
    la_worker_join (workers, 1);
    la_task_queue_final ();
  }

  /* wait up to timeout_msec for at least n commits */
  bool
  wait_commits (int n, int timeout_msec)
  {
    for (int i = 0; i < timeout_msec / 10 && commits < n; i++)
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (10));
      }
    return commits >= n;
  }

  /* start count workers, wait until each has started or failed, report the ones that started in live, then stop
   * the queues and join; returns what la_worker_wait_started returned */
  int
  start_and_stop (LA_WORKER *workers, int count, int *live, int *live_count)
  {
    LA_WORKER_CONFIG config = test_config ();
    int error;

    REQUIRE (la_task_queue_init (count) == NO_ERROR);
    REQUIRE (la_result_queue_init (16) == NO_ERROR);
    REQUIRE (la_worker_start (workers, count, &config) == NO_ERROR);

    error = la_worker_wait_started (workers, count, live, live_count);
    REQUIRE (restart_calls == count);

    la_task_queue_signal_shutdown ();
    la_result_queue_signal_shutdown ();
    la_worker_join (workers, count);
    la_task_queue_final ();

    return error;
  }
}

/* the client library's sub-client calls, as seen by the calling thread's error context */
extern "C" int
db_restart_sub (int sub_index)
{
#if defined (MULTI_CONN_TO_A_SERVER)
  er_init_sub_client_context ();
#endif
  restart_calls++;

  if (sub_index >= blocked_from)
    {
      std::unique_lock<std::mutex> lock (block_mutex);
      block_cv.wait (lock, [] { return block_released; });
    }

  if (fail_mask & (1u << sub_index))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NET_CANT_CONNECT_SERVER, 1, "test");
      return ER_NET_CANT_CONNECT_SERVER;
    }

  return NO_ERROR;
}

extern "C" int
db_shutdown_sub ()
{
  shutdown_calls++;
#if defined (MULTI_CONN_TO_A_SERVER)
  er_final_sub_client_context ();
#endif
  return NO_ERROR;
}

extern "C" bool
db_disable_trigger (void)
{
  return true;
}

void
tr_init (void)
{
}

TEST_CASE ("every worker closes the sub-client it opened", "[log_applier_worker]")
{
  LA_WORKER workers[4] = {};
  int live[4], live_count;

  reset_stubs (0, 1000);
  REQUIRE (start_and_stop (workers, 4, live, &live_count) == NO_ERROR);
  REQUIRE (live_count == 4);

  REQUIRE (shutdown_calls == 4);
  REQUIRE (shutdown_requests == 0);
}

TEST_CASE ("a worker whose sub-client fails to start closes it and the others keep running", "[log_applier_worker]")
{
  LA_WORKER workers[4] = {};
  int live[4], live_count;

  /* sub-client indexes are the worker slots */
  reset_stubs (1u << 2, 1000);
  REQUIRE (start_and_stop (workers, 4, live, &live_count) == NO_ERROR);

  /* the coordinator is given only the workers that started */
  REQUIRE (live_count == 3);
  REQUIRE (live[0] == 0);
  REQUIRE (live[1] == 1);
  REQUIRE (live[2] == 3);

  /* the failed one too: db_shutdown_sub releases the error context a failed db_restart_sub keeps */
  REQUIRE (shutdown_calls == 4);
  REQUIRE (shutdown_requests == 0);
}

TEST_CASE ("when no worker can start the applier is told to stop", "[log_applier_worker]")
{
  LA_WORKER workers[3] = {};
  int live[3], live_count;

  reset_stubs (0x7, 1000);
  REQUIRE (start_and_stop (workers, 3, live, &live_count) == ER_HA_GENERIC_ERROR);
  REQUIRE (live_count == 0);

  REQUIRE (shutdown_calls == 3);
  REQUIRE (shutdown_requests == 1);
}

TEST_CASE ("a stop while workers are still opening their sub-clients waits for them and closes every one",
	   "[log_applier_worker]")
{
  LA_WORKER workers[4] = {};
  LA_WORKER_CONFIG config = test_config ();

  /* the workers open their sub-clients one at a time; the first one in is held inside db_restart_sub and the
   * others wait for their turn */
  reset_stubs (0, 0);
  REQUIRE (la_task_queue_init (4) == NO_ERROR);
  REQUIRE (la_result_queue_init (16) == NO_ERROR);
  REQUIRE (la_worker_start (workers, 4, &config) == NO_ERROR);

  for (int i = 0; i < 500 && restart_calls < 1; i++)
    {
      std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
  CHECK (restart_calls == 1);

  /* the applier stops before any worker has its sub-client */
  la_task_queue_signal_shutdown ();
  la_result_queue_signal_shutdown ();

  std::thread joiner ([&workers] { la_worker_join (workers, 4); });
  release_restart ();
  joiner.join ();

  REQUIRE (restart_calls == 4);
  REQUIRE (shutdown_calls == 4);
  REQUIRE (shutdown_requests == 0);
  la_task_queue_final ();
}

TEST_CASE ("a worker keeps its batch open on an empty queue while told to, and commits once woken",
	   "[log_applier_worker]")
{
  LA_WORKER workers[1] = {};

  reset_stubs (0, 1000);
  reset_batch_stubs ();
  keep_open = true;
  start_batch_worker (workers);

  for (int t = 0; t < 3; t++)
    {
      LA_TASK task = test_task (t, test_lsa (t + 1), test_null_lsa ());
      REQUIRE (la_task_queue_push (&task, 0) == NO_ERROR);
    }

  /* the coordinator still reads log: an empty queue does not end the batch */
  std::this_thread::sleep_for (std::chrono::milliseconds (300));
  CHECK (commits == 0);
  CHECK (la_result_queue_get_count () == 0);

  /* the coordinator reaches the end of the log (or starts waiting for results) and wakes the holding worker */
  keep_open = false;
  la_task_queue_wake_holding ();
  REQUIRE (wait_commits (1, 2000));
  REQUIRE (commits == 1);
  REQUIRE (committed_tasks == 3);
  REQUIRE (la_result_queue_get_count () == 3);

  stop_batch_worker (workers);
}

TEST_CASE ("a worker whose queue never runs dry commits soon after a task is parked", "[log_applier_worker]")
{
  LA_WORKER workers[1] = {};
  std::atomic<bool> feeding (true);

  reset_stubs (0, 1000);
  reset_batch_stubs ();
  keep_open = true;
  start_batch_worker (workers);

  std::thread feeder ([&feeding] ()
  {
    for (int t = 0; feeding && t < 1000; t++)
      {
	LA_TASK task = test_task (t, test_lsa (t + 1), test_null_lsa ());
	if (la_task_queue_push (&task, 0) != NO_ERROR)
	  {
	    break;
	  }
	std::this_thread::sleep_for (std::chrono::milliseconds (5));
      }
  });

  /* nothing parked: the batch stays open while tasks keep coming (the commit interval is 10 s) */
  std::this_thread::sleep_for (std::chrono::milliseconds (300));
  CHECK (commits == 0);

  /* a parked task may wait for this worker's commit: the batch, open for over LA_WORKER_BATCH_PARKED_MAX_MSEC,
   * commits with the next task although the queue is not empty */
  parked = true;
  REQUIRE (wait_commits (1, 1000));

  feeding = false;
  feeder.join ();
  stop_batch_worker (workers);
}

TEST_CASE ("the per-worker task limit follows ha_apply_max_mem_size and the worker count", "[log_applier_worker]")
{
  int one = la_worker_batch_max_tasks (500, 1);
  int ten = la_worker_batch_max_tasks (500, 10);

  /* 30% of 500 MB over about 1.5 KB per task, less one queue */
  CHECK (one > 50000);
  CHECK (one < 200000);
  /* ten workers share the same memory */
  CHECK (ten < one / 5);
  CHECK (la_worker_batch_max_tasks (4096, 1) > one);

  /* bounded on both sides */
  REQUIRE (la_worker_batch_max_tasks (1, 64) == LA_WORKER_BATCH_MIN_TASKS);
  REQUIRE (la_worker_batch_max_tasks (1000000, 1) == LA_WORKER_BATCH_LIMIT_TASKS);

  /* the ring then holds every worker's full queue and full open transaction within that share */
  UINT64 ring_bytes = (UINT64) LA_COMMIT_ORDER_CAPACITY (10, ten) * LA_WORKER_TASK_BOOKKEEPING_BYTES;
  CHECK (ring_bytes <= (UINT64) 500 * 1024 * 1024 * LA_WORKER_BATCH_MEM_PERCENT / 100);
}
