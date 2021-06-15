/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "test_db_private_alloc.hpp"

#include "test_memory_alloc_helper.hpp"
#include "test_perf_compare.hpp"
#include "test_debug.hpp"
#include "test_output.hpp"

#include "thread_manager.hpp"

#include <iostream>
#include <thread>
#include <sstream>
#include <typeinfo>
#include <ctime>
#include <array>
#include <utility>

namespace test_memalloc
{

  /************************************************************************/
  /* Unit helpers                                                         */
  /************************************************************************/

  /* Expand fn_arg<Alloc<T> > with the three possible allocator */
#define FUNC_ALLOCS_AS_ARGS(fn_arg, type_arg) \
  fn_arg<cubmem::private_allocator<type_arg> >, \
  fn_arg<std::allocator<type_arg> >, \
  fn_arg<mallocator<type_arg> >

  /* Expand fn_arg<T, Alloc<T> > with the three possible allocator */
#define FUNC_TYPE_AND_ALLOCS_AS_ARGS(fn_arg, type_arg) \
  fn_arg<type_arg, cubmem::private_allocator<type_arg> >, \
  fn_arg<type_arg, std::allocator<type_arg> >, \
  fn_arg<type_arg, mallocator<type_arg> >

  /*  run single-thread tests with private, standard and malloc allocators and wrap with text
   *
   *
   *  How it works:
   *
   *      Get step names and specialized functions for each tested allocator along with their variadic arguments.
   *
   *  How to call:
   *
   *      test_and_compare_single (err, step_names, fn_specialized_for_private, fn_specialized_for_standard,
   *                               fn_specialized_for_malloc, same_args...)
   *      fn should be a template function with first argument perf_compare & and then same_args...
   *      for fn arguments you may use FUNC_ALLOCS_AS_ARGS or FUNC_TYPE_AND_ALLOCS_AS_ARGS.
   */
  template <typename FuncPrv, typename FuncStd, typename FuncMlc, typename ... Args>
  static void
  test_and_compare_single (int &global_error, const test_common::string_collection &step_names, FuncPrv &&fn_private,
			   FuncStd &&fn_std, FuncMlc &&fn_malloc, Args &&... args)
  {
    test_common::perf_compare compare_performance (get_allocator_names (), step_names);

    run_test (global_error, fn_private, std::ref (compare_performance), std::forward<Args> (args)...);
    run_test (global_error, fn_std, std::ref (compare_performance), std::forward<Args> (args)...);
    run_test (global_error, fn_malloc, std::ref (compare_performance), std::forward<Args> (args)...);
    std::cout << std::endl;

    compare_performance.print_results_and_warnings (std::cout);
  }

  /* run multi-thread tests with private, standard and malloc allocators and wrap with text.
   *
   * Same as test_and_compare_single, mostly.
   *
   * One mention is that if you wanna pass a reference to be used by all threads, you must pass it with std::ref.
   */
  template <typename FuncPrv, typename FuncStd, typename FuncMlc, typename ... Args>
  static void
  test_and_compare_parallel (int &global_error, const test_common::string_collection &step_names,
			     FuncPrv &&fn_private, FuncStd &&fn_std, FuncMlc &&fn_malloc, Args &&... args)
  {
    test_common::perf_compare compare_performance (get_allocator_names (), step_names);

    run_parallel (fn_private, std::ref (compare_performance), std::forward<Args> (args)...);
    run_parallel (fn_std, std::ref (compare_performance), std::forward<Args> (args)...);
    run_parallel (fn_malloc, std::ref (compare_performance), std::forward<Args> (args)...);
    std::cout << std::endl;

    compare_performance.print_results_and_warnings (std::cout);
  }

  cubthread::manager *cub_th_m;

