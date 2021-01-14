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
 * test_manager.cpp - implementation for thread manager tests
 */

#include "test_manager.hpp"

#include "test_output.hpp"

// testing server mode
#define SERVER_MODE
#include "thread_entry_task.hpp"
#include "thread_task.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"

#include "lock_free.h"

#include <iostream>
#include <sstream>

namespace test_thread
{

  class dummy_exec : public cubthread::entry_task
  {
      void execute (cubthread::entry &context)
      {
	std::stringstream ss;
	ss << "entry_p: " << &context << std::endl;
	test_common::sync_cout (ss.str ());
      }

      void retire ()
      {
	cubthread::entry_task::retire ();
	test_common::sync_cout ("dummy_retire\n");
      }
  };

  void
  prepare (std::size_t max_threads)
  {
    lf_initialize_transaction_systems ((int) max_threads);
  }

  void
  end (void)
  {
    lf_destroy_transaction_systems ();
  }

  void
  run (std::size_t max_threads)
  {
    cubthread::manager thread_mgr;

    thread_mgr.init_entries (max_threads);

    auto *dummy_pool = thread_mgr.create_worker_pool (1, 1, NULL, NULL, 1, false);
    thread_mgr.destroy_worker_pool (dummy_pool);

    auto *daemon = thread_mgr.create_daemon (cubthread::looper (), new dummy_exec ());
    // give daemon a chance to loop
    std::this_thread::sleep_for (std::chrono::duration<std::size_t> (1));
    thread_mgr.destroy_daemon (daemon);

    std::cout << "  test_manager successful" << std::endl;
  }

  int
  test_manager (void)
  {
    const std::size_t MAX_THREADS = 16;

    prepare (MAX_THREADS);
    run (MAX_THREADS);
    end ();

    return 0;
  }

} // namespace test_thread
