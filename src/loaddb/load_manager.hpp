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
 * load_manager.hpp - entry point for server side loaddb
 */

#ifndef _LOAD_MANAGER_HPP_
#define _LOAD_MANAGER_HPP_

#ident "$Id$"

#include "connection_defs.h"
#include "load_driver.hpp"
#include "resource_shared_pool.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

namespace cubload
{

  static const std::size_t DRIVER_POOL_SIZE = 1;

  // TODO CBRD-21654 add class documentation
  class manager
  {
    public:
      manager (const manager &copy) = delete;
      manager (manager &&copy) = delete;

      manager &operator= (const manager &other) = delete;
      manager &operator= (manager &&other) = delete;

      ~manager ();

      static manager &get_instance();

      void parse_batch (cubthread::entry &thread_ref, std::string &batch);
      int parse_file (cubthread::entry &thread_ref, std::string &file_name);

    private:
      friend class load_parse_task;

      using driver_pool_t = resource_shared_pool<driver>;

      manager ();

      driver_pool_t m_driver_pool;
      cubthread::entry_workpool *m_worker_pool;
  };

  class load_parse_task : public cubthread::entry_task
  {
    public:
      load_parse_task () = delete;

      load_parse_task (manager &manager, std::string &batch, CSS_CONN_ENTRY conn_entry)
	: m_manager (manager)
	, m_batch (std::move (batch))
	, m_conn_entry (conn_entry)
      {
	//
      }

      void execute (context_type &thread_ref) final;

    private:
      manager &m_manager;
      std::string m_batch;
      CSS_CONN_ENTRY m_conn_entry;
  };

} // namespace cubload

#endif /* _LOAD_MANAGER_HPP_ */
