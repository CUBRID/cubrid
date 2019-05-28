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
 * replication_control_channel_sender.hpp - manages slave control channel entries
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "cubstream.hpp"
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

namespace cubcomm
{
  class channel;
}

namespace control_channel
{
  class sender : public cubthread::task_without_context
  {
    public:
      sender (cubcomm::channel &&chn);
      void execute () override;

      void wake_up_and_send (cubstream::stream_position sp);
    private:
      std::unique_ptr<cubcomm::channel> m_chn;

      /* Wakeup info. */
      std::condition_variable m_condvar;
      std::mutex m_mtx;
      std::atomic<cubstream::stream_position> m_latest_stream_position;
  };
}