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

#define PERFMON_ACTIVE_DEFAULT 0
#define PERFMON_ACTIVE_DETAILED_BTREE_PAGE 1
#define PERFMON_ACTIVE_MVCC_SNAPSHOT 2
#define PERFMON_ACTIVE_LOCK_OBJECT 4
#define PERFMON_ACTIVE_PB_HASH_ANCHOR 8

/* PERF_MODULE_TYPE x PERF_PAGE_TYPE x PAGE_FETCH_MODE x HOLDER_LATCH_MODE x COND_FIX_TYPE */
#define PERF_PAGE_FIX_COUNTERS \
  ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) \
   * (PERF_HOLDER_LATCH_CNT) * (PERF_CONDITIONAL_FIX_CNT))

#define PERF_PAGE_PROMOTE_COUNTERS \
  ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * (PERF_PROMOTE_CONDITION_CNT) \
  * (PERF_HOLDER_LATCH_CNT) * (2 /* success */))

/* PERF_MODULE_TYPE x PAGE_TYPE x DIRTY_OR_CLEAN x DIRTY_OR_CLEAN x READ_OR_WRITE_OR_MIX */
#define PERF_PAGE_UNFIX_COUNTERS \
  ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * 2 * 2 * (PERF_HOLDER_LATCH_CNT))

#define PERF_PAGE_LOCK_TIME_COUNTERS PERF_PAGE_FIX_COUNTERS

#define PERF_PAGE_HOLD_TIME_COUNTERS \
    ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) \
   * (PERF_HOLDER_LATCH_CNT))

#define PERF_PAGE_FIX_TIME_COUNTERS PERF_PAGE_FIX_COUNTERS

#define PERF_PAGE_FIX_STAT_OFFSET(module,page_type,page_found_mode,latch_mode,\
				  cond_type) \
  ((module) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) \
    * (PERF_CONDITIONAL_FIX_CNT) \
    + (page_type) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) \
       * (PERF_CONDITIONAL_FIX_CNT) \
    + (page_found_mode) * (PERF_HOLDER_LATCH_CNT) * (PERF_CONDITIONAL_FIX_CNT) \
    + (latch_mode) * (PERF_CONDITIONAL_FIX_CNT) + (cond_type))

#define PERF_PAGE_PROMOTE_STAT_OFFSET(module,page_type,promote_cond, \
				      holder_latch,success) \
  ((module) * (PERF_PAGE_CNT) * (PERF_PROMOTE_CONDITION_CNT) \
    * (PERF_HOLDER_LATCH_CNT) * 2 /* success */ \
    + (page_type) * (PERF_PROMOTE_CONDITION_CNT) * (PERF_HOLDER_LATCH_CNT) \
    * 2 /* success */ \
    + (promote_cond) * (PERF_HOLDER_LATCH_CNT) * 2 /* success */ \
    + (holder_latch) * 2 /* success */ \
    + success)

#define PERF_PAGE_UNFIX_STAT_OFFSET(module,page_type,buf_dirty,\
				    dirtied_by_holder,holder_latch) \
  ((module) * (PERF_PAGE_CNT) * 2 * 2 * (PERF_HOLDER_LATCH_CNT) + \
   (page_type) * 2 * 2 * (PERF_HOLDER_LATCH_CNT) + \
   (buf_dirty) * 2 * (PERF_HOLDER_LATCH_CNT) + \
   (dirtied_by_holder) * (PERF_HOLDER_LATCH_CNT) + (holder_latch))

#define PERF_PAGE_LOCK_TIME_OFFSET(module,page_type,page_found_mode,latch_mode,\
				  cond_type) \
	PERF_PAGE_FIX_STAT_OFFSET(module,page_type,page_found_mode,latch_mode,\
				  cond_type)

#define PERF_PAGE_HOLD_TIME_OFFSET(module,page_type,page_found_mode,latch_mode)\
  ((module) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) \
    + (page_type) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) \
    + (page_found_mode) * (PERF_HOLDER_LATCH_CNT) \
    + (latch_mode))

