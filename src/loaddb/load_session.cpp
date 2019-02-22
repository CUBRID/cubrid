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

#include "load_driver.hpp"
#include "load_server_loader.hpp"
#include "resource_shared_pool.hpp"
#include "thread_entry_task.hpp"
#include "xserver_interface.h"

#include <sstream>

namespace cubload
{

  void init_driver (driver *driver, session &session);

  bool invoke_parser (driver *driver, const batch &batch_);
}

namespace cubload
{

  void
  init_driver (driver *driver, session &session)
  {
    if (driver == NULL)
      {
	session.fail ();
	assert (false);
	return;
      }

    // avoid driver being initialized twice
    if (driver->is_initialized ())
      {
	return;
      }

    error_handler *error_handler_ = new error_handler (session);
    class_installer *cls_installer = new server_class_installer (session, *error_handler_);
    object_loader *obj_loader = new server_object_loader (session, *error_handler_);

    driver->initialize (cls_installer, obj_loader, error_handler_);
  }

  bool
  invoke_parser (driver *driver, const batch &batch_)
  {
    if (driver == NULL || !driver->is_initialized ())
      {
	return false;
      }

    driver->get_object_loader ().init (batch_.get_class_id ());
    driver->get_class_installer ().set_class_id (batch_.get_class_id ());

    // parse doc says that 0 is returned if parsing succeeds
    std::istringstream iss (batch_.get_content ());
    int parser_result = driver->parse (iss, batch_.get_line_offset ());

    return parser_result == 0;
  }

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
    public:
      loaddb_worker_context_manager (session &session, unsigned int pool_size)
	: m_session (session)
	, m_driver_pool (pool_size)
	, m_conn_entry (*cubthread::get_entry ().conn_entry)
	, m_interrupted (false)
      {
	//
      }

      ~loaddb_worker_context_manager () override = default;

      void on_create (cubthread::entry &context) override
      {
	driver *driver = m_driver_pool.claim ();
	init_driver (driver, m_session);

	context.m_loaddb_driver = driver;

	// save connection entry
	context.conn_entry = &m_conn_entry;
      }

      void on_retire (cubthread::entry &context) override
      {
	if (context.m_loaddb_driver == NULL)
	  {
	    return;
	  }

	m_driver_pool.retire (*context.m_loaddb_driver);

	context.m_loaddb_driver = NULL;
	context.conn_entry = NULL;
      }

      void stop_execution (cubthread::entry &context) override
      {
	if (m_interrupted)
	  {
	    xlogtb_set_interrupt (&context, true);
	  }
      }

      void interrupt ()
      {
	m_interrupted = true;
      }

    private:
      session &m_session;
      resource_shared_pool<driver> m_driver_pool;
      css_conn_entry &m_conn_entry;
      bool m_interrupted;
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
    public:
      load_worker () = delete; // Default c-tor: deleted.

      load_worker (const batch &batch, session &session)
	: m_batch (std::move (batch))
	, m_session (session)
      {
	//
      }

      void execute (cubthread::entry &thread_ref) final
      {
	if (m_session.is_failed ())
	  {
	    return;
	  }

	logtb_assign_tran_index (&thread_ref, NULL_TRANID, TRAN_ACTIVE, NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
				 TRAN_DEFAULT_ISOLATION_LEVEL ());

	driver *driver = thread_ref.m_loaddb_driver;
	bool parser_result = invoke_parser (driver, m_batch);

	if (m_session.is_failed () || !parser_result || er_has_error ())
	  {
	    // if a batch transaction was aborted then abort entire loaddb session
	    m_session.fail ();

	    xtran_server_abort (&thread_ref);
	  }
	else
	  {
	    // order batch commits, therefore wait until previous batch is committed
	    m_session.wait_for_previous_batch (m_batch.get_id ());

	    xtran_server_commit (&thread_ref, false);

	    // update load statistics after commit
	    m_session.stats_update_rows_committed (m_batch.get_rows_number ());
	    m_session.stats_update_last_committed_line (driver->get_scanner ().lineno () + 1);
	  }

	// free transaction index
	logtb_free_tran_index (&thread_ref, thread_ref.tran_index);

	// notify session that batch is done
	m_session.notify_batch_done (m_batch.get_id ());
      }

