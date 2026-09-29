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

/*
 * test_wset_stress.cpp - contention and large-data measurements for the
 *                                writeset global commit history.
 *
 * These target what the integration bench could not isolate:
 *   1. large data:  one big transaction near the per-transaction key limit, and
 *                   the probe/flush cost of walking its whole key set.
 *   2. concurrency: many threads driving probe/flush against the concurrent
 *                   history map, and the reverse-order defence firing under load.
 *
 * The history map is a concurrent hash map guarded by a shared/exclusive lock:
 * probe and per-key publish run under the shared lock, so commits on disjoint
 * keys proceed in parallel; only same-key access serializes on the per-key
 * accessor, and only the rare whole-map clear takes the lock exclusively. So
 * these numbers show where that shared path saturates, not a single-latch wall.
 * The map-capacity clear behaviour is exercised deterministically, without
 * millions of keys, in test_wset_config.cpp.
 *
 * Design note: worker threads call only probe/flush. The writeset is collected
 * up front on the main thread, so no worker touches the REF cast path
 * (db_private_alloc via the TLS THREAD_ENTRY), which is only initialized for the
 * main thread. probe/flush pass thread_p through without dereferencing it.
 *
 * Tag [stress]; heavy (seconds to tens of seconds, hundreds of MB). Run
 * explicitly on a release build: test_writeset "[stress]".
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

#include "test_wset_common.hpp"

using namespace wstest;

namespace
{
  using clock_type = std::chrono::steady_clock;

  double
  ms_since (clock_type::time_point t0)
  {
    auto d = clock_type::now () - t0;
    return std::chrono::duration<double, std::milli> (d).count ();
  }

  void
  report (const std::string &label, double value, const char *unit)
  {
    std::printf ("[stress] %-52s %12.3f %s\n", label.c_str (), value, unit);
    std::fflush (stdout);
  }

  /* the per-transaction key limit and the map capacity are both the cached
   * ha_writeset_history_size; a fresh history_guard has initialized it. keep a
   * flush chunk one key under the limit so the filler never overflows. */
  int
  history_cap ()
  {
    return (int) wset_History.history_size;
  }

  /* run `fn` on `nthreads` threads that all start together (spin on a gate), each
   * looping `iters` times; return the wall-clock milliseconds of the parallel
   * region. fn takes the thread index. */
  template <typename Fn>
  double
  run_parallel (int nthreads, int iters, Fn fn)
  {
    std::atomic<int> ready {0};
    std::atomic<bool> go {false};
    std::vector<std::thread> workers;
    clock_type::time_point start;

    for (int t = 0; t < nthreads; t++)
      {
	workers.emplace_back ([ &, t] ()
	{
	  ready.fetch_add (1);
	  while (!go.load ())
	    {
	      std::this_thread::yield ();
	    }
	  for (int i = 0; i < iters; i++)
	    {
	      fn (t);
	    }
	});
      }

    while (ready.load () < nthreads)
      {
	std::this_thread::yield ();
      }
    start = clock_type::now ();
    go.store (true);
    for (auto &w : workers)
      {
	w.join ();
      }
    return ms_since (start);
  }
}

/* A2: one large transaction near the per-transaction key limit. Measures the
 * collect total and the probe/flush time spent walking the whole key set under
 * the shared history lock for a single commit. */
TEST_CASE ("A2: large single transaction collect/probe/flush cost", "[stress]")
{
  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);
  const int nkeys = history_cap () - 1;	/* just under overflow */
  DB_VALUE v;

  log_tdes *tx = make_tdes (1);
  auto collect_t0 = clock_type::now ();
  for (int i = 0; i < nkeys; i++)
    {
      db_make_int (&v, i);
      wset_add_write_key (tx, &cls, &WS_DEFAULT_VFID, &v);
    }
  double collect_ms = ms_since (collect_t0);
  REQUIRE (!tx->wset_overflow);
  REQUIRE (tx->wset_hashes.size () == (size_t) nkeys);

  LOG_LSA dep;
  auto t0 = clock_type::now ();
  wset_find_dependency_from_history (tx, &dep);
  double probe_ms = ms_since (t0);

  LOG_LSA commit = lsa_of (10, 0);
  t0 = clock_type::now ();
  wset_publish_commit_to_history (tx, &commit);
  double flush_ms = ms_since (t0);

  report ("A2 keys", (double) nkeys, "keys");
  report ("A2 collect total", collect_ms, "ms");
  report ("A2 collect per-key", collect_ms * 1e6 / nkeys, "ns");
  report ("A2 probe (lock hold)", probe_ms, "ms");
  report ("A2 flush (lock hold)", flush_ms, "ms");

  REQUIRE (wset_History.map.size () == (size_t) nkeys);
  delete tx;
}

