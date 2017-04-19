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
 * perf_monitor.h - Monitor execution statistics at Client
 */

#ifndef _PERF_MONITOR_H_
#define _PERF_MONITOR_H_

#ident "$Id$"

#include <stdio.h>

#include "memory_alloc.h"
#include "storage_common.h"
#include "perf_metadata.h"

#if defined (SERVER_MODE)
#include "dbtype.h"
#include "connection_defs.h"
#endif /* SERVER_MODE */

#include "thread.h"

#include <time.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* WINDOWS */

#include "tsc_timer.h"
#include <assert.h>

/* EXPORTED GLOBAL DEFINITIONS */
#define MAX_DIAG_DATA_VALUE     0xfffffffffffffLL

#define MAX_SERVER_THREAD_COUNT         500
#define MAX_SERVER_NAMELENGTH           256
#define SH_MODE 0644

/* Statistics activation flags */

#define PERFMON_ACTIVE_DEFAULT                    0
#define PERFMON_ACTIVE_DETAILED_BTREE_PAGE        1
#define PERFMON_ACTIVE_MVCC_SNAPSHOT              2
#define PERFMON_ACTIVE_LOCK_OBJECT                4
#define PERFMON_ACTIVE_PB_HASH_ANCHOR             8
#define PERFMON_ACTIVE_PB_VICTIMIZATION           16
#define PERFMON_ACTIVE_MAX_VALUE                  31	/* must update when adding new conditions */

#define SAFE_DIV(a, b) ((b) == 0 ? 0 : (a) / (b))

#if !defined(SERVER_MODE)
#if !defined(LOG_TRAN_INDEX)
#define LOG_TRAN_INDEX
extern int log_Tran_index;	/* Index onto transaction table for current thread of execution (client) */
#endif /* !LOG_TRAN_INDEX */
#endif /* !SERVER_MODE */

#if defined (SERVER_MODE)
#if !defined(LOG_FIND_THREAD_TRAN_INDEX)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) \
  ((thrd) ? (thrd)->tran_index : thread_get_current_tran_index())
#endif
#else
#if !defined(LOG_FIND_THREAD_TRAN_INDEX)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) (log_Tran_index)
#endif
#endif

/* All globals on statistics will be here. */
typedef struct pstat_global PSTAT_GLOBAL;
struct pstat_global
{
  int n_stat_values;

  UINT64 *global_stats;

  int n_trans;
  UINT64 **tran_stats;

  bool *is_watching;
#if !defined (HAVE_ATOMIC_BUILTINS)
  pthread_mutex_t watch_lock;
#endif				/* !HAVE_ATOMIC_BUILTINS */

  INT32 n_watchers;

  bool initialized;
  int activation_flag;
};
extern PSTAT_GLOBAL pstat_Global;

typedef struct diag_sys_config DIAG_SYS_CONFIG;
struct diag_sys_config
{
  int Executediag;
  int DiagSM_ID_server;
  int server_long_query_time;	/* min 1 sec */
};

typedef struct t_diag_monitor_db_value T_DIAG_MONITOR_DB_VALUE;
struct t_diag_monitor_db_value
{
  INT64 query_open_page;
  INT64 query_opened_page;
  INT64 query_slow_query;
  INT64 query_full_scan;
  INT64 conn_cli_request;
  INT64 conn_aborted_clients;
  INT64 conn_conn_req;
  INT64 conn_conn_reject;
  INT64 buffer_page_write;
  INT64 buffer_page_read;
  INT64 lock_deadlock;
  INT64 lock_request;
};

typedef struct t_diag_monitor_cas_value T_DIAG_MONITOR_CAS_VALUE;
struct t_diag_monitor_cas_value
{
  INT64 reqs_in_interval;
  INT64 transactions_in_interval;
  INT64 query_in_interval;
  int active_sessions;
};

/* Monitor config related structure */

typedef struct monitor_cas_config MONITOR_CAS_CONFIG;
struct monitor_cas_config
{
  char head;
  char body[2];
};

