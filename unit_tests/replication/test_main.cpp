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

#include "test_log_generator.hpp"
#include <iostream>

template <typename Func, typename ... Args>
int
test_module (int &global_error, Func &&f, Args &&... args)
{
  std::cout << std::endl;
  std::cout << "  start testing module ";

  int err = f (std::forward <Args> (args)...);
  if (err == 0)
    {
      std::cout << "  test completed successfully" << std::endl;
    }
  else
    {
      std::cout << "  test failed" << std::endl;
      global_error = global_error == 0 ? err : global_error;
    }
  return err;
}

int main ()
{
  int global_error = 0;

/* disable log generator tests
 * since interface of log_generator changed to high-level objects, it is not possible to simulate master node state
 */
#if 0
  test_module (global_error, test_replication::test_log_generator1);
  test_module (global_error, test_replication::test_log_generator2);
#endif

  /* add more tests here */

  return global_error;
}