/* helpers shared by the concurrency measurements: build a preloaded map and a
 * per-thread worker transaction whose keys all hit that map. */
namespace
{
  /* preload `total` distinct WRITE keys [0, total) into the map */
  void
  preload_write_keys (OID *cls, int total)
  {
    DB_VALUE v;
    /* flush in chunks below the per-transaction key limit */
    int done = 0;
    int page = 100;
    while (done < total)
      {
	int n = std::min (total - done, history_cap () - 1);
	log_tdes *filler = make_tdes (10000 + page);
	for (int i = 0; i < n; i++)
	  {
	    db_make_int (&v, done + i);
	    wset_add_write_key (filler, cls, &WS_DEFAULT_VFID, &v);
	  }
	LOG_LSA lsa = lsa_of (page++, 0);
	wset_publish_commit_to_history (filler, &lsa);
	delete filler;
	done += n;
      }
  }

  /* a worker tx of `keys` WRITE keys starting at `base` (disjoint ranges give
   * disjoint keys; a shared base gives a hot key set) */
  log_tdes *
  make_worker (int trid, OID *cls, int base, int keys)
  {
    DB_VALUE v;
    log_tdes *tx = make_tdes (trid);
    for (int i = 0; i < keys; i++)
      {
	db_make_int (&v, base + i);
	wset_add_write_key (tx, cls, &WS_DEFAULT_VFID, &v);
      }
    return tx;
  }
}

/* B2: probe+flush throughput as the thread count rises. Commits touch disjoint
 * keys, so under the shared history lock they should scale with cores until the
 * shared lock and map bandwidth saturate, rather than hitting a single-latch
 * wall at one thread. */
TEST_CASE ("B2: throughput vs thread count", "[stress]")
{
  const int thread_counts[] = { 1, 2, 4, 8, 16, 32, 64, 80, 128, 256, 512 };
  const int keys_per_tx = 1000;
  const int iters = 200;

  for (int nthreads : thread_counts)
    {
      ws_history_guard history;
      OID cls = oid_of (1, 100, 1);

      /* disjoint key ranges per thread; preload them all so every probe hits */
      preload_write_keys (&cls, nthreads * keys_per_tx);
      std::vector<log_tdes *> tx (nthreads);
      for (int t = 0; t < nthreads; t++)
	{
	  tx[t] = make_worker (t + 1, &cls, t * keys_per_tx, keys_per_tx);
	}

      LOG_LSA commit = lsa_of (500, 0);
      double wall = run_parallel (nthreads, iters, [&] (int t)
      {
	LOG_LSA dep;
	wset_find_dependency_from_history (tx[t], &dep);
	wset_publish_commit_to_history (tx[t], &commit);
      });

      long ops = (long) nthreads * iters * 2;	/* probe + flush */
      report ("B2 threads=" + std::to_string (nthreads) + " wall", wall, "ms");
      report ("B2 threads=" + std::to_string (nthreads) + " throughput", ops / wall * 1000.0, "ops/s");

      for (auto *p : tx)
	{
	  delete p;
	}
    }
}

/* B3: keys-per-commit x threads. The shared history lock is held for a commit's
 * whole key loop, so a large commit occupies that shared path far longer than a
 * small one; disjoint keys still publish concurrently, so the cost is the walk,
 * not exclusive waiting. */
