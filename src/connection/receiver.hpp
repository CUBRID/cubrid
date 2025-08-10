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
 * receiver.hpp
 */

#ifndef _CONNECTION_RECEIVER_HPP_
#define _CONNECTION_RECEIVER_HPP_

#include "buffer.hpp"
#include "DMRB_SPSC.hpp"

#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  class receiver
  {
  private:
    enum class state
    {
      RecvSize,
      RecvData
    };

  public:
    receiver (std::size_t capacity);
    ~receiver ();

    result drain (int fd);

  private:
    state m_state;
    cubbase::DMRB_SPSC<false> m_buf;

    int m_sizebuf;
    std::byte *m_bufptr;
    std::size_t m_received;
    std::size_t m_target;

    /* output */
    std::vector<std::byte *> m_result;

    void reset ();

    bool received_size ();
    bool received_data ();
    bool received ();

    result receive (int fd);
  };
}

#endif
