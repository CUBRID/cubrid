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

/*
 * test_memory_alloc_helper.hpp - helper classes and function for test memory alloc
 */

#ifndef _TEST_MEMORY_ALLOC_HELPER_HPP_
#define _TEST_MEMORY_ALLOC_HELPER_HPP_

#include "test_string_collection.hpp"

#include "memory_private_allocator.hpp"
#include "thread_entry.hpp"

/* this hack */
#ifdef strlen
#undef strlen
#endif /* strlen */

#include <iostream>
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

  /* thread entry wrapper */
  class custom_thread_entry
  {
    public:
      custom_thread_entry ();
      ~custom_thread_entry ();
      THREAD_ENTRY *get_thread_entry ();
      void check_resource_leaks (void);

    private:
      void start_resource_tracking (void);

      THREAD_ENTRY m_thread_entry;
      int m_rc_track_id;
  };

  /* Mallocator - allocator concept to be used with malloc/free
   */
  template <typename T>
  class mallocator
  {
    public:
      T *allocate (size_t size)
      {
	return (T *) malloc (size * sizeof (T));
      }
      void deallocate (T *pointer, size_t UNUSED (size))
      {
	// cppcheck-suppress autovarInvalidDeallocation
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
  operator== (const mallocator<T> &, const mallocator<U> &)
  {
    return true;
  }
  template <class T, class U>
  bool
  operator!= (const mallocator<T> &, const mallocator<U> &)
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
  const test_common::string_collection &get_allocator_names (void);

  /* Generic functions to allocate an allocator with thread entry argument. */
  template <typename T>
  void
  init_allocator (custom_thread_entry &cte, std::allocator<T> *&alloc, test_allocator_type &type_out)
  {
    alloc = new std::allocator<T> ();
    type_out = test_allocator_type::STANDARD;
  }
  template <typename T>
  void
  init_allocator (custom_thread_entry &cte, mallocator<T> *&alloc, test_allocator_type &type_out)
  {
    alloc = new mallocator<T> ();
    type_out = test_allocator_type::MALLOC;
  }
  template <typename T>
  void
  init_allocator (custom_thread_entry &cte, cubmem::private_allocator<T> *&alloc, test_allocator_type &type_out)
  {
    alloc = new cubmem::private_allocator<T> (cte.get_thread_entry ());
    type_out = test_allocator_type::PRIVATE;
  }

  /* run test and wrap with formatted text */
  template <typename Func, typename ... Args>
  void
  run_test (int &global_err, Func &&f, Args &&... args)
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
  run_parallel (Func &&f, Args &&... args)
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