typedef struct monitor_server_config MONITOR_SERVER_CONFIG;
struct monitor_server_config
{
  char head[2];
  char body[8];
};

typedef struct t_client_monitor_config T_CLIENT_MONITOR_CONFIG;
struct t_client_monitor_config
{
  MONITOR_CAS_CONFIG cas;
  MONITOR_SERVER_CONFIG server;
};

/* Shared memory data struct */

typedef struct t_shm_diag_info_server T_SHM_DIAG_INFO_SERVER;
struct t_shm_diag_info_server
{
  int magic;
  int num_thread;
  int magic_key;
  char servername[MAX_SERVER_NAMELENGTH];
  T_DIAG_MONITOR_DB_VALUE thread[MAX_SERVER_THREAD_COUNT];
};

enum t_diag_shm_mode
{
  DIAG_SHM_MODE_ADMIN = 0,
  DIAG_SHM_MODE_MONITOR = 1
};
typedef enum t_diag_shm_mode T_DIAG_SHM_MODE;

enum t_diag_server_type
{
  DIAG_SERVER_DB = 00000,
  DIAG_SERVER_CAS = 10000,
  DIAG_SERVER_DRIVER = 20000,
  DIAG_SERVER_RESOURCE = 30000
};
typedef enum t_diag_server_type T_DIAG_SERVER_TYPE;

extern void perfmon_get_current_times (time_t * cpu_usr_time, time_t * cpu_sys_time, time_t * elapsed_time);

extern int perfmon_calc_diff_stats (UINT64 * stats_diff, UINT64 * new_stats, UINT64 * old_stats);
extern int perfmon_initialize (int num_trans);
extern void perfmon_finalize (void);
extern int perfmon_get_number_of_statistic_values (void);
extern UINT64 *perfmon_allocate_values (void);
extern char *perfmon_allocate_packed_values_buffer (void);
extern void perfmon_copy_values (UINT64 * src, UINT64 * dest);

#if defined (SERVER_MODE) || defined (SA_MODE)
extern void perfmon_start_watch (THREAD_ENTRY * thread_p);
extern void perfmon_stop_watch (THREAD_ENTRY * thread_p);
#endif /* SERVER_MODE || SA_MODE */

STATIC_INLINE bool perfmon_is_perf_tracking (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool perfmon_is_perf_tracking_and_active (int activation_flag) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool perfmon_is_perf_tracking_force (bool always_collect) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_add_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, UINT64 amount)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_add_stat_to_global (PERF_STAT_ID psid, UINT64 amount) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_add_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 amount)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_inc_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_inc_stat_to_global (PERF_STAT_ID psid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, int statval, bool check_watchers)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_at_offset (THREAD_ENTRY * thread_p, int offset, int statval, bool check_watchers)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_add_at_offset_to_global (int offset, UINT64 amount) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_stat_to_global (PERF_STAT_ID psid, int statval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_at_offset_to_global (int offset, int statval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_time_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 timediff)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_time_bulk_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 timediff, UINT64 count)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_time_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, UINT64 timediff)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int perfmon_get_activation_flag (void) __attribute__ ((ALWAYS_INLINE));

/*
 *  Add/set stats section.
 */

/*
 * perfmon_add_stat () - Accumulate amount to statistic.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * psid (in)	 : Statistic ID.
 * amount (in)	 : Amount to add.
 */
STATIC_INLINE void
perfmon_add_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, UINT64 amount)
{
  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);

  if (!perfmon_is_perf_tracking ())
    {
      /* No need to collect statistics since no one is interested. */
      return;
    }

  assert (pstat_Metadata[psid].valtype == PSTAT_ACCUMULATE_SINGLE_VALUE);

  /* Update statistics. */
  perfmon_add_at_offset (thread_p, pstat_Metadata[psid].start_offset, amount);
}

/*
 * perfmon_add_stat_to_global () - Accumulate amount only to global statistic.
 *
 * return	 : Void.
 * psid (in)	 : Statistic ID.
 * amount (in)	 : Amount to add.
 */
