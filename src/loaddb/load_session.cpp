/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * load_session.cpp - entry point for server side loaddb
 */

#include "load_session.hpp"

#include "connection_defs.h"
#include "error_manager.h"
#include "load_driver.hpp"
#include "load_error_handler.hpp"
#include "load_server_loader.hpp"
#include "log_impl.h"
#include "resource_shared_pool.hpp"
#include "thread_entry_task.hpp"
#include "xserver_interface.h"

#include <cassert>
#include <sstream>

namespace cubload
{

  /*
   * cubload::loaddb_worker_context_manager
   *    extends cubthread::entry_manager
   *
   * description
   *    Thread entry manager for loaddb worker pool. Main functionality of the entry manager is to keep a pool of
   *    cubload::driver instances.
   *      on_create - a driver instance is claimed from the pool and assigned on thread ref
   *      on_retire - previously stored driver in thread ref, is retired to the pool
   */
  class loaddb_worker_context_manager : public cubthread::entry_manager
  {
    private:
      session &m_session;
      unsigned int m_pool_size;
      resource_shared_pool<driver> *m_driver_pool;

    public:
      loaddb_worker_context_manager (session &session, unsigned int pool_size)
	: m_session (session)
	, m_pool_size (pool_size)
	, m_driver_pool (NULL)
      {
	m_driver_pool = new resource_shared_pool<driver> (pool_size);
      }

      ~loaddb_worker_context_manager () override
      {
	delete m_driver_pool;
      }

      void on_create (cubthread::entry &context) override
      {
	driver *driver = m_driver_pool->claim ();
	if (driver == NULL)
	  {
	    m_session.fail ();
	    assert (false);
	    return;
	  }

	// avoid driver being initialized twice
	if (!driver->is_initialized ())
	  {
	    lineno_function line_func = [driver] { return driver->get_scanner ().lineno (); };
	    text_function text_func = [driver] { return driver->get_scanner ().YYText (); };
	    error_handler *error_handler_ = new error_handler (text_func, line_func);
	    error_handler_->initialize (&m_session);

	    loader *loader = new server_loader (m_session, *error_handler_);
	    driver->initialize (loader, error_handler_);
	  }

	context.m_loaddb_driver = driver;
      }

      void on_retire (cubthread::entry &context) override
      {
	if (context.m_loaddb_driver == NULL)
	  {
	    return;
	  }

	m_driver_pool->retire (*context.m_loaddb_driver);
	context.m_loaddb_driver = NULL;
      }
  };

  /*
   * cubload::load_worker
   *    extends cubthread::entry_task
   *
   * description
   *    Loaddb worker thread task, which does parsing and inserting of data rows within a transaction
   */
  class load_worker : public cubthread::entry_task
  {
    private:
      std::string m_batch;
      batch_id m_batch_id;

      session &m_session;
      css_conn_entry m_conn_entry;

    public:
      load_worker () = delete; // Default c-tor: deleted.

      load_worker (std::string &batch, batch_id id, session &session, css_conn_entry conn_entry)
	: m_batch (std::move (batch))
	, m_batch_id (id)
	, m_session (session)
	, m_conn_entry (conn_entry)
      {
	//
      }

      void execute (cubthread::entry &thread_ref) final
      {
	if (m_session.is_failed ())
	  {
	    return;
	  }

	// save connection entry
	thread_ref.conn_entry = &m_conn_entry;

	logtb_assign_tran_index (&thread_ref, NULL_TRANID, TRAN_ACTIVE, NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
				 TRAN_DEFAULT_ISOLATION_LEVEL ());

	// start parsing
	std::istringstream iss (m_batch);
	int ret = thread_ref.m_loaddb_driver->parse (iss); // parse documentation says that 0 is returned if parsing succeeds

	if (m_session.is_failed () || ret != 0 || er_errid_if_has_error () != NO_ERROR)
	  {
	    // if a batch transaction was aborted then abort entire loaddb session
	    m_session.fail ();

	    xtran_server_abort (&thread_ref);
	  }
	else
	  {
	    // order batch commits, therefore wait until previous batch is committed
	    m_session.wait_for_previous_batch (m_batch_id);

	    xtran_server_commit (&thread_ref, false);

	    // TODO fix last_commit assignment
	    //m_session.m_stats.last_commit = m_batch_id * m_session.m_batch_size;
	  }

	// free transaction index
	logtb_free_tran_index (&thread_ref, thread_ref.tran_index);

	// notify session that batch is done
	m_session.notify_batch_done (m_batch_id);
      }
  };

