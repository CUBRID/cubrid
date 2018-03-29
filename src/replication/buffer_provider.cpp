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
 * buffer_provider.cpp
 */

#ident "$Id$"

#include "buffer_provider.hpp"
#include "packing_stream_buffer.hpp"

buffer_provider::~buffer_provider ()
{
  assert (m_buffers.size () == 0);

  unpin_all ();
  free_all_buffers ();
}


buffer_provider *buffer_provider::get_default_instance (void)
{
  static buffer_provider global_buffer_provider;

  return &global_buffer_provider;
}

int buffer_provider::allocate_buffer (packing_stream_buffer **new_buffer, const size_t &amount)
{
  BUFFER_UNIT *mem;
  packing_stream_buffer *my_new_buffer;
  size_t to_alloc;

  if (amount > max_alloc_size)
    {
      return ER_FAILED;
    }

  to_alloc = MAX (amount, min_alloc_size);

  mem = (BUFFER_UNIT *) malloc (to_alloc);
  if (mem == NULL)
    {
      return ER_FAILED;
    }
  
  my_new_buffer = new packing_stream_buffer (mem, to_alloc, this);

  add_buffer (my_new_buffer);

  *new_buffer = my_new_buffer;

  return NO_ERROR;
}

int buffer_provider::free_all_buffers (void)
{
  int i;
  for (i = 0; i < m_buffers.size (); i++)
    {
      if (m_buffers[i]->is_unreferenced () == false)
        {
          assert (false);
          return ER_FAILED;
        }

       BUFFER_UNIT *mem = m_buffers[i]->get_buffer ();
       free (mem);

       delete (m_buffers[i]);
       m_buffers[i] = NULL;
    }
  m_buffers.clear ();

  return NO_ERROR;
}


int buffer_provider::extend_buffer (packing_stream_buffer **existing_buffer, const size_t &amount)
{
  if (*existing_buffer != NULL)
    {
      /* TODO[arnia] : to extend an existing buffer with an amount : to use for unpacking of stream entry data
       * after unpacking of stream entry header
       */
      NOT_IMPLEMENTED();
    }

  return allocate_buffer (existing_buffer, amount);

  return NO_ERROR;
}

int buffer_provider::add_buffer (packing_stream_buffer *new_buffer)
{
  m_buffers.push_back (new_buffer);

  pin (new_buffer);
  
  return NO_ERROR;
};
