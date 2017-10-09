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

/* extensible_array.cpp -
 *
 *  This C++ file implements the extensible_array class template.
 */

#include "extensible_array.hpp"

#include <cassert>
#include <cstring>

#include "db_private_allocator.hpp"
//#include "error_manager.h"

const size_t XARR_SIZE_64 = 64;
const size_t XARR_SIZE_ONE_K = 1024;

/************************************************************************/
/* extensible_array                                                     */
/************************************************************************/

template<typename T, size_t Size, typename Allocator>
inline extensible_array<T, Size, Allocator>::extensible_array (Allocator & allocator, size_t max_size)
  : m_allocator (allocator)
{
  m_dynamic_data = NULL;
  m_size = 0;
  m_capacity = Size;
  m_max_size = max_size;
}

template<typename T, size_t Size, typename Allocator>
inline extensible_array<T, Size, Allocator>::~extensible_array ()
{
  clear ();
}

/* C-style append & copy. they are declared unsafe and is recommended to avoid them (not always possible because of
* legacy code. */
template<typename T, size_t Size, typename Allocator>
inline int
extensible_array<T, Size, Allocator>::append (const T * source, size_t length)
{
  int err = check_resize (m_size + length);
  if (err != 0)
    {
      return err;
    }
  std::memcpy ((m_dynamic_data != NULL ? m_dynamic_data : m_static_data) + m_size, source, length * sizeof (T));
  m_size += length;

  return 0;
}

template<typename T, size_t Size, typename Allocator>
inline int
extensible_array<T, Size, Allocator>::copy (const T * source, size_t length)
{
  reset ();
  return append (source, length);
}

template<typename T, size_t Size, typename Allocator>
inline const T *
extensible_array<T, Size, Allocator>::get_data (void) const
{
  return m_dynamic_data != NULL ? m_dynamic_data : m_static_data;
}

template<typename T, size_t Size, typename Allocator>
inline size_t
extensible_array<T, Size, Allocator>::get_size (void) const
{
  return m_size;
}

template<typename T, size_t Size, typename Allocator>
inline size_t
extensible_array<T, Size, Allocator>::get_memsize (void) const
{
  return m_size * sizeof (T);
}

template<typename T, size_t Size, typename Allocator>
inline int
extensible_array<T, Size, Allocator>::check_resize (size_t size)
{
  if (size <= get_capacity ())
    {
      /* no resize */
      return 0;
    }

  return extend (size);
}

template<typename T, size_t Size, typename Allocator>
inline size_t
extensible_array<T, Size, Allocator>::get_capacity (void)
{
  return m_capacity;
}

/* extension should theoretically be rare, so don't inline them */
template<typename T, size_t Size, typename Allocator>
inline int
extensible_array<T, Size, Allocator>::extend (size_t size)
{
  if (m_max_size > 0 && m_max_size < size)
    {
      /* this increased too much */
      return -1;
    }

  size_t new_capacity = get_capacity ();
  while (new_capacity < size)
    {
      /* double capacity */
      new_capacity *= 2;
    }

  if (m_dynamic_data == NULL)
    {
      m_dynamic_data = m_allocator.allocate (new_capacity * sizeof (T));
      if (m_dynamic_data == NULL)
        {
          /* todo: handle private allocator errors */
          return -1;
        }
      if (m_size > 0)
        {
          std::memcpy (m_dynamic_data, m_static_data, m_size * sizeof (T));
        }
    }
  else
    {
      /* realloc */
      T* new_dynamic_data = m_allocator.allocate (new_capacity * sizeof (T));
      if (new_dynamic_data == NULL)
        {
          /* todo: handle private allocator errors */
          return -1;
        }
      std::memcpy (new_dynamic_data, m_dynamic_data, m_size * sizeof (T));
      clear ();
      m_dynamic_data = new_dynamic_data;
    }

  m_capacity = new_capacity;
  return 0;
}

template<typename T, size_t Size, typename Allocator>
inline void extensible_array<T, Size, Allocator>::reset (void)
{
  m_size = 0;
}

template<typename T, size_t Size, typename Allocator>
inline void extensible_array<T, Size, Allocator>::clear (void)
{
  if (m_dynamic_data != NULL)
    {
      m_allocator.deallocate (m_dynamic_data, m_capacity);
    }
}

template<typename T, size_t Size, typename Allocator>
inline T *
extensible_array<T, Size, Allocator>::end (void)
{
  return (m_dynamic_data != NULL ? m_dynamic_data : m_static_data) + m_size;
}

/************************************************************************/
/* Character-only functions                                             */
/************************************************************************/

template <size_t Size, typename Allocator>
inline int xarr_char_append_string (extensible_array<char, Size, Allocator> & buffer, const char *str, size_t length)
{
  assert (str != NULL);

  if (length == 0)
    {
      length = strlen (str) + 1;
    }

  return buffer.append (str, length);
}

template <class T, size_t Size, typename Allocator>
inline int xarr_char_append_object (extensible_array<char, Size, Allocator> & buffer, const T & to_append)
{
  return buffer.append (reinterpret_cast<const char *> (&to_append), sizeof (to_append));
}

/************************************************************************/
/* Predefined specializations                                           */
/************************************************************************/

/* char-based extensible arrays */

typedef class extensible_array<char, XARR_SIZE_64> xarr_char_64;
typedef class extensible_array<char, XARR_SIZE_ONE_K> xarr_char_1k;
typedef class extensible_array<char, XARR_SIZE_64, db_private_allocator<char> > xarr_char_64_private;
typedef class extensible_array<char, XARR_SIZE_ONE_K, db_private_allocator<char> > xarr_char_1k_private;

/* int-based */

typedef class extensible_array<int, XARR_SIZE_64> xarr_int_64;
typedef class extensible_array<int, XARR_SIZE_ONE_K> xarr_int_1k;
typedef class extensible_array<int, XARR_SIZE_64, db_private_allocator<int> > xarr_int_64_private;
typedef class extensible_array<int, XARR_SIZE_ONE_K, db_private_allocator<int> > xarr_int_1k_private;
