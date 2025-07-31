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
 * DMRB.cpp
 */

#include "DMRB.hpp"
#include "error_manager.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace cubbase
{
  DMRB::DMRB (std::size_t capacity) :
      m_size (capacity),
      m_mask (capacity - 1)
    {
      std::string name;
      long page;

      page = sysconf (_SC_PAGESIZE);
      if ((m_size & m_mask) != 0 || (m_size % page) != 0)
	{
	  assert_release (false);
	}

      /* make virtual descriptor */
      name = generate_unique_name ();
      m_fd = ::shm_open (name.c_str (), O_RDWR | O_CREAT | O_EXCL, 0600);
      if (m_fd < 0)
	{
	  assert_release (false);
	}
      if (::shm_unlink (name.c_str ()) < 0)
	{
	  assert_release (false);
	}
      if (::ftruncate (m_fd, m_size))
	{
	  assert_release (false);
	}

      /* reserve address space */
      m_base = ::mmap (nullptr, m_size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (m_base == MAP_FAILED)
	{
	  assert_release (false);
	}
      /* map virtual address to physical memory */
      if (::mmap (m_base, m_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, m_fd, 0) == MAP_FAILED ||
	  ::mmap (static_cast<char *> (m_base) + m_size, m_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, m_fd, 0) == MAP_FAILED)
	{
	  assert_release (false);
	}

      if (::madvise (m_base, m_size * 2, MADV_DONTFORK) < 0)
	{
	  assert_release (false);
	}
    }

  DMRB::~DMRB ()
    {
      ::munmap (m_base, m_size * 2);
      ::close (m_fd);
    }

  std::size_t DMRB::capacity () const noexcept
    {
      return m_size;
    }

  std::size_t DMRB::available () const noexcept
    {
      std::uint64_t head, tail;

      head = m_head.load (std::memory_order_acquire);
      tail = m_tail.load (std::memory_order_acquire);
      return static_cast<std::size_t> (head - tail);
    }

  bool DMRB::empty () const noexcept
    {
      std::uint64_t head, tail;

      head = m_head.load (std::memory_order_acquire);
      tail = m_tail.load (std::memory_order_acquire);
      return head == tail;
    }

  std::string DMRB::generate_unique_name ()
    {
      static std::atomic<std::uint32_t> counter {0};

      return "/cubbase_dmrb_" + std::to_string (getpid ()) + "_" + std::to_string (counter++);
    }
}

