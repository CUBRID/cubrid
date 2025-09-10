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
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
	if (!m_was_session_notified)
	  {
	    notify_done ();
	  }
	delete &m_batch;
      }

      load_task (const batch &batch, session &session, css_conn_entry &conn_entry)
	: m_batch (batch)
	, m_session (session)
	, m_conn_entry (conn_entry)
	, m_was_session_notified (false)
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

	assert (driver != NULL &&!driver->is_initialized ());
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
	    notify_done ();
	    return;
	  }

	logtb_assign_tran_index (&thread_ref, NULL_TRANID, TRAN_ACTIVE, NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
				 TRAN_DEFAULT_ISOLATION_LEVEL ());
	int tran_index = thread_ref.tran_index;
	m_session.register_tran_start (tran_index);

	// Get the clientids from the session and set it on the current worker.
	LOG_TDES *session_tdes = log_Gl.trantable.all_tdes[m_conn_entry.get_tran_index ()];
	LOG_TDES *worker_tdes = log_Gl.trantable.all_tdes[tran_index];
	worker_tdes->client.set_ids (session_tdes->client);

	bool parser_result = invoke_parser (driver, m_batch);

	// Get the class name.
	std::string class_name = cls_entry->get_class_name ();

	// We need this to update the stats.
	int line_no = driver->get_scanner ().lineno ();

	// Get the inserted lines
	std::size_t rows_number = driver->get_object_loader ().get_rows_number ();

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

	    // update load statistics after commit
	    m_session.stats_update_rows_committed (rows_number);
	    m_session.stats_update_last_committed_line (line_no + 1);

	    MSGCAT_LOADDB_MSG msg_type;
	    if (m_session.get_args ().syntax_check)
	      {
		msg_type = LOADDB_MSG_INSTANCE_COUNT;
	      }
	    else
	      {
		msg_type = LOADDB_MSG_COMMITTED_INSTANCES;
	      }

	    m_session.append_log_msg (msg_type, class_name.c_str (), rows_number);
	  }

	// Clear the clientids.
	worker_tdes->client.reset ();

	// notify session that batch is done
	notify_done_and_tran_end (tran_index);
      }

    private:
      void notify_done ()
      {
	assert (!m_was_session_notified);
	m_session.notify_batch_done (m_batch.get_id ());
	m_was_session_notified = true;
      }

      void notify_done_and_tran_end (int tran_index)
      {
	assert (!m_was_session_notified);
	m_session.notify_batch_done_and_register_tran_end (m_batch.get_id (), tran_index);
	m_was_session_notified = true;
      }

      const batch &m_batch;
      session &m_session;
      css_conn_entry &m_conn_entry;
      bool m_was_session_notified;
  };

  session::session (load_args &args)
    : m_mutex ()
    , m_cond_var ()
    , m_tran_indexes ()
    , m_args (args)
    , m_last_batch_id {NULL_BATCH_ID}
    , m_max_batch_id {NULL_BATCH_ID}
    , m_active_task_count {0}
    , m_class_registry ()
    , m_stats ()
    , m_is_failed (false)
    , m_collected_stats ()
    , m_driver (NULL)
    , m_temp_task (NULL)
  {
    worker_manager_register_session (*this);

    m_driver = new driver ();
    init_driver (m_driver, *this);

    if (!m_args.table_name.empty ())
      {
	// just set class id to 1 since only one table can be specified as command line argument
	cubthread::entry &thread_ref = cubthread::get_entry ();

	{
	  const char *dot = NULL;
	  const char *class_name = NULL;
	  int len = 0;

	  class_name = m_args.table_name.c_str ();
	  len = STATIC_CAST (int, strlen (class_name));

	  dot = strchr (class_name, '.');
	  if (dot)
	    {
	      /* user specified name */

	      /* user name of user specified name */
	      len = STATIC_CAST (int, dot - class_name);
	      if (len >= DB_MAX_USER_LENGTH)
		{
		  m_driver->get_error_handler ().on_error (LOADDB_MSG_EXCEED_MAX_USER_LEN, DB_MAX_USER_LENGTH - 1);
		  return;
		}

	      /* class name of user specified name */
	      len = STATIC_CAST (int, strlen (dot + 1));
	    }

	  if (len >= DB_MAX_IDENTIFIER_LENGTH - DB_MAX_USER_LENGTH)
	    {
	      m_driver->get_error_handler ().on_error (LOADDB_MSG_EXCEED_MAX_LEN, DB_MAX_IDENTIFIER_LENGTH - DB_MAX_USER_LENGTH - 1);
	      return;
	    }
	}

	thread_ref.m_loaddb_driver = m_driver;
	m_driver->get_class_installer ().set_class_id (FIRST_CLASS_ID);
	m_driver->get_class_installer ().install_class (m_args.table_name.c_str ());
	thread_ref.m_loaddb_driver = NULL;
      }
  }

  session::~session ()
  {
    delete m_driver;

    worker_manager_unregister_session (*this);
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

    std::unique_lock<std::mutex> ulock (m_mutex);
    m_cond_var.wait (ulock, pred);
  }

  void
  session::wait_for_completion ()
  {
    auto pred = [this] () -> bool
    {
      // condition of finish and no active tasks
      return (is_failed () || is_completed ()) && (m_active_task_count == 0);
    };

    if (pred ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);
    m_cond_var.wait (ulock, pred);
  }

  void
  session::notify_batch_done (batch_id id)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    assert (m_active_task_count > 0);
    --m_active_task_count;
    if (!is_failed ())
      {
	assert (m_last_batch_id == id - 1);
	m_last_batch_id = id;
      }
    ulock.unlock ();
    notify_waiting_threads ();

    er_clear ();
  }

  void
  session::notify_batch_done_and_register_tran_end (batch_id id, int tran_index)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    // free transaction index
    logtb_free_tran_index (&cubthread::get_entry (), tran_index);

    assert (m_active_task_count > 0);
    --m_active_task_count;
    if (!is_failed ())
      {
	assert (m_last_batch_id == id - 1);
	m_last_batch_id = id;
      }
    if (m_tran_indexes.erase (tran_index) != 1)
      {
	assert (false);
      }
    collect_stats ();
    ulock.unlock ();
    notify_waiting_threads ();

    er_clear ();
  }

  void
  session::register_tran_start (int tran_index)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    auto ret = m_tran_indexes.insert (tran_index);
    assert (ret.second);    // it means it was inserted
  }

  void
  session::on_error (std::string &err_msg)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    m_stats.rows_failed++;
    m_stats.error_message.append (err_msg);
    collect_stats ();
    ulock.unlock ();
    notify_waiting_threads ();
  }

  void
  session::fail (bool has_lock)
  {
    std::unique_lock<std::mutex> ulock (m_mutex, std::defer_lock);
    if (!has_lock)
      {
	ulock.lock ();
      }

    // check if failed after lock was acquired
    if (m_is_failed)
      {
	return;
      }

    m_is_failed = true;
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
    return m_is_failed;
  }

  void
  session::interrupt ()
  {
    cubthread::entry *thread_p = &cubthread::get_entry ();
    std::unique_lock<std::mutex> ulock (m_mutex);
    for (auto &it : m_tran_indexes)
      {
	(void) logtb_set_tran_index_interrupt (thread_p, it, true);
      }
    fail (true);
    ulock.unlock ();
    notify_waiting_threads ();
  }

  void
  session::stats_update_rows_committed (int64_t rows_committed)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    m_stats.rows_committed += rows_committed;
  }

  int64_t
  session::stats_get_rows_committed ()
  {
    return m_stats.rows_committed;
  }

  void
  session::stats_update_last_committed_line (int64_t last_committed_line)
  {
    if (last_committed_line <= m_stats.last_committed_line)
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_mutex);

    // check if again after lock was acquired
    if (last_committed_line <= m_stats.last_committed_line)
      {
	return;
      }

    m_stats.last_committed_line = last_committed_line;
  }

  void
  session::stats_update_current_line (int64_t current_line)
  {
    update_atomic_value_with_max (m_stats.current_line, current_line);
  }

  template<typename T>
  void
  session::update_atomic_value_with_max (std::atomic<T> &atomic_val, T new_max)
  {
    int64_t curr_max;

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

  const load_args &
  session::get_args ()
  {
    return m_args;
  }

  void
  session::notify_waiting_threads ()
  {
    m_cond_var.notify_all ();
  }

  int
  session::install_class (cubthread::entry &thread_ref, const batch &batch, bool &is_ignored, std::string &cls_name)
  {
    thread_ref.m_loaddb_driver = m_driver;

    int error_code = NO_ERROR;
    bool parser_result = invoke_parser (m_driver, batch);
    const class_entry *cls_entry = get_class_registry ().get_class_entry (batch.get_class_id ());
    if (cls_entry != NULL)
      {
	is_ignored = cls_entry->is_ignored ();
	cls_name = cls_entry->get_class_name ();
      }
    else
      {
	is_ignored = false;
      }

    if (is_ignored)
      {
	thread_ref.m_loaddb_driver = NULL;

	return NO_ERROR;
      }

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
  session::load_batch (cubthread::entry &thread_ref, const batch *batch, bool use_temp_batch, bool &is_batch_accepted,
		       load_status &status)
  {
    if (is_failed ())
      {
	return ER_FAILED;
      }

    if (batch != NULL && batch->get_content ().empty ())
      {
	assert (false);
	return ER_FAILED;
      }

    cubthread::entry_task *task = NULL;
    if (use_temp_batch)
      {
	assert (m_temp_task != NULL && batch == NULL);
	task = m_temp_task;
      }
    else
      {
	assert (m_temp_task == NULL && batch != NULL);
	update_atomic_value_with_max (m_max_batch_id, batch->get_id ());

	task = new load_task (*batch, *this, *thread_ref.conn_entry);
      }

    std::unique_lock<std::mutex> ulock (m_mutex);
    auto pred = [&] () -> bool
    {
      is_batch_accepted = worker_manager_try_task (task);
      if (is_batch_accepted)
	{
	  ++m_active_task_count;
	  if (use_temp_batch)
	    {
	      m_temp_task = NULL;
	    }
	}
      else if (!use_temp_batch)
	{
	  m_temp_task = task;
	  use_temp_batch = true;
	}

      return !m_collected_stats.empty () || is_batch_accepted;
    };

    // if worker pool is full, but all jobs belong to other sessions, nobody will notify me when a job is finished.
    // loop & use timed waits instead of infinite wait
    while (true)
      {
	const std::chrono::milliseconds WAIT_MS { 10 };  // wakeup every 10 milliseconds

	if (m_cond_var.wait_for (ulock, WAIT_MS, pred))
	  {
	    break;
	  }
	// go back to waiting
      }

    fetch_status (status, true);

    return NO_ERROR;
  }

  void
  session::collect_stats ()
  {
    m_collected_stats.emplace_back (m_stats);

    // since client periodically fetches the stats, clear error_message in order not to send twice same message
    // However, for syntax checking we do not clear the messages since we throw the errors at the end
    if (!m_args.syntax_check)
      {
	m_stats.error_message.clear ();
      }
    m_stats.log_message.clear ();
  }

  void
  session::fetch_status (load_status &status, bool has_lock)
  {
    std::unique_lock<std::mutex> ulock (m_mutex, std::defer_lock);
    if (!has_lock)
      {
	ulock.lock ();
      }

    std::vector<stats> stats_;
    if (!m_collected_stats.empty ())
      {
	stats_ = std::move (m_collected_stats);
	assert (!stats_.empty ());
	assert (m_collected_stats.empty ());
      }

    status = load_status (is_completed (), is_failed (), stats_);

    if (!has_lock)
      {
	ulock.unlock ();
      }
  }

} // namespace cubload
