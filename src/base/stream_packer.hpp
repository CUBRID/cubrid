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
#include "packer.hpp"
#include "packing_stream.hpp"
#include <vector>

namespace cubstream
{

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
  class stream_packer : public cubpacking::packer
  {
    public:
      stream_packer (packing_stream *stream_arg);
      ~stream_packer ()
        {
          unpacking_completed ();
          if (m_local_buffer)
            {
              assert (m_current_local_buffer_size > 0);
              delete []m_local_buffer;
            }
        };

      void set_stream (packing_stream *stream_arg);

      /* method for starting a packing context */
      char *start_packing_range (const size_t amount);

      /* method for starting an unpacking context */
      char *start_unpacking_range (const size_t amount);
      char *start_unpacking_range_from_pos (const stream_position &start_pos, const size_t amount);

      int packing_completed (void);
      int unpacking_completed (void);

      stream_position &get_stream_read_position (void)
      {
	return m_stream->get_curr_read_position ();
      };
    protected:
      char *alloc_local_buffer (const size_t amount);

    private:
      packing_stream *m_stream;

      char *m_local_buffer;
      size_t m_current_local_buffer_size;
      bool m_use_unpack_stream_buffer;

      /* currently reserved context (set when packing starts) */
      stream_reserve_context *m_stream_reserve_context;
  };

} /* namespace cubstream */

#endif /* _STREAM_PACKER_HPP_ */
