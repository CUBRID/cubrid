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
#include "deduplicate_key.h"
#include "misc_string.h"
#include "error_manager.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "util_func.h"
#include "log_comm.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "environment_variable.h"
#include "intl_support.h"
#include "message_catalog.h"
#include "language_support.h"
#include "connection_defs.h"
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "server_support.h"
#include "boot_sr.h"
#include "page_buffer.h"
#include "session.h"
#include "vacuum.h"
#include "log_manager.h"
#include "xserver_interface.h"
#include "double_write_buffer.hpp"
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
#include "log_applier.h"
#include "utility.h"
#include "tz_support.h"
#include "perf_monitor.h"
#include "fault_injection.h"
#include "tde.h"
#if defined (SERVER_MODE)
#include "thread_worker_pool.hpp"	// for cubthread::system_core_count
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif // SERVER_MODE
#include "string_regex.hpp"
#if !defined (SERVER_MODE)
#include "optimizer.h"
#endif
#include "host_lookup.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

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
// #region PRM_NAME_DEFINE
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

#define PRM_NAME_INSERT_MODE "insert_execution_mode"

#define PRM_NAME_LK_MAX_SCANID_BIT "max_index_scan_count"

#define PRM_NAME_HOSTVAR_LATE_BINDING "hostvar_late_binding"

#define PRM_NAME_HOSTVAR_PEEKING "hostvar_peeking"

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

#define PRM_NAME_XASL_CACHE_MAX_ENTRIES "max_plan_cache_entries"
#define PRM_NAME_XASL_CACHE_MAX_CLONES "max_plan_cache_clones"
#define PRM_NAME_XASL_CACHE_TIMEOUT "plan_cache_timeout"
#define PRM_NAME_XASL_CACHE_LOGGING "plan_cache_logging"

#define PRM_NAME_FILTER_PRED_MAX_CACHE_ENTRIES "max_filter_pred_cache_entries"
#define PRM_NAME_FILTER_PRED_MAX_CACHE_CLONES "max_filter_pred_cache_clones"

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

#define PRM_NAME_HA_TCP_PING_HOSTS "ha_tcp_ping_hosts"

#define PRM_NAME_HA_PING_TIMEOUT "ha_ping_timeout"

#define PRM_NAME_HA_APPLYLOGDB_RETRY_ERROR_LIST "ha_applylogdb_retry_error_list"

#define PRM_NAME_HA_APPLYLOGDB_IGNORE_ERROR_LIST "ha_applylogdb_ignore_error_list"

#define PRM_NAME_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS "ha_applylogdb_log_wait_time_in_secs"

#define PRM_NAME_HA_SQL_LOGGING "ha_enable_sql_logging"

#define PRM_NAME_HA_SQL_LOG_MAX_SIZE_IN_MB "ha_sql_log_max_size_in_mbytes"

#define PRM_NAME_HA_SQL_LOG_PATH "ha_sql_log_path"

#define PRM_NAME_HA_SQL_LOG_MAX_COUNT "ha_sql_log_max_count"

#define PRM_NAME_RECOVERY_REDO_MINIMUM_JOB_COUNT "recovery_redo_minimum_job_count"
#define PRM_NAME_RECOVERY_REDO_JOB_PERIOD_IN_SECS "recovery_redo_job_period_in_secs"

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
#define PRM_NAME_STORED_PROCEDURE "stored_procedure"

#define PRM_NAME_JAVA_STORED_PROCEDURE_PORT "java_stored_procedure_port"
#define PRM_NAME_STORED_PROCEDURE_PORT "stored_procedure_port"

#define PRM_NAME_JAVA_STORED_PROCEDURE_JVM_OPTIONS "java_stored_procedure_jvm_options"
#define PRM_NAME_STORED_PROCEDURE_JVM_OPTIONS "stored_procedure_vm_options"

#define PRM_NAME_JAVA_STORED_PROCEDURE_DEBUG "java_stored_procedure_debug"
#define PRM_NAME_STORED_PROCEDURE_DEBUG "stored_procedure_debug"

#define PRM_NAME_JAVA_STORED_PROCEDURE_UDS "java_stored_procedure_uds"
#define PRM_NAME_STORED_PROCEDURE_UDS "stored_procedure_uds"

#define PRM_NAME_ALLOW_TRUNCATED_STRING "allow_truncated_string"

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

#define PRM_NAME_TDE_KEYS_FILE_PATH "tde_keys_file_path"

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

#define PRM_NAME_PB_AOUT_RATIO "data_aout_ratio"

#define PRM_NAME_MAX_AGG_HASH_SIZE "max_agg_hash_size"
#define PRM_NAME_AGG_HASH_RESPECT_ORDER "agg_hash_respect_order"

#define PRM_NAME_USE_BTREE_FENCE_KEY "use_btree_fence_key"

#define PRM_NAME_OPTIMIZER_ENABLE_MERGE_JOIN "optimizer_enable_merge_join"
#define PRM_NAME_MAX_HASH_LIST_SCAN_SIZE "max_hash_list_scan_size"
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
#define PRM_NAME_PB_LRU_BUFFER_RATIO "lru_buffer_ratio"

#define PRM_NAME_VACUUM_MASTER_WAKEUP_INTERVAL "vacuum_master_interval_in_msecs"

#define PRM_NAME_VACUUM_LOG_BLOCK_PAGES "vacuum_log_block_pages"
#define PRM_NAME_VACUUM_WORKER_COUNT "vacuum_worker_count"

#define PRM_NAME_DISABLE_VACUUM "vacuum_disable"

#define PRM_NAME_SA_MODE_AUTO_VACUUM "sa_mode_auto_vacuum"

#define PRM_NAME_ER_LOG_VACUUM "er_log_vacuum"

#define PRM_NAME_LOG_BTREE_OPS "log_btree_operations"

#define PRM_NAME_OBJECT_PRINT_FORMAT_OID "print_object_as_oid"

#define PRM_NAME_TIMEZONE "timezone"
#define PRM_NAME_SERVER_TIMEZONE "server_timezone"
#define PRM_NAME_TZ_LEAP_SECOND_SUPPORT "tz_leap_second_support"

#define PRM_NAME_OPTIMIZER_ENABLE_AGGREGATE_OPTIMIZATION "optimizer_enable_aggregate_optimization"

#define PRM_NAME_VACUUM_PREFETCH_LOG_NBUFFERS "vacuum_prefetch_log_pages"
#define PRM_NAME_VACUUM_PREFETCH_LOG_BUFFER_SIZE "vacuum_prefetch_log_buffer_size"
#define PRM_NAME_VACUUM_PREFETCH_LOG_MODE "vacuum_prefetch_log_mode"

#define PRM_NAME_PB_NEIGHBOR_FLUSH_NONDIRTY "data_buffer_neighbor_flush_nondirty"
#define PRM_NAME_PB_NEIGHBOR_FLUSH_PAGES "data_buffer_neighbor_flush_pages"

#define PRM_NAME_FAULT_INJECTION_IDS "fault_injection_ids"
#define PRM_NAME_FAULT_INJECTION_TEST "fault_injection_test"
#define PRM_NAME_FAULT_INJECTION_ACTION_PREFER_ABORT_TO_EXIT "fault_injection_action_prefer_abort_to_exit"

#define PRM_NAME_HA_REPL_FILTER_TYPE "ha_repl_filter_type"
#define PRM_NAME_HA_REPL_FILTER_FILE "ha_repl_filter_file"

#define PRM_NAME_COMPENSATE_DEBUG "compensate_debug"
#define PRM_NAME_POSTPONE_DEBUG "postpone_debug"

#define PRM_NAME_CLIENT_CLASS_CACHE_DEBUG "client_class_cache_debug"

#define PRM_NAME_EXAMINE_CLIENT_CACHED_LOCKS "examine_client_cached_locks"

#define PRM_NAME_PB_SEQUENTIAL_VICTIM_FLUSH "data_buffer_sequential_victim_flush"

#define PRM_NAME_LOG_UNIQUE_STATS "log_unique_stats"

#define PRM_NAME_LOGPB_LOGGING_DEBUG "logpb_logging_debug"

#define PRM_NAME_FORCE_RESTART_TO_SKIP_RECOVERY "force_restart_to_skip_recovery"

#define PRM_NAME_ENABLE_STRING_COMPRESSION "enable_string_compression"

#define PRM_NAME_XASL_CACHE_TIME_THRESHOLD_IN_MINUTES "xasl_cache_time_threshold_in_minutes"

#define PRM_NAME_EXTENDED_STATISTICS_ACTIVATION "extended_statistics_activation"

#define PRM_NAME_DISK_LOGGING "disk_logging_debug"
#define PRM_NAME_FILE_LOGGING "file_logging_debug"

#define PRM_NAME_PB_NUM_PRIVATE_CHAINS "num_private_chains"
#define PRM_NAME_PB_MONITOR_LOCKS "pgbuf_monitor_locks"
#define PRM_NAME_PB_MAX_DEPTH_OF_SEARCHING_FOR_VICTIMS_IN_LRU_LIST "max_depth_of_searching_for_victims_in_lru_list"

#define PRM_NAME_CTE_MAX_RECURSIONS "cte_max_recursions"

#define PRM_NAME_DWB_SIZE "double_write_buffer_size"
#define PRM_NAME_DWB_BLOCKS "double_write_buffer_blocks"
#define PRM_NAME_ENABLE_DWB_FLUSH_THREAD "double_write_buffer_enable_flush_thread"
#define PRM_NAME_DWB_LOGGING "double_write_buffer_logging"

#define PRM_NAME_JSON_LOG_ALLOCATIONS "json_log_allocations"
#define PRM_NAME_JSON_MAX_ARRAY_IDX "json_max_array_idx"

#define PRM_NAME_CONNECTION_LOGGING "connection_logging"

#define PRM_NAME_THREAD_LOGGING_FLAG "thread_logging_flag"

#define PRM_NAME_LOG_QUERY_LISTS "log_query_lists"

#define PRM_NAME_THREAD_CONNECTION_POOLING            "thread_connection_pooling"
#define PRM_NAME_THREAD_CONNECTION_TIMEOUT_SECONDS    "thread_connection_timeout_seconds"
#define PRM_NAME_THREAD_WORKER_POOLING                "thread_worker_pooling"
#define PRM_NAME_THREAD_WORKER_TIMEOUT_SECONDS        "thread_worker_timeout_seconds"

#define PRM_NAME_DATA_FILE_ADVISE "data_file_os_advise"

#define PRM_NAME_DEBUG_LOG_ARCHIVES "debug_log_archives"
#define PRM_NAME_DEBUG_ES "debug_external_storage"
#define PRM_NAME_DEBUG_BESTSPACE "debug_heap_bestspace"
#define PRM_NAME_DEBUG_LOG_WRITER "debug_log_writer"
#define PRM_NAME_DEBUG_AUTOCOMMIT "debug_autocommit"
#define PRM_NAME_DEBUG_REPLICATION_DATA "debug_replication_data"
#define PRM_NAME_TRACK_REQUESTS "track_client_requests"
#define PRM_NAME_LOG_PGBUF_VICTIM_FLUSH "log_pgbuf_victim_flush"
#define PRM_NAME_LOG_CHKPT_DETAILED "detailed_checkpoint_logging"
#define PRM_NAME_IB_TASK_MEMSIZE "index_load_task_memsize"
#define PRM_NAME_STATS_ON "stats_on"
#define PRM_NAME_LOADDB_WORKER_COUNT "loaddb_worker_count"
#define PRM_NAME_PERF_TEST_MODE "perf_test_mode"
#define PRM_NAME_REPR_CACHE_LOG "er_log_repr_cache"
#define PRM_NAME_ENABLE_NEW_LFHASH "new_lfhash"
#define PRM_NAME_HEAP_INFO_CACHE_LOGGING "heap_info_cache_logging"
#define PRM_NAME_TDE_DEFAULT_ALGORITHM "tde_default_algorithm"
#define PRM_NAME_ER_LOG_TDE "er_log_tde"

#define PRM_NAME_GENERAL_RESERVE_01 "general_reserve_01"

#define PRM_NAME_TB_DEFAULT_REUSE_OID "create_table_reuseoid"

#define PRM_NAME_USE_STAT_ESTIMATION "use_stat_estimation"
#define PRM_NAME_IGNORE_TRAILING_SPACE "ignore_trailing_space"

#define PRM_NAME_DDL_AUDIT_LOG "ddl_audit_log"
#define PRM_NAME_DDL_AUDIT_LOG_SIZE "ddl_audit_log_size"

#define PRM_NAME_SUPPLEMENTAL_LOG "supplemental_log"
#define PRM_NAME_CDC_LOGGING_DEBUG "cdc_logging_debug"

#define PRM_NAME_RECOVERY_PROGRESS_LOGGING_INTERVAL "recovery_progress_logging_interval"
#define PRM_NAME_FIRST_LOG_PAGEID "first_log_pageid"

#define PRM_NAME_TASK_GROUP "task_group"

#define PRM_NAME_FLASHBACK_TIMEOUT "flashback_timeout"
#define PRM_NAME_FLASHBACK_MAX_TRANSACTION "flashback_max_transaction"
#define PRM_NAME_FLASHBACK_WIN_SIZE "flashback_win_size"
#define PRM_NAME_USE_USER_HOSTS "use_user_hosts"

#define PRM_NAME_QMGR_MAX_QUERY_PER_TRAN "max_query_per_tran"

#define PRM_NAME_REGEXP_ENGINE "regexp_engine"

#define PRM_NAME_ORACLE_COMPAT_NUMBER_BEHAVIOR "oracle_compat_number_behavior"

#define PRM_NAME_PL_TRANSACTION_CONTROL "pl_transaction_control"

#define PRM_NAME_PAGE_LATCH_TIMEOUT "page_latch_timeout"

#define PRM_VALUE_DEFAULT "DEFAULT"
#define PRM_VALUE_MAX "MAX"
#define PRM_VALUE_MIN "MIN"

#define PRM_NAME_STATDUMP_FORCE_ADD_INT_MAX "statdump_force_add_int_max"

#define PRM_NAME_VACUUM_OVFP_CHECK_DURATION  "vacuum_ovfp_check_duration"
#define PRM_NAME_VACUUM_OVFP_CHECK_THRESHOLD "vacuum_ovfp_check_threshold"

#define PRM_NAME_DEDUPLICATE_KEY_LEVEL     "deduplicate_key_level"
#define PRM_NAME_PRINT_INDEX_DETAIL        "print_index_detail"

#define PRM_NAME_MAX_SUBQUERY_CACHE_SIZE    "max_subquery_cache_size"

#define PRM_NAME_ORACLE_STYLE_DIVIDE "oracle_style_divide"

#define PRM_NAME_ENABLE_MEMORY_MONITORING "enable_memory_monitoring"

#define PRM_NAME_STORED_PROCEDURE_DUMP_ICODE "stored_procedure_dump_icode"

#define PRM_NAME_STORED_PROCEDURE_RETURN_NUMERIC_SIZE "stored_procedure_return_numeric_size"

#define PRM_NAME_DBLINK_AUTO_COMMIT "dblink_auto_commit"

#define PRM_NAME_ENABLE_JVM_HEAP_DUMP "enable_jvm_heap_dump"

#define PRM_NAME_PARALLELISM "parallelism"
#define PRM_NAME_MAX_PARALLEL_WORKERS "max_parallel_workers"
#define PRM_NAME_PARALLEL_HEAP_SCAN_PAGE_THRESHOLD "parallel_heap_scan_page_threshold"
#define PRM_NAME_PARALLEL_HASH_JOIN_PAGE_THRESHOLD "parallel_hash_join_page_threshold"
#define PRM_NAME_PARALLEL_SORT_PAGE_THRESHOLD "parallel_sort_page_threshold"

#define PRM_NAME_TCP_KEEPALIVE_IDLE "tcp_keepalive_idle"
#define PRM_NAME_TCP_KEEPALIVE_INTERVAL "tcp_keepalive_interval"
#define PRM_NAME_TCP_KEEPALIVE_COUNT "tcp_keepalive_count"

#define PRM_NAME_TASK_WORKER "task_worker"

#define PRM_NAME_CSS_MAX_CONNECTION_WORKER "max_connection_worker"
#define PRM_NAME_CSS_MIN_CONNECTION_WORKER "min_connection_worker"

#define PRM_NAME_CSS_AUTO_SCALING_WINDOW_SIZE "auto_scaling_window_size"

#define PRM_NAME_CSS_RECV_BUDGET_PER_CONNECTION "recv_budget_per_connection"
#define PRM_NAME_CSS_SEND_BUDGET_PER_CONNECTION "send_budget_per_connection"

#define PRM_NAME_MEMOIZE_MEMORY_LIMIT "memoize_memory_limit"

#define PRM_NAME_LOG_POSTPONE_CACHE_SIZE "postpone_cache_size"

// #endregion 

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
 * Macros to call functions
 */

#define PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (int)); \
      assert (sizeof (*(in_val)) == sizeof (UINT64)); \
      *(err) = (SYSPRM_ERR) (*((prm)->set_dup)) ((void *) (out_val), PRM_INTEGER, (void *) (in_val), PRM_BIGINT); \
    } \
  while (0)

#define PRM_ADJUST_FOR_SET_BIGINT_TO_FLOAT(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (float)); \
      assert (sizeof (*(in_val)) == sizeof (UINT64)); \
      *(err) = (SYSPRM_ERR) (*((prm)->set_dup)) ((void *) (out_val), PRM_FLOAT, (void *) (in_val), PRM_BIGINT); \
    } \
  while (0)

#define PRM_ADJUST_FOR_SET_INTEGER_TO_INTEGER(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (int)); \
      assert (sizeof (*(in_val)) == sizeof (int)); \
      *(err) = (SYSPRM_ERR) (*((prm)->set_dup)) ((void *) (out_val), PRM_INTEGER, (void *) (in_val), PRM_INTEGER);\
    } \
  while (0)

#define PRM_ADJUST_FOR_SET_FLOAT_TO_FLOAT(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (float)); \
      assert (sizeof (*(in_val)) == sizeof (float)); \
      *(err) = (SYSPRM_ERR) (*((prm)->set_dup)) ((void *) (out_val), PRM_FLOAT, (void *) (in_val), PRM_FLOAT); \
    } \
  while (0)

#define PRM_ADJUST_FOR_SET_BIGINT_TO_BIGINT(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (UINT64)); \
      assert (sizeof (*(in_val)) == sizeof (UINT64)); \
      *(err) = (*((prm)->set_dup)) ((void *) (out_val), PRM_BIGINT, (void *) (in_val), PRM_BIGINT); \
    } \
  while (0)

#define PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (UINT64)); \
      assert (sizeof (*(in_val)) == sizeof (int)); \
      *(err) = (SYSPRM_ERR) (*((prm)->get_dup)) ((void *) (out_val), PRM_BIGINT, (void *) (in_val), PRM_INTEGER); \
    } \
  while (0)

#define PRM_ADJUST_FOR_GET_FLOAT_TO_BIGINT(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (UINT64)); \
      assert (sizeof (*(in_val)) == sizeof (float)); \
      *(err) = (SYSPRM_ERR) (*((prm)->get_dup)) ((void *) (out_val), PRM_BIGINT, (void *) (in_val), PRM_FLOAT); \
    } \
  while (0)

#define PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL);\
      assert (sizeof (*(out_val)) == sizeof (int)); \
      assert (sizeof (*(in_val)) == sizeof (int)); \
      *(err) = (SYSPRM_ERR) (*((prm)->get_dup)) ((void *) (out_val), PRM_INTEGER, (void *) (in_val), PRM_INTEGER); \
    } \
  while (0)

#define PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (float)); \
      assert (sizeof (*(in_val)) == sizeof (float)); \
      *(err) = (SYSPRM_ERR) (*((prm)->get_dup)) ((void *) (out_val), PRM_FLOAT, (void *) (in_val), PRM_FLOAT); \
    } \
  while (0)

#define PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT(prm,out_val,in_val,err) \
  do \
    { \
      assert ((prm) != NULL && (out_val) != NULL && (in_val) != NULL); \
      assert (sizeof (*(out_val)) == sizeof (UINT64));\
      assert (sizeof (*(in_val)) == sizeof (UINT64)); \
      *(err) = (SYSPRM_ERR) (*((prm)->get_dup)) ((void *) (out_val), PRM_BIGINT, (void *) (in_val), PRM_BIGINT); \
    } \
  while (0)

/*
 * Macros to access bit fields
 */
#define PRM_USER_CAN_CHANGE(x)     ((x)->static_flag & PRM_USER_CHANGE)
#define PRM_IS_FOR_CLIENT(x)       ((x)->static_flag & PRM_FOR_CLIENT)
#define PRM_IS_FOR_SERVER(x)       ((x)->static_flag & PRM_FOR_SERVER)
#define PRM_IS_HIDDEN(x)           ((x)->static_flag & PRM_HIDDEN)
#define PRM_IS_RELOADABLE(x)       ((x)->static_flag & PRM_RELOADABLE)
#define PRM_IS_COMPOUND(x)         ((x)->static_flag & PRM_COMPOUND)
#define PRM_TEST_CHANGE_ONLY(x)    ((x)->static_flag & PRM_TEST_CHANGE)
#define PRM_IS_FOR_HA(x)           ((x)->static_flag & PRM_FOR_HA)
#define PRM_IS_FOR_SESSION(x)	   ((x)->static_flag & PRM_FOR_SESSION)
#define PRM_GET_FROM_SERVER(x)	   ((x)->static_flag & PRM_FORCE_SERVER)
#define PRM_IS_FOR_QRY_STRING(x)   ((x)->static_flag & PRM_FOR_QRY_STRING)
#define PRM_CLIENT_SESSION_ONLY(x) ((x)->static_flag & PRM_CLIENT_SESSION)
#define PRM_HAS_SIZE_UNIT(x)       ((x)->static_flag & PRM_SIZE_UNIT)
#define PRM_HAS_TIME_UNIT(x)       ((x)->static_flag & PRM_TIME_UNIT)
#define PRM_DIFFERENT_UNIT(x)      ((x)->static_flag & PRM_DIFFER_UNIT)
#define PRM_IS_FOR_HA_CONTEXT(x)   ((x)->static_flag & PRM_FOR_HA_CONTEXT)
#define PRM_IS_FOR_PL_CONTEXT(x)   ((x)->static_flag & PRM_FOR_PL_CONTEXT)
#define PRM_IS_GET_SERVER(x)       ((x)->static_flag & PRM_GET_SERVER)
#define PRM_IS_DEPRECATED(x)       ((x)->static_flag & PRM_DEPRECATED)
#define PRM_IS_OBSOLETED(x)        ((x)->static_flag & PRM_OBSOLETED)

#define PRM_IS_SET(x)              (((x)->dynamic_flag) & PRM_SET)
#define PRM_DEFAULT_VAL_USED(x)    (((x)->dynamic_flag) & PRM_DEFAULT_USED)
#define PRM_IS_DIFFERENT(x)	   (((x)->dynamic_flag) & PRM_DIFFERENT)
#define PRM_IS_ALLOCATED(x)        (((x)->dynamic_flag) & PRM_ALLOCATED)

/*
* Macros to manipulate bit fields
*/
#define PRM_CLEAR_BIT(this, here)  (here &= ~this)
#define PRM_SET_BIT(this, here)    (here |= this)

#if defined (CS_MODE)
#define PRM_PRINT_QRY_STRING(id) (PRM_IS_DIFFERENT (GET_PRM (id)) && PRM_IS_FOR_QRY_STRING (GET_PRM (id)))
#else
#define PRM_PRINT_QRY_STRING(id) (PRM_IS_FOR_QRY_STRING (GET_PRM (id)))
#endif

/*
 * Other macros
 */
#define PRM_DEFAULT_BUFFER_SIZE 256

/* initial error and integer lists */
static const int int_list_initial[1] = { 0 };
static const int prm_stored_procedure_return_numeric_size_default_arr[] = { 2, 38, 15 };

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#define LOGPB_BUFFER_NPAGES_LOWER 128
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#define CSS_MAX_CLIENT_COUNT 4000
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#if !defined (SERVER_MODE) && !defined (SA_MODE)
typedef enum
{
  PGBUF_DEBUG_NO_PAGE_VALIDATION,
  PGBUF_DEBUG_PAGE_VALIDATION_FETCH,
  PGBUF_DEBUG_PAGE_VALIDATION_FREE,
  PGBUF_DEBUG_PAGE_VALIDATION_ALL
} PGBUF_DEBUG_PAGE_VALIDATION_LEVEL;
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#define VACUUM_LOG_BLOCK_PAGES_DEFAULT 0
#define VACUUM_MAX_WORKER_COUNT 0
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

typedef int (*DUP_PRM_FUNC) (void *, SYSPRM_DATATYPE, void *, SYSPRM_DATATYPE);

static int prm_size_to_io_pages (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);
static int prm_io_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);

static int prm_size_to_log_pages (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);
static int prm_log_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);

static int prm_msec_to_sec (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);
static int prm_sec_to_msec (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);

static int prm_sec_to_min (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);
static int prm_min_to_sec (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);

static int prm_equal_to_ori (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type);
#if defined(SERVER_MODE)
static void update_session_state_from_sys_params (THREAD_ENTRY * thread_p, SESSION_PARAM * session_params);
#endif

static const SYSPRM_PARAM_VALUE NULL_SYSPRM_PARAM_VALUE = { true, {.str = NULL} };


