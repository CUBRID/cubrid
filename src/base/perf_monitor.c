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

#include "error_manager.h"
#include "object_representation.h"
#if !defined(SERVER_MODE)
#include "memory_alloc.h"
#include "server_interface.h"
#endif /* !SERVER_MODE */
#include "thread_worker_pool.hpp"
#if defined (SERVER_MODE)
#include "thread_daemon.hpp"
#endif // SERVER_MODE
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif // SERVER_MODE or SA_MODE

#include <cstring>

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

#if defined (SERVER_MODE) || defined (SA_MODE)
#include <string.h>

#include "log_impl.h"
#include "session.h"
#include "error_manager.h"
#include "log_manager.h"
#include "server_support.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "heap_file.h"
#include "vacuum.h"
#include "xasl_cache.h"
#include "load_worker_manager.hpp"

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
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

#if !defined (SERVER_MODE)
#include "network_interface_cl.h"
#endif /* !defined (SERVER_MODE) */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Custom values. */
#define PSTAT_VALUE_CUSTOM	      0x00000001

#define PSTAT_METADATA_INIT_SINGLE_ACC(id, name) { id, name, PSTAT_ACCUMULATE_SINGLE_VALUE, 0, 0, NULL, NULL, NULL }
#define PSTAT_METADATA_INIT_SINGLE_PEEK(id, name) \
  { id, name, PSTAT_PEEK_SINGLE_VALUE, 0, 0, NULL, NULL, NULL }
#define PSTAT_METADATA_INIT_COUNTER_TIMER(id, name) { id, name, PSTAT_COUNTER_TIMER_VALUE, 0, 0, NULL, NULL, NULL }
#define PSTAT_METADATA_INIT_COMPUTED_RATIO(id, name) \
  { id, name, PSTAT_COMPUTED_RATIO_VALUE, 0, 0, NULL, NULL, NULL }
#define PSTAT_METADATA_INIT_COMPLEX(id, name, f_dump_in_file, f_dump_in_buffer, f_load) \
  { id, name, PSTAT_COMPLEX_VALUE, 0, 0, f_dump_in_file, f_dump_in_buffer, f_load }

#define PERFMON_VALUES_MEMSIZE (pstat_Global.n_stat_values * sizeof (UINT64))

static int f_load_Num_data_page_fix_ext (void);
static int f_load_Num_data_page_promote_ext (void);
static int f_load_Num_data_page_promote_time_ext (void);
static int f_load_Num_data_page_unfix_ext (void);
static int f_load_Time_data_page_lock_acquire_time (void);
static int f_load_Time_data_page_hold_acquire_time (void);
static int f_load_Time_data_page_fix_acquire_time (void);
static int f_load_Num_mvcc_snapshot_ext (void);
static int f_load_Time_obj_lock_acquire_time (void);
static int f_load_Num_dwb_flushed_block_volumes (void);
static int f_load_Time_get_snapshot_acquire_time (void);
static int f_load_Count_get_snapshot_retry (void);
static int f_load_Time_tran_complete_time (void);
static int f_load_Time_get_oldest_mvcc_acquire_time (void);
static int f_load_Count_get_oldest_mvcc_retry (void);
static int f_load_thread_stats (void);
static int f_load_thread_daemon_stats (void);

