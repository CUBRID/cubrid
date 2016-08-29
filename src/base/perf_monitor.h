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

/* EXPORTED GLOBAL DEFINITIONS */
#define MAX_DIAG_DATA_VALUE     0xfffffffffffffLL

#define MAX_SERVER_THREAD_COUNT         500
#define MAX_SERVER_NAMELENGTH           256
#define SH_MODE 0644

/*
 * Enable these macros to add extended statistics information in statdump.
 * Some overhead is added, do not activate for production environment.
 * - PERF_ENABLE_DETAILED_BTREE_PAGE_STAT : 
 *    index pages are detailed by root, leaf, non-leaf 
 * - PERF_ENABLE_MVCC_SNAPSHOT_STAT
 *    partitioned information per snapshot function
 * - PERF_ENABLE_LOCK_OBJECT_STAT
 *    partitioned information per type of lock
 * - PERF_ENABLE_PB_HASH_ANCHOR_STAT
 *    count and time of data page buffer hash anchor
 */

#if 1
#define PERF_ENABLE_DETAILED_BTREE_PAGE_STAT
#define PERF_ENABLE_MVCC_SNAPSHOT_STAT
#define PERF_ENABLE_LOCK_OBJECT_STAT
#define PERF_ENABLE_PB_HASH_ANCHOR_STAT
#endif

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

#define PERF_OBJ_LOCK_STAT_OFFSET(module,lock_mode) \
  ((module) * (SCH_M_LOCK + 1) + (lock_mode))

#define PERF_OBJ_LOCK_STAT_COUNTERS (PERF_MODULE_CNT * (SCH_M_LOCK + 1))

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
#if defined(PERF_ENABLE_DETAILED_BTREE_PAGE_STAT)
  PERF_PAGE_BTREE_ROOT,		/* b+tree root index page */
  PERF_PAGE_BTREE_OVF,		/* b+tree overflow index page */
  PERF_PAGE_BTREE_LEAF,		/* b+tree leaf index page */
  PERF_PAGE_BTREE_NONLEAF,	/* b+tree nonleaf index page */
#endif /* PERF_ENABLE_DETAILED_BTREE_PAGE_STAT */
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

/*
 * Server execution statistic structure
 * If members are added or removed in this structure then changes must be made
 * also in MNT_SIZE_OF_SERVER_EXEC_STATS, STAT_SIZE_MEMORY and
 * MNT_SERVER_EXEC_STATS_SIZEOF
 */
typedef struct mnt_server_exec_stats MNT_SERVER_EXEC_STATS;
struct mnt_server_exec_stats
{
  /* Execution statistics for the file io */
  UINT64 file_num_creates;
  UINT64 file_num_removes;
  UINT64 file_num_ioreads;
  UINT64 file_num_iowrites;
  UINT64 file_num_iosynches;
  UINT64 file_num_page_allocs;
  UINT64 file_num_page_deallocs;

  /* Execution statistics for the page buffer manager */
  UINT64 pb_num_fetches;
  UINT64 pb_num_dirties;
  UINT64 pb_num_ioreads;
  UINT64 pb_num_iowrites;
  UINT64 pb_num_victims;
  UINT64 pb_num_replacements;
  UINT64 pb_num_hash_anchor_waits;
  UINT64 pb_time_hash_anchor_wait;
  /* peeked stats */
  UINT64 pb_fixed_cnt;
  UINT64 pb_dirty_cnt;
  UINT64 pb_lru1_cnt;
  UINT64 pb_lru2_cnt;
  UINT64 pb_ain_cnt;
  UINT64 pb_avoid_dealloc_cnt;
  UINT64 pb_avoid_victim_cnt;
  UINT64 pb_victim_cand_cnt;

  /* Execution statistics for the log manager */
  UINT64 log_num_fetches;
  UINT64 log_num_ioreads;
  UINT64 log_num_iowrites;
  UINT64 log_num_appendrecs;
  UINT64 log_num_archives;
  UINT64 log_num_start_checkpoints;
  UINT64 log_num_end_checkpoints;
  UINT64 log_num_wals;
  UINT64 log_num_replacements;

  /* Execution statistics for the lock manager */
  UINT64 lk_num_acquired_on_pages;
  UINT64 lk_num_acquired_on_objects;
  UINT64 lk_num_converted_on_pages;
  UINT64 lk_num_converted_on_objects;
  UINT64 lk_num_re_requested_on_pages;
  UINT64 lk_num_re_requested_on_objects;
  UINT64 lk_num_waited_on_pages;
  UINT64 lk_num_waited_on_objects;
  UINT64 lk_num_waited_time_on_objects;	/* include this to avoid client-server compat issue even if extended stats are
					 * disabled */

  /* Execution statistics for transactions */
  UINT64 tran_num_commits;
  UINT64 tran_num_rollbacks;
  UINT64 tran_num_savepoints;
  UINT64 tran_num_start_topops;
  UINT64 tran_num_end_topops;
  UINT64 tran_num_interrupts;

  /* Execution statistics for the btree manager */
  UINT64 bt_num_inserts;
  UINT64 bt_num_deletes;
  UINT64 bt_num_updates;
  UINT64 bt_num_covered;
  UINT64 bt_num_noncovered;
  UINT64 bt_num_resumes;
  UINT64 bt_num_multi_range_opt;
  UINT64 bt_num_splits;
  UINT64 bt_num_merges;
  UINT64 bt_num_get_stats;

  /* Execution statistics for the heap manager */
  UINT64 heap_num_stats_sync_bestspace;

  /* Execution statistics for the query manager */
  UINT64 qm_num_selects;
  UINT64 qm_num_inserts;
  UINT64 qm_num_deletes;
  UINT64 qm_num_updates;
  UINT64 qm_num_sscans;
  UINT64 qm_num_iscans;
  UINT64 qm_num_lscans;
  UINT64 qm_num_setscans;
  UINT64 qm_num_methscans;
  UINT64 qm_num_nljoins;
  UINT64 qm_num_mjoins;
  UINT64 qm_num_objfetches;
  UINT64 qm_num_holdable_cursors;

  /* Execution statistics for external sort */
  UINT64 sort_num_io_pages;
  UINT64 sort_num_data_pages;

  /* Execution statistics for network communication */
  UINT64 net_num_requests;

  /* flush control stat */
  UINT64 fc_num_pages;
  UINT64 fc_num_log_pages;
  UINT64 fc_tokens;

  /* prior lsa info */
  UINT64 prior_lsa_list_size;	/* kbytes */
  UINT64 prior_lsa_list_maxed;
  UINT64 prior_lsa_list_removed;

  /* best space info */
  UINT64 hf_num_stats_entries;
  UINT64 hf_num_stats_maxed;

  /* HA replication delay */
  UINT64 ha_repl_delay;

