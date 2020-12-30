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
 * test_worker_pool.cpp - implementation for test thread worker pool
 */

#include "test_worker_pool.hpp"

#include "test_output.hpp"

#include "thread_task.hpp"
#include "thread_worker_pool.hpp"

#include <atomic>
#include <chrono>
#include <iostream>

namespace test_thread
{
  class test_context
  {
    public:

      test_context (void)
      {
	//
      }

      void interrupt_execution (void)
      {
	// do nothing
      }
  };

  class test_context_manager : public cubthread::context_manager<test_context>
  {
    public:

      context_type &
      create_context (void) final      // create a new thread context; cannot fail
      {
	return * (new context_type);
      }

      void
      retire_context (context_type &context) final      // retire the thread context
      {
	delete &context;
      }
  };

  class test_task : public cubthread::task<test_context>
  {
      void execute (context_type &context)
      {
	(void) context;  // suppress unused parameter
	test_common::sync_cout ("test\n");
      }
  };

  using test_worker_pool_type = cubthread::worker_pool<test_context>;

  class start_end_task : public cubthread::task<test_context>
  {
    public:
      void execute (context_type &context)
      {
	(void) context;  // suppress unused parameter
	test_common::sync_cout ("start\n");
	std::this_thread::sleep_for (std::chrono::seconds (1));
	test_common::sync_cout ("end\n");
      }
  };

  class inc_work : public cubthread::task<test_context>
  {
    public:
      void execute (context_type &context)
      {
	(void) context;  // suppress unused parameter
	if (++m_count % 1000 == 0)
	  {
	    test_common::sync_cout ("10k increments\n");
	  }
      }

    private:
      static std::atomic<size_t> m_count;
  };
  std::atomic<size_t> inc_work::m_count = { 0 };

  int
  test_one_thread_pool (void)
  {
    test_context_manager ctx_mgr;
    test_worker_pool_type pool (1, 1, ctx_mgr, NULL, 1, false);
    pool.execute (new test_task ());
    pool.execute (new test_task ());

    std::this_thread::sleep_for (std::chrono::seconds (1));
    pool.execute (new test_task ());

    std::this_thread::sleep_for (std::chrono::seconds (1));
    pool.stop_execution ();
    return 0;
  }

  int
  test_two_threads_pool (void)
  {
    test_context_manager ctx_mgr;
    test_worker_pool_type pool (2, 16, ctx_mgr, NULL, 1, false);

    pool.execute (new start_end_task ());
    pool.execute (new start_end_task ());

    pool.stop_execution ();

    return 0;
  }

  int
  test_stress (void)
  {
    test_context_manager ctx_mgr;
    size_t nthreads = std::thread::hardware_concurrency ();
    nthreads = nthreads == 0 ? 24 : nthreads;

    nthreads *= 4;

    test_worker_pool_type workpool (nthreads, nthreads * 16, ctx_mgr, NULL, 1, false);

    auto start_time = std::chrono::high_resolution_clock::now ();
    for (int i = 0; i < 10000; i++)
      {
	workpool.execute (new inc_work ());
      }
    auto end_time = std::chrono::high_resolution_clock::now ();

    std::cout << "  duration - " << std::chrono::duration<double> (end_time - start_time).count () << std::endl;
    workpool.stop_execution ();
    return 0;
  }

  int
  test_worker_pool (void)
  {
    test_one_thread_pool ();
    test_two_threads_pool ();
    test_stress ();
    return 0;
  }

} // namespace test_thread
