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
 * perf_monitor.c - Monitor execution statistics at Client
 * 					Monitor execution statistics
 *                  Monitor execution statistics at Server
 * 					diag server module
 */

#include <stdio.h>
#include <time.h>
#include <assert.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#include <sys/resource.h>
#endif /* WINDOWS */
#include "perf_monitor.h"
#include "network_interface_cl.h"
#include "error_manager.h"

#if !defined(SERVER_MODE)
#include "memory_alloc.h"
#include "server_interface.h"
#endif /* !SERVER_MODE */

#if defined(SERVER_MODE)
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#if !defined(WINDOWS)
#include <sys/shm.h>
#include <sys/ipc.h>
#endif /* WINDOWS */

#include <sys/stat.h>
#include "connection_defs.h"
#include "environment_variable.h"
#include "connection_error.h"
#include "databases_file.h"
#endif /* SERVER_MODE */

#include "thread.h"
#include "log_impl.h"
#include "session.h"

#if !defined(CS_MODE)
#include <string.h>

#include "error_manager.h"
#include "log_manager.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "heap_file.h"
#include "xasl_cache.h"

#if defined (SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* SERVER_MODE */
#endif /* !CS_MODE */


/* Custom values. */
#define PSTAT_VALUE_CUSTOM	      0x00000001

#define PERFMON_VALUES_MEMSIZE (pstat_Global.n_stat_values * sizeof (UINT64))

static void perfmon_print_timer_to_buffer (char **s, int stat_index, UINT64 * stats_ptr, int *remained_size);

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_FILE_IOSYNC_ALL, "file_iosync_all"),
  /* Page buffer basic module */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_FLUSHED, "Num_data_page_flushed"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_PRIVATE_QUOTA, "Num_data_page_private_quota"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_PRIVATE_COUNT, "Num_data_page_private_count"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LRU3_CNT, "Num_data_page_lru3"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_VICT_CAND, "Num_data_page_victim_candidate"),
  /* Page buffer extended */
  /* detailed unfix module */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_TO_PRIVATE_TOP, "Num_unfix_void_to_private_top"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_TO_PRIVATE_MID, "Num_unfix_void_to_private_mid"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_TO_SHARED_MID, "Num_unfix_void_to_shared_mid"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_ONE_PRV_TO_SHR_MID, "Num_unfix_lru1_private_to_shared_mid"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_TWO_PRV_TO_SHR_MID, "Num_unfix_lru2_private_to_shared_mid"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_THREE_PRV_TO_SHR_MID, "Num_unfix_lru3_private_to_shared_mid"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_TWO_PRV_KEEP, "Num_unfix_lru2_private_keep"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_TWO_SHR_KEEP, "Num_unfix_lru2_shared_keep"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_TWO_PRV_TO_TOP, "Num_unfix_lru2_private_to_top"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_TWO_SHR_TO_TOP, "Num_unfix_lru2_shared_to_top"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_THREE_PRV_TO_TOP, "Num_unfix_lru3_private_to_top"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_THREE_SHR_TO_TOP, "Num_unfix_lru3_shared_to_top"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_ONE_PRV_KEEP, "Num_unfix_lru1_private_keep"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_ONE_SHR_KEEP, "Num_unfix_lru1_shared_keep"),
  /* vacuum */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_TO_PRIVATE_TOP_VAC, "Num_unfix_void_to_private_mid_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_ONE_KEEP_VAC, "Num_unfix_lru1_any_keep_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_TWO_KEEP_VAC, "Num_unfix_lru2_any_keep_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_THREE_KEEP_VAC, "Num_unfix_lru3_any_keep_vacuum"),
  /* aout */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_AOUT_FOUND, "Num_unfix_void_aout_found"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_AOUT_NOT_FOUND, "Num_unfix_void_aout_not_found"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_AOUT_FOUND_VAC, "Num_unfix_void_aout_found_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_VOID_AOUT_NOT_FOUND_VAC, "Num_unfix_void_aout_not_found_vacuum"),
  /* hash anchor */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_HASH_ANCHOR_WAITS, "Num_data_page_hash_anchor_waits"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_TIME_HASH_ANCHOR_WAIT, "Time_data_page_hash_anchor_wait"),
  /* flushing */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_FLUSH_COLLECT, "flush_collect"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_FLUSH_FLUSH, "flush_flush"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_FLUSH_SLEEP, "flush_sleep"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_FLUSH_COLLECT_PER_PAGE, "flush_collect_per_page"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_FLUSH_FLUSH_PER_PAGE, "flush_flush_per_page"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_FLUSH_PAGE_FLUSHED, "Num_data_page_writes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_FLUSH_SEND_DIRTY_TO_POST_FLUSH, "Num_data_page_dirty_to_post_flush"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_SKIPPED_FLUSH, "Num_data_page_skipped_flush"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_SKIPPED_NEED_WAL, "Num_data_page_skipped_flush_need_wal"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_SKIPPED_ALREADY_FLUSHED, "Num_data_page_skipped_flush_already_flushed"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_SKIPPED_FIXED_OR_HOT, "Num_data_page_skipped_flush_fixed_or_hot"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_COMPENSATE_FLUSH, "compensate_flush"),
  /* allocate and victim assignments */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB, "alloc_bcb"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB_SEARCH_VICTIM, "alloc_bcb_search_victim"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB_COND_WAIT_HIGH_PRIO, "alloc_bcb_cond_wait_high_prio"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB_COND_WAIT_LOW_PRIO, "alloc_bcb_cond_wait_low_prio"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_ALLOC_BCB_PRIORITIZE_VACUUM, "Num_alloc_bcb_prioritize_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_USE_INVALID_BCB, "Num_victim_use_invalid_bcb"),
  /* direct assignments */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_VOID, "Num_victim_assign_direct_vacuum_void"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_LRU, "Num_victim_assign_direct_vacuum_lru"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_FLUSH, "Num_victim_assign_direct_flush"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_PANIC, "Num_victim_assign_direct_panic"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_ADJUST, "Num_victim_assign_direct_adjust_lru"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_ADJUST_TO_VACUUM,
				  "Num_victim_assign_direct_adjust_lru_to_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_SEARCH_FOR_FLUSH,
				  "Num_victim_assign_direct_search_for_flush"),
  /* successful searches */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_SHARED_LRU_SUCCESS, "Num_victim_shared_lru_success"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_OWN_VICTIM_PRIVATE_LRU_SUCCESS, "Num_victim_own_private_lru_success"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_SUCCESS, "Num_victim_other_private_lru_success"),
  /* failed searches */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_SHARED_LRU_FAIL, "Num_victim_shared_lru_fail"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_OWN_PRIVATE_LRU_FAIL, "Num_victim_own_private_lru_fail"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_FAIL, "Num_victim_other_private_lru_fail"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ALL_LRU_FAIL, "Num_victim_all_lru_fail"),
  /* search lru's */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_GET_FROM_LRU, "Num_victim_get_from_lru"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_GET_FROM_LRU_LIST_WAS_EMPTY, "Num_victim_get_from_lru_was_empty"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_GET_FROM_LRU_FAIL, "Num_victim_get_from_lru_fail"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_GET_FROM_LRU_BAD_HINT, "Num_victim_get_from_lru_bad_hint"),
  /* lock-free circular queues with lru lists having victims */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_LFCQ_LRU_PRV_GET_CALLS, "Num_lfcq_prv_get_total_calls"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_LFCQ_LRU_PRV_GET_EMPTY, "Num_lfcq_prv_get_empty"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_LFCQ_LRU_PRV_GET_BIG, "Num_lfcq_prv_get_big"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_LFCQ_LRU_SHR_GET_CALLS, "Num_lfcq_shr_get_total_calls"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_LFCQ_LRU_SHR_GET_EMPTY, "Num_lfcq_shr_get_empty"),
  /* peeked stats */
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_WAIT_THREADS_HIGH_PRIO, "Num_alloc_bcb_wait_threads_high_priority"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_WAIT_THREADS_LOW_PRIO, "Num_alloc_bcb_wait_threads_low_priority"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_FLUSHED_BCBS_WAIT_FOR_ASSIGN, "Num_flushed_bcbs_wait_for_direct_victim"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_BIG_PRV_NUM, "Num_lfcq_big_private_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_PRV_NUM, "Num_lfcq_private_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_SHR_NUM, "Num_lfcq_shared_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_AVOID_DEALLOC_CNT, "Num_data_page_avoid_dealloc"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_AVOID_VICTIM_CNT, "Num_data_page_avoid_victim"),

