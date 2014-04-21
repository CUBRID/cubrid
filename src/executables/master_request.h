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
 * master_request.h - master request handling module
 */

#ifndef _MASTER_REQUEST_H_
#define _MASTER_REQUEST_H_

#ident "$Id$"

#include "connection_defs.h"
#include "master_util.h"

extern SOCKET css_Master_socket_fd[2];
extern struct timeval *css_Master_timeout;
extern int css_Master_timeout_value_in_seconds;
extern int css_Master_timeout_value_in_microseconds;
extern time_t css_Start_time;
extern int css_Total_server_count;
extern int css_Total_request_count;
extern SOCKET_QUEUE_ENTRY *css_Master_socket_anchor;
#if !defined(WINDOWS)
extern pthread_mutex_t css_Master_socket_anchor_lock;
#endif

extern void css_process_info_request (CSS_CONN_ENTRY * conn);
extern void css_process_stop_shutdown (void);
extern void css_process_start_shutdown (SOCKET_QUEUE_ENTRY * sock_entq,
					int timeout, char *buffer);
extern void css_process_heartbeat_request (CSS_CONN_ENTRY * conn);

extern void css_remove_entry_by_conn (CSS_CONN_ENTRY * conn_p,
				      SOCKET_QUEUE_ENTRY ** anchor_p);

extern void css_master_cleanup (int sig);
extern SOCKET_QUEUE_ENTRY *css_add_request_to_socket_queue (CSS_CONN_ENTRY *
							    conn_p,
							    int info_p,
							    char *name_p,
							    SOCKET fd,
							    int fd_type,
							    int pid,
							    SOCKET_QUEUE_ENTRY
							    ** anchor_p);
extern SOCKET_QUEUE_ENTRY *css_return_entry_by_conn (CSS_CONN_ENTRY * conn_p,
						     SOCKET_QUEUE_ENTRY **
						     anchor_p);
#endif /* _MASTER_REQUEST_H_ */
