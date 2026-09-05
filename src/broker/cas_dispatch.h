/*
 *
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
 * cas_dispatch.h - the CAS request dispatch (extracted from cas.c, stage B1)
 */

#ifndef _CAS_DISPATCH_H_
#define _CAS_DISPATCH_H_

#include <sys/time.h>		/* struct timeval (cas_server_access_log) */

#include "cas_common_function.h"
#include "cas_common_vars.h"	/* CAS_TLS, T_REQ_INFO */
#include "cas_net_buf.h"
#include "cas_network.h"	/* MSG_HEADER */
#include "cas_protocol.h"	/* MAX_HA_DBINFO_LENGTH */
#include "porting.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* one full request: header read, dispatch through server_fn_table, reply.
 * Serves both the cub_cas process loop and the in-server CAS speaker. */
  extern FN_RETURN cas_process_request (SOCKET sock_fd, T_NET_BUF * net_buf, T_REQ_INFO * req_info, SOCKET srv_sock_fd);

/* the shard reconnect cluster (moved from cas.c; cas.c's main loop and
 * process_request share this state) */
  extern CAS_TLS char cas_db_name[MAX_HA_DBINFO_LENGTH];
  extern CAS_TLS char cas_db_user[SRV_CON_DBUSER_SIZE];
  extern CAS_TLS char cas_db_passwd[SRV_CON_DBPASSWD_SIZE];
  extern int net_read_process (SOCKET proxy_sock_fd, MSG_HEADER * client_msg_header, T_REQ_INFO * req_info);
  extern void set_db_connection_info (void);
  extern void clear_db_connection_info (void);
  extern bool need_database_reconnect (void);

#ifdef __cplusplus
}
#endif

#if defined (SERVER_MODE)
/* cas_server_support.cpp: the environment the folded speaker expects */
extern void cas_server_speaker_boot_init (const char *db_name);
extern void cas_server_session_slot_begin (int client_type, int client_version, const char *driver_info);
extern void cas_server_session_slot_end (void);
/* access log writes are one shared append-mode file; the CAS's read-modify-
 * rename rotation is not concurrency-safe, so the server serializes it */
extern int cas_server_access_log (struct timeval *start_time, int as_index, int client_ip_addr, char *dbname,
				  char *dbuser, int log_type);
/* ACCESS_CONTROL db:dbuser:ip check at session establishment (B2-D8);
 * 0 = allowed, -1 = rejected.  address is 4 bytes, network order. */
extern int cas_server_acl_check (const char *broker, const char *dbname, const char *dbuser,
				 const unsigned char *address);
extern void cas_server_acl_reload (void);
#endif

#endif /* _CAS_DISPATCH_H_ */