  /* Execution statistics for Plan cache */
  UINT64 pc_num_add;
  UINT64 pc_num_lookup;
  UINT64 pc_num_hit;
  UINT64 pc_num_miss;
  UINT64 pc_num_full;
  UINT64 pc_num_delete;
  UINT64 pc_num_invalid_xasl_id;
  UINT64 pc_num_query_string_hash_entries;
  UINT64 pc_num_xasl_id_hash_entries;
  UINT64 pc_num_class_oid_hash_entries;

  UINT64 vac_num_vacuumed_log_pages;
  UINT64 vac_num_to_vacuum_log_pages;
  UINT64 vac_num_prefetch_requests_log_pages;
  UINT64 vac_num_prefetch_hits_log_pages;

  /* Track heap modify counters. */
  UINT64 heap_home_inserts;
  UINT64 heap_big_inserts;
  UINT64 heap_assign_inserts;
  UINT64 heap_home_deletes;
  UINT64 heap_home_mvcc_deletes;
  UINT64 heap_home_to_rel_deletes;
  UINT64 heap_home_to_big_deletes;
  UINT64 heap_rel_deletes;
  UINT64 heap_rel_mvcc_deletes;
  UINT64 heap_rel_to_home_deletes;
  UINT64 heap_rel_to_big_deletes;
  UINT64 heap_rel_to_rel_deletes;
  UINT64 heap_big_deletes;
  UINT64 heap_big_mvcc_deletes;
  UINT64 heap_new_ver_inserts;
  UINT64 heap_home_updates;
  UINT64 heap_home_to_rel_updates;
  UINT64 heap_home_to_big_updates;
  UINT64 heap_rel_updates;
  UINT64 heap_rel_to_home_updates;
  UINT64 heap_rel_to_rel_updates;
  UINT64 heap_rel_to_big_updates;
  UINT64 heap_big_updates;
  UINT64 heap_home_vacuums;
  UINT64 heap_big_vacuums;
  UINT64 heap_rel_vacuums;
  UINT64 heap_insid_vacuums;
  UINT64 heap_remove_vacuums;
  UINT64 heap_next_ver_vacuums;

  /* Track heap modify timers. */
  UINT64 heap_insert_prepare;
  UINT64 heap_insert_execute;
  UINT64 heap_insert_log;
  UINT64 heap_delete_prepare;
  UINT64 heap_delete_execute;
  UINT64 heap_delete_log;
  UINT64 heap_update_prepare;
  UINT64 heap_update_execute;
  UINT64 heap_update_log;
  UINT64 heap_vacuum_prepare;
  UINT64 heap_vacuum_execute;
  UINT64 heap_vacuum_log;

  /* B-tree op counters. */
  UINT64 bt_find_unique_cnt;
  UINT64 bt_range_search_cnt;
  UINT64 bt_insert_cnt;
  UINT64 bt_delete_cnt;
  UINT64 bt_mvcc_delete_cnt;
  UINT64 bt_mark_delete_cnt;
  UINT64 bt_update_sk_cnt;
  UINT64 bt_undo_insert_cnt;
  UINT64 bt_undo_delete_cnt;
  UINT64 bt_undo_mvcc_delete_cnt;
  UINT64 bt_undo_update_sk_cnt;
  UINT64 bt_vacuum_cnt;
  UINT64 bt_vacuum_insid_cnt;
  UINT64 bt_vacuum_update_sk_cnt;
  UINT64 bt_fix_ovf_oids_cnt;
  UINT64 bt_unique_rlocks_cnt;
  UINT64 bt_unique_wlocks_cnt;

  /* B-tree op timers. */
  UINT64 bt_find_unique;
  UINT64 bt_range_search;
  UINT64 bt_insert;
  UINT64 bt_delete;
  UINT64 bt_mvcc_delete;
  UINT64 bt_mark_delete;
  UINT64 bt_update_sk;
  UINT64 bt_undo_insert;
  UINT64 bt_undo_delete;
  UINT64 bt_undo_mvcc_delete;
  UINT64 bt_undo_update_sk;
  UINT64 bt_vacuum;
  UINT64 bt_vacuum_insid;
  UINT64 bt_vacuum_update_sk;

  /* B-tree traversal timers. */
  UINT64 bt_traverse;
  UINT64 bt_find_unique_traverse;
  UINT64 bt_range_search_traverse;
  UINT64 bt_insert_traverse;
  UINT64 bt_delete_traverse;
  UINT64 bt_mvcc_delete_traverse;
  UINT64 bt_mark_delete_traverse;
  UINT64 bt_update_sk_traverse;
  UINT64 bt_undo_insert_traverse;
  UINT64 bt_undo_delete_traverse;
  UINT64 bt_undo_mvcc_delete_traverse;
  UINT64 bt_undo_update_sk_traverse;
  UINT64 bt_vacuum_traverse;
  UINT64 bt_vacuum_insid_traverse;
  UINT64 bt_vacuum_update_sk_traverse;

  /* B-tree timers to fix overflow OID's and to lock for unique. */
  UINT64 bt_fix_ovf_oids;
  UINT64 bt_unique_rlocks;
  UINT64 bt_unique_wlocks;

  /* Vacuum master/worker timers. */
  UINT64 vac_master;
  UINT64 vac_worker_process_log;
  UINT64 vac_worker_execute;

  /* Other statistics (change MNT_COUNT_OF_SERVER_EXEC_CALC_STATS) */
  /* ((pb_num_fetches - pb_num_ioreads) x 100 / pb_num_fetches) x 100 */
  UINT64 pb_hit_ratio;
  /* ((log_num_fetches - log_num_ioreads) x 100 / log_num_fetches) x 100 */
  UINT64 log_hit_ratio;
  /* ((fetches of vacuum - fetches of vacuum not found in PB) x 100 / fetches of vacuum) x 100 */
  UINT64 vacuum_data_hit_ratio;
  /* (100 x Number of unfix with of dirty pages of vacuum / total num of unfixes from vacuum) x 100 */
  UINT64 pb_vacuum_efficiency;
  /* (100 x Number of unfix from vacuum / total num of unfix) x 100 */
  UINT64 pb_vacuum_fetch_ratio;
  /* total time to acquire page lock (stored as 10 usec unit, displayed as miliseconds) */
  UINT64 pb_page_lock_acquire_time_10usec;
  /* total time to acquire page hold (stored as 10 usec unit, displayed as miliseconds) */
  UINT64 pb_page_hold_acquire_time_10usec;
  /* total time to acquire page fix (stored as 10 usec unit, displayed as miliseconds) */
  UINT64 pb_page_fix_acquire_time_10usec;
  /* ratio of time required to allocate a buffer for a page : (100 x (fix_time - lock_time - hold_time) / fix_time) x
   * 100 */
  UINT64 pb_page_allocate_time_ratio;
  /* total successful promotions */
  UINT64 pb_page_promote_success;
  /* total failed promotions */
  UINT64 pb_page_promote_failed;
  /* total promotion time */
  UINT64 pb_page_promote_total_time_10usec;

