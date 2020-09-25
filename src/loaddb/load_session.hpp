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
#include "load_class_registry.hpp"
#include "load_common.hpp"
#include "load_error_handler.hpp"
#include "thread_entry_task.hpp"
#include "utility.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace cubload
{

  class driver;

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
   *    session.load_file (*thread_p);
   *
   *    or
   *
   *    cubload::batch batch = "<batch>"; // get batch from client
   *    session.load_batch (*thread_p, batch);
   */
  class session
  {
    public:
      explicit session (load_args &args);
      ~session ();

      session (session &&other) = delete; // Move c-tor: deleted
      session (const session &copy) = delete; // Copy c-tor: deleted

      session &operator= (session &&other) = delete; // Move operator: deleted
      session &operator= (const session &copy) = delete; // Copy operator: deleted

      /*
       * Check and install a class from object file on the the server
       *
       *    return: NO_ERROR in case of success or a error code in case of failure.
       *    thread_ref(in): thread entry
       *    batch(in)     : a batch where content is a line starting with '%id' or '%class' from object file
       */
      int install_class (cubthread::entry &thread_ref, const batch &batch, bool &is_ignored, std::string &cls_name);

      /*
       * Load a batch from object file on the the server
       *
       *    return: NO_ERROR in case of success or a error code in case of failure.
       *    thread_ref(in): thread entry
       *    batch(in)     : a batch from loaddb object
       */
      int load_batch (cubthread::entry &thread_ref, const batch *batch, bool use_temp_batch, bool &is_batch_accepted,
		      load_status &status);

      void wait_for_completion ();
      void wait_for_previous_batch (batch_id id);
      void notify_batch_done (batch_id id);
      void notify_batch_done_and_register_tran_end (batch_id id, int tran_index);
      void register_tran_start (int tran_index);

      void on_error (std::string &err_msg);

      void fail (bool has_lock = false);
      bool is_failed ();
      void interrupt ();

      void fetch_status (load_status &status, bool has_lock = false);

      void stats_update_rows_committed (int64_t rows_committed);
      int64_t stats_get_rows_committed ();

      void stats_update_last_committed_line (int64_t last_committed_line);
      void stats_update_current_line (int64_t current_line);

      void update_class_statistics (cubthread::entry &thread_ref);
      const load_args &get_args ();

      class_registry &get_class_registry ();

      template<typename... Args>
      void append_log_msg (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

    private:
      void notify_waiting_threads ();
      bool is_completed ();
      void collect_stats ();

      template<typename T>
      void update_atomic_value_with_max (std::atomic<T> &atomic_val, T new_max);

      std::mutex m_mutex;
      std::condition_variable m_cond_var;
      std::set<int> m_tran_indexes;

      load_args m_args;
      batch_id m_last_batch_id;
      std::atomic<batch_id> m_max_batch_id;
      std::atomic<size_t> m_active_task_count;    // note: all decrements need to be protected by mutex

      class_registry m_class_registry;

      stats m_stats; // load db stats
      bool m_is_failed;
      std::vector<stats> m_collected_stats;

      driver *m_driver;

      cubthread::entry_task *m_temp_task;
  };

} // namespace cubload

// alias declaration for legacy C files
using load_session = cubload::session;

namespace cubload
{
  // Template implementation
  template<typename... Args>
  void
  session::append_log_msg (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
  {
    if (get_args ().verbose)
      {
	std::string log_msg = error_handler::format_log_msg (msg_id, std::forward<Args> (args)...);

	std::unique_lock<std::mutex> ulock (m_mutex);

	m_stats.log_message.append (log_msg);

	collect_stats ();
	ulock.unlock ();
	notify_waiting_threads ();
      }
  }
}

#endif /* _LOAD_SESSION_HPP_ */
