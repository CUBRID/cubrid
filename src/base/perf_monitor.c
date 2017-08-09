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

/*
 * perf_monitor.c - Monitor execution statistics at Server
 */

#include <stdio.h>
#include <time.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#endif /* !WINDOWS */

#include "perf_monitor.h"
#include "xserver_interface.h"
#include "error_manager.h"

#include "system_parameter.h"
#include "log_impl.h"
#include "session.h"
#include "heap_file.h"
#include "xasl_cache.h"

PSTAT_GLOBAL pstat_Global;
STATIC_INLINE void perfmon_add_stat_at_offset (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, const int offset,
					       UINT64 amount) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int perfmon_get_module_type (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE void perfmon_get_peek_stats (UINT64 * stats) __attribute__ ((ALWAYS_INLINE));

/*
 * perfmon_get_module_type () -
 */
int
perfmon_get_module_type (THREAD_ENTRY * thread_p)
{
  int thread_index;
  int module_type;
  static int first_vacuum_worker_idx = 0;
  static int num_worker_threads = 0;

#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  thread_index = thread_p->index;

  if (first_vacuum_worker_idx == 0)
    {
      first_vacuum_worker_idx = thread_first_vacuum_worker_thread_index ();
    }
  if (num_worker_threads == 0)
    {
      num_worker_threads = thread_num_worker_threads ();
    }
#else
  thread_index = 0;
  first_vacuum_worker_idx = 100;
#endif

  if (thread_index >= 1 && thread_index <= num_worker_threads)
    {
      module_type = PERF_MODULE_USER;
    }
  else if (thread_index >= first_vacuum_worker_idx && thread_index < first_vacuum_worker_idx + VACUUM_MAX_WORKER_COUNT)
    {
      module_type = PERF_MODULE_VACUUM;
    }
  else
    {
      module_type = PERF_MODULE_SYSTEM;
    }

  return module_type;
}

/*
  unsigned int snapshot;
  unsigned int rec_type;
  unsigned int visibility;
      for (snapshot = (unsigned int) PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < (unsigned int) PERF_SNAPSHOT_CNT;
	   snapshot++)
	  for (rec_type = (unsigned int) PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED;
	       rec_type < (unsigned int) PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
	      for (visibility = (unsigned int) PERF_SNAPSHOT_INVISIBLE;
		   visibility < (unsigned int) PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
  unsigned int snapshot;
  unsigned int rec_type;
  unsigned int visibility;
  for (snapshot = (unsigned int) PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < (unsigned int) PERF_SNAPSHOT_CNT;
       snapshot++)
      for (rec_type = (unsigned int) PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED;
	   rec_type < (unsigned int) PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
	  for (visibility = (unsigned int) PERF_SNAPSHOT_INVISIBLE;
	       visibility < (unsigned int) PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
  unsigned int lock_mode;
      for (lock_mode = (unsigned int) NA_LOCK; lock_mode <= (unsigned int) SCH_M_LOCK; lock_mode++)
 * perfmon_initialize () - Computes the metadata values & allocates/initializes global/transaction statistics values.
 *
 * return	  : NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY.
 * num_trans (in) : For server/stand-alone mode to allocate transactions.
 */
int
perfmon_initialize (int num_trans)
{
  int idx = 0;
  int memsize = 0;
  int rc;

  pstat_Global.global_stats = NULL;
  pstat_Global.n_trans = 0;
  pstat_Global.tran_stats = NULL;
  pstat_Global.is_watching = NULL;
  pstat_Global.n_watchers = 0;
  pstat_Global.initialized = false;
  pstat_Global.activation_flag = prm_get_integer_value (PRM_ID_EXTENDED_STATISTICS_ACTIVATION);

  rc = perfmeta_init ();
  if (rc != NO_ERROR)
    {
      /* out of memory */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, 0);
      return rc;
    }

#if defined (SERVER_MODE) || defined (SA_MODE)

#if !defined (HAVE_ATOMIC_BUILTINS)
  (void) pthread_mutex_init (&pstat_Global.watch_lock, NULL);
#endif /* !HAVE_ATOMIC_BUILTINS */

  /* Allocate global stats. */
  pstat_Global.global_stats = (UINT64 *) malloc (perfmeta_get_values_memsize ());
  if (pstat_Global.global_stats == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, perfmeta_get_values_memsize ());
      goto error;
    }
  memset (pstat_Global.global_stats, 0, perfmeta_get_values_memsize ());

  assert (num_trans > 0);

  pstat_Global.n_trans = num_trans + 1;	/* 1 more for easier indexing with tran_index */
  memsize = pstat_Global.n_trans * sizeof (UINT64 *);
  pstat_Global.tran_stats = (UINT64 **) malloc (memsize);
  if (pstat_Global.tran_stats == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
      goto error;
    }
  memsize = pstat_Global.n_trans * (int) perfmeta_get_values_memsize ();
  pstat_Global.tran_stats[0] = (UINT64 *) malloc (memsize);
  if (pstat_Global.tran_stats[0] == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
      goto error;
    }
  memset (pstat_Global.tran_stats[0], 0, memsize);

  for (idx = 1; idx < pstat_Global.n_trans; idx++)
    {
      pstat_Global.tran_stats[idx] = pstat_Global.tran_stats[0] + perfmeta_get_values_count () * idx;
    }

  memsize = pstat_Global.n_trans * sizeof (bool);
  pstat_Global.is_watching = (bool *) malloc (memsize);
  if (pstat_Global.is_watching == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
      goto error;
    }
  memset (pstat_Global.is_watching, 0, memsize);

  pstat_Global.n_watchers = 0;
  pstat_Global.initialized = true;
  return NO_ERROR;

