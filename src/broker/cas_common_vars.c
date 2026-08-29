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
 * cas_common_vars.c - Common global variables for CAS and CGW
 * 
 * This file defines all global variables that are shared between
 * cas_common_lib and the executables (cub_cas, cub_cas_cgw).
 * 
 * These variables are declared as extern in cas_common_vars.h
 */

#ident "$Id$"

#include "cas_common_vars.h"
#include "cas_common.h"
#include "porting.h"

/* Shard ID variables */
int shm_shard_id = SHARD_ID_UNSUPPORTED;
int shm_proxy_id = -1;
int shm_shard_cas_id = -1;
int cas_shard_flag = OFF;

/* Shared memory variables */
CAS_TLS int shm_as_index;
T_SHM_APPL_SERVER *shm_appl;
CAS_TLS T_APPL_SERVER_INFO *as_info;

/* Transaction and query timing */
CAS_TLS struct timeval tran_start_time;
CAS_TLS struct timeval query_start_time;
CAS_TLS int tran_timeout = 0;
CAS_TLS int query_timeout = 0;
CAS_TLS INT64 query_cancel_time;
CAS_TLS char query_cancel_flag;

/* Error handling */
CAS_TLS int errors_in_transaction = 0;
CAS_TLS T_ERROR_INFO err_info;

/* Client info */
CAS_TLS char stripped_column_name;
CAS_TLS char cas_client_type;

/* CAS info buffer */
CAS_TLS char prev_cas_info[CAS_INFO_SIZE];

/* Network socket */
CAS_TLS SOCKET new_req_sock_fd = INVALID_SOCKET;

#if defined(WINDOWS)
/* Request count for restart check (WINDOWS only) */
int cas_req_count = 0;
#endif /* WINDOWS */

/* Program info */
const char *program_name;
char broker_name[BROKER_NAME_LEN];

/* CAS configuration */
CAS_TLS int cas_default_isolation_level = 0;
CAS_TLS int cas_default_lock_timeout = -1;
CAS_TLS int cas_send_result_flag = TRUE;
CAS_TLS bool cas_default_ansi_quotes = true;
CAS_TLS bool cas_default_no_backslash_escapes = true;

/* Request info */
CAS_TLS T_REQ_INFO req_info;

/* Additional variables used by CAS and CGW */
int psize_at_start;
CAS_TLS int con_status_before_check_cas;
CAS_TLS bool is_first_request;
CAS_TLS int cas_info_size = CAS_INFO_SIZE;
CAS_TLS bool autocommit_deferred = false;
