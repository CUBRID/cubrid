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
 * replication_master_senders_manager.cpp - manages master replication channel entries; it is a singleton
 *                                        - it maintains the minimum successful sent position
 */

#include "replication_master_senders_manager.hpp"

#include <utility>
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

namespace cubreplication
{

  std::vector <cubstream::transfer_sender *> master_senders_manager::master_server_stream_senders;
  cubthread::daemon *master_senders_manager::master_channels_supervisor_daemon = NULL;
  bool master_senders_manager::is_initialized = false;
  std::mutex master_senders_manager::mutex_for_singleton;
  std::mutex master_senders_manager::master_senders_mutex;
  cubstream::stream_position master_senders_manager::g_minimum_successful_stream_position;
  cubstream::stream *master_senders_manager::g_stream;

  void master_senders_manager::init (cubstream::stream *stream)
  {
    if (is_initialized == false)
      {
	std::lock_guard<std::mutex> guard (mutex_for_singleton);
	if (is_initialized == false)
	  {
	    master_server_stream_senders.clear ();
	    master_channels_supervisor_daemon = cubthread::get_manager ()->create_daemon (
		cubthread::looper (std::chrono::milliseconds (SUPERVISOR_DAEMON_DELAY_MS)),
		new master_senders_supervisor_task (),
		"supervisor_daemon");
	    g_minimum_successful_stream_position = 0;
	    g_stream = stream;
	    is_initialized = true;
	  }
      }
  }

  void master_senders_manager::add_stream_sender (cubstream::transfer_sender *sender)
  {
    std::lock_guard<std::mutex> guard (master_senders_mutex);

    master_server_stream_senders.push_back (sender);
  }

  void master_senders_manager::reset ()
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
	    for (cubstream::transfer_sender *sender : master_server_stream_senders)
	      {
		delete sender;
	      }
	    master_server_stream_senders.clear ();
	    is_initialized = false;
	  }
      }
  }

  std::size_t master_senders_manager::get_number_of_stream_senders ()
  {
    std::lock_guard<std::mutex> guard (master_senders_mutex);

    return master_server_stream_senders.size ();
  }

  bool master_senders_manager::check_if_senders_reached_position (cubstream::stream_position desired_position)
  {
    std::lock_guard<std::mutex> guard (master_senders_mutex);

    for (cubstream::transfer_sender *sender : master_server_stream_senders)
      {
	if (sender->get_last_sent_position () < desired_position)
	  {
	    return false;
	  }
      }

    return true;
  }

} /* namespace cubreplication */
