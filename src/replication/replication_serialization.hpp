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
 * replication_serialization.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_SERIALIZATION_HPP_
#define _REPLICATION_SERIALIZATION_HPP_

#include "dbtype.h"

class serial_buffer;

/* 
 * the serialization packs or unpacks objects from/into a stream 
 * serialization == context /window over a stream
 * each object should be atomically serialized into the stream.
 * (atomically == means no other object could insert sub-objects in the midle of the packing of 
 * currently serialized object)
 *
 * when packing an atomic object, first a serialization range is reserved into the stream:
 * this ensures that object is contiguosly serialized (this does not imply that that stream range
 * is entirely mapped onto a buffer, but for simplicity the code will make sure of this)
 * 
 * for read (unpack), we have less issues since we don't care if stream entries are interleaved
 * (the only concern is to make sure the serialization range and stream is mapped into a buffer)
 *
 */
class replication_serialization
{
public:
  replication_serialization (replication_stream *stream_arg);
  ~replication_serialization();

  BUFFER_UNIT *reserve_range (const size_t amount, buffered_range **granted_range);

  int pack_int (const int value);
  int unpack_int (int &value);

  int pack_int_array (const int *array, const int count);
  int unpack_int_array (int *array, int &count);

  int pack_int_vector (const vector<int> &array);
  int unpack_int_vector (vector <int> &array);

  int pack_db_value (const DB_VALUE &value);
  int unpack_db_value (DB_VALUE *value);

  int pack_small_string (const char *string);
  int unpack_small_string (char *string, const size_t max_size);

  int pack_stream_entry_header (const stream_entry_header &stream_header);
  int unpack_stream_entry_header (stream_entry_header &stream_header);

  BUFFER_UNIT *get_curr_ptr (void) { return ptr; };

private:
  replication_stream *stream;
  BUFFER_UNIT *ptr;         /* current pointer of serialization */
  BUFFER_UNIT *end_ptr;     /* end of avaialable serialization scope */
};


#endif /* _REPLICATION_SERIALIZATION_HPP_ */