SYSPRM_PARAM prm_Def[] = {
  {PRM_ID_ER_LOG_DEBUG,
   PRM_NAME_ER_LOG_DEBUG,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
#if !defined(NDEBUG)
   {false, {.b = true}},
#else
   {false, {.b = false}},
#endif
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_BTREE_DEBUG,
   PRM_NAME_ER_BTREE_DEBUG,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_LOG_LEVEL,
   PRM_NAME_ER_LOG_LEVEL,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = ER_NOTIFICATION_SEVERITY}},
   {false, {.i = ER_NOTIFICATION_SEVERITY}},
   {false, {.i = ER_NOTIFICATION_SEVERITY}}, {false, {.i = ER_FATAL_ERROR_SEVERITY}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_LOG_WARNING,
   PRM_NAME_ER_LOG_WARNING,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_EXIT_ASK,
   PRM_NAME_ER_EXIT_ASK,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = ER_EXIT_DEFAULT}},
   {false, {.i = ER_EXIT_DEFAULT}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_LOG_SIZE,
   PRM_NAME_ER_LOG_SIZE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = (512 * 1024 * 1024) /* 512M */ }},
   {false, {.i = (512 * 1024 * 1024)}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = (100 * 80)}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_LOG_FILE,
   PRM_NAME_ER_LOG_FILE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = (char *) sysprm_error_log_file}},
   {false, {.str = (char *) sysprm_error_log_file}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ACCESS_IP_CONTROL,
   PRM_NAME_ACCESS_IP_CONTROL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ACCESS_IP_CONTROL_FILE,
   PRM_NAME_ACCESS_IP_CONTROL_FILE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = (char *) ""}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IO_LOCKF_ENABLE,
   PRM_NAME_IO_LOCKF_ENABLE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SR_NBUFFERS,
   PRM_NAME_SR_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 128}},
   {false, {.i = 128}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SORT_BUFFER_SIZE,
   PRM_NAME_SORT_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 128}},
   {false, {.i = 128}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_ID_PB_BUFFER_FLUSH_RATIO,
   PRM_NAME_PB_BUFFER_FLUSH_RATIO,
   (PRM_FOR_SERVER | PRM_HIDDEN | PRM_USER_CHANGE),	/* todo: why user change? */
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 0.01f}},
   {false, {.f = 0.01f}},
   {false, {.f = 0.95f}}, {false, {.f = 0.01f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_NBUFFERS,
   PRM_NAME_PB_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 32768}},
   {false, {.i = 32768}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 1024}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PAGE_BUFFER_SIZE,
   PRM_NAME_PAGE_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 32768}},
   {false, {.i = 32768}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 1024}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_ID_HF_UNFILL_FACTOR,
   PRM_NAME_HF_UNFILL_FACTOR,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 0.10f}},
   {false, {.f = 0.10f}},
   {false, {.f = 0.3f}}, {false, {.f = 0.0f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HF_MAX_BESTSPACE_ENTRIES,
   PRM_NAME_HF_MAX_BESTSPACE_ENTRIES,
   (PRM_FOR_SERVER | PRM_HIDDEN | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000000 /* 110 M */ }},
   {false, {.i = 1000000}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_BT_UNFILL_FACTOR,
   PRM_NAME_BT_UNFILL_FACTOR,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 0.05f}},
   {false, {.f = 0.05f}},
   {false, {.f = 0.5f}}, {false, {.f = 0.0f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_BT_OID_NBUFFERS,
   PRM_NAME_BT_OID_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 4.0f}},
   {false, {.f = 4.0f}},
   {false, {.f = 16.0f}}, {false, {.f = 0.049999f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_BT_OID_BUFFER_SIZE,
   PRM_NAME_BT_OID_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 4.0f}},
   {false, {.f = 4.0f}},
   {false, {.f = 16.0f}}, {false, {.f = 0.049999f}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_ID_BT_INDEX_SCAN_OID_ORDER,
   PRM_NAME_BT_INDEX_SCAN_OID_ORDER,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_BOSR_MAXTMP_PAGES,
   PRM_NAME_BOSR_MAXTMP_PAGES,	/* todo: change me */
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1 /* Infinite */ }},
   {false, {.i = INT_MIN}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LK_TIMEOUT_MESSAGE_DUMP_LEVEL,
   PRM_NAME_LK_TIMEOUT_MESSAGE_DUMP_LEVEL,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   PRM_CLEAR_DYNAMIC_FLAG,
   {true, {.i = 0}},
   {true, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LK_ESCALATION_AT,
   PRM_NAME_LK_ESCALATION_AT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100000}},
   {false, {.i = 100000}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 5}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LK_ROLLBACK_ON_LOCK_ESCALATION,
   PRM_NAME_LK_ROLLBACK_ON_LOCK_ESCALATION,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LK_TIMEOUT_SECS,
   PRM_NAME_LK_TIMEOUT_SECS,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_DEPRECATED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1 /* Infinite */ }},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LK_TIMEOUT,
   PRM_NAME_LK_TIMEOUT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1 /* Infinite */ }},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_ID_LK_RUN_DEADLOCK_INTERVAL,
   PRM_NAME_LK_RUN_DEADLOCK_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 1.0f}},
   {false, {.f = 1.0f}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.f = 0.1f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_NBUFFERS,
   PRM_NAME_LOG_NBUFFERS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 16 * ONE_K}},	/* 16k pages => 64M / 128M / 256M based on log page size */
   {false, {.i = LOGPB_BUFFER_NPAGES_LOWER}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = LOGPB_BUFFER_NPAGES_LOWER}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_BUFFER_SIZE,
   PRM_NAME_LOG_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 16 * ONE_K}},	/* 16k pages => 64M / 128M / 256M based on log page size */
   {false, {.i = LOGPB_BUFFER_NPAGES_LOWER}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = LOGPB_BUFFER_NPAGES_LOWER}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_log_pages,
   (DUP_PRM_FUNC) prm_log_pages_to_size},
  {PRM_ID_LOG_CHECKPOINT_NPAGES,
   PRM_NAME_LOG_CHECKPOINT_NPAGES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100000}},
   {false, {.i = 100000}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 10}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_CHECKPOINT_SIZE,
   PRM_NAME_LOG_CHECKPOINT_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100000}},
   {false, {.i = 100000}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 10}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_log_pages,
   (DUP_PRM_FUNC) prm_log_pages_to_size},
  {PRM_ID_LOG_CHECKPOINT_INTERVAL_SECS,
   PRM_NAME_LOG_CHECKPOINT_INTERVAL_SECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 360}},
   {false, {.i = 360}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 60}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_min_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_min},
  {PRM_ID_LOG_CHECKPOINT_INTERVAL,
   PRM_NAME_LOG_CHECKPOINT_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 360}},
   {false, {.i = 360}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 60}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_ID_LOG_CHECKPOINT_SLEEP_MSECS,
   PRM_NAME_LOG_CHECKPOINT_SLEEP_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1}},
   {false, {.i = 1}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_BACKGROUND_ARCHIVING,
   PRM_NAME_LOG_BACKGROUND_ARCHIVING,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_ISOLATION_LEVEL,
   PRM_NAME_LOG_ISOLATION_LEVEL,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = TRAN_READ_COMMITTED}},
   {false, {.i = TRAN_READ_COMMITTED}},
   {false, {.i = TRAN_SERIALIZABLE}},
   {false, {.i = TRAN_READ_COMMITTED}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_MEDIA_FAILURE_SUPPORT,
   PRM_NAME_LOG_MEDIA_FAILURE_SUPPORT,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   PRM_CLEAR_DYNAMIC_FLAG,
   {true, {.i = 0}},
   {true, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_COMMIT_ON_SHUTDOWN,
   PRM_NAME_COMMIT_ON_SHUTDOWN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SHUTDOWN_WAIT_TIME_IN_SECS,
   PRM_NAME_SHUTDOWN_WAIT_TIME_IN_SECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 600}},
   {false, {.i = 600}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 60}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSQL_AUTO_COMMIT,
   PRM_NAME_CSQL_AUTO_COMMIT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_SWEEP_CLEAN,
   PRM_NAME_LOG_SWEEP_CLEAN,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_WS_HASHTABLE_SIZE,
   PRM_NAME_WS_HASHTABLE_SIZE,
   (PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 4096}},
   {false, {.i = 4096}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 1024}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_WS_MEMORY_REPORT,
   PRM_NAME_WS_MEMORY_REPORT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_GC_ENABLE,
   PRM_NAME_GC_ENABLE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_DEPRECATED),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_PORT_ID,
   PRM_NAME_TCP_PORT_ID,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1523}},
   {false, {.i = 1523}},
   {false, {.i = USHRT_MAX}}, {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_CONNECTION_TIMEOUT,
   PRM_NAME_TCP_CONNECTION_TIMEOUT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 5}},
   {false, {.i = 5}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZATION_LEVEL,
   PRM_NAME_OPTIMIZATION_LEVEL,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1}},
   {false, {.i = 1}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_QO_DUMP,
   PRM_NAME_QO_DUMP,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSS_MAX_CLIENTS,
   PRM_NAME_CSS_MAX_CLIENTS,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100}},
   {false, {.i = 100}},
   {false, {.i = CSS_MAX_CLIENT_COUNT}},
   {false, {.i = 10}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_THREAD_STACKSIZE,
   PRM_NAME_THREAD_STACKSIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = (1024 * 1024)}},
   {false, {.bi = (1024 * 1024)}},
   {false, {.bi = INT_MAX}},
   {false, {.bi = 64 * 1024}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CFG_DB_HOSTS,
   PRM_NAME_CFG_DB_HOSTS,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_RESET_TR_PARSER,
   PRM_NAME_RESET_TR_PARSER,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 10}},
   {false, {.i = 10}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IO_BACKUP_NBUFFERS,
   PRM_NAME_IO_BACKUP_NBUFFERS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 256}},
   {false, {.i = 256}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 256}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE,
   PRM_NAME_IO_BACKUP_MAX_VOLUME_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 0}},
   {false, {.bi = 0}},
   {false, {.bi = DB_BIGINT_MAX}},
   {false, {.bi = 1024 * 32}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IO_BACKUP_SLEEP_MSECS,
   PRM_NAME_IO_BACKUP_SLEEP_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_PAGES_IN_TEMP_FILE_CACHE,
   PRM_NAME_MAX_PAGES_IN_TEMP_FILE_CACHE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000 /* pages */ }},
   {false, {.i = 1000}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 100}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
   PRM_NAME_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 512}},
   {false, {.i = 512}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 10}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PTHREAD_SCOPE_PROCESS,
   PRM_NAME_PTHREAD_SCOPE_PROCESS,	/* AIX only */
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TEMP_MEM_BUFFER_PAGES,
   PRM_NAME_TEMP_MEM_BUFFER_PAGES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 4}},
   {false, {.i = 4}},
   {false, {.i = 20}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INDEX_SCAN_KEY_BUFFER_PAGES,
   PRM_NAME_INDEX_SCAN_KEY_BUFFER_PAGES,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 20}},
   {false, {.i = 20}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INDEX_SCAN_KEY_BUFFER_SIZE,
   PRM_NAME_INDEX_SCAN_KEY_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 20}},
   {false, {.i = 20}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_ID_DONT_REUSE_HEAP_FILE,
   PRM_NAME_DONT_REUSE_HEAP_FILE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INSERT_MODE,
   PRM_NAME_INSERT_MODE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1 + 2}},
   {false, {.i = 1 + 2}},
   {false, {.i = 31 /* For backward compatibility */ }},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LK_MAX_SCANID_BIT,
   PRM_NAME_LK_MAX_SCANID_BIT,
   (PRM_OBSOLETED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 32}},
   {false, {.i = 32}},
   {false, {.i = 128}},
   {false, {.i = 32}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HOSTVAR_LATE_BINDING,
   PRM_NAME_HOSTVAR_LATE_BINDING,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ENABLE_HISTO,
   PRM_NAME_ENABLE_HISTO,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},

  {PRM_ID_MUTEX_BUSY_WAITING_CNT,
   PRM_NAME_MUTEX_BUSY_WAITING_CNT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_NUM_LRU_CHAINS,
   PRM_NAME_PB_NUM_LRU_CHAINS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0 /* system define */ }},
   {false, {.i = 0}},
   {false, {.i = 1000}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PAGE_BG_FLUSH_INTERVAL_MSECS,
   PRM_NAME_PAGE_BG_FLUSH_INTERVAL_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PAGE_BG_FLUSH_INTERVAL,
   PRM_NAME_PAGE_BG_FLUSH_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_equal_to_ori,
   (DUP_PRM_FUNC) prm_equal_to_ori},
  {PRM_ID_ADAPTIVE_FLUSH_CONTROL,
   PRM_NAME_ADAPTIVE_FLUSH_CONTROL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_FLUSH_PAGES_PER_SECOND,
   PRM_NAME_MAX_FLUSH_PAGES_PER_SECOND,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 10000}},
   {false, {.i = 10000}},
   {false, {.i = INT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_FLUSH_SIZE_PER_SECOND,
   PRM_NAME_MAX_FLUSH_SIZE_PER_SECOND,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 10000}},
   {false, {.i = 10000}},
   {false, {.i = INT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_ID_PB_SYNC_ON_NFLUSH,
   PRM_NAME_PB_SYNC_ON_NFLUSH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 200}},
   {false, {.i = 200}},
   {false, {.i = INT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_SYNC_ON_FLUSH_SIZE,
   PRM_NAME_PB_SYNC_ON_FLUSH_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 200}},
   {false, {.i = 200}},
   {false, {.i = INT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_io_pages,
   (DUP_PRM_FUNC) prm_io_pages_to_size},
  {PRM_ID_PB_DEBUG_PAGE_VALIDATION_LEVEL,
   PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
#if !defined(NDEBUG)
   {false, {.i = PGBUF_DEBUG_PAGE_VALIDATION_FETCH}},
#else
   {false, {.i = PGBUF_DEBUG_NO_PAGE_VALIDATION}},
#endif
   {false, {.i = PGBUF_DEBUG_NO_PAGE_VALIDATION}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ORACLE_STYLE_OUTERJOIN,
   PRM_NAME_ORACLE_STYLE_OUTERJOIN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ANSI_QUOTES,
   PRM_NAME_ANSI_QUOTES,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEFAULT_WEEK_FORMAT,
   PRM_NAME_DEFAULT_WEEK_FORMAT,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 7}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TEST_MODE,
   PRM_NAME_TEST_MODE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ONLY_FULL_GROUP_BY,
   PRM_NAME_ONLY_FULL_GROUP_BY,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PIPES_AS_CONCAT,
   PRM_NAME_PIPES_AS_CONCAT,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MYSQL_TRIGGER_CORRELATION_NAMES,
   PRM_NAME_MYSQL_TRIGGER_CORRELATION_NAMES,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER,
   PRM_NAME_REQUIRE_LIKE_ESCAPE_CHARACTER,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE | PRM_FOR_QRY_STRING),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_NO_BACKSLASH_ESCAPES,
   PRM_NAME_NO_BACKSLASH_ESCAPES,
   (PRM_FOR_CLIENT | PRM_FOR_SESSION | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_GROUP_CONCAT_MAX_LEN,
   PRM_NAME_GROUP_CONCAT_MAX_LEN,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 1024}},
   {false, {.bi = 1024}},
   {false, {.bi = INT_MAX}},
   {false, {.bi = 4}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STRING_MAX_SIZE_BYTES,
   PRM_NAME_STRING_MAX_SIZE_BYTES,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FOR_SESSION | PRM_SIZE_UNIT | PRM_FOR_HA_CONTEXT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 1024 * 1024}},
   {false, {.bi = 1024 * 1024}},
   {false, {.bi = 32 * 1024 * 1024}},
   {false, {.bi = 64}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ADD_COLUMN_UPDATE_HARD_DEFAULT,
   PRM_NAME_ADD_COLUMN_UPDATE_HARD_DEFAULT,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS,
   PRM_NAME_RETURN_NULL_ON_FUNCTION_ERRORS,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT,
   PRM_NAME_ALTER_TABLE_CHANGE_TYPE_STRICT,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,	/* 1 --> 0 */
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_COMPACTDB_PAGE_RECLAIM_ONLY,
   PRM_NAME_COMPACTDB_PAGE_RECLAIM_ONLY,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PLUS_AS_CONCAT,
   PRM_NAME_PLUS_AS_CONCAT,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},

  {PRM_ID_LIKE_TERM_SELECTIVITY,
   PRM_NAME_LIKE_TERM_SELECTIVITY,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 0.1f}},
   {false, {.f = 0.1f}},
   {false, {.f = 1.0f}},
   {false, {.f = 0.0f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_OUTER_CARD_OF_IDXJOIN,
   PRM_NAME_MAX_OUTER_CARD_OF_IDXJOIN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN | PRM_DEPRECATED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ORACLE_STYLE_EMPTY_STRING,
   PRM_NAME_ORACLE_STYLE_EMPTY_STRING,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_QRY_STRING | PRM_FORCE_SERVER | PRM_FOR_PL_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SUPPRESS_FSYNC,
   PRM_NAME_SUPPRESS_FSYNC,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 100}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CALL_STACK_DUMP_ON_ERROR,
   PRM_NAME_CALL_STACK_DUMP_ON_ERROR,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},

  {PRM_ID_CALL_STACK_DUMP_ACTIVATION,
   PRM_NAME_CALL_STACK_DUMP_ACTIVATION,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = NULL}},
   {false, {.integer_list = (int *) int_list_initial}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CALL_STACK_DUMP_DEACTIVATION,
   PRM_NAME_CALL_STACK_DUMP_DEACTIVATION,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = NULL}},
   {false, {.integer_list = (int *) int_list_initial}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_COMPAT_NUMERIC_DIVISION_SCALE,
   PRM_NAME_COMPAT_NUMERIC_DIVISION_SCALE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT | PRM_FOR_PL_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DBFILES_PROTECT,
   PRM_NAME_DBFILES_PROTECT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_AUTO_RESTART_SERVER,
   PRM_NAME_AUTO_RESTART_SERVER,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_XASL_CACHE_MAX_ENTRIES,
   PRM_NAME_XASL_CACHE_MAX_ENTRIES,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   {false, {.i = 10000}},
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_XASL_CACHE_MAX_CLONES,
   PRM_NAME_XASL_CACHE_MAX_CLONES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   {false, {.i = 2000}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_XASL_CACHE_TIMEOUT,
   PRM_NAME_XASL_CACHE_TIMEOUT,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1 /* infinity */ }},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},

  {PRM_ID_XASL_CACHE_LOGGING,
   PRM_NAME_XASL_CACHE_LOGGING,
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES,
   PRM_NAME_FILTER_PRED_MAX_CACHE_ENTRIES,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FILTER_PRED_MAX_CACHE_CLONES,
   PRM_NAME_FILTER_PRED_MAX_CACHE_CLONES,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 10}},
   {false, {.i = 10}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LIST_QUERY_CACHE_MODE,
   PRM_NAME_LIST_QUERY_CACHE_MODE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 2}},
   {false, {.i = 2}},
   {false, {.i = 2}}, {false, {.i = 2}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LIST_MAX_QUERY_CACHE_ENTRIES,
   PRM_NAME_LIST_MAX_QUERY_CACHE_ENTRIES,
   (PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = INT_MAX}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LIST_MAX_QUERY_CACHE_PAGES,
   PRM_NAME_LIST_MAX_QUERY_CACHE_PAGES,
   (PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = INT_MAX}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_USE_ORDERBY_SORT_LIMIT,
   PRM_NAME_USE_ORDERBY_SORT_LIMIT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_REPLICATION_MODE,
   PRM_NAME_REPLICATION_MODE,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   PRM_CLEAR_DYNAMIC_FLAG,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_MODE,
   PRM_NAME_HA_MODE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FOR_HA | PRM_FORCE_SERVER),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HA_MODE_OFF}},
   {false, {.i = HA_MODE_OFF}},
   {false, {.i = HA_MODE_REPLICA}}, {false, {.i = HA_MODE_OFF}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY,
   PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY,
   (PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HA_MODE_OFF}},
   {false, {.i = HA_MODE_OFF}},
   {false, {.i = HA_MODE_REPLICA}}, {false, {.i = HA_MODE_OFF}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_SERVER_STATE,
   PRM_NAME_HA_SERVER_STATE,
   (PRM_FOR_SERVER | PRM_HIDDEN | PRM_FOR_HA),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HA_SERVER_STATE_IDLE}},
   {false, {.i = HA_SERVER_STATE_IDLE}},
   {false, {.i = HA_SERVER_STATE_DEAD}},
   {false, {.i = HA_SERVER_STATE_IDLE}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_LOG_APPLIER_STATE,
   PRM_NAME_HA_LOG_APPLIER_STATE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN | PRM_FOR_HA),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HA_LOG_APPLIER_STATE_UNREGISTERED}},
   {false, {.i = HA_LOG_APPLIER_STATE_UNREGISTERED}},
   {false, {.i = HA_LOG_APPLIER_STATE_ERROR}},
   {false, {.i = HA_LOG_APPLIER_STATE_UNREGISTERED}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_NODE_LIST,
   PRM_NAME_HA_NODE_LIST,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_REPLICA_LIST,
   PRM_NAME_HA_REPLICA_LIST,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_DB_LIST,
   PRM_NAME_HA_DB_LIST,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_COPY_LOG_BASE,
   PRM_NAME_HA_COPY_LOG_BASE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_COPY_SYNC_MODE,
   PRM_NAME_HA_COPY_SYNC_MODE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_APPLY_MAX_MEM_SIZE,
   PRM_NAME_HA_APPLY_MAX_MEM_SIZE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_APPLY_MAX_MEM_SIZE}},
   {false, {.i = HB_DEFAULT_APPLY_MAX_MEM_SIZE}},
   {false, {.i = INT_MAX}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_PORT_ID,
   PRM_NAME_HA_PORT_ID,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_HA_PORT_ID}},
   {false, {.i = HB_DEFAULT_HA_PORT_ID}},
   {false, {.i = USHRT_MAX}}, {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_INIT_TIMER_IN_MSECS,
   PRM_NAME_HA_INIT_TIMER_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_INIT_TIMER_IN_MSECS}},
   {false, {.i = HB_DEFAULT_INIT_TIMER_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_HEARTBEAT_INTERVAL_IN_MSECS,
   PRM_NAME_HA_HEARTBEAT_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_HEARTBEAT_INTERVAL_IN_MSECS}},
   {false, {.i = HB_DEFAULT_HEARTBEAT_INTERVAL_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_CALC_SCORE_INTERVAL_IN_MSECS,
   PRM_NAME_HA_CALC_SCORE_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_CALC_SCORE_INTERVAL_IN_MSECS}},
   {false, {.i = HB_DEFAULT_CALC_SCORE_INTERVAL_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_FAILOVER_WAIT_TIME_IN_MSECS,
   PRM_NAME_HA_FAILOVER_WAIT_TIME_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_FAILOVER_WAIT_TIME_IN_MSECS}},
   {false, {.i = HB_DEFAULT_FAILOVER_WAIT_TIME_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS,
   PRM_NAME_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_START_CONFIRM_INTERVAL_IN_MSECS}},
   {false, {.i = HB_DEFAULT_START_CONFIRM_INTERVAL_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS,
   PRM_NAME_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS}},
   {false, {.i = HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_MAX_PROCESS_START_CONFIRM,
   PRM_NAME_HA_MAX_PROCESS_START_CONFIRM,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_MAX_PROCESS_START_CONFIRM}},
   {false, {.i = HB_DEFAULT_MAX_PROCESS_START_CONFIRM}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_MAX_PROCESS_DEREG_CONFIRM,
   PRM_NAME_HA_MAX_PROCESS_DEREG_CONFIRM,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM}},
   {false, {.i = HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS,
   PRM_NAME_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS}},
   {false, {.i = HB_DEFAULT_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_CHANGEMODE_INTERVAL_IN_MSECS,
   PRM_NAME_HA_CHANGEMODE_INTERVAL_IN_MSEC,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS}},
   {false, {.i = HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_MAX_HEARTBEAT_GAP,
   PRM_NAME_HA_MAX_HEARTBEAT_GAP,
   (PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = HB_DEFAULT_MAX_HEARTBEAT_GAP}},
   {false, {.i = HB_DEFAULT_MAX_HEARTBEAT_GAP}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_PING_HOSTS,
   PRM_NAME_HA_PING_HOSTS,
   (PRM_FOR_CLIENT | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_APPLYLOGDB_RETRY_ERROR_LIST,
   PRM_NAME_HA_APPLYLOGDB_RETRY_ERROR_LIST,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = NULL}},
   {false, {.integer_list = (int *) int_list_initial}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
   PRM_NAME_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = NULL}},
   {false, {.integer_list = (int *) int_list_initial}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS,
   PRM_NAME_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_SQL_LOGGING,
   PRM_NAME_HA_SQL_LOGGING,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_SQL_LOG_MAX_SIZE_IN_MB,
   PRM_NAME_HA_SQL_LOG_MAX_SIZE_IN_MB,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 50}},
   {false, {.i = INT_MIN}},
   {false, {.i = 2048}}, {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_COPY_LOG_MAX_ARCHIVES,
   PRM_NAME_HA_COPY_LOG_MAX_ARCHIVES,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1}},
   {false, {.i = 1}},
   {false, {.i = INT_MAX}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_COPY_LOG_TIMEOUT,
   PRM_NAME_HA_COPY_LOG_TIMEOUT,
   (PRM_FOR_SERVER | PRM_FOR_HA | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 5}},
   {false, {.i = 5}},
   {false, {.i = INT_MAX}}, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_REPLICA_DELAY_IN_SECS,
   PRM_NAME_HA_REPLICA_DELAY,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = INT_MAX}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_ID_HA_REPLICA_TIME_BOUND,
   PRM_NAME_HA_REPLICA_TIME_BOUND,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_DELAY_LIMIT_IN_SECS,
   PRM_NAME_HA_DELAY_LIMIT,
   (PRM_FOR_SERVER | PRM_FOR_HA | PRM_USER_CHANGE | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_ID_HA_DELAY_LIMIT_DELTA_IN_SECS,
   PRM_NAME_HA_DELAY_LIMIT_DELTA,
   (PRM_FOR_SERVER | PRM_FOR_HA | PRM_USER_CHANGE | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = INT_MAX}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS,
   PRM_NAME_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 500}},
   {false, {.i = 500}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL,
   PRM_NAME_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 500}},
   {false, {.i = 500}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS,
   PRM_NAME_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS,
   (PRM_FOR_CLIENT | PRM_FOR_HA | PRM_TIME_UNIT | PRM_DIFFER_UNIT | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 15}},
   {false, {.i = 15}},
   {false, {.i = INT_MAX}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_ID_GENERAL_RESERVE_01,
   PRM_NAME_GENERAL_RESERVE_01,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_COMPAT_PRIMARY_KEY,
   PRM_NAME_COMPAT_PRIMARY_KEY,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_HEADER_FLUSH_INTERVAL,
   PRM_NAME_LOG_HEADER_FLUSH_INTERVAL,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   PRM_CLEAR_DYNAMIC_FLAG,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_ASYNC_COMMIT,
   PRM_NAME_LOG_ASYNC_COMMIT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_GROUP_COMMIT_INTERVAL_MSECS,
   PRM_NAME_LOG_GROUP_COMMIT_INTERVAL_MSECS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_BG_FLUSH_INTERVAL_MSECS,
   PRM_NAME_LOG_BG_FLUSH_INTERVAL_MSECS,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   PRM_CLEAR_DYNAMIC_FLAG,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_BG_FLUSH_NUM_PAGES,
   PRM_NAME_LOG_BG_FLUSH_NUM_PAGES,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   PRM_CLEAR_DYNAMIC_FLAG,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INTL_MBS_SUPPORT,
   PRM_NAME_INTL_MBS_SUPPORT,
   (PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_COMPRESS,
   PRM_NAME_LOG_COMPRESS,
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_BLOCK_NOWHERE_STATEMENT,
   PRM_NAME_BLOCK_NOWHERE_STATEMENT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_BLOCK_DDL_STATEMENT,
   PRM_NAME_BLOCK_DDL_STATEMENT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
#if defined (ENABLE_UNUSED_FUNCTION)
  {PRM_ID_SINGLE_BYTE_COMPARE,
   PRM_NAME_SINGLE_BYTE_COMPARE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
#endif
  {PRM_ID_CSQL_HISTORY_NUM,
   PRM_NAME_CSQL_HISTORY_NUM,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 50}},
   {false, {.i = 50}},
   {false, {.i = 200}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_TRACE_DEBUG,
   PRM_NAME_LOG_TRACE_DEBUG,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DL_FORK,
   PRM_NAME_DL_FORK,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_PRODUCTION_MODE,
   PRM_NAME_ER_PRODUCTION_MODE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_GET_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_STOP_ON_ERROR,
   PRM_NAME_ER_STOP_ON_ERROR,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 0}}, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_RCVBUF_SIZE,
   PRM_NAME_TCP_RCVBUF_SIZE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_SNDBUF_SIZE,
   PRM_NAME_TCP_SNDBUF_SIZE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_NODELAY,
   PRM_NAME_TCP_NODELAY,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_KEEPALIVE,
   PRM_NAME_TCP_KEEPALIVE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSQL_SINGLE_LINE_MODE,
   PRM_NAME_CSQL_SINGLE_LINE_MODE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_XASL_DEBUG_DUMP,
   PRM_NAME_XASL_DEBUG_DUMP,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_MAX_ARCHIVES,
   PRM_NAME_LOG_MAX_ARCHIVES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = INT_MAX}},
   {false, {.i = INT_MAX}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FORCE_REMOVE_LOG_ARCHIVES,
   PRM_NAME_FORCE_REMOVE_LOG_ARCHIVES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL,
   PRM_NAME_REMOVE_LOG_ARCHIVES_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_NO_LOGGING,
   PRM_NAME_LOG_NO_LOGGING,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_UNLOADDB_IGNORE_ERROR,
   PRM_NAME_UNLOADDB_IGNORE_ERROR,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_UNLOADDB_LOCK_TIMEOUT,
   PRM_NAME_UNLOADDB_LOCK_TIMEOUT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOADDB_FLUSH_INTERVAL,
   PRM_NAME_LOADDB_FLUSH_INTERVAL,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IO_TEMP_VOLUME_PATH,
   PRM_NAME_IO_TEMP_VOLUME_PATH,
   (PRM_FOR_SERVER),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IO_VOLUME_EXT_PATH,
   PRM_NAME_IO_VOLUME_EXT_PATH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_UNIQUE_ERROR_KEY_VALUE,
   PRM_NAME_UNIQUE_ERROR_KEY_VALUE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN | PRM_DEPRECATED),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_USE_SYSTEM_MALLOC,
   PRM_NAME_USE_SYSTEM_MALLOC,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_EVENT_HANDLER,
   PRM_NAME_EVENT_HANDLER,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_EVENT_ACTIVATION,
   PRM_NAME_EVENT_ACTIVATION,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = NULL}},
   {false, {.integer_list = (int *) int_list_initial}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_READ_ONLY_MODE,
   PRM_NAME_READ_ONLY_MODE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MNT_WAITING_THREAD,
   PRM_NAME_MNT_WAITING_THREAD,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MNT_STATS_THRESHOLD,
   PRM_NAME_MNT_STATS_THRESHOLD,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = NULL}},
   {false, {.integer_list = (int *) int_list_initial}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SERVICE_SERVICE_LIST,
   PRM_NAME_SERVICE_SERVICE_LIST,
   (PRM_FOR_SERVER),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SERVICE_SERVER_LIST,
   PRM_NAME_SERVICE_SERVER_LIST,
   (PRM_FOR_SERVER),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SESSION_STATE_TIMEOUT,
   PRM_NAME_SESSION_STATE_TIMEOUT,
   (PRM_FOR_SERVER | PRM_TEST_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 60 * 60 * 6 /* 6 hours */ }},
   {false, {.i = 60 * 60 * 6 /* 6 hours */ }},
   {false, {.i = 60 * 60 * 24 * 365 /* 1 nonleap year */ }},
   {false, {.i = 60 /* 1 minute */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MULTI_RANGE_OPT_LIMIT,
   PRM_NAME_MULTI_RANGE_OPT_LIMIT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100}},
   {false, {.i = 100}},
   {false, {.i = 10000}}, {false, {.i = 0 /* disabled */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INTL_NUMBER_LANG,
   PRM_NAME_INTL_NUMBER_LANG,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING | PRM_FOR_HA_CONTEXT | PRM_FOR_PL_CONTEXT),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = NULL}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INTL_DATE_LANG,
   PRM_NAME_INTL_DATE_LANG,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING | PRM_FOR_HA_CONTEXT | PRM_FOR_PL_CONTEXT),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = NULL}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  /* All the compound parameters *must* be at the end of the array so that the changes they cause are not overridden by
   * other parameters (for example in sysprm_load_and_init the parameters are set to their default in the order they
   * are found in this array). */
  {PRM_ID_COMPAT_MODE,
   PRM_NAME_COMPAT_MODE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_TEST_CHANGE | PRM_COMPOUND),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = COMPAT_CUBRID}},
   {false, {.i = COMPAT_CUBRID}},
   {false, {.i = COMPAT_ORACLE}}, {false, {.i = COMPAT_CUBRID}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DB_VOLUME_SIZE,
   PRM_NAME_DB_VOLUME_SIZE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 536870912ULL /* 512M */ }},
   {false, {.bi = 536870912ULL}},
   {false, {.bi = 21474836480ULL /* 20G */ }},
   {false, {.bi = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_VOLUME_SIZE,
   PRM_NAME_LOG_VOLUME_SIZE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 536870912ULL /* 512M */ }},
   {false, {.bi = 536870912ULL}},
   {false, {.bi = 4294967296ULL /* 4G */ }},
   {false, {.bi = 20971520ULL /* 20M */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_UNICODE_INPUT_NORMALIZATION,
   PRM_NAME_UNICODE_INPUT_NORMALIZATION,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_UNICODE_OUTPUT_NORMALIZATION,
   PRM_NAME_UNICODE_OUTPUT_NORMALIZATION,
   (PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INTL_CHECK_INPUT_STRING,
   PRM_NAME_INTL_CHECK_INPUT_STRING,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CHECK_PEER_ALIVE,
   PRM_NAME_CHECK_PEER_ALIVE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_CLIENT_SESSION),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = CSS_CHECK_PEER_ALIVE_BOTH}},
   {false, {.i = CSS_CHECK_PEER_ALIVE_BOTH}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SQL_TRACE_SLOW_MSECS,
   PRM_NAME_SQL_TRACE_SLOW_MSECS,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_DEPRECATED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   {false, {.i = 1000 * 60 * 60 * 24 /* 24 hours */ }}, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SQL_TRACE_SLOW,
   PRM_NAME_SQL_TRACE_SLOW,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_TIME_UNIT | PRM_DIFFER_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   {false, {.i = 1000 * 60 * 60 * 24 /* 24 hours */ }}, {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_equal_to_ori,
   (DUP_PRM_FUNC) prm_equal_to_ori},
  {PRM_ID_SQL_TRACE_EXECUTION_PLAN,
   PRM_NAME_SQL_TRACE_EXECUTION_PLAN,
   (PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 2}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_TRACE_FLUSH_TIME_MSECS,
   PRM_NAME_LOG_TRACE_FLUSH_TIME,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_TIME_UNIT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_INTL_COLLATION,
   PRM_NAME_INTL_COLLATION,
   (PRM_FOR_CLIENT | PRM_FOR_SESSION | PRM_USER_CHANGE | PRM_FOR_HA_CONTEXT | PRM_FOR_PL_CONTEXT),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = NULL}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_GENERIC_VOL_PREALLOC_SIZE,
   PRM_NAME_GENERIC_VOL_PREALLOC_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT | PRM_OBSOLETED),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 52428800ULL /* 50M */ }},
   {false, {.bi = 0ULL}},
   {false, {.bi = 21474836480ULL /* 20G */ }},
   {false, {.bi = 0ULL}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SORT_LIMIT_MAX_COUNT,
   PRM_NAME_SORT_LIMIT_MAX_COUNT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_QRY_STRING),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0 /* disabled */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SQL_TRACE_IOREADS,
   PRM_NAME_SQL_TRACE_IOREADS,
   (PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_QUERY_TRACE,
   PRM_NAME_QUERY_TRACE,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_QUERY_TRACE_FORMAT,
   PRM_NAME_QUERY_TRACE_FORMAT,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = QUERY_TRACE_TEXT}},
   {false, {.i = QUERY_TRACE_TEXT}},
   {false, {.i = QUERY_TRACE_JSON}},
   {false, {.i = QUERY_TRACE_TEXT}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_RECURSION_SQL_DEPTH,
   PRM_NAME_MAX_RECURSION_SQL_DEPTH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 400}},
   {false, {.i = 400}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_UPDATE_USE_ATTRIBUTE_REFERENCES,
   PRM_NAME_UPDATE_USE_ATTRIBUTE_REFERENCES,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_AOUT_RATIO,
   PRM_NAME_PB_AOUT_RATIO,
   (PRM_FOR_SERVER | PRM_RELOADABLE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 0.0f}},
   {false, {.f = 0.0f}},
   {false, {.f = 3.0}},
   {false, {.f = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_AGG_HASH_SIZE,
   PRM_NAME_MAX_AGG_HASH_SIZE,
   (PRM_FOR_SERVER | PRM_TEST_CHANGE | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 2 * 1024 * 1024 /* 2 MB */ }},
   {false, {.bi = 2 * 1024 * 1024 /* 2 MB */ }},
   {false, {.bi = 128 * 1024 * 1024 /* 128 MB */ }},
   {false, {.bi = 32 * 1024 /* 32 KB */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_AGG_HASH_RESPECT_ORDER,
   PRM_NAME_AGG_HASH_RESPECT_ORDER,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_USE_BTREE_FENCE_KEY,
   PRM_NAME_USE_BTREE_FENCE_KEY,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_ENABLE_MERGE_JOIN,
   PRM_NAME_OPTIMIZER_ENABLE_MERGE_JOIN,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_HASH_LIST_SCAN_SIZE,
   PRM_NAME_MAX_HASH_LIST_SCAN_SIZE,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION | PRM_FOR_QRY_STRING | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 8 * 1024 * 1024 /* 8 MB */ }},
   {false, {.bi = 8 * 1024 * 1024 /* 8 MB */ }},
   {false, {.bi = 128 * 1024 * 1024 /* 128 MB */ }},
   {false, {.bi = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_02,
   PRM_NAME_OPTIMIZER_RESERVE_02,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_03,
   PRM_NAME_OPTIMIZER_RESERVE_03,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_04,
   PRM_NAME_OPTIMIZER_RESERVE_04,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_05,
   PRM_NAME_OPTIMIZER_RESERVE_05,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_06,
   PRM_NAME_OPTIMIZER_RESERVE_06,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_07,
   PRM_NAME_OPTIMIZER_RESERVE_07,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_08,
   PRM_NAME_OPTIMIZER_RESERVE_08,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_09,
   PRM_NAME_OPTIMIZER_RESERVE_09,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_10,
   PRM_NAME_OPTIMIZER_RESERVE_10,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_11,
   PRM_NAME_OPTIMIZER_RESERVE_11,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_12,
   PRM_NAME_OPTIMIZER_RESERVE_12,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_13,
   PRM_NAME_OPTIMIZER_RESERVE_13,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_14,
   PRM_NAME_OPTIMIZER_RESERVE_14,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_15,
   PRM_NAME_OPTIMIZER_RESERVE_15,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_16,
   PRM_NAME_OPTIMIZER_RESERVE_16,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_17,
   PRM_NAME_OPTIMIZER_RESERVE_17,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_18,
   PRM_NAME_OPTIMIZER_RESERVE_18,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_19,
   PRM_NAME_OPTIMIZER_RESERVE_19,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_RESERVE_20,
   PRM_NAME_OPTIMIZER_RESERVE_20,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_REPL_ENABLE_SERVER_SIDE_UPDATE,
   PRM_NAME_HA_REPL_ENABLE_SERVER_SIDE_UPDATE,
   (PRM_FOR_HA | PRM_HIDDEN | PRM_DEPRECATED),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_LRU_HOT_RATIO,
   PRM_NAME_PB_LRU_HOT_RATIO,
   (PRM_FOR_SERVER | PRM_RELOADABLE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 0.4f}},
   {false, {.f = 0.4f}},
   {false, {.f = 0.90f}},
   {false, {.f = 0.05f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_LRU_BUFFER_RATIO,
   PRM_NAME_PB_LRU_BUFFER_RATIO,
   (PRM_FOR_SERVER | PRM_RELOADABLE),
   PRM_FLOAT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.f = 0.05f}},
   {false, {.f = 0.05f}},
   {false, {.f = 0.90f}},
   {false, {.f = 0.05f}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_VACUUM_MASTER_WAKEUP_INTERVAL,
   PRM_NAME_VACUUM_MASTER_WAKEUP_INTERVAL,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 10}},
   {false, {.i = 10}},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_VACUUM_LOG_BLOCK_PAGES,
   PRM_NAME_VACUUM_LOG_BLOCK_PAGES,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = VACUUM_LOG_BLOCK_PAGES_DEFAULT}},
   {false, {.i = VACUUM_LOG_BLOCK_PAGES_DEFAULT}},
   {false, {.i = 1024}},
   {false, {.i = 4}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_VACUUM_WORKER_COUNT,
   PRM_NAME_VACUUM_WORKER_COUNT,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 10}},
   {false, {.i = 10}},
   {false, {.i = VACUUM_MAX_WORKER_COUNT}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_LOG_VACUUM,
   PRM_NAME_ER_LOG_VACUUM,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1}},
   {false, {.i = 1}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DISABLE_VACUUM,
   PRM_NAME_DISABLE_VACUUM,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_BTREE_OPS,
   PRM_NAME_LOG_BTREE_OPS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OBJECT_PRINT_FORMAT_OID,
   PRM_NAME_OBJECT_PRINT_FORMAT_OID,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TIMEZONE,
   PRM_NAME_TIMEZONE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION | PRM_USER_CHANGE | PRM_FOR_QRY_STRING | PRM_FOR_HA_CONTEXT |
    PRM_FOR_PL_CONTEXT),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = NULL}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SERVER_TIMEZONE,
   PRM_NAME_SERVER_TIMEZONE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = NULL}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TZ_LEAP_SECOND_SUPPORT,
   PRM_NAME_TZ_LEAP_SECOND_SUPPORT,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_OPTIMIZER_ENABLE_AGGREGATE_OPTIMIZATION,
   PRM_NAME_OPTIMIZER_ENABLE_AGGREGATE_OPTIMIZATION,
   (PRM_FOR_SERVER | PRM_TEST_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_VACUUM_PREFETCH_LOG_NBUFFERS,
   PRM_NAME_VACUUM_PREFETCH_LOG_NBUFFERS,
   (PRM_FOR_SERVER | PRM_OBSOLETED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_VACUUM_PREFETCH_LOG_BUFFER_SIZE,
   PRM_NAME_VACUUM_PREFETCH_LOG_BUFFER_SIZE,
   (PRM_FOR_SERVER | PRM_OBSOLETED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) prm_size_to_log_pages,
   (DUP_PRM_FUNC) prm_log_pages_to_size},
  {PRM_ID_VACUUM_PREFETCH_LOG_MODE,
   PRM_NAME_VACUUM_PREFETCH_LOG_MODE,
   (PRM_FOR_SERVER | PRM_OBSOLETED),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_NEIGHBOR_FLUSH_NONDIRTY,
   PRM_NAME_PB_NEIGHBOR_FLUSH_NONDIRTY,
   (PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_NEIGHBOR_FLUSH_PAGES,
   PRM_NAME_PB_NEIGHBOR_FLUSH_PAGES,
   (PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 8}},
   {false, {.i = 8}},
   {false, {.i = 32}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FAULT_INJECTION_IDS,
   PRM_NAME_FAULT_INJECTION_IDS,
   (PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = NULL}},
   {false, {.integer_list = (int *) int_list_initial}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FAULT_INJECTION_TEST,
   PRM_NAME_FAULT_INJECTION_TEST,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = FI_GROUP_NONE}},
   {false, {.i = FI_GROUP_NONE}},
   {false, {.i = FI_GROUP_MAX}},
   {false, {.i = FI_GROUP_NONE}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FAULT_INJECTION_ACTION_PREFER_ABORT_TO_EXIT,
   PRM_NAME_FAULT_INJECTION_ACTION_PREFER_ABORT_TO_EXIT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_REPL_FILTER_TYPE,
   PRM_NAME_HA_REPL_FILTER_TYPE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = REPL_FILTER_NONE}},
   {false, {.i = REPL_FILTER_NONE}},
   {false, {.i = REPL_FILTER_EXCLUDE_TBL}},
   {false, {.i = REPL_FILTER_NONE}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_REPL_FILTER_FILE,
   PRM_NAME_HA_REPL_FILTER_FILE,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = (char *) ""}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_COMPENSATE_DEBUG,
   PRM_NAME_COMPENSATE_DEBUG,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_POSTPONE_DEBUG,
   PRM_NAME_POSTPONE_DEBUG,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CLIENT_CLASS_CACHE_DEBUG,
   PRM_NAME_CLIENT_CLASS_CACHE_DEBUG,
   (PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_EXAMINE_CLIENT_CACHED_LOCKS,
   PRM_NAME_EXAMINE_CLIENT_CACHED_LOCKS,
   (PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_SEQUENTIAL_VICTIM_FLUSH,
   PRM_NAME_PB_SEQUENTIAL_VICTIM_FLUSH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_UNIQUE_STATS,
   PRM_NAME_LOG_UNIQUE_STATS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOGPB_LOGGING_DEBUG,
   PRM_NAME_LOGPB_LOGGING_DEBUG,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FORCE_RESTART_TO_SKIP_RECOVERY,
   PRM_NAME_FORCE_RESTART_TO_SKIP_RECOVERY,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_EXTENDED_STATISTICS_ACTIVATION,
   PRM_NAME_EXTENDED_STATISTICS_ACTIVATION,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = PERFMON_ACTIVATION_FLAG_DETAILED_BTREE_PAGE | PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT |
	    PERFMON_ACTIVATION_FLAG_LOCK_OBJECT | PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR}},
   {false, {.i = PERFMON_ACTIVATION_FLAG_DETAILED_BTREE_PAGE | PERFMON_ACTIVATION_FLAG_MVCC_SNAPSHOT |
	    PERFMON_ACTIVATION_FLAG_LOCK_OBJECT | PERFMON_ACTIVATION_FLAG_PB_HASH_ANCHOR}},
   {false, {.i = PERFMON_ACTIVATION_FLAG_MAX_VALUE}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ENABLE_STRING_COMPRESSION,
   PRM_NAME_ENABLE_STRING_COMPRESSION,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_XASL_CACHE_TIME_THRESHOLD_IN_MINUTES,
   PRM_NAME_XASL_CACHE_TIME_THRESHOLD_IN_MINUTES,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 360}},
   {false, {.i = 360}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DISK_LOGGING,
   PRM_NAME_DISK_LOGGING,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FILE_LOGGING,
   PRM_NAME_FILE_LOGGING,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_NUM_PRIVATE_CHAINS,
   PRM_NAME_PB_NUM_PRIVATE_CHAINS,
   (PRM_FOR_SERVER | PRM_RELOADABLE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   {false, {.i = CSS_MAX_CLIENT_COUNT + VACUUM_MAX_WORKER_COUNT}},
   {false, {.i = -1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PB_MONITOR_LOCKS,
   PRM_NAME_PB_MONITOR_LOCKS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CTE_MAX_RECURSIONS,
   PRM_NAME_CTE_MAX_RECURSIONS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION | PRM_FOR_CLIENT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 2000}},
   {false, {.i = 2000}},
   {false, {.i = 1000000}},
   {false, {.i = 2}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_JSON_LOG_ALLOCATIONS,
   PRM_NAME_JSON_LOG_ALLOCATIONS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_JSON_MAX_ARRAY_IDX,
   PRM_NAME_JSON_MAX_ARRAY_IDX,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 64 * ONE_K}},
   {false, {.i = 64 * ONE_K}},
   {false, {.i = ONE_M}},
   {false, {.i = ONE_K}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CONNECTION_LOGGING,
   PRM_NAME_CONNECTION_LOGGING,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_THREAD_LOGGING_FLAG,
   PRM_NAME_THREAD_LOGGING_FLAG,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_QUERY_LISTS,
   PRM_NAME_LOG_QUERY_LISTS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_THREAD_CONNECTION_POOLING,
   PRM_NAME_THREAD_CONNECTION_POOLING,
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_THREAD_CONNECTION_TIMEOUT_SECONDS,
   PRM_NAME_THREAD_CONNECTION_TIMEOUT_SECONDS,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 300}},
   {false, {.i = 300}},
   {false, {.i = 60 * 60 /* one hour */ }},
   {false, {.i = -1 /* infinite */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_THREAD_WORKER_POOLING,
   PRM_NAME_THREAD_WORKER_POOLING,
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_THREAD_WORKER_TIMEOUT_SECONDS,
   PRM_NAME_THREAD_WORKER_TIMEOUT_SECONDS,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 300}},
   {false, {.i = 300}},
   {false, {.i = 60 * 60 /* one hour */ }},
   {false, {.i = -1 /* infinite */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DWB_SIZE,
   PRM_NAME_DWB_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = (2 * 1024 * 1024) /* 2M */ }},
   {false, {.i = (2 * 1024 * 1024) /* 2M */ }},
   {false, {.i = (32 * 1024 * 1024) /* 32M */ }},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DWB_BLOCKS,
   PRM_NAME_DWB_BLOCKS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 2}},
   {false, {.i = 2}},
   {false, {.i = 32}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ENABLE_DWB_FLUSH_THREAD,
   PRM_NAME_ENABLE_DWB_FLUSH_THREAD,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DWB_LOGGING,
   PRM_NAME_DWB_LOGGING,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DATA_FILE_ADVISE,
   PRM_NAME_DATA_FILE_ADVISE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 6}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEBUG_LOG_ARCHIVES,
   PRM_NAME_DEBUG_LOG_ARCHIVES,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEBUG_ES,
   PRM_NAME_DEBUG_ES,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEBUG_BESTSPACE,
   PRM_NAME_DEBUG_BESTSPACE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEBUG_LOGWR,
   PRM_NAME_DEBUG_LOG_WRITER,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEBUG_AUTOCOMMIT,
   PRM_NAME_DEBUG_AUTOCOMMIT,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEBUG_REPLICATION_DATA,
   PRM_NAME_DEBUG_REPLICATION_DATA,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TRACK_REQUESTS,
   PRM_NAME_TRACK_REQUESTS,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_PGBUF_VICTIM_FLUSH,
   PRM_NAME_LOG_PGBUF_VICTIM_FLUSH,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_CHKPT_DETAILED,
   PRM_NAME_LOG_CHKPT_DETAILED,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IB_TASK_MEMSIZE,
   PRM_NAME_IB_TASK_MEMSIZE,
   (PRM_FOR_SERVER | PRM_SIZE_UNIT | PRM_HIDDEN),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 16 * ONE_M}},
   {false, {.bi = 16 * ONE_M}},
   {false, {.bi = 128 * ONE_M}}, {false, {.bi = ONE_K}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STATS_ON,
   PRM_NAME_STATS_ON,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOADDB_WORKER_COUNT,
   PRM_NAME_LOADDB_WORKER_COUNT,
   (PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 8}},
   {false, {.i = 8}},
   {false, {.i = 64}}, {false, {.i = 2}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PERF_TEST_MODE,
   PRM_NAME_PERF_TEST_MODE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_REPR_CACHE_LOG,
   PRM_NAME_REPR_CACHE_LOG,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ENABLE_NEW_LFHASH,
   PRM_NAME_ENABLE_NEW_LFHASH,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HEAP_INFO_CACHE_LOGGING,
   PRM_NAME_HEAP_INFO_CACHE_LOGGING,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TDE_KEYS_FILE_PATH,
   PRM_NAME_TDE_KEYS_FILE_PATH,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TDE_DEFAULT_ALGORITHM,
   PRM_NAME_TDE_DEFAULT_ALGORITHM,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = TDE_ALGORITHM_AES}},
   {false, {.i = TDE_ALGORITHM_AES}},
   {false, {.i = TDE_ALGORITHM_ARIA}}, {false, {.i = TDE_ALGORITHM_NONE}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ER_LOG_TDE,
   PRM_NAME_ER_LOG_TDE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_JAVA_STORED_PROCEDURE,
   PRM_NAME_JAVA_STORED_PROCEDURE,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_JAVA_STORED_PROCEDURE_PORT,
   PRM_NAME_JAVA_STORED_PROCEDURE_PORT,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 65535}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_JAVA_STORED_PROCEDURE_JVM_OPTIONS,
   PRM_NAME_JAVA_STORED_PROCEDURE_JVM_OPTIONS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_HIDDEN),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = (char *) ""}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_JAVA_STORED_PROCEDURE_DEBUG,
   PRM_NAME_JAVA_STORED_PROCEDURE_DEBUG,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_DEPRECATED | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_JAVA_STORED_PROCEDURE_UDS,
   PRM_NAME_JAVA_STORED_PROCEDURE_UDS,
   (PRM_FOR_SERVER | PRM_DEPRECATED | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ALLOW_TRUNCATED_STRING,
   PRM_NAME_ALLOW_TRUNCATED_STRING,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TB_DEFAULT_REUSE_OID,
   PRM_NAME_TB_DEFAULT_REUSE_OID,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SESSION | PRM_FOR_HA_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_USE_STAT_ESTIMATION,	/* not deleted for compatibility. */
   PRM_NAME_USE_STAT_ESTIMATION,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_DEPRECATED),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_IGNORE_TRAILING_SPACE,
   PRM_NAME_IGNORE_TRAILING_SPACE,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FORCE_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DDL_AUDIT_LOG,
   PRM_NAME_DDL_AUDIT_LOG,
   (PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DDL_AUDIT_LOG_SIZE,
   PRM_NAME_DDL_AUDIT_LOG_SIZE,
   (PRM_FOR_CLIENT | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 10485760ULL /* 10M */ }},
   {false, {.bi = 10485760ULL}},
   {false, {.bi = 2147483648ULL /* 2G */ }},
   {false, {.bi = 10485760ULL}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_SUPPLEMENTAL_LOG,
   PRM_NAME_SUPPLEMENTAL_LOG,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FORCE_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 2}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CDC_LOGGING_DEBUG,
   PRM_NAME_CDC_LOGGING_DEBUG,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_RECOVERY_PROGRESS_LOGGING_INTERVAL,
   PRM_NAME_RECOVERY_PROGRESS_LOGGING_INTERVAL,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 3600}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FIRST_LOG_PAGEID,	/* Except for QA or TEST purposes, never use it. */
   PRM_NAME_FIRST_LOG_PAGEID,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 0ULL}},
   {false, {.bi = 0ULL}},
   {false, {.bi = LOGPAGEID_MAX}},
   {false, {.bi = 0ULL}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TASK_GROUP,
   PRM_NAME_TASK_GROUP,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
#if defined (SERVER_MODE)
   {false, {.i = (int) cubthread::system_core_count ()}},
   {false, {.i = (int) cubthread::system_core_count ()}},
   {false, {.i = (int) cubthread::system_core_count ()}},
#else
   {false, {.i = 1}},
   {false, {.i = 1}},
   NULL_SYSPRM_PARAM_VALUE,
#endif
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FLASHBACK_TIMEOUT,
   PRM_NAME_FLASHBACK_TIMEOUT,
   (PRM_FOR_CLIENT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 300}},
   {false, {.i = 0}},
   {false, {.i = 3600}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FLASHBACK_MAX_TRANSACTION,
   PRM_NAME_FLASHBACK_MAX_TRANSACTION,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = INT_MAX}},
   {false, {.i = INT_MAX}},
   {false, {.i = INT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_FLASHBACK_WIN_SIZE,
   PRM_NAME_FLASHBACK_WIN_SIZE,
   (PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = INT_MAX}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_USE_USER_HOSTS,
   PRM_NAME_USE_USER_HOSTS,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_QMGR_MAX_QUERY_PER_TRAN,
   PRM_NAME_QMGR_MAX_QUERY_PER_TRAN,
   (PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100}},
   {false, {.i = INT_MAX}},
   {false, {.i = SHRT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_REGEXP_ENGINE,
   PRM_NAME_REGEXP_ENGINE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_KEYWORD,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = cubregex::engine_type::LIB_RE2}},
   {false, {.i = cubregex::engine_type::LIB_RE2}},
   {false, {.i = cubregex::engine_type::LIB_RE2}},
   {false, {.i = cubregex::engine_type::LIB_CPPSTD}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR,
   PRM_NAME_ORACLE_COMPAT_NUMBER_BEHAVIOR,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FORCE_SERVER | PRM_FOR_PL_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_TCP_PING_HOSTS,
   PRM_NAME_HA_TCP_PING_HOSTS,
   (PRM_FOR_CLIENT | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_PING_TIMEOUT,
   PRM_NAME_HA_PING_TIMEOUT,
   (PRM_FOR_CLIENT | PRM_RELOADABLE | PRM_FOR_HA | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   /* NOTE: It is difficult to determine an accurate default value for TCP connection timeout, so the default value of the connection_time system parameter is followed. */
   {false, {.i = 5}},
   {false, {.i = 5}},
   {false, {.i = INT_MAX / 1000 /* divided by msecs */ }},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STATDUMP_FORCE_ADD_INT_MAX,
   PRM_NAME_STATDUMP_FORCE_ADD_INT_MAX,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PL_TRANSACTION_CONTROL,
   PRM_NAME_PL_TRANSACTION_CONTROL,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SERVER | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_SQL_LOG_PATH,
   PRM_NAME_HA_SQL_LOG_PATH,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = NULL}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_VACUUM_OVFP_CHECK_DURATION,
   PRM_NAME_VACUUM_OVFP_CHECK_DURATION,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 45000}},
   {false, {.i = 45000}},
   {false, {.i = 600000}},
   {false, {.i = 1 /* 1 min */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) prm_msec_to_sec,
   (DUP_PRM_FUNC) prm_sec_to_msec},
  {PRM_ID_VACUUM_OVFP_CHECK_THRESHOLD,
   PRM_NAME_VACUUM_OVFP_CHECK_THRESHOLD,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1000}},
   {false, {.i = 1000}},
   {false, {.i = INT_MAX}},
   {false, {.i = 2}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DEDUPLICATE_KEY_LEVEL,
   PRM_NAME_DEDUPLICATE_KEY_LEVEL,
   // It is not specified in the manual that it can be changed in the session.
   (PRM_FOR_SERVER | PRM_FORCE_SERVER | PRM_FOR_CLIENT | PRM_FOR_SESSION | PRM_USER_CHANGE | PRM_FOR_HA_CONTEXT),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = DEDUPLICATE_KEY_LEVEL_SYSPARAM_DFLT}},
   {false, {.i = DEDUPLICATE_KEY_LEVEL_SYSPARAM_DFLT}},
   {false, {.i = DEDUPLICATE_KEY_LEVEL_SYSPARAM_MAX}},
   {false, {.i = DEDUPLICATE_KEY_LEVEL_SYSPARAM_MIN}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PRINT_INDEX_DETAIL,
   PRM_NAME_PRINT_INDEX_DETAIL,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HA_SQL_LOG_MAX_COUNT,
   PRM_NAME_HA_SQL_LOG_MAX_COUNT,
   (PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 2}},
   {false, {.i = 2}},
   {false, {.i = 5}},
   {false, {.i = 2}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_RECOVERY_REDO_MINIMUM_JOB_COUNT,
   PRM_NAME_RECOVERY_REDO_MINIMUM_JOB_COUNT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100}},
   {false, {.i = 100}},
   {false, {.i = INT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_RECOVERY_REDO_JOB_PERIOD_IN_SECS,
   PRM_NAME_RECOVERY_REDO_JOB_PERIOD_IN_SECS,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 1}},
   {false, {.i = 1}},
   {false, {.i = INT_MAX}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ENABLE_MEMORY_MONITORING,
   PRM_NAME_ENABLE_MEMORY_MONITORING,
   (PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_SUBQUERY_CACHE_SIZE,
   PRM_NAME_MAX_SUBQUERY_CACHE_SIZE,
   (PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 2 * 1024 * 1024 /* 2 MB */ }},
   {false, {.bi = 2 * 1024 * 1024 /* 2 MB */ }},
   {false, {.bi = 16 * 1024 * 1024 /* 16 MB */ }},
   {false, {.bi = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STORED_PROCEDURE,
   PRM_NAME_STORED_PROCEDURE,
   (PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STORED_PROCEDURE_PORT,
   PRM_NAME_STORED_PROCEDURE_PORT,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 0}},
   {false, {.i = 0}},
   {false, {.i = 65535}}, {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STORED_PROCEDURE_JVM_OPTIONS,
   PRM_NAME_STORED_PROCEDURE_JVM_OPTIONS,
   (PRM_FOR_SERVER),
   PRM_STRING,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.str = (char *) ""}},
   {false, {.str = (char *) ""}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STORED_PROCEDURE_DEBUG,
   PRM_NAME_STORED_PROCEDURE_DEBUG,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STORED_PROCEDURE_UDS,
   PRM_NAME_STORED_PROCEDURE_UDS,
   (PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STORED_PROCEDURE_DUMP_ICODE,
   PRM_NAME_STORED_PROCEDURE_DUMP_ICODE,
   (PRM_FOR_CLIENT | PRM_FOR_SESSION | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_FOR_PL_CONTEXT),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_STORED_PROCEDURE_RETURN_NUMERIC_SIZE,
   PRM_NAME_STORED_PROCEDURE_RETURN_NUMERIC_SIZE,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER_LIST,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.integer_list = (int *) prm_stored_procedure_return_numeric_size_default_arr}},
   {false, {.integer_list = NULL}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_DBLINK_AUTO_COMMIT,
   PRM_NAME_DBLINK_AUTO_COMMIT,
   (PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = true}},
   {false, {.b = true}},
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_ENABLE_JVM_HEAP_DUMP,
   PRM_NAME_ENABLE_JVM_HEAP_DUMP,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
#if defined(NDEBUG)
   {false, {.b = false}},
   {false, {.b = false}},
#else
   {false, {.b = true}},
   {false, {.b = true}},
#endif
   NULL_SYSPRM_PARAM_VALUE, NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PARALLELISM,
   PRM_NAME_PARALLELISM,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 4}},
   {false, {.i = 4}},
   {false, {.i = PRM_MAX_PARALLELISM}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MAX_PARALLEL_WORKERS,
   PRM_NAME_MAX_PARALLEL_WORKERS,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 100}},
   {false, {.i = 100}},
   {false, {.i = 1000}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PARALLEL_HEAP_SCAN_PAGE_THRESHOLD,
   PRM_NAME_PARALLEL_HEAP_SCAN_PAGE_THRESHOLD,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 2048}},
   {false, {.i = 2048}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PARALLEL_HASH_JOIN_PAGE_THRESHOLD,
   PRM_NAME_PARALLEL_HASH_JOIN_PAGE_THRESHOLD,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 2048}},
   {false, {.i = 2048}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PARALLEL_SORT_PAGE_THRESHOLD,
   PRM_NAME_PARALLEL_SORT_PAGE_THRESHOLD,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 2048}},
   {false, {.i = 2048}},
   {false, {.i = INT_MAX}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_KEEPALIVE_IDLE,
   PRM_NAME_TCP_KEEPALIVE_IDLE,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 300 /* 5 min */ }},
   {false, {.i = 300 /* 5 min */ }},
   {false, {.i = 60 * 60 * 24 * 365 /* 1 year */ }},
   {false, {.i = 60 /* 1 min */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_KEEPALIVE_INTERVAL,	/* probe interval */
   PRM_NAME_TCP_KEEPALIVE_INTERVAL,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 300 /* 5 min */ }},
   {false, {.i = 300 /* 5 min */ }},
   {false, {.i = 60 * 60 * 24 * 365 /* 1 year */ }},
   {false, {.i = 60 /* 1 min */ }},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TCP_KEEPALIVE_COUNT,	/* retry count */
   PRM_NAME_TCP_KEEPALIVE_COUNT,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 3}},
   {false, {.i = 3}},
   {false, {.i = 32}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_TASK_WORKER,
   PRM_NAME_TASK_WORKER,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = -1}},
   {false, {.i = -1}},
   {false, {.i = 1048576}},
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSS_MAX_CONNECTION_WORKER,
   PRM_NAME_CSS_MAX_CONNECTION_WORKER,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
