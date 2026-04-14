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
 * packet_buffer.cpp
 */

#include "packet_buffer.hpp"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  packet_buffer::packet_buffer () :
    m_iovmax (8)
  {
    m_buf.reserve (8);
    m_heap.reserve (8);

    this->clear ();
  }

  packet_buffer::packet_buffer (int size) :
    m_iovmax (size)
  {
    m_buf.reserve (size);
    m_heap.reserve (size);

    this->clear ();
  }

  packet_buffer::~packet_buffer ()
  {
  }

  void packet_buffer::clear ()
  {
    m_header.clear ();
    m_buf.clear ();
    m_length = 0;
    m_index = 0;

    for (auto p : m_heap)
      {
	delete[] p;
      }
    m_heap.clear ();

    m_msg.msg_name = nullptr;
    m_msg.msg_namelen = 0;
    m_msg.msg_control = nullptr;
    m_msg.msg_controllen = 0;
    m_msg.msg_flags = 0;

    m_msg.msg_iov = NULL;
    m_msg.msg_iovlen = 0;
  }

  std::size_t packet_buffer::get_length ()
  {
    return m_length;
  }

  void packet_buffer::stamp_msghdr ()
  {
    assert (m_buf.size () != 0);

    if (m_index == 0)
      {
	m_msg.msg_iov = reinterpret_cast<struct ::iovec *> (m_buf.data ());
	m_msg.msg_iovlen = m_buf.size ();
      }
    else
      {
	m_msg.msg_iov = reinterpret_cast<struct ::iovec *> (&m_buf[m_index]);
	m_msg.msg_iovlen = m_buf.size () - m_index;
      }

    assert_release (m_msg.msg_iovlen <= IOV_MAX);
  }

  struct ::msghdr &packet_buffer::get_msghdr ()
  {
    return m_msg;
  }

  void packet_buffer::save_index ()
  {
    size_t delta;

    if (m_msg.msg_iov)
      {
	assert (reinterpret_cast<char *> (m_buf.data ()) <= reinterpret_cast<char *> (m_msg.msg_iov));

	/* previous packets have not yet been sent */
	delta = static_cast<size_t> (reinterpret_cast<char *> (m_msg.msg_iov) - reinterpret_cast<char *> (m_buf.data ()));
	m_index = delta / sizeof (cubbase::span<std::byte>);
      }
    else
      {
	m_index = 0;
      }
  }
}