STATIC_INLINE void perfmon_add_stat_at_offset (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, const int offset,
					       UINT64 amount) __attribute__ ((ALWAYS_INLINE));

static void perfmon_server_calc_stats (UINT64 * stats);

STATIC_INLINE void perfmon_get_peek_stats (UINT64 * stats) __attribute__ ((ALWAYS_INLINE));

#if defined(CS_MODE) || defined(SA_MODE)
bool perfmon_Iscollecting_stats = false;

/* Client execution statistics */
static PERFMON_CLIENT_STAT_INFO perfmon_Stat_info;

/*
 * perfmon_start_stats - Start collecting client execution statistics
 *   return: NO_ERROR or ERROR
 */
int
perfmon_start_stats (bool for_all_trans)
{
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats == true)
    {
      goto exit;
    }

  perfmon_Stat_info.old_global_stats = NULL;
  perfmon_Stat_info.current_global_stats = NULL;
  perfmon_Stat_info.base_server_stats = NULL;
  perfmon_Stat_info.current_server_stats = NULL;

  err = perfmon_server_start_stats ();
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  perfmon_Iscollecting_stats = true;

  perfmon_get_current_times (&perfmon_Stat_info.cpu_start_usr_time, &perfmon_Stat_info.cpu_start_sys_time,
			     &perfmon_Stat_info.elapsed_start_time);

  if (for_all_trans)
    {
      perfmon_Stat_info.old_global_stats = perfmon_allocate_values ();
      if (perfmon_Stat_info.old_global_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}
      perfmon_Stat_info.current_global_stats = perfmon_allocate_values ();

      if (perfmon_Stat_info.current_global_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      if (perfmon_get_global_stats () == NO_ERROR)
	{
	  perfmon_copy_values (perfmon_Stat_info.old_global_stats, perfmon_Stat_info.current_global_stats);
	}
    }
  else
    {
      perfmon_Stat_info.base_server_stats = perfmon_allocate_values ();
      if (perfmon_Stat_info.base_server_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}
      perfmon_Stat_info.current_server_stats = perfmon_allocate_values ();
      if (perfmon_Stat_info.current_server_stats == NULL)
	{
	  ASSERT_ERROR ();
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      if (perfmon_get_stats () == NO_ERROR)
	{
	  perfmon_copy_values (perfmon_Stat_info.base_server_stats, perfmon_Stat_info.current_server_stats);
	}
    }
exit:
  return err;
}

/*
 * perfmon_stop_stats - Stop collecting client execution statistics
 *   return: NO_ERROR or ER_FAILED
 */
int
perfmon_stop_stats (void)
{
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != false)
    {
      err = perfmon_server_stop_stats ();
      perfmon_Iscollecting_stats = false;
    }

  if (perfmon_Stat_info.old_global_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.old_global_stats);
    }
  if (perfmon_Stat_info.current_global_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.current_global_stats);
    }
  if (perfmon_Stat_info.base_server_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.base_server_stats);
    }

  if (perfmon_Stat_info.current_server_stats != NULL)
    {
      free_and_init (perfmon_Stat_info.current_server_stats);
    }

  return err;
}

/*
 * perfmon_reset_stats - Reset client statistics
 *   return: none
 */
void
perfmon_reset_stats (void)
{
  if (perfmon_Iscollecting_stats != false)
    {
      perfmon_get_current_times (&perfmon_Stat_info.cpu_start_usr_time, &perfmon_Stat_info.cpu_start_sys_time,
				 &perfmon_Stat_info.elapsed_start_time);

      if (perfmon_get_stats () == NO_ERROR)
	{
	  perfmon_copy_values (perfmon_Stat_info.base_server_stats, perfmon_Stat_info.current_server_stats);
	}
    }
}

/*
 * perfmon_get_stats - Get the recorded client statistics
 *   return: client statistics
 */
int
perfmon_get_stats (void)
{
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != true)
    {
      return ER_FAILED;
    }

  err = perfmon_server_copy_stats (perfmon_Stat_info.current_server_stats);
  return err;
}

/*
 *   perfmon_get_global_stats - Get the recorded client statistics
 *   return: client statistics
 */
int
perfmon_get_global_stats (void)
{
  UINT64 *tmp_stats;
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != true)
    {
    er_log_debug(ARG_FILE_LINE, "aici100\n");
      return ER_FAILED;
    }

  tmp_stats = perfmon_Stat_info.current_global_stats;
  perfmon_Stat_info.current_global_stats = perfmon_Stat_info.old_global_stats;
  perfmon_Stat_info.old_global_stats = tmp_stats;

  /* Refresh statistics from server */
  err = perfmon_server_copy_global_stats (perfmon_Stat_info.current_global_stats);
  if (err != NO_ERROR)
    {
    er_log_debug(ARG_FILE_LINE, "aici10\n");
      ASSERT_ERROR ();
    }
  return err;
}

/*
 *   perfmon_print_stats - Print the current client statistics
 *   return: error or no error
 *   stream(in): if NULL is given, stdout is used
 */
