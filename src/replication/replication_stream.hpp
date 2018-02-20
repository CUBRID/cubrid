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
 * replication_stream.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_STREAM_HPP_
#define _REPLICATION_STREAM_HPP_

#include <vector>
#include "common_utils.hpp"
#include "storage_common.h"

class replication_serialization;
class stream_provider;
class replication_entry;

struct stream_entry_header
{
  stream_position prev_record;
  MVCCID mvccid;
  unsigned short count_replication_entries;
  size_t data_size;

  stream_entry_header () : prev_record (0), mvccid (-1), count_replication_entries (0) {};
};

/* 
 * maps a replication entry to a buffer (from a file or network buffer)
 * it shoud be used for both reading (file, network) and writing to file (file, network)
 * TODO: allow resumable writing : a producer can write a part of buffer, then resume later
 *
 * reading can be performed either by actual reading (from file) or by mapping/copying other existing buffers
 */
class stream_entry : public pinner
{
private:
  const size_t STREAM_ENTRY_HEADER_SIZE = sizeof (stream_entry_header);

  std::vector <replication_entry *> m_repl_entries;

  stream_entry_header m_header;

  int stream_entry_id;

  buffered_range *m_buffered_range;

public:
  stream_entry() { m_buffered_range = NULL; };

  int pack (replication_serialization *serializator);

  int receive (replication_serialization *serializator);
  
  int unpack (replication_serialization *serializator);

  size_t get_entries_size (replication_serialization *serializator);

  int add_repl_entry (replication_entry *repl_entry);

  size_t get_header_size (void) { return STREAM_ENTRY_HEADER_SIZE; } ;
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
class replication_stream
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

  /* TODO : maybe these should be moved as sub-object for each serial_buffer mapped onto the stream */
  std::vector<buffered_range> m_buffered_ranges;

  /* current stream position not allocated yet to a replication generator */
  stream_position append_position;
  /* contiguosly filled position (all mapped buffers up to this position are filled) */
  stream_position contiguous_filled_position;

  /* last position reported to be ready (filled) to MRC_M */
  stream_position last_reported_ready_pos;

  stream_position max_buffered_position;

  /* size of all active buffers attached to this stream */
  size_t total_buffered_size;

  /* size of all active buffers attached to stream, after which we need to start deferring them to log_file 
   * for flushing;
   * normal mode should not need this : all buffers are send to MRC_Manager to be send to slave */
  size_t trigger_flush_to_disk_size;
  
public:
  replication_stream (const stream_provider *my_provider);

  int init (const stream_position &start_position = 0);

  int add_buffer_mapping (serial_buffer *new_buffer, const STREAM_MODE stream_mode, const stream_position &first_pos,
                          const stream_position &last_pos, buffered_range **granted_range);

  int remove_buffer_mapping (const STREAM_MODE stream_mode, buffered_range &mapped_range);

  /* should be called when serialization of a stream entry ends */
  int update_contiguous_filled_pos (const stream_position &filled_pos);

  stream_position reserve_no_buffer (const size_t amount);

  BUFFER_UNIT * reserve_with_buffer (const size_t amount, buffered_range **granted_range);

  int detach_written_buffers (std::vector <buffered_range> &buffered_ranges);


  BUFFER_UNIT * check_space_and_advance (const size_t amount);
  
  BUFFER_UNIT * check_space_and_advance_with_ptr (BUFFER_UNIT *ptr, const size_t amount);

};


#endif /* _REPLICATION_STREAM_HPP_ */
