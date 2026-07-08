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
 * unittests_snapshot.c : unit tests for snapshot
 */

#include "porting.h"

#include "thread_manager.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <pthread.h>
#include <log_impl.h>
#include <sys/time.h>

#define strlen(s1) ((int) strlen(s1))

#define NOPS_SNAPSHOT   1000000
#define NOPS_COMPLPETE  1000000
#define NOPS_OLDEST     2000000

/* bit area sizes expressed in bits */
#define MVCC_BITAREA_ELEMENT_BITS 64
#define MVCC_BITAREA_ELEMENT_ALL_COMMITTED 0xffffffffffffffffULL
#define MVCC_BITAREA_BIT_COMMITTED 1
#define MVCC_BITAREA_BIT_ACTIVE 0

/* bit area size after cleanup */
#define MVCC_BITAREA_ELEMENTS_AFTER_FULL_CLEANUP      16

/* maximum size - 500 UINT64 */
#define MVCC_BITAREA_MAXIMUM_ELEMENTS		     500

/* maximum size - 32000 bits */
#define MVCC_BITAREA_MAXIMUM_BITS		   32000

#define MVCC_BITAREA_BITS_TO_ELEMENTS(count_bits) (((count_bits) + 63) >> 6)
#define MVCC_BITAREA_BITS_TO_BYTES(count_bits) ((((count_bits) + 63) >> 6) << 3)
#define MVCC_BITAREA_ELEMENTS_TO_BYTES(count_elements) ((count_elements) << 3)
#define MVCC_BITAREA_ELEMENTS_TO_BITS(count_elements) ((count_elements) << 6)

/* print function */
static struct timeval start_time;

static void
begin (char *test_name)
{
#define MSG_LEN	  40
  int i;

  printf ("Testing %s", test_name);
  for (i = 0; i < MSG_LEN - strlen (test_name); i++)
    {
      putchar (' ');
    }
  printf ("...\n");

  gettimeofday (&start_time, NULL);

#undef MSG_LEN
}

static int
success ()
{
  struct timeval end_time;
  long long int elapsed_msec = 0;

  gettimeofday (&end_time, NULL);

  elapsed_msec = (end_time.tv_usec - start_time.tv_usec) / 1000;
  elapsed_msec += (end_time.tv_sec - start_time.tv_sec) * 1000;

  printf (" %s [%9.3f sec]\n", "OK", (float) elapsed_msec / 1000.0f);
  return NO_ERROR;
}

static void
logtb_initialize_mvcctable (void)
{
  log_Gl.mvcc_table.initialize ();
}


static void
logtb_finalize_mvcctable ()
{
  log_Gl.mvcc_table.finalize ();
}

static unsigned int
logtb_tran_btid_hash_func (const void *key, const unsigned int ht_size)
{
  return 0;
}

static int
logtb_tran_btid_hash_cmp_func (const void *key1, const void *key2)
{
  return 0;
}


static void
logtb_initialize_tdes_for_mvcc_testing (LOG_TDES * tdes, int tran_index)
{
  /* TODO: this is completely unsafe.
   * Using memset() on LOG_TDES is not appropriate.
   * However, since this is the only place in the entire codebase where memset() is used and it is for testing purposes,
   * we ensure that no compile warnings are triggered.   */
  memset ((void *) tdes, 0, sizeof (LOG_TDES));

  tdes->tran_index = tran_index;
  tdes->trid = NULL_TRANID;

  tdes->mvccinfo.init ();

  tdes->log_upd_stats.unique_stats_hash =
    mht_create ("Tran_unique_stats", 101, logtb_tran_btid_hash_func, logtb_tran_btid_hash_cmp_func);
  tdes->log_upd_stats.classes_cos_hash = mht_create ("Tran_classes_cos", 101, oid_hash, oid_compare_equals);
}


