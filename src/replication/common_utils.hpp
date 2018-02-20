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
 * common_utils.hpp
 */

#ident "$Id$"

#ifndef _COMMON_UTILS_HPP_
#define _COMMON_UTILS_HPP_

#include <set>
#include <assert.h>
#include "error_code.h"

#define NOT_IMPLEMENTED() \
  do \
    { \
      throw ("Not implemented"); \
    } \
  while (0)

typedef unsigned char BUFFER_UNIT;
typedef size_t stream_position;

class pinnable;
class replication_stream;
class serial_buffer;

class pinner
{
public:
  int pin (pinnable &reference);
  int unpin (pinnable *reference);

  int unpin_all (void);

  ~pinner () { assert (references.size() == 0); }

private:
  std::set <pinnable*> references;
};

class pinnable
{
public:
  int add_pinner (pinner *referencer) { pinners.insert (referencer); return NO_ERROR; }
  int remove_pinner (pinner *referencer) { pinners.erase (referencer); return NO_ERROR; }
  int get_pin_count (void) { return (int) pinners.size(); }

  ~pinnable () { assert (pinners.size() == 0); }

private:
  std::set <pinner*> pinners;

};


typedef enum stream_mode STREAM_MODE;
enum stream_mode
{
  WRITE_STREAM = 0,
  READ_STREAM
};

class buffered_range
{
public:
  buffered_range() : mapped_buffer (NULL) {};
  stream_position first_pos;
  stream_position last_pos;
  serial_buffer *mapped_buffer;
  size_t written_bytes;
  int is_filled;

  bool operator== (const buffered_range &rhs) const
    {
      return mapped_buffer == rhs.mapped_buffer;
    };

};

class stream_reference
{
public:
  stream_reference() : stream(NULL) {};

  replication_stream *stream;
  size_t start_pos;
  size_t end_pos;
  stream_position stream_start_pos;
  stream_position stream_curr_pos;
};

#endif /* _COMMON_UTILS_HPP_ */
