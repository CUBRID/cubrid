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
#if 0//Affix Allocator: allocate(sizeof(Prefix) + size + sizeof(Suffix))
- usefull for debug, statistics, information, ...
    - use Prefix & Suffix as fences to detect wrong writes: "BBBBBBBB"..."EEEEEEEE"
    - use Prefix & Suffix for human readable text to ease memory reading: "type=MyClass"..."end of type=MyClass"
    - use Prefix & Suffix to store information & statistics (creation timestamp, source code file & line, access count, ...)
USAGE:
#endif
#pragma once
#include <new>
#include "allocator_blk.hpp"
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif

namespace allocator
{
  template<typename Allocator, typename Prefix, typename Suffix> class affix
  {
  private:
    static const size_t m_pfxLen = sizeof (Prefix);
    static const size_t m_sfxLen = sizeof (Suffix);
    Allocator& m_a;

  public:
    affix (Allocator& a)
      : m_a (a)
    {
    }

    blk allocate (size_t size)
    {
      blk b = m_a.allocate (m_pfxLen + size + m_sfxLen);
      if (!b)
        return {0, 0};
      new (b.ptr) Prefix;                  //placement new to initialize Prefix memory
      new (b.ptr + m_pfxLen + size) Suffix;//placement new to initialize Suffix memory
      return {size, b.ptr + m_pfxLen};
    }

    void deallocate (blk b)
    {
      //check if Prefix & Suffix are unchanged!
      //...
      _a.deallocate ({m_pfxLen + b.dim + m_sfxLen, b.ptr - m_pfxLen});
    }

    unsigned check (blk b)
    {
      Prefix pfx;
      Suffix sfx;
      int err = 0;
      err |= (memcmp (&pfx, b.ptr - m_pfxLen, m_pfxLen)) ? 1 : 0;
      err |= (memcmp (&sfx, b.ptr + b.dim, m_sfxLen)) ? 2 : 0;
      return err;
    }
  };
}// namespace allocator

#if 0//using inheritance
template<typename Allocator, typename Prefix, typename Suffix=void> class AffixAllocator
    : Allocator
{
public:
    void* allocate(size_t size){
        void* ptr = Allocator::allocate(sizeof(Prefix) + size + sizeof(Suffix));
        new(ptr) Prefix;                    //placement new to initialize Prefix memory 
        new(ptr+sizeof(Prefix)+size) Suffix;//placement new to initialize Suffix memory 
        return ptr;
    }

    void deallocate(void* ptr, size_t size){
        //check if Prefix & Suffix are unchanged!
        //...
        Allocator::deallocate(sizeof(Prefix) + size + sizeof(Suffix));
    }
};
#endif
