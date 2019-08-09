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
 * stream_senders_manager.cpp -
 */

#include "log_impl.h"
#include "stream_senders_manager.hpp"
#include "replication_common.hpp"

#include <utility>
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

/* TODO : I am not sure we will not need some other functionality specific to replication
 * keep this in replication namespace and folder until it is more clear */
namespace cubreplication
{

  const unsigned int stream_senders_manager::SUPERVISOR_DAEMON_DELAY_MS = 10;
  const unsigned int stream_senders_manager::SUPERVISOR_DAEMON_CHECK_CONN_MS = 5000;

  stream_senders_manager::stream_senders_manager (cubstream::stream &supervised_stream)
    : m_stream (supervised_stream)
    , m_supervisor_daemon (NULL)
  {
#if defined (SERVER_MODE)
    int error_code = NO_ERROR;

    std::string daemon_name = "senders_supervisor_daemon_" + m_stream.name ();
    auto exec_f = std::bind (&stream_senders_manager::execute, this, std::placeholders::_1);
    cubthread::entry_callable_task *task = new cubthread::entry_callable_task (exec_f);
    m_supervisor_daemon =
	    cubthread::get_manager ()->create_daemon (
		    cubthread::looper (std::chrono::milliseconds (SUPERVISOR_DAEMON_DELAY_MS)), task, daemon_name.c_str ());

    error_code = rwlock_initialize (&m_senders_lock, "MASTER_SENDERS_LOCK");
    assert (error_code == NO_ERROR);
#endif
  }

  stream_senders_manager::~stream_senders_manager ()
  {
#if defined (SERVER_MODE)
    er_log_debug_replication (ARG_FILE_LINE, "stream_senders_manager::finalize");

    int error_code = NO_ERROR;

    if (m_supervisor_daemon != NULL)
      {
	cubthread::get_manager ()->destroy_daemon (m_supervisor_daemon);
	m_supervisor_daemon = NULL;
      }

    remove_all_senders ();

    error_code = rwlock_finalize (&m_senders_lock);
    assert (error_code == NO_ERROR);
#endif
  }

  void stream_senders_manager::add_stream_sender (cubstream::transfer_sender *sender)
  {
    rwlock_write_lock (&m_senders_lock);
    m_stream_senders.push_back (sender);
    logpb_atomic_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_MASTER_NODE);
    rwlock_write_unlock (&m_senders_lock);
  }

  void stream_senders_manager::remove_all_senders ()
  {
    rwlock_read_lock (&m_senders_lock);

    for (cubstream::transfer_sender *sender : m_stream_senders)
      {
	delete sender;
      }

    m_stream_senders.clear ();

    logpb_atomic_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_SINGLE_NODE);

    rwlock_read_unlock (&m_senders_lock);
  }

  void stream_senders_manager::wakeup_transfer_senders (cubstream::stream_position desired_position)
  {
#if defined (SERVER_MODE)
    rwlock_read_lock (&m_senders_lock);
    for (cubstream::transfer_sender *sender : m_stream_senders)
      {
	if (sender->get_last_sent_position() < desired_position)
	  {
	    sender->get_daemon()->wakeup();
	  }
      }
    rwlock_read_unlock (&m_senders_lock);
#endif
  }

  void stream_senders_manager::stop_stream_sender (cubstream::transfer_sender *sender)
  {
    rwlock_write_lock (&m_senders_lock);
    for (cubstream::transfer_sender *s : m_stream_senders)
      {
	if (sender == s)
	  {
	    er_log_debug_replication (ARG_FILE_LINE, "stream_senders_manager::stop_stream_sender for channel:%s",
				      s->get_channel ().get_channel_id ().c_str ());
	    s->enter_termination_phase ();
	    break;
	  }
      }
    rwlock_write_unlock (&m_senders_lock);
  }

  bool stream_senders_manager::is_stream_sender_alive (const cubstream::transfer_sender *sender)
  {
    bool found = false;

    rwlock_read_lock (&m_senders_lock);
    for (cubstream::transfer_sender *s : m_stream_senders)
      {
	if (sender == s)
	  {
	    found = true;
	    break;
	  }
      }
    rwlock_read_unlock (&m_senders_lock);

    return found;
  }

  std::size_t stream_senders_manager::get_number_of_stream_senders ()
  {
    std::size_t length = 0;

    rwlock_read_lock (&m_senders_lock);
    length = m_stream_senders.size ();
    rwlock_read_unlock (&m_senders_lock);

    return length;
  }

  void stream_senders_manager::execute (cubthread::entry &context)
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

	rwlock_read_lock (&m_senders_lock);
	for (it = m_stream_senders.begin (); it != m_stream_senders.end ();)
	  {
	    cubstream::transfer_sender *sender = *it;

	    if (!sender->get_channel ().is_connection_alive ())
	      {
		if (!have_write_lock)
		  {
		    rwlock_read_unlock (&m_senders_lock);

		    rwlock_write_lock (&m_senders_lock);
		    it = m_stream_senders.begin ();

		    have_write_lock = true;
		  }
		else
		  {
		    it = m_stream_senders.erase (it);
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
	    logpb_atomic_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_SINGLE_NODE);
	  }

	if (!have_write_lock)
	  {
	    rwlock_read_unlock (&m_senders_lock);
	  }
	else
	  {
	    rwlock_write_unlock (&m_senders_lock);
	  }
	check_conn_delay_counter = 0;

	if (active_senders > 0)
	  {
	    /* TODO : we may choose to force flush of all data, even if was read by all senders */
	    m_stream.set_last_recyclable_pos (min_position_send);
	    m_stream.reset_serial_data_read (min_position_send);

	    er_log_debug_replication (ARG_FILE_LINE, "senders_manager (stream:%s) update_senders_min_position: %llu,\n"
				      " stream_read_pos:%llu, commit_pos:%llu", m_stream.name ().c_str (),
				      min_position_send, m_stream.get_curr_read_position (),
				      m_stream.get_last_committed_pos ());
	  }
      }

    check_conn_delay_counter++;
#endif
  }

} /* namespace cubreplication */
