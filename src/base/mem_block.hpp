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

/*
 * mem_block.hpp - Memory Block Functionality
 *
 *  Memory Block is defined as a contiguous memory buffer.
 *
 *  Glossary:
 *
 *    block - the pair of memory pointer and size.
 *    stack block - a block on stack memory
 *    heap block - a block on heap memory; does not have its own structure (a simple block can be used).
 *    extensible block - a block that can be extended when required
 *    extensible stack block - a block that starts as a stack block and can be extended to a heap block.
 */

#ifndef _MEM_BLOCK_HPP_
#define _MEM_BLOCK_HPP_

#include <memory.h>
#include <functional>

#include <cassert>
#include <cinttypes>
#include <cstring>

namespace cubmem
{
  const size_t DEFAULT_ALIGNMENT = 8;
  template <typename T>
  inline T *ptr_align (T *ptr);

  /*
   * Memory Block
   * - groups together memory address and its size
   * - doesn't own the memory, just points to it
   * - used to allocate, deallocate and share memory
   * - could be extended with helper info: allocator, src file&line where allocation was made, ...
   */
  struct block
  {
    public:
      size_t dim;
      char *ptr;

      inline block ();
      inline block (size_t dim, void *ptr);
      inline block (block &&b);             //move ctor

      inline block &operator= (block &&b);  //move assign

      inline bool is_valid () const;

      inline char *move_ptr ();                                    //NOT RECOMMENDED! use move semantics: std::move()

    private:
      block (const block &) = delete;
      block &operator= (const block &) = delete;
  };

  // stack_block - 8-byte aligned stack block of size S
  //
  template <size_t S>
  class stack_block
  {
    public:
      static const size_t SIZE = S;

      stack_block (void) = default;
      inline char *get_ptr (void);
      inline const char *get_read_ptr () const;

      inline stack_block &operator= (stack_block &&other);

    private:
      union
      {
	char m_buf[SIZE];
	std::int64_t dummy;
      };
  };

  // block_allocator - allocation, deallocation and reallocation of memory blocks. it preserves the contents of the
  //                   block on reallocation
  //
  struct block_allocator
  {
    public:
      using alloc_func = std::function<void (block &b, size_t size)>;
      using dealloc_func = std::function<void (block &b)>;

      alloc_func m_alloc_f;   // allocator/reallocator
      dealloc_func m_dealloc_f;              // deallocator

      block_allocator () = delete;
      block_allocator (const alloc_func &alloc_f, const dealloc_func &dealloc_f);

      block_allocator &operator= (const block_allocator &other);
  };
  extern const block_allocator STANDARD_BLOCK_ALLOCATOR;
  extern const block_allocator EXPONENTIAL_STANDARD_BLOCK_ALLOCATOR;
  extern const block_allocator CSTYLE_BLOCK_ALLOCATOR;

  /* Memory Block - Extensible
   * - able to extend/reallocate to accommodate additional bytes
   * - owns the memory by default and it will free the memory in destructor unless it is moved:
   *    {
   *        cubmem::block_ext block{some_realloc, some_dealloc};//some_realloc/dealloc = functions, functors or lambdas
   *        //...
   *        //move it or it will be deallocated; simple copy => compiler error because it is not designed to be copied
   *        cubmem::block b = std::move(block);
   *    }
   */
  struct extensible_block
  {
    public:
      inline extensible_block ();                                          //default ctor
      inline extensible_block (extensible_block &&b);                              //move ctor
      inline extensible_block (const block_allocator &alloc);     //general ctor
      inline ~extensible_block ();                                         //dtor

      inline extensible_block &operator= (extensible_block &&b);                   //move assignment

      inline void extend_by (size_t additional_bytes);
      inline void extend_to (size_t total_bytes);

      inline char *get_ptr ();
      inline const char *get_read_ptr () const;

      inline std::size_t get_size () const;

      inline char *release_ptr ();

    private:
      block m_block;
      const block_allocator *m_allocator;

      extensible_block (const extensible_block &) = delete;             //copy ctor
      extensible_block &operator= (const extensible_block &) = delete;  //copy assignment
  };

  // extensible_stack_block - extensible memory block that start with as a stack_block
  //
  template <size_t S>
  class extensible_stack_block
  {
    public:
      extensible_stack_block ();
      extensible_stack_block (const block_allocator &alloc);

      inline void extend_by (size_t additional_bytes);
      inline void extend_to (size_t total_bytes);

      inline char *get_ptr ();
      inline const char *get_read_ptr () const;

      inline char *release_ptr ();

      inline extensible_stack_block &operator= (extensible_stack_block &&b);                   //move assignment

    private:
      stack_block<S> m_stack;
      extensible_block m_ext_block;
      bool m_use_stack;
  };
} // namespace cubmem

//////////////////////////////////////////////////////////////////////////
// inline/template implementation
//////////////////////////////////////////////////////////////////////////

namespace cubmem
{
  //
  // alignment
  //
  template <typename T>
  T *
  ptr_align (T *ptr)
  {
    std::uintptr_t pt = (std::uintptr_t) ptr;
    pt = (pt + DEFAULT_ALIGNMENT - 1) & (DEFAULT_ALIGNMENT - 1);
    return (T *) pt;
  }

