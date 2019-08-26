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
  class copy_db_consumer
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

      cubstream::multi_thread_stream *m_stream;

      cubthread::entry_workpool *m_dispatch_workers_pool;

      cubthread::entry_workpool *m_applier_workers_pool;

      int m_applier_worker_threads_count;

      std::atomic<int> m_started_tasks;

      bool m_is_stopped;
      bool m_is_finished;

    public:
      copy_db_consumer () :
	m_stream (NULL),
	m_applier_workers_pool (NULL),
	m_applier_worker_threads_count (MAX_APPLIER_THREADS),
	m_started_tasks (0),
	m_is_stopped (false),
	m_is_finished (false),
	m_last_fetched_position (0)
      {
      };

      ~copy_db_consumer ();

      void start_daemons (void);
      void execute_task (copy_db_worker_task *task);

      void set_stream (cubstream::multi_thread_stream *stream)
      {
	m_stream = stream;
      }

      cubstream::multi_thread_stream *get_stream (void)
      {
	return m_stream;
      }

      void end_one_task (void)
      {
	m_started_tasks--;
      }

      int get_started_task (void)
      {
	return m_started_tasks;
      }

      void wait_for_tasks (void);

      bool is_stopping (void)
      {
	return m_is_stopped;
      }

      void set_stop (void);

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
