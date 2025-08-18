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
 * receiver.hpp
 */

#ifndef _CONNECTION_RECEIVER_HPP_
#define _CONNECTION_RECEIVER_HPP_

#include "span.hpp"
#include "buffer.hpp"
#include "DMRBMemoryPool.hpp"

#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  class receiver
  {
    private:
      enum class state
      {
	Recv,
	RecvSizeInTmp,
	RecvInAllocated,

	ParseSize,
	ParseSizeInTmp
      };

    public:
      receiver (std::size_t capacity);
      ~receiver ();

      void reset ();

      result drain (int fd);
      void release (cubbase::span<std::byte> &mem);

      std::vector<cubbase::span<std::byte>> *get_result ();

    private:
      state m_state;
      cubbase::DMRBMemoryPool m_buf;

      std::size_t m_received;
      std::size_t m_size;
      std::byte *m_bufptr;
      std::size_t m_bufsize;

      int m_tmpsize;

      std::vector<std::byte *> m_allocated;

      /* output */
      std::vector<cubbase::span<std::byte>> m_result;

      result to_result (ssize_t bytes, int errid);

      void parse_packet (bool is_buffer);

      result receive_in_allocated (int fd);

      result parse_size_in_tmpsize ();
      result receive_in_tmpsize (int fd);

      result parse_size ();
      result receive (int fd);
  };
}

#endif
