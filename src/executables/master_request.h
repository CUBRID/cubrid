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
extern void css_process_start_shutdown (SOCKET_QUEUE_ENTRY * sock_entq, int timeout, char *buffer);
extern void css_process_heartbeat_request (CSS_CONN_ENTRY * conn);

extern void css_remove_entry_by_conn (CSS_CONN_ENTRY * conn_p, SOCKET_QUEUE_ENTRY ** anchor_p);

extern void css_master_cleanup (int sig);

extern SOCKET_QUEUE_ENTRY *css_return_entry_of_server (char *name_p, SOCKET_QUEUE_ENTRY * anchor_p);


extern SOCKET_QUEUE_ENTRY *css_add_request_to_socket_queue (CSS_CONN_ENTRY * conn_p, int info_p, char *name_p,
							    SOCKET fd, int fd_type, int pid,
							    SOCKET_QUEUE_ENTRY ** anchor_p);
extern SOCKET_QUEUE_ENTRY *css_return_entry_by_conn (CSS_CONN_ENTRY * conn_p, SOCKET_QUEUE_ENTRY ** anchor_p);
#endif /* _MASTER_REQUEST_H_ */
