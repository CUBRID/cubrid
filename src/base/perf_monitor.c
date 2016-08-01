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

#if !defined(CS_MODE)
#include <string.h>

#include "error_manager.h"
#include "log_manager.h"
#include "system_parameter.h"
#include "xserver_interface.h"

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

#define CALC_GLOBAL_STAT_DIFF(DIFF, NEW, OLD, MEMBER)       \
  do {                                                      \
    if ((NEW)->MEMBER >= (OLD)->MEMBER)                     \
      {                                                     \
        (DIFF)->MEMBER = (NEW)->MEMBER - (OLD)->MEMBER;     \
      }                                                     \
    else                                                    \
     {                                                      \
       (DIFF)->MEMBER = 0;                                  \
     }                                                      \
  } while (0)

#define CALC_STAT_DIFF(DIFF, NEW, OLD, MEMBER)              \
  do {                                                      \
    if ((NEW)->MEMBER >= (OLD)->MEMBER)                     \
      {                                                     \
        (DIFF)->MEMBER = (NEW)->MEMBER - (OLD)->MEMBER;     \
      }                                                     \
    else                                                    \
     {                                                      \
       (DIFF)->MEMBER = (NEW)->MEMBER;                      \
       (OLD)->MEMBER = 0;                                   \
     }                                                      \
  } while (0)

#define CALC_GLOBAL_STAT_DIFF_ARRAY(DIFF, NEW, OLD, MEMBER, CNT)       \
  do {								       \
    int i;							       \
    for (i = 0; i < (CNT); i++)					       \
      {								       \
	if ((NEW)->MEMBER[i] >= (OLD)->MEMBER[i])                      \
	  {							       \
	    (DIFF)->MEMBER[i] = (NEW)->MEMBER[i] - (OLD)->MEMBER[i];   \
	  }							       \
	else							       \
	 {							       \
	   (DIFF)->MEMBER[i] = 0;				       \
	 }							       \
      }								       \
  } while (0)

#define CALC_STAT_DIFF_ARRAY(DIFF, NEW, OLD, MEMBER, CNT)	       \
  do {								       \
    int i;							       \
    for (i = 0; i < (CNT); i++)					       \
      {								       \
	if ((NEW)->MEMBER[i] >= (OLD)->MEMBER[i])                      \
	  {							       \
	    (DIFF)->MEMBER[i] = (NEW)->MEMBER[i] - (OLD)->MEMBER[i];   \
	  }							       \
	else							       \
	 {							       \
	  (DIFF)->MEMBER[i] = (NEW)->MEMBER[i];                        \
	  (OLD)->MEMBER[i] = 0;					       \
	 }							       \
      }								       \
  } while (0)

#define PUT_STAT(RES, NEW, MEMBER)     ((RES)->MEMBER = (NEW)->MEMBER)

#define MNT_CALC_STATS(RES, NEW, OLD, DIFF_METHOD)                      \
  do {                                                                  \
    DIFF_METHOD (RES, NEW, OLD, file_num_creates);                      \
    DIFF_METHOD (RES, NEW, OLD, file_num_removes);                      \
    DIFF_METHOD (RES, NEW, OLD, file_num_ioreads);                      \
    DIFF_METHOD (RES, NEW, OLD, file_num_iowrites);                     \
    DIFF_METHOD (RES, NEW, OLD, file_num_iosynches);                    \
    DIFF_METHOD (RES, NEW, OLD, file_num_page_allocs);                  \
    DIFF_METHOD (RES, NEW, OLD, file_num_page_deallocs);                \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, pb_num_fetches);                        \
    DIFF_METHOD (RES, NEW, OLD, pb_num_dirties);                        \
    DIFF_METHOD (RES, NEW, OLD, pb_num_ioreads);                        \
    DIFF_METHOD (RES, NEW, OLD, pb_num_iowrites);                       \
    DIFF_METHOD (RES, NEW, OLD, pb_num_victims);                        \
    DIFF_METHOD (RES, NEW, OLD, pb_num_replacements);                   \
    DIFF_METHOD (RES, NEW, OLD, pb_num_hash_anchor_waits);              \
    DIFF_METHOD (RES, NEW, OLD, pb_time_hash_anchor_wait);              \
    /* Do not need to diff following non-accumulative stats */          \
    PUT_STAT (RES, NEW, pb_fixed_cnt);					\
    PUT_STAT (RES, NEW, pb_dirty_cnt);					\
    PUT_STAT (RES, NEW, pb_lru1_cnt);					\
    PUT_STAT (RES, NEW, pb_lru2_cnt);					\
    PUT_STAT (RES, NEW, pb_ain_cnt);					\
    PUT_STAT (RES, NEW, pb_avoid_dealloc_cnt);				\
    PUT_STAT (RES, NEW, pb_avoid_victim_cnt);				\
    PUT_STAT (RES, NEW, pb_victim_cand_cnt);				\
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, log_num_fetches);                       \
    DIFF_METHOD (RES, NEW, OLD, log_num_fetch_ioreads);                 \
    DIFF_METHOD (RES, NEW, OLD, log_num_ioreads);                       \
    DIFF_METHOD (RES, NEW, OLD, log_num_iowrites);                      \
    DIFF_METHOD (RES, NEW, OLD, log_num_appendrecs);                    \
    DIFF_METHOD (RES, NEW, OLD, log_num_archives);                      \
    DIFF_METHOD (RES, NEW, OLD, log_num_start_checkpoints);             \
    DIFF_METHOD (RES, NEW, OLD, log_num_end_checkpoints);               \
    DIFF_METHOD (RES, NEW, OLD, log_num_wals);                          \
    DIFF_METHOD (RES, NEW, OLD, log_num_replacements);                  \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, lk_num_acquired_on_pages);              \
    DIFF_METHOD (RES, NEW, OLD, lk_num_acquired_on_objects);            \
    DIFF_METHOD (RES, NEW, OLD, lk_num_converted_on_pages);             \
    DIFF_METHOD (RES, NEW, OLD, lk_num_converted_on_objects);           \
    DIFF_METHOD (RES, NEW, OLD, lk_num_re_requested_on_pages);          \
    DIFF_METHOD (RES, NEW, OLD, lk_num_re_requested_on_objects);        \
    DIFF_METHOD (RES, NEW, OLD, lk_num_waited_on_pages);                \
    DIFF_METHOD (RES, NEW, OLD, lk_num_waited_on_objects);              \
    DIFF_METHOD (RES, NEW, OLD, lk_num_waited_time_on_objects);         \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, tran_num_commits);                      \
    DIFF_METHOD (RES, NEW, OLD, tran_num_rollbacks);                    \
    DIFF_METHOD (RES, NEW, OLD, tran_num_savepoints);                   \
    DIFF_METHOD (RES, NEW, OLD, tran_num_start_topops);                 \
    DIFF_METHOD (RES, NEW, OLD, tran_num_end_topops);                   \
    DIFF_METHOD (RES, NEW, OLD, tran_num_interrupts);                   \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, bt_num_inserts);                        \
    DIFF_METHOD (RES, NEW, OLD, bt_num_deletes);                        \
    DIFF_METHOD (RES, NEW, OLD, bt_num_updates);                        \
    DIFF_METHOD (RES, NEW, OLD, bt_num_covered);                        \
    DIFF_METHOD (RES, NEW, OLD, bt_num_noncovered);                     \
    DIFF_METHOD (RES, NEW, OLD, bt_num_resumes);                        \
    DIFF_METHOD (RES, NEW, OLD, bt_num_multi_range_opt);                \
    DIFF_METHOD (RES, NEW, OLD, bt_num_splits);                         \
    DIFF_METHOD (RES, NEW, OLD, bt_num_merges);                         \
    DIFF_METHOD (RES, NEW, OLD, bt_num_get_stats);                      \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, heap_num_stats_sync_bestspace);		\
	  								\
    DIFF_METHOD (RES, NEW, OLD, qm_num_selects);                        \
    DIFF_METHOD (RES, NEW, OLD, qm_num_inserts);                        \
    DIFF_METHOD (RES, NEW, OLD, qm_num_deletes);                        \
    DIFF_METHOD (RES, NEW, OLD, qm_num_updates);                        \
    DIFF_METHOD (RES, NEW, OLD, qm_num_sscans);                         \
    DIFF_METHOD (RES, NEW, OLD, qm_num_iscans);                         \
    DIFF_METHOD (RES, NEW, OLD, qm_num_lscans);                         \
    DIFF_METHOD (RES, NEW, OLD, qm_num_setscans);                       \
    DIFF_METHOD (RES, NEW, OLD, qm_num_methscans);                      \
    DIFF_METHOD (RES, NEW, OLD, qm_num_nljoins);                        \
    DIFF_METHOD (RES, NEW, OLD, qm_num_mjoins);                         \
    DIFF_METHOD (RES, NEW, OLD, qm_num_objfetches);                     \
    DIFF_METHOD (RES, NEW, OLD, qm_num_holdable_cursors);               \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, sort_num_io_pages);                     \
    DIFF_METHOD (RES, NEW, OLD, sort_num_data_pages);                   \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, net_num_requests);                      \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, fc_num_pages);                          \
    DIFF_METHOD (RES, NEW, OLD, fc_num_log_pages);                      \
    DIFF_METHOD (RES, NEW, OLD, fc_tokens);                             \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, prior_lsa_list_size);                   \
    DIFF_METHOD (RES, NEW, OLD, prior_lsa_list_maxed);                  \
    DIFF_METHOD (RES, NEW, OLD, prior_lsa_list_removed);                \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, hf_num_stats_entries);                  \
    DIFF_METHOD (RES, NEW, OLD, hf_num_stats_maxed);                    \
                                                                        \
    DIFF_METHOD (RES, NEW, OLD, pc_num_add);                            \
    DIFF_METHOD (RES, NEW, OLD, pc_num_lookup);                         \
    DIFF_METHOD (RES, NEW, OLD, pc_num_hit);                            \
    DIFF_METHOD (RES, NEW, OLD, pc_num_miss);                           \
    DIFF_METHOD (RES, NEW, OLD, pc_num_full);                           \
    DIFF_METHOD (RES, NEW, OLD, pc_num_delete);                         \
    DIFF_METHOD (RES, NEW, OLD, pc_num_invalid_xasl_id);                \
                                                                        \
    /* Do not need to diff following non-accumulative stats */          \
    PUT_STAT (RES, NEW, ha_repl_delay);                                 \
    PUT_STAT (RES, NEW, pc_num_query_string_hash_entries);              \
    PUT_STAT (RES, NEW, pc_num_xasl_id_hash_entries);                   \
    PUT_STAT (RES, NEW, pc_num_class_oid_hash_entries);                 \
									\
    DIFF_METHOD (RES, NEW, OLD, vac_num_vacuumed_log_pages);            \
    DIFF_METHOD (RES, NEW, OLD, vac_num_to_vacuum_log_pages);           \
    DIFF_METHOD (RES, NEW, OLD, vac_num_prefetch_requests_log_pages);   \
    DIFF_METHOD (RES, NEW, OLD, vac_num_prefetch_hits_log_pages);       \
    									\
    DIFF_METHOD (RES, NEW, OLD, heap_home_inserts);			\
    DIFF_METHOD (RES, NEW, OLD, heap_big_inserts);			\
    DIFF_METHOD (RES, NEW, OLD, heap_assign_inserts);			\
    DIFF_METHOD (RES, NEW, OLD, heap_home_deletes);			\
    DIFF_METHOD (RES, NEW, OLD, heap_home_mvcc_deletes);		\
    DIFF_METHOD (RES, NEW, OLD, heap_home_to_rel_deletes);		\
    DIFF_METHOD (RES, NEW, OLD, heap_home_to_big_deletes);		\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_deletes);			\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_mvcc_deletes);			\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_to_home_deletes);		\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_to_big_deletes);		\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_to_rel_deletes);		\
    DIFF_METHOD (RES, NEW, OLD, heap_big_deletes);			\
    DIFF_METHOD (RES, NEW, OLD, heap_big_mvcc_deletes);			\
    DIFF_METHOD (RES, NEW, OLD, heap_new_ver_inserts);			\
    DIFF_METHOD (RES, NEW, OLD, heap_home_updates);			\
    DIFF_METHOD (RES, NEW, OLD, heap_home_to_rel_updates);		\
    DIFF_METHOD (RES, NEW, OLD, heap_home_to_big_updates);		\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_updates);			\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_to_home_updates);		\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_to_rel_updates);		\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_to_big_updates);		\
    DIFF_METHOD (RES, NEW, OLD, heap_big_updates);			\
    DIFF_METHOD (RES, NEW, OLD, heap_home_vacuums);			\
    DIFF_METHOD (RES, NEW, OLD, heap_big_vacuums);			\
    DIFF_METHOD (RES, NEW, OLD, heap_rel_vacuums);			\
    DIFF_METHOD (RES, NEW, OLD, heap_insid_vacuums);			\
    DIFF_METHOD (RES, NEW, OLD, heap_remove_vacuums);			\
    DIFF_METHOD (RES, NEW, OLD, heap_next_ver_vacuums);			\
    									\
    DIFF_METHOD (RES, NEW, OLD, heap_insert_prepare);			\
    DIFF_METHOD (RES, NEW, OLD, heap_insert_execute);			\
    DIFF_METHOD (RES, NEW, OLD, heap_insert_log);			\
    DIFF_METHOD (RES, NEW, OLD, heap_delete_prepare);			\
    DIFF_METHOD (RES, NEW, OLD, heap_delete_execute);			\
    DIFF_METHOD (RES, NEW, OLD, heap_delete_log);			\
    DIFF_METHOD (RES, NEW, OLD, heap_update_prepare);			\
    DIFF_METHOD (RES, NEW, OLD, heap_update_execute);			\
    DIFF_METHOD (RES, NEW, OLD, heap_update_log);			\
    DIFF_METHOD (RES, NEW, OLD, heap_vacuum_prepare);			\
    DIFF_METHOD (RES, NEW, OLD, heap_vacuum_execute);			\
    DIFF_METHOD (RES, NEW, OLD, heap_vacuum_log);			\
									\
    DIFF_METHOD (RES, NEW, OLD, bt_find_unique_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_range_search_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_insert_cnt);				\
    DIFF_METHOD (RES, NEW, OLD, bt_delete_cnt);				\
    DIFF_METHOD (RES, NEW, OLD, bt_mvcc_delete_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_mark_delete_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_update_sk_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_insert_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_delete_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_mvcc_delete_cnt);		\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_update_sk_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_cnt);				\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_insid_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_update_sk_cnt);		\
    DIFF_METHOD (RES, NEW, OLD, bt_fix_ovf_oids_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_unique_rlocks_cnt);			\
    DIFF_METHOD (RES, NEW, OLD, bt_unique_wlocks_cnt);			\
									\
    DIFF_METHOD (RES, NEW, OLD, bt_find_unique);			\
    DIFF_METHOD (RES, NEW, OLD, bt_range_search);			\
    DIFF_METHOD (RES, NEW, OLD, bt_insert);				\
    DIFF_METHOD (RES, NEW, OLD, bt_delete);				\
    DIFF_METHOD (RES, NEW, OLD, bt_mvcc_delete);			\
    DIFF_METHOD (RES, NEW, OLD, bt_mark_delete);			\
    DIFF_METHOD (RES, NEW, OLD, bt_update_sk);				\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_insert);			\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_delete);			\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_mvcc_delete);			\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_update_sk);			\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum);				\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_insid);			\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_update_sk);			\
									\
    DIFF_METHOD (RES, NEW, OLD, bt_traverse);				\
    DIFF_METHOD (RES, NEW, OLD, bt_find_unique_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_range_search_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_insert_traverse);			\
    DIFF_METHOD (RES, NEW, OLD, bt_delete_traverse);			\
    DIFF_METHOD (RES, NEW, OLD, bt_mvcc_delete_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_mark_delete_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_update_sk_traverse);			\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_insert_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_delete_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_mvcc_delete_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_undo_update_sk_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_traverse);			\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_insid_traverse);		\
    DIFF_METHOD (RES, NEW, OLD, bt_vacuum_update_sk_traverse);		\
									\
    DIFF_METHOD (RES, NEW, OLD, bt_fix_ovf_oids);			\
    DIFF_METHOD (RES, NEW, OLD, bt_unique_rlocks);			\
    DIFF_METHOD (RES, NEW, OLD, bt_unique_wlocks);			\
									\
    DIFF_METHOD (RES, NEW, OLD, vac_master);				\
    DIFF_METHOD (RES, NEW, OLD, vac_worker_process_log);		\
    DIFF_METHOD (RES, NEW, OLD, vac_worker_execute);			\
									\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, pbx_fix_counters,		\
			 PERF_PAGE_FIX_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, pbx_promote_counters,		\
			 PERF_PAGE_PROMOTE_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, pbx_promote_time_counters,	\
			 PERF_PAGE_PROMOTE_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, pbx_unfix_counters,		\
			 PERF_PAGE_UNFIX_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, pbx_lock_time_counters,		\
			 PERF_PAGE_LOCK_TIME_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, pbx_hold_time_counters,		\
			 PERF_PAGE_HOLD_TIME_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, pbx_fix_time_counters,		\
			 PERF_PAGE_FIX_TIME_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, mvcc_snapshot_counters,		\
			 PERF_MVCC_SNAPSHOT_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, obj_lock_time_counters,		\
			 PERF_OBJ_LOCK_STAT_COUNTERS);			\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, log_snapshot_time_counters,     \
			 PERF_MODULE_CNT);				\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, log_snapshot_retry_counters,    \
			 PERF_MODULE_CNT);				\
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, log_tran_complete_time_counters,    \
			 PERF_MODULE_CNT);				    \
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, log_oldest_mvcc_time_counters,	    \
			 PERF_MODULE_CNT);				    \
    DIFF_METHOD##_ARRAY (RES, NEW, OLD, log_oldest_mvcc_retry_counters,	    \
			 PERF_MODULE_CNT);				    \
} while (0)

