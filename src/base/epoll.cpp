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
 * epoll.cpp
 */

#include "epoll.hpp"
#include "error_manager.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace cubsocket
{
  epoll::epoll ()
  {
    m_epoll = ::epoll_create1 (0);
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

  epoll::iores epoll::recvmsg (int fd, struct ::msghdr *msg) noexcept
  {
    ssize_t bytes;
    std::size_t advance;

    while (msg->msg_iovlen)
      {
	bytes = ::recvmsg (fd, msg, MSG_DONTWAIT);
	if (bytes > 0)
	  {
	    advance = static_cast<std::size_t> (bytes);
	    while (advance && msg->msg_iovlen)
	      {
		if (advance < msg->msg_iov->iov_len)
		  {
		    msg->msg_iov->iov_base = static_cast<std::byte *> (msg->msg_iov->iov_base) + advance;
		    msg->msg_iov->iov_len -= advance;
		    advance = 0;
		  }
		else
		  {
		    advance -= msg->msg_iov->iov_len;
		    ++msg->msg_iov;
		    --msg->msg_iovlen;
		  }
	      }
	    continue;
	  }

	if (__builtin_expect (bytes == 0, 0))
	  {
	    return iores::peer_reset;
	  }

	switch (errno)
	  {
	  case EINTR:
	    continue;
	  case EAGAIN:
	    /* case EWOULDBLOCK: */
	    return iores::would_block;
	  case ECONNRESET:
	    return iores::peer_reset;
	  default:
	    return iores::fatal_error;
	  }
      }

    return iores::done;
  }

  epoll::iores epoll::sendmsg (int fd, struct ::msghdr *msg) noexcept
  {
    ssize_t bytes;
    std::size_t advance;

    while (msg->msg_iovlen)
      {
	bytes = ::sendmsg (fd, msg, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (bytes > 0)
	  {
	    advance = static_cast<std::size_t> (bytes);
	    while (advance && msg->msg_iovlen)
	      {
		if (advance < msg->msg_iov->iov_len)
		  {
		    msg->msg_iov->iov_base = static_cast<std::byte *> (msg->msg_iov->iov_base) + advance;
		    msg->msg_iov->iov_len -= advance;
		    advance = 0;
		  }
		else
		  {
		    advance -= msg->msg_iov->iov_len;
		    ++msg->msg_iov;
		    --msg->msg_iovlen;
		  }
	      }
	    continue;
	  }

	if (__builtin_expect (bytes == 0, 0))
	  {
	    return iores::peer_reset;
	  }

	switch (errno)
	  {
	  case EINTR:
	    continue;
	  case EAGAIN:
	    /* case EWOULDBLOCK: */
	    return iores::would_block;
	  case EPIPE:
	  case ECONNRESET:
	    return iores::peer_reset;
	  default:
	    _er_log_debug (__FILE__, __LINE__, "[w] sendmsg error: %s", strerror (errno) );
	    return iores::fatal_error;
	  }
      }

    return iores::done;
  }

  epoll::iores epoll::sendmsg (int fd, struct ::msghdr *msg, std::size_t budget) noexcept
  {
    ssize_t bytes;
    std::size_t advance;

    while (msg->msg_iovlen && budget)
      {
	bytes = ::sendmsg (fd, msg, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (bytes > 0)
	  {
	    budget -= static_cast<std::size_t> (bytes);
	    advance = static_cast<std::size_t> (bytes);
	    while (advance && msg->msg_iovlen)
	      {
		if (advance < msg->msg_iov->iov_len)
		  {
		    msg->msg_iov->iov_base = static_cast<std::byte *> (msg->msg_iov->iov_base) + advance;
		    msg->msg_iov->iov_len -= advance;
		    advance = 0;
		  }
		else
		  {
		    advance -= msg->msg_iov->iov_len;
		    ++msg->msg_iov;
		    --msg->msg_iovlen;
		  }
	      }
	    continue;
	  }

	if (__builtin_expect (bytes == 0, 0))
	  {
	    return iores::peer_reset;
	  }

	switch (errno)
	  {
	  case EINTR:
	    continue;
	  case EAGAIN:
	    /* case EWOULDBLOCK: */
	    return iores::would_block;
	  case EPIPE:
	  case ECONNRESET:
	    return iores::peer_reset;
	  default:
	    return iores::fatal_error;
	  }
      }

    return msg->msg_iovlen ? iores::budget_shortage : iores::done;
  }
}

