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
 * perf_metadata.c - Meta-data used for performance statistics monitoring and processing
 */

#include <assert.h>
#include "perf_metadata.h"
#include "porting.h"
#include <stdarg.h>
#include <error_code.h>

#if defined (SERVER_MODE) || defined (SA_MODE) || defined (CS_MODE)
#include "error_manager.h"
#else
#define er_set(...)
#endif

/************************************************************************/
/* start of macros and structures                                       */
/************************************************************************/

#define PSTAT_METADATA_INIT_SINGLE_ACC(id, name) { id, name, PSTAT_ACCUMULATE_SINGLE_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_SINGLE_PEEK(id, name) \
  { id, name, PSTAT_PEEK_SINGLE_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_COUNTER_TIMER(id, name) { id, name, PSTAT_COUNTER_TIMER_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_COMPUTED_RATIO(id, name) \
  { id, name, PSTAT_COMPUTED_RATIO_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_COMPLEX(id, name, md_complex) \
  { id, name, PSTAT_COMPLEX_VALUE, 0, 0, md_complex }
#define PERFMETA_VALNAME_MAX_SIZE 128

static int perfmeta_Stat_count;
static char (*pstat_Value_names)[PERFMETA_VALNAME_MAX_SIZE] = NULL;

const PERFBASE_DIM perfbase_Dim_module = {
  "MODULE",
  PERF_MODULE_CNT,
  {"SYSTEM", "WORKER", "VACUUM"},
};

const PERFBASE_DIM perfbase_Dim_page_type = {
  "PAGE_TYPE",
  PERF_PAGE_CNT,
  {"PAGE_UNKNOWN", "PAGE_FTAB", "PAGE_HEAP", "PAGE_VOLHEADER", "PAGE_VOLBITMAP", "PAGE_QRESULT", "PAGE_EHASH",
   "PAGE_OVERFLOW", "PAGE_AREA", "PAGE_CATALOG", "PAGE_BTREE", "PAGE_LOG", "PAGE_DROPPED", "PAGE_VACUUM_DATA",
   "PAGE_BTREE_R", "PAGE_BTREE_O", "PAGE_BTREE_L", "PAGE_BTREE_N"},
};

const PERFBASE_DIM perfbase_Dim_page_mode = {
  "PAGE_MODE",
  PERF_PAGE_MODE_CNT,
  {"OLD_WAIT", "OLD_NO_WAIT", "NEW_WAIT", "NEW_NO_WAIT", "OLD_PAGE_IN_PB"},
};

const PERFBASE_DIM perfbase_Dim_holder_latch = {
  "HOLDER_LATCH",
  PERF_HOLDER_LATCH_CNT,
  {"READ", "WRITE", "MIXED"},
};

const PERFBASE_DIM perfbase_Dim_cond_type = {
  "COND_TYPE",
  PERF_CONDITIONAL_FIX_CNT,
  {"COND", "UNCOND", "UNCOND_WAIT"},
};

const PERFBASE_DIM perfbase_Dim_snapshot = {
  "SNAPSHOT",
  PERF_SNAPSHOT_CNT,
  {"DELETE", "DIRTY", "SNAPSHOT", "VACUUM"},
};

const PERFBASE_DIM perfbase_Dim_snapshot_record_type = {
  "RECORD_TYPE",
  PERF_SNAPSHOT_RECORD_TYPE_CNT,
  {"INS_VACUUMED", "INS_CURR", "INS_OTHER", "INS_COMMITTED", "INS_COMMITTED_L", "INS_DELETED", "DELETED_CURR",
   "DELETED_OTHER", "DELETED_COMMITED", "DELETED_COMMITED_L"},
};

const PERFBASE_DIM perfbase_Dim_lock_mode = {
  "LOCK_MODE",
  PERF_LOCK_CNT,
  {"NA_LOCK", "INCON_2PL", "NULL_LOCK", "SCH_S_LOCK", "IS_LOCK", "S_LOCK", "IX_LOCK", "SIX_LOCK", "U_LOCK", "X_LOCK",
   "SCH_M_LOCK"},
};

const PERFBASE_DIM perfbase_Dim_promote_cond = {
  "PROMOTE_COND",
  2,
  {"ONLY_READER", "SHARED_READER"},
};

const PERFBASE_DIM perfbase_Dim_success = {
  "SUCCESS",
  2,
  {"SUCCESS", "FAILED"},
};

const PERFBASE_DIM perfbase_Dim_buf_dirty = {
  "BUF_DIRTY",
  2,
  {"BUF_NON_DIRTY", "BUF_DIRTY"},
};

const PERFBASE_DIM perfbase_Dim_holder_dirty = {
  "HOLDER_DIRTY",
  2,
  {"HOLDER_NON_DIRTY", "HOLDER_DIRTY"},
};

const PERFBASE_DIM perfbase_Dim_visibility = {
  "VISIBILITY",
  2,
  {"INVISIBLE", "VISIBLE"},
};

const PERFBASE_COMPLEX perfbase_Complex_page_fix = {
  5,
  {&perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch,
   &perfbase_Dim_cond_type},
};

PERFBASE_COMPLEX perfbase_Complex_page_promote = {
  5,
  {&perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_promote_cond, &perfbase_Dim_holder_latch,
   &perfbase_Dim_success},
};

PERFBASE_COMPLEX perfbase_Complex_page_promote_time = {
  5,
  {&perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_promote_cond, &perfbase_Dim_holder_latch,
   &perfbase_Dim_success},
};

PERFBASE_COMPLEX perfbase_Complex_page_unfix = {
  5,
  {&perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_buf_dirty, &perfbase_Dim_holder_dirty,
   &perfbase_Dim_holder_latch},
};

PERFBASE_COMPLEX perfbase_Complex_page_lock_acquire_time = {
  5,
  {&perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch,
   &perfbase_Dim_cond_type},
};

PERFBASE_COMPLEX perfbase_Complex_page_hold_acquire_time = {
  4,
  {&perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch}
};

PERFBASE_COMPLEX perfbase_Complex_page_fix_acquire_time = {
  5,
  {&perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch,
   &perfbase_Dim_cond_type},
};

PERFBASE_COMPLEX perfbase_Complex_Num_mvcc_snapshot = {
  3,
  {&perfbase_Dim_snapshot, &perfbase_Dim_snapshot_record_type, &perfbase_Dim_visibility}
};

PERFBASE_COMPLEX perfbase_Complex_Time_obj_lock_acquire_time = {
  1,
  {&perfbase_Dim_lock_mode}
};

typedef struct perfbase_complex_iterator PERFBASE_COMPLEX_ITERATOR;
struct perfbase_complex_iterator
{
  const PERFBASE_COMPLEX *complexp;
  PERFMETA_COMPLEX_CURSOR cursor;
};

PSTAT_METADATA pstat_Metadata[] = {
  /* Execution statistics for the file io */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_CREATES, "Num_file_creates"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_REMOVES, "Num_file_removes"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_IOREADS, "Num_file_ioreads"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_IOWRITES, "Num_file_iowrites"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_IOSYNCHES, "Num_file_iosynches"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_PAGE_ALLOCS, "Num_file_page_allocs"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_FILE_NUM_PAGE_DEALLOCS, "Num_file_page_deallocs"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_FILE_IOSYNC_ALL, "file_iosync_all"),

  /* Execution statistics for the page buffer manager */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_FETCHES, "Num_data_page_fetches"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_DIRTIES, "Num_data_page_dirties"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_IOREADS, "Num_data_page_ioreads"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_IOWRITES, "Num_data_page_iowrites"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_VICTIMS, "Num_data_page_victims"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_REPLACEMENTS, "Num_data_page_iowrites_for_replacement"),
  /* peeked stats */
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_FIXED_CNT, "Num_data_page_fixed"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_DIRTY_CNT, "Num_data_page_dirty"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LRU1_CNT, "Num_data_page_lru1"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LRU2_CNT, "Num_data_page_lru2"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_AIN_CNT, "Num_data_page_ain"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_VICTIM_CAND_CNT, "Num_data_page_victim_cand"),
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
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_UNFIX_LRU_THREE_PRV_TO_SHR_MID,
				  "Num_unfix_lru3_private_to_shared_mid"),
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
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_NUM_SKIPPED_ALREADY_FLUSHED,
				  "Num_data_page_skipped_flush_already_flushed"),
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
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_VOID,
				  "Num_victim_assign_direct_vacuum_void"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ASSIGN_DIRECT_VACUUM_LRU,
				  "Num_victim_assign_direct_vacuum_lru"),
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
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_SUCCESS,
				  "Num_victim_other_private_lru_success"),
  /* failed searches */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_SHARED_LRU_FAIL, "Num_victim_shared_lru_fail"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_OWN_PRIVATE_LRU_FAIL, "Num_victim_own_private_lru_fail"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_OTHER_PRIVATE_LRU_FAIL, "Num_victim_other_private_lru_fail"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_ALL_LRU_FAIL, "Num_victim_all_lru_fail"),
  /* search lru's */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_GET_FROM_LRU, "Num_victim_get_from_lru"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_PB_VICTIM_GET_FROM_LRU_LIST_WAS_EMPTY,
				  "Num_victim_get_from_lru_was_empty"),
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
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_FLUSHED_BCBS_WAIT_FOR_ASSIGN,
				   "Num_flushed_bcbs_wait_for_direct_victim"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_BIG_PRV_NUM, "Num_lfcq_big_private_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_PRV_NUM, "Num_lfcq_private_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_LFCQ_SHR_NUM, "Num_lfcq_shared_lists"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_AVOID_DEALLOC_CNT, "Num_data_page_avoid_dealloc"),
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_PB_AVOID_VICTIM_CNT, "Num_data_page_avoid_victim"),

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

  /* Execution statistics for the heap manager */
  /* TODO: Move this to heap section. TODO: count and timer. */
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HEAP_NUM_STATS_SYNC_BESTSPACE, "Num_heap_stats_sync_bestspace"),

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

  /* best space info */
  PSTAT_METADATA_INIT_SINGLE_PEEK (PSTAT_HF_NUM_STATS_ENTRIES, "Num_heap_stats_bestspace_entries"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_HF_NUM_STATS_MAXED, "Num_heap_stats_bestspace_maxed"),

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
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_VAC_NUM_PREFETCH_REQUESTS_LOG_PAGES,
				  "Num_vacuum_prefetch_requests_log_pages"),
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
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_INSERT_PREPARE, "Time_heap_insert_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_INSERT_EXECUTE, "Time_heap_insert_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_INSERT_LOG, "Time_heap_insert_log"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_DELETE_PREPARE, "Time_heap_delete_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_DELETE_EXECUTE, "Time_heap_delete_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_DELETE_LOG, "Time_heap_delete_log"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_UPDATE_PREPARE, "Time_heap_update_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_UPDATE_EXECUTE, "Time_heap_update_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_UPDATE_LOG, "Time_heap_update_log"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_VACUUM_PREPARE, "Time_heap_vacuum_prepare"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_VACUUM_EXECUTE, "Time_heap_vacuum_execute"),
  PSTAT_METADATA_INIT_COUNTER_TIMER (PSTAT_HEAP_VACUUM_LOG, "Time_heap_vacuum_log"),

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
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_OLDEST_MVCC_TIME_COUNTERS, "Time_get_oldest_mvcc_acquire_time"),
  PSTAT_METADATA_INIT_SINGLE_ACC (PSTAT_LOG_OLDEST_MVCC_RETRY_COUNTERS, "Count_get_oldest_mvcc_retry"),

  /* computed statistics */
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_HIT_RATIO, "Data_page_buffer_hit_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_LOG_HIT_RATIO, "Log_page_buffer_hit_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_VACUUM_DATA_HIT_RATIO, "Vacuum_data_page_buffer_hit_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_VACUUM_EFFICIENCY, "Vacuum_page_efficiency_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_VACUUM_FETCH_RATIO, "Vacuum_page_fetch_ratio"),

  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_LOCK_ACQUIRE_TIME_10USEC,
				      "Data_page_fix_lock_acquire_time_msec"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_HOLD_ACQUIRE_TIME_10USEC,
				      "Data_page_fix_hold_acquire_time_msec"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC, "Data_page_fix_acquire_time_msec"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_ALLOCATE_TIME_RATIO, "Data_page_allocate_time_ratio"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_PROMOTE_SUCCESS, "Data_page_total_promote_success"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_PROMOTE_FAILED, "Data_page_total_promote_fail"),
  PSTAT_METADATA_INIT_COMPUTED_RATIO (PSTAT_PB_PAGE_PROMOTE_TOTAL_TIME_10USEC,
				      "Data_page_total_promote_time_msec"),

  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_FIX_COUNTERS, "Num_data_page_fix_ext", &perfbase_Complex_page_fix),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_PROMOTE_COUNTERS, "Num_data_page_promote_ext", &perfbase_Complex_page_promote),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_PROMOTE_TIME_COUNTERS, "Num_data_page_promote_time_ext",
			       &perfbase_Complex_page_promote_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_UNFIX_COUNTERS, "Num_data_page_unfix_ext", &perfbase_Complex_page_unfix),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_LOCK_TIME_COUNTERS, "Time_data_page_lock_acquire_time",
			       &perfbase_Complex_page_lock_acquire_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_HOLD_TIME_COUNTERS, "Time_data_page_hold_acquire_time",
			       &perfbase_Complex_page_hold_acquire_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_FIX_TIME_COUNTERS, "Time_data_page_fix_acquire_time",
			       &perfbase_Complex_page_fix_acquire_time),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_MVCC_SNAPSHOT_COUNTERS, "Num_mvcc_snapshot_ext",
			       &perfbase_Complex_Num_mvcc_snapshot),
  PSTAT_METADATA_INIT_COMPLEX (PSTAT_OBJ_LOCK_TIME_COUNTERS, "Time_obj_lock_acquire_time",
			       &perfbase_Complex_Time_obj_lock_acquire_time)
};

