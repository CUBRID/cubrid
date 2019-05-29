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
 * master_control_channel.cpp - manages master control channel entries
 */

#include "master_control_channel.hpp"

#include <chrono>
#include <list>
#include <memory>
#include <mutex>

#include "communication_channel.hpp"
#include "stream_transfer_sender.hpp"
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

namespace cubreplication
{
  class ack_reader_task : public cubthread::task_without_context
  {
    public:
      ack_reader_task (cubcomm::channel *chn, cubstream::stream_ack *stream_ack);
      void execute () override;

    private:
      std::unique_ptr<cubcomm::channel> m_chn;
      cubstream::stream_ack *m_stream_ack;
  };

  ack_reader_task::ack_reader_task (cubcomm::channel *chn, cubstream::stream_ack *stream_ack)
    : m_chn (std::move (chn))
    , m_stream_ack (stream_ack)
  {
  }

  void ack_reader_task::execute ()
  {
    if (!m_chn->is_connection_alive ())
      {
	return;
      }
    size_t len = sizeof (cubstream::stream_position);
    cubstream::stream_position ack_sp;
    css_error_code ec = m_chn->recv ((char *) &ack_sp, len);
    if (ec != NO_ERRORS)
      {
	m_chn->close_connection ();
	// will get cleared by control_channel_managing_task
	return;
      }
    m_stream_ack->notify_stream_ack (ack_sp);
  }

  class control_channel_managing_task : public cubthread::task_without_context
  {
    public:
      control_channel_managing_task (std::list <std::pair<cubthread::daemon *, const cubcomm::channel *>>
				     &ctrl_channel_readers, std::mutex &mtx);
      void execute () override;

    private:
      std::list<std::pair<cubthread::daemon *, const cubcomm::channel *>> &m_managed_readers;
      std::mutex &m_mtx;
  };

  control_channel_managing_task::control_channel_managing_task (std::list
      <std::pair<cubthread::daemon *, const cubcomm::channel *>> &ctrl_channel_readers, std::mutex &mtx)
    : m_managed_readers (m_managed_readers)
    , m_mtx (mtx)
  {

  }

  void control_channel_managing_task ::execute ()
  {
    std::lock_guard <std::mutex> lg (m_mtx);

    for (auto it = m_managed_readers.begin (); it != m_managed_readers.end (); ++it)
      {
	if (!it->second->is_connection_alive ())
	  {
	    cubthread::get_manager ()->destroy_daemon (it->first);
	    m_managed_readers.erase (it);
	    --it;
	  }
      }
  }

  master_ctrl::master_ctrl ()
  {
    m_managing_looper = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (
				std::chrono::seconds (10)),
			new control_channel_managing_task (m_ctrl_channel_readers, m_mtx), "control channels manager");
  }

  void
  master_ctrl::init_stream_ack_ref (cubstream::stream_ack *stream_ack)
  {
    assert (stream_ack != NULL);

    m_stream_ack = stream_ack;
  }

  master_ctrl::~master_ctrl ()
  {
    cubthread::get_manager ()->destroy_daemon (m_managing_looper);

    for (auto &cr : m_ctrl_channel_readers)
      {
	cubthread::get_manager ()->destroy_daemon (cr.first);
      }

    // we are not the ones resposible for deallocing this
    m_stream_ack = NULL;
  }

  void
  master_ctrl::add (cubcomm::channel &&chn)
  {
    assert (m_stream_ack != NULL);

    std::lock_guard<std::mutex> lg (m_mtx);

    // assure caller's param gets moved from
    cubcomm::channel *moved_to_chn = new cubcomm::channel (std::move (chn));

    m_ctrl_channel_readers.push_back (std::make_pair (cubthread::get_manager ()->create_daemon_without_entry (
	cubthread::delta_time (0),
	new ack_reader_task (moved_to_chn, m_stream_ack), "control channel reader"), moved_to_chn));
  }
}
