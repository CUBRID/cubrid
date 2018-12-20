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
 */

/*
 * mem_block.cpp - Memory Block Functionality
 */

#include "mem_block.hpp"

#include <functional>

#include <cstring>

namespace mem
{
  void
  standard_alloc (block &b, size_t size)
  {
    if (b.ptr == NULL || b.dim == 0)
      {
	b.ptr = new char[size];
	b.dim = size;
      }
    else if (size <= b.dim)
      {
	// do not reduce
      }
    else
      {
	char *new_ptr = new char[size];
	std::memcpy (new_ptr, b.ptr, b.dim);
	delete[] b.ptr;
	b.ptr = new_ptr;
	b.dim = size;
      }
  }

  void
  standard_dealloc (block &b)
  {
    delete [] b.ptr;
    b.ptr = NULL;
    b.dim = 0;
  }

  const block_allocator STANDARD_BLOCK_ALLOCATOR { standard_alloc, standard_dealloc };

  void
  exponential_standard_alloc (block &b, size_t size)
  {
    if (b.ptr == NULL || b.dim == 0)
      {
	b.ptr = new char[size];
	b.dim = size;
      }
    else if (size <= b.dim)
      {
	// do not reduce
      }
    else
      {
	size_t new_size;
	for (new_size = b.dim; new_size < size; new_size *= 2);
	char *new_ptr = new char[new_size];
	std::memcpy (new_ptr, b.ptr, b.dim);
	delete[] b.ptr;
	b.ptr = new_ptr;
	b.dim = size;
      }
  }

  const block_allocator EXPONENTIAL_STANDARD_BLOCK_ALLOCATOR
  {
    std::bind (exponential_standard_alloc, std::placeholders::_1, std::placeholders::_2),
    std::bind (standard_dealloc, std::placeholders::_1)
  };

  void
  cstyle_alloc (block &b, size_t size)
  {
    if (b.ptr == NULL)
      {
	b.ptr = (char *) malloc (size);
	b.dim = size;
	assert (b.ptr != NULL);
      }
    else
      {
	b.ptr = (char *) realloc (b.ptr, size);
	b.dim = size;
	assert (b.ptr != NULL);
      }
  }

  void
  cstyle_dealloc (block &b)
  {
    if (b.ptr != NULL)
      {
	free (b.ptr);
      }
    b.ptr = NULL;
    b.dim = 0;
  }

  // const block_allocator CSTYLE_BLOCK_ALLOCATOR { cstyle_alloc, cstyle_dealloc };
} // namespace mem
