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
 * transmitter.hpp
 */

#ifndef _CONNECTION_TRANSMITTER_HPP_
#define _CONNECTION_TRANSMITTER_HPP_

#include "packet_buffer.hpp"

#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  class transmitter
  {
    private:
      enum class state
      {
      };

    public:
      transmitter ();
      ~transmitter ();

      void reset ();

    private:
      state m_state;
      cubbase::packet_buffer m_buf;
  };
}

#endif
