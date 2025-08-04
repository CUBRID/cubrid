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

namespace cubbase
{
  packet_buffer::packet_buffer () :
    m_iovmax (8)
  {
    m_header.reserve (8);
    m_buf.reserve (8);
    m_length = 0;
  }

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

    for (auto p : m_heap)
      {
	delete[] p;
      }
    m_heap.clear ();
  }

  std::vector<cubbase::span<std::byte>> &packet_buffer::get_buffer ()
  {
    return m_buf;
  }

  std::size_t packet_buffer::get_length ()
  {
    return m_length;
  }

  struct ::msghdr &packet_buffer::get_msghdr ()
  {
    m_msg.msg_name = nullptr;
    m_msg.msg_namelen = 0;
    m_msg.msg_control = nullptr;
    m_msg.msg_controllen = 0;
    m_msg.msg_flags = 0;

    assert (m_buf.size () != 0);

    m_msg.msg_iov = reinterpret_cast<struct ::iovec *> (m_buf.data ());
    m_msg.msg_iovlen = m_buf.size ();

    return m_msg;
  }
}