#if defined (SA_MODE)
bool perfmeta_Initialized = false;
#endif /* SA_MODE */

/************************************************************************/
/* end of macros and structures                                         */
/************************************************************************/

/************************************************************************/
/* start of static functions section                                    */
/************************************************************************/

static void perfmon_print_timer_to_buffer (char **s, int stat_index, UINT64 * stats_ptr, int *remained_size);
static void perfmon_stat_dump_in_file (FILE * stream, PSTAT_METADATA * stat, const UINT64 * stats_ptr, int show_zeroes);
static void perfmon_stat_dump_in_buffer (PSTAT_METADATA * stat, const UINT64 * stats_ptr, char **s,
					 int *remaining_size);

static void perfbase_load_complex_names (char (*names)[PERFMETA_VALNAME_MAX_SIZE], PSTAT_METADATA * metadata);
static void perfbase_complex_iterator_init (const PERFBASE_COMPLEX * complexp, PERFBASE_COMPLEX_ITERATOR * iterator);
static bool perfbase_complex_iterator_next (PERFBASE_COMPLEX_ITERATOR * iterator);
static void perfmon_print_timer_to_file_in_table_form (FILE * stream, int stat_index, const UINT64 ** stats,
						       int num_of_stats, int show_zero, int show_header);
static void perfmon_stat_dump_in_file_in_table_form (FILE * stream, PSTAT_METADATA * stat, const UINT64 ** stats,
						     int no_of_stats, int show_zeroes);

