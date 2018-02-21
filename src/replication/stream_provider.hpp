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
 * stream_provider.hpp
 */

#ident "$Id$"

#ifndef _STREAM_PROVIDER_HPP_
#define _STREAM_PROVIDER_HPP_

#include "error_code.h"
#include <vector>
#include <cstddef>

class serial_buffer;
class replication_stream;

/*
 * an object of this type provides stream range (with content or empty stream, ready to be filled)
 * it also can allocate buffers as storage support for the stream
 */
class stream_provider
{
private:
  /* a stream provider may allocate several buffers */
  std::vector<serial_buffer*> my_buffers;

public:
  virtual int fetch_for_read (serial_buffer *existing_buffer, const size_t amount) = 0;
  
  virtual int extend_for_write (serial_buffer **existing_buffer, const size_t amount) = 0;

  virtual int flush_ready_stream (void) = 0;

  virtual replication_stream * get_write_stream (void) = 0;

  virtual int add_buffer (serial_buffer *existing_buffer) { my_buffers.push_back (existing_buffer); return NO_ERROR; };
};


#endif /* _STREAM_PROVIDER_HPP_ */
