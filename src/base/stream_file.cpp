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


#include <cstdio>
#include <cassert>
#if defined (WINDOWS)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include "stream_file.hpp"
#include "error_manager.h"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "thread_looper.hpp"
#include "porting.h"

namespace cubstream
{

  const int stream_file::FILE_CREATE_FLAG = O_CREAT;

  /* daemon task for writing stream contents to stream file
   * The stream_file object has a m_target_flush_position; this is the target set by the stream
   * and is the position which desired to be saved to file (all data up to this position)
   * When 'execute' method starts will begin writting all data from 'last_dropped_pos" to m_target_flush_position
   * last_dropped_pos is retrieved from stream by get_last_dropable_pos()
   * To prevent contention on stream, a small buffer is used to read from stream into the buffer
   * and then save to stream_file the contents of buffer
   * while the loop in the execute is running the m_target_flush_position may be updated by the stream:
   * in such case, the execution is continued until the updated target is achieved.
   */
  class writer_daemon_task : public cubthread::entry_task
  {
    public:
      writer_daemon_task (multi_thread_stream &stream_arg)
	: m_stream (stream_arg),
        m_stream_file (*stream_arg.get_stream_file ())
      {
        m_copy_to_buffer_func = std::bind (&writer_daemon_task::copy_to_buffer, std::ref (*this),
				           std::placeholders::_1, std::placeholders::_2);
      }

      void execute (cubthread::entry &thread_ref) override
      {
        int err = NO_ERROR;

        m_stream_file.wait_flush_signal ();

        assert (m_stream.get_last_committed_pos () >= m_stream_file.get_flush_target_position ());

        stream_position curr_pos = m_stream.get_last_dropable_pos ();
        stream_position end_pos;

        while (1)
          {
            end_pos = m_stream_file.get_flush_target_position ();
            if (curr_pos >= end_pos || m_stream_file.is_stopped ())
              {
                break;
              }

            size_t amount_to_copy = MIN (end_pos - curr_pos, BUFFER_SIZE);

            err = m_stream.read (curr_pos, amount_to_copy, m_copy_to_buffer_func);
            if (err != NO_ERROR)
              {
                /* TODO: fatal error ?  */
                break; 
              }
            err = m_stream_file.write (curr_pos, m_buffer, amount_to_copy);
            if (err != NO_ERROR)
              {
                /* TODO: fatal error ?  */
                break; 
              }

            curr_pos += amount_to_copy;

            m_stream.set_last_dropable_pos (curr_pos);
          }
      }

      int copy_to_buffer (char *ptr, const size_t byte_count)
      {
	memcpy (m_buffer, ptr, byte_count);
	return (int) byte_count;
      };

    private:
      /* buffer of 256K */
      static const int BUFFER_SIZE = 256 * 1024;

      multi_thread_stream &m_stream;
      stream_file &m_stream_file;

      char m_buffer[BUFFER_SIZE];

      cubstream::stream::read_func_t m_copy_to_buffer_func;
  };


void stream_file::init (const size_t file_size, const int print_digits)
{
  m_stream.set_stream_file (this);

  m_base_filename = m_stream.name ();
  m_desired_file_size = file_size;
  m_filename_digits_seqno = print_digits;

  m_curr_append_position = 0;
  m_curr_append_file_size = 0;

  m_target_flush_position = 0;

  m_start_file_seqno = 0;

  m_filled_stream_handler = std::bind (&stream_file::stream_filled_func, std::ref (*this), std::placeholders::_1,
                                       std::placeholders::_2);
  m_stream.set_filled_stream_handler (m_filled_stream_handler);

#if defined (SERVER_MODE)
  m_write_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
			new writer_daemon_task (m_stream),
			"replication_stream_flush");
#endif
}

