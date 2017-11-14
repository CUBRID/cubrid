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

/* Memory Management Functionality
 */

#if !defined(_MEM_HPP_)
#define _MEM_HPP_

#include <memory.h>
#include <functional>

namespace mem
{
  /* Memory Block
   * - groups together memory address and its size
   * - used to allocate, deallocate and share memory
   * - could be extended with helper info: allocator, src file&line where allocation was made, ...
   */
  struct block
  {
      size_t dim;
      char  *ptr;

      block()
	: dim {0}
	, ptr {nullptr}
      {}

      block (block &&b)             //move ctor
	: dim {b.dim}
	, ptr {b.ptr}
      {
	b.dim = 0;
	b.ptr = nullptr;
      }

      block &operator= (block &&b)  //move assign
      {
	if (this != &b)
	  {
	    dim = b.dim;
	    ptr = b.ptr;
	    b.dim = 0;
	    b.ptr = nullptr;
	  }
	return *this;
      }

      block (size_t dim, void *ptr)
	: dim {dim}
	, ptr { (char *)ptr}
      {}

      bool is_valid ()
      {
	return (dim != 0 && ptr != 0);
      }

    private:
      block (const block &) = delete;
      block &operator= (const block &) = delete;
  };

  inline void default_realloc (block &b, size_t len)
  {
#if 0 //realloc based
    if (b.dim == 0)
      {
	b.dim = 1;
      }
    for (size_t n=b.dim; b.dim < n+len; b.dim*=2); // calc next power of 2 >= b.dim
    b.ptr = (char *) realloc (b.ptr, b.dim);
#else //new + memcopy + delete
    size_t dim = b.dim ? b.dim : 1;
    for (; dim < b.dim+len; dim*=2); // calc next power of 2 >= b.dim
    mem::block x{dim, new char[dim]};
    memcpy (x.ptr, b.ptr, b.dim);
    delete b.ptr;
    b = std::move (x);
#endif
  }

  inline void default_dealloc (mem::block &block)
  {
#if 0 //free based
    free (block.ptr);
    block = {};
#else //delete based
    delete block.ptr;
    block = {};
#endif
  }

  /* Memory Block - Extendable
   * - able to extend/reallocate to accomodate additional bytes
   */
  struct block_ext: public block
  {
      block_ext()                                       //default ctor
	: block_ext {default_realloc, default_dealloc}
      {
      }

      block_ext (block_ext &&b)                         //move ctor
	: block {std::move (b)}
	, m_extend {b.m_extend}
	, m_dealloc {b.m_dealloc}
      {
	b.dim = 0;
	b.ptr = nullptr;
      }

      block_ext &operator= (block_ext &&b)              //move assignment
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

      ~block_ext()                                      //dtor
      {
	m_dealloc (*this);
      }

      void extend (size_t additional_bytes)
      {
	m_extend (*this, additional_bytes);
      }

    private:
      std::function<void (block &b, size_t n)> m_extend; //extend memory block to fit at least additional n bytes
      std::function<void (block &b)> m_dealloc;         //deallocate memory block

      block_ext (const block_ext &) = delete;           //copy ctor
      block_ext &operator= (const block_ext &) = delete; //copy assignment
  };

} // namespace mem

#endif // !defined(_MEM_HPP_)
