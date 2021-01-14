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

#include "test_hashmap.hpp"

#include "lock_free.h"
#include "lockfree_hashmap.hpp"
#include "lockfree_transaction_system.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

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

  static void keygen_no_conflict (my_key &k, size_t hash_size, size_t nops, std::random_device &rd);
  static void keygen_avg_conflict (my_key &k, size_t hash_size, size_t nops, std::random_device &rd);
  static void keygen_high_conflict (my_key &k, size_t hash_size, size_t nops, std::random_device &rd);

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
    hash_my_key,
    NULL
  };

  static const bool MUTEX_ON = true;
  static const bool MUTEX_OFF = false;

  static void
  set_entry_mutex_mode (bool on_off)
  {
    g_edesc.using_mutex = on_off ? LF_EM_USING_MUTEX : LF_EM_NOT_USING_MUTEX;
  }

  using my_hashmap = hashmap<my_key, my_entry>;
  using my_lf_hash_table = lf_hash_table_cpp<my_key, my_entry>;

  static void init_lf_hash_table (lf_tran_system &transys, int hash_size, my_lf_hash_table &hash);
  static void init_hashmap (tran::system &transys, size_t hash_size, my_hashmap &hash);

  std::string g_tabs = "";

  static void
  increment_tab_indent ()
  {
    g_tabs += '\t';
  }

  static void
  decrement_tab_indent ()
  {
    g_tabs.pop_back ();
  }

  static void
  cout_new_line ()
  {
    std::cout << std::endl << g_tabs;
  }

  template <typename D>
  void
  cout_msec_count (const D &d)
  {
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds> (d).count () << " msec";
  }

  struct test_result
  {
    // op stats
    std::atomic<std::uint64_t> m_find_ops;
    std::atomic<std::uint64_t> m_find_or_insert_ops;
    std::atomic<std::uint64_t> m_insert_ops;
    std::atomic<std::uint64_t> m_insert_given_ops;
    std::atomic<std::uint64_t> m_erase_ops;
    std::atomic<std::uint64_t> m_erase_locked_ops;
    std::atomic<std::uint64_t> m_iterate_ops;
    std::atomic<std::uint64_t> m_clear_ops;
    std::atomic<std::uint64_t> m_claim_ops;
    std::atomic<std::uint64_t> m_retire_ops;

    // result stats
    std::atomic<std::uint64_t> m_successful_inserts;
    std::atomic<std::uint64_t> m_rejected_inserts;
    std::atomic<std::uint64_t> m_found_on_inserts;
    std::atomic<std::uint64_t> m_found_on_finds;
    std::atomic<std::uint64_t> m_not_found_on_finds;
    std::atomic<std::uint64_t> m_found_on_erase_ops;
    std::atomic<std::uint64_t> m_not_found_on_erase_ops;
    std::atomic<std::uint64_t> m_iterate_increments;

    cubmonitor::timer_stat m_timer;

    test_result ()
    {
      // op stats
      m_find_ops = 0;
      m_find_or_insert_ops = 0;
      m_insert_ops = 0;
      m_insert_given_ops = 0;
      m_erase_ops = 0;
      m_erase_locked_ops = 0;
      m_iterate_ops = 0;
      m_clear_ops = 0;
      m_claim_ops = 0;
      m_retire_ops = 0;

      // result stats
      m_successful_inserts = 0;
      m_rejected_inserts = 0;
      m_found_on_inserts = 0;
      m_found_on_finds = 0;
      m_not_found_on_finds = 0;
      m_found_on_erase_ops = 0;
      m_not_found_on_erase_ops = 0;
      m_iterate_increments = 0;
    }

    void dump_stats ()
    {
      cout_new_line ();
      std::cout << "OPS: ";
      dump_not_zero ("find", m_find_ops);
      dump_not_zero ("find_or_ins", m_find_or_insert_ops);
      dump_not_zero ("ins", m_insert_ops);
      dump_not_zero ("ins_given", m_insert_given_ops);
      dump_not_zero ("erase", m_erase_ops);
      dump_not_zero ("erase_lck", m_erase_locked_ops);
      dump_not_zero ("iter", m_iterate_ops);
      dump_not_zero ("clr", m_clear_ops);
      dump_not_zero ("claim", m_claim_ops);
      dump_not_zero ("retire", m_retire_ops);

      cout_new_line ();
      std::cout << "REZ: ";
      dump_not_zero ("ins_succ", m_successful_inserts);
      dump_not_zero ("ins_fail", m_rejected_inserts);
      dump_not_zero ("ins_find", m_found_on_inserts);
      dump_not_zero ("fnd_succ", m_found_on_finds);
      dump_not_zero ("fnd_fail", m_not_found_on_finds);
      dump_not_zero ("ers_succ", m_found_on_erase_ops);
      dump_not_zero ("ers_fail", m_not_found_on_erase_ops);
      dump_not_zero ("iter_incr", m_iterate_increments);

      cout_new_line ();
      std::cout << "TIME: ";
      cout_msec_count (m_timer.get_time ());
    }

    void dump_not_zero (const char *name, std::atomic<std::uint64_t> &val)
    {
      if (val != 0)
	{
	  std::cout << name << " = " << val.load () << " ";
	}
    }
  };

  enum test_type
  {
    TEST_FUNCTIONAL,
    TEST_PERFORMANCE,
    TEST_SHORT
  };

  template <typename Hash, typename Tran>
  class hash_tester
  {
    public:

      hash_tester () = delete;
      hash_tester (test_type tt);

      template <typename F, typename ...Args>
      void run_test (const std::string &case_name, F &&case_func, Args &&...args);
      template <typename F, typename ...Args>
      void build_hash_and_test (test_result &tres, size_t thread_count, size_t hash_size, F &&f, Args &&...args);

      template <typename F, typename ...Args>
      void start_threads (test_result &tres, Hash &hash, std::vector<Tran> &tran_array, F &&f, Args &&... args);

      static const char *TEST_MESSAGE_PREFIX;

      // cases
      static void testcase_inserts (test_result &tres, Hash &hash, Tran lftran, size_t insert_count);
      static void testcase_find_or_inserts_and_erase (test_result &tres, Hash &hash, Tran lftran,
	  size_t insert_count, size_t erase_count);
      static void testcase_insert_given_and_erase_and_claimret (test_result &tres, Hash &hash, Tran lftran,
	  size_t insert_count, size_t erase_count,
	  size_t claimret_count);
      static void testcase_find_or_inserts_and_erase_locked (test_result &tres, Hash &hash, Tran lftran,
	  size_t insert_count, size_t insdel_count);
      static void testcase_insdel_iter_clear (test_result &tres, Hash &hash, Tran lftran, size_t find_insert_count,
					      size_t erase_count, size_t iter_count, size_t clear_count);
      static void testcase_find_insdel_iter_clear (test_result &tres, Hash &hash, Tran lftran, size_t find_count,
	  size_t find_insert_count, size_t erase_count, size_t iter_count,
	  size_t clear_count);

    private:
      test_type m_tt;
      std::vector<size_t> m_thread_counts;
      std::vector<size_t> m_hash_sizes;
      cubmonitor::timer_stat m_timer_stat;
  };
  using my_lf_hash_table_tester = hash_tester<my_lf_hash_table, lf_tran_entry *>;
  using my_hashmap_tester = hash_tester<my_hashmap, tran::index>;

  template <typename Hash, typename Tran>
  void
  hash_tester<Hash, Tran>::testcase_inserts (test_result &tres, Hash &hash, Tran lftran, size_t insert_count)
  {
    my_key k;
    size_t inserted = 0;
    size_t rejected = 0;
    my_entry *ent;
    std::random_device rd;

    for (size_t i = 0; i < insert_count; ++i)
      {
	keygen_no_conflict (k, hash.get_size (), insert_count, rd);

	if (hash.insert (lftran, k, ent))
	  {
	    ++inserted;
	    hash.unlock (lftran, ent);
	  }
	else
	  {
	    ++rejected;
	  }
	ent = hash.find (lftran, k);
	assert (ent != NULL);
	hash.unlock (lftran, ent);
      }

    tres.m_insert_ops += insert_count;
    tres.m_find_ops += insert_count;
    tres.m_successful_inserts += inserted;
    tres.m_rejected_inserts += rejected;
    tres.m_found_on_finds += insert_count;
  }

  template <typename Hash, typename Tran>
  void
  hash_tester<Hash, Tran>::testcase_find_or_inserts_and_erase (test_result &tres, Hash &hash, Tran lftran,
      size_t insert_count, size_t erase_count)
  {
    my_key k;
    size_t inserts = 0;
    size_t found_inserts = 0;
    size_t erased = 0;
    size_t erase_not_found = 0;
    std::random_device rd;
    my_entry *myent;

    size_t hash_size = hash.get_size ();
    size_t total_ops = insert_count + erase_count;
    size_t random_op;
    size_t left_ops = total_ops;

    tres.m_find_or_insert_ops += insert_count;
    tres.m_erase_ops += erase_count;

    while (left_ops > 0)
      {
	keygen_high_conflict (k, hash_size, total_ops, rd);
	random_op = rd () % left_ops;
	if (random_op < insert_count)
	  {
	    if (hash.find_or_insert (lftran, k, myent))
	      {
		++inserts;
	      }
	    else
	      {
		++found_inserts;
	      }
	    assert (myent != NULL);
	    hash.unlock (lftran, myent);

	    --insert_count;
	  }
	else
	  {
	    if (hash.erase (lftran, k))
	      {
		++erased;
	      }
	    else
	      {
		++erase_not_found;
	      }

	    --erase_count;
	  }
	--left_ops;
      }

    tres.m_successful_inserts += inserts;
    tres.m_found_on_inserts += found_inserts;
    tres.m_found_on_erase_ops += erased;
    tres.m_not_found_on_erase_ops += erase_not_found;
  }

  template <typename Hash, typename Tran>
  void
  hash_tester<Hash, Tran>::testcase_insert_given_and_erase_and_claimret (test_result &tres, Hash &hash, Tran lftran,
      size_t insert_count, size_t erase_count,
      size_t claimret_count)
  {
    my_key k;
    size_t inserts = 0;
    size_t found_inserts = 0;
    size_t erased = 0;
    size_t erase_not_found = 0;
    std::random_device rd;
    my_entry *myent;

    size_t hash_size = hash.get_size ();
    size_t total_ops = insert_count + erase_count + claimret_count;
    size_t random_op;
    size_t left_ops = total_ops;

    tres.m_insert_given_ops += insert_count;
    tres.m_erase_ops += erase_count;
    tres.m_claim_ops = insert_count + claimret_count;
    tres.m_retire_ops = claimret_count;

    while (left_ops > 0)
      {
	keygen_high_conflict (k, hash_size, insert_count, rd);
	random_op = rd () % left_ops;

	if (random_op < claimret_count)
	  {
	    myent = hash.freelist_claim (lftran);
	    assert (myent != NULL);
	    hash.freelist_retire (lftran, myent);

	    --claimret_count;
	  }
	else if (random_op < insert_count + claimret_count)
	  {
	    myent = hash.freelist_claim (lftran);
	    assert (myent != NULL);
	    myent->m_key = k;

	    if (hash.insert_given (lftran, k, myent))
	      {
		++inserts;
	      }
	    else
	      {
		++found_inserts;
	      }
	    assert (myent != NULL);
	    hash.unlock (lftran, myent);

	    --insert_count;
	  }
	else
	  {
	    if (hash.erase (lftran, k))
	      {
		++erased;
	      }
	    else
	      {
		++erase_not_found;
	      }

	    --erase_count;
	  }
	--left_ops;
      }

    tres.m_successful_inserts += inserts;
    tres.m_found_on_inserts += found_inserts;
    tres.m_found_on_erase_ops += erased;
    tres.m_not_found_on_erase_ops += erase_not_found;
  }

  template <typename Hash, typename Tran>
  void
  hash_tester<Hash, Tran>::testcase_find_or_inserts_and_erase_locked (test_result &tres, Hash &hash, Tran lftran,
      size_t insert_count, size_t insdel_count)
  {
    my_key k;
    size_t inserts = 0;
    size_t found_inserts = 0;
    size_t erased = 0;
    std::random_device rd;
    my_entry *myent;

    assert (g_edesc.using_mutex != 0);

    size_t hash_size = hash.get_size ();
    size_t total_ops = insert_count + insdel_count;
    size_t random_op;
    size_t left_ops = total_ops;

    tres.m_find_or_insert_ops += total_ops;
    tres.m_erase_locked_ops += insdel_count;

    while (left_ops > 0)
      {
	keygen_high_conflict (k, hash_size, total_ops, rd);
	random_op = rd () % left_ops;

	if (hash.find_or_insert (lftran, k, myent))
	  {
	    ++inserts;
	  }
	else
	  {
	    ++found_inserts;
	  }
	assert (myent != NULL);

	if (random_op < insert_count)
	  {
	    // only insert
	    hash.unlock (lftran, myent);
	    --insert_count;
	  }
	else
	  {
	    // also erase
	    if (hash.erase_locked (lftran, k, myent) != true)
	      {
		assert (false);
	      }
	    ++erased;
	    --insdel_count;
	  }

	--left_ops;
      }

    tres.m_successful_inserts += inserts;
    tres.m_found_on_inserts += found_inserts;
    tres.m_found_on_erase_ops += erased;
  }

  template <typename Hash, typename Tran>
  void
  hash_tester<Hash, Tran>::testcase_insdel_iter_clear (test_result &tres, Hash &hash, Tran lftran,
      size_t find_insert_count, size_t erase_count, size_t iter_count,
      size_t clear_count)
  {
    my_key k;
    size_t inserts = 0;
    size_t found_inserts = 0;
    size_t erased = 0;
    size_t erase_not_found = 0;
    size_t iter_incr = 0;
    std::random_device rd;
    my_entry *myent;

    size_t hash_size = hash.get_size ();
    size_t total_ops = find_insert_count + erase_count + iter_count + clear_count;
    size_t random_op;
    size_t left_ops = total_ops;

    tres.m_find_or_insert_ops += find_insert_count;
    tres.m_erase_ops += erase_count;
    tres.m_iterate_ops += iter_count;
    tres.m_clear_ops += clear_count;

    while (left_ops > 0)
      {
	keygen_avg_conflict (k, hash_size, find_insert_count, rd);
	random_op = rd () % left_ops;

	if (random_op < find_insert_count)
	  {
	    if (hash.find_or_insert (lftran, k, myent))
	      {
		++inserts;
	      }
	    else
	      {
		++found_inserts;
	      }
	    assert (myent != NULL);
	    hash.unlock (lftran, myent);

	    --find_insert_count;
	  }
	else if (random_op < find_insert_count + erase_count)
	  {
	    if (hash.erase (lftran, k))
	      {
		++erased;
	      }
	    else
	      {
		++erase_not_found;
	      }

	    --erase_count;
	  }
	else if (random_op < find_insert_count + erase_count + iter_count)
	  {
	    typename Hash::iterator hash_iterator { lftran, hash };
	    for (my_entry *it_ent = hash_iterator.iterate (); it_ent != NULL; it_ent = hash_iterator.iterate ())
	      {
		++iter_incr;
	      }

	    --iter_count;
	  }
	else
	  {
	    hash.clear (lftran);
	    --clear_count;
	  }
	--left_ops;
      }

    tres.m_successful_inserts += inserts;
    tres.m_found_on_inserts += found_inserts;
    tres.m_found_on_erase_ops += erased;
    tres.m_not_found_on_erase_ops += erase_not_found;
    tres.m_iterate_increments += iter_incr;
  }

  template <typename Hash, typename Tran>
  void
  hash_tester<Hash, Tran>::testcase_find_insdel_iter_clear (test_result &tres, Hash &hash, Tran lftran,
      size_t find_count, size_t find_insert_count,
      size_t erase_count, size_t iter_count, size_t clear_count)
  {
    my_key k;
    size_t inserts = 0;
    size_t found_inserts = 0;
    size_t erased = 0;
    size_t erase_not_found = 0;
    size_t iter_incr = 0;
    size_t find_found = 0;
    size_t find_not_found = 0;
    std::random_device rd;
    my_entry *myent;

    size_t hash_size = hash.get_size ();
    size_t total_ops = find_count + find_insert_count + erase_count + iter_count + clear_count;
    size_t random_op;
    size_t left_ops = total_ops;

    tres.m_find_ops += find_count;
    tres.m_find_or_insert_ops += find_insert_count;
    tres.m_erase_ops += erase_count;
    tres.m_iterate_ops += iter_count;
    tres.m_clear_ops += clear_count;

    while (left_ops > 0)
      {
	keygen_avg_conflict (k, hash_size, find_insert_count, rd);
	random_op = rd () % left_ops;

	if (random_op < find_count)
	  {
	    myent = hash.find (lftran, k);
	    if (myent != NULL)
	      {
		find_found++;
		hash.unlock (lftran, myent);
	      }
	    else
	      {
		find_not_found++;
	      }

	    --find_count;
	  }
	else if (random_op < find_count + find_insert_count)
	  {
	    if (hash.find_or_insert (lftran, k, myent))
	      {
		++inserts;
	      }
	    else
	      {
		++found_inserts;
	      }
	    assert (myent != NULL);
	    hash.unlock (lftran, myent);

	    --find_insert_count;
	  }
	else if (random_op < find_count + find_insert_count + erase_count)
	  {
	    if (hash.erase (lftran, k))
	      {
		++erased;
	      }
	    else
	      {
		++erase_not_found;
	      }

	    --erase_count;
	  }
	else if (random_op < find_count + find_insert_count + erase_count + iter_count)
	  {
	    typename Hash::iterator hash_iterator { lftran, hash };
	    for (my_entry *it_ent = hash_iterator.iterate (); it_ent != NULL; it_ent = hash_iterator.iterate ())
	      {
		++iter_count;
	      }

	    --iter_count;
	  }
	else
	  {
	    hash.clear (lftran);
	    --clear_count;
	  }
	--left_ops;
      }

    tres.m_successful_inserts += inserts;
    tres.m_found_on_inserts += found_inserts;
    tres.m_found_on_erase_ops += erased;
    tres.m_not_found_on_erase_ops += erase_not_found;
    tres.m_iterate_increments += iter_count;
    tres.m_found_on_finds += find_found;
    tres.m_not_found_on_finds += find_not_found;
  }

  template <> const char *my_lf_hash_table_tester::TEST_MESSAGE_PREFIX = "test lf_hash_table";
  template <> const char *my_hashmap_tester::TEST_MESSAGE_PREFIX = "test lockfree::hashmap";

  template <typename Hash, typename Tran>
  hash_tester<Hash, Tran>::hash_tester (test_type tt)
    : m_tt (tt)
    , m_thread_counts {}
    , m_hash_sizes {}
    , m_timer_stat ()
  {
    switch (m_tt)
      {
      case test_lockfree::TEST_FUNCTIONAL:
	m_thread_counts = { 1, 4, 64 };
	m_hash_sizes = { 100, 10000 };
	break;
      case test_lockfree::TEST_PERFORMANCE:
	m_thread_counts = { 64 };
	m_hash_sizes = { 1000 };
	break;
      case test_lockfree::TEST_SHORT:
	m_thread_counts = { 1, 4 };
	m_hash_sizes = { 10000 };
	break;
      default:
	break;
      }
  }

  template <typename Hash, typename Tran>
  template <typename F, typename ... Args>
  void
  hash_tester<Hash, Tran>::start_threads (test_result &tres, Hash &hash, std::vector<Tran> &tran_array, F &&f,
					  Args &&... args)
  {
    size_t count = tran_array.size ();
    std::vector<std::thread> all_threads;
    all_threads.reserve (count);

    for (size_t i = 0; i < count; i++)
      {
	all_threads[i] = std::thread (std::forward<F> (f), std::ref (tres), std::ref (hash), std::ref (tran_array[i]),
				      std::forward<Args> (args)...);
      }
    for (size_t i = 0; i < count; i++)
      {
	all_threads[i].join ();
      }
  }

  template <>
  template <typename F, typename ... Args>
  void
  hash_tester<my_lf_hash_table, lf_tran_entry *>::build_hash_and_test (test_result &tres, size_t thread_count,
      size_t hash_size, F &&f, Args &&...args)
  {
    lf_tran_system l_transys;
    lf_tran_system_init (&l_transys, (int) thread_count);

    std::vector<lf_tran_entry *> l_tran_ents;
    for (size_t i = 0; i < thread_count; ++i)
      {
	l_tran_ents.push_back (lf_tran_request_entry (&l_transys));
      }

    my_lf_hash_table l_hash;
    init_lf_hash_table (l_transys, (int) hash_size, l_hash);

    tres.m_timer.reset_timer ();
    start_threads (tres, l_hash, l_tran_ents, std::forward<F> (f), std::forward<Args> (args)...);
    tres.m_timer.time ();

    l_hash.destroy ();
    for (size_t i = 0; i < thread_count; ++i)
      {
	lf_tran_return_entry (l_tran_ents[i]);
      }
    lf_tran_system_destroy (&l_transys);
  }

  template <>
  template <typename F, typename ... Args>
  void
  hash_tester<my_hashmap, tran::index>::build_hash_and_test (test_result &tres, size_t thread_count, size_t hash_size,
      F &&f, Args &&...args)
  {
    tran::system l_transys { thread_count };

    std::vector<tran::index> l_indexes;
    for (size_t i = 0; i < thread_count; i++)
      {
	l_indexes.push_back (l_transys.assign_index ());
      }

    my_hashmap l_hash;
    init_hashmap (l_transys, hash_size, l_hash);

    l_hash.activate_stats ();

    tres.m_timer.reset_timer ();
    start_threads (tres, l_hash, l_indexes, std::forward<F> (f), std::forward<Args> (args)...);
    tres.m_timer.time ();

    cout_new_line ();
    std::cout << "hash stats: ";
    l_hash.dump_stats<std::chrono::milliseconds> (std::cout);
    l_hash.destroy ();
    for (size_t i = 0; i < thread_count; ++i)
      {
	l_transys.free_index (l_indexes[i]);
      }
  }

  template <typename Hash, typename Tran>
  template <typename F, typename ... Args>
  void
  hash_tester<Hash, Tran>::run_test (const std::string &case_name, F &&f, Args &&...args)
  {
    cout_new_line ();
    m_timer_stat = {};

    for (size_t tc_iter = 0; tc_iter < m_thread_counts.size (); tc_iter++)
      {
	for (size_t hs_iter = 0; hs_iter < m_hash_sizes.size (); hs_iter++)
	  {
	    test_result tres;

	    cout_new_line ();
	    std::cout << TEST_MESSAGE_PREFIX << "|" << case_name;
	    std::cout << " [tcnt = " << m_thread_counts[tc_iter] << ", hsz = " << m_hash_sizes[hs_iter] << "]";
	    increment_tab_indent ();

	    build_hash_and_test (tres, m_thread_counts[tc_iter], m_hash_sizes[hs_iter], std::forward<F> (f),
				 std::forward<Args> (args)...);

	    m_timer_stat.time (tres.m_timer.get_time ());
	    tres.dump_stats ();
	    decrement_tab_indent ();
	  }
      }

    cout_new_line ();
    std::cout << "test took ";
    cout_msec_count (m_timer_stat.get_time ());
  }

  template <typename F, typename ... Args>
  void
  run_test_hashmap (bool for_perf, bool short_version, const std::string &case_name, F &&f, Args &&... args)
  {
    cout_new_line ();
#if 0
    timer_stat testcase_timer;
    auto start_time = std::chrono::high_resolution_clock::now ();

    if (!for_perf)
      {
	test_hashmap_varsizes (for_perf, 1, case_name, testcase_timer, std::forward<F> (f),
			       std::forward<Args> (args)...);
	test_hashmap_varsizes (for_perf, 2, case_name, testcase_timer, std::forward<F> (f),
			       std::forward<Args> (args)...);
      }
    if (!short_version)
      {
	test_hashmap_varsizes (for_perf, 64, case_name, testcase_timer, std::forward<F> (f),
			       std::forward<Args> (args)...);
      }

    auto end_time = std::chrono::high_resolution_clock::now ();
    cout_new_line ();
    std::cout << "test took ";
    cubmonitor::time_rep total_time = testcase_timer.get_time ();
    std::cout << (std::chrono::duration_cast<std::chrono::milliseconds> (total_time)).count () << " msec";
#endif
  }

  template <typename F, typename ... Args>
  void
  run_test_lf_hash_table (bool for_perf, bool short_version, const std::string &case_name, F &&f, Args &&... args)
  {
    cout_new_line ();

#if 0
    auto start_time = std::chrono::high_resolution_clock::now ();

    if (!for_perf)
      {
	test_lf_hash_table_varsizes<1> (for_perf, case_name, std::forward<F> (f), std::forward<Args> (args)...);
	test_lf_hash_table_varsizes<4> (for_perf, case_name, std::forward<F> (f), std::forward<Args> (args)...);
      }
    if (!short_version)
      {
	test_lf_hash_table_varsizes<64> (for_perf, case_name, std::forward<F> (f), std::forward<Args> (args)...);
      }

    auto end_time = std::chrono::high_resolution_clock::now ();
    cout_new_line ();
    std::cout << "test took ";
    std::cout << (std::chrono::duration_cast<std::chrono::milliseconds> (end_time - start_time)).count () << " msec";
#endif
  }

