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
 * slave_replication_channel.cpp - container for comm chn, stream and transfer chn
 */

#include "slave_replication_channel.hpp"

#include "connection_globals.h"
#include "connection_sr.h"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include "system_parameter.h"

#if defined (WINDOWS)
#include "wintcp.h"
#endif

namespace cubreplication
{

  slave_replication_channel::slave_replication_channel (cub_server_communication_channel &&chn,
      cubstream::stream &stream,
      cubstream::stream_position received_from_position) : m_with_master_comm_chn (
	  std::forward<cub_server_communication_channel> (chn)),
    m_stream_receiver (m_with_master_comm_chn, stream, received_from_position)
  {
    assert (is_connected ());
  }

  bool slave_replication_channel::is_connected()
  {
    return m_with_master_comm_chn.is_connection_alive ();
  }

} /* namespace cubreplication */