static void mnt_server_reset_stats_internal (MNT_SERVER_EXEC_STATS * stats);
static void mnt_server_calc_stats (MNT_SERVER_EXEC_STATS * stats);
static void mnt_server_check_stats_threshold (int tran_index, MNT_SERVER_EXEC_STATS * stats);

static const char *perf_stat_module_name (const int module);
static INLINE int perf_get_module_type (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
static const char *perf_stat_page_type_name (const int page_type);
static const char *perf_stat_page_mode_name (const int page_mode);
static const char *perf_stat_holder_latch_name (const int holder_latch);
static const char *perf_stat_cond_type_name (const int cond_type);
static const char *perf_stat_promote_cond_name (const int cond_type);
#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
static const char *perf_stat_snapshot_name (const int snapshot);
static const char *perf_stat_snapshot_record_type (const int rec_type);
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */
#if defined(PERF_ENABLE_LOCK_OBJECT_STAT)
static const char *perf_stat_lock_mode_name (const int lock_mode);
#endif /* PERF_ENABLE_LOCK_OBJECT_STAT */


static void perf_stat_dump_fix_page_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
						bool print_zero_counters);
static void perf_stat_dump_promote_page_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size,
						    FILE * stream, bool print_zero_counters);
static void perf_stat_dump_unfix_page_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
						  bool print_zero_counters);
static void perf_stat_dump_page_lock_time_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size,
						      FILE * stream, bool print_zero_counters);
static void perf_stat_dump_page_hold_time_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size,
						      FILE * stream, bool print_zero_counters);
static void perf_stat_dump_page_fix_time_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size,
						     FILE * stream, bool print_zero_counters);
static void perf_stat_dump_snapshot_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
						bool print_zero_counters);
static INLINE MNT_SERVER_EXEC_STATS *mnt_server_get_stats (THREAD_ENTRY * thread_p) __attribute__ ((ALWAYS_INLINE));
#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
static void perf_stat_dump_mvcc_snapshot_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size,
						     FILE * stream, bool print_zero_counters);
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */
#if defined(PERF_ENABLE_LOCK_OBJECT_STAT)
static void perf_stat_dump_obj_lock_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
						bool print_zero_counters);
#endif /* PERF_ENABLE_LOCK_OBJECT_STAT */

#if defined(CS_MODE) || defined(SA_MODE)
bool mnt_Iscollecting_stats = false;

/* Client execution statistics */
static MNT_CLIENT_STAT_INFO mnt_Stat_info;
static void mnt_client_reset_stats (void);
static int mnt_calc_global_diff_stats (MNT_SERVER_EXEC_STATS * stats_diff, MNT_SERVER_EXEC_STATS * new_stats,
				       MNT_SERVER_EXEC_STATS * old_stats);

static MNT_SERVER_EXEC_STATS mnt_Server_stats[2];
static MNT_SERVER_EXEC_STATS mnt_Global_server_stats[2];

/*
 * mnt_start_stats - Start collecting client execution statistics
 *   return: NO_ERROR or ER_FAILED
 */
int
mnt_start_stats (bool for_all_trans)
{
  int err = NO_ERROR;

  if (mnt_Iscollecting_stats != true)
    {
      err = mnt_server_start_stats (for_all_trans);

      if (err != ER_FAILED)
	{
	  mnt_Iscollecting_stats = true;

	  mnt_get_current_times (&mnt_Stat_info.cpu_start_usr_time, &mnt_Stat_info.cpu_start_sys_time,
				 &mnt_Stat_info.elapsed_start_time);

	  if (for_all_trans)
	    {
	      mnt_Stat_info.old_global_stats = &mnt_Global_server_stats[0];
	      mnt_Stat_info.current_global_stats = &mnt_Global_server_stats[1];

	      mnt_get_global_stats ();
	      *(mnt_Stat_info.old_global_stats) = *(mnt_Stat_info.current_global_stats);
	    }
	  else
	    {
	      mnt_Stat_info.base_server_stats = &mnt_Server_stats[0];
	      mnt_Stat_info.current_server_stats = &mnt_Server_stats[1];

	      mnt_get_stats ();
	      *(mnt_Stat_info.base_server_stats) = *(mnt_Stat_info.current_server_stats);
	    }

	}
    }
  return err;
}

/*
 * mnt_stop_stats - Stop collecting client execution statistics
 *   return: NO_ERROR or ER_FAILED
 */
int
mnt_stop_stats (void)
{
  int err = NO_ERROR;

  if (mnt_Iscollecting_stats != false)
    {
      err = mnt_server_stop_stats ();
      mnt_Iscollecting_stats = false;
    }
  return err;
}

/*
 * mnt_reset_stats - Reset client statistics
 *   return: none
 */
void
mnt_reset_stats (void)
{
  if (mnt_Iscollecting_stats != false)
    {
      mnt_get_current_times (&mnt_Stat_info.cpu_start_usr_time, &mnt_Stat_info.cpu_start_sys_time,
			     &mnt_Stat_info.elapsed_start_time);

      mnt_get_stats ();
      *(mnt_Stat_info.base_server_stats) = *(mnt_Stat_info.current_server_stats);
    }
}

/*
 * mnt_get_stats - Get the recorded client statistics
 *   return: client statistics
 */
MNT_SERVER_EXEC_STATS *
mnt_get_stats ()
{
  if (mnt_Iscollecting_stats != true)
    {
      return NULL;
    }

  mnt_server_copy_stats (mnt_Stat_info.current_server_stats);
  return mnt_Stat_info.current_server_stats;
}

/*
 * mnt_get_global_stats - Get the recorded client statistics
 *   return: client statistics
 */
MNT_SERVER_EXEC_STATS *
mnt_get_global_stats (void)
{
  MNT_SERVER_EXEC_STATS *tmp_stats;

  if (mnt_Iscollecting_stats != true)
    {
      return NULL;
    }

  tmp_stats = mnt_Stat_info.current_global_stats;
  mnt_Stat_info.current_global_stats = mnt_Stat_info.old_global_stats;
  mnt_Stat_info.old_global_stats = tmp_stats;

  /* Refresh statistics from server */
  mnt_server_copy_global_stats (mnt_Stat_info.current_global_stats);

  return mnt_Stat_info.current_global_stats;
}

/*
 * mnt_print_stats - Print the current client statistics
 *   return:
 *   stream(in): if NULL is given, stdout is used
 */
void
mnt_print_stats (FILE * stream)
{
  MNT_SERVER_EXEC_STATS diff_result;
  time_t cpu_total_usr_time;
  time_t cpu_total_sys_time;
  time_t elapsed_total_time;

  if (mnt_Iscollecting_stats != true)
    {
      return;
    }

  if (stream == NULL)
    {
      stream = stdout;
    }

  if (mnt_get_stats () != NULL)
    {
      mnt_get_current_times (&cpu_total_usr_time, &cpu_total_sys_time, &elapsed_total_time);

      fprintf (stream, "\n *** CLIENT EXECUTION STATISTICS ***\n");

      fprintf (stream, "System CPU (sec)              = %10d\n",
	       (int) (cpu_total_sys_time - mnt_Stat_info.cpu_start_sys_time));
      fprintf (stream, "User CPU (sec)                = %10d\n",
	       (int) (cpu_total_usr_time - mnt_Stat_info.cpu_start_usr_time));
      fprintf (stream, "Elapsed (sec)                 = %10d\n",
	       (int) (elapsed_total_time - mnt_Stat_info.elapsed_start_time));

      if (mnt_calc_diff_stats (&diff_result, mnt_Stat_info.current_server_stats, mnt_Stat_info.base_server_stats) ==
	  NO_ERROR)
	{
	  mnt_server_dump_stats (&diff_result, stream, NULL);
	}
    }
}

/*
 *   mnt_get_global_diff_stats -
 *   diff_stats(out) :
 *   return: global statistics
 */
int
mnt_get_global_diff_stats (MNT_SERVER_EXEC_STATS * diff_stats)
{
  if (mnt_Iscollecting_stats != true || !diff_stats)
    {
      return ER_FAILED;
    }

  if (mnt_get_global_stats () != NULL)
    {
      return mnt_calc_global_diff_stats (diff_stats, mnt_Stat_info.current_global_stats,
					 mnt_Stat_info.old_global_stats);
    }

  return ER_FAILED;
}

/*
 * mnt_print_global_stats - Print the global statistics
 *   return:
 *   stream(in): if NULL is given, stdout is used
 */
void
mnt_print_global_stats (FILE * stream, bool cumulative, const char *substr)
{
  MNT_SERVER_EXEC_STATS diff_result;

  if (stream == NULL)
    {
      stream = stdout;
    }

  if (mnt_get_global_stats () != NULL)
    {
      if (cumulative)
	{
	  mnt_server_dump_stats (mnt_Stat_info.current_global_stats, stream, substr);
	}
      else
	{
	  if (mnt_calc_global_diff_stats
	      (&diff_result, mnt_Stat_info.current_global_stats, mnt_Stat_info.old_global_stats) == NO_ERROR)
	    {
	      mnt_server_dump_stats (&diff_result, stream, substr);
	    }
	}
    }
}

/*
 *   mnt_calc_global_diff_stats -
 *   return:
 *   stats_diff :
 *   new_stats :
 *   old_stats :
 */
