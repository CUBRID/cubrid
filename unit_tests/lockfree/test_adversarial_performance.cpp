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
// test_adversarial_performance.cpp - A/B measurement of lockfree::hashmap against lf_hash_table.
//
// Stricter than the harness in test_hashmap.cpp, because the claims under attack are point estimates: a warm-up
// repetition of both is discarded, every figure is a distribution over >= 7 repetitions, the order of the two is
// alternated between repetitions and the medians are also reported split by position, the ratio is paired inside
// a repetition and carries its own spread and a sign count, and the clock starts after every worker has reached
// a gate so thread creation - identical for both, and therefore a pull toward 1.00 - is outside it.
//
// Driven from environment variables, so one build can also measure one implementation per process:
//
//   ADV_SUITE   repro | sweep | tran | freelist | cap | startup | patho | all   (default all)
//   ADV_ONLY    legacy | new    - measure only that implementation, for cross-process comparison
//   ADV_REPS    repetitions per case (default 7)
//   ADV_EQWIDTH 1 - give lockfree::hashmap the same transaction-scan width legacy gets (see equalize_width ())
//   ADV_MEMCASE lkres | xasl | session   - which real entry the mem suite stands in for
//
// ADVP_TAG names the source revision of the two header-only implementation files this binary was built from, so
// a standalone build that injects an older lockfree_hashmap.hpp / lockfree_freelist.hpp on the include path says
// so in its own output. It defaults to "tree", the version checked out in src/base.
//

#include "test_adversarial_performance.hpp"

#include "test_output.hpp"

#include "lock_free.h"
#include "lockfree_hashmap.hpp"
#include "lockfree_transaction_system.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined (ADVP_TAG)
#define ADVP_TAG "tree"
#endif

namespace test_lockfree
{
  namespace advp
  {
    using clock_type = std::chrono::steady_clock;

    static std::atomic<std::uint64_t> g_sink { 0 };

    // How much useful work a run actually did. The two implementations are handed the same nominal op counts, but
    // the outcome of every op depends on races, so the work each one performs is an outcome and not a constant.
    // The harness in test_hashmap.cpp prints these too (iter_incr, ins_succ, ...) and its own performance run
    // shows them differing by tens of percent between the two implementations, which no ratio of wall clocks can
    // correct for. Reported here so the reader can see whether a ratio is a speed difference or a work difference.
    static std::atomic<std::uint64_t> g_work_iter { 0 };
    static std::atomic<std::uint64_t> g_work_ins { 0 };
    static std::atomic<std::uint64_t> g_work_ers { 0 };

    //
    // configuration
    //
    static size_t
    env_num (const char *name, size_t dflt)
    {
      const char *v = getenv (name);
      if (v == NULL || *v == '\0')
	{
	  return dflt;
	}
      return (size_t) strtoull (v, NULL, 10);
    }

    static bool
    env_flag (const char *name)
    {
      const char *v = getenv (name);
      return v != NULL && *v != '\0' && *v != '0';
    }

    static std::string
    env_text (const char *name, const char *dflt)
    {
      const char *v = getenv (name);
      return (v == NULL || *v == '\0') ? std::string (dflt) : std::string (v);
    }

    static void
    say (const char *fmt, ...)
    {
      char buf[1024];
      va_list ap;
      va_start (ap, fmt);
      vsnprintf (buf, sizeof (buf), fmt, ap);
      va_end (ap);
      test_common::sync_cout (std::string (buf) + "\n");
    }

    //
    // per-thread xorshift, same shape as the one in test_hashmap.cpp - a shared or locking generator would cost
    // more than the code under test
    //
    class rng
    {
      public:
	rng ()
	  : m_state (next_seed ())
	{
	}

	unsigned int operator() ()
	{
	  m_state ^= m_state << 13;
	  m_state ^= m_state >> 17;
	  m_state ^= m_state << 5;
	  return m_state;
	}

      private:
	static unsigned int next_seed ()
	{
	  static std::atomic<unsigned int> seed_counter { 0x9e3779b9 };
	  unsigned int seed = seed_counter += 0x9e3779b9;
	  return seed != 0 ? seed : 0x9e3779b9;
	}

	unsigned int m_state;
    };

    //
    // entry type - laid out exactly like my_entry in test_hashmap.cpp, so the two harnesses measure the same thing
    //
    struct adv_key
    {
      unsigned int m_1;
      unsigned int m_2;
    };

    struct adv_entry
    {
      adv_key m_key;
      adv_entry *m_next;
      adv_entry *m_rstack;
      pthread_mutex_t m_mutex;
      UINT64 m_delid;
      bool m_init;

      adv_entry () = default;
      ~adv_entry () = default;
    };

    static int
    copy_key (void *src, void *dest)
    {
      * (adv_key *) dest = * (adv_key *) src;
      return 0;
    }

    static int
    compare_key (void *key1, void *key2)
    {
      adv_key *a = (adv_key *) key1;
      adv_key *b = (adv_key *) key2;
      return (a->m_1 != b->m_1) || (a->m_2 != b->m_2) ? 1 : 0;
    }

    static unsigned int
    hash_key (void *key, int hash_size)
    {
      return ((adv_key *) key)->m_1 % (unsigned int) hash_size;
    }

    static void *
    alloc_entry ()
    {
      return (void *) new adv_entry ();
    }

    static int
    free_entry (void *p)
    {
      delete (adv_entry *) p;
      return 0;
    }

    static int
    init_entry (void *p)
    {
      ((adv_entry *) p)->m_init = true;
      return 0;
    }

    static int
    uninit_entry (void *p)
    {
      ((adv_entry *) p)->m_init = false;
      return 0;
    }

    static lf_entry_descriptor g_edesc =
    {
      offsetof (adv_entry, m_rstack),
      offsetof (adv_entry, m_next),
      offsetof (adv_entry, m_delid),
      offsetof (adv_entry, m_key),
      offsetof (adv_entry, m_mutex),
      0,
      LF_ENTRY_DESCRIPTOR_MAX_ALLOC,
      alloc_entry,
      free_entry,
      init_entry,
      uninit_entry,
      copy_key,
      compare_key,
      hash_key,
      NULL
    };

    // What lockfree::hashmap really puts on a bucket chain. This used to be mirrored here, member by member,
    // and the mirror went stale the moment the real node lost its owner pointer - it kept reporting eight
    // bytes that were no longer there. hashmap::get_chain_node_size () answers from the real type instead.
    template <typename K, typename E>
    static constexpr size_t
    chain_node_size ()
    {
      return lockfree::hashmap<K, E>::get_chain_node_size ();
    }

    using new_map = lockfree::hashmap<adv_key, adv_entry>;
    using old_map = lf_hash_table_cpp<adv_key, adv_entry>;

    //
    // key generators - copied from test_hashmap.cpp so the mixes are the ones the branch measured
    //
    static void
    keygen_no_conflict (adv_key &k, size_t hash_size, size_t nops, rng &rd)
    {
      k.m_1 = rd () % ((unsigned int) hash_size + 1);
      k.m_2 = rd ();
    }

    static void
    keygen_avg_conflict (adv_key &k, size_t hash_size, size_t nops, rng &rd)
    {
      k.m_1 = rd () % (unsigned int) hash_size;
      size_t bucket_size = std::max ((size_t) 2, (nops / hash_size) * 5);
      k.m_2 = rd () % (unsigned int) bucket_size;
    }

    static void
    keygen_high_conflict (adv_key &k, size_t hash_size, size_t nops, rng &rd)
    {
      k.m_1 = rd () % (unsigned int) hash_size;
      size_t bucket_size = std::max ((size_t) 2, (nops / hash_size));
      k.m_2 = rd () % (unsigned int) bucket_size;
    }

    //
    // work specification
    //
    enum workload_id
    {
      WL_INSERT_FIND,
      WL_FOI_ERASE,
      WL_INSGIVEN_ERASE_CLAIMRET,
      WL_FOI_ERASE_LOCKED,
      WL_INSDEL_ITER_CLEAR,
      WL_TRAN_ONLY,
      WL_CLAIM_RETIRE,
      WL_CLAIM_HOLD_RETIRE,
      WL_SAME_KEY,
      WL_FIND_HEAVY
    };

