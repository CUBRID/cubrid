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

#define CATCH_CONFIG_MAIN //

#include "catch2/catch.hpp"
#include "log_page_async_fetcher.hpp"
#include "log_reader.hpp"

#include <iostream>

struct log_page_fetcher_test_data
{
  bool require_log_page_valid;
  LOG_PAGEID log_pageid;
} g_log_page_fetcher_test_data;

class test_env
{
  public:
    test_env (bool require_log_page_valid, LOG_PAGEID log_pageid = 1);
    ~test_env ();

    void run_test ();

  private:
    void on_receive_log_page (const LOG_PAGE *, int error_code);

  private:
    bool m_require_log_page_valid;
    LOG_PAGEID m_log_pageid;
    cublog::async_page_fetcher m_async_page_fetcher;
};

void do_test (test_env &env)
{
  std::cout << "do_test\n";
  env.run_test ();
}

TEST_CASE ("Test with a valid log page returned", "")
{
  THREAD_ENTRY *thread_p = NULL;
  cubthread::initialize (thread_p);
  cubthread::initialize_thread_entries ();

  test_env env (true);
  do_test (env);
}

TEST_CASE ("Test with an invalid log page returned", "")
{
  test_env env (false);
  do_test (env);
}

test_env::test_env (bool require_log_page_valid, LOG_PAGEID log_pageid)
  : m_require_log_page_valid (require_log_page_valid)
  , m_log_pageid (log_pageid)
{
  g_log_page_fetcher_test_data.log_pageid = log_pageid;
  g_log_page_fetcher_test_data.require_log_page_valid = require_log_page_valid;
}

test_env::~test_env () {}

void test_env::run_test ()
{
  m_async_page_fetcher.fetch_page (m_log_pageid, std::bind (&test_env::on_receive_log_page, std::ref (*this),
				   std::placeholders::_1, std::placeholders::_2));
}

void test_env::on_receive_log_page (const LOG_PAGE *log_page, int error_code)
{
  if (m_require_log_page_valid)
    {
      REQUIRE (error_code == NO_ERROR);
      REQUIRE (log_page != nullptr);
      REQUIRE (log_page->hdr.logical_pageid != NULL_PAGEID);
      REQUIRE (log_page->hdr.logical_pageid == m_log_pageid);
    }
  else
    {
      REQUIRE (error_code != NO_ERROR);
      REQUIRE (log_page == nullptr);
    }
}

int log_reader::set_lsa_and_fetch_page (const log_lsa &lsa, fetch_mode fetch_page_mode)
{
  if (g_log_page_fetcher_test_data.require_log_page_valid)
    {
      m_lsa = lsa;
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }
}

const log_page *log_reader::get_page () const
{
  if (g_log_page_fetcher_test_data.require_log_page_valid)
    {
      auto logpage = new log_page ();
      logpage->hdr.logical_pageid = m_lsa.pageid;
      return logpage;
    }
  else
    {
      return nullptr;
    }
}