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

extern void css_queue_unexpected_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *header, int size,
					      int rc);
extern void css_queue_unexpected_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *header, int size,
					       int rc);
extern void css_queue_unexpected_packet (int type, CSS_CONN_ENTRY * conn, unsigned short request_id,
					 NET_HEADER * header, int size);
extern int css_queue_user_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int size, char *buffer);
extern CSS_QUEUE_ENTRY *css_find_queue_entry (CSS_QUEUE_ENTRY * header, unsigned int key);
extern void css_queue_remove_header_entry_ptr (CSS_QUEUE_ENTRY ** anchor, CSS_QUEUE_ENTRY * entry);
extern void css_queue_remove_header_entry (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id);
extern void css_queue_remove_header (CSS_QUEUE_ENTRY ** anchor);

#endif /* _CONNECTION_LIST_CL_H_ */
