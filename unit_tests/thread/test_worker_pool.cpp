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
 * test_worker_pool.cpp - implementation for test thread worker pool
 */

#include "test_worker_pool.hpp"

#include "test_output.hpp"

#include "thread_worker_pool.hpp"

#include <iostream>

namespace test_thread {

class dummy_work : public thread::work
{
public:
  void execute_task ()
  {
    test_common::sync_cout ("dummy test");
  }

  ~dummy_work ()
  {

  }
};

int
test_worker_pool (void)
{
  thread::worker_pool pool (1, 1);
  dummy_work* dwp = new dummy_work ();
  pool.execute (dwp);
  return 0;
}

} // namespace test_thread