STATIC_INLINE void
perfmon_add_stat_to_global (PERF_STAT_ID psid, UINT64 amount)
{
  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);

  if (!pstat_Global.initialized)
    {
      return;
    }

  assert (pstat_Metadata[psid].valtype == PSTAT_ACCUMULATE_SINGLE_VALUE);

  /* Update statistics. */
  perfmon_add_at_offset_to_global (pstat_Metadata[psid].start_offset, amount);
}

/*
 * perfmon_inc_stat () - Increment statistic value by 1.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * psid (in)	 : Statistic ID.
 */
STATIC_INLINE void
perfmon_inc_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid)
{
  perfmon_add_stat (thread_p, psid, 1);
}

/*
 * perfmon_inc_stat_to_global () - Increment global statistic value by 1.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * psid (in)	 : Statistic ID.
 */
STATIC_INLINE void
perfmon_inc_stat_to_global (PERF_STAT_ID psid)
{
  perfmon_add_stat_to_global (psid, 1);
}

/*
 * perfmon_add_at_offset () - Add amount to statistic in global/local at offset.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * offset (in)	 : Offset to statistics value.
 * amount (in)	 : Amount to add.
 */
STATIC_INLINE void
perfmon_add_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 amount)
{
#if defined (SERVER_MODE) || defined (SA_MODE)
  int tran_index;
#endif /* SERVER_MODE || SA_MODE */

  assert (offset >= 0 && offset < pstat_Global.n_stat_values);
  assert (pstat_Global.initialized);

  /* Update global statistic. */
  ATOMIC_INC_64 (&(pstat_Global.global_stats[offset]), amount);

#if defined (SERVER_MODE) || defined (SA_MODE)
  /* Update local statistic */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0 && tran_index < pstat_Global.n_trans);
  if (pstat_Global.is_watching[tran_index])
    {
      assert (pstat_Global.tran_stats[tran_index] != NULL);
      pstat_Global.tran_stats[tran_index][offset] += amount;
    }
#endif /* SERVER_MODE || SA_MODE */
}

/*
 * perfmon_add_at_offset_to_global () - Add amount to statistic in global
 *
 * return	 : Void.
 * offset (in)	 : Offset to statistics value.
 * amount (in)	 : Amount to add.
 */
STATIC_INLINE void
perfmon_add_at_offset_to_global (int offset, UINT64 amount)
{
  assert (offset >= 0 && offset < pstat_Global.n_stat_values);
  assert (pstat_Global.initialized);

  /* Update global statistic. */
  ATOMIC_INC_64 (&(pstat_Global.global_stats[offset]), amount);
}

/*
 * perfmon_set_stat () - Set statistic value.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * psid (in)	 : Statistic ID.
 * statval (in)  : New statistic value.
 * always_collect (in): Flag that tells that we should always collect statistics
 */
STATIC_INLINE void
perfmon_set_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, int statval, bool always_collect)
{
  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);

  if (!perfmon_is_perf_tracking_force (always_collect))
    {
      /* No need to collect statistics since no one is interested. */
      return;
    }

  assert (pstat_Metadata[psid].valtype == PSTAT_PEEK_SINGLE_VALUE);

  perfmon_set_at_offset (thread_p, pstat_Metadata[psid].start_offset, statval, always_collect);
}

/*
 * perfmon_set_at_offset () - Set statistic value in global/local at offset.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * offset (in)   : Offset to statistic value.
 * statval (in)	 : New statistic value.
 * always_collect (in): Flag that tells that we should always collect statistics
 */
