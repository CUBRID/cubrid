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
  PRM_ERR_FILE_ERR = 29,
  PRM_ERR_NOT_FOR_CLIENT_NO_AUTH = 30,
  PRM_ERR_BAD_PARAM = 31
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

typedef enum query_trace_format QUERY_TRACE_FORMAT;
enum query_trace_format
{
  QUERY_TRACE_TEXT = 1,
  QUERY_TRACE_JSON
};

/* NOTE:
 * System parameter ids must respect the order in prm_Def array
 */
typedef enum param_id PARAM_ID;
enum param_id
{
  PRM_FIRST_ID = 0,
  PRM_ID_ER_LOG_DEBUG = 0,
  PRM_ID_ER_BTREE_DEBUG,
  PRM_ID_ER_LOG_LEVEL,
  PRM_ID_ER_LOG_WARNING,
  PRM_ID_ER_EXIT_ASK,
  PRM_ID_ER_LOG_SIZE,
  PRM_ID_ER_LOG_FILE,
  PRM_ID_ACCESS_IP_CONTROL,
  PRM_ID_ACCESS_IP_CONTROL_FILE,
  PRM_ID_IO_LOCKF_ENABLE,
  PRM_ID_SR_NBUFFERS,
  PRM_ID_SORT_BUFFER_SIZE,
  PRM_ID_PB_BUFFER_FLUSH_RATIO,
  PRM_ID_PB_NBUFFERS,
  PRM_ID_PAGE_BUFFER_SIZE,
  PRM_ID_HF_UNFILL_FACTOR,
  PRM_ID_HF_MAX_BESTSPACE_ENTRIES,
  PRM_ID_BT_UNFILL_FACTOR,
  PRM_ID_BT_OID_NBUFFERS,
  PRM_ID_BT_OID_BUFFER_SIZE,
  PRM_ID_BT_INDEX_SCAN_OID_ORDER,
  PRM_ID_BOSR_MAXTMP_PAGES,
  PRM_ID_LK_TIMEOUT_MESSAGE_DUMP_LEVEL,
  PRM_ID_LK_ESCALATION_AT,
  PRM_ID_LK_ROLLBACK_ON_LOCK_ESCALATION,
  PRM_ID_LK_TIMEOUT_SECS,
  PRM_ID_LK_TIMEOUT,
  PRM_ID_LK_RUN_DEADLOCK_INTERVAL,
  PRM_ID_LOG_NBUFFERS,
  PRM_ID_LOG_BUFFER_SIZE,
  PRM_ID_LOG_CHECKPOINT_NPAGES,
  PRM_ID_LOG_CHECKPOINT_SIZE,
  PRM_ID_LOG_CHECKPOINT_INTERVAL_SECS,
  PRM_ID_LOG_CHECKPOINT_INTERVAL,
  PRM_ID_LOG_CHECKPOINT_SLEEP_MSECS,
  PRM_ID_LOG_BACKGROUND_ARCHIVING,
  PRM_ID_LOG_ISOLATION_LEVEL,
  PRM_ID_LOG_MEDIA_FAILURE_SUPPORT,
  PRM_ID_COMMIT_ON_SHUTDOWN,
  PRM_ID_SHUTDOWN_WAIT_TIME_IN_SECS,
  PRM_ID_CSQL_AUTO_COMMIT,
  PRM_ID_LOG_SWEEP_CLEAN,
  PRM_ID_WS_HASHTABLE_SIZE,
  PRM_ID_WS_MEMORY_REPORT,
  PRM_ID_GC_ENABLE,
  PRM_ID_TCP_PORT_ID,
  PRM_ID_TCP_CONNECTION_TIMEOUT,
  PRM_ID_OPTIMIZATION_LEVEL,
  PRM_ID_QO_DUMP,
  PRM_ID_CSS_MAX_CLIENTS,
  PRM_ID_THREAD_STACKSIZE,
  PRM_ID_CFG_DB_HOSTS,
  PRM_ID_RESET_TR_PARSER,
  PRM_ID_IO_BACKUP_NBUFFERS,
  PRM_ID_IO_BACKUP_MAX_VOLUME_SIZE,
  PRM_ID_IO_BACKUP_SLEEP_MSECS,
  PRM_ID_MAX_PAGES_IN_TEMP_FILE_CACHE,
  PRM_ID_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
  PRM_ID_PTHREAD_SCOPE_PROCESS,	/* AIX only */
  PRM_ID_TEMP_MEM_BUFFER_PAGES,
  PRM_ID_INDEX_SCAN_KEY_BUFFER_PAGES,
  PRM_ID_INDEX_SCAN_KEY_BUFFER_SIZE,
  PRM_ID_DONT_REUSE_HEAP_FILE,
  PRM_ID_QUERY_MODE_SYNC,
  PRM_ID_INSERT_MODE,
  PRM_ID_LK_MAX_SCANID_BIT,
  PRM_ID_HOSTVAR_LATE_BINDING,
  PRM_ID_ENABLE_HISTO,
  PRM_ID_MUTEX_BUSY_WAITING_CNT,
  PRM_ID_PB_NUM_LRU_CHAINS,
  PRM_ID_PAGE_BG_FLUSH_INTERVAL_MSEC,
  PRM_ID_PAGE_BG_FLUSH_INTERVAL,
  PRM_ID_ADAPTIVE_FLUSH_CONTROL,
  PRM_ID_MAX_FLUSH_PAGES_PER_SECOND,
  PRM_ID_MAX_FLUSH_SIZE_PER_SECOND,
  PRM_ID_PB_SYNC_ON_NFLUSH,
  PRM_ID_PB_SYNC_ON_FLUSH_SIZE,
  PRM_ID_PB_DEBUG_PAGE_VALIDATION_LEVEL,
  PRM_ID_ORACLE_STYLE_OUTERJOIN,
  PRM_ID_ANSI_QUOTES,
  PRM_ID_DEFAULT_WEEK_FORMAT,
  PRM_ID_TEST_MODE,
  PRM_ID_ONLY_FULL_GROUP_BY,
  PRM_ID_PIPES_AS_CONCAT,
  PRM_ID_MYSQL_TRIGGER_CORRELATION_NAMES,
  PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER,
  PRM_ID_NO_BACKSLASH_ESCAPES,
  PRM_ID_GROUP_CONCAT_MAX_LEN,
  PRM_ID_STRING_MAX_SIZE_BYTES,
  PRM_ID_ADD_COLUMN_UPDATE_HARD_DEFAULT,
  PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS,
  PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT,
  PRM_ID_COMPACTDB_PAGE_RECLAIM_ONLY,
  PRM_ID_PLUS_AS_CONCAT,
  PRM_ID_LIKE_TERM_SELECTIVITY,
  PRM_ID_MAX_OUTER_CARD_OF_IDXJOIN,
  PRM_ID_ORACLE_STYLE_EMPTY_STRING,
  PRM_ID_SUPPRESS_FSYNC,
  PRM_ID_CALL_STACK_DUMP_ON_ERROR,
  PRM_ID_CALL_STACK_DUMP_ACTIVATION,
  PRM_ID_CALL_STACK_DUMP_DEACTIVATION,
  PRM_ID_COMPAT_NUMERIC_DIVISION_SCALE,
  PRM_ID_DBFILES_PROTECT,
  PRM_ID_AUTO_RESTART_SERVER,
  PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES,
#if defined (ENABLE_UNUSED_FUNCTION)
  PRM_ID_XASL_MAX_PLAN_CACHE_CLONES,
#endif /* ENABLE_UNUSED_FUNCTION */
  PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES,
  PRM_ID_FILTER_PRED_MAX_CACHE_CLONES,
  PRM_ID_XASL_PLAN_CACHE_TIMEOUT,
  PRM_ID_LIST_QUERY_CACHE_MODE,
  PRM_ID_LIST_MAX_QUERY_CACHE_ENTRIES,
  PRM_ID_LIST_MAX_QUERY_CACHE_PAGES,
  PRM_ID_USE_ORDERBY_SORT_LIMIT,
  PRM_ID_REPLICATION_MODE,
  PRM_ID_HA_MODE,
  PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY,
  PRM_ID_HA_SERVER_STATE,
  PRM_ID_HA_LOG_APPLIER_STATE,
  PRM_ID_HA_NODE_LIST,
  PRM_ID_HA_REPLICA_LIST,
  PRM_ID_HA_DB_LIST,
  PRM_ID_HA_COPY_LOG_BASE,
  PRM_ID_HA_COPY_SYNC_MODE,
  PRM_ID_HA_APPLY_MAX_MEM_SIZE,
  PRM_ID_HA_PORT_ID,
  PRM_ID_HA_INIT_TIMER_IN_MSECS,
  PRM_ID_HA_HEARTBEAT_INTERVAL_IN_MSECS,
  PRM_ID_HA_CALC_SCORE_INTERVAL_IN_MSECS,
  PRM_ID_HA_FAILOVER_WAIT_TIME_IN_MSECS,
  PRM_ID_HA_PROCESS_START_CONFIRM_INTERVAL_IN_MSECS,
  PRM_ID_HA_PROCESS_DEREG_CONFIRM_INTERVAL_IN_MSECS,
  PRM_ID_HA_MAX_PROCESS_START_CONFIRM,
  PRM_ID_HA_MAX_PROCESS_DEREG_CONFIRM,
  PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS,
  PRM_ID_HA_CHANGEMODE_INTERVAL_IN_MSECS,
  PRM_ID_HA_MAX_HEARTBEAT_GAP,
  PRM_ID_HA_PING_HOSTS,
  PRM_ID_HA_APPLYLOGDB_RETRY_ERROR_LIST,
  PRM_ID_HA_APPLYLOGDB_IGNORE_ERROR_LIST,
  PRM_ID_HA_APPLYLOGDB_LOG_WAIT_TIME_IN_SECS,
  PRM_ID_HA_SQL_LOGGING,
  PRM_ID_HA_SQL_LOG_MAX_SIZE_IN_MB,
  PRM_ID_HA_COPY_LOG_MAX_ARCHIVES,
  PRM_ID_HA_COPY_LOG_TIMEOUT,
  PRM_ID_HA_REPLICA_DELAY_IN_SECS,
  PRM_ID_HA_REPLICA_TIME_BOUND,
  PRM_ID_HA_DELAY_LIMIT_IN_SECS,
  PRM_ID_HA_DELAY_LIMIT_DELTA_IN_SECS,
  PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL_IN_MSECS,
  PRM_ID_HA_APPLYLOGDB_MAX_COMMIT_INTERVAL,
  PRM_ID_HA_CHECK_DISK_FAILURE_INTERVAL_IN_SECS,
  PRM_ID_JAVA_STORED_PROCEDURE,
  PRM_ID_COMPAT_PRIMARY_KEY,
  PRM_ID_LOG_HEADER_FLUSH_INTERVAL,
  PRM_ID_LOG_ASYNC_COMMIT,
  PRM_ID_LOG_GROUP_COMMIT_INTERVAL_MSECS,
  PRM_ID_LOG_BG_FLUSH_INTERVAL_MSECS,
  PRM_ID_LOG_BG_FLUSH_NUM_PAGES,
  PRM_ID_INTL_MBS_SUPPORT,
  PRM_ID_LOG_COMPRESS,
  PRM_ID_BLOCK_NOWHERE_STATEMENT,
  PRM_ID_BLOCK_DDL_STATEMENT,
#if defined (ENABLE_UNUSED_FUNCTION)
  PRM_ID_SINGLE_BYTE_COMPARE,
#endif
  PRM_ID_CSQL_HISTORY_NUM,
  PRM_ID_LOG_TRACE_DEBUG,
  PRM_ID_DL_FORK,
  PRM_ID_ER_PRODUCTION_MODE,
  PRM_ID_ER_STOP_ON_ERROR,
  PRM_ID_TCP_RCVBUF_SIZE,
  PRM_ID_TCP_SNDBUF_SIZE,
  PRM_ID_TCP_NODELAY,
  PRM_ID_TCP_KEEPALIVE,
  PRM_ID_CSQL_SINGLE_LINE_MODE,
  PRM_ID_XASL_DEBUG_DUMP,
  PRM_ID_LOG_MAX_ARCHIVES,
  PRM_ID_FORCE_REMOVE_LOG_ARCHIVES,
  PRM_ID_REMOVE_LOG_ARCHIVES_INTERVAL,
  PRM_ID_LOG_NO_LOGGING,
  PRM_ID_UNLOADDB_IGNORE_ERROR,
  PRM_ID_UNLOADDB_LOCK_TIMEOUT,
  PRM_ID_LOADDB_FLUSH_INTERVAL,
  PRM_ID_IO_TEMP_VOLUME_PATH,
  PRM_ID_IO_VOLUME_EXT_PATH,
  PRM_ID_UNIQUE_ERROR_KEY_VALUE,
  PRM_ID_USE_SYSTEM_MALLOC,
  PRM_ID_EVENT_HANDLER,
  PRM_ID_EVENT_ACTIVATION,
  PRM_ID_READ_ONLY_MODE,
  PRM_ID_MNT_WAITING_THREAD,
  PRM_ID_MNT_STATS_THRESHOLD,
  PRM_ID_SERVICE_SERVICE_LIST,
  PRM_ID_SERVICE_SERVER_LIST,
  PRM_ID_SESSION_STATE_TIMEOUT,
  PRM_ID_MULTI_RANGE_OPT_LIMIT,
  PRM_ID_INTL_NUMBER_LANG,
  PRM_ID_INTL_DATE_LANG,
  /* All the compound parameters *must* be at the end of the array so that the
     changes they cause are not overridden by other parameters (for example in
     sysprm_load_and_init the parameters are set to their default in the order
     they are found in this array). */
  PRM_ID_COMPAT_MODE,
  PRM_ID_DB_VOLUME_SIZE,
  PRM_ID_LOG_VOLUME_SIZE,
  PRM_ID_UNICODE_INPUT_NORMALIZATION,
  PRM_ID_UNICODE_OUTPUT_NORMALIZATION,
  PRM_ID_INTL_CHECK_INPUT_STRING,
  PRM_ID_CHECK_PEER_ALIVE,
  PRM_ID_SQL_TRACE_SLOW_MSECS,
  PRM_ID_SQL_TRACE_SLOW,
  PRM_ID_SQL_TRACE_EXECUTION_PLAN,
  PRM_ID_LOG_TRACE_FLUSH_TIME_MSECS,
  PRM_ID_INTL_COLLATION,
  PRM_ID_GENERIC_VOL_PREALLOC_SIZE,
  PRM_ID_SORT_LIMIT_MAX_COUNT,
  PRM_ID_SQL_TRACE_IOREADS,
  PRM_ID_QUERY_TRACE,
  PRM_ID_QUERY_TRACE_FORMAT,
  PRM_ID_MAX_RECURSION_SQL_DEPTH,
  PRM_ID_UPDATE_USE_ATTRIBUTE_REFERENCES,
  PRM_ID_PB_AIN_RATIO,
  PRM_ID_PB_AOUT_RATIO,
  PRM_ID_MAX_AGG_HASH_SIZE,
  PRM_ID_AGG_HASH_RESPECT_ORDER,
  PRM_ID_USE_BTREE_FENCE_KEY,
  PRM_ID_QA_BTREE_RANDOM_EXIT,

