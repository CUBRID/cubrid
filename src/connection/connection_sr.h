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
 * connection_sr.h - Client/Server Connection List Management
 */

#ifndef _CONNECTION_SR_H_
#define _CONNECTION_SR_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <pthread.h>
#endif /* not WINDOWS */

#include "porting.h"
#include "thread.h"
#include "connection_defs.h"
#include "connection_support.h"
#include "error_manager.h"
#include "critical_section.h"
#include "thread.h"

#define IP_BYTE_COUNT 5

typedef struct ip_info IP_INFO;
struct ip_info
{
  unsigned char *address_list;
  int num_list;
};

extern CSS_CONN_ENTRY *css_Conn_array;
extern CSS_CONN_ENTRY *css_Active_conn_anchor;
extern CSS_CRITICAL_SECTION css_Active_conn_csect;

extern int (*css_Connect_handler) (CSS_CONN_ENTRY *);
extern CSS_THREAD_FN css_Request_handler;
extern CSS_THREAD_FN css_Connection_error_handler;

extern int css_initialize_conn (CSS_CONN_ENTRY * conn, SOCKET fd);
extern void css_shutdown_conn (CSS_CONN_ENTRY * conn);
extern int css_init_conn_list (void);
extern void css_final_conn_list (void);

extern CSS_CONN_ENTRY *css_make_conn (SOCKET fd);
extern void css_insert_into_active_conn_list (CSS_CONN_ENTRY * conn);
extern void css_dealloc_conn_csect (CSS_CONN_ENTRY * conn);

extern void css_free_conn (CSS_CONN_ENTRY * conn);
extern void css_print_conn_entry_info (CSS_CONN_ENTRY * p);
extern void css_print_conn_list (void);
extern void css_print_free_conn_list (void);
extern CSS_CONN_ENTRY *css_connect_to_master_server (int master_port_id,
						     const char *server_name,
						     int name_length);
extern void css_register_handler_routines (int (*connect_handler)
					   (CSS_CONN_ENTRY * conn),
					   CSS_THREAD_FN request_handler,
					   CSS_THREAD_FN
					   connection_error_handler);

extern CSS_CONN_ENTRY *css_find_conn_by_tran_index (int tran_index);
extern CSS_CONN_ENTRY *css_find_conn_from_fd (SOCKET fd);
extern int css_get_session_ids_for_active_connections (SESSION_ID ** ids,
						       int *count);
extern void css_shutdown_conn_by_tran_index (int tran_index);

extern int css_send_abort_request (CSS_CONN_ENTRY * conn,
				   unsigned short request_id);
extern int css_read_header (CSS_CONN_ENTRY * conn,
			    const NET_HEADER * local_header);
extern int css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
				int *request, int *buffer_size);
extern int css_read_and_queue (CSS_CONN_ENTRY * conn, int *type);
extern int css_receive_data (CSS_CONN_ENTRY * conn, unsigned short req_id,
			     char **buffer, int *buffer_size, int timeout);

extern unsigned int css_return_eid_from_conn (CSS_CONN_ENTRY * conn,
					      unsigned short rid);

extern int css_return_queued_data (CSS_CONN_ENTRY * conn,
				   unsigned short rid, char **buffer,
				   int *bufsize, int *rc);

extern int css_return_queued_error (CSS_CONN_ENTRY * conn,
				    unsigned short request_id, char **buffer,
				    int *buffer_size, int *rc);
extern int css_return_queued_request (CSS_CONN_ENTRY * conn,
				      unsigned short *rid,
				      int *request, int *buffer_size);
extern void css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn);
extern int css_queue_user_data_buffer (CSS_CONN_ENTRY * conn,
				       unsigned short request_id,
				       int size, char *buffer);
extern unsigned short css_get_request_id (CSS_CONN_ENTRY * conn);
extern int css_set_accessible_ip_info (void);
extern int css_free_accessible_ip_info (void);
extern int css_free_ip_info (IP_INFO * ip_info);
extern int css_read_ip_info (IP_INFO ** out_ip_info, char *filename);
extern int css_check_ip (IP_INFO * ip_info, unsigned char *address);

#endif /* _CONNECTION_SR_H_ */
