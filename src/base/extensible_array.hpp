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

#include "contiguous_memory_buffer.hpp"

#include <memory>

/* extensible_array -
 *
 *    A Contiguous Container with static initial size that can be dynamically extended if need be. The purpose was to
 *    provide a tool to store data varying in size and handling memory needs automatically (which was often handled
 *    manually), with the best performance when static size is not exceeded.
 *
 *
 * When to use:
 *
 *    When data varying in size must be stored or collected preferably with no dynamic memory allocation.
 *
 *    One example of code we often write for popular operations:
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
 *            buffer_p = db_private_realloc (thread_p, buffer_p, new_capacity);
 *          }
 *        memcpy (buffer_p, page_ptr, size_of_data);
 *        ...
 *
 *      end:
 *        // delete if dynamically allocated
 *        if (buffer_p != buffer)
 *          {
 *            db_private_free (thread_p, buffer_p);
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
 *    1. Include extensible_array.cpp, where the template class implementation is found.
 *
 *    2. declare static variable with a default size:
 *        const int BUF_SIZE = 64;
 *        db_private_allocator<char> char_private_allocator (thread_p);
 *        extensible_array<char, BUF_SIZE, db_private_allocator<char> > buffer(char_private_allocator);
 *
 *    2. append the required buffer.
 *        buffer.append (REINTERPRET_CAST (char *, &header), sizeof (header));
 *        buffer.append (page_ptr, size_of_data);  // size_of_data can vary from 0 to 16k, but will usually be
 *                                                 // small enough
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
  extensible_array (Allocator & allocator);

  ~extensible_array ();

  inline void append (const T* source, size_t length);
  inline void copy (const T* source, size_t length);

  inline const T* get_array (void) const;
  inline size_t get_size (void) const;
  inline size_t get_memsize (void) const;

private:
  inline void reset (void);

  contiguous_memory_buffer<T, Size, Allocator> m_membuf;
  size_t m_size;
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
