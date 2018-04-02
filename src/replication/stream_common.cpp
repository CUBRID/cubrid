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
 * stream_common.cpp
 */

#ident "$Id$"

#include "stream_common.hpp"
#include "stream_buffer.hpp"

namespace cubstream
{

bool buffer_context::is_range_mapped (const stream_position &start, const size_t &amount)
{
  return (start >= first_pos && start + amount <= last_pos) ? true : false;
}

size_t buffer_context::get_mapped_amount (const stream_position &start)
{
  if (start >= first_pos && start < last_pos)
    {
      return last_pos - start;
    }
  return 0;
}

bool buffer_context::is_range_contiguously_mapped (const stream_position &start, const size_t &amount)
{
  return (start == last_pos && start + amount < last_allocated_pos) ? true : false;
}

char * buffer_context::extend_range (const size_t &amount)
{
  char * ptr;

  ptr = mapped_buffer->get_buffer () + last_pos - first_pos;
  last_pos += amount;

  return ptr;
}

} /* namespace cubstream */
