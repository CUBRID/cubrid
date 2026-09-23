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
#include "heap_file.h"
#include "internal_lob_dml_session.hpp"
#include "internal_lob_file.hpp"
#include "load_internal_lob.hpp"
#include "load_server_loader.hpp"
#include "load_worker_manager.hpp"
#include "object_primitive.h"
#include "resource_shared_pool.hpp"
#include "session.h"
#include "xserver_interface.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>
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

  /* Internal LOB payloads of one batch, written by its worker before the batch text arrives. */
  struct internal_lob_load_slot
  {
    DB_TYPE type = DB_TYPE_NULL;
    DB_BIGINT data_length = 0;
    DB_BIGINT logical_length = 0;
    INTERNAL_LOB_LOCATOR locator;
    internal_lob_repl_tracking tracking;
    bool adopted = false;
  };

  struct internal_lob_load_slot_table
  {
    OID class_oid = OID_INITIALIZER;
    VFID lob_vfid = VFID_INITIALIZER;
    std::vector<internal_lob_load_slot> slots;
  };

  /* Ring between the request threads and a batch worker: 256 page-sized slots, 4 MB at the default page size. */
  static const std::size_t LOADDB_INTERNAL_LOB_RING_SLOTS = 256;

  static int
  load_internal_lob_set_error (const char *reason)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, reason);
    return ER_STREAM_SESSION_ERROR;
  }

  /*
   * cubload::load_worker
   *    extends cubthread::entry_task
   *
   * description
   *    Loaddb worker thread task, which does parsing and inserting of data rows within a transaction.
   *
   *    A batch with Internal LOB values starts its task before the batch text exists: the task first drains the
   *    values' payload from the batch ring into the class' Internal LOB file, inside the same transaction, and
   *    takes the batch text from that ring when the client sends it (see load_internal_lob.hpp).
   */
  class load_task : public cubthread::entry_task
  {
    public:
      load_task () = delete; // Default c-tor: deleted.

      ~load_task () override
      {
	if (m_lob_ring != NULL)
	  {
	    /* nobody consumes any longer: a request thread still pushing must not wait forever */
	    m_lob_ring->close (ER_LDR_INVALID_STATE);
	  }
	abort_internal_lob_writer ();
	if (!m_was_session_notified)
	  {
	    notify_done ();
	  }
	delete m_batch;
      }

      /* rows only: the batch text is already here */
      load_task (const batch &batch, session &session, css_conn_entry &conn_entry)
	: m_batch (&batch)
	, m_clsid (batch.get_class_id ())
	, m_batch_id (batch.get_id ())
	, m_lob_ring ()
	, m_lob_slots ()
	, m_lob_writer ()
	, m_lob_writer_active (false)
	, m_session (session)
	, m_conn_entry (conn_entry)
	, m_was_session_notified (false)
      {
	//
      }

      /* Internal LOB payloads first: they and, later, the batch text arrive through ring */
      load_task (class_id clsid, batch_id id, std::shared_ptr<internal_lob_load_ring> ring, session &session,
		 css_conn_entry &conn_entry)
	: m_batch (NULL)
	, m_clsid (clsid)
	, m_batch_id (id)
	, m_lob_ring (std::move (ring))
	, m_lob_slots ()
	, m_lob_writer ()
	, m_lob_writer_active (false)
	, m_session (session)
	, m_conn_entry (conn_entry)
	, m_was_session_notified (false)
      {
	//
      }

      /* the pool never took this task, so there is nothing to report to the session */
      void discard_unaccepted ()
      {
	m_was_session_notified = true;
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
	const class_entry *cls_entry = m_session.get_class_registry ().get_class_entry (m_clsid);
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

	int error_code = NO_ERROR;
	bool parser_result = false;
	int line_no = 0;
	std::size_t rows_number = 0;

	// Get the class name.
	std::string class_name = cls_entry->get_class_name ();

	if (m_lob_ring != NULL)
	  {
	    /* payload phase: write every Internal LOB value of the batch, then take the batch text */
	    m_lob_slots.class_oid = cls_entry->get_class_oid ();
	    error_code = receive_internal_lobs (thread_ref);
	    m_lob_ring->close (error_code);
	    if (error_code != NO_ERROR)
	      {
		report_internal_lob_failure ();
	      }
	  }

	if (error_code == NO_ERROR)
	  {
	    assert (m_batch != NULL);

	    m_session.internal_lob_register_slot_table (tran_index, &m_lob_slots);
	    parser_result = invoke_parser (driver, *m_batch);
	    m_session.internal_lob_unregister_slot_table (tran_index);

	    // We need this to update the stats.
	    line_no = driver->get_scanner ().lineno ();

	    // Get the inserted lines
	    rows_number = driver->get_object_loader ().get_rows_number ();

	    if (parser_result && !er_has_error () && !m_session.is_failed ())
	      {
		/* a payload no row took (a rejected row, a syntax-only run) must not survive the commit */
		error_code = delete_unadopted_internal_lobs (thread_ref);
	      }
	  }

	// We don't need anything from the driver anymore.
	driver->clear ();

	if (m_session.is_failed () || error_code != NO_ERROR
	    || (!is_syntax_check_only && (!parser_result || er_has_error ())))
	  {
	    // if a batch transaction was aborted and syntax only is not enabled then abort entire loaddb session
	    m_session.fail ();

	    xtran_server_abort (&thread_ref);
	  }
	else
	  {
	    // order batch commits, therefore wait until previous batch is committed
	    m_session.wait_for_previous_batch (m_batch_id);

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
	m_session.notify_batch_done (m_batch_id);
	m_was_session_notified = true;
      }

      void notify_done_and_tran_end (int tran_index)
      {
	assert (!m_was_session_notified);
	m_session.notify_batch_done_and_register_tran_end (m_batch_id, tran_index);
	m_was_session_notified = true;
      }

      /*
       * receive_internal_lobs () - Drain the batch ring: write each value's payload as it arrives, stop at the
       *                            batch text.
       *    return: NO_ERROR with m_batch set, or an error (the ring was closed, or a value could not be written)
       */
      int receive_internal_lobs (cubthread::entry &thread_ref)
      {
	internal_lob_load_ring::item item;
	const char *data = NULL;

	while (m_lob_ring->wait_front (item, data))
	  {
	    int error = NO_ERROR;

	    switch (item.kind)
	      {
	      case internal_lob_load_ring::ITEM_LOB_BEGIN:
		error = begin_internal_lob (thread_ref, item);
		break;
	      case internal_lob_load_ring::ITEM_DATA:
		error = append_internal_lob (thread_ref, item, data);
		break;
	      case internal_lob_load_ring::ITEM_LOB_END:
		error = end_internal_lob (thread_ref);
		break;
	      case internal_lob_load_ring::ITEM_LOB_ABORT:
		/* the client could not deliver this value; the batch cannot be completed */
		error = load_internal_lob_set_error ("internal LOB payload was aborted by the client");
		break;
	      case internal_lob_load_ring::ITEM_BATCH:
		m_batch = item.batch_p;
		m_lob_ring->pop_front ();
		if (m_lob_writer_active)
		  {
		    abort_internal_lob_writer ();
		    return load_internal_lob_set_error ("batch text arrived inside an internal LOB payload");
		  }
		return NO_ERROR;
	      }

	    m_lob_ring->pop_front ();
	    if (error != NO_ERROR)
	      {
		abort_internal_lob_writer ();
		return error;
	      }
	  }

	/* closed before the batch text came: the session was interrupted or the client went away */
	abort_internal_lob_writer ();
	return load_internal_lob_set_error ("internal LOB load batch was cancelled before its rows arrived");
      }

      int begin_internal_lob (cubthread::entry &thread_ref, const internal_lob_load_ring::item &item)
      {
	int error;

	if (m_lob_writer_active)
	  {
	    return load_internal_lob_set_error ("internal LOB payload started inside another payload");
	  }
	if ((item.lob_type != DB_TYPE_BLOB && item.lob_type != DB_TYPE_CLOB) || item.data_length < 0
	    || item.data_length > DB_MAX_INTERNAL_LOB_LENGTH || item.logical_length < 0
	    || (item.lob_type == DB_TYPE_CLOB && item.logical_length != item.data_length)
	    || (item.lob_type == DB_TYPE_BLOB
		&& !internal_lob_is_valid_blob_bit_length (item.data_length, item.logical_length)))
	  {
	    return load_internal_lob_set_error ("invalid internal LOB payload metadata");
	  }

	if (VFID_ISNULL (&m_lob_slots.lob_vfid))
	  {
	    HFID hfid;

	    if (heap_get_class_info (&thread_ref, &m_lob_slots.class_oid, &hfid, NULL, NULL) != NO_ERROR)
	      {
		ASSERT_ERROR_AND_SET (error);
		return error;
	      }
	    if (!heap_internal_lob_find_vfid (&thread_ref, &hfid, &m_lob_slots.lob_vfid, true))
	      {
		ASSERT_ERROR_AND_SET (error);
		return error;
	      }
	  }

	internal_lob_load_slot slot;
	slot.type = item.lob_type;
	slot.data_length = item.data_length;
	slot.logical_length = item.logical_length;
	m_lob_slots.slots.push_back (slot);

	error = internal_lob_repl_tracking_mark (&thread_ref, m_lob_slots.slots.back ().tracking);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
	error = internal_lob_reverse_insert_begin (&thread_ref, m_lob_slots.lob_vfid, item.lob_type, item.data_length,
		item.logical_length, m_lob_writer);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
	m_lob_writer_active = true;
	return NO_ERROR;
      }

      int append_internal_lob (cubthread::entry &thread_ref, const internal_lob_load_ring::item &item,
			       const char *data)
      {
	if (!m_lob_writer_active || item.data_size <= 0 || data == NULL)
	  {
	    return load_internal_lob_set_error ("internal LOB payload piece outside a value");
	  }
	return internal_lob_reverse_insert_append (&thread_ref, m_lob_writer, item.offset,
	       oos_buffer (const_cast<char *> (data), (std::size_t) item.data_size));
      }

      int end_internal_lob (cubthread::entry &thread_ref)
      {
	int error;
	int tracking_error;

	if (!m_lob_writer_active)
	  {
	    return load_internal_lob_set_error ("internal LOB payload ended outside a value");
	  }

	internal_lob_load_slot &slot = m_lob_slots.slots.back ();
	error = internal_lob_reverse_insert_end (&thread_ref, m_lob_writer, slot.locator);
	tracking_error = internal_lob_repl_tracking_capture (&thread_ref, slot.tracking);
	if (error != NO_ERROR || tracking_error != NO_ERROR)
	  {
	    internal_lob_reverse_insert_abort (m_lob_writer);
	  }
	m_lob_writer_active = false;
	return error != NO_ERROR ? error : tracking_error;
      }

      void abort_internal_lob_writer ()
      {
	if (m_lob_writer_active)
	  {
	    internal_lob_reverse_insert_abort (m_lob_writer);
	    m_lob_writer_active = false;
	  }
      }

      int delete_unadopted_internal_lobs (cubthread::entry &thread_ref)
      {
	for (internal_lob_load_slot &slot : m_lob_slots.slots)
	  {
	    if (slot.adopted)
	      {
		continue;
	      }

	    int error = internal_lob_delete (&thread_ref, m_lob_slots.lob_vfid, slot.locator);
	    if (error != NO_ERROR)
	      {
		return error;
	      }
	    slot.tracking.oids.clear ();
	    slot.tracking.lsas.clear ();
	  }
	return NO_ERROR;
      }

      /* the payload phase failed on this thread: the client only sees the session statistics */
      void report_internal_lob_failure ()
      {
	const char *reason = er_msg ();
	std::string msg = "internal LOB payload of batch ";

	msg.append (std::to_string ((long long) m_batch_id));
	msg.append (" failed: ");
	msg.append (reason != NULL ? reason : "unknown error");
	msg.push_back ('\n');
	m_session.on_error (msg);
      }

      const batch *m_batch;
      class_id m_clsid;
      batch_id m_batch_id;
      std::shared_ptr<internal_lob_load_ring> m_lob_ring;
      internal_lob_load_slot_table m_lob_slots;
      INTERNAL_LOB_REVERSE_WRITER m_lob_writer;
      bool m_lob_writer_active;
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
    , m_load_client_type (DB_CLIENT_TYPE_LOADDB_UTILITY)
    , m_stats ()
    , m_is_failed (false)
    , m_collected_stats ()
    , m_driver (NULL)
    , m_temp_task (NULL)
    , m_lob_ring ()
    , m_lob_batch_id (NULL_BATCH_ID)
    , m_lob_slot_count (0)
    , m_lob_slot_tables ()
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

    {
      std::unique_lock<std::mutex> ulock (m_mutex);
      if (m_lob_ring != NULL)
	{
	  /* the client left a batch's Internal LOB payload without its rows: that batch can never complete */
	  fail (true);
	  m_lob_ring.reset ();
	}
    }

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
    if (m_lob_ring != NULL)
      {
	/* a worker waiting for payload and a request thread waiting for ring space must both give up */
	m_lob_ring->close (ER_LDR_INVALID_STATE);
      }
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

  int
  session::get_client_type ()
  {
    return m_load_client_type.load ();
  }

  void
  session::set_client_type (int client_type)
  {
    m_load_client_type.store (client_type);
  }

  int
  session::internal_lob_payload_make_value (cubthread::entry &thread_ref, class_id clsid, const char *token_data,
      size_t token_len, DB_TYPE expected_type, DB_BIGINT max_length,
      DB_VALUE *value)
  {
    std::string token_string;
    char *endptr = NULL;
    long long parsed_slot;
    const class_entry *cls_entry = NULL;
    internal_lob_load_slot_table *table = NULL;

    (void) max_length;

    if (token_data == NULL || token_len == 0 || value == NULL || (expected_type != DB_TYPE_BLOB
	&& expected_type != DB_TYPE_CLOB))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	return ER_GENERIC_ERROR;
      }

    token_string.assign (token_data, token_len);
    parsed_slot = strtoll (token_string.c_str (), &endptr, 10);
    if (endptr == token_string.c_str () || *endptr != '\0' || parsed_slot <= 0)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	return ER_GENERIC_ERROR;
      }

    cls_entry = m_class_registry.get_class_entry (clsid);
    table = internal_lob_find_slot_table (thread_ref.tran_index);
    if (cls_entry == NULL || table == NULL || !OID_EQ (&cls_entry->get_class_oid (), &table->class_oid))
      {
	return load_internal_lob_set_error ("internal LOB payload does not belong to this batch");
      }
    if (parsed_slot > (long long) table->slots.size () || table->slots[parsed_slot - 1].type != expected_type)
      {
	return load_internal_lob_set_error ("internal LOB payload slot does not match the column");
      }

    /* the chain is taken over while the row is inserted, see internal_lob_consume_slot () */
    return internal_lob_make_load_slot_db_value (value, expected_type, (INT64) parsed_slot);
  }

  int
  session::internal_lob_consume_slot (cubthread::entry &thread_ref, INT64 slot, const OID *class_oid,
				      DB_TYPE expected_type, internal_lob_locator &locator)
  {
    internal_lob_load_slot_table *table = internal_lob_find_slot_table (thread_ref.tran_index);
    int error;

    if (table == NULL || class_oid == NULL)
      {
	return load_internal_lob_set_error ("internal LOB payload does not belong to this batch");
      }
    if (slot <= 0 || slot > (INT64) table->slots.size () || !OID_EQ (class_oid, &table->class_oid))
      {
	return load_internal_lob_set_error ("internal LOB payload slot does not belong to this class");
      }

    internal_lob_load_slot &entry = table->slots[slot - 1];
    if (entry.type != expected_type)
      {
	return load_internal_lob_set_error ("internal LOB payload slot does not match the column");
      }
    if (entry.adopted)
      {
	return load_internal_lob_set_error ("internal LOB payload slot is already used by another row");
      }

    /* the chunks were published when the worker wrote them; publish them again for this row's replication */
    error = internal_lob_repl_tracking_restore (&thread_ref, entry.tracking);
    if (error != NO_ERROR)
      {
	return error;
      }

    locator = entry.locator;
    locator.adopted = false;
    entry.adopted = true;
    return NO_ERROR;
  }

  void
  session::internal_lob_register_slot_table (int tran_index, internal_lob_load_slot_table *table)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    m_lob_slot_tables[tran_index] = table;
  }

  void
  session::internal_lob_unregister_slot_table (int tran_index)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    m_lob_slot_tables.erase (tran_index);
  }

  internal_lob_load_slot_table *
  session::internal_lob_find_slot_table (int tran_index)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    auto it = m_lob_slot_tables.find (tran_index);
    return it == m_lob_slot_tables.end () ? NULL : it->second;
  }

  /*
   * session::internal_lob_stream_open () - STREAM_KIND_INTERNAL_LOB_LOAD: one Internal LOB value of batch id starts.
   *    The first value of a batch starts the batch's worker, which writes the payload while the client is still
   *    sending; the batch text later reaches that worker through load_batch ().
   */
  int
  session::internal_lob_stream_open (cubthread::entry &thread_ref, class_id clsid, batch_id id, DB_TYPE type,
				     DB_BIGINT data_length, DB_BIGINT logical_length, stream_session *&stream_out)
  {
    std::shared_ptr<internal_lob_load_ring> ring;
    INT64 slot;
    int error;

    stream_out = NULL;

    {
      std::unique_lock<std::mutex> ulock (m_mutex);

      if (is_failed ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_INVALID_STATE, 0);
	  return ER_LDR_INVALID_STATE;
	}

      if (m_lob_ring == NULL)
	{
	  if (id <= m_max_batch_id || m_class_registry.get_class_entry (clsid) == NULL)
	    {
	      return load_internal_lob_set_error ("internal LOB payload does not belong to the next batch");
	    }

	  try
	    {
	      ring = std::make_shared<internal_lob_load_ring> (LOADDB_INTERNAL_LOB_RING_SLOTS, (std::size_t) DB_PAGESIZE);
	    }
	  catch (const std::bad_alloc &)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      LOADDB_INTERNAL_LOB_RING_SLOTS * (std::size_t) DB_PAGESIZE);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  load_task *task = new load_task (clsid, id, ring, *this, *thread_ref.conn_entry);
	  if (task == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (load_task));
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  // like load_batch: the pool may be full of other sessions' jobs, which never notify us, so poll
	  while (!worker_manager_try_task (task))
	    {
	      if (is_failed ())
		{
		  task->discard_unaccepted ();
		  delete task;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_INVALID_STATE, 0);
		  return ER_LDR_INVALID_STATE;
		}

	      const std::chrono::milliseconds WAIT_MS { 10 };
	      m_cond_var.wait_for (ulock, WAIT_MS);
	    }

	  ++m_active_task_count;
	  update_atomic_value_with_max (m_max_batch_id, id);
	  m_lob_ring = ring;
	  m_lob_batch_id = id;
	  m_lob_slot_count = 0;
	}
      else if (m_lob_batch_id != id)
	{
	  return load_internal_lob_set_error ("internal LOB payload belongs to another batch");
	}
      else
	{
	  ring = m_lob_ring;
	}

      slot = ++m_lob_slot_count;
    }

    internal_lob_load_ring::item begin;
    begin.kind = internal_lob_load_ring::ITEM_LOB_BEGIN;
    begin.lob_type = type;
    begin.data_length = data_length;
    begin.logical_length = logical_length;
    error = ring->push (begin, NULL);
    if (error != NO_ERROR)
      {
	return error;
      }

    stream_out = new internal_lob_load_stream_session (ring, slot);
    if (stream_out == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		sizeof (internal_lob_load_stream_session));
	return ER_OUT_OF_VIRTUAL_MEMORY;
      }
    return NO_ERROR;
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

    if (!use_temp_batch && batch != NULL)
      {
	std::shared_ptr<internal_lob_load_ring> ring;

	{
	  std::unique_lock<std::mutex> ulock (m_mutex);
	  if (m_lob_ring != NULL)
	    {
	      if (m_lob_batch_id != batch->get_id ())
		{
		  delete batch;
		  return load_internal_lob_set_error ("batch text does not belong to the internal LOB payload batch");
		}
	      ring = m_lob_ring;
	      m_lob_ring.reset ();
	    }
	}

	if (ring != NULL)
	  {
	    /* the batch's worker is already running: the text follows the payload through its ring */
	    internal_lob_load_ring::item text;

	    text.kind = internal_lob_load_ring::ITEM_BATCH;
	    text.batch_p = batch;
	    int error_code = ring->push (text, NULL);
	    if (error_code != NO_ERROR)
	      {
		delete batch;
		return error_code;
	      }

	    is_batch_accepted = true;
	    fetch_status (status);
	    return NO_ERROR;
	  }
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

    status = load_status (get_client_type (), is_completed (), is_failed (), stats_);

    if (!has_lock)
      {
	ulock.unlock ();
      }
  }

} // namespace cubload