static int
mnt_calc_global_diff_stats (MNT_SERVER_EXEC_STATS * stats_diff, MNT_SERVER_EXEC_STATS * new_stats,
			    MNT_SERVER_EXEC_STATS * old_stats)
{
  assert (stats_diff && new_stats && old_stats);

  if (!stats_diff || !new_stats || !old_stats)
    {
      return ER_FAILED;
    }

  MNT_CALC_STATS (stats_diff, new_stats, old_stats, CALC_GLOBAL_STAT_DIFF);

  mnt_server_calc_stats (stats_diff);

  return NO_ERROR;
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

static const char *mnt_Stats_name[MNT_SERVER_EXEC_STATS_COUNT] = {
  "Num_file_creates",
  "Num_file_removes",
  "Num_file_ioreads",
  "Num_file_iowrites",
  "Num_file_iosynches",
  "Num_file_page_allocs",
  "Num_file_page_deallocs",
  "Num_data_page_fetches",
  "Num_data_page_dirties",
  "Num_data_page_ioreads",
  "Num_data_page_iowrites",
  "Num_data_page_victims",
  "Num_data_page_iowrites_for_replacement",
  "Num_data_page_hash_anchor_waits",
  "Time_data_page_hash_anchor_wait",
  "Num_data_page_fixed",
  "Num_data_page_dirty",
  "Num_data_page_lru1",
  "Num_data_page_lru2",
  "Num_data_page_ain",
  "Num_data_page_avoid_dealloc",
  "Num_data_page_avoid_victim",
  "Num_data_page_victim_cand",
  "Num_log_page_fetches",
  "Num_log_page_fetch_ioreads",
  "Num_log_page_ioreads",
  "Num_log_page_iowrites",
  "Num_log_append_records",
  "Num_log_archives",
  "Num_log_start_checkpoints",
  "Num_log_end_checkpoints",
  "Num_log_wals",
  "Num_log_page_iowrites_for_replacement",
  "Num_page_locks_acquired",
  "Num_object_locks_acquired",
  "Num_page_locks_converted",
  "Num_object_locks_converted",
  "Num_page_locks_re-requested",
  "Num_object_locks_re-requested",
  "Num_page_locks_waits",
  "Num_object_locks_waits",
  "Num_object_locks_time_waited_usec",
  "Num_tran_commits",
  "Num_tran_rollbacks",
  "Num_tran_savepoints",
  "Num_tran_start_topops",
  "Num_tran_end_topops",
  "Num_tran_interrupts",
  "Num_btree_inserts",
  "Num_btree_deletes",
  "Num_btree_updates",
  "Num_btree_covered",
  "Num_btree_noncovered",
  "Num_btree_resumes",
  "Num_btree_multirange_optimization",
  "Num_btree_splits",
  "Num_btree_merges",
  "Num_btree_get_stats",
  "Num_heap_stats_sync_bestspace",
  "Num_query_selects",
  "Num_query_inserts",
  "Num_query_deletes",
  "Num_query_updates",
  "Num_query_sscans",
  "Num_query_iscans",
  "Num_query_lscans",
  "Num_query_setscans",
  "Num_query_methscans",
  "Num_query_nljoins",
  "Num_query_mjoins",
  "Num_query_objfetches",
  "Num_query_holdable_cursors",
  "Num_sort_io_pages",
  "Num_sort_data_pages",
  "Num_network_requests",
  "Num_adaptive_flush_pages",
  "Num_adaptive_flush_log_pages",
  "Num_adaptive_flush_max_pages",
  "Num_prior_lsa_list_size",
  "Num_prior_lsa_list_maxed",
  "Num_prior_lsa_list_removed",
  "Num_heap_stats_bestspace_entries",
  "Num_heap_stats_bestspace_maxed",
  "Time_ha_replication_delay",
  "Num_plan_cache_add",
  "Num_plan_cache_lookup",
  "Num_plan_cache_hit",
  "Num_plan_cache_miss",
  "Num_plan_cache_full",
  "Num_plan_cache_delete",
  "Num_plan_cache_invalid_xasl_id",
  "Num_plan_cache_query_string_hash_entries",
  "Num_plan_cache_xasl_id_hash_entries",
  "Num_plan_cache_class_oid_hash_entries",
  "Num_vacuum_log_pages_vacuumed",
  "Num_vacuum_log_pages_to_vacuum",
  "Num_vacuum_prefetch_requests_log_pages",
  "Num_vacuum_prefetch_hits_log_pages",
  "Num_heap_home_inserts",
  "Num_heap_big_inserts",
  "Num_heap_assign_inserts",
  "Num_heap_home_deletes",
  "Num_heap_home_mvcc_deletes",
  "Num_heap_home_to_rel_deletes",
  "Num_heap_home_to_big_deletes",
  "Num_heap_rel_deletes",
  "Num_heap_rel_mvcc_deletes",
  "Num_heap_rel_to_home_deletes",
  "Num_heap_rel_to_big_deletes",
  "Num_heap_rel_to_rel_deletes",
  "Num_heap_big_deletes",
  "Num_heap_big_mvcc_deletes",
  "Num_heap_new_ver_inserts",
  "Num_heap_home_updates",
  "Num_heap_home_to_rel_updates",
  "Num_heap_home_to_big_updates",
  "Num_heap_rel_updates",
  "Num_heap_rel_to_home_updates",
  "Num_heap_rel_to_rel_updates",
  "Num_heap_rel_to_big_updates",
  "Num_heap_big_updates",
  "Num_heap_home_vacuums",
  "Num_heap_big_vacuums",
  "Num_heap_rel_vacuums",
  "Num_heap_insid_vacuums",
  "Num_heap_remove_vacuums",
  "Num_heap_next_ver_vacuums",
  "Time_heap_insert_prepare",
  "Time_heap_insert_execute",
  "Time_heap_insert_log",
  "Time_heap_delete_prepare",
  "Time_heap_delete_execute",
  "Time_heap_delete_log",
  "Time_heap_update_prepare",
  "Time_heap_update_execute",
  "Time_heap_update_log",
  "Time_heap_vacuum_prepare",
  "Time_heap_vacuum_execute",
  "Time_heap_vacuum_log",
  "Num_bt_find_unique",
  "Num_btrange_search",
  "Num_bt_insert_obj",
  "Num_bt_delete_obj",
  "Num_bt_mvcc_delete",
  "Num_bt_mark_delete",
  "Num_bt_update_sk_cnt",
  "Num_bt_undo_insert",
  "Num_bt_undo_delete",
  "Num_bt_undo_mvcc_delete",
  "Num_bt_undo_update_sk",
  "Num_bt_vacuum",
  "Num_bt_vacuum_insid",
  "Num_bt_vacuum_update_sk",
  "Num_bt_fix_ovf_oids_cnt",
  "Num_bt_unique_rlocks_cnt",
  "Num_bt_unique_wlocks_cnt",
  "Time_bt_find_unique",
  "Time_bt_range_search",
  "Time_bt_insert",
  "Time_bt_delete",
  "Time_bt_mvcc_delete",
  "Time_bt_mark_delete",
  "Time_bt_update_sk",
  "Time_bt_undo_insert",
  "Time_bt_undo_delete",
  "Time_bt_undo_mvcc_delete",
  "Time_bt_undo_update_sk",
  "Time_bt_vacuum",
  "Time_bt_vacuum_insid",
  "Time_bt_vacuum_update_sk",
  "Time_bt_traverse",
  "Time_bt_find_unique_traverse",
  "Time_bt_range_search_traverse",
  "Time_bt_insert_traverse",
  "Time_bt_delete_traverse",
  "Time_bt_mvcc_delete_traverse",
  "Time_bt_mark_delete_traverse",
  "Time_bt_update_sk_traverse",
  "Time_bt_undo_insert_traverse",
  "Time_bt_undo_delete_traverse",
  "Time_bt_undo_mvcc_delete_traverse",
  "Time_bt_undo_update_sk_traverse",
  "Time_bt_vacuum_traverse",
  "Time_bt_vacuum_insid_traverse",
  "Time_bt_vacuum_update_sk_traverse",
  "Time_bt_fix_ovf_oids",
  "Time_bt_unique_rlocks",
  "Time_bt_unique_wlocks",
  "Time_vacuum_master",
  "Time_vacuum_worker_process_log",
  "Time_vacuum_worker_execute",

  /* computed statistics */
  "Data_page_buffer_hit_ratio",
  "Log_page_buffer_hit_ratio",
  "Vacuum_data_page_buffer_hit_ratio",
  "Vacuum_page_efficiency_ratio",
  "Vacuum_page_fetch_ratio",
  "Data_page_fix_lock_acquire_time_msec",
  "Data_page_fix_hold_acquire_time_msec",
  "Data_page_fix_acquire_time_msec",
  "Data_page_allocate_time_ratio",
  "Data_page_total_promote_success",
  "Data_page_total_promote_fail",
  "Data_page_total_promote_time_msec",

  /* Array type statistics */
  "Num_data_page_fix_ext",
  "Num_data_page_promote_ext",
  "Num_data_page_promote_time_ext",
  "Num_data_page_unfix_ext",
  "Time_data_page_lock_acquire_time",
  "Time_data_page_hold_acquire_time",
  "Time_data_page_fix_acquire_time",
  "Num_mvcc_snapshot_ext",
  "Time_obj_lock_acquire_time",
  "Time_get_snapshot_acquire_time",
  "Count_get_snapshot_retry",
  "Time_tran_complete_time",
  "Time_get_oldest_mvcc_acquire_time",
  "Count_get_oldest_mvcc_retry"
};

#if defined(SERVER_MODE) || defined(SA_MODE)
int mnt_Num_tran_exec_stats = 0;

#if defined(SERVER_MODE) && defined(HAVE_ATOMIC_BUILTINS) \
    && (defined (WINDOWS) || (__WORDSIZE == 64) || (GCC_VERSION > 40402))
#define ATOMIC_TAS(A,VAL)   ATOMIC_TAS_64(&(A),(VAL))
#define ATOMIC_INC(A,VAL)   ATOMIC_INC_64(&(A),(VAL))
#else /* SERVER_MODE && HAVE_ATOMIC_BUILTINS */
#define ATOMIC_TAS(A,VAL)          (A)=(VAL)
#define ATOMIC_INC(A,VAL)          (A)+=(VAL)
#if defined (SERVER_MODE)
pthread_mutex_t mnt_Num_tran_stats_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif /* SERVER_MODE && HAVE_ATOMIC_BUILTINS && (WINDOWS) || (__WORDSIZE == 64) || (GCC_VERSION > 40402)) */

#define ADD_STATS(STAT,VAR,VALUE)                        \
do {                                                     \
  if ((STAT)->enable_local_stat)                         \
    {                                                    \
      (STAT)->VAR += (VALUE);                            \
    }                                                    \
  ATOMIC_INC(mnt_Server_table.global_stats->VAR, VALUE); \
} while (0)

#define ADD_STATS_IN_ARRAY(STAT,VAR,POS,VALUE)			\
do {								\
  if ((STAT)->enable_local_stat)				\
    {								\
      (STAT)->VAR[(POS)] += (VALUE);                            \
    }							        \
  ATOMIC_INC(mnt_Server_table.global_stats->VAR[(POS)], VALUE); \
} while (0)

#define SET_STATS(STAT,VAR,VALUE)                        \
do {                                                     \
  if ((STAT)->enable_local_stat)                         \
    {                                                    \
      (STAT)->VAR = (VALUE);                             \
    }                                                    \
  ATOMIC_TAS(mnt_Server_table.global_stats->VAR, VALUE); \
} while (0)

/* Server execution statistics on each transactions */
struct mnt_server_table
{
  int num_tran_indices;
  MNT_SERVER_EXEC_STATS *stats;
  MNT_SERVER_EXEC_STATS *global_stats;
};

static struct mnt_server_table mnt_Server_table = {
  /* num_tran_indices */
  0,
  /* stats */
  NULL,
  /* global_stats */
  NULL
};

/*
 * mnt_server_init - Initialize monitoring resources in the server
 *   return: NO_ERROR or ER_FAILED
 *   num_tran_indices(in): maximum number of know transaction indices
 */
int
mnt_server_init (int num_tran_indices)
{
  mnt_Server_table.num_tran_indices = num_tran_indices;
  mnt_Num_tran_exec_stats = 0;

  mnt_Server_table.stats = malloc (num_tran_indices * sizeof (MNT_SERVER_EXEC_STATS));

  if (mnt_Server_table.stats == NULL)
    {
      return ER_FAILED;
    }

  mnt_Server_table.global_stats = malloc (sizeof (MNT_SERVER_EXEC_STATS));

  if (mnt_Server_table.global_stats == NULL)
    {
      free_and_init (mnt_Server_table.stats);
      return ER_FAILED;
    }

  memset (mnt_Server_table.stats, 0, sizeof (MNT_SERVER_EXEC_STATS) * num_tran_indices);

  memset (mnt_Server_table.global_stats, 0, sizeof (MNT_SERVER_EXEC_STATS));

  return NO_ERROR;
}

/*
 * mnt_server_final - Terminate monitoring resources in the server
 *   return: none
 */
void
mnt_server_final (void)
{
  if (mnt_Server_table.stats != NULL)
    {
      free_and_init (mnt_Server_table.stats);
    }
  if (mnt_Server_table.global_stats != NULL)
    {
      free_and_init (mnt_Server_table.global_stats);
    }

  mnt_Server_table.num_tran_indices = 0;
  mnt_Num_tran_exec_stats = 0;
}

/*
 * xmnt_server_start_stats - Start collecting server execution statistics
 *                           for the current transaction index
 *   return: NO_ERROR or ER_FAILED
 */
int
xmnt_server_start_stats (THREAD_ENTRY * thread_p, bool for_all_trans)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  if (tran_index >= mnt_Server_table.num_tran_indices)
    {
      return ER_FAILED;
    }

  if (mnt_Server_table.stats[tran_index].enable_local_stat == true)
    {
      return NO_ERROR;
    }

  memset (&mnt_Server_table.stats[tran_index], '\0', sizeof (MNT_SERVER_EXEC_STATS));
  mnt_Server_table.stats[tran_index].enable_local_stat = true;

#if defined (HAVE_ATOMIC_BUILTINS)
  ATOMIC_INC_32 (&mnt_Num_tran_exec_stats, 1);
#else
  int rv = pthread_mutex_lock (&mnt_Num_tran_stats_lock);
  mnt_Num_tran_exec_stats++;
  pthread_mutex_unlock (&mnt_Num_tran_stats_lock);
#endif

  return NO_ERROR;
}

/*
 * xmnt_server_stop_stats - Stop collecting server execution statistics
 *                          for the current transaction index
 *   return: none
 */
