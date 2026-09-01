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
 * cas_common_vars.h - 
 */

#ifndef	_CAS_COMMON_VARS_H_
#define	_CAS_COMMON_VARS_H_

#ident "$Id$"

#include "broker_shm.h"
#include "cas_protocol.h"
#include "cas_error.h"


/* stage B1 (#117): in the merged server the CAS speaker runs as one dedicated
 * thread per adopted connection, so a CAS-process-global is made session-local
 * by making it thread-local.  The standalone CAS/CGW builds are unchanged. */
#if defined(SERVER_MODE)
#define CAS_TLS thread_local
#else
#define CAS_TLS
#endif

typedef struct t_object T_OBJECT;
struct t_object
{
  int pageid;
  short slotid;
  short volid;
};

typedef struct t_lob_handle T_LOB_HANDLE;
struct t_lob_handle
{
  int db_type;
  INT64 lob_size;
  int locator_size;
  char *locator;
};

enum tran_auto_commit
{
  TRAN_NOT_AUTOCOMMIT = 0,
  TRAN_AUTOCOMMIT = 1,
  TRAN_AUTOROLLBACK = 2
};

typedef struct t_req_info T_REQ_INFO;
struct t_req_info
{
  T_BROKER_VERSION client_version;
  char driver_info[SRV_CON_CLIENT_INFO_SIZE];
  enum tran_auto_commit need_auto_commit;
  char need_rollback;
};

/* Shard ID variables */
extern int shm_shard_id;
extern int shm_proxy_id;
extern int shm_shard_cas_id;
extern int cas_shard_flag;

/* Shared memory variables */
/* per-session in the folded server (B2-D1): the CAS slot index names the
 * SQL/slow/DDL log files, so each adopted session needs its own */
extern CAS_TLS int shm_as_index;
extern T_SHM_APPL_SERVER *shm_appl;

/* B2-D7 (#116 D9): per-session snapshot of the cas_* config the legacy CAS
 * read from shared memory.  In the merged server every speaker thread reads
 * its own copy (taken at session begin from the system parameters), so no
 * session ever writes a field another session is reading (PR 7837 review).
 * The standalone CAS/CGW builds keep reading the real shm. */
typedef struct t_cas_session_cfg T_CAS_SESSION_CFG;
struct t_cas_session_cfg
{
  int sql_log_max_size;
  char access_log;
  int access_log_max_size;
  int long_query_time;		/* msec */
  int long_transaction_time;	/* msec */
  char jdbc_cache;
  char jdbc_cache_only_hint;
  int jdbc_cache_life_time;
  char statement_pooling;
  char cci_default_autocommit;
  int max_prepared_stmt_count;
  int session_timeout;
  int query_timeout;
  int max_string_length;
};
extern CAS_TLS T_CAS_SESSION_CFG cas_session_cfg;

#if defined(SERVER_MODE)
#define CAS_SHM_CFG(field) (cas_session_cfg.field)
#else
#define CAS_SHM_CFG(field) (shm_appl->field)
#endif
extern CAS_TLS T_APPL_SERVER_INFO *as_info;

/* Transaction and query timing */
extern CAS_TLS struct timeval tran_start_time;
extern CAS_TLS struct timeval query_start_time;
extern CAS_TLS int tran_timeout;
extern CAS_TLS int query_timeout;
extern CAS_TLS INT64 query_cancel_time;
extern CAS_TLS char query_cancel_flag;

/* Error handling */
extern CAS_TLS int errors_in_transaction;
extern CAS_TLS T_ERROR_INFO err_info;

/* Client info */
extern CAS_TLS char stripped_column_name;
extern CAS_TLS char cas_client_type;

/* CAS info buffer */
extern CAS_TLS char prev_cas_info[CAS_INFO_SIZE];

/* Network socket */
extern CAS_TLS SOCKET new_req_sock_fd;

#if defined(WINDOWS)
/* Request count for restart check (WINDOWS only) */
extern int cas_req_count;
#endif /* WINDOWS */

/* Program info */
extern const char *program_name;
extern char broker_name[BROKER_NAME_LEN];

/* CAS configuration */
extern CAS_TLS int cas_default_isolation_level;
extern CAS_TLS int cas_default_lock_timeout;
extern CAS_TLS int cas_send_result_flag;
extern CAS_TLS bool cas_default_ansi_quotes;
extern CAS_TLS bool cas_default_no_backslash_escapes;

/* Request info and query sequence */
extern CAS_TLS T_REQ_INFO req_info;
// extern int query_sequence_num;

/* Additional variables used by CAS and CGW */
extern int psize_at_start;
extern CAS_TLS int con_status_before_check_cas;
extern CAS_TLS bool is_first_request;
extern CAS_TLS int cas_info_size;
extern CAS_TLS bool autocommit_deferred;

/* Common functions */
extern void cas_set_db_connect_status (int status);
extern int cas_get_db_connect_status (void);

#endif /* _CAS_COMMON_VARS_H_ */
