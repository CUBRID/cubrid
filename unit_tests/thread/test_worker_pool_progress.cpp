/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * test_worker_pool_progress.cpp - worker pool progress policy tests
 */

#include "test_manager.hpp"

#include "thread_worker_pool_progress.hpp"

#include <cassert>
#include <chrono>

namespace test_thread
{
  int
  test_worker_pool_progress ()
  {
    using clock = cubthread::worker_pool_progress_tracker::clock;
    using namespace std::chrono_literals;

    cubthread::worker_pool_progress_tracker tracker (1s);
    clock::time_point now {};

    auto decision = tracker.observe (now, false, true, 0);
    assert (!decision.expand);

    decision = tracker.observe (now + 100ms, true, true, 0);
    assert (!decision.expand);

    decision = tracker.observe (now + 1100ms, true, true, 0);
    assert (decision.expand);
    assert (tracker.get_extra_slot_count () == 1);

    decision = tracker.observe (now + 1500ms, true, true, 0);
    assert (!decision.expand);

    decision = tracker.observe (now + 2200ms, true, true, 0);
    assert (decision.expand);
    assert (tracker.get_extra_slot_count () == 2);

    decision = tracker.observe (now + 2250ms, true, true, 1);
    assert (!decision.expand);
    assert (decision.reset_expansion);
    assert (decision.remove_extra_slots == 2);
    assert (tracker.get_extra_slot_count () == 0);

    decision = tracker.observe (now + 3300ms, true, true, 1);
    assert (decision.expand);
    assert (tracker.get_extra_slot_count () == 1);

    decision = tracker.observe (now + 4400ms, true, false, 1);
    assert (!decision.expand);

    decision = tracker.observe (now + 4500ms, false, true, 1);
    assert (!decision.expand);
    assert (decision.reset_expansion);
    assert (decision.remove_extra_slots == 1);

    cubthread::worker_pool_progress_tracker progressing_tracker (1s);
    decision = progressing_tracker.observe (now, true, true, 0);
    assert (!decision.expand);
    for (std::uint64_t completed = 1; completed <= 10; ++completed)
      {
	decision = progressing_tracker.observe (now + std::chrono::seconds (completed), true, true, completed);
	assert (!decision.expand);
	assert (decision.reset_expansion);
	assert (progressing_tracker.get_extra_slot_count () == 0);
      }

    cubthread::worker_pool_progress_tracker bounded_tracker (1s);
    decision = bounded_tracker.observe (now, true, true, 0);
    assert (!decision.expand);
    decision = bounded_tracker.observe (now + 1s, true, true, 0);
    assert (decision.expand);
    assert (bounded_tracker.get_extra_slot_count () == 1);
    decision = bounded_tracker.observe (now + 2s, true, false, 0);
    assert (!decision.expand);
    assert (bounded_tracker.get_extra_slot_count () == 1);
    decision = bounded_tracker.observe (now + 3s, true, true, 1);
    assert (!decision.expand);
    assert (decision.reset_expansion);
    assert (decision.remove_extra_slots == 1);
    assert (bounded_tracker.get_extra_slot_count () == 0);

    return 0;
  }
} // namespace test_thread
