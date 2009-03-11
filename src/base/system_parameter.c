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

#include "porting.h"
#include "chartype.h"
#include "misc_string.h"
#include "error_manager.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "util_func.h"
#include "log_comm.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "intl_support.h"
#include "log_manager.h"
#include "message_catalog.h"
#include "language_support.h"
#if defined (SERVER_MODE)
#include "server_support.h"
#endif /* SERVER_MODE */
#ifdef LINUX
#include "stack_dump.h"
#endif
#include "ini_parser.h"




#define CONF_FILE_DIR   "conf"
#if defined (WINDOWS)
#define ERROR_FILE_DIR  "log\\server"
#else
#define ERROR_FILE_DIR  "log/server"
#endif
#define ER_LOG_SUFFIX   ".err"

#if !defined (CS_MODE)
static const char sysprm_error_log_file[] = "cub_server.err";
#else /* CS_MODE */
static const char sysprm_error_log_file[] = "cub_client.err";
#endif /* CS_MODE */
static const char sysprm_conf_file_name[] = "cubrid.conf";


/*
 * Bit masks for flag representing status words
 */

#define PRM_REQUIRED        0x00000001	/* Must be set with default or by user */
#define PRM_SET             0x00000002	/* has been set */
#define PRM_STRING          0x00000004	/* is string value */
#define PRM_INTEGER         0x00000008	/* is integer value */
#define PRM_FLOAT           0x00000010	/* is float value */
#define PRM_BOOLEAN         0x00000020	/* is boolean value */
#define PRM_ISOLATION_LEVEL 0x00000040	/* is isolation level value */
#define PRM_DEFAULT         0x00000080	/* has system default */
#define PRM_USER_CHANGE     0x00000100	/* user can change, not implemented */
#define PRM_ALLOCATED       0x00000200	/* storage has been malloc'd */
#define PRM_LOWER_LIMIT     0x00000400	/* has lower limit */
#define PRM_UPPER_LIMIT     0x00000800	/* has upper limit */
#define PRM_DEFAULT_USED    0x00001000	/* Default value has been used */
#define PRM_FOR_CLIENT      0x00002000	/* is for client parameter */
#define PRM_FOR_SERVER      0x00004000	/* is for server parameter */
#define PRM_STACK_DUMP_ENABLE  0x00010000	/* call stack dump enabled */
#define PRM_STACK_DUMP_DISABLE 0x00020000	/* call stack dump diabled */

/*
 * Macros to access bit fields
 */

#define PRM_IS_REQUIRED(x)        (x & PRM_REQUIRED)
#define PRM_IS_SET(x)             (x & PRM_SET)
#define PRM_IS_STRING(x)          (x & PRM_STRING)
#define PRM_IS_INTEGER(x)         (x & PRM_INTEGER)
#define PRM_IS_FLOAT(x)           (x & PRM_FLOAT)
#define PRM_IS_BOOLEAN(x)         (x & PRM_BOOLEAN)
#define PRM_IS_ISOLATION_LEVEL(x) (x & PRM_ISOLATION_LEVEL)
#define PRM_HAS_DEFAULT(x)        (x & PRM_DEFAULT)
#define PRM_USER_CAN_CHANGE(x)    (x & PRM_USER_CHANGE)
#define PRM_IS_ALLOCATED(x)       (x & PRM_ALLOCATED)
#define PRM_HAS_LOWER_LIMIT(x)    (x & PRM_LOWER_LIMIT)
#define PRM_HAS_UPPER_LIMIT(x)    (x & PRM_UPPER_LIMIT)
#define PRM_DEFAULT_VAL_USED(x)   (x & PRM_DEFAULT_USED)
#define PRM_IS_FOR_CLIENT(x)      (x & PRM_FOR_CLIENT)
#define PRM_IS_FOR_SERVER(x)      (x & PRM_FOR_SERVER)
#define PRM_IS_STACK_DUMP_ENABLE(x)  (x & PRM_STACK_DUMP_ENABLE)
#define PRM_IS_STACK_DUMP_DISABLE(x) (x & PRM_STACK_DUMP_DISABLE)

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


/*
 * Global variables of parameters' value
 * Default values for the parameters
 * Upper and lower bounds for the parameters
 */
int PRM_ER_EXIT_ASK = ER_EXIT_DEFAULT;
static int prm_er_exit_ask_default = ER_EXIT_DEFAULT;

int PRM_ER_LOG_SIZE = INT_MIN;
static int prm_er_log_size_default = (100000 * 80L);
static int prm_er_log_size_lower = (100 * 80);

const char *PRM_ER_LOG_FILE = sysprm_error_log_file;
static const char *prm_er_log_file_default = sysprm_error_log_file;

bool PRM_IO_LOCKF_ENABLE = false;
static bool prm_io_lockf_enable_default = true;

int PRM_SR_NBUFFERS = INT_MIN;
static int prm_sr_nbuffers_default = 16;
static int prm_sr_nbuffers_lower = 1;

int PRM_PB_NBUFFERS = INT_MIN;
static int prm_pb_nbuffers_default = 25000;
static int prm_pb_nbuffers_lower = 1;

float PRM_HF_UNFILL_FACTOR = FLT_MIN;
static float prm_hf_unfill_factor_default = 0.10;
static float prm_hf_unfill_factor_lower = 0.0;
static float prm_hf_unfill_factor_upper = 0.3;

float PRM_BT_UNFILL_FACTOR = FLT_MIN;
static float prm_bt_unfill_factor_default = 0.20;
static float prm_bt_unfill_factor_lower = 0.0;
static float prm_bt_unfill_factor_upper = 0.35;

int PRM_BT_OID_NBUFFERS = INT_MIN;
static int prm_bt_oid_nbuffers_default = 4;
static int prm_bt_oid_nbuffers_lower = 1;
static int prm_bt_oid_nbuffers_upper = 16;

bool PRM_BT_INDEX_SCAN_OID_ORDER = false;
static bool prm_bt_index_scan_oid_order_default = false;

int PRM_BOSR_MAXTMP_PAGES = INT_MIN;
static int prm_bosr_maxtmp_pages = -1;	/* Infinite */

int PRM_LK_TIMEOUT_MESSAGE_DUMP_LEVEL = INT_MIN;
static int prm_lk_timeout_message_dump_level_default = 0;
static int prm_lk_timeout_message_dump_level_lower = 0;
static int prm_lk_timeout_message_dump_level_upper = 2;

int PRM_LK_ESCALATION_AT = INT_MIN;
static int prm_lk_escalation_at_default = 100000;
static int prm_lk_escalation_at_lower = 5;

int PRM_LK_TIMEOUT_SECS = INT_MIN;
static int prm_lk_timeout_secs_default = -1;	/* Infinite */
static int prm_lk_timeout_secs_lower = -1;

int PRM_LK_RUN_DEADLOCK_INTERVAL = INT_MIN;
static int prm_lk_run_deadlock_interval_default = 1;
static int prm_lk_run_deadlock_interval_lower = 1;

int PRM_LOG_NBUFFERS = INT_MIN;
static int prm_log_nbuffers_default = 50;
static int prm_log_nbuffers_lower = 3;

int PRM_LOG_CHECKPOINT_NPAGES = INT_MIN;
static int prm_log_checkpoint_npages_default = 10000;
static int prm_log_checkpoint_npages_lower = 10;

int PRM_LOG_CHECKPOINT_INTERVAL_MINUTES = INT_MIN;
static int prm_log_checkpoint_interval_minutes_default = 30;
static int prm_log_checkpoint_interval_minutes_lower = 1;

int PRM_LOG_ISOLATION_LEVEL = INT_MIN;
static int prm_log_isolation_level_default = TRAN_REP_CLASS_COMMIT_INSTANCE;
static int prm_log_isolation_level_lower =
  TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
static int prm_log_isolation_level_upper = TRAN_SERIALIZABLE;

bool PRM_LOG_MEDIA_FAILURE_SUPPORT = false;
static bool prm_log_media_failure_support_default = true;

bool PRM_COMMIT_ON_SHUTDOWN = false;
static bool prm_commit_on_shutdown_default = false;

bool PRM_CSQL_AUTO_COMMIT = false;
static bool prm_csql_auto_commit_default = true;

bool PRM_LOG_SWEEP_CLEAN = false;
static bool prm_log_sweep_clean_default = false;

int PRM_WS_HASHTABLE_SIZE = INT_MIN;
static int prm_ws_hashtable_size_default = 4096;
static int prm_ws_hashtable_size_lower = 1024;

bool PRM_WS_MEMORY_REPORT = false;
static bool prm_ws_memory_report_default = false;

bool PRM_GC_ENABLE = INT_MIN;
static bool prm_gc_enable_default = false;

int PRM_TCP_PORT_ID = INT_MIN;
static int prm_tcp_port_id_default = 1523;

int PRM_TCP_CONNECTION_TIMEOUT = INT_MIN;
static int prm_tcp_connection_timeout_default = 2;
static int prm_tcp_connection_timeout_lower = 1;

int PRM_OPTIMIZATION_LEVEL = INT_MIN;
static int prm_optimization_level_default = 1;

bool PRM_QO_DUMP = INT_MIN;
static bool prm_qo_dump_default = false;

int PRM_CSS_MAX_CLIENTS = INT_MIN;
static int prm_css_max_clients_default = 50;
static int prm_css_max_clients_lower = 10;

int PRM_MAX_THREADS = INT_MIN;
static int prm_max_threads_default = 100;
static int prm_max_threads_lower = 2;

