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
 * connection_list_cl.h -
 */

#ifndef _CONNECTION_LIST_CL_H_
#define _CONNECTION_LIST_CL_H_

#ident "$Id$"

#include "connection_defs.h"

class connection_list_cl
{
private:
  CSS_QUEUE_ENTRY * css_make_queue_entry (unsigned int key, char *buffer, int size, CSS_QUEUE_ENTRY * next, int rc,
					  int transid, int invalidate_snapshot, int db_error);
  void css_free_queue_entry (CSS_QUEUE_ENTRY * entry_p);
  int css_add_entry_to_header (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id, char *buffer, int buffer_size,
			       int rc, int transid, int invalidate_snapshot, int db_error);
  bool css_is_request_aborted (CSS_CONN_ENTRY * conn, unsigned short request_id);
  void css_queue_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header);
  void css_queue_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header);
  void css_queue_command_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header, int size);
  void css_process_abort_packet (CSS_CONN_ENTRY * conn, unsigned short request_id);
  int css_queue_packet (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY ** queue_p, unsigned short request_id, char *buffer,
			int size, int rc);
  bool css_recv_and_queue_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *buffer, int size,
				  CSS_QUEUE_ENTRY ** queue_p);
  void css_process_close_packet (CSS_CONN_ENTRY * conn);

protected:
  char *css_get_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int *buffer_size);

  void css_queue_unexpected_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *header, int size,
					 int rc);
  void css_queue_unexpected_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *header, int size,
					  int rc);
  void css_queue_unexpected_packet (int type, CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header,
				    int size);
  void css_queue_remove_header_entry (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id);
  void css_queue_remove_header (CSS_QUEUE_ENTRY ** anchor);
  CSS_QUEUE_ENTRY *css_find_queue_entry (CSS_QUEUE_ENTRY * header, unsigned int key);
  void css_queue_remove_header_entry_ptr (CSS_QUEUE_ENTRY ** anchor, CSS_QUEUE_ENTRY * entry);

public:
    connection_list_cl () = default;
   ~connection_list_cl () = default;
  int css_queue_user_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int size, char *buffer);
  void css_queue_find_and_remove_header_entry_ptr (CSS_CONN_ENTRY * conn, unsigned short request_id);
};

#endif /* _CONNECTION_LIST_CL_H_ */
