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

#include "test_packing.hpp"

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

  test_module (global_error, test_packing::test_packing1);

  test_module (global_error, test_packing::test_packing_buffer1);

  test_module (global_error, test_packing::test_packing_all);

  /* add more tests here */

  return global_error;
}
