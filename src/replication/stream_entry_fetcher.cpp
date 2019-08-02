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
 * stream_entry_fetcher.cpp - Fetcher of stream entries from a stream
 */

#include "stream_entry_fetcher.hpp"
#include "string_buffer.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"

namespace cubstream
{
  template<typename T>
  class stream_entry_fetch_task : public cubthread::entry_task
  {
    public:
      stream_entry_fetch_task (stream_entry_fetcher<T> &fetcher)
	: m_fetcher (fetcher)
      {
      };

      void execute (cubthread::entry &thread_ref) override
      {
	m_fetcher.produce ();
      };

    private:
      stream_entry_fetcher<T> &m_fetcher;
  };

  template<typename T>
  stream_entry_fetcher<T>::stream_entry_fetcher (const std::function<void (T *)> &on_fetch,
      multi_thread_stream &stream)
    : m_on_fetch (on_fetch)
    , m_stream (stream)
  {
    m_fetch_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
		     new stream_entry_fetch_task<T> (*this),
		     "prepare_stream_entry_daemon");
  }

  template<typename T>
  stream_entry_fetcher<T>::~stream_entry_fetcher ()
  {
    cubthread::get_manager ()->destroy_daemon (m_fetch_daemon);
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

    // todo: add wait for fetch resume
    // wait_for_fetch_resume ();

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
	entry->stringify (sb, stream_entry::short_dump);
	_er_log_debug (ARG_FILE_LINE, "log_consumer push_entry:\n%s", sb.get_buffer ());
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
}
