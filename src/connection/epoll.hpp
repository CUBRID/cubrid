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

#include <nonblocking.hpp>

#include <sys/socket.h>
#include <sys/types.h>

#define TIMEOUT_INFINITE -1
#define TIMEOUT_NOWAIT 0

namespace cubsocket
{
  class epoll : public nonblocking
  {
  public:
    epoll ();
    ~epoll ();
    epoll (const epoll& other) = delete;
    epoll &operator= (const epoll& other) = delete;
    
    int wait (void *events, int maxevents, int timeout);
    bool add_descriptor (int fd, int flags);
    bool modify_descriptor (int fd, int flags);

    int recv (int fd, void *buf, int length, int flags, int budget);

    int send (int fd, struct ::iovec *iov, int length, int budget);
    int send (int fd, struct ::msghdr *msg, int budget);

  private:
    enum offset
    {
      Incomplete = 0,
      Done
    };

    int m_epoll;

    offset adjust_iovec (struct ::iovec *&iov, size_t &length, size_t bytes);
  };
}

#endif