#if defined (SERVER_MODE)
   {false, {.i = (int) cubthread::system_core_count () / 2}},
   {false, {.i = (int) cubthread::system_core_count () / 2}},
   {false, {.i = (int) cubthread::system_core_count ()}},
#else
   {false, {.i = 2}},
   {false, {.i = 2}},
   NULL_SYSPRM_PARAM_VALUE,
#endif
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSS_MIN_CONNECTION_WORKER,
   PRM_NAME_CSS_MIN_CONNECTION_WORKER,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 4}},
   {false, {.i = 4}},
#if defined (SERVER_MODE)
   {false, {.i = (int) cubthread::system_core_count ()}},
#else
   NULL_SYSPRM_PARAM_VALUE,
#endif
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSS_AUTO_SCALING_WINDOW_SIZE,
   PRM_NAME_CSS_AUTO_SCALING_WINDOW_SIZE,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 4}},
   {false, {.i = 4}},
#if defined (SERVER_MODE)
   {false, {.i = (int) cubthread::system_core_count ()}},
#else
   NULL_SYSPRM_PARAM_VALUE,
#endif
   {false, {.i = 1}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSS_RECV_BUDGET_PER_CONNECTION,
   PRM_NAME_CSS_RECV_BUDGET_PER_CONNECTION,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 16 * 1024}},	/* 16KB */
   {false, {.i = 16 * 1024}},	/* 16KB */
   {false, {.i = 1 * 1024 * 1024 * 1024}},	/* 1GB */
   {false, {.i = 0}},		/* no limit */
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_CSS_SEND_BUDGET_PER_CONNECTION,
   PRM_NAME_CSS_SEND_BUDGET_PER_CONNECTION,
   (PRM_FOR_SERVER),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 32 * 1024}},	/* 32KB */
   {false, {.i = 32 * 1024}},	/* 32KB */
   {false, {.i = 1 * 1024 * 1024 * 1024}},	/* 1GB */
   {false, {.i = 0}},		/* no limit */
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_PAGE_LATCH_TIMEOUT,
   PRM_NAME_PAGE_LATCH_TIMEOUT,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 300}},
   {false, {.i = 300}},
   {false, {.i = 3000}},
   {false, {.i = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_MEMOIZE_MEMORY_LIMIT,
   PRM_NAME_MEMOIZE_MEMORY_LIMIT,
   (PRM_USER_CHANGE | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_FOR_SESSION | PRM_FOR_QRY_STRING | PRM_SIZE_UNIT),
   PRM_BIGINT,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.bi = 2 * 1024 * 1024 /* 2 MB */ }},
   {false, {.bi = 2 * 1024 * 1024 /* 2 MB */ }},
   NULL_SYSPRM_PARAM_VALUE,
   {false, {.bi = 0}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_HOSTVAR_PEEKING,
   PRM_NAME_HOSTVAR_PEEKING,
   (PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.b = false}},
   {false, {.b = false}},
   NULL_SYSPRM_PARAM_VALUE,
   NULL_SYSPRM_PARAM_VALUE,
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
  {PRM_ID_LOG_POSTPONE_CACHE_SIZE,
   PRM_NAME_LOG_POSTPONE_CACHE_SIZE,
   (PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   PRM_CLEAR_DYNAMIC_FLAG,
   {false, {.i = 512}},
   {false, {.i = 512}},
   {false, {.i = 4096}},
   {false, {.i = 4}},
   (char *) NULL,
   (DUP_PRM_FUNC) NULL,
   (DUP_PRM_FUNC) NULL},
};

