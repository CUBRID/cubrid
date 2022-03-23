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
#include "catch2/catch.hpp"

#include "server_request_responder.hpp"

#include <array>

struct test_thread_init_final
{
  test_thread_init_final ()
  {
    THREAD_ENTRY *thread_p = NULL;
    cubthread::initialize (thread_p);
    cubthread::initialize_thread_entries ();
  }

  ~test_thread_init_final ()
  {
    cubthread::finalize ();
  }
};

class test_conn
{
  public:
    using payload_t = unsigned int;
    using response_sequence_number_t = unsigned int;

    class sequenced_payload
    {
      public:
	sequenced_payload (response_sequence_number_t a_rsn, payload_t a_payload)
	  : m_rsn (a_rsn)
	  , m_payload (a_payload)
	{
	}

	payload_t pull_payload ()
	{
	  payload_t ret = m_payload;
	  m_payload = 0;
	  return ret;
	}

	void push_payload (payload_t &&a_payload)
	{
	  m_payload = a_payload;
	  a_payload = 0;
	}

	response_sequence_number_t get_rsn () const
	{
	  return m_rsn;
	}

      private:
	response_sequence_number_t m_rsn;
	payload_t m_payload;
    };

    ~test_conn ()
    {
      REQUIRE (m_request_count == m_response_count);
      REQUIRE (m_request_count == m_handle_count);
    }

    void respond (sequenced_payload &&a_sp)
    {
      // Require that the sequence_payload was handled
      // the payload should be incremented once (see hanlder in test_env::simulate_request
      REQUIRE (a_sp.pull_payload () == a_sp.get_rsn () + 1);
      ++m_response_count;
    }

    std::atomic<size_t> m_request_count = 0;
    std::atomic<size_t> m_handle_count = 0;
    std::atomic<size_t> m_response_count = 0;
};

template <size_t T_CONN_COUNT>
struct test_env
{
  std::atomic<unsigned int> m_rsn_gen = 0;
  test_thread_init_final m_thread_init_final;   // only to initialize/finalize cubthread
  std::array<test_conn, T_CONN_COUNT> m_conns;
  // responder must be destroyed before connections
  // because responder handles connections in its dtor
  server_request_responder<test_conn> m_rrh;

  test_env ()
  {
    for (const auto &conn : m_conns)
      {
	m_rrh.register_connection (&conn);
      }
  }

  ~test_env ()
  {
    for (const auto &conn : m_conns)
      {
	m_rrh.wait_connection_to_become_idle (&conn);
      }
  }

  void simulate_request (size_t conn_index)
  {
    test_conn::response_sequence_number_t rsn = ++m_rsn_gen;
    test_conn::sequenced_payload sp { rsn, rsn };   // init payload to rsn

    test_conn &conn_ref = m_conns[conn_index];
    ++conn_ref.m_request_count;

    auto handler = [rsn, &conn_ref] (cubthread::entry &, test_conn::payload_t &a_p)
    {
      REQUIRE (a_p == rsn); // input payload is the same
      ++a_p;  // output incremented payload
      ++conn_ref.m_handle_count;
      std::this_thread::sleep_for (std::chrono::microseconds (1));
    };

    m_rrh.async_execute (conn_ref, std::move (sp), std::move (handler));
  }
};

TEST_CASE ("Test one connection", "")
{
  // Do a small test on a single connection
  test_env<1> env;

  env.simulate_request (0);
  env.simulate_request (0);
}

TEST_CASE ("Test multiple connections running concurrently", "")
{
  // Simulate concurrent requests on multiple threads
  constexpr size_t THREAD_COUNT = 10;
  constexpr size_t REQUEST_PER_THREAD_COUNT = 1000;
  test_env<THREAD_COUNT> env;

  std::array<std::thread, THREAD_COUNT> m_request_simulators;
  for (size_t i = 0; i < THREAD_COUNT; ++i)
    {
      m_request_simulators[i] = std::thread ([&env, i, REQUEST_PER_THREAD_COUNT] ()
      {
	for (size_t request_count = 0; request_count < REQUEST_PER_THREAD_COUNT; ++request_count)
	  {
	    env.simulate_request (i);
	  }
      });
    }
  for (size_t i = 0; i < THREAD_COUNT; ++i)
    {
      m_request_simulators[i].join ();
    }
}
