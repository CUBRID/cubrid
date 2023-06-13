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

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <random>
#include <thread>

/* A connection_hander mock to simualte the destruction dealy and count instances */
class connection_handler_mock
{
  public:
    connection_handler_mock (std::atomic<int> &instance_counter, int delay_usec)
      : m_instance_counter { instance_counter }
      , m_delay_usec { delay_usec }
    {
      m_instance_counter++;
    }

    ~connection_handler_mock ()
    {
      std::this_thread::sleep_for (std::chrono::microseconds (m_delay_usec));
      m_instance_counter--;
    }

  private:
    std::atomic<int> &m_instance_counter;
    int m_delay_usec; // simulate the delay doing cleaning connection jobs.
};

TEST_CASE ("Test one connection", "")
{
  // Do a small test on a single connection
  async_disconnect_handler<connection_handler_mock> disconn_handler;
  std::atomic<int> instance_counter = 0;

  constexpr int delay_usec = 100;
  std::unique_ptr<connection_handler_mock> conn_handler;

  conn_handler.reset (new connection_handler_mock (instance_counter, delay_usec));

  REQUIRE (instance_counter == 1);

  disconn_handler.disconnect (std::move (conn_handler));
  disconn_handler.terminate ();

  REQUIRE (instance_counter == 0);
}

TEST_CASE ("Test multiple connections disconnecting concurrently", "")
{
  async_disconnect_handler<connection_handler_mock> disconn_handler;
  std::atomic<int> instance_counter = 0;

  std::random_device rd;
  std::mt19937 gen (rd ());
  std::uniform_int_distribution<int> dis (10, 20000); // [10, 20000] usec

  constexpr int cnt_conn =
	  50; // time consumed for disconnecting all = cnt_conn * dis[] = 10*50 ~ 20000*50 = 0.5ms ~ 1000ms
  std::array<std::unique_ptr<connection_handler_mock>, cnt_conn> conn_arr;

  for (int i=0; i < cnt_conn; i++)
    {
      conn_arr[i].reset (new connection_handler_mock (instance_counter, dis (gen)));
    }

  REQUIRE (instance_counter == cnt_conn);

  for (int i=0; i < cnt_conn; i++)
    {
      disconn_handler.disconnect (std::move (conn_arr[i]));
    }

  disconn_handler.terminate ();

  REQUIRE (instance_counter == 0);  // exepects no connection left
}
