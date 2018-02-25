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
 * packing_stream.cpp
 */

#ident "$Id$"

#include "error_code.h"
#include "common_utils.hpp"
#include "packing_stream.hpp"
#include "packable_object.hpp"
#include "packing_stream_buffer.hpp"
#include "stream_packer.hpp"
#include "stream_provider.hpp"
#include <algorithm>


int stream_entry::pack (stream_packer *serializator)
{
  size_t total_stream_entry_size;
  size_t data_size;
  BUFFER_UNIT *stream_start_ptr;
  int i;

  data_size = get_entries_size (serializator);
  total_stream_entry_size = get_header_size () + data_size;

  stream_start_ptr = serializator->start_packing_range (total_stream_entry_size, &m_buffered_range);
  set_header_data_size (data_size);

  pack_stream_entry_header (serializator);
  for (i = 0; i < m_packable_entries.size(); i++)
    {
      m_packable_entries[i]->pack (serializator);
    }

  /* set last packed stream position */
  serializator->packing_completed ();

  return NO_ERROR;
}

/*
 * this is pre-unpack method : it fetches enough data to unpack stream header contents,
 * then fetches (receive from socket) the actual amount of data without unpacking it.
 */
int stream_entry::receive (stream_packer *serializator)
{
  BUFFER_UNIT *stream_start_ptr;
  size_t stream_entry_header_size = get_header_size ();
  int error_code;

  stream_start_ptr = serializator->start_unpacking_range (stream_entry_header_size, &m_buffered_range);

  error_code = unpack_stream_entry_header (serializator);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  stream_start_ptr = serializator->extend_unpacking_range (get_data_packed_size (), &m_buffered_range);
  if (stream_start_ptr == NULL)
    {
      error_code = ER_FAILED;
      return error_code;
    }

  /* the stream entry header is unpacked, and its contents are ready to be unpacked */

  return error_code;
}

/* this is called only right before applying the replication data */
int stream_entry::unpack (stream_packer *serializator)
{
  int i, error_code = NO_ERROR;

  /* TODO[arnia] : make sure the serializator range already points to data contents */

  size_t count_packable_entries = m_packable_entries.size();

  for (i = 0 ; i < count_packable_entries; i++)
    {
      /* create a specific replication_entry object (depending on type) */
      packable_object *packable_entry = dynamic_cast<packable_object *>(get_builder()->create_object (serializator));
      
      assert (packable_entry != NULL);

      /* unpack one replication entry */
      add_packable_entry (packable_entry);
      packable_entry->unpack (serializator);
    }
 
  return error_code;
}

int stream_entry::add_packable_entry (packable_object *entry)
{
  m_packable_entries.push_back (entry);

  return NO_ERROR;
}

size_t stream_entry::get_entries_size (stream_packer *serializator)
{
  size_t total_size = 0;
  int i;

  for (i = 0; i < m_packable_entries.size (); i++)
    {
      total_size += m_packable_entries[i]->get_packed_size (serializator);
    }

  return total_size;
}


////////////////////////////////////////

packing_stream::packing_stream (const stream_provider *my_provider)
{
  m_stream_provider = (stream_provider *) my_provider;

  /* TODO[arnia] : system parameter */
  trigger_flush_to_disk_size = 1024 * 1024;
}

int packing_stream::init (const stream_position &start_position)
{
  append_position = start_position;

  return NO_ERROR;
}

int packing_stream::add_buffer_mapping (packing_stream_buffer *new_buffer, const STREAM_MODE stream_mode,
                                        const stream_position &first_pos, const stream_position &last_pos,
                                        const size_t &buffer_start_offset, buffered_range **granted_range)
{
  buffered_range mapped_range;
  int error_code;

  mapped_range.mapped_buffer = new_buffer;
  mapped_range.first_pos = first_pos;
  mapped_range.last_pos = last_pos;
  mapped_range.written_bytes = 0;

  m_buffered_ranges.push_back (mapped_range);

  if (granted_range != NULL)
    {
      *granted_range = &(m_buffered_ranges.back());
    }

  if (last_pos > m_max_buffered_position)
    {
      m_max_buffered_position = last_pos;
    }

  m_total_buffered_size += last_pos - first_pos;

  error_code = new_buffer->attach_stream (this, stream_mode, first_pos, last_pos, buffer_start_offset);

  return error_code;
}

