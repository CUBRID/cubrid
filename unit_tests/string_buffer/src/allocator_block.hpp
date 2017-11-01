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

/* Memory Block information managed by allocators
 * - it is what allocate() returns and what deallocate() expects
 * - keeps the pointer and size physically together (logically they are together)
 * - allows better & cleaner design
 * Usage:
 *   block b = some_allocator.allocate(1024);
 *   if(b.is_valid())
 *     snprintf(b.ptr, b.dim, "format...");
 *   some_allocator.resize(b, 2048);
 *   if(b.is_valid())
 *     snprintf(b.ptr, b.dim, "something bigger...");
 *   some_allocator.deallocate(b);
 */

#ifndef _ALLOCATOR_BLOCK_HPP_
#define _ALLOCATOR_BLOCK_HPP_

#if defined (LINUX)
#include <stddef.h> //size_t on Linux
#endif /* LINUX */

namespace allocator
{
  struct block
  {
    size_t dim; //size of the memory block pointed by ptr
    char *ptr;  //pointer to a memory block

    block (size_t dim = 0, void *ptr = 0)
      : dim (dim)
      , ptr ((char *) ptr)
    {
    }

    bool is_valid ()
    {
      return (dim != 0 && ptr != 0);
    }

    friend bool operator== (block b0, block b1)
    {
      return (b0.dim == b1.dim && b0.ptr == b1.ptr);
    }

    friend bool operator!= (block b0, block b1)
    {
      return (b0.dim != b1.dim || b0.ptr != b1.ptr);
    }
  };
} // namespace allocator

#endif /* _ALLOCATOR_BLOCK_HPP_ */
