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
 * connection_support.h -
 */

#ifndef _CONNECTION_SUPPORT_H_
#define _CONNECTION_SUPPORT_H_

#ident "$Id$"

#include "connection_defs.h"

#if defined (WINDOWS)
#include <winsock2.h>
typedef WSAPOLLFD POLL_FD;
/* Corresponds to the structure we set up on Unix platforms to pass to readv & writev. */
struct iovec
{
  char *iov_base;
  long iov_len;
};
#else
#include <sys/poll.h>
typedef struct pollfd POLL_FD;
#endif

#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_net_send_no_block (SOCKET fd, const char *buffer, int size);
#endif

typedef void (*CSS_SERVER_TIMEOUT_FN) (void);
/* check server alive */
typedef bool (*CSS_CHECK_SERVER_ALIVE_FN) (const char *, const char *);
extern CSS_CHECK_SERVER_ALIVE_FN css_check_server_alive_fn;

extern int css_readn (SOCKET fd, char *ptr, int nbytes, int timeout);
extern void css_read_remaining_bytes (SOCKET fd, int len);

extern int css_net_recv (SOCKET fd, char *buffer, int *maxlen, int timeout);
extern css_error_code css_net_recv_allow_truncated (SOCKET fd, char *buffer, int *maxlen, int *remlen, int timeout);
extern css_error_code css_net_recv_remainder (SOCKET fd, char *buffer, int *maxlen, int timeout);
extern int css_net_send (CSS_CONN_ENTRY * conn, const char *buff, int len, int timeout);
extern int css_net_send_buffer_only (CSS_CONN_ENTRY * conn, const char *buff, int len, int timeout);

extern int css_net_read_header (SOCKET fd, char *buffer, int *maxlen, int timeout);

extern int css_send_request_with_data_buffer (CSS_CONN_ENTRY * conn, int request, unsigned short *rid,
					      const char *arg_buffer, int arg_buffer_size, char *data_buffer,
					      int data_buffer_size);

#if defined (CS_MODE) || defined (SA_MODE)
extern int css_send_request_no_reply (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id, char *arg_buffer,
				      int arg_size);
extern int css_send_req_with_2_buffers (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id,
					char *arg_buffer, int arg_size, char *data_buffer, int data_size,
					char *reply_buffer, int reply_size);
extern int css_send_req_with_3_buffers (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id,
					char *arg_buffer, int arg_size, char *data1_buffer, int data1_size,
					char *data2_buffer, int data2_size, char *reply_buffer, int reply_size);
#if 0
extern int css_send_req_with_large_buffer (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id,
					   char *arg_buffer, int arg_size, char *data_buffer, INT64 data_size,
					   char *reply_buffer, int reply_size);
#endif
#endif /* CS_MODE || SA_MODE */

extern int css_send_request (CSS_CONN_ENTRY * conn, int request, unsigned short *rid, const char *arg_buffer,
			     int arg_buffer_size);

extern int css_send_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer, int buffer_size);
#if defined (SERVER_MODE)
extern int css_send_two_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer1, int buffer1_size,
			      const char *buffer2, int buffer2_size);
extern int css_send_three_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer1, int buffer1_size,
				const char *buffer2, int buffer2_size, const char *buffer3, int buffer3_size);
extern int css_send_four_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer1, int buffer1_size,
			       const char *buffer2, int buffer2_size, const char *buffer3, int buffer3_size,
			       const char *buffer4, int buffer4_size);
#endif /* SERVER_MODE */
extern int css_send_error (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer, int buffer_size);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_send_large_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char **buffers, int *buffers_size,
				int num_buffers);
extern int css_local_host_name (CSS_CONN_ENTRY * conn, char *hostname, size_t namelen);

extern int css_peer_host_name (CSS_CONN_ENTRY * conn, char *hostname, size_t namelen);
#endif
extern const char *css_ha_server_state_string (HA_SERVER_STATE state);
extern const char *css_ha_applier_state_string (HA_LOG_APPLIER_STATE state);
extern const char *css_ha_mode_string (HA_MODE mode);

#if !defined (SERVER_MODE)
extern void css_register_server_timeout_fn (CSS_SERVER_TIMEOUT_FN callback_fn);
extern void css_register_check_server_alive_fn (CSS_CHECK_SERVER_ALIVE_FN callback_fn);
#endif /* !SERVER_MODE */

extern int css_send_magic (CSS_CONN_ENTRY * conn);
extern int css_check_magic (CSS_CONN_ENTRY * conn);

extern int css_check_magic_with_socket (SOCKET fd);

extern int css_user_access_status_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt,
					      void **ptr);
extern int css_platform_independent_poll (POLL_FD * fds, int num_of_fds, int timeout);
extern int css_vector_send (SOCKET fd, struct iovec *vec[], int *len, int bytes_written, int timeout);
extern void css_set_net_header (NET_HEADER * header_p, int type, short function_code, int request_id, int buffer_size,
				int transaction_id, int invalidate_snapshot, int db_error);
extern void css_set_io_vector (struct iovec *vec1_p, struct iovec *vec2_p, const char *buff, int len, int *templen);
extern int css_send_io_vector_with_socket (SOCKET & socket, struct iovec *vec_p, ssize_t total_len, int vector_length,
					   int timeout);
extern int css_send_magic_with_socket (SOCKET & socket);
extern int css_net_send_with_socket (SOCKET & socket, const char *buff, int len, int timeout);
extern int css_net_send3_with_socket (SOCKET & socket, const char *buff1, int len1, const char *buff2, int len2,
				      const char *buff3, int len3);
extern int css_send_request_with_socket (SOCKET & socket, int command, unsigned short *request_id,
					 const char *arg_buffer, int arg_buffer_size);
extern CSS_CONN_ENTRY *css_connect_to_master_server (int master_port_id, const char *message_to_master,
						     int message_to_master_length);
extern CSS_CONN_ENTRY *css_common_connect (CSS_CONN_ENTRY * conn, unsigned short *rid, const char *host_name,
					   int connect_type, const char *message, int message_length, int port,
					   int timeout, bool send_magic);
#endif /* _CONNECTION_SUPPORT_H_ */
