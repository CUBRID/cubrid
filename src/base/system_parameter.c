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

#define PRM_NAME_ER_LOG_LEVEL "error_log_level"

#define PRM_NAME_ER_LOG_WARNING "error_log_warning"

#define PRM_NAME_ER_EXIT_ASK "inquire_on_exit"

#define PRM_NAME_ER_LOG_SIZE "error_log_size"

#define PRM_NAME_ER_LOG_FILE "error_log"

#define PRM_NAME_IO_LOCKF_ENABLE "file_lock"

#define PRM_NAME_SR_NBUFFERS "sort_buffer_pages"

#define PRM_NAME_SORT_BUFFER_SIZE "sort_buffer_size"

#define PRM_NAME_PB_NBUFFERS "data_buffer_pages"

#define PRM_NAME_PB_BUFFER_FLUSH_RATIO "data_buffer_flush_ratio"

#define PRM_NAME_PAGE_BUFFER_SIZE "data_buffer_size"

#define PRM_NAME_HF_UNFILL_FACTOR "unfill_factor"

#define PRM_NAME_HF_MAX_BESTSPACE_ENTRIES "max_bestspace_entries"

#define PRM_NAME_BT_UNFILL_FACTOR "index_unfill_factor"

#define PRM_NAME_BT_OID_NBUFFERS "index_scan_oid_buffer_pages"

#define PRM_NAME_BT_OID_BUFFER_SIZE "index_scan_oid_buffer_size"

#define PRM_NAME_BT_INDEX_SCAN_OID_ORDER "index_scan_in_oid_order"

#define PRM_NAME_BOSR_MAXTMP_PAGES "temp_file_max_size_in_pages"

#define PRM_NAME_LK_TIMEOUT_MESSAGE_DUMP_LEVEL "lock_timeout_message_type"

#define PRM_NAME_LK_ESCALATION_AT "lock_escalation"

#define PRM_NAME_LK_TIMEOUT_SECS "lock_timeout_in_secs"

#define PRM_NAME_LK_RUN_DEADLOCK_INTERVAL "deadlock_detection_interval_in_secs"

#define PRM_NAME_LOG_NBUFFERS "log_buffer_pages"

#define PRM_NAME_LOG_BUFFER_SIZE "log_buffer_size"

#define PRM_NAME_LOG_CHECKPOINT_NPAGES "checkpoint_every_npages"

#define PRM_NAME_LOG_CHECKPOINT_INTERVAL_MINUTES "checkpoint_interval_in_mins"

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

#define PRM_NAME_ADAPTIVE_FLUSH_CONTROL "adaptive_flush_control"

#define PRM_NAME_MAX_FLUSH_PAGES_PER_SECOND "max_flush_pages_per_second"

#define PRM_NAME_PB_SYNC_ON_NFLUSH "sync_on_nflush"

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

#define PRM_NAME_HA_CHANGEMODE_INTERVAL_IN_MSEC "ha_changemode_interval_in_msecs"

#define PRM_NAME_HA_MAX_HEARTBEAT_GAP "ha_max_heartbeat_gap"

#define PRM_NAME_HA_PING_HOSTS "ha_ping_hosts"

#define PRM_NAME_HA_APPLYLOGDB_RETRY_ERROR_LIST "ha_applylogdb_retry_error_list"

#define PRM_NAME_HA_APPLYLOGDB_IGNORE_ERROR_LIST "ha_applylogdb_ignore_error_list"

#define PRM_NAME_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS "ha_applylogdb_log_wait_time_in_secs"

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

#define PRM_NAME_TCP_NODELAY "tcp_nodealy"

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

#define PRM_NAME_SQL_TRACE_TEMP_PATH "sql_trace_temp_path"

/*
 * Note about ERROR_LIST and INTEGER_LIST type
 * ERROR_LIST type is an array of bool type with the size of -(ER_LAST_ERROR)
 * INTEGER_LIST type is an array of int type where the first element is
 * the size of the array. The max size of INTEGER_LIST is 255.
 */

/*
 * Bit masks for flag representing status words
 */

#define PRM_REQUIRED        0x00000001	/* Must be set with default or by user */
#define PRM_SET             0x00000002	/* has been set */
#define PRM_DEFAULT         0x00000004	/* has system default */
#define PRM_USER_CHANGE     0x00000008	/* user can change, not implemented */
#define PRM_ALLOCATED       0x00000010	/* storage has been malloc'd */
#define PRM_LOWER_LIMIT     0x00000020	/* has lower limit */
#define PRM_UPPER_LIMIT     0x00000040	/* has upper limit */
#define PRM_DEFAULT_USED    0x00000080	/* Default value has been used */
#define PRM_FOR_CLIENT      0x00000100	/* is for client parameter */
#define PRM_FOR_SERVER      0x00000200	/* is for server parameter */
#define PRM_HIDDEN          0x00000400	/* is hidden */
#define PRM_RELOADABLE      0x00000800	/* is reloadable */
#define PRM_COMPOUND        0x00001000	/* sets the value of several others */
#define PRM_TEST_CHANGE     0x00002000	/* can only be changed in the test mode */
#define PRM_CLEAR_CACHE	    0x00004000	/* xasl cache should be cleared
					   when this parameter is changed */
#define PRM_FOR_HA          0x00008000	/* is for heartbeat */
#define PRM_FOR_SESSION	    0x00010000	/* is a session parameter */
#define PRM_FORCE_SERVER    0x00020000	/* client should get value from server */
#define PRM_DIFFERENT	    0x00040000	/* parameters that have different
					 * values on client and on server
					 * NOTE: for now it is used only for
					 * session parameteres
					 */

#define PRM_DEPRECATED      0x40000000	/* is deprecated */
#define PRM_OBSOLETED       0x80000000	/* is obsoleted */

/*
 * Macros to get data type
 */
#define PRM_IS_STRING(x)          ((x)->datatype == PRM_STRING)
#define PRM_IS_INTEGER(x)         ((x)->datatype == PRM_INTEGER)
#define PRM_IS_FLOAT(x)           ((x)->datatype == PRM_FLOAT)
#define PRM_IS_BOOLEAN(x)         ((x)->datatype == PRM_BOOLEAN)
#define PRM_IS_KEYWORD(x)         ((x)->datatype == PRM_KEYWORD)
#define PRM_IS_ERROR_LIST(x)      ((x)->datatype == PRM_ERROR_LIST)
#define PRM_IS_INTEGER_LIST(x)    ((x)->datatype == PRM_INTEGER_LIST)
#define PRM_IS_SIZE(x)            ((x)->datatype == PRM_SIZE)

/*
 * Macros to access bit fields
 */

#define PRM_IS_REQUIRED(x)        (x & PRM_REQUIRED)
#define PRM_IS_SET(x)             (x & PRM_SET)
#define PRM_HAS_DEFAULT(x)        (x & PRM_DEFAULT)
#define PRM_USER_CAN_CHANGE(x)    (x & PRM_USER_CHANGE)
#define PRM_IS_ALLOCATED(x)       (x & PRM_ALLOCATED)
#define PRM_HAS_LOWER_LIMIT(x)    (x & PRM_LOWER_LIMIT)
#define PRM_HAS_UPPER_LIMIT(x)    (x & PRM_UPPER_LIMIT)
#define PRM_DEFAULT_VAL_USED(x)   (x & PRM_DEFAULT_USED)
#define PRM_IS_FOR_CLIENT(x)      (x & PRM_FOR_CLIENT)
#define PRM_IS_FOR_SERVER(x)      (x & PRM_FOR_SERVER)
#define PRM_IS_HIDDEN(x)          (x & PRM_HIDDEN)
#define PRM_IS_RELOADABLE(x)      (x & PRM_RELOADABLE)
#define PRM_IS_COMPOUND(x)        (x & PRM_COMPOUND)
#define PRM_TEST_CHANGE_ONLY(x)   (x & PRM_TEST_CHANGE)
#define PRM_SHOULD_CLEAR_CACHE(x) (x & PRM_CLEAR_CACHE)
#define PRM_IS_FOR_HA(x)          (x & PRM_FOR_HA)
#define PRM_IS_FOR_SESSION(x)	  (x & PRM_FOR_SESSION)
#define PRM_GET_FROM_SERVER(x)	  (x & PRM_FORCE_SERVER)
#define PRM_IS_DIFFERENT(x)	  (x & PRM_DIFFERENT)
#define PRM_IS_DEPRECATED(x)      (x & PRM_DEPRECATED)
#define PRM_IS_OBSOLETED(x)       (x & PRM_OBSOLETED)

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
#define PRM_GET_ERROR_LIST(x)   (*((bool **) (x)))
#define PRM_GET_INTEGER_LIST(x) (*((int **) (x)))
#define PRM_GET_SIZE(x)     (*((UINT64 *) (x)))

/*
 * Other macros
 */
#define PRM_DEFAULT_BUFFER_SIZE 256

/* initial error and integer lists */
static bool error_list_initial[1] = { false };
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

int PRM_ER_LOG_LEVEL = ER_SYNTAX_ERROR_SEVERITY;
static int prm_er_log_level_default = ER_SYNTAX_ERROR_SEVERITY;
static int prm_er_log_level_lower = ER_FATAL_ERROR_SEVERITY;
static int prm_er_log_level_upper = ER_NOTIFICATION_SEVERITY;

bool PRM_ER_LOG_WARNING = false;
static bool prm_er_log_warning_default = false;

int PRM_ER_EXIT_ASK = ER_EXIT_DEFAULT;
static int prm_er_exit_ask_default = ER_EXIT_DEFAULT;

int PRM_ER_LOG_SIZE = (100000 * 80L);
static int prm_er_log_size_default = (100000 * 80L);
static int prm_er_log_size_lower = (100 * 80);

const char *PRM_ER_LOG_FILE = sysprm_error_log_file;
static const char *prm_er_log_file_default = sysprm_error_log_file;

bool PRM_ACCESS_IP_CONTROL = false;
static bool prm_access_ip_control_default = false;

const char *PRM_ACCESS_IP_CONTROL_FILE = "";
static const char *prm_access_ip_control_file_default = NULL;

bool PRM_IO_LOCKF_ENABLE = false;
static bool prm_io_lockf_enable_default = true;

int PRM_SR_NBUFFERS = 128;
static int prm_sr_nbuffers_default = 128;
static int prm_sr_nbuffers_lower = 1;

UINT64 PRM_SORT_BUFFER_SIZE = 2097152;
static UINT64 prm_sort_buffer_size_default = 2097152;
static UINT64 prm_sort_buffer_size_lower = 65536;

int PRM_PB_NBUFFERS = 32768;
static int prm_pb_nbuffers_default = 32768;
static int prm_pb_nbuffers_lower = 1024;

float PRM_PB_BUFFER_FLUSH_RATIO = 0.01f;
static float prm_pb_buffer_flush_ratio_default = 0.01f;
static float prm_pb_buffer_flush_ratio_lower = 0.01f;
static float prm_pb_buffer_flush_ratio_upper = 0.95f;

UINT64 PRM_PAGE_BUFFER_SIZE = 536870912;
static UINT64 prm_page_buffer_size_default = 536870912;
static UINT64 prm_page_buffer_size_lower = 16777216;
#if __WORDSIZE == 32
#define MAX_PAGE_BUFFER_SIZE INT_MAX
#else
#define MAX_PAGE_BUFFER_SIZE LONG_MAX
#endif
static UINT64 prm_page_buffer_size_upper = MAX_PAGE_BUFFER_SIZE;

float PRM_HF_UNFILL_FACTOR = 0.10f;
static float prm_hf_unfill_factor_default = 0.10f;
static float prm_hf_unfill_factor_lower = 0.0f;
static float prm_hf_unfill_factor_upper = 0.3f;

int PRM_HF_MAX_BESTSPACE_ENTRIES = 1000000;
static int prm_hf_max_bestspace_entries_default = 1000000;	/* 110 M */

float PRM_BT_UNFILL_FACTOR = 0.05f;
static float prm_bt_unfill_factor_default = 0.05f;
static float prm_bt_unfill_factor_lower = 0.0f;
static float prm_bt_unfill_factor_upper = 0.5f;

float PRM_BT_OID_NBUFFERS = 4.0f;
static float prm_bt_oid_nbuffers_default = 4.0f;
static float prm_bt_oid_nbuffers_lower = 0.05f;
static float prm_bt_oid_nbuffers_upper = 16.0f;

UINT64 PRM_BT_OID_BUFFER_SIZE = 65536;
static UINT64 prm_bt_oid_buffer_size_default = 65536;
static UINT64 prm_bt_oid_buffer_size_lower = 1024;
static UINT64 prm_bt_oid_buffer_size_upper = 262144;

bool PRM_BT_INDEX_SCAN_OID_ORDER = false;
static bool prm_bt_index_scan_oid_order_default = false;

int PRM_BOSR_MAXTMP_PAGES = INT_MIN;
static int prm_bosr_maxtmp_pages = -1;	/* Infinite */

int PRM_LK_TIMEOUT_MESSAGE_DUMP_LEVEL = 0;

int PRM_LK_ESCALATION_AT = 100000;
static int prm_lk_escalation_at_default = 100000;
static int prm_lk_escalation_at_lower = 5;

int PRM_LK_TIMEOUT_SECS = -1;
static int prm_lk_timeout_secs_default = -1;	/* Infinite */
static int prm_lk_timeout_secs_lower = -1;

float PRM_LK_RUN_DEADLOCK_INTERVAL = 1.0f;
static float prm_lk_run_deadlock_interval_default = 1.0f;
static float prm_lk_run_deadlock_interval_lower = 0.1f;

int PRM_LOG_NBUFFERS = 128;
static int prm_log_nbuffers_default = 128;
static int prm_log_nbuffers_lower = 3;

UINT64 PRM_LOG_BUFFER_SIZE = 2097152;
static UINT64 prm_log_buffer_size_default = 2097152;
static UINT64 prm_log_buffer_size_lower = 196608;

int PRM_LOG_CHECKPOINT_NPAGES = 100000;
static int prm_log_checkpoint_npages_default = 100000;
static int prm_log_checkpoint_npages_lower = 10;

int PRM_LOG_CHECKPOINT_INTERVAL_MINUTES = 60;
static int prm_log_checkpoint_interval_minutes_default = 60;
static int prm_log_checkpoint_interval_minutes_lower = 1;

int PRM_LOG_CHECKPOINT_SLEEP_MSECS = 1;
static int prm_log_checkpoint_sleep_msecs_default = 1;
static int prm_log_checkpoint_sleep_msecs_lower = 0;

bool PRM_LOG_BACKGROUND_ARCHIVING = true;
static bool prm_log_background_archiving_default = true;

int PRM_LOG_ISOLATION_LEVEL = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
static int prm_log_isolation_level_default = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
static int prm_log_isolation_level_lower =
  TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
static int prm_log_isolation_level_upper = TRAN_SERIALIZABLE;

bool PRM_COMMIT_ON_SHUTDOWN = false;
static bool prm_commit_on_shutdown_default = false;

int PRM_SHUTDOWN_WAIT_TIME_IN_SECS = 60;
static int prm_shutdown_wait_time_in_secs_default = 60;
static int prm_shutdown_wait_time_in_secs_lower = 60;

bool PRM_CSQL_AUTO_COMMIT = true;
static bool prm_csql_auto_commit_default = true;

bool PRM_LOG_SWEEP_CLEAN = true;
static bool prm_log_sweep_clean_default = true;

int PRM_WS_HASHTABLE_SIZE = 4096;
static int prm_ws_hashtable_size_default = 4096;
static int prm_ws_hashtable_size_lower = 1024;

bool PRM_WS_MEMORY_REPORT = false;
static bool prm_ws_memory_report_default = false;

bool PRM_GC_ENABLE = false;
static bool prm_gc_enable_default = false;

int PRM_TCP_PORT_ID = 1523;
static int prm_tcp_port_id_default = 1523;

int PRM_TCP_CONNECTION_TIMEOUT = 5;
static int prm_tcp_connection_timeout_default = 5;
static int prm_tcp_connection_timeout_lower = -1;

int PRM_OPTIMIZATION_LEVEL = 1;
static int prm_optimization_level_default = 1;

bool PRM_QO_DUMP = false;
static bool prm_qo_dump_default = false;

int PRM_CSS_MAX_CLIENTS = 100;
static int prm_css_max_clients_default = 100;
static int prm_css_max_clients_lower = 10;
static int prm_css_max_clients_upper = 10000;

int PRM_THREAD_STACKSIZE = (1024 * 1024);
static int prm_thread_stacksize_default = (1024 * 1024);
static int prm_thread_stacksize_lower = 64 * 1024;

const char *PRM_CFG_DB_HOSTS = "";
static const char *prm_cfg_db_hosts_default = NULL;

int PRM_RESET_TR_PARSER = 10;
static int prm_reset_tr_parser_default = 10;

int PRM_IO_BACKUP_NBUFFERS = 256;
static int prm_io_backup_nbuffers_default = 256;
static int prm_io_backup_nbuffers_lower = 256;

int PRM_IO_BACKUP_MAX_VOLUME_SIZE = -1;
static int prm_io_backup_max_volume_size_default = -1;
static int prm_io_backup_max_volume_size_lower = 1024 * 32;

int PRM_IO_BACKUP_SLEEP_MSECS = 0;
static int prm_io_backup_sleep_msecs_default = 0;
static int prm_io_backup_sleep_msecs_lower = 0;

int PRM_MAX_PAGES_IN_TEMP_FILE_CACHE = 1000;
static int prm_max_pages_in_temp_file_cache_default = 1000;	/* pages */
static int prm_max_pages_in_temp_file_cache_lower = 100;

int PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE = 512;
static int prm_max_entries_in_temp_file_cache_default = 512;
static int prm_max_entries_in_temp_file_cache_lower = 10;

bool PRM_PTHREAD_SCOPE_PROCESS = true;
static bool prm_pthread_scope_process_default = true;

int PRM_TEMP_MEM_BUFFER_PAGES = 4;
static int prm_temp_mem_buffer_pages_default = 4;
static int prm_temp_mem_buffer_pages_lower = 0;
static int prm_temp_mem_buffer_pages_upper = 20;

int PRM_INDEX_SCAN_KEY_BUFFER_PAGES = 20;
static int prm_index_scan_key_buffer_pages_default = 20;
static int prm_index_scan_key_buffer_pages_lower = 0;

UINT64 PRM_INDEX_SCAN_KEY_BUFFER_SIZE = 327680;
static UINT64 prm_index_scan_key_buffer_size_default = 327680;
static UINT64 prm_index_scan_key_buffer_size_lower = 0;

bool PRM_DONT_REUSE_HEAP_FILE = false;
static bool prm_dont_reuse_heap_file_default = false;

bool PRM_QUERY_MODE_SYNC = false;
static bool prm_query_mode_sync_default = false;

int PRM_INSERT_MODE = 1 + 8 + 16;
static int prm_insert_mode_default = 1 + 8 + 16;
static int prm_insert_mode_lower = 0;
static int prm_insert_mode_upper = 31;

int PRM_LK_MAX_SCANID_BIT = 32;
static int prm_lk_max_scanid_bit_default = 32;
static int prm_lk_max_scanid_bit_lower = 32;
static int prm_lk_max_scanid_bit_upper = 128;

bool PRM_HOSTVAR_LATE_BINDING = false;
static bool prm_hostvar_late_binding_default = false;

bool PRM_ENABLE_HISTO = false;
static bool prm_enable_histo_default = false;

int PRM_MUTEX_BUSY_WAITING_CNT = 0;
static int prm_mutex_busy_waiting_cnt_default = 0;

int PRM_PB_NUM_LRU_CHAINS = 0;
static int prm_pb_num_LRU_chains_default = 0;	/* system define */
static int prm_pb_num_LRU_chains_lower = 0;
static int prm_pb_num_LRU_chains_upper = 1000;

int PRM_PAGE_BG_FLUSH_INTERVAL_MSEC = 0;
static int prm_page_bg_flush_interval_msec_default = 0;
static int prm_page_bg_flush_interval_msec_lower = -1;

bool PRM_ADAPTIVE_FLUSH_CONTROL = true;
static bool prm_adaptive_flush_control_default = true;

int PRM_MAX_FLUSH_PAGES_PER_SECOND = 10000;
static int prm_max_flush_pages_per_second_default = 10000;
static int prm_max_flush_pages_per_second_lower = 1;
static int prm_max_flush_pages_per_second_upper = INT_MAX;

int PRM_PB_SYNC_ON_NFLUSH = 200;
static int prm_pb_sync_on_nflush_default = 200;
static int prm_pb_sync_on_nflush_lower = 1;
static int prm_pb_sync_on_nflush_upper = INT_MAX;

int PRM_PB_DEBUG_PAGE_VALIDATION_LEVEL = PGBUF_DEBUG_NO_PAGE_VALIDATION;
#if !defined(NDEBUG)
static int prm_pb_debug_page_validation_level_default =
  PGBUF_DEBUG_PAGE_VALIDATION_FETCH;
#else /* !NDEBUG */
static int prm_pb_debug_page_validation_level_default =
  PGBUF_DEBUG_NO_PAGE_VALIDATION;
#endif /* !NDEBUG */

bool PRM_ORACLE_STYLE_OUTERJOIN = false;
static bool prm_oracle_style_outerjoin_default = false;

int PRM_COMPAT_MODE = COMPAT_CUBRID;
static int prm_compat_mode_default = COMPAT_CUBRID;
static int prm_compat_mode_lower = COMPAT_CUBRID;
static int prm_compat_mode_upper = COMPAT_ORACLE;