  session::session (SESSION_ID id)
    : m_commit_mutex ()
    , m_commit_cond_var ()
    , m_completion_mutex ()
    , m_completion_cond_var ()
    , m_batch_size (100000) // TODO CBRD-21654 get batch size from cub_admin loaddb
    , m_last_batch_id {NULL_BATCH_ID}
    , m_max_batch_id {NULL_BATCH_ID}
    , m_worker_pool (NULL)
    , m_wp_context_manager (NULL)
    , m_stats ()
    , m_stats_mutex ()
    , m_pool_size (0)
  {
    m_pool_size = std::max<unsigned int> (2, std::thread::hardware_concurrency ()); // start at least 2 loaddb threads

    std::string worker_pool_name ("loaddb-workers_session-");
    worker_pool_name.append (std::to_string (id));

    m_wp_context_manager = new loaddb_worker_context_manager (*this, m_pool_size);
    m_worker_pool = cubthread::get_manager ()->create_worker_pool (m_pool_size, m_pool_size, worker_pool_name.c_str (),
		    m_wp_context_manager, 1, false, true);
  }

  session::~session ()
  {
    cubthread::get_manager ()->destroy_worker_pool (m_worker_pool);
    delete m_wp_context_manager;
  }

  bool
  session::is_completed ()
  {
    return m_last_batch_id == m_max_batch_id;
  }

  void
  session::wait_for_previous_batch (const batch_id id)
  {
    auto pred = [this, &id] { return is_failed () || id == (m_last_batch_id + 1); };

    if (id == FIRST_BATCH_ID || pred ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_commit_mutex);
    m_commit_cond_var.wait (ulock, pred);
  }

  void
  session::wait_for_completion ()
  {
    auto pred = [this] { return is_failed () || is_completed (); };

    if (pred ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_completion_mutex);
    m_completion_cond_var.wait (ulock, pred);
  }

  void
  session::notify_batch_done (batch_id id)
  {
    if (!is_failed ())
      {
	m_last_batch_id = id;
      }

    notify_waiting_threads ();
  }

  void
  session::on_error (std::string &err_msg)
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex);

    m_stats.errors++;
    m_stats.error_message.append (err_msg);
  }

  void
  session::fail ()
  {
    if (m_stats.is_failed)
      {
	// already is failed
	return;
      }

    std::unique_lock<std::mutex> ulock (m_stats_mutex);

    // check if failed after lock was acquired
    if (m_stats.is_failed)
      {
	return;
      }

    m_stats.is_failed = true;

    // notify waiting threads that session was aborted
    notify_waiting_threads ();
  }

  bool
  session::is_failed ()
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex);
    return m_stats.is_failed;
  }

  void
  session::inc_total_objects ()
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex);
    m_stats.total_objects++;
  }

  void
  session::notify_waiting_threads ()
  {
    m_commit_cond_var.notify_all ();
    m_completion_cond_var.notify_one ();
  }

  int
  session::load_batch (cubthread::entry &thread_ref, std::string &batch, batch_id id)
  {
    batch_id current_max_id;

    do
      {
	current_max_id = m_max_batch_id.load ();
	if (current_max_id >= id)
	  {
	    // max is already stored
	    break;
	  }
      }
    while (!m_max_batch_id.compare_exchange_strong (current_max_id, id));

    if (batch.empty ())
      {
	// nothing to do, just notify that batch processing is done
	notify_batch_done (id);
	assert (false);
	return ER_FAILED;
      }

    cubthread::get_manager ()->push_task (m_worker_pool, new load_worker (batch, id, *this, *thread_ref.conn_entry));

    return NO_ERROR;
  }

  int
  session::load_file (cubthread::entry &thread_ref, std::string &file_name)
  {
    batch_handler handler = [this, &thread_ref] (std::string &batch, batch_id id)
    {
      return load_batch (thread_ref, batch, id);
    };

    return split (m_batch_size, file_name, handler);
  }

  stats
  session::get_stats ()
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex);

    m_stats.is_completed = is_completed ();
    stats copy = m_stats;

    // since client periodically fetches the stats, clear error_message in order not to send twice same message
    m_stats.error_message.clear ();

    return copy;
  }

} // namespace cubload