TEST_CASE ("B3: cost scales with keys per commit", "[stress]")
{
  const int nthreads = 16;
  /* disjoint preload = nthreads x ksize keys, kept under the map capacity:
   * 16 x 500,000 = 8M < 10M default */
  const int commit_sizes[] = { 10, 1000, 10000, 100000, 500000 };
  const int iters = 50;

  for (int ksize : commit_sizes)
    {
      ws_history_guard history;
      OID cls = oid_of (1, 100, 1);

      preload_write_keys (&cls, nthreads * ksize);
      std::vector<log_tdes *> tx (nthreads);
      for (int t = 0; t < nthreads; t++)
	{
	  tx[t] = make_worker (t + 1, &cls, t * ksize, ksize);
	}

      LOG_LSA commit = lsa_of (500, 0);
      double wall = run_parallel (nthreads, iters, [&] (int t)
      {
	LOG_LSA dep;
	wset_find_dependency_from_history (tx[t], &dep);
	wset_publish_commit_to_history (tx[t], &commit);
      });

      long ops = (long) nthreads * iters * 2;
      report ("B3 keys/commit=" + std::to_string (ksize) + " wall", wall, "ms");
      report ("B3 keys/commit=" + std::to_string (ksize) + " per-op", wall / ops * 1000.0, "us");

      for (auto *p : tx)
	{
	  delete p;
	}
    }
}

/* B3s (supplementary): the B3 keys-per-commit sweep with a single thread. The
 * map is preloaded to the same size as the B3 point (16 x ksize), so the only
 * difference from B3 is the absence of contention. If the per-key jump B3 shows
 * from 100k keys up persists here, it is a property of one call touching that
 * much data, not of the 16-thread load. */
TEST_CASE ("B3s: keys per commit, single thread (supplementary)", "[stress]")
{
  const int commit_sizes[] = { 10, 1000, 10000, 100000, 500000 };
  const int iters = 50;

  for (int ksize : commit_sizes)
    {
      ws_history_guard history;
      OID cls = oid_of (1, 100, 1);

      preload_write_keys (&cls, 16 * ksize);	/* same map size as the B3 point */
      log_tdes *tx = make_worker (1, &cls, 0, ksize);

      LOG_LSA commit = lsa_of (500, 0);
      LOG_LSA dep;
      auto t0 = clock_type::now ();
      for (int i = 0; i < iters; i++)
	{
	  wset_find_dependency_from_history (tx, &dep);
	  wset_publish_commit_to_history (tx, &commit);
	}
      double wall = ms_since (t0);

      long ops = (long) iters * 2;
      report ("B3s keys/commit=" + std::to_string (ksize) + " per-op", wall / ops * 1000.0, "us");
      report ("B3s keys/commit=" + std::to_string (ksize) + " per-key", wall / ops * 1e6 / ksize, "ns");

      delete tx;
    }
}

/* B4: hot key vs disjoint keys at a fixed thread count. Disjoint keys touch
 * independent map entries and publish in parallel under the shared lock; a hot
 * key set forces every thread through the same per-key accessor, so it should
 * now measure slower than the disjoint run. Under the previous single global
 * latch the two were indistinguishable; the concurrent map makes key overlap a
 * real signal. */
