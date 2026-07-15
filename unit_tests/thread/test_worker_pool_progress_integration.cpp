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
 * test_worker_pool_progress_integration.cpp - stalled worker expansion regression
 */

#define SERVER_MODE
#include "concurrency_slot.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

#include "lock_free.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <thread>

namespace test_thread
{
  class progress_entry_manager final : public cubthread::entry_manager
  {
    public:
      context_type &create_context (void) override
      {
	context_type *context = new context_type ();
	context->type = TT_WORKER;
	context->m_status = context_type::status::TS_RUN;
	context->shutdown = false;
	return *context;
      }

      void retire_context (context_type &context) override
      {
	delete &context;
      }

      void recycle_context (context_type &) override
      {
      }

      void stop_execution (context_type &context) override
      {
	context.shutdown = true;
      }
  };

  class progress_task final : public cubthread::entry_task
  {
    public:
      progress_task (std::atomic<bool> *release, std::atomic<std::size_t> &started,
		     std::atomic<std::size_t> &retired, bool return_slot = false)
	: m_release (release)
	, m_started (started)
	, m_retired (retired)
	, m_return_slot (return_slot)
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	if (m_return_slot)
	  {
	    std::unique_ptr<cubthread::concurrency_slot> slot = std::move (thread_ref.m_slot);
	    if (slot)
	      {
		slot->return_to_pool (std::move (slot));
	      }
	  }

	m_started.fetch_add (1, std::memory_order_release);
	while (m_release != nullptr && !m_release->load (std::memory_order_acquire))
	  {
	    std::this_thread::yield ();
	  }
      }

      void retire (void) override
      {
	m_retired.fetch_add (1, std::memory_order_release);
	delete this;
      }

    private:
      std::atomic<bool> *m_release;
      std::atomic<std::size_t> &m_started;
      std::atomic<std::size_t> &m_retired;
      bool m_return_slot;
  };

  bool
  wait_until (const std::atomic<std::size_t> &value, std::size_t expected)
  {
    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    while (value.load (std::memory_order_acquire) < expected)
      {
	if (std::chrono::steady_clock::now () >= deadline)
	  {
	    return false;
	  }
	std::this_thread::yield ();
      }
    return true;
  }

  template <typename Pool>
  bool
  wait_until_base_target (Pool *pool, std::size_t expected)
  {
    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    while (std::chrono::steady_clock::now () < deadline)
      {
	UINT64 total_slots = 0;
	UINT64 target_slots = 0;
	UINT64 busy_slots = 0;
	UINT64 total_workers = 0;
	UINT64 target_workers = 0;
	UINT64 busy_workers = 0;

	pool->get_runtime_stats (total_slots, target_slots, busy_slots, total_workers, target_workers, busy_workers);
	if (target_slots == expected && target_workers == expected)
	  {
	    return true;
	  }
	std::this_thread::yield ();
      }
    return false;
  }
}

int
main (void)
{
  constexpr std::size_t MAX_THREADS = 16;
  using elastic_pool = worker_pool_type<cubthread::stats_t::off, cubthread::pool_t::elastic>;
  using namespace std::chrono_literals;

  lf_initialize_transaction_systems (static_cast<int> (MAX_THREADS));
  int error = 0;
  {
    cubthread::entry *main_entry = nullptr;
    cubthread::initialize (main_entry);
    cubthread::manager *thread_mgr = cubthread::get_manager ();
    thread_mgr->set_max_thread_count (MAX_THREADS);
    thread_mgr->alloc_entries ();
    thread_mgr->init_lockfree_system ();
    thread_mgr->init_entries (true);
    cubthread::concurrency_slot_daemon::initialize ();
    test_thread::progress_entry_manager entry_mgr;

    auto *pool = thread_mgr->create_worker_pool<elastic_pool> (8, 1, 1, 3, "progress_expansion", entry_mgr, false,
		 cubthread::wait_seconds ());
    if (pool == nullptr)
      {
	error = 1;
      }
    else
      {
	std::atomic<bool> release { false };
	std::atomic<std::size_t> started { 0 };
	std::atomic<std::size_t> retired { 0 };
	UINT64 total_slots = 0;
	UINT64 target_slots = 0;
	UINT64 busy_slots = 0;
	UINT64 total_workers = 0;
	UINT64 target_workers = 0;
	UINT64 busy_workers = 0;

	// Return the first worker's slot while keeping the worker occupied. A free slot alone must not create an
	// overcommit worker; only a persistent lack of task completions may do that.
	pool->execute_on_core (new test_thread::progress_task (&release, started, retired, true), 0);
	if (!test_thread::wait_until (started, 1))
	  {
	    std::cerr << "initial task did not start" << std::endl;
	    error = 1;
	  }

	// The first expansion dispatches another blocking task. Dispatch is not completion, so a second expansion must
	// still make the final short task runnable.
	pool->execute_on_core (new test_thread::progress_task (&release, started, retired), 0);
	pool->execute_on_core (new test_thread::progress_task (nullptr, started, retired), 0);
	std::this_thread::sleep_for (500ms);
	if (started.load (std::memory_order_acquire) != 1)
	  {
	    std::cerr << "expanded before timeout" << std::endl;
	    error = 1;
	  }

	if (!test_thread::wait_until (started, 2))
	  {
	    pool->get_runtime_stats (total_slots, target_slots, busy_slots, total_workers, target_workers, busy_workers);
	    std::cerr << "stalled queue did not expand once: started=" << started.load (std::memory_order_relaxed)
		      << " retired=" << retired.load (std::memory_order_relaxed) << " slots=" << total_slots << '/'
		      << target_slots << " workers=" << total_workers << '/' << target_workers << std::endl;
	    error = 1;
	  }

	if (!test_thread::wait_until (started, 3) || !test_thread::wait_until (retired, 1))
	  {
	    pool->get_runtime_stats (total_slots, target_slots, busy_slots, total_workers, target_workers, busy_workers);
	    std::cerr << "task dispatch incorrectly stopped expansion: started=" << started.load (std::memory_order_relaxed)
		      << " retired=" << retired.load (std::memory_order_relaxed) << " slots=" << total_slots << '/'
		      << target_slots << " workers=" << total_workers << '/' << target_workers << std::endl;
	    error = 1;
	  }

	total_slots = 0;
	target_slots = 0;
	busy_slots = 0;
	total_workers = 0;
	target_workers = 0;
	busy_workers = 0;
	if (!test_thread::wait_until_base_target (pool, 1))
	  {
	    pool->get_runtime_stats (total_slots, target_slots, busy_slots, total_workers, target_workers, busy_workers);
	    std::cerr << "expansion did not reset: slots=" << target_slots << " workers=" << target_workers << std::endl;
	    error = 1;
	  }

	release.store (true, std::memory_order_release);
	pool->stop_execution ();
	thread_mgr->destroy_worker_pool (pool);
	if (retired.load (std::memory_order_acquire) != 3)
	  {
	    std::cerr << "tasks were not retired: " << retired.load (std::memory_order_relaxed) << std::endl;
	    error = 1;
	  }
      }

    cubthread::concurrency_slot_daemon::finalize ();
    thread_mgr->return_lock_free_transaction_entries ();
    cubthread::finalize ();
  }
  lf_destroy_transaction_systems ();

  if (error == 0)
    {
      std::cout << "  test_worker_pool_progress_integration successful" << std::endl;
    }
  return error;
}
