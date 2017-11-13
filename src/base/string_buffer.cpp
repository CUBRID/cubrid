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

void string_buffer::add_bytes (size_t len, void *bytes)
{
  if (bytes && m_len + len < m_block.dim)
    {
      memcpy (m_block.ptr + m_len, bytes, len);
      m_block.ptr[m_len += len] = 0;
    }
  else
    {
      m_len += len;
    }
}

void string_buffer::operator+= (const char ch)
{
  if (m_block.dim < m_len + 2)
    {
      m_extend (m_block, 1);
    }
  m_block.ptr[m_len] = ch;
  m_block.ptr[++m_len] = '\0';
}
