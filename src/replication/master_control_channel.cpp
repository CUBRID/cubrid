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

#include "byte_order.h"
#include "communication_channel.hpp"
#include "stream_transfer_sender.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

namespace cubreplication
{
  class ack_reader_task : public cubthread::entry_task
  {
    public:
      ack_reader_task (cubcomm::channel *chn, cubstream::stream_ack *stream_ack);
      void execute (cubthread::entry &thread_ref) override;

    private:
      std::unique_ptr<cubcomm::channel> m_chn;
      cubstream::stream_ack *m_stream_ack;
  };

  ack_reader_task::ack_reader_task (cubcomm::channel *chn, cubstream::stream_ack *stream_ack)
    : m_chn (std::move (chn))
    , m_stream_ack (stream_ack)
  {
  }

  void ack_reader_task::execute (cubthread::entry &thread_ref)
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
    m_stream_ack->notify_stream_ack (ntohi64 (ack_sp));
  }

  class control_channel_managing_task : public cubthread::task_without_context
  {
    public:
      control_channel_managing_task (master_ctrl &master_ctrl);
      void execute () override;

    private:
      master_ctrl &m_master_ctrl;
  };

  control_channel_managing_task::control_channel_managing_task (master_ctrl &master_ctrl)
    : m_master_ctrl (master_ctrl)
  {

  }

  void control_channel_managing_task::execute ()
  {
    m_master_ctrl.check_alive ();
  }

  master_ctrl::master_ctrl (cubstream::stream_ack *stream_ack)
    : m_stream_ack (stream_ack)
  {
    cubthread::delta_time dt = std::chrono::seconds (10);
    control_channel_managing_task *ctrl_channels_manager = new control_channel_managing_task (*this);
    m_managing_daemon = cubthread::get_manager ()->create_daemon_without_entry (dt, ctrl_channels_manager,
			"control channels manager");
  }

  master_ctrl::~master_ctrl ()
  {
    cubthread::get_manager ()->destroy_daemon_without_entry (m_managing_daemon);

    for (auto &cr : m_ctrl_channel_readers)
      {
	cubthread::get_manager ()->destroy_daemon (cr.first);
      }

    // we are not the ones responsible for deallocating this
    m_stream_ack = NULL;
  }

  void
  master_ctrl::add (cubcomm::channel &&chn)
  {
    std::lock_guard<std::mutex> lg (m_mtx);

    // assure caller's param gets moved from
    cubcomm::channel *moved_to_chn = new cubcomm::channel (std::move (chn));
    cubthread::delta_time dt = cubthread::delta_time (0);
    ack_reader_task *ack_reader = new ack_reader_task (moved_to_chn, m_stream_ack);
    m_ctrl_channel_readers.push_back (std::make_pair (cubthread::get_manager ()->create_daemon (dt,
				      ack_reader, "control channel reader"), moved_to_chn));
  }

  void
  master_ctrl::check_alive ()
  {
    std::lock_guard<std::mutex> lg (m_mtx);
    for (auto it = m_ctrl_channel_readers.begin (); it != m_ctrl_channel_readers.end ();)
      {
	if (!it->second->is_connection_alive ())
	  {
	    cubthread::get_manager ()->destroy_daemon (it->first);
	    it = m_ctrl_channel_readers.erase (it);
	  }
	else
	  {
	    ++it;
	  }
      }
  }
}

