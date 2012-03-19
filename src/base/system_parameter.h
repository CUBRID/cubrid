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
 * system_parameter.h - system parameters
 */

#ifndef _SYSTEM_PARAMETER_H_
#define _SYSTEM_PARAMETER_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "error_manager.h"
#include "error_code.h"
#include "porting.h"

typedef enum
{
  PRM_ERR_NO_ERROR = NO_ERROR,
  PRM_ERR_NOT_DIRECTORY = 2,
  PRM_ERR_INIT_FILE_NOT_CREATED = 3,
  PRM_ERR_CANT_WRITE = 4,
  PRM_ERR_CANT_ACCESS = 6,
  PRM_ERR_NO_HOME = 7,
  PRM_ERR_NO_VALUE = 8,
  PRM_ERR_CANT_OPEN_INIT = 9,
  PRM_ERR_BAD_LINE = 10,
  PRM_ERR_BAD_ENV_VAR = 11,
  PRM_ERR_UNKNOWN_PARAM = 12,
  PRM_ERR_BAD_VALUE = 13,
  PRM_ERR_NO_MEM_FOR_PRM = 14,
  PRM_ERR_BAD_STRING = 15,
  PRM_ERR_BAD_RANGE = 16,
  PRM_ERR_UNIX_ERROR = 17,
  PRM_ERR_NO_MSG = 18,
  PRM_ERR_RESET_BAD_RANGE = 19,
  PRM_ERR_KEYWROD_INFO_INT = 20,
  PRM_ERR_KEYWORK_INFO_FLOAT = 21,
  PRM_ERR_DEPRICATED = 22,
  PRM_ERR_NOT_BOTH = 23,
  PRM_ERR_CANNOT_CHANGE = 24,
  PRM_ERR_NOT_FOR_CLIENT = 25,
  PRM_ERR_NOT_FOR_SERVER = 26,
  PRM_ERR_NOT_SOLE_TRAN = 27,
  PRM_ERR_COMM_ERR = 28,
  PRM_ERR_FILE_ERR = 29
} SYSPRM_ERR;

typedef enum compat_mode COMPAT_MODE;

enum compat_mode
{
  COMPAT_CUBRID,
  COMPAT_MYSQL,
  COMPAT_ORACLE
    /*
       COMPAT_ANSI,
       COMPAT_DB2,
       COMPAT_MAXDB,
       COMPAT_MSSQL,
       COMPAT_POSTGRESQL
     */
};

/*
 * Global variables of parameters' value
 */

#define PRM_NAME_ER_LOG_DEBUG "er_log_debug"
extern bool PRM_ER_LOG_DEBUG;

#define PRM_NAME_ER_LOG_LEVEL "error_log_level"
extern int PRM_ER_LOG_LEVEL;

#define PRM_NAME_ER_LOG_WARNING "error_log_warning"
extern bool PRM_ER_LOG_WARNING;

#define PRM_NAME_ER_EXIT_ASK "inquire_on_exit"
extern int PRM_ER_EXIT_ASK;

#define PRM_NAME_ER_LOG_SIZE "error_log_size"
extern int PRM_ER_LOG_SIZE;

#define PRM_NAME_ER_LOG_FILE "error_log"
extern const char *PRM_ER_LOG_FILE;

#define PRM_NAME_IO_LOCKF_ENABLE "file_lock"
extern bool PRM_IO_LOCKF_ENABLE;

#define PRM_NAME_SR_NBUFFERS "sort_buffer_pages"
extern int PRM_SR_NBUFFERS;

#define PRM_NAME_SORT_BUFFER_SIZE "sort_buffer_size"
extern UINT64 PRM_SORT_BUFFER_SIZE;

#define PRM_NAME_PB_NBUFFERS "data_buffer_pages"
extern int PRM_PB_NBUFFERS;

#define PRM_NAME_PB_BUFFER_FLUSH_RATIO "data_buffer_flush_ratio"
extern float PRM_PB_BUFFER_FLUSH_RATIO;

#define PRM_NAME_PAGE_BUFFER_SIZE "data_buffer_size"
extern UINT64 PRM_PAGE_BUFFER_SIZE;