error:
  perfmon_finalize ();
  return ER_OUT_OF_VIRTUAL_MEMORY;
#else
  pstat_Global.initialized = true;
  return NO_ERROR;
#endif /* SERVER_MODE || SA_MODE */
}

/*
 * perfmon_finalize () - Frees all the allocated memory for performance monitor data structures
 *
 * return :
 */

void
perfmon_finalize (void)
{
  perfmeta_final ();

  if (pstat_Global.tran_stats != NULL)
    {
      if (pstat_Global.tran_stats[0] != NULL)
	{
	  free_and_init (pstat_Global.tran_stats[0]);
	}
      free_and_init (pstat_Global.tran_stats);
    }
  if (pstat_Global.is_watching != NULL)
    {
      free_and_init (pstat_Global.is_watching);
    }
  if (pstat_Global.global_stats != NULL)
    {
      free_and_init (pstat_Global.global_stats);
    }
#if defined (SERVER_MODE) || defined (SA_MODE)
#if !defined (HAVE_ATOMIC_BUILTINS)
  pthread_mutex_destroy (&pstat_Global.watch_lock);
#endif /* !HAVE_ATOMIC_BUILTINS */
#endif /* SERVER_MODE || SA_MODE */
}

#if defined (SERVER_MODE) || defined (SA_MODE)

/*
 * perfmon_start_watch () - Start watching performance statistics.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
void
perfmon_start_watch (THREAD_ENTRY * thread_p)
{
  int tran_index;

  assert (pstat_Global.initialized);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0 && tran_index < pstat_Global.n_trans);

  if (pstat_Global.is_watching[tran_index])
    {
      /* Already watching. */
      return;
    }

#if defined (HAVE_ATOMIC_BUILTINS)
  ATOMIC_INC_32 (&pstat_Global.n_watchers, 1);
#else /* !HAVE_ATOMIC_BUILTINS */
  pthread_mutex_lock (&pstat_Global.watch_lock);
  pstat_Global.n_watchers++;
  pthread_mutex_unlock (&pstat_Global.watch_lock);
#endif /* !HAVE_ATOMIC_BUILTINS */

  memset (pstat_Global.tran_stats[tran_index], 0, perfmeta_get_values_memsize ());
  pstat_Global.is_watching[tran_index] = true;
}

/*
 * perfmon_stop_watch () - Stop watching performance statistics.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 */
void
perfmon_stop_watch (THREAD_ENTRY * thread_p)
{
  int tran_index;

  assert (pstat_Global.initialized);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0 && tran_index < pstat_Global.n_trans);

  if (!pstat_Global.is_watching[tran_index])
    {
      /* Not watching. */
      return;
    }

