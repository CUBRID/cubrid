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

#include <memory>
#include <vector>
#include <array>

/* for debug */
#include <iostream>

/* extensible_array -
 *
 *    A Contiguous Container with static initial size that can be dynamically extended if need be. The purpose was to
 *    provide an useful tool to store data varying in size and handling memory needs automatically (which was often
 *    handled manually).
 *
 *    It is designed to be used with both C++ Contiguous Containers (array, basic_string, vector) and C style dynamic
 *    arrays. C style dynamic arrays are deemed unsafe (since bounds are not automatically checked), however is needed
 *    for all the legacy code.
 *
 *
 * When to use:
 *
 *    When data varying in size must be stored or collected preferably with no dynamic memory allocation.
 *
 *    One example (assuming code may be executed very often):
 *
 *        const int BUF_SIZE = 128;
 *        char buffer[BUF_SIZE];      // static buffer
 *        char *buffer_p = buffer;    // for now points to static buffer, but may point to dynamic memory
 *        int offset = 0;             // current offset in buffer
 *        int capacity = BUF_SIZE;    // current capacity
 *
 *        memcpy (buffer_p, &header, sizeof (header));
 *        offset += sizeof (header);
 *        ...
 *        if (offset + size_of_data > capacity)           // size_of_data can vary from 0 to 16k, but will usually be
 *                                                        // small enough
 *          {
 *            int new_capacity = capacity;
 *            while (new_capacity < offset + size_of_data)    // double until we can cover
 *              {
 *                new_capacity = 2 * new_capacity
 *              }
 *
 *            char *new_buffer_p = new char[new_capacity];    // if malloc is used, realloc can be used here.
 *            memcpy (new_buffer_p, buffer_p, offset);
 *            delete buffer_p;
 *            buffer_p = new_buffer_p;
 *          }
 *        memcpy (buffer_p, page_ptr, size_of_data);
 *        ...
 *
 *      end:
 *        // delete if dynamically allocated
 *        if (buffer_p != buffer)
 *          {
 *            delete buffer_p;
 *          }
 *
 *
 * When not to use:
 *
 *    1. When memory requirements can be predicted and are reasonably small - use static array instead.
 *
 *    2. When performance is not an issue and dynamic allocation is affordable. Use std::string or std::vector instead.
 *
 *
 * How to use:
 *
 *    1. Include extensible_array.cpp (yes, not a typo) into your .c/.cpp file. The implementation and predefined
 *       specializations are there and you won't be able to use the extensible_array. The main reason is to prevent
 *       using this in another header file (it is not actually prevented, but it does make it awkward).
 *       Try not to create too many specialization of this template class. Although this is designed to be mainly
 *       inline, I would still not abuse it.
 *
 *    2. declare static variable with a default size:
 *        const int BUF_SIZE = 64;
 *        db_private_allocator<char> char_private_allocator (thread_p);
 *        extensible_array<char, BUF_SIZE, db_private_allocator<char> > buffer(char_private_allocator);
 *
 *    2. append the required buffer.
 *        buffer.append_unsafe (REINTERPRET_CAST (char *, &header), sizeof (header));
 *        buffer.append_unsafe (page_ptr, size_of_data);  // size_of_data can vary from 0 to 16k, but will usually be
 *                                                        // small enough
 *
 *    3. resources are freed automatically once variable scope ends.
 *
 *
 * Templates:
 *
 *    1. T - buffer base type. in most cases a primitive is expected, but the buffer can accept any class.
 *
 *    2. Size - buffer static size. It is recommended to provide a value that covers most cases.
 *
 *    3. [Optional] Allocator - which allocator to be used for dynamic allocation. Default is standard allocator.
 *
 *
 * Implementation details:
 *
 *    A std::array stores the static buffer and a std::vector the dynamic buffer. When static buffer size limit is
 *    reached the vector is allocated. Size is always doubled to cover the new requirements. Vector is resized whenever
 *    necessary.
 *    There are two operations: append and copy (which starts over). They can accept an iterator (which is consumed
 *    from begin to end), or start/end iterators.
 *
 *
 * Notes:
 *
 *    If possible, provide a maximum buffer size to avoid uncontrolled allocations.
 *
 *
 * TODO:
 *
 *    Add alignment. With this feature, we may even replace OR_BUF.
 *
 *    Should we handle out of memory?
 */
template <typename T, size_t Size, typename Allocator = std::allocator<T> >
class extensible_array
{
public:
  extensible_array (Allocator & allocator = NULL, size_t max_size = 0);

  ~extensible_array ();

  /* append and copy with start and end iterators. use it when the intention is to avoid 
   */
  template <typename It>
  inline int append (const It & first, const It & last);
  template <typename It>
  inline int copy (const It & first, const It & last);

  template <typename It>
  inline int append (const It& it);
  template <typename It>
  inline int copy (const It& it);

  /* C-style append & copy. they are declared unsafe and is recommended to avoid them (not always possible because of
   * legacy code. */
  inline int append_unsafe (const T* source, size_t length);
  inline int copy_unsafe (const T* source, size_t length);

  inline const T* get_data (void) const;

  inline size_t get_size (void) const;

private:

  inline int check_resize (size_t size);

  inline size_t get_capacity (void);

  /* extension should theoretically be rare, so don't inline them */
  int extend (size_t size);

  inline void reset (void);

  inline void clear (void);

  Allocator & m_allocator;
  std::vector<T, Allocator> *m_dynamic_data;
  std::array<T, Size> m_static_data;
  size_t m_size;
  size_t m_max_size;
};

/************************************************************************/
/* Character-only functions                                             */
/************************************************************************/

/* extensible_charbuf_append_string - append string to extensible char buffer.
 *
 *  Templates:
 *    Size: extensible buffer static size
 *    Allocator: allocator for extensible buffer dynamic resources
 *
 *  Return:
 *    Error code
 *
 * Parameters:
 *    buffer: extensible char buffer
 *    str: append string to buffer
 *    length: if 0, strlen (str) + 1 is used
 */
template <size_t Size, typename Allocator = std::allocator<char> >
inline int xarr_char_append_string (extensible_array<char, Size, Allocator>& buffer, const char *str,
                                    size_t length = 0);

/* extensible_charbuf_append_object - append object data to extensible char buffer.
 *
 *  Templates:
 *    T: type of object
 *    Size: extensible buffer static size
 *    Allocator: allocator for extensible buffer dynamic resources
 *
 *  Return:
 *    Error code
 *
 * Parameters:
 *    buffer: extensible char buffer
 *    to_append: object to append
 */
template <class T, size_t Size, typename Allocator = std::allocator<char> >
inline int xarr_char_append_object (extensible_array<char, Size, Allocator>& buffer, const T & to_append);

#endif /* !EXTENSIBLE_ARRAY_HPP_ */