void stream_file::finalize (void)
{
  std::map<int,int>::iterator it;

  m_is_stopped = true;
  start_flush (0);

  cubthread::get_manager ()->destroy_daemon (m_write_daemon);

  for (it = m_file_descriptors.begin (); it != m_file_descriptors.end (); it ++)
    {
      assert (it->second > 0);
      close (it->second);
    }
  m_file_descriptors.clear ();
}

int stream_file::get_file_desc_from_file_seqno (const int file_seqno)
{
  std::map<int,int>::iterator it;

  it = m_file_descriptors.find (file_seqno);
  if (it != m_file_descriptors.end ())
    {
      return it->second;
    }

  return -1;
}

int stream_file::get_file_seqno_from_stream_pos (const stream_position &pos)
{
  stream_position start_available_pos;
  int file_seqno;

  start_available_pos = m_start_file_seqno * m_desired_file_size;

  if (pos < start_available_pos)
    {
      /* not on disk anymore */
      return -1;
    }

  file_seqno = (int) (pos / m_desired_file_size);

  return file_seqno;
}

int stream_file::get_file_seqno_from_stream_pos_ext (const stream_position &pos, size_t &amount, size_t &file_offset)
{
  int file_seqno = get_file_seqno_from_stream_pos (pos);
  
  file_offset = pos - file_seqno * m_desired_file_size;

  amount = (file_seqno + 1) * m_desired_file_size - pos;

  return file_seqno;
}

/*
 * create_files_in_range
 *
 * this is an utility function to create "zeroed" content (including missing files) for a range 
 * of stream positions; it should not be needed in normal usage since writting should be done by appending
 * previously written range
 *
 */
int stream_file::create_files_in_range (const stream_position &start_pos, const stream_position &end_pos)
{
  stream_position curr_pos;
  size_t file_offset, hole_amount, available_amount_in_file;
  int file_seqno;
  int err = NO_ERROR;
  const size_t BUFFER_SIZE = 4 * 1024;
  char zero_buffer[BUFFER_SIZE] = { 0 };

  curr_pos = start_pos;
  hole_amount = start_pos - m_curr_append_position;

  if (hole_amount == 0)
    {
      /* append in a new file(s) : files are created as empty (without filling with zero) */
      while (curr_pos < end_pos)
        {
          file_seqno = get_file_seqno_from_stream_pos_ext (curr_pos, available_amount_in_file, file_offset);
          if (file_seqno < 0)
            {
              return ER_FAILED;
            }

          /* file_offset == 0 ==> this is a new file (we didn't written in it)
           * file_offset > 0  ==> file is already created, make sure it may be opened */
          int dummy_fd = open_file_seqno (file_seqno, (file_offset == 0) ? FILE_CREATE_FLAG : 0);
          if (dummy_fd < 0)
            {
              return ER_FAILED;
            }

          /* advance to next file */
          curr_pos += available_amount_in_file;
        }

      return err;
    }

  /* create files and fill them with zero : for range: m_curr_append_position < start_pos */
  curr_pos = m_curr_append_position;
  while (hole_amount > 0)
    {
      file_seqno = get_file_seqno_from_stream_pos_ext (curr_pos, available_amount_in_file, file_offset);
      if (file_seqno < 0)
        {
          return ER_FAILED;
        }

      int dummy_fd = open_file_seqno (file_seqno, (file_offset == 0) ? FILE_CREATE_FLAG : 0);
      if (dummy_fd < 0)
        {
          return ER_FAILED;
        }

      size_t rem_amount_this_file = MIN (hole_amount, available_amount_in_file);

      while (rem_amount_this_file > 0)
        {
          size_t amount_to_write = MIN (BUFFER_SIZE, rem_amount_this_file);
          size_t written_bytes = write_buffer (file_seqno, file_offset, zero_buffer, amount_to_write);
          if (written_bytes != amount_to_write)
            {
              return ER_FAILED;
            }
          rem_amount_this_file -= amount_to_write;
        }

      hole_amount -= rem_amount_this_file;
      curr_pos += rem_amount_this_file;
    }

  return NO_ERROR;
}

