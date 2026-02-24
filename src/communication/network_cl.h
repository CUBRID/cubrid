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
 * network_cl.h - Definitions for client network support
 */

#ifndef _NETWORK_CL_H_
#define _NETWORK_CL_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */
#if defined(SA_MODE)
#error Does not belong to sa mode
#endif

#include <stdio.h>

#include "dbtype_def.h"
#include "locator.h"
#include "log_writer.h"
#include "client_support.h"

class network_cl:public client_support
{
private:
/* Contains the name of the current sever host machine.  */
  char net_Server_host[CUB_MAXHOSTNAMELEN + 1];

/* Contains the name of the current server name. */
  char net_Server_name[DB_MAX_IDENTIFIER_LENGTH + 1];

private:
  void return_error_to_server (char *host, unsigned int eid);
  int client_capabilities (void);
  int check_server_capabilities (int server_cap, int client_type, int rel_compare,
				 REL_COMPATIBILITY * compatibility, const char *server_host, int opt_cap);
  int net_set_alloc_err_if_not_set (int err, const char *file, const int line);
  void net_consume_expected_packets (int rc, int num_packets);
  int compare_size_and_buffer (int *replysize, int size, char **replybuf, char *buf, const char *file, const int line);
  int net_client_request_internal (int request, char *argbuf, int argsize, char *replybuf, int replysize,
				   char *databuf, int datasize, char *replydata, int replydatasize);
  int set_server_error (int error);

public:
    network_cl ();
   ~network_cl ();

  int net_client_request_no_reply (int request, char *argbuf, int argsize);
  int net_client_request (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf,
			  int datasize, char *replydata, int replydatasize);
#if defined(ENABLE_UNUSED_FUNCTION)
  int net_client_request_send_large_data (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					  char *databuf, INT64 datasize, char *replydata, int replydatasize);
#endif
  int net_client_request2 (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf,
			   int datasize, char **replydata_ptr, int *replydatasize_ptr);
  int net_client_request2_no_malloc (int request, char *argbuf, int argsize, char *replybuf, int replysize,
				     char *databuf, int datasize, char *replydata, int *replydatasize_ptr);
  int net_client_request_3_data (int request, char *argbuf, int argsize, char *databuf1, int datasize1,
				 char *databuf2, int datasize2, char *replydata0, int replydatasize0,
				 char *replydata1, int replydatasize1, char *replydata2, int replydatasize2);
  int net_client_request_with_callback (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					char *databuf1, int datasize1, char *databuf2, int datasize2,
					char **replydata_ptr1, int *replydatasize_ptr1, char **replydata_ptr2,
					int *replydatasize_ptr2, char **replydata_ptr3, int *replydatasize_ptr3);
  int net_client_request_method_callback (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					  char **replydata_ptr, int *replydatasize_ptr);
  int net_client_check_log_header (LOGWR_CONTEXT * ctx_ptr, char *argbuf, int argsize, char *replybuf,
				   int replysize, char **logpg_area_buf, bool verbose);
  int net_client_request_with_logwr_context (LOGWR_CONTEXT * ctx_ptr, int request, char *argbuf, int argsize,
					     char *replybuf, int replysize, char *databuf1, int datasize1,
					     char *databuf2, int datasize2);
  void net_client_logwr_send_end_msg (int rc, int error);
  int net_client_get_next_log_pages (int rc, char *replybuf, int replysize, int length);
#if defined(ENABLE_UNUSED_FUNCTION)
  int net_client_request3 (int request, char *argbuf, int argsize, char *replybuf, int replysize, char *databuf,
			   int datasize, char **replydata_ptr, int *replydatasize_ptr, char **replydata_ptr2,
			   int *replydatasize_ptr2);
#endif

  int net_client_request_recv_copyarea (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					LC_COPYAREA ** reply_copy_area);
#if defined(ENABLE_UNUSED_FUNCTION)
  int net_client_request_recv_large_data (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					  char *databuf, int datasize, char *replydata, INT64 * replydatasize_ptr);
#endif
  int net_client_request_2recv_copyarea (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					 char *databuf, int datasize, char *recvbuffer, int recvbuffer_size,
					 LC_COPYAREA ** reply_copy_area, int *eid);
  int net_client_recv_copyarea (int request, char *replybuf, int replysize, char *recvbuffer, int recvbuffer_size,
				LC_COPYAREA ** reply_copy_area, int eid);
  int net_client_request_3_data_recv_copyarea (int request, char *argbuf, int argsize, char *databuf1,
					       int datasize1, char *databuf2, int datasize2, char *replybuf,
					       int replysize, LC_COPYAREA ** reply_copy_area);
  int net_client_request_3recv_copyarea (int request, char *argbuf, int argsize, char *replybuf, int replysize,
					 char *databuf, int datasize, char **recvbuffer, int *recvbuffer_size,
					 LC_COPYAREA ** reply_copy_area);
  int net_client_request_recv_stream (int request, char *argbuf, int argsize, char *replybuf, int replybuf_size,
				      char *databuf, int datasize, FILE * outfp);
  int net_client_ping_server (int client_val, int *server_val, int timeout);
  int net_client_ping_server_with_handshake (int client_type, bool check_capabilities, int opt_cap);

/* Startup/Shutdown */
#if defined(ENABLE_UNUSED_FUNCTION)
  void net_client_shutdown_server (void);
#endif
  int net_client_init (const char *dbname, const char *hostname);
  int net_client_final (void);

  void net_cleanup_client_queues (void);
  int net_client_send_data (unsigned int rc, char *databuf, int datasize);
  int net_client_receive_action (int rc, int *action);
};

extern class network_cl __gv_network_cl;

#endif /* _NETWORK_CL_H_ */

