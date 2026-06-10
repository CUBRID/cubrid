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
 * receiver.cpp
 */

#include "system_parameter.h"
#include "connection_defs.h"
#include "receiver.hpp"
#include "error_manager.h"
#include "span.hpp"
#include "object_primitive.h"

#include <algorithm>
#include <unistd.h>
#include <errno.h>
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
    er_log_conn (__FILE__, __LINE__, "receiver (%p) state %d -> state = %d\n", this, m_state, state::x); \
    (m_state = state::x); \
} while (0)

constexpr std::size_t SIZE_HEADER = sizeof (int);
constexpr std::size_t SIZE_HEADER_PADDING = sizeof (int);

namespace cubconn
{
  receiver::receiver (std::size_t capacity, statistics::metrics<statistics::context> *stats) :
    m_stats (stats),
    m_buf (capacity)
  {
  }

  receiver::receiver () :
    m_buf ()
  {
  }

  receiver::~receiver ()
  {
#if !defined (NDEBUG)
    if (m_allocated.size () > 0)
      {
	er_log_conn (ARG_FILE_LINE, "receiver: found unreleased memory first = %p\n", m_allocated[0]);
	assert (false);
      }
#endif
  }

  bool receiver::prepare ()
  {
    if (!m_buf.prepare ())
      {
	return false;
      }

    m_result.reserve (8);
    this->reset ();
    return true;
  }

  void receiver::reset ()
  {
    cubbase::span<std::byte> buffer;

    m_state = state::Recv;

    m_received = 0;
    m_size = 0;

    m_result.clear ();
    m_allocated.clear ();

    /* if m_buf is already in use, it may be corrupted by subsequent reception. */
    m_buf.reset ();

    buffer = m_buf.buffer ();
    m_bufptr = buffer.data ();
    m_bufsize = buffer.size ();
    m_tmpsize = 0;
  }

