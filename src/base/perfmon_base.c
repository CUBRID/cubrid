//
// Created by paul on 15.03.2017.
//

#include <assert.h>
#include "perfmon_base.h"

#if defined (SERVER_MODE) || defined (SA_MODE) || defined (CS_MODE)
#include "error_manager.h"
#include "porting.h"
#else
#define snprintf _sprintf_p
#endif

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
	/* Array type statistics */
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_FIX_COUNTERS, "Num_data_page_fix_ext", &f_dump_in_file_Num_data_page_fix_ext,
				     &f_dump_in_buffer_Num_data_page_fix_ext, &f_load_Num_data_page_fix_ext, &f_dump_diff_in_file_Num_data_page_fix_ext_in_tabel_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_PROMOTE_COUNTERS, "Num_data_page_promote_ext",
				     &f_dump_in_file_Num_data_page_promote_ext, &f_dump_in_buffer_Num_data_page_promote_ext,
				     &f_load_Num_data_page_promote_ext, &f_dump_diff_in_file_Num_data_page_promote_ext_in_table_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_PROMOTE_TIME_COUNTERS, "Num_data_page_promote_time_ext",
				     &f_dump_in_file_Num_data_page_promote_time_ext,
				     &f_dump_in_buffer_Num_data_page_promote_time_ext,
				     &f_load_Num_data_page_promote_time_ext, &f_dump_diff_in_file_Num_data_page_promote_time_ext_in_table_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_UNFIX_COUNTERS, "Num_data_page_unfix_ext",
				     &f_dump_in_file_Num_data_page_unfix_ext, &f_dump_in_buffer_Num_data_page_unfix_ext,
				     &f_load_Num_data_page_unfix_ext, &f_dump_diff_in_file_Num_data_page_unfix_ext_in_table_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_LOCK_TIME_COUNTERS, "Time_data_page_lock_acquire_time",
				     &f_dump_in_file_Time_data_page_lock_acquire_time,
				     &f_dump_in_buffer_Time_data_page_lock_acquire_time,
				     &f_load_Time_data_page_lock_acquire_time, &f_dump_diff_in_file_Time_data_page_lock_acquire_time_in_table_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_HOLD_TIME_COUNTERS, "Time_data_page_hold_acquire_time",
				     &f_dump_in_file_Time_data_page_hold_acquire_time,
				     &f_dump_in_buffer_Time_data_page_hold_acquire_time,
				     &f_load_Time_data_page_hold_acquire_time, &f_dump_diff_in_file_Time_data_page_hold_acquire_time_in_table_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_PBX_FIX_TIME_COUNTERS, "Time_data_page_fix_acquire_time",
				     &f_dump_in_file_Time_data_page_fix_acquire_time,
				     &f_dump_in_buffer_Time_data_page_fix_acquire_time,
				     &f_load_Time_data_page_fix_acquire_time, &f_dump_diff_in_file_Time_data_page_fix_acquire_time_in_table_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_MVCC_SNAPSHOT_COUNTERS, "Num_mvcc_snapshot_ext",
				     &f_dump_in_file_Num_mvcc_snapshot_ext, &f_dump_in_buffer_Num_mvcc_snapshot_ext,
				     &f_load_Num_mvcc_snapshot_ext, &f_dump_diff_in_file_Num_mvcc_snapshot_ext_in_table_form),
	PSTAT_METADATA_INIT_COMPLEX (PSTAT_OBJ_LOCK_TIME_COUNTERS, "Time_obj_lock_acquire_time",
				     &f_dump_in_file_Time_obj_lock_acquire_time, &f_dump_in_buffer_Time_obj_lock_acquire_time,
				     &f_load_Time_obj_lock_acquire_time, &f_dump_diff_in_file_Time_obj_lock_acquire_time_in_table_form)
};

/*
 * f_load_Num_data_page_fix_ext () - Get the number of values for Num_data_page_fix_ext statistic
 *
 */
int
f_load_Num_data_page_fix_ext (void)
{
    return PERF_PAGE_FIX_COUNTERS;
}

/*
 * f_load_Num_data_page_promote_ext () - Get the number of values for Num_data_page_promote_ext statistic
 *
 */
int
f_load_Num_data_page_promote_ext (void)
{
    return PERF_PAGE_PROMOTE_COUNTERS;
}

/*
 * f_load_Num_data_page_promote_time_ext () - Get the number of values for Num_data_page_promote_time_ext statistic
 *
 */