/************************************************************************/
/* end of static functions section                                      */
/************************************************************************/

static void
perfbase_complex_iterator_init (const PERFBASE_COMPLEX * complexp, PERFBASE_COMPLEX_ITERATOR * iterator)
{
  iterator->complexp = complexp;
  memset (iterator->cursor.indices, 0, sizeof (iterator->cursor.indices));
}

static bool
perfbase_complex_iterator_next (PERFBASE_COMPLEX_ITERATOR * iterator)
{
  int crt_dim;

  for (crt_dim = iterator->complexp->size - 1; crt_dim >= 0; crt_dim--)
    {
      if (++iterator->cursor.indices[crt_dim] < iterator->complexp->dimensions[crt_dim]->size)
	{
	  /* end incrementing offsets */
	  return true;
	}
      /* reset offset for current dimension and proceed to increment next dimension */
      iterator->cursor.indices[crt_dim] = 0;
    }
  /* all dimensions have been consumed */
  return false;
}

void
perfbase_aggregate_complex (int id, const UINT64 * vals, int index_dim, UINT64 * agg_vals)
{
  PERFBASE_COMPLEX_ITERATOR iter;
  const PERFBASE_COMPLEX *complexp = pstat_Metadata[id].complexp;
  int offset_value;

  assert (pstat_Metadata[id].valtype == PSTAT_COMPLEX_VALUE && complexp != NULL);
  assert (index_dim < complexp->size);

  perfbase_complex_iterator_init (complexp, &iter);
  offset_value = pstat_Metadata[id].start_offset;

  /* initialize aggregated values */
  memset (agg_vals, 0, sizeof (UINT64) * complexp->dimensions[index_dim]->size);

  /* compute aggregated values by index_dim */
  do
    {
      agg_vals[iter.cursor.indices[index_dim]] += vals[offset_value];
      ++offset_value;
    }
  while (perfbase_complex_iterator_next (&iter));

  /* safe-guard: all values have been processed */
  assert (offset_value == (pstat_Metadata[id].start_offset + pstat_Metadata[id].n_vals));

  /* done */
}

