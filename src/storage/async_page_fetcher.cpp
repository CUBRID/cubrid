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

#include "log_lsa.hpp"
#include "log_manager.h"
#include "log_reader.hpp"
#include "log_replication.hpp"
#include "page_buffer.h"
#include "page_server.hpp"
#include "thread_manager.hpp"

namespace cublog
{

  class log_page_fetch_task : public cubthread::entry_task
  {
    public:
      explicit log_page_fetch_task (LOG_PAGEID pageid, async_page_fetcher::log_page_callback_type &&callback)
	: m_logpageid (pageid), m_callback (std::move (callback))
      {
      }

      void execute (context_type &context) override;

    private:
      LOG_PAGEID m_logpageid;
      async_page_fetcher::log_page_callback_type m_callback;
  };

  class data_page_fetch_task : public cubthread::entry_task
  {
    public:
      explicit data_page_fetch_task (VPID vpid, const LOG_LSA &lsa, async_page_fetcher::data_page_callback_type &&callback)
	: m_vpid (vpid), m_lsa (lsa), m_callback (std::move (callback))
      {
      }

      void execute (context_type &context) override;

    private:
      VPID m_vpid;
      LOG_LSA m_lsa;
      async_page_fetcher::data_page_callback_type m_callback;
  };

  class log_boot_info_fetch_task : public cubthread::entry_task
  {
    public:
      explicit log_boot_info_fetch_task (const cublog::prior_sender::sink_hook_t &log_prior_sender_sink,
					 async_page_fetcher::log_boot_info_callback_type &&callback)
	: m_log_prior_sender_sink { log_prior_sender_sink }
	, m_callback { std::move (callback) }
      {
      }

      void execute (context_type &context) override;

    private:
      const cublog::prior_sender::sink_hook_t &m_log_prior_sender_sink;
      async_page_fetcher::log_boot_info_callback_type m_callback;
  };

  /*
   * implementations
   */

  void log_page_fetch_task::execute (context_type &context)
  {
    log_lsa loglsa {m_logpageid, 0};
    log_reader logreader { LOG_CS_SAFE_READER };

    if (m_logpageid == LOGPB_HEADER_PAGE_ID)
      {
	// Make sure log page header is updated
	logpb_force_flush_header_and_pages (&context);
      }
    int err = logreader.set_lsa_and_fetch_page (loglsa);
    m_callback (logreader.get_page (), err);
  }

  void data_page_fetch_task::execute (context_type &context)
  {
    if (!m_lsa.is_null ())
      {
	// TODO: FIXME
	// The transaction server boots and reads pages before initializing its log module and before knowing a safe target
	// LSA for replication. A way of knowing this target LSA is required, but disable this wait until that's fixed.
	ps_Gl.get_replicator ().wait_past_target_lsa (m_lsa);
      }

    PAGE_PTR page_ptr = pgbuf_fix (&context, &m_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);

    FILEIO_PAGE *io_pgptr = nullptr;
    pgbuf_cast_pgptr_to_iopgptr (page_ptr, io_pgptr);

    int error = io_pgptr != nullptr ? NO_ERROR : er_errid ();
    m_callback (io_pgptr, error);

    pgbuf_unfix (&context, page_ptr);
  }

  void log_boot_info_fetch_task::execute (context_type &context)
  {
    log_lsa append_lsa, prev_lsa, most_recent_trantable_snapshot_lsa;

    std::string message = log_pack_log_boot_info (&context, append_lsa, prev_lsa,
			  most_recent_trantable_snapshot_lsa, m_log_prior_sender_sink);

    m_callback (std::move (message));

    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
      {
	_er_log_debug (ARG_FILE_LINE,
		       "Sent log boot info to passive tran server with prev_lsa = (%lld|%d), append_lsa = (%lld|%d)"
		       " most_recent_trantable_snapshot_lsa = (%lld|%d)\n",
		       LSA_AS_ARGS (&prev_lsa), LSA_AS_ARGS (&append_lsa),
		       LSA_AS_ARGS (&most_recent_trantable_snapshot_lsa));
      }
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

  void async_page_fetcher::fetch_data_page (const VPID &vpid, const LOG_LSA repl_lsa, data_page_callback_type &&func)
  {
    data_page_fetch_task *const task = new data_page_fetch_task (vpid, repl_lsa, std::move (func));

    // Ownership is transfered to m_threads.
    m_threads->execute (task);
  }

  void async_page_fetcher::fetch_log_boot_info (const cublog::prior_sender::sink_hook_t &log_prior_sender_sink,
      log_boot_info_callback_type &&callback_func)
  {
    cubthread::entry_task *const task =
	    new log_boot_info_fetch_task (log_prior_sender_sink, std::move (callback_func));

    // ownership is transfered to the worker pool
    m_threads->execute (task);
  }
}