int
f_load_Num_data_page_promote_time_ext (void)
{
    return PERF_PAGE_PROMOTE_COUNTERS;
}

/*
 * f_load_Num_data_page_unfix_ext () - Get the number of values for Num_data_page_unfix_ext statistic
 *
 */
int
f_load_Num_data_page_unfix_ext (void)
{
    return PERF_PAGE_UNFIX_COUNTERS;
}

/*
 * f_load_Time_data_page_lock_acquire_time () - Get the number of values for Time_data_page_lock_acquire_time statistic
 *
 */
int
f_load_Time_data_page_lock_acquire_time (void)
{
    return PERF_PAGE_LOCK_TIME_COUNTERS;
}

/*
 * f_load_Time_data_page_hold_acquire_time () - Get the number of values for Time_data_page_hold_acquire_time statistic
 *
 */
int
f_load_Time_data_page_hold_acquire_time (void)
{
    return PERF_PAGE_HOLD_TIME_COUNTERS;
}

/*
 * f_load_Time_data_page_fix_acquire_time () - Get the number of values for Time_data_page_fix_acquire_time statistic
 *
 */
int
f_load_Time_data_page_fix_acquire_time (void)
{
    return PERF_PAGE_FIX_TIME_COUNTERS;
}

/*
 * f_load_Num_mvcc_snapshot_ext () - Get the number of values for Num_mvcc_snapshot_ext statistic
 *
 */
int
f_load_Num_mvcc_snapshot_ext (void)
{
    return PERF_MVCC_SNAPSHOT_COUNTERS;
}

/*
 * f_load_Time_obj_lock_acquire_time () - Get the number of values for Time_obj_lock_acquire_time statistic
 *
 */
int
f_load_Time_obj_lock_acquire_time (void)
{
    return AUX_PERF_OBJ_LOCK_STAT_COUNTERS;
}