SYSPRM_INDIRECT_POS prm_Def_session_idx[DIM (prm_Def)];

static const int prm_Def_size = (int) (DIM (prm_Def));
#define MAX_SYSTEM_PARAMS  prm_Def_size

static int num_session_parameters = 0;
#define NUM_SESSION_PRM num_session_parameters

static const int *PARAM_VALUE_SHARE[] = {
  /*  This mapping table is intended to list other system parameters that need to share the same value 
   * when a specific system parameter is modified.
   * Rules:
   * 1) The first column of each row represents the number of columns in that row, excluding itself.
   * 2) Except for the last row, each row must contain at least 3 columns (i.e., the count and at least two PARAM_IDs).
   * 3) All columns in a row, except the first one, must be sorted in ascending order.
   * 4) The columns from the second onward in each row contain PARAM_ID values.
   * 5) The PARAM_IDs in each row must be consecutive. If they are not, adjust the PARAM_ID enum order and the definition order in prm_Def.
   * 6) The last row must be NULL.   
   *
   * For the logic behind these rules, refer to the sysprm_check_id_order() function.
   */
  // (int[]){n, V1, V2, ..., Vn},
  (int[]) {2, PRM_ID_SR_NBUFFERS, PRM_ID_SORT_BUFFER_SIZE},
  (int[]) {2, PRM_ID_PB_NBUFFERS, PRM_ID_PAGE_BUFFER_SIZE},
  (int[]) {2, PRM_ID_BT_OID_NBUFFERS, PRM_ID_BT_OID_BUFFER_SIZE},
  (int[]) {2, PRM_ID_LK_TIMEOUT_SECS, PRM_ID_LK_TIMEOUT},
  (int[]) {2, PRM_ID_LOG_NBUFFERS, PRM_ID_LOG_BUFFER_SIZE},
  (int[]) {2, PRM_ID_LOG_CHECKPOINT_NPAGES, PRM_ID_LOG_CHECKPOINT_SIZE},
  (int[]) {2, PRM_ID_LOG_CHECKPOINT_INTERVAL_SECS, PRM_ID_LOG_CHECKPOINT_INTERVAL},
  (int[]) {2, PRM_ID_INDEX_SCAN_KEY_BUFFER_PAGES, PRM_ID_INDEX_SCAN_KEY_BUFFER_SIZE},
  (int[]) {2, PRM_ID_PAGE_BG_FLUSH_INTERVAL_MSECS, PRM_ID_PAGE_BG_FLUSH_INTERVAL},
  (int[]) {2, PRM_ID_MAX_FLUSH_PAGES_PER_SECOND, PRM_ID_MAX_FLUSH_SIZE_PER_SECOND},
  (int[]) {2, PRM_ID_PB_SYNC_ON_NFLUSH, PRM_ID_PB_SYNC_ON_FLUSH_SIZE},
  (int[]) {2, PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS, PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL},
  (int[]) {2, PRM_ID_SQL_TRACE_SLOW_MSECS, PRM_ID_SQL_TRACE_SLOW},
  (int[]) {2, PRM_ID_JAVA_STORED_PROCEDURE, PRM_ID_STORED_PROCEDURE},
  (int[]) {2, PRM_ID_JAVA_STORED_PROCEDURE_PORT, PRM_ID_STORED_PROCEDURE_PORT},
  (int[]) {2, PRM_ID_JAVA_STORED_PROCEDURE_JVM_OPTIONS, PRM_ID_STORED_PROCEDURE_JVM_OPTIONS},
  (int[]) {2, PRM_ID_JAVA_STORED_PROCEDURE_DEBUG, PRM_ID_STORED_PROCEDURE_DEBUG},
  (int[]) {2, PRM_ID_JAVA_STORED_PROCEDURE_UDS, PRM_ID_STORED_PROCEDURE_UDS},
  NULL,				/* end of array */
};


#if defined (CS_MODE)
/*
 * Session parameters should be cached with the default values or the values
 * loaded from cubrid.conf file. When a new client connects to CAS, it should
 * reload these parameters (because some may be changed by previous clients)
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

static const KEYVAL boolean_words[] = {
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

static const KEYVAL er_log_level_words[] = {
  {"fatal", ER_FATAL_ERROR_SEVERITY},
  {"error", ER_ERROR_SEVERITY},
  {"syntax", ER_SYNTAX_ERROR_SEVERITY},
  {"warning", ER_WARNING_SEVERITY},
  {"notification", ER_NOTIFICATION_SEVERITY}
};

static const KEYVAL isolation_level_words[] = {
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

static const KEYVAL pgbuf_debug_page_validation_level_words[] = {
  {"fetch", PGBUF_DEBUG_PAGE_VALIDATION_FETCH},
  {"free", PGBUF_DEBUG_PAGE_VALIDATION_FREE},
  {"all", PGBUF_DEBUG_PAGE_VALIDATION_ALL}
};

static const KEYVAL null_words[] = {
  {"null", 0},
  {"0", 0}
};

static const KEYVAL ha_mode_words[] = {
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
  /* {HA_MODE_FAIL_OVER_STR, HA_MODE_FAIL_OVER}, *//* unused */
  {HA_MODE_FAIL_BACK_STR, HA_MODE_FAIL_BACK},
  /* {HA_MODE_LAZY_BACK_STR, HA_MODE_LAZY_BACK}, *//* not implemented yet */
  {HA_MODE_ROLE_CHANGE_STR, HA_MODE_ROLE_CHANGE},
  {"r", HA_MODE_REPLICA},
  {"repl", HA_MODE_REPLICA},
  {"replica", HA_MODE_REPLICA},
  {"2", HA_MODE_REPLICA}
};

static const KEYVAL ha_server_state_words[] = {
  {HA_SERVER_STATE_IDLE_STR, HA_SERVER_STATE_IDLE},
  {HA_SERVER_STATE_ACTIVE_STR, HA_SERVER_STATE_ACTIVE},
  {HA_SERVER_STATE_TO_BE_ACTIVE_STR, HA_SERVER_STATE_TO_BE_ACTIVE},
  {HA_SERVER_STATE_STANDBY_STR, HA_SERVER_STATE_STANDBY},
  {HA_SERVER_STATE_TO_BE_STANDBY_STR, HA_SERVER_STATE_TO_BE_STANDBY},
  {HA_SERVER_STATE_MAINTENANCE_STR, HA_SERVER_STATE_MAINTENANCE},
  {HA_SERVER_STATE_DEAD_STR, HA_SERVER_STATE_DEAD}
};

static const KEYVAL ha_log_applier_state_words[] = {
  {HA_LOG_APPLIER_STATE_UNREGISTERED_STR, HA_LOG_APPLIER_STATE_UNREGISTERED},
  {HA_LOG_APPLIER_STATE_RECOVERING_STR, HA_LOG_APPLIER_STATE_RECOVERING},
  {HA_LOG_APPLIER_STATE_WORKING_STR, HA_LOG_APPLIER_STATE_WORKING},
  {HA_LOG_APPLIER_STATE_DONE_STR, HA_LOG_APPLIER_STATE_DONE},
  {HA_LOG_APPLIER_STATE_ERROR_STR, HA_LOG_APPLIER_STATE_ERROR}
};

static const KEYVAL compat_words[] = {
  {"cubrid", COMPAT_CUBRID},
  {"default", COMPAT_CUBRID},
  {"mysql", COMPAT_MYSQL},
  {"oracle", COMPAT_ORACLE}
};

static const KEYVAL check_peer_alive_words[] = {
  {"none", CSS_CHECK_PEER_ALIVE_NONE},
  {"server_only", CSS_CHECK_PEER_ALIVE_SERVER_ONLY},
  {"client_only", CSS_CHECK_PEER_ALIVE_CLIENT_ONLY},
  {"both", CSS_CHECK_PEER_ALIVE_BOTH},
};

static const KEYVAL query_trace_format_words[] = {
  {"text", QUERY_TRACE_TEXT},
  {"json", QUERY_TRACE_JSON},
};

static const KEYVAL fi_test_words[] = {
  {"recovery", FI_GROUP_RECOVERY},
};

static const KEYVAL ha_repl_filter_type_words[] = {
  {"none", REPL_FILTER_NONE},
  {"include_table", REPL_FILTER_INCLUDE_TBL},
  {"exclude_table", REPL_FILTER_EXCLUDE_TBL}
};

static const KEYVAL tde_algorithm_words[] = {
  /* {"none", TDE_ALGORITHM_NONE}, */
  {"aes", TDE_ALGORITHM_AES},
  {"aria", TDE_ALGORITHM_ARIA}
};

/* *INDENT-OFF* */
using namespace cubregex;
static const KEYVAL regexp_engine_words[] = {
  {get_engine_name(engine_type::LIB_CPPSTD), engine_type::LIB_CPPSTD},
  {get_engine_name(engine_type::LIB_RE2), engine_type::LIB_RE2}
};
/* *INDENT-ON* */

static const char *compat_mode_values_PRM_ANSI_QUOTES[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  "no",				/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE */
  PRM_NAME_ANSI_QUOTES
};

static const char *compat_mode_values_PRM_ORACLE_STYLE_EMPTY_STRING[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  NULL,				/* COMPAT_MYSQL */
  "yes",			/* COMPAT_ORACLE */
  PRM_NAME_ORACLE_STYLE_EMPTY_STRING
};

static const char *compat_mode_values_PRM_ORACLE_STYLE_OUTERJOIN[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  NULL,				/* COMPAT_MYSQL */
  "yes",			/* COMPAT_ORACLE */
  PRM_NAME_ORACLE_STYLE_OUTERJOIN
};

static const char *compat_mode_values_PRM_PIPES_AS_CONCAT[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  "no",				/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE */
  PRM_NAME_PIPES_AS_CONCAT
};

/* Oracle's trigger correlation names are not yet supported. */
static const char *compat_mode_values_PRM_MYSQL_TRIGGER_CORRELATION_NAMES[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  "yes",			/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE */
  PRM_NAME_MYSQL_TRIGGER_CORRELATION_NAMES
};

static const char *compat_mode_values_PRM_REQUIRE_LIKE_ESCAPE_CHARACTER[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  "yes",			/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE */
  PRM_NAME_REQUIRE_LIKE_ESCAPE_CHARACTER
};

static const char *compat_mode_values_PRM_NO_BACKSLASH_ESCAPES[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  "no",				/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE */
  PRM_NAME_NO_BACKSLASH_ESCAPES
};

static const char *compat_mode_values_PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  "yes",			/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE: leave it in cubrid mode for now */
  PRM_NAME_ADD_COLUMN_UPDATE_HARD_DEFAULT
};

static const char *compat_mode_values_PRM_RETURN_NULL_ON_FUNCTION_ERRORS[COMPAT_ORACLE + 2] = {
  NULL,				/* COMPAT_CUBRID */
  "yes",			/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE */
  PRM_NAME_RETURN_NULL_ON_FUNCTION_ERRORS
};

