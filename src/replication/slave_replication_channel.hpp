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
 * slave_replication_channel.hpp - container for comm chn, stream and transfer chn
 */

#ifndef _SLAVE_REPLICATION_CHANNEL_HPP
#define _SLAVE_REPLICATION_CHANNEL_HPP

#include "cub_server_communication_channel.hpp"
#include "connection_defs.h"
#include "thread_entry_task.hpp"
#include "stream_transfer_receiver.hpp"

namespace cubthread
{
  class daemon;
  class looper;
};

namespace cubreplication
{

  class slave_replication_channel
  {
    public:

      slave_replication_channel (cub_server_communication_channel &&chn,
				 cubstream::stream &stream,
				 cubstream::stream_position received_from_position);
      ~slave_replication_channel () = default;

      bool is_connected ();
      inline cubstream::transfer_receiver &get_stream_receiver()
      {
	return m_stream_receiver;
      }

    private:
      cub_server_communication_channel m_with_master_comm_chn;
      cubstream::transfer_receiver m_stream_receiver;
  };

} /* namespace cubreplication */

#endif
