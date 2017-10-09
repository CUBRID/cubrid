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
#include <utility>

#include "test_memory_alloc_helper.hpp"

namespace test_memalloc
{

/************************************************************************/
/* Unit helpers                                                         */
/************************************************************************/

/* Sync output */
std::mutex output_mutex;

void
sync_output (const std::string & str)
{
  std::lock_guard<std::mutex> lock(output_mutex);
  std::cout << str;
}

#define FUNC_ALLOCS_AS_ARGS(fn_arg, type_arg) \
  fn_arg<db_private_allocator<type_arg> >, \
  fn_arg<std::allocator<type_arg> >, \
  fn_arg<mallocator<type_arg> >

#define FUNC_TYPE_AND_ALLOCS_AS_ARGS(fn_arg, type_arg) \
  fn_arg<type_arg, db_private_allocator<type_arg> >, \
  fn_arg<type_arg, std::allocator<type_arg> >, \
  fn_arg<type_arg, mallocator<type_arg> >

typedef test_comparative_results<test_allocator_type> test_compare_allocators;
typedef test_compare_allocators::name_container_type test_step_names;

/* run single-thread tests with private, standard and malloc allocators and wrap with text */
template <typename FuncPrv, typename FuncStd, typename FuncMlc, typename ... Args>
void
test_and_compare_single (int & global_error, const test_step_names & step_names, FuncPrv && fn_private,
                         FuncStd && fn_std, FuncMlc && fn_malloc, Args &&... args)
{
  test_compare_allocators result (step_names);

  run_test (global_error, fn_private, std::ref (result), std::forward<Args> (args)...);
  run_test (global_error, fn_std, std::ref (result), std::forward<Args> (args)...);
  run_test (global_error, fn_malloc, std::ref (result), std::forward<Args> (args)...);
  std::cout << std::endl;

  result.print_results_and_warnings (std::cout);
}

/* run multi-thread tests with private, standard and malloc allocators and wrap with text */
template <typename FuncPrv, typename FuncStd, typename FuncMlc, typename ... Args>
void
test_and_compare_parallel (int & global_error, const test_step_names & step_names, FuncPrv && fn_private,
                           FuncStd && fn_std, FuncMlc && fn_malloc, Args &&... args)
{
  test_compare_allocators result (step_names);

  run_parallel (fn_private, std::ref (result), std::forward<Args> (args)...);
  run_parallel (fn_std, std::ref (result), std::forward<Args> (args)...);
  run_parallel (fn_malloc, std::ref (result), std::forward<Args> (args)...);
  std::cout << std::endl;

  result.print_results_and_warnings (std::cout);
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

const test_step_names TEST_BASIC_PERF_STEP_NAMES =
  {{ "Alternate Alloc/Dealloc", "Successive Allocs", "Successive Deallocs" }};

/* Basic function to test allocator performance
 *
 *  return:
 *
 *      always 0
 *
 *
 *  arguments:
 *   
 *      alloc_count - test alloc_count; translates into the number of pointers allocated for each sub-test
 *      results - test result collector
 */
template <typename T, typename Alloc >
int
test_basic_performance (test_compare_allocators & results, size_t alloc_count)
{
  custom_thread_entry cte;    /* thread entry wrapper */

  /* function header */
  static std::string function_header =
    std::string ("    basic_perf") + typeid(T).name() + "," + typeid(Alloc).name() + ">\n";

  sync_output (function_header);

  /* instantiate allocator */
  Alloc *alloc = NULL;
  test_allocator_type alloc_type; 
  init_allocator<T> (cte, alloc, alloc_type);

  /* init step and timer */
  unsigned step = 0;
  us_timer timer;

  /* step 0: test allocate / deallocate / allocate / deallocate and so on */
  T *ptr = NULL;
  for (size_t i = 0; i < alloc_count; i++)
    {
      ptr = alloc->allocate (1);
      alloc->deallocate (ptr, 1);
    }
  results.register_time (timer, alloc_type, step++);

  /* step 1: test allocate / allocate and so on*/
  T** pointers = new T * [alloc_count];
  for (size_t i = 0; i < alloc_count; i++)
    {
      pointers[i] = alloc->allocate (1);
    }
  results.register_time (timer, alloc_type, step++);

  /* step 2: test deallocate / deallocate and so on*/
  for (size_t i = 0; i < alloc_count; i++)
    {
      alloc->deallocate (pointers[i], 1);
    }
  results.register_time (timer, alloc_type, step++);

  custom_assert (step == TEST_BASIC_PERF_STEP_NAMES.size ());

  delete pointers;
  delete alloc;

  return 0;
}

/* test_and_compare implementation for test basic */
template <typename T>
void
test_and_compare_single_basic (int & global_error, size_t alloc_count)
{
  test_and_compare_single (global_error, TEST_BASIC_PERF_STEP_NAMES,
                           FUNC_TYPE_AND_ALLOCS_AS_ARGS (test_basic_performance, T), alloc_count);
}

template <typename T>
void
test_and_compare_parallel_basic (int & global_error, size_t alloc_count)
{
  test_and_compare_parallel (global_error, TEST_BASIC_PERF_STEP_NAMES,
                             FUNC_TYPE_AND_ALLOCS_AS_ARGS (test_basic_performance, T), alloc_count);
}

class random_values
{
public:
  random_values (size_t size)
  {
    m_values.reserve (size);
    
    std::srand (static_cast<unsigned int> (std::time (0)));
    for (size_t i = 0; i < size; i++)
      {
        m_values.push_back (std::rand ());
      }
  }

  inline unsigned get_at (size_t index) const
  {
    return m_values[index];
  }

  inline unsigned get_at (size_t index, unsigned modulo) const
  {
    return (m_values[index] % modulo);
  }

  inline size_t get_size (void) const
  {
    return m_values.size ();
  }

private:
  random_values ();  /* no implicit constructor */

  std::vector<unsigned> m_values;
};

const std::vector <const char *> TEST_RANDOM_PERF_STEP_NAMES =
  { { "Random Alloc/Dealloc" } };

template <typename Alloc>
int
test_performance_random (test_compare_allocators & result, size_t ptr_pool_size, unsigned alloc_count,
                         const random_values & actions)
{
/* local definition to allocate a pointer and save it in pool at ptr_index_ */
#define PTR_ALLOC(ptr_index_) \
  do { \
    /* get random index in pointer size array. */ \
    size_t ptr_size_index = actions.get_at (random_value_cursor++, PTR_MEMSIZE_COUNT); \
    /* get pointer size */ \
    size_t ptr_size = ptr_sizes[ptr_size_index]; \
    /* allocate pointer */ \
    char *ptr = alloc->allocate (ptr_size); \
    /* save pointer and its size */; \
    pointers_pool[ptr_index_] = std::make_pair (ptr, ptr_size); \
  } while (false)

  typedef std::pair<char *, size_t> ptr_with_size;

  const unsigned PTR_MEMSIZE_COUNT = 14;
  static const std::array<size_t, PTR_MEMSIZE_COUNT> ptr_sizes =
    {{ 1,  64, 64, 64, 64, 64, 256, 1024, 1024, 1024, 4096, 8192, 16384, 65536 }};

  custom_thread_entry cte;    /* thread entry wrapper */

  /* function header */
  static std::string function_header = std::string ("    random_perf") + "<" + typeid(Alloc).name () + ">\n";
  sync_output (function_header);

  /* instantiate allocator */
  Alloc *alloc = NULL;
  test_allocator_type alloc_type;
  init_allocator<char> (cte, alloc, alloc_type);

  /* */
  ptr_with_size *pointers_pool = new ptr_with_size [ptr_pool_size];
  size_t ptr_index;

  /* init step and timer */
  unsigned step = 0;
  unsigned random_value_cursor = 0;
  us_timer timer;

  /* first step: fill pool */
  for (ptr_index = 0; ptr_index < ptr_pool_size; ptr_index++)
    {
      PTR_ALLOC (ptr_index);
    }

  /* second step: deallocate and allocate a new pointer */
  for (size_t count = 0; count < alloc_count; count++)
    {
      ptr_index = actions.get_at (random_value_cursor++, static_cast<unsigned> (ptr_pool_size));
      /* deallocate current pointer */
      alloc->deallocate (pointers_pool[ptr_index].first, pointers_pool[ptr_index].second);
      /* replace with new pointer */
      PTR_ALLOC (ptr_index);
    }

  /* free pool */
  for (ptr_index = 0; ptr_index < ptr_pool_size; ptr_index++)
    {
      alloc->deallocate (pointers_pool[ptr_index].first, pointers_pool[ptr_index].second);
    }

  result.register_time (timer, alloc_type, step++);

  custom_assert (random_value_cursor == actions.get_size ());
  custom_assert (step == TEST_RANDOM_PERF_STEP_NAMES.size ());

  delete pointers_pool;
  delete alloc;

  return 0;

#undef PTR_ALLOC
}

/* test_and_compare implementation for test random */
void
test_and_compare_single_random (int & global_error, size_t ptr_pool_size, unsigned alloc_count)
{
  /* create random values */
  size_t random_value_count = ptr_pool_size + 2 * alloc_count;
  random_values actions (random_value_count);

  test_and_compare_single (global_error, TEST_RANDOM_PERF_STEP_NAMES,
                           FUNC_ALLOCS_AS_ARGS (test_performance_random, char), ptr_pool_size, alloc_count,
                           std::cref (actions));
}

void
test_and_compare_parallel_random (int & global_error, size_t ptr_pool_size, unsigned alloc_count)
{
  /* create random values */
  size_t random_value_count = ptr_pool_size + 2 * alloc_count;
  random_values actions (random_value_count);

  test_and_compare_parallel (global_error, TEST_RANDOM_PERF_STEP_NAMES,
                             FUNC_ALLOCS_AS_ARGS (test_performance_random, char), ptr_pool_size, alloc_count,
                             std::cref (actions));
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

  /* basic performance tests */
  test_and_compare_single_basic<char> (global_err, HUNDRED_K);
  test_and_compare_single_basic<size_t> (global_err, HUNDRED_K);
  test_and_compare_single_basic<dummy_size_512> (global_err, HUNDRED_K);
  test_and_compare_single_basic<dummy_size_8k> (global_err, TEN_K);
  test_and_compare_parallel_basic<dummy_size_512> (global_err, HUNDRED_K);
  test_and_compare_parallel_basic<dummy_size_8k> (global_err, TEN_K);

  /* random performance tests */
  size_t small_pool_size = 128;
  size_t medium_pool_size = 1024;
  size_t large_pool_size = TEN_K;
  test_and_compare_single_random (global_err, small_pool_size, HUNDRED_K);
  test_and_compare_single_random (global_err, medium_pool_size, HUNDRED_K);
  test_and_compare_single_random (global_err, large_pool_size, HUNDRED_K);
  test_and_compare_parallel_random (global_err, small_pool_size, HUNDRED_K);
  test_and_compare_parallel_random (global_err, medium_pool_size, HUNDRED_K);
  test_and_compare_parallel_random (global_err, large_pool_size, HUNDRED_K);

  std::cout << std::endl;

  return global_err;
}

}  // namespace test_memalloc