void
xmnt_server_stop_stats (THREAD_ENTRY * thread_p)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  if (tran_index >= mnt_Server_table.num_tran_indices)
    {
      return;
    }

  if (mnt_Server_table.stats[tran_index].enable_local_stat == false)
    {
      return;
    }

  mnt_Server_table.stats[tran_index].enable_local_stat = false;

#if defined (HAVE_ATOMIC_BUILTINS)
  ATOMIC_INC_32 (&mnt_Num_tran_exec_stats, -1);
#else
  int rv = pthread_mutex_lock (&mnt_Num_tran_stats_lock);
  mnt_Num_tran_exec_stats--;
  pthread_mutex_unlock (&mnt_Num_tran_stats_lock);
#endif
}

/*
 * xmnt_server_is_stats_on - Is collecting server execution statistics
 *                           for the current transaction index
 *   return: bool
 */
bool
mnt_server_is_stats_on (THREAD_ENTRY * thread_p)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  if (tran_index >= mnt_Server_table.num_tran_indices)
    {
      return false;
    }

  return mnt_Server_table.stats[tran_index].enable_local_stat;
}

/*
 * mnt_server_get_stats - Get the recorded server statistics for the current
 *                        transaction index
 */
STATIC_INLINE MNT_SERVER_EXEC_STATS *
mnt_server_get_stats (THREAD_ENTRY * thread_p)
{
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  if (tran_index >= mnt_Server_table.num_tran_indices)
    {
      return NULL;
    }

  return &mnt_Server_table.stats[tran_index];
}

/*
 * mnt_server_check_stats_threshold -
 */
static void
mnt_server_check_stats_threshold (int tran_index, MNT_SERVER_EXEC_STATS * stats)
{
  unsigned int i, size;
  unsigned int *stats_ptr;
  int *prm_ptr;

  if (prm_get_integer_list_value (PRM_ID_MNT_STATS_THRESHOLD))
    {
      size = (unsigned int) prm_get_integer_list_value (PRM_ID_MNT_STATS_THRESHOLD)[0];
      size = MIN (size, MNT_SIZE_OF_SERVER_EXEC_SINGLE_STATS - MNT_COUNT_OF_SERVER_EXEC_CALC_STATS);
      stats_ptr = (unsigned int *) stats;
      prm_ptr = (int *) &prm_get_integer_list_value (PRM_ID_MNT_STATS_THRESHOLD)[1];

      for (i = 0; i < size; i++)
	{
	  if (*prm_ptr > 0 && (unsigned int) *prm_ptr < *stats_ptr)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_MNT_STATS_THRESHOLD, 2, tran_index,
		      mnt_Stats_name[i]);
	    }
	  stats_ptr++, prm_ptr++;
	}
    }
}

/*
 * xmnt_server_copy_stats - Copy recorded server statistics for the current
 *                          transaction index
 *   return: none
 *   to_stats(out): buffer to copy
 */
void
xmnt_server_copy_stats (THREAD_ENTRY * thread_p, MNT_SERVER_EXEC_STATS * to_stats)
{
  MNT_SERVER_EXEC_STATS *from_stats;

  from_stats = mnt_server_get_stats (thread_p);

  if (from_stats != NULL)
    {
      mnt_server_calc_stats (from_stats);
      *to_stats = *from_stats;	/* Structure copy */
    }
}

/*
 * xmnt_server_copy_global_stats - Copy recorded system wide statistics
 *   return: none
 *   to_stats(out): buffer to copy
 */
void
xmnt_server_copy_global_stats (THREAD_ENTRY * thread_p, MNT_SERVER_EXEC_STATS * to_stats)
{
  if (to_stats)
    {
      *to_stats = *mnt_Server_table.global_stats;
      mnt_server_calc_stats (to_stats);
    }
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * enclosing_method - Print server statistics for current transaction index
 *   return: none
 *   stream(in): if NULL is given, stdout is used
 */
void
mnt_server_print_stats (THREAD_ENTRY * thread_p, FILE * stream)
{
  MNT_SERVER_EXEC_STATS *stats;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  stats = mnt_server_get_stats (thread_p);
  if (stats == NULL)
    {
      return;
    }
  rv = pthread_mutex_lock (&stats->lock);
  mnt_server_dump_stats (stats, stream);
  pthread_mutex_unlock (&stats->lock);
}
#endif

/*
 * mnt_x_file_creates - Increase file_num_creates counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_file_creates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, file_num_creates, 1);
    }
}

/*
 * mnt_x_file_removes - Increase file_num_remvoes counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_file_removes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, file_num_removes, 1);
    }
}

/*
 * mnt_x_file_ioreads - Increase file_num_ioreads counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_file_ioreads (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, file_num_ioreads, 1);
    }
}

/*
 * mnt_x_file_iowrites - Increase file_num_iowrites counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_file_iowrites (THREAD_ENTRY * thread_p, int num_pages)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, file_num_iowrites, num_pages);
    }
}

/*
 * mnt_x_file_iosynches - Increase file_num_iosynches counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_file_iosynches (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, file_num_iosynches, 1);
    }
}

/*
 * mnt_x_file_page_allocs - Increase file page alloc counter of the current
 *                          transaction index
 *   return: none
 */
void
mnt_x_file_page_allocs (THREAD_ENTRY * thread_p, int num_pages)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, file_num_page_allocs, num_pages);
    }
}

/*
 * mnt_x_file_page_deallocs - Decrease file page alloc counter of the current
 *                            transaction index
 *   return: none
 */
void
mnt_x_file_page_deallocs (THREAD_ENTRY * thread_p, int num_pages)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, file_num_page_deallocs, num_pages);
    }
}

/*
 * mnt_x_pb_fetches - Increase pb_num_fetches counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_pb_fetches (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pb_num_fetches, 1);
    }
}

/*
 * mnt_x_pb_dirties - Increase pb_num_dirties counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_pb_dirties (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pb_num_dirties, 1);
    }
}

/*
 * mnt_x_pb_ioreads - Increase pb_num_ioreads counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_pb_ioreads (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pb_num_ioreads, 1);
    }
}

/*
 * mnt_x_pb_iowrites - Increase pb_num_iowrites counter of the current
 *                     transaction index
 *   return: none
 */
void
mnt_x_pb_iowrites (THREAD_ENTRY * thread_p, int num_pages)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pb_num_iowrites, num_pages);
    }
}

/*
 * mnt_x_pb_victims - Increase pb_num_victims counter of the current
 *                     transaction index
 *   return: none
 */
void
mnt_x_pb_victims (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pb_num_victims, 1);
    }
}

/*
 * mnt_x_pb_replacements - Increase page replacement counter of the current
 *                     transaction index
 *   return: none
 */
void
mnt_x_pb_replacements (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pb_num_replacements, 1);
    }
}

/*
 * mnt_x_pb_num_hash_anchor_waits - Increase page anchor waits counter and time
 *   return: none
 */
void
mnt_x_pb_num_hash_anchor_waits (THREAD_ENTRY * thread_p, UINT64 time_amount)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pb_num_hash_anchor_waits, 1);
      ADD_STATS (stats, pb_time_hash_anchor_wait, time_amount);
    }
}

/*
 * mnt_x_prior_lsa_list_size -
 *   return: none
 */
void
mnt_x_prior_lsa_list_size (THREAD_ENTRY * thread_p, unsigned int list_size)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, prior_lsa_list_size, list_size);
    }
}

/*
 * mnt_x_prior_lsa_list_maxed -
 *   return: none
 */
void
mnt_x_prior_lsa_list_maxed (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, prior_lsa_list_maxed, 1);
    }
}

/*
 * mnt_x_prior_lsa_list_removed -
 *   return: none
 */
void
mnt_x_prior_lsa_list_removed (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, prior_lsa_list_removed, 1);
    }
}

/*
 * mnt_x_hf_stats_bestspace_entries -
 *   return: none
 */
void
mnt_x_hf_stats_bestspace_entries (THREAD_ENTRY * thread_p, unsigned int num_entries)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      SET_STATS (stats, hf_num_stats_entries, num_entries);
    }
}

/*
 * mnt_x_hf_stats_bestspace_maxed -
 *   return: none
 */
void
mnt_x_hf_stats_bestspace_maxed (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, hf_num_stats_maxed, 1);
    }
}

/*
 * mnt_x_fc_stats -
 *   return: none
 */
void
mnt_x_fc_stats (THREAD_ENTRY * thread_p, unsigned int num_pages, unsigned int num_log_pages, unsigned int tokens)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, fc_num_pages, num_pages);
      ADD_STATS (stats, fc_num_log_pages, num_log_pages);
      ADD_STATS (stats, fc_tokens, tokens);
    }
}

/*
 * mnt_x_log_fetches - Increase log_num_fetches counter of the current
 *                     transaction index
 *   return: none
 */
void
mnt_x_log_fetches (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_fetches, 1);
    }
}

/*
 * mnt_x_log_fetch_ioreads - Increase log_num_fetch_ioreads counter of the
 *			     current transaction index
 *   return: none
 */
void
mnt_x_log_fetch_ioreads (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_fetch_ioreads, 1);
    }
}

/*
 * mnt_x_log_ioreads - Increase pb_num_ioreads counter of the current
 *                     transaction index
 *   return: none
 */
void
mnt_x_log_ioreads (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_ioreads, 1);
    }
}

/*
 * mnt_x_log_iowrites - Increase log_num_iowrites counter of the current
 *                      transaction index
 *   return: none
 *
 *   num_log_pages(in):
 */
void
mnt_x_log_iowrites (THREAD_ENTRY * thread_p, int num_log_pages)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_iowrites, num_log_pages);
    }
}

/*
 * mnt_x_log_appendrecs - Increase log_num_appendrecs counter of the current
 *                        transaction index
 *   return: none
 */
void
mnt_x_log_appendrecs (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_appendrecs, 1);
    }
}

/*
 * mnt_x_log_archives - Increase log_num_archives counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_log_archives (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_archives, 1);
    }
}

/*
 * mnt_x_log_start_checkpoints - Increase log_num_start_checkpoints counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_log_start_checkpoints (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_start_checkpoints, 1);
    }
}

/*
 * mnt_x_log_end_checkpoints - Increase log_num_end_checkpoints counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_log_end_checkpoints (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_end_checkpoints, 1);
    }
}

/*
 * mnt_x_log_wals - Increase log flush for wal counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_log_wals (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_wals, 1);
    }
}

/*
 * mnt_x_log_replacements - Increase log flush for replacement counter of the
 *			    current transaction index
 *   return: none
 */
void
mnt_x_log_replacements (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, log_num_replacements, 1);
    }
}


/*
 * mnt_x_lk_acquired_on_pages - Increase lk_num_acquired_on_pages counter
 *                              of the current transaction index
 *   return: none
 */
void
mnt_x_lk_acquired_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_acquired_on_pages, 1);
    }
}

/*
 * mnt_x_lk_acquired_on_objects - Increase lk_num_acquired_on_objects counter
 *                                of the current transaction index
 *   return: none
 */
void
mnt_x_lk_acquired_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_acquired_on_objects, 1);
    }
}

/*
 * mnt_x_lk_converted_on_pages - Increase lk_num_converted_on_pages counter
 *                               of the current transaction index
 *   return: none
 */
void
mnt_x_lk_converted_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_converted_on_pages, 1);
    }
}

/*
 * mnt_x_lk_converted_on_objects - Increase lk_num_converted_on_objects
 *                                 counter of the current transaction index
 *   return: none
 */
void
mnt_x_lk_converted_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_converted_on_objects, 1);
    }
}

/*
 * mnt_x_lk_re_requested_on_pages - Increase lk_num_re_requested_on_pages
 *                                  counter of the current transaction index
 *   return: none
 */
void
mnt_x_lk_re_requested_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_re_requested_on_pages, 1);
    }
}

/*
 * mnt_x_lk_re_requested_on_objects - Increase lk_num_re_requested_on_objects
 *                                    counter of the current transaction index
 *   return: none
 */
void
mnt_x_lk_re_requested_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_re_requested_on_objects, 1);
    }
}

/*
 * mnt_x_lk_waited_on_pages - Increase lk_num_waited_on_pages counter of the
 *                            current transaction index
 *   return: none
 */
void
mnt_x_lk_waited_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_waited_on_pages, 1);
    }
}

/*
 * mnt_x_lk_waited_on_objects - Increase lk_num_waited_on_objects counter of
 *                              the current transaction index
 *   return: none
 */
void
mnt_x_lk_waited_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_waited_on_objects, 1);
    }
}

#if defined(PERF_ENABLE_LOCK_OBJECT_STAT)
/*
 * mnt_x_lk_waited_time_on_objects - Increase lock time wait counter of
 *                              the current transaction index
 *   return: none
 */
void
mnt_x_lk_waited_time_on_objects (THREAD_ENTRY * thread_p, int lock_mode, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, lk_num_waited_time_on_objects, amount);

      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
      assert (lock_mode >= NA_LOCK && lock_mode <= SCH_M_LOCK);

      offset = PERF_OBJ_LOCK_STAT_OFFSET (module, lock_mode);
      assert (offset < PERF_OBJ_LOCK_STAT_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, obj_lock_time_counters, offset, amount);
    }
}
#endif /* PERF_ENABLE_LOCK_OBJECT_STAT */

/*
 * mnt_x_tran_commits - Increase tran_num_commits counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_tran_commits (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, tran_num_commits, 1);
    }
}

/*
 * mnt_x_tran_rollbacks - Increase tran_num_rollbacks counter of the current
 *                        transaction index
 *   return: none
 */
void
mnt_x_tran_rollbacks (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, tran_num_rollbacks, 1);
    }
}

/*
 * mnt_x_tran_savepoints - Increase tran_num_savepoints counter of the current
 *                         transaction index
 *   return: none
 */
void
mnt_x_tran_savepoints (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, tran_num_savepoints, 1);
    }
}