int PRM_THREAD_STACKSIZE = INT_MIN;
static int prm_thread_stacksize_default = (100 * 1024);
static int prm_thread_stacksize_lower = 64 * 1024;

const char *PRM_CFG_DB_HOSTS = "";
static const char *prm_cfg_db_hosts_default = NULL;

int PRM_RESET_TR_PARSER = INT_MIN;
static int prm_reset_tr_parser_default = 10;

int PRM_IO_BACKUP_NBUFFERS = INT_MIN;
static int prm_io_backup_nbuffers_default = 256;
static int prm_io_backup_nbuffers_lower = 256;

int PRM_IO_BACKUP_MAX_VOLUME_SIZE = INT_MIN;
static int prm_io_backup_max_volume_size_default = -1;
static int prm_io_backup_max_volume_size_lower = 1024 * 32;

int PRM_MAX_PAGES_IN_TEMP_FILE_CACHE = INT_MIN;
static int prm_max_pages_in_temp_file_cache_default = 1000;	/* pages */
static int prm_max_pages_in_temp_file_cache_lower = 100;

int PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE = INT_MIN;
static int prm_max_entries_in_temp_file_cache_default = 512;
static int prm_max_entries_in_temp_file_cache_lower = 10;

bool PRM_PTHREAD_SCOPE_PROCESS = INT_MIN;
static bool prm_pthread_scope_process_default = true;

int PRM_TEMP_MEM_BUFFER_PAGES = INT_MIN;
static int prm_temp_mem_buffer_pages_default = 4;
static int prm_temp_mem_buffer_pages_lower = 0;
static int prm_temp_mem_buffer_pages_upper = 20;

bool PRM_DONT_REUSE_HEAP_FILE = false;
static bool prm_dont_reuse_heap_file_default = false;

bool PRM_QUERY_MODE_SYNC = false;
static bool prm_query_mode_sync_default = false;

int PRM_INSERT_MODE = INT_MIN;
static int prm_insert_mode_default = 1;
static int prm_insert_mode_lower = 1;
static int prm_insert_mode_upper = 7;

int PRM_LK_MAX_SCANID_BIT = INT_MIN;
static int prm_lk_max_scanid_bit_default = 32;
static int prm_lk_max_scanid_bit_lower = 32;
static int prm_lk_max_scanid_bit_upper = 128;

bool PRM_HOSTVAR_LATE_BINDING = false;
static bool prm_hostvar_late_binding_default = false;

bool PRM_ENABLE_HISTO = false;
static bool prm_enable_histo_default = false;

int PRM_MUTEX_BUSY_WAITING_CNT = INT_MIN;
static int prm_mutex_busy_waiting_cnt_default = 0;

int PRM_PB_NUM_LRU_CHAINS = INT_MIN;
static int prm_pb_num_LRU_chains_default = 0;	/* system define */
static int prm_pb_num_LRU_chains_lower = 0;
static int prm_pb_num_LRU_chains_upper = 1000;

bool PRM_ORACLE_STYLE_OUTERJOIN = false;
static bool prm_oracle_style_outerjoin_default = false;

int PRM_COMPACTDB_PAGE_RECLAIM_ONLY = INT_MIN;
static int prm_compactdb_page_reclaim_only_default = 0;

float PRM_LIKE_TERM_SELECTIVITY = 0;
static float prm_like_term_selectivity_default = 0.1;
static float prm_like_term_selectivity_upper = 1.0;
static float prm_like_term_selectivity_lower = 0.0;

int PRM_MAX_OUTER_CARD_OF_IDXJOIN = INT_MIN;
static int prm_max_outer_card_of_idxjoin_default = 0;
static int prm_max_outer_card_of_idxjoin_lower = 0;

bool PRM_ORACLE_STYLE_EMPTY_STRING = false;
static bool prm_oracle_style_empty_string_default = false;

bool PRM_SUPPRESS_FSYNC = false;
static bool prm_suppress_fsync_default = false;

bool PRM_CALL_STACK_DUMP_ON_ERROR = false;
static bool prm_call_stack_dump_on_error_default = false;

const char *PRM_CALL_STACK_DUMP_ACTIVATION = "";
static const char *prm_call_stack_dump_activation_default = NULL;

const char *PRM_CALL_STACK_DUMP_DEACTIVATION = "";
static const char *prm_call_stack_dump_deactivation_default = NULL;

bool PRM_CALL_STACK_DUMP_ACTIVE_ERRORS[1024];

bool PRM_CALL_STACK_DUMP_DEACTIVE_ERRORS[1024];

bool PRM_COMPAT_NUMERIC_DIVISION_SCALE = false;
static bool prm_compat_numeric_division_scale_default = false;

bool PRM_DBFILES_PROTECT = false;
static bool prm_dbfiles_protect_default = false;

bool PRM_AUTO_RESTART_SERVER = false;
static bool prm_auto_restart_server_default = true;

int PRM_XASL_MAX_PLAN_CACHE_ENTRIES = INT_MIN;
static int prm_xasl_max_plan_cache_entries_default = 1000;

int PRM_XASL_MAX_PLAN_CACHE_CLONES = INT_MIN;
static int prm_xasl_max_plan_cache_clones_default = -1;	/* disabled */

int PRM_XASL_PLAN_CACHE_TIMEOUT = INT_MIN;
static int prm_xasl_plan_cache_timeout_default = -1;	/* infinity */

int PRM_LIST_QUERY_CACHE_MODE = INT_MIN;
static int prm_list_query_cache_mode_default = 0;	/* disabled */
static int prm_list_query_cache_mode_upper = 2;
static int prm_list_query_cache_mode_lower = 0;

int PRM_LIST_MAX_QUERY_CACHE_ENTRIES = INT_MIN;
static int prm_list_max_query_cache_entries_default = -1;	/* disabled */

int PRM_LIST_MAX_QUERY_CACHE_PAGES = INT_MIN;
static int prm_list_max_query_cache_pages_default = -1;	/* infinity */

bool PRM_REPLICATION_MODE = false;
static bool prm_replication_mode_default = false;

bool PRM_JAVA_STORED_PROCEDURE = false;
static bool prm_java_stored_procedure_default = false;

bool PRM_COMPAT_PRIMARY_KEY = false;
static bool prm_compat_primary_key_default = false;

int PRM_LOG_HEADER_FLUSH_INTERVAL = INT_MIN;
static int prm_log_header_flush_interval_default = 5;
static int prm_log_header_flush_interval_lower = 1;

bool PRM_LOG_ASYNC_COMMIT = false;
static bool prm_log_async_commit_default = false;

int PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS = INT_MIN;
static int prm_log_group_commit_interval_msecs_default = 0;
static int prm_log_group_commit_interval_msecs_lower = 0;

int PRM_LOG_BG_FLUSH_INTERVAL_MSECS = INT_MIN;
static int prm_log_bg_flush_interval_msecs_default = 50;
static int prm_log_bg_flush_interval_msecs_lower = 0;

int PRM_LOG_BG_FLUSH_NUM_PAGES = INT_MIN;
static int prm_log_bg_flush_num_pages_default = 0;
static int prm_log_bg_flush_num_pages_lower = 0;

bool PRM_BLOCK_DDL_STATEMENT = false;
static bool prm_block_ddl_statement_default = false;

bool PRM_BLOCK_NOWHERE_STATEMENT = false;
static bool prm_block_nowhere_statement_default = false;

bool PRM_INTL_MBS_SUPPORT = false;
static bool prm_intl_mbs_support_default = false;

bool PRM_LOG_COMPRESS = false;
static bool prm_log_compress_default = true;

bool PRM_SINGLE_BYTE_COMPARE = false;
static bool prm_single_byte_compare_default = false;

int PRM_CSQL_HISTORY_NUM = INT_MIN;
static int prm_csql_history_num_default = 50;
static int prm_csql_history_num_upper = 200;
static int prm_csql_history_num_lower = 1;

bool PRM_LOG_TRACE_DEBUG = false;
static bool prm_log_trace_debug_default = false;

const char *PRM_DL_FORK = "";
static const char *prm_dl_fork_default = NULL;

bool PRM_ER_LOG_WARNING = false;
static bool prm_er_log_warning_default = false;

bool PRM_ER_PRODUCTION_MODE = false;
static bool prm_er_production_mode_default = true;

int PRM_ER_STOP_ON_ERROR = INT_MIN;
static int prm_er_stop_on_error_default = 0;
static int prm_er_stop_on_error_upper = 0;

int PRM_TCP_RCVBUF_SIZE = INT_MIN;
static int prm_tcp_rcvbuf_size_default = -1;

int PRM_TCP_SNDBUF_SIZE = INT_MIN;
static int prm_tcp_sndbuf_size_default = -1;

int PRM_TCP_NODELAY = INT_MIN;
static int prm_tcp_nodelay_default = -1;

bool PRM_CSQL_SINGLE_LINE_MODE = false;
static bool prm_csql_single_line_mode_default = false;

bool PRM_XASL_DEBUG_DUMP = false;
static bool prm_xasl_debug_dump_default = false;

int PRM_LOG_MAX_ARCHIVES = INT_MIN;
static int prm_log_max_archives_default = INT_MAX;
static int prm_log_max_archives_lower = 0;

bool PRM_LOG_NO_LOGGING = false;
static bool prm_log_no_logging_default = false;

bool PRM_UNLOADDB_IGNORE_ERROR = false;
static bool prm_unloaddb_ignore_error_default = false;

int PRM_UNLOADDB_LOCK_TIMEOUT = INT_MIN;
static int prm_unloaddb_lock_timeout_default = -1;
static int prm_unloaddb_lock_timeout_lower = -1;

