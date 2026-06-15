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
 * connection_sr.h - Client/Server Connection List Management
 */

#ifndef _CONNECTION_SR_H_
#define _CONNECTION_SR_H_

#ident "$Id$"

#include "connection_defs.h"
#include "connection_support.h"
#include "critical_section.h"
#include "error_manager.h"
#include "porting.h"
#include "thread_compat.hpp"

#include <assert.h>
#if !defined(WINDOWS)
#include <pthread.h>
#endif /* not WINDOWS */

#define IP_BYTE_COUNT 5

typedef struct ip_info IP_INFO;
struct ip_info
{
  unsigned char *address_list;
  int num_list;
};

extern CSS_CONN_ENTRY *css_Conn_array;
extern CSS_CONN_ENTRY *css_Active_conn_anchor;

extern SYNC_RWLOCK css_Rwlock_active_conn_anchor;

#define CSS_RWLOCK_ACTIVE_CONN_ANCHOR (&css_Rwlock_active_conn_anchor)
#define CSS_RWLOCK_ACTIVE_CONN_ANCHOR_NAME "CSS_RWLOCK_ACTIVE_CONN_ANCHOR"

#define START_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_write_lock (CSS_RWLOCK_ACTIVE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

#define END_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_write_unlock (CSS_RWLOCK_ACTIVE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

#define START_SHARED_ACCESS_ACTIVE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_read_lock (CSS_RWLOCK_ACTIVE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

#define END_SHARED_ACCESS_ACTIVE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_read_unlock (CSS_RWLOCK_ACTIVE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

extern SYNC_RWLOCK css_Rwlock_free_conn_anchor;

#define CSS_RWLOCK_FREE_CONN_ANCHOR (&css_Rwlock_free_conn_anchor)
#define CSS_RWLOCK_FREE_CONN_ANCHOR_NAME "CSS_RWLOCK_FREE_CONN_ANCHOR"

#define START_EXCLUSIVE_ACCESS_FREE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_write_lock (CSS_RWLOCK_FREE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

#define END_EXCLUSIVE_ACCESS_FREE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_write_unlock (CSS_RWLOCK_FREE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

#define START_SHARED_ACCESS_FREE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_read_lock (CSS_RWLOCK_FREE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

#define END_SHARED_ACCESS_FREE_CONN_ANCHOR(r) \
  do \
    { \
      (r) = rwlock_read_unlock (CSS_RWLOCK_FREE_CONN_ANCHOR); \
      assert ((r) == NO_ERROR); \
    } \
  while (0)

extern int css_Num_access_user;

typedef void *CSS_THREAD_ARG;
typedef int (*CSS_THREAD_FN) (THREAD_ENTRY * thrd, CSS_THREAD_ARG);

extern css_error_code (*css_Connect_handler) (CSS_CONN_ENTRY *);
extern CSS_THREAD_FN css_Request_handler;
extern CSS_THREAD_FN css_Connection_error_handler;

#define CSS_LOG(msg_arg, ...) \
  if (prm_get_bool_value (PRM_ID_CONNECTION_LOGGING)) _er_log_debug (ARG_FILE_LINE, msg_arg "\n", __VA_ARGS__)
#define CSS_LOG_STACK(msg_arg, ...) \
  if (prm_get_bool_value (PRM_ID_CONNECTION_LOGGING)) er_print_callstack (ARG_FILE_LINE, msg_arg "\n", __VA_ARGS__)

extern int css_initialize_conn (CSS_CONN_ENTRY * conn, SOCKET fd);
extern void css_shutdown_conn (CSS_CONN_ENTRY * conn);
extern int css_init_conn_list (void);
extern void css_final_conn_list (void);

extern CSS_CONN_ENTRY *css_make_conn (SOCKET fd);
extern void css_insert_into_active_conn_list (CSS_CONN_ENTRY * conn);
extern void css_dealloc_conn_rmutex (CSS_CONN_ENTRY * conn);

extern int css_get_num_free_conn (void);

extern int css_increment_num_conn (BOOT_CLIENT_TYPE client_type);
extern void css_decrement_num_conn (BOOT_CLIENT_TYPE client_type);

extern void css_free_conn (CSS_CONN_ENTRY * conn);
extern void css_print_conn_entry_info (CSS_CONN_ENTRY * p);
extern void css_print_conn_list (void);
extern void css_print_free_conn_list (void);
extern CSS_CONN_ENTRY *css_connect_to_master_server (int master_port_id, const char *server_name, int name_length);
extern void css_register_handler_routines (css_error_code (*connect_handler) (CSS_CONN_ENTRY * conn),
					   CSS_THREAD_FN request_handler, CSS_THREAD_FN connection_error_handler);

extern CSS_CONN_ENTRY *css_find_conn_by_tran_index (int tran_index);
extern CSS_CONN_ENTRY *css_find_conn_from_fd (SOCKET fd);
extern int css_get_session_ids_for_active_connections (SESSION_ID ** ids, int *count);
extern void css_shutdown_conn_by_tran_index (int tran_index);

extern int css_send_abort_request (CSS_CONN_ENTRY * conn, unsigned short request_id);
extern int css_read_header (CSS_CONN_ENTRY * conn, const NET_HEADER * local_header);
extern int css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size);
extern int css_read_and_queue (CSS_CONN_ENTRY * conn, int *type);
extern int css_receive_data (CSS_CONN_ENTRY * conn, unsigned short req_id, char **buffer, int *buffer_size,
			     int timeout);

extern unsigned int css_return_eid_from_conn (CSS_CONN_ENTRY * conn, unsigned short rid);

extern int css_return_queued_data (CSS_CONN_ENTRY * conn, unsigned short rid, char **buffer, int *bufsize, int *rc);

extern int css_return_queued_error (CSS_CONN_ENTRY * conn, unsigned short request_id, char **buffer, int *buffer_size,
				    int *rc);
extern int css_return_queued_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size);
extern void css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn);
extern int css_queue_user_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int size, char *buffer);
extern unsigned short css_get_request_id (CSS_CONN_ENTRY * conn);
extern int css_set_accessible_ip_info (void);
extern int css_free_accessible_ip_info (void);
extern int css_free_ip_info (IP_INFO * ip_info);
extern int css_read_ip_info (IP_INFO ** out_ip_info, char *filename);
extern int css_check_ip (IP_INFO * ip_info, unsigned char *address);

extern void css_set_user_access_status (const char *db_user, const char *host, const char *program_name);
extern void css_get_user_access_status (int num_user, LAST_ACCESS_STATUS ** access_status_array);
extern void css_free_user_access_status (void);

#endif /* _CONNECTION_SR_H_ */
