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
 * stream_packer.cpp
 */

#ident "$Id$"

#include "stream_packer.hpp"
#include "packing_stream.hpp"
#include "packing_stream_buffer.hpp"
#include <vector>

stream_packer::stream_packer (packing_stream *stream_arg)
{
  m_stream = stream_arg;
  m_packer_start_ptr = NULL;
  init (NULL, 0);
}

BUFFER_UNIT *stream_packer::start_packing_range (const size_t amount, buffered_range **granted_range)
{
  BUFFER_UNIT *ptr;

  ptr = m_stream->reserve_with_buffer (amount, m_stream_provider, granted_range);
  if (ptr != NULL)
    {
      init (ptr, amount);
      m_packer_start_ptr = ptr;
      m_mapped_range = *granted_range;
      return ptr;
    }

  return NULL;
}

int stream_packer::packing_completed (void)
{
  m_mapped_range->written_bytes += get_curr_ptr() - m_packer_start_ptr;

  if (m_mapped_range->written_bytes > m_mapped_range->last_pos - m_mapped_range->first_pos)
    {
      m_mapped_range->is_filled = 1;
    }

  m_stream->update_contiguous_filled_pos (m_mapped_range->last_pos);

  return NO_ERROR;
}


BUFFER_UNIT *stream_packer::start_unpacking_range (const size_t amount, buffered_range **granted_range)
{
  /* TODO[arnia] */
  return start_packing_range (amount, granted_range);
}

BUFFER_UNIT *stream_packer::extend_unpacking_range (const size_t amount, buffered_range **granted_range)
{
  /* TODO[arnia] : try to extend withing the current buffer;
   * if required amount is not available, allocate a new buffer which fits both existing range and the extended range */
   return start_unpacking_range (amount, granted_range);
}
