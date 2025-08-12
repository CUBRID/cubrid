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

#include "connection_defs.h"
#include "receiver.hpp"
#include "error_manager.h"
#include "span.hpp"
#include "object_primitive.h"

#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#define NEXT_STATE(x) do { \
    _er_log_debug (__FILE__, __LINE__, "receiver set state = %d\n", state::x); \
    (m_state = state::x); \
} while (0)

constexpr std::size_t SIZE_HEADER = sizeof (int);

namespace cubconn
{
  receiver::receiver (std::size_t capacity) :
    m_buf (capacity)
  {
    m_result.reserve (8);
    this->reset ();
  }

  receiver::~receiver ()
  {
#if !defined (NDEBUG)
    if (m_allocated.size () > 0)
    {
      _er_log_debug (ARG_FILE_LINE, "receiver: found unreleased memory first = %p\n", m_allocated[0]);
      assert (false);
    }
#endif
  }

  void receiver::reset ()
  {
    cubbase::span<std::byte> buffer;

    m_state = state::Recv;

    m_received = 0;
    m_size = 0;

    buffer = m_buf.buffer ();
    m_bufptr = buffer.data ();
    m_bufsize = buffer.size ();
    m_tmpsize = -1;
    
    m_result.clear ();
    /* if m_buf is already in use, it may be corrupted by subsequent reception. */
    m_buf.reset ();
  }

  void receiver::parse_packet (bool is_buffer)
  {
    cubbase::span<std::byte> buffer;
    std::uint32_t aligned;

    assert (m_size > 0);

    while (m_received >= SIZE_HEADER + m_size)
    {
      _er_log_debug (ARG_FILE_LINE, "receiver->parse_packet: m_bufptr = %p, m_received = %d, m_size = %d, after parse = %d\n",
		     m_bufptr + SIZE_HEADER, m_received, m_size, m_received - SIZE_HEADER - m_size);

      m_result.emplace_back (m_bufptr + SIZE_HEADER, m_size);
      m_received -= SIZE_HEADER + m_size;
      if (is_buffer)
      {
	m_buf.commit (SIZE_HEADER + m_size);
      }

      buffer = m_buf.buffer ();
#if !defined (NDEBUG)
      if (is_buffer)
	{
	  assert (m_bufptr + SIZE_HEADER + m_size == buffer.data ());
	}
#endif
      m_bufptr = buffer.data ();
      m_bufsize = buffer.size ();

      if (m_received < SIZE_HEADER)
      {
	break;
      }
      std::memcpy (&aligned, m_bufptr, sizeof (std::uint32_t));
      m_size = ntohl (aligned);
    }
    _er_log_debug (ARG_FILE_LINE, "receiver->parse_packet: remains = %d.\n", m_received);
  }

  result receiver::parse_size ()
  {
    std::byte *ptr;
    std::uint32_t aligned;

    assert (m_received >= SIZE_HEADER);

    if (__builtin_expect (m_tmpsize < 0, 1))
    {
      std::memcpy (&aligned, m_bufptr, sizeof (std::uint32_t));
      m_size = ntohl (aligned);
    }
    else
    {
      m_size = ntohl (m_tmpsize);
      m_tmpsize = -1;
    }
    if (m_size == 0)
    {
      _er_log_debug (ARG_FILE_LINE, "receiver->parse_size: ths size of received packet is 0.\n");
      assert_release (false);
    }
    
    NEXT_STATE (Recv);
    if (m_received >= SIZE_HEADER + m_size)
    {
      this->parse_packet (true);

      /* 1. there are only less than 4 bytes.. */
      /* 2. m_received < m_size (= need recv) */
      if (m_received < SIZE_HEADER && m_bufsize < SIZE_HEADER + sizeof (NET_HEADER))
      {
	/* need to recv the size header but the buffer was not large enough */
	std::memcpy (reinterpret_cast<std::byte *> (&m_tmpsize) + m_received, m_bufptr, SIZE_HEADER - m_received);
	NEXT_STATE (RecvSizeInTmp);
      }
    }
    else
    {
      if (SIZE_HEADER + m_size > m_bufsize)
      {
	ptr = new std::byte[SIZE_HEADER + m_size];
	if (!ptr)
	{
	  return result::Error;
	}
      _er_log_debug (ARG_FILE_LINE, "receiver->parse_size: allocate new memory for packet. m_bufsize = %d, m_size = %d\n", m_bufsize, m_size);
	std::memcpy (ptr, m_bufptr, m_received);

#if !defined (NDEBUG)
	m_allocated.push_back (ptr);

	std::memcpy (&aligned, ptr, sizeof (std::uint32_t));
	assert (m_size == ntohl (aligned));
#endif

	m_bufptr = ptr;
	m_bufsize = SIZE_HEADER + m_size;
	NEXT_STATE (RecvInAllocated);
      }
    }
    return result::Partial;
  }