STATIC_INLINE void
perfmon_set_at_offset (THREAD_ENTRY * thread_p, int offset, int statval, bool always_collect)
{
#if defined (SERVER_MODE) || defined (SA_MODE)
  int tran_index;
#endif /* SERVER_MODE || SA_MODE */

  assert (offset >= 0 && offset < pstat_Global.n_stat_values);
  assert (pstat_Global.initialized);

  /* Update global statistic. */
  ATOMIC_TAS_64 (&(pstat_Global.global_stats[offset]), statval);

#if defined (SERVER_MODE) || defined (SA_MODE)
  /* Update local statistic */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0 && tran_index < pstat_Global.n_trans);
  if (always_collect || pstat_Global.is_watching[tran_index])
    {
      assert (pstat_Global.tran_stats[tran_index] != NULL);
      pstat_Global.tran_stats[tran_index][offset] = statval;
    }
#endif /* SERVER_MODE || SA_MODE */
}

/*
 * perfmon_set_stat_to_global () - Set statistic value only for global statistics
 *
 * return	 : Void.
 * psid (in)	 : Statistic ID.
 * statval (in)  : New statistic value.
 */
STATIC_INLINE void
perfmon_set_stat_to_global (PERF_STAT_ID psid, int statval)
{
  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);

  if (!pstat_Global.initialized)
    {
      return;
    }

  assert (pstat_Metadata[psid].valtype == PSTAT_PEEK_SINGLE_VALUE);

  perfmon_set_at_offset_to_global (pstat_Metadata[psid].start_offset, statval);
}

/*
 * perfmon_set_at_offset_to_global () - Set statistic value in global offset.
 *
 * return	 : Void.
 * offset (in)   : Offset to statistic value.
 * statval (in)	 : New statistic value.
 */
STATIC_INLINE void
perfmon_set_at_offset_to_global (int offset, int statval)
{
  assert (offset >= 0 && offset < pstat_Global.n_stat_values);
  assert (pstat_Global.initialized);

  /* Update global statistic. */
  ATOMIC_TAS_64 (&(pstat_Global.global_stats[offset]), statval);
}

/*
 * perfmon_time_stat () - Register statistic timer value. Counter, total time and maximum time are updated.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * psid (in)	 : Statistic ID.
 * timediff (in) : Time difference to register.
 */
STATIC_INLINE void
perfmon_time_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, UINT64 timediff)
{
  assert (pstat_Global.initialized);
  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);

  assert (pstat_Metadata[psid].valtype == PSTAT_COUNTER_TIMER_VALUE);

  perfmon_time_at_offset (thread_p, pstat_Metadata[psid].start_offset, timediff);
}

/*
 * perfmon_time_at_offset () - Register timer statistics in global/local at offset.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * offset (in)   : Offset to timer values.
 * timediff (in) : Time difference to add to timer.
 *
 * NOTE: There will be three values modified: counter, total time and max time.
 */
STATIC_INLINE void
perfmon_time_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 timediff)
{
  /* Update global statistics */
  UINT64 *statvalp = NULL;
  UINT64 max_time;
#if defined (SERVER_MODE) || defined (SA_MODE)
  int tran_index;
#endif /* SERVER_MODE || SA_MODE */

  assert (offset >= 0 && offset < pstat_Global.n_stat_values);
  assert (pstat_Global.initialized);

  /* Update global statistics. */
  statvalp = pstat_Global.global_stats + offset;
  ATOMIC_INC_64 (PSTAT_COUNTER_TIMER_COUNT_VALUE (statvalp), 1);
  ATOMIC_INC_64 (PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (statvalp), timediff);
  do
    {
      max_time = ATOMIC_LOAD_64 (PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp));
      if (max_time >= timediff)
	{
	  /* No need to change max_time. */
	  break;
	}
    }
  while (!ATOMIC_CAS_64 (PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp), max_time, timediff));
  /* Average is not computed here. */

#if defined (SERVER_MODE) || defined (SA_MODE)
  /* Update local statistic */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0 && tran_index < pstat_Global.n_trans);
  if (pstat_Global.is_watching[tran_index])
    {
      assert (pstat_Global.tran_stats[tran_index] != NULL);
      statvalp = pstat_Global.tran_stats[tran_index] + offset;
      (*PSTAT_COUNTER_TIMER_COUNT_VALUE (statvalp)) += 1;
      (*PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (statvalp)) += timediff;
      max_time = *PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp);
      if (max_time < timediff)
	{
	  (*PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp)) = timediff;
	}
    }
