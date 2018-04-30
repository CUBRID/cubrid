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
 * stream_transfer_receiver.cpp - transfer stream through the network;
 *                                 see stream_transfer_sender.cpp commentary for more details
 */

#include "stream_transfer_receiver.hpp"

#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

#include <cstring>

namespace cubstream
{

  class transfer_receiver_task : public cubthread::task_without_context
  {
    public:
      transfer_receiver_task (cubstream::transfer_receiver &consumer_channel)
	: this_consumer_channel (consumer_channel)
      {
      }

      void execute () override
      {
	int rc = 0;
	size_t max_len = MTU;

	assert (this_consumer_channel.m_channel.is_connection_alive ());

	rc = this_consumer_channel.m_channel.recv (this_consumer_channel.m_buffer, max_len);
	if (rc != NO_ERRORS)
	  {
	    this_consumer_channel.m_channel.close_connection ();
	    return;
	  }

	rc = this_consumer_channel.m_stream.write (max_len, &this_consumer_channel);
	if (rc != NO_ERRORS)
	  {
	    this_consumer_channel.m_channel.close_connection ();
	    return;
	  }
      }

    private:
      cubstream::transfer_receiver &this_consumer_channel;
  };

  transfer_receiver::transfer_receiver (communication_channel &chn,
					cubstream::stream &stream,
					stream_position received_from_position)
    : m_channel (chn),
      m_stream (stream),
      m_last_received_position (received_from_position)
  {
    m_receiver_daemon = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
			new transfer_receiver_task (*this), "stream_transfer_receiver");
  }

  transfer_receiver::~transfer_receiver ()
  {
    cubthread::get_manager ()->destroy_daemon_without_entry (m_receiver_daemon);
  }

  int transfer_receiver::write_action (const stream_position pos, char *ptr, const size_t byte_count)
  {
    std::size_t recv_bytes = byte_count;
    int rc = NO_ERRORS;

    std::memcpy (ptr + pos, m_buffer, recv_bytes);
    m_last_received_position += recv_bytes;

    return rc;
  }

  stream_position transfer_receiver::get_last_received_position ()
  {
    return m_last_received_position;
  }


} // namespace cubstream
