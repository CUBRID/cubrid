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

typdef size_t stream_position;

class buffered_range
{
public:
  stream_position first_pos;
  stream_position last_pos;
  serial_buffer *mapped_buffer;
  size_t written_bytes;
  int is_filled;
};

struct stream_entry_header
{
  stream_position prev_record;
  MVCCID mvccid;
  unsinged short count_replication_entries;
  unsigned int data_size;

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

  vector <replication_entry *> repl_entries;

  stream_entry_header header;

  int stream_entry_id;

  buffered_range *my_buffered_range;

public:
  stream_entry() { buffer = NULL; written_bytes = 0; }
  ~stream_entry();

  int pack (replication_serialization *serializator);

  size_t get_entries_size (void);

  int add_repl_entry (replication_entry *repl_entry) { repl_entries.push_back (repl_entry); };

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
  stream_provider *provider;

  /* TODO : maybe these should be moved as sub-object for each serial_buffer mapped onto the stream */
  vector<buffered_range> buffered_ranges;

  /* current stream position not allocated yet to a replication generator */
  stream_position curr_position;

  //stream_position last_send_pos;
  
  /* last position completed and requested to be send to slave */
  stream_position last_request_to_send_pos;

  stream_positon max_buffered_position;

  /* size of all active buffers attached to this stream */
  size_t total_buffered_size;

  /* size of all active buffers attached to stream, after which we need to start deferring them to log_file 
   * for flushing;
   * normal mode should not need this : all buffers are send to MRC_Manager to be send to slave */
  size_t trigger_flush_to_disk_size;
  
public:
  replication_stream (const stream_provider *my_provider);
  ~replication_stream ();

  int init (stream_position start_position = 0);

  int add_buffer (serial_buffer *new_buffer, const stream_position &first_pos,
                  const stream_position &last_pos, buffered_range **granted_range);

  int remove_mapped_buffer (serial_buffer *new_buffer, buffered_range &mapped_range);

  int update_last_flushed_pos (stream_position filled_pos);

  stream_position reserve_no_buffer (const size_t amount);

  BUFFER_UNIT * reserve_with_buffer (const size_t amount, buffered_range **granted_range);

  BUFFER_UNIT * check_space_and_advance (const size_t amount);
  
  BUFFER_UNIT * check_space_and_advance_with_ptr (BUFFER_UNIT *ptr, const size_t amount);

  int add_buffer (serial_buffer *new_buffer, const stream_position &first_pos, const stream_position &last_pos,
                  buffered_range **granted_range);
};


#endif /* _REPLICATION_STREAM_HPP_ */