#define PRM_NAME_HF_UNFILL_FACTOR "unfill_factor"
extern float PRM_HF_UNFILL_FACTOR;

#define PRM_NAME_BT_UNFILL_FACTOR "index_unfill_factor"
extern float PRM_BT_UNFILL_FACTOR;

#define PRM_NAME_BT_OID_NBUFFERS "index_scan_oid_buffer_pages"
extern float PRM_BT_OID_NBUFFERS;

#define PRM_NAME_BT_OID_BUFFER_SIZE "index_scan_oid_buffer_size"
extern UINT64 PRM_BT_OID_BUFFER_SIZE;

#define PRM_NAME_BT_INDEX_SCAN_OID_ORDER "index_scan_in_oid_order"
extern bool PRM_BT_INDEX_SCAN_OID_ORDER;

#define PRM_NAME_BOSR_MAXTMP_PAGES "temp_file_max_size_in_pages"
extern int PRM_BOSR_MAXTMP_PAGES;

#define PRM_NAME_LK_TIMEOUT_MESSAGE_DUMP_LEVEL "lock_timeout_message_type"
extern int PRM_LK_TIMEOUT_MESSAGE_DUMP_LEVEL;

#define PRM_NAME_LK_ESCALATION_AT "lock_escalation"
extern int PRM_LK_ESCALATION_AT;

#define PRM_NAME_LK_TIMEOUT_SECS "lock_timeout_in_secs"
extern int PRM_LK_TIMEOUT_SECS;

#define PRM_NAME_LK_RUN_DEADLOCK_INTERVAL "deadlock_detection_interval_in_secs"
extern float PRM_LK_RUN_DEADLOCK_INTERVAL;

#define PRM_NAME_LOG_NBUFFERS "log_buffer_pages"
extern int PRM_LOG_NBUFFERS;

#define PRM_NAME_LOG_BUFFER_SIZE "log_buffer_size"
extern UINT64 PRM_LOG_BUFFER_SIZE;

#define PRM_NAME_LOG_CHECKPOINT_NPAGES "checkpoint_every_npages"
extern int PRM_LOG_CHECKPOINT_NPAGES;

#define PRM_NAME_LOG_CHECKPOINT_INTERVAL_MINUTES "checkpoint_interval_in_mins"
extern int PRM_LOG_CHECKPOINT_INTERVAL_MINUTES;

#define PRM_NAME_LOG_CHECKPOINT_SLEEP_MSECS "checkpoint_sleep_msecs"
extern int PRM_LOG_CHECKPOINT_SLEEP_MSECS;

#define PRM_NAME_LOG_BACKGROUND_ARCHIVING "background_archiving"
extern bool PRM_LOG_BACKGROUND_ARCHIVING;

#define PRM_NAME_LOG_ISOLATION_LEVEL "isolation_level"
extern int PRM_LOG_ISOLATION_LEVEL;

#define PRM_NAME_LOG_MEDIA_FAILURE_SUPPORT "media_failure_support"
extern bool PRM_LOG_MEDIA_FAILURE_SUPPORT;

#define PRM_NAME_LOG_SWEEP_CLEAN "log_file_sweep_clean"
extern bool PRM_LOG_SWEEP_CLEAN;

#define PRM_NAME_COMMIT_ON_SHUTDOWN "commit_on_shutdown"
extern bool PRM_COMMIT_ON_SHUTDOWN;

#define PRM_NAME_SHUTDOWN_WAIT_TIME_IN_SECS "shutdown_wait_time_in_secs"
extern int PRM_SHUTDOWN_WAIT_TIME_IN_SECS;

#define PRM_NAME_CSQL_AUTO_COMMIT "csql_auto_commit"
extern bool PRM_CSQL_AUTO_COMMIT;

#define PRM_NAME_WS_HASHTABLE_SIZE "initial_workspace_table_size"
extern int PRM_WS_HASHTABLE_SIZE;

#define PRM_NAME_WS_MEMORY_REPORT "workspace_memory_report"
extern bool PRM_WS_MEMORY_REPORT;

