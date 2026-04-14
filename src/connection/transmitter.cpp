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
 * transmitter.cpp
 */

#include "system_parameter.h"
#include "connection_defs.h"
#include "connection_statistics.hpp"
#include "transmitter.hpp"
#include "error_manager.h"
#include "span.hpp"
#include "object_primitive.h"

#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if 0
#define er_log_conn(...) er_log_debug (__VA_ARGS__)
#else
#define er_log_conn(...)
#endif

#define NEXT_STATE(x) do { \
    _er_log_debug (__FILE__, __LINE__, "transmitter state %d -> state = %d\n", m_state, state::x); \
    (m_state = state::x); \
} while (0)

namespace cubconn
{
  transmitter::transmitter (statistics::metrics<statistics::context> *stats) :
    m_buf (IOV_MAX),
    m_stats (stats)
  {
    m_deleter.reserve (IOV_MAX);
  }

  transmitter::transmitter ()
  {
  }

  transmitter::~transmitter ()
  {
  }

  result transmitter::fill (int fd, int limit)
  {
    struct ::msghdr *msg;
    std::size_t advance;
    ssize_t bytes, consumption;

    msg = &m_buf.get_msghdr ();
    consumption = 0;
    while (msg->msg_iovlen)
      {
	if (limit > 0 && consumption >= limit)
	  {
	    m_stats->add (statistics::context::SEND_BUDGET_HIT, 1);
	    return result::BudgetExhausted;
	  }

	bytes = ::sendmsg (fd, msg, MSG_NOSIGNAL);
	er_log_conn (__FILE__, __LINE__, "transmitter->fill: sendmsg returned fd = %d, bytes = %u\n", fd, bytes);
	if (bytes > 0)
	  {
	    m_stats->add (statistics::context::BYTES_OUT_TOTAL, bytes);
	    consumption += bytes;

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
	    return result::PeerReset;
	  }

	switch (errno)
	  {
	  case EINTR:
	    /* retry */
	    continue;
	  case EAGAIN:
#if defined (EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
	    return result::Pending;
	  case EPIPE:
	  case ECONNRESET:
	    return result::PeerReset;
	  default:
	    return result::Error;
	  }
      }
    return result::Ok;
  }

  void transmitter::push_for_deleter (std::function<void ()> &&deleter)
  {
    if (!deleter)
      {
	return;
      }

    m_deleter.push_back (std::move (deleter));
  }

  void transmitter::stamp ()
  {
    m_buf.stamp_msghdr ();
  }

  bool transmitter::empty ()
  {
    return m_buf.get_msghdr ().msg_iovlen == 0;
  }

  void transmitter::clear ()
  {
    for (auto &deleter : m_deleter)
      {
	if (deleter)
	  {
	    deleter ();
	  }
      }
    m_deleter.clear ();
    m_buf.clear ();
  }
}
