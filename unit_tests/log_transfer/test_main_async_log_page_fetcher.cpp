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
#include <mutex>
#include <unordered_map>
#include <vector>

struct page_id_requirement
{
  bool require_log_page_valid;
  bool is_page_received;
};
struct log_page_fetcher_test_data
{
  std::unordered_map<LOG_PAGEID, page_id_requirement> page_ids_requested;
  std::mutex map_mutex;
} g_log_page_fetcher_test_data;

class test_env
{
  public:
    test_env (bool require_log_page_valid, std::vector<LOG_PAGEID> log_pageids);
    ~test_env ();

    void run_test ();

  private:
    void on_receive_log_page (LOG_PAGEID page_id, const LOG_PAGE *, int error_code);

  private:
    std::vector<LOG_PAGEID> m_log_pageids;
    cublog::async_page_fetcher m_async_page_fetcher;
};

void do_test (test_env &env)
{
  env.run_test ();
}

TEST_CASE ("Test with a valid log page returned", "")
{
  THREAD_ENTRY *thread_p = NULL;
  cubthread::initialize (thread_p);
  cubthread::initialize_thread_entries ();

  std::vector<LOG_PAGEID> page_ids { 1, 2, 3 };
  test_env env (true, page_ids);
  do_test (env);
}

TEST_CASE ("Test with an invalid log page returned", "")
{
  std::vector<LOG_PAGEID> page_ids { 4, 5, 6 };
  test_env env (false, page_ids);
  do_test (env);
}

test_env::test_env (bool require_log_page_valid, std::vector<LOG_PAGEID> log_pageids)
  : m_log_pageids (log_pageids)
{
  for (auto log_page_id : log_pageids)
    {
      page_id_requirement req;
      req.is_page_received = false;
      req.require_log_page_valid = require_log_page_valid;

      std::unique_lock<std::mutex> lock (g_log_page_fetcher_test_data.map_mutex);
      g_log_page_fetcher_test_data.page_ids_requested.insert (std::make_pair (log_page_id, req));
    }
}

test_env::~test_env () {}

void test_env::run_test ()
{
  for (auto log_page_id : m_log_pageids)
    {
      m_async_page_fetcher.fetch_page (
	      log_page_id,
	      std::bind (
		      &test_env::on_receive_log_page,
		      std::ref (*this),
		      log_page_id,
		      std::placeholders::_1,
		      std::placeholders::_2
	      )
      );
    }
}

void test_env::on_receive_log_page (LOG_PAGEID page_id, const LOG_PAGE *log_page, int error_code)
{
  std::unique_lock<std::mutex> lock (g_log_page_fetcher_test_data.map_mutex);

  if (g_log_page_fetcher_test_data.page_ids_requested[page_id].require_log_page_valid)
    {
      REQUIRE (error_code == NO_ERROR);
      REQUIRE (log_page != nullptr);
      REQUIRE (log_page->hdr.logical_pageid != NULL_PAGEID);
      REQUIRE (log_page->hdr.logical_pageid == page_id);
    }
  else
    {
      REQUIRE (error_code != NO_ERROR);
      REQUIRE (log_page == nullptr);
    }
  REQUIRE (g_log_page_fetcher_test_data.page_ids_requested[page_id].is_page_received == false);
  g_log_page_fetcher_test_data.page_ids_requested[page_id].is_page_received = true;
}

int log_reader::set_lsa_and_fetch_page (const log_lsa &lsa, fetch_mode fetch_page_mode)
{
  std::unique_lock<std::mutex> lock (g_log_page_fetcher_test_data.map_mutex);
  if (g_log_page_fetcher_test_data.page_ids_requested[lsa.pageid].require_log_page_valid)
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
  std::unique_lock<std::mutex> lock (g_log_page_fetcher_test_data.map_mutex);
  if (g_log_page_fetcher_test_data.page_ids_requested[m_lsa.pageid].require_log_page_valid)
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

// Mock some of the functionality
log_reader::log_reader () = default; // needed by log_page_fetch_task::execute