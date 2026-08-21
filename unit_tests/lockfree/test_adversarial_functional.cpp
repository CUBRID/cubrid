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

//
// Adversarial functional cases for the lockfree:: rewrite, aimed at what CBRD-24794 does not already cover.
//
// Every case that can be run on both implementations is run on both, under identical load, so a failure can be
// attributed to the rewrite rather than to the design both share.
//

#include "test_adversarial_functional.hpp"

#include "test_debug.hpp"
#include "test_output.hpp"

#include "lock_free.h"
#include "lockfree_freelist.hpp"
#include "lockfree_hashmap.hpp"
#include "lockfree_transaction_system.hpp"
#include "string_buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

using namespace lockfree;

namespace test_lockfree
{
  //
  // fixtures
  //
  // A private copy of the entry type and the entry descriptor. test_hashmap.cpp has its own: these cases move
  // max_alloc_cnt and make f_init fail, and must not disturb it.
  //

  struct adv_key
  {
    unsigned int m_1;
    unsigned int m_2;
  };

  static const unsigned int ADV_MAGIC_FREE = 0u;              // uninitialized, or reclaimed
  static const unsigned int ADV_MAGIC_CLAIMED = 0xc1a1ed00u;  // f_init ran, payload not written yet
  static const unsigned int ADV_MAGIC_LIVE = 0x11feda7au;     // key, key copy and payload all written

  struct adv_entry
  {
    adv_key m_key;
    adv_entry *m_next;
    adv_entry *m_rstack;
    pthread_mutex_t m_mutex;
    UINT64 m_delid;

    // a second copy of the key and a life-cycle mark, both written before the entry is made reachable, so a
    // thread that reaches it may assume all three agree for as long as it holds it.
    adv_key m_key_copy;
    unsigned int m_magic;
    int m_init_answer;      // what f_init answered the last time this entry was claimed

    adv_entry () = default;
    ~adv_entry () = default;
  };

  static std::atomic<bool> g_f_init_must_fail { false };

  static void *
  adv_alloc_entry ()
  {
    return (void *) new adv_entry ();
  }

  static int
  adv_free_entry (void *p)
  {
    delete (adv_entry *) p;
    return NO_ERROR;
  }

  static int
  adv_init_entry (void *p)
  {
    adv_entry *e = (adv_entry *) p;
    if (g_f_init_must_fail)
      {
	// fpcache_entry_init () fails exactly this way when the clone stack malloc fails
	e->m_init_answer = ER_FAILED;
	e->m_magic = ADV_MAGIC_FREE;
	return ER_FAILED;
      }
    e->m_init_answer = NO_ERROR;
    e->m_magic = ADV_MAGIC_CLAIMED;
    return NO_ERROR;
  }

  static int
  adv_uninit_entry (void *p)
  {
    adv_entry *e = (adv_entry *) p;
    e->m_magic = ADV_MAGIC_FREE;
    return NO_ERROR;
  }

  static int
  adv_copy_key (void *src, void *dest)
  {
    * (adv_key *) dest = * (adv_key *) src;
    return NO_ERROR;
  }

  static int
  adv_compare_key (void *key1, void *key2)
  {
    adv_key *a = (adv_key *) key1;
    adv_key *b = (adv_key *) key2;
    return (a->m_1 != b->m_1) || (a->m_2 != b->m_2) ? 1 : 0;
  }

  static unsigned int
  adv_hash_key (void *key, int hash_size)
  {
    return ((adv_key *) key)->m_1 % (unsigned int) hash_size;
  }

  static lf_entry_descriptor g_adv_edesc =
  {
    offsetof (adv_entry, m_rstack),
    offsetof (adv_entry, m_next),
    offsetof (adv_entry, m_delid),
    offsetof (adv_entry, m_key),
    offsetof (adv_entry, m_mutex),

    LF_EM_NOT_USING_MUTEX,

    LF_ENTRY_DESCRIPTOR_MAX_ALLOC,

    adv_alloc_entry,
    adv_free_entry,
    adv_init_entry,
    adv_uninit_entry,
    adv_copy_key,
    adv_compare_key,
    adv_hash_key,
    NULL
  };

  using adv_hashmap = hashmap<adv_key, adv_entry>;
  using adv_lf_hash = lf_hash_table_cpp<adv_key, adv_entry>;

  static void
  say (const string_buffer &line)
  {
    test_common::sync_cout (line.get_buffer ());
  }