static void f_dump_in_file_Num_data_page_fix_ext (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Num_data_page_promote_ext (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Num_data_page_promote_time_ext (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Num_data_page_unfix_ext (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Time_data_page_lock_acquire_time (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Time_data_page_hold_acquire_time (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Time_data_page_fix_acquire_time (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Num_mvcc_snapshot_ext (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_Time_obj_lock_acquire_time (FILE *, const UINT64 * stat_vals);
static void f_dump_in_file_thread_stats (FILE * f, const UINT64 * stat_vals);
static void f_dump_in_file_thread_daemon_stats (FILE * f, const UINT64 * stat_vals);
static void f_dump_in_file_Num_dwb_flushed_block_volumes (FILE *, const UINT64 * stat_vals);

static void f_dump_in_buffer_Num_data_page_fix_ext (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Num_data_page_promote_ext (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Num_data_page_promote_time_ext (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Num_data_page_unfix_ext (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Time_data_page_lock_acquire_time (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Time_data_page_hold_acquire_time (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Time_data_page_fix_acquire_time (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Num_mvcc_snapshot_ext (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Time_obj_lock_acquire_time (char **, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_thread_stats (char **s, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_thread_daemon_stats (char **s, const UINT64 * stat_vals, int *remaining_size);
static void f_dump_in_buffer_Num_dwb_flushed_block_volumes (char **s, const UINT64 * stat_vals, int *remaining_size);

static void perfmon_stat_dump_in_file_fix_page_array_stat (FILE *, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_promote_page_array_stat (FILE *, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_unfix_page_array_stat (FILE *, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_page_lock_time_array_stat (FILE *, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_page_hold_time_array_stat (FILE *, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_page_fix_time_array_stat (FILE *, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_snapshot_array_stat (FILE *, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_thread_stats (FILE * stream, const UINT64 * stats_ptr);
static void perfmon_stat_dump_in_file_thread_daemon_stats (FILE * stream, const UINT64 * stats_ptr);

static void perfmon_stat_dump_in_buffer_fix_page_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size);
static void perfmon_stat_dump_in_buffer_promote_page_array_stat (const UINT64 * stats_ptr, char **s,
								 int *remaining_size);
static void perfmon_stat_dump_in_buffer_unfix_page_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size);
static void perfmon_stat_dump_in_buffer_page_lock_time_array_stat (const UINT64 * stats_ptr, char **s,
								   int *remaining_size);
static void perfmon_stat_dump_in_buffer_page_hold_time_array_stat (const UINT64 * stats_ptr, char **s,
								   int *remaining_size);
static void perfmon_stat_dump_in_buffer_page_fix_time_array_stat (const UINT64 * stats_ptr, char **s,
								  int *remaining_size);
static void perfmon_stat_dump_in_buffer_snapshot_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size);
static void perfmon_stat_dump_in_buffer_thread_stats (const UINT64 * stats_ptr, char **s, int *remaining_size);
static void perfmon_stat_dump_in_buffer_thread_daemon_stats (const UINT64 * stats_ptr, char **s, int *remaining_size);

static void perfmon_print_timer_to_file (FILE * stream, int stat_index, UINT64 * stats_ptr);
static void perfmon_print_timer_to_buffer (char **s, int stat_index, UINT64 * stats_ptr, int *remained_size);

STATIC_INLINE size_t thread_stats_count (void) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE size_t perfmon_thread_daemon_stats_count (void) __attribute__ ((ALWAYS_INLINE));
#if defined (SERVER_MODE)
static void perfmon_peek_thread_daemon_stats (UINT64 * stats);
#endif // SERVER_MODE

PSTAT_GLOBAL pstat_Global;

PSTAT_METADATA pstat_Metadata[] = {
  /* Execution statistics for the file io */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_CREATES, "Num_file_creates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_REMOVES, "Num_file_removes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_IOREADS, "Num_file_ioreads"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_IOWRITES, "Num_file_iowrites"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_IOSYNCHES, "Num_file_iosynches"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_FILE_IOSYNC_ALL, "file_iosync_all"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_PAGE_ALLOCS, "Num_file_page_allocs"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_PAGE_DEALLOCS, "Num_file_page_deallocs"),

  /* Page buffer basic module */
  /* Execution statistics for the page buffer manager */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_FETCHES, "Num_data_page_fetches"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_DIRTIES, "Num_data_page_dirties"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_IOREADS, "Num_data_page_ioreads"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_IOWRITES, "Num_data_page_iowrites"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_FLUSHED, "Num_data_page_flushed"),
  /* peeked stats */
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_PRIVATE_QUOTA, "Num_data_page_private_quota"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_PRIVATE_COUNT, "Num_data_page_private_count"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_FIXED_CNT, "Num_data_page_fixed"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_DIRTY_CNT, "Num_data_page_dirty"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LRU1_CNT, "Num_data_page_lru1"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LRU2_CNT, "Num_data_page_lru2"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LRU3_CNT, "Num_data_page_lru3"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_VICT_CAND, "Num_data_page_victim_candidate"),

  /* Execution statistics for the log manager */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_FETCHES, "Num_log_page_fetches"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_IOREADS, "Num_log_page_ioreads"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_IOWRITES, "Num_log_page_iowrites"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_APPENDRECS, "Num_log_append_records"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_ARCHIVES, "Num_log_archives"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_START_CHECKPOINTS, "Num_log_start_checkpoints"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_END_CHECKPOINTS, "Num_log_end_checkpoints"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_WALS, "Num_log_wals"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_REPLACEMENTS_IOWRITES, "Num_log_page_iowrites_for_replacement"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_NUM_REPLACEMENTS, "Num_log_page_replacements"),

  /* Execution statistics for the lock manager */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_ACQUIRED_ON_PAGES, "Num_page_locks_acquired"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS, "Num_object_locks_acquired"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_CONVERTED_ON_PAGES, "Num_page_locks_converted"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_CONVERTED_ON_OBJECTS, "Num_object_locks_converted"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_RE_REQUESTED_ON_PAGES, "Num_page_locks_re-requested"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_RE_REQUESTED_ON_OBJECTS, "Num_object_locks_re-requested"),
  /* TODO: Count and timer */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_WAITED_ON_PAGES, "Num_page_locks_waits"),
  /* TODO: Count and timer */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_WAITED_ON_OBJECTS, "Num_object_locks_waits"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LK_NUM_WAITED_TIME_ON_OBJECTS, "Num_object_locks_time_waited_usec"),

  /* Execution statistics for transactions */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_COMMITS, "Num_tran_commits"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_ROLLBACKS, "Num_tran_rollbacks"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_SAVEPOINTS, "Num_tran_savepoints"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_START_TOPOPS, "Num_tran_start_topops"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_END_TOPOPS, "Num_tran_end_topops"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_INTERRUPTS, "Num_tran_interrupts"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_PPCACHE_HITS, "Num_tran_postpone_cache_hits"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_PPCACHE_MISS, "Num_tran_postpone_cache_miss"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_TOPOP_PPCACHE_HITS, "Num_tran_topop_postpone_cache_hits"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_TRAN_NUM_TOPOP_PPCACHE_MISS, "Num_tran_topop_postpone_cache_miss"),

  /* Execution statistics for the btree manager */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_INSERTS, "Num_btree_inserts"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_DELETES, "Num_btree_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_UPDATES, "Num_btree_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_COVERED, "Num_btree_covered"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_NONCOVERED, "Num_btree_noncovered"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_RESUMES, "Num_btree_resumes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_MULTI_RANGE_OPT, "Num_btree_multirange_optimization"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_SPLITS, "Num_btree_splits"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_MERGES, "Num_btree_merges"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_NUM_GET_STATS, "Num_btree_get_stats"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_ONLINE_LOAD, "btree_online_load"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_ONLINE_INSERT_TASK, "btree_online_insert_task"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_ONLINE_PREPARE_TASK, "btree_online_prepare_task"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_ONLINE_INSERT_LEAF, "btree_online_insert_leaf"),

  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_ONLINE_NUM_INSERTS, "Num_btree_online_inserts"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_ONLINE_NUM_INSERTS_SAME_PAGE_HOLD,
				  "Num_btree_online_inserts_same_page_hold"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_ONLINE_NUM_RETRY, "Num_btree_online_inserts_retry"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_BT_ONLINE_NUM_RETRY_NICE, "Num_btree_online_inserts_retry_nice"),

  /* Execution statistics for the query manager */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_SELECTS, "Num_query_selects"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_INSERTS, "Num_query_inserts"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_DELETES, "Num_query_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_UPDATES, "Num_query_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_SSCANS, "Num_query_sscans"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_ISCANS, "Num_query_iscans"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_LSCANS, "Num_query_lscans"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_SETSCANS, "Num_query_setscans"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_METHSCANS, "Num_query_methscans"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_NLJOINS, "Num_query_nljoins"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_MJOINS, "Num_query_mjoins"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_QM_NUM_OBJFETCHES, "Num_query_objfetches"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_QM_NUM_HOLDABLE_CURSORS, "Num_query_holdable_cursors"),

  /* Execution statistics for external sort */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_SORT_NUM_IO_PAGES, "Num_sort_io_pages"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_SORT_NUM_DATA_PAGES, "Num_sort_data_pages"),

  /* Execution statistics for network communication */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_NET_NUM_REQUESTS, "Num_network_requests"),

  /* flush control stat */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FC_NUM_PAGES, "Num_adaptive_flush_pages"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FC_NUM_LOG_PAGES, "Num_adaptive_flush_log_pages"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FC_TOKENS, "Num_adaptive_flush_max_pages"),

  /* prior lsa info */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PRIOR_LSA_LIST_SIZE, "Num_prior_lsa_list_size"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PRIOR_LSA_LIST_MAXED, "Num_prior_lsa_list_maxed"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PRIOR_LSA_LIST_REMOVED, "Num_prior_lsa_list_removed"),

  /* HA replication delay */
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_HA_REPL_DELAY, "Time_ha_replication_delay"),

  /* Execution statistics for Plan cache */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PC_NUM_ADD, "Num_plan_cache_add"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PC_NUM_LOOKUP, "Num_plan_cache_lookup"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PC_NUM_HIT, "Num_plan_cache_hit"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PC_NUM_MISS, "Num_plan_cache_miss"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PC_NUM_FULL, "Num_plan_cache_full"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PC_NUM_DELETE, "Num_plan_cache_delete"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PC_NUM_INVALID_XASL_ID, "Num_plan_cache_invalid_xasl_id"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PC_NUM_CACHE_ENTRIES, "Num_plan_cache_entries"),

  /* Vacuum process log section. */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_VAC_NUM_VACUUMED_LOG_PAGES, "Num_vacuum_log_pages_vacuumed"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_VAC_NUM_TO_VACUUM_LOG_PAGES, "Num_vacuum_log_pages_to_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_VAC_NUM_PREFETCH_REQUESTS_LOG_PAGES, "Num_vacuum_prefetch_requests_log_pages"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_VAC_NUM_PREFETCH_HITS_LOG_PAGES, "Num_vacuum_prefetch_hits_log_pages"),

  /* Track heap modify counters. */
  /* Make a complex entry for heap stats */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_INSERTS, "Num_heap_home_inserts"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_BIG_INSERTS, "Num_heap_big_inserts"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_ASSIGN_INSERTS, "Num_heap_assign_inserts"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_DELETES, "Num_heap_home_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_MVCC_DELETES, "Num_heap_home_mvcc_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_TO_REL_DELETES, "Num_heap_home_to_rel_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_TO_BIG_DELETES, "Num_heap_home_to_big_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_DELETES, "Num_heap_rel_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_MVCC_DELETES, "Num_heap_rel_mvcc_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_TO_HOME_DELETES, "Num_heap_rel_to_home_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_TO_BIG_DELETES, "Num_heap_rel_to_big_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_TO_REL_DELETES, "Num_heap_rel_to_rel_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_BIG_DELETES, "Num_heap_big_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_BIG_MVCC_DELETES, "Num_heap_big_mvcc_deletes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_UPDATES, "Num_heap_home_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_TO_REL_UPDATES, "Num_heap_home_to_rel_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_TO_BIG_UPDATES, "Num_heap_home_to_big_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_UPDATES, "Num_heap_rel_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_TO_HOME_UPDATES, "Num_heap_rel_to_home_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_TO_REL_UPDATES, "Num_heap_rel_to_rel_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_TO_BIG_UPDATES, "Num_heap_rel_to_big_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_BIG_UPDATES, "Num_heap_big_updates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_HOME_VACUUMS, "Num_heap_home_vacuums"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_BIG_VACUUMS, "Num_heap_big_vacuums"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REL_VACUUMS, "Num_heap_rel_vacuums"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_INSID_VACUUMS, "Num_heap_insid_vacuums"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_REMOVE_VACUUMS, "Num_heap_remove_vacuums"),

  /* Track heap modify timers. */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_INSERT_PREPARE, "heap_insert_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_INSERT_EXECUTE, "heap_insert_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_INSERT_LOG, "heap_insert_log"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_DELETE_PREPARE, "heap_delete_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_DELETE_EXECUTE, "heap_delete_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_DELETE_LOG, "heap_delete_log"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_UPDATE_PREPARE, "heap_update_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_UPDATE_EXECUTE, "heap_update_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_UPDATE_LOG, "heap_update_log"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_VACUUM_PREPARE, "heap_vacuum_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_VACUUM_EXECUTE, "heap_vacuum_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_VACUUM_LOG, "heap_vacuum_log"),

  /* Execution statistics for the heap manager */
  /* best space info */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_STATS_SYNC_BESTSPACE, "heap_stats_sync_bestspace"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_HF_NUM_STATS_ENTRIES, "Num_heap_stats_bestspace_entries"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HF_NUM_STATS_MAXED, "Num_heap_stats_bestspace_maxed"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HF_BEST_SPACE_ADD, "bestspace_add"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HF_BEST_SPACE_DEL, "bestspace_del"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HF_BEST_SPACE_FIND, "bestspace_find"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HF_HEAP_FIND_PAGE_BEST_SPACE, "heap_find_page_bestspace"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HF_HEAP_FIND_BEST_PAGE, "heap_find_best_page"),

  /* B-tree detailed statistics. */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_FIX_OVF_OIDS, "bt_fix_ovf_oids"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNIQUE_RLOCKS, "bt_unique_rlocks"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNIQUE_WLOCKS, "bt_unique_wlocks"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_LEAF, "bt_leaf"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_TRAVERSE, "bt_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_FIND_UNIQUE, "bt_find_unique"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_FIND_UNIQUE_TRAVERSE, "bt_find_unique_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_RANGE_SEARCH, "bt_range_search"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_RANGE_SEARCH_TRAVERSE, "bt_range_search_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_INSERT, "bt_insert"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_INSERT_TRAVERSE, "bt_insert_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_DELETE, "bt_delete_obj"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_DELETE_TRAVERSE, "bt_delete_obj_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_MVCC_DELETE, "bt_mvcc_delete"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_MVCC_DELETE_TRAVERSE, "bt_mvcc_delete_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_MARK_DELETE, "bt_mark_delete"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_MARK_DELETE_TRAVERSE, "bt_mark_delete_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNDO_INSERT, "bt_undo_insert"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNDO_INSERT_TRAVERSE, "bt_undo_insert_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNDO_DELETE, "bt_undo_delete"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNDO_DELETE_TRAVERSE, "bt_undo_delete_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNDO_MVCC_DELETE, "bt_undo_mvcc_delete"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_UNDO_MVCC_DELETE_TRAVERSE, "bt_undo_mvcc_delete_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_VACUUM, "bt_vacuum"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_VACUUM_TRAVERSE, "bt_vacuum_traverse"),

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_VACUUM_INSID, "bt_vacuum_insid"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_BT_VACUUM_INSID_TRAVERSE, "bt_vacuum_insid_traverse"),

  /* Vacuum master/worker timers. */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_VAC_MASTER, "vacuum_master"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_VAC_JOB, "vacuum_job"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_VAC_WORKER_PROCESS_LOG, "vacuum_worker_process_log"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_VAC_WORKER_EXECUTE, "vacuum_worker_execute"),

  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_SNAPSHOT_TIME_COUNTERS, "Time_get_snapshot_acquire_time"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_SNAPSHOT_RETRY_COUNTERS, "Count_get_snapshot_retry"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_TRAN_COMPLETE_TIME_COUNTERS, "Time_tran_complete_time"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_LOG_OLDEST_MVCC_TIME_COUNTERS, "compute_oldest_visible"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_OLDEST_MVCC_RETRY_COUNTERS, "Count_get_oldest_mvcc_retry"),

  /* computed statistics */
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_HIT_RATIO, "Data_page_buffer_hit_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_LOG_HIT_RATIO, "Log_page_buffer_hit_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_VACUUM_DATA_HIT_RATIO, "Vacuum_data_page_buffer_hit_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_VACUUM_EFFICIENCY, "Vacuum_page_efficiency_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_VACUUM_FETCH_RATIO, "Vacuum_page_fetch_ratio"),

  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_LOCK_ACQUIRE_TIME_10USEC, "Data_page_fix_lock_acquire_time_msec"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_HOLD_ACQUIRE_TIME_10USEC, "Data_page_fix_hold_acquire_time_msec"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC, "Data_page_fix_acquire_time_msec"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_ALLOCATE_TIME_RATIO, "Data_page_allocate_time_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_PROMOTE_SUCCESS, "Data_page_total_promote_success"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_PROMOTE_FAILED, "Data_page_total_promote_fail"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_PROMOTE_TOTAL_TIME_10USEC, "Data_page_total_promote_time_msec"),

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
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ASSIGN_DIRECT_BCB, "assign_direct_bcb"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_WAKE_FLUSH_WAITER, "wake_flush_waiter"),
  /* allocate and victim assignments */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB, "alloc_bcb"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB_SEARCH_VICTIM, "alloc_bcb_search_victim"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB_COND_WAIT_HIGH_PRIO, "alloc_bcb_cond_wait_high_prio"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_ALLOC_BCB_COND_WAIT_LOW_PRIO, "alloc_bcb_cond_wait_low_prio"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_ALLOC_BCB_PRIORITIZE_VACUUM, "Num_alloc_bcb_prioritize_vacuum"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_USE_INVALID_BCB, "Num_victim_use_invalid_bcb"),
  /* direct assignments */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_VICTIM_SEARCH_OWN_PRIVATE_LISTS,
				     "alloc_bcb_get_victim_search_own_private_list"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_VICTIM_SEARCH_OTHERS_PRIVATE_LISTS,
				     "alloc_bcb_get_victim_search_others_private_list"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_PB_VICTIM_SEARCH_SHARED_LISTS, "alloc_bcb_get_victim_search_shared_list"),
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

  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_FLUSH_BLOCK_TIME_COUNTERS, "DWB_flush_block"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_FILE_SYNC_HELPER_TIME_COUNTERS, "DWB_file_sync_helper"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_FLUSH_BLOCK_COND_WAIT, "DWB_flush_block_cond_wait"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_FLUSH_BLOCK_SORT_TIME_COUNTERS, "DWB_flush_block_sort"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_DECACHE_PAGES_AFTER_WRITE, "DWB_decache_pages_after_write"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_WAIT_FLUSH_BLOCK_TIME_COUNTERS, "DWB_wait_flush_block"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_WAIT_FILE_SYNC_HELPER_TIME_COUNTERS, "DWB_wait_file_sync_helper"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_DWB_FLUSH_FORCE_TIME_COUNTERS, "DWB_flush_force"),

  /* Log LZ4 compression statistics */
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_LOG_LZ4_COMPRESS_TIME_COUNTERS, "Log_LZ4_compress"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_LOG_LZ4_DECOMPRESS_TIME_COUNTERS, "Log_LZ4_decompress"),

  /* peeked stats */
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_WAIT_THREADS_HIGH_PRIO, "Num_alloc_bcb_wait_threads_high_priority"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_WAIT_THREADS_LOW_PRIO, "Num_alloc_bcb_wait_threads_low_priority"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_FLUSHED_BCBS_WAIT_FOR_ASSIGN, "Num_flushed_bcbs_wait_for_direct_victim"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_BIG_PRV_NUM, "Num_lfcq_big_private_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_PRV_NUM, "Num_lfcq_private_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_SHR_NUM, "Num_lfcq_shared_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_AVOID_DEALLOC_CNT, "Num_data_page_avoid_dealloc"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_AVOID_VICTIM_CNT, "Num_data_page_avoid_victim"),

  /* Array type statistics */
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_FIX_COUNTERS, "Num_data_page_fix_ext", &f_dump_in_file_Num_data_page_fix_ext,
			       &f_dump_in_buffer_Num_data_page_fix_ext, &f_load_Num_data_page_fix_ext),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_PROMOTE_COUNTERS, "Num_data_page_promote_ext",
			       &f_dump_in_file_Num_data_page_promote_ext, &f_dump_in_buffer_Num_data_page_promote_ext,
			       &f_load_Num_data_page_promote_ext),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_PROMOTE_TIME_COUNTERS, "Num_data_page_promote_time_ext",
			       &f_dump_in_file_Num_data_page_promote_time_ext,
			       &f_dump_in_buffer_Num_data_page_promote_time_ext,
			       &f_load_Num_data_page_promote_time_ext),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_UNFIX_COUNTERS, "Num_data_page_unfix_ext",
			       &f_dump_in_file_Num_data_page_unfix_ext, &f_dump_in_buffer_Num_data_page_unfix_ext,
			       &f_load_Num_data_page_unfix_ext),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_LOCK_TIME_COUNTERS, "Time_data_page_lock_acquire_time",
			       &f_dump_in_file_Time_data_page_lock_acquire_time,
			       &f_dump_in_buffer_Time_data_page_lock_acquire_time,
			       &f_load_Time_data_page_lock_acquire_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_HOLD_TIME_COUNTERS, "Time_data_page_hold_acquire_time",
			       &f_dump_in_file_Time_data_page_hold_acquire_time,
			       &f_dump_in_buffer_Time_data_page_hold_acquire_time,
			       &f_load_Time_data_page_hold_acquire_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_FIX_TIME_COUNTERS, "Time_data_page_fix_acquire_time",
			       &f_dump_in_file_Time_data_page_fix_acquire_time,
			       &f_dump_in_buffer_Time_data_page_fix_acquire_time,
			       &f_load_Time_data_page_fix_acquire_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_MVCC_SNAPSHOT_COUNTERS, "Num_mvcc_snapshot_ext",
			       &f_dump_in_file_Num_mvcc_snapshot_ext, &f_dump_in_buffer_Num_mvcc_snapshot_ext,
			       &f_load_Num_mvcc_snapshot_ext),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_OBJ_LOCK_TIME_COUNTERS, "Time_obj_lock_acquire_time",
			       &f_dump_in_file_Time_obj_lock_acquire_time, &f_dump_in_buffer_Time_obj_lock_acquire_time,
			       &f_load_Time_obj_lock_acquire_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_THREAD_STATS, "Thread_stats_counters_timers", &f_dump_in_file_thread_stats,
			       &f_dump_in_buffer_thread_stats, &f_load_thread_stats),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_THREAD_DAEMON_STATS, "Thread_pgbuf_daemon_stats_counters_timers",
			       &f_dump_in_file_thread_daemon_stats, &f_dump_in_buffer_thread_daemon_stats,
			       &f_load_thread_daemon_stats),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_DWB_FLUSHED_BLOCK_NUM_VOLUMES, "Num_dwb_flushed_block_volumes",
			       &f_dump_in_file_Num_dwb_flushed_block_volumes,
			       &f_dump_in_buffer_Num_dwb_flushed_block_volumes,
			       &f_load_Num_dwb_flushed_block_volumes),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_LOAD_THREAD_STATS, "Thread_loaddb_stats_counters_timers",
			       &f_dump_in_file_thread_stats, &f_dump_in_buffer_thread_stats, &f_load_thread_stats)
};

