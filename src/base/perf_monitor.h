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
 * perf_monitor.h - Monitor execution statistics at Client
 */

#ifndef _PERF_MONITOR_H_
#define _PERF_MONITOR_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#include "connection_defs.h"
#include "dbtype_def.h"
#endif /* SERVER_MODE */
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "log_impl.h"
#endif // SERVER_MODE or SA_MODE
#include "memory_alloc.h"
#include "porting_inline.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"
#include "tsc_timer.h"

#include <assert.h>
#include <stdio.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* WINDOWS */
#include <time.h>

/* EXPORTED GLOBAL DEFINITIONS */
#define MAX_DIAG_DATA_VALUE     0xfffffffffffffLL

#define MAX_SERVER_THREAD_COUNT         500
#define MAX_SERVER_NAMELENGTH           256
#define SH_MODE 0644

/* Statistics activation flags */
typedef enum
{
  PERFMON_ACTIVATION_FLAG_DEFAULT = 0x00000000,
  PERFMON_ACTIVATION_FLAG_DETAILED_BTREE_PAGE = 0x00000001,
  PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT = 0x00000002,
  PERFMON_ACTIVATION_FLAG_LOCK_OBJECT = 0x00000004,
  PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR = 0x00000008,
  PERFMON_ACTIVATION_FLAG_PB_VICTIMIZATION = 0x00000010,
  PERFMON_ACTIVATION_FLAG_THREAD = 0x00000020,
  PERFMON_ACTIVATION_FLAG_DAEMONS = 0x00000040,
  PERFMON_ACTIVATION_FLAG_FLUSHED_BLOCK_VOLUMES = 0x00000080,
  PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_MAIN = 0x00000100,
  PERFMON_ACTIVATION_FLAG_LOG_RECOVERY_REDO_ASYNC = 0x00000200,
  PERFMON_ACTIVATION_FLAG_SERVED_COMPR_PAGE_TYPE = 0x00000400,

  /* must update when adding new conditions */
  PERFMON_ACTIVATION_FLAG_LAST = PERFMON_ACTIVATION_FLAG_SERVED_COMPR_PAGE_TYPE,

  PERFMON_ACTIVATION_FLAG_MAX_VALUE = (PERFMON_ACTIVATION_FLAG_LAST << 1) - 1
} PERFMON_ACTIVATION_FLAG;

/* PERF_MODULE_TYPE x PERF_PAGE_TYPE x PAGE_FETCH_MODE x HOLDER_LATCH_MODE x COND_FIX_TYPE */
#define PERF_PAGE_FIX_COUNTERS \
  ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) * (PERF_CONDITIONAL_FIX_CNT))

#define PERF_PAGE_PROMOTE_COUNTERS \
  ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * (PERF_PROMOTE_CONDITION_CNT) * (PERF_HOLDER_LATCH_CNT) * (2 /* success */))

/* PERF_MODULE_TYPE x PAGE_TYPE x DIRTY_OR_CLEAN x DIRTY_OR_CLEAN x READ_OR_WRITE_OR_MIX */
#define PERF_PAGE_UNFIX_COUNTERS \
  ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * 2 * 2 * (PERF_HOLDER_LATCH_CNT))

#define PERF_PAGE_LOCK_TIME_COUNTERS PERF_PAGE_FIX_COUNTERS

#define PERF_PAGE_HOLD_TIME_COUNTERS \
  ((PERF_MODULE_CNT) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT))

#define PERF_PAGE_FIX_TIME_COUNTERS PERF_PAGE_FIX_COUNTERS

#define PERF_PAGE_FIX_STAT_OFFSET(module,page_type,page_found_mode,latch_mode,cond_type) \
  ((module) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) * (PERF_CONDITIONAL_FIX_CNT) \
    + (page_type) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) * (PERF_CONDITIONAL_FIX_CNT) \
    + (page_found_mode) * (PERF_HOLDER_LATCH_CNT) * (PERF_CONDITIONAL_FIX_CNT) \
    + (latch_mode) * (PERF_CONDITIONAL_FIX_CNT) + (cond_type))

#define PERF_PAGE_PROMOTE_STAT_OFFSET(module,page_type,promote_cond,holder_latch,success) \
  ((module) * (PERF_PAGE_CNT) * (PERF_PROMOTE_CONDITION_CNT) * (PERF_HOLDER_LATCH_CNT) * 2 /* success */ \
    + (page_type) * (PERF_PROMOTE_CONDITION_CNT) * (PERF_HOLDER_LATCH_CNT) * 2 /* success */ \
    + (promote_cond) * (PERF_HOLDER_LATCH_CNT) * 2 /* success */ \
    + (holder_latch) * 2 /* success */ \
    + success)

