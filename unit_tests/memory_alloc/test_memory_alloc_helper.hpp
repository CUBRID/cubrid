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

#ifndef _TEST_MEMORY_ALLOC_HELPER_HPP_
#define _TEST_MEMORY_ALLOC_HELPER_HPP_

#include <iostream>
#include <chrono>
#include <cassert>
#include <cstring>

#include "db_private_allocator.hpp"

/************************************************************************/
/* helpers                                                              */
/************************************************************************/
#ifdef strlen
#undef strlen
#endif /* strlen */

const size_t SIZE_64 = 64;
const size_t SIZE_ONE_K = 1024;
const size_t SIZE_16_K = 16 * 1024;
const size_t SIZE_1_M = 1024 * 1024;
const size_t SIZE_100_M = SIZE_1_M * 100;

inline void
custom_assert (bool cond)
{
  if (!cond)
    {
      abort ();
    }
}

class custom_thread_entry
{
public:
  custom_thread_entry ()
    {
      memset (&m_thread_entry, 0, sizeof (m_thread_entry));

      m_thread_entry.private_heap_id = db_create_private_heap ();
      thread_rc_track_initialize (&m_thread_entry);

      start_resource_tracking ();
    }

  ~custom_thread_entry ()
  {
    assert (m_thread_entry.count_private_allocators == 0);
    check_resource_leaks ();

    db_clear_private_heap (&m_thread_entry, m_thread_entry.private_heap_id);
    thread_rc_track_finalize (&m_thread_entry);
  }

  THREAD_ENTRY * get_thread_entry ()
  {
    return &m_thread_entry;
  }

  void check_resource_leaks (void)
  {
    thread_rc_track_exit (&m_thread_entry, m_rc_track_id);
  }

private:

  void start_resource_tracking (void)
  {
    m_rc_track_id = thread_rc_track_enter (&m_thread_entry);
  }

  THREAD_ENTRY m_thread_entry;
  int m_rc_track_id;
};

template <typename Units>
class timer
{
public:
  inline timer ()
  {
    reset ();
  }
  
  inline Units time ()
  {
    return (std::chrono::duration_cast<Units> (get_now () - m_saved_time));
  }

  inline void reset ()
  {
    m_saved_time = get_now ();
  }

  inline Units time_and_reset ()
  {
    Units diff = time ();
    reset ();
    return diff;
  }

private:

  static inline std::chrono::system_clock::time_point get_now (void)
  {
    return std::chrono::system_clock::now ();
  }

  std::chrono::system_clock::time_point m_saved_time;
};

typedef class timer<std::chrono::milliseconds> ms_timer;
typedef class timer<std::chrono::microseconds> us_timer;

/* Mallocator */
template <typename T>
class mallocator
{
public:
  T* allocate (size_t size)
  {
    return (T *) malloc (size * sizeof (T));
  }
  void deallocate (T * pointer, size_t UNUSED (size))
  {
    free (pointer);
  }
  size_t max_size (void) const
  {
    return 0x7FFFFFFF;
  }
private:
};
template <class T, class U>
bool
operator==(const mallocator<T>&, const mallocator<U>&)
{
  return true;
}
template <class T, class U>
bool
operator!=(const mallocator<T>&, const mallocator<U>&)
{
  return false;
}

/* allocators */
typedef enum test_allocator_type
{
  ALLOC_TYPE_PRIVATE,
  ALLOC_TYPE_STANDARD,
  ALLOC_TYPE_MALLOC,
  ALLOC_TYPE_COUNT
} test_allocator_type;

/* Generic functions to allocate an allocator with thread entry argument. */
template <typename T>
void
init_allocator (custom_thread_entry & cte, std::allocator<T> *& alloc, test_allocator_type & type_out)
{
  alloc = new std::allocator<T> ();
  type_out = test_allocator_type::ALLOC_TYPE_STANDARD;
}
template <typename T>
void
init_allocator (custom_thread_entry & cte, mallocator<T> *& alloc, test_allocator_type & type_out)
{
  alloc = new mallocator<T> ();
  type_out = test_allocator_type::ALLOC_TYPE_MALLOC;
}
template <typename T>
void
init_allocator (custom_thread_entry & cte, db_private_allocator<T> *& alloc, test_allocator_type & type_out)
{
  alloc = new db_private_allocator<T> (cte.get_thread_entry ());
  type_out = test_allocator_type::ALLOC_TYPE_PRIVATE;
}

/* generic interface to collect results for all allocators and print them */

const char *DECIMAL_SEPARATOR = ".";
const char *TIME_UNIT = " usec";

const size_t TEST_RESULT_NAME_PRINT_LENGTH = 20;
const size_t TEST_RESULT_VALUE_PRINT_LENGTH = 8;
const size_t TEST_RESTUL_VALUE_PRECISION_LENGTH = 3;
const size_t TEST_RESULT_VALUE_TOTAL_LENGTH =
  TEST_RESULT_VALUE_PRINT_LENGTH + TEST_RESTUL_VALUE_PRECISION_LENGTH + std::strlen (DECIMAL_SEPARATOR)
  + std::strlen (TIME_UNIT);

/* Collect runtime statistics */
template <size_t Size>
class test_result
{
public:
  typedef std::array<const char *, Size> name_container_type;
  typedef unsigned long long value_type;
  typedef std::array<value_type, Size> value_container_type;
  
