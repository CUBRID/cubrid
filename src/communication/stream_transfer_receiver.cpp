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
#include "system_parameter.h" /* for er_log_debug */
#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

#include <cstring>

namespace cubstream
{

  class transfer_receiver_task : public cubthread::entry_task
  {
    public:
      transfer_receiver_task (cubstream::transfer_receiver &consumer_channel)
	: this_consumer_channel (consumer_channel),
	  m_first_loop (true)
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	css_error_code rc = NO_ERRORS;
	std::size_t max_len = DB_ALIGN_BELOW (cubcomm::MTU, MAX_ALIGNMENT);

	if (m_first_loop)
	  {
	    UINT64 last_recv_pos = 0;

	    assert (this_consumer_channel.m_channel.is_connection_alive ());
	    assert (sizeof (stream_position) == sizeof (UINT64));

	    last_recv_pos = htoni64 (this_consumer_channel.m_last_received_position);
	    rc = this_consumer_channel.m_channel.send ((char *) &last_recv_pos,
		 sizeof (UINT64));

	    er_log_debug (ARG_FILE_LINE, "transfer_receiver_task starting : "
			  "m_last_received_position: %lld, rc: %d\n",
			  this_consumer_channel.m_last_received_position, rc);

	    if (rc != NO_ERRORS)
	      {
		assert (false);
		this_consumer_channel.terminate_connection ();
		return;
	      }

	    m_first_loop = false;
	  }

	if (!this_consumer_channel.m_channel.is_connection_alive ())
	  {
	    return;
	  }
	rc = this_consumer_channel.m_channel.recv (this_consumer_channel.m_buffer, max_len);
	if (rc != NO_ERRORS)
	  {
	    this_consumer_channel.terminate_connection ();
	    return;
	  }

	cubcomm::er_log_debug_buffer ("transfer_receiver_task receiving", this_consumer_channel.m_buffer, max_len);

	if (this_consumer_channel.m_stream.write (max_len, this_consumer_channel.m_write_action_function))
	  {
	    this_consumer_channel.terminate_connection ();
	    return;
	  }
      }

    private:
      cubstream::transfer_receiver &this_consumer_channel;
      bool m_first_loop; /* TODO[replication] may be a good idea to use create_context instead */
  };

  transfer_receiver::transfer_receiver (cubcomm::channel &&chn,
					stream &stream,
					stream_position received_from_position)
    : m_channel (std::move (chn)),
      m_stream (stream),
      m_last_received_position (received_from_position)
  {
    m_write_action_function = std::bind (&transfer_receiver::write_action,
					 std::ref (*this),
					 std::placeholders::_1,
					 std::placeholders::_2,
					 std::placeholders::_3);

    std::string daemon_name = "stream_transfer_receiver_" + chn.get_channel_id ();
    m_receiver_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
			new transfer_receiver_task (*this), daemon_name.c_str ());
  }

  transfer_receiver::~transfer_receiver ()
  {
    terminate_connection ();
    cubthread::get_manager ()->destroy_daemon (m_receiver_daemon);
  }

  int transfer_receiver::write_action (const stream_position pos, char *ptr, const size_t byte_count)
  {
    std::memcpy (ptr, m_buffer, byte_count);
    m_last_received_position += byte_count;

    return NO_ERROR;
  }

  void transfer_receiver::wait_disconnect ()
  {
    std::unique_lock<std::mutex> ul (m_sender_disconnect_mtx);
    m_sender_disconnect_cv.wait (ul, [this] {return !m_channel.is_connection_alive ();});
  }

  void transfer_receiver::terminate_connection ()
  {
    m_channel.close_connection ();
    m_sender_disconnect_cv.notify_one ();
  }

} // namespace cubstream
