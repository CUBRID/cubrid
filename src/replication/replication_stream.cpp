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

#include "error_code.h"
#include "common_utils.hpp"
#include "replication_stream.hpp"
#include "replication_entry.hpp"
#include "replication_buffer.hpp"
#include "replication_serialization.hpp"
#include "stream_provider.hpp"
#include <algorithm>


int stream_entry::pack (replication_serialization *serializator)
{
  size_t total_stream_entry_size;
  size_t data_size = get_entries_size (serializator);
  BUFFER_UNIT *stream_start_ptr;
  int i;

  total_stream_entry_size = get_header_size () + data_size;

  stream_start_ptr = serializator->start_packing_range (total_stream_entry_size, &m_buffered_range);
  m_header.data_size = data_size;

  serializator->pack_stream_entry_header (m_header);
  for (i = 0; i < m_repl_entries.size(); i++)
    {
      m_repl_entries[i]->pack (serializator);
    }
  m_buffered_range->written_bytes += serializator->get_curr_ptr() - stream_start_ptr;

  if (m_buffered_range->written_bytes > m_buffered_range->last_pos - m_buffered_range->first_pos)
    {
      m_buffered_range->is_filled = 1;
    }

  /* set last packed stream position */
  serializator->packing_completed ();

  return NO_ERROR;
}

/*
 * this is pre-unpack method : it fetches enough data to unpack header contents,
 * then fetches (receive from socket) the actual amount of data without unpacking it.
 */
int stream_entry::receive (replication_serialization *serializator)
{
  size_t total_stream_entry_size;
  size_t data_size;
  BUFFER_UNIT *stream_start_ptr;
  size_t stream_entry_header_size = get_header_size ();
  int error_code;

  stream_start_ptr = serializator->start_unpacking_range (stream_entry_header_size, &m_buffered_range);

  error_code = serializator->unpack_stream_entry_header (m_header);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  stream_start_ptr = serializator->extend_unpacking_range (m_header.data_size, &m_buffered_range);
  if (stream_start_ptr == NULL)
    {
      error_code = ER_FAILED;
      return error_code;
    }

  /* the stream entry header is unpacked, and its contents are ready to be unpacked */

  return error_code;
}

/* this is called only right before applying the replication data */
int stream_entry::unpack (replication_serialization *serializator)
{
  size_t total_stream_entry_size;
  size_t data_size;
  BUFFER_UNIT *stream_start_ptr;
  int i, error_code;

  /* TODO[arnia] : make sure the serializator range already points to data contents */

  replication_entry *repl_entry;

  size_t count_replication_entries = m_repl_entries.size();

  for (i = 0 ; i < count_replication_entries; i++)
    {
      /* TODO[arnia] : create a specific replication_entry object (depending on type) */
      /* unpack one replication entry */
      add_repl_entry (repl_entry);
      repl_entry->unpack (serializator);
    }
 
  return error_code;
}


int stream_entry::add_repl_entry (replication_entry *entry)
{
  m_repl_entries.push_back (entry);

  return NO_ERROR;
}

size_t stream_entry::get_entries_size (replication_serialization *serializator)
{
  size_t total_size = 0;
  int i;

  for (i = 0; i < m_repl_entries.size (); i++)
    {
      total_size += m_repl_entries[i]->get_packed_size (serializator);
    }

  return total_size;
}


////////////////////////////////////////

replication_stream::replication_stream (const stream_provider *my_provider)
{
  m_stream_provider = (stream_provider *) my_provider;

  /* TODO[arnia] : system parameter */
  trigger_flush_to_disk_size = 1024 * 1024;
}

int replication_stream::init (const stream_position &start_position)
{
  append_position = start_position;

  return NO_ERROR;
}

int replication_stream::add_buffer_mapping (serial_buffer *new_buffer, const STREAM_MODE stream_mode,
                                            const stream_position &first_pos, const stream_position &last_pos,
                                            buffered_range **granted_range)
{
  buffered_range mapped_range;
  int error_code;

  mapped_range.mapped_buffer = new_buffer;
  mapped_range.first_pos = first_pos;
  mapped_range.last_pos = last_pos;
  mapped_range.written_bytes = 0;

  m_buffered_ranges.push_back (mapped_range);

  *granted_range = &(m_buffered_ranges.back());

  if (last_pos > max_buffered_position)
    {
      max_buffered_position = last_pos;
    }

  total_buffered_size += new_buffer->get_buffer_size ();

  error_code = new_buffer->attach_stream (this, stream_mode, first_pos);

  return error_code;
}