TEST_CASE ("B4: hot key vs disjoint keys", "[stress]")
{
  const int nthreads = 16;
  const int keys_per_tx = 1000;
  const int iters = 200;

  double hot_ms = 0.0;
  double disjoint_ms = 0.0;

  {
    ws_history_guard history;
    OID cls = oid_of (1, 100, 1);
    preload_write_keys (&cls, keys_per_tx);	/* one shared key set */
    std::vector<log_tdes *> tx (nthreads);
    for (int t = 0; t < nthreads; t++)
      {
	tx[t] = make_worker (t + 1, &cls, 0, keys_per_tx);	/* same base = hot */
      }
    LOG_LSA commit = lsa_of (500, 0);
    hot_ms = run_parallel (nthreads, iters, [&] (int t)
    {
      LOG_LSA dep;
      wset_find_dependency_from_history (tx[t], &dep);
      wset_publish_commit_to_history (tx[t], &commit);
    });
    for (auto *p : tx)
      {
	delete p;
      }
  }

  {
    ws_history_guard history;
    OID cls = oid_of (1, 100, 1);
    preload_write_keys (&cls, nthreads * keys_per_tx);
    std::vector<log_tdes *> tx (nthreads);
    for (int t = 0; t < nthreads; t++)
      {
	tx[t] = make_worker (t + 1, &cls, t * keys_per_tx, keys_per_tx);	/* disjoint */
      }
    LOG_LSA commit = lsa_of (500, 0);
    disjoint_ms = run_parallel (nthreads, iters, [&] (int t)
    {
      LOG_LSA dep;
      wset_find_dependency_from_history (tx[t], &dep);
      wset_publish_commit_to_history (tx[t], &commit);
    });
    for (auto *p : tx)
      {
	delete p;
      }
  }

  report ("B4 hot-key wall", hot_ms, "ms");
  report ("B4 disjoint wall", disjoint_ms, "ms");
}

/* B5: worst corner - many threads x large commits against a large loaded map. */
TEST_CASE ("B5: worst corner (many threads x large commits x large map)", "[stress]")
{
  const int nthreads = 32;
  const int keys_per_tx = 50000;
  const int iters = 5;

  ws_history_guard history;
  OID cls = oid_of (1, 100, 1);

  preload_write_keys (&cls, nthreads * keys_per_tx);	/* ~1.6M keys */
  std::vector<log_tdes *> tx (nthreads);
  for (int t = 0; t < nthreads; t++)
    {
      tx[t] = make_worker (t + 1, &cls, t * keys_per_tx, keys_per_tx);
    }

  LOG_LSA commit = lsa_of (900, 0);
  double wall = run_parallel (nthreads, iters, [&] (int t)
  {
    LOG_LSA dep;
    wset_find_dependency_from_history (tx[t], &dep);
    wset_publish_commit_to_history (tx[t], &commit);
  });

  long ops = (long) nthreads * iters * 2;
  report ("B5 wall", wall, "ms");
  report ("B5 per-op (probe+flush of 50k keys)", wall / ops, "ms");

  for (auto *p : tx)
    {
      delete p;
    }
}

/* B6: reverse-order defence firing under load. A parent write that hits a key
 * whose REF slot is populated must consult ref_seq (the defence path). Compare
 * throughput against the same write hitting keys with an empty read slot
 * (single-slot equivalent). The delta is the pure cost of the read-slot check;
 * expected negligible (one extra LSA compare per key). */
TEST_CASE ("B6: reverse-order defence firing vs baseline", "[stress]")
{
  const int nthreads = 16;
  const int keys_per_tx = 1000;
  const int iters = 200;

  double defence_ms = 0.0;
  double baseline_ms = 0.0;

  /* baseline: WRITE keys hit entries that have only a write slot */
  {
    ws_history_guard history;
    OID cls = oid_of (1, 100, 1);
    preload_write_keys (&cls, nthreads * keys_per_tx);
    std::vector<log_tdes *> tx (nthreads);
    for (int t = 0; t < nthreads; t++)
      {
	tx[t] = make_worker (t + 1, &cls, t * keys_per_tx, keys_per_tx);
      }
    LOG_LSA commit = lsa_of (500, 0);
    baseline_ms = run_parallel (nthreads, iters, [&] (int t)
    {
      LOG_LSA dep;
      wset_find_dependency_from_history (tx[t], &dep);
      wset_publish_commit_to_history (tx[t], &commit);
    });
    for (auto *p : tx)
      {
	delete p;
      }
  }

  /* defence: the same keys, but each also carries a read-slot stamp from a
   * child reference, so the WRITE probe takes the max(write_seq, ref_seq) path */
  {
    ws_history_guard history;
    OID parent_cls = oid_of (1, 200, 1);
    int total = nthreads * keys_per_tx;

    preload_write_keys (&parent_cls, total);	/* write slots */
    /* stamp read slots via child references to the same keys */
    {
      DB_VALUE v;
      int done = 0;
      int page = 300;
      TP_DOMAIN *int_domain = tp_domain_resolve_default (DB_TYPE_INTEGER);
      REQUIRE (int_domain != NULL);
      while (done < total)
	{
	  int n = std::min (total - done, history_cap () - 1);
	  log_tdes *ref = make_tdes (20000 + page);
	  for (int i = 0; i < n; i++)
	    {
	      db_make_int (&v, done + i);
	      wset_add_ref_key (ref, &parent_cls, &WS_DEFAULT_VFID, &v, int_domain);
	    }
	  LOG_LSA lsa = lsa_of (page++, 0);
	  wset_publish_commit_to_history (ref, &lsa);
	  delete ref;
	  done += n;
	}
    }

    std::vector<log_tdes *> tx (nthreads);
    for (int t = 0; t < nthreads; t++)
      {
	tx[t] = make_worker (t + 1, &parent_cls, t * keys_per_tx, keys_per_tx);	/* WRITE hits read-stamped keys */
      }
    LOG_LSA commit = lsa_of (500, 0);
    defence_ms = run_parallel (nthreads, iters, [&] (int t)
    {
      LOG_LSA dep;
      wset_find_dependency_from_history (tx[t], &dep);
      wset_publish_commit_to_history (tx[t], &commit);
    });
    for (auto *p : tx)
      {
	delete p;
      }
  }

  report ("B6 baseline (write slot only)", baseline_ms, "ms");
  report ("B6 defence (read slot consulted)", defence_ms, "ms");
}