#define PRM_NAME_GC_ENABLE "garbage_collection"
extern bool PRM_GC_ENABLE;

#define PRM_NAME_TCP_PORT_ID "cubrid_port_id"
extern int PRM_TCP_PORT_ID;

#define PRM_NAME_TCP_CONNECTION_TIMEOUT "connection_timeout"
extern int PRM_TCP_CONNECTION_TIMEOUT;

#define PRM_NAME_OPTIMIZATION_LEVEL "optimization_level"
extern int PRM_OPTIMIZATION_LEVEL;

#define PRM_NAME_QO_DUMP "qo_dump"
extern bool PRM_QO_DUMP;

#define PRM_NAME_CSS_MAX_CLIENTS "max_clients"
extern int PRM_CSS_MAX_CLIENTS;

#define PRM_NAME_THREAD_STACKSIZE "thread_stacksize"
extern int PRM_THREAD_STACKSIZE;

#define PRM_NAME_CFG_DB_HOSTS "db_hosts"
extern const char *PRM_CFG_DB_HOSTS;

#define PRM_NAME_RESET_TR_PARSER "reset_tr_parser_interval"
extern int PRM_RESET_TR_PARSER;

#define PRM_NAME_IO_BACKUP_NBUFFERS "backup_buffer_pages"
extern int PRM_IO_BACKUP_NBUFFERS;

#define PRM_NAME_IO_BACKUP_MAX_VOLUME_SIZE "backup_volume_max_size_bytes"
extern int PRM_IO_BACKUP_MAX_VOLUME_SIZE;

#define PRM_NAME_MAX_PAGES_IN_TEMP_FILE_CACHE "max_pages_in_temp_file_cache"
extern int PRM_MAX_PAGES_IN_TEMP_FILE_CACHE;

#define PRM_NAME_MAX_ENTRIES_IN_TEMP_FILE_CACHE "max_entries_in_temp_file_cache"
extern int PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE;

#define PRM_NAME_PTHREAD_SCOPE_PROCESS "pthread_scope_process"
extern bool PRM_PTHREAD_SCOPE_PROCESS;

#define PRM_NAME_TEMP_MEM_BUFFER_PAGES "temp_file_memory_size_in_pages"
extern int PRM_TEMP_MEM_BUFFER_PAGES;

#define PRM_NAME_INDEX_SCAN_KEY_BUFFER_PAGES "index_scan_key_buffer_pages"
extern int PRM_INDEX_SCAN_KEY_BUFFER_PAGES;

#define PRM_NAME_INDEX_SCAN_KEY_BUFFER_SIZE "index_scan_key_buffer_size"
extern UINT64 PRM_INDEX_SCAN_KEY_BUFFER_SIZE;

#define PRM_NAME_DONT_REUSE_HEAP_FILE "dont_reuse_heap_file"
extern bool PRM_DONT_REUSE_HEAP_FILE;

#define PRM_NAME_QUERY_MODE_SYNC "dont_use_async_query"
extern bool PRM_QUERY_MODE_SYNC;

#define PRM_NAME_INSERT_MODE "insert_execution_mode"
extern int PRM_INSERT_MODE;

#define PRM_NAME_LK_MAX_SCANID_BIT "max_index_scan_count"
extern int PRM_LK_MAX_SCANID_BIT;

#define PRM_NAME_HOSTVAR_LATE_BINDING "hostvar_late_binding"
extern bool PRM_HOSTVAR_LATE_BINDING;

#define PRM_NAME_ENABLE_HISTO "communication_histogram"
extern bool PRM_ENABLE_HISTO;

#define PRM_NAME_MUTEX_BUSY_WAITING_CNT "mutex_busy_waiting_cnt"
extern int PRM_MUTEX_BUSY_WAITING_CNT;

#define PRM_NAME_PB_NUM_LRU_CHAINS "num_LRU_chains"
extern int PRM_PB_NUM_LRU_CHAINS;

#define PRM_NAME_PAGE_BG_FLUSH_INTERVAL_MSECS "page_flush_interval_in_msecs"
extern int PRM_PAGE_BG_FLUSH_INTERVAL_MSEC;