    struct spec
    {
      workload_id m_wl;
      const char *m_name;
      size_t m_threads;
      size_t m_hash_size;
      bool m_mutex;
      size_t m_p1;
      size_t m_p2;
      size_t m_p3;
      size_t m_p4;
      size_t m_block_size;
      size_t m_block_count;
      size_t m_max_alloc;       // 0 -> uncapped
      bool m_time_init;         // include construction and destruction in the timed region
      size_t m_prefill;         // distinct keys inserted before the clock starts; 0 -> no prefill phase
      size_t m_find_mode;       // 0 all hits, 1 all misses, 2 alternating
    };

    static spec
    make_spec (workload_id wl, const char *name)
    {
      spec sp;
      sp.m_wl = wl;
      sp.m_name = name;
      sp.m_threads = 64;
      sp.m_hash_size = 10000;
      sp.m_mutex = false;
      sp.m_p1 = 0;
      sp.m_p2 = 0;
      sp.m_p3 = 0;
      sp.m_p4 = 0;
      sp.m_block_size = 100;
      sp.m_block_count = 100;
      sp.m_max_alloc = 0;
      sp.m_time_init = false;
      sp.m_prefill = 0;
      sp.m_find_mode = 0;
      return sp;
    }

    //
    // workloads. one template body per mix, so both implementations execute the same statements.
    //
    template <typename Hash, typename Tran>
    static void
    wl_insert_find (const spec &sp, Hash &hash, Tran tr)
    {
      adv_key k;
      adv_entry *ent;
      rng rd;
      size_t acc = 0;

      for (size_t i = 0; i < sp.m_p1; ++i)
	{
	  keygen_no_conflict (k, hash.get_size (), sp.m_p1, rd);
	  if (hash.insert (tr, k, ent))
	    {
	      hash.unlock (tr, ent);
	      ++acc;
	    }
	  ent = hash.find (tr, k);
	  if (ent != NULL)
	    {
	      hash.unlock (tr, ent);
	      ++acc;
	    }
	}
      g_sink += acc;
    }

    template <typename Hash, typename Tran>
    static void
    wl_foi_erase (const spec &sp, Hash &hash, Tran tr)
    {
      adv_key k;
      adv_entry *ent;
      rng rd;
      size_t inserted = 0;
      size_t erased = 0;
      size_t insert_count = sp.m_p1;
      size_t erase_count = sp.m_p2;
      size_t total_ops = insert_count + erase_count;
      size_t left_ops = total_ops;
      size_t hash_size = hash.get_size ();

      while (left_ops > 0)
	{
	  keygen_high_conflict (k, hash_size, total_ops, rd);
	  size_t random_op = rd () % left_ops;
	  if (random_op < insert_count)
	    {
	      if (hash.find_or_insert (tr, k, ent))
		{
		  ++inserted;
		}
	      hash.unlock (tr, ent);
	      --insert_count;
	    }
	  else
	    {
	      if (hash.erase (tr, k))
		{
		  ++erased;
		}
	      --erase_count;
	    }
	  --left_ops;
	}
      g_sink += inserted + erased;
      g_work_ins += inserted;
      g_work_ers += erased;
    }

    template <typename Hash, typename Tran>
    static void
    wl_insgiven_erase_claimret (const spec &sp, Hash &hash, Tran tr)
    {
      adv_key k;
      adv_entry *ent;
      rng rd;
      size_t acc = 0;
      size_t insert_count = sp.m_p1;
      size_t erase_count = sp.m_p2;
      size_t claimret_count = sp.m_p3;
      size_t total_ops = insert_count + erase_count + claimret_count;
      size_t left_ops = total_ops;
      size_t hash_size = hash.get_size ();

      while (left_ops > 0)
	{
	  keygen_high_conflict (k, hash_size, sp.m_p1, rd);
	  size_t random_op = rd () % left_ops;

	  if (random_op < claimret_count)
	    {
	      ent = hash.freelist_claim (tr);
	      hash.freelist_retire (tr, ent);
	      --claimret_count;
	    }
	  else if (random_op < insert_count + claimret_count)
	    {
	      ent = hash.freelist_claim (tr);
	      ent->m_key = k;
	      if (hash.insert_given (tr, k, ent))
		{
		  ++acc;
		}
	      hash.unlock (tr, ent);
	      --insert_count;
	    }
	  else
	    {
	      if (hash.erase (tr, k))
		{
		  ++acc;
		}
	      --erase_count;
	    }
	  --left_ops;
	}
      g_sink += acc;
    }

    template <typename Hash, typename Tran>
    static void
    wl_foi_erase_locked (const spec &sp, Hash &hash, Tran tr)
    {
      adv_key k;
      adv_entry *ent;
      rng rd;
      size_t acc = 0;
      size_t insert_count = sp.m_p1;
      size_t insdel_count = sp.m_p2;
      size_t total_ops = insert_count + insdel_count;
      size_t left_ops = total_ops;
      size_t hash_size = hash.get_size ();

      while (left_ops > 0)
	{
	  keygen_high_conflict (k, hash_size, total_ops, rd);
	  size_t random_op = rd () % left_ops;

	  if (hash.find_or_insert (tr, k, ent))
	    {
	      ++acc;
	    }
	  if (random_op < insert_count)
	    {
	      hash.unlock (tr, ent);
	      --insert_count;
	    }
	  else
	    {
	      (void) hash.erase_locked (tr, k, ent);
	      --insdel_count;
	    }
	  --left_ops;
	}
      g_sink += acc;
    }

    template <typename Hash, typename Tran>
    static void
    wl_insdel_iter_clear (const spec &sp, Hash &hash, Tran tr)
    {
      adv_key k;
      adv_entry *ent;
      rng rd;
      size_t acc = 0;
      size_t iter_incr = 0;
      size_t inserted = 0;
      size_t erased = 0;
      size_t find_insert_count = sp.m_p1;
      size_t erase_count = sp.m_p2;
      size_t iter_count = sp.m_p3;
      size_t clear_count = sp.m_p4;
      size_t total_ops = find_insert_count + erase_count + iter_count + clear_count;
      size_t left_ops = total_ops;
      size_t hash_size = hash.get_size ();

      while (left_ops > 0)
	{
	  keygen_avg_conflict (k, hash_size, find_insert_count, rd);
	  size_t random_op = rd () % left_ops;

	  if (random_op < find_insert_count)
	    {
	      if (hash.find_or_insert (tr, k, ent))
		{
		  ++inserted;
		}
	      hash.unlock (tr, ent);
	      --find_insert_count;
	    }
	  else if (random_op < find_insert_count + erase_count)
	    {
	      if (hash.erase (tr, k))
		{
		  ++erased;
		}
	      --erase_count;
	    }
	  else if (random_op < find_insert_count + erase_count + iter_count)
	    {
	      typename Hash::iterator it { tr, hash };
	      for (adv_entry *ie = it.iterate (); ie != NULL; ie = it.iterate ())
		{
		  ++iter_incr;
		}
	      --iter_count;
	    }
	  else
	    {
	      hash.clear (tr);
	      --clear_count;
	    }
	  --left_ops;
	}
      g_sink += acc + iter_incr + inserted + erased;
      g_work_iter += iter_incr;
      g_work_ins += inserted;
      g_work_ers += erased;
    }

    template <typename Hash, typename Tran>
    static void
    wl_tran_only (const spec &sp, Hash &hash, Tran tr)
    {
      // nothing but the epoch mark: start_tran () publishes the transaction id, end_tran () retires it. this is
      // the operation 19a937ba1 changed from a plain store bracketed by two MEMORY_BARRIER () into a seq_cst
      // store plus a release store.
      for (size_t i = 0; i < sp.m_p1; ++i)
	{
	  hash.start_tran (tr);
	  hash.end_tran (tr);
	}
      g_sink += sp.m_p1;
    }

    template <typename Hash, typename Tran>
    static void
    wl_claim_retire (const spec &sp, Hash &hash, Tran tr)
    {
      adv_entry *ent;
      size_t acc = 0;
      for (size_t i = 0; i < sp.m_p1; ++i)
	{
	  ent = hash.freelist_claim (tr);
	  if (ent == NULL)
	    {
	      break;
	    }
	  hash.freelist_retire (tr, ent);
	  ++acc;
	}
      g_sink += acc;
    }