    private:
      batch m_batch;
      session &m_session;
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
    , m_class_registry ()
    , m_stats ()
    , m_stats_mutex ()
    , m_driver (NULL)
  {
    // start at least 2 loaddb threads
    unsigned int pool_size = std::max<unsigned int> (2, std::thread::hardware_concurrency ());

    std::string worker_pool_name ("loaddb-workers_session-");
    worker_pool_name.append (std::to_string (id));

    m_wp_context_manager = new loaddb_worker_context_manager (*this, pool_size);
    m_worker_pool = cubthread::get_manager ()->create_worker_pool (pool_size, pool_size, worker_pool_name.c_str (),
		    m_wp_context_manager, 1, false, true);

    m_driver = new driver ();
    init_driver (m_driver, *this);
  }

  session::~session ()
  {
    delete m_driver;

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

    m_stats.rows_failed++;
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
  session::interrupt ()
  {
    m_wp_context_manager->interrupt ();
    fail ();
  }

  void
  session::stats_update_rows_committed (int rows_committed)
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex);
    m_stats.rows_committed += rows_committed;
  }

  void
  session::stats_update_last_committed_line (int last_committed_line)
  {
    if (last_committed_line <= m_stats.last_committed_line)
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_stats_mutex);

    // check if again after lock was acquired
    if (last_committed_line <= m_stats.last_committed_line)
      {
	return;
      }

    m_stats.last_committed_line = last_committed_line;
  }

  void
  session::stats_update_current_line (int current_line)
  {
    update_atomic_value_with_max (m_stats.current_line, current_line);
  }

  template<typename T>
  void
  session::update_atomic_value_with_max (std::atomic<T> &atomic_val, T new_max)
  {
    int curr_max;

    do
      {
	curr_max = atomic_val.load ();
	if (curr_max >= new_max)
	  {
	    // max is already stored
	    break;
	  }
      }
    while (!atomic_val.compare_exchange_strong (curr_max, new_max));
  }

  class_registry &
  session::get_class_registry ()
  {
    return m_class_registry;
  }

  void
  session::notify_waiting_threads ()
  {
    m_commit_cond_var.notify_all ();
    m_completion_cond_var.notify_one ();
  }

  int
  session::install_class (cubthread::entry &thread_ref, const batch &batch)
  {
    thread_ref.m_loaddb_driver = m_driver;

    int error_code = NO_ERROR;
    bool parser_result = invoke_parser (m_driver, batch);

    if (is_failed () || !parser_result || er_has_error ())
      {
	fail ();

	error_code = er_errid_if_has_error ();
	if (error_code == NO_ERROR)
	  {
	    error_code = ER_FAILED;
	  }
      }

    thread_ref.m_loaddb_driver = NULL;

    return error_code;
  }

  int
  session::load_batch (cubthread::entry &thread_ref, const batch &batch)
  {
    update_atomic_value_with_max (m_max_batch_id, batch.get_id ());

    if (batch.get_content ().empty ())
      {
	// nothing to do, just notify that batch processing is done
	notify_batch_done (batch.get_id ());
	assert (false);
	return ER_FAILED;
      }

    cubthread::get_manager ()->push_task (m_worker_pool, new load_worker (batch, *this));

    return NO_ERROR;
  }

  int
  session::load_file (cubthread::entry &thread_ref, const std::string &file_name)
  {
    batch_handler b_handler = [this, &thread_ref] (const batch &batch)
    {
      return load_batch (thread_ref, batch);
    };

    batch_handler c_handler = [this, &thread_ref] (const batch &batch)
    {
      return install_class (thread_ref, batch);
    };

    return split (m_batch_size, file_name, c_handler, b_handler);
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
