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
 * transmitter.hpp
 */

#ifndef _CONNECTION_TRANSMITTER_HPP_
#define _CONNECTION_TRANSMITTER_HPP_

#include "connection_statistics.hpp"
#include "buffer.hpp"
#include "packet_buffer.hpp"

#include <functional>
#include <deque>
#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace cubconn
{
  class transmitter
  {
    public:
      transmitter (statistics::metrics<statistics::context> *stats);
      transmitter ();
      ~transmitter ();

      result fill (int fd, int limit = 0);

      template <typename... Spans>
      void push_for_send (const cubbase::span<std::byte> &&first, const Spans &&... rest);
      void push (const cubbase::span<std::byte> &data);
      void push_for_deleter (std::function<void ()> &&deleter);
      void stamp ();

      bool prepare_append (std::size_t additional_count);
      bool empty ();
      void clear ();

    private:
      struct deleter_entry
      {
	std::size_t m_completion_iov_count;
	std::function<void ()> m_deleter;
      };

      cubbase::packet_buffer m_buf;
      std::deque<deleter_entry> m_deleter;
      std::size_t m_appended_iov_count = 0;

      statistics::metrics<statistics::context> *m_stats;

      void release_completed (std::size_t completed_iov_count);
  };

  template <typename... Spans>
  void transmitter::push_for_send (const cubbase::span<std::byte> &&first, const Spans &&... rest)
  {
    std::size_t appended_iov_count = 1;
    auto count_nonempty = [&appended_iov_count] (const cubbase::span<std::byte> &span)
    {
      if (!span.empty ())
	{
	  ++appended_iov_count;
	}
    };

    count_nonempty (first);
    if constexpr (sizeof... (rest) > 0)
      {
	(count_nonempty (rest), ...);
      }

    m_buf.push_for_send (std::forward<const cubbase::span<std::byte>> (first), std::forward<Spans> (rest)...);
    m_appended_iov_count += appended_iov_count;
  }

  inline void transmitter::push (const cubbase::span<std::byte> &data)
  {
    m_buf.push (data);
    ++m_appended_iov_count;
  }
}

#endif
