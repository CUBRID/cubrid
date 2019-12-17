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

/* Stack Allocator: allocates sequentially from a memory buffer
 * - can deallocate only the last allocated block (top of the stack)
 * - usually used with a stack allocated buffer => no sync needed
 * - constant allocation/dealocation time: O(1)
 */

#ifndef _ALLOCATOR_STACK_HPP_
#define _ALLOCATOR_STACK_HPP_

#include "mem_block.hpp"
#include <stddef.h>

namespace allocator
{
  class stack
  {
    private:
      char *m_ptr;   // pointer to a contiguous memory block
      char *m_start; // begin of the available memory (inside given block)
      char *m_stop;  // end of available memory (=_ptr+SIZE)

    public:
      friend class StackAllocator_Test; //test class can access internals of this class

      stack (void *buf, size_t size)
	: m_ptr ((char *) buf)
	, m_start (m_ptr)
	, m_stop (m_ptr + size)
      {
      }

      ~stack () //can be called explicitly to reinitialize: a.~StackAllocator();
      {
	m_start = m_ptr;
      }

      void operator() (void *buf, size_t size)
      {
	m_ptr = (char *) buf;
	m_start = m_ptr;
	m_stop = m_ptr + size;
      }

      cubmem::block allocate (size_t size)
      {
	//round to next aligned?! natural allignment
	//...
	if (m_start + size > m_stop) //not enough free memory available
	  {
	    return {};
	  }
	return cubmem::block{size, (m_start += size, m_start - size)};
      }

      void deallocate (cubmem::block b)
      {
	//assert(owns(blk));
	if ((char *) b.ptr + b.dim == m_start) // last allocated block
	  {
	    m_start = b.ptr;
	  }
      }

      bool owns (cubmem::block b)
      {
	return (m_ptr <= b.ptr && b.ptr + b.dim <= m_stop);
      }

      size_t get_available ()
      {
	return size_t (m_stop - m_start);
      }
#if 0 //bSolo: unused for now and creates problems on gcc 4.4.7
      mem::block realloc (mem::block b, size_t size) //fit additional size bytes; extend block if possible
      {
	if (b.ptr + b.dim == m_start && b.ptr + b.dim + size <= m_stop) //last allocated block & enough space to extend
	  {
	    b.dim += size;
	    m_start += size;
	    return b;
	  }
	return {0, 0};
      }
#endif
  };
} // namespace allocator

#endif /* _ALLOCATOR_STACK_HPP_ */