#endif /* SERVER_MODE || SA_MODE */
}

/*
 * perfmon_time_bulk_stat () - Register statistic timer value. Counter, total time and maximum time are updated.
 *                             Used to count and time multiple units at once (as opposed to perfmon_time_stat which
 *                             increments count by one).
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * psid (in)	 : Statistic ID.
 * timediff (in) : Time difference to register.
 * count (in)    : Unit count.
 */
STATIC_INLINE void
perfmon_time_bulk_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, UINT64 timediff, UINT64 count)
{
  assert (pstat_Global.initialized);
  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);

  assert (pstat_Metadata[psid].valtype == PSTAT_COUNTER_TIMER_VALUE);

  perfmon_time_bulk_at_offset (thread_p, pstat_Metadata[psid].start_offset, timediff, count);
}

/*
 * perfmon_time_bulk_at_offset () - Register timer statistics in global/local at offset for multiple units at once.
 *
 * return	 : Void.
 * thread_p (in) : Thread entry.
 * offset (in)   : Offset to timer values.
 * timediff (in) : Time difference to add to timer.
 * count (in)    : Unit count timed at once
 *
 * NOTE: There will be three values modified: counter, total time and max time.
 */
STATIC_INLINE void
perfmon_time_bulk_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 timediff, UINT64 count)
{
  /* Update global statistics */
  UINT64 *statvalp = NULL;
  UINT64 max_time;
  UINT64 time_per_unit;
#if defined (SERVER_MODE) || defined (SA_MODE)
  int tran_index;
#endif /* SERVER_MODE || SA_MODE */

  assert (offset >= 0 && offset < pstat_Global.n_stat_values);
  assert (pstat_Global.initialized);

  if (count == 0)
    {
      return;
    }
  time_per_unit = timediff / count;

  /* Update global statistics. */
  statvalp = pstat_Global.global_stats + offset;
  ATOMIC_INC_64 (PSTAT_COUNTER_TIMER_COUNT_VALUE (statvalp), count);
  ATOMIC_INC_64 (PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (statvalp), timediff);
  do
    {
      max_time = ATOMIC_LOAD_64 (PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp));
      if (max_time >= time_per_unit)
	{
	  /* No need to change max_time. */
	  break;
	}
    }
  while (!ATOMIC_CAS_64 (PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp), max_time, time_per_unit));
  /* Average is not computed here. */

#if defined (SERVER_MODE) || defined (SA_MODE)
  /* Update local statistic */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0 && tran_index < pstat_Global.n_trans);
  if (pstat_Global.is_watching[tran_index])
    {
      assert (pstat_Global.tran_stats[tran_index] != NULL);
      statvalp = pstat_Global.tran_stats[tran_index] + offset;
      (*PSTAT_COUNTER_TIMER_COUNT_VALUE (statvalp)) += count;
      (*PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (statvalp)) += timediff;
      max_time = *PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp);
      if (max_time < time_per_unit)
	{
	  (*PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp)) = time_per_unit;
	}
    }
#endif /* SERVER_MODE || SA_MODE */
}

/*
 * perfmon_get_activation_flag - Get the activation flag
 *
 * return: int
 */
STATIC_INLINE int
perfmon_get_activation_flag (void)
{
  return pstat_Global.activation_flag;
}

/*
 * perfmon_is_perf_tracking () - Returns true if there are active threads
 *
 * return	 : true or false
 */
STATIC_INLINE bool
perfmon_is_perf_tracking (void)
{
  return pstat_Global.initialized && pstat_Global.n_watchers > 0;
}

/*
 * perfmon_is_perf_tracking_and_active () - Returns true if there are active threads
 *					    and the activation_flag of the extended statistic is activated
 *
 * return	        : true or false
 * activation_flag (in) : activation flag for extended statistic
 *
 */
