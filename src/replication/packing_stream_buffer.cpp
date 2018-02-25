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
 * packing_stream_buffer.cpp
 */

#ident "$Id$"

#include "packing_stream_buffer.hpp"

BUFFER_UNIT * packing_stream_buffer::reserve (const size_t amount)
{
  if (storage + write_stream_reference.buf_end_offset + amount < end_ptr)
    {
      BUFFER_UNIT *ptr = storage + write_stream_reference.buf_end_offset;
      write_stream_reference.buf_end_offset += amount;

      return ptr;
    }

  return NULL;
}

int packing_stream_buffer::attach_stream (packing_stream *stream, const STREAM_MODE stream_mode,
                                          const stream_position &stream_start, const stream_position &stream_end,
                                          const size_t &buffer_start_offset)
{
  if (stream_mode == WRITE_STREAM)
    {
      assert (write_stream_reference.stream == NULL);

      write_stream_reference.stream = stream;

      write_stream_reference.buf_start_offset = buffer_start_offset;
      write_stream_reference.buf_end_offset = buffer_start_offset + stream_end - stream_start;
      
      write_stream_reference.stream_start_pos = stream_start;
      write_stream_reference.stream_end_pos = stream_end;
    }
  else
    {
      stream_reference new_stream_ref;

      new_stream_ref.stream = stream;

      new_stream_ref.buf_start_offset = buffer_start_offset;
      new_stream_ref.buf_end_offset = buffer_start_offset + stream_end - stream_start;
      
      new_stream_ref.stream_start_pos = stream_start;
      new_stream_ref.stream_end_pos = stream_end;

      read_stream_references.push_back (new_stream_ref);
    }
  
  return NO_ERROR;
}

int packing_stream_buffer::dettach_stream (packing_stream *stream, const STREAM_MODE stream_mode)
{
  if (stream_mode == WRITE_STREAM)
    {
      assert (write_stream_reference.stream != NULL);

      write_stream_reference.stream = NULL;
    }
  else
    {
      int i;
      bool found = false;

      for (i = 0; i < read_stream_references.size (); i++)
        {
          if (read_stream_references[i].stream == stream)
            {
              found = true;
            }
        }

      if (!found)
        {
          return ER_FAILED;
        }

      read_stream_references.erase (read_stream_references.begin() + i);
    }
  
  return NO_ERROR;
}

int packing_stream_buffer::check_stream_append_contiguity (const packing_stream *stream,
                                                           const stream_position &req_pos)
{
  if (stream != write_stream_reference.stream)
    {
      /* not my write stream !*/
      return ER_FAILED;
    }

  if (req_pos == write_stream_reference.stream_end_pos)
    {
      return NO_ERROR;
    }

  return ER_FAILED;
}