  static unsigned int
  next_rand (unsigned int state)
  {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  static int case_f_init_error_dropped ();
  static int case_oom_freelist_state ();
  static int case_max_alloc_cap ();
  static int case_iterator_edges ();
  static int case_iterate_under_churn ();
  static int case_tiny_block_pressure ();

  int
  test_adversarial_functional ()
  {
    test_common::sync_cout ("start test_adversarial_functional\n");

    int err = 0;
    err = err | case_f_init_error_dropped ();
    err = err | case_oom_freelist_state ();
    err = err | case_max_alloc_cap ();
    err = err | case_iterator_edges ();
    err = err | case_iterate_under_churn ();
    err = err | case_tiny_block_pressure ();

    if (err == 0)
      {
	test_common::sync_cout ("success test_adversarial_functional\n");
      }
    else
      {
	test_common::sync_cout ("failed test_adversarial_functional\n");
      }
    return err;
  }

  //
  // case_f_init_error_dropped () - freelist_claim () must answer NULL when f_init fails.
  //
  // lf_freelist_claim () tests f_init's answer (lock_free.c:825-833) and callers are written to that contract -
  // xcache_new_entry () checks for NULL (xasl_cache.c:1574). The rewrite dropped the answer and handed the entry
  // back as ready. The f_init that can fail is fpcache_entry_init (), which leaves clone_stack NULL on a failed
  // malloc, and filter_pred_cache.c:389 dereferences it.
  //
  static int
  case_f_init_error_dropped ()
  {
    const size_t HASH_SIZE = 8;
    const size_t BLOCK_SIZE = 4;
    const size_t BLOCK_COUNT = 2;

    test_common::sync_cout ("case_f_init_error_dropped\n");
    int err = 0;

    // the rewrite
    {
      tran::system l_transys { 2 };
      tran::index l_index = l_transys.assign_index ();
      adv_hashmap l_hash;
      l_hash.init (l_transys, HASH_SIZE, BLOCK_SIZE, BLOCK_COUNT, g_adv_edesc);

      g_f_init_must_fail = true;
      adv_entry *claimed = l_hash.freelist_claim (l_index);
      g_f_init_must_fail = false;

      string_buffer line;
      line ("  lockfree::hashmap : freelist_claim () = %s", claimed == NULL ? "NULL" : "an entry");
      if (claimed != NULL)
	{
	  line (" whose f_init answered %d and whose magic is 0x%x", claimed->m_init_answer, claimed->m_magic);
	  err = 1;
	}
      line ("\n");
      say (line);

      if (claimed != NULL)
	{
	  l_hash.freelist_retire (l_index, claimed);
	}
      l_hash.destroy ();
      l_transys.free_index (l_index);
    }

    // the implementation it replaces, same descriptor, same load
    {
      lf_tran_system l_transys;
      lf_tran_system_init (&l_transys, 2);
      adv_lf_hash l_hash;
      l_hash.init (l_transys, (int) HASH_SIZE, (int) BLOCK_COUNT, (int) BLOCK_SIZE, g_adv_edesc);
      lf_tran_entry *l_tran = lf_tran_request_entry (&l_transys);

      g_f_init_must_fail = true;
      adv_entry *claimed = l_hash.freelist_claim (l_tran);
      g_f_init_must_fail = false;

      string_buffer line;
      line ("  lf_hash_table    : freelist_claim () = %s\n", claimed == NULL ? "NULL" : "an entry");
      say (line);

      if (claimed != NULL)
	{
	  l_hash.freelist_retire (l_tran, claimed);
	}
      lf_tran_return_entry (l_tran);
      l_hash.destroy ();
      lf_tran_system_destroy (&l_transys);
    }

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: f_init's error answer is dropped by lockfree::hashmap\n");
      }
    return err;
  }

  //
  // case_oom_freelist_state () - what the new freelist looks like when every allocation fails.
  //
  // A node larger than the address space makes new (std::nothrow) answer NULL without touching a page, so the
  // out-of-memory path runs deterministically. claim () must answer NULL, and must leave the transaction it
  // started ENDED - a descriptor keeping a published id pins the table's minimum active id and stops
  // reclamation for good, and lf_freelist_claim () does end it (lock_free.c:850-857). The freelist must also
  // survive its own destructor: it cannot fill the back-buffer here, and final_sanity_checks () has to admit
  // the empty buffer alloc_backbuffer () deliberately leaves behind.
  //
  // The legacy side is read, not run: er_set () asserts with no error manager in this binary.
  //
  struct adv_oom_data
  {
    // a user-provided constructor, so value-initializing it does not try to zero the payload
    adv_oom_data () {}
    char m_payload[ (std::size_t) 1 << 62];
    void on_reclaim () {}
  };

  static int
  case_oom_freelist_state ()
  {
    const size_t BLOCK_SIZE = 8;
    const size_t BLOCK_COUNT = 2;

    test_common::sync_cout ("case_oom_freelist_state\n");
    int err = 0;

    tran::system *l_transys = new tran::system { 2 };
    freelist<adv_oom_data> *l_freelist = new freelist<adv_oom_data> { *l_transys, BLOCK_SIZE, BLOCK_COUNT };
    tran::index l_index = l_transys->assign_index ();

    freelist<adv_oom_data>::free_node *node = l_freelist->claim (l_index);
    const bool tran_left_started = l_freelist->get_transaction_table ().get_descriptor (l_index).is_tran_started ();
    if (tran_left_started)
      {
	// end it here, or the descriptor destructor would assert
	l_freelist->get_transaction_table ().end_tran (l_index);
      }
    (void) BLOCK_COUNT;

    const size_t bb_count = l_freelist->get_backbuffer_count ();
    const size_t alloc_count = l_freelist->get_alloc_count ();

    string_buffer line;
    line ("  claim () = %s, transaction left %s, alloc = %zu, backbuffer = %zu (block size %zu)\n",
	  node == NULL ? "NULL" : "a node", tran_left_started ? "STARTED" : "ended", alloc_count, bb_count,
	  BLOCK_SIZE);
    say (line);

    if (node != NULL)
      {
	test_common::sync_cout ("  FAILED: claim () answered a node although no allocation can succeed\n");
	err = 1;
      }
    if (tran_left_started)
      {
	test_common::sync_cout ("  FAILED: claim () returned NULL with the transaction still started\n");
	err = 1;
      }

    // the destructor runs final_sanity_checks (): with nothing allocated the back-buffer is empty, and the
    // invariant has to accept that rather than abort a debug build on a boot-time out-of-memory.
    l_transys->free_index (l_index);
    delete l_freelist;
    delete l_transys;
    return err;
  }

