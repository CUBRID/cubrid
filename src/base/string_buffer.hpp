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
#pragma once
#ifdef __linux__
#include <stddef.h>//size_t on Linux
#endif
#include <stdio.h>
#if 0//!!! snprintf() != _sprintf_p() when buffer capacity is exceeded !!! (disabled until further testing)
#ifdef _WIN32
#define snprintf                                                                                                       \
  _sprintf_p// snprintf() on Windows doesn't support positional parms but there is a similar function; snprintf_p();   \
            // on Linux supports positional parms by default
#endif
#endif

class string_buffer//collect formatted text (printf-like syntax)
{
  char* m_buf; // pointer to a memory buffer (not owned)
  size_t m_dim;// dimension|capacity of the buffer
  size_t m_len;// current length of the buffer content
public:
  string_buffer (size_t capacity = 0, char* buffer = 0);

  operator const char* () { return m_buf; }

  size_t len () { return m_len; }
  void clr () { m_buf[m_len = 0] = '\0'; }

  void set (size_t capacity, char* buffer);// associate with a new buffer[capacity]

  void operator() (size_t len, void* bytes);// add "len" bytes to internal buffer; "bytes" can have '\0' in the middle
  void operator+= (const char ch);

  template<size_t Size, typename... Args> void operator() (const char (&format)[Size], Args&&... args)
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
