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
#include "allocator_stack.hpp"
#include "mem_block.hpp"
#include "string_buffer.hpp"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

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

const size_t N = 8192;

char stack_buf[sizeof (prefix) + N + sizeof (suffix)]; // working buffer
allocator::stack stack_allocator (stack_buf, sizeof (stack_buf));
allocator::affix<allocator::stack, prefix, suffix> affix_allocator (stack_allocator);

void temp_extend (cubmem::block &block, size_t len)
{
  cubmem::block b = affix_allocator.allocate (block.dim + len);
  memcpy (b.ptr, block.ptr, block.dim);
  affix_allocator.deallocate (std::move (block));
  block = std::move (b);
}

void temp_dealloc (cubmem::block &block)
{
  affix_allocator.deallocate (std::move (block));
}

cubmem::block_allocator AFFIX_BLOCK_ALLOCATOR { temp_extend, temp_dealloc };

class test_string_buffer
{
  private:
    size_t m_dim;                                      //_ref[_dim]
    size_t m_len;                                      // strlen(_ref)
    char *m_ref;                                       // reference buffer
    string_buffer m_sb;

  public:
    test_string_buffer ()
      : m_dim (8192)
      , m_len (0)
      , m_ref ((char *) calloc (m_dim, 1))
      , m_sb {AFFIX_BLOCK_ALLOCATOR}
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

    void check_resize (size_t len) //check internal buffer and resize it to fit additional len bytes
    {
      if (m_dim < m_len + len) // calc next power of 2
	{
	  do
	    {
	      m_dim *= 2;
	    }
	  while (m_dim < m_len + len);
	  m_ref = (char *) realloc (m_ref, m_dim);
	}
    }

    void operator() (size_t size) // prepare for a test with a buffer of <size> bytes
    {
      m_len = 0;
      stack_allocator.~stack ();
      m_sb.clear();
    }

    template<typename... Args> void format (const char *file, int line, Args &&... args)
    {
      int len = snprintf (NULL, 0, args...);
      if (len < 0)
	{
	  ERR ("[%s(%d)] StrBuf() snprintf()=%d", file, line, len);
	  return;
	}
      else
	{
	  check_resize (len);
	  len = snprintf (m_ref + m_len, m_dim - m_len, args...);
	  m_len += len;
	}
      m_sb (args...);
      if (m_sb.len () != m_len)//check length
	{
	  ERR ("[%s(%d)] StrBuf() len()=%zu expect %zu", file, line, m_sb.len (), m_len);
	  return;
	}
      if (strcmp (m_sb.get_buffer(), m_ref))//check content
	{
	  ERR ("[%s(%d)] StrBuf() {\"%s\"} expect{\"%s\"}", file, line, m_sb.get_buffer(), m_ref);
	  return;
	}
      const cubmem::block block { m_sb.len (), const_cast<char *> (m_sb.get_buffer ()) };
      if (affix_allocator.check (block))//check overflow
	{
	  ERR ("[%s(%d)] StrBuf() memory corruption", file, line);
	  return;
	}
    }

    void character (const char *file, int line, const char ch)
    {
      check_resize (1);
      m_ref[m_len] = ch;
      m_ref[++m_len] = '\0';

      m_sb += ch;
      if (m_sb.len () != m_len)//check length
	{
	  ERR ("[%s(%d)] StrBuf() len()=%zu expect %zu", file, line, m_sb.len (), m_len);
	  return;
	}
      if (strcmp (m_sb.get_buffer(), m_ref) != 0)//check content
	{
	  ERR ("[%s(%d)] StrBuf() {\"%s\"} expect {\"%s\"}", file, line, m_sb.get_buffer(), m_ref);
	  return;
	}
      const cubmem::block block { m_sb.len (), const_cast<char *> (m_sb.get_buffer ()) };
      if (affix_allocator.check (block))
	{
	  ERR ("[%s(%d)] StrBuf() memory corruption", file, line);
	}
    }
};
test_string_buffer test;
#define SB_FORMAT(frmt, ...) test.format (__FILE__, __LINE__, frmt, ##__VA_ARGS__)
#define SB_CHAR(ch) test.character (__FILE__, __LINE__, ch)

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

#if !defined (_MSC_VER) || (_MSC_VER >= 1700)
  if (flags & FL_TIME)
    {
      printf ("%.9lf ms (iterations: %zu)\n", std::chrono::duration<double, std::milli> (t1 - t0).count (), N);
    }
#endif

  return 0;
}
