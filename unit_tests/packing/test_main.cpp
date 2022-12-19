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

#include "connection_defs.h"
#include "system_parameter.h"

#include "test_packing.hpp"

#include <iostream>

static void initialize_fake_system_parameters ()
{
  // params needed by linked code
  // same default values as in system_parameter module
  prm_set_bool_value (PRM_ID_USE_SYSTEM_MALLOC, false);
  prm_set_bool_value (PRM_ID_PERF_TEST_MODE, false);
  prm_set_integer_value (PRM_ID_CSS_MAX_CLIENTS, 100);
  prm_set_integer_value (PRM_ID_HA_MODE, HA_MODE_OFF);
  prm_set_integer_value (PRM_ID_VACUUM_WORKER_COUNT, 10);
  prm_set_bool_value (PRM_ID_IGNORE_TRAILING_SPACE, false);
}

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

  initialize_fake_system_parameters ();

  test_module (global_error, test_packing::test_packing1);

  test_module (global_error, test_packing::test_packing_buffer1);

  test_module (global_error, test_packing::test_packing_all);

  /* add more tests here */

  return global_error;
}
