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
#include "dbtype.h"

typedef unsigned char BUFFER_UNIT;

class serial_buffer
{
public:
  const enum std::memory_order SERIAL_BUFF_MEMORY_ORDER = std::memory_order_relaxed;

  serial_buffer (const size_t req_capacity = 0) {};

  virtual ~serial_buffer () = 0;

  virtual int init (const size_t req_capacity) = 0;

  virtual BUFFER_UNIT * reserve (const size_t amount) = 0;
  virtual BUFFER_UNIT * get_curr_append_ptr () = 0;
  virtual int add (BUFFER_UNIT *ptr, const size_t size) = 0;
  virtual int read (const BUFFER_UNIT *ptr, const size_t read_pos, const size_t size) = 0;
  virtual int check_space (const BUFFER_UNIT *ptr, const size_t amount) = 0;

  const BUFFER_UNIT * get_buffer (void) { return storage; }


protected:
  size_t capacity;

  BUFFER_UNIT *storage;
  std::atomic_size_t curr_append_pos;
  BUFFER_UNIT *end_ptr;
  std::atomic_size_t curr_read_pos;
};

class replication_buffer : public serial_buffer
{
public:
  replication_buffer (const size_t req_capacity);

  ~replication_buffer (void);

  int init (const size_t req_capacity);
  BUFFER_UNIT *get_curr_append_ptr () { return (storage + curr_append_pos); }

  BUFFER_UNIT * reserve (size_t amount);
  int add (const BUFFER_UNIT *ptr, size_t size);
  int read (BUFFER_UNIT *ptr, size_t read_pos, size_t size);
  int check_space (const BUFFER_UNIT *ptr, size_t amount);
};


#endif /* _REPLICATION_BUFFER_HPP_ */
