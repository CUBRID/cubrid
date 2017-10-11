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

#include "db_private_allocator.hpp"

/* this hack */
#ifdef strlen
#undef strlen
#endif /* strlen */

#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>
#include <iomanip>
#include <thread>

namespace test_memalloc
{

/************************************************************************/
/* helpers                                                              */
/************************************************************************/

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

/* Sync output */
void sync_cout (const std::string & str);

/* thread entry wrapper */
class custom_thread_entry
{
public:
  custom_thread_entry ();
  ~custom_thread_entry ();
  THREAD_ENTRY * get_thread_entry ();
  void check_resource_leaks (void);

private:
  void start_resource_tracking (void);

  THREAD_ENTRY m_thread_entry;
  int m_rc_track_id;
};

template <typename Units>
class timer
{
public:
  /************************************************************************/
  /* timer                                                                */
  /************************************************************************/
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
  COUNT
} test_allocator_type;

const char *enum_stringify_value (test_allocator_type alloc_type);

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

extern const char *DECIMAL_SEPARATOR;
extern const char *TIME_UNIT;

const size_t TEST_RESULT_NAME_PRINT_LENGTH = 20;
const size_t TEST_RESULT_VALUE_PRINT_LENGTH = 8;
const size_t TEST_RESTUL_VALUE_PRECISION_LENGTH = 3;
const size_t TEST_RESULT_VALUE_TOTAL_LENGTH =
  TEST_RESULT_VALUE_PRINT_LENGTH + TEST_RESTUL_VALUE_PRECISION_LENGTH + std::strlen (DECIMAL_SEPARATOR)
  + std::strlen (TIME_UNIT);

/*  Collect runtime timer statistics with various scenarios and compare results.
 *  Must provide an enumerator with values from 0 to COUNT for each scenario.
 *
 *  An automatic warning system is implemented. First scenario (with enumerator value 0) is considered to be the tested
 *  target, which should have smaller times than other scenarios.
 *
 *  How to use:
 *
 *      To be provided.
 */
template <typename E>
class test_comparative_results
{
public:
  typedef std::vector<const char *> name_container_type;
  typedef unsigned long long value_type;
  typedef std::vector<value_type> value_container_type;
  
  test_comparative_results (const name_container_type & step_names)
  {
    m_step_names = step_names; /* copy names */

    /* init all values */
    m_enum_count = static_cast<size_t> (E::COUNT);
    m_values.resize (m_enum_count);
    for (size_t enum_val = 0; enum_val < m_enum_count; enum_val++)
      {
        m_values[enum_val].reserve (m_step_names.size ());
        for (size_t val_index = 0; val_index < m_step_names.size (); val_index++)
          {
            m_values[enum_val].push_back (0);
          }
      }

    /* make sure leftmost_column_length is big enough */
    m_leftmost_column_length = TEST_RESULT_NAME_PRINT_LENGTH;
    size_t name_length;
    for (auto name_iter = m_step_names.cbegin (); name_iter != m_step_names.cend (); name_iter++)
      {
        name_length = strlen (*name_iter) + 1;
        if (name_length > m_leftmost_column_length)
          {
            m_leftmost_column_length = name_length;
          }
      }
  }

  inline void
  register_time (us_timer & timer, E enum_val, size_t step_index)
  {
    value_type time = timer.time_and_reset ().count ();
    size_t enum_index = static_cast <size_t> (enum_val);

    /* can be called concurrently, so use atomic inc */
    (void) ATOMIC_INC_64 (&m_values[enum_index][step_index], time);
  }

  void
  print_results (std::ostream & output)
  {
    print_result_header (output);
    for (size_t row = 0; row < m_step_names.size (); row++)
      {
        print_result_row (row, output);
      }
    output << std::endl;
  }

  void
  print_warnings (std::ostream & output)
  {
    bool no_warnings = true;
    for (size_t row = 0; row < m_step_names.size (); row++)
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

  size_t
  get_step_count (void)
  {
    return m_step_names.size ();
  }

private:
  test_comparative_results (); // prevent implicit constructor
  test_comparative_results (const test_comparative_results& other); // prevent copy

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

    for (unsigned iter = 0; iter < m_enum_count; iter++)
      {
        print_alloc_name_column (enum_stringify_value (static_cast <E> (iter)), output);
      }
    output << std::endl;
  }

  inline void
  print_result_row (size_t row, std::ostream & output)
  {
    print_leftmost_column (m_step_names[row], output);

    for (unsigned iter = 0; iter < m_enum_count; iter++)
      {
        print_value (m_values[iter][row], output);
      }
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
    print_warning_header (no_warnings, output);
    print_leftmost_column (m_step_names[row], output);
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

  name_container_type m_step_names;
  std::vector<value_container_type> m_values;
  size_t m_leftmost_column_length;
  size_t m_enum_count;
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

} // namespace test_memalloc
#endif // !_TEST_MEMORY_ALLOC_HELPER_HPP_