/*
 * f_dump_in_file_Num_data_page_fix_ext () - Write in file the values for Num_data_page_fix_ext statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Num_data_page_fix_ext (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_fix_page_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Num_data_page_fix_ext_in_tabel_form (FILE *stream, const UINT64 * stats1, const UINT64 * stats2)
{
    int module;
    int page_type;
    int page_mode;
    int latch_mode;
    int cond_type;
    int offset;
    UINT64 counter1 = 0, counter2 = 0;

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
			counter1 = stats1[offset];
			counter2 = stats2[offset];
			if (counter1 == 0 && counter2 == 0)
			{
			    continue;
			}

			fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s | %10lld | %10lld | %10lld\n", perfmon_stat_module_name (module),
				 perfmon_stat_page_type_name (page_type), perfmon_stat_page_mode_name (page_mode),
				 perfmon_stat_holder_latch_name (latch_mode), perfmon_stat_cond_type_name (cond_type),
				 (long long) counter1,
				 (long long) counter2,
				 difference((long long)counter1, (long long)counter2));
		    }
		}
	    }
	}
    }
    fprintf(stream, "\n");
}

/*
 * f_dump_in_file_Num_data_page_promote_ext () - Write in file the values for Num_data_page_promote_ext statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Num_data_page_promote_ext (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_promote_page_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Num_data_page_promote_ext_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 * stats2)
{
    int module;
    int page_type;
    int promote_cond;
    int holder_latch;
    int success;
    int offset;
    UINT64 counter1 = 0, counter2 = 0;

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
			counter1 = stats1[offset];
			counter2 = stats2[offset];
			if (counter1 == 0 && counter2 == 0)
			{
			    continue;
			}

			fprintf (stream, "%-6s,%-14s,%-13s,%-5s,%-7s | %10lld | %10lld | %10lld\n", perfmon_stat_module_name (module),
				 perfmon_stat_page_type_name (page_type), perfmon_stat_promote_cond_name (promote_cond),
				 perfmon_stat_holder_latch_name (holder_latch), (success ? "SUCCESS" : "FAILED"),
				 (long long) counter1,
				 (long long) counter2,
				 difference((long long)counter1, (long long)counter2));
		    }
		}
	    }
	}
    }
    fprintf(stream, "\n");
}

/*
 * f_dump_in_file_Num_data_page_promote_time_ext () - Write in file the values for Num_data_page_promote_time_ext
 *						      statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Num_data_page_promote_time_ext (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_promote_page_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Num_data_page_promote_time_ext_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 * stats2)
{
    int module;
    int page_type;
    int promote_cond;
    int holder_latch;
    int success;
    int offset;
    UINT64 counter1 = 0, counter2 = 0;

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
			counter1 = stats1[offset];
			counter2 = stats2[offset];
			if (counter1 == 0 && counter2 == 0)
			{
			    continue;
			}

			fprintf (stream, "%-6s,%-14s,%-13s,%-5s,%-7s | %10lld | %10lld | %10lld\n", perfmon_stat_module_name (module),
				 perfmon_stat_page_type_name (page_type), perfmon_stat_promote_cond_name (promote_cond),
				 perfmon_stat_holder_latch_name (holder_latch), (success ? "SUCCESS" : "FAILED"),
				 (long long) counter1,
				 (long long) counter2,
				 difference((long long)counter1, (long long)counter2));
		    }
		}
	    }
	}
    }
    fprintf(stream, "\n");
}

/*
 * f_dump_in_file_Num_data_page_unfix_ext () - Write in file the values for Num_data_page_unfix_ext
 *					       statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Num_data_page_unfix_ext (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_unfix_page_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Num_data_page_unfix_ext_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 * stats2)
{
    int module;
    int page_type;
    int buf_dirty;
    int holder_dirty;
    int holder_latch;
    int offset;
    UINT64 counter1 = 0, counter2 = 0;

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
			counter1 = stats1[offset];
			counter2 = stats2[offset];
			if (counter1 == 0 && counter2 == 0)
			{
			    continue;
			}

			fprintf (stream, "%-6s,%-14s,%-13s,%-16s,%-5s | %10lld | %10lld | %10lld\n", perfmon_stat_module_name (module),
				 perfmon_stat_page_type_name (page_type), buf_dirty ? "BUF_DIRTY" : "BUF_NON_DIRTY",
				 holder_dirty ? "HOLDER_DIRTY" : "HOLDER_NON_DIRTY",
				 perfmon_stat_holder_latch_name (holder_latch),
				 (long long) counter1,
				 (long long) counter2,
				 difference((long long)counter1, (long long)counter2));
		    }
		}
	    }
	}
    }
    fprintf(stream, "\n");
}

/*
 * f_dump_in_file_Time_data_page_lock_acquire_time () - Write in file the values for Time_data_page_lock_acquire_time
 *					                statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Time_data_page_lock_acquire_time (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_page_lock_time_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Time_data_page_lock_acquire_time_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 * stats2)
{
    int module;
    int page_type;
    int page_mode;
    int latch_mode;
    int cond_type;
    int offset;
    UINT64 counter1 = 0, counter2 = 0;

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
			counter1 = stats1[offset];
			counter2 = stats2[offset];
			if (counter1 == 0 && counter2 == 0)
			{
			    continue;
			}

			fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s | %10lld | %10lld | %10lld\n", perfmon_stat_module_name (module),
				 perfmon_stat_page_type_name (page_type), perfmon_stat_page_mode_name (page_mode),
				 perfmon_stat_holder_latch_name (latch_mode), perfmon_stat_cond_type_name (cond_type),
				 (long long) counter1,
				 (long long) counter2,
				 difference((long long)counter1, (long long)counter2));
		    }
		}
	    }
	}
    }
    fprintf(stream, "\n");
}

/*
 * f_dump_in_file_Time_data_page_hold_acquire_time () - Write in file the values for Time_data_page_hold_acquire_time
 *					                statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Time_data_page_hold_acquire_time (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_page_hold_time_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Time_data_page_hold_acquire_time_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 *stats2)
{
    int module;
    int page_type;
    int page_mode;
    int latch_mode;
    int offset;
    UINT64 counter1 = 0, counter2 = 0;

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

		    counter1 = stats1[offset];
		    counter2 = stats2[offset];
		    if (counter1 == 0 && counter2 == 0)
		    {
			continue;
		    }

		    fprintf (stream, "%-6s,%-14s,%-18s,%-5s | %10lld | %10lld | %10lld\n", perfmon_stat_module_name (module),
			     perfmon_stat_page_type_name (page_type), perfmon_stat_page_mode_name (page_mode),
			     perfmon_stat_holder_latch_name (latch_mode),
			     (long long) counter1,
			     (long long) counter2,
			     difference((long long)counter1, (long long)counter2));
		}
	    }
	}
    }
    fprintf(stream, "\n");
}

/*
 * f_dump_in_file_Time_data_page_fix_acquire_time () - Write in file the values for Time_data_page_fix_acquire_time
 *					               statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Time_data_page_fix_acquire_time (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_page_fix_time_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Time_data_page_fix_acquire_time_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 *stats2)
{
    int module;
    int page_type;
    int page_mode;
    int latch_mode;
    int cond_type;
    int offset;
    UINT64 counter1 = 0, counter2 = 0;

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
			counter1 = stats1[offset];
			counter2 = stats2[offset];
			if (counter1 == 0 && counter2 == 0)
			{
			    continue;
			}

			fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s | %10lld | %10lld | %10lld\n", perfmon_stat_module_name (module),
				 perfmon_stat_page_type_name (page_type), perfmon_stat_page_mode_name (page_mode),
				 perfmon_stat_holder_latch_name (latch_mode), perfmon_stat_cond_type_name (cond_type),
				 (long long) counter1,
				 (long long) counter2,
				 difference((long long)counter1, (long long)counter2));
		    }
		}
	    }
	}
    }
    fprintf(stream, "\n");
}
/*
 * f_dump_in_file_Num_mvcc_snapshot_ext () - Write in file the values for Num_mvcc_snapshot_ext
 *					     statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Num_mvcc_snapshot_ext (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_mvcc_snapshot_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Num_mvcc_snapshot_ext_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 *stats2)
{
  PERF_SNAPSHOT_TYPE snapshot;
  PERF_SNAPSHOT_RECORD_TYPE rec_type;
  PERF_SNAPSHOT_VISIBILITY visibility;
  int offset;
  UINT64 counter1 = 0, counter2 = 0;

  assert (stream != NULL);
  for (snapshot = PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < PERF_SNAPSHOT_CNT; snapshot++)
    {
    for (rec_type = PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED; rec_type < PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
      {
      for (visibility = PERF_SNAPSHOT_INVISIBLE; visibility < PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
        {
        offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);

        assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);

        counter1 = stats1[offset];
        counter2 = stats2[offset];
        if (counter1 == 0 && counter2 == 0)
          {
          continue;
          }

        fprintf (stream, "%-8s,%-18s,%-9s | %10lld | %10lld | %10lld\n", perfmon_stat_snapshot_name (snapshot),
          perfmon_stat_snapshot_record_type (rec_type),
          (visibility == PERF_SNAPSHOT_INVISIBLE) ? "INVISIBLE" : "VISIBLE",
          (long long) counter1,
          (long long) counter2,
          difference((long long)counter1, (long long)counter2));
        }
      }
    }
  fprintf(stream, "\n");
}

/*
 * f_dump_in_file_Time_obj_lock_acquire_time () - Write in file the values for Time_obj_lock_acquire_time
 *						  statistic
 * f (out): File handle
 * stat_vals (in): statistics buffer
 *
 */