  //
  // case_max_alloc_cap () - edesc.max_alloc_cnt must still bound the freelist, and the element count must come
  //                         back to zero exactly.
  //
  // Self-calibrating: the same load runs capped and uncapped on each implementation, so a cap that is not
  // enforced shows up as two equal allocation counts rather than as a threshold nobody can justify.
  //
  static const size_t CAP_HASH_SIZE = 64;
  static const size_t CAP_BLOCK_SIZE = 32;
  static const size_t CAP_BLOCK_COUNT = 2;
  static const unsigned int CAP_FILL = 2000;
  static const size_t CAP_DRAIN_CYCLES = 6000;

  static size_t
  run_cap_workload_new (int max_alloc_cnt, size_t &live_after_fill, size_t &live_after_drain)
  {
    g_adv_edesc.max_alloc_cnt = max_alloc_cnt;

    tran::system l_transys { 2 };
    tran::index l_index = l_transys.assign_index ();
    adv_hashmap l_hash;
    l_hash.init (l_transys, CAP_HASH_SIZE, CAP_BLOCK_SIZE, CAP_BLOCK_COUNT, g_adv_edesc);

    for (unsigned int i = 0; i < CAP_FILL; i++)
      {
	adv_key k = { i, i };
	adv_entry *e = l_hash.freelist_claim (l_index);
	test_common::custom_assert (e != NULL);
	e->m_key = k;
	e->m_key_copy = k;
	e->m_magic = ADV_MAGIC_LIVE;
	test_common::custom_assert (l_hash.insert_given (l_index, k, e));
	l_hash.unlock (l_index, e);
      }
    live_after_fill = l_hash.get_element_count ();

    for (unsigned int i = 0; i < CAP_FILL; i++)
      {
	adv_key k = { i, i };
	test_common::custom_assert (l_hash.erase (l_index, k));
      }

    // the minimum active id only moves every MATI_REFRESH_INTERVAL retires, so reclamation needs traffic
    for (size_t i = 0; i < CAP_DRAIN_CYCLES; i++)
      {
	adv_key k = { 0xdeadu, (unsigned int) i };
	adv_entry *e = l_hash.freelist_claim (l_index);
	test_common::custom_assert (e != NULL);
	e->m_key = k;
	e->m_key_copy = k;
	e->m_magic = ADV_MAGIC_LIVE;
	const bool inserted = l_hash.insert_given (l_index, k, e);
	l_hash.unlock (l_index, e);
	if (inserted)
	  {
	    test_common::custom_assert (l_hash.erase (l_index, k));
	  }
      }

    const size_t alloc = l_hash.get_alloc_element_count ();
    live_after_drain = l_hash.get_element_count ();

    l_hash.destroy ();
    l_transys.free_index (l_index);
    g_adv_edesc.max_alloc_cnt = LF_ENTRY_DESCRIPTOR_MAX_ALLOC;
    return alloc;
  }

  static size_t
  run_cap_workload_old (int max_alloc_cnt, size_t &live_after_fill, size_t &live_after_drain)
  {
    g_adv_edesc.max_alloc_cnt = max_alloc_cnt;

    lf_tran_system l_transys;
    lf_tran_system_init (&l_transys, 2);
    adv_lf_hash l_hash;
    l_hash.init (l_transys, (int) CAP_HASH_SIZE, (int) CAP_BLOCK_COUNT, (int) CAP_BLOCK_SIZE, g_adv_edesc);
    lf_tran_entry *l_tran = lf_tran_request_entry (&l_transys);

    for (unsigned int i = 0; i < CAP_FILL; i++)
      {
	adv_key k = { i, i };
	adv_entry *e = l_hash.freelist_claim (l_tran);
	test_common::custom_assert (e != NULL);
	e->m_key = k;
	e->m_key_copy = k;
	e->m_magic = ADV_MAGIC_LIVE;
	test_common::custom_assert (l_hash.insert_given (l_tran, k, e));
	l_hash.unlock (l_tran, e);
      }
    live_after_fill = l_hash.get_element_count ();

    for (unsigned int i = 0; i < CAP_FILL; i++)
      {
	adv_key k = { i, i };
	test_common::custom_assert (l_hash.erase (l_tran, k));
      }

    for (size_t i = 0; i < CAP_DRAIN_CYCLES; i++)
      {
	adv_key k = { 0xdeadu, (unsigned int) i };
	adv_entry *e = l_hash.freelist_claim (l_tran);
	test_common::custom_assert (e != NULL);
	e->m_key = k;
	e->m_key_copy = k;
	e->m_magic = ADV_MAGIC_LIVE;
	const bool inserted = l_hash.insert_given (l_tran, k, e);
	l_hash.unlock (l_tran, e);
	if (inserted)
	  {
	    test_common::custom_assert (l_hash.erase (l_tran, k));
	  }
      }

    const size_t alloc = l_hash.get_alloc_element_count ();
    live_after_drain = l_hash.get_element_count ();

    lf_tran_return_entry (l_tran);
    l_hash.destroy ();
    lf_tran_system_destroy (&l_transys);
    g_adv_edesc.max_alloc_cnt = LF_ENTRY_DESCRIPTOR_MAX_ALLOC;
    return alloc;
  }

