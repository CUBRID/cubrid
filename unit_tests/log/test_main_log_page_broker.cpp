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

#include "page_broker.hpp"
#include "log_storage.hpp"
#include "storage_common.h"

#include <atomic>
#include <memory>
#include <thread>
#include <queue>

const int count_pages_per_thread = 90;
const int count_skip_pages = 30;

typedef std::shared_ptr<page_broker<log_page_type>> shared_log_page_receiver;
typedef std::weak_ptr<page_broker<log_page_type>> weak_log_page_receiver;

std::shared_ptr<log_page_owner> create_dummy_log_page (LOG_PAGEID page_id);

PGLENGTH
db_log_page_size (void)
{
  return IO_DEFAULT_PAGE_SIZE;
}

// This class simulates a page server's ability to receive log page requests and send back those log pages.
// It has a main loop that will run for the entire test's duration that will check for active requests and satisfy them.
class dummy_ps
{
  public:
    explicit dummy_ps (shared_log_page_receiver log_page_receiver);
    void request_log_page (LOG_PAGEID page_id);
    void run ();
    void close ()
    {
      m_is_closed = true;
    }

  private:
    std::mutex m_request_mutex;
    std::queue<LOG_PAGEID> m_requested_log_page_ids;
    weak_log_page_receiver m_log_page_receiver;
    std::atomic<bool> m_is_closed;
};

class test_env
{
  public:
    test_env (int log_pages_count);
    ~test_env ();

    void run_test ();

  private:
    void request_page_from_ps (LOG_PAGEID page_id);
    void request_and_consume_log_pages (int start_log_page_id, int count);

  private:
    std::size_t m_log_pages_count;
    std::vector<std::thread> m_threads;
    shared_log_page_receiver m_log_page_receiver;
    std::unique_ptr<dummy_ps> m_dummy_ps;
};

void do_test (test_env &env)
{
  env.run_test ();
}

test_env::test_env (int log_pages_count)
  : m_log_pages_count (log_pages_count)
{
  m_log_page_receiver.reset (new page_broker<log_page_type> ());
  m_dummy_ps.reset (new dummy_ps (m_log_page_receiver));
}

test_env::~test_env ()
{
}

void
test_env::run_test ()
{
  // This will simultate a page server that receives requests and provides log pages.
  std::thread ps_thread ([this] { m_dummy_ps->run (); });

  // Trying to have overlap - one log page is requested in average by 3 different threads.
  for (int i = 0; i <= m_log_pages_count - count_pages_per_thread; i += count_skip_pages)
    {
      std::thread worker (&test_env::request_and_consume_log_pages, this, i, count_pages_per_thread);
      m_threads.push_back (std::move (worker));
    }

  for (int i = 0; i < m_threads.size (); ++i)
    {
      m_threads[i].join ();
    }

  // PS no longer needed. All requests have been served.
  m_dummy_ps->close ();
  ps_thread.join ();

  // Making sure we don't keep memory occupied at this point.
  REQUIRE (m_log_page_receiver->get_requests_count () == 0);
  REQUIRE (m_log_page_receiver->get_pages_count () == 0);
}

void
test_env::request_page_from_ps (LOG_PAGEID page_id)
{
  m_dummy_ps->request_log_page (page_id);
}

void
test_env::request_and_consume_log_pages (int start_log_page_id, int count)
{
  for (int i = start_log_page_id; i < start_log_page_id + count; ++i)
    {
      if (m_log_page_receiver->register_entry (i) == page_broker_register_entry_state::ADDED)
	{
	  request_page_from_ps (i);
	}
    }

  for (int i = start_log_page_id; i < start_log_page_id + count; ++i)
    {
      auto required_log_page = m_log_page_receiver->wait_for_page (i);
      REQUIRE (required_log_page->get_id ()  == i);
    }
}

std::shared_ptr<log_page_owner> create_dummy_log_page (LOG_PAGEID page_id)
{
  auto log_page = std::make_unique<LOG_PAGE> ();
  log_page->hdr.logical_pageid = page_id;
  char *buffer = new char[db_log_page_size ()];
  std::memcpy (buffer, log_page.get (), sizeof (LOG_PAGE));

  return std::make_shared<log_page_owner> (buffer);
}

dummy_ps::dummy_ps (shared_log_page_receiver log_page_receiver)
  : m_log_page_receiver (log_page_receiver)
  , m_is_closed (false)
{
}

void
dummy_ps::request_log_page (LOG_PAGEID page_id)
{
  {
    std::unique_lock<std::mutex> lock (m_request_mutex);
    m_requested_log_page_ids.push (page_id);
  }
}

void
dummy_ps::run ()
{
  while (!m_is_closed)
    {
      std::unique_lock<std::mutex> lock (m_request_mutex);
      if (!m_requested_log_page_ids.empty ())
	{
	  LOG_PAGEID page_id = m_requested_log_page_ids.front ();
	  m_requested_log_page_ids.pop ();
	  auto log_page = create_dummy_log_page (page_id);

	  if (auto log_page_receiver = m_log_page_receiver.lock ())
	    {
	      log_page_receiver->set_page (page_id, std::move (log_page));
	    }
	}
    }
}

TEST_CASE ("First test", "")
{
  test_env env (9999);
  do_test (env);
}
