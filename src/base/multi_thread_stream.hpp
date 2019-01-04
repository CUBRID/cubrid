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
 * multi_thread_stream.hpp
 */

#ident "$Id$"

#ifndef _MULTI_THREAD_STREAM_HPP_
#define _MULTI_THREAD_STREAM_HPP_

#include "bip_buffer.hpp"
#include "collapsable_circular_queue.hpp"
#include "cubstream.hpp"
#include "stream_io.hpp"

#include <condition_variable>
#include <mutex>
#include <functional>
#include <vector>

namespace cubstream
{
  /* stream with capability to write/read concurrently
   * the read, read_partial, read_serial, write require a function argument to perform custom write/read operation;
   * Interface:
   *
   * write (amount, write_function):
   *   - the amount needs to be pre-computed
   *   - internally, the write_function is wrapped by a reserve/commit pair
   *
   * read (pos, amount, read_function):
   *    - the position and amount of data are required
   *    - internally, the read_function is wrapped by start_read, end_read (for latching the page in buffer)
   *    - if required range is still in bip_buffer storage and it spans across the its boundary, the read
   *      creates internally a buffer and provides a copy of range to read_function
   *
   * read_serial (amount, read_function):
   *    - the stream stores an internal position for serial reading
   *    - this method must be used by only one thread
   *
   * read_partial (pos, amount, read_amount, read_function):
   *    - similar with read, but it provides only 'read_amount' in case of range spans across buffer boundary
   *
   * Internals:
   *  The reserved position are stored in a circular queue of stream_reserve_context elements.
   *  The memory storage is implemented using bip_buffer.
   *  reserve_with_buffer/commit_append : internal methods used by write;
   *  get_data_from_pos/unlatch_read_data : internal methods used by read;
   *  wait_for_data : used by read when requested range is not yet produced
   */
  class multi_thread_stream : public stream
  {
    public:
      static const int BIP_BUFFER_READ_PAGES_COUNT = 64;

      enum STREAM_SKIP_MODE
      {
	STREAM_DONT_SKIP = 0,
	STREAM_SKIP
      };

    private:
      /* structure capturing context of a stream reserve-commit scope */
      struct stream_reserve_context
      {
	stream_position start_pos;
	char *ptr;
	size_t reserved_amount;
	size_t written_bytes;
      };

      cubmem::bip_buffer<BIP_BUFFER_READ_PAGES_COUNT> m_bip_buffer;

      /* oldest readable position : updated according to buffer availability:
       * oldest stream position available from bip_buffer
       * after reserve, this value is expected to increase, so if any reader needs to get a position
       * older than this, there is no need to check the buffer or reserved queue
       */
      stream_position m_oldest_readable_position;

      cubmem::collapsable_circular_queue<stream_reserve_context> m_reserved_positions;

      /* threshold size of unread stream content not read which triggers signaling "filled" event
       * such event may be throttling the reserve calls on stream (the stream content needs to be saved to disk)
       */
      size_t m_trigger_flush_to_disk_size;

      /* the minimum amount committed to stream which may be read (to avoid notifications in case of too small data) */
      size_t m_trigger_min_to_read_size;

      std::mutex m_buffer_mutex;

      stream_io *m_io;

      /* serial read cv uses m_buffer_mutex */
      std::condition_variable m_serial_read_cv;
      bool m_is_stopped;

      /* stats counters */
      std::uint64_t m_stat_reserve_queue_spins;
      std::uint64_t m_stat_reserve_buffer_spins;
      std::uint64_t m_stat_read_not_enough_data_cnt;
      std::uint64_t m_stat_read_not_in_buffer_cnt;
      std::uint64_t m_stat_read_no_readable_pos_cnt;
      std::uint64_t m_stat_read_buffer_failed_cnt;

    protected:
      /* should be called when serialization of a stream entry ends */
      int commit_append (stream_reserve_context *reserve_context);

      char *reserve_with_buffer (const size_t amount, stream_reserve_context *&reserved_context);

      char *get_data_from_pos (const stream_position &req_start_pos, const size_t amount,
			       size_t &actual_read_bytes, cubmem::buffer_latch_read_id &read_latch_page_idx);
      int unlatch_read_data (const cubmem::buffer_latch_read_id &read_latch_page_idx);

      int wait_for_data (const size_t amount, const STREAM_SKIP_MODE skip_mode);

    public:
      multi_thread_stream (const size_t buffer_capacity, const int max_appenders);
      virtual ~multi_thread_stream ();

      int write (const size_t byte_count, write_func_t &write_action);
      int read_partial (const stream_position first_pos, const size_t byte_count, size_t &actual_read_bytes,
			read_partial_func_t &read_partial_action);
      int read (const stream_position first_pos, const size_t byte_count, read_func_t &read_action);
      int read_serial (const size_t byte_count, read_prepare_func_t &read_action);

      void set_buffer_reserve_margin (const size_t margin)
      {
	m_bip_buffer.set_reserve_margin (margin);
      };

      void set_trigger_min_to_read_size (const size_t min_read_size)
      {
	m_trigger_min_to_read_size = min_read_size;
      }

      /* fill factor : if < 1 : no need to flush or throttle the appenders ; if > 1 : need to flush and/or throttle */
      float stream_fill_factor (void)
      {
	return ((float) m_append_position - (float) m_last_dropable_pos) / (float) m_trigger_flush_to_disk_size;
      };

      void set_stop (void)
      {
	m_is_stopped = true;
	m_serial_read_cv.notify_one ();
      }
  };

} /* namespace cubstream */

#endif /* _MULTI_THREAD_STREAM_HPP_ */
