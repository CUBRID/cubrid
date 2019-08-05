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

#include "concurrent_queue.hpp"
#include "replication_stream_entry.hpp"
#include "string_buffer.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"

namespace cubthread
{
  class daemon;
}

namespace cubstream
{
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
  stream_entry_fetcher<T>::stream_entry_fetcher (multi_thread_stream &stream, const std::function<void (T *)> &on_fetch,
      bool defer_start)
    : m_stream (stream)
    , m_on_fetch (on_fetch)
    , m_fetch_task (NULL)
  {
    m_fetch_task = new stream_entry_fetch_task<T> (*this, defer_start);

    m_fetch_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
		     m_fetch_task, "prepare_stream_entry_daemon");
  }

  template<typename T>
  stream_entry_fetcher<T>::~stream_entry_fetcher ()
  {
    m_fetch_task->stop ();
    cubthread::get_manager ()->destroy_daemon (m_fetch_daemon);
  }

  template<typename T>
  void stream_entry_fetcher<T>::resume ()
  {
    m_fetch_task->resume ();
  }

  template<typename T>
  void stream_entry_fetcher<T>::suspend ()
  {
    m_fetch_task->suspend ();
  }

  template<typename T>
  void stream_entry_fetcher<T>::produce ()
  {
    T *se = NULL;
    int err = fetch_stream_entry (se);
    if (err == NO_ERROR)
      {
	m_on_fetch (se);
	push_entry (se);
      }
  }

  template<typename T>
  int stream_entry_fetcher<T>::fetch_stream_entry (T *&entry)
  {
    int err = NO_ERROR;

    T *se = new T (&m_stream);

    err = se->prepare ();
    if (err != NO_ERROR)
      {
	delete se;
	return err;
      }

    entry = se;
    return err;
  }

  template<typename T>
  void stream_entry_fetcher<T>::push_entry (T *entry)
  {
    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
      {
	string_buffer sb;
	entry->stringify (sb, T::short_dump);
	_er_log_debug (ARG_FILE_LINE, "stream_entry_fetcher push_entry:\n%s", sb.get_buffer ());
      }

    m_stream_entries.push_one (entry);
  }

  template<typename T>
  T *stream_entry_fetcher<T>::pop_entry (bool &should_stop)
  {
    T *entry = m_stream_entries.wait_for_one ();
    if (!m_stream_entries.notifications_enabled ())
      {
	should_stop = true;
	return entry;
      }

    assert (entry != NULL);
    return entry;
  }

  template<typename T>
  void stream_entry_fetcher<T>::release_waiters ()
  {
    m_stream_entries.release_waiters ();
  }

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

  template <typename T>
  stream_entry_fetch_task<T>::stream_entry_fetch_task (stream_entry_fetcher<T> &fetcher, bool defer_start)
    : m_fetcher (fetcher)
    , m_stop (false)
    , m_suspended (defer_start)
  {
  };

  template <typename T>
  void stream_entry_fetch_task<T>::execute (cubthread::entry &thread_ref)
  {
    if (m_suspended || m_stop)
      {
	std::unique_lock<std::mutex> ul (m_suspend_mtx);
	m_suspend_cv.wait (ul, [this] ()
	{
	  // leave condition variable wait on resume or on stop
	  return !m_suspended || m_stop;
	});
	if (m_stop)
	  {
	    return;
	  }
      }

    m_fetcher.produce ();
  };

  template <typename T>
  void stream_entry_fetch_task<T>::resume ()
  {
    std::lock_guard<std::mutex> lg (m_suspend_mtx);
    m_suspended = false;
    m_suspend_cv.notify_one ();
  }

  template <typename T>
  void stream_entry_fetch_task<T>::suspend ()
  {
    std::lock_guard<std::mutex> lg (m_suspend_mtx);
    m_suspended = true;
  }

  template <typename T>
  void stream_entry_fetch_task<T>::stop ()
  {
    std::lock_guard<std::mutex> lg (m_suspend_mtx);
    m_stop = true;
    m_suspend_cv.notify_one ();
  }
}

#endif // _STREAM_ENTRY_FETCHER_HPP_
