/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * top.h - 
 */

#ifndef _TOP_H_
#define _TOP_H_

#ident "$Id$"

#include "defs.h"

extern int css_Errno;
extern CSS_MAP_ENTRY *css_Client_anchor;

extern int css_client_init (int sockid,
			    void (*oob_function) (char, unsigned int),
			    const char *server_name, const char *host_name);
extern unsigned int css_send_request_to_server (char *host, int request,
						char *arg_buffer,
						int arg_buffer_size);
extern unsigned int css_send_request_to_server_with_buffer (char *host,
							    int request,
							    char *arg_buffer,
							    int
							    arg_buffer_size,
							    char *data_buffer,
							    int
							    data_buffer_size);
extern unsigned int css_send_req_to_server (char *host,
					    int request, char *arg_buffer,
					    int arg_buffer_size,
					    char *data_buffer,
					    int data_buffer_size,
					    char *reply_buffer,
					    int reply_size);
extern unsigned int css_send_req_to_server_2_data (char *host,
						   int request,
						   char *arg_buffer,
						   int arg_buffer_size,
						   char *data1_buffer,
						   int data1_buffer_size,
						   char *data2_buffer,
						   int data2_buffer_size,
						   char *reply_buffer,
						   int reply_size);
extern unsigned int css_send_oob_to_server_with_buffer (char *host,
							int request,
							char *arg_buffer,
							int arg_buffer_size);
extern int css_queue_receive_data_buffer (unsigned int eid, char *buffer,
					  int buffer_size);
extern unsigned int css_send_error_to_server (char *host,
					      unsigned int eid, char *buffer,
					      int buffer_size);
extern unsigned int css_send_data_to_server (char *host,
					     unsigned int eid, char *buffer,
					     int buffer_size);
extern unsigned int css_receive_data_from_server (unsigned int eid,
						  char **buffer, int *size);
extern unsigned int css_receive_oob_from_server (unsigned int eid,
						 char **buffer, int *size);
extern unsigned int css_receive_error_from_server (unsigned int eid,
						   char **buffer, int *size);
extern void css_terminate (void);
extern void css_cleanup_client_queues (char *host_name);

#endif /* _TOP_H_ */
