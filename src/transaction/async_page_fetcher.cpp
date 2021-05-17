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

#include "async_page_fetcher.hpp"

#include "log_impl.h"
#include "thread_manager.hpp"
#include "page_buffer.h"

namespace cublog
{
  class log_page_fetch_task : public cubthread::entry_task
  {
    public:
      explicit log_page_fetch_task (LOG_PAGEID pageid, async_page_fetcher::log_page_callback_type &&callback)
	: m_logpageid (pageid), m_callback (std::move (callback))
      {
      }

      void execute (context_type &) override;

    private:
      LOG_PAGEID m_logpageid;
      async_page_fetcher::log_page_callback_type m_callback;
  };

  class data_page_fetch_task : public cubthread::entry_task
  {
    public:
      explicit data_page_fetch_task (VPID vpid, async_page_fetcher::data_page_callback_type &&callback)
	: m_vpid (vpid), m_callback (std::move (callback))
      {
      }

      void execute (context_type &) override;

    private:
      VPID m_vpid;
      async_page_fetcher::data_page_callback_type m_callback;
  };

  void log_page_fetch_task::execute (context_type &context)
  {
    log_lsa loglsa {m_logpageid, 0};
    log_reader logreader;

    int err = logreader.set_lsa_and_fetch_page (loglsa);
    m_callback (logreader.get_page (), err);
  }

  void data_page_fetch_task::execute (context_type &context)
  {
    // TODO: wait for replication
    PAGE_PTR page_ptr = pgbuf_fix (&context, &m_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);

    FILEIO_PAGE *io_pgptr = nullptr;
    cast_pgptr_to_iopgptr (io_pgptr, page_ptr);

    int error = io_pgptr != nullptr ? NO_ERROR : er_errid ();
    m_callback (io_pgptr, error); // TODO: Ilie - send page info from above.

    pgbuf_unfix (&context, page_ptr);
  }

  async_page_fetcher::async_page_fetcher ()
  {
    cubthread::manager *const thread_manager = cubthread::get_manager ();

    const auto thread_count = std::thread::hardware_concurrency ();
    m_worker_pool_context_manager.reset (
	    new cubthread::system_worker_entry_manager (TT_SYSTEM_WORKER));
    m_threads = thread_manager->create_worker_pool (thread_count, thread_count,
		"async_page_fetcher_worker_pool",
		m_worker_pool_context_manager.get (),
		thread_count, false /*debug_logging*/);

    assert (m_threads);
  }

  async_page_fetcher::~async_page_fetcher ()
  {
    cubthread::get_manager ()->destroy_worker_pool (m_threads);
  }

  void async_page_fetcher::fetch_log_page (LOG_PAGEID pageid, log_page_callback_type &&func)
  {
    log_page_fetch_task *const task = new log_page_fetch_task (pageid, std::move (func));

    // Ownership is transfered to m_threads.
    m_threads->execute (task);
  }

  void async_page_fetcher::fetch_data_page (const VPID &vpid, data_page_callback_type &&func)
  {
    data_page_fetch_task *const task = new data_page_fetch_task (vpid, std::move (func));

    // Ownership is transfered to m_threads.
    m_threads->execute (task);
  }
}