int
perfmon_print_stats (FILE * stream)
{
  time_t cpu_total_usr_time;
  time_t cpu_total_sys_time;
  time_t elapsed_total_time;
  UINT64 *diff_result = NULL;
  int err = NO_ERROR;

  if (perfmon_Iscollecting_stats != true)
    {
      return err;
    }

  diff_result = perfmon_allocate_values ();

  if (diff_result == NULL)
    {
      ASSERT_ERROR ();
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  if (stream == NULL)
    {
      stream = stdout;
    }

  if (perfmon_get_stats () != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  perfmon_get_current_times (&cpu_total_usr_time, &cpu_total_sys_time, &elapsed_total_time);

  fprintf (stream, "\n *** CLIENT EXECUTION STATISTICS ***\n");

  fprintf (stream, "System CPU (sec)              = %10d\n",
	   (int) (cpu_total_sys_time - perfmon_Stat_info.cpu_start_sys_time));
  fprintf (stream, "User CPU (sec)                = %10d\n",
	   (int) (cpu_total_usr_time - perfmon_Stat_info.cpu_start_usr_time));
  fprintf (stream, "Elapsed (sec)                 = %10d\n",
	   (int) (elapsed_total_time - perfmon_Stat_info.elapsed_start_time));

  if (perfmon_calc_diff_stats (diff_result, perfmon_Stat_info.current_server_stats,
			       perfmon_Stat_info.base_server_stats) != NO_ERROR)
    {
      assert (false);
      goto exit;
    }
  perfmon_server_dump_stats (diff_result, stream, NULL);

exit:
  if (diff_result != NULL)
    {
      free_and_init (diff_result);
    }
  return err;
}

/*
 *   perfmon_print_global_stats - Print the global statistics
 *   return: error or no error
 *   stream(in): if NULL is given, stdout is used
 */
int
perfmon_print_global_stats (FILE * stream, FILE * bin_stream, bool cumulative, const char *substr)
{
  UINT64 *diff_result = NULL;
  int err = NO_ERROR;

  if (stream == NULL)
    {
      stream = stdout;
    }
  diff_result = perfmon_allocate_values ();

  if (diff_result == NULL)
    {
      ASSERT_ERROR ();
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }
  err = perfmon_get_global_stats ();
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }
  if (cumulative)
    {
	fwrite(perfmon_Stat_info.current_global_stats, sizeof(UINT64), (size_t)pstat_Global.n_stat_values, bin_stream);
      perfmon_server_dump_stats (perfmon_Stat_info.current_global_stats, stream, substr);
    }
  else
    {
      if (perfmon_calc_diff_stats (diff_result, perfmon_Stat_info.current_global_stats,
				   perfmon_Stat_info.old_global_stats) != NO_ERROR)
	{
	  assert (false);
	  goto exit;
	}
      perfmon_server_dump_stats (diff_result, stream, substr);
    }

exit:
  if (diff_result != NULL)
    {
      free_and_init (diff_result);
    }
  return err;
}

#endif /* CS_MODE || SA_MODE */

#if defined (DIAG_DEVEL)
#if defined(SERVER_MODE)
#if defined(WINDOWS)
#define SERVER_SHM_CREATE(SHM_KEY, SIZE, HANDLE_PTR)    \
        server_shm_create(SHM_KEY, SIZE, HANDLE_PTR)
#define SERVER_SHM_OPEN(SHM_KEY, HANDLE_PTR)            \
        server_shm_open(SHM_KEY, HANDLE_PTR)
#define SERVER_SHM_DETACH(PTR, HMAP)	\
        do {				\
          if (HMAP != NULL) {		\
            UnmapViewOfFile(PTR);	\
            CloseHandle(HMAP);		\
          }				\
        } while (0)
#else /* WINDOWS */
#define SERVER_SHM_CREATE(SHM_KEY, SIZE, HANDLE_PTR)    \
        server_shm_create(SHM_KEY, SIZE)
#define SERVER_SHM_OPEN(SHM_KEY, HANDLE_PTR)            \
        server_shm_open(SHM_KEY)
#define SERVER_SHM_DETACH(PTR, HMAP)    shmdt(PTR)
#endif /* WINDOWS */

#define SERVER_SHM_DESTROY(SHM_KEY)     \
        server_shm_destroy(SHM_KEY)

#define CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT(ERR_BUF) \
    do { \
        if (thread_is_manager_initialized() == false) {\
            if (ERR_BUF) strcpy(ERR_BUF, "thread mgr is not initialized");\
            return -1;\
        }\
    } while(0)

#define CHECK_SHM() \
    do { \
        if (g_ShmServer == NULL) return -1; \
    } while(0)

#define CUBRID_KEY_GEN_ID 0x08
#define DIAG_SERVER_MAGIC_NUMBER 07115

/* Global variables */
bool diag_executediag;
int diag_long_query_time;

static int ShmPort;
static T_SHM_DIAG_INFO_SERVER *g_ShmServer = NULL;

#if defined(WINDOWS)
static HANDLE shm_map_object;
#endif /* WINDOWS */

/* Diag value modification function */
static int diag_val_set_query_open_page (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_query_opened_page (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_buffer_page_read (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_buffer_page_write (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_conn_aborted_clients (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_conn_cli_request (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_query_slow_query (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_lock_deadlock (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_lock_request (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_query_full_scan (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_conn_conn_req (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
static int diag_val_set_conn_conn_reject (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);

static int server_shm_destroy (int shm_key);
static bool diag_sm_isopened (void);
static bool init_server_diag_value (T_SHM_DIAG_INFO_SERVER * shm_server);
static bool init_diag_sm (const char *server_name, int num_thread, char *err_buf);
static bool rm_diag_sm (void);

static char *trim_line (char *str);
static int create_shm_key_file (int port, char *vol_dir, const char *servername);
static int read_diag_system_config (DIAG_SYS_CONFIG * config, char *err_buf);
static int get_volumedir (char *vol_dir, const char *dbname);
static int get_server_shmid (char *dir, const char *dbname);


#if defined(WINDOWS)
static void shm_key_to_name (int shm_key, char *name_str);
static void *server_shm_create (int shm_key, int size, HANDLE * hOut);
static void *server_shm_open (int shm_key, HANDLE * hOut);
#else /* WINDOWS */
static void *server_shm_create (int shm_key, int size);
static void *server_shm_open (int shm_key);
#endif /* WINDOWS */

T_DIAG_OBJECT_TABLE diag_obj_list[] = {
  {"open_page", DIAG_OBJ_TYPE_QUERY_OPEN_PAGE, diag_val_set_query_open_page}
  , {"opened_page", DIAG_OBJ_TYPE_QUERY_OPENED_PAGE,
     diag_val_set_query_opened_page}
  , {"slow_query", DIAG_OBJ_TYPE_QUERY_SLOW_QUERY,
     diag_val_set_query_slow_query}
  , {"full_scan", DIAG_OBJ_TYPE_QUERY_FULL_SCAN, diag_val_set_query_full_scan}
  , {"cli_request", DIAG_OBJ_TYPE_CONN_CLI_REQUEST,
     diag_val_set_conn_cli_request}
  , {"aborted_client", DIAG_OBJ_TYPE_CONN_ABORTED_CLIENTS,
     diag_val_set_conn_aborted_clients}
  , {"conn_req", DIAG_OBJ_TYPE_CONN_CONN_REQ, diag_val_set_conn_conn_req}
  , {"conn_reject", DIAG_OBJ_TYPE_CONN_CONN_REJECT,
     diag_val_set_conn_conn_reject}
  , {"buffer_page_read", DIAG_OBJ_TYPE_BUFFER_PAGE_READ,
     diag_val_set_buffer_page_read}
  , {"buffer_page_write", DIAG_OBJ_TYPE_BUFFER_PAGE_WRITE,
     diag_val_set_buffer_page_write}
  , {"lock_deadlock", DIAG_OBJ_TYPE_LOCK_DEADLOCK, diag_val_set_lock_deadlock}
  , {"lock_request", DIAG_OBJ_TYPE_LOCK_REQUEST, diag_val_set_lock_request}
};

/* function definition */
/*
 * trim_line()
 *    return: char *
 *    str(in):
 */
static char *
trim_line (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str; *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'); s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    {
      memcpy (str, s, strlen (s) + 1);
    }

  return (str);
}

/*
 * create_shm_key_file()
 *    return: int
 *    port(in):
 *    vol_dir(in):
 *    servername(in):
 */
static int
create_shm_key_file (int port, char *vol_dir, const char *servername)
{
  FILE *keyfile;
  char keyfilepath[PATH_MAX];

  if (!vol_dir || !servername)
    {
      return -1;
    }

  sprintf (keyfilepath, "%s/%s_shm.key", vol_dir, servername);
  keyfile = fopen (keyfilepath, "w+");
  if (keyfile)
    {
      fprintf (keyfile, "%x", port);
      fclose (keyfile);
      return 1;
    }

  return -1;
}

/*
 * read_diag_system_config()
 *    return: int
 *    config(in):
 *    err_buf(in):
 */
static int
read_diag_system_config (DIAG_SYS_CONFIG * config, char *err_buf)
{
  FILE *conf_file;
  char cbuf[1024], file_path[PATH_MAX];
  char *cubrid_home;
  char ent_name[128], ent_val[128];

  if (config == NULL)
    {
      return -1;
    }

  /* Initialize config data */
  config->Executediag = 0;
  config->server_long_query_time = 0;

  cubrid_home = envvar_root ();

  if (cubrid_home == NULL)
    {
      if (err_buf)
	{
	  strcpy (err_buf, "Environment variable CUBRID is not set.");
	}
      return -1;
    }

  envvar_confdir_file (file_path, PATH_MAX, "cm.conf");

  conf_file = fopen (file_path, "r");

  if (conf_file == NULL)
    {
      if (err_buf)
	{
	  sprintf (err_buf, "File(%s) open error.", file_path);
	}
      return -1;
    }

  while (fgets (cbuf, sizeof (cbuf), conf_file))
    {
      char format[1024];

      trim_line (cbuf);
      if (cbuf[0] == '\0' || cbuf[0] == '#')
	{
	  continue;
	}

      snprintf (format, sizeof (format), "%%%ds %%%ds", (int) sizeof (ent_name), (int) sizeof (ent_val));
      if (sscanf (cbuf, format, ent_name, ent_val) < 2)
	{
	  continue;
	}

      if (strcasecmp (ent_name, "Execute_diag") == 0)
	{
	  if (strcasecmp (ent_val, "ON") == 0)
	    {
	      config->Executediag = 1;
	    }
	  else
	    {
	      config->Executediag = 0;
	    }
	}
      else if (strcasecmp (ent_name, "server_long_query_time") == 0)
	{
	  config->server_long_query_time = atoi (ent_val);
	}
    }

  fclose (conf_file);
  return 1;
}

/*
 * get_volumedir()
 *    return: int
 *    vol_dir(in):
 *    dbname(in):
 *    err_buf(in):
 */
static int
get_volumedir (char *vol_dir, const char *dbname)
{
  FILE *databases_txt;
#if !defined (DO_NOT_USE_CUBRIDENV)
  const char *envpath;
#endif
  char db_txt[PATH_MAX];
  char cbuf[PATH_MAX * 2];
  char volname[MAX_SERVER_NAMELENGTH];

  if (vol_dir == NULL || dbname == NULL)
    {
      return -1;
    }

#if !defined (DO_NOT_USE_CUBRIDENV)
  envpath = envvar_get ("DATABASES");
  if (envpath == NULL || strlen (envpath) == 0)
    {
      return -1;
    }

  sprintf (db_txt, "%s/%s", envpath, DATABASES_FILENAME);
#else
  envvar_vardir_file (db_txt, PATH_MAX, DATABASES_FILENAME);
#endif
  databases_txt = fopen (db_txt, "r");
  if (databases_txt == NULL)
    {
      return -1;
    }

  while (fgets (cbuf, sizeof (cbuf), databases_txt))
    {
      char format[1024];
      snprintf (format, sizeof (format), "%%%ds %%%ds %%*s %%*s", (int) sizeof (volname), PATH_MAX);

      if (sscanf (cbuf, format, volname, vol_dir) < 2)
	continue;

      if (strcmp (volname, dbname) == 0)
	{
	  fclose (databases_txt);
	  return 1;
	}
    }

  fclose (databases_txt);
  return -1;
}

/*
 * get_server_shmid()
 *    return: int
 *    dir(in):
 *    dbname(in):
 */
static int
get_server_shmid (char *dir, const char *dbname)
{
  int shm_key = 0;
  char vol_full_path[PATH_MAX];
  char *p;

  sprintf (vol_full_path, "%s/%s", dir, dbname);
  for (p = vol_full_path; *p; p++)
    {
      shm_key = 31 * shm_key + (*p);
    }
  shm_key &= 0x00ffffff;

  return shm_key;
}

#if defined(WINDOWS)

/*
 * shm_key_to_name()
 *    return: none
 *    shm_key(in):
 *    name_str(in):
 */
static void
shm_key_to_name (int shm_key, char *name_str)
{
  sprintf (name_str, "cubrid_shm_%d", shm_key);
}

/*
 * server_shm_create()
 *    return: void*
 *    shm_key(in):
 *    size(in):
 *    hOut(in):
 */
static void *
server_shm_create (int shm_key, int size, HANDLE * hOut)
{
  LPVOID lpvMem = NULL;
  HANDLE hMapObject = NULL;
  char shm_name[64];

  *hOut = NULL;

  shm_key_to_name (shm_key, shm_name);

  hMapObject = CreateFileMapping (INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, shm_name);
  if (hMapObject == NULL)
    {
      return NULL;
    }

  if (GetLastError () == ERROR_ALREADY_EXISTS)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  lpvMem = MapViewOfFile (hMapObject, FILE_MAP_WRITE, 0, 0, 0);
  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  *hOut = hMapObject;
  return lpvMem;
}

/*
 * server_shm_open()
 *    return: void *
 *    shm_key(in):
 *    hOut(in):
 */
static void *
server_shm_open (int shm_key, HANDLE * hOut)
{
  LPVOID lpvMem = NULL;		/* address of shared memory */
  HANDLE hMapObject = NULL;
  char shm_name[64];

  *hOut = NULL;

  shm_key_to_name (shm_key, shm_name);

  hMapObject = OpenFileMapping (FILE_MAP_WRITE,	/* read/write access */
				FALSE,	/* inherit flag */
				shm_name);	/* name of map object */
  if (hMapObject == NULL)
    {
      return NULL;
    }

  /* Get a pointer to the file-mapped shared memory. */
  lpvMem = MapViewOfFile (hMapObject,	/* object to map view of */
			  FILE_MAP_WRITE,	/* read/write access */
			  0,	/* high offset: map from */
			  0,	/* low offset: beginning */
			  0);	/* default: map entire file */
  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  *hOut = hMapObject;
  return lpvMem;
}

#else /* WINDOWS */

/*
 * server_shm_create()
 *    return: void *
 *    shm_key(in):
 *    size(in):
 */
static void *
server_shm_create (int shm_key, int size)
{
  int mid;
  void *p;

  if (size <= 0 || shm_key <= 0)
    {
      return NULL;
    }

  mid = shmget (shm_key, size, IPC_CREAT | IPC_EXCL | SH_MODE);

  if (mid == -1)
    {
      return NULL;
    }
  p = shmat (mid, (char *) 0, 0);

  if (p == (void *) -1)
    {
      return NULL;
    }

  return p;
}

/*
 * server_shm_open()
 *    return: void *
 *    shm_key(in):
 */
static void *
server_shm_open (int shm_key)
{
  int mid;
  void *p;

  if (shm_key < 0)
    {
      return NULL;
    }
  mid = shmget (shm_key, 0, SHM_RDONLY);

  if (mid == -1)
    return NULL;

  p = shmat (mid, (char *) 0, SHM_RDONLY);

  if (p == (void *) -1)
    {
      return NULL;
    }
  return p;
}
#endif /* WINDOWS */

/*
 * server_shm_destroy() -
 *    return: int
 *    shm_key(in):
 */
static int
server_shm_destroy (int shm_key)
{
#if !defined(WINDOWS)
  int mid;

  mid = shmget (shm_key, 0, SH_MODE);

  if (mid == -1)
    {
      return -1;
    }

  if (shmctl (mid, IPC_RMID, 0) == -1)
    {
      return -1;
    }
#endif /* WINDOWS */
  return 0;
}

/*
 * diag_sm_isopened() -
 *    return : bool
 */
static bool
diag_sm_isopened (void)
{
  return (g_ShmServer == NULL) ? false : true;
}

/*
 * init_server_diag_value() -
 *    return : bool
 *    shm_server(in):
 */
static bool
init_server_diag_value (T_SHM_DIAG_INFO_SERVER * shm_server)
{
  int i, thread_num;

  if (!shm_server)
    return false;

  thread_num = shm_server->num_thread;
  for (i = 0; i < thread_num; i++)
    {
      shm_server->thread[i].query_open_page = 0;
      shm_server->thread[i].query_opened_page = 0;
      shm_server->thread[i].query_slow_query = 0;
      shm_server->thread[i].query_full_scan = 0;
      shm_server->thread[i].conn_cli_request = 0;
      shm_server->thread[i].conn_aborted_clients = 0;
      shm_server->thread[i].conn_conn_req = 0;
      shm_server->thread[i].conn_conn_reject = 0;
      shm_server->thread[i].buffer_page_write = 0;
      shm_server->thread[i].buffer_page_read = 0;
      shm_server->thread[i].lock_deadlock = 0;
      shm_server->thread[i].lock_request = 0;
    }

  return true;
}

/*
 * init_diag_sm()
 *    return: bool
 *    server_name(in):
 *    num_thread(in):
 *    err_buf(in):
 */
static bool
init_diag_sm (const char *server_name, int num_thread, char *err_buf)
{
  DIAG_SYS_CONFIG config_diag;
  char vol_dir[PATH_MAX];
  int i;

  if (server_name == NULL)
    {
      goto init_error;
    }
  if (read_diag_system_config (&config_diag, err_buf) != 1)
    {
      goto init_error;
    }
  if (!config_diag.Executediag)
    {
      goto init_error;
    }
  if (get_volumedir (vol_dir, server_name) == -1)
    {
      goto init_error;
    }

  ShmPort = get_server_shmid (vol_dir, server_name);

  if (ShmPort == -1)
    {
      goto init_error;
    }

  g_ShmServer =
    (T_SHM_DIAG_INFO_SERVER *) SERVER_SHM_CREATE (ShmPort, sizeof (T_SHM_DIAG_INFO_SERVER), &shm_map_object);

  for (i = 0; (i < 5 && !g_ShmServer); i++)
    {
      if (errno == EEXIST)
	{
	  T_SHM_DIAG_INFO_SERVER *shm = (T_SHM_DIAG_INFO_SERVER *) SERVER_SHM_OPEN (ShmPort,
										    &shm_map_object);
	  if (shm != NULL)
	    {
	      if ((shm->magic_key == DIAG_SERVER_MAGIC_NUMBER) && (shm->servername)
		  && strcmp (shm->servername, server_name) == 0)
		{
		  SERVER_SHM_DETACH ((void *) shm, shm_map_object);
		  SERVER_SHM_DESTROY (ShmPort);
		  g_ShmServer =
		    (T_SHM_DIAG_INFO_SERVER *) SERVER_SHM_CREATE (ShmPort, sizeof (T_SHM_DIAG_INFO_SERVER),
								  &shm_map_object);
		  break;
		}
	      else
		SERVER_SHM_DETACH ((void *) shm, shm_map_object);
	    }

	  ShmPort++;
	  g_ShmServer =
	    (T_SHM_DIAG_INFO_SERVER *) SERVER_SHM_CREATE (ShmPort, sizeof (T_SHM_DIAG_INFO_SERVER), &shm_map_object);
	}
      else
	{
	  break;
	}
    }

  if (g_ShmServer == NULL)
    {
      if (err_buf)
	{
	  strcpy (err_buf, strerror (errno));
	}
      goto init_error;
    }

  diag_long_query_time = config_diag.server_long_query_time;
  diag_executediag = (config_diag.Executediag == 0) ? false : true;

  if (diag_long_query_time < 1)
    {
      diag_long_query_time = DB_INT32_MAX;
    }

  strcpy (g_ShmServer->servername, server_name);
  g_ShmServer->num_thread = num_thread;
  g_ShmServer->magic_key = DIAG_SERVER_MAGIC_NUMBER;

  init_server_diag_value (g_ShmServer);

  if (create_shm_key_file (ShmPort, vol_dir, server_name) == -1)
    {
      if (err_buf)
	{
	  strcpy (err_buf, strerror (errno));
	}
      SERVER_SHM_DETACH ((void *) g_ShmServer, shm_map_object);
      SERVER_SHM_DESTROY (ShmPort);
      goto init_error;
    }

  return true;

init_error:
  g_ShmServer = NULL;
  diag_executediag = false;
  diag_long_query_time = DB_INT32_MAX;
  return false;
}

/*
 * rm_diag_sm()
 *    return: bool
 *
 */
static bool
rm_diag_sm (void)
{
  if (diag_sm_isopened () == true)
    {
      SERVER_SHM_DESTROY (ShmPort);
      return true;
    }

  return false;
}

/*
 * diag_val_set_query_open_page()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_query_open_page (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  if (settype == DIAG_VAL_SETTYPE_INC)
    {
      g_ShmServer->thread[thread_index].query_open_page += value;
    }
  else if (settype == DIAG_VAL_SETTYPE_SET)
    {
      g_ShmServer->thread[thread_index].query_open_page = value;
    }
  else if (settype == DIAG_VAL_SETTYPE_DEC)
    {
      g_ShmServer->thread[thread_index].query_open_page -= value;
    }

  return 0;
}

/*
 * diag_val_set_query_opened_page()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_query_opened_page (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].query_opened_page += value;

  return 0;
}

/*
 * diag_val_set_buffer_page_read()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_buffer_page_read (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].buffer_page_read += value;

  return 0;
}

/*
 * diag_val_set_buffer_page_write()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_buffer_page_write (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].buffer_page_write += value;

  return 0;
}


/*
 * diag_val_set_conn_aborted_clients()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_conn_aborted_clients (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].conn_aborted_clients += value;

  return 0;
}

/*
 * diag_val_set_conn_cli_request()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_conn_cli_request (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].conn_cli_request += value;

  return 0;
}

/*
 * diag_val_set_query_slow_query()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_query_slow_query (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].query_slow_query += value;

  return 0;
}

/*
 * diag_val_set_lock_deadlock()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_lock_deadlock (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].lock_deadlock += value;

  return 0;
}

/*
 * diag_val_set_lock_request()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_lock_request (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].lock_request += value;

  return 0;
}

/*
 * diag_val_set_query_full_scan()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_query_full_scan (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].query_full_scan += value;

  return 0;
}

/*
 * diag_val_set_conn_conn_req()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_conn_conn_req (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].conn_conn_req += value;

  return 0;
}

/*
 * diag_val_set_conn_conn_reject()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_conn_conn_reject (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = THREAD_GET_CURRENT_ENTRY_INDEX (NULL);

  g_ShmServer->thread[thread_index].conn_conn_reject += value;

  return 0;
}

/* Interface function */
/*
 * init_diag_mgr()
 *    return: bool
 *    server_name(in):
 *    num_thread(in):
 *    err_buf(in):
 */
bool
init_diag_mgr (const char *server_name, int num_thread, char *err_buf)
{
  if (init_diag_sm (server_name, num_thread, err_buf) == false)
    return false;

  return true;
}

/*
 * close_diag_mgr()
 *    return: none
 */
void
close_diag_mgr (void)
{
  rm_diag_sm ();
}

/*
 * set_diag_value() -
 *    return: bool
 *    type(in):
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
bool
set_diag_value (T_DIAG_OBJ_TYPE type, int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  T_DO_FUNC task_func;

  if (diag_executediag == false)
    return false;

  task_func = diag_obj_list[type].func;

  if (task_func (value, settype, err_buf) < 0)
    {
      return false;
    }
  else
    {
      return true;
    }
}
#endif /* SERVER_MODE */
#endif /* DIAG_DEVEL */

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
      perfmon_server_calc_stats (from_stats);
      perfmon_copy_values (to_stats, from_stats);
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
      perfmon_copy_values (to_stats, pstat_Global.global_stats);
      perfmon_server_calc_stats (to_stats);
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
  UINT64 copied;

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
  int module;
  int offset;

  assert (pstat_Global.initialized);

  module = perfmon_get_module_type (thread_p);

  assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
  assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
  assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
  assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
  assert (cond_type >= PERF_CONDITIONAL_FIX && cond_type < PERF_CONDITIONAL_FIX_CNT);

  offset = PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type);
  assert (offset < PERF_PAGE_FIX_COUNTERS);

  perfmon_add_stat_at_offset (thread_p, PSTAT_PBX_FIX_COUNTERS, offset, 1);
}

/*
 *   perfmon_pbx_promote - 
 *   return: none
 */
void
perfmon_pbx_promote (THREAD_ENTRY * thread_p, int page_type, int promote_cond, int holder_latch, int success,
		     UINT64 amount)
{
  int module;
  int offset;

  assert (pstat_Global.initialized);

  module = perfmon_get_module_type (thread_p);

  assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
  assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
  assert (promote_cond >= PERF_PROMOTE_ONLY_READER && promote_cond < PERF_PROMOTE_CONDITION_CNT);
  assert (holder_latch >= PERF_HOLDER_LATCH_READ && holder_latch < PERF_HOLDER_LATCH_CNT);
  assert (success == 0 || success == 1);

  offset = PERF_PAGE_PROMOTE_STAT_OFFSET (module, page_type, promote_cond, holder_latch, success);
  assert (offset < PERF_PAGE_PROMOTE_COUNTERS);

  perfmon_add_stat_at_offset (thread_p, PSTAT_PBX_PROMOTE_COUNTERS, offset, 1);
  perfmon_add_stat_at_offset (thread_p, PSTAT_PBX_PROMOTE_TIME_COUNTERS, offset, amount);
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
  int module;
  int offset;

  assert (pstat_Global.initialized);

  module = perfmon_get_module_type (thread_p);

  assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
  assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
  assert (buf_dirty == 0 || buf_dirty == 1);
  assert (dirtied_by_holder == 0 || dirtied_by_holder == 1);
  assert (holder_latch >= PERF_HOLDER_LATCH_READ && holder_latch < PERF_HOLDER_LATCH_CNT);

  offset = PERF_PAGE_UNFIX_STAT_OFFSET (module, page_type, buf_dirty, dirtied_by_holder, holder_latch);
  assert (offset < PERF_PAGE_UNFIX_COUNTERS);

  perfmon_add_stat_at_offset (thread_p, PSTAT_PBX_UNFIX_COUNTERS, offset, 1);
}

/*
 *   perfmon_pbx_lock_acquire_time - 
 *   return: none
 */
void
perfmon_pbx_lock_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
			       int cond_type, UINT64 amount)
{
  int module;
  int offset;

  assert (pstat_Global.initialized);

  module = perfmon_get_module_type (thread_p);

  assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
  assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
  assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
  assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
  assert (cond_type >= PERF_CONDITIONAL_FIX && cond_type < PERF_CONDITIONAL_FIX_CNT);
  assert (amount > 0);

  offset = PERF_PAGE_LOCK_TIME_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type);
  assert (offset < PERF_PAGE_LOCK_TIME_COUNTERS);

  perfmon_add_stat_at_offset (thread_p, PSTAT_PBX_LOCK_TIME_COUNTERS, offset, amount);
}

/*
 *   perfmon_pbx_hold_acquire_time - 
 *   return: none
 */
void
perfmon_pbx_hold_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
			       UINT64 amount)
{
  int module;
  int offset;

  assert (pstat_Global.initialized);

  module = perfmon_get_module_type (thread_p);

  assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
  assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
  assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
  assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
  assert (amount > 0);

  offset = PERF_PAGE_HOLD_TIME_OFFSET (module, page_type, page_found_mode, latch_mode);
  assert (offset < PERF_PAGE_HOLD_TIME_COUNTERS);

  perfmon_add_stat_at_offset (thread_p, PSTAT_PBX_HOLD_TIME_COUNTERS, offset, amount);
}

/*
 *   perfmon_pbx_fix_acquire_time - 
 *   return: none
 */
void
perfmon_pbx_fix_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
			      int cond_type, UINT64 amount)
{
  int module;
  int offset;

  assert (pstat_Global.initialized);

  module = perfmon_get_module_type (thread_p);

  assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
  assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
  assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
  assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
  assert (cond_type >= PERF_CONDITIONAL_FIX && cond_type < PERF_CONDITIONAL_FIX_CNT);
  assert (amount > 0);

  offset = PERF_PAGE_FIX_TIME_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type);
  assert (offset < PERF_PAGE_FIX_TIME_COUNTERS);

  perfmon_add_stat_at_offset (thread_p, PSTAT_PBX_FIX_TIME_COUNTERS, offset, amount);
}

/*
 *   perfmon_mvcc_snapshot - 
 *   return: none
 */
void
perfmon_mvcc_snapshot (THREAD_ENTRY * thread_p, int snapshot, int rec_type, int visibility)
{
  int offset;

  assert (pstat_Global.initialized);

  assert (snapshot >= PERF_SNAPSHOT_SATISFIES_DELETE && snapshot < PERF_SNAPSHOT_CNT);
  assert (rec_type >= PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED && rec_type < PERF_SNAPSHOT_RECORD_TYPE_CNT);
  assert (visibility >= PERF_SNAPSHOT_INVISIBLE && visibility < PERF_SNAPSHOT_VISIBILITY_CNT);
  offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);
  assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);

  perfmon_add_stat_at_offset (thread_p, PSTAT_MVCC_SNAPSHOT_COUNTERS, offset, 1);
}

#endif /* SERVER_MODE || SA_MODE */

int
perfmon_calc_diff_stats (UINT64 * stats_diff, UINT64 * new_stats, UINT64 * old_stats)
{
  int i, j;
  int offset;

  if (!stats_diff || !new_stats || !old_stats)
    {
      assert (false);
      return ER_FAILED;
    }

  offset = pstat_Metadata[PSTAT_PB_AVOID_VICTIM_CNT].start_offset;
  if (new_stats[offset] >= old_stats[offset])
    {
      stats_diff[offset] = new_stats[offset] - old_stats[offset];
    }
  else
    {
      stats_diff[offset] = 0;
    }

  for (i = 0; i < PSTAT_COUNT; i++)
    {
      switch (pstat_Metadata[i].valtype)
	{
	case PSTAT_ACCUMULATE_SINGLE_VALUE:
	case PSTAT_COUNTER_TIMER_VALUE:
	case PSTAT_COMPLEX_VALUE:
	case PSTAT_COMPUTED_RATIO_VALUE:
	  for (j = pstat_Metadata[i].start_offset; j < pstat_Metadata[i].start_offset + pstat_Metadata[i].n_vals; j++)
	    {
	      if (new_stats[j] >= old_stats[j])
		{
		  stats_diff[j] = new_stats[j] - old_stats[j];
		}
	      else
		{
		  stats_diff[j] = 0;
		}
	    }
	  break;

	case PSTAT_PEEK_SINGLE_VALUE:
	  if (i != PSTAT_PB_AVOID_VICTIM_CNT)
	    {
	      stats_diff[pstat_Metadata[i].start_offset] = new_stats[pstat_Metadata[i].start_offset];
	    }
	  break;
	default:
	  assert (false);
	  break;
	}
    }

  perfmon_server_calc_stats (stats_diff);
  return NO_ERROR;
}

/*
 *   perfmon_server_dump_stats_to_buffer -
 *   return: none
 *   stats(in) server statistics to print
 *   buffer(in):
 *   buf_size(in):
 *   substr(in):
 */
void
perfmon_server_dump_stats_to_buffer (const UINT64 * stats, char *buffer, int buf_size, const char *substr)
{
  int i;
  int ret;
  UINT64 *stats_ptr;
  int remained_size;
  const char *s;
  char *p;

  if (buffer == NULL || buf_size <= 0)
    {
      return;
    }

  p = buffer;
  remained_size = buf_size - 1;
  ret = snprintf (p, remained_size, "\n *** SERVER EXECUTION STATISTICS *** \n");
  remained_size -= ret;
  p += ret;

  if (remained_size <= 0)
    {
      return;
    }

  stats_ptr = (UINT64 *) stats;
  for (i = 0; i < PSTAT_COUNT; i++)
    {
      if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
	{
	  break;
	}

      if (substr != NULL)
	{
	  s = strstr (pstat_Metadata[i].stat_name, substr);
	}
      else
	{
	  s = pstat_Metadata[i].stat_name;
	}

      if (s)
	{
	  int offset = pstat_Metadata[i].start_offset;

	  if (pstat_Metadata[i].valtype != PSTAT_COMPUTED_RATIO_VALUE)
	    {
	      if (pstat_Metadata[i].valtype != PSTAT_COUNTER_TIMER_VALUE)
		{
		  ret = snprintf (p, remained_size, "%-29s = %10llu\n", pstat_Metadata[i].stat_name,
				  (unsigned long long) stats_ptr[offset]);
		}
	      else
		{
		  perfmon_print_timer_to_buffer (&p, i, stats_ptr, &remained_size);
		  ret = 0;
		}
	    }
	  else
	    {
	      ret = snprintf (p, remained_size, "%-29s = %10.2f\n", pstat_Metadata[i].stat_name,
			      (float) stats_ptr[offset] / 100);
	    }
	  remained_size -= ret;
	  p += ret;
	  if (remained_size <= 0)
	    {
	      assert (remained_size == 0);	/* should not overrun the buffer */
	      return;
	    }
	}
    }

  for (; i < PSTAT_COUNT && remained_size > 0; i++)
    {
      if (substr != NULL)
	{
	  s = strstr (pstat_Metadata[i].stat_name, substr);
	}
      else
	{
	  s = pstat_Metadata[i].stat_name;
	}
      if (s == NULL)
	{
	  continue;
	}

      ret = snprintf (p, remained_size, "%s:\n", pstat_Metadata[i].stat_name);
      remained_size -= ret;
      p += ret;
      if (remained_size <= 0)
	{
	  assert (remained_size == 0);	/* should not overrun the buffer */
	  return;
	}
      pstat_Metadata[i].f_dump_in_buffer (&p, &(stats[pstat_Metadata[i].start_offset]), &remained_size);
    }

  buffer[buf_size - 1] = '\0';
}

/*
 *   perfmon_server_dump_stats - Print the given server statistics
 *   return: none
 *   stats(in) server statistics to print
 *   stream(in): if NULL is given, stdout is used
 */
void
perfmon_server_dump_stats (const UINT64 * stats, FILE * stream, const char *substr)
{
  int i;
  UINT64 *stats_ptr;
  const char *s;

  if (stream == NULL)
    {
      stream = stdout;
    }

  fprintf (stream, "\n *** SERVER EXECUTION STATISTICS *** \n");

  stats_ptr = (UINT64 *) stats;
  for (i = 0; i < PSTAT_COUNT; i++)
    {
      if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
	{
	  break;
	}

      if (substr != NULL)
	{
	  s = strstr (pstat_Metadata[i].stat_name, substr);
	}
      else
	{
	  s = pstat_Metadata[i].stat_name;
	}

      if (s)
	{
	  int offset = pstat_Metadata[i].start_offset;

	  if (pstat_Metadata[i].valtype != PSTAT_COMPUTED_RATIO_VALUE)
	    {
	      if (pstat_Metadata[i].valtype != PSTAT_COUNTER_TIMER_VALUE)
		{
		  fprintf (stream, "%-29s = %10llu\n", pstat_Metadata[i].stat_name,
			   (unsigned long long) stats_ptr[offset]);
		}
	      else
		{
		  perfmon_print_timer_to_file (stream, i, stats_ptr);
		}
	    }
	  else
	    {
	      fprintf (stream, "%-29s = %10.2f\n", pstat_Metadata[i].stat_name, (float) stats_ptr[offset] / 100);
	    }
	}
    }

  for (; i < PSTAT_COUNT; i++)
    {
      if (substr != NULL)
	{
	  s = strstr (pstat_Metadata[i].stat_name, substr);
	}
      else
	{
	  s = pstat_Metadata[i].stat_name;
	}
      if (s == NULL)
	{
	  continue;
	}

      fprintf (stream, "%s:\n", pstat_Metadata[i].stat_name);
      pstat_Metadata[i].f_dump_in_file (stream, &(stats[pstat_Metadata[i].start_offset]));
    }
}

/*
 *   perfmon_get_current_times - Get current CPU and elapsed times
 *   return:
 *   cpu_user_time(out):
 *   cpu_sys_time(out):
 *   elapsed_time(out):
 *
 * Note:
 */
void
perfmon_get_current_times (time_t * cpu_user_time, time_t * cpu_sys_time, time_t * elapsed_time)
{
#if defined (WINDOWS)
  *cpu_user_time = 0;
  *cpu_sys_time = 0;
  *elapsed_time = 0;

  *elapsed_time = time (NULL);
#else /* WINDOWS */
  struct rusage rusage;

  *cpu_user_time = 0;
  *cpu_sys_time = 0;
  *elapsed_time = 0;

  *elapsed_time = time (NULL);

  if (getrusage (RUSAGE_SELF, &rusage) == 0)
    {
      *cpu_user_time = rusage.ru_utime.tv_sec;
      *cpu_sys_time = rusage.ru_stime.tv_sec;
    }
#endif /* WINDOWS */
}

/*
 *   perfmon_server_calc_stats - Do post processing of server statistics
 *   return: none
 *   stats(in/out): server statistics block to be processed
 */
static void
perfmon_server_calc_stats (UINT64 * stats)
{
  int page_type;
  int module;
  int offset;
  int buf_dirty;
  int holder_dirty;
  int holder_latch;
  int page_found_mode;
  int cond_type;
  int promote_cond;
  int success;
  UINT64 counter = 0;
  UINT64 total_unfix_vacuum = 0;
  UINT64 total_unfix_vacuum_dirty = 0;
  UINT64 total_unfix = 0;
  UINT64 total_fix_vacuum = 0;
  UINT64 total_fix_vacuum_hit = 0;
  UINT64 fix_time_usec = 0;
  UINT64 lock_time_usec = 0;
  UINT64 hold_time_usec = 0;
  UINT64 total_promote_time = 0;
  int i;

  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	{
	  for (buf_dirty = 0; buf_dirty <= 1; buf_dirty++)
	    {
	      for (holder_dirty = 0; holder_dirty <= 1; holder_dirty++)
		{
		  for (holder_latch = PERF_HOLDER_LATCH_READ; holder_latch < PERF_HOLDER_LATCH_CNT; holder_latch++)
		    {
		      offset = PERF_PAGE_UNFIX_STAT_OFFSET (module, page_type, buf_dirty, holder_dirty, holder_latch);

		      assert (offset < PERF_PAGE_UNFIX_COUNTERS);
		      counter = stats[pstat_Metadata[PSTAT_PBX_UNFIX_COUNTERS].start_offset + offset];

		      total_unfix += counter;
		      if (module == PERF_MODULE_VACUUM)
			{
			  total_unfix_vacuum += counter;
			  if (holder_dirty == 1)
			    {
			      total_unfix_vacuum_dirty += counter;
			    }
			}
		    }
		}
	    }
	}
    }

  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	{
	  for (page_found_mode = PERF_PAGE_MODE_OLD_LOCK_WAIT; page_found_mode < PERF_PAGE_MODE_CNT; page_found_mode++)
	    {
	      for (holder_latch = PERF_HOLDER_LATCH_READ; holder_latch < PERF_HOLDER_LATCH_CNT; holder_latch++)
		{
		  offset = PERF_PAGE_HOLD_TIME_OFFSET (module, page_type, page_found_mode, holder_latch);
		  assert (offset < PERF_PAGE_HOLD_TIME_COUNTERS);
		  counter = stats[pstat_Metadata[PSTAT_PBX_HOLD_TIME_COUNTERS].start_offset + offset];

		  if (page_type != PAGE_LOG && counter > 0)
		    {
		      hold_time_usec += counter;
		    }

		  for (cond_type = PERF_CONDITIONAL_FIX; cond_type < PERF_CONDITIONAL_FIX_CNT; cond_type++)
		    {
		      offset = PERF_PAGE_FIX_TIME_OFFSET (module, page_type, page_found_mode, holder_latch, cond_type);
		      assert (offset < PERF_PAGE_FIX_TIME_COUNTERS);
		      counter = stats[pstat_Metadata[PSTAT_PBX_FIX_TIME_COUNTERS].start_offset + offset];
		      /* do not include fix time of log pages */
		      if (page_type != PAGE_LOG && counter > 0)
			{
			  fix_time_usec += counter;
			}

		      offset = PERF_PAGE_LOCK_TIME_OFFSET (module, page_type, page_found_mode, holder_latch, cond_type);
		      assert (offset < PERF_PAGE_LOCK_TIME_COUNTERS);
		      counter = stats[pstat_Metadata[PSTAT_PBX_LOCK_TIME_COUNTERS].start_offset + offset];

		      if (page_type != PAGE_LOG && counter > 0)
			{
			  lock_time_usec += counter;
			}

		      if (module == PERF_MODULE_VACUUM && page_found_mode != PERF_PAGE_MODE_NEW_LOCK_WAIT
			  && page_found_mode != PERF_PAGE_MODE_NEW_NO_WAIT)
			{
			  offset =
			    PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_found_mode, holder_latch, cond_type);

			  assert (offset < PERF_PAGE_FIX_COUNTERS);
			  counter = stats[pstat_Metadata[PSTAT_PBX_FIX_COUNTERS].start_offset + offset];

			  if (module == PERF_MODULE_VACUUM)
			    {
			      total_fix_vacuum += counter;
			      if (page_found_mode == PERF_PAGE_MODE_OLD_IN_BUFFER)
				{
				  total_fix_vacuum_hit += counter;
				}
			    }
			}
		    }
		}
	    }
	}
    }

  stats[pstat_Metadata[PSTAT_PB_VACUUM_EFFICIENCY].start_offset] =
    SAFE_DIV (total_unfix_vacuum_dirty * 100 * 100, total_unfix_vacuum);

  stats[pstat_Metadata[PSTAT_PB_VACUUM_FETCH_RATIO].start_offset] =
    SAFE_DIV (total_unfix_vacuum * 100 * 100, total_unfix);

  stats[pstat_Metadata[PSTAT_VACUUM_DATA_HIT_RATIO].start_offset] =
    SAFE_DIV (total_fix_vacuum_hit * 100 * 100, total_fix_vacuum);

  stats[pstat_Metadata[PSTAT_PB_HIT_RATIO].start_offset] =
    SAFE_DIV ((stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset] -
	       stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset]) * 100 * 100,
	      stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset]);

  stats[pstat_Metadata[PSTAT_LOG_HIT_RATIO].start_offset] =
    SAFE_DIV ((stats[pstat_Metadata[PSTAT_LOG_NUM_FETCHES].start_offset]
	       - stats[pstat_Metadata[PSTAT_LOG_NUM_IOREADS].start_offset]) * 100 * 100,
	      stats[pstat_Metadata[PSTAT_LOG_NUM_FETCHES].start_offset]);

  stats[pstat_Metadata[PSTAT_PB_PAGE_LOCK_ACQUIRE_TIME_10USEC].start_offset] = 100 * lock_time_usec / 1000;
  stats[pstat_Metadata[PSTAT_PB_PAGE_HOLD_ACQUIRE_TIME_10USEC].start_offset] = 100 * hold_time_usec / 1000;
  stats[pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset] = 100 * fix_time_usec / 1000;

  stats[pstat_Metadata[PSTAT_PB_PAGE_ALLOCATE_TIME_RATIO].start_offset] =
    SAFE_DIV ((stats[pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset] -
	       stats[pstat_Metadata[PSTAT_PB_PAGE_HOLD_ACQUIRE_TIME_10USEC].start_offset] -
	       stats[pstat_Metadata[PSTAT_PB_PAGE_LOCK_ACQUIRE_TIME_10USEC].start_offset]) * 100 * 100,
	      stats[pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset]);

  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	{
	  for (promote_cond = PERF_PROMOTE_ONLY_READER; promote_cond < PERF_PROMOTE_CONDITION_CNT; promote_cond++)
	    {
	      for (holder_latch = PERF_HOLDER_LATCH_READ; holder_latch < PERF_HOLDER_LATCH_CNT; holder_latch++)
		{
		  for (success = 0; success < 2; success++)
		    {
		      offset = PERF_PAGE_PROMOTE_STAT_OFFSET (module, page_type, promote_cond, holder_latch, success);
		      assert (offset < PERF_PAGE_PROMOTE_COUNTERS);

		      counter = stats[pstat_Metadata[PSTAT_PBX_PROMOTE_TIME_COUNTERS].start_offset + offset];
		      if (counter)
			{
			  total_promote_time += counter;
			}

		      counter = stats[pstat_Metadata[PSTAT_PBX_PROMOTE_COUNTERS].start_offset + offset];
		      if (counter)
			{
			  if (success)
			    {
			      stats[pstat_Metadata[PSTAT_PB_PAGE_PROMOTE_SUCCESS].start_offset] += counter;
			    }
			  else
			    {
			      stats[pstat_Metadata[PSTAT_PB_PAGE_PROMOTE_FAILED].start_offset] += counter;
			    }
			}
		    }
		}
	    }
	}
    }

  stats[pstat_Metadata[PSTAT_PB_PAGE_PROMOTE_TOTAL_TIME_10USEC].start_offset] = 100 * total_promote_time / 1000;
  stats[pstat_Metadata[PSTAT_PB_PAGE_PROMOTE_SUCCESS].start_offset] *= 100;
  stats[pstat_Metadata[PSTAT_PB_PAGE_PROMOTE_FAILED].start_offset] *= 100;

