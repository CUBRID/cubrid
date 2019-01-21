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

  // class_entry
  class_entry::class_entry (std::string &class_name, OID &class_oid, class_id clsid, int attr_count)
    : m_clsid (clsid)
    , m_class_oid (class_oid)
    , m_class_name (std::move (class_name))
    , m_attr_count (attr_count)
    , m_attr_count_checker (0)
    , m_attributes ()
  {
    //
  }

  void
  class_entry::register_attribute (ATTR_ID attr_id, std::string attr_name, or_attribute *attr_repr)
  {
    assert (m_attr_count_checker < m_attr_count);

    m_attributes.emplace_back (attr_id, attr_name, attr_repr);
    m_attr_count_checker++;
  }

  OID &
  class_entry::get_class_oid ()
  {
    return m_class_oid;
  }

  attribute &
  class_entry::get_attribute (int index)
  {
    // check that all attributes were registered
    assert (m_attr_count_checker == m_attr_count);

    // assert that index is within the range
    assert (0 <= index && ((std::size_t) index) < m_attributes.size ());

    return m_attributes[index];
  }

  // class_registry
  class_registry::class_registry ()
    : m_mutex ()
    , m_class_by_id ()
  {
    //
  }

  class_registry::~class_registry ()
  {
    for (auto &it : m_class_by_id)
      {
	delete it.second;
      }
    m_class_by_id.clear ();
  }

  class_entry *
  class_registry::register_class (const char *class_name, class_id clsid, OID class_oid, int attr_count)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    class_entry *c_entry = get_class_entry_without_lock (clsid);
    if (c_entry != NULL)
      {
	return c_entry;
      }

    std::string c_name (class_name);
    c_entry  = new class_entry (c_name, class_oid, clsid, attr_count);

    m_class_by_id.insert (std::make_pair (clsid, c_entry));

    return c_entry;
  }

  class_entry *
  class_registry::get_class_entry (class_id clsid)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    return get_class_entry_without_lock (clsid);
  }

  class_entry *
  class_registry::get_class_entry_without_lock (class_id clsid)
  {
    auto found = m_class_by_id.find (clsid);
    if (found != m_class_by_id.end ())
      {
	return found->second;
      }
    else
      {
	return NULL;
      }
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
	, m_driver_pool (pool_size + 1) // +1 in case when a driver will be required by session::load_batch function
	, m_conn_entry (*cubthread::get_entry ().conn_entry)
      {
	//
      }

      ~loaddb_worker_context_manager () override = default;

      driver *claim_driver ()
      {
	driver *driver = m_driver_pool.claim ();
	init_driver (driver);

	return driver;
      }

      void retire_driver (driver &claimed)
      {
	m_driver_pool.retire (claimed);
      }

      void on_create (cubthread::entry &context) override
      {
	context.m_loaddb_driver = claim_driver ();

	// save connection entry
	context.conn_entry = &m_conn_entry;
      }

      void on_retire (cubthread::entry &context) override
      {
	if (context.m_loaddb_driver == NULL)
	  {
	    return;
	  }

	retire_driver (*context.m_loaddb_driver);
	context.m_loaddb_driver = NULL;
	context.conn_entry = NULL;
      }

    private:
      session &m_session;
      resource_shared_pool<driver> m_driver_pool;
      css_conn_entry &m_conn_entry;

      void init_driver (driver *driver)
      {
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
	    error_handler *error_handler_ = new error_handler (line_func);
	    error_handler_->initialize (&m_session);

	    loader *loader = new server_loader (m_session, *error_handler_);
	    driver->initialize (loader, error_handler_);
	  }
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
    public:
      load_worker () = delete; // Default c-tor: deleted.

      load_worker (batch &batch, session &session)
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

	assert (thread_ref.m_loaddb_driver != NULL);

	logtb_assign_tran_index (&thread_ref, NULL_TRANID, TRAN_ACTIVE, NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
				 TRAN_DEFAULT_ISOLATION_LEVEL ());

	thread_ref.m_loaddb_driver->get_loader ().init (m_batch.m_clsid);

	// start parsing
	std::istringstream iss (m_batch.m_content);
	int ret = thread_ref.m_loaddb_driver->parse (iss); // parse doc says that 0 is returned if parsing succeeds

	if (m_session.is_failed () || ret != 0 || er_errid_if_has_error () != NO_ERROR)
	  {
	    // if a batch transaction was aborted then abort entire loaddb session
	    m_session.fail ();

	    xtran_server_abort (&thread_ref);
	  }
	else
	  {
	    // order batch commits, therefore wait until previous batch is committed
	    m_session.wait_for_previous_batch (m_batch.m_batch_id);

	    xtran_server_commit (&thread_ref, false);

	    // TODO fix last_commit assignment
	    //m_session.m_stats.last_commit = m_batch_id * m_session.m_batch_size;
	  }

	// free transaction index
	logtb_free_tran_index (&thread_ref, thread_ref.tran_index);

	// notify session that batch is done
	m_session.notify_batch_done (m_batch.m_batch_id);
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
  {
    // start at least 2 loaddb threads
    unsigned int pool_size = std::max<unsigned int> (2, std::thread::hardware_concurrency ());

    std::string worker_pool_name ("loaddb-workers_session-");
    worker_pool_name.append (std::to_string (id));

    m_wp_context_manager = new loaddb_worker_context_manager (*this, pool_size);
    m_worker_pool = cubthread::get_manager ()->create_worker_pool (pool_size, pool_size, worker_pool_name.c_str (),
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
  session::load_batch (cubthread::entry &thread_ref, batch &batch)
  {
    batch_id current_max_id;

    do
      {
	current_max_id = m_max_batch_id.load ();
	if (current_max_id >= batch.m_batch_id)
	  {
	    // max is already stored
	    break;
	  }
      }
    while (!m_max_batch_id.compare_exchange_strong (current_max_id, batch.m_batch_id));

    if (batch.m_content.empty ())
      {
	// nothing to do, just notify that batch processing is done
	notify_batch_done (batch.m_batch_id);
	assert (false);
	return ER_FAILED;
      }

    if (batch.m_handle_async)
      {
	// if batch.m_handle_async is true then batch content contains just objects so push a new task on worker pool
	cubthread::get_manager ()->push_task (m_worker_pool, new load_worker (batch, *this));
      }
    else
      {
	// if batch.m_handle_async is false then it means that we have batch content starting with '%class' or '%line'
	// and we need to register the class and return immediately. Execute the load_worker task on current thread
	thread_ref.m_loaddb_driver = m_wp_context_manager->claim_driver ();

	cubthread::get_manager ()->push_task (NULL, new load_worker (batch, *this));

	m_wp_context_manager->retire_driver (*thread_ref.m_loaddb_driver);
      }

    return NO_ERROR;
  }

  int
  session::load_file (cubthread::entry &thread_ref, std::string &file_name)
  {
    batch_handler handler = [this, &thread_ref] (batch &batch)
    {
      return load_batch (thread_ref, batch);
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
