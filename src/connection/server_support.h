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

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error server_support.h belongs to server or stand-alone modules
#endif // not SERVER_MODE and not SA_MODE

#include "connection_defs.h"
#include "connection_sr.h"
#include "thread_compat.hpp"
#include "master_heartbeat.h"

// forward definitions
namespace cubthread
{
  class entry_task;
}				// namespace cubthread

enum css_thread_stop_type
{
  THREAD_STOP_WORKERS_EXCEPT_LOGWR,
  THREAD_STOP_LOGWR
};

extern void css_block_all_active_conn (unsigned short stop_phase);

extern THREAD_RET_T THREAD_CALLING_CONVENTION css_master_thread (void);

extern unsigned int css_send_error_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *buffer, int buffer_size);
extern unsigned int css_send_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *buffer, int buffer_size);
extern unsigned int css_send_reply_and_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *reply,
						       int reply_size, char *buffer, int buffer_size);
#if 0
extern unsigned int css_send_reply_and_large_data_to_client (unsigned int eid, char *reply, int reply_size,
							     char *buffer, INT64 buffer_size);
#endif
extern unsigned int css_send_reply_and_2_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *reply,
							 int reply_size, char *buffer1, int buffer1_size, char *buffer2,
							 int buffer2_size);
extern unsigned int css_send_reply_and_3_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *reply,
							 int reply_size, char *buffer1, int buffer1_size, char *buffer2,
							 int buffer2_size, char *buffer3, int buffer3_size);
extern unsigned int css_receive_data_from_client (CSS_CONN_ENTRY * conn, unsigned int eid, char **buffer, int *size);
extern unsigned int css_receive_data_from_client_with_timeout (CSS_CONN_ENTRY * conn, unsigned int eid, char **buffer,
							       int *size, int timeout);
extern unsigned int css_send_abort_to_client (CSS_CONN_ENTRY * conn, unsigned int eid);
extern void
css_initialize_server_interfaces (int (*request_handler)
				  (THREAD_ENTRY * thrd, unsigned int eid, int request, int size, char *buffer),
				  CSS_THREAD_FN connection_error_handler);
extern char *css_pack_server_name (const char *server_name, int *name_length);
extern int css_init (THREAD_ENTRY * thread_p, char *server_name, int server_name_length, int connection_id);
extern char *css_add_client_version_string (THREAD_ENTRY * thread_p, const char *version_string);
#if defined (ENABLE_UNUSED_FUNCTION)
extern char *css_get_client_version_string (void);
#endif
extern void css_cleanup_server_queues (unsigned int eid);
extern void css_end_server_request (CSS_CONN_ENTRY * conn);
extern bool css_is_shutdown_timeout_expired (void);

extern void css_set_ha_num_of_hosts (int num);
extern int css_get_ha_num_of_hosts (void);
extern HA_SERVER_STATE css_ha_server_state (void);
extern bool css_is_ha_repl_delayed (void);
extern void css_set_ha_repl_delayed (void);
extern void css_unset_ha_repl_delayed (void);
extern int css_check_ha_server_state_for_client (THREAD_ENTRY * thread_p, int whence);
extern int css_change_ha_server_state (THREAD_ENTRY * thread_p, HA_SERVER_STATE state, bool force, int timeout,
				       bool heartbeat);
extern int css_notify_ha_log_applier_state (THREAD_ENTRY * thread_p, HA_LOG_APPLIER_STATE state);

extern int css_process_master_hostname (void);
extern const char *get_master_hostname ();

extern void css_push_external_task (THREAD_ENTRY & thread_ref, CSS_CONN_ENTRY * conn, cubthread::entry_task * task);
extern void css_get_thread_stats (UINT64 * stats_out);
extern size_t css_get_num_request_workers (void);
extern size_t css_get_num_connection_workers (void);
extern size_t css_get_num_total_workers (void);
extern bool css_are_all_request_handlers_suspended (void);
extern size_t css_count_transaction_worker_threads (THREAD_ENTRY * thread_p, int tran_index, int client_id);

extern void css_set_thread_info (THREAD_ENTRY * thread_p, int client_id, int rid, int tran_index,
				 int net_request_index);
extern int css_get_client_id (THREAD_ENTRY * thread_p);
extern unsigned int css_get_comm_request_id (THREAD_ENTRY * thread_p);
extern struct css_conn_entry *css_get_current_conn_entry (void);

#if defined (SERVER_MODE)
extern int css_job_queues_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt,
				      void **ptr);
#else // not SERVER_MODE = SA_MODE
// SA_MODE does not have access to server_support.c, but job scan is a common function
// however, on SA_MODE, the result is always empty list
inline int
css_job_queues_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  // suppress all unused parameter warnings
  (void) thread_p;
  (void) show_type;
  (void) arg_values;
  (void) arg_cnt;

  *ptr = NULL;
  return NO_ERROR;
}
#endif // not SERVER_MODE = SA_MODE

#endif /* _SERVER_SUPPORT_H_ */