  static int
  case_max_alloc_cap ()
  {
    const int CAP = 128;
    // the cap only stops recycling, so a block in the back-buffer and a couple of forced blocks are expected
    const size_t CAP_SLACK = 4 * CAP_BLOCK_SIZE;

    test_common::sync_cout ("case_max_alloc_cap\n");
    int err = 0;

    size_t new_fill = 0, new_drain = 0, old_fill = 0, old_drain = 0;
    const size_t new_uncapped = run_cap_workload_new (LF_ENTRY_DESCRIPTOR_MAX_ALLOC, new_fill, new_drain);
    const size_t new_capped = run_cap_workload_new (CAP, new_fill, new_drain);
    const size_t old_uncapped = run_cap_workload_old (LF_ENTRY_DESCRIPTOR_MAX_ALLOC, old_fill, old_drain);
    const size_t old_capped = run_cap_workload_old (CAP, old_fill, old_drain);

    string_buffer line;
    line ("  lockfree::hashmap : alloc uncapped = %zu, alloc capped at %d = %zu, live after fill = %zu,"
	  " live after drain = %zu\n", new_uncapped, CAP, new_capped, new_fill, new_drain);
    line ("  lf_hash_table     : alloc uncapped = %zu, alloc capped at %d = %zu, live after fill = %zu,"
	  " live after drain = %zu\n", old_uncapped, CAP, old_capped, old_fill, old_drain);
    say (line);

    if (new_capped > (size_t) CAP + CAP_SLACK)
      {
	test_common::sync_cout ("  FAILED: lockfree::hashmap did not hold the max_alloc_cnt cap\n");
	err = 1;
      }
    if (new_capped >= new_uncapped)
      {
	test_common::sync_cout ("  FAILED: lockfree::hashmap allocated as much capped as uncapped\n");
	err = 1;
      }
    if (new_fill != CAP_FILL)
      {
	test_common::sync_cout ("  FAILED: lockfree::hashmap element count after the fill is not the fill\n");
	err = 1;
      }
    if (new_drain != 0)
      {
	test_common::sync_cout ("  FAILED: lockfree::hashmap element count did not return to zero\n");
	err = 1;
      }
    return err;
  }

  //
  // case_iterator_edges () - iterator::restart () in the shapes the callers actually produce, and a few they
  //                          do not.
  //
  // testcase_iterator_restart () covers one interruption mid-pass. These add a restart before the first
  // iterate (), after a pass ran to its end, twice in a row, from the very last entry, and across a clear ().
  //
  static const size_t ITER_HASH_SIZE = 8;
  static const unsigned int ITER_ENTRY_COUNT = 40;

  static size_t
  full_pass_new (adv_hashmap::iterator &iter)
  {
    size_t seen = 0;
    for (adv_entry *e = iter.iterate (); e != NULL; e = iter.iterate ())
      {
	++seen;
      }
    return seen;
  }

  static size_t
  full_pass_old (adv_lf_hash::iterator &iter)
  {
    size_t seen = 0;
    for (adv_entry *e = iter.iterate (); e != NULL; e = iter.iterate ())
      {
	++seen;
      }
    return seen;
  }

  static int
  check_shape (const char *impl, const char *shape, size_t seen, size_t expected, bool count_it)
  {
    string_buffer line;
    line ("  %-17s %-34s saw %zu of %zu%s\n", impl, shape, seen, expected,
	  seen == expected ? "" : "   <-- MISMATCH");
    say (line);
    return (count_it && seen != expected) ? 1 : 0;
  }