    template <typename Hash, typename Tran>
    static void
    wl_claim_hold_retire (const spec &sp, Hash &hash, Tran tr)
    {
      // claim m_p2 entries, hold them, then retire the batch. the retired run is what a8f630f8a splices and what
      // the max_alloc_cnt cap of d8dafcf56 decides to free instead of recycle.
      std::vector<adv_entry *> held;
      held.reserve (sp.m_p2);
      size_t acc = 0;

      for (size_t it = 0; it < sp.m_p1; ++it)
	{
	  held.clear ();
	  for (size_t i = 0; i < sp.m_p2; ++i)
	    {
	      adv_entry *ent = hash.freelist_claim (tr);
	      if (ent == NULL)
		{
		  break;
		}
	      held.push_back (ent);
	    }
	  for (size_t i = 0; i < held.size (); ++i)
	    {
	      adv_entry *ent = held[i];
	      hash.freelist_retire (tr, ent);
	    }
	  acc += held.size ();
	}
      g_sink += acc;
    }

    template <typename Hash, typename Tran>
    static void
    wl_same_key (const spec &sp, Hash &hash, Tran tr)
    {
      // every thread hammers one key: maximum contention on one bucket, no list walk at all
      adv_key k = { 7u, 7u };
      adv_entry *ent;
      size_t acc = 0;
      for (size_t i = 0; i < sp.m_p1; ++i)
	{
	  if (hash.find_or_insert (tr, k, ent))
	    {
	      ++acc;
	    }
	  hash.unlock (tr, ent);
	  ent = hash.find (tr, k);
	  if (ent != NULL)
	    {
	      hash.unlock (tr, ent);
	      ++acc;
	    }
	}
      g_sink += acc;
    }

    // A key whose bucket is j % hash_size and whose position inside that bucket is j / hash_size. Prefilling
    // j = 0 .. prefill-1 therefore gives every bucket the same chain length, prefill / hash_size, with no
    // dependence on the PRNG - so a find () costs the same number of node visits in both implementations and a
    // wall-clock ratio is a cost-per-visit ratio.
    static inline void
    keygen_indexed (adv_key &k, size_t j, size_t hash_size)
    {
      k.m_1 = (unsigned int) (j % hash_size);
      k.m_2 = (unsigned int) (j / hash_size);
    }

    template <typename Hash, typename Tran>
    static void
    prefill_indexed (const spec &sp, Hash &hash, Tran tr, size_t tid)
    {
      adv_key k;
      adv_entry *ent;
      size_t hash_size = hash.get_size ();
      for (size_t j = tid; j < sp.m_prefill; j += sp.m_threads)
	{
	  keygen_indexed (k, j, hash_size);
	  if (hash.find_or_insert (tr, k, ent))
	    {
	      // inserted
	    }
	  hash.unlock (tr, ent);
	}
    }

    // find () and nothing else, over a table whose chain length is known exactly. this is the path 7ecd01fc6
    // changed: list_find () used to take the bucket head by value, loaded once at the call site, and now takes
    // it by reference and loads it inside the transaction, re-reading the slot on every restart iteration.
    template <typename Hash, typename Tran>
    static void
    wl_find_heavy (const spec &sp, Hash &hash, Tran tr)
    {
      adv_key k;
      adv_entry *ent;
      rng rd;
      size_t acc = 0;
      size_t hash_size = hash.get_size ();
      size_t span = sp.m_prefill;

      for (size_t i = 0; i < sp.m_p1; ++i)
	{
	  size_t j = (size_t) rd () % span;
	  bool miss = (sp.m_find_mode == 1) || (sp.m_find_mode == 2 && (i & 1) == 0);
	  keygen_indexed (k, miss ? j + span : j, hash_size);
	  ent = hash.find (tr, k);
	  if (ent != NULL)
	    {
	      hash.unlock (tr, ent);
	      ++acc;
	    }
	}
      g_sink += acc;
      g_work_iter += acc;
    }

    struct gate
    {
      std::atomic<size_t> m_arrived { 0 };
      std::atomic<bool> m_go { false };
      std::atomic<size_t> m_prefilled { 0 };
      std::atomic<bool> m_go2 { false };
    };

    template <typename Hash, typename Tran>
    static void
    worker (const spec *sp, Hash *hash, Tran tr, gate *g, size_t tid)
    {
      g->m_arrived.fetch_add (1);
      while (!g->m_go.load (std::memory_order_acquire))
	{
	  std::this_thread::yield ();
	}
      if (sp->m_prefill != 0)
	{
	  prefill_indexed (*sp, *hash, tr, tid);
	  g->m_prefilled.fetch_add (1);
	}
      while (!g->m_go2.load (std::memory_order_acquire))
	{
	  std::this_thread::yield ();
	}

      switch (sp->m_wl)
	{
	case WL_INSERT_FIND:
	  wl_insert_find (*sp, *hash, tr);
	  break;
	case WL_FOI_ERASE:
	  wl_foi_erase (*sp, *hash, tr);
	  break;
	case WL_INSGIVEN_ERASE_CLAIMRET:
	  wl_insgiven_erase_claimret (*sp, *hash, tr);
	  break;
	case WL_FOI_ERASE_LOCKED:
	  wl_foi_erase_locked (*sp, *hash, tr);
	  break;
	case WL_INSDEL_ITER_CLEAR:
	  wl_insdel_iter_clear (*sp, *hash, tr);
	  break;
	case WL_TRAN_ONLY:
	  wl_tran_only (*sp, *hash, tr);
	  break;
	case WL_CLAIM_RETIRE:
	  wl_claim_retire (*sp, *hash, tr);
	  break;
	case WL_CLAIM_HOLD_RETIRE:
	  wl_claim_hold_retire (*sp, *hash, tr);
	  break;
	case WL_SAME_KEY:
	  wl_same_key (*sp, *hash, tr);
	  break;
	case WL_FIND_HEAVY:
	  wl_find_heavy (*sp, *hash, tr);
	  break;
	default:
	  break;
	}
    }

    template <typename Hash, typename Tran>
    static double
    drive (const spec &sp, Hash &hash, std::vector<Tran> &trans, clock_type::time_point init_start)
    {
      gate g;
      std::vector<std::thread> threads;
      threads.reserve (sp.m_threads);
      for (size_t i = 0; i < sp.m_threads; i++)
	{
	  threads.emplace_back (worker<Hash, Tran>, &sp, &hash, trans[i], &g, i);
	}
      while (g.m_arrived.load () < sp.m_threads)
	{
	  std::this_thread::yield ();
	}

      clock_type::time_point t0;
      if (sp.m_prefill != 0)
	{
	  // the table is filled by the same threads, outside the clock, so the measured phase starts from a
	  // table of a known shape in both implementations
	  g.m_go.store (true, std::memory_order_release);
	  while (g.m_prefilled.load () < sp.m_threads)
	    {
	      std::this_thread::yield ();
	    }
	  t0 = clock_type::now ();
	  g.m_go2.store (true, std::memory_order_release);
	}
      else
	{
	  t0 = sp.m_time_init ? init_start : clock_type::now ();
	  g.m_go.store (true, std::memory_order_release);
	  g.m_go2.store (true, std::memory_order_release);
	}
      for (size_t i = 0; i < sp.m_threads; i++)
	{
	  threads[i].join ();
	}
      clock_type::time_point t1 = clock_type::now ();
      return std::chrono::duration<double, std::milli> (t1 - t0).count ();
    }

    // legacy lf_tran_system_init () rounds the entry count up to a multiple of 32 and
    // lf_tran_compute_minimum_transaction_id () scans every one of them; tran::system takes the count literally.
    // below 32 threads that hands lockfree::hashmap a narrower scan for the same load, which is a property of the
    // two harnesses and not of this branch. ADV_EQWIDTH=1 removes it.
    static size_t
    equalize_width (size_t threads)
    {
      if (!env_flag ("ADV_EQWIDTH"))
	{
	  return threads;
	}
      return (threads + 31u) & ~ ((size_t) 31u);
    }

    struct work_counts
    {
      std::uint64_t m_iter;
      std::uint64_t m_ins;
      std::uint64_t m_ers;
    };

    static void
    reset_work ()
    {
      g_work_iter = 0;
      g_work_ins = 0;
      g_work_ers = 0;
    }

    static work_counts
    take_work ()
    {
      work_counts w;
      w.m_iter = g_work_iter.load ();
      w.m_ins = g_work_ins.load ();
      w.m_ers = g_work_ers.load ();
      return w;
    }

    static work_counts g_last_work { 0, 0, 0 };

