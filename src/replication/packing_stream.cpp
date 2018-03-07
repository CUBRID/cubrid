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


stream_entry::stream_entry (packing_stream *stream)
{ 
  m_stream = stream;
  m_buffered_range = NULL;
  m_data_start_position = 0;
  set_packable (false);
}

int stream_entry::pack (void)
{
  size_t total_stream_entry_size;
  size_t data_size, header_size;
  BUFFER_UNIT *stream_start_ptr;
  int i;

  assert (m_is_packable == true);
  if (m_packable_entries.size() == 0)
    {
      return NO_ERROR;
    }

  stream_packer *serializator = get_packer ();
  header_size = get_header_size ();
  data_size = get_entries_size ();
  total_stream_entry_size = header_size + data_size;

  stream_start_ptr = serializator->start_packing_range (total_stream_entry_size, &m_buffered_range);
  set_header_data_size (data_size);

  pack_stream_entry_header ();
  for (i = 0; i < m_packable_entries.size(); i++)
    {
      serializator->align (MAX_ALIGNMENT);
#if !defined (NDEBUG)
      BUFFER_UNIT *old_ptr = serializator->get_curr_ptr ();
      BUFFER_UNIT *curr_ptr;
      size_t entry_size = m_packable_entries[i]->get_packed_size (serializator);
#endif

      m_packable_entries[i]->pack (serializator);

#if !defined (NDEBUG)
      curr_ptr = serializator->get_curr_ptr ();
      assert (curr_ptr - old_ptr == entry_size);
#endif
    }

  /* set last packed stream position */
  serializator->packing_completed ();

  return NO_ERROR;
}

/*
 * this is pre-unpack method : it fetches enough data to unpack stream header contents,
 * then fetches (receive from socket) the actual amount of data without unpacking it.
 */