#define PERF_PAGE_UNFIX_STAT_OFFSET(module,page_type,buf_dirty,dirtied_by_holder,holder_latch) \
  ((module) * (PERF_PAGE_CNT) * 2 * 2 * (PERF_HOLDER_LATCH_CNT) \
   + (page_type) * 2 * 2 * (PERF_HOLDER_LATCH_CNT) \
   + (buf_dirty) * 2 * (PERF_HOLDER_LATCH_CNT) \
   + (dirtied_by_holder) * (PERF_HOLDER_LATCH_CNT) \
   + (holder_latch))

#define PERF_PAGE_LOCK_TIME_OFFSET(module,page_type,page_found_mode,latch_mode,cond_type) \
  PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type)

#define PERF_PAGE_HOLD_TIME_OFFSET(module,page_type,page_found_mode,latch_mode)\
  ((module) * (PERF_PAGE_CNT) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) \
    + (page_type) * (PERF_PAGE_MODE_CNT) * (PERF_HOLDER_LATCH_CNT) \
    + (page_found_mode) * (PERF_HOLDER_LATCH_CNT) \
    + (latch_mode))

#define PERF_PAGE_FIX_TIME_OFFSET(module,page_type,page_found_mode,latch_mode,cond_type) \
  PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type)

#define PERF_MVCC_SNAPSHOT_COUNTERS \
  (PERF_SNAPSHOT_CNT * PERF_SNAPSHOT_RECORD_TYPE_CNT * PERF_SNAPSHOT_VISIBILITY_CNT)

#define PERF_MVCC_SNAPSHOT_OFFSET(snapshot,rec_type,visibility) \
  ((snapshot) * PERF_SNAPSHOT_RECORD_TYPE_CNT * PERF_SNAPSHOT_VISIBILITY_CNT \
   + (rec_type) * PERF_SNAPSHOT_VISIBILITY_CNT + (visibility))

#define PERF_OBJ_LOCK_STAT_COUNTERS (SCH_M_LOCK + 1)
#define PERF_DWB_FLUSHED_BLOCK_VOLUMES_CNT 10

/* PAGE_TYPE x (PAGE_COUNT + COMPR_RATIO_x_100) */
#define PERF_SERVED_COMPR_PAGE_TYPE_COUNTERS_SIZE \
  (PERF_PAGE_CNT * PERF_SERVED_COMPR_PAGE_TYPE_CNT)

#define PERF_SERVED_COMPR_PAGE_TYPE_COUNTERS_OFFSET(page_type, count_or_compress_ratio_index) \
  ((page_type) * PERF_SERVED_COMPR_PAGE_TYPE_CNT) + (count_or_compress_ratio_index)

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
// todo - remove from here
#if !defined(LOG_FIND_THREAD_TRAN_INDEX)
#define LOG_FIND_THREAD_TRAN_INDEX(thrd) \
  ((thrd) ? (thrd)->tran_index : logtb_get_current_tran_index())
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
  PERF_MODULE_REPLICATION,

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
  PERF_SERVED_COMPR_COUNT = 0,	/* per page type, this many pages have been compressed and served by page server */
  PERF_SERVED_COMPR_RATIO,	/* per page type, the average ratio of pages compressed and served by page server */
  PERF_SERVED_COMPR_SLOTTED,
  PERF_SERVED_COMPR_SLOTTED_NEEDS_COMPACT,

  PERF_SERVED_COMPR_PAGE_TYPE_CNT
} PERF_SERVED_COMPR_PAGE_TYPE;

