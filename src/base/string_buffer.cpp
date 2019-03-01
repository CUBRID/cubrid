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
 * string_buffer.cpp
 */

#include "string_buffer.hpp"

#include <algorithm>  // for std::min
#include <memory.h>

void string_buffer::add_bytes (size_t len, char *bytes)
{
  assert (bytes != NULL);
  m_ext_block.extend_to (m_len + len + 2);
  memcpy (m_ext_block.get_ptr () + m_len, bytes, len);
  m_len += len;
  m_ext_block.get_ptr ()[m_len] = '\0';
}

void 
string_buffer::hex_dump (const string_buffer &in, string_buffer &out, const size_t max_to_dump,
                         const size_t line_size, const bool print_ascii)
{
  const char *ptr = in.get_buffer ();
  const char *ptr_line = ptr;
  size_t length = std::min (max_to_dump, in.len ());

  out ("  0000: ");
  for (int i = 0; i < length; i++)
    {
      out ("%02X ", (unsigned char) (*ptr++));
      if (print_ascii == true
          && (i % line_size == (line_size - 1) || i == length - 1))
        {
          const char *ptr_print;

          if (i % line_size != (line_size - 1))
            {
              std::string spaces (3 * (line_size - 1 - (i % line_size)), ' ');
              out ("%s", spaces.c_str ());
            }
          
          for (ptr_print = ptr_line; ptr_print < ptr; ptr_print++)
            {
              if (*ptr_print >= 32 && *ptr_print < 128)
                {
                  out ("%c", *ptr_print);
                }
              else
                {
                  out (".");
                }
            }

          ptr_line += line_size;
        }

      if (i % line_size == (line_size - 1) && i != length)
	{
	  out ("\n  %04d: ", i + 1);
	}
    }
  out ("\n");
}
