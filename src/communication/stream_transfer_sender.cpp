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
 *
 *   Termination :
 *    - some streams are infinite (they continuosly send data until socket becomes invalid) or finite
 *    - a finite stream require an explicit termination phase :
 *      the user code of stream sender must use 'enter_termination_phase' of the sender object.
 *      this sets the sender into "receive" mode which expects the peer to either send something or
 *      simply close the connection
 *    - the stream receiver also has a 'terminate_connection' method which needs to be explicitely
 *      called by the user code
 *    - since stream and sender/receiver view the contents as bytes, the 'decision' to terminate the connection
 *      is taken at logical level (user code of stream/sender/receiver) ;
 *      for a finite stream, a logical 'end' packet (stream entry) could be send from sender side to receiver side;
 *      after decoding of this packet (which is ussualy asynchronous with sender/receive threads), the user must call
 *      sender->enter_termination_phase; on receiver side, after decoding the logical 'end' packet, the user code calls
 *      receiver->terminate_connection (this will close the connection, which is detected by sender side, which in turn
 *      is unblocked)
*/

#include "stream_transfer_sender.hpp"

#include "system_parameter.h" /* for er_log_debug */
#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"

#include <algorithm>          /* for std::min */
#include "byte_order.h"       /* for htoni64 */

namespace cubstream
{

  class transfer_sender_task : public cubthread::entry_task
  {
    public:
      transfer_sender_task (cubstream::transfer_sender &producer_channel)
	: this_producer_channel (producer_channel),
	  m_first_loop (true)
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	css_error_code rc = NO_ERRORS;
	stream_position last_reported_ready_pos = this_producer_channel.m_stream.get_last_committed_pos ();

	if (!this_producer_channel.m_channel.is_connection_alive ())
	  {
	    return;
	  }

	if (m_first_loop)
	  {
	    UINT64 last_sent_position = 0;
	    std::size_t max_len = sizeof (UINT64);

	    static_assert (sizeof (stream_position) == sizeof (UINT64),
			   "stream position size differs from requested start stream position");

	    rc = this_producer_channel.m_channel.recv ((char *) &last_sent_position, max_len);
	    if (rc != NO_ERRORS)
	      {
		this_producer_channel.m_channel.close_connection ();
		return;
	      }

	    this_producer_channel.m_last_sent_position = htoni64 (last_sent_position);

	    er_log_debug (ARG_FILE_LINE, "transfer_sender_task starting : last_sent_position:%llu, rc:%d\n",
			  this_producer_channel.m_last_sent_position, rc);

	    assert (max_len == sizeof (UINT64));

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

	if (this_producer_channel.m_last_sent_position < this_producer_channel.m_stream.get_last_committed_pos ())
	  {
	    /* send all stream data before termination */
	    return;
	  }

	if (this_producer_channel.is_termination_phase ())
	  {
	    UINT64 expected_magic;
	    std::size_t max_len = sizeof (expected_magic);

	    /* wait for connection closing, we don't care about received content */
	    (void) this_producer_channel.m_channel.recv ((char *) &expected_magic, max_len);

	    this_producer_channel.m_channel.close_connection ();
	    return;
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
      m_last_sent_position (begin_sending_position),
      m_is_termination_phase (false)
  {
    cubthread::delta_time daemon_period = std::chrono::milliseconds (10);

    m_read_action_function =
	    std::bind (&transfer_sender::read_action, std::ref (*this), std::placeholders::_1,
		       std::placeholders::_2);

    std::string daemon_name = "stream_transfer_sender_" + chn.get_channel_id ();
    m_sender_daemon = cubthread::get_manager ()->create_daemon (daemon_period, new transfer_sender_task (*this),
		      daemon_name.c_str ());
  }

  transfer_sender::~transfer_sender ()
  {
    cubthread::get_manager ()->destroy_daemon (m_sender_daemon);
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

	if (m_p_stream_ack)
	  {
	    m_p_stream_ack->notify_stream_ack (m_last_sent_position);
	  }
	return NO_ERROR;
      }

    return ER_FAILED;
  }

} // namespace cubstream
