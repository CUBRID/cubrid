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
 * stream_common.hpp
 */

#ident "$Id$"

#ifndef _STREAM_COMMON_HPP_
#define _STREAM_COMMON_HPP_

#include "packing_common.hpp"
#include <set>
#include <assert.h>
#include <cstddef>
#include "error_code.h"

typedef unsigned long long stream_position;

enum stream_mode
{
  WRITE_STREAM = 0,
  READ_STREAM
};
typedef enum stream_mode STREAM_MODE;

class packing_stream_buffer;
class packing_stream;
/* this is stored in packing_stream */
class buffer_context
{
public:
  buffer_context() : mapped_buffer (NULL), is_filled (false) {};
  /* range of stream position reserved */
  stream_position first_pos;
  stream_position last_pos;
  /* allocated position (the amount up to which last_pos can grow) */
  stream_position last_allocated_pos;
  packing_stream_buffer *mapped_buffer;
  size_t written_bytes;
  bool is_filled;
  
  bool is_range_mapped (const stream_position &start, const size_t &amount);
  size_t get_mapped_amount (const stream_position &start);
  bool is_range_contiguously_mapped (const stream_position &start, const size_t &amount);
  char * extend_range (const size_t &amount);

  bool operator== (const buffer_context &rhs) const
    {
      return mapped_buffer == rhs.mapped_buffer;
    };

};

/* this is stored in packing_stream_buffer */
class stream_reference
{
public:
  stream_reference() : stream(NULL) {};

  packing_stream *stream;

  /*
   * currently mapped start and end offset relative to buffer start 
   * for write streams, the start_offset should always be zero
   */
  size_t buf_start_offset;
  size_t buf_end_offset;

  /* first and last position mapped to the stream (these are stream positions) */
  stream_position stream_start_pos;
  stream_position stream_end_pos;
};

#endif /* _STREAM_COMMON_HPP_ */
