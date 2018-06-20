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

/* own header */
#include "test_private_unique_ptr.hpp"

#include "test_memory_alloc_helper.hpp"

/* headers from cubrid */
#include "memory_alloc.h"

/* system headers */
#include <iostream>
#include <memory>

namespace test_memalloc
{
  void
  test_private_unique_ptr_swap()
  {
    custom_thread_entry cte;
    db_private_allocator<char> private_alloc (cte.get_thread_entry());

    char *a = private_alloc.allocate (SIZE_64);
    char *b = private_alloc.allocate (SIZE_64);

    // copy something different in each buffer
    strcpy (a, "1");
    PRIVATE_UNIQUE_PTR<char> pa (a, cte.get_thread_entry());

    strcpy (b, "2");
    PRIVATE_UNIQUE_PTR<char> pb (b, cte.get_thread_entry());

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
    std::cout << "start testing pointer is freed when the desctructor of PRIVATE_UNIQUE_PTR is called" << std::endl;
    custom_thread_entry cte;
    T *ptr = nullptr;
    db_private_allocator<T> private_alloc (cte.get_thread_entry());

    ptr = private_alloc.allocate (SIZE_64);
    *ptr = T();
    * (ptr + SIZE_64 - 1) = T();

    PRIVATE_UNIQUE_PTR<char> priv_uniq_ptr (ptr, cte.get_thread_entry());
  }

  template<typename T>
  void
  test_private_unique_ptr_release()
  {
    std::cout << "start testing release function of PRIVATE_UNIQUE_PTR" << std::endl;
    custom_thread_entry cte;
    T *ptr = nullptr;
    db_private_allocator<T> private_alloc (cte.get_thread_entry());

    ptr = private_alloc.allocate (SIZE_64);
    *ptr = T();
    * (ptr + SIZE_64 - 1) = T();

    PRIVATE_UNIQUE_PTR<T> priv_uniq_ptr (ptr, cte.get_thread_entry());

    T *ptr_release = priv_uniq_ptr.release();

    // these 2 pointers should be the same
    assert (ptr == ptr_release);

    // after the release, the priv_uniq_ptr should not hold any reference
    assert (priv_uniq_ptr.get() == nullptr);

    // priv_uniq_ptr doesn't do the deallocation any more
    // so we need to call it explicitly
    private_alloc.deallocate (ptr_release);
  }

  int
  test_private_unique_ptr (void)
  {
    int global_error = 0;
    std::cout << std::endl;

    test_private_unique_ptr_free<char>();

    test_private_unique_ptr_release<char>();

    test_private_unique_ptr_swap();

    return global_error;
  }

}  // namespace test_memalloc
