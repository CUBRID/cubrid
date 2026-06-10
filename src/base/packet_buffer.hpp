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
 * packet_buffer.hpp
 */

#ifndef _PACKET_BUFFER_HPP_
#define _PACKET_BUFFER_HPP_

#ident "$Id$"

#include "error_manager.h"
#include "span.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <type_traits>
#include <deque>
#include <vector>
#include <cstddef>

static_assert (sizeof (cubbase::span<std::byte>) == sizeof (struct ::iovec), "size mismatch");
static_assert (alignof (cubbase::span<std::byte>) == alignof (struct ::iovec), "alignment mismatch");
static_assert (std::is_standard_layout<cubbase::span<std::byte>>::value, "not standard layout");
static_assert (offsetof (cubbase::span<std::byte>, _data) == offsetof (struct ::iovec, iov_base),
	       "pointer offset mismatch");
static_assert (offsetof (cubbase::span<std::byte>, _size) == offsetof (struct ::iovec, iov_len),
	       "size offset mismatch");

namespace cubbase
{
  class packet_buffer
  {
    public:
      packet_buffer ();
      packet_buffer (int size);
      virtual ~packet_buffer ();

      void clear ();

      template <typename T>
      inline T *allocate ();

      template <typename... Spans>
      void push_for_send (const cubbase::span<std::byte> &first, const Spans &... rest);
      template <typename... Spans>
      void push (const cubbase::span<std::byte> &first, const Spans &... rest);

      std::size_t get_length ();

      void stamp_msghdr ();
      struct ::msghdr &get_msghdr ();

    private:
      const std::size_t m_iovmax;

      std::deque<int> m_header;
      std::vector<cubbase::span<std::byte>> m_buf;
      std::vector<std::byte *> m_heap;
      std::size_t m_length;
      std::size_t m_index;

      struct ::msghdr m_msg;

      void save_index ();
  };

  template <typename T>
  inline T *packet_buffer::allocate ()
  {
    std::byte *mem;

    mem = new std::byte[sizeof (T)];
    m_heap.push_back (mem);
    return reinterpret_cast<T *> (mem);
  }

  template <typename... Spans>
  void packet_buffer::push_for_send (const cubbase::span<std::byte> &first, const Spans &... rest)
  {
    std::size_t size;

    this->save_index ();

    size = first.size () + (rest.size () + ... + 0);
    auto append = [&] (const cubbase::span<std::byte> &s)
    {
      if (s.size () != 0)
	{
	  m_buf.push_back (s);
	}
    };

    m_header.push_back (htonl (static_cast<int> (size)));
    m_buf.push_back ({ reinterpret_cast<std::byte *> (&m_header.back ()), sizeof (int) });
    m_length += sizeof (int) + size;
    append (first);
    if constexpr (sizeof... (rest) > 0)
      {
	(append (rest), ...);
      }

    assert_release (m_buf.size () <= m_iovmax);
  }

  template <typename... Spans>
  void packet_buffer::push (const cubbase::span<std::byte> &first, const Spans &... rest)
  {
    this->save_index ();

    m_length += first.size() + (rest.size() + ... + 0);
    auto append = [&] (const cubbase::span<std::byte> &s)
    {
      m_buf.push_back (s);
    };

    append (first);
    if constexpr (sizeof... (rest) > 0)
      {
	(append (rest), ...);
      }

    assert_release (m_buf.size () <= m_iovmax);
  }
}

#endif /* _PACKET_BUFFER_HPP_ */
