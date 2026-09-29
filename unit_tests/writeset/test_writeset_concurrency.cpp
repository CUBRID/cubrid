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
 * test_wset_concurrency.cpp - concurrency throughput/latency of the
 *                                     global commit history's probe + flush.
 *
 * This is the measurement the move to a concurrent hash map exists for. Each
 * scenario runs the same probe+flush workload two ways:
 *
 *   tbb     - probe/flush called directly. Their internal shared history lock
 *             lets commits on disjoint keys publish in parallel; only same-key
 *             access serializes on the per-key accessor.
 *   latched - the same probe/flush calls wrapped in one test-owned std::mutex,
 *             reproducing the granularity of a single global latch where every
 *             commit serializes regardless of which keys it touched.
 *
 * The latched arm is a baseline built entirely from the real functions: no map
 * is duplicated and no product code changes. Note that the latched arm still
 * runs probe/flush's own internal history lock inside the test mutex, so it
 * takes two locks per call. It is therefore an upper bound on what the
 * concurrent map buys, not an exact model of a real single-latch build - read
 * the tbb-vs-latched gap as a direction and ceiling, not a precise ratio. The
 * gap between the two arms is what the concurrent map buys. Two key patterns
 * are swept:
 *
 *   no-conflict  - each thread owns a disjoint key range (parallel-friendly).
 *   row-conflict - every thread hits the same key set (worst case for the map).
 *
 * Each thread records the wall time of every probe+flush pair into a
 * pre-reserved vector, so timing never allocates inside the measured region;
 * the vectors are merged afterwards for ops/sec and p50/p99. Workers call only
 * probe/flush - the writeset is collected up front on the main thread, so no
 * worker touches the REF cast path. Tag [benchmark]; run on a release build.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

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

  double
  us_since (clock_type::time_point t0)
  {
    auto d = clock_type::now () - t0;
    return std::chrono::duration<double, std::micro> (d).count ();
  }

  void
  report (const std::string &label, double value, const char *unit)
  {
    std::printf ("[concurrency] %-46s %12.3f %s\n", label.c_str (), value, unit);
    std::fflush (stdout);
  }

  /* nearest-rank percentile of an already-sorted microsecond vector */
  double
  percentile_us (const std::vector<double> &sorted, double q)
  {
    if (sorted.empty ())
      {
	return 0.0;
      }
    size_t idx = (size_t) (q * (double) (sorted.size () - 1) + 0.5);
    if (idx >= sorted.size ())
      {
	idx = sorted.size () - 1;
      }
    return sorted[idx];
  }

  struct engine_result
  {
    double wall_ms;
    std::vector<double> latencies_us;	/* one entry per probe+flush pair */
  };

  /* drive `iters` probe+flush pairs per thread over the prepared transactions.
   * With latched=true each pair runs under the shared test mutex. Every pair's
   * latency lands in a per-thread vector reserved before the timed region, so
   * the measurement never allocates while the clock is running. */
  engine_result
  run_engine (std::vector<log_tdes *> &tx, int iters, bool latched, std::mutex &latch)
  {
    int nthreads = (int) tx.size ();
    std::vector<std::vector<double>> per_thread (nthreads);
    for (int t = 0; t < nthreads; t++)
      {
	per_thread[t].reserve ((size_t) iters);
      }

    std::atomic<int> ready {0};
    std::atomic<bool> go {false};
    std::vector<std::thread> workers;

    for (int t = 0; t < nthreads; t++)
      {
	LOG_LSA commit = lsa_of (1000 + t, 0);	/* each thread commits at its own LSA */
	workers.emplace_back ([ &, t, commit] ()
	{
	  std::vector<double> &lat = per_thread[t];
	  ready.fetch_add (1);
	  while (!go.load ())
	    {
	      std::this_thread::yield ();
	    }
	  for (int i = 0; i < iters; i++)
	    {
	      auto op0 = clock_type::now ();
	      if (latched)
		{
		  latch.lock ();
		}
	      LOG_LSA dep;
	      wset_find_dependency_from_history (tx[t], &dep);
	      wset_publish_commit_to_history (tx[t], &commit);
	      if (latched)
		{
		  latch.unlock ();
		}
	      lat.push_back (us_since (op0));
	    }
	});
      }

    while (ready.load () < nthreads)
      {
	std::this_thread::yield ();
      }
    auto start = clock_type::now ();
    go.store (true);
    for (auto &w : workers)
      {
	w.join ();
      }

    engine_result r;
    r.wall_ms = ms_since (start);
    for (int t = 0; t < nthreads; t++)
      {
	r.latencies_us.insert (r.latencies_us.end (), per_thread[t].begin (), per_thread[t].end ());
      }
    return r;
  }

  /* set up a fresh history, preload the keys, build one worker transaction per
   * thread, run one engine, check correctness that must hold whichever engine
   * ran, and report throughput and tail latency. */
  void
  run_scenario (const char *name, int nthreads, int keys_per_tx, int iters, bool conflict, bool latched)
  {
    ws_history_guard history;
    OID cls = oid_of (1, 100, 1);
    DB_VALUE v;

    /* disjoint scenario publishes nthreads * keys_per_tx distinct keys; the
     * row-conflict scenario shares one keys_per_tx key set across all threads */
    int distinct = conflict ? keys_per_tx : nthreads * keys_per_tx;
    log_tdes *filler = make_tdes (900000);
    for (int i = 0; i < distinct; i++)
      {
	db_make_int (&v, i);
	wset_add_write_key (filler, &cls, &WS_DEFAULT_VFID, &v);
      }
    LOG_LSA fill_lsa = lsa_of (10, 0);
    wset_publish_commit_to_history (filler, &fill_lsa);
    delete filler;

    std::vector<log_tdes *> tx (nthreads);
    for (int t = 0; t < nthreads; t++)
      {
	int base = conflict ? 0 : t * keys_per_tx;	/* shared base = hot key set */
	tx[t] = make_tdes (t + 1);
	for (int i = 0; i < keys_per_tx; i++)
	  {
	    db_make_int (&v, base + i);
	    wset_add_write_key (tx[t], &cls, &WS_DEFAULT_VFID, &v);
	  }
      }

    std::mutex latch;
    engine_result r = run_engine (tx, iters, latched, latch);

    /* the key population never changes: flushes only restamp write slots, so no
     * key is lost or duplicated regardless of how the commits interleaved */
    REQUIRE (wset_History.map.size () == (size_t) distinct);

    /* a fresh writer probing an existing key must see a valid published commit
     * LSA, never null and never garbage. In the disjoint scenario each key was
     * written by exactly one thread, so the writer is that thread and the value
     * is exact. In the row-conflict scenario every thread wrote the key with no
     * row lock ordering them, so write_seq settles on one of their commit LSAs;
     * the deterministic invariant is that it is one of them. */
    log_tdes *probe_tx = make_tdes (999999);
    add_write_int (probe_tx, &cls, 0);
    LOG_LSA dep = probe (probe_tx);
    if (conflict)
      {
	REQUIRE (!LSA_ISNULL (&dep));
	REQUIRE (dep.pageid >= 1000);
	REQUIRE (dep.pageid <= 1000 + nthreads - 1);
      }
    else
      {
	LOG_LSA expected = lsa_of (1000, 0);	/* key 0 belongs to thread 0 */
	REQUIRE (LSA_EQ (&dep, &expected));
      }
    delete probe_tx;

    if (!conflict && nthreads > 1)
      {
	/* a key from thread 1's disjoint range must name thread 1's commit */
	log_tdes *probe_tx1 = make_tdes (999998);
	add_write_int (probe_tx1, &cls, keys_per_tx);
	LOG_LSA dep1 = probe (probe_tx1);
	LOG_LSA expected1 = lsa_of (1001, 0);
	REQUIRE (LSA_EQ (&dep1, &expected1));
	delete probe_tx1;
      }

    std::sort (r.latencies_us.begin (), r.latencies_us.end ());
    double ops_per_sec = (double) r.latencies_us.size () / (r.wall_ms / 1000.0);
    report (std::string (name) + " ops/sec (probe+flush)", ops_per_sec, "ops/s");
    report (std::string (name) + " p50", percentile_us (r.latencies_us, 0.50), "us");
    report (std::string (name) + " p99", percentile_us (r.latencies_us, 0.99), "us");

    for (auto *p : tx)
      {
	delete p;
      }
  }
}

/* The four arms of the 2x2. Each runs on its own freshly initialized history so
 * the tbb and latched arms start from the same clean state and can be compared
 * directly. no-conflict shows the parallel headroom the concurrent map adds
 * over a single latch; row-conflict shows the residual cost when every commit
 * contends on the same keys. */
TEST_CASE ("concurrency: no-conflict disjoint keys, tbb vs latched", "[benchmark]")
{
  const int nthreads = 8;
  const int keys_per_tx = 500;
  const int iters = 400;

  run_scenario ("no-conflict tbb", nthreads, keys_per_tx, iters, false, false);
  run_scenario ("no-conflict latched", nthreads, keys_per_tx, iters, false, true);
}

TEST_CASE ("concurrency: row-conflict same keys, tbb vs latched", "[benchmark]")
{
  const int nthreads = 8;
  const int keys_per_tx = 500;
  const int iters = 400;

  run_scenario ("row-conflict tbb", nthreads, keys_per_tx, iters, true, false);
  run_scenario ("row-conflict latched", nthreads, keys_per_tx, iters, true, true);
}
