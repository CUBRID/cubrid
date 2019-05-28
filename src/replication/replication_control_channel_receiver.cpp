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

#include <list>
#include <mutex>
#include <queue>
#include <vector>

#include "communication_channel.hpp"
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
	control_reader ();
	void execute () override;
    };

    std::list<cubcomm::channel *> chns;
    std::vector<POLL_FD> pollfd_v;
    std::queue<POLL_FD> pollfd_q;
    std::mutex mtx;
    cubstream::stream_ack *g_stream_ack = NULL;
    cubthread::daemon *g_ack_reader = NULL;

    control_reader::control_reader ()
    {
    }

    void control_reader::execute ()
    {
      std::unique_lock<std::mutex> ul (mtx);
      while (!pollfd_q.empty ())
	{
	  pollfd_v.push_back (pollfd_q.front ());
	  pollfd_q.pop ();
	}
      ul.unlock ();

      // todo: optimize timeout
      int ec = css_platform_independent_poll (pollfd_v.data (), pollfd_v.size (), 10);

      if (ec < 0)
	{
	  // error
	  retire ();
	  return;
	}

      if (ec == 0)
	{
	  // timeout
	  return;
	}

      std::list<cubcomm::channel *>::iterator it = chns.begin ();
      for (size_t i = 0; i < pollfd_v.size () && it != chns.end (); ++i, ++it)
	{
	  if (pollfd_v[i].revents == 0)
	    {
	      continue;
	    }
	  assert (pollfd_v[i].revents == POLLIN);

	  size_t len = sizeof (cubstream::stream_position);
	  cubstream::stream_position ack_sp;
	  css_error_code ec = (*it)->read_after_poll ((char *) &ack_sp, len);

	  if (ec != NO_ERRORS)
	    {
	      (*it)->close_connection ();
	      delete (*it);
	      it = chns.erase (it);
	      // todo: need to also remove replication channel connection
	      pollfd_v.erase (pollfd_v.begin () + i);
	      --it;
	      --i;
	      continue;
	    }
	  g_stream_ack->notify_stream_ack (ack_sp);

	}
    }

    void init (cubstream::stream_ack *stream_ack)
    {
      assert (stream_ack != NULL);

      g_stream_ack = stream_ack;

      g_ack_reader = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
		     new control_reader, "control channel reader");
    }

    void finalize ()
    {
      assert (g_ack_reader != NULL);
      cubthread::get_manager()->destroy_daemon (g_ack_reader);
      g_ack_reader = NULL;

      for (cubcomm::channel *chn : chns)
	{
	  chn->close_connection ();
	  delete chn;
	}
      chns.clear ();
      pollfd_v.clear ();
      pollfd_q = std::queue <POLL_FD> ();
      g_ack_reader = NULL;

      // we are not the ones resposible for deallocing this
      g_stream_ack = NULL;
    }

    void add (cubcomm::channel &&chn)
    {
      std::lock_guard<std::mutex> lg (mtx);
      pollfd_q.push ({chn.get_socket (), POLLIN, 0});
      chns.push_back (new cubcomm::channel (std::move (chn)));
    }
  }
}