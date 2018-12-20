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

//
// parser_allocator.cpp - C++ extensions to parser allocation
//

#include "parser_allocator.hpp"

#include "parser.h"

parser_block_allocator::parser_block_allocator (parser_context *parser)
  : mem::block_allocator (std::bind (&parser_block_allocator::alloc, this, std::placeholders::_1,
				     std::placeholders::_2),
			  std::bind (&parser_block_allocator::dealloc, this, std::placeholders::_1))
  , m_parser (parser)
{
  //
}

void
parser_block_allocator::alloc (mem::block &b, size_t size)
{
  if (b.ptr == NULL || b.dim == 0)
    {
      b.ptr = (char *) parser_alloc (m_parser, (const int) size);
      b.dim = size;
    }
  else if (size <= b.dim)
    {
      // do nothing
    }
  else
    {
      size_t new_size;
      for (new_size = b.dim; new_size < size; new_size *= 2);
      char *new_ptr = (char *) parser_alloc (m_parser, (const int) new_size);
      std::memcpy (new_ptr, b.ptr, b.dim);
      // no freeing
      b.ptr = new_ptr;
      b.dim = new_size;
    }
}

void
parser_block_allocator::dealloc (mem::block &b)
{
  // no deallocation
}