  //
  // block
  //
  block::block ()
    : dim { 0 }
    , ptr { NULL }
  {
  }

  block::block (block &&b)
    : dim {b.dim}
    , ptr {b.ptr}
  {
    b.dim = 0;
    b.ptr = NULL;
  }

  block::block (size_t dim, void *ptr)
    : dim {dim}
    , ptr { (char *) ptr}
  {
  }

  block &
  block::operator= (block &&b)   //move assign
  {
    if (this != &b)
      {
	dim = b.dim;
	ptr = b.ptr;
	b.dim = 0;
	b.ptr = NULL;
      }
    return *this;
  }

  bool
  block::is_valid () const
  {
    return (dim != 0 && ptr != NULL);
  }

  char *
  block::move_ptr ()
  {
    char *p = ptr;

    dim = 0;
    ptr = NULL;

    return p;
  }

  //
  // stack_block
  //
  template <size_t S>
  char *
  stack_block<S>::get_ptr (void)
  {
    return &m_buf[0];
  }

  template <size_t S>
  const char *
  stack_block<S>::get_read_ptr (void) const
  {
    return &m_buf[0];
  }

  template <size_t S>
  stack_block<S> &
  stack_block<S>::operator= (stack_block &&other)
  {
    std::memcpy (m_buf, other.m_buf, S);
    return *this;
  }

  //
  // extensible_block
  //
  extensible_block::extensible_block ()
    : extensible_block { STANDARD_BLOCK_ALLOCATOR }
  {
  }

  extensible_block::extensible_block (extensible_block &&b)
    : extensible_block { *b.m_allocator }
  {
    b.m_block.dim = 0;
    b.m_block.ptr = NULL;
  }

  extensible_block::extensible_block (const block_allocator &alloc)
    : m_block {}
    , m_allocator (&alloc)
  {
  }

  extensible_block &
  extensible_block::operator= (extensible_block &&b)
  {
    if (this != &b)
      {
	this->~extensible_block ();
	m_allocator = b.m_allocator;
	m_block.dim = b.m_block.dim;
	m_block.ptr = b.m_block.ptr;
	b.m_block.dim = 0;
	b.m_block.ptr = NULL;
      }
    return *this;
  }

  extensible_block::~extensible_block ()
  {
    m_allocator->m_dealloc_f (m_block);
  }

  void
  extensible_block::extend_by (size_t additional_bytes)
  {
    m_allocator->m_alloc_f (m_block, m_block.dim + additional_bytes);
  }

  void
  extensible_block::extend_to (size_t total_bytes)
  {
    if (total_bytes <= m_block.dim)
      {
	return;
      }
    extend_by (total_bytes - m_block.dim);
  }

  char *
  extensible_block::get_ptr ()
  {
    return m_block.ptr;
  }

  const char *
  extensible_block::get_read_ptr () const
  {
    return m_block.ptr;
  }

  std::size_t
  extensible_block::get_size () const
  {
    return m_block.dim;
  }

  char *
  extensible_block::release_ptr ()
  {
    char *ret_ptr = m_block.ptr;
    m_block.ptr = NULL;
    m_block.dim = 0;
    return ret_ptr;
  }

  //
  // extensible_stack_block
  //
  template <size_t S>
  extensible_stack_block<S>::extensible_stack_block ()
    : m_stack ()
    , m_ext_block ()
    , m_use_stack (true)
  {
  }

  template <size_t S>
  extensible_stack_block<S>::extensible_stack_block (const block_allocator &alloc)
    : m_stack ()
    , m_ext_block (alloc)
    , m_use_stack (true)
  {
  }

  template <size_t S>
  void
  extensible_stack_block<S>::extend_by (size_t additional_bytes)
  {
    if (m_use_stack)
      {
	m_ext_block.extend_to (m_stack.SIZE + additional_bytes);
      }
    else
      {
	m_ext_block.extend_by (additional_bytes);
      }
    m_use_stack = false;
  }

  template <size_t S>
  void
  extensible_stack_block<S>::extend_to (size_t total_bytes)
  {
    if (total_bytes <= m_stack.SIZE)
      {
	return;
      }
    m_ext_block.extend_to (total_bytes);
    m_use_stack = false;
  }

  template <size_t S>
  char *
  extensible_stack_block<S>::get_ptr ()
  {
    return m_use_stack ? m_stack.get_ptr () : m_ext_block.get_ptr ();
  }

  template <size_t S>
  const char *
  extensible_stack_block<S>::get_read_ptr () const
  {
    return m_use_stack ? m_stack.get_read_ptr () : m_ext_block.get_read_ptr ();
  }

  template <size_t S>
  char *
  extensible_stack_block<S>::release_ptr ()
  {
    return m_ext_block.release_ptr ();
  }

  template <size_t S>
  extensible_stack_block<S> &
  extensible_stack_block<S>::operator= (extensible_stack_block &&b)
  {
    this->~extensible_stack_block ();
    m_use_stack = b.m_use_stack;
    if (b.m_use_stack)
      {
	m_stack = std::move (b.m_stack);
      }
    else
      {
	m_ext_block = std::move (b.m_ext_block);
      }
    return *this;
  }
} // namespace cubmem

#endif // _MEM_BLOCK_HPP_
