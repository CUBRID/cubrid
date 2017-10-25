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
#if 0// Stack Allocator
- allocate sequentially from a memory buffer
- can deallocate only the last allocated block
- usually used with a stack allocated buffer => no sync needed
- constant allocation/dealocation time: O(1)
#endif
#pragma once
#include "Al_Blk.hpp"
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif

namespace Al
{
  class StackAllocator
  {
  private:
    char* m_ptr;//pointer to a contiguous memory block
    char* m_0;  //begin of the available memory (inside given block)
    char* m_1;  //end of available memory (=_ptr+SIZE)
  public:
    friend class StackAllocator_Test;//test class can access internals of this class

    StackAllocator(void* buf, size_t size)
      : m_ptr((char*)buf)
      , m_0(m_ptr)
      , m_1(m_ptr + size)
    {
    }

    ~StackAllocator()
    {//can be called explicitly to reinitialize: a.~StackAllocator();
      m_0 = m_ptr;
    }

    void operator()(void* buf, size_t size)
    {
      m_ptr = (char*)buf;
      m_0   = m_ptr;
      m_1   = m_ptr + size;
    }

    Blk allocate(size_t size)
    {
      //round to next aligned?! natural allignment
      //...
      if(m_0 + size > m_1)
        {//not enough free memory available
          return {0, 0};
        }
      return {size, (m_0 += size, m_0 - size)};
    }

    void deallocate(Blk blk)
    {
      //assert(owns(blk));
      if((char*)blk.ptr + blk.dim == m_0)
        {//last allocated block
          m_0 = blk.ptr;
        }
    }

    bool owns(Blk blk)
    {// test if blk in [_0, _1)
      return (m_ptr <= blk.ptr && blk.ptr + blk.dim <= m_1);
    }

    size_t available() { return (m_1 - m_0); }

    Blk realloc(Blk blk, size_t size)
    {//fit additional size bytes; extend block if possible
      if(blk.ptr + blk.dim == m_0 && blk.ptr + blk.dim + size <= m_1)
        {//last allocated block & enough space to extend
          blk.dim += size;
          m_0 += size;
          return blk;
        }
      return {0, 0};
    }
  };
}// namespace Al