  PRM_ID_OPTIMIZER_ENABLE_MERGE_JOIN,
  PRM_ID_OPTIMIZER_RESERVE_01,
  PRM_ID_OPTIMIZER_RESERVE_02,
  PRM_ID_OPTIMIZER_RESERVE_03,
  PRM_ID_OPTIMIZER_RESERVE_04,
  PRM_ID_OPTIMIZER_RESERVE_05,
  PRM_ID_OPTIMIZER_RESERVE_06,
  PRM_ID_OPTIMIZER_RESERVE_07,
  PRM_ID_OPTIMIZER_RESERVE_08,
  PRM_ID_OPTIMIZER_RESERVE_09,
  PRM_ID_OPTIMIZER_RESERVE_10,
  PRM_ID_OPTIMIZER_RESERVE_11,
  PRM_ID_OPTIMIZER_RESERVE_12,
  PRM_ID_OPTIMIZER_RESERVE_13,
  PRM_ID_OPTIMIZER_RESERVE_14,
  PRM_ID_OPTIMIZER_RESERVE_15,
  PRM_ID_OPTIMIZER_RESERVE_16,
  PRM_ID_OPTIMIZER_RESERVE_17,
  PRM_ID_OPTIMIZER_RESERVE_18,
  PRM_ID_OPTIMIZER_RESERVE_19,
  PRM_ID_OPTIMIZER_RESERVE_20,