  /* array counters : do not include in MNT_SIZE_OF_SERVER_EXEC_SINGLE_STATS */
  /* change MNT_COUNT_OF_SERVER_EXEC_ARRAY_STATS and MNT_SIZE_OF_SERVER_EXEC_ARRAY_STATS */
  UINT64 pbx_fix_counters[PERF_PAGE_FIX_COUNTERS];
  UINT64 pbx_promote_counters[PERF_PAGE_PROMOTE_COUNTERS];
  UINT64 pbx_promote_time_counters[PERF_PAGE_PROMOTE_COUNTERS];
  UINT64 pbx_unfix_counters[PERF_PAGE_UNFIX_COUNTERS];
  UINT64 pbx_lock_time_counters[PERF_PAGE_LOCK_TIME_COUNTERS];
  UINT64 pbx_hold_time_counters[PERF_PAGE_HOLD_TIME_COUNTERS];
  UINT64 pbx_fix_time_counters[PERF_PAGE_FIX_TIME_COUNTERS];
  UINT64 mvcc_snapshot_counters[PERF_MVCC_SNAPSHOT_COUNTERS];
  UINT64 obj_lock_time_counters[PERF_OBJ_LOCK_STAT_COUNTERS];
  UINT64 log_snapshot_time_counters[PERF_MODULE_CNT];
  UINT64 log_snapshot_retry_counters[PERF_MODULE_CNT];
  UINT64 log_tran_complete_time_counters[PERF_MODULE_CNT];
  UINT64 log_oldest_mvcc_time_counters[PERF_MODULE_CNT];
  UINT64 log_oldest_mvcc_retry_counters[PERF_MODULE_CNT];

  /* This must be kept as last member. Otherwise the MNT_SERVER_EXEC_STATS_SIZEOF macro must be modified */
  bool enable_local_stat;	/* used for local stats */
};

/* number of fields of MNT_SERVER_EXEC_STATS structure (includes computed stats) */
#define MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS 202

/* number of array stats of MNT_SERVER_EXEC_STATS structure */
#define MNT_COUNT_OF_SERVER_EXEC_ARRAY_STATS 14

/* number of computed stats of MNT_SERVER_EXEC_STATS structure */
#define MNT_COUNT_OF_SERVER_EXEC_CALC_STATS 12

#define MNT_SIZE_OF_SERVER_EXEC_SINGLE_STATS \
  MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS

#define MNT_SERVER_EXEC_STATS_COUNT \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS \
   + MNT_COUNT_OF_SERVER_EXEC_ARRAY_STATS)

#define MNT_SIZE_OF_SERVER_EXEC_ARRAY_STATS \
(PERF_PAGE_FIX_COUNTERS + PERF_PAGE_PROMOTE_COUNTERS \
 + PERF_PAGE_PROMOTE_COUNTERS + PERF_PAGE_UNFIX_COUNTERS \
 + PERF_PAGE_LOCK_TIME_COUNTERS + PERF_PAGE_HOLD_TIME_COUNTERS \
 + PERF_PAGE_FIX_TIME_COUNTERS + PERF_MVCC_SNAPSHOT_COUNTERS \
 + PERF_OBJ_LOCK_STAT_COUNTERS + PERF_MODULE_CNT + PERF_MODULE_CNT \
 + PERF_MODULE_CNT + PERF_MODULE_CNT + PERF_MODULE_CNT)

#define MNT_SIZE_OF_SERVER_EXEC_STATS \
  (MNT_SIZE_OF_SERVER_EXEC_SINGLE_STATS \
   + MNT_SIZE_OF_SERVER_EXEC_ARRAY_STATS)

/* The exact size of mnt_server_exec_stats structure */
#define MNT_SERVER_EXEC_STATS_SIZEOF \
  (offsetof (MNT_SERVER_EXEC_STATS, enable_local_stat) + sizeof (bool))

#define MNT_SERVER_PBX_FIX_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 0)
#define MNT_SERVER_PROMOTE_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 1)
#define MNT_SERVER_PROMOTE_TIME_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 2)
#define MNT_SERVER_PBX_UNFIX_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 3)
#define MNT_SERVER_PBX_LOCK_TIME_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 4)
#define MNT_SERVER_PBX_HOLD_TIME_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 5)
#define MNT_SERVER_PBX_FIX_TIME_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 6)
#define MNT_SERVER_MVCC_SNAPSHOT_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 7)
#define MNT_SERVER_OBJ_LOCK_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 8)
#define MNT_SERVER_SNAPSHOT_TIME_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 9)
#define MNT_SERVER_SNAPSHOT_RETRY_CNT_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 10)
#define MNT_SERVER_TRAN_COMPLETE_TIME_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 11)
#define MNT_SERVER_OLDEST_MVCC_TIME_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 12)
#define MNT_SERVER_OLDEST_MVCC_RETRY_CNT_STAT_POSITION \
  (MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS + 13)

extern void mnt_server_dump_stats (const MNT_SERVER_EXEC_STATS * stats, FILE * stream, const char *substr);

extern void mnt_server_dump_stats_to_buffer (const MNT_SERVER_EXEC_STATS * stats, char *buffer, int buf_size,
					     const char *substr);

extern void mnt_get_current_times (time_t * cpu_usr_time, time_t * cpu_sys_time, time_t * elapsed_time);

extern int mnt_calc_diff_stats (MNT_SERVER_EXEC_STATS * stats_diff, MNT_SERVER_EXEC_STATS * new_stats,
				MNT_SERVER_EXEC_STATS * old_stats);

#if defined(CS_MODE) || defined(SA_MODE)
/* Client execution statistic structure */
typedef struct mnt_client_stat_info MNT_CLIENT_STAT_INFO;
struct mnt_client_stat_info
{
  time_t cpu_start_usr_time;
  time_t cpu_start_sys_time;
  time_t elapsed_start_time;
  MNT_SERVER_EXEC_STATS *base_server_stats;
  MNT_SERVER_EXEC_STATS *current_server_stats;
  MNT_SERVER_EXEC_STATS *old_global_stats;
  MNT_SERVER_EXEC_STATS *current_global_stats;
};

extern bool mnt_Iscollecting_stats;

extern int mnt_start_stats (bool for_all_trans);
extern int mnt_stop_stats (void);
extern void mnt_reset_stats (void);
extern void mnt_print_stats (FILE * stream);
extern void mnt_print_global_stats (FILE * stream, bool cumulative, const char *substr);
extern MNT_SERVER_EXEC_STATS *mnt_get_stats (void);
extern MNT_SERVER_EXEC_STATS *mnt_get_global_stats (void);
extern int mnt_get_global_diff_stats (MNT_SERVER_EXEC_STATS * diff_stats);
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
      (track)->is_perf_tracking = mnt_is_perf_tracking (thread_p); \
      if ((track)->is_perf_tracking) tsc_getticks (&(track)->start_tick); \
    } \
  while (false)
#define PERF_UTIME_TRACKER_TIME(thread_p, track, callback) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      callback (thread_p, \
		tsc_elapsed_utime ((track)->end_tick,  (track)->start_tick)); \
    } \
  while (false)
