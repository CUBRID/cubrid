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
 * replication_control_channel_sender.cpp - manages slave control channel entries
 */

#include "replication_control_channel_sender.hpp"

#include <atomic>
#include <memory>
#include <mutex>

#include "communication_channel.hpp"

namespace control_channel
{
  sender::sender (cubcomm::channel &&chn)
    : m_chn (new cubcomm::channel (std::move (chn)))
  {
  }

  void sender::execute ()
  {
    std::unique_lock<std::mutex> lk (m_mtx);
    m_condvar.wait (lk);

    cubstream::stream_position latest = m_latest_stream_position.load ();
    css_error_code ec = m_chn->send ((const char *) &latest, sizeof (latest));

    if ( ec != NO_ERRORS)
      {
	m_chn->close_connection ();
	retire ();
	return;
      }
  }

  void sender::wake_up_and_send (cubstream::stream_position sp)
  {
    while ( m_latest_stream_position.load () < sp)
      {
	m_latest_stream_position.compare_exchange_strong (sp, m_latest_stream_position.load ());
      }
    m_condvar.notify_one ();
  }
}