/*
 * mnt_x_tran_start_topops - Increase tran_num_start_topops counter of the
 *                           current transaction index
 *   return: none
 */
void
mnt_x_tran_start_topops (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, tran_num_start_topops, 1);
    }
}

/*
 * mnt_x_tran_end_topops - Increase tran_num_end_topops counter of the current
 *                         transaction index
 *   return: none
 */
void
mnt_x_tran_end_topops (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, tran_num_end_topops, 1);
    }
}

/*
 * mnt_x_tran_interrupts - Increase tran_num_interrupts counter of the current
 *                         transaction index
 *   return: none
 */
void
mnt_x_tran_interrupts (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, tran_num_interrupts, 1);
    }
}

/*
 * mnt_x_bt_inserts - Increase bt_num_inserts counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_bt_inserts (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_inserts, 1);
    }
}

/*
 * mnt_x_bt_deletes - Increase bt_num_deletes counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_bt_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_deletes, 1);
    }
}

/*
 * mnt_x_bt_updates - Increase bt_num_updates counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_bt_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_updates, 1);
    }
}

/*
 * mnt_x_bt_covered - Increase bt_num_covered counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_bt_covered (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_covered, 1);
    }
}

/*
 * mnt_x_bt_noncovered - Increase bt_num_noncovered counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_bt_noncovered (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_noncovered, 1);
    }
}

/*
 * mnt_x_bt_resumes - Increase bt_num_resumes counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_bt_resumes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_resumes, 1);
    }
}

/*
 * mnt_x_bt_multi_range_opt - Increase bt_num_multi_range_opt counter of the
 *                            current transaction index
 *   return: none
 */
void
mnt_x_bt_multi_range_opt (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_multi_range_opt, 1);
    }
}

/*
 * mnt_x_bt_splits - Increase bt_num_splits counter of the current
 *                   transaction index
 *   return: none
 */
void
mnt_x_bt_splits (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_splits, 1);
    }
}

/*
 * mnt_x_bt_merges - Increase bt_num_merges counter of the current
 *                   transaction index
 *   return: none
 */
void
mnt_x_bt_merges (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_merges, 1);
    }
}

/*
 * mnt_x_bt_get_stats - Increase bt_num_get_stats counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_bt_get_stats (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_num_get_stats, 1);
    }
}

/*
 * mnt_x_qm_selects - Increase qm_num_selects counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_selects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_selects, 1);
    }
}

/*
 * mnt_x_qm_inserts - Increase qm_num_inserts counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_inserts (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_inserts, 1);
    }
}

/*
 * mnt_x_qm_deletes - Increase qm_num_deletes counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_deletes, 1);
    }
}

/*
 * mnt_x_qm_updates - Increase qm_num_updates counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_updates, 1);
    }
}

/*
 * mnt_x_qm_sscans - Increase qm_num_sscans counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_sscans (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_sscans, 1);
    }
}

/*
 * mnt_x_qm_iscans - Increase qm_num_iscans counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_iscans (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_iscans, 1);
    }
}

/*
 * mnt_x_qm_lscans - Increase qm_num_lscans counter of the current
                     transaction index
 *   return: none
 */
void
mnt_x_qm_lscans (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_lscans, 1);
    }
}

/*
 * mnt_x_qm_setscans - Increase qm_num_setscans counter of the current
                       transaction index
 *   return: none
 */
void
mnt_x_qm_setscans (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_setscans, 1);
    }
}

/*
 * mnt_x_qm_methscans - Increase qm_num_methscans counter of the current
                        transaction index
 *   return: none
 */
void
mnt_x_qm_methscans (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_methscans, 1);
    }
}

/*
 * mnt_x_qm_nljoins - Increase qm_num_nljoins counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_nljoins (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_nljoins, 1);
    }
}

/*
 * mnt_x_qm_mjoins - Increase qm_num_mjoins counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_mjoins (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_mjoins, 1);
    }
}

/*
 * mnt_x_qm_objfetches - Increase qm_num_objfetches counter of the current
                      transaction index
 *   return: none
 */
void
mnt_x_qm_objfetches (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, qm_num_objfetches, 1);
    }
}

/*
 * mnt_x_holdable_cursor - Increase qm_num_holdable_cursors counter of the
 *                         current transaction index
 *   return: none
 */
void
mnt_x_qm_holdable_cursor (THREAD_ENTRY * thread_p, int num_cursors)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      SET_STATS (stats, qm_num_holdable_cursors, num_cursors);
    }
}

/*
 * mnt_x_sort_io_pages - Increase sort_num_io_pages counter of the current
 *			 transaction index
 *   return: none
 */
void
mnt_x_sort_io_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, sort_num_io_pages, 1);
    }
}

/*
 * mnt_x_sort_data_pages - Increase sort_num_data_pages counter of the
 *			   current transaction index
 *   return: none
 */
void
mnt_x_sort_data_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, sort_num_data_pages, 1);
    }
}

/*
 * mnt_get_sort_io_pages - return sort_num_io_pages counter of the current
 *                       transaction index
 *   return: none
 */
UINT64
mnt_get_sort_io_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      return stats->sort_num_io_pages;
    }

  return 0;
}

/*
 * mnt_get_sort_data_pages - return sort_num_data_pages counter of the
 *                         current transaction index
 *   return: none
 */
UINT64
mnt_get_sort_data_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      return stats->sort_num_data_pages;
    }

  return 0;
}

/*
 * mnt_get_pb_fetches - Return pb_num_fetches counter of the current
 *                    transaction index
 *   return: pb_num_fetches
 */
UINT64
mnt_get_pb_fetches (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      return stats->pb_num_fetches;
    }

  return 0;
}

/*
 * mnt_get_pb_ioreads - Return pb_num_ioreads counter of the current
 *                    transaction index
 *   return: pb_num_ioreads
 */
UINT64
mnt_get_pb_ioreads (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      return stats->pb_num_ioreads;
    }

  return 0;
}

/*
 * mnt_x_net_requests - Increase net_num_requests counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_net_requests (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, net_num_requests, 1);
    }
}

UINT64
mnt_x_get_stats_and_clear (THREAD_ENTRY * thread_p, const char *stat_name)
{
  MNT_SERVER_EXEC_STATS *stats;
  unsigned int i;
  UINT64 *stats_ptr;
  UINT64 copied;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      stats_ptr = (UINT64 *) stats;
      for (i = 0; i < MNT_SIZE_OF_SERVER_EXEC_SINGLE_STATS - MNT_COUNT_OF_SERVER_EXEC_CALC_STATS; i++)
	{
	  if (strcmp (mnt_Stats_name[i], stat_name) == 0)
	    {
	      copied = stats_ptr[i];
	      stats_ptr[i] = 0;
	      return copied;
	    }
	}
    }

  return 0;
}


void
mnt_x_ha_repl_delay (THREAD_ENTRY * thread_p, int delay)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      SET_STATS (stats, ha_repl_delay, delay);
    }

}

/*
 * mnt_x_pc_add - Increase pc_num_add when a plan cache entry is added.
 *   return: none
 */
void
mnt_x_pc_add (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pc_num_add, 1);
    }
}

/*
 * mnt_x_pc_lookup - Increase pc_num_lookup when a plan cache entry is 
 *                   looked up. 
 *   return: none
 */
void
mnt_x_pc_lookup (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pc_num_lookup, 1);
    }
}

/*
 * mnt_x_pc_hit - Increase pc_num_hit when a plan cache entry is hit.
 *   return: none
 */
void
mnt_x_pc_hit (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pc_num_hit, 1);
    }
}

/*
 * mnt_x_pc_miss - Increase pc_num_miss when a plan cache entry is not hit. 
 *   return: none
 */
void
mnt_x_pc_miss (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pc_num_miss, 1);
    }
}

/*
 * mnt_x_pc_full - Increase pc_num_full when the number of plan cache entries 
 *                 is exceed the maximum entries.
 *   return: none
 */
void
mnt_x_pc_full (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pc_num_full, 1);
    }
}

/*
 * mnt_x_pc_delete - Increase pc_num_delete when a plan cache entry is deleted.
 *   return: none
 */
void
mnt_x_pc_delete (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pc_num_delete, 1);
    }
}

/*
 * mnt_x_pc_invalid_xasl_id - Increase pc_num_invalid_xasl_id when a xasl_id 
 *                            is invalid.
 *   return: none
 */
void
mnt_x_pc_invalid_xasl_id (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, pc_num_invalid_xasl_id, 1);
    }
}

/*
 * mnt_x_pc_query_string_hash_entries - update pc_num_query_string_hash_entries
 *                                      that represent the current number of 
 *                                      entries of the query string hash.
 *   return: none
 *   num_entries(in): the number of entries.
 */
void
mnt_x_pc_query_string_hash_entries (THREAD_ENTRY * thread_p, unsigned int num_entries)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      SET_STATS (stats, pc_num_query_string_hash_entries, num_entries);
    }
}

/*
 * mnt_x_pc_xasl_id_hash_entries - update pc_num_xasl_id_hash_entries that 
 *                                 represent the current numver of entries 
 *                                 of the xasl id hash.
 *   return: none
 *   num_entries(in): the number of entries.
 */
void
mnt_x_pc_xasl_id_hash_entries (THREAD_ENTRY * thread_p, unsigned int num_entries)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      SET_STATS (stats, pc_num_xasl_id_hash_entries, num_entries);
    }
}

/*
 * mnt_x_pc_class_oid_hash_entries - update pc_num_class_oid_hash_entries
 *                                   that represent the current number of 
 *                                   entries of the class oid hash.
 *   return: none
 *   num_entries(in): the number of entries.
 */
void
mnt_x_pc_class_oid_hash_entries (THREAD_ENTRY * thread_p, unsigned int num_entries)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      SET_STATS (stats, pc_num_class_oid_hash_entries, num_entries);
    }
}

/*
 *   mnt_x_heap_stats_sync_bestspace - Increase heap_num_stats_sync_bestspace 
 *				       counter when synchronized 
 *				       the statistics of best space of heap.
 *   return: none
 */

void
mnt_x_heap_stats_sync_bestspace (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_num_stats_sync_bestspace, 1);
    }
}

/*
 * mnt_x_vac_log_vacuumed_pages - Increase vac_num_vacuumed_log_pages when a log
 *				  page was vacuumed
 *   return: none
 */
void
mnt_x_vac_log_vacuumed_pages (THREAD_ENTRY * thread_p, unsigned int num_entries)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, vac_num_vacuumed_log_pages, num_entries);
    }
}

/*
 * mnt_x_vac_log_to_vacuum_pages - Increase vac_num_to_vacuum_log_pages when a
 *				   log page needs to be vacuumed
 *   return: none
 */
void
mnt_x_vac_log_to_vacuum_pages (THREAD_ENTRY * thread_p, unsigned int num_entries)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, vac_num_to_vacuum_log_pages, num_entries);
    }
}

/*
 * mnt_x_vac_prefetch_log_requests_pages -
 *		  Increase vac_num_prefetch_requests_log_pages when a
 *		  log page is requested from vacuum prefetch log buffer
 *
 *   return: none
 */
void
mnt_x_vac_prefetch_log_requests_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, vac_num_prefetch_requests_log_pages, 1);
    }
}

/*
 * mnt_x_vac_prefetch_log_hits_pages -
 *		  Increase vac_num_prefetch_hits_log_pages when a
 *		  log page is hit in vacuum prefetch log buffer
 *
 *   return: none
 */
void
mnt_x_vac_prefetch_log_hits_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, vac_num_prefetch_hits_log_pages, 1);
    }
}

/*
 *   mnt_x_pbx_fix - 
 *   return: none
 */
void
mnt_x_pbx_fix (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode, int cond_type)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
      assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
      assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
      assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
      assert (cond_type >= PERF_CONDITIONAL_FIX && cond_type < PERF_CONDITIONAL_FIX_CNT);

      offset = PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type);
      assert (offset < PERF_PAGE_FIX_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, pbx_fix_counters, offset, 1);
    }
}

/*
 *   mnt_x_pbx_promote - 
 *   return: none
 */
void
mnt_x_pbx_promote (THREAD_ENTRY * thread_p, int page_type, int promote_cond, int holder_latch, int success,
		   UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
      assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
      assert (promote_cond >= PERF_PROMOTE_ONLY_READER && promote_cond < PERF_PROMOTE_CONDITION_CNT);
      assert (holder_latch >= PERF_HOLDER_LATCH_READ && holder_latch < PERF_HOLDER_LATCH_CNT);
      assert (success == 0 || success == 1);

      offset = PERF_PAGE_PROMOTE_STAT_OFFSET (module, page_type, promote_cond, holder_latch, success);
      assert (offset < PERF_PAGE_PROMOTE_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, pbx_promote_counters, offset, 1);
      ADD_STATS_IN_ARRAY (stats, pbx_promote_time_counters, offset, amount);
    }
}

/*
 *   mnt_x_pbx_unfix - 
 *   return: none
 */
void
mnt_x_pbx_unfix (THREAD_ENTRY * thread_p, int page_type, int buf_dirty, int dirtied_by_holder, int holder_latch)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
      assert (page_type > PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
      assert (buf_dirty == 0 || buf_dirty == 1);
      assert (dirtied_by_holder == 0 || dirtied_by_holder == 1);
      assert (holder_latch >= PERF_HOLDER_LATCH_READ && holder_latch < PERF_HOLDER_LATCH_CNT);

      offset = PERF_PAGE_UNFIX_STAT_OFFSET (module, page_type, buf_dirty, dirtied_by_holder, holder_latch);
      assert (offset < PERF_PAGE_UNFIX_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, pbx_unfix_counters, offset, 1);
    }
}

/*
 *   mnt_x_pbx_lock_acquire_time - 
 *   return: none
 */
