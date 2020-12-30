/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

//
// parser_allocator.cpp - C++ extensions to parser allocation
//

#include "parser_allocator.hpp"

#include "parser.h"

#include <cstring>

parser_block_allocator::parser_block_allocator (parser_context *parser)
  : cubmem::block_allocator (std::bind (&parser_block_allocator::alloc, this, std::placeholders::_1,
					std::placeholders::_2),
			     std::bind (&parser_block_allocator::dealloc, this, std::placeholders::_1))
  , m_parser (parser)
{
  //
}

void
parser_block_allocator::alloc (cubmem::block &b, size_t size)
{
  if (b.ptr == NULL || b.dim == 0)
    {
      b.ptr = (char *) parser_alloc (m_parser, (int) size);
      b.dim = size;
    }
  else if (size <= b.dim)
    {
      // do nothing
    }
  else
    {
      size_t new_size;

      for (new_size = b.dim; new_size < size; new_size *= 2)
	;

      char *new_ptr = (char *) parser_alloc (m_parser, (int) new_size);
      std::memcpy (new_ptr, b.ptr, b.dim);

      // no freeing
      b.ptr = new_ptr;
      b.dim = new_size;
    }
}

void
parser_block_allocator::dealloc (cubmem::block &b)
{
  // no deallocation
}
