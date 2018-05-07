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
#include <vector>

namespace cubstream
{

  stream_packer::stream_packer (packing_stream *stream_arg)
  {
    set_stream (stream_arg);
    m_local_buffer = NULL;
    m_current_local_buffer_size = 0;
    m_stream_reserve_context = NULL;
    m_use_unpack_stream_buffer = false;
    init (NULL, 0);
  }

  void stream_packer::set_stream (packing_stream *stream_arg)
  {
    m_stream = stream_arg;
  }

  char *stream_packer::start_packing_range (const size_t amount)
  {
    char *ptr;
    size_t aligned_amount;

    assert (m_stream_reserve_context == NULL);

    aligned_amount = DB_ALIGN (amount, MAX_ALIGNMENT);

    ptr = m_stream->reserve_with_buffer (aligned_amount, m_stream_reserve_context);
    if (ptr != NULL)
      {
	init (ptr, aligned_amount);
	return ptr;
      }

    return NULL;
  }

  int stream_packer::packing_completed (void)
  {
    assert (m_stream_reserve_context != NULL);

    m_stream_reserve_context->written_bytes += get_curr_ptr () - get_packer_buffer ();

    assert (m_stream_reserve_context->written_bytes <= m_stream_reserve_context->reserved_amount);

    m_stream->commit_append (m_stream_reserve_context);
    
    m_stream_reserve_context = NULL;

    return NO_ERROR;
  }

  int stream_packer::unpacking_completed (void)
    {
      if (m_use_unpack_stream_buffer)
        {
          m_stream->unlatch_read_data (get_packer_buffer (), get_packer_end () - get_packer_buffer ());
          m_use_unpack_stream_buffer = false;
        }

      init (NULL, 0);

      return NO_ERROR;
    }

  char *stream_packer::start_unpacking_range (const size_t amount)
  {
    char *ptr;
    size_t aligned_amount;
    size_t actual_read_btyes;

    if (m_use_unpack_stream_buffer)
      {
        assert (false);
        unpacking_completed ();
      }

    aligned_amount = DB_ALIGN (amount, MAX_ALIGNMENT);

    ptr = m_stream->get_more_data_with_buffer (aligned_amount, actual_read_btyes);
    if (ptr != NULL)
      {
        if (actual_read_btyes < aligned_amount)
          {
            size_t next_actual_read_btyes;

            alloc_local_buffer (aligned_amount);
            memcpy (m_local_buffer, ptr, actual_read_btyes);
            m_stream->unlatch_read_data (ptr, actual_read_btyes);

            ptr = m_stream->get_more_data_with_buffer (aligned_amount - actual_read_btyes, next_actual_read_btyes);
            if (ptr == NULL || next_actual_read_btyes != aligned_amount - actual_read_btyes)
              {
                return NULL;
              }
            memcpy (m_local_buffer + actual_read_btyes, ptr, next_actual_read_btyes);
            m_stream->unlatch_read_data (ptr, next_actual_read_btyes);

            ptr = m_local_buffer;
            m_use_unpack_stream_buffer = false;
          }
        else
          {
            m_use_unpack_stream_buffer = true;
          }
	/* set unpacking context to memory pointer */
	init (ptr, aligned_amount);
	return ptr;
      }

    return NULL;
  }

  char *stream_packer::start_unpacking_range_from_pos (const stream_position &start_pos, const size_t amount)
  {
    char *ptr;
    size_t aligned_amount;
    size_t actual_read_btyes;

    if (m_use_unpack_stream_buffer)
      {
        assert (false);
        unpacking_completed ();
      }

    aligned_amount = DB_ALIGN (amount, MAX_ALIGNMENT);

    ptr = m_stream->get_data_from_pos (start_pos, aligned_amount, actual_read_btyes);
    if (ptr != NULL)
      {
        if (actual_read_btyes < aligned_amount)
          {
            size_t next_actual_read_btyes;
            stream_position next_pos;

            alloc_local_buffer (aligned_amount);
            memcpy (m_local_buffer, ptr, actual_read_btyes);
            m_stream->unlatch_read_data (ptr, actual_read_btyes);

            next_pos = start_pos + actual_read_btyes;
            ptr = m_stream->get_data_from_pos (next_pos, aligned_amount - actual_read_btyes, next_actual_read_btyes);
            if (ptr == NULL || next_actual_read_btyes != aligned_amount - actual_read_btyes)
              {
                return NULL;
              }
            memcpy (m_local_buffer + actual_read_btyes, ptr, next_actual_read_btyes);
            m_stream->unlatch_read_data (ptr, next_actual_read_btyes);

            ptr = m_local_buffer;
            m_use_unpack_stream_buffer = false;
          }
        else
          {
            m_use_unpack_stream_buffer = true;
          }
	/* set unpacking context to memory pointer */
	init (ptr, aligned_amount);
	return ptr;
      }

    return NULL;
  }

  char *stream_packer::alloc_local_buffer (const size_t amount)
    {
      if (amount > m_current_local_buffer_size)
        {
          m_local_buffer = (char *) realloc (m_local_buffer, amount);
          m_current_local_buffer_size = amount;
        }
      return m_local_buffer;
    }

} /* namespace cubstream */