static int
logtb_initialize_mvcc_testing (int num_threads, THREAD_ENTRY ** thread_array)
{
  LOG_ADDR_TDESAREA *area = NULL;	/* Contiguous area for new transaction indices */
  size_t size, area_size;
  int i;
  THREAD_ENTRY *thread_p;
  int error_code = NO_ERROR;
  LOG_TDES *tdes;

  if (num_threads == 0 || thread_array == NULL)
    {
      return ER_FAILED;
    }

  log_Gl.trantable.area = NULL;
  log_Gl.trantable.all_tdes = NULL;

  size = num_threads * sizeof (THREAD_ENTRY);
  *thread_array = (THREAD_ENTRY *) malloc (size);
  if (*thread_array == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  for (i = 0; i < num_threads; i++)
    {
      // placement new
      new ((*thread_array) + i) THREAD_ENTRY ();
    }

  for (i = 0; i < num_threads; i++)
    {
      thread_p = *thread_array + i;
      thread_p->type = TT_WORKER;	/* init */
      thread_p->index = i;
      thread_p->tran_index = i + 1;	/* quick fix to avoid issue in logtb_get_mvcc_snapshot - LOG_SYSTEM_TRAN_INDEX */
    }

  size = num_threads * sizeof (*log_Gl.trantable.all_tdes);
  log_Gl.trantable.all_tdes = (LOG_TDES **) malloc (size);
  if (log_Gl.trantable.all_tdes == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  area_size = num_threads * sizeof (LOG_TDES) + sizeof (LOG_ADDR_TDESAREA);
  area = (LOG_ADDR_TDESAREA *) malloc (area_size);
  if (area == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  area->tdesarea = ((LOG_TDES *) ((char *) area + sizeof (LOG_ADDR_TDESAREA)));
  area->next = NULL;

  /*
   * Initialize every newly created transaction descriptor index
   */
  for (i = 0; i < num_threads; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i] = &area->tdesarea[i];
      logtb_initialize_tdes_for_mvcc_testing (tdes, i);
    }

  log_Gl.trantable.area = area;
  log_Gl.trantable.num_total_indices = num_threads;

  logtb_initialize_mvcctable ();
  log_Gl.hdr.mvcc_next_id = MVCCID_FIRST;

  return NO_ERROR;

error:
  if (*thread_array)
    {
      free_and_init (*thread_array);
    }

  if (log_Gl.trantable.all_tdes)
    {
      free_and_init (log_Gl.trantable.all_tdes);
    }

  if (log_Gl.trantable.area)
    {
      free_and_init (log_Gl.trantable.area);
    }

  return error_code;
}

static void
logtb_finalize_mvcc_testing (THREAD_ENTRY ** thread_array)
{
  LOG_TDES *tdes;
  MVCC_INFO *curr_mvcc_info;
  int i;

  logtb_finalize_mvcctable ();

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      curr_mvcc_info = &tdes->mvccinfo;

      curr_mvcc_info->snapshot.m_active_mvccs.finalize ();

      if (tdes->log_upd_stats.unique_stats_hash != NULL)
	{
	  mht_destroy (tdes->log_upd_stats.unique_stats_hash);
	  tdes->log_upd_stats.unique_stats_hash = NULL;
	}
    }

  if (thread_array && *thread_array)
    {
      free_and_init (*thread_array);
    }

  if (log_Gl.trantable.all_tdes)
    {
      free_and_init (log_Gl.trantable.all_tdes);
    }

  if (log_Gl.trantable.area)
    {
      free_and_init (log_Gl.trantable.area);
    }
}

static UINT64 count_snapshots = 0;
static UINT64 count_complete = 0;
static UINT64 count_oldest = 0;

THREAD_RET_T THREAD_CALLING_CONVENTION
test_mvcc_get_snapshot (void *param)
{
  int i;
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  unsigned int local_count_snapshots = 0;
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;

  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*

  for (i = 0; i < NOPS_SNAPSHOT; i++)
    {
      if (logtb_get_mvcc_snapshot (thread_p) != NULL)
	{
	  local_count_snapshots++;
	}

      /* Invalidate snapshot */
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
      curr_mvcc_info->reset ();
    }

  ATOMIC_INC_64 (&count_snapshots, local_count_snapshots);
  fprintf (stdout, "snapshot worker thread (%p) is leaving\n", thread_p);
  fflush (stdout);

  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*

  return (THREAD_RET_T) 0;
}

THREAD_RET_T THREAD_CALLING_CONVENTION
test_new_mvcc_complete (void *param)
{
  int i;
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  unsigned int local_count_complete = 0;
  bool committed = true;
  MVCCID mvccid;

  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*

  for (i = 0; i < NOPS_COMPLPETE; i++)
    {
      mvccid = logtb_get_current_mvccid (thread_p);
      if (mvccid == MVCCID_NULL)
	{
	  abort ();
	}

      logtb_complete_mvcc (thread_p, tdes, committed);
      committed = !committed;

      /* here we may test whether bit was set */
      local_count_complete++;

      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
    }

  ATOMIC_INC_64 (&count_complete, local_count_complete);
  fprintf (stdout, "complete worker thread (%p) is leaving\n", thread_p);
  fflush (stdout);

  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*

  return (THREAD_RET_T) 0;
}

THREAD_RET_T THREAD_CALLING_CONVENTION
test_mvcc_get_oldest (void *param)
{
  int i;
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  unsigned int local_count_oldest = 0;
  MVCCID prev_oldest, curr_oldest = MVCCID_NULL;

  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*

  for (i = 0; i < NOPS_OLDEST; i++)
    {
      prev_oldest = curr_oldest;
      curr_oldest = log_Gl.mvcc_table.get_global_oldest_visible ();
      if (MVCC_ID_PRECEDES (curr_oldest, prev_oldest))
	{
	  abort ();
	  continue;
	}

      local_count_oldest++;
    }

  ATOMIC_INC_64 (&count_oldest, local_count_oldest);

  fprintf (stdout, "get_oldest thread (%p) is leaving\n", thread_p);
  fflush (stdout);

  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*

  return (THREAD_RET_T) 0;
}

static int
test_mvcc_operations (int num_snapshot_threads, int num_complete_threads, int num_oldest_mvccid_threads,
		      THREAD_ENTRY * thread_array)
{
  int i;
  int numthreads;
#define MAX_THREADS	  100
  pthread_t threads[MAX_THREADS];
  int idx_thread_entry;
  char msg[256];

  numthreads = num_snapshot_threads + num_complete_threads + num_oldest_mvccid_threads;
  sprintf (msg, "test_mvcc_operations (%d snapshot threads, %d complete threads, %d oldest threads)",
	   num_snapshot_threads, num_complete_threads, num_oldest_mvccid_threads);
  begin (msg);

  if (num_snapshot_threads < 0 || num_complete_threads < 0 || num_oldest_mvccid_threads < 0)
    {
      printf (" %s: %s\n", "FAILED", "negative number of threads not allowed");
      return ER_FAILED;
    }

  if (numthreads > MAX_THREADS)
    {
      printf (" %s: %s\n", "FAILED", "too many threads");
      return ER_FAILED;
    }

  count_snapshots = count_complete = count_oldest = 0;
  idx_thread_entry = 0;
  for (i = 0; i < num_snapshot_threads; i++, idx_thread_entry++)
    {
      if (pthread_create (&threads[idx_thread_entry], NULL, test_mvcc_get_snapshot,
			  (void *) (thread_array + idx_thread_entry)) != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread create error");
	  return ER_FAILED;
	}
    }

  for (i = 0; i < num_complete_threads; i++, idx_thread_entry++)
    {
      if (pthread_create (&threads[idx_thread_entry], NULL, test_new_mvcc_complete,
			  (void *) (thread_array + idx_thread_entry)) != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread create error");
	  return ER_FAILED;
	}
    }

  for (i = 0; i < num_oldest_mvccid_threads; i++, idx_thread_entry++)
    {
      if (pthread_create (&threads[idx_thread_entry], NULL, test_mvcc_get_oldest,
			  (void *) (thread_array + idx_thread_entry)) != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread create error");
	  return ER_FAILED;
	}
    }

  for (i = 0; i < numthreads; i++)
    {
      void *retval;

      pthread_join (threads[i], &retval);
      if (retval != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread proc error");
	  return ER_FAILED;
	}
    }

  if (count_snapshots != (UINT64) num_snapshot_threads * NOPS_SNAPSHOT)
    {
      printf ("snapshot count fail (%llu != %llu)",
	      (unsigned long long) count_snapshots, (unsigned long long) num_snapshot_threads * NOPS_SNAPSHOT);
      return ER_FAILED;
    }

  if (count_complete != (UINT64) num_complete_threads * NOPS_COMPLPETE)
    {
      printf ("complete count fail (%llu != %llu)",
	      (unsigned long long) count_complete, (unsigned long long) num_complete_threads * NOPS_COMPLPETE);
      return ER_FAILED;
    }

  if (count_oldest != (UINT64) num_oldest_mvccid_threads * NOPS_OLDEST)
    {
      printf ("oldest count fail (%llu != %llu)",
	      (unsigned long long) count_oldest, (unsigned long long) num_oldest_mvccid_threads * NOPS_OLDEST);
      return ER_FAILED;
    }

  success ();

  return NO_ERROR;
}

/* CBRD-26971: single-threaded back-to-back microbench to attribute MVCC-module
 * cost between the snapshot READ path (build_mvcc_info) and the commit path
 * (complete_mvcc), with no host/cold-start/contention noise. Uses only the
 * common public API so it compiles under both OLD (seqlock) and NEW (slot) designs.
 * Usage: unittests_snapshot micro [n_background_active] [n_reps] */
static double
micro_now_ns (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (double) ts.tv_sec * 1e9 + (double) ts.tv_nsec;
}

static int
run_micro (THREAD_ENTRY * thread_array, int n_bg, long n_reps)
{
  long i;
  double t0, t1;
  THREAD_ENTRY *thread_p = &thread_array[0];
  int tran_index = thread_p->tran_index;
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;
  bool committed = true;

  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*

  /* Make n_bg parent MVCCIDs active in other slots so snapshots collect real ids
     (sort/dedup/bitmap-span all see representative work). These stay active. */
  for (i = 1; i <= n_bg && i < 100; i++)
    {
      (void) logtb_get_current_mvccid (&thread_array[i]);
    }

  fprintf (stdout, "micro: bg_active=%d reps=%ld\n", n_bg, n_reps);

  /* ---- SNAPSHOT path (build_mvcc_info) ---- */
  for (i = 0; i < 100000; i++)	/* warmup */
    {
      (void) logtb_get_mvcc_snapshot (thread_p);
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
      curr_mvcc_info->reset ();
    }
  t0 = micro_now_ns ();
  for (i = 0; i < n_reps; i++)
    {
      (void) logtb_get_mvcc_snapshot (thread_p);
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
      curr_mvcc_info->reset ();
    }
  t1 = micro_now_ns ();
  fprintf (stdout, "SNAPSHOT  build_mvcc_info: %8.1f ns/op  (%ld reps)\n", (t1 - t0) / (double) n_reps, n_reps);

  /* ---- COMMIT path (complete_mvcc) ---- */
  for (i = 0; i < 100000; i++)	/* warmup */
    {
      (void) logtb_get_current_mvccid (thread_p);
      logtb_complete_mvcc (thread_p, tdes, committed);
      committed = !committed;
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
    }
  t0 = micro_now_ns ();
  for (i = 0; i < n_reps; i++)
    {
      (void) logtb_get_current_mvccid (thread_p);
      logtb_complete_mvcc (thread_p, tdes, committed);
      committed = !committed;
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
    }
  t1 = micro_now_ns ();
  fprintf (stdout, "COMMIT    complete_mvcc:   %8.1f ns/op  (%ld reps)\n", (t1 - t0) / (double) n_reps, n_reps);

  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return NO_ERROR;
}

/* ---- CBRD-26971 concurrent workloads: measure MVCC-module scaling in isolation
 * (no storage/WAL/network). Answers whether the ProcArray redesign actually
 * relieves commit serialization and how the SHARED-lock read path scales. ---- */
/* ============================================================================
 * CBRD-26971 MVCC micro/concurrency benchmark modes
 * ============================================================================
 * Default (no args): the original 100-config thread-matrix stress test
 * (snapshot 1-10 x complete 1-10 x oldest 1), self-checking op counts.
 *
 * Additional modes (argv[1]), all in-process (no DB/WAL/storage):
 *   micro [bg] [reps]              single-thread ns/op for snapshot + commit paths
 *   conccommit N [reps]            N concurrent committers -> aggregate ops/s
 *   conccommitw N [work] [reps]    ... with synthetic per-commit work (exposes serialization)
 *   concsnap N [reps]              N concurrent snapshotters -> aggregate ops/s
 *   concmix R W [reps]             R snapshotters + W committers
 *   mixstat R W [reps] [work]      mixed load, per-side avg latency (needs lib counters*)
 *   mixgap R W creps work [keep]   fully-overlapped mix; readers loop until committers done;
 *                                  keep=1 -> READ-COMMITTED-style invalidation (per-tran cache on)
 *   vistime k [checks] [range]     per-visibility-check cost vs k held-active txns (O(1) vs O(log k))
 *   vischeck R W [reps]            snapshot build + uncertain-range width + per-check cost under load
 *   anomaly R F pairs [spin]       inconsistent-cut detector: writer commits A then B; counts
 *                                  snapshots with "B visible AND A invisible" (must be 0)
 *   snaphit N [total]              READ-COMMITTED-style snapshot reuse (cache hit rate)
 *   snaptime N [total]             total build_mvcc_info time, fixed total snapshots (*)
 *   completetime N [total]         total complete_mvcc time, fixed total commits (*)
 *   commitwait N [reps]            commit-lock acquire wait (only meaningful on lock-based builds) (*)
 *   allocwait N [reps]             m_new_mvccid_lock acquire wait (*)
 *
 * (*) These modes read g_mvcc_* counters that are only populated when matching
 *     temporary diag_timer/counter instrumentation is added to mvcc_table.cpp
 *     (see mvcc-bottlenecks/ experiment reports for the patch pattern); on a clean
 *     build they print zeros for those fields.
 *
 * Env: MVCC_TEST_TRANS=<n> sets the tran-table (slot) size (default 100). Keep it
 * IDENTICAL across designs under comparison: the slot scan is O(#slots), so the
 * table size is itself a cost parameter. Workers use entries 1..n-2.
 * ============================================================================ */

static volatile int micro_go = 0;
/* synthetic per-commit "transaction work" (busy iterations) to model the real work that
   spaces out MVCC commits; used by the conccommitw mode to expose commit serialization. */
static long micro_work = 0;

/* CBRD-26971 diagnostic: commit-completion lock-wait accumulation. The OLD design's
   complete_mvcc (in libcubrid) references these via extern and adds its m_active_trans_mutex
   acquire-wait; the NEW lock-free design never touches them, so they stay 0 (= no serialization,
   by construction). Defined here in the executable; resolved into libcubrid at exe-link. */
std::atomic<unsigned long long> g_mvcc_lock_wait_ns (0);
std::atomic<unsigned long long> g_mvcc_lock_cnt (0);

/* CBRD-26971 diagnostic: total execution time of complete_mvcc (whole function, RAII-timed in
   libcubrid for both OLD and NEW). Sum over all calls + call count. */
std::atomic<unsigned long long> g_mvcc_complete_ns (0);
std::atomic<unsigned long long> g_mvcc_complete_cnt (0);

/* CBRD-26971 diagnostic: total execution time of build_mvcc_info (snapshot fetch, RAII-timed). */
std::atomic<unsigned long long> g_mvcc_snap_ns (0);
std::atomic<unsigned long long> g_mvcc_snap_cnt (0);

/* CBRD-26971 diagnostic: Phase-2 snapshot reuse cache hit/miss counters. */
std::atomic<unsigned long long> g_mvcc_snap_hit (0);
std::atomic<unsigned long long> g_mvcc_snap_miss (0);

/* CBRD-26971 diagnostic: seqlock retry count in build_mvcc_info (design-B only; 0 for lock-based). */
std::atomic<unsigned long long> g_mvcc_snap_retry (0);

/* CBRD-26971 diagnostic: m_new_mvccid_lock (mvccid allocation) acquire-wait — the shared #2
   serializer present in BOTH OLD and NEW (get_new_mvccid is identical). */
std::atomic<unsigned long long> g_mvcc_alloc_wait_ns (0);
std::atomic<unsigned long long> g_mvcc_alloc_cnt (0);

/* snapshot worker cache mode: 0 = full reset (defeats cache), 1 = READ-COMMITTED-style invalidate
   (valid=false but keep m_xip/cached_completion_count -> exercises Phase-2 reuse). */
static int micro_snap_keep = 0;

/* tran-table size chosen in main (workers use entries 1..tran_count-1). */
static int micro_tran_count = 100;

/* ---- mixgap mode: fully-overlapped mixed load with WIDE commit intervals ----
   Committers run a fixed creps each (micro_work spin between commits = the interval knob);
   readers loop until ALL committers finish (no early-reader-exit mixture skew). */
static volatile int mixgap_done = 0;
static std::atomic<int> mixgap_left (0);
static std::atomic<unsigned long long> g_gap_snap_ns (0);
static std::atomic<unsigned long long> g_gap_snap_cnt (0);
static std::atomic<unsigned long long> g_gap_commit_ns (0);
static std::atomic<unsigned long long> g_gap_commit_cnt (0);
static int mixgap_keep = 0;

struct mixgap_warg { THREAD_ENTRY *tp; long creps; };

static THREAD_RET_T THREAD_CALLING_CONVENTION
mixgap_committer (void *param)
{
  struct mixgap_warg *a = (struct mixgap_warg *) param;
  THREAD_ENTRY *thread_p = a->tp;
  LOG_TDES *tdes = LOG_FIND_TDES (thread_p->tran_index);
  bool committed = true;
  volatile long sink = 0;
  struct timespec t0, t1;
  unsigned long long ns = 0;
  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*
  while (micro_go == 0);
  for (long i = 0; i < a->creps; i++)
    {
      (void) logtb_get_current_mvccid (thread_p);
      clock_gettime (CLOCK_MONOTONIC, &t0);
      logtb_complete_mvcc (thread_p, tdes, committed);
      clock_gettime (CLOCK_MONOTONIC, &t1);
      ns += (unsigned long long) (t1.tv_sec - t0.tv_sec) * 1000000000ULL
	+ (unsigned long long) (t1.tv_nsec - t0.tv_nsec);
      committed = !committed;
      log_Gl.mvcc_table.reset_transaction_lowest_active (thread_p->tran_index);
      for (long j = 0; j < micro_work; j++) sink += j;	/* the interval */
    }
  g_gap_commit_ns.fetch_add (ns);
  g_gap_commit_cnt.fetch_add ((unsigned long long) a->creps);
  if (mixgap_left.fetch_sub (1) == 1)
    {
      mixgap_done = 1;
    }
  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return (THREAD_RET_T) 0;
}

static THREAD_RET_T THREAD_CALLING_CONVENTION
mixgap_reader (void *param)
{
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  int tran_index = thread_p->tran_index;
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;
  struct timespec t0, t1;
  unsigned long long ns = 0, cnt = 0;
  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*
  while (micro_go == 0);
  while (!mixgap_done)
    {
      clock_gettime (CLOCK_MONOTONIC, &t0);
      (void) logtb_get_mvcc_snapshot (thread_p);
      clock_gettime (CLOCK_MONOTONIC, &t1);
      ns += (unsigned long long) (t1.tv_sec - t0.tv_sec) * 1000000000ULL
	+ (unsigned long long) (t1.tv_nsec - t0.tv_nsec);
      cnt++;
      if (mixgap_keep)
	{
	  curr_mvcc_info->snapshot.valid = false;	/* RC-style: keep cached_valid/m_xip */
	}
      else
	{
	  log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
	  curr_mvcc_info->reset ();	/* full reset: force a real build every time */
	}
    }
  g_gap_snap_ns.fetch_add (ns);
  g_gap_snap_cnt.fetch_add (cnt);
  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return (THREAD_RET_T) 0;
}

/* ---- anomaly mode: inconsistent-cut detector ----
   One pair-writer commits A then B (strict order, same thread). Fillers keep last_completed
   above B (so B can be judged < xmax). Checkers take snapshots and flag the impossible state
   "B visible AND A invisible" (a snapshot that corresponds to no point in time, since A
   committed strictly before B). With the seqlock retry this must be 0; without it, it fires. */
static volatile int anom_done = 0;
static std::atomic<unsigned long long> g_pair_gen (0);   /* seqlock: odd = writer updating */
static MVCCID g_pair_a = MVCCID_NULL;
static MVCCID g_pair_b = MVCCID_NULL;
static std::atomic<unsigned long long> g_anom_snaps (0);
static std::atomic<unsigned long long> g_anom_hits (0);
static std::atomic<unsigned long long> g_anom_build_ns (0);
static long anom_spin = 2000;

static THREAD_RET_T THREAD_CALLING_CONVENTION
anom_filler (void *param)
{
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  LOG_TDES *tdes = LOG_FIND_TDES (thread_p->tran_index);
  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*
  while (micro_go == 0);
  while (!anom_done)
    {
      (void) logtb_get_current_mvccid (thread_p);
      logtb_complete_mvcc (thread_p, tdes, false);
      log_Gl.mvcc_table.reset_transaction_lowest_active (thread_p->tran_index);
    }
  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return (THREAD_RET_T) 0;
}

struct anom_writer_arg
{
  THREAD_ENTRY *low;		/* A holder — low slot index (scanned first) */
  THREAD_ENTRY *high;		/* B holder — high slot index (scanned last) */
  THREAD_ENTRY *scratch;
  long pairs;
};

static THREAD_RET_T THREAD_CALLING_CONVENTION
anom_writer (void *param)
{
  struct anom_writer_arg *a = (struct anom_writer_arg *) param;
  LOG_TDES *tdes_low = LOG_FIND_TDES (a->low->tran_index);
  LOG_TDES *tdes_high = LOG_FIND_TDES (a->high->tran_index);
  LOG_TDES *tdes_scr = LOG_FIND_TDES (a->scratch->tran_index);
  volatile long spin_sink = 0;
  while (micro_go == 0);
  for (long i = 0; i < a->pairs; i++)
    {
      MVCCID ida = logtb_get_current_mvccid (a->low);	/* A: low slot, allocated first */
      MVCCID idb = logtb_get_current_mvccid (a->high);	/* B: high slot, A < B */
      (void) logtb_get_current_mvccid (a->scratch);	/* C > B: complete to push xmax above B */
      logtb_complete_mvcc (a->scratch, tdes_scr, false);
      /* publish the pair (seqlock) */
      g_pair_gen.fetch_add (1, std::memory_order_acq_rel);	/* odd */
      g_pair_a = ida;
      g_pair_b = idb;
      g_pair_gen.fetch_add (1, std::memory_order_release);	/* even */
      for (long j = 0; j < anom_spin; j++) spin_sink += j;	/* let scans get past LOW slot */
      logtb_complete_mvcc (a->low, tdes_low, false);	/* ★ A commits FIRST */
      for (long j = 0; j < anom_spin / 4; j++) spin_sink += j;
      logtb_complete_mvcc (a->high, tdes_high, false);	/* ★ B commits AFTER A */
      log_Gl.mvcc_table.reset_transaction_lowest_active (a->low->tran_index);
      log_Gl.mvcc_table.reset_transaction_lowest_active (a->high->tran_index);
    }
  anom_done = 1;
  return (THREAD_RET_T) 0;
}

static THREAD_RET_T THREAD_CALLING_CONVENTION
anom_checker (void *param)
{
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  int tran_index = thread_p->tran_index;
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;
  struct timespec t0, t1;
  unsigned long long local_snaps = 0, local_hits = 0, local_ns = 0;
  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*
  while (micro_go == 0);
  while (!anom_done)
    {
      clock_gettime (CLOCK_MONOTONIC, &t0);
      MVCC_SNAPSHOT *snap = logtb_get_mvcc_snapshot (thread_p);
      clock_gettime (CLOCK_MONOTONIC, &t1);
      local_ns += (unsigned long long) (t1.tv_sec - t0.tv_sec) * 1000000000ULL
	+ (unsigned long long) (t1.tv_nsec - t0.tv_nsec);
      local_snaps++;
      /* read the (A,B) pair consistently */
      MVCCID pa, pb;
      unsigned long long g1, g2;
      do
	{
	  g1 = g_pair_gen.load (std::memory_order_acquire);
	  pa = g_pair_a;
	  pb = g_pair_b;
	  g2 = g_pair_gen.load (std::memory_order_acquire);
	}
      while ((g1 & 1) != 0 || g1 != g2);
      if (MVCCID_IS_VALID (pa) && MVCCID_IS_VALID (pb))
	{
	  MVCC_REC_HEADER h = MVCC_REC_HEADER_INITIALIZER;
	  h.mvcc_flag = OR_MVCC_FLAG_VALID_INSID;
	  h.mvcc_ins_id = pb;
	  bool vb = (snap->snapshot_fnc (thread_p, &h, snap) == SNAPSHOT_SATISFIED);
	  h.mvcc_ins_id = pa;
	  bool va = (snap->snapshot_fnc (thread_p, &h, snap) == SNAPSHOT_SATISFIED);
	  if (vb && !va)
	    {
	      local_hits++;	/* B(나중 커밋) visible + A(먼저 커밋) invisible = 존재한 적 없는 상태 */
	    }
	}
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
      curr_mvcc_info->reset ();
    }
  g_anom_snaps.fetch_add (local_snaps);
  g_anom_hits.fetch_add (local_hits);
  g_anom_build_ns.fetch_add (local_ns);
  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return (THREAD_RET_T) 0;
}

/* ---- vischeck mode: measure snapshot-build time, uncertain-range width (xmax - xmin, the
   direct cost of a lagging oldest-active), and per-check visibility-judgment latency through
   the production path (snapshot_fnc = mvcc_satisfies_snapshot), under W live committers. ---- */
#include "object_representation_constants.h"	/* OR_MVCC_FLAG_VALID_INSID */
static volatile int vis_stop = 0;
std::atomic<unsigned long long> g_vis_build_ns (0);
std::atomic<unsigned long long> g_vis_build_cnt (0);
std::atomic<unsigned long long> g_vis_width_sum (0);
std::atomic<unsigned long long> g_vis_recent_ns (0);
std::atomic<unsigned long long> g_vis_old_ns (0);
std::atomic<unsigned long long> g_vis_check_cnt (0);
static volatile unsigned long long g_vis_sink = 0;

static THREAD_RET_T THREAD_CALLING_CONVENTION
vis_committer (void *param)
{
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  int tran_index = thread_p->tran_index;
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  bool committed = true;
  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*
  while (micro_go == 0)
    ;
  while (!vis_stop)
    {
      (void) logtb_get_current_mvccid (thread_p);
      logtb_complete_mvcc (thread_p, tdes, committed);
      committed = !committed;
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
    }
  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return (THREAD_RET_T) 0;
}

struct vis_checker_arg
{
  THREAD_ENTRY *thread_p;
  long reps;
};

static THREAD_RET_T THREAD_CALLING_CONVENTION
vis_checker (void *param)
{
  struct vis_checker_arg *a = (struct vis_checker_arg *) param;
  THREAD_ENTRY *thread_p = a->thread_p;
  int tran_index = thread_p->tran_index;
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;
  const int K = 512;		/* checks per band per snapshot */
  struct timespec t0, t1;
  unsigned long long sink = 0;
  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*
  while (micro_go == 0)
    ;
  for (long i = 0; i < a->reps; i++)
    {
      clock_gettime (CLOCK_MONOTONIC, &t0);
      MVCC_SNAPSHOT *snap = logtb_get_mvcc_snapshot (thread_p);
      clock_gettime (CLOCK_MONOTONIC, &t1);
      g_vis_build_ns.fetch_add ((unsigned long long) (t1.tv_sec - t0.tv_sec) * 1000000000ULL
				+ (unsigned long long) (t1.tv_nsec - t0.tv_nsec), std::memory_order_relaxed);
      g_vis_build_cnt.fetch_add (1, std::memory_order_relaxed);
      g_vis_width_sum.fetch_add (snap->highest_completed_mvccid - snap->lowest_active_mvccid,
				 std::memory_order_relaxed);

      MVCCID frontier = log_Gl.hdr.mvcc_next_id;
      MVCC_REC_HEADER h = MVCC_REC_HEADER_INITIALIZER;
      h.mvcc_flag = OR_MVCC_FLAG_VALID_INSID;

      /* recent band: ids just below the allocation frontier (mostly just-committed) — the band
         a lagging xmin pushes from the O(1) fast path into binary_search */
      clock_gettime (CLOCK_MONOTONIC, &t0);
      for (int j = 0; j < K; j++)
	{
	  MVCCID id = (frontier > (MVCCID) (j + 2)) ? frontier - 2 - j : MVCCID_FIRST;
	  h.mvcc_ins_id = id;
	  sink += (unsigned long long) snap->snapshot_fnc (thread_p, &h, snap);
	}
      clock_gettime (CLOCK_MONOTONIC, &t1);
      g_vis_recent_ns.fetch_add ((unsigned long long) (t1.tv_sec - t0.tv_sec) * 1000000000ULL
				 + (unsigned long long) (t1.tv_nsec - t0.tv_nsec), std::memory_order_relaxed);

      /* old band: ids far below xmin — fast path in both designs (control) */
      MVCCID old_base = (frontier > 2000000) ? frontier / 2 : MVCCID_FIRST;
      clock_gettime (CLOCK_MONOTONIC, &t0);
      for (int j = 0; j < K; j++)
	{
	  h.mvcc_ins_id = (old_base > (MVCCID) (j + 1)) ? old_base - j : MVCCID_FIRST;
	  sink += (unsigned long long) snap->snapshot_fnc (thread_p, &h, snap);
	}
      clock_gettime (CLOCK_MONOTONIC, &t1);
      g_vis_old_ns.fetch_add ((unsigned long long) (t1.tv_sec - t0.tv_sec) * 1000000000ULL
			      + (unsigned long long) (t1.tv_nsec - t0.tv_nsec), std::memory_order_relaxed);
      g_vis_check_cnt.fetch_add (K, std::memory_order_relaxed);

      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
      curr_mvcc_info->reset ();
    }
  g_vis_sink += sink;
  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return (THREAD_RET_T) 0;
}

struct micro_worker_arg
{
  THREAD_ENTRY *thread_p;
  long reps;
  int op;			/* 0 = snapshot, 1 = commit */
  UINT64 done;			/* ops actually performed */
};

static THREAD_RET_T THREAD_CALLING_CONVENTION
micro_worker (void *param)
{
  struct micro_worker_arg *a = (struct micro_worker_arg *) param;
  THREAD_ENTRY *thread_p = a->thread_p;
  int tran_index = thread_p->tran_index;
  LOG_TDES *tdes = LOG_FIND_TDES (tran_index);
  MVCC_INFO *curr_mvcc_info = &tdes->mvccinfo;
  bool committed = true;
  long i;

  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*

  while (micro_go == 0)
    {				/* spin barrier so all threads start together */
      ;
    }

  if (a->op == 0)
    {
      for (i = 0; i < a->reps; i++)
	{
	  (void) logtb_get_mvcc_snapshot (thread_p);
	  if (micro_snap_keep)
	    {
	      /* READ-COMMITTED-style per-statement invalidate: drop valid but keep m_xip/cached
	         so the next build can reuse (exercises Phase-2). */
	      curr_mvcc_info->snapshot.valid = false;
	    }
	  else
	    {
	      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
	      curr_mvcc_info->reset ();
	    }
	}
    }
  else
    {
      volatile unsigned long sink = 0;
      for (i = 0; i < a->reps; i++)
	{
	  (void) logtb_get_current_mvccid (thread_p);
	  logtb_complete_mvcc (thread_p, tdes, committed);
	  committed = !committed;
	  log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
	  /* synthetic transaction work between commits (spaces out MVCC coordination) */
	  for (long w = 0; w < micro_work; w++)
	    {
	      sink += (unsigned long) w;
	    }
	}
    }
  a->done = (UINT64) a->reps;

  // *INDENT-OFF*
  cubthread::clear_thread_local_entry ();
  // *INDENT-ON*
  return (THREAD_RET_T) 0;
}

/* Run n_snap snapshot threads + n_commit commit threads concurrently, each doing
 * `reps` ops, and report aggregate throughput. Worker i uses thread_array[i+1]
 * (index 0 reserved; each gets its own tran_index/slot). */
static int
run_conc (THREAD_ENTRY * thread_array, int n_snap, int n_commit, long reps)
{
  int n = n_snap + n_commit;
  int i;
  double t0, t1, secs;
  pthread_t *tids;
  struct micro_worker_arg *args;
  UINT64 total = 0;

  if (n <= 0 || n >= micro_tran_count - 1)
    {
      fprintf (stdout, "conc: bad thread count %d (1..%d; raise MVCC_TEST_TRANS)\n", n, micro_tran_count - 2);
      return ER_FAILED;
    }
  tids = (pthread_t *) malloc (sizeof (pthread_t) * n);
  args = (struct micro_worker_arg *) malloc (sizeof (struct micro_worker_arg) * n);

  micro_go = 0;
  for (i = 0; i < n; i++)
    {
      args[i].thread_p = &thread_array[i + 1];	/* distinct tran_index per worker */
      args[i].reps = reps;
      args[i].op = (i < n_snap) ? 0 : 1;
      args[i].done = 0;
      if (pthread_create (&tids[i], NULL, micro_worker, &args[i]) != 0)
	{
	  /* cgroup pids limit etc. — release the already-created workers and bail out cleanly */
	  fprintf (stdout, "conc: pthread_create failed at worker %d (pids limit?) — aborting run\n", i);
	  micro_go = 1;
	  for (int j = 0; j < i; j++)
	    {
	      pthread_join (tids[j], NULL);
	    }
	  free (tids);
	  free (args);
	  return ER_FAILED;
	}
    }

  t0 = micro_now_ns ();
  micro_go = 1;			/* release the barrier */
  for (i = 0; i < n; i++)
    {
      pthread_join (tids[i], NULL);
      total += args[i].done;
    }
  t1 = micro_now_ns ();
  secs = (t1 - t0) / 1e9;

  fprintf (stdout,
	   "conc snap=%d commit=%d | %.0f total ops in %.3f s | %8.0f ops/s aggregate | %8.0f ops/s per-thread\n",
	   n_snap, n_commit, (double) total, secs, (double) total / secs, (double) total / secs / n);
  free (tids);
  free (args);
  return NO_ERROR;
}

/* program entry */
int
main (int argc, char **argv)
{
#define MAX_SNAPSHOT_THREADS 10
#define MAX_COMPLETE_THREADS 10
#define MAX_OLDEST_THREADS 1

  int num_snapshot_threads, num_complete_threads, num_oldest_threads;
  THREAD_ENTRY *thread_array = NULL;

  /* tran-table size (= slot-array size). Overridable via MVCC_TEST_TRANS for high-thread-count
     experiments; keep it IDENTICAL across designs under comparison (NEW's slot scan is O(#slots),
     so the table size is itself a cost parameter). */
  int tran_count = 100;
  {
    const char *env = getenv ("MVCC_TEST_TRANS");
    if (env != NULL && atoi (env) > 2)
      {
	tran_count = atoi (env);
      }
  }
  micro_tran_count = tran_count;
  logtb_initialize_mvcc_testing (tran_count, &thread_array);

  if (argc > 1 && strcmp (argv[1], "micro") == 0)
    {
      int n_bg = (argc > 2) ? atoi (argv[2]) : 8;
      long n_reps = (argc > 3) ? atol (argv[3]) : 2000000L;
      run_micro (thread_array, n_bg, n_reps);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "conccommit") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      long reps = (argc > 3) ? atol (argv[3]) : 1000000L;
      run_conc (thread_array, 0, nt, reps);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "mixgap") == 0)
    {
      int nr = (argc > 2) ? atoi (argv[2]) : 16;
      int nw = (argc > 3) ? atoi (argv[3]) : 16;
      long creps = (argc > 4) ? atol (argv[4]) : 2000L;
      micro_work = (argc > 5) ? atol (argv[5]) : 0;
      mixgap_keep = (argc > 6) ? atoi (argv[6]) : 0;
      if (nr + nw + 2 > micro_tran_count)
	{
	  fprintf (stdout, "mixgap: too many workers for table\n");
	  logtb_finalize_mvcc_testing (&thread_array);
	  return ER_FAILED;
	}
      pthread_t *rt = (pthread_t *) malloc (sizeof (pthread_t) * nr);
      pthread_t *wt2 = (pthread_t *) malloc (sizeof (pthread_t) * nw);
      struct mixgap_warg *wargs = (struct mixgap_warg *) malloc (sizeof (struct mixgap_warg) * nw);
      int i, spawned_r = 0, spawned_w = 0;
      struct timespec w0, w1;
      mixgap_done = 0;
      micro_go = 0;
      mixgap_left.store (nw);
      g_gap_snap_ns.store (0); g_gap_snap_cnt.store (0);
      g_gap_commit_ns.store (0); g_gap_commit_cnt.store (0);
      g_mvcc_snap_retry.store (0); g_mvcc_snap_hit.store (0); g_mvcc_snap_miss.store (0);
      for (i = 0; i < nw; i++)
	{
	  wargs[i].tp = &thread_array[1 + i];
	  wargs[i].creps = creps;
	  if (pthread_create (&wt2[i], NULL, mixgap_committer, &wargs[i]) != 0) break;
	  spawned_w++;
	}
      for (i = 0; i < nr; i++)
	{
	  if (pthread_create (&rt[i], NULL, mixgap_reader, &thread_array[1 + nw + i]) != 0) break;
	  spawned_r++;
	}
      if (spawned_w < nw || spawned_r < nr)
	{
	  fprintf (stdout, "mixgap: spawn failed (w=%d/%d r=%d/%d) — pids limit\n", spawned_w, nw, spawned_r, nr);
	  mixgap_left.store (spawned_w > 0 ? spawned_w : 1);
	  if (spawned_w == 0) mixgap_done = 1;
	}
      clock_gettime (CLOCK_MONOTONIC, &w0);
      micro_go = 1;
      for (i = 0; i < spawned_w; i++) pthread_join (wt2[i], NULL);
      for (i = 0; i < spawned_r; i++) pthread_join (rt[i], NULL);
      clock_gettime (CLOCK_MONOTONIC, &w1);
      double wall = (double) (w1.tv_sec - w0.tv_sec) + (double) (w1.tv_nsec - w0.tv_nsec) / 1e9;
      unsigned long long sc = g_gap_snap_cnt.load (), cc = g_gap_commit_cnt.load ();
      unsigned long long rr = g_mvcc_snap_retry.load (), ph = g_mvcc_snap_hit.load ();
      fprintf (stdout,
	       "mixgap R=%d W=%d work=%ld keep=%d | wall=%.2fs compl/s=%.0f | snap calls=%llu avg_ns=%.1f retry/call=%.4f hit%%=%.1f | commit avg_ns=%.1f\n",
	       spawned_r, spawned_w, micro_work, mixgap_keep, wall, wall > 0 ? (double) cc / wall : 0.0,
	       sc, sc ? (double) g_gap_snap_ns.load () / sc : 0.0, sc ? (double) rr / sc : 0.0,
	       sc ? 100.0 * (double) ph / (double) sc : 0.0, cc ? (double) g_gap_commit_ns.load () / cc : 0.0);
      free (rt); free (wt2); free (wargs);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "anomaly") == 0)
    {
      int nr = (argc > 2) ? atoi (argv[2]) : 8;	/* checkers */
      int nf = (argc > 3) ? atoi (argv[3]) : 4;	/* fillers */
      long pairs = (argc > 4) ? atol (argv[4]) : 100000L;
      anom_spin = (argc > 5) ? atol (argv[5]) : 2000L;
      pthread_t wt, ft[64], ct[128];
      struct anom_writer_arg wa;
      int i;
      anom_done = 0;
      micro_go = 0;
      g_anom_snaps.store (0);
      g_anom_hits.store (0);
      g_anom_build_ns.store (0);
      wa.low = &thread_array[3];
      wa.high = &thread_array[micro_tran_count - 3];
      wa.scratch = &thread_array[4];
      wa.pairs = pairs;
      for (i = 0; i < nf; i++)
	{
	  pthread_create (&ft[i], NULL, anom_filler, &thread_array[6 + i]);
	}
      for (i = 0; i < nr; i++)
	{
	  pthread_create (&ct[i], NULL, anom_checker, &thread_array[6 + nf + i]);
	}
      pthread_create (&wt, NULL, anom_writer, &wa);
      micro_go = 1;
      pthread_join (wt, NULL);
      for (i = 0; i < nf; i++) pthread_join (ft[i], NULL);
      for (i = 0; i < nr; i++) pthread_join (ct[i], NULL);
      unsigned long long sn = g_anom_snaps.load ();
      unsigned long long an = g_anom_hits.load ();
      fprintf (stdout, "anomaly R=%d F=%d pairs=%ld | snapshots=%llu anomalies=%llu (%.5f%%) | build avg_ns=%.1f\n",
	       nr, nf, pairs, sn, an, sn ? 100.0 * (double) an / (double) sn : 0.0,
	       sn ? (double) g_anom_build_ns.load () / (double) sn : 0.0);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "vistime") == 0)
    {
      /* per-check visibility-judgment microbench: one PRE-BUILT snapshot, k held-active
         transactions spread over a ~20K-id window (< OLD 32K bitmap window so OLD stays on
         the O(1) bit-test path), then N random middle-range checks through the production
         path (snapshot_fnc). Single-threaded: no contention/retry/preemption — pure per-call. */
      int k = (argc > 2) ? atoi (argv[2]) : 64;
      long checks = (argc > 3) ? atol (argv[3]) : 20000000L;
      long M = (argc > 4) ? atol (argv[4]) : 20000L;
      if (k + 4 > micro_tran_count)
	{
	  fprintf (stdout, "vistime: k=%d too large for table=%d (raise MVCC_TEST_TRANS)\n", k, micro_tran_count);
	  logtb_finalize_mvcc_testing (&thread_array);
	  return ER_FAILED;
	}
      THREAD_ENTRY *checker = &thread_array[1];
      THREAD_ENTRY *scratch = &thread_array[2];
      /* holders: entries[3 .. 3+k) — an "active transaction" is just published slot state */
      // *INDENT-OFF*
      cubthread::set_thread_local_entry (*checker);
      // *INDENT-ON*
      long stride = (k > 0 && (M / k) > 0) ? (M / k) : 1;
      int held = 0;
      for (long i = 0; i < M; i++)
	{
	  if (held < k && (i % stride) == 0)
	    {
	      (void) logtb_get_current_mvccid (&thread_array[3 + held]);	/* hold: never completed */
	      held++;
	    }
	  else
	    {
	      (void) logtb_get_current_mvccid (scratch);
	      /* committed=false: skips unique-stats (no thread-local needed); slot-clear/bit-set
	         and xmax advance are identical to commit for snapshot purposes */
	      logtb_complete_mvcc (scratch, LOG_FIND_TDES (scratch->tran_index), false);
	    }
	}
      MVCC_SNAPSHOT *snap = logtb_get_mvcc_snapshot (checker);
      MVCCID lo = snap->lowest_active_mvccid;
      unsigned long long span = (unsigned long long) (snap->highest_completed_mvccid - lo);
      MVCC_REC_HEADER h = MVCC_REC_HEADER_INITIALIZER;
      h.mvcc_flag = OR_MVCC_FLAG_VALID_INSID;
      unsigned long long lcg = 88172645463325252ULL;
      unsigned long long sink = 0;
      struct timespec t0, t1;
      clock_gettime (CLOCK_MONOTONIC, &t0);
      for (long i = 0; i < checks; i++)
	{
	  lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;	/* random access: no prefetch/branch pattern */
	  h.mvcc_ins_id = lo + (MVCCID) ((lcg >> 33) % span);	/* always the uncertain middle range */
	  sink += (unsigned long long) snap->snapshot_fnc (checker, &h, snap);
	}
      clock_gettime (CLOCK_MONOTONIC, &t1);
      double ns = (double) (t1.tv_sec - t0.tv_sec) * 1e9 + (double) (t1.tv_nsec - t0.tv_nsec);
      g_vis_sink += sink;
      fprintf (stdout, "vistime k=%d held=%d span=%llu checks=%ld avg_ns=%.2f\n",
	       k, held, span, checks, ns / (double) checks);
      // *INDENT-OFF*
      cubthread::clear_thread_local_entry ();
      // *INDENT-ON*
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "vischeck") == 0)
    {
      int nr = (argc > 2) ? atoi (argv[2]) : 4;	/* checkers */
      int nw = (argc > 3) ? atoi (argv[3]) : 16;	/* live committers */
      long reps = (argc > 4) ? atol (argv[4]) : 2000L;	/* snapshots per checker */
      pthread_t ct[128], wt[512];
      struct vis_checker_arg cargs[128];
      int i;
      vis_stop = 0;
      micro_go = 0;
      for (i = 0; i < nw; i++)
	{
	  pthread_create (&wt[i], NULL, vis_committer, &thread_array[1 + i]);
	}
      for (i = 0; i < nr; i++)
	{
	  cargs[i].thread_p = &thread_array[1 + nw + i];
	  cargs[i].reps = reps;
	  pthread_create (&ct[i], NULL, vis_checker, &cargs[i]);
	}
      micro_go = 1;
      for (i = 0; i < nr; i++)
	{
	  pthread_join (ct[i], NULL);
	}
      vis_stop = 1;
      for (i = 0; i < nw; i++)
	{
	  pthread_join (wt[i], NULL);
	}
      unsigned long long bc = g_vis_build_cnt.load ();
      unsigned long long cc = g_vis_check_cnt.load ();
      fprintf (stdout,
	       "vischeck R=%d W=%d | build avg_ns=%.1f | width avg=%.1f ids | check recent=%.2f ns old=%.2f ns\n",
	       nr, nw, bc ? (double) g_vis_build_ns.load () / bc : 0.0,
	       bc ? (double) g_vis_width_sum.load () / bc : 0.0,
	       cc ? (double) g_vis_recent_ns.load () / cc : 0.0, cc ? (double) g_vis_old_ns.load () / cc : 0.0);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "mixstat") == 0)
    {
      /* R readers (full-reset snapshots -> always scan) + W committers, per-side latency + retries */
      int nr = (argc > 2) ? atoi (argv[2]) : 4;
      int nw = (argc > 3) ? atoi (argv[3]) : 8;
      long reps = (argc > 4) ? atol (argv[4]) : 200000L;
      micro_work = (argc > 5) ? atol (argv[5]) : 0;	/* per-commit synthetic work (spaces out completions) */
      g_mvcc_snap_ns.store (0);
      g_mvcc_snap_cnt.store (0);
      g_mvcc_snap_retry.store (0);
      g_mvcc_snap_hit.store (0);
      g_mvcc_snap_miss.store (0);
      g_mvcc_complete_ns.store (0);
      g_mvcc_complete_cnt.store (0);
      run_conc (thread_array, nr, nw, reps);
      unsigned long long sc = g_mvcc_snap_cnt.load ();
      unsigned long long sn = g_mvcc_snap_ns.load ();
      unsigned long long sr = g_mvcc_snap_retry.load ();
      unsigned long long cc = g_mvcc_complete_cnt.load ();
      unsigned long long cn = g_mvcc_complete_ns.load ();
      fprintf (stdout,
	       "mixstat R=%d W=%d | snap: calls=%llu avg_ns=%.1f retries=%llu retry/call=%.3f hit=%llu miss=%llu | commit: calls=%llu avg_ns=%.1f\n",
	       nr, nw, sc, sc ? (double) sn / sc : 0.0, sr, sc ? (double) sr / sc : 0.0,
	       g_mvcc_snap_hit.load (), g_mvcc_snap_miss.load (),
	       cc, cc ? (double) cn / cc : 0.0);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "allocwait") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      long reps = (argc > 3) ? atol (argv[3]) : 300000L;
      g_mvcc_alloc_wait_ns.store (0);
      g_mvcc_alloc_cnt.store (0);
      run_conc (thread_array, 0, nt, reps);	/* commit workers -> each iter allocates an mvccid */
      unsigned long long ns = g_mvcc_alloc_wait_ns.load ();
      unsigned long long cnt = g_mvcc_alloc_cnt.load ();
      fprintf (stdout, "allocwait threads=%d allocs=%llu avg_wait_ns=%.1f\n",
	       nt, cnt, cnt ? (double) ns / (double) cnt : 0.0);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "snaphit") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      long total = (argc > 3) ? atol (argv[3]) : 4000000L;
      long per = total / nt;
      if (per < 1)
	{
	  per = 1;
	}
      micro_snap_keep = 1;	/* READ-COMMITTED-style invalidate (keep cache) */
      /* warm-up: one real commit so m_completion_count > 0 (a real DB always has prior commits;
         the reuse guard needs a nonzero cached count to engage). Uses an idle slot. */
      {
	THREAD_ENTRY *tb = &thread_array[micro_tran_count - 2];	/* high idle entry (tran_index = count-1, in bounds) */
	// *INDENT-OFF*
	cubthread::set_thread_local_entry (*tb);
	// *INDENT-ON*
	(void) logtb_get_current_mvccid (tb);
	logtb_complete_mvcc (tb, LOG_FIND_TDES (tb->tran_index), true);
	// *INDENT-OFF*
	cubthread::clear_thread_local_entry ();
	// *INDENT-ON*
      }
      g_mvcc_snap_ns.store (0);
      g_mvcc_snap_cnt.store (0);
      g_mvcc_snap_hit.store (0);
      g_mvcc_snap_miss.store (0);
      run_conc (thread_array, nt, 0, per);
      micro_snap_keep = 0;
      unsigned long long ns = g_mvcc_snap_ns.load ();
      unsigned long long cnt = g_mvcc_snap_cnt.load ();
      unsigned long long hit = g_mvcc_snap_hit.load ();
      unsigned long long miss = g_mvcc_snap_miss.load ();
      fprintf (stdout, "snaphit threads=%d calls=%llu total_ms=%.1f avg_ns=%.1f hit=%llu miss=%llu\n",
	       nt, cnt, (double) ns / 1e6, cnt ? (double) ns / (double) cnt : 0.0, hit, miss);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "snaptime") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      long total = (argc > 3) ? atol (argv[3]) : 4000000L;	/* fixed TOTAL snapshots, split across threads */
      long per = total / nt;
      if (per < 1)
	{
	  per = 1;
	}
      g_mvcc_snap_ns.store (0);
      g_mvcc_snap_cnt.store (0);
      run_conc (thread_array, nt, 0, per);	/* nt snapshot workers, 0 committers */
      unsigned long long ns = g_mvcc_snap_ns.load ();
      unsigned long long cnt = g_mvcc_snap_cnt.load ();
      fprintf (stdout, "snaptime threads=%d calls=%llu total_ms=%.1f avg_ns=%.1f\n",
	       nt, cnt, (double) ns / 1e6, cnt ? (double) ns / (double) cnt : 0.0);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "completetime") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      long total = (argc > 3) ? atol (argv[3]) : 2000000L;	/* fixed TOTAL commits, split across threads */
      long per = total / nt;
      if (per < 1)
	{
	  per = 1;
	}
      g_mvcc_complete_ns.store (0);
      g_mvcc_complete_cnt.store (0);
      run_conc (thread_array, 0, nt, per);
      unsigned long long ns = g_mvcc_complete_ns.load ();
      unsigned long long cnt = g_mvcc_complete_cnt.load ();
      fprintf (stdout, "completetime threads=%d calls=%llu total_ms=%.1f avg_ns=%.1f\n",
	       nt, cnt, (double) ns / 1e6, cnt ? (double) ns / (double) cnt : 0.0);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "commitwait") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      long reps = (argc > 3) ? atol (argv[3]) : 300000L;
      g_mvcc_lock_wait_ns.store (0);
      g_mvcc_lock_cnt.store (0);
      run_conc (thread_array, 0, nt, reps);
      unsigned long long cnt = g_mvcc_lock_cnt.load ();
      unsigned long long ns = g_mvcc_lock_wait_ns.load ();
      fprintf (stdout, "commitwait threads=%d lock_acquires=%llu avg_wait_ns=%.1f\n",
	       nt, cnt, cnt ? (double) ns / (double) cnt : 0.0);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "conccommitw") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      micro_work = (argc > 3) ? atol (argv[3]) : 2000;
      long reps = (argc > 4) ? atol (argv[4]) : 200000L;
      run_conc (thread_array, 0, nt, reps);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "concsnap") == 0)
    {
      int nt = (argc > 2) ? atoi (argv[2]) : 4;
      long reps = (argc > 3) ? atol (argv[3]) : 1000000L;
      run_conc (thread_array, nt, 0, reps);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }
  if (argc > 1 && strcmp (argv[1], "concmix") == 0)
    {
      int nr = (argc > 2) ? atoi (argv[2]) : 4;
      int nw = (argc > 3) ? atoi (argv[3]) : 2;
      long reps = (argc > 4) ? atol (argv[4]) : 1000000L;
      run_conc (thread_array, nr, nw, reps);
      logtb_finalize_mvcc_testing (&thread_array);
      return 0;
    }

  for (num_oldest_threads = 1; num_oldest_threads <= MAX_OLDEST_THREADS; num_oldest_threads++)
    {
      for (num_complete_threads = 1; num_complete_threads <= MAX_COMPLETE_THREADS; num_complete_threads++)
	{
	  for (num_snapshot_threads = 1; num_snapshot_threads <= MAX_SNAPSHOT_THREADS; num_snapshot_threads++)
	    {
	      if (test_mvcc_operations (num_snapshot_threads, num_complete_threads, num_oldest_threads,
					thread_array) != NO_ERROR)
		{
		  goto fail;
		}
	    }
	}
    }

  logtb_finalize_mvcc_testing (&thread_array);
  return 0;

fail:
  logtb_finalize_mvcc_testing (&thread_array);
  printf ("Unit tests failed!\n");
  return ER_FAILED;

#undef MAX_SNAPSHOT_THREADS
#undef MAX_COMPLETE_THREADS
#undef MAX_OLDEST_THREADS
}
