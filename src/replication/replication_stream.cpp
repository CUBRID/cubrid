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
 * replication_stream.cpp
 */

#ident "$Id$"


#include "replication_stream.hpp"
#include "replication_serializatio.hpp"


int stream_entry::pack (replication_serialization *serializator)
{
  size_t total_stream_entry_size;
  unsigned int data_size = (unsigned int) get_entries_size ();
  BUFFER_UNIT *stream_start_ptr;

  total_stream_entry_size = get_header_size () + data_size;

  stream_start_ptr = serializator->reserve_range (total_stream_entry_size, my_buffered_range);
  header.data_size = data_size;

  serializator->pack_stream_entry_header (&header);
  for (i = 0; i < repl_entries.size(); i++)
    {
      repl_entries(i)->pack (serializator);
    }
  my_buffered_range.written_bytes += serializator->get_curr_ptr() - stream_start_ptr;
  
  if (my_buffered_range.written_bytes > my_buffered_range.last_pos - my_buffered_range.first_pos)
    {
      my_buffered_range.is_filled = 1;
    }

  return NO_ERROR;
}

int stream_entry::append_entry (replication_entry *entry)
{
  repl_entries.push_back (entry);
}

size_t stream_entry::get_entries_size (void)
{
  size_t total_size = 0;

  for (i = 0; i < repl_entries.size (); i++)
    {
      total_size += repl_entries.get_size ();
    }

  return total_size;
}


////////////////////////////////////////

replication_stream::replication_stream (const stream_provider *my_provider)
{
  provider = my_provider;

  /* TODO[arnia] : system parameter */
  trigger_flush_to_disk_size = 1024 * 1024;
}

int replication_stream::init (stream_position start_position)
{
  curr_position = start_position;

  return NO_ERROR;
}

int replication_stream::add_buffer_mapping (serial_buffer *new_buffer, const stream_position &first_pos,
                                            const stream_position &last_pos, buffered_range **granted_range)
{
  buffered_range mapped_range;

  mapped_range.mapped_buffer = new_buffer;
  mapped_range.first_pos = first_pos;
  mapped_range.last_pos = last_pos;
  mapped_range.written_bytes = 0;

  buffered_ranges.push_back (mapped_range);

  *granted_range = &(buffered_ranges.back());

  if (last_pos > max_buffered_position)
    {
      max_buffered_position = last_pos;
    }

  total_buffered_size += new_buffer->get_buffer_size ();

  new_buffer->attach_to_stream (this, first_pos);
}

int replication_stream::remove_buffer_mapping (serial_buffer *new_buffer, buffered_range &mapped_range)
{
  new_buffer->dettach_from_stream (this);

  buffered_ranges.remove (mapped_range);
}


int replication_stream::update_last_flushed_pos (stream_position filled_pos)
{
  int i;
  stream_position min_completed_position = max_buffered_position;

  /* parse all mapped ranges and get the minimum start position among all incomplete
   * ranges; we start from a max buffered position (last position of of most recent range)
   * we assume that any gaps between ranges were already covered by ranges which are now send */
  for (i = 0; i < buffered_ranges.size(); i++)
    {
      if (buffered_ranges[i]->is_filled == 0
          && min_completed_position > buffered_ranges[i]->first_pos)
        {
          min_completed_position = buffered_ranges[i]->first_pos;
        }
    }

  if (min_completed_position > last_request_to_send_pos)
    {
      last_request_to_send_pos = min_completed_position;
      /* signal master_replication_channel that this position is ready to be send */
      /* TODO[arnia] */
      /* mrc_manager->ready_position (last_request_to_send_pos); */
    }
}

BUFFER_UNIT * replication_stream::reserve_with_buffer (const size_t amount, buffered_range **granted_range)
{
  int i;
  /* this is my current position in stream */
  stream_position start_pos = reserve_no_buffer (amount);

  /* check if any buffer covers the current range */
  for (i = 0; i < buffered_ranges.size(); i++)
    {
      if (buffered_ranges[i].last_pos < start_pos
            && buffered_ranges[i].buffer->check_stream_append_contiguity (this, start_pos) != NO_ERROR)
        {
          BUFFER_UNIT * ptr = buffered_ranges[i].buffer->reserve (amount);

          err = add_buffer_mapping (buffered_ranges[i].buffer, start_pos, start_pos + amount, granted_range);
          if (err != NO_ERROR)
            {
              return NULL;
            }

          return ptr;
        }
    }

  if (total_buffered_size + amount > trigger_flush_to_disk_size)
    {
      start_deffer_buffers_to_file ();
    }
  serial_buffer *new_buffer = NULL;
  err = provider->extend_for_write (&new_buffer, amount);
  if (err != NO_ERROR)
    {
      return NULL;
    }
  
  err = add_buffer_mapping (new_buffer, start_pos, start_pos + new_buffer->get_buffer_size (), buffered_ranges);
  if (err != NO_ERROR)
    {
      return NULL;
    }

  return new_buffer->get_storage();
}

int replication_stream::start_deffer_buffers_to_file (void)
{
  int i;

  for (i = 0; i < buffered_ranges.size(); i++)
    {
      if (buffered_ranges[i].is_filled
          && buffered_ranges[i].last_pos < last_request_to_send_pos)
        {
          /* move this range and buffer to log_file ownership */
          if (buffered_ranges[i]->mapped_buffer->get_pin_count () == 0)
            {
              buffered_range mapped_range = buffered_ranges[i];
              remove_buffer_mapping (mapped_range);


            }
        }
    }
}
stream_position replication_stream::reserve_no_buffer (const size_t amount)
{
  /* TODO[arnia] : atomic */
  stream_position initial_pos = curr_position;
  curr_position += amount;
  return initial_pos;
}


BUFFER_UNIT * replication_stream::check_space_and_advance (const size_t amount)
{
  BUFFER_UNIT * ret_pos = curr_ptr;
  if (curr_ptr + amount < end_ptr)
    {
      curr_ptr += amount;
      return ret_pos;
    }

  /* TODO[arnia]: get more data */

  return NULL;
}

BUFFER_UNIT * replication_stream::check_space_and_advance_with_ptr (BUFFER_UNIT *ptr, const size_t amount)
{
  if (ptr + amount < end_ptr)
    {
      return ptr;
    }

  /* TODO[arnia]: get more data */
  return NULL;
}
