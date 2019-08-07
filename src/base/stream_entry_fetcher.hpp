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

#include "multi_thread_stream.hpp"
#include <cassert>
#include <functional>

namespace cubstream
{
  // todo: find a way to constrain T to be of type cubstream::entry<T>
  template<typename T>
  class stream_entry_fetcher
  {
    public:
      stream_entry_fetcher (cubstream::multi_thread_stream &stream);
      void set_on_fetch_func (const std::function<void (T *, bool &)> &on_fetch);
      T *pop_entry (bool &should_stop, bool &skip);

    private:
      int fetch_stream_entry (T *&entry);

      cubstream::multi_thread_stream &m_stream;
      std::function<void (T *, bool &)> m_on_fetch;
  };

  template<typename T>
  stream_entry_fetcher<T>::stream_entry_fetcher (cubstream::multi_thread_stream &stream)
    : m_stream (stream)
    , m_on_fetch ([] (T *, bool &)
  {
    assert (false);
  })
  {
  }

  template<typename T>
  void stream_entry_fetcher<T>::set_on_fetch_func (const std::function<void (T *, bool &)> &on_fetch)
  {
    m_on_fetch = on_fetch;
  }

  template<typename T>
  T *stream_entry_fetcher<T>::pop_entry (bool &should_stop, bool &skip)
  {
    T *se = nullptr;
    int err = fetch_stream_entry (se);
    if (err == NO_ERROR)
      {
	m_on_fetch (se, skip);
      }
    else
      {
	should_stop = true;
      }
    return se;
  }

  template<typename T>
  int stream_entry_fetcher<T>::fetch_stream_entry (T *&entry)
  {
    T *se = new T (&m_stream);

    int err = se->prepare ();
    if (err != NO_ERROR)
      {
	delete se;
	return err;
      }

    entry = se;
    return err;
  }
};

#endif // _STREAM_ENTRY_FETCHER_HPP_