bool PRM_ANSI_QUOTES = true;
static bool prm_ansi_quotes_default = true;

int PRM_DEFAULT_WEEK_FORMAT = 0;
static int prm_week_format_default = 0;
static int prm_week_format_lower = 0;
static int prm_week_format_upper = 7;

bool PRM_TEST_MODE = false;
static bool prm_test_mode_default = false;

bool PRM_ONLY_FULL_GROUP_BY = false;
static bool prm_only_full_group_by_default = false;

bool PRM_PIPES_AS_CONCAT = true;
static bool prm_pipes_as_concat_default = true;

bool PRM_MYSQL_TRIGGER_CORRELATION_NAMES = false;
static bool prm_mysql_trigger_correlation_names_default = false;

bool PRM_REQUIRE_LIKE_ESCAPE_CHARACTER = false;
static bool prm_require_like_escape_character_default = false;

bool PRM_NO_BACKSLASH_ESCAPES = true;
static bool prm_no_backslash_escapes_default = true;

int PRM_GROUP_CONCAT_MAX_LEN = 1024;
static int prm_group_concat_max_len_default = 1024;
static int prm_group_concat_max_len_lower = 4;

int PRM_STRING_MAX_SIZE_BYTES = 1024 * 1024;
static int prm_string_max_size_bytes_default = 1024 * 1024;
static int prm_string_max_size_bytes_lower = 64;
static int prm_string_max_size_bytes_upper = 32 * 1024 * 1024;

bool PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT = false;
static bool prm_add_column_update_hard_default_default = false;

bool PRM_RETURN_NULL_ON_FUNCTION_ERRORS = false;
static bool prm_return_null_on_function_errors_default = false;

bool PRM_ALTER_TABLE_CHANGE_TYPE_STRICT = false;
static bool prm_alter_table_change_type_strict_default = false;

bool PRM_PLUS_AS_CONCAT = true;
static bool prm_plus_as_concat_default = true;

int PRM_COMPACTDB_PAGE_RECLAIM_ONLY = 0;
static int prm_compactdb_page_reclaim_only_default = 0;

float PRM_LIKE_TERM_SELECTIVITY = 0.1f;
static float prm_like_term_selectivity_default = 0.1f;
static float prm_like_term_selectivity_upper = 1.0f;
static float prm_like_term_selectivity_lower = 0.0f;

int PRM_MAX_OUTER_CARD_OF_IDXJOIN = 0;
static int prm_max_outer_card_of_idxjoin_default = 0;
static int prm_max_outer_card_of_idxjoin_lower = 0;

bool PRM_ORACLE_STYLE_EMPTY_STRING = false;
static bool prm_oracle_style_empty_string_default = false;

int PRM_SUPPRESS_FSYNC = 0;
static int prm_suppress_fsync_default = 0;
static int prm_suppress_fsync_upper = 100;
static int prm_suppress_fsync_lower = 0;

bool PRM_CALL_STACK_DUMP_ON_ERROR = false;
static bool prm_call_stack_dump_on_error_default = false;

bool *PRM_CALL_STACK_DUMP_ACTIVATION = error_list_initial;
static bool *prm_call_stack_dump_activation_default = NULL;

bool *PRM_CALL_STACK_DUMP_DEACTIVATION = error_list_initial;
static bool *prm_call_stack_dump_deactivation_default = NULL;

bool PRM_COMPAT_NUMERIC_DIVISION_SCALE = false;
static bool prm_compat_numeric_division_scale_default = false;

bool PRM_DBFILES_PROTECT = false;
static bool prm_dbfiles_protect_default = false;

bool PRM_AUTO_RESTART_SERVER = true;
static bool prm_auto_restart_server_default = true;

int PRM_XASL_MAX_PLAN_CACHE_ENTRIES = 1000;
static int prm_xasl_max_plan_cache_entries_default = 1000;

#if defined (ENABLE_UNUSED_FUNCTION)
int PRM_XASL_MAX_PLAN_CACHE_CLONES = -1;
static int prm_xasl_max_plan_cache_clones_default = -1;	/* disabled */
#endif /* ENABLE_UNUSED_FUNCTION */

int PRM_FILTER_PRED_MAX_CACHE_ENTRIES = 1000;
static int prm_filter_pred_max_cache_entries_default = 1000;

int PRM_FILTER_PRED_MAX_CACHE_CLONES = 10;
static int prm_filter_pred_max_cache_clones_default = 10;

int PRM_XASL_PLAN_CACHE_TIMEOUT = -1;
static int prm_xasl_plan_cache_timeout_default = -1;	/* infinity */

int PRM_LIST_QUERY_CACHE_MODE = 0;
static int prm_list_query_cache_mode_default = 0;	/* disabled */
static int prm_list_query_cache_mode_upper = 2;
static int prm_list_query_cache_mode_lower = 0;

int PRM_LIST_MAX_QUERY_CACHE_ENTRIES = -1;
static int prm_list_max_query_cache_entries_default = -1;	/* disabled */

int PRM_LIST_MAX_QUERY_CACHE_PAGES = -1;
static int prm_list_max_query_cache_pages_default = -1;	/* infinity */

bool PRM_USE_ORDERBY_SORT_LIMIT = true;
static int prm_use_orderby_sort_limit_default = true;

int PRM_HA_MODE = HA_MODE_OFF;
static int prm_ha_mode_default = HA_MODE_OFF;
static int prm_ha_mode_upper = HA_MODE_REPLICA;
static int prm_ha_mode_lower = HA_MODE_OFF;
int PRM_HA_MODE_FOR_SA_UTILS_ONLY = HA_MODE_OFF;

int PRM_HA_SERVER_STATE = HA_SERVER_STATE_IDLE;
static int prm_ha_server_state_default = HA_SERVER_STATE_IDLE;
static int prm_ha_server_state_upper = HA_SERVER_STATE_DEAD;
static int prm_ha_server_state_lower = HA_SERVER_STATE_IDLE;

int PRM_HA_LOG_APPLIER_STATE = HA_LOG_APPLIER_STATE_UNREGISTERED;
static int prm_ha_log_applier_state_default =
  HA_LOG_APPLIER_STATE_UNREGISTERED;
static int prm_ha_log_applier_state_upper = HA_LOG_APPLIER_STATE_ERROR;
static int prm_ha_log_applier_state_lower = HA_LOG_APPLIER_STATE_UNREGISTERED;

const char *PRM_HA_NODE_LIST = "";
static const char *prm_ha_node_list_default = NULL;

const char *PRM_HA_REPLICA_LIST = "";
static const char *prm_ha_replica_list_default = NULL;

const char *PRM_HA_DB_LIST = "";
static const char *prm_ha_db_list_default = NULL;

const char *PRM_HA_COPY_LOG_BASE = "";
static const char *prm_ha_copy_log_base_default = NULL;

const char *PRM_HA_COPY_SYNC_MODE = "";
static const char *prm_ha_copy_sync_mode_default = NULL;

int PRM_HA_APPLY_MAX_MEM_SIZE = HB_DEFAULT_APPLY_MAX_MEM_SIZE;
static int prm_ha_apply_max_mem_size_default = HB_DEFAULT_APPLY_MAX_MEM_SIZE;

int PRM_HA_PORT_ID = HB_DEFAULT_HA_PORT_ID;
static int prm_ha_port_id_default = HB_DEFAULT_HA_PORT_ID;

int PRM_HA_INIT_TIMER_IN_MSECS = HB_DEFAULT_INIT_TIMER_IN_MSECS;
static int prm_ha_init_timer_im_msecs_default =
  HB_DEFAULT_INIT_TIMER_IN_MSECS;

int PRM_HA_HEARTBEAT_INTERVAL_IN_MSECS =
  HB_DEFAULT_HEARTBEAT_INTERVAL_IN_MSECS;
static int prm_ha_heartbeat_interval_in_msecs_default =
  HB_DEFAULT_HEARTBEAT_INTERVAL_IN_MSECS;

int PRM_HA_CALC_SCORE_INTERVAL_IN_MSECS =
  HB_DEFAULT_CALC_SCORE_INTERVAL_IN_MSECS;
static int prm_ha_calc_score_interval_in_msecs_default =
  HB_DEFAULT_CALC_SCORE_INTERVAL_IN_MSECS;

int PRM_HA_FAILOVER_WAIT_TIME_IN_MSECS =
  HB_DEFAULT_FAILOVER_WAIT_TIME_IN_MSECS;
static int prm_ha_failover_wait_time_in_msecs_default =
  HB_DEFAULT_FAILOVER_WAIT_TIME_IN_MSECS;

int PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS =
  HB_DEFAULT_START_CONFIRM_INTERVAL_IN_MSECS;
static int prm_ha_process_start_confirm_interval_in_msecs_default =
  HB_DEFAULT_START_CONFIRM_INTERVAL_IN_MSECS;

int PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS =
  HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS;
static int prm_ha_process_dereg_confirm_interval_in_msecs_default =
  HB_DEFAULT_DEREG_CONFIRM_INTERVAL_IN_MSECS;

int PRM_HA_MAX_PROCESS_START_CONFIRM = HB_DEFAULT_MAX_PROCESS_START_CONFIRM;
static int prm_ha_max_process_start_confirm_default =
  HB_DEFAULT_MAX_PROCESS_START_CONFIRM;

int PRM_HA_MAX_PROCESS_DEREG_CONFIRM = HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM;
static int prm_ha_max_process_dereg_confirm_default =
  HB_DEFAULT_MAX_PROCESS_DEREG_CONFIRM;

int PRM_HA_CHANGEMODE_INTERVAL_IN_MSECS =
  HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS;
static int prm_ha_changemode_interval_in_msecs_default =
  HB_DEFAULT_CHANGEMODE_INTERVAL_IN_MSECS;

int PRM_HA_MAX_HEARTBEAT_GAP = HB_DEFAULT_MAX_HEARTBEAT_GAP;
static int prm_ha_max_heartbeat_gap_default = HB_DEFAULT_MAX_HEARTBEAT_GAP;

const char *PRM_HA_PING_HOSTS = "";
static const char *prm_ha_ping_hosts_default = NULL;

bool *PRM_HA_APPLYLOGDB_RETRY_ERROR_LIST = error_list_initial;
static bool *prm_ha_applylogdb_retry_error_list_default = NULL;

bool *PRM_HA_APPLYLOGDB_IGNORE_ERROR_LIST = error_list_initial;
static bool *prm_ha_applylogdb_ignore_error_list_default = NULL;

int PRM_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS = -1;
static int prm_ha_applylogdb_log_wait_time_in_secs_default = -1;
static int prm_ha_applylogdb_log_wait_time_in_secs_lower = -1;

bool PRM_JAVA_STORED_PROCEDURE = false;
static bool prm_java_stored_procedure_default = false;

bool PRM_COMPAT_PRIMARY_KEY = false;
static bool prm_compat_primary_key_default = false;

int PRM_LOG_HEADER_FLUSH_INTERVAL = 5;
static int prm_log_header_flush_interval_default = 5;
static int prm_log_header_flush_interval_lower = 1;

bool PRM_LOG_ASYNC_COMMIT = false;
static bool prm_log_async_commit_default = false;

int PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS = 0;
static int prm_log_group_commit_interval_msecs_default = 0;
static int prm_log_group_commit_interval_msecs_lower = 0;

int PRM_LOG_BG_FLUSH_INTERVAL_MSECS = 0;
static int prm_log_bg_flush_interval_msecs_default = 0;	/* not used */
static int prm_log_bg_flush_interval_msecs_lower = 0;

int PRM_LOG_BG_FLUSH_NUM_PAGES = 0;
static int prm_log_bg_flush_num_pages_default = 0;
static int prm_log_bg_flush_num_pages_lower = 0;

bool PRM_INTL_MBS_SUPPORT = false;
static bool prm_intl_mbs_support_default = false;

bool PRM_LOG_COMPRESS = false;
static bool prm_log_compress_default = true;

bool PRM_BLOCK_NOWHERE_STATEMENT = false;
static bool prm_block_nowhere_statement_default = false;

bool PRM_BLOCK_DDL_STATEMENT = false;
static bool prm_block_ddl_statement_default = false;

#if defined (ENABLE_UNUSED_FUNCTION)
bool PRM_SINGLE_BYTE_COMPARE = false;
static bool prm_single_byte_compare_default = false;
#endif

int PRM_CSQL_HISTORY_NUM = 50;
static int prm_csql_history_num_default = 50;
static int prm_csql_history_num_upper = 200;
static int prm_csql_history_num_lower = 1;

bool PRM_LOG_TRACE_DEBUG = false;
static bool prm_log_trace_debug_default = false;

const char *PRM_DL_FORK = "";
static const char *prm_dl_fork_default = NULL;

bool PRM_ER_PRODUCTION_MODE = true;
static bool prm_er_production_mode_default = true;

int PRM_ER_STOP_ON_ERROR = 0;
static int prm_er_stop_on_error_default = 0;
static int prm_er_stop_on_error_upper = 0;

int PRM_TCP_RCVBUF_SIZE = -1;
static int prm_tcp_rcvbuf_size_default = -1;

int PRM_TCP_SNDBUF_SIZE = -1;
static int prm_tcp_sndbuf_size_default = -1;

int PRM_TCP_NODELAY = -1;
static int prm_tcp_nodelay_default = -1;

bool PRM_CSQL_SINGLE_LINE_MODE = false;
static bool prm_csql_single_line_mode_default = false;

bool PRM_XASL_DEBUG_DUMP = false;
static bool prm_xasl_debug_dump_default = false;

int PRM_LOG_MAX_ARCHIVES = INT_MAX;
static int prm_log_max_archives_default = INT_MAX;
static int prm_log_max_archives_lower = 0;

bool PRM_FORCE_REMOVE_LOG_ARCHIVES = true;
static bool prm_force_remove_log_archives_default = true;

int PRM_REMOVE_LOG_ARCHIVES_INTERVAL = 0;
static int prm_remove_log_archives_interval_default = 0;
static int prm_remove_log_archives_interval_lower = 0;

bool PRM_LOG_NO_LOGGING = false;
static bool prm_log_no_logging_default = false;

bool PRM_UNLOADDB_IGNORE_ERROR = false;
static bool prm_unloaddb_ignore_error_default = false;

int PRM_UNLOADDB_LOCK_TIMEOUT = -1;
static int prm_unloaddb_lock_timeout_default = -1;
static int prm_unloaddb_lock_timeout_lower = -1;

int PRM_LOADDB_FLUSH_INTERVAL = 1000;
static int prm_loaddb_flush_interval_default = 1000;
static int prm_loaddb_flush_interval_lower = 0;

const char *PRM_IO_TEMP_VOLUME_PATH = "";
static char *prm_io_temp_volume_path_default = NULL;

const char *PRM_IO_VOLUME_EXT_PATH = "";
static char *prm_io_volume_ext_path_default = NULL;

bool PRM_UNIQUE_ERROR_KEY_VALUE = false;
static bool prm_unique_error_key_value_default = false;

bool PRM_USE_SYSTEM_MALLOC = false;
static bool prm_use_system_malloc_default = false;

const char *PRM_EVENT_HANDLER = "";
static const char *prm_event_handler_default = NULL;

bool *PRM_EVENT_ACTIVATION = error_list_initial;
static bool *prm_event_activation_default = NULL;

bool PRM_READ_ONLY_MODE = false;
static bool prm_read_only_mode_default = false;

int PRM_MNT_WAITING_THREAD = 0;
static int prm_mnt_waiting_thread_default = 0;
static int prm_mnt_waiting_thread_lower = 0;

int *PRM_MNT_STATS_THRESHOLD = int_list_initial;
static int *prm_mnt_stats_threshold_default = NULL;

const char *PRM_SERVICE_SERVICE_LIST = "";
static const char *prm_service_service_list_default = NULL;

const char *PRM_SERVICE_SERVER_LIST = "";
static const char *prm_service_server_list_default = NULL;

int PRM_SESSION_STATE_TIMEOUT = 60 * 60 * 6;	/* 6 hours */
static int prm_session_timeout_default = 60 * 60 * 6;	/* 6 hours */
static int prm_session_timeout_lower = 60;	/* 1 minute */
static int prm_session_timeout_upper = 60 * 60 * 24 * 365;	/* 1 nonleap year */

int PRM_MULTI_RANGE_OPT_LIMIT = 100;
static int prm_multi_range_opt_limit_default = 100;
static int prm_multi_range_opt_limit_lower = 0;	/*disabled */
static int prm_multi_range_opt_limit_upper = 10000;

UINT64 PRM_DB_VOLUME_SIZE = 536870912ULL;
static UINT64 prm_db_volume_size_default = 536870912ULL;	/* 512M */
static UINT64 prm_db_volume_size_lower = 20971520ULL;	/* 20M */
static UINT64 prm_db_volume_size_upper = 21474836480ULL;	/* 20G */

UINT64 PRM_LOG_VOLUME_SIZE = 536870912ULL;
static UINT64 prm_log_volume_size_default = 536870912ULL;	/* 512M */
static UINT64 prm_log_volume_size_lower = 20971520ULL;	/* 20M */
static UINT64 prm_log_volume_size_upper = 4294967296ULL;	/* 4G */

char *PRM_INTL_NUMBER_LANG = NULL;
static char *prm_intl_number_lang_default = NULL;

char *PRM_INTL_DATE_LANG = NULL;
static char *prm_intl_date_lang_default = NULL;

bool PRM_UNICODE_INPUT_NORMALIZATION = false;
static bool prm_unicode_input_normalization_default = false;

bool PRM_UNICODE_OUTPUT_NORMALIZATION = false;
static bool prm_unicode_output_normalization_default = false;

bool PRM_INTL_CHECK_INPUT_STRING = false;
static bool prm_intl_check_input_string_default = false;

int PRM_CHECK_PEER_ALIVE = CSS_CHECK_PEER_ALIVE_BOTH;
static int prm_check_peer_alive_default = CSS_CHECK_PEER_ALIVE_BOTH;

int PRM_SQL_TRACE_SLOW_MSECS = -1;
static int prm_sql_trace_slow_msecs_default = -1;
static int prm_sql_trace_slow_msecs_lower = -1;
static int prm_sql_trace_slow_msecs_upper = 1000 * 60 * 60 * 24;	/* 24 hours */

bool PRM_SQL_TRACE_EXECUTION_PLAN = false;
static bool prm_sql_trace_execution_plan_default = false;

char *PRM_SQL_TRACE_TEMP_PATH = "";
static char *prm_sql_trace_temp_path_default = NULL;

typedef struct sysprm_param SYSPRM_PARAM;
struct sysprm_param
{
  const char *name;		/* the keyword expected */
  unsigned int flag;		/* bitmask flag representing status words */
  SYSPRM_DATATYPE datatype;	/* value data type */
  void *default_value;		/* address of (pointer to) default value */
  void *value;			/* address of (pointer to) current value */
  void *upper_limit;		/* highest allowable value */
  void *lower_limit;		/* lowest allowable value */
  char *force_value;		/* address of (pointer to) force value string */
};

