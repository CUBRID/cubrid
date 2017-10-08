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

#include "test_db_private_alloc.hpp"

#include <array>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <thread>
#include <sstream>
#include <typeinfo>
#include <iomanip>

#include "db_private_allocator.hpp"
#include "test_memory_alloc_helper.hpp"

/************************************************************************/
/* Helpers                                                              */
/************************************************************************/

/* Sync output */
std::mutex output_mutex;

void
sync_output (const std::string & str)
{
  std::lock_guard<std::mutex> lock(output_mutex);
  std::cout << str;
}

/* Collect runtime statistics */
typedef enum stat_offset
{
  STATOFF_ALLOC_AND_DEALLOC,
  STATOFF_SUCCESSIVE_ALLOCS,
  STATOFF_SUCCESSIVE_DEALLOCS,
  STATOFF_COUNT
} stat_offset;
const  char * statoff_names [] =
  {
    "Alloc & Dealloc",
    "Successive Allocs",
    "Successive deallocs"
  };

const char *statoff_get_name (stat_offset enum_val)
{
  return statoff_names[enum_val];
}

typedef unsigned long long stat_type;
typedef std::array<stat_type, stat_offset::STATOFF_COUNT> stat_array;

void
register_performance_time (us_timer & timer, stat_type & to)
{
  stat_type time_count = timer.time_and_reset ().count ();
  
  ATOMIC_INC_64 (&to, time_count);
  std::cout << time_count;
}

const size_t STATOFF_PRINT_LENGTH = 20;
const size_t USEC_PRINT_LENGTH = 10;

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

/* Generic functions to allocate an allocator with thread entry argument. */
template <typename T>
void
init_allocator (custom_thread_entry & cte, std::allocator<T> *& alloc)
{
  alloc = new std::allocator<T> ();
}
template <typename T>
void
init_allocator (custom_thread_entry & cte, mallocator<T> *& alloc)
{
  alloc = new mallocator<T> ();
}
template <typename T>
void
init_allocator (custom_thread_entry & cte, db_private_allocator<T> *& alloc)
{
  alloc = new db_private_allocator<T> (cte.get_thread_entry ());
}

/* Base function to test private allocator functionality */
template <typename T>
static int
test_private_allocator ()
{
  custom_thread_entry cte;

  static std::string prefix = std::string (4,' ') + PORTABLE_FUNC_NAME + "<" + typeid(T).name () + ">: ";

  db_private_allocator<T> private_alloc (cte.get_thread_entry ());

  std::cout << prefix << "alloc 64" << std::endl;
  T *ptr = private_alloc.allocate (SIZE_64);
  *ptr = T();
  *(ptr + SIZE_64 - 1) = T();
  private_alloc.deallocate (ptr);
  cte.check_resource_leaks ();

  std::cout << prefix << "alloc 1M" << std::endl;
  ptr = private_alloc.allocate(SIZE_1_M);
  *ptr = T ();
  *(ptr + SIZE_1_M - 1) = T ();
  private_alloc.deallocate (ptr);
  cte.check_resource_leaks ();

  std::cout << prefix << "alloc 64x64" << std::endl;
  std::array<T *, SIZE_64> ptr_array;
  for (size_t i = 0; i < ptr_array.size (); i++)
    {
      ptr_array[i] = private_alloc.allocate (SIZE_64);
      *ptr_array[i] = T ();
      *(ptr_array[i] + SIZE_64 - 1) = T ();
    }
  for (size_t i = 0; i < ptr_array.size (); i++)
    {
      private_alloc.deallocate (ptr_array[i]);
    }
  cte.check_resource_leaks ();

  /* test containers */
  std::vector<T, db_private_allocator<T>> vec (private_alloc);
  vec.resize (SIZE_64);
  vec.resize (SIZE_ONE_K);
  vec.resize (SIZE_16_K);
  vec.clear ();
  cte.check_resource_leaks ();

  return 0;
}

/* Basic function to test allocator performance
 *
 *  return:
 *
 *      always 0
 *
 *
 *  arguments:
 *   
 *      size - test size; translates into the number of pointers allocated for each sub-test
 *      time_collect - array to collect statistics
 */
