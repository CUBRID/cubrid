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

#if 0
BUFFER_UNIT * replication_buffer::reserve (const size_t amount)
{
  BUFFER_UNIT *ptr;
  int error;

  ptr = get_curr_append_ptr ();
  error = check_space (ptr, OR_INT_SIZE);

  if (error == NO_ERROR)
    {
      ptr = storage + curr_append_pos.fetch_add (amount, SERIAL_BUFF_MEMORY_ORDER);
      if ((error = check_space (ptr, OR_INT_SIZE)) != NO_ERROR)
        {
          return NULL;
        }

      return storage + curr_append_pos;
    }

  return NULL;
}

int replication_buffer::check_space (const BUFFER_UNIT *ptr, size_t amount)
{
  if (ptr + amount < end_ptr)
    {
      return NO_ERROR;
    }
  return ER_FAILED;
}
#endif