static SYSPRM_PARAM prm_Def[] = {
  {PRM_NAME_ER_LOG_DEBUG,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_er_log_debug_default,
   (void *) &PRM_ER_LOG_DEBUG,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ER_LOG_LEVEL,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_KEYWORD,
   (void *) &prm_er_log_level_default,
   (void *) &PRM_ER_LOG_LEVEL,
   (void *) &prm_er_log_level_upper, (void *) &prm_er_log_level_lower,
   (char *) NULL},
  {PRM_NAME_ER_LOG_WARNING,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_er_log_warning_default,
   (void *) &PRM_ER_LOG_WARNING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ER_EXIT_ASK,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_er_exit_ask_default,
   (void *) &PRM_ER_EXIT_ASK,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ER_LOG_SIZE,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_er_log_size_default,
   (void *) &PRM_ER_LOG_SIZE,
   (void *) NULL, (void *) &prm_er_log_size_lower,
   (char *) NULL},
  {PRM_NAME_ER_LOG_FILE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_er_log_file_default,
   (void *) &PRM_ER_LOG_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ACCESS_IP_CONTROL,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_access_ip_control_default,
   (void *) &PRM_ACCESS_IP_CONTROL,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ACCESS_IP_CONTROL_FILE,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_access_ip_control_file_default,
   (void *) &PRM_ACCESS_IP_CONTROL_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_IO_LOCKF_ENABLE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_io_lockf_enable_default,
   (void *) &PRM_IO_LOCKF_ENABLE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SR_NBUFFERS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_DEPRECATED),
   PRM_INTEGER,
   (void *) &prm_sr_nbuffers_default,
   (void *) &PRM_SR_NBUFFERS,
   (void *) NULL, (void *) &prm_sr_nbuffers_lower,
   (char *) NULL},
  {PRM_NAME_SORT_BUFFER_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_SIZE,
   (void *) &prm_sort_buffer_size_default,
   (void *) &PRM_SORT_BUFFER_SIZE,
   (void *) NULL, (void *) &prm_sort_buffer_size_lower,
   (char *) NULL},
  {PRM_NAME_PB_NBUFFERS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_DEPRECATED),
   PRM_INTEGER,
   (void *) &prm_pb_nbuffers_default,
   (void *) &PRM_PB_NBUFFERS,
   (void *) NULL, (void *) &prm_pb_nbuffers_lower,
   (char *) NULL},
  {PRM_NAME_PB_BUFFER_FLUSH_RATIO,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN |
    PRM_USER_CHANGE),
   PRM_FLOAT,
   (void *) &prm_pb_buffer_flush_ratio_default,
   (void *) &PRM_PB_BUFFER_FLUSH_RATIO,
   (void *) &prm_pb_buffer_flush_ratio_upper,
   (void *) &prm_pb_buffer_flush_ratio_lower,
   (char *) NULL},
  {PRM_NAME_PAGE_BUFFER_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_SIZE,
   (void *) &prm_page_buffer_size_default,
   (void *) &PRM_PAGE_BUFFER_SIZE,
   (void *) &prm_page_buffer_size_upper, (void *) &prm_page_buffer_size_lower,
   (char *) NULL},
  {PRM_NAME_HF_UNFILL_FACTOR,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_FLOAT,
   (void *) &prm_hf_unfill_factor_default,
   (void *) &PRM_HF_UNFILL_FACTOR,
   (void *) &prm_hf_unfill_factor_upper, (void *) &prm_hf_unfill_factor_lower,
   (char *) NULL},
  {PRM_NAME_HF_MAX_BESTSPACE_ENTRIES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN |
    PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_hf_max_bestspace_entries_default,
   (void *) &PRM_HF_MAX_BESTSPACE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_BT_UNFILL_FACTOR,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_FLOAT,
   (void *) &prm_bt_unfill_factor_default,
   (void *) &PRM_BT_UNFILL_FACTOR,
   (void *) &prm_bt_unfill_factor_upper, (void *) &prm_bt_unfill_factor_lower,
   (char *) NULL},
  {PRM_NAME_BT_OID_NBUFFERS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_DEPRECATED),
   PRM_FLOAT,
   (void *) &prm_bt_oid_nbuffers_default,
   (void *) &PRM_BT_OID_NBUFFERS,
   (void *) &prm_bt_oid_nbuffers_upper, (void *) &prm_bt_oid_nbuffers_lower,
   (char *) NULL},
  {PRM_NAME_BT_OID_BUFFER_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_SIZE,
   (void *) &prm_bt_oid_buffer_size_default,
   (void *) &PRM_BT_OID_BUFFER_SIZE,
   (void *) &prm_bt_oid_buffer_size_upper,
   (void *) &prm_bt_oid_buffer_size_lower,
   (char *) NULL},
  {PRM_NAME_BT_INDEX_SCAN_OID_ORDER,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_bt_index_scan_oid_order_default,
   (void *) &PRM_BT_INDEX_SCAN_OID_ORDER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_BOSR_MAXTMP_PAGES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_bosr_maxtmp_pages,
   (void *) &PRM_BOSR_MAXTMP_PAGES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LK_TIMEOUT_MESSAGE_DUMP_LEVEL,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LK_ESCALATION_AT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_lk_escalation_at_default,
   (void *) &PRM_LK_ESCALATION_AT,
   (void *) NULL, (void *) &prm_lk_escalation_at_lower,
   (char *) NULL},
  {PRM_NAME_LK_TIMEOUT_SECS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_lk_timeout_secs_default,
   (void *) &PRM_LK_TIMEOUT_SECS,
   (void *) NULL, (void *) &prm_lk_timeout_secs_lower,
   (char *) NULL},
  {PRM_NAME_LK_RUN_DEADLOCK_INTERVAL,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_FLOAT,
   (void *) &prm_lk_run_deadlock_interval_default,
   (void *) &PRM_LK_RUN_DEADLOCK_INTERVAL,
   (void *) NULL, (void *) &prm_lk_run_deadlock_interval_lower,
   (char *) NULL},
  {PRM_NAME_LOG_NBUFFERS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_DEPRECATED),
   PRM_INTEGER,
   (void *) &prm_log_nbuffers_default,
   (void *) &PRM_LOG_NBUFFERS,
   (void *) NULL, (void *) &prm_log_nbuffers_lower,
   (char *) NULL},
  {PRM_NAME_LOG_BUFFER_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_SIZE,
   (void *) &prm_log_buffer_size_default,
   (void *) &PRM_LOG_BUFFER_SIZE,
   (void *) NULL, (void *) &prm_log_buffer_size_lower,
   (char *) NULL},
  {PRM_NAME_LOG_CHECKPOINT_NPAGES,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_npages_default,
   (void *) &PRM_LOG_CHECKPOINT_NPAGES,
   (void *) NULL, (void *) &prm_log_checkpoint_npages_lower,
   (char *) NULL},
  {PRM_NAME_LOG_CHECKPOINT_INTERVAL_MINUTES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_interval_minutes_default,
   (void *) &PRM_LOG_CHECKPOINT_INTERVAL_MINUTES,
   (void *) NULL, (void *) &prm_log_checkpoint_interval_minutes_lower,
   (char *) NULL},
  {PRM_NAME_LOG_CHECKPOINT_SLEEP_MSECS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_log_checkpoint_sleep_msecs_default,
   (void *) &PRM_LOG_CHECKPOINT_SLEEP_MSECS,
   (void *) NULL, (void *) &prm_log_checkpoint_sleep_msecs_lower,
   (char *) NULL},
  {PRM_NAME_LOG_BACKGROUND_ARCHIVING,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_log_background_archiving_default,
   (void *) &PRM_LOG_BACKGROUND_ARCHIVING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LOG_ISOLATION_LEVEL,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_KEYWORD,
   (void *) &prm_log_isolation_level_default,
   (void *) &PRM_LOG_ISOLATION_LEVEL,
   (void *) &prm_log_isolation_level_upper,
   (void *) &prm_log_isolation_level_lower,
   (char *) NULL},
  {PRM_NAME_LOG_MEDIA_FAILURE_SUPPORT,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_COMMIT_ON_SHUTDOWN,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_commit_on_shutdown_default,
   (void *) &PRM_COMMIT_ON_SHUTDOWN,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SHUTDOWN_WAIT_TIME_IN_SECS,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_shutdown_wait_time_in_secs_default,
   (void *) &PRM_SHUTDOWN_WAIT_TIME_IN_SECS,
   (void *) NULL, (void *) &prm_shutdown_wait_time_in_secs_lower,
   (char *) NULL},
  {PRM_NAME_CSQL_AUTO_COMMIT,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_csql_auto_commit_default,
   (void *) &PRM_CSQL_AUTO_COMMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LOG_SWEEP_CLEAN,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_log_sweep_clean_default,
   (void *) &PRM_LOG_SWEEP_CLEAN,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_WS_HASHTABLE_SIZE,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_ws_hashtable_size_default,
   (void *) &PRM_WS_HASHTABLE_SIZE,
   (void *) NULL, (void *) &prm_ws_hashtable_size_lower,
   (char *) NULL},
  {PRM_NAME_WS_MEMORY_REPORT,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_ws_memory_report_default,
   (void *) &PRM_WS_MEMORY_REPORT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_GC_ENABLE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_gc_enable_default,
   (void *) &PRM_GC_ENABLE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_TCP_PORT_ID,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_tcp_port_id_default,
   (void *) &PRM_TCP_PORT_ID,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_TCP_CONNECTION_TIMEOUT,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_tcp_connection_timeout_default,
   (void *) &PRM_TCP_CONNECTION_TIMEOUT,
   (void *) NULL, (void *) &prm_tcp_connection_timeout_lower,
   (char *) NULL},
  {PRM_NAME_OPTIMIZATION_LEVEL,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_optimization_level_default,
   (void *) &PRM_OPTIMIZATION_LEVEL,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_QO_DUMP,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_qo_dump_default,
   (void *) &PRM_QO_DUMP,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_CSS_MAX_CLIENTS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_css_max_clients_default,
   (void *) &PRM_CSS_MAX_CLIENTS,
   (void *) NULL, (void *) &prm_css_max_clients_lower,
   (char *) NULL},
  {PRM_NAME_THREAD_STACKSIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_thread_stacksize_default,
   (void *) &PRM_THREAD_STACKSIZE,
   (void *) NULL, (void *) &prm_thread_stacksize_lower,
   (char *) NULL},
  {PRM_NAME_CFG_DB_HOSTS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_STRING,
   (void *) &prm_cfg_db_hosts_default,
   (void *) &PRM_CFG_DB_HOSTS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_RESET_TR_PARSER,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_reset_tr_parser_default,
   (void *) &PRM_RESET_TR_PARSER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_IO_BACKUP_NBUFFERS,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_io_backup_nbuffers_default,
   (void *) &PRM_IO_BACKUP_NBUFFERS,
   (void *) NULL, (void *) &prm_io_backup_nbuffers_lower,
   (char *) NULL},
  {PRM_NAME_IO_BACKUP_MAX_VOLUME_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_io_backup_max_volume_size_default,
   (void *) &PRM_IO_BACKUP_MAX_VOLUME_SIZE,
   (void *) NULL, (void *) &prm_io_backup_max_volume_size_lower,
   (char *) NULL},
  {PRM_NAME_IO_BACKUP_SLEEP_MSECS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_io_backup_sleep_msecs_default,
   (void *) &PRM_IO_BACKUP_SLEEP_MSECS,
   (void *) NULL, (void *) &prm_io_backup_sleep_msecs_lower,
   (char *) NULL},
  {PRM_NAME_MAX_PAGES_IN_TEMP_FILE_CACHE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_max_pages_in_temp_file_cache_default,
   (void *) &PRM_MAX_PAGES_IN_TEMP_FILE_CACHE,
   (void *) NULL, (void *) &prm_max_pages_in_temp_file_cache_lower,
   (char *) NULL},
  {PRM_NAME_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_max_entries_in_temp_file_cache_default,
   (void *) &PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
   (void *) NULL, (void *) &prm_max_entries_in_temp_file_cache_lower,
   (char *) NULL},
  {PRM_NAME_PTHREAD_SCOPE_PROCESS,	/* AIX only */
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_pthread_scope_process_default,
   (void *) &PRM_PTHREAD_SCOPE_PROCESS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_TEMP_MEM_BUFFER_PAGES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_temp_mem_buffer_pages_default,
   (void *) &PRM_TEMP_MEM_BUFFER_PAGES,
   (void *) &prm_temp_mem_buffer_pages_upper,
   (void *) &prm_temp_mem_buffer_pages_lower,
   (char *) NULL},
  {PRM_NAME_INDEX_SCAN_KEY_BUFFER_PAGES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_DEPRECATED),
   PRM_INTEGER,
   (void *) &prm_index_scan_key_buffer_pages_default,
   (void *) &PRM_INDEX_SCAN_KEY_BUFFER_PAGES,
   (void *) NULL,
   (void *) &prm_index_scan_key_buffer_pages_lower,
   (char *) NULL},
  {PRM_NAME_INDEX_SCAN_KEY_BUFFER_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_SIZE,
   (void *) &prm_index_scan_key_buffer_size_default,
   (void *) &PRM_INDEX_SCAN_KEY_BUFFER_SIZE,
   (void *) NULL,
   (void *) &prm_index_scan_key_buffer_size_lower,
   (char *) NULL},
  {PRM_NAME_DONT_REUSE_HEAP_FILE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_dont_reuse_heap_file_default,
   (void *) &PRM_DONT_REUSE_HEAP_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_QUERY_MODE_SYNC,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_query_mode_sync_default,
   (void *) &PRM_QUERY_MODE_SYNC,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_INSERT_MODE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_insert_mode_default,
   (void *) &PRM_INSERT_MODE,
   (void *) &prm_insert_mode_upper,
   (void *) &prm_insert_mode_lower,
   (char *) NULL},
  {PRM_NAME_LK_MAX_SCANID_BIT,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_lk_max_scanid_bit_default,
   (void *) &PRM_LK_MAX_SCANID_BIT,
   (void *) &prm_lk_max_scanid_bit_upper,
   (void *) &prm_lk_max_scanid_bit_lower,
   (char *) NULL},
  {PRM_NAME_HOSTVAR_LATE_BINDING,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_hostvar_late_binding_default,
   (void *) &PRM_HOSTVAR_LATE_BINDING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ENABLE_HISTO,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_enable_histo_default,
   (void *) &PRM_ENABLE_HISTO,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_MUTEX_BUSY_WAITING_CNT,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_mutex_busy_waiting_cnt_default,
   (void *) &PRM_MUTEX_BUSY_WAITING_CNT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_PB_NUM_LRU_CHAINS,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_pb_num_LRU_chains_default,
   (void *) &PRM_PB_NUM_LRU_CHAINS,
   (void *) &prm_pb_num_LRU_chains_upper,
   (void *) &prm_pb_num_LRU_chains_lower,
   (char *) NULL},
  {PRM_NAME_PAGE_BG_FLUSH_INTERVAL_MSECS,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_page_bg_flush_interval_msec_default,
   (void *) &PRM_PAGE_BG_FLUSH_INTERVAL_MSEC,
   (void *) NULL, (void *) &prm_page_bg_flush_interval_msec_lower,
   (char *) NULL},
  {PRM_NAME_ADAPTIVE_FLUSH_CONTROL,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_adaptive_flush_control_default,
   (void *) &PRM_ADAPTIVE_FLUSH_CONTROL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL},
  {PRM_NAME_MAX_FLUSH_PAGES_PER_SECOND,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_max_flush_pages_per_second_default,
   (void *) &PRM_MAX_FLUSH_PAGES_PER_SECOND,
   (void *) &prm_max_flush_pages_per_second_upper,
   (void *) &prm_max_flush_pages_per_second_lower,
   (char *) NULL},
  {PRM_NAME_PB_SYNC_ON_NFLUSH,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_pb_sync_on_nflush_default,
   (void *) &PRM_PB_SYNC_ON_NFLUSH,
   (void *) &prm_pb_sync_on_nflush_upper,
   (void *) &prm_pb_sync_on_nflush_lower,
   (char *) NULL},
  {PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_KEYWORD,
   (void *) &prm_pb_debug_page_validation_level_default,
   (void *) &PRM_PB_DEBUG_PAGE_VALIDATION_LEVEL,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ORACLE_STYLE_OUTERJOIN,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_oracle_style_outerjoin_default,
   (void *) &PRM_ORACLE_STYLE_OUTERJOIN,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ANSI_QUOTES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_TEST_CHANGE | PRM_CLEAR_CACHE),
   PRM_BOOLEAN,
   (void *) &prm_ansi_quotes_default,
   (void *) &PRM_ANSI_QUOTES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_DEFAULT_WEEK_FORMAT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER
    | PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_INTEGER,
   (void *) &prm_week_format_default,
   (void *) &PRM_DEFAULT_WEEK_FORMAT,
   (void *) &prm_week_format_upper,
   (void *) &prm_week_format_lower,
   (char *) NULL},
  {PRM_NAME_TEST_MODE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER
    | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_test_mode_default,
   (void *) &PRM_TEST_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ONLY_FULL_GROUP_BY,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_only_full_group_by_default,
   (void *) &PRM_ONLY_FULL_GROUP_BY,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_PIPES_AS_CONCAT,
   (PRM_REQUIRED | PRM_DEFAULT |
    PRM_FOR_CLIENT | PRM_TEST_CHANGE | PRM_CLEAR_CACHE),
   PRM_BOOLEAN,
   (void *) &prm_pipes_as_concat_default,
   (void *) &PRM_PIPES_AS_CONCAT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_MYSQL_TRIGGER_CORRELATION_NAMES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_mysql_trigger_correlation_names_default,
   (void *) &PRM_MYSQL_TRIGGER_CORRELATION_NAMES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_REQUIRE_LIKE_ESCAPE_CHARACTER,
   (PRM_REQUIRED | PRM_DEFAULT |
    PRM_FOR_CLIENT | PRM_TEST_CHANGE | PRM_CLEAR_CACHE),
   PRM_BOOLEAN,
   (void *) &prm_require_like_escape_character_default,
   (void *) &PRM_REQUIRE_LIKE_ESCAPE_CHARACTER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_NO_BACKSLASH_ESCAPES,
   (PRM_REQUIRED | PRM_DEFAULT |
    PRM_FOR_CLIENT | PRM_TEST_CHANGE | PRM_CLEAR_CACHE),
   PRM_BOOLEAN,
   (void *) &prm_no_backslash_escapes_default,
   (void *) &PRM_NO_BACKSLASH_ESCAPES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_GROUP_CONCAT_MAX_LEN,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_group_concat_max_len_default,
   (void *) &PRM_GROUP_CONCAT_MAX_LEN,
   (void *) NULL, (void *) &prm_group_concat_max_len_lower,
   (char *) NULL},
  {PRM_NAME_STRING_MAX_SIZE_BYTES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_USER_CHANGE |
    PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_FOR_SESSION),
   PRM_INTEGER,
   (void *) &prm_string_max_size_bytes_default,
   (void *) &PRM_STRING_MAX_SIZE_BYTES,
   (void *) &prm_string_max_size_bytes_upper,
   (void *) &prm_string_max_size_bytes_lower,
   (char *) NULL},
  {PRM_NAME_ADD_COLUMN_UPDATE_HARD_DEFAULT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_USER_CHANGE |
    PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_add_column_update_hard_default_default,
   (void *) &PRM_ADD_COLUMN_UPDATE_HARD_DEFAULT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_RETURN_NULL_ON_FUNCTION_ERRORS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_CLEAR_CACHE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_return_null_on_function_errors_default,
   (void *) &PRM_RETURN_NULL_ON_FUNCTION_ERRORS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ALTER_TABLE_CHANGE_TYPE_STRICT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_USER_CHANGE | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_alter_table_change_type_strict_default,
   (void *) &PRM_ALTER_TABLE_CHANGE_TYPE_STRICT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_COMPACTDB_PAGE_RECLAIM_ONLY,
   (PRM_REQUIRED | PRM_DEFAULT),
   PRM_INTEGER,
   (void *) &prm_compactdb_page_reclaim_only_default,
   (void *) &PRM_COMPACTDB_PAGE_RECLAIM_ONLY,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_PLUS_AS_CONCAT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER |
    PRM_TEST_CHANGE | PRM_CLEAR_CACHE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_plus_as_concat_default,
   (void *) &PRM_PLUS_AS_CONCAT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LIKE_TERM_SELECTIVITY,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_FLOAT,
   (void *) &prm_like_term_selectivity_default,
   (void *) &PRM_LIKE_TERM_SELECTIVITY,
   (void *) &prm_like_term_selectivity_upper,
   (void *) &prm_like_term_selectivity_lower,
   (char *) NULL},
  {PRM_NAME_MAX_OUTER_CARD_OF_IDXJOIN,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_max_outer_card_of_idxjoin_default,
   (void *) &PRM_MAX_OUTER_CARD_OF_IDXJOIN,
   (void *) NULL,
   (void *) &prm_max_outer_card_of_idxjoin_lower,
   (char *) NULL},
  {PRM_NAME_ORACLE_STYLE_EMPTY_STRING,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_oracle_style_empty_string_default,
   (void *) &PRM_ORACLE_STYLE_EMPTY_STRING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SUPPRESS_FSYNC,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_suppress_fsync_default,
   (void *) &PRM_SUPPRESS_FSYNC,
   (void *) &prm_suppress_fsync_upper,
   (void *) &prm_suppress_fsync_lower,
   (char *) NULL},
  {PRM_NAME_CALL_STACK_DUMP_ON_ERROR,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_call_stack_dump_on_error_default,
   (void *) &PRM_CALL_STACK_DUMP_ON_ERROR,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_CALL_STACK_DUMP_ACTIVATION,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_ERROR_LIST,
   (void *) &prm_call_stack_dump_activation_default,
   (void *) &PRM_CALL_STACK_DUMP_ACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_CALL_STACK_DUMP_DEACTIVATION,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_ERROR_LIST,
   (void *) &prm_call_stack_dump_deactivation_default,
   (void *) &PRM_CALL_STACK_DUMP_DEACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_COMPAT_NUMERIC_DIVISION_SCALE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_FOR_SESSION),
   PRM_BOOLEAN,
   (void *) &prm_compat_numeric_division_scale_default,
   (void *) &PRM_COMPAT_NUMERIC_DIVISION_SCALE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_DBFILES_PROTECT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_dbfiles_protect_default,
   (void *) &PRM_DBFILES_PROTECT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_AUTO_RESTART_SERVER,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_auto_restart_server_default,
   (void *) &PRM_AUTO_RESTART_SERVER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_XASL_MAX_PLAN_CACHE_ENTRIES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_INTEGER,
   (void *) &prm_xasl_max_plan_cache_entries_default,
   (void *) &PRM_XASL_MAX_PLAN_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
