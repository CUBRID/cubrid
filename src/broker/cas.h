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
 * cas.h -
 */

#ifndef	_CAS_H_
#define	_CAS_H_

#ident "$Id$"

#ifndef CAS
#define CAS
#endif

#include "broker_shm.h"
#include "cas_protocol.h"
#include "broker_cas_cci.h"
#include "cas_common.h"

#define ERROR_INDICATOR_UNSET	0
#define CAS_ERROR_INDICATOR	-1
#define DBMS_ERROR_INDICATOR	-2
#define CAS_NO_ERROR		0
#define ERR_MSG_LENGTH		1024
#define ERR_FILE_LENGTH		256
#define MAX_SHARD_INFO_LENGTH   30

#define MAX_HA_DBINFO_LENGTH    (SRV_CON_DBNAME_SIZE + MAX_CONN_INFO_LENGTH)

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

typedef struct t_error_info T_ERROR_INFO;
struct t_error_info
{
  int err_indicator;
  int err_number;
  char err_string[ERR_MSG_LENGTH];
  char err_file[ERR_FILE_LENGTH];
  int err_line;
};

extern int cas_shard_flag;
extern int shm_shard_id;


extern int restart_is_needed (void);

extern const char *program_name;
extern char broker_name[BROKER_NAME_LEN];

extern int shm_proxy_id;
extern int shm_shard_cas_id;
extern int shm_as_index;
extern T_SHM_APPL_SERVER *shm_appl;
extern T_APPL_SERVER_INFO *as_info;

extern struct timeval tran_start_time;
extern struct timeval query_start_time;
extern int tran_timeout;
extern int query_timeout;
extern INT64 query_cancel_time;
extern char query_cancel_flag;

extern int errors_in_transaction;
extern char stripped_column_name;
extern char cas_client_type;

extern int cas_default_isolation_level;
extern int cas_default_lock_timeout;
extern bool cas_default_ansi_quotes;
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
extern bool cas_default_no_backslash_escapes;
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
extern int cas_send_result_flag;
extern int cas_info_size;

extern T_ERROR_INFO err_info;

extern bool is_xa_prepared (void);
extern void set_xa_prepare_flag (void);
extern void unset_xa_prepare_flag (void);
extern int query_seq_num_next_value (void);
extern int query_seq_num_current_value (void);

extern void set_hang_check_time (void);
extern void unset_hang_check_time (void);

extern bool check_server_alive (const char *db_name, const char *db_host);

extern void cas_set_db_connect_status (int status);
extern int cas_get_db_connect_status (void);
extern T_BROKER_VERSION cas_get_client_version (void);

#endif /* _CAS_H_ */
