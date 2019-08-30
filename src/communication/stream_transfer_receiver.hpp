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
 * stream_transfer_receiver.hpp - transfer stream through the network
 *                                see stream_transfer_sender.cpp commentary for more details
 */

#ifndef _STREAM_TRANSFER_RECEIVER_HPP_
#define _STREAM_TRANSFER_RECEIVER_HPP_

#include "communication_channel.hpp"
#include "cubstream.hpp"

#include <condition_variable>
#include <mutex>

namespace cubthread
{
  class daemon;
};

namespace cubstream
{

  class transfer_receiver
  {
    public:

      transfer_receiver (cubcomm::channel &&chn,
			 cubstream::stream &stream,
			 stream_position received_from_position = 0);
      virtual ~transfer_receiver ();

      void wait_disconnect ();

      cubcomm::channel &get_channel ()
      {
	return m_channel;
      }

    private:

      int write_action (const stream_position pos, char *ptr, const size_t byte_count);
      void terminate_connection ();

      friend class transfer_receiver_task;

      cubcomm::channel m_channel;
      cubstream::stream &m_stream;
      cubstream::stream_position m_last_received_position;
      cubthread::daemon *m_receiver_daemon;

      std::mutex m_sender_disconnect_mtx;
      std::condition_variable m_sender_disconnect_cv;

      char m_buffer[cubcomm::MTU];

    protected:
      cubstream::stream::write_func_t m_write_action_function;
  };

} // namespace cubstream

#endif /* _STREAM_TRANSFER_RECEIVER_HPP_ */
