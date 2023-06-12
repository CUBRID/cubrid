/*
 *
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
#include "catch2/catch.hpp"

#include "async_disconnect_handler.hpp"

#include <thread>

class connection_handler_mock
{
  public:
    ~connection_handler_mock ()
    {
      std::this_thread::sleep_for (std::chrono::microseconds (1));
    }
};

TEST_CASE ("Test one connection", "")
{
  // Do a small test on a single connection
  async_disconnect_handler<connection_handler_mock> disconn_handler;
  std::unique_ptr<connection_handler_mock> conn_handler;

  disconn_handler.disconnect (std::move (conn_handler));
  disconn_handler.terminate ();
}

TEST_CASE ("Test multiple connections running concurrently", "")
{
  async_disconnect_handler<connection_handler_mock> disconn_handler;
  std::unique_ptr<connection_handler_mock> conn_handler;

  disconn_handler.disconnect (std::move (conn_handler));
  disconn_handler.terminate ();
}
