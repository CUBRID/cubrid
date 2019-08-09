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

#include "cubstream.hpp"
#include "semaphore.hpp"
#include "slave_control_channel.hpp"
#include "thread_manager.hpp"
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <queue>

namespace cubthread
{
  class daemon;
};

namespace cubstream
{
  class multi_thread_stream;
};

namespace cubreplication
{
  /*
   * main class for consuming log packing stream entries;
   * it should be created only as a global instance
   */
  class applier_worker_task;
  class stream_entry;
  class subtran_applier;

  /*
   * log_consumer : class intended as singleton for slave server
   *
   * Data members:
   *  - a pointer to slave stream (currently it also creates it, but future code should have a higher level
   *    object which aggregates both log_consumer and stream)
   *  - a queue of replication stream entry objects; the queue is protected by a mutex
   *  - m_apply_task_cv : condition variable used with m_queue_mutex to signal between consume daemon and
   *    dispatch daemon (when first adds a new stream entry in the queue)
   *  - m_is_stopped : flag to signal stopping of log_consumer; currently, the stopping is performed
   *    by destructor of log_consumer, but in future code, a higher level objects will handle stopping
   *    and destroy in separate steps;
   *    stopping process needs to wait for daemons and thread pool to stop; consume daemon needs to wait
   *    for stream to unblock from reading (so, we first signal the stop command to stream)
   *
   * Methods/daemons/threads:
   *  - a daemon which "consumes" replication stream entries : create a new replication_stream_entry object,
   *    prepares it (uses stream to receive and unpacks its header), and pushes to the queue
   *  - a dispatch daemon which extracts replication stream entry from queue and builds applier_worker_task
   *    objects; each applier_worker_task contains a list of stream_entries belonging to the same transaction;
   *    when a group commit special stream entry is encoutered by dispatch daemon, all gathered commited
   *    applier_worker_task are pushed to a worker thread pool (m_applier_workers_pool);
   *    the remainder of applier_worker_task objects (not having coommit), are copied to next cycle (until next
   *    group commit) : see dispatch_daemon_task::execute;
   *  - a thread pool for applying applier_worker_task; all replication stream entries are unpacked
   *    (the consumer daemon task is unpacking only the header) and then each replication object from a stream entry
   *    is applied
   */
  class log_consumer
  {
    private:
      std::queue<stream_entry *> m_stream_entries;

      cubstream::multi_thread_stream *m_stream;

      std::mutex m_queue_mutex;

      cubthread::daemon *m_consumer_daemon;

      cubthread::daemon *m_dispatch_daemon;

      cubthread::entry_workpool *m_applier_workers_pool;
      int m_applier_worker_threads_count;

      cubreplication::subtran_applier *m_subtran_applier;

      bool m_use_daemons;

      std::atomic<int> m_started_tasks;
      std::mutex m_join_tasks_mutex;
      std::condition_variable m_join_tasks_cv;

      std::condition_variable m_apply_task_cv;
      bool m_apply_task_ready;

      bool m_is_stopped;

      /* fetch suspend flag : this is required in context of replication with copy phase :
       * while replication copy is running the fetch from online replication must be suspended
       * (although the stream contents are received and stored on local slave node)
       */
      cubsync::event_semaphore m_fetch_suspend;

    private:

    public:

      std::function<void (cubstream::stream_position)> ack_produce;

      log_consumer ()
	: m_stream (NULL)
	, m_consumer_daemon (NULL)
	, m_dispatch_daemon (NULL)
	, m_applier_workers_pool (NULL)
	, m_applier_worker_threads_count (100)
	, m_subtran_applier (NULL)
	, m_use_daemons (false)
	, m_started_tasks (0)
	, m_apply_task_ready (false)
	, m_is_stopped (false)
	, ack_produce ([] (cubstream::stream_position)
      {
	assert (false);
      })
      {
	fetch_suspend ();
      };

      ~log_consumer ();

      void push_entry (stream_entry *entry);

      void pop_entry (stream_entry *&entry, bool &should_stop);

      int fetch_stream_entry (stream_entry *&entry);

      void start_daemons (void);
      void execute_task (applier_worker_task *task);
      void push_task (cubthread::entry_task *task);

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
	int started_task;
	started_task = --m_started_tasks;

	assert (started_task >= 0);
	if (started_task == 0)
	  {
	    /* Notify all. Currently, there  is only one waiter, but may be added others, later. */
	    std::unique_lock<std::mutex> ulock (m_join_tasks_mutex);
	    m_join_tasks_cv.notify_all ();
	  }
      }

      void set_ack_producer (const std::function<void (cubstream::stream_position)> &ack_producer)
      {
	ack_produce = ack_producer;
      }


      bool is_stopping (void)
      {
	return m_is_stopped;
      }
      void stop (void);
      void wait_for_tasks ();

      void fetch_suspend ();
      void fetch_resume ();
      void wait_for_fetch_resume ();

      subtran_applier &get_subtran_applier ();
  };

  //
  // dispatch_consumer is the common interface used by dispatch to control group creation.
  //
  class dispatch_consumer
  {
    public:
      virtual void wait_for_complete_stream_position (cubstream::stream_position stream_position) = 0;
      virtual void set_close_info_for_current_group (cubstream::stream_position stream_position,
	  int count_expected_transactions) = 0;
  };

} /* namespace cubreplication */

#endif /* _LOG_CONSUMER_HPP_ */