int
perfmeta_init (void)
{
  int idx, i, n_vals;
  int nvals_total = 0;

#if defined (SA_MODE)
  /* called twice, once for client and once for server. */
  if (perfmeta_Initialized)
    {
      return NO_ERROR;
    }
  perfmeta_Initialized = true;
#endif /* SA_MODE*/

  for (idx = 0; idx < PSTAT_COUNT; idx++)
    {
      pstat_Metadata[idx].start_offset = nvals_total;
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
	  n_vals = 1;
	  for (i = 0; i < pstat_Metadata[idx].complexp->size; i++)
	    {
	      n_vals *= pstat_Metadata[idx].complexp->dimensions[i]->size;
	    }
	  pstat_Metadata[idx].n_vals = n_vals;
	  if (pstat_Metadata[idx].complexp->size < 1)
	    {
	      assert (false);
	      return pstat_Metadata[idx].n_vals;
	    }
	  break;
	default:
	  assert (false);
	}
      nvals_total += pstat_Metadata[idx].n_vals;
    }

  perfmeta_Stat_count = nvals_total;

  /* initialize stat name array */
  pstat_Value_names = (char (*)[PERFMETA_VALNAME_MAX_SIZE]) malloc (PERFMETA_VALNAME_MAX_SIZE * nvals_total);
  if (pstat_Value_names == NULL)
    {
      /* this won't work... */
      assert (false);
      return -1;
    }
  for (i = 0; i < PSTAT_COUNT; i++)
    {
      switch (pstat_Metadata[i].valtype)
	{
	case PSTAT_ACCUMULATE_SINGLE_VALUE:
	case PSTAT_PEEK_SINGLE_VALUE:
	case PSTAT_COMPUTED_RATIO_VALUE:
	  strncpy (pstat_Value_names[pstat_Metadata[i].start_offset], pstat_Metadata[i].stat_name,
		   PERFMETA_VALNAME_MAX_SIZE);
	  break;

	case PSTAT_COUNTER_TIMER_VALUE:
	  /* num, total time, max time and average time */

	  strcpy (pstat_Value_names[PSTAT_COUNTER_TIMER_COUNT_VALUE (pstat_Metadata[i].start_offset)], "Num_");
	  strncat (pstat_Value_names[PSTAT_COUNTER_TIMER_COUNT_VALUE (pstat_Metadata[i].start_offset)],
		   pstat_Metadata[i].stat_name, PERFMETA_VALNAME_MAX_SIZE - strlen ("Num_"));

	  strcpy (pstat_Value_names[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (pstat_Metadata[i].start_offset)],
		  "Total_time_");
	  strncat (pstat_Value_names[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (pstat_Metadata[i].start_offset)],
		   pstat_Metadata[i].stat_name, PERFMETA_VALNAME_MAX_SIZE - strlen ("Total_time_"));

	  strcpy (pstat_Value_names[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (pstat_Metadata[i].start_offset)], "Max_time_");
	  strncat (pstat_Value_names[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (pstat_Metadata[i].start_offset)],
		   pstat_Metadata[i].stat_name, PERFMETA_VALNAME_MAX_SIZE - strlen ("Max_time_"));

	  strcpy (pstat_Value_names[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (pstat_Metadata[i].start_offset)], "Avg_time_");
	  strncat (pstat_Value_names[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (pstat_Metadata[i].start_offset)],
		   pstat_Metadata[i].stat_name, PERFMETA_VALNAME_MAX_SIZE - strlen ("Avg_time_"));
	  break;

	case PSTAT_COMPLEX_VALUE:
	  perfbase_load_complex_names (pstat_Value_names, &pstat_Metadata[i]);
	  break;

	default:
	  assert (false);
	  break;
	}
    }

  return NO_ERROR;
}

