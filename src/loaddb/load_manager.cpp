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
 * load_manager.cpp - entry point for server side loaddb
 */

#include <cassert>
#include <sstream>

#include "load_manager.hpp"
#include "log_impl.h"
#include "thread_worker_pool.hpp"
#include "xserver_interface.h"

namespace cubload
{

  manager::manager ()
    : m_driver_pool (DRIVER_POOL_SIZE)
  {
    m_worker_pool = cubthread::get_manager ()->create_worker_pool (DRIVER_POOL_SIZE, DRIVER_POOL_SIZE, "loaddb_workers",
		    NULL, 1, false, true);
  }

  manager::~manager ()
  {
    cubthread::get_manager ()->destroy_worker_pool (m_worker_pool);
  }

  manager &
  manager::get_instance ()
  {
    static manager instance;
    return instance;
  }

  void
  manager::parse_batch (cubthread::entry &thread_ref, std::string &batch)
  {
    if (batch.empty ())
      {
	return;
      }

    m_worker_pool->execute (new load_parse_task (*this, batch, *thread_ref.conn_entry));
  }

  int
  manager::parse_file (cubthread::entry &thread_ref, std::string &file_name)
  {
    int batch_size = 100000; // TODO CBRD-21654 get batch size from cub_admin loaddb
    std::function<void (std::string &)> batch_handler = [this, &thread_ref] (std::string &batch)
    {
      parse_batch (thread_ref, batch);
    };

    return split (batch_size, file_name, batch_handler);
  }

  void
  load_parse_task::execute (cubthread::entry &thread_ref)
  {
    driver *driver = m_manager.m_driver_pool.claim ();

    if (driver == NULL)
      {
	assert (false);
	return;
      }

    // save connection entry
    thread_ref.conn_entry = &m_conn_entry;

    logtb_assign_tran_index (&thread_ref, NULL_TRANID, TRAN_ACTIVE, NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
			     TRAN_DEFAULT_ISOLATION_LEVEL ());

    std::istringstream iss (m_batch);
    driver->parse (iss);

    if (xtran_server_commit (&thread_ref, false) != TRAN_UNACTIVE_COMMITTED)
      {
	return;
      }

    // TODO CBRD-21654 xtran_server_abort in case of error

    logtb_free_tran_index (&thread_ref, thread_ref.tran_index);

    m_manager.m_driver_pool.retire (*driver);
  }
} // namespace cubload

