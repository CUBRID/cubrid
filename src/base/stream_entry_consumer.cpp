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
 * stream_entry_consumer.cpp
 */


#include "stream_entry_consumer.hpp"
#include "multi_thread_stream.hpp"

namespace cubstream
{
  stream_entry_consumer::stream_entry_consumer (const char *name, multi_thread_stream *stream, size_t applier_threads)
    : m_name (name ? name : "")
    , m_stream (stream)
    , m_applier_worker_threads_count (applier_threads)
    {
    }

  void stream_entry_consumer::start (void)
  {
    std::string pool_name = m_name + std::string ("_worker_thread_pool");

    m_applier_workers_pool = cubthread::get_manager ()->create_worker_pool (m_applier_worker_threads_count,
			     m_applier_worker_threads_count, pool_name.c_str (),NULL, 1, 1);
    start_dispatcher ();
  }

  void stream_entry_consumer::stop (void)
  {
    m_stream->stop ();

    stop_dispatcher ();

    cubthread::get_manager ()->destroy_worker_pool (m_applier_workers_pool);
  }

  void stream_entry_consumer::wait_for_tasks (void)
  {
    while (m_started_tasks.load () > 0)
      {
	thread_sleep (1);
      }
  }
} /* namespace cubstream */
