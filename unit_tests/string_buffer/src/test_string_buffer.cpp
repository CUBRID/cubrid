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
#include "allocator_affix.hpp"
#include "allocator_block.hpp"
#include "allocator_stack.hpp"
#include "string_buffer.hpp"

#include <assert.h>
#include <chrono>

#if defined (LINUX)
#include <stddef.h> //size_t on Linux
#endif /* LINUX */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 8192

#define ERR(format, ...) printf ("ERR " format "\n", __VA_ARGS__)
#define WRN(format, ...) printf ("WRN " format "\n", __VA_ARGS__)

struct prefix
{
  char buf[66];
  prefix ()
  {
    memset (buf, 0xBB, sizeof (buf));
  }
};
struct suffix
{
  char buf[64];
  suffix ()
  {
    memset (buf, 0xEE, sizeof (buf));
  }
};

class test_string_buffer
{
  private:
    char m_buf[sizeof (prefix) + N + sizeof (suffix)]; // working buffer
    size_t m_dim;                                      //_ref[_dim]
    size_t m_len;                                      // sizeof(_ref)
    char *m_ref;                                       // reference buffer
    allocator::stack m_stack_allocator;
    allocator::affix<allocator::stack, prefix, suffix> m_affix_allocator;
    allocator::block m_block;
    string_buffer m_sb;

  public:
    test_string_buffer ()
      : m_buf ()
      , m_dim (1024)
      , m_len (0)
      , m_ref ((char *) calloc (m_dim, 1))
      , m_stack_allocator (m_buf, sizeof (m_buf))
      , m_affix_allocator (m_stack_allocator)
      , m_block ()
      , m_sb ()
    {
    }

    ~test_string_buffer ()
    {
      if (m_ref != 0)
	{
	  free (m_ref);
	  m_ref = 0;
	}
    }

    void operator() (size_t size) // prepare for a test with a buffer of <size> bytes
    {
      if (m_dim < size) // adjust internal buffer is necessary
	{
	  do
	    {
	      m_dim *= 2;
	    }
	  while (m_dim < size);
	  m_ref = (char *) realloc (m_ref, m_dim);
	}
      m_len = 0;
      m_stack_allocator.~stack ();
      m_block = m_affix_allocator.allocate (size);
      m_sb.set_buffer (size, m_block.ptr);
    }

    template<size_t Size, typename... Args>
    void operator() (const char *file, int line, const char (&format)[Size], Args &&... args)
    {
      int len = snprintf (m_ref + m_len, m_len < m_block.dim ? m_block.dim - m_len : 0, format, args...);
      if (len < 0)
	{
	  ERR ("[%s(%d)] StrBuf([%zu]) snprintf()=%d", file, line, m_block.dim, len);
	  return;
	}
      else
	{
	  m_len += len;
	}
      m_sb (format, args...);
      if (m_sb.len () != m_len)
	{
	  ERR ("[%s(%d)] StrBuf([%zu]) len()=%zu expect %zu", file, line, m_block.dim, m_sb.len (), m_len);
	  return;
	}
      if (strcmp (m_block.ptr, m_ref))
	{
	  ERR ("[%s(%d)] StrBuf([%zu]) {\"%s\"} expect{\"%s\"}", file, line, m_block.dim, m_block.ptr, m_ref);
	  return;
	}
      if (m_affix_allocator.check (m_block))
	{
	  ERR ("[%s(%d)] StrBuf(buf[%zu]) memory corruption", file, line, m_block.dim);
	  return;
	}
    }

    void operator() (const char *file, int line, char ch)
    {
      if (m_len + 1 < m_block.dim) // include also ending '\0'
	{
	  m_ref[m_len] = ch;
	  m_ref[m_len + 1] = '\0';
	}
      ++m_len;

      m_sb += ch;
      if (strcmp (m_block.ptr, m_ref) != 0)
	{
	  ERR ("[%s(%d)] StrBuf([%zu]) {\"%s\"} expect {\"%s\"}", file, line, m_block.dim, m_block.ptr, m_ref);
	  return;
	}
      if (m_affix_allocator.check (m_block))
	{
	  ERR ("[%s(%d)] StrBuf([%zu]) memory corruption", file, line, m_block.dim);
	}
    }
};
test_string_buffer test;

#define SB_FORMAT(format, ...) test (__FILE__, __LINE__, format, ##__VA_ARGS__)
#define SB_CHAR(ch) test (__FILE__, __LINE__, ch)

int main (int argc, char **argv)
{
  enum flags
  {
    FL_HELP = (1 << 0),
    FL_DEBUG = (1 << 1),
    FL_TIME = (1 << 2),
  };
  unsigned flags = 0;
  for (int i = 1; i < argc; ++i)
    {
      unsigned command = argv[i][0] << 24 | argv[i][1] << 16 | argv[i][2] << 8 | argv[i][3]; // little endian
      switch (command)
	{
	case 'help':
	  flags |= FL_HELP;
	  break;
	case 'dbug':
	  flags |= FL_DEBUG;
	  break;
	case 'time':
	  flags |= FL_TIME;
	  break;
	}
    }
  if (flags & FL_HELP)
    {
      printf ("%s [dbug] [time]\n", argv[0]);
      return 0;
    }
  if (flags & FL_DEBUG)
    {
      printf ("%s\n", argv[0]);
      for (int i = 1; i < argc; ++i)
	{
	  printf ("    %s\n", argv[i]);
	}
    }

  auto t0 = std::chrono::high_resolution_clock::now ();
  for (size_t n = 1; n < N; ++n)
    {
      test (n);
      SB_FORMAT ("simple text.");
      SB_FORMAT ("another.");
      SB_CHAR ('x');
      SB_CHAR ('.');
      SB_FORMAT ("format with %d argument.", 1);
      SB_FORMAT ("%1$04d-%2$02d-%3$02d.", 1973, 11, 28);
      SB_FORMAT ("%3$02d.%2$02d.%1$04d.", 1973, 11, 28);
    }
  auto t1 = std::chrono::high_resolution_clock::now ();

  if (flags & FL_TIME)
    {
      printf ("%.9lf ms\n", std::chrono::duration<double, std::milli> (t1 - t0).count ());
    }

  return 0;
}
