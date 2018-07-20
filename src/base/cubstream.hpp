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

#ident "$Id$"

#ifndef _CUBSTREAM_HPP_
#define _CUBSTREAM_HPP_

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <string>
#include <functional>

namespace cubstream
{

  typedef std::uint64_t stream_position;

  /*
   * stream is a contiguous stream (flow) of bytes
   * at one time, a part of it has a storage support (buffer) which can be read or written
   * a stream can be read/written when packing/unpacking objects or by higher level objects (files, communication channels)
   * if an operation would exceed the storage range, the stream needs to fetch additional data or
   * append new storage (for writting)
   *
   */
  class stream
  {
    public:
      typedef std::function<int (char *, const size_t)> read_func_t;
      typedef std::function<int (char *, const size_t, size_t &)> read_partial_func_t;
      typedef std::function<int (const stream_position &, char *, const size_t, size_t &)> read_prepare_func_t;
      typedef std::function<int (const stream_position &, char *, const size_t)> write_func_t;
      typedef std::function<int (const stream_position &, const size_t)> notify_func_t;
      typedef std::function<int (const stream_position &, char *, const size_t, size_t &)> fetch_func_t;

    protected:
      /* callback functions: */
      /* called before old data content needs to be dropped : the drop position is an aggregate minimum of all
       * readers; when the difference between append position and drop position exceeds threshold, this action
       * needs to be taken (usually saving to disk) */
      notify_func_t m_filled_stream_handler;

      /* called when reader does not have enough data to read; usually it blocks in waiting for producing, but
       * may be used as actively producing data */
      fetch_func_t m_fetch_data_handler;

      /* called when commit position advances enough (delta above threshold); the usual action is notify readers
       */
      notify_func_t m_ready_pos_handler;

      /* current stream position not allocated yet */
      stream_position m_append_position;

      /* last stream position read
       * in most scenarios, each reader provides its own read position,
       * this is a shared "read position" usable only in single read scenarios
       * any reader can access the stream using random position concurrently */
      stream_position m_read_position;

      /* last position committed (filled) by appenders; can be read by readers */
      stream_position m_last_committed_pos;

      /* last position notified as committed; used to send notifications */
      stream_position m_last_notified_committed_pos;

      /* last position which may be dropped (the underlying memory associated may be recycled)
       * is checked by stream notify (e.g. flush to disk), and set by external clients;
       */
      stream_position m_last_dropable_pos;

      std::string m_stream_name;

    public:
      stream ();

      int init (const stream_position &start_position = 0);

      virtual int write (const size_t byte_count, write_func_t &write_action) = 0;

      virtual int read_partial (const stream_position first_pos, const size_t byte_count, size_t &actual_read_bytes,
				read_partial_func_t &read_partial_action) = 0;

      virtual int read (const stream_position first_pos, const size_t byte_count, read_func_t &read_action) = 0;

      const stream_position &get_curr_read_position (void)
      {
	return m_read_position;
      }

      const stream_position &get_last_committed_pos (void)
      {
	return m_last_committed_pos;
      }

      const stream_position &get_last_dropable_pos (void)
      {
	return m_last_dropable_pos;
      }

      void set_last_dropable_pos (const stream_position &last_dropable_pos)
      {
	m_last_dropable_pos = last_dropable_pos;
      }

      void set_filled_stream_handler (notify_func_t handler)
      {
	m_filled_stream_handler = handler;
      }

      void set_fetch_data_handler (fetch_func_t handler)
      {
	m_fetch_data_handler = handler;
      }

      void set_ready_pos_handler (notify_func_t handler)
      {
	m_ready_pos_handler = handler;
      }

      const std::string &name (void)
      {
	return m_stream_name;
      }
  };

} /* namespace cubstream */

#endif /* _CUBSTREAM_HPP_ */
