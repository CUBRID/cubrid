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
#include "load_worker_manager.hpp"
#include "resource_shared_pool.hpp"
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

    driver->get_object_loader ().destroy ();

    return parser_result == 0;
  }

  /*
   * cubload::load_worker
   *    extends cubthread::entry_task
   *
   * description
   *    Loaddb worker thread task, which does parsing and inserting of data rows within a transaction
   */
  class load_task : public cubthread::entry_task
  {
    public:
      load_task () = delete; // Default c-tor: deleted.

      ~load_task () override
      {
	delete &m_batch;
      }

      load_task (const batch &batch, session &session, css_conn_entry &conn_entry)
	: m_batch (batch)
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

	thread_ref.conn_entry = &m_conn_entry;
	driver *driver = thread_ref.m_loaddb_driver;

	assert (driver != NULL && !driver->is_initialized ());
	init_driver (driver, m_session);

	bool is_syntax_check_only = m_session.get_args ().syntax_check;
	const class_entry *cls_entry = m_session.get_class_registry ().get_class_entry (m_batch.get_class_id ());
	if (cls_entry == NULL)
	  {
	    if (!is_syntax_check_only)
	      {
		driver->get_error_handler ().on_failure_with_line (LOADDB_MSG_TABLE_IS_MISSING);
	      }
	    else
	      {
		driver->get_error_handler ().on_error_with_line (LOADDB_MSG_TABLE_IS_MISSING);
	      }

	    driver->clear ();
	    m_session.notify_batch_done (m_batch.get_id ());
	    return;
	  }

	logtb_assign_tran_index (&thread_ref, NULL_TRANID, TRAN_ACTIVE, NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
				 TRAN_DEFAULT_ISOLATION_LEVEL ());
	int tran_index = thread_ref.tran_index;
	m_session.register_tran_start (tran_index);

	bool parser_result = invoke_parser (driver, m_batch);

	// Get the class name.
	std::string class_name = cls_entry->get_class_name ();

	// We need this to update the stats.
	int line_no = driver->get_scanner ().lineno ();

	// Get the inserted lines
	int lines_inserted = driver->get_lines_inserted ();

	// We don't need anything from the driver anymore.
	driver->clear ();

	if (m_session.is_failed () || (!is_syntax_check_only && (!parser_result || er_has_error ())))
	  {
	    // if a batch transaction was aborted and syntax only is not enabled then abort entire loaddb session
	    m_session.fail ();

	    xtran_server_abort (&thread_ref);
	  }
	else
	  {
	    // order batch commits, therefore wait until previous batch is committed
	    m_session.wait_for_previous_batch (m_batch.get_id ());

	    xtran_server_commit (&thread_ref, false);

	    if (m_session.get_args ().syntax_check)
	      {
		m_session.append_log_msg (LOADDB_MSG_INSTANCE_COUNT, class_name.c_str (), m_batch.get_rows_number ());
	      }
	    else
	      {
		m_session.append_log_msg (LOADDB_MSG_COMMITTED_INSTANCES, class_name.c_str (),
					  m_batch.get_rows_number ());
	      }

	    // update load statistics after commit
	    m_session.stats_update_rows_committed (lines_inserted);
	    m_session.stats_update_last_committed_line (line_no + 1);

	    if (!m_session.get_args ().syntax_check)
	      {
		m_session.append_log_msg (LOADDB_MSG_UPDATED_CLASS_STATS, class_name.c_str ());
	      }
	  }

	// free transaction index
	logtb_free_tran_index (&thread_ref, thread_ref.tran_index);

	// notify session that batch is done
	m_session.notify_batch_done_and_register_tran_end (m_batch.get_id (), tran_index);
      }

    private:
      const batch &m_batch;
      session &m_session;
      css_conn_entry &m_conn_entry;
  };

  session::session (load_args &args)
    : m_commit_mutex ()
    , m_commit_cond_var ()
    , m_tran_indexes ()
    , m_args (args)
    , m_last_batch_id {NULL_BATCH_ID}
    , m_max_batch_id {NULL_BATCH_ID}
    , m_class_registry ()
    , m_stats ()
    , m_stats_mutex ()
    , m_driver (NULL)
  {
    worker_manager_register_session ();

    m_driver = new driver ();
    init_driver (m_driver, *this);

    if (!m_args.table_name.empty ())
      {
	// just set class id to 1 since only one table can be specified as command line argument
	cubthread::entry &thread_ref = cubthread::get_entry ();
	thread_ref.m_loaddb_driver = m_driver;
	m_driver->get_class_installer ().set_class_id (FIRST_CLASS_ID);
	m_driver->get_class_installer ().install_class (m_args.table_name.c_str ());
	thread_ref.m_loaddb_driver = NULL;
      }
  }

  session::~session ()
  {
    delete m_driver;

    worker_manager_unregister_session ();
  }

  bool
  session::is_completed ()
  {
    return m_last_batch_id == m_max_batch_id;
  }

  void
  session::wait_for_previous_batch (const batch_id id)
  {
    auto pred = [this, &id] () -> bool { return is_failed () || id == (m_last_batch_id + 1); };

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
    auto pred = [this] () -> bool { return is_failed () || is_completed (); };

    if (pred ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_commit_mutex);
    m_commit_cond_var.wait (ulock, pred);
  }

  void
  session::notify_batch_done (batch_id id)
  {
    if (is_failed ())
      {
	return;
      }
    m_commit_mutex.lock ();
    assert (m_last_batch_id == id - 1);
    m_last_batch_id = id;
    m_commit_mutex.unlock ();
    notify_waiting_threads ();
  }

  void
  session::notify_batch_done_and_register_tran_end (batch_id id, int tran_index)
  {
    if (is_failed ())
      {
	return;
      }
    m_commit_mutex.lock ();
    assert (m_last_batch_id == id - 1);
    m_last_batch_id = id;
    if (m_tran_indexes.erase (tran_index) != 1)
      {
	assert (false);
      }
    m_commit_mutex.unlock ();
    notify_waiting_threads ();
  }

  void
  session::register_tran_start (int tran_index)
  {
    m_commit_mutex.lock ();
    auto ret = m_tran_indexes.insert (tran_index);
    assert (ret.second);    // it means it was inserted
    m_commit_mutex.unlock ();
  }

  void
  session::on_error (std::string &err_msg)
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex);

    m_stats.rows_failed++;
    m_stats.error_message.append (err_msg);
  }

  void
  session::fail (bool has_lock)
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex, std::defer_lock);
    if (!has_lock)
      {
	ulock.lock ();
      }

    // check if failed after lock was acquired
    if (m_stats.is_failed)
      {
	return;
      }

    m_stats.is_failed = true;
    if (!has_lock)
      {
	ulock.unlock ();
	// notify waiting threads that session was aborted
	notify_waiting_threads ();
      }
    else
      {
	// caller should manage notifications too
      }
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
    THREAD_ENTRY *thread_p = &cubthread::get_entry ();
    std::unique_lock<std::mutex> ulock (m_commit_mutex);
    for (auto &it : m_tran_indexes)
      {
	(void) logtb_set_tran_index_interrupt (thread_p, it, true);
      }
    fail (true);
    ulock.unlock ();
    notify_waiting_threads ();
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

  void
  session::update_class_statistics (cubthread::entry &thread_ref)
  {
    if (m_args.disable_statistics)
      {
	return;
      }

    std::vector<const class_entry *> class_entries;
    m_class_registry.get_all_class_entries (class_entries);

    for (const class_entry *class_entry : class_entries)
      {
	OID *class_oid = const_cast<OID *> (&class_entry->get_class_oid ());
	xstats_update_statistics (&thread_ref, class_oid, STATS_WITH_SAMPLING);
      }
  }

  class_registry &
  session::get_class_registry ()
  {
    return m_class_registry;
  }

  const load_args &
  session::get_args ()
  {
    return m_args;
  }

  void
  session::notify_waiting_threads ()
  {
    m_commit_cond_var.notify_all ();
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
    delete &batch;

    return error_code;
  }

  int
  session::load_batch (cubthread::entry &thread_ref, const batch &batch)
  {
    if (is_failed ())
      {
	return ER_FAILED;
      }

    update_atomic_value_with_max (m_max_batch_id, batch.get_id ());

    if (batch.get_content ().empty ())
      {
	// nothing to do, just notify that batch processing is done
	notify_batch_done (batch.get_id ());
	assert (false);
	return ER_FAILED;
      }

    worker_manager_push_task (new load_task (batch, *this, *thread_ref.conn_entry));

    return NO_ERROR;
  }

  int
  session::load_file (cubthread::entry &thread_ref)
  {
    batch_handler b_handler = [this, &thread_ref] (const batch &batch) -> int
    {
      return load_batch (thread_ref, batch);
    };

    batch_handler c_handler = [this, &thread_ref] (const batch &batch) -> int
    {
      return install_class (thread_ref, batch);
    };

    return split (m_args.periodic_commit, m_args.server_object_file, c_handler, b_handler);
  }

  void
  session::fetch_stats (stats &stats_)
  {
    std::unique_lock<std::mutex> ulock (m_stats_mutex);

    m_stats.is_completed = is_completed ();
    stats_ = m_stats;

    // since client periodically fetches the stats, clear error_message in order not to send twice same message
    // However, for syntax checking we do not clear the messages since we throw the errors at the end
    if (!m_args.syntax_check)
      {
	m_stats.error_message.clear ();
      }
    m_stats.log_message.clear ();
  }

} // namespace cubload