#if defined (ENABLE_UNUSED_FUNCTION)
  {PRM_NAME_XASL_MAX_PLAN_CACHE_CLONES,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_xasl_max_plan_cache_clones_default,
   (void *) &PRM_XASL_MAX_PLAN_CACHE_CLONES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
#endif /* ENABLE_UNUSED_FUNCTION */
  {PRM_NAME_FILTER_PRED_MAX_CACHE_ENTRIES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_filter_pred_max_cache_entries_default,
   (void *) &PRM_FILTER_PRED_MAX_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_FILTER_PRED_MAX_CACHE_CLONES,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_filter_pred_max_cache_clones_default,
   (void *) &PRM_FILTER_PRED_MAX_CACHE_CLONES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_XASL_PLAN_CACHE_TIMEOUT,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_xasl_plan_cache_timeout_default,
   (void *) &PRM_XASL_PLAN_CACHE_TIMEOUT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LIST_QUERY_CACHE_MODE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_list_query_cache_mode_default,
   (void *) &PRM_LIST_QUERY_CACHE_MODE,
   (void *) &prm_list_query_cache_mode_upper,
   (void *) &prm_list_query_cache_mode_lower,
   (char *) NULL},
  {PRM_NAME_LIST_MAX_QUERY_CACHE_ENTRIES,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_list_max_query_cache_entries_default,
   (void *) &PRM_LIST_MAX_QUERY_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LIST_MAX_QUERY_CACHE_PAGES,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_INTEGER,
   (void *) &prm_list_max_query_cache_pages_default,
   (void *) &PRM_LIST_MAX_QUERY_CACHE_PAGES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_USE_ORDERBY_SORT_LIMIT,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_use_orderby_sort_limit_default,
   (void *) &PRM_USE_ORDERBY_SORT_LIMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_REPLICATION_MODE,
   (PRM_OBSOLETED),
   PRM_NO_TYPE,
   (void *) NULL,
   (void *) NULL,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_MODE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_HA | PRM_FORCE_SERVER),
   PRM_KEYWORD,
   (void *) &prm_ha_mode_default,
   (void *) &PRM_HA_MODE,
   (void *) &prm_ha_mode_upper,
   (void *) &prm_ha_mode_lower,
   (char *) NULL},
  {PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY,
   (PRM_DEFAULT),
   PRM_KEYWORD,
   (void *) &prm_ha_mode_default,
   (void *) &PRM_HA_MODE_FOR_SA_UTILS_ONLY,
   (void *) &prm_ha_mode_upper,
   (void *) &prm_ha_mode_lower,
   (char *) NULL},
  {PRM_NAME_HA_SERVER_STATE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN | PRM_FOR_HA),
   PRM_KEYWORD,
   (void *) &prm_ha_server_state_default,
   (void *) &PRM_HA_SERVER_STATE,
   (void *) &prm_ha_server_state_upper,
   (void *) &prm_ha_server_state_lower,
   (char *) NULL},
  {PRM_NAME_HA_LOG_APPLIER_STATE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN | PRM_FOR_HA),
   PRM_KEYWORD,
   (void *) &prm_ha_log_applier_state_default,
   (void *) &PRM_HA_LOG_APPLIER_STATE,
   (void *) &prm_ha_log_applier_state_upper,
   (void *) &prm_ha_log_applier_state_lower,
   (char *) NULL},
  {PRM_NAME_HA_NODE_LIST,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT |
    PRM_USER_CHANGE | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_node_list_default,
   (void *) &PRM_HA_NODE_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_REPLICA_LIST,
   (PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_replica_list_default,
   (void *) &PRM_HA_REPLICA_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_DB_LIST,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_db_list_default,
   (void *) &PRM_HA_DB_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_COPY_LOG_BASE,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_copy_log_base_default,
   (void *) &PRM_HA_COPY_LOG_BASE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_COPY_SYNC_MODE,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_copy_sync_mode_default,
   (void *) &PRM_HA_COPY_SYNC_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_APPLY_MAX_MEM_SIZE,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_apply_max_mem_size_default,
   (void *) &PRM_HA_APPLY_MAX_MEM_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_PORT_ID,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_port_id_default,
   (void *) &PRM_HA_PORT_ID,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_INIT_TIMER_IN_MSECS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_init_timer_im_msecs_default,
   (void *) &PRM_HA_INIT_TIMER_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_HEARTBEAT_INTERVAL_IN_MSECS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_heartbeat_interval_in_msecs_default,
   (void *) &PRM_HA_HEARTBEAT_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_CALC_SCORE_INTERVAL_IN_MSECS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_calc_score_interval_in_msecs_default,
   (void *) &PRM_HA_CALC_SCORE_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_FAILOVER_WAIT_TIME_IN_MSECS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_failover_wait_time_in_msecs_default,
   (void *) &PRM_HA_FAILOVER_WAIT_TIME_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_process_start_confirm_interval_in_msecs_default,
   (void *) &PRM_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_process_dereg_confirm_interval_in_msecs_default,
   (void *) &PRM_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_MAX_PROCESS_START_CONFIRM,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_max_process_start_confirm_default,
   (void *) &PRM_HA_MAX_PROCESS_START_CONFIRM,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_MAX_PROCESS_DEREG_CONFIRM,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_max_process_dereg_confirm_default,
   (void *) &PRM_HA_MAX_PROCESS_DEREG_CONFIRM,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_CHANGEMODE_INTERVAL_IN_MSEC,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_changemode_interval_in_msecs_default,
   (void *) &PRM_HA_CHANGEMODE_INTERVAL_IN_MSECS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_MAX_HEARTBEAT_GAP,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_HIDDEN | PRM_FOR_HA),
   PRM_INTEGER,
   (void *) &prm_ha_max_heartbeat_gap_default,
   (void *) &PRM_HA_MAX_HEARTBEAT_GAP,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_PING_HOSTS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_RELOADABLE | PRM_FOR_HA),
   PRM_STRING,
   (void *) &prm_ha_ping_hosts_default,
   (void *) &PRM_HA_PING_HOSTS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_APPLYLOGDB_RETRY_ERROR_LIST,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_ERROR_LIST,
   (void *) &prm_ha_applylogdb_retry_error_list_default,
   (void *) &PRM_HA_APPLYLOGDB_RETRY_ERROR_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA),
   PRM_ERROR_LIST,
   (void *) &prm_ha_applylogdb_ignore_error_list_default,
   (void *) &PRM_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_HA | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_ha_applylogdb_log_wait_time_in_secs_default,
   (void *) &PRM_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS,
   (void *) NULL, (void *) &prm_ha_applylogdb_log_wait_time_in_secs_lower,
   (char *) NULL},
  {PRM_NAME_JAVA_STORED_PROCEDURE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_java_stored_procedure_default,
   (void *) &PRM_JAVA_STORED_PROCEDURE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_COMPAT_PRIMARY_KEY,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_compat_primary_key_default,
   (void *) &PRM_COMPAT_PRIMARY_KEY,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LOG_HEADER_FLUSH_INTERVAL,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_log_header_flush_interval_default,
   (void *) &PRM_LOG_HEADER_FLUSH_INTERVAL,
   (void *) NULL, (void *) &prm_log_header_flush_interval_lower,
   (char *) NULL},
  {PRM_NAME_LOG_ASYNC_COMMIT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_log_async_commit_default,
   (void *) &PRM_LOG_ASYNC_COMMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LOG_GROUP_COMMIT_INTERVAL_MSECS,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_log_group_commit_interval_msecs_default,
   (void *) &PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS,
   (void *) NULL, (void *) &prm_log_group_commit_interval_msecs_lower,
   (char *) NULL},
  {PRM_NAME_LOG_BG_FLUSH_INTERVAL_MSECS,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_log_bg_flush_interval_msecs_default,
   (void *) &PRM_LOG_BG_FLUSH_INTERVAL_MSECS,
   (void *) NULL, (void *) &prm_log_bg_flush_interval_msecs_lower,
   (char *) NULL},
  {PRM_NAME_LOG_BG_FLUSH_NUM_PAGES,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_log_bg_flush_num_pages_default,
   (void *) &PRM_LOG_BG_FLUSH_NUM_PAGES,
   (void *) NULL, (void *) &prm_log_bg_flush_num_pages_lower,
   (char *) NULL},
  {PRM_NAME_INTL_MBS_SUPPORT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   (void *) &prm_intl_mbs_support_default,
   (void *) &PRM_INTL_MBS_SUPPORT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LOG_COMPRESS,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_log_compress_default,
   (void *) &PRM_LOG_COMPRESS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_BLOCK_NOWHERE_STATEMENT,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_block_nowhere_statement_default,
   (void *) &PRM_BLOCK_NOWHERE_STATEMENT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_BLOCK_DDL_STATEMENT,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_block_ddl_statement_default,
   (void *) &PRM_BLOCK_DDL_STATEMENT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
#if defined (ENABLE_UNUSED_FUNCTION)
  {PRM_NAME_SINGLE_BYTE_COMPARE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_FORCE_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_single_byte_compare_default,
   (void *) &PRM_SINGLE_BYTE_COMPARE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
#endif
  {PRM_NAME_CSQL_HISTORY_NUM,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_csql_history_num_default,
   (void *) &PRM_CSQL_HISTORY_NUM,
   (void *) &prm_csql_history_num_upper,
   (void *) &prm_csql_history_num_lower,
   (char *) NULL},
  {PRM_NAME_LOG_TRACE_DEBUG,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_log_trace_debug_default,
   (void *) &PRM_LOG_TRACE_DEBUG,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_DL_FORK,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_STRING,
   (void *) &prm_dl_fork_default,
   (void *) &PRM_DL_FORK,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ER_PRODUCTION_MODE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_er_production_mode_default,
   (void *) &PRM_ER_PRODUCTION_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_ER_STOP_ON_ERROR,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_er_stop_on_error_default,
   (void *) &PRM_ER_STOP_ON_ERROR,
   (void *) &prm_er_stop_on_error_upper, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_TCP_RCVBUF_SIZE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_tcp_rcvbuf_size_default,
   (void *) &PRM_TCP_RCVBUF_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_TCP_SNDBUF_SIZE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_tcp_sndbuf_size_default,
   (void *) &PRM_TCP_SNDBUF_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_TCP_NODELAY,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_tcp_nodelay_default,
   (void *) &PRM_TCP_NODELAY,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_CSQL_SINGLE_LINE_MODE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_csql_single_line_mode_default,
   (void *) &PRM_CSQL_SINGLE_LINE_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_XASL_DEBUG_DUMP,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_xasl_debug_dump_default,
   (void *) &PRM_XASL_DEBUG_DUMP,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_LOG_MAX_ARCHIVES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_INTEGER,
   (void *) &prm_log_max_archives_default,
   (void *) &PRM_LOG_MAX_ARCHIVES,
   (void *) NULL, (void *) &prm_log_max_archives_lower,
   (char *) NULL},
  {PRM_NAME_FORCE_REMOVE_LOG_ARCHIVES,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_force_remove_log_archives_default,
   (void *) &PRM_FORCE_REMOVE_LOG_ARCHIVES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_REMOVE_LOG_ARCHIVES_INTERVAL,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_remove_log_archives_interval_default,
   (void *) &PRM_REMOVE_LOG_ARCHIVES_INTERVAL,
   (void *) NULL, (void *) &prm_remove_log_archives_interval_lower,
   (char *) NULL},
  {PRM_NAME_LOG_NO_LOGGING,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_log_no_logging_default,
   (void *) &PRM_LOG_NO_LOGGING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_UNLOADDB_IGNORE_ERROR,
   (PRM_DEFAULT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_unloaddb_ignore_error_default,
   (void *) &PRM_UNLOADDB_IGNORE_ERROR,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_UNLOADDB_LOCK_TIMEOUT,
   (PRM_DEFAULT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_unloaddb_lock_timeout_default,
   (void *) &PRM_UNLOADDB_LOCK_TIMEOUT,
   (void *) NULL, (void *) &prm_unloaddb_lock_timeout_lower,
   (char *) NULL},
  {PRM_NAME_LOADDB_FLUSH_INTERVAL,
   (PRM_DEFAULT | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_loaddb_flush_interval_default,
   (void *) &PRM_LOADDB_FLUSH_INTERVAL,
   (void *) NULL, (void *) &prm_loaddb_flush_interval_lower,
   (char *) NULL},
  {PRM_NAME_IO_TEMP_VOLUME_PATH,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_io_temp_volume_path_default,
   (void *) &PRM_IO_TEMP_VOLUME_PATH,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_IO_VOLUME_EXT_PATH,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_io_volume_ext_path_default,
   (void *) &PRM_IO_VOLUME_EXT_PATH,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_UNIQUE_ERROR_KEY_VALUE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN |
    PRM_DEPRECATED),
   PRM_BOOLEAN,
   (void *) &prm_unique_error_key_value_default,
   (void *) &PRM_UNIQUE_ERROR_KEY_VALUE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_USE_SYSTEM_MALLOC,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_BOOLEAN,
   (void *) &prm_use_system_malloc_default,
   (void *) &PRM_USE_SYSTEM_MALLOC,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_EVENT_HANDLER,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_event_handler_default,
   (void *) &PRM_EVENT_HANDLER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_EVENT_ACTIVATION,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER),
   PRM_ERROR_LIST,
   (void *) &prm_event_activation_default,
   (void *) &PRM_EVENT_ACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_READ_ONLY_MODE,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT),
   PRM_BOOLEAN,
   (void *) &prm_read_only_mode_default,
   (void *) &PRM_READ_ONLY_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_MNT_WAITING_THREAD,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE | PRM_HIDDEN),
   PRM_INTEGER,
   (void *) &prm_mnt_waiting_thread_default,
   (void *) &PRM_MNT_WAITING_THREAD,
   (void *) NULL, (void *) &prm_mnt_waiting_thread_lower,
   (char *) NULL},
  {PRM_NAME_MNT_STATS_THRESHOLD,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_HIDDEN),
   PRM_INTEGER_LIST,
   (void *) &prm_mnt_stats_threshold_default,
   (void *) &PRM_MNT_STATS_THRESHOLD,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SERVICE_SERVICE_LIST,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_service_service_list_default,
   (void *) &PRM_SERVICE_SERVICE_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SERVICE_SERVER_LIST,
   (PRM_DEFAULT | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_service_server_list_default,
   (void *) &PRM_SERVICE_SERVER_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SESSION_STATE_TIMEOUT,
   (PRM_DEFAULT | PRM_FOR_SERVER | PRM_TEST_CHANGE),
   PRM_INTEGER,
   (void *) &prm_session_timeout_default,
   (void *) &PRM_SESSION_STATE_TIMEOUT,
   (void *) &prm_session_timeout_upper,
   (void *) &prm_session_timeout_lower,
   (char *) NULL},
  {PRM_NAME_MULTI_RANGE_OPT_LIMIT,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE | PRM_LOWER_LIMIT | PRM_UPPER_LIMIT | PRM_TEST_CHANGE),
   PRM_INTEGER,
   (void *) &prm_multi_range_opt_limit_default,
   (void *) &PRM_MULTI_RANGE_OPT_LIMIT,
   (void *) &prm_multi_range_opt_limit_upper,
   (void *) &prm_multi_range_opt_limit_lower,
   (char *) NULL},
  {PRM_NAME_INTL_NUMBER_LANG,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION |
    PRM_CLEAR_CACHE | PRM_TEST_CHANGE),
   PRM_STRING,
   (void *) &prm_intl_number_lang_default,
   (void *) &PRM_INTL_NUMBER_LANG,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_INTL_DATE_LANG,
   (PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE | PRM_FOR_SESSION |
    PRM_CLEAR_CACHE | PRM_TEST_CHANGE),
   PRM_STRING,
   (void *) &prm_intl_date_lang_default,
   (void *) &PRM_INTL_DATE_LANG,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  /* All the compound parameters *must* be at the end of the array so that the
     changes they cause are not overridden by other parameters (for example in
     sysprm_load_and_init the parameters are set to their default in the order
     they are found in this array). */
  {PRM_NAME_COMPAT_MODE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER
    | PRM_TEST_CHANGE | PRM_COMPOUND | PRM_CLEAR_CACHE),
   PRM_KEYWORD,
   (void *) &prm_compat_mode_default,
   (void *) &PRM_COMPAT_MODE,
   (void *) &prm_compat_mode_upper, (void *) &prm_compat_mode_lower,
   (char *) NULL},
  {PRM_NAME_DB_VOLUME_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_LOWER_LIMIT | PRM_UPPER_LIMIT),
   PRM_SIZE,
   (void *) &prm_db_volume_size_default,
   (void *) &PRM_DB_VOLUME_SIZE,
   (void *) &prm_db_volume_size_upper,
   (void *) &prm_db_volume_size_lower,
   (char *) NULL},
  {PRM_NAME_LOG_VOLUME_SIZE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_LOWER_LIMIT | PRM_UPPER_LIMIT),
   PRM_SIZE,
   (void *) &prm_log_volume_size_default,
   (void *) &PRM_LOG_VOLUME_SIZE,
   (void *) &prm_log_volume_size_upper,
   (void *) &prm_log_volume_size_lower,
   (char *) NULL},
  {PRM_NAME_UNICODE_INPUT_NORMALIZATION,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_unicode_input_normalization_default,
   (void *) &PRM_UNICODE_INPUT_NORMALIZATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_UNICODE_OUTPUT_NORMALIZATION,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_unicode_output_normalization_default,
   (void *) &PRM_UNICODE_OUTPUT_NORMALIZATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_INTL_CHECK_INPUT_STRING,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_TEST_CHANGE),
   PRM_BOOLEAN,
   (void *) &prm_intl_check_input_string_default,
   (void *) &PRM_INTL_CHECK_INPUT_STRING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_CHECK_PEER_ALIVE,
   (PRM_REQUIRED | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER
    | PRM_USER_CHANGE),
   PRM_KEYWORD,
   (void *) &prm_check_peer_alive_default,
   (void *) &PRM_CHECK_PEER_ALIVE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SQL_TRACE_SLOW_MSECS,
   (PRM_DEFAULT | PRM_LOWER_LIMIT | PRM_USER_CHANGE |
    PRM_FOR_SERVER | PRM_UPPER_LIMIT),
   PRM_INTEGER,
   (void *) &prm_sql_trace_slow_msecs_default,
   (void *) &PRM_SQL_TRACE_SLOW_MSECS,
   (void *) &prm_sql_trace_slow_msecs_upper,
   (void *) &prm_sql_trace_slow_msecs_lower,
   (char *) NULL},
  {PRM_NAME_SQL_TRACE_EXECUTION_PLAN,
   (PRM_DEFAULT | PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_BOOLEAN,
   (void *) &prm_sql_trace_execution_plan_default,
   (void *) &PRM_SQL_TRACE_EXECUTION_PLAN,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL},
  {PRM_NAME_SQL_TRACE_TEMP_PATH,
   (PRM_DEFAULT | PRM_USER_CHANGE | PRM_FOR_SERVER),
   PRM_STRING,
   (void *) &prm_sql_trace_temp_path_default,
   (void *) &PRM_SQL_TRACE_TEMP_PATH,
   (void *) NULL,
   (void *) NULL,
   (char *) NULL},
};

#define NUM_PRM ((int)(sizeof(prm_Def)/sizeof(prm_Def[0])))
#define PARAM_MSG_FMT(msgid) msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, (msgid))

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

  {"tran_rep_class_rep_instance", TRAN_REP_CLASS_REP_INSTANCE},
  {"tran_rep_read", TRAN_REP_CLASS_REP_INSTANCE},
  {"tran_rep_class_commit_instance", TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"tran_read_committed", TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"tran_cursor_stability", TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"tran_rep_class_uncommit_instance", TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  /*
   * This silly spelling has to hang around because it was in there
   * once upon a time and users may have come to depend on it.
   */
  {"tran_read_uncommited", TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"tran_read_uncommitted", TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"tran_commit_class_commit_instance", TRAN_COMMIT_CLASS_COMMIT_INSTANCE},
  {"tran_commit_class_uncommit_instance",
   TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE},

  /*
   * Why be so fascict about the "tran_" prefix?  Are we afraid someone
   * is going to use these gonzo words?
   */
  {"serializable", TRAN_SERIALIZABLE},
  {"no_phantom_read", TRAN_SERIALIZABLE},

  {"rep_class_rep_instance", TRAN_REP_CLASS_REP_INSTANCE},
  {"rep_read", TRAN_REP_CLASS_REP_INSTANCE},
  {"rep_class_commit_instance", TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"read_committed", TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"cursor_stability", TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"rep_class_uncommit_instance", TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"read_uncommited", TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"commit_class_commit_instance", TRAN_COMMIT_CLASS_COMMIT_INSTANCE},
  {"commit_class_uncommit_instance", TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE}
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
  ER_DESC_ISCAN_ABORTED
};

typedef enum
{
  PRM_PRINT_NONE = 0,
  PRM_PRINT_NAME,
  PRM_PRINT_ID
} PRM_PRINT_MODE;

static void prm_the_file_has_been_loaded (const char *path);
static int prm_print_value (const SYSPRM_PARAM * prm, char *buf, size_t len);
static int prm_print (const SYSPRM_PARAM * prm, char *buf, size_t len,
		      PRM_PRINT_MODE print_mode);
static int prm_print_session_prm (const SESSION_PARAM * sprm, char *buf,
				  size_t len, PRM_PRINT_MODE print_mode);
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
static SESSION_PARAM *prm_get_session_prm_from_list (SESSION_PARAM *
						     session_params,
						     PARAM_ID prm_id);
static SESSION_PARAM *sysprm_obtain_session_parameters (void);
static int prm_get_id (const SYSPRM_PARAM * prm);
static int prm_compare_prm_value_with_value (PRM_VALUE prm_value, void *value,
					     unsigned int val_type);
#if !defined SERVER_MODE
static void prm_init_intl_param (void);
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
      if (PRM_IS_HIDDEN (prm->flag) || PRM_IS_OBSOLETED (prm->flag))
	{
	  continue;
	}
      prm_print (prm, buf, LINE_MAX, PRM_PRINT_NAME);
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
  if (er_log_file == NULL || PRM_IS_SET (er_log_file->flag))
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
#if defined (SERVER_MODE) && !defined (WINDOWS)
  log_tm_p = localtime_r (&log_time, &log_tm);
#else
  log_tm_p = localtime (&log_time);
