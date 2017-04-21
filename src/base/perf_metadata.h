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
 * perf_metadata.h - Meta-data used for performance statistics monitoring and processing
 */

#ifndef _PERF_METADATA_H_
#define _PERF_METADATA_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <system.h>
#include <porting.h>

#ifdef __cplusplus
}
#endif

#define PERFBASE_DIMENSION_MAX_SIZE 32
#define PERFBASE_COMPLEX_MAX_DIMENSIONS 8

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
  PERF_PAGE_QRESULT,		/* query result page */
  PERF_PAGE_EHASH,		/* ehash bucket/dir page */
  PERF_PAGE_OVERFLOW,		/* overflow page (with ovf_keyval) */
  PERF_PAGE_AREA,		/* area page */
  PERF_PAGE_CATALOG,		/* catalog page */
  PERF_PAGE_BTREE_GENERIC,	/* b+tree index (uninitialized) */
  PERF_PAGE_LOG,		/* NONE - log page (unused) */
  PERF_PAGE_DROPPED_FILES,	/* Dropped files page.  */
  PERF_PAGE_VACUUM_DATA,	/* Vacuum data */
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
  PSTAT_FILE_IOSYNC_ALL,

  /* Execution statistics for the page buffer manager */
  PSTAT_PB_NUM_FETCHES,
  PSTAT_PB_NUM_DIRTIES,
  PSTAT_PB_NUM_IOREADS,
  PSTAT_PB_NUM_IOWRITES,
  PSTAT_PB_NUM_VICTIMS,
  PSTAT_PB_NUM_REPLACEMENTS,
  /* peeked stats */
  PSTAT_PB_FIXED_CNT,
  PSTAT_PB_DIRTY_CNT,
  PSTAT_PB_LRU1_CNT,
  PSTAT_PB_LRU2_CNT,
  PSTAT_PB_AIN_CNT,   /* obsolete - I think this might have been kept for cm */
  PSTAT_PB_VICTIM_CAND_CNT,   /* obsolete - I think this might have been kept for cm */
  PSTAT_PB_NUM_FLUSHED,
  PSTAT_PB_PRIVATE_QUOTA,
  PSTAT_PB_PRIVATE_COUNT,
  PSTAT_PB_LRU3_CNT,
  PSTAT_PB_VICT_CAND,
  PSTAT_PB_UNFIX_VOID_TO_PRIVATE_TOP,
  PSTAT_PB_UNFIX_VOID_TO_PRIVATE_MID,
  PSTAT_PB_UNFIX_VOID_TO_SHARED_MID,
  PSTAT_PB_UNFIX_LRU_ONE_PRV_TO_SHR_MID,
  PSTAT_PB_UNFIX_LRU_TWO_PRV_TO_SHR_MID,
  PSTAT_PB_UNFIX_LRU_THREE_PRV_TO_SHR_MID,
  PSTAT_PB_UNFIX_LRU_TWO_PRV_KEEP,
  PSTAT_PB_UNFIX_LRU_TWO_SHR_KEEP,
  PSTAT_PB_UNFIX_LRU_TWO_PRV_TO_TOP,
  PSTAT_PB_UNFIX_LRU_TWO_SHR_TO_TOP,
  PSTAT_PB_UNFIX_LRU_THREE_PRV_TO_TOP,
  PSTAT_PB_UNFIX_LRU_THREE_SHR_TO_TOP,
  PSTAT_PB_UNFIX_LRU_ONE_PRV_KEEP,
  PSTAT_PB_UNFIX_LRU_ONE_SHR_KEEP,
  /* vacuum */
  PSTAT_PB_UNFIX_VOID_TO_PRIVATE_TOP_VAC,
  PSTAT_PB_UNFIX_LRU_ONE_KEEP_VAC,
  PSTAT_PB_UNFIX_LRU_TWO_KEEP_VAC,
  PSTAT_PB_UNFIX_LRU_THREE_KEEP_VAC,
  /* aout */
  PSTAT_PB_UNFIX_VOID_AOUT_FOUND,
  PSTAT_PB_UNFIX_VOID_AOUT_NOT_FOUND,
  PSTAT_PB_UNFIX_VOID_AOUT_FOUND_VAC,
  PSTAT_PB_UNFIX_VOID_AOUT_NOT_FOUND_VAC,
  PSTAT_PB_NUM_HASH_ANCHOR_WAITS,
  PSTAT_PB_TIME_HASH_ANCHOR_WAIT,
  PSTAT_PB_FLUSH_COLLECT,
  PSTAT_PB_FLUSH_FLUSH,
  PSTAT_PB_FLUSH_SLEEP,
  PSTAT_PB_FLUSH_COLLECT_PER_PAGE,
  PSTAT_PB_FLUSH_FLUSH_PER_PAGE,
  PSTAT_PB_FLUSH_PAGE_FLUSHED,
  PSTAT_PB_FLUSH_SEND_DIRTY_TO_POST_FLUSH,
  PSTAT_PB_NUM_SKIPPED_FLUSH,
  PSTAT_PB_NUM_SKIPPED_NEED_WAL,
  PSTAT_PB_NUM_SKIPPED_ALREADY_FLUSHED,
  PSTAT_PB_NUM_SKIPPED_FIXED_OR_HOT,
  PSTAT_PB_COMPENSATE_FLUSH,
  /* allocate and victim assignments */
  PSTAT_PB_ALLOC_BCB,
  PSTAT_PB_ALLOC_BCB_SEARCH_VICTIM,
  PSTAT_PB_ALLOC_BCB_COND_WAIT_HIGH_PRIO,
  PSTAT_PB_ALLOC_BCB_COND_WAIT_LOW_PRIO,
  PSTAT_PB_ALLOC_BCB_PRIORITIZE_VACUUM,
  PSTAT_PB_VICTIM_USE_INVALID_BCB,
  /* direct assignments */
  PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_VOID,
  PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_LRU,
  PSTAT_PB_VICTIM_ASSIGN_DIRECT_FLUSH,
  PSTAT_PB_VICTIM_ASSIGN_DIRECT_PANIC,
  PSTAT_PB_VICTIM_ASSIGN_DIRECT_ADJUST,
  PSTAT_PB_VICTIM_ASSIGN_DIRECT_ADJUST_TO_VACUUM,
  PSTAT_PB_VICTIM_ASSIGN_DIRECT_SEARCH_FOR_FLUSH,
  /* successful searches */
  PSTAT_PB_VICTIM_SHARED_LRU_SUCCESS,
  PSTAT_PB_OWN_VICTIM_PRIVATE_LRU_SUCCESS,
  PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_SUCCESS,
  /* failed searches */
  PSTAT_PB_VICTIM_SHARED_LRU_FAIL,
  PSTAT_PB_VICTIM_OWN_PRIVATE_LRU_FAIL,
  PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_FAIL,
  PSTAT_PB_VICTIM_ALL_LRU_FAIL,
  /* search lru's */
  PSTAT_PB_VICTIM_GET_FROM_LRU,
  PSTAT_PB_VICTIM_GET_FROM_LRU_LIST_WAS_EMPTY,
  PSTAT_PB_VICTIM_GET_FROM_LRU_FAIL,
  PSTAT_PB_VICTIM_GET_FROM_LRU_BAD_HINT,
  /* lock-free circular queues with lru lists having victims */
  PSTAT_PB_LFCQ_LRU_PRV_GET_CALLS,
  PSTAT_PB_LFCQ_LRU_PRV_GET_EMPTY,
  PSTAT_PB_LFCQ_LRU_PRV_GET_BIG,
  PSTAT_PB_LFCQ_LRU_SHR_GET_CALLS,
  PSTAT_PB_LFCQ_LRU_SHR_GET_EMPTY,
  /* peeked stats */
  PSTAT_PB_WAIT_THREADS_HIGH_PRIO,
  PSTAT_PB_WAIT_THREADS_LOW_PRIO,
  PSTAT_PB_FLUSHED_BCBS_WAIT_FOR_ASSIGN,
  PSTAT_PB_LFCQ_BIG_PRV_NUM,
  PSTAT_PB_LFCQ_PRV_NUM,
  PSTAT_PB_LFCQ_SHR_NUM,
  PSTAT_PB_AVOID_DEALLOC_CNT,
  PSTAT_PB_AVOID_VICTIM_CNT,

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
  PSTAT_LK_NUM_CONVERTED_ON_PAGES,	/* obsolete? */
  PSTAT_LK_NUM_CONVERTED_ON_OBJECTS,
  PSTAT_LK_NUM_RE_REQUESTED_ON_PAGES,	/* obsolete? */
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

