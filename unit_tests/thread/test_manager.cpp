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

#include "thread_manager.hpp"

#include <iostream>

namespace test_thread
{

int
test_manager (void)
{
  thread::manager thread_manager;

  thread::worker_pool *dummy_pool = thread_manager.create_worker_pool (1, 1);
  thread_manager.destroy_worker_pool (dummy_pool);

  std::cout << "  test_manager successful" << std::endl;

  return 0;
}

} // namespace test_thread
