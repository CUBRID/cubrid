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
 * packing_stream.hpp
 */

#ident "$Id$"

#ifndef _PACKING_STREAM_HPP_
#define _PACKING_STREAM_HPP_

#include "bip_buffer.hpp"
#include "collapsable_circular_queue.hpp"
#include "cubstream.hpp"
#include "object_factory.hpp"
#include "packable_object.hpp"
#include "packer.hpp"
#include "stream_io.hpp"
#include "storage_common.h"
#include <vector>
#include <functional>

namespace cubstream
{
  /* stream with capability to pack/unpack objects concurrently
   * the read, read_partial, read_serial, write require a function argument to perform the packing/unpacking;
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
   */
  class packing_stream : public stream
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

      mem::bip_buffer<BIP_BUFFER_READ_PAGES_COUNT> m_bip_buffer;

      /* oldest readable position : updated according to buffer availability:
       * oldest stream position available from bip_buffer
       * after reserve, this value is expected to increase, so if any reader needs to get a position
       * older than this, there is no need to check the buffer or reserved queue */
      stream_position m_oldest_readable_position;

      mem::collapsable_circular_queue<stream_reserve_context> m_reserved_positions;

      /* threshold size of unread stream content not read which triggers signalling "filled" event
       * such event may be throttling the reserve calls on stream (the stream content needs to saved to disk) */
      size_t m_trigger_flush_to_disk_size;

      /* the minimum amount commited to stream which may be read (to avoid notifications in case of too small data) */
      size_t m_trigger_min_to_read_size;

      std::mutex m_buffer_mutex;

      stream_io *m_io;

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
			       size_t &actual_read_bytes, mem::buffer_latch_read_id &read_latch_page_idx);
      int unlatch_read_data (const mem::buffer_latch_read_id &read_latch_page_idx);

      int wait_for_data (const size_t amount, const STREAM_SKIP_MODE skip_mode);

    public:
      packing_stream (const size_t buffer_capacity, const int max_appenders);
      ~packing_stream ();

      int write (const size_t byte_count, write_func_t &write_action);
      int read_partial (const stream_position first_pos, const size_t byte_count, size_t &actual_read_bytes,
			read_partial_func_t &read_partial_action);
      int read (const stream_position first_pos, const size_t byte_count, read_func_t &read_action);
      int read_serial (const size_t byte_count, read_prepare_func_t &read_action);

      void set_buffer_reserve_margin (const size_t margin)
      {
	m_bip_buffer.set_reserve_margin (margin);
      };

      /* fill factor : if < 1 : no need to flush or throtle the appenders ; if > 1 : need to flush and/or throttle */
      float stream_fill_factor (void)
      {
	return ((float) m_append_position - (float) m_last_dropable_pos) / (float) m_trigger_flush_to_disk_size;
      };

    };

  class entry
  {
    private:
      bool m_is_packable;

    protected:
      std::vector <cubpacking::packable_object *> m_packable_entries;

      packing_stream *m_stream;

      stream_position m_data_start_position;

      stream::write_func_t m_packing_func;
      stream::read_prepare_func_t m_prepare_func;
      stream::read_func_t m_unpack_func;

      int packing_func (const stream_position &pos, char *ptr, const size_t amount);
      int prepare_func (const stream_position &data_start_pos, char *ptr, const size_t header_size,
			size_t &payload_size);
      int unpack_func (char *ptr, const size_t data_size);

    public:
      using packable_factory = cubbase::factory<int, cubpacking::packable_object>;

      virtual packable_factory *get_builder () = 0;

      entry (packing_stream *stream);

      virtual ~entry()
      {
	reset ();
      };

      int pack (void);

      int prepare (void);

      int unpack (void);

      void reset (void)
      {
	set_packable (false);
	destroy_objects ();
      };

      size_t get_entries_size (void);

      int add_packable_entry (cubpacking::packable_object *entry);

      void set_packable (const bool is_packable)
      {
	m_is_packable = is_packable;
      };

      /* stream entry header methods : header is implemention dependent, is not known here ! */
      virtual cubpacking::packer *get_packer () = 0;
      virtual size_t get_header_size (void) = 0;
      virtual void set_header_data_size (const size_t &data_size) = 0;
      virtual size_t get_data_packed_size (void) = 0;
      virtual int pack_stream_entry_header (void) = 0;
      virtual int unpack_stream_entry_header (void) = 0;
      virtual int get_packable_entry_count_from_header (void) = 0;
      virtual bool is_equal (const entry *other) = 0;
      virtual void destroy_objects ();
  };

} /* namespace cubstream */

#endif /* _PACKING_STREAM_HPP_ */