#define PRM_NAME_ADAPTIVE_FLUSH_CONTROL "adaptive_flush_control"
extern bool PRM_ADAPTIVE_FLUSH_CONTROL;

#define PRM_NAME_MAX_FLUSH_PAGES_PER_SECOND "max_flush_pages_per_second"
extern int PRM_MAX_FLUSH_PAGES_PER_SECOND;

#define PRM_NAME_PB_SYNC_ON_NFLUSH "sync_on_nflush"
extern int PRM_PB_SYNC_ON_NFLUSH;

#define PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL "page_validation_level"
extern int PRM_PB_DEBUG_PAGE_VALIDATION_LEVEL;

#define PRM_NAME_ORACLE_STYLE_OUTERJOIN "oracle_style_outerjoin"
extern bool PRM_ORACLE_STYLE_OUTERJOIN;

#define PRM_NAME_COMPAT_MODE "compat_mode"
extern int PRM_COMPAT_MODE;

#define PRM_NAME_ANSI_QUOTES "ansi_quotes"
extern bool PRM_ANSI_QUOTES;

#define PRM_NAME_DEFAULT_WEEK_FORMAT "default_week_format"
extern int PRM_DEFAULT_WEEK_FORMAT;

#define PRM_NAME_TEST_MODE "test_mode"
extern bool PRM_TEST_MODE;

#define PRM_NAME_ONLY_FULL_GROUP_BY "only_full_group_by"
extern bool PRM_ONLY_FULL_GROUP_BY;

#define PRM_NAME_PIPES_AS_CONCAT "pipes_as_concat"
extern bool PRM_PIPES_AS_CONCAT;

#define PRM_NAME_MYSQL_TRIGGER_CORRELATION_NAMES "mysql_trigger_correlation_names"
extern bool PRM_MYSQL_TRIGGER_CORRELATION_NAMES;

#define PRM_NAME_REQUIRE_LIKE_ESCAPE_CHARACTER "require_like_escape_character"
extern bool PRM_REQUIRE_LIKE_ESCAPE_CHARACTER;

#define PRM_NAME_NO_BACKSLASH_ESCAPES "no_backslash_escapes"
extern bool PRM_NO_BACKSLASH_ESCAPES;

#define PRM_NAME_GROUP_CONCAT_MAX_LEN "group_concat_max_len"
extern int PRM_GROUP_CONCAT_MAX_LEN;

#define PRM_NAME_STRING_MAX_SIZE_BYTES "string_max_size_bytes"
extern int PRM_STRING_MAX_SIZE_BYTES;

#define PRM_NAME_ADD_COLUMN_UPDATE_HARD_DEFAULT "add_column_update_hard_default"
extern bool PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT;

#define PRM_NAME_RETURN_NULL_ON_FUNCTION_ERRORS "return_null_on_function_errors"
extern bool PRM_RETURN_NULL_ON_FUNCTION_ERRORS;

#define PRM_NAME_PLUS_AS_CONCAT "plus_as_concat"
extern bool PRM_PLUS_AS_CONCAT;

#define PRM_NAME_ALTER_TABLE_CHANGE_TYPE_STRICT "alter_table_change_type_strict"
extern bool PRM_ALTER_TABLE_CHANGE_TYPE_STRICT;

#define PRM_NAME_COMPACTDB_PAGE_RECLAIM_ONLY "compactdb_page_reclaim_only"
extern int PRM_COMPACTDB_PAGE_RECLAIM_ONLY;

#define PRM_NAME_LIKE_TERM_SELECTIVITY "like_term_selectivity"
extern float PRM_LIKE_TERM_SELECTIVITY;

#define PRM_NAME_MAX_OUTER_CARD_OF_IDXJOIN "max_outer_card_of_idxjoin"
extern int PRM_MAX_OUTER_CARD_OF_IDXJOIN;

#define PRM_NAME_ORACLE_STYLE_EMPTY_STRING "oracle_style_empty_string"
extern bool PRM_ORACLE_STYLE_EMPTY_STRING;

