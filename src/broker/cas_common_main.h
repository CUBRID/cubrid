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
 * cas_common_main.h 
 */

#ifndef	_CAS_COMMON_MAIN_H_
#define	_CAS_COMMON_MAIN_H_

#ident "$Id$"

#include "cas_protocol.h"
#include "cas_network.h"
#include "cas_common_function.h"

int cas_get_graceful_down_timeout (void);
void cas_sig_handler (int signo);
void cas_final (void);
void cas_free (bool from_sighandler);

typedef void (*cas_cleanup_callback_t) (void);

typedef void (*cas_database_shutdown_callback_t) (bool request_server);

void cas_set_cleanup_callback (cas_cleanup_callback_t callback);
void cas_set_database_shutdown_callback (cas_database_shutdown_callback_t callback);

void set_hang_check_time (void);
void unset_hang_check_time (void);
bool check_server_alive (const char *db_name, const char *db_host);
extern int restart_is_needed (void);
extern int query_seq_num_next_value (void);
extern int query_seq_num_current_value (void);
T_BROKER_VERSION cas_get_client_version (void);
int net_read_header_keep_con_on (SOCKET clt_sock_fd, MSG_HEADER * client_msg_header);
int net_read_int_keep_con_auto (SOCKET clt_sock_fd, MSG_HEADER * client_msg_header, T_REQ_INFO * req_info,
				SOCKET srv_sock_fd);
void set_cas_info_size (void);
#if !defined(WINDOWS)
void query_cancel (int signo);
#endif
#if defined(WINDOWS)
LONG WINAPI CreateMiniDump (struct _EXCEPTION_POINTERS *pException);
#endif

#define FUNC_NEEDS_RESTORING_CON_STATUS(func_code) \
  (((func_code) == CAS_FC_GET_DB_PARAMETER) \
   ||((func_code) == CAS_FC_SET_DB_PARAMETER) \
   ||((func_code) == CAS_FC_CLOSE_REQ_HANDLE) \
   ||((func_code) == CAS_FC_GET_DB_VERSION) \
   ||((func_code) == CAS_FC_GET_ATTR_TYPE_STR) \
   ||((func_code) == CAS_FC_CURSOR_CLOSE) \
   ||((func_code) == CAS_FC_END_SESSION)  \
   ||((func_code) == CAS_FC_CAS_CHANGE_MODE))

/* DB connection information structure */
typedef struct
{
  char *db_name;
  char *db_user;
  char *db_passwd;
  char *url;
  char *db_sessionid;
} DB_CONN_INFO;

/* DB connection callback function types */
typedef int (*cas_db_connect_fn_t) (SOCKET client_sock_fd, const char *db_name, const char *db_user,
				    const char *db_passwd, const char *url, T_REQ_INFO * req_info, char *cas_info);
typedef int (*cas_db_pre_connect_fn_t) (const char *db_name, const char *db_user,
					const char *db_passwd, const char *url, void *context);
typedef void (*cas_db_post_connect_fn_t) (void *context, struct timeval * cas_start_time, int shm_as_index,
					  int client_ip_addr, char *db_name, char *db_user, const char *url,
					  bool is_new_connection);
typedef void (*cas_cleanup_session_fn_t) (void);
typedef FN_RETURN (*cas_process_request_fn_t) (SOCKET sock_fd, T_NET_BUF * net_buf, T_REQ_INFO * req_info);
typedef void (*cas_set_session_id_fn_t) (T_CAS_PROTOCOL protocol, char *session);
typedef void (*cas_send_connect_reply_fn_t) (T_CAS_PROTOCOL protocol, SOCKET client_sock_fd, char *cas_info);

/* cas_main() operations structure */
typedef struct
{
  int (*init_specific) (void);	/* Mode-specific initialization (e.g., cgw_init) */
  cas_db_pre_connect_fn_t pre_db_connect;	/* Pre-DB connection processing */
  cas_db_connect_fn_t db_connect;	/* DB connection function */
  cas_db_post_connect_fn_t post_db_connect;	/* Post-DB connection processing */
  cas_cleanup_session_fn_t cleanup_session;	/* Session cleanup */
  cas_process_request_fn_t process_request;	/* Request processing */
  cas_set_session_id_fn_t set_session_id;	/* Set session ID (cas.c only) */
  cas_send_connect_reply_fn_t send_connect_reply;	/* Send connect reply */
  void *context;		/* Context */
} CAS_MAIN_OPS;

int cas_main_loop (CAS_MAIN_OPS * ops);
int cas_main_init (T_NET_BUF * net_buf, SOCKET * srv_sock_fd);
int cas_accept_client (SOCKET br_sock_fd, SOCKET * client_sock_fd, int *client_ip_addr);
int cas_parse_db_info (char *read_buf, int db_info_size, T_REQ_INFO * req_info, DB_CONN_INFO * conn_info);
int cas_handle_db_connection (SOCKET client_sock_fd, T_REQ_INFO * req_info,
			      DB_CONN_INFO * conn_info, char *cas_info, int client_ip_addr, CAS_MAIN_OPS * ops,
			      bool is_new_connection);
void cas_finish_session (SOCKET client_sock_fd, bool ssl_client);

extern FN_RETURN cas_main_fn_ret;

#endif /* _CAS_COMMON_MAIN_H_ */
