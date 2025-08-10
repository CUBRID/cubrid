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
 * receiver.cpp
 */

#include "receiver.hpp"
#include "error_manager.h"

#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#define NEXT_STATE(x) do { \
    _er_log_debug (__FILE__, __LINE__, "receiver set state = %d\n", state::x); \
    (m_state = state::x); \
} while (0)

namespace cubconn
{
  receiver::receiver (std::size_t capacity) :
    m_state (state::RecvSize),
    m_buf (capacity),
    m_bufptr (nullptr),
    m_received (0),
    m_target (0)
  {
    m_result.reserve (8);
    this->reset ();
  }

  receiver::~receiver ()
  {
  }

  void receiver::reset ()
  {
    m_sizebuf = 0;
    m_bufptr = reinterpret_cast<std::byte *> (&m_sizebuf);
    m_received = 0;
    m_target = sizeof (int);
  }

  bool receiver::received_size ()
  {
    m_received = 0;
    m_target = m_sizebuf;

    if (m_target)
    {
    }
  }

  bool receiver::received ()
  {
    switch (m_state)
    {
      case state::RecvSize:
	this->received_size ();
	NEXT_STATE (RecvData);
	break;

      case state::RecvData:

	this->reset ();
	NEXT_STATE (RecvSize);
	break;

      default:
	_er_log_debug (ARG_FILE_LINE, "receiver->received: unknown state\n");
	assert_release (false);
	break;
    }
  }

  result receiver::receive (int fd)
  {
    ssize_t bytes;

    bytes = ::recv (fd, m_bufptr + m_received, m_target - m_received, 0);
    if (bytes > 0)
    {
      m_received += bytes;

      assert (m_received <= m_target);
      if (m_received < m_target)
      {
	return result::Partial;
      }

      assert (m_received == m_target);
      return result::Ok;
    }

    if (__builtin_expect (bytes == 0, 0))
    {
      return result::PeerReset;
    }

    switch (errno)
    {
    case EINTR:
      /* retry */
      return result::Partial;
    case EAGAIN:
      /* case EWOULDBLOCK: */
      return result::Pending;
    case EPIPE:
    case ECONNRESET:
      return result::PeerReset;
    default:
      return result::Error;
    }

    return result::Error;
  }

  result receiver::drain (int fd)
  {
    result status;

    while (true)
    {
	status = this->receive (fd);
	switch (status)
	{
	  case result::Ok:
	    this->received ();
	    break;

	  case result::Partial:
	    break;

	  case result::Pending:
	  case result::PeerReset:
	  case result::Error:
	    return status;

	  default:
	    _er_log_debug (ARG_FILE_LINE, "receiver->drain: unknown state\n");
	    assert_release (false);
	    break;
	}
    }

    return result::Error;
  }
}
