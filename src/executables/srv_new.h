/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * srv_new.h -
 */

#ifndef _SRV_NEW_H_
#define _SRV_NEW_H_

#ident "$Id$"

#include "defs.h"
#include "conn.h"
#include "thread_impl.h"

extern void css_block_all_active_conn (void);
extern void css_broadcast_shutdown_thread (void);

#if defined(WINDOWS)
extern unsigned __stdcall css_oob_handler_thread (void *arg);
extern unsigned __stdcall css_master_thread (void);
#else /* WINDOWS */
extern void *css_oob_handler_thread (void *arg);
extern void *css_master_thread (void);
#endif /* WINDOWS */

extern void css_set_error_code (unsigned int eid, int error_code);
extern unsigned int css_send_error_to_client (unsigned int eid, char *buffer,
					      int buffer_size);
extern int css_number_of_clients (void);
extern unsigned int css_send_data_to_client (unsigned int eid, char *buffer,
					     int buffer_size);
extern unsigned int css_send_reply_and_data_to_client (unsigned int eid,
						       char *reply,
						       int reply_size,
						       char *buffer,
						       int buffer_size);
extern unsigned int css_send_reply_and_2_data_to_client (unsigned int eid,
							 char *reply,
							 int reply_size,
							 char *buffer1,
							 int buffer1_size,
							 char *buffer2,
							 int buffer2_size);
extern unsigned int css_send_reply_and_3_data_to_client (unsigned int eid,
							 char *reply,
							 int reply_size,
							 char *buffer1,
							 int buffer1_size,
							 char *buffer2,
							 int buffer2_size,
							 char *buffer3,
							 int buffer3_size);
extern unsigned int css_receive_data_from_client (unsigned int eid,
						  char **buffer, int *size);
extern unsigned int css_send_abort_to_client (unsigned int eid);
extern void css_initialize_server_interfaces (int (*request_handler)
					      (THREAD_ENTRY * thrd,
					       unsigned int eid, int request,
					       int size, char *buffer),
					      CSS_THREAD_FN
					      connection_error_handler);
extern char *css_pack_server_name (const char *server_name, int *name_length);
extern int css_init (char *server_name, int server_name_length,
		     int connection_id);
extern char *css_add_client_version_string (const char *version_string);
extern char *css_get_client_version_string (void);
extern void css_cleanup_server_queues (unsigned int eid);
extern void css_end_server_request (CSS_CONN_ENTRY * conn);

#endif /* _SRV_NEW_H_ */