void
mnt_x_pbx_lock_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode, int cond_type,
			     UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
      assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
      assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
      assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
      assert (cond_type >= PERF_CONDITIONAL_FIX && cond_type < PERF_CONDITIONAL_FIX_CNT);
      assert (amount > 0);

      offset = PERF_PAGE_LOCK_TIME_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type);
      assert (offset < PERF_PAGE_LOCK_TIME_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, pbx_lock_time_counters, offset, amount);
    }
}

/*
 *   mnt_x_pbx_hold_acquire_time - 
 *   return: none
 */
void
mnt_x_pbx_hold_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
      assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
      assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
      assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
      assert (amount > 0);

      offset = PERF_PAGE_HOLD_TIME_OFFSET (module, page_type, page_found_mode, latch_mode);
      assert (offset < PERF_PAGE_HOLD_TIME_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, pbx_hold_time_counters, offset, amount);
    }
}

/*
 *   mnt_x_pbx_fix_acquire_time - 
 *   return: none
 */
void
mnt_x_pbx_fix_acquire_time (THREAD_ENTRY * thread_p, int page_type, int page_found_mode, int latch_mode, int cond_type,
			    UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);
      assert (page_type >= PERF_PAGE_UNKNOWN && page_type < PERF_PAGE_CNT);
      assert (page_found_mode >= PERF_PAGE_MODE_OLD_LOCK_WAIT && page_found_mode < PERF_PAGE_MODE_CNT);
      assert (latch_mode >= PERF_HOLDER_LATCH_READ && latch_mode < PERF_HOLDER_LATCH_CNT);
      assert (cond_type >= PERF_CONDITIONAL_FIX && cond_type < PERF_CONDITIONAL_FIX_CNT);
      assert (amount > 0);

      offset = PERF_PAGE_FIX_TIME_OFFSET (module, page_type, page_found_mode, latch_mode, cond_type);
      assert (offset < PERF_PAGE_FIX_TIME_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, pbx_fix_time_counters, offset, amount);
    }
}

#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
/*
 *   mnt_x_mvcc_snapshot - 
 *   return: none
 */
void
mnt_x_mvcc_snapshot (THREAD_ENTRY * thread_p, int snapshot, int rec_type, int visibility)
{
  MNT_SERVER_EXEC_STATS *stats;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      assert (snapshot >= PERF_SNAPSHOT_SATISFIES_DELETE && snapshot < PERF_SNAPSHOT_CNT);
      assert (rec_type >= PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED && rec_type < PERF_SNAPSHOT_RECORD_TYPE_CNT);
      assert (visibility >= PERF_SNAPSHOT_INVISIBLE && visibility < PERF_SNAPSHOT_VISIBILITY_CNT);
      offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);
      assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);

      ADD_STATS_IN_ARRAY (stats, mvcc_snapshot_counters, offset, 1);
    }
}
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */

/*
 *   mnt_x_snapshot_acquire_time - 
 *   return: none
 */
void
mnt_x_snapshot_acquire_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);

      assert (amount > 0);

      offset = module;

      ADD_STATS_IN_ARRAY (stats, log_snapshot_time_counters, offset, amount);
    }
}

/*
 *   mnt_x_snapshot_retry_counters - 
 *   return: none
 */
void
mnt_x_snapshot_retry_counters (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);

      assert (amount > 0);

      offset = module;

      ADD_STATS_IN_ARRAY (stats, log_snapshot_retry_counters, offset, amount);
    }
}

/*
 *   mnt_x_tran_complete_time - 
 *   return: none
 */
void
mnt_x_tran_complete_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);

      assert (amount > 0);

      offset = module;

      ADD_STATS_IN_ARRAY (stats, log_tran_complete_time_counters, offset, amount);
    }
}

/*
 *   mnt_x_oldest_mvcc_acquire_time - 
 *   return: none
 */
void
mnt_x_oldest_mvcc_acquire_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);

      assert (amount > 0);

      offset = module;

      ADD_STATS_IN_ARRAY (stats, log_oldest_mvcc_time_counters, offset, amount);
    }
}

/*
 *   mnt_x_oldest_mvcc_retry_counters - 
 *   return: none
 */
void
mnt_x_oldest_mvcc_retry_counters (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats;
  int module;
  int offset;

  stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      module = perf_get_module_type (thread_p);

      assert (module >= PERF_MODULE_SYSTEM && module < PERF_MODULE_CNT);

      assert (amount > 0);

      offset = module;

      ADD_STATS_IN_ARRAY (stats, log_oldest_mvcc_retry_counters, offset, amount);
    }
}

void
mnt_x_heap_home_inserts (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_inserts, 1);
    }
}

void
mnt_x_heap_big_inserts (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_big_inserts, 1);
    }
}

void
mnt_x_heap_assign_inserts (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_assign_inserts, 1);
    }
}

void
mnt_x_heap_home_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_deletes, 1);
    }
}

void
mnt_x_heap_home_mvcc_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_mvcc_deletes, 1);
    }
}

void
mnt_x_heap_home_to_rel_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_to_rel_deletes, 1);
    }
}

void
mnt_x_heap_home_to_big_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_to_big_deletes, 1);
    }
}

void
mnt_x_heap_rel_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_deletes, 1);
    }
}

void
mnt_x_heap_rel_mvcc_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_mvcc_deletes, 1);
    }
}

void
mnt_x_heap_rel_to_home_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_to_home_deletes, 1);
    }
}

void
mnt_x_heap_rel_to_big_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_to_big_deletes, 1);
    }
}

void
mnt_x_heap_rel_to_rel_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_to_rel_deletes, 1);
    }
}

void
mnt_x_heap_big_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_big_deletes, 1);
    }
}

void
mnt_x_heap_big_mvcc_deletes (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_big_mvcc_deletes, 1);
    }
}

void
mnt_x_heap_new_ver_inserts (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_new_ver_inserts, 1);
    }
}

void
mnt_x_heap_home_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_updates, 1);
    }
}

void
mnt_x_heap_home_to_rel_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_to_rel_updates, 1);
    }
}

void
mnt_x_heap_home_to_big_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_to_big_updates, 1);
    }
}

void
mnt_x_heap_rel_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_updates, 1);
    }
}

void
mnt_x_heap_rel_to_home_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_to_home_updates, 1);
    }
}

void
mnt_x_heap_rel_to_rel_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_to_rel_updates, 1);
    }
}

void
mnt_x_heap_rel_to_big_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_to_big_updates, 1);
    }
}

void
mnt_x_heap_big_updates (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_big_updates, 1);
    }
}

void
mnt_x_heap_home_vacuums (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_home_vacuums, 1);
    }
}

void
mnt_x_heap_big_vacuums (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_big_vacuums, 1);
    }
}

void
mnt_x_heap_rel_vacuums (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_rel_vacuums, 1);
    }
}

void
mnt_x_heap_insid_vacuums (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_insid_vacuums, 1);
    }
}

void
mnt_x_heap_remove_vacuums (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_remove_vacuums, 1);
    }
}

void
mnt_x_heap_insert_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_insert_prepare, amount);
    }
}

void
mnt_x_heap_insert_execute_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_insert_execute, amount);
    }
}

void
mnt_x_heap_insert_log_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_insert_log, amount);
    }
}

void
mnt_x_heap_delete_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_delete_prepare, amount);
    }
}

void
mnt_x_heap_delete_execute_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_delete_execute, amount);
    }
}

void
mnt_x_heap_delete_log_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_delete_log, amount);
    }
}

void
mnt_x_heap_update_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_update_prepare, amount);
    }
}

void
mnt_x_heap_update_execute_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_update_execute, amount);
    }
}

void
mnt_x_heap_update_log_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_update_log, amount);
    }
}

void
mnt_x_heap_vacuum_prepare_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_vacuum_prepare, amount);
    }
}

void
mnt_x_heap_vacuum_execute_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_vacuum_execute, amount);
    }
}

void
mnt_x_heap_vacuum_log_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, heap_vacuum_log, amount);
    }
}

void
mnt_x_bt_find_unique_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_find_unique, amount);
      ADD_STATS (stats, bt_find_unique_cnt, 1);
    }
}

void
mnt_x_bt_range_search_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_range_search, amount);
      ADD_STATS (stats, bt_range_search_cnt, 1);
    }
}

void
mnt_x_bt_insert_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_insert, amount);
      ADD_STATS (stats, bt_insert_cnt, 1);
    }
}

void
mnt_x_bt_delete_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_delete, amount);
      ADD_STATS (stats, bt_delete_cnt, 1);
    }
}

void
mnt_x_bt_mvcc_delete_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_mvcc_delete, amount);
      ADD_STATS (stats, bt_mvcc_delete_cnt, 1);
    }
}

void
mnt_x_bt_mark_delete_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_mark_delete, amount);
      ADD_STATS (stats, bt_mark_delete_cnt, 1);
    }
}

void
mnt_x_bt_update_sk_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_update_sk, amount);
      ADD_STATS (stats, bt_update_sk_cnt, 1);
    }
}

void
mnt_x_bt_undo_insert_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_undo_insert, amount);
      ADD_STATS (stats, bt_undo_insert_cnt, 1);
    }
}

void
mnt_x_bt_undo_delete_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_undo_delete, amount);
      ADD_STATS (stats, bt_undo_delete_cnt, 1);
    }
}

void
mnt_x_bt_undo_mvcc_delete_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_undo_mvcc_delete, amount);
      ADD_STATS (stats, bt_undo_mvcc_delete_cnt, 1);
    }
}

void
mnt_x_bt_undo_update_sk_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_undo_update_sk, amount);
      ADD_STATS (stats, bt_undo_update_sk_cnt, 1);
    }
}

void
mnt_x_bt_vacuum_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_vacuum, amount);
      ADD_STATS (stats, bt_vacuum_cnt, 1);
    }
}

void
mnt_x_bt_vacuum_insid_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_vacuum_insid, amount);
      ADD_STATS (stats, bt_vacuum_insid_cnt, 1);
    }
}

void
mnt_x_bt_vacuum_update_sk_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_vacuum_update_sk, amount);
      ADD_STATS (stats, bt_vacuum_update_sk_cnt, 1);
    }
}

void
mnt_x_bt_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
    }
}

void
mnt_x_bt_find_unique_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_find_unique_traverse, amount);
    }
}

void
mnt_x_bt_range_search_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_range_search_traverse, amount);
    }
}

void
mnt_x_bt_insert_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_insert_traverse, amount);
    }
}

void
mnt_x_bt_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_delete_traverse, amount);
    }
}

void
mnt_x_bt_mvcc_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_mvcc_delete_traverse, amount);
    }
}

void
mnt_x_bt_mark_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_mark_delete_traverse, amount);
    }
}

void
mnt_x_bt_update_sk_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_update_sk_traverse, amount);
    }
}

void
mnt_x_bt_undo_insert_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_undo_insert_traverse, amount);
    }
}

void
mnt_x_bt_undo_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_undo_delete_traverse, amount);
    }
}

void
mnt_x_bt_undo_mvcc_delete_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_undo_mvcc_delete_traverse, amount);
    }
}

void
mnt_x_bt_undo_update_sk_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_undo_update_sk_traverse, amount);
    }
}

void
mnt_x_bt_vacuum_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_vacuum_traverse, amount);
    }
}

void
mnt_x_bt_vacuum_insid_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_vacuum_insid_traverse, amount);
    }
}

void
mnt_x_bt_vacuum_update_sk_traverse_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_traverse, amount);
      ADD_STATS (stats, bt_vacuum_update_sk_traverse, amount);
    }
}

void
mnt_x_bt_fix_ovf_oids_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_fix_ovf_oids, amount);
      ADD_STATS (stats, bt_fix_ovf_oids_cnt, 1);
    }
}

void
mnt_x_bt_unique_rlocks_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_unique_rlocks, amount);
      ADD_STATS (stats, bt_unique_rlocks_cnt, 1);
    }
}

void
mnt_x_bt_unique_wlocks_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, bt_unique_wlocks, amount);
      ADD_STATS (stats, bt_unique_wlocks_cnt, 1);
    }
}

void
mnt_x_vac_master_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, vac_master, amount);
    }
}

void
mnt_x_vac_worker_process_log_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, vac_worker_process_log, amount);
    }
}

void
mnt_x_vac_worker_execute_time (THREAD_ENTRY * thread_p, UINT64 amount)
{
  MNT_SERVER_EXEC_STATS *stats = mnt_server_get_stats (thread_p);
  if (stats != NULL)
    {
      ADD_STATS (stats, vac_worker_execute, amount);
    }
}

#endif /* SERVER_MODE || SA_MODE */

/*
 *   mnt_calc_diff_stats -
 *   return:
 *   stats_diff :
 *   new_stats :
 *   old_stats :
 */
int
mnt_calc_diff_stats (MNT_SERVER_EXEC_STATS * stats_diff, MNT_SERVER_EXEC_STATS * new_stats,
		     MNT_SERVER_EXEC_STATS * old_stats)
{
  assert (stats_diff && new_stats && old_stats);

  if (!stats_diff || !new_stats || !old_stats)
    {
      return ER_FAILED;
    }

  MNT_CALC_STATS (stats_diff, new_stats, old_stats, CALC_STAT_DIFF);

  mnt_server_calc_stats (stats_diff);

  return NO_ERROR;
}

/*
 * mnt_server_dump_stats_to_buffer -
 *   return: none
 *   stats(in) server statistics to print
 *   buffer(in):
 *   buf_size(in):
 *   substr(in):
 */