typedef struct perfbase_Dim PERFBASE_DIM;
struct perfbase_Dim
{
  const char *alias;
  int size;
  const char *names[PERFBASE_DIMENSION_MAX_SIZE];
};

typedef struct perfbase_Complex PERFBASE_COMPLEX;
struct perfbase_Complex
{
  int size;
  const PERFBASE_DIM *dimensions[PERFBASE_COMPLEX_MAX_DIMENSIONS];
};

/* PSTAT_METADATA
 * This structure will keep meta-data information on each statistic we are monitoring.
 */
typedef struct pstat_metadata PSTAT_METADATA;
struct pstat_metadata
{
  /* These members must be set. */
  PERF_STAT_ID psid;
  const char *stat_name;
  PSTAT_VALUE_TYPE valtype;

  /* These members are computed at startup. */
  int start_offset;
  int n_vals;

  /* dimensions are only for complex types */
  const PERFBASE_COMPLEX *complexp;
};

/* Statistics activation flags */

#define PERFMON_ACTIVE_DEFAULT 0
#define PERFMON_ACTIVE_DETAILED_BTREE_PAGE 1
#define PERFMON_ACTIVE_MVCC_SNAPSHOT 2
#define PERFMON_ACTIVE_LOCK_OBJECT 4
#define PERFMON_ACTIVE_PB_HASH_ANCHOR 8

