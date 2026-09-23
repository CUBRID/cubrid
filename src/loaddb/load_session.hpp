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
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class stream_session;
struct internal_lob_locator;

namespace cubload
{

  class driver;
  class internal_lob_load_ring;
  struct internal_lob_load_slot_table;

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

      const load_args &get_args ();

      class_registry &get_class_registry ();

      void set_client_type (int client_type);
      int get_client_type ();

      /*
       * Internal LOB payloads of a batch (STREAM_KIND_INTERNAL_LOB_LOAD, see load_internal_lob.hpp)
       *
       *    The first value of a batch starts the batch's worker, which writes every value into the class'
       *    Internal LOB file inside the batch transaction while the client is still sending; the batch text
       *    then reaches that same worker through load_batch.  A row refers to a value by its slot number
       *    (internal_lob_payload_make_value); while the row is inserted, the heap sink takes the chain and its
       *    replication bookkeeping over through internal_lob_consume_slot.
       */
      int internal_lob_stream_open (cubthread::entry &thread_ref, class_id clsid, batch_id id, DB_TYPE type,
				    DB_BIGINT data_length, DB_BIGINT logical_length, stream_session *&stream_out);
      int internal_lob_consume_slot (cubthread::entry &thread_ref, INT64 slot, const OID *class_oid,
				     DB_TYPE expected_type, internal_lob_locator &locator);
      void internal_lob_register_slot_table (int tran_index, internal_lob_load_slot_table *table);
      void internal_lob_unregister_slot_table (int tran_index);

      int internal_lob_payload_make_value (cubthread::entry &thread_ref, class_id clsid, const char *token_data,
					   size_t token_len, DB_TYPE expected_type, DB_BIGINT max_length,
					   DB_VALUE *value);

      template<typename... Args>
      void append_log_msg (MSGCAT_LOADDB_MSG msg_id, Args &&... args);

    private:
      void notify_waiting_threads ();
      bool is_completed ();
      void collect_stats ();
      internal_lob_load_slot_table *internal_lob_find_slot_table (int tran_index);

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

      std::atomic<int> m_load_client_type;	/* ADMIN_LOADDB_COMPAT_UNDER_11_2 or ADMIN_LOADDB_COMPAT_UNDER_11_4 */

      stats m_stats; // load db stats
      bool m_is_failed;
      std::vector<stats> m_collected_stats;

      driver *m_driver;

      cubthread::entry_task *m_temp_task;

      std::shared_ptr<internal_lob_load_ring> m_lob_ring;	// ring of the batch whose Internal LOB payloads are arriving
      batch_id m_lob_batch_id;
      INT64 m_lob_slot_count;
      std::unordered_map<int, internal_lob_load_slot_table *> m_lob_slot_tables;	// worker tran index -> payloads it wrote

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