void
perfmeta_final (void)
{
  if (pstat_Value_names != NULL)
    {
      free (pstat_Value_names);
      pstat_Value_names = NULL;
    }
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
void
perfmon_print_timer_to_file (FILE * stream, int stat_index, UINT64 * stats_ptr, int show_zero, int show_header)
{
  int offset = pstat_Metadata[stat_index].start_offset;
  long long timer_count = (long long) stats_ptr[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)];
  long long timer_total = (long long) stats_ptr[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)];
  long long timer_max = (long long) stats_ptr[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)];
  long long timer_avg = (long long) stats_ptr[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)];

  assert (pstat_Metadata[stat_index].valtype == PSTAT_COUNTER_TIMER_VALUE);
  if (show_header == 1)
    {
      fprintf (stream, "The timer values for %s are:\n", pstat_Metadata[stat_index].stat_name);
    }
  if (timer_count != 0 || show_zero == 1)
    {
      fprintf (stream, "Num_%-25s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_count);
    }

  if (timer_total != 0 || show_zero == 1)
    {
      fprintf (stream, "Total_time_%-18s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_total);
    }

  if (timer_max != 0 || show_zero == 1)
    {
      fprintf (stream, "Max_time_%-20s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_max);
    }

  if (timer_avg != 0 || show_zero == 1)
    {
      fprintf (stream, "Avg_time_%-20s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_avg);
    }
}

/*
 * perfmon_print_timer_to_file_in_table_form - Print in a file multiple statistic values in table form (colums)
 *
 * stream (in/out): input file
 * stat_index (in): statistic index
 * stats (in) : statistic values array
 * num_of_stats (in) : number of stats in array
 * show_zero (in) : show(1) or not(0) null values
 * show_header (in) : show(1) or not(0) the header
 * return: void
 *
 */
static void
perfmon_print_timer_to_file_in_table_form (FILE * stream, int stat_index, const UINT64 ** stats, int num_of_stats,
					   int show_zero, int show_header)
{
  int offset = pstat_Metadata[stat_index].start_offset;
  int i;
  long long *timer_count = (long long *) malloc (sizeof (long long) * num_of_stats);
  long long *timer_total = (long long *) malloc (sizeof (long long) * num_of_stats);
  long long *timer_max = (long long *) malloc (sizeof (long long) * num_of_stats);
  long long *timer_avg = (long long *) malloc (sizeof (long long) * num_of_stats);
  int show_timer_count = 0, show_timer_total = 0, show_timer_max = 0, show_timer_avg = 0;

  assert (pstat_Metadata[stat_index].valtype == PSTAT_COUNTER_TIMER_VALUE);

  for (i = 0; i < num_of_stats; i++)
    {
      timer_count[i] = (long long) stats[i][PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)];
      timer_total[i] = (long long) stats[i][PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)];
      timer_max[i] = (long long) stats[i][PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)];
      timer_avg[i] = (long long) stats[i][PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)];

      if (timer_count[i] != 0)
	{
	  show_timer_count = 1;
	}
      if (timer_total[i] != 0)
	{
	  show_timer_total = 1;
	}
      if (timer_max[i] != 0)
	{
	  show_timer_max = 1;
	}
      if (timer_avg[i] != 0)
	{
	  show_timer_avg = 1;
	}
    }

  if (show_header == 1)
    {
      fprintf (stream, "The timer values for %s are:\n", pstat_Metadata[stat_index].stat_name);
    }
  if (show_timer_count != 0 || show_zero == 1)
    {
      fprintf (stream, "Num_%-46s", pstat_Metadata[stat_index].stat_name);
      for (i = 0; i < num_of_stats; i++)
	{
	  fprintf (stream, "%15lld", timer_count[i]);
	}
      fprintf (stream, "\n");
    }

  if (show_timer_total != 0 || show_zero == 1)
    {
      fprintf (stream, "Total_time_%-40s", pstat_Metadata[stat_index].stat_name);
      for (i = 0; i < num_of_stats; i++)
	{
	  fprintf (stream, "%15lld", timer_total[i]);
	}
      fprintf (stream, "\n");
    }

  if (show_timer_max != 0 || show_zero == 1)
    {
      fprintf (stream, "Max_time_%-41s", pstat_Metadata[stat_index].stat_name);
      for (i = 0; i < num_of_stats; i++)
	{
	  fprintf (stream, "%15lld", timer_max[i]);
	}
      fprintf (stream, "\n");
    }

  if (show_timer_avg != 0 || show_zero == 1)
    {
      fprintf (stream, "Avg_time_%-41s", pstat_Metadata[stat_index].stat_name);
      for (i = 0; i < num_of_stats; i++)
	{
	  fprintf (stream, "%15lld", timer_avg[i]);
	}
      fprintf (stream, "\n");
    }

  free (timer_total);
  free (timer_max);
  free (timer_avg);
  free (timer_count);
}

