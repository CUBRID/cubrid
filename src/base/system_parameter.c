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
 * system_parameter.c - system parameters
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#include <time.h>
#if defined (WINDOWS)
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif /* !WINDOWS */
#include <sys/types.h>
#include <sys/stat.h>
#if !defined(WINDOWS)
#include <sys/param.h>
#endif
#include <assert.h>
#include <ctype.h>

#include "porting.h"
#include "chartype.h"
#include "misc_string.h"
#include "error_manager.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "util_func.h"
#include "log_comm.h"
#include "log_impl.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "intl_support.h"
#include "log_manager.h"
#include "message_catalog.h"
#include "language_support.h"
#include "connection_defs.h"
#if defined (SERVER_MODE)
#include "server_support.h"
#include "boot_sr.h"
#endif /* SERVER_MODE */
#if defined (LINUX)
#include "stack_dump.h"
#endif
#include "ini_parser.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "heartbeat.h"
#include "utility.h"
#include "page_buffer.h"
#if !defined (CS_MODE)
#include "session.h"
#endif
#include "vacuum.h"


#define ER_LOG_FILE_DIR	"server"
#if !defined (CS_MODE)
static const char sysprm_error_log_file[] = "cub_server.err";
#else /* CS_MODE */
static const char sysprm_error_log_file[] = "cub_client.err";
#endif /* CS_MODE */
static const char sysprm_conf_file_name[] = "cubrid.conf";
static const char sysprm_ha_conf_file_name[] = "cubrid_ha.conf";

/*
 * System variable names
 */

#define PRM_NAME_ER_LOG_DEBUG "er_log_debug"

#define PRM_NAME_ER_BTREE_DEBUG "er_btree_debug"

#define PRM_NAME_ER_LOG_LEVEL "error_log_level"

#define PRM_NAME_ER_LOG_WARNING "error_log_warning"

#define PRM_NAME_ER_EXIT_ASK "inquire_on_exit"

#define PRM_NAME_ER_LOG_SIZE "error_log_size"

#define PRM_NAME_ER_LOG_FILE "error_log"

#define PRM_NAME_IO_LOCKF_ENABLE "file_lock"

#define PRM_NAME_SR_NBUFFERS "sort_buffer_pages"

#define PRM_NAME_SORT_BUFFER_SIZE "sort_buffer_size"

#define PRM_NAME_PB_NBUFFERS "data_buffer_pages"

#define PRM_NAME_PAGE_BUFFER_SIZE "data_buffer_size"

#define PRM_NAME_PB_BUFFER_FLUSH_RATIO "data_buffer_flush_ratio"

#define PRM_NAME_HF_UNFILL_FACTOR "unfill_factor"

#define PRM_NAME_HF_MAX_BESTSPACE_ENTRIES "max_bestspace_entries"

#define PRM_NAME_BT_UNFILL_FACTOR "index_unfill_factor"

#define PRM_NAME_BT_OID_NBUFFERS "index_scan_oid_buffer_pages"

#define PRM_NAME_BT_OID_BUFFER_SIZE "index_scan_oid_buffer_size"

#define PRM_NAME_BT_INDEX_SCAN_OID_ORDER "index_scan_in_oid_order"

#define PRM_NAME_BOSR_MAXTMP_PAGES "temp_file_max_size_in_pages"

#define PRM_NAME_LK_TIMEOUT_MESSAGE_DUMP_LEVEL "lock_timeout_message_type"

#define PRM_NAME_LK_ESCALATION_AT "lock_escalation"

#define PRM_NAME_LK_ROLLBACK_ON_LOCK_ESCALATION "rollback_on_lock_escalation"

#define PRM_NAME_LK_TIMEOUT_SECS "lock_timeout_in_secs"

#define PRM_NAME_LK_TIMEOUT "lock_timeout"

#define PRM_NAME_LK_RUN_DEADLOCK_INTERVAL "deadlock_detection_interval_in_secs"

#define PRM_NAME_LOG_NBUFFERS "log_buffer_pages"

#define PRM_NAME_LOG_BUFFER_SIZE "log_buffer_size"

#define PRM_NAME_LOG_CHECKPOINT_NPAGES "checkpoint_every_npages"

#define PRM_NAME_LOG_CHECKPOINT_SIZE "checkpoint_every_size"

/* This parameter is stored in units of sec. */
#define PRM_NAME_LOG_CHECKPOINT_INTERVAL_SECS "checkpoint_interval_in_mins"

#define PRM_NAME_LOG_CHECKPOINT_INTERVAL "checkpoint_interval"

#define PRM_NAME_LOG_CHECKPOINT_SLEEP_MSECS "checkpoint_sleep_msecs"

#define PRM_NAME_LOG_BACKGROUND_ARCHIVING "background_archiving"

#define PRM_NAME_LOG_ISOLATION_LEVEL "isolation_level"

#define PRM_NAME_LOG_MEDIA_FAILURE_SUPPORT "media_failure_support"

#define PRM_NAME_LOG_SWEEP_CLEAN "log_file_sweep_clean"

#define PRM_NAME_COMMIT_ON_SHUTDOWN "commit_on_shutdown"

#define PRM_NAME_SHUTDOWN_WAIT_TIME_IN_SECS "shutdown_wait_time_in_secs"

#define PRM_NAME_CSQL_AUTO_COMMIT "csql_auto_commit"

#define PRM_NAME_WS_HASHTABLE_SIZE "initial_workspace_table_size"

#define PRM_NAME_WS_MEMORY_REPORT "workspace_memory_report"

#define PRM_NAME_GC_ENABLE "garbage_collection"

#define PRM_NAME_TCP_PORT_ID "cubrid_port_id"

#define PRM_NAME_TCP_CONNECTION_TIMEOUT "connection_timeout"

#define PRM_NAME_OPTIMIZATION_LEVEL "optimization_level"

#define PRM_NAME_QO_DUMP "qo_dump"

#define PRM_NAME_CSS_MAX_CLIENTS "max_clients"

#define PRM_NAME_THREAD_STACKSIZE "thread_stacksize"

#define PRM_NAME_CFG_DB_HOSTS "db_hosts"

#define PRM_NAME_RESET_TR_PARSER "reset_tr_parser_interval"

#define PRM_NAME_IO_BACKUP_NBUFFERS "backup_buffer_pages"

#define PRM_NAME_IO_BACKUP_MAX_VOLUME_SIZE "backup_volume_max_size_bytes"

#define PRM_NAME_IO_BACKUP_SLEEP_MSECS "backup_sleep_msecs"

#define PRM_NAME_MAX_PAGES_IN_TEMP_FILE_CACHE "max_pages_in_temp_file_cache"

#define PRM_NAME_MAX_ENTRIES_IN_TEMP_FILE_CACHE "max_entries_in_temp_file_cache"

#define PRM_NAME_PTHREAD_SCOPE_PROCESS "pthread_scope_process"

#define PRM_NAME_TEMP_MEM_BUFFER_PAGES "temp_file_memory_size_in_pages"

#define PRM_NAME_INDEX_SCAN_KEY_BUFFER_PAGES "index_scan_key_buffer_pages"

#define PRM_NAME_INDEX_SCAN_KEY_BUFFER_SIZE "index_scan_key_buffer_size"

#define PRM_NAME_DONT_REUSE_HEAP_FILE "dont_reuse_heap_file"

#define PRM_NAME_QUERY_MODE_SYNC "dont_use_async_query"

#define PRM_NAME_INSERT_MODE "insert_execution_mode"

#define PRM_NAME_LK_MAX_SCANID_BIT "max_index_scan_count"

#define PRM_NAME_HOSTVAR_LATE_BINDING "hostvar_late_binding"

#define PRM_NAME_ENABLE_HISTO "communication_histogram"

#define PRM_NAME_MUTEX_BUSY_WAITING_CNT "mutex_busy_waiting_cnt"

#define PRM_NAME_PB_NUM_LRU_CHAINS "num_LRU_chains"

#define PRM_NAME_PAGE_BG_FLUSH_INTERVAL_MSECS "page_flush_interval_in_msecs"

#define PRM_NAME_PAGE_BG_FLUSH_INTERVAL "page_flush_interval"

#define PRM_NAME_ADAPTIVE_FLUSH_CONTROL "adaptive_flush_control"

#define PRM_NAME_MAX_FLUSH_PAGES_PER_SECOND "max_flush_pages_per_second"

#define PRM_NAME_MAX_FLUSH_SIZE_PER_SECOND "max_flush_size_per_second"

#define PRM_NAME_PB_SYNC_ON_NFLUSH "sync_on_nflush"

#define PRM_NAME_PB_SYNC_ON_FLUSH_SIZE "sync_on_flush_size"

#define PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL "page_validation_level"

#define PRM_NAME_ORACLE_STYLE_OUTERJOIN "oracle_style_outerjoin"

#define PRM_NAME_COMPAT_MODE "compat_mode"

#define PRM_NAME_ANSI_QUOTES "ansi_quotes"

#define PRM_NAME_DEFAULT_WEEK_FORMAT "default_week_format"

#define PRM_NAME_TEST_MODE "test_mode"

#define PRM_NAME_ONLY_FULL_GROUP_BY "only_full_group_by"

#define PRM_NAME_PIPES_AS_CONCAT "pipes_as_concat"

#define PRM_NAME_MYSQL_TRIGGER_CORRELATION_NAMES "mysql_trigger_correlation_names"

#define PRM_NAME_REQUIRE_LIKE_ESCAPE_CHARACTER "require_like_escape_character"

#define PRM_NAME_NO_BACKSLASH_ESCAPES "no_backslash_escapes"

#define PRM_NAME_GROUP_CONCAT_MAX_LEN "group_concat_max_len"

#define PRM_NAME_STRING_MAX_SIZE_BYTES "string_max_size_bytes"

#define PRM_NAME_ADD_COLUMN_UPDATE_HARD_DEFAULT "add_column_update_hard_default"

#define PRM_NAME_RETURN_NULL_ON_FUNCTION_ERRORS "return_null_on_function_errors"

#define PRM_NAME_PLUS_AS_CONCAT "plus_as_concat"

#define PRM_NAME_ALTER_TABLE_CHANGE_TYPE_STRICT "alter_table_change_type_strict"

#define PRM_NAME_COMPACTDB_PAGE_RECLAIM_ONLY "compactdb_page_reclaim_only"

#define PRM_NAME_LIKE_TERM_SELECTIVITY "like_term_selectivity"

#define PRM_NAME_MAX_OUTER_CARD_OF_IDXJOIN "max_outer_card_of_idxjoin"

#define PRM_NAME_ORACLE_STYLE_EMPTY_STRING "oracle_style_empty_string"

#define PRM_NAME_SUPPRESS_FSYNC "suppress_fsync"

#define PRM_NAME_CALL_STACK_DUMP_ON_ERROR "call_stack_dump_on_error"

#define PRM_NAME_CALL_STACK_DUMP_ACTIVATION "call_stack_dump_activation_list"

#define PRM_NAME_CALL_STACK_DUMP_DEACTIVATION "call_stack_dump_deactivation_list"

#define PRM_NAME_COMPAT_NUMERIC_DIVISION_SCALE "compat_numeric_division_scale"

#define PRM_NAME_DBFILES_PROTECT "dbfiles_protect"

#define PRM_NAME_AUTO_RESTART_SERVER "auto_restart_server"

#define PRM_NAME_XASL_MAX_PLAN_CACHE_ENTRIES "max_plan_cache_entries"

#if defined (ENABLE_UNUSED_FUNCTION)
#define PRM_NAME_XASL_MAX_PLAN_CACHE_CLONES "max_plan_cache_clones"
#endif /* ENABLE_UNUSED_FUNCTION */

#define PRM_NAME_FILTER_PRED_MAX_CACHE_ENTRIES "max_filter_pred_cache_entries"

#define PRM_NAME_FILTER_PRED_MAX_CACHE_CLONES "max_filter_pred_cache_clones"

#define PRM_NAME_XASL_PLAN_CACHE_TIMEOUT "plan_cache_timeout"

#define PRM_NAME_LIST_QUERY_CACHE_MODE "query_cache_mode"

#define PRM_NAME_LIST_MAX_QUERY_CACHE_ENTRIES "max_query_cache_entries"

#define PRM_NAME_LIST_MAX_QUERY_CACHE_PAGES "query_cache_size_in_pages"

#define PRM_NAME_USE_ORDERBY_SORT_LIMIT  "use_orderby_sort_limit"

#define PRM_NAME_REPLICATION_MODE "replication"

#define PRM_NAME_HA_MODE "ha_mode"

#define PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY "ha_mode_for_sa_utils_only"

#define PRM_NAME_HA_SERVER_STATE "ha_server_state"

#define PRM_NAME_HA_LOG_APPLIER_STATE "ha_log_applier_state"

#define PRM_NAME_HA_NODE_LIST "ha_node_list"

#define PRM_NAME_HA_REPLICA_LIST "ha_replica_list"

#define PRM_NAME_HA_DB_LIST "ha_db_list"

#define PRM_NAME_HA_COPY_LOG_BASE "ha_copy_log_base"

#define PRM_NAME_HA_COPY_SYNC_MODE "ha_copy_sync_mode"

#define PRM_NAME_HA_APPLY_MAX_MEM_SIZE "ha_apply_max_mem_size"

#define PRM_NAME_HA_PORT_ID "ha_port_id"

#define PRM_NAME_HA_INIT_TIMER_IN_MSECS "ha_init_timer_in_msec"

#define PRM_NAME_HA_HEARTBEAT_INTERVAL_IN_MSECS "ha_heartbeat_interval_in_msecs"

#define PRM_NAME_HA_CALC_SCORE_INTERVAL_IN_MSECS "ha_calc_score_interval_in_msecs"

#define PRM_NAME_HA_FAILOVER_WAIT_TIME_IN_MSECS "ha_failover_wait_time_in_msecs"

#define PRM_NAME_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS "ha_process_start_confirm_interval_in_msecs"

#define PRM_NAME_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS "ha_process_dereg_confirm_interval_in_msecs"

#define PRM_NAME_HA_MAX_PROCESS_START_CONFIRM "ha_max_process_start_confirm"

#define PRM_NAME_HA_MAX_PROCESS_DEREG_CONFIRM "ha_max_process_dereg_confirm"

#define PRM_NAME_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF "ha_unacceptable_proc_restart_timediff"

#define PRM_NAME_HA_CHANGEMODE_INTERVAL_IN_MSEC "ha_changemode_interval_in_msecs"

#define PRM_NAME_HA_MAX_HEARTBEAT_GAP "ha_max_heartbeat_gap"

#define PRM_NAME_HA_PING_HOSTS "ha_ping_hosts"

#define PRM_NAME_HA_APPLYLOGDB_RETRY_ERROR_LIST "ha_applylogdb_retry_error_list"

#define PRM_NAME_HA_APPLYLOGDB_IGNORE_ERROR_LIST "ha_applylogdb_ignore_error_list"

#define PRM_NAME_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS "ha_applylogdb_log_wait_time_in_secs"

#define PRM_NAME_HA_SQL_LOGGING "ha_enable_sql_logging"

#define PRM_NAME_HA_SQL_LOG_MAX_SIZE_IN_MB "ha_sql_log_max_size_in_mbytes"

#define PRM_NAME_HA_COPY_LOG_MAX_ARCHIVES "ha_copy_log_max_archives"

#define PRM_NAME_HA_COPY_LOG_TIMEOUT "ha_copy_log_timeout"

#define PRM_NAME_HA_REPLICA_DELAY "ha_replica_delay"

#define PRM_NAME_HA_REPLICA_TIME_BOUND "ha_replica_time_bound"

#define PRM_NAME_HA_DELAY_LIMIT "ha_delay_limit"

#define PRM_NAME_HA_DELAY_LIMIT_DELTA "ha_delay_limit_delta"

#define PRM_NAME_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS "ha_applylogdb_max_commit_interval_in_msecs"

#define PRM_NAME_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL "ha_applylogdb_max_commit_interval"

#define PRM_NAME_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS "ha_check_disk_failure_interval"

#define PRM_NAME_JAVA_STORED_PROCEDURE "java_stored_procedure"

#define PRM_NAME_COMPAT_PRIMARY_KEY "compat_primary_key"

#define PRM_NAME_INTL_MBS_SUPPORT "intl_mbs_support"

#define PRM_NAME_LOG_HEADER_FLUSH_INTERVAL "log_header_flush_interval_in_secs"

#define PRM_NAME_LOG_ASYNC_COMMIT "async_commit"

#define PRM_NAME_LOG_GROUP_COMMIT_INTERVAL_MSECS "group_commit_interval_in_msecs"

#define PRM_NAME_LOG_BG_FLUSH_INTERVAL_MSECS "log_flush_interval_in_msecs"

#define PRM_NAME_LOG_BG_FLUSH_NUM_PAGES "log_flush_every_npages"

#define PRM_NAME_LOG_COMPRESS "log_compress"

#define PRM_NAME_BLOCK_NOWHERE_STATEMENT "block_nowhere_statement"

#define PRM_NAME_BLOCK_DDL_STATEMENT "block_ddl_statement"

#define PRM_NAME_SINGLE_BYTE_COMPARE "single_byte_compare"

#define PRM_NAME_CSQL_HISTORY_NUM "csql_history_num"

#define PRM_NAME_LOG_TRACE_DEBUG "log_trace_debug"

#define PRM_NAME_DL_FORK "dl_fork"

#define PRM_NAME_ER_PRODUCTION_MODE "error_log_production_mode"

#define PRM_NAME_ER_STOP_ON_ERROR "stop_on_error"

#define PRM_NAME_TCP_RCVBUF_SIZE "tcp_rcvbuf_size"

#define PRM_NAME_TCP_SNDBUF_SIZE "tcp_sndbuf_size"

#define PRM_NAME_TCP_NODELAY "tcp_nodelay"

#define PRM_NAME_TCP_KEEPALIVE "tcp_keepalive"

#define PRM_NAME_CSQL_SINGLE_LINE_MODE "csql_single_line_mode"

#define PRM_NAME_XASL_DEBUG_DUMP "xasl_debug_dump"

#define PRM_NAME_LOG_MAX_ARCHIVES "log_max_archives"

#define PRM_NAME_FORCE_REMOVE_LOG_ARCHIVES "force_remove_log_archives"

#define PRM_NAME_REMOVE_LOG_ARCHIVES_INTERVAL "remove_log_archive_interval_in_secs"

#define PRM_NAME_LOG_NO_LOGGING "no_logging"

#define PRM_NAME_UNLOADDB_IGNORE_ERROR "unload_ignore_error"

#define PRM_NAME_UNLOADDB_LOCK_TIMEOUT "unload_lock_timeout"

#define PRM_NAME_LOADDB_FLUSH_INTERVAL "load_flush_interval"

#define PRM_NAME_IO_TEMP_VOLUME_PATH "temp_volume_path"

#define PRM_NAME_IO_VOLUME_EXT_PATH "volume_extension_path"

#define PRM_NAME_UNIQUE_ERROR_KEY_VALUE "print_key_value_on_unique_error"

#define PRM_NAME_USE_SYSTEM_MALLOC "use_system_malloc"

#define PRM_NAME_EVENT_HANDLER "event_handler"

#define PRM_NAME_EVENT_ACTIVATION "event_activation_list"

#define PRM_NAME_READ_ONLY_MODE "read_only"

#define PRM_NAME_MNT_WAITING_THREAD "monitor_waiting_thread"

#define PRM_NAME_MNT_STATS_THRESHOLD "monitor_stats_threshold"

#define PRM_NAME_SERVICE_SERVICE_LIST "service::service"

#define PRM_NAME_SERVICE_SERVER_LIST "service::server"

#define PRM_NAME_SESSION_STATE_TIMEOUT "session_state_timeout"

#define PRM_NAME_MULTI_RANGE_OPT_LIMIT "multi_range_optimization_limit"

#define PRM_NAME_ACCESS_IP_CONTROL "access_ip_control"

#define PRM_NAME_ACCESS_IP_CONTROL_FILE "access_ip_control_file"

#define PRM_NAME_DB_VOLUME_SIZE "db_volume_size"

#define PRM_NAME_LOG_VOLUME_SIZE "log_volume_size"

#define PRM_NAME_INTL_NUMBER_LANG "intl_number_lang"

#define PRM_NAME_INTL_DATE_LANG "intl_date_lang"

#define PRM_NAME_UNICODE_INPUT_NORMALIZATION "unicode_input_normalization"

#define PRM_NAME_UNICODE_OUTPUT_NORMALIZATION "unicode_output_normalization"

#define PRM_NAME_INTL_CHECK_INPUT_STRING "intl_check_input_string"

#define PRM_NAME_CHECK_PEER_ALIVE "check_peer_alive"

#define PRM_NAME_SQL_TRACE_EXECUTION_PLAN "sql_trace_execution_plan"

#define PRM_NAME_SQL_TRACE_SLOW_MSECS "sql_trace_slow_msecs"

#define PRM_NAME_SQL_TRACE_SLOW "sql_trace_slow"

#define PRM_NAME_LOG_TRACE_FLUSH_TIME "log_trace_flush_time"

#define PRM_NAME_INTL_COLLATION "intl_collation"

#define PRM_NAME_GENERIC_VOL_PREALLOC_SIZE "generic_vol_prealloc_size"

#define PRM_NAME_SORT_LIMIT_MAX_COUNT "sort_limit_max_count"

#define PRM_NAME_SQL_TRACE_IOREADS "sql_trace_ioread_pages"

/* For query profile. Internal use only. */
#define PRM_NAME_QUERY_TRACE "query_trace"
#define PRM_NAME_QUERY_TRACE_FORMAT "query_trace_format"

#define PRM_NAME_MAX_RECURSION_SQL_DEPTH "max_recursion_sql_depth"

#define PRM_NAME_UPDATE_USE_ATTRIBUTE_REFERENCES "update_use_attribute_references"

#define PRM_NAME_PB_AIN_RATIO "data_ain_ratio"
#define PRM_NAME_PB_AOUT_RATIO "data_aout_ratio"

#define PRM_NAME_MAX_AGG_HASH_SIZE "max_agg_hash_size"
#define PRM_NAME_AGG_HASH_RESPECT_ORDER "agg_hash_respect_order"

#define PRM_NAME_USE_BTREE_FENCE_KEY "use_btree_fence_key"

#define PRM_NAME_QA_BTREE_RANDOM_EXIT "qa_btree_random_exit"

#define PRM_NAME_OPTIMIZER_ENABLE_MERGE_JOIN "optimizer_enable_merge_join"
#define PRM_NAME_OPTIMIZER_RESERVE_01 "optimizer_reserve_01"
#define PRM_NAME_OPTIMIZER_RESERVE_02 "optimizer_reserve_02"
#define PRM_NAME_OPTIMIZER_RESERVE_03 "optimizer_reserve_03"
#define PRM_NAME_OPTIMIZER_RESERVE_04 "optimizer_reserve_04"
#define PRM_NAME_OPTIMIZER_RESERVE_05 "optimizer_reserve_05"
#define PRM_NAME_OPTIMIZER_RESERVE_06 "optimizer_reserve_06"
#define PRM_NAME_OPTIMIZER_RESERVE_07 "optimizer_reserve_07"
#define PRM_NAME_OPTIMIZER_RESERVE_08 "optimizer_reserve_08"
#define PRM_NAME_OPTIMIZER_RESERVE_09 "optimizer_reserve_09"
#define PRM_NAME_OPTIMIZER_RESERVE_10 "optimizer_reserve_10"
#define PRM_NAME_OPTIMIZER_RESERVE_11 "optimizer_reserve_11"
#define PRM_NAME_OPTIMIZER_RESERVE_12 "optimizer_reserve_12"
#define PRM_NAME_OPTIMIZER_RESERVE_13 "optimizer_reserve_13"
#define PRM_NAME_OPTIMIZER_RESERVE_14 "optimizer_reserve_14"
#define PRM_NAME_OPTIMIZER_RESERVE_15 "optimizer_reserve_15"
#define PRM_NAME_OPTIMIZER_RESERVE_16 "optimizer_reserve_16"
#define PRM_NAME_OPTIMIZER_RESERVE_17 "optimizer_reserve_17"
#define PRM_NAME_OPTIMIZER_RESERVE_18 "optimizer_reserve_18"
#define PRM_NAME_OPTIMIZER_RESERVE_19 "optimizer_reserve_19"
#define PRM_NAME_OPTIMIZER_RESERVE_20 "optimizer_reserve_20"

#define PRM_NAME_HA_REPL_ENABLE_SERVER_SIDE_UPDATE "ha_repl_enable_server_side_update"
#define PRM_NAME_PB_LRU_HOT_RATIO "lru_hot_ratio"

#define PRM_NAME_HA_PREFETCHLOGDB_ENABLE "ha_prefetchlogdb_enable"
#define PRM_NAME_HA_PREFETCHLOGDB_MAX_THREAD_COUNT "ha_prefetchlogdb_max_thread_count"
#define PRM_NAME_HA_PREFETCHLOGDB_PAGE_DISTANCE "ha_prefetchlogdb_page_distance"
#define PRM_NAME_HA_PREFETCHLOGDB_MAX_PAGE_COUNT "ha_prefetchlogdb_max_page_count"

#define PRM_NAME_MVCC_ENABLED "mvcc_enabled"

#define PRM_NAME_VACUUM_MASTER_WAKEUP_INTERVAL "vacuum_master_interval_in_secs"

#define PRM_NAME_VACUUM_DATA_PAGES "vacuum_data_pages"
#define PRM_NAME_VACUUM_LOG_BLOCK_PAGES "vacuum_log_block_pages"
#define PRM_NAME_VACUUM_WORKER_COUNT "vacuum_worker_count"

#define PRM_NAME_DISABLE_VACUUM "vacuum_disable"

#define PRM_NAME_ER_LOG_VACUUM "er_log_vacuum"

#define PRM_NAME_LOG_BTREE_OPS "log_btree_operations"

#define PRM_NAME_OBJECT_PRINT_FORMAT_OID "print_object_as_oid"

/*
 * Note about ERROR_LIST and INTEGER_LIST type
 * ERROR_LIST type is an array of bool type with the size of -(ER_LAST_ERROR)
 * INTEGER_LIST type is an array of int type where the first element is
 * the size of the array. The max size of INTEGER_LIST is 255.
 */

/*
 * Bit masks for flag representing status words
 */

/*
 * Static flags
 */
#define PRM_EMPTY_FLAG	    0x00000000	/* empty flag */
#define PRM_USER_CHANGE     0x00000001	/* user can change, not implemented */
#define PRM_FOR_CLIENT      0x00000002	/* is for client parameter */
#define PRM_FOR_SERVER      0x00000004	/* is for server parameter */
#define PRM_HIDDEN          0x00000008	/* is hidden */
#define PRM_RELOADABLE      0x00000010	/* is reloadable */
#define PRM_COMPOUND        0x00000020	/* sets the value of several others */
#define PRM_TEST_CHANGE     0x00000040	/* can only be changed in the test mode */
#define PRM_FOR_HA          0x00000080	/* is for heartbeat */
#define PRM_FOR_SESSION	    0x00000100	/* is a session parameter - all client
					 * or client/server parameters that
					 * can be changed on-line
					 */
#define PRM_FORCE_SERVER    0x00000200	/* client should get value from server */
#define PRM_FOR_QRY_STRING  0x00000400	/* if a parameter can affect the plan
					 * generation it should be included
					 * in the query string
					 */
#define PRM_CLIENT_SESSION  0x00000800	/* mark those client/server session
					 * parameters that should not affect
					 * the server
					 */
#define PRM_SIZE_UNIT       0x00001000	/* has size unit interface */
#define PRM_TIME_UNIT       0x00002000	/* has time unit interface */
#define PRM_DIFFER_UNIT     0x00004000	/* parameter unit need to be changed */
#define PRM_FOR_HA_CONTEXT  0x00008000	/* should be replicated into HA log
					 * applier */

#define PRM_GET_SERVER      0x00010000	/* return the value of server parameter 
					 * from client/server parameter.  
					 * Note that this flag only can be set 
					 * if the parameter has PRM_FOR_CLIENT, 
					 * PRM_FOR_SERVER, and PRM_USER_CHANGE 
					 * flags.
					 */

#define PRM_DEPRECATED      0x40000000	/* is deprecated */
#define PRM_OBSOLETED       0x80000000	/* is obsoleted */

/*
 * Dynamic flags
 */
#define PRM_SET             0x00000001	/* has been set */
#define PRM_ALLOCATED       0x00000002	/* storage has been malloc'd */
#define PRM_DEFAULT_USED    0x00000004	/* Default value has been used */
#define PRM_DIFFERENT	    0x00000008	/* mark those parameters that have
					 * values different than their default.
					 * currently used by parameters that
					 * should be printed to query string
					 */

/*
 * Macros to get data type
 */
#define PRM_IS_STRING(x)          ((x)->datatype == PRM_STRING)
#define PRM_IS_INTEGER(x)         ((x)->datatype == PRM_INTEGER)
#define PRM_IS_FLOAT(x)           ((x)->datatype == PRM_FLOAT)
#define PRM_IS_BOOLEAN(x)         ((x)->datatype == PRM_BOOLEAN)
#define PRM_IS_KEYWORD(x)         ((x)->datatype == PRM_KEYWORD)
#define PRM_IS_INTEGER_LIST(x)    ((x)->datatype == PRM_INTEGER_LIST)
#define PRM_IS_BIGINT(x)          ((x)->datatype == PRM_BIGINT)

/*
 * Macros to access bit fields
 */

#define PRM_USER_CAN_CHANGE(x)    (x & PRM_USER_CHANGE)
#define PRM_IS_FOR_CLIENT(x)      (x & PRM_FOR_CLIENT)
#define PRM_IS_FOR_SERVER(x)      (x & PRM_FOR_SERVER)
#define PRM_IS_HIDDEN(x)          (x & PRM_HIDDEN)
#define PRM_IS_RELOADABLE(x)      (x & PRM_RELOADABLE)
#define PRM_IS_COMPOUND(x)        (x & PRM_COMPOUND)
#define PRM_TEST_CHANGE_ONLY(x)   (x & PRM_TEST_CHANGE)
#define PRM_IS_FOR_HA(x)          (x & PRM_FOR_HA)
#define PRM_IS_FOR_SESSION(x)	  (x & PRM_FOR_SESSION)
#define PRM_GET_FROM_SERVER(x)	  (x & PRM_FORCE_SERVER)
#define PRM_IS_FOR_QRY_STRING(x)  (x & PRM_FOR_QRY_STRING)
#define PRM_CLIENT_SESSION_ONLY(x) (x & PRM_CLIENT_SESSION)
#define PRM_HAS_SIZE_UNIT(x)      (x & PRM_SIZE_UNIT)
#define PRM_HAS_TIME_UNIT(x)      (x & PRM_TIME_UNIT)
#define PRM_DIFFERENT_UNIT(x)     (x & PRM_DIFFER_UNIT)
#define PRM_IS_FOR_HA_CONTEXT(x)  (x & PRM_FOR_HA_CONTEXT)
#define PRM_IS_GET_SERVER(x)      (x & PRM_GET_SERVER)
#define PRM_IS_DEPRECATED(x)      (x & PRM_DEPRECATED)
#define PRM_IS_OBSOLETED(x)       (x & PRM_OBSOLETED)

#define PRM_IS_SET(x)             (x & PRM_SET)
#define PRM_IS_ALLOCATED(x)       (x & PRM_ALLOCATED)
#define PRM_DEFAULT_VAL_USED(x)   (x & PRM_DEFAULT_USED)
#define PRM_IS_DIFFERENT(x)	  (x & PRM_DIFFERENT)

/*
 * Macros to manipulate bit fields
 */

#define PRM_CLEAR_BIT(this, here)  (here &= ~this)
#define PRM_SET_BIT(this, here)    (here |= this)

/*
 * Macros to get values
 */

#define PRM_GET_INT(x)      (*((int *) (x)))
#define PRM_GET_FLOAT(x)    (*((float *) (x)))
#define PRM_GET_STRING(x)   (*((char **) (x)))
#define PRM_GET_BOOL(x)     (*((bool *) (x)))
#define PRM_GET_INTEGER_LIST(x) (*((int **) (x)))
#define PRM_GET_BIGINT(x)     (*((UINT64 *) (x)))

/*
 * Macros to call functions
 */

#define PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER(prm,out_val,in_val,err) do {	\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (int));				\
	assert (sizeof (*(in_val)) == sizeof (UINT64));				\
	*(err) = (*((prm)->set_dup)) ((void *) (out_val), PRM_INTEGER, 		\
				(void *) (in_val), PRM_BIGINT);			\
} while (0)

#define PRM_ADJUST_FOR_SET_BIGINT_TO_FLOAT(prm,out_val,in_val,err) do {		\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (float));				\
	assert (sizeof (*(in_val)) == sizeof (UINT64));				\
	*(err) = (*((prm)->set_dup)) ((void *) (out_val), PRM_FLOAT, 		\
				(void *) (in_val), PRM_BIGINT);			\
} while (0)

#define PRM_ADJUST_FOR_SET_INTEGER_TO_INTEGER(prm,out_val,in_val,err) do {	\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (int));				\
	assert (sizeof (*(in_val)) == sizeof (int));				\
	*(err) = (*((prm)->set_dup)) ((void *) (out_val), PRM_INTEGER, 		\
				(void *) (in_val), PRM_INTEGER);		\
} while (0)

#define PRM_ADJUST_FOR_SET_FLOAT_TO_FLOAT(prm,out_val,in_val,err) do {		\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (float));				\
	assert (sizeof (*(in_val)) == sizeof (float));				\
	*(err) = (*((prm)->set_dup)) ((void *) (out_val), PRM_FLOAT, 		\
				(void *) (in_val), PRM_FLOAT);			\
} while (0)

#define PRM_ADJUST_FOR_SET_BIGINT_TO_BIGINT(prm,out_val,in_val,err) do {	\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (UINT64));			\
	assert (sizeof (*(in_val)) == sizeof (UINT64));				\
	*(err) = (*((prm)->set_dup)) ((void *) (out_val), PRM_BIGINT, 		\
				(void *) (in_val), PRM_BIGINT);			\
} while (0)

#define PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT(prm,out_val,in_val,err) do {	\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (UINT64));			\
	assert (sizeof (*(in_val)) == sizeof (int));				\
	*(err) = (*((prm)->get_dup)) ((void *) (out_val), PRM_BIGINT, 		\
				(void *) (in_val), PRM_INTEGER);		\
} while (0)

#define PRM_ADJUST_FOR_GET_FLOAT_TO_BIGINT(prm,out_val,in_val,err) do {		\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (UINT64));			\
	assert (sizeof (*(in_val)) == sizeof (float));				\
	*(err) = (*((prm)->get_dup)) ((void *) (out_val), PRM_BIGINT, 		\
				(void *) (in_val), PRM_FLOAT);			\
} while (0)

#define PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER(prm,out_val,in_val,err) do {	\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (int));				\
	assert (sizeof (*(in_val)) == sizeof (int));				\
	*(err) = (*((prm)->get_dup)) ((void *) (out_val), PRM_INTEGER, 		\
				(void *) (in_val), PRM_INTEGER);		\
} while (0)

#define PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT(prm,out_val,in_val,err) do {		\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (float));				\
	assert (sizeof (*(in_val)) == sizeof (float));				\
	*(err) = (*((prm)->get_dup)) ((void *) (out_val), PRM_FLOAT, 		\
				(void *) (in_val), PRM_FLOAT);			\
} while (0)

#define PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT(prm,out_val,in_val,err) do {	\
	assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);	\
	assert (sizeof (*(out_val)) == sizeof (UINT64));			\
	assert (sizeof (*(in_val)) == sizeof (UINT64));				\
	*(err) = (*((prm)->get_dup)) ((void *) (out_val), PRM_BIGINT, 		\
				(void *) (in_val), PRM_BIGINT);			\
} while (0)


/*
 * Other macros
 */
#define PRM_DEFAULT_BUFFER_SIZE 256

/* initial error and integer lists */
static int int_list_initial[1] = { 0 };

/*
 * Global variables of parameters' value
 * Default values for the parameters
 * Upper and lower bounds for the parameters
 */
bool PRM_ER_LOG_DEBUG = false;
#if !defined(NDEBUG)
static bool prm_er_log_debug_default = true;
#else /* !NDEBUG */
static bool prm_er_log_debug_default = false;
#endif /* !NDEBUG */
static unsigned int prm_er_log_debug_flag = 0;

int PRM_ER_BTREE_DEBUG = 0;
static int prm_er_btree_debug_default = 0;
static unsigned int prm_er_btree_debug_flag = 0;

int PRM_ER_LOG_LEVEL = ER_SYNTAX_ERROR_SEVERITY;
static int prm_er_log_level_default = ER_SYNTAX_ERROR_SEVERITY;
static int prm_er_log_level_lower = ER_FATAL_ERROR_SEVERITY;
static int prm_er_log_level_upper = ER_NOTIFICATION_SEVERITY;
static unsigned int prm_er_log_level_flag = 0;

bool PRM_ER_LOG_WARNING = false;
static bool prm_er_log_warning_default = false;
static unsigned int prm_er_log_warning_flag = 0;

int PRM_ER_EXIT_ASK = ER_EXIT_DEFAULT;
static int prm_er_exit_ask_default = ER_EXIT_DEFAULT;
static unsigned int prm_er_exit_ask_flag = 0;

int PRM_ER_LOG_SIZE = (100000 * 80L);
static int prm_er_log_size_default = (100000 * 80L);
static int prm_er_log_size_lower = (100 * 80);
static unsigned int prm_er_log_size_flag = 0;

const char *PRM_ER_LOG_FILE = sysprm_error_log_file;
static const char *prm_er_log_file_default = sysprm_error_log_file;
static unsigned int prm_er_log_file_flag = 0;

bool PRM_ACCESS_IP_CONTROL = false;
static bool prm_access_ip_control_default = false;
static unsigned int prm_access_ip_control_flag = 0;

const char *PRM_ACCESS_IP_CONTROL_FILE = "";
static const char *prm_access_ip_control_file_default = "";
static unsigned int prm_access_ip_control_file_flag = 0;

bool PRM_IO_LOCKF_ENABLE = false;
static bool prm_io_lockf_enable_default = true;
static unsigned int prm_io_lockf_enable_flag = 0;

int PRM_SR_NBUFFERS = 128;
static int prm_sr_nbuffers_default = 128;
static int prm_sr_nbuffers_lower = 1;
static unsigned int prm_sr_nbuffers_flag = 0;

int PRM_PB_NBUFFERS = 32768;
static int prm_pb_nbuffers_default = 32768;
static int prm_pb_nbuffers_lower = 1024;
static unsigned int prm_pb_nbuffers_flag = 0;

float PRM_PB_BUFFER_FLUSH_RATIO = 0.01f;
static float prm_pb_buffer_flush_ratio_default = 0.01f;
static float prm_pb_buffer_flush_ratio_lower = 0.01f;
static float prm_pb_buffer_flush_ratio_upper = 0.95f;
static unsigned int prm_pb_buffer_flush_ratio_flag = 0;

float PRM_HF_UNFILL_FACTOR = 0.10f;
static float prm_hf_unfill_factor_default = 0.10f;
static float prm_hf_unfill_factor_lower = 0.0f;
static float prm_hf_unfill_factor_upper = 0.3f;
static unsigned int prm_hf_unfill_factor_flag = 0;

int PRM_HF_MAX_BESTSPACE_ENTRIES = 1000000;
static int prm_hf_max_bestspace_entries_default = 1000000;	/* 110 M */
static unsigned int prm_hf_max_bestspace_entries_flag = 0;

float PRM_BT_UNFILL_FACTOR = 0.05f;
static float prm_bt_unfill_factor_default = 0.05f;
static float prm_bt_unfill_factor_lower = 0.0f;
static float prm_bt_unfill_factor_upper = 0.5f;
static unsigned int prm_bt_unfill_factor_flag = 0;

float PRM_BT_OID_NBUFFERS = 4.0f;
static float prm_bt_oid_nbuffers_default = 4.0f;
static float prm_bt_oid_nbuffers_lower = 0.049999f;
static float prm_bt_oid_nbuffers_upper = 16.0f;
static unsigned int prm_bt_oid_nbuffers_flag = 0;

bool PRM_BT_INDEX_SCAN_OID_ORDER = false;
static bool prm_bt_index_scan_oid_order_default = false;
static unsigned int prm_bt_index_scan_oid_order_flag = 0;

int PRM_BOSR_MAXTMP_PAGES = INT_MIN;
static int prm_bosr_maxtmp_pages = -1;	/* Infinite */
static unsigned int prm_bosr_maxtmp_flag = 0;

int PRM_LK_TIMEOUT_MESSAGE_DUMP_LEVEL = 0;
static unsigned int prm_lk_timeout_message_dump_level_flag = 0;

int PRM_LK_ESCALATION_AT = 100000;
static int prm_lk_escalation_at_default = 100000;
static int prm_lk_escalation_at_lower = 5;
static unsigned int prm_lk_escalation_at_flag = 0;

bool PRM_LK_ROLLBACK_ON_LOCK_ESCALATION = false;
static bool prm_lk_rollback_on_lock_escalation_default = false;
static unsigned int prm_lk_rollback_on_lock_escalation_flag = 0;

int PRM_LK_TIMEOUT_SECS = -1;
static int prm_lk_timeout_secs_default = -1;	/* Infinite */
static int prm_lk_timeout_secs_lower = -1;
static unsigned int prm_lk_timeout_secs_flag = 0;

float PRM_LK_RUN_DEADLOCK_INTERVAL = 1.0f;
static float prm_lk_run_deadlock_interval_default = 1.0f;
static float prm_lk_run_deadlock_interval_lower = 0.1f;
static unsigned int prm_lk_run_deadlock_interval_flag = 0;

int PRM_LOG_NBUFFERS = LOGPB_BUFFER_NPAGES_LOWER;
static int prm_log_nbuffers_default = LOGPB_BUFFER_NPAGES_LOWER;
static int prm_log_nbuffers_lower = LOGPB_BUFFER_NPAGES_LOWER;
static unsigned int prm_log_nbuffers_flag = 0;

int PRM_LOG_CHECKPOINT_NPAGES = 100000;
static int prm_log_checkpoint_npages_default = 100000;
static int prm_log_checkpoint_npages_lower = 10;
static unsigned int prm_log_checkpoint_npages_flag = 0;

int PRM_LOG_CHECKPOINT_INTERVAL_SECS = 360;
static int prm_log_checkpoint_interval_secs_default = 360;
static int prm_log_checkpoint_interval_secs_lower = 60;
static unsigned int prm_log_checkpoint_interval_secs_flag = 0;

int PRM_LOG_CHECKPOINT_SLEEP_MSECS = 1;
static int prm_log_checkpoint_sleep_msecs_default = 1;
static int prm_log_checkpoint_sleep_msecs_lower = 0;
static unsigned int prm_log_checkpoint_sleep_msecs_flag = 0;

bool PRM_LOG_BACKGROUND_ARCHIVING = true;
static bool prm_log_background_archiving_default = true;
static unsigned int prm_log_background_archiving_flag = 0;

int PRM_LOG_ISOLATION_LEVEL = TRAN_READ_COMMITTED;
static int prm_log_isolation_level_default = TRAN_READ_COMMITTED;
static int prm_log_isolation_level_lower = TRAN_READ_COMMITTED;
static int prm_log_isolation_level_upper = TRAN_SERIALIZABLE;
static unsigned int prm_log_isolation_level_flag = 0;

int PRM_MVCC_LOG_ISOLATION_LEVEL = TRAN_READ_COMMITTED;
static int prm_mvcc_log_isolation_level_default = TRAN_READ_COMMITTED;
static int prm_mvcc_log_isolation_level_lower = TRAN_READ_COMMITTED;

static unsigned int prm_log_media_failure_support_flag = 0;

bool PRM_COMMIT_ON_SHUTDOWN = false;
static bool prm_commit_on_shutdown_default = false;
static unsigned int prm_commit_on_shutdown_flag = 0;

int PRM_SHUTDOWN_WAIT_TIME_IN_SECS = 600;
static int prm_shutdown_wait_time_in_secs_default = 600;
static int prm_shutdown_wait_time_in_secs_lower = 60;
static unsigned int prm_shutdown_wait_time_in_secs_flag = 0;

bool PRM_CSQL_AUTO_COMMIT = true;
static bool prm_csql_auto_commit_default = true;
static unsigned int prm_csql_auto_commit_flag = 0;

bool PRM_LOG_SWEEP_CLEAN = true;
static bool prm_log_sweep_clean_default = true;
static unsigned int prm_log_sweep_clean_flag = 0;

int PRM_WS_HASHTABLE_SIZE = 4096;
static int prm_ws_hashtable_size_default = 4096;
static int prm_ws_hashtable_size_lower = 1024;
static unsigned int prm_ws_hashtable_size_flag = 0;

bool PRM_WS_MEMORY_REPORT = false;
static bool prm_ws_memory_report_default = false;
static unsigned int prm_ws_memory_report_flag = 0;

bool PRM_GC_ENABLE = false;
static bool prm_gc_enable_default = false;
static unsigned int prm_gc_enable_flag = 0;

int PRM_TCP_PORT_ID = 1523;
static int prm_tcp_port_id_default = 1523;
static unsigned int prm_tcp_port_id_flag = 0;
static int prm_tcp_port_id_upper = USHRT_MAX;
static int prm_tcp_port_id_lower = 1;

int PRM_TCP_CONNECTION_TIMEOUT = 5;
static int prm_tcp_connection_timeout_default = 5;
static int prm_tcp_connection_timeout_lower = -1;
static unsigned int prm_tcp_connection_timeout_flag = 0;

int PRM_OPTIMIZATION_LEVEL = 1;
static int prm_optimization_level_default = 1;
static unsigned int prm_optimization_level_flag = 0;

bool PRM_QO_DUMP = false;
static bool prm_qo_dump_default = false;
static unsigned int prm_qo_dump_flag = 0;

int PRM_CSS_MAX_CLIENTS = 100;
static int prm_css_max_clients_default = 100;
static int prm_css_max_clients_lower = 10;
static int prm_css_max_clients_upper = 10000;
static unsigned int prm_css_max_clients_flag = 0;

UINT64 PRM_THREAD_STACKSIZE = (1024 * 1024);
static UINT64 prm_thread_stacksize_default = (1024 * 1024);
static UINT64 prm_thread_stacksize_lower = 64 * 1024;
static UINT64 prm_thread_stacksize_upper = INT_MAX;
static unsigned int prm_thread_stacksize_flag = 0;

const char *PRM_CFG_DB_HOSTS = "";
static const char *prm_cfg_db_hosts_default = NULL;
static unsigned int prm_cfg_db_hosts_flag = 0;

int PRM_RESET_TR_PARSER = 10;
static int prm_reset_tr_parser_default = 10;
static unsigned int prm_reset_tr_parser_flag = 0;

int PRM_IO_BACKUP_NBUFFERS = 256;
static int prm_io_backup_nbuffers_default = 256;
static int prm_io_backup_nbuffers_lower = 256;
static unsigned int prm_io_backup_nbuffers_flag = 0;

UINT64 PRM_IO_BACKUP_MAX_VOLUME_SIZE = 0;
static UINT64 prm_io_backup_max_volume_size_default = 0;
static UINT64 prm_io_backup_max_volume_size_lower = 1024 * 32;
static UINT64 prm_io_backup_max_volume_size_upper = INT_MAX;
static unsigned int prm_io_backup_max_volume_size_flag = 0;

int PRM_IO_BACKUP_SLEEP_MSECS = 0;
static int prm_io_backup_sleep_msecs_default = 0;
static int prm_io_backup_sleep_msecs_lower = 0;
static unsigned int prm_io_backup_sleep_msecs_flag = 0;

int PRM_MAX_PAGES_IN_TEMP_FILE_CACHE = 1000;
static int prm_max_pages_in_temp_file_cache_default = 1000;	/* pages */
static int prm_max_pages_in_temp_file_cache_lower = 100;
static unsigned int prm_max_pages_in_temp_file_cache_flag = 0;

int PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE = 512;
static int prm_max_entries_in_temp_file_cache_default = 512;
static int prm_max_entries_in_temp_file_cache_lower = 10;
static unsigned int prm_max_entries_in_temp_file_cache_flag = 0;

bool PRM_PTHREAD_SCOPE_PROCESS = true;
static bool prm_pthread_scope_process_default = true;
static unsigned int prm_pthread_scope_process_flag = 0;

int PRM_TEMP_MEM_BUFFER_PAGES = 4;
static int prm_temp_mem_buffer_pages_default = 4;
static int prm_temp_mem_buffer_pages_lower = 0;
static int prm_temp_mem_buffer_pages_upper = 20;
static unsigned int prm_temp_mem_buffer_pages_flag = 0;

int PRM_INDEX_SCAN_KEY_BUFFER_PAGES = 20;
static int prm_index_scan_key_buffer_pages_default = 20;
static int prm_index_scan_key_buffer_pages_lower = 0;
static unsigned int prm_index_scan_key_buffer_pages_flag = 0;

bool PRM_DONT_REUSE_HEAP_FILE = false;
static bool prm_dont_reuse_heap_file_default = false;
static unsigned int prm_dont_reuse_heap_file_flag = 0;

bool PRM_QUERY_MODE_SYNC = true;
static bool prm_query_mode_sync_default = true;
static unsigned int prm_query_mode_sync_flag = 0;

int PRM_INSERT_MODE = 1 + 2;
static int prm_insert_mode_default = 1 + 2;
static int prm_insert_mode_lower = 0;
static int prm_insert_mode_upper = 31;	/* For backward compatibility */
static unsigned int prm_insert_mode_flag = 0;

int PRM_LK_MAX_SCANID_BIT = 32;
static int prm_lk_max_scanid_bit_default = 32;
static int prm_lk_max_scanid_bit_lower = 32;
static int prm_lk_max_scanid_bit_upper = 128;
static unsigned int prm_lk_max_scanid_bit_flag = 0;

bool PRM_HOSTVAR_LATE_BINDING = false;
static bool prm_hostvar_late_binding_default = false;
static unsigned int prm_hostvar_late_binding_flag = 0;

bool PRM_ENABLE_HISTO = false;
static bool prm_enable_histo_default = false;
static unsigned int prm_enable_histo_flag = 0;

int PRM_MUTEX_BUSY_WAITING_CNT = 0;
static int prm_mutex_busy_waiting_cnt_default = 0;
static unsigned int prm_mutex_busy_waiting_cnt_flag = 0;

int PRM_PB_NUM_LRU_CHAINS = 0;
static int prm_pb_num_LRU_chains_default = 0;	/* system define */
static int prm_pb_num_LRU_chains_lower = 0;
static int prm_pb_num_LRU_chains_upper = 1000;
static unsigned int prm_pb_num_LRU_chains_flag = 0;

int PRM_PAGE_BG_FLUSH_INTERVAL_MSEC = 0;
static int prm_page_bg_flush_interval_msec_default = 0;
static int prm_page_bg_flush_interval_msec_lower = -1;
static unsigned int prm_page_bg_flush_interval_msec_flag = 0;

bool PRM_ADAPTIVE_FLUSH_CONTROL = true;
static bool prm_adaptive_flush_control_default = true;
static unsigned int prm_adaptive_flush_control_flag = 0;

int PRM_MAX_FLUSH_PAGES_PER_SECOND = 10000;
static int prm_max_flush_pages_per_second_default = 10000;
static int prm_max_flush_pages_per_second_lower = 1;
static int prm_max_flush_pages_per_second_upper = INT_MAX;
static unsigned int prm_max_flush_pages_per_second_flag = 0;

int PRM_PB_SYNC_ON_NFLUSH = 200;
static int prm_pb_sync_on_nflush_default = 200;
static int prm_pb_sync_on_nflush_lower = 1;
static int prm_pb_sync_on_nflush_upper = INT_MAX;
static unsigned int prm_pb_sync_on_nflush_flag = 0;

int PRM_PB_DEBUG_PAGE_VALIDATION_LEVEL = PGBUF_DEBUG_NO_PAGE_VALIDATION;
#if !defined(NDEBUG)
static int prm_pb_debug_page_validation_level_default =
  PGBUF_DEBUG_PAGE_VALIDATION_FETCH;
#else /* !NDEBUG */
static int prm_pb_debug_page_validation_level_default =
  PGBUF_DEBUG_NO_PAGE_VALIDATION;
#endif /* !NDEBUG */
static unsigned int prm_pb_debug_page_validation_level_flag = 0;

bool PRM_ORACLE_STYLE_OUTERJOIN = false;
static bool prm_oracle_style_outerjoin_default = false;
static unsigned int prm_oracle_style_outerjoin_flag = 0;

int PRM_COMPAT_MODE = COMPAT_CUBRID;
static int prm_compat_mode_default = COMPAT_CUBRID;
static int prm_compat_mode_lower = COMPAT_CUBRID;
static int prm_compat_mode_upper = COMPAT_ORACLE;
static unsigned int prm_compat_mode_flag = 0;

bool PRM_ANSI_QUOTES = true;
static bool prm_ansi_quotes_default = true;
static unsigned int prm_ansi_quotes_flag = 0;

int PRM_DEFAULT_WEEK_FORMAT = 0;
static int prm_week_format_default = 0;
static int prm_week_format_lower = 0;
static int prm_week_format_upper = 7;
static unsigned int prm_week_format_flag = 0;

bool PRM_TEST_MODE = false;
static bool prm_test_mode_default = false;
static unsigned int prm_test_mode_flag = 0;

bool PRM_ONLY_FULL_GROUP_BY = false;
static bool prm_only_full_group_by_default = false;
static unsigned int prm_only_full_group_by_flag = 0;

bool PRM_PIPES_AS_CONCAT = true;
static bool prm_pipes_as_concat_default = true;
static unsigned int prm_pipes_as_concat_flag = 0;

bool PRM_MYSQL_TRIGGER_CORRELATION_NAMES = false;
static bool prm_mysql_trigger_correlation_names_default = false;
static unsigned int prm_mysql_trigger_correlation_names_flag = 0;

bool PRM_REQUIRE_LIKE_ESCAPE_CHARACTER = false;
static bool prm_require_like_escape_character_default = false;
static unsigned int prm_require_like_escape_character_flag = 0;

bool PRM_NO_BACKSLASH_ESCAPES = true;
static bool prm_no_backslash_escapes_default = true;
static unsigned int prm_no_backslash_escapes_flag = 0;

UINT64 PRM_GROUP_CONCAT_MAX_LEN = 1024;
static UINT64 prm_group_concat_max_len_default = 1024;
static UINT64 prm_group_concat_max_len_lower = 4;
static UINT64 prm_group_concat_max_len_upper = INT_MAX;
static unsigned int prm_group_concat_max_len_flag = 0;

UINT64 PRM_STRING_MAX_SIZE_BYTES = 1024 * 1024;
static UINT64 prm_string_max_size_bytes_default = 1024 * 1024;
static UINT64 prm_string_max_size_bytes_lower = 64;
static UINT64 prm_string_max_size_bytes_upper = 32 * 1024 * 1024;
static unsigned int prm_string_max_size_bytes_flag = 0;

bool PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT = false;
static bool prm_add_column_update_hard_default_default = false;
static unsigned int prm_add_column_update_hard_default_flag = 0;

bool PRM_RETURN_NULL_ON_FUNCTION_ERRORS = false;
static bool prm_return_null_on_function_errors_default = false;
static unsigned int prm_return_null_on_function_errors_flag = 0;

bool PRM_ALTER_TABLE_CHANGE_TYPE_STRICT = false;
static bool prm_alter_table_change_type_strict_default = false;
static unsigned int prm_alter_table_change_type_strict_flag = 0;

bool PRM_PLUS_AS_CONCAT = true;
static bool prm_plus_as_concat_default = true;
static unsigned int prm_plus_as_concat_flag = 0;

int PRM_COMPACTDB_PAGE_RECLAIM_ONLY = 0;
static int prm_compactdb_page_reclaim_only_default = 0;
static unsigned int prm_compactdb_page_reclaim_only_flag = 0;

float PRM_LIKE_TERM_SELECTIVITY = 0.1f;
static float prm_like_term_selectivity_default = 0.1f;
static float prm_like_term_selectivity_upper = 1.0f;
static float prm_like_term_selectivity_lower = 0.0f;
static unsigned int prm_like_term_selectivity_flag = 0;

int PRM_MAX_OUTER_CARD_OF_IDXJOIN = 0;
static int prm_max_outer_card_of_idxjoin_default = 0;
static int prm_max_outer_card_of_idxjoin_lower = 0;
static unsigned int prm_max_outer_card_of_idxjoin_flag = 0;

bool PRM_ORACLE_STYLE_EMPTY_STRING = false;
static bool prm_oracle_style_empty_string_default = false;
static unsigned int prm_oracle_style_empty_string_flag = 0;

int PRM_SUPPRESS_FSYNC = 0;
static int prm_suppress_fsync_default = 0;
static int prm_suppress_fsync_upper = 100;
static int prm_suppress_fsync_lower = 0;
static unsigned int prm_suppress_fsync_flag = 0;

bool PRM_CALL_STACK_DUMP_ON_ERROR = false;
static bool prm_call_stack_dump_on_error_default = false;
static unsigned int prm_call_stack_dump_on_error_flag = 0;

int *PRM_CALL_STACK_DUMP_ACTIVATION = int_list_initial;
static bool *prm_call_stack_dump_activation_default = NULL;
static unsigned int prm_call_stack_dump_activation_flag = 0;

int *PRM_CALL_STACK_DUMP_DEACTIVATION = int_list_initial;
static bool *prm_call_stack_dump_deactivation_default = NULL;
static unsigned int prm_call_stack_dump_deactivation_flag = 0;

bool PRM_COMPAT_NUMERIC_DIVISION_SCALE = false;
static bool prm_compat_numeric_division_scale_default = false;
static unsigned int prm_compat_numeric_division_scale_flag = 0;

bool PRM_DBFILES_PROTECT = false;
static bool prm_dbfiles_protect_default = false;
static unsigned int prm_dbfiles_protect_flag = 0;

bool PRM_AUTO_RESTART_SERVER = true;
static bool prm_auto_restart_server_default = true;
static unsigned int prm_auto_restart_server_flag = 0;

int PRM_XASL_MAX_PLAN_CACHE_ENTRIES = 1000;
static int prm_xasl_max_plan_cache_entries_default = 1000;
static unsigned int prm_xasl_max_plan_cache_entries_flag = 0;

#if defined (ENABLE_UNUSED_FUNCTION)
int PRM_XASL_MAX_PLAN_CACHE_CLONES = -1;
static int prm_xasl_max_plan_cache_clones_default = -1;	/* disabled */
static unsigned int prm_xasl_max_plan_cache_clones_flag = 0;
#endif /* ENABLE_UNUSED_FUNCTION */

int PRM_FILTER_PRED_MAX_CACHE_ENTRIES = 1000;
static int prm_filter_pred_max_cache_entries_default = 1000;
static unsigned int prm_filter_pred_max_cache_entries_flag = 0;

int PRM_FILTER_PRED_MAX_CACHE_CLONES = 10;
static int prm_filter_pred_max_cache_clones_default = 10;
static unsigned int prm_filter_pred_max_cache_clones_flag = 0;

int PRM_XASL_PLAN_CACHE_TIMEOUT = -1;
static int prm_xasl_plan_cache_timeout_default = -1;	/* infinity */
static unsigned int prm_xasl_plan_cache_timeout_flag = 0;

int PRM_LIST_QUERY_CACHE_MODE = 0;
static int prm_list_query_cache_mode_default = 0;	/* disabled */
static int prm_list_query_cache_mode_upper = 2;
static int prm_list_query_cache_mode_lower = 0;
static unsigned int prm_list_query_cache_mode_flag = 0;

int PRM_LIST_MAX_QUERY_CACHE_ENTRIES = -1;
static int prm_list_max_query_cache_entries_default = -1;	/* disabled */
static unsigned int prm_list_max_query_cache_entries_flag = 0;

int PRM_LIST_MAX_QUERY_CACHE_PAGES = -1;
static int prm_list_max_query_cache_pages_default = -1;	/* infinity */
static unsigned int prm_list_max_query_cache_pages_flag = 0;

bool PRM_USE_ORDERBY_SORT_LIMIT = true;
static bool prm_use_orderby_sort_limit_default = true;
static unsigned int prm_use_orderby_sort_limit_flag = 0;

static unsigned int prm_replication_mode_flag = 0;

int PRM_HA_MODE = HA_MODE_OFF;
static int prm_ha_mode_default = HA_MODE_OFF;
static int prm_ha_mode_upper = HA_MODE_REPLICA;
static int prm_ha_mode_lower = HA_MODE_OFF;
int PRM_HA_MODE_FOR_SA_UTILS_ONLY = HA_MODE_OFF;
static unsigned int prm_ha_mode_flag = 0;

int PRM_HA_SERVER_STATE = HA_SERVER_STATE_IDLE;
static int prm_ha_server_state_default = HA_SERVER_STATE_IDLE;
static int prm_ha_server_state_upper = HA_SERVER_STATE_DEAD;
static int prm_ha_server_state_lower = HA_SERVER_STATE_IDLE;
static unsigned int prm_ha_server_state_flag = 0;

int PRM_HA_LOG_APPLIER_STATE = HA_LOG_APPLIER_STATE_UNREGISTERED;
static int prm_ha_log_applier_state_default =
  HA_LOG_APPLIER_STATE_UNREGISTERED;
static int prm_ha_log_applier_state_upper = HA_LOG_APPLIER_STATE_ERROR;
static int prm_ha_log_applier_state_lower = HA_LOG_APPLIER_STATE_UNREGISTERED;
static unsigned int prm_ha_log_applier_state_flag = 0;

const char *PRM_HA_NODE_LIST = "";
static const char *prm_ha_node_list_default = NULL;
static unsigned int prm_ha_node_list_flag = 0;

const char *PRM_HA_REPLICA_LIST = "";
static const char *prm_ha_replica_list_default = NULL;
static unsigned int prm_ha_replica_list_flag = 0;

const char *PRM_HA_DB_LIST = "";
static const char *prm_ha_db_list_default = NULL;
static unsigned int prm_ha_db_list_flag = 0;

const char *PRM_HA_COPY_LOG_BASE = "";
static const char *prm_ha_copy_log_base_default = NULL;
static unsigned int prm_ha_copy_log_base_flag = 0;

const char *PRM_HA_COPY_SYNC_MODE = "";
static const char *prm_ha_copy_sync_mode_default = NULL;
static unsigned int prm_ha_copy_sync_mode_flag = 0;

int PRM_HA_APPLY_MAX_MEM_SIZE = HB_DEFAULT_APPLY_MAX_MEM_SIZE;
static int prm_ha_apply_max_mem_size_default = HB_DEFAULT_APPLY_MAX_MEM_SIZE;
static unsigned int prm_ha_apply_max_mem_size_flag = 0;

int PRM_HA_PORT_ID = HB_DEFAULT_HA_PORT_ID;
static int prm_ha_port_id_default = HB_DEFAULT_HA_PORT_ID;
static unsigned int prm_ha_port_id_flag = 0;
static int prm_ha_port_id_upper = USHRT_MAX;
static int prm_ha_port_id_lower = 1;

int PRM_HA_INIT_TIMER_IN_MSECS = HB_DEFAULT_INIT_TIMER_IN_MSECS;
static int prm_ha_init_timer_im_msecs_default =
  HB_DEFAULT_INIT_TIMER_IN_MSECS;
static unsigned int prm_ha_init_timer_im_msecs_flag = 0;

int PRM_HA_HEARTBEAT_INTERVAL_IN_MSECS =
  HB_DEFAULT_HEARTBEAT_INTERVAL_IN_MSECS;
static int prm_ha_heartbeat_interval_in_msecs_default =
  HB_DEFAULT_HEARTBEAT_INTERVAL_IN_MSECS;
static unsigned int prm_ha_heartbeat_interval_in_msecs_flag = 0;

int PRM_HA_CALC_SCORE_INTERVAL_IN_MSECS =
  HB_DEFAULT_CALC_SCORE_INTERVAL_IN_MSECS;
static int prm_ha_calc_score_interval_in_msecs_default =
  HB_DEFAULT_CALC_SCORE_INTERVAL_IN_MSECS;
static unsigned int prm_ha_calc_score_interval_in_msecs_flag = 0;

int PRM_HA_FAILOVER_WAIT_TIME_IN_MSECS =
  HB_DEFAULT_FAILOVER_WAIT_TIME_IN_MSECS;
static int prm_ha_failover_wait_time_in_msecs_default =
  HB_DEFAULT_FAILOVER_WAIT_TIME_IN_MSECS;
static unsigned int prm_ha_failover_wait_time_in_msecs_flag = 0;

int PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS =
  HB_DEFAULT_START_CONFIRM_INTERVAL_IN_MSECS;
static int prm_ha_process_start_confirm_interval_in_msecs_default =
  HB_DEFAULT_START_CONFIRM_INTERVAL_IN_MSECS;
static unsigned int prm_ha_process_start_confirm_interval_in_msecs_flag = 0;

int PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS =
  HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS;
static int prm_ha_process_dereg_confirm_interval_in_msecs_default =
  HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS;
static unsigned int prm_ha_process_dereg_confirm_interval_in_msecs_flag = 0;

int PRM_HA_MAX_PROCESS_START_CONFIRM = HB_DEFAULT_MAX_PROCESS_START_CONFIRM;
static int prm_ha_max_process_start_confirm_default =
  HB_DEFAULT_MAX_PROCESS_START_CONFIRM;
static unsigned int prm_ha_max_process_start_confirm_flag = 0;

int PRM_HA_MAX_PROCESS_DEREG_CONFIRM = HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM;
static int prm_ha_max_process_dereg_confirm_default =
  HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM;
static unsigned int prm_ha_max_process_dereg_confirm_flag = 0;

int PRM_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF =
  HB_DEFAULT_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS;
static int prm_ha_unacceptable_proc_restart_timediff_default =
  HB_DEFAULT_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS;
static unsigned int prm_ha_unacceptable_proc_restart_timediff_flag = 0;

int PRM_HA_CHANGEMODE_INTERVAL_IN_MSECS =
  HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS;
static int prm_ha_changemode_interval_in_msecs_default =
  HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS;
static unsigned int prm_ha_changemode_interval_in_msecs_flag = 0;

int PRM_HA_MAX_HEARTBEAT_GAP = HB_DEFAULT_MAX_HEARTBEAT_GAP;
static int prm_ha_max_heartbeat_gap_default = HB_DEFAULT_MAX_HEARTBEAT_GAP;
static unsigned int prm_ha_max_heartbeat_gap_flag = 0;

const char *PRM_HA_PING_HOSTS = "";
static const char *prm_ha_ping_hosts_default = NULL;
static unsigned int prm_ha_ping_hosts_flag = 0;

int *PRM_HA_APPLYLOGDB_RETRY_ERROR_LIST = int_list_initial;
static bool *prm_ha_applylogdb_retry_error_list_default = NULL;
static unsigned int prm_ha_applylogdb_retry_error_list_flag = 0;

int *PRM_HA_APPLYLOGDB_IGNORE_ERROR_LIST = int_list_initial;
static bool *prm_ha_applylogdb_ignore_error_list_default = NULL;
static unsigned int prm_ha_applylogdb_ignore_error_list_flag = 0;

int PRM_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS = -1;
static int prm_ha_applylogdb_log_wait_time_in_secs_default = -1;
static int prm_ha_applylogdb_log_wait_time_in_secs_lower = -1;
static unsigned int prm_ha_applylogdb_log_wait_time_in_secs_flag = 0;

bool PRM_HA_SQL_LOGGING = false;
static bool prm_ha_sql_logging_default = false;
static unsigned int prm_ha_sql_logging_flag = 0;

int PRM_HA_SQL_LOG_MAX_SIZE_IN_MB = INT_MIN;
static int prm_ha_sql_log_max_size_in_mb_default = 50;
static int prm_ha_sql_log_max_size_in_mb_upper = 2048;
static int prm_ha_sql_log_max_size_in_mb_lower = 1;
static unsigned int prm_ha_sql_log_max_size_in_mb_flag = 0;

int PRM_HA_COPY_LOG_MAX_ARCHIVES = 1;
static int prm_ha_copy_log_max_archives_default = 1;
static int prm_ha_copy_log_max_archives_upper = INT_MAX;
static int prm_ha_copy_log_max_archives_lower = 0;
static unsigned int prm_ha_copy_log_max_archives_flag = 0;

int PRM_HA_COPY_LOG_TIMEOUT = 5;
static int prm_ha_copy_log_timeout_default = 5;
static int prm_ha_copy_log_timeout_upper = INT_MAX;
static int prm_ha_copy_log_timeout_lower = -1;
static unsigned int prm_ha_copy_log_timeout_flag = 0;

int PRM_HA_REPLICA_DELAY_IN_SECS = 0;
static int prm_ha_replica_delay_in_secs_default = 0;
static int prm_ha_replica_delay_in_secs_upper = INT_MAX;
static int prm_ha_replica_delay_in_secs_lower = 0;
static unsigned int prm_ha_replica_delay_in_secs_flag = 0;

const char *PRM_HA_REPLICA_TIME_BOUND = "";
static const char *prm_ha_replica_time_bound_default = NULL;
static unsigned int prm_ha_replica_time_bound_flag = 0;

int PRM_HA_DELAY_LIMIT_IN_SECS = 0;
static int prm_ha_delay_limit_in_secs_default = 0;
static int prm_ha_delay_limit_in_secs_upper = INT_MAX;
static int prm_ha_delay_limit_in_secs_lower = 0;
static unsigned int prm_ha_delay_limit_in_secs_flag = 0;

int PRM_HA_DELAY_LIMIT_DELTA_IN_SECS = 0;
static int prm_ha_delay_limit_delta_in_secs_default = 0;
static int prm_ha_delay_limit_delta_in_secs_upper = INT_MAX;
static int prm_ha_delay_limit_delta_in_secs_lower = 0;
static unsigned int prm_ha_delay_limit_delta_in_secs_flag = 0;

int PRM_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS = 500;
static int prm_ha_applylogdb_max_commit_interval_in_msecs_default = 500;
static int prm_ha_applylogdb_max_commit_interval_in_msecs_upper = INT_MAX;
static int prm_ha_applylogdb_max_commit_interval_in_msecs_lower = 0;
static unsigned int prm_ha_applylogdb_max_commit_interval_in_msecs_flag = 0;

int PRM_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS = 30;
static int prm_ha_check_disk_failure_interval_in_secs_default = 30;
static int prm_ha_check_disk_failure_interval_in_secs_upper = INT_MAX;
static int prm_ha_check_disk_failure_interval_in_secs_lower = 0;
static unsigned int prm_ha_check_disk_failure_interval_in_secs_flag = 0;

bool PRM_JAVA_STORED_PROCEDURE = false;
static bool prm_java_stored_procedure_default = false;
static unsigned int prm_java_stored_procedure_flag = 0;

bool PRM_COMPAT_PRIMARY_KEY = false;
static bool prm_compat_primary_key_default = false;
static unsigned int prm_compat_primary_key_flag = 0;

static unsigned int prm_log_header_flush_interval_flag = 0;

bool PRM_LOG_ASYNC_COMMIT = false;
static bool prm_log_async_commit_default = false;
static unsigned int prm_log_async_commit_flag = 0;

int PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS = 0;
static int prm_log_group_commit_interval_msecs_default = 0;
static int prm_log_group_commit_interval_msecs_lower = 0;
static unsigned int prm_log_group_commit_interval_msecs_flag = 0;

static unsigned int prm_log_bg_flush_interval_msecs_flag = 0;

static unsigned int prm_log_bg_flush_num_pages_flag = 0;

bool PRM_INTL_MBS_SUPPORT = false;
static bool prm_intl_mbs_support_default = false;
static unsigned int prm_intl_mbs_support_flag = 0;

bool PRM_LOG_COMPRESS = false;
static bool prm_log_compress_default = true;
static unsigned int prm_log_compress_flag = 0;

bool PRM_BLOCK_NOWHERE_STATEMENT = false;
static bool prm_block_nowhere_statement_default = false;
static unsigned int prm_block_nowhere_statement_flag = 0;

bool PRM_BLOCK_DDL_STATEMENT = false;
static bool prm_block_ddl_statement_default = false;
static unsigned int prm_block_ddl_statement_flag = 0;

#if defined (ENABLE_UNUSED_FUNCTION)
bool PRM_SINGLE_BYTE_COMPARE = false;
static bool prm_single_byte_compare_default = false;
static unsigned int prm_single_byte_compare_flag = 0;
#endif

int PRM_CSQL_HISTORY_NUM = 50;
static int prm_csql_history_num_default = 50;
static int prm_csql_history_num_upper = 200;
static int prm_csql_history_num_lower = 1;
static unsigned int prm_csql_history_num_flag = 0;

bool PRM_LOG_TRACE_DEBUG = false;
static bool prm_log_trace_debug_default = false;
static unsigned int prm_log_trace_debug_flag = 0;

const char *PRM_DL_FORK = "";
static const char *prm_dl_fork_default = NULL;
static unsigned int prm_dl_fork_flag = 0;

bool PRM_ER_PRODUCTION_MODE = true;
static bool prm_er_production_mode_default = true;
static unsigned int prm_er_production_mode_flag = 0;

int PRM_ER_STOP_ON_ERROR = 0;
static int prm_er_stop_on_error_default = 0;
static int prm_er_stop_on_error_upper = 0;
static unsigned int prm_er_stop_on_error_flag = 0;

int PRM_TCP_RCVBUF_SIZE = -1;
static int prm_tcp_rcvbuf_size_default = -1;
static unsigned int prm_tcp_rcvbuf_size_flag = 0;

int PRM_TCP_SNDBUF_SIZE = -1;
static int prm_tcp_sndbuf_size_default = -1;
static unsigned int prm_tcp_sndbuf_size_flag = 0;

bool PRM_TCP_NODELAY = false;
static bool prm_tcp_nodelay_default = false;
static unsigned int prm_tcp_nodelay_flag = 0;

bool PRM_TCP_KEEPALIVE = true;
static bool prm_tcp_keepalive_default = true;
static unsigned int prm_tcp_keepalive_flag = 0;

bool PRM_CSQL_SINGLE_LINE_MODE = false;
static bool prm_csql_single_line_mode_default = false;
static unsigned int prm_csql_single_line_mode_flag = 0;

bool PRM_XASL_DEBUG_DUMP = false;
static bool prm_xasl_debug_dump_default = false;
static unsigned int prm_xasl_debug_dump_flag = 0;

int PRM_LOG_MAX_ARCHIVES = INT_MAX;
static int prm_log_max_archives_default = INT_MAX;
static int prm_log_max_archives_lower = 0;
static unsigned int prm_log_max_archives_flag = 0;

bool PRM_FORCE_REMOVE_LOG_ARCHIVES = true;
static bool prm_force_remove_log_archives_default = true;
static unsigned int prm_force_remove_log_archives_flag = 0;

int PRM_REMOVE_LOG_ARCHIVES_INTERVAL = 0;
static int prm_remove_log_archives_interval_default = 0;
static int prm_remove_log_archives_interval_lower = 0;
static unsigned int prm_remove_log_archives_interval_flag = 0;

bool PRM_LOG_NO_LOGGING = false;
static bool prm_log_no_logging_default = false;
static unsigned int prm_log_no_logging_flag = 0;

bool PRM_UNLOADDB_IGNORE_ERROR = false;
static bool prm_unloaddb_ignore_error_default = false;
static unsigned int prm_unloaddb_ignore_error_flag = 0;

int PRM_UNLOADDB_LOCK_TIMEOUT = -1;
static int prm_unloaddb_lock_timeout_default = -1;
static int prm_unloaddb_lock_timeout_lower = -1;
static unsigned int prm_unloaddb_lock_timeout_flag = 0;

int PRM_LOADDB_FLUSH_INTERVAL = 1000;
static int prm_loaddb_flush_interval_default = 1000;
static int prm_loaddb_flush_interval_lower = 0;
static unsigned int prm_loaddb_flush_interval_flag = 0;

const char *PRM_IO_TEMP_VOLUME_PATH = "";
static char *prm_io_temp_volume_path_default = NULL;
static unsigned int prm_io_temp_volume_path_flag = 0;

const char *PRM_IO_VOLUME_EXT_PATH = "";
static char *prm_io_volume_ext_path_default = NULL;
static unsigned int prm_io_volume_ext_path_flag = 0;

bool PRM_UNIQUE_ERROR_KEY_VALUE = false;
static bool prm_unique_error_key_value_default = false;
static unsigned int prm_unique_error_key_value_flag = 0;

bool PRM_USE_SYSTEM_MALLOC = false;
static bool prm_use_system_malloc_default = false;
static unsigned int prm_use_system_malloc_flag = 0;

const char *PRM_EVENT_HANDLER = "";
static const char *prm_event_handler_default = NULL;
static unsigned int prm_event_handler_flag = 0;

int *PRM_EVENT_ACTIVATION = int_list_initial;
static bool *prm_event_activation_default = NULL;
static unsigned int prm_event_activation_flag = 0;

bool PRM_READ_ONLY_MODE = false;
static bool prm_read_only_mode_default = false;
static unsigned int prm_read_only_mode_flag = 0;

int PRM_MNT_WAITING_THREAD = 0;
static int prm_mnt_waiting_thread_default = 0;
static int prm_mnt_waiting_thread_lower = 0;
static unsigned int prm_mnt_waiting_thread_flag = 0;

int *PRM_MNT_STATS_THRESHOLD = int_list_initial;
static int *prm_mnt_stats_threshold_default = NULL;
static unsigned int prm_mnt_stats_threshold_flag = 0;

const char *PRM_SERVICE_SERVICE_LIST = "";
static const char *prm_service_service_list_default = NULL;
static unsigned int prm_service_service_list_flag = 0;

const char *PRM_SERVICE_SERVER_LIST = "";
static const char *prm_service_server_list_default = NULL;
static unsigned int prm_service_server_list_flag = 0;

int PRM_SESSION_STATE_TIMEOUT = 60 * 60 * 6;	/* 6 hours */
static int prm_session_timeout_default = 60 * 60 * 6;	/* 6 hours */
static int prm_session_timeout_lower = 60;	/* 1 minute */
static int prm_session_timeout_upper = 60 * 60 * 24 * 365;	/* 1 nonleap year */
static unsigned int prm_session_timeout_flag = 0;

int PRM_MULTI_RANGE_OPT_LIMIT = 100;
static int prm_multi_range_opt_limit_default = 100;
static int prm_multi_range_opt_limit_lower = 0;	/*disabled */
static int prm_multi_range_opt_limit_upper = 10000;
static unsigned int prm_multi_range_opt_limit_flag = 0;

UINT64 PRM_DB_VOLUME_SIZE = 536870912ULL;
static UINT64 prm_db_volume_size_default = 536870912ULL;	/* 512M */
static UINT64 prm_db_volume_size_lower = 20971520ULL;	/* 20M */
static UINT64 prm_db_volume_size_upper = 21474836480ULL;	/* 20G */
static unsigned int prm_db_volume_size_flag = 0;

UINT64 PRM_LOG_VOLUME_SIZE = 536870912ULL;
static UINT64 prm_log_volume_size_default = 536870912ULL;	/* 512M */
static UINT64 prm_log_volume_size_lower = 20971520ULL;	/* 20M */
static UINT64 prm_log_volume_size_upper = 4294967296ULL;	/* 4G */
static unsigned int prm_log_volume_size_flag = 0;

char *PRM_INTL_NUMBER_LANG = NULL;
static char *prm_intl_number_lang_default = NULL;
static unsigned int prm_intl_number_lang_flag = 0;

char *PRM_INTL_DATE_LANG = NULL;
static char *prm_intl_date_lang_default = NULL;
static unsigned int prm_intl_date_lang_flag = 0;

bool PRM_UNICODE_INPUT_NORMALIZATION = false;
static bool prm_unicode_input_normalization_default = false;
static unsigned int prm_unicode_input_normalization_flag = 0;

bool PRM_UNICODE_OUTPUT_NORMALIZATION = false;
static bool prm_unicode_output_normalization_default = false;
static unsigned int prm_unicode_output_normalization_flag = 0;

bool PRM_INTL_CHECK_INPUT_STRING = false;
static bool prm_intl_check_input_string_default = false;
static unsigned int prm_intl_check_input_string_flag = 0;

int PRM_CHECK_PEER_ALIVE = CSS_CHECK_PEER_ALIVE_BOTH;
static int prm_check_peer_alive_default = CSS_CHECK_PEER_ALIVE_BOTH;
static unsigned int prm_check_peer_alive_flag = 0;

UINT64 PRM_GENERIC_VOL_PREALLOC_SIZE;
static UINT64 prm_generic_vol_prealloc_size_default = 52428800ULL;	/* 50M */
static UINT64 prm_generic_vol_prealloc_size_lower = 0ULL;
static UINT64 prm_generic_vol_prealloc_size_upper = 21474836480ULL;	/* 20G */
static unsigned int prm_generic_vol_prealloc_size_flag = 0;

int PRM_SQL_TRACE_SLOW_MSECS = -1;
static int prm_sql_trace_slow_msecs_default = -1;
static int prm_sql_trace_slow_msecs_lower = -1;
static int prm_sql_trace_slow_msecs_upper = 1000 * 60 * 60 * 24;	/* 24 hours */
static unsigned int prm_sql_trace_slow_msecs_flag = 0;

bool PRM_SQL_TRACE_EXECUTION_PLAN = false;
static bool prm_sql_trace_execution_plan_default = false;
static unsigned int prm_sql_trace_execution_plan_flag = 0;

int PRM_LOG_TRACE_FLUSH_TIME_MSECS = 0;
static int prm_log_trace_flush_time_msecs_default = 0;
static int prm_log_trace_flush_time_msecs_lower = 0;
static unsigned int prm_log_trace_flush_time_msecs_flag = 0;

char *PRM_INTL_COLLATION = NULL;
static char *prm_intl_collation_default = NULL;
static unsigned int prm_intl_collation_flag = 0;

int PRM_SORT_LIMIT_MAX_COUNT = 1000;
static int prm_sort_limit_max_count_default = 1000;
static int prm_sort_limit_max_count_lower = 0;	/* disabled */
static int prm_sort_limit_max_count_upper = INT_MAX;
static unsigned int prm_sort_limit_max_count_flag = 0;

int PRM_SQL_TRACE_IOREADS = 0;
static int prm_sql_trace_ioreads_default = 0;
static int prm_sql_trace_ioreads_lower = 0;
static unsigned int prm_sql_trace_ioreads_flag = 0;

bool PRM_QUERY_TRACE = false;
static bool prm_query_trace_default = false;
static unsigned int prm_query_trace_flag = 0;

int PRM_QUERY_TRACE_FORMAT = QUERY_TRACE_TEXT;
static int prm_query_trace_format_default = QUERY_TRACE_TEXT;
static int prm_query_trace_format_lower = QUERY_TRACE_TEXT;
static int prm_query_trace_format_upper = QUERY_TRACE_JSON;
static unsigned int prm_query_trace_format_flag = 0;

int PRM_MAX_RECURSION_SQL_DEPTH = 400;
static int prm_max_recursion_sql_depth_default = 400;
static unsigned int prm_max_recursion_sql_depth_flag = 0;

UINT64 PRM_MAX_AGG_HASH_SIZE = 2 * 1024 * 1024;	/* 2 MB */
static UINT64 prm_max_agg_hash_size_default = 2 * 1024 * 1024;	/* 2 MB */
static UINT64 prm_max_agg_hash_size_lower = 32 * 1024;	/* 32 KB */
static UINT64 prm_max_agg_hash_size_upper = 128 * 1024 * 1024;	/* 128 MB */
static unsigned int prm_max_agg_hash_size_flag = 0;

bool PRM_AGG_HASH_RESPECT_ORDER = true;
static bool prm_agg_hash_respect_order_default = true;
static unsigned int prm_agg_hash_respect_order_flag = 0;

bool PRM_USE_BTREE_FENCE_KEY = true;
static bool prm_use_btree_fence_key_default = true;
static unsigned int prm_use_btree_fence_key_flag = 0;

bool PRM_QA_BTREE_RANDOM_EXIT = false;
static bool prm_qa_btree_random_exit_default = false;
static unsigned int prm_qa_btree_random_exit_flag = 0;

bool PRM_UPDATE_USE_ATTRIBUTE_REFERENCES = false;
static bool prm_update_use_attribute_references_default = false;
static unsigned int prm_update_use_attribute_references_flag = 0;

float PRM_PB_AIN_RATIO = 0.25f;
static float prm_pb_ain_ratio_default = 0.25f;
static float prm_pb_ain_ratio_upper = 0.8f;
static float prm_pb_ain_ratio_lower = 0.0f;
static unsigned int prm_pb_ain_ratio_flag = 0;

float PRM_PB_AOUT_RATIO = 0.5f;
static float prm_pb_aout_ratio_default = 0.5f;
static float prm_pb_aout_ratio_upper = 3.0;
static float prm_pb_aout_ratio_lower = 0;
static unsigned int prm_pb_aout_ratio_flag = 0;

bool PRM_OPTIMIZER_ENABLE_MERGE_JOIN = false;
static bool prm_optimizer_enable_merge_join_default = false;
static unsigned int prm_optimizer_enable_merge_join_flag = 0;

bool PRM_OPTIMIZER_RESERVE_01 = false;
static bool prm_optimizer_reserve_01_default = false;
static unsigned int prm_optimizer_reserve_01_flag = 0;

bool PRM_OPTIMIZER_RESERVE_02 = false;
static bool prm_optimizer_reserve_02_default = false;
static unsigned int prm_optimizer_reserve_02_flag = 0;

bool PRM_OPTIMIZER_RESERVE_03 = false;
static bool prm_optimizer_reserve_03_default = false;
static unsigned int prm_optimizer_reserve_03_flag = 0;

bool PRM_OPTIMIZER_RESERVE_04 = false;
static bool prm_optimizer_reserve_04_default = false;
static unsigned int prm_optimizer_reserve_04_flag = 0;

bool PRM_OPTIMIZER_RESERVE_05 = false;
static bool prm_optimizer_reserve_05_default = false;
static unsigned int prm_optimizer_reserve_05_flag = 0;

bool PRM_OPTIMIZER_RESERVE_06 = false;
static bool prm_optimizer_reserve_06_default = false;
static unsigned int prm_optimizer_reserve_06_flag = 0;

bool PRM_OPTIMIZER_RESERVE_07 = false;
static bool prm_optimizer_reserve_07_default = false;
static unsigned int prm_optimizer_reserve_07_flag = 0;

bool PRM_OPTIMIZER_RESERVE_08 = false;
static bool prm_optimizer_reserve_08_default = false;
static unsigned int prm_optimizer_reserve_08_flag = 0;

bool PRM_OPTIMIZER_RESERVE_09 = false;
static bool prm_optimizer_reserve_09_default = false;
static unsigned int prm_optimizer_reserve_09_flag = 0;

bool PRM_OPTIMIZER_RESERVE_10 = false;
static bool prm_optimizer_reserve_10_default = false;
static unsigned int prm_optimizer_reserve_10_flag = 0;

bool PRM_OPTIMIZER_RESERVE_11 = false;
static bool prm_optimizer_reserve_11_default = false;
static unsigned int prm_optimizer_reserve_11_flag = 0;

bool PRM_OPTIMIZER_RESERVE_12 = false;
static bool prm_optimizer_reserve_12_default = false;
static unsigned int prm_optimizer_reserve_12_flag = 0;

bool PRM_OPTIMIZER_RESERVE_13 = false;
static bool prm_optimizer_reserve_13_default = false;
static unsigned int prm_optimizer_reserve_13_flag = 0;

bool PRM_OPTIMIZER_RESERVE_14 = false;
static bool prm_optimizer_reserve_14_default = false;
static unsigned int prm_optimizer_reserve_14_flag = 0;

bool PRM_OPTIMIZER_RESERVE_15 = false;
static bool prm_optimizer_reserve_15_default = false;
static unsigned int prm_optimizer_reserve_15_flag = 0;

bool PRM_OPTIMIZER_RESERVE_16 = false;
static bool prm_optimizer_reserve_16_default = false;
static unsigned int prm_optimizer_reserve_16_flag = 0;

bool PRM_OPTIMIZER_RESERVE_17 = false;
static bool prm_optimizer_reserve_17_default = false;
static unsigned int prm_optimizer_reserve_17_flag = 0;

bool PRM_OPTIMIZER_RESERVE_18 = false;
static bool prm_optimizer_reserve_18_default = false;
static unsigned int prm_optimizer_reserve_18_flag = 0;

bool PRM_OPTIMIZER_RESERVE_19 = false;
static bool prm_optimizer_reserve_19_default = false;
static unsigned int prm_optimizer_reserve_19_flag = 0;

bool PRM_OPTIMIZER_RESERVE_20 = false;
static bool prm_optimizer_reserve_20_default = false;
static unsigned int prm_optimizer_reserve_20_flag = 0;

bool PRM_HA_REPL_ENABLE_SERVER_SIDE_UPDATE = true;
static bool prm_ha_repl_enable_server_side_update_default = true;
static bool prm_ha_repl_enable_server_side_update_flag = 0;

float PRM_PB_LRU_HOT_RATIO = 0.4f;
static float prm_pb_lru_hot_ratio_default = 0.4f;
static float prm_pb_lru_hot_ratio_upper = 0.95f;
static float prm_pb_lru_hot_ratio_lower = 0.05f;
static unsigned int prm_pb_lru_hot_ratio_flag = 0;

bool PRM_HA_PREFETCHLOGDB_ENABLE = false;
static unsigned int prm_ha_prefetchlogdb_enable_flag = 0;
static unsigned int prm_ha_prefetchlogdb_enable_default = false;

unsigned int PRM_HA_PREFETCHLOGDB_MAX_THREAD_COUNT = 4;
static unsigned int prm_ha_prefetchlogdb_max_thread_count_flag = 0;
static unsigned int prm_ha_prefetchlogdb_max_thread_count_default = 4;
static unsigned int prm_ha_prefetchlogdb_max_thread_count_lower = 1;
static unsigned int prm_ha_prefetchlogdb_max_thread_count_upper = INT_MAX;

unsigned int PRM_HA_PREFETCHLOGDB_PAGE_DISTANCE = 200;
static unsigned int prm_ha_prefetchlogdb_page_distance_flag = 0;
static unsigned int prm_ha_prefetchlogdb_page_distance_default = 200;
static unsigned int prm_ha_prefetchlogdb_page_distance_lower = 50;
static unsigned int prm_ha_prefetchlogdb_page_distance_upper = INT_MAX;

unsigned int PRM_HA_PREFETCHLOGDB_MAX_PAGE_COUNT = 1000;
static unsigned int prm_ha_prefetchlogdb_max_page_count_flag = 0;
static unsigned int prm_ha_prefetchlogdb_max_page_count_default = 1000;
static unsigned int prm_ha_prefetchlogdb_max_page_count_lower = 0;
static unsigned int prm_ha_prefetchlogdb_max_page_count_upper = INT_MAX;

bool PRM_MVCC_ENABLED = true;
static bool prm_mvcc_enabled_default = true;
static unsigned int prm_mvcc_enabled_flag = 0;

int PRM_VACUUM_MASTER_WAKEUP_INTERVAL = 1;
static int prm_vacuum_master_wakeup_interval_default = 1;
static int prm_vacuum_master_wakeup_interval_lower = 1;
static unsigned int prm_vacuum_master_wakeup_interval_flag = 0;

int PRM_VACUUM_DATA_PAGES = 40;
static int prm_vacuum_data_pages_default = 40;
static int prm_vacuum_data_pages_lower = 10;
static int prm_vacuum_data_pages_upper = 10000;
static unsigned int prm_vacuum_data_pages_flag = 0;

int PRM_VACUUM_LOG_BLOCK_PAGES = 32;
static int prm_vacuum_log_block_pages_default = 32;
static int prm_vacuum_log_block_pages_lower = 4;
static int prm_vacuum_log_block_pages_upper = 1024;
static unsigned int prm_vacuum_log_block_pages_flag = 0;

int PRM_VACUUM_WORKER_COUNT = 10;
static int prm_vacuum_worker_count_default = 10;
static int prm_vacuum_worker_count_lower = 1;
static int prm_vacuum_worker_count_upper = VACUUM_MAX_WORKER_COUNT;
static unsigned int prm_vacuum_worker_count_flag = 0;

int PRM_ER_LOG_VACUUM = 1;
static int prm_er_log_vacuum_default = 1;
static unsigned int prm_er_log_vacuum_flag = 0;

bool PRM_DISABLE_VACUUM = false;
static bool prm_disable_vacuum_default = false;
static unsigned int prm_disable_vacuum_flag = 0;

bool PRM_LOG_BTREE_OPS = false;
static bool prm_log_btree_ops_default = false;
static unsigned int prm_log_btree_ops_flag = 0;

bool PRM_OBJECT_PRINT_FORMAT_OID = false;
static bool prm_object_print_format_oid_default = false;
static unsigned int prm_object_print_format_oid_flag = 0;

typedef int (*DUP_PRM_FUNC) (void *, SYSPRM_DATATYPE, void *,
			     SYSPRM_DATATYPE);

static int prm_size_to_io_pages (void *out_val, SYSPRM_DATATYPE out_type,
				 void *in_val, SYSPRM_DATATYPE in_type);
static int prm_io_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type,
				 void *in_val, SYSPRM_DATATYPE in_type);

static int prm_size_to_log_pages (void *out_val, SYSPRM_DATATYPE out_type,
				  void *in_val, SYSPRM_DATATYPE in_type);
static int prm_log_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type,
				  void *in_val, SYSPRM_DATATYPE in_type);

static int prm_msec_to_sec (void *out_val, SYSPRM_DATATYPE out_type,
			    void *in_val, SYSPRM_DATATYPE in_type);
static int prm_sec_to_msec (void *out_val, SYSPRM_DATATYPE out_type,
			    void *in_val, SYSPRM_DATATYPE in_type);

static int prm_sec_to_min (void *out_val, SYSPRM_DATATYPE out_type,
			   void *in_val, SYSPRM_DATATYPE in_type);
static int prm_min_to_sec (void *out_val, SYSPRM_DATATYPE out_type,
			   void *in_val, SYSPRM_DATATYPE in_type);

static int prm_equal_to_ori (void *out_val, SYSPRM_DATATYPE out_type,
			     void *in_val, SYSPRM_DATATYPE in_type);

typedef struct sysprm_param SYSPRM_PARAM;
struct sysprm_param
{
  const char *name;		/* the keyword expected */
  unsigned int static_flag;	/* bitmask flag representing status words */
  SYSPRM_DATATYPE datatype;	/* value data type */
  unsigned int *dynamic_flag;	/* shared by both original and duplicated */
  void *default_value;		/* address of (pointer to) default value */
  void *value;			/* address of (pointer to) current value */
  void *upper_limit;		/* highest allowable value */
  void *lower_limit;		/* lowest allowable value */
  char *force_value;		/* address of (pointer to) force value string */
  DUP_PRM_FUNC set_dup;		/* set duplicated value to original value */
  DUP_PRM_FUNC get_dup;		/* get duplicated value from original value */
};

static SYSPRM_PARAM prm_Def[] = {
  {PRM_NAME_ER_LOG_DEBUG,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_er_log_debug_flag,
   (void *) &prm_er_log_debug_default,
   (void *) &PRM_ER_LOG_DEBUG,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_BTREE_DEBUG,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_er_btree_debug_flag,
   (void *) &prm_er_btree_debug_default,
   (void *) &PRM_ER_BTREE_DEBUG,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_LOG_LEVEL,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_KEYWORD,
   (void *) &prm_er_log_level_flag,
   (void *) &prm_er_log_level_default,
   (void *) &PRM_ER_LOG_LEVEL,
   (void *) &prm_er_log_level_upper, (void *) &prm_er_log_level_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_LOG_WARNING,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_er_log_warning_flag,
   (void *) &prm_er_log_warning_default,
   (void *) &PRM_ER_LOG_WARNING,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_EXIT_ASK,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_er_exit_ask_flag,
   (void *) &prm_er_exit_ask_default,
   (void *) &PRM_ER_EXIT_ASK,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_LOG_SIZE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_INTEGER,
   (void *) &prm_er_log_size_flag,
   (void *) &prm_er_log_size_default,
   (void *) &PRM_ER_LOG_SIZE,
   (void *) NULL, (void *) &prm_er_log_size_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_LOG_FILE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_er_log_file_flag,
   (void *) &prm_er_log_file_default,
   (void *) &PRM_ER_LOG_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ACCESS_IP_CONTROL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_access_ip_control_flag,
   (void *) &prm_access_ip_control_default,
   (void *) &PRM_ACCESS_IP_CONTROL,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ACCESS_IP_CONTROL_FILE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_STRING,
   (void *) &prm_access_ip_control_file_flag,
   (void *) &prm_access_ip_control_file_default,
   (void *) &PRM_ACCESS_IP_CONTROL_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_IO_LOCKF_ENABLE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_io_lockf_enable_flag,
   (void *) &prm_io_lockf_enable_default,
   (void *) &PRM_IO_LOCKF_ENABLE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SR_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_sr_nbuffers_flag,
   (void *) &prm_sr_nbuffers_default,
   (void *) &PRM_SR_NBUFFERS,
   (void *) NULL, (void *) &prm_sr_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SORT_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT |
    PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_sr_nbuffers_flag,
   (void *) &prm_sr_nbuffers_default,
   (void *) &PRM_SR_NBUFFERS,
   (void *) NULL, (void *) &prm_sr_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_NAME_PB_BUFFER_FLUSH_RATIO,
   (PRM_FOR_SERVER | PRM_HIDDEN | PRM_USER_CHANGE),
   PRM_FLOAT,
   (void *) &prm_pb_buffer_flush_ratio_flag,
   (void *) &prm_pb_buffer_flush_ratio_default,
   (void *) &PRM_PB_BUFFER_FLUSH_RATIO,
   (void *) &prm_pb_buffer_flush_ratio_upper,
   (void *) &prm_pb_buffer_flush_ratio_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PB_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_pb_nbuffers_flag,
   (void *) &prm_pb_nbuffers_default,
   (void *) &PRM_PB_NBUFFERS,
   (void *) NULL, (void *) &prm_pb_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PAGE_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_pb_nbuffers_flag,
   (void *) &prm_pb_nbuffers_default,
   (void *) &PRM_PB_NBUFFERS,
   (void *) NULL, (void *) &prm_pb_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_NAME_HF_UNFILL_FACTOR,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_FLOAT,
   (void *) &prm_hf_unfill_factor_flag,
   (void *) &prm_hf_unfill_factor_default,
   (void *) &PRM_HF_UNFILL_FACTOR,
   (void *) &prm_hf_unfill_factor_upper, (void *) &prm_hf_unfill_factor_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HF_MAX_BESTSPACE_ENTRIES,
   (PRM_FOR_SERVER | PRM_HIDDEN | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_hf_max_bestspace_entries_flag,
   (void *) &prm_hf_max_bestspace_entries_default,
   (void *) &PRM_HF_MAX_BESTSPACE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_BT_UNFILL_FACTOR,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_FLOAT,
   (void *) &prm_bt_unfill_factor_flag,
   (void *) &prm_bt_unfill_factor_default,
   (void *) &PRM_BT_UNFILL_FACTOR,
   (void *) &prm_bt_unfill_factor_upper, (void *) &prm_bt_unfill_factor_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_BT_OID_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_FLOAT,
   (void *) &prm_bt_oid_nbuffers_flag,
   (void *) &prm_bt_oid_nbuffers_default,
   (void *) &PRM_BT_OID_NBUFFERS,
   (void *) &prm_bt_oid_nbuffers_upper, (void *) &prm_bt_oid_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_BT_OID_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT |
    PRM_RELOADABLE),
   PRM_FLOAT,
   (void *) &prm_bt_oid_nbuffers_flag,
   (void *) &prm_bt_oid_nbuffers_default,
   (void *) &PRM_BT_OID_NBUFFERS,
   (void *) &prm_bt_oid_nbuffers_upper, (void *) &prm_bt_oid_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_NAME_BT_INDEX_SCAN_OID_ORDER,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_bt_index_scan_oid_order_flag,
   (void *) &prm_bt_index_scan_oid_order_default,
   (void *) &PRM_BT_INDEX_SCAN_OID_ORDER,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_BOSR_MAXTMP_PAGES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_bosr_maxtmp_flag,
   (void *) &prm_bosr_maxtmp_pages,
   (void *) &PRM_BOSR_MAXTMP_PAGES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LK_TIMEOUT_MESSAGE_DUMP_LEVEL,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) &prm_lk_timeout_message_dump_level_flag,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LK_ESCALATION_AT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_lk_escalation_at_flag,
   (void *) &prm_lk_escalation_at_default,
   (void *) &PRM_LK_ESCALATION_AT,
   (void *) NULL, (void *) &prm_lk_escalation_at_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LK_ROLLBACK_ON_LOCK_ESCALATION,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_lk_rollback_on_lock_escalation_flag,
   (void *) &prm_lk_rollback_on_lock_escalation_default,
   (void *) &PRM_LK_ROLLBACK_ON_LOCK_ESCALATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LK_TIMEOUT_SECS,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_DEPRECATED),
   PRM_INTEGER,
   (void *) &prm_lk_timeout_secs_flag,
   (void *) &prm_lk_timeout_secs_default,
   (void *) &PRM_LK_TIMEOUT_SECS,
   (void *) NULL, (void *) &prm_lk_timeout_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LK_TIMEOUT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_TIME_UNIT |
    PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_lk_timeout_secs_flag,
   (void *) &prm_lk_timeout_secs_default,
   (void *) &PRM_LK_TIMEOUT_SECS,
   (void *) NULL, (void *) &prm_lk_timeout_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_NAME_LK_RUN_DEADLOCK_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_FLOAT,
   (void *) &prm_lk_run_deadlock_interval_flag,
   (void *) &prm_lk_run_deadlock_interval_default,
   (void *) &PRM_LK_RUN_DEADLOCK_INTERVAL,
   (void *) NULL, (void *) &prm_lk_run_deadlock_interval_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_log_nbuffers_flag,
   (void *) &prm_log_nbuffers_default,
   (void *) &PRM_LOG_NBUFFERS,
   (void *) NULL, (void *) &prm_log_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_log_nbuffers_flag,
   (void *) &prm_log_nbuffers_default,
   (void *) &PRM_LOG_NBUFFERS,
   (void *) NULL, (void *) &prm_log_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_log_pages,
   (DUP_PRM_FUNC) prm_log_pages_to_size},
  {PRM_NAME_LOG_CHECKPOINT_NPAGES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_npages_flag,
   (void *) &prm_log_checkpoint_npages_default,
   (void *) &PRM_LOG_CHECKPOINT_NPAGES,
   (void *) NULL, (void *) &prm_log_checkpoint_npages_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_CHECKPOINT_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT |
    PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_npages_flag,
   (void *) &prm_log_checkpoint_npages_default,
   (void *) &PRM_LOG_CHECKPOINT_NPAGES,
   (void *) NULL, (void *) &prm_log_checkpoint_npages_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_log_pages,
   (DUP_PRM_FUNC) prm_log_pages_to_size},
  {PRM_NAME_LOG_CHECKPOINT_INTERVAL_SECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_interval_secs_flag,
   (void *) &prm_log_checkpoint_interval_secs_default,
   (void *) &PRM_LOG_CHECKPOINT_INTERVAL_SECS,
   (void *) NULL, (void *) &prm_log_checkpoint_interval_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_min_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_min},
  {PRM_NAME_LOG_CHECKPOINT_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_interval_secs_flag,
   (void *) &prm_log_checkpoint_interval_secs_default,
   (void *) &PRM_LOG_CHECKPOINT_INTERVAL_SECS,
   (void *) NULL, (void *) &prm_log_checkpoint_interval_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_NAME_LOG_CHECKPOINT_SLEEP_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_sleep_msecs_flag,
   (void *) &prm_log_checkpoint_sleep_msecs_default,
   (void *) &PRM_LOG_CHECKPOINT_SLEEP_MSECS,
   (void *) NULL, (void *) &prm_log_checkpoint_sleep_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_BACKGROUND_ARCHIVING,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_log_background_archiving_flag,
   (void *) &prm_log_background_archiving_default,
   (void *) &PRM_LOG_BACKGROUND_ARCHIVING,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_ISOLATION_LEVEL,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_KEYWORD,
   (void *) &prm_log_isolation_level_flag,
   (void *) &prm_log_isolation_level_default,
   (void *) &PRM_LOG_ISOLATION_LEVEL,
   (void *) &prm_log_isolation_level_upper,
   (void *) &prm_log_isolation_level_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_MEDIA_FAILURE_SUPPORT,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) &prm_log_media_failure_support_flag,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_COMMIT_ON_SHUTDOWN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_commit_on_shutdown_flag,
   (void *) &prm_commit_on_shutdown_default,
   (void *) &PRM_COMMIT_ON_SHUTDOWN,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SHUTDOWN_WAIT_TIME_IN_SECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_shutdown_wait_time_in_secs_flag,
   (void *) &prm_shutdown_wait_time_in_secs_default,
   (void *) &PRM_SHUTDOWN_WAIT_TIME_IN_SECS,
   (void *) NULL, (void *) &prm_shutdown_wait_time_in_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CSQL_AUTO_COMMIT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_csql_auto_commit_flag,
   (void *) &prm_csql_auto_commit_default,
   (void *) &PRM_CSQL_AUTO_COMMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_SWEEP_CLEAN,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_log_sweep_clean_flag,
   (void *) &prm_log_sweep_clean_default,
   (void *) &PRM_LOG_SWEEP_CLEAN,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_WS_HASHTABLE_SIZE,
   (PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_ws_hashtable_size_flag,
   (void *) &prm_ws_hashtable_size_default,
   (void *) &PRM_WS_HASHTABLE_SIZE,
   (void *) NULL, (void *) &prm_ws_hashtable_size_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_WS_MEMORY_REPORT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_ws_memory_report_flag,
   (void *) &prm_ws_memory_report_default,
   (void *) &PRM_WS_MEMORY_REPORT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_GC_ENABLE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_gc_enable_flag,
   (void *) &prm_gc_enable_default,
   (void *) &PRM_GC_ENABLE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TCP_PORT_ID,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_tcp_port_id_flag,
   (void *) &prm_tcp_port_id_default,
   (void *) &PRM_TCP_PORT_ID,
   (void *) &prm_tcp_port_id_upper, (void *) &prm_tcp_port_id_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TCP_CONNECTION_TIMEOUT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_INTEGER,
   (void *) &prm_tcp_connection_timeout_flag,
   (void *) &prm_tcp_connection_timeout_default,
   (void *) &PRM_TCP_CONNECTION_TIMEOUT,
   (void *) NULL, (void *) &prm_tcp_connection_timeout_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZATION_LEVEL,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_INTEGER,
   (void *) &prm_optimization_level_flag,
   (void *) &prm_optimization_level_default,
   (void *) &PRM_OPTIMIZATION_LEVEL,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_QO_DUMP,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_qo_dump_flag,
   (void *) &prm_qo_dump_default,
   (void *) &PRM_QO_DUMP,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CSS_MAX_CLIENTS,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_css_max_clients_flag,
   (void *) &prm_css_max_clients_default,
   (void *) &PRM_CSS_MAX_CLIENTS,
   (void *) NULL, (void *) &prm_css_max_clients_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_THREAD_STACKSIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   (void *) &prm_thread_stacksize_flag,
   (void *) &prm_thread_stacksize_default,
   (void *) &PRM_THREAD_STACKSIZE,
   (void *) &prm_thread_stacksize_upper,
   (void *) &prm_thread_stacksize_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CFG_DB_HOSTS,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_STRING,
   (void *) &prm_cfg_db_hosts_flag,
   (void *) &prm_cfg_db_hosts_default,
   (void *) &PRM_CFG_DB_HOSTS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_RESET_TR_PARSER,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_reset_tr_parser_flag,
   (void *) &prm_reset_tr_parser_default,
   (void *) &PRM_RESET_TR_PARSER,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_IO_BACKUP_NBUFFERS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_io_backup_nbuffers_flag,
   (void *) &prm_io_backup_nbuffers_default,
   (void *) &PRM_IO_BACKUP_NBUFFERS,
   (void *) NULL, (void *) &prm_io_backup_nbuffers_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_IO_BACKUP_MAX_VOLUME_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   (void *) &prm_io_backup_max_volume_size_flag,
   (void *) &prm_io_backup_max_volume_size_default,
   (void *) &PRM_IO_BACKUP_MAX_VOLUME_SIZE,
   (void *) &prm_io_backup_max_volume_size_upper,
   (void *) &prm_io_backup_max_volume_size_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_IO_BACKUP_SLEEP_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_io_backup_sleep_msecs_flag,
   (void *) &prm_io_backup_sleep_msecs_default,
   (void *) &PRM_IO_BACKUP_SLEEP_MSECS,
   (void *) NULL, (void *) &prm_io_backup_sleep_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MAX_PAGES_IN_TEMP_FILE_CACHE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_max_pages_in_temp_file_cache_flag,
   (void *) &prm_max_pages_in_temp_file_cache_default,
   (void *) &PRM_MAX_PAGES_IN_TEMP_FILE_CACHE,
   (void *) NULL, (void *) &prm_max_pages_in_temp_file_cache_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_max_entries_in_temp_file_cache_flag,
   (void *) &prm_max_entries_in_temp_file_cache_default,
   (void *) &PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
   (void *) NULL, (void *) &prm_max_entries_in_temp_file_cache_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PTHREAD_SCOPE_PROCESS,	/* AIX only */
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_pthread_scope_process_flag,
   (void *) &prm_pthread_scope_process_default,
   (void *) &PRM_PTHREAD_SCOPE_PROCESS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TEMP_MEM_BUFFER_PAGES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_temp_mem_buffer_pages_flag,
   (void *) &prm_temp_mem_buffer_pages_default,
   (void *) &PRM_TEMP_MEM_BUFFER_PAGES,
   (void *) &prm_temp_mem_buffer_pages_upper,
   (void *) &prm_temp_mem_buffer_pages_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INDEX_SCAN_KEY_BUFFER_PAGES,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_index_scan_key_buffer_pages_flag,
   (void *) &prm_index_scan_key_buffer_pages_default,
   (void *) &PRM_INDEX_SCAN_KEY_BUFFER_PAGES,
   (void *) NULL,
   (void *) &prm_index_scan_key_buffer_pages_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INDEX_SCAN_KEY_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_index_scan_key_buffer_pages_flag,
   (void *) &prm_index_scan_key_buffer_pages_default,
   (void *) &PRM_INDEX_SCAN_KEY_BUFFER_PAGES,
   (void *) NULL,
   (void *) &prm_index_scan_key_buffer_pages_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_NAME_DONT_REUSE_HEAP_FILE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_dont_reuse_heap_file_flag,
   (void *) &prm_dont_reuse_heap_file_default,
   (void *) &PRM_DONT_REUSE_HEAP_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_QUERY_MODE_SYNC,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_query_mode_sync_flag,
   (void *) &prm_query_mode_sync_default,
   (void *) &PRM_QUERY_MODE_SYNC,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INSERT_MODE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_insert_mode_flag,
   (void *) &prm_insert_mode_default,
   (void *) &PRM_INSERT_MODE,
   (void *) &prm_insert_mode_upper,
   (void *) &prm_insert_mode_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LK_MAX_SCANID_BIT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_lk_max_scanid_bit_flag,
   (void *) &prm_lk_max_scanid_bit_default,
   (void *) &PRM_LK_MAX_SCANID_BIT,
   (void *) &prm_lk_max_scanid_bit_upper,
   (void *) &prm_lk_max_scanid_bit_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HOSTVAR_LATE_BINDING,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_hostvar_late_binding_flag,
   (void *) &prm_hostvar_late_binding_default,
   (void *) &PRM_HOSTVAR_LATE_BINDING,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ENABLE_HISTO,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_enable_histo_flag,
   (void *) &prm_enable_histo_default,
   (void *) &PRM_ENABLE_HISTO,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MUTEX_BUSY_WAITING_CNT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_mutex_busy_waiting_cnt_flag,
   (void *) &prm_mutex_busy_waiting_cnt_default,
   (void *) &PRM_MUTEX_BUSY_WAITING_CNT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PB_NUM_LRU_CHAINS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_pb_num_LRU_chains_flag,
   (void *) &prm_pb_num_LRU_chains_default,
   (void *) &PRM_PB_NUM_LRU_CHAINS,
   (void *) &prm_pb_num_LRU_chains_upper,
   (void *) &prm_pb_num_LRU_chains_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PAGE_BG_FLUSH_INTERVAL_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED),
   PRM_INTEGER,
   (void *) &prm_page_bg_flush_interval_msec_flag,
   (void *) &prm_page_bg_flush_interval_msec_default,
   (void *) &PRM_PAGE_BG_FLUSH_INTERVAL_MSEC,
   (void *) NULL, (void *) &prm_page_bg_flush_interval_msec_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PAGE_BG_FLUSH_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_page_bg_flush_interval_msec_flag,
   (void *) &prm_page_bg_flush_interval_msec_default,
   (void *) &PRM_PAGE_BG_FLUSH_INTERVAL_MSEC,
   (void *) NULL, (void *) &prm_page_bg_flush_interval_msec_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_equal_to_ori,
   (DUP_PRM_FUNC) prm_equal_to_ori},
  {PRM_NAME_ADAPTIVE_FLUSH_CONTROL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_adaptive_flush_control_flag,
   (void *) &prm_adaptive_flush_control_default,
   (void *) &PRM_ADAPTIVE_FLUSH_CONTROL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MAX_FLUSH_PAGES_PER_SECOND,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_max_flush_pages_per_second_flag,
   (void *) &prm_max_flush_pages_per_second_default,
   (void *) &PRM_MAX_FLUSH_PAGES_PER_SECOND,
   (void *) &prm_max_flush_pages_per_second_upper,
   (void *) &prm_max_flush_pages_per_second_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MAX_FLUSH_SIZE_PER_SECOND,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT |
    PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_max_flush_pages_per_second_flag,
   (void *) &prm_max_flush_pages_per_second_default,
   (void *) &PRM_MAX_FLUSH_PAGES_PER_SECOND,
   (void *) &prm_max_flush_pages_per_second_upper,
   (void *) &prm_max_flush_pages_per_second_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_NAME_PB_SYNC_ON_NFLUSH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_pb_sync_on_nflush_flag,
   (void *) &prm_pb_sync_on_nflush_default,
   (void *) &PRM_PB_SYNC_ON_NFLUSH,
   (void *) &prm_pb_sync_on_nflush_upper,
   (void *) &prm_pb_sync_on_nflush_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PB_SYNC_ON_FLUSH_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT |
    PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_pb_sync_on_nflush_flag,
   (void *) &prm_pb_sync_on_nflush_default,
   (void *) &PRM_PB_SYNC_ON_NFLUSH,
   (void *) &prm_pb_sync_on_nflush_upper,
   (void *) &prm_pb_sync_on_nflush_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_KEYWORD,
   (void *) &prm_pb_debug_page_validation_level_flag,
   (void *) &prm_pb_debug_page_validation_level_default,
   (void *) &PRM_PB_DEBUG_PAGE_VALIDATION_LEVEL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ORACLE_STYLE_OUTERJOIN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_oracle_style_outerjoin_flag,
   (void *) &prm_oracle_style_outerjoin_default,
   (void *) &PRM_ORACLE_STYLE_OUTERJOIN,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ANSI_QUOTES,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_ansi_quotes_flag,
   (void *) &prm_ansi_quotes_default,
   (void *) &PRM_ANSI_QUOTES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_DEFAULT_WEEK_FORMAT,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION
    | PRM_FOR_HA_CONTEXT),
   PRM_INTEGER,
   (void *) &prm_week_format_flag,
   (void *) &prm_week_format_default,
   (void *) &PRM_DEFAULT_WEEK_FORMAT,
   (void *) &prm_week_format_upper,
   (void *) &prm_week_format_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TEST_MODE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_test_mode_flag,
   (void *) &prm_test_mode_default,
   (void *) &PRM_TEST_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ONLY_FULL_GROUP_BY,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_only_full_group_by_flag,
   (void *) &prm_only_full_group_by_default,
   (void *) &PRM_ONLY_FULL_GROUP_BY,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PIPES_AS_CONCAT,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_pipes_as_concat_flag,
   (void *) &prm_pipes_as_concat_default,
   (void *) &PRM_PIPES_AS_CONCAT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MYSQL_TRIGGER_CORRELATION_NAMES,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_mysql_trigger_correlation_names_flag,
   (void *) &prm_mysql_trigger_correlation_names_default,
   (void *) &PRM_MYSQL_TRIGGER_CORRELATION_NAMES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_REQUIRE_LIKE_ESCAPE_CHARACTER,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE | PRM_FOR_QRY_STRING),
   PRM_BOOLEAN,
   (void *) &prm_require_like_escape_character_flag,
   (void *) &prm_require_like_escape_character_default,
   (void *) &PRM_REQUIRE_LIKE_ESCAPE_CHARACTER,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_NO_BACKSLASH_ESCAPES,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_no_backslash_escapes_flag,
   (void *) &prm_no_backslash_escapes_default,
   (void *) &PRM_NO_BACKSLASH_ESCAPES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_GROUP_CONCAT_MAX_LEN,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   (void *) &prm_group_concat_max_len_flag,
   (void *) &prm_group_concat_max_len_default,
   (void *) &PRM_GROUP_CONCAT_MAX_LEN,
   (void *) &prm_group_concat_max_len_upper,
   (void *) &prm_group_concat_max_len_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_STRING_MAX_SIZE_BYTES,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FOR_SESSION |
    PRM_SIZE_UNIT | PRM_FOR_HA_CONTEXT),
   PRM_BIGINT,
   (void *) &prm_string_max_size_bytes_flag,
   (void *) &prm_string_max_size_bytes_default,
   (void *) &PRM_STRING_MAX_SIZE_BYTES,
   (void *) &prm_string_max_size_bytes_upper,
   (void *) &prm_string_max_size_bytes_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ADD_COLUMN_UPDATE_HARD_DEFAULT,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION
    | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   (void *) &prm_add_column_update_hard_default_flag,
   (void *) &prm_add_column_update_hard_default_default,
   (void *) &PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_RETURN_NULL_ON_FUNCTION_ERRORS,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION |
    PRM_FOR_QRY_STRING | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   (void *) &prm_return_null_on_function_errors_flag,
   (void *) &prm_return_null_on_function_errors_default,
   (void *) &PRM_RETURN_NULL_ON_FUNCTION_ERRORS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ALTER_TABLE_CHANGE_TYPE_STRICT,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION
    | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   (void *) &prm_alter_table_change_type_strict_flag,
   (void *) &prm_alter_table_change_type_strict_default,
   (void *) &PRM_ALTER_TABLE_CHANGE_TYPE_STRICT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_COMPACTDB_PAGE_RECLAIM_ONLY,
   (PRM_EMPTY_FLAG),
   PRM_INTEGER,
   (void *) &prm_compactdb_page_reclaim_only_flag,
   (void *) &prm_compactdb_page_reclaim_only_default,
   (void *) &PRM_COMPACTDB_PAGE_RECLAIM_ONLY,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PLUS_AS_CONCAT,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_plus_as_concat_flag,
   (void *) &prm_plus_as_concat_default,
   (void *) &PRM_PLUS_AS_CONCAT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LIKE_TERM_SELECTIVITY,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_FLOAT,
   (void *) &prm_like_term_selectivity_flag,
   (void *) &prm_like_term_selectivity_default,
   (void *) &PRM_LIKE_TERM_SELECTIVITY,
   (void *) &prm_like_term_selectivity_upper,
   (void *) &prm_like_term_selectivity_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MAX_OUTER_CARD_OF_IDXJOIN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_max_outer_card_of_idxjoin_flag,
   (void *) &prm_max_outer_card_of_idxjoin_default,
   (void *) &PRM_MAX_OUTER_CARD_OF_IDXJOIN,
   (void *) NULL,
   (void *) &prm_max_outer_card_of_idxjoin_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ORACLE_STYLE_EMPTY_STRING,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_QRY_STRING | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_oracle_style_empty_string_flag,
   (void *) &prm_oracle_style_empty_string_default,
   (void *) &PRM_ORACLE_STYLE_EMPTY_STRING,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SUPPRESS_FSYNC,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_suppress_fsync_flag,
   (void *) &prm_suppress_fsync_default,
   (void *) &PRM_SUPPRESS_FSYNC,
   (void *) &prm_suppress_fsync_upper,
   (void *) &prm_suppress_fsync_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CALL_STACK_DUMP_ON_ERROR,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_call_stack_dump_on_error_flag,
   (void *) &prm_call_stack_dump_on_error_default,
   (void *) &PRM_CALL_STACK_DUMP_ON_ERROR,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CALL_STACK_DUMP_ACTIVATION,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_INTEGER_LIST,
   (void *) &prm_call_stack_dump_activation_flag,
   (void *) &prm_call_stack_dump_activation_default,
   (void *) &PRM_CALL_STACK_DUMP_ACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CALL_STACK_DUMP_DEACTIVATION,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_INTEGER_LIST,
   (void *) &prm_call_stack_dump_deactivation_flag,
   (void *) &prm_call_stack_dump_deactivation_default,
   (void *) &PRM_CALL_STACK_DUMP_DEACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_COMPAT_NUMERIC_DIVISION_SCALE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION
    | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   (void *) &prm_compat_numeric_division_scale_flag,
   (void *) &prm_compat_numeric_division_scale_default,
   (void *) &PRM_COMPAT_NUMERIC_DIVISION_SCALE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_DBFILES_PROTECT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_dbfiles_protect_flag,
   (void *) &prm_dbfiles_protect_default,
   (void *) &PRM_DBFILES_PROTECT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_AUTO_RESTART_SERVER,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_auto_restart_server_flag,
   (void *) &prm_auto_restart_server_default,
   (void *) &PRM_AUTO_RESTART_SERVER,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_XASL_MAX_PLAN_CACHE_ENTRIES,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_INTEGER,
   (void *) &prm_xasl_max_plan_cache_entries_flag,
   (void *) &prm_xasl_max_plan_cache_entries_default,
   (void *) &PRM_XASL_MAX_PLAN_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
#if defined (ENABLE_UNUSED_FUNCTION)
  {PRM_NAME_XASL_MAX_PLAN_CACHE_CLONES,
   (PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_xasl_max_plan_cache_clones_flag,
   (void *) &prm_xasl_max_plan_cache_clones_default,
   (void *) &PRM_XASL_MAX_PLAN_CACHE_CLONES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
#endif /* ENABLE_UNUSED_FUNCTION */
  {PRM_NAME_FILTER_PRED_MAX_CACHE_ENTRIES,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_filter_pred_max_cache_entries_flag,
   (void *) &prm_filter_pred_max_cache_entries_default,
   (void *) &PRM_FILTER_PRED_MAX_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_FILTER_PRED_MAX_CACHE_CLONES,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_filter_pred_max_cache_clones_flag,
   (void *) &prm_filter_pred_max_cache_clones_default,
   (void *) &PRM_FILTER_PRED_MAX_CACHE_CLONES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_XASL_PLAN_CACHE_TIMEOUT,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_xasl_plan_cache_timeout_flag,
   (void *) &prm_xasl_plan_cache_timeout_default,
   (void *) &PRM_XASL_PLAN_CACHE_TIMEOUT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LIST_QUERY_CACHE_MODE,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_list_query_cache_mode_flag,
   (void *) &prm_list_query_cache_mode_default,
   (void *) &PRM_LIST_QUERY_CACHE_MODE,
   (void *) &prm_list_query_cache_mode_upper,
   (void *) &prm_list_query_cache_mode_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LIST_MAX_QUERY_CACHE_ENTRIES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_list_max_query_cache_entries_flag,
   (void *) &prm_list_max_query_cache_entries_default,
   (void *) &PRM_LIST_MAX_QUERY_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LIST_MAX_QUERY_CACHE_PAGES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_list_max_query_cache_pages_flag,
   (void *) &prm_list_max_query_cache_pages_default,
   (void *) &PRM_LIST_MAX_QUERY_CACHE_PAGES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_USE_ORDERBY_SORT_LIMIT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_use_orderby_sort_limit_flag,
   (void *) &prm_use_orderby_sort_limit_default,
   (void *) &PRM_USE_ORDERBY_SORT_LIMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_REPLICATION_MODE,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) &prm_replication_mode_flag,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_MODE,
   (PRM_FOR_SERVER | PRM_FOR_HA | PRM_FORCE_SERVER),
   PRM_KEYWORD,
   (void *) &prm_ha_mode_flag,
   (void *) &prm_ha_mode_default,
   (void *) &PRM_HA_MODE,
   (void *) &prm_ha_mode_upper,
   (void *) &prm_ha_mode_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY,
   (PRM_EMPTY_FLAG),
   PRM_KEYWORD,
   (void *) &prm_ha_mode_flag,
   (void *) &prm_ha_mode_default,
   (void *) &PRM_HA_MODE_FOR_SA_UTILS_ONLY,
   (void *) &prm_ha_mode_upper,
   (void *) &prm_ha_mode_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_SERVER_STATE,
   (PRM_FOR_SERVER | PRM_HIDDEN | PRM_FOR_HA),
   PRM_KEYWORD,
   (void *) &prm_ha_server_state_flag,
   (void *) &prm_ha_server_state_default,
   (void *) &PRM_HA_SERVER_STATE,
   (void *) &prm_ha_server_state_upper,
   (void *) &prm_ha_server_state_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_LOG_APPLIER_STATE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN | PRM_FOR_HA),
   PRM_KEYWORD,
   (void *) &prm_ha_log_applier_state_flag,
   (void *) &prm_ha_log_applier_state_default,
   (void *) &PRM_HA_LOG_APPLIER_STATE,
   (void *) &prm_ha_log_applier_state_upper,
   (void *) &prm_ha_log_applier_state_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_NODE_LIST,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_RELOADABLE |
    PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_node_list_flag,
   (void *) &prm_ha_node_list_default,
   (void *) &PRM_HA_NODE_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_REPLICA_LIST,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_replica_list_flag,
   (void *) &prm_ha_replica_list_default,
   (void *) &PRM_HA_REPLICA_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_DB_LIST,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_db_list_flag,
   (void *) &prm_ha_db_list_default,
   (void *) &PRM_HA_DB_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_COPY_LOG_BASE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_copy_log_base_flag,
   (void *) &prm_ha_copy_log_base_default,
   (void *) &PRM_HA_COPY_LOG_BASE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_COPY_SYNC_MODE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_copy_sync_mode_flag,
   (void *) &prm_ha_copy_sync_mode_default,
   (void *) &PRM_HA_COPY_SYNC_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_APPLY_MAX_MEM_SIZE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_apply_max_mem_size_flag,
   (void *) &prm_ha_apply_max_mem_size_default,
   (void *) &PRM_HA_APPLY_MAX_MEM_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PORT_ID,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_port_id_flag,
   (void *) &prm_ha_port_id_default,
   (void *) &PRM_HA_PORT_ID,
   (void *) &prm_ha_port_id_upper, (void *) &prm_ha_port_id_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_INIT_TIMER_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_init_timer_im_msecs_flag,
   (void *) &prm_ha_init_timer_im_msecs_default,
   (void *) &PRM_HA_INIT_TIMER_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_HEARTBEAT_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_heartbeat_interval_in_msecs_flag,
   (void *) &prm_ha_heartbeat_interval_in_msecs_default,
   (void *) &PRM_HA_HEARTBEAT_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_CALC_SCORE_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_calc_score_interval_in_msecs_flag,
   (void *) &prm_ha_calc_score_interval_in_msecs_default,
   (void *) &PRM_HA_CALC_SCORE_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_FAILOVER_WAIT_TIME_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_failover_wait_time_in_msecs_flag,
   (void *) &prm_ha_failover_wait_time_in_msecs_default,
   (void *) &PRM_HA_FAILOVER_WAIT_TIME_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_process_start_confirm_interval_in_msecs_flag,
   (void *) &prm_ha_process_start_confirm_interval_in_msecs_default,
   (void *) &PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_process_dereg_confirm_interval_in_msecs_flag,
   (void *) &prm_ha_process_dereg_confirm_interval_in_msecs_default,
   (void *) &PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_MAX_PROCESS_START_CONFIRM,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_max_process_start_confirm_flag,
   (void *) &prm_ha_max_process_start_confirm_default,
   (void *) &PRM_HA_MAX_PROCESS_START_CONFIRM,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_MAX_PROCESS_DEREG_CONFIRM,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_max_process_dereg_confirm_flag,
   (void *) &prm_ha_max_process_dereg_confirm_default,
   (void *) &PRM_HA_MAX_PROCESS_DEREG_CONFIRM,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT),
   PRM_INTEGER,
   (void *) &prm_ha_unacceptable_proc_restart_timediff_flag,
   (void *) &prm_ha_unacceptable_proc_restart_timediff_default,
   (void *) &PRM_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_CHANGEMODE_INTERVAL_IN_MSEC,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_changemode_interval_in_msecs_flag,
   (void *) &prm_ha_changemode_interval_in_msecs_default,
   (void *) &PRM_HA_CHANGEMODE_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_MAX_HEARTBEAT_GAP,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_max_heartbeat_gap_flag,
   (void *) &prm_ha_max_heartbeat_gap_default,
   (void *) &PRM_HA_MAX_HEARTBEAT_GAP,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PING_HOSTS,
   (PRM_FOR_CLIENT | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_ping_hosts_flag,
   (void *) &prm_ha_ping_hosts_default,
   (void *) &PRM_HA_PING_HOSTS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_APPLYLOGDB_RETRY_ERROR_LIST,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER_LIST,
   (void *) &prm_ha_applylogdb_retry_error_list_flag,
   (void *) &prm_ha_applylogdb_retry_error_list_default,
   (void *) &PRM_HA_APPLYLOGDB_RETRY_ERROR_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER_LIST,
   (void *) &prm_ha_applylogdb_ignore_error_list_flag,
   (void *) &prm_ha_applylogdb_ignore_error_list_default,
   (void *) &PRM_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_ha_applylogdb_log_wait_time_in_secs_flag,
   (void *) &prm_ha_applylogdb_log_wait_time_in_secs_default,
   (void *) &PRM_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS,
   (void *) NULL, (void *) &prm_ha_applylogdb_log_wait_time_in_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_SQL_LOGGING,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_BOOLEAN,
   (void *) &prm_ha_sql_logging_flag,
   (void *) &prm_ha_sql_logging_default,
   (void *) &PRM_HA_SQL_LOGGING,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_SQL_LOG_MAX_SIZE_IN_MB,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_sql_log_max_size_in_mb_flag,
   (void *) &prm_ha_sql_log_max_size_in_mb_default,
   (void *) &PRM_HA_SQL_LOG_MAX_SIZE_IN_MB,
   (void *) &prm_ha_sql_log_max_size_in_mb_upper,
   (void *) &prm_ha_sql_log_max_size_in_mb_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_COPY_LOG_MAX_ARCHIVES,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_copy_log_max_archives_flag,
   (void *) &prm_ha_copy_log_max_archives_default,
   (void *) &PRM_HA_COPY_LOG_MAX_ARCHIVES,
   (void *) &prm_ha_copy_log_max_archives_upper,
   (void *) &prm_ha_copy_log_max_archives_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_COPY_LOG_TIMEOUT,
   (PRM_FOR_SERVER | PRM_FOR_HA | PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_ha_copy_log_timeout_flag,
   (void *) &prm_ha_copy_log_timeout_default,
   (void *) &PRM_HA_COPY_LOG_TIMEOUT,
   (void *) &prm_ha_copy_log_timeout_upper,
   (void *) &prm_ha_copy_log_timeout_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_REPLICA_DELAY,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_ha_replica_delay_in_secs_flag,
   (void *) &prm_ha_replica_delay_in_secs_default,
   (void *) &PRM_HA_REPLICA_DELAY_IN_SECS,
   (void *) &prm_ha_replica_delay_in_secs_upper,
   (void *) &prm_ha_replica_delay_in_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_NAME_HA_REPLICA_TIME_BOUND,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_replica_time_bound_flag,
   (void *) &prm_ha_replica_time_bound_default,
   (void *) &PRM_HA_REPLICA_TIME_BOUND,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_DELAY_LIMIT,
   (PRM_FOR_SERVER | PRM_FOR_HA | PRM_USER_CHANGE | PRM_TIME_UNIT |
    PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_ha_delay_limit_in_secs_flag,
   (void *) &prm_ha_delay_limit_in_secs_default,
   (void *) &PRM_HA_DELAY_LIMIT_IN_SECS,
   (void *) &prm_ha_delay_limit_in_secs_upper,
   (void *) &prm_ha_delay_limit_in_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_NAME_HA_DELAY_LIMIT_DELTA,
   (PRM_FOR_SERVER | PRM_FOR_HA | PRM_USER_CHANGE | PRM_TIME_UNIT |
    PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_ha_delay_limit_delta_in_secs_flag,
   (void *) &prm_ha_delay_limit_delta_in_secs_default,
   (void *) &PRM_HA_DELAY_LIMIT_DELTA_IN_SECS,
   (void *) &prm_ha_delay_limit_delta_in_secs_upper,
   (void *) &prm_ha_delay_limit_delta_in_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_NAME_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_flag,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_default,
   (void *) &PRM_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_upper,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT),
   PRM_INTEGER,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_flag,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_default,
   (void *) &PRM_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_upper,
   (void *) &prm_ha_applylogdb_max_commit_interval_in_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT | PRM_DIFFER_UNIT |
    PRM_RELOADABLE),
   PRM_INTEGER,
   (void *) &prm_ha_check_disk_failure_interval_in_secs_flag,
   (void *) &prm_ha_check_disk_failure_interval_in_secs_default,
   (void *) &PRM_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS,
   (void *) &prm_ha_check_disk_failure_interval_in_secs_upper,
   (void *) &prm_ha_check_disk_failure_interval_in_secs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_NAME_JAVA_STORED_PROCEDURE,
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_java_stored_procedure_flag,
   (void *) &prm_java_stored_procedure_default,
   (void *) &PRM_JAVA_STORED_PROCEDURE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_COMPAT_PRIMARY_KEY,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   (void *) &prm_compat_primary_key_flag,
   (void *) &prm_compat_primary_key_default,
   (void *) &PRM_COMPAT_PRIMARY_KEY,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_HEADER_FLUSH_INTERVAL,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) &prm_log_header_flush_interval_flag,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_ASYNC_COMMIT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_log_async_commit_flag,
   (void *) &prm_log_async_commit_default,
   (void *) &PRM_LOG_ASYNC_COMMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_GROUP_COMMIT_INTERVAL_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_log_group_commit_interval_msecs_flag,
   (void *) &prm_log_group_commit_interval_msecs_default,
   (void *) &PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS,
   (void *) NULL, (void *) &prm_log_group_commit_interval_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_BG_FLUSH_INTERVAL_MSECS,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) &prm_log_bg_flush_interval_msecs_flag,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_BG_FLUSH_NUM_PAGES,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) &prm_log_bg_flush_num_pages_flag,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INTL_MBS_SUPPORT,
   (PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   (void *) &prm_intl_mbs_support_flag,
   (void *) &prm_intl_mbs_support_default,
   (void *) &PRM_INTL_MBS_SUPPORT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_COMPRESS,
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_log_compress_flag,
   (void *) &prm_log_compress_default,
   (void *) &PRM_LOG_COMPRESS,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_BLOCK_NOWHERE_STATEMENT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_block_nowhere_statement_flag,
   (void *) &prm_block_nowhere_statement_default,
   (void *) &PRM_BLOCK_NOWHERE_STATEMENT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_BLOCK_DDL_STATEMENT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_block_ddl_statement_flag,
   (void *) &prm_block_ddl_statement_default,
   (void *) &PRM_BLOCK_DDL_STATEMENT,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
#if defined (ENABLE_UNUSED_FUNCTION)
  {PRM_NAME_SINGLE_BYTE_COMPARE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_single_byte_compare_flag,
   (void *) &prm_single_byte_compare_default,
   (void *) &PRM_SINGLE_BYTE_COMPARE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
#endif
  {PRM_NAME_CSQL_HISTORY_NUM,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_INTEGER,
   (void *) &prm_csql_history_num_flag,
   (void *) &prm_csql_history_num_default,
   (void *) &PRM_CSQL_HISTORY_NUM,
   (void *) &prm_csql_history_num_upper,
   (void *) &prm_csql_history_num_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_TRACE_DEBUG,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_log_trace_debug_flag,
   (void *) &prm_log_trace_debug_default,
   (void *) &PRM_LOG_TRACE_DEBUG,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_DL_FORK,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_STRING,
   (void *) &prm_dl_fork_flag,
   (void *) &prm_dl_fork_default,
   (void *) &PRM_DL_FORK,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_PRODUCTION_MODE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_er_production_mode_flag,
   (void *) &prm_er_production_mode_default,
   (void *) &PRM_ER_PRODUCTION_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_STOP_ON_ERROR,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_er_stop_on_error_flag,
   (void *) &prm_er_stop_on_error_default,
   (void *) &PRM_ER_STOP_ON_ERROR,
   (void *) &prm_er_stop_on_error_upper, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TCP_RCVBUF_SIZE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_tcp_rcvbuf_size_flag,
   (void *) &prm_tcp_rcvbuf_size_default,
   (void *) &PRM_TCP_RCVBUF_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TCP_SNDBUF_SIZE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_tcp_sndbuf_size_flag,
   (void *) &prm_tcp_sndbuf_size_default,
   (void *) &PRM_TCP_SNDBUF_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TCP_NODELAY,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_tcp_nodelay_flag,
   (void *) &prm_tcp_nodelay_default,
   (void *) &PRM_TCP_NODELAY,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_TCP_KEEPALIVE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   (void *) &prm_tcp_keepalive_flag,
   (void *) &prm_tcp_keepalive_default,
   (void *) &PRM_TCP_KEEPALIVE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CSQL_SINGLE_LINE_MODE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_csql_single_line_mode_flag,
   (void *) &prm_csql_single_line_mode_default,
   (void *) &PRM_CSQL_SINGLE_LINE_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_XASL_DEBUG_DUMP,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_xasl_debug_dump_flag,
   (void *) &prm_xasl_debug_dump_default,
   (void *) &PRM_XASL_DEBUG_DUMP,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_MAX_ARCHIVES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_log_max_archives_flag,
   (void *) &prm_log_max_archives_default,
   (void *) &PRM_LOG_MAX_ARCHIVES,
   (void *) NULL, (void *) &prm_log_max_archives_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_FORCE_REMOVE_LOG_ARCHIVES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_force_remove_log_archives_flag,
   (void *) &prm_force_remove_log_archives_default,
   (void *) &PRM_FORCE_REMOVE_LOG_ARCHIVES,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_REMOVE_LOG_ARCHIVES_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_remove_log_archives_interval_flag,
   (void *) &prm_remove_log_archives_interval_default,
   (void *) &PRM_REMOVE_LOG_ARCHIVES_INTERVAL,
   (void *) NULL, (void *) &prm_remove_log_archives_interval_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_NO_LOGGING,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_log_no_logging_flag,
   (void *) &prm_log_no_logging_default,
   (void *) &PRM_LOG_NO_LOGGING,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_UNLOADDB_IGNORE_ERROR,
   (PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_unloaddb_ignore_error_flag,
   (void *) &prm_unloaddb_ignore_error_default,
   (void *) &PRM_UNLOADDB_IGNORE_ERROR,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_UNLOADDB_LOCK_TIMEOUT,
   (PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_unloaddb_lock_timeout_flag,
   (void *) &prm_unloaddb_lock_timeout_default,
   (void *) &PRM_UNLOADDB_LOCK_TIMEOUT,
   (void *) NULL, (void *) &prm_unloaddb_lock_timeout_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOADDB_FLUSH_INTERVAL,
   (PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_loaddb_flush_interval_flag,
   (void *) &prm_loaddb_flush_interval_default,
   (void *) &PRM_LOADDB_FLUSH_INTERVAL,
   (void *) NULL, (void *) &prm_loaddb_flush_interval_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_IO_TEMP_VOLUME_PATH,
   (PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_io_temp_volume_path_flag,
   (void *) &prm_io_temp_volume_path_default,
   (void *) &PRM_IO_TEMP_VOLUME_PATH,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_IO_VOLUME_EXT_PATH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_STRING,
   (void *) &prm_io_volume_ext_path_flag,
   (void *) &prm_io_volume_ext_path_default,
   (void *) &PRM_IO_VOLUME_EXT_PATH,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_UNIQUE_ERROR_KEY_VALUE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN | PRM_DEPRECATED),
   PRM_BOOLEAN,
   (void *) &prm_unique_error_key_value_flag,
   (void *) &prm_unique_error_key_value_default,
   (void *) &PRM_UNIQUE_ERROR_KEY_VALUE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_USE_SYSTEM_MALLOC,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_use_system_malloc_flag,
   (void *) &prm_use_system_malloc_default,
   (void *) &PRM_USE_SYSTEM_MALLOC,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_EVENT_HANDLER,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_event_handler_flag,
   (void *) &prm_event_handler_default,
   (void *) &PRM_EVENT_HANDLER,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_EVENT_ACTIVATION,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER_LIST,
   (void *) &prm_event_activation_flag,
   (void *) &prm_event_activation_default,
   (void *) &PRM_EVENT_ACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_READ_ONLY_MODE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   (void *) &prm_read_only_mode_flag,
   (void *) &prm_read_only_mode_default,
   (void *) &PRM_READ_ONLY_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MNT_WAITING_THREAD,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_mnt_waiting_thread_flag,
   (void *) &prm_mnt_waiting_thread_default,
   (void *) &PRM_MNT_WAITING_THREAD,
   (void *) NULL, (void *) &prm_mnt_waiting_thread_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MNT_STATS_THRESHOLD,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER_LIST,
   (void *) &prm_mnt_stats_threshold_flag,
   (void *) &prm_mnt_stats_threshold_default,
   (void *) &PRM_MNT_STATS_THRESHOLD,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SERVICE_SERVICE_LIST,
   (PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_service_service_list_flag,
   (void *) &prm_service_service_list_default,
   (void *) &PRM_SERVICE_SERVICE_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SERVICE_SERVER_LIST,
   (PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_service_server_list_flag,
   (void *) &prm_service_server_list_default,
   (void *) &PRM_SERVICE_SERVER_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SESSION_STATE_TIMEOUT,
   (PRM_FOR_SERVER | PRM_TEST_CHANGE),
   PRM_INTEGER,
   (void *) &prm_session_timeout_flag,
   (void *) &prm_session_timeout_default,
   (void *) &PRM_SESSION_STATE_TIMEOUT,
   (void *) &prm_session_timeout_upper,
   (void *) &prm_session_timeout_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MULTI_RANGE_OPT_LIMIT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING),
   PRM_INTEGER,
   (void *) &prm_multi_range_opt_limit_flag,
   (void *) &prm_multi_range_opt_limit_default,
   (void *) &PRM_MULTI_RANGE_OPT_LIMIT,
   (void *) &prm_multi_range_opt_limit_upper,
   (void *) &prm_multi_range_opt_limit_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INTL_NUMBER_LANG,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING
    | PRM_FOR_HA_CONTEXT),
   PRM_STRING,
   (void *) &prm_intl_number_lang_flag,
   (void *) &prm_intl_number_lang_default,
   (void *) &PRM_INTL_NUMBER_LANG,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INTL_DATE_LANG,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING
    | PRM_FOR_HA_CONTEXT),
   PRM_STRING,
   (void *) &prm_intl_date_lang_flag,
   (void *) &prm_intl_date_lang_default,
   (void *) &PRM_INTL_DATE_LANG,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  /* All the compound parameters *must* be at the end of the array so that the
     changes they cause are not overridden by other parameters (for example in
     sysprm_load_and_init the parameters are set to their default in the order
     they are found in this array). */
  {PRM_NAME_COMPAT_MODE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_TEST_CHANGE | PRM_COMPOUND),
   PRM_KEYWORD,
   (void *) &prm_compat_mode_flag,
   (void *) &prm_compat_mode_default,
   (void *) &PRM_COMPAT_MODE,
   (void *) &prm_compat_mode_upper, (void *) &prm_compat_mode_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_DB_VOLUME_SIZE,
   (PRM_SIZE_UNIT),
   PRM_BIGINT,
   (void *) &prm_db_volume_size_flag,
   (void *) &prm_db_volume_size_default,
   (void *) &PRM_DB_VOLUME_SIZE,
   (void *) &prm_db_volume_size_upper,
   (void *) &prm_db_volume_size_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_VOLUME_SIZE,
   (PRM_SIZE_UNIT),
   PRM_BIGINT,
   (void *) &prm_log_volume_size_flag,
   (void *) &prm_log_volume_size_default,
   (void *) &PRM_LOG_VOLUME_SIZE,
   (void *) &prm_log_volume_size_upper,
   (void *) &prm_log_volume_size_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_UNICODE_INPUT_NORMALIZATION,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_unicode_input_normalization_flag,
   (void *) &prm_unicode_input_normalization_default,
   (void *) &PRM_UNICODE_INPUT_NORMALIZATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_UNICODE_OUTPUT_NORMALIZATION,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_unicode_output_normalization_flag,
   (void *) &prm_unicode_output_normalization_default,
   (void *) &PRM_UNICODE_OUTPUT_NORMALIZATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INTL_CHECK_INPUT_STRING,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_intl_check_input_string_flag,
   (void *) &prm_intl_check_input_string_default,
   (void *) &PRM_INTL_CHECK_INPUT_STRING,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_CHECK_PEER_ALIVE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION |
    PRM_CLIENT_SESSION),
   PRM_KEYWORD,
   (void *) &prm_check_peer_alive_flag,
   (void *) &prm_check_peer_alive_default,
   (void *) &PRM_CHECK_PEER_ALIVE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SQL_TRACE_SLOW_MSECS,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_DEPRECATED),
   PRM_INTEGER,
   (void *) &prm_sql_trace_slow_msecs_flag,
   (void *) &prm_sql_trace_slow_msecs_default,
   (void *) &PRM_SQL_TRACE_SLOW_MSECS,
   (void *) &prm_sql_trace_slow_msecs_upper,
   (void *) &prm_sql_trace_slow_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SQL_TRACE_SLOW,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   (void *) &prm_sql_trace_slow_msecs_flag,
   (void *) &prm_sql_trace_slow_msecs_default,
   (void *) &PRM_SQL_TRACE_SLOW_MSECS,
   (void *) &prm_sql_trace_slow_msecs_upper,
   (void *) &prm_sql_trace_slow_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_equal_to_ori,
   (DUP_PRM_FUNC) prm_equal_to_ori},
  {PRM_NAME_SQL_TRACE_EXECUTION_PLAN,
   (PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_sql_trace_execution_plan_flag,
   (void *) &prm_sql_trace_execution_plan_default,
   (void *) &PRM_SQL_TRACE_EXECUTION_PLAN,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_TRACE_FLUSH_TIME,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_TIME_UNIT),
   PRM_INTEGER,
   (void *) &prm_log_trace_flush_time_msecs_flag,
   (void *) &prm_log_trace_flush_time_msecs_default,
   (void *) &PRM_LOG_TRACE_FLUSH_TIME_MSECS,
   (void *) NULL,
   (void *) &prm_log_trace_flush_time_msecs_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_INTL_COLLATION,
   (PRM_FOR_CLIENT | PRM_FOR_SESSION | PRM_USER_CHANGE | PRM_FOR_HA_CONTEXT),
   PRM_STRING,
   (void *) &prm_intl_collation_flag,
   (void *) &prm_intl_collation_default,
   (void *) &PRM_INTL_COLLATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_GENERIC_VOL_PREALLOC_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT),
   PRM_BIGINT,
   (void *) &prm_generic_vol_prealloc_size_flag,
   (void *) &prm_generic_vol_prealloc_size_default,
   (void *) &PRM_GENERIC_VOL_PREALLOC_SIZE,
   (void *) &prm_generic_vol_prealloc_size_upper,
   (void *) &prm_generic_vol_prealloc_size_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SORT_LIMIT_MAX_COUNT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING),
   PRM_INTEGER,
   (void *) &prm_sort_limit_max_count_flag,
   (void *) &prm_sort_limit_max_count_default,
   (void *) &PRM_SORT_LIMIT_MAX_COUNT,
   (void *) &prm_sort_limit_max_count_upper,
   (void *) &prm_sort_limit_max_count_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_SQL_TRACE_IOREADS,
   (PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_sql_trace_ioreads_flag,
   (void *) &prm_sql_trace_ioreads_default,
   (void *) &PRM_SQL_TRACE_IOREADS,
   (void *) NULL,
   (void *) &prm_sql_trace_ioreads_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_QUERY_TRACE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_query_trace_flag,
   (void *) &prm_query_trace_default,
   (void *) &PRM_QUERY_TRACE,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_QUERY_TRACE_FORMAT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_KEYWORD,
   (void *) &prm_query_trace_format_flag,
   (void *) &prm_query_trace_format_default,
   (void *) &PRM_QUERY_TRACE_FORMAT,
   (void *) &prm_query_trace_format_upper,
   (void *) &prm_query_trace_format_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MAX_RECURSION_SQL_DEPTH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_max_recursion_sql_depth_flag,
   (void *) &prm_max_recursion_sql_depth_default,
   (void *) &PRM_MAX_RECURSION_SQL_DEPTH,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_UPDATE_USE_ATTRIBUTE_REFERENCES,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_update_use_attribute_references_flag,
   (void *) &prm_update_use_attribute_references_default,
   (void *) &PRM_UPDATE_USE_ATTRIBUTE_REFERENCES,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PB_AIN_RATIO,
   (PRM_FOR_SERVER | PRM_RELOADABLE),
   PRM_FLOAT,
   (void *) &prm_pb_ain_ratio_flag,
   (void *) &prm_pb_ain_ratio_default,
   (void *) &PRM_PB_AIN_RATIO,
   (void *) &prm_pb_ain_ratio_upper,
   (void *) &prm_pb_ain_ratio_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PB_AOUT_RATIO,
   (PRM_FOR_SERVER | PRM_RELOADABLE),
   PRM_FLOAT,
   (void *) &prm_pb_aout_ratio_flag,
   (void *) &prm_pb_aout_ratio_default,
   (void *) &PRM_PB_AOUT_RATIO,
   (void *) &prm_pb_aout_ratio_upper,
   (void *) &prm_pb_aout_ratio_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MAX_AGG_HASH_SIZE,
   (PRM_FOR_SERVER | PRM_TEST_CHANGE | PRM_SIZE_UNIT),
   PRM_BIGINT,
   (void *) &prm_max_agg_hash_size_flag,
   (void *) &prm_max_agg_hash_size_default,
   (void *) &PRM_MAX_AGG_HASH_SIZE,
   (void *) &prm_max_agg_hash_size_upper,
   (void *) &prm_max_agg_hash_size_lower,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_AGG_HASH_RESPECT_ORDER,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_agg_hash_respect_order_flag,
   (void *) &prm_agg_hash_respect_order_default,
   (void *) &PRM_AGG_HASH_RESPECT_ORDER,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_USE_BTREE_FENCE_KEY,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_use_btree_fence_key_flag,
   (void *) &prm_use_btree_fence_key_default,
   (void *) &PRM_USE_BTREE_FENCE_KEY,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_QA_BTREE_RANDOM_EXIT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_qa_btree_random_exit_flag,
   (void *) &prm_qa_btree_random_exit_default,
   (void *) &PRM_QA_BTREE_RANDOM_EXIT,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_ENABLE_MERGE_JOIN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_enable_merge_join_flag,
   (void *) &prm_optimizer_enable_merge_join_default,
   (void *) &PRM_OPTIMIZER_ENABLE_MERGE_JOIN,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_01,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_01_flag,
   (void *) &prm_optimizer_reserve_01_default,
   (void *) &PRM_OPTIMIZER_RESERVE_01,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_02,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_02_flag,
   (void *) &prm_optimizer_reserve_02_default,
   (void *) &PRM_OPTIMIZER_RESERVE_02,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_03,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_03_flag,
   (void *) &prm_optimizer_reserve_03_default,
   (void *) &PRM_OPTIMIZER_RESERVE_03,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_04,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_04_flag,
   (void *) &prm_optimizer_reserve_04_default,
   (void *) &PRM_OPTIMIZER_RESERVE_04,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_05,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_05_flag,
   (void *) &prm_optimizer_reserve_05_default,
   (void *) &PRM_OPTIMIZER_RESERVE_05,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_06,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_06_flag,
   (void *) &prm_optimizer_reserve_06_default,
   (void *) &PRM_OPTIMIZER_RESERVE_06,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_07,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_07_flag,
   (void *) &prm_optimizer_reserve_07_default,
   (void *) &PRM_OPTIMIZER_RESERVE_07,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_08,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_08_flag,
   (void *) &prm_optimizer_reserve_08_default,
   (void *) &PRM_OPTIMIZER_RESERVE_08,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_09,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_09_flag,
   (void *) &prm_optimizer_reserve_09_default,
   (void *) &PRM_OPTIMIZER_RESERVE_09,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_10,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_10_flag,
   (void *) &prm_optimizer_reserve_10_default,
   (void *) &PRM_OPTIMIZER_RESERVE_10,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_11,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_11_flag,
   (void *) &prm_optimizer_reserve_11_default,
   (void *) &PRM_OPTIMIZER_RESERVE_11,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_12,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_12_flag,
   (void *) &prm_optimizer_reserve_12_default,
   (void *) &PRM_OPTIMIZER_RESERVE_12,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_13,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_13_flag,
   (void *) &prm_optimizer_reserve_13_default,
   (void *) &PRM_OPTIMIZER_RESERVE_13,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_14,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_14_flag,
   (void *) &prm_optimizer_reserve_14_default,
   (void *) &PRM_OPTIMIZER_RESERVE_14,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_15,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_15_flag,
   (void *) &prm_optimizer_reserve_15_default,
   (void *) &PRM_OPTIMIZER_RESERVE_15,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_16,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_16_flag,
   (void *) &prm_optimizer_reserve_16_default,
   (void *) &PRM_OPTIMIZER_RESERVE_16,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_17,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_17_flag,
   (void *) &prm_optimizer_reserve_17_default,
   (void *) &PRM_OPTIMIZER_RESERVE_17,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_18,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_18_flag,
   (void *) &prm_optimizer_reserve_18_default,
   (void *) &PRM_OPTIMIZER_RESERVE_18,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_19,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_19_flag,
   (void *) &prm_optimizer_reserve_19_default,
   (void *) &PRM_OPTIMIZER_RESERVE_19,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OPTIMIZER_RESERVE_20,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_optimizer_reserve_20_flag,
   (void *) &prm_optimizer_reserve_20_default,
   (void *) &PRM_OPTIMIZER_RESERVE_20,
   (void *) NULL, (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_REPL_ENABLE_SERVER_SIDE_UPDATE,
   (PRM_FOR_HA | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_ha_repl_enable_server_side_update_flag,
   (void *) &prm_ha_repl_enable_server_side_update_default,
   (void *) &PRM_HA_REPL_ENABLE_SERVER_SIDE_UPDATE,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_PB_LRU_HOT_RATIO,
   (PRM_FOR_SERVER | PRM_RELOADABLE),
   PRM_FLOAT,
   (void *) &prm_pb_lru_hot_ratio_flag,
   (void *) &prm_pb_lru_hot_ratio_default,
   (void *) &PRM_PB_LRU_HOT_RATIO,
   (void *) &prm_pb_lru_hot_ratio_upper,
   (void *) &prm_pb_lru_hot_ratio_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PREFETCHLOGDB_ENABLE,
   (PRM_FOR_HA | PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   (void *) &prm_ha_prefetchlogdb_enable_flag,
   (void *) &prm_ha_prefetchlogdb_enable_default,
   (void *) &PRM_HA_PREFETCHLOGDB_ENABLE,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PREFETCHLOGDB_MAX_THREAD_COUNT,
   (PRM_FOR_HA | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_ha_prefetchlogdb_max_thread_count_flag,
   (void *) &prm_ha_prefetchlogdb_max_thread_count_default,
   (void *) &PRM_HA_PREFETCHLOGDB_MAX_THREAD_COUNT,
   (void *) &prm_ha_prefetchlogdb_max_thread_count_upper,
   (void *) &prm_ha_prefetchlogdb_max_thread_count_lower,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PREFETCHLOGDB_PAGE_DISTANCE,
   (PRM_FOR_HA | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_ha_prefetchlogdb_page_distance_flag,
   (void *) &prm_ha_prefetchlogdb_page_distance_default,
   (void *) &PRM_HA_PREFETCHLOGDB_PAGE_DISTANCE,
   (void *) &prm_ha_prefetchlogdb_page_distance_upper,
   (void *) &prm_ha_prefetchlogdb_page_distance_lower,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_HA_PREFETCHLOGDB_MAX_PAGE_COUNT,
   (PRM_FOR_HA | PRM_FOR_CLIENT),
   PRM_INTEGER,
   (void *) &prm_ha_prefetchlogdb_max_page_count_flag,
   (void *) &prm_ha_prefetchlogdb_max_page_count_default,
   (void *) &PRM_HA_PREFETCHLOGDB_MAX_PAGE_COUNT,
   (void *) &prm_ha_prefetchlogdb_max_page_count_upper,
   (void *) &prm_ha_prefetchlogdb_max_page_count_lower,
   (void *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_MVCC_ENABLED,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_mvcc_enabled_flag,
   (void *) &prm_mvcc_enabled_default,
   (void *) &PRM_MVCC_ENABLED,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_VACUUM_MASTER_WAKEUP_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_vacuum_master_wakeup_interval_flag,
   (void *) &prm_vacuum_master_wakeup_interval_default,
   (void *) &PRM_VACUUM_MASTER_WAKEUP_INTERVAL,
   (void *) NULL,
   (void *) &prm_vacuum_master_wakeup_interval_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_VACUUM_DATA_PAGES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_vacuum_data_pages_flag,
   (void *) &prm_vacuum_data_pages_default,
   (void *) &PRM_VACUUM_DATA_PAGES,
   (void *) &prm_vacuum_data_pages_upper,
   (void *) &prm_vacuum_data_pages_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_VACUUM_LOG_BLOCK_PAGES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_vacuum_log_block_pages_flag,
   (void *) &prm_vacuum_log_block_pages_default,
   (void *) &PRM_VACUUM_LOG_BLOCK_PAGES,
   (void *) &prm_vacuum_log_block_pages_upper,
   (void *) &prm_vacuum_log_block_pages_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_VACUUM_WORKER_COUNT,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_vacuum_worker_count_flag,
   (void *) &prm_vacuum_worker_count_default,
   (void *) &PRM_VACUUM_WORKER_COUNT,
   (void *) &prm_vacuum_worker_count_upper,
   (void *) &prm_vacuum_worker_count_lower,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_ER_LOG_VACUUM,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_er_log_vacuum_flag,
   (void *) &prm_er_log_vacuum_default,
   (void *) &PRM_ER_LOG_VACUUM,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_DISABLE_VACUUM,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_disable_vacuum_flag,
   (void *) &prm_disable_vacuum_default,
   (void *) &PRM_DISABLE_VACUUM,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_LOG_BTREE_OPS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_log_btree_ops_flag,
   (void *) &prm_log_btree_ops_default,
   (void *) &PRM_LOG_BTREE_OPS,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_NAME_OBJECT_PRINT_FORMAT_OID,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_object_print_format_oid_flag,
   (void *) &prm_object_print_format_oid_default,
   (void *) &PRM_OBJECT_PRINT_FORMAT_OID,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL}
};

#define NUM_PRM ((int)(sizeof(prm_Def)/sizeof(prm_Def[0])))
#define PARAM_MSG_FMT(msgid) msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, (msgid))

#define GET_PRM(id) (&prm_Def[(id)])
#define GET_PRM_STATIC_FLAG(id) ((GET_PRM (id))->static_flag)
#define GET_PRM_DYNAMIC_FLAG(id) ((GET_PRM (id))->dynamic_flag)
#define GET_PRM_DATATYPE(id) ((GET_PRM (id))->datatype)

#if defined (CS_MODE)
#define PRM_PRINT_QRY_STRING(id) (PRM_IS_DIFFERENT (*(GET_PRM_DYNAMIC_FLAG (id))) \
			&& PRM_IS_FOR_QRY_STRING (GET_PRM_STATIC_FLAG (id)))
#else
#define PRM_PRINT_QRY_STRING(id) (PRM_IS_FOR_QRY_STRING (GET_PRM_STATIC_FLAG (id)))
#endif

#define PRM_SERVER_SESSION(id) (PRM_IS_FOR_SESSION (GET_PRM_STATIC_FLAG (id)) \
			&& PRM_IS_FOR_SERVER (GET_PRM_STATIC_FLAG (id)) \
			&& !PRM_CLIENT_SESSION_ONLY (GET_PRM_STATIC_FLAG (id)))

static int num_session_parameters = 0;
#define NUM_SESSION_PRM num_session_parameters

#if defined (CS_MODE)
/*
 * Session parameters should be cached with the default values or the values
 * loaded from cubrid.conf file. When a new client connects to CAS, it should
 * reload these paremeters (because some may be changed by previous clients)
 */
SESSION_PARAM *cached_session_parameters = NULL;
#endif /* CS_MODE */

/*
 * Keyword searches do a intl_mbs_ncasecmp(), using the LENGTH OF THE TABLE KEY
 * as the limit, so make sure that overlapping keywords are ordered
 * correctly.  For example, make sure that "yes" precedes "y".
 */

typedef struct keyval KEYVAL;
struct keyval
{
  const char *key;
  int val;
};

static KEYVAL boolean_words[] = {
  {"yes", 1},
  {"y", 1},
  {"1", 1},
  {"true", 1},
  {"on", 1},
  {"no", 0},
  {"n", 0},
  {"0", 0},
  {"false", 0},
  {"off", 0}
};

static KEYVAL er_log_level_words[] = {
  {"fatal", ER_FATAL_ERROR_SEVERITY},
  {"error", ER_ERROR_SEVERITY},
  {"syntax", ER_SYNTAX_ERROR_SEVERITY},
  {"warning", ER_WARNING_SEVERITY},
  {"notification", ER_NOTIFICATION_SEVERITY}
};

static KEYVAL isolation_level_words[] = {
  {"tran_serializable", TRAN_SERIALIZABLE},
  {"tran_no_phantom_read", TRAN_SERIALIZABLE},

  {"tran_rep_class_rep_instance", TRAN_REPEATABLE_READ},
  {"tran_rep_read", TRAN_REPEATABLE_READ},
  {"tran_rep_class_commit_instance", TRAN_READ_COMMITTED},
  {"tran_read_committed", TRAN_READ_COMMITTED},
  {"tran_cursor_stability", TRAN_READ_COMMITTED},

  {"serializable", TRAN_SERIALIZABLE},
  {"no_phantom_read", TRAN_SERIALIZABLE},

  {"rep_class_rep_instance", TRAN_REPEATABLE_READ},
  {"rep_read", TRAN_REPEATABLE_READ},
  {"rep_class_commit_instance", TRAN_READ_COMMITTED},
  {"read_committed", TRAN_READ_COMMITTED},
  {"cursor_stability", TRAN_READ_COMMITTED},
};

static KEYVAL pgbuf_debug_page_validation_level_words[] = {
  {"fetch", PGBUF_DEBUG_PAGE_VALIDATION_FETCH},
  {"free", PGBUF_DEBUG_PAGE_VALIDATION_FREE},
  {"all", PGBUF_DEBUG_PAGE_VALIDATION_ALL}
};

static KEYVAL null_words[] = {
  {"null", 0},
  {"0", 0}
};

static KEYVAL ha_mode_words[] = {
  {HA_MODE_OFF_STR, HA_MODE_OFF},
  {"no", HA_MODE_OFF},
  {"n", HA_MODE_OFF},
  {"0", HA_MODE_OFF},
  {"false", HA_MODE_OFF},
  {"off", HA_MODE_OFF},
  {"yes", HA_MODE_FAIL_BACK},
  {"y", HA_MODE_FAIL_BACK},
  {"1", HA_MODE_FAIL_BACK},
  {"true", HA_MODE_FAIL_BACK},
  {"on", HA_MODE_FAIL_BACK},
  /*{HA_MODE_FAIL_OVER_STR, HA_MODE_FAIL_OVER}, *//* unused */
  {HA_MODE_FAIL_BACK_STR, HA_MODE_FAIL_BACK},
  /*{HA_MODE_LAZY_BACK_STR, HA_MODE_LAZY_BACK}, *//* not implemented yet */
  {HA_MODE_ROLE_CHANGE_STR, HA_MODE_ROLE_CHANGE},
  {"r", HA_MODE_REPLICA},
  {"repl", HA_MODE_REPLICA},
  {"replica", HA_MODE_REPLICA},
  {"2", HA_MODE_REPLICA}
};

static KEYVAL ha_server_state_words[] = {
  {HA_SERVER_STATE_IDLE_STR, HA_SERVER_STATE_IDLE},
  {HA_SERVER_STATE_ACTIVE_STR, HA_SERVER_STATE_ACTIVE},
  {HA_SERVER_STATE_TO_BE_ACTIVE_STR, HA_SERVER_STATE_TO_BE_ACTIVE},
  {HA_SERVER_STATE_STANDBY_STR, HA_SERVER_STATE_STANDBY},
  {HA_SERVER_STATE_TO_BE_STANDBY_STR, HA_SERVER_STATE_TO_BE_STANDBY},
  {HA_SERVER_STATE_MAINTENANCE_STR, HA_SERVER_STATE_MAINTENANCE},
  {HA_SERVER_STATE_DEAD_STR, HA_SERVER_STATE_DEAD}
};

static KEYVAL ha_log_applier_state_words[] = {
  {HA_LOG_APPLIER_STATE_UNREGISTERED_STR, HA_LOG_APPLIER_STATE_UNREGISTERED},
  {HA_LOG_APPLIER_STATE_RECOVERING_STR, HA_LOG_APPLIER_STATE_RECOVERING},
  {HA_LOG_APPLIER_STATE_WORKING_STR, HA_LOG_APPLIER_STATE_WORKING},
  {HA_LOG_APPLIER_STATE_DONE_STR, HA_LOG_APPLIER_STATE_DONE},
  {HA_LOG_APPLIER_STATE_ERROR_STR, HA_LOG_APPLIER_STATE_ERROR}
};

static KEYVAL compat_words[] = {
  {"cubrid", COMPAT_CUBRID},
  {"default", COMPAT_CUBRID},
  {"mysql", COMPAT_MYSQL},
  {"oracle", COMPAT_ORACLE}
};

static KEYVAL check_peer_alive_words[] = {
  {"none", CSS_CHECK_PEER_ALIVE_NONE},
  {"server_only", CSS_CHECK_PEER_ALIVE_SERVER_ONLY},
  {"client_only", CSS_CHECK_PEER_ALIVE_CLIENT_ONLY},
  {"both", CSS_CHECK_PEER_ALIVE_BOTH},
};

static KEYVAL query_trace_format_words[] = {
  {"text", QUERY_TRACE_TEXT},
  {"json", QUERY_TRACE_JSON},
};

static const char *compat_mode_values_PRM_ANSI_QUOTES[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID     */
  "no",				/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE     */
  PRM_NAME_ANSI_QUOTES
};

static const char
  *compat_mode_values_PRM_ORACLE_STYLE_EMPTY_STRING[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID     */
  NULL,				/* COMPAT_MYSQL      */
  "yes",			/* COMPAT_ORACLE     */
  PRM_NAME_ORACLE_STYLE_EMPTY_STRING
};

static const char
  *compat_mode_values_PRM_ORACLE_STYLE_OUTERJOIN[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID     */
  NULL,				/* COMPAT_MYSQL      */
  "yes",			/* COMPAT_ORACLE     */
  PRM_NAME_ORACLE_STYLE_OUTERJOIN
};

static const char *compat_mode_values_PRM_PIPES_AS_CONCAT[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID     */
  "no",				/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE     */
  PRM_NAME_PIPES_AS_CONCAT
};

/* Oracle's trigger correlation names are not yet supported. */
static const char
  *compat_mode_values_PRM_MYSQL_TRIGGER_CORRELATION_NAMES[COMPAT_ORACLE + 2] =
{
  NULL,				/* COMPAT_CUBRID     */
  "yes",			/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE     */
  PRM_NAME_MYSQL_TRIGGER_CORRELATION_NAMES
};

static const char
  *compat_mode_values_PRM_REQUIRE_LIKE_ESCAPE_CHARACTER[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID     */
  "yes",			/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE     */
  PRM_NAME_REQUIRE_LIKE_ESCAPE_CHARACTER
};

static const char
  *compat_mode_values_PRM_NO_BACKSLASH_ESCAPES[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID     */
  "no",				/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE     */
  PRM_NAME_NO_BACKSLASH_ESCAPES
};

static const char
  *compat_mode_values_PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT[COMPAT_ORACLE + 2] =
{
  NULL,				/* COMPAT_CUBRID     */
  "yes",			/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE: leave it in cubrid
				 * mode  for now     */
  PRM_NAME_ADD_COLUMN_UPDATE_HARD_DEFAULT
};

static const char
  *compat_mode_values_PRM_RETURN_NULL_ON_FUNCTION_ERRORS[COMPAT_ORACLE + 2] =
{
  NULL,				/* COMPAT_CUBRID     */
  "yes",			/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE     */
  PRM_NAME_RETURN_NULL_ON_FUNCTION_ERRORS
};

static const char *compat_mode_values_PRM_PLUS_AS_CONCAT[COMPAT_ORACLE + 2] = {
  "yes",			/* COMPAT_CUBRID     */
  "no",				/* COMPAT_MYSQL      */
  NULL,				/* COMPAT_ORACLE     */
  PRM_NAME_PLUS_AS_CONCAT
};

static const char **compat_mode_values[] = {
  compat_mode_values_PRM_ANSI_QUOTES,
  compat_mode_values_PRM_ORACLE_STYLE_EMPTY_STRING,
  compat_mode_values_PRM_ORACLE_STYLE_OUTERJOIN,
  compat_mode_values_PRM_PIPES_AS_CONCAT,
  compat_mode_values_PRM_MYSQL_TRIGGER_CORRELATION_NAMES,
  compat_mode_values_PRM_REQUIRE_LIKE_ESCAPE_CHARACTER,
  compat_mode_values_PRM_NO_BACKSLASH_ESCAPES,
  compat_mode_values_PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT,
  compat_mode_values_PRM_RETURN_NULL_ON_FUNCTION_ERRORS,
  compat_mode_values_PRM_PLUS_AS_CONCAT
};

static const int call_stack_dump_error_codes[] = {
  ER_GENERIC_ERROR,
  ER_IO_FORMAT_BAD_NPAGES,
  ER_IO_READ,
  ER_IO_WRITE,
  ER_PB_BAD_PAGEID,
  ER_PB_UNFIXED_PAGEPTR,
  ER_DISK_UNKNOWN_SECTOR,
  ER_DISK_UNKNOWN_PAGE,
  ER_SP_BAD_INSERTION_SLOT,
  ER_SP_UNKNOWN_SLOTID,
  ER_HEAP_UNKNOWN_OBJECT,
  ER_HEAP_BAD_RELOCATION_RECORD,
  ER_HEAP_BAD_OBJECT_TYPE,
  ER_HEAP_OVFADDRESS_CORRUPTED,
  ER_LK_PAGE_TIMEOUT,
  ER_LOG_READ,
  ER_LOG_WRITE,
  ER_LOG_PAGE_CORRUPTED,
  ER_LOG_REDO_INTERFACE,
  ER_LOG_MAYNEED_MEDIA_RECOVERY,
  ER_LOG_NOTIN_ARCHIVE,
  ER_TF_BUFFER_UNDERFLOW,
  ER_TF_BUFFER_OVERFLOW,
  ER_BTREE_UNKNOWN_KEY,
  ER_CT_UNKNOWN_CLASSID,
  ER_CT_INVALID_CLASSID,
  ER_CT_UNKNOWN_REPRID,
  ER_CT_INVALID_REPRID,
  ER_FILE_ALLOC_NOPAGES,
  ER_FILE_TABLE_CORRUPTED,
  ER_PAGE_LATCH_TIMEDOUT,
  ER_PAGE_LATCH_ABORTED,
  ER_PARTITION_WORK_FAILED,
  ER_PARTITION_NOT_EXIST,
  ER_FILE_TABLE_OVERFLOW,
  ER_HA_GENERIC_ERROR,
  ER_DESC_ISCAN_ABORTED,
  ER_SP_INVALID_HEADER,
  ER_LOG_CHECKPOINT_SKIP_INVALID_PAGE
};

typedef enum
{
  PRM_PRINT_NONE = 0,
  PRM_PRINT_NAME,
  PRM_PRINT_ID
} PRM_PRINT_MODE;

typedef enum
{
  PRM_PRINT_CURR_VAL = 0,
  PRM_PRINT_DEFAULT_VAL
} PRM_PRINT_VALUE_MODE;

static void prm_the_file_has_been_loaded (const char *path);
static int prm_print_value (const SYSPRM_PARAM * prm, char *buf, size_t len);
static int prm_print (const SYSPRM_PARAM * prm, char *buf, size_t len,
		      PRM_PRINT_MODE print_mode,
		      PRM_PRINT_VALUE_MODE print_value_mode);
static int sysprm_load_and_init_internal (const char *db_name,
					  const char *conf_file, bool reload,
					  bool check_intl_param);
static void prm_check_environment (void);
static int prm_check_parameters (void);
static int prm_load_by_section (INI_TABLE * ini, const char *section,
				bool ignore_section, bool reload,
				const char *file, bool ha,
				bool check_intl_param);
static int prm_read_and_parse_ini_file (const char *prm_file_name,
					const char *db_name,
					const bool reload, const bool ha,
					const bool check_intl_param);
static void prm_report_bad_entry (const char *key, int line, int err,
				  const char *where);
static int prm_check_range (SYSPRM_PARAM * prm, void *value);
static int prm_set (SYSPRM_PARAM * prm, const char *value, bool set_flag);
static int prm_set_force (SYSPRM_PARAM * prm, const char *value);
static int prm_set_default (SYSPRM_PARAM * prm);
static SYSPRM_PARAM *prm_find (const char *pname, const char *section);
static const KEYVAL *prm_keyword (int val, const char *name,
				  const KEYVAL * tbl, int dim);
static void prm_tune_parameters (void);
static int prm_compound_has_changed (SYSPRM_PARAM * prm, bool set_flag);
static void prm_set_compound (SYSPRM_PARAM * param,
			      const char **compound_param_values[],
			      const int values_count, bool set_flag);
static int prm_get_next_param_value (char **data, char **prm, char **value);
static PARAM_ID sysprm_get_id (const SYSPRM_PARAM * prm);
static int sysprm_compare_values (void *first_value, void *second_value,
				  unsigned int val_type);
static void sysprm_set_sysprm_value_from_parameter (SYSPRM_VALUE * prm_value,
						    SYSPRM_PARAM * prm);
static SESSION_PARAM *sysprm_alloc_session_parameters (void);
static SYSPRM_ERR sysprm_generate_new_value (SYSPRM_PARAM * prm,
					     const char *value, bool check,
					     SYSPRM_VALUE * new_value);
static int sysprm_set_value (SYSPRM_PARAM * prm, SYSPRM_VALUE value,
			     bool set_flag, bool duplicate);
static void sysprm_set_system_parameter_value (SYSPRM_PARAM * prm,
					       SYSPRM_VALUE value);
static int sysprm_print_sysprm_value (PARAM_ID prm_id, SYSPRM_VALUE value,
				      char *buf, size_t len,
				      PRM_PRINT_MODE print_mode);

static void sysprm_update_flag_different (SYSPRM_PARAM * prm);
static void sysprm_update_flag_allocated (SYSPRM_PARAM * prm);
static void sysprm_update_session_prm_flag_allocated (SESSION_PARAM * prm);

static void sysprm_clear_sysprm_value (SYSPRM_VALUE * value,
				       SYSPRM_DATATYPE datatype);
static char *sysprm_pack_sysprm_value (char *ptr, SYSPRM_VALUE value,
				       SYSPRM_DATATYPE datatype);
static int sysprm_packed_sysprm_value_length (SYSPRM_VALUE value,
					      SYSPRM_DATATYPE datatype,
					      int offset);
static char *sysprm_unpack_sysprm_value (char *ptr, SYSPRM_VALUE * value,
					 SYSPRM_DATATYPE datatype);

#if defined (SERVER_MODE)
static SYSPRM_ERR sysprm_set_session_parameter_value (SESSION_PARAM *
						      session_parameter,
						      int id,
						      SYSPRM_VALUE value);
static SYSPRM_ERR sysprm_set_session_parameter_default (SESSION_PARAM *
							session_parameter,
							PARAM_ID prm_id);
#endif /* SERVER_MODE */

#if defined (CS_MODE)
static void sysprm_update_cached_session_param_val (const PARAM_ID prm_id);
#endif

/* conf files that have been loaded */
#define MAX_NUM_OF_PRM_FILES_LOADED	10
static struct
{
  char *conf_path;
  char *db_name;
} prm_Files_loaded[MAX_NUM_OF_PRM_FILES_LOADED];

/*
 * prm_file_has_been_loaded - Record the file path that has been loaded
 *   return: none
 *   conf_path(in): path of the conf file to be recorded
 *   db_name(in): db name to be recorded
 */
static void
prm_file_has_been_loaded (const char *conf_path, const char *db_name)
{
  int i;
  assert (conf_path != NULL);

  for (i = 0; i < MAX_NUM_OF_PRM_FILES_LOADED; i++)
    {
      if (prm_Files_loaded[i].conf_path == NULL)
	{
	  prm_Files_loaded[i].conf_path = strdup (conf_path);
	  prm_Files_loaded[i].db_name = db_name ? strdup (db_name) : NULL;
	  return;
	}
    }
}


/*
 * sysprm_dump_parameters - Print out current system parameters
 *   return: none
 *   fp(in):
 */
void
sysprm_dump_parameters (FILE * fp)
{
  char buf[LINE_MAX];
  int i;
  const SYSPRM_PARAM *prm;

  fprintf (fp, "#\n# cubrid.conf\n#\n\n");
  fprintf (fp,
	   "# system parameters were loaded from the files ([@section])\n");
  for (i = 0; i < MAX_NUM_OF_PRM_FILES_LOADED; i++)
    {
      if (prm_Files_loaded[i].conf_path != NULL)
	{
	  fprintf (fp, "# %s", prm_Files_loaded[i].conf_path);
	  if (prm_Files_loaded[i].db_name)
	    {
	      fprintf (fp, " [@%s]\n", prm_Files_loaded[i].db_name);
	    }
	  else
	    {
	      fprintf (fp, "\n");
	    }
	}
    }

  fprintf (fp, "\n# system parameters\n");
  for (i = 0; i < NUM_PRM; i++)
    {
      prm = &prm_Def[i];
      if (PRM_IS_HIDDEN (prm->static_flag)
	  || PRM_IS_OBSOLETED (prm->static_flag))
	{
	  continue;
	}
      prm_print (prm, buf, LINE_MAX, PRM_PRINT_NAME, PRM_PRINT_CURR_VAL);
      fprintf (fp, "%s\n", buf);
    }

  return;
}

/*
 * sysprm_set_er_log_file -
 *   return: void
 *   base_db_name(in): database name
 *
 */
void
sysprm_set_er_log_file (const char *db_name)
{
  char *s, *base_db_name;
  char local_db_name[DB_MAX_IDENTIFIER_LENGTH];
  time_t log_time;
  struct tm log_tm, *log_tm_p = &log_tm;
  char error_log_name[PATH_MAX];
  SYSPRM_PARAM *er_log_file;

  if (db_name == NULL)
    {
      return;
    }

  er_log_file = prm_find (PRM_NAME_ER_LOG_FILE, NULL);
  if (er_log_file == NULL || PRM_IS_SET (*er_log_file->dynamic_flag))
    {
      return;
    }

  strncpy (local_db_name, db_name, DB_MAX_IDENTIFIER_LENGTH);
  s = strchr (local_db_name, '@');
  if (s)
    {
      *s = '\0';
    }
  base_db_name = basename ((char *) local_db_name);
  if (base_db_name == NULL)
    {
      return;
    }

  log_time = time (NULL);
  log_tm_p = localtime_r (&log_time, &log_tm);
  if (log_tm_p != NULL)
    {
      snprintf (error_log_name, PATH_MAX - 1,
		"%s%c%s_%04d%02d%02d_%02d%02d.err", ER_LOG_FILE_DIR,
		PATH_SEPARATOR, base_db_name, log_tm_p->tm_year + 1900,
		log_tm_p->tm_mon + 1, log_tm_p->tm_mday, log_tm_p->tm_hour,
		log_tm_p->tm_min);
      prm_set (er_log_file, error_log_name, true);
    }
}

/*
 * sysprm_load_and_init_internal - Read system parameters from the init files
 *   return: NO_ERROR or ER_FAILED
 *   db_name(in): database name
 *   conf_file(in): config file
 *   reload(in):
 *   check_intl_param(in):
 *
 * Note: Parameters would be tuned and forced according to the internal rules.
 */
static int
sysprm_load_and_init_internal (const char *db_name, const char *conf_file,
			       bool reload, bool check_intl_param)
{
  char *base_db_name = NULL;
  char file_being_dealt_with[PATH_MAX];
  char local_db_name[DB_MAX_IDENTIFIER_LENGTH];
  unsigned int i;
  struct stat stat_buf;
  int r = NO_ERROR;
  char *s;
#if defined (CS_MODE)
  SESSION_PARAM *sprm = NULL;
  int num_session_prms;
#endif

  if (reload)
    {
      for (i = 0; i < NUM_PRM; i++)
	{
	  if (PRM_IS_RELOADABLE (prm_Def[i].static_flag))
	    {
	      if (prm_set_default (&prm_Def[i]) != PRM_ERR_NO_ERROR)
		{
		  prm_Def[i].value = (void *) NULL;
		}
	    }
	}
    }

  if (db_name == NULL)
    {
      /* initialize message catalog at here because there could be a code path
       * that did not call msgcat_init() before */
      if (msgcat_init () != NO_ERROR)
	{
	  return ER_FAILED;
	}
      base_db_name = NULL;
    }
  else
    {
      strncpy (local_db_name, db_name, DB_MAX_IDENTIFIER_LENGTH);
      s = strchr (local_db_name, '@');
      if (s)
	{
	  *s = '\0';
	}
      base_db_name = basename ((char *) local_db_name);
    }

#if !defined (CS_MODE)
  if (base_db_name != NULL && reload == false)
    {
      sysprm_set_er_log_file (base_db_name);
    }
#endif /* !CS_MODE */

  /*
   * Read installation configuration file - $CUBRID/conf/cubrid.conf
   * or use conf_file if exist
   */

  if (conf_file == NULL)
    {
      /* use environment variable's value if exist */
      conf_file = envvar_get ("CONF_FILE");
      if (conf_file != NULL && *conf_file == '\0')
	{
	  conf_file = NULL;
	}
    }

  if (conf_file != NULL)
    {
      /* use user specified config path and file */
      strncpy (file_being_dealt_with, conf_file,
	       sizeof (file_being_dealt_with) - 1);
    }
  else
    {
      envvar_confdir_file (file_being_dealt_with, PATH_MAX,
			   sysprm_conf_file_name);
    }

  if (stat (file_being_dealt_with, &stat_buf) != 0)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARAMETERS,
				       PRM_ERR_CANT_ACCESS),
	       file_being_dealt_with, strerror (errno));
    }
  else
    {
      r = prm_read_and_parse_ini_file (file_being_dealt_with,
				       base_db_name, reload, HA_IGNORE,
				       check_intl_param);
    }

  if (r != NO_ERROR)
    {
      return r;
    }

  if (PRM_HA_MODE != HA_MODE_OFF)
    {
      /* use environment variable's value if exist */
      conf_file = envvar_get ("HA_CONF_FILE");
      if (conf_file != NULL && conf_file[0] != '\0')
	{
	  strncpy (file_being_dealt_with, conf_file, PATH_MAX);
	}
      else
	{
	  envvar_confdir_file (file_being_dealt_with, PATH_MAX,
			       sysprm_ha_conf_file_name);
	}
      if (stat (file_being_dealt_with, &stat_buf) == 0)
	{
	  r = prm_read_and_parse_ini_file (file_being_dealt_with, NULL,
					   reload, HA_READ, check_intl_param);
	}
    }

  if (r != NO_ERROR)
    {
      return r;
    }

  /*
   * If a parameter is not given, set it by default
   */
  for (i = 0; i < NUM_PRM; i++)
    {
      if (!PRM_IS_SET (*prm_Def[i].dynamic_flag)
	  && !PRM_IS_OBSOLETED (prm_Def[i].static_flag))
	{
	  if (prm_set_default (&prm_Def[i]) != PRM_ERR_NO_ERROR)
	    {
	      fprintf (stderr,
		       msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARAMETERS,
				       PRM_ERR_NO_VALUE), prm_Def[i].name);
	      assert (0);
	      return ER_FAILED;
	    }
	}
    }

  /*
   * Perform system parameter check and tuning.
   */
  prm_check_environment ();
  prm_tune_parameters ();
#if 0
  if (prm_check_parameters () != NO_ERROR)
    {
      return ER_FAILED;
    }
#endif

  /*
   * Perform forced system parameter setting.
   */
  for (i = 0; i < DIM (prm_Def); i++)
    {
      if (prm_Def[i].force_value)
	{
	  prm_set (&prm_Def[i], prm_Def[i].force_value, false);
	}
    }

#if 0
  if (envvar_get ("PARAM_DUMP"))
    {
      sysprm_dump_parameters (stdout);
    }
#endif

  intl_Mbs_support = prm_get_bool_value (PRM_ID_INTL_MBS_SUPPORT);
#if !defined (SERVER_MODE)
  intl_String_validation =
    prm_get_bool_value (PRM_ID_INTL_CHECK_INPUT_STRING);
#endif

  /* count the number of session parameters */
  num_session_parameters = 0;
  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_IS_FOR_SESSION (prm_Def[i].static_flag))
	{
	  num_session_parameters++;
	}
    }
#if defined (CS_MODE)
  /* cache session parameters */
  if (cached_session_parameters != NULL)
    {
      /* free previous cache */
      sysprm_free_session_parameters (&cached_session_parameters);
    }
  cached_session_parameters = sysprm_alloc_session_parameters ();
  num_session_prms = 0;
  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_IS_FOR_SESSION (prm_Def[i].static_flag))
	{
	  sprm = &cached_session_parameters[num_session_prms++];
	  sprm->prm_id = i;
	  sprm->flag = *prm_Def[i].dynamic_flag;
	  sprm->datatype = prm_Def[i].datatype;
	  sysprm_set_sysprm_value_from_parameter (&sprm->value, GET_PRM (i));
	  sysprm_update_session_prm_flag_allocated (sprm);
	}
    }
#endif /* CS_MODE */

#if !defined(NDEBUG)
  /* verify flags are not incorrect or confusing */
  for (i = 0; i < NUM_PRM; i++)
    {
      int flag = prm_Def[i].static_flag;
      if (PRM_IS_FOR_SESSION (flag)
	  && (!PRM_IS_FOR_CLIENT (flag) || !PRM_USER_CAN_CHANGE (flag)))
	{
	  /* session parameters can only be parameters for client that are
	   * changeable on-line
	   */
	  assert (0);
	}
      if (PRM_IS_FOR_SESSION (flag) && PRM_IS_HIDDEN (flag))
	{
	  /* hidden parameters are not allowed to use PRM_FOR_SESSION flag */
	  assert (0);
	}
      if (PRM_CLIENT_SESSION_ONLY (flag)
	  && (!PRM_IS_FOR_SERVER (flag) || !PRM_IS_FOR_SESSION (flag)))
	{
	  /* client session only makes sense if the parameter is for session
	   * and for server
	   */
	  assert (0);
	}
      if (PRM_USER_CAN_CHANGE (flag) && PRM_TEST_CHANGE_ONLY (flag))
	{
	  /* do not set both parameters:
	   * USER_CHANGE: the user can change parameter value on-line
	   * TEST_CHANGE: for QA only
	   */
	  assert (0);
	}
      if (PRM_IS_GET_SERVER (flag)
	  && (PRM_IS_FOR_SESSION (flag) || PRM_IS_HIDDEN (flag)))
	{
	  /* session and hidden parameters are not allowed 
	   * to use PRM_GET_SERVER flag 
	   */
	  assert (0);
	}
      if (PRM_IS_GET_SERVER (flag)
	  && (!PRM_IS_FOR_CLIENT (flag) || !PRM_IS_FOR_SERVER (flag)
	      || !PRM_USER_CAN_CHANGE (flag)))
	{
	  /* Note that PRM_GET_SERVER flag only can be set if the 
	   * parameter has PRM_FOR_CLIENT, PRM_FOR_SERVER, and 
	   * PRM_USER_CHANGE flags.                              
	   */
	  assert (0);
	}
    }
#endif

  return NO_ERROR;
}

/*
 * sysprm_load_and_init - Read system parameters from the init files
 *   return: NO_ERROR or ER_FAILED
 *   db_name(in): database name
 *   conf_file(in): config file
 *
 */
int
sysprm_load_and_init (const char *db_name, const char *conf_file)
{
  return sysprm_load_and_init_internal (db_name, conf_file, false, false);
}

/*
 * sysprm_load_and_init_client - Read system parameters from the init files
 *				 (client version)
 *   return: NO_ERROR or ER_FAILED
 *   db_name(in): database name
 *   conf_file(in): config file
 *
 */
int
sysprm_load_and_init_client (const char *db_name, const char *conf_file)
{
  return sysprm_load_and_init_internal (db_name, conf_file, false, true);
}

/*
 * sysprm_reload_and_init - Read system parameters from the init files
 *   return: NO_ERROR or ER_FAILED
 *   db_name(in): database name
 *   conf_file(in): config file
 *
 */
int
sysprm_reload_and_init (const char *db_name, const char *conf_file)
{
  return sysprm_load_and_init_internal (db_name, conf_file, true, false);
}

/*
 * prm_load_by_section - Set system parameters from a file
 *   return: void
 *   ini(in):
 *   section(in):
 *   ignore_section(in):
 *   reload(in):
 *   file(in):
 *   ha(in):
 *   check_intl_param(in):
 */
static int
prm_load_by_section (INI_TABLE * ini, const char *section,
		     bool ignore_section, bool reload, const char *file,
		     bool ha, bool check_intl_param)
{
  int i, error;
  int sec_len;
  const char *sec_p;
  const char *key, *value;
  SYSPRM_PARAM *prm;

  sec_p = (ignore_section) ? NULL : section;

  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] == NULL || ini->val[i] == NULL)
	{
	  continue;
	}

      key = ini->key[i];
      value = ini->val[i];

      if (ini_hassec (key))
	{
	  sec_len = ini_seccmp (key, section);
	  if (!sec_len)
	    {
	      continue;
	    }
	  sec_len++;
	}
      else
	{
	  if (ignore_section)
	    {
	      sec_len = 1;
	    }
	  else
	    {
	      continue;
	    }
	}

      prm = prm_find (key + sec_len, sec_p);
      if (prm == NULL)
	{
	  error = PRM_ERR_UNKNOWN_PARAM;
	  prm_report_bad_entry (key + sec_len, ini->lineno[i], error, file);
	  return error;
	}

      if (reload && !PRM_IS_RELOADABLE (prm->static_flag))
	{
	  continue;
	}
      if (ha == HA_READ && !PRM_IS_FOR_HA (prm->static_flag))
	{
	  continue;
	}

      if (PRM_IS_OBSOLETED (prm->static_flag))
	{
	  continue;
	}

      if (PRM_IS_DEPRECATED (prm->static_flag))
	{
	  prm_report_bad_entry (key + sec_len, ini->lineno[i],
				PRM_ERR_DEPRICATED, file);
	}

      if (check_intl_param)
	{
	  if (strcasecmp (PRM_NAME_INTL_DATE_LANG, prm->name) == 0
	      || strcasecmp (PRM_NAME_INTL_NUMBER_LANG, prm->name) == 0)
	    {
	      INTL_LANG dummy;
	      if (value == NULL
		  || lang_get_lang_id_from_name (value, &dummy) != 0)
		{
		  error = PRM_ERR_BAD_VALUE;
		  prm_report_bad_entry (key + sec_len, ini->lineno[i], error,
					file);
		  return error;
		}
	    }

	  if (strcasecmp (PRM_NAME_INTL_COLLATION, prm->name) == 0)
	    {
	      LANG_COLLATION *lc = NULL;
	      if (value != NULL)
		{
		  lc = lang_get_collation_by_name (value);
		}

	      if (lc == NULL)
		{
		  error = PRM_ERR_BAD_VALUE;
		  prm_report_bad_entry (key + sec_len, ini->lineno[i], error,
					file);
		  return error;
		}
	    }
	}

      if (strcasecmp (PRM_NAME_MVCC_ENABLED, prm->name) == 0)
	{
	  PRM_LOG_ISOLATION_LEVEL = PRM_MVCC_LOG_ISOLATION_LEVEL;

	  prm_log_isolation_level_default =
	    prm_mvcc_log_isolation_level_default;
	  prm_log_isolation_level_lower = prm_mvcc_log_isolation_level_lower;
	}

      error = prm_set (prm, value, true);
      if (error != NO_ERROR)
	{
	  prm_report_bad_entry (key + sec_len, ini->lineno[i], error, file);
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * prm_read_and_parse_ini_file - Set system parameters from a file
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   prm_file_name(in):
 *   db_name(in):
 *   reload(in):
 *   ha(in):
 *   check_intl_param(in):
 */
static int
prm_read_and_parse_ini_file (const char *prm_file_name, const char *db_name,
			     const bool reload, const bool ha,
			     const bool check_intl_param)
{
  INI_TABLE *ini;
  char sec_name[LINE_MAX];
  char host_name[MAXHOSTNAMELEN];
  char user_name[MAXHOSTNAMELEN];
  int error;

  ini = ini_parser_load (prm_file_name);
  if (ini == NULL)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
			       PRM_ERR_CANT_OPEN_INIT), prm_file_name,
	       strerror (errno));
      return PRM_ERR_FILE_ERR;
    }

  error =
    prm_load_by_section (ini, "common", true, reload, prm_file_name, ha,
			 check_intl_param);
  if (error == NO_ERROR && !ha && db_name != NULL && *db_name != '\0')
    {
      snprintf (sec_name, LINE_MAX, "@%s", db_name);
      error =
	prm_load_by_section (ini, sec_name, true, reload, prm_file_name, ha,
			     check_intl_param);
    }

#if defined (SA_MODE)
  if (error == NO_ERROR)
    {
      error =
	prm_load_by_section (ini, "standalone", true, reload, prm_file_name,
			     ha, check_intl_param);
    }
#endif /* SA_MODE */

  if (error == NO_ERROR && !ha)
    {
      error =
	prm_load_by_section (ini, "service", false, reload, prm_file_name,
			     ha, check_intl_param);
    }
  if (error == NO_ERROR && ha && PRM_HA_MODE != HA_MODE_OFF
      && GETHOSTNAME (host_name, MAXHOSTNAMELEN) == 0)
    {
      snprintf (sec_name, LINE_MAX, "%%%s|*", host_name);
      error =
	prm_load_by_section (ini, sec_name, true, reload, prm_file_name, ha,
			     check_intl_param);
      if (error == NO_ERROR && getlogin_r (user_name, MAXHOSTNAMELEN) == 0)
	{
	  snprintf (sec_name, LINE_MAX, "%%%s|%s", host_name, user_name);
	  error =
	    prm_load_by_section (ini, sec_name, true, reload, prm_file_name,
				 ha, check_intl_param);
	}
    }

  ini_parser_free (ini);

  prm_file_has_been_loaded (prm_file_name, db_name);

  return error;
}

/*
 * prm_size_to_io_pages -
 *   return: value
 */
static int
prm_size_to_io_pages (void *out_val, SYSPRM_DATATYPE out_type,
		      void *in_val, SYSPRM_DATATYPE in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_INTEGER && in_type == PRM_BIGINT)
    {
      int *page_value = (int *) out_val;
      UINT64 *size_value = (UINT64 *) in_val;
      UINT64 tmp_value;

      tmp_value = *size_value / IO_PAGESIZE;
      if (*size_value % IO_PAGESIZE > 0)
	{
	  tmp_value++;
	}

      if (tmp_value > INT_MAX)
	{
	  return PRM_ERR_BAD_RANGE;
	}
      else
	{
	  *page_value = (int) tmp_value;
	}
    }
  else if (out_type == PRM_FLOAT && in_type == PRM_BIGINT)
    {
      float *page_value = (float *) out_val;
      UINT64 *size_value = (UINT64 *) in_val;
      double tmp_value;

      tmp_value = (double) *size_value / IO_PAGESIZE;

      if (tmp_value > FLT_MAX)
	{
	  return PRM_ERR_BAD_RANGE;
	}
      else
	{
	  *page_value = ceilf ((float) tmp_value * 100) / 100;
	}
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_io_pages_to_size -
 *   return: value
 */
static int
prm_io_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type,
		      void *in_val, SYSPRM_DATATYPE in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_BIGINT && in_type == PRM_INTEGER)
    {
      UINT64 *size_value = (UINT64 *) out_val;
      UINT64 page_value = *(int *) in_val;

      *size_value = (UINT64) (page_value * IO_PAGESIZE);
    }
  else if (out_type == PRM_BIGINT && in_type == PRM_FLOAT)
    {
      UINT64 *size_value = (UINT64 *) out_val;
      float page_value = *(float *) in_val;

      *size_value = (UINT64) (page_value * IO_PAGESIZE);
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_size_to_log_pages -
 *   return: value
 */
static int
prm_size_to_log_pages (void *out_val, SYSPRM_DATATYPE out_type,
		       void *in_val, SYSPRM_DATATYPE in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_INTEGER && in_type == PRM_BIGINT)
    {
      int *page_value = (int *) out_val;
      UINT64 *size_value = (UINT64 *) in_val;
      UINT64 tmp_value;

      tmp_value = *size_value / LOG_PAGESIZE;
      if (*size_value % LOG_PAGESIZE)
	{
	  tmp_value++;
	}

      if (tmp_value > INT_MAX)
	{
	  return PRM_ERR_BAD_RANGE;
	}
      else
	{
	  *page_value = (int) tmp_value;
	}
    }
  else if (out_type == PRM_FLOAT && in_type == PRM_BIGINT)
    {
      float *page_value = (float *) out_val;
      UINT64 *size_value = (UINT64 *) in_val;
      double tmp_value;

      tmp_value = (double) *size_value / LOG_PAGESIZE;

      if (tmp_value > FLT_MAX)
	{
	  return PRM_ERR_BAD_RANGE;
	}
      else
	{
	  *page_value = ceilf ((float) tmp_value * 100) / 100;
	}
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_log_pages_to_size -
 *   return: value
 */
static int
prm_log_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type,
		       void *in_val, SYSPRM_DATATYPE in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_BIGINT && in_type == PRM_INTEGER)
    {
      UINT64 *size_value = (UINT64 *) out_val;
      UINT64 page_value = *(int *) in_val;

      *size_value = (UINT64) (page_value * LOG_PAGESIZE);
    }
  else if (out_type == PRM_BIGINT && in_type == PRM_FLOAT)
    {
      UINT64 *size_value = (UINT64 *) out_val;
      float page_value = *(float *) in_val;

      *size_value = (UINT64) (page_value * LOG_PAGESIZE);
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_msec_to_sec -
 *   return: value
 */
static int
prm_msec_to_sec (void *out_val, unsigned int out_type,
		 void *in_val, unsigned int in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_INTEGER && in_type == PRM_BIGINT)
    {
      int *sec_value = (int *) out_val;
      UINT64 *msec_value = (UINT64 *) in_val;
      UINT64 tmp_value;

      tmp_value = *msec_value / ONE_SEC;

      if (tmp_value > INT_MAX)
	{
	  return PRM_ERR_BAD_RANGE;
	}
      else
	{
	  *sec_value = (int) tmp_value;
	  if (*msec_value % ONE_SEC > 0)
	    {
	      *sec_value = *sec_value + 1;
	    }
	}
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_sec_to_msec -
 *   return: value
 */
static int
prm_sec_to_msec (void *out_val, unsigned int out_type,
		 void *in_val, unsigned int in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_BIGINT && in_type == PRM_INTEGER)
    {
      UINT64 *msec_value = (UINT64 *) out_val;
      int sec_value = *(int *) in_val;

      *msec_value = (UINT64) sec_value *ONE_SEC;
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_sec_to_min -
 *   return: value
 */
static int
prm_sec_to_min (void *out_val, unsigned int out_type,
		void *in_val, unsigned int in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_INTEGER && in_type == PRM_INTEGER)
    {
      int *min_value = (int *) out_val;
      int sec_value = *((int *) in_val);

      if (sec_value < 0)
	{
	  *min_value = sec_value;
	}
      else
	{
	  *min_value = sec_value / 60;
	  if (sec_value % 60 > 0)
	    {
	      *min_value = *min_value + 1;
	    }
	}
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_min_to_sec -
 *   return: value
 */
static int
prm_min_to_sec (void *out_val, unsigned int out_type,
		void *in_val, unsigned int in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_INTEGER && in_type == PRM_INTEGER)
    {
      int *sec_value = (int *) out_val;
      int *min_value = (int *) in_val;
      UINT64 tmp_value;

      if (*min_value < 0)
	{
	  *sec_value = *min_value;
	}
      else
	{
	  tmp_value = *min_value * 60;

	  if (tmp_value > INT_MAX)
	    {
	      return PRM_ERR_BAD_RANGE;
	    }
	  else
	    {
	      *sec_value = (int) tmp_value;
	    }
	}
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_equal_to_ori -
 *   return: value
 */
static int
prm_equal_to_ori (void *out_val, unsigned int out_type,
		  void *in_val, unsigned int in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_INTEGER && in_type == PRM_INTEGER)
    {
      int *in_value = (int *) in_val;
      int *out_value = (int *) out_val;

      *out_value = *in_value;
    }
  else if (out_type == PRM_FLOAT && in_type == PRM_FLOAT)
    {
      float *in_value = (float *) in_val;
      float *out_value = (float *) out_val;

      *out_value = *in_value;
    }
  else if (out_type == PRM_BIGINT && in_type == PRM_BIGINT)
    {
      UINT64 *in_value = (UINT64 *) in_val;
      UINT64 *out_value = (UINT64 *) out_val;

      *out_value = *in_value;
    }
  else if (out_type == PRM_INTEGER && in_type == PRM_BIGINT)
    {
      UINT64 *in_value = (UINT64 *) in_val;
      int *out_value = (int *) out_val;

      if (*in_value > INT_MAX)
	{
	  return PRM_ERR_BAD_RANGE;
	}

      *out_value = (int) *in_value;
    }
  else if (out_type == PRM_BIGINT && in_type == PRM_INTEGER)
    {
      int *in_value = (int *) in_val;
      UINT64 *out_value = (UINT64 *) out_val;

      *out_value = (UINT64) (*in_value);
    }
  else
    {
      assert_release (false);
      return PRM_ERR_BAD_VALUE;
    }

  return NO_ERROR;
}

/*
 * prm_check_parameters -
 *   return: int
 */
static int
prm_check_parameters (void)
{
  /*
   * ha_node_list and ha_db_list should be not null for ha_mode=yes|replica
   */
  if (PRM_HA_MODE != HA_MODE_OFF)
    {
      if (PRM_HA_NODE_LIST == NULL || PRM_HA_NODE_LIST[0] == 0
	  || PRM_HA_DB_LIST == NULL || PRM_HA_DB_LIST[0] == 0)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_PARAMETERS,
					   PRM_ERR_NO_VALUE),
		   PRM_NAME_HA_NODE_LIST);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * prm_check_environment -
 *   return: none
 */
static void
prm_check_environment (void)
{
  int i;
  char buf[PRM_DEFAULT_BUFFER_SIZE];

  for (i = 0; i < NUM_PRM; i++)
    {
      SYSPRM_PARAM *prm;
      const char *str;

      prm = &prm_Def[i];
      strncpy (buf, prm->name, sizeof (buf) - 1);
      buf[sizeof (buf) - 1] = '\0';
      ustr_upper (buf);

      str = envvar_get (buf);
      if (str && str[0])
	{
	  int error;
	  error = prm_set (prm, str, true);
	  if (error != 0)
	    {
	      prm_report_bad_entry (prm->name, -1, error, buf);
	    }
	}
    }
}

#if !defined (SERVER_MODE)
/*
 * sysprm_validate_change_parameters () - validate the parameter value changes
 *
 * return		 : SYSPRM_ERR
 * data (in)		 : string containing "parameter = value" assignments
 * check (in)		 : check if user can change parameter and if
 *			   parameter should also change on server. set to
 *			   false if assignments are supposed to be forced and
 *			   not checked.
 * assignments_ptr (out) : list of assignments.
 *
 * NOTE: Data string is parsed entirely and if all changes are valid a list
 *	 of SYSPRM_ASSIGN_VALUEs is generated. If any change is invalid an
 *	 error is returned and no list is generated.
 *	 If changes need to be done on server too PRM_ERR_NOT_FOR_CLIENT or
 *	 PRM_ERR_NOT_FOR_CLIENT_NO_AUTH is returned.
 */
SYSPRM_ERR
sysprm_validate_change_parameters (const char *data, bool check,
				   SYSPRM_ASSIGN_VALUE ** assignments_ptr)
{
  char buf[LINE_MAX], *p = NULL, *name = NULL, *value = NULL;
  SYSPRM_PARAM *prm = NULL;
  SYSPRM_ERR err = PRM_ERR_NO_ERROR;
  SYSPRM_ASSIGN_VALUE *assignments = NULL, *last_assign = NULL;
  SYSPRM_ERR change_error = PRM_ERR_NO_ERROR;

  assert (assignments_ptr != NULL);
  *assignments_ptr = NULL;

  if (!data || *data == '\0')
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (intl_mbs_ncpy (buf, data, sizeof (buf)) == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  p = buf;
  do
    {
      /* parse data */
      SYSPRM_ASSIGN_VALUE *assign = NULL;

      /* get parameter name and value */
      err = prm_get_next_param_value (&p, &name, &value);
      if (err != PRM_ERR_NO_ERROR || name == NULL || value == NULL)
	{
	  break;
	}

      prm = prm_find (name, NULL);
      if (prm == NULL)
	{
	  err = PRM_ERR_UNKNOWN_PARAM;
	  break;
	}

      if (!check
	  || PRM_USER_CAN_CHANGE (prm->static_flag)
	  || (PRM_TEST_CHANGE_ONLY (prm->static_flag) && PRM_TEST_MODE))
	{
	  /* We allow changing the parameter value. */
	}
      else
	{
	  err = PRM_ERR_CANNOT_CHANGE;
	  break;
	}

      if (check)
	{
	  if (strcmp (prm->name, PRM_NAME_INTL_NUMBER_LANG) == 0
	      || strcmp (prm->name, PRM_NAME_INTL_DATE_LANG) == 0)
	    {
	      INTL_LANG dummy;

	      if (lang_get_lang_id_from_name (value, &dummy) != 0)
		{
		  err = PRM_ERR_BAD_VALUE;
		  break;
		}
	    }
	  if (strcmp (prm->name, PRM_NAME_INTL_COLLATION) == 0)
	    {
	      LANG_COLLATION *lc = NULL;
	      if (value != NULL)
		{
		  lc = lang_get_collation_by_name (value);
		}

	      if (lc == NULL)
		{
		  err = PRM_ERR_BAD_VALUE;
		  break;
		}
	    }
	}

      /* create a SYSPRM_CHANGE_VAL object */
      assign = (SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
      if (assign == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (SYSPRM_ASSIGN_VALUE));
	  err = PRM_ERR_NO_MEM_FOR_PRM;
	  break;
	}
      err = sysprm_generate_new_value (prm, value, check, &assign->value);
      if (err != PRM_ERR_NO_ERROR)
	{
	  if (err == PRM_ERR_NOT_FOR_CLIENT
	      || err == PRM_ERR_NOT_FOR_CLIENT_NO_AUTH)
	    {
	      /* update change_error */
	      if (change_error != PRM_ERR_NOT_FOR_CLIENT
		  || err != PRM_ERR_NOT_FOR_CLIENT_NO_AUTH)
		{
		  /* do not replace change_error PRM_ERR_NOT_FOR_CLIENT with
		   * PRM_ERR_NOT_FOR_CLIENT_NO_AUTH
		   */
		  change_error = err;
		}
	      /* do not invalidate assignments */
	      err = PRM_ERR_NO_ERROR;
	    }
	  else
	    {
	      /* bad value */
	      free_and_init (assign);
	      break;
	    }
	}
      assign->prm_id = sysprm_get_id (prm);
      assign->next = NULL;

      /* append to assignments list */
      if (assignments != NULL)
	{
	  last_assign->next = assign;
	  last_assign = assign;
	}
      else
	{
	  assignments = last_assign = assign;
	}
    }
  while (p);

  if (err == PRM_ERR_NO_ERROR)
    {
      /* changes are valid, save assignments list */
      *assignments_ptr = assignments;

      /* return change_error in order to update values on server too */
      return change_error;
    }

  /* changes are not valid, clean up */
  sysprm_free_assign_values (&assignments);
  return err;
}

/*
 * sysprm_make_default_values () -
 *
 * return		 : SYSPRM_ERR
 * data (in)		 : string containing "parameter = value" assignments
 * default_val_buf(out)	 : string containing "parameter = def_value"
 *			   assignements created from data argument
 * buf_size(in)		 : size available in default_val_buf
 *
 */
SYSPRM_ERR
sysprm_make_default_values (const char *data, char *default_val_buf,
			    const int buf_size)
{
  char buf[LINE_MAX], *p = NULL, *out_p = NULL;
  char *name = NULL, *value = NULL;
  int remaining_size, n;
  SYSPRM_ERR err = PRM_ERR_NO_ERROR;
  SYSPRM_PARAM *prm = NULL;

  if (intl_mbs_ncpy (buf, data, sizeof (buf)) == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  p = buf;
  out_p = default_val_buf;
  remaining_size = buf_size - 1;
  do
    {
      /* parse data */
      SYSPRM_ASSIGN_VALUE *assign = NULL;

      /* get parameter name and value */
      err = prm_get_next_param_value (&p, &name, &value);
      if (err != PRM_ERR_NO_ERROR || name == NULL || value == NULL)
	{
	  break;
	}

      prm = prm_find (name, NULL);
      if (prm == NULL)
	{
	  err = PRM_ERR_UNKNOWN_PARAM;
	  break;
	}

      if (PRM_ID_INTL_COLLATION == sysprm_get_id (prm))
	{
	  n =
	    snprintf (out_p, remaining_size, "%s=%s", PRM_NAME_INTL_COLLATION,
		      lang_get_collation_name (LANG_GET_BINARY_COLLATION
					       (LANG_SYS_CODESET)));
	}
      else if (PRM_ID_INTL_DATE_LANG == sysprm_get_id (prm))
	{
	  n =
	    snprintf (out_p, remaining_size, "%s=%s", PRM_NAME_INTL_DATE_LANG,
		      lang_get_Lang_name ());
	}
      else if (PRM_ID_INTL_NUMBER_LANG == sysprm_get_id (prm))
	{
	  n =
	    snprintf (out_p, remaining_size, "%s=%s",
		      PRM_NAME_INTL_NUMBER_LANG, lang_get_Lang_name ());
	}
      else
	{
	  n =
	    prm_print (prm, out_p, remaining_size, PRM_PRINT_NAME,
		       PRM_PRINT_DEFAULT_VAL);
	}

      out_p += n;
      remaining_size -= n;

      n = snprintf (out_p, remaining_size, ";");
      out_p += n;
      remaining_size -= n;
    }
  while (p && remaining_size > 0);

  return err;
}
#endif /* !SERVER_MODE */

/*
 * sysprm_change_parameter_values () - update system parameter values
 *
 * return	    : void
 * assignments (in) : list of assignments
 * check (in)	    : check if the parameter belongs to current scope
 * set_flag (in)    : update PRM_SET flag if true
 *
 * NOTE: This function does not check if the new values are valid (e.g. in
 *	 the restricted range). First validate new values before calling this
 *	 function.
 */
void
sysprm_change_parameter_values (const SYSPRM_ASSIGN_VALUE * assignments,
				bool check, bool set_flag)
{
  SYSPRM_PARAM *prm = NULL;
  for (; assignments != NULL; assignments = assignments->next)
    {
      prm = GET_PRM (assignments->prm_id);
#if defined (CS_MODE)
      if (check)
	{
	  if (!PRM_IS_FOR_CLIENT (prm->static_flag))
	    {
	      /* skip this assignment */
	      continue;
	    }
	}
#endif
#if defined (SERVER_MODE)
      if (check)
	{
	  if (!PRM_IS_FOR_SERVER (prm->static_flag)
	      && !PRM_IS_FOR_SESSION (prm->static_flag))
	    {
	      /* skip this assignment */
	      continue;
	    }
	}
#endif
      sysprm_set_value (prm, assignments->value, set_flag, true);
#if defined (SERVER_MODE)
      if (sysprm_get_id (prm) == PRM_ID_ACCESS_IP_CONTROL)
	{
	  css_set_accessible_ip_info ();
	}
#endif
    }
}

/*
 * prm_print - Print a parameter to the buffer
 *   return: number of chars printed
 *   prm(in): parameter
 *   buf(out): print buffer
 *   len(in): length of the buffer
 *   print_mode(in): print name/id or just value of the parameter
 */
static int
prm_print (const SYSPRM_PARAM * prm, char *buf, size_t len,
	   PRM_PRINT_MODE print_mode, PRM_PRINT_VALUE_MODE print_value_mode)
{
  int n = 0;
  PARAM_ID id;
  int error = NO_ERROR;
  char left_side[PRM_DEFAULT_BUFFER_SIZE];
  void *prm_value;

  if (len == 0)
    {
      /* don't print anything */
      return 0;
    }

  memset (left_side, 0, PRM_DEFAULT_BUFFER_SIZE);

  assert (prm != NULL && buf != NULL && len > 0);

  if (PRM_DIFFERENT_UNIT (prm->static_flag))
    {
      assert (prm->get_dup != NULL);
    }

  if (print_mode == PRM_PRINT_ID)
    {
      id = sysprm_get_id (prm);
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%d=", id);
    }
  else if (print_mode == PRM_PRINT_NAME)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%s=", prm->name);
    }

  if (print_value_mode == PRM_PRINT_DEFAULT_VAL)
    {
      prm_value = prm->default_value;
    }
  else
    {
      prm_value = prm->value;
    }

  if (PRM_IS_INTEGER (prm))
    {
      int val = PRM_GET_INT (prm_value);
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm->static_flag) &&
	  !PRM_HAS_SIZE_UNIT (prm->static_flag) &&
	  !PRM_HAS_TIME_UNIT (prm->static_flag))
	{
	  PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  UINT64 dup_val;
	  val = PRM_GET_INT (prm_value);

	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &dup_val, &val,
						    &error);
	      if (error != NO_ERROR)
		{
		  assert_release (false);
		  return n;
		}
	    }
	  else
	    {
	      assert_release (false);
	    }

	  (void) util_byte_to_size_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else if (PRM_HAS_TIME_UNIT (prm->static_flag))
	{
	  INT64 dup_val;
	  val = PRM_GET_INT (prm_value);

	  if (PRM_DIFFERENT_UNIT (prm->static_flag) && val >= 0)
	    {
	      UINT64 tmp_val;
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &tmp_val, &val,
						    &error);
	      if (error != NO_ERROR)
		{
		  assert_release (false);
		  return n;
		}
	      dup_val = (INT64) tmp_val;
	    }
	  else
	    {
	      dup_val = (INT64) val;
	    }

	  (void) util_msec_to_time_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%d", left_side, val);
	}
    }
  else if (PRM_IS_BIGINT (prm))
    {
      UINT64 val = PRM_GET_BIGINT (prm_value);
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm->static_flag))
	{
	  PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  (void) util_byte_to_size_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%ld", left_side, val);
	}
    }
  else if (PRM_IS_BOOLEAN (prm))
    {
      n = snprintf (buf, len, "%s%c", left_side,
		    (PRM_GET_BOOL (prm_value) ? 'y' : 'n'));
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float val = PRM_GET_FLOAT (prm_value);
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm->static_flag) &&
	  !PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  UINT64 dup_val;
	  val = PRM_GET_FLOAT (prm_value);

	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_BIGINT (prm, &dup_val, &val,
						  &error);
	      if (error != NO_ERROR)
		{
		  assert_release (false);
		  return n;
		}
	    }
	  else
	    {
	      assert_release (false);
	    }

	  (void) util_byte_to_size_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%f", left_side, val);
	}
    }
  else if (PRM_IS_STRING (prm))
    {
      n = snprintf (buf, len, "%s\"%s\"", left_side,
		    (PRM_GET_STRING (prm_value) ?
		     PRM_GET_STRING (prm_value) : ""));
    }
  else if (PRM_IS_KEYWORD (prm))
    {
      const KEYVAL *keyvalp = NULL;

      if (intl_mbs_casecmp (prm->name, PRM_NAME_ER_LOG_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, er_log_level_words,
				 DIM (er_log_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_LOG_ISOLATION_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, isolation_level_words,
				 DIM (isolation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL) ==
	       0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL,
				 pgbuf_debug_page_validation_level_words,
				 DIM
				 (pgbuf_debug_page_validation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_MODE) == 0
	       || intl_mbs_casecmp (prm->name,
				    PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY) == 0)

	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, ha_mode_words, DIM (ha_mode_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_SERVER_STATE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, ha_server_state_words,
				 DIM (ha_server_state_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_HA_LOG_APPLIER_STATE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, ha_log_applier_state_words,
				 DIM (ha_log_applier_state_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_COMPAT_MODE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, compat_words, DIM (compat_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_CHECK_PEER_ALIVE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, check_peer_alive_words,
				 DIM (check_peer_alive_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_QUERY_TRACE_FORMAT) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm_value),
				 NULL, query_trace_format_words,
				 DIM (query_trace_format_words));
	}
      else
	{
	  assert (false);
	}

      if (keyvalp)
	{
	  n = snprintf (buf, len, "%s\"%s\"", left_side, keyvalp->key);
	}
      else
	{
	  n = snprintf (buf, len, "%s%d", left_side, PRM_GET_INT (prm_value));
	}
    }
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      int *int_list, list_size, i;
      char *s;

      int_list = PRM_GET_INTEGER_LIST (prm_value);
      if (int_list)
	{
	  list_size = int_list[0];
	  s = buf;
	  n = snprintf (s, len, "%s", left_side);
	  s += n;
	  len -= n;

	  for (i = 1; i <= list_size; i++)
	    {
	      n = snprintf (s, len, "%d,", int_list[i]);
	      s += n;
	      len -= n;
	    }

	  if (list_size > 0)
	    {
	      /* remove last "," */
	      s -= 1;
	      len += 1;
	    }
	  if (len > 0)
	    {
	      *s = '\0';
	    }
	  n = strlen (buf);
	}
      else
	{
	  n = snprintf (buf, len, "%s", left_side);
	}
    }
  else
    {
      n = snprintf (buf, len, "%s?", left_side);
    }

  return n;
}

/*
 * sysprm_print_sysprm_value () - print sysprm_value
 *
 * return	   : length of printed string
 * prm_id (in)	   : parameter ID (to which sysprm_value belongs).
 * value (in)	   : printed sysprm_value
 * buf (in/out)	   : printing destination
 * len (in)	   : maximum size of printed string
 * print_mode (in) : PRM_PRINT_MODE
 */
static int
sysprm_print_sysprm_value (PARAM_ID prm_id, SYSPRM_VALUE value, char *buf,
			   size_t len, PRM_PRINT_MODE print_mode)
{
  int n = 0;
  int error = NO_ERROR;
  char left_side[PRM_DEFAULT_BUFFER_SIZE];
  SYSPRM_PARAM *prm = NULL;

  if (len == 0)
    {
      /* don't print anything */
      return 0;
    }

  memset (left_side, 0, PRM_DEFAULT_BUFFER_SIZE);

  assert (buf != NULL && len > 0);
  prm = GET_PRM (prm_id);

  if (PRM_DIFFERENT_UNIT (prm->static_flag))
    {
      assert (prm->get_dup != NULL);
    }

  if (print_mode == PRM_PRINT_ID)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%d=", prm_id);
    }
  else if (print_mode == PRM_PRINT_NAME)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%s=", prm->name);
    }

  if (PRM_IS_INTEGER (prm))
    {
      int val = value.i;
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm->static_flag) &&
	  !PRM_HAS_SIZE_UNIT (prm->static_flag) &&
	  !PRM_HAS_TIME_UNIT (prm->static_flag))
	{
	  PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  UINT64 dup_val;
	  val = value.i;

	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &dup_val, &val,
						    &error);
	      if (error != NO_ERROR)
		{
		  assert_release (false);
		  return n;
		}
	    }
	  else
	    {
	      assert_release (false);
	    }

	  (void) util_byte_to_size_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else if (PRM_HAS_TIME_UNIT (prm->static_flag))
	{
	  INT64 dup_val;
	  val = value.i;

	  if (PRM_DIFFERENT_UNIT (prm->static_flag) && val >= 0)
	    {
	      UINT64 tmp_val;
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &tmp_val, &val,
						    &error);
	      if (error != NO_ERROR)
		{
		  assert_release (false);
		  return n;
		}
	      dup_val = (INT64) tmp_val;
	    }
	  else
	    {
	      dup_val = (INT64) val;
	    }

	  (void) util_msec_to_time_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%d", left_side, val);
	}
    }
  else if (PRM_IS_BIGINT (prm))
    {
      UINT64 val = value.bi;
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm->static_flag))
	{
	  PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  (void) util_byte_to_size_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%ld", left_side, val);
	}
    }
  else if (PRM_IS_BOOLEAN (prm))
    {
      n = snprintf (buf, len, "%s%c", left_side, (value.b ? 'y' : 'n'));
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float val = value.f;
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm->static_flag) &&
	  !PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	{
	  UINT64 dup_val;
	  val = value.f;

	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_BIGINT (prm, &dup_val, &val,
						  &error);
	      if (error != NO_ERROR)
		{
		  assert_release (false);
		  return n;
		}
	    }
	  else
	    {
	      assert_release (false);
	    }

	  (void) util_byte_to_size_string (left_side + left_side_len,
					   PRM_DEFAULT_BUFFER_SIZE -
					   left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%f", left_side, val);
	}
    }
  else if (PRM_IS_STRING (prm))
    {
      n = snprintf (buf, len, "%s\"%s\"", left_side,
		    (value.str != NULL ? value.str : ""));
    }
  else if (PRM_IS_KEYWORD (prm))
    {
      const KEYVAL *keyvalp = NULL;

      if (intl_mbs_casecmp (prm->name, PRM_NAME_ER_LOG_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, er_log_level_words,
				 DIM (er_log_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_LOG_ISOLATION_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, isolation_level_words,
				 DIM (isolation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL) ==
	       0)
	{
	  keyvalp = prm_keyword (value.i, NULL,
				 pgbuf_debug_page_validation_level_words,
				 DIM
				 (pgbuf_debug_page_validation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_MODE) == 0
	       || intl_mbs_casecmp (prm->name,
				    PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, ha_mode_words,
				 DIM (ha_mode_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_SERVER_STATE) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, ha_server_state_words,
				 DIM (ha_server_state_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_HA_LOG_APPLIER_STATE) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, ha_log_applier_state_words,
				 DIM (ha_log_applier_state_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_COMPAT_MODE) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, compat_words,
				 DIM (compat_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_CHECK_PEER_ALIVE) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, check_peer_alive_words,
				 DIM (check_peer_alive_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_QUERY_TRACE_FORMAT) == 0)
	{
	  keyvalp = prm_keyword (value.i, NULL, query_trace_format_words,
				 DIM (query_trace_format_words));
	}
      else
	{
	  assert (false);
	}

      if (keyvalp)
	{
	  n = snprintf (buf, len, "%s\"%s\"", left_side, keyvalp->key);
	}
      else
	{
	  n = snprintf (buf, len, "%s%d", left_side, value.i);
	}
    }
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      int *int_list = NULL, list_size, i;
      char *s = NULL;

      int_list = value.integer_list;
      if (int_list)
	{
	  list_size = int_list[0];
	  s = buf;
	  n = snprintf (s, len, "%s", left_side);
	  s += n;
	  len -= n;

	  for (i = 1; i <= list_size; i++)
	    {
	      n = snprintf (s, len, "%d,", int_list[i]);
	      s += n;
	      len -= n;
	    }

	  if (list_size > 0)
	    {
	      /* remove last "," */
	      s -= 1;
	      len += 1;
	    }
	  if (len > 0)
	    {
	      *s = '\0';
	    }
	  n = strlen (buf);
	}
      else
	{
	  n = snprintf (buf, len, "%s", left_side);
	}
    }
  else
    {
      n = snprintf (buf, len, "%s?", left_side);
    }

  return n;
}

/*
 * sysprm_obtain_parameters () - Get parameter values
 *
 * return		: SYSPRM_ERR code.
 * data (in)	        : string containing the names of parameters.
 * prm_values_ptr (out) : list of ids and values for the parameters read from
 *			  data.
 *
 * NOTE: Multiple parameters can be obtained by providing a string like:
 *	 "param_name1; param_name2; ..."
 *	 If some values must be read from server, PRM_ERR_NOT_FOR_CLIENT is
 *	 returned.
 */
int
sysprm_obtain_parameters (char *data, SYSPRM_ASSIGN_VALUE ** prm_values_ptr)
{
  char buf[LINE_MAX], *p = NULL, *name = NULL;
  SYSPRM_PARAM *prm = NULL;
  SYSPRM_ASSIGN_VALUE *prm_value_list = NULL, *last_prm_value = NULL;
  SYSPRM_ASSIGN_VALUE *prm_value = NULL;
  SYSPRM_ERR error = PRM_ERR_NO_ERROR, scope_error = PRM_ERR_NO_ERROR;

  if (!data || *data == '\0')
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (intl_mbs_ncpy (buf, data, LINE_MAX) == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  assert (prm_values_ptr != NULL);
  *prm_values_ptr = NULL;

  p = buf;
  do
    {
      /* read name */
      while (char_isspace (*p))
	{
	  p++;
	}
      if (*p == '\0')
	{
	  break;
	}
      name = p;

      while (*p && !char_isspace (*p) && *p != ';')
	{
	  p++;
	}

      if (*p)
	{
	  *p++ = '\0';
	  while (char_isspace (*p))
	    {
	      p++;
	    }
	  if (*p == ';')
	    {
	      p++;
	    }
	}

      prm = prm_find (name, NULL);
      if (prm == NULL)
	{
	  error = PRM_ERR_UNKNOWN_PARAM;
	  break;
	}

#if defined (CS_MODE)
      if (!PRM_IS_FOR_CLIENT (prm->static_flag)
	  && !PRM_IS_FOR_SERVER (prm->static_flag))
	{
	  error = PRM_ERR_CANNOT_CHANGE;
	  break;
	}

      if ((PRM_IS_FOR_SERVER (prm->static_flag)
	   && !PRM_IS_FOR_CLIENT (prm->static_flag))
	  || PRM_IS_GET_SERVER (prm->static_flag))
	{
	  /* have to read the value on server */
	  scope_error = PRM_ERR_NOT_FOR_CLIENT;
	}
#endif /* CS_MODE */

      /* create a SYSPRM_ASSING_VALUE object to store parameter value */
      prm_value =
	(SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
      if (prm_value == NULL)
	{
	  error = PRM_ERR_NO_MEM_FOR_PRM;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (SYSPRM_ASSIGN_VALUE));
	  break;
	}
      prm_value->prm_id = sysprm_get_id (prm);
      prm_value->next = NULL;
#if defined (CS_MODE)
      if (PRM_IS_FOR_CLIENT (prm->static_flag))
	{
	  /* set the value here */
	  sysprm_set_sysprm_value_from_parameter (&prm_value->value, prm);
	}
      else
	{
	  memset (&prm_value->value, 0, sizeof (SYSPRM_VALUE));
	}
#else /* CS_MODE */
      sysprm_set_sysprm_value_from_parameter (&prm_value->value, prm);
#endif /* !CS_MODE */

      /* append prm_value to prm_value_list */
      if (prm_value_list != NULL)
	{
	  last_prm_value->next = prm_value;
	  last_prm_value = prm_value;
	}
      else
	{
	  prm_value_list = last_prm_value = prm_value;
	}
    }
  while (*p);

  if (error == PRM_ERR_NO_ERROR)
    {
      /* all parameter names are valid and values can be obtained */
      *prm_values_ptr = prm_value_list;
      /* update error in order to get values from server too if needed */
      error = scope_error;
    }
  else
    {
      /* error obtaining values, clean up */
      sysprm_free_assign_values (&prm_value_list);
    }

  return error;
}

#if !defined(CS_MODE)
/*
 * xsysprm_change_server_parameters () - changes parameter values on server
 *
 * return	    : void
 * assignments (in) : list of changes
 */
void
xsysprm_change_server_parameters (const SYSPRM_ASSIGN_VALUE * assignments)
{
  sysprm_change_parameter_values (assignments, true, true);
}

/*
 * xsysprm_obtain_server_parameters () - get parameter values from server
 *
 * return	   : void
 * prm_values (in) : list of parameters
 *
 * NOTE: Obtains value for parameters that are for server only. For parameters
 *       that are client/server, values should be obtained from client UNLESS
 *       it has PRM_GET_SERVER flag. 
 */
void
xsysprm_obtain_server_parameters (SYSPRM_ASSIGN_VALUE * prm_values)
{
  SYSPRM_PARAM *prm = NULL;

  for (; prm_values != NULL; prm_values = prm_values->next)
    {
      prm = GET_PRM (prm_values->prm_id);

      if ((PRM_IS_FOR_SERVER (prm->static_flag)
	   && !PRM_IS_FOR_CLIENT (prm->static_flag))
	  || PRM_IS_GET_SERVER (prm->static_flag))
	{
	  /* set value */
	  sysprm_set_sysprm_value_from_parameter (&prm_values->value, prm);
	}
    }
}

/*
 * xsysprm_get_force_server_parameters () - obtain values for parameters
 *					    marked as PRM_FORCE_SERVER
 *
 * return : list of values
 *
 * NOTE: This is called after client registers to server.
 */
SYSPRM_ASSIGN_VALUE *
xsysprm_get_force_server_parameters (void)
{
  SYSPRM_ASSIGN_VALUE *force_values = NULL, *last_assign = NULL;
  SYSPRM_PARAM *prm = NULL;
  int i;

  for (i = 0; i < NUM_PRM; i++)
    {
      prm = GET_PRM (i);
      if (PRM_GET_FROM_SERVER (prm->static_flag))
	{
	  SYSPRM_ASSIGN_VALUE *change_val =
	    (SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
	  if (change_val == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      sizeof (SYSPRM_ASSIGN_VALUE));
	      goto cleanup;
	    }
	  change_val->prm_id = i;
	  change_val->next = NULL;
	  sysprm_set_sysprm_value_from_parameter (&change_val->value, prm);
	  if (force_values != NULL)
	    {
	      last_assign->next = change_val;
	      last_assign = change_val;
	    }
	  else
	    {
	      force_values = last_assign = change_val;
	    }
	}
    }

  return force_values;

cleanup:
  sysprm_free_assign_values (&force_values);
  return NULL;
}

/*
 * xsysprm_dump_server_parameters -
 *   return: none
 *   fp(in):
 */
void
xsysprm_dump_server_parameters (FILE * outfp)
{
  sysprm_dump_parameters (outfp);
}
#endif /* !CS_MODE */

/*
 * sysprm_get_range -
 *   return:
 *   pname (in): parameter name
 *   value (in): parameter value
 */
int
sysprm_get_range (const char *pname, void *min, void *max)
{
  int error = NO_ERROR;
  SYSPRM_PARAM *prm;

  prm = prm_find (pname, NULL);
  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (PRM_DIFFERENT_UNIT (prm->static_flag))
    {
      assert (prm->get_dup != NULL);
    }

  if (PRM_IS_INTEGER (prm))
    {
      if (prm->lower_limit)
	{
	  *((int *) min) = PRM_GET_INT (prm->lower_limit);
	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, (int *) min,
						     (int *) min, &error);
	    }
	}
      else
	{
	  *((int *) min) = INT_MIN;
	}

      if (prm->upper_limit)
	{
	  *((int *) max) = PRM_GET_INT (prm->upper_limit);
	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, (int *) max,
						     (int *) max, &error);
	    }
	}
      else
	{
	  *((int *) max) = INT_MAX;
	}
    }
  else if (PRM_IS_FLOAT (prm))
    {
      if (prm->lower_limit)
	{
	  *((float *) min) = PRM_GET_FLOAT (prm->lower_limit);
	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, (float *) min,
						 (float *) min, &error);
	    }
	}
      else
	{
	  *((float *) min) = FLT_MIN;
	}

      if (prm->upper_limit)
	{
	  *((float *) max) = PRM_GET_FLOAT (prm->upper_limit);
	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, (float *) max,
						 (float *) max, &error);
	    }
	}
      else
	{
	  *((float *) max) = FLT_MAX;
	}
    }
  else if (PRM_IS_BIGINT (prm))
    {
      if (prm->lower_limit)
	{
	  *((UINT64 *) min) = PRM_GET_BIGINT (prm->lower_limit);
	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, (UINT64 *) min,
						   (UINT64 *) min, &error);
	    }
	}
      else
	{
	  *((UINT64 *) min) = 0ULL;
	}

      if (prm->upper_limit)
	{
	  *((UINT64 *) max) = PRM_GET_BIGINT (prm->upper_limit);
	  if (PRM_DIFFERENT_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, (UINT64 *) max,
						   (UINT64 *) max, &error);
	    }
	}
      else
	{
	  *((UINT64 *) max) = ULLONG_MAX;
	}
    }
  else
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (error != NO_ERROR)
    {
      return PRM_ERR_BAD_VALUE;
    }

  return PRM_ERR_NO_ERROR;
}

/*
 * sysprm_check_range -
 *   return:
 *   pname (in): parameter name
 *   value (in): parameter value
 */
int
sysprm_check_range (const char *pname, void *value)
{
  int error = 0;
  SYSPRM_PARAM *prm;

  prm = prm_find (pname, NULL);
  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  error = prm_check_range (prm, value);

  return error;
}

/*
 * prm_check_range -
 *   return:
 *   prm(in):
 *   value (in):
 */
static int
prm_check_range (SYSPRM_PARAM * prm, void *value)
{
  int error = NO_ERROR;

  if (PRM_DIFFERENT_UNIT (prm->static_flag))
    {
      assert (prm->set_dup != NULL);
    }

  if (PRM_IS_INTEGER (prm) || PRM_IS_KEYWORD (prm))
    {
      int val;

      if (PRM_DIFFERENT_UNIT (prm->static_flag))
	{
	  if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val,
						    (UINT64 *) value, &error);
	    }
	  else if (PRM_HAS_TIME_UNIT (prm->static_flag))
	    {
	      INT64 *dup_val = (INT64 *) value;

	      if (*dup_val >= 0)
		{
		  UINT64 tmp_val = (UINT64) * dup_val;
		  PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val,
							&tmp_val, &error);
		}
	      else
		{
		  val = (int) *dup_val;
		}
	    }
	  else
	    {
	      PRM_ADJUST_FOR_SET_INTEGER_TO_INTEGER (prm, &val, (int *) value,
						     &error);
	    }
	  if (error != NO_ERROR)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }
	}
      else
	{
	  val = *((int *) value);
	}

      if ((prm->upper_limit && PRM_GET_INT (prm->upper_limit) < val)
	  || (prm->lower_limit && PRM_GET_INT (prm->lower_limit) > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float val;
      float lower, upper;

      lower = upper = 0;	/* to make compilers be silent */
      if (PRM_DIFFERENT_UNIT (prm->static_flag))
	{
	  if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	    {
	      PRM_ADJUST_FOR_SET_BIGINT_TO_FLOAT (prm, &val, (UINT64 *) value,
						  &error);
	    }
	  else
	    {
	      PRM_ADJUST_FOR_SET_FLOAT_TO_FLOAT (prm, &val, (float *) value,
						 &error);
	    }
	  if (error != NO_ERROR)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }

	  if (prm->upper_limit)
	    {
	      upper = ceilf (PRM_GET_FLOAT (prm->upper_limit) * 100) / 100;
	    }

	  if (prm->lower_limit)
	    {
	      lower = ceilf (PRM_GET_FLOAT (prm->lower_limit) * 100) / 100;
	    }
	}
      else
	{
	  val = *((float *) value);

	  if (prm->upper_limit)
	    {
	      upper = PRM_GET_FLOAT (prm->upper_limit);
	    }

	  if (prm->lower_limit)
	    {
	      lower = PRM_GET_FLOAT (prm->lower_limit);
	    }
	}

      if ((prm->upper_limit && upper < val)
	  || (prm->lower_limit && lower > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}
    }
  else if (PRM_IS_BIGINT (prm))
    {
      UINT64 val;

      if (PRM_DIFFERENT_UNIT (prm->static_flag))
	{
	  PRM_ADJUST_FOR_SET_BIGINT_TO_BIGINT (prm, &val, (UINT64 *) value,
					       &error);
	  if (error != NO_ERROR)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }
	}
      else
	{
	  val = *((UINT64 *) value);
	}

      if ((prm->upper_limit && PRM_GET_BIGINT (prm->upper_limit) < val)
	  || (prm->lower_limit && PRM_GET_BIGINT (prm->lower_limit) > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}
    }
  else
    {
      return PRM_ERR_BAD_VALUE;
    }

  return PRM_ERR_NO_ERROR;
}

/*
 * sysprm_generate_new_value () - converts string into a system parameter value
 *
 * return	   : SYSPRM_ERR
 * prm (in)	   : target system parameter
 * value (in)	   : parameter value in char * format
 * check (in)	   : check if value can be changed. set to false if value
 *		     should be forced
 * new_value (out) : SYSPRM_VALUE converted from string
 */
static SYSPRM_ERR
sysprm_generate_new_value (SYSPRM_PARAM * prm, const char *value, bool check,
			   SYSPRM_VALUE * new_value)
{
  char *end = NULL;
  int error = NO_ERROR;
  SYSPRM_ERR ret = PRM_ERR_NO_ERROR;

  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }
  if (value == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  assert (new_value != NULL);

  if (PRM_DIFFERENT_UNIT (prm->static_flag))
    {
      assert (prm->set_dup != NULL);
    }

#if defined (CS_MODE)
  if (check)
    {
      /* check the scope of parameter */
      if (PRM_IS_FOR_CLIENT (prm->static_flag))
	{
	  if (PRM_IS_FOR_SESSION (prm->static_flag))
	    {
	      /* the value in session state must also be updated. user doesn't
	       * have to be part of DBA group.
	       */
	      ret = PRM_ERR_NOT_FOR_CLIENT_NO_AUTH;
	    }
	  else if (PRM_IS_FOR_SERVER (prm->static_flag))
	    {
	      /* the value has to be changed on server too. user has to be
	       * part of DBA group.
	       */
	      ret = PRM_ERR_NOT_FOR_CLIENT;
	    }
	}
      else
	{
	  if (PRM_IS_FOR_SERVER (prm->static_flag))
	    {
	      /* this value is only for server. user has to be DBA. */
	      ret = PRM_ERR_NOT_FOR_CLIENT;
	    }
	  else
	    {
	      /* not for client or server, cannot be changed on-line */
	      return PRM_ERR_CANNOT_CHANGE;
	    }
	}
    }
#endif /* CS_MODE */

#if defined (SERVER_MODE)
  if (check)
    {
      if (!PRM_IS_FOR_SERVER (prm->static_flag)
	  && !PRM_IS_FOR_SESSION (prm->static_flag))
	{
	  return PRM_ERR_NOT_FOR_SERVER;
	}
    }
#endif /* SERVER_MODE */

  switch (prm->datatype)
    {
    case PRM_INTEGER:
      {
	/* convert string to int */
	int val;

	if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	  {
	    UINT64 dup_val;
	    if (util_size_string_to_byte (&dup_val, value) != NO_ERROR)
	      {
		return PRM_ERR_BAD_VALUE;
	      }

	    if (prm_check_range (prm, (void *) &dup_val) != NO_ERROR)
	      {
		return PRM_ERR_BAD_RANGE;
	      }

	    if (PRM_DIFFERENT_UNIT (prm->static_flag))
	      {
		PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val, &dup_val,
						      &error);
		if (error != NO_ERROR)
		  {
		    return PRM_ERR_BAD_VALUE;
		  }
	      }
	    else
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	  }
	else if (PRM_HAS_TIME_UNIT (prm->static_flag))
	  {
	    INT64 dup_val;

	    if (util_time_string_to_msec (&dup_val, (char *) value) !=
		NO_ERROR)
	      {
		return PRM_ERR_BAD_VALUE;
	      }

	    if (prm_check_range (prm, (void *) &dup_val) != NO_ERROR)
	      {
		return PRM_ERR_BAD_RANGE;
	      }

	    if (PRM_DIFFERENT_UNIT (prm->static_flag) && dup_val >= 0)
	      {
		UINT64 tmp_val = (UINT64) dup_val;
		PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val, &tmp_val,
						      &error);
		if (error != NO_ERROR)
		  {
		    return PRM_ERR_BAD_VALUE;
		  }
	      }
	    else
	      {
		val = (int) dup_val;
	      }
	  }
	else
	  {
	    int result;

	    result = parse_int (&val, value, 10);

	    if (result != 0)
	      {
		return PRM_ERR_BAD_VALUE;
	      }

	    if (prm_check_range (prm, (void *) &val) != NO_ERROR)
	      {
		return PRM_ERR_BAD_RANGE;
	      }

	    if (PRM_DIFFERENT_UNIT (prm->static_flag))
	      {
		PRM_ADJUST_FOR_SET_INTEGER_TO_INTEGER (prm, &val, &val,
						       &error);
		if (error != NO_ERROR)
		  {
		    return PRM_ERR_BAD_VALUE;
		  }
	      }
	  }

	new_value->i = val;
	break;
      }

    case PRM_BIGINT:
      {
	/* convert string to UINT64 */
	int result;
	UINT64 val;
	char *end_p;

	if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	  {
	    if (util_size_string_to_byte (&val, value) != NO_ERROR)
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	  }
	else
	  {
	    result = str_to_uint64 (&val, &end_p, value, 10);
	    if (result != 0)
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	  }

	if (prm_check_range (prm, (void *) &val) != NO_ERROR)
	  {
	    return PRM_ERR_BAD_RANGE;
	  }

	if (PRM_DIFFERENT_UNIT (prm->static_flag))
	  {
	    PRM_ADJUST_FOR_SET_BIGINT_TO_BIGINT (prm, &val, &val, &error);
	    if (error != NO_ERROR)
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	  }

	new_value->bi = val;
	break;
      }

    case PRM_FLOAT:
      {
	/* convert string to float */
	float val;

	if (PRM_HAS_SIZE_UNIT (prm->static_flag))
	  {
	    UINT64 dup_val;
	    if (util_size_string_to_byte (&dup_val, value) != NO_ERROR)
	      {
		return PRM_ERR_BAD_VALUE;
	      }

	    if (prm_check_range (prm, (void *) &dup_val) != NO_ERROR)
	      {
		return PRM_ERR_BAD_RANGE;
	      }

	    if (PRM_DIFFERENT_UNIT (prm->static_flag))
	      {
		PRM_ADJUST_FOR_SET_BIGINT_TO_FLOAT (prm, &val, &dup_val,
						    &error);
		if (error != NO_ERROR)
		  {
		    return PRM_ERR_BAD_VALUE;
		  }
	      }
	    else
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	  }
	else
	  {
	    val = (float) strtod (value, &end);
	    if (end == value)
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	    else if (*end != '\0')
	      {
		return PRM_ERR_BAD_VALUE;
	      }

	    if (prm_check_range (prm, (void *) &val) != NO_ERROR)
	      {
		return PRM_ERR_BAD_RANGE;
	      }

	    if (PRM_DIFFERENT_UNIT (prm->static_flag))
	      {
		PRM_ADJUST_FOR_SET_FLOAT_TO_FLOAT (prm, &val, &val, &error);
		if (error != NO_ERROR)
		  {
		    return PRM_ERR_BAD_VALUE;
		  }
	      }
	  }

	new_value->f = val;
	break;
      }

    case PRM_BOOLEAN:
      {
	/* convert string to boolean */
	const KEYVAL *keyvalp = NULL;

	keyvalp = prm_keyword (-1, value, boolean_words, DIM (boolean_words));
	if (keyvalp == NULL)
	  {
	    return PRM_ERR_BAD_VALUE;
	  }

	new_value->b = (bool) keyvalp->val;
	break;
      }

    case PRM_STRING:
      {
	/* duplicate string */
	char *val = NULL;

	/* check if the value is represented as a null keyword */
	if (prm_keyword (-1, value, null_words, DIM (null_words)) != NULL)
	  {
	    val = NULL;
	  }
	else
	  {
	    val = strdup (value);
	    if (val == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (value));
		return PRM_ERR_NO_MEM_FOR_PRM;
	      }
	  }
	new_value->str = val;
	break;
      }

    case PRM_INTEGER_LIST:
      {
	/* convert string into an array of integers */
	int *val = NULL;

	/* check if the value is represented as a null keyword */
	if (prm_keyword (-1, value, null_words, DIM (null_words)) != NULL)
	  {
	    val = NULL;
	  }
	else
	  {
	    char *s, *p;
	    char save;
	    int list_size, tmp;

	    val = calloc (1024, sizeof (int));	/* max size is 1023 */
	    if (val == NULL)
	      {
		size_t size = 1024 * sizeof (int);
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		return PRM_ERR_NO_MEM_FOR_PRM;
	      }

	    list_size = 0;
	    s = (char *) value;
	    p = s;

	    while (true)
	      {
		if (*s == ',' || *s == '\0')
		  {
		    save = *s;
		    *s = '\0';
		    if (intl_mbs_casecmp ("default", p) == 0)
		      {
			if (sysprm_get_id (prm) ==
			    PRM_ID_CALL_STACK_DUMP_ACTIVATION)
			  {
			    memcpy (&val[list_size + 1],
				    call_stack_dump_error_codes,
				    sizeof (call_stack_dump_error_codes));
			    list_size += DIM (call_stack_dump_error_codes);
			  }
			else
			  {
			    free_and_init (val);
			    return PRM_ERR_BAD_VALUE;
			  }
		      }
		    else
		      {
			int result;

			result = parse_int (&tmp, p, 10);
			if (result != 0)
			  {
			    free_and_init (val);
			    return PRM_ERR_BAD_VALUE;
			  }
			val[++list_size] = tmp;
		      }
		    *s = save;
		    if (*s == '\0')
		      {
			break;
		      }
		    p = s + 1;
		  }
		s++;
	      }
	    /* save size in the first position */
	    val[0] = list_size;
	  }
	new_value->integer_list = val;
	break;
      }

    case PRM_KEYWORD:
      {
	/* check if string can be identified as a keyword */
	int val;
	const KEYVAL *keyvalp = NULL;

	if (intl_mbs_casecmp (prm->name, PRM_NAME_ER_LOG_LEVEL) == 0)
	  {
	    keyvalp = prm_keyword (-1, value, er_log_level_words,
				   DIM (er_log_level_words));
	  }
	else if (intl_mbs_casecmp (prm->name,
				   PRM_NAME_LOG_ISOLATION_LEVEL) == 0)
	  {
	    keyvalp = prm_keyword (-1, value, isolation_level_words,
				   DIM (isolation_level_words));
	  }
	else if (intl_mbs_casecmp (prm->name,
				   PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL)
		 == 0)
	  {
	    keyvalp = prm_keyword (-1, value,
				   pgbuf_debug_page_validation_level_words,
				   DIM
				   (pgbuf_debug_page_validation_level_words));
	  }
	else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_MODE) == 0
		 || intl_mbs_casecmp (prm->name,
				      PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY) ==
		 0)
	  {
	    keyvalp = prm_keyword (-1, value, ha_mode_words,
				   DIM (ha_mode_words));
	  }
	else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_SERVER_STATE) == 0)
	  {
	    keyvalp = prm_keyword (-1, value, ha_server_state_words,
				   DIM (ha_server_state_words));
	  }
	else if (intl_mbs_casecmp (prm->name,
				   PRM_NAME_HA_LOG_APPLIER_STATE) == 0)
	  {
	    keyvalp = prm_keyword (-1, value, ha_log_applier_state_words,
				   DIM (ha_log_applier_state_words));
	  }
	else if (intl_mbs_casecmp (prm->name, PRM_NAME_COMPAT_MODE) == 0)
	  {
	    keyvalp = prm_keyword (-1, value, compat_words,
				   DIM (compat_words));
	  }
	else if (intl_mbs_casecmp (prm->name, PRM_NAME_CHECK_PEER_ALIVE) == 0)
	  {
	    keyvalp = prm_keyword (-1, value, check_peer_alive_words,
				   DIM (check_peer_alive_words));
	  }
	else if (intl_mbs_casecmp (prm->name,
				   PRM_NAME_QUERY_TRACE_FORMAT) == 0)
	  {
	    keyvalp = prm_keyword (-1, value, query_trace_format_words,
				   DIM (query_trace_format_words));
	  }
	else
	  {
	    assert (false);
	  }

	if (keyvalp)
	  {
	    val = (int) keyvalp->val;
	  }
	else
	  {
	    int result;
	    /* check if string can be converted to an integer */
	    result = parse_int (&val, value, 10);
	    if (result != 0)
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	  }

	if ((prm->upper_limit && PRM_GET_INT (prm->upper_limit) < val)
	    || (prm->lower_limit && PRM_GET_INT (prm->lower_limit) > val))
	  {
	    return PRM_ERR_BAD_RANGE;
	  }
	new_value->i = val;
      }
      break;
    case PRM_NO_TYPE:
      break;

    default:
      assert (false);
    }

  return ret;
}

/*
 * prm_set () - Set a new value for parameter.
 *
 * return	 : SYSPRM_ERR code.
 * prm (in)	 : system parameter that will have its value changed.
 * value (in)	 : new value as string.
 * set_flag (in) : updates PRM_SET flag is true.
 */
static int
prm_set (SYSPRM_PARAM * prm, const char *value, bool set_flag)
{
  SYSPRM_ERR error = PRM_ERR_NO_ERROR;
  SYSPRM_VALUE new_value;

  error = sysprm_generate_new_value (prm, value, false, &new_value);
  if (error != PRM_ERR_NO_ERROR)
    {
      return error;
    }

  return sysprm_set_value (prm, new_value, set_flag, false);
}

/*
 * sysprm_set_value () - Set a new value for parameter.
 *
 * return	  : SYSPRM_ERR code
 * prm (in)       : system parameter that will have its value changed.
 * value (in)     : new values as sysprm_value
 * set_flag (in)  : updates PRM_SET flag.
 * duplicate (in) : duplicate values for data types that need memory
 *		    allocation.
 */
static int
sysprm_set_value (SYSPRM_PARAM * prm, SYSPRM_VALUE value, bool set_flag,
		  bool duplicate)
{
#if defined (SERVER_MODE)
  PARAM_ID id;
  THREAD_ENTRY *thread_p;
#endif

  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (duplicate)
    {
      /* duplicate values for data types that need memory allocation */
      switch (prm->datatype)
	{
	case PRM_STRING:
	  if (value.str != NULL)
	    {
	      value.str = strdup (value.str);
	      if (value.str == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  strlen (value.str) + 1);
		  return PRM_ERR_NO_MEM_FOR_PRM;
		}
	    }
	  break;

	case PRM_INTEGER_LIST:
	  if (value.integer_list != NULL)
	    {
	      int *integer_list = value.integer_list;
	      value.integer_list =
		(int *) malloc ((integer_list[0] + 1) * sizeof (int));
	      if (value.integer_list == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  (integer_list[0] + 1) * sizeof (int));
		  return PRM_ERR_NO_MEM_FOR_PRM;
		}
	      memcpy (value.integer_list, integer_list,
		      (integer_list[0] + 1) * sizeof (int));
	    }

	default:
	  break;
	}
    }

#if defined (SERVER_MODE)
  if (PRM_IS_FOR_SESSION (prm->static_flag) && BO_IS_SERVER_RESTARTED ())
    {
      SESSION_PARAM *param;

      /* update session parameter */
      id = sysprm_get_id (prm);
      thread_p = thread_get_thread_entry_info ();

      param = session_get_session_parameter (thread_p, id);
      if (param == NULL)
	{
	  return PRM_ERR_UNKNOWN_PARAM;
	}

      return sysprm_set_session_parameter_value (param, id, value);
    }

  /* if prm is not for session or if session_parameters have not been
   * initialized just set the system parameter stored on server
   */
#endif /* SERVER_MODE */

  sysprm_set_system_parameter_value (prm, value);

  if (PRM_IS_COMPOUND (prm->static_flag))
    {
      prm_compound_has_changed (prm, set_flag);
    }

  if (set_flag)
    {
      PRM_SET_BIT (PRM_SET, *prm->dynamic_flag);
      /* Indicate that the default value was not used */
      PRM_CLEAR_BIT (PRM_DEFAULT_USED, *prm->dynamic_flag);
    }

  return PRM_ERR_NO_ERROR;
}

/*
 * sysprm_set_system_parameter_value () - change a parameter value in prm_Def
 *					  array.
 *
 * return     : void.
 * prm (in)   : parameter that needs changed.
 * value (in) : new value.
 */
static void
sysprm_set_system_parameter_value (SYSPRM_PARAM * prm, SYSPRM_VALUE value)
{
  PARAM_ID prm_id = sysprm_get_id (prm);
  switch (prm->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      prm_set_integer_value (prm_id, value.i);
      break;

    case PRM_FLOAT:
      prm_set_float_value (prm_id, value.f);
      break;

    case PRM_BOOLEAN:
      prm_set_bool_value (prm_id, value.b);
      break;

    case PRM_STRING:
      prm_set_string_value (prm_id, value.str);
      break;

    case PRM_INTEGER_LIST:
      prm_set_integer_list_value (prm_id, value.integer_list);
      break;

    case PRM_BIGINT:
      prm_set_bigint_value (prm_id, value.bi);
      break;
    default:
      assert_release (0);
      break;
    }
}

/*
 * prm_compound_has_changed () - update all affected system parameters if prm
 *				 is a compound.
 *
 * return	 : error code
 * prm (in)	 : system parameter that has changed
 * set_flag (in) : updates PRM_SET flag.
 */
static int
prm_compound_has_changed (SYSPRM_PARAM * prm, bool set_flag)
{
  assert (PRM_IS_COMPOUND (prm->static_flag));

  if (sysprm_get_id (prm) == PRM_ID_COMPAT_MODE)
    {
      prm_set_compound (prm, compat_mode_values, DIM (compat_mode_values),
			set_flag);
    }
  else
    {
      assert (false);
    }
  return NO_ERROR;
}


/*
 * prm_set_force -
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   prm(in):
 *   value(in):
 */
static int
prm_set_force (SYSPRM_PARAM * prm, const char *value)
{
  if (prm->force_value)
    {
      free_and_init (PRM_GET_STRING (&prm->force_value));
    }

  prm->force_value = strdup (value);
  if (prm->force_value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      strlen (value) + 1);
      return PRM_ERR_NO_MEM_FOR_PRM;
    }

  return NO_ERROR;
}

/*
 * prm_set_default -
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   prm(in):
 */
static int
prm_set_default (SYSPRM_PARAM * prm)
{
#if defined (SERVER_MODE)
  if (PRM_IS_FOR_SESSION (prm->static_flag) && BO_IS_SERVER_RESTARTED ())
    {
      PARAM_ID id;
      SESSION_PARAM *sprm = NULL;
      THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();

      id = sysprm_get_id (prm);
      sprm = session_get_session_parameter (thread_p, id);
      if (sprm == NULL)
	{
	  return PRM_ERR_UNKNOWN_PARAM;
	}

      return sysprm_set_session_parameter_default (sprm, id);
    }
#endif /* SERVER_MODE */

  if (prm == NULL)
    {
      return ER_FAILED;
    }

  if (PRM_IS_INTEGER (prm) || PRM_IS_KEYWORD (prm))
    {
      int val, *valp;

      val = PRM_GET_INT (prm->default_value);
      valp = (int *) prm->value;
      *valp = val;
    }
  if (PRM_IS_BIGINT (prm))
    {
      UINT64 val, *valp;

      val = PRM_GET_BIGINT (prm->default_value);
      valp = (UINT64 *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_BOOLEAN (prm))
    {
      bool val, *valp;

      val = PRM_GET_BOOL (prm->default_value);
      valp = (bool *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float val, *valp;

      val = PRM_GET_FLOAT (prm->default_value);
      valp = (float *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_STRING (prm))
    {
      char *val, **valp;

      if (PRM_IS_ALLOCATED (*prm->dynamic_flag))
	{
	  char *str = PRM_GET_STRING (prm->value);
	  free_and_init (str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, *prm->dynamic_flag);
	}

      val = *(char **) prm->default_value;
      valp = (char **) prm->value;
      *valp = val;
    }
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      int *val, **valp;

      if (PRM_IS_ALLOCATED (*prm->dynamic_flag))
	{
	  int *int_list = PRM_GET_INTEGER_LIST (prm->value);

	  free_and_init (int_list);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, *prm->dynamic_flag);
	}

      val = *(int **) prm->default_value;
      valp = (int **) prm->value;
      *valp = val;
    }

  /* Indicate that the default value was used */
  PRM_SET_BIT (PRM_DEFAULT_USED, *prm->dynamic_flag);

  if (PRM_IS_FOR_QRY_STRING (prm->static_flag))
    {
      PRM_CLEAR_BIT (PRM_DIFFERENT, *prm->dynamic_flag);
    }

  return NO_ERROR;
}

/*
 * prm_find -
 *   return: NULL or found parameter
 *   pname(in): parameter name to find
 */
static SYSPRM_PARAM *
prm_find (const char *pname, const char *section)
{
  unsigned int i;
  char *key;
  char buf[4096];

  if (pname == NULL)
    {
      return NULL;
    }

  if (section != NULL)
    {
      snprintf (buf, sizeof (buf) - 1, "%s::%s", section, pname);
      key = buf;
    }
  else
    {
      key = (char *) pname;
    }

  for (i = 0; i < DIM (prm_Def); i++)
    {
      if (intl_mbs_casecmp (prm_Def[i].name, key) == 0)
	{
	  return &prm_Def[i];
	}
    }

  return NULL;
}

/*
 * sysprm_set_force -
 *   return: NO_ERROR or error code
 *   pname(in): parameter name to set
 *   pvalue(in): value to be set to the parameter
 */
int
sysprm_set_force (const char *pname, const char *pvalue)
{
  SYSPRM_PARAM *prm;

  if (pname == NULL || pvalue == NULL)
    {
      return ER_PRM_BAD_VALUE;
    }

  prm = prm_find (pname, NULL);
  if (prm == NULL)
    {
      return ER_PRM_BAD_VALUE;
    }

  if (prm_set_force (prm, pvalue) != NO_ERROR)
    {
      return ER_PRM_CANNOT_CHANGE;
    }

  return NO_ERROR;
}

/*
 * sysprm_set_to_default -
 *   return: NO_ERROR or error code
 *   pname(in): parameter name to set to default value
 */
int
sysprm_set_to_default (const char *pname, bool set_to_force)
{
  SYSPRM_PARAM *prm;
  char val[LINE_MAX];

  if (pname == NULL)
    {
      return ER_PRM_BAD_VALUE;
    }

  prm = prm_find (pname, NULL);
  if (prm == NULL)
    {
      return ER_PRM_BAD_VALUE;
    }

  if (prm_set_default (prm) != NO_ERROR)
    {
      return ER_PRM_CANNOT_CHANGE;
    }

  if (set_to_force)
    {
      prm_print (prm, val, LINE_MAX, PRM_PRINT_NONE, PRM_PRINT_CURR_VAL);
      prm_set_force (prm, val);
    }

  return NO_ERROR;
}

/*
 * prm_keyword - Search a keyword within the keyword table
 *   return: NULL or found keyword
 *   val(in): keyword value
 *   name(in): keyword name
 *   tbl(in): keyword table
 *   dim(in): size of the table
 */
static const KEYVAL *
prm_keyword (int val, const char *name, const KEYVAL * tbl, int dim)
{
  int i;

  if (name != NULL)
    {
      for (i = 0; i < dim; i++)
	{
	  if (intl_mbs_casecmp (name, tbl[i].key) == 0)
	    {
	      return &tbl[i];
	    }
	}
    }
  else
    {
      for (i = 0; i < dim; i++)
	{
	  if (tbl[i].val == val)
	    {
	      return &tbl[i];
	    }
	}
    }

  return NULL;
}

/*
 * prm_report_bad_entry -
 *   return:
 *   line(in):
 *   err(in):
 *   where(in):
 */
static void
prm_report_bad_entry (const char *key, int line, int err, const char *where)
{
  if (line > 0)
    {
      fprintf (stderr, PARAM_MSG_FMT (PRM_ERR_BAD_LINE), key, line, where);
    }
  else if (line == 0)
    {
      fprintf (stderr, PARAM_MSG_FMT (PRM_ERR_BAD_PARAM), key, line, where);
    }
  else
    {
      fprintf (stderr, PARAM_MSG_FMT (PRM_ERR_BAD_ENV_VAR), where);
    }

  if (err > 0)
    {
      switch (err)
	{
	case PRM_ERR_DEPRICATED:
	case PRM_ERR_NO_MEM_FOR_PRM:
	  fprintf (stderr, "%s\n", PARAM_MSG_FMT (err));
	  break;

	case PRM_ERR_UNKNOWN_PARAM:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_UNKNOWN_SYSPRM, 3,
		  key, line, where);
	  fprintf (stderr, "%s\n", PARAM_MSG_FMT (err));
	  break;

	case PRM_ERR_BAD_VALUE:
	case PRM_ERR_BAD_STRING:
	case PRM_ERR_BAD_RANGE:
	case PRM_ERR_RESET_BAD_RANGE:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, key);
	  fprintf (stderr, "%s\n", PARAM_MSG_FMT (err));
	  break;

	default:
	  break;
	}
    }
  else
    {
      fprintf (stderr, PARAM_MSG_FMT (PRM_ERR_UNIX_ERROR), strerror (err));
    }

  fflush (stderr);
}


/*
 * sysprm_final - Clean up the storage allocated during parameter parsing
 *   return: none
 */
void
sysprm_final (void)
{
  SYSPRM_PARAM *prm;
  char **valp;
  int i;

  for (i = 0; i < NUM_PRM; i++)
    {
      prm = &prm_Def[i];
      if (PRM_IS_ALLOCATED (*prm->dynamic_flag) && PRM_IS_STRING (prm))
	{
	  char *str = PRM_GET_STRING (prm->value);

	  free_and_init (str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, *prm->dynamic_flag);

	  valp = (char **) prm->value;
	  *valp = NULL;
	}

      /* reset all dynamic flags */
      *prm->dynamic_flag = 0;
    }

  for (i = 0; i < MAX_NUM_OF_PRM_FILES_LOADED; i++)
    {
      if (prm_Files_loaded[i].conf_path != NULL)
	{
	  free_and_init (prm_Files_loaded[i].conf_path);
	}
      if (prm_Files_loaded[i].db_name != NULL)
	{
	  free_and_init (prm_Files_loaded[i].db_name);
	}
    }
}


/*
 * prm_tune_parameters - Sets the values of various system parameters
 *                       depending on the value of other parameters
 *   return: none
 *
 * Note: Used for providing a mechanism for tuning various system parameters.
 *       The parameters are only tuned if the user has not set them
 *       explictly, this can be ascertained by checking if the default
 *       value has been used.
 */
#if defined (SA_MODE) || defined (SERVER_MODE)
static void
prm_tune_parameters (void)
{
  SYSPRM_PARAM *max_clients_prm;
  SYSPRM_PARAM *max_scanid_bit_prm;
  SYSPRM_PARAM *max_plan_cache_entries_prm;
#if defined (ENABLE_UNUSED_FUNCTION)
  SYSPRM_PARAM *max_plan_cache_clones_prm;
#endif /* ENABLE_UNUSED_FUNCTION */
  SYSPRM_PARAM *query_cache_mode_prm;
  SYSPRM_PARAM *max_query_cache_entries_prm;
  SYSPRM_PARAM *query_cache_size_in_pages_prm;
  SYSPRM_PARAM *ha_mode_prm;
  SYSPRM_PARAM *ha_server_state_prm;
  SYSPRM_PARAM *auto_restart_server_prm;
  SYSPRM_PARAM *log_background_archiving_prm;
  SYSPRM_PARAM *ha_node_list_prm;
  SYSPRM_PARAM *max_log_archives_prm;
  SYSPRM_PARAM *force_remove_log_archives_prm;
  SYSPRM_PARAM *call_stack_dump_activation_prm;

  char newval[LINE_MAX];
  char host_name[MAXHOSTNAMELEN];
  int max_clients;

  /* Find the parameters that require tuning */
  max_clients_prm = prm_find (PRM_NAME_CSS_MAX_CLIENTS, NULL);
  max_scanid_bit_prm = prm_find (PRM_NAME_LK_MAX_SCANID_BIT, NULL);
  max_plan_cache_entries_prm =
    prm_find (PRM_NAME_XASL_MAX_PLAN_CACHE_ENTRIES, NULL);
#if defined (ENABLE_UNUSED_FUNCTION)
  max_plan_cache_clones_prm =
    prm_find (PRM_NAME_XASL_MAX_PLAN_CACHE_CLONES, NULL);
#endif /* ENABLE_UNUSED_FUNCTION */
  query_cache_mode_prm = prm_find (PRM_NAME_LIST_QUERY_CACHE_MODE, NULL);
  max_query_cache_entries_prm =
    prm_find (PRM_NAME_LIST_MAX_QUERY_CACHE_ENTRIES, NULL);
  query_cache_size_in_pages_prm =
    prm_find (PRM_NAME_LIST_MAX_QUERY_CACHE_PAGES, NULL);

  /* temporarily modifies the query result cache feature to be disabled
   * in RB-8.2.2. because it is not verified on 64 bit environment.
   */
  if (query_cache_mode_prm != NULL)
    {
      prm_set (query_cache_mode_prm, "0", false);
    }

  ha_mode_prm = prm_find (PRM_NAME_HA_MODE, NULL);
  ha_server_state_prm = prm_find (PRM_NAME_HA_SERVER_STATE, NULL);
  auto_restart_server_prm = prm_find (PRM_NAME_AUTO_RESTART_SERVER, NULL);
  log_background_archiving_prm =
    prm_find (PRM_NAME_LOG_BACKGROUND_ARCHIVING, NULL);
  ha_node_list_prm = prm_find (PRM_NAME_HA_NODE_LIST, NULL);
  max_log_archives_prm = prm_find (PRM_NAME_LOG_MAX_ARCHIVES, NULL);
  force_remove_log_archives_prm =
    prm_find (PRM_NAME_FORCE_REMOVE_LOG_ARCHIVES, NULL);

  /* Check that max clients has been set */
  assert (max_clients_prm != NULL);
  if (max_clients_prm == NULL)
    {
      return;
    }

#if 0
  if (!(PRM_IS_SET (*max_clients_prm->dynamic_flag)))
    {
      if (prm_set_default (max_clients_prm) != PRM_ERR_NO_ERROR)
	{
	  fprintf (stderr,
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_PARAMETERS,
				   PRM_ERR_NO_VALUE), max_clients_prm->name);
	  assert (0);
	  return;
	}
    }
  else
#endif
    {
      max_clients =
	MIN (prm_css_max_clients_upper, css_get_max_socket_fds ());
      if (PRM_GET_INT (max_clients_prm->value) > max_clients)
	{
	  sprintf (newval, "%d", max_clients);
	  (void) prm_set (max_clients_prm, newval, false);
	}
    }

#if defined (SERVER_MODE)
  assert (max_scanid_bit_prm != NULL);
  if (max_scanid_bit_prm == NULL)
    {
      return;
    }

  if (PRM_GET_INT (max_scanid_bit_prm->value) % 32)
    {
      sprintf (newval, "%d",
	       ((PRM_GET_INT (max_scanid_bit_prm->value)) +
		32 - (PRM_GET_INT (max_scanid_bit_prm->value)) % 32));
      (void) prm_set (max_scanid_bit_prm, newval, false);
    }
#endif /* SERVER_MODE */

  /* check Plan Cache and Query Cache parameters */
  assert (max_plan_cache_entries_prm != NULL);
  assert (query_cache_mode_prm != NULL);
  assert (max_query_cache_entries_prm != NULL);
  assert (query_cache_size_in_pages_prm != NULL);
#if defined (ENABLE_UNUSED_FUNCTION)
  assert (max_plan_cache_clones_prm != NULL);
#endif /* ENABLE_UNUSED_FUNCTION */

#if defined (ENABLE_UNUSED_FUNCTION)
  if (max_plan_cache_entries_prm == NULL || max_plan_cache_clones_prm == NULL
      || query_cache_mode_prm == NULL || max_query_cache_entries_prm == NULL)
    {
      return;
    }
#else /* ENABLE_UNUSED_FUNCTION */
  if (max_plan_cache_entries_prm == NULL
      || query_cache_mode_prm == NULL || max_query_cache_entries_prm == NULL)
    {
      return;
    }
#endif /* !ENABLE_UNUSED_FUNCTION */

  if (PRM_GET_INT (max_plan_cache_entries_prm->value) == 0)
    {
      /* 0 means disable plan cache */
      (void) prm_set (max_plan_cache_entries_prm, "-1", false);
    }

  if (PRM_GET_INT (max_plan_cache_entries_prm->value) <= 0)
    {
      /* disable all by default */
#if defined (ENABLE_UNUSED_FUNCTION)
      (void) prm_set_default (max_plan_cache_clones_prm);
#endif /* ENABLE_UNUSED_FUNCTION */
      (void) prm_set_default (query_cache_mode_prm);
      (void) prm_set_default (max_query_cache_entries_prm);
      (void) prm_set_default (query_cache_size_in_pages_prm);
    }
  else
    {
#if defined (ENABLE_UNUSED_FUNCTION)
#if 1				/* block XASL clone feature because of new heap layer */
      if (PRM_GET_INT (max_plan_cache_clones_prm->value) >=
	  PRM_GET_INT (max_plan_cache_entries_prm->value))
#else
      if ((PRM_GET_INT (max_plan_cache_clones_prm->value) >=
	   PRM_GET_INT (max_plan_cache_entries_prm->value))
	  || (PRM_GET_INT (max_plan_cache_clones_prm->value) < 0))
#endif
	{
	  sprintf (newval, "%d",
		   PRM_GET_INT (max_plan_cache_entries_prm->value));
	  (void) prm_set (max_plan_cache_clones_prm, newval, false);
	}
#endif /* ENABLE_UNUSED_FUNCTION */
      if (PRM_GET_INT (query_cache_mode_prm->value) == 0)
	{
	  (void) prm_set_default (max_query_cache_entries_prm);
	  (void) prm_set_default (query_cache_size_in_pages_prm);
	}
      else
	{
	  if (PRM_GET_INT (max_query_cache_entries_prm->value) <= 0)
	    {
	      sprintf (newval, "%d",
		       PRM_GET_INT (max_plan_cache_entries_prm->value));
	      (void) prm_set (max_query_cache_entries_prm, newval, false);
	    }
	}
    }

  /* check HA related parameters */
  assert (ha_mode_prm != NULL);
  assert (auto_restart_server_prm != NULL);
  if (ha_mode_prm == NULL || ha_server_state_prm == NULL
      || auto_restart_server_prm == NULL)
    {
      return;
    }

#if defined (SA_MODE) || defined (WINDOWS)
  /* we should save original PRM_HA_MODE value before tuning */
  PRM_HA_MODE_FOR_SA_UTILS_ONLY = PRM_HA_MODE;

  /* reset to default 'active mode' */
  (void) prm_set_default (ha_mode_prm);
  (void) prm_set (ha_server_state_prm, HA_SERVER_STATE_ACTIVE_STR, false);

  if (force_remove_log_archives_prm != NULL
      && !PRM_GET_BOOL (force_remove_log_archives_prm->value))
    {
      (void) prm_set_default (max_log_archives_prm);
    }
#else /* !SERVER_MODE */
  if (PRM_GET_INT (ha_mode_prm->value) != HA_MODE_OFF)
    {
      if (PRM_DEFAULT_VAL_USED (*ha_server_state_prm->dynamic_flag))
	{
	  sprintf (newval, "%s", HA_SERVER_STATE_STANDBY_STR);
	  prm_set (ha_server_state_prm, newval, false);
	}
      prm_set (auto_restart_server_prm, "no", false);

      if (PRM_GET_INT (ha_mode_prm->value) == HA_MODE_REPLICA)
	{
	  prm_set (force_remove_log_archives_prm, "yes", false);
	}
    }
  else
    {
      sprintf (newval, "%s", HA_SERVER_STATE_ACTIVE_STR);
      prm_set (ha_server_state_prm, newval, false);

      if (force_remove_log_archives_prm != NULL
	  && !PRM_GET_BOOL (force_remove_log_archives_prm->value))
	{
	  (void) prm_set_default (max_log_archives_prm);
	}
    }
#endif /* SERVER_MODE */

  if (ha_node_list_prm == NULL
      || PRM_DEFAULT_VAL_USED (*ha_node_list_prm->dynamic_flag))
    {
      if (GETHOSTNAME (host_name, sizeof (host_name)))
	{
	  strncpy (host_name, "localhost", sizeof (host_name) - 1);
	}

      snprintf (newval, sizeof (newval) - 1, "%s@%s", host_name, host_name);
      prm_set (ha_node_list_prm, newval, false);
    }

  call_stack_dump_activation_prm =
    GET_PRM (PRM_ID_CALL_STACK_DUMP_ACTIVATION);
  if (!PRM_IS_SET (*call_stack_dump_activation_prm->dynamic_flag))
    {
      int dim;
      int *integer_list = NULL;

      if (PRM_IS_ALLOCATED (*call_stack_dump_activation_prm->dynamic_flag))
	{
	  free_and_init (PRM_GET_INTEGER_LIST
			 (call_stack_dump_activation_prm->value));
	}

      dim = DIM (call_stack_dump_error_codes);
      integer_list = (int *) malloc ((dim + 1) * sizeof (int));
      if (integer_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, (dim + 1) * sizeof (int));
	  return;
	}

      integer_list[0] = dim;
      memcpy (&integer_list[1], call_stack_dump_error_codes,
	      dim * sizeof (int));
      prm_set_integer_list_value (PRM_ID_CALL_STACK_DUMP_ACTIVATION,
				  integer_list);
      PRM_SET_BIT (PRM_SET, *call_stack_dump_activation_prm->dynamic_flag);
      PRM_CLEAR_BIT (PRM_DEFAULT_USED,
		     *call_stack_dump_activation_prm->dynamic_flag);
    }

  return;
}
#else /* SA_MODE || SERVER_MODE */
static void
prm_tune_parameters (void)
{
  SYSPRM_PARAM *max_plan_cache_entries_prm;
  SYSPRM_PARAM *ha_node_list_prm;
  SYSPRM_PARAM *ha_mode_prm;
  SYSPRM_PARAM *ha_process_dereg_confirm_interval_in_msecs_prm;
  SYSPRM_PARAM *ha_max_process_dereg_confirm_prm;
  SYSPRM_PARAM *shutdown_wait_time_in_secs_prm;
  SYSPRM_PARAM *ha_copy_log_timeout_prm;
  SYSPRM_PARAM *ha_check_disk_failure_interval_prm;

  char newval[LINE_MAX];
  char host_name[MAXHOSTNAMELEN];

  /* Find the parameters that require tuning */
  max_plan_cache_entries_prm =
    prm_find (PRM_NAME_XASL_MAX_PLAN_CACHE_ENTRIES, NULL);
  ha_node_list_prm = prm_find (PRM_NAME_HA_NODE_LIST, NULL);

  ha_mode_prm = prm_find (PRM_NAME_HA_MODE, NULL);
  ha_process_dereg_confirm_interval_in_msecs_prm =
    prm_find (PRM_NAME_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS, NULL);
  ha_max_process_dereg_confirm_prm =
    prm_find (PRM_NAME_HA_MAX_PROCESS_DEREG_CONFIRM, NULL);
  shutdown_wait_time_in_secs_prm =
    prm_find (PRM_NAME_SHUTDOWN_WAIT_TIME_IN_SECS, NULL);
  ha_copy_log_timeout_prm = prm_find (PRM_NAME_HA_COPY_LOG_TIMEOUT, NULL);
  ha_check_disk_failure_interval_prm =
    prm_find (PRM_NAME_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS, NULL);

  assert (max_plan_cache_entries_prm != NULL);
  if (max_plan_cache_entries_prm == NULL)
    {
      return;
    }

  /* check Plan Cache and Query Cache parameters */
  if (PRM_GET_INT (max_plan_cache_entries_prm->value) == 0)
    {
      /* 0 means disable plan cache */
      (void) prm_set (max_plan_cache_entries_prm, "-1", false);
    }

#if defined (WINDOWS)
  /* reset to default 'active mode' */
  (void) prm_set_default (ha_mode_prm);
#endif

  if (PRM_GET_INT (ha_mode_prm->value) != HA_MODE_OFF)
    {
      int ha_check_disk_failure_interval_value;
      int ha_copy_log_timeout_value;

      ha_check_disk_failure_interval_value =
	PRM_GET_INT (ha_check_disk_failure_interval_prm->value);
      ha_copy_log_timeout_value =
	PRM_GET_INT (ha_copy_log_timeout_prm->value);
      if (ha_copy_log_timeout_value == -1)
	{
	  prm_set (ha_check_disk_failure_interval_prm, "0", false);
	}
      else if (ha_check_disk_failure_interval_value -
	       ha_copy_log_timeout_value <
	       HB_MIN_DIFF_CHECK_DISK_FAILURE_INTERVAL_IN_SECS)
	{
	  ha_check_disk_failure_interval_value =
	    ha_copy_log_timeout_value +
	    HB_MIN_DIFF_CHECK_DISK_FAILURE_INTERVAL_IN_SECS;
	  sprintf (newval, "%ds", ha_check_disk_failure_interval_value);
	  prm_set (ha_check_disk_failure_interval_prm, newval, false);
	}
    }

  if (ha_node_list_prm == NULL
      || PRM_DEFAULT_VAL_USED (*ha_node_list_prm->dynamic_flag))
    {
      if (GETHOSTNAME (host_name, sizeof (host_name)))
	{
	  strncpy (host_name, "localhost", sizeof (host_name) - 1);
	}

      snprintf (newval, sizeof (newval) - 1, "%s@%s", host_name, host_name);
      prm_set (ha_node_list_prm, newval, false);
    }

  assert (ha_mode_prm != NULL);

  return;
}
#endif /* CS_MODE */

#if defined (CS_MODE)
/*
 * sysprm_tune_client_parameters () - Synchronize system parameters marked
 *				      with PRM_FORCE_SERVER flag with server.
 *
 * return : void.
 */
void
sysprm_tune_client_parameters (void)
{
  SYSPRM_ASSIGN_VALUE *force_server_values = NULL;

  /* get values from server */
  if (sysprm_get_force_server_parameters (&force_server_values) == NO_ERROR
      && force_server_values != NULL)
    {
      /* update system parameters on client */
      sysprm_change_parameter_values (force_server_values, false, true);
    }

  /* free list of assign_values */
  sysprm_free_assign_values (&force_server_values);
}
#endif /* CS_MODE */

int
prm_get_master_port_id (void)
{
  return PRM_TCP_PORT_ID;
}

bool
prm_get_commit_on_shutdown (void)
{
  return PRM_COMMIT_ON_SHUTDOWN;
}

bool
prm_get_query_mode_sync (void)
{
  return PRM_QUERY_MODE_SYNC;
}

/*
 * prm_set_compound - Sets the values of various system parameters based on
 *                    the value of a "compound" parameter.
 *   return: none
 *   param(in): the compound parameter whose value has changed
 *   compound_param_values(in): an array of arrays that indicate the name of
 *                              the parameter to change and its new value.
 *                              NULL indicates resetting the parameter to its
 *                              default. The name of the parameter is the last
 *                              element of the array, 1 past the upper limit
 *                              of the compound parameter.
 *   values_count(in): the number of elements in the compound_param_values
 *                     array
 */
static void
prm_set_compound (SYSPRM_PARAM * param, const char **compound_param_values[],
		  const int values_count, bool set_flag)
{
  int i = 0;
  const int param_value = *(int *) param->value;
  const int param_upper_limit = *(int *) param->upper_limit;

  assert (PRM_IS_INTEGER (param) || PRM_IS_KEYWORD (param));
  assert (0 == *(int *) param->lower_limit);
  assert (param_value <= param_upper_limit);

  for (i = 0; i < values_count; ++i)
    {
      const char *compound_param_name =
	compound_param_values[i][param_upper_limit + 1];
      const char *compound_param_value =
	compound_param_values[i][param_value];

      assert (compound_param_name != NULL);

      if (compound_param_value == NULL)
	{
	  prm_set_default (prm_find (compound_param_name, NULL));
	}
      else
	{
	  prm_set (prm_find (compound_param_name, NULL),
		   compound_param_value, set_flag);
	}
    }
}

/*
 * prm_get_next_param_value - get next param=value token from a string
 *			      containing a "param1=val1;param2=val2..." list
 *   return: NO_ERROR or error code if data format is incorrect
 *   data (in): the string containing the list
 *   prm (out): parameter name
 *   val (out): parameter value
 */
static int
prm_get_next_param_value (char **data, char **prm, char **val)
{
  char *p = *data;
  char *name = NULL;
  char *value = NULL;
  int err = PRM_ERR_NO_ERROR;

  while (char_isspace (*p))
    {
      p++;
    }

  if (*p == '\0')
    {
      /* reached the end of the list */
      err = PRM_ERR_NO_ERROR;
      goto cleanup;
    }

  name = p;
  while (*p && !char_isspace (*p) && *p != '=')
    {
      p++;
    }

  if (*p == '\0')
    {
      err = PRM_ERR_BAD_VALUE;
      goto cleanup;
    }
  else if (*p == '=')
    {
      *p++ = '\0';
    }
  else
    {
      *p++ = '\0';
      while (char_isspace (*p))
	{
	  p++;
	}
      if (*p == '=')
	{
	  p++;
	}
    }

  while (char_isspace (*p))
    {
      p++;
    }
  if (*p == '\0')
    {
      err = PRM_ERR_NO_ERROR;
      goto cleanup;
    }

  value = p;

  if (*p == '"' || *p == '\'')
    {
      char *t, delim;

      delim = *p++;
      value = t = p;
      while (*t && *t != delim)
	{
	  if (*t == '\\')
	    {
	      t++;
	    }
	  *p++ = *t++;
	}
      if (*t != delim)
	{
	  err = PRM_ERR_BAD_STRING;
	  goto cleanup;
	}
    }
  else
    {
      while (*p && !char_isspace (*p) && *p != ';')
	{
	  p++;
	}
    }

  if (*p)
    {
      *p++ = '\0';
      while (char_isspace (*p))
	{
	  p++;
	}
      if (*p == ';')
	{
	  p++;
	}
    }

  *data = p;
  *val = value;
  *prm = name;

  return err;

cleanup:
  *prm = NULL;
  *val = NULL;
  *data = NULL;

  return err;
}

/*
 * prm_get_name () - returns the name of a parameter
 *
 * return      : parameter name
 * prm_id (in) : parameter id
 */
const char *
prm_get_name (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);

  return prm_Def[prm_id].name;
}

/*
 * prm_get_value () - returns a pointer to the value of a system parameter
 *
 * return      : pointer to value
 * prm_id (in) : parameter id
 *
 * NOTE: for session parameters, in server mode, the value stored in
 *	 conn_entry->session_parameters is returned instead of the value
 *	 from prm_Def array.
 */
void *
prm_get_value (PARAM_ID prm_id)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *thread_p;

  assert (prm_id <= PRM_LAST_ID);

  if (PRM_SERVER_SESSION (prm_id) && BO_IS_SERVER_RESTARTED ())
    {
      SESSION_PARAM *sprm;
      thread_p = thread_get_thread_entry_info ();
      sprm = session_get_session_parameter (thread_p, prm_id);
      if (sprm)
	{
	  return &(sprm->value);
	}
    }

  return prm_Def[prm_id].value;
#else /* SERVER_MODE */
  assert (prm_id <= PRM_LAST_ID);

  return prm_Def[prm_id].value;
#endif /* SERVER_MODE */
}

/*
 * prm_get_integer_value () - get the value of a parameter of type integer
 *
 * return      : value
 * prm_id (in) : parameter id
 *
 * NOTE: keywords are stored as integers
 */
int
prm_get_integer_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_INTEGER (&prm_Def[prm_id])
	  || PRM_IS_KEYWORD (&prm_Def[prm_id]));

  return PRM_GET_INT (prm_get_value (prm_id));
}

/*
 * prm_get_bool_value () - get the value of a parameter of type bool
 *
 * return      : value
 * prm_id (in) : parameter id
 */
bool
prm_get_bool_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_BOOLEAN (&prm_Def[prm_id]));

  return PRM_GET_BOOL (prm_get_value (prm_id));
}

/*
 * prm_get_float_value () - get the value of a parameter of type float
 *
 * return      : value
 * prm_id (in) : parameter id
 */
float
prm_get_float_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_FLOAT (&prm_Def[prm_id]));

  return PRM_GET_FLOAT (prm_get_value (prm_id));
}

/*
 * prm_get_string_value () - get the value of a parameter of type string
 *
 * return      : value
 * prm_id (in) : parameter id
 */
char *
prm_get_string_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_STRING (&prm_Def[prm_id]));

  return PRM_GET_STRING (prm_get_value (prm_id));
}

/*
 * prm_get_integer_list_value () - get the value of a parameter of type
 *				   integer list
 *
 * return      : value
 * prm_id (in) : parameter id
 */
int *
prm_get_integer_list_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_INTEGER_LIST (&prm_Def[prm_id]));

  return PRM_GET_INTEGER_LIST (prm_get_value (prm_id));
}

/*
 * prm_get_bigint_value () - get the value of a parameter of type size
 *
 * return      : value
 * prm_id (in) : parameter id
 */
UINT64
prm_get_bigint_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_BIGINT (&prm_Def[prm_id]));

  return PRM_GET_BIGINT (prm_get_value (prm_id));
}

/*
 * prm_set_integer_value () - set a new value to a parameter of type integer
 *
 * return      : void
 * prm_id (in) : parameter id
 * value (in)  : new value
 *
 * NOTE: keywords are stored as integers
 */
void
prm_set_integer_value (PARAM_ID prm_id, int value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_INTEGER (&prm_Def[prm_id])
	  || PRM_IS_KEYWORD (&prm_Def[prm_id]));

  PRM_GET_INT (prm_Def[prm_id].value) = value;

  sysprm_update_flag_different (&prm_Def[prm_id]);
}

/*
 * prm_set_bool_value () - set a new value to a parameter of type bool
 *
 * return      : void
 * prm_id (in) : parameter id
 * value (in)  : new value
 */
void
prm_set_bool_value (PARAM_ID prm_id, bool value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_BOOLEAN (&prm_Def[prm_id]));

  PRM_GET_BOOL (prm_Def[prm_id].value) = value;

  sysprm_update_flag_different (&prm_Def[prm_id]);
}

/*
 * prm_set_float_value () - set a new value to a parameter of type float
 *
 * return      : void
 * prm_id (in) : parameter id
 * value (in)  : new value
 */
void
prm_set_float_value (PARAM_ID prm_id, float value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_FLOAT (&prm_Def[prm_id]));

  PRM_GET_FLOAT (prm_Def[prm_id].value) = value;

  sysprm_update_flag_different (&prm_Def[prm_id]);
}

/*
 * prm_set_string_value () - set a new value to a parameter of type string
 *
 * return      : void
 * prm_id (in) : parameter id
 * value (in)  : new value
 */
void
prm_set_string_value (PARAM_ID prm_id, char *value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_STRING (&prm_Def[prm_id]));

  if (PRM_IS_ALLOCATED (*prm_Def[prm_id].dynamic_flag))
    {
      free_and_init (PRM_GET_STRING (prm_Def[prm_id].value));
      PRM_CLEAR_BIT (PRM_ALLOCATED, *prm_Def[prm_id].dynamic_flag);
    }
  PRM_GET_STRING (prm_Def[prm_id].value) = value;
  if (PRM_GET_STRING (prm_Def[prm_id].value) != NULL)
    {
      PRM_SET_BIT (PRM_ALLOCATED, *prm_Def[prm_id].dynamic_flag);
    }

  sysprm_update_flag_different (&prm_Def[prm_id]);
}

/*
 * prm_set_integer_list_value () - set a new value to a parameter of type
 *				   integer list
 *
 * return      : void
 * prm_id (in) : parameter id
 * value (in)  : new value
 */
void
prm_set_integer_list_value (PARAM_ID prm_id, int *value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_INTEGER_LIST (&prm_Def[prm_id]));

  if (PRM_IS_ALLOCATED (*prm_Def[prm_id].dynamic_flag))
    {
      free_and_init (PRM_GET_INTEGER_LIST (prm_Def[prm_id].value));
      PRM_CLEAR_BIT (PRM_ALLOCATED, *prm_Def[prm_id].dynamic_flag);
    }
  PRM_GET_INTEGER_LIST (prm_Def[prm_id].value) = value;
  if (PRM_GET_INTEGER_LIST (prm_Def[prm_id].value) != NULL)
    {
      PRM_SET_BIT (PRM_ALLOCATED, *prm_Def[prm_id].dynamic_flag);
    }

  sysprm_update_flag_different (&prm_Def[prm_id]);
}

/*
 * prm_set_bigint_value () - set a new value to a parameter of type size
 *
 * return      : void
 * prm_id (in) : parameter id
 * value (in)  : new value
 */
void
prm_set_bigint_value (PARAM_ID prm_id, UINT64 value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_BIGINT (&prm_Def[prm_id]));

  PRM_GET_BIGINT (prm_Def[prm_id].value) = value;

  sysprm_update_flag_different (&prm_Def[prm_id]);
}

/*
 * sysprm_find_err_in_integer_list () - function that searches a error_code in an
 *				     integer list
 *
 * return      : true if error_code is found, false otherwise
 * prm_id (in) : id of the system parameter that contains an integer list
 * error_code (in)  : error_code to look for
 */
bool
sysprm_find_err_in_integer_list (PARAM_ID prm_id, int error_code)
{
  int i;
  int *integer_list = prm_get_integer_list_value (prm_id);

  if (integer_list == NULL)
    {
      return false;
    }

  for (i = 1; i <= integer_list[0]; i++)
    {
      if (integer_list[i] == error_code || integer_list[i] == -error_code)
	{
	  return true;
	}
    }
  return false;
}

/*
 * sysprm_update_flag_different () - updates the PRM_DIFFERENT bit in flag.
 *
 * return   : void
 * prm (in) : system parameter to update
 *
 * NOTE: Should be called whenever a system parameter changes value.
 *	 This only affects parameters on client (there is no use for this flag
 *	 on server side) that are also flagged with PRM_FOR_QRY_STRING.
 */
static void
sysprm_update_flag_different (SYSPRM_PARAM * prm)
{
#if defined (CS_MODE)
  if (!PRM_IS_FOR_QRY_STRING (prm->static_flag)
      && !PRM_IS_FOR_HA_CONTEXT (prm->static_flag))
    {
      /* nothing to do */
      return;
    }

  /* compare current value with default value and update PRM_DIFFERENT bit */
  if (sysprm_compare_values (prm->value, prm->default_value, prm->datatype) !=
      0)
    {
      PRM_SET_BIT (PRM_DIFFERENT, *prm->dynamic_flag);
    }
  else
    {
      PRM_CLEAR_BIT (PRM_DIFFERENT, *prm->dynamic_flag);
    }
#endif /* CS_MODE */
}

/*
 * sysprm_update_flag_allocated () - update PRM_ALLOCATED flag. If value is
 *				     NULL, the bit is cleared, if not, the bit
 *				     is set.
 *
 * return   : void.
 * prm (in) : system parameter.
 */
static void
sysprm_update_flag_allocated (SYSPRM_PARAM * prm)
{
  bool allocated;
  switch (prm->datatype)
    {
    case PRM_STRING:
      allocated = PRM_GET_STRING (prm->value) != NULL;
      break;

    case PRM_INTEGER_LIST:
      allocated = PRM_GET_INTEGER_LIST (prm->value) != NULL;
      break;

    default:
      allocated = false;
      break;
    }

  if (allocated)
    {
      PRM_SET_BIT (PRM_ALLOCATED, *prm->dynamic_flag);
    }
  else
    {
      PRM_CLEAR_BIT (PRM_ALLOCATED, *prm->dynamic_flag);
    }
}

/*
 * sysprm_update_session_prm_flag_allocated () - update PRM_ALLOCATED flag for
 *						 session parameters. If the
 *						 value is null, the bit is
 *						 cleared, if not, the bit is
 *						 set.
 *
 * return   : void
 * prm (in) : session parameter
 */
static void
sysprm_update_session_prm_flag_allocated (SESSION_PARAM * prm)
{
  bool allocated;
  switch (prm->datatype)
    {
    case PRM_STRING:
      allocated = prm->value.str != NULL;
      break;

    case PRM_INTEGER_LIST:
      allocated = prm->value.integer_list != NULL;
      break;

    default:
      allocated = false;
      break;
    }

  if (allocated)
    {
      PRM_SET_BIT (PRM_ALLOCATED, prm->flag);
    }
  else
    {
      PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
    }
}

/*
 * sysprm_clear_sysprm_value () - Clears a SYSPRM_VALUE.
 *
 * return	 : void.
 * value (in)	 : value that needs cleared.
 * datatype (in) : data type for value.
 */
static void
sysprm_clear_sysprm_value (SYSPRM_VALUE * value, SYSPRM_DATATYPE datatype)
{
  switch (datatype)
    {
    case PRM_STRING:
      free_and_init (value->str);
      break;

    case PRM_INTEGER_LIST:
      free_and_init (value->integer_list);
      break;

    default:
      /* do nothing */
      break;
    }
}

/*
 * sysprm_alloc_session_parameters () - allocates memory for session
 *					parameters array
 *
 * return : NULL or pointer to array of session parameters
 */
static SESSION_PARAM *
sysprm_alloc_session_parameters (void)
{
  SESSION_PARAM *result = NULL;
  int size;

  if (NUM_SESSION_PRM == 0)
    {
      return NULL;
    }
  size = NUM_SESSION_PRM * sizeof (SESSION_PARAM);
  result = (SESSION_PARAM *) malloc (size);
  if (result == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      size);
      return NULL;
    }
  memset (result, 0, size);
  return result;
}

/*
 * sysprm_free_session_parameters () - free session parameter array
 *
 * return		       : void
 * session_parameters_ptr (in) : pointer to session parameter array
 */
void
sysprm_free_session_parameters (SESSION_PARAM ** session_parameters_ptr)
{
  int i = 0;
  SESSION_PARAM *sprm = NULL;

  assert (session_parameters_ptr != NULL);

  if (*session_parameters_ptr == NULL)
    {
      return;
    }

  for (i = 0; i < NUM_SESSION_PRM; i++)
    {
      sprm = &((*session_parameters_ptr)[i]);
      sysprm_clear_sysprm_value (&sprm->value, sprm->datatype);
    }

  free_and_init (*session_parameters_ptr);
}

/*
 * sysprm_pack_sysprm_value () - Packs a sysprm_value.
 *
 * return	 : pointer after the packed value.
 * ptr (in)	 : pointer to position where the value should be packed.
 * value (in)	 : sysprm_value to be packed.
 * datatype (in) : value data type.
 */
static char *
sysprm_pack_sysprm_value (char *ptr, SYSPRM_VALUE value,
			  SYSPRM_DATATYPE datatype)
{
  if (ptr == NULL)
    {
      return NULL;
    }

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  switch (datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      ptr = or_pack_int (ptr, value.i);
      break;

    case PRM_BOOLEAN:
      ptr = or_pack_int (ptr, value.b);
      break;

    case PRM_FLOAT:
      ptr = or_pack_float (ptr, value.f);
      break;

    case PRM_STRING:
      ptr = or_pack_string (ptr, value.str);
      break;

    case PRM_INTEGER_LIST:
      if (value.integer_list != NULL)
	{
	  int i;
	  ptr = or_pack_int (ptr, value.integer_list[0]);
	  for (i = 1; i <= value.integer_list[0]; i++)
	    {
	      ptr = or_pack_int (ptr, value.integer_list[i]);
	    }
	}
      else
	{
	  ptr = or_pack_int (ptr, -1);
	}
      break;

    case PRM_BIGINT:
      ptr = or_pack_int64 (ptr, value.bi);
      break;
    default:
      assert_release (0);
      break;
    }

  return ptr;
}

/*
 * sysprm_packed_sysprm_value_length () - size of packed sysprm_value.
 *
 * return	 : size of packed sysprm_value.
 * value (in)	 : sysprm_value.
 * datatype (in) : value data type.
 * offset (in)	 : offset to pointer where sysprm_value will be packed
 *		   (required for PRM_BIGINT data type)
 */
static int
sysprm_packed_sysprm_value_length (SYSPRM_VALUE value,
				   SYSPRM_DATATYPE datatype, int offset)
{
  switch (datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
    case PRM_BOOLEAN:
      return OR_INT_SIZE;

    case PRM_FLOAT:
      return OR_FLOAT_SIZE;

    case PRM_STRING:
      return or_packed_string_length (value.str, NULL);

    case PRM_INTEGER_LIST:
      if (value.integer_list != NULL)
	{
	  return OR_INT_SIZE * (value.integer_list[0] + 1);
	}
      else
	{
	  return OR_INT_SIZE;
	}

    case PRM_BIGINT:
      /* pointer will be aligned to MAX_ALIGNMENT */
      return DB_ALIGN (offset, MAX_ALIGNMENT) - offset + OR_INT64_SIZE;

    default:
      return 0;
    }
}

/*
 * sysprm_unpack_sysprm_value () - unpacks a sysprm_value.
 *
 * return        : pointer after the unpacked sysprm_value.
 * ptr (in)      : pointer to the position where sysprm_value is packed.
 * value (out)   : pointer to unpacked sysprm_value.
 * datatype (in) : value data type.
 */
static char *
sysprm_unpack_sysprm_value (char *ptr, SYSPRM_VALUE * value,
			    SYSPRM_DATATYPE datatype)
{
  assert (value != NULL);

  if (ptr == NULL)
    {
      return NULL;
    }

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  switch (datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      ptr = or_unpack_int (ptr, &value->i);
      break;

    case PRM_BOOLEAN:
      {
	int temp;
	ptr = or_unpack_int (ptr, &temp);
	value->b = temp;
      }
      break;

    case PRM_FLOAT:
      ptr = or_unpack_float (ptr, &value->f);
      break;

    case PRM_STRING:
      {
	char *str = NULL;
	ptr = or_unpack_string_nocopy (ptr, &str);
	if (str != NULL)
	  {
	    value->str = strdup (str);
	    if (value->str == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (str) + 1);
		return NULL;
	      }
	  }
	else
	  {
	    value->str = NULL;
	  }
      }
      break;

    case PRM_INTEGER_LIST:
      {
	int temp, i;
	ptr = or_unpack_int (ptr, &temp);
	if (temp == -1)
	  {
	    value->integer_list = NULL;
	  }
	else
	  {
	    value->integer_list = (int *) malloc ((temp + 1) * OR_INT_SIZE);
	    if (value->integer_list == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1,
			(temp + 1) * OR_INT_SIZE);
		return NULL;
	      }
	    else
	      {
		value->integer_list[0] = temp;
		for (i = 1; i <= temp; i++)
		  {
		    ptr = or_unpack_int (ptr, &value->integer_list[i]);
		  }
	      }
	  }
      }
      break;

    case PRM_BIGINT:
      {
	INT64 size;
	ptr = or_unpack_int64 (ptr, &size);
	value->bi = (UINT64) size;
      }
      break;

    default:
      break;
    }

  return ptr;
}

/*
 * sysprm_pack_session_parameters () - packs the array of session parameters
 *
 * return		   : new position pointer after packing
 * ptr (in)		   : pointer to the position where the packing starts
 * session_parameters (in) : array of session parameters
 */
char *
sysprm_pack_session_parameters (char *ptr, SESSION_PARAM * session_parameters)
{
  SESSION_PARAM *prm = NULL;
  int i = 0;

  if (ptr == NULL)
    {
      return NULL;
    }

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  for (i = 0; i < NUM_SESSION_PRM; i++)
    {
      prm = &session_parameters[i];

      ptr = or_pack_int (ptr, prm->prm_id);
      ptr = or_pack_int (ptr, prm->flag);
      ptr = or_pack_int (ptr, prm->datatype);
      ptr = sysprm_pack_sysprm_value (ptr, prm->value, prm->datatype);
    }

  return ptr;
}

/*
 * sysprm_packed_session_parameters_length () - returns the length needed to
 *						pack an array of session
 *						parameters
 *
 * return		   : size of packed data
 * session_parameters (in) : array of session parameters
 * offset (in)		   : the offset to packed session parameters
 */
int
sysprm_packed_session_parameters_length (SESSION_PARAM * session_parameters,
					 int offset)
{
  SESSION_PARAM *prm = NULL;
  int size = 0, i = 0;

  for (i = 0; i < NUM_SESSION_PRM; i++)
    {
      prm = &session_parameters[i];
      size += OR_INT_SIZE;	/* prm_id */
      size += OR_INT_SIZE;	/* flag */
      size += OR_INT_SIZE;	/* datatype */
      size +=			/* value */
	sysprm_packed_sysprm_value_length (session_parameters[i].value,
					   prm->datatype, size + offset);
    }

  return size;
}

/*
 * sysprm_unpack_session_parameters () - unpacks an array of session parameters
 *					 from buffer
 *
 * return			: new pointer position after unpacking
 * ptr (in)			: pointer to position where unpacking starts
 * session_parameters_ptr (out) : pointer to the unpacked list of session
 *				  parameters
 */
char *
sysprm_unpack_session_parameters (char *ptr,
				  SESSION_PARAM ** session_parameters_ptr)
{
  SESSION_PARAM *prm, *session_params = NULL;
  int prm_index, tmp;

  assert (session_parameters_ptr != NULL);
  *session_parameters_ptr = NULL;

  if (ptr == NULL)
    {
      return NULL;
    }

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  session_params = sysprm_alloc_session_parameters ();
  if (session_params == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SESSION_PARAM));
      goto error;
    }

  for (prm_index = 0; prm_index < NUM_SESSION_PRM; prm_index++)
    {
      int flag;

      prm = &session_params[prm_index];

      ptr = or_unpack_int (ptr, &tmp);
      prm->prm_id = tmp;

      ptr = or_unpack_int (ptr, &flag);
      prm->flag = (unsigned int) flag;

      ptr = or_unpack_int (ptr, &prm->datatype);

      ptr = sysprm_unpack_sysprm_value (ptr, &prm->value, prm->datatype);
      if (ptr == NULL)
	{
	  /* error unpacking value */
	  goto error;
	}
      sysprm_update_session_prm_flag_allocated (prm);
    }

  *session_parameters_ptr = session_params;
  return ptr;

error:
  /* clean up */
  sysprm_free_session_parameters (&session_params);
  return NULL;
}

/*
 * sysprm_pack_assign_values () - packs a list of SYSPRM_ASSIGN_VALUEs.
 *
 * return	      : pointer after the packed list.
 * ptr (in)	      : pointer to position where the list should be packed.
 * assign_values (in) : list of sysprm_assign_values.
 */
char *
sysprm_pack_assign_values (char *ptr,
			   const SYSPRM_ASSIGN_VALUE * assign_values)
{
  char *old_ptr = ptr;
  int count;

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  /* skip one int -> size of assign_values */
  ptr += OR_INT_SIZE;

  for (count = 0; assign_values != NULL;
       assign_values = assign_values->next, count++)
    {
      ptr = or_pack_int (ptr, assign_values->prm_id);
      ptr =
	sysprm_pack_sysprm_value (ptr, assign_values->value,
				  GET_PRM_DATATYPE (assign_values->prm_id));
    }

  OR_PUT_INT (old_ptr, count);
  return ptr;
}

/*
 * sysprm_packed_assign_values_length () - size of packed list of
 *					   sysprm_assing_values.
 *
 * return	      : size of packed list of sysprm_assing_values.
 * assign_values (in) : list of sysprm_assing_values.
 * offset (in)	      : offset to pointer where assign values will be packed.
 */
int
sysprm_packed_assign_values_length (const SYSPRM_ASSIGN_VALUE * assign_values,
				    int offset)
{
  int size = 0;

  size += OR_INT_SIZE;		/* size of assign_values list */

  for (; assign_values != NULL; assign_values = assign_values->next)
    {
      size += OR_INT_SIZE;	/* prm_id */
      size +=
	sysprm_packed_sysprm_value_length (assign_values->value,
					   GET_PRM_DATATYPE (assign_values->
							     prm_id),
					   size + offset);
    }

  return size;
}

/*
 * sysprm_unpack_assign_values () - Unpacks a list of sysprm_assign_values.
 *
 * return		   : pointer after unpacking sysprm_assing_values.
 * ptr (in)		   : pointer to the position where
 *			     sysprm_assing_values are packed.
 * assign_values_ptr (out) : pointer to the unpacked list of
 *			     sysprm_assing_values.
 */
char *
sysprm_unpack_assign_values (char *ptr,
			     SYSPRM_ASSIGN_VALUE ** assign_values_ptr)
{
  SYSPRM_ASSIGN_VALUE *assign_values = NULL, *last_assign_val = NULL;
  SYSPRM_ASSIGN_VALUE *assign_val = NULL;
  int i = 0, count = 0, tmp;

  assert (assign_values_ptr != NULL);
  *assign_values_ptr = NULL;

  if (ptr == NULL)
    {
      return NULL;
    }

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  ptr = or_unpack_int (ptr, &count);
  if (count <= 0)
    {
      return ptr;
    }

  for (i = 0; i < count; i++)
    {
      assign_val =
	(SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
      if (assign_val == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (SYSPRM_ASSIGN_VALUE));
	  goto error;
	}

      ptr = or_unpack_int (ptr, &tmp);
      assign_val->prm_id = tmp;

      ptr = sysprm_unpack_sysprm_value (ptr, &assign_val->value,
					GET_PRM_DATATYPE (assign_val->
							  prm_id));
      if (ptr == NULL)
	{
	  free_and_init (assign_val);
	  goto error;
	}
      assign_val->next = NULL;
      if (assign_values != NULL)
	{
	  last_assign_val->next = assign_val;
	  last_assign_val = assign_val;
	}
      else
	{
	  assign_values = last_assign_val = assign_val;
	}
    }

  *assign_values_ptr = assign_values;
  return ptr;

error:
  sysprm_free_assign_values (&assign_values);
  return NULL;
}

/*
 * sysprm_free_assign_values () - free a list of sysprm_assign_values.
 *
 * return		      : void
 * assign_values_ptr (in/out) : pointer to list to free.
 */
void
sysprm_free_assign_values (SYSPRM_ASSIGN_VALUE ** assign_values_ptr)
{
  SYSPRM_ASSIGN_VALUE *assignment = NULL, *save_next = NULL;

  if (assign_values_ptr == NULL || *assign_values_ptr == NULL)
    {
      return;
    }
  assignment = *assign_values_ptr;

  while (assignment != NULL)
    {
      save_next = assignment->next;
      sysprm_clear_sysprm_value (&assignment->value,
				 GET_PRM_DATATYPE (assignment->prm_id));
      free_and_init (assignment);
      assignment = save_next;
    }

  *assign_values_ptr = NULL;
}

/*
 * sysprm_get_id () - returns the id for a system parameter
 *
 * return  : id
 * prm(in) : address for system parameter
 */
static PARAM_ID
sysprm_get_id (const SYSPRM_PARAM * prm)
{
  int id = (prm - prm_Def);

  assert (id >= PRM_FIRST_ID && id <= PRM_LAST_ID);

  return (PARAM_ID) id;
}

#if defined (SERVER_MODE)
/*
 * sysprm_session_init_session_parameters () - adds array of session
 *					       parameters to session state and
 *					       connection entry.
 *
 * return                          : NO_ERROR or error_code
 * session_parameters_ptr (in/out) : array of session parameters sent by
 *				     client while registering to server
 * found_session_parameters (out)  : true if session parameters were found in
 *				     session state, false otherwise
 *
 * NOTE: If the session state already has session parameters, the client must
 *	 have reconnected. The values stored in session state will be used
 *	 instead of the values sent by client (this way the client recovers
 *	 the session parameters changed before disconnecting).
 */
int
sysprm_session_init_session_parameters (SESSION_PARAM **
					session_parameters_ptr,
					int *found_session_parameters)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  int error_code = NO_ERROR;
  SESSION_PARAM *session_params = NULL, *prm = NULL;

  assert (found_session_parameters != NULL);
  *found_session_parameters = 0;

  /* first check if there is a list in session state */
  error_code = session_get_session_parameters (thread_p, &session_params);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (session_params == NULL)
    {
      /* set the parameter values sent by client to session state */
      session_params = *session_parameters_ptr;
      error_code = session_set_session_parameters (thread_p, session_params);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  else
    {
      /* use the values stored in session state and free the session
       * parameters received from client
       */
      sysprm_free_session_parameters (session_parameters_ptr);
      *session_parameters_ptr = session_params;
      *found_session_parameters = 1;
    }

  return NO_ERROR;
}

/*
 * sysprm_set_session_parameter_value - set a new value for the session
 *					parameter identified by id.
 * return : PRM_ERR_NO_ERROR or error_code
 * session_parameter (in)  : session parameter to set the value of
 * id (in)		   : id for the session parameter that needs changed
 * value (in)		   : new value
 */
static SYSPRM_ERR
sysprm_set_session_parameter_value (SESSION_PARAM * session_parameter, int id,
				    SYSPRM_VALUE value)
{
  char *end = NULL;
  SYSPRM_PARAM *prm = &prm_Def[id];

  switch (session_parameter->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      session_parameter->value.i = value.i;
      break;

    case PRM_FLOAT:
      session_parameter->value.f = value.f;
      break;

    case PRM_BOOLEAN:
      session_parameter->value.b = value.b;
      break;

    case PRM_STRING:
      if (PRM_IS_ALLOCATED (session_parameter->flag))
	{
	  free_and_init (session_parameter->value.str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, session_parameter->flag);
	}
      session_parameter->value.str = value.str;
      if (session_parameter->value.str != NULL)
	{
	  PRM_SET_BIT (PRM_ALLOCATED, session_parameter->flag);
	}
      break;

    case PRM_INTEGER_LIST:
      if (PRM_IS_ALLOCATED (session_parameter->flag))
	{
	  free_and_init (session_parameter->value.integer_list);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, session_parameter->flag);
	}
      session_parameter->value.integer_list = value.integer_list;
      if (session_parameter->value.integer_list != NULL)
	{
	  PRM_SET_BIT (PRM_ALLOCATED, session_parameter->flag);
	}
      break;

    case PRM_BIGINT:
      session_parameter->value.bi = value.bi;
      break;
    }

  return PRM_ERR_NO_ERROR;
}

/*
 * sysprm_set_session_parameter_default - set session parameter value to default
 *
 * return		  : PRM_ERR_NO_ERROR or SYSPRM_ERR error code
 * session_parameter(in)  : session parameter
 * prm_id(in)		  : parameter id
 */
static SYSPRM_ERR
sysprm_set_session_parameter_default (SESSION_PARAM * session_parameter,
				      PARAM_ID prm_id)
{
  SYSPRM_PARAM *prm = &prm_Def[prm_id];

  switch (session_parameter->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      session_parameter->value.i = PRM_GET_INT (prm->default_value);
      break;
    case PRM_FLOAT:
      session_parameter->value.f = PRM_GET_FLOAT (prm->default_value);
      break;
    case PRM_BOOLEAN:
      session_parameter->value.b = PRM_GET_BOOL (prm->default_value);
      break;
    case PRM_BIGINT:
      session_parameter->value.bi = PRM_GET_BIGINT (prm->default_value);
      break;
    case PRM_STRING:
      {
	if (PRM_IS_ALLOCATED (session_parameter->flag))
	  {
	    free_and_init (session_parameter->value.str);
	    PRM_CLEAR_BIT (PRM_ALLOCATED, session_parameter->flag);
	  }
	session_parameter->value.str = PRM_GET_STRING (prm->default_value);
	break;
      }
    case PRM_INTEGER_LIST:
      {
	if (PRM_IS_ALLOCATED (session_parameter->flag))
	  {
	    free_and_init (session_parameter->value.integer_list);
	    PRM_CLEAR_BIT (PRM_ALLOCATED, session_parameter->flag);
	  }
	session_parameter->value.integer_list =
	  PRM_GET_INTEGER_LIST (prm->default_value);
	break;
      }
    }

  return PRM_ERR_NO_ERROR;
}
#endif /* SERVER_MODE */

/*
 * sysprm_compare_values () - compare two system parameter values
 *
 * return	     : comparison result (0 - equal, otherwise - different).
 * first_value (in)  : pointer to first value
 * second_value (in) : pointer to second value
 * val_type (in)     : datatype for values (make sure the values that are
 *		       compared have the same datatype)
 */
static int
sysprm_compare_values (void *first_value, void *second_value,
		       unsigned int val_type)
{
  switch (val_type)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      return (PRM_GET_INT (first_value) != PRM_GET_INT (second_value));

    case PRM_BOOLEAN:
      return (PRM_GET_BOOL (first_value) != PRM_GET_BOOL (second_value));

    case PRM_FLOAT:
      return (PRM_GET_FLOAT (first_value) != PRM_GET_FLOAT (second_value));

    case PRM_STRING:
      {
	char *first_str = PRM_GET_STRING (first_value);
	char *second_str = PRM_GET_STRING (second_value);

	if (first_str == NULL && second_str == NULL)
	  {
	    /* both values are null, return equal */
	    return 0;
	  }

	if (first_str == NULL || second_str == NULL)
	  {
	    /* only one is null, return different */
	    return 1;
	  }

	return intl_mbs_casecmp (first_str, second_str);
      }

    case PRM_BIGINT:
      return (PRM_GET_BIGINT (first_value) != PRM_GET_BIGINT (second_value));

    case PRM_INTEGER_LIST:
      {
	int i;
	int *first_int_list = PRM_GET_INTEGER_LIST (first_value);
	int *second_int_list = PRM_GET_INTEGER_LIST (second_value);

	if (first_int_list == NULL && second_int_list == NULL)
	  {
	    /* both values are null, return equal */
	    return 0;
	  }

	if (second_int_list == NULL || second_int_list == NULL)
	  {
	    /* only one value is null, return different */
	    return 1;
	  }

	if (first_int_list[0] != second_int_list[0])
	  {
	    /* different size for integer lists, return different */
	    return 1;
	  }

	for (i = 1; i <= second_int_list[0]; i++)
	  {
	    if (first_int_list[i] != second_int_list[i])
	      {
		/* found a different integer, return different */
		return 0;
	      }
	  }

	/* all integers are equal, return equal */
	return 1;
      }

    default:
      assert (0);
      break;
    }

  return 0;
}

/*
 * sysprm_set_sysprm_value_from_parameter () - set the value of sysprm_value
 *					       from a system parameter.
 *
 * return	  : void.
 * prm_value (in) : sysprm_value.
 * prm (in)	  : system parameter.
 */
static void
sysprm_set_sysprm_value_from_parameter (SYSPRM_VALUE * prm_value,
					SYSPRM_PARAM * prm)
{
  int size;

  switch (prm->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      prm_value->i = PRM_GET_INT (prm->value);
      break;
    case PRM_FLOAT:
      prm_value->f = PRM_GET_FLOAT (prm->value);
      break;
    case PRM_BOOLEAN:
      prm_value->b = PRM_GET_BOOL (prm->value);
      break;
    case PRM_BIGINT:
      prm_value->bi = PRM_GET_BIGINT (prm->value);
      break;
    case PRM_STRING:
      if (PRM_GET_STRING (prm->value) != NULL)
	{
	  prm_value->str = strdup (PRM_GET_STRING (prm->value));
	  if (prm_value->str == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      strlen (PRM_GET_STRING (prm->value)) + 1);
	    }
	}
      else
	{
	  prm_value->str = NULL;
	}
      break;
    case PRM_INTEGER_LIST:
      {
	int *integer_list = PRM_GET_INTEGER_LIST (prm->value);
	if (integer_list != NULL)
	  {
	    size = (integer_list[0] + 1) * sizeof (int);
	    prm_value->integer_list = (int *) malloc (size);
	    if (prm_value->integer_list == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      }
	    else
	      {
		memcpy (prm_value->integer_list, integer_list, size);
	      }
	  }
	else
	  {
	    prm_value->integer_list = NULL;
	  }
	break;
      }
    case PRM_NO_TYPE:
      break;
    default:
      assert_release (0);
      break;
    }
}

#if defined (CS_MODE)
/*
 * sysprm_update_client_session_parameters () - update the session parameters
 *						stored on client from array of
 *						session parameters
 *
 * return		   : void
 * session_parameters (in) : array of session parameters
 */
void
sysprm_update_client_session_parameters (SESSION_PARAM * session_parameters)
{
  int i;

  if (session_parameters == NULL)
    {
      /* nothing to do */
      return;
    }

  for (i = 0; i < NUM_SESSION_PRM; i++)
    {
      /* update value */
      sysprm_set_value (GET_PRM (session_parameters[i].prm_id),
			session_parameters[i].value, true, true);
    }
}
#endif /* CS_MODE */

/*
 * sysprm_print_assign_values () - print list of sysprm_assign_values.
 *
 * return	   : size of printed string.
 * prm_values (in) : list of values that need printing.
 * buffer (in)	   : print destination.
 * length (in)	   : maximum allowed size for printed string.
 */
int
sysprm_print_assign_values (SYSPRM_ASSIGN_VALUE * prm_values, char *buffer,
			    int length)
{
  int n = 0;

  if (length == 0)
    {
      /* don't print anything */
      return 0;
    }

  for (; prm_values != NULL; prm_values = prm_values->next)
    {
      n +=
	sysprm_print_sysprm_value (prm_values->prm_id, prm_values->value,
				   buffer + n, length - n, PRM_PRINT_NAME);
      if (prm_values->next)
	{
	  n += snprintf (buffer + n, length - n, "; ");
	}
    }
  return n;
}

#if !defined (SERVER_MODE)
/*
 * sysprm_print_parameters_for_qry_string () - print parameters for query
 *					       string
 *
 * return : Printed string of system parameters. Only parameters marked with
 *	    both PRM_FOR_QRY_STRING and PRM_DIFFERENT flags are printed.
 */
char *
sysprm_print_parameters_for_qry_string (void)
{
  int i, n, len = LINE_MAX;
  char buf[LINE_MAX];
  char *ptr = NULL, *q = NULL;
  int size;

  memset (buf, 0, LINE_MAX);
  ptr = buf;

  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_PRINT_QRY_STRING (i))
	{
	  n = prm_print (GET_PRM (i), ptr, len, PRM_PRINT_ID,
			 PRM_PRINT_CURR_VAL);
	  ptr += n;
	  len -= n;

	  n = snprintf (ptr, len, ";");
	  ptr += n;
	  len -= n;
	}
    }

  /* verify the length of the printed parameters does not exceed LINE_MAX */
  assert (len > 0);

  size = (LINE_MAX - len) * sizeof (char);
  if (size == 0)
    {
      return NULL;
    }

  q = (char *) malloc (size + 1);
  if (q == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      size + 1);
      return NULL;
    }

  memcpy (q, buf, size + 1);

  return q;
}

/*
 * sysprm_print_parameters_for_ha_repl () - print parameters for HA
 *					    replication
 *
 * return : Printed string of system parameters which should be reproduced
 *	    in HA log applier context before HA replication
 */
char *
sysprm_print_parameters_for_ha_repl (void)
{
  int i, n, len = LINE_MAX;
  char buf[LINE_MAX];
  char *ptr = NULL, *q = NULL;
  int size;

  memset (buf, 0, LINE_MAX);
  ptr = buf;

  for (i = 0; i < NUM_PRM; i++)
    {
      unsigned int flag = GET_PRM_STATIC_FLAG (i);

      if (!PRM_IS_FOR_HA_CONTEXT (flag))
	{
	  continue;
	}

      if (i == PRM_ID_INTL_COLLATION || i == PRM_ID_INTL_DATE_LANG
	  || i == PRM_ID_INTL_NUMBER_LANG)
	{
	  char *val = prm_get_string_value (i);

	  if (val == NULL)
	    {
	      continue;
	    }

	  if (i == PRM_ID_INTL_COLLATION
	      && strcasecmp (val, lang_get_collation_name
			     (LANG_GET_BINARY_COLLATION
			      (LANG_SYS_CODESET))) == 0)
	    {
	      continue;
	    }

	  if ((i == PRM_ID_INTL_DATE_LANG || i == PRM_ID_INTL_NUMBER_LANG)
	      && strcasecmp (val, lang_get_Lang_name ()) == 0)
	    {
	      continue;
	    }
	}
      else if (!PRM_IS_DIFFERENT (*(GET_PRM_DYNAMIC_FLAG (i))))
	{
	  continue;
	}

      n = prm_print (GET_PRM (i), ptr, len, PRM_PRINT_NAME,
		     PRM_PRINT_CURR_VAL);
      ptr += n;
      len -= n;

      n = snprintf (ptr, len, ";");
      ptr += n;
      len -= n;
    }

  /* verify the length of the printed parameters does not exceed LINE_MAX */
  assert (len > 0);

  size = (LINE_MAX - len) * sizeof (char);
  if (size == 0)
    {
      return NULL;
    }

  q = (char *) malloc (size + 1);
  if (q == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      size + 1);
      return NULL;
    }

  memcpy (q, buf, size + 1);

  return q;
}

/*
 * sysprm_init_intl_param () -
 *
 * return:
 */
void
sysprm_init_intl_param (void)
{
  SYSPRM_PARAM *prm_date_lang;
  SYSPRM_PARAM *prm_number_lang;
  SYSPRM_PARAM *prm_intl_collation;

  prm_date_lang = prm_find (PRM_NAME_INTL_DATE_LANG, NULL);
  prm_number_lang = prm_find (PRM_NAME_INTL_NUMBER_LANG, NULL);
  prm_intl_collation = prm_find (PRM_NAME_INTL_COLLATION, NULL);

  /* intl system parameters are session based and depend on system language;
   * The language is read from DB (and client from server), after the system
   * parameters module was initialized and default values read from
   * configuration file.
   * This function avoids to override any value already read from config.
   * If no values are set from config, then this function sets the values
   * according to the default language.
   *
   * On client (CAS), we set the client copy of the value, and also the cached
   * session parameter value (which is the default starting value, when the
   * existing CAS serves another session) is intialized.
   */

  if (prm_date_lang != NULL && !PRM_IS_SET (*prm_date_lang->dynamic_flag))
    {
      prm_set (prm_date_lang, lang_get_Lang_name (), true);
#if defined (CS_MODE)
      sysprm_update_cached_session_param_val (PRM_ID_INTL_DATE_LANG);
#endif
    }

  if (prm_number_lang != NULL && !PRM_IS_SET (*prm_number_lang->dynamic_flag))
    {
      prm_set (prm_number_lang, lang_get_Lang_name (), true);
#if defined (CS_MODE)
      sysprm_update_cached_session_param_val (PRM_ID_INTL_NUMBER_LANG);
#endif
    }

  if (prm_intl_collation != NULL
      && !PRM_IS_SET (*prm_intl_collation->dynamic_flag))
    {
      prm_set (prm_intl_collation,
	       lang_get_collation_name (LANG_GET_BINARY_COLLATION
					(LANG_SYS_CODESET)), true);
#if defined (CS_MODE)
      sysprm_update_cached_session_param_val (PRM_ID_INTL_COLLATION);
#endif
    }
}
#endif /* !SERVER_MODE */

/*
 * sysprm_set_error () - sets an error for system parameter errors
 *
 * return    : error code
 * rc (in)   : SYSPRM_ERR error
 * data (in) : data to be printed with error
 */
int
sysprm_set_error (SYSPRM_ERR rc, const char *data)
{
  int error;

  /* first check if error was already set */
  error = er_errid ();
  if (error != NO_ERROR)
    {
      /* already set */
      return error;
    }

  if (rc != PRM_ERR_NO_ERROR)
    {
      switch (rc)
	{
	case PRM_ERR_UNKNOWN_PARAM:
	case PRM_ERR_BAD_VALUE:
	case PRM_ERR_BAD_STRING:
	case PRM_ERR_BAD_RANGE:
	  if (data)
	    {
	      error = ER_PRM_BAD_VALUE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, data);
	    }
	  else
	    {
	      error = ER_PRM_BAD_VALUE_NO_DATA;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  break;
	case PRM_ERR_CANNOT_CHANGE:
	case PRM_ERR_NOT_FOR_CLIENT:
	case PRM_ERR_NOT_FOR_SERVER:
	  if (data)
	    {
	      error = ER_PRM_CANNOT_CHANGE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, data);
	    }
	  else
	    {
	      error = ER_PRM_CANNOT_CHANGE_NO_DATA;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  break;
	case PRM_ERR_NOT_SOLE_TRAN:
	  error = ER_NOT_SOLE_TRAN;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  break;
	case PRM_ERR_COMM_ERR:
	  error = ER_NET_SERVER_COMM_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
		  "db_set_system_parameters");
	  break;
	case PRM_ERR_NO_MEM_FOR_PRM:
	default:
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  break;
	}
    }

  return error;
}

/*
 * sysprm_get_session_parameters_count () - get the count of session
 *					    parameters
 * return : count
 */
int
sysprm_get_session_parameters_count (void)
{
  return NUM_SESSION_PRM;
}

#if defined (CS_MODE)
/*
 * sysprm_update_cached_session_param_val () - updates value of a cached
 *		 session parameter
 * prm_id (in) : cached session system paramater id to update
 *
 *  Note : cached session parameters (global cached_session_parameters) are
 *	   default value copies of the system parameters which can be changed
 *	   in a session context; this should be used only during init process
 *	   to alter the default initial value of system parameter for session.
 */
static void
sysprm_update_cached_session_param_val (const PARAM_ID prm_id)
{
  SESSION_PARAM *cached_session_prm;
  SYSPRM_PARAM *sys_prm;
  int i;

  assert (NUM_SESSION_PRM > 0);

  sys_prm = GET_PRM (prm_id);

  for (i = 0; i < NUM_SESSION_PRM; i++)
    {
      cached_session_prm = &cached_session_parameters[i];
      if (cached_session_prm->prm_id != prm_id)
	{
	  continue;
	}

      cached_session_prm->flag = *sys_prm->dynamic_flag;
      sysprm_set_sysprm_value_from_parameter (&cached_session_prm->value,
					      sys_prm);
      sysprm_update_session_prm_flag_allocated (cached_session_prm);
      break;
    }
}
#endif /* CS_MODE */
