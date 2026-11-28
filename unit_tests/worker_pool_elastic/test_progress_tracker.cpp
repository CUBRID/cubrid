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
 * test_progress_tracker.cpp - when progress_tracker asks a core to retire a worker
 *
 * - the shrink clock runs only while the queue is empty and the core has an idle worker
 * - a poll that sees no idle worker clears the clock; the next idle poll starts it again
 * - queued work clears the clock
 * - completions do not restart the clock, but the request comes only on a poll without completions
 * - the request comes once the retention interval has passed, not before
 */

#include "catch2/catch.hpp"

#include "thread_worker_pool_elastic.hpp"

#include <chrono>
#include <cstdint>

namespace
{
  using namespace std::chrono_literals;

  using tracker_t = cubthread::worker_pool_elastic<cubthread::stats_t::off>::progress_tracker;
  using tracker_clock = tracker_t::clock;

  constexpr tracker_clock::duration STALL_TIMEOUT = 500ms;
  constexpr tracker_clock::duration RETENTION = 10s;

  // the tracker reads a zero time point as a stopped clock, so the polls start well after it
  const tracker_clock::time_point T0 = tracker_clock::time_point {} + 1h;

  bool
  requests_shrink (tracker_t &tracker, tracker_clock::duration at, bool has_queued_task, std::uint64_t completed,
		   bool has_idle_worker)
  {
    return tracker.observe (T0 + at, has_queued_task, completed, false, has_idle_worker).request_worker_shrink;
  }
}

TEST_CASE ("the shrink clock does not run while no worker is idle", "[worker_pool_elastic]")
{
  tracker_t tracker (STALL_TIMEOUT, RETENTION);

  // every worker is blocked in a wait: the queue is empty, but no worker can retire
  CHECK_FALSE (requests_shrink (tracker, 0s, false, 0, false));
  CHECK_FALSE (requests_shrink (tracker, 10s, false, 0, false));
  CHECK_FALSE (requests_shrink (tracker, 30s, false, 0, false));

  // the clock starts when a worker becomes idle, not when the queue emptied
  CHECK_FALSE (requests_shrink (tracker, 31s, false, 0, true));
  CHECK_FALSE (requests_shrink (tracker, 40s, false, 0, true));
  CHECK (requests_shrink (tracker, 41s, false, 0, true));
}

TEST_CASE ("a poll without an idle worker clears the shrink clock", "[worker_pool_elastic]")
{
  tracker_t tracker (STALL_TIMEOUT, RETENTION);

  CHECK_FALSE (requests_shrink (tracker, 0s, false, 0, true));
  CHECK_FALSE (requests_shrink (tracker, 9s, false, 0, true));
  CHECK_FALSE (requests_shrink (tracker, 9500ms, false, 0, false));

  CHECK_FALSE (requests_shrink (tracker, 10s, false, 0, true));
  CHECK_FALSE (requests_shrink (tracker, 19s, false, 0, true));
  CHECK (requests_shrink (tracker, 20s, false, 0, true));
}

TEST_CASE ("queued work clears the shrink clock", "[worker_pool_elastic]")
{
  tracker_t tracker (STALL_TIMEOUT, RETENTION);

  CHECK_FALSE (requests_shrink (tracker, 0s, false, 0, true));
  CHECK_FALSE (requests_shrink (tracker, 5s, true, 0, true));

  // the queue drains at 6 s: the clock starts there
  CHECK_FALSE (requests_shrink (tracker, 6s, false, 1, true));
  CHECK_FALSE (requests_shrink (tracker, 15s, false, 1, true));
  CHECK (requests_shrink (tracker, 16s, false, 1, true));
}

TEST_CASE ("completions keep the shrink clock but defer the request", "[worker_pool_elastic]")
{
  tracker_t tracker (STALL_TIMEOUT, RETENTION);

  CHECK_FALSE (requests_shrink (tracker, 0s, false, 0, true));
  CHECK_FALSE (requests_shrink (tracker, 4s, false, 1, true));
  CHECK_FALSE (requests_shrink (tracker, 8s, false, 2, true));

  // past the interval, but this poll saw a completion
  CHECK_FALSE (requests_shrink (tracker, 12s, false, 3, true));

  // the clock started at 0 s, not at the last completion
  CHECK (requests_shrink (tracker, 12050ms, false, 3, true));
}

TEST_CASE ("the shrink request comes at the retention interval", "[worker_pool_elastic]")
{
  tracker_t tracker (STALL_TIMEOUT, RETENTION);

  CHECK_FALSE (requests_shrink (tracker, 0s, false, 0, true));
  CHECK_FALSE (requests_shrink (tracker, RETENTION - 1ms, false, 0, true));
  CHECK (requests_shrink (tracker, RETENTION, false, 0, true));
}