void
perfmon_compare_timer (FILE * stream, int stat_index, UINT64 * stats1, UINT64 * stats2)
{
  int offset = pstat_Metadata[stat_index].start_offset;

  long long timer_count1 = (long long) stats1[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)];
  long long timer_count2 = (long long) stats2[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)];

  long long timer_total1 = (long long) stats1[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)];
  long long timer_total2 = (long long) stats2[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)];

  long long timer_max1 = (long long) stats1[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)];
  long long timer_max2 = (long long) stats2[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)];

  long long timer_avg1 = (long long) stats1[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)];
  long long timer_avg2 = (long long) stats2[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)];

  assert (pstat_Metadata[stat_index].valtype == PSTAT_COUNTER_TIMER_VALUE);
  fprintf (stream, "The timer values for %s are:\n", pstat_Metadata[stat_index].stat_name);

  if (timer_count1 != 0 || timer_count2 != 0)
    {

      fprintf (stream, "Num_%-54s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
	       timer_count1, timer_count2, timer_count1 - timer_count2);
    }
  if (timer_total1 != 0 || timer_total2 != 0)
    {
      fprintf (stream, "Total_time_%-47s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
	       timer_total1, timer_total2, timer_total1 - timer_total2);
    }
  if (timer_max1 != 0 || timer_max2 != 0)
    {
      fprintf (stream, "Max_time_%-49s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
	       timer_max1, timer_max2, timer_max1 - timer_max2);
    }
  if (timer_avg1 != 0 || timer_avg2 != 0)
    {
      fprintf (stream, "Avg_time_%-49s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
	       timer_avg1, timer_avg2, timer_avg1 - timer_avg2);
    }
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

  for (i = 0; i < perfmeta_Stat_count; i++)
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

  for (i = 0; i < perfmeta_Stat_count; i++)
    {
      OR_GET_INT64 (ptr, &(stats[i]));
      ptr += OR_INT64_SIZE;
    }

  return (ptr);
}

/*
 * perfbase_load_complex_names () - generate names for all complex statistic fields
 *
 * return              : void
 * names (in)          : array with names for each offset
 * metadata (in)       : metadata
 */

void
perfbase_load_complex_names (char (*names)[PERFMETA_VALNAME_MAX_SIZE], PSTAT_METADATA * metadata)
{
  PERFBASE_COMPLEX_ITERATOR iter;
  const PERFBASE_COMPLEX *complexp = metadata->complexp;
  int i;
  int offset = metadata->start_offset;

  perfbase_complex_iterator_init (complexp, &iter);

  do
    {
      int str_size = 0;
      for (i = 0; i < complexp->size; i++)
	{
	  strncpy (names[offset] + str_size, complexp->dimensions[i]->names[iter.cursor.indices[i]],
		   PERFMETA_VALNAME_MAX_SIZE - str_size);
	  if (strlen (complexp->dimensions[i]->names[iter.cursor.indices[i]]) < PERFMETA_VALNAME_MAX_SIZE - str_size)
	    {
	      *(names[offset] + str_size + strlen (complexp->dimensions[i]->names[iter.cursor.indices[i]])) = ' ';
	      str_size++;
	    }
	  str_size += strlen (complexp->dimensions[i]->names[iter.cursor.indices[i]]);
	}
      names[offset][PERFMETA_VALNAME_MAX_SIZE <= str_size ? PERFMETA_VALNAME_MAX_SIZE - 1 : str_size] = '\0';
      offset++;
    }
  while (perfbase_complex_iterator_next (&iter));
}

/*
 * perfmon_print_timer_to_buffer - Print in a buffer the statistic values for a timer type statistic
 *
 * return                 : void
 * s (in/out)             : input stream
 * stat_index (in)        : statistic index
 * stats_ptr (in)         : statistic values array
 * remained_size (in/out) : remained size to write in the buffer
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
 * perfmon_stat_dump_in_file () - document me!
 *
 * return         : void
 * stream (in)    :
 * stat (in)      :
 * stats_ptr (in) :
 */
void
perfmon_stat_dump_in_file (FILE * stream, PSTAT_METADATA * stat, const UINT64 * stats_ptr, int show_zeroes)
{
  UINT64 counter = 0;
  int i;
  int start_offset = stat->start_offset;
  int end_offset = stat->start_offset + stat->n_vals;

  assert (stream != NULL);
  for (i = start_offset; i < end_offset; i++)
    {
      counter = stats_ptr[i];
      if (counter == 0 && show_zeroes == 0)
	{
	  continue;
	}
      fprintf (stream, "%-56s = %16lld\n", pstat_Value_names[i], (long long) counter);
    }
}

