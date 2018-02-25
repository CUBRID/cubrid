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
 * packing_stream_buffer.hpp
 */

#ident "$Id$"

#ifndef _PACKING_STREAM_BUFFER_HPP_
#define _PACKING_STREAM_BUFFER_HPP_

#include <atomic>
#include <vector>
#include "dbtype.h"
#include "common_utils.hpp"
#include "packing_buffer.hpp"


class replication_stream;

/*
 * This should serve as storage for packing streams.
 *
 * Each buffer has a storage producer - we call it stream_provider 
 * (the one which decides when to create or scrap a buffer)
 * 
 */
class packing_stream_buffer : public packing_buffer
{
public:
  packing_stream_buffer (BUFFER_UNIT *ptr, const size_t buf_size, pinner *referencer) { init (ptr, buf_size, referencer); };

  BUFFER_UNIT * reserve (const size_t amount);

  BUFFER_UNIT * get_curr_append_ptr (void) { return storage + write_stream_reference.buf_end_offset; };

  /* mapping methods : a memory already exists, just instruct buffer to use it */
  //int map_buffer_with_pin (serial_buffer *ref_buffer, pinner *referencer);

  /* TODO[arnia] : STREAM_MODE should be property of replication_stream (and called STREAM_TYPE instead ) ? */
  int attach_stream (packing_stream *stream, const STREAM_MODE stream_mode,
                     const stream_position &stream_start, const stream_position &stream_end,
                     const size_t &buffer_start_offset);

  int dettach_stream (packing_stream *stream, const STREAM_MODE stream_mode);
  
  int check_stream_append_contiguity (const packing_stream *stream, const stream_position &req_pos);

  bool is_unreferenced (void) { return (write_stream_reference.stream == NULL) && (read_stream_references.size () == 0); };


protected:
  /* mapping of buffer to streams :
   * several read streams can be attached to buffer, only one write stream
   */
  stream_reference write_stream_reference;
  
  /* read streams : each read stream is unique */
  std::vector<stream_reference> read_stream_references;
 
};


#endif /* _PACKING_STREAM_BUFFER_HPP_ */
