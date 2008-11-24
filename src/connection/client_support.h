/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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

#endif /* _CLIENT_SUPPORT_H_ */