#if defined (HAVE_ATOMIC_BUILTINS)
  ATOMIC_INC_32 (&pstat_Global.n_watchers, -1);
#else /* !HAVE_ATOMIC_BUILTINS */
  pthread_mutex_lock (&pstat_Global.watch_lock);
  pstat_Global.n_watchers--;
  pthread_mutex_unlock (&pstat_Global.watch_lock);
#endif /* !HAVE_ATOMIC_BUILTINS */

  pstat_Global.is_watching[tran_index] = false;
}

#endif /* SERVER_MODE || SA_MODE */

/*
 * perfmon_add_stat_at_offset () - Accumulate amount to statistic.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * psid (in)	 : Statistic ID.
 * offset (in)   : offset at which to add the amount
 * amount (in)	 : Amount to add.
 */
STATIC_INLINE void
perfmon_add_stat_at_offset (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, const int offset, UINT64 amount)
{
  assert (pstat_Global.initialized);
  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);

  /* Update statistics. */
  perfmon_add_at_offset (thread_p, pstat_Metadata[psid].start_offset + offset, amount);
}

/*
 * perfmon_allocate_packed_values_buffer () - Allocate perfmeta_get_values_memsize () bytes and verify alignment
 * 
 */
char *
perfmon_allocate_packed_values_buffer (void)
{
  char *buf;

  buf = (char *) malloc (perfmeta_get_values_memsize ());
  if (buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, perfmeta_get_values_memsize ());
    }
  ASSERT_ALIGN (buf, MAX_ALIGNMENT);

  return buf;
}

/*
 * perfmon_get_peek_stats - Copy into the statistics array the values of the peek statistics
 *		         
 * return: void
 *
 *   stats (in): statistics array
 */
STATIC_INLINE void
perfmon_get_peek_stats (UINT64 * stats)
{
  stats[pstat_Metadata[PSTAT_PC_NUM_CACHE_ENTRIES].start_offset] = xcache_get_entry_count ();
  stats[pstat_Metadata[PSTAT_HF_NUM_STATS_ENTRIES].start_offset] = heap_get_best_space_num_stats_entries ();
  stats[pstat_Metadata[PSTAT_QM_NUM_HOLDABLE_CURSORS].start_offset] = session_get_number_of_holdable_cursors ();

  pgbuf_peek_stats (&(stats[pstat_Metadata[PSTAT_PB_FIXED_CNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_DIRTY_CNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_LRU1_CNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_LRU2_CNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_LRU3_CNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_VICT_CAND].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_AVOID_DEALLOC_CNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_AVOID_VICTIM_CNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_PRIVATE_QUOTA].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_PRIVATE_COUNT].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_WAIT_THREADS_HIGH_PRIO].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_WAIT_THREADS_LOW_PRIO].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_FLUSHED_BCBS_WAIT_FOR_ASSIGN].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_LFCQ_BIG_PRV_NUM].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_LFCQ_PRV_NUM].start_offset]),
		    &(stats[pstat_Metadata[PSTAT_PB_LFCQ_SHR_NUM].start_offset]));
}

#if defined(SERVER_MODE) || defined(SA_MODE)

/*
 * perfmon_server_is_stats_on - Is collecting server execution statistics
 *				for the current transaction index
 *   return: bool
 */
bool
perfmon_server_is_stats_on (THREAD_ENTRY * thread_p)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  if (tran_index >= pstat_Global.n_trans)
    {
      return false;
    }

  return pstat_Global.is_watching[tran_index];
}

/*
 * perfmon_server_get_stats - Get the recorded server statistics for the current
 *			      transaction index
 */
STATIC_INLINE UINT64 *
perfmon_server_get_stats (THREAD_ENTRY * thread_p)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  if (tran_index >= pstat_Global.n_trans)
    {
      return NULL;
    }

  perfmon_get_peek_stats (pstat_Global.tran_stats[tran_index]);
  return pstat_Global.tran_stats[tran_index];
}

/*
 *   xperfmon_server_copy_stats - Copy recorded server statistics for the current
 *				  transaction index
 *   return: none
 *   to_stats(out): buffer to copy
 */