  result receiver::receive (int fd)
  {
    std::byte *buf;
    std::size_t length;
    ssize_t bytes;

    /* buffer selection */
    if (__builtin_expect (m_state == state::RecvSizeInTmp, 0))
    {
      buf = reinterpret_cast<std::byte *> (&m_tmpsize) + m_received;
      length = SIZE_HEADER - m_received;
    }
    else
    {
      buf = m_bufptr + m_received;
      length = m_bufsize - m_received;
    }
    /* receive */
    bytes = ::recv (fd, buf, length, 0);
    if (bytes > 0)
    {
      m_received += bytes;
      _er_log_debug (ARG_FILE_LINE, "receiver->receiver: received = %d, accumulated = %d\n", bytes, m_received);
      if (m_state == state::RecvInAllocated)
      {
	if (m_received < SIZE_HEADER + m_size)
	{
	  return result::Partial;
	}
	assert (m_received == SIZE_HEADER + m_size);
	this->parse_packet (false);
	assert (m_received == 0);
	if (m_bufsize < SIZE_HEADER + sizeof (NET_HEADER))
	{
	  NEXT_STATE (RecvSizeInTmp);
	}
	else
	{
	  NEXT_STATE (Recv);
	}
      }
      else
      {
	if (m_received < SIZE_HEADER)
	{
	  return result::Partial;
	}
	NEXT_STATE (ParseSize);
      }

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
    
    m_result.clear ();
    while (true)
    {
	switch (m_state)
	{
	  case state::Recv:
	  case state::RecvInAllocated:
	  case state::RecvSizeInTmp:
	    status = this->receive (fd);
	    break;

	  case state::ParseSize:
	    status = this->parse_size ();
	    break;

	  default:
	    _er_log_debug (ARG_FILE_LINE, "receiver->drain: unknown state\n");
	    assert_release (false);
	    break;
	}

	switch (status)
	{
	  case result::Partial:
	  case result::Ok:
	    break;

	  case result::Pending:
	      return m_result.size () > 0 ? result::Ok : result::Pending;

	  case result::Error:
	  case result::PeerReset:
	    return status;

	  default:
	    _er_log_debug (ARG_FILE_LINE, "receiver->drain: unknown state\n");
	    assert_release (false);
	    break;
	}
    }

    return result::Error;
  }

  void receiver::release (cubbase::span<std::byte> &mem)
  {
    cubbase::span<std::byte> source;

    if (m_buf.is_in (mem))
    {
      source = cubbase::span<std::byte> (mem.data () - SIZE_HEADER, mem.size () + SIZE_HEADER);
      m_buf.restore (source);
    }
    else
    {
#if !defined (NDEBUG)
      auto it = std::find (m_allocated.begin (), m_allocated.end (), mem.data () - SIZE_HEADER);
      if (it == m_allocated.end ())
      {
	_er_log_debug (ARG_FILE_LINE, "receiver: memory = %p is not the receiver that belongs to\n", mem.data () - SIZE_HEADER);
	assert (false);
      }
      m_allocated.erase (it);
#endif
      delete[] (mem.data () - SIZE_HEADER);
    }
  }

  std::vector<cubbase::span<std::byte>> *receiver::get_result ()
  {
    return &m_result;
  }
}