  int init_common_cubrid_modules (void)
  {
    static bool initialized = false;
    THREAD_ENTRY *thread_p = NULL;

    if (initialized)
      {
	return 0;
      }

    //cub_th_m.set_max_thread_count (100);

    //cubthread::set_manager (&cub_th_m);
    cubthread::initialize (thread_p);
    cub_th_m = cubthread::get_manager();
    cub_th_m->set_max_thread_count (100);

    cub_th_m->alloc_entries();
    cub_th_m->init_entries (false);

    initialized = true;


    return NO_ERROR;
  }

  /* Base function to test private allocator functionality */
  template <typename T>
  static int
  test_private_allocator ()
  {
    static std::string prefix = std::string (4, ' ') + PORTABLE_FUNC_NAME + "<" + typeid (T).name() + ">: ";
    T *ptr = nullptr;

    {
      custom_thread_entry cte;
      cubmem::private_allocator<T> private_alloc (cte.get_thread_entry());

      std::cout << prefix << "alloc 64" << std::endl;
      ptr = private_alloc.allocate (SIZE_64);
      *ptr = T();
      * (ptr + SIZE_64 - 1) = T();
      private_alloc.deallocate (ptr);
    }

    {
      custom_thread_entry cte;
      cubmem::private_allocator<T> private_alloc (cte.get_thread_entry());
      std::cout << prefix << "alloc 1M" << std::endl;
      ptr = private_alloc.allocate (SIZE_1_M);
      *ptr = T();
      * (ptr + SIZE_1_M - 1) = T();
      private_alloc.deallocate (ptr);
    }

    {
      custom_thread_entry cte;
      cubmem::private_allocator<T> private_alloc (cte.get_thread_entry());

      std::cout << prefix << "alloc 64x64" << std::endl;
      std::array<T *, SIZE_64> ptr_array;
      for (size_t i = 0; i < ptr_array.size(); i++)
	{
	  ptr_array[i] = private_alloc.allocate (SIZE_64);
	  *ptr_array[i] = T();
	  * (ptr_array[i] + SIZE_64 - 1) = T();
	}
      for (size_t i = 0; i < ptr_array.size(); i++)
	{
	  private_alloc.deallocate (ptr_array[i]);
	}
    }

    {
      custom_thread_entry cte;
      cubmem::private_allocator<T> private_alloc (cte.get_thread_entry());

      /* test containers */
      std::vector<T, cubmem::private_allocator<T>> vec (private_alloc);
      vec.resize (SIZE_64);
      vec.resize (SIZE_ONE_K);
      vec.resize (SIZE_16_K);
      vec.clear();
    }

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
   *      alloc_count - test alloc_count; translates into the number of pointers allocated for each sub-test
   *      results - test result collector
   */
  template <typename T, typename Alloc >
  static int
  test_basic_performance (test_common::perf_compare &results, size_t alloc_count)
  {
    custom_thread_entry cte;    /* thread entry wrapper */

    /* function header */
    static std::string function_header =
	    std::string ("    basic_perf") + typeid (T).name() + "," + typeid (Alloc).name() + ">\n";

    test_common::sync_cout (function_header);

    /* instantiate allocator */
    Alloc *alloc = NULL;
    test_allocator_type alloc_type;
    init_allocator<T> (cte, alloc, alloc_type);
    size_t alloc_index = static_cast <size_t> (alloc_type);

    /* init step and timer */
    unsigned step = 0;
    test_common::us_timer timer;

    /* step 0: test allocate / deallocate / allocate / deallocate and so on */
    T *ptr = NULL;
    for (size_t i = 0; i < alloc_count; i++)
      {
	ptr = alloc->allocate (1);
	alloc->deallocate (ptr, 1);
      }
    results.register_time (timer, alloc_index, step++);

    /* step 1: test allocate / allocate and so on*/
    T **pointers = new T * [alloc_count];
    for (size_t i = 0; i < alloc_count; i++)
      {
	pointers[i] = alloc->allocate (1);
      }
    results.register_time (timer, alloc_index, step++);

    /* step 2: test deallocate / deallocate and so on*/
    for (size_t i = 0; i < alloc_count; i++)
      {
	alloc->deallocate (pointers[i], 1);
      }
    results.register_time (timer, alloc_index, step++);

    test_common::custom_assert (step == results.get_step_count ());

    delete [] pointers;
    delete alloc;

    return 0;
  }

  test_common::string_collection basic_step_names ("Alternate Alloc/Dealloc", "Successive Allocs",
      "Successive Deallocs");

  /* test_and_compare implementation for test basic
   *
   *  Runs basic tests on all allocators using the given type. See more details in test_and_compare_single and
   *  test_basic_performance.
   */
  template <typename T>
  static void
  test_and_compare_single_basic (int &global_error, size_t alloc_count)
  {
    test_and_compare_single (global_error, basic_step_names, FUNC_TYPE_AND_ALLOCS_AS_ARGS (test_basic_performance, T),
			     alloc_count);
  }

  /* test_and_compare_parallel implementation for test basic
   *
   *  Runs concurrent basic tests on all allocators using the given type. See more details in test_and_compare_parallel
   *  and test_basic_performance.
   */
  template <typename T>
  static void
  test_and_compare_parallel_basic (int &global_error, size_t alloc_count)
  {
    test_and_compare_parallel (global_error, basic_step_names, FUNC_TYPE_AND_ALLOCS_AS_ARGS (test_basic_performance, T),
			       alloc_count);
  }

  /* generate a number of random values */
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

  /* function tests performance with allocating a series of random-sized pointers.
   *
   *  First a pool of random-sized pointers is created. Then a loop that deallocates one random pointer and replaces it
   *  with another random-sized pointer. At the end deallocates everything.
   *
   *  Template:
   *
   *      Alloc - tested allocator. its specialization should be char.
   */
  template <typename Alloc>
  static int
  test_performance_random (test_common::perf_compare &result, size_t ptr_pool_size, unsigned alloc_count,
			   const random_values &actions)
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
    static std::string function_header = std::string ("    random_perf") + "<" + typeid (Alloc).name () + ">\n";
    test_common::sync_cout (function_header);

    /* instantiate allocator */
    Alloc *alloc = NULL;
    test_allocator_type alloc_type;
    init_allocator<char> (cte, alloc, alloc_type);
    size_t alloc_index = static_cast <size_t> (alloc_type);

    /* */
    ptr_with_size *pointers_pool = new ptr_with_size [ptr_pool_size];
    size_t ptr_index;

    /* init step and timer */
    unsigned step = 0;
    unsigned random_value_cursor = 0;
    test_common::us_timer timer;

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

    result.register_time (timer, alloc_index, step++);

    test_common::custom_assert (random_value_cursor == actions.get_size ());
    test_common::custom_assert (step == result.get_step_count ());

    delete [] pointers_pool;
    delete alloc;

    return 0;

#undef PTR_ALLOC
  }

  test_common::string_collection random_step_names ("Random Alloc/Dealloc");

  /* test_and_compare implementation for test random
   *
   *  Run single-threaded random allocation tests. See test_and_compare_single and test_performance_random.
   */
  static void
  test_and_compare_single_random (int &global_error, size_t ptr_pool_size, unsigned alloc_count)
  {
    /* create random values */
    size_t random_value_count = ptr_pool_size + 2 * alloc_count;
    random_values actions (random_value_count);

    test_and_compare_single (global_error, random_step_names, FUNC_ALLOCS_AS_ARGS (test_performance_random, char),
			     ptr_pool_size, alloc_count, std::cref (actions));
  }

  /* test_and_compare implementation for test random
   *
   *  Run multi-threaded random allocation tests. See test_and_compare_single and test_performance_random.
   */
  static void
  test_and_compare_parallel_random (int &global_error, size_t ptr_pool_size, unsigned alloc_count)
  {
    /* create random values */
    size_t random_value_count = ptr_pool_size + 2 * alloc_count;
    random_values actions (random_value_count);

    test_and_compare_parallel (global_error, random_step_names, FUNC_ALLOCS_AS_ARGS (test_performance_random, char),
			       ptr_pool_size, alloc_count, std::cref (actions));
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