int packing_stream::remove_buffer_mapping (const STREAM_MODE stream_mode, buffered_range &mapped_range)
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

int packing_stream::update_contiguous_filled_pos (const stream_position &filled_pos)
{
  int i;
  stream_position min_completed_position = MIN (filled_pos, m_max_buffered_position);
  int error_code = NO_ERROR;

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

  if (min_completed_position > m_last_reported_ready_pos)
    {
      m_last_reported_ready_pos = min_completed_position;
      /* signal master_replication_channel that this position is ready to be send */
      /* TODO[arnia] */
      /* mrc_manager->ready_position (m_last_reported_ready_pos); */
    }

  return error_code;
}

BUFFER_UNIT * packing_stream::acquire_new_write_buffer (stream_provider *req_stream_provider,
                                                        const stream_position &start_pos,
                                                        const size_t &amount, buffered_range **granted_range)
{
  packing_stream_buffer *new_buffer = NULL;
  int err;

  err = req_stream_provider->allocate_buffer (&new_buffer, amount);
  if (err != NO_ERROR)
    {
      return NULL;
    }
  
  err = add_buffer_mapping (new_buffer, WRITE_STREAM, start_pos, start_pos + amount, 0,
                            granted_range);
  if (err != NO_ERROR)
    {
      return NULL;
    }

  return new_buffer->get_buffer ();
}

BUFFER_UNIT * packing_stream::reserve_with_buffer (const size_t amount, const stream_provider *context_provider,
                                                   buffered_range **granted_range)
{
  int i;
  int err;
  packing_stream_buffer *new_buffer = NULL;
  stream_provider *curr_stream_provider;

  /* TODO : should decide which provider to choose based on amount to reserve ? */
  curr_stream_provider = (context_provider != NULL) ? (stream_provider *)context_provider : m_stream_provider;

  /* this is my current position in stream */
  stream_position start_pos = reserve_no_buffer (amount);

  /* check if any buffer covers the current range */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (m_buffered_ranges[i].last_pos < start_pos
            && m_buffered_ranges[i].mapped_buffer->check_stream_append_contiguity (this, start_pos) != NO_ERROR)
        {
          BUFFER_UNIT * ptr = m_buffered_ranges[i].mapped_buffer->reserve (amount);
          size_t buffer_start_offset = ptr - m_buffered_ranges[i].mapped_buffer->get_buffer ();

          err = add_buffer_mapping (m_buffered_ranges[i].mapped_buffer, WRITE_STREAM, start_pos, start_pos + amount,
                                    buffer_start_offset, granted_range);
          if (err != NO_ERROR)
            {
              return NULL;
            }

          return ptr;
        }
    }

  if (m_total_buffered_size + amount > trigger_flush_to_disk_size)
    {
      curr_stream_provider->flush_ready_stream ();
    }
  
  return acquire_new_write_buffer (curr_stream_provider, start_pos, amount, granted_range);
}

stream_position packing_stream::reserve_no_buffer (const size_t amount)
{
  /* TODO[arnia] : atomic */
  stream_position initial_pos = append_position;
  append_position += amount;
  return initial_pos;
}

int packing_stream::detach_written_buffers (std::vector <buffered_range> &buffered_ranges)
{
  int i;
  int error_code = NO_ERROR;
  
  /* TODO[arnia]: should detach only buffers filling contiguous stream since last flushed */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (m_buffered_ranges[i].is_filled)
        {
          packing_stream_buffer *buf = m_buffered_ranges[i].mapped_buffer;

          buffered_ranges.push_back (m_buffered_ranges[i]);

          error_code = remove_buffer_mapping (WRITE_STREAM, m_buffered_ranges[i]);
          if (error_code != NO_ERROR)
            {
              return error_code;
            }

          /* TODO : where to unpin buffer from stream_provider ? */
          assert (buf->get_pin_count () == 0);
        }
    }
  return error_code;
}


BUFFER_UNIT * packing_stream::check_space_and_advance (const size_t amount)
{
  buffered_range *granted_range = NULL;
  BUFFER_UNIT *ptr;

  ptr = reserve_with_buffer (amount, m_stream_provider, &granted_range);

  return ptr;
}

BUFFER_UNIT * packing_stream::check_space_and_advance_with_ptr (BUFFER_UNIT *ptr, const size_t amount)
{
  NOT_IMPLEMENTED ();

  return NULL;
}