int stream_entry::receive (void)
{
  BUFFER_UNIT *stream_start_ptr;
  stream_packer *serializator = get_packer ();
  size_t stream_entry_header_size = get_header_size ();
  int error_code;

  stream_start_ptr = serializator->start_unpacking_range (stream_entry_header_size, &m_buffered_range);

  error_code = unpack_stream_entry_header ();
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  m_data_start_position = serializator->get_stream_read_position ();
  /* force read (fetch data from stream source) */
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
int stream_entry::unpack (void)
{
  int i, error_code = NO_ERROR;

  /* TODO[arnia] : make sure the serializator range already points to data contents */

  stream_packer *serializator = get_packer ();
  size_t count_packable_entries = get_packable_entry_count_from_header ();
  BUFFER_UNIT *stream_start_ptr = serializator->start_unpacking_range_from_pos (m_data_start_position,
                                                                                get_data_packed_size (),
                                                                                &m_buffered_range);

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

size_t stream_entry::get_entries_size (void)
{
  size_t entry_size, total_size = 0;
  int i;

  stream_packer *serializator = get_packer ();
  for (i = 0; i < m_packable_entries.size (); i++)
    {
      entry_size = m_packable_entries[i]->get_packed_size (serializator);
      entry_size = DB_ALIGN (entry_size, MAX_ALIGNMENT);
      total_size += entry_size;
    }

  return total_size;
}


////////////////////////////////////////

packing_stream::packing_stream (const stream_provider *my_provider)
{
  m_stream_provider = (stream_provider *) my_provider;
  m_last_reported_ready_pos = 0;
  m_max_buffered_position = 0;
  m_total_buffered_size = 0;
  m_read_position = 0;

  /* TODO[arnia] : system parameter */
  trigger_flush_to_disk_size = 1024 * 1024;
}

int packing_stream::init (const stream_position &start_position)
{
  m_append_position = start_position;

  return NO_ERROR;
}

int packing_stream::create_buffer_context (packing_stream_buffer *new_buffer, const STREAM_MODE stream_mode,
                                           const stream_position &first_pos, const stream_position &last_allocated_pos,
                                           const size_t &buffer_start_offset, buffer_context **granted_range)
{
  buffer_context mapped_range;
  int error_code = NO_ERROR;

  mapped_range.mapped_buffer = new_buffer;
  mapped_range.first_pos = first_pos;
  mapped_range.last_pos = first_pos;
  mapped_range.last_allocated_pos= last_allocated_pos;
  mapped_range.written_bytes = 0;

  m_buffered_ranges.push_back (mapped_range);

  if (granted_range != NULL)
    {
      *granted_range = &(m_buffered_ranges.back());
    }

  if (last_allocated_pos > m_max_buffered_position)
    {
      m_max_buffered_position = last_allocated_pos;
    }

  m_total_buffered_size += last_allocated_pos - first_pos;

  error_code = new_buffer->attach_stream (this, stream_mode, first_pos, last_allocated_pos, buffer_start_offset);

  return error_code;
}

int packing_stream::add_buffer_context (packing_stream_buffer *new_buffer, const STREAM_MODE stream_mode,
                                        const stream_position &first_pos, const stream_position &last_pos,
                                        const size_t &buffer_start_offset, buffer_context **granted_range)
{
  buffer_context mapped_range;
  int error_code = NO_ERROR;

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

int packing_stream::remove_buffer_mapping (const STREAM_MODE stream_mode, buffer_context &mapped_range)
{
  int error_code = NO_ERROR;

  error_code = mapped_range.mapped_buffer->dettach_stream (this, stream_mode);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  std::vector<buffer_context>::iterator it = std::find (m_buffered_ranges.begin(), m_buffered_ranges.end(), mapped_range);

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
      if (m_buffered_ranges[i].is_filled == false
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
                                                        const size_t &amount, buffer_context **granted_range)
{
  packing_stream_buffer *new_buffer = NULL;
  int err;

  err = req_stream_provider->allocate_buffer (&new_buffer, amount);
  if (err != NO_ERROR)
    {
      return NULL;
    }

  err = create_buffer_context (new_buffer, WRITE_STREAM, start_pos, start_pos + amount, 0, granted_range);
  if (err != NO_ERROR)
    {
      return NULL;
    }

  return new_buffer->get_buffer ();
}

BUFFER_UNIT * packing_stream::reserve_with_buffer (const size_t amount, const stream_provider *context_provider,
                                                   buffer_context **granted_range)
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
      if (m_buffered_ranges[i].is_range_contiguously_mapped (start_pos, amount))
        {
          *granted_range = &(m_buffered_ranges[i]);
          return m_buffered_ranges[i].extend_range (amount);
        }
      else if (m_buffered_ranges[i].last_allocated_pos == start_pos)
        {
          BUFFER_UNIT * ptr = m_buffered_ranges[i].mapped_buffer->reserve (amount);
          size_t buffer_start_offset = ptr - m_buffered_ranges[i].mapped_buffer->get_buffer ();

          err = add_buffer_context (m_buffered_ranges[i].mapped_buffer, WRITE_STREAM, start_pos, start_pos + amount,
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
      curr_stream_provider->flush_old_stream_data ();
    }
  
  return acquire_new_write_buffer (curr_stream_provider, start_pos, amount, granted_range);
}

stream_position packing_stream::reserve_no_buffer (const size_t amount)
{
  /* TODO[arnia] : atomic */
  stream_position initial_pos = m_append_position;
  m_append_position += amount;
  return initial_pos;
}

int packing_stream::collect_buffers (std::vector <buffer_context> &buffered_ranges, COLLECT_FILTER collect_filter,
                                     COLLECT_ACTION collect_action)
{
  int i;
  int error_code = NO_ERROR;
  
  /* TODO[arnia]: should detach only buffers filling contiguous stream since last flushed */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (collect_filter == COLLECT_ALL_BUFFERS || m_buffered_ranges[i].is_filled)
        {
          packing_stream_buffer *buf = m_buffered_ranges[i].mapped_buffer;

          buffered_ranges.push_back (m_buffered_ranges[i]);

          if (collect_action == COLLECT_AND_DETACH)
            {
              error_code = remove_buffer_mapping (WRITE_STREAM, m_buffered_ranges[i]);
              if (error_code != NO_ERROR)
                {
                  return error_code;
                }

              /* TODO : where to unpin buffer from stream_provider ? */
              assert (buf->get_pin_count () == 0);
            }
        }
    }
  return error_code;
}


int packing_stream::attach_buffers (std::vector <buffer_context> &buffered_ranges)
{
  int i;
  int error_code = NO_ERROR;
  
  for (i = 0; i < buffered_ranges.size(); i++)
    {
      packing_stream_buffer *buf = buffered_ranges[i].mapped_buffer;

      error_code = add_buffer_context (buf, READ_STREAM, buffered_ranges[i].first_pos, buffered_ranges[i].last_pos,
                                       0, NULL);
      if (error_code != NO_ERROR)
        {
          return error_code;
        }
      m_append_position = MAX (m_append_position, buffered_ranges[i].last_pos);
    }

  return error_code;
}

BUFFER_UNIT * packing_stream::fetch_data_from_provider (stream_provider *context_provider, BUFFER_UNIT *ptr,
                                                        const size_t &amount)
{
  int err = NO_ERROR;
  err = context_provider->fetch_data (ptr, amount);
  if (err != NO_ERROR)
    {
      return NULL;
    }

  m_read_position += amount;

  return ptr;
}

BUFFER_UNIT * packing_stream::get_more_data_with_buffer (const size_t amount, const stream_provider *context_provider,
                                                         buffer_context **granted_range)
{
  int i;
  int err = NO_ERROR;
  packing_stream_buffer *new_buffer = NULL;
  stream_provider *curr_stream_provider;
  BUFFER_UNIT *ptr;

  if (m_read_position + amount <= m_append_position)
    {
      /* already fetched */
      for (i = 0; i < m_buffered_ranges.size(); i++)
        {
          if (m_buffered_ranges[i].is_range_mapped (m_read_position, amount))
            {
              *granted_range = &(m_buffered_ranges[i]);
              ptr = m_buffered_ranges[i].mapped_buffer->get_buffer () +  m_read_position - m_buffered_ranges[i].first_pos;

              m_read_position += amount;
              return ptr;
            }
        }

      /* TODO : fetched, but not in memory anymore */
      return NULL;
    }

  /* TODO : should decide which provider to choose based on amount to reserve ? */
  curr_stream_provider = (context_provider != NULL) ? (stream_provider *)context_provider : m_stream_provider;

  /* this is my current position in stream */
  stream_position start_pos = reserve_no_buffer (amount);

  /* check if any buffer covers the current range */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (m_buffered_ranges[i].is_range_contiguously_mapped (start_pos, amount))
        {
          *granted_range = &(m_buffered_ranges[i]);
          ptr = m_buffered_ranges[i].extend_range (amount);

          return fetch_data_from_provider (curr_stream_provider, ptr, amount);
        }
      else if (m_buffered_ranges[i].last_allocated_pos == start_pos)
        {
          BUFFER_UNIT * ptr = m_buffered_ranges[i].mapped_buffer->reserve (amount);
          size_t buffer_start_offset = ptr - m_buffered_ranges[i].mapped_buffer->get_buffer ();

          err = add_buffer_context (m_buffered_ranges[i].mapped_buffer, WRITE_STREAM, start_pos, start_pos + amount,
                                    buffer_start_offset, granted_range);
          if (err != NO_ERROR)
            {
              return NULL;
            }
          return fetch_data_from_provider (curr_stream_provider, ptr, amount);
        }
    }

  if (m_total_buffered_size + amount > trigger_flush_to_disk_size)
    {
      curr_stream_provider->flush_old_stream_data ();
    }
  
  ptr = acquire_new_write_buffer (curr_stream_provider, start_pos, amount, granted_range);

  return fetch_data_from_provider (curr_stream_provider, ptr, amount);
}

BUFFER_UNIT * packing_stream::get_data_from_pos (const stream_position &req_start_pos, const size_t amount,
                                                 const stream_provider *context_provider,
                                                 buffer_context **granted_range)
{
  int i;
  int err = NO_ERROR;
  packing_stream_buffer *new_buffer = NULL;
  stream_provider *curr_stream_provider;
  BUFFER_UNIT *ptr;

  if (req_start_pos + amount <= m_read_position)
    {
      /* already fetched */
      for (i = 0; i < m_buffered_ranges.size(); i++)
        {
          if (m_buffered_ranges[i].is_range_mapped (req_start_pos, amount))
            {
              *granted_range = &(m_buffered_ranges[i]);
              ptr = m_buffered_ranges[i].mapped_buffer->get_buffer () +  req_start_pos - m_buffered_ranges[i].first_pos;

              return ptr;
            }
        }

      /* TODO : fetched, but not in memory anymore */
      return NULL;
    }

  /* TODO : should decide which provider to choose based on amount to reserve ? */
  curr_stream_provider = (context_provider != NULL) ? (stream_provider *)context_provider : m_stream_provider;

  /* this is my current position in stream */
  stream_position start_pos = reserve_no_buffer (amount);

  /* check if any buffer covers the current range */
  for (i = 0; i < m_buffered_ranges.size(); i++)
    {
      if (m_buffered_ranges[i].is_range_contiguously_mapped (start_pos, amount))
        {
          *granted_range = &(m_buffered_ranges[i]);
          ptr = m_buffered_ranges[i].extend_range (amount);

          return fetch_data_from_provider (curr_stream_provider, ptr, amount);
        }
      else if (m_buffered_ranges[i].last_allocated_pos == start_pos)
        {
          BUFFER_UNIT * ptr = m_buffered_ranges[i].mapped_buffer->reserve (amount);
          size_t buffer_start_offset = ptr - m_buffered_ranges[i].mapped_buffer->get_buffer ();

          err = add_buffer_context (m_buffered_ranges[i].mapped_buffer, WRITE_STREAM, start_pos, start_pos + amount,
                                    buffer_start_offset, granted_range);
          if (err != NO_ERROR)
            {
              return NULL;
            }
          return fetch_data_from_provider (curr_stream_provider, ptr, amount);
        }
    }

  if (m_total_buffered_size + amount > trigger_flush_to_disk_size)
    {
      curr_stream_provider->flush_old_stream_data ();
    }
  
  ptr = acquire_new_write_buffer (curr_stream_provider, start_pos, amount, granted_range);

  return fetch_data_from_provider (curr_stream_provider, ptr, amount);
}
