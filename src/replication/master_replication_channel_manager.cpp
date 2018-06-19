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
 * master_replication_channel_manager.cpp - manages master replication channel entries; it is a singleton
 *                                        - it maintains the minimum successful sent position
 *                                        - it has a daemon for group commit synchronization
 */

#include "master_replication_channel_manager.hpp"

#include <utility>
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

namespace cubreplication
{

  std::vector <master_replication_channel_entry> master_replication_channel_manager::master_channels;
  cubthread::daemon *master_replication_channel_manager::master_channels_supervisor_daemon = NULL;
  bool master_replication_channel_manager::is_initialized = false;
  std::mutex master_replication_channel_manager::mutex_for_singleton;
  cubstream::stream_position master_replication_channel_manager::g_minimum_successful_stream_position;
  cubstream::stream *master_replication_channel_manager::g_stream;

  void master_replication_channel_manager::init (cubstream::stream *stream)
  {
    if (is_initialized == false)
      {
	std::lock_guard<std::mutex> guard (mutex_for_singleton);
	if (is_initialized == false)
	  {
	    master_channels_supervisor_daemon = cubthread::get_manager ()->create_daemon (cubthread::looper (
						  std::chrono::milliseconds (SUPERVISOR_DAEMON_DELAY_MS)),
						new master_channels_supervisor_task (), "supervisor_daemon");
	    master_channels.clear ();
	    is_initialized = true;
	    g_minimum_successful_stream_position = 0;
	    g_stream = stream;
	  }
      }
  }

  void master_replication_channel_manager::add_master_replication_channel (master_replication_channel_entry &&channel)
  {
    master_channels.push_back (std::forward<master_replication_channel_entry> (channel));
  }

  void master_replication_channel_manager::reset ()
  {
    if (is_initialized == true)
      {
	std::lock_guard<std::mutex> guard (mutex_for_singleton);
	if (is_initialized == true)
	  {
	    if (master_channels_supervisor_daemon != NULL)
	      {
		cubthread::get_manager ()->destroy_daemon (master_channels_supervisor_daemon);
		master_channels_supervisor_daemon = NULL;
	      }

	    master_channels.clear ();
	    is_initialized = false;
	  }
      }
  }

  unsigned int master_replication_channel_manager::get_number_of_channels ()
  {
    return is_initialized ? master_channels.size () : 0;
  }

  master_replication_channel_entry &master_replication_channel_entry::add_daemon (MASTER_DAEMON_THREAD_ID daemon_index,
      const cubthread::looper &loop_rule, cubthread::task_without_context *task)
  {
    if (m_master_daemon_threads[daemon_index] != NULL)
      {
	cubthread::get_manager()->destroy_daemon_without_entry (m_master_daemon_threads[daemon_index]);
      }

    m_master_daemon_threads[daemon_index] = cubthread::get_manager ()->create_daemon_without_entry (loop_rule,
					    task,
					    master_replication_channel::master_daemon_names[daemon_index]);

    return *this;
  }

  master_replication_channel_entry::master_replication_channel_entry (communication_channel &&chn)
  {
    this->m_channel = std::make_shared <cubreplication::master_replication_channel> (std::forward <communication_channel>
		      (chn),
		      master_replication_channel_manager::get_stream ());
    for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
      {
	this->m_master_daemon_threads[i] = NULL;
      }
  }

  master_replication_channel_entry::~master_replication_channel_entry ()
  {
    for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
      {
	if (m_master_daemon_threads[i] != NULL)
	  {
	    cubthread::get_manager()->destroy_daemon_without_entry (m_master_daemon_threads[i]);
	  }
      }
  }

  master_replication_channel_entry::master_replication_channel_entry (master_replication_channel_entry &&entry)
  {
    this->m_channel = std::move (entry.m_channel);

    for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
      {
	this->m_master_daemon_threads[i] = entry.m_master_daemon_threads[i];
	entry.m_master_daemon_threads[i] = NULL;
      }
  }

  master_replication_channel_entry &master_replication_channel_entry::operator= (master_replication_channel_entry &&entry)
  {
    this->~master_replication_channel_entry();
    new (this) master_replication_channel_entry (std::forward<master_replication_channel_entry> (entry));

    return *this;
  }

  std::shared_ptr<master_replication_channel> &master_replication_channel_entry::get_replication_channel()
  {
    return m_channel;
  }

} /* namespace cubreplication */