#define PRM_NAME_SUPPRESS_FSYNC "suppress_fsync"
extern int PRM_SUPPRESS_FSYNC;

#define PRM_NAME_CALL_STACK_DUMP_ON_ERROR "call_stack_dump_on_error"
extern bool PRM_CALL_STACK_DUMP_ON_ERROR;

#define PRM_NAME_CALL_STACK_DUMP_ACTIVATION "call_stack_dump_activation_list"
extern bool *PRM_CALL_STACK_DUMP_ACTIVATION;

#define PRM_NAME_CALL_STACK_DUMP_DEACTIVATION "call_stack_dump_deactivation_list"
extern bool *PRM_CALL_STACK_DUMP_DEACTIVATION;

#define PRM_NAME_COMPAT_NUMERIC_DIVISION_SCALE "compat_numeric_division_scale"
extern bool PRM_COMPAT_NUMERIC_DIVISION_SCALE;

#define PRM_NAME_DBFILES_PROTECT "dbfiles_protect"
extern bool PRM_DBFILES_PROTECT;

#define PRM_NAME_AUTO_RESTART_SERVER "auto_restart_server"
extern bool PRM_AUTO_RESTART_SERVER;

#define PRM_NAME_XASL_MAX_PLAN_CACHE_ENTRIES "max_plan_cache_entries"
extern int PRM_XASL_MAX_PLAN_CACHE_ENTRIES;

#if defined (ENABLE_UNUSED_FUNCTION)
#define PRM_NAME_XASL_MAX_PLAN_CACHE_CLONES "max_plan_cache_clones"
extern int PRM_XASL_MAX_PLAN_CACHE_CLONES;
#endif /* ENABLE_UNUSED_FUNCTION */

#define PRM_NAME_FILTER_PRED_MAX_CACHE_ENTRIES "max_filter_pred_cache_entries"
extern int PRM_FILTER_PRED_MAX_CACHE_ENTRIES;

#define PRM_NAME_FILTER_PRED_MAX_CACHE_CLONES "max_filter_pred_cache_clones"
extern int PRM_FILTER_PRED_MAX_CACHE_CLONES;

#define PRM_NAME_XASL_PLAN_CACHE_TIMEOUT "plan_cache_timeout"
extern int PRM_XASL_PLAN_CACHE_TIMEOUT;

#define PRM_NAME_LIST_QUERY_CACHE_MODE "query_cache_mode"
extern int PRM_LIST_QUERY_CACHE_MODE;

#define PRM_NAME_LIST_MAX_QUERY_CACHE_ENTRIES "max_query_cache_entries"
extern int PRM_LIST_MAX_QUERY_CACHE_ENTRIES;

#define PRM_NAME_LIST_MAX_QUERY_CACHE_PAGES "query_cache_size_in_pages"
extern int PRM_LIST_MAX_QUERY_CACHE_PAGES;

#define PRM_NAME_USE_ORDERBY_SORT_LIMIT  "use_orderby_sort_limit"
extern bool PRM_USE_ORDERBY_SORT_LIMIT;

#define PRM_NAME_REPLICATION_MODE "replication"
extern bool PRM_REPLICATION_MODE;

#define PRM_NAME_HA_MODE "ha_mode"
extern int PRM_HA_MODE;
extern int PRM_HA_MODE_FOR_SA_UTILS_ONLY;	/* this is for SA mode cubrid
						 * utilities only, such as
						 * 'cubrid heartbeat' */

#define PRM_NAME_HA_SERVER_STATE "ha_server_state"
extern int PRM_HA_SERVER_STATE;

#define PRM_NAME_HA_LOG_APPLIER_STATE "ha_log_applier_state"
extern int PRM_HA_LOG_APPLIER_STATE;

#define PRM_NAME_HA_NODE_LIST "ha_node_list"
extern const char *PRM_HA_NODE_LIST;

#define PRM_NAME_HA_REPLICA_LIST "ha_replica_list"
extern const char *PRM_HA_REPLICA_LIST;

