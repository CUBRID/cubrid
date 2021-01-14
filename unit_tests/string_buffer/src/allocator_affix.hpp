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
