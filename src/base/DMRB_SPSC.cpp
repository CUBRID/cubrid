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

/*
 * DMRB_SPSC.cpp
 */

#include "DMRB_SPSC.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace cubbase
{
  DMRB_SPSC::DMRB_SPSC (std::size_t capacity) :
    DMRB (capacity)
  {
  }

  DMRB_SPSC::~DMRB_SPSC ()
  {
  }

  cubbase::span<std::byte> DMRB_SPSC::reserve (std::size_t length)
  {
    std::uint64_t head, tail;
    std::byte *ptr;

    head = m_head.load (std::memory_order_relaxed);
    tail = m_tail.load (std::memory_order_acquire);
    if (length > m_size - (head - tail))
      {
	return { nullptr, 0 };
      }

    ptr = static_cast<std::byte *> (m_base) + (head & m_mask);
    return { ptr, length };
  }

  void DMRB_SPSC::commit (std::size_t length)
  {
    m_head.fetch_add (length, std::memory_order_release);
  }

  void DMRB_SPSC::consume (std::size_t length)
  {
    m_tail.fetch_add (length, std::memory_order_release);
  }

  cubbase::span<const std::byte> DMRB_SPSC::peek () const
  {
    std::uint64_t tail, head;
    std::size_t length;
    const std::byte *ptr;

    tail = m_tail.load (std::memory_order_relaxed);
    head = m_head.load (std::memory_order_acquire);
    length  = head - tail;
    if (length == 0)
      {
	return { nullptr, 0 };
      }

    ptr = static_cast<const std::byte *> (m_base) + (tail & m_mask);
    return { ptr, length };
  }
}