    static double
    run_new (const spec &sp, size_t *alloc_out = NULL)
    {
      reset_work ();
      g_edesc.using_mutex = sp.m_mutex ? LF_EM_USING_MUTEX : LF_EM_NOT_USING_MUTEX;
      g_edesc.max_alloc_cnt = (sp.m_max_alloc == 0) ? LF_ENTRY_DESCRIPTOR_MAX_ALLOC : (int) sp.m_max_alloc;

      clock_type::time_point init_start = clock_type::now ();
      lockfree::tran::system transys { equalize_width (sp.m_threads) };
      std::vector<lockfree::tran::index> idxs;
      for (size_t i = 0; i < sp.m_threads; i++)
	{
	  idxs.push_back (transys.assign_index ());
	}
      new_map hash;
      hash.init (transys, sp.m_hash_size, sp.m_block_size, sp.m_block_count, g_edesc);

      double ms = drive (sp, hash, idxs, init_start);
      g_last_work = take_work ();

      if (alloc_out != NULL)
	{
	  *alloc_out = hash.get_alloc_element_count ();
	}
      hash.destroy ();
      for (size_t i = 0; i < sp.m_threads; i++)
	{
	  transys.free_index (idxs[i]);
	}
      if (sp.m_time_init)
	{
	  ms = std::chrono::duration<double, std::milli> (clock_type::now () - init_start).count ();
	}
      return ms;
    }

    static double
    run_legacy (const spec &sp, size_t *alloc_out = NULL)
    {
      reset_work ();
      g_edesc.using_mutex = sp.m_mutex ? LF_EM_USING_MUTEX : LF_EM_NOT_USING_MUTEX;
      g_edesc.max_alloc_cnt = (sp.m_max_alloc == 0) ? LF_ENTRY_DESCRIPTOR_MAX_ALLOC : (int) sp.m_max_alloc;

      clock_type::time_point init_start = clock_type::now ();
      lf_tran_system transys;
      lf_tran_system_init (&transys, (int) sp.m_threads);
      std::vector<lf_tran_entry *> ents;
      for (size_t i = 0; i < sp.m_threads; i++)
	{
	  ents.push_back (lf_tran_request_entry (&transys));
	}
      old_map hash;
      hash.init (transys, (int) sp.m_hash_size, (int) sp.m_block_count, (int) sp.m_block_size, g_edesc);

      double ms = drive (sp, hash, ents, init_start);
      g_last_work = take_work ();

      if (alloc_out != NULL)
	{
	  *alloc_out = (size_t) hash.get_freelist ().alloc_cnt;
	}
      hash.destroy ();
      for (size_t i = 0; i < sp.m_threads; i++)
	{
	  lf_tran_return_entry (ents[i]);
	}
      lf_tran_system_destroy (&transys);
      if (sp.m_time_init)
	{
	  ms = std::chrono::duration<double, std::milli> (clock_type::now () - init_start).count ();
	}
      return ms;
    }

    //
    // statistics
    //
    struct dist
    {
      double m_min;
      double m_q1;
      double m_med;
      double m_q3;
      double m_max;
    };

    static double
    quantile (const std::vector<double> &sorted, double q)
    {
      if (sorted.empty ())
	{
	  return 0.0;
	}
      double pos = q * (double) (sorted.size () - 1);
      size_t lo = (size_t) pos;
      size_t hi = std::min (lo + 1, sorted.size () - 1);
      double frac = pos - (double) lo;
      return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }

    static dist
    summarize (std::vector<double> v)
    {
      std::sort (v.begin (), v.end ());
      dist d;
      d.m_min = v.front ();
      d.m_q1 = quantile (v, 0.25);
      d.m_med = quantile (v, 0.5);
      d.m_q3 = quantile (v, 0.75);
      d.m_max = v.back ();
      return d;
    }

    static double
    median_of (std::vector<double> v)
    {
      if (v.empty ())
	{
	  return 0.0;
	}
      std::sort (v.begin (), v.end ());
      return quantile (v, 0.5);
    }

    struct rep
    {
      double m_legacy;
      double m_new;
      bool m_legacy_first;
      work_counts m_legacy_work;
      work_counts m_new_work;
    };

    static void
    describe (const spec &sp)
    {
      say ("");
      char pf[128] = "";
      if (sp.m_prefill != 0)
	{
	  snprintf (pf, sizeof (pf), " prefill=%zu chain=%.1f find=%s", sp.m_prefill,
		    (double) sp.m_prefill / (double) sp.m_hash_size,
		    sp.m_find_mode == 0 ? "hit" : (sp.m_find_mode == 1 ? "miss" : "half"));
	}
      say ("== %s  [tcnt=%zu hsz=%zu mutex=%d p=%zu/%zu/%zu/%zu block=%zux%zu maxalloc=%zu%s%s]",
	   sp.m_name, sp.m_threads, sp.m_hash_size, sp.m_mutex ? 1 : 0, sp.m_p1, sp.m_p2, sp.m_p3, sp.m_p4,
	   sp.m_block_count, sp.m_block_size, sp.m_max_alloc, sp.m_time_init ? " timed-with-init" : "", pf);
    }

    static void
    compare (const spec &sp, size_t reps)
    {
      describe (sp);

      // warm-up, discarded
      (void) run_legacy (sp);
      (void) run_new (sp);

      std::vector<rep> reps_v;
      for (size_t r = 0; r < reps; r++)
	{
	  rep rr;
	  rr.m_legacy_first = ((r % 2) == 0);
	  if (rr.m_legacy_first)
	    {
	      rr.m_legacy = run_legacy (sp);
	      rr.m_legacy_work = g_last_work;
	      rr.m_new = run_new (sp);
	      rr.m_new_work = g_last_work;
	    }
	  else
	    {
	      rr.m_new = run_new (sp);
	      rr.m_new_work = g_last_work;
	      rr.m_legacy = run_legacy (sp);
	      rr.m_legacy_work = g_last_work;
	    }
	  reps_v.push_back (rr);
	}

      std::vector<double> leg, nw, ratio;
      std::vector<double> leg_first, leg_second, new_first, new_second;
      size_t new_wins = 0;
      for (size_t i = 0; i < reps_v.size (); i++)
	{
	  leg.push_back (reps_v[i].m_legacy);
	  nw.push_back (reps_v[i].m_new);
	  ratio.push_back (reps_v[i].m_new / reps_v[i].m_legacy);
	  if (reps_v[i].m_new < reps_v[i].m_legacy)
	    {
	      ++new_wins;
	    }
	  if (reps_v[i].m_legacy_first)
	    {
	      leg_first.push_back (reps_v[i].m_legacy);
	      new_second.push_back (reps_v[i].m_new);
	    }
	  else
	    {
	      leg_second.push_back (reps_v[i].m_legacy);
	      new_first.push_back (reps_v[i].m_new);
	    }
	}

      dist dl = summarize (leg);
      dist dn = summarize (nw);
      dist dr = summarize (ratio);

      say ("   legacy ms : min=%8.2f q1=%8.2f med=%8.2f q3=%8.2f max=%8.2f  (iqr=%.1f%% of med)",
	   dl.m_min, dl.m_q1, dl.m_med, dl.m_q3, dl.m_max, 100.0 * (dl.m_q3 - dl.m_q1) / dl.m_med);
      say ("   new    ms : min=%8.2f q1=%8.2f med=%8.2f q3=%8.2f max=%8.2f  (iqr=%.1f%% of med)",
	   dn.m_min, dn.m_q1, dn.m_med, dn.m_q3, dn.m_max, 100.0 * (dn.m_q3 - dn.m_q1) / dn.m_med);
      say ("   paired ratio new/legacy : min=%.3f q1=%.3f med=%.3f q3=%.3f max=%.3f ; med/med=%.3f ;"
	   " new faster in %zu/%zu reps", dr.m_min, dr.m_q1, dr.m_med, dr.m_q3, dr.m_max, dn.m_med / dl.m_med,
	   new_wins, reps_v.size ());
      say ("   order effect : legacy-first med=%.2f legacy-second med=%.2f | new-first med=%.2f new-second med=%.2f",
	   median_of (leg_first), median_of (leg_second), median_of (new_first), median_of (new_second));

      // work actually done, so a wall-clock ratio can be checked against a work ratio
      std::vector<double> wi_l, wi_n, ws_l, ws_n, we_l, we_n;
      for (size_t i = 0; i < reps_v.size (); i++)
	{
	  wi_l.push_back ((double) reps_v[i].m_legacy_work.m_iter);
	  wi_n.push_back ((double) reps_v[i].m_new_work.m_iter);
	  ws_l.push_back ((double) reps_v[i].m_legacy_work.m_ins);
	  ws_n.push_back ((double) reps_v[i].m_new_work.m_ins);
	  we_l.push_back ((double) reps_v[i].m_legacy_work.m_ers);
	  we_n.push_back ((double) reps_v[i].m_new_work.m_ers);
	}
      double mi_l = median_of (wi_l);
      double mi_n = median_of (wi_n);
      double ms_l = median_of (ws_l);
      double ms_n = median_of (ws_n);
      double me_l = median_of (we_l);
      double me_n = median_of (we_n);
      if (mi_l + mi_n + ms_l + ms_n > 0.0)
	{
	  say ("   work done (median) : iterate_visits legacy=%.0f new=%.0f (new/legacy=%.3f) ;"
	       " inserts legacy=%.0f new=%.0f (%.3f) ; erases legacy=%.0f new=%.0f (%.3f)",
	       mi_l, mi_n, mi_l > 0.0 ? mi_n / mi_l : 0.0, ms_l, ms_n, ms_l > 0.0 ? ms_n / ms_l : 0.0,
	       me_l, me_n, me_l > 0.0 ? me_n / me_l : 0.0);
	}
      bool resolvable = (dr.m_min > 1.0) || (dr.m_max < 1.0);
      say ("   VERDICT : %s", resolvable
	   ? (dr.m_med < 1.0 ? "new faster, every rep agrees" : "new slower, every rep agrees")
	   : "NOT RESOLVABLE - the paired ratio range straddles 1.00");
      say ("RATIO,%s,%s,%zu,%zu,%d,%.3f,%.3f,%.3f,%.2f,%.2f,%zu,%zu", ADVP_TAG, sp.m_name, sp.m_hash_size,
	   sp.m_threads, sp.m_mutex ? 1 : 0, dr.m_med, dr.m_min, dr.m_max, dl.m_med, dn.m_med, new_wins,
	   reps_v.size ());

      if (sp.m_max_alloc != 0 || env_flag ("ADV_PROBE"))
	{
	  // direct evidence that the growth cap is or is not engaged: how many nodes each freelist ended up owning
	  size_t alloc_legacy = 0;
	  size_t alloc_new = 0;
	  (void) run_legacy (sp, &alloc_legacy);
	  (void) run_new (sp, &alloc_new);
	  say ("   freelist alloc_count at end of run : legacy=%zu new=%zu (cap=%zu)", alloc_legacy, alloc_new,
	       sp.m_max_alloc);
	}
    }

