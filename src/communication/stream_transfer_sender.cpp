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
 * stream_transfer_[sender/receiver].cpp
 *
 * - sends or receives chunks(usually MTU bytes) from cubstream::stream
 * - the sender interogates the respective stream and tries to send the minimum between MTU and what is left of
 *   the stream
 * - it contains a daemon that is started automatically at creation, that does the task of sending/receiving bytes
 * - usage can be found in transfer_channel/test_main.cpp;
 *   on one machine will exist a transfer_sender instance and on the other a transfer_receiver.
 *   the instances are created using consumer/producer streams and connected communication channels.
 *   the stream will be sent automatically until m_last_committed_pos
 */

#include "stream_transfer_sender.hpp"
#include "transaction_master_group_complete_manager.hpp"

#include "log_impl.h"

#include "system_parameter.h" /* for er_log_debug */
#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

#include <algorithm>          /* for std::min */


namespace cubstream
{

  class transfer_sender_task : public cubthread::task_without_context
  {
    public:
      transfer_sender_task (cubstream::transfer_sender &producer_channel)
	: this_producer_channel (producer_channel),
	  m_first_loop (true)
      {
      }

      void execute () override
      {
	css_error_code rc = NO_ERRORS;
	stream_position last_reported_ready_pos = this_producer_channel.m_stream.get_last_committed_pos ();

	if (m_first_loop)
	  {
	    UINT64 last_sent_position = 0;
	    std::size_t max_len = sizeof (UINT64);

	    assert (this_producer_channel.m_channel.is_connection_alive ());
	    assert (sizeof (stream_position) == sizeof (UINT64));

	    rc = this_producer_channel.m_channel.recv ((char *) &last_sent_position, max_len);
	    this_producer_channel.m_last_sent_position = htoni64 (last_sent_position);

	    er_log_debug (ARG_FILE_LINE, "transfer_sender_task starting : last_sent_position:%lld, rc:%d\n",
			  last_sent_position, rc);

	    assert (max_len == sizeof (UINT64));

	    if (rc != NO_ERRORS)
	      {
		this_producer_channel.m_channel.close_connection ();
		return;
	      }

	    m_first_loop = false;
	  }

	while (this_producer_channel.m_last_sent_position < last_reported_ready_pos)
	  {
	    std::size_t byte_count = std::min ((stream_position) cubcomm::MTU,
					       last_reported_ready_pos - this_producer_channel.m_last_sent_position);
	    int error_code = NO_ERROR;

	    er_log_debug (ARG_FILE_LINE, "transfer_sender_task sending : pos: %lld, bytes: %d\n",
			  this_producer_channel.m_last_sent_position, byte_count);

	    error_code = this_producer_channel.m_stream.read (this_producer_channel.m_last_sent_position, byte_count,
			 this_producer_channel.m_read_action_function);

	    if (error_code != NO_ERROR)
	      {
		this_producer_channel.m_channel.close_connection ();
		break;
	      }
	  }
      }

    private:
      cubstream::transfer_sender &this_producer_channel;
      bool m_first_loop;
  };

  transfer_sender::transfer_sender (cubcomm::channel &&chn, cubstream::stream &stream,
				    cubstream::stream_position begin_sending_position)
    : m_channel (std::move (chn)),
      m_stream (stream),
      m_last_sent_position (begin_sending_position)
  {
    cubthread::delta_time daemon_period = std::chrono::milliseconds (10);

    m_read_action_function =
	    std::bind (&transfer_sender::read_action, std::ref (*this), std::placeholders::_1,
		       std::placeholders::_2);

    m_sender_daemon = cubthread::get_manager ()->create_daemon_without_entry (daemon_period,
		      new transfer_sender_task (*this),
		      "stream_transfer_sender");

    m_p_stream_ack = cubtx::master_group_complete_manager::get_instance ();
  }

  transfer_sender::~transfer_sender ()
  {
    cubthread::get_manager ()->destroy_daemon_without_entry (m_sender_daemon);
  }

  cubcomm::channel &transfer_sender::get_channel ()
  {
    return m_channel;
  }

  //TODO[replication] make this atomic
  stream_position transfer_sender::get_last_sent_position ()
  {
    return m_last_sent_position;
  }

  int transfer_sender::read_action (char *ptr, const size_t byte_count)
  {
    if (m_channel.send (ptr, byte_count) == NO_ERRORS)
      {
	cubcomm::er_log_debug_buffer ("transfer_sender::read_action", ptr, byte_count);

	m_last_sent_position += byte_count;

	_er_log_debug (ARG_FILE_LINE, "m_ack_stream_position updated: (transfer sender sent)"
		       "previous m_ack_stream_position=%llu, new m_ack_stream_position=%llu",
		       (std::uint64_t) log_Gl.hdr.m_ack_stream_position, m_last_sent_position);
	log_Gl.hdr.m_ack_stream_position = m_last_sent_position;

	if (m_p_stream_ack)
	  {
	    m_p_stream_ack->notify_stream_ack (m_last_sent_position);
	  }
	return NO_ERROR;
      }

    return ER_FAILED;
  }

} // namespace cubstream
