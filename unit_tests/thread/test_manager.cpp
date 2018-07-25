/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
