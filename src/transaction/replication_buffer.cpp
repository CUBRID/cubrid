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

replication_buffer::replication_buffer (const size_t req_capacity)
{
  if (init (req_capacity) != NO_ERROR)
    {
      throw ("low memory");
    }
}

int replication_buffer::init (const size_t req_capacity)
{
  storage = malloc (req_capacity);
  if (storage == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  curr_append_ptr = storage;
  end_ptr = storage + req_capacity;
  curr_read_ptr = storage;

  return NO_ERROR;
}


replication_buffer::~replication_buffer ()
{
  free (storage);
  storage = NULL;
}


unsigned char* serial_buffer::reserve (const size_t amount)
{
  unsigned char *ptr;

  ptr = buf->get_curr_append_pos ();
  error = buf->check_space (ptr, OR_INT_SIZE);

  if (error == NO_ERROR)
    {
    ptr = buf->curr_append_ptr.fetch_add (1, SERIAL_BUFF_MEMORY_ORDER);
    if ((error = buf->check_space (ptr, OR_INT_SIZE)) != NO_ERROR)
      {
      return NULL;
      }

    return curr_append_ptr;

    }
  return NULL;
}