static const char *compat_mode_values_PRM_PLUS_AS_CONCAT[COMPAT_ORACLE + 2] = {
  "yes",			/* COMPAT_CUBRID */
  "no",				/* COMPAT_MYSQL */
  NULL,				/* COMPAT_ORACLE */
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

static int prm_print (const SYSPRM_PARAM * prm, char *buf, size_t len, PRM_PRINT_MODE print_mode,
		      PRM_PRINT_VALUE_MODE print_value_mode);
static int sysprm_load_and_init_internal (const char *db_name, const char *conf_file, bool reload,
					  const int load_flags);
static void prm_check_environment (void);
#if defined(_ENABLE_SYSTEM_PARAMS_CHECK_)
static int prm_check_parameters (void);
#endif
#if !defined (SERVER_MODE)
static SYSPRM_ERR sysprm_validate_escape_char_parameters (const SYSPRM_ASSIGN_VALUE * assignment_list);
#endif
static int prm_load_by_section (INI_TABLE * ini, const char *section, bool ignore_section, bool reload,
				const char *file, const int load_flags, bool ignore_case, bool is_common_section);
static int prm_read_and_parse_ini_file (const char *prm_file_name, const char *db_name, const bool reload,
					const int load_flags);
static void prm_report_bad_entry (const char *key, int line, int err, const char *where);
static SYSPRM_ERR sysprm_get_param_range (SYSPRM_PARAM * prm, void *min, void *max);
static int prm_check_range (SYSPRM_PARAM * prm, void *value);
static int prm_set (SYSPRM_PARAM * prm, const char *value, bool set_flag);
static int prm_set_force (SYSPRM_PARAM * prm, const char *value);
static int prm_set_default (SYSPRM_PARAM * prm);
static SYSPRM_PARAM *prm_find (const char *pname, const char *section);
static const KEYVAL *prm_keyword (int val, const char *name, const KEYVAL * tbl, int dim);
static void prm_tune_parameters (void);
static int prm_compound_has_changed (SYSPRM_PARAM * prm, bool set_flag);
static void prm_set_compound (SYSPRM_PARAM * param, const char **compound_param_values[], const int values_count,
			      bool set_flag);
static int prm_get_next_param_value (char **data, char **prm, char **value);
static int sysprm_compare_values (SYSPRM_PARAM * prm);
static void sysprm_set_sysprm_value_from_parameter (SYSPRM_VALUE * prm_value, SYSPRM_PARAM * prm);
static SESSION_PARAM *sysprm_alloc_session_parameters (void);
static SYSPRM_ERR sysprm_generate_new_value (SYSPRM_PARAM * prm, const char *value, bool check,
					     SYSPRM_VALUE * new_value);
static int sysprm_set_value (SYSPRM_PARAM * prm, SYSPRM_VALUE value, bool set_flag, bool duplicate);
static void sysprm_set_system_parameter_value (SYSPRM_PARAM * prm, SYSPRM_VALUE value, bool set_flag);
static int sysprm_print_sysprm_value (PARAM_ID prm_id, SYSPRM_VALUE value, char *buf, size_t len,
				      PRM_PRINT_MODE print_mode);

static void sysprm_update_flag_different (SYSPRM_PARAM * prm);
#if defined(UNUSED_FUNCTIONS)
static void sysprm_update_flag_allocated (SYSPRM_PARAM * prm);
#endif
static void sysprm_update_session_prm_flag_allocated (SESSION_PARAM * prm);

static void sysprm_clear_sysprm_value (SYSPRM_VALUE * value, SYSPRM_DATATYPE datatype);
static char *sysprm_pack_sysprm_value (char *ptr, SYSPRM_VALUE value, SYSPRM_DATATYPE datatype);
static int sysprm_packed_sysprm_value_length (SYSPRM_VALUE value, SYSPRM_DATATYPE datatype, int offset);
static char *sysprm_unpack_sysprm_value (char *ptr, SYSPRM_VALUE * value, SYSPRM_DATATYPE datatype);

static bool prm_set_default_internal (SYSPRM_PARAM * prm);
static int sysprm_set_value_internal (SYSPRM_PARAM * prm, SYSPRM_VALUE value, bool set_flag, bool duplicate);
static const int *sysprm_find_shared_system_parameter (SYSPRM_PARAM * prm);

#if defined (SERVER_MODE)
static SYSPRM_ERR sysprm_set_session_parameter_value (SESSION_PARAM * session_parameter, SYSPRM_VALUE value);
static SYSPRM_ERR sysprm_set_session_parameter_default (SESSION_PARAM * session_parameter, PARAM_ID prm_id);
#endif /* SERVER_MODE */

#if defined (CS_MODE)
static void sysprm_update_cached_session_param_val (const PARAM_ID prm_id);
#endif

#if defined (SA_MODE) || defined (SERVER_MODE)
static void init_server_timezone_parameter (void);
#endif

/* conf files that have been loaded */
#define MAX_NUM_OF_PRM_FILES_LOADED	(10)
typedef struct
{
  char *conf_path;
  char *db_name;
} conf_file_n_db_name_map;
struct prm_config_files_loaded
{
private:
  conf_file_n_db_name_map m_loaded[MAX_NUM_OF_PRM_FILES_LOADED];
  int m_used;

public:
    prm_config_files_loaded ()
  {
    m_used = 0;
  }

   ~prm_config_files_loaded ()
  {
    clear ();
  }

  void clear ()
  {
    assert (m_used >= 0 && m_used <= MAX_NUM_OF_PRM_FILES_LOADED);
    while (m_used > 0)
      {
	m_used--;
	if (m_loaded[m_used].conf_path)
	  {
	    free_and_init (m_loaded[m_used].conf_path);
	  }
	if (m_loaded[m_used].db_name)
	  {
	    free_and_init (m_loaded[m_used].db_name);
	  }
      }
  }

  void add (const char *conf_path, const char *db_name)
  {
    assert (conf_path != NULL);
    assert (m_used >= 0);

    for (int i = 0; i < m_used; i++)
      {
	if (m_loaded[i].conf_path != NULL && strcmp (conf_path, m_loaded[i].conf_path) == 0)
	  {
	    if (db_name)
	      {
		if (m_loaded[i].db_name != NULL && strcmp (db_name, m_loaded[i].db_name) == 0)
		  {
		    return;
		  }
	      }
	    else if (m_loaded[i].db_name == NULL)
	      {
		return;
	      }
	  }
      }

    if (m_used < MAX_NUM_OF_PRM_FILES_LOADED)
      {
	assert (m_loaded[m_used].conf_path == NULL);

	m_loaded[m_used].conf_path = strdup (conf_path);
	m_loaded[m_used].db_name = db_name ? strdup (db_name) : NULL;
	m_used++;
      }
  }

  void dump (FILE * fp)
  {
    assert (m_used >= 0 && m_used <= MAX_NUM_OF_PRM_FILES_LOADED);

    for (int i = 0; i < m_used; i++)
      {
	assert (m_loaded[i].conf_path != NULL);

	fprintf (fp, "# %s", m_loaded[i].conf_path);
	if (m_loaded[i].db_name)
	  {
	    fprintf (fp, " [@%s]\n", m_loaded[i].db_name);
	  }
	else
	  {
	    fprintf (fp, "\n");
	  }
      }
  }
};				// struct prm_config_files_loaded

static struct prm_config_files_loaded prm_file_has_been_loaded;

/*
 * sysprm_dump_parameters - Print out current system parameters
 *   return: none
 *   fp(in): file descriptor to save results
 *   pmarker(in): prefix marker
 *   in_flags(in): combination of bit flags you want to dump 
 *   if_cond(in): dumping condition of including flags (OR, AND)
 *   out_flags(in): combination of bit flags that you want to exclude from the dump
 *   of_cond(in): dumping condition of excluding flags (OR, AND)
 *   old_style(in): print in the old sytle
 */
void
sysprm_dump_parameters (FILE * fp, char pmarker, unsigned int in_flags, SYSPRM_DUMP_CONDITION if_cond,
			unsigned int out_flags, SYSPRM_DUMP_CONDITION of_cond, bool old_style)
{
  char buf[LINE_MAX], tmpbuf[LINE_MAX];
  int i;
  const SYSPRM_PARAM *prm;
  char dmarker;
  char *ptr;

  fprintf (fp, "#\n# cubrid.conf\n#\n\n");
  fprintf (fp, "# system parameters were loaded from the files ([@section])\n");

  prm_file_has_been_loaded.dump (fp);

  fprintf (fp, "\n# system parameters\n");
  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      prm = GET_PRM (i);

      if (PRM_IS_OBSOLETED (prm))
	{
	  continue;
	}

      /* skip matched excluding flags */
      if (of_cond == PRM_OR_CONDITION && (prm->static_flag & out_flags))
	{
	  continue;
	}
      else if (of_cond == PRM_AND_CONDITION && ((prm->static_flag & out_flags) == out_flags))
	{
	  continue;
	}

      /* skip unmatched including flags */
      if (if_cond == PRM_OR_CONDITION && !(prm->static_flag | in_flags))
	{
	  continue;
	}
      else if (if_cond == PRM_AND_CONDITION && ((prm->static_flag & in_flags) != in_flags))
	{
	  continue;
	}

      prm_print (prm, buf, LINE_MAX, PRM_PRINT_NAME, PRM_PRINT_CURR_VAL);
      prm_print (prm, tmpbuf, LINE_MAX, PRM_PRINT_NONE, PRM_PRINT_DEFAULT_VAL);

      /* check if it is different from the default value */
      dmarker = ' ';
      ptr = strchr (buf, '=');
      if (ptr != NULL && strcmp (ptr + 1, tmpbuf) != 0)
	{
	  dmarker = '*';
	}

      if (old_style)
	{
	  fprintf (fp, "%s\n", buf);
	}
      else
	{
	  fprintf (fp, "[%c%c] %s (%s)\n", pmarker, dmarker, buf, tmpbuf);
	}
    }
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

  if (db_name == NULL)
    {
      return;
    }

  if (PRM_IS_SET (GET_PRM (PRM_ID_ER_LOG_FILE)))
    {
      return;
    }

  strncpy_bufsize (local_db_name, db_name);
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
      snprintf (error_log_name, PATH_MAX - 1, "%s%c%s_%04d%02d%02d_%02d%02d.err", ER_LOG_FILE_DIR, PATH_SEPARATOR,
		base_db_name, log_tm_p->tm_year + 1900, log_tm_p->tm_mon + 1, log_tm_p->tm_mday, log_tm_p->tm_hour,
		log_tm_p->tm_min);
      prm_set (GET_PRM (PRM_ID_ER_LOG_FILE), error_log_name, true);
    }
}

#ifndef NDEBUG
static void
sysprm_check_id_order ()
{
  static bool is_initialized =[](){
    int i, k;
    const int *ptr;
    assert (GET_PRM (0)->id == PRM_FIRST_ID);
    for (i = 1; i < MAX_SYSTEM_PARAMS; i++)
      {
	assert (GET_PRM (i - 1)->id == ((GET_PRM (i)->id) - 1));
	assert (GET_PRM (i)->id == (PARAM_ID) i);
      }

    for (i = 0; PARAM_VALUE_SHARE[i] != NULL; i++)
      {
	ptr = PARAM_VALUE_SHARE[i];
	assert (ptr[0] >= 2);
	for (k = 1; k < ptr[0]; k++)
	  {
	    assert (ptr[k] < ptr[k + 1]);
	  }

	if (i > 0)
	  {
	    assert (ptr[1] > PARAM_VALUE_SHARE[i - 1][1]);
	  }
      }

    return true;
  }
  ();

  assert (is_initialized == true);
  /* Notice:
   * This ensures the variable is not optimized away, even though it does not change the functional logic of the code
   * Please do not delete the following two lines.
   */
  (void) is_initialized;	// Dummy Reference  
  *(volatile bool *) &is_initialized;
}
#endif

/*
 * sysprm_load_and_init_internal - Read system parameters from the init files
 *   return: NO_ERROR or ER_FAILED
 *   db_name(in): database name
 *   conf_file(in): config file
 *   reload(in):
 *   load_flags(in):
 *
 * Note: Parameters would be tuned and forced according to the internal rules.
 */
static int
sysprm_load_and_init_internal (const char *db_name, const char *conf_file, bool reload, const int load_flags)
{
  char *base_db_name = NULL;
  const char *file_path_ptr = NULL;
  char file_being_dealt_with[PATH_MAX];
  char local_db_name[DB_MAX_IDENTIFIER_LENGTH];
  int i;
  struct stat stat_buf;
  int r = NO_ERROR;
  char *s;
#if defined (CS_MODE)
  SESSION_PARAM *sprm = NULL;
  int num_session_prms;
#endif

  /* TODO: conf_file is always NULL. Therefore, you need to decide whether to keep this argument. */
  assert (conf_file == NULL);

#ifndef NDEBUG
  sysprm_check_id_order ();
#endif

  if (reload)
    {
      for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
	{
	  if (PRM_IS_RELOADABLE (GET_PRM (i)))
	    {
	      if (prm_set_default (GET_PRM (i)) != PRM_ERR_NO_ERROR)
		{
		  GET_PRM (i)->value = NULL_SYSPRM_PARAM_VALUE;
		}
	    }
	}
    }

  if (db_name == NULL)
    {
      /* initialize message catalog at here because there could be a code path that did not call msgcat_init() before */
      if (msgcat_init () != NO_ERROR)
	{
	  return ER_FAILED;
	}
      base_db_name = NULL;
    }
  else
    {
      strncpy_bufsize (local_db_name, db_name);
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
      file_path_ptr = conf_file;
    }
  else
    {
      envvar_confdir_file (file_being_dealt_with, PATH_MAX, sysprm_conf_file_name);
      file_path_ptr = file_being_dealt_with;
    }

  if (stat (file_path_ptr, &stat_buf) != 0)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, PRM_ERR_CANT_ACCESS),
	       file_path_ptr, strerror (errno));
    }
  else
    {
      r = prm_read_and_parse_ini_file (file_path_ptr, base_db_name, reload, load_flags | SYSPRM_IGNORE_HA);
      if (r != NO_ERROR)
	{
	  return r;
	}
    }

  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
    {
      /* use environment variable's value if exist */
      conf_file = envvar_get ("HA_CONF_FILE");
      if (conf_file != NULL && conf_file[0] != '\0')
	{
	  file_path_ptr = conf_file;
	}
      else
	{
	  envvar_confdir_file (file_being_dealt_with, PATH_MAX, sysprm_ha_conf_file_name);
	  file_path_ptr = file_being_dealt_with;
	}
      if (stat (file_path_ptr, &stat_buf) == 0)
	{
	  r = prm_read_and_parse_ini_file (file_path_ptr, NULL, reload, load_flags);
	  if (r != NO_ERROR)
	    {
	      return r;
	    }
	}
    }

  /*
   * If a parameter is not given, set it by default
   */
  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      if (!PRM_IS_SET (GET_PRM (i)) && !PRM_IS_OBSOLETED (GET_PRM (i)))
	{
	  if (prm_set_default (GET_PRM (i)) != PRM_ERR_NO_ERROR)
	    {
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, PRM_ERR_NO_VALUE),
		       GET_PRM (i)->name);
	      assert (0);
	      return ER_FAILED;
	    }
	}
    }

#if defined (SA_MODE) || defined (SERVER_MODE)
  init_server_timezone_parameter ();
#endif

  /*
   * Perform system parameter check and tuning.
   */
  prm_check_environment ();
  prm_tune_parameters ();
#if defined(_ENABLE_SYSTEM_PARAMS_CHECK_)
  if (prm_check_parameters () != NO_ERROR)
    {
      return ER_FAILED;
    }
#endif

  /*
   * Perform forced system parameter setting.
   */
  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      if (GET_PRM (i)->force_value)
	{
	  prm_set (GET_PRM (i), GET_PRM (i)->force_value, false);
	}
    }

#if 0
  if (envvar_get ("PARAM_DUMP"))
    {
      sysprm_dump_parameters (stdout, ' ', PRM_ALL_FLAGS, SYSPRM_OR_CONDITION, PRM_EMPTY_FLAG, SYSPRM_OR_CONDITION,
			      false);
    }