STATIC_INLINE void perfmon_add_stat_at_offset (THREAD_ENTRY * thread_p, PERF_STAT_ID psid, const int offset,
					       UINT64 amount) __attribute__ ((ALWAYS_INLINE));

static void perfmon_server_calc_stats (UINT64 * stats);

STATIC_INLINE const char *perfmon_stat_module_name (const int module) __attribute__ ((ALWAYS_INLINE));
#if defined (SERVER_MODE) || defined (SA_MODE)
STATIC_INLINE int perfmon_get_module_type (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
#endif
STATIC_INLINE const char *perfmon_stat_page_type_name (const int page_type) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const char *perfmon_stat_page_mode_name (const int page_mode) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const char *perfmon_stat_holder_latch_name (const int holder_latch) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const char *perfmon_stat_cond_type_name (const int cond_type) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const char *perfmon_stat_promote_cond_name (const int cond_type) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const char *perfmon_stat_snapshot_name (const int snapshot) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const char *perfmon_stat_snapshot_record_type (const int rec_type) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE const char *perfmon_stat_lock_mode_name (const int lock_mode) __attribute__ ((ALWAYS_INLINE));
static const char *perfmon_stat_thread_stat_name (size_t index);

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
      return ER_FAILED;
    }

  tmp_stats = perfmon_Stat_info.current_global_stats;
  perfmon_Stat_info.current_global_stats = perfmon_Stat_info.old_global_stats;
  perfmon_Stat_info.old_global_stats = tmp_stats;

  /* Refresh statistics from server */
  err = perfmon_server_copy_global_stats (perfmon_Stat_info.current_global_stats);
  if (err != NO_ERROR)
    {
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
perfmon_print_global_stats (FILE * stream, bool cumulative, const char *substr)
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
 *   xperfmon_server_copy_stats_for - Copy recorded server statistics for trace
 *				on the numbers of fetches, ioreads, and iowrites
 *   return: none
 *   to_stats(out): buffer to copy
 */
void
xperfmon_server_copy_stats_for_trace (THREAD_ENTRY * thread_p, UINT64 * to_stats)
{
  UINT64 *from_stats;

  from_stats = perfmon_server_get_stats (thread_p);

  if (from_stats != NULL)
    {
      to_stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset] =
	from_stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset];
      to_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset] =
	from_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset];
      to_stats[pstat_Metadata[PSTAT_PB_NUM_IOWRITES].start_offset] =
	from_stats[pstat_Metadata[PSTAT_PB_NUM_IOWRITES].start_offset];
    }
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
      if (statistic_id == PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC)
	{
	  perfmon_server_calc_stats (stats);
	  return stats[offset] / 100;
	}
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

/*
 *   perfmon_db_flushed_block_volumes -
 *   return: none
 */
void
perfmon_db_flushed_block_volumes (THREAD_ENTRY * thread_p, int num_volumes)
{
  int offset;

  assert (num_volumes >= 0);
  if (num_volumes < PERF_DWB_FLUSHED_BLOCK_VOLUMES_CNT)
    {
      offset = num_volumes;
    }
  else
    {
      offset = PERF_DWB_FLUSHED_BLOCK_VOLUMES_CNT - 1;
    }
  perfmon_add_stat_at_offset (thread_p, PSTAT_DWB_FLUSHED_BLOCK_NUM_VOLUMES, offset, 1);
}
#endif /* SERVER_MODE || SA_MODE */

int
perfmon_calc_diff_stats_for_trace (UINT64 * stats_diff, UINT64 * new_stats, UINT64 * old_stats)
{
  int i;
  int index[3] = {
    pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset,
    pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset,
    pstat_Metadata[PSTAT_PB_NUM_IOWRITES].start_offset
  };

  for (i = 0; i < 3; i++)
    {
      if (new_stats[index[i]] >= old_stats[index[i]])
	{
	  stats_diff[index[i]] = new_stats[index[i]] - old_stats[index[i]];
	}
      else
	{
	  stats_diff[index[i]] = 0;
	}
    }

  return NO_ERROR;
}

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
		  if (prm_get_bool_value (PRM_ID_STATDUMP_FORCE_ADD_INT_MAX))
		    {
		      fprintf (stream, "%-29s = %10llu\n", pstat_Metadata[i].stat_name,
			       (unsigned long long) stats_ptr[offset] + INT_MAX);
		    }
		  else
		    {
		      fprintf (stream, "%-29s = %10llu\n", pstat_Metadata[i].stat_name,
			       (unsigned long long) stats_ptr[offset]);
		    }
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

  css_get_thread_stats (&stats[pstat_Metadata[PSTAT_THREAD_STATS].start_offset]);
  perfmon_peek_thread_daemon_stats (stats);
  // *INDENT-OFF*
  cubload::worker_manager_get_stats (&stats[pstat_Metadata[PSTAT_LOAD_THREAD_STATS].start_offset]);
  // *INDENT-ON*