void
f_dump_in_file_Time_obj_lock_acquire_time (FILE * f, const UINT64 * stat_vals)
{
    perfmon_stat_dump_in_file_obj_lock_array_stat (f, stat_vals);
}

void
f_dump_diff_in_file_Time_obj_lock_acquire_time_in_table_form (FILE * stream, const UINT64 * stats1, const UINT64 *stats2)
{
	int lock_mode;
	UINT64 counter1 = 0, counter2 = 0;

	assert (stream != NULL);

	for (lock_mode = PERF_NA_LOCK; lock_mode <= PERF_SCH_M_LOCK; lock_mode++)
	{
	    counter1 = stats1[lock_mode];
	    counter2 = stats2[lock_mode];
	    if (counter1 == 0 && counter2 == 0)
	    {
		continue;
	    }

	    fprintf (stream, "%-10s | %10lld | %10lld | %10lld\n",
		     perfmon_stat_lock_mode_name (lock_mode),
		     (long long) counter1,
		     (long long) counter2,
		     difference((long long)counter1, (long long)counter2));
	}
    fprintf(stream, "\n");
}

/*
 * f_dump_in_buffer_Num_data_page_fix_ext () - Write to a buffer the values for Num_data_page_fix_ext
 *					       statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
void
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
void
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
void
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
void
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
void
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
void
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
void
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
void
f_dump_in_buffer_Num_mvcc_snapshot_ext (char **s, const UINT64 * stat_vals, int *remaining_size)
{
   perfmon_stat_dump_in_buffer_mvcc_snapshot_array_stat (stat_vals, s, remaining_size);
}

/*
 * f_dump_in_buffer_Time_obj_lock_acquire_time () - Write to a buffer the values for Time_obj_lock_acquire_time
 *						    statistic
 * s (out): Buffer to write to
 * stat_vals (in): statistics buffer
 * remaining_size (in): size of input buffer
 *
 */
