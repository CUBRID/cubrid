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
#include "common_utils.hpp"
#include <vector>
#include <cstddef>

class packing_stream_buffer;
class packing_stream;

/*
 * an object of this type provides stream range (with content or empty stream, ready to be filled)
 * it also can allocate buffers as storage support for the stream
 *
 * TODO : maybe should be split into :
 * storage_provider : an object which only deals with memory allocation, and
 * content_provider : an object which provides data into stream (by recv from socket, read from file,
 *                    depending on stream input)
 */
class stream_provider : public pinner
{
private:
  /* a stream provider may allocate several buffers */
  std::vector<packing_stream_buffer*> m_buffers;

protected:
  size_t min_alloc_size;
  size_t max_alloc_size;

public:

  stream_provider () { min_alloc_size = 512 * 1024; max_alloc_size = 100 * 1024 * 1024; };

  ~stream_provider () { unpin_all (); free_all_buffers (); };

  virtual int allocate_buffer (packing_stream_buffer **new_buffer, const size_t &amount);

  virtual int free_all_buffers (void);

  virtual int fetch_for_read (packing_stream_buffer *existing_buffer, const size_t &amount) = 0;
  
  virtual int extend_buffer (packing_stream_buffer **existing_buffer, const size_t &amount);

  virtual int flush_ready_stream (void) = 0;

  virtual packing_stream * get_write_stream (void) = 0;

  virtual int add_buffer (packing_stream_buffer *new_buffer) { m_buffers.push_back (new_buffer); return NO_ERROR; };
};


#endif /* _STREAM_PROVIDER_HPP_ */