void
mnt_server_dump_stats_to_buffer (const MNT_SERVER_EXEC_STATS * stats, char *buffer, int buf_size, const char *substr)
{
  unsigned int i;
  int ret;
  UINT64 *stats_ptr;
  int remained_size;
  const char *s;
  char *p;
  bool computed_stats;

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
  for (i = 0; i < MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS; i++)
    {
      if (i == MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS - MNT_COUNT_OF_SERVER_EXEC_CALC_STATS)
	{
	  ret = snprintf (p, remained_size, "\n *** OTHER STATISTICS *** \n");
	  remained_size -= ret;

	  if (remained_size <= 0)
	    {
	      return;
	    }
	}

      computed_stats = (i < MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS - MNT_COUNT_OF_SERVER_EXEC_CALC_STATS) ? false : true;

      if (substr != NULL && computed_stats == false)
	{
	  s = strstr (mnt_Stats_name[i], substr);
	}
      else
	{
	  s = mnt_Stats_name[i];
	}

      if (s)
	{
	  if (computed_stats == false)
	    {
	      ret =
		snprintf (p, remained_size, "%-29s = %10llu\n", mnt_Stats_name[i], (unsigned long long) stats_ptr[i]);
	    }
	  else
	    {
	      ret = snprintf (p, remained_size, "%-29s = %10.2f\n", mnt_Stats_name[i], (float) stats_ptr[i] / 100);
	    }
	  remained_size -= ret;
	  p += ret;
	  if (remained_size <= 0)
	    {
	      return;
	    }
	}
    }

  for (i = MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS; i < MNT_SERVER_EXEC_STATS_COUNT && remained_size > 0; i++)
    {
      if (substr != NULL)
	{
	  s = strstr (mnt_Stats_name[i], substr);
	}
      else
	{
	  s = mnt_Stats_name[i];
	}
      if (s == NULL)
	{
	  continue;
	}

      ret = snprintf (p, remained_size, "%s:\n", mnt_Stats_name[i]);
      remained_size -= ret;
      if (remained_size <= 0)
	{
	  return;
	}

      switch (i)
	{
	case MNT_SERVER_PBX_FIX_STAT_POSITION:
	  perf_stat_dump_fix_page_array_stat (stats->pbx_fix_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_PBX_UNFIX_STAT_POSITION:
	  perf_stat_dump_unfix_page_array_stat (stats->pbx_unfix_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_PBX_LOCK_TIME_STAT_POSITION:
	  perf_stat_dump_page_lock_time_array_stat (stats->pbx_lock_time_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_PBX_HOLD_TIME_STAT_POSITION:
	  perf_stat_dump_page_hold_time_array_stat (stats->pbx_hold_time_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_PBX_FIX_TIME_STAT_POSITION:
	  perf_stat_dump_page_fix_time_array_stat (stats->pbx_fix_time_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_PROMOTE_STAT_POSITION:
	  perf_stat_dump_promote_page_array_stat (stats->pbx_promote_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_PROMOTE_TIME_STAT_POSITION:
	  perf_stat_dump_promote_page_array_stat (stats->pbx_promote_time_counters, p, &remained_size, NULL, false);
	  break;
#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
	case MNT_SERVER_MVCC_SNAPSHOT_STAT_POSITION:
	  perf_stat_dump_mvcc_snapshot_array_stat (stats->mvcc_snapshot_counters, p, &remained_size, NULL, false);
	  break;
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */
#if defined(PERF_ENABLE_LOCK_OBJECT_STAT)
	case MNT_SERVER_OBJ_LOCK_STAT_POSITION:
	  perf_stat_dump_obj_lock_array_stat (stats->obj_lock_time_counters, p, &remained_size, NULL, false);
	  break;
#endif /* PERF_ENABLE_LOCK_OBJECT_STAT */
	case MNT_SERVER_SNAPSHOT_TIME_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_snapshot_time_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_SNAPSHOT_RETRY_CNT_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_snapshot_retry_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_TRAN_COMPLETE_TIME_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_tran_complete_time_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_OLDEST_MVCC_TIME_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_oldest_mvcc_time_counters, p, &remained_size, NULL, false);
	  break;
	case MNT_SERVER_OLDEST_MVCC_RETRY_CNT_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_oldest_mvcc_retry_counters, p, &remained_size, NULL, false);
	  break;
	default:
	  break;
	}
    }

  buffer[buf_size - 1] = '\0';
}

/*
 * mnt_server_dump_stats - Print the given server statistics
 *   return: none
 *   stats(in) server statistics to print
 *   stream(in): if NULL is given, stdout is used
 */
void
mnt_server_dump_stats (const MNT_SERVER_EXEC_STATS * stats, FILE * stream, const char *substr)
{
  unsigned int i;
  UINT64 *stats_ptr;
  const char *s;
  bool computed_stats;

  if (stream == NULL)
    {
      stream = stdout;
    }

  fprintf (stream, "\n *** SERVER EXECUTION STATISTICS *** \n");

  stats_ptr = (UINT64 *) stats;
  for (i = 0; i < MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS; i++)
    {
      if (i == MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS - MNT_COUNT_OF_SERVER_EXEC_CALC_STATS)
	{
	  fprintf (stream, "\n *** OTHER STATISTICS *** \n");
	}

      computed_stats = (i < MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS - MNT_COUNT_OF_SERVER_EXEC_CALC_STATS) ? false : true;

      if (substr != NULL && computed_stats == false)
	{
	  s = strstr (mnt_Stats_name[i], substr);
	}
      else
	{
	  s = mnt_Stats_name[i];
	}

      if (s)
	{
	  if (computed_stats == false)
	    {
	      fprintf (stream, "%-29s = %10llu\n", mnt_Stats_name[i], (unsigned long long) stats_ptr[i]);
	    }
	  else
	    {
	      fprintf (stream, "%-29s = %10.2f\n", mnt_Stats_name[i], (float) stats_ptr[i] / 100);
	    }
	}
    }

  for (i = MNT_COUNT_OF_SERVER_EXEC_SINGLE_STATS; i < MNT_SERVER_EXEC_STATS_COUNT; i++)
    {
      if (substr != NULL)
	{
	  s = strstr (mnt_Stats_name[i], substr);
	}
      else
	{
	  s = mnt_Stats_name[i];
	}
      if (s == NULL)
	{
	  continue;
	}

      fprintf (stream, "%s:\n", mnt_Stats_name[i]);

      switch (i)
	{
	case MNT_SERVER_PBX_FIX_STAT_POSITION:
	  perf_stat_dump_fix_page_array_stat (stats->pbx_fix_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_PBX_UNFIX_STAT_POSITION:
	  perf_stat_dump_unfix_page_array_stat (stats->pbx_unfix_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_PBX_LOCK_TIME_STAT_POSITION:
	  perf_stat_dump_page_lock_time_array_stat (stats->pbx_lock_time_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_PBX_HOLD_TIME_STAT_POSITION:
	  perf_stat_dump_page_hold_time_array_stat (stats->pbx_hold_time_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_PBX_FIX_TIME_STAT_POSITION:
	  perf_stat_dump_page_fix_time_array_stat (stats->pbx_fix_time_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_PROMOTE_STAT_POSITION:
	  perf_stat_dump_promote_page_array_stat (stats->pbx_promote_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_PROMOTE_TIME_STAT_POSITION:
	  perf_stat_dump_promote_page_array_stat (stats->pbx_promote_time_counters, NULL, NULL, stream, false);
	  break;
#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
	case MNT_SERVER_MVCC_SNAPSHOT_STAT_POSITION:
	  perf_stat_dump_mvcc_snapshot_array_stat (stats->mvcc_snapshot_counters, NULL, NULL, stream, false);
	  break;
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */
#if defined(PERF_ENABLE_LOCK_OBJECT_STAT)
	case MNT_SERVER_OBJ_LOCK_STAT_POSITION:
	  perf_stat_dump_obj_lock_array_stat (stats->obj_lock_time_counters, NULL, NULL, stream, false);
	  break;
#endif /* PERF_ENABLE_LOCK_OBJECT_STAT */
	case MNT_SERVER_SNAPSHOT_TIME_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_snapshot_time_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_SNAPSHOT_RETRY_CNT_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_snapshot_retry_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_TRAN_COMPLETE_TIME_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_tran_complete_time_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_OLDEST_MVCC_TIME_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_oldest_mvcc_time_counters, NULL, NULL, stream, false);
	  break;
	case MNT_SERVER_OLDEST_MVCC_RETRY_CNT_STAT_POSITION:
	  perf_stat_dump_snapshot_array_stat (stats->log_oldest_mvcc_retry_counters, NULL, NULL, stream, false);
	  break;
	default:
	  break;
	}
    }
}

/*
 * mnt_get_current_times - Get current CPU and elapsed times
 *   return:
 *   cpu_user_time(out):
 *   cpu_sys_time(out):
 *   elapsed_time(out):
 *
 * Note:
 */
void
mnt_get_current_times (time_t * cpu_user_time, time_t * cpu_sys_time, time_t * elapsed_time)
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
 * mnt_server_calc_stats - Do post processing of server statistics
 *   return: none
 *   stats(in/out): server statistics block to be processed
 */
static void
mnt_server_calc_stats (MNT_SERVER_EXEC_STATS * stats)
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
  UINT64 *counter;
  UINT64 total_unfix_vacuum = 0;
  UINT64 total_unfix_vacuum_dirty = 0;
  UINT64 total_unfix = 0;
  UINT64 total_fix_vacuum = 0;
  UINT64 total_fix_vacuum_hit = 0;
  UINT64 fix_time_usec = 0;
  UINT64 lock_time_usec = 0;
  UINT64 hold_time_usec = 0;
  UINT64 total_promote_time = 0;

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
		      counter = stats->pbx_unfix_counters + offset;

		      total_unfix += *counter;
		      if (module == PERF_MODULE_VACUUM)
			{
			  total_unfix_vacuum += *counter;
			  if (holder_dirty == 1)
			    {
			      total_unfix_vacuum_dirty += *counter;
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
		  counter = stats->pbx_hold_time_counters + offset;

		  if (page_type != PAGE_LOG && *counter > 0)
		    {
		      hold_time_usec += *counter;
		    }

		  for (cond_type = PERF_CONDITIONAL_FIX; cond_type < PERF_CONDITIONAL_FIX_CNT; cond_type++)
		    {
		      offset = PERF_PAGE_FIX_TIME_OFFSET (module, page_type, page_found_mode, holder_latch, cond_type);
		      assert (offset < PERF_PAGE_FIX_TIME_COUNTERS);
		      counter = stats->pbx_fix_time_counters + offset;
		      /* do not include fix time of log pages */
		      if (page_type != PAGE_LOG && *counter > 0)
			{
			  fix_time_usec += *counter;
			}

		      offset = PERF_PAGE_LOCK_TIME_OFFSET (module, page_type, page_found_mode, holder_latch, cond_type);
		      assert (offset < PERF_PAGE_LOCK_TIME_COUNTERS);
		      counter = stats->pbx_lock_time_counters + offset;

		      if (page_type != PAGE_LOG && *counter > 0)
			{
			  lock_time_usec += *counter;
			}

		      if (module == PERF_MODULE_VACUUM && page_found_mode != PERF_PAGE_MODE_NEW_LOCK_WAIT
			  && page_found_mode != PERF_PAGE_MODE_NEW_NO_WAIT)
			{
			  offset =
			    PERF_PAGE_FIX_STAT_OFFSET (module, page_type, page_found_mode, holder_latch, cond_type);

			  assert (offset < PERF_PAGE_FIX_COUNTERS);
			  counter = stats->pbx_fix_counters + offset;

			  if (module == PERF_MODULE_VACUUM)
			    {
			      total_fix_vacuum += *counter;
			      if (page_found_mode == PERF_PAGE_MODE_OLD_IN_BUFFER)
				{
				  total_fix_vacuum_hit += *counter;
				}
			    }
			}
		    }
		}
	    }
	}
    }

  stats->pb_vacuum_efficiency =
    (total_unfix_vacuum == 0) ? 0 : (total_unfix_vacuum_dirty) * 100 * 100 / total_unfix_vacuum;

  stats->pb_vacuum_fetch_ratio = (total_unfix == 0) ? 0 : (total_unfix_vacuum) * 100 * 100 / total_unfix;

  stats->vacuum_data_hit_ratio = (total_fix_vacuum == 0) ? 0 : (total_fix_vacuum_hit) * 100 * 100 / total_fix_vacuum;

  stats->pb_hit_ratio =
    (stats->pb_num_fetches ==
     0) ? 0 : (stats->pb_num_fetches - stats->pb_num_ioreads) * 100 * 100 / stats->pb_num_fetches;

  stats->log_hit_ratio =
    (stats->log_num_fetches ==
     0) ? 0 : (stats->log_num_fetches - stats->log_num_fetch_ioreads) * 100 * 100 / stats->log_num_fetches;

  stats->pb_page_lock_acquire_time_10usec = 100 * lock_time_usec / 1000;
  stats->pb_page_hold_acquire_time_10usec = 100 * hold_time_usec / 1000;
  stats->pb_page_fix_acquire_time_10usec = 100 * fix_time_usec / 1000;

  stats->pb_page_allocate_time_ratio =
    (stats->pb_page_fix_acquire_time_10usec ==
     0) ? 0 : ((stats->pb_page_fix_acquire_time_10usec - stats->pb_page_hold_acquire_time_10usec -
		stats->pb_page_lock_acquire_time_10usec) * 100 * 100 / stats->pb_page_fix_acquire_time_10usec);

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

		      counter = stats->pbx_promote_time_counters + offset;
		      if (*counter)
			{
			  total_promote_time += *counter;
			}

		      counter = stats->pbx_promote_counters + offset;
		      if (*counter)
			{
			  if (success)
			    {
			      stats->pb_page_promote_success += *counter;
			    }
			  else
			    {
			      stats->pb_page_promote_failed += *counter;
			    }
			}
		    }
		}
	    }
	}
    }

  stats->pb_page_promote_total_time_10usec = 100 * total_promote_time / 1000;
  stats->pb_page_promote_success *= 100;
  stats->pb_page_promote_failed *= 100;

#if defined (SERVER_MODE)
  pgbuf_peek_stats (&stats->pb_fixed_cnt, &stats->pb_dirty_cnt, &stats->pb_lru1_cnt, &stats->pb_lru2_cnt,
		    &stats->pb_ain_cnt, &stats->pb_avoid_dealloc_cnt, &stats->pb_avoid_victim_cnt,
		    &stats->pb_victim_cand_cnt);
#endif
}


/*
 * perf_stat_module_name () -
 */
static const char *
perf_stat_module_name (const int module)
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


/*
 * perf_get_module_type () -
 */
STATIC_INLINE int
perf_get_module_type (THREAD_ENTRY * thread_p)
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
 * perf_stat_page_type_name () -
 */
static const char *
perf_stat_page_type_name (const int page_type)
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
    case PERF_PAGE_XASL:
      return "PAGE_XASL";
    case PERF_PAGE_QRESULT:
      return "PAGE_QRESULT";
    case PERF_PAGE_EHASH:
      return "PAGE_EHASH";
    case PERF_PAGE_LARGEOBJ:
      return "PAGE_LARGEOBJ";
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
#if defined(PERF_ENABLE_DETAILED_BTREE_PAGE_STAT)
    case PERF_PAGE_BTREE_ROOT:
      return "PAGE_BTREE_R";
    case PERF_PAGE_BTREE_OVF:
      return "PAGE_BTREE_O";
    case PERF_PAGE_BTREE_LEAF:
      return "PAGE_BTREE_L";
    case PERF_PAGE_BTREE_NONLEAF:
      return "PAGE_BTREE_N";
#endif /* PERF_ENABLE_DETAILED_BTREE_PAGE_STAT */
    default:
      break;
    }
  return "ERROR";
}