#define PERF_PAGE_FIX_TIME_OFFSET(module,page_type,page_found_mode,latch_mode,\
				  cond_type) \
	PERF_PAGE_FIX_STAT_OFFSET(module,page_type,page_found_mode,latch_mode,\
				  cond_type)

#define PERF_MVCC_SNAPSHOT_COUNTERS \
  (PERF_SNAPSHOT_CNT * PERF_SNAPSHOT_RECORD_TYPE_CNT \
   * PERF_SNAPSHOT_VISIBILITY_CNT)

#define PERF_MVCC_SNAPSHOT_OFFSET(snapshot,rec_type,visibility) \
  ((snapshot) * PERF_SNAPSHOT_RECORD_TYPE_CNT * PERF_SNAPSHOT_VISIBILITY_CNT \
   + (rec_type) * PERF_SNAPSHOT_VISIBILITY_CNT + (visibility))

#define PERF_OBJ_LOCK_STAT_COUNTERS (SCH_M_LOCK + 1)

#define SAFE_DIV(a, b) ((b) == 0 ? 0 : (a) / (b))

/* Count & timer values. */
#define PSTAT_COUNTER_TIMER_COUNT_VALUE(startvalp) (startvalp)
#define PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE(startvalp) ((startvalp) + 1)
#define PSTAT_COUNTER_TIMER_MAX_TIME_VALUE(startvalp) ((startvalp) + 2)
#define PSTAT_COUNTER_TIMER_AVG_TIME_VALUE(startvalp) ((startvalp) + 3)

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

typedef enum
{
  PERF_MODULE_SYSTEM = 0,
  PERF_MODULE_USER,
  PERF_MODULE_VACUUM,

  PERF_MODULE_CNT
} PERF_MODULE_TYPE;

typedef enum
{
  PERF_HOLDER_LATCH_READ = 0,
  PERF_HOLDER_LATCH_WRITE,
  PERF_HOLDER_LATCH_MIXED,

  PERF_HOLDER_LATCH_CNT
} PERF_HOLDER_LATCH;

typedef enum
{
  PERF_CONDITIONAL_FIX = 0,
  PERF_UNCONDITIONAL_FIX_NO_WAIT,
  PERF_UNCONDITIONAL_FIX_WITH_WAIT,

  PERF_CONDITIONAL_FIX_CNT
} PERF_CONDITIONAL_FIX_TYPE;

typedef enum
{
  PERF_PROMOTE_ONLY_READER,
  PERF_PROMOTE_SHARED_READER,

  PERF_PROMOTE_CONDITION_CNT
} PERF_PROMOTE_CONDITION;

typedef enum
{
  PERF_PAGE_MODE_OLD_LOCK_WAIT = 0,
  PERF_PAGE_MODE_OLD_NO_WAIT,
  PERF_PAGE_MODE_NEW_LOCK_WAIT,
  PERF_PAGE_MODE_NEW_NO_WAIT,
  PERF_PAGE_MODE_OLD_IN_BUFFER,

  PERF_PAGE_MODE_CNT
} PERF_PAGE_MODE;

/* extension of PAGE_TYPE (storage_common.h) - keep value compatibility */
typedef enum
{
  PERF_PAGE_UNKNOWN = 0,	/* used for initialized page */
  PERF_PAGE_FTAB,		/* file allocset table page */
  PERF_PAGE_HEAP,		/* heap page */
  PERF_PAGE_VOLHEADER,		/* volume header page */
  PERF_PAGE_VOLBITMAP,		/* volume bitmap page */
  PERF_PAGE_XASL,		/* xasl stream page */
  PERF_PAGE_QRESULT,		/* query result page */
  PERF_PAGE_EHASH,		/* ehash bucket/dir page */
  PERF_PAGE_LARGEOBJ,		/* large object/dir page */
  PERF_PAGE_OVERFLOW,		/* overflow page (with ovf_keyval) */
  PERF_PAGE_AREA,		/* area page */
  PERF_PAGE_CATALOG,		/* catalog page */
  PERF_PAGE_BTREE_GENERIC,	/* b+tree index (uninitialized) */
  PERF_PAGE_LOG,		/* NONE - log page (unused) */
  PERF_PAGE_DROPPED_FILES,	/* Dropped files page.  */
  PERF_PAGE_BTREE_ROOT,		/* b+tree root index page */
  PERF_PAGE_BTREE_OVF,		/* b+tree overflow index page */
  PERF_PAGE_BTREE_LEAF,		/* b+tree leaf index page */
  PERF_PAGE_BTREE_NONLEAF,	/* b+tree nonleaf index page */
  PERF_PAGE_CNT
} PERF_PAGE_TYPE;