    static void
    single (const spec &sp, size_t reps, bool is_new)
    {
      describe (sp);
      (void) (is_new ? run_new (sp) : run_legacy (sp));
      std::vector<double> v;
      for (size_t r = 0; r < reps; r++)
	{
	  v.push_back (is_new ? run_new (sp) : run_legacy (sp));
	}
      dist d = summarize (v);
      say ("   %-6s ms : min=%8.2f q1=%8.2f med=%8.2f q3=%8.2f max=%8.2f", is_new ? "new" : "legacy",
	   d.m_min, d.m_q1, d.m_med, d.m_q3, d.m_max);
      say ("SOLO,%s,%s,%s,%zu,%zu,%d,%.2f,%.2f,%.2f", ADVP_TAG, is_new ? "new" : "legacy", sp.m_name,
	   sp.m_hash_size, sp.m_threads, sp.m_mutex ? 1 : 0, d.m_med, d.m_min, d.m_max);
    }

    static void
    measure (const spec &sp, size_t reps)
    {
      std::string only = env_text ("ADV_ONLY", "");
      if (only == "legacy")
	{
	  single (sp, reps, false);
	}
      else if (only == "new")
	{
	  single (sp, reps, true);
	}
      else
	{
	  compare (sp, reps);
	}
    }

    //
    // suites
    //
    static std::vector<spec>
    pr_cases (size_t hash_size, size_t threads, bool mutex)
    {
      std::vector<spec> out;

      spec a = make_spec (WL_INSERT_FIND, "insert_find");
      a.m_p1 = 10000;
      out.push_back (a);

      spec b = make_spec (WL_FOI_ERASE, "find_or_insert_and_erase");
      b.m_p1 = 10000;
      b.m_p2 = 1000;
      out.push_back (b);

      spec c = make_spec (WL_INSGIVEN_ERASE_CLAIMRET, "insert_given_and_erase_and_claimret");
      c.m_p1 = 10000;
      c.m_p2 = 1000;
      c.m_p3 = 1000;
      out.push_back (c);

      spec d = make_spec (WL_INSDEL_ITER_CLEAR, "insdel_iter_clear");
      d.m_p1 = 10000;
      d.m_p2 = 1000;
      d.m_p3 = 100;
      d.m_p4 = 10;
      out.push_back (d);

      if (mutex)
	{
	  spec e = make_spec (WL_FOI_ERASE_LOCKED, "find_or_inserts_and_erase_locked");
	  e.m_p1 = 10000;
	  e.m_p2 = 1000;
	  out.push_back (e);
	}

      for (size_t i = 0; i < out.size (); i++)
	{
	  out[i].m_hash_size = hash_size;
	  out[i].m_threads = threads;
	  out[i].m_mutex = mutex;
	}
      return out;
    }

    static void
    suite_repro (size_t reps)
    {
      say ("");
      say ("#### SUITE repro - the five mixes of the PR table, 64 threads, both mutex modes");
      size_t sizes[2] = { 100, 10000 };
      for (size_t mi = 0; mi < 2; mi++)
	{
	  for (size_t si = 0; si < 2; si++)
	    {
	      std::vector<spec> cases = pr_cases (sizes[si], 64, mi == 1);
	      for (size_t i = 0; i < cases.size (); i++)
		{
		  measure (cases[i], reps);
		}
	    }
	}
    }

    static void
    suite_sweep (size_t reps)
    {
      say ("");
      say ("#### SUITE sweep - thread-count shape, hsz=10000, mutex off");
      size_t tcs[7] = { 1, 2, 4, 8, 16, 32, 64 };
      for (size_t i = 0; i < 7; i++)
	{
	  spec a = make_spec (WL_FOI_ERASE, "sweep_find_or_insert_and_erase");
	  a.m_p1 = 10000;
	  a.m_p2 = 1000;
	  a.m_hash_size = 10000;
	  a.m_threads = tcs[i];
	  measure (a, reps);
	}
      for (size_t i = 0; i < 7; i++)
	{
	  spec b = make_spec (WL_INSDEL_ITER_CLEAR, "sweep_insdel_iter_clear");
	  b.m_p1 = 10000;
	  b.m_p2 = 1000;
	  b.m_p3 = 100;
	  b.m_p4 = 10;
	  b.m_hash_size = 10000;
	  b.m_threads = tcs[i];
	  measure (b, reps);
	}
    }

    static void
    suite_tran (size_t reps)
    {
      say ("");
      say ("#### SUITE tran - nothing but start_tran ()/end_tran (), the 19a937ba1 barrier change");
      size_t tcs[4] = { 1, 4, 16, 64 };
      for (size_t i = 0; i < 4; i++)
	{
	  spec a = make_spec (WL_TRAN_ONLY, "tran_start_end");
	  a.m_p1 = 2000000;
	  a.m_hash_size = 100;
	  a.m_threads = tcs[i];
	  measure (a, reps);
	}
    }

    static void
    suite_freelist (size_t reps)
    {
      say ("");
      say ("#### SUITE freelist - claim ()/retire () with no hash work at all");
      size_t tcs[3] = { 1, 16, 64 };
      for (size_t i = 0; i < 3; i++)
	{
	  spec a = make_spec (WL_CLAIM_RETIRE, "claim_retire_pairs");
	  a.m_p1 = 100000;
	  a.m_hash_size = 100;
	  a.m_threads = tcs[i];
	  measure (a, reps);
	}
      size_t depths[4] = { 1, 8, 64, 512 };
      for (size_t i = 0; i < 4; i++)
	{
	  spec b = make_spec (WL_CLAIM_HOLD_RETIRE, "claim_hold_retire");
	  b.m_p1 = 200000 / depths[i];
	  b.m_p2 = depths[i];
	  b.m_hash_size = 100;
	  b.m_threads = 16;
	  measure (b, reps);
	}
    }

