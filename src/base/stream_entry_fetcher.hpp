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

#include "type_traits_utils.hpp"
#include "multi_thread_stream.hpp"
#include "stream_entry.hpp"
#include <cassert>
#include <functional>

namespace cubstream
{
  template<typename T>
  class entry_fetcher
  {
    private:
      static_assert (cubbase::is_instance_of<T, cubstream::entry>::value,
		     "T needs to be derived from or be a specialisation of cubstream::entry");
      using stream_entry_type = T;
    public:
      entry_fetcher (cubstream::multi_thread_stream &stream);
      int fetch_entry (stream_entry_type *&se);

    private:
      cubstream::multi_thread_stream &m_stream;
  };

  template<typename T>
  entry_fetcher<T>::entry_fetcher (cubstream::multi_thread_stream &stream)
    : m_stream (stream)
  {
  }

  template<typename T>
  int entry_fetcher<T>::fetch_entry (stream_entry_type *&se)
  {
    se = new stream_entry_type (&m_stream);
    int err = se->prepare ();
    return err;
  }
};

#endif // _STREAM_ENTRY_FETCHER_HPP_
