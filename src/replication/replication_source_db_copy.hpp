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
 * replication_source_db_copy.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_SOURCE_DB_COPY_HPP_
#define _REPLICATION_SOURCE_DB_COPY_HPP_

#include "cubstream.hpp"
#include "replication_object.hpp"
#include "thread_manager.hpp"

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>

namespace cubcomm
{
  class channel;
}

namespace cubstream
{
  class multi_thread_stream;
  class stream_file;
  class transfer_sender;
}

namespace cubreplication
{
  class multirow_object;
  class stream_senders_manager;

  /*
   * source_copy_context : server side context stored on transaction description
   * It holds objects required for
   *  - communication : stream (instance for db copy), stream transfer, stream file
   *  - storing of partial contructed objects (SBRs of schema)
   *
   * It centralizes the state of copy process on source server, required to drive the copy extraction process.
   * Depending on the state transition, it may append finalized SBRS objects to db copy stream.
   *
   */
  class source_copy_context
  {
    public:
      const size_t EXTRACT_HEAP_WORKER_POOL_SIZE = 20;

      enum copy_stage
      {
	NOT_STARTED = 0,
	SCHEMA_APPLY_CLASSES,
	SCHEMA_APPLY_CLASSES_FINISHED,
	SCHEMA_TRIGGERS_RECEIVED,
	SCHEMA_INDEXES_RECEIVED,
	SCHEMA_CLASSES_LIST_FINISHED,
	HEAP_COPY,
	HEAP_COPY_FINISHED,
	SCHEMA_APPLY_TRIGGERS,
	SCHEMA_APPLY_INDEXES
      };

      source_copy_context ();

      ~source_copy_context ();

      void pack_and_add_object (multirow_object *&obj);

      int execute_and_transit_phase (copy_stage new_state);

      void append_class_schema (const char *buffer, const size_t buf_size);
      void append_triggers_schema (const char *buffer, const size_t buf_size);
      void append_indexes_schema (const char *buffer, const size_t buf_size);
      void unpack_class_oid_list (const char *buffer, const size_t buf_size);

      void execute_db_copy (cubthread::entry &thread_ref, SOCKET fd);
      int setup_copy_protocol (cubcomm::channel &chn);

      int get_tran_index (void);
      void inc_error_cnt ();

      void inc_extract_running_thread ()
      {
	++m_running_extract_threads;
      }
      void dec_extract_running_thread ()
      {
	--m_running_extract_threads;
      }
      int get_extract_running_thread ()
      {
	return m_running_extract_threads;
      }

      cubstream::multi_thread_stream *get_stream () const
      {
	return m_stream;
      }
      void set_online_replication_start_pos (const cubstream::stream_position &pos)
      {
	m_online_replication_start_pos = pos;
      }

      void stop ();

    private:
      int wait_for_state (const copy_stage &desired_state);
      cubstream::multi_thread_stream *acquire_stream_for_copy ();
      void detach_stream_for_copy ();

      void pack_and_add_statement (const std::string &statement);
      void pack_and_add_start_of_extract_heap ();
      void pack_and_add_end_of_extract_heap ();
      void pack_and_add_end_of_copy ();

      int wait_slave_receive_ack (cubcomm::channel &chn);
      int wait_receive_class_list (void);
      int wait_send_triggers_indexes (void);

    private:
      int m_tran_index;
      int m_error_cnt;
      std::atomic<int> m_running_extract_threads;
      cubstream::stream_position m_online_replication_start_pos;

      cubstream::multi_thread_stream *m_stream;
      cubstream::stream_file *m_stream_file;
      cubstream::transfer_sender *m_transfer_sender;
      stream_senders_manager *m_senders_manager;
      cubthread::entry_workpool *m_heap_extract_workers_pool;

      std::string m_class_schema;
      std::string m_triggers;
      std::string m_indexes;
      std::list<OID> m_class_oid_list;

      copy_stage m_state;
      bool m_is_stop;

      std::mutex m_state_mutex;

      std::condition_variable m_state_cv;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_SOURCE_DB_COPY_HPP_ */
