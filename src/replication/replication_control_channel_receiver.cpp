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
 * replication_control_channel_receiver.cpp - manages master control channel entries
 */

#include "replication_control_channel_receiver.hpp"

#include <list>
#include <mutex>

#include "communication_channel.hpp"
#include "stream_transfer_sender.hpp"
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

namespace cubreplication
{
  namespace master_ctrl
  {
    class chn_manager : public cubthread::task_without_context
    {
      public:
	chn_manager (cubcomm::channel &&chn, cubstream::stream_ack *stream_ack);
	void execute () override;

      private:
	cubcomm::channel m_chn;
	cubstream::stream_ack *m_stream_ack;
    };

    std::list<cubthread::daemon *> chns;
    std::mutex mtx;
    cubstream::stream_ack *g_stream_ack = NULL;

    chn_manager::chn_manager (cubcomm::channel &&chn, cubstream::stream_ack *stream_ack)
      : m_chn (std::move (chn))
      , m_stream_ack (stream_ack)
    {
    }

    void chn_manager::execute ()
    {
      size_t len = sizeof (cubstream::stream_position);
      cubstream::stream_position ack_sp;
      css_error_code ec = m_chn.recv ((char *) &ack_sp, len);
      if (ec != NO_ERRORS)
	{
	  m_chn.close_connection ();
	  retire ();
	  return;
	}
      m_stream_ack->notify_stream_ack (ack_sp);
    }

    void init (cubstream::stream_ack *stream_ack)
    {
      assert (stream_ack != NULL);

      g_stream_ack = stream_ack;
    }

    void finalize ()
    {
      for (cubthread::daemon *cr : chns)
	{
	  cubthread::get_manager()->destroy_daemon (cr);
	}
      chns.clear ();

      // we are not the ones resposible for deallocing this
      g_stream_ack = NULL;
    }

    void add (cubcomm::channel &&chn)
    {
      std::lock_guard<std::mutex> lg (mtx);
      chns.push_back (cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
		      new chn_manager (std::move (chn), g_stream_ack), "control channel reader"));
    }
  }
}