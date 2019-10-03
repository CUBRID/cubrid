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

#include <array>
#include <cstddef>
#include <cstdlib>
#include <thread>

using namespace lockfree;

namespace test_lockfree
{
  using dummy_int_hashmap = hashmap<int, int>;    // for compile

  struct my_key
  {
    unsigned int m_1;
    unsigned int m_2;
  };
  static int copy_my_key (void *src, void *dest);
  static int compare_my_key (void *key1, void *key2);
  static unsigned int hash_my_key (void *key, int hash_size);

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

    alloc_my_entry,
    free_my_entry,
    init_my_entry,
    uninit_my_entry,
    copy_my_key,
    compare_my_key,
    hash_my_key
  };

  using my_hashmap = hashmap<my_key, my_entry>;
  using my_hash_table = lf_hash_table_cpp<my_key, my_entry>;

  static void init_hash_table (lf_tran_system &transys, int hash_size, my_hash_table &hash);
  static void init_hashmap (tran::system &transys, size_t hash_size, my_hashmap &hash);

  template <class H>
  testcase_inserts_only (H &hash, tran::index tran_index, size_t insert_count)
  {
    my_key k;
    size_t inserted;
    size_t rejected;
    my_entry *ent;

    for (size_t i = 0; i < insert_count; ++i)
      {
	k.m_1 = (unsigned int) std::rand ();
	k.m_2 = (unsigned int) std::rand ();

	if (hash.insert (tran_index, k, ent))
	  {
	    ++inserted;
	  }
	else
	  {
	    ++rejected;
	  }
      }
  }

  template <class H, class Tran, size_t ThCnt, typename F, typename ... Args>
  start_threads (H &hash, std::array<Tran, ThCnt> &tran_array, F &&f, Args &&... args)
  {
    std::thread all_threads[ThCnt];

    for (size_t i = 0; i < ThCnt; i++)
      {
	all_threads[i] = std::thread (f, std::ref (hash), std::ref (tran_array[i]), std::forward (args)...);
      }
    for (size_t i = 0; i < ThCnt; i++)
      {
	all_threads[i].join ();
      }
  }

  template <size_t ThCnt, typename F, typename ... Args>
  void
  test_hashmap_case ()
  {
    // todo...
  }

  int
  test_hashmap_functional ()
  {
    tran::system sys (10);
    dummy_int_hashmap hash;
    hash.init (sys, 10, 10, 10, g_edesc);
    return 0;
  }

  int
  test_hashmap_performance ()
  {
    tran::system sys (10);
    dummy_int_hashmap hash;
    hash.init (sys, 10, 10, 10, g_edesc);
    return 0;
  }

  static int
  copy_my_key (void *src, void *dest)
  {
    * (my_key *) src = * (my_key *) dest;
  }

  static int
  compare_my_key (void *key1, void *key2)
  {
    my_key *a = (my_key *) key1;
    my_key *b = (my_key *) key2;

    return (a->m_1 != b->m_1) || (a->m_2 != b->m_2) ? 1 : 0;
  }

  static unsigned int
  hash_my_key (void *key, int hash_size)
  {
    unsigned int size = (unsigned int) hash_size;
    return ((my_key *) key)->m_1 % hash_size;
  }

  static void *
  alloc_my_entry ()
  {
    return (void *) new my_entry ();
  }

  static int
  free_my_entry (void *p)
  {
    my_entry *e = (my_entry *) p;
    delete e;
  }

  static int
  init_my_entry (void *p)
  {
    my_entry *e = (my_entry *) p;
    assert (!e->m_init);
    e->m_init = true;
  }

  static int
  uninit_my_entry (void *p)
  {
    my_entry *e = (my_entry *) p;
    assert (e->m_init);
    e->m_init = false;
  }

  static void
  init_hash_table (lf_tran_system &transys, int hash_size, my_hash_table &hash)
  {
    hash.init (transys, hash_size, 100, 100, g_edesc);
  }

  static void
  init_hashmap (tran::system &transys, size_t hash_size, my_hashmap &hash)
  {
    hash.init (transys, hash_size, 100, 100, g_edesc);
  }
} // namespace test_lockfree
