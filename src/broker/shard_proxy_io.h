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
 * shard_proxy_io.h - 
 */

#ifndef _SHARD_PROXY_IO_H_
#define _SHARD_PROXY_IO_H_

#ident "$Id$"

#include <fcntl.h>
#if !defined(WINDOWS)
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif /* !WINDOWS */

#include "broker_recv_fd.h"
#include "cas_network.h"
#include "shard_proxy_common.h"
#include "shard_metadata.h"

#ifndef min
#define min(a,b)    ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b)    ((a) > (b) ? (a) : (b))
#endif

#if defined(WINDOWS)
#define CLOSESOCKET(fd)                 closesocket((SOCKET)fd)
#define READSOCKET(fd, buf, len)        recv((SOCKET)fd, buf, len, 0)
#define WRITESOCKET(fd, buf, len)       send((SOCKET)fd, buf, len, 0)
#else
#define CLOSESOCKET(fd)                 close(fd)
#define READSOCKET(fd, buf, len)        read(fd, buf, (size_t)len)
#define WRITESOCKET(fd, buf, len)       write(fd, buf, (size_t)len)
#endif

#define DEFAULT_POLL_INTERVAL   1

/* connection protocol(size : information)
PROTOCOL_SIZE : CAS_CONNECTION_REPLY_SIZE 
CAS_INFO_SIZE : CAS_INFO_SIZE
CAS_PID_SIZE : proxy pid 
BROKER_INFO_SIZE : broker_info
CAS_INFO_SIZE : cas_info 
*/

#define PROTOCOL_SIZE           sizeof(int)
#define PROXY_CONNECTION_REPLY_SIZE     (PROTOCOL_SIZE + CAS_INFO_SIZE + CAS_CONNECTION_REPLY_SIZE)

#define SHARD_TEMPORARY_UNAVAILABLE     (-1)

#define PROXY_IO_FROM_CAS               (true)
#define PROXY_IO_FROM_CLIENT    (false)

/* for network global variables */
extern fd_set rset, wset, allset, wnewset, wallset;
extern int maxfd;

extern T_CLIENT_IO_GLOBAL proxy_Client_io;
extern T_SHARD_IO_GLOBAL proxy_Shard_io;

extern void set_data_length (char *buffer, int length);
extern int get_data_length (char *buffer);
extern int get_msg_length (char *buffer);

extern char *proxy_dup_msg (char *msg);

extern void proxy_set_con_status_in_tran (char *msg);
extern void proxy_set_con_status_out_tran (char *msg);
extern void proxy_set_force_out_tran (char *msg);
extern void proxy_unset_force_out_tran (char *msg);
extern int proxy_make_net_buf (T_NET_BUF * net_buf, int size);

extern int proxy_io_make_error_msg (T_BROKER_VERSION client_version,
				    char **buffer, int error_ind,
				    int error_code, char *error_msg,
				    char is_in_tran);
extern int proxy_io_make_no_error (T_BROKER_VERSION client_version,
				   char **buffer);
extern int proxy_io_make_con_close_ok (T_BROKER_VERSION client_version,
				       char **buffer);
extern int proxy_io_make_end_tran_ok (T_BROKER_VERSION client_version,
				      char **buffer);
extern int proxy_io_make_check_cas_ok (T_BROKER_VERSION client_version,
				       char **buffer);

extern int proxy_io_make_end_tran_request (T_BROKER_VERSION client_version,
					   char **buffer, bool commit);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int proxy_io_make_end_tran_commit (char **buffer);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int proxy_io_make_end_tran_abort (T_BROKER_VERSION client_version,
					 char **buffer);
extern int proxy_io_make_close_req_handle_ok (T_BROKER_VERSION client_version,
					      char **buffer, bool is_in_tran);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int proxy_io_make_close_req_handle_in_tran_ok (char **buffer);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int proxy_io_make_close_req_handle_out_tran_ok (T_BROKER_VERSION
						       client_version,
						       char **buffer);

extern int proxy_io_make_get_db_version (T_BROKER_VERSION client_version,
					 char **buffer);
extern int proxy_io_make_client_conn_ok (T_BROKER_VERSION client_version,
					 char **buffer);
extern int proxy_io_make_client_proxy_alive (T_BROKER_VERSION client_version,
					     char **buffer);
extern int proxy_io_make_client_dbinfo_ok (T_BROKER_VERSION client_version,
					   char **buffer);
extern int proxy_io_make_client_acl_fail (T_BROKER_VERSION client_version,
					  char **buffer);


extern int proxy_socket_set_write_event (T_SOCKET_IO * sock_io_p,
					 T_PROXY_EVENT * event_p);
extern void proxy_io_buffer_clear (T_IO_BUFFER * io_buffer);
extern void proxy_socket_io_print (bool print_all);
extern void proxy_client_io_print (bool print_all);
extern char *proxy_str_client_io (T_CLIENT_IO * cli_io_p);

extern void proxy_shard_io_print (bool print_all);
extern char *proxy_str_cas_io (T_CAS_IO * cas_io_p);

extern void proxy_client_io_free (T_CLIENT_IO * cli_io_p);
extern void proxy_client_io_free_by_ctx (int client_id, int ctx_cid,
					 int ctx_uid);
extern T_CLIENT_IO *proxy_client_io_find_by_ctx (int client_id,
						 int ctx_cid,
						 unsigned int ctx_uid);
extern T_CLIENT_IO *proxy_client_io_find_by_fd (int client_id, SOCKET fd);
extern int proxy_client_io_write (T_CLIENT_IO * cli_io_p,
				  T_PROXY_EVENT * event_p);
extern void proxy_cas_io_free_by_ctx (int shard_id, int cas_id, int ctx_cid,
				      unsigned int ctx_uid);
extern T_CAS_IO *proxy_cas_find_io_by_ctx (int shard_id, int cas_id,
					   int ctx_cid, unsigned int ctx_uid);

extern T_CAS_IO *proxy_cas_alloc_by_ctx (int shard_id, int cas_id,
					 int ctx_cid, unsigned int ctx_uid,
					 int timeout);
extern void proxy_cas_release_by_ctx (int shard_id, int cas_id, int ctx_cid,
				      unsigned int ctx_uid);
extern int proxy_cas_io_write (T_CAS_IO * cas_io_p, T_PROXY_EVENT * event_p);

extern int proxy_io_close_all_fd (void);

extern int proxy_io_process (void);
extern int proxy_io_initialize (void);
extern void proxy_io_destroy (void);


extern int proxy_socket_io_delete (SOCKET fd);

extern T_BROKER_VERSION proxy_client_io_version_find_by_ctx (T_PROXY_CONTEXT *
							     ctx_p);
extern T_BROKER_VERSION proxy_client_io_version_find_by_fd (T_SOCKET_IO *
							    sock_io_p);
extern void proxy_available_cas_wait_timer (void);
#endif /* _SHARD_PROXY_IO_H_ */