void
f_dump_in_buffer_Time_obj_lock_acquire_time (char **s, const UINT64 * stat_vals, int *remaining_size)
{
    perfmon_stat_dump_in_buffer_obj_lock_array_stat (stat_vals, s, remaining_size);
}


/*
 * perfmon_stat_dump_in_buffer_fix_page_array_stat () -
 *
 * stats_ptr(in): start of array values
 * s(in/out): output string (NULL if not used)
 * remaining_size(in/out): remaining size in string s (NULL if not used)
 *
 */
void
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
void
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

			fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s = %10lld\n", perfmon_stat_module_name (module),
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
void
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

			    ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-13s,%-5s,%-7s = %10lld\n",
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
void
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

			fprintf (stream, "%-6s,%-14s,%-13s,%-5s,%-7s = %10lld\n", perfmon_stat_module_name (module),
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
void
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

			    ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-13s,%-16s,%-5s = %10lld\n",
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
void
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

			fprintf (stream, "%-6s,%-14s,%-13s,%-16s,%-5s = %10lld\n", perfmon_stat_module_name (module),
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
void
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

			    ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-18s,%-5s,%-11s = %16lld\n",
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
void
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

			fprintf (stream, "%-6s,%-14s,%-18s,%-5s,%-11s = %16lld\n", perfmon_stat_module_name (module),
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
void
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

			ret = snprintf (*s, *remaining_size, "%-6s,%-14s,%-18s,%-5s = %16lld\n",
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
void
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

		    fprintf (stream, "%-6s,%-14s,%-18s,%-5s = %16lld\n", perfmon_stat_module_name (module),
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
void
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
void
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
void
perfmon_stat_dump_in_buffer_mvcc_snapshot_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
    PERF_SNAPSHOT_TYPE snapshot;
    PERF_SNAPSHOT_RECORD_TYPE rec_type;
    PERF_SNAPSHOT_VISIBILITY visibility;
    int offset;
    UINT64 counter = 0;
    int ret;

    assert (remaining_size != NULL);
    assert (s != NULL);
    if (*s != NULL)
    {
	for (snapshot = PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < PERF_SNAPSHOT_CNT; snapshot++)
	{
	    for (rec_type = PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED; rec_type < PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
	    {
		for (visibility = PERF_SNAPSHOT_INVISIBLE; visibility < PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
		{
		    offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);

		    assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);
		    counter = stats_ptr[offset];
		    if (counter == 0)
		    {
			continue;
		    }

		    ret =
			    snprintf (*s, *remaining_size, "%-8s,%-18s,%-9s = %16lld\n", perfmon_stat_snapshot_name (snapshot),
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
void
perfmon_stat_dump_in_file_mvcc_snapshot_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
    PERF_SNAPSHOT_TYPE snapshot;
    PERF_SNAPSHOT_RECORD_TYPE rec_type;
    PERF_SNAPSHOT_VISIBILITY visibility;
    int offset;
    UINT64 counter = 0;

    assert (stream != NULL);
    for (snapshot = PERF_SNAPSHOT_SATISFIES_DELETE; snapshot < PERF_SNAPSHOT_CNT; snapshot++)
    {
	for (rec_type = PERF_SNAPSHOT_RECORD_INSERTED_VACUUMED; rec_type < PERF_SNAPSHOT_RECORD_TYPE_CNT; rec_type++)
	{
	    for (visibility = PERF_SNAPSHOT_INVISIBLE; visibility < PERF_SNAPSHOT_VISIBILITY_CNT; visibility++)
	    {
		offset = PERF_MVCC_SNAPSHOT_OFFSET (snapshot, rec_type, visibility);

		assert (offset < PERF_MVCC_SNAPSHOT_COUNTERS);

		counter = stats_ptr[offset];
		if (counter == 0)
		{
		    continue;
		}

		fprintf (stream, "%-8s,%-18s,%-9s = %16lld\n", perfmon_stat_snapshot_name (snapshot),
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
void
perfmon_stat_dump_in_buffer_obj_lock_array_stat (const UINT64 * stats_ptr, char **s, int *remaining_size)
{
    int lock_mode;
    UINT64 counter = 0;
    int ret;

    assert (remaining_size != NULL);
    assert (s != NULL);
    if (*s != NULL)
    {
	for (lock_mode = PERF_NA_LOCK; lock_mode <= PERF_SCH_M_LOCK; lock_mode++)
	{
	    counter = stats_ptr[lock_mode];
	    if (counter == 0)
	    {
		continue;
	    }

	    ret = snprintf (*s, *remaining_size, "%-10s = %16lld\n", perfmon_stat_lock_mode_name (lock_mode),
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
void
perfmon_stat_dump_in_file_obj_lock_array_stat (FILE * stream, const UINT64 * stats_ptr)
{
    int lock_mode;
    UINT64 counter = 0;

    assert (stream != NULL);

    for (lock_mode = PERF_NA_LOCK; lock_mode <= PERF_SCH_M_LOCK; lock_mode++)
    {
	counter = stats_ptr[lock_mode];
	if (counter == 0)
	{
	    continue;
	}

	fprintf (stream, "%-10s = %16lld\n", perfmon_stat_lock_mode_name (lock_mode), (long long unsigned int) counter);
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
void
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

	    ret = snprintf (*s, *remaining_size, "%-6s = %16lld\n", perfmon_stat_module_name (module),
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
void
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

	fprintf (stream, "%-6s = %16lld\n", perfmon_stat_module_name (module), (long long unsigned int) counter);
    }
}

int metadata_initialize ()
{
    int idx;
    int n_stat_values = 0;

    for (idx = 0; idx < PSTAT_COUNT; idx++)
    {
	pstat_Metadata[idx].start_offset = n_stat_values;
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
		    #if defined(SERVER_MODE)
		    ASSERT_ERROR ();
		    #endif
		    return pstat_Metadata[idx].n_vals;
		}
		/* Debug check: we should have dump/compute too. */
		assert (pstat_Metadata[idx].f_dump_in_file != NULL);
		assert (pstat_Metadata[idx].f_dump_in_buffer != NULL);
	}
	n_stat_values += pstat_Metadata[idx].n_vals;
    }

    return n_stat_values;
}

/*
 * perfmon_stat_module_name () -
 */
const char *
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

/*
 * perf_stat_page_type_name () -
 */
const char *
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
const char *
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
const char *
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
const char *
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
const char *
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
const char *
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

const char *
perfmon_stat_lock_mode_name (const int lock_mode)
{
    switch (lock_mode)
    {
	case PERF_NA_LOCK:
	    return "NA_LOCK";
	case PERF_INCON_NON_TWO_PHASE_LOCK:
	    return "INCON_2PL";
	case PERF_NULL_LOCK:
	    return "NULL_LOCK";
	case PERF_SCH_S_LOCK:
	    return "SCH_S_LOCK";
	case PERF_IS_LOCK:
	    return "IS_LOCK";
	case PERF_S_LOCK:
	    return "S_LOCK";
	case PERF_IX_LOCK:
	    return "IX_LOCK";
	case PERF_SIX_LOCK:
	    return "SIX_LOCK";
	case PERF_U_LOCK:
	    return "U_LOCK";
	case PERF_X_LOCK:
	    return "X_LOCK";
	case PERF_SCH_M_LOCK:
	    return "SCH_M_LOCK";
	default:
	    break;
    }
    return "ERROR";
}

/*
 * perfmon_stat_cond_type_name () -
 */
const char *
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
    if (timer_count != 0) {
	fprintf(stream, "Num_%-25s = %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_count);
    }

    if (timer_total != 0) {
	fprintf(stream, "Total_time_%-18s = %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_total);
    }

    if (timer_max != 0) {
	fprintf(stream, "Max_time_%-20s = %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_max);
    }

    if(timer_avg != 0) {
	fprintf(stream, "Avg_time_%-20s = %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_avg);
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

    if (timer_count1 != 0 || timer_count2 != 0) {

	fprintf(stream, "Num_%-54s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_count1,
		timer_count2,
		difference(timer_count1, timer_count2));
    }
    if (timer_total1 != 0 || timer_total2 != 0) {
	fprintf(stream, "Total_time_%-47s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_total1,
		timer_total2,
		difference(timer_total1, timer_total2));
    }
    if (timer_max1 != 0 || timer_max2 != 0) {
	fprintf(stream, "Max_time_%-49s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_max1,
		timer_max2,
		difference(timer_max1, timer_max2));
    }
    if (timer_avg1 != 0 || timer_avg2 != 0) {
	fprintf(stream, "Avg_time_%-49s | %10lld | %10lld | %10lld\n", pstat_Metadata[stat_index].stat_name,
		timer_avg1,
		timer_avg2,
		difference(timer_avg1, timer_avg2));
    }
}

long long difference (long long var1, long long var2)
{
    return (var1 - var2);
}
