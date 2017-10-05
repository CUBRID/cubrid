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

#include "db_private_allocator.hpp"

const size_t XARR_SIZE_64 = 64;
const size_t XARR_SIZE_ONE_K = 1024;

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

/************************************************************************/
/* extensible_array                                                     */
/************************************************************************/

template<typename T, size_t Size, typename Allocator>
inline extensible_array<T, Size, Allocator>::extensible_array (Allocator & allocator, size_t max_size)
{
  m_dynamic_data = nullptr;
  m_allocator = allocator;
  m_size = 0;
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
inline int extensible_array<T, Size, Allocator>::append_unsafe (const T * source, size_t length)
{
  return append (source, source + length);
}

template<typename T, size_t Size, typename Allocator>
inline int extensible_array<T, Size, Allocator>::copy_unsafe (const T * source, size_t length)
{
  reset ();
  return append_unsafe (source, length);
}

template<typename T, size_t Size, typename Allocator>
inline const T * extensible_array<T, Size, Allocator>::get_data (void) const
{
  return m_dynamic_data != nullptr ? m_dynamic_data->data () : m_static_data.data ();
}

template<typename T, size_t Size, typename Allocator>
inline size_t extensible_array<T, Size, Allocator>::get_size (void) const
{
  return m_size;
}

template<typename T, size_t Size, typename Allocator>
inline int extensible_array<T, Size, Allocator>::check_resize (size_t size)
{
  if (size <= get_capacity ())
    {
      /* no resize */
      return 0;
    }

  return extend (size);
}

template<typename T, size_t Size, typename Allocator>
inline size_t extensible_array<T, Size, Allocator>::get_capacity (void)
{
  return m_dynamic_data != nullptr ? m_dynamic_data->capacity () : Size;
}

/* extension should theoretically be rare, so don't inline them */
template<typename T, size_t Size, typename Allocator>
inline int extensible_array<T, Size, Allocator>::extend (size_t size)
{
  std::cout << "extend " << size << std::endl;

  if (m_max_size > 0 && m_max_size < size)
    {
    /* this increased too much */
    return -1;
    }

  /* no dynamic allocation */
  if (m_allocator == nullptr)
    {
    return -1;
    }

  size_t new_capacity = get_capacity ();
  while (new_capacity < size)
    {
    /* double capacity */
    new_capacity *= 2;
    }

  if (m_dynamic_data == nullptr)
    {
    m_dynamic_data = new std::vector<T, Allocator> (new_capacity, *m_allocator);
    if (m_dynamic_data == nullptr)
      {
      /* todo: handle private allocator errors */
      return -1;
      }
    if (m_size > 0)
      {
      std::copy (&m_static_data[0], &m_static_data[m_size], m_dynamic_data->begin ());
      }
    }
  else
    {
    m_dynamic_data->resize (new_capacity);
    /* todo: handle private allocator errors */
    }
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
  delete m_dynamic_data;
}

/* append and copy with start and end iterators. use it when the intention is to avoid
*/
template<typename T, size_t Size, typename Allocator>
template<typename It>
inline int extensible_array<T, Size, Allocator>::append (const It & first, const It & last)
{
  assert (first < last);

  size_t diff = last - first;
  int err = check_resize (m_size + diff);
  if (err != 0)
    {
      return err;
    }
  if (m_dynamic_data != nullptr)
    {
      std::copy (first, last, m_dynamic_data->begin () + m_size);
    }
  else
    {
      std::copy (first, last, m_static_data.begin () + m_size);
    }
  m_size += diff;
  return 0;
}

template<typename T, size_t Size, typename Allocator>
template<typename It>
inline int extensible_array<T, Size, Allocator>::copy (const It & first, const It & last)
{
  reset ();
  return append (first, last);
}

template<typename T, size_t Size, typename Allocator>
template<typename It>
inline int extensible_array<T, Size, Allocator>::append (const It & it)
{
  return append (it.cbegin (), it.cend ());
}

template<typename T, size_t Size, typename Allocator>
template<typename It>
inline int extensible_array<T, Size, Allocator>::copy (const It & it)
{
  reset ();
  return append (it);
}

/************************************************************************/
/* Character-only functions                                             */
/************************************************************************/

template <size_t Size, typename Allocator>
inline int xarr_char_append_string (extensible_array<char, Size, Allocator> & buffer, const char *str, size_t length)
{
  assert (str != nullptr);

  if (length == 0)
    {
      length = std::strlen (str) + 1;
    }

  return buffer.append_unsafe (str, length);
}

template <class T, size_t Size, typename Allocator>
inline int xarr_char_append_object (extensible_array<char, Size, Allocator> & buffer, const T & to_append)
{
  return buffer.append_unsafe (reinterpret_cast<const char *> (&to_append), sizeof (to_append));
}
