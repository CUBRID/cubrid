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
 * mem_block.cpp - Memory Block Functionality
 */

#include "mem_block.hpp"

#include <functional>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmem
{
  void
  standard_alloc (block &b, size_t size)
  {
    if (b.ptr == NULL || b.dim == 0)
      {
	b.ptr = new char[size];
	b.dim = size;
      }
    else if (size <= b.dim)
      {
	// do not reduce
      }
    else
      {
	char *new_ptr = new char[size];
	std::memcpy (new_ptr, b.ptr, b.dim);

	delete[] b.ptr;

	b.ptr = new_ptr;
	b.dim = size;
      }
  }

  void
  standard_dealloc (block &b)
  {
    delete [] b.ptr;
    b.ptr = NULL;
    b.dim = 0;
  }

  const block_allocator STANDARD_BLOCK_ALLOCATOR { standard_alloc, standard_dealloc };

  void
  exponential_standard_alloc (block &b, size_t size)
  {
    if (b.ptr == NULL || b.dim == 0)
      {
	b.ptr = new char[size];
	b.dim = size;
      }
    else if (size <= b.dim)
      {
	// do not reduce
      }
    else
      {
	size_t new_size;

	for (new_size = b.dim; new_size < size; new_size *= 2)
	  ;

	char *new_ptr = new char[new_size];
	std::memcpy (new_ptr, b.ptr, b.dim);

	delete[] b.ptr;

	b.ptr = new_ptr;
	b.dim = new_size;
      }
  }

  const block_allocator EXPONENTIAL_STANDARD_BLOCK_ALLOCATOR
  {
    std::bind (exponential_standard_alloc, std::placeholders::_1, std::placeholders::_2),
    std::bind (standard_dealloc, std::placeholders::_1)
  };

  void
  cstyle_alloc (block &b, size_t size)
  {
    if (b.ptr == NULL)
      {
	b.ptr = (char *) malloc (size);
	b.dim = size;
	assert (b.ptr != NULL);
      }
    else
      {
	b.ptr = (char *) realloc (b.ptr, size);
	b.dim = size;
	assert (b.ptr != NULL);
      }
  }

  void
  cstyle_dealloc (block &b)
  {
    if (b.ptr != NULL)
      {
	free (b.ptr);
      }
    b.ptr = NULL;
    b.dim = 0;
  }

  const block_allocator CSTYLE_BLOCK_ALLOCATOR { cstyle_alloc, cstyle_dealloc };

  //
  // block_allocator
  //
  block_allocator::block_allocator (const alloc_func &alloc_f, const dealloc_func &dealloc_f)
    : m_alloc_f (alloc_f)
    , m_dealloc_f (dealloc_f)
  {
  }

  block_allocator &
  block_allocator::operator= (const block_allocator &other)
  {
    m_alloc_f = other.m_alloc_f;
    m_dealloc_f = other.m_dealloc_f;
    return *this;
  }

  //
  // single_block_allocator
  //

  single_block_allocator::single_block_allocator (const block_allocator &base_alloc)
    : m_base_allocator (base_alloc)
    , m_block {}
    , m_allocator { std::bind (&single_block_allocator::allocate, this, std::placeholders::_1, std::placeholders::_2),
		    std::bind (&single_block_allocator::deallocate, this, std::placeholders::_1) }
  {
  }

  single_block_allocator::~single_block_allocator ()
  {
    m_base_allocator.m_dealloc_f (m_block);
  }

  void
  single_block_allocator::allocate (block &b, size_t size)
  {
    // argument should be uninitialized or should be same as m_block; giving a different block may be a logical error
    assert (b.ptr == NULL || (b.ptr == m_block.ptr && b.dim == m_block.dim));

    m_base_allocator.m_alloc_f (m_block, size);
    b.ptr = m_block.ptr;
    b.dim = m_block.dim;
  }

  void
  single_block_allocator::deallocate (block &b)
  {
    // local block remains
    b.ptr = NULL;
    b.dim = 0;
  }

  const block_allocator &
  single_block_allocator::get_block_allocator () const
  {
    return m_allocator;
  }

  const block &
  single_block_allocator::get_block() const
  {
    return m_block;
  }

  char *
  single_block_allocator::get_ptr () const
  {
    return m_block.ptr;
  }

  size_t
  single_block_allocator::get_size () const
  {
    return m_block.dim;
  }

  void
  single_block_allocator::reserve (size_t size)
  {
    m_base_allocator.m_alloc_f (m_block, size);
  }

} // namespace cubmem
