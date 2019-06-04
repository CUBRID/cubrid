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
 * slave_control_channel.hpp - manages slave control channel entries
 */

#ifndef _SLAVE_CONTROL_CHANNEL_HPP_
#define _SLAVE_CONTROL_CHANNEL_HPP_

#include <condition_variable>
#include <memory>
#include <mutex>

#include "cubstream.hpp"
#include "thread_task.hpp"

namespace cubcomm
{
  class channel;
}

namespace cubreplication
{
  class slave_control_channel
  {
    public:
      slave_control_channel (cubcomm::channel &&chn);
      void send_ack (cubstream::stream_position sp);
    private:
      std::unique_ptr<cubcomm::channel> m_chn;
  };

  class slave_control_sender : public cubthread::task_without_context
  {
    public:
      slave_control_sender (slave_control_channel &&ctrl_chn);
      ~slave_control_sender ();
      void execute () override;
      void append_synced (const cubstream::stream_position &sp);

    private:
      slave_control_channel m_ctrl_chn;

      std::mutex m_mtx;
      std::condition_variable m_cv;
      std::atomic<cubstream::stream_position> m_last_stream_pos;
      bool m_stop;
  };
}

#endif

