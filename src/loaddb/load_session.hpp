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

#include "dbtype_def.h"
#include "load_common.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace cubload
{

  const batch_id NULL_BATCH_ID = -1;
  const batch_id FIRST_BATCH_ID = 1;

  /*
   * cubload::session
   *
   * description
   *    This class serves as an entry point to server side loaddb functionality.
   *    It has two main public function:
   *        * load_file : for parsing a loaddb object file directly on server if the file exists on the server machine
   *        * load_batch: when loaddb object file exists only on client machine, then client must send over
   *                       the network batches from file and then these batches will be parsed by the server
   *
   *    The file is split into batches or batches are received over the network, then each batch is delegated to a
   *    worker thread from a internal worker pool. The worker thread does the scanning/parsing and inserting of the data
   *    Loaddb session is attached to database client session in session_state struct
   *
   * how to use
   *    cubload::session *session = NULL;
   *    session_get_loaddb_context (thread_p, session);
   *
   *    std::string file_name = "<file>"; // the absolute path of the loaddb object file
   *    session.load_file (*thread_p, file_name);
   *
   *    or
   *
   *    std::string batch = "<batch>"; // get batch from client
   *    session.load_batch (*thread_p, batch, batch_id);
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
       *    return: NO_ERROR in case of success or a error code in case of failure.
       *    thread_ref(in): thread entry
       *    batch(in)     : a batch from loaddb object
       *    id(in)        : id of the batch
       */
      int load_batch (cubthread::entry &thread_ref, std::string &batch, batch_id id);

      /*
       * Load object file entirely on the the server
       *
       *    return: NO_ERROR in case of success or ER_FAILED if file does not exists
       *    thread_ref(in)    : thread entry
       *    file_name(in)     : loaddb object file name (absolute path is required)
       */
      int load_file (cubthread::entry &thread_ref, std::string &file_name);

      void wait_for_completion ();
      void wait_for_previous_batch (batch_id id);
      void notify_batch_done (batch_id id);

      void abort ();
      void abort (std::string &&err_msg);
      bool is_aborted ();

      stats get_stats ();
      void inc_total_objects ();

    private:
      void notify_waiting_threads ();
      bool is_completed ();

      std::mutex m_commit_mutex;
      std::condition_variable m_commit_cond_var;

      std::mutex m_completion_mutex;
      std::condition_variable m_completion_cond_var;

      std::atomic<bool> m_aborted;

      int m_batch_size;
      std::atomic<batch_id> m_last_batch_id;
      std::atomic<batch_id> m_max_batch_id;

      cubthread::entry_workpool *m_worker_pool;
      cubthread::entry_manager *m_wp_context_manager;

      stats m_stats; // load db stats

      unsigned int m_pool_size;
  };

} // namespace cubload

// alias declaration for legacy C files
using load_session = cubload::session;

#endif /* _LOAD_SESSION_HPP_ */