    static void
    suite_cap (size_t reps)
    {
      say ("");
      say ("#### SUITE cap - max_alloc_cnt, the d8dafcf56 growth cap. 16 threads hold 512 entries each, so the");
      say ("####            live set is ~8192; a cap below that forces every reclaim to free and every claim to");
      say ("####            allocate. uncapped first, for the baseline.");
      size_t caps[6] = { 0, 100000, 8192, 2048, 1024, 128 };
      for (size_t i = 0; i < 6; i++)
	{
	  spec a = make_spec (WL_CLAIM_HOLD_RETIRE, "cap_claim_hold_retire");
	  a.m_p1 = 200;
	  a.m_p2 = 512;
	  a.m_hash_size = 100;
	  a.m_threads = 16;
	  a.m_max_alloc = caps[i];
	  measure (a, reps);
	}
    }

    static void
    suite_startup (size_t reps)
    {
      say ("");
      say ("#### SUITE startup - the two newest commits made the freelist constructor allocate initial_block_count");
      say ("####                 blocks instead of initial_block_count + 1. total allocation is now the same as");
      say ("####                 legacy, but one of those blocks sits in the back-buffer, so the available list");
      say ("####                 starts one block shorter. a burst whose peak demand falls between one block and");
      say ("####                 two must therefore swap the back-buffer and allocate a replacement, which legacy");
      say ("####                 serves out of what it already had.");

      // 64 threads x 200 = 12800 concurrent claims. block of 10000: legacy starts with 20000 available and never
      // allocates; the rewrite starts with 10000 available plus 10000 in the back-buffer, so it runs dry, swaps,
      // and allocates a third block of 10000 while the burst is in flight.
      spec a = make_spec (WL_CLAIM_HOLD_RETIRE, "burst_between_one_and_two_blocks");
      a.m_p1 = 1;
      a.m_p2 = 200;
      a.m_hash_size = 100;
      a.m_threads = 64;
      a.m_block_size = 10000;
      a.m_block_count = 2;
      measure (a, reps);

      // same burst, four blocks: 30000 available on the rewrite, 40000 on legacy, neither runs dry
      spec b = a;
      b.m_name = "burst_inside_four_blocks";
      b.m_block_count = 4;
      measure (b, reps);

      // construction and destruction alone, one thread, so the block accounting is all that is being timed
      spec c = a;
      c.m_name = "construct_and_destroy_only";
      c.m_p1 = 1;
      c.m_p2 = 1;
      c.m_threads = 1;
      c.m_time_init = true;
      measure (c, reps);
    }

    static void
    suite_find (size_t reps)
    {
      say ("");
      say ("#### SUITE find - find () and nothing else, over a prefilled table of an exact chain length. this is");
      say ("####              the path 7ecd01fc6 changed: list_find () took the bucket head by value and now");
      say ("####              takes it by reference, so the slot is loaded inside the transaction and re-read on");
      say ("####              every restart iteration. chain length separates a per-node cost in the walk from a");
      say ("####              per-call cost outside it; threads separate an instruction cost from a coherence");
      say ("####              cost on the slot.");

      const size_t prefill = 64000;
      struct shape
      {
	size_t m_hash_size;
	size_t m_ops;
      };
      shape shapes[3] = { { 64000, 2700000 }, { 8000, 1080000 }, { 1000, 200000 } };
      size_t tcs[4] = { 1, 8, 16, 64 };

      for (size_t si = 0; si < 3; si++)
	{
	  for (size_t ti = 0; ti < 4; ti++)
	    {
	      spec a = make_spec (WL_FIND_HEAVY, "find_hit");
	      a.m_hash_size = shapes[si].m_hash_size;
	      a.m_prefill = prefill;
	      // 64 threads on 16 hardware threads take four times the wall clock for the same work per thread;
	      // shrinking the per-thread count there keeps every case in the same 50-200 ms band, which is where
	      // the clock is trustworthy and the run is affordable
	      a.m_p1 = tcs[ti] > 16 ? shapes[si].m_ops * 16 / tcs[ti] : shapes[si].m_ops;
	      a.m_threads = tcs[ti];
	      a.m_block_size = 1000;
	      a.m_block_count = 100;
	      measure (a, reps);
	    }
	}

      // a miss walks the whole chain instead of half of it, so a per-node cost shows up at twice the weight
      for (size_t si = 0; si < 3; si++)
	{
	  spec b = make_spec (WL_FIND_HEAVY, "find_miss");
	  b.m_hash_size = shapes[si].m_hash_size;
	  b.m_prefill = prefill;
	  b.m_p1 = shapes[si].m_ops / 2;
	  b.m_threads = 16;
	  b.m_find_mode = 1;
	  b.m_block_size = 1000;
	  b.m_block_count = 100;
	  measure (b, reps);
	}

      // 64000 nodes of either footprint fit in this box's 96 MB L3, so the extra bytes per node cost nothing
      // there. one case large enough to miss it.
      spec c = make_spec (WL_FIND_HEAVY, "find_hit_out_of_cache");
      c.m_hash_size = 1000000;
      c.m_prefill = 1000000;
      c.m_p1 = 1200000;
      c.m_threads = 16;
      c.m_block_size = 10000;
      c.m_block_count = 120;
      measure (c, reps);
    }

    static void
    suite_patho (size_t reps)
    {
      say ("");
      say ("#### SUITE patho - shapes the PR table does not cover");

      spec a = make_spec (WL_SAME_KEY, "same_key_hammer");
      a.m_p1 = 100000;
      a.m_hash_size = 1000;
      a.m_threads = 64;
      measure (a, reps);

      spec b = make_spec (WL_INSERT_FIND, "insert_find_hsz1");
      b.m_p1 = 200;
      b.m_hash_size = 1;
      b.m_threads = 16;
      measure (b, reps);

      // insert_find with keygen_no_conflict makes almost every key distinct, so hsz sets the average bucket
      // chain length: 64 * p1 / hsz. sweeping hsz separates a per-node cost in the list walk from a per-call
      // cost outside it.
      size_t sizes[6] = { 100, 300, 1000, 3000, 10000, 100000 };
      for (size_t i = 0; i < 6; i++)
	{
	  spec c = make_spec (WL_INSERT_FIND, "chainlen_insert_find");
	  c.m_p1 = 10000;
	  c.m_hash_size = sizes[i];
	  c.m_threads = 64;
	  measure (c, reps);
	}
    }

    static void
    suite_stability (size_t reps)
    {
      // The same case measured as several independent blocks. Everything inside a block is what a careful
      // single sitting looks like - warm-up discarded, order alternated, a paired sign count - so if the blocks
      // disagree with each other then that discipline is not enough for this case, whatever its within-block
      // verdict says.
      say ("");
      say ("#### SUITE stability - the same two cases measured five times over, as five independent blocks");

      for (size_t block = 0; block < 5; block++)
	{
	  spec a = make_spec (WL_FOI_ERASE, "stab_small_find_or_insert_and_erase");
	  a.m_p1 = 10000;
	  a.m_p2 = 1000;
	  a.m_hash_size = 100;
	  a.m_threads = 64;
	  measure (a, reps);
	}
      for (size_t block = 0; block < 5; block++)
	{
	  spec b = make_spec (WL_INSDEL_ITER_CLEAR, "stab_large_insdel_iter_clear");
	  b.m_p1 = 10000;
	  b.m_p2 = 1000;
	  b.m_p3 = 100;
	  b.m_p4 = 10;
	  b.m_hash_size = 10000;
	  b.m_threads = 64;
	  measure (b, reps);
	}
    }

    static void
    suite_xproc (size_t reps)
    {
      // the three cases worth running one implementation per process: the branch's largest claimed win, the case
      // it concedes it loses, and the claim/retire mix. with ADV_ONLY set each process measures one side only.
      say ("");
      say ("#### SUITE xproc - cases for the one-implementation-per-process control");

      spec a = make_spec (WL_INSDEL_ITER_CLEAR, "xproc_insdel_iter_clear");
      a.m_p1 = 10000;
      a.m_p2 = 1000;
      a.m_p3 = 100;
      a.m_p4 = 10;
      a.m_hash_size = 10000;
      a.m_threads = 64;
      measure (a, reps);

      spec b = make_spec (WL_INSERT_FIND, "xproc_insert_find");
      b.m_p1 = 10000;
      b.m_hash_size = 100;
      b.m_threads = 64;
      measure (b, reps);

      spec c = make_spec (WL_INSGIVEN_ERASE_CLAIMRET, "xproc_insert_given_and_erase_and_claimret");
      c.m_p1 = 10000;
      c.m_p2 = 1000;
      c.m_p3 = 1000;
      c.m_hash_size = 10000;
      c.m_threads = 64;
      measure (c, reps);
    }