int PRM_LOADDB_FLUSH_INTERVAL = INT_MIN;
static int prm_loaddb_flush_interval_default = 1000;
static int prm_loaddb_flush_interval_lower = 0;

const char *PRM_IO_TEMP_VOLUME_PATH = "";
static char *prm_io_temp_volume_path_default = NULL;

const char *PRM_IO_VOLUME_EXT_PATH = "";
static char *prm_io_volume_ext_path_default = NULL;

bool PRM_UNIQUE_ERROR_KEY_VALUE = false;
static bool prm_unique_error_key_value_default = false;

const char *PRM_SERVICE_SERVICE_LIST = "";
static const char *prm_service_service_list_default = NULL;

const char *PRM_SERVICE_SERVER_LIST = "";
static const char *prm_service_server_list_default = NULL;



typedef struct sysprm_param SYSPRM_PARAM;
struct sysprm_param
{
  const char *name;		/* the keyword expected */
  unsigned int flag;		/* bitmask flag representing status words */
  void *default_value;		/* address of (pointer to) default value */
  void *value;			/* address of (pointer to) current value */
  void *upper_limit;		/* highest allowable value */
  void *lower_limit;		/* lowest allowable value */
  char *force_value;		/* address of (pointer to) force value string */
};

static SYSPRM_PARAM prm_Def[] = {
  {"inquire_on_exit",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER),
   (void *) &prm_er_exit_ask_default,
   (void *) &PRM_ER_EXIT_ASK,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"error_log_size",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_FOR_SERVER),
   (void *) &prm_er_log_size_default,
   (void *) &PRM_ER_LOG_SIZE,
   (void *) NULL, (void *) &prm_er_log_size_lower,
   (char *) NULL},
  {"error_log",
   (PRM_REQUIRED | PRM_STRING | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER),
   (void *) &prm_er_log_file_default,
   (void *) &PRM_ER_LOG_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"file_lock",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_io_lockf_enable_default,
   (void *) &PRM_IO_LOCKF_ENABLE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"sort_buffer_pages",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_sr_nbuffers_default,
   (void *) &PRM_SR_NBUFFERS,
   (void *) NULL, (void *) &prm_sr_nbuffers_lower,
   (char *) NULL},
  {"data_buffer_pages",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_pb_nbuffers_default,
   (void *) &PRM_PB_NBUFFERS,
   (void *) NULL, (void *) &prm_pb_nbuffers_lower,
   (char *) NULL},
  {"unfill_factor",
   (PRM_REQUIRED | PRM_FLOAT | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_hf_unfill_factor_default,
   (void *) &PRM_HF_UNFILL_FACTOR,
   (void *) &prm_hf_unfill_factor_upper, (void *) &prm_hf_unfill_factor_lower,
   (char *) NULL},
  {"index_unfill_factor",
   (PRM_REQUIRED | PRM_FLOAT | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_bt_unfill_factor_default,
   (void *) &PRM_BT_UNFILL_FACTOR,
   (void *) &prm_bt_unfill_factor_upper, (void *) &prm_bt_unfill_factor_lower,
   (char *) NULL},
  {"index_scan_oid_buffer_pages",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_bt_oid_nbuffers_default,
   (void *) &PRM_BT_OID_NBUFFERS,
   (void *) &prm_bt_oid_nbuffers_upper, (void *) &prm_bt_oid_nbuffers_lower,
   (char *) NULL},
  {"index_scan_in_oid_order",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_bt_index_scan_oid_order_default,
   (void *) &PRM_BT_INDEX_SCAN_OID_ORDER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"temp_file_max_size_in_pages",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_bosr_maxtmp_pages,
   (void *) &PRM_BOSR_MAXTMP_PAGES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"lock_timeout_message_type",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_lk_timeout_message_dump_level_default,
   (void *) &PRM_LK_TIMEOUT_MESSAGE_DUMP_LEVEL,
   (void *) &prm_lk_timeout_message_dump_level_upper,
   (void *) &prm_lk_timeout_message_dump_level_lower,
   (char *) NULL},
  {"lock_escalation",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_lk_escalation_at_default,
   (void *) &PRM_LK_ESCALATION_AT,
   (void *) NULL, (void *) &prm_lk_escalation_at_lower,
   (char *) NULL},
  {"lock_timeout_in_secs",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_lk_timeout_secs_default,
   (void *) &PRM_LK_TIMEOUT_SECS,
   (void *) NULL, (void *) &prm_lk_timeout_secs_lower,
   (char *) NULL},
  {"deadlock_detection_interval_in_secs",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_lk_run_deadlock_interval_default,
   (void *) &PRM_LK_RUN_DEADLOCK_INTERVAL,
   (void *) NULL, (void *) &prm_lk_run_deadlock_interval_lower,
   (char *) NULL},
  {"log_buffer_pages",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_log_nbuffers_default,
   (void *) &PRM_LOG_NBUFFERS,
   (void *) NULL, (void *) &prm_log_nbuffers_lower,
   (char *) NULL},
  {"checkpoint_every_npages",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_log_checkpoint_npages_default,
   (void *) &PRM_LOG_CHECKPOINT_NPAGES,
   (void *) NULL, (void *) &prm_log_checkpoint_npages_lower,
   (char *) NULL},
  {"checkpoint_interval_in_mins",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_log_checkpoint_interval_minutes_default,
   (void *) &PRM_LOG_CHECKPOINT_INTERVAL_MINUTES,
   (void *) NULL, (void *) &prm_log_checkpoint_interval_minutes_lower,
   (char *) NULL},
  {"isolation_level",
   (PRM_REQUIRED | PRM_ISOLATION_LEVEL | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_log_isolation_level_default,
   (void *) &PRM_LOG_ISOLATION_LEVEL,
   (void *) &prm_log_isolation_level_upper,
   (void *) &prm_log_isolation_level_lower,
   (char *) NULL},
  {"media_failure_support",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_log_media_failure_support_default,
   (void *) &PRM_LOG_MEDIA_FAILURE_SUPPORT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"commit_on_shutdown",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_commit_on_shutdown_default,
   (void *) &PRM_COMMIT_ON_SHUTDOWN,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"csql_auto_commit",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_csql_auto_commit_default,
   (void *) &PRM_CSQL_AUTO_COMMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"log_file_sweep_clean",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_log_sweep_clean_default,
   (void *) &PRM_LOG_SWEEP_CLEAN,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"initial_workspace_table_size",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_ws_hashtable_size_default,
   (void *) &PRM_WS_HASHTABLE_SIZE,
   (void *) NULL, (void *) &prm_ws_hashtable_size_lower,
   (char *) NULL},
  {"workspace_memory_report",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_ws_memory_report_default,
   (void *) &PRM_WS_MEMORY_REPORT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"garbage_collection",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_gc_enable_default,
   (void *) &PRM_GC_ENABLE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"cubrid_port_id",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER),
   (void *) &prm_tcp_port_id_default,
   (void *) &PRM_TCP_PORT_ID,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"connection_timeout",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_tcp_connection_timeout_default,
   (void *) &PRM_TCP_CONNECTION_TIMEOUT,
   (void *) NULL, (void *) &prm_tcp_connection_timeout_lower,
   (char *) NULL},
  {"optimization_level",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_optimization_level_default,
   (void *) &PRM_OPTIMIZATION_LEVEL,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"qo_dump",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_qo_dump_default,
   (void *) &PRM_QO_DUMP,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"max_clients",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_css_max_clients_default,
   (void *) &PRM_CSS_MAX_CLIENTS,
   (void *) NULL, (void *) &prm_css_max_clients_lower,
   (char *) NULL},
  {"max_threads",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_max_threads_default,
   (void *) &PRM_MAX_THREADS,
   (void *) NULL, (void *) &prm_max_threads_lower,
   (char *) NULL},
  {"thread_stacksize",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_thread_stacksize_default,
   (void *) &PRM_THREAD_STACKSIZE,
   (void *) NULL, (void *) &prm_thread_stacksize_lower,
   (char *) NULL},
  {"db_hosts",
   (PRM_REQUIRED | PRM_STRING | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_cfg_db_hosts_default,
   (void *) &PRM_CFG_DB_HOSTS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"reset_tr_parser_interval",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_reset_tr_parser_default,
   (void *) &PRM_RESET_TR_PARSER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"backup_buffer_pages",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_io_backup_nbuffers_default,
   (void *) &PRM_IO_BACKUP_NBUFFERS,
   (void *) NULL, (void *) &prm_io_backup_nbuffers_lower,
   (char *) NULL},
  {"backup_volume_max_size_bytes",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_io_backup_max_volume_size_default,
   (void *) &PRM_IO_BACKUP_MAX_VOLUME_SIZE,
   (void *) NULL, (void *) &prm_io_backup_max_volume_size_lower,
   (char *) NULL},
  {"max_pages_in_temp_file_cache",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_max_pages_in_temp_file_cache_default,
   (void *) &PRM_MAX_PAGES_IN_TEMP_FILE_CACHE,
   (void *) NULL, (void *) &prm_max_pages_in_temp_file_cache_lower,
   (char *) NULL},
  {"max_entries_in_temp_file_cache",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_max_entries_in_temp_file_cache_default,
   (void *) &PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE,
   (void *) NULL, (void *) &prm_max_entries_in_temp_file_cache_lower,
   (char *) NULL},
  {"pthread_scope_process",	/* AIX only */
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_pthread_scope_process_default,
   (void *) &PRM_PTHREAD_SCOPE_PROCESS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"temp_file_memory_size_in_pages",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_temp_mem_buffer_pages_default,
   (void *) &PRM_TEMP_MEM_BUFFER_PAGES,
   (void *) &prm_temp_mem_buffer_pages_upper,
   (void *) &prm_temp_mem_buffer_pages_lower,
   (char *) NULL},
  {"dont_reuse_heap_file",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_dont_reuse_heap_file_default,
   (void *) &PRM_DONT_REUSE_HEAP_FILE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"dont_use_async_query",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_query_mode_sync_default,
   (void *) &PRM_QUERY_MODE_SYNC,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"insert_execution_mode",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_insert_mode_default,
   (void *) &PRM_INSERT_MODE,
   (void *) &prm_insert_mode_upper,
   (void *) &prm_insert_mode_lower,
   (char *) NULL},
  {"max_index_scan_count",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_lk_max_scanid_bit_default,
   (void *) &PRM_LK_MAX_SCANID_BIT,
   (void *) &prm_lk_max_scanid_bit_upper,
   (void *) &prm_lk_max_scanid_bit_lower,
   (char *) NULL},
  {"hostvar_late_binding",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_hostvar_late_binding_default,
   (void *) &PRM_HOSTVAR_LATE_BINDING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"communication_histogram",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_enable_histo_default,
   (void *) &PRM_ENABLE_HISTO,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"mutex_busy_waiting_cnt",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_mutex_busy_waiting_cnt_default,
   (void *) &PRM_MUTEX_BUSY_WAITING_CNT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"num_LRU_chains",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER),
   (void *) &prm_pb_num_LRU_chains_default,
   (void *) &PRM_PB_NUM_LRU_CHAINS,
   (void *) &prm_pb_num_LRU_chains_upper,
   (void *) &prm_pb_num_LRU_chains_lower,
   (char *) NULL},
  {"oracle_style_outerjoin",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_oracle_style_outerjoin_default,
   (void *) &PRM_ORACLE_STYLE_OUTERJOIN,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"compactdb_page_reclaim_only",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_USER_CHANGE),
   (void *) &prm_compactdb_page_reclaim_only_default,
   (void *) &PRM_COMPACTDB_PAGE_RECLAIM_ONLY,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"like_term_selectivity",
   (PRM_FLOAT | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_like_term_selectivity_default,
   (void *) &PRM_LIKE_TERM_SELECTIVITY,
   (void *) &prm_like_term_selectivity_upper,
   (void *) &prm_like_term_selectivity_lower,
   (char *) NULL},
  {"max_outer_card_of_idxjoin",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_max_outer_card_of_idxjoin_default,
   (void *) &PRM_MAX_OUTER_CARD_OF_IDXJOIN,
   (void *) NULL,
   (void *) &prm_max_outer_card_of_idxjoin_lower,
   (char *) NULL},
  {"oracle_style_empty_string",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER),
   (void *) &prm_oracle_style_empty_string_default,
   (void *) &PRM_ORACLE_STYLE_EMPTY_STRING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"suppress_fsync",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_suppress_fsync_default,
   (void *) &PRM_SUPPRESS_FSYNC,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"call_stack_dump_on_error",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_call_stack_dump_on_error_default,
   (void *) &PRM_CALL_STACK_DUMP_ON_ERROR,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"call_stack_dump_activation_list",
   (PRM_REQUIRED | PRM_STRING | PRM_STACK_DUMP_ENABLE | PRM_DEFAULT |
    PRM_USER_CHANGE),
   (void *) &prm_call_stack_dump_activation_default,
   (void *) &PRM_CALL_STACK_DUMP_ACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"call_stack_dump_deactivation_list",
   (PRM_REQUIRED | PRM_STRING | PRM_STACK_DUMP_DISABLE | PRM_DEFAULT |
    PRM_USER_CHANGE),
   (void *) &prm_call_stack_dump_deactivation_default,
   (void *) &PRM_CALL_STACK_DUMP_DEACTIVATION,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"compat_numeric_division_scale",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_compat_numeric_division_scale_default,
   (void *) &PRM_COMPAT_NUMERIC_DIVISION_SCALE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"dbfiles_protect",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_dbfiles_protect_default,
   (void *) &PRM_DBFILES_PROTECT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"auto_restart_server",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_auto_restart_server_default,
   (void *) &PRM_AUTO_RESTART_SERVER,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"max_plan_cache_entries",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_xasl_max_plan_cache_entries_default,
   (void *) &PRM_XASL_MAX_PLAN_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"max_plan_cache_clones",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_xasl_max_plan_cache_clones_default,
   (void *) &PRM_XASL_MAX_PLAN_CACHE_CLONES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"plan_cache_timeout",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_xasl_plan_cache_timeout_default,
   (void *) &PRM_XASL_PLAN_CACHE_TIMEOUT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"query_cache_mode",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_list_query_cache_mode_default,
   (void *) &PRM_LIST_QUERY_CACHE_MODE,
   (void *) &prm_list_query_cache_mode_upper,
   (void *) &prm_list_query_cache_mode_lower,
   (char *) NULL},
  {"max_query_cache_entries",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_list_max_query_cache_entries_default,
   (void *) &PRM_LIST_MAX_QUERY_CACHE_ENTRIES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"query_cache_size_in_pages",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_list_max_query_cache_pages_default,
   (void *) &PRM_LIST_MAX_QUERY_CACHE_PAGES,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"replication",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_replication_mode_default,
   (void *) &PRM_REPLICATION_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"java_stored_procedure",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_java_stored_procedure_default,
   (void *) &PRM_JAVA_STORED_PROCEDURE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"compat_primary_key",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_compat_primary_key_default,
   (void *) &PRM_COMPAT_PRIMARY_KEY,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"log_header_flush_interval_in_secs",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_log_header_flush_interval_default,
   (void *) &PRM_LOG_HEADER_FLUSH_INTERVAL,
   (void *) NULL, (void *) &prm_log_header_flush_interval_lower,
   (char *) NULL},
  {"async_commit",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_log_async_commit_default,
   (void *) &PRM_LOG_ASYNC_COMMIT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"group_commit_interval_in_msecs",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_log_group_commit_interval_msecs_default,
   (void *) &PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS,
   (void *) NULL, (void *) &prm_log_group_commit_interval_msecs_lower,
   (char *) NULL},
  {"log_flush_interval_in_msecs",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_log_bg_flush_interval_msecs_default,
   (void *) &PRM_LOG_BG_FLUSH_INTERVAL_MSECS,
   (void *) NULL, (void *) &prm_log_bg_flush_interval_msecs_lower,
   (char *) NULL},
  {"log_flush_every_npages",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_log_bg_flush_num_pages_default,
   (void *) &PRM_LOG_BG_FLUSH_NUM_PAGES,
   (void *) NULL, (void *) &prm_log_bg_flush_num_pages_lower,
   (char *) NULL},
  {"block_ddl_statement",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_block_ddl_statement_default,
   (void *) &PRM_BLOCK_DDL_STATEMENT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"block_nowhere_statement",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_block_nowhere_statement_default,
   (void *) &PRM_BLOCK_NOWHERE_STATEMENT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"intl_mbs_support",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT),
   (void *) &prm_intl_mbs_support_default,
   (void *) &PRM_INTL_MBS_SUPPORT,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"log_compress",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_log_compress_default,
   (void *) &PRM_LOG_COMPRESS,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"single_byte_compare",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_single_byte_compare_default,
   (void *) &PRM_SINGLE_BYTE_COMPARE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"csql_history_num",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_csql_history_num_default,
   (void *) &PRM_CSQL_HISTORY_NUM,
   (void *) &prm_csql_history_num_upper,
   (void *) &prm_csql_history_num_lower,
   (char *) NULL},
  {"log_trace_debug",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_log_trace_debug_default,
   (void *) &PRM_LOG_TRACE_DEBUG,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"dl_fork",
   (PRM_STRING | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_dl_fork_default,
   (void *) &PRM_DL_FORK,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"error_log_warning",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_er_log_warning_default,
   (void *) &PRM_ER_LOG_WARNING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"error_log_production_mode",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_er_production_mode_default,
   (void *) &PRM_ER_PRODUCTION_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"stop_on_error",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_er_stop_on_error_default,
   (void *) &PRM_ER_STOP_ON_ERROR,
   (void *) &prm_er_stop_on_error_upper, (void *) NULL,
   (char *) NULL},
  {"tcp_rcvbuf_size",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_tcp_rcvbuf_size_default,
   (void *) &PRM_TCP_RCVBUF_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"tcp_sndbuf_size",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_tcp_sndbuf_size_default,
   (void *) &PRM_TCP_SNDBUF_SIZE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"tcp_nodealy",
   (PRM_INTEGER | PRM_DEFAULT | PRM_FOR_SERVER | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_tcp_nodelay_default,
   (void *) &PRM_TCP_NODELAY,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"csql_single_line_mode",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_csql_single_line_mode_default,
   (void *) &PRM_CSQL_SINGLE_LINE_MODE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"xasl_debug_dump",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_CLIENT | PRM_USER_CHANGE),
   (void *) &prm_xasl_debug_dump_default,
   (void *) &PRM_XASL_DEBUG_DUMP,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"log_max_archives",
   (PRM_REQUIRED | PRM_INTEGER | PRM_DEFAULT | PRM_FOR_CLIENT |
    PRM_USER_CHANGE),
   (void *) &prm_log_max_archives_default,
   (void *) &PRM_LOG_MAX_ARCHIVES,
   (void *) NULL, (void *) &prm_log_max_archives_lower,
   (char *) NULL},
  {"no_logging",
   (PRM_REQUIRED | PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_log_no_logging_default,
   (void *) &PRM_LOG_NO_LOGGING,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"unload_ignore_error",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_USER_CHANGE),
   (void *) &prm_unloaddb_ignore_error_default,
   (void *) &PRM_UNLOADDB_IGNORE_ERROR,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"unload_lock_timeout",
   (PRM_INTEGER | PRM_DEFAULT | PRM_USER_CHANGE),
   (void *) &prm_unloaddb_lock_timeout_default,
   (void *) &PRM_UNLOADDB_LOCK_TIMEOUT,
   (void *) NULL, (void *) &prm_unloaddb_lock_timeout_lower,
   (char *) NULL},
  {"load_flush_interval",
   (PRM_INTEGER | PRM_DEFAULT | PRM_USER_CHANGE),
   (void *) &prm_loaddb_flush_interval_default,
   (void *) &PRM_LOADDB_FLUSH_INTERVAL,
   (void *) NULL, (void *) &prm_loaddb_flush_interval_lower,
   (char *) NULL},
  {"temp_volume_path",
   (PRM_REQUIRED | PRM_STRING | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_io_temp_volume_path_default,
   (void *) &PRM_IO_TEMP_VOLUME_PATH,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"volume_extension_path",
   (PRM_REQUIRED | PRM_STRING | PRM_DEFAULT | PRM_FOR_SERVER |
    PRM_USER_CHANGE),
   (void *) &prm_io_volume_ext_path_default,
   (void *) &PRM_IO_VOLUME_EXT_PATH,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"print_key_value_on_unique_error",
   (PRM_BOOLEAN | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_unique_error_key_value_default,
   (void *) &PRM_UNIQUE_ERROR_KEY_VALUE,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"service::service",
   (PRM_STRING | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_service_service_list_default,
   (void *) &PRM_SERVICE_SERVICE_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL},
  {"service::server",
   (PRM_STRING | PRM_DEFAULT | PRM_FOR_SERVER | PRM_USER_CHANGE),
   (void *) &prm_service_server_list_default,
   (void *) &PRM_SERVICE_SERVER_LIST,
   (void *) NULL, (void *) NULL,
   (char *) NULL}
};

#define NUM_PRM ((int)(sizeof(prm_Def)/sizeof(prm_Def[0])))


/*
 * Keyword searches do a intl_mbs_ncasecmp(), using the LENGTH OF THE TABLE KEY
 * as the limit, so make sure that overlapping keywords are ordered
 * correctly.  For example, make sure that "yes" precedes "y".
 */

typedef struct keyval KEYVAL;
struct keyval
{
  const char *key;
  void *val;
};

static KEYVAL boolean_words[] = {
  {"yes", (void *) 1},
  {"y", (void *) 1},
  {"1", (void *) 1},
  {"true", (void *) 1},
  {"no", (void *) 0},
  {"n", (void *) 0},
  {"0", (void *) 0},
  {"false", (void *) 0}
};

static KEYVAL isolation_level_words[] = {
  {"tran_serializable", (void *) TRAN_SERIALIZABLE},
  {"tran_no_phantom_read", (void *) TRAN_SERIALIZABLE},

  {"tran_rep_class_rep_instance", (void *) TRAN_REP_CLASS_REP_INSTANCE},
  {"tran_rep_read", (void *) TRAN_REP_CLASS_REP_INSTANCE},
  {"tran_rep_class_commit_instance", (void *) TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"tran_read_committed", (void *) TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"tran_cursor_stability", (void *) TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"tran_rep_class_uncommit_instance",
   (void *) TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  /*
   * This silly spelling has to hang around because it was in there
   * once upon a time and users may have come to depend on it.
   */
  {"tran_read_uncommited", (void *) TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"tran_read_uncommitted", (void *) TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"tran_commit_class_commit_instance",
   (void *) TRAN_COMMIT_CLASS_COMMIT_INSTANCE},
  {"tran_commit_class_uncommit_instance",
   (void *) TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE},

  /*
   * Why be so fascict about the "tran_" prefix?  Are we afraid someone
   * is going to use these gonzo words?
   */
  {"serializable", (void *) TRAN_SERIALIZABLE},
  {"no_phantom_read", (void *) TRAN_SERIALIZABLE},

  {"rep_class_rep_instance", (void *) TRAN_REP_CLASS_REP_INSTANCE},
  {"rep_read", (void *) TRAN_REP_CLASS_REP_INSTANCE},
  {"rep_class_commit_instance", (void *) TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"read_committed", (void *) TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"cursor_stability", (void *) TRAN_REP_CLASS_COMMIT_INSTANCE},
  {"rep_class_uncommit_instance", (void *) TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"read_uncommited", (void *) TRAN_REP_CLASS_UNCOMMIT_INSTANCE},
  {"commit_class_commit_instance",
   (void *) TRAN_COMMIT_CLASS_COMMIT_INSTANCE},
  {"commit_class_uncommit_instance",
   (void *) TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE}
};

static KEYVAL null_words[] = {
  {"null", (void *) 0},
  {"0", (void *) 0}
};


/*
 * Message id in the set MSGCAT_SET_PARAMETER
 * in the message catalog MSGCAT_CATALOG_CUBRID (file cubrid.msg).
 */
#define MSGCAT_PARAM_NOT_DIRECTORY              2
#define MSGCAT_PARAM_INIT_FILE_NOT_CREATED      3
#define MSGCAT_PARAM_CANT_WRITE                 4
#define MSGCAT_PARAM_CANT_ACCESS                6
#define MSGCAT_PARAM_NO_HOME                    7
#define MSGCAT_PARAM_NO_VALUE                   8
#define MSGCAT_PARAM_CANT_OPEN_INIT             9
#define MSGCAT_PARAM_BAD_LINE                   10
#define MSGCAT_PARAM_BAD_ENV_VAR                11
#define MSGCAT_PARAM_BAD_KEYWORD                12
#define MSGCAT_PARAM_BAD_VALUE                  13
#define MSGCAT_PARAM_NO_MEM                     14
#define MSGCAT_PARAM_BAD_STRING                 15
#define MSGCAT_PARAM_BAD_RANGE                  16
#define MSGCAT_PARAM_UNIX_ERROR                 17
#define MSGCAT_PARAM_NO_MSG                     18
#define MSGCAT_PARAM_RESET_BAD_RANGE            19
#define MSGCAT_PARAM_KEYWORD_INFO_INT           20
#define MSGCAT_PARAM_KEYWORD_INFO_FLOAT         21

static void prm_dump_system_parameter_table (FILE * fp);
static int prm_print (const SYSPRM_PARAM * prm, char *buf, size_t len);
static void prm_check_environment (void);
static void prm_load_by_section (INI_TABLE * ini, const char *section,
				 bool ignore_section, const char *file);
static int prm_read_and_parse_ini_file (const char *prm_file_name,
					const char *db_name);
static void prm_set_call_stack_dump_err_array (const char *s, bool errors[]);
static void prm_report_bad_entry (int line, int err, const char *where);
static int prm_set (SYSPRM_PARAM * prm, const char *value);
static int prm_set_force (SYSPRM_PARAM * prm, const char *value);
static int prm_set_default (SYSPRM_PARAM * prm);
static SYSPRM_PARAM *prm_find (const char *pname, const char *section);
static const KEYVAL *prm_search (const char *pname, const KEYVAL * tbl,
				 int dim);
#if defined (SA_MODE) || defined (SERVER_MODE)
static void prm_tune_server_parameters (void);
#endif

#if defined (SA_MODE) || defined (CS_MODE)
static void prm_tune_client_parameters (void);
#endif

/*
 * prm_dump_system_parameter_table - Print out current system parameters
 *   return: none
 *   fp(in):
 */
static void
prm_dump_system_parameter_table (FILE * fp)
{
  char tmp_buf[PATH_MAX];
  char null_string_format[] = "%s=(char*)%x\n";
  char string_format[] = "%s=\"%s\"\n";
  char float_format[] = "%s=%f\n";
  char int_format[] = "%s=%d\n";
  char bool_yes_format[] = "%s=y\n";
  char bool_no_format[] = "%s=n\n";
  char *format;
  int i;

  for (i = 0; i < NUM_PRM; i++)
    {
      if (PRM_IS_INTEGER (prm_Def[i].flag)
	  || PRM_IS_ISOLATION_LEVEL (prm_Def[i].flag))
	{
	  format = int_format;
	  sprintf (tmp_buf, format, prm_Def[i].name,
		   PRM_GET_INT (prm_Def[i].value));
	}
      else if (PRM_IS_STRING (prm_Def[i].flag))
	{
	  if (PRM_GET_STRING (prm_Def[i].value))
	    {
	      format = string_format;
	      sprintf (tmp_buf, format, prm_Def[i].name,
		       PRM_GET_STRING (prm_Def[i].value));
	    }
	  else
	    {
	      format = null_string_format;
	      sprintf (tmp_buf, format, prm_Def[i].name,
		       PRM_GET_STRING (prm_Def[i].value));

	    }
	}
      else if (PRM_IS_FLOAT (prm_Def[i].flag))
	{
	  format = float_format;
	  sprintf (tmp_buf, format, prm_Def[i].name,
		   PRM_GET_FLOAT (prm_Def[i].value));
	}
      else if (PRM_IS_BOOLEAN (prm_Def[i].flag))
	{
	  format = (PRM_GET_BOOL (prm_Def[i].value)
		    ? bool_yes_format : bool_no_format);
	  sprintf (tmp_buf, format, prm_Def[i].name);
	}
      fprintf (fp, "%s", tmp_buf);
    }

  return;
}


/*
 * sysprm_load_and_init - Read system parameters from the init files
 *   return: NO_ERROR or ER_FAILED
 *   db_name(in): database name
 *
 * Note: Parameters would be tuned and forced according to the internal rules.
 */
int
sysprm_load_and_init (const char *db_name, const char *conf_file)
{
#if !defined(CS_MODE)
  time_t log_time;
  struct tm log_tm, *log_tm_p = &log_tm;
  const char *base_db_name = NULL;
  const char *slash;
#endif /* !CS_MODE */
  char error_log_name[PATH_MAX];
  char file_being_dealt_with[PATH_MAX];
  const char *root_path;
  unsigned int i;
  struct stat buf;

  if (db_name == NULL)
    {
      /* intialize message catalog at here because there could be a code path
       * that did not call msgcat_init() before */
      if (msgcat_init () != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  root_path = envvar_root ();
#if defined (WINDOWS)
  sprintf (error_log_name, "%s\\%s", root_path, ERROR_FILE_DIR);
#else /* WINDOWS */
  sprintf (error_log_name, "%s/%s", root_path, ERROR_FILE_DIR);
#endif /* !WINDOWS */
  if (access (error_log_name, 0) < 0)
    {
      mkdir (error_log_name, 0777);
      /* if fail, logfile is stderr */
    }

#if !defined (CS_MODE)
  /*
   * Use the base database name to construct a DEFAULT name
   * for the error log message file (server only).
   */
  if (db_name && *db_name != '\0')
    {
      slash = (char *) strrchr (db_name, '/');
#if defined (WINDOWS)
      {
	char *r_slash = (char *) strrchr (db_name, '\\');
	if (slash < r_slash)
	  {
	    slash = r_slash;
	  }
      }
#endif /* WINDOWS */
      if (slash != NULL)
	{
	  base_db_name = slash + 1;
	}
      else
	{
	  base_db_name = db_name;
	}

      if (*base_db_name != '\0' && root_path)
	{
	  /*
	   * error_log_name size = strlen (base_db_name) +
	   *                       strlen (root_path) +
	   *                       strlen (ERROR_FILE_DIR) +
	   *                       strlen (ER_LOG_SUFFIX) +
	   *                       20 (delimeter, date & time)
	   */
	  log_time = time (NULL);
#if defined (SERVER_MODE) && !defined (WINDOWS)
	  log_tm_p = localtime_r (&log_time, &log_tm);
#else
	  log_tm_p = localtime (&log_time);
#endif /* SERVER_MODE && !WINDOWS */
#if defined(WINDOWS)
	  sprintf (error_log_name, "%s\\%s_%04d%02d%02d_%02d%02d%s",
		   error_log_name, base_db_name,
		   log_tm_p->tm_year + 1900, log_tm_p->tm_mon + 1,
		   log_tm_p->tm_mday, log_tm_p->tm_hour,
		   log_tm_p->tm_min, ER_LOG_SUFFIX);
#else /* WINDOWS */
	  sprintf (error_log_name, "%s/%s_%04d%02d%02d_%02d%02d%s",
		   error_log_name, base_db_name,
		   log_tm_p->tm_year + 1900, log_tm_p->tm_mon + 1,
		   log_tm_p->tm_mday, log_tm_p->tm_hour,
		   log_tm_p->tm_min, ER_LOG_SUFFIX);
#endif /* !WINDOWS */
	  (void) prm_set (prm_find ("error_log", NULL), error_log_name);
	}
    }
#endif /* !CS_MODE */


  memset (PRM_CALL_STACK_DUMP_ACTIVE_ERRORS, 0, 1024);
  memset (PRM_CALL_STACK_DUMP_DEACTIVE_ERRORS, 0, 1024);

  if (conf_file == NULL)
    {
      /* use environment variable's value if exist */
      conf_file = envvar_get ("CONF_FILE");
    }

  /*
   * Read installation configuration file - $CUBRID/conf/cubrid.conf
   *   or use conf_file if exist
   */
  if (root_path || (conf_file && strlen (conf_file) > 0))
    {
      if (conf_file)
	{
	  /* use user specified config path and file */
	  strcpy (file_being_dealt_with, conf_file);
	}
      else if (root_path)
	{
#if defined (WINDOWS)
	  sprintf (file_being_dealt_with, "%s\\%s\\%s",
		   root_path, CONF_FILE_DIR, sysprm_conf_file_name);
#else /* WINDOWS */
	  sprintf (file_being_dealt_with, "%s/%s/%s",
		   root_path, CONF_FILE_DIR, sysprm_conf_file_name);
#endif /* !WINDOWS */
	}

      if (stat (file_being_dealt_with, &buf) != 0)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_PARAMETERS,
					   MSGCAT_PARAM_CANT_ACCESS),
		   file_being_dealt_with, strerror (errno));
	}
      else
	{
#if !defined(CS_MODE)
	  (void) prm_read_and_parse_ini_file (file_being_dealt_with,
					      base_db_name);
#else
	  (void) prm_read_and_parse_ini_file (file_being_dealt_with, NULL);
#endif
	}
    }

#if !defined (SERVER_MODE)
  if (conf_file == NULL)
    {
      /*
       * Read $PWD/cubrid.conf if exist
       */
#if defined (WINDOWS)
      sprintf (file_being_dealt_with, ".\\%s", sysprm_conf_file_name);
#else /* WINDOWS */
      sprintf (file_being_dealt_with, "./%s", sysprm_conf_file_name);
#endif /* WINDOWS */
      if (stat (file_being_dealt_with, &buf) == 0)
	{
#if !defined(CS_MODE)
	  (void) prm_read_and_parse_ini_file (file_being_dealt_with,
					      base_db_name);
#else
	  (void) prm_read_and_parse_ini_file (file_being_dealt_with, NULL);
#endif
	}
    }
#endif

  /*
   * If a parameter is not given, set it by default
   */
  for (i = 0; i < NUM_PRM; i++)
    {
      if (!PRM_IS_SET (prm_Def[i].flag))
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
				       MSGCAT_PARAM_NO_VALUE),
		       prm_Def[i].name);
	    }
	}
    }

  /*
   * Perform system parameter check and tuning.
   */
  prm_check_environment ();
