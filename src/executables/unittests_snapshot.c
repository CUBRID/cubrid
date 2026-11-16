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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <log_impl.h>
#include <sys/time.h>
#include <time.h>

#include "mvcc.h"
#include "object_representation_constants.h"

#include <atomic>

#define strlen(s1) ((int) strlen(s1))

#define NOPS_SNAPSHOT   1000000
#define NOPS_COMPLPETE  1000000
#define NOPS_OLDEST     2000000

/* operations per thread; main () may lower them for a single benchmark run */
static int nops_snapshot = NOPS_SNAPSHOT;
static int nops_complete = NOPS_COMPLPETE;
static int nops_oldest = NOPS_OLDEST;

/* thread entries and transaction descriptors created once; thread entry i works under tran index i + 1, and order
 * writer w borrows tran index TEST_MAX_THREADS + 1 + w as its second transaction */
#define TEST_MAX_THREADS 128
/* order writers; each one also borrows one extra tran index after the thread entries' indices */
#define TEST_ORDER_WRITERS 4
#define TEST_NUM_TRAN_INDICES (TEST_MAX_THREADS + 1 + TEST_ORDER_WRITERS)
#define TEST_ORDER_SECOND_TRAN_INDEX(writer) (TEST_MAX_THREADS + 1 + (writer))
/* upper bound of operations per thread given on the command line */
#define TEST_MAX_OPS 10000000

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

static int
test_parse_count (const char *arg, int min_value, int max_value, int *value)
{
  char *end = NULL;
  long parsed;

  errno = 0;
  parsed = strtol (arg, &end, 10);
  if (errno != 0 || end == arg || *end != '\0' || parsed < min_value || parsed > max_value)
    {
      return ER_FAILED;
    }
  *value = (int) parsed;
  return NO_ERROR;
}

static UINT64
test_now_ns (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (UINT64) ts.tv_sec * 1000000000ULL + (UINT64) ts.tv_nsec;
}

/*
 * Order oracle (CBRD-26971).  Each of TEST_ORDER_WRITERS writer threads owns two transactions.  In each round it
 * takes two MVCCIDs, publishes the pair, and commits them one after the other.  Snapshot threads check every snapshot
 * they build:
 *  - real time: an id whose commit returned before the snapshot started is visible;
 *  - order: if a pair's second commit is visible, its first commit is visible too (the last two pairs are checked:
 *    the last one may be between its commits, the one before it is done);
 *  - vacuum bound: the global oldest visible MVCCID never passes the snapshot's lowest active MVCCID.
 * Every writer keeps its own pairs.  The writers keep going while snapshot threads run (at least one round each, at
 * most nops_snapshot rounds each).
 * The current table passes trivially; a table that lets commits and snapshots overlap must keep passing.
 */
typedef struct test_order_pair TEST_ORDER_PAIR;
struct test_order_pair
{
  MVCCID first_completed;
  MVCCID second_completed;
};

static TEST_ORDER_PAIR *order_pairs[TEST_ORDER_WRITERS];
static int nops_order = 0;
static int order_writer_first_entry = 0;
// *INDENT-OFF*
static std::atomic<int> snapshot_threads_running (0);
static std::atomic<int> order_pairs_published[TEST_ORDER_WRITERS];
static std::atomic<MVCCID> order_last_completed[TEST_ORDER_WRITERS];
// *INDENT-ON*

static UINT64 count_order_rounds = 0;
static UINT64 count_checks = 0;
static UINT64 count_stale = 0;
static UINT64 count_order_violations = 0;
static UINT64 count_vacuum_violations = 0;
static UINT64 total_snapshot_ns = 0;
static UINT64 total_complete_ns = 0;

