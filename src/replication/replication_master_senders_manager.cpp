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
  cubstream::stream_position master_senders_manager::g_minimum_successful_stream_position;
  cubstream::stream *master_senders_manager::g_stream;
  SYNC_RWLOCK master_senders_manager::master_senders_lock;

  const unsigned int master_senders_manager::SUPERVISOR_DAEMON_DELAY_MS = 10;
  const unsigned int master_senders_manager::SUPERVISOR_DAEMON_CHECK_CONN_MS = 5000;

  void master_senders_manager::init (cubstream::stream *stream)
  {
    int error_code = NO_ERROR;
    std::lock_guard<std::mutex> guard (mutex_for_singleton);

    if (is_initialized)
      {
	return;
      }

    master_server_stream_senders.clear ();
    master_channels_supervisor_daemon = cubthread::get_manager ()->create_daemon (
	cubthread::looper (std::chrono::milliseconds (SUPERVISOR_DAEMON_DELAY_MS)),
	new master_senders_supervisor_task (),
	"supervisor_daemon");
    g_minimum_successful_stream_position = 0;
    g_stream = stream;

    error_code = rwlock_initialize (&master_senders_lock, "MASTER_SENDERS_LOCK");
    assert (error_code == NO_ERROR);

    is_initialized = true;
  }

  void master_senders_manager::add_stream_sender (cubstream::transfer_sender *sender)
  {
    assert (is_initialized);

    rwlock_write_lock (&master_senders_lock);
    master_server_stream_senders.push_back (sender);
    rwlock_write_unlock (&master_senders_lock);
  }

  void master_senders_manager::final ()
  {
    int error_code = NO_ERROR;
    std::lock_guard<std::mutex> guard (mutex_for_singleton);

    if (!is_initialized)
      {
	return;
      }

    if (master_channels_supervisor_daemon != NULL)
      {
	cubthread::get_manager ()->destroy_daemon (master_channels_supervisor_daemon);
	master_channels_supervisor_daemon = NULL;
      }

    rwlock_write_lock (&master_senders_lock);
    for (cubstream::transfer_sender *sender : master_server_stream_senders)
      {
	delete sender;
      }
    master_server_stream_senders.clear ();
    rwlock_write_unlock (&master_senders_lock);

    error_code = rwlock_finalize (&master_senders_lock);
    assert (error_code == NO_ERROR);

    is_initialized = false;
  }

  std::size_t master_senders_manager::get_number_of_stream_senders ()
  {
    std::size_t length = 0;

    assert (is_initialized);

    rwlock_read_lock (&master_senders_lock);
    length = master_server_stream_senders.size ();
    rwlock_read_unlock (&master_senders_lock);

    return length;
  }

  void master_senders_manager::block_until_position_sent (cubstream::stream_position desired_position)
  {
    bool is_position_sent = false;
    const std::chrono::microseconds SLEEP_BETWEEN_SPINS (20);

    while (!is_position_sent)
      {
	is_position_sent = true;

	rwlock_read_lock (&master_senders_lock);
	for (cubstream::transfer_sender *sender : master_server_stream_senders)
	  {
	    if (sender->get_last_sent_position () < desired_position)
	      {
		is_position_sent = false;
		sender->get_daemon ()->wakeup ();
	      }
	  }
	rwlock_read_unlock (&master_senders_lock);

	std::this_thread::sleep_for (SLEEP_BETWEEN_SPINS);
      }
  }

  master_senders_manager::master_senders_supervisor_task::master_senders_supervisor_task ()
  {
  }

  void master_senders_manager::master_senders_supervisor_task::execute (cubthread::entry &context)
  {
    static unsigned int check_conn_delay_counter = 0;
    bool promoted_to_write = false;

    if (check_conn_delay_counter >
	SUPERVISOR_DAEMON_CHECK_CONN_MS / SUPERVISOR_DAEMON_DELAY_MS)
      {
	std::vector <cubstream::transfer_sender *>::iterator it;

	rwlock_read_lock (&master_senders_lock);
	for (it = master_server_stream_senders.begin (); it != master_server_stream_senders.end ();)
	  {
	    if (! (*it)->get_channel ().is_connection_alive ())
	      {
		if (!promoted_to_write)
		  {
		    rwlock_read_unlock (&master_senders_lock);

		    rwlock_write_lock (&master_senders_lock);
		    it = master_server_stream_senders.begin ();

		    promoted_to_write = true;
		  }
		else
		  {
		    it = master_server_stream_senders.erase (it);
		  }
	      }
	    else
	      {
		++it;
	      }
	  }
	if (!promoted_to_write)
	  {
	    rwlock_read_unlock (&master_senders_lock);
	  }
	else
	  {
	    rwlock_write_unlock (&master_senders_lock);
	  }
	check_conn_delay_counter = 0;
      }

    check_conn_delay_counter++;
  }

} /* namespace cubreplication */
