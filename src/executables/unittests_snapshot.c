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
  memset (tdes, 0, sizeof (LOG_TDES));
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
  memset (*thread_array, 0, size);
  for (i = 0; i < num_threads; i++)
    {
      thread_p = *thread_array + i;
      thread_p->set_thread_type (TT_WORKER);	/* init */
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

/* program entry */
int
main (int argc, char **argv)
{
#define MAX_SNAPSHOT_THREADS 10
#define MAX_COMPLETE_THREADS 10
#define MAX_OLDEST_THREADS 1

  int num_snapshot_threads, num_complete_threads, num_oldest_threads;
  THREAD_ENTRY *thread_array = NULL;

  logtb_initialize_mvcc_testing (100, &thread_array);

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