  PRM_ID_HA_REPL_ENABLE_SERVER_SIDE_UPDATE,
  PRM_ID_PB_LRU_HOT_RATIO,
  PRM_ID_HA_PREFETCHLOGDB_ENABLE,
  PRM_ID_HA_PREFETCHLOGDB_MAX_THREAD_COUNT,
  PRM_ID_HA_PREFETCHLOGDB_PAGE_DISTANCE,
  PRM_ID_HA_PREFETCHLOGDB_MAX_PAGE_COUNT,

  PRM_ID_MVCC_ENABLED,
  PRM_ID_VACUUM_MASTER_WAKEUP_INTERVAL,
  PRM_ID_VACUUM_DATA_PAGES,
  PRM_ID_VACUUM_LOG_BLOCK_PAGES,
  PRM_ID_VACUUM_WORKER_COUNT,
  PRM_ID_ER_LOG_VACUUM,
  PRM_ID_DISABLE_VACUUM,

  /* Debugging system parameter */
  PRM_ID_LOG_BTREE_OPS,

  PRM_ID_OBJECT_PRINT_FORMAT_OID,
  /* change PRM_LAST_ID when adding new system parameters */
  PRM_LAST_ID = PRM_ID_OBJECT_PRINT_FORMAT_OID
};

/*
 *  System parameter data types
 */
