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
 * server_support.h -
 */

#ifndef _SERVER_SUPPORT_H_
#define _SERVER_SUPPORT_H_

#ident "$Id$"

#include "connection_defs.h"
#include "connection_sr.h"
#include "thread.h"

extern void css_block_all_active_conn (unsigned short stop_phase);
extern void css_broadcast_shutdown_thread (void);

#if defined(WINDOWS)
extern unsigned __stdcall css_oob_handler_thread (void *arg);
extern unsigned __stdcall css_master_thread (void);
#else /* WINDOWS */
extern void *css_oob_handler_thread (void *arg);
extern void *css_master_thread (void);
#endif /* WINDOWS */

extern unsigned int css_send_error_to_client (CSS_CONN_ENTRY * conn,
					      unsigned int eid, char *buffer,
					      int buffer_size);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int css_number_of_clients (void);
#endif
extern unsigned int
css_send_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
			 char *buffer, int buffer_size);
extern unsigned int
css_send_reply_and_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
				   char *reply, int reply_size,
				   char *buffer, int buffer_size);
#if 0
extern unsigned int
css_send_reply_and_large_data_to_client (unsigned int eid, char *reply,
					 int reply_size, char *buffer,
					 INT64 buffer_size);
#endif
extern unsigned int
css_send_reply_and_2_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid,
				     char *reply, int reply_size,
				     char *buffer1, int buffer1_size,
				     char *buffer2, int buffer2_size);
extern unsigned int
css_send_reply_and_3_data_to_client (CSS_CONN_ENTRY * conn,
				     unsigned int eid, char *reply,
				     int reply_size, char *buffer1,
				     int buffer1_size, char *buffer2,
				     int buffer2_size, char *buffer3,
				     int buffer3_size);
extern unsigned int
css_receive_data_from_client (CSS_CONN_ENTRY * conn, unsigned int eid,
			      char **buffer, int *size);
extern unsigned int
css_receive_data_from_client_with_timeout (CSS_CONN_ENTRY * conn,
					   unsigned int eid, char **buffer,
					   int *size, int timeout);
extern unsigned int
css_send_abort_to_client (CSS_CONN_ENTRY * conn, unsigned int eid);
extern void
css_initialize_server_interfaces (int (*request_handler)
				  (THREAD_ENTRY * thrd, unsigned int eid,
				   int request, int size, char *buffer),
				  CSS_THREAD_FN connection_error_handler);
extern char *css_pack_server_name (const char *server_name, int *name_length);
extern int css_init (char *server_name, int server_name_length,
		     int connection_id);
extern char *css_add_client_version_string (THREAD_ENTRY * thread_p,
					    const char *version_string);
#if defined (ENABLE_UNUSED_FUNCTION)
extern char *css_get_client_version_string (void);
#endif
extern void css_cleanup_server_queues (unsigned int eid);
extern void css_end_server_request (CSS_CONN_ENTRY * conn);
extern bool css_is_shutdown_timeout_expired (void);

extern void css_set_ha_num_of_hosts (int num);
extern int css_get_ha_num_of_hosts (void);
extern HA_SERVER_STATE css_ha_server_state (void);
extern int css_check_ha_server_state_for_client (THREAD_ENTRY * thread_p,
						 int whence);
extern int css_change_ha_server_state (THREAD_ENTRY * thread_p,
				       HA_SERVER_STATE state, bool force,
				       int timeout, bool heartbeat);
extern int css_notify_ha_log_applier_state (THREAD_ENTRY * thread_p,
					    HA_LOG_APPLIER_STATE state);

#endif /* _SERVER_SUPPORT_H_ */
