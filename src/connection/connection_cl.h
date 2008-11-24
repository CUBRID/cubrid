/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */


/*
 * connection_cl.h -
 */

#ifndef _CONNECTION_CL_H_
#define _CONNECTION_CL_H_

#ident "$Id$"

#include "connection_defs.h"

extern void css_shutdown_conn (CSS_CONN_ENTRY * conn);
extern CSS_CONN_ENTRY *css_make_conn (int);
extern void css_free_conn (CSS_CONN_ENTRY * conn);

extern int css_net_send_no_block (int fd, const char *buffer, int size);

extern int css_net_send (CSS_CONN_ENTRY * conn, const char *buff, int len);
extern int css_net_recv (int, char *, int *);
extern int css_net_read_header (int fd, char *buffer, int *maxlen);

extern CSS_CONN_ENTRY *css_connect_to_master_server (int master_port_id,
						     const char *server_name,
						     int name_length);

extern CSS_CONN_ENTRY *css_find_exception_conn (void);
extern void css_read_remaining_bytes (int fd, int len);
extern int css_receive_oob (CSS_CONN_ENTRY * conn, char **buffer,
			    int *buffer_size);

extern int css_receive_error (CSS_CONN_ENTRY * conn, unsigned short req_id,
			      char **buffer, int *buffer_size);

extern CSS_CONN_ENTRY *css_connect_to_cubrid_server (char *host_name,
						     char *server_name);
extern CSS_CONN_ENTRY *css_connect_to_master_for_info (const char *host_name,
						       int port_id,
						       unsigned short *rid);
extern bool css_does_master_exist (int port_id);

extern int css_receive_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			     char **buffer, int *size);
extern int css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
				int *request, int *arg_buffer_size);
extern int css_send_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			  const char *buffer, int buffer_size);
extern int css_send_oob (CSS_CONN_ENTRY * conn, char data, const char *buffer,
			 int buffer_size);
extern int css_send_error (CSS_CONN_ENTRY * conn, unsigned short rid,
			   const char *buffer, int buffer_size);
extern int css_send_close_request (CSS_CONN_ENTRY * conn);

extern int css_send_request (CSS_CONN_ENTRY * conn, int request,
			     unsigned short *rid, const char *arg_buffer,
			     int arg_buffer_size);
extern int css_send_request_with_data_buffer (CSS_CONN_ENTRY * conn,
					      int request,
					      unsigned short *rid,
					      const char *arg_buffer,
					      int arg_buffer_size,
					      char *data_buffer,
					      int data_buffer_size);
extern int css_send_oob_request_with_data_buffer (CSS_CONN_ENTRY * conn,
						  char byte,
						  int request,
						  unsigned short *request_id,
						  char *arg_buffer,
						  int arg_size);
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

extern int css_test_for_open_conn (CSS_CONN_ENTRY * conn);
extern CSS_CONN_ENTRY *css_find_conn_from_fd (int fd);
extern unsigned short css_get_request_id (CSS_CONN_ENTRY * conn);
extern int css_readn (int fd, char *ptr, int nbytes);

extern char *css_return_data_buffer (CSS_CONN_ENTRY * conn,
                                     unsigned short request_id,
                                     int *buffer_size);
extern char *css_return_oob_buffer (int *buffer_size);

extern int css_return_queued_error (CSS_CONN_ENTRY * conn,
                                    unsigned short request_id, char **buffer,
                                    int *buffer_size, int *rc);
extern void css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn);
#endif /* _CONNECTION_CL_H_ */
