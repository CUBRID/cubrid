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
 *
 */

//
// memory_private_allocator.hpp - extension to memory_alloc.h private allocation
//

#ifndef _MEMORY_PRIVATE_ALLOCATOR_HPP_
#define _MEMORY_PRIVATE_ALLOCATOR_HPP_

#include "mem_block.hpp"
#include "memory_alloc.h"

namespace mem
{
  template <class T>
  class private_unique_ptr_deleter
  {
    public:
      private_unique_ptr_deleter ()
	: thread_p (NULL)
      {
      }

      private_unique_ptr_deleter (THREAD_ENTRY *thread_p)
	: thread_p (thread_p)
      {
      }

      void operator () (T *ptr) const
      {
	if (ptr != NULL)
	  {
	    db_private_free (thread_p, ptr);
	  }
      }

    private:
      THREAD_ENTRY *thread_p;
  };

  template <class T>
  class private_unique_ptr
  {
    public:
      private_unique_ptr (T *ptr, THREAD_ENTRY *thread_p)
      {
	smart_ptr = std::unique_ptr<T, private_unique_ptr_deleter <T> >
		    (ptr, private_unique_ptr_deleter <T> (thread_p));
      }

      T *get ()
      {
	return smart_ptr.get ();
      }

      T *release ()
      {
	return smart_ptr.release ();
      }

      void swap (private_unique_ptr <T> &other)
      {
	smart_ptr.swap (other.smart_ptr);
      }

      void reset (T *ptr)
      {
	smart_ptr.reset (ptr);
      }

      T *operator-> () const
      {
	return smart_ptr.get();
      }

      T &operator* () const
      {
	return *smart_ptr.get();
      }

    private:
      std::unique_ptr <T, private_unique_ptr_deleter <T> >smart_ptr;
  };

  extern const block_allocator PRIVATE_BLOCK_ALLOCATOR;
} // namespace mem

#endif // _MEMORY_PRIVATE_ALLOCATOR_HPP_