#define PRM_NAME_HA_DB_LIST "ha_db_list"
extern const char *PRM_HA_DB_LIST;

#define PRM_NAME_HA_COPY_LOG_BASE "ha_copy_log_base"
extern const char *PRM_HA_COPY_LOG_BASE;

#define PRM_NAME_HA_COPY_SYNC_MODE "ha_copy_sync_mode"
extern const char *PRM_HA_COPY_SYNC_MODE;

#define PRM_NAME_HA_APPLY_MAX_MEM_SIZE "ha_apply_max_mem_size"
extern int PRM_HA_APPLY_MAX_MEM_SIZE;

#define PRM_NAME_HA_PORT_ID "ha_port_id"
extern int PRM_HA_PORT_ID;

#define PRM_NAME_HA_INIT_TIMER_IN_MSECS "ha_init_timer_in_msec"
extern int PRM_HA_INIT_TIMER_IN_MSECS;

#define PRM_NAME_HA_HEARTBEAT_INTERVAL_IN_MSECS "ha_heartbeat_interval_in_msecs"
extern int PRM_HA_HEARTBEAT_INTERVAL_IN_MSECS;

#define PRM_NAME_HA_CALC_SCORE_INTERVAL_IN_MSECS "ha_calc_score_interval_in_msecs"
extern int PRM_HA_CALC_SCORE_INTERVAL_IN_MSECS;

#define PRM_NAME_HA_FAILOVER_WAIT_TIME_IN_MSECS "ha_failover_wait_time_in_msecs"
extern int PRM_HA_FAILOVER_WAIT_TIME_IN_MSECS;

#define PRM_NAME_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS "ha_process_start_confirm_interval_in_msecs"
extern int PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS;

#define PRM_NAME_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS "ha_process_dereg_confirm_interval_in_msecs"
extern int PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS;

#define PRM_NAME_HA_MAX_PROCESS_START_CONFIRM "ha_max_process_start_confirm"
extern int PRM_HA_MAX_PROCESS_START_CONFIRM;

#define PRM_NAME_HA_MAX_PROCESS_DEREG_CONFIRM "ha_max_process_dereg_confirm"
extern int PRM_HA_MAX_PROCESS_DEREG_CONFIRM;

#define PRM_NAME_HA_CHANGEMODE_INTERVAL_IN_MSEC "ha_changemode_interval_in_msecs"
extern int PRM_HA_CHANGEMODE_INTERVAL_IN_MSECS;

#define PRM_NAME_HA_MAX_HEARTBEAT_GAP "ha_max_heartbeat_gap"
extern int PRM_HA_MAX_HEARTBEAT_GAP;

#define PRM_NAME_HA_PING_HOSTS "ha_ping_hosts"
extern const char *PRM_HA_PING_HOSTS;

#define PRM_NAME_HA_APPLYLOGDB_RETRY_ERROR_LIST "ha_applylogdb_retry_error_list"
extern bool *PRM_HA_APPLYLOGDB_RETRY_ERROR_LIST;

#define PRM_NAME_HA_APPLYLOGDB_IGNORE_ERROR_LIST "ha_applylogdb_ignore_error_list"
extern bool *PRM_HA_APPLYLOGDB_IGNORE_ERROR_LIST;

#define PRM_NAME_JAVA_STORED_PROCEDURE "java_stored_procedure"
extern bool PRM_JAVA_STORED_PROCEDURE;

#define PRM_NAME_COMPAT_PRIMARY_KEY "compat_primary_key"
extern bool PRM_COMPAT_PRIMARY_KEY;

#define PRM_NAME_INTL_MBS_SUPPORT "intl_mbs_support"
extern bool PRM_INTL_MBS_SUPPORT;

#define PRM_NAME_LOG_HEADER_FLUSH_INTERVAL "log_header_flush_interval_in_secs"
extern int PRM_LOG_HEADER_FLUSH_INTERVAL;

#define PRM_NAME_LOG_ASYNC_COMMIT "async_commit"
extern bool PRM_LOG_ASYNC_COMMIT;

#define PRM_NAME_LOG_GROUP_COMMIT_INTERVAL_MSECS "group_commit_interval_in_msecs"
extern int PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS;