#if defined (SA_MODE) || defined (SERVER_MODE)
  prm_tune_server_parameters ();
#endif

#if defined (SA_MODE) || defined (CS_MODE)
  prm_tune_client_parameters ();
#endif

  /*
   * Perform forced system parameter setting.
   */
  for (i = 0; i < DIM (prm_Def); i++)
    {
      if (prm_Def[i].force_value)
	{
	  prm_set (&prm_Def[i], prm_Def[i].force_value);
	}
    }

  if (envvar_get ("PARAM_DUMP"))
    prm_dump_system_parameter_table (stdout);

  intl_Mbs_support = PRM_INTL_MBS_SUPPORT;

  return NO_ERROR;
}

/*
 * prm_load_by_section - Set system parameters from a file
 *   return: void
 *   ini(in):
 *   section(in):
 *   file(in):
 */
static void
prm_load_by_section (INI_TABLE * ini, const char *section,
		     bool ignore_section, const char *file)
{
  int i, error;
  int sec_len;
  const char *key, *value;

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

      if (ignore_section)
	{
	  error = prm_set (prm_find (key + sec_len, NULL), value);
	}
      else
	{
	  error = prm_set (prm_find (key + sec_len, section), value);
	}
      if (error != NO_ERROR)
	{
	  prm_report_bad_entry (ini->lineno[i], error, file);
	}
    }
}

