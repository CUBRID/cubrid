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
#include "pinning.hpp"
#include "stream_io.hpp"
#include "storage_common.h"
#include <vector>
#include <functional>

namespace cubstream
{

  class stream_packer;
  class packing_stream;

  typedef enum
  {
    COLLECT_ALL_BUFFERS = 0,
    COLLECT_ONLY_FILLED_BUFFERS
  } COLLECT_FILTER;

  typedef enum
  {
    COLLECT_KEEP = 0,
    COLLECT_AND_DETACH
  } COLLECT_ACTION;

  class entry : public cubbase::pinner
  {
    private:
      int stream_entry_id;

      bool m_is_packable;

    protected:
      std::vector <cubpacking::packable_object *> m_packable_entries;

      packing_stream *m_stream;

      stream_position m_data_start_position;


    public:
      using packable_factory = cubbase::factory<int, cubpacking::packable_object>;

      virtual packable_factory *get_builder () = 0;

      entry (packing_stream *stream);

      int pack (void);

      int prepare (void);

      int unpack (void);

      void reset (void)
      {
	set_packable (false);
	m_packable_entries.clear ();
      };

      size_t get_entries_size (void);

      int add_packable_entry (cubpacking::packable_object *entry);

      void set_packable (const bool is_packable)
      {
	m_is_packable = is_packable;
      };


      /* TODO : unit testing only */
      std::vector <cubpacking::packable_object *> *get_packable_entries_ptr (void)
      {
	return &m_packable_entries;
      };

      /* stream entry header methods : header is implemention dependent, is not known here ! */
      virtual stream_packer *get_packer () = 0;
      virtual size_t get_header_size (void) = 0;
      virtual void set_header_data_size (const size_t &data_size) = 0;
      virtual size_t get_data_packed_size (void) = 0;
      virtual int pack_stream_entry_header (void) = 0;
      virtual int unpack_stream_entry_header (void) = 0;
      virtual int get_packable_entry_count_from_header (void) = 0;
      virtual bool is_equal (const entry *other) = 0;
  };

  /*
   * this adds a layer which allows handling a stream in chunks (cubstream::entry),
   * especially in context of packable objects
   * for writing/reading packable_objects into/from (packing_)stream, the friend class stream_packer is used as
   * both a packer and a context over a range in stream
   */
  struct stream_reserve_context
    {
      stream_position start_pos;
      char *ptr;
      size_t reserved_amount;
      size_t written_bytes;
    };

  class packing_stream : public stream
  {
      friend class stream_packer;

    private:
      /* a BIP-Buffer with 64 pages */
      mem::bip_buffer<64> m_bip_buffer;

      /* oldest readable position : updated according to buffer availability:
       * oldest stream position available from bip_buffer
       * after reserve, this value is expected to increase, so if any reader needs to get a position
       * older than this, there is no need to check the buffer or reserved queue */
      stream_position m_oldest_readable_position;

      mem::collapsable_circular_queue<stream_reserve_context> m_reserved_positions;

      /* threshold size of unread stream content not read which triggers signalling "filled" event
       * such event may be throttling the reserve calls on stream */
      size_t m_trigger_flush_to_disk_size;

      std::mutex m_buffer_mutex;

      stream_io *m_io;

    protected:
      int fetch_data_from_provider (const stream_position &pos, const size_t amount);

      stream_position reserve_no_buffer (const size_t amount);

      /* should be called when serialization of a stream entry ends */
      int commit_append (stream_reserve_context *reserve_context);

      char *reserve_with_buffer (const size_t amount, stream_reserve_context* &reserved_context);

      char *get_more_data_with_buffer (const size_t amount, size_t &actual_read_bytes, stream_position &trail_pos);
      char *get_data_from_pos (const stream_position &req_start_pos, const size_t amount, 
                               size_t &actual_read_bytes);
      int unlatch_read_data (const char *ptr, const size_t amount);
    public:
      packing_stream (const size_t buffer_capacity, const int max_appenders);
      ~packing_stream ();

      void init_storage (const size_t buffer_capacity, const int max_appenders);

      int write (const size_t byte_count, write_handler *handler);
      int read_partial (const stream_position first_pos, const size_t byte_count, size_t *actual_read_bytes,
			partial_read_handler *handler);
      int read (const stream_position first_pos, const size_t byte_count, read_handler *handler);

      void set_buffer_reserve_margin (const size_t margin)
        {
          m_bip_buffer.set_reserve_margin (margin);
        }

  };

} /* namespace cubstream */

#endif /* _PACKING_STREAM_HPP_ */
