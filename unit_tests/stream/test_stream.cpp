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
#include "buffer_provider.hpp"

namespace test_stream
{

int stream_handler_write::handling_action (const stream_position pos, BUFFER_UNIT *ptr, const size_t byte_count,
                                           size_t *processed_bytes)
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

  if (processed_bytes != NULL)
    {
      *processed_bytes = byte_count;
    }

  return 0;
}

int stream_handler_read::handling_action (const stream_position pos, BUFFER_UNIT *ptr, const size_t byte_count,
                                          size_t *processed_bytes)
{
  int i;
  size_t to_read;
  size_t byte_count_rem = byte_count;
  
  while (byte_count_rem > 0)
    {
      if (m_remaining_to_read <= 0)
        {
          if (byte_count_rem < OR_INT_SIZE)
            {
              /* not_enough to decode size prefix */
              break;
            }

          m_remaining_to_read = OR_GET_INT (ptr);
          ptr += OR_INT_SIZE;
          expected_val = m_remaining_to_read % 255;
          m_remaining_to_read -= OR_INT_SIZE;
          byte_count_rem -= OR_INT_SIZE;
        }
      
      to_read = MIN (byte_count_rem, m_remaining_to_read);

      for (i = 0; i < to_read; i++)
        {
          if (*ptr != expected_val)
            {
              return ER_FAILED;
            }
          ptr++;
        }

      m_remaining_to_read -= i;

      if (to_read > byte_count_rem)
        {
          return ER_FAILED;
        }
      byte_count_rem -= to_read;
    }

  if (processed_bytes != NULL)
    {
      *processed_bytes = byte_count - byte_count_rem;
    }
  return NO_ERROR;
}



int test_stream1 (void)
{
  int res = 0;
  int i = 0;
  long long desired_amount = 1 * 1024;
  long long writted_amount;
  long long rem_amount;
  int max_data_size = 500;

  packing_stream *my_stream = new packing_stream ();

  stream_handler_write writer;
  stream_handler_read reader;

  /* writing in stream */
  for (rem_amount = desired_amount, i = 0; rem_amount > 5; i++)
    {
      int amount = 5 + std::rand () % max_data_size;
      rem_amount -= amount;

      res = my_stream->write (amount, NULL, &writer);
      if (res != 0)
        {
          assert (false);
          return res;
        }
    }
  writted_amount = desired_amount - rem_amount;

  /* read from stream */
  stream_position start_read_pos = my_stream->get_curr_read_position ();
  stream_position curr_read_pos = start_read_pos;
  for (rem_amount = writted_amount; rem_amount > 5; i--)
    {
      int amount = 5 + std::rand () % max_data_size;
      size_t processed_amount;

      amount = MIN (writted_amount - curr_read_pos, amount);

      res = my_stream->read (curr_read_pos, amount, &processed_amount, &reader);
      if (res != 0)
        {
          assert (false);
          return res;
        }

      curr_read_pos += processed_amount;
      rem_amount -= processed_amount;
    }

  std::vector <buffer_context> my_buffered_ranges;

  my_stream->collect_buffers (my_buffered_ranges, COLLECT_ALL_BUFFERS, COLLECT_AND_DETACH);

  buffer_provider::get_default_instance ()->unpin_all ();
  buffer_provider::get_default_instance ()->free_all_buffers ();

  return res;
}

}
