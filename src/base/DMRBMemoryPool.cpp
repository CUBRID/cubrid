/*
 *
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

/*
 * DMRBMemoryPool.cpp
 */

#include "DMRBMemoryPool.hpp"

#include <cstring>
#include <cstddef>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  DMRBMemoryPool::DMRBMemoryPool (std::size_t capacity) :
    DMRB<false> (capacity)
  {
  }

  DMRBMemoryPool::DMRBMemoryPool () :
    DMRB<false> ()
  {
  }

  DMRBMemoryPool::~DMRBMemoryPool ()
  {
  }

  void DMRBMemoryPool::reset ()
  {
    m_head = 0;
    m_tail = 0;
    m_free.clear ();
  }

  cubbase::span<std::byte> DMRBMemoryPool::buffer ()
  {
    std::size_t length;
    std::byte *ptr;

    length = m_size - (m_head - m_tail);
    ptr = static_cast<std::byte *> (m_base) + (m_head & m_mask);
    return { ptr, length };
  }

  void DMRBMemoryPool::restore (cubbase::span<std::byte> &mem)
  {
    std::uint64_t order, size;
    std::map<std::uint64_t, std::uint64_t>::iterator it, prev;

    order = m_tail + ((m_size - (m_tail & m_mask) + ((reinterpret_cast<std::uintptr_t> (mem.data ()) -
		       reinterpret_cast<std::uintptr_t> (m_base)) & m_mask)) & m_mask);
    size = mem.size ();

#if !defined (NDEBUG)
    it = m_free.lower_bound (order);
    if (it != m_free.begin ())
      {
	prev = std::prev (it);
	assert (prev->first + prev->second <= order);
      }
    if (it != m_free.end ())
      {
	assert (order + size <= it->first);
      }
#endif

    if (order != m_tail)
      {
	m_free.emplace (order, size);
	return ;
      }

    assert (order == m_tail);

    for (it = m_free.begin (); it != m_free.end (); )
      {
	if (it->first != order + size)
	  {
	    break;
	  }
	size += it->second;
	it = m_free.erase (it);
      }

    this->consume (size);
  }

  bool DMRBMemoryPool::is_in (cubbase::span<std::byte> &mem)
  {
    std::uintptr_t base, addr;

    base = reinterpret_cast<std::uintptr_t> (m_base);
    addr = reinterpret_cast<std::uintptr_t> (mem.data ());

    return (addr >= base && addr < base + m_size * 2);
  }

  bool DMRBMemoryPool::is_in (std::byte *ptr)
  {
    std::uintptr_t base, addr;

    base = reinterpret_cast<std::uintptr_t> (m_base);
    addr = reinterpret_cast<std::uintptr_t> (ptr);

    return (addr >= base && addr < base + m_size * 2);
  }

  cubbase::span<std::byte> DMRBMemoryPool::reserve (std::size_t length)
  {
    assert_release (false);
    return { nullptr, 0 };
  }

  void DMRBMemoryPool::commit (std::size_t length)
  {
    assert (m_tail <= m_head);
    assert ((m_head - m_tail) + length <= m_size);
    this->m_head += length;
  }

  void DMRBMemoryPool::consume (std::size_t length)
  {
    assert (m_tail <= m_head);
    assert (length <= (m_head - m_tail));
    this->m_tail += length;
  }

  cubbase::span<const std::byte> DMRBMemoryPool::peek () const
  {
    assert_release (false);
    return { nullptr, 0 };
  }
}

