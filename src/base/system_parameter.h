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
#include "porting_inline.hpp"
#include "chartype.h"

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

enum compat_mode
{
  COMPAT_CUBRID,
  COMPAT_MYSQL,
  COMPAT_ORACLE
    /*
     * COMPAT_ANSI, COMPAT_DB2, COMPAT_MAXDB, COMPAT_MSSQL, COMPAT_POSTGRESQL */
};
typedef enum compat_mode COMPAT_MODE;

enum query_trace_format
{
  QUERY_TRACE_TEXT = 1,
  QUERY_TRACE_JSON
};
typedef enum query_trace_format QUERY_TRACE_FORMAT;

/* NOTE:
 * System parameter ids must respect the order in prm_Def array
 * When adding a new system paramter, insert it before PRM_LAST_ID and change PRM_LAST_ID to it
 */
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
  PRM_ID_INSERT_MODE,
  PRM_ID_LK_MAX_SCANID_BIT,
  PRM_ID_HOSTVAR_LATE_BINDING,
  PRM_ID_ENABLE_HISTO,
  PRM_ID_MUTEX_BUSY_WAITING_CNT,
  PRM_ID_PB_NUM_LRU_CHAINS,
  PRM_ID_PAGE_BG_FLUSH_INTERVAL_MSECS,
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
  PRM_ID_XASL_CACHE_MAX_ENTRIES,
  PRM_ID_XASL_CACHE_MAX_CLONES,
  PRM_ID_XASL_CACHE_TIMEOUT,
  PRM_ID_XASL_CACHE_LOGGING,
  PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES,
  PRM_ID_FILTER_PRED_MAX_CACHE_CLONES,
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
  PRM_ID_GENERAL_RESERVE_01,
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
  /* All the compound parameters *must* be at the end of the array so that the changes they cause are not overridden by
   * other parameters (for example in sysprm_load_and_init the parameters are set to their default in the order they
   * are found in this array). */
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
  PRM_ID_PB_AOUT_RATIO,
  PRM_ID_MAX_AGG_HASH_SIZE,
  PRM_ID_AGG_HASH_RESPECT_ORDER,
  PRM_ID_USE_BTREE_FENCE_KEY,
  PRM_ID_OPTIMIZER_ENABLE_MERGE_JOIN,
  PRM_ID_MAX_HASH_LIST_SCAN_SIZE,
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
  PRM_ID_PB_LRU_BUFFER_RATIO,

  PRM_ID_VACUUM_MASTER_WAKEUP_INTERVAL,
  PRM_ID_VACUUM_LOG_BLOCK_PAGES,
  PRM_ID_VACUUM_WORKER_COUNT,
  PRM_ID_ER_LOG_VACUUM,
  PRM_ID_DISABLE_VACUUM,

  /* Debugging system parameter */
  PRM_ID_LOG_BTREE_OPS,

  PRM_ID_OBJECT_PRINT_FORMAT_OID,

  PRM_ID_TIMEZONE,
  PRM_ID_SERVER_TIMEZONE,
  PRM_ID_TZ_LEAP_SECOND_SUPPORT,

  PRM_ID_OPTIMIZER_ENABLE_AGGREGATE_OPTIMIZATION,

  PRM_ID_VACUUM_PREFETCH_LOG_NBUFFERS,	// Obsolete
  PRM_ID_VACUUM_PREFETCH_LOG_BUFFER_SIZE,	// Obsolete
  PRM_ID_VACUUM_PREFETCH_LOG_MODE,	// Obsolete

  PRM_ID_PB_NEIGHBOR_FLUSH_NONDIRTY,
  PRM_ID_PB_NEIGHBOR_FLUSH_PAGES,
  PRM_ID_FAULT_INJECTION_IDS,
  PRM_ID_FAULT_INJECTION_TEST,
  PRM_ID_FAULT_INJECTION_ACTION_PREFER_ABORT_TO_EXIT,

  PRM_ID_HA_REPL_FILTER_TYPE,
  PRM_ID_HA_REPL_FILTER_FILE,

  PRM_ID_COMPENSATE_DEBUG,
  PRM_ID_POSTPONE_DEBUG,

  PRM_ID_CLIENT_CLASS_CACHE_DEBUG,

  PRM_ID_EXAMINE_CLIENT_CACHED_LOCKS,

  PRM_ID_PB_SEQUENTIAL_VICTIM_FLUSH,

  PRM_ID_LOG_UNIQUE_STATS,

  PRM_ID_LOGPB_LOGGING_DEBUG,

  PRM_ID_FORCE_RESTART_TO_SKIP_RECOVERY,

  PRM_ID_EXTENDED_STATISTICS_ACTIVATION,

  PRM_ID_ENABLE_STRING_COMPRESSION,

  PRM_ID_XASL_CACHE_TIME_THRESHOLD_IN_MINUTES,

  PRM_ID_DISK_LOGGING,
  PRM_ID_FILE_LOGGING,

  PRM_ID_PB_NUM_PRIVATE_CHAINS,
  PRM_ID_PB_MONITOR_LOCKS,

  PRM_ID_CTE_MAX_RECURSIONS,

  PRM_ID_JSON_LOG_ALLOCATIONS,
  PRM_ID_JSON_MAX_ARRAY_IDX,

  PRM_ID_CONNECTION_LOGGING,

  PRM_ID_THREAD_LOGGING_FLAG,
  PRM_ID_LOG_QUERY_LISTS,

  PRM_ID_THREAD_CONNECTION_POOLING,
  PRM_ID_THREAD_CONNECTION_TIMEOUT_SECONDS,
  PRM_ID_THREAD_WORKER_POOLING,
  PRM_ID_THREAD_WORKER_TIMEOUT_SECONDS,

  PRM_ID_DWB_SIZE,
  PRM_ID_DWB_BLOCKS,
  PRM_ID_ENABLE_DWB_FLUSH_THREAD,
  PRM_ID_DWB_LOGGING,
  PRM_ID_DATA_FILE_ADVISE,

  PRM_ID_DEBUG_LOG_ARCHIVES,
  PRM_ID_DEBUG_ES,
  PRM_ID_DEBUG_BESTSPACE,
  PRM_ID_DEBUG_LOGWR,
  PRM_ID_DEBUG_AUTOCOMMIT,
  PRM_ID_DEBUG_REPLICATION_DATA,
  PRM_ID_TRACK_REQUESTS,
  PRM_ID_LOG_PGBUF_VICTIM_FLUSH,
  PRM_ID_LOG_CHKPT_DETAILED,
  PRM_ID_IB_TASK_MEMSIZE,
  PRM_ID_STATS_ON,
  PRM_ID_LOADDB_WORKER_COUNT,
  PRM_ID_PERF_TEST_MODE,
  PRM_ID_REPR_CACHE_LOG,

  PRM_ID_ENABLE_NEW_LFHASH,

  PRM_ID_HEAP_INFO_CACHE_LOGGING,

  PRM_ID_TDE_KEYS_FILE_PATH,
  PRM_ID_TDE_DEFAULT_ALGORITHM,
  PRM_ID_ER_LOG_TDE,

  PRM_ID_JAVA_STORED_PROCEDURE,
  PRM_ID_JAVA_STORED_PROCEDURE_PORT,
  PRM_ID_JAVA_STORED_PROCEDURE_JVM_OPTIONS,
  PRM_ID_JAVA_STORED_PROCEDURE_DEBUG,
  PRM_ID_JAVA_STORED_PROCEDURE_UDS,
  PRM_ID_ALLOW_TRUNCATED_STRING,
  PRM_ID_TB_DEFAULT_REUSE_OID,
  PRM_ID_USE_STAT_ESTIMATION,
  PRM_ID_IGNORE_TRAILING_SPACE,
  PRM_ID_DDL_AUDIT_LOG,
  PRM_ID_DDL_AUDIT_LOG_SIZE,
  PRM_ID_SUPPLEMENTAL_LOG,
  PRM_ID_CDC_LOGGING_DEBUG,
  PRM_ID_RECOVERY_PROGRESS_LOGGING_INTERVAL,
  PRM_ID_FIRST_LOG_PAGEID,	/* Except for QA or TEST purposes, never use it. */
  PRM_ID_THREAD_CORE_COUNT,
  PRM_ID_FLASHBACK_TIMEOUT,
  PRM_ID_FLASHBACK_MAX_TRANSACTION,	/* Hidden parameter For QA test */
  PRM_ID_FLASHBACK_WIN_SIZE,	/* Hidden parameter For QA test */
  PRM_ID_USE_USER_HOSTS,
  PRM_ID_QMGR_MAX_QUERY_PER_TRAN,
  PRM_ID_REGEXP_ENGINE,
  PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR,
  PRM_ID_HA_TCP_PING_HOSTS,
  PRM_ID_HA_PING_TIMEOUT,
  PRM_ID_STATDUMP_FORCE_ADD_INT_MAX,	/* Hidden parameter for QA only */
  PRM_ID_HA_SQL_LOG_PATH,
  PRM_ID_VACUUM_OVFP_CHECK_DURATION,
  PRM_ID_VACUUM_OVFP_CHECK_THRESHOLD,
  PRM_ID_DEDUPLICATE_KEY_LEVEL,	/* support for SUPPORT_DEDUPLICATE_KEY_MODE */
  PRM_ID_PRINT_INDEX_DETAIL,	/* support for SUPPORT_DEDUPLICATE_KEY_MODE */
  PRM_ID_HA_SQL_LOG_MAX_COUNT,
  PRM_ID_ENABLE_MEMORY_MONITORING,
  /* change PRM_LAST_ID when adding new system parameters */
  PRM_LAST_ID = PRM_ID_ENABLE_MEMORY_MONITORING
};
typedef enum param_id PARAM_ID;

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

