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

/* extensible_array.hpp -
 *
 *  This header file defines the extensible_array class template.
 */

#ifndef EXTENSIBLE_ARRAY_HPP_
#define EXTENSIBLE_ARRAY_HPP_

#include "mem_block.hpp"

#include <memory>

namespace cubmem
{
  template <size_t Size>
  class appendible_block : public cubmem::extensible_stack_block<Size>
  {
    private:
      using base_type = cubmem::extensible_stack_block<Size>;

    public:

      appendible_block ();
      appendible_block (const block_allocator &alloc);

      inline void append (const char *source, size_t length);    // append at the end of existing data
      inline void copy (const char *source, size_t length);      // overwrite entire array

      template <typename T>
      inline void append (const T &obj);

      inline std::size_t get_size () const;

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
      void extend_by (size_t count);
      void extend_to (size_t count);

      const T *get_array (void) const;

    protected:
      T *get_data_ptr ();

      size_t get_memsize_for_count (size_t count) const;
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

      inline void append (const T &source);
      inline void append (const T *source, size_t length);    // append at the end of existing data
      inline void copy (const T *source, size_t length);      // overwrite entire array
      void erase (size_t index);    // remove at index; does not preserve order

      inline size_t get_size (void) const;                    // get current size

      size_t get_memsize () const;

    private:
      inline void reset (void);                               // reset array
      inline T *get_append_ptr ();

      size_t m_size;                                          // current size
  };
} // namespace cubmem

/************************************************************************/
/* Implementation                                                       */
/************************************************************************/

#include <cassert>
#include <cstring>

namespace cubmem
{
  //
  //  appendible_block
  //
  template <size_t Size>
  appendible_block<Size>::appendible_block ()
    : extensible_stack_block<Size> ()
    , m_size (0)
  {
  }

  template <size_t Size>
  appendible_block<Size>::appendible_block (const block_allocator &alloc)
    : extensible_stack_block<Size> (alloc)
    , m_size (0)
  {
  }

  template <size_t Size>
  std::size_t
  appendible_block<Size>::get_size () const
  {
    return m_size;
  }

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

  //
  // extensible_array
  //
  template <typename T, size_t Size>
  void
  extensible_array<T, Size>::extend_by (size_t count)
  {
    base_type::extend_by (get_memsize_for_count (count));
  }

  template <typename T, size_t Size>
  void
  extensible_array<T, Size>::extend_to (size_t count)
  {
    base_type::extend_to (get_memsize_for_count (count));
  }

  template <typename T, size_t Size>
  const T *
  extensible_array<T, Size>::get_array (void) const
  {
    return reinterpret_cast<const T *> (base_type::get_read_ptr ());
  }

  template <typename T, size_t Size>
  T *
  extensible_array<T, Size>::get_data_ptr ()
  {
    return reinterpret_cast<T *> (base_type::get_ptr ());
  }

  template <typename T, size_t Size>
  size_t
  extensible_array<T, Size>::get_memsize_for_count (size_t count) const
  {
    return count * sizeof (T);
  }

  //
  // appendable_array
  //
  template <typename T, size_t Size>
  appendable_array<T, Size>::appendable_array (void)
    : base_type ()
    , m_size (0)
  {
    //
  }

  template <typename T, size_t Size>
  appendable_array<T, Size>::appendable_array (const block_allocator &allocator)
    : base_type (allocator)
    , m_size (0)
  {
    // empty
  }

  template <typename T, size_t Size>
  appendable_array<T, Size>::~appendable_array ()
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
  appendable_array<T, Size>::append (const T &source)
  {
    append (&source, 1);
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
  void
  appendable_array<T, Size>::copy (const T *source, size_t length)
  {
    // copy = reset + append
    reset ();
    append (source, length);
  }

  template <typename T, size_t Size>
  void
  appendable_array<T, Size>::erase (size_t index)
  {
    if (index >= m_size)
      {
	// no op
	return;
      }
    if (index < m_size - 1)
      {
	// overwrite with last
	base_type::get_data_ptr ()[index] = base_type::get_data_ptr ()[m_size - 1];
      }
    --m_size;
  }

  template <typename T, size_t Size>
  size_t
  appendable_array<T, Size>::get_size (void) const
  {
    return m_size;
  }

  template<typename T, size_t S>
  size_t
  appendable_array<T, S>::get_memsize () const
  {
    return base_type::get_memsize_for_count (m_size);
  }

  template <typename T, size_t Size>
  void
  appendable_array<T, Size>::reset (void)
  {
    // set size to zero
    m_size = 0;
  }

} // namespace cubmem

#endif /* !EXTENSIBLE_ARRAY_HPP_ */