typedef enum
{
  PERF_SNAPSHOT_SATISFIES_DELETE = 0,
  PERF_SNAPSHOT_SATISFIES_DIRTY,
  PERF_SNAPSHOT_SATISFIES_SNAPSHOT,
  PERF_SNAPSHOT_SATISFIES_VACUUM,

  PERF_SNAPSHOT_CNT
} PERF_SNAPSHOT_TYPE;

typedef enum
{
  PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED = 0,	/* already vacuumed */
  PERF_SNAPSHOT_RECORD_INSERTED_CURR_TRAN,	/* needs commit and vacuum */
  PERF_SNAPSHOT_RECORD_INSERTED_OTHER_TRAN,	/* needs commit and vacuum */
  PERF_SNAPSHOT_RECORD_INSERTED_COMMITED,	/* commited, needs vacuum */
  PERF_SNAPSHOT_RECORD_INSERTED_COMMITED_LOST,	/* commited, unvacuumed/lost */

  PERF_SNAPSHOT_RECORD_INSERTED_DELETED,	/* inserted, than deleted */

  PERF_SNAPSHOT_RECORD_DELETED_CURR_TRAN,	/* deleted by current tran */
  PERF_SNAPSHOT_RECORD_DELETED_OTHER_TRAN,	/* deleted by other active tran */
  PERF_SNAPSHOT_RECORD_DELETED_COMMITTED,	/* deleted, and committed */
  PERF_SNAPSHOT_RECORD_DELETED_COMMITTED_LOST,	/* deleted, and committed, unvacuumed/lost */

  PERF_SNAPSHOT_RECORD_TYPE_CNT
} PERF_SNAPSHOT_RECORD_TYPE;

typedef enum
{
  PERF_SNAPSHOT_INVISIBLE = 0,
  PERF_SNAPSHOT_VISIBLE,

  PERF_SNAPSHOT_VISIBILITY_CNT
} PERF_SNAPSHOT_VISIBILITY;

