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
 * packet_buffer.hpp
 */

#ifndef _PACKET_BUFFER_HPP_
#define _PACKET_BUFFER_HPP_

#ident "$Id$"

#include "assert.h"
#include "span.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <type_traits>
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
      packet_buffer () = delete;
      packet_buffer (int size);
      ~packet_buffer ();

      void clear ();

      template <typename... Spans>
      void push (const cubbase::span<std::byte> &first, const Spans &... rest);

      std::vector<cubbase::span<std::byte>> &get_buffer ();
      std::size_t get_length ();

    private:
      const int m_iovmax;

      std::vector<int> m_header;
      std::vector<cubbase::span<std::byte>> m_buf;
      std::size_t m_length;
  };

  template <typename... Spans>
  void packet_buffer::push (const cubbase::span<std::byte> &first, const Spans &... rest)
  {
    std::size_t size;

    size = first.size() + (rest.size() + ... + 0);
    auto append = [&] (const cubbase::span<std::byte> &s)
    {
      m_buf.push_back (s);
    };

    m_header.push_back (htonl (static_cast<int> (size)));
    m_buf.push_back ({ reinterpret_cast<std::byte *> (&m_header.back ()), sizeof (int) });
    m_length += sizeof (int) + size;
    append (first);
    if constexpr (sizeof... (rest) > 0)
      {
	(append (rest), ...);
      }

    assert (m_buf.size () <= m_iovmax);
  }
}

#endif /* _PACKET_BUFFER_HPP_ */
