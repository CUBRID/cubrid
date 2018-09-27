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

#include "error_manager.h"
#include "load_common.hpp"
#include "load_error_manager.hpp"
#include "log_impl.h"
#include "xserver_interface.h"

#include <cassert>
#include <sstream>

namespace cubload
{

  load_worker::load_worker (std::string &batch, int batch_id, session *session, css_conn_entry conn_entry)
    : m_batch (std::move (batch))
    , m_batch_id (batch_id)
    , m_session (session)
    , m_conn_entry (conn_entry)
  {
    //
  }

  void
  load_worker::execute (cubthread::entry &thread_ref)
  {
    if (m_session->aborted ())
      {
	return;
      }

    driver *driver = m_session->m_driver_pool->claim ();
    if (driver == NULL)
      {
	m_session->abort (""); // TODO CBRD-22254 add proper error id
	assert (false);
	return;
      }

    // save connection entry
    thread_ref.conn_entry = &m_conn_entry;

    logtb_assign_tran_index (&thread_ref, NULL_TRANID, TRAN_ACTIVE, NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
			     TRAN_DEFAULT_ISOLATION_LEVEL ());

    // start parsing
    std::istringstream iss (m_batch);
    int ret = driver->parse (iss); // parse documentation says that 0 is returned if parsing succeeds

    // once driver is not needed anymore, recycle it
    m_session->m_driver_pool->retire (*driver);

    if (m_session->aborted () || ret != 0 || er_errid_if_has_error () != NO_ERROR)
      {
	// if a batch transaction was aborted then abort entire loaddb session
	m_session->abort (""); // TODO CBRD-22254 add proper error id

	xtran_server_abort (&thread_ref);
      }
    else
      {
	// order batch commits, therefore wait until previous batch is committed
	m_session->wait_for_previous_batch (m_batch_id);

	xtran_server_commit (&thread_ref, false);

	m_session->m_stats.last_commit = m_batch_id * m_session->m_batch_size;
      }

    // free transaction index
    logtb_free_tran_index (&thread_ref, thread_ref.tran_index);

    // notify session that batch is done
    m_session->notify_batch_done (m_batch_id);
  }

  session::session (SESSION_ID id)
    : m_commit_mutex ()
    , m_commit_cond_var ()
    , m_completion_mutex ()
    , m_completion_cond_var ()
    , m_aborted {false}
    , m_batch_size (100000) // TODO CBRD-21654 get batch size from cub_admin loaddb
    , m_last_batch_id {NULL_BATCH_ID}
    , m_worker_pool (NULL)
    , m_drivers (NULL)
    , m_driver_pool (NULL)
    , m_stats {{0}, {0}, {0}, {0}}
  {
    void *raw_memory = operator new[] (DRIVER_POOL_SIZE * sizeof (driver));
    m_drivers = static_cast<driver *> (raw_memory);

    for (size_t i = 0; i < DRIVER_POOL_SIZE; ++i)
      {
	driver *driver_ptr = &m_drivers[i];
	server_loader *loader = new server_loader (this);
	new (driver_ptr) driver (loader);

	error_manager *err_mng = new error_manager (*this, driver_ptr->get_scanner ());
	loader->set_error_manager (err_mng);
      }

    m_driver_pool = new resource_shared_pool<driver> (m_drivers, DRIVER_POOL_SIZE);

    std::string worker_pool_name ("loaddb-workers_session-");
    worker_pool_name.append (std::to_string (id));

    m_worker_pool = cubthread::get_manager ()->create_worker_pool (DRIVER_POOL_SIZE, DRIVER_POOL_SIZE,
		    worker_pool_name.c_str (), NULL, 1, false, true);
  }

  session::~session ()
  {
    m_worker_pool->stop_execution ();
    cubthread::get_manager ()->destroy_worker_pool (m_worker_pool);

    delete m_driver_pool;

    for (size_t i = 0; i < DRIVER_POOL_SIZE; ++i)
      {
	m_drivers[i].~driver ();
      }
    operator delete[] (m_drivers);
  }

  void
  session::wait_for_previous_batch (const int batch_id)
  {
    auto pred = [this, &batch_id] { return aborted () || batch_id == (m_last_batch_id + 1); };

    if (batch_id == FIRST_BATCH_ID || pred ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_commit_mutex);
    m_commit_cond_var.wait (ulock, pred);
  }

  void
  session::wait_for_completion (const int max_batch_id)
  {
    auto pred = [this, &max_batch_id] { return aborted () || m_last_batch_id == max_batch_id; };

    if (pred ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_completion_mutex);
    m_completion_cond_var.wait (ulock, pred);
  }

  void
  session::notify_batch_done (int batch_id)
  {
    if (!aborted ())
      {
	m_last_batch_id = batch_id;
      }

    notify_waiting_threads ();
  }

  void
  session::abort (std::string &&err_msg)
  {
    if (m_aborted.exchange (true))
      {
	// already aborted
	return;
      }

    std::unique_lock<std::mutex> ulock (m_completion_mutex);

    m_stats.failures++;

    // notify waiting threads that session was aborted
    notify_waiting_threads ();
  }

  bool
  session::aborted ()
  {
    return m_aborted;
  }

  void
  session::inc_total_objects ()
  {
    m_stats.total_objects++;
  }

  void
  session::notify_waiting_threads ()
  {
    m_commit_cond_var.notify_all ();
    m_completion_cond_var.notify_one ();
  }

  void
  session::load_batch (cubthread::entry &thread_ref, std::string &batch, int batch_id)
  {
    if (batch.empty ())
      {
	// nothing to do, just notify that batch processing is done
	notify_batch_done (batch_id);
	return;
      }

    m_worker_pool->execute (new load_worker (batch, batch_id, this, *thread_ref.conn_entry));
  }

  int
  session::load_file (cubthread::entry &thread_ref, std::string &file_name, int &total_batches)
  {
    batch_handler handler = [this, &thread_ref] (std::string &batch, int batch_id)
    {
      load_batch (thread_ref, batch, batch_id);
    };

    return split (m_batch_size, file_name, handler, total_batches);
  }

} // namespace cubload
