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
 * stream_file.cpp
 */

#ident "$Id$"


#include "stream_file.hpp"
#include "packing_stream.hpp"
#include "stream_buffer.hpp"
#if defined (LINUX)
#include <sys/stat.h>
#include <fcntl.h>
#endif

namespace cubreplication
{

stream_file::stream_file (const char *file_path)
{
  if (open_file (file_path) != NO_ERROR)
    {
      /* TODO[arnia] : error */
      throw ("Cannot open replication file");
    }
}

void stream_file::init (const std::string& base_name, const size_t file_size = DEFAULT_FILE_SIZE,
                        const int print_digits = DEFAULT_FILENAME_DIGITS)
{
  m_base_filename = base_name;
  m_desired_file_size = file_size;
  m_filename_digits_seqno = print_digits;

  m_start_file_seqno = 1;
  m_curr_file_seqno = 1;
}

int stream_file::get_seqno_from_stream_pos (const cubstream::stream_position pos)
{
  for (auto it:m_file_end_positions)
    {
      if (pos < *it)
        {
          return m_start_file_seqno + it - m_file_end_positions.begin ();
        }
    }

  return -1;
}

int stream_file::get_filename (char *filename, const size_t max_filename, const cubstream::stream_position pos)
{
  int seqno;
  std::string filename;

  seqno = get_seqno_from_stream_pos (pos);
  if (seqno > 0)
    {
      snprintf (filename, max_filename, "%s_%*d", m_base_filename, m_filename_digits_seqno, seqno);
      return NO_ERROR;
    }
  return ER_FAILED;
}

int stream_file::open_file (const char *file_path)
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

int stream_file::write_buffer (cubstream::stream_buffer *buffer)
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

int stream_file::read_no_cache (char *storage, const size_t count, file_pos_t start_pos)
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

int stream_file::fetch_data (char *ptr, const size_t &amount)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}
  
int stream_file::extend_buffer (cubstream::stream_buffer **existing_buffer, const size_t &amount)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}

int stream_file::flush_old_stream_data (void)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}

cubstream::packing_stream * stream_file::get_write_stream (void)
{
  NOT_IMPLEMENTED ();

  return NO_ERROR;
}

} /* namespace cubreplication */
