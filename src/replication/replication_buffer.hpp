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
 * replication_buffer.hpp
 */

#ident "$Id$"

#ifndef _REPLICATION_BUFFER_HPP_
#define _REPLICATION_BUFFER_HPP_

#include <atomic>
#include <list>
#include "dbtype.h"


class file_cache;
class pinnable;

/*
 * This should serve as storage for streams
 * Each buffer has a storage_producer (the one which decides when to create or scrap a buffer)
 * log_generator uses it to add replication entries
 *   - in such case, log_generator should be  responsible for triggering memory allocation
 * Also, it should be used as storage for decoding replication entries, either from file or network buffers
 *   - in this case, the log_consumer is providing new content
 */
class serial_buffer : public pinnable
{
public:
  const enum std::memory_order SERIAL_BUFF_MEMORY_ORDER = std::memory_order_relaxed;

  serial_buffer (const size_t req_capacity = 0) { mapped_cache = NULL; storage = NULL; };

  virtual int init (const size_t req_capacity) = 0;

  virtual BUFFER_UNIT * reserve (const size_t amount) = 0;
  virtual BUFFER_UNIT * get_curr_append_ptr () = 0;
  // obsolete :virtual int add (BUFFER_UNIT *ptr, const size_t size) = 0;
  // obsolete :virtual int read (const BUFFER_UNIT *ptr, const size_t read_pos, const size_t size) = 0;
  // obsolete :virtual int check_space (const BUFFER_UNIT *ptr, const size_t amount) = 0;

  const BUFFER_UNIT * get_buffer (void) { return storage; }

  const BUFFER_UNIT * get_curr_append_ptr (void) { return storage + curr_append_pos; }

  size_t get_buffer_size (void) { return end_ptr - storage; }

  /* mapping methods : a memory already exists, just instruct buffer to use it */
  int map_buffer (BUFFER_UNIT *ptr, const size_t count);
  int map_buffer_with_pin (serial_buffer *ref_buffer, pinner *referencer);


protected:
  size_t capacity;

  BUFFER_UNIT *storage;
  std::atomic_size_t curr_append_pos;
  BUFFER_UNIT *end_ptr;
  std::atomic_size_t curr_read_pos;

  file_cache *mapped_cache;
};

class replication_buffer : public serial_buffer
{
public:
  replication_buffer (const size_t req_capacity);

  ~replication_buffer (void);

  int init (const size_t req_capacity);
  BUFFER_UNIT *get_curr_append_ptr () { return (storage + curr_append_pos); }

  // obsolete : BUFFER_UNIT * reserve (size_t amount);
  // obsolete : int check_space (const BUFFER_UNIT *ptr, size_t amount);
  // obsolete : int add (const BUFFER_UNIT *ptr, size_t size);
  // obsolete : int read (BUFFER_UNIT *ptr, size_t read_pos, size_t size);
};


#endif /* _REPLICATION_BUFFER_HPP_ */
