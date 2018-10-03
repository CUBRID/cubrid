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
 * load_session.hpp - entry point for server side loaddb
 */

#ifndef _LOAD_SESSION_HPP_
#define _LOAD_SESSION_HPP_

#include "connection_defs.h"
#include "load_driver.hpp"
#include "load_server_loader.hpp"
#include "resource_shared_pool.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>

#define NULL_BATCH_ID -1
#define FIRST_BATCH_ID 1

namespace cubload
{

  static const std::size_t DRIVER_POOL_SIZE = 1;

  // forward declaration
  class session;

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
      load_worker (std::string &batch, int batch_id, session *session, css_conn_entry conn_entry);

      void execute (context_type &thread_ref) final;

    private:
      std::string m_batch;
      int m_batch_id;

      session *m_session;
      css_conn_entry m_conn_entry;
  };

  /*
   * cubload::session
   *
   * description
   *
   * how to use
   */
  class session
  {
    public:
      explicit session (SESSION_ID id);
      ~session ();

      session (session &&copy) = delete; // Move c-tor: deleted
      session (const session &copy) = delete; // Copy c-tor: deleted

      session &operator= (session &&other) = delete; // Move operator: deleted
      session &operator= (const session &other) = delete; // Copy operator: deleted

      /*
       * Load a batch from object file on the the server
       *
       *    return: void
       *    thread_ref(in): thread entry
       *    batch(in)     : a batch from loaddb object
       *    batch_id(in)  : id of the batch
       */
      void load_batch (cubthread::entry &thread_ref, std::string &batch, int batch_id);

      /*
       * Load object file entirely on the the server
       *
       *    return: NO_ERROR in case of success or ER_FAILED if file does not exists
       *    thread_ref(in)    : thread entry
       *    file_name(in)     : loaddb object file name (absolute path is required)
       *    total_batches(out): the total number of batches as of result of split operation
       */
      int load_file (cubthread::entry &thread_ref, std::string &file_name, int &total_batches);

      stats get_stats ();

      void wait_for_completion (int max_batch_id);

      void abort (std::string &&err_msg);
      bool aborted ();

      void inc_total_objects ();

    private:
      void notify_waiting_threads ();
      void notify_batch_done (int batch_id);
      void wait_for_previous_batch (int batch_id);

      friend class load_worker;

      std::mutex m_commit_mutex;
      std::condition_variable m_commit_cond_var;

      std::mutex m_completion_mutex;
      std::condition_variable m_completion_cond_var;

      std::atomic<bool> m_aborted;

      int m_batch_size;
      std::atomic_int m_last_batch_id;

      cubthread::entry_workpool *m_worker_pool;

      driver *m_drivers;
      resource_shared_pool<driver> *m_driver_pool;

      stats m_stats; // load db stats
  };

  /*
   * cubload::manager
   *
   * description
   *    This class serves as an entry point to server side loaddb functionality. The class is a singleton class.
   *    It has two main public function:
   *        * load_file : for parsing a loaddb object file directly on server if the file exists on the server machine
   *        * load_batch: when loaddb object file exists only on client machine, then client must send over
   *                       the network batches from file and then these batches will be parsed by the server
   *
   *    The file is splitted into batches or batches are received over the network, then each batch is delegated to a
   *    worker thread from a internal worker pool. The worker thread does the scanning/parsing and inserting of the data
   *
   * how to use
   *    cubload::manager &manager = cubload::manager::get_instance ();
   *
   *    std::string file_name = "<file>"; // the absolute path of the loaddb object file
   *    manager.load_file (*thread_p, file_name);
   *
   *    or
   *
   *    std::string batch = "<batch>"; // get batch from client
   *    manager.load_batch (*thread_p, batch);
   */
} // namespace cubload

#endif /* _LOAD_SESSION_HPP_ */
