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

void stream_file::init (const std::string& base_name, const size_t file_size, const int print_digits)
{
  m_base_filename = base_name;
  m_desired_file_size = file_size;
  m_filename_digits_seqno = print_digits;

  m_start_file_seqno = 1;
  m_curr_file_seqno = 1;
}

int stream_file::get_file_desc_from_file_id (const int file_id)
{
  std::map<int,int>::iterator it;

  it = file_descriptors.find (file_id);
  if (it != file_descriptors.end ())
    {
      return it->second;
    }

  return -1;
}

int stream_file::get_fileid_from_stream_pos (const cubstream::stream_position &pos)
{
  std::vector<cubstream::stream_position>::iterator it;

  for (it = m_file_end_positions.begin (); it != m_file_end_positions.end (); it ++)
    {
      if (pos < *it)
        {
          return m_start_file_seqno + (int) (it - m_file_end_positions.begin ());
        }
    }

  return -1;
}

int stream_file::get_fileid_from_stream_pos_ext (const cubstream::stream_position &pos, size_t &amount,
                                                 size_t &file_offset)
{
  std::vector<cubstream::stream_position>::iterator it;

  for (it = m_file_end_positions.begin (); it != m_file_end_positions.end (); it ++)
    {
      if (pos < *it)
        {
          amount = *it - pos;
          if (it == m_file_end_positions.begin ())
            {
              file_offset = pos;
            }
          else
            {
              file_offset = pos - *(it - 1);
            }

          return m_start_file_seqno + (int) (it - m_file_end_positions.begin ());
        }
    }

  return -1;
}

int stream_file::create_fileid_to_pos (const cubstream::stream_position &pos)
{

  return NO_ERROR;
}


int stream_file::get_filename_with_position (char *filename, const size_t max_filename,
                                             const cubstream::stream_position &pos)
{
  int file_id;

  file_id = get_fileid_from_stream_pos (pos);
  if (file_id > 0)
    {
      return get_filename_with_fileid (filename, max_filename, file_id);
    }

  return ER_FAILED;
}

int stream_file::get_filename_with_fileid (char *filename, const size_t max_filename, const int file_id)
{
  assert (file_id >= 0);

  snprintf (filename, max_filename, "%s_%*d", m_base_filename.c_str (), m_filename_digits_seqno, file_id);
  return NO_ERROR;
}


int stream_file::open_fileid (const int file_id)
{
  int fd;
  char file_name [PATH_MAX];
  
  get_filename_with_fileid (file_name, sizeof (file_name) - 1, file_id);

  fd = open_file (file_name);
  if (fd < 0)
    {
      return ER_FAILED;
    }

  file_descriptors[file_id] = fd;

  return NO_ERROR;
}

int stream_file::open_file (const char *file_path)
{
  int fd;

#if defined (WINDOWS)
  fd = open (file_path, O_RDWR | O_BINARY);
#else
  fd = open (file_path, O_RDWR);
#endif

  if (fd < 0)
    {
      /* TODO[arnia] : error */
    }

  return fd;
}

int stream_file::create_file (const char *file_path)
{
  int fd;

#if defined (WINDOWS)
  fd = open (file_path, O_RDWR | O_BINARY | O_CREAT | O_EXCL);
#else
  fd = open (file_path, O_RDWR | O_CREAT | O_EXCL);
#endif

  if (fd < 0)
    {
      /* TODO[arnia] : error */
    }

  return fd;
}

size_t stream_file::read_buffer (const int file_id, const size_t file_offset, const char *buf, const size_t amount)
{
  size_t actual_read;
  int fd;

  fd = get_file_desc_from_file_id (file_id);

  if (fd <= 0)
    {
      fd = open_fileid (file_id);
    }

  if (fd <= 0)
    {
      return 0;
    }

#if defined (LINUX)  
  actual_read = pread (fd, buf, amount, file_offset);
#else
   if (lseek (fd, file_offset, SEEK_SET) != file_offset)
     {
        return 0;
     }
   actual_read = read (fd, buf, amount);
#endif

  return actual_read;
}

size_t stream_file::write_buffer (const int file_id, const size_t file_offset, const char *buf, const size_t amount)
{
  size_t actual_write;
  int fd;

  fd = get_file_desc_from_file_id (file_id);

  if (fd <= 0)
    {
      fd = open_fileid (file_id);
    }

  if (fd <= 0)
    {
      return 0;
    }

#if defined (LINUX)  
  actual_write = pwrite (fd, buf, amount, file_offset);
#else
  NOT_IMPLEMENTED ();
#endif

  return actual_write;
}


int stream_file::write (const cubstream::stream_position &pos, const char *buf, const size_t amount)
{
  cubstream::stream_position curr_pos;
  size_t file_offset, rem_amount, available_amount_in_file;
  int file_id;
  int err = NO_ERROR;

  if (pos + amount >= curr_append_position)
    {
      create_fileid_to_pos (pos + amount);
    }

  curr_pos = pos;
  rem_amount = amount;

  while (rem_amount > 0)
    {
      size_t current_to_read;
      size_t actual_read;

      file_id = get_fileid_from_stream_pos_ext (curr_pos, available_amount_in_file, file_offset);
      if (file_id < 0)
        {
          /* TODO[arnia] : not found */
          return ER_FAILED;
        }

      current_to_read = MIN (available_amount_in_file, rem_amount);

      actual_read = read_buffer (file_id, file_offset, buf, current_to_read);
      if (actual_read < current_to_read)
        {
          return ER_FAILED;
        }

      rem_amount -= current_to_read;
      curr_pos += current_to_read;
      buf += current_to_read;
    }


  return NO_ERROR;
}

int stream_file::read (const cubstream::stream_position &pos, const char *buf, const size_t amount)
{
  cubstream::stream_position curr_pos;
  size_t available_amount_in_file, file_offset, rem_amount;
  int file_id;
  int err = NO_ERROR;

  curr_pos = pos;
  rem_amount = amount;

  while (rem_amount > 0)
    {
      size_t current_to_read;
      size_t actual_read;

      file_id = get_fileid_from_stream_pos_ext (curr_pos, available_amount_in_file, file_offset);
      if (file_id < 0)
        {
          /* TODO[arnia] : not found */
          return ER_FAILED;
        }

      current_to_read = MIN (available_amount_in_file, rem_amount);

      actual_read = read_buffer (file_id, file_offset, buf, current_to_read);
      if (actual_read < current_to_read)
        {
          return ER_FAILED;
        }

      rem_amount -= current_to_read;
      curr_pos += current_to_read;
      buf += current_to_read;
    }

  return NO_ERROR;
}


} /* namespace cubreplication */