/*
 * prm_read_and_parse_ini_file - Set system parameters from a file
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   prm_file_name(in):
 *   db_name(in):
 */
static int
prm_read_and_parse_ini_file (const char *prm_file_name, const char *db_name)
{
  INI_TABLE *ini;

  ini = ini_parser_load (prm_file_name);
  if (ini == NULL)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
			       MSGCAT_PARAM_CANT_OPEN_INIT), prm_file_name,
	       strerror (errno));
      return PRM_ERR_FILE_ERR;
    }

  prm_load_by_section (ini, "common", true, prm_file_name);
  if (db_name && strlen (db_name) > 0)
    {
      char *sec_name;

      sec_name = (char *) malloc (strlen (db_name) + 2);
      if (sec_name == NULL)
	{
	  return (PRM_ERR_NO_MEM_FOR_PRM);
	}
      sprintf (sec_name, "@%s", db_name);
      prm_load_by_section (ini, sec_name, true, prm_file_name);
      free_and_init (sec_name);
    }
  prm_load_by_section (ini, "service", false, prm_file_name);

  ini_parser_free (ini);
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
  char buf[256];

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
	  error = prm_set (prm, str);
	  if (error != 0)
	    {
	      prm_report_bad_entry (-1, error, buf);
	    }
	}
    }
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
  char buf[256], *p, *name, *value;
  SYSPRM_PARAM *prm;
  int err = PRM_ERR_NO_ERROR;

  if (!data || *data == '\0')
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (intl_mbs_ncpy (buf, data, 255) == NULL)
    {
      return PRM_ERR_BAD_VALUE;
    }

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

      while (*p && !char_isspace (*p) && *p != '=')
	{
	  p++;
	}
      if (*p == '\0')
	{
	  return PRM_ERR_BAD_VALUE;
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
	  break;
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
	      return PRM_ERR_BAD_STRING;
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

      if ((prm = prm_find (name, NULL)) == NULL)
	{
	  return PRM_ERR_UNKNOWN_PARAM;
	}
      if (!PRM_USER_CAN_CHANGE (prm->flag))
	{
	  return PRM_ERR_CANNOT_CHANGE;
	}
#if defined (SA_MODE)
      err = prm_set (prm, value);
#endif /* SA_MODE */
#if defined(CS_MODE)
      if (PRM_IS_FOR_CLIENT (prm->flag))
	{
	  err = prm_set (prm, value);
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
#endif /* CS_MODE */
#if defined (SERVER_MODE)
      if (!PRM_IS_FOR_SERVER (prm->flag))
	{
	  return PRM_ERR_NOT_FOR_SERVER;
	}
      err = prm_set (prm, value);
#endif /* SERVER_MODE */
    }
  while (err == PRM_ERR_NO_ERROR && *p);

  return err;
}


