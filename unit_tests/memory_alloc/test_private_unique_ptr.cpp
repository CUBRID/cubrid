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

/* own header */
#include "test_private_unique_ptr.hpp"

#include "test_memory_alloc_helper.hpp"

/* headers from cubrid */
#include "memory_alloc.h"
#include "memory_private_allocator.hpp"

/* system headers */
#include <iostream>
#include <memory>

using namespace cubmem;

namespace test_memalloc
{
  const std::string TAB = std::string (4, ' ');

  void
  test_private_unique_ptr_swap()
  {
    custom_thread_entry cte;
    cubmem::private_allocator<char> private_alloc (cte.get_thread_entry());

    char *a = private_alloc.allocate (SIZE_64);
    char *b = private_alloc.allocate (SIZE_64);

    // copy something different in each buffer
    strcpy (a, "1");
    private_unique_ptr<char> pa (a, cte.get_thread_entry());

    strcpy (b, "2");
    private_unique_ptr<char> pb (b, cte.get_thread_entry());

    pa.swap (pb);

    // after the swap, pa will hold the reference to b
    char *a_after_swap = pa.get();
    char *b_after_swap = pb.get();

    assert (strcmp (a_after_swap, "2") == 0);
    assert (strcmp (b_after_swap, "1") == 0);
  }

  template<typename T>
  void
  test_private_unique_ptr_free()
  {
    std::cout << TAB << "testing pointer is freed when the desctructor of PRIVATE_UNIQUE_PTR is called" << std::endl;
    custom_thread_entry cte;
    T *ptr = nullptr;
    cubmem::private_allocator<T> private_alloc (cte.get_thread_entry());

    ptr = private_alloc.allocate (SIZE_64);
    *ptr = T();
    * (ptr + SIZE_64 - 1) = T();

    private_unique_ptr<char> priv_uniq_ptr (ptr, cte.get_thread_entry());
  }

  template<typename T>
  void
  test_private_unique_ptr_release()
  {
    std::cout << TAB << "testing release function of PRIVATE_UNIQUE_PTR" << std::endl;
    custom_thread_entry cte;
    T *ptr = nullptr;
    cubmem::private_allocator<T> private_alloc (cte.get_thread_entry());

    ptr = private_alloc.allocate (SIZE_64);
    *ptr = T();
    * (ptr + SIZE_64 - 1) = T();

    private_unique_ptr<T> priv_uniq_ptr (ptr, cte.get_thread_entry());

    T *ptr_release = priv_uniq_ptr.release();

    // these 2 pointers should be the same
    assert (ptr == ptr_release);

    // after the release, the priv_uniq_ptr should not hold any reference
    assert (priv_uniq_ptr.get() == nullptr);

    // priv_uniq_ptr doesn't do the deallocation any more
    // so we need to call it explicitly
    private_alloc.deallocate (ptr_release);
  }

  struct Foo
  {
    void test_arrow_operator()
    {
      std::cout << TAB << "testing arrow operator" << std::endl;
    }
  };

  void
  test_dereference_operator (const Foo &ptr)
  {
    std::cout << TAB << "testing dereference operator" << std::endl;
  }

  void
  test_operators()
  {
    std::cout << TAB << "testing operators overload of PRIVATE_UNIQUE_PTR" << std::endl;

    custom_thread_entry cte;
    Foo *ptr = (Foo *) db_private_alloc (cte.get_thread_entry(), sizeof (Foo));
    private_unique_ptr<Foo> priv_uniq_ptr (ptr, cte.get_thread_entry());

    priv_uniq_ptr->test_arrow_operator();
    test_dereference_operator (*priv_uniq_ptr);
  }

  int
  test_private_unique_ptr (void)
  {
    int global_error = 0;
    std::cout << std::endl;

    test_private_unique_ptr_free<char>();

    test_private_unique_ptr_release<char>();

    test_private_unique_ptr_swap();

    test_operators();

    return global_error;
  }

}  // namespace test_memalloc
