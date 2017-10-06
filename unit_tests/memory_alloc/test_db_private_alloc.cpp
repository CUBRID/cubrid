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

#include "db_private_allocator.hpp"
#include "test_memory_alloc_helper.hpp"

std::mutex output_mutex;

void
sync_output (const std::string & str)
{
  std::lock_guard<std::mutex> lock(output_mutex);
  std::cout << str;
}

class dummy
{
public:
  dummy ()
  {
  }

  dummy (const dummy & other)
  {
    *this = other;
  }

  dummy& operator= (const dummy& other)
  {
    this->c = other.c;
    return *this;
  }

private:
  char c;
  short s;
  int i;
  double d;
};

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

template <typename T>
static int
test_allocator ()
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
  std::vector<T, db_private_allocator<T>> vec (SIZE_64, private_alloc);
  vec.resize (SIZE_ONE_K);
  vec.resize (SIZE_16_K);
  vec.clear ();
  cte.check_resource_leaks ();

  return 0;
}

template <typename T, typename Alloc >
int
test_performance_alloc (size_t size)
{
  custom_thread_entry cte;

  static const char * funcname = PORTABLE_FUNC_NAME;
  static std::stringstream prefix (std::string ("    ") + funcname + "<" + typeid(T).name() + ","
                                   + typeid(Alloc).name() + ">: ");
  std::stringstream log (std::string (prefix.str()));

  Alloc *alloc = NULL;
  init_allocator<T> (cte, alloc);

  millitimer timer;
  T *ptr = NULL;
  log << prefix.str () << "alloc + dealloc + alloc + dealloc ... = ";
  for (int i = 0; i < size; i++)
    {
      ptr = alloc->allocate (1);
      alloc->deallocate (ptr, 1);
    }
  log << timer.time_and_reset ().count () << std::endl;

  log << prefix.str () << "alloc + alloc + ... + dealloc + dealloc ... = ";
  T** pointers = new T * [size];
  for (size_t i = 0; i < size; i++)
    {
      pointers[i] = alloc->allocate (1);
    }
  for (size_t i = 0; i < size; i++)
    {
      alloc->deallocate (pointers[i], 1);
    }
  log << timer.time_and_reset ().count () << std::endl;

  delete pointers;
  delete alloc;

  sync_output (log.str ());

  return 0;
}

template <typename ... Args>
void
run_test (int & global_err, int (*f) (Args...), Args &... args)
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

template <typename ... Args>
void
run_parallel (int (*f) (Args...), Args &... args)
{
  unsigned int worker_count = std::thread::hardware_concurrency ();
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

int
test_db_private_alloc ()
{
  std::cout << PORTABLE_FUNC_NAME << std::endl;

  int global_err = 0;

  run_test (global_err, test_allocator<char>);
  run_test (global_err, test_allocator<int>);
  run_test (global_err, test_allocator<dummy>);

  size_t one_mil = SIZE_1_M;
  run_test<size_t> (global_err, test_performance_alloc<char, db_private_allocator<char> >, one_mil);
  run_test<size_t> (global_err, test_performance_alloc<char, std::allocator<char> >, one_mil);
  run_test<size_t> (global_err, test_performance_alloc<char, mallocator<char> >, one_mil);

  run_test<size_t> (global_err, test_performance_alloc<size_t, db_private_allocator<size_t> >, one_mil);
  run_test<size_t> (global_err, test_performance_alloc<size_t, std::allocator<size_t> >, one_mil);
  run_test<size_t> (global_err, test_performance_alloc<size_t, mallocator<size_t> >, one_mil);

  typedef std::array<size_t, SIZE_64> size_512;
  run_test<size_t> (global_err, test_performance_alloc<size_512, db_private_allocator<size_512> >, one_mil);
  run_test<size_t> (global_err, test_performance_alloc<size_512, std::allocator<size_512> >, one_mil);
  run_test<size_t> (global_err, test_performance_alloc<size_512, mallocator<size_512> >, one_mil);

  size_t hundred_k = SIZE_ONE_K * 100;
  typedef std::array<size_t, SIZE_ONE_K> size_8k;
  run_test<size_t> (global_err, test_performance_alloc<size_8k, db_private_allocator<size_8k> >, hundred_k);
  run_test<size_t> (global_err, test_performance_alloc<size_8k, std::allocator<size_8k> >, hundred_k);
  run_test<size_t> (global_err, test_performance_alloc<size_8k, mallocator<size_8k> >, hundred_k);

  run_parallel<size_t> (test_performance_alloc<size_512, db_private_allocator<size_512> >, one_mil);
  run_parallel<size_t> (test_performance_alloc<size_512, std::allocator<size_512> >, one_mil);
  run_parallel<size_t> (test_performance_alloc<size_512, mallocator<size_512> >, one_mil);

  run_parallel<size_t> (test_performance_alloc<size_8k, db_private_allocator<size_8k> >, hundred_k);
  run_parallel<size_t> (test_performance_alloc<size_8k, std::allocator<size_8k> >, hundred_k);
  run_parallel<size_t> (test_performance_alloc<size_8k, mallocator<size_8k> >, hundred_k);

  std::cout << std::endl;

  return global_err;
}

