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

/* String Buffer: collects formatted text (printf-like syntax) in a fixed size buffer
 * - useful to build a formatted string in successive function calls without dynamic memory allocation
 * - if the provided buffer is too small then the len() method can be used to find necessary size
 * (similar with snprintf() behavior)
 * to simplify the caller code and because this class has only one functionality (add text with format), I used
 * operator() instead of a named method (see below)
 *
 * Usage:
 *   char buffer[1024] = {0};
 *   string_buffer sb(sizeof(buffer), buffer);
 *   sb("simple text");                                   // <=> sb.add_with_format("simple text")
 *   sb("format i=%d f=%lf s=\"%s\"", 1, 2.3, "4567890");
 *   if(sb.len() >= sizeof(buffer)) // buffer capacity exceeded
 *     {
 *       // provide a bigger buffer or fail
 *     }
 *   printf(buffer); // => i=1 f=2.3 s="4567890"
 */

#ifndef _STRING_BUFFER_HPP_
#define _STRING_BUFFER_HPP_

#include "mem_block.hpp"

#include <stddef.h>
#include <stdio.h>
#include <functional>

class string_buffer //collect formatted text (printf-like syntax)
{
  public:
    string_buffer() = delete;                                     //default ctor
    ~string_buffer()
    {
      m_len=0; //dtor
    }
    string_buffer (const string_buffer &) = delete;               //copy ctor
    string_buffer (string_buffer &&) = delete;                    //move ctor

    inline string_buffer (mem::block_ext &block);                 //general ctor

    void operator= (const string_buffer &) = delete;              //copy operator
    void operator= (string_buffer &&) = delete;                   //move operator

    const char *get_buffer() const
    {
      return m_block.ptr;
    }
    inline void clear();
    size_t len() const
    {
      return m_len; //current content length
    }
    inline void operator+= (const char ch);                       //add a single char

    void add_bytes (size_t len, void *bytes);                     //add "len" bytes (can have '\0' in the middle)

    template<typename... Args> inline void operator() (Args &&... args); //add with printf format

  private:
    mem::block_ext &m_block;                                      //memory block (not owned, just used)
    size_t m_len;                                                 //current content length
};

//implementation for small (inline) methods

string_buffer::string_buffer (mem::block_ext &block)
  : m_block (block)
  , m_len {0}
{
  if (m_block.ptr)
    {
      m_block.ptr[0] = '\0';
    }
}

void string_buffer::clear()
{
  if (m_block.ptr)
    {
      m_block.ptr[m_len=0] = '\0';
    }
}

void string_buffer::operator+= (const char ch)
{
  if (m_block.dim < m_len + 2)
    {
      m_block.extend (1);
    }
  m_block.ptr[m_len] = ch;
  m_block.ptr[++m_len] = '\0';
}

template<typename... Args> void string_buffer::operator() (Args &&... args)
{
  int len = snprintf (nullptr, 0, std::forward<Args> (args)...);
  if (m_block.dim <= m_len + size_t (len) + 1)
    {
      m_block.extend (m_len + size_t (len) + 1 - m_block.dim); //ask to extend to fit at least additional len chars
    }
  snprintf (m_block.ptr + m_len, m_block.dim - m_len, std::forward<Args> (args)...);
  m_len += len;
}

#endif /* _STRING_BUFFER_HPP_ */
