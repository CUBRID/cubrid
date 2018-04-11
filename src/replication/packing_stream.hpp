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
#include <functional>
#include "packable_object.hpp"
#include "cubstream.hpp"
#include "stream_common.hpp"
#include "storage_common.h"

class cubpacking::packable_object;
class cubpacking::object_builder;

namespace cubstream
{

class buffer_provider;
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

class entry : public cubpacking::pinner
{
private:
  int stream_entry_id;

  /* TODO : should move this to stream_packer ? */
  buffer_context *m_buffered_range;

  bool m_is_packable;

protected:
  std::vector <cubpacking::packable_object *> m_packable_entries;

  packing_stream *m_stream;

  stream_position m_data_start_position;

  virtual cubpacking::object_builder *get_builder () = 0;

public:
  entry (packing_stream *stream);

  int pack (void);

  int prepare (void);
  
  int unpack (void);

  void reset (void) { set_packable (false); m_packable_entries.clear (); };

  size_t get_entries_size (void);

  int add_packable_entry (cubpacking::packable_object *entry);

  void set_packable (const bool is_packable) { m_is_packable = is_packable;};


  /* TODO : unit testing only */
  std::vector <cubpacking::packable_object *>* get_packable_entries_ptr (void) { return &m_packable_entries; };

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
 * this adds a layer which allows handling a stream in chunks (stream_entry),
 * especially in context of packable objects
 */
class packing_stream : public stream
{
private:
  buffer_provider *m_buffer_provider;

  /* 
   * a buffered range is a chunk of stream mapped onto a buffer (memory) 
   * a buffer can have multiple mappings (but contiguous) from the a stream (but only the same stream)
   * when we flush (disk or network) a chunk of stream, we just sort the buffered_range object by position
   * and send/flush individually each buffer; pre-processing may used to concatenate adiacent ranges mapped
   * onto the same buffer;
   * different ranges can be filled at different speeds, concatenation of ranges should be done only on filled buffers
   */

  /* TODO : maybe these should be moved as sub-object for each stream_buffer mapped onto the stream */
  std::vector<buffer_context> m_buffered_ranges;

  /* first and last positions of all currently mapped buffers; the range may have holes
   * in it and also some ranges may be mapped by more than one buffer */
  stream_position m_first_buffered_position;
  stream_position m_last_buffered_position;

  /* size of all active buffers attached to this stream */
  size_t m_total_buffered_size;

  /* size of all active buffers attached to stream, after which we need to start deferring them to log_file 
   * for flushing;
   * normal mode should not need this : all buffers are send to MRC_Manager to be send to slave */
  size_t trigger_flush_to_disk_size;

protected:
  int create_buffer_context (stream_buffer *new_buffer, const STREAM_MODE stream_mode,
                             const stream_position &first_pos, const stream_position &last_pos,
                             const size_t &buffer_start_offset, buffer_context **granted_range);

  int add_buffer_context (buffer_context *src_context, const STREAM_MODE stream_mode,
                          buffer_context **granted_range);

  int add_buffer_context (stream_buffer *new_buffer, const STREAM_MODE stream_mode,
                          const stream_position &first_pos, const stream_position &last_pos,
                          const size_t &buffer_start_offset, buffer_context **granted_range);

  char * create_buffer_from_existing (buffer_provider *req_buffer_provider,
                                             const stream_position &start_pos,
                                             const size_t &amount, buffer_context **granted_range);

  int remove_buffer_mapping (const STREAM_MODE stream_mode, buffer_context &mapped_range);

  char * fetch_data_from_provider (buffer_provider *context_provider, const stream_position &pos, 
                                   char *ptr, const size_t amount);

  stream_position reserve_no_buffer (const size_t amount);

public:
  packing_stream (const buffer_provider *my_provider = NULL);
  ~packing_stream ();

  /* should be called when serialization of a stream entry ends */
  int update_contiguous_filled_pos (const stream_position &filled_pos);

  int write (const size_t byte_count, write_handler *handler);
  int read_partial (const stream_position first_pos, const size_t byte_count, size_t *actual_read_bytes,
                    partial_read_handler *handler);
  int read (const stream_position first_pos, const size_t byte_count, read_handler *handler);

  char * reserve_with_buffer (const size_t amount, const buffer_provider *context_provider,
                                     stream_position *reserved_pos, buffer_context **granted_range);

  char * acquire_new_write_buffer (buffer_provider *req_buffer_provider, const stream_position &start_pos,
                                          const size_t &amount, buffer_context **granted_range);

  int collect_buffers (std::vector <buffer_context> &buffered_ranges, COLLECT_FILTER collect_filter,
                       COLLECT_ACTION collect_action);
  int attach_buffers (std::vector <buffer_context> &buffered_ranges);

  /* TODO[arnia] : temporary for unit test */
  void detach_all_buffers (void) { m_buffered_ranges.clear (); };

  char * get_more_data_with_buffer (const size_t amount, const buffer_provider *context_provider,
                                           buffer_context **granted_range);
  char * get_data_from_pos (const stream_position &req_start_pos, const size_t amount,
                                   const buffer_provider *context_provider, buffer_context **granted_range);

};

} /* namespace cubstream */

#endif /* _PACKING_STREAM_HPP_ */