/*
 * drop_files_to_pos
 *
 * physically removes files exclusively used by range 0 -> drop_pos
 * a file containing data after drop_pos is not removed
 *
 */
int stream_file::drop_files_to_pos (const stream_position &drop_pos)
{
  stream_position curr_pos;
  int err = NO_ERROR;

  curr_pos = 0;

  while (curr_pos < drop_pos)
    {
      size_t file_offset;
      size_t amount_to_end_file;

      int file_seqno = get_file_seqno_from_stream_pos_ext (curr_pos, amount_to_end_file, file_offset);

      if (file_offset == 0 && amount_to_end_file <= drop_pos - curr_pos)
        {
          err = close_file_seqno (file_seqno, REMOVE_PHYSICAL_FILE);
          if (err != NO_ERROR)
            {
              return err;
            }
        }
      curr_pos += amount_to_end_file;
    }

  return err;
}

int stream_file::get_filename_with_position (char *filename, const size_t max_filename, const stream_position &pos)
{
  int file_seqno;

  file_seqno = get_file_seqno_from_stream_pos (pos);
  if (file_seqno > 0)
    {
      return get_filename_with_file_seqno (filename, max_filename, file_seqno);
    }

  return ER_FAILED;
}

int stream_file::get_filename_with_file_seqno (char *filename, const size_t max_filename, const int file_seqno)
{
  assert (file_seqno >= 0);

  snprintf (filename, max_filename, "%s%c%s_%0*d", 
            m_base_path.c_str (),
            PATH_SEPARATOR,
            m_base_filename.c_str (),
            m_filename_digits_seqno,
            file_seqno);
  return NO_ERROR;
}

int stream_file::open_file_seqno (const int file_seqno, int flags)
{
  int fd;
  char file_name [PATH_MAX];
  int err = NO_ERROR;
  
  auto it = m_file_descriptors.find (file_seqno);

  if (it != m_file_descriptors.end ())
    {
      assert ((flags & FILE_CREATE_FLAG) == 0);
      return it->second;
    }

  get_filename_with_file_seqno (file_name, sizeof (file_name) - 1, file_seqno);

  fd = open_file (file_name, flags);
  if (fd < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_VOL, 2, file_name, "");
      assert ((flags & FILE_CREATE_FLAG) != FILE_CREATE_FLAG);
      return -1;
    }

  m_file_descriptors[file_seqno] = fd;

  return fd;
}

/*
 * close_file_seqno
 *
 * Closes the file descriptor associated to a file sequence
 * if remove_physical flag is set it also physical removes the file
 * 
 */
int stream_file::close_file_seqno (int file_seqno, bool remove_physical)
{
  char file_name[PATH_MAX];
  auto it = m_file_descriptors.find (file_seqno);
  int fd = -1;
  int err = NO_ERROR;

  if (it != m_file_descriptors.end ())
    {
      fd = it->second;
    }
  
  if (fd == -1)
    {
      /* already closed */
      if (remove_physical)
        {
          fd = open_file_seqno (file_seqno);
          if (fd < 0)
            {
              ASSERT_ERROR_AND_SET (err);
              return err;
            }

         assert (fd != -1);
        }
    }

  if (fd == -1)
    {
      /* already closed, nothing to do */
      assert (remove_physical == false);
      return err;
    }

  if (remove_physical)
    {
      get_filename_with_file_seqno (file_name, sizeof (file_name) - 1, file_seqno);
    }

  assert (it != m_file_descriptors.end ());
  m_file_descriptors.erase (it);

  if (close (fd) != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME, 1, file_name);
      err = ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME;
      return err;
    }

  if (remove_physical)
    {
      if (::remove (file_name) != 0)
        {
          er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME, 1, file_name);
          err = ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME;
          return err;
        }
    }

  return err;
}