/*
 * prm_print - Print a parameter to the buffer
 *   return: number of chars printed
 *   prm(in): parameter
 *   buf(out): print buffer
 *   len(in): length of the buffer
 *
 * Note:
 */
static int
prm_print (const SYSPRM_PARAM * prm, char *buf, size_t len)
{
  int n;

  assert (prm != NULL && buf != NULL && len > 0);

  if (PRM_IS_INTEGER (prm->flag) || PRM_IS_ISOLATION_LEVEL (prm->flag))
    {
      n = snprintf (buf, len, "%s=%d", prm->name, PRM_GET_INT (prm->value));
    }
  else if (PRM_IS_BOOLEAN (prm->flag))
    {
      n = snprintf (buf, len, "%s=%c", prm->name,
		    (PRM_GET_BOOL (prm->value) ? 'y' : 'n'));
    }
  else if (PRM_IS_FLOAT (prm->flag))
    {
      n = snprintf (buf, len, "%s=%f", prm->name, PRM_GET_FLOAT (prm->value));
    }
  else if (PRM_IS_STRING (prm->flag))
    {
      n = snprintf (buf, len, "%s=\"%s\"", prm->name,
		    (PRM_GET_STRING (prm->value) ?
		     PRM_GET_STRING (prm->value) : ""));
    }
  else
    {
      n = snprintf (buf, len, "%s=?", prm->name);
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
  char buf[256], *p, *name, *t;
  int n;
  SYSPRM_PARAM *prm;

  if (!data || *data == '\0' || len <= 0)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (intl_mbs_ncpy (buf, data, 255) == NULL)
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

      if ((prm = prm_find (name, NULL)) == NULL)
	{
	  return PRM_ERR_UNKNOWN_PARAM;
	}

      if (t != data)
	{
	  *t++ = ';';
	  len--;
	}

#ifdef SA_MODE
      n = prm_print (prm, t, len);
#endif
#if defined(CS_MODE) || defined(CS_MODE_MT)
      if (PRM_IS_FOR_CLIENT (prm->flag))
	{
	  n = prm_print (prm, t, len);
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
#ifdef SERVER_MODE
      if (!PRM_IS_FOR_SERVER (prm->flag))
	{
	  return PRM_ERR_NOT_FOR_SERVER;
	}
      n = prm_print (prm, t, len);
#endif
      len -= n;
      t += n;
    }
  while (*p && len > 0);

  return NO_ERROR;
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
  int err = PRM_ERR_NOT_SOLE_TRAN;

#if defined (SERVER_MODE)
  if (ustr_casestr (data, "suppress_fsync"))
    {
      return sysprm_change_parameters (data);
    }

  if (css_number_of_clients () == 1)
    if (logtb_am_i_sole_tran (NULL))
      {
	err = sysprm_change_parameters (data);
	logtb_i_am_not_sole_tran ();
      }
#else /* SERVER_MODE */
  err = sysprm_change_parameters (data);
#endif /* SERVER_MODE */

  return err;
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
#endif /* !CS_MODE */


/*
 * prm_set_call_stack_dump_err_array - Convert an error id list of a string
 *                                     format to boolean array of error id
 *   return: none
 *   str(in):
 *   errors(out):
 *
 * Note: Set values of PRM_CALL_STACK_DUMP_ACTIVATION/DISABLE_ERRORS parameters
 *       from values of PRM_CALL_STACK_DUMP_ACTIVATION/DISABLE parameters
 */
static void
prm_set_call_stack_dump_err_array (const char *str, bool errors[])
{
  char *p, *err_val;
  int err_id;

  p = (char *) str;
  err_val = p;
  while (*p)
    {
      if (*p == ',')
	{
	  *p = '\0';
	  err_id = atoi (err_val);
	  errors[abs (err_id)] = true;
	  err_val = p + 1;
	  *p = ',';
	}
      p++;
    }
  err_id = atoi (err_val);
  errors[abs (err_id)] = true;
}


/*
 * prm_set - Set the value of a parameter
 *   return: NO_ERROR or error code defined in enum SYSPRM_ERR
 *   prm(in):
 *   value(in):
 */
static int
prm_set (SYSPRM_PARAM * prm, const char *value)
{
  char *end;
  int warning_status = NO_ERROR;

  if (prm == NULL)
    {
      return PRM_ERR_UNKNOWN_PARAM;
    }

  if (value == NULL || strlen (value) == 0)
    {
      return PRM_ERR_BAD_VALUE;
    }

  if (PRM_IS_INTEGER (prm->flag))
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
	  if (PRM_HAS_DEFAULT (prm->flag))
	    {
	      if (val != PRM_GET_INT (prm->default_value))
		{
		  fprintf (stderr,
			   msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_PARAMETERS,
					   MSGCAT_PARAM_KEYWORD_INFO_INT),
			   prm->name, val, PRM_GET_INT (prm->default_value));
		  val = PRM_GET_INT (prm->default_value);
		  warning_status = PRM_ERR_RESET_BAD_RANGE;
		}
	    }
	  else
	    {
	      return PRM_ERR_BAD_RANGE;
	    }
	}
      valp = (int *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_FLOAT (prm->flag))
    {
      float val, *valp;

      val = strtod (value, &end);
      if (end == value)
	{
	  return PRM_ERR_BAD_VALUE;
	}
      if ((prm->upper_limit && PRM_GET_FLOAT (prm->upper_limit) < val)
	  || (prm->lower_limit && PRM_GET_FLOAT (prm->lower_limit) > val))
	{
	  if (PRM_HAS_DEFAULT (prm->flag))
	    {
	      if (val != PRM_GET_FLOAT (prm->default_value))
		{
		  fprintf (stderr,
			   msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_PARAMETERS,
					   MSGCAT_PARAM_KEYWORD_INFO_FLOAT),
			   prm->name, val,
			   PRM_GET_FLOAT (prm->default_value));
		  val = PRM_GET_FLOAT (prm->default_value);
		  warning_status = PRM_ERR_RESET_BAD_RANGE;
		}
	    }
	  else
	    {
	      return PRM_ERR_BAD_RANGE;
	    }
	}
      valp = (float *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_BOOLEAN (prm->flag))
    {
      bool *valp;
      const KEYVAL *keyvalp;

      keyvalp = prm_search (value, boolean_words, DIM (boolean_words));
      if (keyvalp == NULL)
	{
	  return PRM_ERR_BAD_VALUE;
	}
      valp = (bool *) prm->value;
      *valp = (bool) ((int) (keyvalp->val) & 0xFF);
    }
  else if (PRM_IS_STRING (prm->flag))
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
      if (prm_search (value, null_words, DIM (null_words)) != NULL)
	{
	  val = NULL;
	}
      else
	{
	  val = strdup (value);
	  if (val == NULL)
	    {
	      return (PRM_ERR_NO_MEM_FOR_PRM);
	    }
	  PRM_SET_BIT (PRM_ALLOCATED, prm->flag);
	}
      *valp = val;
    }
  else if (PRM_IS_ISOLATION_LEVEL (prm->flag))
    {
      int val, *valp;
      const KEYVAL *keyvalp;

      keyvalp = prm_search (value, isolation_level_words,
			    DIM (isolation_level_words));
      if (keyvalp)
	{
	  val = (int) (keyvalp->val);
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
	  if (PRM_HAS_DEFAULT (prm->flag))
	    {
	      if (val != PRM_GET_INT (prm->default_value))
		{
		  fprintf (stderr,
			   msgcat_message (MSGCAT_CATALOG_CUBRID,
					   MSGCAT_SET_PARAMETERS,
					   MSGCAT_PARAM_KEYWORD_INFO_INT),
			   prm->name, val, PRM_GET_INT (prm->default_value));
		  val = PRM_GET_INT (prm->default_value);
		  warning_status = PRM_ERR_RESET_BAD_RANGE;
		}
	    }
	  else
	    {
	      return PRM_ERR_BAD_RANGE;
	    }
	}
      valp = (int *) prm->value;
      *valp = val;
    }

  if (PRM_IS_STACK_DUMP_ENABLE (prm->flag))
    {
      prm_set_call_stack_dump_err_array (PRM_CALL_STACK_DUMP_ACTIVATION,
					 PRM_CALL_STACK_DUMP_ACTIVE_ERRORS);
    }
  else if (PRM_IS_STACK_DUMP_DISABLE (prm->flag))
    {
      prm_set_call_stack_dump_err_array (PRM_CALL_STACK_DUMP_DEACTIVATION,
					 PRM_CALL_STACK_DUMP_DEACTIVE_ERRORS);
    }

  PRM_SET_BIT (PRM_SET, prm->flag);
  /* Indicate that the default value was not used */
  PRM_CLEAR_BIT (PRM_DEFAULT_USED, prm->flag);
  return warning_status;
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
      free_and_init (PRM_GET_STRING (prm->force_value));
    }

  prm->force_value = strdup (value);
  if (prm->force_value == NULL)
    {
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
  if (PRM_IS_INTEGER (prm->flag) || PRM_IS_ISOLATION_LEVEL (prm->flag))
    {
      int val, *valp;
      val = PRM_GET_INT (prm->default_value);
      valp = (int *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_BOOLEAN (prm->flag))
    {
      bool val, *valp;
      val = PRM_GET_BOOL (prm->default_value);
      valp = (bool *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_FLOAT (prm->flag))
    {
      float val, *valp;
      val = PRM_GET_FLOAT (prm->default_value);
      valp = (float *) prm->value;
      *valp = val;
    }
  else if (PRM_IS_STRING (prm->flag))
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

  PRM_SET_BIT (PRM_SET, prm->flag);
  /* Indicate that the default value was used */
  PRM_SET_BIT (PRM_DEFAULT_USED, prm->flag);
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
      sprintf (buf, "%s::%s", section, pname);
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
sysprm_set_to_default (const char *pname)
{
  SYSPRM_PARAM *prm;

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
  return NO_ERROR;
}


/*
 * prm_search - Search a keyword within the keyword table
 *   return: NULL or found keyword
 *   name(in): keyword name
 *   tbl(in): keyword table
 *   dim(in): size of the table
 */
static const KEYVAL *
prm_search (const char *name, const KEYVAL * tbl, int dim)
{
  int i;

  for (i = 0; i < dim; i++)
    {
      if (intl_mbs_ncasecmp (name, tbl[i].key, strlen (tbl[i].key)) == 0)
	{
	  return &tbl[i];
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
prm_report_bad_entry (int line, int err, const char *where)
{
  if (line >= 0)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
			       MSGCAT_PARAM_BAD_LINE), line, where);
    }
  else
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
			       MSGCAT_PARAM_BAD_ENV_VAR), where);
    }

  if (err > 0)
    {
      switch (err)
	{
	case PRM_ERR_UNKNOWN_PARAM:
	  {
	    fprintf (stderr, "%s\n",
		     msgcat_message (MSGCAT_CATALOG_CUBRID,
				     MSGCAT_SET_PARAMETERS,
				     MSGCAT_PARAM_BAD_KEYWORD));
	    break;
	  }
	case PRM_ERR_BAD_VALUE:
	  {
	    fprintf (stderr, "%s\n",
		     msgcat_message (MSGCAT_CATALOG_CUBRID,
				     MSGCAT_SET_PARAMETERS,
				     MSGCAT_PARAM_BAD_VALUE));
	    break;
	  }
	case PRM_ERR_NO_MEM_FOR_PRM:
	  {
	    fprintf (stderr, "%s\n",
		     msgcat_message (MSGCAT_CATALOG_CUBRID,
				     MSGCAT_SET_PARAMETERS,
				     MSGCAT_PARAM_NO_MEM));
	    break;
	  }

	case PRM_ERR_BAD_STRING:
	  fprintf (stderr, "%s\n",
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_PARAMETERS,
				   MSGCAT_PARAM_BAD_STRING));
	  break;

	case PRM_ERR_BAD_RANGE:
	  fprintf (stderr, "%s\n",
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_PARAMETERS,
				   MSGCAT_PARAM_BAD_RANGE));
	  break;

	case PRM_ERR_RESET_BAD_RANGE:
	  fprintf (stderr, "%s\n",
		   msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_PARAMETERS,
				   MSGCAT_PARAM_RESET_BAD_RANGE));
	  break;

	default:
	  break;
	}
    }
  else
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
			       MSGCAT_PARAM_UNIX_ERROR), strerror (err));
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
      if (PRM_IS_ALLOCATED (prm->flag) && PRM_IS_STRING (prm->flag))
	{
	  char *str = PRM_GET_STRING (prm->value);
	  free_and_init (str);
	  PRM_CLEAR_BIT (PRM_ALLOCATED, prm->flag);
	  valp = (char **) prm->value;
	  *valp = NULL;
	}
    }
}


