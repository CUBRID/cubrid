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
 * buffer.cpp
 */

#include "error_manager.h"
#include "buffer.hpp"

#include <array>
#include <cstring>
#include <cstddef>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubconn
{
  bool buffer::set_data (const void *data, size_t size) noexcept
  {
    if (size > m_data.size ())
      {
	_er_log_debug (__FILE__, __LINE__, "master_buffer->set_data: data too large for buffer.");
	return false;
      }

    std::memcpy (m_data.data (), data, size);
    m_pos = 0;
    m_size = size;

    return true;
  }

  /* for send */
  std::pair<const std::byte *, std::size_t> buffer::remaining_data () const noexcept
  {
    assert (m_pos <= m_size);

    if (m_pos == m_size)
      {
	return { nullptr, 0 };
      }
    return { m_data.data() + m_pos, m_size - m_pos };
  }

  /* for recv */
  std::pair<std::byte *, std::size_t> buffer::remaining_space () noexcept
  {
    assert (m_pos <= m_data.size ());

    if (m_pos >= m_data.size ())
      {
	return { nullptr, 0 };
      }
    return { m_data.data() + m_pos, m_data.size() - m_pos };
  }

  void buffer::advance (std::size_t bytes) noexcept
  {
    assert (m_pos + bytes <= m_size);
    assert (m_pos + bytes <= m_data.size ());

    m_pos += bytes;
  }

  bool buffer::is_complete () const noexcept
  {
    assert (m_pos <= m_size);

    return m_pos == m_size;
  }

  bool buffer::has_data () const noexcept
  {
    return m_size > 0 && m_pos < m_size;
  }

  bool buffer::has_space () const noexcept
  {
    return m_pos < m_data.size ();
  }

  std::size_t buffer::position () const noexcept
  {
    return m_pos;
  }

  std::size_t buffer::total_size () const noexcept
  {
    return m_size;
  }

  void buffer::mark_consumed ()
  {
    reset ();
  }

  void buffer::set_target_size (size_t target)
  {
    assert (target <= m_data.size ());

    m_size = target;
    m_pos = 0;
  }

  void buffer::reset()
  {
    m_pos = 0;
    m_size = 0;
  }
}