enum sysprm_load_flag
{
  SYSPRM_LOAD_ALL = 0x0,
  SYSPRM_IGNORE_HA = 0x1,
  SYSPRM_IGNORE_INTL_PARAMS = 0x2
};
typedef enum sysprm_load_flag SYSPRM_LOAD_FLAG;

#define SYSPRM_LOAD_IS_IGNORE_HA(flags) ((flags) & SYSPRM_IGNORE_HA)
#define SYSPRM_LOAD_IS_IGNORE_INTL(flags) ((flags) & SYSPRM_IGNORE_INTL_PARAMS)

#ifdef __cplusplus
extern "C"
{
#endif

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
#define PRM_FOR_SESSION	    0x00000100	/* is a session parameter - all client or client/server parameters that can be
					 * changed on-line */
#define PRM_FORCE_SERVER    0x00000200	/* client should get value from server */
#define PRM_FOR_QRY_STRING  0x00000400	/* if a parameter can affect the plan generation it should be included in the
					 * query string */
#define PRM_CLIENT_SESSION  0x00000800	/* mark those client/server session parameters that should not affect the
					 * server */
#define PRM_SIZE_UNIT       0x00001000	/* has size unit interface */
#define PRM_TIME_UNIT       0x00002000	/* has time unit interface */
#define PRM_DIFFER_UNIT     0x00004000	/* parameter unit need to be changed */
#define PRM_FOR_HA_CONTEXT  0x00008000	/* should be replicated into HA log applier */

#define PRM_GET_SERVER      0x00010000	/* return the value of server parameter from client/server parameter. Note that
					 * this flag only can be set if the parameter has PRM_FOR_CLIENT,
					 * PRM_FOR_SERVER, and PRM_USER_CHANGE flags. */

#define PRM_DEPRECATED      0x40000000	/* is deprecated */
#define PRM_OBSOLETED       0x80000000	/* is obsoleted */

/*
 * Dynamic flags
 */
#define PRM_SET             0x00000001	/* has been set */
#define PRM_ALLOCATED       0x00000002	/* storage has been malloc'd */
#define PRM_DEFAULT_USED    0x00000004	/* Default value has been used */
#define PRM_DIFFERENT	    0x00000008	/* mark those parameters that have values different than their default.
					 * currently used by parameters that should be printed to query string */

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
 * Macros to get data type
 */
#define PRM_IS_STRING(x)          ((x)->datatype == PRM_STRING)
#define PRM_IS_INTEGER(x)         ((x)->datatype == PRM_INTEGER)
#define PRM_IS_FLOAT(x)           ((x)->datatype == PRM_FLOAT)
#define PRM_IS_BOOLEAN(x)         ((x)->datatype == PRM_BOOLEAN)
#define PRM_IS_KEYWORD(x)         ((x)->datatype == PRM_KEYWORD)
#define PRM_IS_INTEGER_LIST(x)    ((x)->datatype == PRM_INTEGER_LIST)
#define PRM_IS_BIGINT(x)          ((x)->datatype == PRM_BIGINT)

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

  typedef int (*DUP_PRM_FUNC) (void *, SYSPRM_DATATYPE, void *, SYSPRM_DATATYPE);

  struct sysprm_param
  {
    PARAM_ID id;		/* parameter ID */
    const char *name;		/* the keyword expected */
    unsigned int static_flag;	/* bitmask flag representing status words */
    SYSPRM_DATATYPE datatype;	/* value data type */
    unsigned int *dynamic_flag;	/* shared by both original and duplicated */
    void *default_value;	/* address of (pointer to) default value */
    void *value;		/* address of (pointer to) current value */
    void *upper_limit;		/* highest allowable value */
    void *lower_limit;		/* lowest allowable value */
    char *force_value;		/* address of (pointer to) force value string */
    DUP_PRM_FUNC set_dup;	/* set duplicated value to original value */
    DUP_PRM_FUNC get_dup;	/* get duplicated value from original value */
  };
  typedef struct sysprm_param SYSPRM_PARAM;

  extern SYSPRM_PARAM prm_Def[];

#if defined (CS_MODE)
/* when system parameters are loaded, session parameters need to be cached for
 * future clients that connect to broker
 */
  extern SESSION_PARAM *cached_session_parameters;
#endif				/* CS_MODE */

  extern const char *prm_get_name (PARAM_ID prm_id);

  extern void *prm_get_value (PARAM_ID prm_id);

  extern void prm_set_integer_value (PARAM_ID prm_id, int value);
  extern void prm_set_float_value (PARAM_ID prm_id, float value);
  extern void prm_set_bool_value (PARAM_ID prm_id, bool value);
  extern void prm_set_string_value (PARAM_ID prm_id, char *value);
  extern void prm_set_integer_list_value (PARAM_ID prm_id, int *value);
  extern void prm_set_bigint_value (PARAM_ID prm_id, UINT64 value);

  extern bool sysprm_find_err_in_integer_list (PARAM_ID prm_id, int error_code);
  extern bool sysprm_find_fi_code_in_integer_list (PARAM_ID prm_id, int fi_code);

  extern int sysprm_load_and_init (const char *db_name, const char *conf_file, const int load_flags);
  extern int sysprm_load_and_init_client (const char *db_name, const char *conf_file);
  extern int sysprm_reload_and_init (const char *db_name, const char *conf_file);
  extern void sysprm_final (void);
  extern void sysprm_dump_parameters (FILE * fp);
  extern void sysprm_set_er_log_file (const char *base_db_name);
  extern void sysprm_dump_server_parameters (FILE * fp);
  extern SYSPRM_ERR sysprm_obtain_parameters (char *data, SYSPRM_ASSIGN_VALUE ** prm_values);
  extern SYSPRM_ERR sysprm_change_server_parameters (const SYSPRM_ASSIGN_VALUE * assignments);
  extern SYSPRM_ERR sysprm_obtain_server_parameters (SYSPRM_ASSIGN_VALUE ** prm_values_ptr);
  extern int sysprm_get_force_server_parameters (SYSPRM_ASSIGN_VALUE ** change_values);
  extern void sysprm_tune_client_parameters (void);
  extern void sysprm_free_session_parameters (SESSION_PARAM ** session_parameters);

#if !defined (CS_MODE)
  extern void xsysprm_change_server_parameters (const SYSPRM_ASSIGN_VALUE * assignments);
  extern void xsysprm_obtain_server_parameters (SYSPRM_ASSIGN_VALUE * prm_values);
  extern SYSPRM_ASSIGN_VALUE *xsysprm_get_force_server_parameters (void);
  extern void xsysprm_dump_server_parameters (FILE * fp);
#endif				/* !CS_MODE */

  extern int sysprm_set_force (const char *pname, const char *pvalue);
  extern int sysprm_set_to_default (const char *pname, bool set_to_force);
  extern int sysprm_check_range (const char *pname, void *value);
  extern int sysprm_get_range (const char *pname, void *min, void *max);
  extern int prm_get_master_port_id (void);
  extern bool prm_get_commit_on_shutdown (void);

  extern char *sysprm_pack_session_parameters (char *ptr, SESSION_PARAM * session_parameters);
  extern int sysprm_packed_session_parameters_length (SESSION_PARAM * session_parameters, int offset);
  extern char *sysprm_unpack_session_parameters (char *ptr, SESSION_PARAM ** session_parameters_ptr);
  extern char *sysprm_pack_assign_values (char *ptr, const SYSPRM_ASSIGN_VALUE * assign_values);
  extern int sysprm_packed_assign_values_length (const SYSPRM_ASSIGN_VALUE * assign_values, int offset);
  extern char *sysprm_unpack_assign_values (char *ptr, SYSPRM_ASSIGN_VALUE ** assign_values_ptr);
  extern void sysprm_free_assign_values (SYSPRM_ASSIGN_VALUE ** assign_values_ptr);
  extern void sysprm_change_parameter_values (const SYSPRM_ASSIGN_VALUE * assignments, bool check, bool set_flag);

#if defined (SERVER_MODE)
  extern int sysprm_session_init_session_parameters (SESSION_PARAM ** session_params, int *found_session_parameters);
#endif				/* SERVER_MODE */

#if defined (CS_MODE)
  extern void sysprm_update_client_session_parameters (SESSION_PARAM * session_parameters);
#endif				/* CS_MODE */

#if !defined (SERVER_MODE)
  extern char *sysprm_print_parameters_for_qry_string (void);
  extern char *sysprm_print_parameters_for_ha_repl (void);
  extern SYSPRM_ERR sysprm_validate_change_parameters (const char *data, bool check,
						       SYSPRM_ASSIGN_VALUE ** assignments_ptr);
  extern SYSPRM_ERR sysprm_make_default_values (const char *data, char *default_val_buf, const int buf_size);
  extern int sysprm_init_intl_param (void);
#endif				/* !SERVER_MODE */

  extern int sysprm_print_assign_values (SYSPRM_ASSIGN_VALUE * prm_values, char *buffer, int length);
  extern int sysprm_set_error (SYSPRM_ERR rc, const char *data);
  extern int sysprm_get_session_parameters_count (void);

#if defined (WINDOWS)
  /* FIXME!!! Segmentation fault when using inline function on Window. Temporarily, disable inline functions on Window. */
  extern int prm_get_integer_value (PARAM_ID prm_id);
  extern bool prm_get_bool_value (PARAM_ID prm_id);
  extern float prm_get_float_value (PARAM_ID prm_id);
  extern char *prm_get_string_value (PARAM_ID prm_id);
  extern int *prm_get_integer_list_value (PARAM_ID prm_id);
  extern UINT64 prm_get_bigint_value (PARAM_ID prm_id);
#else				/* window */
  STATIC_INLINE int prm_get_integer_value (PARAM_ID prm_id) __attribute__ ((ALWAYS_INLINE));
  STATIC_INLINE bool prm_get_bool_value (PARAM_ID prm_id) __attribute__ ((ALWAYS_INLINE));
  STATIC_INLINE float prm_get_float_value (PARAM_ID prm_id) __attribute__ ((ALWAYS_INLINE));
  STATIC_INLINE char *prm_get_string_value (PARAM_ID prm_id) __attribute__ ((ALWAYS_INLINE));
  STATIC_INLINE int *prm_get_integer_list_value (PARAM_ID prm_id) __attribute__ ((ALWAYS_INLINE));
  STATIC_INLINE UINT64 prm_get_bigint_value (PARAM_ID prm_id) __attribute__ ((ALWAYS_INLINE));

/*
 * prm_get_integer_value () - get the value of a parameter of type integer
 *
 * return      : value
 * prm_id (in) : parameter id
 *
 * NOTE: keywords are stored as integers
 */
  STATIC_INLINE int prm_get_integer_value (PARAM_ID prm_id)
  {
    assert (prm_id <= PRM_LAST_ID);
    assert (PRM_IS_INTEGER (&prm_Def[prm_id]) || PRM_IS_KEYWORD (&prm_Def[prm_id]));

#if defined (SERVER_MODE)
    if (!PRM_SERVER_SESSION (prm_id))
      {
	return PRM_GET_INT (prm_Def[prm_id].value);
      }

    return PRM_GET_INT (prm_get_value (prm_id));
#else				/* SERVER_MODE */
      return PRM_GET_INT (prm_Def[prm_id].value);
#endif				/* SERVER_MODE */
  }

/*
 * prm_get_bool_value () - get the value of a parameter of type bool
 *
 * return      : value
 * prm_id (in) : parameter id
 */
  STATIC_INLINE bool prm_get_bool_value (PARAM_ID prm_id)
  {
    assert (prm_id <= PRM_LAST_ID);
    assert (PRM_IS_BOOLEAN (&prm_Def[prm_id]));

#if defined (SERVER_MODE)
    if (!PRM_SERVER_SESSION (prm_id))
      {
	return PRM_GET_BOOL (prm_Def[prm_id].value);
      }

    return PRM_GET_BOOL (prm_get_value (prm_id));
#else /* SERVER_MODE */
    return PRM_GET_BOOL (prm_Def[prm_id].value);
#endif /* SERVER_MODE */
  }

/*
 * prm_get_float_value () - get the value of a parameter of type float
 *
 * return      : value
 * prm_id (in) : parameter id
 */
  STATIC_INLINE float prm_get_float_value (PARAM_ID prm_id)
  {
    assert (prm_id <= PRM_LAST_ID);
    assert (PRM_IS_FLOAT (&prm_Def[prm_id]));

#if defined (SERVER_MODE)
    if (!PRM_SERVER_SESSION (prm_id))
      {
	return PRM_GET_FLOAT (prm_Def[prm_id].value);
      }

    return PRM_GET_FLOAT (prm_get_value (prm_id));
#else /* SERVER_MODE */
    return PRM_GET_FLOAT (prm_Def[prm_id].value);
#endif /* SERVER_MODE */
  }

/*
 * prm_get_string_value () - get the value of a parameter of type string
 *
 * return      : value
 * prm_id (in) : parameter id
 */
  STATIC_INLINE char *prm_get_string_value (PARAM_ID prm_id)
  {
    assert (prm_id <= PRM_LAST_ID);
    assert (PRM_IS_STRING (&prm_Def[prm_id]));

#if defined (SERVER_MODE)
    if (!PRM_SERVER_SESSION (prm_id))
      {
	return PRM_GET_STRING (prm_Def[prm_id].value);
      }

    return PRM_GET_STRING (prm_get_value (prm_id));
#else /* SERVER_MODE */
    return PRM_GET_STRING (prm_Def[prm_id].value);
#endif /* SERVER_MODE */
  }

/*
 * prm_get_integer_list_value () - get the value of a parameter of type
 *				   integer list
 *
 * return      : value
 * prm_id (in) : parameter id
 */
  STATIC_INLINE int *prm_get_integer_list_value (PARAM_ID prm_id)
  {
    assert (prm_id <= PRM_LAST_ID);
    assert (PRM_IS_INTEGER_LIST (&prm_Def[prm_id]));

#if defined (SERVER_MODE)
    if (!PRM_SERVER_SESSION (prm_id))
      {
	return PRM_GET_INTEGER_LIST (prm_Def[prm_id].value);
      }

    return PRM_GET_INTEGER_LIST (prm_get_value (prm_id));
#else /* SERVER_MODE */
    return PRM_GET_INTEGER_LIST (prm_Def[prm_id].value);
#endif /* SERVER_MODE */
  }

/*
 * prm_get_bigint_value () - get the value of a parameter of type size
 *
 * return      : value
 * prm_id (in) : parameter id
 */
  STATIC_INLINE UINT64 prm_get_bigint_value (PARAM_ID prm_id)
  {
    assert (prm_id <= PRM_LAST_ID);
    assert (PRM_IS_BIGINT (&prm_Def[prm_id]));

#if defined (SERVER_MODE)
    if (!PRM_SERVER_SESSION (prm_id))
      {
	return PRM_GET_BIGINT (prm_Def[prm_id].value);
      }

    return PRM_GET_BIGINT (prm_get_value (prm_id));
#else /* SERVER_MODE */
    return PRM_GET_BIGINT (prm_Def[prm_id].value);
#endif /* SERVER_MODE */
  }

#endif /* window */

#ifdef __cplusplus
}
#endif



#endif /* _SYSTEM_PARAMETER_H_ */
