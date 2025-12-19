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
int shm_as_index;
T_SHM_APPL_SERVER *shm_appl;
T_APPL_SERVER_INFO *as_info;

/* Transaction and query timing */
struct timeval tran_start_time;
struct timeval query_start_time;
int tran_timeout = 0;
int query_timeout = 0;
INT64 query_cancel_time;
char query_cancel_flag;

/* Error handling */
int errors_in_transaction = 0;
T_ERROR_INFO err_info;

/* Client info */
char stripped_column_name;
char cas_client_type;

/* CAS info buffer */
char prev_cas_info[CAS_INFO_SIZE];

/* Network socket */
SOCKET new_req_sock_fd = INVALID_SOCKET;

/* Program info */
const char *program_name;
char broker_name[BROKER_NAME_LEN];

/* CAS configuration */
int cas_default_isolation_level = 0;
int cas_default_lock_timeout = -1;
int cas_send_result_flag = TRUE;
bool cas_default_ansi_quotes = true;
bool cas_default_no_backslash_escapes = true;

/* Request info */
T_REQ_INFO req_info;

/* Additional variables used by CAS and CGW */
int psize_at_start;
int con_status_before_check_cas;
bool is_first_request;
int cas_info_size = CAS_INFO_SIZE;
bool autocommit_deferred = false;