#endif /* SERVER_MODE && !WINDOWS */
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
  char *base_db_name;
  char file_being_dealt_with[PATH_MAX];
  char local_db_name[DB_MAX_IDENTIFIER_LENGTH];
  unsigned int i;
  struct stat stat_buf;
  int r = NO_ERROR;
  char *s;

  if (reload)
    {
      for (i = 0; i < NUM_PRM; i++)
	{
	  if (PRM_IS_RELOADABLE (prm_Def[i].flag))
	    {
	      if (PRM_HAS_DEFAULT (prm_Def[i].flag))
		{
		  (void) prm_set_default (&prm_Def[i]);
		}
	      else
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

#if !defined (SERVER_MODE)
  prm_init_intl_param ();
#endif

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

#if !defined (SERVER_MODE)
  /*
   * Read $PWD/cubrid.conf if exist; not for server
   */
  if (conf_file == NULL)
    {
      snprintf (file_being_dealt_with, sizeof (file_being_dealt_with) - 1,
		"%s", sysprm_conf_file_name);
      if (stat (file_being_dealt_with, &stat_buf) == 0)
	{
	  r = prm_read_and_parse_ini_file (file_being_dealt_with,
					   base_db_name, reload, HA_IGNORE,
					   check_intl_param);
	}
    }
#endif /* !SERVER_MODE */

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
      if (!PRM_IS_SET (prm_Def[i].flag)
	  && !PRM_IS_OBSOLETED (prm_Def[i].flag))
	{
	  if (PRM_HAS_DEFAULT (prm_Def[i].flag))
	    {
	      (void) prm_set_default (&prm_Def[i]);
	    }
	  else
	    {
	      fprintf (stderr,
		       msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARAMETERS,
				       PRM_ERR_NO_VALUE), prm_Def[i].name);
	    }
	}
    }

  /*
   * Perform system parameter check and tuning.
   */
  prm_check_environment ();
  prm_tune_parameters ();
  if (prm_adjust_parameters () != NO_ERROR)
    {
      return ER_FAILED;
    }
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
      if (reload && (prm && !PRM_IS_RELOADABLE (prm->flag)))
	{
	  continue;
	}
      if (ha == HA_READ && (prm && !PRM_IS_FOR_HA (prm->flag)))
	{
	  continue;
	}

      if (prm && PRM_IS_OBSOLETED (prm->flag))
	{
	  continue;
	}

      if (prm && PRM_IS_DEPRECATED (prm->flag))
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
 * prm_calc_size_by_pages -
 *   return: error code
 */
static int
prm_calc_pages_by_size (const char *size, const char *page, PGLENGTH len)
{
  UINT64 size_value;
  int page_value;
  SYSPRM_PARAM *size_prm, *page_prm;
  char newval[LINE_MAX];

  size_prm = prm_find (size, NULL);
  page_prm = prm_find (page, NULL);
  if (size_prm == NULL || page_prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (PRM_DEFAULT_VAL_USED (size_prm->flag) && PRM_IS_SET (page_prm->flag))
    {
      return NO_ERROR;
    }

  if (PRM_IS_SET (size_prm->flag))
    {
      size_value = PRM_GET_SIZE (size_prm->value);

      if (page_prm->datatype == PRM_FLOAT)
	{
	  snprintf (newval, LINE_MAX, "%.2f", (float) size_value / len);
	}
      else
	{
	  assert (page_prm->datatype == PRM_INTEGER);

	  page_value = (int) (size_value / len);
	  snprintf (newval, LINE_MAX, "%d", page_value);
	}

      if (prm_set (page_prm, newval, false) != NO_ERROR)
	{
	  return PRM_ERR_BAD_VALUE;
	}
    }
  return NO_ERROR;
}

/*
 * prm_calc_size_by_pages -
 *   return: error code
 */
static int
prm_calc_size_by_pages (const char *page, const char *size, PGLENGTH len)
{
  UINT64 size_value;
  int int_page_value;
  float float_page_value;
  SYSPRM_PARAM *size_prm, *page_prm;
  char newval[LINE_MAX];

  size_prm = prm_find (size, NULL);
  page_prm = prm_find (page, NULL);
  if (size_prm == NULL || page_prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (PRM_IS_SET (size_prm->flag))
    {
      return NO_ERROR;
    }

  if (PRM_DEFAULT_VAL_USED (size_prm->flag) && PRM_IS_SET (page_prm->flag))
    {
      if (PRM_IS_INTEGER (page_prm))
	{
	  int_page_value = PRM_GET_INT (page_prm->value);
	  size_value = ((UINT64) int_page_value) * ((UINT64) len);
	}
      else if (PRM_IS_FLOAT (page_prm))
	{
	  float_page_value = PRM_GET_FLOAT (page_prm->value);
	  size_value = (UINT64) ((UINT64) len * float_page_value);
	}
      else
	{
	  assert (false);
	  return PRM_ERR_BAD_VALUE;
	}

      /* backward compatibility for old cubrid.conf */
      size_value = MAX (PRM_GET_SIZE (size_prm->lower_limit), size_value);

      snprintf (newval, LINE_MAX, "%llu", (unsigned long long) size_value);
      if (prm_set (size_prm, newval, false) != NO_ERROR)
	{
	  return PRM_ERR_BAD_VALUE;
	}
    }
  return NO_ERROR;
}

/*
 * prm_is_set_both -
 *   return: error code
 */
static int
prm_is_set_both (const char *one, const char *other)
{
  SYSPRM_PARAM *one_prm, *other_prm;

  one_prm = prm_find (one, NULL);
  other_prm = prm_find (other, NULL);
  if (one_prm == NULL || other_prm == NULL)
    {
      return PRM_ERR_NO_ERROR;
    }

  if (PRM_IS_SET (one_prm->flag) && PRM_IS_SET (other_prm->flag))
    {
      return PRM_ERR_NOT_BOTH;
    }

  return PRM_ERR_NO_ERROR;
}

/*
 * prm_adjust_parameters -
 *   return: error code
 */
int
prm_adjust_parameters ()
{
  const char *size[] = {
    PRM_NAME_PAGE_BUFFER_SIZE,
    PRM_NAME_SORT_BUFFER_SIZE,
    PRM_NAME_LOG_BUFFER_SIZE,
    PRM_NAME_INDEX_SCAN_KEY_BUFFER_SIZE,
    PRM_NAME_BT_OID_BUFFER_SIZE
  };
  const char *page[] = {
    PRM_NAME_PB_NBUFFERS,
    PRM_NAME_SR_NBUFFERS,
    PRM_NAME_LOG_NBUFFERS,
    PRM_NAME_INDEX_SCAN_KEY_BUFFER_PAGES,
    PRM_NAME_BT_OID_NBUFFERS
  };
  PGLENGTH len[] = {
    IO_PAGESIZE,
    IO_PAGESIZE,
    LOG_PAGESIZE,
    IO_PAGESIZE,
    IO_PAGESIZE,
  };
  int error;
  unsigned int i;

  for (i = 0; i < sizeof (size) / sizeof (size[0]); i++)
    {
      if (prm_is_set_both (size[i], page[i]) != PRM_ERR_NO_ERROR)
	{
	  char *fmt = PARAM_MSG_FMT (PRM_ERR_NOT_BOTH);
	  fprintf (stderr, fmt, size[i], page[i]);
	  return PRM_ERR_NOT_BOTH;
	}
    }

  for (i = 0; i < sizeof (size) / sizeof (size[0]); i++)
    {
      error = prm_calc_pages_by_size (size[i], page[i], len[i]);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = prm_calc_size_by_pages (page[i], size[i], len[i]);
      if (error != NO_ERROR)
	{
	  return error;
	}
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
   * ha_node_list and ha_db_list shold be not null for ha_mode=yes|replica
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


/*
 * prm_change - Set the values of parameters
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   data(in): parameter setting line, e.g. "name=value"
 *   check(in): check PRM_USER_CAN_CHANGE, PRM_IS_FOR_CLIENT
 *
 * Note: If SERVER_MODE is defined, session parameters will change values
 *	 stored in thread_entry->conn_entry and in session_state.
 */
static int
prm_change (const char *data, bool check)
{
  char buf[PRM_DEFAULT_BUFFER_SIZE], *p = NULL, *name = NULL, *value = NULL;
  SYSPRM_PARAM *prm;
  int err = PRM_ERR_NO_ERROR;

  if (!data || *data == '\0')
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (intl_mbs_ncpy (buf, data, PRM_DEFAULT_BUFFER_SIZE) == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  p = buf;
  do
    {
      err = prm_get_next_param_value (&p, &name, &value);
      if (err != PRM_ERR_NO_ERROR || name == NULL || value == NULL)
	{
	  break;
	}

      prm = prm_find (name, NULL);
      if (prm == NULL)
	{
	  return PRM_ERR_UNKNOWN_PARAM;
	}

      if (!check
	  || PRM_USER_CAN_CHANGE (prm->flag)
	  || (PRM_TEST_CHANGE_ONLY (prm->flag) && PRM_TEST_MODE))
	{
	  /* We allow changing the parameter value. */
	}
      else
	{
	  return PRM_ERR_CANNOT_CHANGE;
	}
      if (check)
	{
	  if (strcmp (prm->name, PRM_NAME_INTL_NUMBER_LANG) == 0
	      || strcmp (prm->name, PRM_NAME_INTL_DATE_LANG) == 0)
	    {
	      INTL_LANG dummy;

	      if (lang_get_lang_id_from_name (value, &dummy) != 0)
		{
		  return PRM_ERR_BAD_VALUE;
		}
	    }
	}
#if defined (SA_MODE)
      err = prm_set (prm, value, true);
#endif /* SA_MODE */
#if defined(CS_MODE)
      if (check == true)
	{
	  if (PRM_IS_FOR_CLIENT (prm->flag))
	    {
	      err = prm_set (prm, value, true);
	      if (PRM_IS_FOR_SERVER (prm->flag))
		{
		  /* for both client and server, return not for client after set it */
		  return PRM_ERR_NOT_FOR_CLIENT;
		}
	    }
	  else
	    {
	      if (PRM_IS_FOR_SERVER (prm->flag))
		{
		  /* for server only, return not for client */
		  return PRM_ERR_NOT_FOR_CLIENT;
		}
	      else
		{
		  /* not for both client and server, return error */
		  return PRM_ERR_CANNOT_CHANGE;
		}
	    }
	}
      else
	{
	  err = prm_set (prm, value, true);
	}
#endif /* CS_MODE */
#if defined (SERVER_MODE)
      if (!PRM_IS_FOR_SERVER (prm->flag))
	{
	  return PRM_ERR_NOT_FOR_SERVER;
	}
      err = prm_set (prm, value, true);
#endif /* SERVER_MODE */
    }
  while (err == PRM_ERR_NO_ERROR && p);

  return err;
}

/*
 * sysprm_change_parameters - Set the values of parameters
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   data(in): parameter setting line, e.g. "name=value"
 *
 * Note: Mutiple parameters can be changed at a time by providing a string
 *       line which is like "param1=value1; param2=value2; ...".
 */
int
sysprm_change_parameters (const char *data)
{
  return prm_change (data, true);
}

/*
 * sysprm_prm_change_should_invalidate_cache - check if changing a parameter
 *			value should cause the xasl cache to be cleared
 *   return: true if the cache should be cleared
 *   data(in): parameter setting line: "name1=value1; name2 = value2;..."
 */
bool
sysprm_prm_change_should_clear_cache (const char *data)
{
  char buf[PRM_DEFAULT_BUFFER_SIZE], *p = NULL, *name = NULL, *value = NULL;
  SYSPRM_PARAM *prm;
  int err = PRM_ERR_NO_ERROR;

  if (PRM_XASL_MAX_PLAN_CACHE_ENTRIES <= 0)
    {
      /* if cache is disabled, there's no need to clear it */
      return false;
    }
  if (!data || *data == '\0')
    {
      return false;
    }
  if (intl_mbs_ncpy (buf, data, PRM_DEFAULT_BUFFER_SIZE) == NULL)
    {
      return false;
    }

  p = buf;
  do
    {
      err = prm_get_next_param_value (&p, &name, &value);
      if (err != PRM_ERR_NO_ERROR || name == NULL || value == NULL)
	{
	  break;
	}

      prm = prm_find (name, NULL);
      if (prm == NULL)
	{
	  return false;
	}
      if (PRM_SHOULD_CLEAR_CACHE (prm->flag))
	{
	  return true;
	}
    }
  while (err == PRM_ERR_NO_ERROR && p);

  return false;
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
	   PRM_PRINT_MODE print_mode)
{
  int n = 0, id = -1;
  char left_side[PRM_DEFAULT_BUFFER_SIZE];

  memset (left_side, 0, PRM_DEFAULT_BUFFER_SIZE);

  assert (prm != NULL && buf != NULL && len > 0);

  if (print_mode == PRM_PRINT_ID)
    {
      id = prm_get_id (prm);
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%d=", id);
    }
  else if (print_mode == PRM_PRINT_NAME)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%s=", prm->name);
    }

  if (PRM_IS_INTEGER (prm))
    {
      n = snprintf (buf, len, "%s%d", left_side, PRM_GET_INT (prm->value));
    }
  else if (PRM_IS_SIZE (prm))
    {
      UINT64 v = PRM_GET_SIZE (prm->value);
      n = snprintf (buf, len, "%s", left_side);
      n += util_byte_to_size_string (v, buf + n, len - n);
    }
  else if (PRM_IS_BOOLEAN (prm))
    {
      n = snprintf (buf, len, "%s%c", left_side,
		    (PRM_GET_BOOL (prm->value) ? 'y' : 'n'));
    }
  else if (PRM_IS_FLOAT (prm))
    {
      n = snprintf (buf, len, "%s%f", prm->name, PRM_GET_FLOAT (prm->value));
    }
  else if (PRM_IS_STRING (prm))
    {
      n = snprintf (buf, len, "%s\"%s\"", left_side,
		    (PRM_GET_STRING (prm->value) ?
		     PRM_GET_STRING (prm->value) : ""));
    }
  else if (PRM_IS_KEYWORD (prm))
    {
      const KEYVAL *keyvalp = NULL;

      if (intl_mbs_casecmp (prm->name, PRM_NAME_ER_LOG_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL, er_log_level_words,
				 DIM (er_log_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_LOG_ISOLATION_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL, isolation_level_words,
				 DIM (isolation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL) ==
	       0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL,
				 pgbuf_debug_page_validation_level_words,
				 DIM
				 (pgbuf_debug_page_validation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_MODE) == 0
	       || intl_mbs_casecmp (prm->name,
				    PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY) == 0)

	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL, ha_mode_words, DIM (ha_mode_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_SERVER_STATE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL, ha_server_state_words,
				 DIM (ha_server_state_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_HA_LOG_APPLIER_STATE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL, ha_log_applier_state_words,
				 DIM (ha_log_applier_state_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_COMPAT_MODE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL, compat_words, DIM (compat_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_CHECK_PEER_ALIVE) == 0)
	{
	  keyvalp = prm_keyword (PRM_GET_INT (prm->value),
				 NULL, check_peer_alive_words,
				 DIM (check_peer_alive_words));
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
	  n = snprintf (buf, len, "%s%d", left_side,
			PRM_GET_INT (prm->value));
	}
    }
  else if (PRM_IS_ERROR_LIST (prm))
    {
      bool *error_list;
      int err_id;
      char *s;

      error_list = PRM_GET_ERROR_LIST (prm->value);
      if (error_list)
	{
	  bool is_empty_list = true;

	  s = buf;
	  n = snprintf (s, len, "%s", left_side);
	  s += n;
	  len -= n;

	  for (err_id = 1; err_id <= -(ER_LAST_ERROR); err_id++)
	    {
	      if (error_list[err_id])
		{
		  n = snprintf (s, len, "%d,", -err_id);
		  s += n;
		  len -= n;
		  is_empty_list = false;
		}
	    }

	  if (is_empty_list == false)
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
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      int *int_list, list_size, i;
      char *s;

      int_list = PRM_GET_INTEGER_LIST (prm->value);
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
 * prm_print_session_prm - Print a session parameter to the buffer
 *   return: number of chars printed
 *   sprm(in): session parameter
 *   buf(out): print buffer
 *   len(in): length of the buffer
 *   print_mode(in): print name/id or just value of the parameter
 */
static int
prm_print_session_prm (const SESSION_PARAM * sprm, char *buf, size_t len,
		       PRM_PRINT_MODE print_mode)
{
  int n = 0, id = -1;
  char left_side[PRM_DEFAULT_BUFFER_SIZE];
  SYSPRM_PARAM *prm;

  memset (left_side, 0, PRM_DEFAULT_BUFFER_SIZE);

  assert (sprm != NULL && buf != NULL && len > 0);

  id = sprm->prm_id;
  prm = &prm_Def[id];

  if (print_mode == PRM_PRINT_ID)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%d=", id);
    }
  else if (print_mode == PRM_PRINT_NAME)
    {
      snprintf (left_side, PRM_DEFAULT_BUFFER_SIZE, "%s=", prm->name);
    }

  if (PRM_IS_INTEGER (prm))
    {
      n = snprintf (buf, len, "%s%d", left_side, sprm->prm_value.i);
    }
  else if (PRM_IS_SIZE (prm))
    {
      UINT64 v = sprm->prm_value.size;
      n = snprintf (buf, len, "%s", left_side);
      n += util_byte_to_size_string (v, buf + n, len - n);
    }
  else if (PRM_IS_BOOLEAN (prm))
    {
      n = snprintf (buf, len, "%s%c", left_side,
		    (sprm->prm_value.b ? 'y' : 'n'));
    }
  else if (PRM_IS_FLOAT (prm))
    {
      n = snprintf (buf, len, "%s%f", prm->name, sprm->prm_value.f);
    }
  else if (PRM_IS_STRING (prm))
    {
      n = snprintf (buf, len, "%s\"%s\"", left_side,
		    (sprm->prm_value.str ? sprm->prm_value.str : ""));
    }
  else if (PRM_IS_KEYWORD (prm))
    {
      const KEYVAL *keyvalp = NULL;

      if (intl_mbs_casecmp (prm->name, PRM_NAME_ER_LOG_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL, er_log_level_words,
				 DIM (er_log_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_LOG_ISOLATION_LEVEL) == 0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL,
				 isolation_level_words,
				 DIM (isolation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_PB_DEBUG_PAGE_VALIDATION_LEVEL) ==
	       0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL,
				 pgbuf_debug_page_validation_level_words,
				 DIM
				 (pgbuf_debug_page_validation_level_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_MODE) == 0
	       || intl_mbs_casecmp (prm->name,
				    PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY) == 0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL, ha_mode_words,
				 DIM (ha_mode_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_HA_SERVER_STATE) == 0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL,
				 ha_server_state_words,
				 DIM (ha_server_state_words));
	}
      else if (intl_mbs_casecmp (prm->name,
				 PRM_NAME_HA_LOG_APPLIER_STATE) == 0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL,
				 ha_log_applier_state_words,
				 DIM (ha_log_applier_state_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_COMPAT_MODE) == 0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL, compat_words,
				 DIM (compat_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_CHECK_PEER_ALIVE) == 0)
	{
	  keyvalp = prm_keyword (sprm->prm_value.i, NULL,
				 check_peer_alive_words,
				 DIM (check_peer_alive_words));
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
	  n = snprintf (buf, len, "%s%d", left_side, sprm->prm_value.i);
	}
    }
  else if (PRM_IS_ERROR_LIST (prm))
    {
      bool *error_list;
      int err_id;
      char *s;

      error_list = sprm->prm_value.error_list;
      if (error_list)
	{
	  bool is_empty_list = true;

	  s = buf;
	  n = snprintf (s, len, "%s", left_side);
	  s += n;
	  len -= n;

	  for (err_id = 1; err_id <= -(ER_LAST_ERROR); err_id++)
	    {
	      if (error_list[err_id])
		{
		  n = snprintf (s, len, "%d,", -err_id);
		  s += n;
		  len -= n;
		  is_empty_list = false;
		}
	    }

	  if (is_empty_list == false)
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
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      int *int_list, list_size, i;
      char *s;

      int_list = sprm->prm_value.integer_list;
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
 * sysprm_obtain_parameters - Get parameters as the form of setting line,
 *                         e.g. "name=value"
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   data(in/out): names of parameters to get
 *                 and as buffer where the output to be stored
 *   len(in): size of the data buffer
 *
 * Note: Mutiple parameters can be ontained at a time by providing a string
 *       line which is like "param1; param2; ...".
 */
int
sysprm_obtain_parameters (char *data, int len)
{
  char buf[LINE_MAX], *p, *name, *t;
  int n;
  SYSPRM_PARAM *prm;

  if (!data || *data == '\0' || len <= 0)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (intl_mbs_ncpy (buf, data, LINE_MAX) == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

  *(t = data) = '\0';
  p = buf;
  do
    {
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
	  return PRM_ERR_UNKNOWN_PARAM;
	}

      if (t != data)
	{
	  *t++ = ';';
	  len--;
	}

#ifdef SA_MODE
      n = prm_print (prm, t, len, PRM_PRINT_NAME);
#endif
#if defined(CS_MODE) || defined(CS_MODE_MT)
      if (PRM_IS_FOR_CLIENT (prm->flag))
	{
	  n = prm_print (prm, t, len, PRM_PRINT_NAME);
	  if (PRM_IS_FOR_SERVER (prm->flag))
	    {
	      /* for both client and server, return not for client after set it */
	      return PRM_ERR_NOT_FOR_CLIENT;
	    }
	}
      else
	{
	  if (PRM_IS_FOR_SERVER (prm->flag))
	    {
	      /* for server only, return not for client */
	      return PRM_ERR_NOT_FOR_CLIENT;
	    }
	  else
	    {
	      /* not for both client and server, return error */
	      return PRM_ERR_CANNOT_CHANGE;
	    }
	}
#endif
#if defined (SERVER_MODE)
      if (!PRM_IS_FOR_SERVER (prm->flag))
	{
	  return PRM_ERR_NOT_FOR_SERVER;
	}
      if (PRM_IS_FOR_SESSION (prm->flag) && BO_IS_SERVER_RESTARTED ())
	{
	  THREAD_ENTRY *thread_p;
	  CSS_CONN_ENTRY *conn;
	  bool found = false;

	  thread_p = thread_get_thread_entry_info ();
	  if (thread_p)
	    {
	      conn = thread_p->conn_entry;
	      if (conn && conn->session_params)
		{
		  int id = prm_get_id (prm);
		  SESSION_PARAM *sprm =
		    prm_get_session_prm_from_list (conn->session_params, id);

		  if (sprm == NULL)
		    {
		      return PRM_ERR_UNKNOWN_PARAM;
		    }
		  n = prm_print_session_prm (sprm, t, len, PRM_PRINT_NAME);
		  found = true;
		}
	    }
	  if (!found)
	    {
	      return PRM_ERR_UNKNOWN_PARAM;
	    }
	}
      else
	{
	  n = prm_print (prm, t, len, PRM_PRINT_NAME);
	}
#endif
      len -= n;
      t += n;
    }
  while (*p && len > 0);

  return NO_ERROR;
}

/*
 * sysprm_obtain_session_parameters () - obtain a list of session parameters
 *
 * return: list of session parameters
 *
 * NOTE:
 *  If on client, the list is obtained by finding all session parameters in
 *  prm_Def array of system parameters. On server, first it looks in
 *  conn_entry and in session_state. If no list is found, then it builds the
 *  list from prm_Def array.
 */
SESSION_PARAM *
sysprm_obtain_session_parameters (void)
{
  int i;
  SESSION_PARAM *session_params = NULL;
#if defined (SERVER_MODE)
  if (BO_IS_SERVER_RESTARTED ())
    {
      THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
      int error;

      if (thread_p)
	{
	  CSS_CONN_ENTRY *conn_p = thread_p->conn_entry;

	  if (conn_p && conn_p->session_params)
	    {
	      return sysprm_duplicate_session_parameters (conn_p->
							  session_params);
	    }
	  error = session_get_session_parameters (thread_p, &session_params);
	  if (error != NO_ERROR)
	    {
	      sysprm_free_session_parameters (&session_params);
	      return NULL;
	    }
	  if (session_params)
	    {
	      return session_params;
	    }
	}
    }
#endif /* SERVER_MODE */

  for (i = 0; i < NUM_PRM; i++)
    {
      SYSPRM_PARAM prm = prm_Def[i];

      if (PRM_IS_FOR_SESSION (prm.flag))
	{
	  SESSION_PARAM *session_prm =
	    (SESSION_PARAM *) malloc (sizeof (SESSION_PARAM));

	  if (session_prm == NULL)
	    {
	      sysprm_free_session_parameters (&session_params);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SESSION_PARAM));
	      return NULL;
	    }

	  if (session_params)
	    {
	      session_prm->next = session_params;
	      session_params = session_prm;
	    }
	  else
	    {
	      session_params = session_prm;
	    }
	}
    }

  return session_params;
}

#if !defined(CS_MODE)
/*
 * xsysprm_change_server_parameters -
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   arg1(in):
 *   arg2(in):
 */
int
xsysprm_change_server_parameters (const char *data)
{
  return sysprm_change_parameters (data);
}

/*
 * xsysprm_change_server_parameters -
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   data(data):
 *   len(in):
 */
int
xsysprm_obtain_server_parameters (char *data, int len)
{
  return sysprm_obtain_parameters (data, len);
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

static int
prm_make_size (UINT64 * pre, char post)
{
  switch (post)
    {
    case 'b':
    case 'B':
      /* bytes */
      break;
    case 'k':
    case 'K':
      /* kilo */
      *pre = *pre * 1024ULL;
      break;
    case 'm':
    case 'M':
      /* mega */
      *pre = *pre * 1048576ULL;
      break;
    case 'g':
    case 'G':
      /* giga */
      *pre = *pre * 1073741824ULL;
      break;
    case 't':
    case 'T':
      /* tera */
      *pre = *pre * 1099511627776ULL;
      break;
    default:
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * sysprm_get_range -
 *   return:
 *   pname (in): parameter name
 *   value (in): parameter value
 */
int
sysprm_get_range (const char *pname, void *min, void *max)
{
  SYSPRM_PARAM *prm;

  prm = prm_find (pname, NULL);
  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (PRM_IS_INTEGER (prm))
    {
      if (prm->lower_limit)
	{
	  *((int *) min) = PRM_GET_INT (prm->lower_limit);
	}
      else
	{
	  *((int *) min) = INT_MIN;
	}

      if (prm->upper_limit)
	{
	  *((int *) max) = PRM_GET_INT (prm->upper_limit);
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
	}
      else
	{
	  *((float *) min) = FLT_MIN;
	}

      if (prm->upper_limit)
	{
	  *((float *) max) = PRM_GET_FLOAT (prm->upper_limit);
	}
      else
	{
	  *((float *) max) = FLT_MAX;
	}
    }
  else if (PRM_IS_SIZE (prm))
    {
      if (prm->lower_limit)
	{
	  *((UINT64 *) min) = PRM_GET_SIZE (prm->lower_limit);
	}
      else
	{
	  *((UINT64 *) min) = 0ULL;
	}

      if (prm->upper_limit)
	{
	  *((UINT64 *) max) = PRM_GET_SIZE (prm->upper_limit);
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
  SYSPRM_PARAM *prm;

  prm = prm_find (pname, NULL);
  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (PRM_IS_INTEGER (prm))
    {
      int v = *((int *) value);

      if ((prm->upper_limit && PRM_GET_INT (prm->upper_limit) < v)
	  || (prm->lower_limit && PRM_GET_INT (prm->lower_limit) > v))
	{
	  return PRM_ERR_BAD_RANGE;
	}
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float v = *((float *) value);

      if ((prm->upper_limit && PRM_GET_FLOAT (prm->upper_limit) < v)
	  || (prm->lower_limit && PRM_GET_FLOAT (prm->lower_limit) > v))
	{
	  return PRM_ERR_BAD_RANGE;
	}
    }
  else if (PRM_IS_SIZE (prm))
    {
      UINT64 v = *((UINT64 *) value);

      if ((prm->upper_limit && PRM_GET_SIZE (prm->upper_limit) < v)
	  || (prm->lower_limit && PRM_GET_SIZE (prm->lower_limit) > v))
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
 * prm_set - Set the value of a parameter
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   prm(in):
 *   value(in):
 */
static int
prm_set (SYSPRM_PARAM * prm, const char *value, bool set_flag)
{
  char *end;
  int warning_status = NO_ERROR;
#if defined (SERVER_MODE)
  int id, error;
  THREAD_ENTRY *thread_p;
  CSS_CONN_ENTRY *conn_p;
#endif

  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (value == NULL || value[0] == '\0')
    {
      return PRM_ERR_BAD_VALUE;
    }

#if defined (SERVER_MODE)
  if (PRM_IS_FOR_SESSION (prm->flag) && BO_IS_SERVER_RESTARTED ())
    {
      id = prm_get_id (prm);
      thread_p = thread_get_thread_entry_info ();
      if (thread_p)
	{
	  conn_p = thread_p->conn_entry;
	  if (conn_p && conn_p->session_params)
	    {
	      error = prm_set_session_parameter_value (conn_p->session_params,
						       id, value, true);
	      if (error != PRM_ERR_NO_ERROR)
		{
		  return error;
		}
	      if (session_change_session_parameter (thread_p, id, value)
		  != NO_ERROR)
		{
		  return PRM_ERR_BAD_VALUE;
		}
	      return PRM_ERR_NO_ERROR;
	    }
	}
    }
  /* if prm is not for session or if session_params have not been initialized
   * just set the system parameter stored on server
   */
#endif /* SERVER_MODE */

  if (PRM_IS_INTEGER (prm))
    {
      int val, *valp;

      val = strtol (value, &end, 10);
      if (end == value)
	{
	  return PRM_ERR_BAD_VALUE;
	}
      if ((prm->upper_limit && PRM_GET_INT (prm->upper_limit) < val)
	  || (prm->lower_limit && PRM_GET_INT (prm->lower_limit) > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}

      valp = (int *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_FLOAT (prm))
    {
      float val, *valp;

      val = (float) strtod (value, &end);
      if (end == value)
	{
	  return PRM_ERR_BAD_VALUE;
	}
      if ((prm->upper_limit && PRM_GET_FLOAT (prm->upper_limit) < val)
	  || (prm->lower_limit && PRM_GET_FLOAT (prm->lower_limit) > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}

      valp = (float *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_BOOLEAN (prm))
    {
      bool *valp;
      const KEYVAL *keyvalp;

      keyvalp = prm_keyword (-1, value, boolean_words, DIM (boolean_words));
      if (keyvalp == NULL)
	{
	  return PRM_ERR_BAD_VALUE;
	}

      valp = (bool *) prm->value;
      *valp = (bool) keyvalp->val;
    }
  else if (PRM_IS_STRING (prm))
    {
      char *val, **valp;

      if (PRM_IS_ALLOCATED (prm->flag))
	{
	  char *str = PRM_GET_STRING (prm->value);
	  free_and_init (str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
	}

      valp = (char **) prm->value;

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
	      return (PRM_ERR_NO_MEM_FOR_PRM);
	    }
	  PRM_SET_BIT (PRM_ALLOCATED, prm->flag);
	}

      *valp = val;
    }
  else if (PRM_IS_KEYWORD (prm))
    {
      int val, *valp;
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
				    PRM_NAME_HA_MODE_FOR_SA_UTILS_ONLY) == 0)
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
	  keyvalp = prm_keyword (-1, value, compat_words, DIM (compat_words));
	}
      else if (intl_mbs_casecmp (prm->name, PRM_NAME_CHECK_PEER_ALIVE) == 0)
	{
	  keyvalp = prm_keyword (-1, value, check_peer_alive_words,
				 DIM (check_peer_alive_words));
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
	  val = strtol (value, &end, 10);
	  if (end == value)
	    {
	      return PRM_ERR_BAD_VALUE;
	    }
	}

      if ((prm->upper_limit && PRM_GET_INT (prm->upper_limit) < val)
	  || (prm->lower_limit && PRM_GET_INT (prm->lower_limit) > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}

      valp = (int *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_ERROR_LIST (prm))
    {
      bool *val, **valp;

      if (PRM_IS_ALLOCATED (prm->flag))
	{
	  bool *error_list = PRM_GET_ERROR_LIST (prm->value);

	  free_and_init (error_list);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
	}

      valp = (bool **) prm->value;

      /* check if the value is represented as a null keyword */
      if (prm_keyword (-1, value, null_words, DIM (null_words)) != NULL)
	{
	  val = NULL;
	}
      else
	{
	  char *s, *p;
	  int err_id;

	  val = calloc (-(ER_LAST_ERROR) + 1, sizeof (bool));
	  if (val == NULL)
	    {
	      size_t size = (-(ER_LAST_ERROR) + 1) * sizeof (bool);

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      return PRM_ERR_NO_MEM_FOR_PRM;
	    }

	  PRM_SET_BIT (PRM_ALLOCATED, prm->flag);
	  s = (char *) value;
	  p = s;

	  while (*s)
	    {
	      if (*s == ',')
		{
		  *s = '\0';
		  err_id = abs (atoi (p));
		  if (err_id != 0 && err_id <= -(ER_LAST_ERROR))
		    {
		      val[err_id] = true;
		    }
		  p = s + 1;
		  *s = ',';
		}
	      s++;
	    }

	  err_id = abs (atoi (p));
	  if (err_id != 0)
	    {
	      val[err_id] = true;
	    }
	}

      *valp = val;
    }
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      int *val, **valp;

      if (PRM_IS_ALLOCATED (prm->flag))
	{
	  int *int_list = PRM_GET_INTEGER_LIST (prm->value);

	  free_and_init (int_list);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
	}

      valp = (int **) prm->value;

      /* check if the value is represented as a null keyword */
      if (prm_keyword (-1, value, null_words, DIM (null_words)) != NULL)
	{
	  val = NULL;
	}
      else
	{
	  char *s, *p;
	  int list_size;

	  val = calloc (1024, sizeof (int));	/* max size is 1023 */
	  if (val == NULL)
	    {
	      size_t size = 1024 * sizeof (int);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	      return PRM_ERR_NO_MEM_FOR_PRM;
	    }

	  PRM_SET_BIT (PRM_ALLOCATED, prm->flag);

	  list_size = 0;
	  s = (char *) value;
	  p = s;

	  while (*s)
	    {
	      if (*s == ',')
		{
		  *s = '\0';
		  val[++list_size] = atoi (p);
		  p = s + 1;
		  *s = ',';
		}
	      s++;
	    }

	  val[++list_size] = atoi (p);
	  val[0] = list_size;
	}

      *valp = val;
    }
  else if (PRM_IS_SIZE (prm))
    {
      UINT64 val, *valp;

      if (util_size_string_to_byte (value, &val) != NO_ERROR)
	{
	  return PRM_ERR_BAD_VALUE;
	}

      if ((prm->upper_limit && PRM_GET_SIZE (prm->upper_limit) < val)
	  || (prm->lower_limit && PRM_GET_SIZE (prm->lower_limit) > val))
	{
	  return PRM_ERR_BAD_RANGE;
	}

      valp = (UINT64 *) prm->value;
      *valp = val;
    }
  else
    {
      assert (false);
    }

  if (PRM_IS_COMPOUND (prm->flag))
    {
      prm_compound_has_changed (prm, set_flag);
    }

  if (set_flag)
    {
      PRM_SET_BIT (PRM_SET, prm->flag);
      /* Indicate that the default value was not used */
      PRM_CLEAR_BIT (PRM_DEFAULT_USED, prm->flag);
    }

  return warning_status;
}

static int
prm_compound_has_changed (SYSPRM_PARAM * prm, bool set_flag)
{
  assert (PRM_IS_COMPOUND (prm->flag));

  if (prm == prm_find (PRM_NAME_COMPAT_MODE, NULL))
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
  if (PRM_IS_FOR_SESSION (prm->flag) && BO_IS_SERVER_RESTARTED ())
    {
      THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
      if (thread_p)
	{
	  CSS_CONN_ENTRY *conn_p = thread_p->conn_entry;
	  if (conn_p && conn_p->session_params)
	    {
	      int id = prm_get_id (prm);
	      int error =
		prm_set_session_parameter_default (conn_p->session_params, id,
						   true);
	      if (error != PRM_ERR_NO_ERROR)
		{
		  return error;
		}
	      if (session_set_session_parameter_default (thread_p, id)
		  != NO_ERROR)
		{
		  return PRM_ERR_BAD_VALUE;
		}
	      return PRM_ERR_NO_ERROR;
	    }
	}
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
  if (PRM_IS_SIZE (prm))
    {
      UINT64 val, *valp;

      val = PRM_GET_SIZE (prm->default_value);
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

      if (PRM_IS_ALLOCATED (prm->flag))
	{
	  char *str = PRM_GET_STRING (prm->value);
	  free_and_init (str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
	}

      val = *(char **) prm->default_value;
      valp = (char **) prm->value;
      *valp = val;
    }
  else if (PRM_IS_ERROR_LIST (prm))
    {
      bool *val, **valp;

      if (PRM_IS_ALLOCATED (prm->flag))
	{
	  bool *error_list = PRM_GET_ERROR_LIST (prm->value);

	  free_and_init (error_list);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
	}

      val = *(bool **) prm->default_value;
      valp = (bool **) prm->value;
      *valp = val;
    }
  else if (PRM_IS_INTEGER_LIST (prm))
    {
      int *val, **valp;

      if (PRM_IS_ALLOCATED (prm->flag))
	{
	  int *int_list = PRM_GET_INTEGER_LIST (prm->value);

	  free_and_init (int_list);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
	}

      val = *(int **) prm->default_value;
      valp = (int **) prm->value;
      *valp = val;
    }

  /* Indicate that the default value was used */
  PRM_SET_BIT (PRM_DEFAULT_USED, prm->flag);
  return NO_ERROR;
}

/*
 * prm_set_session_parameter_default - set session parameter value to default
 *
 * return: PRM_ERR_NO_ERROR or SYSPRM_ERR error code
 * session_params(in): list of session parameters
 * prm_id(in): parameter id
 * verify_different(in): if true will update the different flag
 */
SYSPRM_ERR
prm_set_session_parameter_default (SESSION_PARAM * session_params, int prm_id,
				   bool verify_different)
{
  SESSION_PARAM *sprm;
  SYSPRM_PARAM *prm = &prm_Def[prm_id];

  sprm = prm_get_session_prm_from_list (session_params, prm_id);
  if (sprm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  switch (sprm->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      sprm->prm_value.i = PRM_GET_INT (prm->default_value);
      break;
    case PRM_FLOAT:
      sprm->prm_value.f = PRM_GET_FLOAT (prm->default_value);
      break;
    case PRM_BOOLEAN:
      sprm->prm_value.b = PRM_GET_BOOL (prm->default_value);
      break;
    case PRM_SIZE:
      sprm->prm_value.size = PRM_GET_SIZE (prm->default_value);
      break;
    case PRM_STRING:
      {
	char *str;
	int alloc_size;
	if (sprm->prm_value.str)
	  {
	    free_and_init (sprm->prm_value.str);
	  }
	str = PRM_GET_STRING (prm->default_value);
	if (str != NULL)
	  {
	    alloc_size = strlen (str) + 1;
	    sprm->prm_value.str = (char *) malloc (alloc_size);
	    if (sprm->prm_value.str == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
		return PRM_ERR_NO_MEM_FOR_PRM;
	      }
	    memcpy (sprm->prm_value.str, str, alloc_size);
	  }
	break;
      }
    case PRM_ERROR_LIST:
      {
	bool *error_list;
	int alloc_size;
	if (sprm->prm_value.error_list)
	  {
	    free_and_init (sprm->prm_value.error_list);
	  }
	error_list = PRM_GET_ERROR_LIST (prm->default_value);
	if (error_list != NULL)
	  {
	    alloc_size = (-ER_LAST_ERROR + 1) * sizeof (bool);
	    sprm->prm_value.error_list = (bool *) malloc (alloc_size);
	    if (sprm->prm_value.error_list == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
		return PRM_ERR_NO_MEM_FOR_PRM;
	      }
	    memcpy (sprm->prm_value.error_list, error_list, alloc_size);
	  }
	break;
      }
    case PRM_INTEGER_LIST:
      {
	bool *integer_list;
	int alloc_size;
	if (sprm->prm_value.integer_list)
	  {
	    free_and_init (sprm->prm_value.integer_list);
	  }
	integer_list = PRM_GET_ERROR_LIST (prm->default_value);
	if (integer_list != NULL)
	  {
	    alloc_size = (integer_list[0] + 1) * sizeof (int);
	    sprm->prm_value.integer_list = (int *) malloc (alloc_size);
	    if (sprm->prm_value.integer_list == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, alloc_size);
		return PRM_ERR_NO_MEM_FOR_PRM;
	      }
	    memcpy (sprm->prm_value.integer_list, integer_list, alloc_size);
	  }
	break;
      }
    }
  if (verify_different)
    {
      if (prm_compare_prm_value_with_value (sprm->prm_value, prm->value,
					    sprm->datatype) != 0)
	{
	  PRM_SET_BIT (PRM_DIFFERENT, sprm->flag);
	}
      else
	{
	  PRM_CLEAR_BIT (PRM_DIFFERENT, sprm->flag);
	}
    }

  return PRM_ERR_NO_ERROR;
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
      prm_print (prm, val, LINE_MAX, PRM_PRINT_NONE);
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
  if (line >= 0)
    {
      fprintf (stderr, PARAM_MSG_FMT (PRM_ERR_BAD_LINE), key, line, where);
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
      if (PRM_IS_ALLOCATED (prm->flag) && PRM_IS_STRING (prm))
	{
	  char *str = PRM_GET_STRING (prm->value);

	  free_and_init (str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);

	  valp = (char **) prm->value;
	  *valp = NULL;
	}
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
  if (!(PRM_IS_SET (max_clients_prm->flag)))
    {
      if (PRM_HAS_DEFAULT (max_clients_prm->flag))
	{
	  (void) prm_set_default (max_clients_prm);
	}
      else
	{
	  fprintf (stderr,
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_PARAMETERS,
				   PRM_ERR_NO_VALUE), max_clients_prm->name);
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
      if (PRM_DEFAULT_VAL_USED (ha_server_state_prm->flag))
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
      || PRM_DEFAULT_VAL_USED (ha_node_list_prm->flag))
    {
      if (GETHOSTNAME (host_name, sizeof (host_name)))
	{
	  strncpy (host_name, "localhost", sizeof (host_name) - 1);
	}

      snprintf (newval, sizeof (newval) - 1, "%s@%s", host_name, host_name);
      prm_set (ha_node_list_prm, newval, false);
    }

  call_stack_dump_activation_prm =
    prm_find (PRM_NAME_CALL_STACK_DUMP_ACTIVATION, NULL);
  if (call_stack_dump_activation_prm)
    {
      bool *val;
      unsigned int i;

      if (!PRM_IS_ALLOCATED (call_stack_dump_activation_prm->flag))
	{
	  prm_set (call_stack_dump_activation_prm, "-2", false);
	}

      val = PRM_GET_ERROR_LIST (call_stack_dump_activation_prm->value);
      for (i = 0; i < DIM (call_stack_dump_error_codes); i++)
	{
	  val[abs (call_stack_dump_error_codes[i])] = true;
	}
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

  char newval[LINE_MAX];
  char host_name[MAXHOSTNAMELEN];

  int ha_process_dereg_confirm_interval_in_msecs;
  int ha_max_process_dereg_confirm;
  int shutdown_wait_time_in_secs;

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

  if (ha_node_list_prm == NULL
      || PRM_DEFAULT_VAL_USED (ha_node_list_prm->flag))
    {
      if (GETHOSTNAME (host_name, sizeof (host_name)))
	{
	  strncpy (host_name, "localhost", sizeof (host_name) - 1);
	}

      snprintf (newval, sizeof (newval) - 1, "%s@%s", host_name, host_name);
      prm_set (ha_node_list_prm, newval, false);
    }

  assert (ha_mode_prm != NULL);
  if (ha_mode_prm != NULL && PRM_GET_INT (ha_mode_prm->value) != HA_MODE_OFF)
    {
      assert (ha_process_dereg_confirm_interval_in_msecs_prm != NULL);
      assert (ha_max_process_dereg_confirm_prm != NULL);
      assert (shutdown_wait_time_in_secs_prm != NULL);

      if (ha_process_dereg_confirm_interval_in_msecs_prm != NULL
	  && ha_max_process_dereg_confirm_prm != NULL
	  && shutdown_wait_time_in_secs_prm != NULL)
	{
	  ha_process_dereg_confirm_interval_in_msecs =
	    PRM_GET_INT (ha_process_dereg_confirm_interval_in_msecs_prm->
			 value);
	  ha_max_process_dereg_confirm =
	    PRM_GET_INT (ha_max_process_dereg_confirm_prm->value);
	  shutdown_wait_time_in_secs =
	    PRM_GET_INT (shutdown_wait_time_in_secs_prm->value);

	  if ((shutdown_wait_time_in_secs * 1000) >
	      (ha_process_dereg_confirm_interval_in_msecs *
	       ha_max_process_dereg_confirm))
	    {
	      ha_max_process_dereg_confirm =
		((shutdown_wait_time_in_secs * 1000) /
		 ha_process_dereg_confirm_interval_in_msecs) + 3;

	      snprintf (newval, sizeof (newval) - 1, "%d",
			ha_max_process_dereg_confirm);
	      prm_set (ha_max_process_dereg_confirm_prm, newval, false);
	    }
	}
    }

  return;
}
#endif /* CS_MODE */

#if defined (CS_MODE)
/*
 * sysprm_tune_client_parameters - Sets the values of various client system
 *                          parameters depending on the value from the server
 *   return: none
 */
void
sysprm_tune_client_parameters (void)
{
  char data[LINE_MAX], *newval;
  int n, i, len = LINE_MAX;
  char *ptr = data;

#if !defined(NDEBUG)
  memset (data, 0, LINE_MAX);
#endif

  /* those parameters should be same to them of server's */
  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_GET_FROM_SERVER (prm_Def[i].flag))
	{
	  n = snprintf (ptr, len, "%s;", prm_Def[i].name);
	  ptr += n;
	  len -= n;
	  assert (len > 0);
	}
    }

  if (sysprm_obtain_server_parameters (data, LINE_MAX) == NO_ERROR)
    {
      newval = strtok (data, ";");
      while (newval != NULL)
	{
	  prm_change (newval, false);
	  newval = strtok (NULL, ";");
	}
    }
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
 * return: parameter name
 * prm_id (in): parameter id
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
 * return: pointer to value
 * prm_id (in): parameter id
 *
 * NOTE: for session parameters, in server mode, the value stored in
 *	 conn_entry->session_params is returned instead of the value
 *	 from prm_Def array.
 */
void *
prm_get_value (PARAM_ID prm_id)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *thread_p;
  CSS_CONN_ENTRY *conn_entry;

  assert (prm_id <= PRM_LAST_ID);

  if (PRM_IS_FOR_SESSION (prm_Def[prm_id].flag) && BO_IS_SERVER_RESTARTED ())
    {
      thread_p = thread_get_thread_entry_info ();
      if (thread_p)
	{
	  conn_entry = thread_p->conn_entry;
	  if (conn_entry && conn_entry->session_params)
	    {
	      SESSION_PARAM *prm =
		prm_get_session_prm_from_list (conn_entry->session_params,
					       prm_id);
	      if (prm)
		{
		  return &(prm->prm_value);
		}
	    }
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
 * return: value
 * prm_id (in): parameter id
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
 * return: value
 * prm_id (in): parameter id
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
 * return: value
 * prm_id (in): parameter id
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
 * return: value
 * prm_id (in): parameter id
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
 * return: value
 * prm_id (in): parameter id
 */
int *
prm_get_integer_list_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_INTEGER_LIST (&prm_Def[prm_id]));

  return PRM_GET_INTEGER_LIST (prm_get_value (prm_id));
}

/*
 * prm_get_error_list_value () - get the value of a parameter of type error
 *				 list
 *
 * return: value
 * prm_id (in): parameter id
 */
bool *
prm_get_error_list_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_ERROR_LIST (&prm_Def[prm_id]));

  return PRM_GET_ERROR_LIST (prm_get_value (prm_id));
}

/*
 * prm_get_size_value () - get the value of a parameter of type size
 *
 * return: value
 * prm_id (in): parameter id
 */
UINT64
prm_get_size_value (PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_SIZE (&prm_Def[prm_id]));

  return PRM_GET_SIZE (prm_get_value (prm_id));
}

/*
 * prm_set_value () - set a new value to a parameter
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
 */
void
prm_set_value (PARAM_ID prm_id, void *value)
{
  assert (prm_id <= PRM_LAST_ID);

  prm_Def[prm_id].value = value;
}

/*
 * prm_set_integer_value () - set a new value to a parameter of type integer
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
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
}

/*
 * prm_set_bool_value () - set a new value to a parameter of type bool
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
 */
void
prm_set_bool_value (PARAM_ID prm_id, bool value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_BOOLEAN (&prm_Def[prm_id]));

  PRM_GET_BOOL (prm_Def[prm_id].value) = value;
}

/*
 * prm_set_float_value () - set a new value to a parameter of type float
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
 */
void
prm_set_float_value (PARAM_ID prm_id, float value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_FLOAT (&prm_Def[prm_id]));

  PRM_GET_FLOAT (prm_Def[prm_id].value) = value;
}

/*
 * prm_set_string_value () - set a new value to a parameter of type string
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
 */
void
prm_set_string_value (PARAM_ID prm_id, char *value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_STRING (&prm_Def[prm_id]));

  PRM_GET_STRING (prm_Def[prm_id].value) = value;
}

/*
 * prm_set_error_list_value () - set a new value to a parameter of type
 *				 error list
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
 */
void
prm_set_error_list_value (PARAM_ID prm_id, bool * value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_ERROR_LIST (&prm_Def[prm_id]));

  PRM_GET_ERROR_LIST (prm_Def[prm_id].value) = value;
}

/*
 * prm_set_integer_list_value () - set a new value to a parameter of type
 *				   integer list
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
 */
void
prm_set_integer_list_value (PARAM_ID prm_id, int *value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_INTEGER_LIST (&prm_Def[prm_id]));

  PRM_GET_INTEGER_LIST (prm_Def[prm_id].value) = value;
}

/*
 * prm_set_size_value () - set a new value to a parameter of type size
 *
 * return: void
 * prm_id (in): parameter id
 * value (in): new value
 */
void
prm_set_size_value (PARAM_ID prm_id, UINT64 value)
{
  assert (prm_id <= PRM_LAST_ID);
  assert (PRM_IS_SIZE (&prm_Def[prm_id]));

  PRM_GET_SIZE (prm_Def[prm_id].value) = value;
}

/*
 * sysprm_duplicate_session_parameters () - duplicate a list of session
 *					    parameters
 *
 * return: duplicated list
 * src_prm: list of session parameters
 */
SESSION_PARAM *
sysprm_duplicate_session_parameters (SESSION_PARAM * src_prm)
{
  SESSION_PARAM *new_prm = NULL;
  int len;

  if (src_prm == NULL)
    {
      return NULL;
    }

  new_prm = (SESSION_PARAM *) malloc (sizeof (SESSION_PARAM));
  if (new_prm == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (SESSION_PARAM));
      return NULL;
    }

  new_prm->prm_id = src_prm->prm_id;
  new_prm->flag = src_prm->flag;
  new_prm->datatype = src_prm->datatype;

  /* duplicate value */
  switch (new_prm->datatype)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      new_prm->prm_value.i = src_prm->prm_value.i;
      break;

    case PRM_FLOAT:
      new_prm->prm_value.f = src_prm->prm_value.f;
      break;

    case PRM_BOOLEAN:
      new_prm->prm_value.b = src_prm->prm_value.b;
      break;

    case PRM_SIZE:
      new_prm->prm_value.size = src_prm->prm_value.size;
      break;

    case PRM_STRING:
      if (!src_prm->prm_value.str)
	{
	  new_prm->prm_value.str = NULL;
	}
      else
	{
	  len = strlen (src_prm->prm_value.str) + 1;
	  new_prm->prm_value.str = (char *) malloc (len * sizeof (char));
	  if (new_prm->prm_value.str == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, len * sizeof (char));
	      free_and_init (new_prm);
	      return NULL;
	    }
	  memcpy (new_prm->prm_value.str, src_prm->prm_value.str,
		  len * sizeof (char));
	}
      break;

    case PRM_ERROR_LIST:
      if (!src_prm->prm_value.error_list)
	{
	  new_prm->prm_value.error_list = NULL;
	}
      else
	{
	  len = -ER_LAST_ERROR + 1;
	  new_prm->prm_value.error_list =
	    (bool *) malloc ((len + 1) * sizeof (bool));
	  if (new_prm->prm_value.error_list == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      ((len + 1) * sizeof (bool)));
	      free_and_init (new_prm);
	      return NULL;
	    }
	  memcpy (new_prm->prm_value.error_list,
		  src_prm->prm_value.error_list, len * sizeof (bool));
	}
      break;

    case PRM_INTEGER_LIST:
      if (!src_prm->prm_value.integer_list)
	{
	  new_prm->prm_value.integer_list = NULL;
	}
      else
	{
	  len = src_prm->prm_value.integer_list[0] + 1;
	  new_prm->prm_value.integer_list =
	    (int *) malloc ((len + 1) * sizeof (int));
	  if (new_prm->prm_value.integer_list == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, (len + 1) * sizeof (int));
	      free_and_init (new_prm);
	      return NULL;
	    }
	  memcpy (new_prm->prm_value.integer_list,
		  src_prm->prm_value.integer_list, len * sizeof (int));
	}
      break;
    }

  if (src_prm->next)
    {
      new_prm->next = sysprm_duplicate_session_parameters (src_prm->next);
      if (new_prm->next == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SESSION_PARAM));
	  free_and_init (new_prm);
	  return NULL;
	}
    }
  else
    {
      new_prm->next = NULL;
    }

  return new_prm;
}

/*
 * sysprm_free_session_parameters () - free session parameter list
 *
 * return: void
 * session_prm_ptr: pointer to list to free
 */
void
sysprm_free_session_parameters (SESSION_PARAM ** session_prm_ptr)
{
  SESSION_PARAM *session_prm;

  assert (session_prm_ptr != NULL);

  session_prm = *session_prm_ptr;
  if (session_prm == NULL)
    {
      return;
    }

  if (session_prm->next)
    {
      sysprm_free_session_parameters (&session_prm->next);
    }

  if (PRM_IS_STRING (session_prm) && session_prm->prm_value.str)
    {
      free_and_init (session_prm->prm_value.str);
    }
  else if (PRM_IS_ERROR_LIST (session_prm)
	   && session_prm->prm_value.error_list)
    {
      free_and_init (session_prm->prm_value.error_list);
    }
  else if (PRM_IS_INTEGER_LIST (session_prm)
	   && session_prm->prm_value.integer_list)
    {
      free_and_init (session_prm->prm_value.integer_list);
    }

  free_and_init (session_prm);
  *session_prm_ptr = NULL;
}

/*
 * prm_get_session_prm_from_list () - find the session parameter identified
 *		by prm_id in list
 *
 * return: session parameter
 * session_params (in): list of session parameters
 * prm_id (in): id of session parameter
 */
static SESSION_PARAM *
prm_get_session_prm_from_list (SESSION_PARAM * session_params,
			       PARAM_ID prm_id)
{
  assert (prm_id <= PRM_LAST_ID);

  if (session_params->prm_id == prm_id)
    {
      return session_params;
    }

  if (session_params->next)
    {
      return prm_get_session_prm_from_list (session_params->next, prm_id);
    }

  return NULL;
}

/*
 * sysprm_pack_local_session_parameters - packs the locally stored session
 *		  parameters
 *
 * return: pointer to the new position after packing
 * ptr (in): pointer to the position where packing starts
 */
char *
sysprm_pack_local_session_parameters (char *ptr)
{
  int i;
  void *value;
  char *old_ptr = ptr;
  int count = 0;

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  ptr += OR_INT_SIZE;		/* skip one int
				 * the number of session parameters will be put later
				 */

  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_IS_FOR_SESSION (prm_Def[i].flag))
	{
	  count++;

	  ptr = or_pack_int (ptr, i);
	  ptr = or_pack_int (ptr, prm_Def[i].flag);
	  ptr = or_pack_int (ptr, prm_Def[i].datatype);

	  value = prm_Def[i].value;

	  switch (prm_Def[i].datatype)
	    {
	    case PRM_INTEGER:
	    case PRM_KEYWORD:
	      ptr = or_pack_int (ptr, PRM_GET_INT (value));
	      break;

	    case PRM_FLOAT:
	      ptr = or_pack_float (ptr, PRM_GET_FLOAT (value));
	      break;

	    case PRM_BOOLEAN:
	      ptr = or_pack_int (ptr, PRM_GET_BOOL (value));
	      break;

	    case PRM_STRING:
	      ptr = or_pack_string (ptr, PRM_GET_STRING (value));
	      break;

	    case PRM_ERROR_LIST:
	      ptr = or_pack_bool_array (ptr, PRM_GET_ERROR_LIST (value),
					(-ER_LAST_ERROR + 1));
	      break;

	    case PRM_INTEGER_LIST:
	      {
		int *integer_list = PRM_GET_INTEGER_LIST (value);

		if (integer_list)
		  {
		    int j;

		    for (j = 0; j <= integer_list[0]; j++)
		      {
			ptr = or_pack_int (ptr, integer_list[j]);
		      }
		  }
		else
		  {
		    ptr = or_pack_int (ptr, -1);
		  }
		break;
	      }

	    case PRM_SIZE:
	      ptr = or_pack_int64 (ptr, PRM_GET_SIZE (value));
	      break;

	    default:
	      assert (0);
	      break;
	    }
	}
    }

  OR_PUT_INT (old_ptr, count);

  return ptr;
}

/*
 * sysprm_packed_local_session_parameters_length () - returns the length of
 *		    	packed session parameters
 *
 * return: size of packed data
 *
 * NOTE: the locally stored system parameters are used
 */
int
sysprm_packed_local_session_parameters_length (void)
{
  int size = 0, i;
  void *value;

  size += OR_INT_SIZE;		/* number of session parameters */

  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_IS_FOR_SESSION (prm_Def[i].flag))
	{
	  size += OR_INT_SIZE;	/* prm_id */
	  size += OR_INT_SIZE;	/* flag */
	  size += OR_INT_SIZE;	/* datatype */

	  value = prm_Def[i].value;

	  switch (prm_Def[i].datatype)
	    {
	    case PRM_INTEGER:
	    case PRM_KEYWORD:
	    case PRM_BOOLEAN:
	      size += OR_INT_SIZE;
	      break;

	    case PRM_FLOAT:
	      size += OR_FLOAT_SIZE;
	      break;

	    case PRM_STRING:
	      size += or_packed_string_length (PRM_GET_STRING (value), NULL);
	      break;

	    case PRM_ERROR_LIST:
	      size +=
		or_packed_bool_array_length (PRM_GET_ERROR_LIST (value),
					     (-ER_LAST_ERROR + 1));
	      break;

	    case PRM_INTEGER_LIST:
	      {
		int *integer_list = PRM_GET_INTEGER_LIST (value);

		if (integer_list)
		  {
		    size += (integer_list[0] + 1) * OR_INT_SIZE;
		  }
		else
		  {
		    size += OR_INT_SIZE;	/* -1 */
		  }
		break;
	      }

	    case PRM_SIZE:
	      size += OR_INT64_SIZE;
	      break;

	    case PRM_NO_TYPE:
	      break;
	    }
	}
    }

  return size;
}

/*
 * sysprm_pack_session_parameters () - packs the list of session parameters
 *
 * return: new position pointer after packing
 * ptr (in): pointer to the position where packing starts
 * session_params (in): list of session parameters
 */
char *
sysprm_pack_session_parameters (char *ptr, SESSION_PARAM * session_params)
{
  char *old_ptr = ptr;
  SESSION_PARAM *prm;
  int count = 0;

  if (ptr == NULL)
    {
      return NULL;
    }

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  /* skip one int, the number of session parameters will be put later */
  ptr += OR_INT_SIZE;

  for (prm = session_params; prm; prm = prm->next)
    {
      count++;

      ptr = or_pack_int (ptr, prm->prm_id);
      ptr = or_pack_int (ptr, prm->flag);
      ptr = or_pack_int (ptr, prm->datatype);

      switch (prm->datatype)
	{
	case PRM_INTEGER:
	case PRM_KEYWORD:
	  ptr = or_pack_int (ptr, prm->prm_value.i);
	  break;

	case PRM_FLOAT:
	  ptr = or_pack_float (ptr, prm->prm_value.f);
	  break;

	case PRM_BOOLEAN:
	  ptr = or_pack_int (ptr, prm->prm_value.b);
	  break;

	case PRM_STRING:
	  ptr = or_pack_string (ptr, prm->prm_value.str);
	  break;

	case PRM_ERROR_LIST:
	  ptr = or_pack_bool_array (ptr, prm->prm_value.error_list,
				    (-ER_LAST_ERROR + 1));
	  break;

	case PRM_INTEGER_LIST:
	  if (prm->prm_value.integer_list)
	    {
	      int j;

	      for (j = 0; j <= prm->prm_value.integer_list[0]; j++)
		{
		  ptr = or_pack_int (ptr, prm->prm_value.integer_list[j]);
		}
	    }
	  else
	    {
	      ptr = or_pack_int (ptr, -1);
	    }
	  break;

	case PRM_SIZE:
	  ptr = or_pack_int64 (ptr, (INT64) prm->prm_value.size);
	  break;
	}
    }

  OR_PUT_INT (old_ptr, count);

  return ptr;
}

/*
 * sysprm_packed_session_parameters_length () - returns the length of packed
 *		session parameters in the list give as argument
 *
 * return: size of packed data
 * session_params (in):  list of session parameters
 */
int
sysprm_packed_session_parameters_length (SESSION_PARAM * session_params)
{
  SESSION_PARAM *prm;
  int size = 0;

  size += OR_INT_SIZE;		/* the number of session parameters */

  for (prm = session_params; prm; prm = prm->next)
    {
      size += OR_INT_SIZE;	/* prm_id */
      size += OR_INT_SIZE;	/* flag */
      size += OR_INT_SIZE;	/* datatype */

      switch (prm->datatype)
	{
	case PRM_INTEGER:
	case PRM_KEYWORD:
	case PRM_BOOLEAN:
	  size += OR_INT_SIZE;
	  break;

	case PRM_FLOAT:
	  size += OR_FLOAT_SIZE;
	  break;

	case PRM_STRING:
	  size += or_packed_string_length (prm->prm_value.str, NULL);
	  break;

	case PRM_ERROR_LIST:
	  size +=
	    or_packed_bool_array_length (prm->prm_value.error_list,
					 (-ER_LAST_ERROR + 1));
	  break;

	case PRM_INTEGER_LIST:
	  if (prm->prm_value.integer_list)
	    {
	      size += (prm->prm_value.integer_list[0] + 1) * OR_INT_SIZE;
	    }
	  else
	    {
	      size += OR_INT_SIZE;	/* -1 */
	    }
	  break;

	case PRM_SIZE:
	  size += OR_INT64_SIZE;
	  break;
	}
    }

  return size;
}

/*
 * sysprm_unpack_session_parameters () - unpacks a list of session parameters
 *			    from buffer
 *
 * return: new pointer position after unpacking
 * ptr (in): pointer to position where unpacking starts
 * session_params_ptr (out): pointer to the unpacked list of session
 *			     parameters
 */
char *
sysprm_unpack_session_parameters (char *ptr,
				  SESSION_PARAM ** session_params_ptr)
{
  SESSION_PARAM *prm, *session_params = NULL;
  int count, i;

  assert (session_params_ptr != NULL);

  ptr = or_unpack_int (ptr, &count);

  for (i = 0; i < count; i++)
    {
      prm = (SESSION_PARAM *) malloc (sizeof (SESSION_PARAM));
      if (prm == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SESSION_PARAM));
	  goto error;
	}

      ptr = or_unpack_int (ptr, (int *) (&prm->prm_id));
      ptr = or_unpack_int (ptr, &prm->flag);
      ptr = or_unpack_int (ptr, &prm->datatype);

      switch (prm->datatype)
	{
	case PRM_INTEGER:
	case PRM_KEYWORD:
	  ptr = or_unpack_int (ptr, &prm->prm_value.i);
	  break;

	case PRM_BOOLEAN:
	  {
	    int temp;

	    ptr = or_unpack_int (ptr, &temp);
	    prm->prm_value.b = (bool) temp;
	    break;
	  }

	case PRM_SIZE:
	  ptr = or_unpack_int64 (ptr, &prm->prm_value.size);
	  break;

	case PRM_STRING:
	  {
	    char *str = NULL;
	    int size;

	    ptr = or_unpack_string (ptr, &str);
	    if (str == NULL)
	      {
		prm->prm_value.str = NULL;
	      }
	    else
	      {
		size = (strlen (str) + 1) * sizeof (char);
		prm->prm_value.str = (char *) malloc (size);
		if (prm->prm_value.str == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		    db_private_free (NULL, str);
		    goto error;
		  }
		else
		  {
		    memcpy (prm->prm_value.str, str, size);
		    db_private_free (NULL, str);
		  }
	      }
	    break;
	  }

	case PRM_ERROR_LIST:
	  {
	    bool *bools;
	    int size;

	    ptr = or_unpack_bool_array (ptr, &bools);
	    if (bools == NULL)
	      {
		prm->prm_value.error_list = NULL;
	      }
	    else
	      {
		size = (-ER_LAST_ERROR + 1) * sizeof (bool);
		prm->prm_value.error_list = (bool *) malloc (size);
		if (prm->prm_value.error_list == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		    db_private_free (NULL, bools);
		    goto error;
		  }
		else
		  {
		    memcpy (prm->prm_value.error_list, bools, size);
		    db_private_free (NULL, bools);
		  }
	      }
	    break;
	  }

	case PRM_INTEGER_LIST:
	  {
	    int temp, i;

	    ptr = or_unpack_int (ptr, &temp);
	    if (temp == -1)
	      {
		prm->prm_value.integer_list = NULL;
	      }
	    else
	      {
		prm->prm_value.integer_list =
		  (int *) malloc ((temp + 1) * OR_INT_SIZE);
		if (prm->prm_value.integer_list == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_OUT_OF_VIRTUAL_MEMORY, 1,
			    (temp + 1) * OR_INT_SIZE);
		    return NULL;
		  }

		prm->prm_value.integer_list[0] = temp;
		for (i = 0; i <= temp; i++)
		  {
		    ptr =
		      or_unpack_int (ptr, &prm->prm_value.integer_list[i]);
		  }
	      }
	    break;
	  }
	}

      prm->next = NULL;

      if (session_params)
	{
	  prm->next = session_params;
	  session_params = prm;
	}
      else
	{
	  session_params = prm;
	}
    }

  *session_params_ptr = session_params;
  return ptr;

error:
  sysprm_free_session_parameters (&session_params);
  return NULL;
}

/*
 * prm_get_id () - returns the id for a system parameter
 *
 * return: id
 * prm(in): address for system parameter
 */
static int
prm_get_id (const SYSPRM_PARAM * prm)
{
  int id = (prm - prm_Def);

  assert (id >= PRM_FIRST_ID && id <= PRM_LAST_ID);

  return id;
}

#if defined (SERVER_MODE)
/*
 * sysprm_session_init_session_parameters () - tries to set the list of
 *		session parameters to session_state and css_conn_entry.
 *		If there is a list of session parameters in session_state,
 *		that list is used instead (this way the values changed online
 *		are kept even when the client reconnects.
 *
 * return: NO_ERROR or error_code
 * session_params_ptr (in/out): the list of session parameters
 */
int
sysprm_session_init_session_parameters (SESSION_PARAM ** session_params_ptr)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  int error_code;
  SESSION_PARAM *session_params = NULL, *prm;

  /* first check if there is a list in session state */
  error_code = session_get_session_parameters (thread_p, &session_params);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (session_params)
    {
      /* a list on session state was found, use that list */
      sysprm_free_session_parameters (session_params_ptr);
      *session_params_ptr = session_params;
    }
  else
    {
      /* a list on session state was not found, use the list given as
       * argument
       */
      session_params = *session_params_ptr;

      error_code = session_set_session_parameters (thread_p, session_params);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  if (thread_p->conn_entry->session_params)
    {
      sysprm_free_session_parameters (&thread_p->conn_entry->session_params);
    }
  thread_p->conn_entry->session_params = session_params;

  /* update different flags */
  for (prm = session_params; prm; prm = prm->next)
    {
      if (prm_compare_prm_value_with_value (prm->prm_value,
					    prm_Def[prm->prm_id].value,
					    prm->datatype) != 0)
	{
	  PRM_SET_BIT (PRM_DIFFERENT, prm->flag);
	}
      else
	{
	  PRM_CLEAR_BIT (PRM_DIFFERENT, prm->flag);
	}
    }

  return NO_ERROR;
}

/*
 * sysprm_packed_different_session_parameters_length () - size of integer array
 *				that contains pairs of (id, is_different) for
 *				each session parameter
 *
 * return: size of packed data
 */
int
sysprm_packed_different_session_parameters_length (void)
{
  THREAD_ENTRY *thread_p;
  SESSION_PARAM *prm;
  int size = 0;

  size += OR_INT_SIZE;		/* number of session parameters */

  thread_p = thread_get_thread_entry_info ();
  if (!thread_p || !thread_p->conn_entry
      || !thread_p->conn_entry->session_params)
    {
      return size;
    }

  for (prm = thread_p->conn_entry->session_params; prm; prm = prm->next)
    {
      size += OR_INT_SIZE	/* prm_id */
	+ OR_INT_SIZE;		/* is_different, 1 or 0 */
    }

  return size;
}

/*
 * sysprm_pack_different_session_parameters () - packs an array of integers
 *		  containing the number of packed parameters then pairs of
 *		  (id, is_different) for each parameter
 *
 * return: new pointer position after packing
 * ptr (in): pointer position from where the packed data starts
 */
char *
sysprm_pack_different_session_parameters (char *ptr)
{
  THREAD_ENTRY *thread_p;
  SESSION_PARAM *session_params, *prm;
  int count = 0;
  char *old_ptr = ptr;

  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  thread_p = thread_get_thread_entry_info ();
  if (!thread_p || !thread_p->conn_entry
      || !thread_p->conn_entry->session_params)
    {
      ptr = or_pack_int (ptr, 0);
      return ptr;
    }

  session_params = thread_p->conn_entry->session_params;

  ptr += OR_INT_SIZE;		/* skip the count field */
  for (prm = session_params; prm; prm = prm->next)
    {
      count++;
      ptr = or_pack_int (ptr, prm->prm_id);
      ptr = or_pack_int (ptr, PRM_IS_DIFFERENT (prm->flag) ? 1 : 0);
    }
  OR_PUT_INT (old_ptr, count);

  return ptr;
}
#endif /* SERVER_MODE */

/*
 * sysprm_unpack_different_session_parameters () - unpacks the different
 *			      session parameters
 *
 * return: new pointer position after unpacking
 * data_ptr (out): pointer to an integer array that will store the number
 *	for parameters in the first field, then pairs of (id, is_different)
 *	for each session parameter
 */
char *
sysprm_unpack_different_session_parameters (char *ptr, int **data_ptr)
{
  int count, i;
  int *data;

  assert (data_ptr != NULL);

  data = *data_ptr;
  ptr = or_unpack_int (ptr, &count);

  data = (int *) malloc ((2 * count + 1) * OR_INT_SIZE);
  if (data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (2 * count + 1) * OR_INT_SIZE);
      *data_ptr = NULL;
      return NULL;
    }

  for (i = 0; i < count; i++)
    {
      ptr = or_unpack_int (ptr, &data[2 * i + 1]);	/* prm_id */
      ptr = or_unpack_int (ptr, &data[2 * i + 2]);	/* is_different 1 or 0 */
    }

  data[0] = count;
  *data_ptr = data;

  return ptr;
}