    static void
    suite_micro (size_t reps)
    {
      // c95223f65 removed a T construction and destruction from every retire. This harness's entry type has a
      // defaulted constructor, so the fix cannot show up in any A/B number above; size what it saves for an
      // entry type that really initializes its mutex, as xasl_cache_ent does.
      say ("");
      say ("#### SUITE micro - what one pthread_mutex_init/destroy pair costs, single thread");
      const size_t iters = 2000000;
      std::vector<double> mtx_ns;
      std::vector<double> zero_ns;
      for (size_t r = 0; r <= reps; r++)
	{
	  clock_type::time_point t0 = clock_type::now ();
	  for (size_t i = 0; i < iters; i++)
	    {
	      pthread_mutex_t m;
	      pthread_mutex_init (&m, NULL);
	      pthread_mutex_destroy (&m);
	      g_sink += (std::uint64_t) (((char *) &m)[0] == 0 ? 0 : 1);
	    }
	  clock_type::time_point t1 = clock_type::now ();
	  for (size_t i = 0; i < iters; i++)
	    {
	      adv_entry e {};
	      g_sink += (std::uint64_t) (e.m_init ? 1 : 0);
	    }
	  clock_type::time_point t2 = clock_type::now ();
	  if (r == 0)
	    {
	      continue;                 // warm-up
	    }
	  mtx_ns.push_back (std::chrono::duration<double, std::nano> (t1 - t0).count () / (double) iters);
	  zero_ns.push_back (std::chrono::duration<double, std::nano> (t2 - t1).count () / (double) iters);
	}
      dist dm = summarize (mtx_ns);
      dist dz = summarize (zero_ns);
      say ("   pthread_mutex_init+destroy : min=%.2f med=%.2f max=%.2f ns", dm.m_min, dm.m_med, dm.m_max);
      say ("   value-init of this entry   : min=%.2f med=%.2f max=%.2f ns", dz.m_min, dz.m_med, dz.m_max);

      // the three ways of publishing and retiring an epoch mark, in isolation: what the branch writes, the
      // relaxation it could have written instead, and what the legacy path emitted. one thread, one private
      // location, so this is instruction cost only and says nothing about contention.
      say ("");
      say ("#### SUITE micro - publishing and retiring one epoch mark, three orderings, single thread");
      std::vector<double> sc_ns;
      std::vector<double> rel_ns;
      std::vector<double> mb_ns;
      std::atomic<std::uint64_t> mark { 0 };
      volatile std::uint64_t plain_mark = 0;
      for (size_t r = 0; r <= reps; r++)
	{
	  clock_type::time_point t0 = clock_type::now ();
	  for (size_t i = 0; i < iters; i++)
	    {
	      mark.store (i + 1, std::memory_order_seq_cst);
	      mark.store (UINT64_MAX, std::memory_order_release);
	    }
	  clock_type::time_point t1 = clock_type::now ();
	  for (size_t i = 0; i < iters; i++)
	    {
	      mark.store (i + 1, std::memory_order_release);
	      mark.store (UINT64_MAX, std::memory_order_release);
	    }
	  clock_type::time_point t2 = clock_type::now ();
	  for (size_t i = 0; i < iters; i++)
	    {
	      plain_mark = i + 1;
	      MEMORY_BARRIER ();
	      MEMORY_BARRIER ();
	      plain_mark = UINT64_MAX;
	    }
	  clock_type::time_point t3 = clock_type::now ();
	  g_sink += mark.load () + plain_mark;
	  if (r == 0)
	    {
	      continue;
	    }
	  sc_ns.push_back (std::chrono::duration<double, std::nano> (t1 - t0).count () / (double) iters);
	  rel_ns.push_back (std::chrono::duration<double, std::nano> (t2 - t1).count () / (double) iters);
	  mb_ns.push_back (std::chrono::duration<double, std::nano> (t3 - t2).count () / (double) iters);
	}
      dist dsc = summarize (sc_ns);
      dist drel = summarize (rel_ns);
      dist dmb = summarize (mb_ns);
      say ("   seq_cst publish + release retire (this branch) : min=%.2f med=%.2f max=%.2f ns", dsc.m_min,
	   dsc.m_med, dsc.m_max);
      say ("   release publish + release retire (relaxation)  : min=%.2f med=%.2f max=%.2f ns", drel.m_min,
	   drel.m_med, drel.m_max);
      say ("   plain store + two MEMORY_BARRIER () (legacy)   : min=%.2f med=%.2f max=%.2f ns", dmb.m_min,
	   dmb.m_med, dmb.m_max);
    }

    //
    // memory footprint against a real entry size at a real table geometry
    //
    static size_t
    // Linux only. Returns 0 where /proc is not there, and mem_supported () below is what callers check so
    // that a platform without it skips the suite instead of printing zeros as if they were measurements.
    proc_status_kb (const char *field)
    {
      FILE *f = fopen ("/proc/self/status", "r");
      if (f == NULL)
	{
	  return 0;
	}
      char line[256];
      size_t len = strlen (field);
      size_t kb = 0;
      while (fgets (line, sizeof (line), f) != NULL)
	{
	  if (strncmp (line, field, len) == 0)
	    {
	      kb = (size_t) strtoull (line + len + 1, NULL, 10);
	      break;
	    }
	}
      fclose (f);
      return kb;
    }

    // RSS can fall between the two samples - the allocator returning pages, another thread freeing - and
    // proc_status_kb () answers 0 where /proc is not there. Both make an unsigned subtraction wrap and print
    // an astronomical delta as if it were a measurement.
    static size_t
    rss_delta (size_t before, size_t after)
    {
      return (after > before) ? after - before : 0;
    }

    // Whether the resident-set numbers mean anything on this platform at all.
    static bool
    mem_supported ()
    {
      return proc_status_kb ("VmRSS") != 0;
    }

    // adv_entry padded out to the size of a real entry type. the descriptor fields the two implementations
    // actually use are in the same places, so only the payload size changes.
    template <size_t SIZE>
    struct sized_entry
    {
      adv_key m_key;
      sized_entry *m_next;
      sized_entry *m_rstack;
      pthread_mutex_t m_mutex;
      UINT64 m_delid;
      bool m_init;
      char m_pad[SIZE > sizeof (adv_entry) ? SIZE - sizeof (adv_entry) : 1];
    };

    template <size_t SIZE>
    static int
    sized_copy_key (void *src, void *dest)
    {
      * (adv_key *) dest = * (adv_key *) src;
      return 0;
    }

    template <size_t SIZE>
    static void *
    sized_alloc ()
    {
      return (void *) new sized_entry<SIZE> ();
    }

    template <size_t SIZE>
    static int
    sized_free (void *p)
    {
      delete (sized_entry<SIZE> *) p;
      return 0;
    }

    template <size_t SIZE>
    static int
    sized_init (void *p)
    {
      ((sized_entry<SIZE> *) p)->m_init = true;
      return 0;
    }

    template <size_t SIZE>
    static int
    sized_uninit (void *p)
    {
      ((sized_entry<SIZE> *) p)->m_init = false;
      return 0;
    }

    template <size_t SIZE>
    static lf_entry_descriptor &
    sized_edesc ()
    {
      static lf_entry_descriptor d =
      {
	offsetof (sized_entry<SIZE>, m_rstack),
	offsetof (sized_entry<SIZE>, m_next),
	offsetof (sized_entry<SIZE>, m_delid),
	offsetof (sized_entry<SIZE>, m_key),
	offsetof (sized_entry<SIZE>, m_mutex),
	LF_EM_NOT_USING_MUTEX,
	LF_ENTRY_DESCRIPTOR_MAX_ALLOC,
	sized_alloc<SIZE>,
	sized_free<SIZE>,
	sized_init<SIZE>,
	sized_uninit<SIZE>,
	sized_copy_key<SIZE>,
	compare_key,
	hash_key,
	NULL
      };
      return d;
    }