static bool
test_is_committed_in_snapshot (THREAD_ENTRY * thread_p, MVCCID mvccid, MVCC_SNAPSHOT * snapshot)
{
  MVCC_REC_HEADER rec_header = MVCC_REC_HEADER_INITIALIZER;

  /* a record inserted by mvccid is visible exactly when mvccid is committed for the snapshot */
  rec_header.mvcc_flag = OR_MVCC_FLAG_VALID_INSID;
  rec_header.mvcc_ins_id = mvccid;
  return mvcc_satisfies_snapshot (thread_p, &rec_header, snapshot) == SNAPSHOT_SATISFIED;
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

  UINT64 local_ns = 0, local_checks = 0, local_stale = 0, local_order = 0, local_vacuum = 0;
  MVCC_SNAPSHOT *snapshot;
  MVCCID done_before[TEST_ORDER_WRITERS], oldest_visible;
  int published[TEST_ORDER_WRITERS], k, w;
  TEST_ORDER_PAIR *pair;
  UINT64 t0;

  for (i = 0; i < nops_snapshot; i++)
    {
      /* everything read here happened before the snapshot started */
      for (w = 0; w < TEST_ORDER_WRITERS; w++)
	{
	  done_before[w] = order_last_completed[w].load (std::memory_order_acquire);
	  published[w] = order_pairs_published[w].load (std::memory_order_acquire);
	}

      t0 = test_now_ns ();
      snapshot = logtb_get_mvcc_snapshot (thread_p);
      local_ns += test_now_ns () - t0;
      if (snapshot != NULL)
	{
	  local_count_snapshots++;

	  local_checks++;
	  for (w = 0; w < TEST_ORDER_WRITERS; w++)
	    {
	      if (MVCCID_IS_VALID (done_before[w])
		  && !test_is_committed_in_snapshot (thread_p, done_before[w], snapshot))
		{
		  local_stale++;
		}
	      for (k = published[w] - 1; k >= 0 && k >= published[w] - 2; k--)
		{
		  pair = &order_pairs[w][k];
		  if (test_is_committed_in_snapshot (thread_p, pair->second_completed, snapshot)
		      && !test_is_committed_in_snapshot (thread_p, pair->first_completed, snapshot))
		    {
		      local_order++;
		    }
		}
	    }
	  /* this transaction's lowest active MVCCID is published, so vacuum must stay at or below it */
	  oldest_visible = log_Gl.mvcc_table.get_global_oldest_visible ();
	  if (MVCC_ID_PRECEDES (snapshot->lowest_active_mvccid, oldest_visible))
	    {
	      local_vacuum++;
	    }
	}

      /* Invalidate snapshot */
      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
      curr_mvcc_info->reset ();
    }

  ATOMIC_INC_64 (&count_snapshots, local_count_snapshots);
  ATOMIC_INC_64 (&total_snapshot_ns, local_ns);
  ATOMIC_INC_64 (&count_checks, local_checks);
  ATOMIC_INC_64 (&count_stale, local_stale);
  ATOMIC_INC_64 (&count_order_violations, local_order);
  ATOMIC_INC_64 (&count_vacuum_violations, local_vacuum);
  snapshot_threads_running--;
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

  UINT64 local_ns = 0;
  UINT64 t0;

  for (i = 0; i < nops_complete; i++)
    {
      mvccid = logtb_get_current_mvccid (thread_p);
      if (mvccid == MVCCID_NULL)
	{
	  abort ();
	}

      t0 = test_now_ns ();
      logtb_complete_mvcc (thread_p, tdes, committed);
      local_ns += test_now_ns () - t0;
      committed = !committed;

      /* here we may test whether bit was set */
      local_count_complete++;

      log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
    }

  ATOMIC_INC_64 (&count_complete, local_count_complete);
  ATOMIC_INC_64 (&total_complete_ns, local_ns);
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

  for (i = 0; i < nops_oldest; i++)
    {
      prev_oldest = curr_oldest;
      /* compute it the way vacuum does, so snapshot threads can check the bound against a moving value */
      curr_oldest = log_Gl.mvcc_table.update_global_oldest_visible ();
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

static void
test_order_commit (THREAD_ENTRY * thread_p, int tran_index)
{
  thread_p->tran_index = tran_index;
  logtb_complete_mvcc (thread_p, LOG_FIND_TDES (tran_index), true);
  log_Gl.mvcc_table.reset_transaction_lowest_active (tran_index);
}

THREAD_RET_T THREAD_CALLING_CONVENTION
test_mvcc_order_writer (void *param)
{
  int i;
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) param;
  int own_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  int writer = thread_p->index - order_writer_first_entry;
  int second_tran_index = TEST_ORDER_SECOND_TRAN_INDEX (writer);
  int tran_a, tran_b, tran_tmp;
  MVCCID id_a, id_b, id_tmp;
  unsigned int local_rounds = 0;

  // *INDENT-OFF*
  cubthread::set_thread_local_entry (*thread_p);
  // *INDENT-ON*

  for (i = 0; i < nops_order && (i == 0 || snapshot_threads_running.load () > 0); i++)
    {
      thread_p->tran_index = own_tran_index;
      id_a = logtb_get_current_mvccid (thread_p);
      thread_p->tran_index = second_tran_index;
      id_b = logtb_get_current_mvccid (thread_p);
      if (id_a == MVCCID_NULL || id_b == MVCCID_NULL)
	{
	  abort ();
	}

      /* alternate older-first and newer-first, so both ends of the active range move first */
      tran_a = own_tran_index;
      tran_b = second_tran_index;
      if (i % 2 != 0)
	{
	  id_tmp = id_a;
	  id_a = id_b;
	  id_b = id_tmp;
	  tran_tmp = tran_a;
	  tran_a = tran_b;
	  tran_b = tran_tmp;
	}
      order_pairs[writer][i].first_completed = id_a;
      order_pairs[writer][i].second_completed = id_b;
      order_pairs_published[writer].store (i + 1, std::memory_order_release);

      test_order_commit (thread_p, tran_a);
      order_last_completed[writer].store (id_a, std::memory_order_release);
      test_order_commit (thread_p, tran_b);
      order_last_completed[writer].store (id_b, std::memory_order_release);

      local_rounds++;
    }
  thread_p->tran_index = own_tran_index;

  ATOMIC_INC_64 (&count_order_rounds, local_rounds);

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
  pthread_t threads[TEST_MAX_THREADS];
  int idx_thread_entry;
  int error = NO_ERROR;
  char msg[256];

  numthreads = num_snapshot_threads + num_complete_threads + num_oldest_mvccid_threads + TEST_ORDER_WRITERS;
  sprintf (msg, "test_mvcc_operations (%d snapshot threads, %d complete threads, %d oldest threads)",
	   num_snapshot_threads, num_complete_threads, num_oldest_mvccid_threads);
  begin (msg);

  if (num_snapshot_threads < 0 || num_complete_threads < 0 || num_oldest_mvccid_threads < 0)
    {
      printf (" %s: %s\n", "FAILED", "negative number of threads not allowed");
      return ER_FAILED;
    }

  if (numthreads > TEST_MAX_THREADS)
    {
      printf (" %s: %s\n", "FAILED", "too many threads");
      return ER_FAILED;
    }

  count_snapshots = count_complete = count_oldest = 0;
  count_order_rounds = count_checks = count_stale = count_order_violations = count_vacuum_violations = 0;
  total_snapshot_ns = total_complete_ns = 0;
  nops_order = nops_snapshot;
  for (i = 0; i < TEST_ORDER_WRITERS; i++)
    {
      order_pairs[i] = (TEST_ORDER_PAIR *) malloc (nops_order * sizeof (TEST_ORDER_PAIR));
      if (order_pairs[i] == NULL)
	{
	  while (--i >= 0)
	    {
	      free_and_init (order_pairs[i]);
	    }
	  printf (" %s: %s\n", "FAILED", "out of memory");
	  return ER_FAILED;
	}
      order_pairs_published[i].store (0);
      order_last_completed[i].store (MVCCID_NULL);
    }
  snapshot_threads_running.store (num_snapshot_threads);
  idx_thread_entry = 0;
  for (i = 0; i < num_snapshot_threads && error == NO_ERROR; i++, idx_thread_entry++)
    {
      if (pthread_create (&threads[idx_thread_entry], NULL, test_mvcc_get_snapshot,
			  (void *) (thread_array + idx_thread_entry)) != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread create error");
	  error = ER_FAILED;
	  break;
	}
    }

  for (i = 0; i < num_complete_threads && error == NO_ERROR; i++, idx_thread_entry++)
    {
      if (pthread_create (&threads[idx_thread_entry], NULL, test_new_mvcc_complete,
			  (void *) (thread_array + idx_thread_entry)) != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread create error");
	  error = ER_FAILED;
	  break;
	}
    }

  for (i = 0; i < num_oldest_mvccid_threads && error == NO_ERROR; i++, idx_thread_entry++)
    {
      if (pthread_create (&threads[idx_thread_entry], NULL, test_mvcc_get_oldest,
			  (void *) (thread_array + idx_thread_entry)) != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread create error");
	  error = ER_FAILED;
	  break;
	}
    }

  order_writer_first_entry = idx_thread_entry;
  for (i = 0; i < TEST_ORDER_WRITERS && error == NO_ERROR; i++, idx_thread_entry++)
    {
      if (pthread_create (&threads[idx_thread_entry], NULL, test_mvcc_order_writer,
			  (void *) (thread_array + idx_thread_entry)) != NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread create error");
	  error = ER_FAILED;
	  break;
	}
    }

  /* Wait for every thread that started, also after a failure: they use the MVCC table and the pair arrays, which
   * the caller and this function free. idx_thread_entry is the number of threads created. */
  for (i = 0; i < idx_thread_entry; i++)
    {
      void *retval;

      pthread_join (threads[i], &retval);
      if (retval != NO_ERROR && error == NO_ERROR)
	{
	  printf (" %s: %s\n", "FAILED", "thread proc error");
	  error = ER_FAILED;
	}
    }
  for (i = 0; i < TEST_ORDER_WRITERS; i++)
    {
      free_and_init (order_pairs[i]);
    }
  if (error != NO_ERROR)
    {
      return error;
    }

  if (count_snapshots != (UINT64) num_snapshot_threads * nops_snapshot)
    {
      printf ("snapshot count fail (%llu != %llu)",
	      (unsigned long long) count_snapshots, (unsigned long long) num_snapshot_threads * nops_snapshot);
      return ER_FAILED;
    }

  if (count_complete != (UINT64) num_complete_threads * nops_complete)
    {
      printf ("complete count fail (%llu != %llu)",
	      (unsigned long long) count_complete, (unsigned long long) num_complete_threads * nops_complete);
      return ER_FAILED;
    }

  if (count_oldest != (UINT64) num_oldest_mvccid_threads * nops_oldest)
    {
      printf ("oldest count fail (%llu != %llu)",
	      (unsigned long long) count_oldest, (unsigned long long) num_oldest_mvccid_threads * nops_oldest);
      return ER_FAILED;
    }

  printf ("  snapshot avg %.0f ns, complete avg %.0f ns, order rounds %llu, checked snapshots %llu\n",
	  count_snapshots > 0 ? (double) total_snapshot_ns / count_snapshots : 0.0,
	  count_complete > 0 ? (double) total_complete_ns / count_complete : 0.0,
	  (unsigned long long) count_order_rounds, (unsigned long long) count_checks);
  if (count_order_rounds < TEST_ORDER_WRITERS || count_stale != 0 || count_order_violations != 0
      || count_vacuum_violations != 0)
    {
      printf (" %s: order rounds %llu, stale snapshots %llu, order violations %llu, vacuum bound violations %llu\n",
	      "FAILED", (unsigned long long) count_order_rounds, (unsigned long long) count_stale,
	      (unsigned long long) count_order_violations, (unsigned long long) count_vacuum_violations);
      return ER_FAILED;
    }

  success ();

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

  if (argc != 1 && argc != 3 && argc != 4)
    {
      printf ("usage: %s [<snapshot threads> <complete threads> [<operations per thread>]]\n", argv[0]);
      return ER_FAILED;
    }

  if (logtb_initialize_mvcc_testing (TEST_NUM_TRAN_INDICES, &thread_array) != NO_ERROR)
    {
      printf ("Unit tests failed!\n");
      return ER_FAILED;
    }

  if (argc > 1)
    {
      /* one run with the given thread counts, e.g. to compare the cost per operation as threads grow */
      if (test_parse_count (argv[1], 0, TEST_MAX_THREADS, &num_snapshot_threads) != NO_ERROR
	  || test_parse_count (argv[2], 0, TEST_MAX_THREADS, &num_complete_threads) != NO_ERROR
	  || (argc == 4 && test_parse_count (argv[3], 1, TEST_MAX_OPS, &nops_snapshot) != NO_ERROR))
	{
	  printf ("invalid argument: thread counts 0..%d, operations 1..%d\n", TEST_MAX_THREADS, TEST_MAX_OPS);
	  goto fail;
	}
      nops_complete = nops_snapshot;
      nops_oldest = 2 * nops_snapshot;
      if (test_mvcc_operations (num_snapshot_threads, num_complete_threads, 1, thread_array) != NO_ERROR)
	{
	  goto fail;
	}
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