STATIC_INLINE bool
perfmon_is_perf_tracking_and_active (int activation_flag)
{
  return perfmon_is_perf_tracking () && (activation_flag & pstat_Global.activation_flag);
}


/*
 * perfmon_is_perf_tracking_force () - Skips the check for active threads if the always_collect
 *				       flag is set to true
 *				       
 * return	        : true or false
 * always_collect (in)  : flag that tells that we should always collect statistics
 *
 */
STATIC_INLINE bool
perfmon_is_perf_tracking_force (bool always_collect)
{
  return pstat_Global.initialized && (always_collect || pstat_Global.n_watchers > 0);
}

#if defined(CS_MODE) || defined(SA_MODE)
/* Client execution statistic structure */
typedef struct perfmon_client_stat_info PERFMON_CLIENT_STAT_INFO;
struct perfmon_client_stat_info
{
  time_t cpu_start_usr_time;
  time_t cpu_start_sys_time;
  time_t elapsed_start_time;
  UINT64 *base_server_stats;
  UINT64 *current_server_stats;
  UINT64 *old_global_stats;
  UINT64 *current_global_stats;
};

extern bool perfmon_Iscollecting_stats;

extern int perfmon_start_stats (bool for_all_trans);
extern int perfmon_stop_stats (void);
extern void perfmon_reset_stats (void);
extern int perfmon_print_stats (FILE * stream);
extern int perfmon_print_global_stats (FILE * stream, FILE * bin_stream, bool cumulative, const char *substr);
extern int perfmon_get_stats (void);
extern int perfmon_get_global_stats (void);
#endif /* CS_MODE || SA_MODE */

#if defined (DIAG_DEVEL)
#if defined(SERVER_MODE)

typedef enum t_diag_obj_type T_DIAG_OBJ_TYPE;
enum t_diag_obj_type
{
  DIAG_OBJ_TYPE_QUERY_OPEN_PAGE = 0,
  DIAG_OBJ_TYPE_QUERY_OPENED_PAGE = 1,
  DIAG_OBJ_TYPE_QUERY_SLOW_QUERY = 2,
  DIAG_OBJ_TYPE_QUERY_FULL_SCAN = 3,
  DIAG_OBJ_TYPE_CONN_CLI_REQUEST = 4,
  DIAG_OBJ_TYPE_CONN_ABORTED_CLIENTS = 5,
  DIAG_OBJ_TYPE_CONN_CONN_REQ = 6,
  DIAG_OBJ_TYPE_CONN_CONN_REJECT = 7,
  DIAG_OBJ_TYPE_BUFFER_PAGE_READ = 8,
  DIAG_OBJ_TYPE_BUFFER_PAGE_WRITE = 9,
  DIAG_OBJ_TYPE_LOCK_DEADLOCK = 10,
  DIAG_OBJ_TYPE_LOCK_REQUEST = 11
};

typedef enum t_diag_value_settype T_DIAG_VALUE_SETTYPE;
enum t_diag_value_settype
{
  DIAG_VAL_SETTYPE_INC,
  DIAG_VAL_SETTYPE_DEC,
  DIAG_VAL_SETTYPE_SET
};

typedef int (*T_DO_FUNC) (int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);

typedef struct t_diag_object_table T_DIAG_OBJECT_TABLE;
struct t_diag_object_table
{
  char typestring[32];
  T_DIAG_OBJ_TYPE type;
  T_DO_FUNC func;
};

/* MACRO definition */
#define SET_DIAG_VALUE(DIAG_EXEC_FLAG, ITEM_TYPE, VALUE, SET_TYPE, ERR_BUF)          \
    do {                                                                             \
        if (DIAG_EXEC_FLAG == true) {                                             \
            set_diag_value(ITEM_TYPE , VALUE, SET_TYPE, ERR_BUF);                    \
        }                                                                            \
    } while(0)

#define DIAG_GET_TIME(DIAG_EXEC_FLAG, TIMER)                                         \
    do {                                                                             \
        if (DIAG_EXEC_FLAG == true) {                                             \
            gettimeofday(&TIMER, NULL);                                              \
        }                                                                            \
    } while(0)