#if defined (SERVER_MODE)
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
#endif

  for (i = 0; i < PSTAT_COUNT; i++)
    {
      if (pstat_Metadata[i].valtype == PSTAT_COUNTER_TIMER_VALUE)
	{
	  int offset = pstat_Metadata[i].start_offset;
	  stats[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)]
	    = SAFE_DIV (stats[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)],
			stats[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)]);
	}
    }
}

      assert (pstat_Metadata[idx].psid == (PERF_STAT_ID) idx);
  pstat_Global.initialized = true;
  return NO_ERROR;
#else
#endif /* SERVER_MODE || SA_MODE */

/*
 * Watcher section.
 */

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

  memset (pstat_Global.tran_stats[tran_index], 0, PERFMON_VALUES_MEMSIZE);
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

int
perfmon_get_number_of_statistic_values (void)
{
  return pstat_Global.n_stat_values;
}

/*
 * perfmon_allocate_values () - Allocate PERFMON_VALUES_MEMSIZE bytes 
 * 
 */
UINT64 *
perfmon_allocate_values (void)
{
  UINT64 *vals;

  vals = (UINT64 *) malloc (PERFMON_VALUES_MEMSIZE);
  if (vals == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, PERFMON_VALUES_MEMSIZE);
    }

  return vals;
}

