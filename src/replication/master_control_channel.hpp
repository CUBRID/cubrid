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


namespace cubcomm
{
  class channel;
}

namespace cubstream
{
  class stream_ack;
};

namespace cubreplication
{
  namespace master_ctrl
  {
    void init (cubstream::stream_ack *stream_ack);
    void add (cubcomm::channel &&chn);
    void finalize ();
  }
} /* namespace cubreplication */

#endif /* _MASTER_CONTROL_CHANNEL_HPP_ */
