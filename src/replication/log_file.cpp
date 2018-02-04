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
 * log_file.cpp
 */

#ident "$Id$"


#include "log_file.hpp"

bool file_cache::is_in_cache (const file_post_t start_pos, const size_t count)
{
  if (start_pos >= cached_range.start_pos && file_post_t + count <= cached_range.end_pos)
    {
      return true;
    }

  return false;
}

int file_cache::release (void)
{
  serial_buffer *buffer;
  buffer = get_buffer ();
  if (buffer != NULL && buffer->get_pin_count () == 0)
    {
      buffer = NULL;
      cached_range.end_pos = -1;
      cached_range.start_pos = -1;
      return NO_ERROR;
    }

  return ER_FAILED;
}
///////////////////////////////////////////////////////


int stream_entry::pack (serial_buffer *buffer)
{
  buffer->reserve (get_header_size ());
  buffer->
}

///////////////////////////////////////////////////////

log_file::log_file (const char *file_path)
{
  if (open_file (file_path) != NO_ERROR)
    {
      /* TODO[arnia] : error */
      throw ("Cannot open replication file");
    }
}

int log_file::open_file (const char *file_path)
{
  fd = open (file_path, O_RDWR);
  if (fd == -1)
    {
      /* TODO[arnia] : error */
      return ER_FAILED;
    }

  return NO_ERROR;
}

file_cache *log_file::get_cache (void)
{
  /* TODO[arnia] */
  NOT_IMPLEMENTED();
  return &caches[0];
}

file_cache *log_file::new_cache (void)
{
  /* TODO[arnia] */
  file_cache *my_cache;

  my_cache = new new_cache;

  return my_cache;
}


int log_file::append_entry (stream_entry *entry)
{
  file_pos_t buffer_size = entry->buffer->get_buffer_size();
  if (entry->written_bytes >= buffer_size)
    {
      assert (written_bytes == buffer_size);
      return NO_ERROR;
    }

  /* TODO[arnia] : atomic write  */
  /* TODO[arnia] : incremental write not supported yet */
  if (pwrite (fd, entry->buffer->get_buffer(), buffer_size, curr_append_position) != buffer_size)
    {
      /* TODO[arnia] : error */
      return ER_FAILED;
    }
  curr_append_position += buffer_size;

  return NO_ERROR;
}

int log_file::read_no_cache (BUFFER_UNIT *storage, const size_t count, file_pos_t start_pos)
{
  size_t actual_read;

  if (start_pos == CURRENT_POSITION)
    {
      start_pos = curr_read_position;
    }
  
  actual_read = pread (fd, storage, count, start_post);

  if (actual_read < 0)
    {
      /* TODO[arnia] : error */
      return ER_FAILED;
    }

  return actual_read;
}

int log_file::read_with_cache (stream_entry *entry, const size_t count, file_pos_t start_pos)
{
  int i;
  bool found_in_cache = false;

  if (start_pos == CURRENT_POSITION)
    {
      start_pos = curr_read_position;
    }

  /* 1. search all of the caches, if any includes the request range, map it */
  for (i = 0; i < caches.size(); i++)
    {
      if (caches[i].is_in_cache (start_pos, count))
        {
          serial_buffer *cache_buffer = caches[i].get_buffer();
          serial_buffer *entry_buffer = entry->get_buffer ();

          entry_buffer->map_buffer_with_pin (cache_buffer, entry);
          found_in_cache = true;
          break;
        }
    }

  if (!found_in_cache)
    {
      file_cache *my_cache = get_cache ();

      if (read_no_cache (my_cache->get_storage (), count, start_pos) < 0)
        {
          /* TODO[arnia] : error */
          return ER_FAILED;
        }

      my_cache->
    }
  /* 2. adjust buffer starting position to a an entry start */
  NOT_IMPLEMENTED();

  return NO_ERROR;
}

static char *log_file::get_filename (const stream_position &start_position)
{
  /* TODO[arnia]:*/
  NOT_IMPLEMENTED();
  return NULL;
}