/*
 * perfmon_allocate_packed_values_buffer () - Allocate PERFMON_VALUES_MEMSIZE bytes and verify alignment
 * 
 */
char *
perfmon_allocate_packed_values_buffer (void)
{
  char *buf;

  buf = (char *) malloc (PERFMON_VALUES_MEMSIZE);
  if (buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, PERFMON_VALUES_MEMSIZE);
    }
  ASSERT_ALIGN (buf, MAX_ALIGNMENT);

  return buf;
}

/*
 * perfmon_copy_values () -
 *
 * dest (in/out): destination buffer
 * source (in): source buffer
 * 
 */
void
perfmon_copy_values (UINT64 * dest, UINT64 * src)
{
  memcpy (dest, src, PERFMON_VALUES_MEMSIZE);
}

/*
 * perfmon_pack_stats - Pack the statistic values in the buffer
 *
 * return:
 *
 *   buf (in):
 *   stats (in):
 *
 *
 */
char *
perfmon_pack_stats (char *buf, UINT64 * stats)
{
  char *ptr;
  int i;

  ptr = buf;

  for (i = 0; i < pstat_Global.n_stat_values; i++)
    {
      OR_PUT_INT64 (ptr, &(stats[i]));
      ptr += OR_INT64_SIZE;
    }

  return (ptr);
}

