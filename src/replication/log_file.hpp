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
 * log_file.hpp
 */

#ident "$Id$"

#ifndef _LOG_FILE_HPP_
#define _LOG_FILE_HPP_

#include "stream_common.hpp"
#include "buffer_provider.hpp"
#include "packing_stream.hpp"
#include <vector>

typedef size_t file_pos_t;

class replication_entry;
class cubstream::stream_buffer;
class log_file;
class cubstream::entry;


enum
{
  CURRENT_POSITION = -1
};

struct file_range
{
  file_pos_t start_pos;
  file_pos_t end_pos;

  file_range () { start_pos = -1; end_pos = -1; }
};

class file_cache : public cubpacking::pinner
{
private:
  cubstream::stream_buffer *buffer;
  file_range cached_range;
  log_file *owner;

  char *storage;

public:
  const size_t FILE_CACHE_ONE_BUFFFER_SIZE = 16 * 1024;

  file_cache() { buffer = NULL; owner = NULL; storage = new char[FILE_CACHE_ONE_BUFFFER_SIZE]; };

  cubstream::stream_buffer *get_buffer (void) { return buffer; };
  log_file *get_owner (void) { return owner; };

  char *get_storage (void) { return storage; };

  int release (void);

  bool is_in_cache (const file_pos_t start_pos, const size_t count);

};

class log_file : public cubstream::buffer_provider
{
private:
  cubstream::packing_stream *stream;

  file_pos_t curr_append_position;
  file_pos_t curr_read_position;

  std::vector<file_cache> caches;

  int fd;

public:
  const int MAX_FILE_CACHES = 32;

  log_file () { fd = -1; };
  log_file (const char *file_path);

  file_cache *new_cache (void);
  file_cache *get_cache (void);

  int open_file (const char *file_path);

  int read_no_cache (char *storage, const size_t count, file_pos_t start_pos = CURRENT_POSITION);

  int write_buffer (cubstream::stream_buffer *buffer);

  static char *get_filename (const cubstream::stream_position &start_position);


  int fetch_data (char *ptr, const size_t &amount);
  
  int extend_buffer (cubstream::stream_buffer **existing_buffer, const size_t &amount);

  int flush_old_stream_data (void);

  cubstream::packing_stream * get_write_stream (void);

};

#endif /* _LOG_FILE_HPP_ */
