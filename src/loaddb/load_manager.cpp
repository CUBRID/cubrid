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
 * load_manager.cpp - entry point for loaddb manager
 */

#include "load_manager.hpp"

namespace cubload
{
  loaddb_worker_context_manager::loaddb_worker_context_manager (unsigned int pool_size)
    : m_driver_pool (pool_size)
    , m_interrupted (false)
  {
    //
  }

  void loaddb_worker_context_manager::on_create (cubthread::entry &context)
  {
    driver *driver = m_driver_pool.claim ();

    context.m_loaddb_driver = driver;
  }

  void loaddb_worker_context_manager::on_retire (cubthread::entry &context)
  {
    if (context.m_loaddb_driver == NULL)
      {
	return;
      }

    context.m_loaddb_driver->clear ();

    m_driver_pool.retire (*context.m_loaddb_driver);

    context.m_loaddb_driver = NULL;
    context.conn_entry = NULL;
  }

  void loaddb_worker_context_manager::stop_execution (cubthread::entry &context)
  {
    if (m_interrupted)
      {
	xlogtb_set_interrupt (&context, true);
      }
  }

  void loaddb_worker_context_manager::interrupt ()
  {
    m_interrupted = true;
  }
}
