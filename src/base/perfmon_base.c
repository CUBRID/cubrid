//
// Created by paul on 15.03.2017.
//

#include <assert.h>
#include "perfmon_base.h"
#include "porting.h"

#if defined (SERVER_MODE) || defined (SA_MODE) || defined (CS_MODE)
#include "error_manager.h"
#endif

#define PSTAT_METADATA_INIT_SINGLE_ACC(id, name) { id, name, PSTAT_ACCUMULATE_SINGLE_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_SINGLE_PEEK(id, name) \
  { id, name, PSTAT_PEEK_SINGLE_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_COUNTER_TIMER(id, name) { id, name, PSTAT_COUNTER_TIMER_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_COMPUTED_RATIO(id, name) \
  { id, name, PSTAT_COMPUTED_RATIO_VALUE, 0, 0, NULL}
#define PSTAT_METADATA_INIT_COMPLEX(id, name, md_complex) \
  { id, name, PSTAT_COMPLEX_VALUE, 0, 0, md_complex }

PSTAT_GLOBAL pstat_Global;
PSTAT_NAMEOFFSET *pstat_Nameoffset;
int total_num_stat_vals;

const PERFBASE_DIM perfbase_Dim_module = {
  PERF_MODULE_CNT,
  {"SYSTEM", "WORKER", "VACUUM"},
};

const PERFBASE_DIM perfbase_Dim_page_type = {
  PERF_PAGE_CNT,
  { "PAGE_UNKNOWN", "PAGE_FTAB", "PAGE_HEAP", "PAGE_VOLHEADER", "PAGE_VOLBITMAP", "PAGE_QRESULT", "PAGE_EHASH",
    "PAGE_OVERFLOW", "PAGE_AREA", "PAGE_CATALOG", "PAGE_BTREE", "PAGE_LOG", "PAGE_DROPPED", "PAGE_VACUUM_DATA",
    "PAGE_BTREE_R", "PAGE_BTREE_O", "PAGE_BTREE_L", "PAGE_BTREE_N"},
};

const PERFBASE_DIM perfbase_Dim_page_mode = {
  PERF_PAGE_MODE_CNT,
  { "OLD_WAIT", "OLD_NO_WAIT", "NEW_WAIT", "NEW_NO_WAIT", "OLD_PAGE_IN_PB" },
};

const PERFBASE_DIM perfbase_Dim_holder_latch = {
  PERF_HOLDER_LATCH_CNT,
  { "READ", "WRITE", "MIXED" },
};

const PERFBASE_DIM perfbase_Dim_cond_type = {
  PERF_CONDITIONAL_FIX_CNT,
  { "COND", "UNCOND", "UNCOND_WAIT" },
};

const PERFBASE_DIM perfbase_Dim_snapshot = {
  PERF_SNAPSHOT_CNT,
  { "DELETE", "DIRTY", "SNAPSHOT", "VACUUM" },
};

const PERFBASE_DIM perfbase_Dim_snapshot_record_type = {
  PERF_SNAPSHOT_RECORD_TYPE_CNT,
  { "INS_VACUUMED", "INS_CURR", "INS_OTHER", "INS_COMMITTED", "INS_COMMITTED_L", "INS_DELETED", "DELETED_CURR",
    "DELETED_OTHER", "DELETED_COMMITED", "DELETED_COMMITED_L" },
};

const PERFBASE_DIM perfbase_Dim_lock_mode = {
  PERF_LOCK_CNT,
  { "NA_LOCK", "INCON_2PL", "NULL_LOCK", "SCH_S_LOCK", "IS_LOCK", "S_LOCK", "IX_LOCK", "SIX_LOCK", "U_LOCK", "X_LOCK",
    "SCH_M_LOCK" },
};

const PERFBASE_DIM perfbase_Dim_promote_cond = {
  2,
  { "ONLY_READER", "SHARED_READER" },
};

const PERFBASE_DIM perfbase_Dim_success = {
  2,
  { "SUCCESS", "FAILED" },
};

const PERFBASE_DIM perfbase_Dim_buf_dirty = {
  2,
  { "BUF_DIRTY", "BUF_NON_DIRTY" },
};

const PERFBASE_DIM perfbase_Dim_holder_dirty = {
  2,
  { "HOLDER_DIRTY", "HOLDER_NON_DIRTY" },
};

const PERFBASE_DIM perfbase_Dim_visibility = {
  2,
  { "INVISIBLE", "VISIBLE" },
};

const PERFBASE_COMPLEX perfbase_Complex_page_fix = {
  5,
  { &perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch,
    &perfbase_Dim_cond_type },
};

PERFBASE_COMPLEX perfbase_Complex_page_promote = {
  5,
  { &perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_promote_cond, &perfbase_Dim_holder_latch,
    &perfbase_Dim_success },
};

PERFBASE_COMPLEX perfbase_Complex_page_promote_time = {
  5,
  { &perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_promote_cond, &perfbase_Dim_holder_latch,
    &perfbase_Dim_success },
};

PERFBASE_COMPLEX perfbase_Complex_page_unfix = {
  5,
  { &perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_buf_dirty, &perfbase_Dim_holder_dirty,
    &perfbase_Dim_holder_latch },
};

PERFBASE_COMPLEX perfbase_Complex_page_lock_acquire_time = {
  5,
  { &perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch,
    &perfbase_Dim_cond_type },
};

PERFBASE_COMPLEX perfbase_Complex_page_hold_acquire_time = {
  4,
  { &perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch }
};

PERFBASE_COMPLEX perfbase_Complex_page_fix_acquire_time = {
  5,
  { &perfbase_Dim_module, &perfbase_Dim_page_type, &perfbase_Dim_page_mode, &perfbase_Dim_holder_latch,
    &perfbase_Dim_cond_type },
};

PERFBASE_COMPLEX perfbase_Complex_Num_mvcc_snapshot = {
  3,
  { &perfbase_Dim_snapshot, &perfbase_Dim_snapshot_record_type, &perfbase_Dim_visibility }
};

PERFBASE_COMPLEX perfbase_Complex_Time_obj_lock_acquire_time = {
  1,
  { &perfbase_Dim_lock_mode }
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

void
perfbase_aggregate_complex_data (PSTAT_METADATA * stat, UINT64 * stats, const int fix_dim_num, const int fix_index,
				 UINT64 *res, int dim, int offset)
{
  int i, j;
  int calculated_offset;
  int k;

  if (dim == stat->complexp->size)
    {
      *res += stats[offset];
      return;
    }

  if (dim != fix_dim_num)
    {
      for (i = 0; i < stat->complexp->dimensions[dim]->size; i++)
	{
	  calculated_offset = offset;
	  k = 1;
	  for (j = dim + 1; j < stat->complexp->size; j++)
	    {
	      k *= stat->complexp->dimensions[j]->size;
	    }
	  calculated_offset += i * k;
	  perfbase_aggregate_complex_data (stat, stats, fix_dim_num, fix_index, res, dim + 1, calculated_offset);
	}
    }
  else
    {
      calculated_offset = offset;
      k = 1;
      for (i = dim + 1; i < stat->complexp->size; i++)
	{
	  k *= stat->complexp->dimensions[i]->size;
	}
      calculated_offset += fix_index * k;
      perfbase_aggregate_complex_data (stat, stats, fix_dim_num, fix_index, res, dim + 1, calculated_offset);
    }
}

int
metadata_initialize ()
{
  int idx, i, n_vals;

  for (idx = 0; idx < PSTAT_COUNT; idx++)
    {
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
	  n_vals = 1;
	  for (i = 0; i < pstat_Metadata[idx].complexp->size; i++)
	    {
	      n_vals *= pstat_Metadata[idx].complexp->dimensions[i]->size;
	    }
	  pstat_Metadata[idx].n_vals = n_vals;
	  if (pstat_Metadata[idx].complexp->size < 1)
	    {
	      /* Error. */
#if defined(SERVER_MODE)
	      ASSERT_ERROR ();
#endif
	      return pstat_Metadata[idx].n_vals;
	    }
	}
      pstat_Global.n_stat_values += pstat_Metadata[idx].n_vals;
    }

  return 0;
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
perfmon_print_timer_to_file (FILE * stream, int stat_index, UINT64 * stats_ptr)
{
  int offset = pstat_Metadata[stat_index].start_offset;
  long long timer_count = (long long) stats_ptr[PSTAT_COUNTER_TIMER_COUNT_VALUE (offset)];
  long long timer_total = (long long) stats_ptr[PSTAT_COUNTER_TIMER_TOTAL_TIME_VALUE (offset)];
  long long timer_max = (long long) stats_ptr[PSTAT_COUNTER_TIMER_MAX_TIME_VALUE (offset)];
  long long timer_avg = (long long) stats_ptr[PSTAT_COUNTER_TIMER_AVG_TIME_VALUE (offset)];

  assert (pstat_Metadata[stat_index].valtype == PSTAT_COUNTER_TIMER_VALUE);
  fprintf (stream, "The timer values for %s are:\n", pstat_Metadata[stat_index].stat_name);
  if (timer_count != 0)
    {
      fprintf (stream, "Num_%-25s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_count);
    }

  if (timer_total != 0)
    {
      fprintf (stream, "Total_time_%-18s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_total);
    }

  if (timer_max != 0)
    {
      fprintf (stream, "Max_time_%-20s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_max);
    }

  if (timer_avg != 0)
    {
      fprintf (stream, "Avg_time_%-20s = %10lld\n", pstat_Metadata[stat_index].stat_name, timer_avg);
    }
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

void
perfbase_init_name_offset_assoc ()
{
  int vals = 0;
  int realI = 0;
  unsigned int i;

  for (i = 0; i < PSTAT_COUNT; i++)
    {
      vals += pstat_Metadata[i].n_vals;
    }
  total_num_stat_vals = vals;

  pstat_Nameoffset = (PSTAT_NAMEOFFSET *) malloc (sizeof (PSTAT_NAMEOFFSET) * vals);

  for (i = 0; i < PSTAT_COUNT; i++)
    {
      int offset = pstat_Metadata[i].start_offset;
      if (pstat_Metadata[i].valtype == PSTAT_ACCUMULATE_SINGLE_VALUE ||
	  pstat_Metadata[i].valtype == PSTAT_PEEK_SINGLE_VALUE ||
	  pstat_Metadata[i].valtype == PSTAT_COMPUTED_RATIO_VALUE)
	{
	  strcpy (pstat_Nameoffset[realI].name, pstat_Metadata[i].stat_name);
	}
      else if (pstat_Metadata[i].valtype == PSTAT_COUNTER_TIMER_VALUE)
	{
	  strcpy (pstat_Nameoffset[realI].name, "Num_");
	  strcat (pstat_Nameoffset[realI].name, pstat_Metadata[i].stat_name);

	  strcpy (pstat_Nameoffset[realI + 1].name, "Total_time_");
	  strcat (pstat_Nameoffset[realI + 1].name, pstat_Metadata[i].stat_name);

	  strcpy (pstat_Nameoffset[realI + 2].name, "Max_time_");
	  strcat (pstat_Nameoffset[realI + 2].name, pstat_Metadata[i].stat_name);

	  strcpy (pstat_Nameoffset[realI + 3].name, "Avg_time_");
	  strcat (pstat_Nameoffset[realI + 3].name, pstat_Metadata[i].stat_name);
	}
      else if (pstat_Metadata[i].valtype == PSTAT_COMPLEX_VALUE)
	{
	  char buffer[STAT_NAME_MAX_SIZE];
	  perfbase_Complex_load_names (pstat_Nameoffset, &pstat_Metadata[i], 0, offset, buffer);
	}
      realI += pstat_Metadata[i].n_vals;
    }
}

void
perfbase_Complex_load_names (PSTAT_NAMEOFFSET * names, PSTAT_METADATA * metadata, int curr_dimension, int curr_offset,
			     char *name_buffer)
{
  if (curr_dimension == metadata->complexp->size)
    {
      strcpy (names[curr_offset].name, name_buffer);
    }
  else
    {
      int i, offset, k;
      char buffer[STAT_NAME_MAX_SIZE];
      k = 1;
      for (i = curr_dimension + 1; i < metadata->complexp->size; i++)
	{
	  k *= metadata->complexp->dimensions[i]->size;
	}
      for (i = 0; i < metadata->complexp->dimensions[curr_dimension]->size; i++)
	{
	  offset = curr_offset;
	  offset += k * i;
	  if (curr_dimension == 0)
	    {
	      strcpy (buffer, metadata->complexp->dimensions[curr_dimension]->names[i]);
	    }
	  else
	    {
	      strcpy (buffer, name_buffer);
	      if (curr_dimension != metadata->complexp->size)
		{
		  strcat (buffer, ",");
		}
	      strcat (buffer, metadata->complexp->dimensions[curr_dimension]->names[i]);
	    }
	  perfbase_Complex_load_names (names, metadata, curr_dimension + 1, offset, buffer);
	}
    }
}

void
perfmon_stat_dump_in_file (FILE * stream, PSTAT_METADATA * stat, const UINT64 * stats_ptr)
{
  UINT64 counter = 0;
  int i;
  int start_offset = stat->start_offset;
  int end_offset = stat->start_offset + stat->n_vals;

  assert (stream != NULL);
  for (i = start_offset; i < end_offset; i++)
    {
      counter = stats_ptr[i];
      if (counter == 0)
	{
	  continue;
	}
      fprintf (stream, "%-56s = %16lld\n", pstat_Nameoffset[i].name, (long long) counter);
    }
}

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
	  ret = snprintf (*s, (size_t) * remaining_size, "%-56s = %16lld\n",
			  pstat_Nameoffset[i].name, (long long) counter);
	  *remaining_size -= ret;
	  *s += ret;
	  if (*remaining_size <= 0)
	    {
	      return;
	    }
	}
    }
}
