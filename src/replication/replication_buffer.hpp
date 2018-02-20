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
 * replication_buffer.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_BUFFER_HPP_
#define _REPLICATION_BUFFER_HPP_

#include <atomic>
#include <vector>
#include "dbtype.h"
#include "common_utils.hpp"


class pinnable;
class replication_stream;

/*
 * This should serve as storage for streams.
 * This is not intended to be used as character stream, but as buld operations: users of it
 * reserve / allocate parts of it; there are objects which deal of byte level operations (see : serializator)
 *
 * Each buffer has a storage producer - we call it stream_provider 
 * (the one which decides when to create or scrap a buffer)
 * 
 * log_generator uses it to add replication entries
 *   - in such case, log_generator should be  responsible for triggering memory allocation
 * Also, it should be used as storage for decoding replication entries, either from file or network buffers
 *   - in this case, the log_consumer is providing new content
 */
class serial_buffer : public pinnable
{
public:
  const enum std::memory_order SERIAL_BUFF_MEMORY_ORDER = std::memory_order_relaxed;

  serial_buffer (const size_t req_capacity = 0) { storage = NULL; attached_stream = NULL; };

  virtual int init (const size_t req_capacity) = 0;

  BUFFER_UNIT * reserve (const size_t amount);

  BUFFER_UNIT * get_buffer (void) { return storage; };

  BUFFER_UNIT * get_curr_append_ptr (void) { return storage + write_stream_reference.stream_curr_pos - write_stream_reference.start_pos; };

  size_t get_buffer_size (void) { return end_ptr - storage; };

  /* mapping methods : a memory already exists, just instruct buffer to use it */
  int map_buffer (BUFFER_UNIT *ptr, const size_t count);
  int map_buffer_with_pin (serial_buffer *ref_buffer, pinner *referencer);

  /* TODO[arnia] : STREAM_MODE should be property of replication_stream (and called STREAM_TYPE instead ) ? */
  int attach_stream (replication_stream *stream, const STREAM_MODE stream_mode, const stream_position &stream_start);
  int dettach_stream (replication_stream *stream, const STREAM_MODE stream_mode);
  
  int check_stream_append_contiguity (const replication_stream *stream, const stream_position &req_pos);


protected:
  size_t capacity;

  /* start of allocated memory */
  BUFFER_UNIT *storage;
  /* end of allocated memory */
  BUFFER_UNIT *end_ptr;

  /* mapping of buffer to streams :
   * several read streams can be attached to buffer, only one write stream
   */
  stream_reference write_stream_reference;
  std::vector<stream_reference> read_stream_references;
  
  replication_stream *attached_stream;
};

class replication_buffer : public serial_buffer
{
public:
  replication_buffer (const size_t req_capacity);

  ~replication_buffer (void);

  int init (const size_t req_capacity);
};


#endif /* _REPLICATION_BUFFER_HPP_ */
