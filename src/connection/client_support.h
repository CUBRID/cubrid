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
 * client_support.h -
 */

#ifndef _CLIENT_SUPPORT_H_
#define _CLIENT_SUPPORT_H_

#ident "$Id$"

#include "connection_defs.h"

extern int css_Errno;
extern CSS_MAP_ENTRY *css_Client_anchor;

extern int css_client_init (int sockid, const char *server_name, const char *host_name);
#if defined(ENABLE_UNUSED_FUNCTION)
extern unsigned int css_send_request_to_server (char *host, int request, char *arg_buffer, int arg_buffer_size);
#endif
extern unsigned int css_send_request_to_server_with_buffer (char *host, int request, char *arg_buffer,
							    int arg_buffer_size, char *data_buffer,
							    int data_buffer_size);
extern unsigned int css_send_req_to_server (char *host, int request, char *arg_buffer, int arg_buffer_size,
					    char *data_buffer, int data_buffer_size, char *reply_buffer,
					    int reply_size);
#if 0
extern unsigned int css_send_req_to_server_with_large_data (char *host, int request, char *arg_buffer,
							    int arg_buffer_size, char *data_buffer,
							    INT64 data_buffer_size, char *reply_buffer, int reply_size);
#endif

extern unsigned int css_send_req_to_server_2_data (char *host, int request, char *arg_buffer, int arg_buffer_size,
						   char *data1_buffer, int data1_buffer_size, char *data2_buffer,
						   int data2_buffer_size, char *reply_buffer, int reply_size);
extern unsigned int css_send_req_to_server_no_reply (char *host, int request, char *arg_buffer, int arg_buffer_size);
extern int css_queue_receive_data_buffer (unsigned int eid, char *buffer, int buffer_size);
extern unsigned int css_send_error_to_server (char *host, unsigned int eid, char *buffer, int buffer_size);
extern unsigned int css_send_data_to_server (char *host, unsigned int eid, char *buffer, int buffer_size);
#if defined (ENABLE_UNUSED_FUNCTION)
extern unsigned int css_receive_error_from_server (unsigned int eid, char **buffer, int *size);
#endif
extern unsigned int css_receive_data_from_server (unsigned int eid, char **buffer, int *size);
extern unsigned int css_receive_data_from_server_with_timeout (unsigned int eid, char **buffer, int *size, int timeout);
extern void css_terminate (bool server_error);
extern void css_cleanup_client_queues (char *host_name);
extern HA_SERVER_STATE css_ha_server_state (void);

#endif /* _CLIENT_SUPPORT_H_ */
