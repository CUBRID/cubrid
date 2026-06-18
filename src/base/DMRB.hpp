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
 * DMRB.hpp
 */

#ifndef _DOUBLE_MAPPED_RING_BUFFER_HPP_
#define _DOUBLE_MAPPED_RING_BUFFER_HPP_

#ident "$Id$"

#include "span.hpp"
#include "error_manager.h"

#include <atomic>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace cubbase
{
  /* double mapped ring buffer */
  template <bool ThreadSafe>
  class DMRB
  {
    protected:
      using type_t = std::conditional_t<ThreadSafe, std::atomic<std::uint64_t>, std::uint64_t>;

      static inline std::uint64_t value_load (const type_t &value, std::memory_order order = std::memory_order_release)
      {
	if constexpr (ThreadSafe)
	  {
	    return value.load (order);
	  }
	else
	  {
	    return value;
	  }
      }

      static inline void value_store (type_t &value, std::uint64_t n, std::memory_order order = std::memory_order_release)
      {
	if constexpr (ThreadSafe)
	  {
	    value.store (n, order);
	  }
	else
	  {
	    value = n;
	  }
      }

      static inline void value_add (type_t &value, std::uint64_t n, std::memory_order order = std::memory_order_release)
      {
	if constexpr (ThreadSafe)
	  {
	    value.fetch_add (n, order);
	  }
	else
	  {
	    value += n;
	  }
      }

    public:
      DMRB (std::size_t capacity);
      DMRB ();
      virtual ~DMRB ();

      DMRB (const DMRB &other) = delete;
      DMRB &operator= (const DMRB &other) = delete;
      DMRB (DMRB &&other) = delete;
      DMRB &operator= (DMRB &&other) = delete;

      bool prepare ();

      std::size_t capacity () const noexcept;
      std::size_t available () const noexcept;
      std::size_t readable () const noexcept;
      bool empty () const noexcept;

      /* provider */
      virtual cubbase::span<std::byte> reserve (std::size_t length) = 0;
      virtual void commit (std::size_t length) = 0;

      /* consumer */
      virtual void consume (std::size_t length) = 0;

      virtual cubbase::span<const std::byte> peek () const = 0;

    private:
      std::string generate_unique_name ();

    protected:
      alignas (ThreadSafe ? 64 : alignof (std::uint64_t)) type_t m_head { 0 };
      alignas (ThreadSafe ? 64 : alignof (std::uint64_t)) type_t m_tail { 0 };

      alignas (ThreadSafe ? 64 : alignof (std::uint64_t))
      void *m_base;
      int m_fd;
      std::size_t m_size;
      std::size_t m_mask;
  };

  template <bool T>
  DMRB<T>::DMRB (std::size_t capacity) :
    m_base (nullptr),
    m_fd (-1),
    m_size (capacity),
    m_mask (capacity - 1)
  {
    assert (capacity > 0);
  }

  template <bool T>
  DMRB<T>::DMRB () :
    m_base (nullptr),
    m_fd (-1),
    m_size (0),
    m_mask (0)
  {
  }

  template <bool T>
  DMRB<T>::~DMRB ()
  {
    if (m_base)
      {
	::munmap (m_base, m_size * 2);
      }
    if (m_fd >= 0)
      {
	::close (m_fd);
      }
  }

  template <bool T>
  bool DMRB<T>::prepare ()
  {
    std::string name;
    long page;

    assert (m_fd < 0 && m_base == nullptr);
    assert (m_size != 0 && m_mask != 0);

    page = sysconf (_SC_PAGESIZE);
    if (m_size % page != 0)
      {
	return false;
      }

    /* make virtual descriptor */
    name = generate_unique_name ();
    m_fd = ::shm_open (name.c_str (), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (m_fd < 0)
      {
	_er_log_debug (ARG_FILE_LINE, "shm_open failed: %s.\n", strerror (errno));
	return false;
      }
    if (::shm_unlink (name.c_str ()) < 0)
      {
	_er_log_debug (ARG_FILE_LINE, "shm_unlink failed: %s.\n", strerror (errno));
	return false;
      }
    if (::ftruncate (m_fd, m_size))
      {
	_er_log_debug (ARG_FILE_LINE, "ftruncate failed: %s.\n", strerror (errno));
	return false;
      }

    /* reserve address space */
    /* TODO: change this to NUMA or MUST make first touch on epoll group core */
    m_base = ::mmap (nullptr, m_size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m_base == MAP_FAILED)
      {
	_er_log_debug (ARG_FILE_LINE, "mmap failed: %s.\n", strerror (errno));

	m_base = nullptr;
	return false;
      }
    /* map virtual address to physical memory */
    if (::mmap (m_base, m_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, m_fd, 0) == MAP_FAILED ||
	::mmap (static_cast<char *> (m_base) + m_size, m_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, m_fd,
		0) == MAP_FAILED)
      {
	_er_log_debug (ARG_FILE_LINE, "mmap failed: %s.\n", strerror (errno));
	return false;
      }

    if (::madvise (m_base, m_size * 2, MADV_DONTFORK) < 0)
      {
	_er_log_debug (ARG_FILE_LINE, "madvise failed: %s.\n", strerror (errno));
	return false;
      }

    return true;
  }

  template <bool T>
  std::size_t DMRB<T>::capacity () const noexcept
  {
    return m_size;
  }

  template <bool T>
  std::size_t DMRB<T>::available () const noexcept
  {
    std::uint64_t head, tail;

    head = value_load (m_head, std::memory_order_acquire);
    tail = value_load (m_tail, std::memory_order_acquire);

    assert (head - tail <= m_size);

    return m_size - static_cast<std::size_t> (head - tail);
  }

  template <bool T>
  std::size_t DMRB<T>::readable () const noexcept
  {
    std::uint64_t head, tail;

    head = value_load (m_head, std::memory_order_acquire);
    tail = value_load (m_tail, std::memory_order_acquire);

    assert (head - tail <= m_size);

    return static_cast<std::size_t> (head - tail);
  }

  template <bool T>
  bool DMRB<T>::empty () const noexcept
  {
    std::uint64_t head, tail;

    head = value_load (m_head, std::memory_order_acquire);
    tail = value_load (m_tail, std::memory_order_acquire);
    return head == tail;
  }

  template <bool T>
  std::string DMRB<T>::generate_unique_name ()
  {
    static std::atomic<std::uint32_t> counter { 0 };

    return "/cubbase_dmrb_" + std::to_string (getpid ()) + "_" + std::to_string (counter++);
  }
}

#endif
