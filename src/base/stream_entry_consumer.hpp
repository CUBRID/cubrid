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
 * stream_entry_consumer.hpp
 */

#ident "$Id$"

#ifndef _STREAM_ENTRY_CONSUMER_HPP_
#define _STREAM_ENTRY_CONSUMER_HPP_

#include "error_manager.h"
#include "string_buffer.hpp"
#include "thread_manager.hpp"
#include <string>

namespace cubstream
{
  class multi_thread_stream;

  /*
   * stream_entry_consumer : abtract class for "consuming" stream entries with a thread pool
   *
   * Requires a multi_thread_stream
   * It does not include the dispatcher functionality, it requires the implementation of start and stop of dispatcher
   *
   * Usage :
   *  - implement consumer derived from stream_entry_consumer
   *  - required : implement start_dispatcher, stop_dispatcher (it must include a stream_fetcher mechanism)
   *  - optional : implement on_task_execution; this is executed before execute_task method
   */
  class stream_entry_consumer
  {
    private:
      std::string m_name;
      multi_thread_stream *m_stream;

      cubthread::entry_workpool *m_applier_workers_pool;

    protected:

      size_t m_applier_worker_threads_count;
      std::atomic<int> m_started_tasks;

    public:

      stream_entry_consumer (const char *name, multi_thread_stream *stream, size_t applier_threads);

      virtual ~stream_entry_consumer () = 0;

      cubstream::multi_thread_stream *get_stream (void)
      {
	return m_stream;
      }

      void start ();

      void stop ();

      virtual void start_dispatcher (void) = 0;

      virtual void stop_dispatcher (void) = 0;

      void end_one_task (void)
      {
	m_started_tasks--;
      }

      int get_started_task (void)
      {
	return m_started_tasks;
      }

      void wait_for_tasks (void);

      virtual void on_task_execution () {}

      template <typename Task>
      void execute_task (Task *task, bool detailed_dump = false)
      {
	on_task_execution ();

	if (detailed_dump)
	  {
	    string_buffer sb;
	    task->stringify (sb);
	    _er_log_debug (ARG_FILE_LINE, "%s::execute_task:\n%s", m_name.c_str (), sb.get_buffer ());
	  }

	push_task (task);
      }

      template <typename Task>
      void push_task (Task *task)
      {
	cubthread::get_manager ()->push_task (m_applier_workers_pool, task);

	m_started_tasks++;
      }
  };

} /* namespace cubstream */

#endif /* _STREAM_ENTRY_CONSUMER_HPP_ */