/* Count & timer values. */
#define PSTAT_COUNTER_TIMER_COUNT_VALUE(startvalp) (startvalp)
#define PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE(startvalp) ((startvalp) + 1)
#define PSTAT_COUNTER_TIMER_MAX_TIME_VALUE(startvalp) ((startvalp) + 2)
#define PSTAT_COUNTER_TIMER_AVG_TIME_VALUE(startvalp) ((startvalp) + 3)

extern PSTAT_METADATA pstat_Metadata[];
typedef struct perfmeta_complex_cursor PERFMETA_COMPLEX_CURSOR;
struct perfmeta_complex_cursor
{
  int indices[PERFBASE_COMPLEX_MAX_DIMENSIONS];
};

typedef enum
{
  /* Don't change the initialization since they reflect the elements of lock_Conv and lock_Comp */
  /* this is a clone, the original being in storage_common.h and we should fix that! */
  PERF_NA_LOCK = 0,		/* N/A lock */
  PERF_INCON_NON_TWO_PHASE_LOCK = 1,	/* Incompatible 2 phase lock. */
  PERF_NULL_LOCK = 2,		/* NULL LOCK */
  PERF_SCH_S_LOCK = 3,		/* Schema Stability Lock */
  PERF_IS_LOCK = 4,		/* Intention Shared lock */
  PERF_S_LOCK = 5,		/* Shared lock */
  PERF_IX_LOCK = 6,		/* Intention exclusive lock */
  PERF_SIX_LOCK = 7,		/* Shared and intention exclusive lock */
  PERF_U_LOCK = 8,		/* Update lock */
  PERF_X_LOCK = 9,		/* Exclusive lock */
  PERF_SCH_M_LOCK = 10,		/* Schema Modification Lock */
  PERF_LOCK_CNT
} PERF_LOCK;

#define SAFE_DIV(a, b) ((b) == 0 ? 0 : (a) / (b))

extern char *perfmon_pack_stats (char *buf, UINT64 * stats);
extern char *perfmon_unpack_stats (char *buf, UINT64 * stats);

void perfmon_print_timer_to_file (FILE * stream, int stat_index, UINT64 * stats_ptr, int show_zero, int show_header);
void perfmon_compare_timer (FILE * stream, int stat_index, UINT64 * stats1, UINT64 * stats2);
extern void perfbase_aggregate_complex (int id, const UINT64 * vals, int index_dim, UINT64 * agg_vals);
extern void perfmon_server_dump_stats (const UINT64 * stats, FILE * stream, const char *substr);
extern void perfmon_server_dump_stats_to_buffer (const UINT64 * stats, char *buffer, int buf_size, const char *substr);
extern int perfmeta_init (void);
extern void perfmeta_final (void);
extern int perfmeta_complex_cursor_get_offset (PERF_STAT_ID psid, const PERFMETA_COMPLEX_CURSOR * cursor);
extern int perfmeta_complex_get_offset (PERF_STAT_ID psid, ...);
UINT64 perfmeta_get_stat_value_from_name (const char *stat_name, UINT64 * raw_stats);
void perfmeta_custom_dump_stats_in_table_form (const UINT64 ** stats, int no_of_stats, FILE * stream, int show_complex,
					       int show_zero);
void perfmeta_copy_stats (UINT64 * dst, UINT64 * src);
void perfmeta_get_stat_index_and_dimension (const char *stat_name, const char *dimension_name, int *stat_index,
					    int *fixed_dimension);
extern int perfmeta_get_values_memsize (void);
extern int perfmeta_get_values_count (void);


extern UINT64 *perfmeta_allocate_values (void);
extern void perfmeta_copy_values (UINT64 * src, UINT64 * dest);

#endif /*_PERF_METADATA_H_*/
