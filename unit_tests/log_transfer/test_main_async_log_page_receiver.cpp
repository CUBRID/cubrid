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
#include "async_log_page_receiver.hpp"

#include "log_storage.hpp"

#include <memory>
#include <thread>

const int count_pages_per_thread = 90;
const int count_skip_pages = 30;

std::shared_ptr<LOG_PAGE> create_dummy_log_page (LOG_PAGEID page_id);

class test_env
{
  public:
    test_env (int log_pages_count);
    ~test_env ();

    void run_test ();

  private:
    void request_and_consume_log_pages (int start_log_page_id, int count);

  private:
    int m_log_pages_count;
    std::vector<std::thread> m_threads;
    cublog::async_log_page_receiver m_log_page_receiver;
};

void do_test (test_env &env)
{
  env.run_test ();
}

TEST_CASE ("First test", "")
{
  test_env env (9999);
  do_test (env);
}

test_env::test_env (int log_pages_count)
  : m_log_pages_count (log_pages_count)
{

}

test_env::~test_env ()
{
}

void
test_env::run_test ()
{
  for (int i = 0; i <= m_log_pages_count - count_pages_per_thread; i += count_skip_pages)
    {
      std::thread worker (&test_env::request_and_consume_log_pages, this, i, count_pages_per_thread);
      m_threads.push_back (std::move (worker));
    }

  for (int i = 0; i < m_threads.size (); ++i)
    {
      m_threads[i].join ();
    }

  for (int i = 0; i < m_log_pages_count; ++i)
    {
      REQUIRE (m_log_page_receiver.is_page_requested (i) == false);
    }
}

void
test_env::request_and_consume_log_pages (int start_log_page_id, int count)
{
  for (int i = start_log_page_id; i < start_log_page_id + count; ++i)
    {
      if (!m_log_page_receiver.is_page_requested (i))
	{
	  auto log_page = create_dummy_log_page (i);
	  m_log_page_receiver.set_page (log_page);
	}
      m_log_page_receiver.set_page_requested (i);
    }

  for (int i = start_log_page_id; i < start_log_page_id + count; ++i)
    {
      REQUIRE (m_log_page_receiver.is_page_requested (i) == true);
    }

  for (int i = start_log_page_id; i < start_log_page_id + count; ++i)
    {
      auto log_page = create_dummy_log_page (i);
      m_log_page_receiver.set_page (log_page);

      auto required_log_page = m_log_page_receiver.wait_for_page (i);
      REQUIRE (required_log_page->hdr.logical_pageid == i);
    }
}

std::shared_ptr<LOG_PAGE> create_dummy_log_page (LOG_PAGEID page_id)
{
  auto log_page = std::make_shared<LOG_PAGE> ();
  log_page->hdr.logical_pageid = page_id;
  return log_page;
}
