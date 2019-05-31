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
 * master_control_channel.hpp - manages master control channel entries
 */

#ifndef _MASTER_CONTROL_CHANNEL_HPP_
#define _MASTER_CONTROL_CHANNEL_HPP_

#include <list>
#include <mutex>

namespace cubcomm
{
  class channel;
}

namespace cubstream
{
  class stream_ack;
};

namespace cubthread
{
  class daemon;
};

namespace cubreplication
{
  class control_channel_managing_task;

  class master_ctrl
  {
    public:
      master_ctrl (cubstream::stream_ack *stream_ack);
      ~master_ctrl ();
      void add (cubcomm::channel &&chn);

    private:
      void check_alive ();

      cubthread::daemon *m_managing_daemon;
      std::list<std::pair<cubthread::daemon *, const cubcomm::channel *>> m_ctrl_channel_readers;
      std::mutex m_mtx;
      cubstream::stream_ack *m_stream_ack;

      friend class control_channel_managing_task;
  };
} /* namespace cubreplication */

#endif /* _MASTER_CONTROL_CHANNEL_HPP_ */