/*
 * prm_set_session_parameter_value - set a new value for the session parameter
 *		  in session_params list identified by id
 *
 * return: PRM_ERR_NO_ERROR or error_code
 * session_params (in): list of session parameters
 * id (in): id for the session parameter that needs changed
 * value (in): new value
 * verify_different (in): if true, the new value is compared with the value
 *		stored on server and the different flag is updated
 */
SYSPRM_ERR
prm_set_session_parameter_value (SESSION_PARAM * session_params,
				 int id, const char *value,
				 bool verify_different)
{
  SESSION_PARAM *sprm;
  char *end;
  SYSPRM_PARAM *prm = &prm_Def[id];

  sprm = prm_get_session_prm_from_list (session_params, id);
  if (sprm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  switch (sprm->datatype)
    {
    case PRM_INTEGER:
      {
	int val;

	val = strtol (value, &end, 10);
	if (end == value)
	  {
	    return PRM_ERR_BAD_VALUE;
	  }

	if ((prm->upper_limit && PRM_GET_INT (prm->upper_limit) < val)
	    || (prm->lower_limit && PRM_GET_INT (prm->lower_limit) > val))
	  {
	    return PRM_ERR_BAD_RANGE;
	  }

	sprm->prm_value.i = val;
	break;
      }

    case PRM_FLOAT:
      {
	float val;

	val = (float) strtod (value, &end);
	if (end == value)
	  {
	    return PRM_ERR_BAD_VALUE;
	  }

	if ((prm->upper_limit && PRM_GET_FLOAT (prm->upper_limit) < val)
	    || (prm->lower_limit && PRM_GET_FLOAT (prm->lower_limit) > val))
	  {
	    return PRM_ERR_BAD_RANGE;
	  }

	sprm->prm_value.f = val;
	break;
      }

    case PRM_BOOLEAN:
      {
	const KEYVAL *keyvalp;

	keyvalp = prm_keyword (-1, value, boolean_words, DIM (boolean_words));
	if (keyvalp == NULL)
	  {
	    return PRM_ERR_BAD_VALUE;
	  }

	sprm->prm_value.b = (bool) keyvalp->val;
	break;
      }

    case PRM_STRING:
      {
	char *val;

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

	if (sprm->prm_value.str)
	  {
	    free_and_init (sprm->prm_value.str);
	  }

	sprm->prm_value.str = val;
	break;
      }

    case PRM_ERROR_LIST:
      {
	bool *val;

	/* check if the value is represented as a null keyword */
	if (prm_keyword (-1, value, null_words, DIM (null_words)) != NULL)
	  {
	    val = NULL;
	  }
	else
	  {
	    char *s, *p;
	    int err_id;

	    val = calloc (-(ER_LAST_ERROR) + 1, sizeof (bool));
	    if (val == NULL)
	      {
		size_t size = (-(ER_LAST_ERROR) + 1) * sizeof (bool);
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		return PRM_ERR_NO_MEM_FOR_PRM;
	      }

	    s = (char *) value;
	    p = s;

	    while (*s)
	      {
		if (*s == ',')
		  {
		    *s = '\0';
		    err_id = abs (atoi (p));
		    if (err_id != 0 && err_id <= -(ER_LAST_ERROR))
		      {
			val[err_id] = true;
		      }
		    p = s + 1;
		    *s = ',';
		  }
		s++;
	      }

	    err_id = abs (atoi (p));
	    if (err_id != 0)
	      {
		val[err_id] = true;
	      }
	  }

	if (sprm->prm_value.error_list)
	  {
	    free_and_init (sprm->prm_value.error_list);
	  }

	sprm->prm_value.error_list = val;
	break;
      }

    case PRM_INTEGER_LIST:
      {
	int *val;

	/* check if the value is represented as a null keyword */
	if (prm_keyword (-1, value, null_words, DIM (null_words)) != NULL)
	  {
	    val = NULL;
	  }
	else
	  {
	    char *s, *p;
	    int list_size;

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

	    while (*s)
	      {
		if (*s == ',')
		  {
		    *s = '\0';
		    val[++list_size] = atoi (p);
		    p = s + 1;
		    *s = ',';
		  }
		s++;
	      }

	    val[++list_size] = atoi (p);
	    val[0] = list_size;
	  }

	if (sprm->prm_value.integer_list)
	  {
	    free_and_init (sprm->prm_value.integer_list);
	  }

	sprm->prm_value.integer_list = val;
	break;
      }

    case PRM_SIZE:
      {
	UINT64 val;

	if (util_size_string_to_byte (value, &val) != NO_ERROR)
	  {
	    return PRM_ERR_BAD_VALUE;
	  }

	if ((prm->upper_limit && PRM_GET_SIZE (prm->upper_limit) < val)
	    || (prm->lower_limit && PRM_GET_SIZE (prm->lower_limit) > val))
	  {
	    return PRM_ERR_BAD_RANGE;
	  }

	sprm->prm_value.size = val;
	break;
      }

    case PRM_KEYWORD:
      {
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
	    val = strtol (value, &end, 10);
	    if (end == value)
	      {
		return PRM_ERR_BAD_VALUE;
	      }
	  }

	if ((prm->upper_limit && PRM_GET_INT (prm->upper_limit) < val)
	    || (prm->lower_limit && PRM_GET_INT (prm->lower_limit) > val))
	  {
	    return PRM_ERR_BAD_RANGE;
	  }

	sprm->prm_value.i = val;
      }
    }

  if (verify_different)
    {
      if (prm_compare_prm_value_with_value (sprm->prm_value, prm->value,
					    sprm->datatype) != 0)
	{
	  PRM_SET_BIT (PRM_DIFFERENT, sprm->flag);
	}
      else
	{
	  PRM_CLEAR_BIT (PRM_DIFFERENT, sprm->flag);
	}
    }

  return PRM_ERR_NO_ERROR;
}

