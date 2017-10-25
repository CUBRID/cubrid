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
#include "string_buffer.hpp"
#include <memory.h>

string_buffer::string_buffer (size_t capacity, char* buffer)
  : m_buf (buffer)
  , m_dim (capacity)
  , m_len (0)
{
  if (buffer)
    m_buf[0] = '\0';
}

void string_buffer::set (size_t capacity, char* buffer)
{// associate with a new buffer[capacity]
  m_buf = buffer;
  m_dim = capacity;
  if (m_buf)
    {
      m_buf[0] = '\0';
    }
  m_len = 0;
}

void string_buffer::operator() (size_t len, void* bytes)
{// add "len" bytes to internal buffer; "bytes" can have '\0' in the middle
  if (bytes && m_len + len < m_dim)
    {
      memcpy (m_buf + m_len, bytes, len);
      m_buf[m_len += len] = 0;
    }
  else
    {
      m_len += len;
    }
}

void string_buffer::operator+= (const char ch)
{
  if (m_len + 1 < m_dim)
    {// include also '\0'
      m_buf[m_len]     = ch;
      m_buf[m_len + 1] = '\0';
    }
  ++m_len;
}