#define PERF_UTIME_TRACKER_TIME_AND_RESTART(thread_p, track, callback) \
  do \
    { \
      if (!(track)->is_perf_tracking) break; \
      tsc_getticks (&(track)->end_tick); \
      callback (thread_p, \
		tsc_elapsed_utime ((track)->end_tick,  (track)->start_tick)); \
      (track)->start_tick = (track)->end_tick; \
    } \
  while (false)

#if defined(SERVER_MODE) || defined (SA_MODE)
extern int mnt_Num_tran_exec_stats;

#define mnt_is_perf_tracking(thread_p) \
  ((mnt_Num_tran_exec_stats > 0) ? true : false)
/*
 * Statistics at file io level
 */
#define mnt_file_creates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_creates(thread_p)
#define mnt_file_removes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_removes(thread_p)
#define mnt_file_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_ioreads(thread_p)
#define mnt_file_iowrites(thread_p, num_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_iowrites(thread_p, num_pages)
#define mnt_file_iosynches(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_iosynches(thread_p)
#define mnt_file_page_allocs(thread_p, num_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_page_allocs(thread_p, num_pages)
#define mnt_file_page_deallocs(thread_p, num_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_file_page_deallocs(thread_p, num_pages)

/*
 * Statistics at page level
 */
#define mnt_pb_fetches(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_fetches(thread_p)
#define mnt_pb_dirties(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_dirties(thread_p)
#define mnt_pb_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_ioreads(thread_p)
#define mnt_pb_iowrites(thread_p, num_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_iowrites(thread_p, num_pages)
#define mnt_pb_victims(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_victims(thread_p)
#define mnt_pb_replacements(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_replacements(thread_p)
#define mnt_pb_num_hash_anchor_waits(thread_p, time_amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pb_num_hash_anchor_waits(thread_p, \
								  time_amount)

/*
 * Statistics at log level
 */
#define mnt_log_fetches(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_fetches(thread_p)
#define mnt_log_ioreads(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_ioreads(thread_p)
#define mnt_log_iowrites(thread_p, num_log_pages) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_iowrites(thread_p, num_log_pages)
#define mnt_log_appendrecs(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_appendrecs(thread_p)
#define mnt_log_archives(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_archives(thread_p)
#define mnt_log_start_checkpoints(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_start_checkpoints(thread_p)
#define mnt_log_end_checkpoints(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_end_checkpoints(thread_p)
#define mnt_log_wals(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_wals(thread_p)
#define mnt_log_replacements(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_log_replacements(thread_p)

/*
 * Statistics at lock level
 */
#define mnt_lk_acquired_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_acquired_on_pages(thread_p)
#define mnt_lk_acquired_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_acquired_on_objects(thread_p)
#define mnt_lk_converted_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_converted_on_pages(thread_p)
#define mnt_lk_converted_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_converted_on_objects(thread_p)
#define mnt_lk_re_requested_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_re_requested_on_pages(thread_p)
#define mnt_lk_re_requested_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_re_requested_on_objects(thread_p)
#define mnt_lk_waited_on_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_waited_on_pages(thread_p)
#define mnt_lk_waited_on_objects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_waited_on_objects(thread_p)
#define mnt_lk_waited_time_on_objects(thread_p, lock_mode, time_usec) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_lk_waited_time_on_objects(thread_p, \
								   lock_mode, \
								   time_usec)

/*
 * Transaction Management level
 */
#define mnt_tran_commits(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_commits(thread_p)
#define mnt_tran_rollbacks(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_rollbacks(thread_p)
#define mnt_tran_savepoints(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_savepoints(thread_p)
#define mnt_tran_start_topops(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_start_topops(thread_p)
#define mnt_tran_end_topops(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_end_topops(thread_p)
#define mnt_tran_interrupts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_interrupts(thread_p)

/*
 * Statistics at btree level
 */
#define mnt_bt_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_inserts(thread_p)
#define mnt_bt_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_deletes(thread_p)
#define mnt_bt_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_updates(thread_p)
#define mnt_bt_covered(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_covered(thread_p)
#define mnt_bt_noncovered(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_noncovered(thread_p)
#define mnt_bt_resumes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_resumes(thread_p)
#define mnt_bt_multi_range_opt(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_multi_range_opt(thread_p)
#define mnt_bt_splits(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_splits(thread_p)
#define mnt_bt_merges(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_merges(thread_p)
#define mnt_bt_get_stats(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_bt_get_stats(thread_p)

/* Execution statistics for the query manager */
#define mnt_qm_selects(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_selects(thread_p)
#define mnt_qm_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_inserts(thread_p)
#define mnt_qm_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_deletes(thread_p)
#define mnt_qm_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_updates(thread_p)
#define mnt_qm_sscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_sscans(thread_p)
#define mnt_qm_iscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_iscans(thread_p)
#define mnt_qm_lscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_lscans(thread_p)
#define mnt_qm_setscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_setscans(thread_p)
#define mnt_qm_methscans(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_methscans(thread_p)
#define mnt_qm_nljoins(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_nljoins(thread_p)
#define mnt_qm_mjoins(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_mjoins(thread_p)
#define mnt_qm_objfetches(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_objfetches(thread_p)
#define mnt_qm_holdable_cursor(thread_p, num_cursors) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_qm_holdable_cursor(thread_p, num_cursors)

/* execution statistics for external sort */
#define mnt_sort_io_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0 && thread_get_sort_stats_active(thread_p)) \
    mnt_x_sort_io_pages(thread_p)
#define mnt_sort_data_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0 && thread_get_sort_stats_active(thread_p)) \
    mnt_x_sort_data_pages(thread_p)

/* Prior LSA */
#define mnt_prior_lsa_list_size(thread_p, list_size) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_prior_lsa_list_size(thread_p, list_size)
#define mnt_prior_lsa_list_maxed(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_prior_lsa_list_maxed(thread_p)
#define mnt_prior_lsa_list_removed(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_prior_lsa_list_removed(thread_p)

/* Heap best space info */
#define mnt_hf_stats_bestspace_entries(thread_p, num_entries) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_hf_stats_bestspace_entries(thread_p, num_entries)
#define mnt_hf_stats_bestspace_maxed(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_hf_stats_bestspace_maxed(thread_p)

/* Statistics at Flush Control */
#define mnt_fc_stats(thread_p, num_pages, num_overflows, tokens) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_fc_stats(thread_p, num_pages, num_overflows, tokens)

/* Network Communication level */
#define mnt_net_requests(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_net_requests(thread_p)

/* Plan cache */
#define mnt_pc_add(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_add(thread_p)
#define mnt_pc_lookup(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_lookup(thread_p)
#define mnt_pc_hit(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_hit(thread_p)
#define mnt_pc_miss(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_miss(thread_p)
#define mnt_pc_full(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_full(thread_p)
#define mnt_pc_delete(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_delete(thread_p)
#define mnt_pc_invalid_xasl_id(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_invalid_xasl_id(thread_p)
#define mnt_pc_query_string_hash_entries(thread_p, num_entries) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_query_string_hash_entries(thread_p, num_entries)
#define mnt_pc_xasl_id_hash_entries(thread_p, num_entries) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_xasl_id_hash_entries(thread_p, num_entries)
#define mnt_pc_class_oid_hash_entries(thread_p, num_entries) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pc_class_oid_hash_entries(thread_p, num_entries)