#define SET_DIAG_VALUE_SLOW_QUERY(DIAG_EXEC_FLAG, START_TIME, END_TIME, VALUE, SET_TYPE, ERR_BUF)\
    do {                                                                                 \
        if (DIAG_EXEC_FLAG == true) {                                                 \
            struct timeval result = {0,0};                                               \
            ADD_TIMEVAL(result, START_TIME, END_TIME);                                   \
            if (result.tv_sec >= diag_long_query_time)                                   \
                set_diag_value(DIAG_OBJ_TYPE_QUERY_SLOW_QUERY, VALUE, SET_TYPE, ERR_BUF);\
        }                                                                                \
    } while(0)

#define SET_DIAG_VALUE_FULL_SCAN(DIAG_EXEC_FLAG, VALUE, SET_TYPE, ERR_BUF, XASL, SPECP) \
    do {                                                                                \
        if (DIAG_EXEC_FLAG == true) {                                                \
            if (((XASL_TYPE(XASL) == BUILDLIST_PROC) ||                                 \
                 (XASL_TYPE(XASL) == BUILDVALUE_PROC))                                  \
                && ACCESS_SPEC_ACCESS(SPECP) == SEQUENTIAL) {                           \
                set_diag_value(DIAG_OBJ_TYPE_QUERY_FULL_SCAN                            \
                        , 1                                                             \
                        , DIAG_VAL_SETTYPE_INC                                          \
                        , NULL);                                                        \
            }                                                                           \
        }                                                                               \
    } while(0)

extern int diag_long_query_time;
extern bool diag_executediag;

extern bool init_diag_mgr (const char *server_name, int num_thread, char *err_buf);
extern void close_diag_mgr (void);
extern bool set_diag_value (T_DIAG_OBJ_TYPE type, int value, T_DIAG_VALUE_SETTYPE settype, char *err_buf);
#endif /* SERVER_MODE */
#endif /* DIAG_DEVEL */

#ifndef DIFF_TIMEVAL
#define DIFF_TIMEVAL(start, end, elapsed) \
    do { \
      (elapsed).tv_sec = (end).tv_sec - (start).tv_sec; \
      (elapsed).tv_usec = (end).tv_usec - (start).tv_usec; \
      if ((elapsed).tv_usec < 0) \
        { \
          (elapsed).tv_sec--; \
          (elapsed).tv_usec += 1000000; \
        } \
    } while (0)
#endif

#define ADD_TIMEVAL(total, start, end) do {     \
  total.tv_usec +=                              \
    (end.tv_usec - start.tv_usec) >= 0 ?        \
      (end.tv_usec-start.tv_usec)               \
    : (1000000 + (end.tv_usec-start.tv_usec));  \
  total.tv_sec +=                               \
    (end.tv_usec - start.tv_usec) >= 0 ?        \
      (end.tv_sec-start.tv_sec)                 \
    : (end.tv_sec-start.tv_sec-1);              \
  total.tv_sec +=                               \
    total.tv_usec/1000000;                      \
  total.tv_usec %= 1000000;                     \
} while(0)

#define TO_MSEC(elapsed) \
  ((int)((elapsed.tv_sec * 1000) + (int) (elapsed.tv_usec / 1000)))

#if defined (EnableThreadMonitoring)
#define MONITOR_WAITING_THREAD(elapsed) \
    (prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD) > 0 \
     && ((elapsed).tv_sec * 1000 + (elapsed).tv_usec / 1000) \
         > prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
#else
#define MONITOR_WAITING_THREAD(elapsed) (0)
#endif

