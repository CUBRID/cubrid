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
 * replication_control_channel_receiver.cpp - manages master replication channel entries; it is a singleton
 *                                          - it maintains the minimum successful sent position
 */

#include "replication_control_channel_receiver.hpp"

#include <mutex>

#include "stream_transfer_sender.hpp"
#include "thread_manager.hpp"
#include "thread_daemon.hpp"


namespace cubreplication
{
  namespace control_channel
  {
    class control_reader : public cubthread::task_without_context
    {
      public:
	control_reader (cubcomm::channel &&chn);
	void execute () override;

      private:
	cubcomm::channel m_chn;
    };

    std::list<control_reader *> chns;
    std::mutex mtx;
    cubstream::stream_ack *g_stream_ack = NULL;

    control_reader::control_reader (cubcomm::channel &&chn)
      : m_chn (std::move (chn))
    {
    }

    void control_reader::execute ()
    {
      size_t len = sizeof (cubstream::stream_position);
      cubstream::stream_position ack_sp;
      int error_code = m_chn.recv ((char *)&ack_sp, len);

      if (g_stream_ack != NULL && len == sizeof (cubstream::stream_position))
	{
	  g_stream_ack->notify_stream_ack (ack_sp);
	}
    }

    void init (cubstream::stream_ack *stream_ack)
    {
      g_stream_ack = stream_ack;
    }

    void add (cubcomm::channel &&chn)
    {
      std::lock_guard<std::mutex> lg (mtx);
      chns.emplace_back (new control_reader (std::move (chn)));

      cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
	  new control_reader (std::move (chn)), "control channel reader");
    }
  }
}