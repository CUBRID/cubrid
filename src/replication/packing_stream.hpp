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

#include <vector>
#include "common_utils.hpp"
#include "storage_common.h"

class stream_provider;
class packable_object;
class object_builder;
class stream_packer;

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


class stream_entry : public pinner
{
protected:
  std::vector <packable_object *> m_packable_entries;

  int stream_entry_id;

  /* TODO : should move this to stream_packer ? */
  buffer_context *m_buffered_range;

  bool m_is_packable;

protected:

  stream_position m_data_start_position;

  virtual object_builder *get_builder () = 0;

public:
  stream_entry() { m_buffered_range = NULL;  m_data_start_position = 0; set_packable (false); };

  int pack (stream_packer *serializator);

  int receive (stream_packer *serializator);
  
  int unpack (stream_packer *serializator);

  void reset (void) { set_packable (false); m_packable_entries.clear (); };

  size_t get_entries_size (stream_packer *serializator);

  int add_packable_entry (packable_object *entry);

  void set_packable (const bool is_packable) { m_is_packable = is_packable;};


  /* TODO : unit testing only */
  std::vector <packable_object *>* get_packable_entries_ptr (void) { return &m_packable_entries; };

  /* stream entry header methods : header is implemention dependent, is not known here ! */
  virtual size_t get_header_size (stream_packer *serializator) = 0;
  virtual void set_header_data_size (const size_t &data_size) = 0;
  virtual size_t get_data_packed_size (void) = 0;
  virtual int pack_stream_entry_header (stream_packer *serializator) = 0;
  virtual int unpack_stream_entry_header (stream_packer *serializator) = 0;
  virtual int get_packable_entry_count_from_header (void) = 0;
  virtual bool is_equal (const stream_entry *other) = 0;
};

/*
 * stream is a contiguous stream (flow) of bytes
 * at one time, a part of it has a storage support (buffer) which can be read or written 
 * a stream can be read/written when packing/unpacking objects or by higher level objects (files, communication channels) 
 * if an operation would exceed the storage range, the stream needs to fetch aditional data or 
 * append new storage (for writting)
 *
 * TODO : create a stream only for read of only for write (never both read and write !!!)
 */
class packing_stream
{
private:
  stream_provider *m_stream_provider;

  /* a buffered range is a chunk of stream mapped onto a buffer (memory) 
   * a buffer can have multiple mappings (but contiguous) from the a stream (but only the same stream)
   * when we flush (disk or network) a chunk of stream, we just sort the buffered_range object by position
   * and send/flush individually each buffer; pre-processing may used to concatenate adiacent ranges mapped
   * onto the same buffer;
   * different ranges can be filled at different speeds, concatenation of ranges should be done only on filled buffers
   */

  /* TODO : maybe these should be moved as sub-object for each packing_stream_buffer mapped onto the stream */
  std::vector<buffer_context> m_buffered_ranges;

  /* current stream position not allocated yet */
  stream_position m_append_position;

  /* last stream position read */
  stream_position m_read_position;

  /* last position reported to be ready (filled) - can be discarded from stream */
  stream_position m_last_reported_ready_pos;

  stream_position m_max_buffered_position;

  /* size of all active buffers attached to this stream */
  size_t m_total_buffered_size;

  /* size of all active buffers attached to stream, after which we need to start deferring them to log_file 
   * for flushing;
   * normal mode should not need this : all buffers are send to MRC_Manager to be send to slave */
  size_t trigger_flush_to_disk_size;

protected:
  int create_buffer_context (packing_stream_buffer *new_buffer, const STREAM_MODE stream_mode,
                             const stream_position &first_pos, const stream_position &last_allocated_pos,
                             const size_t &buffer_start_offset, buffer_context **granted_range);

  int add_buffer_context (packing_stream_buffer *new_buffer, const STREAM_MODE stream_mode,
                          const stream_position &first_pos, const stream_position &last_pos,
                          const size_t &buffer_start_offset, buffer_context **granted_range);

  int remove_buffer_mapping (const STREAM_MODE stream_mode, buffer_context &mapped_range);

  BUFFER_UNIT * fetch_data_from_provider (stream_provider *context_provider, BUFFER_UNIT *ptr, const size_t &amount);

public:
  packing_stream (const stream_provider *my_provider);

  int init (const stream_position &start_position = 0);

  /* should be called when serialization of a stream entry ends */
  int update_contiguous_filled_pos (const stream_position &filled_pos);

  stream_position reserve_no_buffer (const size_t amount);

  BUFFER_UNIT * reserve_with_buffer (const size_t amount, const stream_provider *context_provider,
                                     buffer_context **granted_range);

  BUFFER_UNIT * acquire_new_write_buffer (stream_provider *req_stream_provider, const stream_position &start_pos,
                                          const size_t &amount, buffer_context **granted_range);

  int collect_buffers (std::vector <buffer_context> &buffered_ranges, COLLECT_FILTER collect_filter,
                       COLLECT_ACTION collect_action);
  int attach_buffers (std::vector <buffer_context> &buffered_ranges);

  /* TODO[arnia] : temporary for unit test */
  void detach_all_buffers (void) { m_buffered_ranges.clear (); };

  BUFFER_UNIT * get_more_data_with_buffer (const size_t amount, const stream_provider *context_provider,
                                           buffer_context **granted_range);
  BUFFER_UNIT * get_data_from_pos (const stream_position &req_start_pos, const size_t amount,
                                   const stream_provider *context_provider, buffer_context **granted_range);

  stream_position &get_curr_read_position (void) { return m_read_position; };
};

#endif /* _PACKING_STREAM_HPP_ */
