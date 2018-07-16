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
 * mem_block.hpp - Memory Management Functionality
 */

#ifndef _MEM_BLOCK_HPP_
#define _MEM_BLOCK_HPP_

#include <memory.h>
#include <functional>

namespace mem
{
  /*
   * Memory Block
   * - groups together memory address and its size
   * - doesn't own the memory, just points to it
   * - used to allocate, deallocate and share memory
   * - could be extended with helper info: allocator, src file&line where allocation was made, ...
   */
  struct block
  {
      size_t dim;
      char  *ptr;

      block ()
	: dim {0}
	, ptr {NULL}
      {
      }

      block (block &&b)             //move ctor
	: dim {b.dim}
	, ptr {b.ptr}
      {
	b.dim = 0;
	b.ptr = NULL;
      }

      block &operator= (block &&b)  //move assign
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

      block (size_t dim, void *ptr)
	: dim {dim}
	, ptr { (char *) ptr}
      {
      }

      virtual ~block ()
      {
	dim = 0;
	ptr = NULL;
      }

      bool is_valid ()
      {
	return (dim != 0 && ptr != NULL);
      }

      char *move_ptr ()                                    //NOT RECOMMENDED! use move semantics: std::move()
      {
	char *p = ptr;

	dim = 0;
	ptr = NULL;

	return p;
      }

    private:
      block (const block &) = delete;
      block &operator= (const block &) = delete;
  };

  inline void default_realloc (block &b, size_t len)
  {
    size_t dim = b.dim ? b.dim : 1;

    // calc next power of 2 >= b.dim
    for (; dim < b.dim + len; dim *= 2)
      ;

    block x{dim, new char[dim]};
    memcpy (x.ptr, b.ptr, b.dim);

    delete [] b.ptr;

    b = std::move (x);
  }

  inline void default_dealloc (block &b)
  {
    delete [] b.ptr;
    b = {};
  }

  /* Memory Block - Extendable
   * - able to extend/reallocate to accomodate additional bytes
   * - owns the memory by default and it will free the memory in destructor unless it is moved:
   *    {
   *        mem::block_ext block{some_realloc, some_dealloc};//some_realloc/dealloc = functions, functors or lambdas
   *        //...
   *        //move it or it will be deallocated; simple copy => compiler error because it is not designed to be copied
   *        mem::block b = std::move(block);
   *    }
   */
  struct block_ext: public block
  {
      block_ext ()                                         //default ctor
      //: block_ext {default_realloc, default_dealloc} //doesn't work on gcc 4.4.7
	: block {}
	, m_extend {default_realloc}
	, m_dealloc {default_dealloc}
      {
      }

      block_ext (block_ext &&b)                           //move ctor
	: block {std::move (b)}
	, m_extend {b.m_extend}
	, m_dealloc {b.m_dealloc}
      {
	b.dim = 0;
	b.ptr = NULL;
      }

      block_ext &operator= (block_ext &&b)                //move assignment
      {
	if (this != &b)
	  {
	    m_dealloc (*this);
	    dim = b.dim;
	    ptr = b.ptr;
	    m_extend = b.m_extend;
	    m_dealloc = b.m_dealloc;
	    b.dim = 0;
	    b.ptr = NULL;
	  }

	return *this;
      }

      block_ext (std::function<void (block &b, size_t n)> extend, std::function<void (block &b)> dealloc) //general ctor
	: block {}
	, m_extend {extend}
	, m_dealloc {dealloc}
      {}

      ~block_ext ()                                        //dtor
      {
	m_dealloc (*this);
      }

      void extend (size_t additional_bytes)
      {
	m_extend (*this, additional_bytes);
      }

    private:
      std::function<void (block &b, size_t n)> m_extend;  //extend memory block to fit at least additional n bytes
      std::function<void (block &b)> m_dealloc;           //deallocate memory block

      block_ext (const block_ext &) = delete;             //copy ctor
      block_ext &operator= (const block_ext &) = delete;  //copy assignment
  };

} // namespace mem

#endif // _MEM_BLOCK_HPP_
