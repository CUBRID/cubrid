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
#include "replication_node_manager.hpp"
#include "replication_master_node.hpp"

#include <utility>
#include "log_impl.h"
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

namespace cubreplication
{

  std::vector<cubstream::transfer_sender *> master_senders_manager::master_server_stream_senders;
  cubthread::daemon *master_senders_manager::master_channels_supervisor_daemon = NULL;
  bool master_senders_manager::is_initialized = false;
  std::mutex master_senders_manager::mutex_for_singleton;
  cubstream::stream_position master_senders_manager::g_minimum_successful_stream_position;
  SYNC_RWLOCK master_senders_manager::master_senders_lock;

  const unsigned int master_senders_manager::SUPERVISOR_DAEMON_DELAY_MS = 10;
  const unsigned int master_senders_manager::SUPERVISOR_DAEMON_CHECK_CONN_MS = 5000;

  void master_senders_manager::init ()
  {
#if defined (SERVER_MODE)
    int error_code = NO_ERROR;
    std::lock_guard<std::mutex> guard (mutex_for_singleton);

    if (is_initialized)
      {
	return;
      }

    master_server_stream_senders.clear ();

    cubthread::entry_callable_task *task = new cubthread::entry_callable_task (master_senders_manager::execute);
    master_channels_supervisor_daemon = cubthread::get_manager ()->create_daemon (
	cubthread::looper (std::chrono::milliseconds (SUPERVISOR_DAEMON_DELAY_MS)), task, "supervisor_daemon");
    g_minimum_successful_stream_position = 0;

    error_code = rwlock_initialize (&master_senders_lock, "MASTER_SENDERS_LOCK");
    assert (error_code == NO_ERROR);

    is_initialized = true;
#endif
  }

  void master_senders_manager::add_stream_sender (cubstream::transfer_sender *sender)
  {
    assert (is_initialized);

    rwlock_write_lock (&master_senders_lock);
    master_server_stream_senders.push_back (sender);
    logpb_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_MASTER_NODE);
    rwlock_write_unlock (&master_senders_lock);
  }

  void master_senders_manager::final ()
  {
#if defined (SERVER_MODE)
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
#endif
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

  void master_senders_manager::wakeup_transfer_senders (cubstream::stream_position desired_position)
  {
#if defined (SERVER_MODE)
    rwlock_read_lock (&master_senders_lock);
    for (cubstream::transfer_sender *sender : master_server_stream_senders)
      {
	if (sender->get_last_sent_position () < desired_position)
	  {
	    sender->get_daemon ()->wakeup ();
	  }
      }
    rwlock_read_unlock (&master_senders_lock);
#endif
  }

  void master_senders_manager::execute (cubthread::entry &context)
  {
#if defined (SERVER_MODE)
    static unsigned int check_conn_delay_counter = 0;
    bool have_write_lock = false;
    int active_senders = 0;
    bool senders_deleted = false;
    cubstream::stream_position min_position_send = std::numeric_limits<cubstream::stream_position>::max ();

    if (check_conn_delay_counter > SUPERVISOR_DAEMON_CHECK_CONN_MS / SUPERVISOR_DAEMON_DELAY_MS)
      {
	std::vector<cubstream::transfer_sender *>::iterator it;

	rwlock_read_lock (&master_senders_lock);
	for (it = master_server_stream_senders.begin (); it != master_server_stream_senders.end ();)
	  {
	    cubstream::transfer_sender *sender = *it;

	    if (!sender->get_channel ().is_connection_alive ())
	      {
		if (!have_write_lock)
		  {
		    rwlock_read_unlock (&master_senders_lock);

		    rwlock_write_lock (&master_senders_lock);
		    it = master_server_stream_senders.begin ();

		    have_write_lock = true;
		  }
		else
		  {
		    it = master_server_stream_senders.erase (it);
		    delete sender;
		    senders_deleted = true;
		  }
	      }
	    else
	      {
		cubstream::stream_position this_sender_pos = sender->get_last_sent_position ();
		min_position_send = std::min (this_sender_pos, min_position_send);
		active_senders++;
		++it;
	      }
	  }

	if (senders_deleted && active_senders == 0)
	  {
	    logpb_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_SINGLE_NODE);
	  }

	if (!have_write_lock)
	  {
	    rwlock_read_unlock (&master_senders_lock);
	  }
	else
	  {
	    rwlock_write_unlock (&master_senders_lock);
	  }
	check_conn_delay_counter = 0;

	if (active_senders > 0)
	  {
	    cubreplication::replication_node_manager::get_master_node ()
	    ->update_senders_min_position (min_position_send);
	  }
      }

    check_conn_delay_counter++;
#endif
  }

} /* namespace cubreplication */