/*
 * perfmon_unpack_stats - Unpack the values from the buffer in the statistics array
 *
 * return:
 *
 *   buf (in):
 *   stats (out):
 *
 */
char *
perfmon_unpack_stats (char *buf, UINT64 * stats)
{
  char *ptr;
  int i;

  ptr = buf;

  for (i = 0; i < pstat_Global.n_stat_values; i++)
    {
      OR_GET_INT64 (ptr, &(stats[i]));
      ptr += OR_INT64_SIZE;
    }

  return (ptr);
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
}

/*
 * perfmon_print_timer_to_buffer - Print in a buffer the statistic values for a timer type statistic
 *
 * s (in/out): input stream
 * stat_index (in): statistic index
 * stats_ptr (in): statistic values array
 * offset (in): offset in the statistics array
 * remained_size (in/out): remained size to write in the buffer 
 *
 * return: void
 *
 */
static void
perfmon_print_timer_to_buffer (char **s, int stat_index, UINT64 * stats_ptr, int *remained_size)
{
  int ret;
  int offset = pstat_Metadata[stat_index].start_offset;

  assert (pstat_Metadata[stat_index].valtype == PSTAT_COUNTER_TIMER_VALUE);
  ret = snprintf (*s, *remained_size, "The timer values for %s are:\n", pstat_Metadata[stat_index].stat_name);
  *remained_size -= ret;
  *s += ret;
  ret = snprintf (*s, *remained_size, "Num_%-25s = %10llu\n", pstat_Metadata[stat_index].stat_name,
		  (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)]);
  *remained_size -= ret;
  *s += ret;
  ret = snprintf (*s, *remained_size, "Total_time_%-18s = %10llu\n", pstat_Metadata[stat_index].stat_name,
		  (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)]);
  *remained_size -= ret;
  *s += ret;
  ret = snprintf (*s, *remained_size, "Max_time_%-20s = %10llu\n", pstat_Metadata[stat_index].stat_name,
		  (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)]);
  *remained_size -= ret;
  *s += ret;
  ret = snprintf (*s, *remained_size, "Avg_time_%-20s = %10llu\n", pstat_Metadata[stat_index].stat_name,
		  (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)]);
  *remained_size -= ret;
  *s += ret;
}