#define PRM_NAME_LOG_BG_FLUSH_INTERVAL_MSECS "log_flush_interval_in_msecs"
extern int PRM_LOG_BG_FLUSH_INTERVAL_MSECS;

#define PRM_NAME_LOG_BG_FLUSH_NUM_PAGES "log_flush_every_npages"
extern int PRM_LOG_BG_FLUSH_NUM_PAGES;

#define PRM_NAME_LOG_COMPRESS "log_compress"
extern bool PRM_LOG_COMPRESS;

#define PRM_NAME_BLOCK_NOWHERE_STATEMENT "block_nowhere_statement"
extern bool PRM_BLOCK_NOWHERE_STATEMENT;

#define PRM_NAME_BLOCK_DDL_STATEMENT "block_ddl_statement"
extern bool PRM_BLOCK_DDL_STATEMENT;

#define PRM_NAME_SINGLE_BYTE_COMPARE "single_byte_compare"
extern bool PRM_SINGLE_BYTE_COMPARE;

#define PRM_NAME_CSQL_HISTORY_NUM "csql_history_num"
extern int PRM_CSQL_HISTORY_NUM;

#define PRM_NAME_LOG_TRACE_DEBUG "log_trace_debug"
extern bool PRM_LOG_TRACE_DEBUG;

#define PRM_NAME_DL_FORK "dl_fork"
extern const char *PRM_DL_FORK;

#define PRM_NAME_ER_PRODUCTION_MODE "error_log_production_mode"
extern bool PRM_ER_PRODUCTION_MODE;

#define PRM_NAME_ER_STOP_ON_ERROR "stop_on_error"
extern int PRM_ER_STOP_ON_ERROR;

#define PRM_NAME_TCP_RCVBUF_SIZE "tcp_rcvbuf_size"
extern int PRM_TCP_RCVBUF_SIZE;

#define PRM_NAME_TCP_SNDBUF_SIZE "tcp_sndbuf_size"
extern int PRM_TCP_SNDBUF_SIZE;

#define PRM_NAME_TCP_NODELAY "tcp_nodealy"
extern int PRM_TCP_NODELAY;

#define PRM_NAME_CSQL_SINGLE_LINE_MODE "csql_single_line_mode"
extern bool PRM_CSQL_SINGLE_LINE_MODE;

#define PRM_NAME_XASL_DEBUG_DUMP "xasl_debug_dump"
extern bool PRM_XASL_DEBUG_DUMP;

#define PRM_NAME_LOG_MAX_ARCHIVES "log_max_archives"
extern int PRM_LOG_MAX_ARCHIVES;

#define PRM_NAME_FORCE_REMOVE_LOG_ARCHIVES "force_remove_log_archives"
extern bool PRM_FORCE_REMOVE_LOG_ARCHIVES;

#define PRM_NAME_REMOVE_LOG_ARCHIVES_INTERVAL "remove_log_archive_interval_in_secs"
extern int PRM_REMOVE_LOG_ARCHIVES_INTERVAL;

#define PRM_NAME_LOG_NO_LOGGING "no_logging"
extern bool PRM_LOG_NO_LOGGING;

#define PRM_NAME_UNLOADDB_IGNORE_ERROR "unload_ignore_error"
extern bool PRM_UNLOADDB_IGNORE_ERROR;

#define PRM_NAME_UNLOADDB_LOCK_TIMEOUT "unload_lock_timeout"
extern int PRM_UNLOADDB_LOCK_TIMEOUT;

#define PRM_NAME_LOADDB_FLUSH_INTERVAL "load_flush_interval"
extern int PRM_LOADDB_FLUSH_INTERVAL;

#define PRM_NAME_IO_TEMP_VOLUME_PATH "temp_volume_path"
extern const char *PRM_IO_TEMP_VOLUME_PATH;

#define PRM_NAME_IO_VOLUME_EXT_PATH "volume_extension_path"
extern const char *PRM_IO_VOLUME_EXT_PATH;