typedef enum
{
  PSTAT_BASE = -1,		/* not a real entry. just to avoid compile warnings */

  /* Execution statistics for the file io */
  PSTAT_FILE_NUM_CREATES = 0,
  PSTAT_FILE_NUM_REMOVES,
  PSTAT_FILE_NUM_IOREADS,
  PSTAT_FILE_NUM_IOWRITES,
  PSTAT_FILE_NUM_IOSYNCHES,
  PSTAT_FILE_NUM_PAGE_ALLOCS,
  PSTAT_FILE_NUM_PAGE_DEALLOCS,

  /* Execution statistics for the page buffer manager */
  PSTAT_PB_NUM_FETCHES,
  PSTAT_PB_NUM_DIRTIES,
  PSTAT_PB_NUM_IOREADS,
  PSTAT_PB_NUM_IOWRITES,
  PSTAT_PB_NUM_VICTIMS,
  PSTAT_PB_NUM_REPLACEMENTS,
  PSTAT_PB_NUM_HASH_ANCHOR_WAITS,
  PSTAT_PB_TIME_HASH_ANCHOR_WAIT,
  /* peeked stats */
  PSTAT_PB_FIXED_CNT,
  PSTAT_PB_DIRTY_CNT,
  PSTAT_PB_LRU1_CNT,
  PSTAT_PB_LRU2_CNT,
  PSTAT_PB_AIN_CNT,
  PSTAT_PB_AVOID_DEALLOC_CNT,
  PSTAT_PB_AVOID_VICTIM_CNT,
  PSTAT_PB_VICTIM_CAND_CNT,

  /* Execution statistics for the log manager */
  PSTAT_LOG_NUM_FETCHES,
  PSTAT_LOG_NUM_IOREADS,
  PSTAT_LOG_NUM_IOWRITES,
  PSTAT_LOG_NUM_APPENDRECS,
  PSTAT_LOG_NUM_ARCHIVES,
  PSTAT_LOG_NUM_START_CHECKPOINTS,
  PSTAT_LOG_NUM_END_CHECKPOINTS,
  PSTAT_LOG_NUM_WALS,
  PSTAT_LOG_NUM_REPLACEMENTS_IOWRITES,
  PSTAT_LOG_NUM_REPLACEMENTS,

  /* Execution statistics for the lock manager */
  PSTAT_LK_NUM_ACQUIRED_ON_PAGES,
  PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS,
  PSTAT_LK_NUM_CONVERTED_ON_PAGES,
  PSTAT_LK_NUM_CONVERTED_ON_OBJECTS,
  PSTAT_LK_NUM_RE_REQUESTED_ON_PAGES,
  PSTAT_LK_NUM_RE_REQUESTED_ON_OBJECTS,
  PSTAT_LK_NUM_WAITED_ON_PAGES,
  PSTAT_LK_NUM_WAITED_ON_OBJECTS,
  PSTAT_LK_NUM_WAITED_TIME_ON_OBJECTS,	/* include this to avoid client-server compat issue even if extended stats are
					 * disabled */

  /* Execution statistics for transactions */
  PSTAT_TRAN_NUM_COMMITS,
  PSTAT_TRAN_NUM_ROLLBACKS,
  PSTAT_TRAN_NUM_SAVEPOINTS,
  PSTAT_TRAN_NUM_START_TOPOPS,
  PSTAT_TRAN_NUM_END_TOPOPS,
  PSTAT_TRAN_NUM_INTERRUPTS,

  /* Execution statistics for the btree manager */
  PSTAT_BT_NUM_INSERTS,
  PSTAT_BT_NUM_DELETES,
  PSTAT_BT_NUM_UPDATES,
  PSTAT_BT_NUM_COVERED,
  PSTAT_BT_NUM_NONCOVERED,
  PSTAT_BT_NUM_RESUMES,
  PSTAT_BT_NUM_MULTI_RANGE_OPT,
  PSTAT_BT_NUM_SPLITS,
  PSTAT_BT_NUM_MERGES,
  PSTAT_BT_NUM_GET_STATS,

  /* Execution statistics for the heap manager */
  PSTAT_HEAP_NUM_STATS_SYNC_BESTSPACE,

  /* Execution statistics for the query manager */
  PSTAT_QM_NUM_SELECTS,
  PSTAT_QM_NUM_INSERTS,
  PSTAT_QM_NUM_DELETES,
  PSTAT_QM_NUM_UPDATES,
  PSTAT_QM_NUM_SSCANS,
  PSTAT_QM_NUM_ISCANS,
  PSTAT_QM_NUM_LSCANS,
  PSTAT_QM_NUM_SETSCANS,
  PSTAT_QM_NUM_METHSCANS,
  PSTAT_QM_NUM_NLJOINS,
  PSTAT_QM_NUM_MJOINS,
  PSTAT_QM_NUM_OBJFETCHES,
  PSTAT_QM_NUM_HOLDABLE_CURSORS,

  /* Execution statistics for external sort */
  PSTAT_SORT_NUM_IO_PAGES,
  PSTAT_SORT_NUM_DATA_PAGES,

  /* Execution statistics for network communication */
  PSTAT_NET_NUM_REQUESTS,

  /* flush control stat */
  PSTAT_FC_NUM_PAGES,
  PSTAT_FC_NUM_LOG_PAGES,
  PSTAT_FC_TOKENS,

  /* prior lsa info */
  PSTAT_PRIOR_LSA_LIST_SIZE,	/* kbytes */
  PSTAT_PRIOR_LSA_LIST_MAXED,
  PSTAT_PRIOR_LSA_LIST_REMOVED,

  /* best space info */
  PSTAT_HF_NUM_STATS_ENTRIES,
  PSTAT_HF_NUM_STATS_MAXED,

  /* HA replication delay */
  PSTAT_HA_REPL_DELAY,

  /* Execution statistics for Plan cache */
  PSTAT_PC_NUM_ADD,
  PSTAT_PC_NUM_LOOKUP,
  PSTAT_PC_NUM_HIT,
  PSTAT_PC_NUM_MISS,
  PSTAT_PC_NUM_FULL,
  PSTAT_PC_NUM_DELETE,
  PSTAT_PC_NUM_INVALID_XASL_ID,
  PSTAT_PC_NUM_CACHE_ENTRIES,

  PSTAT_VAC_NUM_VACUUMED_LOG_PAGES,
  PSTAT_VAC_NUM_TO_VACUUM_LOG_PAGES,
  PSTAT_VAC_NUM_PREFETCH_REQUESTS_LOG_PAGES,
  PSTAT_VAC_NUM_PREFETCH_HITS_LOG_PAGES,

  /* Track heap modify counters. */
  PSTAT_HEAP_HOME_INSERTS,
  PSTAT_HEAP_BIG_INSERTS,
  PSTAT_HEAP_ASSIGN_INSERTS,
  PSTAT_HEAP_HOME_DELETES,
  PSTAT_HEAP_HOME_MVCC_DELETES,
  PSTAT_HEAP_HOME_TO_REL_DELETES,
  PSTAT_HEAP_HOME_TO_BIG_DELETES,
  PSTAT_HEAP_REL_DELETES,
  PSTAT_HEAP_REL_MVCC_DELETES,
  PSTAT_HEAP_REL_TO_HOME_DELETES,
  PSTAT_HEAP_REL_TO_BIG_DELETES,
  PSTAT_HEAP_REL_TO_REL_DELETES,
  PSTAT_HEAP_BIG_DELETES,
  PSTAT_HEAP_BIG_MVCC_DELETES,
  PSTAT_HEAP_HOME_UPDATES,
  PSTAT_HEAP_HOME_TO_REL_UPDATES,
  PSTAT_HEAP_HOME_TO_BIG_UPDATES,
  PSTAT_HEAP_REL_UPDATES,
  PSTAT_HEAP_REL_TO_HOME_UPDATES,
  PSTAT_HEAP_REL_TO_REL_UPDATES,
  PSTAT_HEAP_REL_TO_BIG_UPDATES,
  PSTAT_HEAP_BIG_UPDATES,
  PSTAT_HEAP_HOME_VACUUMS,
  PSTAT_HEAP_BIG_VACUUMS,
  PSTAT_HEAP_REL_VACUUMS,
  PSTAT_HEAP_INSID_VACUUMS,
  PSTAT_HEAP_REMOVE_VACUUMS,

  /* Track heap modify timers. */
  PSTAT_HEAP_INSERT_PREPARE,
  PSTAT_HEAP_INSERT_EXECUTE,
  PSTAT_HEAP_INSERT_LOG,
  PSTAT_HEAP_DELETE_PREPARE,
  PSTAT_HEAP_DELETE_EXECUTE,
  PSTAT_HEAP_DELETE_LOG,
  PSTAT_HEAP_UPDATE_PREPARE,
  PSTAT_HEAP_UPDATE_EXECUTE,
  PSTAT_HEAP_UPDATE_LOG,
  PSTAT_HEAP_VACUUM_PREPARE,
  PSTAT_HEAP_VACUUM_EXECUTE,
  PSTAT_HEAP_VACUUM_LOG,

  /* B-tree ops detailed statistics. */
  PSTAT_BT_FIX_OVF_OIDS,
  PSTAT_BT_UNIQUE_RLOCKS,
  PSTAT_BT_UNIQUE_WLOCKS,
  PSTAT_BT_LEAF,
  PSTAT_BT_TRAVERSE,
  PSTAT_BT_FIND_UNIQUE,
  PSTAT_BT_FIND_UNIQUE_TRAVERSE,
  PSTAT_BT_RANGE_SEARCH,
  PSTAT_BT_RANGE_SEARCH_TRAVERSE,
  PSTAT_BT_INSERT,
  PSTAT_BT_INSERT_TRAVERSE,
  PSTAT_BT_DELETE,
  PSTAT_BT_DELETE_TRAVERSE,
  PSTAT_BT_MVCC_DELETE,
  PSTAT_BT_MVCC_DELETE_TRAVERSE,
  PSTAT_BT_MARK_DELETE,
  PSTAT_BT_MARK_DELETE_TRAVERSE,
  PSTAT_BT_UNDO_INSERT,
  PSTAT_BT_UNDO_INSERT_TRAVERSE,
  PSTAT_BT_UNDO_DELETE,
  PSTAT_BT_UNDO_DELETE_TRAVERSE,
  PSTAT_BT_UNDO_MVCC_DELETE,
  PSTAT_BT_UNDO_MVCC_DELETE_TRAVERSE,
  PSTAT_BT_VACUUM,
  PSTAT_BT_VACUUM_TRAVERSE,
  PSTAT_BT_VACUUM_INSID,
  PSTAT_BT_VACUUM_INSID_TRAVERSE,

  /* Vacuum master/worker timers. */
  PSTAT_VAC_MASTER,
  PSTAT_VAC_JOB,
  PSTAT_VAC_WORKER_PROCESS_LOG,
  PSTAT_VAC_WORKER_EXECUTE,

  /* Log statistics */
  PSTAT_LOG_SNAPSHOT_TIME_COUNTERS,
  PSTAT_LOG_SNAPSHOT_RETRY_COUNTERS,
  PSTAT_LOG_TRAN_COMPLETE_TIME_COUNTERS,
  PSTAT_LOG_OLDEST_MVCC_TIME_COUNTERS,
  PSTAT_LOG_OLDEST_MVCC_RETRY_COUNTERS,

  /* Computed statistics */
  /* ((pb_num_fetches - pb_num_ioreads) x 100 / pb_num_fetches) x 100 */
  PSTAT_PB_HIT_RATIO,
  /* ((log_num_fetches - log_num_ioreads) x 100 / log_num_fetches) x 100 */
  PSTAT_LOG_HIT_RATIO,
  /* ((fetches of vacuum - fetches of vacuum not found in PB) x 100 / fetches of vacuum) x 100 */
  PSTAT_VACUUM_DATA_HIT_RATIO,
  /* (100 x Number of unfix with of dirty pages of vacuum / total num of unfixes from vacuum) x 100 */
  PSTAT_PB_VACUUM_EFFICIENCY,
  /* (100 x Number of unfix from vacuum / total num of unfix) x 100 */
  PSTAT_PB_VACUUM_FETCH_RATIO,
  /* total time to acquire page lock (stored as 10 usec unit, displayed as miliseconds) */
  PSTAT_PB_PAGE_LOCK_ACQUIRE_TIME_10USEC,
  /* total time to acquire page hold (stored as 10 usec unit, displayed as miliseconds) */
  PSTAT_PB_PAGE_HOLD_ACQUIRE_TIME_10USEC,
  /* total time to acquire page fix (stored as 10 usec unit, displayed as miliseconds) */
  PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC,
  /* ratio of time required to allocate a buffer for a page : (100 x (fix_time - lock_time - hold_time) / fix_time) x
   * 100 */
  PSTAT_PB_PAGE_ALLOCATE_TIME_RATIO,
  /* total successful promotions */
  PSTAT_PB_PAGE_PROMOTE_SUCCESS,
  /* total failed promotions */
  PSTAT_PB_PAGE_PROMOTE_FAILED,
  /* total promotion time */
  PSTAT_PB_PAGE_PROMOTE_TOTAL_TIME_10USEC,

  /* Complex statistics */
  PSTAT_PBX_FIX_COUNTERS,
  PSTAT_PBX_PROMOTE_COUNTERS,
  PSTAT_PBX_PROMOTE_TIME_COUNTERS,
  PSTAT_PBX_UNFIX_COUNTERS,
  PSTAT_PBX_LOCK_TIME_COUNTERS,
  PSTAT_PBX_HOLD_TIME_COUNTERS,
  PSTAT_PBX_FIX_TIME_COUNTERS,
  PSTAT_MVCC_SNAPSHOT_COUNTERS,
  PSTAT_OBJ_LOCK_TIME_COUNTERS,

  PSTAT_COUNT = PSTAT_OBJ_LOCK_TIME_COUNTERS + 1
} PERF_STAT_ID;

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

