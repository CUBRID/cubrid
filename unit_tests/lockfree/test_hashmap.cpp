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

#include "test_hashmap.hpp"

#include "lock_free.h"
#include "lockfree_hashmap.hpp"
#include "lockfree_transaction_system.hpp"

#include <cstddef>

using namespace lockfree;

namespace test_lockfree
{
  using int_hashmap = hashmap<int, int>;

  struct my_key
  {
    int m_1;
    int m_2;
  };

  struct my_entry
  {
    my_key m_key;
    my_entry *m_next;
    my_entry *m_rstack;
    pthread_mutex_t m_mutex;
    UINT64 m_delid;

    bool m_init;

    my_entry () = default;
    ~my_entry () = default;
  };
  static void *alloc_my_entry ();
  static int free_my_entry (void *p);
  static int init_my_entry (void *p);
  static int uninit_my_entry (void *p);

  static lf_entry_descriptor g_edesc =
  {
    offsetof (my_entry, m_rstack),
    offsetof (my_entry, m_next),
    offsetof (my_entry, m_delid),
    offsetof (my_entry, m_key),
    offsetof (my_entry, m_mutex),

    0, // is subject to change
  };

  using my_hashmap = hashmap<my_key, my_entry>;

  int
  test_hashmap_functional ()
  {
    tran::system sys (10);
    int_hashmap hash;
    hash.init (sys, 10, 10, 10, g_edesc);
    return 0;
  }

  int
  test_hashmap_performance ()
  {
    tran::system sys (10);
    int_hashmap hash;
    hash.init (sys, 10, 10, 10, g_edesc);
    return 0;
  }

  static void *
  alloc_my_entry ()
  {
    return (void *) new my_entry ();
  }
} // namespace test_lockfree
