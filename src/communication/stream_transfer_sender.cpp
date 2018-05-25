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

#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

namespace cubstream
{

  class transfer_sender_task : public cubthread::task_without_context
  {
    public:
      transfer_sender_task (cubstream::transfer_sender &producer_channel)
	: this_producer_channel (producer_channel)
      {
      }

      void execute () override
      {
	int rc = NO_ERRORS;
	stream_position last_reported_ready_pos = this_producer_channel.m_stream.get_last_committed_pos ();

	assert (this_producer_channel.m_channel.is_connection_alive ());

	while (rc == NO_ERRORS && this_producer_channel.m_last_sent_position < last_reported_ready_pos)
	  {
	    std::size_t byte_count = std::min ((stream_position) MTU,
					       last_reported_ready_pos - this_producer_channel.m_last_sent_position);

	    rc = this_producer_channel.m_stream.read (this_producer_channel.m_last_sent_position, byte_count,
		 this_producer_channel.m_read_action_function);

	    if (rc != NO_ERRORS)
	      {
		this_producer_channel.m_channel.close_connection ();
	      }
	  }
      }

    private:
      cubstream::transfer_sender &this_producer_channel;
  };

  transfer_sender::transfer_sender (communication_channel &chn, cubstream::stream &stream,
				    stream_position begin_sending_position)
    : m_channel (chn),
      m_stream (stream),
      m_last_sent_position (begin_sending_position)
  {
    cubthread::delta_time daemon_period = std::chrono::milliseconds (10);
    m_sender_daemon = cubthread::get_manager ()->create_daemon_without_entry (daemon_period,
		      new transfer_sender_task (*this),
		      "stream_transfer_sender");
    m_read_action_function =
	    std::bind (&transfer_sender::read_action, std::ref (*this), std::placeholders::_1,
		       std::placeholders::_2);
  }

  transfer_sender::~transfer_sender ()
  {
    cubthread::get_manager ()->destroy_daemon_without_entry (m_sender_daemon);
  }

  communication_channel &transfer_sender::get_communication_channel ()
  {
    return m_channel;
  }

  stream_position transfer_sender::get_last_sent_position ()
  {
    return m_last_sent_position;
  }

  int transfer_sender::read_action (char *ptr, const size_t byte_count)
  {
    int rc = m_channel.send (ptr, byte_count);

    if (rc == NO_ERRORS)
      {
	m_last_sent_position += byte_count;
      }

    return rc;
  }

} // namespace cubstream