/* helper for the FK-shaped cases below: a worker transaction with the key
 * pattern of an INSERT into a child table with a foreign key - every row adds
 * one WRITE key (the child PK) and one REF key (the parent PK it points at).
 * Built on the main thread because REF collection casts the value through the
 * main thread's runtime; workers only call probe/flush. */
namespace
{
  /* rows rows; row i = WRITE (child_base + i) on child_cls
   *                  + REF (parent_base + i % parent_count) on parent_cls */
  log_tdes *
  make_fk_worker (int trid, OID *child_cls, OID *parent_cls, int child_base, int rows,
		  int parent_base, int parent_count)
  {
    DB_VALUE v;
    TP_DOMAIN *int_domain = tp_domain_resolve_default (DB_TYPE_INTEGER);
    log_tdes *tx = make_tdes (trid);

    for (int i = 0; i < rows; i++)
      {
	db_make_int (&v, child_base + i);
	wset_add_write_key (tx, child_cls, &WS_DEFAULT_VFID, &v);
	db_make_int (&v, parent_base + i % parent_count);
	wset_add_ref_key (tx, parent_cls, &WS_DEFAULT_VFID, &v, int_domain);
      }
    return tx;
  }
}

/* F1: the A2 shape with FK rows. The per-tx key budget counts keys, and an FK
 * row spends two of them, so the largest FK transaction holds half the rows of
 * the largest PK transaction: rows = limit/2 - 1, keys just under the limit.
 * Every parent is distinct, so flush publishes one entry per key (children get
 * the write slot, parents get the read slot). */
TEST_CASE ("F1: large single FK transaction collect/probe/flush cost", "[stress]")
{
  ws_history_guard history;
  OID child_cls = oid_of (1, 100, 1);
  OID parent_cls = oid_of (1, 200, 1);
  const int rows = history_cap () / 2 - 1;

  auto collect_t0 = clock_type::now ();
  log_tdes *tx = make_fk_worker (1, &child_cls, &parent_cls, 0, rows, 0, rows);
  double collect_ms = ms_since (collect_t0);
  REQUIRE (!tx->wset_overflow);
  REQUIRE (tx->wset_hashes.size () == (size_t) rows * 2);

  LOG_LSA dep;
  auto t0 = clock_type::now ();
  wset_find_dependency_from_history (tx, &dep);
  double probe_ms = ms_since (t0);

  LOG_LSA commit = lsa_of (10, 0);
  t0 = clock_type::now ();
  wset_publish_commit_to_history (tx, &commit);
  double flush_ms = ms_since (t0);

  report ("F1 rows", (double) rows, "rows");
  report ("F1 keys (2 per row)", (double) rows * 2, "keys");
  report ("F1 collect total", collect_ms, "ms");
  report ("F1 collect per-row", collect_ms * 1e6 / rows, "ns");
  report ("F1 probe (lock hold)", probe_ms, "ms");
  report ("F1 flush (lock hold)", flush_ms, "ms");

  REQUIRE (wset_History.map.size () == (size_t) rows * 2);
  delete tx;
}

