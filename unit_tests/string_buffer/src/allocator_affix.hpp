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

/* Affix Allocator: allocate(sizeof(Prefix) + size + sizeof(Suffix))
 * usefull for debug, statistics, information, ...
 * use Prefix & Suffix as fences to detect wrong writes: "BBBBBBBB"..."EEEEEEEE"
 * use Prefix & Suffix for human readable text to ease memory reading: "type=MyClass"..."end of type=MyClass"
 * use Prefix & Suffix to store information & statistics (creation timestamp, source code file & line, access count...)
 */
#if !defined(_ALLOCATOR_AFFIX_HPP_)
#define _ALLOCATOR_AFFIX_HPP_

#include "mem_block.hpp"
#include <new>
#include <stddef.h>

namespace allocator
{
  template<typename Allocator, typename Prefix, typename Suffix> class affix
  {
    private:
      static const size_t m_prefix_len = sizeof (Prefix);
      static const size_t m_suffix_len = sizeof (Suffix);
      Allocator &m_a;

    public:
      affix (Allocator &a)
	: m_a (a)
      {
      }

      cubmem::block allocate (size_t size)
      {
	cubmem::block b = m_a.allocate (m_prefix_len + size + m_suffix_len);
	if (!b.is_valid ())
	  return {0, 0};
	new (b.ptr) Prefix;                       //placement new to initialize Prefix memory
	new (b.ptr + m_prefix_len + size) Suffix; //placement new to initialize Suffix memory
	return {size, b.ptr + m_prefix_len};
      }

      void deallocate (cubmem::block b)
      {
	//check if Prefix & Suffix are unchanged!
	//...
	m_a.deallocate ({m_prefix_len + b.dim + m_suffix_len, b.ptr - m_prefix_len});
      }

      unsigned check (const cubmem::block &b)
      {
	Prefix pfx;
	Suffix sfx;
	enum
	{
	  ERR_NONE,
	  ERR_PREFIX = (1 << 0),
	  ERR_SUFFIX = (1 << 1),
	};
	unsigned err = 0;
	if (memcmp (&pfx, b.ptr - m_prefix_len, m_prefix_len) != 0)
	  {
	    err |= ERR_PREFIX;
	  }
	if (memcmp (&sfx, b.ptr + b.dim, m_suffix_len) != 0)
	  {
	    err |= ERR_SUFFIX;
	  }
	return err;
      }
  };
} // namespace allocator
#endif //!defined(_ALLOCATOR_AFFIX_HPP_)