/*
 * prm_compare_prm_value_with_value () - compare a value stored in prm_value
 *                        field in a session parameter with a void* value
 *
 * return: 0 if equal, otherwise != 0
 * prm_value (in):
 * value (in):
 * val_type (in): datatype for values
 */
static int
prm_compare_prm_value_with_value (PRM_VALUE prm_value, void *value,
				  unsigned int val_type)
{
  switch (val_type)
    {
    case PRM_INTEGER:
    case PRM_KEYWORD:
      return (prm_value.i != PRM_GET_INT (value));

    case PRM_BOOLEAN:
      return (prm_value.b != PRM_GET_BOOL (value));

    case PRM_FLOAT:
      return (prm_value.f != PRM_GET_FLOAT (value));

    case PRM_STRING:
      {
	char *str = PRM_GET_STRING (value);

	if (prm_value.str == NULL && str == NULL)
	  {
	    return 0;
	  }

	if (prm_value.str == NULL || str == NULL)
	  {
	    return 1;
	  }

	return intl_mbs_casecmp (prm_value.str, str);
      }

    case PRM_SIZE:
      return prm_value.size != PRM_GET_SIZE (value);

    case PRM_ERROR_LIST:
      {
	int i;
	bool *error_list = PRM_GET_ERROR_LIST (value);

	if (prm_value.error_list == NULL && error_list == NULL)
	  {
	    return 0;
	  }

	if (prm_value.error_list == NULL || error_list == NULL)
	  {
	    return 1;
	  }

	for (i = 1; i <= -ER_LAST_ERROR; i++)
	  {
	    if (prm_value.error_list[i] != error_list[i])
	      {
		return 1;
	      }
	  }

	return 0;
      }

    case PRM_INTEGER_LIST:
      {
	int i;
	int *integer_list = PRM_GET_INTEGER_LIST (value);

	if (prm_value.integer_list == NULL && integer_list == NULL)
	  {
	    return 0;
	  }

	if (prm_value.integer_list == NULL || integer_list == NULL)
	  {
	    return 1;
	  }

	if (prm_value.integer_list[0] != integer_list[0])
	  {
	    return 1;
	  }

	for (i = 1; i <= integer_list[0]; i++)
	  {
	    if (prm_value.integer_list[i] != integer_list[i])
	      {
		return 0;
	      }
	  }

	return 1;
      }

    default:
      assert (0);
      break;
    }

  return 0;
}