    template <size_t SIZE>
    static void
    mem_case (const char *label, size_t hash_size, size_t live, size_t block_size, size_t block_count,
	      size_t max_alloc, bool is_new)
    {
      using sized = sized_entry<SIZE>;
      lf_entry_descriptor &ed = sized_edesc<SIZE> ();
      ed.max_alloc_cnt = (max_alloc == 0) ? LF_ENTRY_DESCRIPTOR_MAX_ALLOC : (int) max_alloc;

      size_t rss0 = proc_status_kb ("VmRSS");
      size_t alloc_count = 0;
      adv_key k;
      sized *ent;

      if (is_new)
	{
	  lockfree::tran::system transys { 1 };
	  lockfree::tran::index idx = transys.assign_index ();
	  lockfree::hashmap<adv_key, sized> hash;
	  hash.init (transys, hash_size, block_size, block_count, ed);
	  for (size_t j = 0; j < live; j++)
	    {
	      keygen_indexed (k, j, hash_size);
	      (void) hash.find_or_insert (idx, k, ent);
	      hash.unlock (idx, ent);
	    }
	  size_t rss1 = proc_status_kb ("VmRSS");
	  alloc_count = hash.get_alloc_element_count ();
	  say ("   MEM,%s,%s,%s,entry=%zu,node=%zu,hsz=%zu,live=%zu,alloc=%zu,rss_delta_kb=%zu,"
	       "bytes_per_live=%.1f,vmhwm_kb=%zu", ADVP_TAG, "new", label, sizeof (sized),
	       chain_node_size<adv_key, sized> (), hash_size, live, alloc_count,
	       rss_delta (rss0, rss1), 1024.0 * (double) rss_delta (rss0, rss1) / (double) live,
	       proc_status_kb ("VmHWM"));
	  hash.destroy ();
	  transys.free_index (idx);
	}
      else
	{
	  lf_tran_system transys;
	  lf_tran_system_init (&transys, 1);
	  lf_tran_entry *te = lf_tran_request_entry (&transys);
	  lf_hash_table_cpp<adv_key, sized> hash;
	  hash.init (transys, (int) hash_size, (int) block_count, (int) block_size, ed);
	  for (size_t j = 0; j < live; j++)
	    {
	      keygen_indexed (k, j, hash_size);
	      (void) hash.find_or_insert (te, k, ent);
	      hash.unlock (te, ent);
	    }
	  size_t rss1 = proc_status_kb ("VmRSS");
	  alloc_count = (size_t) hash.get_freelist ().alloc_cnt;
	  say ("   MEM,%s,%s,%s,entry=%zu,node=%zu,hsz=%zu,live=%zu,alloc=%zu,rss_delta_kb=%zu,"
	       "bytes_per_live=%.1f,vmhwm_kb=%zu", ADVP_TAG, "legacy", label, sizeof (sized), sizeof (sized),
	       hash_size, live, alloc_count, rss_delta (rss0, rss1),
	       1024.0 * (double) rss_delta (rss0, rss1) / (double) live, proc_status_kb ("VmHWM"));
	  hash.destroy ();
	  lf_tran_return_entry (te);
	  lf_tran_system_destroy (&transys);
	}
    }

    static void
    suite_mem ()
    {
      // A PROXY, not the server. The entry is adv_entry padded to the size of the real one and the geometry is
      // the one the real caller passes, but the real entry's own heap-allocated members are not here, so this
      // sizes the map's overhead per entry and nothing else.
      //
      //   LK_RES         sizeof 120, lock_manager.c:1272 - obj_hash_size = MAX (initial_object_locks = 10000,
      //                  MIN (num_trans * lock_escalation * 3 / 1000, 2^23)); with the default lock_escalation
      //                  100000 and ~101 transactions that is 30300 buckets, block 2 x 500, cap = 100000
      //   xasl_cache_ent sizeof 272, xasl_cache.c:335 - hash size = max_plan_cache_entries (default 1000),
      //                  block count 2, block size 500
      //   session_state  sizeof 312, session.c:621 - hash size 1000, block size 2, block count 50
      say ("");
      if (!mem_supported ())
	{
	  say ("");
	  say ("#### SUITE mem - skipped: /proc/self/status is not readable, so there is nothing to measure");
	  return;
	}
      say ("#### SUITE mem - resident set against a real entry SIZE at the real table geometry. proxy only:");
      say ("####             the entry is adv_entry padded to that size, not the real type.");
      std::string only = env_text ("ADV_ONLY", "new");
      bool is_new = (only != "legacy");
      std::string which = env_text ("ADV_MEMCASE", "lkres");
      say ("   impl=%s case=%s", is_new ? "new" : "legacy", which.c_str ());

      if (which == "lkres" || which == "all")
	{
	  mem_case<120> ("lk_res", 30300, 100000, 500, 2, 100000, is_new);
	}
      if (which == "xasl" || which == "all")
	{
	  mem_case<272> ("xasl_cache_ent", 1000, 1000, 500, 2, 0, is_new);
	}
      if (which == "session" || which == "all")
	{
	  mem_case<312> ("session_state", 1000, 500, 2, 50, 0, is_new);
	}
    }

    static int
    run ()
    {
      size_t reps = env_num ("ADV_REPS", 7);
      std::string suite = env_text ("ADV_SUITE", "all");
      std::string only = env_text ("ADV_ONLY", "");

      say ("test_adversarial_performance: tag=%s suite=%s reps=%zu only=%s eqwidth=%d hw_concurrency=%u",
	   ADVP_TAG, suite.c_str (), reps, only.empty () ? "-" : only.c_str (), env_flag ("ADV_EQWIDTH") ? 1 : 0,
	   std::thread::hardware_concurrency ());
#if defined (NDEBUG)
      say ("build: NDEBUG defined (release; assertions off)");
#else
      say ("build: NDEBUG NOT defined (debug; assertions on - timings are not release timings)");
#endif
      // node footprint. legacy stores an entry exactly as the descriptor describes it and reaches its links
      // through offsets; the rewrite wraps the entry in a freelist node. That wrapper was five words - a vtable
      // pointer, the retire link, the retire id, the owning freelist and the entry descriptor - and is now two:
      // the retire link and the retire id. Every bucket chain walk touches that much more per node than legacy.
      say ("node footprint: entry=%zu bytes; legacy chain node=%zu bytes; rewrite chain node=%zu bytes"
	   " (reclaimable_node=%zu + entry)",
	   sizeof (adv_entry), sizeof (adv_entry), chain_node_size<adv_key, adv_entry> (),
	   sizeof (lockfree::tran::reclaimable_node));

      bool all = (suite == "all");
      if (all || suite == "repro")
	{
	  suite_repro (reps);
	}
      if (all || suite == "sweep")
	{
	  suite_sweep (reps);
	}
      if (all || suite == "tran")
	{
	  suite_tran (reps);
	}
      if (all || suite == "freelist")
	{
	  suite_freelist (reps);
	}
      if (all || suite == "cap")
	{
	  suite_cap (reps);
	}
      if (all || suite == "startup")
	{
	  suite_startup (reps);
	}
      if (all || suite == "find")
	{
	  suite_find (reps);
	}
      if (all || suite == "patho")
	{
	  suite_patho (reps);
	}
      if (suite == "xproc")
	{
	  suite_xproc (reps);
	}
      if (suite == "stability")
	{
	  suite_stability (reps);
	}
      if (suite == "mem")
	{
	  suite_mem ();
	}
      if (all || suite == "micro")
	{
	  suite_micro (reps);
	}

      say ("");
      say ("test_adversarial_performance: done (sink=%llu)", (unsigned long long) g_sink.load ());
      return 0;
    }
  } // namespace advp

  int
  test_adversarial_performance ()
  {
    return advp::run ();
  }
} // namespace test_lockfree

#if defined (ADVP_STANDALONE)
// Built on its own, outside test_lockfree, so that an older revision of the two header-only implementation files
// can be put in front of src/base on the include path without disturbing the tree or the rest of the suite. The
// legacy implementation lives in libcubrid and is therefore the same code in every such binary, which is what
// makes the in-process paired ratio comparable across them.
int
main (void)
{
  return test_lockfree::test_adversarial_performance ();
}
#endif // ADVP_STANDALONE
