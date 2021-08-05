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
#include "dbtype_def.h"
#include "fake_packable_object.hpp"
#include "log_reader.hpp"
#include "page_buffer.h"

#include <mutex>
#include <unordered_map>
#include <vector>

struct page_id_requirement
{
  bool require_data_page_valid;
  bool is_page_received;
};

struct data_page_fetcher_test_data
{
  std::unordered_map<int64_t, page_id_requirement> page_ids_requested;
  std::mutex map_mutex;
} g_data_page_fetcher_test_data;

int64_t get_key (VPID page_id)
{
  int64_t key = page_id.pageid;
  key = (key << (sizeof (short) * 8)) | page_id.volid;
  return key;
}

class test_env
{
  public:
    test_env (bool require_data_page_valid, const std::vector<VPID> &vpids);
    ~test_env ();

    void run_test ();

  private:
    void on_receive_data_page (VPID page_id, const FILEIO_PAGE *data_page, int error_code);

  private:
    std::vector<VPID> m_vpids;
    cublog::async_page_fetcher m_async_page_fetcher;
};

void do_test (test_env &env)
{
  env.run_test ();
}

TEST_CASE ("Test for data page fetcher", "")
{
  THREAD_ENTRY *thread_p = NULL;
  cubthread::initialize (thread_p);
  cubthread::initialize_thread_entries (); // + finalize

  SECTION ("1. Test with valid data pages returned")
  {
    std::vector<VPID> vpids { {1, 0}, {2, 0}, {3, 0} };
    test_env env (true, vpids);
    do_test (env);
  }

  SECTION ("2. Test with errors returned")
  {
    std::vector<VPID> vpids { {4, 0}, {5, 0}, {6, 0} };
    test_env env (false, vpids);
    do_test (env);
  }

  SECTION ("3. Test with a very big number of pages")
  {
    std::vector<VPID> vpids;
    for (auto i = 0; i < 10000; ++i)
      {
	vpids.push_back ({i, 0});
      }
    test_env env (true, vpids);
    do_test (env);
  }

  cubthread::finalize ();
}

test_env::test_env (bool require_data_page_valid, const std::vector<VPID> &vpids)
  : m_vpids (vpids)
{
  for (auto a_vpid : vpids)
    {
      page_id_requirement req;
      req.is_page_received = false;
      req.require_data_page_valid = require_data_page_valid;

      std::unique_lock<std::mutex> lock (g_data_page_fetcher_test_data.map_mutex);
      g_data_page_fetcher_test_data.page_ids_requested.insert (std::make_pair (get_key (a_vpid), req));
    }
}

test_env::~test_env ()
{
}

void
test_env::run_test ()
{
  for (auto a_vpid : m_vpids)
    {
      m_async_page_fetcher.fetch_data_page (
	      a_vpid,
	      log_lsa (),
	      std::bind (
		      &test_env::on_receive_data_page,
		      std::ref (*this),
		      a_vpid,
		      std::placeholders::_1,
		      std::placeholders::_2
	      )
      );
    }
}

void test_env::on_receive_data_page (VPID page_id, const FILEIO_PAGE *data_page, int error_code)
{
  std::unique_lock<std::mutex> lock (g_data_page_fetcher_test_data.map_mutex);

  if (g_data_page_fetcher_test_data.page_ids_requested[get_key (page_id)].require_data_page_valid)
    {
      REQUIRE (error_code == NO_ERROR);
      REQUIRE (data_page != nullptr);
      REQUIRE (data_page->prv.pageid == page_id.pageid);
      REQUIRE (data_page->prv.volid == page_id.volid);
    }
  else
    {
      REQUIRE (error_code != NO_ERROR);
      REQUIRE (data_page == nullptr);
    }
  g_data_page_fetcher_test_data.page_ids_requested[get_key (page_id)].is_page_received = true;
}

PAGE_PTR
create_dummy_data_page (const VPID a_vpid)
{
  FILEIO_PAGE *io_page = new FILEIO_PAGE ();
  io_page->prv.pageid = a_vpid.pageid;
  io_page->prv.volid = a_vpid.volid;

  PAGE_PTR page = reinterpret_cast<PAGE_PTR> (io_page);
  return page;
}

void
delete_page (PAGE_PTR page_ptr)
{
  FILEIO_PAGE *io_page = reinterpret_cast<FILEIO_PAGE *> (page_ptr);
  delete io_page;
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
logpb_fetch_page (THREAD_ENTRY *, const LOG_LSA *, LOG_CS_ACCESS_MODE, LOG_PAGE *)
{
  assert (false);
  return NO_ERROR;
}

void
logpb_flush_pages (THREAD_ENTRY *thread_p, const LOG_LSA *flush_lsa)
{
  assert (false);
}

void
logpb_fatal_error (THREAD_ENTRY *, bool, const char *, const int, const char *, ...)
{
  assert (false);
}

PAGE_PTR
pgbuf_fix_debug (THREAD_ENTRY *thread_p, const VPID *vpid, PAGE_FETCH_MODE fetch_mode, PGBUF_LATCH_MODE request_mode,
		 PGBUF_LATCH_CONDITION condition, const char *caller_file, int caller_line)
{
  if (g_data_page_fetcher_test_data.page_ids_requested[get_key (*vpid)].require_data_page_valid)
    {
      return create_dummy_data_page (*vpid);
    }
  else
    {
      er_set (0, "", 0, ER_FAILED, 0);
      return nullptr;
    }
}

void
pgbuf_cast_pgptr_to_iopgptr (PAGE_PTR page_ptr, FILEIO_PAGE *&io_page)
{
  io_page = reinterpret_cast<FILEIO_PAGE *> (page_ptr);
}

void
pgbuf_unfix_debug (THREAD_ENTRY *thread_p, PAGE_PTR pgptr, const char *caller_file, int caller_line)
{
  delete_page (pgptr);
}

#if defined(NDEBUG)
PAGE_PTR
pgbuf_fix_release (THREAD_ENTRY *thread_p, const VPID *vpid, PAGE_FETCH_MODE fetch_mode,
		   PGBUF_LATCH_MODE request_mode, PGBUF_LATCH_CONDITION condition)
{
  return pgbuf_fix_debug (thread_p, vpid, fetch_mode, request_mode, condition, nullptr, 0);
}

void
pgbuf_unfix (THREAD_ENTRY *thread_p, PAGE_PTR pgptr)
{
  pgbuf_unfix_debug (thread_p, pgptr, nullptr, 0);
}
#endif //NDEBUG