typedef enum
{
  PRM_INTEGER = 0,
  PRM_FLOAT,
  PRM_BOOLEAN,
  PRM_KEYWORD,
  PRM_BIGINT,
  PRM_STRING,
  PRM_INTEGER_LIST,

  PRM_NO_TYPE
} SYSPRM_DATATYPE;

typedef union sysprm_value SYSPRM_VALUE;
union sysprm_value
{
  int i;
  bool b;
  float f;
  char *str;
  int *integer_list;
  UINT64 bi;
};

typedef struct session_param SESSION_PARAM;
struct session_param
{
  PARAM_ID prm_id;
  unsigned int flag;
  int datatype;
  SYSPRM_VALUE value;
};

typedef struct sysprm_assign_value SYSPRM_ASSIGN_VALUE;
struct sysprm_assign_value
{
  PARAM_ID prm_id;
  SYSPRM_VALUE value;
  SYSPRM_ASSIGN_VALUE *next;
};

#if defined (CS_MODE)
/* when system parameters are loaded, session parameters need to be cached for
 * future clients that connect to broker
 */
extern SESSION_PARAM *cached_session_parameters;
#endif /* CS_MODE */

extern const char *prm_get_name (PARAM_ID prm_id);

extern void *prm_get_value (PARAM_ID prm_id);
extern int prm_get_integer_value (PARAM_ID prm_id);
extern float prm_get_float_value (PARAM_ID prm_id);
extern bool prm_get_bool_value (PARAM_ID prm_id);
extern char *prm_get_string_value (PARAM_ID prm_id);
extern int *prm_get_integer_list_value (PARAM_ID prm_id);
extern UINT64 prm_get_bigint_value (PARAM_ID prm_id);

