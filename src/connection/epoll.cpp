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

#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace cubsocket
{
  epoll::epoll ()
  {
    this->m_epoll = ::epoll_create1 (0);
    if (this->m_epoll == -1)
    {
      assert_release (false);
    }
  }

  epoll::~epoll ()
  {
    if (this->m_epoll != -1)
    {
      ::close (this->m_epoll);
    }
  }

  int epoll::wait (void *events, int maxevents, int timeout)
  {
    return epoll_wait (this->m_epoll, (struct epoll_event *) events, maxevents, timeout);
  }

  bool epoll::add_descriptor (int fd, int flags)
  {
    return 0;
  }

  bool epoll::modify_descriptor (int fd, int flags)
  {
    return 0;
  }

  int epoll::recv (int fd, void *buf, int length, int flags, int budget)
  {
    return 0;
  }

  int epoll::send (int fd, struct ::iovec *iov, int length, int budget)
  {
    struct ::msghdr msg = { 0, 0, 0, 0, 0, 0, 0 };

    msg.msg_iov = iov;
    msg.msg_iovlen = length;
    return this->send (fd, &msg, budget);
  }

  int epoll::send (int fd, struct ::msghdr *msg, int budget)
  {
    ssize_t bytes;

    while (budget > 0)
    {
      bytes = sendmsg (fd, msg, 0);

      if (bytes == 0)
      {
	/* disconnected by peer */
	/* TODO: change the value */
	return -2;
      }
      if (bytes < 0)
      {
	if (errno == EAGAIN || errno == EWOULDBLOCK)
	{
	  /* wait the time to send the data */
	  /* TODO: change the value */
	  return -1;
	}

	/* this may be critical */
	/* TODO: change the value */
        return -3;
      }

      if (this->adjust_iovec (msg->msg_iov, msg->msg_iovlen, (size_t) bytes) == offset::Done)
      {
	/* done */
	/* TODO: change the value */
	return 0;
      }
      budget -= bytes;
    }

    /* low on budget */
    /* TODO: change the value */
    return -4;
  }

  epoll::offset epoll::adjust_iovec (struct ::iovec *&iov, size_t &length, size_t bytes)
  {
    assert (iov != NULL);
    assert (length > 0);
    assert (bytes > 0);

    while (true)
    {
      if (iov->iov_len > bytes)
      {
	iov->iov_base = (char *) iov->iov_base + bytes;
	iov->iov_len -= bytes;
	break;
      }
      bytes -= iov->iov_len;
      iov++;
      length--;

      /* previous iov->iov_len and bytes are the same */
      if (bytes == 0)
      {
	break;
      }
    }

    return (length == 0) ? offset::Done : offset::Incomplete;
  }
}