/*
 * perf_stat_page_mode_name () -
 */
static const char *
perf_stat_page_mode_name (const int page_mode)
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
 * perf_stat_holder_latch_name () -
 */
static const char *
perf_stat_holder_latch_name (const int holder_latch)
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
 * perf_stat_cond_type_name () -
 */
static const char *
perf_stat_cond_type_name (const int cond_type)
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

#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
/*
 * perf_stat_snapshot_name () -
 */
static const char *
perf_stat_snapshot_name (const int snapshot)
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
 * perf_stat_snapshot_record_type () -
 */
static const char *
perf_stat_snapshot_record_type (const int rec_type)
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
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */

#if defined(PERF_ENABLE_LOCK_OBJECT_STAT)
static const char *
perf_stat_lock_mode_name (const int lock_mode)
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
#endif /* PERF_ENABLE_LOCK_OBJECT_STAT */

/*
 * perf_stat_cond_type_name () -
 */
static const char *
perf_stat_promote_cond_name (const int cond_type)
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
 * perf_stat_dump_fix_page_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_fix_page_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
				    bool print_zero_counters)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int cond_type;
  int offset;
  const UINT64 *counter;
  int ret;

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
		      counter = stats_ptr + offset;
		      if (*counter == 0 && print_zero_counters == false)
			{
			  continue;
			}

		      if (s != NULL)
			{
			  assert (remaining_size != NULL);

			  ret =
			    snprintf (s, *remaining_size, "%-6s,%-14s,%-18s,%-5s,%-11s = %10llu\n",
				      perf_stat_module_name (module), perf_stat_page_type_name (page_type),
				      perf_stat_page_mode_name (page_mode), perf_stat_holder_latch_name (latch_mode),
				      perf_stat_cond_type_name (cond_type), (long long unsigned int) *counter);
			  *remaining_size -= ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		      else
			{
			  assert (stream != NULL);

			  fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s = %10llu\n", perf_stat_module_name (module),
				   perf_stat_page_type_name (page_type), perf_stat_page_mode_name (page_mode),
				   perf_stat_holder_latch_name (latch_mode), perf_stat_cond_type_name (cond_type),
				   (long long unsigned int) *counter);
			}
		    }
		}
	    }
	}
    }
}

/*
 * perf_stat_dump_promote_page_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_promote_page_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
					bool print_zero_counters)
{
  int module;
  int page_type;
  int promote_cond;
  int holder_latch;
  int success;
  int offset;
  const UINT64 *counter;
  int ret;

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
		      counter = stats_ptr + offset;
		      if (*counter == 0 && print_zero_counters == false)
			{
			  continue;
			}

		      if (s != NULL)
			{
			  assert (remaining_size != NULL);

			  ret =
			    snprintf (s, *remaining_size, "%-6s,%-14s,%-13s,%-5s,%-7s = %10llu\n",
				      perf_stat_module_name (module), perf_stat_page_type_name (page_type),
				      perf_stat_promote_cond_name (promote_cond),
				      perf_stat_holder_latch_name (holder_latch), (success ? "SUCCESS" : "FAILED"),
				      (long long unsigned int) *counter);
			  *remaining_size -= ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		      else
			{
			  assert (stream != NULL);

			  fprintf (stream, "%-6s,%-14s,%-13s,%-5s,%-7s = %10llu\n", perf_stat_module_name (module),
				   perf_stat_page_type_name (page_type), perf_stat_promote_cond_name (promote_cond),
				   perf_stat_holder_latch_name (holder_latch), (success ? "SUCCESS" : "FAILED"),
				   (long long unsigned int) *counter);
			}
		    }
		}
	    }
	}
    }
}

/*
 * perf_stat_dump_unfix_page_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_unfix_page_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
				      bool print_zero_counters)
{
  int module;
  int page_type;
  int buf_dirty;
  int holder_dirty;
  int holder_latch;
  int offset;
  const UINT64 *counter;
  int ret;

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
		      counter = stats_ptr + offset;
		      if (*counter == 0 && print_zero_counters == false)
			{
			  continue;
			}

		      if (s != NULL)
			{
			  assert (remaining_size != NULL);

			  ret =
			    snprintf (s, *remaining_size, "%-6s,%-14s,%-13s,%-16s,%-5s = %10llu\n",
				      perf_stat_module_name (module), perf_stat_page_type_name (page_type),
				      buf_dirty ? "BUF_DIRTY" : "BUF_NON_DIRTY",
				      holder_dirty ? "HOLDER_DIRTY" : "HOLDER_NON_DIRTY",
				      perf_stat_holder_latch_name (holder_latch), (long long unsigned int) *counter);
			  *remaining_size -= ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		      else
			{
			  assert (stream != NULL);

			  fprintf (stream, "%-6s,%-14s,%-13s,%-16s,%-5s = %10llu\n", perf_stat_module_name (module),
				   perf_stat_page_type_name (page_type), buf_dirty ? "BUF_DIRTY" : "BUF_NON_DIRTY",
				   holder_dirty ? "HOLDER_DIRTY" : "HOLDER_NON_DIRTY",
				   perf_stat_holder_latch_name (holder_latch), (long long unsigned int) *counter);
			}
		    }
		}
	    }
	}
    }
}

/*
 * perf_stat_dump_page_lock_time_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_page_lock_time_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
					  bool print_zero_counters)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int cond_type;
  int offset;
  const UINT64 *counter;
  int ret;

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
		      counter = stats_ptr + offset;
		      if (*counter == 0 && print_zero_counters == false)
			{
			  continue;
			}

		      if (s != NULL)
			{
			  assert (remaining_size != NULL);

			  ret =
			    snprintf (s, *remaining_size, "%-6s,%-14s,%-18s,%-5s,%-11s = %16llu\n",
				      perf_stat_module_name (module), perf_stat_page_type_name (page_type),
				      perf_stat_page_mode_name (page_mode), perf_stat_holder_latch_name (latch_mode),
				      perf_stat_cond_type_name (cond_type), (long long unsigned int) *counter);
			  *remaining_size -= ret;
			  if (*remaining_size <= 0)
			    {
			      return;
			    }
			}
		      else
			{
			  assert (stream != NULL);

			  fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s = %16llu\n", perf_stat_module_name (module),
				   perf_stat_page_type_name (page_type), perf_stat_page_mode_name (page_mode),
				   perf_stat_holder_latch_name (latch_mode), perf_stat_cond_type_name (cond_type),
				   (long long unsigned int) *counter);
			}
		    }
		}
	    }
	}
    }
}

/*
 * perf_stat_dump_page_hold_time_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_page_hold_time_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
					  bool print_zero_counters)
{
  int module;
  int page_type;
  int page_mode;
  int latch_mode;
  int offset;
  const UINT64 *counter;
  int ret;

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
		  counter = stats_ptr + offset;
		  if (*counter == 0 && print_zero_counters == false)
		    {
		      continue;
		    }

		  if (s != NULL)
		    {
		      assert (remaining_size != NULL);

		      ret =
			snprintf (s, *remaining_size, "%-6s,%-14s,%-18s,%-5s = %16llu\n",
				  perf_stat_module_name (module), perf_stat_page_type_name (page_type),
				  perf_stat_page_mode_name (page_mode), perf_stat_holder_latch_name (latch_mode),
				  (long long unsigned int) *counter);
		      *remaining_size -= ret;
		      if (*remaining_size <= 0)
			{
			  return;
			}
		    }
		  else
		    {
		      assert (stream != NULL);

		      fprintf (stream, "%-6s,%-14s,%-18s,%-5s = %16llu\n", perf_stat_module_name (module),
			       perf_stat_page_type_name (page_type), perf_stat_page_mode_name (page_mode),
			       perf_stat_holder_latch_name (latch_mode), (long long unsigned int) *counter);
		    }
		}
	    }
	}
    }
}

/*
 * perf_stat_dump_page_fix_time_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_page_fix_time_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
					 bool print_zero_counters)
{
  /* the counters partitioning match with page fix statistics */
  perf_stat_dump_page_lock_time_array_stat (stats_ptr, s, remaining_size, stream, print_zero_counters);
}

#if defined(PERF_ENABLE_MVCC_SNAPSHOT_STAT)
/*
 * perf_stat_dump_mvcc_snapshot_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_mvcc_snapshot_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
					 bool print_zero_counters)
{
  PERF_SNAPSHOT_TYPE snapshot;
  PERF_SNAPSHOT_RECORD_TYPE rec_type;
  PERF_SNAPSHOT_VISIBILITY visibility;
  int offset;
  const UINT64 *counter;
  int ret;

  for (snapshot = PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < PERF_SNAPSHOT_CNT; snapshot++)
    {
      for (rec_type = PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED; rec_type < PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
	{
	  for (visibility = PERF_SNAPSHOT_INVISIBLE; visibility < PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
	    {
	      offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);

	      assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);
	      counter = stats_ptr + offset;
	      if (*counter == 0 && print_zero_counters == false)
		{
		  continue;
		}

	      if (s != NULL)
		{
		  assert (remaining_size != NULL);

		  ret =
		    snprintf (s, *remaining_size, "%-8s,%-18s,%-9s = %16llu\n", perf_stat_snapshot_name (snapshot),
			      perf_stat_snapshot_record_type (rec_type),
			      (visibility == PERF_SNAPSHOT_INVISIBLE) ? "INVISIBLE" : "VISIBLE",
			      (long long unsigned int) *counter);
		  *remaining_size -= ret;
		  if (*remaining_size <= 0)
		    {
		      return;
		    }
		}
	      else
		{
		  assert (stream != NULL);

		  fprintf (stream, "%-8s,%-18s,%-9s = %16llu\n", perf_stat_snapshot_name (snapshot),
			   perf_stat_snapshot_record_type (rec_type),
			   (visibility == PERF_SNAPSHOT_INVISIBLE) ? "INVISIBLE" : "VISIBLE",
			   (long long unsigned int) *counter);
		}
	    }
	}
    }
}
#endif /* PERF_ENABLE_MVCC_SNAPSHOT_STAT */


#if defined(PERF_ENABLE_LOCK_OBJECT_STAT)
/*
 * perf_stat_dump_obj_lock_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_obj_lock_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
				    bool print_zero_counters)
{
  int module;
  int lock_mode;
  int offset;
  const UINT64 *counter;
  int ret;

  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      for (lock_mode = NA_LOCK; lock_mode <= SCH_M_LOCK; lock_mode++)
	{
	  offset = PERF_OBJ_LOCK_STAT_OFFSET (module, lock_mode);

	  assert (offset < PERF_OBJ_LOCK_STAT_COUNTERS);
	  counter = stats_ptr + offset;
	  if (*counter == 0 && print_zero_counters == false)
	    {
	      continue;
	    }

	  if (s != NULL)
	    {
	      assert (remaining_size != NULL);

	      ret =
		snprintf (s, *remaining_size, "%-6s,%-10s = %16llu\n", perf_stat_module_name (module),
			  perf_stat_lock_mode_name (lock_mode), (long long unsigned int) *counter);
	      *remaining_size -= ret;
	      if (*remaining_size <= 0)
		{
		  return;
		}
	    }
	  else
	    {
	      assert (stream != NULL);

	      fprintf (stream, "%-6s,%-10s = %16llu\n", perf_stat_module_name (module),
		       perf_stat_lock_mode_name (lock_mode), (long long unsigned int) *counter);
	    }
	}
    }
}
#endif /* PERF_ENABLE_LOCK_OBJECT_STAT */

/*
 * perf_stat_dump_snapshot_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaing size in string s (NULL if not used)
 * stream(in): output file
 * print_zero_counters(in): true if counters with zero values should be printed
 * 
 */
static void
perf_stat_dump_snapshot_array_stat (const UINT64 * stats_ptr, char *s, int *remaining_size, FILE * stream,
				    bool print_zero_counters)
{
  int module;
  int offset;
  const UINT64 *counter;
  int ret;

  for (module = PERF_MODULE_SYSTEM; module < PERF_MODULE_CNT; module++)
    {
      offset = module;

      assert (offset < PERF_MODULE_CNT);
      counter = stats_ptr + offset;
      if (*counter == 0 && print_zero_counters == false)
	{
	  continue;
	}

      if (s != NULL)
	{
	  assert (remaining_size != NULL);
	  ret =
	    snprintf (s, *remaining_size, "%-6s = %16llu\n", perf_stat_module_name (module),
		      (long long unsigned int) *counter);
	  *remaining_size -= ret;
	  if (*remaining_size <= 0)
	    {
	      return;
	    }
	}
      else
	{
	  assert (stream != NULL);
	  fprintf (stream, "%-6s = %16llu\n", perf_stat_module_name (module), (long long unsigned int) *counter);
	}
    }
}