/* F4 (supplementary to the FK cost cases): the FK commit shape swept by rows per
 * commit on a single thread. The PK-side single-thread sweep (B3s) showed
 * per-key cost inflating once one call walks enough data; this locates where the
 * FK shape (two keys per row) crosses that boundary. */
TEST_CASE ("F4: FK rows per commit, single thread", "[stress]")
{
  const int row_counts[] = { 10, 1000, 10000, 17577, 50000, 100000, 250000 };
  const int iters = 50;

  for (int rows : row_counts)
    {
      ws_history_guard history;
      OID child_cls = oid_of (1, 100, 1);
      OID parent_cls = oid_of (1, 200, 1);

      /* children + shared parent pool, all hit */
      preload_write_keys (&child_cls, rows);
      preload_write_keys (&parent_cls, rows);
      log_tdes *tx = make_fk_worker (1, &child_cls, &parent_cls, 0, rows, 0, rows);

      LOG_LSA commit = lsa_of (500, 0);
      LOG_LSA dep;
      auto t0 = clock_type::now ();
      for (int i = 0; i < iters; i++)
	{
	  wset_find_dependency_from_history (tx, &dep);
	  wset_publish_commit_to_history (tx, &commit);
	}
      double wall = ms_since (t0);

      long ops = (long) iters * 2;
      report ("F4 rows=" + std::to_string (rows) + " per-op", wall / ops * 1000.0, "us");
      report ("F4 rows=" + std::to_string (rows) + " per-key", wall / ops * 1e6 / ((long) rows * 2), "ns");

      delete tx;
    }
}

/* F5: few transactions x mass FK inserts - the FK counterpart of the extended
 * B3. 16 concurrent transactions each commit a large FK row set (up to 250k
 * rows = 500k keys walked per call). Children and parents are both disjoint per
 * thread, so the preloaded map is 16x the walked keys - the same walked:stored
 * ratio as B3. Map bound: 16 x 250,000 rows x 2 keys = 8M < 10M default. */
TEST_CASE ("F5: few transactions x mass FK inserts (16 threads, rows sweep)", "[stress]")
{
  const int nthreads = 16;
  const int row_counts[] = { 500, 5000, 50000, 250000 };	/* walked keys: 1k, 10k, 100k, 500k */
  const int iters = 50;

  for (int rows : row_counts)
    {
      ws_history_guard history;
      OID child_cls = oid_of (1, 100, 1);
      OID parent_cls = oid_of (1, 200, 1);

      preload_write_keys (&child_cls, nthreads * rows);
      preload_write_keys (&parent_cls, nthreads * rows);	/* per-thread disjoint parents */
      std::vector<log_tdes *> tx (nthreads);
      for (int t = 0; t < nthreads; t++)
	{
	  tx[t] = make_fk_worker (t + 1, &child_cls, &parent_cls, t * rows, rows, t * rows, rows);
	}

      LOG_LSA commit = lsa_of (500, 0);
      double wall = run_parallel (nthreads, iters, [&] (int t)
      {
	LOG_LSA dep;
	wset_find_dependency_from_history (tx[t], &dep);
	wset_publish_commit_to_history (tx[t], &commit);
      });

      long ops = (long) nthreads * iters * 2;
      report ("F5 rows=" + std::to_string (rows) + " per-op", wall / ops * 1000.0, "us");
      report ("F5 rows=" + std::to_string (rows) + " per-key", wall / ops * 1e6 / ((long) rows * 2), "ns");

      for (auto *p2 : tx)
	{
	  delete p2;
	}
    }
}
