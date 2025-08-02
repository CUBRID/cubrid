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
 * epoll.hpp
 */

#ifndef _CONNECTION_EPOLL_HPP_
#define _CONNECTION_EPOLL_HPP_

#include "nonblocking.hpp"
#include "packet_buffer.hpp"

#include <sys/socket.h>
#include <sys/types.h>
#include <cstdint>

#define TIMEOUT_INFINITE -1
#define TIMEOUT_NOWAIT 0

namespace cubsocket
{
  class epoll : public nonblocking
  {
    public:
      enum class iores : int
      {
	done,
	would_block,
	out_of_buffer,
	budget_shortage,
	peer_reset,
	fatal_error,
	unknown
      };

      epoll ();
      ~epoll ();
      epoll (const epoll &other) = delete;
      epoll &operator= (const epoll &other) = delete;

      int wait (void *events, int maxevents, int timeout) noexcept;
      bool add_descriptor (int fd, std::uint32_t flags, void *ptr = nullptr) noexcept;
      bool modify_descriptor (int fd, std::uint32_t flags, void *ptr = nullptr) noexcept;
      bool remove_descriptor (int fd) noexcept;

      [[gnu::hot]]
      std::tuple<iores, std::size_t> recvmsg (int fd, struct ::msghdr *msg) noexcept;

      /* DO NOT MERGE THESE TWO FUNCTIONS BY REFACTORING! */
      [[gnu::hot]]
      iores sendmsg (int fd, struct ::msghdr *msg) noexcept;
      [[gnu::hot]]
      iores sendmsg (int fd, struct ::msghdr *msg, std::size_t budget) noexcept;

    private:
      int m_epoll;
  };
}

#endif