  static int
  case_iterator_edges ()
  {
    test_common::sync_cout ("case_iterator_edges\n");
    int err = 0;

    // the rewrite
    {
      tran::system l_transys { 2 };
      tran::index l_index = l_transys.assign_index ();
      adv_hashmap l_hash;
      l_hash.init (l_transys, ITER_HASH_SIZE, 16, 4, g_adv_edesc);

      for (unsigned int i = 0; i < ITER_ENTRY_COUNT; i++)
	{
	  adv_key k = { i, i };
	  adv_entry *e = l_hash.freelist_claim (l_index);
	  test_common::custom_assert (e != NULL);
	  e->m_key = k;
	  e->m_key_copy = k;
	  e->m_magic = ADV_MAGIC_LIVE;
	  test_common::custom_assert (l_hash.insert_given (l_index, k, e));
	  l_hash.unlock (l_index, e);
	}

      {
	// restart before the first iterate (), the shape session_remove_expired_sessions () always takes
	adv_hashmap::iterator iter { l_index, l_hash };
	iter.restart ();
	err = err | check_shape ("lockfree::hashmap", "restart before first iterate",
				 full_pass_new (iter), ITER_ENTRY_COUNT, true);
      }
      {
	// a pass that ran to its end, restarted, and run again
	adv_hashmap::iterator iter { l_index, l_hash };
	(void) full_pass_new (iter);
	iter.restart ();
	err = err | check_shape ("lockfree::hashmap", "restart after a complete pass",
				 full_pass_new (iter), ITER_ENTRY_COUNT, true);
      }
      {
	adv_hashmap::iterator iter { l_index, l_hash };
	(void) full_pass_new (iter);
	iter.restart ();
	iter.restart ();
	err = err | check_shape ("lockfree::hashmap", "two restarts in a row",
				 full_pass_new (iter), ITER_ENTRY_COUNT, true);
      }
      {
	// interrupted while sitting on the last entry of the last non-empty bucket
	adv_hashmap::iterator iter { l_index, l_hash };
	size_t walked = 0;
	adv_entry *e = NULL;
	while (walked < ITER_ENTRY_COUNT)
	  {
	    e = iter.iterate ();
	    test_common::custom_assert (e != NULL);
	    ++walked;
	  }
	l_hash.end_tran (l_index);
	iter.restart ();
	err = err | check_shape ("lockfree::hashmap", "restart from the last entry",
				 full_pass_new (iter), ITER_ENTRY_COUNT, true);
      }
      {
	// interrupted after three entries, the shape xcache_invalidate_qcaches () takes on a full buffer
	adv_hashmap::iterator iter { l_index, l_hash };
	for (size_t i = 0; i < 3; i++)
	  {
	    test_common::custom_assert (iter.iterate () != NULL);
	  }
	l_hash.end_tran (l_index);
	iter.restart ();
	err = err | check_shape ("lockfree::hashmap", "restart after three entries",
				 full_pass_new (iter), ITER_ENTRY_COUNT, true);
      }
      {
	// interrupted after three entries, then the map is cleared under the iterator
	adv_hashmap::iterator iter { l_index, l_hash };
	for (size_t i = 0; i < 3; i++)
	  {
	    test_common::custom_assert (iter.iterate () != NULL);
	  }
	l_hash.end_tran (l_index);
	l_hash.clear (l_index);
	iter.restart ();
	err = err | check_shape ("lockfree::hashmap", "restart across a clear ()",
				 full_pass_new (iter), 0, true);
      }

      l_hash.destroy ();
      l_transys.free_index (l_index);
    }

    // the implementation it replaces. the clear () shape is left out on purpose: lf_hash_table_cpp's restart ()
    // does not reset its current entry, so a restart taken mid-pass walks the chain of retired entries.
    {
      lf_tran_system l_transys;
      lf_tran_system_init (&l_transys, 2);
      adv_lf_hash l_hash;
      l_hash.init (l_transys, (int) ITER_HASH_SIZE, 4, 16, g_adv_edesc);
      lf_tran_entry *l_tran = lf_tran_request_entry (&l_transys);

      for (unsigned int i = 0; i < ITER_ENTRY_COUNT; i++)
	{
	  adv_key k = { i, i };
	  adv_entry *e = l_hash.freelist_claim (l_tran);
	  test_common::custom_assert (e != NULL);
	  e->m_key = k;
	  e->m_key_copy = k;
	  e->m_magic = ADV_MAGIC_LIVE;
	  test_common::custom_assert (l_hash.insert_given (l_tran, k, e));
	  l_hash.unlock (l_tran, e);
	}

      {
	adv_lf_hash::iterator iter { l_tran, l_hash };
	iter.restart ();
	(void) check_shape ("lf_hash_table", "restart before first iterate",
			    full_pass_old (iter), ITER_ENTRY_COUNT, false);
      }
      {
	adv_lf_hash::iterator iter { l_tran, l_hash };
	(void) full_pass_old (iter);
	iter.restart ();
	(void) check_shape ("lf_hash_table", "restart after a complete pass",
			    full_pass_old (iter), ITER_ENTRY_COUNT, false);
      }
      {
	adv_lf_hash::iterator iter { l_tran, l_hash };
	(void) full_pass_old (iter);
	iter.restart ();
	iter.restart ();
	(void) check_shape ("lf_hash_table", "two restarts in a row",
			    full_pass_old (iter), ITER_ENTRY_COUNT, false);
      }
      {
	adv_lf_hash::iterator iter { l_tran, l_hash };
	size_t walked = 0;
	while (walked < ITER_ENTRY_COUNT)
	  {
	    test_common::custom_assert (iter.iterate () != NULL);
	    ++walked;
	  }
	l_hash.end_tran (l_tran);
	iter.restart ();
	(void) check_shape ("lf_hash_table", "restart from the last entry",
			    full_pass_old (iter), ITER_ENTRY_COUNT, false);
      }
      {
	adv_lf_hash::iterator iter { l_tran, l_hash };
	for (size_t i = 0; i < 3; i++)
	  {
	    test_common::custom_assert (iter.iterate () != NULL);
	  }
	l_hash.end_tran (l_tran);
	iter.restart ();
	(void) check_shape ("lf_hash_table", "restart after three entries",
			    full_pass_old (iter), ITER_ENTRY_COUNT, false);
      }

      lf_tran_return_entry (l_tran);
      l_hash.destroy ();
      lf_tran_system_destroy (&l_transys);
    }

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: an iterator shape did not see the whole map\n");
      }
    return err;
  }

  //
  // case_iterate_under_churn () - epoch protection has to hold for the iterator too.
  //
  // testcase_held_entry_consistency () holds an entry from find (); the iterator keeps one transaction per
  // bucket and walks the chain inside it, so it depends most directly on reclaim_retired_list () handing out
  // only a prefix older than the minimum active id, and on this branch's batched reclaim_run (). Entries are
  // stamped - key, second copy, magic - before they are reachable and never touched again, so anything the
  // iterator reaches must show ADV_MAGIC_LIVE and two keys that agree.
  //
  static std::atomic<std::uint64_t> g_iter_seen { 0 };
  static std::atomic<std::uint64_t> g_iter_inconsistent { 0 };
  static std::atomic<bool> g_churn_done { false };

  static const size_t CHURN_HASH_SIZE = 32;
  static const unsigned int CHURN_KEYSPACE = 400;
  static const size_t CHURN_OPS = 200000;
  static const size_t CHURN_CLEAR_EVERY = 200;
  // the check has to keep the entry for a while, not glance at it: a reclaim landing in the few
  // instructions between iterate () returning and one read of the magic is not worth waiting for.
  static const size_t CHURN_HOLD_SPINS = 4000;
  static const size_t CHURN_WORKERS = 6;
  static const size_t CHURN_ITERATORS = 2;

  static bool
  hold_and_check (adv_entry *e)
  {
    // volatile, so the loop really re-reads the entry for the whole hold
    const volatile unsigned int *magicp = &e->m_magic;
    const volatile unsigned int *k1p = &e->m_key.m_1;
    const volatile unsigned int *k2p = &e->m_key.m_2;
    const volatile unsigned int *c1p = &e->m_key_copy.m_1;
    const volatile unsigned int *c2p = &e->m_key_copy.m_2;
    const unsigned int first_1 = *k1p;
    const unsigned int first_2 = *k2p;

    for (size_t s = 0; s < CHURN_HOLD_SPINS; s++)
      {
	const unsigned int magic = *magicp;
	const unsigned int k1 = *k1p;
	const unsigned int k2 = *k2p;
	const unsigned int c1 = *c1p;
	const unsigned int c2 = *c2p;
	if (magic != ADV_MAGIC_LIVE || k1 != c1 || k2 != c2 || k1 != first_1 || k2 != first_2)
	  {
	    return false;
	  }
      }
    return true;
  }

  static void
  churn_worker_new (adv_hashmap *hash, tran::index index, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < CHURN_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % CHURN_KEYSPACE;
	adv_key k = { kv, kv };
	adv_entry *e = hash->freelist_claim (index);
	if (e == NULL)
	  {
	    continue;
	  }
	e->m_key = k;
	e->m_key_copy = k;
	e->m_magic = ADV_MAGIC_LIVE;
	(void) hash->insert_given (index, k, e);
	hash->unlock (index, e);

	state = next_rand (state);
	const unsigned int dv = state % CHURN_KEYSPACE;
	adv_key dk = { dv, dv };
	(void) hash->erase (index, dk);

	if ((i % CHURN_CLEAR_EVERY) == CHURN_CLEAR_EVERY - 1)
	  {
	    hash->clear (index);
	  }
      }
  }

  static void
  iterate_worker_new (adv_hashmap *hash, tran::index index)
  {
    while (!g_churn_done)
      {
	adv_hashmap::iterator iter { index, *hash };
	for (adv_entry *e = iter.iterate (); e != NULL; e = iter.iterate ())
	  {
	    ++g_iter_seen;
	    if (!hold_and_check (e))
	      {
		++g_iter_inconsistent;
	      }
	  }
      }
  }

  static void
  churn_worker_old (adv_lf_hash *hash, lf_tran_entry *tran, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < CHURN_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % CHURN_KEYSPACE;
	adv_key k = { kv, kv };
	adv_entry *e = hash->freelist_claim (tran);
	if (e == NULL)
	  {
	    continue;
	  }
	e->m_key = k;
	e->m_key_copy = k;
	e->m_magic = ADV_MAGIC_LIVE;
	(void) hash->insert_given (tran, k, e);
	hash->unlock (tran, e);

	state = next_rand (state);
	const unsigned int dv = state % CHURN_KEYSPACE;
	adv_key dk = { dv, dv };
	(void) hash->erase (tran, dk);

	if ((i % CHURN_CLEAR_EVERY) == CHURN_CLEAR_EVERY - 1)
	  {
	    hash->clear (tran);
	  }
      }
  }

  static void
  iterate_worker_old (adv_lf_hash *hash, lf_tran_entry *tran)
  {
    while (!g_churn_done)
      {
	adv_lf_hash::iterator iter { tran, *hash };
	for (adv_entry *e = iter.iterate (); e != NULL; e = iter.iterate ())
	  {
	    ++g_iter_seen;
	    if (!hold_and_check (e))
	      {
		++g_iter_inconsistent;
	      }
	  }
      }
  }

  static int
  case_iterate_under_churn ()
  {
    const size_t THREADS = CHURN_WORKERS + CHURN_ITERATORS;

    test_common::sync_cout ("case_iterate_under_churn\n");
    int err = 0;

    // the rewrite
    {
      g_iter_seen = 0;
      g_iter_inconsistent = 0;
      g_churn_done = false;

      tran::system l_transys { THREADS };
      std::vector<tran::index> l_indexes;
      for (size_t i = 0; i < THREADS; i++)
	{
	  l_indexes.push_back (l_transys.assign_index ());
	}
      adv_hashmap l_hash;
      l_hash.init (l_transys, CHURN_HASH_SIZE, 64, 4, g_adv_edesc);

      std::vector<std::thread> l_threads;
      for (size_t i = 0; i < CHURN_ITERATORS; i++)
	{
	  l_threads.emplace_back (iterate_worker_new, &l_hash, l_indexes[i]);
	}
      for (size_t i = 0; i < CHURN_WORKERS; i++)
	{
	  l_threads.emplace_back (churn_worker_new, &l_hash, l_indexes[CHURN_ITERATORS + i],
				  0x9e3779b9u * (unsigned int) (i + 1));
	}
      for (size_t i = CHURN_ITERATORS; i < THREADS; i++)
	{
	  l_threads[i].join ();
	}
      g_churn_done = true;
      for (size_t i = 0; i < CHURN_ITERATORS; i++)
	{
	  l_threads[i].join ();
	}

      string_buffer line;
      line ("  lockfree::hashmap : iterator saw %llu entries, %llu inconsistent\n",
	    (unsigned long long) g_iter_seen.load (), (unsigned long long) g_iter_inconsistent.load ());
      say (line);
      if (g_iter_inconsistent != 0)
	{
	  err = 1;
	}

      l_hash.destroy ();
      for (size_t i = 0; i < THREADS; i++)
	{
	  l_transys.free_index (l_indexes[i]);
	}
    }

    // the implementation it replaces, same load
    {
      g_iter_seen = 0;
      g_iter_inconsistent = 0;
      g_churn_done = false;

      lf_tran_system l_transys;
      lf_tran_system_init (&l_transys, (int) THREADS);
      std::vector<lf_tran_entry *> l_trans;
      for (size_t i = 0; i < THREADS; i++)
	{
	  l_trans.push_back (lf_tran_request_entry (&l_transys));
	}
      adv_lf_hash l_hash;
      l_hash.init (l_transys, (int) CHURN_HASH_SIZE, 4, 64, g_adv_edesc);

      std::vector<std::thread> l_threads;
      for (size_t i = 0; i < CHURN_ITERATORS; i++)
	{
	  l_threads.emplace_back (iterate_worker_old, &l_hash, l_trans[i]);
	}
      for (size_t i = 0; i < CHURN_WORKERS; i++)
	{
	  l_threads.emplace_back (churn_worker_old, &l_hash, l_trans[CHURN_ITERATORS + i],
				  0x9e3779b9u * (unsigned int) (i + 1));
	}
      for (size_t i = CHURN_ITERATORS; i < THREADS; i++)
	{
	  l_threads[i].join ();
	}
      g_churn_done = true;
      for (size_t i = 0; i < CHURN_ITERATORS; i++)
	{
	  l_threads[i].join ();
	}

      string_buffer line;
      line ("  lf_hash_table     : iterator saw %llu entries, %llu inconsistent\n",
	    (unsigned long long) g_iter_seen.load (), (unsigned long long) g_iter_inconsistent.load ());
      say (line);

      l_hash.destroy ();
      for (size_t i = 0; i < THREADS; i++)
	{
	  lf_tran_return_entry (l_trans[i]);
	}
      lf_tran_system_destroy (&l_transys);
    }

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: an entry changed identity under an iterator that was standing on it\n");
      }
    return err;
  }

  //
  // case_tiny_block_pressure () - the back-buffer head/tail protocol with a block of one, at 64 threads.
  //
  // A block of one is the newest shape in the freelist - the constructor asserted block_size > 1 until two
  // commits ago - and the worst case for the protocol this branch last touched: every claim empties the
  // available list, so swap_backbuffer (), alloc_backbuffer () and force_alloc_block () all race. Checked at
  // quiescence: available + back-buffer + retired == alloc, the back-buffer holds exactly one block, and
  // nothing is still claimed. claim () also asserts m_available_count > 0, so a credit lost by the swap aborts
  // here rather than wrapping.
  //
  struct adv_item
  {
    // stamped by whoever holds the node, so the same node handed to two threads is caught
    unsigned int m_holder;

    adv_item ()
      : m_holder (0)
    {
    }

    void on_reclaim () {}
  };

  static std::atomic<std::uint64_t> g_double_handout { 0 };

  static const size_t TINY_THREADS = 64;
  static const size_t TINY_OPS = 3000;
  static const size_t TINY_HOLD = 4;

  static void
  tiny_worker (freelist<adv_item> *lffl, unsigned int who)
  {
    tran::index index = lffl->get_transaction_system ().assign_index ();
    std::vector<freelist<adv_item>::free_node *> held;

    for (size_t i = 0; i < TINY_OPS; i++)
      {
	freelist<adv_item>::free_node *node = lffl->claim (index);
	test_common::custom_assert (node != NULL);
	// claim () leaves the transaction started on purpose
	lffl->get_transaction_table ().end_tran (index);

	if (node->get_data ().m_holder != 0)
	  {
	    ++g_double_handout;
	  }
	node->get_data ().m_holder = who;
	held.push_back (node);

	if (held.size () >= TINY_HOLD)
	  {
	    for (auto &it : held)
	      {
		it->get_data ().m_holder = 0;
		lffl->retire (index, *it);
	      }
	    held.clear ();
	  }
      }

    for (auto &it : held)
      {
	it->get_data ().m_holder = 0;
	lffl->retire (index, *it);
      }
    lffl->get_transaction_system ().free_index (index);
  }


  static void
  tiny_worker_old (lf_freelist *lffl, lf_tran_system *sys, unsigned int who)
  {
    lf_tran_entry *tran = lf_tran_request_entry (sys);
    std::vector<void *> held;

    for (size_t i = 0; i < TINY_OPS; i++)
      {
	void *node = lf_freelist_claim (tran, lffl);
	test_common::custom_assert (node != NULL);
	adv_entry *e = (adv_entry *) node;
	if (e->m_key_copy.m_1 != 0)
	  {
	    ++g_double_handout;
	  }
	e->m_key_copy.m_1 = who;
	held.push_back (node);

	if (held.size () >= TINY_HOLD)
	  {
	    for (auto &it : held)
	      {
		((adv_entry *) it)->m_key_copy.m_1 = 0;
		(void) lf_freelist_retire (tran, lffl, it);
	      }
	    held.clear ();
	  }
      }

    for (auto &it : held)
      {
	((adv_entry *) it)->m_key_copy.m_1 = 0;
	(void) lf_freelist_retire (tran, lffl, it);
      }
    lf_tran_return_entry (tran);
  }

  static void
  run_tiny_old (size_t block_size)
  {
    g_double_handout = 0;

    lf_tran_system l_transys;
    lf_tran_system_init (&l_transys, (int) TINY_THREADS + 1);
    lf_freelist l_freelist = LF_FREELIST_INITIALIZER;
    test_common::custom_assert (lf_freelist_init (&l_freelist, 2, (int) block_size, &g_adv_edesc, &l_transys)
				== NO_ERROR);

    std::vector<std::thread> l_threads;
    for (size_t i = 0; i < TINY_THREADS; i++)
      {
	l_threads.emplace_back (tiny_worker_old, &l_freelist, &l_transys, (unsigned int) (i + 1));
      }
    for (auto &it : l_threads)
      {
	it.join ();
      }

    string_buffer line;
    line ("  lf_freelist         block size %zu: alloc = %d, available = %d, retired = %d, double handouts = %llu\n",
	  block_size, l_freelist.alloc_cnt, l_freelist.available_cnt, l_freelist.retired_cnt,
	  (unsigned long long) g_double_handout.load ());
    say (line);

    lf_freelist_destroy (&l_freelist);
    lf_tran_system_destroy (&l_transys);
  }

  static int
  case_tiny_block_pressure ()
  {
    const size_t BLOCK_SIZES[] = { 1, 2 };

    test_common::sync_cout ("case_tiny_block_pressure\n");
    int err = 0;

    for (size_t block_size : BLOCK_SIZES)
      {
	g_double_handout = 0;

	tran::system l_transys { TINY_THREADS + 1 };
	freelist<adv_item> l_freelist { l_transys, block_size, 2 };

	std::vector<std::thread> l_threads;
	for (size_t i = 0; i < TINY_THREADS; i++)
	  {
	    l_threads.emplace_back (tiny_worker, &l_freelist, (unsigned int) (i + 1));
	  }
	for (auto &it : l_threads)
	  {
	    it.join ();
	  }

	const size_t alloc = l_freelist.get_alloc_count ();
	const size_t available = l_freelist.get_available_count ();
	const size_t backbuffer = l_freelist.get_backbuffer_count ();
	const size_t retired = l_freelist.get_transaction_table ().get_current_retire_count ();
	const size_t claimed = l_freelist.get_claimed_count ();
	const size_t forced = l_freelist.get_forced_allocation_count ();

	string_buffer line;
	line ("  lockfree::freelist  block size %zu: alloc = %zu, available = %zu, backbuffer = %zu, retired = %zu,"
	      " claimed = %zu, forced blocks = %zu, double handouts = %llu\n", block_size, alloc, available,
	      backbuffer, retired, claimed, forced, (unsigned long long) g_double_handout.load ());
	say (line);

	if (available + backbuffer + retired != alloc)
	  {
	    test_common::sync_cout ("  FAILED: available + backbuffer + retired does not add up to alloc\n");
	    err = 1;
	  }
	if (backbuffer != block_size)
	  {
	    test_common::sync_cout ("  FAILED: the back-buffer does not hold exactly one block\n");
	    err = 1;
	  }
	if (claimed != 0)
	  {
	    test_common::sync_cout ("  FAILED: nodes are still counted as claimed after every one was retired\n");
	    err = 1;
	  }
	if (g_double_handout != 0)
	  {
	    test_common::sync_cout ("  FAILED: a node was handed to two threads at once\n");
	    err = 1;
	  }

	// the same shape on the implementation it replaces, for the retention numbers above
	run_tiny_old (block_size);
      }
    return err;
  }
} // namespace test_lockfree
