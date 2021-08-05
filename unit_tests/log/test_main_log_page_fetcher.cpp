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

#include "async_page_fetcher.hpp"
#include "fake_packable_object.hpp"
#include "log_reader.hpp"
#include "page_buffer.h"

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

TEST_CASE ("Test with a very big number of pages", "")
{
  std::vector<LOG_PAGEID> page_ids;
  for (auto i = 0; i < 10000; ++i)
    {
      page_ids.push_back (i);
    }
  test_env env (true, page_ids);
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

test_env::~test_env ()
{
}

void
test_env::run_test ()
{
  for (auto log_page_id : m_log_pageids)
    {
      m_async_page_fetcher.fetch_log_page (
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
    }
  g_log_page_fetcher_test_data.page_ids_requested[page_id].is_page_received = true;
}

// implementation of cubrid stuff require for linking
log_global log_Gl;

log_global::log_global ()
  : m_prior_recver (std::make_unique<cublog::prior_recver> (prior_info))
{
}

log_global::~log_global ()
{
}

namespace cublog
{
  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (meta);
  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (checkpoint_info);

  prior_recver::prior_recver (log_prior_lsa_info &prior_lsa_info)
    : m_prior_lsa_info (prior_lsa_info)
  {
  }
  prior_recver::~prior_recver () = default;
}

mvcc_active_tran::mvcc_active_tran () = default;
mvcc_active_tran::~mvcc_active_tran () = default;
mvcc_trans_status::mvcc_trans_status () = default;
mvcc_trans_status::~mvcc_trans_status () = default;
mvcctable::mvcctable () = default;
mvcctable::~mvcctable () = default;

log_append_info::log_append_info () = default;
log_prior_lsa_info::log_prior_lsa_info () = default;

int
logpb_fetch_page (THREAD_ENTRY *, const LOG_LSA *log_lsa, LOG_CS_ACCESS_MODE, LOG_PAGE *log_pgptr)
{
  std::unique_lock<std::mutex> lock (g_log_page_fetcher_test_data.map_mutex);
  if (g_log_page_fetcher_test_data.page_ids_requested[log_lsa->pageid].require_log_page_valid)
    {
      log_pgptr->hdr.logical_pageid = log_lsa->pageid;
      log_pgptr->hdr.offset = log_lsa->offset;
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }
}

void
logpb_flush_pages (THREAD_ENTRY *thread_p, const LOG_LSA *flush_lsa)
{
}

void
logpb_fatal_error (THREAD_ENTRY *, bool, const char *, const int, const char *, ...)
{
  // todo: don't do fatal error on failed fetch log page
  // assert (false);
}

PAGE_PTR
pgbuf_fix_debug (THREAD_ENTRY *thread_p, const VPID *vpid, PAGE_FETCH_MODE fetch_mode, PGBUF_LATCH_MODE request_mode,
		 PGBUF_LATCH_CONDITION condition, const char *caller_file, int caller_line)
{
  return nullptr;
}

void
pgbuf_cast_pgptr_to_iopgptr (char *, fileio_page *&)
{
}

void
pgbuf_unfix_debug (THREAD_ENTRY *thread_p, PAGE_PTR pgptr, const char *caller_file, int caller_line)
{
}

PAGE_PTR
pgbuf_fix_release (THREAD_ENTRY *thread_p, const VPID *vpid, PAGE_FETCH_MODE fetch_mode,
		   PGBUF_LATCH_MODE request_mode, PGBUF_LATCH_CONDITION condition)
{
  return nullptr;
}

#if defined(NDEBUG)
void
pgbuf_unfix (THREAD_ENTRY *thread_p, PAGE_PTR pgptr)
{
}
#endif