#if defined(SA_MODE) || defined (SERVER_MODE)
/*
 * prm_tune_server_parameters - Sets the values of various system parameters depending
 *                       on the value of other parameters
 *   return: none
 *
 * Note: Used for providing a mechanism for tuning various system parameters.                                                   *
 *       The parameters are only tuned if the user has not set them
 *       explictly, this can be ascertained by checking if the default
 *       value has been used.
 */
static void
prm_tune_server_parameters (void)
{
  SYSPRM_PARAM *num_data_buffers_prm;
  SYSPRM_PARAM *num_log_buffers_prm;
  SYSPRM_PARAM *max_clients_prm;
  SYSPRM_PARAM *max_threads_prm;
  SYSPRM_PARAM *checkpoint_interval_minutes_prm;
  SYSPRM_PARAM *max_scanid_bit_prm;
  SYSPRM_PARAM *max_plan_cache_entries_prm;
  SYSPRM_PARAM *max_plan_cache_clones_prm;
  SYSPRM_PARAM *query_cache_mode_prm;
  SYSPRM_PARAM *max_query_cache_entries_prm;
  SYSPRM_PARAM *query_cache_size_in_pages_prm;

  char newval[PATH_MAX];

  /* Find the parameters that require tuning */

  num_data_buffers_prm = prm_find ("data_buffer_pages", NULL);
  num_log_buffers_prm = prm_find ("log_buffer_pages", NULL);
  max_clients_prm = prm_find ("max_clients", NULL);
  max_threads_prm = prm_find ("max_threads", NULL);
  checkpoint_interval_minutes_prm =
    prm_find ("checkpoint_interval_in_mins", NULL);
  max_scanid_bit_prm = prm_find ("max_index_scan_count", NULL);
  max_plan_cache_entries_prm = prm_find ("max_plan_cache_entries", NULL);
  max_plan_cache_clones_prm = prm_find ("max_plan_cache_clones", NULL);
  query_cache_mode_prm = prm_find ("query_cache_mode", NULL);
  max_query_cache_entries_prm = prm_find ("max_query_cache_entries", NULL);
  query_cache_size_in_pages_prm =
    prm_find ("query_cache_size_in_pages", NULL);

  if (num_data_buffers_prm == NULL || num_log_buffers_prm == NULL
      || max_clients_prm == NULL || max_threads_prm == NULL
      /* || checkpoint_interval_minutes_prm == NULL */
    )
    {
      return;
    }

  /* Check that max clients has been set */
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
				   MSGCAT_PARAM_NO_VALUE),
		   max_clients_prm->name);
	  return;
	}
    }

  /* Set max thread parameter */
  sprintf (newval, "%d", (PRM_GET_INT (max_clients_prm->value)) * 2);
  (void) prm_set (max_threads_prm, (char *) newval);

  /* perform the actual tuning here, if the parameters have
     not been set, by user, i.e., default values used. */

