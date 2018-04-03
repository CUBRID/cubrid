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

#include "stream_common.hpp"
#include "buffer_provider.hpp"
#include "packing_stream.hpp"
#include <vector>

class cubstream::stream_buffer;
class cubstream::entry;

namespace cubreplication
{

typedef size_t file_pos_t;

enum
{
  CURRENT_POSITION = -1
};

class stream_file;

class file_reader : public cubstream::partial_read_handler
{
private:
  stream_file *m_file;

public:

  int read_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count, size_t *processed_bytes);
};

class file_writer : public cubstream::write_handler
{
private:
  stream_file *m_file;

public:

  int write_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count);
};


/* 
 * class for handling reading/writing to files from a stream 
 * stream file consists of several physical files on disk;
 * there is a base file name (considered the name of stream), suffixed by a sequence number, generated incrementally
 * For instance: $CUBRID/logs/replica_0001, ... replica_0123
 * We reserve 0000 seqno for special purpose (maybe metainformation file ?)
 * 
 */
class stream_file : public cubstream::buffer_provider
{
private:
  cubstream::packing_stream *stream;

  file_pos_t curr_append_position;
  file_pos_t curr_read_position;

  int fd;

  /* stream file is split into several physical files
   * this is the desired size of such physical file;
   * a new file should be created when the current file (the one which is appended into) size approaches this limit 
   * or already exceeded it;
   * the purpose is to avoid splittin stream entries among stream files
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

  /* end positions of files, the positions are relevant for available files (m_start_file_seqno) */
  std::map<cubstream::stream_position> m_file_end_positions;

private:
  int get_seqno_from_stream_pos (const cubstream::stream_position pos);

  int get_filename (char *filename, const size_t max_filename, const cubstream::stream_position pos);
public:
  static const int DEFAULT_FILENAME_DIGITS = 4;
  
  /* 100 MBytes */
  static const size_t DEFAULT_FILE_SIZE = 100 * 1024 * 1024;  

  stream_file (const std::string& base_name) { init (base_name); };

  void init (const std::string& base_name, const size_t file_size = DEFAULT_FILE_SIZE,
             const int print_digits = DEFAULT_FILENAME_DIGITS);


  int open_file (const char *file_path);

  int read_no_cache (char *storage, const size_t count, file_pos_t start_pos = CURRENT_POSITION);

  int write_buffer (cubstream::stream_buffer *buffer);

  int fetch_data (char *ptr, const size_t &amount);
  
  int extend_buffer (cubstream::stream_buffer **existing_buffer, const size_t &amount);

  int flush_old_stream_data (void);

  cubstream::packing_stream * get_write_stream (void);

};

} /*  namespace cubreplication */

#endif /* _STREAM_FILE_HPP_ */
