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
extern int shm_as_index;
extern T_SHM_APPL_SERVER *shm_appl;
extern T_APPL_SERVER_INFO *as_info;

/* Transaction and query timing */
extern struct timeval tran_start_time;
extern struct timeval query_start_time;
extern int tran_timeout;
extern int query_timeout;
extern INT64 query_cancel_time;
extern char query_cancel_flag;

/* Error handling */
extern int errors_in_transaction;
extern T_ERROR_INFO err_info;

/* Client info */
extern char stripped_column_name;
extern char cas_client_type;

/* CAS info buffer */
extern char prev_cas_info[CAS_INFO_SIZE];

/* Network socket */
extern SOCKET new_req_sock_fd;

#if defined(WINDOWS)
/* Request count for restart check (WINDOWS only) */
extern int cas_req_count;
#endif /* WINDOWS */

/* Program info */
extern const char *program_name;
extern char broker_name[BROKER_NAME_LEN];

/* CAS configuration */
extern int cas_default_isolation_level;
extern int cas_default_lock_timeout;
extern int cas_send_result_flag;
extern bool cas_default_ansi_quotes;
extern bool cas_default_no_backslash_escapes;

/* Request info and query sequence */
extern T_REQ_INFO req_info;
// extern int query_sequence_num;

/* Additional variables used by CAS and CGW */
extern int psize_at_start;
extern int con_status_before_check_cas;
extern bool is_first_request;
extern int cas_info_size;
extern bool autocommit_deferred;

/* Common functions */
extern void cas_set_db_connect_status (int status);
extern int cas_get_db_connect_status (void);

#endif /* _CAS_COMMON_VARS_H_ */
