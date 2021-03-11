/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
  enum class server_server //server to server commands
  {
    CONNECT_ACTIVE_TRAN_TO_PAGE_SERVER, // active transaction to page server
  };

  class server_channel : public channel
  {
    public:
      server_channel (const char *server_name, int max_timeout_in_ms = -1);
      ~server_channel () = default;

      server_channel (const server_channel &) = delete;
      server_channel &operator= (const server_channel &) = delete;

      server_channel (server_channel &&comm);
      server_channel &operator= (server_channel &&comm);

      css_error_code connect (const char *hostname, int port, css_command_type cmd_type);

    private:
      std::string m_server_name;
  };

}; /* cubcomm namepace */

#endif /* _COMMUNICATION_SERVER_CHANNEL_HPP_ */