extern void prm_set_integer_value (PARAM_ID prm_id, int value);
extern void prm_set_float_value (PARAM_ID prm_id, float value);
extern void prm_set_bool_value (PARAM_ID prm_id, bool value);
extern void prm_set_string_value (PARAM_ID prm_id, char *value);
extern void prm_set_integer_list_value (PARAM_ID prm_id, int *value);
extern void prm_set_bigint_value (PARAM_ID prm_id, UINT64 value);

extern bool sysprm_find_err_in_integer_list (PARAM_ID prm_id, int error_code);

extern int sysprm_load_and_init (const char *db_name, const char *conf_file);
extern int sysprm_load_and_init_client (const char *db_name,
					const char *conf_file);
extern int sysprm_reload_and_init (const char *db_name,
				   const char *conf_file);
extern void sysprm_final (void);
extern void sysprm_dump_parameters (FILE * fp);
extern void sysprm_set_er_log_file (const char *base_db_name);
extern void sysprm_dump_server_parameters (FILE * fp);
extern int sysprm_obtain_parameters (char *data,
				     SYSPRM_ASSIGN_VALUE ** prm_values);
extern int sysprm_change_server_parameters (const SYSPRM_ASSIGN_VALUE *
					    assignments);
extern int sysprm_obtain_server_parameters (SYSPRM_ASSIGN_VALUE **
					    prm_values_ptr);
