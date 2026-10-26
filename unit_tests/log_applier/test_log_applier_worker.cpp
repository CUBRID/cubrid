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

  void
  request_shutdown ()
  {
    shutdown_requests++;
  }

  LA_WORKER_CONFIG
  test_config ()
  {
    LA_WORKER_CONFIG config;

    config.db_page_size = 16 * 1024;
    config.recdes_pool_size = 4;
    config.trace_level = 0;
    config.run_task = run_task_never;
    config.request_shutdown = request_shutdown;
    return config;
  }

  /* start count workers, wait until each has tried its sub-client, stop the queues and join */
  void
  start_and_stop (LA_WORKER *workers, int count)
  {
    LA_WORKER_CONFIG config = test_config ();

    la_task_queue_init ();
    REQUIRE (la_result_queue_init (16) == NO_ERROR);
    REQUIRE (la_worker_start (workers, count, &config) == NO_ERROR);

    for (int i = 0; i < 500 && restart_calls < count; i++)
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (10));
      }
    REQUIRE (restart_calls == count);

    la_task_queue_signal_shutdown ();
    la_result_queue_signal_shutdown ();
    la_worker_join (workers, count);
  }
}

/* the client library's sub-client calls, as seen by the calling thread's error context */
extern "C" int
db_restart_sub (int sub_index)
{
  er_init_sub_client_context ();
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
  er_final_sub_client_context ();
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

  reset_stubs (0, 1000);
  start_and_stop (workers, 4);

  REQUIRE (shutdown_calls == 4);
  REQUIRE (shutdown_requests == 0);
}

TEST_CASE ("a worker whose sub-client fails to start closes it and the others keep running", "[log_applier_worker]")
{
  LA_WORKER workers[4] = {};

  /* sub-client indexes are the worker slots */
  reset_stubs (1u << 2, 1000);
  start_and_stop (workers, 4);

  /* the failed one too: db_shutdown_sub releases the error context a failed db_restart_sub keeps */
  REQUIRE (shutdown_calls == 4);
  REQUIRE (shutdown_requests == 0);
}

TEST_CASE ("when no worker can start the applier is told to stop", "[log_applier_worker]")
{
  LA_WORKER workers[3] = {};

  reset_stubs (0x7, 1000);
  start_and_stop (workers, 3);

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
  la_task_queue_init ();
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
}
