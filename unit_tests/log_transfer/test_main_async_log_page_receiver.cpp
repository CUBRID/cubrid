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

#include "async_log_page_receiver.hpp"

#include "log_storage.hpp"

#include <memory>
#include <thread>

shared_ptr<LOG_PAGE> create_dummy_log_page (LOG_PAGEID page_id);

struct test_data
{
  std::atomic<int> count_pages_created;
  std::atomic<int> count_pages_consumed;
} g_test_data;

class test_env
{
  public:
    test_env (int log_pages_count);
    ~test_env ();

    void run_test ();

  private:
    void request_log_pages ();
    void check_requested_log_pages ();
    void feed_log_pages ();
    void consume_log_pages ();

  private:
    int m_log_pages_count;
    cublog::async_log_page_receiver m_log_page_receiver;
};

void do_test (test_env &env)
{
  env.run_test ();
}

TEST_CASE ("First test", "")
{
  test_env env (1000);
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
  request_log_pages ();
  check_requested_log_pages ();

  std::thread feed_log_pages_thread (&test_env::feed_log_pages, this);
  std::thread consume_log_pages_thread (&test_env::consume_log_pages, this);

  feed_log_pages_thread.join ();
  consume_log_pages_thread.join ();

  REQUIRE (g_test_data.count_pages_created == g_test_data.count_pages_consumed);
}

void
test_env::request_log_pages ()
{
  for (auto i = 0; i < m_log_pages_count; ++i)
    {
      m_log_page_receiver.set_page_requested (i);
    }
}

void
test_env::check_requested_log_pages ()
{
  for (auto i = 0; i < m_log_pages_count; ++i)
    {
      REQUIRE (m_log_page_receiver.is_page_requested (i));
    }
}

void
test_env::feed_log_pages ()
{
  for (auto i = 0; i < m_log_pages_count; ++i)
    {
      auto log_page = create_dummy_log_page (i);
      m_log_page_receiver.set_page (log_page);
      ++g_test_data.count_pages_created;
    }
}

void
test_env::consume_log_pages ()
{
  for (auto i = 0; i < m_log_pages_count; ++i)
    {
      auto log_page = m_log_page_receiver.wait_for_page (i);
      REQUIRE (log_page->hdr.logical_pageid == i);
      ++g_test_data.count_pages_consumed;
    }
}

shared_ptr<LOG_PAGE> create_dummy_log_page (LOG_PAGEID page_id)
{
  auto log_page = std::make_shared<LOG_PAGE> ();
  log_page->hdr.logical_pageid = page_id;
  return log_page;
}