#endif // SERVER_MODE

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

/*
 * perfmon_stat_module_name () -
 */
STATIC_INLINE const char *
perfmon_stat_module_name (const int module)
{
  switch (module)
    {
    case PERF_MODULE_SYSTEM:
      return "SYSTEM";
    case PERF_MODULE_USER:
      return "WORKER";
    case PERF_MODULE_VACUUM:
      return "VACUUM";
    default:
      break;
    }
  return "ERROR";
}

#if defined (SERVER_MODE) || defined (SA_MODE)
/*
 * perfmon_get_module_type () -
 */
STATIC_INLINE int
perfmon_get_module_type (THREAD_ENTRY * thread_p)
{
#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  switch (thread_p->type)
    {
    case TT_WORKER:
      return PERF_MODULE_USER;
    case TT_VACUUM_WORKER:
    case TT_VACUUM_MASTER:
      return PERF_MODULE_VACUUM;
    default:
      return PERF_MODULE_SYSTEM;
    }
#else
  return PERF_MODULE_USER;
#endif
}
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

/*
 * perf_stat_page_type_name () -
 */
STATIC_INLINE const char *
perfmon_stat_page_type_name (const int page_type)
{
  switch (page_type)
    {
    case PERF_PAGE_UNKNOWN:
      return "PAGE_UNKNOWN";
    case PERF_PAGE_FTAB:
      return "PAGE_FTAB";
    case PERF_PAGE_HEAP:
      return "PAGE_HEAP";
    case PERF_PAGE_VOLHEADER:
      return "PAGE_VOLHEADER";
    case PERF_PAGE_VOLBITMAP:
      return "PAGE_VOLBITMAP";
    case PERF_PAGE_QRESULT:
      return "PAGE_QRESULT";
    case PERF_PAGE_EHASH:
      return "PAGE_EHASH";
    case PERF_PAGE_OVERFLOW:
      return "PAGE_OVERFLOW";
    case PERF_PAGE_AREA:
      return "PAGE_AREA";
    case PERF_PAGE_CATALOG:
      return "PAGE_CATALOG";
    case PERF_PAGE_BTREE_GENERIC:
      return "PAGE_BTREE";
    case PERF_PAGE_LOG:
      return "PAGE_LOG";
    case PERF_PAGE_DROPPED_FILES:
      return "PAGE_DROPPED";
    case PERF_PAGE_VACUUM_DATA:
      return "PAGE_VACUUM_DATA";
    case PERF_PAGE_BTREE_ROOT:
      return "PAGE_BTREE_R";
    case PERF_PAGE_BTREE_OVF:
      return "PAGE_BTREE_O";
    case PERF_PAGE_BTREE_LEAF:
      return "PAGE_BTREE_L";
    case PERF_PAGE_BTREE_NONLEAF:
      return "PAGE_BTREE_N";
    default:
      break;
    }
  return "ERROR";
}

/*
 * perfmon_stat_page_mode_name () -
 */
STATIC_INLINE const char *
perfmon_stat_page_mode_name (const int page_mode)
{
  switch (page_mode)
    {
    case PERF_PAGE_MODE_OLD_LOCK_WAIT:
      return "OLD_WAIT";
    case PERF_PAGE_MODE_OLD_NO_WAIT:
      return "OLD_NO_WAIT";
    case PERF_PAGE_MODE_NEW_LOCK_WAIT:
      return "NEW_WAIT";
    case PERF_PAGE_MODE_NEW_NO_WAIT:
      return "NEW_NO_WAIT";
    case PERF_PAGE_MODE_OLD_IN_BUFFER:
      return "OLD_PAGE_IN_PB";
    default:
      break;
    }
  return "ERROR";
}

/*
 * perfmon_stat_holder_latch_name () -
 */
STATIC_INLINE const char *
perfmon_stat_holder_latch_name (const int holder_latch)
{
  switch (holder_latch)
    {
    case PERF_HOLDER_LATCH_READ:
      return "READ";
    case PERF_HOLDER_LATCH_WRITE:
      return "WRITE";
    case PERF_HOLDER_LATCH_MIXED:
      return "MIXED";
    default:
      break;
    }
  return "ERROR";
}

/*
 * perfmon_stat_cond_type_name () -
 */
STATIC_INLINE const char *
perfmon_stat_cond_type_name (const int cond_type)
{
  switch (cond_type)
    {
    case PERF_CONDITIONAL_FIX:
      return "COND";
    case PERF_UNCONDITIONAL_FIX_NO_WAIT:
      return "UNCOND";
    case PERF_UNCONDITIONAL_FIX_WITH_WAIT:
      return "UNCOND_WAIT";
    default:
      break;
    }
  return "ERROR";
}

/*
 * perfmon_stat_snapshot_name () -
 */
STATIC_INLINE const char *
perfmon_stat_snapshot_name (const int snapshot)
{
  switch (snapshot)
    {
    case PERF_SNAPSHOT_SATISFIES_DELETE:
      return "DELETE";
    case PERF_SNAPSHOT_SATISFIES_DIRTY:
      return "DIRTY";
    case PERF_SNAPSHOT_SATISFIES_SNAPSHOT:
      return "SNAPSHOT";
    case PERF_SNAPSHOT_SATISFIES_VACUUM:
      return "VACUUM";
    default:
      break;
    }
  return "ERROR";
}

/*
 * perfmon_stat_snapshot_record_type () -
 */
STATIC_INLINE const char *
perfmon_stat_snapshot_record_type (const int rec_type)
{
  switch (rec_type)
    {
    case PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED:
      return "INS_VACUUMED";
    case PERF_SNAPSHOT_RECORD_INSERTED_CURR_TRAN:
      return "INS_CURR";
    case PERF_SNAPSHOT_RECORD_INSERTED_OTHER_TRAN:
      return "INS_OTHER";
    case PERF_SNAPSHOT_RECORD_INSERTED_COMMITED:
      return "INS_COMMITTED";
    case PERF_SNAPSHOT_RECORD_INSERTED_COMMITED_LOST:
      return "INS_COMMITTED_L";
    case PERF_SNAPSHOT_RECORD_INSERTED_DELETED:
      return "INS_DELETED";
    case PERF_SNAPSHOT_RECORD_DELETED_CURR_TRAN:
      return "DELETED_CURR";
    case PERF_SNAPSHOT_RECORD_DELETED_OTHER_TRAN:
      return "DELETED_OTHER";
    case PERF_SNAPSHOT_RECORD_DELETED_COMMITTED:
      return "DELETED_COMMITED";
    case PERF_SNAPSHOT_RECORD_DELETED_COMMITTED_LOST:
      return "DELETED_COMMITED_L";
    default:
      break;
    }
  return "ERROR";
}

STATIC_INLINE const char *
perfmon_stat_lock_mode_name (const int lock_mode)
{
  switch (lock_mode)
    {
    case NA_LOCK:
      return "NA_LOCK";
    case INCON_NON_TWO_PHASE_LOCK:
      return "INCON_2PL";
    case NULL_LOCK:
      return "NULL_LOCK";
    case SCH_S_LOCK:
      return "SCH_S_LOCK";
    case IS_LOCK:
      return "IS_LOCK";
    case S_LOCK:
      return "S_LOCK";
    case IX_LOCK:
      return "IX_LOCK";
    case SIX_LOCK:
      return "SIX_LOCK";
    case U_LOCK:
      return "U_LOCK";
    case X_LOCK:
      return "X_LOCK";
    case SCH_M_LOCK:
      return "SCH_M_LOCK";
    default:
      break;
    }
  return "ERROR";
}

/*
 * perfmon_stat_cond_type_name () -
 */
STATIC_INLINE const char *
perfmon_stat_promote_cond_name (const int cond_type)
{
  switch (cond_type)
    {
    case PERF_PROMOTE_ONLY_READER:
      return "ONLY_READER";
    case PERF_PROMOTE_SHARED_READER:
      return "SHARED_READER";
    default:
      break;
    }
  return "ERROR";
}