void
xperfmon_server_copy_stats (THREAD_ENTRY * thread_p, UINT64 * to_stats)
{
  UINT64 *from_stats;

  from_stats = perfmon_server_get_stats (thread_p);

  if (from_stats != NULL)
    {
      perfmeta_compute_stats (from_stats);
      perfmeta_copy_values (to_stats, from_stats);
    }
}

/*
 *   xperfmon_server_copy_global_stats - Copy recorded system wide statistics
 *   return: none
 *   to_stats(out): buffer to copy
 */
void
xperfmon_server_copy_global_stats (UINT64 * to_stats)
{
  if (to_stats)
    {
      perfmon_get_peek_stats (pstat_Global.global_stats);
      perfmeta_copy_values (to_stats, pstat_Global.global_stats);
      perfmeta_compute_stats (to_stats);
    }
}

UINT64
perfmon_get_from_statistic (THREAD_ENTRY * thread_p, const int statistic_id)
{
  UINT64 *stats;

  stats = perfmon_server_get_stats (thread_p);
  if (stats != NULL)
    {
      int offset = pstat_Metadata[statistic_id].start_offset;
      return stats[offset];
    }

  return 0;
}

/*
 * perfmon_lk_waited_time_on_objects - Increase lock time wait counter of
 *				       the current transaction index
 *   return: none
 */
void
perfmon_lk_waited_time_on_objects (THREAD_ENTRY * thread_p, int lock_mode, UINT64 amount)
{
  assert (pstat_Global.initialized);

  perfmon_add_stat (thread_p, PSTAT_LK_NUM_WAITED_TIME_ON_OBJECTS, amount);
  assert (lock_mode >= NA_LOCK && lock_mode <= SCH_M_LOCK);
  perfmon_add_stat_at_offset (thread_p, PSTAT_OBJ_LOCK_TIME_COUNTERS, lock_mode, amount);
}

UINT64
perfmon_get_stats_and_clear (THREAD_ENTRY * thread_p, const char *stat_name)
{
  UINT64 *stats;
  int i;
  UINT64 *stats_ptr;
  UINT64 copied = 0;

  stats = perfmon_server_get_stats (thread_p);
  if (stats != NULL)
    {
      stats_ptr = (UINT64 *) stats;
      for (i = 0; i < PSTAT_COUNT; i++)
	{
	  if (strcmp (pstat_Metadata[i].stat_name, stat_name) == 0)
	    {
	      int offset = pstat_Metadata[i].start_offset;

	      switch (pstat_Metadata[i].valtype)
		{
		case PSTAT_ACCUMULATE_SINGLE_VALUE:
		case PSTAT_PEEK_SINGLE_VALUE:
		case PSTAT_COMPUTED_RATIO_VALUE:
		  copied = stats_ptr[offset];
		  stats_ptr[offset] = 0;
		  break;
		case PSTAT_COUNTER_TIMER_VALUE:
		  copied = stats_ptr[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)];
		  stats_ptr[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)] = 0;
		  stats_ptr[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)] = 0;
		  stats_ptr[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)] = 0;
		  stats_ptr[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)] = 0;
		  break;
		case PSTAT_COMPLEX_VALUE:
		default:
		  assert (false);
		  break;
		}
	      return copied;
	    }
	}
    }

  return 0;
}

/*
 *   perfmon_pbx_fix - 
 *   return: none
 */
void
perfmon_pbx_fix (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode, int cond_type)
{
  PERFMETA_COMPLEX_CURSOR cursor;

  assert (pstat_Global.initialized);

  /* todo: hm... how can we do this in a better way? */
  cursor.indices[0] = perfmon_get_module_type (thread_p);
  cursor.indices[1] = page_type;
  cursor.indices[2] = page_found_mode;
  cursor.indices[3] = latch_mode;
  cursor.indices[4] = cond_type;

  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_PBX_FIX_COUNTERS, &cursor), 1);
}

/*
 *   perfmon_pbx_promote - 
 *   return: none
 */
