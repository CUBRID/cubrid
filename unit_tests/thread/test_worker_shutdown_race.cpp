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
 * test_worker_shutdown_race.cpp - detached worker shutdown regression
 */

#define SERVER_MODE
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

#include "lock_free.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>

namespace test_thread
{
  class standalone_entry_manager final : public cubthread::entry_manager
  {
    public:
      standalone_entry_manager ()
	: m_release_on_stop (nullptr)
      {
      }

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
	if (m_release_on_stop != nullptr)
	  {
	    m_release_on_stop->store (true, std::memory_order_release);
	  }
      }

      void set_release_on_stop (std::atomic<bool> *release)
      {
	m_release_on_stop = release;
      }

    private:
      std::atomic<bool> *m_release_on_stop;
  };

  class short_task final : public cubthread::entry_task
  {
    public:
      explicit short_task (std::atomic<std::size_t> &retired)
	: m_retired (retired)
      {
      }

      void execute (cubthread::entry &) override
      {
	std::this_thread::yield ();
      }

      void retire (void) override
      {
	m_retired.fetch_add (1, std::memory_order_release);
	delete this;
      }

    private:
      std::atomic<std::size_t> &m_retired;
  };

  class blocking_task final : public cubthread::entry_task
  {
    public:
      blocking_task (std::atomic<bool> &started, std::atomic<bool> &release, std::atomic<std::size_t> &retired)
	: m_started (started)
	, m_release (release)
	, m_retired (retired)
      {
      }

      void execute (cubthread::entry &) override
      {
	m_started.store (true, std::memory_order_release);
	while (!m_release.load (std::memory_order_acquire))
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
      std::atomic<bool> &m_started;
      std::atomic<bool> &m_release;
      std::atomic<std::size_t> &m_retired;
  };

  class counted_task final : public cubthread::entry_task
  {
    public:
      counted_task (std::atomic<std::size_t> &executed, std::atomic<std::size_t> &retired)
	: m_executed (executed)
	, m_retired (retired)
      {
      }

      void execute (cubthread::entry &) override
      {
	m_executed.fetch_add (1, std::memory_order_release);
      }

      void retire (void) override
      {
	m_retired.fetch_add (1, std::memory_order_release);
	delete this;
      }

    private:
      std::atomic<std::size_t> &m_executed;
      std::atomic<std::size_t> &m_retired;
  };
}

int
main (void)
{
  constexpr std::size_t MAX_THREADS = 16;
  constexpr std::size_t ITERATION_COUNT = 500;
  using elastic_pool = worker_pool_type<cubthread::stats_t::off, cubthread::pool_t::elastic>;

  lf_initialize_transaction_systems (static_cast<int> (MAX_THREADS));
  int error = 0;
  {
    cubthread::manager thread_mgr;
    thread_mgr.set_max_thread_count (MAX_THREADS);
    thread_mgr.alloc_entries ();
    thread_mgr.init_lockfree_system ();
    thread_mgr.init_entries (true);
    test_thread::standalone_entry_manager entry_mgr;

    for (std::size_t iteration = 0; iteration < ITERATION_COUNT; ++iteration)
      {
	auto *pool = thread_mgr.create_worker_pool<elastic_pool> (8, 1, 1, 2, "shutdown_race", entry_mgr, false,
		     cubthread::wait_seconds ());
	if (pool == nullptr)
	  {
	    error = 1;
	    break;
	  }

	std::atomic<std::size_t> retired { 0 };
	pool->execute_on_core (new test_thread::short_task (retired), 0);
	pool->stop_execution ();
	thread_mgr.destroy_worker_pool (pool);

	if (retired.load (std::memory_order_acquire) != 1)
	  {
	    error = 1;
	    break;
	  }
      }

    if (error == 0)
      {
	constexpr std::size_t QUEUED_TASK_COUNT = 8;
	auto *pool = thread_mgr.create_worker_pool<elastic_pool> (8, 1, 1, 2, "shutdown_queue", entry_mgr, false,
		     cubthread::wait_seconds ());
	if (pool == nullptr)
	  {
	    error = 1;
	  }
	else
	  {
	    std::atomic<bool> started { false };
	    std::atomic<bool> release { false };
	    std::atomic<std::size_t> executed { 0 };
	    std::atomic<std::size_t> retired { 0 };
	    entry_mgr.set_release_on_stop (&release);

	    pool->execute_on_core (new test_thread::blocking_task (started, release, retired), 0);
	    auto wait_until = std::chrono::steady_clock::now () + std::chrono::seconds (5);
	    while (!started.load (std::memory_order_acquire) && std::chrono::steady_clock::now () < wait_until)
	      {
		std::this_thread::sleep_for (std::chrono::milliseconds (1));
	      }

	    if (!started.load (std::memory_order_acquire))
	      {
		error = 1;
		release.store (true, std::memory_order_release);
	      }
	    else
	      {
		for (std::size_t task_index = 0; task_index < QUEUED_TASK_COUNT; ++task_index)
		  {
		    pool->execute_on_core (new test_thread::counted_task (executed, retired), 0);
		  }
	      }

	    pool->stop_execution ();
	    thread_mgr.destroy_worker_pool (pool);
	    entry_mgr.set_release_on_stop (nullptr);

	    if (executed.load (std::memory_order_acquire) != 0
		|| retired.load (std::memory_order_acquire) != QUEUED_TASK_COUNT + 1)
	      {
		error = 1;
	      }
	  }
      }

    thread_mgr.return_lock_free_transaction_entries ();
  }
  lf_destroy_transaction_systems ();

  if (error == 0)
    {
      std::cout << "  test_worker_shutdown_race successful" << std::endl;
    }
  return error;
}
