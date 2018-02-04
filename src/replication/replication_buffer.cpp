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
 * replication_buffer.cpp
 */

#ident "$Id$"

#include "replication_buffer.hpp"
#include "object_representation.h"


BUFFER_UNIT * serial_buffer::reserve (const size_t amount)
{
  if (storage + curr_append_pos + amount < end_ptr)
    {
      BUFFER_UNIT *ptr = storage + curr_append_pos;
      curr_append_pos += amount;

      return ptr;
    }

  return NULL;
}

int serial_buffer::map_buffer (BUFFER_UNIT *ptr, const size_t count)
{
  assert (storage == NULL && capacity == 0);

  storage = ptr;
  capacity = count;

  return NO_ERROR;
}

int serial_buffer::map_buffer_with_pin (serial_buffer *ref_buffer, pinner *referencer)
{
  int error = NO_ERROR;
  
  error = map_buffer (ref_buffer->get_buffer (), ref_buffer->get_buffer_size ());
  if (error != NO_ERROR)
    {
      error = add_pinner (referencer);
    }

  return error;
}

int serial_buffer::attach_to_stream (replication_stream *stream, const stream_position &stream_start)
{
  attached_stream = stream;
  attached_stream_start_pos = stream_start;

  add_pinner (stream);
  
  return NO_ERROR;
}

int serial_buffer::dettach_from_stream (replication_stream *stream)
{
  attached_stream = NULL;
  attached_stream_start_pos = -1;

  remove_pinner (stream);
  
  return NO_ERROR;
}

int serial_buffer::check_stream_append_contiguity (replication_stream *stream, const stream_position &req_pos)
{
  if (stream != attached_stream)
    {
      /* not my stream !*/
      return ER_FAILED;
    }
  if (req_pos == attached_stream_start_pos + curr_append_pos)
    {
      return NO_ERROR;
    }

  return ER_FAILED;
}

/* ---------------------------------------------------------------- */

replication_buffer::replication_buffer (const size_t req_capacity)
{
  if (init (req_capacity) != NO_ERROR)
    {
      throw ("low memory");
    }
}

int replication_buffer::init (const size_t req_capacity)
{
  storage = (BUFFER_UNIT *) malloc (req_capacity);
  if (storage == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  curr_append_pos = 0;
  end_ptr = storage + req_capacity;
  curr_read_pos = 0;

  return NO_ERROR;
}

replication_buffer::~replication_buffer ()
{
  free (storage);
  storage = NULL;
}
