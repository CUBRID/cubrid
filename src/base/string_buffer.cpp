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
/*
 * hex_dump : appends a buffer containing hex dump of input and optionally the ASCII content
 *
 * in : input string
 * max_to_dump : maximum contents to dump from input
 * line_size : number of characters to dump in each line (counting from input)
 * print_ascii : if true, prints the ASCII content of input on the right of hex dump
 */
void
string_buffer::hex_dump (const string_buffer &in, const size_t max_to_dump,
                         const size_t line_size, const bool print_ascii)
{
  const char *buf = in.get_buffer ();
  size_t buf_len = std::min (max_to_dump, in.len ());

  return hex_dump (buf, buf_len, line_size, print_ascii);
}

void
string_buffer::hex_dump (const char *ptr, const size_t length, const size_t line_size, const bool print_ascii)
{
  const char *ptr_line = ptr;

  this->operator() ("  0000: ");
  for (int i = 0; i < length; i++)
    {
      this->operator() ("%02X ", (unsigned char) (*ptr++));
      if (print_ascii == true
	  && (i % line_size == (line_size - 1) || i == length - 1))
	{
	  const char *ptr_print;

	  if (i % line_size != (line_size - 1))
	    {
	      std::string spaces (3 * (line_size - 1 - (i % line_size)), ' ');
	      this->operator() ("%s", spaces.c_str ());
	    }

	  for (ptr_print = ptr_line; ptr_print < ptr; ptr_print++)
	    {
	      if (*ptr_print >= 32 && *ptr_print < 128)
		{
		  this->operator() ("%c", *ptr_print);
		}
	      else
		{
		  this->operator() (".");
		}
	    }

	  ptr_line += line_size;
	}

      if (i % line_size == (line_size - 1) && i != length)
	{
	  this->operator() ("\n  %04d: ", i + 1);
	}
    }
  this->operator() ("\n");
}
