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
#include "replication_stream.hpp"
#include "replication_buffer.hpp"
#if defined (LINUX)
#include <sys/stat.h>
#include <fcntl.h>
#endif


bool file_cache::is_in_cache (const file_pos_t start_pos, const size_t count)
{
  if (start_pos >= cached_range.start_pos && start_pos + count <= cached_range.end_pos)
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
#if defined (LINUX)
  fd = open (file_path, O_RDWR);
  if (fd == -1)
    {
      /* TODO[arnia] : error */
      return ER_FAILED;
    }
#else
  NOT_IMPLEMENTED ();
#endif

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

  my_cache = new file_cache;

  return my_cache;
}


int log_file::write_buffer (serial_buffer *buffer)
{
  file_pos_t buffer_size = buffer->get_buffer_size();

  /* TODO[arnia] : atomic write  */
  /* TODO[arnia] : incremental write not supported yet */
#if defined (LINUX)
  if (pwrite (fd, buffer->get_buffer(), buffer_size, curr_append_position) != buffer_size)
    {
      /* TODO[arnia] : error */
      return ER_FAILED;
    }
#else
  NOT_IMPLEMENTED ();
#endif
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

#if defined (LINUX)  
  actual_read = pread (fd, storage, count, start_pos);
#else
  NOT_IMPLEMENTED ();
#endif

  if (actual_read < 0)
    {
      /* TODO[arnia] : error */
      return ER_FAILED;
    }

  return (int) actual_read;
}

int log_file::read_with_cache (BUFFER_UNIT *storage, const size_t count, file_pos_t start_pos)
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

          cache_buffer->map_buffer_with_pin (cache_buffer, this);
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

      NOT_IMPLEMENTED ();
    }
  /* 2. adjust buffer starting position to a an entry start */
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}

char *log_file::get_filename (const stream_position &start_position)
{
  /* TODO[arnia]:*/
  NOT_IMPLEMENTED();
  return NULL;
}

int log_file::fetch_for_read (serial_buffer *existing_buffer, const size_t amount)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}
  
int log_file::extend_for_write (serial_buffer **existing_buffer, const size_t amount)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}

int log_file::flush_ready_stream (void)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}

replication_stream * log_file::get_write_stream (void)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}