/*
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

    pstat_Global.n_stat_values = 0;
    pstat_Global.global_stats = NULL;
    pstat_Global.n_trans = 0;
    pstat_Global.tran_stats = NULL;
    pstat_Global.is_watching = NULL;
    pstat_Global.n_watchers = 0;
    pstat_Global.initialized = false;
    pstat_Global.activation_flag = prm_get_integer_value (PRM_ID_EXTENDED_STATISTICS_ACTIVATION);

    pstat_Global.n_stat_values = metadata_initialize();

#if defined (SERVER_MODE) || defined (SA_MODE)

#if !defined (HAVE_ATOMIC_BUILTINS)
    (void) pthread_mutex_init (&pstat_Global.watch_lock, NULL);
#endif /* !HAVE_ATOMIC_BUILTINS */

    /* Allocate global stats. */
    pstat_Global.global_stats = (UINT64 *) malloc (PERFMON_VALUES_MEMSIZE);
    if (pstat_Global.global_stats == NULL)
    {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, PERFMON_VALUES_MEMSIZE);
	goto error;
    }
    memset (pstat_Global.global_stats, 0, PERFMON_VALUES_MEMSIZE);

    assert (num_trans > 0);

    pstat_Global.n_trans = num_trans + 1;	/* 1 more for easier indexing with tran_index */
    memsize = pstat_Global.n_trans * sizeof (UINT64 *);
    pstat_Global.tran_stats = (UINT64 **) malloc (memsize);
    if (pstat_Global.tran_stats == NULL)
    {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
	goto error;
    }
    memsize = pstat_Global.n_trans * PERFMON_VALUES_MEMSIZE;
    pstat_Global.tran_stats[0] = (UINT64 *) malloc (memsize);
    if (pstat_Global.tran_stats[0] == NULL)
    {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, memsize);
	goto error;
    }
    memset (pstat_Global.tran_stats[0], 0, memsize);

    for (idx = 1; idx < pstat_Global.n_trans; idx++)
    {
	pstat_Global.tran_stats[idx] = pstat_Global.tran_stats[0] + pstat_Global.n_stat_values * idx;
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
    goto exit;

    error:
    perfmon_finalize ();
    return ER_OUT_OF_VIRTUAL_MEMORY;
#endif /* SERVER_MODE || SA_MODE */

    exit:
    pstat_Global.initialized = true;
    return NO_ERROR;
}


/*
 * perfmon_finalize () - Frees all the allocated memory for performance monitor data structures
 *
 * return :
 */

void
perfmon_finalize (void)
{
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