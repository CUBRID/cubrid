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
 * DMRB.hpp
 */

#ifndef _DOUBLE_MAPPED_RING_BUFFER_HPP_
#define _DOUBLE_MAPPED_RING_BUFFER_HPP_

#ident "$Id$"

#include "span.hpp"

#include <atomic>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <cstddef>

namespace cubbase
{
  /* double mapped ring buffer */
  class DMRB
  {
  public:
    DMRB (std::size_t capacity);
    ~DMRB ();

    DMRB (const DMRB &other) = delete;
    DMRB &operator= (const DMRB &other) = delete;
    DMRB (DMRB &&other) = delete;
    DMRB &operator= (DMRB &&other) = delete;

    std::size_t capacity () const noexcept;
    std::size_t available () const noexcept;
    bool empty () const noexcept;

    virtual cubbase::span<std::byte> reserve (std::size_t length) = 0;
    virtual void consume (std::size_t length) = 0;

    virtual cubbase::span<const std::byte> peek () const = 0;

  private:
    std::string generate_unique_name ();

  protected:
    __attribute__((aligned(64))) std::atomic<std::uint64_t> m_head {0};
    __attribute__((aligned(64))) std::atomic<std::uint64_t> m_tail {0};

    __attribute__((aligned(64)))
    void *m_base;
    int m_fd;
    std::size_t m_size;
    std::size_t m_mask;
  };
}

#endif
