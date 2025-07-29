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

#include <sys/socket.h>
#include <sys/types.h>
#include <vector>
#include <cstddef>
#include <iterator>

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
    void push (const cubbase::span<std::byte>& first, const Spans&... rest);

    std::vector<cubbase::span<std::byte>> &get_buffer ();
    std::size_t get_length ();

  private:
    const int m_iovmax;

    std::vector<int> m_header;
    std::vector<cubbase::span<std::byte>> m_buf;
    std::size_t m_length;
  };
}

#endif /* _PACKET_BUFFER_HPP_ */
