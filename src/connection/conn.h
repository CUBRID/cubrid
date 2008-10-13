/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * conn.h - Client/Server Connection List Management
 */

#ifndef _CONN_H_
#define _CONN_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <pthread.h>
#endif /* not WINDOWS */

#include "thread.h"
#include "defs.h"
#include "error_manager.h"
#include "critical_section.h"
#include "thread_impl.h"

extern CSS_CONN_ENTRY *css_Conn_array;
extern CSS_CONN_ENTRY *css_Active_conn_anchor;
extern CSS_CRITICAL_SECTION css_Active_conn_csect;

extern int (*css_Connect_handler) (CSS_CONN_ENTRY *);
extern CSS_THREAD_FN css_Request_handler;
extern CSS_THREAD_FN css_Connection_error_handler;

extern int css_initialize_conn (CSS_CONN_ENTRY * conn, int fd);
extern void css_shutdown_conn (CSS_CONN_ENTRY * conn);
extern int css_init_conn_list (void);
extern void css_final_conn_list (void);

extern CSS_CONN_ENTRY *css_make_conn (int fd);
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
extern CSS_CONN_ENTRY *css_find_conn_from_fd (int fd);
extern void css_shutdown_conn_by_tran_index (int tran_index);

extern int css_net_send_no_block (int fd, const char *buffer, int size);
extern int css_readn (int fd, char *ptr, int nbytes);
extern void css_read_remaining_bytes (int fd, int len);
extern int css_net_recv (int fd, char *buffer, int *maxlen);
extern int css_net_send (CSS_CONN_ENTRY * conn, const char *buff, int len);
extern int css_net_read_header (int fd, char *buffer, int *maxlen);

extern int css_send_request_with_data_buffer (CSS_CONN_ENTRY * conn,
					      int request,
					      unsigned short *request_id,
					      const char *arg_buffer,
					      int arg_size,
					      char *reply_buffer,
					      int reply_size);
extern int css_send_request (CSS_CONN_ENTRY * conn, int command,
			     unsigned short *request_id,
			     const char *arg_buffer, int arg_buffer_size);
extern int css_send_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			  const char *buffer, int buffer_size);
extern int css_send_two_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			      const char *buffer1, int buffer1_size,
			      const char *buffer2, int buffer2_size);
extern int css_send_three_data (CSS_CONN_ENTRY * conn, unsigned short rid,
				const char *buffer1, int buffer1_size,
				const char *buffer2, int buffer2_size,
				const char *buffer3, int buffer3_size);
extern int css_send_four_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			       const char *buffer1, int buffer1_size,
			       const char *buffer2, int buffer2_size,
			       const char *buffer3, int buffer3_size,
			       const char *buffer4, int buffer4_size);

extern int css_send_error (CSS_CONN_ENTRY * conn, unsigned short rid,
			   const char *buffer, int buffer_size);
extern int css_send_oob (CSS_CONN_ENTRY * conn, char byte,
			 const char *buffer, int buffer_size);
extern int css_send_abort_request (CSS_CONN_ENTRY * conn,
				   unsigned short request_id);
extern int css_read_header (CSS_CONN_ENTRY * conn,
			    const NET_HEADER * local_header);
extern int css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
				int *request, int *buffer_size);
extern int css_read_and_queue (CSS_CONN_ENTRY * conn, int *type);
extern int css_receive_data (CSS_CONN_ENTRY * conn, unsigned short req_id,
			     char **buffer, int *buffer_size);

extern unsigned int css_return_eid_from_conn (CSS_CONN_ENTRY * conn,
					      unsigned short rid);
extern unsigned short css_return_rid_from_eid (unsigned int eid);
extern unsigned short css_return_entry_id_from_eid (unsigned int eid);

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
#endif /* _CONN_H_ */