/* Execution statistics for the heap manager */
#define mnt_heap_stats_sync_bestspace(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_stats_sync_bestspace(thread_p)

#define mnt_vac_log_vacuumed_pages(thread_p, num_entries) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_vac_log_vacuumed_pages(thread_p, num_entries)

#define mnt_vac_log_to_vacuum_pages(thread_p, num_entries) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_vac_log_to_vacuum_pages(thread_p, num_entries)

#define mnt_vac_prefetch_log_requests_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_vac_prefetch_log_requests_pages(thread_p)

#define mnt_vac_prefetch_log_hits_pages(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_vac_prefetch_log_hits_pages(thread_p)

#define mnt_pbx_fix(thread_p,page_type,page_found_mode,latch_mode,cond_type) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pbx_fix(thread_p, page_type, \
						 page_found_mode,latch_mode, \
						 cond_type)
#define mnt_pbx_promote(thread_p,page_type,promote_cond,holder_latch,success, amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pbx_promote(thread_p, page_type, \
						     promote_cond, \
						     holder_latch, \
						     success, amount)
#define mnt_pbx_unfix(thread_p,page_type,buf_dirty,dirtied_by_holder,holder_latch) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pbx_unfix(thread_p, page_type, \
						   buf_dirty, \
						   dirtied_by_holder, \
						   holder_latch)
#define mnt_pbx_hold_acquire_time(thread_p,page_type,page_found_mode,latch_mode,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pbx_hold_acquire_time(thread_p, \
							       page_type, \
							       page_found_mode, \
							       latch_mode, \
							       amount)
#define mnt_pbx_lock_acquire_time(thread_p,page_type,page_found_mode,latch_mode,cond_type,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pbx_lock_acquire_time(thread_p, \
							       page_type, \
							       page_found_mode, \
							       latch_mode, \
							       cond_type, \
							       amount)
#define mnt_pbx_fix_acquire_time(thread_p,page_type,page_found_mode,latch_mode,cond_type,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_pbx_fix_acquire_time(thread_p, \
							      page_type, \
							      page_found_mode, \
							      latch_mode, \
							      cond_type, \
							      amount)
#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
#define mnt_mvcc_snapshot(thread_p,snapshot,rec_type,visibility) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_mvcc_snapshot(thread_p, \
						       snapshot, \
						       rec_type, \
						       visibility)
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */
#define mnt_snapshot_acquire_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_snapshot_acquire_time(thread_p, \
							       amount)
#define mnt_snapshot_retry_counters(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_snapshot_retry_counters(thread_p, \
								 amount)
#define mnt_tran_complete_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_tran_complete_time(thread_p, amount)
#define mnt_oldest_mvcc_acquire_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_oldest_mvcc_acquire_time(thread_p, \
								  amount)
#define mnt_oldest_mvcc_retry_counters(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_oldest_mvcc_retry_counters(thread_p, \
								    amount)

#define mnt_heap_home_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_inserts (thread_p)
#define mnt_heap_big_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_big_inserts (thread_p)
#define mnt_heap_assign_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_assign_inserts (thread_p)
#define mnt_heap_home_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_deletes (thread_p)
#define mnt_heap_home_mvcc_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_mvcc_deletes (thread_p)
#define mnt_heap_home_to_rel_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_to_rel_deletes (thread_p)
#define mnt_heap_home_to_big_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_to_big_deletes (thread_p)
#define mnt_heap_rel_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_deletes (thread_p)
#define mnt_heap_rel_mvcc_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_mvcc_deletes (thread_p)
#define mnt_heap_rel_to_home_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_to_home_deletes (thread_p)
#define mnt_heap_rel_to_big_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_to_big_deletes (thread_p)
#define mnt_heap_rel_to_rel_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_to_rel_deletes (thread_p)
#define mnt_heap_big_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_big_deletes (thread_p)
#define mnt_heap_big_mvcc_deletes(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_big_mvcc_deletes (thread_p)
#define mnt_heap_new_ver_inserts(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_new_ver_inserts (thread_p)
#define mnt_heap_home_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_updates (thread_p)
#define mnt_heap_home_to_rel_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_to_rel_updates (thread_p)
#define mnt_heap_home_to_big_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_to_big_updates (thread_p)
#define mnt_heap_rel_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_updates (thread_p)
#define mnt_heap_rel_to_home_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_to_home_updates (thread_p)
#define mnt_heap_rel_to_rel_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_to_rel_updates (thread_p)
#define mnt_heap_rel_to_big_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_to_big_updates (thread_p)
#define mnt_heap_big_updates(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_big_updates (thread_p)
#define mnt_heap_home_vacuums(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_home_vacuums (thread_p)
#define mnt_heap_big_vacuums(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_big_vacuums (thread_p)
#define mnt_heap_rel_vacuums(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_rel_vacuums (thread_p)
#define mnt_heap_insid_vacuums(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_insid_vacuums (thread_p)
#define mnt_heap_remove_vacuums(thread_p) \
  if (mnt_Num_tran_exec_stats > 0) mnt_x_heap_remove_vacuums (thread_p)

#define mnt_heap_insert_prepare_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_insert_prepare_time (thread_p, amount)
#define mnt_heap_insert_execute_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_insert_execute_time (thread_p, amount)
#define mnt_heap_insert_log_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_insert_log_time (thread_p, amount)
#define mnt_heap_delete_prepare_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_delete_prepare_time (thread_p, amount)
#define mnt_heap_delete_execute_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_delete_execute_time (thread_p, amount)
#define mnt_heap_delete_log_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_delete_log_time (thread_p, amount)
#define mnt_heap_update_prepare_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_update_prepare_time (thread_p, amount)
#define mnt_heap_update_execute_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_update_execute_time (thread_p, amount)
#define mnt_heap_update_log_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_update_log_time (thread_p, amount)
#define mnt_heap_vacuum_prepare_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_vacuum_prepare_time (thread_p, amount)
#define mnt_heap_vacuum_execute_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_vacuum_execute_time (thread_p, amount)
#define mnt_heap_vacuum_log_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_heap_vacuum_log_time (thread_p, amount)