typedef enum
{
  PSTAT_ACCUMULATE_SINGLE_VALUE,	/* A single accumulator value. */
  PSTAT_PEEK_SINGLE_VALUE,	/* A single value peeked from database. */
  /* TODO: Currently this type of statistics is set by active workers. I think in
   *       most cases we could peek it at "compute". There can be two approaches:
   *       if f_compute is provided, it is read at compute phase. If not, the
   *       existing value is used and it must be set by active workers.
   */
  PSTAT_COUNTER_TIMER_VALUE,	/* A counter/timer. Counter is incremented, timer is accumulated, max time
				 * is compared with all registered timers and average time is computed.
				 */
  PSTAT_COMPUTED_RATIO_VALUE,	/* Value is computed based on other values in statistics. A ratio is obtained
				 * at the end.
				 */
  PSTAT_COMPLEX_VALUE		/* A "complex" value. The creator must handle loading, adding, dumping and
				 * computing values.
				 */
} PSTAT_VALUE_TYPE;

/* PSTAT_METADATA
 * This structure will keep meta-data information on each statistic we are monitoring.
 */
typedef struct pstat_metadata PSTAT_METADATA;

typedef void (*PSTAT_DUMP_IN_FILE_FUNC) (FILE *, const UINT64 * stat_vals);
typedef void (*PSTAT_DUMP_IN_BUFFER_FUNC) (char **, const UINT64 * stat_vals, int *remaining_size);
typedef int (*PSTAT_LOAD_FUNC) (void);