/*
 * perfmon_stat_dump_in_buffer_fix_page_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_fix_page_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int cond_type;
  int offset;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
      for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
	{
	  for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	    {
	      for (page_mode = PERF_PAGE_MODE_OLD_LOCK_WAIT; page_mode < PERF_PAGE_MODE_CNT; page_mode++)
		{
		  for (latch_mode = PERF_HOLDER_LATCH_READ; latch_mode < PERF_HOLDER_LATCH_CNT; latch_mode++)
		    {
		      for (cond_type = PERF_CONDITIONAL_FIX; cond_type < PERF_CONDITIONAL_FIX_CNT; cond_type++)
			{
			  offset = PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_mode, latch_mode, cond_type);

			  assert (offset < PERF_PAGE_FIX_COUNTERS);

			  counter = stats_ptr[offset];
			  if (counter == 0)
			    {
			      continue;
			    }

			  ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-18s,%-5s,%-11s = %10llu\n",
					  perfmon_stat_module_name (module), perfmon_stat_page_type_name (page_type),
					  perfmon_stat_page_mode_name (page_mode),
					  perfmon_stat_holder_latch_name (latch_mode),
					  perfmon_stat_cond_type_name (cond_type), (long long unsigned int) counter);
			  *remaining_size -= ret;
			  *s += ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_fix_page_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_fix_page_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int cond_type;
  int offset;
  UINT64 counter = 0;

  assert (stream != NULL);
  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	{
	  for (page_mode = PERF_PAGE_MODE_OLD_LOCK_WAIT; page_mode < PERF_PAGE_MODE_CNT; page_mode++)
	    {
	      for (latch_mode = PERF_HOLDER_LATCH_READ; latch_mode < PERF_HOLDER_LATCH_CNT; latch_mode++)
		{
		  for (cond_type = PERF_CONDITIONAL_FIX; cond_type < PERF_CONDITIONAL_FIX_CNT; cond_type++)
		    {
		      offset = PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_mode, latch_mode, cond_type);

		      assert (offset < PERF_PAGE_FIX_COUNTERS);
		      counter = stats_ptr[offset];
		      if (counter == 0)
			{
			  continue;
			}

		      fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s = %10llu\n", perfmon_stat_module_name (module),
			       perfmon_stat_page_type_name (page_type), perfmon_stat_page_mode_name (page_mode),
			       perfmon_stat_holder_latch_name (latch_mode), perfmon_stat_cond_type_name (cond_type),
			       (long long unsigned int) counter);
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_buffer_promote_page_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_promote_page_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  int module;
  int page_type;
  int promote_cond;
  int holder_latch;
  int success;
  int offset;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
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
			  offset =
			    PERF_PAGE_PROMOTE_STAT_OFFSET (module, page_type, promote_cond, holder_latch, success);

			  assert (offset < PERF_PAGE_PROMOTE_COUNTERS);

			  counter = stats_ptr[offset];
			  if (counter == 0)
			    {
			      continue;
			    }

			  ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-13s,%-5s,%-7s = %10llu\n",
					  perfmon_stat_module_name (module), perfmon_stat_page_type_name (page_type),
					  perfmon_stat_promote_cond_name (promote_cond),
					  perfmon_stat_holder_latch_name (holder_latch),
					  (success ? "SUCCESS" : "FAILED"), (long long unsigned int) counter);
			  *remaining_size -= ret;
			  *s += ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_promote_page_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_promote_page_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  int module;
  int page_type;
  int promote_cond;
  int holder_latch;
  int success;
  int offset;
  UINT64 counter = 0;

  assert (stream != NULL);
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
		      counter = stats_ptr[offset];
		      if (counter == 0)
			{
			  continue;
			}

		      fprintf (stream, "%-6s,%-14s,%-13s,%-5s,%-7s = %10llu\n", perfmon_stat_module_name (module),
			       perfmon_stat_page_type_name (page_type), perfmon_stat_promote_cond_name (promote_cond),
			       perfmon_stat_holder_latch_name (holder_latch), (success ? "SUCCESS" : "FAILED"),
			       (long long unsigned int) counter);
		    }
		}
	    }
	}
    }
}


/*
 * perfmon_stat_dump_in_buffer_unfix_page_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_unfix_page_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  int module;
  int page_type;
  int buf_dirty;
  int holder_dirty;
  int holder_latch;
  int offset;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
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
			  offset =
			    PERF_PAGE_UNFIX_STAT_OFFSET (module, page_type, buf_dirty, holder_dirty, holder_latch);

			  assert (offset < PERF_PAGE_UNFIX_COUNTERS);
			  counter = stats_ptr[offset];
			  if (counter == 0)
			    {
			      continue;
			    }

			  ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-13s,%-16s,%-5s = %10llu\n",
					  perfmon_stat_module_name (module), perfmon_stat_page_type_name (page_type),
					  buf_dirty ? "BUF_DIRTY" : "BUF_NON_DIRTY",
					  holder_dirty ? "HOLDER_DIRTY" : "HOLDER_NON_DIRTY",
					  perfmon_stat_holder_latch_name (holder_latch),
					  (long long unsigned int) counter);
			  *remaining_size -= ret;
			  *s += ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_unfix_page_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_unfix_page_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  int module;
  int page_type;
  int buf_dirty;
  int holder_dirty;
  int holder_latch;
  int offset;
  UINT64 counter = 0;

  assert (stream != NULL);
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
		      counter = stats_ptr[offset];
		      if (counter == 0)
			{
			  continue;
			}

		      fprintf (stream, "%-6s,%-14s,%-13s,%-16s,%-5s = %10llu\n", perfmon_stat_module_name (module),
			       perfmon_stat_page_type_name (page_type), buf_dirty ? "BUF_DIRTY" : "BUF_NON_DIRTY",
			       holder_dirty ? "HOLDER_DIRTY" : "HOLDER_NON_DIRTY",
			       perfmon_stat_holder_latch_name (holder_latch), (long long unsigned int) counter);
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_buffer_page_lock_time_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_page_lock_time_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int cond_type;
  int offset;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
      for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
	{
	  for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	    {
	      for (page_mode = PERF_PAGE_MODE_OLD_LOCK_WAIT; page_mode < PERF_PAGE_MODE_CNT; page_mode++)
		{
		  for (latch_mode = PERF_HOLDER_LATCH_READ; latch_mode < PERF_HOLDER_LATCH_CNT; latch_mode++)
		    {
		      for (cond_type = PERF_CONDITIONAL_FIX; cond_type < PERF_CONDITIONAL_FIX_CNT; cond_type++)
			{
			  offset = PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_mode, latch_mode, cond_type);

			  assert (offset < PERF_PAGE_FIX_COUNTERS);
			  counter = stats_ptr[offset];
			  if (counter == 0)
			    {
			      continue;
			    }

			  ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-18s,%-5s,%-11s = %16llu\n",
					  perfmon_stat_module_name (module), perfmon_stat_page_type_name (page_type),
					  perfmon_stat_page_mode_name (page_mode),
					  perfmon_stat_holder_latch_name (latch_mode),
					  perfmon_stat_cond_type_name (cond_type), (long long unsigned int) counter);
			  *remaining_size -= ret;
			  *s += ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_page_lock_time_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_page_lock_time_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int cond_type;
  int offset;
  UINT64 counter = 0;

  assert (stream != NULL);
  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	{
	  for (page_mode = PERF_PAGE_MODE_OLD_LOCK_WAIT; page_mode < PERF_PAGE_MODE_CNT; page_mode++)
	    {
	      for (latch_mode = PERF_HOLDER_LATCH_READ; latch_mode < PERF_HOLDER_LATCH_CNT; latch_mode++)
		{
		  for (cond_type = PERF_CONDITIONAL_FIX; cond_type < PERF_CONDITIONAL_FIX_CNT; cond_type++)
		    {
		      offset = PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_mode, latch_mode, cond_type);

		      assert (offset < PERF_PAGE_FIX_COUNTERS);
		      counter = stats_ptr[offset];
		      if (counter == 0)
			{
			  continue;
			}

		      fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s = %16llu\n", perfmon_stat_module_name (module),
			       perfmon_stat_page_type_name (page_type), perfmon_stat_page_mode_name (page_mode),
			       perfmon_stat_holder_latch_name (latch_mode), perfmon_stat_cond_type_name (cond_type),
			       (long long unsigned int) counter);
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_buffer_page_hold_time_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_page_hold_time_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int offset;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
      for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
	{
	  for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	    {
	      for (page_mode = PERF_PAGE_MODE_OLD_LOCK_WAIT; page_mode < PERF_PAGE_MODE_CNT; page_mode++)
		{
		  for (latch_mode = PERF_HOLDER_LATCH_READ; latch_mode < PERF_HOLDER_LATCH_CNT; latch_mode++)
		    {
		      offset = PERF_PAGE_HOLD_TIME_OFFSET (module, page_type, page_mode, latch_mode);

		      assert (offset < PERF_PAGE_HOLD_TIME_COUNTERS);
		      counter = stats_ptr[offset];
		      if (counter == 0)
			{
			  continue;
			}

		      ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-18s,%-5s = %16llu\n",
				      perfmon_stat_module_name (module), perfmon_stat_page_type_name (page_type),
				      perfmon_stat_page_mode_name (page_mode),
				      perfmon_stat_holder_latch_name (latch_mode), (long long unsigned int) counter);
		      *remaining_size -= ret;
		      *s += ret;
		      if (*remaining_size <= 0)
			{
			  return;
			}
		    }
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_page_hold_time_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_page_hold_time_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int offset;
  UINT64 counter = 0;

  assert (stream != NULL);
  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      for (page_type = PERF_PAGE_UNKNOWN; page_type < PERF_PAGE_CNT; page_type++)
	{
	  for (page_mode = PERF_PAGE_MODE_OLD_LOCK_WAIT; page_mode < PERF_PAGE_MODE_CNT; page_mode++)
	    {
	      for (latch_mode = PERF_HOLDER_LATCH_READ; latch_mode < PERF_HOLDER_LATCH_CNT; latch_mode++)
		{
		  offset = PERF_PAGE_HOLD_TIME_OFFSET (module, page_type, page_mode, latch_mode);

		  assert (offset < PERF_PAGE_HOLD_TIME_COUNTERS);


		  counter = stats_ptr[offset];
		  if (counter == 0)
		    {
		      continue;
		    }

		  fprintf (stream, "%-6s,%-14s,%-18s,%-5s = %16llu\n", perfmon_stat_module_name (module),
			   perfmon_stat_page_type_name (page_type), perfmon_stat_page_mode_name (page_mode),
			   perfmon_stat_holder_latch_name (latch_mode), (long long unsigned int) counter);
		}
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_buffer_page_fix_time_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_page_fix_time_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  /* the counters partitioning match with page fix statistics */
  perfmon_stat_dump_in_buffer_page_lock_time_array_stat (stats_ptr, s, remaining_size);
}

/*
 * perfmon_stat_dump_in_file_page_fix_time_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_page_fix_time_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  /* the counters partitioning match with page fix statistics */
  perfmon_stat_dump_in_file_page_lock_time_array_stat (stream, stats_ptr);
}

