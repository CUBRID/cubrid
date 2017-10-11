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

/* timer - time durations between checkpoints.
 *
 *  Templates:
 *
 *      Units - desired duration unit.
 */
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

/* Specialization for microseconds and milliseconds */
typedef class timer<std::chrono::milliseconds> ms_timer;
typedef class timer<std::chrono::microseconds> us_timer;

/* name collections */
class string_collection
{
public:
  template <typename ... Args>
  string_collection (Args &... args)
  {
    register_names (args...);
  }

  size_t get_count () const;
  size_t get_max_length () const;
  const char * get_name (size_t name_index) const;

private:
  string_collection ();

  template <typename FirstArg, typename ... OtherArgs>
  void register_names (FirstArg & first, OtherArgs &... other)
  {
    m_names.push_back (first);
    register_names (other...);
  }

  template <typename OneArg>
  void register_names (OneArg & arg)
  {
    m_names.push_back (arg);
  }
  
  std::vector<std::string> m_names;
};

/* Mallocator - allocator concept to be used with malloc/free
 */
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
  PRIVATE,
  STANDARD,
  MALLOC,
  COUNT
} test_allocator_type;
const string_collection & get_allocator_names (void);

/* Generic functions to allocate an allocator with thread entry argument. */
template <typename T>
void
init_allocator (custom_thread_entry & cte, std::allocator<T> *& alloc, test_allocator_type & type_out)
{
  alloc = new std::allocator<T> ();
  type_out = test_allocator_type::STANDARD;
}
template <typename T>
void
init_allocator (custom_thread_entry & cte, mallocator<T> *& alloc, test_allocator_type & type_out)
{
  alloc = new mallocator<T> ();
  type_out = test_allocator_type::MALLOC;
}
template <typename T>
void
init_allocator (custom_thread_entry & cte, db_private_allocator<T> *& alloc, test_allocator_type & type_out)
{
  alloc = new db_private_allocator<T> (cte.get_thread_entry ());
  type_out = test_allocator_type::PRIVATE;
}

/*  Collect runtime timer statistics with various scenarios and compare results.
 *
 *
 *  How it works:
 *
 *      Stores timer values for each step of each scenario (it is assumed that each scenarios goes through same steps).
 *      This data structure is designed to be used by multiple threads concurrently (timers are incremented using
 *      atomic operations).
 *
 *
 *  How to use:
 *
 *      Instantiate using scenario names and step names (results will be printed using these names).
 *      Pass same instance to functions running all scenarios. Scenarios can run in parallel.
 *      The running function should time each step using register_time function.
 *      After running all scenarios, results can be printed.
 *      If first scenario is expected to be best, it can be checked against the other scenarios and warnings will be
 *      printed for each slower step.
 *      print_results_and_warnings can be used to print both.
 */
class test_compare_performance
{
public:
  
  /* instantiate with an vector containing step names. */
  test_compare_performance (const string_collection & scenarios, const string_collection & steps);

  /* register the time for step_index step of scenario_index scenario. multiple timers can stack on the same value
   * concurrently. */
  inline void register_time (us_timer & timer, size_t scenario_index, size_t step_index)
  {
    custom_assert (scenario_index < m_scenario_names.get_count ());
    custom_assert (step_index < m_step_names.get_count ());

    value_type time = timer.time_and_reset ().count ();

    /* can be called concurrently, so use atomic inc */
    (void) ATOMIC_INC_64 (&m_values[scenario_index][step_index], time);
  }

  /* print formatted results */
  void print_results (std::ostream & output);

  /* check where first scenario was worse than others and print warnings */
  void print_warnings (std::ostream & output);

  /* print both results and warnings */
  void print_results_and_warnings (std::ostream & output);

  /* get step count */
  size_t get_step_count (void);

private:
  typedef unsigned long long value_type;
  typedef std::vector<value_type> value_container_type;

  test_compare_performance (); // prevent implicit constructor
  test_compare_performance (const test_compare_performance& other); // prevent copy

  inline void print_value (value_type value, std::ostream & output);
  inline void print_leftmost_column (const char *str, std::ostream & output);
  inline void print_alloc_name_column (const char *str, std::ostream & output);
  inline void print_result_header (std::ostream & output);
  inline void print_result_row (size_t row, std::ostream & output);
  inline void print_warning_header (bool & no_warnings, std::ostream & output);
  inline void check_row_and_print_warning (size_t row, bool & no_warnings, std::ostream & output);

  const string_collection & m_scenario_names;
  const string_collection & m_step_names;

  std::vector<value_container_type> m_values;
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

} // namespace test_memalloc
#endif // !_TEST_MEMORY_ALLOC_HELPER_HPP_
