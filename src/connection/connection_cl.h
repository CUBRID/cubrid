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
 * connection_cl.h -
 */

#ifndef _CONNECTION_CL_H_
#define _CONNECTION_CL_H_

#ident "$Id$"

#include "connection_defs.h"
#include "connection_support.h"

/* the order to connect to db-hosts in databases.txt */
#define DB_CONNECT_ORDER_SEQ         0
#define DB_CONNECT_ORDER_RANDOM      1

/* abnormal DB host status */
#define DB_HS_NORMAL                    0x00000000
#define DB_HS_CONN_TIMEOUT              0x00000001
#define DB_HS_CONN_FAILURE              0x00000002
#define DB_HS_MISMATCHED_RW_MODE        0x00000004
#define DB_HS_HA_DELAYED                0x00000008
#define DB_HS_NON_PREFFERED_HOSTS       0x00000010
#define DB_HS_UNUSABLE_DATABASES        0x00000020

#define DB_HS_RECONNECT_INDICATOR \
  (DB_HS_MISMATCHED_RW_MODE | DB_HS_HA_DELAYED | DB_HS_NON_PREFFERED_HOSTS)

extern void css_shutdown_conn (CSS_CONN_ENTRY * conn);
extern CSS_CONN_ENTRY *css_make_conn (SOCKET fd);
extern void css_free_conn (CSS_CONN_ENTRY * conn);

extern CSS_CONN_ENTRY *css_common_connect (const char *host_name, CSS_CONN_ENTRY * conn, int connect_type,
					   const char *server_name, int server_name_length, int port, int timeout,
					   unsigned short *rid, bool send_magic);
extern CSS_CONN_ENTRY *css_connect_to_master_server (int master_port_id, const char *server_name, int name_length);

extern CSS_CONN_ENTRY *css_find_exception_conn (void);
extern int css_receive_error (CSS_CONN_ENTRY * conn, unsigned short req_id, char **buffer, int *buffer_size);

extern CSS_CONN_ENTRY *css_connect_to_cubrid_server (char *host_name, char *server_name);
extern CSS_CONN_ENTRY *css_connect_to_master_for_info (const char *host_name, int port_id, unsigned short *rid);
extern CSS_CONN_ENTRY *css_connect_to_master_timeout (const char *host_name, int port_id, int timeout,
						      unsigned short *rid);
extern bool css_does_master_exist (int port_id);

extern int css_receive_data (CSS_CONN_ENTRY * conn, unsigned short rid, char **buffer, int *size, int timeout);
extern int css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *arg_buffer_size);
extern int css_send_close_request (CSS_CONN_ENTRY * conn);

extern int css_test_for_open_conn (CSS_CONN_ENTRY * conn);
extern CSS_CONN_ENTRY *css_find_conn_from_fd (SOCKET fd);
extern unsigned short css_get_request_id (CSS_CONN_ENTRY * conn);

extern char *css_return_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int *buffer_size);
extern bool css_is_valid_request_id (CSS_CONN_ENTRY * conn, unsigned short request_id);

extern int css_return_queued_error (CSS_CONN_ENTRY * conn, unsigned short request_id, char **buffer, int *buffer_size,
				    int *rc);
extern void css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn);
extern int css_read_one_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size);
extern void css_close_conn (CSS_CONN_ENTRY * conn);
extern CSS_CONN_ENTRY *css_server_connect_part_two (char *host_name, CSS_CONN_ENTRY * conn, int port_id, unsigned short *rid);
#endif /* _CONNECTION_CL_H_ */