#define mnt_bt_find_unique_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_find_unique_time (thread_p, amount)
#define mnt_bt_range_search_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_range_search_time (thread_p, amount)
#define mnt_bt_insert_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_insert_time (thread_p, amount)
#define mnt_bt_delete_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_delete_time (thread_p, amount)
#define mnt_bt_mvcc_delete_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_mvcc_delete_time (thread_p, amount)
#define mnt_bt_mark_delete_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_mark_delete_time (thread_p, amount)
#define mnt_bt_undo_insert_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_undo_insert_time (thread_p, amount)
#define mnt_bt_undo_delete_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_undo_delete_time (thread_p, amount)
#define mnt_bt_undo_mvcc_delete_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_undo_mvcc_delete_time (thread_p, amount)
#define mnt_bt_vacuum_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_vacuum_time (thread_p, amount)
#define mnt_bt_vacuum_insid_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_vacuum_insid_time (thread_p, amount)
#define mnt_bt_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_traverse_time (thread_p, amount)
#define mnt_bt_find_unique_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_find_unique_traverse_time (thread_p, amount)
#define mnt_bt_range_search_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_range_search_traverse_time (thread_p, amount)
#define mnt_bt_insert_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_insert_traverse_time (thread_p, amount)
#define mnt_bt_delete_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_delete_traverse_time (thread_p, amount)
#define mnt_bt_mvcc_delete_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_mvcc_delete_traverse_time (thread_p, amount)
#define mnt_bt_mark_delete_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_mark_delete_traverse_time (thread_p, amount)
#define mnt_bt_undo_insert_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_undo_insert_traverse_time (thread_p, amount)
#define mnt_bt_undo_delete_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_undo_delete_traverse_time (thread_p, amount)
#define mnt_bt_undo_mvcc_delete_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_undo_mvcc_delete_traverse_time (thread_p, amount)
#define mnt_bt_vacuum_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_vacuum_traverse_time (thread_p, amount)
#define mnt_bt_vacuum_insid_traverse_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_vacuum_insid_traverse_time (thread_p, amount)
#define mnt_bt_fix_ovf_oids_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_fix_ovf_oids_time (thread_p, amount)
#define mnt_bt_unique_rlocks_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_unique_rlocks_time (thread_p, amount)
#define mnt_bt_unique_wlocks_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_bt_unique_wlocks_time (thread_p, amount)

#define mnt_vac_master_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_vac_master_time (thread_p, amount)
#define mnt_vac_worker_process_log_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_vac_worker_process_log_time (thread_p, amount)
#define mnt_vac_worker_execute_time(thread_p,amount) \
  if (mnt_Num_tran_exec_stats > 0) \
    mnt_x_vac_worker_execute_time (thread_p, amount)


extern bool mnt_server_is_stats_on (THREAD_ENTRY * thread_p);