template <typename T, typename Alloc >
int
test_performance_alloc (size_t size, stat_array & time_collect)
{
  custom_thread_entry cte;

  static const char * funcname = PORTABLE_FUNC_NAME;
  static std::stringstream prefix (std::string ("    ") + funcname + "<" + typeid(T).name() + ","
                                   + typeid(Alloc).name() + ">: ");
  std::stringstream log (std::string (prefix.str()));

  Alloc *alloc = NULL;
  init_allocator<T> (cte, alloc);

  us_timer timer;
  T *ptr = NULL;
  log << prefix.str () << "alloc + dealloc + alloc + dealloc ... = ";
  for (size_t i = 0; i < size; i++)
    {
      ptr = alloc->allocate (1);
      alloc->deallocate (ptr, 1);
    }
  register_performance_time (timer, time_collect[stat_offset::STATOFF_ALLOC_AND_DEALLOC]);
  log << std::endl;

  log << prefix.str () << "alloc + alloc + ... = ";
  T** pointers = new T * [size];
  for (size_t i = 0; i < size; i++)
    {
      pointers[i] = alloc->allocate (1);
    }
  register_performance_time (timer, time_collect[stat_offset::STATOFF_SUCCESSIVE_ALLOCS]);
  log << std::endl;
  log << prefix.str () << "dealloc + dealloc ... = ";
  for (size_t i = 0; i < size; i++)
    {
      alloc->deallocate (pointers[i], 1);
    }
  register_performance_time (timer, time_collect[stat_offset::STATOFF_SUCCESSIVE_DEALLOCS]);
  log << std::endl;

  delete pointers;
  delete alloc;

  sync_output (log.str ());

  return 0;
}

/* run test and wrap with formatted text */
template <typename Func, typename ... Args>
void
run_test (int & global_err, Func * f, Args &... args)
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
run_parallel (Func * f, Args &... args)
{
  unsigned int worker_count = std::thread::hardware_concurrency ();
  worker_count = worker_count != 0 ? worker_count : 24;

  std::cout << std::endl;
  std::cout << "    starting test with " << worker_count << " concurrent threads - " << std::endl;;
  std::thread *workers = new std::thread [worker_count];

  for (unsigned int i = 0; i < worker_count; i++)
    {
      workers[i] = std::thread (f, std::forward<Args> (args)...);
    }
  for (unsigned int i = 0; i < worker_count; i++)
    {
      workers[i].join ();
    }
}

/* print comparative results obtained from testing private, standard and malloc allocators.
 * print warnings when private allocator results are worse than standard/mallocator.
 */
void
print_and_compare_results (stat_array & private_results, stat_array & std_results, stat_array & malloc_results)
{
  stat_offset enum_val;

  /* print all results */
  std::cout << "    ";
  std::cout << std::setw (STATOFF_PRINT_LENGTH) << " ";
  std::cout << std::setw (USEC_PRINT_LENGTH) << "private";
  std::cout << std::setw (USEC_PRINT_LENGTH) << "standard";
  std::cout << std::setw (USEC_PRINT_LENGTH) << "malloc";
  std::cout << std::endl;
  for (int iter = 0; iter < stat_offset::STATOFF_COUNT; iter++)
    {
      enum_val = static_cast <stat_offset> (iter);
      std::cout << "    ";
      std::cout << std::setw (STATOFF_PRINT_LENGTH) << statoff_get_name (enum_val);
      std::cout << ": ";
      std::cout << std::setw (USEC_PRINT_LENGTH) << private_results[iter];
      std::cout << std::setw (USEC_PRINT_LENGTH) << std_results[iter];
      std::cout << std::setw (USEC_PRINT_LENGTH) << malloc_results[iter];
    }
  /* print slow private warnings: */
  bool no_warnings = true;
  bool worse_than_std;
  bool worse_than_malloc;
  for (int iter = 0; iter < stat_offset::STATOFF_COUNT; iter++)
    {
      worse_than_std = private_results[iter] >= std_results[iter];
      worse_than_malloc = private_results[iter] >= malloc_results[iter];
      if (!worse_than_std && !worse_than_malloc)
        {
          continue;
        }
      if (no_warnings)
        {
          /* print warnings: */
          std::cout << "    Warnings:" << std::endl;
          no_warnings = false;
        }
      enum_val = static_cast <stat_offset> (iter);
      if (worse_than_std)
        {
          std::cout << "    ";
          std::cout << std::setw (STATOFF_PRINT_LENGTH) << statoff_get_name (enum_val);
          std::cout << ": ";
          std::cout << "Private worse than standard";
          std::cout << std::endl;
        }
      if (worse_than_malloc)
        {
          std::cout << "    ";
          std::cout << std::setw (STATOFF_PRINT_LENGTH) << statoff_get_name (enum_val);
          std::cout << ": ";
          std::cout << "Private worse than malloc";
          std::cout << std::endl;
        }
    }
}

