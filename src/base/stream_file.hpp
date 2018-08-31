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

#include "stream_io.hpp"
#include "multi_thread_stream.hpp"
#include <map>


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
 * For instance: $CUBRID/logs/replica_0000, replica_0001, ... replica_0123
 * each file has a configured fixed size
 * The file having the filename of base has special purpose (maybe metainformation file ?)
 * 
 */
class stream_file : public stream_io
{
private:
  static const int DEFAULT_FILENAME_DIGITS = 4;
  
  /* 100 MBytes */
  static const size_t DEFAULT_FILE_SIZE = 100 * 1024 * 1024;


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

  /* number of digits in the sequence number suffixing the base name 
   * TODO : this should be used only as a nice priting of names, we should allow increasing sequence number
   * even if exceeds the digits in filename */
  int m_filename_digits_seqno;

  /* oldest avaiable file (old file may be removed to save disk space) */
  int m_start_file_seqno;

  /* last/current file */
  int m_curr_file_seqno;

protected:
  int get_file_desc_from_file_seqno (const int file_seqno);
  int get_file_seqno_from_stream_pos (const stream_position &pos);
  int get_file_seqno_from_stream_pos_ext (const stream_position &pos, size_t &amount, size_t &file_offset);

  int create_files_to_pos (const stream_position &pos);

  int get_filename_with_position (char *filename, const size_t max_filename, const stream_position &pos);
  int get_filename_with_file_seqno (char *filename, const size_t max_filename, const int file_seqno);

  int open_file_seqno (const int file_seqno);

  int open_file (const char *file_path);

  int create_file (const char *file_path);

  size_t read_buffer (const int file_seqno, const size_t file_offset, const char *buf, const size_t amount);
  size_t write_buffer (const int file_seqno, const size_t file_offset, const char *buf, const size_t amount);

  int write_internal (const stream_position &pos, const char *buf, const size_t amount, const WRITE_MODE wr_mode);

public:
  stream_file (const std::string& base_name) { init (base_name); };

  ~stream_file () { finalize (); };

  void init (const std::string& base_name, const size_t file_size = DEFAULT_FILE_SIZE,
             const int print_digits = DEFAULT_FILENAME_DIGITS);

  void finalize ();

  int write (const stream_position &pos, const char *buf, const size_t amount);

  int read (const stream_position &pos, const char *buf, const size_t amount);
};

} /*  namespace cubstream */

#endif /* _STREAM_FILE_HPP_ */
