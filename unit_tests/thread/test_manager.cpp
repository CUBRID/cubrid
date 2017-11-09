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

#include "thread_entry_executable.hpp"
#include "thread_executable.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"

#include "lock_free.h"

#include <iostream>
#include <sstream>

namespace test_thread
{

class dummy_exec : public thread::entry_executable
{
  void execute_task ()
  {
    std::stringstream ss;
    ss << "entry_p: " << get_entry () << std::endl;
    test_common::sync_cout (ss.str ());
  }

  void retire ()
  {
    thread::entry_executable::retire ();
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
  thread::manager thread_manager (max_threads);

  thread::worker_pool *dummy_pool = thread_manager.create_worker_pool (1, 1);
  thread_manager.destroy_worker_pool (dummy_pool);

  thread::daemon *daemon = thread_manager.create_daemon (thread::looper (), new dummy_exec ());
  thread_manager.destroy_daemon (daemon);

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
