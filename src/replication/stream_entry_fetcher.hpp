/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * stream_entry_fetcher.hpp - Fetcher of stream entries from a stream
 */

#ifndef _STREAM_ENTRY_FETCHER_HPP_
#define _STREAM_ENTRY_FETCHER_HPP_

#include "replication_stream_entry.hpp"
#include "concurrent_queue.hpp"

namespace cubthread
{
  class daemon;
}

namespace cubstream
{
  using cubreplication::stream_entry;

  // todo: find a way to constrain T to be of type cubstream::entry<T>
  template<typename T>
  class stream_entry_fetcher
  {
    public:
      stream_entry_fetcher (const std::function<void (T *)> &on_fetch, cubstream::multi_thread_stream &stream);
      ~stream_entry_fetcher ();
      void produce ();

      T *pop_entry (bool &should_stop);
      void release_waiters ();

    private:
      int fetch_stream_entry (T *&entry);
      void push_entry (T *entry);
      std::function<void (T *)> m_on_fetch;

      cubsync::concurrent_queue<T *> m_stream_entries;
      cubthread::daemon *m_fetch_daemon;
      cubstream::multi_thread_stream &m_stream;
  };

  template class stream_entry_fetcher<stream_entry>;
  using repl_stream_entry_fetcher = stream_entry_fetcher<stream_entry>;
}

#endif // _STREAM_ENTRY_FETCHER_HPP_
