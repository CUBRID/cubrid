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
 * master_replication_channel.cpp - container for comm chn, stream and transfer chn
 *                                - maintains a group commit position
 */

#include "master_replication_channel.hpp"

#include "thread_manager.hpp"
#include "thread_entry_task.hpp"

namespace cubreplication
{

  const char *master_replication_channel::master_daemon_names[NUM_OF_MASTER_DAEMON_THREADS] = {"check_gc_daemon",
											       "for_testing_daemon"
											      };

  master_replication_channel::master_replication_channel (communication_channel &&chn,
      cubstream::stream &stream,
      cubstream::stream_position begin_sending_position) :  m_with_slave_comm_chn (std::forward <communication_channel>
	    (chn)),
    m_stream_sender (m_with_slave_comm_chn, stream, begin_sending_position),
    m_group_commit_position (0)
  {
    assert (is_connected ());
  }

  bool master_replication_channel::is_connected ()
  {
    return m_with_slave_comm_chn.is_connection_alive ();
  }

  cubstream::stream_position master_replication_channel::get_current_sending_position()
  {
    return m_stream_sender.get_last_sent_position ();
  }

} /* namespace cubreplication */
