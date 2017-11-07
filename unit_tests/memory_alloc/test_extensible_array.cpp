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

/* own header */
#include "test_extensible_array.hpp"

/* header in same module */
#include "test_memory_alloc_helper.hpp"
#include "test_perf_compare.hpp"
#include "test_debug.hpp"
#include "test_output.hpp"

/* headers from cubrid */
#include "extensible_array.hpp"

/* system headers */
#include <iostream>
#include <typeinfo>
#include <array>

namespace test_memalloc
{

enum class test_string_buffer_types
{
  EXTENSIBLE_ARRAY,
  STD_STRING,
  CSTYLE_STRING,
  COUNT
};
test_common::string_collection string_buffer_names ("Extensible Array", "std::string", "C-Style String");

template <size_t Size>
class cstyle_char_array
{
  public:
    cstyle_char_array ()
    {
      ptr = buffer;
    }

    void append (const char *str, size_t len)
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

test_common::string_collection append_step_names ("Successive appends");

/* test_append_strings - append append_count strings of size append_size into given buffer.
 *
 *
 *  Template:
 *
 *      Buffer having member function append (const char *str, size_t len);
 *
 *
 *  How it works:
 *
 *      Loops and executes repeated appends (strings are filled with blank spaces) and times the duration of all append
 *      operations.
 */
template <typename Buf>
static int
test_append_strings (test_common::perf_compare &result, Buf &buf, test_string_buffer_types buf_type,
                     size_t append_size, unsigned append_count)
{
  static std::string log_string = std::string (4,' ') + PORTABLE_FUNC_NAME + "<" + typeid (Buf).name () + ">\n";
  test_common::sync_cout (log_string);

  char *str = new char [append_size];
  memset (str, ' ', append_size);

  /* start timing */
  unsigned step = 0;
  test_common::us_timer timer;

  for (unsigned count = 0; count < append_count; count++)
    {
      buf.append (str, append_size);
    }
  result.register_time (timer, static_cast <size_t> (buf_type), step++);

  test_common::custom_assert (step == result.get_step_count ());

  delete str;

  return 0;
}

/* test_compare_append_strings_performance -
 *
 *  Run the same number of same size string appends with three types of buffers: cubrid extensible array, std::string
 *  and C-Style static allocated array. The purpose is to have a performance as close as possible to C-Style array.
 *
 *  Templates:
 *
 *      AppendSize - size of one string
 *      AppendCount - number of append operations
 *
 *
 *  How it works:
 *
 *      Instantiates the timer collector which is passed to specialized test_append_strings functions. Results are
 *      printed after.
 *
 *
 *  How to call:
 *
 *      Just specialize the size and count of append operations.
 */
template <size_t AppendSize, unsigned AppendCount>
static void
test_compare_append_strings_performance (int &global_error)
{
  typedef extensible_array<char, AppendSize *AppendCount, std::allocator<char> > xarr_type;

  test_common::perf_compare compare_result (string_buffer_names, append_step_names);
  size_t append_size = AppendSize;
  unsigned append_count = AppendCount;

  /* first test extensible array */
  std::allocator<char> std_alloc;
  xarr_type xarr (std_alloc);
  run_test (global_error, test_append_strings<xarr_type >,
            std::ref (compare_result), std::ref (xarr), test_string_buffer_types::EXTENSIBLE_ARRAY, append_size,
            append_count);

  /* test standard string */
  std::string str;
  run_test (global_error, test_append_strings<std::string>, std::ref (compare_result), std::ref (str),
            test_string_buffer_types::STD_STRING, append_size, append_count);

  /* test c-style char array */
  cstyle_char_array<AppendSize *AppendCount> charr;
  run_test (global_error, test_append_strings<cstyle_char_array<AppendSize *AppendCount>>, std::ref (compare_result),
            std::ref (charr), test_string_buffer_types::CSTYLE_STRING, append_size, append_count);

  std::cout << std::endl;
  compare_result.print_results_and_warnings (std::cout);
}

/* test_extensible_array_correctness_append -
 *
 *  Run one append operation into both std::string and extensible array, then compare length and content.
 */
static void
test_extensible_array_correctness_append (int &global_error, test_common::perf_compare &test_compare,
    extensible_array<char, SIZE_64> &xarr_buf,
    std::string &string_buf, size_t append_size)
{
  run_test (global_error, test_append_strings<extensible_array<char, SIZE_64> >, test_compare, xarr_buf,
            test_string_buffer_types::EXTENSIBLE_ARRAY, append_size, 1);
  run_test (global_error, test_append_strings<std::string>, test_compare, string_buf,
            test_string_buffer_types::CSTYLE_STRING, append_size, 1);

  if (string_buf.size () != xarr_buf.get_size ())
    {
      /* incorrect size */
      std::cout << "  ERROR: extensible buffer size = " <<  xarr_buf.get_size ();
      std::cout << " is expected to be " << string_buf.size () << std::endl;
      global_error = global_error == 0 ? -1 : 0;
    }
  else if (std::strncmp (string_buf.c_str (), xarr_buf.get_array (), string_buf.size ()) != 0)
    {
      /* incorrect data */
      std::cout << "  ERROR: incorrect data" << std::endl;
      global_error = global_error == 0 ? -1 : 0;
    }
}

/* test_extensible_array_correctness -
 *
 *  Run several append operations over extensible array and string buffer. Their content should match (verified by
 *  test_extensible_array_correctness_append). The array is extended beyond its static size.
 */
static void
test_extensible_array_correctness (int &global_error)
{
  const size_t APPEND_COUNT = 6;

  std::string verifier;
  std::allocator<char> allocator;
  extensible_array<char, SIZE_64> xarr (allocator);

  test_common::perf_compare compare_result (string_buffer_names, append_step_names);

  std::array<size_t, APPEND_COUNT> append_sizes = {{ 1, 32, 64, 1024, 12, 8096 }};

  for (auto it = append_sizes.cbegin (); it != append_sizes.cend (); it++)
    {
      test_extensible_array_correctness_append (global_error, compare_result, xarr, verifier, *it);
    }
}

int
test_extensible_array (void)
{
  int global_error = 0;

  test_extensible_array_correctness (global_error);
  test_compare_append_strings_performance<SIZE_64, SIZE_ONE_K> (global_error);

  return global_error;
}

}  // namespace test_memalloc