typedef struct perf_utime_tracker PERF_UTIME_TRACKER;
struct perf_utime_tracker
{
  bool is_perf_tracking;
  TSC_TICKS start_tick;
  TSC_TICKS end_tick;
};
#define PERF_UTIME_TRACKER_INITIALIZER { false, {0}, {0} }
#define PERF_UTIME_TRACKER_START(thread_p, track) \
  do \
    { \
      (track)->is_perf_tracking = perfmon_is_perf_tracking (); \
      if ((track)->is_perf_tracking) tsc_getticks (&(track)->start_tick); \
    } \
  while (false)
/* Time trackers - perfmon_time_stat is called. */
#define PERF_UTIME_TRACKER_TIME(thread_p, track, psid) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      perfmon_time_stat (thread_p, psid, tsc_elapsed_utime ((track)->end_tick,  (track)->start_tick)); \
    } \
  while (false)
#define PERF_UTIME_TRACKER_TIME_AND_RESTART(thread_p, track, psid) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      perfmon_time_stat (thread_p, psid, tsc_elapsed_utime ((track)->end_tick,  (track)->start_tick)); \
      (track)->start_tick = (track)->end_tick; \
    } \
  while (false)
/* Bulk time trackers - perfmon_time_bulk_stat is called. */
#define PERF_UTIME_TRACKER_BULK_TIME(thread_p, track, psid, count) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      perfmon_time_bulk_stat (thread_p, psid, tsc_elapsed_utime ((track)->end_tick, (track)->start_tick), count); \
    } \
  while (false)
#define PERF_UTIME_TRACKER_BULK_TIME_AND_RESTART(thread_p, track, psid, count) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      perfmon_time_bulk, stat (thread_p, psid, tsc_elapsed_utime ((track)->end_tick,  (track)->start_tick), count); \
      (track)->start_tick = (track)->end_tick; \
    } \
  while (false)

/* Time accumulators only - perfmon_add_stat is called. */
/* todo: PERF_UTIME_TRACKER_ADD_TIME is never used and PERF_UTIME_TRACKER_ADD_TIME_AND_RESTART is similar to
 * PERF_UTIME_TRACKER_TIME_AND_RESTART. they were supposed to collect just timers, but now have changed */
#define PERF_UTIME_TRACKER_ADD_TIME(thread_p, track, psid) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      perfmon_time_stat (thread_p, psid, (int) tsc_elapsed_utime ((track)->end_tick,  (track)->start_tick)); \
    } \
  while (false)
#define PERF_UTIME_TRACKER_ADD_TIME_AND_RESTART(thread_p, track, psid) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      perfmon_time_stat (thread_p, psid, (int) tsc_elapsed_utime ((track)->end_tick,  (track)->start_tick)); \
      (track)->start_tick = (track)->end_tick; \
    } \
  while (false)

#if defined(SERVER_MODE) || defined (SA_MODE)
/*
 * Statistics at file io level
 */
extern bool perfmon_server_is_stats_on (THREAD_ENTRY * thread_p);

extern UINT64 perfmon_get_from_statistic (THREAD_ENTRY * thread_p, const int statistic_id);

extern void perfmon_lk_waited_time_on_objects (THREAD_ENTRY * thread_p, int lock_mode, UINT64 amount);

extern UINT64 perfmon_get_stats_and_clear (THREAD_ENTRY * thread_p, const char *stat_name);

extern void perfmon_pbx_fix (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
			     int cond_type);
extern void perfmon_pbx_promote (THREAD_ENTRY * thread_p, int page_type, int promote_cond, int holder_latch,
				 int success, UINT64 amount);
extern void perfmon_pbx_unfix (THREAD_ENTRY * thread_p, int page_type, int buf_dirty, int dirtied_by_holder,
			       int holder_latch);
extern void perfmon_pbx_lock_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
					   int cond_type, UINT64 amount);
extern void perfmon_pbx_hold_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
					   UINT64 amount);
extern void perfmon_pbx_fix_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
					  int cond_type, UINT64 amount);
extern void perfmon_mvcc_snapshot (THREAD_ENTRY * thread_p, int snapshot, int rec_type, int visibility);

#endif /* SERVER_MODE || SA_MODE */

#endif /* _PERF_MONITOR_H_ */