#define TEMPL_LFHT my_lf_hash_table, lf_tran_entry *
#define TEMPL_HASHMAP my_hashmap, tran::index

  static void
  test_hashmap_functional_internal (bool mutex_on_off, bool short_version)
  {
    set_entry_mutex_mode (mutex_on_off);

    test_type tt = short_version ? test_type::TEST_SHORT : test_type::TEST_FUNCTIONAL;
    my_hashmap_tester hm_tester { tt };
    my_lf_hash_table_tester lfht_tester { tt };

    cout_new_line ();
    std::cout << "Start testing lock-free hashmap/hash table with mutex " << mutex_on_off;
    increment_tab_indent ();

    lfht_tester.run_test ("insert_find=10000", &my_lf_hash_table_tester::testcase_inserts, 10000);
    hm_tester.run_test ("insert_find=10000", &my_hashmap_tester::testcase_inserts, 10000);

    lfht_tester.run_test ("find_or_insert_and_erase=10000,1000",
			  &my_lf_hash_table_tester::testcase_find_or_inserts_and_erase, 10000, 1000);
    hm_tester.run_test ("find_or_insert_and_erase=10000,1000", &my_hashmap_tester::testcase_find_or_inserts_and_erase,
			10000, 1000);

    lfht_tester.run_test ("testcase_insert_given_and_erase_and_claimret=10000,1000,1000",
			  &my_lf_hash_table_tester::testcase_insert_given_and_erase_and_claimret, 10000, 1000, 1000);
    hm_tester.run_test ("testcase_insert_given_and_erase_and_claimret=10000,1000,1000",
			&my_hashmap_tester::testcase_insert_given_and_erase_and_claimret, 10000, 1000, 1000);

    if (mutex_on_off == MUTEX_ON)
      {
	lfht_tester.run_test ("testcase_find_or_inserts_and_erase_locked=10000,1000",
			      &my_lf_hash_table_tester::testcase_find_or_inserts_and_erase_locked, 10000, 1000);
	hm_tester.run_test ("testcase_find_or_inserts_and_erase_locked=10000,1000",
			    &my_hashmap_tester::testcase_find_or_inserts_and_erase_locked, 10000, 1000);
      }

    lfht_tester.run_test ("testcase_insdel_iter_clear=10000,1000,100,10",
			  &my_lf_hash_table_tester::testcase_insdel_iter_clear, 10000, 1000, 100, 10);
    hm_tester.run_test ("testcase_insdel_iter_clear=10000,1000,100,10",
			&my_hashmap_tester::testcase_insdel_iter_clear, 10000, 1000, 100, 10);

    decrement_tab_indent ();
    cout_new_line ();
  }

  int
  test_hashmap_functional (bool short_version)
  {
    test_hashmap_functional_internal (MUTEX_OFF, short_version);
    test_hashmap_functional_internal (MUTEX_ON, short_version);

    return 0;
  }

  static void
  test_hashmap_performance_internal (bool mutex_on_or_off)
  {
    set_entry_mutex_mode (mutex_on_or_off);

    test_type tt = test_type::TEST_PERFORMANCE;
    my_hashmap_tester hm_tester { tt };
    my_lf_hash_table_tester lfht_tester { tt };

    cout_new_line ();
    std::cout << "Start performance testing lock-free hashmap/hash table with mutex " << mutex_on_or_off;
    increment_tab_indent ();

    lfht_tester.run_test ("testcase_find_insdel_iter_clear=1M,30k,5k,1,1",
			  &my_lf_hash_table_tester::testcase_find_insdel_iter_clear, 1000000, 30000, 5000, 1, 1);
    hm_tester.run_test ("testcase_find_insdel_iter_clear=1M,30k,5k,1,1",
			&my_hashmap_tester::testcase_find_insdel_iter_clear, 1000000, 30000, 5000, 1, 1);

    decrement_tab_indent ();
    cout_new_line ();
  }

  int
  test_hashmap_performance ()
  {
    test_hashmap_performance_internal (false);
    test_hashmap_performance_internal (true);

    return 0;
  }

  static int
  copy_my_key (void *src, void *dest)
  {
    * (my_key *) dest = * (my_key *) src;
    return 0;
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

  static void
  keygen_no_conflict (my_key &k, size_t hash_size, size_t nops, std::random_device &rd)
  {
    unsigned int bucket_size = std::numeric_limits<unsigned int>::max ();
    std::uniform_int_distribution<unsigned int> uid1 (0, (unsigned int) hash_size);
    std::uniform_int_distribution<unsigned int> uid2 (0, bucket_size);
    k.m_1 = uid1 (rd);
    k.m_2 = uid2 (rd);
  }

  static void
  keygen_avg_conflict (my_key &k, size_t hash_size, size_t nops, std::random_device &rd)
  {
    k.m_1 = std::rand () % hash_size;
    size_t bucket_size = std::max ((size_t ) 2, (nops / hash_size) * 5);
    k.m_2 = std::rand () % bucket_size;
  }

  static void
  keygen_high_conflict (my_key &k, size_t hash_size, size_t nops, std::random_device &rd)
  {
    k.m_1 = std::rand () % hash_size;
    size_t bucket_size = std::max ((size_t ) 2, (nops / hash_size));
    k.m_2 = std::rand () % bucket_size;
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
    return 0;
  }

  static int
  init_my_entry (void *p)
  {
    my_entry *e = (my_entry *) p;
    assert (!e->m_init);
    e->m_init = true;
    return 0;
  }

  static int
  uninit_my_entry (void *p)
  {
    my_entry *e = (my_entry *) p;
    assert (e->m_init);
    e->m_init = false;
    return 0;
  }

  static void
  init_lf_hash_table (lf_tran_system &transys, int hash_size, my_lf_hash_table &hash)
  {
    hash.init (transys, hash_size, 100, 100, g_edesc);
  }

  static void
  init_hashmap (tran::system &transys, size_t hash_size, my_hashmap &hash)
  {
    hash.init (transys, hash_size, 100, 100, g_edesc);
  }
} // namespace test_lockfree
