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

#ifndef _STREAM_TRANSFER_RECEIVER_HPP
#define _STREAM_TRANSFER_RECEIVER_HPP

#include "communication_channel.hpp"
#include "cubstream.hpp"

namespace cubthread
{
  class daemon;
};

namespace cubstream
{

  class transfer_receiver : public cubstream::write_handler
  {
    public:

      transfer_receiver (communication_channel &chn,
			 cubstream::stream &stream,
			 stream_position received_from_position = 0);
      virtual ~transfer_receiver ();

      virtual int write_action (const stream_position pos, char *ptr, const size_t byte_count) override;

      stream_position get_last_received_position ();

    private:

      friend class transfer_receiver_task;

      communication_channel &m_channel;
      cubstream::stream &m_stream;
      stream_position m_last_received_position;
      cubthread::daemon *m_receiver_daemon;
      char m_buffer[MTU];
  };

} // namespace cubstream

#endif /* _STREAM_TRANSFER_RECEIVER_HPP */
