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
#include "stream_buffer.hpp"
#include <vector>

namespace cubstream
{

  stream_packer::stream_packer (packing_stream *stream_arg)
  {
    set_stream (stream_arg);
    m_packer_start_ptr = NULL;
    m_buffer_provider = NULL;
    init (NULL, 0);
  }

  void stream_packer::set_stream (packing_stream *stream_arg)
  {
    m_stream = stream_arg;
  }

  char *stream_packer::start_packing_range (const size_t amount, buffer_context **granted_range)
  {
    char *ptr;
    size_t aligned_amount;

    aligned_amount = DB_ALIGN (amount, MAX_ALIGNMENT);

    ptr = m_stream->reserve_with_buffer (aligned_amount, m_buffer_provider, NULL, granted_range);
    if (ptr != NULL)
      {
	init (ptr, aligned_amount);
	m_packer_start_ptr = ptr;
	m_mapped_range = *granted_range;
	return ptr;
      }

    return NULL;
  }

  int stream_packer::packing_completed (void)
  {
    m_mapped_range->written_bytes += get_curr_ptr() - m_packer_start_ptr;

    if (m_mapped_range->written_bytes >= m_mapped_range->last_pos - m_mapped_range->first_pos)
      {
	m_mapped_range->is_filled = true;
      }

    m_stream->update_contiguous_filled_pos (m_mapped_range->last_pos);

    return NO_ERROR;
  }


  char *stream_packer::start_unpacking_range (const size_t amount, buffer_context **granted_range)
  {
    /* TODO[arnia] */
    char *ptr;
    size_t aligned_amount;

    aligned_amount = DB_ALIGN (amount, MAX_ALIGNMENT);

    ptr = m_stream->get_more_data_with_buffer (aligned_amount, m_buffer_provider, granted_range);
    if (ptr != NULL)
      {
	/* set unpacking context to memory pointer */
	init (ptr, aligned_amount);
	m_packer_start_ptr = ptr;
	m_mapped_range = *granted_range;
	return ptr;
      }

    return NULL;
  }

  char *stream_packer::start_unpacking_range_from_pos (const stream_position &start_pos, const size_t amount,
      buffer_context **granted_range)
  {
    /* TODO[arnia] */
    char *ptr;
    size_t aligned_amount;

    aligned_amount = DB_ALIGN (amount, MAX_ALIGNMENT);

    ptr = m_stream->get_data_from_pos (start_pos, aligned_amount, m_buffer_provider, granted_range);
    if (ptr != NULL)
      {
	/* set unpacking context to memory pointer */
	init (ptr, aligned_amount);
	m_packer_start_ptr = ptr;
	m_mapped_range = *granted_range;
	return ptr;
      }

    return NULL;
  }

  char *stream_packer::extend_unpacking_range (const size_t amount, buffer_context **granted_range)
  {
    /* TODO[arnia] : try to extend withing the current buffer;
     * if required amount is not available, allocate a new buffer which fits both existing range and the extended range */
    return start_unpacking_range (amount, granted_range);
  }

  char *stream_packer::extend_unpacking_range_from_pos (const stream_position &start_pos,
      const size_t amount, buffer_context **granted_range)
  {
    /* TODO[arnia] : try to extend withing the current buffer;
     * if required amount is not available, allocate a new buffer which fits both existing range and the extended range */
    return start_unpacking_range_from_pos (start_pos, amount, granted_range);
  }

} /* namespace cubstream */