int stream_file::open_file (const char *file_path, int flags)
{
  int fd;

#if defined (WINDOWS)
  if ((flags & FILE_CREATE_FLAG) == FILE_CREATE_FLAG)
    {
      fd = _sopen (file_path, O_RDWR | O_CREAT | O_BINARY, _SH_DENYWR, 0600);
    }
  else
    {
      fd = open (file_path, O_RDWR | O_BINARY | flags);
    }
#else
  fd = open (file_path, O_RDWR | flags);
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

size_t stream_file::read_buffer (const int file_seqno, const size_t file_offset, const char *buf, const size_t amount)
{
  size_t actual_read;
  int fd;

  fd = open_file_seqno (file_seqno);

  if (fd <= 0)
    {
      return 0;
    }

#if defined (WINDOWS)
  /* TODO : use Windows API for paralel reads */
  lseek (fd, (long) file_offset, SEEK_SET);
  actual_read = read (fd, buf, amount);
#else
  actual_read = pread (fd, (void *) buf, amount, file_offset);
#endif

  return actual_read;
}

size_t stream_file::write_buffer (const int file_seqno, const size_t file_offset, const char *buf, const size_t amount)
{
  size_t actual_write;
  int fd;

  fd = open_file_seqno (file_seqno);

  if (fd <= 0)
    {
      return 0;
    }

#if defined (WINDOWS)  
  lseek (fd, (long ) file_offset, SEEK_SET);
  actual_write = ::write (fd, buf, amount);
#else
  actual_write = pwrite (fd, buf, amount, file_offset);
#endif

  return actual_write;
}

/* 
 * write
 *
 * writes the contents of buf of size amount into the stream file starting from position pos
 *
 */
int stream_file::write (const stream_position &pos, const char *buf, const size_t amount)
{
  stream_position curr_pos;
  size_t file_offset, rem_amount, available_amount_in_file;
  int file_seqno;
  int err = NO_ERROR;

  curr_pos = pos;
  rem_amount = amount;

  if (curr_pos + amount > m_curr_append_position)
    {
      err = create_files_in_range (curr_pos, curr_pos + amount);
      if (err != NO_ERROR)
        {
          return err;
        }
    }

  while (rem_amount > 0)
    {
      size_t current_to_write;
      size_t actual_write;

      file_seqno = get_file_seqno_from_stream_pos_ext (curr_pos, available_amount_in_file, file_offset);
      if (file_seqno < 0)
        {
          /* TODO[arnia] : not found */
          return ER_FAILED;
        }

      current_to_write = MIN (available_amount_in_file, rem_amount);

      actual_write = write_buffer (file_seqno, file_offset, buf, current_to_write);
      if (actual_write < current_to_write)
        {
          return ER_FAILED;
        }

      rem_amount -= actual_write;
      curr_pos += current_to_write;
      buf += current_to_write;
    }

  m_curr_append_position += amount;

  return NO_ERROR;
}

/* 
 * read
 *
 * reads into buffer buf an amount of size amount from the stream file starting from position pos
 * return : error code
 *
 */
int stream_file::read (const stream_position &pos, const char *buf, const size_t amount)
{
  stream_position curr_pos;
  size_t available_amount_in_file, file_offset, rem_amount;
  int file_seqno;
  int err = NO_ERROR;

  curr_pos = pos;
  rem_amount = amount;

  while (rem_amount > 0)
    {
      size_t current_to_read;
      size_t actual_read;

      file_seqno = get_file_seqno_from_stream_pos_ext (curr_pos, available_amount_in_file, file_offset);
      if (file_seqno < 0)
        {
          /* TODO[arnia] : not found */
          return ER_FAILED;
        }

      current_to_read = MIN (available_amount_in_file, rem_amount);

      actual_read = read_buffer (file_seqno, file_offset, buf, current_to_read);
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

int stream_file::stream_filled_func (const stream_position &last_saved_pos, const size_t available_to_save)
{
  start_flush (last_saved_pos + available_to_save);
  return NO_ERROR;
}

} /* namespace cubstream */
