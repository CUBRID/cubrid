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

#if defined (LINUX)
#include <stddef.h> //size_t on Linux
#endif /* LINUX */

#include <stdio.h>

#if 0 //!!! snprintf() != _sprintf_p() when buffer capacity is exceeded !!! (disabled until further testing)
#ifdef _WIN32
#define snprintf                                                                                                       \
  _sprintf_p // snprintf() on Windows doesn't support positional parms but there is a similar function; snprintf_p();  \
// on Linux supports positional parms by default
#endif
#endif

class string_buffer //collect formatted text (printf-like syntax)
{
    char *m_buf;  // pointer to a memory buffer (not owned)
    size_t m_dim; // dimension|capacity of the buffer
    size_t m_len; // current length of the buffer content
  public:
    string_buffer (size_t capacity = 0, char *buffer = 0);

    size_t len ()
    {
      return m_len;
    }
    void clear ()
    {
      m_len = 0;
      m_buf[0] = '\0';
    }

    void set_buffer (size_t capacity, char *buffer); // associate with a new buffer[capacity]

    void operator() (size_t len, void *bytes); // add "len" bytes to internal buffer; "bytes" can have '\0' in the middle
    void operator+= (const char ch);

    template<size_t Size, typename... Args> void operator() (const char (&format)[Size], Args &&... args)
    {
      int len = snprintf (m_buf + m_len, m_len < m_dim ? m_dim - m_len : 0, format, args...);
      if (len >= 0)
	{
	  if (m_dim <= m_len + len)
	    {
	      // WRN not enough space in buffer
	    }
	  m_len += len;
	}
    }
};

#endif /* _STRING_BUFFER_HPP_ */
