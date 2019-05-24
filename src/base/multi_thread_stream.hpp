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

#include <condition_variable>
#include <mutex>
#include <functional>
#include <vector>

namespace cubstream
{
  class stream_file;
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

      /* The minimum amount of space to read from file;
       * when data is not found in BIP buffer, read from file is used;
       * however, the stream file flush daemon may have not flushed yet the desired range;
       * we need to wait for it to flush
       */
      static const size_t MIN_BYTES_TO_READ_FROM_FILE = 16 * 1024;

      static const int WAIT_FOR_FILE_SLEEP_MICROSECS = 500;

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

      /* read context holds a file_buffer (used in case we read the data from stream_file) or a latch read
       * (used when reading for stream's buffer) */
      struct stream_read_context
      {
	stream_read_context () : file_buffer (NULL), buffer_size (0), read_latch_page_idx (0) {}
	~stream_read_context ()
	{
	  assert (file_buffer == NULL);
	}

	char *file_buffer;
	size_t buffer_size;
	cubmem::buffer_latch_read_id read_latch_page_idx;
      };

      cubmem::bip_buffer<BIP_BUFFER_READ_PAGES_COUNT> m_bip_buffer;

      /* oldest position of strem still in buffer : updated according to buffer availability:
       * oldest stream position available from bip_buffer
       * after reserve, this value is expected to increase, so if any reader needs to get a position
       * older than this, there is no need to check the buffer or reserved queue
       */
      stream_position m_oldest_buffered_position;

      cubmem::collapsable_circular_queue<stream_reserve_context> m_reserved_positions;

      /* threshold size of unread stream content not read which triggers signaling "filled" event
       * such event may be throttling the reserve calls on stream (the stream content needs to be saved to disk)
       */
      size_t m_trigger_flush_to_disk_size;

      /* the range for difference (append_position - m_last_recyclable_pos) when stream reserve blocks:
       * if the append continues beyond this range, we may loose unflushed (or un-read by all readers) data;
       * normally, we can set this to buffer_capacity, but due to contiguous requirement of allocation, in practice,
       * this limit is lower;
       * if this is set to zero, we can afford to loose data (unread and unflushed).
       */
      size_t m_threshold_block_reserve;

      /* the range for difference (append_position - m_last_recyclable_pos) when stream reserve may resume */
      size_t m_threshold_resume_reserve;

      /* mutex accessing/updating recyclable position */
      std::mutex m_recyclable_pos_mutex;

      /* updating and waiting for recyclable position cv uses m_recyclable_pos_mutex */
      std::condition_variable m_recyclable_pos_cv;

      /* the minimum amount committed to stream which may be read (to avoid notifications in case of too small data) */
      size_t m_trigger_min_to_read_size;

      /* mutex for reserve/reading for attached bip_buffer */
      std::mutex m_buffer_mutex;

      /* serial read cv uses m_buffer_mutex */
      std::condition_variable m_serial_read_cv;
      bool m_is_stopped;

      stream_file *m_stream_file;

      /* if enabled, it will force flush stream contents after append (still using daemon thread) */
      bool m_flush_on_commit;

      /* stats counters */
      std::uint64_t m_stat_reserve_queue_spins;
      std::uint64_t m_stat_reserve_buffer_spins;
      std::uint64_t m_stat_read_not_enough_data_cnt;
      std::uint64_t m_stat_read_not_in_buffer_cnt;
      std::uint64_t m_stat_read_not_in_buffer_wait_for_flush_cnt;
      std::uint64_t m_stat_read_no_readable_pos_cnt;
      std::uint64_t m_stat_read_buffer_failed_cnt;

    protected:
      /* should be called when serialization of a stream entry ends */
      void commit_append (stream_reserve_context *reserve_context);

      char *reserve_with_buffer (const size_t amount, stream_reserve_context *&reserved_context);

      char *get_data_from_pos (const stream_position &req_start_pos, const size_t amount,
			       size_t &actual_read_bytes, stream_read_context &read_context);
      void unlatch_read_data (const cubmem::buffer_latch_read_id &read_latch_page_idx);

      void release_read_context (stream_read_context &read_context);

      int wait_for_data (const size_t amount, const STREAM_SKIP_MODE skip_mode);

    public:
      multi_thread_stream (const size_t buffer_capacity, const int max_appenders);
      virtual ~multi_thread_stream ();
      int init (const stream_position &start_position = 0);

      int write (const size_t byte_count, write_func_t &write_action, stream_position * p_stream_position);
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

      void set_threshold_block_reserve (const size_t threshold)
      {
	m_threshold_block_reserve = threshold;
      }

      void set_threshold_resume_reserve (const size_t threshold)
      {
	m_threshold_resume_reserve = threshold;
      }

      bool need_block_reserve (const size_t reserve_amount)
      {
	/* we need to protect:
	 * - append will run out of buffer space (would overwrite old unflushed/unrecycled data)
	 * - after reserve success, the reserve margin invalidates old data of buffer: we need to adjust
	 *   oldest buffered position ==> we need to prevent increasing oldest buffered position
	 *   beyond the recyclable position
	 */
	return (m_append_position > m_last_recyclable_pos + m_threshold_block_reserve
		|| (m_last_committed_pos > m_oldest_buffered_position + m_trigger_flush_to_disk_size
		    && (m_oldest_buffered_position + reserve_amount + m_bip_buffer.get_reserve_margin ()
			> m_last_recyclable_pos))) ? true : false;
      }

      /* fill factor : if < 1 : no need to flush or throttle the appenders ; if > 1 : need to flush and/or throttle */
      float stream_fill_factor (void)
      {
	return ((float) m_last_committed_pos - (float) m_last_recyclable_pos) / (float) m_trigger_flush_to_disk_size;
      };

      void set_stop (void)
      {
	m_is_stopped = true;
	m_serial_read_cv.notify_one ();
	m_recyclable_pos_cv.notify_one ();
      }

      void set_stream_file (stream_file *sf)
      {
	m_stream_file = sf;
      }

      stream_file *get_stream_file (void)
      {
	return m_stream_file;
      }

      void wake_up_flusher (float fill_factor, const stream_position start_flush_pos, const size_t flush_amount);
      void wait_for_flush_or_readers (const stream_position &last_commit_pos, const stream_position &last_append_pos);
      size_t wait_for_flush (const stream_position &req_pos, const size_t min_amount);
      void set_last_recyclable_pos (const stream_position &pos);
  };

} /* namespace cubstream */

#endif /* _MULTI_THREAD_STREAM_HPP_ */
