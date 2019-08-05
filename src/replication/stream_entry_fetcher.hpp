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
#include "thread_entry_task.hpp"

namespace cubthread
{
  class daemon;
}

namespace cubstream
{
  using cubreplication::stream_entry;

  template<typename T>
  class stream_entry_fetch_task;

  // todo: find a way to constrain T to be of type cubstream::entry<T>
  template<typename T>
  class stream_entry_fetcher
  {
    public:
      stream_entry_fetcher (cubstream::multi_thread_stream &stream, const std::function<void (T *)> &on_fetch,
			    bool defer_start = false);
      ~stream_entry_fetcher ();
      void resume ();
      void suspend ();
      void produce ();

      T *pop_entry (bool &should_stop);
      void release_waiters ();

    private:
      int fetch_stream_entry (T *&entry);
      void push_entry (T *entry);

      cubstream::multi_thread_stream &m_stream;
      std::function<void (T *)> m_on_fetch;

      cubsync::concurrent_queue<T *> m_stream_entries;
      stream_entry_fetch_task<T> *m_fetch_task;
      cubthread::daemon *m_fetch_daemon;
  };

  template<typename T>
  class stream_entry_fetch_task : public cubthread::entry_task
  {
    public:
      stream_entry_fetch_task (stream_entry_fetcher<T> &fetcher, bool defer_start = false);
      void execute (cubthread::entry &thread_ref) override;

      void resume ();
      void suspend ();
      void stop ();

    private:
      stream_entry_fetcher<T> &m_fetcher;
      bool m_stop;
      bool m_suspended;
      std::condition_variable m_suspend_cv;
      std::mutex m_suspend_mtx;
  };

  template class stream_entry_fetcher<stream_entry>;
  template class stream_entry_fetch_task<stream_entry>;

  using repl_stream_entry_fetcher = stream_entry_fetcher<stream_entry>;
}

#endif // _STREAM_ENTRY_FETCHER_HPP_
