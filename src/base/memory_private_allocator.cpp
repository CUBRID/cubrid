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
// memory_private_allocator.hpp - extension to memory_alloc.h private allocation
//

#include "memory_private_allocator.hpp"

namespace mem
{
  void
  private_block_allocate (block &b, size_t size)
  {
    if (b.ptr == NULL || b.dim == 0)
      {
	b.ptr = (char *) db_private_alloc (NULL, size);
	b.dim = size;
      }
    else if (size <= b.dim)
      {
	// no change
      }
    else
      {
	char *new_ptr = (char *) db_private_realloc (NULL, b.ptr, size);
	if (new_ptr != NULL)
	  {
	    b.ptr = new_ptr;
	    b.dim = size;
	  }
	else
	  {
	    assert (false);
	  }
      }
  }

  void
  private_block_deallocate (block &b)
  {
    if (b.ptr != NULL)
      {
	db_private_free (NULL, b.ptr);
      }
    b.ptr = NULL;
    b.dim = 0;
  }

  const block_allocator PRIVATE_BLOCK_ALLOCATOR { private_block_allocate, private_block_deallocate };
} // namespace mem
