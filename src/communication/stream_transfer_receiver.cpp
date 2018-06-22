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

#include "byte_order.h"
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
	: this_consumer_channel (consumer_channel),
          m_first_loop (true)
      {
      }

      void execute () override
      {
	css_error_code rc = NO_ERRORS;
	std::size_t max_len = cubcomm::MTU;

	if (m_first_loop)
	  {
            UINT64 last_recv_pos = 0;

	    assert (this_consumer_channel.m_channel.is_connection_alive ());
            assert (sizeof (stream_position) == sizeof (UINT64));

            last_recv_pos = htoni64 (this_consumer_channel.m_last_received_position);
            rc = (css_error_code) this_consumer_channel.m_channel.send ((char *) &last_recv_pos,
                                                                        sizeof (UINT64));
	    assert (rc == NO_ERRORS);

	    m_first_loop = false;
	  }

	rc = (css_error_code) this_consumer_channel.m_channel.recv (this_consumer_channel.m_buffer, max_len);
	if (rc != NO_ERRORS)
	  {
	    this_consumer_channel.m_channel.close_connection ();
	    return;
	  }

	rc = (css_error_code) this_consumer_channel.m_stream.write (max_len, this_consumer_channel.m_write_action_function);
	if (rc != NO_ERROR)
	  {
	    this_consumer_channel.m_channel.close_connection ();
	    return;
	  }
      }

    private:
      cubstream::transfer_receiver &this_consumer_channel;
      bool m_first_loop; /* TODO[arnia] may be a good idea to use create_context instead */
  };

  transfer_receiver::transfer_receiver (cubcomm::channel &&chn,
					stream &stream,
					stream_position received_from_position)
    : m_channel (std::move (chn)),
      m_stream (stream),
      m_last_received_position (received_from_position)
  {
    m_receiver_daemon = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
			new transfer_receiver_task (*this), "stream_transfer_receiver");

    m_write_action_function = std::bind (&transfer_receiver::write_action,
					 std::ref (*this),
					 std::placeholders::_1,
					 std::placeholders::_2,
					 std::placeholders::_3);
  }

  transfer_receiver::~transfer_receiver ()
  {
    cubthread::get_manager ()->destroy_daemon_without_entry (m_receiver_daemon);
  }

  int transfer_receiver::write_action (const stream_position pos, char *ptr, const size_t byte_count)
  {
    std::size_t recv_bytes = byte_count;
    int rc = NO_ERROR;

    std::memcpy (ptr + pos, m_buffer, recv_bytes);
    m_last_received_position += recv_bytes;

    return rc;
  }

  stream_position transfer_receiver::get_last_received_position ()
  {
    return m_last_received_position;
  }


} // namespace cubstream