static void
perfmon_stat_dump_in_file_in_table_form (FILE * stream, PSTAT_METADATA * stat, const UINT64 ** stats, int no_of_stats,
					 int show_zeroes)
{
  int i, j;
  int start_offset = stat->start_offset;
  int end_offset = stat->start_offset + stat->n_vals;

  assert (stream != NULL);
  for (i = start_offset; i < end_offset; i++)
    {
      int show = 0;

      for (j = 0; j < no_of_stats; j++)
	{
	  if (stats[j][i] != 0)
	    {
	      show = 1;
	    }
	}

      if (show == 0 && show_zeroes == 0)
	{
	  continue;
	}
      fprintf (stream, "%-50s", pstat_Value_names[i]);
      for (j = 0; j < no_of_stats; j++)
	{
	  fprintf (stream, "%15lld", (long long) stats[j][i]);
	}
      fprintf (stream, "\n");
    }
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
		  perfmon_print_timer_to_file (stream, i, stats_ptr, 0, 1);
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
      perfmon_stat_dump_in_file (stream, &pstat_Metadata[i], stats, 0);
    }
}

void
perfmeta_custom_dump_stats_in_table_form (const UINT64 ** stats, int no_of_stats, FILE * stream, int show_complex,
					  int show_zero)
{
  int i, j, show;
  int offset;

  if (stream == NULL)
    {
      stream = stdout;
    }

  for (i = 0; i < PSTAT_COUNT; i++)
    {
      offset = pstat_Metadata[i].start_offset;
      if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
	{
	  break;
	}

      if (pstat_Metadata[i].valtype != PSTAT_COMPUTED_RATIO_VALUE)
	{
	  if (pstat_Metadata[i].valtype != PSTAT_COUNTER_TIMER_VALUE)
	    {
	      show = 0;
	      for (j = 0; j < no_of_stats; j++)
		{
		  if (stats[j][offset] != 0)
		    {
		      show = 1;
		    }
		}

	      if (show == 1 || show_zero == 1)
		{
		  fprintf (stream, "%-50s", pstat_Metadata[i].stat_name);
		  for (j = 0; j < no_of_stats; j++)
		    {
		      fprintf (stream, "%15lld", (long long) stats[j][offset]);
		    }
		  fprintf (stream, "\n");
		}
	    }
	  else
	    {
	      perfmon_print_timer_to_file_in_table_form (stream, i, stats, no_of_stats, show_zero, 0);
	    }
	}
      else
	{
	  show = 0;
	  for (j = 0; j < no_of_stats; j++)
	    {
	      if (stats[j][offset] != 0)
		{
		  show = 1;
		}
	    }
	  if (show == 1 || show_zero == 1)
	    {
	      fprintf (stream, "%-50s", pstat_Metadata[i].stat_name);
	      for (j = 0; j < no_of_stats; j++)
		{
		  fprintf (stream, "%15.2f", (float) stats[j][offset] / 100);
		}
	      fprintf (stream, "\n");
	    }
	}
    }

  for (; show_complex == 1 && i < PSTAT_COUNT; i++)
    {
      fprintf (stream, "%s:\n", pstat_Metadata[i].stat_name);
      perfmon_stat_dump_in_file_in_table_form (stream, &pstat_Metadata[i], stats, no_of_stats, show_zero);
    }
}

/*
 * perfmon_stat_dump_in_buffer () - document me!
 *
 * return                  : void
 * stat (in)               : current metadata stat
 * stats_ptr (in)          : raw data of stat
 * s (in/out)              : out buffer
 * remaining_size (in/out) :
 */