  test_result (const name_container_type & result_names)
  {
    m_names = result_names;

    /* init all values */
    for (unsigned alloc_type = 0; alloc_type < ALLOC_TYPE_COUNT; alloc_type++)
      {
        for (size_t val_index = 0; val_index < Size; val_index++)
          {
            m_values[alloc_type].at(val_index) = 0;
          }
      }

    /* make sure leftmost_column_length is big enough */
    m_leftmost_column_length = TEST_RESULT_NAME_PRINT_LENGTH;
    for (size_t name_index = 0; name_index < Size; name_index++)
      {
        if (strlen (result_names[name_index]) >= m_leftmost_column_length)
          {
            m_leftmost_column_length = strlen (result_names[name_index]) + 1;
          }
      }
  }

  inline void
  register_time (us_timer & timer, test_allocator_type alloc_type, size_t index)
  {
    value_type time = timer.time_and_reset ().count ();

    /* can be called concurrently, so use atomic inc */
    (void) ATOMIC_INC_64 (&m_values[alloc_type][index], time);
  }

  void
  print_results (std::ostream & output)
  {
    print_result_header (output);
    for (size_t row = 0; row < Size; row++)
      {
        print_result_row (row, output);
      }
    output << std::endl;
  }

  void
  print_warnings (std::ostream & output)
  {
    bool no_warnings = true;
    for (size_t row = 0; row < Size; row++)
      {
        check_row_and_print_warning (row, no_warnings, output);
      }
    if (!no_warnings)
      {
        output << std::endl;
      }
  }

  void
  print_results_and_warnings (std::ostream & output)
  {
    print_results (output);
    print_warnings (output);
  }

private:
  test_result (); // prevent implicit constructor
  test_result (const test_result& other); // prevent copy

  void
  print_value (value_type value, std::ostream & output)
  {
    output << std::right << std::setw (TEST_RESULT_VALUE_PRINT_LENGTH) << value / 1000;
    output << DECIMAL_SEPARATOR;
    output << std::setfill ('0') << std::setw (3) << value % 1000;
    output << std::setfill (' ') << TIME_UNIT;
  }

  inline void
  print_leftmost_column (const char *str, std::ostream & output)
  {
    output << "    ";   // prefix
    output << std::left << std::setw (m_leftmost_column_length) << str;
    output << ": ";
  }

  inline void
  print_alloc_name_column (const char *str, std::ostream & output)
  {
    output << std::right << std::setw (TEST_RESULT_VALUE_TOTAL_LENGTH) << str;
  }

  inline void
  print_result_header (std::ostream & output)
  {
    print_leftmost_column ("Results", output);
    print_alloc_name_column ("Private", output);
    print_alloc_name_column ("Standard", output);
    print_alloc_name_column ("Malloc", output);
    output << std::endl;
  }

  inline void
  print_result_row (size_t row, std::ostream & output)
  {
    print_leftmost_column (m_names[row], output);
    print_value (m_values[ALLOC_TYPE_PRIVATE][row], output);
    print_value (m_values[ALLOC_TYPE_STANDARD][row], output);
    print_value (m_values[ALLOC_TYPE_MALLOC][row], output);
    output << std::endl;
  }

  inline void
  print_warning_header (bool & no_warnings, std::ostream & output)
  {
    if (no_warnings)
      {
        output << "    Warnings:" << std::endl;
        no_warnings = false;
      }
  }

  inline void
  check_row_and_print_warning (size_t row, bool & no_warnings, std::ostream & output)
  {
    bool slower_than_standard = m_values[ALLOC_TYPE_PRIVATE] > m_values[ALLOC_TYPE_STANDARD];
    bool slower_than_malloc = m_values[ALLOC_TYPE_PRIVATE] > m_values[ALLOC_TYPE_MALLOC];
    if (!slower_than_malloc && !slower_than_standard)
      {
        return;
      }
    print_leftmost_column (m_names[row], output);
    if (slower_than_standard && slower_than_malloc)
      {
        output << "Private is slower than both Standard and Malloc";
      }
    else if (slower_than_standard)
      {
        output << "Private is slower than Standard";
      }
    else
      {
        output << "Private is slower than Malloc";
      }
    output << std::endl;
  }

  name_container_type m_names;
  value_container_type m_values[ALLOC_TYPE_COUNT];
  size_t m_leftmost_column_length;
};

/* run test and wrap with formatted text */
template <typename Func, typename ... Args>
void
run_test (int & global_err, Func && f, Args &&... args)
{
  std::cout << std::endl;
  std::cout << "    starting test - " << std::endl;;
  
  int err = f (std::forward<Args> (args)...);
  if (err == 0)
    {
      std::cout << "    test successful" << std::endl;
    }
  else
    {
      std::cout << "    test failed" << std::endl;
      global_err = global_err != 0 ? global_err : err;
    }
}

/* run test on multiple thread and wrap with formatted text */
template <typename Func, typename ... Args>
void
run_parallel (Func && f, Args &&... args)
{
  unsigned int worker_count = std::thread::hardware_concurrency ();
  worker_count = worker_count != 0 ? worker_count : 24;

  std::cout << std::endl;
  std::cout << "    starting test with " << worker_count << " concurrent threads - " << std::endl;;
  std::thread *workers = new std::thread [worker_count];

  for (unsigned int i = 0; i < worker_count; i++)
    {
      workers[i] = std::thread (std::forward<Func> (f), std::forward<Args> (args)...);
    }
  for (unsigned int i = 0; i < worker_count; i++)
    {
      workers[i].join ();
    }
  std::cout << "    finished test with " << worker_count << " concurrent threads - " << std::endl;;
  std::cout << std::endl;
}

#endif // !_TEST_MEMORY_ALLOC_HELPER_HPP_
