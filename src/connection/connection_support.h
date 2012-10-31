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
 * connection_support.h -
 */

#ifndef _CONNECTION_SUPPORT_H_
#define _CONNECTION_SUPPORT_H_

#ident "$Id$"

#include "connection_defs.h"

#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_net_send_no_block (SOCKET fd, const char *buffer, int size);
#endif

typedef void (*CSS_SERVER_TIMEOUT_FN) (void);

extern int css_readn (SOCKET fd, char *ptr, int nbytes, int timeout);
extern void css_read_remaining_bytes (SOCKET fd, int len);

extern int css_net_recv (SOCKET fd, char *buffer, int *maxlen, int timeout);
extern int css_net_send (CSS_CONN_ENTRY * conn, const char *buff, int len,
			 int timeout);
extern int css_net_send_buffer_only (CSS_CONN_ENTRY * conn,
				     const char *buff, int len, int timeout);

extern int css_net_read_header (SOCKET fd, char *buffer, int *maxlen);

extern int css_send_request_with_data_buffer (CSS_CONN_ENTRY * conn,
					      int request,
					      unsigned short *rid,
					      const char *arg_buffer,
					      int arg_buffer_size,
					      char *data_buffer,
					      int data_buffer_size);

#if defined (CS_MODE) || defined (SA_MODE)
extern int css_send_request_no_reply (CSS_CONN_ENTRY * conn, int request,
				      unsigned short *request_id,
				      char *arg_buffer, int arg_size);
extern int css_send_req_with_2_buffers (CSS_CONN_ENTRY * conn,
					int request,
					unsigned short *request_id,
					char *arg_buffer,
					int arg_size,
					char *data_buffer,
					int data_size,
					char *reply_buffer, int reply_size);
extern int css_send_req_with_3_buffers (CSS_CONN_ENTRY * conn,
					int request,
					unsigned short *request_id,
					char *arg_buffer,
					int arg_size,
					char *data1_buffer,
					int data1_size,
					char *data2_buffer,
					int data2_size,
					char *reply_buffer, int reply_size);
#if 0
extern int
css_send_req_with_large_buffer (CSS_CONN_ENTRY * conn, int request,
				unsigned short *request_id, char *arg_buffer,
				int arg_size, char *data_buffer,
				INT64 data_size, char *reply_buffer,
				int reply_size);
#endif
#endif /* CS_MODE || SA_MODE */

extern int css_send_request (CSS_CONN_ENTRY * conn, int request,
			     unsigned short *rid, const char *arg_buffer,
			     int arg_buffer_size);

extern int css_send_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			  const char *buffer, int buffer_size);
#if defined (SERVER_MODE)
extern int css_send_two_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			      const char *buffer1, int buffer1_size,
			      const char *buffer2, int buffer2_size);
extern int css_send_three_data (CSS_CONN_ENTRY * conn, unsigned short rid,
				const char *buffer1, int buffer1_size,
				const char *buffer2, int buffer2_size,
				const char *buffer3, int buffer3_size);
extern int css_send_four_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			       const char *buffer1, int buffer1_size,
			       const char *buffer2, int buffer2_size,
			       const char *buffer3, int buffer3_size,
			       const char *buffer4, int buffer4_size);
#endif /* SERVER_MODE */
extern int css_send_error (CSS_CONN_ENTRY * conn, unsigned short rid,
			   const char *buffer, int buffer_size);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_send_large_data (CSS_CONN_ENTRY * conn, unsigned short rid,
				const char **buffers, int *buffers_size,
				int num_buffers);
extern int css_local_host_name (CSS_CONN_ENTRY * conn, char *hostname,
				size_t namelen);

extern int css_peer_host_name (CSS_CONN_ENTRY * conn, char *hostname,
			       size_t namelen);
#endif
extern const char *css_ha_server_state_string (HA_SERVER_STATE state);
extern const char *css_ha_applier_state_string (HA_LOG_APPLIER_STATE state);
extern const char *css_ha_mode_string (HA_MODE mode);

#if !defined (SERVER_MODE)
extern void css_register_server_timeout_fn (CSS_SERVER_TIMEOUT_FN
					    callback_fn);
#endif /* !SERVER_MODE */
#endif /* _CONNECTION_SUPPORT_H_ */

extern int css_send_magic (CSS_CONN_ENTRY * conn);
extern int css_check_magic (CSS_CONN_ENTRY * conn);
