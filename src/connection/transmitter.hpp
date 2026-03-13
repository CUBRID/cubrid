/*
 *
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

#include "connection_statistics.hpp"
#include "buffer.hpp"
#include "packet_buffer.hpp"

#include <functional>
#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  class transmitter
  {
    public:
      transmitter (statistics::metrics<statistics::context> *stats);
      transmitter ();
      ~transmitter ();

      result fill (int fd, int limit = 0);

      template <typename... Spans>
      void push_for_send (const cubbase::span<std::byte> &&first, const Spans &&... rest);
      void push_for_deleter (std::function<void ()> &&deleter);
      void stamp ();

      bool empty ();
      void clear ();

    private:
      cubbase::packet_buffer m_buf;
      std::vector<std::function<void ()>> m_deleter;

      statistics::metrics<statistics::context> *m_stats;
  };

  template <typename... Spans>
  void transmitter::push_for_send (const cubbase::span<std::byte> &&first, const Spans &&... rest)
  {
    m_buf.push_for_send (std::forward<const cubbase::span<std::byte>> (first), std::forward<Spans> (rest)...);
  }
}

#endif
