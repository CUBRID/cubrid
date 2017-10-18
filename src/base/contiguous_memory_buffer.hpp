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
 *      char *str_ptr = string_membuf.resize (std::strlen (string_to_copy));
 *      std::strcpy (str_ptr, string_to_copy);
 *
 */
template <typename T, size_t Size, typename Allocator>
class contiguous_memory_buffer
{
public:

  contiguous_memory_buffer (Allocator& alloc) :
    m_capacity (Size),
    m_alloc (alloc),
    m_dynamic_membuf (NULL),
    m_current_membuf (m_static_membuf)
  {
  }

  inline ~contiguous_memory_buffer ()
  {
    if (m_dynamic_membuf != nullptr)
      {
        m_alloc.deallocate (m_dynamic_membuf, m_capacity);
      }
  }

  inline T* resize (size_t size)
  {
    if (size > m_capacity)
      {
        extend (size);
      }
    return m_current_membuf;
  }

  inline T* get_membuf_data (void) const
  {
    return m_current_membuf;
  }

private:
  // no implicit constructor
  contiguous_memory_buffer ();

  inline size_t get_memsize (size_t count)
  {
    return count * sizeof (T);
  }

  void extend (size_t size)
  {
    size_t new_capacity = m_capacity;
    while (new_capacity < size)
      {
        /* double capacity */
        new_capacity *= 2;
      }

    if (m_dynamic_membuf == NULL)
      {
        assert (m_current_membuf == m_static_membuf);
        m_dynamic_membuf = m_alloc.allocate (get_memsize (new_capacity));
      }
    else
      {
        /* realloc */
        assert (m_current_membuf == m_dynamic_membuf);
        m_dynamic_membuf = m_alloc.allocate (new_capacity * sizeof (T));
      }
    std::memcpy (m_dynamic_membuf, m_current_membuf, get_memsize (m_capacity));
    if (m_current_membuf != m_static_membuf)
      {
        m_alloc.deallocate (m_current_membuf, m_capacity);
      }
    m_current_membuf = m_dynamic_membuf;
    m_capacity = new_capacity;
  }

  Allocator &m_alloc;
  T m_static_membuf[Size];
  T *m_dynamic_membuf;
  size_t m_capacity;
  T *m_current_membuf;
};

#endif /* _CONTIGUOUS_MEMORY_BUFFER_HPP_ */
