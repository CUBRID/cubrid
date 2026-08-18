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

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "double_write_buffer.hpp"
#include "error_code.h"

TEST_CASE ("Disabled DWB delegates permanent volume synchronization", "[double_write_buffer]")
{
  REQUIRE_FALSE (dwb_is_created ());

  bool all_sync = true;
  int error_code = dwb_flush_force (NULL, &all_sync);

  REQUIRE (error_code == NO_ERROR);
  REQUIRE_FALSE (all_sync);
}