  result receiver::to_result (ssize_t bytes, int errid)
  {
    if (__builtin_expect (bytes == 0, 0))
      {
	return result::PeerReset;
      }

    switch (errid)
      {
      case EINTR:
	/* retry */
	return result::Partial;
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

    return result::Error;
  }

  void receiver::parse_packet (bool is_buffer)
  {
    cubbase::span<std::byte> buffer;
    std::uint32_t aligned;
    int padding;

    assert (m_size > 0);

    while (m_received >= SIZE_HEADER + m_size)
      {
	er_log_conn (ARG_FILE_LINE, "receiver->parse_packet: m_bufptr = %p, m_received = %d, m_size = %d, after parse = %d\n",
		     m_bufptr + SIZE_HEADER + SIZE_HEADER_PADDING, m_received, m_size, m_received - SIZE_HEADER - m_size);

	assert ((reinterpret_cast<uintptr_t> (m_bufptr) & 7) == 0);

	std::memcpy (&aligned, m_bufptr + SIZE_HEADER, sizeof (std::uint32_t));
	padding = ntohl (aligned);
	assert_release (padding >= 0 && padding < 8);

	*reinterpret_cast<int *> (m_bufptr + SIZE_HEADER) = padding;

	er_log_conn (ARG_FILE_LINE, "receiver->parse_packet: packet size = %d, padding = %d, push size = %d.\n", m_size,
		     SIZE_HEADER_PADDING + padding, m_size - SIZE_HEADER_PADDING - padding);

	m_result.emplace_back (m_bufptr + SIZE_HEADER + SIZE_HEADER_PADDING, m_size - SIZE_HEADER_PADDING - padding);
	m_received -= SIZE_HEADER + m_size;
	if (is_buffer)
	  {
	    m_buf.commit (SIZE_HEADER + m_size);
	  }

	buffer = m_buf.buffer ();
	m_bufptr = buffer.data ();
	m_bufsize = buffer.size ();

	if (m_received < SIZE_HEADER)
	  {
	    break;
	  }
	std::memcpy (&aligned, m_bufptr, sizeof (std::uint32_t));
	m_size = ntohl (aligned);
      }
    er_log_conn (ARG_FILE_LINE, "receiver->parse_packet: remains = %d.\n", m_received);
  }

  result receiver::parse_size_in_tmpsize (size_t consumption, size_t limit)
  {
    cubbase::span<std::byte> mem, buffer;
    std::byte *ptr;

    assert (m_received >= SIZE_HEADER);

    m_size = ntohl (m_tmpsize);
    if (m_size == 0)
      {
	er_log_conn (ARG_FILE_LINE, "receiver->parse_size_in_tmpsize: zero-length frame: peer sent a size of 0.\n");
	assert_release (false);
      }

    ptr = new std::byte[SIZE_HEADER + m_size];
    if (!ptr)
      {
	return result::Error;
      }
    er_log_conn (ARG_FILE_LINE,
		 "receiver->parse_size_in_tmpsize: allocate new memory for packet. m_bufsize = %d, m_size = %d\n",
		 m_bufsize, m_size);

    assert (m_received == SIZE_HEADER);
    std::memcpy (ptr, &m_tmpsize, m_received);

    m_allocated.push_back (ptr);

    m_bufptr = ptr;
    m_bufsize = SIZE_HEADER + m_size;
    NEXT_STATE (RecvInAllocated);

    if (limit > 0 && consumption >= limit)
      {
	er_log_conn (ARG_FILE_LINE, "receiver->parse_size_in_tmpsize: budget exhausted.\n");
	return result::BudgetExhausted;
      }
    return result::Partial;
  }

  result receiver::parse_size (size_t consumption, size_t limit)
  {
    cubbase::span<std::byte> mem, buffer;
    std::byte *ptr;
    std::uint32_t aligned;

    assert (m_received >= SIZE_HEADER);

    std::memcpy (&aligned, m_bufptr, sizeof (std::uint32_t));
    m_size = ntohl (aligned);
    if (m_size == 0)
      {
	er_log_conn (ARG_FILE_LINE, "receiver->parse_size: zero-length frame: peer sent a size of 0.\n");
	assert_release (false);
      }

    if (m_received >= SIZE_HEADER + m_size)
      {
	this->parse_packet (true);

	if (m_received >= SIZE_HEADER)
	  {
	    NEXT_STATE (ParseSize);

	    if (limit > 0 && consumption >= limit)
	      {
		er_log_conn (ARG_FILE_LINE, "receiver->parse_size: budget exhausted.\n");
		return result::BudgetExhausted;
	      }
	    return result::Partial;
	  }

	/* m_received < SIZE_HEADER */

	if (m_bufsize < SIZE_HEADER + SIZE_HEADER_PADDING + sizeof (NET_HEADER))
	  {
	    /* need to recv the size header but the buffer was not large enough */
	    std::memcpy (&m_tmpsize, m_bufptr, m_received);
	    NEXT_STATE (RecvSizeInTmp);
	    return result::Partial;
	  }

	/* m_received < SIZE_HEADER */
	/* m_bufsize >= SIZE_HEADER + SIZE_HEADER_PADDING + sizeof (NET_HEADER) */

	NEXT_STATE (Recv);

	if (limit > 0 && consumption >= limit)
	  {
	    er_log_conn (ARG_FILE_LINE, "receiver->parse_size: budget exhausted.\n");
	    return result::BudgetExhausted;
	  }
	return result::Partial;
      }

    /* m_received < SIZE_HEADER + m_size */

    if (SIZE_HEADER + m_size > m_bufsize)
      {
	ptr = new std::byte[SIZE_HEADER + m_size];
	if (!ptr)
	  {
	    return result::Error;
	  }
	er_log_conn (ARG_FILE_LINE, "receiver->parse_size: allocate new memory for packet. m_bufsize = %d, m_size = %d\n",
		     m_bufsize, m_size);
	std::memcpy (ptr, m_bufptr, m_received);

	m_allocated.push_back (ptr);
#if !defined (NDEBUG)
	std::memcpy (&aligned, ptr, sizeof (std::uint32_t));
	assert (m_size == ntohl (aligned));
#endif
	m_bufptr = ptr;
	m_bufsize = SIZE_HEADER + m_size;
	NEXT_STATE (RecvInAllocated);

	if (limit > 0 && consumption >= limit)
	  {
	    er_log_conn (ARG_FILE_LINE, "receiver->parse_size: budget exhausted.\n");
	    return result::BudgetExhausted;
	  }
	return result::Partial;
      }

    assert (m_bufsize - m_received != 0);

    /* m_bufsize >= SIZE_HEADER + m_size */

    NEXT_STATE (Recv);

    if (limit > 0 && consumption >= limit)
      {
	er_log_conn (ARG_FILE_LINE, "receiver->parse_size: budget exhausted.\n");
	return result::BudgetExhausted;
      }
    return result::Partial;
  }

  result receiver::receive_in_allocated (int fd, size_t &consumption, size_t limit)
  {
    ssize_t bytes;

    /* receive data from socket */
    assert (m_bufsize - m_received > 0);
    if (limit > 0)
      {
	bytes = ::recv (fd, reinterpret_cast<char *> (m_bufptr) + m_received, std::min (m_bufsize - m_received, limit), 0);
      }
    else
      {
	bytes = ::recv (fd, reinterpret_cast<char *> (m_bufptr) + m_received, m_bufsize - m_received, 0);
      }
    if (bytes > 0)
      {
	m_received += bytes;
	consumption += bytes;

	m_stats->add (statistics::context::BYTES_IN_TOTAL, bytes);
	er_log_conn (ARG_FILE_LINE, "receiver->receive_in_allocated: state = %d, received = %d, accumulated = %d\n",
		     (int) m_state,
		     (int) bytes, (int) m_received);

	if (m_received < SIZE_HEADER + m_size)
	  {
	    return result::Partial;
	  }
	assert (m_received == SIZE_HEADER + m_size);
	this->parse_packet (false);
	assert (m_received == 0);

	assert (m_bufptr == m_buf.buffer ().data ());
	assert (m_bufsize == m_buf.buffer ().size ());

	if (m_bufsize < SIZE_HEADER + SIZE_HEADER_PADDING + sizeof (NET_HEADER))
	  {
	    NEXT_STATE (RecvSizeInTmp);
	  }
	else
	  {
	    NEXT_STATE (Recv);
	  }

	if (limit > 0 && consumption >= limit)
	  {
	    er_log_conn (ARG_FILE_LINE, "receiver->receive_in_allocated: budget exhausted.\n");
	    return result::BudgetExhausted;
	  }
	return result::Ok;
      }

    return this->to_result (bytes, errno);
  }

  result receiver::receive_in_tmpsize (int fd, size_t &consumption)
  {
    ssize_t bytes;

    /* receive data from socket */
    assert (SIZE_HEADER - m_received > 0);
    bytes = ::recv (fd, reinterpret_cast<char *> (&m_tmpsize) + m_received, SIZE_HEADER - m_received, 0);
    if (bytes > 0)
      {
	m_received += bytes;
	consumption += bytes;

	m_stats->add (statistics::context::BYTES_IN_TOTAL, bytes);
	er_log_conn (ARG_FILE_LINE, "receiver->receive_in_tmpsize: state = %d, received = %d, accumulated = %d\n",
		     (int) m_state,
		     (int) bytes, (int) m_received);

	if (m_received < SIZE_HEADER)
	  {
	    return result::Partial;
	  }
	NEXT_STATE (ParseSizeInTmp);
	return result::Ok;
      }

    return this->to_result (bytes, errno);
  }

  result receiver::receive (int fd, size_t &consumption, size_t limit)
  {
    ssize_t bytes;

    er_log_conn (__FILE__, __LINE__, "receiver (%p) drain from fd = %d\n", this, fd);

    /* receive data from socket */
    assert (m_bufsize - m_received > 0);
    if (limit > 0)
      {
	bytes = ::recv (fd, reinterpret_cast<char *> (m_bufptr) + m_received, std::min (m_bufsize - m_received, limit), 0);
      }
    else
      {
	bytes = ::recv (fd, reinterpret_cast<char *> (m_bufptr) + m_received, m_bufsize - m_received, 0);
      }
    if (bytes > 0)
      {
	m_received += bytes;
	consumption += bytes;

	m_stats->add (statistics::context::BYTES_IN_TOTAL, bytes);
	er_log_conn (ARG_FILE_LINE, "receiver->receive: state = %d, received = %d, accumulated = %d\n", (int) m_state,
		     (int) bytes, (int) m_received);
	if (m_received < SIZE_HEADER)
	  {
	    return result::Partial;
	  }
	NEXT_STATE (ParseSize);

	return result::Ok;
      }

    return this->to_result (bytes, errno);
  }

  result receiver::drain (int fd, size_t limit)
  {
    result status;
    size_t consumption;

    consumption = 0;
    m_result.clear ();
    while (true)
      {
	switch (m_state)
	  {
	  case state::Recv:
	    status = this->receive (fd, consumption, limit);
	    break;

	  case state::RecvSizeInTmp:
	    status = this->receive_in_tmpsize (fd, consumption);
	    break;

	  case state::RecvInAllocated:
	    status = this->receive_in_allocated (fd, consumption, limit);
	    break;

	  case state::ParseSize:
	    status = this->parse_size (consumption, limit);
	    break;

	  case state::ParseSizeInTmp:
	    status = this->parse_size_in_tmpsize (consumption, limit);
	    break;

	  default:
	    status = result::Error;
	    er_log_conn (ARG_FILE_LINE, "receiver->drain: unknown state\n");
	    assert_release (false);
	    break;
	  }

	switch (status)
	  {
	  case result::Partial:
	  case result::Ok:
	    break;

	  case result::BudgetExhausted:
	    m_stats->add (statistics::context::RECV_BUDGET_HIT, 1);
	    [[fallthrough]];

	  case result::Pending:
	  case result::Error:
	  case result::PeerReset:
	    return status;

	  default:
	    er_log_conn (ARG_FILE_LINE, "receiver->drain: unknown state\n");
	    assert_release (false);
	    break;
	  }
      }

    return result::Error;
  }

  void receiver::release (std::byte *ptr)
  {
    cubbase::span<std::byte> source;
    int size;

    if (m_buf.is_in (ptr))
      {
	size = ntohl (*reinterpret_cast<int *> (ptr - (SIZE_HEADER + SIZE_HEADER_PADDING)));
	source = cubbase::span<std::byte> (ptr - (SIZE_HEADER + SIZE_HEADER_PADDING), size + SIZE_HEADER);

	m_buf.restore (source);
      }
    else
      {
	auto it = std::find (m_allocated.begin (), m_allocated.end (), ptr - (SIZE_HEADER + SIZE_HEADER_PADDING));
	if (it == m_allocated.end ())
	  {
	    er_log_conn (ARG_FILE_LINE, "receiver: memory = %p does not belong to this receiver\n",
			 ptr - (SIZE_HEADER + SIZE_HEADER_PADDING));
	    assert_release (false);
	    return;
	  }
	m_allocated.erase (it);

	delete[] (ptr - (SIZE_HEADER + SIZE_HEADER_PADDING));
      }
  }

  std::vector<cubbase::span<std::byte>> *receiver::get_result ()
  {
    return &m_result;
  }
}

