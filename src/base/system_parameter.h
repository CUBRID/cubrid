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
#include "porting.h"

typedef enum
{
  PRM_ERR_NO_ERROR = NO_ERROR,
  PRM_ERR_UNKNOWN_PARAM,
  PRM_ERR_BAD_VALUE,
  PRM_ERR_NO_MEM_FOR_PRM,
  PRM_ERR_BAD_STRING,
  PRM_ERR_BAD_RANGE,
  PRM_ERR_RESET_BAD_RANGE,
  PRM_ERR_CANNOT_CHANGE,
  PRM_ERR_NOT_FOR_CLIENT,
  PRM_ERR_NOT_FOR_SERVER,
  PRM_ERR_NOT_SOLE_TRAN,
  PRM_ERR_COMM_ERR,
  PRM_ERR_FILE_ERR
} SYSPRM_ERR;


/*
 * Global variables of parameters' value
 */

extern int PRM_ER_EXIT_ASK;
extern int PRM_ER_LOG_SIZE;
extern const char *PRM_ER_LOG_FILE;
extern bool PRM_IO_LOCKF_ENABLE;
extern int PRM_SR_NBUFFERS;
extern int PRM_PB_NBUFFERS;
extern float PRM_HF_UNFILL_FACTOR;
extern float PRM_BT_UNFILL_FACTOR;
extern int PRM_BT_OID_NBUFFERS;
extern bool PRM_BT_INDEX_SCAN_OID_ORDER;
extern int PRM_BOSR_MAXTMP_PAGES;
extern int PRM_LK_TIMEOUT_MESSAGE_DUMP_LEVEL;
extern int PRM_LK_ESCALATION_AT;
extern int PRM_LK_TIMEOUT_SECS;
extern int PRM_LK_RUN_DEADLOCK_INTERVAL;
extern int PRM_LOG_NBUFFERS;
extern int PRM_LOG_CHECKPOINT_NPAGES;
extern int PRM_LOG_CHECKPOINT_INTERVAL_MINUTES;
extern int PRM_LOG_ISOLATION_LEVEL;
extern bool PRM_LOG_MEDIA_FAILURE_SUPPORT;
extern bool PRM_LOG_SWEEP_CLEAN;
extern bool PRM_COMMIT_ON_SHUTDOWN;
extern bool PRM_CSQL_AUTO_COMMIT;
extern int PRM_WS_HASHTABLE_SIZE;
extern bool PRM_WS_MEMORY_REPORT;
extern bool PRM_GC_ENABLE;
extern int PRM_CSS_MAX_CLIENTS;
extern int PRM_TCP_PORT_ID;
extern int PRM_TCP_CONNECTION_TIMEOUT;
extern int PRM_OPTIMIZATION_LEVEL;
extern bool PRM_QO_DUMP;
extern int PRM_MAX_THREADS;
extern const char *PRM_CFG_DB_HOSTS;
extern int PRM_RESET_TR_PARSER;
extern int PRM_IO_BACKUP_NBUFFERS;
extern int PRM_IO_BACKUP_MAX_VOLUME_SIZE;
extern int PRM_MAX_PAGES_IN_TEMP_FILE_CACHE;
extern int PRM_MAX_ENTRIES_IN_TEMP_FILE_CACHE;
extern int PRM_THREAD_STACKSIZE;
extern bool PRM_PTHREAD_SCOPE_PROCESS;
extern int PRM_TEMP_MEM_BUFFER_PAGES;
extern bool PRM_DONT_REUSE_HEAP_FILE;
extern bool PRM_QUERY_MODE_SYNC;
extern int PRM_INSERT_MODE;
extern int PRM_LK_MAX_SCANID_BIT;
extern bool PRM_HOSTVAR_LATE_BINDING;
extern bool PRM_ENABLE_HISTO;
extern int PRM_MUTEX_BUSY_WAITING_CNT;
extern int PRM_PB_NUM_LRU_CHAINS;
extern bool PRM_ORACLE_STYLE_OUTERJOIN;
extern int PRM_COMPACTDB_PAGE_RECLAIM_ONLY;
extern float PRM_LIKE_TERM_SELECTIVITY;
extern int PRM_MAX_OUTER_CARD_OF_IDXJOIN;
extern bool PRM_ORACLE_STYLE_EMPTY_STRING;
extern bool PRM_SUPPRESS_FSYNC;
extern bool PRM_CALL_STACK_DUMP_ON_ERROR;
extern bool PRM_CALL_STACK_DUMP_ACTIVE_ERRORS[];
extern bool PRM_CALL_STACK_DUMP_DEACTIVE_ERRORS[];
extern bool PRM_COMPAT_NUMERIC_DIVISION_SCALE;
extern bool PRM_DBFILES_PROTECT;
extern bool PRM_AUTO_RESTART_SERVER;
extern int PRM_XASL_MAX_PLAN_CACHE_ENTRIES;
extern int PRM_XASL_MAX_PLAN_CACHE_CLONES;
extern int PRM_XASL_PLAN_CACHE_TIMEOUT;
extern int PRM_LIST_QUERY_CACHE_MODE;
extern int PRM_LIST_MAX_QUERY_CACHE_ENTRIES;
extern int PRM_LIST_MAX_QUERY_CACHE_PAGES;
extern bool PRM_REPLICATION_MODE;
extern bool PRM_HA_MODE;
extern int PRM_HA_SERVER_MODE;
extern bool PRM_JAVA_STORED_PROCEDURE;
extern bool PRM_COMPAT_PRIMARY_KEY;
extern bool PRM_BLOCK_DDL_STATEMENT;
extern bool PRM_BLOCK_NOWHERE_STATEMENT;
extern bool PRM_INTL_MBS_SUPPORT;
extern int PRM_LOG_HEADER_FLUSH_INTERVAL;
extern bool PRM_LOG_ASYNC_COMMIT;
extern int PRM_LOG_GROUP_COMMIT_INTERVAL_MSECS;
extern int PRM_LOG_BG_FLUSH_INTERVAL_MSECS;
extern int PRM_LOG_BG_FLUSH_NUM_PAGES;
extern bool PRM_LOG_COMPRESS;
extern bool PRM_SINGLE_BYTE_COMPARE;
extern int PRM_CSQL_HISTORY_NUM;
extern bool PRM_LOG_TRACE_DEBUG;
extern const char *PRM_DL_FORK;
extern bool PRM_ER_LOG_WARNING;
extern bool PRM_ER_PRODUCTION_MODE;
extern int PRM_ER_STOP_ON_ERROR;
extern int PRM_TCP_RCVBUF_SIZE;
extern int PRM_TCP_SNDBUF_SIZE;
extern int PRM_TCP_NODELAY;
extern bool PRM_CSQL_SINGLE_LINE_MODE;
extern bool PRM_XASL_DEBUG_DUMP;
extern int PRM_LOG_MAX_ARCHIVES;
extern bool PRM_LOG_NO_LOGGING;
extern bool PRM_UNLOADDB_IGNORE_ERROR;
extern int PRM_UNLOADDB_LOCK_TIMEOUT;
extern int PRM_LOADDB_FLUSH_INTERVAL;
extern const char *PRM_IO_TEMP_VOLUME_PATH;
extern const char *PRM_IO_VOLUME_EXT_PATH;
extern bool PRM_UNIQUE_ERROR_KEY_VALUE;
extern const char *PRM_SERVICE_SERVICE_LIST;
extern const char *PRM_SERVICE_SERVER_LIST;

extern int sysprm_load_and_init (const char *db_name, const char *conf_file);
extern void sysprm_final (void);
extern void sysprm_dump_system_parameters (FILE * fp);
extern int sysprm_change_parameters (const char *data);
extern int sysprm_obtain_parameters (char *data, int len);
extern int sysprm_change_server_parameters (const char *data);
extern int sysprm_obtain_server_parameters (char *data, int len);
extern void sysprm_tune_client_parameters (void);
#if !defined (CS_MODE)
extern int xsysprm_change_server_parameters (const char *data);
extern int xsysprm_obtain_server_parameters (char *data, int len);
#endif /* !CS_MODE */
extern int sysprm_set_force (const char *pname, const char *pvalue);
extern int sysprm_set_to_default (const char *pname);
extern int prm_get_master_port_id (void);
#endif /* _SYSTEM_PARAMETER_H_ */
