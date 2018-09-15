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
 * stream_file.hpp
 */

#ident "$Id$"

#ifndef _STREAM_FILE_HPP_
#define _STREAM_FILE_HPP_

#include "multi_thread_stream.hpp"
#include <map>


namespace cubthread
  {
    class daemon;
  }

namespace cubstream
{

typedef enum
{
  WRITE_MODE_DATA = 0,
  WRITE_MODE_CREATE
} WRITE_MODE;

/* 
 * class for handling reading/writing to files from a stream 
 * stream file consists of several physical files on disk;
 * there is a base file name (considered the name of stream), suffixed by a sequence number, generated incrementally
 * For instance: $CUBRID_DATABASES/<dbname>/replica_0000, replica_0001, ... replica_0123
 * each file has a configured fixed size
 * The file having the filename of base is reserved - (maybe metainformation file ?)
 *
 * Restrictions and usage:
 * - a range of data is written only all previous ranges are written (we don't allow holes in written data)
 * - active range is considered between a dropped position and append position; the active range is contiguous
 * - read range must entirely be inside the active range
 *
 * TODOs:
 * - resizing of physical files
 */
class stream_file
{
private:
  static const int DEFAULT_FILENAME_DIGITS = 4;
  static const bool REMOVE_PHYSICAL_FILE = true;
  
  /* 100 MBytes */
  static const size_t DEFAULT_FILE_SIZE = 100 * 1024 * 1024;

  multi_thread_stream &m_stream;

  /* largest stream position written to file */
  stream_position m_curr_append_position;

  /* TODO : size of last physical file - do we need it ? it may be computed from curr_append_position */
  size_t m_curr_append_file_size;

  /* mapping of file sequence to OS file descriptor */
  std::map<int, int> m_file_descriptors;

  /* stream file is split into several physical files
   * this is the desired size of such physical file;
   * a new file should be created when the current file (the one which is appended into) size approaches this limit 
   * or already exceeded it;
   * the purpose is to avoid splitting stream entries among stream files
   */
  size_t m_desired_file_size;

  /* base name of stream files; */
  std::string m_base_filename;

  /* path for stream files location */
  std::string m_base_path;

  /* number of digits in the sequence number suffixing the base name 
   * TODO : this should be used only as a nice priting of names, we should allow increasing sequence number
   * even if exceeds the digits in filename */
  int m_filename_digits_seqno;

  /* oldest avaiable file (old file may be removed to save disk space) */
  int m_start_file_seqno;

  stream_position m_target_flush_position;

  cubstream::stream::notify_func_t m_filled_stream_handler;

  cubthread::daemon *m_write_daemon;
  std::mutex m_flush_mutex;
  std::condition_variable m_flush_cv;

  bool m_is_stopped;

  static const int FILE_CREATE_FLAG;

protected:
  int get_file_desc_from_file_seqno (const int file_seqno);
  int get_file_seqno_from_stream_pos (const stream_position &pos);
  int get_file_seqno_from_stream_pos_ext (const stream_position &pos, size_t &amount, size_t &file_offset);

  int create_files_in_range (const stream_position &start_pos, const stream_position &end_pos);

  int get_filename_with_position (char *filename, const size_t max_filename, const stream_position &pos);
  int get_filename_with_file_seqno (char *filename, const size_t max_filename, const int file_seqno);

  int open_file_seqno (const int file_seqno, int flags = 0);
  int close_file_seqno (int file_seqno, bool remove_physical = false);

  int open_file (const char *file_path, int flags = 0);

  int create_file (const char *file_path);

  size_t read_buffer (const int file_seqno, const size_t file_offset, char *buf, const size_t amount);
  size_t write_buffer (const int file_seqno, const size_t file_offset, const char *buf, const size_t amount);

  int stream_filled_func (const stream_position &last_saved_pos, const size_t available_to_save);

public:
  stream_file (multi_thread_stream &stream_arg, const size_t file_size = DEFAULT_FILE_SIZE,
              const int print_digits = DEFAULT_FILENAME_DIGITS)
    : m_stream (stream_arg) 
    { init (file_size, print_digits); };

  ~stream_file () { finalize (); };

  void init (const size_t file_size = DEFAULT_FILE_SIZE,
             const int print_digits = DEFAULT_FILENAME_DIGITS);

  void set_path (const std::string &path)
    {
      m_base_path = path;
    }

  void finalize ();

  int write (const stream_position &pos, const char *buf, const size_t amount);

  int read (const stream_position &pos, char *buf, const size_t amount);

  int drop_files_to_pos (const stream_position &drop_pos);

  size_t get_desired_file_size (void) { return m_desired_file_size; }

  size_t get_max_available_from_pos (const stream_position &pos)
    {
      if (m_curr_append_position > pos)
        {
          return m_curr_append_position - pos;
        }
      else
        {
          return 0;
        }
    }

  stream_position get_flush_target_position (void)
    {
      std::unique_lock<std::mutex> ulock (m_flush_mutex);
      return m_target_flush_position;
    }

  void start_flush (const stream_position &target_position)
  {
    std::unique_lock<std::mutex> ulock (m_flush_mutex);
    if (target_position != 0)
      {
        m_target_flush_position = target_position;
      }
    m_flush_cv.notify_one ();
  }

  void wait_flush_signal (void)
  {
    std::unique_lock<std::mutex> ulock (m_flush_mutex);
    m_flush_cv.wait (ulock);
  }

  bool is_stopped (void)
  {
    return m_is_stopped;
  }
};

} /*  namespace cubstream */

#endif /* _STREAM_FILE_HPP_ */