#if defined (XSERVER_MODE)
  if (PRM_GET_INT (max_clients_prm->value) * 2 >
      PRM_GET_INT (max_threads_prm->value))
    {
      sprintf (newval, "%d", (PRM_GET_INT (max_clients_prm->value) * 2));
      (void) prm_set (max_threads_prm, (char *) newval);
    }
#endif /* XSERVER */

#if defined (SERVER_MODE)
  if (PRM_GET_INT (max_scanid_bit_prm->value) % 32)
    {
      sprintf (newval, "%d",
	       ((PRM_GET_INT (max_scanid_bit_prm->value)) +
		32 - (PRM_GET_INT (max_scanid_bit_prm->value)) % 32));
      (void) prm_set (max_scanid_bit_prm, (char *) newval);
    }
#endif /* SERVER_MODE */

  /* check Plan Cache and Query Cache parameters */
  if (PRM_GET_INT (max_plan_cache_entries_prm->value) == 0)
    {
      /* 0 means disable plan cache */
      (void) prm_set (max_plan_cache_entries_prm, "-1");
    }

  if (PRM_GET_INT (max_plan_cache_entries_prm->value) <= 0)
    {
      /* disable all by default */
      (void) prm_set_default (max_plan_cache_clones_prm);
      (void) prm_set_default (query_cache_mode_prm);
      (void) prm_set_default (max_query_cache_entries_prm);
      (void) prm_set_default (query_cache_size_in_pages_prm);
    }
  else
    {
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
	  (void) prm_set (max_plan_cache_clones_prm, newval);
	}
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
	      (void) prm_set (max_query_cache_entries_prm, newval);
	    }
	}
    }

  return;
}
#endif /* SA_MODE || SERVER_MODE */

#if defined(SA_MODE) || defined (CS_MODE)
/*
 * prm_tune_client_parameters - Sets the values of various system parameters depending
 *                       on the value of other parameters
 *   return: none
 *
 * Note: Used for providing a mechanism for tuning various system parameters.
 *       The parameters are only tuned if the user has not set them
 *       explictly, this can be ascertained by checking if the default
 *       value has been used.
 */
static void
prm_tune_client_parameters (void)
{
  SYSPRM_PARAM *max_plan_cache_entries_prm;

  /* Find the parameters that require tuning */
  max_plan_cache_entries_prm = prm_find ("max_plan_cache_entries", NULL);

  /* check Plan Cache and Query Cache parameters */
  if (PRM_GET_INT (max_plan_cache_entries_prm->value) == 0)
    {
      /* 0 means disable plan cache */
      (void) prm_set (max_plan_cache_entries_prm, "-1");
    }

  return;
}
#endif /* SA_MODE || CS_MODE */
