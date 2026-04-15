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
 * epoll.cpp
 */

#include "epoll.hpp"
#include "error_manager.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubsocket
{
  epoll::epoll ()
  {
    m_epoll = ::epoll_create1 (EPOLL_CLOEXEC);
    if (m_epoll == -1)
      {
	assert_release (false);
      }
  }

  epoll::~epoll ()
  {
    if (m_epoll != -1)
      {
	::close (m_epoll);
      }
  }

  int epoll::wait (void *events, int maxevents, int timeout) noexcept
  {
    return ::epoll_wait (m_epoll, (struct epoll_event *) events, maxevents, timeout);
  }

  bool epoll::add_descriptor (int fd, std::uint32_t flags, void *ptr) noexcept
  {
    epoll_event ev {};

    ev.events = flags;
    if (ptr)
      {
	ev.data.ptr = ptr;
      }
    else
      {
	ev.data.fd = fd;
      }

    if (::epoll_ctl (m_epoll, EPOLL_CTL_ADD, fd, &ev) == -1)
      {
	return false;
      }
    return true;
  }

  bool epoll::modify_descriptor (int fd, std::uint32_t flags, void *ptr) noexcept
  {
    epoll_event ev {};

    ev.events = flags;
    if (ptr)
      {
	ev.data.ptr = ptr;
      }
    else
      {
	ev.data.fd = fd;
      }

    if (::epoll_ctl (m_epoll, EPOLL_CTL_MOD, fd, &ev) == -1)
      {
	return false;
      }
    return true;
  }

  bool epoll::remove_descriptor (int fd) noexcept
  {
    if (::epoll_ctl (m_epoll, EPOLL_CTL_DEL, fd, nullptr) == -1)
      {
	return false;
      }
    return true;
  }
}

