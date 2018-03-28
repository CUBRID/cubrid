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

#ifndef _TEST_STREAM_HPP_
#define _TEST_STREAM_HPP_

#include "packable_object.hpp"
#include "packing_stream.hpp"
#include <vector>


namespace test_stream
{

int test_stream1 (void);

class stream_handler_write : public stream_handler
{
public:
  int handling_action (const stream_position pos, BUFFER_UNIT *ptr, const size_t byte_count, size_t *processed_bytes);
};


class stream_handler_read : public stream_handler
{
private:
  size_t m_remaining_to_read;
  BUFFER_UNIT expected_val;
public:
  stream_handler_read () { m_remaining_to_read = 0; };

  int handling_action (const stream_position pos, BUFFER_UNIT *ptr, const size_t byte_count, size_t *processed_bytes);
};

}

#endif /* _TEST_STREAM_HPP_ */
