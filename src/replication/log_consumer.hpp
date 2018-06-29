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
 * log_consumer.hpp
 */

#ident "$Id$"

#ifndef _LOG_CONSUMER_HPP_
#define _LOG_CONSUMER_HPP_

#include "packing_stream.hpp"
#include "replication_stream_entry.hpp"
#include "thread_daemon.hpp"
#include "thread_task.hpp"
#include "thread_manager.hpp"
#include <chrono>
#include <cstddef>
#include <queue>

namespace cubreplication
{
  /*
   * main class for consuming log packing stream entries;
   * it should be created only as a global instance
   */
  class repl_applier_worker_context_manager;
  class repl_applier_worker_task;

  class log_consumer
  {
      friend class prepare_stream_entry_task;
      friend class apply_stream_entry_task;
    private:
      std::queue<replication_stream_entry *> m_stream_entries;

      cubstream::packing_stream *m_stream;

      /* start append position */
      cubstream::stream_position m_start_position;

      static log_consumer *global_log_consumer;

      std::mutex m_queue_mutex;

      cubthread::daemon *m_prepare_daemon;

      cubthread::daemon *m_apply_daemon;

      cubthread::entry_workpool *m_applier_workers_pool;

      repl_applier_worker_context_manager *m_repl_applier_worker_context_manager;

      int m_applier_worker_threads_count;

      bool m_use_daemons;

      std::atomic<int> m_started_tasks;

      std::mutex m_apply_task_mutex;
      std::condition_variable m_apply_task_cv;
      bool m_apply_task_ready;

      bool m_is_stopped;

    protected:
      void wait_apply_ready (void)
      {
	std::unique_lock<std::mutex> local_lock (m_apply_task_mutex);
	m_apply_task_ready = false;
	m_apply_task_cv.wait_for (local_lock,
				  std::chrono::milliseconds (1000),
				  [this] { return m_apply_task_ready == true;});
      }

      void signal_apply_ready (void)
      {
	std::lock_guard<std::mutex> local_lock (m_apply_task_mutex);
	m_apply_task_ready = true;
	m_apply_task_cv.notify_one ();
      }

    public:

      log_consumer () :
	m_applier_worker_threads_count (100),
	m_use_daemons (false),
	m_started_tasks (0),
	m_is_stopped (false)
      {
      };

      ~log_consumer ();

      int push_entry (replication_stream_entry *entry);

      int pop_entry (replication_stream_entry *&entry);

      int fetch_stream_entry (replication_stream_entry *&entry);

      void start_daemons (void);
      void execute_task (cubthread::entry &thread, repl_applier_worker_task *task);

      static log_consumer *new_instance (const cubstream::stream_position &start_position, bool use_daemons = false);

      cubstream::packing_stream *get_stream (void)
      {
	return m_stream;
      }

      cubstream::stream_position &get_start_position ()
      {
	return m_start_position;
      }

      void end_one_task (void)
      {
	m_started_tasks--;
      }

      void wait_for_tasks (void)
      {
	while (m_started_tasks > 0)
	  {
	    thread_sleep (1);
	  }
      }

      bool is_stopping (void)
      {
	return m_is_stopped;
      }

      void set_stop (void)
      {
	m_is_stopped = true;
	log_consumer::get_stream ()->set_stop ();
      }

  };

} /* namespace cubreplication */

#endif /* _LOG_CONSUMER_HPP_ */