extern int sysprm_get_force_server_parameters (SYSPRM_ASSIGN_VALUE **
					       change_values);
extern void sysprm_tune_client_parameters (void);
extern void sysprm_free_session_parameters (SESSION_PARAM **
					    session_parameters);

#if !defined (CS_MODE)
extern void xsysprm_change_server_parameters (const SYSPRM_ASSIGN_VALUE *
					      assignments);
extern void xsysprm_obtain_server_parameters (SYSPRM_ASSIGN_VALUE *
					      prm_values);
extern SYSPRM_ASSIGN_VALUE *xsysprm_get_force_server_parameters (void);
extern void xsysprm_dump_server_parameters (FILE * fp);
#endif /* !CS_MODE */

extern int sysprm_set_force (const char *pname, const char *pvalue);
extern int sysprm_set_to_default (const char *pname, bool set_to_force);
extern int sysprm_check_range (const char *pname, void *value);
extern int sysprm_get_range (const char *pname, void *min, void *max);
extern int prm_get_master_port_id (void);
extern bool prm_get_commit_on_shutdown (void);
extern bool prm_get_query_mode_sync (void);

extern char *sysprm_pack_session_parameters (char *ptr,
					     SESSION_PARAM *
					     session_parameters);
extern int sysprm_packed_session_parameters_length (SESSION_PARAM *
						    session_parameters,
						    int offset);
extern char *sysprm_unpack_session_parameters (char *ptr,
					       SESSION_PARAM **
					       session_parameters_ptr);
extern char *sysprm_pack_assign_values (char *ptr,
					const SYSPRM_ASSIGN_VALUE *
					assign_values);
extern int sysprm_packed_assign_values_length (const SYSPRM_ASSIGN_VALUE *
					       assign_values, int offset);
extern char *sysprm_unpack_assign_values (char *ptr,
					  SYSPRM_ASSIGN_VALUE **
					  assign_values_ptr);
extern void sysprm_free_assign_values (SYSPRM_ASSIGN_VALUE **
				       assign_values_ptr);
extern void sysprm_change_parameter_values (const SYSPRM_ASSIGN_VALUE *
					    assignments,
					    bool check, bool set_flag);

#if defined (SERVER_MODE)
extern int sysprm_session_init_session_parameters (SESSION_PARAM **
						   session_params,
						   int
						   *found_session_parameters);
#endif /* SERVER_MODE */

#if defined (CS_MODE)
extern void sysprm_update_client_session_parameters (SESSION_PARAM *
						     session_parameters);
#endif /* CS_MODE */

#if !defined (SERVER_MODE)
extern char *sysprm_print_parameters_for_qry_string (void);
extern char *sysprm_print_parameters_for_ha_repl (void);
extern SYSPRM_ERR sysprm_validate_change_parameters (const char *data,
						     bool check,
						     SYSPRM_ASSIGN_VALUE **
						     assignments_ptr);
extern SYSPRM_ERR
sysprm_make_default_values (const char *data, char *default_val_buf,
			    const int buf_size);
extern void sysprm_init_intl_param (void);
#endif /* !SERVER_MODE */

extern int sysprm_print_assign_values (SYSPRM_ASSIGN_VALUE * prm_values,
				       char *buffer, int length);
extern int sysprm_set_error (SYSPRM_ERR rc, const char *data);
extern int sysprm_get_session_parameters_count (void);
#endif /* _SYSTEM_PARAMETER_H_ */