/* run single-thread tests with private, standard and malloc allocators and wrap with text */
template <typename T, size_t Size>
void
test_and_compare (int & global_error)
{
  size_t size = Size;
  stat_array time_collect_private;
  stat_array time_collect_std;
  stat_array time_collect_malloc;

  std::cout << "    start single-thread comparison test between allocators using type = " << typeid(T).name ();
  std::cout << " and size = " << size << std::endl;

  run_test (global_error, test_performance_alloc<T, db_private_allocator<T> >, size,
            std::reference_wrapper<stat_array> (time_collect_private));
  run_test (global_error, test_performance_alloc<T, std::allocator<T> >, size,
            std::reference_wrapper<stat_array> (time_collect_std));
  run_test (global_error, test_performance_alloc<T, mallocator<T> >, size,
            std::reference_wrapper<stat_array> (time_collect_malloc));

  std::cout << std::endl;
  print_and_compare_results (time_collect_private, time_collect_std, time_collect_malloc);
  std::cout << std::endl;
}

/* run multi-thread tests with private, standard and malloc allocators and wrap with text */
template <typename T, size_t Size>
void
test_and_compare_parallel (int & global_error)
{
  size_t size = Size;
  stat_array time_collect_private;
  stat_array time_collect_std;
  stat_array time_collect_malloc;

  std::cout << "    start multi-thread comparison test between allocators using type = " << typeid(T).name ();
  std::cout << " and size = " << size << std::endl;

  run_parallel (test_performance_alloc<T, db_private_allocator<T> >, size,
                std::reference_wrapper<stat_array> (time_collect_private));
  run_parallel (test_performance_alloc<T, std::allocator<T> >, size,
                std::reference_wrapper<stat_array> (time_collect_std));
  run_parallel (test_performance_alloc<T, mallocator<T> >, size,
                std::reference_wrapper<stat_array> (time_collect_malloc));

  std::cout << std::endl;
  print_and_compare_results (time_collect_private, time_collect_std, time_collect_malloc);
  std::cout << std::endl;
}

/* main for test_db_private_alloc function */
int
test_db_private_alloc ()
{
  std::cout << PORTABLE_FUNC_NAME << std::endl;

  int global_err = 0;

  typedef std::array<size_t, SIZE_64> dummy_size_512;
  typedef std::array<size_t, SIZE_ONE_K> dummy_size_8k;

  const size_t TEN_K = SIZE_ONE_K * 10;
  const size_t HUNDRED_K = SIZE_ONE_K * 100;

  run_test (global_err, test_private_allocator<char>);
  run_test (global_err, test_private_allocator<int>);
  run_test (global_err, test_private_allocator<dummy_size_512>);
  
  test_and_compare<char, HUNDRED_K> (global_err);
  test_and_compare<size_t, HUNDRED_K> (global_err);
  test_and_compare<dummy_size_512, HUNDRED_K> (global_err);
  test_and_compare<dummy_size_8k, TEN_K> (global_err);

  test_and_compare_parallel<dummy_size_512, HUNDRED_K> (global_err);
  test_and_compare_parallel<dummy_size_8k, TEN_K> (global_err);

  std::cout << std::endl;

  return global_err;
}

