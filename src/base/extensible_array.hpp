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

/* extensible_array.hpp -
 *
 *  This header file defines the extensible_array class template.
 */

#ifndef EXTENSIBLE_ARRAY_HPP_
#define EXTENSIBLE_ARRAY_HPP_

#include "mem_block.hpp"

#include <memory>

namespace mem
{
  template <size_t Size>
  class appendible_block : public mem::extensible_stack_block<Size>
  {
    private:
      using base_type = mem::extensible_stack_block<Size>;

    public:

      appendible_block ()
	: extensible_stack_block<Size> ()
	, m_size (0)
      {
      }

      appendible_block (const block_allocator &alloc)
	: extensible_stack_block<Size> (alloc)
	, m_size (0)
      {
      }

      inline void append (const char *source, size_t length);    // append at the end of existing data
      inline void copy (const char *source, size_t length);      // overwrite entire array

      template <typename T>
      inline void append (const T &obj);

    private:
      inline void reset ();

      size_t m_size;                                          // current size
  };

  template <typename T, size_t Size>
  class extensible_array : protected extensible_stack_block<sizeof (T) * Size>
  {
    private:
      using base_type = extensible_stack_block<sizeof (T) * Size>;

    public:
      void extend_by (size_t count)
      {
	base_type::extend_by (get_memsize_for_count (count));
      }

      void extend_to (size_t count)
      {
	base_type::extend_to (get_memsize_for_count (count));
      }

      const T *get_array (void)
      {
	return get_data_ptr ();
      }

      size_t get_memsize () const
      {
	return base_type::get_size ();
      }

    protected:
      T *get_data_ptr ()
      {
	return (T *) base_type::get_ptr ();
      }

    private:

      size_t get_memsize_for_count (size_t count)
      {
	return count * sizeof (T);
      }
  };

  template <typename T, size_t S>
  class appendable_array : public extensible_array<T, S>
  {
    private:
      using base_type = extensible_array<T, S>;

    public:
      appendable_array (void);
      appendable_array (const block_allocator &allocator);
      ~appendable_array ();

      inline void append (const T *source, size_t length);    // append at the end of existing data
      inline void copy (const T *source, size_t length);      // overwrite entire array

      inline size_t get_size (void) const;                    // get current size

    private:
      inline void reset (void);                               // reset array
      inline T *get_append_ptr ();

      size_t m_size;                                          // current size
  };
} // namespace mem

/************************************************************************/
/* Implementation                                                       */
/************************************************************************/

#include <cassert>
#include <cstring>

namespace mem
{
  //
  //  appendible_block
  //
  template <size_t Size>
  void
  appendible_block<Size>::reset ()
  {
    m_size = 0;
  }

  template <size_t Size>
  void
  appendible_block<Size>::append (const char *source, size_t length)
  {
    base_type::extend_to (m_size + length);
    std::memcpy (base_type::get_ptr () + m_size, source, length);
    m_size += length;
  }

  template <size_t Size>
  template <typename T>
  void
  appendible_block<Size>::append (const T &obj)
  {
    append (reinterpret_cast<const char *> (&obj), sizeof (obj));
  }

  template <size_t Size>
  void
  appendible_block<Size>::copy (const char *source, size_t length)
  {
    reset ();
    append (source, length);
  }

  template <typename T, size_t Size>
  inline appendable_array<T, Size>::appendable_array (void)
    : base_type ()
    , m_size (0)
  {
    //
  }

  template <typename T, size_t Size>
  inline appendable_array<T, Size>::appendable_array (const block_allocator &allocator)
    : base_type (allocator)
    , m_size (0)
  {
    // empty
  }

  template <typename T, size_t Size>
  inline appendable_array<T, Size>::~appendable_array ()
  {
    // empty
  }

  template <typename T, size_t Size>
  T *
  appendable_array<T, Size>::get_append_ptr ()
  {
    return base_type::get_data_ptr () + m_size;
  }

  template <typename T, size_t Size>
  void
  appendable_array<T, Size>::append (const T *source, size_t length)
  {
    // make sure memory is enough
    base_type::extend_to (m_size + length);

    // copy data at the end of the array
    std::memcpy (base_type::get_data_ptr () + m_size, source, length * sizeof (T));
    m_size += length;
  }

  template <typename T, size_t Size>
  inline void
  appendable_array<T, Size>::copy (const T *source, size_t length)
  {
    // copy = reset + append
    reset ();
    append (source, length);
  }

  template <typename T, size_t Size>
  inline size_t
  appendable_array<T, Size>::get_size (void) const
  {
    return m_size;
  }

  template <typename T, size_t Size>
  inline void appendable_array<T, Size>::reset (void)
  {
    // set size to zero
    m_size = 0;
  }

} // namespace mem

#endif /* !EXTENSIBLE_ARRAY_HPP_ */
