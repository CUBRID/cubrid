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
 * stream_packer.hpp
 */

#ident "$Id$"

#ifndef _STREAM_PACKER_HPP_
#define _STREAM_PACKER_HPP_

#include "dbtype.h"
#include "common_utils.hpp"
#include "packer.hpp"
#include <vector>

class serial_buffer;
class stream_entry_header;
class packing_stream;
class stream_provider;

/* 
 * this packs or unpacks objects from/into a stream
 * it contains a context /window over a stream
 * each object should be atomically packed into the stream.
 * (atomically == means no other object could insert sub-objects in the midle of the packing of 
 * currently serialized object)
 *
 * when packing an atomic object, first a packing range is reserved into the stream:
 * this ensures that object is contiguosly serialized (this does not imply that that stream range
 * is entirely mapped onto a buffer, but for simplicity, some code will use this)
 *
 */
class stream_packer : public packer
{
public:
  stream_packer (packing_stream *stream_arg);

  /* method for starting a packing context */
  BUFFER_UNIT *start_packing_range (const size_t amount, buffered_range **granted_range);

  /* method for starting an unpacking context */
  BUFFER_UNIT *start_unpacking_range (const size_t amount, buffered_range **granted_range);
  BUFFER_UNIT *extend_unpacking_range (const size_t amount, buffered_range **granted_range);

  int packing_completed (void);
 
private:
  packing_stream *m_stream;

  BUFFER_UNIT *m_packer_start_ptr;


  /* stream provider is optional for packer :
   * it should be used when generating packable objects from multiple threads on the same
   * write stream : there is only one stream holding the global append position, but the storage
   * may be granted by each thread on its own
   * By default, if packer does not have a provider, the stream provider is used (shared memory)
   */
  stream_provider *m_stream_provider;

  /* currently mapped range (set when packing starts) */
  buffered_range *m_mapped_range;
};


#endif /* _STREAM_PACKER_HPP_ */
