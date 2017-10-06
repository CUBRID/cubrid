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

#include "db_private_allocator.hpp"
#include "test_memory_alloc_helper.hpp"

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
void
init_allocator (custom_thread_entry & cte, std::allocator<T> *& alloc)
{
  alloc = new std::allocator<T> ();
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

  std::cout << PORTABLE_FUNC_NAME << std::endl;
  std::string prefix(4,' ');
  prefix += PORTABLE_FUNC_NAME;
  prefix += ": ";

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
test_performance_alloc ()
{
  custom_thread_entry cte;
  std::cout << PORTABLE_FUNC_NAME << std::endl;
  std::string prefix (4, ' ');
  prefix += PORTABLE_FUNC_NAME;
  prefix += ": ";

  Alloc *alloc = NULL;
  init_allocator<T> (cte, alloc);

  millitimer timer;
  T *ptr = NULL;
  std::cout << prefix << "alloc + dealloc + alloc + dealloc ... = ";
  for (int i = 0; i < SIZE_1_M; i++)
    {
      ptr = alloc->allocate (1);
      ptr = T();
      alloc->deallocate (ptr, 1);
    }
  std::cout << timer.time_and_reset ().count () << std::endl;

  std::cout << prefix << "alloc + alloc + ... + dealloc + dealloc ... = ";
  T** pointers = new T * [SIZE_1_M];
  for (size_t i = 0; i < SIZE_1_M; i++)
    {
      pointers[i] = alloc->allocate (1);
    }
  for (size_t i = 0; i < SIZE_1_M; i++)
    {
      alloc->deallocate (pointers[i], 1);
    }
  std::cout << timer.time_and_reset ().count () << std::endl;

  delete pointers;
  delete alloc;

  return 0;
}

template <typename ... Args>
void
run_test (int & global_err, int (*f) (Args...), Args &... args)
{
  std::cout << std::endl;
  std::cout << "    starting test - ";
  
  int err = f (args...);
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

int
test_db_private_alloc ()
{
  std::cout << PORTABLE_FUNC_NAME << std::endl;

  int global_err = 0;

  run_test (global_err, test_allocator<char>);
  run_test (global_err, test_allocator<int>);
  run_test (global_err, test_allocator<dummy>);

  run_test (global_err, test_performance_alloc<char, db_private_allocator<char> >);
  run_test (global_err, test_performance_alloc<char, std::allocator<char> >);

  std::cout << std::endl;

  return global_err;
}