void
perfmon_stat_dump_in_buffer (PSTAT_METADATA * stat, const UINT64 * stats_ptr, char **s, int *remaining_size)
{
  UINT64 counter = 0;
  int ret, i;
  int start_offset = stat->start_offset;
  int end_offset = stat->start_offset + stat->n_vals;

  assert (remaining_size != NULL);
  assert (s != NULL);
  if (*s != NULL)
    {
      for (i = start_offset; i < end_offset; i++)
	{
	  counter = stats_ptr[i];
	  if (counter == 0)
	    {
	      continue;
	    }
	  ret = snprintf (*s, (size_t) * remaining_size, "%-56s = %16lld\n", pstat_Value_names[i], (long long) counter);
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
      perfmon_stat_dump_in_buffer (&pstat_Metadata[i], stats, &p, &remained_size);
    }

  buffer[buf_size - 1] = '\0';
}

/* todo: inline */
int
perfmeta_complex_cursor_get_offset (PERF_STAT_ID psid, const PERFMETA_COMPLEX_CURSOR * cursor)
{
  PSTAT_METADATA *metada = &pstat_Metadata[psid];
  int offset = 0;
  int iter_dim = 0;
  int multiplier = 1;

  assert (PSTAT_BASE < psid && psid < PSTAT_COUNT);
  assert (metada->valtype == PSTAT_COMPLEX_VALUE);

  /* compute offset */
  for (iter_dim = metada->complexp->size - 1; iter_dim >= 0; iter_dim--)
    {
      assert (cursor->indices[iter_dim] < metada->complexp->dimensions[iter_dim]->size
	      && cursor->indices[iter_dim] >= 0);
      offset += cursor->indices[iter_dim] * multiplier;
      multiplier *= metada->complexp->dimensions[iter_dim]->size;
    }

  return offset + metada->start_offset;
}

int
perfmeta_complex_get_offset (PERF_STAT_ID psid, ...)
{
  PSTAT_METADATA *metada = &pstat_Metadata[psid];
  va_list ap;
  int offset = 0;
  int iter_dim;
  int val;
  int multiplier = 1;

  va_start (ap, psid);
  for (iter_dim = metada->complexp->size - 1; iter_dim >= 0; iter_dim--)
    {
      val = va_arg (ap, int);
      assert (val < metada->complexp->dimensions[iter_dim]->size && val >= 0);
      offset += val * multiplier;
      multiplier *= metada->complexp->dimensions[iter_dim]->size;
    }
  va_end (ap);

  return offset + metada->start_offset;
}

UINT64
perfmeta_get_stat_value_from_name (const char *stat_name, UINT64 * raw_stats)
{
  int i;

  for (i = 0; i < perfmeta_Stat_count; i++)
    {
      if (strcmp (pstat_Value_names[i], stat_name) == 0)
	{
	  return raw_stats[i];
	}
    }
  return 0;
}

void
perfmeta_copy_stats (UINT64 * dst, UINT64 * src)
{
  int i;
  for (i = 0; i < perfmeta_get_values_count (); i++)
    {
      dst[i] = src[i];
    }
}

int
perfmeta_get_values_count ()
{
  return perfmeta_Stat_count;
}

void
perfmeta_get_stat_index_and_dimension (const char *stat_name, const char *dimension_name, int *stat_index,
				       int *fixed_dimension)
{
  int i, j;
  for (i = 0; i < PSTAT_COUNT; i++)
    {
      if (strcmp (pstat_Metadata[i].stat_name, stat_name) == 0)
	{
	  *stat_index = i;
	  if (*fixed_dimension == -1)
	    {
	      for (j = 0; j < pstat_Metadata[*stat_index].complexp->size; j++)
		{
		  if (strcmp (dimension_name, pstat_Metadata[*stat_index].complexp->dimensions[j]->alias) == 0)
		    {
		      *fixed_dimension = j;
		      break;
		    }
		}
	    }
	  break;
	}
    }
}

size_t
perfmeta_get_values_memsize (void)
{
  return perfmeta_get_values_count () * sizeof (UINT64);
}

/*
 * perfmeta_allocate_values () - Allocate perfmeta_get_values_memsize () bytes 
 * 
 */
UINT64 *
perfmeta_allocate_values (void)
{
  UINT64 *vals;

  vals = (UINT64 *) malloc (perfmeta_get_values_memsize ());
  if (vals == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, perfmeta_get_values_memsize ());
    }

  return vals;
}

/*
 * perfmeta_copy_values () -
 *
 * dest (in/out): destination buffer
 * source (in): source buffer
 * 
 */
void
perfmeta_copy_values (UINT64 * dest, UINT64 * src)
{
  memcpy (dest, src, perfmeta_get_values_memsize ());
}

/*
 *   perfmeta_compute_stats - Do post processing of server statistics
 *   return: none
 *   stats(in/out): server statistics block to be processed
 */
void
perfmeta_compute_stats (UINT64 * stats)
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
		      offset =
			perfmeta_complex_get_offset (PSTAT_PBX_UNFIX_COUNTERS, module, page_type, buf_dirty,
						     holder_dirty, holder_latch);
		      counter = stats[offset];

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
		  offset =
		    perfmeta_complex_get_offset (PSTAT_PBX_HOLD_TIME_COUNTERS, module, page_type, page_found_mode,
						 holder_latch);
		  counter = stats[offset];

		  if (page_type != PERF_PAGE_LOG && counter > 0)
		    {
		      hold_time_usec += counter;
		    }

		  for (cond_type = PERF_CONDITIONAL_FIX; cond_type < PERF_CONDITIONAL_FIX_CNT; cond_type++)
		    {
		      offset =
			perfmeta_complex_get_offset (PSTAT_PBX_FIX_TIME_COUNTERS, module, page_type, page_found_mode,
						     holder_latch, cond_type);
		      counter = stats[offset];

		      /* do not include fix time of log pages */
		      if (page_type != PERF_PAGE_LOG && counter > 0)
			{
			  fix_time_usec += counter;
			}

		      offset =
			perfmeta_complex_get_offset (PSTAT_PBX_LOCK_TIME_COUNTERS, module, page_type, page_found_mode,
						     holder_latch, cond_type);
		      counter = stats[offset];

		      if (page_type != PERF_PAGE_LOG && counter > 0)
			{
			  lock_time_usec += counter;
			}

		      if (module == PERF_MODULE_VACUUM && page_found_mode != PERF_PAGE_MODE_NEW_LOCK_WAIT
			  && page_found_mode != PERF_PAGE_MODE_NEW_NO_WAIT)
			{
			  offset =
			    perfmeta_complex_get_offset (PSTAT_PBX_FIX_COUNTERS, module, page_type, page_found_mode,
							 holder_latch, cond_type);
			  counter = stats[offset];

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
		      offset =
			perfmeta_complex_get_offset (PSTAT_PBX_PROMOTE_TIME_COUNTERS, module, page_type, promote_cond,
						     holder_latch, success);
		      counter = stats[offset];
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

int
perfmeta_diff_stats (UINT64 * stats_diff, UINT64 * new_stats, UINT64 * old_stats)
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

        case PSTAT_COMPUTED_RATIO_VALUE:
          /* will be computed later */
          break;

	default:
	  assert (false);
	  break;
	}
    }

  perfmeta_compute_stats (stats_diff);
  return NO_ERROR;
}
