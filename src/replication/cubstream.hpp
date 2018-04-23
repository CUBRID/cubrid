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
 * cubstream.hpp
 */

#ifndef _CUBSTREAM_HPP_
#define _CUBSTREAM_HPP_

#ident "$Id$"

#include <vector>
#include <functional>
#include <atomic>

/* TODO[arnia] these will be added from razvan's branch repl_base */
typedef unsigned long long stream_position;
typedef char BUFFER_UNIT;

namespace cubstream
{

  class read_handler
  {
    public:
      virtual int read_action (const stream_position pos, char *ptr, const size_t byte_count) = 0;
  };

  class partial_read_handler
  {
    public:
      virtual int read_action (const stream_position pos, char *ptr, const size_t byte_count,
			       size_t *processed_bytes) = 0;
  };

  class write_handler
  {
    public:
      virtual int write_action (const stream_position pos, char *ptr, const size_t byte_count) = 0;
  };

  class fetch_handler
  {
    public:
      virtual int fetch_action (const stream_position pos, char *ptr, const size_t byte_count,
				size_t *processed_bytes) = 0;
  };

  class notify_handler
  {
    public:
      virtual int notify (const stream_position pos, const size_t byte_count) = 0;
  };

  /*
   * stream is a contiguous stream (flow) of bytes
   * at one time, a part of it has a storage support (buffer) which can be read or written
   * a stream can be read/written when packing/unpacking objects or by higher level objects (files, communication channels)
   * if an operation would exceed the storage range, the stream needs to fetch aditional data or
   * append new storage (for writting)
   */
  class stream
  {
    protected:
      notify_handler *m_filled_stream_handler;
      fetch_handler *m_fetch_data_handler;
      notify_handler *m_ready_pos_handler;

      /* current stream position not allocated yet */
      stream_position m_append_position;

      /* last position reported to be ready (filled) by appenders; can be read by readers */
      std::atomic<stream_position> m_last_reported_ready_pos;

      /* last stream position read
       * in most scenarios, each reader provides its own read position,
       * this is a shared "read position" usable only in single read scenarios */
      stream_position m_read_position;

    public:
      stream ();
      int init (const stream_position &start_position = 0);

      virtual int write (const size_t byte_count, write_handler *handler) = 0;
      virtual int read_partial (const stream_position first_pos, const size_t byte_count, size_t *actual_read_bytes,
				partial_read_handler *handler) = 0;
      virtual int read (const stream_position first_pos, const size_t byte_count, read_handler *handler) = 0;

      stream_position &get_curr_read_position (void)
      {
	return m_read_position;
      };
      std::atomic<stream_position> &get_last_reported_ready_pos (void)
      {
	return m_last_reported_ready_pos;
      };

      void set_filled_stream_handler (notify_handler *handler)
      {
	m_filled_stream_handler = handler;
      };
      void set_fetch_data_handler (fetch_handler *handler)
      {
	m_fetch_data_handler = handler;
      };
      void set_ready_pos_handler (notify_handler *handler)
      {
	m_ready_pos_handler = handler;
      };
  };

} /* namespace cubstream */

#endif /* _CUBSTREAM_HPP_ */