#define PRM_NAME_UNIQUE_ERROR_KEY_VALUE "print_key_value_on_unique_error"
extern bool PRM_UNIQUE_ERROR_KEY_VALUE;

#define PRM_NAME_USE_SYSTEM_MALLOC "use_system_malloc"
extern bool PRM_USE_SYSTEM_MALLOC;

#define PRM_NAME_EVENT_HANDLER "event_handler"
extern const char *PRM_EVENT_HANDLER;

#define PRM_NAME_EVENT_ACTIVATION "event_activation_list"
extern bool *PRM_EVENT_ACTIVATION;

#define PRM_NAME_READ_ONLY_MODE "read_only"
extern bool PRM_READ_ONLY_MODE;

#define PRM_NAME_MNT_WAITING_THREAD "monitor_waiting_thread"
extern int PRM_MNT_WAITING_THREAD;

#define PRM_NAME_MNT_STATS_THRESHOLD "monitor_stats_threshold"
extern int *PRM_MNT_STATS_THRESHOLD;

#define PRM_NAME_SERVICE_SERVICE_LIST "service::service"
extern const char *PRM_SERVICE_SERVICE_LIST;

#define PRM_NAME_SERVICE_SERVER_LIST "service::server"
extern const char *PRM_SERVICE_SERVER_LIST;

#define PRM_NAME_SESSION_STATE_TIMEOUT "session_state_timeout"
extern int PRM_SESSION_STATE_TIMEOUT;

#define PRM_NAME_MULTI_RANGE_OPT_LIMIT "multi_range_optimization_limit"
extern int PRM_MULTI_RANGE_OPT_LIMIT;

#define PRM_NAME_ACCESS_IP_CONTROL "access_ip_control"
extern bool PRM_ACCESS_IP_CONTROL;

#define PRM_NAME_ACCESS_IP_CONTROL_FILE "access_ip_control_file"
extern const char *PRM_ACCESS_IP_CONTROL_FILE;

#define PRM_NAME_DB_VOLUME_SIZE "db_volume_size"
extern UINT64 PRM_DB_VOLUME_SIZE;

#define PRM_NAME_LOG_VOLUME_SIZE "log_volume_size"
extern UINT64 PRM_LOG_VOLUME_SIZE;

#define PRM_NAME_USE_LOCALE_NUMBER_FORMAT "use_locale_number_format"
extern bool PRM_USE_LOCALE_NUMBER_FORMAT;

#define PRM_NAME_USE_LOCALE_DATE_FORMAT "use_locale_date_format"
extern bool PRM_USE_LOCALE_DATE_FORMAT;

extern int sysprm_load_and_init (const char *db_name, const char *conf_file);
extern int sysprm_reload_and_init (const char *db_name,
				   const char *conf_file);
extern void sysprm_final (void);
extern void sysprm_dump_parameters (FILE * fp);
extern void sysprm_set_er_log_file (const char *base_db_name);
extern void sysprm_dump_server_parameters (FILE * fp);
extern int sysprm_change_parameters (const char *data);
extern int sysprm_obtain_parameters (char *data, int len);
extern int sysprm_change_server_parameters (const char *data);
extern int sysprm_obtain_server_parameters (char *data, int len);
extern void sysprm_tune_client_parameters (void);
extern bool sysprm_prm_change_should_clear_cache (const char *data);
#if !defined (CS_MODE)
extern int xsysprm_change_server_parameters (const char *data);
extern int xsysprm_obtain_server_parameters (char *data, int len);
extern void xsysprm_dump_server_parameters (FILE * fp);
#endif /* !CS_MODE */
extern int sysprm_set_force (const char *pname, const char *pvalue);
extern int sysprm_set_to_default (const char *pname, bool set_to_force);
extern int sysprm_check_range (const char *pname, void *value);
extern int sysprm_get_range (const char *pname, void *min, void *max);
extern int prm_get_master_port_id (void);
extern bool prm_get_commit_on_shutdown (void);
extern bool prm_get_query_mode_sync (void);
extern int prm_adjust_parameters (void);

#endif /* _SYSTEM_PARAMETER_H_ */