#endif

  intl_Mbs_support = prm_get_bool_value (PRM_ID_INTL_MBS_SUPPORT);
  intl_String_validation = prm_get_bool_value (PRM_ID_INTL_CHECK_INPUT_STRING);

  /* count the number of session parameters */
  if (num_session_parameters == 0)
    {
      for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
	{
	  prm_Def_session_idx[i] = (PRM_IS_FOR_SESSION (GET_PRM (i))) ? num_session_parameters++ : -1;
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
  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      if (PRM_IS_FOR_SESSION (GET_PRM (i)))
	{
	  assert (prm_Def_session_idx[i] == num_session_prms);
	  sprm = &cached_session_parameters[num_session_prms++];
	  sprm->prm_id = (PARAM_ID) i;
	  sprm->flag = (GET_PRM (i)->dynamic_flag);
	  sprm->datatype = GET_PRM (i)->datatype;
	  sysprm_set_sysprm_value_from_parameter (&sprm->value, GET_PRM (i));
	  sysprm_update_session_prm_flag_allocated (sprm);
	}
      else
	{
	  assert (prm_Def_session_idx[i] == -1);
	}
    }
#endif /* CS_MODE */

#if !defined(NDEBUG)
  /* verify flags are not incorrect or confusing */
  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      SYSPRM_PARAM *prm = GET_PRM (i);
      if (PRM_IS_FOR_SESSION (prm) && (!PRM_IS_FOR_CLIENT (prm) || !PRM_USER_CAN_CHANGE (prm)))
	{
	  /* session parameters can only be parameters for client that are changeable on-line */
	  assert (0);
	}
      if (PRM_IS_FOR_SESSION (prm) && PRM_IS_HIDDEN (prm))
	{
	  /* hidden parameters are not allowed to use PRM_FOR_SESSION flag */
	  assert (0);
	}
      if (PRM_CLIENT_SESSION_ONLY (prm) && (!PRM_IS_FOR_SERVER (prm) || !PRM_IS_FOR_SESSION (prm)))
	{
	  /* client session only makes sense if the parameter is for session and for server */
	  assert (0);
	}
      if (PRM_USER_CAN_CHANGE (prm) && PRM_TEST_CHANGE_ONLY (prm))
	{
	  /* do not set both parameters: USER_CHANGE: the user can change parameter value on-line TEST_CHANGE: for QA
	   * only */
	  assert (0);
	}
      if (PRM_IS_GET_SERVER (prm) && (PRM_IS_FOR_SESSION (prm) || PRM_IS_HIDDEN (prm)))
	{
	  /* session and hidden parameters are not allowed to use PRM_GET_SERVER flag */
	  assert (0);
	}
      if (PRM_IS_GET_SERVER (prm)
	  && (!PRM_IS_FOR_CLIENT (prm) || !PRM_IS_FOR_SERVER (prm) || !PRM_USER_CAN_CHANGE (prm)))
	{
	  /* Note that PRM_GET_SERVER flag only can be set if the parameter has PRM_FOR_CLIENT, PRM_FOR_SERVER, and
	   * PRM_USER_CHANGE flags. */
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
sysprm_load_and_init (const char *db_name, const char *conf_file, const int load_flags)
{
  return sysprm_load_and_init_internal (db_name, conf_file, false, load_flags);
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
  return sysprm_load_and_init_internal (db_name, conf_file, false, SYSPRM_LOAD_ALL);
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
  return sysprm_load_and_init_internal (db_name, conf_file, true, SYSPRM_LOAD_ALL);
}



static bool
prm_section_cmp (const char *key1, const char *key2, int key_len, bool ignore_case)
{
  const char *s1 = strchr (key1, ':');
  int len = (s1) ? CAST_STRLEN (s1 - key1) : strlen (key1);

  if (len != key_len)
    {
      return false;
    }

  if (ignore_case)
    {
      return (strncasecmp (key1, key2, key_len) == 0);
    }

  return (memcmp (key1, key2, key_len) == 0);
}

/*
 * prm_load_by_section - Set system parameters from a file
 *   return: void
 *   ini(in):
 *   section(in):
 *   ignore_section(in):
 *   reload(in):
 *   file(in):
 *   load_flags(in):
 *   ignore_case(in):
 */
static int
prm_load_by_section (INI_TABLE * ini, const char *section, bool ignore_section, bool reload, const char *file,
		     const int load_flags, bool ignore_case, bool is_common_section)
{
  int i, error;
  int sec_len;
  const char *sec_p;
  const char *key, *value;
  SYSPRM_PARAM *prm;
  int sec_key_len;

  sec_p = strchr (section, ':');
  sec_key_len = (sec_p) ? CAST_STRLEN (sec_p - section) : strlen (section);

  sec_p = (ignore_section) ? NULL : section;

  for (i = 0; i < ini->size; i++)
    {
      if (ini->key[i] == NULL || ini->val[i] == NULL)
	{
	  continue;
	}

      key = ini->key[i];
      value = ini->val[i];

      if (key[0] != ':')
	{
	  if (!prm_section_cmp (key, section, sec_key_len, ignore_case))
	    {
	      continue;
	    }
	  sec_len = sec_key_len + 1;
	}
      else if (ignore_section)
	{
	  sec_len = 1;
	}
      else
	{
	  continue;
	}

      prm = prm_find (key + sec_len, sec_p);
      if (prm == NULL)
	{
	  error = PRM_ERR_UNKNOWN_PARAM;
	  prm_report_bad_entry (key + sec_len, ini->lineno[i], error, file);
	  return error;
	}

#if defined (CS_MODE)
      if (PRM_IS_FOR_SERVER (prm) && !PRM_IS_FOR_CLIENT (prm))
	{
	  /* prm for only server */
	  continue;
	}
#endif /* CS_MODE */

#if defined (SERVER_MODE)
      if (PRM_IS_FOR_CLIENT (prm) && !PRM_IS_FOR_SERVER (prm))
	{
	  /* prm for only client */
	  continue;
	}
#endif /* SERVER_MODE */

      if (reload && !PRM_IS_RELOADABLE (prm))
	{
	  continue;
	}
      if (!SYSPRM_LOAD_IS_IGNORE_HA (load_flags) && !PRM_IS_FOR_HA (prm))
	{
	  continue;
	}

      if (PRM_IS_OBSOLETED (prm))
	{
	  continue;
	}

      if (PRM_IS_DEPRECATED (prm))
	{
	  prm_report_bad_entry (key + sec_len, ini->lineno[i], PRM_ERR_DEPRICATED, file);
	}

#if defined(SERVER_MODE)
      if (prm->id == PRM_ID_TIMEZONE)
	{
	  continue;
	}
#endif

      if (SYSPRM_LOAD_IS_IGNORE_INTL (load_flags)
	  && (prm->id == PRM_ID_INTL_CHECK_INPUT_STRING
	      || prm->id == PRM_ID_INTL_NUMBER_LANG || prm->id == PRM_ID_INTL_DATE_LANG
	      || prm->id == PRM_ID_INTL_COLLATION || prm->id == PRM_ID_INTL_MBS_SUPPORT
	      || prm->id == PRM_ID_SERVER_TIMEZONE || prm->id == PRM_ID_TIMEZONE))
	{
	  continue;
	}

#if !defined(SERVER_MODE)
      if (prm->id == PRM_ID_OPTIMIZATION_LEVEL)
	{
	  if (value != NULL)
	    {
	      int level;
	      if (parse_int (&level, value, 10) < 0 || CHECK_INVALID_OPTIMIZATION_LEVEL (level))
		{
		  error = PRM_ERR_BAD_VALUE;
		  prm_report_bad_entry (key + sec_len, ini->lineno[i], error, file);
		  return error;
		}
	    }
	}
#endif
      /* The contents of the cubrid_hosts.conf file associated with use_user_hosts are also considered system parameters. */
      if (prm->id == PRM_ID_USE_USER_HOSTS)
	{
	  if (value != NULL)
	    {
	      const KEYVAL *keyvalp = NULL;
	      keyvalp = prm_keyword (-1, value, boolean_words, DIM (boolean_words));
	      if (keyvalp != NULL && keyvalp->val == 1)
		{
		  if (validate_uhost_conf () == false)
		    {
		      return PRM_ERR_BAD_VALUE;
		    }
		}
	    }
	}

      if (prm->id == PRM_ID_SERVER_TIMEZONE)
	{
	  TZ_REGION tz_region_system;
	  int er_status = NO_ERROR;

	  if (value != NULL)
	    {
	      /* offset timezone can work without timezone lib */
	      er_status = tz_str_to_region (value, strlen (value), &tz_region_system);
	      if (er_status != NO_ERROR)
		{
		  /* we may have geographic region name, make sure timezone lib is loaded */
		  if (tz_load () != NO_ERROR)
		    {
		      return PRM_ERR_BAD_VALUE;
		    }
		  er_status = tz_str_to_region (value, strlen (value), &tz_region_system);
		}
	      if (er_status != NO_ERROR)
		{
		  error = PRM_ERR_BAD_VALUE;
		  prm_report_bad_entry (key + sec_len, ini->lineno[i], error, file);
		  return error;
		}
	      tz_set_tz_region_system (&tz_region_system);
	    }
	}

#if defined(SERVER_MODE) || defined(CS_MODE)
      if (prm->id == PRM_ID_SERVER_TIMEZONE || prm->id == PRM_ID_TIMEZONE)
	{
	  int er_status = NO_ERROR;

	  if (value != NULL)
	    {
	      /* offset timezone can work without timezone lib */
#if !defined(SERVER_MODE)
	      er_status = tz_str_to_region (value, strlen (value), tz_get_client_tz_region_session ());
#else
	      er_status = tz_str_to_region (value, strlen (value), NULL);
#endif
	      if (er_status != NO_ERROR)
		{
		  /* we may have geographic region name, make sure timezone lib is loaded */
		  if (tz_load () != NO_ERROR)
		    {
		      return PRM_ERR_BAD_VALUE;
		    }
#if !defined(SERVER_MODE)
		  er_status = tz_str_to_region (value, strlen (value), tz_get_client_tz_region_session ());
#else
		  er_status = tz_str_to_region (value, strlen (value), NULL);
#endif
		}
	      if (er_status != NO_ERROR)
		{
		  error = PRM_ERR_BAD_VALUE;
		  prm_report_bad_entry (key + sec_len, ini->lineno[i], error, file);
		  return error;
		}
	    }
	}
#endif

      if (is_common_section && (prm->id == PRM_ID_STORED_PROCEDURE_PORT))
	{
	  error = PRM_ERR_CANNOT_CHANGE;
	  prm_report_bad_entry (key + sec_len, ini->lineno[i], error, file);
	  return error;
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
 *   load_flags(in):
 */
static int
prm_read_and_parse_ini_file (const char *prm_file_name, const char *db_name, const bool reload, const int load_flags)
{
  INI_TABLE *ini;
  char sec_name[LINE_MAX];
  char host_name[CUB_MAXHOSTNAMELEN];
  char user_name[CUB_MAXHOSTNAMELEN];
  int error;

  ini = ini_parser_load (prm_file_name);
  if (ini == NULL)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, PRM_ERR_CANT_OPEN_INIT),
	       prm_file_name, strerror (errno));
      return PRM_ERR_FILE_ERR;
    }

  error = prm_load_by_section (ini, "common", true, reload, prm_file_name, load_flags, true, true);
  if (error == NO_ERROR && SYSPRM_LOAD_IS_IGNORE_HA (load_flags) && db_name != NULL && *db_name != '\0')
    {
      snprintf (sec_name, LINE_MAX, "@%s", db_name);
      error = prm_load_by_section (ini, sec_name, true, reload, prm_file_name, load_flags, false, false);
    }

#if defined (SA_MODE)
  if (error == NO_ERROR)
    {
      error = prm_load_by_section (ini, "standalone", true, reload, prm_file_name, load_flags, true, false);
    }
#endif /* SA_MODE */

  if (error == NO_ERROR && SYSPRM_LOAD_IS_IGNORE_HA (load_flags))
    {
      error = prm_load_by_section (ini, "service", false, reload, prm_file_name, load_flags, true, false);
    }
  if (error == NO_ERROR && !SYSPRM_LOAD_IS_IGNORE_HA (load_flags)
      && prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF && GETHOSTNAME (host_name, CUB_MAXHOSTNAMELEN) == 0)
    {
      snprintf (sec_name, LINE_MAX, "%%%s|*", host_name);
      error = prm_load_by_section (ini, sec_name, true, reload, prm_file_name, load_flags, true, false);
      if (error == NO_ERROR && getlogin_r (user_name, CUB_MAXHOSTNAMELEN) == 0)
	{
	  snprintf (sec_name, LINE_MAX, "%%%s|%s", host_name, user_name);
	  error = prm_load_by_section (ini, sec_name, true, reload, prm_file_name, load_flags, true, false);
	}
    }

  ini_parser_free (ini);

  prm_file_has_been_loaded.add (prm_file_name, db_name);

  return error;
}

/*
 * prm_size_to_io_pages -
 *   return: value
 */
static int
prm_size_to_io_pages (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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
prm_io_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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
prm_size_to_log_pages (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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
prm_log_pages_to_size (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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
  else if (out_type == PRM_INTEGER && in_type == PRM_INTEGER)
    {
      int *size_value = (int *) out_val;
      int page_value = *(int *) in_val;
      UINT64 t;

      t = ((UINT64) page_value) * LOG_PAGESIZE;
      if (t > INT_MAX)
	{
	  return PRM_ERR_BAD_VALUE;
	}

      *size_value = (int) t;
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
prm_msec_to_sec (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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
prm_sec_to_msec (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
{
  if (out_val == NULL || in_val == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (out_type == PRM_BIGINT && in_type == PRM_INTEGER)
    {
      UINT64 *msec_value = (UINT64 *) out_val;
      int sec_value = *(int *) in_val;

      *msec_value = ((UINT64) sec_value) * ONE_SEC;
    }
  else if (out_type == PRM_INTEGER && in_type == PRM_INTEGER)
    {
      int *msec_value = (int *) out_val;
      int sec_value = *(int *) in_val;
      UINT64 t;

      t = ((UINT64) sec_value) * ONE_SEC;
      if (t > INT_MAX)
	{
	  return PRM_ERR_BAD_VALUE;
	}

      *msec_value = (int) t;
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
prm_sec_to_min (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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
prm_min_to_sec (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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
prm_equal_to_ori (void *out_val, SYSPRM_DATATYPE out_type, void *in_val, SYSPRM_DATATYPE in_type)
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

#if defined(_ENABLE_SYSTEM_PARAMS_CHECK_)
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
  if (prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
    {
      char *node_list = prm_get_string_value (PRM_ID_HA_NODE_LIST);
      char *db_list = prm_get_string_value (PRM_ID_HA_DB_LIST);

      if (node_list == NULL || node_list[0] == 0 || db_list == NULL || db_list[0] == 0)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, PRM_ERR_NO_VALUE),
		   PRM_NAME_HA_NODE_LIST);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}
#endif

/*
 * prm_check_environment -
 *   return: none
 */
static void
prm_check_environment (void)
{
  int i;
  char buf[PRM_DEFAULT_BUFFER_SIZE];

  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      SYSPRM_PARAM *prm;
      const char *str;

      prm = GET_PRM (i);
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
 * sysprm_validate_escape_char_parameters () - validate escape char setting
 *
 * return                : SYSPRM_ERR
 * assignments_list (in) : list of assignments to validate.
 *
 * NOTE: To validate whether there will be conflictive settings between
 *       PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER and PRM_ID_NO_BACKSLASH_ESCAPES.
 *       Since both must not simultaneously be true.
 */
static SYSPRM_ERR
sysprm_validate_escape_char_parameters (const SYSPRM_ASSIGN_VALUE * assignment_list)
{
  SYSPRM_PARAM *prm = NULL;
  const SYSPRM_ASSIGN_VALUE *assignment = NULL;
  bool set_require_like_escape, set_no_backslash_escape;
  bool is_require_like_escape = false, is_no_backslash_escape = false;

  set_require_like_escape = set_no_backslash_escape = false;
  for (assignment = assignment_list; assignment != NULL; assignment = assignment->next)
    {
      if (assignment->prm_id == PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER)
	{
	  set_require_like_escape = true;
	  is_require_like_escape = assignment->value.b;
	}
      else if (assignment->prm_id == PRM_ID_NO_BACKSLASH_ESCAPES)
	{
	  set_no_backslash_escape = true;
	  is_no_backslash_escape = assignment->value.b;
	}
    }

  if (!set_require_like_escape && !set_no_backslash_escape)
    {
      return PRM_ERR_NO_ERROR;
    }

  if (!set_no_backslash_escape)
    {
      prm = GET_PRM (PRM_ID_NO_BACKSLASH_ESCAPES);
      is_no_backslash_escape = PRM_GET_BOOL (prm->value);
    }

  if (!set_require_like_escape)
    {
      prm = GET_PRM (PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER);
      is_require_like_escape = PRM_GET_BOOL (prm->value);
    }

  if (is_require_like_escape == true && is_no_backslash_escape == true)
    {
      return PRM_ERR_CANNOT_CHANGE;
    }

  return PRM_ERR_NO_ERROR;
}

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
sysprm_validate_change_parameters (const char *data, bool check, SYSPRM_ASSIGN_VALUE ** assignments_ptr)
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
      err = (SYSPRM_ERR) prm_get_next_param_value (&p, &name, &value);
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

      if (!check || PRM_USER_CAN_CHANGE (prm) || (PRM_TEST_CHANGE_ONLY (prm) && prm_get_bool_value (PRM_ID_TEST_MODE)))
	{
	  /* We allow changing the parameter value. */
	}
      else
	{
	  err = PRM_ERR_CANNOT_CHANGE;
	  break;
	}

      /* create a SYSPRM_CHANGE_VAL object */
      assign = (SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
      if (assign == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SYSPRM_ASSIGN_VALUE));
	  err = PRM_ERR_NO_MEM_FOR_PRM;
	  break;
	}
      err = sysprm_generate_new_value (prm, value, check, &assign->value);
      if (err != PRM_ERR_NO_ERROR)
	{
	  if (err == PRM_ERR_NOT_FOR_CLIENT || err == PRM_ERR_NOT_FOR_CLIENT_NO_AUTH)
	    {
	      /* update change_error */
	      if (change_error != PRM_ERR_NOT_FOR_CLIENT || err != PRM_ERR_NOT_FOR_CLIENT_NO_AUTH)
		{
		  /* do not replace change_error PRM_ERR_NOT_FOR_CLIENT with PRM_ERR_NOT_FOR_CLIENT_NO_AUTH */
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
      assign->prm_id = prm->id;
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
      err = sysprm_validate_escape_char_parameters (assignments);
    }

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
 *			   assignments created from data argument
 * buf_size(in)		 : size available in default_val_buf
 *
 */
SYSPRM_ERR
sysprm_make_default_values (const char *data, char *default_val_buf, const int buf_size)
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
      /* get parameter name and value */
      err = (SYSPRM_ERR) prm_get_next_param_value (&p, &name, &value);
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

      if (PRM_ID_INTL_COLLATION == prm->id)
	{
	  n = snprintf (out_p, remaining_size, "%s=%s", PRM_NAME_INTL_COLLATION,
			lang_get_collation_name (LANG_GET_BINARY_COLLATION (LANG_SYS_CODESET)));
	}
      else if (PRM_ID_INTL_DATE_LANG == prm->id)
	{
	  n = snprintf (out_p, remaining_size, "%s=%s", PRM_NAME_INTL_DATE_LANG, lang_get_Lang_name ());
	}
      else if (PRM_ID_INTL_NUMBER_LANG == prm->id)
	{
	  n = snprintf (out_p, remaining_size, "%s=%s", PRM_NAME_INTL_NUMBER_LANG, lang_get_Lang_name ());
	}
      else if (PRM_ID_TIMEZONE == prm->id)
	{
	  n = snprintf (out_p, remaining_size, "%s=%s", PRM_NAME_TIMEZONE, tz_get_system_timezone ());
	}
      else
	{
	  n = prm_print (prm, out_p, remaining_size, PRM_PRINT_NAME, PRM_PRINT_DEFAULT_VAL);
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
sysprm_change_parameter_values (const SYSPRM_ASSIGN_VALUE * assignments, bool check, bool set_flag)
{
  SYSPRM_PARAM *prm = NULL;
  for (; assignments != NULL; assignments = assignments->next)
    {
      prm = GET_PRM (assignments->prm_id);
#if defined (CS_MODE)
      if (check)
	{
	  if (!PRM_IS_FOR_CLIENT (prm))
	    {
	      /* skip this assignment */
	      continue;
	    }
	}
#endif
#if defined (SERVER_MODE)
      if (check)
	{
	  if (!PRM_IS_FOR_SERVER (prm) && !PRM_IS_FOR_SESSION (prm))
	    {
	      /* skip this assignment */
	      continue;
	    }
	}
#endif
      sysprm_set_value (prm, assignments->value, set_flag, true);
#if defined (SERVER_MODE)
      if (prm->id == PRM_ID_ACCESS_IP_CONTROL)
	{
	  css_set_accessible_ip_info ();
	}
#endif
    }
}

static int
prm_rewrite_int_list (int *int_list, char *left_side, char *buf, int len)
{
  int list_size, i;
  char *s;
  int n;

  if (int_list == NULL)
    {
      return snprintf (buf, len, "%s", left_side);
    }

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

  return strlen (buf);
}

static const KEYVAL *
prm_get_keyval (const SYSPRM_PARAM * prm, int value, const char *name)
{
  switch (prm->id)
    {
    case PRM_ID_ER_LOG_LEVEL:
      return prm_keyword (value, name, er_log_level_words, DIM (er_log_level_words));

    case PRM_ID_LOG_ISOLATION_LEVEL:
      return prm_keyword (value, name, isolation_level_words, DIM (isolation_level_words));

    case PRM_ID_PB_DEBUG_PAGE_VALIDATION_LEVEL:
      return prm_keyword (value, name, pgbuf_debug_page_validation_level_words,
			  DIM (pgbuf_debug_page_validation_level_words));

    case PRM_ID_HA_MODE:
    case PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY:
      return prm_keyword (value, name, ha_mode_words, DIM (ha_mode_words));

    case PRM_ID_HA_SERVER_STATE:
      return prm_keyword (value, name, ha_server_state_words, DIM (ha_server_state_words));

    case PRM_ID_HA_LOG_APPLIER_STATE:
      return prm_keyword (value, name, ha_log_applier_state_words, DIM (ha_log_applier_state_words));

    case PRM_ID_COMPAT_MODE:
      return prm_keyword (value, name, compat_words, DIM (compat_words));

    case PRM_ID_CHECK_PEER_ALIVE:
      return prm_keyword (value, name, check_peer_alive_words, DIM (check_peer_alive_words));

    case PRM_ID_QUERY_TRACE_FORMAT:
      return prm_keyword (value, name, query_trace_format_words, DIM (query_trace_format_words));

    case PRM_ID_FAULT_INJECTION_TEST:
      return prm_keyword (value, name, fi_test_words, DIM (fi_test_words));

    case PRM_ID_HA_REPL_FILTER_TYPE:
      return prm_keyword (value, name, ha_repl_filter_type_words, DIM (ha_repl_filter_type_words));

    case PRM_ID_TDE_DEFAULT_ALGORITHM:
      return prm_keyword (value, name, tde_algorithm_words, DIM (tde_algorithm_words));

    case PRM_ID_REGEXP_ENGINE:
      return prm_keyword (value, name, regexp_engine_words, DIM (regexp_engine_words));

    default:
      assert (false);
      break;
    }

  return NULL;
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
prm_print (const SYSPRM_PARAM * prm, char *buf, size_t len, PRM_PRINT_MODE print_mode,
	   PRM_PRINT_VALUE_MODE print_value_mode)
{
  int n = 0;
  int error = NO_ERROR;
  char left_side[PRM_DEFAULT_BUFFER_SIZE];
  const SYSPRM_PARAM_VALUE *prm_value;

  if (len == 0)
    {
      /* don't print anything */
      return 0;
    }

  memset (left_side, 0, PRM_DEFAULT_BUFFER_SIZE);

  assert (prm != NULL && buf != NULL && len > 0);

  if (PRM_DIFFERENT_UNIT (prm))
    {
      assert (prm->get_dup != NULL);
    }

  if (print_mode == PRM_PRINT_ID)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%d=", prm->id);
    }
  else if (print_mode == PRM_PRINT_NAME)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%s=", prm->name);
    }

  if (print_value_mode == PRM_PRINT_DEFAULT_VAL)
    {
      prm_value = &prm->default_value;
    }
  else
    {
      prm_value = &prm->value;
    }

  if (PRM_IS_INTEGER (prm))
    {
      int val = PRM_GET_INT (*prm_value);
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm) && !PRM_HAS_SIZE_UNIT (prm) && !PRM_HAS_TIME_UNIT (prm))
	{
	  PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm))
	{
	  UINT64 dup_val;
	  val = PRM_GET_INT (*prm_value);

	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &dup_val, &val, &error);
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

	  (void) util_byte_to_size_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else if (PRM_HAS_TIME_UNIT (prm))
	{
	  INT64 dup_val;
	  val = PRM_GET_INT (*prm_value);

	  if (PRM_DIFFERENT_UNIT (prm) && val >= 0)
	    {
	      UINT64 tmp_val;
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &tmp_val, &val, &error);
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

	  (void) util_msec_to_time_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%d", left_side, val);
	}
    }
  else if (PRM_IS_BIGINT (prm))
    {
      UINT64 val = PRM_GET_BIGINT (*prm_value);
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm))
	{
	  PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm))
	{
	  (void) util_byte_to_size_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%lld", left_side, (unsigned long long) val);
	}
    }
  else if (PRM_IS_BOOLEAN (prm))
    {
      n = snprintf (buf, len, "%s%c", left_side, (PRM_GET_BOOL (*prm_value) ? 'y' : 'n'));
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float val = PRM_GET_FLOAT (*prm_value);
      int left_side_len = strlen (left_side);

      if (PRM_DIFFERENT_UNIT (prm) && !PRM_HAS_SIZE_UNIT (prm))
	{
	  PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm))
	{
	  UINT64 dup_val;
	  val = PRM_GET_FLOAT (*prm_value);

	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_BIGINT (prm, &dup_val, &val, &error);
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

	  (void) util_byte_to_size_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%f", left_side, val);
	}
    }
  else if (PRM_IS_STRING (prm))
    {
      n = snprintf (buf, len, "%s\"%s\"", left_side, (PRM_GET_STRING (*prm_value) ? PRM_GET_STRING (*prm_value) : ""));
    }
  else if (PRM_IS_KEYWORD (prm))
    {
      const KEYVAL *keyvalp = NULL;

      switch (prm->id)
	{
	case PRM_ID_HA_REPL_FILTER_TYPE:
	case PRM_ID_TDE_DEFAULT_ALGORITHM:
	case PRM_ID_REGEXP_ENGINE:
	  keyvalp = prm_get_keyval (prm, PRM_GET_INT (prm->value), NULL);
	  break;
	default:
	  keyvalp = prm_get_keyval (prm, PRM_GET_INT (*prm_value), NULL);
	  break;
	}

      if (keyvalp)
	{
	  n = snprintf (buf, len, "%s\"%s\"", left_side, keyvalp->key);
	}
      else
	{
	  n = snprintf (buf, len, "%s%d", left_side, PRM_GET_INT (*prm_value));
	}
    }
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      n = prm_rewrite_int_list (PRM_GET_INTEGER_LIST (*prm_value), left_side, buf, len);
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
sysprm_print_sysprm_value (PARAM_ID prm_id, SYSPRM_VALUE value, char *buf, size_t len, PRM_PRINT_MODE print_mode)
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

  if (PRM_DIFFERENT_UNIT (prm))
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

      if (PRM_DIFFERENT_UNIT (prm) && !PRM_HAS_SIZE_UNIT (prm) && !PRM_HAS_TIME_UNIT (prm))
	{
	  PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm))
	{
	  UINT64 dup_val;
	  val = value.i;

	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &dup_val, &val, &error);
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

	  (void) util_byte_to_size_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else if (PRM_HAS_TIME_UNIT (prm))
	{
	  INT64 dup_val;
	  val = value.i;

	  if (PRM_DIFFERENT_UNIT (prm) && val >= 0)
	    {
	      UINT64 tmp_val;
	      PRM_ADJUST_FOR_GET_INTEGER_TO_BIGINT (prm, &tmp_val, &val, &error);
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

	  (void) util_msec_to_time_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, dup_val);
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

      if (PRM_DIFFERENT_UNIT (prm))
	{
	  PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm))
	{
	  (void) util_byte_to_size_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%lld", left_side, (unsigned long long) val);
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

      if (PRM_DIFFERENT_UNIT (prm) && !PRM_HAS_SIZE_UNIT (prm))
	{
	  PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, &val, &val, &error);
	  if (error != NO_ERROR)
	    {
	      assert_release (false);
	      return n;
	    }
	}

      if (PRM_HAS_SIZE_UNIT (prm))
	{
	  UINT64 dup_val;
	  val = value.f;

	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_BIGINT (prm, &dup_val, &val, &error);
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

	  (void) util_byte_to_size_string (left_side + left_side_len, PRM_DEFAULT_BUFFER_SIZE - left_side_len, dup_val);
	  n = snprintf (buf, len, "%s", left_side);
	}
      else
	{
	  n = snprintf (buf, len, "%s%f", left_side, val);
	}
    }
  else if (PRM_IS_STRING (prm))
    {
      n = snprintf (buf, len, "%s\"%s\"", left_side, (value.str != NULL ? value.str : ""));
    }
  else if (PRM_IS_KEYWORD (prm))
    {
      const KEYVAL *keyvalp = NULL;

      keyvalp = prm_get_keyval (prm, value.i, NULL);
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
      n = prm_rewrite_int_list (value.integer_list, left_side, buf, len);
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
SYSPRM_ERR
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
      else if (prm->value.is_null == true)
	{
	  error = PRM_ERR_NO_VALUE;
	  break;
	}

#if defined (CS_MODE)
      if (!PRM_IS_FOR_CLIENT (prm) && !PRM_IS_FOR_SERVER (prm))
	{
	  error = PRM_ERR_CANNOT_CHANGE;
	  break;
	}

      if ((PRM_IS_FOR_SERVER (prm) && !PRM_IS_FOR_CLIENT (prm)) || PRM_IS_GET_SERVER (prm))
	{
	  /* have to read the value on server */
	  scope_error = PRM_ERR_NOT_FOR_CLIENT;
	}
#endif /* CS_MODE */

      /* create a SYSPRM_ASSING_VALUE object to store parameter value */
      prm_value = (SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
      if (prm_value == NULL)
	{
	  error = PRM_ERR_NO_MEM_FOR_PRM;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SYSPRM_ASSIGN_VALUE));
	  break;
	}
      memset (prm_value, 0x00, sizeof (SYSPRM_ASSIGN_VALUE));
      prm_value->prm_id = prm->id;

#if defined (CS_MODE)
      if (PRM_IS_FOR_CLIENT (prm))
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

      if ((PRM_IS_FOR_SERVER (prm) && !PRM_IS_FOR_CLIENT (prm)) || PRM_IS_GET_SERVER (prm))
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

  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      prm = GET_PRM (i);
      if (PRM_GET_FROM_SERVER (prm))
	{
	  SYSPRM_ASSIGN_VALUE *change_val = (SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
	  if (change_val == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SYSPRM_ASSIGN_VALUE));
	      goto cleanup;
	    }
	  memset (change_val, 0x00, sizeof (SYSPRM_ASSIGN_VALUE));
	  change_val->prm_id = (PARAM_ID) i;
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
 */
void
xsysprm_dump_server_parameters (FILE * outfp, unsigned int in_flags, SYSPRM_DUMP_CONDITION if_cond,
				unsigned int out_flags, SYSPRM_DUMP_CONDITION of_cond, bool old_style)
{
  sysprm_dump_parameters (outfp, 'S', in_flags, if_cond, out_flags, of_cond, old_style);
}

/*
 * xsysprm_get_pl_context_parameters () - obtain values for parameters
 *					    marked as PRM_FOR_PL_CONTEXT
 *
 * return : list of values
 *
 */
SYSPRM_ASSIGN_VALUE *
xsysprm_get_pl_context_parameters (int flag)
{
  SYSPRM_ASSIGN_VALUE *pl_ctx_values = NULL, *last_assign = NULL;
  SYSPRM_PARAM *prm = NULL;
  int i;

  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      prm = GET_PRM (i);
      if (PRM_IS_FOR_PL_CONTEXT (prm) && (prm->static_flag & flag))
	{
	  SYSPRM_ASSIGN_VALUE *change_val = (SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
	  if (change_val == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SYSPRM_ASSIGN_VALUE));
	      goto cleanup;
	    }
	  memset (change_val, 0x00, sizeof (SYSPRM_ASSIGN_VALUE));
	  change_val->prm_id = (PARAM_ID) i;
	  sysprm_set_sysprm_value_from_parameter (&change_val->value, prm);
	  if (pl_ctx_values != NULL)
	    {
	      last_assign->next = change_val;
	      last_assign = change_val;
	    }
	  else
	    {
	      pl_ctx_values = last_assign = change_val;
	    }
	}
    }

  return pl_ctx_values;

cleanup:
  sysprm_free_assign_values (&pl_ctx_values);
  return NULL;
}
#endif /* !CS_MODE */

/*
 * sysprm_get_param_range - returns the minimum and maximum value for a SYSPRM_PARAM
 *   return: error code
 *   prm (in): the parameter for which we want the limits
 *   min (out): the minimum possible value for the parameter
 *   max (out): the maximum possible value for the parameter
 */
static SYSPRM_ERR
sysprm_get_param_range (SYSPRM_PARAM * prm, void *min, void *max)
{
  SYSPRM_ERR error = PRM_ERR_NO_ERROR;
  if (PRM_IS_INTEGER (prm))
    {
      if (prm->lower_limit.is_null == false)
	{
	  *((int *) min) = PRM_GET_INT (prm->lower_limit);
	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, (int *) min, (int *) min, &error);
	    }
	}
      else
	{
	  *((int *) min) = INT_MIN;
	}

      if (prm->upper_limit.is_null == false)
	{
	  *((int *) max) = PRM_GET_INT (prm->upper_limit);
	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_INTEGER_TO_INTEGER (prm, (int *) max, (int *) max, &error);
	    }
	}
      else
	{
	  *((int *) max) = INT_MAX;
	}
    }
  else if (PRM_IS_FLOAT (prm))
    {
      if (prm->lower_limit.is_null == false)
	{
	  *((float *) min) = PRM_GET_FLOAT (prm->lower_limit);
	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, (float *) min, (float *) min, &error);
	    }
	}
      else
	{
	  *((float *) min) = FLT_MIN;
	}

      if (prm->upper_limit.is_null == false)
	{
	  *((float *) max) = PRM_GET_FLOAT (prm->upper_limit);
	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_FLOAT_TO_FLOAT (prm, (float *) max, (float *) max, &error);
	    }
	}
      else
	{
	  *((float *) max) = FLT_MAX;
	}
    }
  else if (PRM_IS_BIGINT (prm))
    {
      if (prm->lower_limit.is_null == false)
	{
	  *((UINT64 *) min) = PRM_GET_BIGINT (prm->lower_limit);
	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, (UINT64 *) min, (UINT64 *) min, &error);
	    }
	}
      else
	{
	  *((UINT64 *) min) = 0ULL;
	}

      if (prm->upper_limit.is_null == false)
	{
	  *((UINT64 *) max) = PRM_GET_BIGINT (prm->upper_limit);
	  if (PRM_DIFFERENT_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_GET_BIGINT_TO_BIGINT (prm, (UINT64 *) max, (UINT64 *) max, &error);
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
 * sysprm_get_range - returns the minimum and maximum value
 *                    for a paramter, given by its name
 *   return: error code
 *   param_id (in): parameter id
 *   min (out): the minimum possible value for the parameter
 *   max (out): the maximum possible value for the parameter
 */
int
sysprm_get_range (PARAM_ID param_id, void *min, void *max)
{
  if (param_id < PRM_FIRST_ID || param_id > PRM_LAST_ID)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (PRM_DIFFERENT_UNIT (GET_PRM (param_id)))
    {
      assert (GET_PRM (param_id)->get_dup != NULL);
    }
  return sysprm_get_param_range (GET_PRM (param_id), min, max);
}

/*
 * sysprm_check_range -
 *   return:
 *   param_id (in): parameter id
 *   value (in): parameter value
 */
int
sysprm_check_range (PARAM_ID param_id, void *value)
{
  if (param_id < PRM_FIRST_ID || param_id > PRM_LAST_ID)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  return prm_check_range (GET_PRM (param_id), value);
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

  if (PRM_DIFFERENT_UNIT (prm))
    {
      assert (prm->set_dup != NULL);
    }

  if (PRM_IS_INTEGER (prm) || PRM_IS_KEYWORD (prm))
    {
      int val;

      if (PRM_DIFFERENT_UNIT (prm))
	{
	  if (PRM_HAS_SIZE_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val, (UINT64 *) value, &error);
	    }
	  else if (PRM_HAS_TIME_UNIT (prm))
	    {
	      INT64 *dup_val = (INT64 *) value;

	      if (*dup_val >= 0)
		{
		  UINT64 tmp_val = (UINT64) * dup_val;
		  PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val, &tmp_val, &error);
		}
	      else
		{
		  val = (int) *dup_val;
		}
	    }
	  else
	    {
	      PRM_ADJUST_FOR_SET_INTEGER_TO_INTEGER (prm, &val, (int *) value, &error);
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

      if ((prm->upper_limit.is_null == false && PRM_GET_INT (prm->upper_limit) < val)
	  || (prm->lower_limit.is_null == false && PRM_GET_INT (prm->lower_limit) > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float val;
      float lower, upper;

      lower = upper = 0;	/* to make compilers be silent */
      if (PRM_DIFFERENT_UNIT (prm))
	{
	  if (PRM_HAS_SIZE_UNIT (prm))
	    {
	      PRM_ADJUST_FOR_SET_BIGINT_TO_FLOAT (prm, &val, (UINT64 *) value, &error);
	    }
	  else
	    {
	      PRM_ADJUST_FOR_SET_FLOAT_TO_FLOAT (prm, &val, (float *) value, &error);
	    }
	  if (error != NO_ERROR)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }

	  if (prm->upper_limit.is_null == false)
	    {
	      upper = ceilf (PRM_GET_FLOAT (prm->upper_limit) * 100) / 100;
	    }

	  if (prm->lower_limit.is_null == false)
	    {
	      lower = ceilf (PRM_GET_FLOAT (prm->lower_limit) * 100) / 100;
	    }
	}
      else
	{
	  val = *((float *) value);

	  if (prm->upper_limit.is_null == false)
	    {
	      upper = PRM_GET_FLOAT (prm->upper_limit);
	    }

	  if (prm->lower_limit.is_null == false)
	    {
	      lower = PRM_GET_FLOAT (prm->lower_limit);
	    }
	}

      if ((prm->upper_limit.is_null == false && upper < val) || (prm->lower_limit.is_null == false && lower > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}
    }
  else if (PRM_IS_BIGINT (prm))
    {
      UINT64 val;

      if (PRM_DIFFERENT_UNIT (prm))
	{
	  PRM_ADJUST_FOR_SET_BIGINT_TO_BIGINT (prm, &val, (UINT64 *) value, &error);
	  if (error != NO_ERROR)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }
	}
      else
	{
	  val = *((UINT64 *) value);
	}

      if ((prm->upper_limit.is_null == false && PRM_GET_BIGINT (prm->upper_limit) < val)
	  || (prm->lower_limit.is_null == false && PRM_GET_BIGINT (prm->lower_limit) > val))
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
sysprm_generate_new_value (SYSPRM_PARAM * prm, const char *value, bool check, SYSPRM_VALUE * new_value)
{
  char *end = NULL;
  int error = NO_ERROR;
  int set_min = 0, set_max = 0, set_default = 0;
  SYSPRM_ERR ret = PRM_ERR_NO_ERROR;
  SYSPRM_VALUE min, max;

  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }
  if (value == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  assert (new_value != NULL);

  if (PRM_DIFFERENT_UNIT (prm))
    {
      assert (prm->set_dup != NULL);
    }

#if defined (CS_MODE)
  if (check)
    {
      /* check the scope of parameter */
      if (PRM_IS_FOR_CLIENT (prm))
	{
	  if (PRM_IS_FOR_SESSION (prm))
	    {
	      /* the value in session state must also be updated. user doesn't have to be part of DBA group. */
	      ret = PRM_ERR_NOT_FOR_CLIENT_NO_AUTH;
	    }
	  else if (PRM_IS_FOR_SERVER (prm))
	    {
	      /* the value has to be changed on server too. user has to be part of DBA group. */
	      ret = PRM_ERR_NOT_FOR_CLIENT;
	    }
	}
      else
	{
	  if (PRM_IS_FOR_SERVER (prm))
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
      if (!PRM_IS_FOR_SERVER (prm) && !PRM_IS_FOR_SESSION (prm))
	{
	  return PRM_ERR_NOT_FOR_SERVER;
	}
    }
#endif /* SERVER_MODE */

  if (strcasecmp (value, PRM_VALUE_DEFAULT) == 0)
    {
      set_default = true;
    }
  else if (strcasecmp (value, PRM_VALUE_MAX) == 0)
    {
      set_max = true;
    }
  else if (strcasecmp (value, PRM_VALUE_MIN) == 0)
    {
      set_min = true;
    }
#if defined(CS_MODE)
  if (!set_default)
    {
      if (prm->id == PRM_ID_INTL_NUMBER_LANG || prm->id == PRM_ID_INTL_DATE_LANG)
	{
	  INTL_LANG dummy;

	  if (lang_get_lang_id_from_name (value, &dummy) != 0)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }
	}
      if (prm->id == PRM_ID_INTL_COLLATION)
	{
	  LANG_COLLATION *lc = NULL;
	  if (value != NULL)
	    {
	      lc = lang_get_collation_by_name (value);
	    }

	  if (lc == NULL)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }
	}
      if (prm->id == PRM_ID_TIMEZONE)
	{
	  int er_status = NO_ERROR;

	  if (value != NULL)
	    {
	      er_status = tz_str_to_region (value, strlen (value), NULL);
	      if (er_status != NO_ERROR)
		{
		  return PRM_ERR_BAD_VALUE;
		}
	    }
	}
    }
#endif
  if (set_max || set_min)
    {
      ret = sysprm_get_param_range (prm, &min, &max);
      if (ret != PRM_ERR_NO_ERROR)
	{
	  return PRM_ERR_BAD_VALUE;
	}
      if (set_min)
	{
	  *new_value = min;
	}
      else
	{
	  *new_value = max;
	}
      return PRM_ERR_NO_ERROR;
    }
  switch (prm->datatype)
    {
    case PRM_INTEGER:
      {
	/* convert string to int */
	int val = 0;

	if (set_default)
	  {
	    new_value->i = PRM_GET_INT (prm->default_value);
	    break;
	  }

	if (PRM_HAS_SIZE_UNIT (prm))
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

	    if (PRM_DIFFERENT_UNIT (prm))
	      {
		PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val, &dup_val, &error);
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
	else if (PRM_HAS_TIME_UNIT (prm))
	  {
	    INT64 dup_val;

	    if (util_time_string_to_msec (&dup_val, (char *) value) != NO_ERROR)
	      {
		return PRM_ERR_BAD_VALUE;
	      }

	    if (prm_check_range (prm, (void *) &dup_val) != NO_ERROR)
	      {
		return PRM_ERR_BAD_RANGE;
	      }

	    if (PRM_DIFFERENT_UNIT (prm) && dup_val >= 0)
	      {
		UINT64 tmp_val = (UINT64) dup_val;
		PRM_ADJUST_FOR_SET_BIGINT_TO_INTEGER (prm, &val, &tmp_val, &error);
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

	    if (PRM_DIFFERENT_UNIT (prm))
	      {
		PRM_ADJUST_FOR_SET_INTEGER_TO_INTEGER (prm, &val, &val, &error);
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

	if (set_default)
	  {
	    new_value->bi = PRM_GET_BIGINT (prm->default_value);
	    break;
	  }

	if (PRM_HAS_SIZE_UNIT (prm))
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

	if (PRM_DIFFERENT_UNIT (prm))
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
	float val = 0.f;

	if (set_default)
	  {
	    new_value->f = PRM_GET_FLOAT (prm->default_value);
	    break;
	  }

	if (PRM_HAS_SIZE_UNIT (prm))
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

	    if (PRM_DIFFERENT_UNIT (prm))
	      {
		PRM_ADJUST_FOR_SET_BIGINT_TO_FLOAT (prm, &val, &dup_val, &error);
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

	    if (PRM_DIFFERENT_UNIT (prm))
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
	const KEYVAL *keyvalp = NULL;

	if (set_default)
	  {
	    new_value->b = PRM_GET_BOOL (prm->default_value);
	    break;
	  }

	/* convert string to boolean */

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
	const char *val = NULL;

	if (set_default)
	  {
	    val = PRM_GET_STRING (prm->default_value);
	    if (val == NULL)
	      {
		switch (prm->id)
		  {
		  case PRM_ID_INTL_COLLATION:
		    val = lang_get_collation_name (LANG_GET_BINARY_COLLATION (LANG_SYS_CODESET));
		    break;
		  case PRM_ID_INTL_DATE_LANG:
		  case PRM_ID_INTL_NUMBER_LANG:
		    val = lang_get_Lang_name ();
		    break;
		  case PRM_ID_TIMEZONE:
		    val = prm_get_string_value (PRM_ID_SERVER_TIMEZONE);
		    break;
		  default:
		    /* do nothing */
		    break;
		  }
	      }

	    if (val == NULL)
	      {
		new_value->str = NULL;
	      }
	    else
	      {
		new_value->str = strdup (val);
		if (new_value->str == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (val)));
		    return PRM_ERR_NO_MEM_FOR_PRM;
		  }
	      }
	    break;
	  }

	/* check if the value is represented as a null keyword */
	if (prm_keyword (-1, value, null_words, DIM (null_words)) != NULL)
	  {
	    new_value->str = NULL;
	  }
	else
	  {
	    new_value->str = strdup (value);
	    if (new_value->str == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (value)));
		return PRM_ERR_NO_MEM_FOR_PRM;
	      }
	  }
	break;
      }

    case PRM_INTEGER_LIST:
      {
	/* convert string into an array of integers */
	int *val = NULL;

	if (set_default && prm->id != PRM_ID_CALL_STACK_DUMP_ACTIVATION)
	  {
	    val = PRM_GET_INTEGER_LIST (prm->default_value);
	    if (val == NULL)
	      {
		new_value->integer_list = NULL;
	      }
	    else
	      {
		size_t size = (size_t) ((val[0] + 1) * sizeof (int));
		new_value->integer_list = (int *) malloc (size);

		if (new_value->integer_list == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		    return PRM_ERR_NO_MEM_FOR_PRM;
		  }

		memcpy (new_value->integer_list, val, size);
	      }

	    break;
	  }

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

	    val = (int *) calloc (1024, sizeof (int));	/* max size is 1023 */
	    if (val == NULL)
	      {
		size_t size = 1024 * sizeof (int);
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
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
		    if (intl_mbs_casecmp ("default", p) == 0)	// TODO: strcasecmp
		      {
			if (prm->id == PRM_ID_CALL_STACK_DUMP_ACTIVATION)
			  {
			    memcpy (&val[list_size + 1], call_stack_dump_error_codes,
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

	if (prm->id == PRM_ID_STORED_PROCEDURE_RETURN_NUMERIC_SIZE)
	  {
	    /*  
	     *  The length of the parameter must be 2
	     *  Check the valid range
	     *    precision ( 1 ~ 38 ) and scale (0 ~ 38)
	     *    precision >= scale
	     */
	    if (val[0] != 2 || val[PRM_PRECISION] < 1 || val[PRM_PRECISION] > DB_MAX_NUMERIC_PRECISION
		|| val[PRM_SCALE] < 0 || val[PRM_SCALE] > val[PRM_PRECISION])
	      {
		free_and_init (val);
		return PRM_ERR_BAD_VALUE;
	      }
	  }

	new_value->integer_list = val;
	break;
      }

    case PRM_KEYWORD:
      {
	/* check if string can be identified as a keyword */
	int val;
	const KEYVAL *keyvalp = NULL;

	if (set_default)
	  {
	    new_value->i = PRM_GET_INT (prm->default_value);
	    break;
	  }

	keyvalp = prm_get_keyval (prm, -1, value);
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

	if ((prm->upper_limit.is_null == false && PRM_GET_INT (prm->upper_limit) < val)
	    || (prm->lower_limit.is_null == false && PRM_GET_INT (prm->lower_limit) > val))
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


static int
sysprm_set_value_internal (SYSPRM_PARAM * prm, SYSPRM_VALUE value, bool set_flag, bool duplicate)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *thread_p;
#endif
  void *tmp_ptr = NULL;

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
	      tmp_ptr = strdup (value.str);
	      if (tmp_ptr == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  (size_t) (strlen (value.str) + 1));
		  return PRM_ERR_NO_MEM_FOR_PRM;
		}

	      value.str = (char *) tmp_ptr;
	    }
	  break;

	case PRM_INTEGER_LIST:
	  if (value.integer_list != NULL)
	    {
	      int *integer_list = value.integer_list;
	      tmp_ptr = (int *) malloc ((integer_list[0] + 1) * sizeof (int));
	      if (tmp_ptr == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  (integer_list[0] + 1) * sizeof (int));
		  return PRM_ERR_NO_MEM_FOR_PRM;
		}

	      value.integer_list = (int *) tmp_ptr;
	      memcpy (value.integer_list, integer_list, (integer_list[0] + 1) * sizeof (int));
	    }

	default:
	  break;
	}
    }

#if defined (SERVER_MODE)
  if (PRM_IS_FOR_SESSION (prm) && BO_IS_SERVER_RESTARTED ())
    {
      SESSION_PARAM *param;
      TZ_REGION *session_tz_region;

      /* update session parameter */
      thread_p = thread_get_thread_entry_info ();

      param = session_get_session_parameter (thread_p, prm->id);
      if (param == NULL)
	{
	  if (tmp_ptr != NULL)
	    {
	      free (tmp_ptr);
	    }

	  return PRM_ERR_UNKNOWN_PARAM;
	}

      if (prm->id == PRM_ID_TIMEZONE)
	{
	  session_tz_region = session_get_session_tz_region (thread_p);
	  if (session_tz_region != NULL)
	    {
	      tz_str_to_region (value.str, strlen (value.str), session_tz_region);
	    }
	}

      SYSPRM_ERR err = sysprm_set_session_parameter_value (param, value);

      // err always returns PRM_ERR_NO_ERROR
      if (PRM_IS_FOR_PL_CONTEXT (prm))
	{
	  (void) session_set_pl_session_parameter (thread_p, prm->id);
	}

      return err;
    }

  /* if prm is not for session or if session_parameters have not been initialized just set the system parameter stored
   * on server */
#endif /* SERVER_MODE */

  sysprm_set_system_parameter_value (prm, value, set_flag);

  /* Set the cached parsed system timezone region on the server */
  if (prm->id == PRM_ID_SERVER_TIMEZONE)
    {
      TZ_REGION tz_region_system;
      int err_status = 0;

      err_status = tz_str_to_region (value.str, strlen (value.str), &tz_region_system);
      if (err_status != NO_ERROR)
	{
	  return PRM_ERR_BAD_PARAM;
	}
      tz_set_tz_region_system (&tz_region_system);
    }

  /* Set the cached parsed session timezone region on the client */
#if !defined(SERVER_MODE)
  if (prm->id == PRM_ID_TIMEZONE)
    {
      int err_status = 0;
      err_status = tz_str_to_region (value.str, strlen (value.str), tz_get_client_tz_region_session ());
      if (err_status != NO_ERROR)
	{
	  return PRM_ERR_BAD_PARAM;
	}
    }
#endif

  if (PRM_IS_COMPOUND (prm))
    {
      prm_compound_has_changed (prm, set_flag);
    }

  if (set_flag)
    {
      PRM_SET_BIT (PRM_SET, prm->dynamic_flag);
      /* Indicate that the default value was not used */
      PRM_CLEAR_BIT (PRM_DEFAULT_USED, prm->dynamic_flag);
    }

  return PRM_ERR_NO_ERROR;
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
sysprm_set_value (SYSPRM_PARAM * prm, SYSPRM_VALUE value, bool set_flag, bool duplicate)
{
  int ret = PRM_ERR_NO_ERROR;

  ret = sysprm_set_value_internal (prm, value, set_flag, duplicate);
  if (ret == PRM_ERR_NO_ERROR)
    {
      const PARAM_ID *ptr = (PARAM_ID *) sysprm_find_shared_system_parameter (prm);
      if (ptr)
	{
	  SYSPRM_VALUE tmp_value = value;
	  int max = *(int *) ptr;
	  for (int k = 1; k <= max; k++)
	    {
	      if (prm->id != ptr[k])
		{
		  ret = sysprm_set_value_internal (GET_PRM (ptr[k]), tmp_value, set_flag, true);
		  if (ret != PRM_ERR_NO_ERROR)
		    {
		      return ret;
		    }
		}
	    }
	}
    }

  return PRM_ERR_NO_ERROR;
}


static const int *
sysprm_find_shared_system_parameter (SYSPRM_PARAM * prm)
{
  const PARAM_ID *ptr = NULL;
  int i, k, max = 0;

  for (i = 0; PARAM_VALUE_SHARE[i] != NULL; i++)
    {
      max = PARAM_VALUE_SHARE[i][0];
      ptr = (PARAM_ID *) PARAM_VALUE_SHARE[i];

      if (prm->id < ptr[1])
	{
	  break;
	}
      else if (prm->id <= ptr[max])
	{
	  for (k = 1; k <= max; k++)
	    {
	      if (prm->id == ptr[k])
		{
		  return PARAM_VALUE_SHARE[i];
		}
	    }
	}
    }

  return NULL;
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
sysprm_set_system_parameter_value (SYSPRM_PARAM * prm, SYSPRM_VALUE value, bool set_flag)
{
  switch (prm->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      prm_set_integer_value (prm->id, value.i);
      break;

    case PRM_FLOAT:
      prm_set_float_value (prm->id, value.f);
      break;

    case PRM_BOOLEAN:
      prm_set_bool_value (prm->id, value.b);
      break;

    case PRM_STRING:
      prm_set_string_value (prm->id, value.str);
      break;

    case PRM_INTEGER_LIST:
      prm_set_integer_list_value (prm->id, value.integer_list);
      break;

    case PRM_BIGINT:
      prm_set_bigint_value (prm->id, value.bi);
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
  assert (PRM_IS_COMPOUND (prm));

  if (prm->id == PRM_ID_COMPAT_MODE)
    {
      prm_set_compound (prm, compat_mode_values, DIM (compat_mode_values), set_flag);
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
      free_and_init (prm->force_value);
    }

  prm->force_value = strdup (value);
  if (prm->force_value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (value) + 1));
      return PRM_ERR_NO_MEM_FOR_PRM;
    }

  return NO_ERROR;
}

static bool
prm_set_default_internal (SYSPRM_PARAM * prm)
{
  switch (prm->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
    case PRM_BIGINT:
    case PRM_BOOLEAN:
    case PRM_FLOAT:
      prm->value = prm->default_value;
      break;

    case PRM_STRING:
      if (PRM_IS_ALLOCATED (prm))
	{
	  char *str = PRM_GET_STRING (prm->value);
	  free_and_init (str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->dynamic_flag);
	}

      prm->value = prm->default_value;
      break;

    case PRM_INTEGER_LIST:
      if (PRM_IS_ALLOCATED (prm))
	{
	  int *int_list = PRM_GET_INTEGER_LIST (prm->value);

	  free_and_init (int_list);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->dynamic_flag);
	}
      prm->value = prm->default_value;
      break;

    default:
      return false;
    }

  /* Indicate that the default value was used */
  PRM_SET_BIT (PRM_DEFAULT_USED, prm->dynamic_flag);

  if (PRM_IS_FOR_QRY_STRING (prm))
    {
      PRM_CLEAR_BIT (PRM_DIFFERENT, prm->dynamic_flag);
    }

  return true;
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
  if (PRM_IS_FOR_SESSION (prm) && BO_IS_SERVER_RESTARTED ())
    {
      SESSION_PARAM *sprm = NULL;
      THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();

      sprm = session_get_session_parameter (thread_p, prm->id);
      if (sprm == NULL)
	{
	  return PRM_ERR_UNKNOWN_PARAM;
	}

      return sysprm_set_session_parameter_default (sprm, prm->id);
    }
#endif /* SERVER_MODE */

  if (prm == NULL)
    {
      return ER_FAILED;
    }

  if (!prm_set_default_internal (prm))
    {
      return ER_FAILED;
    }

  const PARAM_ID *ptr = (PARAM_ID *) sysprm_find_shared_system_parameter (prm);
  if (ptr)
    {
      int max = *(int *) ptr;
      for (int k = 1; k <= max; k++)
	{
	  if (prm->id != ptr[k])
	    {
	      if (!prm_set_default_internal (GET_PRM (ptr[k])))
		{
		  return ER_FAILED;
		}
	    }
	}
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
  int i;
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

  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      if (intl_mbs_casecmp (GET_PRM (i)->name, key) == 0)	// TODO: strcasecmp
	{
	  assert ((PARAM_ID) i == GET_PRM (i)->id);
	  return GET_PRM (i);
	}
    }

  return NULL;
}

/*
 * sysprm_set_force -
 *   return: NO_ERROR or error code
 *   param_id(in): parameter id to set
 *   pvalue(in): value to be set to the parameter
 */
int
sysprm_set_force (PARAM_ID param_id, const char *pvalue)
{
  if ((param_id < PRM_FIRST_ID || param_id > PRM_LAST_ID) || pvalue == NULL)
    {
      return ER_PRM_BAD_VALUE;
    }

  if (prm_set_force (GET_PRM (param_id), pvalue) != NO_ERROR)
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
sysprm_set_to_default (PARAM_ID param_id, bool set_to_force)
{
  SYSPRM_PARAM *prm;

  if (param_id < PRM_FIRST_ID || param_id > PRM_LAST_ID)
    {
      return ER_PRM_BAD_VALUE;
    }

  prm = GET_PRM (param_id);
  if (prm_set_default (prm) != NO_ERROR)
    {
      return ER_PRM_CANNOT_CHANGE;
    }

  if (set_to_force)
    {
      char val[LINE_MAX];
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_UNKNOWN_SYSPRM, 3, key, line, where);
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
  int i;

  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      prm = GET_PRM (i);
      if (PRM_IS_ALLOCATED (prm))
	{
	  switch (prm->datatype)
	    {
	    case PRM_STRING:
	      free_and_init (PRM_GET_STRING (prm->value));
	      PRM_CLEAR_BIT (PRM_ALLOCATED, prm->dynamic_flag);
	      break;

	    case PRM_INTEGER_LIST:
	      free_and_init (PRM_GET_INTEGER_LIST (prm->value));
	      PRM_CLEAR_BIT (PRM_ALLOCATED, prm->dynamic_flag);
	      break;

	    default:
	      /* do nothing */
	      break;
	    }
	}

      /* reset all dynamic flags */
      prm->dynamic_flag = 0;
    }

  prm_file_has_been_loaded.clear ();
}

#if defined (SA_MODE) || defined (SERVER_MODE)
static void
init_server_timezone_parameter (void)
{
  SYSPRM_PARAM *prm_server_timezone = GET_PRM (PRM_ID_SERVER_TIMEZONE);

  if (!PRM_IS_SET (prm_server_timezone))
    {
      char timezone_name[TZ_GENERIC_NAME_SIZE + 1];
      int ret;

      if (PRM_GET_STRING (prm_server_timezone->value))
	{
	  free_and_init (PRM_GET_STRING (prm_server_timezone->value));
	}
      PRM_CLEAR_BIT (PRM_ALLOCATED, prm_server_timezone->dynamic_flag);

      ret = tz_resolve_os_timezone (timezone_name, TZ_GENERIC_NAME_SIZE);
      if (ret < 0)
	{
	  if (tz_get_data () != NULL)
	    {
	      strcpy (timezone_name, "Asia/Seoul");
	    }
	  else
	    {
	      /* IANA timezone data not available, just use offset timezone */
	      strcpy (timezone_name, "+09:00");
	    }
	}
      prm_set (prm_server_timezone, timezone_name, true);
    }
}
#endif

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
  SYSPRM_PARAM *pb_aout_ratio_prm;
  SYSPRM_PARAM *max_clients_prm;
  SYSPRM_PARAM *max_plan_cache_entries_prm;
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
  SYSPRM_PARAM *test_mode_prm;
  SYSPRM_PARAM *tz_leap_second_support_prm;
  SYSPRM_PARAM *task_worker_prm;
  SYSPRM_PARAM *task_group_prm;
#if defined (SERVER_MODE)
  SYSPRM_PARAM *max_parallel_workers_prm;
  SYSPRM_PARAM *parallelism_prm;
  SYSPRM_PARAM *max_connection_workers_prm;
  SYSPRM_PARAM *min_connection_workers_prm;
#endif
  char newval[LINE_MAX];
  char host_name[CUB_MAXHOSTNAMELEN];
  int system_cpu_count;
  int task_worker;
  int max_clients;

  /* Find the parameters that require tuning */
  pb_aout_ratio_prm = GET_PRM (PRM_ID_PB_AOUT_RATIO);
  max_clients_prm = GET_PRM (PRM_ID_CSS_MAX_CLIENTS);
  max_plan_cache_entries_prm = GET_PRM (PRM_ID_XASL_CACHE_MAX_ENTRIES);
  query_cache_mode_prm = GET_PRM (PRM_ID_LIST_QUERY_CACHE_MODE);
  max_query_cache_entries_prm = GET_PRM (PRM_ID_LIST_MAX_QUERY_CACHE_ENTRIES);
  query_cache_size_in_pages_prm = GET_PRM (PRM_ID_LIST_MAX_QUERY_CACHE_PAGES);
  test_mode_prm = GET_PRM (PRM_ID_TEST_MODE);
  tz_leap_second_support_prm = GET_PRM (PRM_ID_TZ_LEAP_SECOND_SUPPORT);

  /* disable AOUT list until we fix CBRD-20741 */
  prm_set (pb_aout_ratio_prm, "0", false);

  ha_mode_prm = GET_PRM (PRM_ID_HA_MODE);
  ha_server_state_prm = GET_PRM (PRM_ID_HA_SERVER_STATE);
  auto_restart_server_prm = GET_PRM (PRM_ID_AUTO_RESTART_SERVER);
  log_background_archiving_prm = GET_PRM (PRM_ID_LOG_BACKGROUND_ARCHIVING);
  ha_node_list_prm = GET_PRM (PRM_ID_HA_NODE_LIST);
  max_log_archives_prm = GET_PRM (PRM_ID_LOG_MAX_ARCHIVES);
  force_remove_log_archives_prm = GET_PRM (PRM_ID_FORCE_REMOVE_LOG_ARCHIVES);

  /* Check that max clients has been set */
#if 0
  if (!(PRM_IS_SET (max_clients_prm)))
    {
      if (prm_set_default (max_clients_prm) != PRM_ERR_NO_ERROR)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, PRM_ERR_NO_VALUE),
		   max_clients_prm->name);
	  assert (0);
	  return;
	}
    }
  else
#endif
    {
      max_clients = MIN (CSS_MAX_CLIENT_COUNT, css_get_max_socket_fds ());
      if (PRM_GET_INT (max_clients_prm->value) > max_clients)
	{
	  sprintf (newval, "%d", max_clients);
	  (void) prm_set (max_clients_prm, newval, false);
	}

#if defined (SERVER_MODE)
      system_cpu_count = cubthread::system_core_count ();
      task_worker = css_get_max_connections ();
#else
      system_cpu_count = 1;
      task_worker = system_cpu_count * 6;
#endif

      task_worker_prm = GET_PRM (PRM_ID_TASK_WORKER);
      if (PRM_GET_INT (task_worker_prm->value) < 0)
	{
	  /* the value of task worker is default. */
	  sprintf (newval, "%d", task_worker);
	  (void) prm_set (task_worker_prm, newval, false);
	}

      task_group_prm = GET_PRM (PRM_ID_TASK_GROUP);
      if (PRM_GET_INT (task_group_prm->value) > system_cpu_count)
	{
	  sprintf (newval, "%d", system_cpu_count);
	  (void) prm_set (task_group_prm, newval, false);
	}
      if (PRM_GET_INT (task_group_prm->value) > PRM_GET_INT (task_worker_prm->value))
	{
	  sprintf (newval, "%d", PRM_GET_INT (task_worker_prm->value));
	  (void) prm_set (task_group_prm, newval, false);
	}

#if defined (SERVER_MODE)
      max_connection_workers_prm = GET_PRM (PRM_ID_CSS_MAX_CONNECTION_WORKER);
      min_connection_workers_prm = GET_PRM (PRM_ID_CSS_MIN_CONNECTION_WORKER);

      if (PRM_GET_INT (max_connection_workers_prm->value) > system_cpu_count)
	{
	  sprintf (newval, "%d", system_cpu_count);
	  (void) prm_set (max_connection_workers_prm, newval, false);
	}
      if (PRM_GET_INT (min_connection_workers_prm->value) > PRM_GET_INT (max_connection_workers_prm->value))
	{
	  sprintf (newval, "%d", PRM_GET_INT (max_connection_workers_prm->value));
	  (void) prm_set (min_connection_workers_prm, newval, false);
	}

      /* set parallelism to system_cpu_count if it is greater than cubthread::system_core_count () */
      parallelism_prm = GET_PRM (PRM_ID_PARALLELISM);
      if (PRM_GET_INT (parallelism_prm->value) > system_cpu_count)
	{
	  sprintf (newval, "%d", system_cpu_count);
	  (void) prm_set (parallelism_prm, newval, false);
	}

      /* set parallelism to max_parallel_workers if it is greater than max_parallel_workers */
      max_parallel_workers_prm = GET_PRM (PRM_ID_MAX_PARALLEL_WORKERS);
      if (PRM_GET_INT (parallelism_prm->value) > PRM_GET_INT (max_parallel_workers_prm->value))
	{
	  sprintf (newval, "%d", PRM_GET_INT (max_parallel_workers_prm->value));
	  (void) prm_set (parallelism_prm, newval, false);
	}

      if (PRM_GET_BOOL (test_mode_prm->value) == true)
	{
	  SYSPRM_PARAM *heap_scan_page_threshold_prm = GET_PRM (PRM_ID_PARALLEL_HEAP_SCAN_PAGE_THRESHOLD);
	  SYSPRM_PARAM *hash_join_page_threshold_prm = GET_PRM (PRM_ID_PARALLEL_HASH_JOIN_PAGE_THRESHOLD);
	  SYSPRM_PARAM *sort_page_threshold_prm = GET_PRM (PRM_ID_PARALLEL_SORT_PAGE_THRESHOLD);

	  if (PRM_GET_INT (heap_scan_page_threshold_prm->value) ==
	      PRM_GET_INT (heap_scan_page_threshold_prm->default_value))
	    {
	      sprintf (newval, "%d", 32);	/* TODO: 0 ? */
	      (void) prm_set (heap_scan_page_threshold_prm, newval, false);
	    }

	  if (PRM_GET_INT (hash_join_page_threshold_prm->value) ==
	      PRM_GET_INT (hash_join_page_threshold_prm->default_value))
	    {
	      sprintf (newval, "%d", 0);
	      (void) prm_set (hash_join_page_threshold_prm, newval, false);
	    }

	  if (PRM_GET_INT (sort_page_threshold_prm->value) == PRM_GET_INT (sort_page_threshold_prm->default_value))
	    {
	      sprintf (newval, "%d", 0);
	      (void) prm_set (sort_page_threshold_prm, newval, false);
	    }
	}
#endif
    }

  /* check Plan Cache and Query Cache parameters */
  if (max_plan_cache_entries_prm == NULL || query_cache_mode_prm == NULL || max_query_cache_entries_prm == NULL)
    {
      return;
    }

  if (PRM_GET_INT (max_plan_cache_entries_prm->value) == 0)
    {
      /* 0 means disable plan cache */
      (void) prm_set (max_plan_cache_entries_prm, "-1", false);
    }

  if (PRM_GET_INT (max_plan_cache_entries_prm->value) <= 0)
    {
      /* disable query cache mode by default */
      (void) prm_set (max_query_cache_entries_prm, 0, false);
      (void) prm_set (query_cache_size_in_pages_prm, 0, false);
    }

  /* check HA related parameters */
#if defined (SA_MODE) || defined (WINDOWS)
  /* we should save original PRM_HA_MODE value before tuning */
  prm_set_integer_value (PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY, prm_get_integer_value (PRM_ID_HA_MODE));

  /* reset to default 'active mode' */
  (void) prm_set_default (ha_mode_prm);
  (void) prm_set (ha_server_state_prm, HA_SERVER_STATE_ACTIVE_STR, false);

  if (force_remove_log_archives_prm != NULL && !PRM_GET_BOOL (force_remove_log_archives_prm->value))
    {
      (void) prm_set_default (max_log_archives_prm);
    }
#else /* SA_MODE || WINDOWS */
  if (PRM_GET_INT (ha_mode_prm->value) != HA_MODE_OFF)
    {
      if (PRM_DEFAULT_VAL_USED (ha_server_state_prm))
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

      if (force_remove_log_archives_prm != NULL && !PRM_GET_BOOL (force_remove_log_archives_prm->value))
	{
	  (void) prm_set_default (max_log_archives_prm);
	}
    }
#endif /* !SA_MODE && !WINDOWS */
  /* disable them temporarily */

  if (ha_node_list_prm == NULL || PRM_DEFAULT_VAL_USED (ha_node_list_prm))
    {
      if (GETHOSTNAME (host_name, sizeof (host_name)))
	{
	  strncpy (host_name, "localhost", sizeof (host_name) - 1);
	  er_clear ();
	}

      snprintf (newval, sizeof (newval) - 1, "%s@%s", host_name, host_name);
      prm_set (ha_node_list_prm, newval, false);
    }

  call_stack_dump_activation_prm = GET_PRM (PRM_ID_CALL_STACK_DUMP_ACTIVATION);
  if (!PRM_IS_SET (call_stack_dump_activation_prm))
    {
      int dim;
      int *integer_list = NULL;

      if (PRM_IS_ALLOCATED (call_stack_dump_activation_prm))
	{
	  free_and_init (PRM_GET_INTEGER_LIST (call_stack_dump_activation_prm->value));
	}

      dim = DIM (call_stack_dump_error_codes);
      integer_list = (int *) malloc ((dim + 1) * sizeof (int));
      if (integer_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (dim + 1) * sizeof (int));
	  return;
	}

      integer_list[0] = dim;
      memcpy (&integer_list[1], call_stack_dump_error_codes, dim * sizeof (int));
      prm_set_integer_list_value (PRM_ID_CALL_STACK_DUMP_ACTIVATION, integer_list);
      PRM_SET_BIT (PRM_SET, call_stack_dump_activation_prm->dynamic_flag);
      PRM_CLEAR_BIT (PRM_DEFAULT_USED, call_stack_dump_activation_prm->dynamic_flag);
    }

#if !defined(NDEBUG)
  SYSPRM_PARAM *fault_injection_ids_prm = GET_PRM (PRM_ID_FAULT_INJECTION_IDS);
  SYSPRM_PARAM *fault_injection_test_prm = GET_PRM (PRM_ID_FAULT_INJECTION_TEST);
  if (PRM_IS_SET (fault_injection_test_prm))
    {
      int group_code;
      int dim;
      int *integer_list = NULL;

      group_code = PRM_GET_INT (fault_injection_test_prm->value);

      if (PRM_IS_ALLOCATED (fault_injection_ids_prm))
	{
	  free_and_init (PRM_GET_INTEGER_LIST (fault_injection_ids_prm->value));
	}

      for (dim = 0; fi_Groups[group_code][dim] != FI_TEST_NONE; dim++)
	{
	  ;
	}

      integer_list = (int *) malloc ((dim + 1) * sizeof (int));
      if (integer_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (dim + 1) * sizeof (int));
	  return;
	}

      integer_list[0] = dim;
      memcpy (&integer_list[1], fi_Groups[group_code], dim * sizeof (int));
      prm_set_integer_list_value (PRM_ID_FAULT_INJECTION_IDS, integer_list);
      PRM_SET_BIT (PRM_SET, fault_injection_ids_prm->dynamic_flag);
      PRM_CLEAR_BIT (PRM_DEFAULT_USED, fault_injection_ids_prm->dynamic_flag);
    }