extern int mnt_server_init (int num_tran_indices);
extern void mnt_server_final (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void mnt_server_print_stats (THREAD_ENTRY * thread_p, FILE * stream);
#endif
extern void mnt_x_file_creates (THREAD_ENTRY * thread_p);
extern void mnt_x_file_removes (THREAD_ENTRY * thread_p);
extern void mnt_x_file_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_file_iowrites (THREAD_ENTRY * thread_p, int num_pages);
extern void mnt_x_file_iosynches (THREAD_ENTRY * thread_p);
extern void mnt_x_file_page_allocs (THREAD_ENTRY * thread_p, int num_pages);
extern void mnt_x_file_page_deallocs (THREAD_ENTRY * thread_p, int num_pages);
extern void mnt_x_pb_fetches (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_dirties (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_iowrites (THREAD_ENTRY * thread_p, int num_pages);
extern void mnt_x_pb_victims (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_replacements (THREAD_ENTRY * thread_p);
extern void mnt_x_pb_num_hash_anchor_waits (THREAD_ENTRY * thread_p, UINT64 time_amount);
extern void mnt_x_log_fetches (THREAD_ENTRY * thread_p);
extern void mnt_x_log_ioreads (THREAD_ENTRY * thread_p);
extern void mnt_x_log_iowrites (THREAD_ENTRY * thread_p, int num_log_pages);
extern void mnt_x_log_appendrecs (THREAD_ENTRY * thread_p);
extern void mnt_x_log_archives (THREAD_ENTRY * thread_p);
extern void mnt_x_log_start_checkpoints (THREAD_ENTRY * thread_p);
extern void mnt_x_log_end_checkpoints (THREAD_ENTRY * thread_p);
extern void mnt_x_log_wals (THREAD_ENTRY * thread_p);
extern void mnt_x_log_replacements (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_acquired_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_acquired_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_converted_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_converted_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_re_requested_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_re_requested_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_waited_on_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_waited_on_objects (THREAD_ENTRY * thread_p);
extern void mnt_x_lk_waited_time_on_objects (THREAD_ENTRY * thread_p, int lock_mode, UINT64 amount);
extern void mnt_x_tran_commits (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_rollbacks (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_savepoints (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_start_topops (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_end_topops (THREAD_ENTRY * thread_p);
extern void mnt_x_tran_interrupts (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_covered (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_noncovered (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_resumes (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_multi_range_opt (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_splits (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_merges (THREAD_ENTRY * thread_p);
extern void mnt_x_bt_get_stats (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_selects (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_sscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_iscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_lscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_setscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_methscans (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_nljoins (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_mjoins (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_objfetches (THREAD_ENTRY * thread_p);
extern void mnt_x_qm_holdable_cursor (THREAD_ENTRY * thread_p, int num_cursors);
extern void mnt_x_sort_io_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_sort_data_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_net_requests (THREAD_ENTRY * thread_p);

extern void mnt_x_prior_lsa_list_size (THREAD_ENTRY * thread_p, unsigned int list_size);
extern void mnt_x_prior_lsa_list_maxed (THREAD_ENTRY * thread_p);
extern void mnt_x_prior_lsa_list_removed (THREAD_ENTRY * thread_p);

extern void mnt_x_hf_stats_bestspace_entries (THREAD_ENTRY * thread_p, unsigned int num_entries);
extern void mnt_x_hf_stats_bestspace_maxed (THREAD_ENTRY * thread_p);

extern void mnt_x_fc_stats (THREAD_ENTRY * thread_p, unsigned int num_pages, unsigned int num_log_pages,
			    unsigned int tokens);
extern UINT64 mnt_x_get_stats_and_clear (THREAD_ENTRY * thread_p, const char *stat_name);
extern void mnt_x_ha_repl_delay (THREAD_ENTRY * thread_p, int delay);

extern UINT64 mnt_get_pb_fetches (THREAD_ENTRY * thread_p);
extern UINT64 mnt_get_pb_ioreads (THREAD_ENTRY * thread_p);
extern UINT64 mnt_get_sort_io_pages (THREAD_ENTRY * thread_p);
extern UINT64 mnt_get_sort_data_pages (THREAD_ENTRY * thread_p);

extern void mnt_x_pc_add (THREAD_ENTRY * thread_p);
extern void mnt_x_pc_lookup (THREAD_ENTRY * thread_p);
extern void mnt_x_pc_hit (THREAD_ENTRY * thread_p);
extern void mnt_x_pc_miss (THREAD_ENTRY * thread_p);
extern void mnt_x_pc_full (THREAD_ENTRY * thread_p);
extern void mnt_x_pc_delete (THREAD_ENTRY * thread_p);
extern void mnt_x_pc_invalid_xasl_id (THREAD_ENTRY * thread_p);
extern void mnt_x_pc_query_string_hash_entries (THREAD_ENTRY * thread_p, unsigned int num_entries);
extern void mnt_x_pc_xasl_id_hash_entries (THREAD_ENTRY * thread_p, unsigned int num_entries);
extern void mnt_x_pc_class_oid_hash_entries (THREAD_ENTRY * thread_p, unsigned int num_entries);

extern void mnt_x_heap_stats_sync_bestspace (THREAD_ENTRY * thread_p);

extern void mnt_x_vac_log_vacuumed_pages (THREAD_ENTRY * thread_p, unsigned int num_entries);
extern void mnt_x_vac_log_to_vacuum_pages (THREAD_ENTRY * thread_p, unsigned int num_entries);
extern void mnt_x_vac_prefetch_log_requests_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_vac_prefetch_log_hits_pages (THREAD_ENTRY * thread_p);
extern void mnt_x_pbx_fix (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode, int cond_type);
extern void mnt_x_pbx_promote (THREAD_ENTRY * thread_p, int page_type, int promote_cond, int holder_latch, int success,
			       UINT64 amount);
extern void mnt_x_pbx_unfix (THREAD_ENTRY * thread_p, int page_type, int buf_dirty, int dirtied_by_holder,
			     int holder_latch);
extern void mnt_x_pbx_lock_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
					 int cond_type, UINT64 amount);
extern void mnt_x_pbx_hold_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
					 UINT64 amount);
extern void mnt_x_pbx_fix_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode,
					int cond_type, UINT64 amount);
#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
extern void mnt_x_mvcc_snapshot (THREAD_ENTRY * thread_p, int snapshot, int rec_type, int visibility);
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */
extern void mnt_x_snapshot_acquire_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_snapshot_retry_counters (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_tran_complete_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_oldest_mvcc_acquire_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_oldest_mvcc_retry_counters (THREAD_ENTRY * thread_p, UINT64 amount);



extern void mnt_x_heap_home_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_big_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_assign_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_mvcc_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_to_rel_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_to_big_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_mvcc_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_to_home_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_to_big_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_to_rel_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_big_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_big_mvcc_deletes (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_new_ver_inserts (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_to_rel_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_to_big_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_to_home_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_to_rel_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_to_big_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_big_updates (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_home_vacuums (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_big_vacuums (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_rel_vacuums (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_insid_vacuums (THREAD_ENTRY * thread_p);
extern void mnt_x_heap_remove_vacuums (THREAD_ENTRY * thread_p);

extern void mnt_x_heap_insert_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_insert_execute_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_insert_log_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_delete_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_delete_execute_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_delete_log_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_update_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_update_execute_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_update_log_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_vacuum_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_vacuum_execute_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_heap_vacuum_log_time (THREAD_ENTRY * thread_p, UINT64 amount);

extern void mnt_x_bt_find_unique_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_range_search_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_insert_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_delete_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_mvcc_delete_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_mark_delete_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_update_sk_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_insert_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_delete_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_mvcc_delete_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_update_sk_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_vacuum_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_vacuum_insid_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_vacuum_update_sk_time (THREAD_ENTRY * thread_p, UINT64 amount);

extern void mnt_x_bt_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_find_unique_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_range_search_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_insert_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_mvcc_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_mark_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_update_sk_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_insert_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_mvcc_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_undo_update_sk_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_vacuum_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_vacuum_insid_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_vacuum_update_sk_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount);

extern void mnt_x_bt_fix_ovf_oids_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_unique_rlocks_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_bt_unique_wlocks_time (THREAD_ENTRY * thread_p, UINT64 amount);

extern void mnt_x_vac_master_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_vac_worker_process_log_time (THREAD_ENTRY * thread_p, UINT64 amount);
extern void mnt_x_vac_worker_execute_time (THREAD_ENTRY * thread_p, UINT64 amount);

#else /* SERVER_MODE || SA_MODE */

#define mnt_file_creates(thread_p)
#define mnt_file_removes(thread_p)
#define mnt_file_ioreads(thread_p)
#define mnt_file_iowrites(thread_p, num_pages)
#define mnt_file_iosynches(thread_p)
#define mnt_file_page_allocs(thread_p, num_pages)
#define mnt_file_page_deallocs(thread_p, num_pages)

#define mnt_pb_fetches(thread_p)
#define mnt_pb_dirties(thread_p)
#define mnt_pb_ioreads(thread_p)
#define mnt_pb_iowrites(thread_p, num_pages)
#define mnt_pb_victims(thread_p)
#define mnt_pb_replacements(thread_p)
#define mnt_pb_num_hash_anchor_waits(thread_p, time_amount)

#define mnt_log_fetches(thread_p)
#define mnt_log_ioreads(thread_p)
#define mnt_log_iowrites(thread_p, num_log_pages)
#define mnt_log_appendrecs(thread_p)
#define mnt_log_archives(thread_p)
#define mnt_log_start_checkpoints(thread_p)
#define mnt_log_end_checkpoints(thread_p)
#define mnt_log_wals(thread_p)
#define mnt_log_replacements(thread_p)

#define mnt_lk_acquired_on_pages(thread_p)
#define mnt_lk_acquired_on_objects(thread_p)
#define mnt_lk_converted_on_pages(thread_p)
#define mnt_lk_converted_on_objects(thread_p)
#define mnt_lk_re_requested_on_pages(thread_p)
#define mnt_lk_re_requested_on_objects(thread_p)
#define mnt_lk_waited_on_pages(thread_p)
#define mnt_lk_waited_on_objects(thread_p)
#define mnt_lk_waited_time_on_objects(thread_p, lock_mode, time_usec)

#define mnt_tran_commits(thread_p)
#define mnt_tran_rollbacks(thread_p)
#define mnt_tran_savepoints(thread_p)
#define mnt_tran_start_topops(thread_p)
#define mnt_tran_end_topops(thread_p)
#define mnt_tran_interrupts(thread_p)

#define mnt_bt_inserts(thread_p)
#define mnt_bt_deletes(thread_p)
#define mnt_bt_updates(thread_p)
#define mnt_bt_covered(thread_p)
#define mnt_bt_noncovered(thread_p)
#define mnt_bt_resumes(thread_p)
#define mnt_bt_multi_range_opt(thread_p)
#define mnt_bt_splits(thread_p)
#define mnt_bt_merges(thread_p)
#define mnt_bt_get_stat(thread_p)

#define mnt_qm_selects(thread_p)
#define mnt_qm_inserts(thread_p)
#define mnt_qm_deletes(thread_p)
#define mnt_qm_updates(thread_p)
#define mnt_qm_sscans(thread_p)
#define mnt_qm_iscans(thread_p)
#define mnt_qm_lscans(thread_p)
#define mnt_qm_setscans(thread_p)
#define mnt_qm_methscans(thread_p)
#define mnt_qm_nljoins(thread_p)
#define mnt_qm_mjoins(thread_p)
#define mnt_qm_objfetches(thread_p)
#define mnt_qm_holdable_cursor(thread_p, num_cursors)

#define mnt_net_requests(thread_p)

#define mnt_prior_lsa_list_size(thread_p, list_size)
#define mnt_prior_lsa_list_maxed(thread_p)
#define mnt_prior_lsa_list_removed(thread_p)

#define mnt_hf_stats_bestspace_entries(thread_p, num_entries)
#define mnt_hf_stats_bestspace_maxed(thread_p)

#define mnt_fc_stats(thread_p, num_pages, num_log_pages, num_tokens)

#define mnt_pc_add(thread_p)
#define mnt_pc_lookup(thread_p)
#define mnt_pc_hit(thread_p)
#define mnt_pc_miss(thread_p)
#define mnt_pc_full(thread_p)
#define mnt_pc_delete(thread_p)
#define mnt_pc_invalid_xasl_id(thread_p)
#define mnt_pc_query_string_hash_entries(thread_p, num_entries)
#define mnt_pc_xasl_id_hash_entries(thread_p, num_entries)
#define mnt_pc_class_oid_hash_entries(thread_p, num_entries)

#define mnt_heap_stats_sync_bestspace(thread_p)

#define mnt_vac_log_vacuumed_pages(thread_p, num_entries)
#define mnt_vac_log_to_vacuum_pages(thread_p, num_entries)
#define mnt_vac_prefetch_log_requests_pages(thread_p)
#define mnt_vac_prefetch_log_hits_pages(thread_p)

#define mnt_pbx_fix(thread_p,page_type,page_found_mode,latch_mode,cond_type)
#define mnt_pbx_promote(thread_p,page_type,promote_cond,holder_latch,success,amount)
#define mnt_pbx_unfix(thread_p,page_type,buf_dirty,dirtied_by_holder,holder_latch)
#define mnt_pbx_hold_acquire_time(thread_p,page_type,page_found_mode,latch_mode,amount)
#define mnt_pbx_lock_acquire_time(thread_p,page_type,page_found_mode,latch_mode,cond_type,amount)
#define mnt_pbx_fix_acquire_time(thread_p,page_type,page_found_mode,latch_mode,cond_type,amount)
#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
#define mnt_mvcc_snapshot(thread_p,snapshot,rec_type,visibility)
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */
#define mnt_snapshot_acquire_time(thread_p,amount)
#define mnt_snapshot_retry_counters(thread_p,amount)
#define mnt_tran_complete_time(thread_p,amount)
#define mnt_oldest_mvcc_acquire_time(thread_p,amount)
#define mnt_oldest_mvcc_retry_counters(thread_p,amount)

#define mnt_heap_home_inserts(thread_p)
#define mnt_heap_big_inserts(thread_p)
#define mnt_heap_assign_inserts(thread_p)
#define mnt_heap_home_deletes(thread_p)
#define mnt_heap_home_mvcc_deletes(thread_p)
#define mnt_heap_home_to_rel_deletes(thread_p)
#define mnt_heap_home_to_big_deletes(thread_p)
#define mnt_heap_rel_deletes(thread_p)
#define mnt_heap_rel_mvcc_deletes(thread_p)
#define mnt_heap_rel_to_home_deletes(thread_p)
#define mnt_heap_rel_to_big_deletes(thread_p)
#define mnt_heap_rel_to_rel_deletes(thread_p)
#define mnt_heap_big_deletes(thread_p)
#define mnt_heap_big_mvcc_deletes(thread_p)
#define mnt_heap_new_ver_inserts(thread_p)
#define mnt_heap_home_updates(thread_p)
#define mnt_heap_home_to_rel_updates(thread_p)
#define mnt_heap_home_to_big_updates(thread_p)
#define mnt_heap_rel_updates(thread_p)
#define mnt_heap_rel_to_home_updates(thread_p)
#define mnt_heap_rel_to_rel_updates(thread_p)
#define mnt_heap_rel_to_big_updates(thread_p)
#define mnt_heap_big_updates(thread_p)
#define mnt_heap_home_vacuums(thread_p)
#define mnt_heap_big_vacuums(thread_p)
#define mnt_heap_rel_vacuums(thread_p)
#define mnt_heap_insid_vacuums(thread_p)
#define mnt_heap_remove_vacuums(thread_p)
#define mnt_heap_next_ver_vacuums(thread_p)

#define mnt_heap_insert_prepare_time(thread_p,amount)
#define mnt_heap_insert_execute_time(thread_p,amount)
#define mnt_heap_insert_log_time(thread_p,amount)
#define mnt_heap_delete_prepare_time(thread_p,amount)
#define mnt_heap_delete_execute_time(thread_p,amount)
#define mnt_heap_delete_log_time(thread_p,amount)
#define mnt_heap_update_prepare_time(thread_p,amount)
#define mnt_heap_update_execute_time(thread_p,amount)
#define mnt_heap_update_log_time(thread_p,amount)
#define mnt_heap_vacuum_prepare_time(thread_p,amount)
#define mnt_heap_vacuum_execute_time(thread_p,amount)
#define mnt_heap_vacuum_log_time(thread_p,amount)

#define mnt_bt_find_unique_time(thread_p,amount)
#define mnt_bt_range_search_time(thread_p,amount)
#define mnt_bt_insert_time(thread_p,amount)
#define mnt_bt_delete_time(thread_p,amount)
#define mnt_bt_mvcc_delete_time(thread_p,amount)
#define mnt_bt_mark_delete_time(thread_p,amount)
#define mnt_bt_undo_insert_time(thread_p,amount)
#define mnt_bt_undo_delete_time(thread_p,amount)
#define mnt_bt_undo_mvcc_delete_time(thread_p,amount)
#define mnt_bt_vacuum_time(thread_p,amount)
#define mnt_bt_vacuum_insid_time(thread_p,amount)

#define mnt_bt_traverse_time(thread_p,amount)
#define mnt_bt_find_unique_traverse_time(thread_p,amount)
#define mnt_bt_range_search_traverse_time(thread_p,amount)
#define mnt_bt_insert_traverse_time(thread_p,amount)
#define mnt_bt_delete_traverse_time(thread_p,amount)
#define mnt_bt_mvcc_delete_traverse_time(thread_p,amount)
#define mnt_bt_mark_delete_traverse_time(thread_p,amount)
#define mnt_bt_undo_insert_traverse_time(thread_p,amount)
#define mnt_bt_undo_delete_traverse_time(thread_p,amount)
#define mnt_bt_undo_mvcc_delete_traverse_time(thread_p,amount)
#define mnt_bt_vacuum_traverse_time(thread_p,amount)
#define mnt_bt_vacuum_insid_traverse_time(thread_p,amount)

#define mnt_bt_fix_ovf_oids_time(thread_p,amount)
#define mnt_bt_unique_slocks_time(thread_p,amount)
#define mnt_bt_unique_wlocks_time(thread_p,amount)

#define mnt_vac_master_time(thread_p,amount)
#define mnt_vac_worker_process_log_time(thread_p,amount)
#define mnt_vac_worker_execute_time(thread_p,amount)

#endif /* CS_MODE */

#endif /* _PERF_MONITOR_H_ */
