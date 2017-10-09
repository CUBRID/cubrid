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
 *
 */

#include "test_extensible_array.hpp"

#include "test_memory_alloc_helper.hpp"
#include "extensible_array.cpp"

namespace test_memalloc
{

enum class test_string_buffer_types
{
  STRBUF_EXTENSIBLE_ARRAY,
  STRBUF_STD_STRING,
  STRBUF_CSTYLE_STRING,
  COUNT
};

const char *
enum_stringify_value (test_string_buffer_types value)
{
  switch (value)
    {
    case test_string_buffer_types::STRBUF_EXTENSIBLE_ARRAY:
      return "EXTENSIBLE_ARRAY";
    case test_string_buffer_types::STRBUF_STD_STRING:
      return "STD::STRING";
    case test_string_buffer_types::STRBUF_CSTYLE_STRING:
      return "CSTYLE_STRING";
    case test_string_buffer_types::COUNT:
    default:
      custom_assert (false);
      return NULL;
    }
}

typedef test_comparative_results<test_string_buffer_types> test_compare_string_buffers;
typedef test_compare_string_buffers::name_container_type test_compare_string_buffers_step_names;

template <size_t Size>
class cstyle_char_array
{
public:
  cstyle_char_array ()
  {
    ptr = buffer;
  }

  void append (const char * str, size_t len)
  {
#ifdef DEBUG
    if (buffer + Size - ptr < str.size ())
      {
        assert (false);
        return;
      }
#endif
    memcpy (ptr, str, len);
    ptr += len;
  }

private:
  char buffer [Size];
  char *ptr;
};

template <size_t Size>
inline void
append (const char * str, size_t len, cstyle_char_array<Size> & buf)
{
  buf.append (str, len);
}

template <size_t Size>
inline void
append (const char * str, size_t len, extensible_array<char, Size> & buf)
{
  buf.append (str, len);
}

inline void
append (const char * str, size_t len, std::string & buf)
{
  buf.append (str, len);
}

const test_compare_string_buffers_step_names test_append_strings_step_names = {{ "Successive appends" }};

template <typename Buf>
int
test_append_strings (test_compare_string_buffers & result, Buf & buf, test_string_buffer_types buf_type,
                     size_t append_size, unsigned append_count)
{
  char *str = new char [append_size];
  memset (str, ' ', append_size);

  /* start timing */
  unsigned step = 0;
  us_timer timer;

  for (unsigned count = 0; count < append_count; count++)
    {
      append (str, append_size, buf);
    }
  result.register_time (timer, buf_type, step++);

  custom_assert (step == result.get_step_count ());

  delete str;

  return 0;
}

template <size_t AppendSize, unsigned AppendCount>
void
test_compare_append_strings (int & global_error)
{
#define XARR_TYPE extensible_array<char, AppendSize * AppendCount, std::allocator<char> >

  test_compare_string_buffers compare_result (test_append_strings_step_names);
  size_t append_size = AppendSize;
  unsigned append_count = AppendCount;

  /* first test extensible array */
  std::allocator<char> std_alloc;
  XARR_TYPE xarr (std_alloc);
  run_test (global_error, test_append_strings<XARR_TYPE >,
            std::ref (compare_result), std::ref (xarr), test_string_buffer_types::STRBUF_EXTENSIBLE_ARRAY, append_size,
            append_count);

  /* test standard string */
  std::string str;
  run_test (global_error, test_append_strings<std::string>, std::ref (compare_result), std::ref (str),
            test_string_buffer_types::STRBUF_STD_STRING, append_size, append_count);

  /* test c-style char array */
  cstyle_char_array<AppendSize * AppendCount> charr;
  run_test (global_error, test_append_strings<cstyle_char_array<AppendSize * AppendCount>>, std::ref (compare_result),
            std::ref (charr), test_string_buffer_types::STRBUF_CSTYLE_STRING, append_size, append_count);

  std::cout << std::endl;
  compare_result.print_results_and_warnings (std::cout);

#undef XARR_TYPE
}

int test_extensible_array (void)
{
  int global_error = 0;

  test_compare_append_strings<SIZE_64, SIZE_ONE_K> (global_error);

  return global_error;
}

}  // namespace test_memalloc