#endif

  if (PRM_GET_BOOL (test_mode_prm->value) == true)
    {
      tz_leap_second_support_prm->static_flag |= PRM_FOR_QRY_STRING;
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
  SYSPRM_PARAM *test_mode_prm;
  SYSPRM_PARAM *tz_leap_second_support_prm;

  char newval[LINE_MAX];
  char host_name[CUB_MAXHOSTNAMELEN];

  /* Find the parameters that require tuning */
  max_plan_cache_entries_prm = GET_PRM (PRM_ID_XASL_CACHE_MAX_ENTRIES);
  ha_node_list_prm = GET_PRM (PRM_ID_HA_NODE_LIST);

  ha_mode_prm = GET_PRM (PRM_ID_HA_MODE);
  ha_process_dereg_confirm_interval_in_msecs_prm = GET_PRM (PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS);
  ha_max_process_dereg_confirm_prm = GET_PRM (PRM_ID_HA_MAX_PROCESS_DEREG_CONFIRM);
  shutdown_wait_time_in_secs_prm = GET_PRM (PRM_ID_SHUTDOWN_WAIT_TIME_IN_SECS);
  ha_copy_log_timeout_prm = GET_PRM (PRM_ID_HA_COPY_LOG_TIMEOUT);
  ha_check_disk_failure_interval_prm = GET_PRM (PRM_ID_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS);
  test_mode_prm = GET_PRM (PRM_ID_TEST_MODE);
  tz_leap_second_support_prm = GET_PRM (PRM_ID_TZ_LEAP_SECOND_SUPPORT);

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

      ha_check_disk_failure_interval_value = PRM_GET_INT (ha_check_disk_failure_interval_prm->value);
      ha_copy_log_timeout_value = PRM_GET_INT (ha_copy_log_timeout_prm->value);
      if (ha_copy_log_timeout_value == -1)
	{
	  prm_set (ha_check_disk_failure_interval_prm, "0", false);
	}
      else if (ha_check_disk_failure_interval_value - ha_copy_log_timeout_value <
	       HB_MIN_DIFF_CHECK_DISK_FAILURE_INTERVAL_IN_SECS)
	{
	  ha_check_disk_failure_interval_value =
	    ha_copy_log_timeout_value + HB_MIN_DIFF_CHECK_DISK_FAILURE_INTERVAL_IN_SECS;
	  sprintf (newval, "%ds", ha_check_disk_failure_interval_value);
	  prm_set (ha_check_disk_failure_interval_prm, newval, false);
	}
    }

  /* disable them temporarily */

  if (PRM_DEFAULT_VAL_USED (ha_node_list_prm))
    {
      if (GETHOSTNAME (host_name, sizeof (host_name)))
	{
	  strncpy (host_name, "localhost", sizeof (host_name) - 1);
	  er_clear ();
	}

      snprintf (newval, sizeof (newval) - 1, "%s@%s", host_name, host_name);
      prm_set (ha_node_list_prm, newval, false);
    }

  assert (ha_mode_prm != NULL);

  if (PRM_GET_BOOL (test_mode_prm->value) == true)
    {
      tz_leap_second_support_prm->static_flag |= PRM_FOR_QRY_STRING;
    }

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
  if (sysprm_get_force_server_parameters (&force_server_values) == NO_ERROR && force_server_values != NULL)
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
  return prm_get_integer_value (PRM_ID_TCP_PORT_ID);
}

