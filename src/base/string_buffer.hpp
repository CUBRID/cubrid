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

/*
 * string_buffer.hpp
 *
 * - collects formatted text (printf-like syntax) in a fixed size buffer
 * - useful to build a formatted string in successive function calls without dynamic memory allocation
 * - if the provided buffer is too small then the len() method can be used to find necessary size
 * (similar with snprintf() behavior)
 * to simplify the caller code and because this class has only one functionality (add text with format), I used
 * operator() instead of a named method (see below)
 *
 * Usage:
 *   string_buffer sb; //uses default_realloc & default_dealloc by default
 *   sb("simple text");                                   // <=> sb.add_with_format("simple text")
 *   sb("format i=%d f=%lf s=\"%s\"", 1, 2.3, "4567890");
 *   printf(sb.get_buffer()); // => i=1 f=2.3 s="4567890"
 */

#ifndef _STRING_BUFFER_HPP_
#define _STRING_BUFFER_HPP_

#include "mem_block.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <functional>

class string_buffer: public mem::block_ext //collect formatted text (printf-like syntax)
{
  public:
#if defined(NO_GCC_44) //temporary until evolve above gcc 4.4.7
    string_buffer () = default;                                    //default ctor
#else
    string_buffer ()
      : mem::block_ext ()
      , m_len (0)
    {}
#endif

    ~string_buffer ()
    {
      m_len = 0; //dtor
    }

    string_buffer (std::function<void (block &b, size_t n)> extend, std::function<void (block &b)> dealloc)
      : mem::block_ext {extend, dealloc}
      , m_len {0}
    {
    }

    using mem::block_ext::move_ptr;

    const char *get_buffer () const
    {
      return this->ptr;
    }

    void clear ()
    {
      if (ptr)
	{
	  m_len = 0;
	  ptr[m_len] = '\0';
	}
    }

    size_t len () const
    {
      // current content length, not including ending '\0' (similar with strlen(...))
      return m_len;
    }

    inline void operator+= (const char ch);                       //add a single char

    void add_bytes (size_t len, void *bytes);                     //add "len" bytes (can have '\0' in the middle)

    template<typename... Args> inline int operator() (Args &&... args); //add with printf format

  private:
    size_t m_len;                                                 //current content length not including ending '\0'

    string_buffer (const string_buffer &) = delete;               //copy ctor
    string_buffer (string_buffer &&) = delete;                    //move ctor
    void operator= (const string_buffer &) = delete;              //copy assign
    void operator= (string_buffer &&) = delete;                   //move assign
};

//implementation for small (inline) methods

void string_buffer::operator+= (const char ch)
{
  if (dim == 0)
    {
      extend (2); // 2 new bytes needed: ch + '\0'
    }
  else
    {
      assert (ptr[m_len] == '\0');

      // (m_len + 1) is the current number of chars including ending '\0'
      if (dim <= m_len + 1)
	{
	  extend (1);
	}
    }

  ptr[m_len] = ch;
  ptr[++m_len] = '\0';
}

template<typename... Args> int string_buffer::operator() (Args &&... args)
{
  int len = snprintf (NULL, 0, std::forward<Args> (args)...);

  assert (len >= 0);

  if (dim <= m_len + size_t (len) + 1)
    {
      extend (m_len + size_t (len) + 1 - dim); //ask to extend to fit at least additional len chars
    }

  snprintf (ptr + m_len, dim - m_len, std::forward<Args> (args)...);
  m_len += len;

  return len;
}

#endif /* _STRING_BUFFER_HPP_ */
