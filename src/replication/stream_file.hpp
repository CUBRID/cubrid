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


/* class for handling reading/writing to files from a stream */
class stream_file : public cubstream::buffer_provider
{
private:
  cubstream::packing_stream *stream;

  file_pos_t curr_append_position;
  file_pos_t curr_read_position;

  int fd;

  std::string m_base_filename;

  /* end positions of files */
  std::vector<cubstream::stream_position> m_file_end_positions;

public:
  const int MAX_FILE_CACHES = 32;

  stream_file () { fd = -1; };
  stream_file (const char *file_path);

  int open_file (const char *file_path);

  int read_no_cache (char *storage, const size_t count, file_pos_t start_pos = CURRENT_POSITION);

  int write_buffer (cubstream::stream_buffer *buffer);

  static char *get_filename (const cubstream::stream_position &start_position);

  int fetch_data (char *ptr, const size_t &amount);
  
  int extend_buffer (cubstream::stream_buffer **existing_buffer, const size_t &amount);

  int flush_old_stream_data (void);

  cubstream::packing_stream * get_write_stream (void);

};

} /*  namespace cubreplication */

#endif /* _STREAM_FILE_HPP_ */
