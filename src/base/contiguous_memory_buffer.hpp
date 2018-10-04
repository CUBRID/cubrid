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


/*
 * compact_stack_heap_allocator.hpp - Stack allocator to manipulate stack memory
 */

#ifndef _CONTIGUOUS_MEMORY_BUFFER_HPP_
#define _CONTIGUOUS_MEMORY_BUFFER_HPP_

#include <stddef.h>
#include <cstring>

/* contiguous_memory_buffer -
 *
 *  this class can be used to maintain a contiguous memory area, either on stack or on heap. if the required size can
 *  fit stack, then stack buffer stores data. If not, a dynamic buffer is allocated (or reallocated) whenever required
 *  size exceeds current capacity. Data is automatically copied to new buffer.
 *
 *
 *  Templates:
 *
 *      T: base allocation type.
 *      Size: stack buffer size
 *      Allocator: heap allocator
 *
 *
 *  How to use:
 *
 *      db_private_allocator<char> prv_alloc (thread_p);
 *      contiguous_memory_buffer<char, SIZE_64, db_private_allocator<char> > string_membuf;
 *
 *      // make a string copy. in most cases, it will fit into string_membuf stack buffer
 *      char *str_ptr = string_membuf.resize (std::strlen (string_to_copy) + 1);
 *      std::strcpy (str_ptr, string_to_copy);
 *
 */
template <typename T, size_t Size, typename Allocator>
class contiguous_memory_buffer
{
  public:

    contiguous_memory_buffer (void);
    contiguous_memory_buffer (Allocator &alloc);
    inline ~contiguous_memory_buffer (void);

    inline T *resize (size_t size);           // resize if size is bigger than current buffer and return updated buffer
    // pointer
    inline T *get_membuf_data (void) const;   // get current buffer pointer

  private:

    inline size_t get_memsize (size_t count); // memory size for count T's
    void extend (size_t size);                // extend buffer to hold count T's
    // this is the only part not-inlined of contiguous_memory_buffer. it is
    // supposed to be an exceptional case

    Allocator *m_alloc;                       // heap allocator
    Allocator *m_own_alloc;                   // own heap allocator
    size_t m_capacity;                        // current buffer capacity
    T m_stack_membuf[Size];                   // stack buffer
    T *m_heap_membuf;                         // heap buffer
    T *m_current_membuf;                      // current buffer pointer
};

#endif /* _CONTIGUOUS_MEMORY_BUFFER_HPP_ */

/************************************************************************/
/* Implementation                                                       */
/************************************************************************/

template<typename T, size_t Size, typename Allocator>
inline
contiguous_memory_buffer<T, Size, Allocator>::contiguous_memory_buffer (void)
  : m_alloc (NULL)
  , m_own_alloc (NULL)
  , m_capacity (Size)
  , m_stack_membuf {}
  , m_heap_membuf (NULL)
  , m_current_membuf (m_stack_membuf)
{
  //
}

template<typename T, size_t Size, typename Allocator>
inline
contiguous_memory_buffer<T, Size, Allocator>::contiguous_memory_buffer (Allocator &alloc)
  : contiguous_memory_buffer<T, Size, Allocator> ()
{
  m_alloc = &alloc;
}

template<typename T, size_t Size, typename Allocator>
inline
contiguous_memory_buffer<T, Size, Allocator>::~contiguous_memory_buffer ()
{
  // free dynamic buffer
  if (m_heap_membuf != NULL)
    {
      assert (m_alloc != NULL);
      m_alloc->deallocate (m_heap_membuf, m_capacity);
    }
  if (m_own_alloc != NULL)
    {
      delete m_own_alloc;
    }
}

template<typename T, size_t Size, typename Allocator>
inline T *
contiguous_memory_buffer<T, Size, Allocator>::resize (size_t size)
{
  if (size > m_capacity)
    {
      extend (size);

      // m_current_membuf is automatically updated by extend
    }
  return m_current_membuf;
}

template<typename T, size_t Size, typename Allocator>
inline T *
contiguous_memory_buffer<T, Size, Allocator>::get_membuf_data (void) const
{
  return m_current_membuf;
}

template<typename T, size_t Size, typename Allocator>
inline size_t
contiguous_memory_buffer<T, Size, Allocator>::get_memsize (size_t count)
{
  return count * sizeof (T);
}

template<typename T, size_t Size, typename Allocator>
inline void
contiguous_memory_buffer<T, Size, Allocator>::extend (size_t size)
{
  size_t new_capacity = m_capacity;
  while (new_capacity < size)
    {
      // double capacity until required size can be covered
      new_capacity *= 2;
    }

  if (m_heap_membuf == NULL)
    {
      if (m_alloc == NULL)
	{
	  m_own_alloc = m_alloc = new Allocator ();
	}
      // allocate heap buffer for first time
      assert (m_current_membuf == m_stack_membuf);
      m_heap_membuf = m_alloc->allocate (get_memsize (new_capacity));
    }
  else
    {
      // reallocate heap buffer
      assert (m_current_membuf == m_heap_membuf);
      assert (m_alloc != NULL);
      m_heap_membuf = m_alloc->allocate (new_capacity * sizeof (T));
    }
  // copy the data from old buffer - m_current_membuf - to new buffer - m_heap_membuf
  std::memcpy (m_heap_membuf, m_current_membuf, get_memsize (m_capacity));
  if (m_current_membuf != m_stack_membuf)
    {
      // deallocate heap buffer
      assert (m_alloc != NULL);
      m_alloc->deallocate (m_current_membuf, m_capacity);
    }
  // update current buffer pointer
  m_current_membuf = m_heap_membuf;
  // update capacity
  m_capacity = new_capacity;
}

