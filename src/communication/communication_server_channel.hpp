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
 * communication_server_channel.hpp
 */

#ifndef _COMMUNICATION_SERVER_CHANNEL_HPP_
#define _COMMUNICATION_SERVER_CHANNEL_HPP_

#include "communication_channel.hpp"

#include <string>

namespace cubcomm
{

  class server_channel : public channel
  {
    public:
      server_channel (const char *server_name, int max_timeout_in_ms = -1);
      ~server_channel () = default;

      server_channel (const server_channel &) = delete;
      server_channel &operator= (const server_channel &) = delete;

      server_channel (server_channel &&comm);
      server_channel &operator= (server_channel &&comm);

      css_error_code connect (const char *hostname, int port) override;

    private:
      std::string m_server_name;
  };

}; /* cubcomm namepace */

#endif /* _COMMUNICATION_SERVER_CHANNEL_HPP_ */