/*
 * perfmon_stat_dump_in_buffer_mvcc_snapshot_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_mvcc_snapshot_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  unsigned int snapshot;
  unsigned int rec_type;
  unsigned int visibility;
  int offset;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
      for (snapshot = (unsigned int) PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < (unsigned int) PERF_SNAPSHOT_CNT;
	   snapshot++)
	{
	  for (rec_type = (unsigned int) PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED;
	       rec_type < (unsigned int) PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
	    {
	      for (visibility = (unsigned int) PERF_SNAPSHOT_INVISIBLE;
		   visibility < (unsigned int) PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
		{
		  offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);

		  assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);
		  counter = stats_ptr[offset];
		  if (counter == 0)
		    {
		      continue;
		    }

		  ret =
		    snprintf (*s, *remaining_size, "%-8s,%-18s,%-9s = %16llu\n", perfmon_stat_snapshot_name (snapshot),
			      perfmon_stat_snapshot_record_type (rec_type),
			      (visibility == PERF_SNAPSHOT_INVISIBLE) ? "INVISIBLE" : "VISIBLE",
			      (long long unsigned int) counter);
		  *remaining_size -= ret;
		  *s += ret;
		  if (*remaining_size <= 0)
		    {
		      return;
		    }
		}
	    }
	}
    }
}

/*
 * perf_stat_dump_in_file_mvcc_snapshot_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_mvcc_snapshot_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  unsigned int snapshot;
  unsigned int rec_type;
  unsigned int visibility;
  int offset;
  UINT64 counter = 0;

  assert (stream != NULL);
  for (snapshot = (unsigned int) PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < (unsigned int) PERF_SNAPSHOT_CNT;
       snapshot++)
    {
      for (rec_type = (unsigned int) PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED;
	   rec_type < (unsigned int) PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
	{
	  for (visibility = (unsigned int) PERF_SNAPSHOT_INVISIBLE;
	       visibility < (unsigned int) PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
	    {
	      offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);

	      assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);

	      counter = stats_ptr[offset];
	      if (counter == 0)
		{
		  continue;
		}

	      fprintf (stream, "%-8s,%-18s,%-9s = %16llu\n", perfmon_stat_snapshot_name (snapshot),
		       perfmon_stat_snapshot_record_type (rec_type),
		       (visibility == PERF_SNAPSHOT_INVISIBLE) ? "INVISIBLE" : "VISIBLE",
		       (long long unsigned int) counter);
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_buffer_obj_lock_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_obj_lock_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  unsigned int lock_mode;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
      for (lock_mode = (unsigned int) NA_LOCK; lock_mode <= (unsigned int) SCH_M_LOCK; lock_mode++)
	{
	  counter = stats_ptr[lock_mode];
	  if (counter == 0)
	    {
	      continue;
	    }

	  ret = snprintf (*s, *remaining_size, "%-10s = %16llu\n", perfmon_stat_lock_mode_name (lock_mode),
			  (long long unsigned int) counter);
	  *remaining_size -= ret;
	  *s += ret;
	  if (*remaining_size <= 0)
	    {
	      return;
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_obj_lock_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_obj_lock_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  int lock_mode;
  UINT64 counter = 0;

  assert (stream != NULL);

  for (lock_mode = NA_LOCK; lock_mode <= SCH_M_LOCK; lock_mode++)
    {
      counter = stats_ptr[lock_mode];
      if (counter == 0)
	{
	  continue;
	}

      fprintf (stream, "%-10s = %16llu\n", perfmon_stat_lock_mode_name (lock_mode), (long long unsigned int) counter);
    }
}

/*
 * perfmon_stat_dump_in_buffer_obj_lock_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_flushed_block_volumes_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  unsigned int flushed_block_volumes;
  UINT64 counter = 0;
  int ret;
  char buffer[15];

  assert (remaining_size != NULL);
  assert (s != NULL);

  if (*s != NULL)
    {
      for (flushed_block_volumes = (unsigned int) 0;
	   flushed_block_volumes < (unsigned int) PERF_DWB_FLUSHED_BLOCK_VOLUMES_CNT; flushed_block_volumes++)
	{
	  counter = stats_ptr[flushed_block_volumes];
	  if (counter == 0)
	    {
	      continue;
	    }

	  sprintf (buffer, "%d Volumes", flushed_block_volumes);
	  ret = snprintf (*s, *remaining_size, "%-15s = %16llu\n", buffer, (long long unsigned int) counter);
	  *remaining_size -= ret;
	  *s += ret;
	  if (*remaining_size <= 0)
	    {
	      return;
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_flushed_block_volumes_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_flushed_block_volumes_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  unsigned int flushed_block_volumes;
  UINT64 counter = 0;
  char buffer[15];

  assert (stream != NULL);

  for (flushed_block_volumes = (unsigned int) 0;
       flushed_block_volumes < (unsigned int) PERF_DWB_FLUSHED_BLOCK_VOLUMES_CNT; flushed_block_volumes++)
    {
      counter = stats_ptr[flushed_block_volumes];
      if (counter == 0)
	{
	  continue;
	}

      sprintf (buffer, "%d Volumes", flushed_block_volumes);
      fprintf (stream, "%-15s = %16llu\n", buffer, (long long unsigned int) counter);
    }
}

/*
 * perfmon_stat_dump_in_buffer_snapshot_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_snapshot_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  int module;
  int offset;
  UINT64 counter = 0;
  int ret;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
      for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
	{
	  offset = module;

	  assert (offset < PERF_MODULE_CNT);
	  counter = stats_ptr[offset];
	  if (counter == 0)
	    {
	      continue;
	    }

	  ret = snprintf (*s, *remaining_size, "%-6s = %16llu\n", perfmon_stat_module_name (module),
			  (long long unsigned int) counter);
	  *remaining_size -= ret;
	  *s += ret;
	  if (*remaining_size <= 0)
	    {
	      return;
	    }
	}
    }
}

/*
 * perfmon_stat_dump_in_file_snapshot_array_stat () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_snapshot_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
  int module;
  int offset;
  UINT64 counter = 0;

  assert (stream != NULL);
  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      offset = module;

      assert (offset < PERF_MODULE_CNT);
      counter = stats_ptr[offset];
      if (counter == 0)
	{
	  continue;
	}

      fprintf (stream, "%-6s = %16llu\n", perfmon_stat_module_name (module), (long long unsigned int) counter);
    }
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
#if defined (SERVER_MODE) || defined (SA_MODE)
  int memsize = 0;
#endif

  pstat_Global.n_stat_values = 0;
  pstat_Global.global_stats = NULL;
  pstat_Global.n_trans = 0;
  pstat_Global.tran_stats = NULL;
  pstat_Global.is_watching = NULL;
  pstat_Global.n_watchers = 0;
  pstat_Global.initialized = false;
  pstat_Global.activation_flag = prm_get_integer_value (PRM_ID_EXTENDED_STATISTICS_ACTIVATION);

#if defined (SERVER_MODE)
  if (prm_get_bool_value (PRM_ID_STATS_ON))
    {
      // always watching
      pstat_Global.n_watchers++;
    }
#endif

  for (idx = 0; idx < PSTAT_COUNT; idx++)
    {
      assert (pstat_Metadata[idx].psid == (PERF_STAT_ID) idx);
      pstat_Metadata[idx].start_offset = pstat_Global.n_stat_values;
      switch (pstat_Metadata[idx].valtype)
	{
	case PSTAT_ACCUMULATE_SINGLE_VALUE:
	case PSTAT_PEEK_SINGLE_VALUE:
	  /* Only one value stored. */
	  pstat_Metadata[idx].n_vals = 1;
	  break;
	case PSTAT_COMPUTED_RATIO_VALUE:
	  /* Only one value stored. */
	  pstat_Metadata[idx].n_vals = 1;
	  break;
	case PSTAT_COUNTER_TIMER_VALUE:
	  /* We have:
	   * 1. counter
	   * 2. timer
	   * 3. max time
	   * 4. average time
	   */
	  pstat_Metadata[idx].n_vals = 4;
	  break;
	case PSTAT_COMPLEX_VALUE:
	  /* We should have a load function. */
	  assert (pstat_Metadata[idx].f_load != NULL);
	  pstat_Metadata[idx].n_vals = pstat_Metadata[idx].f_load ();
	  if (pstat_Metadata[idx].n_vals < 0)
	    {
	      /* Error. */
	      ASSERT_ERROR ();
	      return pstat_Metadata[idx].n_vals;
	    }
	  /* Debug check: we should have dump/compute too. */
	  assert (pstat_Metadata[idx].f_dump_in_file != NULL);
	  assert (pstat_Metadata[idx].f_dump_in_buffer != NULL);
	}
      pstat_Global.n_stat_values += pstat_Metadata[idx].n_vals;
    }

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

/*
 * f_load_Num_data_page_fix_ext () - Get the number of values for Num_data_page_fix_ext statistic
 *
 */
static int
f_load_Num_data_page_fix_ext (void)
{
  return PERF_PAGE_FIX_COUNTERS;
}

/*
 * f_load_Num_data_page_promote_ext () - Get the number of values for Num_data_page_promote_ext statistic
 *
 */
static int
f_load_Num_data_page_promote_ext (void)
{
  return PERF_PAGE_PROMOTE_COUNTERS;
}

/*
 * f_load_Num_data_page_promote_time_ext () - Get the number of values for Num_data_page_promote_time_ext statistic
 *
 */
static int
f_load_Num_data_page_promote_time_ext (void)
{
  return PERF_PAGE_PROMOTE_COUNTERS;
}

/*
 * f_load_Num_data_page_unfix_ext () - Get the number of values for Num_data_page_unfix_ext statistic
 *
 */
static int
f_load_Num_data_page_unfix_ext (void)
{
  return PERF_PAGE_UNFIX_COUNTERS;
}

/*
 * f_load_Time_data_page_lock_acquire_time () - Get the number of values for Time_data_page_lock_acquire_time statistic
 *
 */
static int
f_load_Time_data_page_lock_acquire_time (void)
{
  return PERF_PAGE_LOCK_TIME_COUNTERS;
}

/*
 * f_load_Time_data_page_hold_acquire_time () - Get the number of values for Time_data_page_hold_acquire_time statistic
 *
 */
static int
f_load_Time_data_page_hold_acquire_time (void)
{
  return PERF_PAGE_HOLD_TIME_COUNTERS;
}

/*
 * f_load_Time_data_page_fix_acquire_time () - Get the number of values for Time_data_page_fix_acquire_time statistic
 *
 */
static int
f_load_Time_data_page_fix_acquire_time (void)
{
  return PERF_PAGE_FIX_TIME_COUNTERS;
}

/*
 * f_load_Num_mvcc_snapshot_ext () - Get the number of values for Num_mvcc_snapshot_ext statistic
 *
 */
static int
f_load_Num_mvcc_snapshot_ext (void)
{
  return PERF_MVCC_SNAPSHOT_COUNTERS;
}

/*
 * f_load_Time_obj_lock_acquire_time () - Get the number of values for Time_obj_lock_acquire_time statistic
 *
 */
static int
f_load_Time_obj_lock_acquire_time (void)
{
  return PERF_OBJ_LOCK_STAT_COUNTERS;
}

/*
 * f_load_Num_dwb_flushed_block_volumes () - Get the number of values for Num_dwb_flushed_block_volumes statistic
 *
 */
static int
f_load_Num_dwb_flushed_block_volumes (void)
{
  return PERF_DWB_FLUSHED_BLOCK_VOLUMES_CNT;
}

/*
 * f_load_Time_get_snapshot_acquire_time () - Get the number of values for Time_get_snapshot_acquire_time statistic
 *
 */
