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

  /*
   * cubload::manager
   *
   * description
   *    This class serves as an entry point to server side loaddb functionality. The class is a singleton class.
   *    It has two main public function:
   *        * parse_file : for parsing a loaddb object file directly on server if the file exists on the server machine
   *        * parse_batch: when loaddb object file exists only on client machine, then client must send over
   *                       the network batches from file and then these batches will be parsed by the server
   *
   *    The file is splitted into batches or batches are received over the network, then each batch is delegated to a
   *    worker thread from a internal worker pool. The worker thread does the scanning/parsing and inserting of the data
   *
   * how to use
   *    cubload::manager &manager = cubload::manager::get_instance ();
   *
   *    std::string file_name = "<file>"; // the absolute path of the loaddb object file
   *    manager.parse_file (*thread_p, file_name);
   *
   *    or
   *
   *    std::string batch = "<batch>"; // get batch from client
   *    manager.parse_batch (*thread_p, batch);
   */
  class manager
  {
    public:
      manager (manager &&copy) = delete; // Move c-tor: deleted
      manager (const manager &copy) = delete; // Copy c-tor: deleted

      manager &operator= (manager &&other) = delete; // Move operator: deleted
      manager &operator= (const manager &other) = delete; // Copy operator: deleted

      ~manager (); // Destructor

      /*
       * Get manager singleton instance
       */
      static manager &get_instance();

      /*
       * Parse a batch from loaddb object file on the the server
       *
       *    return: void
       *    thread_ref(in): thread entry
       *    batch(in)     : a batch from loaddb object
       */
      void parse_batch (cubthread::entry &thread_ref, std::string &batch);

      /*
       * Parse loaddb object file entirely on the the server
       *
       *    return: NO_ERROR in case of success or ER_FAILED if file does not exists
       *    thread_ref(in): thread entry
       *    file_name(in) : loaddb object file name (absolute path is required)
       */
      int parse_file (cubthread::entry &thread_ref, std::string &file_name);

    private:
      manager (); // Default c-tor

      friend class load_parse_task;

      using driver_pool_t = resource_shared_pool<driver>;

      driver_pool_t m_driver_pool;
      cubthread::entry_workpool *m_worker_pool;
  };

  /*
   * cubload::load_parse_task
   *    extends cubthread::entry_task
   *
   * description
   *    Loaddb worker thread task, which does parsing and inserting of data rows within a transaction
   */
  class load_parse_task : public cubthread::entry_task
  {
    public:
      load_parse_task () = delete; // Default c-tor: deleted.

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