int replication_stream::remove_buffer_mapping (const STREAM_MODE stream_mode, buffered_range &mapped_range)
{
  int error_code = NO_ERROR;

  error_code = mapped_range.mapped_buffer->dettach_stream (this, stream_mode);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  std::vector<buffered_range>::iterator it = std::find (m_buffered_ranges.begin(), m_buffered_ranges.end(), mapped_range);

  if (it != m_buffered_ranges.end())
    {
      m_buffered_ranges.erase (it);
    }
  else
    {
      error_code = ER_FAILED;
    }

  return error_code;
}


int replication_stream::update_contiguous_filled_pos (const stream_position &filled_pos)
{
  int i;
  stream_position min_completed_position = MIN (filled_pos, max_buffered_position);
  int error_code;

  /* parse all mapped ranges and get the minimum start position among all incomplete
   * ranges; we start from a max buffered position (last position of of most recent range)
   * we assume that any gaps between ranges were already covered by ranges which are now send */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (m_buffered_ranges[i].is_filled == 0
          && min_completed_position > m_buffered_ranges[i].first_pos)
        {
          min_completed_position = m_buffered_ranges[i].first_pos;
        }
    }

  if (min_completed_position > last_reported_ready_pos)
    {
      last_reported_ready_pos = min_completed_position;
      /* signal master_replication_channel that this position is ready to be send */
      /* TODO[arnia] */
      /* mrc_manager->ready_position (last_reported_ready_pos); */
    }

  return error_code;
}

BUFFER_UNIT * replication_stream::reserve_with_buffer (const size_t amount, buffered_range **granted_range)
{
  int i;
  int err;
  serial_buffer *new_buffer = NULL;

  /* this is my current position in stream */
  stream_position start_pos = reserve_no_buffer (amount);

  /* check if any buffer covers the current range */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (m_buffered_ranges[i].last_pos < start_pos
            && m_buffered_ranges[i].mapped_buffer->check_stream_append_contiguity (this, start_pos) != NO_ERROR)
        {
          BUFFER_UNIT * ptr = m_buffered_ranges[i].mapped_buffer->reserve (amount);

          err = add_buffer_mapping (m_buffered_ranges[i].mapped_buffer, WRITE_STREAM, start_pos, start_pos + amount,
                                    granted_range);
          if (err != NO_ERROR)
            {
              return NULL;
            }

          return ptr;
        }
    }

  if (total_buffered_size + amount > trigger_flush_to_disk_size)
    {
      m_stream_provider->flush_ready_stream ();
    }
  
  err = m_stream_provider->extend_for_write (&new_buffer, amount);
  if (err != NO_ERROR)
    {
      return NULL;
    }
  
  err = add_buffer_mapping (new_buffer, WRITE_STREAM, start_pos, start_pos + new_buffer->get_buffer_size (),
                            granted_range);
  if (err != NO_ERROR)
    {
      return NULL;
    }

  return new_buffer->get_buffer ();
}

stream_position replication_stream::reserve_no_buffer (const size_t amount)
{
  /* TODO[arnia] : atomic */
  stream_position initial_pos = append_position;
  append_position += amount;
  return initial_pos;
}

int replication_stream::detach_written_buffers (std::vector <buffered_range> &buffered_ranges)
{
  int i;
  int error_code;
  
  /* TODO[arnia]: should detach only buffers filling contiguous stream since last flushed */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (m_buffered_ranges[i].is_filled)
        {
          serial_buffer *buf = m_buffered_ranges[i].mapped_buffer;

          buffered_ranges.push_back (m_buffered_ranges[i]);

          error_code = remove_buffer_mapping (WRITE_STREAM, m_buffered_ranges[i]);
          if (error_code != NO_ERROR)
            {
              return error_code;
            }

          assert (buf->get_pin_count () == 0);
        }
    }
}


BUFFER_UNIT * replication_stream::check_space_and_advance (const size_t amount)
{
  buffered_range *granted_range = NULL;
  BUFFER_UNIT *ptr;

  ptr = reserve_with_buffer (amount, &granted_range);

  return ptr;
}

BUFFER_UNIT * replication_stream::check_space_and_advance_with_ptr (BUFFER_UNIT *ptr, const size_t amount)
{
  NOT_IMPLEMENTED ();

  return NULL;
}
