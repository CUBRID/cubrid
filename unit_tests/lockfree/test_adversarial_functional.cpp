/*
 *
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
  static int case_mutex_path_identity ();
  static int case_alloc_cap_under_load ();
  static int case_shared_transys ();
  static int case_index_churn ();
  static int case_iterator_completeness ();
  static int case_erase_locked_liveness ();
  static int case_insert_given_promote ();

  struct adv_case
  {
    const char *m_name;
    int (*m_fn) ();
  };

  // LFADV_ONLY=<substring> runs only the cases whose name contains it. Calibrating a case against a fault
  // injection needs exactly that: an injected fault that corrupts a chain hangs whichever case reaches it
  // first, and without a filter every case after that one reports nothing.
  static bool
  adv_case_is_enabled (const char *name)
  {
    static const char *only = getenv ("LFADV_ONLY");
    return only == NULL || strstr (name, only) != NULL;
  }

  int
  test_adversarial_functional ()
  {
    // last on purpose: case_erase_locked_liveness () may have to abandon two wedged threads rather than join them
    static const adv_case CASES[] =
    {
      { "f_init_error_dropped", case_f_init_error_dropped },
      { "oom_freelist_state", case_oom_freelist_state },
      { "max_alloc_cap", case_max_alloc_cap },
      { "iterator_edges", case_iterator_edges },
      { "iterate_under_churn", case_iterate_under_churn },
      { "tiny_block_pressure", case_tiny_block_pressure },
      { "mutex_path_identity", case_mutex_path_identity },
      { "alloc_cap_under_load", case_alloc_cap_under_load },
      { "shared_transys", case_shared_transys },
      { "index_churn", case_index_churn },
      { "iterator_completeness", case_iterator_completeness },
      { "insert_given_promote", case_insert_given_promote },
      { "erase_locked_liveness", case_erase_locked_liveness }
    };

    test_common::sync_cout ("start test_adversarial_functional\n");

    int err = 0;
    for (const adv_case &c : CASES)
      {
	if (adv_case_is_enabled (c.m_name))
	  {
	    err = err | c.m_fn ();
	  }
      }

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

  //
  // shared held-entry identity check
  //
  // The property epoch reclamation exists to provide: an entry a thread holds - by mutex or by transaction - must
  // still be the entry it asked for until it lets go. Every entry carries a second copy of its key and a
  // life-cycle magic that f_init/f_uninit and the inserter move through FREE -> CLAIMED -> LIVE, so a holder can
  // check three things that agree only while the entry is really its own. Recorded on failure only, so the happy
  // path costs five loads.
  //
  enum adv_site
  {
    ADV_S_FIND = 0,
    ADV_S_FIND_HELD,
    ADV_S_FOI,
    ADV_S_FOI_HELD,
    ADV_S_INSERT_GIVEN,
    ADV_S_ITERATE,
    ADV_S_ITERATE_HELD
  };
  static const char *ADV_SITE_NAME[] =
  {
    "find", "find/held", "find_or_insert", "find_or_insert/held", "insert_given", "iterate", "iterate/held"
  };

  struct adv_witness
  {
    int m_site;
    unsigned int m_magic;
    int m_init_answer;
    adv_key m_wanted;
    adv_key m_key;
    adv_key m_copy;
  };

  static const size_t ADV_WITNESS_MAX = 6;
  static adv_witness g_adv_witness[ADV_WITNESS_MAX];
  static std::atomic<size_t> g_adv_witness_count { 0 };
  static std::atomic<std::uint64_t> g_adv_checks { 0 };
  static std::atomic<std::uint64_t> g_adv_violations { 0 };

  static void
  adv_reset_checks ()
  {
    g_adv_witness_count = 0;
    g_adv_checks = 0;
    g_adv_violations = 0;
  }

  static bool
  adv_check_entry (adv_entry *e, const adv_key &wanted, int site)
  {
    // volatile, so the compiler cannot hoist these out of a hold loop
    const volatile unsigned int *magicp = &e->m_magic;
    const volatile unsigned int *k1p = &e->m_key.m_1;
    const volatile unsigned int *k2p = &e->m_key.m_2;
    const volatile unsigned int *c1p = &e->m_key_copy.m_1;
    const volatile unsigned int *c2p = &e->m_key_copy.m_2;

    const unsigned int magic = *magicp;
    const unsigned int k1 = *k1p;
    const unsigned int k2 = *k2p;
    const unsigned int c1 = *c1p;
    const unsigned int c2 = *c2p;

    ++g_adv_checks;
    if (magic == ADV_MAGIC_LIVE && k1 == wanted.m_1 && k2 == wanted.m_2 && c1 == k1 && c2 == k2)
      {
	return true;
      }

    ++g_adv_violations;
    const size_t slot = g_adv_witness_count++;
    if (slot < ADV_WITNESS_MAX)
      {
	g_adv_witness[slot] = { site, magic, e->m_init_answer, wanted, { k1, k2 }, { c1, c2 } };
      }
    return false;
  }

  static void
  adv_spin (size_t spins)
  {
    for (volatile size_t s = 0; s < spins; ++s)
      ;
  }

  static void
  adv_report_checks (const char *impl)
  {
    string_buffer line;
    line ("  %-17s %llu identity checks, %llu violations\n", impl, (unsigned long long) g_adv_checks.load (),
	  (unsigned long long) g_adv_violations.load ());
    say (line);

    const size_t shown = std::min (g_adv_witness_count.load (), ADV_WITNESS_MAX);
    for (size_t i = 0; i < shown; i++)
      {
	const adv_witness &w = g_adv_witness[i];
	string_buffer wl;
	wl ("    at %-19s magic = 0x%08x, f_init said %d, wanted {%u,%u}, entry held {%u,%u}, copy {%u,%u}\n",
	    ADV_SITE_NAME[w.m_site], w.m_magic, w.m_init_answer, w.m_wanted.m_1, w.m_wanted.m_2, w.m_key.m_1,
	    w.m_key.m_2, w.m_copy.m_1, w.m_copy.m_2);
	say (wl);
      }
  }

  //
  // a private entry descriptor per case, so a case that moves using_mutex or max_alloc_cnt cannot disturb another
  //
  static void
  adv_edesc_init (lf_entry_descriptor &edesc, bool using_mutex, int max_alloc_cnt)
  {
    edesc = g_adv_edesc;
    edesc.using_mutex = using_mutex ? LF_EM_USING_MUTEX : LF_EM_NOT_USING_MUTEX;
    edesc.max_alloc_cnt = max_alloc_cnt;
  }

  //
  // case_mutex_path_identity () - the mutex-on path, attacked on purpose.
  //
  // Every held-entry failure found on this branch so far was mutex = 0. The mutex path re-checks the delete mark
  // after locking and restarts, which is why it looked immune, and testcase_held_entry_consistency () does run
  // with mutex on - but only over find (), insert_given (), erase () and clear (). find_or_insert () and
  // erase_locked () are the two entry points that exist *only* because there is a mutex, and no held-entry check
  // has ever been pointed at them.
  //
  // Geometry is degenerate on purpose: one or eight buckets and a key space of sixteen, so the chain is as long
  // and as contended as it can be. Plain erase () is deliberately absent - see case_erase_locked_liveness ().
  //
  static const size_t MPI_THREADS = 64;
  static const size_t MPI_OPS = 1500;
  static const unsigned int MPI_KEYSPACE = 16;
  static const size_t MPI_HOLD_SPINS = 4000;
  static const size_t MPI_CLEAR_EVERY = 300;

  static void
  mutex_path_worker_new (adv_hashmap *hash, tran::index index, unsigned int seed, bool clears)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < MPI_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % MPI_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 4)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (index, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (MPI_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (index, e);
	      }
	    break;
	  }
	  case 1:
	  case 2:
	  {
	    adv_entry *e = NULL;
	    const bool inserted = hash->find_or_insert (index, k, e);
	    test_common::custom_assert (e != NULL);
	    if (inserted)
	      {
		// the mutex is held, so nobody can see this entry before it is stamped
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
	      }
	    bool ok = adv_check_entry (e, k, ADV_S_FOI);
	    adv_spin (MPI_HOLD_SPINS);
	    ok = adv_check_entry (e, k, ADV_S_FOI_HELD) && ok;
	    hash->unlock (index, e);
	    break;
	  }
	  default:
	  {
	    adv_entry *e = NULL;
	    const bool inserted = hash->find_or_insert (index, k, e);
	    test_common::custom_assert (e != NULL);
	    if (inserted)
	      {
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
	      }
	    (void) adv_check_entry (e, k, ADV_S_FOI);
	    if (hash->erase_locked (index, k, e))
	      {
		// erase_locked () released the mutex and cleared the handle
		test_common::custom_assert (e == NULL);
	      }
	    else
	      {
		test_common::custom_assert (e != NULL);
		hash->unlock (index, e);
	      }
	    break;
	  }
	  }

	if (clears && (i % MPI_CLEAR_EVERY) == MPI_CLEAR_EVERY - 1)
	  {
	    hash->clear (index);
	  }
      }
  }

  static void
  mutex_path_worker_old (adv_lf_hash *hash, lf_tran_entry *tran, unsigned int seed, bool clears)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < MPI_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % MPI_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 4)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (tran, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (MPI_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (tran, e);
	      }
	    break;
	  }
	  case 1:
	  case 2:
	  {
	    adv_entry *e = NULL;
	    const bool inserted = hash->find_or_insert (tran, k, e);
	    test_common::custom_assert (e != NULL);
	    if (inserted)
	      {
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
	      }
	    bool ok = adv_check_entry (e, k, ADV_S_FOI);
	    adv_spin (MPI_HOLD_SPINS);
	    ok = adv_check_entry (e, k, ADV_S_FOI_HELD) && ok;
	    hash->unlock (tran, e);
	    break;
	  }
	  default:
	  {
	    adv_entry *e = NULL;
	    const bool inserted = hash->find_or_insert (tran, k, e);
	    test_common::custom_assert (e != NULL);
	    if (inserted)
	      {
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
	      }
	    (void) adv_check_entry (e, k, ADV_S_FOI);
	    if (hash->erase_locked (tran, k, e))
	      {
		test_common::custom_assert (e == NULL);
	      }
	    else
	      {
		test_common::custom_assert (e != NULL);
		hash->unlock (tran, e);
	      }
	    break;
	  }
	  }

	if (clears && (i % MPI_CLEAR_EVERY) == MPI_CLEAR_EVERY - 1)
	  {
	    hash->clear (tran);
	  }
      }
  }

  static int
  case_mutex_path_identity ()
  {
    const size_t HASH_SIZES[] = { 1, 8 };

    test_common::sync_cout ("case_mutex_path_identity\n");
    int err = 0;

    for (size_t hash_size : HASH_SIZES)
      {
	{
	  string_buffer head;
	  head ("  hash size %zu, %zu threads, key space %u, mutex on\n", hash_size, MPI_THREADS, MPI_KEYSPACE);
	  say (head);
	}

	// the rewrite
	{
	  adv_reset_checks ();
	  lf_entry_descriptor l_edesc;
	  adv_edesc_init (l_edesc, true, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

	  tran::system l_transys { MPI_THREADS + 1 };
	  std::vector<tran::index> l_indexes;
	  for (size_t i = 0; i < MPI_THREADS; i++)
	    {
	      l_indexes.push_back (l_transys.assign_index ());
	    }
	  adv_hashmap l_hash;
	  test_common::custom_assert (l_hash.init (l_transys, hash_size, 16, 2, l_edesc) == NO_ERROR);

	  std::vector<std::thread> l_threads;
	  for (size_t i = 0; i < MPI_THREADS; i++)
	    {
	      l_threads.emplace_back (mutex_path_worker_new, &l_hash, l_indexes[i],
				      0x9e3779b9u * (unsigned int) (i + 1), i == 0);
	    }
	  for (auto &it : l_threads)
	    {
	      it.join ();
	    }

	  adv_report_checks ("lockfree::hashmap");
	  if (g_adv_violations != 0)
	    {
	      err = 1;
	    }

	  l_hash.destroy ();
	  for (size_t i = 0; i < MPI_THREADS; i++)
	    {
	      l_transys.free_index (l_indexes[i]);
	    }
	}

	// the implementation it replaces, same load
	{
	  adv_reset_checks ();
	  lf_entry_descriptor l_edesc;
	  adv_edesc_init (l_edesc, true, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

	  lf_tran_system l_transys;
	  lf_tran_system_init (&l_transys, (int) MPI_THREADS + 1);
	  std::vector<lf_tran_entry *> l_trans;
	  for (size_t i = 0; i < MPI_THREADS; i++)
	    {
	      l_trans.push_back (lf_tran_request_entry (&l_transys));
	    }
	  adv_lf_hash l_hash;
	  test_common::custom_assert (l_hash.init (l_transys, (int) hash_size, 2, 16, l_edesc) == NO_ERROR);

	  std::vector<std::thread> l_threads;
	  for (size_t i = 0; i < MPI_THREADS; i++)
	    {
	      l_threads.emplace_back (mutex_path_worker_old, &l_hash, l_trans[i],
				      0x9e3779b9u * (unsigned int) (i + 1), i == 0);
	    }
	  for (auto &it : l_threads)
	    {
	      it.join ();
	    }

	  adv_report_checks ("lf_hash_table");

	  l_hash.destroy ();
	  for (size_t i = 0; i < MPI_THREADS; i++)
	    {
	      lf_tran_return_entry (l_trans[i]);
	    }
	  lf_tran_system_destroy (&l_transys);
	}
      }

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: an entry held on the mutex path was not the entry that was asked for\n");
      }
    return err;
  }

  //
  // case_alloc_cap_under_load () - the allocation cap engaged while entries are held.
  //
  // Above edesc.max_alloc_cnt, free_node::reclaim () and reclaim_run () do delete rather than recycle
  // (lockfree_freelist.hpp:599, :630), and lf_freelist_transport () calls f_free (lock_free.c). That turns any
  // epoch hole from "the entry was wiped" into a real use-after-free, and the object lock resource table takes
  // that cap from PRM_ID_LK_ESCALATION_AT in production. Every case so far ran uncapped, so this path has never
  // carried load.
  //
  // insert_given () in both mutex modes: the entry is stamped before it is reachable, so a holder may assume all
  // three fields agree. A crash here is the finding, not a test bug.
  //
  static const size_t CAPL_THREADS = 64;
  static const size_t CAPL_OPS = 2000;
  static const unsigned int CAPL_KEYSPACE = 32;
  static const size_t CAPL_HOLD_SPINS = 4000;
  static const int CAPL_MAX_ALLOC = 96;
  static const size_t CAPL_CLEAR_EVERY = 400;

  static void
  cap_worker_new (adv_hashmap *hash, tran::index index, unsigned int seed, bool clears)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < CAPL_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % CAPL_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (index, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (CAPL_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (index, e);
	      }
	    break;
	  }
	  case 1:
	  {
	    adv_entry *e = hash->freelist_claim (index);
	    if (e == NULL)
	      {
		break;
	      }
	    e->m_key = k;
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	    (void) hash->insert_given (index, k, e);
	    test_common::custom_assert (e != NULL);
	    (void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
	    hash->unlock (index, e);
	    break;
	  }
	  default:
	    (void) hash->erase (index, k);
	    break;
	  }

	if (clears && (i % CAPL_CLEAR_EVERY) == CAPL_CLEAR_EVERY - 1)
	  {
	    hash->clear (index);
	  }
      }
  }

  static void
  cap_worker_old (adv_lf_hash *hash, lf_tran_entry *tran, unsigned int seed, bool clears)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < CAPL_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % CAPL_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (tran, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (CAPL_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (tran, e);
	      }
	    break;
	  }
	  case 1:
	  {
	    adv_entry *e = hash->freelist_claim (tran);
	    if (e == NULL)
	      {
		break;
	      }
	    e->m_key = k;
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	    (void) hash->insert_given (tran, k, e);
	    test_common::custom_assert (e != NULL);
	    (void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
	    hash->unlock (tran, e);
	    break;
	  }
	  default:
	    (void) hash->erase (tran, k);
	    break;
	  }

	if (clears && (i % CAPL_CLEAR_EVERY) == CAPL_CLEAR_EVERY - 1)
	  {
	    hash->clear (tran);
	  }
      }
  }

  static int
  case_alloc_cap_under_load ()
  {
    const bool MUTEX_MODES[] = { false, true };

    test_common::sync_cout ("case_alloc_cap_under_load\n");
    int err = 0;

    for (bool using_mutex : MUTEX_MODES)
      {
	{
	  string_buffer head;
	  head ("  max_alloc_cnt %d, %zu threads, key space %u, mutex %s\n", CAPL_MAX_ALLOC, CAPL_THREADS,
		CAPL_KEYSPACE, using_mutex ? "on" : "off");
	  say (head);
	}

	// the rewrite
	{
	  adv_reset_checks ();
	  lf_entry_descriptor l_edesc;
	  adv_edesc_init (l_edesc, using_mutex, CAPL_MAX_ALLOC);

	  tran::system l_transys { CAPL_THREADS + 1 };
	  std::vector<tran::index> l_indexes;
	  for (size_t i = 0; i < CAPL_THREADS; i++)
	    {
	      l_indexes.push_back (l_transys.assign_index ());
	    }
	  adv_hashmap l_hash;
	  test_common::custom_assert (l_hash.init (l_transys, 8, 8, 2, l_edesc) == NO_ERROR);

	  std::vector<std::thread> l_threads;
	  for (size_t i = 0; i < CAPL_THREADS; i++)
	    {
	      l_threads.emplace_back (cap_worker_new, &l_hash, l_indexes[i], 0x85ebca6bu * (unsigned int) (i + 1),
				      i == 0);
	    }
	  for (auto &it : l_threads)
	    {
	      it.join ();
	    }

	  {
	    string_buffer line;
	    line ("  lockfree::hashmap alloc = %zu (cap %d), live = %zu\n", l_hash.get_alloc_element_count (),
		  CAPL_MAX_ALLOC, l_hash.get_element_count ());
	    say (line);
	  }
	  adv_report_checks ("lockfree::hashmap");
	  if (g_adv_violations != 0)
	    {
	      err = 1;
	    }

	  l_hash.destroy ();
	  for (size_t i = 0; i < CAPL_THREADS; i++)
	    {
	      l_transys.free_index (l_indexes[i]);
	    }
	}

	// the implementation it replaces, same load and same cap
	{
	  adv_reset_checks ();
	  lf_entry_descriptor l_edesc;
	  adv_edesc_init (l_edesc, using_mutex, CAPL_MAX_ALLOC);

	  lf_tran_system l_transys;
	  lf_tran_system_init (&l_transys, (int) CAPL_THREADS + 1);
	  std::vector<lf_tran_entry *> l_trans;
	  for (size_t i = 0; i < CAPL_THREADS; i++)
	    {
	      l_trans.push_back (lf_tran_request_entry (&l_transys));
	    }
	  adv_lf_hash l_hash;
	  test_common::custom_assert (l_hash.init (l_transys, 8, 2, 8, l_edesc) == NO_ERROR);

	  std::vector<std::thread> l_threads;
	  for (size_t i = 0; i < CAPL_THREADS; i++)
	    {
	      l_threads.emplace_back (cap_worker_old, &l_hash, l_trans[i], 0x85ebca6bu * (unsigned int) (i + 1),
				      i == 0);
	    }
	  for (auto &it : l_threads)
	    {
	      it.join ();
	    }

	  {
	    string_buffer line;
	    line ("  lf_hash_table     alloc = %zu (cap %d), live = %zu\n", l_hash.get_alloc_element_count (),
		  CAPL_MAX_ALLOC, l_hash.get_element_count ());
	    say (line);
	  }
	  adv_report_checks ("lf_hash_table");

	  l_hash.destroy ();
	  for (size_t i = 0; i < CAPL_THREADS; i++)
	    {
	      lf_tran_return_entry (l_trans[i]);
	    }
	  lf_tran_system_destroy (&l_transys);
	}
      }

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: a held entry changed identity with the allocation cap engaged\n");
      }
    return err;
  }

  //
  // case_shared_transys () - several hash maps on one transaction system.
  //
  // This is the shape production takes on this branch and nowhere else: thread_manager.cpp:119 builds one
  // lockfree::tran::system and every hash table in the server shares it, where the legacy path gives each table
  // its own lf_tran_system (lock_free.c:467-501). The soundness argument for the batched reclaim added here -
  // reclaim_run () splicing a whole run onto one available list - rests on each freelist building its own
  // tran::table, so nothing else can retire into its descriptors (lockfree_freelist.hpp:611). Four live maps on
  // one system is the smallest load that can contradict that.
  //
  // The legacy side is run in *its* production shape, one transaction system per table: giving it one shared
  // system would put nodes of four different freelists on one retired list, which is a legacy hazard the server
  // never produces and not what this case is asking about.
  //
  static const size_t SHT_MAPS = 4;
  static const size_t SHT_THREADS = 32;
  static const size_t SHT_OPS = 3000;
  static const unsigned int SHT_KEYSPACE = 64;
  static const size_t SHT_HOLD_SPINS = 2000;

  static void
  shared_worker_new (std::vector<adv_hashmap *> *maps, tran::index index, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < SHT_OPS; i++)
      {
	state = next_rand (state);
	adv_hashmap *hash = (*maps)[state % SHT_MAPS];
	state = next_rand (state);
	const unsigned int kv = state % SHT_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (index, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (SHT_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (index, e);
	      }
	    break;
	  }
	  case 1:
	  {
	    adv_entry *e = hash->freelist_claim (index);
	    if (e == NULL)
	      {
		break;
	      }
	    e->m_key = k;
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	    (void) hash->insert_given (index, k, e);
	    (void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
	    hash->unlock (index, e);
	    break;
	  }
	  default:
	    (void) hash->erase (index, k);
	    break;
	  }
      }
  }

  static void
  shared_worker_old (std::vector<adv_lf_hash *> *maps, std::vector<lf_tran_entry *> *trans, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < SHT_OPS; i++)
      {
	state = next_rand (state);
	const size_t which = state % SHT_MAPS;
	adv_lf_hash *hash = (*maps)[which];
	lf_tran_entry *tran = (*trans)[which];
	state = next_rand (state);
	const unsigned int kv = state % SHT_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (tran, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (SHT_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (tran, e);
	      }
	    break;
	  }
	  case 1:
	  {
	    adv_entry *e = hash->freelist_claim (tran);
	    if (e == NULL)
	      {
		break;
	      }
	    e->m_key = k;
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	    (void) hash->insert_given (tran, k, e);
	    (void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
	    hash->unlock (tran, e);
	    break;
	  }
	  default:
	    (void) hash->erase (tran, k);
	    break;
	  }
      }
  }

  static int
  case_shared_transys ()
  {
    test_common::sync_cout ("case_shared_transys\n");
    int err = 0;

    {
      string_buffer head;
      head ("  %zu maps, %zu threads, key space %u, mutex off\n", SHT_MAPS, SHT_THREADS, SHT_KEYSPACE);
      say (head);
    }

    // the rewrite: one transaction system, four maps, exactly as thread_manager builds it
    {
      adv_reset_checks ();
      lf_entry_descriptor l_edesc;
      adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

      tran::system l_transys { SHT_THREADS + 1 };
      std::vector<tran::index> l_indexes;
      for (size_t i = 0; i < SHT_THREADS; i++)
	{
	  l_indexes.push_back (l_transys.assign_index ());
	}
      std::vector<adv_hashmap *> l_maps;
      for (size_t i = 0; i < SHT_MAPS; i++)
	{
	  adv_hashmap *h = new adv_hashmap ();
	  test_common::custom_assert (h->init (l_transys, 16, 16, 2, l_edesc) == NO_ERROR);
	  l_maps.push_back (h);
	}

      std::vector<std::thread> l_threads;
      for (size_t i = 0; i < SHT_THREADS; i++)
	{
	  l_threads.emplace_back (shared_worker_new, &l_maps, l_indexes[i], 0xc2b2ae35u * (unsigned int) (i + 1));
	}
      for (auto &it : l_threads)
	{
	  it.join ();
	}

      // every key erased, then enough claim/retire traffic to move the minimum active id, so the live count has
      // somewhere to go back to
      for (size_t m = 0; m < SHT_MAPS; m++)
	{
	  for (unsigned int kv = 0; kv < SHT_KEYSPACE; kv++)
	    {
	      adv_key k = { kv, kv };
	      (void) l_maps[m]->erase (l_indexes[0], k);
	    }
	}
      string_buffer line;
      line ("  lockfree::hashmap live after draining every key:");
      for (size_t m = 0; m < SHT_MAPS; m++)
	{
	  const size_t live = l_maps[m]->get_element_count ();
	  line (" %zu", live);
	  if (live != 0)
	    {
	      err = 1;
	    }
	}
      line ("\n");
      say (line);
      adv_report_checks ("lockfree::hashmap");
      if (g_adv_violations != 0)
	{
	  err = 1;
	}

      for (size_t m = 0; m < SHT_MAPS; m++)
	{
	  l_maps[m]->destroy ();
	  delete l_maps[m];
	}
      for (size_t i = 0; i < SHT_THREADS; i++)
	{
	  l_transys.free_index (l_indexes[i]);
	}
    }

    // the implementation it replaces, in its own production shape: one transaction system per table
    {
      adv_reset_checks ();
      lf_entry_descriptor l_edesc;
      adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

      std::vector<lf_tran_system *> l_transys;
      std::vector<adv_lf_hash *> l_maps;
      for (size_t m = 0; m < SHT_MAPS; m++)
	{
	  lf_tran_system *sys = new lf_tran_system ();
	  lf_tran_system_init (sys, (int) SHT_THREADS + 1);
	  l_transys.push_back (sys);
	  adv_lf_hash *h = new adv_lf_hash ();
	  test_common::custom_assert (h->init (*sys, 16, 2, 16, l_edesc) == NO_ERROR);
	  l_maps.push_back (h);
	}

      std::vector<std::vector<lf_tran_entry *>> l_trans;
      for (size_t i = 0; i < SHT_THREADS; i++)
	{
	  std::vector<lf_tran_entry *> mine;
	  for (size_t m = 0; m < SHT_MAPS; m++)
	    {
	      mine.push_back (lf_tran_request_entry (l_transys[m]));
	    }
	  l_trans.push_back (mine);
	}

      std::vector<std::thread> l_threads;
      for (size_t i = 0; i < SHT_THREADS; i++)
	{
	  l_threads.emplace_back (shared_worker_old, &l_maps, &l_trans[i], 0xc2b2ae35u * (unsigned int) (i + 1));
	}
      for (auto &it : l_threads)
	{
	  it.join ();
	}

      for (size_t m = 0; m < SHT_MAPS; m++)
	{
	  for (unsigned int kv = 0; kv < SHT_KEYSPACE; kv++)
	    {
	      adv_key k = { kv, kv };
	      (void) l_maps[m]->erase (l_trans[0][m], k);
	    }
	}
      string_buffer line;
      line ("  lf_hash_table     live after draining every key:");
      for (size_t m = 0; m < SHT_MAPS; m++)
	{
	  line (" %zu", l_maps[m]->get_element_count ());
	}
      line ("\n");
      say (line);
      adv_report_checks ("lf_hash_table");

      for (size_t i = 0; i < SHT_THREADS; i++)
	{
	  for (size_t m = 0; m < SHT_MAPS; m++)
	    {
	      lf_tran_return_entry (l_trans[i][m]);
	    }
	}
      for (size_t m = 0; m < SHT_MAPS; m++)
	{
	  l_maps[m]->destroy ();
	  delete l_maps[m];
	  lf_tran_system_destroy (l_transys[m]);
	  delete l_transys[m];
	}
    }

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: four maps on one transaction system did not behave as one map does\n");
      }
    return err;
  }

  //
  // case_index_churn () - transaction indexes assigned and freed while other threads operate.
  //
  // A descriptor carries state that outlives its owner: the retired list, m_last_reclaim_minid, m_saved_node and
  // m_did_incr. free_index () returns the index to the bitmap without touching any of it, so the next thread to
  // be given that index inherits all four. Retired nodes left behind are a known and accepted property of both
  // implementations; a *correctness* failure caused by the reuse is not.
  //
  // Two arms, identical total work and identical thread count, so a violation can be attributed to the reuse
  // rather than to the load: one where 24 of the 32 threads take and give back an index around every short
  // burst, one where all 32 keep the index they were given.
  //
  static const size_t ICH_RESIDENT = 8;
  static const size_t ICH_TRANSIENT = 24;
  static const size_t ICH_RESIDENT_OPS = 6000;
  static const size_t ICH_TRANSIENT_LIVES = 40;
  static const size_t ICH_TRANSIENT_OPS = 150;
  static const unsigned int ICH_KEYSPACE = 32;
  static const size_t ICH_HOLD_SPINS = 1000;
  static const size_t ICH_THREADS = ICH_RESIDENT + ICH_TRANSIENT;
  // the fixed arm spreads the churned arm's total work over the same number of threads
  static const size_t ICH_TRANSIENT_TOTAL_OPS = ICH_TRANSIENT_LIVES * ICH_TRANSIENT_OPS;
  static const size_t ICH_CHURNED_TOTAL_OPS = ICH_RESIDENT * ICH_RESIDENT_OPS
      + ICH_TRANSIENT * ICH_TRANSIENT_TOTAL_OPS;
  static const size_t ICH_FIXED_OPS = ICH_CHURNED_TOTAL_OPS / ICH_THREADS;

  static void
  churn_ops_new (adv_hashmap *hash, tran::index index, unsigned int &state, size_t ops)
  {
    for (size_t i = 0; i < ops; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % ICH_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (index, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (ICH_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (index, e);
	      }
	    break;
	  }
	  case 1:
	  {
	    adv_entry *e = hash->freelist_claim (index);
	    if (e == NULL)
	      {
		break;
	      }
	    e->m_key = k;
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	    (void) hash->insert_given (index, k, e);
	    (void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
	    hash->unlock (index, e);
	    break;
	  }
	  default:
	    (void) hash->erase (index, k);
	    break;
	  }
      }
  }

  static void
  resident_worker_new (adv_hashmap *hash, tran::index index, unsigned int seed, size_t ops)
  {
    unsigned int state = seed;
    churn_ops_new (hash, index, state, ops);
  }

  static void
  transient_worker_new (adv_hashmap *hash, tran::system *transys, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t life = 0; life < ICH_TRANSIENT_LIVES; life++)
      {
	tran::index index = transys->assign_index ();
	test_common::custom_assert (index != tran::INVALID_INDEX);
	churn_ops_new (hash, index, state, ICH_TRANSIENT_OPS);
	transys->free_index (index);
      }
  }

  static void
  churn_ops_old (adv_lf_hash *hash, lf_tran_entry *tran, unsigned int &state, size_t ops)
  {
    for (size_t i = 0; i < ops; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % ICH_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (tran, k);
	    if (e != NULL)
	      {
		bool ok = adv_check_entry (e, k, ADV_S_FIND);
		adv_spin (ICH_HOLD_SPINS);
		ok = adv_check_entry (e, k, ADV_S_FIND_HELD) && ok;
		hash->unlock (tran, e);
	      }
	    break;
	  }
	  case 1:
	  {
	    adv_entry *e = hash->freelist_claim (tran);
	    if (e == NULL)
	      {
		break;
	      }
	    e->m_key = k;
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	    (void) hash->insert_given (tran, k, e);
	    (void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
	    hash->unlock (tran, e);
	    break;
	  }
	  default:
	    (void) hash->erase (tran, k);
	    break;
	  }
      }
  }

  static void
  resident_worker_old (adv_lf_hash *hash, lf_tran_entry *tran, unsigned int seed, size_t ops)
  {
    unsigned int state = seed;
    churn_ops_old (hash, tran, state, ops);
  }

  static void
  transient_worker_old (adv_lf_hash *hash, lf_tran_system *transys, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t life = 0; life < ICH_TRANSIENT_LIVES; life++)
      {
	lf_tran_entry *tran = lf_tran_request_entry (transys);
	test_common::custom_assert (tran != NULL);
	churn_ops_old (hash, tran, state, ICH_TRANSIENT_OPS);
	lf_tran_return_entry (tran);
      }
  }

  static int
  ich_arm_new (bool churn_indexes)
  {
    const size_t resident = churn_indexes ? ICH_RESIDENT : ICH_THREADS;
    const size_t transient = churn_indexes ? ICH_TRANSIENT : 0;
    const size_t resident_ops = churn_indexes ? ICH_RESIDENT_OPS : ICH_FIXED_OPS;

    adv_reset_checks ();
    lf_entry_descriptor l_edesc;
    adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

    tran::system l_transys { ICH_THREADS + 1 };
    std::vector<tran::index> l_indexes;
    for (size_t i = 0; i < resident; i++)
      {
	l_indexes.push_back (l_transys.assign_index ());
      }
    adv_hashmap l_hash;
    test_common::custom_assert (l_hash.init (l_transys, 8, 16, 2, l_edesc) == NO_ERROR);

    std::vector<std::thread> l_threads;
    for (size_t i = 0; i < resident; i++)
      {
	l_threads.emplace_back (resident_worker_new, &l_hash, l_indexes[i], 0x27d4eb2fu * (unsigned int) (i + 1),
				resident_ops);
      }
    for (size_t i = 0; i < transient; i++)
      {
	l_threads.emplace_back (transient_worker_new, &l_hash, &l_transys, 0x165667b1u * (unsigned int) (i + 1));
      }
    for (auto &it : l_threads)
      {
	it.join ();
      }

    for (unsigned int kv = 0; kv < ICH_KEYSPACE; kv++)
      {
	adv_key k = { kv, kv };
	(void) l_hash.erase (l_indexes[0], k);
      }
    const size_t live = l_hash.get_element_count ();
    int err = 0;
    string_buffer line;
    line ("  lockfree::hashmap %-24s live after draining every key = %zu, alloc = %zu\n",
	  churn_indexes ? "indexes churned" : "indexes fixed", live, l_hash.get_alloc_element_count ());
    say (line);
    if (live != 0)
      {
	err = 1;
      }
    adv_report_checks ("lockfree::hashmap");
    if (g_adv_violations != 0)
      {
	err = 1;
      }

    l_hash.destroy ();
    for (size_t i = 0; i < resident; i++)
      {
	l_transys.free_index (l_indexes[i]);
      }
    return err;
  }

  static void
  ich_arm_old (bool churn_indexes)
  {
    const size_t resident = churn_indexes ? ICH_RESIDENT : ICH_THREADS;
    const size_t transient = churn_indexes ? ICH_TRANSIENT : 0;
    const size_t resident_ops = churn_indexes ? ICH_RESIDENT_OPS : ICH_FIXED_OPS;

    adv_reset_checks ();
    lf_entry_descriptor l_edesc;
    adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

    lf_tran_system l_transys;
    lf_tran_system_init (&l_transys, (int) (ICH_THREADS + 1));
    std::vector<lf_tran_entry *> l_trans;
    for (size_t i = 0; i < resident; i++)
      {
	l_trans.push_back (lf_tran_request_entry (&l_transys));
      }
    adv_lf_hash l_hash;
    test_common::custom_assert (l_hash.init (l_transys, 8, 2, 16, l_edesc) == NO_ERROR);

    std::vector<std::thread> l_threads;
    for (size_t i = 0; i < resident; i++)
      {
	l_threads.emplace_back (resident_worker_old, &l_hash, l_trans[i], 0x27d4eb2fu * (unsigned int) (i + 1),
				resident_ops);
      }
    for (size_t i = 0; i < transient; i++)
      {
	l_threads.emplace_back (transient_worker_old, &l_hash, &l_transys, 0x165667b1u * (unsigned int) (i + 1));
      }
    for (auto &it : l_threads)
      {
	it.join ();
      }

    for (unsigned int kv = 0; kv < ICH_KEYSPACE; kv++)
      {
	adv_key k = { kv, kv };
	(void) l_hash.erase (l_trans[0], k);
      }
    string_buffer line;
    line ("  lf_hash_table     %-24s live after draining every key = %zu, alloc = %zu\n",
	  churn_indexes ? "indexes churned" : "indexes fixed", l_hash.get_element_count (),
	  l_hash.get_alloc_element_count ());
    say (line);
    adv_report_checks ("lf_hash_table");

    l_hash.destroy ();
    for (size_t i = 0; i < resident; i++)
      {
	lf_tran_return_entry (l_trans[i]);
      }
    lf_tran_system_destroy (&l_transys);
  }

  static int
  case_index_churn ()
  {
    test_common::sync_cout ("case_index_churn\n");
    int err = 0;

    {
      string_buffer head;
      head ("  %zu threads either way, key space %u; churned arm: %zu keep an index, %zu take and give one back"
	    " %zu times\n", ICH_THREADS, ICH_KEYSPACE, ICH_RESIDENT, ICH_TRANSIENT, ICH_TRANSIENT_LIVES);
      say (head);
    }

    err = err | ich_arm_new (true);
    ich_arm_old (true);
    err = err | ich_arm_new (false);
    ich_arm_old (false);

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: an entry held under this load was not the entry that was asked for\n");
      }
    return err;
  }

  //
  // case_iterator_completeness () - what iterate () returns, not only what a holder sees.
  //
  // case_iterate_under_churn () checks that an entry the iterator is standing on stays itself. It cannot check
  // whether the pass was *complete*, because clear () makes the expected population unknowable. Here a resident
  // set is inserted first and never erased, and the churn runs on a disjoint key range with no clear (), so the
  // answer is known exactly: every pass must return all of the resident set and each member once.
  //
  // The resident entries sit at the head of every chain, because insert appends at the tail, so an erase of the
  // first churn entry in a bucket writes the next pointer of a resident entry the iterator may be standing on.
  // That is the interaction being tested. The legacy iterator's twin defect - restart () leaving curr set, which
  // walked 42 entries of a 40-entry map - is the reason a duplicate is counted as loudly as a loss.
  //
  static const unsigned int ITC_RESIDENT = 64;
  static const unsigned int ITC_CHURN_BASE = 1000;
  static const unsigned int ITC_CHURN_SPAN = 400;
  static const size_t ITC_HASH_SIZE = 32;
  static const size_t ITC_CHURN_WORKERS = 6;
  static const size_t ITC_ITERATORS = 4;
  static const size_t ITC_CHURN_OPS = 40000;

  static std::atomic<bool> g_itc_done { false };
  static std::atomic<std::uint64_t> g_itc_passes { 0 };
  static std::atomic<std::uint64_t> g_itc_incomplete { 0 };
  static std::atomic<std::uint64_t> g_itc_duplicated { 0 };
  static std::atomic<std::uint64_t> g_itc_worst_missing { 0 };

  static void
  itc_account (const bool *seen, unsigned int dup_count)
  {
    unsigned int missing = 0;
    for (unsigned int i = 0; i < ITC_RESIDENT; i++)
      {
	if (!seen[i])
	  {
	    ++missing;
	  }
      }
    ++g_itc_passes;
    if (missing != 0)
      {
	++g_itc_incomplete;
	std::uint64_t worst = g_itc_worst_missing.load ();
	while (missing > worst && !g_itc_worst_missing.compare_exchange_weak (worst, missing))
	  ;
      }
    if (dup_count != 0)
      {
	++g_itc_duplicated;
      }
  }

  static void
  itc_iterator_new (adv_hashmap *hash, tran::index index)
  {
    while (!g_itc_done)
      {
	bool seen[ITC_RESIDENT] = {};
	unsigned int dup = 0;
	adv_hashmap::iterator iter { index, *hash };
	for (adv_entry *e = iter.iterate (); e != NULL; e = iter.iterate ())
	  {
	    const unsigned int k1 = e->m_key.m_1;
	    adv_key expected = { k1, e->m_key.m_2 };
	    (void) adv_check_entry (e, expected, ADV_S_ITERATE);
	    if (k1 < ITC_RESIDENT)
	      {
		if (seen[k1])
		  {
		    ++dup;
		  }
		seen[k1] = true;
	      }
	  }
	itc_account (seen, dup);
      }
  }

  static void
  itc_churn_new (adv_hashmap *hash, tran::index index, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < ITC_CHURN_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = ITC_CHURN_BASE + (state % ITC_CHURN_SPAN);
	adv_key k = { kv, kv };
	if ((state >> 16) % 2 == 0)
	  {
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
	  }
	else
	  {
	    (void) hash->erase (index, k);
	  }
      }
  }

  static void
  itc_iterator_old (adv_lf_hash *hash, lf_tran_entry *tran)
  {
    while (!g_itc_done)
      {
	bool seen[ITC_RESIDENT] = {};
	unsigned int dup = 0;
	adv_lf_hash::iterator iter { tran, *hash };
	for (adv_entry *e = iter.iterate (); e != NULL; e = iter.iterate ())
	  {
	    const unsigned int k1 = e->m_key.m_1;
	    adv_key expected = { k1, e->m_key.m_2 };
	    (void) adv_check_entry (e, expected, ADV_S_ITERATE);
	    if (k1 < ITC_RESIDENT)
	      {
		if (seen[k1])
		  {
		    ++dup;
		  }
		seen[k1] = true;
	      }
	  }
	itc_account (seen, dup);
      }
  }

  static void
  itc_churn_old (adv_lf_hash *hash, lf_tran_entry *tran, unsigned int seed)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < ITC_CHURN_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = ITC_CHURN_BASE + (state % ITC_CHURN_SPAN);
	adv_key k = { kv, kv };
	if ((state >> 16) % 2 == 0)
	  {
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
	  }
	else
	  {
	    (void) hash->erase (tran, k);
	  }
      }
  }

  static void
  itc_report (const char *impl)
  {
    string_buffer line;
    line ("  %-17s %llu passes, %llu incomplete (worst %llu of %u missing), %llu with a duplicate\n", impl,
	  (unsigned long long) g_itc_passes.load (), (unsigned long long) g_itc_incomplete.load (),
	  (unsigned long long) g_itc_worst_missing.load (), ITC_RESIDENT,
	  (unsigned long long) g_itc_duplicated.load ());
    say (line);
  }

  static void
  itc_reset ()
  {
    g_itc_done = false;
    g_itc_passes = 0;
    g_itc_incomplete = 0;
    g_itc_duplicated = 0;
    g_itc_worst_missing = 0;
  }

  static int
  case_iterator_completeness ()
  {
    const size_t THREADS = ITC_CHURN_WORKERS + ITC_ITERATORS;

    test_common::sync_cout ("case_iterator_completeness\n");
    int err = 0;

    {
      string_buffer head;
      head ("  %u resident keys never erased, %zu churn threads on keys %u..%u, %zu iterator threads\n",
	    ITC_RESIDENT, ITC_CHURN_WORKERS, ITC_CHURN_BASE, ITC_CHURN_BASE + ITC_CHURN_SPAN - 1, ITC_ITERATORS);
      say (head);
    }

    // the rewrite
    {
      adv_reset_checks ();
      itc_reset ();
      lf_entry_descriptor l_edesc;
      adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

      tran::system l_transys { THREADS + 1 };
      std::vector<tran::index> l_indexes;
      for (size_t i = 0; i < THREADS + 1; i++)
	{
	  l_indexes.push_back (l_transys.assign_index ());
	}
      adv_hashmap l_hash;
      test_common::custom_assert (l_hash.init (l_transys, ITC_HASH_SIZE, 64, 4, l_edesc) == NO_ERROR);

      for (unsigned int kv = 0; kv < ITC_RESIDENT; kv++)
	{
	  adv_key k = { kv, kv };
	  adv_entry *e = l_hash.freelist_claim (l_indexes[THREADS]);
	  test_common::custom_assert (e != NULL);
	  e->m_key = k;
	  e->m_key_copy = k;
	  e->m_magic = ADV_MAGIC_LIVE;
	  test_common::custom_assert (l_hash.insert_given (l_indexes[THREADS], k, e));
	  l_hash.unlock (l_indexes[THREADS], e);
	}

      std::vector<std::thread> l_threads;
      for (size_t i = 0; i < ITC_ITERATORS; i++)
	{
	  l_threads.emplace_back (itc_iterator_new, &l_hash, l_indexes[i]);
	}
      for (size_t i = 0; i < ITC_CHURN_WORKERS; i++)
	{
	  l_threads.emplace_back (itc_churn_new, &l_hash, l_indexes[ITC_ITERATORS + i],
				  0x9e3779b1u * (unsigned int) (i + 1));
	}
      for (size_t i = ITC_ITERATORS; i < THREADS; i++)
	{
	  l_threads[i].join ();
	}
      g_itc_done = true;
      for (size_t i = 0; i < ITC_ITERATORS; i++)
	{
	  l_threads[i].join ();
	}

      itc_report ("lockfree::hashmap");
      adv_report_checks ("lockfree::hashmap");
      if (g_itc_incomplete != 0 || g_itc_duplicated != 0 || g_adv_violations != 0)
	{
	  err = 1;
	}

      l_hash.destroy ();
      for (size_t i = 0; i < THREADS + 1; i++)
	{
	  l_transys.free_index (l_indexes[i]);
	}
    }

    // the implementation it replaces, same load
    {
      adv_reset_checks ();
      itc_reset ();
      lf_entry_descriptor l_edesc;
      adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

      lf_tran_system l_transys;
      lf_tran_system_init (&l_transys, (int) THREADS + 1);
      std::vector<lf_tran_entry *> l_trans;
      for (size_t i = 0; i < THREADS + 1; i++)
	{
	  l_trans.push_back (lf_tran_request_entry (&l_transys));
	}
      adv_lf_hash l_hash;
      test_common::custom_assert (l_hash.init (l_transys, (int) ITC_HASH_SIZE, 4, 64, l_edesc) == NO_ERROR);

      for (unsigned int kv = 0; kv < ITC_RESIDENT; kv++)
	{
	  adv_key k = { kv, kv };
	  adv_entry *e = l_hash.freelist_claim (l_trans[THREADS]);
	  test_common::custom_assert (e != NULL);
	  e->m_key = k;
	  e->m_key_copy = k;
	  e->m_magic = ADV_MAGIC_LIVE;
	  test_common::custom_assert (l_hash.insert_given (l_trans[THREADS], k, e));
	  l_hash.unlock (l_trans[THREADS], e);
	}

      std::vector<std::thread> l_threads;
      for (size_t i = 0; i < ITC_ITERATORS; i++)
	{
	  l_threads.emplace_back (itc_iterator_old, &l_hash, l_trans[i]);
	}
      for (size_t i = 0; i < ITC_CHURN_WORKERS; i++)
	{
	  l_threads.emplace_back (itc_churn_old, &l_hash, l_trans[ITC_ITERATORS + i],
				  0x9e3779b1u * (unsigned int) (i + 1));
	}
      for (size_t i = ITC_ITERATORS; i < THREADS; i++)
	{
	  l_threads[i].join ();
	}
      g_itc_done = true;
      for (size_t i = 0; i < ITC_ITERATORS; i++)
	{
	  l_threads[i].join ();
	}

      itc_report ("lf_hash_table");
      adv_report_checks ("lf_hash_table");

      l_hash.destroy ();
      for (size_t i = 0; i < THREADS + 1; i++)
	{
	  lf_tran_return_entry (l_trans[i]);
	}
      lf_tran_system_destroy (&l_transys);
    }

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: an iterator pass did not return the resident set exactly once\n");
      }
    return err;
  }

  //
  // case_erase_locked_liveness () - erase () and erase_locked () racing on one key.
  //
  // list_delete () marks the next pointer first and only then waits for the entry mutex
  // (lockfree_hashmap.hpp:1201, :1224). A thread calling erase () therefore holds a mark and blocks on a mutex
  // that a thread inside erase_locked () is holding - while that thread's own mark CAS keeps failing on the mark
  // already there and hash_erase_internal () restarts it forever (:1201, :1364). Neither can finish.
  //
  // serial.c and session.c both use LF_EM_USING_MUTEX and call both erase_locked () and erase () on the same
  // table, so this is reachable, and lf_list_delete () has the same order - this is a property of the design and
  // not of the rewrite. Bounded: the threads are given a deadline, and if they miss it the load is abandoned
  // rather than joined, so the case reports instead of hanging the suite.
  //
  static std::atomic<bool> g_elv_stop { false };
  static std::atomic<std::uint64_t> g_elv_erase_locked_ops { 0 };
  static std::atomic<std::uint64_t> g_elv_erase_ops { 0 };
  static std::atomic<int> g_elv_finished { 0 };
  static const size_t ELV_HOLD_SPINS = 200;
  static const int ELV_DEADLINE_MSEC = 1500;
  static const int ELV_GRACE_MSEC = 3000;

  static void
  elv_locked_worker_new (adv_hashmap *hash, tran::index index)
  {
    adv_key k = { 0u, 0u };
    while (!g_elv_stop)
      {
	adv_entry *e = NULL;
	const bool inserted = hash->find_or_insert (index, k, e);
	if (e == NULL)
	  {
	    continue;
	  }
	if (inserted)
	  {
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	  }
	// hold the mutex long enough for the other thread to plant its mark
	adv_spin (ELV_HOLD_SPINS);
	if (!hash->erase_locked (index, k, e))
	  {
	    hash->unlock (index, e);
	  }
	++g_elv_erase_locked_ops;
      }
    ++g_elv_finished;
  }

  static void
  elv_erase_worker_new (adv_hashmap *hash, tran::index index)
  {
    adv_key k = { 0u, 0u };
    while (!g_elv_stop)
      {
	adv_entry *e = NULL;
	const bool inserted = hash->find_or_insert (index, k, e);
	if (e != NULL)
	  {
	    if (inserted)
	      {
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
	      }
	    hash->unlock (index, e);
	  }
	(void) hash->erase (index, k);
	++g_elv_erase_ops;
      }
    ++g_elv_finished;
  }

  static void
  elv_locked_worker_old (adv_lf_hash *hash, lf_tran_entry *tran)
  {
    adv_key k = { 0u, 0u };
    while (!g_elv_stop)
      {
	adv_entry *e = NULL;
	const bool inserted = hash->find_or_insert (tran, k, e);
	if (e == NULL)
	  {
	    continue;
	  }
	if (inserted)
	  {
	    e->m_key_copy = k;
	    e->m_magic = ADV_MAGIC_LIVE;
	  }
	adv_spin (ELV_HOLD_SPINS);
	if (!hash->erase_locked (tran, k, e))
	  {
	    hash->unlock (tran, e);
	  }
	++g_elv_erase_locked_ops;
      }
    ++g_elv_finished;
  }

  static void
  elv_erase_worker_old (adv_lf_hash *hash, lf_tran_entry *tran)
  {
    adv_key k = { 0u, 0u };
    while (!g_elv_stop)
      {
	adv_entry *e = NULL;
	const bool inserted = hash->find_or_insert (tran, k, e);
	if (e != NULL)
	  {
	    if (inserted)
	      {
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
	      }
	    hash->unlock (tran, e);
	  }
	(void) hash->erase (tran, k);
	++g_elv_erase_ops;
      }
    ++g_elv_finished;
  }

  static bool
  elv_wait_for (int expected, int msec)
  {
    for (int waited = 0; waited < msec; waited += 10)
      {
	if (g_elv_finished.load () >= expected)
	  {
	    return true;
	  }
	std::this_thread::sleep_for (std::chrono::milliseconds (10));
      }
    return g_elv_finished.load () >= expected;
  }

  static void
  elv_reset ()
  {
    g_elv_stop = false;
    g_elv_finished = 0;
    g_elv_erase_locked_ops = 0;
    g_elv_erase_ops = 0;
  }

  static int
  case_erase_locked_liveness ()
  {
    test_common::sync_cout ("case_erase_locked_liveness\n");
    int stuck_new = 0;
    int stuck_old = 0;

    // the rewrite. everything is leaked on purpose: if the two threads are wedged they cannot be joined, and
    // they must not be left holding a destroyed map.
    {
      elv_reset ();
      lf_entry_descriptor *l_edesc = new lf_entry_descriptor ();
      adv_edesc_init (*l_edesc, true, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);
      tran::system *l_transys = new tran::system { 4 };
      adv_hashmap *l_hash = new adv_hashmap ();
      test_common::custom_assert (l_hash->init (*l_transys, 1, 8, 2, *l_edesc) == NO_ERROR);

      std::thread t1 (elv_locked_worker_new, l_hash, l_transys->assign_index ());
      std::thread t2 (elv_erase_worker_new, l_hash, l_transys->assign_index ());
      std::this_thread::sleep_for (std::chrono::milliseconds (ELV_DEADLINE_MSEC));
      g_elv_stop = true;
      const bool done = elv_wait_for (2, ELV_GRACE_MSEC);

      string_buffer line;
      line ("  lockfree::hashmap erase_locked = %llu ops, erase = %llu ops, both threads %s after %d msec\n",
	    (unsigned long long) g_elv_erase_locked_ops.load (), (unsigned long long) g_elv_erase_ops.load (),
	    done ? "returned" : "STILL RUNNING", ELV_DEADLINE_MSEC + ELV_GRACE_MSEC);
      say (line);

      if (done)
	{
	  t1.join ();
	  t2.join ();
	  l_hash->destroy ();
	  delete l_hash;
	  delete l_transys;
	  delete l_edesc;
	}
      else
	{
	  stuck_new = 1;
	  t1.detach ();
	  t2.detach ();
	}
    }

    // the implementation it replaces, same race
    {
      elv_reset ();
      lf_entry_descriptor *l_edesc = new lf_entry_descriptor ();
      adv_edesc_init (*l_edesc, true, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);
      lf_tran_system *l_transys = new lf_tran_system ();
      lf_tran_system_init (l_transys, 4);
      adv_lf_hash *l_hash = new adv_lf_hash ();
      test_common::custom_assert (l_hash->init (*l_transys, 1, 2, 8, *l_edesc) == NO_ERROR);

      std::thread t1 (elv_locked_worker_old, l_hash, lf_tran_request_entry (l_transys));
      std::thread t2 (elv_erase_worker_old, l_hash, lf_tran_request_entry (l_transys));
      std::this_thread::sleep_for (std::chrono::milliseconds (ELV_DEADLINE_MSEC));
      g_elv_stop = true;
      const bool done = elv_wait_for (2, ELV_GRACE_MSEC);

      string_buffer line;
      line ("  lf_hash_table     erase_locked = %llu ops, erase = %llu ops, both threads %s after %d msec\n",
	    (unsigned long long) g_elv_erase_locked_ops.load (), (unsigned long long) g_elv_erase_ops.load (),
	    done ? "returned" : "STILL RUNNING", ELV_DEADLINE_MSEC + ELV_GRACE_MSEC);
      say (line);

      if (done)
	{
	  t1.join ();
	  t2.join ();
	  l_hash->destroy ();
	  delete l_hash;
	  lf_tran_system_destroy (l_transys);
	  delete l_transys;
	  delete l_edesc;
	}
      else
	{
	  stuck_old = 1;
	  t1.detach ();
	  t2.detach ();
	}
    }

    if (stuck_new != 0 && stuck_old != 0)
      {
	test_common::sync_cout ("  DESIGN: erase () and erase_locked () wedge each other on both implementations,"
				" so this is not a regression from the rewrite\n");
	return 0;
      }
    if (stuck_new != 0)
      {
	test_common::sync_cout ("  FAILED: erase () and erase_locked () wedge each other only on"
				" lockfree::hashmap\n");
	return 1;
      }
    if (stuck_old != 0)
      {
	test_common::sync_cout ("  NOTE: only lf_hash_table wedged; the rewrite finished this race\n");
      }
    return 0;
  }

  //
  // case_insert_given_promote () - which of two insert entry points loses a held entry, and why.
  //
  // Every held-entry violation this file produces lands at the insert_given () site and none at the find ()
  // site, on both implementations. The two paths differ in one thing. insert_given () hands the map an entry the
  // caller claimed; when the key turns out to be there already the map retires that spare entry before handing
  // back the one it found - lockfree_hashmap.hpp:1055, lf_list_insert_internal () at lock_free.c:1534 - and
  // retiring *promotes the caller's published transaction id to a new, higher global id*
  // (lockfree_transaction_descriptor.cpp:68 -> :109, lf_freelist_retire () at lock_free.c:795-802 ->
  // lf_tran_start (entry, true)). The thread published id T, walked the chain under it, and is now announcing
  // T' >> T while still holding an entry T was protecting: anything retired in between is no longer covered by
  // compute_min_active_tranid (), so the entry it is about to return can be reclaimed and refilled underneath it.
  //
  // find_or_insert () reaches the same output through the same search and never retires on that path, because
  // there is no caller-supplied entry to retire. Two arms, identical load, identical geometry, differing only in
  // that: if the promote is the mechanism, only the insert_given arm fails.
  //
  // The check has to be looser on the find_or_insert arm: the map claims the entry and copies the key before
  // publishing it, but nothing stamps the magic to LIVE, so a reader may legitimately see CLAIMED. FREE still
  // cannot happen without f_uninit, so it still detects a reclaim.
  //
  static const size_t IGP_THREADS = 32;
  static const size_t IGP_OPS = 6000;
  static const unsigned int IGP_KEYSPACE = 32;
  static const size_t IGP_HASH_SIZE = 8;
  static const size_t IGP_HOLD_SPINS = 1000;

  static bool
  adv_check_entry_relaxed (adv_entry *e, const adv_key &wanted, int site)
  {
    const volatile unsigned int *magicp = &e->m_magic;
    const volatile unsigned int *k1p = &e->m_key.m_1;
    const volatile unsigned int *k2p = &e->m_key.m_2;

    const unsigned int magic = *magicp;
    const unsigned int k1 = *k1p;
    const unsigned int k2 = *k2p;

    ++g_adv_checks;
    if (magic != ADV_MAGIC_FREE && k1 == wanted.m_1 && k2 == wanted.m_2)
      {
	return true;
      }

    ++g_adv_violations;
    const size_t slot = g_adv_witness_count++;
    if (slot < ADV_WITNESS_MAX)
      {
	g_adv_witness[slot] = { site, magic, e->m_init_answer, wanted, { k1, k2 }, { k1, k2 } };
      }
    return false;
  }

  static void
  igp_worker_new (adv_hashmap *hash, tran::index index, unsigned int seed, bool preclaim)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < IGP_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % IGP_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (index, k);
	    if (e != NULL)
	      {
		bool ok = preclaim ? adv_check_entry (e, k, ADV_S_FIND) : adv_check_entry_relaxed (e, k, ADV_S_FIND);
		adv_spin (IGP_HOLD_SPINS);
		ok = (preclaim ? adv_check_entry (e, k, ADV_S_FIND_HELD)
		      : adv_check_entry_relaxed (e, k, ADV_S_FIND_HELD)) && ok;
		hash->unlock (index, e);
	      }
	    break;
	  }
	  case 1:
	    if (preclaim)
	      {
		adv_entry *e = hash->freelist_claim (index);
		if (e == NULL)
		  {
		    break;
		  }
		e->m_key = k;
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
		(void) hash->insert_given (index, k, e);
		(void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
		hash->unlock (index, e);
	      }
	    else
	      {
		adv_entry *e = NULL;
		(void) hash->find_or_insert (index, k, e);
		if (e != NULL)
		  {
		    (void) adv_check_entry_relaxed (e, k, ADV_S_FOI);
		    hash->unlock (index, e);
		  }
	      }
	    break;
	  default:
	    (void) hash->erase (index, k);
	    break;
	  }
      }
  }

  static void
  igp_worker_old (adv_lf_hash *hash, lf_tran_entry *tran, unsigned int seed, bool preclaim)
  {
    unsigned int state = seed;
    for (size_t i = 0; i < IGP_OPS; i++)
      {
	state = next_rand (state);
	const unsigned int kv = state % IGP_KEYSPACE;
	adv_key k = { kv, kv };

	switch ((state >> 16) % 3)
	  {
	  case 0:
	  {
	    adv_entry *e = hash->find (tran, k);
	    if (e != NULL)
	      {
		bool ok = preclaim ? adv_check_entry (e, k, ADV_S_FIND) : adv_check_entry_relaxed (e, k, ADV_S_FIND);
		adv_spin (IGP_HOLD_SPINS);
		ok = (preclaim ? adv_check_entry (e, k, ADV_S_FIND_HELD)
		      : adv_check_entry_relaxed (e, k, ADV_S_FIND_HELD)) && ok;
		hash->unlock (tran, e);
	      }
	    break;
	  }
	  case 1:
	    if (preclaim)
	      {
		adv_entry *e = hash->freelist_claim (tran);
		if (e == NULL)
		  {
		    break;
		  }
		e->m_key = k;
		e->m_key_copy = k;
		e->m_magic = ADV_MAGIC_LIVE;
		(void) hash->insert_given (tran, k, e);
		(void) adv_check_entry (e, k, ADV_S_INSERT_GIVEN);
		hash->unlock (tran, e);
	      }
	    else
	      {
		adv_entry *e = NULL;
		(void) hash->find_or_insert (tran, k, e);
		if (e != NULL)
		  {
		    (void) adv_check_entry_relaxed (e, k, ADV_S_FOI);
		    hash->unlock (tran, e);
		  }
	      }
	    break;
	  default:
	    (void) hash->erase (tran, k);
	    break;
	  }
      }
  }

  static int
  igp_arm_new (bool preclaim)
  {
    adv_reset_checks ();
    lf_entry_descriptor l_edesc;
    adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

    tran::system l_transys { IGP_THREADS + 1 };
    std::vector<tran::index> l_indexes;
    for (size_t i = 0; i < IGP_THREADS; i++)
      {
	l_indexes.push_back (l_transys.assign_index ());
      }
    adv_hashmap l_hash;
    test_common::custom_assert (l_hash.init (l_transys, IGP_HASH_SIZE, 16, 2, l_edesc) == NO_ERROR);

    std::vector<std::thread> l_threads;
    for (size_t i = 0; i < IGP_THREADS; i++)
      {
	l_threads.emplace_back (igp_worker_new, &l_hash, l_indexes[i], 0x7feb352du * (unsigned int) (i + 1),
				preclaim);
      }
    for (auto &it : l_threads)
      {
	it.join ();
      }

    string_buffer line;
    line ("  lockfree::hashmap %-28s alloc = %zu\n",
	  preclaim ? "insert_given (pre-claimed)" : "find_or_insert (map-claimed)", l_hash.get_alloc_element_count ());
    say (line);
    adv_report_checks ("lockfree::hashmap");
    const int err = (g_adv_violations != 0) ? 1 : 0;

    l_hash.destroy ();
    for (size_t i = 0; i < IGP_THREADS; i++)
      {
	l_transys.free_index (l_indexes[i]);
      }
    return err;
  }

  static void
  igp_arm_old (bool preclaim)
  {
    adv_reset_checks ();
    lf_entry_descriptor l_edesc;
    adv_edesc_init (l_edesc, false, LF_ENTRY_DESCRIPTOR_MAX_ALLOC);

    lf_tran_system l_transys;
    lf_tran_system_init (&l_transys, (int) IGP_THREADS + 1);
    std::vector<lf_tran_entry *> l_trans;
    for (size_t i = 0; i < IGP_THREADS; i++)
      {
	l_trans.push_back (lf_tran_request_entry (&l_transys));
      }
    adv_lf_hash l_hash;
    test_common::custom_assert (l_hash.init (l_transys, (int) IGP_HASH_SIZE, 2, 16, l_edesc) == NO_ERROR);

    std::vector<std::thread> l_threads;
    for (size_t i = 0; i < IGP_THREADS; i++)
      {
	l_threads.emplace_back (igp_worker_old, &l_hash, l_trans[i], 0x7feb352du * (unsigned int) (i + 1), preclaim);
      }
    for (auto &it : l_threads)
      {
	it.join ();
      }

    string_buffer line;
    line ("  lf_hash_table     %-28s alloc = %zu\n",
	  preclaim ? "insert_given (pre-claimed)" : "find_or_insert (map-claimed)", l_hash.get_alloc_element_count ());
    say (line);
    adv_report_checks ("lf_hash_table");

    l_hash.destroy ();
    for (size_t i = 0; i < IGP_THREADS; i++)
      {
	lf_tran_return_entry (l_trans[i]);
      }
    lf_tran_system_destroy (&l_transys);
  }

  static int
  case_insert_given_promote ()
  {
    test_common::sync_cout ("case_insert_given_promote\n");
    int err = 0;

    {
      string_buffer head;
      head ("  %zu threads, %zu buckets, key space %u, mutex off, %zu ops each arm per thread\n", IGP_THREADS,
	    IGP_HASH_SIZE, IGP_KEYSPACE, IGP_OPS);
      say (head);
    }

    err = err | igp_arm_new (true);
    igp_arm_old (true);
    err = err | igp_arm_new (false);
    igp_arm_old (false);

    if (err != 0)
      {
	test_common::sync_cout ("  FAILED: a held entry changed identity on one of the two insert paths\n");
      }
    return err;
  }
} // namespace test_lockfree