/*
 * prm_update_prm_different_flag () - update the different flag for the system
 *			parameter at prm_id in prm_Def array.
 *
 * return: void
 * prm_id (in): parameter id
 * is_different (in): true if different values on client and server, false
 *		      otherwise
 */
void
prm_update_prm_different_flag (PARAM_ID prm_id, bool is_different)
{
  assert (prm_id <= PRM_LAST_ID);

  if (is_different)
    {
      PRM_SET_BIT (PRM_DIFFERENT, prm_Def[prm_id].flag);
    }
  else
    {
      PRM_CLEAR_BIT (PRM_DIFFERENT, prm_Def[prm_id].flag);
    }
}

/*
 * sysprm_update_client_session_parameters () - update the session parameters
 *			stored on client from session parameter list
 *
 * return: void
 * session_params (in): session parameter list
 */
void
sysprm_update_client_session_parameters (SESSION_PARAM * session_params)
{
  SESSION_PARAM *sprm;
  int size;

  for (sprm = session_params; sprm; sprm = sprm->next)
    {
      /* update value */
      switch (sprm->datatype)
	{
	case PRM_INTEGER:
	case PRM_KEYWORD:
	  prm_set_integer_value (sprm->prm_id, sprm->prm_value.i);
	  break;

	case PRM_FLOAT:
	  prm_set_float_value (sprm->prm_id, sprm->prm_value.f);
	  break;

	case PRM_BOOLEAN:
	  prm_set_bool_value (sprm->prm_id, sprm->prm_value.b);
	  break;

	case PRM_STRING:
	  if (PRM_GET_STRING (prm_Def[sprm->prm_id].value))
	    {
	      free_and_init (PRM_GET_STRING (prm_Def[sprm->prm_id].value));
	    }
	  if (sprm->prm_value.str)
	    {
	      char *str;
	      size = strlen (sprm->prm_value.str) + 1;
	      str = (char *) malloc (size);
	      if (str == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  return;
		}
	      memcpy (str, sprm->prm_value.str, size);
	      prm_set_string_value (sprm->prm_id, str);
	    }
	  break;
	case PRM_ERROR_LIST:
	  if (PRM_GET_ERROR_LIST (prm_Def[sprm->prm_id].value))
	    {
	      free_and_init (PRM_GET_ERROR_LIST
			     (prm_Def[sprm->prm_id].value));
	    }
	  if (sprm->prm_value.error_list)
	    {
	      bool *error_list;
	      size = (-ER_LAST_ERROR + 1) * sizeof (bool);
	      error_list = (bool *) malloc (size);
	      if (error_list == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  return;
		}
	      memcpy (error_list, sprm->prm_value.error_list, size);
	      prm_set_error_list_value (sprm->prm_id, error_list);
	    }
	  break;

	case PRM_INTEGER_LIST:
	  if (PRM_GET_INTEGER_LIST (prm_Def[sprm->prm_id].value))
	    {
	      free_and_init (PRM_GET_INTEGER_LIST
			     (prm_Def[sprm->prm_id].value));
	    }
	  if (sprm->prm_value.integer_list)
	    {
	      int *integer_list;
	      size = (sprm->prm_value.integer_list[0] + 1) * sizeof (int);
	      integer_list = (int *) malloc (size);
	      if (integer_list == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
		  return;
		}
	      memcpy (integer_list, sprm->prm_value.integer_list, size);
	      prm_set_integer_list_value (sprm->prm_id, integer_list);
	    }
	  break;

	case PRM_SIZE:
	  prm_set_size_value (sprm->prm_id, sprm->prm_value.size);
	  break;
	}

      /* update different flag */
      if (PRM_IS_DIFFERENT (sprm->flag))
	{
	  PRM_SET_BIT (PRM_DIFFERENT, prm_Def[sprm->prm_id].flag);
	}
      else
	{
	  PRM_CLEAR_BIT (PRM_DIFFERENT, prm_Def[sprm->prm_id].flag);
	}
    }
}

/*
 * sysprm_print_different_session_parameters () - print all different session
 *		      parameters like "id=value;id=value;..."
 *
 * return: printed string
 */
char *
sysprm_print_different_session_parameters (void)
{
  int i, n, len = LINE_MAX;
  char buf[LINE_MAX];
  char *ptr, *q, size;

  memset (buf, 0, LINE_MAX);
  ptr = buf;

  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_IS_FOR_SESSION (prm_Def[i].flag)
	  && PRM_IS_DIFFERENT (prm_Def[i].flag))
	{
	  n = prm_print (&prm_Def[i], ptr, len, PRM_PRINT_ID);
	  ptr += n;
	  len -= n;

	  n = snprintf (ptr, len, ";");
	  ptr += n;
	  len -= n;
	}
    }

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

#if !defined (SERVER_MODE)
/*
 * prm_init_intl_param () -
 *
 * return: printed string
 */
static void
prm_init_intl_param (void)
{
  SYSPRM_PARAM *prm_date_lang;
  SYSPRM_PARAM *prm_number_lang;

  prm_date_lang = prm_find (PRM_NAME_INTL_DATE_LANG, NULL);
  prm_number_lang = prm_find (PRM_NAME_INTL_NUMBER_LANG, NULL);

  if (prm_date_lang != NULL)
    {
      if (PRM_GET_STRING (prm_date_lang->value))
	{
	  free_and_init (PRM_GET_STRING (prm_date_lang->value));
	}
      PRM_CLEAR_BIT (PRM_ALLOCATED, prm_date_lang->flag);
      prm_set (prm_date_lang, lang_get_Lang_name (), true);
    }

  if (prm_number_lang != NULL)
    {
      if (PRM_GET_STRING (prm_number_lang->value))
	{
	  free_and_init (PRM_GET_STRING (prm_number_lang->value));
	}
      PRM_CLEAR_BIT (PRM_ALLOCATED, prm_number_lang->flag);
      prm_set (prm_number_lang, lang_get_Lang_name (), true);
    }
}
#endif

/*
 * sysprm_set_error () - sets an error for system parameter errors
 *
 * return    : error code
 * rc (in)   : SYSPRM_ERR error
 * data (in) : data to be printed with error
 */
int
sysprm_set_error (SYSPRM_ERR rc, char *data)
{
  int error = NO_ERROR;

  if (rc != NO_ERROR)
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
