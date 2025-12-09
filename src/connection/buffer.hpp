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
 * buffer.hpp
 */

#ifndef _CONNECTION_BUFFER_HPP_
#define _CONNECTION_BUFFER_HPP_

#include "packet_buffer.hpp"
#include "error_manager.h"

#include <tuple>
#include <array>
#include <cstring>
#include <cstddef>
#include <type_traits>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  constexpr size_t BUFFER_SIZE = 1024;

  enum class result
  {
    Ok,
    Partial,
    Error,
    Reset,
    Pending,
    BudgetExhausted,
    PeerReset,
    RefuseConnection,
    ClosedConnection,
    Aborted,
    Skewed
  };

  class buffer
  {
    public:
      bool set_data (const void *data, size_t size) noexcept;

      std::pair<const std::byte *, std::size_t> remaining_data () const noexcept;
      std::pair<std::byte *, std::size_t> remaining_space () noexcept;

      void advance (std::size_t bytes) noexcept;
      bool is_complete () const noexcept;
      bool has_data () const noexcept;
      bool has_space () const noexcept;

      std::size_t position () const noexcept;
      std::size_t total_size () const noexcept;

      void mark_consumed ();
      void set_target_size (size_t target);

      void reset ();

      template<typename T>
      bool set_data (const T &data) noexcept
      {
	static_assert (std::is_trivially_copyable_v<T>);

	if (sizeof (T) > m_data.size ())
	  {
	    _er_log_debug (__FILE__, __LINE__, "master_buffer->set_data: data too large for buffer.");
	    return false;
	  }
	std::memcpy (m_data.data (), &data, sizeof (T));
	m_pos = 0;
	m_size = sizeof (T);

	return true;
      }

      template<typename T>
      const T *data_as () const noexcept
      {
	static_assert (std::is_trivially_copyable_v<T>);

	if (m_pos < sizeof (T))
	  {
	    return nullptr;
	  }

	return reinterpret_cast<const T *> (m_data.data ());
      }

      template<typename T>
      bool is_ready_for () const noexcept
      {
	return m_pos >= sizeof (T);
      }

    private:
      std::array<std::byte, BUFFER_SIZE> m_data;
      std::size_t m_pos = 0;
      std::size_t m_size = 0;
  };

  class buffered_socket
  {
    public:
      [[gnu::hot]]
      static result send_partial (int fd, cubbase::packet_buffer &buffer) noexcept
      {
	struct ::msghdr *msg;
	std::size_t advance;
	ssize_t bytes;

	msg = &buffer.get_msghdr ();
	while (msg->msg_iovlen)
	  {
	    bytes = ::sendmsg (fd, msg, MSG_NOSIGNAL);
	    if (bytes < 0 && errno == EINTR)
	      {
		/* interrupted by signal, retry */
		continue;
	      }
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
		_er_log_debug (__FILE__, __LINE__, "socket_io->send_partial: send returned 0 - error: %s", strerror (errno));
		return result::PeerReset;
	      }

	    switch (errno)
	      {
	      case EPIPE:
	      case ECONNRESET:
		_er_log_debug (__FILE__, __LINE__, "socket_io->send_partial: client disconnected: %s", strerror (errno));
		return result::PeerReset;

	      case EAGAIN:
#if defined (EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	      case EWOULDBLOCK:
#endif
		return result::Pending;

	      default:
		_er_log_debug (__FILE__, __LINE__, "socket_io->send_partial: unexpected send error: %s", strerror (errno));
		return result::Error;
	      }
	  }

	return result::Ok;
      }

      [[gnu::hot]]
      static result recv_partial (int fd, buffer &buffer)
      {
	std::byte *space;
	std::size_t available;
	ssize_t n;

	if (!buffer.has_space ())
	  {
	    _er_log_debug (__FILE__, __LINE__, "socket_io->recv_partial: out of buffer");
	    return result::Error;
	  }

	std::tie (space, available) = buffer.remaining_space ();
	if (!space)
	  {
	    _er_log_debug (__FILE__, __LINE__, "socket_io->recv_partial: out of buffer");
	    return result::Error;
	  }

	assert (buffer.total_size () - buffer.position () <= available);

	n = ::recv (fd, space, buffer.total_size () - buffer.position (), 0);
	if (n > 0)
	  {
	    buffer.advance (n);
	  }
	else if (n < 0)
	  {
	    if (errno == EAGAIN || errno == EWOULDBLOCK)
	      {
		return result::Pending;
	      }
	    else
	      {
		_er_log_debug (__FILE__, __LINE__, "socket_io->recv_partial: recv error: %s", strerror (errno));
		return result::Error;
	      }
	  }
	else
	  {
	    _er_log_debug (__FILE__, __LINE__, "socket_io->recv_partial: recv returned 0 - error: %s", strerror (errno));
	    return result::PeerReset;
	  }

	return buffer.is_complete () ? result::Ok : result::Pending;
      }

      /* recv helper */
      template<typename T>
      [[gnu::hot]]
      static std::tuple<result, const T *> read_fixed_size (int fd, buffer &buffer)
      {
	result status;

	static_assert (std::is_trivially_copyable_v<T>);

	if (buffer.total_size () != sizeof (T))
	  {
	    buffer.set_target_size (sizeof (T));
	  }

	status = recv_partial (fd, buffer);
	if (status == result::Ok)
	  {
	    assert_release (buffer.position () == sizeof (T));
	  }
	return { status, buffer.data_as<T> () };
      }
  };
}

#endif