struct pstat_metadata
{
  /* These members must be set. */
  PERF_STAT_ID psid;
  const char *stat_name;
  PSTAT_VALUE_TYPE valtype;

  /* These members are computed at startup. */
  int start_offset;
  int n_vals;

  PSTAT_DUMP_IN_FILE_FUNC f_dump_in_file;
  PSTAT_DUMP_IN_BUFFER_FUNC f_dump_in_buffer;
  PSTAT_LOAD_FUNC f_load;
};

extern PSTAT_METADATA pstat_Metadata[];

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

extern void perfmon_server_dump_stats (const UINT64 * stats, FILE * stream, const char *substr);

extern void perfmon_server_dump_stats_to_buffer (const UINT64 * stats, char *buffer, int buf_size, const char *substr);

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
STATIC_INLINE void perfmon_add_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 amount)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_inc_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, int statval, bool check_watchers)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_at_offset (THREAD_ENTRY * thread_p, int offset, int statval, bool check_watchers)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_stat_to_global (PERF_STAT_ID psid, int statval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_set_at_offset_to_global (int offset, int statval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_time_at_offset (THREAD_ENTRY * thread_p, int offset, UINT64 timediff)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_time_stat (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, UINT64 timediff)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int perfmon_get_activation_flag (void) __attribute__ ((ALWAYS_INLINE));
extern char *perfmon_pack_stats (char *buf, UINT64 * stats);
extern char *perfmon_unpack_stats (char *buf, UINT64 * stats);

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
      max_time = ATOMIC_INC_64 (PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (statvalp), 0);
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
extern int perfmon_print_global_stats (FILE * stream, bool cumulative, const char *substr);
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

/* Time accumulators only - perfmon_add_stat is called. */
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
