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
#include "connection_cl.h"
#include "connection_less.h"

#if defined (SERVER_MODE)
#error Belongs to not server module
#endif

class client_support:public connection_less, public connection_cl
{
private:
  int m_multiple_count;
  CSS_MAP_ENTRY *css_Client_anchor;	// connection_less의 멤버로 이동하면 함수 호출이 단순해짐

private:
  static void css_handle_pipe_shutdown (int sig);
  void css_set_pipe_signal (void);
  int css_test_for_server_errors (CSS_MAP_ENTRY * entry, unsigned int eid);

//protected:
public:
  int css_Errno;

public:
    client_support ();
   ~client_support ();

  int css_set_client_multi_connect (int max)
  {
    if (max >= 1)
      m_multiple_count = max;
    return NO_ERROR;
  }

  int css_client_init (int sockid, const char *server_name, const char *host_name);
  unsigned int css_send_request_to_server_with_buffer (char *host, int request, char *arg_buffer,
						       int arg_buffer_size, char *data_buffer, int data_buffer_size);
  unsigned int css_send_req_to_server (char *host, int request, char *arg_buffer, int arg_buffer_size,
				       char *data_buffer, int data_buffer_size, char *reply_buffer, int reply_size);
  unsigned int css_send_req_to_server_2_data (char *host, int request, char *arg_buffer, int arg_buffer_size,
					      char *data1_buffer, int data1_buffer_size, char *data2_buffer,
					      int data2_buffer_size, char *reply_buffer, int reply_size);
  unsigned int css_send_req_to_server_no_reply (char *host, int request, char *arg_buffer, int arg_buffer_size);
  int css_queue_receive_data_buffer (unsigned int eid, char *buffer, int buffer_size);
  unsigned int css_send_error_to_server (char *host, unsigned int eid, char *buffer, int buffer_size);
  unsigned int css_send_data_to_server (char *host, unsigned int eid, char *buffer, int buffer_size);
  unsigned int css_receive_data_from_server (unsigned int eid, char **buffer, int *size);
  unsigned int css_receive_data_from_server_with_timeout (unsigned int eid, char **buffer, int *size, int timeout);
  void css_terminate (bool server_error);
  void css_cleanup_client_queues (char *host_name);

#if defined(ENABLE_UNUSED_FUNCTION)
  unsigned int css_send_request_to_server (char *host, int request, char *arg_buffer, int arg_buffer_size);
  unsigned int css_receive_error_from_server (unsigned int eid, char **buffer, int *size);
#endif
};

extern HA_SERVER_STATE css_ha_server_state (void);

#endif /* _CLIENT_SUPPORT_H_ */