static int
f_load_Time_get_snapshot_acquire_time (void)
{
  return PERF_MODULE_CNT;
}

/*
 * f_load_Count_get_snapshot_retry () - Get the number of values for Count_get_snapshot_retry statistic
 *
 */
static int
f_load_Count_get_snapshot_retry (void)
{
  return PERF_MODULE_CNT;
}

/*
 * f_load_Time_tran_complete_time () - Get the number of values for Time_tran_complete_time statistic
 *
 */
static int
f_load_Time_tran_complete_time (void)
{
  return PERF_MODULE_CNT;
}

/*
 * f_load_Time_get_oldest_mvcc_acquire_time () - Get the number of values for Time_get_oldest_mvcc_acquire_time
 *						 statistic
 *
 */
static int
f_load_Time_get_oldest_mvcc_acquire_time (void)
{
  return PERF_MODULE_CNT;
}

/*
 * f_load_Count_get_oldest_mvcc_retry () - Get the number of values for Count_get_oldest_mvcc_retry statistic
 *
 */
static int
f_load_Count_get_oldest_mvcc_retry (void)
{
  return PERF_MODULE_CNT;
}

/*
 * f_dump_in_file_Num_data_page_fix_ext () - Write in file the values for Num_data_page_fix_ext statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Num_data_page_fix_ext (FILE * f, const UINT64 * stat_vals)
{
  perfmon_stat_dump_in_file_fix_page_array_stat (f, stat_vals);
}

/*
 * f_dump_in_file_Num_data_page_promote_ext () - Write in file the values for Num_data_page_promote_ext statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Num_data_page_promote_ext (FILE * f, const UINT64 * stat_vals)
{
  perfmon_stat_dump_in_file_promote_page_array_stat (f, stat_vals);
}

/*
 * f_dump_in_file_Num_data_page_promote_time_ext () - Write in file the values for Num_data_page_promote_time_ext
 *						      statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Num_data_page_promote_time_ext (FILE * f, const UINT64 * stat_vals)
{
  perfmon_stat_dump_in_file_promote_page_array_stat (f, stat_vals);
}

/*
 * f_dump_in_file_Num_data_page_unfix_ext () - Write in file the values for Num_data_page_unfix_ext
 *					       statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Num_data_page_unfix_ext (FILE * f, const UINT64 * stat_vals)
{
  perfmon_stat_dump_in_file_unfix_page_array_stat (f, stat_vals);
}

/*
 * f_dump_in_file_Time_data_page_lock_acquire_time () - Write in file the values for Time_data_page_lock_acquire_time
 *					                statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Time_data_page_lock_acquire_time (FILE * f, const UINT64 * stat_vals)
{
  perfmon_stat_dump_in_file_page_lock_time_array_stat (f, stat_vals);
}

/*
 * f_dump_in_file_Time_data_page_hold_acquire_time () - Write in file the values for Time_data_page_hold_acquire_time
 *					                statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Time_data_page_hold_acquire_time (FILE * f, const UINT64 * stat_vals)
{
  perfmon_stat_dump_in_file_page_hold_time_array_stat (f, stat_vals);
}

/*
 * f_dump_in_file_Time_data_page_fix_acquire_time () - Write in file the values for Time_data_page_fix_acquire_time
 *					               statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Time_data_page_fix_acquire_time (FILE * f, const UINT64 * stat_vals)
{
  perfmon_stat_dump_in_file_page_fix_time_array_stat (f, stat_vals);
}

/*
 * f_dump_in_file_Num_mvcc_snapshot_ext () - Write in file the values for Num_mvcc_snapshot_ext
 *					     statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Num_mvcc_snapshot_ext (FILE * f, const UINT64 * stat_vals)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT)
    {
      perfmon_stat_dump_in_file_mvcc_snapshot_array_stat (f, stat_vals);
    }
}

/*
 * f_dump_in_file_Time_obj_lock_acquire_time () - Write in file the values for Time_obj_lock_acquire_time
 *						  statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Time_obj_lock_acquire_time (FILE * f, const UINT64 * stat_vals)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_LOCK_OBJECT)
    {
      perfmon_stat_dump_in_file_obj_lock_array_stat (f, stat_vals);
    }
}

/*
 * f_dump_in_file_Num_dwb_flushed_block_volumes () - Write in file the values for Num_dwb_flushed_block_volumes
 *						  statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_Num_dwb_flushed_block_volumes (FILE * f, const UINT64 * stat_vals)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_FLUSHED_BLOCK_VOLUMES)
    {
      perfmon_stat_dump_in_file_flushed_block_volumes_array_stat (f, stat_vals);
    }
}

/*
 * f_dump_in_buffer_Num_data_page_fix_ext () - Write to a buffer the values for Num_data_page_fix_ext
 *					       statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Num_data_page_fix_ext (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  perfmon_stat_dump_in_buffer_fix_page_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Num_data_page_promote_ext () - Write to a buffer the values for Num_data_page_promote_ext
 *						   statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Num_data_page_promote_ext (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  perfmon_stat_dump_in_buffer_promote_page_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Num_data_page_promote_time_ext () - Write to a buffer the values for Num_data_page_promote_time_ext
 *						        statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Num_data_page_promote_time_ext (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  perfmon_stat_dump_in_buffer_promote_page_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Num_data_page_unfix_ext () - Write to a buffer the values for Num_data_page_unfix_ext
 *						 statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Num_data_page_unfix_ext (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  perfmon_stat_dump_in_buffer_unfix_page_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Time_data_page_lock_acquire_time () - Write to a buffer the values for
 *							  Time_data_page_lock_acquire_time statistic
 *
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Time_data_page_lock_acquire_time (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  perfmon_stat_dump_in_buffer_page_lock_time_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Time_data_page_hold_acquire_time () - Write to a buffer the values for
 *							  Time_data_page_hold_acquire_time statistic
 *
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Time_data_page_hold_acquire_time (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  perfmon_stat_dump_in_buffer_page_hold_time_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Time_data_page_fix_acquire_time () - Write to a buffer the values for
 *							 Time_data_page_fix_acquire_time statistic
 *
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Time_data_page_fix_acquire_time (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  perfmon_stat_dump_in_buffer_page_fix_time_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Num_mvcc_snapshot_ext () - Write to a buffer the values for Num_mvcc_snapshot_ext
 *					       statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Num_mvcc_snapshot_ext (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT)
    {
      perfmon_stat_dump_in_buffer_mvcc_snapshot_array_stat (stat_vals, s, remaining_size);
    }
}

/*
 * f_dump_in_buffer_Time_obj_lock_acquire_time () - Write to a buffer the values for Time_obj_lock_acquire_time
 *						    statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Time_obj_lock_acquire_time (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_LOCK_OBJECT)
    {
      perfmon_stat_dump_in_buffer_obj_lock_array_stat (stat_vals, s, remaining_size);
    }
}

/*
 * f_dump_in_buffer_Num_dwb_flushed_block_volumes () - Write to a buffer the values for Num_dwb_flushed_block_volumes
 *					       statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_Num_dwb_flushed_block_volumes (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_FLUSHED_BLOCK_VOLUMES)
    {
      perfmon_stat_dump_in_buffer_fix_page_array_stat (stat_vals, s, remaining_size);
    }
}

/*
 * perfmon_get_number_of_statistic_values () - Get the number of entries in the statistic array
 *
 */
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
  /* fixme(rem) - will be fixed in stattool patch */
#if defined (SERVER_MODE) || defined (SA_MODE)
  stats[pstat_Metadata[PSTAT_PC_NUM_CACHE_ENTRIES].start_offset] = xcache_get_entry_count ();
  stats[pstat_Metadata[PSTAT_HF_NUM_STATS_ENTRIES].start_offset] = heap_get_best_space_num_stats_entries ();
  stats[pstat_Metadata[PSTAT_QM_NUM_HOLDABLE_CURSORS].start_offset] = session_get_number_of_holdable_cursors ();
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */
}

/*
 * perfmon_print_timer_to_file - Print in a file the statistic values for a timer type statistic
 *
 * stream (in/out): input file
 * stat_index (in): statistic index
 * stats_ptr (in) : statistic values array
 *
 * return: void
 *
 */
static void
perfmon_print_timer_to_file (FILE * stream, int stat_index, UINT64 * stats_ptr)
{
  int offset = pstat_Metadata[stat_index].start_offset;

  assert (pstat_Metadata[stat_index].valtype == PSTAT_COUNTER_TIMER_VALUE);
  fprintf (stream, "The timer values for %s are:\n", pstat_Metadata[stat_index].stat_name);
  fprintf (stream, "Num_%-25s = %10llu\n", pstat_Metadata[stat_index].stat_name,
	   (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)]);
  fprintf (stream, "Total_time_%-18s = %10llu\n", pstat_Metadata[stat_index].stat_name,
	   (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)]);
  fprintf (stream, "Max_time_%-20s = %10llu\n", pstat_Metadata[stat_index].stat_name,
	   (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)]);
  fprintf (stream, "Avg_time_%-20s = %10llu\n", pstat_Metadata[stat_index].stat_name,
	   (unsigned long long) stats_ptr[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)]);
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

// *INDENT-OFF*
//////////////////////////////////////////////////////////////////////////
// thread workers section
//////////////////////////////////////////////////////////////////////////

// note - current monitor implementation requires a single point to manage all statistics meta information (stats
//        count and names). this prevents us from isolating statistics management per each module, because some
//        modules are restricted to server only, while the monitor implementation is same for server and client.
//
//        ideally, meta information should be fetched from server and not necessarily known from start by client.
//        this would prevent any miss-matches between server and clients and better module encapsulation. performance
//        monitor would only store the values and collect from all modules rather than keeping extended information
//        on each module (like it does now with page buffer, thread and others)
//
//        for now, we are forced to duplicate here some information about thread workers and daemons that is normally
//        restricted to server
//

// NOTE - should match cubthread::Worker_pool_statdef
const char *perfmon_Portable_worker_stat_names [] =
  {
    "Counter_start_thread",
    "Timer_start_thread",
    "Counter_create_context",
    "Timer_create_context",
    "Counter_execute_task",
    "Timer_execute_task",
    "Counter_retire_task",
    "Timer_retire_task",
    "Counter_found_task_in_queue",
    "Timer_found_task_in_queue",
    "Counter_wakeup_with_task",
    "Timer_wakeup_with_task",
    "Counter_recycle_context",
    "Timer_recycle_context",
    "Counter_retire_context",
    "Timer_retire_context"
  };
