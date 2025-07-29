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
 * packet_buffer.cpp
 */

#include "packet_buffer.hpp"
#include <arpa/inet.h>

namespace cubbase
{
  packet_buffer::packet_buffer (int size) :
    m_iovmax (size)
  {
    m_header.reserve (size);
    m_buf.reserve (size);
    m_length = 0;
  }

  packet_buffer::~packet_buffer ()
  {
  }

  void packet_buffer::clear ()
  {
    m_header.clear ();
    m_buf.clear ();
    m_length = 0;
  }

  template <typename... Spans>
  void packet_buffer::push (const cubbase::span<std::byte>& first, const Spans&... rest)
  {
    std::size_t size;

    size = first.size() + (rest.size() + ... + 0);
    auto append = [&](const cubbase::span<std::byte> &s) {
      m_buf.push_back (s);
    };

    m_header.push_back (htonl (static_cast<int> (size)));
    m_buf.push_back ({ reinterpret_cast<std::byte *> (&m_header.back ()), sizeof (int) });
    m_length += sizeof (int) + size;
    append (first);
    if constexpr (sizeof...(rest) > 0)
    {
      (append (rest), ...);
    }

    assert (m_buf.size () <= m_iovmax);
  }

  std::vector<cubbase::span<std::byte>> &packet_buffer::get_buffer ()
  {
    return m_buf;
  }

  std::size_t packet_buffer::get_length ()
  {
    return m_length;
  }
}

