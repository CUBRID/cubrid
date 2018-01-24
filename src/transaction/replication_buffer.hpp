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

#include "dbtype.h"

class serial_buffer
{
public:
  const enum memory_order SERIAL_BUFF_MEMORY_ORDER = memory_order_relaxed;

  serial_buffer (const size_t req_capacity) = 0;

  int init (const size_t req_capacity) = 0;

  virtual unsigned char * reserve (const size_t amount) = 0;
  virtual int add (const char *ptr, const size_t size) = 0;
  virtual int read (char *ptr, const size_t read_pos, const size_t size) = 0;
  virtual int check_space(const unsigned char *ptr, const size_t amount) = 0;

  const char * get_buffer (void) { return storage; }


private:
  size_t capacity;

  unsigned char *storage;
  std::atomic <char *> curr_append_ptr;
  unsigned char *end_ptr;
  std::atomic <char *> curr_read_ptr;
};

class replication_buffer : public serial_buffer
{
public:
  replication_buffer (const size_t req_capacity);

  int init (const size_t req_capacity);
  unsigned char *get_curr_append_pos ();

  unsigned char * reserve (size_t amount);
  int add (const char *ptr, size_t size);
  int read (char *ptr, size_t read_pos, size_t size);
  int check_space (const unsigned char *ptr, size_t amount);
};


#endif /* _REPLICATION_BUFFER_HPP_ */
