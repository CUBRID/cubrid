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

namespace cubstream
{
  class stream_entry_fetch_task : public cubthread::entry_task
  {
    public:
      stream_entry_fetch_task (stream_entry_fetcher &fetcher)
	: m_fetcher (fetcher)
      {
      };

      void execute (cubthread::entry &thread_ref) override
      {
	stream_entry *se = NULL;

	int err = m_fetcher.fetch_stream_entry (se);
	if (err == NO_ERROR)
	  {
	    m_fetcher.m_on_fetch (se);

	    m_fetcher.push_entry (se);
	  }
      };

    private:
      stream_entry_fetcher &m_fetcher;
  };

  stream_entry_fetcher::stream_entry_fetcher (const std::function<void (stream_entry *)> &on_fetch,
      multi_thread_stream &stream)
    : m_on_fetch (on_fetch)
    , m_stream (stream)
  {
    m_fetch_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
		     new stream_entry_fetch_task (*this),
		     "prepare_stream_entry_daemon");
  }

  stream_entry_fetcher::~stream_entry_fetcher ()
  {
    cubthread::get_manager ()->destroy_daemon (m_fetch_daemon);
  }

  int stream_entry_fetcher::fetch_stream_entry (stream_entry *&entry)
  {
    int err = NO_ERROR;

    // todo: add wait for fetch resume
    // wait_for_fetch_resume ();

    stream_entry *se = new stream_entry (&m_stream);

    err = se->prepare ();
    if (err != NO_ERROR)
      {
	delete se;
	return err;
      }

    entry = se;

    return err;
  }

  void stream_entry_fetcher::push_entry (stream_entry *entry)
  {
    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
      {
	string_buffer sb;
	entry->stringify (sb, stream_entry::short_dump);
	_er_log_debug (ARG_FILE_LINE, "log_consumer push_entry:\n%s", sb.get_buffer ());
      }

    m_stream_entries.push_one (entry);
  }

  stream_entry *stream_entry_fetcher::pop_entry (bool &should_stop)
  {
    stream_entry *entry = m_stream_entries.wait_for_one ();
    if (!m_stream_entries.notifications_enabled ())
      {
	should_stop = true;
	return entry;
      }

    assert (entry != NULL);
    return entry;
  }

  void stream_entry_fetcher::release_waiters ()
  {
    m_stream_entries.release_waiters ();
  }
}