static const size_t PERFMON_PORTABLE_WORKER_STAT_COUNT =
  sizeof (perfmon_Portable_worker_stat_names) / sizeof (const char *);

static size_t
thread_stats_count (void)
{
#if defined (SERVER_MODE)
  assert (PERFMON_PORTABLE_WORKER_STAT_COUNT == cubthread::wp_worker_statset_get_count ());
  static bool check_names = true;
  if (check_names)
    {
      for (size_t index = 0; index < PERFMON_PORTABLE_WORKER_STAT_COUNT; index++)
        {
          if (std::strcmp (perfmon_Portable_worker_stat_names[index], cubthread::wp_worker_statset_get_name (index)) != 0)
            {
              assert (false);
              _er_log_debug (ARG_FILE_LINE,
                             "Warning - Monitoring thread worker statistics; statistics name not matching for %zu\n"
                             "\t\tperfmon name = %s\n" "\t\tdaemon name = %s\n", index,
                             perfmon_Portable_worker_stat_names[index], cubthread::wp_worker_statset_get_name (index));
            }
        }
      check_names = false;
    }
#endif // SERVER_MODE
  return PERFMON_PORTABLE_WORKER_STAT_COUNT;
}

static int
f_load_thread_stats (void)
{
  return (int) thread_stats_count ();
}

static const char *
perfmon_stat_thread_stat_name (size_t index)
{
  return perfmon_Portable_worker_stat_names[index];
}

/*
 * f_dump_in_file_thread_stats () - Write in file the values for thread statistic
 *
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_thread_stats (FILE * f, const UINT64 * stat_vals)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_THREAD)
    {
      perfmon_stat_dump_in_file_thread_stats (f, stat_vals);
    }
}

/*
 * perfmon_stat_dump_in_file_thread_stats () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_thread_stats (FILE * stream, const UINT64 * stats_ptr)
{
  UINT64 value = 0;

  assert (stream != NULL);

  for (size_t it = 0; it < thread_stats_count (); it++)
    {
      value = stats_ptr[it];
      if (value == 0)
	{
	  continue;
	}
      fprintf (stream, "%-10s = %16llu\n", perfmon_stat_thread_stat_name (it), (long long unsigned int) value);
    }
}

/*
 * f_dump_in_buffer_thread_stats () - Write to a buffer the values for thread statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_thread_stats (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_THREAD)
    {
      perfmon_stat_dump_in_buffer_thread_stats (stat_vals, s, remaining_size);
    }
}

/*
 * perfmon_stat_dump_in_buffer_thread_stats () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_thread_stats (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  UINT64 value = 0;
  int ret;

  assert (s != NULL);
  assert (remaining_size != NULL);

  for (size_t it = 0; it < thread_stats_count (); it++)
    {
      value = stats_ptr[it];
      if (value == 0)
	{
	  continue;
	}
      ret = snprintf (*s, *remaining_size, "%-10s = %16llu\n", perfmon_stat_thread_stat_name (it),
                      (long long unsigned int) value);

      *remaining_size -= ret;
      *s += ret;
      if (*remaining_size <= 0)
	{
	  return;
	}
    }
}

//////////////////////////////////////////////////////////////////////////
// Thread daemons section
//////////////////////////////////////////////////////////////////////////

// NOTE - should match cubthread::daemon + cubthread::looper + cubthread::waiter statistics
static const char *perfmon_Portable_daemon_stat_names [] =
{
  // daemon
  "daemon_loop_count",
  "daemon_execute_time",
  "daemon_pause_time",

  // looper
  "looper_sleep_count",
  "looper_sleep_time",
  "looper_reset_count",

  // waiter
  "waiter_wakeup_count",
  "waiter_lock_wakeup_count",
  "waiter_sleep_count",
  "waiter_timeout_count",
  "waiter_no_sleep_count",
  "waiter_awake_count",
  "waiter_wakeup_delay_time",

  // todo - probably has to be moved to thread_daemon.hpp
};
static const size_t PERFMON_PORTABLE_DAEMON_STAT_COUNT =
  sizeof (perfmon_Portable_daemon_stat_names) / sizeof (const char *);

static const char *perfmon_Portable_daemon_names [] =
{
  "Page_flush_daemon_thread",
  "Page_post_flush_daemon_thread",
  "Page_flush_control_daemon_thread",
  "Page_maintenance_daemon_thread",
  "Deadlock_detect_daemon_thread",
  "Log_flush_daemon_thread",
};
static const size_t PERFMON_PORTABLE_DAEMON_COUNT = sizeof (perfmon_Portable_daemon_names) / sizeof (const char *);

static size_t
perfmon_per_daemon_stat_count (void)
{
#if defined (SERVER_MODE) && !defined (NDEBUG)
  assert (PERFMON_PORTABLE_DAEMON_STAT_COUNT == cubthread::daemon::get_stats_value_count ());

  static bool check_names = true;
  if (check_names)
    {
      for (size_t index = 0; index < PERFMON_PORTABLE_DAEMON_STAT_COUNT; index++)
        {
          if (std::strcmp (perfmon_Portable_daemon_stat_names[index], cubthread::daemon::get_stat_name (index)) != 0)
            {
              assert (false);
              _er_log_debug (ARG_FILE_LINE,
                             "Warning - Monitoring daemon statistics; statistics name not matching for %zu\n"
                             "\t\tperfmon name = %s\n" "\t\tdaemon name = %s\n", index,
                             perfmon_Portable_daemon_stat_names[index], cubthread::daemon::get_stat_name (index));
            }
        }
      check_names = false;
    }
#endif // SERVER_MODE and DEBUG
  return PERFMON_PORTABLE_DAEMON_STAT_COUNT;
}

static size_t
perfmon_thread_daemon_stats_count (void)
{
  return PERFMON_PORTABLE_DAEMON_COUNT * perfmon_per_daemon_stat_count ();
}

static const char *
perfmon_thread_daemon_name (size_t index)
{
  assert (index < PERFMON_PORTABLE_DAEMON_STAT_COUNT);
  return perfmon_Portable_daemon_stat_names[index];
}

static int
f_load_thread_daemon_stats (void)
{
  return (int) perfmon_thread_daemon_stats_count ();
}

/*
 * f_dump_in_file_thread_daemon_stats () - Write in file the values for daemon statistic
 *
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
static void
f_dump_in_file_thread_daemon_stats (FILE * f, const UINT64 * stat_vals)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_DAEMONS)
    {
      perfmon_stat_dump_in_file_thread_daemon_stats (f, stat_vals);
    }
}

/*
 * perfmon_stat_dump_in_file_thread_daemon_stats () -
 *
 * stream(in): output file
 * stats_ptr(in): start of array values
 *
 */
static void
perfmon_stat_dump_in_file_thread_daemon_stats (FILE * stream, const UINT64 * stats_ptr)
{
  UINT64 value = 0;

  assert (stream != NULL);

  for (size_t daemon_it = 0; daemon_it < PERFMON_PORTABLE_DAEMON_COUNT; daemon_it++)
    {
      for (size_t stat_it = 0; stat_it < perfmon_per_daemon_stat_count (); stat_it++)
	{
	  value = stats_ptr[daemon_it * perfmon_per_daemon_stat_count () + stat_it];
          fprintf (stream, "%s.%s = %16llu\n", perfmon_Portable_daemon_names[daemon_it],
                   perfmon_thread_daemon_name (stat_it), (long long unsigned int) value);
	}
    }
}

/*
 * f_dump_in_buffer_thread_daemon_stats () - Write to a buffer the values for daemon statistics
 *
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
static void
f_dump_in_buffer_thread_daemon_stats (char **s, const UINT64 * stat_vals, int *remaining_size)
{
  if (pstat_Global.activation_flag & PERFMON_ACTIVATION_FLAG_DAEMONS)
    {
      perfmon_stat_dump_in_buffer_thread_daemon_stats (stat_vals, s, remaining_size);
    }
}

/*
 * perfmon_stat_dump_in_buffer_thread_daemon_stats () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
static void
perfmon_stat_dump_in_buffer_thread_daemon_stats (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  UINT64 value = 0;
  int ret;

  assert (s != NULL);
  assert (remaining_size != NULL);

  for (size_t daemon_it = 0; daemon_it < PERFMON_PORTABLE_DAEMON_COUNT; daemon_it++)
    {
      for (size_t stat_it = 0; stat_it < perfmon_per_daemon_stat_count (); stat_it++)
	{
	  value = stats_ptr[daemon_it * perfmon_per_daemon_stat_count () + stat_it];
          ret = snprintf (*s, *remaining_size, "%s.%s = %16llu\n", perfmon_Portable_daemon_names[daemon_it],
                          perfmon_thread_daemon_name (stat_it), (long long unsigned int) value);

	  *remaining_size -= ret;
	  *s += ret;
	  if (*remaining_size <= 0)
	    {
	      return;
	    }
	}
    }
}

#if defined (SERVER_MODE)
static void
perfmon_peek_thread_daemon_stats (UINT64 * stats)
{
  UINT64 *statsp = &stats[pstat_Metadata[PSTAT_THREAD_DAEMON_STATS].start_offset];
  pgbuf_daemons_get_stats (statsp);	// 4 x daemons
  statsp += 4 * perfmon_per_daemon_stat_count ();

  // get deadlock stats
  lock_deadlock_detect_daemon_get_stats (statsp);
  statsp += perfmon_per_daemon_stat_count ();

  // get log flush stats
  log_flush_daemon_get_stats (statsp);
  statsp += perfmon_per_daemon_stat_count ();
}
#endif // SERVER_MODE

#if defined (SERVER_MODE) || defined (SA_MODE)
void
perfmon_er_log_current_stats (THREAD_ENTRY * thread_p)
{
  UINT64 *stats = perfmon_allocate_values ();
  if (stats == NULL)
    {
      assert (false);
      return;
    }

  xperfmon_server_copy_global_stats (stats);

  const size_t STATS_DUMP_MAX_SIZE = ONE_M;     // a mega-byte
  char *strbuf = new char[STATS_DUMP_MAX_SIZE];
  strbuf[0] = '\0';

  perfmon_server_dump_stats_to_buffer (stats, strbuf, STATS_DUMP_MAX_SIZE, NULL);

  _er_log_debug (ARG_FILE_LINE, "%s\n", strbuf);

  delete strbuf;
}
#endif // SERVER_MODE || SA_MODE
// *INDENT-ON*
