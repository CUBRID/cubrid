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
 * DMRB_SPSC.hpp
 */

#ifndef _DMRB_SPSC_HPP_
#define _DMRB_SPSC_HPP_

#ident "$Id$"

#include "DMRB.hpp"

#include <cstring>
#include <cstddef>

namespace cubbase
{
  /* single producer single consumer */
  template <bool ThreadSafe>
  class DMRB_SPSC : public DMRB<ThreadSafe>
  {
    public:
      DMRB_SPSC (std::size_t capacity);
      ~DMRB_SPSC ();

      cubbase::span<std::byte> reserve (std::size_t length);
      cubbase::span<std::byte> reserve_all ();
      void commit (std::size_t length);

      void consume (std::size_t length);

      cubbase::span<const std::byte> peek () const;
  };

  template <bool T>
  DMRB_SPSC<T>::DMRB_SPSC (std::size_t capacity) :
    DMRB<T> (capacity)
  {
  }

  template <bool T>
  DMRB_SPSC<T>::~DMRB_SPSC ()
  {
  }

  template <bool T>
  cubbase::span<std::byte> DMRB_SPSC<T>::reserve (std::size_t length)
  {
    std::uint64_t head, tail;
    std::byte *ptr;

    head = DMRB<T>::value_load (this->m_head, std::memory_order_relaxed);
    tail = DMRB<T>::value_load (this->m_tail, std::memory_order_acquire);

    if (length > this->m_size - (head - tail))
      {
	return { nullptr, 0 };
      }

    ptr = static_cast<std::byte *> (this->m_base) + (head & this->m_mask);
    return { ptr, length };
  }

  template <bool T>
  cubbase::span<std::byte> DMRB_SPSC<T>::reserve_all ()
  {
    std::uint64_t head, tail;
    std::size_t length;
    std::byte *ptr;

    head = DMRB<T>::value_load (this->m_head, std::memory_order_relaxed);
    tail = DMRB<T>::value_load (this->m_tail, std::memory_order_acquire);

    length = this->m_size - (head - tail);
    ptr = static_cast<std::byte *> (this->m_base) + (head & this->m_mask);
    return { ptr, length };
  }

  template <bool T>
  void DMRB_SPSC<T>::commit (std::size_t length)
  {
    DMRB<T>::value_add (this->m_head, length, std::memory_order_release);
  }

  template <bool T>
  void DMRB_SPSC<T>::consume (std::size_t length)
  {
    DMRB<T>::value_add (this->m_tail, length, std::memory_order_release);
  }

  template <bool T>
  cubbase::span<const std::byte> DMRB_SPSC<T>::peek () const
  {
    std::uint64_t tail, head;
    std::size_t length;
    const std::byte *ptr;

    tail = DMRB<T>::value_load (this->m_tail, std::memory_order_relaxed);
    head = DMRB<T>::value_load (this->m_head, std::memory_order_acquire);
    length = head - tail;
    if (length == 0)
      {
	return { nullptr, 0 };
      }

    assert (length <= this->m_size);

    ptr = static_cast<const std::byte *> (this->m_base) + (tail & this->m_mask);
    return { ptr, length };
  }
}

#endif
