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
#include "stream_entry_fetcher.hpp"
#include "thread_manager.hpp"
#include <cstddef>
#include <memory>

namespace cubthread
{
  class daemon;
}

namespace cubstream
{
  class multi_thread_stream;
}

namespace cubreplication
{
  /*
   * main class for consuming log packing stream entries;
   * it should be created only as a global instance
   */
  class applier_worker_task;
  class stream_entry;
  class subtran_applier;
  using stream_entry_fetcher = cubstream::entry_fetcher<stream_entry>;

  /*
   * log_consumer : class intended as singleton for slave server
   *
   * Data members:
   *  - a pointer to slave stream (currently it also creates it, but future code should have a higher level
   *    object which aggregates both log_consumer and stream)
   *
   * Methods/daemons/threads:
   *  - a dispatch daemon which extracts replication stream entry from stream and builds applier_worker_task
   *    objects; each applier_worker_task contains a list of stream_entries belonging to the same transaction;
   *    when a group commit special stream entry is encoutered by dispatch daemon, all gathered commited
   *    applier_worker_task are pushed to a worker thread pool (m_applier_workers_pool);
   *    the remainder of applier_worker_task objects (not having coommit), are copied to next cycle (until next
   *    group commit) : see dispatch_daemon_task::execute;
   *  - a thread pool for applying applier_worker_task; all replication stream entries are unpacked and then
   *    each replication object from a stream entry is applied
   */
  class log_consumer
  {
    private:
      cubstream::multi_thread_stream *m_stream;

      cubthread::daemon *m_dispatch_daemon;

      cubthread::entry_workpool *m_applier_workers_pool;
      int m_applier_worker_threads_count;

      cubreplication::subtran_applier *m_subtran_applier;

      bool m_use_daemons;

      std::atomic<int> m_started_tasks;

      /* fetch suspend flag : this is required in context of replication with copy phase :
       * while replication copy is running the fetch from online replication must be suspended
       * (although the stream contents are received and stored on local slave node)
       */
      cubsync::event_semaphore m_fetch_suspend;

    public:

      std::function<void (cubstream::stream_position)> ack_produce;

      log_consumer ()
	: m_stream (NULL)
	, m_dispatch_daemon (NULL)
	, m_applier_workers_pool (NULL)
	, m_applier_worker_threads_count (100)
	, m_subtran_applier (NULL)
	, m_use_daemons (false)
	, m_started_tasks (0)
	, ack_produce ([] (cubstream::stream_position)
      {
	assert (false);
      })
      {
	fetch_suspend ();
      };

      ~log_consumer ();

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
	m_started_tasks--;
      }

      void set_ack_producer (const std::function<void (cubstream::stream_position)> &ack_producer)
      {
	ack_produce = ack_producer;
      }

      int get_started_task (void)
      {
	return m_started_tasks;
      }

      void wait_for_tasks (void);

      void stop (void);

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