bool
prm_get_commit_on_shutdown (void)
{
  return prm_get_bool_value (PRM_ID_COMMIT_ON_SHUTDOWN);
  //return PRM_COMMIT_ON_SHUTDOWN;
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
prm_set_compound (SYSPRM_PARAM * param, const char **compound_param_values[], const int values_count, bool set_flag)
{
  int i = 0;
  const int param_value = PRM_GET_INT (param->value);
  const int param_upper_limit = PRM_GET_INT (param->upper_limit);

  assert (PRM_IS_INTEGER (param) || PRM_IS_KEYWORD (param));
  assert (0 == PRM_GET_INT (param->lower_limit));
  assert (param_value <= param_upper_limit);

  for (i = 0; i < values_count; ++i)
    {
      const char *compound_param_name = compound_param_values[i][param_upper_limit + 1];
      const char *compound_param_value = compound_param_values[i][param_value];

      assert (compound_param_name != NULL);

      if (compound_param_value == NULL)
	{
	  prm_set_default (prm_find (compound_param_name, NULL));
	}
      else
	{
	  prm_set (prm_find (compound_param_name, NULL), compound_param_value, set_flag);
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
      err = PRM_ERR_BAD_VALUE;
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

  return GET_PRM (prm_id)->name;
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
SYSPRM_VALUE *
prm_get_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
#if defined (SERVER_MODE)
  THREAD_ENTRY *thread_p;

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
#endif

  switch (GET_PRM (prm_id)->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
    case PRM_FLOAT:
    case PRM_BOOLEAN:
    case PRM_BIGINT:
    case PRM_STRING:
    case PRM_INTEGER_LIST:
      assert (GET_PRM (prm_id)->value.is_null == false);
      return &(GET_PRM (prm_id)->value.v);
    default:
      break;
    }

  assert (false);
  return NULL;
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
  assert (PRM_IS_INTEGER (GET_PRM (prm_id)) || PRM_IS_KEYWORD (GET_PRM (prm_id)));

  PRM_GET_INT (GET_PRM (prm_id)->value) = value;

  sysprm_update_flag_different (GET_PRM (prm_id));
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
  assert (PRM_IS_BOOLEAN (GET_PRM (prm_id)));

  PRM_GET_BOOL (GET_PRM (prm_id)->value) = value;

  sysprm_update_flag_different (GET_PRM (prm_id));
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
  assert (PRM_IS_FLOAT (GET_PRM (prm_id)));

  PRM_GET_FLOAT (GET_PRM (prm_id)->value) = value;

  sysprm_update_flag_different (GET_PRM (prm_id));
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
  assert (PRM_IS_STRING (GET_PRM (prm_id)));

  if (PRM_IS_ALLOCATED (GET_PRM (prm_id)))
    {
      free_and_init (PRM_GET_STRING (GET_PRM (prm_id)->value));
      PRM_CLEAR_BIT (PRM_ALLOCATED, (GET_PRM (prm_id)->dynamic_flag));
    }
  PRM_GET_STRING (GET_PRM (prm_id)->value) = value;
  if (PRM_GET_STRING (GET_PRM (prm_id)->value) != NULL)
    {
      PRM_SET_BIT (PRM_ALLOCATED, (GET_PRM (prm_id)->dynamic_flag));
    }

  sysprm_update_flag_different (GET_PRM (prm_id));
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
  assert (PRM_IS_INTEGER_LIST (GET_PRM (prm_id)));

  if (PRM_IS_ALLOCATED (GET_PRM (prm_id)))
    {
      free_and_init (PRM_GET_INTEGER_LIST (GET_PRM (prm_id)->value));
      PRM_CLEAR_BIT (PRM_ALLOCATED, (GET_PRM (prm_id)->dynamic_flag));
    }
  PRM_GET_INTEGER_LIST (GET_PRM (prm_id)->value) = value;
  if (PRM_GET_INTEGER_LIST (GET_PRM (prm_id)->value) != NULL)
    {
      PRM_SET_BIT (PRM_ALLOCATED, (GET_PRM (prm_id)->dynamic_flag));
    }

  sysprm_update_flag_different (GET_PRM (prm_id));
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
  assert (PRM_IS_BIGINT (GET_PRM (prm_id)));

  PRM_GET_BIGINT (GET_PRM (prm_id)->value) = value;

  sysprm_update_flag_different (GET_PRM (prm_id));
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
      // TODO: Is "integer_list[i] == -error_code" necessary? Why is it necessary?
      if (integer_list[i] == error_code || integer_list[i] == -error_code)
	{
	  return true;
	}
    }
  return false;
}

/*
 * sysprm_find_fi_code_in_integer_list () - function that searches a FI_TEST_CODE in an
 *                                   integer list
 *
 * return      : true if FI_TEST_CODE is found, false otherwise
 * prm_id (in) : id of the system parameter that contains an integer list
 * fi_code (in)  : FI_TEST_CODE to look for
 */
bool
sysprm_find_fi_code_in_integer_list (PARAM_ID prm_id, int fi_code)
{
  int i;
  int *integer_list = prm_get_integer_list_value (prm_id);

  if (integer_list == NULL)
    {
      return false;
    }

  for (i = 1; i <= integer_list[0]; i++)
    {
      if (integer_list[i] == fi_code)
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
  if (!PRM_IS_FOR_QRY_STRING (prm) && !PRM_IS_FOR_HA_CONTEXT (prm))
    {
      /* nothing to do */
      return;
    }

  /* compare current value with default value and update PRM_DIFFERENT bit */
  if (sysprm_compare_values (prm) != 0)
    {
      PRM_SET_BIT (PRM_DIFFERENT, prm->dynamic_flag);
    }
  else
    {
      PRM_CLEAR_BIT (PRM_DIFFERENT, prm->dynamic_flag);
    }
#endif /* CS_MODE */
}

#if defined(UNUSED_FUNCTIONS)
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
      PRM_SET_BIT (PRM_ALLOCATED, prm->dynamic_flag);
    }
  else
    {
      PRM_CLEAR_BIT (PRM_ALLOCATED, prm->dynamic_flag);
    }
}
#endif
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
  size_t size;

  assert (NUM_SESSION_PRM > 0);

  size = NUM_SESSION_PRM * sizeof (SESSION_PARAM);
  result = (SESSION_PARAM *) malloc (size);
  if (result == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
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
      sysprm_clear_sysprm_value (&sprm->value, (SYSPRM_DATATYPE) sprm->datatype);
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
sysprm_pack_sysprm_value (char *ptr, SYSPRM_VALUE value, SYSPRM_DATATYPE datatype)
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
sysprm_packed_sysprm_value_length (SYSPRM_VALUE value, SYSPRM_DATATYPE datatype, int offset)
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
sysprm_unpack_sysprm_value (char *ptr, SYSPRM_VALUE * value, SYSPRM_DATATYPE datatype)
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
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (str) + 1));
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
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			(size_t) ((temp + 1) * OR_INT_SIZE));
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
      ptr = sysprm_pack_sysprm_value (ptr, prm->value, (SYSPRM_DATATYPE) prm->datatype);
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
sysprm_packed_session_parameters_length (SESSION_PARAM * session_parameters, int offset)
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
	sysprm_packed_sysprm_value_length (session_parameters[i].value, (SYSPRM_DATATYPE) prm->datatype, size + offset);
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
sysprm_unpack_session_parameters (char *ptr, SESSION_PARAM ** session_parameters_ptr)
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SESSION_PARAM));
      goto error;
    }

  for (prm_index = 0; prm_index < NUM_SESSION_PRM; prm_index++)
    {
      int flag;

      prm = &session_params[prm_index];

      ptr = or_unpack_int (ptr, &tmp);
      prm->prm_id = (PARAM_ID) tmp;

      ptr = or_unpack_int (ptr, &flag);
      prm->flag = (unsigned int) flag;

      ptr = or_unpack_int (ptr, &prm->datatype);

      ptr = sysprm_unpack_sysprm_value (ptr, &prm->value, (SYSPRM_DATATYPE) prm->datatype);
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
sysprm_pack_assign_values (char *ptr, const SYSPRM_ASSIGN_VALUE * assign_values)
{
  char *old_ptr = ptr;
  int count;

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  /* skip one int -> size of assign_values */
  ptr += OR_INT_SIZE;

  for (count = 0; assign_values != NULL; assign_values = assign_values->next, count++)
    {
      ptr = or_pack_int (ptr, assign_values->prm_id);
      ptr = sysprm_pack_sysprm_value (ptr, assign_values->value, GET_PRM (assign_values->prm_id)->datatype);
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
sysprm_packed_assign_values_length (const SYSPRM_ASSIGN_VALUE * assign_values, int offset)
{
  int size = 0;

  size += OR_INT_SIZE;		/* size of assign_values list */

  for (; assign_values != NULL; assign_values = assign_values->next)
    {
      size += OR_INT_SIZE;	/* prm_id */
      size +=
	sysprm_packed_sysprm_value_length (assign_values->value, GET_PRM (assign_values->prm_id)->datatype,
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
sysprm_unpack_assign_values (char *ptr, SYSPRM_ASSIGN_VALUE ** assign_values_ptr)
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
      assign_val = (SYSPRM_ASSIGN_VALUE *) malloc (sizeof (SYSPRM_ASSIGN_VALUE));
      if (assign_val == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SYSPRM_ASSIGN_VALUE));
	  goto error;
	}

      ptr = or_unpack_int (ptr, &tmp);
      assign_val->prm_id = (PARAM_ID) tmp;

      ptr = sysprm_unpack_sysprm_value (ptr, &assign_val->value, GET_PRM (assign_val->prm_id)->datatype);
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
      sysprm_clear_sysprm_value (&assignment->value, GET_PRM (assignment->prm_id)->datatype);
      free_and_init (assignment);
      assignment = save_next;
    }

  *assign_values_ptr = NULL;
}

#if defined (SERVER_MODE)
/*
 * update_session_state_from_sys_params () - Updates the session state members
 *                                           from session system parameters
 *
 * return		   : void
 * thread_p (in)	   : thread entry
 * session_params (in)     : pointer to session parameters vector
 *
 */
static void
update_session_state_from_sys_params (THREAD_ENTRY * thread_p, SESSION_PARAM * session_params)
{
  TZ_REGION *session_tz_region;

  session_tz_region = session_get_session_tz_region (thread_p);
  if (session_tz_region != NULL)
    {
#ifndef NDEBUG
      int i;
      for (i = 0; i < NUM_SESSION_PRM; i++)
	{
	  if (session_params[i].prm_id == PRM_ID_TIMEZONE)
	    {
	      assert (prm_Def_session_idx[PRM_ID_TIMEZONE] == i);
	      break;
	    }
	}
      if (i >= NUM_SESSION_PRM)
	{
	  assert (prm_Def_session_idx[PRM_ID_TIMEZONE] == -1);
	}
#endif

      if (prm_Def_session_idx[PRM_ID_TIMEZONE] >= 0)
	{
	  SESSION_PARAM *ssession_prm = &(session_params[prm_Def_session_idx[PRM_ID_TIMEZONE]]);
	  assert (ssession_prm->prm_id == PRM_ID_TIMEZONE);
	  tz_str_to_region (ssession_prm->value.str, strlen (ssession_prm->value.str), session_tz_region);
	}
    }
}

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
sysprm_session_init_session_parameters (SESSION_PARAM ** session_parameters_ptr, int *found_session_parameters)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  int error_code = NO_ERROR;
  SESSION_PARAM *session_params = NULL;

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
      update_session_state_from_sys_params (thread_p, session_params);
    }
  else
    {
      /* use the values stored in session state and free the session parameters received from client */
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
 * value (in)		   : new value
 */
static SYSPRM_ERR
sysprm_set_session_parameter_value (SESSION_PARAM * session_parameter, SYSPRM_VALUE value)
{
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
      if (session_parameter->flag & PRM_ALLOCATED)
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
      if (session_parameter->flag & PRM_ALLOCATED)
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
sysprm_set_session_parameter_default (SESSION_PARAM * session_parameter, PARAM_ID prm_id)
{
  SYSPRM_PARAM *prm = GET_PRM (prm_id);

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
	if (session_parameter->flag & PRM_ALLOCATED)
	  {
	    free_and_init (session_parameter->value.str);
	    PRM_CLEAR_BIT (PRM_ALLOCATED, session_parameter->flag);
	  }
	session_parameter->value.str = PRM_GET_STRING (prm->default_value);
	break;
      }
    case PRM_INTEGER_LIST:
      {
	if (session_parameter->flag & PRM_ALLOCATED)
	  {
	    free_and_init (session_parameter->value.integer_list);
	    PRM_CLEAR_BIT (PRM_ALLOCATED, session_parameter->flag);
	  }
	session_parameter->value.integer_list = PRM_GET_INTEGER_LIST (prm->default_value);
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
sysprm_compare_values (SYSPRM_PARAM * prm)
{
  switch (prm->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      return (PRM_GET_INT (prm->value) != PRM_GET_INT (prm->default_value));

    case PRM_BOOLEAN:
      return (PRM_GET_BOOL (prm->value) != PRM_GET_BOOL (prm->default_value));

    case PRM_FLOAT:
      return (PRM_GET_FLOAT (prm->value) != PRM_GET_FLOAT (prm->default_value));

    case PRM_STRING:
      {
	char *first_str = PRM_GET_STRING (prm->value);
	char *second_str = PRM_GET_STRING (prm->default_value);

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
      return (PRM_GET_BIGINT (prm->value) != PRM_GET_BIGINT (prm->default_value));

    case PRM_INTEGER_LIST:
      {
	int i;
	int *first_int_list = PRM_GET_INTEGER_LIST (prm->value);
	int *second_int_list = PRM_GET_INTEGER_LIST (prm->default_value);

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
sysprm_set_sysprm_value_from_parameter (SYSPRM_VALUE * prm_value, SYSPRM_PARAM * prm)
{
  size_t size;

  assert (&prm->value.v != prm_value);

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
      {
	char *str = PRM_GET_STRING (prm->value);
	char *old_str;
	if (str != NULL)
	  {
	    old_str = prm_value->str;

	    prm_value->str = strdup (str);
	    if (prm_value->str == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			(size_t) (strlen (PRM_GET_STRING (prm->value)) + 1));
		prm_value->str = old_str;
	      }
	    else
	      {
		free (old_str);
	      }
	  }
	else
	  {
	    prm_value->str = NULL;
	  }
      }
      break;
    case PRM_INTEGER_LIST:
      {
	int *integer_list = PRM_GET_INTEGER_LIST (prm->value);
	int *old_list;
	if (integer_list != NULL)
	  {
	    old_list = prm_value->integer_list;

	    size = (integer_list[0] + 1) * sizeof (int);
	    prm_value->integer_list = (int *) malloc (size);
	    if (prm_value->integer_list == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		prm_value->integer_list = old_list;
	      }
	    else
	      {
		memcpy (prm_value->integer_list, integer_list, size);
		free (old_list);
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
      sysprm_set_value (GET_PRM (session_parameters[i].prm_id), session_parameters[i].value, true, true);
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
sysprm_print_assign_values (SYSPRM_ASSIGN_VALUE * prm_values, char *buffer, int length)
{
  int n = 0;
  char *ptr = buffer;

  if (length == 0)
    {
      /* don't print anything */
      return 0;
    }

  for (; prm_values != NULL; prm_values = prm_values->next)
    {
      n = sysprm_print_sysprm_value (prm_values->prm_id, prm_values->value, ptr, length, PRM_PRINT_NAME);
      ptr += n;
      length -= n;

      if (prm_values->next)
	{
	  *ptr++ = ';';
	  *ptr++ = ' ';
	  length -= 2;
	}
      assert (length > 0);
    }
  *ptr = '\0';

  return (int) (ptr - buffer);
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

  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      if (PRM_PRINT_QRY_STRING (i))
	{
	  n = prm_print (GET_PRM (i), ptr, len, PRM_PRINT_ID, PRM_PRINT_CURR_VAL);
	  ptr += n;
	  len -= n;

	  *ptr++ = ';';
	  len--;
	  assert (len > 0);
	}
    }
  *ptr = '\0';

  /* TODO: 
   *     If we pass the buffer and its size as arguments,
   *    we can reduce the cost of allocating new memory and copying it here.
   */

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size + 1);
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
  size_t size;

  memset (buf, 0, LINE_MAX);
  ptr = buf;

  for (i = 0; i < MAX_SYSTEM_PARAMS; i++)
    {
      if (!PRM_IS_FOR_HA_CONTEXT (GET_PRM (i)))
	{
	  continue;
	}

      if (i == PRM_ID_INTL_COLLATION || i == PRM_ID_INTL_DATE_LANG || i == PRM_ID_INTL_NUMBER_LANG)
	{
	  char *val = prm_get_string_value ((PARAM_ID) i);

	  if (val == NULL)
	    {
	      continue;
	    }

	  if (i == PRM_ID_INTL_COLLATION)
	    {
	      if (strcasecmp (val, lang_get_collation_name (LANG_GET_BINARY_COLLATION (LANG_SYS_CODESET))) == 0)
		{
		  continue;
		}
	    }
	  else
	    {
	      if (strcasecmp (val, lang_get_Lang_name ()) == 0)
		{
		  continue;
		}
	    }
	}
      else if (!PRM_IS_DIFFERENT (GET_PRM (i)))
	{
	  continue;
	}

      n = prm_print (GET_PRM (i), ptr, len, PRM_PRINT_NAME, PRM_PRINT_CURR_VAL);
      ptr += n;
      len -= n;

      *ptr++ = ';';
      len--;
      assert (len > 0);
    }
  *ptr = '\0';

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size + 1);
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
int
sysprm_init_intl_param (void)
{
  SYSPRM_PARAM *prm_date_lang = GET_PRM (PRM_ID_INTL_DATE_LANG);
  SYSPRM_PARAM *prm_number_lang = GET_PRM (PRM_ID_INTL_NUMBER_LANG);
  SYSPRM_PARAM *prm_intl_collation = GET_PRM (PRM_ID_INTL_COLLATION);
  SYSPRM_PARAM *prm_timezone = GET_PRM (PRM_ID_TIMEZONE);
  int error = NO_ERROR;

  /* intl system parameters are session based and depend on system language; The language is read from DB (and client
   * from server), after the system parameters module was initialized and default values read from configuration file.
   * This function avoids to override any value already read from config. If no values are set from config, then this
   * function sets the values according to the default language. On client (CAS), we set the client copy of the value,
   * and also the cached session parameter value (which is the default starting value, when the existing CAS serves
   * another session) is intialized. */

  if (!PRM_IS_SET (prm_date_lang))
    {
      prm_set (prm_date_lang, lang_get_Lang_name (), true);
#if defined (CS_MODE)
      sysprm_update_cached_session_param_val (PRM_ID_INTL_DATE_LANG);
#endif
    }

  if (!PRM_IS_SET (prm_number_lang))
    {
      prm_set (prm_number_lang, lang_get_Lang_name (), true);
#if defined (CS_MODE)
      sysprm_update_cached_session_param_val (PRM_ID_INTL_NUMBER_LANG);
#endif
    }

  if (!PRM_IS_SET (prm_intl_collation))
    {
      prm_set (prm_intl_collation, lang_get_collation_name (LANG_GET_BINARY_COLLATION (LANG_SYS_CODESET)), true);
#if defined (CS_MODE)
      sysprm_update_cached_session_param_val (PRM_ID_INTL_COLLATION);
#endif
    }

  if (!PRM_IS_SET (prm_timezone))
    {
      prm_set (prm_timezone, prm_get_string_value (PRM_ID_SERVER_TIMEZONE), true);
#if defined (CS_MODE)
      sysprm_update_cached_session_param_val (PRM_ID_TIMEZONE);
#endif
    }

  return error;
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "db_set_system_parameters");
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
  SYSPRM_PARAM *sys_prm = GET_PRM (prm_id);

  assert (NUM_SESSION_PRM > 0);

#ifndef NDEBUG
  int i;
  for (i = 0; i < NUM_SESSION_PRM; i++)
    {
      if (cached_session_parameters[i].prm_id == prm_id)
	{
	  assert (prm_Def_session_idx[prm_id] == i);
	  break;
	}
    }
  if (i >= NUM_SESSION_PRM)
    {
      assert (prm_Def_session_idx[prm_id] == -1);
    }
#endif

  if (prm_Def_session_idx[prm_id] >= 0)
    {
      cached_session_prm = &cached_session_parameters[prm_Def_session_idx[prm_id]];
      assert (cached_session_prm->prm_id == prm_id);

      cached_session_prm->flag = sys_prm->dynamic_flag;
      sysprm_set_sysprm_value_from_parameter (&cached_session_prm->value, sys_prm);
      sysprm_update_session_prm_flag_allocated (cached_session_prm);
    }
}
#endif /* CS_MODE */

#if defined (WINDOWS)
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
  assert (PRM_IS_INTEGER (GET_PRM (prm_id)) || PRM_IS_KEYWORD (GET_PRM (prm_id)));

  return PRM_GET_INT_P (prm_get_value (prm_id));
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
  assert (PRM_IS_BOOLEAN (GET_PRM (prm_id)));

  return PRM_GET_BOOL_P (prm_get_value (prm_id));
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
  assert (PRM_IS_FLOAT (GET_PRM (prm_id)));

  return PRM_GET_FLOAT_P (prm_get_value (prm_id));
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
  assert (PRM_IS_STRING (GET_PRM (prm_id)));

  return PRM_GET_STRING_P (prm_get_value (prm_id));
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
  assert (PRM_IS_INTEGER_LIST (GET_PRM (prm_id)));

  return PRM_GET_INTEGER_LIST_P (prm_get_value (prm_id));
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
  assert (PRM_IS_BIGINT (GET_PRM (prm_id)));

  return PRM_GET_BIGINT_P (prm_get_value (prm_id));
}
#endif /* window */


#if !defined(NDEBUG) && !defined(MULTI_CONN_TO_A_SERVER)
pthread_t gv_main_tid;

__attribute__ ((constructor))
     static void get_main_thread_id ()
{
  gv_main_tid = pthread_self ();
}
#endif
