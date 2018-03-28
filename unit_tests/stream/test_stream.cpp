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

#include "test_stream.hpp"
#include "packing_stream.hpp"

namespace test_stream
{

int stream_handler_write::handling_action (const stream_position pos, BUFFER_UNIT *ptr, size_t byte_count)
{
  int i;

  assert (byte_count > OR_INT_SIZE);

  OR_PUT_INT (ptr, byte_count);
  ptr += OR_INT_SIZE;

  for (i = 0; i < byte_count - OR_INT_SIZE; i++)
    {
      *ptr = byte_count % 255;
      ptr++;
    }

  return 0;
}

int stream_handler_read::handling_action (const stream_position pos, BUFFER_UNIT *ptr, size_t byte_count)
{
  int i;
  size_t to_read;
  
  while (byte_count > 0)
    {
      if (m_remaining_to_read <= 0)
        {
          m_remaining_to_read = OR_GET_INT (ptr);
          ptr += OR_INT_SIZE;
          expected_val = m_remaining_to_read % 255;
          m_remaining_to_read -= OR_INT_SIZE;
        }
      
      to_read = MIN (byte_count, m_remaining_to_read);

      for (i = 0; i < to_read; i++)
        {
          if (*ptr != expected_val)
            {
              return -1;
            }
          ptr++;
        }

      m_remaining_to_read -= i;

      if (to_read > byte_count)
        {
          return -1;
        }
      byte_count -= to_read;
    }

  return 0;
}



int test_stream1 (void)
{
  int res = 0;
  int i = 0;
  long long desired_amount = 10000 * 1024;
  long long rem_amount;

  packing_stream *my_stream = new packing_stream ();

  stream_handler_write writer;
  stream_handler_read reader;

  /* writing in stream */
  for (rem_amount = desired_amount, i = 0; rem_amount > 5; i++)
    {
      int amount = 5 + std::rand () % 100000;
      rem_amount -= amount;

      res = my_stream->write (amount, &writer);
      if (res != 0)
        {
          assert (false);
          return res;
        }
    }
  
  /* writing in stream */
  stream_position curr_read_pos = my_stream->get_curr_read_position ();
  for (rem_amount = desired_amount; rem_amount > 5; i--)
    {
      int amount = 5 + std::rand () % 100000;
      res = my_stream->read (curr_read_pos, amount,  &reader);
      if (res != 0)
        {
          assert (false);
          return res;
        }

      curr_read_pos += amount;
      rem_amount -= amount;
    }


  return res;
}

}