typedef enum
{
  PSTAT_BASE = -1,		/* not a real entry. just to avoid compile warnings */

  /* Execution statistics for the file io */
  PSTAT_FILE_NUM_CREATES = 0,
  PSTAT_FILE_NUM_REMOVES,
  PSTAT_FILE_NUM_IOREADS,
  PSTAT_FILE_NUM_IOWRITES,
  PSTAT_FILE_NUM_IOSYNCHES,
  PSTAT_FILE_IOSYNC_ALL,
  PSTAT_FILE_NUM_PAGE_ALLOCS,
  PSTAT_FILE_NUM_PAGE_DEALLOCS,

  /* Page buffer basic module */
  /* Execution statistics for the page buffer manager */
  PSTAT_PB_NUM_FETCHES,
  PSTAT_PB_NUM_DIRTIES,
  PSTAT_PB_NUM_IOREADS,
  PSTAT_PB_NUM_IOWRITES,
  PSTAT_PB_NUM_FLUSHED,
  /* peeked stats */
  PSTAT_PB_PRIVATE_QUOTA,
  PSTAT_PB_PRIVATE_COUNT,
  PSTAT_PB_FIXED_CNT,
  PSTAT_PB_DIRTY_CNT,
  PSTAT_PB_LRU1_CNT,
  PSTAT_PB_LRU2_CNT,
  PSTAT_PB_LRU3_CNT,
  PSTAT_PB_VICT_CAND,

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
  PSTAT_TRAN_NUM_PPCACHE_HITS,
  PSTAT_TRAN_NUM_PPCACHE_MISS,
  PSTAT_TRAN_NUM_TOPOP_PPCACHE_HITS,
  PSTAT_TRAN_NUM_TOPOP_PPCACHE_MISS,

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

  PSTAT_BT_ONLINE_LOAD,
  PSTAT_BT_ONLINE_INSERT_TASK,
  PSTAT_BT_ONLINE_PREPARE_TASK,
  PSTAT_BT_ONLINE_INSERT_LEAF,

  PSTAT_BT_ONLINE_NUM_INSERTS,
  PSTAT_BT_ONLINE_NUM_INSERTS_SAME_PAGE_HOLD,
  PSTAT_BT_ONLINE_NUM_RETRY,
  PSTAT_BT_ONLINE_NUM_RETRY_NICE,

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

  /* Execution statistics for the heap manager */
  /* best space info */
  PSTAT_HEAP_STATS_SYNC_BESTSPACE,
  PSTAT_HF_NUM_STATS_ENTRIES,
  PSTAT_HF_NUM_STATS_MAXED,
  PSTAT_HF_BEST_SPACE_ADD,
  PSTAT_HF_BEST_SPACE_DEL,
  PSTAT_HF_BEST_SPACE_FIND,
  PSTAT_HF_HEAP_FIND_PAGE_BEST_SPACE,
  PSTAT_HF_HEAP_FIND_BEST_PAGE,

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

  /* Page buffer extended */
  /* detailed unfix module */
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
  /* hash anchor */
  PSTAT_PB_NUM_HASH_ANCHOR_WAITS,
  PSTAT_PB_TIME_HASH_ANCHOR_WAIT,
  /* flushing */
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
  PSTAT_PB_ASSIGN_DIRECT_BCB,
  PSTAT_PB_WAKE_FLUSH_WAITER,
  /* allocate and victim assignments */
  PSTAT_PB_ALLOC_BCB,
  PSTAT_PB_ALLOC_BCB_SEARCH_VICTIM,
  PSTAT_PB_ALLOC_BCB_COND_WAIT_HIGH_PRIO,
  PSTAT_PB_ALLOC_BCB_COND_WAIT_LOW_PRIO,
  PSTAT_PB_ALLOC_BCB_PRIORITIZE_VACUUM,
  PSTAT_PB_VICTIM_USE_INVALID_BCB,
  /* direct assignments */
  PSTAT_PB_VICTIM_SEARCH_OWN_PRIVATE_LISTS,
  PSTAT_PB_VICTIM_SEARCH_OTHERS_PRIVATE_LISTS,
  PSTAT_PB_VICTIM_SEARCH_SHARED_LISTS,
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

  /* DWB statistics */
  PSTAT_DWB_FLUSH_BLOCK_TIME_COUNTERS,
  PSTAT_DWB_FILE_SYNC_HELPER_TIME_COUNTERS,
  PSTAT_DWB_FLUSH_BLOCK_COND_WAIT,
  PSTAT_DWB_FLUSH_BLOCK_SORT_TIME_COUNTERS,
  PSTAT_DWB_DECACHE_PAGES_AFTER_WRITE,
  PSTAT_DWB_WAIT_FLUSH_BLOCK_TIME_COUNTERS,
  PSTAT_DWB_WAIT_FILE_SYNC_HELPER_TIME_COUNTERS,
  PSTAT_DWB_FLUSH_FORCE_TIME_COUNTERS,

  /* LOG LZ4 compress statistics */
  PSTAT_LOG_LZ4_COMPRESS_TIME_COUNTERS,
  PSTAT_LOG_LZ4_DECOMPRESS_TIME_COUNTERS,

  /* peeked stats */
  PSTAT_PB_WAIT_THREADS_HIGH_PRIO,
  PSTAT_PB_WAIT_THREADS_LOW_PRIO,
  PSTAT_PB_FLUSHED_BCBS_WAIT_FOR_ASSIGN,
  PSTAT_PB_LFCQ_BIG_PRV_NUM,
  PSTAT_PB_LFCQ_PRV_NUM,
  PSTAT_PB_LFCQ_SHR_NUM,
  PSTAT_PB_AVOID_DEALLOC_CNT,
  PSTAT_PB_AVOID_VICTIM_CNT,

  /* Redo recovery and replication statistics */
  PSTAT_REDO_REPL_DELAY,
  PSTAT_REDO_REPL_LOG_REDO_SYNC,
  PSTAT_LOG_REDO_FUNC_EXEC,
  PSTAT_COMPR_HEAP_PAGES_TRANSF_COMPRESSED,
  PSTAT_COMPR_HEAP_PAGES_TRANSF_UNCOMPRESSED,
  PSTAT_COMPR_HEAP_PAGES_TRANSF_RATIO,

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
  PSTAT_THREAD_STATS,
  PSTAT_THREAD_DAEMON_STATS,
  PSTAT_DWB_FLUSHED_BLOCK_NUM_VOLUMES,
  PSTAT_LOAD_THREAD_STATS,
  PSTAT_SERVED_COMPR_PAGE_TYPE_COUNTERS,

  /* IMPORTANT: only add complex statistics here; non-complex statistics
   * should be added before the complex entries; dump to file/buffer internal
   * functions depend on this invariant */

  PSTAT_COUNT
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
extern int perfmon_calc_diff_stats_for_trace (UINT64 * stats_diff, UINT64 * new_stats, UINT64 * old_stats);
extern int perfmon_initialize (int num_trans);
extern void perfmon_finalize (void);
extern int perfmon_get_number_of_statistic_values (void);
extern UINT64 *perfmon_allocate_values (void);
extern char *perfmon_allocate_packed_values_buffer (void);
extern void perfmon_copy_values (UINT64 * dest, const UINT64 * src);

#if defined (SERVER_MODE) || defined (SA_MODE)
extern void perfmon_start_watch (THREAD_ENTRY * thread_p);
extern void perfmon_stop_watch (THREAD_ENTRY * thread_p);
extern void perfmon_er_log_current_stats (THREAD_ENTRY * thread_p);
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
extern char *perfmon_pack_stats (char *buf, UINT64 * stats);
extern char *perfmon_unpack_stats (char *buf, UINT64 * stats);

STATIC_INLINE void perfmon_diff_timeval (struct timeval *elapsed, struct timeval *start, struct timeval *end)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void perfmon_add_timeval (struct timeval *total, struct timeval *start, struct timeval *end)
  __attribute__ ((ALWAYS_INLINE));

#ifdef __cplusplus
/* TODO: it looks ugly now, but it should be fixed with stat tool patch */

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
  ATOMIC_INC_64 (PSTAT_COUNTER_TIMER_COUNT_VALUE (statvalp), 1ULL);
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

#endif /* __cplusplus */

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

STATIC_INLINE void
perfmon_diff_timeval (struct timeval *elapsed, struct timeval *start, struct timeval *end)
{
  elapsed->tv_sec = end->tv_sec - start->tv_sec;
  elapsed->tv_usec = end->tv_usec - start->tv_usec;

  if (elapsed->tv_usec < 0)
    {
      elapsed->tv_sec--;
      elapsed->tv_usec += 1000000;
    }
}

STATIC_INLINE void
perfmon_add_timeval (struct timeval *total, struct timeval *start, struct timeval *end)
{
  if (end->tv_usec - start->tv_usec >= 0)
    {
      total->tv_usec += end->tv_usec - start->tv_usec;
      total->tv_sec += end->tv_sec - start->tv_sec;
    }
  else
    {
      total->tv_usec += 1000000 + (end->tv_usec - start->tv_usec);
      total->tv_sec += end->tv_sec - start->tv_sec - 1;
    }

  total->tv_sec += total->tv_usec / 1000000;
  total->tv_usec %= 1000000;
}

#define TO_MSEC(elapsed) \
  ((int)(((elapsed).tv_sec * 1000) + (int) ((elapsed).tv_usec / 1000)))

#if defined (EnableThreadMonitoring)
#define MONITOR_WAITING_THREAD(elapsed) \
    (prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD) > 0 \
     && ((elapsed).tv_sec * 1000 + (elapsed).tv_usec / 1000) \
         > prm_get_integer_value (PRM_ID_MNT_WAITING_THREAD))
#else
#define MONITOR_WAITING_THREAD(elapsed) (0)
#endif

#if defined (SERVER_MODE) || defined (SA_MODE)
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
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

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
extern void perfmon_db_flushed_block_volumes (THREAD_ENTRY * thread_p, int num_volumes);

extern void perfmon_compr_page_type (THREAD_ENTRY * thread_p, int page_type, int ratio, bool is_slotted,
				     bool needs_compact);

#endif /* SERVER_MODE || SA_MODE */

#endif /* _PERF_MONITOR_H_ */
