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
 * replication_apply_db_copy.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_APPLY_DB_COPY_HPP_
#define _REPLICATION_APPLY_DB_COPY_HPP_

#include "cubstream.hpp"
#include "stream_entry_consumer.hpp"
#include "thread_manager.hpp"

#include <chrono>
#include <queue>

namespace cubcomm
{
  class channel;
}

namespace cubstream
{
  class multi_thread_stream;
  class stream_file;
  class transfer_receiver;
}

namespace cubreplication
{
  class node_definition;
  class copy_db_worker_task;
  class stream_entry;

  /* TODO : this is copied from log_consumer : refactor */
  class copy_db_consumer : public cubstream::stream_entry_consumer
  {
    public:
      /* TODO : thread requires a TDES, do we need to increase the number of TDES to account of this ? */
      static const int MAX_APPLIER_THREADS = 50;

      enum apply_phase
      {
	CLASS_SCHEMA,
	CLASS_HEAP,
	TRIGGER,
	INDEX,
	END
      };

    private:

      cubthread::entry_workpool *m_dispatch_workers_pool;

      bool m_is_stopped;
      bool m_is_finished;

    public:
      copy_db_consumer (const char *name, cubstream::multi_thread_stream *stream, size_t applier_threads)
	: cubstream::stream_entry_consumer (name, stream, applier_threads)
	, m_is_stopped (false)
	, m_is_finished (false)
	, m_last_fetched_position (0)
      {
      };

      ~copy_db_consumer () override;

      void start_dispatcher (void) override;
      void stop_dispatcher (void) override;

      void on_task_execution (void) override;

      bool is_stopping (void)
      {
	return m_is_stopped;
      }

      void set_is_finished ()
      {
	m_is_finished = true;
      }

      bool is_finished ()
      {
	return m_is_finished;
      }

      cubstream::stream_position m_last_fetched_position;
  };

  class apply_copy_context
  {
    public:
      apply_copy_context (node_definition *myself, node_definition *source_node);

      ~apply_copy_context ();

      int execute_copy ();

      void wait_replication_copy ();

    private:

      int setup_copy_protocol (cubcomm::channel &chn);

      node_definition *m_source_identity;
      node_definition *m_my_identity;

      cubstream::stream_position m_online_repl_start_pos;

      cubstream::multi_thread_stream *m_stream;
      cubstream::stream_file *m_stream_file;
      cubstream::transfer_receiver *m_transfer_receiver;
      copy_db_consumer *m_copy_consumer;

      std::chrono::system_clock::time_point m_start_time;
  };

} /* namespace cubreplication */

#endif /* _REPLICATION_APPLY_DB_COPY_HPP_ */