void
perfmon_pbx_promote (THREAD_ENTRY * thread_p, int page_type, int promote_cond, int holder_latch, int success,
		     UINT64 amount)
{
  PERFMETA_COMPLEX_CURSOR cursor;

  assert (pstat_Global.initialized);

  /* todo: hm... how can we do this in a better way? */
  cursor.indices[0] = perfmon_get_module_type (thread_p);
  cursor.indices[1] = page_type;
  cursor.indices[2] = promote_cond;
  cursor.indices[3] = holder_latch;
  cursor.indices[4] = success;

  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_PBX_PROMOTE_COUNTERS, &cursor), 1);
  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_PBX_PROMOTE_TIME_COUNTERS, &cursor),
			 amount);
}

/*
 *   perfmon_pbx_unfix - 
 *   return: none
 *
 * todo: inline
 */
void
perfmon_pbx_unfix (THREAD_ENTRY * thread_p, int page_type, int buf_dirty, int dirtied_by_holder, int holder_latch)
{
  PERFMETA_COMPLEX_CURSOR cursor;

  assert (pstat_Global.initialized);

  /* todo: hm... how can we do this in a better way? */
  cursor.indices[0] = perfmon_get_module_type (thread_p);
  cursor.indices[1] = page_type;
  cursor.indices[2] = buf_dirty;
  cursor.indices[3] = dirtied_by_holder;
  cursor.indices[4] = holder_latch;

  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_PBX_UNFIX_COUNTERS, &cursor), 1);
}

/*
 *   perfmon_pbx_lock_acquire_time - 
 *   return: none
 */
void
perfmon_pbx_lock_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
			       int cond_type, UINT64 amount)
{
  PERFMETA_COMPLEX_CURSOR cursor;

  assert (pstat_Global.initialized);
  assert (amount > 0);

  /* todo: hm... how can we do this in a better way? */
  cursor.indices[0] = perfmon_get_module_type (thread_p);
  cursor.indices[1] = page_type;
  cursor.indices[2] = page_found_mode;
  cursor.indices[3] = latch_mode;
  cursor.indices[4] = cond_type;

  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_PBX_LOCK_TIME_COUNTERS, &cursor), amount);
}

/*
 *   perfmon_pbx_hold_acquire_time - 
 *   return: none
 */
void
perfmon_pbx_hold_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
			       UINT64 amount)
{
  PERFMETA_COMPLEX_CURSOR cursor;

  assert (pstat_Global.initialized);
  assert (amount > 0);

  /* todo: hm... how can we do this in a better way? */
  cursor.indices[0] = perfmon_get_module_type (thread_p);
  cursor.indices[1] = page_type;
  cursor.indices[2] = page_found_mode;
  cursor.indices[3] = latch_mode;

  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_PBX_HOLD_TIME_COUNTERS, &cursor), amount);
}

/*
 *   perfmon_pbx_fix_acquire_time - 
 *   return: none
 */
void
perfmon_pbx_fix_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
			      int cond_type, UINT64 amount)
{
  PERFMETA_COMPLEX_CURSOR cursor;

  assert (pstat_Global.initialized);
  assert (amount > 0);

  /* todo: hm... how can we do this in a better way? */
  cursor.indices[0] = perfmon_get_module_type (thread_p);
  cursor.indices[1] = page_type;
  cursor.indices[2] = page_found_mode;
  cursor.indices[3] = latch_mode;
  cursor.indices[4] = cond_type;

  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_PBX_FIX_TIME_COUNTERS, &cursor), amount);
}

/*
 *   perfmon_mvcc_snapshot - 
 *   return: none
 */
void
perfmon_mvcc_snapshot (THREAD_ENTRY * thread_p, int snapshot, int rec_type, int visibility)
{
  PERFMETA_COMPLEX_CURSOR cursor;

  assert (pstat_Global.initialized);

  /* todo: hm... how can we do this in a better way? */
  cursor.indices[0] = snapshot;
  cursor.indices[1] = rec_type;
  cursor.indices[2] = visibility;

  perfmon_add_at_offset (thread_p, perfmeta_complex_cursor_get_offset (PSTAT_MVCC_SNAPSHOT_COUNTERS, &cursor), 1);
}

#endif /* SERVER_MODE || SA_MODE */
